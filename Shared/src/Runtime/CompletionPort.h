#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <limits>
#include <mutex>
#include <new>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace runtime::completion_port {

using Handle = void*;

struct Packet {
    bool success = false;
    std::uint32_t transferred = 0;
    std::uintptr_t completionKey = 0;
    void* overlapped = nullptr;
    int error = 0;
};

enum class WaitStatus {
    Packet,
    Timeout,
    ClosedOrFailed,
};

#if defined(_WIN32)

namespace detail {

// PostQueuedCompletionStatus cannot manufacture the failed completion that
// GetQueuedCompletionStatus normally receives from an overlapped operation.
// Portable socket backends still need to deliver that exact success/error
// tuple, so keep the result beside the native wake packet until Wait consumes
// it. Native completions never enter this table.
struct PostedFailure {
    Handle handle = nullptr;
    std::uint32_t transferred = 0;
    std::uintptr_t completionKey = 0;
    void* overlapped = nullptr;
    int error = 0;
};

struct PostedFailureRegistry {
    std::mutex mutex;
    std::deque<PostedFailure> failures;
};

inline PostedFailureRegistry& FailureRegistry() {
    // Completion ports and their worker threads have process lifetime in the
    // recovered products. Avoid imposing a new static-destruction ordering
    // constraint on their portable transport.
    static auto* const registry = new PostedFailureRegistry;
    return *registry;
}

inline bool TakePostedFailure(
    Handle handle, std::uint32_t transferred, std::uintptr_t completionKey, void* overlapped, int& error
) noexcept {
    PostedFailureRegistry& registry = FailureRegistry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    for (auto current = registry.failures.begin(); current != registry.failures.end(); ++current) {
        if (current->handle == handle && current->transferred == transferred && current->completionKey == completionKey &&
            current->overlapped == overlapped) {
            error = current->error;
            registry.failures.erase(current);
            return true;
        }
    }
    return false;
}

inline void RemovePostedFailure(
    Handle handle, std::uint32_t transferred, std::uintptr_t completionKey, void* overlapped
) noexcept {
    int ignored = 0;
    TakePostedFailure(handle, transferred, completionKey, overlapped, ignored);
}

inline void RemovePostedFailuresFor(Handle handle) noexcept {
    PostedFailureRegistry& registry = FailureRegistry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    auto current = registry.failures.begin();
    while (current != registry.failures.end()) {
        if (current->handle == handle) {
            current = registry.failures.erase(current);
        } else {
            ++current;
        }
    }
}

} // namespace detail

inline Handle Create() noexcept { return ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0); }

inline bool Close(Handle handle) noexcept {
    if (handle == nullptr) {
        return false;
    }
    detail::RemovePostedFailuresFor(handle);
    return ::CloseHandle(handle) != FALSE;
}

inline bool Post(Handle handle, std::uint32_t transferred, std::uintptr_t completionKey, void* overlapped) noexcept {
    return handle != nullptr &&
        ::PostQueuedCompletionStatus(
            handle, static_cast<DWORD>(transferred), static_cast<ULONG_PTR>(completionKey),
            static_cast<LPOVERLAPPED>(overlapped)
        ) != FALSE;
}

inline bool PostResult(
    Handle handle, bool success, std::uint32_t transferred, std::uintptr_t completionKey, void* overlapped, int error = 0
) noexcept {
    if (success) {
        return Post(handle, transferred, completionKey, overlapped);
    }
    if (handle == nullptr) {
        return false;
    }
    {
        detail::PostedFailureRegistry& registry = detail::FailureRegistry();
        std::lock_guard<std::mutex> guard(registry.mutex);
        registry.failures.push_back({handle, transferred, completionKey, overlapped, error});
    }
    if (::PostQueuedCompletionStatus(
            handle, static_cast<DWORD>(transferred), static_cast<ULONG_PTR>(completionKey),
            static_cast<LPOVERLAPPED>(overlapped)
        ) != FALSE) {
        return true;
    }
    detail::RemovePostedFailure(handle, transferred, completionKey, overlapped);
    return false;
}

inline WaitStatus Wait(Handle handle, std::uint32_t timeoutMilliseconds, Packet& packet) noexcept {
    if (handle == nullptr) {
        return WaitStatus::ClosedOrFailed;
    }
    DWORD transferred = 0;
    ULONG_PTR completionKey = 0;
    LPOVERLAPPED overlapped = nullptr;
    const BOOL success = ::GetQueuedCompletionStatus(
        handle, &transferred, &completionKey, &overlapped, static_cast<DWORD>(timeoutMilliseconds)
    );
    const DWORD error = success != FALSE ? ERROR_SUCCESS : ::GetLastError();
    if (success == FALSE && overlapped == nullptr) {
        return error == WAIT_TIMEOUT ? WaitStatus::Timeout : WaitStatus::ClosedOrFailed;
    }
    int postedError = 0;
    const bool postedFailure = success != FALSE &&
        detail::TakePostedFailure(
            handle, static_cast<std::uint32_t>(transferred), static_cast<std::uintptr_t>(completionKey), overlapped,
            postedError
        );
    packet.success = success != FALSE && !postedFailure;
    packet.transferred = static_cast<std::uint32_t>(transferred);
    packet.completionKey = static_cast<std::uintptr_t>(completionKey);
    packet.overlapped = overlapped;
    packet.error = postedFailure ? postedError : static_cast<int>(error);
    return WaitStatus::Packet;
}

#else

namespace detail {

struct State {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Packet> queue;
    bool closed = false;
};

} // namespace detail

inline Handle Create() noexcept { return new (std::nothrow) detail::State; }

inline bool Close(Handle handle) noexcept {
    auto* const state = static_cast<detail::State*>(handle);
    if (state == nullptr) {
        return false;
    }
    {
        std::lock_guard<std::mutex> guard(state->mutex);
        state->closed = true;
    }
    state->wake.notify_all();
    // The recovered process joins all IO workers before closing the port.
    // That makes deletion here equivalent to Win32 CloseHandle ownership.
    delete state;
    return true;
}

inline bool Post(Handle handle, std::uint32_t transferred, std::uintptr_t completionKey, void* overlapped) noexcept {
    auto* const state = static_cast<detail::State*>(handle);
    if (state == nullptr) {
        return false;
    }
    {
        std::lock_guard<std::mutex> guard(state->mutex);
        if (state->closed) {
            return false;
        }
        Packet packet;
        packet.success = true;
        packet.transferred = transferred;
        packet.completionKey = completionKey;
        packet.overlapped = overlapped;
        state->queue.push_back(packet);
    }
    state->wake.notify_one();
    return true;
}

inline bool PostResult(
    Handle handle, bool success, std::uint32_t transferred, std::uintptr_t completionKey, void* overlapped, int error = 0
) noexcept {
    auto* const state = static_cast<detail::State*>(handle);
    if (state == nullptr) {
        return false;
    }
    {
        std::lock_guard<std::mutex> guard(state->mutex);
        if (state->closed) {
            return false;
        }
        Packet packet;
        packet.success = success;
        packet.transferred = transferred;
        packet.completionKey = completionKey;
        packet.overlapped = overlapped;
        packet.error = error;
        state->queue.push_back(packet);
    }
    state->wake.notify_one();
    return true;
}

inline WaitStatus Wait(Handle handle, std::uint32_t timeoutMilliseconds, Packet& packet) noexcept {
    auto* const state = static_cast<detail::State*>(handle);
    if (state == nullptr) {
        return WaitStatus::ClosedOrFailed;
    }
    std::unique_lock<std::mutex> guard(state->mutex);
    const auto ready = [state] { return state->closed || !state->queue.empty(); };
    if (!ready()) {
        if (timeoutMilliseconds == std::numeric_limits<std::uint32_t>::max()) {
            state->wake.wait(guard, ready);
        } else if (!state->wake.wait_for(guard, std::chrono::milliseconds(timeoutMilliseconds), ready)) {
            return WaitStatus::Timeout;
        }
    }
    if (state->queue.empty()) {
        return WaitStatus::ClosedOrFailed;
    }
    packet = state->queue.front();
    state->queue.pop_front();
    return WaitStatus::Packet;
}

#endif

} // namespace runtime::completion_port
