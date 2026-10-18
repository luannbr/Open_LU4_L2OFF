#pragma once

// Cross-platform representation of the process-wide native thread handles.

#include <cstdint>
#include <functional>
#include <thread>
#include <type_traits>

#if defined(_WIN32)
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <pthread.h>
#endif

namespace runtime {

namespace detail {

template <typename NativeHandle>
inline void* OpaqueNativeThreadHandle(NativeHandle handle) noexcept {
    static_assert(
        std::is_pointer_v<NativeHandle> || std::is_integral_v<NativeHandle>,
        "unsupported std::thread native handle representation"
    );
    if constexpr (std::is_pointer_v<NativeHandle>) {
        return const_cast<void*>(static_cast<const void*>(handle));
    } else {
        return reinterpret_cast<void*>(static_cast<std::uintptr_t>(handle));
    }
}

template <typename NativeHandle>
inline NativeHandle NativeThreadHandleFromOpaque(void* handle) noexcept {
    static_assert(
        std::is_pointer_v<NativeHandle> || std::is_integral_v<NativeHandle>,
        "unsupported std::thread native handle representation"
    );
    if constexpr (std::is_pointer_v<NativeHandle>) {
        return reinterpret_cast<NativeHandle>(handle);
    } else {
        return static_cast<NativeHandle>(reinterpret_cast<std::uintptr_t>(handle));
    }
}

} // namespace detail

// _beginthreadex returns a Win32 HANDLE, which the target publishes through
// CThreadManager. std::thread owns that handle in the portable composition
// expose the same native token instead of substituting an unrelated object
// address. POSIX builds keep their pthread token only as opaque state.
inline void* NativeThreadHandle(std::thread& thread) noexcept {
    return detail::OpaqueNativeThreadHandle(thread.native_handle());
}

inline std::thread::native_handle_type NativeThreadHandleFromOpaque(void* handle) noexcept {
    return detail::NativeThreadHandleFromOpaque<std::thread::native_handle_type>(handle);
}

// _beginthreadex also writes a 32-bit native thread id into the caller's
// output slot. Windows can recover that exact value from the native handle.
// Darwin exposes the matching kernel thread id. Other POSIX targets
// have no portable cross-thread query, so keep a stable opaque 32-bit id.
inline std::uint32_t NativeThreadId(std::thread& thread) noexcept {
#if defined(_WIN32)
    return static_cast<std::uint32_t>(::GetThreadId(static_cast<HANDLE>(NativeThreadHandle(thread))));
#elif defined(__APPLE__)
    std::uint64_t identifier = 0;
    if (::pthread_threadid_np(thread.native_handle(), &identifier) != 0) {
        return 0;
    }
    return static_cast<std::uint32_t>(identifier);
#else
    return static_cast<std::uint32_t>(std::hash<std::thread::id>{}(thread.get_id()));
#endif
}

} // namespace runtime
