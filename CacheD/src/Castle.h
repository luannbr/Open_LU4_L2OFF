#pragma once

#include "WareHouse.h"

#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/Obj.h"
#include "Shared/src/RWLock.h"

#include <cstddef>
#include <cstdint>

class CCastle;
using CCastleSP = CSPointer<CCastle>;


// Shipping owner: CacheD/CastleDb.{h,cpp}. The current object is the
// 0x18-byte Shared MemoryObject, one warehouse storage, one RWLock and the
// castle database identifier at +0x58.
class CCastle : public MemoryObject {
public:
    explicit CCastle(int castleId);
    ~CCastle() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    CWareHouseSP GetWareHouse();
    void SetCheckSumOnDB();
    void SetLoadTime();
    void SetUnloadTime();
    void ItemCheckOnLoad();
    void ItemCheckOnUnload();

    int GetId() const noexcept;

private:

    void OnItemCheckFailed(int checkFailType);

    L2_DECLARE_OBJECT_REGISTRATION(CCastle)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CCastle)

    CObjectStorage<CWareHouse> m_warehouse; // +0x18
    RWLock m_lock; // +0x20
    int m_castleId; // +0x58
    [[maybe_unused]] std::uint32_t m_targetPadding; // +0x5c
};
