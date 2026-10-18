#include "Shared/src/Runtime/TimerQueue.h"
#include <atomic>
#include "Shared/src/Runtime/OperatorLog.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

namespace runtime::timer_queue {
namespace {

using Clock = std::chrono::steady_clock;

struct TimerEntry {
    Callback callback = nullptr;
    void* parameter = nullptr;
    Clock::time_point deadline;
    std::chrono::milliseconds period{0};
};

class TimerQueue final {
public:
    bool Start() noexcept {
        try {
            worker_ = std::thread([this] { Run(); });
            return true;
        } catch (...) {
            return false;
        }
    }

    ~TimerQueue() { Stop(); }

    bool Add(
        TimerHandle* timer,
        Callback callback,
        void* parameter,
        std::uint32_t dueTimeMilliseconds,
        std::uint32_t periodMilliseconds
    ) noexcept {
        if (timer == nullptr || callback == nullptr) {
            return false;
        }

        std::shared_ptr<TimerEntry> entry;
        try {
            entry = std::make_shared<TimerEntry>();
            entry->callback = callback;
            entry->parameter = parameter;
            entry->deadline = Clock::now() + std::chrono::milliseconds(dueTimeMilliseconds);
            entry->period = std::chrono::milliseconds(periodMilliseconds);
        } catch (...) {
            return false;
        }

        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (stopping_) {
                return false;
            }
            try {
                timers_.push_back(entry);
            } catch (...) {
                return false;
            }
            ++revision_;
            *timer = entry.get();
        }
        wake_.notify_one();
        return true;
    }

    bool Remove(TimerHandle timer) noexcept {
        if (timer == nullptr) {
            return false;
        }
        {
            std::lock_guard<std::mutex> guard(mutex_);
            const auto found =
                std::find_if(timers_.begin(), timers_.end(), [timer](const auto& entry) { return entry.get() == timer; });
            if (found == timers_.end()) {
                return false;
            }
            timers_.erase(found);
            ++revision_;
        }
        wake_.notify_one();
        return true;
    }

private:
    void Stop() noexcept {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (stopping_) {
                return;
            }
            stopping_ = true;
            timers_.clear();
            ++revision_;
        }
        wake_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void Run() noexcept {
        // Diagnostic sequence only. Independent of recovered TLS IDs and socket handles.
        static std::atomic<unsigned long long> nextLogIndex{0};
        logging::SetIndexedThreadName("timer-queue", nextLogIndex.fetch_add(1, std::memory_order_relaxed));
        for (;;) {
            std::vector<std::shared_ptr<TimerEntry>> ready;
            {
                std::unique_lock<std::mutex> guard(mutex_);
                for (;;) {
                    if (stopping_) {
                        return;
                    }
                    if (timers_.empty()) {
                        const std::uint64_t revision = revision_;
                        wake_.wait(guard, [this, revision] { return stopping_ || revision_ != revision; });
                        continue;
                    }

                    const auto earliest =
                        std::min_element(timers_.begin(), timers_.end(), [](const auto& left, const auto& right) {
                            return left->deadline < right->deadline;
                        });
                    const Clock::time_point deadline = (*earliest)->deadline;
                    const std::uint64_t revision = revision_;
                    if (wake_.wait_until(guard, deadline, [this, revision] { return stopping_ || revision_ != revision; })) {
                        continue;
                    }

                    const Clock::time_point now = Clock::now();
                    auto current = timers_.begin();
                    while (current != timers_.end()) {
                        const std::shared_ptr<TimerEntry>& entry = *current;
                        if (entry->deadline > now) {
                            ++current;
                            continue;
                        }
                        ready.push_back(entry);
                        if (entry->period.count() == 0) {
                            current = timers_.erase(current);
                        } else {
                            entry->deadline = now + entry->period;
                            ++current;
                        }
                    }
                    ++revision_;
                    break;
                }
            }

            for (const auto& entry : ready) {
                entry->callback(entry->parameter, 0);
            }
        }
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::vector<std::shared_ptr<TimerEntry>> timers_;
    std::thread worker_;
    std::uint64_t revision_ = 0;
    bool stopping_ = false;
};

TimerQueue* DefaultQueue() noexcept {
    // Win32 accepts a null timer-queue handle as the process default queue.
    // Keep the portable equivalent alive for process lifetime. Explicit
    // queues continue to be owned by Create/Delete.
    static TimerQueue* const queue = []() noexcept {
        auto* const created = new (std::nothrow) TimerQueue;
        if (created == nullptr || !created->Start()) {
            delete created;
            return static_cast<TimerQueue*>(nullptr);
        }
        return created;
    }();
    return queue;
}

TimerQueue* ResolveQueue(Handle queue) noexcept {
    return queue == nullptr ? DefaultQueue() : static_cast<TimerQueue*>(queue);
}

} // namespace

Handle Create() noexcept {
    auto* const queue = new (std::nothrow) TimerQueue;
    if (queue == nullptr) {
        return nullptr;
    }
    if (!queue->Start()) {
        delete queue;
        return nullptr;
    }
    return queue;
}

bool Delete(Handle queue) noexcept {
    if (queue == nullptr) {
        return false;
    }
    delete static_cast<TimerQueue*>(queue);
    return true;
}

bool CreateTimer(
    TimerHandle* timer,
    Handle queue,
    Callback callback,
    void* parameter,
    std::uint32_t dueTimeMilliseconds,
    std::uint32_t periodMilliseconds
) noexcept {
    TimerQueue* const owner = ResolveQueue(queue);
    return owner != nullptr && owner->Add(timer, callback, parameter, dueTimeMilliseconds, periodMilliseconds);
}

bool DeleteTimer(Handle queue, TimerHandle timer) noexcept {
    TimerQueue* const owner = ResolveQueue(queue);
    return owner != nullptr && owner->Remove(timer);
}

} // namespace runtime::timer_queue
