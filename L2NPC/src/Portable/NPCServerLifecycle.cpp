#include "Portable/NPCServerLifecycle.h"

#include "Shared/src/Runtime/NativeThreadHandle.h"

#include <utility>

namespace l2npc {

const char* NPCServerLifecycleStateName(NPCServerLifecycleState state) noexcept {
    switch (state) {
        case NPCServerLifecycleState::Starting:
            return "starting";
        case NPCServerLifecycleState::Ready:
            return "ready";
        case NPCServerLifecycleState::Failed:
            return "failed";
        case NPCServerLifecycleState::Stopping:
            return "stopping";
        case NPCServerLifecycleState::Stopped:
            return "stopped";
    }
    return "unknown";
}

NPCServerLifecycle::~NPCServerLifecycle() {
    RequestStop();
    Join();
}

bool NPCServerLifecycle::Start(BootTask boot, ExceptionSink exceptionSink, ThreadExitTask threadExit) {
    if (!boot) {
        return false;
    }

    std::lock_guard<std::mutex> guard(mutex_);
    if (started_ || worker_.joinable() || state_.load(std::memory_order_acquire) != NPCServerLifecycleState::Stopped) {
        return false;
    }

    started_ = true;
    stopRequested_.store(false, std::memory_order_release);
    Publish(NPCServerLifecycleState::Starting);
    try {
        worker_ = std::thread([this, boot = std::move(boot), exceptionSink = std::move(exceptionSink),
                                  threadExit = std::move(threadExit)]() mutable {
            bool succeeded = false;
            try {
                succeeded = boot();
            } catch (...) {
                if (exceptionSink) {
                    try {
                        exceptionSink(std::current_exception());
                    } catch (...) {
                        // A diagnostic sink cannot replace the original
                        // lifecycle result with another exception.
                    }
                }
            }

            NPCServerLifecycleState expected = NPCServerLifecycleState::Starting;
            if (succeeded) {
                if (state_.compare_exchange_strong(
                        expected, NPCServerLifecycleState::Ready, std::memory_order_acq_rel, std::memory_order_acquire
                    )) {
                    wake_.notify_all();
                    std::unique_lock<std::mutex> lock(mutex_);
                    wake_.wait(lock, [this] { return stopRequested_.load(std::memory_order_acquire); });
                }
            } else {
                state_.compare_exchange_strong(
                    expected, NPCServerLifecycleState::Failed, std::memory_order_acq_rel, std::memory_order_acquire
                );
                wake_.notify_all();
            }

            if (threadExit) {
                try {
                    threadExit();
                } catch (...) {
                    // Thread ownership must be released even if a
                    // diagnostic cleanup hook is faulty.
                }
            }
        });
    } catch (...) {
        Publish(NPCServerLifecycleState::Failed);
        if (exceptionSink) {
            try {
                exceptionSink(std::current_exception());
            } catch (...) {
            }
        }
        return false;
    }
    return true;
}

void NPCServerLifecycle::RequestStop() {
    {
        // The condition and its notification must be ordered with the same
        // mutex used by WaitForStopFor(). An atomic condition alone does not
        // prevent a notify from landing between the waiter's condition check
        // and the condition-variable wait.
        std::lock_guard<std::mutex> guard(mutex_);
        stopRequested_.store(true, std::memory_order_release);

        NPCServerLifecycleState current = state_.load(std::memory_order_acquire);
        while (current != NPCServerLifecycleState::Stopped && current != NPCServerLifecycleState::Stopping &&
            !state_.compare_exchange_weak(
                current, NPCServerLifecycleState::Stopping, std::memory_order_acq_rel, std::memory_order_acquire
            )) {
        }
    }
    wake_.notify_all();
}

bool NPCServerLifecycle::StopRequested() const noexcept { return stopRequested_.load(std::memory_order_acquire); }

bool NPCServerLifecycle::WaitForStopFor(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> guard(mutex_);
    return wake_.wait_for(guard, timeout, [this] { return stopRequested_.load(std::memory_order_acquire); });
}

void NPCServerLifecycle::MarkReady() {
    NPCServerLifecycleState expected = NPCServerLifecycleState::Starting;
    if (state_.compare_exchange_strong(
            expected, NPCServerLifecycleState::Ready, std::memory_order_acq_rel, std::memory_order_acquire
        )) {
        wake_.notify_all();
    }
}

void NPCServerLifecycle::MarkFailed() {
    NPCServerLifecycleState expected = NPCServerLifecycleState::Starting;
    if (state_.compare_exchange_strong(
            expected, NPCServerLifecycleState::Failed, std::memory_order_acq_rel, std::memory_order_acquire
        )) {
        wake_.notify_all();
    }
}

void NPCServerLifecycle::Join() {
    std::thread worker;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) {
            worker = std::move(worker_);
        }
    }
    if (worker.joinable()) {
        worker.join();
    }
}

void* NPCServerLifecycle::NativeHandle() {
    std::lock_guard<std::mutex> guard(mutex_);
    return worker_.joinable() ? runtime::NativeThreadHandle(worker_) : nullptr;
}

void NPCServerLifecycle::MarkStopped() { Publish(NPCServerLifecycleState::Stopped); }

NPCServerLifecycleState NPCServerLifecycle::State() const noexcept {
    // Ready's acquire load publishes all successful BootTask writes to the
    // console/main thread.
    return state_.load(std::memory_order_acquire);
}

void NPCServerLifecycle::Publish(NPCServerLifecycleState state) noexcept {
    state_.store(state, std::memory_order_release);
    wake_.notify_all();
}

} // namespace l2npc
