#pragma once

#include <chrono>
#include <cstdint>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace runtime {

// Cross-platform transport for Win32 GetTickCount. The returned value keeps
// the target's modulo-2^32 millisecond counter and therefore wraps after the
// same interval.
inline std::uint32_t TickCount32() noexcept {
#if defined(_WIN32)
    return static_cast<std::uint32_t>(::GetTickCount());
#else
    using Clock = std::chrono::steady_clock;
    // Win32 GetTickCount is based on system uptime, so values exchanged by
    // separate server processes share one epoch. A process-local start time
    // breaks current cross-process consumers such as the L2Server/L2NPC ping
    // round trip. The native steady-clock epoch is monotonic and shared by
    // processes using the same host implementation. Retain only the target's
    // modulo-2^32 millisecond projection.
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch());
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(elapsed.count()));
#endif
}

// Win32 code commonly subtracts two DWORD ticks and then interprets the
// resulting bits as a signed LONG. Spell that two's-complement conversion
// instead of relying on an implementation-defined unsigned-to-
// signed cast on portable hosts.
inline std::int32_t SignedDifference32(std::uint32_t left, std::uint32_t right) noexcept {
    const std::uint32_t bits = left - right;
    if (bits <= 0x7fffffffU) {
        return static_cast<std::int32_t>(bits);
    }
    return -1 - static_cast<std::int32_t>(0xffffffffU - bits);
}

} // namespace runtime
