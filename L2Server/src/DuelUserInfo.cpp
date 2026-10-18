#include "DuelUserInfo.h"
#include "Shared/src/ScopedLockGuards.h"

#include "DB.h"
#include "User.h"


CDuelUserInfo::CDuelUserInfo() : m_nDuelSid(0), m_eDuelTeam(DTT_TEAM_MAX), m_pDuelPlayerInfo(nullptr) {}

CDuel* CDuelUserInfo::GetDuel() const { return CDuel::GetObjectW(static_cast<std::uint32_t>(m_nDuelSid)); }

bool CDuelUserInfo::IsInIndividualDuel() const {
    if (m_nDuelSid <= 0) {
        return false;
    }
    CDuel* const duel = GetDuel();
    return duel != nullptr && duel->IsIndividualDuel();
}

bool CDuelUserInfo::IsInPartyDuel() const {
    if (m_nDuelSid <= 0) {
        return false;
    }
    CDuel* const duel = GetDuel();
    return duel != nullptr && duel->IsPartyDuel();
}

bool CDuelUserInfo::IsInFightStatus() const {
    if (m_nDuelSid <= 0) {
        return false;
    }
    CDuel* const duel = GetDuel();
    return duel != nullptr && duel->GetDuelStatus() == 2;
}

void CDuelUserInfo::SetDuelPlayerInfo(CDuelPlayerInfo* duelPlayerInfo) { m_pDuelPlayerInfo = duelPlayerInfo; }

CDuelPlayerInfo* CDuelUserInfo::GetPartyDuelPlayerInfo() {
    if (m_pDuelPlayerInfo == nullptr) {
        return nullptr;
    }
    return m_pDuelPlayerInfo->CastPartyDuelPlayerInfo();
}

bool CDuelUserInfo::IsDuelEffectOn(User* user [[maybe_unused]]) const {
    L2_GUARD_FUNCTION();

    CDuel* const duel = GetDuel();
    if (duel == nullptr) {
        return false;
    }

    const int duelStatus = duel->GetDuelStatus();
    if (duelStatus != 2 && duelStatus != 3) {
        return false;
    }
    if (duel->IsIndividualDuel()) {
        return true;
    }
    if (!duel->IsPartyDuel() || m_pDuelPlayerInfo == nullptr) {
        return false;
    }

    CPartyDuelPlayerInfo* const playerInfo = m_pDuelPlayerInfo->CastPartyDuelPlayerInfo();
    return playerInfo != nullptr && playerInfo->IsAlive();
}

bool CDuelUserInfo::IsDuelDead(User* user [[maybe_unused]]) {
    L2_GUARD_FUNCTION();

    CDuel* const duel = GetDuel();
    if (duel == nullptr || duel->IsIndividualDuel() || !duel->IsPartyDuel() || m_pDuelPlayerInfo == nullptr) {
        return false;
    }

    CPartyDuelPlayerInfo* const playerInfo = m_pDuelPlayerInfo->CastPartyDuelPlayerInfo();
    return playerInfo != nullptr && !playerInfo->IsAlive();
}

void CDuelUserInfo::SetTeam(DuelTeamType team) { m_eDuelTeam = team; }

DuelTeamType CDuelUserInfo::GetOpponentTeam() const {
    L2_GUARD_FUNCTION();
    return CDuelManager::GetOpponentTeam(m_eDuelTeam);
}

std::uint8_t CDuelUserInfo::GetTeamForEffect(User* user) const {
    if (IsDuelEffectOn(user)) {
        if (m_eDuelTeam == DTT_TEAM_A) {
            return 1;
        }
        if (m_eDuelTeam == DTT_TEAM_B) {
            return 2;
        }
    }
    return 0;
}

bool CDuelUserInfo::IsAlly(User* user) {
    L2_GUARD_FUNCTION();

    CDuel* const duel = GetDuel();
    if (duel == nullptr || !duel->IsPartyDuel()) {
        return false;
    }
    if (m_nDuelSid != user->m_DuelUserInfo.GetDuelSid() || m_eDuelTeam == DTT_TEAM_MAX ||
        user->m_DuelUserInfo.GetTeam() == DTT_TEAM_MAX) {
        return false;
    }
    return m_eDuelTeam == user->m_DuelUserInfo.GetTeam();
}

User* CDuelUserInfo::GetIndividualDuelEnemy() {
    // Current L2Server 0x006ef97c.
    L2_GUARD_FUNCTION();

    CDuel* const duel = GetDuel();
    if (duel == nullptr || !duel->IsIndividualDuel()) {
        return nullptr;
    }
    CIndividualDuel* const individualDuel = duel->CastIndividualDuel();
    if (individualDuel == nullptr) {
        return nullptr;
    }
    return individualDuel->GetPlayer(GetOpponentTeam());
}

bool CDuelUserInfo::IsEnemy(User* user) {
    L2_GUARD_FUNCTION();

    CDuel* const duel = GetDuel();
    if (duel == nullptr || m_nDuelSid != user->m_DuelUserInfo.GetDuelSid()) {
        return false;
    }
    const int status = duel->GetDuelStatus();
    if (status != 2 && status != 3) {
        return false;
    }
    const DuelTeamType targetTeam = user->m_DuelUserInfo.GetTeam();
    if (m_eDuelTeam == DTT_TEAM_MAX || targetTeam == DTT_TEAM_MAX) {
        return false;
    }
    return targetTeam == GetOpponentTeam();
}

const FVector CDuelUserInfo::GetReturnPos() {
    CPartyDuelPlayerInfo* const playerInfo = static_cast<CPartyDuelPlayerInfo*>(GetPartyDuelPlayerInfo());
    if (playerInfo == nullptr) {
        return FVector{};
    }
    return playerInfo->GetReturnPos();
}

void CDuelUserInfo::GetOriginalStatus(double& hp, double& mp, double& cp) {
    if (m_pDuelPlayerInfo != nullptr) {
        m_pDuelPlayerInfo->GetOriginalStatus(hp, mp, cp);
    }
}

void CDuelUserInfo::GetSummonOriginalStatus(int summonId, double& hp, double& mp) {
    if (m_pDuelPlayerInfo == nullptr || m_pDuelPlayerInfo->m_vSummonInfo.empty()) {
        return;
    }

    {
        L2_GUARD(m_pDuelPlayerInfo->m_SummonLock);
        for (CDuelPlayerInfo* summonInfo : m_pDuelPlayerInfo->m_vSummonInfo) {
            if (summonInfo != nullptr && summonInfo->GetSID() == summonId) {
                double cp = 0.0;
                summonInfo->GetOriginalStatus(hp, mp, cp);
            }
        }
    }
}

void CDuelUserInfo::SaveDuelRecord(User* user) {
    L2_GUARD_FUNCTION();

    if (user != nullptr) {
        db.RequestSaveDuelRecord(
            user, m_DuelRecord.m_IndividualRecord.m_nWinCount, m_DuelRecord.m_IndividualRecord.m_nLoseCount,
            m_DuelRecord.m_IndividualRecord.m_nDrawCount, m_DuelRecord.m_PartyRecord.m_nWinCount,
            m_DuelRecord.m_PartyRecord.m_nLoseCount, m_DuelRecord.m_PartyRecord.m_nDrawCount
        );
    }
}

void CDuelUserInfo::SetDuelInfo(int duelSid, DuelTeamType team, CDuelPlayerInfo* duelPlayerInfo) {
    m_nDuelSid = duelSid;
    m_eDuelTeam = team;
    m_pDuelPlayerInfo = duelPlayerInfo;
}

void CDuelUserInfo::ResetDuelInfo() {
    m_nDuelSid = 0;
    m_pDuelPlayerInfo = nullptr;
    m_eDuelTeam = DTT_TEAM_MAX;
}

void CDuelUserInfo::UpdateDuelRecord(
    int individualWinCount,
    int individualLoseCount [[maybe_unused]],
    int individualDrawCount [[maybe_unused]],
    int partyWinCount,
    int partyLoseCount,
    int partyDrawCount
) {

    m_DuelRecord.m_IndividualRecord.m_nWinCount = individualWinCount;
    m_DuelRecord.m_IndividualRecord.m_nLoseCount = individualWinCount;
    m_DuelRecord.m_IndividualRecord.m_nDrawCount = individualWinCount;
    m_DuelRecord.m_PartyRecord.m_nWinCount = partyWinCount;
    m_DuelRecord.m_PartyRecord.m_nLoseCount = partyLoseCount;
    m_DuelRecord.m_PartyRecord.m_nDrawCount = partyDrawCount;
}
