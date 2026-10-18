#include "ThreadPool.h"

#include "Shared/src/ScopedLockGuards.h"

#include "Shared/src/Runtime/OperatorLog.h"

#include "Platform/ThreadPoolRuntime.h"
#include "Processor.h"
#include "Shared/src/Runtime/CompletionPort.h"
#include "Shared/src/Runtime/FileTimeTimer.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

constexpr std::uint32_t kInfiniteWait = 0xffffffffU;
constexpr std::uintptr_t kStopCompletionKey = 1;
} // namespace

// AuthGateD.exe 0x00412DC0, ThreadPool.cpp.
ThreadPool::Slot::Slot() = default;

// AuthGateD.exe 0x00412DE0, ThreadPool.cpp.
TimerItem* ThreadPool::AllocTimerItem() {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            mNumAllocItems.fetch_add(
                1,
                std::memory_order_relaxed) + 1) &
        0x0fU;
    Slot& slot = mFreeItemsSlot[slotIndex];
    TimerItem* item = nullptr;
    {
        L2_GUARD(slot.mLock);
        item = slot.mpTimerItem;
        if (item) {
            slot.mpTimerItem = item->pNextItem;
        }
    }

    if (!item) {
        item = new TimerItem;
    }
    item->pNextItem = nullptr;
    return item;
}

void ThreadPool::FreeTimerItem(TimerItem* item) {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            mNumFreeItems.fetch_add(
                1,
                std::memory_order_relaxed) + 1) &
        0x0fU;
    Slot& slot = mFreeItemsSlot[slotIndex];
    {
        L2_GUARD(slot.mLock);
        item->pNextItem = slot.mpTimerItem;
        // AuthGateD.exe 0x00412EC5 writes the old head back to itself and leaks
        // the completed TimerItem. Re-linking the item here changes no callback or
        // timing result and keeps the intended 16-slot pool usable cross-platform.
        slot.mpTimerItem = item;
    }
}

// AuthGateD.exe 0x00412E40, ThreadPool.cpp.
std::uint32_t ThreadPool::ThreadPoolWorkProc(void* parameter) {
    // runtime::logging::SetIndexedThreadName("timer-work", runtime::CurrentNativeThreadId())
    // Diagnostic numbering only: CurrentNativeThreadId is a hash on POSIX.
    static std::atomic<unsigned long long> nextLogIndex{0};
    runtime::logging::SetIndexedThreadName("timer-work", nextLogIndex.fetch_add(1, std::memory_order_relaxed));
    auto* pool = static_cast<ThreadPool*>(parameter);
    pool->mNumWorkThreads.fetch_add(1, std::memory_order_relaxed);

    runtime::completion_port::Packet packet;
    while (runtime::completion_port::Wait(
               pool->mhIOCP,
               kInfiniteWait,
               packet) ==
           runtime::completion_port::WaitStatus::Packet) {
        if (packet.completionKey == kStopCompletionKey) {
            break;
        }
        auto* item = reinterpret_cast<TimerItem*>(
            packet.completionKey);
        item->callback(item->pParam);
        pool->FreeTimerItem(item);
    }

    pool->mNumWorkThreads.fetch_sub(1, std::memory_order_relaxed);
    return 0;
}

// AuthGateD.exe 0x00413020, ThreadPool.cpp.
void ThreadPool::FinalizeWorkThreads() {
    std::int32_t remaining =
        mNumWorkThreads.load(std::memory_order_relaxed);
    while (remaining-- > 0) {
        // runtime::completion_port::Post(
        //     mhIOCP,
        //     0,
        //     kStopCompletionKey,
        //     nullptr)
        if (!runtime::completion_port::Post(
            mhIOCP,
            0,
            kStopCompletionKey,
            nullptr)) {
            runtime::logging::Write("authgated", runtime::logging::Level::Error,
                "[threads] Cannot queue a pool-worker stop notification. Shutdown will continue waiting for workers to exit.",
                __FILE__);
        }
    }
    while (mNumWorkThreads.load(std::memory_order_relaxed) > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// AuthGateD.exe 0x004131A0, ThreadPool.cpp.
void ThreadPool::SetTimerNextTime() {
    {
        L2_GUARD(mTimerLock);
        std::int64_t nextDueTime = mNextDueTime;
        if (!mTimerMap.empty()) {
            nextDueTime = mTimerMap.begin()->first;
        }
        if (nextDueTime != mNextDueTime) {
            authgated::platform::ArmTimer(mhTimer, nextDueTime);
            mNextDueTime = nextDueTime;
        }
    }
}

// AuthGateD.exe 0x004137E0, ThreadPool.cpp.
bool ThreadPool::QueueTimerWorkItem(
    TimerItem::Callback callback,
    void* parameter,
    std::uint32_t delayMilliseconds) {
    bool full = false;
    {
        L2_GUARD(mTimerLock);
        full =
            static_cast<std::int64_t>(mTimerMap.size()) + 1 >
            mMaxItems;
    }
    if (full) {
        return false;
    }

    const std::uint64_t now = static_cast<std::uint64_t>(runtime::CurrentFileTimeTicks());
    TimerItem* item = AllocTimerItem();
    item->pParam = parameter;
    item->callback = callback;

    // The x86 target adds only the low 32 bits of delay*10000 to FILETIME,
    // then propagates the addition carry. Preserve that exact arithmetic.
    const std::uint32_t nowLow = static_cast<std::uint32_t>(now);
    const std::uint32_t nowHigh = static_cast<std::uint32_t>(now >> 32U);
    const std::uint32_t delta = delayMilliseconds * 10000U;
    const std::uint32_t dueLow = nowLow + delta;
    const std::uint32_t dueHigh = nowHigh + (dueLow < nowLow ? 1U : 0U);
    const std::int64_t dueTime = static_cast<std::int64_t>(
        (static_cast<std::uint64_t>(dueHigh) << 32U) | dueLow);

    {
        L2_GUARD(mTimerLock);
        mTimerMap.emplace(dueTime, item);
    }
    SetTimerNextTime();
    return true;
}

// AuthGateD.exe 0x004139C0, ThreadPool.cpp.
std::uint32_t ThreadPool::ThreadPoolTimerProc(void* parameter) {
    runtime::logging::SetThreadName("timer");
    auto* pool = static_cast<ThreadPool*>(parameter);
    for (;;) {
        if (authgated::platform::WaitForTimer(pool->mhTimer) ==
            authgated::platform::TimerWaitResult::Stop) {
            return 0;
        }

        std::vector<TimerItem*> expired;
        {
            L2_GUARD(pool->mTimerLock);
            auto current = pool->mTimerMap.begin();
            while (current != pool->mTimerMap.end() &&
                   current->first <= pool->mNextDueTime) {
                expired.push_back(current->second);
                current = pool->mTimerMap.erase(current);
            }
        }

        for (TimerItem* item : expired) {
            // runtime::completion_port::Post(
            //     pool->mhIOCP,
            //     0,
            //     reinterpret_cast<std::uintptr_t>(item),
            //     nullptr)
            if (!runtime::completion_port::Post(
                pool->mhIOCP,
                0,
                reinterpret_cast<std::uintptr_t>(item),
                nullptr)) {
                runtime::logging::Write("authgated", runtime::logging::Level::Error,
                    "[threads] Cannot queue an expired timer callback. It was removed from the timer schedule and will not be retried.",
                    __FILE__);
            }
        }
        pool->SetTimerNextTime();
    }
}

// AuthGateD.exe 0x00413BF0, ThreadPool.cpp.
ThreadPool::ThreadPool(std::int32_t maxItems)
    : mMaxItems(maxItems) {
    void* runtime = authgated::platform::CreateThreadPoolRuntime();
    if (!runtime) {
        throw std::runtime_error("AuthGateD ThreadPool runtime creation failed");
    }
    mhTimer = runtime;
    mhEvent = runtime;

    mpTimerThread = OsThreadCreate(
        ThreadPoolTimerProc,
        this,
        "ThreadPoolTimerThread");
    if (!mpTimerThread) {
        authgated::platform::DestroyThreadPoolRuntime(runtime);
        mhTimer = nullptr;
        mhEvent = nullptr;
        throw std::runtime_error("AuthGateD timer thread creation failed");
    }
    authgated::platform::AdoptThread(runtime, mpTimerThread);

    mhIOCP = runtime::completion_port::Create();
    if (!mhIOCP) {
        authgated::platform::SignalThreadPoolStop(runtime);
        authgated::platform::DestroyThreadPoolRuntime(runtime);
        mhTimer = nullptr;
        mhEvent = nullptr;
        mpTimerThread = nullptr;
        throw std::runtime_error("AuthGateD work completion port creation failed");
    }

    const std::uint32_t workerCount = OsGetNumProcessors() * 2U;
    for (std::uint32_t index = 0; index < workerCount; ++index) {
        IoThread* worker = OsThreadCreate(
            ThreadPoolWorkProc,
            this,
            "ThreadPoolWorkThread");
        if (!worker) {
            runtime::logging::Write("authgated", runtime::logging::Level::Error,
                "[threads] Cannot start a pool worker. Pool initialization continues with fewer workers; no replacement is scheduled here.",
                __FILE__);
        }
        authgated::platform::AdoptThread(runtime, worker);
    }
}

// AuthGateD.exe 0x004138C0, ThreadPool.cpp.
ThreadPool::~ThreadPool() {
    void* const runtime = mhEvent;
    authgated::platform::SignalThreadPoolStop(runtime);
    authgated::platform::JoinIoThread(mpTimerThread);
    FinalizeWorkThreads();
    authgated::platform::DestroyThreadPoolRuntime(runtime);
    mpTimerThread = nullptr;
    mhTimer = nullptr;
    mhEvent = nullptr;

    runtime::completion_port::Close(mhIOCP);
    mhIOCP = nullptr;

    for (const auto& entry : mTimerMap) {
        delete entry.second;
    }
    mTimerMap.clear();
    for (Slot& slot : mFreeItemsSlot) {
        TimerItem* item = slot.mpTimerItem;
        slot.mpTimerItem = nullptr;
        while (item) {
            TimerItem* const next = item->pNextItem;
            delete item;
            item = next;
        }
    }
}
