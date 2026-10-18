#include "Memory.h"
#include "Shared/src/MemoryBackend.h"

#include "Shared/src/Runtime/RawAtomic.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <Windows.h>
#endif

namespace {

constexpr std::size_t kMemoryShardCount = 16;
constexpr unsigned int kSmallBlockThreshold = 0x3f8;
constexpr unsigned int kSmallBlockAlignment = 0x10;

#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
constexpr int kSmallBlockClassCount = 64;
constexpr std::size_t kSmallBlockRegionSize = 0x200000;
constexpr std::size_t kSmallBlockGroupSize = 0x10000;
constexpr std::size_t kSmallBlockPageSize = 0x2000;
constexpr std::uint32_t kAllGroupsUncommitted = UINT32_MAX;

Memory::tagEntry* FreeListHead(
    Memory::tagGroup* group,
    int sizeClass) noexcept {
    auto* const list = &group->listHead[sizeClass];
    return reinterpret_cast<Memory::tagEntry*>(
        reinterpret_cast<unsigned char*>(list) -
        offsetof(Memory::tagEntry, pEntryNext));
}

int SizeClass(std::uint32_t blockSize) noexcept {
    int sizeClass =
        static_cast<int>(blockSize >> 4U) - 1;
    if (sizeClass > 0x3f) {
        sizeClass = 0x3f;
    }
    return sizeClass;
}

std::uint32_t SizeClassBit(int sizeClass) noexcept {
    return UINT32_C(0x80000000) >>
        (static_cast<unsigned int>(sizeClass) & 0x1fU);
}

void RemoveAvailableSizeClass(
    Memory::tagHeader* header,
    Memory::tagRegion* region,
    int groupIndex,
    int sizeClass) noexcept {
    const std::uint32_t bit = SizeClassBit(sizeClass);
    if (sizeClass < 0x20) {
        region->bitvGroupHi[groupIndex] &= ~bit;
        if (--region->cntRegionSize[sizeClass] == 0) {
            header->bitvEntryHi &= ~bit;
        }
    } else {
        region->bitvGroupLo[groupIndex] &= ~bit;
        if (--region->cntRegionSize[sizeClass] == 0) {
            header->bitvEntryLo &= ~bit;
        }
    }
}

void AddAvailableSizeClass(
    Memory::tagHeader* header,
    Memory::tagRegion* region,
    int groupIndex,
    int sizeClass) noexcept {
    const std::uint32_t bit = SizeClassBit(sizeClass);
    char& regionCount =
        region->cntRegionSize[sizeClass];
    const char previousCount = regionCount++;
    if (sizeClass < 0x20) {
        if (previousCount == 0) {
            header->bitvEntryHi |= bit;
        }
        region->bitvGroupHi[groupIndex] |= bit;
    } else {
        if (previousCount == 0) {
            header->bitvEntryLo |= bit;
        }
        region->bitvGroupLo[groupIndex] |= bit;
    }
}

int FirstMostSignificantBit(std::uint32_t bits) noexcept {
    int index = 0;
    while ((bits & UINT32_C(0x80000000)) == 0) {
        bits <<= 1U;
        ++index;
    }
    return index;
}
#endif

// HeapCreate/HeapAlloc/HeapFree are the only OS-facing part of AuthD's
// private allocator. The native portable heap state carries no
// product state: shard selection and the owner word remain in Memory itself.
#if !defined(_WIN32) || INTPTR_MAX != INT32_MAX
struct PortableHeapState {};
#endif

std::array<Memory*, kMemoryShardCount> g_pMemory{};
// Used only by the legacy memory backend.
[[maybe_unused]] volatile std::int32_t g_nMemoryCount = 0;

#if L2_MEMORY_BACKEND_LEGACY
MemoryInit theMemoryInit;
#endif

} // namespace

// L2Authd.exe 0x0046A400, Src/memory.cpp:39-43.
Memory::Memory() {
    authd::platform::InitializeCriticalSection(_lock);
    __sbh_indGroupDefer = 0;
    [[maybe_unused]] const int initialized = _heap_init();
}

// L2Authd.exe 0x0046A430, Src/memory.cpp:46-49.
Memory::~Memory() {
    _heap_term();
    authd::platform::DeleteCriticalSection(_lock);
}

// L2Authd.exe 0x0046A450, Src/memory.cpp:112-120. The Win32 target stores a
// four-byte Memory* immediately before the returned block. Native x64 keeps
// the same owner-word relation using its native pointer width.
void* Memory::_malloc(unsigned int size) noexcept {
#if L2_MEMORY_BACKEND_LEGACY
    const std::int32_t shardSequence =
        runtime::raw_atomic::IncrementI32(
            &g_nMemoryCount);
    Memory* const pMemory = g_pMemory[
        static_cast<std::uint32_t>(shardSequence) &
        (kMemoryShardCount - 1)];
    if (pMemory == nullptr) {
        return nullptr;
    }

    const unsigned int allocationSize =
        size + static_cast<unsigned int>(sizeof(Memory*));
    auto** const ppMemory = static_cast<Memory**>(
        pMemory->_heap_alloc_base(allocationSize));
    if (ppMemory == nullptr) {
        return nullptr;
    }
    *ppMemory = pMemory;
    return ppMemory + 1;
#else
    return std::malloc(size);
#endif
}

// L2Authd.exe 0x0046A4A0, Src/memory.cpp:511-527.
void Memory::_free(void* pBlock) noexcept {
#if L2_MEMORY_BACKEND_LEGACY
    if (pBlock == nullptr) {
        return;
    }

    auto** const ppMemory =
        static_cast<Memory**>(pBlock) - 1;
    Memory* const pMemory = *ppMemory;
    // The shipping image writes through address zero when the
    // owner word does not identify one of its initialized 0x3f8 heaps. Abort
    // is the portable fail-fast boundary for that already-invalid call.
    if (pMemory == nullptr ||
        pMemory->__sbh_threshold != kSmallBlockThreshold) {
        std::abort();
    }
    *ppMemory = nullptr;
    pMemory->_free_base(ppMemory);
#else
    std::free(pBlock);
#endif
}

// L2Authd.exe 0x0046A500, Src/memory.cpp:52-66.
int Memory::_heap_init() noexcept {
#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    _crtheap = ::HeapCreate(
        HEAP_NO_SERIALIZE,
        0x2000,
        0);
#else
    _crtheap = new (std::nothrow) PortableHeapState;
#endif
    if (_crtheap == nullptr) {
        return 0;
    }

    if (__sbh_heap_init(kSmallBlockThreshold) == 0) {
#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
        ::HeapDestroy(static_cast<HANDLE>(_crtheap));
#else
        delete static_cast<PortableHeapState*>(_crtheap);
#endif
        _crtheap = nullptr;
        return 0;
    }
    return 1;
}

// L2Authd.exe 0x0046A560, Src/memory.cpp:69-95.
void Memory::_heap_term() noexcept {
#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    if (_crtheap == nullptr) {
        return;
    }

    tagHeader* header = __sbh_pHeaderList;
    for (int index = 0;
         index < __sbh_cntHeaderList;
         ++index, ++header) {
        ::VirtualFree(
            header->pHeapData,
            kSmallBlockRegionSize,
            MEM_DECOMMIT);
        ::VirtualFree(
            header->pHeapData,
            0,
            MEM_RELEASE);
        ::HeapFree(
            static_cast<HANDLE>(_crtheap),
            0,
            header->pRegion);
    }
    ::HeapFree(
        static_cast<HANDLE>(_crtheap),
        0,
        __sbh_pHeaderList);
    ::HeapDestroy(static_cast<HANDLE>(_crtheap));
#else
    delete static_cast<PortableHeapState*>(_crtheap);
    _crtheap = nullptr;
#endif
}

// L2Authd.exe 0x0046A610, Src/memory.cpp:98-109.
int Memory::__sbh_heap_init(
    unsigned int threshold) noexcept {
#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    __sbh_pHeaderList = static_cast<tagHeader*>(
        ::HeapAlloc(
            static_cast<HANDLE>(_crtheap),
            0,
            0x140));
    if (__sbh_pHeaderList == nullptr) {
        return 0;
    }

    __sbh_threshold = threshold;
    __sbh_pHeaderScan = __sbh_pHeaderList;
    __sbh_pHeaderDefer = nullptr;
    __sbh_cntHeaderList = 0;
    __sbh_sizeHeaderList = 0x10;
#else
    __sbh_threshold = threshold;
    __sbh_pHeaderList = nullptr;
    __sbh_pHeaderScan = nullptr;
    __sbh_pHeaderDefer = nullptr;
    __sbh_cntHeaderList = 0;
    __sbh_sizeHeaderList = 0;
#endif
    return 1;
}

// L2Authd.exe 0x0046A680, Src/memory.cpp:123-141.
void* Memory::_heap_alloc_base(unsigned int size) noexcept {
    if (_crtheap == nullptr) {
        return nullptr;
    }

#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    if (size <= __sbh_threshold) {
        void* smallBlock;
        {
            L2_GUARD(*this);
            smallBlock =
                __sbh_alloc_block(static_cast<int>(size));
        }
        if (smallBlock != nullptr) {
            return smallBlock;
        }
    }

    void* allocation;
    {
        L2_GUARD(*this);
        allocation = ::HeapAlloc(
            static_cast<HANDLE>(_crtheap),
            0,
            (size + 0x0fU) & UINT32_C(0xfffffff0));
    }
    return allocation;
#else
    // The copied VS2013 x86 SBH embeds two four-byte links in its minimum
    // 16-byte block and cannot keep that representation after pointers
    // widen. The host heap replaces only that allocator transport. The
    // sixteen-way shard and owner-word behavior stays above this boundary.
    void* allocation;
    {
        L2_GUARD(*this);
        const std::size_t roundedSize =
            (static_cast<std::size_t>(size) +
             (kSmallBlockAlignment - 1U)) &
            ~std::size_t{kSmallBlockAlignment - 1U};
        allocation = std::malloc(roundedSize);
    }
    return allocation;
#endif
}

// L2Authd.exe 0x0046A700, Src/memory.cpp:144-376.
void* Memory::__sbh_alloc_block(
    [[maybe_unused]] int size) noexcept {
#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    tagHeader* const headerEnd =
        __sbh_pHeaderList + __sbh_cntHeaderList;
    const std::uint32_t blockSize =
        (static_cast<std::uint32_t>(size) + 0x17U) &
        UINT32_C(0xfffffff0);
    const int minimumSizeClass = SizeClass(blockSize);

    std::uint32_t highMask;
    std::uint32_t lowMask;
    if (minimumSizeClass < 0x20) {
        highMask = UINT32_MAX >> minimumSizeClass;
        lowMask = UINT32_MAX;
    } else {
        highMask = 0;
        lowMask = UINT32_MAX >>
            (minimumSizeClass - 0x20);
    }

    tagHeader* header = __sbh_pHeaderScan;
    while (header < headerEnd &&
           (highMask & header->bitvEntryHi) == 0 &&
           (lowMask & header->bitvEntryLo) == 0) {
        ++header;
    }

    if (header == headerEnd) {
        header = __sbh_pHeaderList;
        while (header < __sbh_pHeaderScan &&
               (highMask & header->bitvEntryHi) == 0 &&
               (lowMask & header->bitvEntryLo) == 0) {
            ++header;
        }

        if (header == __sbh_pHeaderScan) {
            while (header < headerEnd &&
                   header->bitvCommit == 0) {
                ++header;
            }
            if (header == headerEnd) {
                header = __sbh_pHeaderList;
                while (header < __sbh_pHeaderScan &&
                       header->bitvCommit == 0) {
                    ++header;
                }
                if (header == __sbh_pHeaderScan) {
                    header = __sbh_alloc_new_region();
                    if (header == nullptr) {
                        return nullptr;
                    }
                }
            }

            const int groupIndex =
                __sbh_alloc_new_group(header);
            header->pRegion->indGroupUse = groupIndex;
            if (groupIndex == -1) {
                return nullptr;
            }
        }
    }

    __sbh_pHeaderScan = header;
    tagRegion* const region = header->pRegion;
    int groupIndex = region->indGroupUse;
    if (groupIndex == -1 ||
        ((highMask & region->bitvGroupHi[groupIndex]) == 0 &&
         (lowMask & region->bitvGroupLo[groupIndex]) == 0)) {
        groupIndex = 0;
        while ((highMask & region->bitvGroupHi[groupIndex]) == 0 &&
               (lowMask & region->bitvGroupLo[groupIndex]) == 0) {
            ++groupIndex;
        }
    }

    tagGroup* const group =
        &region->grpHeadList[groupIndex];
    std::uint32_t available =
        highMask & region->bitvGroupHi[groupIndex];
    int sizeClassOffset = 0;
    if (available == 0) {
        sizeClassOffset = 0x20;
        available =
            lowMask & region->bitvGroupLo[groupIndex];
    }
    const int sizeClass =
        sizeClassOffset + FirstMostSignificantBit(available);

    tagEntry* const freeEntry =
        group->listHead[sizeClass].pEntryNext;
    const std::uint32_t remainingSize =
        static_cast<std::uint32_t>(freeEntry->sizeFront) -
        blockSize;
    const int remainingSizeClass =
        SizeClass(remainingSize);

    if (remainingSizeClass != sizeClass) {
        if (freeEntry->pEntryNext ==
            freeEntry->pEntryPrev) {
            RemoveAvailableSizeClass(
                header,
                region,
                groupIndex,
                sizeClass);
        }

        freeEntry->pEntryPrev->pEntryNext =
            freeEntry->pEntryNext;
        freeEntry->pEntryNext->pEntryPrev =
            freeEntry->pEntryPrev;

        if (remainingSize != 0) {
            tagEntry* const listHead =
                FreeListHead(
                    group,
                    remainingSizeClass);
            freeEntry->pEntryNext =
                listHead->pEntryNext;
            freeEntry->pEntryPrev = listHead;
            listHead->pEntryNext = freeEntry;
            freeEntry->pEntryNext->pEntryPrev =
                freeEntry;
            if (freeEntry->pEntryNext ==
                freeEntry->pEntryPrev) {
                AddAvailableSizeClass(
                    header,
                    region,
                    groupIndex,
                    remainingSizeClass);
            }
        }
    }

    auto* const freeBytes =
        reinterpret_cast<unsigned char*>(freeEntry);
    if (remainingSize != 0) {
        freeEntry->sizeFront =
            static_cast<int>(remainingSize);
        *reinterpret_cast<std::uint32_t*>(
            freeBytes + remainingSize - 4U) =
            remainingSize;
    }

    auto* const allocatedHeader =
        reinterpret_cast<std::uint32_t*>(
            freeBytes + remainingSize);
    *allocatedHeader = blockSize + 1U;
    *reinterpret_cast<std::uint32_t*>(
        reinterpret_cast<unsigned char*>(allocatedHeader) +
        blockSize - 4U) = blockSize + 1U;

    const int previousEntryCount = group->cntEntries++;
    if (previousEntryCount == 0 &&
        header == __sbh_pHeaderDefer &&
        groupIndex == __sbh_indGroupDefer) {
        __sbh_pHeaderDefer = nullptr;
    }
    region->indGroupUse = groupIndex;
    return allocatedHeader + 1;
#else
    return nullptr;
#endif
}

// L2Authd.exe 0x0046AC30, Src/memory.cpp:379-424.
Memory::tagHeader*
Memory::__sbh_alloc_new_region() noexcept {
#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    if (__sbh_cntHeaderList ==
        __sbh_sizeHeaderList) {
        tagHeader* const resized =
            static_cast<tagHeader*>(::HeapReAlloc(
                static_cast<HANDLE>(_crtheap),
                0,
                __sbh_pHeaderList,
                static_cast<std::size_t>(
                    __sbh_sizeHeaderList + 0x10) *
                    sizeof(tagHeader)));
        if (resized == nullptr) {
            return nullptr;
        }
        __sbh_pHeaderList = resized;
        __sbh_sizeHeaderList += 0x10;
    }

    tagHeader* const header =
        __sbh_pHeaderList + __sbh_cntHeaderList;
    header->pRegion = static_cast<tagRegion*>(
        ::HeapAlloc(
            static_cast<HANDLE>(_crtheap),
            HEAP_ZERO_MEMORY,
            sizeof(tagRegion)));
    if (header->pRegion == nullptr) {
        return nullptr;
    }

    header->pHeapData = ::VirtualAlloc(
        nullptr,
        kSmallBlockRegionSize,
        MEM_RESERVE,
        PAGE_READWRITE);
    if (header->pHeapData == nullptr) {
        ::HeapFree(
            static_cast<HANDLE>(_crtheap),
            0,
            header->pRegion);
        return nullptr;
    }

    header->bitvEntryHi = 0;
    header->bitvEntryLo = 0;
    header->bitvCommit = kAllGroupsUncommitted;
    ++__sbh_cntHeaderList;
    header->pRegion->indGroupUse = -1;
    return header;
#else
    return nullptr;
#endif
}

// L2Authd.exe 0x0046AD50, Src/memory.cpp:427-508.
int Memory::__sbh_alloc_new_group(
    [[maybe_unused]] tagHeader* header) noexcept {
#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    tagRegion* const region = header->pRegion;
    const int groupIndex =
        FirstMostSignificantBit(header->bitvCommit);
    tagGroup* const group =
        &region->grpHeadList[groupIndex];

    // The CRT represents each list head as a tagEntry whose sizeFront word
    // overlaps the previous field. Only the next/previous words are used.
    for (int sizeClass = 0;
         sizeClass < kSmallBlockClassCount - 1;
         ++sizeClass) {
        tagEntry* const listHead =
            FreeListHead(group, sizeClass);
        listHead->pEntryNext = listHead;
        listHead->pEntryPrev = listHead;
    }

    auto* const groupBase =
        static_cast<unsigned char*>(header->pHeapData) +
        static_cast<std::size_t>(groupIndex) *
            kSmallBlockGroupSize;
    if (::VirtualAlloc(
            groupBase,
            kSmallBlockGroupSize,
            MEM_COMMIT,
            PAGE_READWRITE) == nullptr) {
        return -1;
    }

    for (std::size_t pageOffset = 0;
         pageOffset <= 0xe000;
         pageOffset += kSmallBlockPageSize) {
        unsigned char* const page =
            groupBase + pageOffset;
        *reinterpret_cast<std::uint32_t*>(page + 8) =
            UINT32_MAX;
        *reinterpret_cast<std::uint32_t*>(
            page + 0x1ffc) = UINT32_MAX;

        auto* const freeEntry =
            reinterpret_cast<tagEntry*>(page + 0x0c);
        freeEntry->sizeFront = 0x1ff0;
        freeEntry->pEntryNext =
            reinterpret_cast<tagEntry*>(
                reinterpret_cast<std::uintptr_t>(page) +
                0x200cU);
        freeEntry->pEntryPrev =
            reinterpret_cast<tagEntry*>(
                reinterpret_cast<std::uintptr_t>(page) -
                0x1ff4U);
        *reinterpret_cast<std::uint32_t*>(
            page + 0x1ff8) = 0x1ff0;
    }

    tagEntry* const largestListHead =
        FreeListHead(
            group,
            kSmallBlockClassCount - 1);
    auto* const firstEntry =
        reinterpret_cast<tagEntry*>(groupBase + 0x0c);
    auto* const lastEntry =
        reinterpret_cast<tagEntry*>(groupBase + 0xe00c);
    largestListHead->pEntryNext = firstEntry;
    firstEntry->pEntryPrev = largestListHead;
    largestListHead->pEntryPrev = lastEntry;
    lastEntry->pEntryNext = largestListHead;

    region->bitvGroupHi[groupIndex] = 0;
    region->bitvGroupLo[groupIndex] = 1;
    const char previousCount =
        region->cntRegionSize[0x3f]++;
    if (previousCount == 0) {
        header->bitvEntryLo |= 1;
    }
    header->bitvCommit &=
        ~SizeClassBit(groupIndex);
    return groupIndex;
#else
    return -1;
#endif
}

// L2Authd.exe 0x0046AF70, Src/memory.cpp:530-549.
void Memory::_free_base(void* pBlock) noexcept {
    {
        L2_GUARD(*this);
#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
        tagHeader* const header =
            __sbh_find_block(pBlock);
        if (header == nullptr) {
            ::HeapFree(
                static_cast<HANDLE>(_crtheap),
                0,
                pBlock);
        } else {
            __sbh_free_block(header, pBlock);
        }
#else
        std::free(pBlock);
#endif
    }
}

// L2Authd.exe 0x0046AFD0, Src/memory.cpp:552-568.
Memory::tagHeader* Memory::__sbh_find_block(
    void* allocation) noexcept {
#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    tagHeader* header = __sbh_pHeaderList;
    tagHeader* const headerEnd =
        __sbh_pHeaderList + __sbh_cntHeaderList;
    while (header < headerEnd) {
        const std::uint32_t offset =
            static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(
                    allocation) -
                reinterpret_cast<std::uintptr_t>(
                    header->pHeapData));
        if (offset < kSmallBlockRegionSize) {
            return header;
        }
        ++header;
    }
#else
    [[maybe_unused]] void* const ignoredAllocation =
        allocation;
#endif
    return nullptr;
}

// L2Authd.exe 0x0046B030, Src/memory.cpp:571-801.
void Memory::__sbh_free_block(
    tagHeader* header,
    void* allocation) noexcept {
#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    tagRegion* const region = header->pRegion;
    const int groupIndex = static_cast<int>(
        (static_cast<unsigned char*>(allocation) -
         static_cast<unsigned char*>(header->pHeapData)) >>
        16U);
    tagGroup* const group =
        &region->grpHeadList[groupIndex];

    auto* entry = reinterpret_cast<tagEntry*>(
        static_cast<unsigned char*>(allocation) - 4U);
    std::uint32_t mergedSize =
        static_cast<std::uint32_t>(entry->sizeFront) - 1U;
    if ((mergedSize & 1U) != 0) {
        return;
    }

    auto* nextEntry = reinterpret_cast<tagEntry*>(
        reinterpret_cast<unsigned char*>(entry) +
        mergedSize);
    const std::uint32_t nextSize =
        static_cast<std::uint32_t>(
            nextEntry->sizeFront);
    const std::uint32_t previousSize =
        *reinterpret_cast<std::uint32_t*>(
            static_cast<unsigned char*>(allocation) -
            8U);

    if ((nextSize & 1U) == 0) {
        const int nextSizeClass =
            SizeClass(nextSize);
        if (nextEntry->pEntryNext ==
            nextEntry->pEntryPrev) {
            RemoveAvailableSizeClass(
                header,
                region,
                groupIndex,
                nextSizeClass);
        }
        nextEntry->pEntryPrev->pEntryNext =
            nextEntry->pEntryNext;
        nextEntry->pEntryNext->pEntryPrev =
            nextEntry->pEntryPrev;
        mergedSize += nextSize;
    }

    int mergedSizeClass = SizeClass(mergedSize);
    int previousSizeClass = 0;
    const bool previousEntryIsFree =
        (previousSize & 1U) == 0;
    if (previousEntryIsFree) {
        entry = reinterpret_cast<tagEntry*>(
            reinterpret_cast<unsigned char*>(entry) -
            previousSize);
        previousSizeClass = SizeClass(previousSize);
        mergedSize += previousSize;
        mergedSizeClass = SizeClass(mergedSize);
        if (previousSizeClass != mergedSizeClass) {
            if (entry->pEntryNext ==
                entry->pEntryPrev) {
                RemoveAvailableSizeClass(
                    header,
                    region,
                    groupIndex,
                    previousSizeClass);
            }
            entry->pEntryPrev->pEntryNext =
                entry->pEntryNext;
            entry->pEntryNext->pEntryPrev =
                entry->pEntryPrev;
        }
    }

    if (!previousEntryIsFree ||
        previousSizeClass != mergedSizeClass) {
        tagEntry* const listHead =
            FreeListHead(group, mergedSizeClass);
        entry->pEntryNext = listHead->pEntryNext;
        entry->pEntryPrev = listHead;
        listHead->pEntryNext = entry;
        entry->pEntryNext->pEntryPrev = entry;
        if (entry->pEntryNext ==
            entry->pEntryPrev) {
            AddAvailableSizeClass(
                header,
                region,
                groupIndex,
                mergedSizeClass);
        }
    }

    entry->sizeFront = static_cast<int>(mergedSize);
    *reinterpret_cast<std::uint32_t*>(
        reinterpret_cast<unsigned char*>(entry) +
        mergedSize - 4U) = mergedSize;

    --group->cntEntries;
    if (group->cntEntries != 0) {
        return;
    }

    if (__sbh_pHeaderDefer != nullptr) {
        tagHeader* const deferredHeader =
            __sbh_pHeaderDefer;
        tagRegion* const deferredRegion =
            deferredHeader->pRegion;
        auto* const deferredGroupBase =
            static_cast<unsigned char*>(
                deferredHeader->pHeapData) +
            static_cast<std::size_t>(
                __sbh_indGroupDefer) *
                kSmallBlockGroupSize;
        ::VirtualFree(
            deferredGroupBase,
            kSmallBlockGroupSize,
            MEM_DECOMMIT);
        deferredHeader->bitvCommit |=
            SizeClassBit(__sbh_indGroupDefer);
        deferredRegion->bitvGroupLo[
            __sbh_indGroupDefer] = 0;
        if (--deferredRegion->cntRegionSize[0x3f] ==
            0) {
            deferredHeader->bitvEntryLo &=
                UINT32_C(0xfffffffe);
        }

        if (deferredHeader->bitvCommit ==
            kAllGroupsUncommitted) {
            ::VirtualFree(
                deferredHeader->pHeapData,
                0,
                MEM_RELEASE);
            ::HeapFree(
                static_cast<HANDLE>(_crtheap),
                0,
                deferredHeader->pRegion);

            tagHeader* const source =
                deferredHeader + 1;
            tagHeader* const end =
                __sbh_pHeaderList +
                __sbh_cntHeaderList;
            std::memmove(
                deferredHeader,
                source,
                static_cast<std::size_t>(
                    end - source) *
                    sizeof(tagHeader));
            --__sbh_cntHeaderList;
            if (deferredHeader < header) {
                --header;
            }
            __sbh_pHeaderScan =
                __sbh_pHeaderList;
        }
    }

    __sbh_pHeaderDefer = header;
    __sbh_indGroupDefer = groupIndex;
#else
    [[maybe_unused]] tagHeader* const ignoredHeader =
        header;
    [[maybe_unused]] void* const ignoredAllocation =
        allocation;
#endif
}

// L2Authd.exe 0x0046B780, Src/memory.h:106.
void Memory::_mlock() {
    authd::platform::EnterCriticalSection(_lock);
}

// L2Authd.exe 0x0046B7A0, Src/memory.h:107.
void Memory::_munlock() noexcept {
    authd::platform::LeaveCriticalSection(_lock);
}

// L2Authd.exe 0x0046B610, Src/memory.cpp:26-30.
MemoryInit::MemoryInit() {
    for (std::size_t i = 0; i < kMemoryShardCount; ++i) {
        g_pMemory[i] = new Memory;
    }
}

// L2Authd.exe 0x0046B6C0, Src/memory.cpp:32-35.
MemoryInit::~MemoryInit() {
    for (std::size_t i = 0; i < kMemoryShardCount; ++i) {
        delete g_pMemory[i];
        g_pMemory[i] = nullptr;
    }
}
