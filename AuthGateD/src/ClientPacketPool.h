#pragma once

#include "ClientPacket.h"
#include "Lock.h"
#include "StateMonMgr.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

class CClientPacketPool {
public:
    class CSlot {
    public:
        CSlot();

        CClientPacket* m_pPacket = nullptr;
        CLock m_lock;
    };

    CClientPacketPool();
    virtual ~CClientPacketPool();

    CClientPacketPool(const CClientPacketPool&) = delete;
    CClientPacketPool& operator=(const CClientPacketPool&) = delete;

    CClientPacket* Alloc();
    void Free(CClientPacket* packet);
    void FreeAll();
    virtual void RegisterMonitoringObjects();

    static CClientPacketPool* g_ClientPacketPool;

private:
    static constexpr std::size_t kSlotCount = 16;

    std::array<CSlot, kSlotCount> m_slot;
    std::atomic<std::int32_t> m_iAlloc{-1};
    std::atomic<std::int32_t> m_iFree{0};
    MonObj m_monClientPacket;
    MonObj m_monActiveClientPacket;
};

