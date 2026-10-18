#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace cached::portable {

// Host ownership for Cached.exe's process-lifetime LISTEN thread. The
// shipping GUI process exits this thread only during process teardown. The
// portable console host supplies SIGINT/SIGTERM stop and join meaning
// without adding methods or fields to any recovered target class.
class CacheDProcessLifecycle {
public:
    using ListenerBody = std::function<void()>;

    CacheDProcessLifecycle() = default;
    ~CacheDProcessLifecycle();

    CacheDProcessLifecycle(
        const CacheDProcessLifecycle&) = delete;
    CacheDProcessLifecycle& operator=(
        const CacheDProcessLifecycle&) = delete;

    void InstallSignalHandlers();
    bool StartListener(ListenerBody body);
    void RequestStop() noexcept;
    bool StopRequested() const noexcept;
    bool WaitForStopFor(
        std::chrono::milliseconds timeout);
    void JoinListener();
    void* ListenerNativeHandle() noexcept;

private:
    std::atomic<bool> stopRequested_{false};
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread listener_;
};

} // namespace cached::portable
