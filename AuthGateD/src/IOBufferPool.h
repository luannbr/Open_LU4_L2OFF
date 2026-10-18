#pragma once

#include "IOBuffer.h"
#include "Lock.h"
#include "StateMonMgr.h"

#include <array>
#include <atomic>
#include <cstddef>

class CIOBufferPool {
public:
    class CSlot {
    public:
        CSlot();

        CIOBuffer* m_pBuffer = nullptr;
        CLock m_lock;
    };

    CIOBufferPool();
    virtual ~CIOBufferPool();

    CIOBufferPool(const CIOBufferPool&) = delete;
    CIOBufferPool& operator=(const CIOBufferPool&) = delete;

    CIOBuffer* Alloc();
    void Free(CIOBuffer* buffer);
    void FreeAll();
    virtual void RegisterMonitoringObjects();

    static CIOBufferPool* g_IOBufferPool;

private:
    static constexpr std::size_t kSlotCount = 16;

    std::array<CSlot, kSlotCount> m_slot;
    std::atomic<std::int32_t> m_iAlloc{-1};
    std::atomic<std::int32_t> m_iFree{0};
    MonObj m_monIOBuffer;
    MonObj m_monActiveIOBuffer;
};
