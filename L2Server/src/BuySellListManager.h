#pragma once

#include "L2Core/src/inc/XStd.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>
#include <ctime>

// Current NpcSocket 0xd7 producer and User buy/sell consumers agree on this
// exact 0x30-byte row.
struct BuySell {
    std::int32_t m_nItemClassId = 0; // +0x00
    std::byte m_alignment04To08[0x04]{}; // +0x04
    std::int64_t m_nPrice = 0; // +0x08
    double m_dStockRate = 0.0; // +0x10
    std::int64_t m_nStock = 0; // +0x18
    std::int64_t m_nCurrentStock = 0; // +0x20
    std::int32_t m_nStockTimestamp = 0; // +0x28
    std::byte m_alignment2CTo30[0x04]{}; // +0x2c
};


class BuySellListCache {
public:
    using Key = std::CTriKey<int, int, std::int64_t>;
    using List = std::vector<BuySell>;

    BuySellListCache() = default;
    ~BuySellListCache();

    static BuySellListCache& Inst();
    static bool IsInBuySellBanList(int itemClassId, const List& list);

    bool Register(const Key& key, List* list);
    List* GetList(const Key& key);
    void ClearAll();

private:
    CYieldLock m_lock; // +0x00
    std::map<Key, List*> mMap; // +0x10
};
