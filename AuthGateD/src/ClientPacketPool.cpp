#include "ClientPacketPool.h"

#include "Shared/src/ScopedLockGuards.h"

CClientPacketPool* CClientPacketPool::g_ClientPacketPool = nullptr;

CClientPacketPool::CSlot::CSlot() : m_lock(u"CClientPacketPool::CSlot::m_lock", true) {}

// AuthGateD.exe 0x00407490, ClientPacketPool.cpp.
CClientPacketPool::CClientPacketPool() {
    if (!g_ClientPacketPool)
        g_ClientPacketPool = this;
    m_iAlloc.store(-1, std::memory_order_relaxed);
    m_iFree.store(0, std::memory_order_relaxed);
    CClientPacketPool::RegisterMonitoringObjects();
}

// AuthGateD.exe 0x00407360, ClientPacketPool.cpp.
CClientPacketPool::~CClientPacketPool() {
    FreeAll();
    if (g_ClientPacketPool == this) {
        g_ClientPacketPool = nullptr;
    }
}

// AuthGateD.exe 0x00407260, ClientPacketPool.cpp.
CClientPacket* CClientPacketPool::Alloc() {
    const std::uint32_t index = static_cast<std::uint32_t>(m_iAlloc.fetch_add(1, std::memory_order_relaxed) + 1) & 0x0fU;
    CSlot& slot = m_slot[index];
    CClientPacket* packet = nullptr;
    {
        L2_GUARD(slot.m_lock);
        packet = slot.m_pPacket;
        if (packet) {
            slot.m_pPacket = reinterpret_cast<CClientPacket*>(packet->m_pSocket);
        }
    }
    if (!packet) {
        packet = new CClientPacket;
        m_monClientPacket.iValue.fetch_add(1, std::memory_order_relaxed);
    }
    m_monActiveClientPacket.iValue.fetch_add(1, std::memory_order_relaxed);
    return packet;
}

// AuthGateD.exe 0x004055C0, ClientPacketPool.cpp (folded pool body).
void CClientPacketPool::Free(CClientPacket* packet) {
    const std::uint32_t index = static_cast<std::uint32_t>(m_iFree.fetch_add(1, std::memory_order_relaxed) + 1) & 0x0fU;
    CSlot& slot = m_slot[index];
    {
        L2_GUARD(slot.m_lock);
        packet->m_pSocket = reinterpret_cast<CClientSocket*>(slot.m_pPacket);
        slot.m_pPacket = packet;
    }
    m_monActiveClientPacket.iValue.fetch_sub(1, std::memory_order_relaxed);
}

// AuthGateD.exe 0x00407310, ClientPacketPool.cpp.
void CClientPacketPool::FreeAll() {
    for (CSlot& slot : m_slot) {
        CClientPacket* packet = nullptr;
        {
            L2_GUARD(slot.m_lock);
            packet = slot.m_pPacket;
            slot.m_pPacket = nullptr;
        }
        while (packet) {
            CClientPacket* const next = reinterpret_cast<CClientPacket*>(packet->m_pSocket);
            delete packet;
            packet = next;
        }
    }
}

// AuthGateD.exe 0x00407430, ClientPacketPool.cpp.
void CClientPacketPool::RegisterMonitoringObjects() {
    m_monClientPacket.strName = u"ClientPacket";
    m_monActiveClientPacket.strName = u"ActiveClientPacket";
    if (CStateMonMgr::g_pStateMonMgr) {
        CStateMonMgr::g_pStateMonMgr->Register(&m_monClientPacket);
        CStateMonMgr::g_pStateMonMgr->Register(&m_monActiveClientPacket);
    }
}
