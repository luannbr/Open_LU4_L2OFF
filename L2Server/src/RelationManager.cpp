#include "RelationManager.h"
#include "Shared/src/TextRuntime.h"

#include "Pledge.h"
#include "User.h"

#include "Shared/src/Log.h"

unsigned char CRelationManager::GetRelation(User* user, User* target) {
    L2_GUARD_FUNCTION();

    unsigned char& cached = m_pRelationTable[static_cast<std::uint32_t>(user->m_nRelationSlot)]
                                            [static_cast<std::uint32_t>(target->m_nRelationSlot)];
    if (cached != 0 || !user->IsPledgeMemberOrAcademyMemberPledgeRelationAffected() ||
        !target->IsPledgeMemberOrAcademyMemberPledgeRelationAffected()) {
        return cached;
    }

    CPledge* const userPledge = user->GetPledge();
    CPledge* const targetPledge = target->GetPledge();
    unsigned char relation = 1;
    if (userPledge == nullptr || targetPledge == nullptr) {
        cached = relation;
        if (user->m_nPledgeSid != 0 && target->m_nPledgeSid != 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d], [PledgeWar], CRelationManager::GetRelation, user's pledge is null, %s, %s",
            //     L"RelationManager.cpp",
            //     0x28,
            //     user->GetName(),
            //     target->GetName())
            // Log.Add(LOG_ERROR,
            //     L"[clan relations] Clan lookup failed: player \"%ls\" (clan SID %u, %ls), target \"%ls\" (clan SID %u,
            //     %ls). Using default relation 1.", user->GetName(), user->m_nPledgeSid, userPledge == nullptr ? L"missing"
            //     : L"found", target->GetName(), target->m_nPledgeSid, targetPledge == nullptr ? L"missing" : L"found")
            // L2_LOG_ERROR(Log,
            //     L"[clan relations] Clan lookup failed: player \"%ls\" (clan SID %u, %ls), target \"%ls\" (clan SID %u,
            //     %ls). Using default relation 1.", user->GetName(), user->m_nPledgeSid, userPledge == nullptr ? L"missing"
            //     : L"found", target->GetName(), target->m_nPledgeSid, targetPledge == nullptr ? L"missing" : L"found")
            L2_LOG_ERROR(
                Log,
                u"[clan relations] Clan lookup failed: player \"%ls\" (clan SID %u, %ls), target \"%ls\" (clan SID %u, "
                u"%ls). Using default relation 1.",
                user->GetName(), user->m_nPledgeSid, userPledge == nullptr ? L"missing" : L"found", target->GetName(),
                target->m_nPledgeSid, targetPledge == nullptr ? L"missing" : L"found"
            );
        }
        return relation;
    }

    if (userPledge->DidDeclareWar(static_cast<int>(targetPledge->Id()))) {
        relation = 3;
    }
    if (targetPledge->DidDeclareWar(static_cast<int>(userPledge->Id()))) {
        relation = static_cast<unsigned char>(relation | 4);
    }
    cached = relation;
    return relation;
}
