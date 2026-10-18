#include "CacheDProcessLifecycle.h"

#include "Shared/src/Runtime/NativeThreadHandle.h"
#include "Shared/src/Runtime/ConsoleSignals.h"

#include <utility>

namespace cached::portable {

CacheDProcessLifecycle::~CacheDProcessLifecycle() {
    RequestStop();
    JoinListener();
}

void CacheDProcessLifecycle::InstallSignalHandlers() {
    runtime::InstallConsoleSignalHandlers();
}

bool CacheDProcessLifecycle::StartListener(
    ListenerBody body) {
    if (!body || listener_.joinable()) return false;
    stopRequested_.store(false, std::memory_order_release);
    try {
        listener_ = std::thread(
            [body = std::move(body)]() mutable {
                body();
            });
    } catch (...) {
        return false;
    }
    return true;
}

void CacheDProcessLifecycle::RequestStop() noexcept {
    stopRequested_.store(true, std::memory_order_release);
    wake_.notify_all();
}

bool CacheDProcessLifecycle::StopRequested() const noexcept {
    return stopRequested_.load(std::memory_order_acquire) ||
        runtime::ConsoleStopRequested();
}

bool CacheDProcessLifecycle::WaitForStopFor(
    std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> guard(mutex_);
    return wake_.wait_for(
        guard,
        timeout,
        [this] {
            return StopRequested();
        });
}

void CacheDProcessLifecycle::JoinListener() {
    if (listener_.joinable() &&
        listener_.get_id() != std::this_thread::get_id()) {
        listener_.join();
    }
}

void* CacheDProcessLifecycle::ListenerNativeHandle() noexcept {
    return listener_.joinable()
        ? runtime::NativeThreadHandle(listener_)
        : nullptr;
}

} // namespace cached::portable
