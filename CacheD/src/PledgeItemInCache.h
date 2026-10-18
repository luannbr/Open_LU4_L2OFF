#pragma once

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/PledgePacket.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class PledgeItemInCache;

namespace PledgeItemStuff {

void SelectPledgeItems(const PledgeItemInCache* item, std::list<protocol::PkPledgeItem>& items);

} // namespace PledgeItemStuff


// Exact current CacheD owner. Target long is always 32-bit on Win64, so the
// portable signature uses int32_t while the guard text keeps the original
// source spelling.
class PledgeItemInCache {
public:
    explicit PledgeItemInCache(std::int32_t pledgeId);
    ~PledgeItemInCache();

    bool LoadFromDB();
    bool AddItem(std::int32_t itemClassId, std::int32_t expiryTime);
    bool AddItemAndSave(std::int32_t itemClassId, std::int32_t expiryTime);

private:
    friend void PledgeItemStuff::SelectPledgeItems(const PledgeItemInCache*, std::list<protocol::PkPledgeItem>&);

    bool _AddItem(std::int32_t itemClassId, std::int32_t expiryTime);

    // Current Cached.exe 0x0051fc84. The current binary keeps this as a
    // separate header-owned non-null body and 0x0051fdf8 is its only caller.
    // Its original private symbol did not survive. SelectPledgeItems is the
    // semantic name inferred from that sole caller, not a recovered PDB name.
    void SelectPledgeItems(std::list<protocol::PkPledgeItem>& items) const {
        {
            L2_READ_GUARD(m_lock);
            for (const auto& entry : m_items) {
                items.push_back(protocol::PkPledgeItem{entry.first, entry.second});
            }
        }
    }

    mutable RWLock m_lock; // +0x00
    std::int32_t m_pledgeId; // +0x38
    [[maybe_unused]] std::uint32_t m_targetAlignment; // +0x3c
    std::map<std::int32_t, std::int32_t> m_items; // +0x40
};
