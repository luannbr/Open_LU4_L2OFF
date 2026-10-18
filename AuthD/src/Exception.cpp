#include "Exception.h"

#include "Config.h"
#include "MailReport.h"

#include "Shared/src/Runtime/BoundedFormat.h"
#include "Shared/src/Runtime/RawAtomic.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <string>
#include <thread>

#if defined(_WIN32)
#  if !defined(NOMINMAX)
#    define NOMINMAX
#  endif
#  include <DbgHelp.h>
#  include <Lmcons.h>
#  include <WinSock2.h>
#  include <Windows.h>
#  include <winreg.h>
#else
#  include <csignal>
#  include <dlfcn.h>
#  include <execinfo.h>
#  include <netdb.h>
#  include <pwd.h>
#  include <sys/socket.h>
#  include <sys/stat.h>
#  include <sys/time.h>
#  include <sys/types.h>
#  include <unistd.h>
#  if defined(__APPLE__)
#    include <mach-o/dyld.h>
#    include <mach-o/loader.h>
#    include <pthread.h>
#    include <sys/sysctl.h>
#  elif defined(__linux__)
#    include <link.h>
#    include <sys/syscall.h>
#    include <sys/sysinfo.h>
#  endif
#endif

authd::platform::CriticalSectionStorage32
    CLogCritical::m_CriticalSection{};
std::int32_t CExceptionInit::g_timeStart = 0;

const char* mailServer = nullptr;
std::int32_t Terminating = 0;

namespace {

// Exact file-local data from Exception.obj. The selected image keeps the
// three 256-byte command buffers in .data and the report/subject buffers in
// zero-filled storage.
char heloStr[256] = "HELO main1.ncsoft.co.kr\r\n";
char mailStr[256] =
    "MAIL From: <linserver@ncsoft.co.kr>\r\n";
char rcptStr[256] =
    "RCPT To: <veni@ncsoft.co.kr>\r\n";
char dataStr[7] = "DATA\r\n";
bool xfirst = true;
char finishStr[6] = "\r\n.\r\n";
char quitStr[7] = "QUIT\r\n";

char g_szLogPath[260]{};
char subjectStr[256]{};
int BeenHere = 0;

[[maybe_unused]] constexpr std::uint32_t
    kDiskSpaceException =
    UINT32_C(0xe0000001);
[[maybe_unused]] constexpr std::uint32_t
    kAccessViolation =
    UINT32_C(0xc0000005);

struct LocalSystemTime {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
};

LocalSystemTime ReadLocalSystemTime() noexcept {
#if defined(_WIN32)
    SYSTEMTIME value{};
    ::GetLocalTime(&value);
    return {
        value.wYear,
        value.wMonth,
        value.wDay,
        value.wHour,
        value.wMinute,
        value.wSecond};
#else
    const std::time_t now = std::time(nullptr);
    std::tm value{};
    if (::localtime_r(&now, &value) == nullptr) {
        return {};
    }
    return {
        value.tm_year + 1900,
        value.tm_mon + 1,
        value.tm_mday,
        value.tm_hour,
        value.tm_min,
        value.tm_sec};
#endif
}

std::uint32_t CurrentThreadId32() noexcept {
#if defined(_WIN32)
    return static_cast<std::uint32_t>(
        ::GetCurrentThreadId());
#elif defined(__APPLE__)
    std::uint64_t threadId = 0;
    if (::pthread_threadid_np(
            nullptr,
            &threadId) == 0) {
        return static_cast<std::uint32_t>(
            threadId);
    }
    return 0;
#elif defined(__linux__)
    return static_cast<std::uint32_t>(
        ::syscall(SYS_gettid));
#else
    return static_cast<std::uint32_t>(
        std::hash<std::thread::id>{}(
            std::this_thread::get_id()));
#endif
}

bool IsTerminating() noexcept {
    return runtime::raw_atomic::LoadI32Relaxed(
               &Terminating) != 0;
}

void SetTerminating() noexcept {
    runtime::raw_atomic::StoreI32Relaxed(
        &Terminating,
        1);
}

void DeleteLogFile() noexcept {
#if defined(_WIN32)
    ::DeleteFileA(g_szLogPath);
#else
    std::remove(g_szLogPath);
#endif
}

// Diagnostic-only state: no report-file or application lock is acquired.
// Both append entry points use the same destination and share suppression.
void ReportCrashFileOpenResult(bool opened) noexcept {
    const int savedErrno = errno;
#if defined(_WIN32)
    const DWORD savedError = ::GetLastError();
#endif
    static std::atomic_flag reported = ATOMIC_FLAG_INIT;
    if (opened) {
        reported.clear(std::memory_order_relaxed);
    } else if (!reported.test_and_set(std::memory_order_relaxed)) {
        constexpr char warning[] =
            "ERROR [exception.cpp] Cannot open AuthD crash log for append; report output is unavailable. Further open warnings are suppressed until an open succeeds.\n";
#if defined(_WIN32)
        DWORD written = 0;
        ::WriteFile(::GetStdHandle(STD_ERROR_HANDLE), warning,
            static_cast<DWORD>(sizeof(warning) - 1), &written, nullptr);
#else
        (void)::write(STDERR_FILENO, warning, sizeof(warning) - 1);
#endif
    }
    errno = savedErrno;
#if defined(_WIN32)
    ::SetLastError(savedError);
#endif
}

#if defined(_WIN32)
void ReportCrashFileSeekResult(DWORD position) noexcept {
    const DWORD seekError = ::GetLastError();
    static std::atomic_flag reported = ATOMIC_FLAG_INIT;
    if (position != INVALID_SET_FILE_POINTER || seekError == NO_ERROR) {
        reported.clear(std::memory_order_relaxed);
    } else if (!reported.test_and_set(std::memory_order_relaxed)) {
        constexpr char warning[] =
            "ERROR [exception.cpp] Cannot seek to the end of AuthD crash log; subsequent writes may overwrite existing report data. Further seek warnings are suppressed until a seek succeeds.\n";
        DWORD written = 0;
        ::WriteFile(::GetStdHandle(STD_ERROR_HANDLE), warning,
            static_cast<DWORD>(sizeof(warning) - 1), &written, nullptr);
    }
    ::SetLastError(seekError);
}
#endif

void* OpenLogForAppend() noexcept {
#if defined(_WIN32)
    HANDLE const file = ::CreateFileA(
        g_szLogPath,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    ReportCrashFileOpenResult(file != INVALID_HANDLE_VALUE);
    if (file == INVALID_HANDLE_VALUE) {
        return nullptr;
    }
    // ::SetFilePointer(
    //     file,
    //     0,
    //     nullptr,
    //     FILE_END)
    const DWORD position = ::SetFilePointer(
        file,
        0,
        nullptr,
        FILE_END);
    ReportCrashFileSeekResult(position);
    return file;
#else
    // return std::fopen(g_szLogPath, "ab")
    std::FILE* const file = std::fopen(g_szLogPath, "ab");
    ReportCrashFileOpenResult(file != nullptr);
    return file;
#endif
}

void* OpenReportForAppend() noexcept {
#if defined(_WIN32)
    HANDLE const file = ::CreateFileA(
        g_szLogPath,
        GENERIC_WRITE,
        0,
        nullptr,
        OPEN_ALWAYS,
        FILE_FLAG_WRITE_THROUGH |
            FILE_ATTRIBUTE_NORMAL,
        nullptr);
    ReportCrashFileOpenResult(file != INVALID_HANDLE_VALUE);
    if (file == INVALID_HANDLE_VALUE) {
        return nullptr;
    }
    // ::SetFilePointer(
    //     file,
    //     0,
    //     nullptr,
    //     FILE_END)
    const DWORD position = ::SetFilePointer(
        file,
        0,
        nullptr,
        FILE_END);
    ReportCrashFileSeekResult(position);
    return file;
#else
    // return std::fopen(g_szLogPath, "ab")
    std::FILE* const file = std::fopen(g_szLogPath, "ab");
    ReportCrashFileOpenResult(file != nullptr);
    return file;
#endif
}

void CloseLogFile(void* file) noexcept {
    if (file == nullptr) {
        return;
    }
#if defined(_WIN32)
    // ::CloseHandle(static_cast<HANDLE>(file))
    if (!::CloseHandle(static_cast<HANDLE>(file))) {
        const DWORD closeError = ::GetLastError();
        constexpr char warning[] = "ERROR [exception.cpp] Cannot close AuthD crash report handle; report completion is unconfirmed.\n";
        DWORD written = 0;
        ::WriteFile(::GetStdHandle(STD_ERROR_HANDLE), warning,
            static_cast<DWORD>(sizeof(warning) - 1), &written, nullptr);
        ::SetLastError(closeError);
    }
#else
    // std::fclose(static_cast<std::FILE*>(file))
    if (std::fclose(static_cast<std::FILE*>(file)) != 0) {
        const int closeError = errno;
        constexpr char warning[] = "ERROR [exception.cpp] Cannot close or flush AuthD crash report; output may be incomplete.\n";
        // This path is reachable from the signal handler. Avoid formatter,
        // allocation and CRT stream locks for the fallback warning itself.
        (void)::write(STDERR_FILENO, warning, sizeof(warning) - 1);
        errno = closeError;
    }
#endif
}

void WriteLogBytes(
    void* file,
    const void* data,
    std::size_t size) noexcept {
    if (file == nullptr) {
        return;
    }
    // Constant-initialized, lock-free suppression shared by report writers.
    // Only an actual nonempty successful write allows a new warning.
    static std::atomic_flag writeFailureReported = ATOMIC_FLAG_INIT;
#if defined(_WIN32)
    DWORD written = 0;
    // ::WriteFile(
    //     static_cast<HANDLE>(file),
    //     data,
    //     static_cast<DWORD>(size),
    //     &written,
    //     nullptr)
    const BOOL succeeded = ::WriteFile(static_cast<HANDLE>(file), data,
        static_cast<DWORD>(size), &written, nullptr);
    const DWORD writeError = ::GetLastError();
    const bool complete = succeeded && static_cast<std::size_t>(written) == size;
#else
    // std::fwrite(
    //     data,
    //     1,
    //     size,
    //     static_cast<std::FILE*>(file))
    const std::size_t written = std::fwrite(data, 1, size, static_cast<std::FILE*>(file));
    const int writeError = errno;
    const bool complete = written == size;
#endif
    if (complete) {
        if (size != 0) writeFailureReported.clear(std::memory_order_relaxed);
    } else if (!writeFailureReported.test_and_set(std::memory_order_relaxed)) {
        constexpr char warning[] =
            "ERROR [exception.cpp] AuthD crash report write failed or was incomplete. Further write warnings are suppressed until a successful write.\n";
        // This fallback itself takes no CRT stream or application lock.
#if defined(_WIN32)
        DWORD warningWritten = 0;
        ::WriteFile(::GetStdHandle(STD_ERROR_HANDLE), warning,
            static_cast<DWORD>(sizeof(warning) - 1), &warningWritten, nullptr);
#else
        (void)::write(STDERR_FILENO, warning, sizeof(warning) - 1);
#endif
    }
#if defined(_WIN32)
    ::SetLastError(writeError);
#else
    errno = writeError;
#endif
}

bool LogFileExists() noexcept {
#if defined(_WIN32)
    HANDLE const file = ::CreateFileA(
        g_szLogPath,
        GENERIC_READ,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    ::CloseHandle(file);
    return true;
#else
    std::FILE* const file =
        std::fopen(g_szLogPath, "rb");
    if (file == nullptr) {
        return false;
    }
    std::fclose(file);
    return true;
#endif
}

void hprintf(
    void* logFile,
    const char* format,
    ...) {
    // L2Authd.exe 0x00438770, Exception.cpp:67-77. The target uses the
    // unbounded wvsprintfA into an exact 2000-byte stack buffer.
    std::array<char, 2000> buffer{};
    va_list arguments;
    va_start(arguments, format);
#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
    // const int length =
    //     std::vsprintf(
    //         buffer.data(),
    //         format,
    //         arguments)
    const int length = format
        ? std::vsnprintf(buffer.data(), buffer.size(), format, arguments)
        : -1;
#if defined(__clang__)
#  pragma clang diagnostic pop
#endif
    va_end(arguments);
    if (length < 0) {
        constexpr char failure[] = "[logging: could not format crash report fragment]\r\n";
        WriteLogBytes(logFile, failure, sizeof(failure) - 1);
        return;
    }
    if (static_cast<std::size_t>(length) >= buffer.size()) {
        constexpr char marker[] = " [logging: crash report fragment truncated]\r\n";
        std::memcpy(buffer.data() + buffer.size() - sizeof(marker), marker, sizeof(marker));
        WriteLogBytes(logFile, buffer.data(), buffer.size() - 1);
        return;
    }
    if (length > 0) {
        WriteLogBytes(
            logFile,
            buffer.data(),
            static_cast<std::size_t>(length));
    }
}

#if defined(_WIN32)
void PrintTime(
    char* output,
    FILETIME timeToPrint) {
    // L2Authd.exe 0x004387F0, Exception.cpp:83-96.
    WORD date = 0;
    WORD time = 0;
    if (::FileTimeToLocalFileTime(
            &timeToPrint,
            &timeToPrint) != FALSE &&
        ::FileTimeToDosDateTime(
            &timeToPrint,
            &date,
            &time) != FALSE) {
        ::wsprintfA(
            output,
            "%d/%d/%d %02d:%02d:%02d",
            (date >> 5) & 0x0f,
            date & 0x1f,
            (date >> 9) + 1980,
            time >> 11,
            (time >> 5) & 0x3f,
            (time & 0x1f) << 1);
        return;
    }
    output[0] = '\0';
}
#else
void PrintTime(
    char* output,
    std::size_t outputSize,
    std::time_t timeToPrint) {
    std::tm local{};
    if (::localtime_r(
            &timeToPrint,
            &local) == nullptr) {
        output[0] = '\0';
        return;
    }
    std::snprintf(
        output,
        outputSize,
        "%d/%d/%d %02d:%02d:%02d",
        local.tm_mon + 1,
        local.tm_mday,
        local.tm_year + 1900,
        local.tm_hour,
        local.tm_min,
        local.tm_sec);
}
#endif

[[maybe_unused]] const char* GetFilePart(
    char* path) noexcept {
    // L2Authd.exe 0x00438F10. The original helper recognizes only the
    // Win32 separator. Portable module enumeration uses a separate native
    // basename helper instead of changing this recovered function.
    char* const separator = std::strrchr(path, '\\');
    return separator == nullptr
        ? path
        : separator + 1;
}

const char* NativeFilePart(
    const char* path) noexcept {
    if (path == nullptr) {
        return "Unknown";
    }
    const char* slash = std::strrchr(path, '/');
    const char* backslash = std::strrchr(path, '\\');
    const char* separator = slash;
    if (backslash != nullptr &&
        (separator == nullptr ||
         separator < backslash)) {
        separator = backslash;
    }
    return separator == nullptr
        ? path
        : separator + 1;
}

std::string CurrentExecutablePath() {
#if defined(_WIN32)
    std::array<char, 260> path{};
    if (::GetModuleFileNameA(
            nullptr,
            path.data(),
            static_cast<DWORD>(path.size())) == 0) {
        return "Unknown";
    }
    return path.data();
#elif defined(__APPLE__)
    std::array<char, 4096> path{};
    std::uint32_t size =
        static_cast<std::uint32_t>(path.size());
    if (::_NSGetExecutablePath(
            path.data(),
            &size) != 0) {
        return "Unknown";
    }
    return path.data();
#elif defined(__linux__)
    std::array<char, 4096> path{};
    const ssize_t size = ::readlink(
        "/proc/self/exe",
        path.data(),
        path.size() - 1);
    if (size <= 0) {
        return "Unknown";
    }
    path[static_cast<std::size_t>(size)] = '\0';
    return path.data();
#else
    return "Unknown";
#endif
}

std::string CurrentUserName() {
#if defined(_WIN32)
    std::array<char, UNLEN + 1> user{};
    DWORD length = static_cast<DWORD>(user.size());
    if (::GetUserNameA(
            user.data(),
            &length) == FALSE) {
        return "Unknown";
    }
    return user.data();
#else
    if (passwd* const entry =
            ::getpwuid(::geteuid());
        entry != nullptr &&
        entry->pw_name != nullptr) {
        return entry->pw_name;
    }
    const char* const user = std::getenv("USER");
    return user == nullptr
        ? "Unknown"
        : user;
#endif
}

std::uint64_t PhysicalMemoryBytes() noexcept {
#if defined(_WIN32)
    MEMORYSTATUS status{};
    status.dwLength = sizeof(status);
    ::GlobalMemoryStatus(&status);
    return status.dwTotalPhys;
#elif defined(__APPLE__)
    std::uint64_t memory = 0;
    std::size_t size = sizeof(memory);
    if (::sysctlbyname(
            "hw.memsize",
            &memory,
            &size,
            nullptr,
            0) != 0) {
        return 0;
    }
    return memory;
#elif defined(__linux__)
    struct sysinfo status {};
    if (::sysinfo(&status) != 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(
               status.totalram) *
        status.mem_unit;
#else
    const long pages = ::sysconf(_SC_PHYS_PAGES);
    const long pageSize = ::sysconf(_SC_PAGESIZE);
    if (pages <= 0 || pageSize <= 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(pages) *
        static_cast<std::uint64_t>(pageSize);
#endif
}

void SendConfiguredCrashReport() {
    if (!config.useReportMail) {
        return;
    }
    CMailReport* const report =
        CMailReport::Instance();
    report->AddAttachment(g_szLogPath);
    report->SendMail("Crashed", g_szLogPath);
    report->ClearAttachments();
}

void GenerateExceptionReport(
    _EXCEPTION_POINTERS* exceptionPointers);
void GenerateMiniDump(
    _EXCEPTION_POINTERS* exceptionPointers);
bool sendFile(
    std::uintptr_t socket,
    const char* filename);
int sendMail(
    const char* server,
    const char* sender,
    const char* receivers,
    const char* filename);
void RecordModuleList(void* logFile);
void RecordSystemInformation(void* logFile);
const char* GetExceptionDescription(
    std::uint32_t exceptionCode) noexcept;

} // namespace

CLogCritical::CLogCritical() {
    // L2Authd.exe 0x0043A660, Exception.cpp:22.
    authd::platform::EnterCriticalSection(
        m_CriticalSection);
}

CLogCritical::~CLogCritical() {
    // L2Authd.exe 0x0043A680, Exception.cpp:23.
    authd::platform::LeaveCriticalSection(
        m_CriticalSection);
}

void CLogCritical::Initialize() {
    // L2Authd.exe 0x0043A6B0, Exception.cpp:24.
    authd::platform::InitializeCriticalSection(
        m_CriticalSection);
}

void CLogCritical::Deinitialize() {
    // L2Authd.exe 0x0043A6A0, Exception.cpp:25.
    authd::platform::DeleteCriticalSection(
        m_CriticalSection);
}

CExceptionInit::CExceptionInit() {
    // L2Authd.exe 0x004386D0, Exception.cpp:847-859.
    CLogCritical::Initialize();
    const LocalSystemTime now =
        ReadLocalSystemTime();
    std::snprintf(
        g_szLogPath,
        sizeof(g_szLogPath),
        " L2AuthD_%04d-%02d-%02d_%02d,%02d,%02d.txt",
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second);
    CExceptionInit::g_timeStart =
        static_cast<std::int32_t>(
            std::time(nullptr));
    SetUpExceptionFilter();
}

CExceptionInit::~CExceptionInit() {
    // L2Authd.exe 0x00438750, Exception.cpp:862-865.
    ClearExceptionFilter();
    CLogCritical::Deinitialize();
}

void ELOG(
    const char* format,
    ...) {
    // Preserve the error being diagnosed across opening, writing and closing the report.
    // struct RestoreErrno {
    //     int value = errno
    //     ~RestoreErrno() { errno = value. }
    // } restoreErrno
    struct RestoreReportError {
        int value = errno;
#if defined(_WIN32)
        DWORD windowsError = ::GetLastError();
#endif
        ~RestoreReportError() {
            errno = value;
#if defined(_WIN32)
            ::SetLastError(windowsError);
#endif
        }
    } restoreReportError;
    // L2Authd.exe 0x004382E0, Exception.cpp:869-920.
    if (IsTerminating()) {
        return;
    }
    CLogCritical critical;
    const LocalSystemTime now =
        ReadLocalSystemTime();
    void* const file = OpenLogForAppend();
    if (file == nullptr) {
        return;
    }

    std::array<char, 1024> buffer{};
    int length = std::snprintf(
        buffer.data(),
        buffer.size(),
        "[(%d) %04d/%02d/%02d %02d:%02d:%02d]: ",
        CurrentThreadId32(),
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second);
    if (length >=
        static_cast<int>(buffer.size())) {
        length = -1;
    }
    if (length >= 0) {
        WriteLogBytes(
            file,
            buffer.data(),
            static_cast<std::size_t>(length));

        va_list arguments;
        va_start(arguments, format);
#if defined(_MSC_VER)
        length = ::_vsnprintf(
            buffer.data(),
            buffer.size(),
            format,
            arguments);
#else
        length = std::vsnprintf(
            buffer.data(),
            buffer.size(),
            format,
            arguments);
        if (length >=
            static_cast<int>(buffer.size())) {
            // VS2013 _vsnprintf reports truncation as -1.
            length = -1;
        }
#endif
        va_end(arguments);

        if (length >= 0) {
            int scan = 0;
            while (scan < length) {
                auto* const newline =
                    static_cast<char*>(
                        std::memchr(
                            buffer.data() + scan,
                            '\n',
                            static_cast<std::size_t>(
                                length - scan)));
                if (newline == nullptr) {
                    break;
                }
                if (length <
                    static_cast<int>(buffer.size())) {
                    ++length;
                }
                const int newlineIndex =
                    static_cast<int>(
                        newline - buffer.data());
                std::memmove(
                    newline + 1,
                    newline,
                    static_cast<std::size_t>(
                        length - newlineIndex - 1));
                *newline = '\r';
                scan = newlineIndex + 2;
            }
            WriteLogBytes(
                file,
                buffer.data(),
                static_cast<std::size_t>(length));
        } else {
            constexpr char failure[] =
                "[logging: ELOG message omitted; formatting failed or exceeded the 1024-byte buffer]\r\n";
            WriteLogBytes(file, failure, sizeof(failure) - 1);
        }
    } else {
        constexpr char failure[] =
            "[logging: ELOG entry omitted; timestamp formatting failed]\r\n";
        WriteLogBytes(file, failure, sizeof(failure) - 1);
    }
    CloseLogFile(file);
}

int exception_filter(
    _EXCEPTION_POINTERS* exceptionPointers) {
    // L2Authd.exe 0x00438140, Exception.cpp:934-950.
    bool report = !IsTerminating();
#if defined(_WIN32)
    report = report &&
        exceptionPointers != nullptr &&
        exceptionPointers->ExceptionRecord != nullptr &&
        exceptionPointers
                ->ExceptionRecord
                ->ExceptionCode !=
            kDiskSpaceException;
#endif
    if (report) {
        CLogCritical critical;
        if (mailServer == nullptr) {
            DeleteLogFile();
        }
        GenerateMiniDump(exceptionPointers);
        GenerateExceptionReport(exceptionPointers);
        SendConfiguredCrashReport();
    }
    return 1;
}

void send_exception(bool terminateProcess) {
    // L2Authd.exe 0x004385B0, Exception.cpp:976-989.
    if (IsTerminating()) {
        return;
    }
    {
        CLogCritical critical;
        static constexpr char sender[] =
            "server@ncsoft.net";
        // The extra explicit NUL reproduces the double-NUL recipient list
        // expected by the local sendMail loop.
        static constexpr char receivers[] =
            "veni@ncsoft.net\0";
        if (mailServer != nullptr &&
            sendMail(
                mailServer,
                sender,
                receivers,
                g_szLogPath) != 0) {
            DeleteLogFile();
        }
    }
    if (terminateProcess) {
        SetTerminating();
#if defined(_WIN32)
        ::ExitProcess(0);
#else
        std::_Exit(0);
#endif
    }
}

void exception_init() {
    // L2Authd.exe 0x00438660, Exception.cpp:992-1001.
    if (mailServer == nullptr ||
        !LogFileExists()) {
        return;
    }
    static constexpr char sender[] =
        "server@ncsoft.net";
    static constexpr char receivers[] =
        "veni@ncsoft.net\0";
    [[maybe_unused]] const int result = sendMail(
        mailServer,
        sender,
        receivers,
        g_szLogPath);
    DeleteLogFile();
}

void violation() {
    // L2Authd.exe 0x00438220, Exception.cpp:1185-1200.
    if (!xfirst) {
        return;
    }
    xfirst = false;
#if defined(_WIN32) && defined(_MSC_VER)
    __try {
        // Original: ELOG("Intentional Exception\n")
        ELOG("[Exception.cpp] violation() requested an intentional exception for diagnostic reporting.\n");
        volatile char* const crash = nullptr;
        *crash = 0;
    } __except (
        exception_filter(
            GetExceptionInformation())) {
        // Original: ELOG(" %s:%d(%s) exception\n", "Exception.cpp", 0x4ac,
        //     "Wed Oct 23 17:02:11 2019")
        ELOG("[Exception.cpp] Intentional exception caught in violation(); entering exception reporting.\n");
        send_exception(false);
    }
#else
    // SEH catches this intentional null write inside the selected Windows
    // body. Reproduce the caught path on hosts without SEH without raising a
    // fatal process signal.
    // Original: ELOG("Intentional Exception\n")
    ELOG("[Exception.cpp] violation() requested diagnostic reporting through the portable exception path.\n");
    exception_filter(nullptr);
    // Original: ELOG(" %s:%d(%s) exception\n", "Exception.cpp", 0x4ac,
    //     "Wed Oct 23 17:02:11 2019")
    ELOG("[Exception.cpp] Portable violation() exception filter returned; entering exception reporting.\n");
    send_exception(false);
#endif
}

namespace {

#if defined(_WIN32)
LPTOP_LEVEL_EXCEPTION_FILTER previousFilter =
    nullptr;
#else
constexpr std::array<int, 5> kFatalSignals{{
    SIGABRT,
    SIGBUS,
    SIGFPE,
    SIGILL,
    SIGSEGV,
}};
std::array<struct sigaction, kFatalSignals.size()>
    previousSignalActions{};
std::terminate_handler previousTerminateHandler =
    nullptr;
bool portableHandlersInstalled = false;
std::atomic_flag portableCrashActive =
    ATOMIC_FLAG_INIT;

[[noreturn]] void PortableTerminateHandler() noexcept {
    if (!portableCrashActive.test_and_set(
            std::memory_order_acq_rel)) {
        RecordExceptionInfo(nullptr);
    }
    std::_Exit(EXIT_FAILURE);
}

void PortableSignalHandler(
    int signalNumber,
    siginfo_t*,
    void*) {
    if (!portableCrashActive.test_and_set(
            std::memory_order_acq_rel)) {
        RecordExceptionInfo(nullptr);
    }
    struct sigaction defaultAction {};
    defaultAction.sa_handler = SIG_DFL;
    sigemptyset(&defaultAction.sa_mask);
    ::sigaction(
        signalNumber,
        &defaultAction,
        nullptr);
    ::raise(signalNumber);
    ::_exit(128 + signalNumber);
}

void InstallPortableExceptionHandlers() {
    if (portableHandlersInstalled) {
        return;
    }
    previousTerminateHandler =
        std::set_terminate(
            &PortableTerminateHandler);
    struct sigaction action {};
    action.sa_sigaction =
        &PortableSignalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags =
        SA_SIGINFO | SA_RESETHAND;
    for (std::size_t index = 0;
         index < kFatalSignals.size();
         ++index) {
        ::sigaction(
            kFatalSignals[index],
            &action,
            &previousSignalActions[index]);
    }
    portableHandlersInstalled = true;
}

void RemovePortableExceptionHandlers() {
    if (!portableHandlersInstalled) {
        return;
    }
    for (std::size_t index = 0;
         index < kFatalSignals.size();
         ++index) {
        ::sigaction(
            kFatalSignals[index],
            &previousSignalActions[index],
            nullptr);
    }
    std::set_terminate(previousTerminateHandler);
    portableHandlersInstalled = false;
}
#endif

} // namespace

#if defined(_WIN32)
ExceptionFilterResult __stdcall RecordExceptionInfo(
#else
ExceptionFilterResult RecordExceptionInfo(
#endif
    _EXCEPTION_POINTERS* exceptionPointers) {
    // L2Authd.exe 0x0043A4C0, Exception.cpp:799-832.
    if (BeenHere == 0) {
        BeenHere = 1;
        bool report = !IsTerminating();
#if defined(_WIN32)
        report = report &&
            exceptionPointers != nullptr &&
            exceptionPointers->ExceptionRecord !=
                nullptr &&
            exceptionPointers
                    ->ExceptionRecord
                    ->ExceptionCode !=
                kDiskSpaceException;
#endif
        if (report) {
            CLogCritical critical;
            if (mailServer == nullptr) {
                DeleteLogFile();
            }
            GenerateMiniDump(exceptionPointers);
            GenerateExceptionReport(exceptionPointers);
            SendConfiguredCrashReport();

            static constexpr char sender[] =
                "server@ncsoft.net";
            static constexpr char receivers[] =
                "veni@ncsoft.net\0";
            if (mailServer != nullptr &&
                sendMail(
                    mailServer,
                    sender,
                    receivers,
                    g_szLogPath) != 0) {
                DeleteLogFile();
            }
        }
    }
#if defined(_WIN32)
    return previousFilter == nullptr
        ? EXCEPTION_CONTINUE_SEARCH
        : previousFilter(exceptionPointers);
#else
    return 0;
#endif
}

void SetUpExceptionFilter() {
    // L2Authd.exe 0x0043A620, Exception.cpp:835-837.
#if defined(_WIN32)
    previousFilter =
        ::SetUnhandledExceptionFilter(
            &RecordExceptionInfo);
#else
    InstallPortableExceptionHandlers();
#endif
}

void ClearExceptionFilter() {
    // L2Authd.exe 0x0043A640, Exception.cpp:840-843.
#if defined(_WIN32)
    if (previousFilter != nullptr) {
        ::SetUnhandledExceptionFilter(
            previousFilter);
    }
#else
    RemovePortableExceptionHandlers();
#endif
}

CExceptionInit theExceptionInit;

namespace {

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket =
    INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
#endif

std::uintptr_t SocketToken(
    NativeSocket socket) noexcept {
    return static_cast<std::uintptr_t>(socket);
}

NativeSocket SocketFromToken(
    std::uintptr_t socket) noexcept {
    return static_cast<NativeSocket>(socket);
}

bool StartSocketRuntime() noexcept {
#if defined(_WIN32)
    WSADATA data{};
    return ::WSAStartup(
               MAKEWORD(2, 2),
               &data) == 0;
#else
    return true;
#endif
}

void StopSocketRuntime() noexcept {
#if defined(_WIN32)
    ::WSACleanup();
#endif
}

void CloseSocket(NativeSocket socket) noexcept {
#if defined(_WIN32)
    ::closesocket(socket);
#else
    ::close(socket);
#endif
}

int SocketSendFlags() noexcept {
#if defined(MSG_NOSIGNAL)
    return MSG_NOSIGNAL;
#else
    return 0;
#endif
}

int NativeSend(
    NativeSocket socket,
    const void* data,
    int length) noexcept {
#if defined(_WIN32)
    return ::send(
        socket,
        static_cast<const char*>(data),
        length,
        0);
#else
    return static_cast<int>(::send(
        socket,
        data,
        static_cast<std::size_t>(length),
        SocketSendFlags()));
#endif
}

int NativeReceive(
    NativeSocket socket,
    void* data,
    int length) noexcept {
#if defined(_WIN32)
    return ::recv(
        socket,
        static_cast<char*>(data),
        length,
        0);
#else
    return static_cast<int>(::recv(
        socket,
        data,
        static_cast<std::size_t>(length),
        0));
#endif
}

bool SetSocketTimeout(
    NativeSocket socket,
    int option,
    int milliseconds) noexcept {
#if defined(_WIN32)
    return ::setsockopt(
               socket,
               SOL_SOCKET,
               option,
               reinterpret_cast<const char*>(
                   &milliseconds),
               sizeof(milliseconds)) !=
        SOCKET_ERROR;
#else
    timeval timeout{};
    timeout.tv_sec = milliseconds / 1000;
    timeout.tv_usec =
        (milliseconds % 1000) * 1000;
    return ::setsockopt(
               socket,
               SOL_SOCKET,
               option,
               &timeout,
               sizeof(timeout)) == 0;
#endif
}

bool sendFile(
    std::uintptr_t socketToken,
    const char* filename) {
    // L2Authd.exe 0x00438F50, Exception.cpp:318-334. The target's unusual
    // success values and its failed-send file-handle leak are deliberate
    // baseline behavior, confirmed by the current x86 body.
#if defined(_WIN32)
    HANDLE const file = ::CreateFileA(
        filename,
        GENERIC_READ,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return true;
    }
    std::array<unsigned char, 1024> buffer{};
    for (;;) {
        DWORD bytesRead = 0;
        if (::ReadFile(
                file,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &bytesRead,
                nullptr) == FALSE ||
            bytesRead == 0) {
            ::CloseHandle(file);
            return true;
        }
        if (NativeSend(
                SocketFromToken(socketToken),
                buffer.data(),
                static_cast<int>(bytesRead)) <= 0) {
            return false;
        }
    }
#else
    std::FILE* const file =
        std::fopen(filename, "rb");
    if (file == nullptr) {
        return true;
    }
    std::array<unsigned char, 1024> buffer{};
    for (;;) {
        const std::size_t bytesRead =
            std::fread(
                buffer.data(),
                1,
                buffer.size(),
                file);
        if (bytesRead == 0) {
            std::fclose(file);
            return true;
        }
        if (NativeSend(
                SocketFromToken(socketToken),
                buffer.data(),
                static_cast<int>(bytesRead)) <= 0) {
            // Exact target edge: the file is not closed here.
            return false;
        }
    }
#endif
}

int sendMail(
    const char* server,
    const char* sender,
    const char* receivers,
    const char* filename) {
    // L2Authd.exe 0x00439010, Exception.cpp:337-448. This is the old,
    // file-local SMTP implementation, distinct from SendMail.cpp and
    // MailReport.cpp.
    if (!StartSocketRuntime()) {
        return 0;
    }
    const NativeSocket socket = ::socket(
        AF_INET,
        SOCK_STREAM,
        0);
    if (socket == kInvalidSocket) {
        StopSocketRuntime();
        return 0;
    }

    const bool completed = [&]() -> bool {
        if (!SetSocketTimeout(
                socket,
                SO_RCVTIMEO,
                5000)) {
            return false;
        }
        if (!SetSocketTimeout(
                socket,
                SO_SNDTIMEO,
                5000)) {
            return false;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        if (::bind(
                socket,
                reinterpret_cast<const sockaddr*>(
                    &address),
                sizeof(address)) != 0) {
            return false;
        }

        address.sin_family = AF_INET;
        if (server[0] < '0' ||
            server[0] > '9') {
            hostent* const host =
                ::gethostbyname(server);
            if (host == nullptr) {
                return false;
            }
            std::memcpy(
                &address.sin_addr,
                host->h_addr_list[0],
                static_cast<std::size_t>(
                    host->h_length));
            [[maybe_unused]] const char* const
                resolvedAddress =
                    ::inet_ntoa(address.sin_addr);
        } else {
            address.sin_addr.s_addr =
                ::inet_addr(server);
        }
        address.sin_port = htons(25);
        if (::connect(
                socket,
                reinterpret_cast<const sockaddr*>(
                    &address),
                sizeof(address)) != 0) {
            return false;
        }

        std::array<unsigned char, 2048> reply{};
        [[maybe_unused]] int received =
            NativeReceive(
                socket,
                reply.data(),
                static_cast<int>(reply.size()));
        if (NativeSend(
                socket,
                heloStr,
                static_cast<int>(
                    std::strlen(heloStr))) <= 0) {
            return false;
        }

        received = NativeReceive(
            socket,
            reply.data(),
            static_cast<int>(reply.size()));
        // Source bug: the configured sender was copied without a bound.
        if (runtime::BoundedFormat(
                mailStr,
                sizeof(mailStr),
                "MAIL From: <%s>\r\n",
                sender) < 0) {
            return false;
        }
        if (NativeSend(
                socket,
                mailStr,
                static_cast<int>(
                    std::strlen(mailStr))) <= 0) {
            return false;
        }

        received = NativeReceive(
            socket,
            reply.data(),
            static_cast<int>(reply.size()));
        const char* receiver = receivers;
        while (receiver[0] != '\0') {
            // Source bug: each configured receiver was copied without a bound.
            if (runtime::BoundedFormat(
                    rcptStr,
                    sizeof(rcptStr),
                    "RCPT To: <%s>\r\n",
                    receiver) < 0) {
                return false;
            }
            if (NativeSend(
                    socket,
                    rcptStr,
                    static_cast<int>(
                        std::strlen(rcptStr))) <= 0) {
                return false;
            }
            received = NativeReceive(
                socket,
                reply.data(),
                static_cast<int>(reply.size()));
            receiver +=
                std::strlen(receiver) + 1;
        }

        if (NativeSend(
                socket,
                dataStr,
                static_cast<int>(
                    std::strlen(dataStr))) <= 0) {
            return false;
        }
        received = NativeReceive(
            socket,
            reply.data(),
            static_cast<int>(reply.size()));

        [[maybe_unused]] const std::time_t now =
            std::time(nullptr);
        sockaddr localAddress{};
#if defined(_WIN32)
        int localAddressLength =
            sizeof(localAddress);
#else
        socklen_t localAddressLength =
            sizeof(localAddress);
#endif
        ::getsockname(
            socket,
            &localAddress,
            &localAddressLength);

        // Current instructions contain no write to subjectStr anywhere in
        // the image. strlen therefore returns zero and Winsock send returns
        // zero, making this legacy path fail here. Preserve that shipping
        // behavior instead of manufacturing the old donor's subject text.
        if (NativeSend(
                socket,
                subjectStr,
                static_cast<int>(
                    std::strlen(subjectStr))) <= 0) {
            return false;
        }
        if (!sendFile(
                SocketToken(socket),
                filename)) {
            return false;
        }
        if (NativeSend(
                socket,
                finishStr,
                static_cast<int>(
                    std::strlen(finishStr))) <= 0) {
            return false;
        }
        received = NativeReceive(
            socket,
            reply.data(),
            static_cast<int>(reply.size()));
        if (NativeSend(
                socket,
                quitStr,
                static_cast<int>(
                    std::strlen(quitStr))) <= 0) {
            return false;
        }
        received = NativeReceive(
            socket,
            reply.data(),
            static_cast<int>(reply.size()));
        return true;
    }();

    CloseSocket(socket);
    StopSocketRuntime();
    return completed ? 1 : 0;
}

} // namespace

int GetLogicalAddress(
    const void* address,
    char* moduleName,
    std::uint32_t moduleNameSize,
    std::uint32_t& section,
    std::uint32_t& offset) {
    // L2Authd.exe 0x00439550, Exception.cpp:453-497.
#if defined(_WIN32)
    MEMORY_BASIC_INFORMATION memoryInfo{};
    if (::VirtualQuery(
            address,
            &memoryInfo,
            sizeof(memoryInfo)) == 0) {
        return 0;
    }
    HMODULE const module =
        static_cast<HMODULE>(
            memoryInfo.AllocationBase);
    if (::GetModuleFileNameA(
            module,
            moduleName,
            moduleNameSize) == 0) {
        return 0;
    }

    auto* const dosHeader =
        reinterpret_cast<IMAGE_DOS_HEADER*>(
            module);
    auto* const ntHeader =
        reinterpret_cast<IMAGE_NT_HEADERS*>(
            reinterpret_cast<unsigned char*>(
                module) +
            dosHeader->e_lfanew);
    IMAGE_SECTION_HEADER* currentSection =
        IMAGE_FIRST_SECTION(ntHeader);
    const std::uintptr_t rva =
        reinterpret_cast<std::uintptr_t>(
            address) -
        reinterpret_cast<std::uintptr_t>(
            module);
    for (std::uint32_t index = 0;
         index <
             ntHeader->FileHeader.NumberOfSections;
         ++index, ++currentSection) {
        const std::uint32_t start =
            currentSection->VirtualAddress;
        const std::uint32_t length = std::max(
            currentSection->SizeOfRawData,
            currentSection->Misc.VirtualSize);
        if (rva >= start &&
            rva <=
                static_cast<std::uintptr_t>(start) +
                    length) {
            section = index + 1;
            offset = static_cast<std::uint32_t>(
                rva - start);
            return 1;
        }
    }
    return 0;
#else
    Dl_info info{};
    if (::dladdr(address, &info) == 0 ||
        info.dli_fname == nullptr) {
        return 0;
    }
    if (moduleNameSize != 0) {
        std::snprintf(
            moduleName,
            moduleNameSize,
            "%s",
            info.dli_fname);
    }
    section = 0;
    offset = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(address) -
        reinterpret_cast<std::uintptr_t>(
            info.dli_fbase));
    return 1;
#endif
}

int getVideoDesc(
    char* description,
    int descriptionSize) {
    // L2Authd.exe 0x00438B60, Exception.cpp:201-219.
#if defined(_WIN32)
    HKEY key = nullptr;
    if (::RegOpenKeyExA(
            HKEY_LOCAL_MACHINE,
            "System\\CurrentControlSet\\Services\\Class\\Display\\0000",
            0,
            KEY_QUERY_VALUE,
            &key) != ERROR_SUCCESS) {
        return 0;
    }
    DWORD valueType = 0;
    DWORD size = static_cast<DWORD>(
        descriptionSize);
    const LSTATUS result = ::RegQueryValueExA(
        key,
        "DriverDesc",
        nullptr,
        &valueType,
        reinterpret_cast<LPBYTE>(description),
        &size);
    ::RegCloseKey(key);
    return result == ERROR_SUCCESS ? 1 : 0;
#else
    [[maybe_unused]] char* portableDescription =
        description;
    [[maybe_unused]] const int portableSize =
        descriptionSize;
    return 0;
#endif
}

namespace {

#if defined(_WIN32)
bool ReadCrashMemory(
    const void* source,
    void* destination,
    std::size_t size) noexcept {
    SIZE_T bytesRead = 0;
    return ::ReadProcessMemory(
               ::GetCurrentProcess(),
               source,
               destination,
               size,
               &bytesRead) != FALSE &&
        bytesRead == size;
}

void PrintInstructionBytes(
    void* logFile,
    std::uintptr_t instruction) {
    for (int index = 0;
         index < 16;
         ++index) {
        unsigned char value = 0;
        if (ReadCrashMemory(
                reinterpret_cast<const void*>(
                    instruction +
                    static_cast<std::uintptr_t>(
                        index)),
                &value,
                sizeof(value))) {
            hprintf(
                logFile,
                "%02x ",
                static_cast<unsigned int>(value));
        } else {
            hprintf(logFile, "?? ");
        }
    }
}
#else
void PortableStackWalk(
    void* logFile,
    const char* heading) {
    hprintf(logFile, "%s", heading);
    std::array<void*, 128> frames{};
    const int frameCount = ::backtrace(
        frames.data(),
        static_cast<int>(frames.size()));
    for (int index = 0;
         index < frameCount;
         ++index) {
        const auto address =
            reinterpret_cast<std::uintptr_t>(
                frames[static_cast<std::size_t>(
                    index)]);
        Dl_info info{};
        const bool hasInfo =
            ::dladdr(
                reinterpret_cast<const void*>(
                    address),
                &info) != 0;
        const std::uintptr_t frame =
            index + 1 < frameCount
            ? reinterpret_cast<std::uintptr_t>(
                  frames[static_cast<std::size_t>(
                      index + 1)])
            : 0;
        hprintf(
            logFile,
            "%016llx  %016llx  ",
            static_cast<unsigned long long>(
                address),
            static_cast<unsigned long long>(
                frame));
        if (hasInfo &&
            info.dli_sname != nullptr) {
            hprintf(
                logFile,
                "%s+%llx ",
                info.dli_sname,
                static_cast<unsigned long long>(
                    address -
                    reinterpret_cast<std::uintptr_t>(
                        info.dli_saddr)));
        }

        std::array<char, 260> moduleName{};
        std::uint32_t section = 0;
        std::uint32_t offset = 0;
        GetLogicalAddress(
            reinterpret_cast<const void*>(address),
            moduleName.data(),
            static_cast<std::uint32_t>(
                moduleName.size()),
            section,
            offset);
        hprintf(
            logFile,
            "%04x:%08x %s\r\n",
            section,
            offset,
            moduleName.data());
        hprintf(
            logFile,
            "Params: %08x %08x %08x %08x\r\n",
            0U,
            0U,
            0U,
            0U);
        hprintf(
            logFile,
            "[%s] Bytes at CS:EIP: ",
            hasInfo
            ? NativeFilePart(info.dli_fname)
            : "Unknown");
        for (int byte = 0;
             byte < 16;
             ++byte) {
            hprintf(logFile, "?? ");
        }
        hprintf(logFile, "\r\n\r\n");
    }
}
#endif

} // namespace

void ImageHelpStackWalk(
    void* logFile,
    _CONTEXT* context) {
    // L2Authd.exe 0x00439660, Exception.cpp:500-576.
#if defined(_WIN32) && !defined(_WIN64)
    hprintf(
        logFile,
        "\r\nCall Stack Information\r\n");
    ::SymSetOptions(SYMOPT_DEFERRED_LOADS);
    STACKFRAME frame{};
    frame.AddrPC.Offset = context->Eip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context->Esp;
    frame.AddrStack.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context->Ebp;
    frame.AddrFrame.Mode = AddrModeFlat;

    for (;;) {
        if (::StackWalk(
                IMAGE_FILE_MACHINE_I386,
                ::GetCurrentProcess(),
                ::GetCurrentThread(),
                &frame,
                context,
                nullptr,
                ::SymFunctionTableAccess,
                ::SymGetModuleBase,
                nullptr) == FALSE ||
            frame.AddrFrame.Offset == 0) {
            break;
        }
        hprintf(
            logFile,
            "%08x  %08x  ",
            frame.AddrPC.Offset,
            frame.AddrFrame.Offset);

        std::array<char,
                   sizeof(IMAGEHLP_SYMBOL) + 512>
            symbolBuffer{};
        auto* const symbol =
            reinterpret_cast<IMAGEHLP_SYMBOL*>(
                symbolBuffer.data());
        // The selected body writes 0x218 (the entire buffer size), not
        // sizeof(IMAGEHLP_SYMBOL). Retain that legacy DbgHelp contract.
        symbol->SizeOfStruct =
            static_cast<DWORD>(
                symbolBuffer.size());
        symbol->MaxNameLength = 512;
        DWORD displacement = 0;
        if (::SymGetSymFromAddr(
                ::GetCurrentProcess(),
                frame.AddrPC.Offset,
                &displacement,
                symbol) != FALSE) {
            hprintf(
                logFile,
                "%s+%x ",
                symbol->Name,
                displacement);
        }

        const char* filename = "Unknown";
        std::array<char, 260> executableName{};
        MEMORY_BASIC_INFORMATION memoryInfo{};
        if (::VirtualQuery(
                reinterpret_cast<const void*>(
                    static_cast<std::uintptr_t>(
                        frame.AddrPC.Offset)),
                &memoryInfo,
                sizeof(memoryInfo)) != 0 &&
            ::GetModuleFileNameA(
                static_cast<HMODULE>(
                    memoryInfo.AllocationBase),
                executableName.data(),
                static_cast<DWORD>(
                    executableName.size())) != 0) {
            filename = GetFilePart(
                executableName.data());
        }

        std::array<char, 260> moduleName{};
        std::uint32_t section = 0;
        std::uint32_t offset = 0;
        GetLogicalAddress(
            reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(
                    frame.AddrPC.Offset)),
            moduleName.data(),
            static_cast<std::uint32_t>(
                moduleName.size()),
            section,
            offset);
        hprintf(
            logFile,
            "%04x:%08x %s\r\n",
            section,
            offset,
            moduleName.data());
        hprintf(
            logFile,
            "Params: %08x %08x %08x %08x\r\n",
            frame.Params[0],
            frame.Params[1],
            frame.Params[2],
            frame.Params[3]);
        hprintf(
            logFile,
            "[%s] Bytes at CS:EIP: ",
            filename);
        PrintInstructionBytes(
            logFile,
            frame.AddrPC.Offset);
        hprintf(logFile, "\r\n\r\n");
    }
#elif defined(_WIN32)
    // The authoritative image is x86. This branch is the native-width
    // transport adaptation needed by the reconstructed Win64 target.
    hprintf(
        logFile,
        "\r\nCall Stack Information\r\n");
    ::SymSetOptions(SYMOPT_DEFERRED_LOADS);
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context->Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context->Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context->Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    for (;;) {
        if (::StackWalk64(
                IMAGE_FILE_MACHINE_AMD64,
                ::GetCurrentProcess(),
                ::GetCurrentThread(),
                &frame,
                context,
                nullptr,
                ::SymFunctionTableAccess64,
                ::SymGetModuleBase64,
                nullptr) == FALSE ||
            frame.AddrFrame.Offset == 0) {
            break;
        }
        hprintf(
            logFile,
            "%016llx  %016llx  ",
            static_cast<unsigned long long>(
                frame.AddrPC.Offset),
            static_cast<unsigned long long>(
                frame.AddrFrame.Offset));
        std::array<unsigned char,
                   sizeof(SYMBOL_INFO) + 512>
            symbolBuffer{};
        auto* const symbol =
            reinterpret_cast<SYMBOL_INFO*>(
                symbolBuffer.data());
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 512;
        DWORD64 displacement = 0;
        if (::SymFromAddr(
                ::GetCurrentProcess(),
                frame.AddrPC.Offset,
                &displacement,
                symbol) != FALSE) {
            hprintf(
                logFile,
                "%s+%llx ",
                symbol->Name,
                static_cast<unsigned long long>(
                    displacement));
        }
        std::array<char, 260> moduleName{};
        std::uint32_t section = 0;
        std::uint32_t offset = 0;
        GetLogicalAddress(
            reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(
                    frame.AddrPC.Offset)),
            moduleName.data(),
            static_cast<std::uint32_t>(
                moduleName.size()),
            section,
            offset);
        hprintf(
            logFile,
            "%04x:%08x %s\r\n",
            section,
            offset,
            moduleName.data());
        hprintf(
            logFile,
            "Params: %016llx %016llx %016llx %016llx\r\n",
            static_cast<unsigned long long>(
                frame.Params[0]),
            static_cast<unsigned long long>(
                frame.Params[1]),
            static_cast<unsigned long long>(
                frame.Params[2]),
            static_cast<unsigned long long>(
                frame.Params[3]));
        PrintInstructionBytes(
            logFile,
            static_cast<std::uintptr_t>(
                frame.AddrPC.Offset));
        hprintf(logFile, "\r\n\r\n");
    }
#else
    [[maybe_unused]] _CONTEXT* portableContext =
        context;
    PortableStackWalk(
        logFile,
        "\r\nCall Stack Information\r\n");
#endif
}

void IntelStackWalk(
    void* logFile,
    _CONTEXT* context) {
    // L2Authd.exe 0x00439A00, Exception.cpp:579-624.
#if defined(_WIN32)
    hprintf(
        logFile,
        "\r\nIntel Call Stack Information\r\n");
#  if defined(_WIN64)
    std::uintptr_t programCounter =
        static_cast<std::uintptr_t>(context->Rip);
    std::uintptr_t frameAddress =
        static_cast<std::uintptr_t>(context->Rbp);
#  else
    std::uintptr_t programCounter =
        static_cast<std::uintptr_t>(context->Eip);
    std::uintptr_t frameAddress =
        static_cast<std::uintptr_t>(context->Ebp);
#  endif
    for (;;) {
        std::array<char, 260> moduleName{};
        std::uint32_t section = 0;
        std::uint32_t offset = 0;
        GetLogicalAddress(
            reinterpret_cast<const void*>(
                programCounter),
            moduleName.data(),
            static_cast<std::uint32_t>(
                moduleName.size()),
            section,
            offset);
#  if defined(_WIN64)
        hprintf(
            logFile,
            "%016llx  %016llx  %04x:%08x %s\r\n",
            static_cast<unsigned long long>(
                programCounter),
            static_cast<unsigned long long>(
                frameAddress),
            section,
            offset,
            moduleName.data());
#  else
        hprintf(
            logFile,
            "%08x  %08x  %04x:%08x %s\r\n",
            static_cast<unsigned int>(
                programCounter),
            static_cast<unsigned int>(
                frameAddress),
            section,
            offset,
            moduleName.data());
#  endif
        hprintf(logFile, "Bytes at CS:EIP: ");
        PrintInstructionBytes(
            logFile,
            programCounter);
        hprintf(logFile, "\r\n\r\n");

        std::array<std::uintptr_t, 2> frame{};
        if (frameAddress == 0 ||
            !ReadCrashMemory(
                reinterpret_cast<const void*>(
                    frameAddress),
                frame.data(),
                sizeof(frame))) {
            break;
        }
        programCounter = frame[1];
        const std::uintptr_t nextFrame = frame[0];
        if ((nextFrame & 3U) != 0 ||
            nextFrame <= frameAddress) {
            break;
        }
        frameAddress = nextFrame;
    }
#else
    [[maybe_unused]] _CONTEXT* portableContext =
        context;
    PortableStackWalk(
        logFile,
        "\r\nIntel Call Stack Information\r\n");
#endif
}

namespace {

std::tm StartLocalTime() noexcept {
    const std::time_t start =
        static_cast<std::time_t>(
            CExceptionInit::g_timeStart);
    std::tm local{};
#if defined(_WIN32)
    ::localtime_s(&local, &start);
#else
    ::localtime_r(&start, &local);
#endif
    return local;
}

#if defined(_WIN32)
void WriteTargetStackDump(
    void* logFile,
    std::uintptr_t stackAddress) {
#  if defined(_WIN64)
    auto* const tib =
        reinterpret_cast<NT_TIB*>(
            ::NtCurrentTeb());
    std::uintptr_t stackBase =
        reinterpret_cast<std::uintptr_t>(
            tib->StackBase);
    const std::uintptr_t targetLimit =
        stackAddress +
        80 * sizeof(std::uintptr_t);
    if (stackBase > targetLimit) {
        stackBase = targetLimit;
    }
    std::size_t count = 0;
    for (std::uintptr_t current = stackAddress;
         current + sizeof(std::uintptr_t) <=
             stackBase;
         current += sizeof(std::uintptr_t),
                        ++count) {
        std::uintptr_t value = 0;
        if (!ReadCrashMemory(
                reinterpret_cast<const void*>(
                    current),
                &value,
                sizeof(value))) {
            hprintf(
                logFile,
                "Exception encountered during stack dump.\r\n");
            return;
        }
        if ((count & 7U) == 0) {
            hprintf(
                logFile,
                "%016llx: ",
                static_cast<unsigned long long>(
                    current));
        }
        const bool endOfLine =
            ((count + 1) & 7U) == 0 ||
            current +
                    2 * sizeof(std::uintptr_t) >
                stackBase;
        hprintf(
            logFile,
            "%016llx%s",
            static_cast<unsigned long long>(
                value),
            endOfLine ? "\r\n" : " ");
    }
#  else
    auto* const tib =
        reinterpret_cast<NT_TIB*>(
            ::NtCurrentTeb());
    std::uintptr_t stackBase =
        reinterpret_cast<std::uintptr_t>(
            tib->StackBase);
    const std::uintptr_t targetLimit =
        stackAddress + 80 * sizeof(std::uint32_t);
    if (stackBase > targetLimit) {
        stackBase = targetLimit;
    }

    std::array<char, 1000> buffer{};
    char* output = buffer.data();
    const char* const limit =
        buffer.data() + 950;
    int counter = 0;
    for (std::uintptr_t current = stackAddress;
         current + sizeof(std::uint32_t) <=
             stackBase;
         current += sizeof(std::uint32_t)) {
        std::uint32_t value = 0;
        if (!ReadCrashMemory(
                reinterpret_cast<const void*>(
                    current),
                &value,
                sizeof(value))) {
            hprintf(
                logFile,
                "Exception encountered during stack dump.\r\n");
            return;
        }
        if ((counter & 7) == 0) {
            output += std::sprintf(
                output,
                "%08x: ",
                static_cast<unsigned int>(
                    current));
        }
        ++counter;
        const char* const separator =
            (counter & 7) == 0 ||
                current +
                        2 * sizeof(std::uint32_t) >
                    stackBase
            ? "\r\n"
            : " ";
        output += std::sprintf(
            output,
            "%08x%s",
            value,
            separator);
        if (output > limit) {
            hprintf(
                logFile,
                "%s",
                buffer.data());
            buffer[0] = '\0';
            output = buffer.data();
        }
    }
    hprintf(logFile, "%s", buffer.data());
#  endif
}
#endif

void GenerateExceptionReport(
    _EXCEPTION_POINTERS* exceptionPointers) {
    // L2Authd.exe 0x00439C30, Exception.cpp:627-774.
    void* const logFile = OpenReportForAppend();
    if (logFile == nullptr) {
#if defined(_WIN32)
        ::OutputDebugStringA(
            "Error creating exception report");
#endif
        return;
    }

    const LocalSystemTime now =
        ReadLocalSystemTime();
    hprintf(
        logFile,
        "[(%d) %04d/%02d/%02d %02d:%02d:%02d]: =======================\r\n",
        CurrentThreadId32(),
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second);

#if defined(_WIN32)
    if (exceptionPointers == nullptr ||
        exceptionPointers->ExceptionRecord == nullptr ||
        exceptionPointers->ContextRecord == nullptr) {
        hprintf(
            logFile,
            "Unknown exception type in module Unknown at 0000:00000000.\r\n");
        hprintf(
            logFile,
            "Exception handler called in the Lineage2 Auth Server.\r\n");
        const std::tm start = StartLocalTime();
        hprintf(
            logFile,
            "start at %d/%d/%d %02d:%02d:%02d\r\n",
            start.tm_year + 1900,
            start.tm_mon + 1,
            start.tm_mday,
            start.tm_hour,
            start.tm_min,
            start.tm_sec);
        RecordSystemInformation(logFile);
        RecordModuleList(logFile);
        CloseLogFile(logFile);
        return;
    }

    EXCEPTION_RECORD* const exceptionRecord =
        exceptionPointers->ExceptionRecord;
    CONTEXT* const context =
        exceptionPointers->ContextRecord;
#  if defined(_WIN64)
    const std::uintptr_t instructionPointer =
        static_cast<std::uintptr_t>(context->Rip);
    const std::uintptr_t stackPointer =
        static_cast<std::uintptr_t>(context->Rsp);
#  else
    const std::uintptr_t instructionPointer =
        static_cast<std::uintptr_t>(context->Eip);
    const std::uintptr_t stackPointer =
        static_cast<std::uintptr_t>(context->Esp);
#  endif

    const char* moduleFilename = "Unknown";
    std::array<char, 260> modulePath{};
    MEMORY_BASIC_INFORMATION memoryInfo{};
    if (::VirtualQuery(
            reinterpret_cast<const void*>(
                instructionPointer),
            &memoryInfo,
            sizeof(memoryInfo)) != 0 &&
        ::GetModuleFileNameA(
            static_cast<HMODULE>(
                memoryInfo.AllocationBase),
            modulePath.data(),
            static_cast<DWORD>(
                modulePath.size())) != 0) {
        moduleFilename = GetFilePart(
            modulePath.data());
    }

#  if defined(_WIN64)
    hprintf(
        logFile,
        "%s in module %s at %04x:%016llx.\r\n",
        GetExceptionDescription(
            exceptionRecord->ExceptionCode),
        moduleFilename,
        context->SegCs,
        static_cast<unsigned long long>(
            instructionPointer));
#  else
    hprintf(
        logFile,
        "%s in module %s at %04x:%08x.\r\n",
        GetExceptionDescription(
            exceptionRecord->ExceptionCode),
        moduleFilename,
        context->SegCs,
        static_cast<unsigned int>(
            instructionPointer));
#  endif
    hprintf(
        logFile,
        "Exception handler called in the Lineage2 Auth Server.\r\n");

    const std::tm start = StartLocalTime();
    hprintf(
        logFile,
        "start at %d/%d/%d %02d:%02d:%02d\r\n",
        start.tm_year + 1900,
        start.tm_mon + 1,
        start.tm_mday,
        start.tm_hour,
        start.tm_min,
        start.tm_sec);
    RecordSystemInformation(logFile);

    if (exceptionRecord->ExceptionCode ==
            kAccessViolation &&
        exceptionRecord->NumberParameters >= 2) {
        const char* const operation =
            exceptionRecord->ExceptionInformation[0] ==
                    0
            ? "Read from"
            : "Write to";
        std::array<char, 1000> violation{};
#  if defined(_WIN64)
        std::sprintf(
            violation.data(),
            "%s location %016llx caused an access violation.\r\n",
            operation,
            static_cast<unsigned long long>(
                exceptionRecord
                    ->ExceptionInformation[1]));
#  else
        std::sprintf(
            violation.data(),
            "%s location %08x caused an access violation.\r\n",
            operation,
            static_cast<unsigned int>(
                exceptionRecord
                    ->ExceptionInformation[1]));
#  endif
        hprintf(logFile, "%s", violation.data());
    }

    hprintf(logFile, "\r\n");
    hprintf(logFile, "Registers:\r\n");
#  if defined(_WIN64)
    hprintf(
        logFile,
        "RAX=%016llx CS=%04x RIP=%016llx EFLGS=%08x\r\n",
        static_cast<unsigned long long>(context->Rax),
        context->SegCs,
        static_cast<unsigned long long>(context->Rip),
        context->EFlags);
    hprintf(
        logFile,
        "RBX=%016llx SS=%04x RSP=%016llx RBP=%016llx\r\n",
        static_cast<unsigned long long>(context->Rbx),
        context->SegSs,
        static_cast<unsigned long long>(context->Rsp),
        static_cast<unsigned long long>(context->Rbp));
    hprintf(
        logFile,
        "RCX=%016llx DS=%04x RSI=%016llx FS=%04x\r\n",
        static_cast<unsigned long long>(context->Rcx),
        context->SegDs,
        static_cast<unsigned long long>(context->Rsi),
        context->SegFs);
    hprintf(
        logFile,
        "RDX=%016llx ES=%04x RDI=%016llx GS=%04x\r\n",
        static_cast<unsigned long long>(context->Rdx),
        context->SegEs,
        static_cast<unsigned long long>(context->Rdi),
        context->SegGs);
#  else
    hprintf(
        logFile,
        "EAX=%08x CS=%04x EIP=%08x EFLGS=%08x\r\n",
        context->Eax,
        context->SegCs,
        context->Eip,
        context->EFlags);
    hprintf(
        logFile,
        "EBX=%08x SS=%04x ESP=%08x EBP=%08x\r\n",
        context->Ebx,
        context->SegSs,
        context->Esp,
        context->Ebp);
    hprintf(
        logFile,
        "ECX=%08x DS=%04x ESI=%08x FS=%04x\r\n",
        context->Ecx,
        context->SegDs,
        context->Esi,
        context->SegFs);
    hprintf(
        logFile,
        "EDX=%08x ES=%04x EDI=%08x GS=%04x\r\n",
        context->Edx,
        context->SegEs,
        context->Edi,
        context->SegGs);
#  endif
    hprintf(logFile, "Bytes at CS:EIP:\r\n");
    PrintInstructionBytes(
        logFile,
        instructionPointer);
    hprintf(logFile, "\r\nStack dump:\r\n");
    WriteTargetStackDump(
        logFile,
        stackPointer);

    if (::SymInitialize(
            ::GetCurrentProcess(),
            nullptr,
            TRUE) != FALSE) {
        ImageHelpStackWalk(logFile, context);
        ::SymCleanup(::GetCurrentProcess());
    } else {
        IntelStackWalk(logFile, context);
    }
#else
    // POSIX has no Win32 _EXCEPTION_POINTERS carrier. Keep the current
    // report owner and headings, then terminate the unavailable register and
    // minidump operations at the native backtrace boundary.
    [[maybe_unused]] _EXCEPTION_POINTERS*
        portableExceptionPointers =
            exceptionPointers;
    const std::string modulePath =
        CurrentExecutablePath();
    hprintf(
        logFile,
        "%s in module %s at %04x:%08x.\r\n",
        GetExceptionDescription(0),
        NativeFilePart(modulePath.c_str()),
        0,
        0);
    hprintf(
        logFile,
        "Exception handler called in the Lineage2 Auth Server.\r\n");
    const std::tm start = StartLocalTime();
    hprintf(
        logFile,
        "start at %d/%d/%d %02d:%02d:%02d\r\n",
        start.tm_year + 1900,
        start.tm_mon + 1,
        start.tm_mday,
        start.tm_hour,
        start.tm_min,
        start.tm_sec);
    RecordSystemInformation(logFile);
    IntelStackWalk(logFile, nullptr);
#endif

    RecordModuleList(logFile);
    CloseLogFile(logFile);
}

void GenerateMiniDump(
    _EXCEPTION_POINTERS* exceptionPointers) {
    // L2Authd.exe 0x0043A3C0, Exception.cpp:777-796.
#if defined(_WIN32)
    const LocalSystemTime now =
        ReadLocalSystemTime();
    std::array<char, 260> path{};
    std::sprintf(
        path.data(),
        " L2AuthD_%04d-%02d-%02d_%02d,%02d,%02d.dmp",
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second);
    HANDLE const file = ::CreateFileA(
        path.data(),
        GENERIC_WRITE,
        FILE_SHARE_WRITE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        constexpr char warning[] =
            "ERROR [exception.cpp] Cannot create the AuthD minidump file; the text crash report will still be attempted.\n";
        DWORD written = 0;
        ::WriteFile(::GetStdHandle(STD_ERROR_HANDLE), warning,
            static_cast<DWORD>(sizeof(warning) - 1), &written, nullptr);
        return;
    }
    MINIDUMP_EXCEPTION_INFORMATION information{};
    information.ThreadId = ::GetCurrentThreadId();
    information.ExceptionPointers =
        exceptionPointers;
    information.ClientPointers = FALSE;
    // ::MiniDumpWriteDump(
    //     ::GetCurrentProcess(),
    //     ::GetCurrentProcessId(),
    //     file,
    //     MiniDumpNormal,
    //     &information,
    //     nullptr,
    //     nullptr)
    const BOOL dumpWritten = ::MiniDumpWriteDump(
        ::GetCurrentProcess(),
        ::GetCurrentProcessId(),
        file,
        MiniDumpNormal,
        &information,
        nullptr,
        nullptr);
    if (dumpWritten == FALSE) {
        constexpr char warning[] =
            "ERROR [exception.cpp] MiniDumpWriteDump failed; the dump file may be empty or incomplete.\n";
        DWORD written = 0;
        ::WriteFile(::GetStdHandle(STD_ERROR_HANDLE), warning,
            static_cast<DWORD>(sizeof(warning) - 1), &written, nullptr);
    }
    // ::CloseHandle(file)
    if (!::CloseHandle(file)) {
        constexpr char warning[] =
            "ERROR [exception.cpp] Cannot close the AuthD minidump handle; dump completion is unconfirmed.\n";
        DWORD written = 0;
        ::WriteFile(::GetStdHandle(STD_ERROR_HANDLE), warning,
            static_cast<DWORD>(sizeof(warning) - 1), &written, nullptr);
    }
#else
    // There is no portable equivalent of a Win32 minidump. The accompanying
    // report still records native frames in GenerateExceptionReport.
    [[maybe_unused]] _EXCEPTION_POINTERS*
        portableExceptionPointers =
            exceptionPointers;
#endif
}

} // namespace

namespace {

#if defined(_WIN32)
void ShowModuleInfo(
    void* logFile,
    HMODULE moduleHandle) {
    // L2Authd.exe 0x004388B0, Exception.cpp:102-145.
    std::array<char, 260> moduleName{};
    if (::GetModuleFileNameA(
            moduleHandle,
            moduleName.data(),
            static_cast<DWORD>(moduleName.size())) == 0) {
        return;
    }

    auto* const dosHeader =
        reinterpret_cast<IMAGE_DOS_HEADER*>(
            moduleHandle);
    if (dosHeader->e_magic !=
        IMAGE_DOS_SIGNATURE) {
        return;
    }
    auto* const ntHeader =
        reinterpret_cast<IMAGE_NT_HEADERS*>(
            reinterpret_cast<unsigned char*>(
                moduleHandle) +
            dosHeader->e_lfanew);
    if (ntHeader->Signature !=
        IMAGE_NT_SIGNATURE) {
        return;
    }

    HANDLE const moduleFile = ::CreateFileA(
        moduleName.data(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    std::array<char, 100> timeBuffer{};
    DWORD fileSize = 0;
    if (moduleFile != INVALID_HANDLE_VALUE) {
        fileSize = ::GetFileSize(
            moduleFile,
            nullptr);
        FILETIME lastWriteTime{};
        if (::GetFileTime(
                moduleFile,
                nullptr,
                nullptr,
                &lastWriteTime) != FALSE) {
            ::wsprintfA(
                timeBuffer.data(),
                " - file date is ");
            PrintTime(
                timeBuffer.data() +
                    ::lstrlenA(
                        timeBuffer.data()),
                lastWriteTime);
        }
        ::CloseHandle(moduleFile);
    }

    hprintf(
        logFile,
        "%s, loaded at 0x%08x - %d bytes - %08x%s\r\n",
        moduleName.data(),
        static_cast<unsigned int>(
            reinterpret_cast<std::uintptr_t>(
                moduleHandle)),
        fileSize,
        ntHeader->FileHeader.TimeDateStamp,
        timeBuffer.data());
}
#else
void ShowPortableModuleInfo(
    void* logFile,
    const char* moduleName,
    std::uintptr_t moduleBase) {
    if (moduleName == nullptr ||
        moduleName[0] == '\0') {
        return;
    }

    struct stat status {};
    std::uint64_t fileSize = 0;
    std::array<char, 100> timeBuffer{};
    if (::stat(moduleName, &status) == 0) {
        fileSize = static_cast<std::uint64_t>(
            status.st_size);
        std::strcpy(
            timeBuffer.data(),
            " - file date is ");
        const std::size_t prefixLength = std::strlen(timeBuffer.data());
        PrintTime(
            timeBuffer.data() + prefixLength,
            timeBuffer.size() - prefixLength,
            status.st_mtime);
    }

    hprintf(
        logFile,
        "%s, loaded at 0x%016llx - %llu bytes - %08x%s\r\n",
        moduleName,
        static_cast<unsigned long long>(
            moduleBase),
        static_cast<unsigned long long>(
            fileSize),
        0U,
        timeBuffer.data());
}
#endif

void RecordModuleList(void* logFile) {
    // L2Authd.exe 0x00438A90, Exception.cpp:153-193.
    hprintf(
        logFile,
        "\r\n\tModule list: names, addresses, sizes, time stamps and file times:\r\n");
#if defined(_WIN32)
    SYSTEM_INFO systemInfo{};
    ::GetSystemInfo(&systemInfo);
    const std::uintptr_t pageSize =
        systemInfo.dwPageSize;
    constexpr std::uint64_t targetAddressLimit =
        UINT64_C(0x100000000);
    std::uint64_t address = 0;
    std::uintptr_t lastAllocationBase = 0;
    while (address < targetAddressLimit) {
        MEMORY_BASIC_INFORMATION memoryInfo{};
        if (::VirtualQuery(
                reinterpret_cast<const void*>(
                    static_cast<std::uintptr_t>(
                        address)),
                &memoryInfo,
                sizeof(memoryInfo)) == 0 ||
            memoryInfo.RegionSize == 0) {
            address += UINT64_C(0x10000);
            continue;
        }
        address += memoryInfo.RegionSize;
        const std::uintptr_t allocationBase =
            reinterpret_cast<std::uintptr_t>(
                memoryInfo.AllocationBase);
        if (memoryInfo.State == MEM_COMMIT &&
            allocationBase > lastAllocationBase) {
            lastAllocationBase = allocationBase;
#  if defined(_MSC_VER)
            __try {
                ShowModuleInfo(
                    logFile,
                    static_cast<HMODULE>(
                        memoryInfo.AllocationBase));
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
#  else
            ShowModuleInfo(
                logFile,
                static_cast<HMODULE>(
                    memoryInfo.AllocationBase));
#  endif
        }
        if (pageSize == 0) {
            break;
        }
    }
#elif defined(__APPLE__)
    const std::uint32_t imageCount =
        ::_dyld_image_count();
    for (std::uint32_t index = 0;
         index < imageCount;
         ++index) {
        const mach_header* const header =
            ::_dyld_get_image_header(index);
        ShowPortableModuleInfo(
            logFile,
            ::_dyld_get_image_name(index),
            reinterpret_cast<std::uintptr_t>(
                header));
    }
#elif defined(__linux__)
    struct CallbackState {
        void* logFile;
    } state{logFile};
    ::dl_iterate_phdr(
        [](dl_phdr_info* info,
           std::size_t,
           void* opaque) -> int {
            auto* const state =
                static_cast<CallbackState*>(opaque);
            const std::string fallback =
                CurrentExecutablePath();
            const char* const name =
                info->dlpi_name != nullptr &&
                    info->dlpi_name[0] != '\0'
                ? info->dlpi_name
                : fallback.c_str();
            ShowPortableModuleInfo(
                state->logFile,
                name,
                static_cast<std::uintptr_t>(
                    info->dlpi_addr));
            return 0;
        },
        &state);
#else
    const std::string executable =
        CurrentExecutablePath();
    ShowPortableModuleInfo(
        logFile,
        executable.c_str(),
        0);
#endif
}

void RecordSystemInformation(void* logFile) {
    // L2Authd.exe 0x00438BE0, Exception.cpp:222-248.
#if defined(_WIN32)
    FILETIME systemTime{};
    ::GetSystemTimeAsFileTime(&systemTime);
    std::array<char, 100> timeBuffer{};
    PrintTime(timeBuffer.data(), systemTime);
    hprintf(
        logFile,
        "Error occurred at %s.\r\n",
        timeBuffer.data());

    std::array<char, 260> modulePath{};
    if (::GetModuleFileNameA(
            nullptr,
            modulePath.data(),
            static_cast<DWORD>(modulePath.size())) == 0) {
        ::lstrcpyA(modulePath.data(), "Unknown");
    }
    std::array<char, 200> userName{};
    DWORD userNameSize =
        static_cast<DWORD>(userName.size());
    if (::GetUserNameA(
            userName.data(),
            &userNameSize) == FALSE) {
        ::lstrcpyA(userName.data(), "Unknown");
    }
    hprintf(
        logFile,
        "%s, run by %s.\r\n",
        modulePath.data(),
        userName.data());

    SYSTEM_INFO systemInfo{};
    ::GetSystemInfo(&systemInfo);
    hprintf(
        logFile,
        "%d processor(s), type %d.\r\n",
        systemInfo.dwNumberOfProcessors,
        systemInfo.dwProcessorType);

    MEMORYSTATUS memoryStatus{};
    memoryStatus.dwLength = sizeof(memoryStatus);
    ::GlobalMemoryStatus(&memoryStatus);
    const std::uint32_t megabytes =
        (static_cast<std::uint32_t>(
             memoryStatus.dwTotalPhys) +
         UINT32_C(0x000fffff)) >>
        20;
    hprintf(
        logFile,
        "%d MBytes physical memory.\r\n",
        megabytes);
#else
    const std::time_t now = std::time(nullptr);
    std::array<char, 100> timeBuffer{};
    PrintTime(timeBuffer.data(), timeBuffer.size(), now);
    hprintf(
        logFile,
        "Error occurred at %s.\r\n",
        timeBuffer.data());

    const std::string modulePath =
        CurrentExecutablePath();
    const std::string userName =
        CurrentUserName();
    hprintf(
        logFile,
        "%s, run by %s.\r\n",
        modulePath.c_str(),
        userName.c_str());

    const unsigned int processorCount =
        std::thread::hardware_concurrency();
    hprintf(
        logFile,
        "%d processor(s), type %d.\r\n",
        processorCount,
        0);
    hprintf(
        logFile,
        "%llu MBytes physical memory.\r\n",
        static_cast<unsigned long long>(
            (PhysicalMemoryBytes() +
             UINT64_C(0x000fffff)) >>
            20));
#endif
}

const char* GetExceptionDescription(
    std::uint32_t exceptionCode) noexcept {
    // L2Authd.exe 0x00438D20, Exception.cpp:253-293.
    struct ExceptionName {
        std::uint32_t code;
        const char* description;
    };
    constexpr std::array<ExceptionName, 24>
        exceptionNames{{
            {UINT32_C(0x40010005), "a Control-C"},
            {UINT32_C(0x40010008), "a Control-Break"},
            {UINT32_C(0x80000002), "a Datatype Misalignment"},
            {UINT32_C(0x80000003), "a Breakpoint"},
            {UINT32_C(0xc0000005), "an Access Violation"},
            {UINT32_C(0xc0000006), "an In Page Error"},
            {UINT32_C(0xc0000017), "a No Memory"},
            {UINT32_C(0xc000001d), "an Illegal Instruction"},
            {UINT32_C(0xc0000025), "a Noncontinuable Exception"},
            {UINT32_C(0xc0000026), "an Invalid Disposition"},
            {UINT32_C(0xc000008c), "a Array Bounds Exceeded"},
            {UINT32_C(0xc000008d), "a Float Denormal Operand"},
            {UINT32_C(0xc000008e), "a Float Divide by Zero"},
            {UINT32_C(0xc000008f), "a Float Inexact Result"},
            {UINT32_C(0xc0000090), "a Float Invalid Operation"},
            {UINT32_C(0xc0000091), "a Float Overflow"},
            {UINT32_C(0xc0000092), "a Float Stack Check"},
            {UINT32_C(0xc0000093), "a Float Underflow"},
            {UINT32_C(0xc0000094), "an Integer Divide by Zero"},
            {UINT32_C(0xc0000095), "an Integer Overflow"},
            {UINT32_C(0xc0000096), "a Privileged Instruction"},
            {UINT32_C(0xc00000fd), "a Stack Overflow"},
            {UINT32_C(0xc0000142), "a DLL Initialization Failed"},
            {UINT32_C(0xe06d7363), "a Microsoft C++ Exception"},
        }};
    const auto match = std::find_if(
        exceptionNames.begin(),
        exceptionNames.end(),
        [exceptionCode](
            const ExceptionName& candidate) {
            return candidate.code ==
                exceptionCode;
        });
    return match == exceptionNames.end()
        ? "Unknown exception type"
        : match->description;
}

} // namespace
