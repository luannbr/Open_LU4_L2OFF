#pragma once

#include "Shared/src/Runtime/NativeThreadHandle.h"

#include <cerrno>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <process.h>
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace runtime {

// std::thread does not expose the stack-reserve argument carried by the
// target's _beginthreadex calls. This owner keeps std::thread's join/terminate
// lifetime contract and the requested Windows stack reserve. POSIX workers
// keep at least the native default stack size.
class StackThread {
public:
    StackThread() noexcept = default;

    template <typename Function>
    StackThread(std::size_t stackReserve, Function&& function) {
        Start(stackReserve, std::function<void()>(std::forward<Function>(function)));
    }

    ~StackThread() {
        if (joinable()) {
            std::terminate();
        }
    }

    StackThread(const StackThread&) = delete;
    StackThread& operator=(const StackThread&) = delete;

    StackThread(StackThread&& other) noexcept { MoveFrom(other); }

    StackThread& operator=(StackThread&& other) noexcept {
        if (joinable()) {
            std::terminate();
        }
        MoveFrom(other);
        return *this;
    }

    bool joinable() const noexcept {
#if defined(_WIN32)
        return m_handle != nullptr;
#else
        return m_joinable;
#endif
    }

    void join() {
        if (!joinable()) {
            throw std::system_error(std::make_error_code(std::errc::invalid_argument));
        }
#if defined(_WIN32)
        const DWORD waitResult = ::WaitForSingleObject(m_handle, INFINITE);
        if (waitResult != WAIT_OBJECT_0) {
            throw std::system_error(static_cast<int>(::GetLastError()), std::system_category());
        }
        ::CloseHandle(m_handle);
        m_handle = nullptr;
#else
        const int result = ::pthread_join(m_thread, nullptr);
        if (result != 0) {
            throw std::system_error(result, std::generic_category());
        }
        m_joinable = false;
#endif
    }

    void* native_handle() noexcept {
#if defined(_WIN32)
        return m_handle;
#else
        return detail::OpaqueNativeThreadHandle(m_thread);
#endif
    }

private:
    struct StartState {
        std::function<void()> function;
    };

    static void Run(StartState* state) noexcept {
        std::unique_ptr<StartState> owner(state);
        try {
            owner->function();
        } catch (...) {
            std::terminate();
        }
    }

#if defined(_WIN32)
    static unsigned __stdcall Entry(void* parameter) noexcept {
        Run(static_cast<StartState*>(parameter));
        return 0;
    }
#else
    static void* Entry(void* parameter) noexcept {
        Run(static_cast<StartState*>(parameter));
        return nullptr;
    }
#endif

    void Start(std::size_t stackReserve, std::function<void()> function) {
        auto state = std::make_unique<StartState>();
        state->function = std::move(function);
#if defined(_WIN32)
        unsigned threadId = 0;
        const std::uintptr_t handle =
            ::_beginthreadex(nullptr, static_cast<unsigned>(stackReserve), &Entry, state.get(), 0, &threadId);
        if (handle == 0) {
            throw std::system_error(errno, std::generic_category());
        }
        m_handle = reinterpret_cast<HANDLE>(handle);
        state.release();
#else
        pthread_attr_t attributes;
        int result = ::pthread_attr_init(&attributes);
        if (result != 0) {
            throw std::system_error(result, std::generic_category());
        }
        // Static TLS can share the POSIX stack allocation. Do not shrink
        // the native default to a Windows reserve that excludes that cost.
        std::size_t nativeStackReserve = 0;
        result = ::pthread_attr_getstacksize(&attributes, &nativeStackReserve);
        if (result == 0 && stackReserve > nativeStackReserve) {
            result = ::pthread_attr_setstacksize(&attributes, stackReserve);
        }
        if (result == 0) {
            result = ::pthread_create(&m_thread, &attributes, &Entry, state.get());
        }
        ::pthread_attr_destroy(&attributes);
        if (result != 0) {
            throw std::system_error(result, std::generic_category());
        }
        m_joinable = true;
        state.release();
#endif
    }

    void MoveFrom(StackThread& other) noexcept {
#if defined(_WIN32)
        m_handle = other.m_handle;
        other.m_handle = nullptr;
#else
        m_thread = other.m_thread;
        m_joinable = other.m_joinable;
        other.m_joinable = false;
#endif
    }

#if defined(_WIN32)
    HANDLE m_handle = nullptr;
#else
    pthread_t m_thread{};
    bool m_joinable = false;
#endif
};

} // namespace runtime
