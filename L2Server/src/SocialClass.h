#pragma once

#include "Pledge.h"
#include "User.h"

namespace SocialClassNS {

bool Level0(User* const user [[maybe_unused]]) { return true; }

bool LevelNone(User* const user [[maybe_unused]]) { return false; }

bool Level1(User* const user) {
    L2_GUARD_FUNCTION();
    return user->GetPledge() != nullptr;
}

bool IsMaster(User* const user) {
    L2_GUARD_FUNCTION();
    return user->IsPledgeMaster();
}

bool IsMember(User* const user) {
    L2_GUARD_FUNCTION();
    return user->m_data->m_nPledgeID != 0 && user->m_data->m_nPledgeType != ACADEMY;
}

bool IsKnight(User* const user) {
    L2_GUARD_FUNCTION();
    if (!IsMember(user)) {
        return false;
    }
    const int pledgeType = user->m_data->m_nPledgeType;
    return pledgeType == SUB_1 || pledgeType == SUB_2;
}

bool IsMasterMember(User* const user) {
    L2_GUARD_FUNCTION();
    return IsMember(user) && user->m_data->m_nPledgeType == MASTER_PLEDGE;
}

bool IsMercenary(User* const user) {
    L2_GUARD_FUNCTION();
    if (!IsMember(user)) {
        return false;
    }
    const int pledgeType = user->m_data->m_nPledgeType;
    return pledgeType == SUB_1_1 || pledgeType == SUB_1_2 || pledgeType == SUB_2_1 || pledgeType == SUB_2_2;
}

bool IsNobless(User* const user) {
    L2_GUARD_FUNCTION();
    const int noblessType = user->m_data->m_nNoblessType;
    return noblessType == 1 || noblessType == 2;
}

bool IsHero(User* const user) {
    L2_GUARD_FUNCTION();
    return user->IsHero();
}

bool IsMasterOrNobless(User* const user) {
    L2_GUARD_FUNCTION();
    return IsMaster(user) || IsNobless(user);
}

bool IsMasterOrHero(User* const user) {
    L2_GUARD_FUNCTION();
    return IsMaster(user) || IsHero(user);
}

bool IsMercenaryOrNobless(User* const user) {
    L2_GUARD_FUNCTION();
    return IsMercenary(user) || IsNobless(user);
}

bool IsKnightOrNobless(User* const user) {
    L2_GUARD_FUNCTION();
    return IsKnight(user) || IsNobless(user);
}

bool IsMasterMemberOrNobless(User* const user) {
    L2_GUARD_FUNCTION();
    return IsMasterMember(user) || IsNobless(user);
}

bool IsKnightMaster(User* const user) {
    L2_GUARD_FUNCTION();
    CPledge* const pledge = user->GetPledge();
    return pledge != nullptr && (pledge->IsSubMaster(user, SUB_1) || pledge->IsSubMaster(user, SUB_2));
}

bool IsMercenaryMaster(User* const user) {
    L2_GUARD_FUNCTION();
    CPledge* const pledge = user->GetPledge();
    return pledge != nullptr &&
        (pledge->IsSubMaster(user, SUB_1_1) || pledge->IsSubMaster(user, SUB_1_2) || pledge->IsSubMaster(user, SUB_2_1) ||
            pledge->IsSubMaster(user, SUB_2_2));
}

bool IsMercenaryMasterOrHero(User* const user) {
    L2_GUARD_FUNCTION();
    return IsMercenaryMaster(user) || IsHero(user);
}

bool IsKnightMasterOrHero(User* const user) {
    L2_GUARD_FUNCTION();
    return IsKnightMaster(user) || IsHero(user);
}

bool IsMercenaryMasterOrNobless(User* const user) {
    L2_GUARD_FUNCTION();
    return IsMercenaryMaster(user) || IsNobless(user);
}

using ClassifyFunction = bool (*)(User* user);

// Current L2Server 0x015dd580..0x015dd9ff. The table is a physical
// externally linked Pledge.obj owner: twelve pledge levels by twelve social
// classes. Every cell below is closed by the current PE pointer image. Donor
// symbols are used only for the original table and two constant-function
// spellings.
ClassifyFunction s_SocialClassClassifyFuncTable[12][12] = {
    {
        Level0,
        Level1,
        LevelNone,
        LevelNone,
        LevelNone,
        IsNobless,
        LevelNone,
        LevelNone,
        IsHero,
        LevelNone,
        LevelNone,
        LevelNone,
    },
    {
        Level0,
        Level1,
        LevelNone,
        LevelNone,
        LevelNone,
        IsNobless,
        LevelNone,
        LevelNone,
        IsHero,
        LevelNone,
        LevelNone,
        LevelNone,
    },
    {
        Level0,
        Level1,
        LevelNone,
        LevelNone,
        LevelNone,
        IsNobless,
        LevelNone,
        LevelNone,
        IsHero,
        LevelNone,
        LevelNone,
        LevelNone,
    },
    {
        Level0,
        Level1,
        LevelNone,
        LevelNone,
        LevelNone,
        IsNobless,
        LevelNone,
        LevelNone,
        IsHero,
        LevelNone,
        LevelNone,
        LevelNone,
    },
    {
        Level0,
        Level1,
        LevelNone,
        IsMaster,
        LevelNone,
        IsNobless,
        LevelNone,
        LevelNone,
        IsHero,
        LevelNone,
        LevelNone,
        LevelNone,
    },
    {
        Level0,
        Level1,
        IsMember,
        LevelNone,
        IsMaster,
        IsNobless,
        LevelNone,
        LevelNone,
        IsHero,
        LevelNone,
        LevelNone,
        LevelNone,
    },
    {
        Level0,
        Level1,
        IsKnight,
        IsMasterMember,
        IsKnightMaster,
        IsMasterOrNobless,
        LevelNone,
        LevelNone,
        IsHero,
        LevelNone,
        LevelNone,
        LevelNone,
    },
    {
        Level0,
        Level1,
        IsMercenary,
        IsKnight,
        IsMasterMember,
        IsMercenaryMasterOrNobless,
        IsKnightMaster,
        IsMaster,
        IsHero,
        LevelNone,
        LevelNone,
        LevelNone,
    },
    {
        Level0,
        Level1,
        IsMember,
        IsMercenary,
        IsKnight,
        IsMasterMemberOrNobless,
        IsMercenaryMaster,
        IsKnightMaster,
        IsMasterOrHero,
        LevelNone,
        LevelNone,
        LevelNone,
    },
    {
        Level0,
        Level1,
        IsMember,
        IsMember,
        IsMercenary,
        IsKnightOrNobless,
        IsMasterMember,
        IsMercenaryMaster,
        IsKnightMasterOrHero,
        IsMaster,
        LevelNone,
        LevelNone,
    },
    {
        Level0,
        Level1,
        IsMember,
        IsMember,
        IsMember,
        IsMercenaryOrNobless,
        IsKnight,
        IsMasterMember,
        IsMercenaryMasterOrHero,
        IsKnightMaster,
        IsMaster,
        LevelNone,
    },
    {
        Level0,
        Level1,
        IsMember,
        IsMember,
        IsMember,
        IsMember,
        IsMercenaryOrNobless,
        IsKnight,
        IsMasterMember,
        IsMercenaryMasterOrHero,
        IsKnightMaster,
        IsMaster,
    },
};


} // namespace SocialClassNS
