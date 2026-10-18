#include "IOBuffer.h"

#include "IOBufferPool.h"

CIOBuffer* CIOBuffer::Alloc() {
    return CIOBufferPool::g_IOBufferPool
        ? CIOBufferPool::g_IOBufferPool->Alloc()
        : nullptr;
}

void CIOBuffer::Free() {
    if (CIOBufferPool::g_IOBufferPool) {
        CIOBufferPool::g_IOBufferPool->Free(this);
    } else {
        delete this;
    }
}
