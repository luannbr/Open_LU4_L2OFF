#pragma once

#include <cstdint>

struct IoThread;

namespace authgated::platform {

enum class TimerWaitResult {
    Timer,
    Stop,
};

void* CreateThreadPoolRuntime() noexcept;
void DestroyThreadPoolRuntime(void* runtime) noexcept;
void AdoptThread(void* runtime, IoThread* thread) noexcept;

void ArmTimer(void* runtime, std::int64_t dueFileTime) noexcept;
TimerWaitResult WaitForTimer(void* runtime) noexcept;
void SignalThreadPoolStop(void* runtime) noexcept;

} // namespace authgated::platform
