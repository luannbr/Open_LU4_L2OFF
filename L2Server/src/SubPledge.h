#pragma once

#include "Shared/src/PledgeType.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

// Exact current 0x38 subpledge value. The eight bytes at +0x28 and seven
// tail bytes are not initialized by the constructor, matching
// 00add680. The UTF-16 carrier is the proved 0x20 MSVC string envelope.
class CSubPledge final {
public:
    CSubPledge(PledgeType pledgeType, wchar_t* subPledgeName, std::uint32_t masterDatabaseId, bool memberCountUpgrade);
    ~CSubPledge();

    PledgeType m_pledgeType;
    std::uint32_t m_masterDbId;
    ::std::u16string m_subPledgeName;
    std::byte reserved_28[8];
    bool m_bMemberCountUpgrade;
    std::byte padding_31[7];
};
