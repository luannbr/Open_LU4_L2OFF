#include "Shared/src/Runtime/OperatorLog.h"
#include "Platform/ProcessRuntime.h"
#include "Shared/src/Runtime/ConsoleSignals.h"

#include "FileLog.h"
#include "Platform/WindowRuntime.h"
#include "WindowMgr.h"

#include "Shared/src/Runtime/ProcessServices.h"
#include "Shared/src/Runtime/SocketTransport.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace authgated::platform {
namespace {

constexpr char16_t kInstanceName[] = u"Global\\{B951F802-278D-4b9d-9A8B-10DAB046B855}";

#if defined(_WIN32)
// Windows ends the process as soon as the console handlers return from these
// events, so nothing else would record why AuthGateD stopped. Registered after
// the shared Ctrl+C handler, this one runs first and lets the default handler
// end the process.
BOOL WINAPI LogConsoleClose(DWORD control) {
    if (control == CTRL_CLOSE_EVENT) {
        L2_LOG_WARN(authGateLog, u"[shutdown] The console window was closed; AuthGateD stops now.");
    } else if (control == CTRL_SHUTDOWN_EVENT) {
        L2_LOG_WARN(authGateLog, u"[shutdown] Windows is shutting down; AuthGateD stops now.");
    }
    return FALSE;
}
#endif

} // namespace

InstanceGateResult AcquireInstanceGate(bool allowMultipleInstances, void*& nativeHandle, std::string& diagnostic) {
    const runtime::InstanceGateResult result = runtime::AcquireNamedInstanceGate(
        allowMultipleInstances, kInstanceName, "AuthGateD-B951F802-278D-4b9d-9A8B-10DAB046B855.lock", nativeHandle,
        diagnostic
    );
    switch (result) {
    case runtime::InstanceGateResult::Acquired:
        return InstanceGateResult::Acquired;
    case runtime::InstanceGateResult::Bypassed:
        return InstanceGateResult::Bypassed;
    case runtime::InstanceGateResult::AlreadyRunning:
        return InstanceGateResult::AlreadyRunning;
    case runtime::InstanceGateResult::Error:
        return InstanceGateResult::Error;
    }
    return InstanceGateResult::Error;
}

void CloseInstanceGate(void*& nativeHandle) noexcept { runtime::CloseNamedInstanceGate(nativeHandle); }

bool SetWorkingDirectoryToExecutableDirectory(std::string& diagnostic) {
    return runtime::SetWorkingDirectoryToExecutableDirectory(diagnostic);
}

int StartSocketRuntime() noexcept { return ::portable::InitializeSocketSubsystem(); }

void StopSocketRuntime() noexcept { ::portable::CleanupSocketSubsystem(); }

void DisableProcessPriorityBoost() noexcept { runtime::DisableCurrentProcessPriorityBoost(); }

void ShowAlreadyRunning() {
#if defined(_WIN32)
    // ::MessageBoxW(nullptr, L"Already running", L"Error", MB_ICONERROR)
    ::MessageBoxW(nullptr,
        L"Another AuthGateD instance is already running. This instance will exit.",
        L"AuthGateD startup", MB_ICONERROR);
#else
    // Noise: service_main emits the common startup diagnostic in every service mode.
    // std::fputs("AuthGateD: Already running\n", stderr)
#endif
}

void ShowSocketStartupError(int error) {
#if defined(_WIN32)
    std::array<wchar_t, 1024> message{};
    // ::swprintf(message.data(), message.size(), L"WSAStartup error 0x%x", error)
    // ::MessageBoxW(nullptr, message.data(), L"Error", MB_ICONERROR)
    ::swprintf(message.data(), message.size(),
        L"Could not initialize the socket runtime (Winsock error %d). Startup aborted (exit code -3).",
        error);
    ::MessageBoxW(nullptr, message.data(), L"AuthGateD startup", MB_ICONERROR);
#else
    // Noise: service_main emits the common startup diagnostic in every service mode.
    // std::fprintf(stderr, "AuthGateD: WSAStartup error 0x%x\n", error)
    (void)error;
#endif
}

SERVICE_STATUS_HANDLE RegisterServiceControlHandler(const char* serviceName, ServiceControlHandler handler) noexcept {
    return runtime::RegisterServiceControlHandler(serviceName, handler);
}

bool StartServiceDispatcher(const char* serviceName, ServiceMain serviceMain) noexcept {
    return runtime::StartServiceDispatcher(serviceName, serviceMain);
}

void PostProcessQuit() noexcept {
    if (CWindowMgr::g_pWindowMgr) {
        // PostQuitMessage is thread-local on Win32. The portable window loop
        // has an explicit owner, so the same quit request is delivered there.
        RequestWindowQuit(CWindowMgr::g_pWindowMgr);
    }
}

struct TerminationBridge::Impl {
    explicit Impl(CWindowMgr* value) : window(value) {
        runtime::InstallConsoleSignalHandlers();
#if defined(_WIN32)
        ::SetConsoleCtrlHandler(LogConsoleClose, TRUE);
#endif
        watcher = std::thread([this] {
            runtime::logging::SetThreadName("signal-watch");
            while (!stopping.load(std::memory_order_acquire)) {
                if (runtime::ConsoleStopRequested()) {
                    RequestWindowQuit(window);
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
        });
    }

    ~Impl() {
#if defined(_WIN32)
        ::SetConsoleCtrlHandler(LogConsoleClose, FALSE);
#endif
        stopping.store(true, std::memory_order_release);
        if (watcher.joinable())
            watcher.join();
    }

    CWindowMgr* window = nullptr;
    std::atomic<bool> stopping{false};
    std::thread watcher;
};

TerminationBridge::TerminationBridge(CWindowMgr* window) : impl_(std::make_unique<Impl>(window)) {}

TerminationBridge::~TerminationBridge() = default;

} // namespace authgated::platform
