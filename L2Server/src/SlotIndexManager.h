#pragma once

#include "Shared/src/StandardContainers.h"

class CItem;

class CSlotIndexManager {
public:
    CSlotIndexManager();
    ~CSlotIndexManager();

    int Add(int slotIndex, CItem* item);
    void Init();
    bool Remove(CItem* item);
    void SetMaxSlotCount(int maxSlotCount) noexcept { m_nMaxSlotCount = maxSlotCount; }

private:
    int FindFirstEmptySlot();

    std::map<int, int> m_mapSlotInfo;
    int m_nMaxSlotCount;
};
