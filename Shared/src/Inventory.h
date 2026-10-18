// Shared/inventory.h
//
// CInventory is shared with the other Lineage II server processes. The
// nearby L2NPC PDB supplies the original member names and API, while the
// current L2NPC.exe constructor at 0x004440c8 fixes this revision's 0xa0
// layout (its MSVC xmap is 0x20 bytes and its xvector is 0x20 bytes).
#pragma once

#include "Shared/src/CenteredSharedRWLock.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>


class CInventory {
public:
    static constexpr int kMaximumTraversalDepth = 0x193;

    CInventory() :
        m_nCreatureIndex(-1),
        m_bIsUser(false),
        m_lock(nullptr),
        m_nCarryingWeight(0),
        m_nEXPRuneCount(0),
        m_nSPRuneCount(0),
        m_nRPRuneCount(0) {}

    ~CInventory() {
        // The shipping inline destructor clears the non-owning centered-lock
        // pointer before destroying the vector, RWLock and map members.
        m_lock = nullptr;
    }

    static CInventoryLockManager& GetLockManager();

    bool SetIndex(int creatureIndex, bool isUser) {
        m_nCreatureIndex = creatureIndex;
        m_bIsUser = isUser;
        m_lock = GetLockManager().GetLock(creatureIndex);
        return m_lock != nullptr;
    }

    int GetCarryingWeight() { return m_nCarryingWeight; }

    void WriteLock(const wchar_t* file, int line) { m_lock->WriteLock(file, line); }

    void ReadLock(const wchar_t* file, int line) { m_lock->ReadLock(file, line, false); }

    void WriteUnlock() { m_lock->WriteUnlock(nullptr, 0); }

    void ReadUnlock() { m_lock->ReadUnlock(nullptr, 0); }

    int GetFirstIndex();
    CSharedItemData* GetFirst();
    bool Have(int itemIndex);
    std::int64_t GetItemAmount(int nItemClassID, int nEnchanted, bool bCheckVariation);

protected:
    std::int32_t m_nCreatureIndex;
    bool m_bIsUser;
    CSharedRWLock* m_lock;
    std::int32_t m_nCarryingWeight;

public:
    // These two members are public in the original shared header. L2NPC
    // constructs and destroys them even though its linked Inventory_common
    // module only emits the read-side methods above.
    std::map<int, int> MapChangedItems;
    RWLock lockMapChangedItems;

protected:
    std::int32_t m_nEXPRuneCount;
    std::int32_t m_nSPRuneCount;
    std::int32_t m_nRPRuneCount;
    std::vector<std::uint32_t> m_RuneItemClassIds;
};
