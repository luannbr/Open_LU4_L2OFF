#pragma once

#include "Lock.h"
#include "Thread.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>

struct TimerItem {
    using Callback = void (*)(void* parameter);

    Callback callback = nullptr;
    void* pParam = nullptr;
    TimerItem* pNextItem = nullptr;
};

class ThreadPool {
public:
    class Slot {
    public:
        Slot();
        ~Slot() = default;

        TimerItem* mpTimerItem = nullptr;
        CLock mLock;
    };

    explicit ThreadPool(std::int32_t maxItems);
    virtual ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    bool QueueTimerWorkItem(
        TimerItem::Callback callback,
        void* parameter,
        std::uint32_t delayMilliseconds);

    static std::uint32_t ThreadPoolWorkProc(void* parameter);
    static std::uint32_t ThreadPoolTimerProc(void* parameter);

private:
    TimerItem* AllocTimerItem();
    void FreeTimerItem(TimerItem* item);
    void FinalizeWorkThreads();
    void SetTimerNextTime();

    std::array<Slot, 16> mFreeItemsSlot;
    std::atomic<std::int32_t> mNumAllocItems{0};
    std::atomic<std::int32_t> mNumFreeItems{0};
    std::multimap<std::int64_t, TimerItem*> mTimerMap;
    CLock mTimerLock;
    void* mhTimer = nullptr;
    void* mhEvent = nullptr;
    std::int64_t mNextDueTime = 0;
    std::int32_t mMaxItems = 0;
    std::atomic<std::int32_t> mNumWorkThreads{0};
    void* mhIOCP = nullptr;
    IoThread* mpTimerThread = nullptr;
};
