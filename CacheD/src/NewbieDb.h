#pragma once

#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/Obj.h"
#include "Shared/src/RWLock.h"

#include <cstddef>
#include <cstdint>

// The two values used by the current image. CNewbieData treats zero as the
// active-newbie state. CNewbieDB::SetExpert writes one.
enum class NEWBIE_STAT : std::int32_t {
    Newbie = 0,
    Expert = 1,
};

class CNewbieData;
class CNewbieDb;

using CNewbieDataSP = CSPointer<CNewbieData>;
using CNewbieDataMap = CObjectMap<CNewbieData, 1, 5, RWLock, int>;


// Shipping owner: CacheD/NewbieDb.{h,cpp}. The current allocator publishes
// 0x28 bytes and the four recovered methods address these three fields at
// +0x18, +0x1c and +0x20 respectively.
class CNewbieData : public MemoryObject {
public:
    CNewbieData(int accountId, int characterId, NEWBIE_STAT newbieStat);
    ~CNewbieData() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    bool IsNewbieChar(int characterId) const;
    bool SetNewbieStat(NEWBIE_STAT newbieStat);
    int GetNewbieStat() const;
    bool IsNewbieAccount() const;
    bool SetNewbieChar(int characterId);

private:

    L2_DECLARE_OBJECT_REGISTRATION(CNewbieData)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CNewbieData)

    [[maybe_unused]] std::int32_t m_accountId; // +0x18
    std::int32_t m_characterId; // +0x1c
    NEWBIE_STAT m_newbieStat; // +0x20
    // Target tail alignment, not a source field. Current construction leaves
    // these bytes untouched, so do not give them a deterministic value.
    [[maybe_unused]] std::uint32_t m_targetPadding; // +0x24
};



// Current methods use one process-global CObjectMap. LoadNewbieDataAll is
// static in the shipping call at 0x00459f7b. The remaining methods receive
// the global CNewbieDB object as their this pointer.
class CNewbieDb {
public:
    static void LoadNewbieDataAll();

    CNewbieDataSP CreateNewbieData(int accountId);
    bool UpdateNewbieCharData(int accountId, int characterId);
    CNewbieDataSP GetNewbieData(int accountId);
    bool IsNewbieUser(int accountId, int characterId);
    bool IsNewbieAccount(int accountId);
    void SetNewbieUser(int accountId, int characterId);
    void SetNewbieStat(int accountId, NEWBIE_STAT newbieStat);

private:
    static CNewbieDataMap m_newbieData;
};

// This capitalization is intentional. The current guard symbols distinguish
// CNewbieDB::SetExpert from the inherited CNewbieDb methods, while every call
// uses the same global object at 0x02a03740.
class CNewbieDB final : public CNewbieDb {
public:
    void SetExpert(int accountId);
};

extern CNewbieDB g_newbieDB;
