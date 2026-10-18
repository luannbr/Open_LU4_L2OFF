#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <new>
#include <optional>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace runtime::event_handle {

using Handle = void*;

inline constexpr std::uint32_t kInfinite = std::numeric_limits<std::uint32_t>::max();

#if defined(_WIN32)

inline Handle Create(bool manualReset, bool initialState) noexcept {
    return ::CreateEventA(nullptr, manualReset ? TRUE : FALSE, initialState ? TRUE : FALSE, nullptr);
}

inline bool Close(Handle handle) noexcept { return handle != nullptr && ::CloseHandle(handle) != FALSE; }

inline bool Set(Handle handle) noexcept { return handle != nullptr && ::SetEvent(handle) != FALSE; }

inline bool Reset(Handle handle) noexcept { return handle != nullptr && ::ResetEvent(handle) != FALSE; }

inline std::optional<std::size_t> WaitAny(
    Handle const* handles, std::size_t count, std::uint32_t timeoutMilliseconds
) noexcept {
    if (handles == nullptr || count == 0 || count > MAXIMUM_WAIT_OBJECTS) {
        return std::nullopt;
    }
    const DWORD result =
        ::WaitForMultipleObjects(static_cast<DWORD>(count), handles, FALSE, static_cast<DWORD>(timeoutMilliseconds));
    if (result < WAIT_OBJECT_0 || result >= WAIT_OBJECT_0 + count) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(result - WAIT_OBJECT_0);
}

#else

namespace detail {

struct Coordinator {
    std::mutex mutex;
    std::condition_variable wake;
};

inline Coordinator& SharedCoordinator() {
    static auto* const coordinator = new Coordinator;
    return *coordinator;
}

struct EventState {
    bool manualReset = false;
    bool signaled = false;
};

inline std::optional<std::size_t> FindSignaled(Handle const* handles, std::size_t count, bool consume) noexcept {
    for (std::size_t index = 0; index < count; ++index) {
        auto* const event = static_cast<EventState*>(handles[index]);
        if (event == nullptr) {
            return std::nullopt;
        }
        if (event->signaled) {
            if (consume && !event->manualReset) {
                event->signaled = false;
            }
            return index;
        }
    }
    return std::nullopt;
}

} // namespace detail

inline Handle Create(bool manualReset, bool initialState) noexcept {
    auto* const event = new (std::nothrow) detail::EventState;
    if (event != nullptr) {
        event->manualReset = manualReset;
        event->signaled = initialState;
    }
    return event;
}

inline bool Close(Handle handle) noexcept {
    if (handle == nullptr) {
        return false;
    }
    // As with Win32 CloseHandle, closing a handle while another thread is
    // waiting on it is outside this transport's contract. Recovered owners
    // close their events only after their worker threads have stopped.
    delete static_cast<detail::EventState*>(handle);
    return true;
}

inline bool Set(Handle handle) noexcept {
    auto* const event = static_cast<detail::EventState*>(handle);
    if (event == nullptr) {
        return false;
    }
    detail::Coordinator& coordinator = detail::SharedCoordinator();
    {
        std::lock_guard<std::mutex> guard(coordinator.mutex);
        event->signaled = true;
    }
    coordinator.wake.notify_all();
    return true;
}

inline bool Reset(Handle handle) noexcept {
    auto* const event = static_cast<detail::EventState*>(handle);
    if (event == nullptr) {
        return false;
    }
    detail::Coordinator& coordinator = detail::SharedCoordinator();
    std::lock_guard<std::mutex> guard(coordinator.mutex);
    event->signaled = false;
    return true;
}

inline std::optional<std::size_t> WaitAny(
    Handle const* handles, std::size_t count, std::uint32_t timeoutMilliseconds
) noexcept {
    if (handles == nullptr || count == 0 || count > 64) {
        return std::nullopt;
    }

    detail::Coordinator& coordinator = detail::SharedCoordinator();
    std::unique_lock<std::mutex> guard(coordinator.mutex);
    const auto ready = [handles, count] { return detail::FindSignaled(handles, count, false).has_value(); };

    if (!ready()) {
        if (timeoutMilliseconds == kInfinite) {
            coordinator.wake.wait(guard, ready);
        } else if (!coordinator.wake.wait_for(guard, std::chrono::milliseconds(timeoutMilliseconds), ready)) {
            return std::nullopt;
        }
    }
    return detail::FindSignaled(handles, count, true);
}

#endif

} // namespace runtime::event_handle
