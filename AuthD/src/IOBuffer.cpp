#include "IOBuffer.h"
#include "Shared/src/ScopedLockGuards.h"

CIOBufferPool::Slot CIOBufferPool::m_slot[16];
std::int32_t CIOBufferPool::m_alloc = -1;
std::int32_t CIOBufferPool::m_free = 0;
CIOBufferPool g_bufferPool;

// L2Authd.exe 0x00447490, Src/iobuffer.cpp:15-16.
CIOBuffer::CIOBuffer() = default;

// L2Authd.exe 0x004474B0, Src/iobuffer.cpp:19-20.
CIOBuffer::~CIOBuffer() = default;

// L2Authd.exe 0x004474D0, Src/iobuffer.cpp:29-47.
CIOBuffer* CIOBuffer::Alloc() {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            runtime::raw_atomic::IncrementI32(
                &CIOBufferPool::m_alloc)) &
        0x0fU;
    CIOBufferPool::Slot& slot =
        CIOBufferPool::m_slot[slotIndex];

    CIOBuffer* buffer = nullptr;
    {
        L2_GUARD(slot.m_lock);
        buffer = slot.m_pBuffer;
        if (buffer != nullptr) {
            slot.m_pBuffer = buffer->m_pNext;
        }
    }
    if (buffer == nullptr) {
        buffer = new CIOBuffer;
    }

    buffer->m_size = 0;
    buffer->m_ref = 1;
    buffer->m_pNext = nullptr;
    return buffer;
}

// L2Authd.exe 0x004475D0, Src/iobuffer.cpp:60-71.
void CIOBuffer::FreeAll() {
    for (CIOBufferPool::Slot& slot : CIOBufferPool::m_slot) {
        {
            L2_GUARD(slot.m_lock);
            while (slot.m_pBuffer != nullptr) {
                CIOBuffer* buffer = slot.m_pBuffer;
                slot.m_pBuffer = buffer->m_pNext;
                delete buffer;
            }
        }
    }
}

// L2Authd.exe 0x00447670, Src/iobuffer.cpp:50-57.
void CIOBuffer::Free() {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            runtime::raw_atomic::DecrementI32(
                &CIOBufferPool::m_free)) &
        0x0fU;
    CIOBufferPool::Slot& slot =
        CIOBufferPool::m_slot[slotIndex];

    {
        L2_GUARD(slot.m_lock);
        m_pNext = slot.m_pBuffer;
        slot.m_pBuffer = this;
    }
}

// L2Authd.exe 0x004476D0, Src/iobuffer.h:43.
CIOBufferPool::Slot::Slot()
    : m_pBuffer(nullptr),
      m_lock(eCustomSpinLock, 0) {}

// L2Authd.exe 0x00447700, Src/iobuffer.h:48.
CIOBufferPool::~CIOBufferPool() {
    CIOBuffer::FreeAll();
}
