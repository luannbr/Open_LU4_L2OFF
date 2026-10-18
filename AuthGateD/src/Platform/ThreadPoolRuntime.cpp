#include "Platform/ThreadPoolRuntime.h"

#include "Thread.h"

#include "Shared/src/Runtime/FileTimeTimer.h"
#include "Shared/src/Runtime/OperatorLog.h"

#include <cstdint>
#include <new>
#include <vector>

namespace authgated::platform {
namespace {

struct ThreadPoolRuntime {
    std::vector<IoThread*> threads;
    void* timer = nullptr;
};

} // namespace

void* CreateThreadPoolRuntime() noexcept {
    auto* const runtime = new (std::nothrow) ThreadPoolRuntime;
    if (!runtime)
        return nullptr;
    runtime->timer = runtime::CreateFileTimeTimer();
    if (!runtime->timer) {
        delete runtime;
        return nullptr;
    }
    return runtime;
}

void DestroyThreadPoolRuntime(void* opaqueRuntime) noexcept {
    auto* runtime = static_cast<ThreadPoolRuntime*>(opaqueRuntime);
    if (!runtime) {
        return;
    }
    for (IoThread* thread : runtime->threads) {
        DestroyIoThread(thread);
    }
    runtime::DestroyFileTimeTimer(runtime->timer);
    delete runtime;
}

void AdoptThread(void* opaqueRuntime, IoThread* thread) noexcept {
    auto* runtime = static_cast<ThreadPoolRuntime*>(opaqueRuntime);
    if (!runtime || !thread) {
        return;
    }
    try {
        runtime->threads.push_back(thread);
    } catch (...) {
        // The thread is already live. Retaining the descriptor is safer than
        // destroying a potentially executing std::thread during allocation
        // failure, matching the target's process-lifetime handle behavior.
        // runtime::logging::Write("authgated", runtime::logging::Level::Error,
        //     "[threads] Cannot track a running pool thread. Its descriptor is retained, but pool shutdown will not visit it.",
        //     __FILE__)
        runtime::logging::Write("authgated", runtime::logging::Level::Error,
            "[threads] Cannot track a running pool thread. Runtime cleanup will not release its descriptor.",
            __FILE__);
    }
}

void ArmTimer(void* opaqueRuntime, std::int64_t dueFileTime) noexcept {
    auto* runtime = static_cast<ThreadPoolRuntime*>(opaqueRuntime);
    if (!runtime) {
        return;
    }
    runtime::ArmFileTimeTimer(runtime->timer, dueFileTime);
}

TimerWaitResult WaitForTimer(void* opaqueRuntime) noexcept {
    auto* runtime = static_cast<ThreadPoolRuntime*>(opaqueRuntime);
    if (!runtime) {
        return TimerWaitResult::Stop;
    }

    return runtime::WaitForFileTimeTimer(runtime->timer) == runtime::FileTimeTimerWaitResult::Timer
        ? TimerWaitResult::Timer
        : TimerWaitResult::Stop;
}

void SignalThreadPoolStop(void* opaqueRuntime) noexcept {
    auto* runtime = static_cast<ThreadPoolRuntime*>(opaqueRuntime);
    if (!runtime) {
        return;
    }
    runtime::SignalFileTimeTimerStop(runtime->timer);
}

} // namespace authgated::platform
