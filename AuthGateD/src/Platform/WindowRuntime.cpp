#include "Platform/WindowRuntime.h"

#include "WindowMgr.h"
#include "Shared/src/Runtime/DirectLog.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace authgated::platform {
namespace {

using Clock = std::chrono::steady_clock;

struct TimerState {
    std::chrono::milliseconds period{0};
    Clock::time_point deadline;
};

struct WindowRuntime {
    explicit WindowRuntime(CWindowMgr* value)
        : owner(value) {}

    CWindowMgr* owner = nullptr;
    std::mutex mutex;
    std::condition_variable wake;
    std::unordered_map<std::uint32_t, TimerState> timers;
    std::uint64_t revision = 0;
    bool quit = false;
};

struct Registry {
    std::mutex mutex;
    std::vector<std::shared_ptr<WindowRuntime>> entries;
};

Registry& RuntimeRegistry() {
    static auto* const registry = new Registry;
    return *registry;
}

std::shared_ptr<WindowRuntime> FindRuntime(
    CWindowMgr* owner) {
    Registry& registry = RuntimeRegistry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    const auto found = std::find_if(
        registry.entries.begin(),
        registry.entries.end(),
        [owner](const auto& runtime) {
            return runtime->owner == owner;
        });
    return found == registry.entries.end()
        ? std::shared_ptr<WindowRuntime>{}
        : *found;
}

} // namespace

void RegisterWindowRuntime(CWindowMgr* owner) {
    if (!owner) return;
    Registry& registry = RuntimeRegistry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    const auto found = std::find_if(
        registry.entries.begin(),
        registry.entries.end(),
        [owner](const auto& runtime) {
            return runtime->owner == owner;
        });
    if (found == registry.entries.end()) {
        registry.entries.push_back(
            std::make_shared<WindowRuntime>(owner));
    }
}

void UnregisterWindowRuntime(CWindowMgr* owner) noexcept {
    Registry& registry = RuntimeRegistry();
    std::shared_ptr<WindowRuntime> removed;
    {
        std::lock_guard<std::mutex> guard(registry.mutex);
        const auto found = std::find_if(
            registry.entries.begin(),
            registry.entries.end(),
            [owner](const auto& runtime) {
                return runtime->owner == owner;
            });
        if (found == registry.entries.end()) return;
        removed = *found;
        registry.entries.erase(found);
    }
    {
        std::lock_guard<std::mutex> guard(removed->mutex);
        removed->quit = true;
        removed->timers.clear();
        ++removed->revision;
    }
    removed->wake.notify_all();
}

void SetWindowTimer(
    CWindowMgr* owner,
    std::uint32_t id,
    std::uint32_t milliseconds) {
    const std::shared_ptr<WindowRuntime> runtime =
        FindRuntime(owner);
    // Original: if (!runtime) return.
    if (!runtime) {
        // Names follow CWindowMgr::WinProc's WM_TIMER dispatch.
        const char* purpose = "unknown operation";
        switch (id) {
        case 0x65: purpose = "status display refresh"; break;
        case 0x66: purpose = "AuthD reconnect"; break;
        case 0x67: purpose = "status report"; break;
        case 0x68: purpose = "client connection timeout check"; break;
        case 0x69: purpose = "LogD reconnect"; break;
        default: break;
        }
        // L2_LOG_DIRECT(stderr, runtime::logging::Level::Error,
        //     "[timer] Cannot schedule window timer %u (%u ms): its window runtime is not registered. Timer request ignored.",
        //     id, milliseconds)
        L2_LOG_DIRECT(stderr, runtime::logging::Level::Error,
            "[timer] Cannot schedule %s (timer %u, %u ms): its window runtime is not registered. Timer request ignored.",
            purpose, id, milliseconds);
        return;
    }
    {
        std::lock_guard<std::mutex> guard(runtime->mutex);
        const auto period = std::chrono::milliseconds(milliseconds);
        runtime->timers[id] = {
            period,
            Clock::now() + period};
        ++runtime->revision;
    }
    runtime->wake.notify_all();
}

void KillWindowTimer(
    CWindowMgr* owner,
    std::uint32_t id) noexcept {
    const std::shared_ptr<WindowRuntime> runtime =
        FindRuntime(owner);
    if (!runtime) return;
    {
        std::lock_guard<std::mutex> guard(runtime->mutex);
        runtime->timers.erase(id);
        ++runtime->revision;
    }
    runtime->wake.notify_all();
}

void RunWindowMessageLoop(CWindowMgr* owner) {
    const std::shared_ptr<WindowRuntime> runtime =
        FindRuntime(owner);
    if (!runtime) return;

    std::unique_lock<std::mutex> guard(runtime->mutex);
    while (!runtime->quit) {
        if (runtime->timers.empty()) {
            const std::uint64_t revision = runtime->revision;
            runtime->wake.wait(
                guard,
                [&] {
                    return runtime->quit ||
                        runtime->revision != revision;
                });
            continue;
        }

        const auto earliest = std::min_element(
            runtime->timers.begin(),
            runtime->timers.end(),
            [](const auto& left, const auto& right) {
                return left.second.deadline < right.second.deadline;
            });
        const Clock::time_point deadline = earliest->second.deadline;
        const std::uint64_t revision = runtime->revision;
        if (runtime->wake.wait_until(
                guard,
                deadline,
                [&] {
                    return runtime->quit ||
                        runtime->revision != revision;
                })) {
            continue;
        }

        const Clock::time_point now = Clock::now();
        std::vector<std::uint32_t> ready;
        for (auto& timer : runtime->timers) {
            if (timer.second.deadline <= now) {
                ready.push_back(timer.first);
                timer.second.deadline = now + timer.second.period;
            }
        }
        guard.unlock();
        for (const std::uint32_t id : ready) {
            owner->WinProc(nullptr, 0x0113, id, 0);
        }
        guard.lock();
    }
}

void RequestWindowQuit(CWindowMgr* owner) noexcept {
    const std::shared_ptr<WindowRuntime> runtime =
        FindRuntime(owner);
    if (!runtime) return;
    {
        std::lock_guard<std::mutex> guard(runtime->mutex);
        runtime->quit = true;
        ++runtime->revision;
    }
    runtime->wake.notify_all();
}

} // namespace authgated::platform
