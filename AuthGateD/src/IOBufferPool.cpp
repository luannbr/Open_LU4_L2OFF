#include "IOBufferPool.h"

#include "Shared/src/ScopedLockGuards.h"

#include <cstdint>

CIOBufferPool* CIOBufferPool::g_IOBufferPool = nullptr;

CIOBufferPool::CSlot::CSlot()
    : m_lock(u"CIOBufferPool::CSlot::m_lock", true) {}

CIOBufferPool::CIOBufferPool() {
    if (!g_IOBufferPool) g_IOBufferPool = this;
    RegisterMonitoringObjects();
}

CIOBufferPool::~CIOBufferPool() {
    FreeAll();
    if (g_IOBufferPool == this) g_IOBufferPool = nullptr;
}

CIOBuffer* CIOBufferPool::Alloc() {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            m_iAlloc.fetch_add(1, std::memory_order_relaxed) + 1) &
        0x0fU;
    CSlot& slot = m_slot[slotIndex];
    CIOBuffer* buffer = nullptr;
    {
        L2_GUARD(slot.m_lock);
        buffer = slot.m_pBuffer;
        if (buffer) slot.m_pBuffer = buffer->m_pNext;
    }

    if (!buffer) {
        buffer = new CIOBuffer;
        m_monIOBuffer.iValue.fetch_add(1, std::memory_order_relaxed);
    }
    m_monActiveIOBuffer.iValue.fetch_add(1, std::memory_order_relaxed);
    buffer->m_size = 0;
    buffer->m_pNext = nullptr;
    buffer->m_ref.store(1, std::memory_order_relaxed);
    return buffer;
}

void CIOBufferPool::Free(CIOBuffer* buffer) {
    if (!buffer) return;
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            m_iFree.fetch_add(1, std::memory_order_relaxed) + 1) &
        0x0fU;
    CSlot& slot = m_slot[slotIndex];
    {
        L2_GUARD(slot.m_lock);
        buffer->m_pNext = slot.m_pBuffer;
        slot.m_pBuffer = buffer;
    }
    m_monActiveIOBuffer.iValue.fetch_sub(1, std::memory_order_relaxed);
}

void CIOBufferPool::FreeAll() {
    for (CSlot& slot : m_slot) {
        CIOBuffer* current = nullptr;
        {
            L2_GUARD(slot.m_lock);
            current = slot.m_pBuffer;
            slot.m_pBuffer = nullptr;
        }
        while (current) {
            CIOBuffer* const next = current->m_pNext;
            delete current;
            current = next;
        }
    }
}

void CIOBufferPool::RegisterMonitoringObjects() {
    m_monIOBuffer.strName = u"IOBuffer";
    m_monActiveIOBuffer.strName = u"ActiveIOBuffer";
    if (CStateMonMgr::g_pStateMonMgr) {
        CStateMonMgr::g_pStateMonMgr->Register(&m_monIOBuffer);
        CStateMonMgr::g_pStateMonMgr->Register(&m_monActiveIOBuffer);
    }
}
