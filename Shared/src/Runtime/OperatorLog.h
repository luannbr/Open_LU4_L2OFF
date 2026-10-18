#pragma once

#include "SourceLog.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <functional>
#include <limits>
#include <string_view>
#include <thread>

#if defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#elif defined(__linux__)
#include <pthread.h>
#include <signal.h>
#endif

namespace runtime::logging {

// File rotation needs the whole local calendar date, even when no records
// were written for a month or year. Legacy logger owners keep this key in
// their existing previous-day field. No object layout or filename changes.
// Out-of-range fields or unrepresentable keys return -1 to retry selection.
inline std::int32_t FileDateKey(const std::tm& date) noexcept {
    if (date.tm_mon < 0 || date.tm_mon > 11 || date.tm_mday < 1 || date.tm_mday > 31) {
        return -1;
    }
    const std::int64_t year = static_cast<std::int64_t>(date.tm_year) + 1900;
    const std::int64_t key = year * 372 + date.tm_mon * 31 + date.tm_mday;
    return year < 0 || key > std::numeric_limits<std::int32_t>::max() ? -1 : static_cast<std::int32_t>(key);
}

enum class Level { Debug, Info, Warning, Error };

inline const char* LevelName(Level level) noexcept {
    switch (level) {
        case Level::Debug:
            return "DEBUG";
        case Level::Info:
            return "INFO";
        case Level::Warning:
            return "WARN";
        case Level::Error:
            return "ERROR";
    }
    return "ERROR";
}

// Operator console transport, separate from recovered packet/audit records.
// No global logger lifetime, allocation or application lock is required: one
// bounded fwrite submits the entire record through the CRT's FILE lock.
// Callers provide UTF-8. Invalid bytes and terminal controls are made visible.
struct Record {
    std::array<char, 8192> bytes{};
    std::size_t length = 0;
};

// Diagnostic role, set only when the current thread actually registers it.
// Separate from recovered TLS IDs: an unregistered thread must not appear as io-0.
inline thread_local std::array<char, 32> operatorThreadName{};

inline void SetThreadName(std::string_view name) noexcept {
    std::size_t size = 0;
    for (char value : name) {
        if (size == operatorThreadName.size() - 1) {
            break;
        }
        const bool safe = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') || value == '-' || value == '_' || value == '.';
        operatorThreadName[size++] = safe ? value : '_';
    }
    operatorThreadName[size] = '\0';
}

inline void SetIndexedThreadName(std::string_view role, unsigned long long index) noexcept {
    const int savedErrno = errno;
    // char name[64]{}
    // const auto length = role.size() < 16 ? role.size() : std::size_t{16}
    // std::snprintf(name, sizeof(name), "%.*s-%llu", static_cast<int>(length),
    //     role.empty() ? "" : role.data(), index)
    // SetThreadName(name)
    // Reserve enough space for the entire index in the diagnostic TLS label.
    char suffix[22]{};
    const int suffixLength = std::snprintf(suffix, sizeof(suffix), "-%llu", index);
    // Do not convert a formatting failure to an unsigned capacity calculation.
    if (suffixLength <= 0 || static_cast<std::size_t>(suffixLength) >= sizeof(suffix)) {
        SetThreadName(role);
        errno = savedErrno;
        return;
    }
    const auto available = operatorThreadName.size() - 1 - static_cast<std::size_t>(suffixLength);
    char name[64]{};
    const auto length = role.size() < available ? role.size() : available;
    // std::snprintf(name, sizeof(name), "%.*s%s", static_cast<int>(length),
    //     role.empty() ? "" : role.data(), suffix)
    const int nameLength =
        std::snprintf(name, sizeof(name), "%.*s%s", static_cast<int>(length), role.empty() ? "" : role.data(), suffix);
    if (nameLength <= 0 || static_cast<std::size_t>(nameLength) >= sizeof(name)) {
        SetThreadName(role);
        errno = savedErrno;
        return;
    }
    SetThreadName(name);
    errno = savedErrno;
}

inline std::size_t AppendEscapedLogText(
    Record& output, std::size_t& used, std::string_view message, std::size_t limit
) noexcept {
    auto& record = output.bytes;
    std::size_t offset = 0;
    constexpr char hex[] = "0123456789ABCDEF";
    while (offset < message.size()) {
        const unsigned char byte = static_cast<unsigned char>(message[offset]);
        std::size_t length = 1;
        bool escape = byte < 0x20 || byte == 0x7f;
        if (byte >= 0x80) {
            length = byte >= 0xc2 && byte <= 0xdf ? 2
                : byte >= 0xe0 && byte <= 0xef    ? 3
                : byte >= 0xf0 && byte <= 0xf4    ? 4
                                                  : 0;
            bool valid = length != 0 && offset + length <= message.size();
            for (std::size_t i = 1; valid && i < length; ++i) {
                const auto next = static_cast<unsigned char>(message[offset + i]);
                valid = next >= 0x80 && next <= 0xbf;
            }
            if (valid && length >= 3) {
                const auto second = static_cast<unsigned char>(message[offset + 1]);
                valid = !(byte == 0xe0 && second < 0xa0) && !(byte == 0xed && second >= 0xa0) &&
                    !(byte == 0xf0 && second < 0x90) && !(byte == 0xf4 && second >= 0x90);
            }
            if (!valid) {
                escape = true;
                length = 1;
            }
        }
        const std::size_t outputLength = escape ? 4 : length;
        if (used + outputLength > limit) {
            break;
        }
        if (escape) {
            record[used++] = '\\';
            record[used++] = 'x';
            record[used++] = hex[byte >> 4];
            record[used++] = hex[byte & 15];
        } else {
            for (std::size_t i = 0; i < length; ++i) {
                record[used++] = message[offset + i];
            }
        }
        offset += length;
    }
    return offset;
}

// Recovered call sites often put the same file and a historical source line at
// the start of the message. SourceLog already prints the current basename, so
// hide only that exact duplicate from operator output. The original formatted
// message remains untouched for non-operator destinations.
inline std::string_view StripDuplicateLegacySourcePrefix(std::string_view message, std::string_view sourceFile) noexcept {
    const auto currentFile = SourceFileName(sourceFile);
    if (currentFile.empty() || message.size() < currentFile.size() + 5 || message.front() != '[') {
        return message;
    }
    const auto fileEnd = message.find(']');
    if (fileEnd == std::string_view::npos || SourceFileName(message.substr(1, fileEnd - 1)) != currentFile ||
        fileEnd + 2 >= message.size() || message[fileEnd + 1] != '[') {
        return message;
    }
    const auto lineEnd = message.find(']', fileEnd + 2);
    if (lineEnd == std::string_view::npos || lineEnd == fileEnd + 2) {
        return message;
    }
    for (std::size_t index = fileEnd + 2; index < lineEnd; ++index) {
        const char value = message[index];
        const bool sourceCoordinate = (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
            (value >= 'A' && value <= 'F') || value == 'x' || value == 'X';
        if (!sourceCoordinate) {
            return message;
        }
    }
    message.remove_prefix(lineEnd + 1);
    while (!message.empty() && (message.front() == ' ' || message.front() == '\t')) {
        message.remove_prefix(1);
    }
    return message;
}

inline std::string_view StripDuplicateLegacySourceSuffix(std::string_view message, std::string_view sourceFile) noexcept {
    const auto currentFile = SourceFileName(sourceFile);
    if (currentFile.empty()) {
        return message;
    }
    const auto fileName = message.rfind(currentFile);
    if (fileName == std::string_view::npos || fileName == 0 || message[fileName - 1] != '[') {
        return message;
    }
    std::size_t cursor = fileName + currentFile.size();
    if (cursor >= message.size() || message[cursor++] != ']') {
        return message;
    }
    while (cursor < message.size() && message[cursor] == ' ') {
        ++cursor;
    }
    if (cursor < message.size() && message[cursor] == ',') {
        ++cursor;
    }
    while (cursor < message.size() && message[cursor] == ' ') {
        ++cursor;
    }
    constexpr std::string_view lineWord = "line";
    if (message.substr(cursor, lineWord.size()) != lineWord) {
        return message;
    }
    cursor += lineWord.size();
    while (cursor < message.size() && message[cursor] == ' ') {
        ++cursor;
    }
    if (cursor >= message.size() || message[cursor++] != '[') {
        return message;
    }
    const std::size_t coordinateBegin = cursor;
    while (cursor < message.size() && message[cursor] != ']') {
        const char value = message[cursor++];
        const bool sourceCoordinate = (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
            (value >= 'A' && value <= 'F') || value == 'x' || value == 'X';
        if (!sourceCoordinate) {
            return message;
        }
    }
    if (cursor == coordinateBegin || cursor >= message.size()) {
        return message;
    }
    ++cursor;
    while (cursor < message.size() &&
        (message[cursor] == ' ' || message[cursor] == '\t' || message[cursor] == '.' || message[cursor] == ',' ||
            message[cursor] == ';' || message[cursor] == ':')) {
        ++cursor;
    }
    if (cursor != message.size()) {
        return message;
    }

    std::size_t cut = fileName - 1;
    std::size_t wordEnd = cut;
    while (wordEnd > 0 && message[wordEnd - 1] == ' ') {
        --wordEnd;
    }
    constexpr std::string_view fileWord = "file";
    if (wordEnd >= fileWord.size() && message.substr(wordEnd - fileWord.size(), fileWord.size()) == fileWord) {
        cut = wordEnd - fileWord.size();
        wordEnd = cut;
        while (wordEnd > 0 && message[wordEnd - 1] == ' ') {
            --wordEnd;
        }
        constexpr std::string_view atWord = "at";
        if (wordEnd >= atWord.size() && message.substr(wordEnd - atWord.size(), atWord.size()) == atWord) {
            cut = wordEnd - atWord.size();
        }
    }
    while (cut > 0 && (message[cut - 1] == ' ' || message[cut - 1] == ',')) {
        --cut;
    }
    message = message.substr(0, cut);
    while (!message.empty() && (message.back() == ' ' || message.back() == '\t' || message.back() == ',')) {
        message.remove_suffix(1);
    }
    return message;
}

// inline Record Format(const char* application, Level level, std::string_view message) noexcept {
inline Record Format(
    const char* application, Level level, std::string_view message, std::string_view sourceFile = {}
) noexcept {
    const int savedErrno = errno;
    Record result;
    while (!message.empty() &&
        (message.back() == '\r' || message.back() == '\n' || message.back() == ' ' || message.back() == '\t')) {
        message.remove_suffix(1);
    }
    while (!message.empty() &&
        (message.front() == ' ' || message.front() == '\t' || message.front() == '\r' || message.front() == '\n')) {
        message.remove_prefix(1);
    }
    message = StripDuplicateLegacySourcePrefix(message, sourceFile);
    message = StripDuplicateLegacySourceSuffix(message, sourceFile);
    if (message.empty()) {
        return result;
    }

    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now - seconds).count();
    const std::time_t time = std::chrono::system_clock::to_time_t(seconds);
    std::tm utc{};
#if defined(_WIN32)
    ::gmtime_s(&utc, &time);
#else
    ::gmtime_r(&time, &utc);
#endif
    auto& record = result.bytes;
    // const int prefixLength = std::snprintf(record.data(), record.size(),
    //     "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ %-5s app=%.32s thread=%zx | ",
    //     utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
    //     utc.tm_hour, utc.tm_min, utc.tm_sec, static_cast<int>(milliseconds),
    //     LevelName(level), application,
    //     std::hash<std::thread::id>{}(std::this_thread::get_id()))
    // Noise: Product identity is already given by the console/file destination.
    // a hash of std::thread::id does not explain the worker's role.
    (void)application; // Keep the shared transport API and existing callers.
    const bool namedThread = operatorThreadName[0] != '\0';
    const int prefixLength = std::snprintf(
        record.data(), record.size(), "%04d-%02d-%02d %02d:%02d:%02d.%03d UTC %-5s %s%s%s", utc.tm_year + 1900,
        utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec, static_cast<int>(milliseconds), LevelName(level),
        namedThread ? "[" : "", operatorThreadName.data(), namedThread ? "] " : ""
    );
    if (prefixLength <= 0 || static_cast<std::size_t>(prefixLength) >= record.size()) {
        errno = savedErrno;
        return result;
    }
    std::size_t used = static_cast<std::size_t>(prefixLength);
    constexpr std::string_view truncated = " [truncated]";
    const std::size_t limit = record.size() - truncated.size() - 1;
    // std::size_t offset = 0
    // constexpr char hex[] = "0123456789ABCDEF"
    // while (offset < message.size()) {
    //     const unsigned char byte = static_cast<unsigned char>(message[offset])
    //     std::size_t length = 1
    //     bool escape = byte < 0x20 || byte == 0x7f
    //     if (byte >= 0x80) {
    //         length = byte >= 0xc2 && byte <= 0xdf ? 2 :
    //                  byte >= 0xe0 && byte <= 0xef ? 3 :
    //                  byte >= 0xf0 && byte <= 0xf4 ? 4 : 0.
    //         bool valid = length != 0 && offset + length <= message.size()
    //         for (std::size_t i = 1, valid && i < length, ++i) {
    //             const auto next = static_cast<unsigned char>(message[offset + i])
    //             valid = next >= 0x80 && next <= 0xbf
    //         }
    //         if (valid && length >= 3) {
    //             const auto second = static_cast<unsigned char>(message[offset + 1])
    //             valid = !(byte == 0xe0 && second < 0xa0) &&
    //                     !(byte == 0xed && second >= 0xa0) &&
    //                     !(byte == 0xf0 && second < 0x90) &&
    //                     !(byte == 0xf4 && second >= 0x90)
    //         }
    //         if (!valid) { escape = true. Length = 1. }
    //     }
    //     const std::size_t outputLength = escape ? 4 : length
    //     if (used + outputLength > limit) break.
    //     if (escape) {
    //         record[used++] = '\\'. Record[used++] = 'x'.
    //         record[used++] = hex[byte >> 4]. Record[used++] = hex[byte & 15].
    //     } else {
    //         for (std::size_t i = 0, i < length, ++i) record[used++] = message[offset + i]
    //     }
    //     offset += length.
    // }
    const auto file = SourceFileName(sourceFile);
    if (!file.empty()) {
        record[used++] = '[';
        const std::size_t fileLimit = used + 512;
        const auto copied = AppendEscapedLogText(result, used, file, fileLimit);
        if (copied != file.size()) {
            record[used++] = '.';
            record[used++] = '.';
            record[used++] = '.';
        }
        record[used++] = ']';
        record[used++] = ' ';
    }
    const auto offset = AppendEscapedLogText(result, used, message, limit);
    if (offset != message.size()) {
        for (char byte : truncated) {
            record[used++] = byte;
        }
    }
    record[used++] = '\n';
    result.length = used;
    errno = savedErrno;
    return result;
}

// File ownership/rotation must be locked by the caller. Report errors to the
// caller instead of recursively entering an application logger.
#if defined(__linux__)
class ConsolePipeSignalScope {
public:
    explicit ConsolePipeSignalScope(bool console) noexcept {
        if (!console) {
            return;
        }
        ::sigemptyset(&pipeSet);
        ::sigaddset(&pipeSet, SIGPIPE);
        error = ::pthread_sigmask(SIG_BLOCK, &pipeSet, &previousMask);
        if (error != 0) {
            return;
        }
        masked = true;
        sigset_t pending{};
        if (::sigpending(&pending) != 0) {
            error = errno;
            return;
        }
        // consumeNewPipe = ::sigismember(&pending, SIGPIPE) == 0 &&
        //                  ::sigismember(&previousMask, SIGPIPE) == 0.
        // Preserve an already pending signal, which may coalesce with ours.
        // If none was pending, consume the write's signal even when the caller
        // already blocked SIGPIPE. Do not leave it to fire on a later unblock.
        consumeNewPipe = ::sigismember(&pending, SIGPIPE) == 0;
    }
    ~ConsolePipeSignalScope() {
        if (!masked) {
            return;
        }
        const int savedErrno = errno;
        if (consumeNewPipe && brokenPipeWrite) {
            const timespec noWait{};
            while (::sigtimedwait(&pipeSet, nullptr, &noWait) == -1 && errno == EINTR) {
            }
        }
        ::pthread_sigmask(SIG_SETMASK, &previousMask, nullptr);
        errno = savedErrno;
    }
    ConsolePipeSignalScope(const ConsolePipeSignalScope&) = delete;
    ConsolePipeSignalScope& operator=(const ConsolePipeSignalScope&) = delete;
    int Error() const noexcept { return error; }
    void RecordResult(bool success, int writeError) noexcept { brokenPipeWrite = !success && writeError == EPIPE; }

private:
    sigset_t pipeSet{};
    sigset_t previousMask{};
    int error = 0;
    bool masked = false;
    bool consumeNewPipe = false;
    bool brokenPipeWrite = false;
};
#endif

inline bool WriteRecord(std::FILE* destination, const Record& record) noexcept {
    if (record.length == 0) {
        return true;
    }
    if (!destination) {
        errno = EBADF;
        return false;
    }
#if defined(__linux__)
    ConsolePipeSignalScope pipeSignal(destination == stdout || destination == stderr);
    if (pipeSignal.Error() != 0) {
        errno = pipeSignal.Error();
        return false;
    }
#endif
#if defined(__APPLE__)
    if (destination == stdout || destination == stderr) {
        // Darwin's descriptor-local flag lets a closed console pipe return
        // EPIPE without changing process signal handlers. Keep it enabled for
        // later writes. Reapply in case the console has been redirected.
        const int descriptor = ::fileno(destination);
        if (descriptor < 0 || ::fcntl(descriptor, F_SETNOSIGPIPE, 1) == -1) {
            return false;
        }
    }
#endif
    // if (std::fwrite(record.bytes.data(), 1, record.length, destination) != record.length) return false
    // return std::fflush(destination) == 0
    const int savedErrno = errno;
    errno = 0;
    const bool success =
        std::fwrite(record.bytes.data(), 1, record.length, destination) == record.length && std::fflush(destination) == 0;
#if defined(__linux__)
    pipeSignal.RecordResult(success, errno);
#endif
    if (success) {
        errno = savedErrno;
    }
    return success;
}

// A separate stream reports console failure without entering an application
// logger. Failed records are attempted on stderr. A partial stdout write can
// therefore duplicate a prefix across destinations. Re-arm only after a
// nonempty successful write. Concurrent writers may observe interleaved results.
// this latch bounds duplicate diagnostics, not the ordering of stream recovery.
inline std::atomic_flag consoleWriteFailureReported = ATOMIC_FLAG_INIT;

inline bool WriteConsoleRecord(const Record& record) noexcept {
    if (record.length == 0) {
        return true;
    }
    const int savedErrno = errno;
    errno = 0;
    if (WriteRecord(stdout, record)) {
        consoleWriteFailureReported.clear(std::memory_order_relaxed);
        errno = savedErrno;
        return true;
    }
    const int writeError = errno;
    if (!consoleWriteFailureReported.test_and_set(std::memory_order_relaxed)) {
        char message[192]{};
        // const int length = std::snprintf(message, sizeof(message),
        //     "[logging] Could not write or flush stdout (CRT errno %d. 0 means unavailable). Further console-write errors
        //     are suppressed until a successful write.", writeError)
        const int length = std::snprintf(
            message, sizeof(message),
            "[logging] stdout write failed (CRT errno %d; 0 means unavailable). Attempting failed records on stderr; "
            "repeated stdout errors are suppressed until recovery.",
            writeError
        );
        const auto diagnostic = Format(
            nullptr, Level::Error,
            length > 0 && static_cast<std::size_t>(length) < sizeof(message)
                ? std::string_view(message, static_cast<std::size_t>(length))
                : std::string_view("[logging] Could not write or flush stdout."),
            __FILE__
        );
        // Best effort only: stderr may also be unavailable. Never recurse.
        WriteRecord(stderr, diagnostic);
    }
    // Keep the original record available when the other console stream works.
    // Return false even if this succeeds: the requested stdout write failed.
    WriteRecord(stderr, record);
    errno = writeError;
    return false;
}

// inline void Write(const char* application, Level level, std::string_view message) noexcept {
inline void Write(
    const char* application, Level level, std::string_view message, std::string_view sourceFile = {}
) noexcept {
    const int savedErrno = errno;
    // const Record record = Format(application, level, message)
    const Record record = Format(application, level, message, sourceFile);
    // WriteRecord(stdout, record)
    WriteConsoleRecord(record);
    errno = savedErrno;
}

} // namespace runtime::logging
