#pragma once

#include "Shared/src/IOCs.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

enum WATCHTYPE : std::int32_t {
    WATCH_NPC = 1,
    WATCH_ITEM = 2,
};

class CWatchItem {
public:
    CWatchItem(WATCHTYPE type, int objectId, const char16_t* information);

    int m_nNpcId;
    int m_nItemId;
    char16_t m_strInformation[256];
};


class CWatchList {
public:
    CWatchList();
    ~CWatchList();

    CWatchItem* CheckWatchList(WATCHTYPE type, int objectId) const;
    bool ParseWatchList();

private:
    using WatchItemMap = std::map<int, CWatchItem*>;

    CIOCriticalSection m_lock;
    WatchItemMap m_npcs;
    WatchItemMap m_items;
};


extern CWatchList g_WatchList;
