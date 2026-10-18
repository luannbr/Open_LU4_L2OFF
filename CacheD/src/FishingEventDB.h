#pragma once

#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>

class PrizeItem;
struct RankingEntry;

// Shipping owner: CacheD/FishingEventDB.{h,cpp}. The current executable keeps
// this object alive, but no production function reads or mutates it. Only its
// constructor, destructor, three xvector carriers and four RWLocks survive.
class CFishingEventDB {
public:
    CFishingEventDB();
    ~CFishingEventDB();

private:

    // PrizeItem and RankingEntry are corroborated names from the compatible
    // FishingEventDB donor. Their current element fields are not
    // exposed: the current image never creates an element.
    std::vector<PrizeItem> m_prizeItems; // +0x000
    std::vector<RankingEntry> m_rankingList; // +0x020
    std::vector<RankingEntry> m_prizeList; // +0x040
    RWLock m_prizeItemsLock; // +0x060
    RWLock m_rankingListLock; // +0x098
    RWLock m_prizeListLock; // +0x0d0
    RWLock m_registrationLock; // +0x108
};



extern CFishingEventDB g_fishingEventDb;
