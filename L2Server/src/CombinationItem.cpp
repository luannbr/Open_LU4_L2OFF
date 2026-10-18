#include "CombinationItem.h"

#include "Shared/src/Log.h"

CombinationItemManager g_CombinationManager;

CombinationItemInfo::CombinationItemInfo() : m_nProb(0) {}

void CombinationItemInfo::AddMaterialItem(protocol::ItemInfo item) {
    L2_GUARD_FUNCTION();
    m_vMaterialItem.push_back(item);
}

void CombinationItemInfo::AddMaterialItem(std::int32_t itemClassId, std::int64_t amount) {
    L2_GUARD_FUNCTION();
    protocol::ItemInfo item;
    item.nItemClassId = itemClassId;
    item.nAmount = amount;
    AddMaterialItem(item);
}

void CombinationItemInfo::SetSuccessItem(std::int32_t itemClassId, std::int64_t amount) {
    m_SuccessItem.nItemClassId = itemClassId;
    m_SuccessItem.nAmount = amount;
}

void CombinationItemInfo::SetFailItem(std::int32_t itemClassId, std::int64_t amount) {
    m_FailItem.nItemClassId = itemClassId;
    m_FailItem.nAmount = amount;
}

void CombinationItemInfo::SetProb(std::int32_t probability) { m_nProb = probability; }

void CombinationItemManager::AddCombinationSet(CombinationItemKey key, CombinationItemInfoSet* combinationSet) {
    L2_GUARD_FUNCTION();

    if (combinationSet == nullptr) {
        return;
    }

    const auto inserted = m_mapCombinationDB.emplace(key, combinationSet);
    if (!inserted.second) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Combination Manager] Duplicated Combination Set")
        // Log.Add(LOG_ERROR,
        //     L"[item combination] Duplicate set key {%d. %lld}. Keeping the existing set.",
        //     key.m_PrimaryKey,
        //     static_cast<long long>(key.m_SecondaryKey))
        L2_LOG_ERROR(
            Log, u"[item combination] Duplicate set key {%d; %lld}. Keeping the existing set.", key.m_PrimaryKey,
            static_cast<long long>(key.m_SecondaryKey)
        );
    }
}
