#include "Agit.h"

#include "AccountJobFinishSignal.h"
#include "CConfig.h"
#include "Castle.h"
#include "DB.h"
#include "Door.h"
#include "Pledge.h"
#include "PledgeFunction.h"
#include "PledgeStuff.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"

#include "Shared/src/DXPacket.h"
#include "Shared/src/GIP.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Time32.h"

#include <cstdint>
#include <ctime>

namespace NameValueForAgit {

// Current L2Server .data 0x015c1cc8..0x015c1cd4.
int SUCCESS_FROM_NPC = 65000;
int SUCCESS_FROM_PC = 55000;
int SUCCESS_DEFENCE = 25000;
int FAILED_DEFENCE = -20000;

} // namespace NameValueForAgit

namespace {

class NameValueUpdaterAtAgitResult {
public:
    NameValueUpdaterAtAgitResult(const int previousOwnerPledgeId, CPledge* const pledge, const CWorldAgit* const agit) :
        m_previousOwnerPledgeId(previousOwnerPledgeId),
        m_pledge(pledge),
        m_isBattleTypeAgit(false),
        m_isSiegeAgit(false),
        m_isCompetitionAgit(false) {
        L2_GUARD_FUNCTION();
        if (agit == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]assert falied: %s",
            //     L"Agit.cpp",
            //     0x24,
            //     L"(pAgit != NULL)")
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]assert falied: %s",
            //     L"Agit.cpp",
            //     0x24,
            //     L"(pAgit != NULL)")
            L2_LOG_ERROR(
                Log,
                "Cannot classify the clan hall for reputation updates: the hall is null (previous owner clan %d); default "
                "hall-type flags are retained.",
                m_previousOwnerPledgeId
            );
            return;
        }
        m_isBattleTypeAgit = agit->IsBattleTypeAgit();
        m_isSiegeAgit = agit->IsSiegeAgit();
        m_isCompetitionAgit = agit->IsCompetitionAgit();
    }

    void Do() {
        L2_GUARD_FUNCTION();
        if (IsWinnerChanged()) {
            UpdateNameValueOfLoser();
            UpdateNameValueOfWinner();
        } else if (m_isBattleTypeAgit && !m_isSiegeAgit) {
            UpdateNameValueOf(m_pledge, NameValueForAgit::SUCCESS_DEFENCE);
        }
    }

private:
    bool IsWinnerChanged() const {
        L2_GUARD_FUNCTION();
        return m_pledge == nullptr || static_cast<int>(m_pledge->GetDbId()) != m_previousOwnerPledgeId;
    }

    static void UpdateNameValueOf(CPledge* const pledge, const int amount) {
        L2_GUARD_FUNCTION();
        if (pledge != nullptr && pledge->GetFunction()->IsEnable(PledgeFunctionType::AGIT_SIEGE)) {
            PledgeStuff::UpdateNameValueOf(pledge, amount);
        }
    }

    void UpdateNameValueOfWinner() const {
        L2_GUARD_FUNCTION();
        const int amount =
            m_previousOwnerPledgeId == 0 ? NameValueForAgit::SUCCESS_FROM_NPC : NameValueForAgit::SUCCESS_FROM_PC;
        UpdateNameValueOf(m_pledge, amount);
    }

    void UpdateNameValueOfLoser() const {
        L2_GUARD_FUNCTION();
        if (m_previousOwnerPledgeId == 0 || m_isCompetitionAgit) {
            return;
        }
        CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, m_previousOwnerPledgeId);
        UpdateNameValueOf(pledge.get(), NameValueForAgit::FAILED_DEFENCE);
    }

    int m_previousOwnerPledgeId;
    CPledge* m_pledge;
    bool m_isBattleTypeAgit;
    bool m_isSiegeAgit;
    bool m_isCompetitionAgit;
    [[maybe_unused]] std::byte m_padding13To18[5]{};
};


} // namespace

CWorldAgit::CWorldAgit() {
    // Current L2Server 0x0045384c.
    L2_GUARD_FUNCTION();
    m_pParentCastle = nullptr;
}

CWorldAgit::~CWorldAgit() {
    // Current L2Server 0x00453b08.
    m_pParentCastle = nullptr;
}

bool CWorldAgit::IsWorldAgit() const { return true; }

CWorldAgit* CWorldAgit::CastWorldAgit() const { return const_cast<CWorldAgit*>(this); }

bool CWorldAgit::IsAuctionAgit() const { return false; }

bool CWorldAgit::IsTeamBattleAgit() const { return false; }

bool CWorldAgit::IsNpcBattleAgit() const { return false; }

bool CWorldAgit::IsSiegeAgit() const { return false; }

bool CWorldAgit::IsMiniGameAgit() const { return false; }

bool CWorldAgit::IsCompetitionAgit() const { return false; }

bool CWorldAgit::IsBaseCampAgit() const { return false; }

bool CWorldAgit::IsBattleTypeAgit() const { return false; }

CAuctionAgit* CWorldAgit::CastAuctionAgit() const { return nullptr; }

CTeamBattleAgit* CWorldAgit::CastTeamBattleAgit() const { return nullptr; }

CNpcBattleAgit* CWorldAgit::CastNpcBattleAgit() const { return nullptr; }

CSiegeAgit* CWorldAgit::CastSiegeAgit() const { return nullptr; }

CMiniGameAgit* CWorldAgit::CastMiniGameAgit() const { return nullptr; }

CCompeteAgit* CWorldAgit::CastCompeteAgit() const { return nullptr; }

CBaseCampAgit* CWorldAgit::CastBaseCampAgit() const { return nullptr; }

void CWorldAgit::OnBossNpcEnterWorld(CNPC*) {}

void CWorldAgit::OnBossNpcDead(CNPC*) {}

void CWorldAgit::InitWorldAgit() {
    // Current L2Server 0x00453b20.
    L2_GUARD_FUNCTION();
    db.RequestLoadAllAgit();
    if (g_JobFinishSignal.m_nMustLoadAgit < 1) {
        // Log.Add(LOG_NORMAL, L"Load Success[Agit], count: 0")
        // L2_LOG_INFO(Log, L"Load Success[Agit], count: 0")
        L2_LOG_INFO(
            Log,
            "Clan-hall startup skips the completion wait: expected database count is %d; proceeding with the startup "
            "notification.",
            g_JobFinishSignal.m_nMustLoadAgit
        );
        g_JobFinishSignal.NotifyFinish(JFT_LOAD_AGIT);
        CPledge::SetFlagReservePledge(PRLT_AGIT);
    } else if (!g_JobFinishSignal.WaitFinish(JFT_LOAD_AGIT, 10)) {
        // Log.Add(LOG_ERROR, L"Problem occurred in Loading Agit")
        // L2_LOG_ERROR(Log, L"Problem occurred in Loading Agit")
        L2_LOG_ERROR(
            Log,
            "Clan-hall startup continues without load confirmation after a wait with a 10-second timeout; expected database "
            "count %d.",
            g_JobFinishSignal.m_nMustLoadAgit
        );
    } else {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Load Success[Agit], count: %d",
        //     g_JobFinishSignal.m_nMustLoadAgit)
        // L2_LOG_INFO(Log,
        //     L"Load Success[Agit], count: %d",
        //     g_JobFinishSignal.m_nMustLoadAgit)
        L2_LOG_INFO(
            Log, "Clan-hall load completion signal received; expected database count %d.", g_JobFinishSignal.m_nMustLoadAgit
        );
    }
}

void CWorldAgit::Set(const int ownerPledgeId, const int residenceState, const int nextSiege) {
    // Current L2Server 0x00453c3c.
    L2_GUARD_FUNCTION();
    m_data->m_nResidenceOwnerID = static_cast<std::uint32_t>(ownerPledgeId);
    m_data->m_ResidenceState = static_cast<ResidenceState>(residenceState);
    m_data->m_NextSiege = nextSiege;
}

std::int32_t CWorldAgit::GetAgitFirstSiegeDate() {
    // Current L2Server 0x00453cfc.
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    const std::tm* const localTime = runtime::crt::LocalTime32(now);
    return now - ((localTime->tm_hour * 60 + localTime->tm_min) * 60 + localTime->tm_sec) - localTime->tm_wday * 86400 +
        0x10f950;
}

void CWorldAgit::StartTimer() {
    // Current L2Server 0x00453dbc.
    L2_GUARD_FUNCTION();
    if (!IsAuctionAgit() && !IsCompetitionAgit() && m_data->m_NextSiege == 0) {
        m_data->m_NextSiege = GetAgitFirstSiegeDate();
        db.RequestSetNextAgitBattleTime(this);
    }
    SetParentCastle();
    OnDecoSet();
    AddTimer(IsMiniGameAgit() ? 1000 : 10000, 0);
}

void CWorldAgit::TimerExpired(int) {
    // Current L2Server 0x00453ee4.
    L2_GUARD_FUNCTION();
    const std::time_t time64 = std::time(nullptr);
    DoDecoCheck(static_cast<std::int32_t>(time64));
    DoDecoNpcCheck(runtime::crt::Time32Now());
    AddTimer(1000, 0);
}

void CWorldAgit::OnReplyLoadAgitPacket() {
    // Current L2Server 0x00453fb0.
    L2_GUARD_FUNCTION();
    if (IsTeamBattleAgit()) {
        db.RequestLoadTeamBattleAgit(*this);
    } else if (IsSiegeAgit()) {
        db.RequestLoadSiegeAgit(CastSiegeAgit());
    } else if (IsMiniGameAgit()) {
        db.RequestLoadMiniGameAgit(*this);
    } else if (IsCompetitionAgit()) {
        db.RequestLoadCompeteAgit(*this);
    } else {
        StartTimer();
        if (g_JobFinishSignal.SetAgitLoaded()) {
            CPledge::SetFlagReservePledge(PRLT_AGIT);
        }
    }
}

void CWorldAgit::StartSiege() { L2_GUARD_FUNCTION(); }

void CWorldAgit::EndSiege() { L2_GUARD_FUNCTION(); }

void CWorldAgit::PassSiege() { L2_GUARD_FUNCTION(); }

void CWorldAgit::PauseSiege() { L2_GUARD_FUNCTION(); }

void CWorldAgit::ResumeSiege() { L2_GUARD_FUNCTION(); }

void CWorldAgit::ChangeOwner(CPledge* const pledge, const int reason, const bool announce) {
    // Current L2Server 0x00454550.
    L2_GUARD_FUNCTION();
    const int previousOwnerPledgeId = static_cast<int>(m_data->m_nResidenceOwnerID);
    const int newOwnerPledgeId = pledge == nullptr ? 0 : static_cast<int>(pledge->GetDbId());
    const char16_t* previousPledgeName = u"";

    if (announce) {
        CheckAttendedAuction(newOwnerPledgeId);
    }

    if (previousOwnerPledgeId != newOwnerPledgeId) {
        if (previousOwnerPledgeId != 0) {
            CSPointer<CPledge> previousPledge = CPledge::FindPledge(g_nWorldId, previousOwnerPledgeId);
            if (!previousPledge) {
                db.RequestSetPledgeInfo(g_nWorldId, static_cast<unsigned int>(previousOwnerPledgeId), 7, 0);
                // Log.Add(
                //     LOG_ERROR,
                //     L"owner pledge not existed[%d]",
                //     previousOwnerPledgeId)
                // L2_LOG_ERROR(Log,
                //     L"owner pledge not existed[%d]",
                //     previousOwnerPledgeId)
                L2_LOG_ERROR(
                    Log,
                    "Clan hall %d: previous owner clan %d is unavailable; clearing its hall association was requested from "
                    "the database. Ownership change to clan %d continues.",
                    m_data != nullptr ? m_data->m_nResidenceID : 0, previousOwnerPledgeId, newOwnerPledgeId
                );
            } else {
                previousPledge->SetWorldAgit(nullptr);
                previousPledge->Save(7);
                previousPledge->UpdateInfoWindowPacket();
                previousPledge->UnequipMemberCastleItem();
                previousPledgeName = previousPledge->GetPledgeName();
            }
        }

        if (pledge == nullptr) {
            SetPledge(nullptr);
        } else {
            SetPledge(pledge);
            pledge->SetWorldAgit(this);
            pledge->Save(7);
            pledge->UpdateInfoWindowPacket();

            CResetAllResidenceDeco resetDeco;
            resetDeco.nNpcIndex = -1;
            resetDeco.nUserIndex = -1;
            resetDeco.nAgitId = m_data->m_nResidenceID;
            db.RequestTPacketProcess(g_nWorldId, 3, static_cast<int>(sizeof(resetDeco)), resetDeco);

            CResetAllResidenceDecoNpc resetNpcDeco(previousOwnerPledgeId, m_data->m_nResidenceID);
            db.RequestTPacketProcess(g_nWorldId, 0x3d, static_cast<int>(sizeof(resetNpcDeco)), resetNpcDeco);

            if (IsBattleTypeAgit()) {
                Log.Add(
                    LOG_AUDIT, u"%I64d,,,,,,,,%s,%s,,,,,,%I64d,%I64d,%I64d,,,,,,,,", static_cast<std::int64_t>(0x596),
                    previousPledgeName, pledge->GetPledgeName(), static_cast<std::int64_t>(previousOwnerPledgeId),
                    static_cast<std::int64_t>(m_data->m_nResidenceID), static_cast<std::int64_t>(newOwnerPledgeId)
                );
            }

            const std::time_t now = std::time(nullptr);
            GIP._Add(
                L"CMD_secretBaseInfo(%d, %d, %d, %d, %d", g_nWorldId, m_data->m_nResidenceID,
                static_cast<int>(pledge->GetDbId()), IsAuctionAgit() ? 1 : 0, static_cast<std::int64_t>(now)
            );
        }
    }

    m_data->m_ResidenceType = reason;
    db.ReqOwnAgit(this, static_cast<int>(m_data->m_nResidenceOwnerID));
}

void CWorldAgit::UpdatePledgeNameValue(const int previousOwnerPledgeId, CPledge* const pledge) {
    // Current L2Server 0x00454a50.
    L2_GUARD_FUNCTION();
    NameValueUpdaterAtAgitResult updater(previousOwnerPledgeId, pledge, this);
    updater.Do();
}

void CWorldAgit::OpenAllDoor() {
    // Current L2Server 0x00454b18.
    L2_GUARD_FUNCTION();
    for (CDoor* const door : m_DoorList) {
        door->Open(false, false);
    }
}

void CWorldAgit::CloseAllDoor() {
    // Current L2Server 0x00454bd4.
    L2_GUARD_FUNCTION();
    for (CDoor* const door : m_DoorList) {
        door->Close(false, false);
    }
}

CCastle* CWorldAgit::GetParentCastle() const { return m_pParentCastle; }

void CWorldAgit::SetParentCastle() {
    // Current L2Server 0x00453a30.
    L2_GUARD_FUNCTION();
    if (m_data->m_nParentResidenceId == 0) {
        m_pParentCastle = nullptr;
        return;
    }
    CResidence* const residence = g_ResidenceDB.GetResidence(m_data->m_nParentResidenceId);
    m_pParentCastle = residence == nullptr ? nullptr : residence->CastCastle();
}

void CWorldAgit::ExpelNonPledgeMember() {
    // Current L2Server 0x00454c90 dereferences the first
    // owner-restart position without an empty-container gate.
    L2_GUARD_FUNCTION();
    std::vector<User*> users;

    class TLMFunc final : public CTLMFunc {
    public:
        TLMFunc(CPledge* const ownerPledge, CWorldAgit* const worldAgit, std::vector<User*>& users) :
            m_ownerPledge(ownerPledge), m_worldAgit(worldAgit), m_users(users) {}

        bool CanDo(CObject* const object, double) override {
            if (!object->IsPC()) {
                return true;
            }
            User* const user = object->CastUser();
            if (user == nullptr) {
                return true;
            }
            if (object->IsPC() && (object->IsDeleting() || object->m_Deleted != 0)) {
                return true;
            }

            if (m_ownerPledge == nullptr) {
                if (!m_worldAgit->IsInResidence(user->GetPos(), true)) {
                    return true;
                }
            } else {
                if (static_cast<int>(m_ownerPledge->GetDbId()) == static_cast<int>(user->m_data->m_nPledgeID)) {
                    return true;
                }
                if (!m_worldAgit->IsInResidence(user->GetPos(), true)) {
                    return true;
                }
            }

            m_users.push_back(user);
            L2_ADD_REF(user, ObjectRefType::EventRetain);
            return true;
        }

    private:
        CPledge* m_ownerPledge;
        CWorldAgit* m_worldAgit;
        std::vector<User*>& m_users;
    };

    CTLMFilterRect filter(0, *m_OwnerRestartPointList.begin(), 16384.0);
    TLMFunc function(GetResidenceOwnerPledge(), this, users);
    FOR_ALL_NEIGHBOR_USER_RECT(&filter, &function);
    ReserveExpelOfUsers(users);
}
