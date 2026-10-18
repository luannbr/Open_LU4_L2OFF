#include "ExceptionReport.h"

#include "AuthGateConfig.h"
#include "MailReport.h"
#include "Platform/LegacyWideFormat.h"

#include "Shared/src/Runtime/WindowsAnsi.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#  include <Lmcons.h>
#else
#  include <csignal>
#  include <exception>
#  include <execinfo.h>
#  include <pwd.h>
#  include <unistd.h>
#  if defined(__APPLE__)
#    include <mach-o/dyld.h>
#  endif
#endif

std::array<char16_t, 260>
    CExceptionReport::m_szLogPrefixName{};
std::array<char16_t, 260>
    CExceptionReport::m_szModuleName{};
CExceptionReport::UserFunction
    CExceptionReport::m_lpUserFunc = nullptr;
void* CExceptionReport::m_hProcess = nullptr;
void* CExceptionReport::m_hReportFile = nullptr;
CExceptionReport::ExceptionFilter
    CExceptionReport::m_OldFilter = nullptr;
int CExceptionReport::m_bHasSymbol = 0;

namespace {

constexpr char16_t kSeparator[] =
    u"------------------------------------------------------------------------------\r\n";
constexpr char16_t kDoubleSeparator[] =
    u"==============================================================================\r\n";

// Crash fallback: the normal logger, allocation, stdio and clock/TLS locks may
// be unavailable. Keep one bounded best-effort stderr write, with no retry or
// recursive error reporting. UTC/thread decoration belongs to normal logging.
// This does not make the existing symbol/report/mail pipeline signal-safe.
void WriteCrashDiagnostic(
    const char* severity,
    const char* message,
    std::u16string_view path = {},
    const char* detail = nullptr,
    std::uint64_t value = 0) noexcept {
    const int savedErrno = errno;
#if defined(_WIN32)
    const DWORD savedError = ::GetLastError();
#endif
    char record[2048];
    std::size_t used = 0;
    bool truncated = false;
    constexpr std::size_t limit = sizeof(record) - 24;
    const auto append = [&](const char* data, std::size_t size) {
        if (size > limit - used) { truncated = true; return false; }
        for (std::size_t i = 0; i < size; ++i) record[used++] = data[i];
        return true;
    };
    const auto text = [&](const char* data) {
        std::size_t size = 0;
        while (size < limit && data[size]) ++size;
        if (size == limit) truncated = true;
        append(data, size);
    };
    text(severity);
    text(" [crash] ");
    text(message);
    if (detail) {
        text(" ("); text(detail); text(" ");
        char digits[20];
        std::size_t count = 0;
        do { digits[count++] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
        while (count) append(&digits[--count], 1);
        text(")");
    }
    if (!path.empty()) {
        text(" Path: \"");
        constexpr char hex[] = "0123456789ABCDEF";
        for (std::size_t i = 0; i < path.size(); ++i) {
            std::uint32_t cp = path[i];
            if (cp >= 0xd800 && cp <= 0xdbff && i + 1 < path.size() &&
                path[i + 1] >= 0xdc00 && path[i + 1] <= 0xdfff) {
                cp = 0x10000 + ((cp - 0xd800) << 10) + (path[++i] - 0xdc00);
            }
            char encoded[6];
            std::size_t size = 0;
            if (cp < 0x20 || cp == 0x7f || (cp >= 0xd800 && cp <= 0xdfff)) {
                encoded[0] = '\\'; encoded[1] = 'u';
                for (unsigned j = 0; j < 4; ++j) encoded[2 + j] = hex[(cp >> (12 - 4 * j)) & 15];
                size = 6;
            } else if (cp == '"' || cp == '\\') {
                encoded[0] = '\\'; encoded[1] = static_cast<char>(cp); size = 2;
            } else if (cp < 0x80) {
                encoded[0] = static_cast<char>(cp); size = 1;
            } else if (cp < 0x800) {
                encoded[0] = static_cast<char>(0xc0 | (cp >> 6));
                encoded[1] = static_cast<char>(0x80 | (cp & 63)); size = 2;
            } else if (cp < 0x10000) {
                encoded[0] = static_cast<char>(0xe0 | (cp >> 12));
                encoded[1] = static_cast<char>(0x80 | ((cp >> 6) & 63));
                encoded[2] = static_cast<char>(0x80 | (cp & 63)); size = 3;
            } else {
                encoded[0] = static_cast<char>(0xf0 | (cp >> 18));
                encoded[1] = static_cast<char>(0x80 | ((cp >> 12) & 63));
                encoded[2] = static_cast<char>(0x80 | ((cp >> 6) & 63));
                encoded[3] = static_cast<char>(0x80 | (cp & 63)); size = 4;
            }
            if (!append(encoded, size)) break;
        }
        text("\"");
    }
    if (truncated) {
        constexpr char marker[] = " [truncated]";
        for (std::size_t i = 0; i < sizeof(marker) - 1; ++i) record[used++] = marker[i];
    }
    record[used++] = '\n';
#if defined(_WIN32)
    DWORD written = 0;
    ::WriteFile(::GetStdHandle(STD_ERROR_HANDLE), record,
        static_cast<DWORD>(used), &written, nullptr);
    ::SetLastError(savedError);
#else
    (void)::write(STDERR_FILENO, record, used);
#endif
    errno = savedErrno;
}

// Diagnostic-only state. Never alter the recovered report object/layout or
// stop writing after a failure. Noise: emit the first fragment failure only.
std::atomic<bool> g_reportOutputFailed{false};
static_assert(std::atomic<bool>::is_always_lock_free);

void ReportCrashOutputFailure(
    const char* message,
    const char* detail = nullptr,
    std::uint64_t value = 0) noexcept {
    if (!g_reportOutputFailed.exchange(true, std::memory_order_relaxed)) {
        WriteCrashDiagnostic("ERROR", message, {}, detail, value);
    }
}

template <std::size_t Capacity>
void CopyTargetString(
    std::array<char16_t, Capacity>& destination,
    const char16_t* source) noexcept {
    destination.fill(u'\0');
    if (!source) return;
    std::size_t index = 0;
    while (index + 1U < Capacity && source[index] != u'\0') {
        destination[index] = source[index];
        ++index;
    }
}

std::u16string TargetFormat(
    const char16_t* format,
    ...) {
    std::u16string output;
    va_list arguments;
    va_start(arguments, format);
    // Original: authgated::portable::FormatTargetWide(output, 4095, format, arguments)
    try {
        authgated::portable::FormatTargetWide(output, 4095, format, arguments);
    } catch (...) {
        va_end(arguments);
        throw;
    }
    va_end(arguments);
    return output;
}

std::filesystem::path ExecutablePath() {
#if defined(_WIN32)
    std::array<wchar_t, 32768> path{};
    const DWORD length = ::GetModuleFileNameW(
        nullptr,
        path.data(),
        static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return {};
    return std::filesystem::path(path.data());
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    ::_NSGetExecutablePath(nullptr, &size);
    if (size == 0) return {};
    std::string path(size, '\0');
    if (::_NSGetExecutablePath(path.data(), &size) != 0) return {};
    path.resize(std::strlen(path.c_str()));
    return path;
#elif defined(__linux__)
    std::array<char, 4096> path{};
    const ssize_t length = ::readlink(
        "/proc/self/exe",
        path.data(),
        path.size() - 1U);
    if (length <= 0) return {};
    path[static_cast<std::size_t>(length)] = '\0';
    return path.data();
#else
    return {};
#endif
}

std::u16string CurrentUserName() {
#if defined(_WIN32)
    std::array<wchar_t, UNLEN + 1> value{};
    DWORD length = static_cast<DWORD>(value.size());
    if (!::GetUserNameW(value.data(), &length)) return u"Unknown";
    return std::u16string(
        reinterpret_cast<const char16_t*>(value.data()));
#else
    if (passwd* const entry = ::getpwuid(::geteuid());
        entry && entry->pw_name) {
        return Utf8ToUtf16(entry->pw_name);
    }
    return u"Unknown";
#endif
}

std::u16string CurrentComputerName() {
#if defined(_WIN32)
    std::array<wchar_t, 260> value{};
    DWORD length = static_cast<DWORD>(value.size());
    if (!::GetComputerNameW(value.data(), &length)) return u"Unknown";
    return std::u16string(
        reinterpret_cast<const char16_t*>(value.data()),
        length);
#else
    std::array<char, 260> value{};
    if (::gethostname(value.data(), value.size()) != 0) {
        return u"Unknown";
    }
    return Utf8ToUtf16(value.data());
#endif
}

std::u16string CrashFileName(
    const std::array<char16_t, 260>& prefix,
    const std::tm& now,
    const char16_t* extension) {
    return TargetFormat(
        u"%s %04d-%02d-%02d %02d,%02d,%02d.%s",
        prefix.data(),
        now.tm_year + 1900,
        now.tm_mon + 1,
        now.tm_mday,
        now.tm_hour,
        now.tm_min,
        now.tm_sec,
        extension);
}

#if !defined(_WIN32)
std::string NativePath(const std::u16string& value) {
    return Utf16ToUtf8(value);
}

constexpr std::array<int, 5> kFatalSignals{{
    SIGABRT,
    SIGBUS,
    SIGFPE,
    SIGILL,
    SIGSEGV,
}};
std::array<struct sigaction, kFatalSignals.size()>
    g_previousSignalActions{};
std::terminate_handler g_previousTerminateHandler = nullptr;
std::atomic_flag g_crashActive = ATOMIC_FLAG_INIT;

void RestoreAndRaise(int signalNumber) noexcept {
    struct sigaction defaultAction {};
    defaultAction.sa_handler = SIG_DFL;
    sigemptyset(&defaultAction.sa_mask);
    ::sigaction(signalNumber, &defaultAction, nullptr);
    ::raise(signalNumber);
    ::_exit(128 + signalNumber);
}

void PortableSignalHandler(
    int signalNumber,
    [[maybe_unused]] siginfo_t* info,
    [[maybe_unused]] void* context) {
    if (!g_crashActive.test_and_set(std::memory_order_acq_rel)) {
        _EXCEPTION_RECORD record{};
        record.ExceptionCode = static_cast<std::uint32_t>(signalNumber);
        _EXCEPTION_POINTERS pointers{};
        pointers.ExceptionRecord = &record;
        CExceptionReport::UnhandledExceptionFilter(&pointers);
    }
    RestoreAndRaise(signalNumber);
}

[[noreturn]] void PortableTerminateHandler() noexcept {
    if (!g_crashActive.test_and_set(std::memory_order_acq_rel)) {
        CExceptionReport::UnhandledExceptionFilter(nullptr);
    }
    std::_Exit(EXIT_FAILURE);
}
#endif

} // namespace

// AuthGateD.exe ExceptionReport.obj $E1/$E2. The compiler inlined these two
// one-byte object methods into the module's initializer and exit callback.
CExceptionReport::CExceptionReport() {
    m_bHasSymbol = 0;
    SetProgramName(nullptr);
    m_lpUserFunc = nullptr;
#if defined(_WIN32)
    m_OldFilter = reinterpret_cast<ExceptionFilter>(
        ::SetUnhandledExceptionFilter(
            reinterpret_cast<LPTOP_LEVEL_EXCEPTION_FILTER>(
                UnhandledExceptionFilter)));
    m_hProcess = ::GetCurrentProcess();
#else
    m_hProcess = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(::getpid()));
    g_previousTerminateHandler =
        std::set_terminate(PortableTerminateHandler);
    struct sigaction action {};
    action.sa_sigaction = PortableSignalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    for (std::size_t index = 0; index < kFatalSignals.size(); ++index) {
        ::sigaction(
            kFatalSignals[index],
            &action,
            &g_previousSignalActions[index]);
    }
#endif
}

CExceptionReport::~CExceptionReport() {
#if defined(_WIN32)
    ::SetUnhandledExceptionFilter(
        reinterpret_cast<LPTOP_LEVEL_EXCEPTION_FILTER>(m_OldFilter));
#else
    for (std::size_t index = 0; index < kFatalSignals.size(); ++index) {
        ::sigaction(
            kFatalSignals[index],
            &g_previousSignalActions[index],
            nullptr);
    }
    std::set_terminate(g_previousTerminateHandler);
#endif
}

// AuthGateD.exe 0x00413D20, ExceptionReport.cpp.
void CExceptionReport::SetProgramName(
    const char16_t* programName) {
    if (programName) {
        CopyTargetString(m_szLogPrefixName, programName);
        return;
    }
    const std::filesystem::path executable = ExecutablePath();
    const std::u16string stem =
        Utf8ToUtf16(executable.stem().string());
    CopyTargetString(m_szLogPrefixName, stem.c_str());
}

// AuthGateD.exe 0x00413DE0, ExceptionReport.cpp.
char16_t* CExceptionReport::GetExceptionString(
    std::uint32_t exceptionCode) {
    struct Entry {
        std::uint32_t code;
        const char16_t* name;
    };
    static constexpr Entry entries[] = {
        {UINT32_C(0x80000001), u"GUARD_PAGE"},
        {UINT32_C(0x80000002), u"DATATYPE_MISALIGNMENT"},
        {UINT32_C(0x80000003), u"BREAKPOINT"},
        {UINT32_C(0x80000004), u"SINGLE_STEP"},
        {UINT32_C(0xc0000005), u"ACCESS_VIOLATION"},
        {UINT32_C(0xc0000006), u"IN_PAGE_ERROR"},
        {UINT32_C(0xc0000008), u"INVALID_HANDLE"},
        {UINT32_C(0xc000001d), u"ILLEGAL_INSTRUCTION"},
        {UINT32_C(0xc0000025), u"NONCONTINUABLE_EXCEPTION"},
        {UINT32_C(0xc0000026), u"INVALID_DISPOSITION"},
        {UINT32_C(0xc000008c), u"ARRAY_BOUNDS_EXCEEDED"},
        {UINT32_C(0xc000008d), u"FLT_DENORMAL_OPERAND"},
        {UINT32_C(0xc000008e), u"FLT_DIVIDE_BY_ZERO"},
        {UINT32_C(0xc000008f), u"FLT_INEXACT_RESULT"},
        {UINT32_C(0xc0000090), u"FLT_INVALID_OPERATION"},
        {UINT32_C(0xc0000091), u"FLT_OVERFLOW"},
        {UINT32_C(0xc0000092), u"FLT_STACK_CHECK"},
        {UINT32_C(0xc0000093), u"FLT_UNDERFLOW"},
        {UINT32_C(0xc0000094), u"INT_DIVIDE_BY_ZERO"},
        {UINT32_C(0xc0000095), u"INT_OVERFLOW"},
        {UINT32_C(0xc0000096), u"PRIV_INSTRUCTION"},
        {UINT32_C(0xc00000fd), u"STACK_OVERFLOW"},
    };
    for (const Entry& entry : entries) {
        if (entry.code == exceptionCode) {
            return const_cast<char16_t*>(entry.name);
        }
    }

    static std::array<char16_t, 2048> buffer{};
    buffer.fill(u'\0');
#if defined(_WIN32)
    ::FormatMessageW(
        FORMAT_MESSAGE_FROM_HMODULE |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        ::GetModuleHandleW(L"NTDLL.DLL"),
        exceptionCode,
        0,
        reinterpret_cast<wchar_t*>(buffer.data()),
        static_cast<DWORD>(buffer.size()),
        nullptr);
#else
    const std::u16string value = TargetFormat(
        u"signal/exception %08X",
        exceptionCode);
    CopyTargetString(buffer, value.c_str());
#endif
    return buffer.data();
}

// AuthGateD.exe 0x00414050, ExceptionReport.cpp.
int CExceptionReport::GetLogicalAddress(
    const void* address,
    char16_t* moduleName,
    std::uint32_t moduleNameSize,
    std::uint32_t& section,
    std::uint32_t& offset) {
    section = 0;
    offset = 0;
    if (!moduleName || moduleNameSize == 0) return 0;
#if defined(_WIN32)
    MEMORY_BASIC_INFORMATION memory{};
    if (!::VirtualQuery(address, &memory, sizeof(memory))) return 0;
    const DWORD capacity = std::min<DWORD>(
        moduleNameSize,
        260);
    if (!::GetModuleFileNameW(
            static_cast<HMODULE>(memory.AllocationBase),
            reinterpret_cast<wchar_t*>(moduleName),
            capacity)) {
        return 0;
    }
    const auto* const base = static_cast<const std::uint8_t*>(
        memory.AllocationBase);
    const auto* const dos =
        reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        base + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);
    const std::uintptr_t relative =
        static_cast<const std::uint8_t*>(address) - base;
    for (std::uint32_t index = 0;
         index < nt->FileHeader.NumberOfSections;
         ++index) {
        const std::uint32_t size = std::max(
            sections[index].SizeOfRawData,
            sections[index].Misc.VirtualSize);
        if (relative >= sections[index].VirtualAddress &&
            relative <= sections[index].VirtualAddress + size) {
            section = index + 1U;
            offset = static_cast<std::uint32_t>(relative) -
                sections[index].VirtualAddress;
            return 1;
        }
    }
    return 0;
#else
    const std::u16string path =
        Utf8ToUtf16(ExecutablePath().string());
    const std::size_t capacity =
        std::min<std::size_t>(moduleNameSize, 260U);
    const std::size_t count = std::min(
        path.size(),
        capacity - 1U);
    std::copy_n(path.data(), count, moduleName);
    moduleName[count] = u'\0';
    offset = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(address));
    return 1;
#endif
}

// AuthGateD.exe 0x00414120, ExceptionReport.cpp.
char16_t* CExceptionReport::FormatOutputValue(
    char16_t* output,
    BasicType basicType,
    std::uint64_t size,
    const void* value) {
    if (!output || !value) return output;
    std::u16string formatted;
    switch (size) {
    case 1:
        formatted = TargetFormat(
            u" = %X",
            static_cast<unsigned int>(
                *static_cast<const std::uint8_t*>(value)));
        break;
    case 2:
        formatted = TargetFormat(
            u" = %X",
            static_cast<unsigned int>(
                *static_cast<const std::uint16_t*>(value)));
        break;
    case 4:
        if (basicType == btFloat) {
            formatted = TargetFormat(
                u" = %f",
                static_cast<double>(
                    *static_cast<const float*>(value)));
        } else if (basicType == btChar) {
            const char* const string =
                *static_cast<const char* const*>(value);
#if defined(_WIN32)
            if (string && !::IsBadStringPtrA(string, 32)) {
#else
            if (string) {
#endif
                formatted = TargetFormat(u" = \"%.31S\"", string);
            } else {
                formatted = TargetFormat(
                    u" = %X",
                    *static_cast<const std::uint32_t*>(value));
            }
        } else {
            formatted = TargetFormat(
                u" = %X",
                *static_cast<const std::uint32_t*>(value));
        }
        break;
    case 8:
        if (basicType == btFloat) {
            formatted = TargetFormat(
                u" = %lf",
                *static_cast<const double*>(value));
        } else {
            formatted = TargetFormat(
                u" = %I64X",
                *static_cast<const std::uint64_t*>(value));
        }
        break;
    default:
        return output;
    }
    std::copy(formatted.begin(), formatted.end(), output);
    return output + formatted.size();
}

// AuthGateD.exe 0x00414240, ExceptionReport.cpp.
BasicType CExceptionReport::GetBasicType(
    std::uint32_t typeIndex,
    std::uint64_t moduleBase) {
#if defined(_WIN32)
    BasicType basicType = btNoType;
    if (::SymGetTypeInfo(
            static_cast<HANDLE>(m_hProcess),
            moduleBase,
            typeIndex,
            TI_GET_BASETYPE,
            &basicType)) {
        return basicType;
    }
    std::uint32_t underlyingType = 0;
    if (::SymGetTypeInfo(
            static_cast<HANDLE>(m_hProcess),
            moduleBase,
            typeIndex,
            TI_GET_TYPEID,
            &underlyingType) &&
        ::SymGetTypeInfo(
            static_cast<HANDLE>(m_hProcess),
            moduleBase,
            underlyingType,
            TI_GET_BASETYPE,
            &basicType)) {
        return basicType;
    }
#else
    [[maybe_unused]] const std::uint32_t unusedTypeIndex = typeIndex;
    [[maybe_unused]] const std::uint64_t unusedModuleBase = moduleBase;
#endif
    return btNoType;
}

// AuthGateD.exe 0x004142C0, ExceptionReport.cpp.
int CExceptionReport::_tprintfile(
    const char16_t* format,
    ...) {
    if (!m_hReportFile || !format) return 0;
    std::u16string text;
    va_list arguments;
    va_start(arguments, format);
    // authgated::portable::FormatTargetWide(
    //     text,
    //     0x1000,
    //     format,
    //     arguments)
    // Original: const bool formatted = authgated::portable::FormatTargetWide(
    //     text, 0x1000, format, arguments)
    bool formatted;
    try {
        formatted = authgated::portable::FormatTargetWide(text, 0x1000, format, arguments);
    } catch (...) {
        va_end(arguments);
        throw;
    }
    va_end(arguments);
    if (!formatted) {
        ReportCrashOutputFailure("A crash-report fragment could not be formatted completely; the report may be incomplete.");
    }
    if (text.empty()) return 0;
#if defined(_WIN32)
    DWORD written = 0;
    // ::WriteFile(
    //     static_cast<HANDLE>(m_hReportFile),
    //     text.data(),
    //     static_cast<DWORD>(text.size() * sizeof(char16_t)),
    //     &written,
    //     nullptr)
    const BOOL writeOk = ::WriteFile(
        static_cast<HANDLE>(m_hReportFile),
        text.data(),
        static_cast<DWORD>(text.size() * sizeof(char16_t)),
        &written,
        nullptr);
    if (!writeOk) {
        ReportCrashOutputFailure("Crash-report write failed; the report may be incomplete.", "Win32 error", ::GetLastError());
    } else if (written != text.size() * sizeof(char16_t)) {
        ReportCrashOutputFailure("Crash-report write was short; the report may be incomplete.");
    }
#else
    std::FILE* const file = static_cast<std::FILE*>(m_hReportFile);
    for (const char16_t value : text) {
        const std::array<unsigned char, 2> encoded{{
            static_cast<unsigned char>(value & 0xffU),
            static_cast<unsigned char>(value >> 8U),
        }};
        // std::fwrite(encoded.data(), 1, encoded.size(), file)
        const std::size_t written = std::fwrite(encoded.data(), 1, encoded.size(), file);
        if (written != encoded.size()) {
            ReportCrashOutputFailure("Crash-report write was short; the report may be incomplete.", "errno", errno);
        }
    }
    // std::fflush(file)
    const int flushResult = std::fflush(file);
    if (flushResult != 0) {
        ReportCrashOutputFailure("Crash-report flush failed; the report may be incomplete.", "errno", errno);
    }
#endif
    return static_cast<int>(text.size());
}

// AuthGateD.exe 0x00414340, ExceptionReport.cpp.
std::int32_t CExceptionReport::UnhandledExceptionFilterTwo(
    _EXCEPTION_POINTERS* exceptionPointers) {
    if (m_hReportFile) {
        // std::fputs(
        //     "Exception occured AGAIN! EXITING NOW!\r\n",
        //     stderr)
        WriteCrashDiagnostic("ERROR", "A second exception interrupted crash reporting; closing the incomplete report.");
#if defined(_WIN32)
        // ::CloseHandle(static_cast<HANDLE>(m_hReportFile))
        const BOOL closeOk = ::CloseHandle(static_cast<HANDLE>(m_hReportFile));
        if (!closeOk) {
            WriteCrashDiagnostic("ERROR", "Could not close the interrupted crash report.", {}, "Win32 error", ::GetLastError());
        }
#else
        // std::fclose(static_cast<std::FILE*>(m_hReportFile))
        const int closeResult = std::fclose(static_cast<std::FILE*>(m_hReportFile));
        if (closeResult != 0) {
            WriteCrashDiagnostic("ERROR", "Could not close the interrupted crash report.", {}, "errno", errno);
        }
#endif
        m_hReportFile = nullptr;
    }
    return m_OldFilter
        ? m_OldFilter(exceptionPointers)
        : 0;
}

// AuthGateD.exe 0x00414380, ExceptionReport.cpp.
void CExceptionReport::WriteBasicInfo(
    _EXCEPTION_RECORD* exceptionRecord) {
    const std::u16string executable =
        Utf8ToUtf16(ExecutablePath().string());
    const std::u16string user = CurrentUserName();
    const std::u16string computer = CurrentComputerName();
    const void* const faultAddress = exceptionRecord
        ? exceptionRecord->ExceptionAddress
        : nullptr;
    const std::uint32_t exceptionCode = exceptionRecord
        ? exceptionRecord->ExceptionCode
        : 0;
    std::uint32_t section = 0;
    std::uint32_t offset = 0;
    GetLogicalAddress(
        faultAddress,
        m_szModuleName.data(),
        0x208,
        section,
        offset);

    _tprintfile(kSeparator);
    _tprintfile(u"    Basic Information\r\n");
    _tprintfile(kSeparator);
    _tprintfile(u"\r\n");
    _tprintfile(u"Program Name : %s\r\n", m_szLogPrefixName.data());
    _tprintfile(u"EXE          : %s\r\n", executable.c_str());
    _tprintfile(u"User         : %s\r\n", user.c_str());
    _tprintfile(u"Computer     : %s\r\n", computer.c_str());
    _tprintfile(u"\r\n");
    _tprintfile(u"Program      : %s\r\n", m_szModuleName.data());
    _tprintfile(
        u"Exception    : %08X (%s)\r\n",
        exceptionCode,
        GetExceptionString(exceptionCode));
    _tprintfile(
        u"Fault Address: %08X %02X:%08X\r\n",
        static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(faultAddress)),
        section,
        offset);
    _tprintfile(u"\r\n\r\n");
}

// AuthGateD.exe 0x00414530, ExceptionReport.cpp.
void CExceptionReport::WriteRegistersInfo(_CONTEXT* context) {
    _tprintfile(kSeparator);
    _tprintfile(u"    x86 Registers\r\n");
    _tprintfile(kSeparator);
    _tprintfile(u"\r\n");
#if defined(_WIN32) && (defined(_M_IX86) || defined(__i386__))
    if (context) {
        _tprintfile(
            u"EAX=%08X  EBX=%08X  ECX=%08X  EDX=%08X\r\n",
            context->Eax,
            context->Ebx,
            context->Ecx,
            context->Edx);
        _tprintfile(
            u"ESI=%08X  EDI=%08X  EBP=%08X\r\n",
            context->Esi,
            context->Edi,
            context->Ebp);
        _tprintfile(
            u"DS =%04X      ES=%04X       FS=%04X       GS:%04X\r\n",
            context->SegDs,
            context->SegEs,
            context->SegFs,
            context->SegGs);
        _tprintfile(
            u"CS:EIP=%04X:%08X\r\n",
            context->SegCs,
            context->Eip);
        _tprintfile(
            u"SS:ESP=%04X:%08X\r\n",
            context->SegSs,
            context->Esp);
        _tprintfile(u"Flags=%08X\r\n", context->EFlags);
    }
#else
    [[maybe_unused]] _CONTEXT* const unavailableContext = context;
    _tprintfile(u"Native register set is not x86.\r\n");
#endif
    _tprintfile(u"\r\n\r\n");
}

// AuthGateD.exe 0x00414630, ExceptionReport.cpp.
char16_t* CExceptionReport::DumpTypeIndex(
    char16_t* output,
    std::uint64_t moduleBase,
    std::uint32_t typeIndex,
    std::uint32_t nesting,
    std::uint32_t address,
    int& expanded) {
    expanded = 0;
#if defined(_WIN32)
    wchar_t* symbolName = nullptr;
    if (::SymGetTypeInfo(
            static_cast<HANDLE>(m_hProcess),
            moduleBase,
            typeIndex,
            TI_GET_SYMNAME,
            &symbolName)) {
        const std::u16string name = TargetFormat(
            u" %s",
            reinterpret_cast<char16_t*>(symbolName));
        std::copy(name.begin(), name.end(), output);
        output += name.size();
        ::LocalFree(symbolName);
    }

    std::uint32_t childCount = 0;
    ::SymGetTypeInfo(
        static_cast<HANDLE>(m_hProcess),
        moduleBase,
        typeIndex,
        TI_GET_CHILDRENCOUNT,
        &childCount);
    if (childCount != 0) {
        const std::size_t bytes =
            sizeof(TI_FINDCHILDREN_PARAMS) +
            (static_cast<std::size_t>(childCount) - 1U) *
                sizeof(std::uint32_t);
        std::vector<std::uint8_t> storage(bytes);
        auto* const children =
            reinterpret_cast<TI_FINDCHILDREN_PARAMS*>(storage.data());
        children->Count = childCount;
        children->Start = 0;
        if (::SymGetTypeInfo(
                static_cast<HANDLE>(m_hProcess),
                moduleBase,
                typeIndex,
                TI_FINDCHILDREN,
                children)) {
            *output++ = u'\r';
            *output++ = u'\n';
            for (std::uint32_t index = 0; index < childCount; ++index) {
                for (std::uint32_t tab = 0; tab <= nesting; ++tab) {
                    *output++ = u'\t';
                }
                int childExpanded = 0;
                const std::uint32_t child = children->ChildId[index];
                output = DumpTypeIndex(
                    output,
                    moduleBase,
                    child,
                    nesting + 1U,
                    address,
                    childExpanded);
                if (!childExpanded) {
                    std::uint32_t memberOffset = 0;
                    std::uint32_t memberType = 0;
                    std::uint64_t length = 0;
                    ::SymGetTypeInfo(
                        static_cast<HANDLE>(m_hProcess),
                        moduleBase,
                        child,
                        TI_GET_OFFSET,
                        &memberOffset);
                    ::SymGetTypeInfo(
                        static_cast<HANDLE>(m_hProcess),
                        moduleBase,
                        child,
                        TI_GET_TYPEID,
                        &memberType);
                    ::SymGetTypeInfo(
                        static_cast<HANDLE>(m_hProcess),
                        moduleBase,
                        memberType,
                        TI_GET_LENGTH,
                        &length);
                    output = FormatOutputValue(
                        output,
                        GetBasicType(child, moduleBase),
                        length,
                        reinterpret_cast<const void*>(
                            static_cast<std::uintptr_t>(
                                address + memberOffset)));
                    *output++ = u'\r';
                    *output++ = u'\n';
                }
            }
            expanded = 1;
        }
    }
#else
    [[maybe_unused]] const std::uint64_t unusedModuleBase = moduleBase;
    [[maybe_unused]] const std::uint32_t unusedTypeIndex = typeIndex;
    [[maybe_unused]] const std::uint32_t unusedNesting = nesting;
    [[maybe_unused]] const std::uint32_t unusedAddress = address;
#endif
    return output;
}

// AuthGateD.exe 0x00414870, ExceptionReport.cpp.
void CExceptionReport::Dump(
    std::uint64_t address,
    std::uint32_t byteCount,
    int alignAddress) {
#if defined(_WIN32)
    if (alignAddress == 1) address &= UINT64_C(0xfffffff0);
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(
        static_cast<std::uintptr_t>(address));
    for (std::uint32_t row = 0; row < byteCount; row += 16U) {
        _tprintfile(
            u"%08lX: ",
            static_cast<std::uint32_t>(address + row));
        for (std::uint32_t column = 0; column < 16U; ++column) {
            if (column == 8U) _tprintfile(u" ");
            if (row + column >= byteCount) {
                _tprintfile(u"?? ");
            } else {
                _tprintfile(u"%02X ", bytes[row + column]);
            }
        }
        _tprintfile(u" ");
        for (std::uint32_t column = 0; column < 16U; ++column) {
            if (row + column >= byteCount) {
                _tprintfile(u" ");
            } else {
                const unsigned char value = bytes[row + column];
                _tprintfile(
                    u"%c",
                    std::isprint(value) ? value : '.');
            }
        }
        _tprintfile(u"\r\n");
    }
#else
    [[maybe_unused]] const std::uint64_t unavailableAddress = address;
    [[maybe_unused]] const std::uint32_t unavailableByteCount = byteCount;
    [[maybe_unused]] const int unavailableAlignment = alignAddress;
#endif
}

// AuthGateD.exe 0x004149A0, ExceptionReport.cpp.
void CExceptionReport::WriteMemoryDump(_CONTEXT* context) {
    _tprintfile(kSeparator);
    _tprintfile(u"    Memory Dump\r\n");
    _tprintfile(kSeparator);
    _tprintfile(u"\r\n");
#if defined(_WIN32) && (defined(_M_IX86) || defined(__i386__))
    if (context) {
        _tprintfile(
            u"Code: %d bytes starting at (EIP = %08lX)\r\n",
            16,
            context->Eip);
        Dump(context->Eip, 16, 0);
        _tprintfile(u"\r\n");
        _tprintfile(
            u"Stack: %d bytes starting at (ESP = %08lX)\r\n",
            0x400,
            context->Esp);
        Dump(context->Esp, 0x400, 1);
    }
#else
    [[maybe_unused]] _CONTEXT* const unavailableContext = context;
    _tprintfile(u"Native memory dump is unavailable on this host.\r\n");
#endif
    _tprintfile(u"\r\n\r\n");
}

// AuthGateD.exe 0x00414A50, ExceptionReport.cpp.
int CExceptionReport::FormatSymbolValue(
    _SYMBOL_INFO* symbol,
    ExceptionReportStackFrame* frame,
    char16_t* output,
    [[maybe_unused]] std::uint32_t outputSize) {
#if defined(_WIN32)
    if (!symbol || !frame || !output) return 0;
    char16_t* cursor = output;
    if (symbol->Flags & SYMFLAG_PARAMETER) {
        const std::u16string prefix = u"Parameter ";
        std::copy(prefix.begin(), prefix.end(), cursor);
        cursor += prefix.size();
    } else if (symbol->Flags & SYMFLAG_LOCAL) {
        const std::u16string prefix = u"Local ";
        std::copy(prefix.begin(), prefix.end(), cursor);
        cursor += prefix.size();
    }
    // SymTagFunction is value 5 in the CodeView SymTagEnum. MinGW does not
    // ship cvconst.h, so keep the exact target comparison explicitly.
    constexpr std::uint32_t kSymTagFunction = 5;
    if (symbol->Tag == kSymTagFunction) return 0;
    const std::u16string name = TargetFormat(u"'%S'", symbol->Name);
    std::copy(name.begin(), name.end(), cursor);
    cursor += name.size();

    std::uint64_t valueAddress = symbol->Address;
    if (symbol->Flags & SYMFLAG_REGREL) {
        valueAddress += frame->AddrFrame.Offset;
    } else if (symbol->Flags & SYMFLAG_REGISTER) {
        return 0;
    }
    int expanded = 0;
    cursor = DumpTypeIndex(
        cursor,
        symbol->ModBase,
        symbol->TypeIndex,
        0,
        static_cast<std::uint32_t>(valueAddress),
        expanded);
    if (!expanded) {
        FormatOutputValue(
            cursor,
            GetBasicType(symbol->TypeIndex, symbol->ModBase),
            symbol->Size,
            reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(valueAddress)));
    }
    return 1;
#else
    [[maybe_unused]] _SYMBOL_INFO* const unusedSymbol = symbol;
    [[maybe_unused]] ExceptionReportStackFrame* const unusedFrame = frame;
    [[maybe_unused]] char16_t* const unusedOutput = output;
    return 0;
#endif
}

// AuthGateD.exe 0x00414B10, ExceptionReport.cpp.
int CExceptionReport::EnumerateSymbolsCallback(
    _SYMBOL_INFO* symbol,
    [[maybe_unused]] std::uint32_t symbolSize,
    void* userContext) {
#if defined(_WIN32)
    std::array<char16_t, 16384> output{};
    if (FormatSymbolValue(
            symbol,
            static_cast<ExceptionReportStackFrame*>(userContext),
            output.data(),
            0x8000)) {
        _tprintfile(u"\t%s\r\n", output.data());
    }
    return 1;
#else
    [[maybe_unused]] _SYMBOL_INFO* const unusedSymbol = symbol;
    [[maybe_unused]] void* const unusedContext = userContext;
    return 1;
#endif
}

// AuthGateD.exe 0x00414BD0, ExceptionReport.cpp.
void CExceptionReport::WriteStackDetails(
    _CONTEXT* context,
    int detail) {
    _tprintfile(kSeparator);
    _tprintfile(
        u"    Call Stack (%s)\r\n",
        detail ? u"Detail" : u"Short");
    _tprintfile(kSeparator);
    _tprintfile(u"\r\n");
    _tprintfile(
        u"Address   Frame     Function                        SourceFile\r\n");
#if defined(_WIN32)
    if (context) {
        STACKFRAME64 frame{};
        DWORD machineType = 0;
#  if defined(_M_IX86) || defined(__i386__)
        machineType = IMAGE_FILE_MACHINE_I386;
        frame.AddrPC.Offset = context->Eip;
        frame.AddrStack.Offset = context->Esp;
        frame.AddrFrame.Offset = context->Ebp;
#  elif defined(_M_X64) || defined(__x86_64__)
        machineType = IMAGE_FILE_MACHINE_AMD64;
        frame.AddrPC.Offset = context->Rip;
        frame.AddrStack.Offset = context->Rsp;
        frame.AddrFrame.Offset = context->Rbp;
#  endif
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrStack.Mode = AddrModeFlat;
        frame.AddrFrame.Mode = AddrModeFlat;
        while (machineType != 0 && ::StackWalk64(
                   machineType,
                   static_cast<HANDLE>(m_hProcess),
                   ::GetCurrentThread(),
                   &frame,
                   context,
                   nullptr,
                   ::SymFunctionTableAccess64,
                   ::SymGetModuleBase64,
                   nullptr) &&
               frame.AddrFrame.Offset != 0) {
            _tprintfile(
                u"%08X  %08X  ",
                static_cast<std::uint32_t>(frame.AddrPC.Offset),
                static_cast<std::uint32_t>(frame.AddrFrame.Offset));
            std::array<std::uint8_t,
                sizeof(SYMBOL_INFO) + MAX_SYM_NAME> symbolStorage{};
            auto* const symbol =
                reinterpret_cast<SYMBOL_INFO*>(symbolStorage.data());
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = MAX_SYM_NAME;
            DWORD64 displacement = 0;
            if (::SymFromAddr(
                    static_cast<HANDLE>(m_hProcess),
                    frame.AddrPC.Offset,
                    &displacement,
                    symbol)) {
                const std::u16string function = TargetFormat(
                    u"%-s+%I64X",
                    symbol->Name,
                    displacement);
                _tprintfile(u"%-30s", function.c_str());
            } else {
                std::array<char16_t, 260> module{};
                std::uint32_t section = 0;
                std::uint32_t offset = 0;
                GetLogicalAddress(
                    reinterpret_cast<const void*>(
                        static_cast<std::uintptr_t>(
                            frame.AddrPC.Offset)),
                    module.data(),
                    0x208,
                    section,
                    offset);
                _tprintfile(
                    u"%04X:%08X                   %s",
                    section,
                    offset,
                    module.data());
            }
            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisplacement = 0;
            if (::SymGetLineFromAddr64(
                    static_cast<HANDLE>(m_hProcess),
                    frame.AddrPC.Offset,
                    &lineDisplacement,
                    &line)) {
                _tprintfile(
                    u"  %S line %u",
                    line.FileName,
                    line.LineNumber);
                m_bHasSymbol = 1;
            }
            _tprintfile(u"\r\n");
            if (detail) {
                IMAGEHLP_STACK_FRAME symbolFrame{};
                symbolFrame.InstructionOffset = frame.AddrPC.Offset;
                ::SymSetContext(
                    static_cast<HANDLE>(m_hProcess),
                    &symbolFrame,
                    nullptr);
                ::SymEnumSymbols(
                    static_cast<HANDLE>(m_hProcess),
                    0,
                    nullptr,
                    reinterpret_cast<PSYM_ENUMERATESYMBOLS_CALLBACK>(
                        EnumerateSymbolsCallback),
                    &frame);
                _tprintfile(u"\r\n");
            }
        }
    }
#else
    [[maybe_unused]] _CONTEXT* const unavailableContext = context;
    void* frames[128]{};
    const int frameCount = ::backtrace(frames, 128);
    char** const symbols = ::backtrace_symbols(frames, frameCount);
    for (int index = 0; index < frameCount; ++index) {
        _tprintfile(
            u"%08X  %08X  %-30S\r\n",
            static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(frames[index])),
            0U,
            symbols ? symbols[index] : "Unknown");
    }
    std::free(symbols);
#endif
    _tprintfile(u"\r\n\r\n");
}

// AuthGateD.exe 0x00414EF0, ExceptionReport.cpp.
void CExceptionReport::WriteExceptionReport(
    _EXCEPTION_POINTERS* exceptionPointers) {
    _EXCEPTION_RECORD* const exceptionRecord = exceptionPointers
        ? exceptionPointers->ExceptionRecord
        : nullptr;
    _CONTEXT* const context = exceptionPointers
        ? exceptionPointers->ContextRecord
        : nullptr;
    _tprintfile(kDoubleSeparator);
    _tprintfile(u"\r\n");
    WriteBasicInfo(exceptionRecord);
    WriteRegistersInfo(context);
#if defined(_WIN32)
    ::SymSetOptions(SYMOPT_LOAD_LINES);
    if (::SymInitialize(
            static_cast<HANDLE>(m_hProcess),
            nullptr,
            TRUE)) {
        _CONTEXT copied{};
        if (context) copied = *context;
        WriteStackDetails(context ? &copied : nullptr, 0);
        if (m_bHasSymbol) {
            if (context) copied = *context;
            WriteStackDetails(context ? &copied : nullptr, 1);
            _tprintfile(kSeparator);
            _tprintfile(u"    Global Variables\r\n");
            _tprintfile(kSeparator);
            _tprintfile(u"\r\n");
            ::SymEnumSymbols(
                static_cast<HANDLE>(m_hProcess),
                reinterpret_cast<DWORD64>(
                    ::GetModuleHandleW(
                        reinterpret_cast<const wchar_t*>(
                            m_szModuleName.data()))),
                nullptr,
                reinterpret_cast<PSYM_ENUMERATESYMBOLS_CALLBACK>(
                    EnumerateSymbolsCallback),
                nullptr);
        }
        ::SymCleanup(static_cast<HANDLE>(m_hProcess));
        _tprintfile(u"\r\n\r\n");
    }
#else
    WriteStackDetails(context, 0);
#endif
    WriteMemoryDump(context);
    if (m_lpUserFunc) {
        _tprintfile(kSeparator);
        _tprintfile(u"    Application-specific log\r\n");
        _tprintfile(kSeparator);
        _tprintfile(u"\r\n");
        m_lpUserFunc(m_hReportFile);
        _tprintfile(u"\r\n\r\n");
    }
    _tprintfile(kDoubleSeparator);
}

// AuthGateD.exe 0x00415090, ExceptionReport.cpp.
std::int32_t CExceptionReport::UnhandledExceptionFilter(
    _EXCEPTION_POINTERS* exceptionPointers) {
    WriteCrashDiagnostic("ERROR", "Fatal failure; starting crash-report generation.");
    g_reportOutputFailed.store(false, std::memory_order_relaxed);
#if defined(_WIN32)
    ::SetUnhandledExceptionFilter(
        reinterpret_cast<LPTOP_LEVEL_EXCEPTION_FILTER>(
            UnhandledExceptionFilterTwo));
#endif
    const std::time_t current = std::time(nullptr);
    const std::tm now =
        runtime::crt::LocalTimeOrZero(
            current);
    const std::u16string dumpName = CrashFileName(
        m_szLogPrefixName,
        now,
        u"DMP");
    const std::u16string reportName = CrashFileName(
        m_szLogPrefixName,
        now,
        u"TXT");
    WriteCrashDiagnostic("INFO", "Attempting to write the crash report.", reportName);

#if defined(_WIN32)
    HANDLE const dump = ::CreateFileW(
        reinterpret_cast<const wchar_t*>(dumpName.c_str()),
        GENERIC_WRITE,
        FILE_SHARE_WRITE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (dump != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION information{};
        information.ThreadId = ::GetCurrentThreadId();
        information.ExceptionPointers = exceptionPointers;
        information.ClientPointers = FALSE;
        // ::MiniDumpWriteDump(
        //     ::GetCurrentProcess(),
        //     ::GetCurrentProcessId(),
        //     dump,
        //     MiniDumpNormal,
        //     exceptionPointers ? &information : nullptr,
        //     nullptr,
        //     nullptr)
        const BOOL dumpOk = ::MiniDumpWriteDump(
            ::GetCurrentProcess(),
            ::GetCurrentProcessId(),
            dump,
            MiniDumpNormal,
            exceptionPointers ? &information : nullptr,
            nullptr,
            nullptr);
        if (!dumpOk) {
            WriteCrashDiagnostic("ERROR", "Minidump generation failed; the file may be incomplete.", dumpName, "Win32 error", ::GetLastError());
        }
        // ::CloseHandle(dump)
        const BOOL dumpClosed = ::CloseHandle(dump);
        if (!dumpClosed) {
            WriteCrashDiagnostic("ERROR", "Could not close the minidump file.", dumpName, "Win32 error", ::GetLastError());
        } else if (dumpOk) {
            WriteCrashDiagnostic("INFO", "Minidump generation finished and the file was closed.", dumpName);
        }
        if (config.useReportMail) {
            const std::string path = portable::WindowsAnsiFromUtf16(dumpName);
            CMailReport::Instance()->AddAttachment(path.c_str());
        }
    } else {
        WriteCrashDiagnostic("ERROR", "Could not create the minidump file.", dumpName, "Win32 error", ::GetLastError());
    }
    HANDLE const report = ::CreateFileW(
        reinterpret_cast<const wchar_t*>(reportName.c_str()),
        GENERIC_WRITE,
        0,
        nullptr,
        OPEN_ALWAYS,
        FILE_FLAG_WRITE_THROUGH,
        nullptr);
    if (report != INVALID_HANDLE_VALUE) {
        // ::SetFilePointer(report, 0, nullptr, FILE_END)
        const DWORD position = ::SetFilePointer(report, 0, nullptr, FILE_END);
        if (position == INVALID_SET_FILE_POINTER) {
            // This return can also be a valid position. Do not invent an OS error.
            ReportCrashOutputFailure("Crash-report append position was not confirmed (SetFilePointer returned 0xffffffff).");
        }
        m_hReportFile = report;
        WriteExceptionReport(exceptionPointers);
        // ::CloseHandle(report)
        const BOOL reportClosed = ::CloseHandle(report);
        if (!reportClosed) {
            WriteCrashDiagnostic("ERROR", "Could not close the crash-report file; output may be incomplete.", reportName, "Win32 error", ::GetLastError());
        } else {
            const bool outputFailed = g_reportOutputFailed.load(std::memory_order_relaxed);
            WriteCrashDiagnostic(outputFailed ? "ERROR" : "INFO",
                outputFailed
                    ? "Crash-report file closed; complete output was not confirmed."
                    : "Crash-report file closed.", reportName);
        }
        m_hReportFile = nullptr;
    } else {
        WriteCrashDiagnostic("ERROR", "Could not open the crash-report file.", reportName, "Win32 error", ::GetLastError());
    }
#else
    const std::string nativeReport = NativePath(reportName);
    std::FILE* const report = std::fopen(nativeReport.c_str(), "ab");
    if (report) {
        m_hReportFile = report;
        WriteExceptionReport(exceptionPointers);
        // std::fclose(report)
        const int closeResult = std::fclose(report);
        if (closeResult != 0) {
            WriteCrashDiagnostic("ERROR", "Could not close the crash-report file; output may be incomplete.", reportName, "errno", errno);
        } else {
            const bool outputFailed = g_reportOutputFailed.load(std::memory_order_relaxed);
            WriteCrashDiagnostic(outputFailed ? "ERROR" : "INFO",
                outputFailed
                    ? "Crash-report file closed; complete output was not confirmed."
                    : "Crash-report file closed.", reportName);
        }
        m_hReportFile = nullptr;
        if (config.useReportMail) {
            // MiniDumpWriteDump has no POSIX equivalent. Attach the recovered
            // UTF-16 report so enabled crash mail still carries diagnostics.
            CMailReport::Instance()->AddAttachment(nativeReport.c_str());
        }
    } else {
        WriteCrashDiagnostic("ERROR", "Could not open the crash-report file.", reportName, "errno", errno);
    }
#endif
    if (config.useReportMail) {
        CMailReport::Instance()->SendMail(
            "Crashed",
            "AuthGated Crashed");
    }
    return 1;
}

CExceptionReport g_CExceptionReport;
