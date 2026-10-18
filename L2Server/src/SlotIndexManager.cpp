#include "SlotIndexManager.h"

#include "Item.h"


#include <cstdint>
#include <cstring>

namespace {

std::int32_t ItemSidBits(CItem* item) noexcept {
    const std::uint32_t sid = item->GetSmartID().Sid();
    std::int32_t signedBits = 0;
    std::memcpy(&signedBits, &sid, sizeof(signedBits));
    return signedBits;
}

} // namespace


CSlotIndexManager::CSlotIndexManager() : m_nMaxSlotCount(0) { m_mapSlotInfo.clear(); }

CSlotIndexManager::~CSlotIndexManager() = default;

bool CSlotIndexManager::Remove(CItem* item) {
    L2_GUARD_FUNCTION();

    if (item == nullptr) {
        return false;
    }

    const std::int32_t itemSid = ItemSidBits(item);
    for (auto it = m_mapSlotInfo.begin(); it != m_mapSlotInfo.end(); ++it) {
        if (it->second == itemSid) {
            m_mapSlotInfo.erase(it);
            return true;
        }
    }
    return false;
}

void CSlotIndexManager::Init() { m_mapSlotInfo.clear(); }

int CSlotIndexManager::FindFirstEmptySlot() {
    L2_GUARD_FUNCTION();

    for (int slotIndex = 0; slotIndex < m_nMaxSlotCount; ++slotIndex) {
        if (m_mapSlotInfo.find(slotIndex) == m_mapSlotInfo.end()) {
            return slotIndex;
        }
    }
    return -1;
}

int CSlotIndexManager::Add(int slotIndex, CItem* item) {
    L2_GUARD_FUNCTION();

    if (slotIndex < -1 || item == nullptr) {
        return -1;
    }

    if (slotIndex == -1) {
        slotIndex = FindFirstEmptySlot();
    } else if (m_mapSlotInfo.find(slotIndex) != m_mapSlotInfo.end()) {
        return -1;
    }

    if (slotIndex == -1) {
        return -1;
    }

    m_mapSlotInfo.insert(std::make_pair(slotIndex, ItemSidBits(item)));
    return slotIndex;
}
