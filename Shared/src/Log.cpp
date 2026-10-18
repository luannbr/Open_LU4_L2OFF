#include "Shared/src/Log.h"
#include "Shared/src/Runtime/DirectLog.h"
#include "Shared/src/Runtime/SourceFile.h"
#include "Shared/src/Runtime/Utf8.h"
#include "Shared/src/Runtime/SharedMemoryName.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WindowsAnsi.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/ThreadManager.h"
#include "TextRuntime.h"

#include "Shared/src/Runtime/LocalLogScope.h"
#include "Shared/src/Runtime/OperatorLog.h"

#if defined(L2_BUILD_L2NPC)
#include "L2Core/src/inc/GuardProfile.h"
#include "L2Core/src/inc/MemoryMappedFile.h"
#include "Shared/src/Exception.h"
#elif defined(L2_BUILD_L2SERVER)
#include "Shared/src/Exception.h"
#elif !defined(L2_BUILD_CACHED)
#error "Shared/log.cpp must be compiled for a concrete server process"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

volatile bool g_OptionalErrorLogEnabled = false;
int g_bOnLogging[ThreadID::MAX]{};

#if defined(L2_BUILD_L2NPC)
// Owned by L2NPC/config.cpp. Use the existing ScriptDebugLog setting rather
// than maintaining a second copy of its enable state in the shared logger.
extern bool g_bScriptDebugLog;
#endif

namespace {

// Constant-initialized so early global-constructor diagnostics remain safe.
std::atomic<bool> portableDebugLoggingEnabled{false};

constexpr std::size_t kLogFileCount = 10;
constexpr std::uint32_t kReconnectMilliseconds = 600000;
constexpr std::uintptr_t kInvalidFileHandle = std::numeric_limits<std::uintptr_t>::max();

#if defined(L2_BUILD_CACHED)
constexpr int kLogThreadId = 18;
constexpr int kProcessLogServerType = 2;
constexpr const wchar_t* kConsoleMappingName = L"Global\\CONSOLE_LOG_CACHED";
#elif defined(L2_BUILD_L2SERVER)
constexpr int kLogThreadId = ThreadID::LOG;
constexpr int kProcessLogServerType = 3;
constexpr const wchar_t* kConsoleMappingName = L"Global\\CONSOLE_LOG_L2SERVER";
#else
constexpr int kLogThreadId = ThreadID::LOG;
constexpr int kProcessLogServerType = 4;
constexpr const wchar_t* kConsoleMappingName = L"Global\\CONSOLE_LOG_L2NPC";
#endif

constexpr std::array<const wchar_t*, kLogFileCount> LogCategory{
    L"system",
    L"in",
    L"out",
    L"db",
    L"audit",
    L"err",
    L"chat",
    L"chatout",
    L"long",
    L"ncguard",
};

constexpr std::array<const wchar_t*, 7> LogServerType{
    L"",
    L"authd",
    L"cached",
    L"server",
    L"npc",
    L"logd",
    L"updated",
};

constexpr std::array<std::uint32_t, 7> colors{
    0x00000000U,
    0x00ff0000U,
    0x000000ffU,
    0x00008cffU,
    0x00008cffU,
    0x00008cffU,
    0x00008cffU,
};

struct TimestampedUtf16Line {
    runtime::SystemTime time{};
    std::u16string text;
};

struct LocalLogFileStatus {
    std::filesystem::path requestedPath;
    std::chrono::steady_clock::time_point retryAfter;
    std::uint64_t openFailures = 0;
    std::uint64_t writeFailures = 0;
};

struct CLogPortableState {
    mutable std::recursive_mutex section;
    std::mutex consoleSection;
    std::array<std::mutex, kLogFileCount> fileSections;
    std::array<std::ofstream, kLogFileCount> files;
    std::array<std::filesystem::path, kLogFileCount> paths;
    // Host-only file retry/diagnostic state. The recovered CLog layout is intact.
    std::array<LocalLogFileStatus, kLogFileCount> fileStatus;
    runtime::log::LogOptions options;
    std::atomic<bool> connectionsEnabled{false};
};

CLogPortableState& State() {
    // Process globals can reach CLog from their own constructors before the
    // CLog translation unit's dynamic initializer. The target stores these
    // locks/files directly in CLog and therefore keeps them valid through its
    // destructor. Retain that lifetime for the portable OS sidecar instead of
    // allowing function-static destruction order to invalidate its mutexes.
    static auto* const state = new CLogPortableState;
    return *state;
}

struct SharedMappingState {
    void* base = nullptr;
    std::size_t size = 0;
#if !defined(_WIN32)
    int descriptor = -1;
    std::string name;
#endif
};

using SharedMappingStateMap = std::unordered_map<const CSharedLogLines*, SharedMappingState>;

std::mutex& SharedMappingMutex() {
    static auto* const mutex = new std::mutex;
    return *mutex;
}

SharedMappingStateMap& SharedMappings() {
    static auto* const states = new SharedMappingStateMap;
    return *states;
}

void RegisterSharedMapping(const CSharedLogLines& owner, SharedMappingState state) {
    std::lock_guard<std::mutex> guard(SharedMappingMutex());
    SharedMappings().emplace(&owner, std::move(state));
}

SharedMappingState RemoveSharedMapping(const CSharedLogLines& owner) {
    std::lock_guard<std::mutex> guard(SharedMappingMutex());
    const auto iterator = SharedMappings().find(&owner);
    if (iterator == SharedMappings().end()) {
        return {};
    }
    SharedMappingState state = std::move(iterator->second);
    SharedMappings().erase(iterator);
    return state;
}

TimestampedUtf16Line AddTimestampUtf16(const std::u16string& line, bool spaceAfterComma) {
    TimestampedUtf16Line output;
    output.time = runtime::crt::LocalSystemTimeNow<runtime::SystemTime>();

    char prefix[64]{};
    std::snprintf(
        prefix, sizeof(prefix), "%02u/%02u/%04u %02u:%02u:%02u.%03u,%s", output.time.wMonth, output.time.wDay,
        output.time.wYear, output.time.wHour, output.time.wMinute, output.time.wSecond, output.time.wMilliseconds,
        spaceAfterComma ? " " : ""
    );
    for (const unsigned char value : std::string(prefix)) {
        output.text.push_back(static_cast<char16_t>(value));
    }
    const auto visibleEnd = line.find(u'\0');
    output.text.append(
        line.begin(),
        visibleEnd == std::u16string::npos ? line.end() : line.begin() + static_cast<std::ptrdiff_t>(visibleEnd)
    );
    output.text.append(u"\r\n");
    return output;
}

void AppendCodePoint(std::u16string& output, std::uint32_t codePoint) {
    if (codePoint <= 0xffffU) {
        output.push_back(static_cast<char16_t>(codePoint));
        return;
    }
    codePoint -= 0x10000U;
    output.push_back(static_cast<char16_t>(0xd800U + (codePoint >> 10U)));
    output.push_back(static_cast<char16_t>(0xdc00U + (codePoint & 0x3ffU)));
}

std::u16string Utf8ToUtf16(const std::string& input) {
    std::u16string output;
    output.reserve(input.size());
    std::size_t index = 0;
    while (index < input.size()) {
        const auto first = static_cast<unsigned char>(input[index]);
        std::uint32_t codePoint = 0;
        std::size_t count = 0;
        std::uint32_t minimum = 0;
        if (first < 0x80U) {
            codePoint = first;
            count = 1;
        } else if ((first & 0xe0U) == 0xc0U) {
            codePoint = first & 0x1fU;
            count = 2;
            minimum = 0x80U;
        } else if ((first & 0xf0U) == 0xe0U) {
            codePoint = first & 0x0fU;
            count = 3;
            minimum = 0x800U;
        } else if ((first & 0xf8U) == 0xf0U) {
            codePoint = first & 0x07U;
            count = 4;
            minimum = 0x10000U;
        } else {
            AppendCodePoint(output, 0xfffdU);
            ++index;
            continue;
        }

        bool valid = index + count <= input.size();
        for (std::size_t part = 1; valid && part < count; ++part) {
            const auto continuation = static_cast<unsigned char>(input[index + part]);
            if ((continuation & 0xc0U) != 0x80U) {
                valid = false;
                break;
            }
            codePoint = (codePoint << 6U) | (continuation & 0x3fU);
        }
        if (!valid || codePoint < minimum || codePoint > 0x10ffffU || (codePoint >= 0xd800U && codePoint <= 0xdfffU)) {
            AppendCodePoint(output, 0xfffdU);
            ++index;
            continue;
        }
        AppendCodePoint(output, codePoint);
        index += count;
    }
    return output;
}

std::u16string NarrowLogBytesToTargetWide(std::string_view input) {
    std::u16string output;
    output.reserve(input.size());
    for (const char value : input) {
        // The shipping loop assigns a signed MSVC char directly to WCHAR.
        // Preserve that sign extension instead of treating the legacy narrow
        // logging API as UTF-8.
        output.push_back(static_cast<char16_t>(static_cast<std::int16_t>(static_cast<signed char>(value))));
    }
    return output;
}

constexpr const char* OperatorApplication() noexcept {
#if defined(L2_BUILD_CACHED)
    return "cached";
#elif defined(L2_BUILD_L2SERVER)
    return "l2server";
#else
    return "l2npc";
#endif
}

bool IsOperatorLog(int type) noexcept {
    return type == LOG_NORMAL || type == LOG_WARN || type == LOG_ERROR || type == LOG_OPT_ERROR || type == LOG_LOCAL ||
        type == LOG_DEBUG;
}

bool IsAccountingConsoleCopy(int type, std::u16string_view line) noexcept {
    // These channels carry records whose first CSV field is a numeric log ID.
    // Some also carry free-text diagnostics, so channel selection alone is
    // too little. Do not check or change the contents of human channels.
    switch (type) {
        case LOG_INNER:
        case LOG_OUTER:
        case LOG_BOTH:
        case LOG_DB:
        case LOG_AUDIT:
        case LOG_CHAT:
        case LOG_LONG_KEEP:
        case LOG_NCGUARD:
            break;
        default:
            return false;
    }
    std::size_t position = 0;
    if (!line.empty() && (line.front() == u'-' || line.front() == u'+')) {
        ++position;
    }
    const std::size_t firstDigit = position;
    while (position < line.size() && line[position] >= u'0' && line[position] <= u'9') {
        ++position;
    }
    return position > firstDigit && position < line.size() && line[position] == u',';
}

bool SuppressDebugLog(int type) noexcept {
    if (type != LOG_DEBUG) {
        return false;
    }
#if defined(L2_BUILD_L2NPC)
    return !g_bScriptDebugLog;
#else
    // Other products keep their existing policy until their debug
    // configuration and destinations have been reviewed.
    // return true
    return !portableDebugLoggingEnabled.load(std::memory_order_relaxed);
#endif
}

runtime::logging::Level OperatorLevel(int type) noexcept {
    using runtime::logging::Level;
    return type == LOG_ERROR || type == LOG_OPT_ERROR ? Level::Error
        : type == LOG_WARN                            ? Level::Warning
        : type == LOG_DEBUG                           ? Level::Debug
                                                      : Level::Info;
}

std::u16string NarrowOperatorLogToUtf16(int type, std::string_view input) {
    return IsOperatorLog(type) ? Utf8ToUtf16(std::string(input)) : NarrowLogBytesToTargetWide(input);
}

// Human diagnostics have a larger bounded formatter. The recovered 999-unit
// formatter below remains the accounting/protocol path. At most 8191 UTF-8
// bytes also fit the 20480-byte LogD frame after UTF-16 conversion and headers.
constexpr std::size_t kOperatorMessageCapacity = 8192;

std::u16string BoundOperatorMessage(std::string_view text, std::string_view suffix = {}) {
    constexpr std::string_view truncated = " [truncated]";
    if (suffix.empty() && text.size() >= kOperatorMessageCapacity) {
        suffix = truncated;
    }
    const std::size_t limit = kOperatorMessageCapacity - 1 - suffix.size();
    std::size_t length = std::min(text.size(), limit);
    // Keep a valid UTF-8 character whole when making room for the marker.
    while (length > 0 && length < text.size() && (static_cast<unsigned char>(text[length]) & 0xc0U) == 0x80U) {
        --length;
    }
    std::string output(text.substr(0, length));
    output.append(suffix);
    return Utf8ToUtf16(output);
}

std::u16string FormatOperatorMessage(const char* format, va_list arguments) {
    if (!format) {
        return u"[logging] Cannot format a message: the format pointer is null.";
    }
    std::array<char, kOperatorMessageCapacity> buffer{};
    const int savedErrno = errno;
    errno = 0;
    va_list copy;
    va_copy(copy, arguments);
    const int length = std::vsnprintf(buffer.data(), buffer.size(), format, copy);
    va_end(copy);
    const int formatError = errno;
    errno = savedErrno;
    if (length >= 0 && static_cast<std::size_t>(length) < buffer.size()) {
        return BoundOperatorMessage(std::string_view(buffer.data(), static_cast<std::size_t>(length)));
    }
    std::size_t available = 0;
    while (available < buffer.size() - 1 && buffer[available] != '\0') {
        ++available;
    }
    if (length >= 0) {
        // return BoundOperatorMessage(std::string_view(buffer.data(), available), " [truncated]")
        // A successful truncated vsnprintf fills the buffer up to its final
        // terminator. Embedded NUL bytes do not end the formatted message.
        return BoundOperatorMessage(std::string_view(buffer.data(), buffer.size() - 1), " [truncated]");
    }
    char suffix[96]{};
    std::snprintf(suffix, sizeof(suffix), " [incomplete: narrow message formatting failed; errno %d]", formatError);
    return BoundOperatorMessage(std::string_view(buffer.data(), available), suffix);
}

std::u16string FormatOperatorMessage(const wchar_t* format, va_list arguments) {
    if (!format) {
        return u"[logging] Cannot format a wide message: the format pointer is null.";
    }
    std::array<wchar_t, kOperatorMessageCapacity> buffer{};
    const int savedErrno = errno;
    errno = 0;
    // The shared Windows-wide adapter handles %s/%S/I64 and non-ASCII text
    // without changing the process locale. Format only once: %n may write.
    const int length = WindowsVswprintf(buffer.data(), buffer.size(), format, arguments);
    const int formatError = errno;
    errno = savedErrno;
    if (length >= 0 && static_cast<std::size_t>(length) < buffer.size()) {
        return BoundOperatorMessage(WideToUtf8(std::wstring_view(buffer.data(), static_cast<std::size_t>(length))));
    }
    std::size_t available = 0;
    while (available < buffer.size() - 1 && buffer[available] != L'\0') {
        ++available;
    }
    char suffix[128]{};
    // vswprintf does not portably distinguish a size limit from encoding or
    // format failure. Keep any prefix it supplied without inventing a cause.
    std::snprintf(
        suffix, sizeof(suffix), " [incomplete: wide formatting failed or exceeded 8191 characters; errno %d]", formatError
    );
    return BoundOperatorMessage(WideToUtf8(std::wstring_view(buffer.data(), available)), suffix);
}

std::u16string OperatorRingPreview(const std::u16string& text) {
    constexpr std::size_t limit = 999; // LogLine::str includes one terminator.
    if (text.size() <= limit) {
        return text;
    }
    constexpr std::u16string_view suffix = u" [truncated]";
    std::size_t length = limit - suffix.size();
    if (text[length - 1] >= 0xd800 && text[length - 1] <= 0xdbff) {
        --length;
    }
    std::u16string preview(text, 0, length);
    preview.append(suffix);
    return preview;
}

// Storage diagnostics bypass CLog so a broken destination cannot recurse into
// itself. Callers hold the matching fileSections lock for the failure counters.
void ReportLocalFileFailure(
    const char* operation,
    const std::filesystem::path& path,
    int error,
    std::uint64_t& failures,
    const std::error_category& category = std::generic_category()
) {
    const int savedErrno = errno;
    // ++failures.
    // Saturate diagnostic counts. Wrapping to zero would lose pending failures.
    if (failures != std::numeric_limits<std::uint64_t>::max()) {
        ++failures;
    }
    if ((failures & (failures - 1)) == 0) {
        std::array<char, 8192> message{};
        // const std::error_code code(error ? error : EIO, category)
        // A failed stream operation can leave errno unset. Do not invent EIO.
        const std::error_code code(error, category);
        // std::snprintf(message.data(), message.size(),
        //     "component=logging event=storage_failure operation=%s error_code=%d error_domain=%s reason=\"%s\"
        //     failures=%llu path=\"%s\"", operation, code.value(), category.name(), code.message().c_str(),
        //     static_cast<unsigned long long>(failures), path.u8string().c_str())
        // std::snprintf(message.data(), message.size(),
        //     "[logging] Log operation %s failed (error %d, %s: \"%s\". %llu failures. Path \"%s\").",
        //     operation, code.value(), category.name(), code.message().c_str(),
        //     static_cast<unsigned long long>(failures), path.u8string().c_str())
        std::snprintf(
            message.data(), message.size(),
            "[logging] Log operation %s failed (error %d, %s: \"%s\"; %llu failures; path \"%s\").", operation, code.value(),
            category.name(), error ? code.message().c_str() : "no error code supplied",
            static_cast<unsigned long long>(failures), runtime::Utf8Bytes(path.u8string()).c_str()
        );
        // runtime::logging::Write(OperatorApplication(), runtime::logging::Level::Error,
        //                           message.data())
        runtime::logging::Write(OperatorApplication(), runtime::logging::Level::Error, message.data(), __FILE__);
    }
    errno = savedErrno;
}

void ReportLocalFileRecovery(const char* operation, const std::filesystem::path& path, std::uint64_t& failures) {
    if (failures == 0) {
        return;
    }
    const int savedErrno = errno;
    std::array<char, 8192> message{};
    // std::snprintf(message.data(), message.size(),
    //     "component=logging event=storage_recovered operation=%s failures=%llu path=\"%s\"",
    //     operation, static_cast<unsigned long long>(failures), path.u8string().c_str())
    // std::snprintf(message.data(), message.size(),
    //     "[logging] Log %s recovered after %llu failures (path \"%s\").",
    //     operation, static_cast<unsigned long long>(failures), path.u8string().c_str())
    // The write counter also includes flush/close failures. A new successful
    // operation does not restore records lost during any earlier failure.
    // std::snprintf(message.data(), message.size(),
    //     "[logging] Log operation %s succeeded after %llu earlier file failures (path \"%s\"). Previously failed records
    //     may be incomplete.", operation, static_cast<unsigned long long>(failures), path.u8string().c_str())
    std::snprintf(
        message.data(), message.size(),
        "[logging] Log operation %s succeeded after %s%llu earlier file failures (path \"%s\"). Previously failed records "
        "may be incomplete.",
        operation, failures == std::numeric_limits<std::uint64_t>::max() ? "at least " : "",
        static_cast<unsigned long long>(failures), runtime::Utf8Bytes(path.u8string()).c_str()
    );
    // runtime::logging::Write(OperatorApplication(), runtime::logging::Level::Info,
    //                           message.data())
    runtime::logging::Write(OperatorApplication(), runtime::logging::Level::Info, message.data(), __FILE__);
    failures = 0;
    errno = savedErrno;
}

// The owner section protects date/config selection. The per-file lock protects
// the stream, actual path and retry state from writes, panic flush and shutdown.
bool OpenLocalLogFile(CLogPortableState& state, std::size_t index, const std::filesystem::path& path) {
    std::lock_guard<std::mutex> guard(state.fileSections[index]);
    auto& output = state.files[index];
    auto& status = state.fileStatus[index];
    if (status.requestedPath != path) {
        status.requestedPath = path;
        status.retryAfter = {};
    }
    if (output.is_open() && output.good() && state.paths[index] == path) {
        ReportLocalFileRecovery("open", path, status.openFailures);
        return true;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now < status.retryAfter) {
        return false;
    }
    status.retryAfter = now + std::chrono::seconds(1);

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        ReportLocalFileFailure("create_directory", path.parent_path(), error.value(), status.openFailures, error.category());
        return false;
    }
    std::ofstream replacement;
    errno = 0;
    replacement.open(path, std::ios::binary | std::ios::out | std::ios::app);
    if (!replacement.is_open()) {
        ReportLocalFileFailure("open", path, errno, status.openFailures);
        return false;
    }

    if (output.is_open()) {
        // A prior failed write has already reported the bad stream. Calling
        // flush on its failbit would report another error without doing I/O.
        if (output.good()) {
            errno = 0;
            output.flush();
            if (!output) {
                ReportLocalFileFailure("flush", state.paths[index], errno, status.writeFailures);
            }
        }
        // Clear the old stream state only to observe a distinct close failure.
        output.clear();
        errno = 0;
        output.close();
        if (!output) {
            ReportLocalFileFailure("close", state.paths[index], errno, status.writeFailures);
        }
    }
    output.swap(replacement);
    state.paths[index] = path;
    status.retryAfter = {};
    ReportLocalFileRecovery("open", path, status.openFailures);
    return true;
}

bool WriteLocalLogFile(CLogPortableState& state, std::size_t index, std::string_view text) {
    std::lock_guard<std::mutex> guard(state.fileSections[index]);
    auto& output = state.files[index];
    if (!output.is_open() || !output.good()) {
        return false;
    }
    errno = 0;
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.flush();
    if (!output) {
        ReportLocalFileFailure("write_or_flush", state.paths[index], errno, state.fileStatus[index].writeFailures);
        return false;
    }
    // ReportLocalFileRecovery("write", state.paths[index], state.fileStatus[index].writeFailures)
    // Noise: a successful empty flush does not prove that record writes recovered.
    if (!text.empty()) {
        ReportLocalFileRecovery("write", state.paths[index], state.fileStatus[index].writeFailures);
    }
    return true;
}

void FlushLocalLogFile(CLogPortableState& state, std::size_t index, bool close) {
    std::lock_guard<std::mutex> guard(state.fileSections[index]);
    auto& output = state.files[index];
    if (!output.is_open()) {
        return;
    }
    if (output.good()) {
        errno = 0;
        output.flush();
        if (!output) {
            ReportLocalFileFailure("flush", state.paths[index], errno, state.fileStatus[index].writeFailures);
        }
    }
    if (close) {
        output.clear();
        errno = 0;
        output.close();
        if (!output) {
            ReportLocalFileFailure("close", state.paths[index], errno, state.fileStatus[index].writeFailures);
        }
    }
}

std::string NarrowAsciiLogName(const wchar_t* input) {
    std::string output;
    while (*input) {
        output.push_back(static_cast<char>(*input++));
    }
    return output;
}

std::u16string LogWideToUtf16(std::wstring_view input) {
    std::u16string output;
    output.reserve(input.size());
    for (const wchar_t value : input) {
        std::uint32_t codePoint = static_cast<std::uint32_t>(value);
#if WCHAR_MAX <= 0xffff
        output.push_back(static_cast<char16_t>(codePoint));
#else
        if (codePoint > 0x10ffffU || (codePoint >= 0xd800U && codePoint <= 0xdfffU)) {
            codePoint = 0xfffdU;
        }
        AppendCodePoint(output, codePoint);
#endif
    }
    return output;
}

std::u16string TargetUtf16OverflowFormat(std::u16string text, LogType type) {
    if (type != LOG_ERROR && type != LOG_NORMAL && type != LOG_CHAT) {
        return {};
    }

    if (type == LOG_CHAT) {
        if (text.size() > 199U) {
            text.resize(199U);
        }
        return text;
    }

    // Current 0x0045e688 zeroes a 1000-WCHAR destination and then performs
    // memcpy(destination, format, 999). This copies 499 whole
    // UTF-16 units plus only the low byte of the 500th unit.
    if (text.size() > 499U) {
        const char16_t partial = static_cast<char16_t>(text[499] & 0x00ffU);
        text.resize(499U);
        if (partial != u'\0') {
            text.push_back(partial);
        }
    }
    return text;
}

std::u16string TargetWideOverflowFormat(const wchar_t* format, LogType type) {
    return TargetUtf16OverflowFormat(LogWideToUtf16(format), type);
}

std::string Utf16ForLocalTransport(const std::u16string& input) {
    // Local encoding fallback may set errno. Logging must keep the caller's
    // error while preserving the existing byte conversion and fallback policy.
    struct RestoreErrno {
        int value = errno;
        ~RestoreErrno() { errno = value; }
    } restoreErrno;
    return ::portable::WindowsAnsiFromUtf16(input);
}

int FileIndexForLogType(int logType) {
    switch (logType) {
        case LOG_ERROR:
            return 5;
        case LOG_BOTH:
            return 1;
        case LOG_DB:
            return 3;
        case LOG_AUDIT:
            return 4;
        case LOG_CHAT:
            return 6;
        case LOG_LONG_KEEP:
            return 8;
        case LOG_NCGUARD:
            return 9;
        default:
            return 0;
    }
}

void CopyTargetPathToBuffer(std::string_view path, std::array<char16_t, 256>& output) {
    output.fill(u'\0');
    const std::u16string encoded = Utf8ToUtf16(std::string(path));
    const std::size_t count = std::min(output.size() - 1, encoded.size());
    std::copy_n(encoded.begin(), count, output.begin());
}

#if !defined(_WIN32)
std::string PosixSharedMemoryName(const wchar_t* mappingName) {
    std::string result{"/"};
    if (!mappingName) {
        return result + "L2NPC_CONSOLE_LOG";
    }
    const wchar_t* visible = mappingName;
    while (*visible) {
        const wchar_t value = *visible++;
        if ((value >= L'a' && value <= L'z') || (value >= L'A' && value <= L'Z') || (value >= L'0' && value <= L'9') ||
            value == L'_' || value == L'-') {
            result.push_back(static_cast<char>(value));
        } else {
            result.push_back('_');
        }
    }
    return result;
}
#endif

void ReportSharedLogMappingError(bool mappingHandleCreated, int nativeError) {
#if defined(_WIN32)
    const wchar_t* const format = mappingHandleCreated ? L"m_lines is null, GetLastError %d"
                                                       : L"linesHandle is null GetLastError(%d), \r\n"
                                                         L"Move Control Panel->Administrative Tools->\r\n"
                                                         L"Local Security Policy->\r\n"
                                                         L"User Rights Assignment->\r\n"
                                                         L"Create Global Object, Add your account";
    std::array<wchar_t, 1024> message{};
    std::swprintf(message.data(), message.size(), format, nativeError);
    ::MessageBoxW(nullptr, message.data(), L"", 0);
#else
    // Keep stderr byte-oriented for the portable signal/error reporters and
    // report the host fallback instead of Win32 account-policy instructions.
    // std::fprintf(
    //     stderr,
    //     "[portable] named console log %s failed (errno=%d: %s). "
    //     "trying anonymous shared memory\n",
    //     mappingHandleCreated ? "mapping" : "creation",
    //     nativeError,
    //     std::strerror(nativeError))
    L2_LOG_DIRECT(
        stderr, runtime::logging::Level::Warning,
        "[logging] Named console log %s failed (errno %d: %s); trying anonymous shared memory.",
        mappingHandleCreated ? "mapping" : "creation", nativeError, std::strerror(nativeError)
    );
    // std::fflush(stderr), // L2_LOG_DIRECT flushes the complete record.
#endif
}

class LogAddVHelper {
public:
    LogAddVHelper() { ++g_bOnLogging[CThreadManager::GetThreadID()]; }

    ~LogAddVHelper();
};

LogAddVHelper::~LogAddVHelper() { --g_bOnLogging[CThreadManager::GetThreadID()]; }


} // namespace

CLogLines::CLogLines() : m_lines(nullptr), m_tail(nullptr) {}

CLogLines::CLogLines(unsigned rowCapacity) : m_lines(nullptr), m_tail(nullptr), m_rowCapacity(rowCapacity) {}

CLogLines::~CLogLines() {
    // Current scalar deleting destructor 0x004ecd78 clears both borrowed
    // ring pointers before optional object deletion.
    m_lines = nullptr;
    m_tail = nullptr;
}

unsigned CLogLines::GetRowCapacity() const { return m_rowCapacity; }

int CLogLines::GetIndex(int index) const { return static_cast<int>((m_rowCapacity - 1U) & static_cast<unsigned>(index)); }

void CLogLines::TextOutW(HDC__* deviceContext, int y, int index) const {
    // Current Cached.exe 0x004eccec is this exact row presentation helper:
    // choose the color from LogType and draw the kept UTF-16 text/length.
#if defined(_WIN32)
    const LogLine& line = m_lines[index];
    if (line.str[0] != u'\0') {
        ::SetTextColor(
            reinterpret_cast<HDC>(deviceContext), static_cast<COLORREF>(colors[static_cast<std::size_t>(line.type)])
        );
        ::TextOutW(reinterpret_cast<HDC>(deviceContext), 0, y, reinterpret_cast<LPCWSTR>(line.str.data()), line.len);
    }
#else
    // GDI presentation has no cross-platform equivalent. The identical
    // mapped records remain available to the portable status front end.
#endif
}

void CLogLines::WriteLog(LogType type, wchar_t* text, int length) {
    const std::u16string encoded = LogWideToUtf16(text);
    WriteLogUtf16(type, encoded.c_str(), length);
}

void CLogLines::WriteLogUtf16(LogType type, const char16_t* text, int length) {
    // Current Cached.exe unnamed CLogLines helper body 0x004ecfe0.
    LogLine& line = m_lines[GetIndex(static_cast<int>(*m_tail))];
    line.type = type;
    const std::size_t count = std::char_traits<char16_t>::length(text);
    if (count >= line.str.size()) {
        // wcscpy_s clears the destination on a range failure. All production
        // callers supply the 999-unit formatting buffer, so this is only the
        // target CRT failure edge.
        line.str[0] = u'\0';
    } else {
        std::copy_n(text, count + 1, line.str.begin());
    }
    line.len = length;
    SetNextTail();
}

unsigned CLogLines::GetTail() const { return *m_tail; }

void CLogLines::SetNextTail() { *m_tail = (m_rowCapacity - 1U) & (*m_tail + 1U); }

CNormalLogLines::CNormalLogLines(unsigned rowCapacity) : CLogLines(rowCapacity), m_realTail(0) {
    if (rowCapacity == 0) {
        return;
    }
    m_lines = new LogLine[rowCapacity]{};
    m_tail = &m_realTail;
}

CNormalLogLines::~CNormalLogLines() { delete[] m_lines; }

CSharedLogLines::CSharedLogLines(unsigned rowCapacity, const wchar_t* mappingName) :
    CLogLines(rowCapacity), m_linesHandle(nullptr), m_tailHandle(nullptr) {
    std::wstring scopedMappingName;
    if (mappingName) {
        const std::string logicalName = WideToUtf8(mappingName);
        const std::string scopedName = runtime::ScopedSharedMemoryName(logicalName);
        if (scopedName != logicalName) {
            scopedMappingName = Utf8ToWideStrict(scopedName);
            mappingName = scopedMappingName.c_str();
        }
    }
    // Current Cached.exe 0x004ecda8. The startup owner passes 0x80 rows and
    // the named mapping Global\\CONSOLE_LOG_CACHED to this constructor.
    const std::size_t lineBytes = static_cast<std::size_t>(rowCapacity) * sizeof(LogLine);
    const std::uint32_t mappingSize32 =
        static_cast<std::uint32_t>(rowCapacity * static_cast<std::uint32_t>(sizeof(LogLine)) + sizeof(unsigned));
    const std::size_t mappingSize = mappingSize32;
    SharedMappingState state;
    state.size = mappingSize;

#if defined(_WIN32)
    auto* securityDescriptor = static_cast<PSECURITY_DESCRIPTOR>(std::malloc(sizeof(SECURITY_DESCRIPTOR)));
    SECURITY_ATTRIBUTES attributes{};
    // Current 0x0045d104 passes the malloc result directly to both security
    // descriptor APIs. Allocation failure is not normalized into the
    // no-security-descriptor fallback.
    ::InitializeSecurityDescriptor(securityDescriptor, SECURITY_DESCRIPTOR_REVISION);
    ::SetSecurityDescriptorDacl(securityDescriptor, TRUE, nullptr, FALSE);
    attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
    attributes.lpSecurityDescriptor = securityDescriptor;
    attributes.bInheritHandle = FALSE;
    HANDLE handle = static_cast<HANDLE>(
#if defined(L2_BUILD_L2NPC)
        L2CreateFileMapping(
            INVALID_HANDLE_VALUE, &attributes, PAGE_READWRITE, 0, static_cast<unsigned long>(mappingSize),
            const_cast<wchar_t*>(mappingName)
        )
#else
        ::CreateFileMappingW(
            INVALID_HANDLE_VALUE, &attributes, PAGE_READWRITE, 0, static_cast<DWORD>(mappingSize), mappingName
        )
#endif
    );
    if (!handle) {
        handle = static_cast<HANDLE>(
#if defined(L2_BUILD_L2NPC)
            L2CreateFileMapping(
                INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast<unsigned long>(mappingSize),
                const_cast<wchar_t*>(mappingName)
            )
#else
            ::CreateFileMappingW(
                INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(mappingSize), mappingName
            )
#endif
        );
    }
    if (handle) {
        state.base = ::MapViewOfFile(handle, FILE_MAP_WRITE, 0, 0, 0);
    }
    DWORD mappingError = state.base ? ERROR_SUCCESS : ::GetLastError();
    constexpr std::wstring_view globalPrefix = L"Global\\";
    if (!state.base && mappingError == ERROR_ACCESS_DENIED && mappingName &&
        std::wstring_view(mappingName).starts_with(globalPrefix)) {
        // The Global namespace needs SeCreateGlobalPrivilege. A normal user
        // process does not have it, for example a server started from an IDE.
        // Use the same name in the session namespace. A status window in the
        // same session can open it there.
        if (handle) {
            ::CloseHandle(handle);
        }
        const std::wstring localName =
            L"Local\\" + std::wstring(std::wstring_view(mappingName).substr(globalPrefix.size()));
        handle = ::CreateFileMappingW(
            INVALID_HANDLE_VALUE, &attributes, PAGE_READWRITE, 0, static_cast<DWORD>(mappingSize), localName.c_str()
        );
        if (handle) {
            state.base = ::MapViewOfFile(handle, FILE_MAP_WRITE, 0, 0, 0);
        }
        if (state.base) {
            L2_LOG_DIRECT(
                stderr, runtime::logging::Level::Info,
                "[logging] The account cannot create global objects. The console log mapping is \"%s\" in this session.",
                WideToUtf8(localName.c_str()).c_str()
            );
        } else {
            mappingError = ::GetLastError();
        }
    }
    if (!state.base) {
        // Like the POSIX path below, keep the same LogLine/tail layout in an
        // unnamed mapping. Only external status window attachment is
        // unavailable for this process run.
        // The original error box is modal, so a fallback must not raise it.
        L2_LOG_DIRECT(
            stderr, runtime::logging::Level::Warning,
            "[logging] Cannot create the shared console log mapping (error %lu); using a private buffer. "
            "Grant 'Create global objects' to the account to attach the status window.",
            static_cast<unsigned long>(mappingError)
        );
        if (handle) {
            ::CloseHandle(handle);
        }
        handle = ::CreateFileMappingW(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(mappingSize), nullptr
        );
        if (handle) {
            state.base = ::MapViewOfFile(handle, FILE_MAP_WRITE, 0, 0, 0);
        }
    }
    m_linesHandle = handle;
#else
    state.name = PosixSharedMemoryName(mappingName);
    m_linesHandle =
#if defined(L2_BUILD_L2NPC)
        L2CreateFileMapping(
            reinterpret_cast<void*>(static_cast<std::intptr_t>(-1)), nullptr, 4, 0, static_cast<unsigned long>(mappingSize),
            const_cast<wchar_t*>(mappingName)
        );
#else
        nullptr;
    int descriptor = ::shm_open(state.name.c_str(), O_CREAT | O_RDWR, 0600);
    if (descriptor >= 0) {
        // macOS rejects a second ftruncate on an allocated POSIX shm object,
        // even when its size is unchanged. Reuse a sufficiently large mapping
        // left by a previous process, as CreateFileMappingW does on Windows.
        struct stat mappingInfo{};
        bool sized = ::fstat(descriptor, &mappingInfo) == 0;
        if (sized && mappingInfo.st_size < static_cast<off_t>(mappingSize)) {
            sized = ::ftruncate(descriptor, static_cast<off_t>(mappingSize)) == 0;
            if (!sized) {
                const int resizeError = errno;
                // Another opener may have allocated it after our first fstat.
                sized = ::fstat(descriptor, &mappingInfo) == 0 && mappingInfo.st_size >= static_cast<off_t>(mappingSize);
                if (!sized) {
                    errno = resizeError;
                }
            }
        }
        if (!sized) {
            const int mappingError = errno;
            ::close(descriptor);
            descriptor = -1;
            errno = mappingError;
        }
    }
    if (descriptor >= 0) {
        m_linesHandle = reinterpret_cast<void*>(static_cast<std::intptr_t>(descriptor + 1));
    }
#endif
    if (m_linesHandle) {
        state.descriptor = static_cast<int>(reinterpret_cast<std::intptr_t>(m_linesHandle) - 1);
        state.base = ::mmap(nullptr, mappingSize, PROT_READ | PROT_WRITE, MAP_SHARED, state.descriptor, 0);
        if (state.base == MAP_FAILED) {
            state.base = nullptr;
        }
    }
#endif

#if !defined(_WIN32)
    if (!state.base) {
        const int mappingError = errno;
        ReportSharedLogMappingError(m_linesHandle != nullptr, mappingError);

        // Named POSIX shared memory is the transport equivalent of the
        // target's Global\\CONSOLE_LOG_* file mapping. Sandboxed/container
        // hosts can prohibit that namespace even though an anonymous shared
        // mapping is available. Retain the exact LogLine/tail byte layout
        // locally. Only external status-window attachment is unavailable for
        // this process run.
        if (state.descriptor >= 0) {
            ::close(state.descriptor);
            state.descriptor = -1;
            if (!state.name.empty()) {
                ::shm_unlink(state.name.c_str());
            }
        }
        state.name.clear();
        m_linesHandle = nullptr;
        state.base = ::mmap(nullptr, mappingSize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
        if (state.base == MAP_FAILED) {
            const int fallbackError = errno;
            state.base = nullptr;
            L2_LOG_DIRECT(
                stderr, runtime::logging::Level::Error,
                "[logging] Anonymous shared-memory fallback failed (errno %d: %s); "
                "the shared console log buffer is unavailable.",
                fallbackError, std::strerror(fallbackError)
            );
        }
    }
#endif

    if (state.base) {
        std::memset(state.base, 0, lineBytes);
        m_lines = static_cast<LogLine*>(state.base);
        m_tail = reinterpret_cast<unsigned*>(static_cast<std::byte*>(state.base) + lineBytes);
        *m_tail = 0;
    } else {
#if defined(_WIN32)
        ReportSharedLogMappingError(m_linesHandle != nullptr, static_cast<int>(::GetLastError()));
#else
        // The named-mapping error was reported before the anonymous fallback.
#endif
    }
    RegisterSharedMapping(*this, std::move(state));
}

CSharedLogLines::~CSharedLogLines() {
    // Current scalar deleting destructor 0x004ecf0c unmaps the view and
    // closes its mapping handle before the CLogLines base cleanup. POSIX
    // performs the equivalent shm/mmap teardown below.
    SharedMappingState state = RemoveSharedMapping(*this);
    if (state.base) {
#if defined(_WIN32)
        ::UnmapViewOfFile(state.base);
#else
        ::munmap(state.base, state.size);
#endif
    }
#if defined(_WIN32)
    if (m_linesHandle) {
        ::CloseHandle(static_cast<HANDLE>(m_linesHandle));
    }
#else
    if (state.descriptor >= 0) {
        ::close(state.descriptor);
    }
    if (!state.name.empty()) {
        ::shm_unlink(state.name.c_str());
    }
#endif
    m_linesHandle = nullptr;
    m_tailHandle = nullptr;
}

CLog Log(new CSharedLogLines(128, kConsoleMappingName));

CLog::CLog(CLogLines* logLines) : m_logLines(logLines), m_nWorldId(0), m_StopWhenError(false), m_InTesting(false) {
    L2_GUARD_FUNCTION();

    // Current L2NPC.exe 0x0045f378 performs these source-body stores after
    // entering the guard. Fields not written by that constructor deliberately
    // keep their static-storage zeroing (for the process global) or their
    // ordinary uninitialized state (for a non-static instance).
    font = nullptr;
    enabled = false;
    sendable = false;
    supressed = false;

    // The target initializes one main and ten per-file CRITICAL_SECTIONs and
    // creates its suspended connector thread here. The portable sidecars own
    // those OS resources. Target-visible fields and ordering remain below.
    State();
    const runtime::log::LogClientThreadIdentity connectThread = runtime::log::StartLogClientConnectionWorker(&logcli);
    m_nConnectThread = connectThread.id;
    m_hConnectThread = connectThread.handle;
    CThreadManager::SetThread(kLogThreadId, m_hConnectThread);
    m_hLogFile.fill(kInvalidFileHandle);
    m_bDirty = true;
    m_nMonth = 0;
    m_nDay = 0;
    m_nHour = 0;
    m_nHalf = 0;
    m_nWorld = 0;
    m_nType = -1;
    for (auto& fileName : m_szFileName) {
        fileName.fill(u'\0');
    }
    m_nConnectTick = runtime::GetTickCount();
#if defined(L2_BUILD_CACHED) || defined(L2_BUILD_L2SERVER)
    // Current Cached.exe 0x004eefb0 reads DAT_007e1b84, whose .data default
    // is 30 before InitConfig later refreshes the process global. Current
    // L2Server.exe 0x0081064c likewise stores the immediate value 30 before
    // CConfig::Load calls CLog::Init.
    m_nFileDuration = 30;
#else
    m_nFileDuration = ::g_nFileDuration;
#endif
}

CLog::~CLog() {
    L2_GUARD_FUNCTION();

    CLogPortableState& state = State();
    for (std::size_t index = 0; index < state.files.size(); ++index) {
        // Original implementation kept. Shared operator logging now includes L2Server.
        //         std::lock_guard<std::mutex> guard(
        //             state.fileSections[index])
        //         if (state.files[index].is_open()) {
        //             state.files[index].flush()
        //             state.files[index].close()
        //         }
        FlushLocalLogFile(state, index, true);
        m_hLogFile[index] = kInvalidFileHandle;
    }
}

void CLog::Init(int fileDuration, int worldId) {
    m_nFileDuration = fileDuration;
    m_nWorldId = worldId;
}

void CLog::Set(HWND__* window) {
    L2_GUARD_FUNCTION();
    wnd = window;
}

void CLog::Enable(bool value) {
    L2_GUARD_FUNCTION();
    enabled = value;
#if defined(_WIN32)
    if (value) {
        ::InvalidateRect(reinterpret_cast<HWND>(wnd), nullptr, FALSE);
    }
#endif
}

void CLog::Sendable(bool value) {
    // Current L2NPC.exe 0x0045d6fc.
    L2_GUARD_FUNCTION();
    sendable = value;
}

void CLog::Supress(bool value) {
    L2_GUARD_FUNCTION();
    supressed = value;
}

bool CLog::IsSupressed() const { return supressed; }

bool CLog::IsSendable() const { return sendable; }

void CLog::Resize(int width, int height) {
    L2_GUARD_FUNCTION();

    clientRect.right = width;
    clientRect.bottom = height;
#if defined(_WIN32)
    if (enabled) {
        ::InvalidateRect(reinterpret_cast<HWND>(wnd), nullptr, FALSE);
    }
#endif
}

void CLog::Enter() { State().section.lock(); }

void CLog::Leave() { State().section.unlock(); }

// Original implementation kept. Shared operator logging now includes L2Server.
// bool CLog::ValidLogFile(
//     int,
//     int,
//     int day,
//     int hour,
//     int half) const {
//     return !m_bDirty &&
//         half == m_nHalf &&
//         hour == m_nHour &&
//         day == m_nDay.
// }
bool CLog::ValidLogFile(int year, int month, int day, int hour, int half) const {
    std::lock_guard<std::recursive_mutex> guard(State().section);
    return !m_bDirty && year == m_nYear && month == m_nMonth && day == m_nDay && hour == m_nHour && half == m_nHalf;
}

void CLog::SetLogFileInfo(int world, int type) {
    m_nWorld = world;
    m_nType = type;
}

void CLog::SetLogFileDate(int year, int month, int day, int hour, int half) {
    m_nYear = year;
    m_nMonth = month;
    m_nDay = day;
    m_nHour = hour;
    m_nHalf = half;
}

void CLog::LogFileLock(int index) { State().fileSections[static_cast<std::size_t>(index)].lock(); }

void CLog::LogFileUnLock(int index) { State().fileSections[static_cast<std::size_t>(index)].unlock(); }

int CLog::GetConnectTick() const { return static_cast<int>(m_nConnectTick); }

void CLog::SetConnectTick() { m_nConnectTick = runtime::GetTickCount(); }

void runtime::log::SetDebugLoggingEnabled(bool enabled) noexcept {
    portableDebugLoggingEnabled.store(enabled, std::memory_order_relaxed);
}

void runtime::log::ConfigureLog(CLog& log, LogOptions options) {
    if (options.localRoot.empty()) {
        options.localRoot = "log";
    }
    if (options.fileDurationMinutes < 1 || 60 % options.fileDurationMinutes != 0) {
        options.fileDurationMinutes = 30;
    }
    const std::string host = options.logDAddress;
    const int port = options.logDPort;

    CLogPortableState& state = State();
    {
        std::lock_guard<std::recursive_mutex> guard(state.section);
        state.options = std::move(options);
        // NPCServer::Load in the current image (0x004ff00c) writes only
        // CLog::m_bDirty at DAT_019f5c70. World id is copied by AddV for
        // each message, while file duration and connect tick keep the
        // values established by CLog's constructor.
        log.m_bDirty = true;
    }
    runtime::log::ConfigureLogClient(logcli, host, port);
    state.connectionsEnabled.store(true);
}

void runtime::log::WriteLogFormatterLine(CLog& log, LogType type, const std::u16string& line) {
    // Current Cached.exe CLogFormatter::WriteLog/WriteAuditLog at
    // 0x004efa60/0x004efc5c enter the wide CLog::Add wrapper at 0x004ef90c.
    // The variadic formatting has already produced `line`. Continue at the
    // same typed UTF-16 stage and keep the wide overload's no-space comma.
    log.AddTypedUtf16(static_cast<int>(type), line, false);
}

void runtime::log::WriteConsoleErrorV(const wchar_t* format, std::va_list arguments) {
#if defined(_WIN32)
    struct RestoreWindowsError {
        DWORD value = ::GetLastError();
        ~RestoreWindowsError() { ::SetLastError(value); }
    } restoreWindowsError;
#endif
    const char* sourceFile = runtime::logging::SourceFileScope::Consume();
    struct RestoreErrno {
        int value = errno;
        ~RestoreErrno() { errno = value; }
    } restoreErrno;
    const std::u16string message = FormatOperatorMessage(format, arguments);
    // runtime::logging::Write(nullptr, runtime::logging::Level::Error,
    //     Utf16ToUtf8(message))
    runtime::logging::Write(
        nullptr, runtime::logging::Level::Error, Utf16ToUtf8(message), *sourceFile ? sourceFile : __FILE__
    );
}

void runtime::log::WriteErrorWithLocalCopy(CLog& log, const char* format, ...) {
    // Explicit diagnostic extension: format once and keep ERROR severity
    // independent of the recovered LOG_LOCAL file destination.
    const char* sourceFile = runtime::logging::SourceFileScope::Consume();
    LogAddVHelper logging;
    const int threadId = CThreadManager::GetThreadID();
    if (g_bOnLogging[threadId] >= 2 || !log.m_logLines) {
        return;
    }

    va_list arguments;
    va_start(arguments, format);
    std::u16string message;
    try {
        message = FormatOperatorMessage(format, arguments);
    } catch (...) {
        va_end(arguments);
        throw;
    }
    va_end(arguments);
    // log.AddTypedUtf16(LOG_ERROR, message, true, true)
    log.AddTypedUtf16(LOG_ERROR, message, true, true, sourceFile);
}

void CLog::ConnectToLogD() {
    // Current Cached.exe 0x004ed3bc. The target stores GetTickCount in
    // m_nConnectTick and resumes its suspended connection thread. The
    // portable condition-variable wake is the platform replacement for that
    // ResumeThread transport and still requests exactly one CLogCli::Init.
    L2_GUARD_FUNCTION();

    CLogPortableState& state = State();
    m_nConnectTick = runtime::GetTickCount();
    if (state.connectionsEnabled.load()) {
        runtime::log::WakeLogClientConnectionWorker(logcli);
    }
}

void CLog::SuspendConnect() { State().connectionsEnabled.store(false); }

void CLog::ResumeConnect() {
    CLogPortableState& state = State();
    state.connectionsEnabled.store(true);
    runtime::log::WakeLogClientConnectionWorker(logcli);
}

void runtime::log::StartLogReceiving(CLog& log) { StartLogClientReceiving(logcli); }

void runtime::log::StopLogTransport(CLog& log) {
    CLogPortableState& state = State();
    state.connectionsEnabled.store(false);
    log.Sendable(false);
    StopLogClient(logcli);
}

void ConnectThread_common() {
    // Current L2NPC.exe 0x0045dc04 and Cached.exe 0x004ed840. SuspendThread
    // is represented by the condition-variable wait. Each wake still
    // performs exactly one CLogCli::Init.
    for (;;) {
        if (!runtime::log::WaitForLogClientConnectionRequest(logcli)) {
            return;
        }

#if defined(L2_BUILD_L2NPC)
        GuardProfileHelper::InitializeGuardProfilePerThread();
        const int threadId = CThreadManager::GetThreadID();
        g_dwGuardStartTick[threadId] = runtime::GetTickCount();
#endif
        logcli.Init();
    }
}

void ConnectThread_mailing() {
    // Current L2NPC.exe 0x0045dc64 is the mailing/SEH wrapper around the
    // non-returning common loop. A normal return is possible only for the
    // portable shutdown path.
    try {
        ConnectThread_common();
    } catch (...) {
        runtime::process::HandleUnhandledSharedThreadException(L2_SOURCE_FILE, __LINE__, __TIMESTAMP__);
    }
}

unsigned ConnectThread(void* argument) {
    // Current L2NPC.exe 0x0045f2f4 and Cached.exe 0x004eef2c. CacheD's body
    // registers logical thread 0x12, constructs the same 0x80-entry default
    // IO-buffer pool and then enters its constant-true mailing wrapper.
    CThreadManager::RegisterThisThread(kLogThreadId);
    InitializeCurrentIOBufferPool(0x80);

    struct ClearLogThreadHandle {
        ~ClearLogThreadHandle() { CThreadManager::SetThread(kLogThreadId, nullptr); }
    } clearLogThreadHandle;

#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004dacec is the constant true condition.
    ConnectThread_mailing();
#else
    if (ExceptionMailing()) {
        ConnectThread_mailing();
    } else {
        ConnectThread_common();
    }
#endif
    return 0;
}

void CLog::CloseLogFile() {
    // Current L2NPC.exe 0x0045d530 has no body beyond GuardHelper bookkeeping.
    L2_GUARD_FUNCTION();
}

// Original implementation kept. Shared operator logging now includes L2Server.
// void CLog::FlushLogFile() {
//     // Current L2NPC.exe 0x0045d840 writes this exact marker to the error
//     // channel, then FlushFileBuffers on all ten handles.
//     L2_GUARD_FUNCTION()
//
//     static constexpr char kFlushMarker[] =
//         "\nException occured. Flush Flush!!!\n".
//     CLogPortableState& state = State()
//     {
//         constexpr std::size_t kErrorIndex = 5
//         std::lock_guard<std::mutex> guard(
//             state.fileSections[kErrorIndex])
//         if (state.files[kErrorIndex].is_open()) {
//             state.files[kErrorIndex].write(
//                 kFlushMarker,
//                 static_cast<std::streamsize>(
//                     sizeof(kFlushMarker) - 1))
//         }
//     }
//     for (std::size_t index = 0
//          index < state.files.size()
//          ++index) {
//         std::lock_guard<std::mutex> guard(
//             state.fileSections[index])
//         if (state.files[index].is_open()) {
//             state.files[index].flush()
//         }
//     }
// }
void CLog::FlushLogFile() {
#if defined(_WIN32)
    struct RestoreWindowsError {
        DWORD value = ::GetLastError();
        ~RestoreWindowsError() { ::SetLastError(value); }
    } restoreWindowsError;
#endif
    // Emergency diagnostics must not replace the caller's error with file I/O errno.
    struct RestoreErrno {
        int value = errno;
        ~RestoreErrno() { errno = value; }
    } restoreErrno;
    L2_GUARD_FUNCTION();
    CLogPortableState& state = State();
    // const auto record = runtime::logging::Format(OperatorApplication(),
    //     runtime::logging::Level::Error,
    //     "component=logging event=emergency_flush_requested")
    // const auto record = runtime::logging::Format(OperatorApplication(),
    //     runtime::logging::Level::Error,
    //     "[logging] Emergency log flush requested.")
    const auto record = runtime::logging::Format(
        OperatorApplication(), runtime::logging::Level::Error, "[logging] Emergency log flush requested.", __FILE__
    );
    // runtime::logging::WriteRecord(stdout, record)
    runtime::logging::WriteConsoleRecord(record);
    WriteLocalLogFile(state, 5, std::string_view(record.bytes.data(), record.length));
    for (std::size_t index = 0; index < state.files.size(); ++index) {
        FlushLocalLogFile(state, index, false);
    }
}

// Original implementation kept. Shared operator logging now includes L2Server.
// void CLog::CreateNewLogFile(
//     int year,
//     int month,
//     int day,
//     int hour,
//     int half) {
//     L2_GUARD_FUNCTION()
//
//     CLogPortableState& state = State()
//     if (m_nType < 0 ||
//         ValidLogFile(
//             year,
//             month,
//             day,
//             hour,
//             half)) {
//         return.
//     }
//
//     m_bDirty = false
//     SetLogFileDate(
//         year,
//         month,
//         day,
//         hour,
//         half)
//
//     for (std::size_t index = 0
//          index < kLogFileCount.
//          ++index) {
//         std::lock_guard<std::mutex> fileGuard(
//             state.fileSections[index])
//         if (state.files[index].is_open()) {
//             state.files[index].flush()
//             state.files[index].close()
//             std::error_code sizeError
//             const std::uintmax_t size =
//                 std::filesystem::file_size(
//                     state.paths[index],
//                     sizeError)
//             if (!sizeError && size == 0) {
//                 std::error_code removeError
//                 std::filesystem::remove(
//                     state.paths[index],
//                     removeError)
//             }
//         }
//
//         const std::string category =
//             NarrowAsciiLogName(
//                 LogCategory[index])
//         const std::filesystem::path directory =
//             std::filesystem::path(
//                 state.options.localRoot) /
//             category.
//         std::error_code directoryError
//         std::filesystem::create_directories(
//             directory,
//             directoryError)
//
//         std::array<char, 256> fileName{}
//         std::snprintf(
//             fileName.data(),
//             fileName.size(),
//             "%04d-%02d-%02d-100%02d-%02d-%s-%s%d.log",
//             year,
//             month,
//             day,
//             hour,
//             m_nWorld,
//             NarrowAsciiLogName(
//                 LogServerType[
//                     static_cast<std::size_t>(
//                         m_nType)])
//                 .c_str(),
//             category.c_str(),
//             half)
//         state.paths[index] =
//             directory / fileName.data()
//         const std::string targetPath =
//             "log\\" + category + "\\" +
//             fileName.data()
//         CopyTargetPathToBuffer(
//             targetPath,
//             m_szFileName[index])
//
//         state.files[index].clear()
//         state.files[index].open(
//             state.paths[index],
//             std::ios::binary |
//                 std::ios::out |
//                 std::ios::app)
//         m_hLogFile[index] =
//             state.files[index].is_open()
//                 ? reinterpret_cast<
//                     std::uintptr_t>(
//                     &state.files[index])
//                 : kInvalidFileHandle.
//     }
// }
void CLog::CreateNewLogFile(int year, int month, int day, int hour, int half) {
    L2_GUARD_FUNCTION();
    CLogPortableState& state = State();
    std::lock_guard<std::recursive_mutex> guard(state.section);
    if (m_nType < 0 || ValidLogFile(year, month, day, hour, half)) {
        return;
    }

    bool ready = true;
    for (std::size_t index = 0; index < kLogFileCount; ++index) {
        const std::string category = NarrowAsciiLogName(LogCategory[index]);
        const auto directory = std::filesystem::path(runtime::Utf8CodeUnits(state.options.localRoot)) / category;
        std::array<char, 256> fileName{};
        std::snprintf(
            fileName.data(), fileName.size(), "%04d-%02d-%02d-100%02d-%02d-%s-%s%d.log", year, month, day, hour, m_nWorld,
            NarrowAsciiLogName(LogServerType[static_cast<std::size_t>(m_nType)]).c_str(), category.c_str(), half
        );
        if (OpenLocalLogFile(state, index, directory / fileName.data())) {
            const std::string targetPath = "log\\" + category + "\\" + fileName.data();
            CopyTargetPathToBuffer(targetPath, m_szFileName[index]);
            m_hLogFile[index] = reinterpret_cast<std::uintptr_t>(&state.files[index]);
        } else {
            // Keep the previous stream/handle on failure. A missing initial
            // destination remains invalid and is retried on later logs.
            ready = false;
        }
    }
    if (ready) {
        SetLogFileDate(year, month, day, hour, half);
    }
    m_bDirty = !ready;
}

void CLog::Add(LogType type, char* format, ...) {
    // Current Cached.exe 0x004eeddc is this guard plus one forwarding call to
    // the narrow AddV at 0x004ed8f0.
    L2_GUARD_FUNCTION();

    va_list arguments;
    va_start(arguments, format);
    // AddV(type, format, arguments)
    try {
        AddV(type, format, arguments);
    } catch (...) {
        va_end(arguments);
        throw;
    }
    va_end(arguments);
}

void CLog::Add(LogType type, wchar_t* format, ...) {
    // Current Cached.exe 0x004eee84 is the matching wide forwarding
    // wrapper and enters AddV at 0x004ee2a0.
    L2_GUARD_FUNCTION();

    va_list arguments;
    va_start(arguments, format);
    // AddV(type, format, arguments)
    try {
        AddV(type, format, arguments);
    } catch (...) {
        va_end(arguments);
        throw;
    }
    va_end(arguments);
}

void CLog::Add(LogType type, const char* format, ...) {
    L2_GUARD_FUNCTION();

    va_list arguments;
    va_start(arguments, format);
    // AddV(
    //     type,
    //     const_cast<char*>(format),
    //     arguments)
    try {
        AddV(type, const_cast<char*>(format), arguments);
    } catch (...) {
        va_end(arguments);
        throw;
    }
    va_end(arguments);
}

void CLog::Add(LogType type, const wchar_t* format, ...) {
    L2_GUARD_FUNCTION();

    va_list arguments;
    va_start(arguments, format);
    // AddV(
    //     type,
    //     const_cast<wchar_t*>(format),
    //     arguments)
    try {
        AddV(type, const_cast<wchar_t*>(format), arguments);
    } catch (...) {
        va_end(arguments);
        throw;
    }
    va_end(arguments);
}

void CLog::AddV(LogType type, char* format, va_list arguments) {
#if defined(_WIN32)
    struct RestoreWindowsError {
        DWORD value = ::GetLastError();
        ~RestoreWindowsError() { ::SetLastError(value); }
    } restoreWindowsError;
#endif
    // Preserve caller errno across formatting and every destination, including exceptions.
    struct RestoreErrno {
        int value = errno;
        ~RestoreErrno() { errno = value; }
    } restoreErrno;
    const char* sourceFile = runtime::logging::SourceFileScope::Consume();
    // Current L2NPC.exe 0x0045dca8 / Cached.exe 0x004ed8f0.
    L2_GUARD_FUNCTION();

    const int logType = static_cast<int>(type);
    if (logType >= LOG_MAX) {
        return;
    }
    if (type == LOG_OPT_ERROR) {
        if (!g_OptionalErrorLogEnabled) {
            return;
        }
        type = LOG_ERROR;
    }

    LogAddVHelper __t;
    const int threadId = CThreadManager::GetThreadID();
    // if (g_bOnLogging[threadId] >= 2 ||
    //     !m_logLines ||
    //     type == LOG_DEBUG) {
    //     return.
    // }
    if (g_bOnLogging[threadId] >= 2 || !m_logLines || SuppressDebugLog(type)) {
        return;
    }

    CLogPortableState& state = State();
    {
        std::lock_guard<std::recursive_mutex> guard(state.section);
#if !defined(L2_BUILD_L2SERVER)
        m_nWorldId = state.options.worldId;
#endif
    }

    if (IsOperatorLog(type)) {
        // Noise: A long human message is not a server fault. Preserve a
        // bounded message/marker instead of the legacy overflow stack dump.
        // AddTypedUtf16(type, FormatOperatorMessage(format, arguments), true)
        AddTypedUtf16(type, FormatOperatorMessage(format, arguments), true, false, sourceFile);
        return;
    }

    std::array<char, 1000> buffer{};
    va_list copy;
    va_copy(copy, arguments);
    const int length = std::vsnprintf(buffer.data(), 999, format, copy);
    va_end(copy);
    if (length < 1 || length >= 999) {
        int id = 0;
        // Diagnostic ID: the first argument may be a string pointer (for
        // example L2NPC's preformatted %s records), or there may be no args.
        // Read the ID from the bounded formatted prefix in WriteOverflow.
        // if (type == LOG_BOTH ||
        //     type == LOG_AUDIT) {
        //     va_list idArguments.
        //     va_copy(idArguments, arguments)
        //     id = va_arg(idArguments, int)
        //     va_end(idArguments)
        // }

        std::size_t formatLength = 0;
        if (type == LOG_ERROR || type == LOG_NORMAL || type == LOG_CHAT) {
            const std::size_t limit = type == LOG_CHAT ? 199 : 999;
            while (formatLength < limit && format[formatLength] != '\0') {
                ++formatLength;
            }
        }
        std::size_t bufferLength = 0;
        while (bufferLength < 999 && buffer[bufferLength] != '\0') {
            ++bufferLength;
        }
        // Original narrow conversion kept. Diagnostics now decode UTF-8.
        // WriteOverflow(
        //     type,
        //     length >= 999 ? -1 : length,
        //     NarrowLogBytesToTargetWide(
        //         std::string_view(
        //             buffer.data(),
        //             bufferLength)),
        //     NarrowLogBytesToTargetWide(
        //         std::string_view(
        //             format,
        //             formatLength)),
        //     id,
        //     false)
        WriteOverflow(
            type, length >= 999 ? -1 : length, NarrowOperatorLogToUtf16(type, std::string_view(buffer.data(), bufferLength)),
            NarrowOperatorLogToUtf16(type, std::string_view(format, formatLength)), id, false
        );
        return;
    }

    // Original narrow conversion kept. Diagnostics now decode UTF-8.
    // AddTypedUtf16(
    //     static_cast<int>(type),
    //     NarrowLogBytesToTargetWide(
    //         std::string_view(
    //             buffer.data(),
    //             static_cast<std::size_t>(
    //                 length))),
    //     true)
    AddTypedUtf16(
        static_cast<int>(type),
        NarrowOperatorLogToUtf16(type, std::string_view(buffer.data(), static_cast<std::size_t>(length))), true
    );
}

void CLog::AddV(LogType type, wchar_t* format, va_list arguments) {
#if defined(_WIN32)
    struct RestoreWindowsError {
        DWORD value = ::GetLastError();
        ~RestoreWindowsError() { ::SetLastError(value); }
    } restoreWindowsError;
#endif
    // Preserve caller errno across formatting and every destination, including exceptions.
    struct RestoreErrno {
        int value = errno;
        ~RestoreErrno() { errno = value; }
    } restoreErrno;
    const char* sourceFile = runtime::logging::SourceFileScope::Consume();
    // Current L2NPC.exe 0x0045e688 / Cached.exe 0x004ee2a0. Unlike the narrow
    // overload, the shipping function does not contain an upper LogType
    // range check.
    L2_GUARD_FUNCTION();

    if (type == LOG_OPT_ERROR) {
        if (!g_OptionalErrorLogEnabled) {
            return;
        }
        type = LOG_ERROR;
    }

    LogAddVHelper __t;
    const int threadId = CThreadManager::GetThreadID();
    // if (g_bOnLogging[threadId] >= 2 ||
    //     !m_logLines ||
    //     type == LOG_DEBUG) {
    //     return.
    // }
    if (g_bOnLogging[threadId] >= 2 || !m_logLines || SuppressDebugLog(type)) {
        return;
    }

    CLogPortableState& state = State();
    {
        std::lock_guard<std::recursive_mutex> guard(state.section);
#if !defined(L2_BUILD_L2SERVER)
        m_nWorldId = state.options.worldId;
#endif
    }

    if (IsOperatorLog(type)) {
        // Noise: Expected truncation must not replace the useful text with
        // the legacy overflow stack dump. Machine records keep that path.
        // AddTypedUtf16(type, FormatOperatorMessage(format, arguments), false)
        AddTypedUtf16(type, FormatOperatorMessage(format, arguments), false, false, sourceFile);
        return;
    }

    std::array<wchar_t, 1000> buffer{};
    int length = 0;
    {
        GuardHelper formatGuard(L"Log::AddV - vswprintf_s", CThreadManager::GetThreadID());
        // Use the existing thread-local UTF-8 adapter: on POSIX the CRT can
        // otherwise reject valid Unicode accounting text under the C locale.
        // Retain the original capacity and failure/rejection policy.
        // va_list copy.
        // va_copy(copy, arguments)
        // #if defined(_WIN32)
        // const wchar_t* const hostFormat = format
        // #else
        // const std::wstring translated =
        //     WindowsWideFormatToHost(format)
        // const wchar_t* const hostFormat =
        //     translated.c_str()
        // #endif
        // length = std::vswprintf(
        //     buffer.data(),
        //     999,
        //     hostFormat,
        //     copy)
        // va_end(copy)
        length = WindowsVswprintf(buffer.data(), 999, format, arguments);
    }
    if (length < 1 || length >= 999) {
        int id = 0;
        // Diagnostic ID: the first argument may be a string pointer (for
        // example L2NPC's preformatted %s records), or there may be no args.
        // Read the ID from the bounded formatted prefix in WriteOverflow.
        // if (type == LOG_BOTH ||
        //     type == LOG_AUDIT) {
        //     va_list idArguments.
        //     va_copy(idArguments, arguments)
        //     id = va_arg(idArguments, int)
        //     va_end(idArguments)
        // }

        const std::u16string formatText = TargetWideOverflowFormat(format, type);
        std::size_t bufferLength = 0;
        while (bufferLength < 999 && buffer[bufferLength] != L'\0') {
            ++bufferLength;
        }
        WriteOverflow(
            type, length >= 999 ? -1 : length, LogWideToUtf16(std::wstring_view(buffer.data(), bufferLength)), formatText,
            id, true
        );
        return;
    }

    AddTypedUtf16(
        static_cast<int>(type), LogWideToUtf16(std::wstring_view(buffer.data(), static_cast<std::size_t>(length))), false
    );
}

void CLog::AddUtf16(LogType type, const std::u16string_view format, const std::u16string& text) {
#if defined(_WIN32)
    struct RestoreWindowsError {
        DWORD value = ::GetLastError();
        ~RestoreWindowsError() { ::SetLastError(value); }
    } restoreWindowsError;
#endif
    // Preserve caller errno across every destination, including exceptions.
    struct RestoreErrno {
        int value = errno;
        ~RestoreErrno() { errno = value; }
    } restoreErrno;
    const char* sourceFile = runtime::logging::SourceFileScope::Consume();
    // The same checks and routes as the wide AddV after its vswprintf step.
    L2_GUARD_FUNCTION();

    if (type == LOG_OPT_ERROR) {
        if (!g_OptionalErrorLogEnabled) {
            return;
        }
        type = LOG_ERROR;
    }

    LogAddVHelper __t;
    const int threadId = CThreadManager::GetThreadID();
    if (g_bOnLogging[threadId] >= 2 || !m_logLines || SuppressDebugLog(type)) {
        return;
    }

    CLogPortableState& state = State();
    {
        std::lock_guard<std::recursive_mutex> guard(state.section);
#if !defined(L2_BUILD_L2SERVER)
        m_nWorldId = state.options.worldId;
#endif
    }

    if (IsOperatorLog(type)) {
        AddTypedUtf16(type, text, false, false, sourceFile);
        return;
    }

    // The wide AddV formats into 999 units and treats an empty result or a
    // full buffer as an overflow.
    if (text.empty() || text.size() >= 999) {
        const std::u16string formatText = TargetUtf16OverflowFormat(std::u16string(format), type);
        WriteOverflow(
            type, text.size() >= 999 ? -1 : static_cast<int>(text.size()), text.substr(0, 998), formatText, 0, true
        );
        return;
    }

    AddTypedUtf16(static_cast<int>(type), text, false);
}

// Original overflow diagnostic kept. Readable errors now use the common operator sinks.
// void CLog::WriteOverflow(
//     LogType type,
//     int formatResult,
//     const std::u16string& buffer,
//     const std::u16string& format,
//     int id,
//     bool wideInput) {
//     // Both target AddV overloads contain this block inline. It is shared here
//     // only to keep the portable source readable. Routing, ordering and text
//     // remain those of current L2NPC.exe 0x0045dca8/0x0045e688 and current
//     // Cached.exe 0x004ed8f0/0x004ee2a0.
//     const runtime::SystemTime time =
//         runtime::crt::
//             LocalSystemTimeNow<runtime::SystemTime>()
//     const bool local =
//         type == LOG_LOCAL || !IsSendable()
//     CLogPortableState& state = State()
//
//     if (local) {
//         const int half =
//             static_cast<int>(time.wMinute) /
//             m_nFileDuration.
//         if (!ValidLogFile(
//                 time.wYear,
//                 time.wMonth,
//                 time.wDay,
//                 time.wHour,
//                 half)) {
//             std::lock_guard<std::recursive_mutex> guard(
//                 state.section)
//             m_nWorld = m_nWorldId
//             m_nType = kProcessLogServerType
//             CreateNewLogFile(
//                 time.wYear,
//                 time.wMonth,
//                 time.wDay,
//                 time.wHour,
//                 half)
//         }
//     }
//
//     std::ostringstream body
//     if (!local && type == LOG_CHAT) {
//         body
//             << "log overflow. len["
//             << formatResult
//             << "] "
//             << (wideInput ? "Buffer[" : "buffer[")
//             << Utf16ForLocalTransport(buffer)
//             << "] type["
//             << static_cast<int>(type)
//             << "] id["
//             << id
//             << "] format["
//             << Utf16ForLocalTransport(format)
//             << ']'.
//     } else {
//         body
//             << "log overflow. type["
//             << static_cast<int>(type)
//             << "] id["
//             << id
//             << "] format["
//             << Utf16ForLocalTransport(format)
//             << ']'.
//     }
//
//     char prefix[64]{}
//     std::snprintf(
//         prefix,
//         sizeof(prefix),
//         "%02u/%02u/%04u %02u:%02u:%02u.%03u, ",
//         time.wMonth,
//         time.wDay,
//         time.wYear,
//         time.wHour,
//         time.wMinute,
//         time.wSecond,
//         time.wMilliseconds)
//     std::string text(prefix)
//     text.append(body.str())
//     text.append("\r\n")
//     if (text.size() > 2175) {
//         text.resize(2175)
//     }
//
//     runtime::process::ReportCallStack(
//         LOG_ERROR,
//         false)
//     if (local) {
//         WriteLocal(
//             LOG_ERROR,
//             text,
//             time)
//     } else {
//         std::u16string targetText =
//             Utf8ToUtf16(text)
//         logcli.LogSendMsg(
//             LOG_ERROR,
//             targetText.data())
//     }
// }
void CLog::WriteOverflow(
    LogType type, int formatResult, const std::u16string& buffer, const std::u16string& format, int id, bool wideInput
) {
    // Operator-facing error only: the rejected accounting record, formatter
    // thresholds, destination selection and stack-report policy are unchanged.
    const runtime::SystemTime time = runtime::crt::LocalSystemTimeNow<runtime::SystemTime>();
    // const bool local =
    //     type == LOG_LOCAL || !IsSendable()
    const bool local = runtime::log::LocalLogScope::IsActive() || type == LOG_LOCAL || !IsSendable();
    CLogPortableState& state = State();

    if (local) {
        const int half = static_cast<int>(time.wMinute) / m_nFileDuration;
        if (!ValidLogFile(time.wYear, time.wMonth, time.wDay, time.wHour, half)) {
            std::lock_guard<std::recursive_mutex> guard(state.section);
            m_nWorld = m_nWorldId;
            m_nType = kProcessLogServerType;
            CreateNewLogFile(time.wYear, time.wMonth, time.wDay, time.wHour, half);
        }
    }

    const char* channel = "unknown";
    switch (type) {
        case LOG_INNER:
            channel = "inner";
            break;
        case LOG_OUTER:
            channel = "outer";
            break;
        case LOG_BOTH:
            channel = "both";
            break;
        case LOG_DB:
            channel = "database";
            break;
        case LOG_AUDIT:
            channel = "audit";
            break;
        case LOG_CHAT:
            channel = "chat";
            break;
        case LOG_LONG_KEEP:
            channel = "long-keep";
            break;
        case LOG_NCGUARD:
            channel = "NCGuard";
            break;
        default:
            break;
    }
    std::ostringstream body;
    body << "[logging] Dropped log record on channel '" << channel << "' (" << static_cast<int>(type) << ')';
    // // Only these callers supply an actual ID. Zero in other channels is a
    // // placeholder and must not be presented as the identity of the record.
    // if (type == LOG_BOTH || type == LOG_AUDIT) body << ", record ID " << id.
    // Retain the legacy helper parameter, but never interpret an unknown
    // variadic argument as an integer. The formatted first CSV field is the
    // diagnostic authority for numeric IDs, including preformatted records.
    static_cast<void>(id);
    if ((type == LOG_BOTH || type == LOG_AUDIT) && IsAccountingConsoleCopy(type, buffer)) {
        body << ", record ID " << Utf16ToUtf8(buffer.substr(0, buffer.find(u',')));
    }
    if (formatResult == 0) {
        body << ": the formatter produced an empty record.";
    } else if (formatResult < 0) {
        // Narrow AddV maps a too-large result to -1. Wide CRT failure is also
        // negative. This boundary cannot identify either as overflow alone.
        body << ": formatting failed or exceeded the limit of 998 " << (wideInput ? "wide characters" : "bytes")
             << " (formatter result " << formatResult << ").";
    } else {
        body << ": the formatted result of " << formatResult << ' ' << (wideInput ? "wide characters" : "bytes")
             << " exceeds the limit of 998.";
    }
    // Keep provided text literal, including SQL/percent signs/whitespace. The
    // operator formatter handles terminal escaping and marked truncation.
    if (!format.empty()) {
        body << " Format: " << Utf16ToUtf8(format);
    }
    if (!local && type == LOG_CHAT && !buffer.empty()) {
        body << " Partial record: " << Utf16ToUtf8(buffer);
    }
    const std::u16string message = BoundOperatorMessage(body.str());
    // const auto record = runtime::logging::Format(
    //     OperatorApplication(), runtime::logging::Level::Error, Utf16ToUtf8(message))
    const auto record = runtime::logging::Format(
        OperatorApplication(), runtime::logging::Level::Error, Utf16ToUtf8(message), __FILE__
    );
    bool consoleOutput = false;
    {
        std::lock_guard<std::recursive_mutex> guard(state.section);
        consoleOutput = state.options.consoleOutput;
    }

    runtime::process::ReportCallStack(LOG_ERROR, false);
    if (consoleOutput) {
        const int savedErrno = errno;
        // runtime::logging::WriteRecord(stdout, record)
        runtime::logging::WriteConsoleRecord(record);
        errno = savedErrno;
    }
    if (local) {
        WriteLocal(LOG_ERROR, std::string(record.bytes.data(), record.length), time);
    } else {
        // LogD keeps its existing UTF-16 timestamp/frame convention, as on
        // the ordinary operator path. No accounting payload is sent here.
        auto timestamped = AddTimestampUtf16(message, true);
        logcli.LogSendMsg(LOG_ERROR, timestamped.text.data());
    }
}
void CLog::Redraw() {
    // Current L2NPC.exe 0x0045f020. Cached.exe 0x004eec44.
    // L2Server.exe 0x008102f4.
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    // The portable writer publishes rows/tail under this same lock. Keep a
    // local paint from reading a row while it is being rewritten. This is not
    // an interprocess synchronization protocol for external mapping readers.
    std::lock_guard<std::recursive_mutex> displayGuard(State().section);
    if (!enabled) {
        return;
    }

    ::GetClientRect(reinterpret_cast<HWND>(wnd), reinterpret_cast<LPRECT>(&clientRect));
    PAINTSTRUCT paint{};
    HDC deviceContext = ::BeginPaint(reinterpret_cast<HWND>(wnd), &paint);
    if (!font) {
        font = ::GetStockObject(SYSTEM_FIXED_FONT);
        ::SelectObject(deviceContext, static_cast<HGDIOBJ>(font));
        brush = ::GetStockObject(WHITE_BRUSH);
        // ::GetTextExtentPoint32W(
        //     deviceContext,
        //     L"H",
        //     1,
        //     reinterpret_cast<LPSIZE>(
        //         &fontSize))
        const BOOL measured = ::GetTextExtentPoint32W(deviceContext, L"H", 1, reinterpret_cast<LPSIZE>(&fontSize));
        // A failed/zero-height measurement must not enter the drawing loop:
        // subtracting zero from y would keep the GUI in this paint forever.
        // Retry font setup on the next paint request. The font is borrowed.
        if (!measured || fontSize.cy <= 0) {
            font = nullptr;
            ::EndPaint(reinterpret_cast<HWND>(wnd), &paint);
            return;
        }
    } else {
        ::SelectObject(deviceContext, static_cast<HGDIOBJ>(font));
    }

    int y = clientRect.bottom;
    unsigned tail = m_logLines->GetTail();
    do {
        y -= fontSize.cy;
        RECT lineRect{0, y, clientRect.right, y + fontSize.cy};
        ::FillRect(deviceContext, &lineRect, static_cast<HBRUSH>(brush));
        tail = static_cast<unsigned>(m_logLines->GetIndex(static_cast<int>(tail) - 1));
        m_logLines->TextOutW(reinterpret_cast<HDC__*>(deviceContext), y, static_cast<int>(tail));
    } while (y >= 0);

    ::EndPaint(reinterpret_cast<HWND>(wnd), &paint);
#endif
}

void CLog::AddTypedUtf16(int logType, const std::u16string& line, bool spaceAfterTimestampComma) {
    AddTypedUtf16(logType, line, spaceAfterTimestampComma, false);
}

// Original entry signature kept above. Its sink body below now also
// supports an explicit local ERROR copy without changing other channels.
// void CLog::AddTypedUtf16(
//     int logType,
//     const std::u16string& line,
//     bool spaceAfterTimestampComma) {
void CLog::AddTypedUtf16(int logType, const std::u16string& line, bool spaceAfterTimestampComma, bool copyErrorLocally) {
    AddTypedUtf16(logType, line, spaceAfterTimestampComma, copyErrorLocally, "");
}

// Previous sink signature kept by the forwarding overload above.
// void CLog::AddTypedUtf16(
//     int logType,
//     const std::u16string& line,
//     bool spaceAfterTimestampComma,
//     bool copyErrorLocally) {
void CLog::AddTypedUtf16(
    int logType, const std::u16string& line, bool spaceAfterTimestampComma, bool copyErrorLocally, const char* sourceFile
) {
#if defined(_WIN32)
    struct RestoreWindowsError {
        DWORD value = ::GetLastError();
        ~RestoreWindowsError() { ::SetLastError(value); }
    } restoreWindowsError;
#endif
    // Direct UTF-16 callers also need error preservation around file/LogD routing.
    struct RestoreErrno {
        int value = errno;
        ~RestoreErrno() { errno = value; }
    } restoreErrno;
    // if (!m_logLines ||
    //     logType == LOG_DEBUG) {
    //     return.
    // }
    if (!m_logLines || SuppressDebugLog(logType)) {
        return;
    }

    const bool operatorLog = IsOperatorLog(logType);
    // The direct UTF-16 formatter entry also reaches this boundary. Bound
    // human messages before the ring/LogD routes, preserving machine records.
    const std::u16string boundedLine = operatorLog ? BoundOperatorMessage(Utf16ToUtf8(line)) : std::u16string{};
    const std::u16string& routedLine = operatorLog ? boundedLine : line;
    const auto operatorRecord = runtime::logging::Format(
        // OperatorApplication(), OperatorLevel(logType), Utf16ToUtf8(routedLine))
        OperatorApplication(), OperatorLevel(logType), Utf16ToUtf8(routedLine), operatorLog ? sourceFile : ""
    );
    // Noise: Blank human messages have no content in any destination.
    if (operatorLog && operatorRecord.length == 0) {
        return;
    }

    CLogPortableState& state = State();
    bool consoleOutput = false;
    {
        std::lock_guard<std::recursive_mutex> guard(state.section);
#if !defined(L2_BUILD_L2SERVER)
        m_nWorldId = state.options.worldId;
#endif
        consoleOutput = state.options.consoleOutput;
        if (logType < LOG_DEBUG) {
            // m_logLines->WriteLogUtf16(
            //     static_cast<LogType>(logType),
            //     line.c_str(),
            //     static_cast<int>(line.size()))
            // The mapped GUI row keeps its recovered size. Do not clear
            // its text or publish a length larger than the stored preview.
            // const std::u16string preview = operatorLog ? OperatorRingPreview(routedLine) : routedLine
            // Match the console/file presentation in the GUI too. Exclude the
            // final record LF before applying the mapped row's UTF-16 limit.
            const std::u16string preview = operatorLog
                ? OperatorRingPreview(Utf8ToUtf16(std::string_view(operatorRecord.bytes.data(), operatorRecord.length - 1)))
                : routedLine;
            m_logLines->WriteLogUtf16(static_cast<LogType>(logType), preview.c_str(), static_cast<int>(preview.size()));
        }
    }
    // TimestampedUtf16Line timestamped =
    //     AddTimestampUtf16(
    //         line,
    //         spaceAfterTimestampComma)
    TimestampedUtf16Line timestamped = AddTimestampUtf16(routedLine, spaceAfterTimestampComma);
    // const auto operatorRecord = runtime::logging::Format(
    //     OperatorApplication(), OperatorLevel(logType), Utf16ToUtf8(line))
    // Portable presentation extension: mirror the record only after the
    // recovered CLog path has selected its type, formatted its UTF-16 text
    // and added the target timestamp. It does not replace or short-circuit
    // the original LogD/local-file routing below.
    // Noise: Numeric accounting records already follow the LogD/category-file
    // route below. Their console copies obscure operator messages. Preserve
    // free-text messages in the same channels and every non-console route.
    // if (consoleOutput) {
    if (consoleOutput && !IsAccountingConsoleCopy(logType, routedLine)) {
        // Original implementation kept. Shared operator logging now includes L2Server.
        //         std::string consoleLine =
        //             Utf16ToUtf8(timestamped.text)
        // #if !defined(_WIN32)
        //         // Files/LogD keep target CRLF. Native terminals and IDE consoles
        //         // receive LF so their output translation cannot produce CR CR LF.
        //         for (std::size_t offset = 0
        //              (offset = consoleLine.find("\r\n", offset)) != std::string::npos) {
        //             consoleLine.erase(offset, 1)
        //         }
        // #endif
        //         std::lock_guard<std::mutex> consoleGuard(
        //             state.consoleSection)
        //         std::fwrite(
        //             consoleLine.data(),
        //             1,
        //             consoleLine.size(),
        //             stdout)
        //         std::fflush(stdout)
        const int savedErrno = errno;
        // runtime::logging::WriteRecord(stdout, operatorRecord)
        runtime::logging::WriteConsoleRecord(operatorRecord);
        errno = savedErrno;
    }
    // const bool local =
    //     logType == LOG_LOCAL ||
    //     logType == LOG_NCGUARD ||
    //     !IsSendable()
    // Debug records were previously suppressed, so no LogD contract exists
    // for them here. Keep enabled diagnostics in local sinks.
    if (copyErrorLocally && logType == LOG_ERROR) {
        // Preserve the local-first route and reconnect check of the former
        // LOG_LOCAL + LOG_ERROR pair. Reuse one ERROR record for console,
        // GUI, the local category and the normal error destination.
        const std::uint32_t now = runtime::GetTickCount();
        // if (now - m_nConnectTick > kReconnectMilliseconds) ConnectToLogD()
        if (!runtime::log::LocalLogScope::IsActive() && now - m_nConnectTick > kReconnectMilliseconds) {
            ConnectToLogD();
        }
        WriteLocal(LOG_LOCAL, std::string(operatorRecord.bytes.data(), operatorRecord.length), timestamped.time);
    }
    const bool local =
        // LogD's own diagnostics keep their severity in local files and
        // must not recursively report through the failed transport.
        runtime::log::LocalLogScope::IsActive() || logType == LOG_DEBUG || logType == LOG_LOCAL ||
        logType == LOG_NCGUARD || !IsSendable();
    if (!local) {
        logcli.LogSendMsg(static_cast<LogType>(logType), timestamped.text.data());
    } else {
        const std::uint32_t now = runtime::GetTickCount();
        // if (now - m_nConnectTick >
        //     kReconnectMilliseconds) {
        // if (logType != LOG_DEBUG && now - m_nConnectTick >
        //     kReconnectMilliseconds) {
        if (!runtime::log::LocalLogScope::IsActive() && logType != LOG_DEBUG &&
            now - m_nConnectTick > kReconnectMilliseconds) {
            ConnectToLogD();
        }

        if (IsOperatorLog(logType)) {
            WriteLocal(logType, std::string(operatorRecord.bytes.data(), operatorRecord.length), timestamped.time);
        } else {
            // The target converts this final wide line with CP_ACP before the
            // local write. The portable adapter uses the configured equivalent.
            WriteLocal(logType, Utf16ForLocalTransport(timestamped.text), timestamped.time);
        }
    }

#if defined(_WIN32)
    if (enabled && logType < LOG_DEBUG) {
        ::InvalidateRect(reinterpret_cast<HWND>(wnd), nullptr, FALSE);
    }
#endif
}

// Original implementation kept. Shared operator logging now includes L2Server.
// void CLog::WriteLocal(
//     int logType,
//     const std::string& timestampedLine,
//     const runtime::SystemTime& time) {
//     CLogPortableState& state = State()
//     const int half =
//         static_cast<int>(time.wMinute) /
//         m_nFileDuration.
//     if (!ValidLogFile(
//             time.wYear,
//             time.wMonth,
//             time.wDay,
//             time.wHour,
//             half)) {
//         std::lock_guard<std::recursive_mutex> guard(
//             state.section)
//         m_nWorld = m_nWorldId
//         m_nType = kProcessLogServerType
//         CreateNewLogFile(
//             time.wYear,
//             time.wMonth,
//             time.wDay,
//             time.wHour,
//             half)
//     }
//
//     const std::size_t fileIndex =
//         static_cast<std::size_t>(
//             FileIndexForLogType(logType))
//     std::lock_guard<std::mutex> fileGuard(
//         state.fileSections[fileIndex])
//     std::ofstream& output =
//         state.files[fileIndex].
//     if (!output.is_open()) {
//         return.
//     }
//     output.write(
//         timestampedLine.data(),
//         static_cast<std::streamsize>(
//             timestampedLine.size()))
//     // std::ostream buffering is above the target's WriteFile layer. This
//     // flush only reaches the OS. FlushLogFile remains the durable flush.
//     output.flush()
// }
void CLog::WriteLocal(int logType, const std::string& timestampedLine, const runtime::SystemTime& time) {
    if (timestampedLine.empty()) {
        return;
    }
    CLogPortableState& state = State();
    std::lock_guard<std::recursive_mutex> guard(state.section);
    const int half = static_cast<int>(time.wMinute) / m_nFileDuration;
    m_nWorld = m_nWorldId;
    m_nType = kProcessLogServerType;
    CreateNewLogFile(time.wYear, time.wMonth, time.wDay, time.wHour, half);
    const auto index = static_cast<std::size_t>(FileIndexForLogType(logType));
    if (!WriteLocalLogFile(state, index, timestampedLine)) {
        m_bDirty = true;
    }
}
