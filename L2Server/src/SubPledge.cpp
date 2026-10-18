#include "SubPledge.h"

#include "Shared/src/TextRuntime.h"

#include <string>


CSubPledge::CSubPledge(
    PledgeType pledgeType, wchar_t* subPledgeName, std::uint32_t masterDatabaseId, bool memberCountUpgrade
) :
    m_pledgeType(pledgeType),
    m_masterDbId(masterDatabaseId),
    m_subPledgeName(target_utf16::FromRecoveredWindowsWide(subPledgeName)),
    m_bMemberCountUpgrade(memberCountUpgrade) {}

CSubPledge::~CSubPledge() = default;
