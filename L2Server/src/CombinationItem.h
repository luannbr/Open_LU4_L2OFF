#pragma once

#include "L2Core/src/inc/XStd.h"
#include "Shared/src/StandardContainers.h"
#include "UserPacketEx.h"

#include <cstddef>
#include <cstdint>

class CParserForCombinationItem;

class CombinationItemInfo {
public:
    CombinationItemInfo();

    void AddMaterialItem(protocol::ItemInfo item);
    void AddMaterialItem(std::int32_t itemClassId, std::int64_t amount);
    void SetSuccessItem(std::int32_t itemClassId, std::int64_t amount);
    void SetFailItem(std::int32_t itemClassId, std::int64_t amount);
    void SetProb(std::int32_t probability);

private:
    friend class CParserForCombinationItem;

    std::vector<protocol::ItemInfo> m_vMaterialItem; // +0x00
    std::int32_t m_nProb; // +0x20
    protocol::ItemInfo m_SuccessItem; // +0x24
    protocol::ItemInfo m_FailItem; // +0x30
};

class CombinationItemInfoSet {
private:
    friend class CParserForCombinationItem;

    std::vector<CombinationItemInfo*> m_vCombinationList; // +0x00
    std::int32_t m_nId; // +0x20
};

using CombinationItemKey = std::CDualKey<std::int32_t, std::int64_t>;

class CombinationItemManager {
public:
    void AddCombinationSet(CombinationItemKey key, CombinationItemInfoSet* combinationSet);

private:
    std::map<CombinationItemKey, CombinationItemInfoSet*> m_mapCombinationDB;
};

extern CombinationItemManager g_CombinationManager;
