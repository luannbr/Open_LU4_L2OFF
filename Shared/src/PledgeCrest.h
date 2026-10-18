#pragma once

#include "L2Core/src/inc/XStd.h"
#include "Shared/src/Obj.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/Utils.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CPledgeCrest;
using CPledgeCrestSP = CSPointer<CPledgeCrest>;
using CPledgeCrestKey = std::CDualKey<int, int>;
using CPledgeCrestMap = CObjectMap<CPledgeCrest, 1, 5, RWLock, CPledgeCrestKey>;


// Shipping owner: Server/Main/Shared/pledgecrest.h. The current CacheD
// allocator, constructor and release body fix the complete 0xc20 layout.
class CPledgeCrest : public MemoryObject {
public:
    static constexpr int MAX_BITMAP_SIZE = 3072;

    CPledgeCrest(std::uint32_t crestId, int bitmapSize, const std::uint8_t* bitmap);
    ~CPledgeCrest() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    std::uint32_t id() const noexcept;
    int bitmapSize() const noexcept;
    const std::uint8_t* bitmap() const noexcept;

private:
#if defined(L2_BUILD_L2SERVER)
    L2_DECLARE_OBJECT_REGISTRATION(CPledgeCrest)
#else
    L2_DECLARE_OBJECT_REGISTRATION(CPledgeCrest)
#endif
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CPledgeCrest)

    std::uint32_t m_pledgeCrestId; // +0x18
    std::int32_t m_bitmapSize; // +0x1c
    std::array<std::uint8_t, MAX_BITMAP_SIZE> m_bitmap; // +0x20
};


using CPledgeEmblemKey = std::CDualKey<int, int>;
using CPledgeEmblemMap = std::map<CPledgeEmblemKey, std::u16string>;


// Shipping owner: Server/Main/Shared/PledgeCrest.{h,cpp}. Current L2Server
// accesses the 0x20-byte map at +0x00 and its CYieldLock at +0x20.
class CPledgeEmblemIDMapDB {
public:
    bool IsExist(int worldId, int emblemId);
    std::u16string* Find(int worldId, int emblemId);
    void Delete(int worldId, int emblemId);
    bool Add(int worldId, int emblemId, SharedUtilsWideChar* downloadUrl);

private:
    CPledgeEmblemMap m_PledgeEmblemMap; // +0x00
    CYieldLock m_Lock; // +0x20
};


// The shipping type is Shared. The kept process cache is instantiated by
// CacheD's PledgeCrestDb translation unit under the current world-qualified
// key contract.
extern CPledgeCrestMap g_pledgeCrests;
#if defined(L2_BUILD_L2SERVER)
extern CPledgeCrestMap g_PledgeCrestMap;
#endif
extern CPledgeEmblemIDMapDB g_PledgeEmblemIDMapDB;
