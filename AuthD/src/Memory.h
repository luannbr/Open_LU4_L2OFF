#pragma once

#include "Platform/CriticalSection.h"
#include "Shared/src/ScopedLockGuards.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>

// Original owner: Src/memory.h. This is AuthD's private sixteen-shard heap,
// not Server/Main/Shared/MemorySub.h::Memory. The selected Win32 PDB records
// type 0x5E6D as 0x38 bytes. Its small-block implementation is a private copy
// of the VS2013 x86 SBH. The portable build keeps the product-visible shard
// and allocation-owner contract while the OS heap itself is a platform
// boundary implemented in memory.cpp.
class Memory {
public:
    using BITVEC = unsigned int;

    struct tagEntry {
        int sizeFront;
        tagEntry* pEntryNext;
        tagEntry* pEntryPrev;
    };

    struct tagListHead {
        tagEntry* pEntryNext;
        tagEntry* pEntryPrev;
    };

    using LISTHEAD = tagListHead;
    using PLISTHEAD = tagListHead*;
    using ENTRY = tagEntry;
    using PENTRY = tagEntry*;

    struct tagEntryEnd {
        int sizeBack;
    };

    using ENTRYEND = tagEntryEnd;
    using PENTRYEND = tagEntryEnd*;

    struct tagGroup {
        int cntEntries;
        tagListHead listHead[64];
    };

    using GROUP = tagGroup;
    using PGROUP = tagGroup*;

    struct tagRegion {
        int indGroupUse;
        char cntRegionSize[64];
        BITVEC bitvGroupHi[32];
        BITVEC bitvGroupLo[32];
        tagGroup grpHeadList[32];
    };

    using REGION = tagRegion;
    using PREGION = tagRegion*;

    struct tagHeader {
        BITVEC bitvEntryHi;
        BITVEC bitvEntryLo;
        BITVEC bitvCommit;
        void* pHeapData;
        tagRegion* pRegion;
    };

    using HEADER = tagHeader;
    using PHEADER = tagHeader*;

    Memory();
    ~Memory();

    static void* _malloc(unsigned int size) noexcept;
    static void _free(void* pBlock) noexcept;

protected:
    // Portable guard access keeps the protected allocator lock API.
    friend struct scoped_lock_detail::OwnerAccess;

    int _heap_init() noexcept;
    void _heap_term() noexcept;
    int __sbh_heap_init(unsigned int threshold) noexcept;
    void* _heap_alloc_base(unsigned int size) noexcept;
    void* __sbh_alloc_block(int size) noexcept;
    tagHeader* __sbh_alloc_new_region() noexcept;
    int __sbh_alloc_new_group(tagHeader* header) noexcept;
    void _free_base(void* pBlock) noexcept;
    tagHeader* __sbh_find_block(void* allocation) noexcept;
    void __sbh_free_block(
        tagHeader* header,
        void* allocation) noexcept;
    void _mlock();
    void _munlock() noexcept;

    void* _crtheap = nullptr; // target +0x00
    unsigned int __sbh_threshold = 0; // target +0x04
    tagHeader* __sbh_pHeaderList = nullptr; // target +0x08
    tagHeader* __sbh_pHeaderScan = nullptr; // target +0x0C
    int __sbh_sizeHeaderList = 0; // target +0x10
    int __sbh_cntHeaderList = 0; // target +0x14
    tagHeader* __sbh_pHeaderDefer = nullptr; // target +0x18
    int __sbh_indGroupDefer = 0; // target +0x1C
    authd::platform::CriticalSectionStorage32 _lock; // target +0x20
};

// Original owner: Src/memory.cpp. PDB type 0x8464 has only a vptr and exists
// to construct and destroy the sixteen Memory instances during CRT startup.
class MemoryInit {
public:
    MemoryInit();
    virtual ~MemoryInit();
};


// Original owner: Src/memory.h. L2Authd.pdb type 0x5E88 is an empty class
// (sizeof 1) whose only members are these two static allocation functions.
// L2Authd.exe 0x0042E520/0x0042E540 call the VS CRT malloc/free directly.
// they do not call the unrelated sharded Memory::_malloc allocator.
class MemoryObject {
public:
    static void* operator new(std::size_t requestedSize) noexcept { return std::malloc(requestedSize); }

    static void operator delete(void* allocation) noexcept { std::free(allocation); }
};
