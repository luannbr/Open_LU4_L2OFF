#pragma once

#include <cstddef>
#include <cstdint>

namespace protocol {

// Shared CacheD <-> L2Server element encoded as two consecutive dwords by the
// current RP_EX_PLEDGE_ITEM_LOAD encoder at 0x0054c088. The exact member
// names are present in the paired 2015 packet PDB and their meaning are
// unchanged in the current PledgeItem path.
struct PkPledgeItem {
    std::int32_t nItemClassID;
    std::int32_t nExpiryTime;
};

} // namespace protocol
