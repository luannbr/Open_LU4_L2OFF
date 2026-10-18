#include "SiegeAgit.h"
#include <optional>
#include "Alliance.h"
#include "BattleCamp.h"
#include "CConfig.h"
#include "DB.h"
#include "Door.h"
#include "NPC.h"
#include "NpcServer.h"
#include "Pledge.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/TextRuntime.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/IOCPacket.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"

#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {


const char16_t* ResidenceName(const CResidenceData* const data) noexcept {
    return data != nullptr ? data->m_sResidenceName.data() : u"";
}

int ResidenceId(const CResidenceData* const data) noexcept { return data != nullptr ? data->m_nResidenceID : 0; }

int ResidenceOwnerId(const CResidenceData* const data) noexcept {
    return data != nullptr ? static_cast<int>(data->m_nResidenceOwnerID) : 0;
}

void SendSiegeState(const CResidenceData* const data, const int active, const int resumed) {
    g_npcserver.Send("cdSddd", 0x31, ResidenceId(data), ResidenceName(data), ResidenceOwnerId(data), active, resumed);
}

} // namespace

CSiegeAgit::CSiegeAgit() {
    // Current L2Server 0x00a60e40. BufForCaching performs the five exact
    // zero fills before the guarded scalar assignments below.
    L2_GUARD_FUNCTION();
    m_nWinnerPledgeId = -1;
    m_bAttackerListCached = false;
    m_nAttackerListTotalPage = 0;
    m_nSiegePeriod = 0xe10;
    m_tAttackerListLastCached = 0;
}

CSiegeAgit::~CSiegeAgit() = default;

bool CSiegeAgit::IsSiegeAgit() const { return true; }

bool CSiegeAgit::IsBattleTypeAgit() const { return true; }

CSiegeAgit* CSiegeAgit::CastSiegeAgit() const { return const_cast<CSiegeAgit*>(this); }

CSPointer<CNPC> CSiegeAgit::GetBossNpc() {
    // Current compiled SiegeAgit.h body 0x006b66c4.
    return m_osBossNpc.GetObjectW();
}

void CSiegeAgit::StartTimer() {
    // Current L2Server 0x00a5f9fc.
    L2_GUARD_FUNCTION();
    if (IsUnderSiege()) {
        m_bSiegeContinued = true;
        if (m_data != nullptr) {
            m_data->m_ResidenceState = RS_STANDBY;
        }
    }
    CWorldAgit::StartTimer();
}

void CSiegeAgit::PauseSiege() {
    // Current L2Server 0x00a5fac4.
    L2_GUARD_FUNCTION();
    if (!m_bPaused) {
        MakeDoorsBreakable(false);
        m_bPaused = true;
    }
}

void CSiegeAgit::ResumeSiege() {
    // Current L2Server 0x00a5fb78.
    L2_GUARD_FUNCTION();
    MakeDoorsBreakable(true);
    SendSiegeState(m_data, 1, 1);
    m_bPaused = false;
}

void CSiegeAgit::StartSiege() {
    // Current L2Server 0x00a60178.
    L2_GUARD_FUNCTION();
    ChangeOwner(nullptr, 0, true);
    ExpelNonPledgeMember();
    MakeNPCDisable(true);
    db.RequestInstallResidenceDeco(*this, 0, 0, 0);
    db.RequestInstallResidenceDeco(*this, 1, 0, 0);
    db.RequestInstallResidenceDeco(*this, 3, 0, 0);
    SetBattleField(true);
    SendSiegeState(m_data, 1, m_bSiegeContinued ? 1 : 0);
    if (!m_bSiegeContinued) {
        db.RequestResetContribution(*this);
    }
    for (CDoor* const door : m_DoorList) {
        if (m_bSiegeContinued) {
            door->ValidateDamageGrade();
            door->ValidatePlaneCollision();
        }
        door->Close(false, false);
    }
    MakeDoorsBreakable(true);
    if (m_bSiegeContinued) {
        db.RequestLoadBattleCamp(this, 0);
    }
    m_bSiegeContinued = false;
    m_nWinnerPledgeId = -1;
    BroadcastSystemMessageW(SystemMessageId::CLAN_HALL_SIEGE_STARTED);
}

void CSiegeAgit::EndSiege() {
    // Current L2Server 0x00a61158.
    L2_GUARD_FUNCTION();
    SendSiegeState(m_data, 0, 0);

    CSPointer<CPledge> winner;
    if (m_nWinnerPledgeId >= 1) {
        winner = CPledge::FindPledge(g_nWorldId, m_nWinnerPledgeId);
    }

    const int previousOwnerId = ResidenceOwnerId(m_data);
    if (!winner) {
        BroadcastSystemMessageW(SystemMessageId::CLAN_HALL_SIEGE_ENDED_IN_DRAW);
        ChangeOwner(nullptr, 0, true);
    } else {
        BroadcastSystemMessageW(winner->GetPledgeName(), SystemMessageId::CLAN_WON_NAMED_CLAN_HALL_SIEGE);
        ChangeOwner(winner.get(), 0, true);
    }
    UpdatePledgeNameValue(previousOwnerId, winner.get());
    BroadcastSystemMessageW(SystemMessageId::CLAN_HALL_SIEGE_ENDED);
    ExpelNonPledgeMember();
    MakeNPCDisable(false);
    for (CDoor* const door : m_DoorList) {
        door->ResetDoorState(false, 1.0);
    }
    MakeDoorsBreakable(false);
    SetBattleField(false);
    if (m_data != nullptr) {
        m_data->m_NextSiege = runtime::AddInt32Wrapping(m_data->m_NextSiege, 0x127500);
    }
    db.RequestSetNextAgitBattleTime(this);
    m_osBossNpc.SetObject(nullptr);
    db.RequestResetContribution(*this);
    CBattleCampDb::inst().SetDisable(m_data->m_nResidenceID);
    db.RequestUninstallBattleCampBy(this);
    FreeSiegePledgeList();
    db.RequestResetSiegeAgitPledge(this);
}

void CSiegeAgit::PassSiege() {
    // Current L2Server 0x00a61488.
    L2_GUARD_FUNCTION();
    FreeSiegePledgeList();
    db.RequestResetSiegeAgitPledge(this);
    if (m_data != nullptr) {
        m_data->m_NextSiege = runtime::AddInt32Wrapping(m_data->m_NextSiege, 0x127500);
    }
    db.RequestSetNextAgitBattleTime(this);
    BroadcastSystemMessageW(SystemMessageId::CLAN_HALL_SIEGE_CANCELLED_NO_PARTICIPATING_CLANS);
}

void CSiegeAgit::TimerExpired(const int id) {
    // Current L2Server 0x00a60c38.
    L2_GUARD_FUNCTION();
    const int state = m_data != nullptr ? static_cast<int>(m_data->m_ResidenceState) : static_cast<int>(RS_MAX);
    const std::int32_t now = runtime::crt::Time32Now();

    if (state >= 0) {
        if (state < RS_STANDBY) {
            const std::int32_t nextSiege = m_data != nullptr ? m_data->m_NextSiege : 0;
            if (runtime::AddInt32Wrapping(nextSiege, -0x1c20) < now) {
                ChangeResidenceState(RS_STANDBY);
                BroadcastSystemMessageW(SystemMessageId::CLAN_HALL_SIEGE_REGISTRATION_ENDED);
            }
        } else if (state == RS_STANDBY) {
            const std::int32_t nextSiege = m_data != nullptr ? m_data->m_NextSiege : 0;
            if (!(((runtime::AddInt32Wrapping(nextSiege, -10) < now) && (now <= nextSiege)) || (now <= nextSiege)) &&
                g_PledgeTimer.IsNpcServerAlive(CPledgeTimer::NSAT_20)) {
                if (GetAttackerCount() < 1) {
                    PassSiege();
                    ChangeResidenceState(RS_REGISTRATION);
                } else {
                    StartSiege();
                    ChangeResidenceState(RS_SIEGE);
                }
            }
        } else if (state == RS_SIEGE) {
            if (!g_PledgeTimer.IsNpcServerAlive(CPledgeTimer::NSAT_20)) {
                PauseSiege();
            } else {
                const std::int32_t nextSiege = m_data != nullptr ? m_data->m_NextSiege : 0;
                const std::int32_t endTime =
                    runtime::AddInt32Wrapping(nextSiege, static_cast<std::int32_t>(m_nSiegePeriod));
                if (m_bPaused) {
                    ResumeSiege();
                } else if (m_nWinnerPledgeId == -1 && now < endTime) {
                    SendElapsedTimeMessage(now, endTime);
                } else {
                    EndSiege();
                    ChangeResidenceState(RS_REGISTRATION);
                }
            }
        }
    }
    CWorldAgit::TimerExpired(id);
}

bool CSiegeAgit::IsRegisteredForSiege(CPledge* const pledge) const {
    // Current L2Server 0x00a605a0.
    L2_GUARD_FUNCTION();
    return const_cast<CSiegeAgit*>(this)->IsAttackerPledge(pledge);
}

void CSiegeAgit::OnStartInBattleField(User* const user, FVector& position, bool& started) {
    // Current L2Server 0x00a607b8 assumes both restart lists
    // are non-empty when their matching branch is selected.
    L2_GUARD_FUNCTION();
    if (user->m_data->m_nWorldId != g_nWorldId) {
        return;
    }

    if (user->m_data->m_nKarma < 0) {
        position = m_ChaoRestartPointList[static_cast<std::size_t>(std::rand()) % m_ChaoRestartPointList.size()];
    } else {
        position = m_OtherRestartVillageList[static_cast<std::size_t>(std::rand()) % m_OtherRestartVillageList.size()];
    }
    started = true;

    if (user->m_data->m_nPledgeID == 0) {
        return;
    }
    CSPointer<CPledge> pledge = CPledge::FindPledge(user->m_data->m_nWorldId, static_cast<int>(user->m_data->m_nPledgeID));
    if (!pledge || !IsAttackerPledge(pledge.get())) {
        return;
    }
    CSPointer<CBattleCamp> battleCamp = pledge->GetBattleCamp();
    if (!battleCamp || !battleCamp->IsEnabled()) {
        return;
    }
    const int residenceId = m_data != nullptr ? m_data->m_nResidenceID : 0;
    if (battleCamp->GetBaseResidenceId() == residenceId) {
        position = battleCamp->GetTeleportPos();
        started = true;
    }
}

void CSiegeAgit::SendElapsedTimeMessage(const std::int32_t now, const std::int32_t endTime) {
    // Current L2Server 0x00a60648.
    L2_GUARD_FUNCTION();
    const std::int32_t remaining = runtime::AddInt32Wrapping(endTime, -now);
    if (remaining == 60) {
        g_ptlUserMap->BroadcastToNeighborRect(
            nullptr, 0x80000, m_OwnerRestartPointList[0], 0x4000, "chccc", 0x62, 0x8c4, 1, 0x14, 1
        );
        return;
    }
    if (remaining == 30 || remaining == 20 || (remaining - 1U) < 10U) {
        g_ptlUserMap->BroadcastToNeighborRect(
            nullptr, 0x80000, m_OwnerRestartPointList[0], 0x4000, "chcch", 0x62, 0x8c5, 1, 0x15,
            static_cast<int>(static_cast<std::int16_t>(remaining))
        );
    }
}

void CSiegeAgit::OnBossNpcEnterWorld(CNPC* const npc) {
    // Current L2Server 0x00a600c8 assumes a non-null NPC.
    L2_GUARD_FUNCTION();
    m_osBossNpc.SetObject(npc);
    ++npc->m_nPledgeContributeSaveTick;
}

void CSiegeAgit::OnBossNpcDead(CNPC* const npc) {
    // Current L2Server 0x00a60380 assumes a non-null NPC/data.
    L2_GUARD_FUNCTION();
    if (npc->m_nPledgeContributeSaveTick != 0) {
        {
            L2_GUARD(npc->m_PledgeContributeSect);
            db.RequestSavePledgeContribution(npc->m_data->m_nRulingAgit, true, true, &npc->m_PledgeContributes);
            npc->m_PledgeContributes.clear();
        }
    }
}

bool CSiegeAgit::IsAttackerPledge(CPledge* pledge) {
    L2_GUARD_FUNCTION();

    L2_GUARD(m_SiegeAgitSect);
    const std::uint32_t pledgeDatabaseId = pledge->GetDbId();
    return m_mapAttackerPledge.find(pledgeDatabaseId) != m_mapAttackerPledge.end();
}

int CSiegeAgit::GetAttackerCount() {
    // Current L2Server 0x00a5fff4 truncates the target map size to int.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_SiegeAgitSect);
    return static_cast<int>(m_mapAttackerPledge.size());
}

void CSiegeAgit::FreeSiegePledgeList() {
    // Current L2Server 0x00a60fdc keeps the residence owner, including its
    // previous score, and clears every other attacker.
    L2_GUARD_FUNCTION();
    std::uint32_t ownerPledgeId = 0;
    std::int32_t ownerScore = 0;
    CPledge* const owner = GetResidenceOwnerPledge();

    L2_GUARD(m_SiegeAgitSect);
    if (owner != nullptr) {
        const std::uint32_t candidate = owner->GetDbId();
        const auto found = m_mapAttackerPledge.find(candidate);
        if (found != m_mapAttackerPledge.end()) {
            ownerPledgeId = found->first;
            ownerScore = found->second;
        }
    }
    m_mapAttackerPledge.clear();
    if (ownerPledgeId != 0) {
        m_mapAttackerPledge[ownerPledgeId] = ownerScore;
    }
    m_bAttackerListCached = false;
}

void CSiegeAgit::SendCastleSiegeInfo(CUserSocket* const socket, User* const user) {
    // Current L2Server 0x00a5fc7c.
    L2_GUARD_FUNCTION();
    CPledge* const owner = GetResidenceOwnerPledge();
    const std::int32_t now = runtime::crt::Time32Now();
    if (owner == nullptr) {
        socket->Send(
            "cdddSSdSddd", 0xc9, ResidenceId(m_data), 0, 0, u"", u"", 0, u"", now,
            m_data != nullptr ? m_data->m_NextSiege : 0, 0
        );
        return;
    }

    int canRegister = 0;
    if (user->m_data->m_nPledgeID == owner->GetDbId() &&
        (user->m_data->m_bPledgeMaster || user->HavePledgePower(PP_REGISTER_CASTLE_WAR))) {
        canRegister = 1;
    }

    std::uint32_t allianceDatabaseId = 0;
    const char16_t* allianceName = u"";
    if (CAlliance* const alliance = owner->GetAlliance()) {
        allianceDatabaseId = alliance->m_data->m_nDbId;
        allianceName = alliance->m_data->m_sAllianceName;
    }
    socket->Send(
        "cdddSSdSddd", 0xc9, ResidenceId(m_data), canRegister, static_cast<int>(owner->GetDbId()), owner->GetPledgeName(),
        owner->GetMasterName(), static_cast<int>(allianceDatabaseId), allianceName, now,
        m_data != nullptr ? m_data->m_NextSiege : 0, 0
    );
}

void CSiegeAgit::SendSiegeDefenderList(CUserSocket* const socket, User* const user) {
    // Current L2Server 0x00a5fed4.
    L2_GUARD_FUNCTION();
    int canRegister = 0;
    if (user->m_data->m_nPledgeID != 0 && user->m_data->m_nPledgeID == m_data->m_nResidenceOwnerID &&
        (user->m_data->m_bPledgeMaster || user->HavePledgePower(PP_REGISTER_CASTLE_WAR))) {
        canRegister = 1;
    }
    socket->Send("cdddddd", 0xcb, m_data->m_nResidenceID, canRegister, 0, 0, 0, 0);
}

void CSiegeAgit::GetAttackerPledge(std::vector<CPledge*>& pledges, std::vector<int>& scores) {
    // Current L2Server 0x00a61574.
    L2_GUARD_FUNCTION();
    pledges.clear();
    scores.clear();
    for (const auto& entry : m_mapAttackerPledge) {
        CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, static_cast<int>(entry.first));
        if (!pledge) {
            continue;
        }
        pledges.push_back(pledge.get());
        scores.push_back(entry.second);
    }
}

void CSiegeAgit::SendSiegeAttackerList(CUserSocket* const socket, User* const user) {
    // Current L2Server 0x00a6174c.
    L2_GUARD_FUNCTION();
    const int residenceId = m_data->m_nResidenceID;
    const std::uint32_t userPledgeId = user->m_data->m_nPledgeID;
    int canRegister = 0;
    if (userPledgeId != 0 && userPledgeId == m_data->m_nResidenceOwnerID &&
        (user->m_data->m_bPledgeMaster || user->HavePledgePower(PP_REGISTER_CASTLE_WAR))) {
        canRegister = 1;
    }

    const std::int32_t now = runtime::crt::Time32Now();
    std::optional<std::vector<CPledge*>> pledgesStorage;
    std::optional<std::vector<int>> scoresStorage;
    {
        L2_GUARD(m_SiegeAgitSect);
        const bool rebuild = !m_bAttackerListCached || now - m_tAttackerListLastCached > 0x3b;

        if (rebuild) {
            std::vector<CPledge*>& pledges = pledgesStorage.emplace();
            std::vector<int>& scores = scoresStorage.emplace();
            GetAttackerPledge(pledges, scores);

            const int attackerCount = static_cast<int>(pledges.size());
            const int lastPageCount = attackerCount % 0x40;
            m_nAttackerListTotalPage = attackerCount / 0x40 + 1;
            if (m_nAttackerListTotalPage > 0 && lastPageCount == 0) {
                --m_nAttackerListTotalPage;
            }

            if (m_nAttackerListTotalPage == 0) {
                socket->Send("cdddddd", 0xca, residenceId, canRegister, 0, 0, 0, 0);
            } else {
                char payload[0x2008]{};
                int payloadSize = 0;
                int pageIndex = 0;
                int recordNumber = 1;
                for (int index = 0; index < attackerCount; ++index) {
                    CPledge* const pledge = pledges[static_cast<std::size_t>(index)];
                    (void)pledge->GetDbId();
                    const char16_t* const pledgeName = pledge->GetPledgeName();
                    const char16_t* const pledgeMasterName = pledge->GetPledgeMasterName();
                    std::uint32_t allianceDatabaseId = 0;
                    std::uint32_t allianceCrestId = 0;
                    const char16_t* allianceName = u"";
                    const char16_t* allianceMasterName = u"";
                    if (CAlliance* const alliance = pledge->GetAlliance()) {
                        allianceDatabaseId = alliance->m_data->m_nDbId;
                        allianceName = alliance->m_data->m_sAllianceName;
                        if (CPledge* const masterPledge = alliance->GetMasterPledge()) {
                            allianceMasterName = masterPledge->GetPledgeMasterName();
                        }
                        allianceCrestId = alliance->m_data->m_nCrestId;
                    }

                    payloadSize += ::Assemble(
                        payload + payloadSize, 0x2004 - payloadSize, "dSSdddSSd", static_cast<int>(pledge->Id()), pledgeName,
                        pledgeMasterName, static_cast<int>(pledge->GetCrestId()), scores[static_cast<std::size_t>(index)],
                        static_cast<int>(allianceDatabaseId), allianceName, allianceMasterName,
                        static_cast<int>(allianceCrestId)
                    );

                    if ((recordNumber & 0x3f) == 0) {
                        BufForCaching& cache = m_bufForAttackerListCaching[pageIndex];
                        cache.nBufSize = ::Assemble(cache.pBuf, 0x2004, "ddb", attackerCount, 0x40, payloadSize, payload);
                        socket->Send(
                            "cddddb", 0xca, residenceId, canRegister, m_nAttackerListTotalPage, pageIndex, cache.nBufSize,
                            cache.pBuf
                        );
                        payloadSize = 0;
                        ++pageIndex;
                    }
                    ++recordNumber;
                }

                if (lastPageCount != 0) {
                    BufForCaching& cache = m_bufForAttackerListCaching[pageIndex];
                    cache.nBufSize =
                        ::Assemble(cache.pBuf, 0x2004, "ddb", attackerCount, lastPageCount, payloadSize, payload);
                    socket->Send(
                        "cddddb", 0xca, residenceId, canRegister, m_nAttackerListTotalPage, pageIndex, cache.nBufSize,
                        cache.pBuf
                    );
                }
            }
            m_bAttackerListCached = true;
            m_tAttackerListLastCached = now;
            return;
        }

        if (m_nAttackerListTotalPage == 0) {
            socket->Send("cdddddd", 0xca, residenceId, canRegister, 0, 0, 0, 0);
        } else if (m_nAttackerListTotalPage > 0) {
            for (int pageIndex = 0; pageIndex < m_nAttackerListTotalPage; ++pageIndex) {
                const BufForCaching& cache = m_bufForAttackerListCaching[pageIndex];
                socket->Send(
                    "cddddb", 0xca, residenceId, canRegister, m_nAttackerListTotalPage, pageIndex, cache.nBufSize, cache.pBuf
                );
            }
        }
    }
}
