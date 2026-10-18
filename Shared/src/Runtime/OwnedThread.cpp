#include "Shared/src/Runtime/OwnedThread.h"

#include "Shared/src/Runtime/NativeThreadHandle.h"

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace runtime {
namespace {

struct OwnedThreadState {
    std::mutex mutex;
    std::condition_variable wake;
    bool released = false;
    OwnedThreadProcedure procedure = nullptr;
    void* parameter = nullptr;
    std::thread thread;
    std::uint32_t threadId = 0;
};

} // namespace

void* CreateOwnedThread(OwnedThreadProcedure procedure, void* parameter, bool suspended) noexcept {
    if (!procedure) {
        return nullptr;
    }
    std::unique_ptr<OwnedThreadState> state;
    try {
        state = std::make_unique<OwnedThreadState>();
        state->procedure = procedure;
        state->parameter = parameter;
        state->released = !suspended;
        OwnedThreadState* const raw = state.get();
        state->thread = std::thread([raw] {
            {
                std::unique_lock<std::mutex> guard(raw->mutex);
                raw->wake.wait(guard, [raw] { return raw->released; });
            }
            raw->procedure(raw->parameter);
        });
        state->threadId = NativeThreadId(state->thread);
    } catch (...) {
        return nullptr;
    }
    return state.release();
}

bool ResumeOwnedThread(void* handle) noexcept {
    auto* const state = static_cast<OwnedThreadState*>(handle);
    if (!state) {
        return false;
    }
    {
        std::lock_guard<std::mutex> guard(state->mutex);
        state->released = true;
    }
    state->wake.notify_one();
    return true;
}

void JoinOwnedThread(void* handle) noexcept {
    auto* const state = static_cast<OwnedThreadState*>(handle);
    if (!state) {
        return;
    }
    ResumeOwnedThread(state);
    if (state->thread.joinable()) {
        state->thread.join();
    }
}

void DestroyOwnedThread(void* handle) noexcept {
    std::unique_ptr<OwnedThreadState> state(static_cast<OwnedThreadState*>(handle));
    if (!state) {
        return;
    }
    JoinOwnedThread(state.get());
}

std::uint32_t OwnedThreadId(void* handle) noexcept {
    const auto* const state = static_cast<const OwnedThreadState*>(handle);
    return state ? state->threadId : 0;
}

std::uint32_t CurrentNativeThreadId() noexcept {
#if defined(_WIN32)
    return static_cast<std::uint32_t>(::GetCurrentThreadId());
#else
    return static_cast<std::uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}

} // namespace runtime
