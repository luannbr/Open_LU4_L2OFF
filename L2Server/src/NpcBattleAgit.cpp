#include "NpcBattleAgit.h"
#include "CConfig.h"
#include "DB.h"
#include "NPC.h"
#include "NpcServer.h"
#include "Pledge.h"
#include "Protocol/src/SystemMessageIds.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"

#include <cstdint>

CNpcBattleAgit::CNpcBattleAgit() : m_nWinnerPledgeId(-1), m_alignment49cTo4a0{} {
    // Current L2Server 0x008982c8.
    L2_GUARD_FUNCTION();
}

CNpcBattleAgit::~CNpcBattleAgit() = default;

bool CNpcBattleAgit::IsNpcBattleAgit() const { return true; }

bool CNpcBattleAgit::IsBattleTypeAgit() const { return true; }

CNpcBattleAgit* CNpcBattleAgit::CastNpcBattleAgit() const { return const_cast<CNpcBattleAgit*>(this); }

void CNpcBattleAgit::StartTimer() {
    // Current L2Server 0x00898058.
    L2_GUARD_FUNCTION();
    if (!IsUnderSiege()) {
        const ResidenceState state = m_data != nullptr ? m_data->m_ResidenceState : RS_MAX;
        if (state != RS_STANDBY) {
            ChangeResidenceState(RS_STANDBY);
        }
    } else {
        m_bSiegeContinued = true;
        if (m_data != nullptr) {
            m_data->m_ResidenceState = RS_STANDBY;
        }
    }
    CWorldAgit::StartTimer();
}

void CNpcBattleAgit::StartSiege() {
    // Current L2Server 0x00897ce4.
    L2_GUARD_FUNCTION();
    CResidenceData* const data = m_data;
    const int ownerId = data != nullptr ? static_cast<int>(data->m_nResidenceOwnerID) : 0;
    const int residenceId = data != nullptr ? data->m_nResidenceID : 0;
    const char16_t* const residenceName = data != nullptr ? data->m_sResidenceName.data() : u"";
    g_npcserver.Send("cdSddd", 0x31, residenceId, residenceName, ownerId, 1, m_bSiegeContinued ? 1 : 0);
    if (!m_bSiegeContinued) {
        db.RequestResetContribution(*this);
    }
    m_bSiegeContinued = false;
    m_nWinnerPledgeId = -1;
    MakeNPCDisable(true);
    db.RequestInstallResidenceDeco(*this, 0, 0, 0);
    db.RequestInstallResidenceDeco(*this, 1, 0, 0);
    db.RequestInstallResidenceDeco(*this, 3, 0, 0);
    SetBattleField(true);
}

void CNpcBattleAgit::EndSiege() {
    // Current L2Server 0x008986d8.
    L2_GUARD_FUNCTION();
    CResidenceData* const data = m_data;
    const int ownerId = data != nullptr ? static_cast<int>(data->m_nResidenceOwnerID) : 0;
    const int residenceId = data != nullptr ? data->m_nResidenceID : 0;
    const char16_t* const residenceName = data != nullptr ? data->m_sResidenceName.data() : u"";
    g_npcserver.Send("cdSddd", 0x31, residenceId, residenceName, ownerId, 0, 0);

    CSPointer<CPledge> winner;
    if (m_nWinnerPledgeId >= 1) {
        winner = CPledge::FindPledge(g_nWorldId, m_nWinnerPledgeId);
    }

    const CResidenceData* const ownerData = m_data;
    const int previousOwnerId = ownerData != nullptr ? static_cast<int>(ownerData->m_nResidenceOwnerID) : 0;
    if (!winner) {
        BroadcastSystemMessageW(SystemMessageId::CLAN_HALL_SIEGE_ENDED_IN_DRAW);
        ChangeOwner(nullptr, 0, true);
    } else {
        BroadcastSystemMessageW(winner->GetPledgeName(), SystemMessageId::CLAN_WON_NAMED_CLAN_HALL_SIEGE);
        ChangeOwner(winner.get(), 0, true);
    }
    UpdatePledgeNameValue(previousOwnerId, winner.get());
    MakeNPCDisable(false);
    SetBattleField(false);
    m_osBossNpc.SetObject(nullptr);
    db.RequestResetContribution(*this);
}

void CNpcBattleAgit::PauseSiege() {
    // Current L2Server 0x00898140.
    L2_GUARD_FUNCTION();
    if (!m_bPaused) {
        m_bPaused = true;
    }
}

void CNpcBattleAgit::ResumeSiege() {
    // Current L2Server 0x008981d4.
    L2_GUARD_FUNCTION();
    m_bPaused = false;
    CResidenceData* const data = m_data;
    const int ownerId = data != nullptr ? static_cast<int>(data->m_nResidenceOwnerID) : 0;
    const int residenceId = data != nullptr ? data->m_nResidenceID : 0;
    const char16_t* const residenceName = data != nullptr ? data->m_sResidenceName.data() : u"";
    g_npcserver.Send("cdSddd", 0x31, residenceId, residenceName, ownerId, 1, 1);
}

void CNpcBattleAgit::TimerExpired(const int id) {
    // Current L2Server 0x00897e58.
    L2_GUARD_FUNCTION();
    const ResidenceState state = m_data != nullptr ? m_data->m_ResidenceState : RS_MAX;
    const std::int32_t now = runtime::crt::Time32Now();

    if (state < RS_REGISTRATION || state > RS_SIEGE) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Unknown Agit State %d",
        //     static_cast<int>(state))
        // L2_LOG_ERROR(Log,
        //     L"Unknown Agit State %d",
        //     static_cast<int>(state))
        L2_LOG_ERROR(
            Log,
            "NPC battle clan hall %d: timer cannot handle residence state %d; this update is skipped, with the next check "
            "in 60 seconds.",
            m_data != nullptr ? m_data->m_nResidenceID : 0, static_cast<int>(state)
        );
        AddTimer(60000, 0);
        return;
    }

    if (state < RS_SIEGE) {
        CResidenceData* const data = m_data;
        const std::int32_t nextSiege = data != nullptr ? data->m_NextSiege : 0;
        if ((now <= runtime::AddInt32Wrapping(nextSiege, -10) || nextSiege < now) && data->m_nAgitAuction == 0 &&
            nextSiege < now && g_PledgeTimer.IsNpcServerAlive(CPledgeTimer::NSAT_20)) {
            StartSiege();
            ChangeResidenceState(RS_SIEGE);
        }
        CWorldAgit::TimerExpired(id);
        return;
    }

    const std::int32_t nextSiege = m_data != nullptr ? m_data->m_NextSiege : 0;
    if (!g_PledgeTimer.IsNpcServerAlive(CPledgeTimer::NSAT_20)) {
        PauseSiege();
    } else if (!m_bPaused) {
        if (m_nWinnerPledgeId != -1 || runtime::AddInt32Wrapping(nextSiege, m_nSiegePeriod) <= now) {
            if (m_data != nullptr) {
                m_data->m_NextSiege = runtime::AddInt32Wrapping(m_data->m_NextSiege, 0x127500);
            }
            db.RequestSetNextAgitBattleTime(this);
            EndSiege();
            ChangeResidenceState(RS_STANDBY);
        }
    } else {
        ResumeSiege();
    }
    CWorldAgit::TimerExpired(id);
}

void CNpcBattleAgit::OnBossNpcEnterWorld(CNPC* const npc) {
    // Current L2Server 0x00898958 assumes a non-null NPC.
    L2_GUARD_FUNCTION();
    m_osBossNpc.SetObject(npc);
    ++npc->m_nPledgeContributeSaveTick;
}

void CNpcBattleAgit::OnBossNpcDead(CNPC* const npc) {
    // Current L2Server 0x00898a08 assumes a non-null NPC/data.
    L2_GUARD_FUNCTION();
    if (npc->m_nPledgeContributeSaveTick != 0) {
        {
            L2_GUARD(npc->m_PledgeContributeSect);
            db.RequestSavePledgeContribution(npc->m_data->m_nRulingAgit, false, true, &npc->m_PledgeContributes);
            npc->m_PledgeContributes.clear();
        }
    }
}

CSPointer<CNPC> CNpcBattleAgit::GetBossNpc() { return m_osBossNpc.GetObjectW(); }
