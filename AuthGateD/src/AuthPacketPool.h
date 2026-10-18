#pragma once

#include "AuthPacket.h"
#include "Lock.h"
#include "StateMonMgr.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

class CAuthPacketPool {
public:
    class CSlot {
    public:
        CSlot();

        CAuthPacket* m_pPacket = nullptr;
        CLock m_lock;
    };

    CAuthPacketPool();
    virtual ~CAuthPacketPool();

    CAuthPacketPool(const CAuthPacketPool&) = delete;
    CAuthPacketPool& operator=(const CAuthPacketPool&) = delete;

    CAuthPacket* Alloc();
    void Free(CAuthPacket* packet);
    void FreeAll();
    virtual void RegisterMonitoringObjects();

    static CAuthPacketPool* g_AuthPacketPool;

private:
    static constexpr std::size_t kSlotCount = 16;

    std::array<CSlot, kSlotCount> m_slot;
    std::atomic<std::int32_t> m_iAlloc{-1};
    std::atomic<std::int32_t> m_iFree{0};
    MonObj m_monAuthPacket;
    MonObj m_monActiveAuthPacket;
};
