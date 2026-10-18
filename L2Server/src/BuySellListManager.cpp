#include "BuySellListManager.h"

#include "Shared/src/Log.h"

BuySellListCache::~BuySellListCache() { ClearAll(); }

BuySellListCache& BuySellListCache::Inst() {
    static BuySellListCache cache;
    return cache;
}

bool BuySellListCache::IsInBuySellBanList(const int itemClassId, const List& list) {
    // Current L2Server 0x0057e340.
    L2_GUARD_FUNCTION();
    for (const BuySell& item : list) {
        if (item.m_nItemClassId == itemClassId) {
            return true;
        }
    }
    return false;
}

bool BuySellListCache::Register(const Key& key, List* const list) {
    // Current L2Server 0x0057eacc.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_lock);
        if (mMap.find(key) != mMap.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Already registerd BuySellLIst[%d]",
            //     kBuySellListSource,
            //     0x28,
            //     key.m_SecondaryKey)
            // Log.Add(LOG_ERROR,
            //     L"[trade lists] List %lld is already registered for residence %d in zone %d. Keeping the existing list.",
            //     static_cast<long long>(key.m_SecondaryKey),
            //     key.m_PrimaryKey.m_PrimaryKey,
            //     key.m_PrimaryKey.m_SecondaryKey)
            L2_LOG_ERROR(
                Log,
                u"[trade lists] List %lld is already registered for residence %d in zone %d. Keeping the existing list.",
                static_cast<long long>(key.m_SecondaryKey), key.m_PrimaryKey.m_PrimaryKey, key.m_PrimaryKey.m_SecondaryKey
            );
            return false;
        }
        mMap[key] = list;
    }
    return true;
}

BuySellListCache::List* BuySellListCache::GetList(const Key& key) {
    // Current L2Server 0x0057e9c8.
    L2_GUARD_FUNCTION();
    List* result;
    {
        L2_GUARD(m_lock);
        const auto found = mMap.find(key);
        result = found == mMap.end() ? nullptr : found->second;
    }
    return result;
}

void BuySellListCache::ClearAll() {
    // Current L2Server 0x0057e580.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_lock);
        for (auto& entry : mMap) {
            delete entry.second;
        }
        mMap.clear();
    }
}
