#pragma once

#include <cstdint>

namespace runtime::timer_queue {

using Handle = void*;
using TimerHandle = void*;
#if defined(_WIN32)
using Callback = void(__stdcall*)(void* parameter, std::uint8_t timerOrWaitFired);
#else
using Callback = void (*)(void* parameter, std::uint8_t timerOrWaitFired);
#endif

// Cross-platform transport for the Win32 timer-queue API. Product owners
// keep their recorded HANDLE fields and callback/state transitions. Only
// scheduling and native handle ownership live behind this boundary.
Handle Create() noexcept;
bool Delete(Handle queue) noexcept;

bool CreateTimer(
    TimerHandle* timer,
    Handle queue,
    Callback callback,
    void* parameter,
    std::uint32_t dueTimeMilliseconds,
    std::uint32_t periodMilliseconds
) noexcept;

bool DeleteTimer(Handle queue, TimerHandle timer) noexcept;

} // namespace runtime::timer_queue
