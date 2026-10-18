#include "Inventory.h"

#include "Shared/src/Log.h"
#include "Shared/src/ScopedLockGuards.h"

#include <cstring>


CInventoryLockManager& CInventory::GetLockManager() {
    // L2NPC.exe 0x00454714 owns this function-local static. The current
    // revision doubled the nearby PDB's 16384 slots to 32768.
    static CInventoryLockManager m_lock_manager(kInventoryLockLogicalName, kInventoryLockSpinCount);
    return m_lock_manager;
}

int CInventory::GetFirstIndex() {
    CSharedCreatureData* owner = CCreatureFactory::Inst().GetData(m_nCreatureIndex);
    // The target dereferences the factory result unconditionally. Callers
    // establish a valid creature index through SetIndex before reaching here.
    return owner->m_nInventoryIndex;
}

CSharedItemData* CInventory::GetFirst() {
    const int index = GetFirstIndex();
    return index == -1 ? nullptr : CItemFactory::Inst().GetData(index);
}

bool CInventory::Have(int itemIndex) {
    L2_READ_GUARD(*this);
    CItemFactory& items = CItemFactory::Inst();
    int currentIndex = GetFirstIndex();
    int depth = 0;
    while (currentIndex != -1) {
        if (currentIndex == itemIndex) {
            return true;
        }
        currentIndex = items.GetNextIndex(currentIndex);
        ++depth;
        if (depth >= kMaximumTraversalDepth) {
            // Log.Add(
            //     LOG_ERROR,
            //     "depth > %d at file[%s], line[%d]",
            //     0x192,
            //     "..\\Shared\\Inventory_common.cpp",
            //     0x3c)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Membership scan for item index %d reached the %d-link limit for creature index %d. Returning
            //     not found. Next index: %d.", itemIndex, kMaximumTraversalDepth, m_nCreatureIndex, currentIndex)
            L2_LOG_ERROR(
                Log,
                u"[inventory] Membership scan for item index %d reached the %d-link limit for creature index %d; returning "
                u"not found. Next index: %d.",
                itemIndex, kMaximumTraversalDepth, m_nCreatureIndex, currentIndex
            );
            break;
        }
    }
    return false;
}

std::int64_t CInventory::GetItemAmount(int nItemClassID, int nEnchanted, bool bCheckVariation) {
    L2_READ_GUARD(*this);
    CItemFactory& items = CItemFactory::Inst();
    std::uint64_t amount = 0;
    int currentIndex = GetFirstIndex();
    int depth = 0;
    while (currentIndex != -1) {
        CSharedItemData* item = items.GetData(currentIndex);
        if (!item) {
            // Log.Add(
            //     LOG_ERROR,
            //     "spItem is Invalid at file[%s], line[%d]",
            //     "..\\Shared\\Inventory_common.cpp",
            //     0x5d)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Amount scan for item template %d stopped at invalid item index %d for creature index %d.
            //     returning the amount accumulated so far.", nItemClassID, currentIndex, m_nCreatureIndex)
            L2_LOG_ERROR(
                Log,
                u"[inventory] Amount scan for item template %d stopped at invalid item index %d for creature index %d; "
                u"returning the amount accumulated so far.",
                nItemClassID, currentIndex, m_nCreatureIndex
            );
            break;
        }
        if (item->m_nClassID != nItemClassID || (nEnchanted >= 0 && item->GetEnchanted() != nEnchanted) ||
            (bCheckVariation && item->m_bIsVariation)) {
            // Continue through the exact current-slot +0x3c link below.
        } else if (item->IsStackable()) {
            amount += static_cast<std::uint64_t>(item->m_nCount);
        } else {
            ++amount;
        }
        currentIndex = items.GetNextIndex(currentIndex);
        ++depth;
        if (depth >= kMaximumTraversalDepth) {
            // Log.Add(
            //     LOG_ERROR,
            //     "depth > %d at file[%s], line[%d]",
            //     0x192,
            //     "..\\Shared\\Inventory_common.cpp",
            //     0x75)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Amount scan for item template %d reached the %d-link limit for creature index %d. Returning
            //     the amount accumulated so far. Next index: %d.", nItemClassID, kMaximumTraversalDepth, m_nCreatureIndex,
            //     currentIndex)
            L2_LOG_ERROR(
                Log,
                u"[inventory] Amount scan for item template %d reached the %d-link limit for creature index %d; returning "
                u"the amount accumulated so far. Next index: %d.",
                nItemClassID, kMaximumTraversalDepth, m_nCreatureIndex, currentIndex
            );
            break;
        }
    }
    std::int64_t signedAmount = 0;
    static_assert(sizeof(signedAmount) == sizeof(amount));
    std::memcpy(&signedAmount, &amount, sizeof(signedAmount));
    return signedAmount;
}
