#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>

namespace l2npc {

// The shipping NPCServer::Start publishes a LISTEN thread and returns
// immediately. This portable owner adds cooperative stop/join meaning around
// that process-lifetime thread without changing the asynchronous startup
// boundary.
enum class NPCServerLifecycleState {
    Starting,
    Ready,
    Failed,
    Stopping,
    Stopped,
};

const char* NPCServerLifecycleStateName(NPCServerLifecycleState state) noexcept;

class NPCServerLifecycle {
public:
    using BootTask = std::function<bool()>;
    using ExceptionSink = std::function<void(std::exception_ptr)>;
    using ThreadExitTask = std::function<void()>;

    NPCServerLifecycle() = default;
    ~NPCServerLifecycle();

    NPCServerLifecycle(const NPCServerLifecycle&) = delete;
    NPCServerLifecycle& operator=(const NPCServerLifecycle&) = delete;

    // Returns once the worker has been created. Boot completion is published
    // later through State(). False means the worker itself could not be
    // created.
    bool Start(BootTask boot, ExceptionSink exceptionSink = {}, ThreadExitTask threadExit = {});

    void RequestStop();
    bool StopRequested() const noexcept;

    // Returns true when stop was requested and false on timeout.
    bool WaitForStopFor(std::chrono::milliseconds timeout);

    // NPCServer::Run reaches readiness before its final WaitThread call.
    // These two notifications keep that exact blocking boundary while
    // keeping readiness observable by the portable frontend.
    void MarkReady();
    void MarkFailed();

    void Join();

    // Native token returned by _beginthreadex in the target. Ownership stays
    // with this lifecycle and ends at Join().
    void* NativeHandle();

    // The owner calls this only after transports, LogD, IO workers and world
    // state have been torn down. Until then observers keep Stopping.
    void MarkStopped();

    NPCServerLifecycleState State() const noexcept;

private:
    void Publish(NPCServerLifecycleState state) noexcept;

    std::atomic<NPCServerLifecycleState> state_{NPCServerLifecycleState::Stopped};
    std::atomic<bool> stopRequested_{false};
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    bool started_ = false;
};

} // namespace l2npc
