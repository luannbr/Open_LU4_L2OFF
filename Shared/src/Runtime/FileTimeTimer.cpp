#include "Shared/src/Runtime/FileTimeTimer.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <new>

namespace runtime {
namespace {

constexpr std::int64_t kWindowsEpochOffset = 116444736000000000LL;

} // namespace

std::int64_t CurrentFileTimeTicks() noexcept {
    const auto sinceUnixEpoch = std::chrono::system_clock::now().time_since_epoch();
    const auto ticks =
        std::chrono::duration_cast<std::chrono::duration<std::int64_t, std::ratio<1, 10000000>>>(sinceUnixEpoch);
    return kWindowsEpochOffset + ticks.count();
}

namespace {

struct FileTimeTimer {
    std::mutex mutex;
    std::condition_variable wake;
    std::int64_t dueFileTime = 0;
    bool armed = false;
    bool stopping = false;
};

} // namespace

void* CreateFileTimeTimer() noexcept { return new (std::nothrow) FileTimeTimer; }

void DestroyFileTimeTimer(void* opaqueTimer) noexcept { delete static_cast<FileTimeTimer*>(opaqueTimer); }

void ArmFileTimeTimer(void* opaqueTimer, std::int64_t dueFileTime) noexcept {
    auto* const timer = static_cast<FileTimeTimer*>(opaqueTimer);
    if (!timer) {
        return;
    }
    {
        std::lock_guard<std::mutex> guard(timer->mutex);
        timer->dueFileTime = dueFileTime;
        timer->armed = true;
    }
    timer->wake.notify_one();
}

FileTimeTimerWaitResult WaitForFileTimeTimer(void* opaqueTimer) noexcept {
    auto* const timer = static_cast<FileTimeTimer*>(opaqueTimer);
    if (!timer) {
        return FileTimeTimerWaitResult::Stop;
    }

    std::unique_lock<std::mutex> guard(timer->mutex);
    for (;;) {
        if (timer->stopping) {
            return FileTimeTimerWaitResult::Stop;
        }
        if (!timer->armed) {
            timer->wake.wait(guard);
            continue;
        }

        const std::int64_t remaining = timer->dueFileTime - CurrentFileTimeTicks();
        if (remaining <= 0) {
            timer->armed = false;
            return FileTimeTimerWaitResult::Timer;
        }
        timer->wake.wait_for(guard, std::chrono::duration<std::int64_t, std::ratio<1, 10000000>>(remaining));
    }
}

void SignalFileTimeTimerStop(void* opaqueTimer) noexcept {
    auto* const timer = static_cast<FileTimeTimer*>(opaqueTimer);
    if (!timer) {
        return;
    }
    {
        std::lock_guard<std::mutex> guard(timer->mutex);
        timer->stopping = true;
    }
    timer->wake.notify_all();
}

} // namespace runtime
