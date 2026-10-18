#include "ConsoleSignals.h"

#include <csignal>

#if defined(_WIN32)
#include <atomic>
#include <Windows.h>
#endif

namespace runtime {
namespace {

volatile std::sig_atomic_t signalStopRequested = 0;

void RequestSignalStop(int) noexcept {
    signalStopRequested = 1;
}

#if defined(_WIN32)
std::atomic<bool> consoleStopRequested{false};

BOOL WINAPI HandleConsoleControl(DWORD control) {
    if (control == CTRL_C_EVENT || control == CTRL_BREAK_EVENT) {
        consoleStopRequested.store(true, std::memory_order_relaxed);
        return TRUE;
    }
    return FALSE;
}
#endif

} // namespace

void InstallConsoleSignalHandlers() {
    signalStopRequested = 0;
    std::signal(SIGINT, RequestSignalStop);
    std::signal(SIGTERM, RequestSignalStop);
#if defined(_WIN32)
    consoleStopRequested.store(false, std::memory_order_relaxed);
    ::SetConsoleCtrlHandler(HandleConsoleControl, TRUE);
#endif
}

bool ConsoleStopRequested() noexcept {
#if defined(_WIN32)
    if (consoleStopRequested.load(std::memory_order_relaxed)) return true;
#endif
    return signalStopRequested != 0;
}

} // namespace runtime
