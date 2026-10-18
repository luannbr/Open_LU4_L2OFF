#pragma once

#include "Lock.h"

#include "Shared/src/Runtime/RawAtomic.h"

#include <cstddef>
#include <cstdint>

class CIOBuffer;

// Original owner: Src/iobuffer.h. L2Authd.pdb type 0x54A4 is an all-static
// interface (sizeof 1). Its nested Slot is 0x24 bytes in the selected Win32 ABI.
class CIOBufferPool {
public:
    class Slot {
    public:
        Slot();
        ~Slot() = default;

        CIOBuffer* m_pBuffer;
        CLock m_lock;
    };

    ~CIOBufferPool();

    static Slot m_slot[16];
    static std::int32_t m_alloc;
    static std::int32_t m_free;
};

// Original owner: Src/iobuffer.h. The selected PDB records a 0x2010-byte
// Win32 object: vptr + 8192-byte payload + size/next/ref fields. Native host
// pointers keep their native width in the portable object.
class CIOBuffer {
public:
    CIOBuffer();
    virtual ~CIOBuffer();

    static CIOBuffer* Alloc();
    static void FreeAll();

    // These two one-line bodies are compiled from Src/iobuffer.h:29-30.
    void AddRef() {
        runtime::raw_atomic::IncrementI32(&m_ref);
    }

    void Release() {
        if (runtime::raw_atomic::DecrementI32(&m_ref) == 0) {
            Free();
        }
    }

    void Free();

    char m_buffer[8192];
    std::uint32_t m_size;
    CIOBuffer* m_pNext;
    std::int32_t m_ref;
};

extern CIOBufferPool g_bufferPool;
