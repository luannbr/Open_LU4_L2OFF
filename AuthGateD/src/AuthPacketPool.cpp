#include "AuthPacketPool.h"

#include "Shared/src/ScopedLockGuards.h"

CAuthPacketPool* CAuthPacketPool::g_AuthPacketPool = nullptr;

CAuthPacketPool::CSlot::CSlot() : m_lock(u"CAuthPacketPool::CSlot::m_lock", true) {}

// AuthGateD.exe 0x00405B80, AuthPacketPool.cpp.
CAuthPacketPool::CAuthPacketPool() {
    if (!g_AuthPacketPool)
        g_AuthPacketPool = this;
    m_iAlloc.store(-1, std::memory_order_relaxed);
    m_iFree.store(0, std::memory_order_relaxed);
    RegisterMonitoringObjects();
}

// AuthGateD.exe 0x00405840, AuthPacketPool.cpp.
CAuthPacketPool::~CAuthPacketPool() {
    FreeAll();
    if (g_AuthPacketPool == this) {
        g_AuthPacketPool = nullptr;
    }
}

// AuthGateD.exe 0x00405510, AuthPacketPool.cpp.
CAuthPacket* CAuthPacketPool::Alloc() {
    const std::uint32_t index = static_cast<std::uint32_t>(m_iAlloc.fetch_add(1, std::memory_order_relaxed) + 1) & 0x0fU;
    CSlot& slot = m_slot[index];
    CAuthPacket* packet = nullptr;
    {
        L2_GUARD(slot.m_lock);
        packet = slot.m_pPacket;
        if (packet) {
            slot.m_pPacket = reinterpret_cast<CAuthPacket*>(packet->m_pSocket);
        }
    }

    if (!packet) {
        packet = new CAuthPacket;
        m_monAuthPacket.iValue.fetch_add(1, std::memory_order_relaxed);
    }
    m_monActiveAuthPacket.iValue.fetch_add(1, std::memory_order_relaxed);
    return packet;
}

// AuthGateD.exe 0x004055C0, AuthPacketPool.cpp (folded pool body).
void CAuthPacketPool::Free(CAuthPacket* packet) {
    const std::uint32_t index = static_cast<std::uint32_t>(m_iFree.fetch_add(1, std::memory_order_relaxed) + 1) & 0x0fU;
    CSlot& slot = m_slot[index];
    {
        L2_GUARD(slot.m_lock);
        packet->m_pSocket = reinterpret_cast<CAuthSocket*>(slot.m_pPacket);
        slot.m_pPacket = packet;
    }
    m_monActiveAuthPacket.iValue.fetch_sub(1, std::memory_order_relaxed);
}

// AuthGateD.exe 0x00407310, AuthPacketPool.cpp (folded pool body).
void CAuthPacketPool::FreeAll() {
    for (CSlot& slot : m_slot) {
        CAuthPacket* packet = nullptr;
        {
            L2_GUARD(slot.m_lock);
            packet = slot.m_pPacket;
            slot.m_pPacket = nullptr;
        }
        while (packet) {
            CAuthPacket* const next = reinterpret_cast<CAuthPacket*>(packet->m_pSocket);
            delete packet;
            packet = next;
        }
    }
}

// AuthGateD.exe 0x00405B20, AuthPacketPool.cpp.
void CAuthPacketPool::RegisterMonitoringObjects() {
    m_monAuthPacket.strName = u"AuthPacket";
    m_monActiveAuthPacket.strName = u"ActiveAuthPacket";
    if (CStateMonMgr::g_pStateMonMgr) {
        CStateMonMgr::g_pStateMonMgr->Register(&m_monAuthPacket);
        CStateMonMgr::g_pStateMonMgr->Register(&m_monActiveAuthPacket);
    }
}
