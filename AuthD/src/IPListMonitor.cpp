#include "IPListMonitor.h"

#include "Log.h"
#include "Util.h"

#include "Shared/src/Runtime/EventHandle.h"
#include "Shared/src/Runtime/NativeThreadHandle.h"
#include "Shared/src/Runtime/OperatorLog.h"
#include "Shared/src/TextRuntime.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#include <process.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace {

constexpr std::size_t kLegacyPathCapacity = 260;

// Diagnostic-only state and conversion: do not change the recovered monitor's
// fields, timestamp comparisons, callback policy or handle ownership.
void ReportMonitorEvent(
    LogType level,
    const char* event,
    const wchar_t* path,
    const char* context,
    const char* errorDomain = nullptr,
    std::int64_t code = 0) noexcept {
    const int savedErrno = errno;
#if defined(_WIN32)
    const DWORD savedLastError = ::GetLastError();
#endif
    try {
        std::size_t length = 0;
        if (path != nullptr) {
            while (length < kLegacyPathCapacity - 1 && path[length] != L'\0') {
                ++length;
            }
        }
        const std::string pathUtf8 = path == nullptr ? "<unavailable>" :
            WideToUtf8(std::wstring_view(path, length));
        if (errorDomain != nullptr) {
            // Log.AddLog(level,
            //     "component=ip_allowlist event=%s path=\"%s\" %s error_domain=%s code=%lld",
            //     event, pathUtf8.c_str(), context, errorDomain,
            //     static_cast<long long>(code))
            // Log.AddLog(level,
            //     "[ip-list] %s for \"%s\". %s (%s error %lld).",
            //     event, pathUtf8.c_str(), context, errorDomain,
            //     static_cast<long long>(code))
            L2_LOG(Log, level,
                "[ip-list] %s for \"%s\". %s (%s error %lld).",
                event, pathUtf8.c_str(), context, errorDomain,
                static_cast<long long>(code));
        } else {
            // Log.AddLog(level,
            //     "component=ip_allowlist event=%s path=\"%s\" %s",
            //     event, pathUtf8.c_str(), context)
            // Log.AddLog(level,
            //     "[ip-list] %s for \"%s\". %s",
            //     event, pathUtf8.c_str(), context)
            L2_LOG(Log, level,
                "[ip-list] %s for \"%s\". %s",
                event, pathUtf8.c_str(), context);
        }
    } catch (...) {
        // Do not let path conversion/log storage failure escape into a watcher
        // failure path. This fallback needs neither allocation nor CLog locks.
        std::array<char, 768> message{};
        // std::snprintf(message.data(), message.size(),
        //     "component=ip_allowlist event=%s path=unavailable %s error_domain=%s code=%lld diagnostic_error=exception sink=console_only",
        //     event, context, errorDomain == nullptr ? "none" : errorDomain,
        //     static_cast<long long>(code))
        // std::snprintf(message.data(), message.size(),
        //     "[ip-list] %s. Path unavailable. %s (%s error %lld). Diagnostic formatting failed. This message reaches only the console.",
        //     event, context, errorDomain == nullptr ? "none" : errorDomain,
        //     static_cast<long long>(code))
        if (errorDomain != nullptr) {
            std::snprintf(message.data(), message.size(),
                "[ip-list] %s; path unavailable. %s (%s error %lld). Diagnostic formatting failed; this message reaches only the console.",
                event, context, errorDomain, static_cast<long long>(code));
        } else {
            std::snprintf(message.data(), message.size(),
                "[ip-list] %s; path unavailable. %s Diagnostic formatting failed; this message reaches only the console.",
                event, context);
        }
        // runtime::logging::Write("authd", runtime::logging::Level::Error,
        //     message.data())
        runtime::logging::Write("authd", runtime::logging::Level::Error,
            message.data(), __FILE__);
    }
    errno = savedErrno;
#if defined(_WIN32)
    ::SetLastError(savedLastError);
#endif
}

void ReportTimestampStatus(
    const wchar_t* path,
    const char* errorDomain,
    std::int64_t code) noexcept {
    // Each monitor owns one worker. Keep suppression local to that thread and
    // its most recent path. No recovered owner fields or application locks.
    static thread_local std::array<wchar_t, kLegacyPathCapacity> previousPath{};
    static thread_local bool failed = false;
    static thread_local std::int64_t previousCode = 0;
    static thread_local std::array<char, 64> previousDomain{};
    const char* const domainKey = errorDomain == nullptr ? "" : errorDomain;
    const wchar_t* const pathKey = path == nullptr ? L"" : path;
    const bool samePath = std::wcsncmp(previousPath.data(), pathKey,
        previousPath.size() - 1) == 0;
    if (code != 0) {
        // Noise: native polling runs every 100 ms. Report the first failed
        // timestamp read for this path until a successful read resets it.
        // if (!failed || !samePath) {
        if (!failed || !samePath || previousCode != code ||
            std::strncmp(previousDomain.data(), domainKey, previousDomain.size()) != 0) {
            // ReportMonitorEvent(LOG_WARN, "monitor_timestamp_failed", path,
            //     "path_kind=file action=return_zero_timestamp repeat_policy=once_per_failure_sequence",
            //     errorDomain, code)
            // ReportMonitorEvent(LOG_WARN, "Could not read the file timestamp", path,
            //     "Returning a zero timestamp. Repeated failures are suppressed until recovery.",
            //     errorDomain, code)
            ReportMonitorEvent(LOG_WARN, "Could not read the file timestamp", path,
                "Returning a zero timestamp; identical repeated failures are suppressed.",
                errorDomain, code);
        }
    } else if (failed && samePath) {
        // ReportMonitorEvent(LOG_NORMAL, "monitor_timestamp_recovered", path,
        //     "path_kind=file result=timestamp_available")
        ReportMonitorEvent(LOG_NORMAL, "Timestamp reads resumed", path,
            "The file timestamp is available again.");
    }
    std::wcsncpy(previousPath.data(), pathKey, previousPath.size() - 1);
    std::strncpy(previousDomain.data(), domainKey, previousDomain.size() - 1);
    previousCode = code;
    failed = code != 0;
}

void CopyPortableExecutablePath(
    char* destination,
    std::size_t capacity) {
    if (destination == nullptr || capacity == 0) {
        return;
    }
    destination[0] = '\0';

#if defined(__APPLE__)
    std::uint32_t nativeCapacity =
        static_cast<std::uint32_t>(capacity);
    if (_NSGetExecutablePath(
            destination,
            &nativeCapacity) != 0) {
        destination[0] = '\0';
    }
#elif defined(__linux__)
    const ssize_t length = ::readlink(
        "/proc/self/exe",
        destination,
        capacity - 1);
    if (length < 0) {
        destination[0] = '\0';
    } else {
        destination[length] = '\0';
    }
#else
    const std::string fallback =
        (std::filesystem::current_path() / "L2Authd").string();
    std::strncpy(
        destination,
        fallback.c_str(),
        capacity);
    destination[capacity - 1] = '\0';
#endif
}

bool FileTimesDiffer(
    const FileTime& left,
    const FileTime& right) noexcept {
    return left.dwLowDateTime != right.dwLowDateTime ||
        left.dwHighDateTime != right.dwHighDateTime;
}

#if !defined(_WIN32)

std::uint64_t PortableTimestampBits(
    const std::filesystem::file_time_type& value) noexcept {
    return static_cast<std::uint64_t>(
        value.time_since_epoch().count());
}

std::thread* WorkerFromHandle(void* handle) noexcept {
    return static_cast<std::thread*>(handle);
}

#endif

} // namespace

// L2Authd.exe 0x00456850, Src/iplistmonitor.cpp:10-27.
CIpListMonitor::CIpListMonitor(wchar_t* fileName)
    : m_fpCallback(nullptr),
      m_pParam(nullptr),
      m_handles{nullptr, nullptr, nullptr} {
    std::array<char, kLegacyPathCapacity> modulePath{};
#if defined(_WIN32)
    ::GetModuleFileNameA(
        nullptr,
        modulePath.data(),
        static_cast<DWORD>(modulePath.size()));
#else
    CopyPortableExecutablePath(
        modulePath.data(),
        modulePath.size());
#endif

    [[maybe_unused]] const int convertedLength =
        AnsiToUnicode(
            modulePath.data(),
            static_cast<int>(modulePath.size()),
            m_ipListPath,
            static_cast<int>(kLegacyPathCapacity));

#if defined(_WIN32)
    m_pBackSlash = std::wcsrchr(m_ipListPath, L'\\');
#else
    m_pBackSlash = std::wcsrchr(m_ipListPath, L'/');
#endif
    // GetModuleFileNameA returns an absolute path in the selected target, so
    // the shipping constructor dereferences this pointer without a null test.
    m_pBackSlash[1] = L'\0';
    std::wcscat(m_ipListPath, fileName);
    m_ftLastWriteTime = GetFileTimestamp(m_ipListPath);
    *m_pBackSlash = L'\0';
}

// L2Authd.exe 0x004569C0, Src/iplistmonitor.cpp:30-32.
CIpListMonitor::~CIpListMonitor() {
    StopMonitor();
}

// L2Authd.exe 0x004569E0, Src/iplistmonitor.cpp:45-76.
unsigned int CIpListMonitor::MonitorLoop() {
    runtime::logging::SetThreadName("ip-list-watch");
#if defined(_WIN32)
    DWORD waitResult = WAIT_FAILED;
    bool waitFailureReported = false;
    DWORD previousWaitResult = WAIT_FAILED;
    DWORD previousWaitError = ERROR_SUCCESS;
    while ((waitResult = ::WaitForMultipleObjects(
                2,
                reinterpret_cast<HANDLE*>(m_handles),
                FALSE,
                INFINITE)) != WAIT_OBJECT_0) {
        if (waitResult == WAIT_OBJECT_0 + 1) {
            if (waitFailureReported) {
                // ReportMonitorEvent(LOG_NORMAL, "monitor_wait_recovered", nullptr,
                //     "result=directory_notification_received")
                // ReportMonitorEvent(LOG_NORMAL, "Directory notifications resumed", nullptr,
                //     "Received a directory-change notification.")
                ReportMonitorEvent(LOG_NORMAL, "Directory notifications resumed", m_ipListPath,
                    "Received a directory-change notification.");
                waitFailureReported = false;
            }
            *m_pBackSlash = L'\\';
            const FileTime current =
                GetFileTimestamp(m_ipListPath);
            *m_pBackSlash = L'\0';

            const FILETIME previousNative{
                m_ftLastWriteTime.dwLowDateTime,
                m_ftLastWriteTime.dwHighDateTime};
            const FILETIME currentNative{
                current.dwLowDateTime,
                current.dwHighDateTime};
            if (::CompareFileTime(
                    &previousNative,
                    &currentNative) != 0) {
                m_fpCallback(m_pParam);
                m_ftLastWriteTime = current;
            }

            ::CloseHandle(
                static_cast<HANDLE>(m_handles[1]));
            m_handles[1] = ::FindFirstChangeNotificationW(
                m_ipListPath,
                FALSE,
                FILE_NOTIFY_CHANGE_FILE_NAME |
                    FILE_NOTIFY_CHANGE_LAST_WRITE);
            if (m_handles[1] == INVALID_HANDLE_VALUE) {
                // ReportMonitorEvent(LOG_ERROR, "monitor_rearm_failed", m_ipListPath,
                //     "path_kind=directory action=continue_wait_with_invalid_notification_handle",
                //     "win32", ::GetLastError())
                ReportMonitorEvent(LOG_ERROR, "Could not rearm directory notifications", m_ipListPath,
                    "The wait loop continues with an invalid notification handle.",
                    "win32", ::GetLastError());
            }
        // } else if (!waitFailureReported) {
        } else {
            const DWORD waitError = waitResult == WAIT_FAILED ? ::GetLastError() : waitResult;
            if (waitFailureReported && previousWaitResult == waitResult &&
                previousWaitError == waitError) {
                continue;
            }
            // Noise: keep the legacy retry loop but report one failure per
            // uninterrupted sequence instead of logging every failed wait.
            // Updated suppression: a different wait result/error starts a new
            // sequence even before directory notifications recover.
            // ReportMonitorEvent(LOG_ERROR, "monitor_wait_failed", nullptr,
            //     "action=retry_wait repeat_policy=once_per_failure_sequence",
            //     waitResult == WAIT_FAILED ? "win32" : "wait_result",
            //     waitResult == WAIT_FAILED ? ::GetLastError() : waitResult)
            // ReportMonitorEvent(LOG_ERROR, "Waiting for directory changes failed", nullptr,
            //     "Retrying the wait. Repeated failures are suppressed until recovery.",
            //     waitResult == WAIT_FAILED ? "win32" : "wait result",
            //     waitResult == WAIT_FAILED ? ::GetLastError() : waitResult)
            // ReportMonitorEvent(LOG_ERROR, "Waiting for directory changes failed", m_ipListPath,
            //     "Retrying the wait. Repeated failures are suppressed until recovery.",
            //     waitResult == WAIT_FAILED ? "win32" : "wait result",
            //     waitResult == WAIT_FAILED ? ::GetLastError() : waitResult)
            ReportMonitorEvent(LOG_ERROR, "Waiting for directory changes failed", m_ipListPath,
                "Retrying the wait; identical repeated failures are suppressed.",
                waitResult == WAIT_FAILED ? "win32" : "wait result", waitError);
            previousWaitResult = waitResult;
            previousWaitError = waitError;
            waitFailureReported = true;
        }
    }

    if (m_handles[0] != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(m_handles[0]));
        m_handles[0] = nullptr;
    }
    if (m_handles[1] != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(m_handles[1]));
        m_handles[1] = nullptr;
    }
#else
    // FindFirstChangeNotification has no standard C++ counterpart. The
    // transport waits on the exact stop event and polls only the file timestamp.
    // callback conditions and published CIpListMonitor state remain unchanged.
    for (;;) {
        void* stopHandle = m_handles[0];
        const auto stop =
            runtime::event_handle::WaitAny(
                &stopHandle,
                1,
                100);
        if (stop.has_value()) {
            break;
        }

        *m_pBackSlash = L'/';
        const FileTime current =
            GetFileTimestamp(m_ipListPath);
        *m_pBackSlash = L'\0';
        if (FileTimesDiffer(
                m_ftLastWriteTime,
                current)) {
            m_fpCallback(m_pParam);
            m_ftLastWriteTime = current;
        }
    }

    if (m_handles[0] != nullptr) {
        runtime::event_handle::Close(m_handles[0]);
        m_handles[0] = nullptr;
    }
    m_handles[1] = nullptr;
#endif
    // Noise: omit a routine worker-exit message. This can run during global
    // teardown after CLog's lifetime. No new logger access belongs here.
    return 0;
}

// L2Authd.exe 0x00456B50, Src/iplistmonitor.cpp:79-89.
void CIpListMonitor::StartMonitor(
    IpListMonitorCallback callback,
    void* parameter) {
    if (callback == nullptr) {
        // ReportMonitorEvent(LOG_WARN, "monitor_start_skipped", nullptr,
        //     "reason=missing_callback action=leave_monitor_unchanged")
        ReportMonitorEvent(LOG_WARN, "File monitoring was not started", nullptr,
            "No callback was supplied; the existing monitor is unchanged.");
        return;
    }

    // Capture the diagnostic directory before the worker starts temporarily
    // restoring the path separator in m_ipListPath.
    std::array<wchar_t, kLegacyPathCapacity> diagnosticDirectory{};
    std::wcsncpy(diagnosticDirectory.data(), m_ipListPath,
        diagnosticDirectory.size() - 1);
    m_fpCallback = callback;
    m_pParam = parameter;
#if defined(_WIN32)
    m_handles[0] = ::CreateEventA(
        nullptr,
        FALSE,
        FALSE,
        nullptr);
    if (m_handles[0] == nullptr) {
        // ReportMonitorEvent(LOG_ERROR, "monitor_stop_event_failed", diagnosticDirectory.data(),
        //     "path_kind=directory stage=create action=continue_start_without_stop_event",
        //     "win32", ::GetLastError())
        ReportMonitorEvent(LOG_ERROR, "Could not create the monitor stop event", diagnosticDirectory.data(),
            "Startup continues without a stop event.",
            "win32", ::GetLastError());
    }
    m_handles[1] = ::FindFirstChangeNotificationW(
        m_ipListPath,
        FALSE,
        FILE_NOTIFY_CHANGE_FILE_NAME |
            FILE_NOTIFY_CHANGE_LAST_WRITE);
    if (m_handles[1] == INVALID_HANDLE_VALUE) {
        // ReportMonitorEvent(LOG_ERROR, "monitor_notification_failed", diagnosticDirectory.data(),
        //     "path_kind=directory stage=create action=continue_start_without_notifications",
        //     "win32", ::GetLastError())
        ReportMonitorEvent(LOG_ERROR, "Could not create directory notifications", diagnosticDirectory.data(),
            "Startup continues without change notifications.",
            "win32", ::GetLastError());
    }
    // Snapshot before the worker can replace/close notification handles.
    const bool monitoringHandlesReady = m_handles[0] != nullptr &&
        m_handles[1] != INVALID_HANDLE_VALUE;
    m_handles[2] = reinterpret_cast<void*>(
        _beginthreadex(
            nullptr,
            0,
            IpListFileMonitorThread,
            this,
            0,
            &threadId));
    if (m_handles[2] == nullptr) {
        // ReportMonitorEvent(LOG_ERROR, "monitor_worker_failed", diagnosticDirectory.data(),
        //     "path_kind=directory stage=create action=automatic_reload_unavailable",
        //     "errno", errno)
        ReportMonitorEvent(LOG_ERROR, "Could not start the file monitor", diagnosticDirectory.data(),
            "Automatic reload is unavailable.",
            "errno", errno);
    } else {
        // ReportMonitorEvent(LOG_NORMAL, "monitor_worker_started", diagnosticDirectory.data(),
        //     "path_kind=directory mode=directory_notification")
        // ReportMonitorEvent(LOG_NORMAL, "File monitoring started", diagnosticDirectory.data(),
        //     "Using directory-change notifications.")
        ReportMonitorEvent(monitoringHandlesReady ? LOG_NORMAL : LOG_ERROR,
            "File-monitor worker started", diagnosticDirectory.data(),
            monitoringHandlesReady
                ? "Directory-change notification handles were created."
                : "Required wait handles could not be created; automatic reload is unavailable.");
    }
#else
    m_handles[0] =
        runtime::event_handle::Create(false, false);
    if (m_handles[0] == nullptr) {
        // ReportMonitorEvent(LOG_ERROR, "monitor_stop_event_failed", diagnosticDirectory.data(),
        //     "path_kind=directory stage=create reason=allocation_failed action=continue_start_without_stop_event")
        ReportMonitorEvent(LOG_ERROR, "Could not create the monitor stop event", diagnosticDirectory.data(),
            "Allocation failed; startup continues without a stop event.");
    }
    m_handles[1] = nullptr;
    try {
        auto* const worker = new std::thread(
            IpListFileMonitorThread,
            this);
        m_handles[2] = worker;
        threadId = runtime::NativeThreadId(*worker);
        // ReportMonitorEvent(LOG_NORMAL, "monitor_worker_started", diagnosticDirectory.data(),
        //     "path_kind=directory mode=timestamp_poll interval_ms=100")
        ReportMonitorEvent(LOG_NORMAL, "File monitoring started", diagnosticDirectory.data(),
            "Checking file timestamps every 100 ms.");
    } catch (...) {
        m_handles[2] = nullptr;
        // ReportMonitorEvent(LOG_ERROR, "monitor_worker_failed", diagnosticDirectory.data(),
        //     "path_kind=directory stage=create reason=exception action=automatic_reload_unavailable")
        ReportMonitorEvent(LOG_ERROR, "Could not start the file monitor", diagnosticDirectory.data(),
            "Worker creation threw; automatic reload is unavailable.");
    }
#endif
}

// L2Authd.exe 0x00456BE0, Src/iplistmonitor.cpp:92-97.
void CIpListMonitor::StopMonitor() {
    if (m_handles[0] != nullptr) {
#if defined(_WIN32)
        ::SetEvent(static_cast<HANDLE>(m_handles[0]));
#else
        runtime::event_handle::Set(m_handles[0]);
#endif
    }

    if (m_handles[2] != nullptr) {
#if defined(_WIN32)
        ::CloseHandle(static_cast<HANDLE>(m_handles[2]));
#else
        std::thread* const worker =
            WorkerFromHandle(m_handles[2]);
        if (worker->joinable()) {
            if (worker->get_id() ==
                std::this_thread::get_id()) {
                worker->detach();
            } else {
                worker->join();
            }
        }
        delete worker;
        m_handles[2] = nullptr;
#endif
    }
}

// L2Authd.exe 0x00456C40, Src/iplistmonitor.cpp:100-114.
FileTime CIpListMonitor::GetFileTimestamp(wchar_t* fileName) {
#if defined(_WIN32)
    WIN32_FIND_DATAW findData{};
    const HANDLE file =
        ::FindFirstFileW(fileName, &findData);
    if (file == INVALID_HANDLE_VALUE) {
        ReportTimestampStatus(fileName, "win32", ::GetLastError());
        findData.ftLastWriteTime.dwLowDateTime = 0;
        findData.ftLastWriteTime.dwHighDateTime = 0;
    } else {
        ReportTimestampStatus(fileName, nullptr, 0);
        ::FindClose(file);
    }
    return {
        findData.ftLastWriteTime.dwLowDateTime,
        findData.ftLastWriteTime.dwHighDateTime};
#else
    std::error_code error;
    const auto timestamp =
        std::filesystem::last_write_time(
            std::filesystem::path(fileName),
            error);
    if (error) {
        ReportTimestampStatus(fileName, error.category().name(), error.value());
        return {};
    }
    ReportTimestampStatus(fileName, nullptr, 0);
    const std::uint64_t bits =
        PortableTimestampBits(timestamp);
    return {
        static_cast<std::uint32_t>(bits),
        static_cast<std::uint32_t>(bits >> 32U)};
#endif
}

// L2Authd.exe 0x00456CE0, Src/iplistmonitor.cpp:35-41. The machine-code
// function ends in `ret 4`, as required by _beginthreadex on Win32. The source
// name and body otherwise match the PDB record exactly.
#if defined(_WIN32)
unsigned int __stdcall IpListFileMonitorThread(void* parameter) {
#else
unsigned int IpListFileMonitorThread(void* parameter) {
#endif
    if (parameter == nullptr) {
        return 1;
    }
    return static_cast<CIpListMonitor*>(parameter)->MonitorLoop();
}
