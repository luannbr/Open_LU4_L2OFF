#include "TeamBattleAgit.h"
#include "DB.h"
#include "Door.h"
#include "NPC.h"
#include "NpcServer.h"
#include "Pledge.h"
#include "PledgeFunction.h"
#include "PledgeStuff.h"
#include "Protocol/src/SystemMessageIds.h"
#include "TeamBattleFlag.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/IOCPacket.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace {


const char16_t* ResidenceName(const CResidenceData* const data) noexcept {
    return data != nullptr ? data->m_sResidenceName.data() : u"";
}

int ResidenceId(const CResidenceData* const data) noexcept { return data != nullptr ? data->m_nResidenceID : 0; }


void SendMessageToEntries(const std::set<std::uint32_t>& entries, const unsigned int messageId) {
    for (const std::uint32_t userDatabaseId : entries) {
        CSPointer<User> user =
            g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(userDatabaseId)));
        if (user && user->GetSocket() != nullptr) {
            user->GetSocket()->SendSystemMessage(messageId);
        }
    }
}

void ClearInfo(TeamBattleInfo& info) {
    info.nPledgeDbId = 0;
    info.tPropose = 0;
    info.nNpcType = 0;
    info.mapEntryPledgeMember.clear();
}

void ReleaseFlags(std::vector<CTeamBattleFlag*>& flags) {
    for (CTeamBattleFlag* const flag : flags) {
        flag->Disappear();
        L2_RELEASE(flag, ObjectRefType::CreateDelete);
    }
    flags.clear();
}

wchar_t kBattleRoyalFlagNames[TBC_MAX][24] = {
    L"azit_contest_flag_a",
    L"azit_contest_flag_d",
    L"azit_contest_flag_c",
    L"azit_contest_flag_b",
    L"azit_contest_flag_e",
};
wchar_t kFinalWinnerFlagName[] = L"azit_contest_flag_winner";

} // namespace

CTeamBattleAgit::CTeamBattleAgit() {
    // Current L2Server 0x00ae1dd4.
    L2_GUARD_FUNCTION();
    m_nWinnerPledgeId = -1;
    m_SubStatus = TBSS_WAITING;
    m_bSiegeContinued = false;
    m_nBattleRoyalTimeout = 1200;
    m_nWaitingTimeout = 300;
    m_nBattleRoyalBeginTime = 0;
    m_nWaitingBattleRoyalBeginTime = 0;
    m_nWaitingFinalBeginTime = 0;
    m_nFinalTimeout = 2400;
    m_nFinalBeginTime = 0;
}

CTeamBattleAgit::~CTeamBattleAgit() = default;

bool CTeamBattleAgit::IsTeamBattleAgit() const { return true; }

bool CTeamBattleAgit::IsBattleTypeAgit() const { return true; }

CTeamBattleAgit* CTeamBattleAgit::CastTeamBattleAgit() const { return const_cast<CTeamBattleAgit*>(this); }

void CTeamBattleAgit::StartTimer() {
    // Current L2Server 0x00adf5a8.
    L2_GUARD_FUNCTION();
    if (IsUnderSiege()) {
        m_bSiegeContinued = true;
        if (m_data != nullptr) {
            m_data->m_ResidenceState = RS_STANDBY;
        }
    } else {
        const ResidenceState state = m_data != nullptr ? m_data->m_ResidenceState : RS_MAX;
        if (state != RS_STANDBY) {
            ChangeResidenceState(RS_STANDBY);
        }
    }
    CWorldAgit::StartTimer();
}

TEAM_BATTLE_COLOR CTeamBattleAgit::GetColor(int pledgeDatabaseId, bool& alreadyRegistered) {
    L2_GUARD_FUNCTION();
    TEAM_BATTLE_COLOR result = TBC_NOT_DETERMINED;
    TEAM_BATTLE_COLOR empty = TBC_NOT_DETERMINED;
    alreadyRegistered = false;

    L2_GUARD(m_TeamBattleSect);
    for (int index = 0; index < TBC_MAX; ++index) {
        TeamBattleInfo& entry = m_TeamBattleEntryInfo[index];
        if (empty == TBC_NOT_DETERMINED && entry.nPledgeDbId == 0) {
            empty = static_cast<TEAM_BATTLE_COLOR>(index);
        }
        if (entry.nPledgeDbId == static_cast<std::uint32_t>(pledgeDatabaseId)) {
            alreadyRegistered = true;
            result = static_cast<TEAM_BATTLE_COLOR>(index);
        }
    }

    if (!alreadyRegistered && empty != TBC_NOT_DETERMINED) {
        m_TeamBattleEntryInfo[static_cast<int>(empty)].nPledgeDbId = static_cast<std::uint32_t>(pledgeDatabaseId);
        result = empty;
    }
    return result;
}

FVector CTeamBattleAgit::GetChallengerRestartPoint(int pledgeDatabaseId) {
    L2_GUARD_FUNCTION();
    bool alreadyRegistered = false;
    const TEAM_BATTLE_COLOR color = GetColor(pledgeDatabaseId, alreadyRegistered);
    const int index = static_cast<int>(color);
    if (alreadyRegistered && index >= 0 && static_cast<std::size_t>(index) < m_ChallengerRestartPointList.size()) {
        return m_ChallengerRestartPointList[static_cast<std::size_t>(index)];
    }
    return FVector{};
}

bool CTeamBattleAgit::IsBattleRoyalEntryMember(std::uint32_t pledgeDatabaseId, std::uint32_t userDatabaseId) {
    if (pledgeDatabaseId == 0) {
        return false;
    }

    L2_GUARD(m_TeamBattleSect);
    for (const TeamBattleInfo& entry : m_TeamBattleEntryInfo) {
        if (entry.nPledgeDbId == pledgeDatabaseId) {
            return entry.mapEntryPledgeMember.find(userDatabaseId) != entry.mapEntryPledgeMember.end();
        }
    }
    return false;
}

bool CTeamBattleAgit::IsMasterEntryMember(std::uint32_t userDatabaseId) {
    L2_GUARD(m_TeamBattleSect);
    return m_TeamBattleMasterInfo.mapEntryPledgeMember.find(userDatabaseId) !=
        m_TeamBattleMasterInfo.mapEntryPledgeMember.end();
}

int CTeamBattleAgit::AddEntryPledge(
    const std::uint32_t pledgeDatabaseId, const std::int32_t proposeTime, const TEAM_BATTLE_COLOR color, const int npcType
) {
    // Current L2Server 0x00ae00a4.
    L2_GUARD_FUNCTION();
    if (static_cast<std::uint32_t>(color) >= TBC_MAX) {
        return -7;
    }

    L2_GUARD(m_TeamBattleSect);
    TeamBattleInfo* entry = nullptr;
    const std::uint32_t ownerPledgeId = m_data == nullptr ? 0 : m_data->m_nResidenceOwnerID;
    if (ownerPledgeId == pledgeDatabaseId) {
        entry = &m_TeamBattleMasterInfo;
    } else {
        entry = &m_TeamBattleEntryInfo[static_cast<int>(color)];
    }
    entry->nPledgeDbId = pledgeDatabaseId;
    entry->tPropose = proposeTime;
    entry->nNpcType = npcType;
    entry->mapEntryPledgeMember.clear();
    return static_cast<int>(color);
}

bool CTeamBattleAgit::RemoveEntryPledge(const std::uint32_t pledgeDatabaseId) {
    // Current L2Server 0x00ae01f4.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_TeamBattleSect);
    for (TeamBattleInfo& entry : m_TeamBattleEntryInfo) {
        if (entry.nPledgeDbId != pledgeDatabaseId) {
            continue;
        }
        entry.nPledgeDbId = 0;
        entry.tPropose = 0;
        entry.nNpcType = 0;
        entry.mapEntryPledgeMember.clear();
        return true;
    }
    return false;
}

bool CTeamBattleAgit::RemoveEntryPledgeMember(const std::uint32_t pledgeDatabaseId, const std::uint32_t userDatabaseId) {
    // Current L2Server 0x00ae0d50.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_TeamBattleSect);
    for (TeamBattleInfo& entry : m_TeamBattleEntryInfo) {
        if (entry.nPledgeDbId != pledgeDatabaseId) {
            continue;
        }
        entry.mapEntryPledgeMember.erase(userDatabaseId);
        return true;
    }
    return false;
}

int CTeamBattleAgit::AddEntryPledgeMember(
    const std::uint32_t userDatabaseId, const std::uint32_t pledgeDatabaseId, const std::int32_t proposeTime
) {
    // Current L2Server 0x00ae19bc.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_TeamBattleSect);

    std::map<std::uint32_t, std::int32_t>* members = nullptr;
    if (m_TeamBattleMasterInfo.nPledgeDbId == pledgeDatabaseId) {
        members = &m_TeamBattleMasterInfo.mapEntryPledgeMember;
    } else {
        for (TeamBattleInfo& entry : m_TeamBattleEntryInfo) {
            if (entry.nPledgeDbId == pledgeDatabaseId) {
                members = &entry.mapEntryPledgeMember;
                break;
            }
        }
    }

    if (members == nullptr) {
        return -3;
    }
    if (members->size() >= 0x12) {
        return -2;
    }
    (*members)[userDatabaseId] = proposeTime;
    return 0;
}

int CTeamBattleAgit::GetPledgeRegisterStatusForNpcServer(const int pledgeDatabaseId) {
    // Current L2Server 0x00adf788.
    L2_GUARD_FUNCTION();
    int firstEmpty = -1;
    int registered = -1;
    L2_GUARD(m_TeamBattleSect);
    for (int index = 0; index < TBC_MAX; ++index) {
        const std::uint32_t entryPledgeId = m_TeamBattleEntryInfo[index].nPledgeDbId;
        if (firstEmpty == -1 && entryPledgeId == 0) {
            firstEmpty = index;
        }
        if (entryPledgeId == static_cast<std::uint32_t>(pledgeDatabaseId)) {
            registered = index;
        }
    }
    if (registered != -1) {
        return registered;
    }
    return firstEmpty == -1 ? -2 : -1;
}

bool CTeamBattleAgit::IsBattleRoyalEntryPledge(const std::uint32_t pledgeDatabaseId) const {
    if (pledgeDatabaseId == 0) {
        return false;
    }
    CYieldLock& lock = const_cast<CYieldLock&>(m_TeamBattleSect);
    L2_GUARD(lock);
    for (const TeamBattleInfo& entry : m_TeamBattleEntryInfo) {
        if (entry.nPledgeDbId == pledgeDatabaseId) {
            return true;
        }
    }
    return false;
}

bool CTeamBattleAgit::IsRegisteredForSiege(CPledge* const pledge) const {
    // Current L2Server 0x00adf8ec.
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        return false;
    }
    const std::uint32_t pledgeDatabaseId = pledge->GetDbId();
    return IsBattleRoyalEntryPledge(pledgeDatabaseId) || pledgeDatabaseId == m_TeamBattleMasterInfo.nPledgeDbId;
}

void CTeamBattleAgit::OnStartInBattleField(User* const user, FVector& position, bool& started) {
    // Current L2Server 0x00adf9b4 assumes the selected
    // restart list is non-empty and consumes exactly one rand value.
    L2_GUARD_FUNCTION();
    const std::uint32_t randomValue = static_cast<std::uint32_t>(std::rand());
    if (user->m_data->m_nKarma < 0) {
        position = m_ChaoRestartPointList[randomValue % m_ChaoRestartPointList.size()];
    } else {
        position = m_OtherRestartVillageList[randomValue % m_OtherRestartVillageList.size()];
    }
    started = true;
}

FVector CTeamBattleAgit::GetBanishPoint(
    User* const user, const bool useOwnerRestart, std::set<std::uint32_t>& masterEntries
) {
    // Current L2Server 0x00adfa70 keeps the target's unchecked modulo.
    L2_GUARD_FUNCTION();
    FVector position{};
    if (useOwnerRestart && masterEntries.find(user->GetDBID()) != masterEntries.end()) {
        position = m_OwnerRestartPointList[static_cast<std::uint32_t>(std::rand()) % m_OwnerRestartPointList.size()];
    } else {
        position = m_BanishPointList[static_cast<std::uint32_t>(std::rand()) % m_BanishPointList.size()];
    }
    return position;
}

void CTeamBattleAgit::GetMasterEntry(std::set<std::uint32_t>& entries) {
    entries.clear();
    L2_GUARD(m_TeamBattleSect);
    for (const auto& member : m_TeamBattleMasterInfo.mapEntryPledgeMember) {
        entries.insert(member.first);
    }
}

void CTeamBattleAgit::GetTeamBattleEntry(std::set<std::uint32_t>& entries, const bool challengersOnly) {
    entries.clear();
    L2_GUARD(m_TeamBattleSect);
    for (const TeamBattleInfo& info : m_TeamBattleEntryInfo) {
        for (const auto& member : info.mapEntryPledgeMember) {
            entries.insert(member.first);
        }
    }
    if (!challengersOnly) {
        for (const auto& member : m_TeamBattleMasterInfo.mapEntryPledgeMember) {
            entries.insert(member.first);
        }
    }
}

void CTeamBattleAgit::BanBattleRoyalLoser(const std::uint32_t pledgeDatabaseId) {
    // Current L2Server 0x00ae32bc.
    L2_GUARD_FUNCTION();
    class TLMFIlter final : public CTLMFilterRect {
    public:
        TLMFIlter(const FVector& origin, const std::uint32_t pledgeDatabaseId, CTeamBattleAgit* const agit) :
            CTLMFilterRect(0, origin, 16384.0), m_pledgeDatabaseId(pledgeDatabaseId), m_agit(agit) {}

        bool CanDo(CObject* const object) override {
            if (!CTLMFilterRect::CanDo(object)) {
                return false;
            }
            User* const user = object->CastUser();
            return user != nullptr && user->m_data->m_nPledgeID == m_pledgeDatabaseId &&
                m_agit->IsInResidence(user->GetPos(), false);
        }

    private:
        std::uint32_t m_pledgeDatabaseId;
        CTeamBattleAgit* m_agit;
    };

    class TLMFunc final : public CTLMFunc {
    public:
        explicit TLMFunc(std::vector<User*>& users) : m_users(users) {}

        bool CanDo(CObject* const object, double) override {
            if (object->IsPC() && !object->IsDeleting() && object->m_Deleted == 0) {
                User* const user = object->CastUser();
                m_users.push_back(user);
                L2_ADD_REF(user, ObjectRefType::EventRetain);
            }
            return true;
        }

    private:
        std::vector<User*>& m_users;
    };

    std::vector<User*> users;
    TLMFIlter filter(*m_OwnerRestartPointList.begin(), pledgeDatabaseId, this);
    TLMFunc gather(users);
    FOR_ALL_NEIGHBOR_USER_RECT(&filter, &gather);
    ReserveExpelOfUsers(users);
}

void CTeamBattleAgit::BanExceptTeamBattleEntry(const bool battleRoyal) {
    // Current L2Server 0x00ae2f38.
    L2_GUARD_FUNCTION();
    std::set<std::uint32_t> entries;
    std::set<std::uint32_t> masterEntries;
    GetTeamBattleEntry(entries, battleRoyal);
    GetMasterEntry(masterEntries);

    class TLMFIlter final : public CTLMFilterRect {
    public:
        TLMFIlter(const FVector& origin, const std::set<std::uint32_t>& entries, CTeamBattleAgit* const agit) :
            CTLMFilterRect(0, origin, 16384.0), m_entries(entries), m_agit(agit) {}

        bool CanDo(CObject* const object) override {
            if (!CTLMFilterRect::CanDo(object)) {
                return false;
            }
            User* const user = object->CastUser();
            return user != nullptr && m_entries.find(user->GetDBID()) == m_entries.end() &&
                m_agit->IsInResidence(user->GetPos(), false);
        }

    private:
        const std::set<std::uint32_t>& m_entries;
        CTeamBattleAgit* m_agit;
    };

    class TLMFunc final : public CTLMFunc {
    public:
        explicit TLMFunc(std::vector<User*>& users) : m_users(users) {}

        bool CanDo(CObject* const object, double) override {
            if (object->IsPC() && !object->IsDeleting() && object->m_Deleted == 0) {
                User* const user = object->CastUser();
                m_users.push_back(user);
                L2_ADD_REF(user, ObjectRefType::EventRetain);
            }
            return true;
        }

    private:
        std::vector<User*>& m_users;
    };

    std::vector<User*> users;
    TLMFIlter filter(*m_OwnerRestartPointList.begin(), entries, this);
    TLMFunc gather(users);
    FOR_ALL_NEIGHBOR_USER_RECT(&filter, &gather);

    {
        L2_WRITE_GUARD(g_ExpelTimer);
        for (User* const user : users) {
            g_ExpelTimer.ReserveExpelWithNoLock(user, GetBanishPoint(user, battleRoyal, masterEntries));
        }
    }
}

void CTeamBattleAgit::SendWaitTimeMessage(const std::int32_t now) {
    // Current L2Server 0x00ae081c.
    L2_GUARD_FUNCTION();
    const std::int32_t remaining =
        runtime::AddInt32Wrapping(runtime::AddInt32Wrapping(m_nWaitingBattleRoyalBeginTime, m_nWaitingTimeout), -now);
    if (remaining == 60) {
        g_ptlUserMap->BroadcastToNeighborRect(
            nullptr, 0x80000, *m_OwnerRestartPointList.begin(), 0x4000, "chccc", 0x62, 0x8c6, 1, 0x14, 1
        );
    } else if (remaining == 30 || remaining == 20 || (static_cast<std::uint32_t>(remaining) - 1U) < 10U) {
        g_ptlUserMap->BroadcastToNeighborRect(
            nullptr, 0x80000, *m_OwnerRestartPointList.begin(), 0x4000, "chccc", 0x62, 0x759, 1, 0x14,
            static_cast<int>(static_cast<char>(remaining))
        );
    }
}

void CTeamBattleAgit::SendElapsedTimeMessage(const std::int32_t now, const std::int32_t endTime) {
    // Current L2Server 0x00ae0924.
    L2_GUARD_FUNCTION();
    const std::int32_t remaining = runtime::AddInt32Wrapping(endTime, -now);
    if (remaining == 60) {
        g_ptlUserMap->BroadcastToNeighborRect(
            nullptr, 0x80000, *m_OwnerRestartPointList.begin(), 0x4000, "chccc", 0x62, 0x8c4, 1, 0x14, 1
        );
    } else if (remaining == 30 || remaining == 20 || (static_cast<std::uint32_t>(remaining) - 1U) < 10U) {
        g_ptlUserMap->BroadcastToNeighborRect(
            nullptr, 0x80000, *m_OwnerRestartPointList.begin(), 0x4000, "chccc", 0x62, 0x8c5, 1, 0x14,
            static_cast<int>(static_cast<char>(remaining))
        );
    }
}

void CTeamBattleAgit::PauseSiege() {
    // Current L2Server 0x00ae04c4.
    L2_GUARD_FUNCTION();
    if (!m_bPaused) {
        m_bPaused = true;
        m_tPaused = runtime::crt::Time32Now();
        L2_GUARD(m_TeamBattleSect);
        m_setNpcInBattle.clear();
    }
}

void CTeamBattleAgit::ResumeSiege() {
    // Current L2Server 0x00ae05b4.
    L2_GUARD_FUNCTION();
    m_bPaused = false;
    const std::int32_t now = runtime::crt::Time32Now();
    if (m_tPaused != 0) {
        const std::int32_t elapsed = runtime::AddInt32Wrapping(now, -m_tPaused);
        if (elapsed > 0) {
            m_nLostTime = runtime::AddInt32Wrapping(m_nLostTime, elapsed);
        }
    }
    m_tPaused = 0;

    char packet[1024];
    int packetSize = 0;
    int count = 0;
    L2_GUARD(m_TeamBattleSect);
    if (m_SubStatus == TBSS_BATTLE_ROYAL) {
        for (int color = 0; color < TBC_MAX; ++color) {
            const TeamBattleInfo& info = m_TeamBattleEntryInfo[color];
            if (info.nPledgeDbId == 0) {
                continue;
            }
            packetSize += ::Assemble(
                packet + packetSize, static_cast<int>(sizeof(packet)) - packetSize, "ddd", color, info.nNpcType,
                info.nPledgeDbId
            );
            ++count;
        }
        m_setNpcInBattle.clear();
        g_npcserver.Send("cdSdddb", 0x3d, ResidenceId(m_data), ResidenceName(m_data), 1, 1, count, packetSize, packet);
    } else if (m_SubStatus == TBSS_FINAL) {
        if (m_TeamBattleMasterInfo.nPledgeDbId != 0) {
            packetSize += ::Assemble(
                packet + packetSize, static_cast<int>(sizeof(packet)) - packetSize, "ddd", 0,
                m_TeamBattleMasterInfo.nNpcType, m_TeamBattleMasterInfo.nPledgeDbId
            );
            ++count;
        }
        for (const TeamBattleInfo& info : m_TeamBattleEntryInfo) {
            if (info.nPledgeDbId == 0) {
                continue;
            }
            packetSize += ::Assemble(
                packet + packetSize, static_cast<int>(sizeof(packet)) - packetSize, "ddd", 1, info.nNpcType, info.nPledgeDbId
            );
            ++count;
            break;
        }
        m_setNpcInBattle.clear();
        g_npcserver.Send(
            "cdSdddb", 0x3d, ResidenceId(m_data), ResidenceName(m_data), 2, m_bSiegeContinued ? 1 : 0, count, packetSize,
            packet
        );
    }
}

void CTeamBattleAgit::OnBossNpcEnterWorld(CNPC* const npc) {
    // Current L2Server 0x00ae24d0.
    L2_GUARD_FUNCTION();
    m_setNpcInBattle.insert(npc);
    CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, static_cast<int>(npc->m_data->m_nPledgeID));
    if (!pledge || pledge->GetMaster() == nullptr) {
        return;
    }
    CLogFormatter formatter;
    formatter.WriteLog4(
        0x103, pledge->GetMaster(), nullptr, pledge->GetPledgeName(), nullptr, nullptr, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    );
}

void CTeamBattleAgit::OnBossNpcDead(CNPC* const npc) {
    // Current L2Server 0x00ae2660.
    L2_GUARD_FUNCTION();
    int winnerPledgeId = -1;
    int winnerNpcId = 0;
    {
        L2_GUARD(m_TeamBattleSect);
        m_setNpcInBattle.erase(npc);
        if (m_setNpcInBattle.size() == 1) {
            CNPC* const winnerNpc = *m_setNpcInBattle.begin();
            winnerPledgeId = static_cast<int>(winnerNpc->m_data->m_nPledgeID);
            winnerNpcId = winnerNpc->GetNPCId();
        }
    }

    const std::uint32_t deadPledgeId = npc->m_data->m_nPledgeID;
    if (deadPledgeId != 0) {
        BanBattleRoyalLoser(deadPledgeId);
    }
    if (winnerPledgeId != -1) {
        runtime::raw_atomic::CompareExchangeI32(&m_nWinnerPledgeId, winnerPledgeId, -1);
    }
    if (winnerPledgeId == 0 || winnerNpcId <= 0) {
        return;
    }

    CSPointer<CPledge> winner = CPledge::FindPledge(g_nWorldId, winnerPledgeId);
    if (!winner) {
        return;
    }
    const std::u16string pledgeName = winner->GetPledgeName();
    CLogFormatter formatter;
    formatter.WriteLog(
        0x102, winnerNpcId, winnerNpcId, 0, 0, 0, 0, 0, pledgeName.c_str(), nullptr, nullptr, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        nullptr, nullptr, nullptr, nullptr, 0
    );
}

void CTeamBattleAgit::PassSiege() {
    // Current L2Server 0x00ae130c.
    L2_GUARD_FUNCTION();
    m_SubStatus = TBSS_WAITING;
    db.RequestSetTeamBattleStatus(this, 0);

    const std::int32_t now = runtime::crt::Time32Now();
    std::int32_t nextSiege = runtime::AddInt32Wrapping(m_data != nullptr ? m_data->m_NextSiege : 0, 0x127500);
    if (nextSiege <= now) {
        nextSiege = runtime::AddInt32Wrapping(now, 0x127500);
    }
    if (m_data != nullptr) {
        m_data->m_NextSiege = nextSiege;
    }
    db.RequestSetNextAgitBattleTime(this);

    CPledge* const owner = GetResidenceOwnerPledge();
    {
        L2_GUARD(m_TeamBattleSect);
        for (TeamBattleInfo& info : m_TeamBattleEntryInfo) {
            ClearInfo(info);
        }
        m_TeamBattleMasterInfo.mapEntryPledgeMember.clear();
        if (owner == nullptr || owner->GetDbId() != m_TeamBattleMasterInfo.nPledgeDbId) {
            ClearInfo(m_TeamBattleMasterInfo);
        } else {
            m_TeamBattleMasterInfo.mapEntryPledgeMember[owner->GetMasterDbId()] = runtime::crt::Time32Now();
        }
    }
    db.RequestResetTeamBattleAgitPledge(this);

    m_nBattleRoyalTimeout = 1200;
    m_nWaitingTimeout = 300;
    m_nBattleRoyalBeginTime = 0;
    m_nWaitingBattleRoyalBeginTime = 0;
    m_nWaitingFinalBeginTime = 0;
    m_nFinalTimeout = 2400;
    m_nFinalBeginTime = 0;
    m_bSiegeContinued = false;
    m_nWinnerPledgeId = -1;

    BroadcastSystemMessageW(SystemMessageId::CLAN_HALL_SIEGE_CANCELLED_NO_PARTICIPATING_CLANS);
    if (owner != nullptr && owner->GetFunction() != nullptr &&
        owner->GetFunction()->IsEnable(PledgeFunctionType::AGIT_SIEGE)) {
        PledgeStuff::UpdateNameValueOf(owner, NameValueForAgit::SUCCESS_DEFENCE);
    }
}

void CTeamBattleAgit::EndSiege() {
    // Current L2Server 0x00ae1550.
    L2_GUARD_FUNCTION();
    MakeNPCDisable(false);
    SetBattleField(false);
    ExpelNonPledgeMember();

    const std::int32_t now = runtime::crt::Time32Now();
    std::int32_t nextSiege = runtime::AddInt32Wrapping(m_data != nullptr ? m_data->m_NextSiege : 0, 0x127500);
    if (nextSiege <= now) {
        nextSiege = runtime::AddInt32Wrapping(now, 0x127500);
    }
    if (m_data != nullptr) {
        m_data->m_NextSiege = nextSiege;
    }
    db.RequestSetNextAgitBattleTime(this);

    CPledge* const owner = GetResidenceOwnerPledge();
    {
        L2_GUARD(m_TeamBattleSect);
        if (m_nWinnerPledgeId == -1) {
            for (TeamBattleInfo& info : m_TeamBattleEntryInfo) {
                ClearInfo(info);
            }
            m_TeamBattleMasterInfo.mapEntryPledgeMember.clear();
        } else {
            TeamBattleInfo winnerInfo;
            for (TeamBattleInfo& info : m_TeamBattleEntryInfo) {
                if (info.nPledgeDbId == static_cast<std::uint32_t>(m_nWinnerPledgeId)) {
                    winnerInfo.nPledgeDbId = info.nPledgeDbId;
                    winnerInfo.tPropose = info.tPropose;
                    winnerInfo.nNpcType = info.nNpcType;
                }
                ClearInfo(info);
            }
            if (m_nWinnerPledgeId == 0 ||
                m_TeamBattleMasterInfo.nPledgeDbId == static_cast<std::uint32_t>(m_nWinnerPledgeId)) {
                m_TeamBattleMasterInfo.mapEntryPledgeMember.clear();
            } else {
                ClearInfo(m_TeamBattleMasterInfo);
                m_TeamBattleMasterInfo.nPledgeDbId = winnerInfo.nPledgeDbId;
                m_TeamBattleMasterInfo.tPropose = winnerInfo.tPropose;
                m_TeamBattleMasterInfo.nNpcType = winnerInfo.nNpcType;
            }
        }

        if (owner == nullptr || owner->GetDbId() != m_TeamBattleMasterInfo.nPledgeDbId) {
            ClearInfo(m_TeamBattleMasterInfo);
        } else {
            m_TeamBattleMasterInfo.mapEntryPledgeMember[owner->GetMasterDbId()] = runtime::crt::Time32Now();
        }
    }
    db.RequestResetTeamBattleAgitPledge(this);

    m_nBattleRoyalTimeout = 1200;
    m_nWaitingTimeout = 300;
    m_nBattleRoyalBeginTime = 0;
    m_nWaitingBattleRoyalBeginTime = 0;
    m_nWaitingFinalBeginTime = 0;
    m_nFinalTimeout = 2400;
    m_nFinalBeginTime = 0;
    m_nWinnerPledgeId = -1;

    g_npcserver.Send("cdSddd", 0x3d, ResidenceId(m_data), ResidenceName(m_data), 3, 0, 0);
    m_bSiegeContinued = false;
    {
        L2_GUARD(m_TeamBattleSect);
        ReleaseFlags(m_lstTeamBattleObject);
    }
    m_SubStatus = TBSS_WAITING;
    db.RequestSetTeamBattleStatus(this, 0);
    ChangeResidenceState(RS_STANDBY);
    m_nLostTime = 0;
    m_tPaused = 0;
    BroadcastSystemMessageW(SystemMessageId::CLAN_HALL_SIEGE_ENDED);
}

void CTeamBattleAgit::WaitBattleRoyal() {
    // Current L2Server 0x00ae0a74.
    L2_GUARD_FUNCTION();
    SetBattleField(false);
    OpenAllDoor();
    m_nWinnerPledgeId = -1;
    m_nBattleRoyalTimeout = 1200;
    m_nWaitingTimeout = 300;
    m_nBattleRoyalBeginTime = 0;
    m_nWaitingBattleRoyalBeginTime = 0;
    m_nWaitingFinalBeginTime = 0;
    m_nFinalTimeout = 2400;
    m_nFinalBeginTime = 0;
    m_nWaitingBattleRoyalBeginTime = runtime::crt::Time32Now();
    m_SubStatus = TBSS_PREPARE_BATTLE_ROYAL;
    db.RequestSetTeamBattleStatus(this, 1);

    {
        L2_GUARD(m_TeamBattleSect);
        ReleaseFlags(m_lstTeamBattleObject);
        const std::size_t count = std::min<std::size_t>(TBC_MAX, m_BattleRoyalFlagList.size());
        for (std::size_t index = 0; index < count; ++index) {
            const auto& placement = m_BattleRoyalFlagList[index];
            CTeamBattleFlag* const flag =
                new CTeamBattleFlag(kBattleRoyalFlagNames[index], placement.first, placement.second);
            m_lstTeamBattleObject.push_back(flag);
            flag->Appear();
        }
    }

    std::set<std::uint32_t> entries;
    GetTeamBattleEntry(entries, true);
    SendMessageToEntries(entries, 0x32f);
}

void CTeamBattleAgit::StartBattleRoyal() {
    // Current L2Server 0x00ae0e40.
    L2_GUARD_FUNCTION();
    SetBattleField(true);
    CloseAllDoor();
    BanExceptTeamBattleEntry(true);
    m_nWinnerPledgeId = -1;
    m_nBattleRoyalTimeout = 1200;
    m_nWaitingTimeout = 300;
    m_nBattleRoyalBeginTime = 0;
    m_nWaitingBattleRoyalBeginTime = 0;
    m_nWaitingFinalBeginTime = 0;
    m_nFinalTimeout = 2400;
    m_nFinalBeginTime = 0;
    m_nBattleRoyalBeginTime = runtime::crt::Time32Now();

    char packet[1024];
    int packetSize = 0;
    int count = 0;
    {
        L2_GUARD(m_TeamBattleSect);
        for (int color = 0; color < TBC_MAX; ++color) {
            const TeamBattleInfo& info = m_TeamBattleEntryInfo[color];
            if (info.nPledgeDbId == 0) {
                continue;
            }
            packetSize += ::Assemble(
                packet + packetSize, static_cast<int>(sizeof(packet)) - packetSize, "ddd", color, info.nNpcType,
                info.nPledgeDbId
            );
            ++count;
        }
        m_setNpcInBattle.clear();
    }
    g_npcserver.Send(
        "cdSdddb", 0x3d, ResidenceId(m_data), ResidenceName(m_data), 1, m_bSiegeContinued ? 1 : 0, count, packetSize, packet
    );
    m_bSiegeContinued = false;
    m_SubStatus = TBSS_BATTLE_ROYAL;
    db.RequestSetTeamBattleStatus(this, 2);

    std::set<std::uint32_t> entries;
    GetTeamBattleEntry(entries, true);
    SendMessageToEntries(entries, 0x315);
}

void CTeamBattleAgit::WaitFinal() {
    // Current L2Server 0x00ae21a8.
    L2_GUARD_FUNCTION();
    g_npcserver.Send("cdSddd", 0x3d, ResidenceId(m_data), ResidenceName(m_data), 3, 0, 0);
    SetBattleField(false);
    for (CDoor* const door : m_DoorList) {
        if (m_setFinalOpenDoor.find(door) == m_setFinalOpenDoor.end()) {
            door->Close(false, false);
        } else {
            door->Open(false, false);
        }
    }

    if (m_nWinnerPledgeId != -1) {
        {
            L2_GUARD(m_TeamBattleSect);
            for (TeamBattleInfo& info : m_TeamBattleEntryInfo) {
                if (info.nPledgeDbId != static_cast<std::uint32_t>(m_nWinnerPledgeId)) {
                    ClearInfo(info);
                }
            }
        }
        db.RequestResetTeamBattleAgitPledge(this);
    }
    runtime::raw_atomic::CompareExchangeI32(&m_nWinnerPledgeId, -1, -1);

    m_nBattleRoyalTimeout = 1200;
    m_nWaitingTimeout = 300;
    m_nBattleRoyalBeginTime = 0;
    m_nWaitingBattleRoyalBeginTime = 0;
    m_nWaitingFinalBeginTime = 0;
    m_nFinalTimeout = 2400;
    m_nFinalBeginTime = 0;
    m_nWaitingFinalBeginTime = runtime::crt::Time32Now();
    BanExceptTeamBattleEntry(false);
    m_SubStatus = TBSS_PREPARE_FINAL;
    db.RequestSetTeamBattleStatus(this, 3);

    std::set<std::uint32_t> entries;
    GetTeamBattleEntry(entries, false);
    SendMessageToEntries(entries, 0x317);

    {
        L2_GUARD(m_TeamBattleSect);
        ReleaseFlags(m_lstTeamBattleObject);

        const auto& winnerPlacement = m_FinalFlagList[0];
        CTeamBattleFlag* const winnerFlag =
            new CTeamBattleFlag(kFinalWinnerFlagName, winnerPlacement.first, winnerPlacement.second);
        m_lstTeamBattleObject.push_back(winnerFlag);
        winnerFlag->Appear();

        for (int color = 0; color < TBC_MAX; ++color) {
            if (m_TeamBattleEntryInfo[color].nPledgeDbId == 0) {
                continue;
            }
            const auto& challengerPlacement = m_FinalFlagList[1];
            CTeamBattleFlag* const challengerFlag =
                new CTeamBattleFlag(kBattleRoyalFlagNames[color], challengerPlacement.first, challengerPlacement.second);
            m_lstTeamBattleObject.push_back(challengerFlag);
            challengerFlag->Appear();
            break;
        }
    }
}

void CTeamBattleAgit::StartFinal() {
    // Current L2Server 0x00ae2a04.
    L2_GUARD_FUNCTION();
    m_nBattleRoyalTimeout = 1200;
    m_nWaitingTimeout = 300;
    m_nBattleRoyalBeginTime = 0;
    m_nWaitingBattleRoyalBeginTime = 0;
    m_nWaitingFinalBeginTime = 0;
    m_nFinalTimeout = 2400;
    m_nFinalBeginTime = 0;
    m_nFinalBeginTime = runtime::crt::Time32Now();
    BanExceptTeamBattleEntry(false);
    SetBattleField(true);
    CloseAllDoor();
    m_nWinnerPledgeId = -1;

    char packet[1024];
    int packetSize = 0;
    int count = 0;
    {
        L2_GUARD(m_TeamBattleSect);
        if (m_TeamBattleMasterInfo.nPledgeDbId != 0) {
            packetSize += ::Assemble(
                packet + packetSize, static_cast<int>(sizeof(packet)) - packetSize, "ddd", 0,
                m_TeamBattleMasterInfo.nNpcType, m_TeamBattleMasterInfo.nPledgeDbId
            );
            ++count;
        }
        for (const TeamBattleInfo& info : m_TeamBattleEntryInfo) {
            if (info.nPledgeDbId == 0) {
                continue;
            }
            packetSize += ::Assemble(
                packet + packetSize, static_cast<int>(sizeof(packet)) - packetSize, "ddd", 1, info.nNpcType, info.nPledgeDbId
            );
            ++count;
            break;
        }
        m_setNpcInBattle.clear();
    }
    g_npcserver.Send(
        "cdSdddb", 0x3d, ResidenceId(m_data), ResidenceName(m_data), 2, m_bSiegeContinued ? 1 : 0, count, packetSize, packet
    );
    m_bSiegeContinued = false;
    m_SubStatus = TBSS_FINAL;
    db.RequestSetTeamBattleStatus(this, 4);

    std::set<std::uint32_t> entries;
    GetTeamBattleEntry(entries, false);
    SendMessageToEntries(entries, 0x316);
}

void CTeamBattleAgit::StartSiege() {
    // Current L2Server 0x00ae3b68.
    L2_GUARD_FUNCTION();
    m_nWaitingBattleRoyalBeginTime = 0;
    m_nBattleRoyalBeginTime = 0;
    m_nWaitingFinalBeginTime = 0;
    m_nFinalBeginTime = 0;
    m_nWinnerPledgeId = -1;
    m_nBattleRoyalTimeout = 1200;
    m_nWaitingTimeout = 300;
    m_nFinalTimeout = 2400;

    if (!m_bSiegeContinued) {
        int challengerCount = 0;
        for (const TeamBattleInfo& info : m_TeamBattleEntryInfo) {
            if (info.nPledgeDbId != 0) {
                ++challengerCount;
            }
        }
        if (challengerCount < 1) {
            PassSiege();
            ChangeResidenceState(RS_STANDBY);
            m_nLostTime = 0;
            m_tPaused = 0;
            return;
        }
        BroadcastSystemMessageW(SystemMessageId::CLAN_HALL_SIEGE_STARTED);
        WaitBattleRoyal();
    } else if (m_SubStatus >= TBSS_WAITING && m_SubStatus <= TBSS_BATTLE_ROYAL) {
        BroadcastSystemMessageW(SystemMessageId::CLAN_HALL_SIEGE_STARTED);
        WaitBattleRoyal();
    } else if (m_SubStatus >= TBSS_PREPARE_FINAL && m_SubStatus <= TBSS_FINAL) {
        BroadcastSystemMessageW(SystemMessageId::CLAN_HALL_SIEGE_STARTED);
        WaitFinal();
    }

    ChangeResidenceState(RS_SIEGE);
    m_nLostTime = 0;
    m_tPaused = 0;
}

void CTeamBattleAgit::TimerExpired(const int id) {
    // Current L2Server 0x00ae350c.
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    const ResidenceState state = m_data != nullptr ? m_data->m_ResidenceState : RS_MAX;
    const std::int32_t nextSiege = m_data != nullptr ? m_data->m_NextSiege : 0;

    if ((state == RS_REGISTRATION || state == RS_STANDBY) && nextSiege <= now &&
        g_PledgeTimer.IsNpcServerAlive(CPledgeTimer::NSAT_20)) {
        StartSiege();
    } else if (state == RS_SIEGE) {
        if (!g_PledgeTimer.IsNpcServerAlive(CPledgeTimer::NSAT_20)) {
            PauseSiege();
        } else if (m_bPaused) {
            ResumeSiege();
        } else if (m_SubStatus == TBSS_WAITING) {
            StartSiege();
        } else if (m_SubStatus == TBSS_PREPARE_BATTLE_ROYAL) {
            const std::int32_t waitEnd = runtime::AddInt32Wrapping(m_nWaitingBattleRoyalBeginTime, m_nWaitingTimeout);
            if (now < waitEnd) {
                SendWaitTimeMessage(now);
            } else {
                StartBattleRoyal();
            }
        } else if (m_SubStatus == TBSS_BATTLE_ROYAL) {
            if (m_nWinnerPledgeId == -1) {
                const std::int32_t battleEnd = runtime::AddInt32Wrapping(
                    runtime::AddInt32Wrapping(m_nBattleRoyalBeginTime, m_nBattleRoyalTimeout), m_nLostTime
                );
                if (now < battleEnd) {
                    SendElapsedTimeMessage(now, battleEnd);
                } else {
                    runtime::raw_atomic::CompareExchangeI32(&m_nWinnerPledgeId, 0, -1);
                }
            }

            if (m_nWinnerPledgeId != -1) {
                const std::uint32_t masterPledgeId = m_TeamBattleMasterInfo.nPledgeDbId;
                CSPointer<CPledge> winner;
                if (m_nWinnerPledgeId != 0) {
                    winner = CPledge::FindPledge(g_nWorldId, m_nWinnerPledgeId);
                }

                if (masterPledgeId != 0) {
                    if (!winner) {
                        BroadcastSystemMessageW(SystemMessageId::PRELIMINARY_MATCH_ENDED_IN_DRAW);
                        CPledge* const owner = GetResidenceOwnerPledge();
                        if (owner != nullptr) {
                            UpdatePledgeNameValue(static_cast<int>(masterPledgeId), owner);
                        }
                        EndSiege();
                    } else {
                        BroadcastSystemMessageW(winner->GetPledgeName(), SystemMessageId::CLAN_WON_PRELIMINARY_MATCH);
                        WaitFinal();
                    }
                } else {
                    const int previousOwnerPledgeId = m_data != nullptr ? static_cast<int>(m_data->m_nResidenceOwnerID) : 0;
                    if (!winner) {
                        BroadcastSystemMessageW(SystemMessageId::CLAN_HALL_SIEGE_ENDED_IN_DRAW);
                        ChangeOwner(nullptr, 0, true);
                        UpdatePledgeNameValue(previousOwnerPledgeId, nullptr);
                    } else if (winner->GetWorldAgit() == nullptr) {
                        BroadcastSystemMessageW(winner->GetPledgeName(), SystemMessageId::CLAN_WON_NAMED_CLAN_HALL_SIEGE);
                        ChangeOwner(winner.get(), 0, true);
                        UpdatePledgeNameValue(previousOwnerPledgeId, winner.get());
                    } else {
                        BroadcastSystemMessageW(SystemMessageId::CLAN_HALL_SIEGE_ENDED_IN_DRAW);
                    }
                    EndSiege();
                }
            }
        } else if (m_SubStatus == TBSS_PREPARE_FINAL) {
            const std::int32_t waitEnd = runtime::AddInt32Wrapping(m_nWaitingFinalBeginTime, m_nWaitingTimeout);
            if (waitEnd <= now) {
                StartFinal();
            }
        } else if (m_SubStatus == TBSS_FINAL) {
            if (m_nWinnerPledgeId == -1) {
                const std::int32_t finalEnd = runtime::AddInt32Wrapping(
                    runtime::AddInt32Wrapping(m_nFinalBeginTime, m_nFinalTimeout), m_nLostTime
                );
                if (now < finalEnd) {
                    SendElapsedTimeMessage(now, finalEnd);
                } else {
                    runtime::raw_atomic::CompareExchangeI32(&m_nWinnerPledgeId, 0, -1);
                }
            }

            if (m_nWinnerPledgeId != -1) {
                CSPointer<CPledge> winner;
                if (m_nWinnerPledgeId != 0) {
                    winner = CPledge::FindPledge(g_nWorldId, m_nWinnerPledgeId);
                }
                const int previousOwnerPledgeId = m_data != nullptr ? static_cast<int>(m_data->m_nResidenceOwnerID) : 0;
                if (!winner) {
                    BroadcastSystemMessageW(SystemMessageId::CLAN_HALL_SIEGE_ENDED_IN_DRAW);
                    ChangeOwner(nullptr, 0, true);
                    UpdatePledgeNameValue(previousOwnerPledgeId, nullptr);
                } else if (m_nWinnerPledgeId == previousOwnerPledgeId || winner->GetWorldAgit() == nullptr) {
                    BroadcastSystemMessageW(winner->GetPledgeName(), SystemMessageId::CLAN_WON_NAMED_CLAN_HALL_SIEGE);
                    ChangeOwner(winner.get(), 0, true);
                    UpdatePledgeNameValue(previousOwnerPledgeId, winner.get());
                } else {
                    BroadcastSystemMessageW(SystemMessageId::CLAN_HALL_SIEGE_ENDED_IN_DRAW);
                }
                EndSiege();
            }
        }
    }
    CWorldAgit::TimerExpired(id);
}
