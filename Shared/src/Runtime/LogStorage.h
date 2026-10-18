#pragma once

#include "OperatorLog.h"

#include <cstring>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace runtime::logging {

// Diagnostics must not replace the error from the failed stream operation.
struct StorageErrorState {
    const int savedErrno = errno;
#if defined(_WIN32)
    const DWORD savedWindowsError = ::GetLastError();
#endif
    ~StorageErrorState() {
        errno = savedErrno;
#if defined(_WIN32)
        ::SetLastError(savedWindowsError);
#endif
    }
};

// Capture before fclose. The current requested filename can differ from the
// actual stream after a failed rotation or directory change. Empty means that
// the OS could not supply a complete path. Never replace the new filename.
inline std::array<char, 4096> SnapshotLogFilePath(std::FILE* stream) noexcept {
    const StorageErrorState preserveErrors;
    std::array<char, 4096> path{};
    if (!stream) {
        return path;
    }
#if defined(_WIN32)
    const int descriptor = ::_fileno(stream);
    if (descriptor < 0) {
        return path;
    }
    const auto handle = ::_get_osfhandle(descriptor);
    if (handle == -1) {
        return path;
    }
    std::array<wchar_t, 4096> wide{};
    const DWORD length = ::GetFinalPathNameByHandleW(
        reinterpret_cast<HANDLE>(handle), wide.data(), static_cast<DWORD>(wide.size()), FILE_NAME_OPENED
    );
    if (length == 0 || length >= wide.size()) {
        return path;
    }
    if (::WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), -1, path.data(), static_cast<int>(path.size()), nullptr, nullptr
        ) == 0) {
        path.fill(0);
    }
#elif defined(__APPLE__)
    const int descriptor = ::fileno(stream);
    if (descriptor < 0 || ::fcntl(descriptor, F_GETPATH, path.data()) != 0) {
        path.fill(0);
    }
#elif defined(__linux__)
    const int descriptor = ::fileno(stream);
    if (descriptor < 0) {
        return path;
    }
    std::array<char, 64> link{};
    std::snprintf(link.data(), link.size(), "/proc/self/fd/%d", descriptor);
    const auto length = ::readlink(link.data(), path.data(), path.size());
    if (length < 0 || static_cast<std::size_t>(length) >= path.size()) {
        path.fill(0);
    } else {
        path[static_cast<std::size_t>(length)] = '\0';
    }
#endif
    return path;
}

namespace storage_detail {

struct Failure {
    bool active = false;
    std::array<char, 32> operation{};
    std::array<char, 64> sink{};
    std::array<char, 1024> path{};
    int error = 0;
    unsigned long long count = 0;
    unsigned long long pending = 0;
    std::chrono::steady_clock::time_point lastReport;
};
struct FailureCache {
    std::array<Failure, 16> entries{};
    std::size_t next = 0;
};

inline FailureCache& Cache() noexcept {
    static thread_local FailureCache cache;
    return cache;
}

inline void Emit(
    const char* application, const char* operation, const char* sink, int error, const char* path, unsigned long long count
) noexcept {
    std::array<char, 160> repetition{};
    if (count > 1) {
        // std::snprintf(repetition.data(), repetition.size(), ", %s%llu failures on this thread since the first report",
        //     count == std::numeric_limits<unsigned long long>::max() ? "at least " : "", count)
        std::snprintf(
            repetition.data(), repetition.size(), "; %s%llu failures on this thread, including the first reported failure",
            count == std::numeric_limits<unsigned long long>::max() ? "at least " : "", count
        );
    }
    std::array<char, 48> errorDetail{};
    const int errorLength = std::snprintf(errorDetail.data(), errorDetail.size(), "errno %d", error);
    const char* errorText = error != 0 && errorLength > 0 && static_cast<std::size_t>(errorLength) < errorDetail.size()
        ? errorDetail.data()
        : "no error code reported";
    std::array<char, 8192> message{};
    // const int length = std::snprintf(message.data(), message.size(),
    //     "[logging] Cannot %s %s%slog file (errno %d%s), path \"%s\". Check the path, permissions and free disk space.",
    //     std::strcmp(operation, "set_directory") == 0 ? "select the directory for" : operation,
    //     sink, *sink ? " " : "", error, repetition.data(), *path ? path : "<path unavailable>")
    const int length = std::snprintf(
        message.data(), message.size(),
        "[logging] Cannot %s %s%slog file (%s%s), path \"%s\". Check the path, permissions and free disk space.",
        std::strcmp(operation, "set_directory") == 0 ? "select the directory for" : operation, sink, *sink ? " " : "",
        errorText, repetition.data(), *path ? path : "<path unavailable>"
    );
    if (length < 0) {
        // Write(application, Level::Error, "[logging] Could not format a log-storage failure.")
        Write(application, Level::Error, "[logging] Could not format a log-storage failure.", __FILE__);
        return;
    }
    if (static_cast<std::size_t>(length) >= message.size()) {
        constexpr char marker[] = " [truncated]";
        // std::memcpy(message.data() + message.size() - sizeof(marker), marker, sizeof(marker))
        std::size_t end = message.size() - sizeof(marker);
        while (end > 0 && (static_cast<unsigned char>(message[end]) & 0xc0U) == 0x80U) {
            --end;
        }
        std::memcpy(message.data() + end, marker, sizeof(marker));
    }
    // Write(application, Level::Error, message.data())
    Write(application, Level::Error, message.data(), __FILE__);
}

} // namespace storage_detail

// A different path, operation, sink or errno is reportable.
// The first failure is immediate. Identical repeats are summarized on the
// first later failure after 30 seconds or before cache eviction. Cache
// keys are copied in full. Unknown/oversized keys bypass suppression rather
// than sharing a truncated prefix. No allocation, application lock, timer or
// thread-exit destructor is introduced. Pending counts have no exit flush.
inline void ReportLogStorageFailure(
    const char* application, const char* operation, const char* sink, int error, const char* path
) noexcept {
    const StorageErrorState preserveErrors;
    operation = operation ? operation : "access";
    sink = sink ? sink : "";
    path = path ? path : "";
    // static thread_local storage_detail::FailureCache cache.
    auto& cache = storage_detail::Cache();
    if (!*path || std::strlen(operation) >= 32 || std::strlen(sink) >= 64 || std::strlen(path) >= 1024) {
        storage_detail::Emit(application, operation, sink, error, path, 1);
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    for (auto& entry : cache.entries) {
        if (!entry.active || entry.error != error || std::strcmp(entry.operation.data(), operation) != 0 ||
            std::strcmp(entry.sink.data(), sink) != 0 || std::strcmp(entry.path.data(), path) != 0) {
            continue;
        }
        if (entry.count < std::numeric_limits<unsigned long long>::max()) {
            ++entry.count;
        }
        if (entry.pending < std::numeric_limits<unsigned long long>::max()) {
            ++entry.pending;
        }
        if (now - entry.lastReport >= std::chrono::seconds(30)) {
            entry.lastReport = now;
            entry.pending = 0;
            storage_detail::Emit(application, operation, sink, error, path, entry.count);
        }
        return;
    }
    auto& entry = cache.entries[cache.next];
    cache.next = (cache.next + 1) % cache.entries.size();
    if (entry.pending != 0) {
        storage_detail::Emit(
            application, entry.operation.data(), entry.sink.data(), entry.error, entry.path.data(), entry.count
        );
    }
    entry = {};
    entry.active = true;
    std::strcpy(entry.operation.data(), operation);
    std::strcpy(entry.sink.data(), sink);
    std::strcpy(entry.path.data(), path);
    entry.error = error;
    entry.count = 1;
    entry.lastReport = now;
    storage_detail::Emit(application, operation, sink, error, path, 1);
}

// A successful operation resolves only this thread's matching path/operation/
// sink, including all errno variants. Other files and operations remain failed.
// This reports the observed success, not recovery of previously lost records.
inline void ReportLogStorageSuccess(
    const char* application, const char* operation, const char* sink, const char* path
) noexcept {
    const StorageErrorState preserveErrors;
    if (!operation || !path || !*path) {
        return;
    }
    sink = sink ? sink : "";
    const char* action = std::strcmp(operation, "open") == 0 ? "Opening"
        : std::strcmp(operation, "write") == 0               ? "Writing"
        : std::strcmp(operation, "close") == 0               ? "Closing"
                                                             : nullptr;
    if (!action) {
        return;
    }
    unsigned long long count = 0;
    constexpr auto maximum = std::numeric_limits<unsigned long long>::max();
    for (auto& entry : storage_detail::Cache().entries) {
        if (!entry.active || std::strcmp(entry.operation.data(), operation) != 0 ||
            std::strcmp(entry.sink.data(), sink) != 0 || std::strcmp(entry.path.data(), path) != 0) {
            continue;
        }
        count = entry.count > maximum - count ? maximum : count + entry.count;
        entry = {};
    }
    if (count == 0) {
        return;
    }
    // Stored keys fit completely, so this message fits before transport escaping.
    std::array<char, 1536> message{};
    std::snprintf(
        message.data(), message.size(),
        // "[logging] %s %s%slog file succeeded again after %s%llu earlier failures on this thread, path \"%s\". Previously
        // failed records may be incomplete.", action, sink, *sink ? " " : "", count == maximum ? "at least " : "", count,
        // path)
        "[logging] %s %s%slog file succeeded again after %s%llu earlier failure%s on this thread, path \"%s\". Previously "
        "failed records may be incomplete.",
        action, sink, *sink ? " " : "", count == maximum ? "at least " : "", count, count == 1 ? "" : "s", path
    );
    // Write(application, Level::Info, message.data())
    Write(application, Level::Info, message.data(), __FILE__);
}

// Avoid an OS path query on ordinary successful writes when this thread has
// no unresolved write failure for the sink. No logger object fields are added.
inline void ReportLogWriteSuccess(const char* application, const char* sink, std::FILE* stream) noexcept {
    const StorageErrorState preserveErrors;
    if (!stream) {
        return;
    }
    sink = sink ? sink : "";
    for (const auto& entry : storage_detail::Cache().entries) {
        if (!entry.active || std::strcmp(entry.operation.data(), "write") != 0 ||
            std::strcmp(entry.sink.data(), sink) != 0) {
            continue;
        }
        const auto path = SnapshotLogFilePath(stream);
        ReportLogStorageSuccess(application, "write", sink, path.data());
        return;
    }
}

} // namespace runtime::logging
