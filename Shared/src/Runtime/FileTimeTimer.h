#pragma once

#include <cstdint>

namespace runtime {

enum class FileTimeTimerWaitResult {
    Timer,
    Stop,
};

std::int64_t CurrentFileTimeTicks() noexcept;
void* CreateFileTimeTimer() noexcept;
void DestroyFileTimeTimer(void* timer) noexcept;
void ArmFileTimeTimer(void* timer, std::int64_t dueFileTime) noexcept;
FileTimeTimerWaitResult WaitForFileTimeTimer(void* timer) noexcept;
void SignalFileTimeTimerStop(void* timer) noexcept;

} // namespace runtime
