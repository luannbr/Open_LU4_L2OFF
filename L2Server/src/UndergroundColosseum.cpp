#include "UndergroundColosseum.h"
#include "DB.h"
#include "NPC.h"
#include "NpcServer.h"
#include "Object.h"
#include "Party.h"
#include "Protocol/src/SystemMessageIds.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/platform/Runtime.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>

namespace {


User* FindUser(std::uint32_t userId) {
    CObject* const object = CObject::GetObjectW(userId);
    return object != nullptr && object->IsPC() ? object->CastUser() : nullptr;
}

CParty* FindParty(std::uint32_t partyId) { return g_PartySmartIdManager.GetObjectW(partyId); }

CNPC* FindNpc(std::uint32_t npcId) {
    CObject* const object = CObject::GetObjectW(npcId);
    return object != nullptr && object->IsNPC() ? object->CastNPC() : nullptr;
}

constexpr std::int32_t kResurrectionPoints[5][2][9][3] = {
    {
        {{-86433, -46655, -11496}, {-86882, -48441, -11496}, {-84499, -49128, -11496}, {-83776, -47943, -11496},
            {-85517, -47668, -11496}, {-85664, -47805, -11496}, {-85706, -48000, -11496}, {-85666, -48197, -11496},
            {-85487, -47911, -11496}},
        {{-86066, -46491, -11496}, {-86837, -49123, -11496}, {-83812, -49250, -11496}, {-83645, -47764, -11496},
            {-84954, -47807, -11496}, {-84909, -47993, -11496}, {-84963, -48202, -11496}, {-85113, -48348, -11496},
            {-85150, -48063, -11496}},
    },
    {
        {{-85823, -52669, -11496}, {-84280, -53771, -11496}, {-82764, -51737, -11496}, {-83744, -50461, -11496},
            {-84509, -52150, -11496}, {-84418, -52340, -11496}, {-84248, -52443, -11496}, {-84054, -52472, -11496},
            {-84253, -52292, -11496}},
        {{-85716, -52234, -11496}, {-83565, -53805, -11496}, {-82516, -51012, -11496}, {-83615, -50635, -11496},
            {-84172, -51686, -11496}, {-83968, -51709, -11496}, {-83803, -51828, -11496}, {-83713, -52022, -11496},
            {-83981, -51847, -11496}},
    },
    {
        {{-79745, -53977, -11496}, {-78311, -52783, -11496}, {-79750, -50746, -11496}, {-81277, -51335, -11529},
            {-79868, -52545, -11496}, {-79680, -52511, -11496}, {-79516, -52388, -11496}, {-79445, -52206, -11496},
            {-79723, -52356, -11496}},
        {{-80133, -53731, -11496}, {-77966, -52125, -11496}, {-80430, -50326, -11529}, {-81084, -51249, -11496},
            {-80222, -52094, -11496}, {-80144, -51920, -11496}, {-79984, -51793, -11496}, {-79775, -51759, -11496},
            {-79929, -51988, -11496}},
    },
    {
        {{-76648, -48627, -11496}, {-77359, -46902, -11496}, {-79810, -47590, -11496}, {-79636, -49285, -11496},
            {-78054, -48315, -11496}, {-78027, -48122, -11496}, {-78093, -47934, -11496}, {-78240, -47789, -11496},
            {-78165, -48101, -11496}},
        {{-77008, -48935, -11496}, {-77909, -46393, -11496}, {-80260, -48239, -11496}, {-79649, -49092, -11496},
            {-78592, -47981, -11496}, {-78740, -48376, -11496}, {-78802, -48183, -11496}, {-78771, -47981, -11496},
            {-78655, -47828, -11496}},
    },
    {
        {{-80792, -44106, -11496}, {-82613, -44144, -11496}, {-82820, -46741, -11496}, {-81121, -47099, -11496},
            {-81518, -45291, -11496}, {-81694, -45199, -11496}, {-81895, -45200, -11496}, {-82067, -45304, -11496},
            {-81808, -45352, -11496}},
        {{-80571, -44476, -11496}, {-83277, -44538, -11496}, {-82352, -47370, -11496}, {-81315, -47056, -11496},
            {-81508, -45854, -11496}, {-81682, -45953, -11496}, {-81890, -45954, -11496}, {-82075, -45863, -11496},
            {-81768, -45798, -11496}},
    },
};

} // namespace

CUCRecordBoardData::CUCRecordBoardData(std::int32_t userId, const char16_t* userName) :
    m_nUserId(0),
    m_targetPadding4(0),
    m_nKillCount(0),
    m_nDeathCount(0),
    m_dwDieTime(0),
    m_targetPadding3c(0) {
    Init(static_cast<std::uint32_t>(userId), userName);
}

CUCRecordBoardData::~CUCRecordBoardData() = default;

void CUCRecordBoardData::Init(std::uint32_t userId, const char16_t* userName) {
    m_nUserId = userId;
    m_strUserName = userName ? userName : u"";
    m_nKillCount = 0;
    m_nDeathCount = 0;
    m_dwDieTime = 0;
}

void CUCRecordBoardData::SetUserDieTime(std::uint32_t dieTime) { m_dwDieTime = dieTime; }

std::uint32_t CUCRecordBoardData::GetUserDieTime() const { return m_dwDieTime; }

void CUCRecordBoardData::IncDeathCount() { ++m_nDeathCount; }
void CUCRecordBoardData::IncKillCount() { ++m_nKillCount; }

CUCRecordBoard::CUCRecordBoard() {
    // Current L2Server 0x00b01084 duplicates the reset body instead of
    // delegating to the separately guarded Init owner.
    L2_GUARD_FUNCTION();
    m_nBlueTeamTotalKillCount = 0;
    m_nRedTeamTotalKillCount = 0;
    m_nBlueTeamTotalDeathCount = 0;
    m_nRedTeamTotalDeathCount = 0;
    for (CUCRecordBoardData* record : m_vecBlueTeam) {
        delete record;
    }
    for (CUCRecordBoardData* record : m_vecRedTeam) {
        delete record;
    }
    m_vecBlueTeam.clear();
    m_vecRedTeam.clear();
}

CUCRecordBoard::~CUCRecordBoard() { Init(); }

void CUCRecordBoard::Init() {
    // Current L2Server 0x00b00870.
    L2_GUARD_FUNCTION();
    m_nBlueTeamTotalKillCount = 0;
    m_nRedTeamTotalKillCount = 0;
    m_nBlueTeamTotalDeathCount = 0;
    m_nRedTeamTotalDeathCount = 0;
    for (CUCRecordBoardData* record : m_vecBlueTeam) {
        delete record;
    }
    for (CUCRecordBoardData* record : m_vecRedTeam) {
        delete record;
    }
    m_vecBlueTeam.clear();
    m_vecRedTeam.clear();
}

void CUCRecordBoard::AddNewRecordData(int team, User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    CUCRecordBoardData* const record = new CUCRecordBoardData(static_cast<std::int32_t>(user->Id()), user->GetName());
    if (team == 1) {
        m_vecBlueTeam.push_back(record);
    } else if (team == 2) {
        m_vecRedTeam.push_back(record);
    } else {
        delete record;
    }
}

void CUCRecordBoard::UserDie(User* user, User* enemy) {
    L2_GUARD_FUNCTION();
    CUCRecordBoardData* record = GetRecordData(user);
    if (record != nullptr) {
        record->IncDeathCount();
        record->SetUserDieTime(runtime::GetTickCount());
    }
    record = GetRecordData(enemy);
    if (record != nullptr) {
        record->IncKillCount();
    }
    Summation();
}

void CUCRecordBoard::OnHandOverPartyMaster(int team, User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    UCRecordBoardDataVec* records = nullptr;
    if (team == 1) {
        records = &m_vecBlueTeam;
    } else if (team == 2) {
        records = &m_vecRedTeam;
    }
    if (records == nullptr) {
        return;
    }
    for (std::size_t index = 0; index < records->size(); ++index) {
        if (runtime::CheckedVectorAt((*records), index)->m_nUserId == user->Id()) {
            std::swap(runtime::CheckedVectorAt((*records), 0), runtime::CheckedVectorAt((*records), index));
        }
    }
}

CUCRecordBoardData* CUCRecordBoard::GetRecordData(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return nullptr;
    }
    const std::uint32_t userId = user->Id();
    for (CUCRecordBoardData* record : m_vecBlueTeam) {
        if (record->m_nUserId == userId) {
            return record;
        }
    }
    for (CUCRecordBoardData* record : m_vecRedTeam) {
        if (record->m_nUserId == userId) {
            return record;
        }
    }
    return nullptr;
}

void CUCRecordBoard::Summation() {
    L2_GUARD_FUNCTION();
    m_nBlueTeamTotalKillCount = 0;
    m_nRedTeamTotalKillCount = 0;
    m_nBlueTeamTotalDeathCount = 0;
    m_nRedTeamTotalDeathCount = 0;
    for (const CUCRecordBoardData* record : m_vecBlueTeam) {
        m_nBlueTeamTotalKillCount += record->m_nKillCount;
        m_nBlueTeamTotalDeathCount += record->m_nDeathCount;
    }
    for (const CUCRecordBoardData* record : m_vecRedTeam) {
        m_nRedTeamTotalKillCount += record->m_nKillCount;
        m_nRedTeamTotalDeathCount += record->m_nDeathCount;
    }
}

int CUCRecordBoard::GetWinnerTeamIndex() {
    if (m_nBlueTeamTotalKillCount > m_nRedTeamTotalKillCount) {
        return 1;
    }
    if (m_nBlueTeamTotalKillCount < m_nRedTeamTotalKillCount) {
        return 2;
    }
    if (m_nBlueTeamTotalDeathCount < m_nRedTeamTotalDeathCount) {
        return 1;
    }
    return m_nBlueTeamTotalDeathCount > m_nRedTeamTotalDeathCount ? 2 : 1;
}

void CUCRecordBoard::SendCurrentRecordForSystemMessage(User* user) {
    L2_GUARD_FUNCTION();

    if (user == nullptr || user->GetSocket() == nullptr) {
        return;
    }
    char16_t line[256];
    constexpr const char16_t* kRecordFormat = u"%s : %d kill, %d death";
    user->SendSystemMessage(u"SYS", u"===== Current Record =====");
    user->SendSystemMessage(u"SYS", u"===== <Blue Team> =====");
    for (const CUCRecordBoardData* record : m_vecBlueTeam) {
        FormatUtf16To(
            line, std::size(line), kRecordFormat, record->m_strUserName, record->m_nKillCount, record->m_nDeathCount
        );
        user->SendSystemMessage(u"SYS", line);
    }
    FormatUtf16To(
        line, std::size(line), u"Total : %d kill, %d death", m_nBlueTeamTotalKillCount, m_nBlueTeamTotalDeathCount
    );
    user->SendSystemMessage(u"SYS", line);
    user->SendSystemMessage(u"SYS", u"===== <Red Team> =====");
    for (const CUCRecordBoardData* record : m_vecRedTeam) {
        FormatUtf16To(
            line, std::size(line), kRecordFormat, record->m_strUserName, record->m_nKillCount, record->m_nDeathCount
        );
        user->SendSystemMessage(u"SYS", line);
    }
    FormatUtf16To(line, std::size(line), u"Total : %d kill, %d death", m_nRedTeamTotalKillCount, m_nRedTeamTotalDeathCount);
    user->SendSystemMessage(u"SYS", line);
}

int CUCRecordBoard::MakeCurrentRecordPacket(char* packet, int packetSize, int type, int first, int second) {
    L2_GUARD_FUNCTION();
    int length =
        ::Assemble(packet, packetSize, "ddddd", type, first, second, m_nBlueTeamTotalKillCount, m_nRedTeamTotalKillCount);
    length += ::Assemble(packet + length, packetSize - length, "d", static_cast<int>(m_vecBlueTeam.size()));
    for (const CUCRecordBoardData* record : m_vecBlueTeam) {
        length += ::Assemble(
            packet + length, packetSize - length, "Sdd", record->m_strUserName.c_str(), record->m_nKillCount,
            record->m_nDeathCount
        );
    }
    length += ::Assemble(packet + length, packetSize - length, "d", static_cast<int>(m_vecRedTeam.size()));
    for (const CUCRecordBoardData* record : m_vecRedTeam) {
        length += ::Assemble(
            packet + length, packetSize - length, "Sdd", record->m_strUserName.c_str(), record->m_nKillCount,
            record->m_nDeathCount
        );
    }
    return length;
}

void CUCRecordBoard::SendCurrentRecordForUI(User* user, int type) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->GetSocket() == nullptr) {
        return;
    }
    char packet[1024]{};
    const int length = MakeCurrentRecordPacket(packet, static_cast<int>(sizeof(packet)), type, 0, 0);
    if (length > 0) {
        user->GetSocket()->Send("chb", 0xfe, 0x7f, length, packet);
    }
}

CUndergroundColosseum::CUndergroundColosseum(PVP_MATCH_TYPE type) :
    CPVPMatch2(type),
    m_nBlueTeamResurrectionTowerId(0),
    m_nRedTeamResurrectionTowerId(0) {
    m_dwIgnoreStraightWinTime = 120000;
}

CUndergroundColosseum::~CUndergroundColosseum() = default;

bool CUndergroundColosseum::IsUC() const { return true; }
CUndergroundColosseum* CUndergroundColosseum::CastUC() { return this; }
PVPMatchType::Enum CUndergroundColosseum::GetPVPMatchType() const { return PVPMatchType::UNDERGROUND_COLOSSEUM; }

bool CUndergroundColosseum::CheckRegisterLevel(int level) {
    L2_GUARD_FUNCTION();
    switch (m_eType) {
        case PVPMT_UGC_40:
            return level >= 40 && level <= 49;
        case PVPMT_UGC_50:
            return level >= 50 && level <= 59;
        case PVPMT_UGC_60:
            return level >= 60 && level <= 69;
        case PVPMT_UGC_70:
            return level >= 70 && level <= 79;
        case PVPMT_UGC_UL:
        case PVPMT_UGC_EVENT:
            return true;
        default:
            return false;
    }
}

bool CUndergroundColosseum::CheckDistance(FVector& first, FVector& second, int distance) {
    L2_GUARD_FUNCTION();
    return !CPVPMatchSystem::inst().IsCheckPartyMemberDist() || CPVPMatch::CheckDistance(first, second, distance);
}

bool CUndergroundColosseum::CheckBannedUser(User* user) {
    L2_GUARD_FUNCTION();
    return !CPVPMatchSystem::inst().IsCheckPartyMemberDebuff() ||
        (user != nullptr && user->m_data->GetAbnormalLevel(ABNS_FOR_GAME) == -1);
}

PVPMatchResult CUndergroundColosseum::CheckRegisterParty(std::uint32_t partyId) {
    L2_GUARD_FUNCTION();
    CParty* const party = FindParty(partyId);
    if (party == nullptr) {
        return PVPMR_INVALID_PARTY_ID;
    }
    if (CPVPMatchSystem::inst().IsCheckPartyMemberCount() && party->GetMemberCount() > 6) {
        return PVPMR_INVALID_MEMBER_NUM;
    }
    std::vector<User*> members;
    party->GetAllMember(members);
    CNPC* const managerNpc = FindNpc(GetManagerNpcId());
    if (managerNpc == nullptr) {
        return PVPMR_INVALID_MATCH;
    }
    FVector managerPosition = managerNpc->GetPos();
    for (User* member : members) {
        if (member == nullptr) {
            continue;
        }
        if (CPVPMatchSystem::inst().IsCheckPartyMemberLevel() && !CheckRegisterLevel(member->Level())) {
            return PVPMR_INVALID_MEMBER_LEVEL;
        }
        if (m_eType != PVPMT_UGC_EVENT) {
            FVector memberPosition = member->GetPos();
            if (!CheckDistance(managerPosition, memberPosition, 1500)) {
                return PVPMR_INVALID_MEMBER_DIST;
            }
        }
        if (!CheckBannedUser(member)) {
            return PVPMR_BANNED_PARTY_MEMBER;
        }
    }
    return PVPMR_SUCCESS;
}

bool CUndergroundColosseum::IsRegisteredParty(std::uint32_t partyId) {
    L2_GUARD_FUNCTION();
    const bool registered = [&] {
        L2_GUARD(m_Lock);
        const bool registered =
            std::find(m_listRegisteredParties.begin(), m_listRegisteredParties.end(), static_cast<std::int32_t>(partyId)) !=
            m_listRegisteredParties.end();
        return registered;
    }();
    return registered;
}

int CUndergroundColosseum::GetRegisteredPartyIndex(std::uint32_t partyId) {
    L2_GUARD_FUNCTION();
    for (std::size_t index = 0; index < m_listRegisteredParties.size(); ++index) {
        if (static_cast<std::uint32_t>(runtime::CheckedVectorAt(m_listRegisteredParties, index)) == partyId) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

PVPMatchResult CUndergroundColosseum::RegisterParty(UCMaxParty maxParty, std::uint32_t partyId) {
    L2_GUARD_FUNCTION();

    ValidateParties();
    if (IsRegisteredParty(partyId)) {
        return PVPMR_ALREADY_REGISTERED;
    }
    if (m_listRegisteredParties.size() > static_cast<std::size_t>(maxParty)) {
        return PVPMR_FULL;
    }
    const PVPMatchResult result = CheckRegisterParty(partyId);
    if (result == PVPMR_SUCCESS) {
        m_listRegisteredParties.push_back(static_cast<std::int32_t>(partyId));

        CParty* const party = FindParty(partyId);
        User* const master = party != nullptr ? party->GetMaster() : nullptr;
        if (master != nullptr) {
            const int matchType = static_cast<int>(m_eType);
            const int memberCount = party->GetMemberCount();
            CSharedCreatureData* const masterData = master->m_data;
            const char16_t* const accountNameData = master->m_sAccountName.data();
            const char16_t* const masterName = master->GetName();
            const int partyIndex = GetRegisteredPartyIndex(partyId);
            const std::uint32_t accountId = master->GetAccountID();
            const std::uint32_t databaseId = master->GetDBID();
            const std::wstring accountName = Utf16ToWide(accountNameData);
            // Log.AddT(
            //     LOG_BOTH,
            //     L"%I64d,%I64d,%I64d,,,,,,,,,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
            //     INT64_C(0x37d),
            //     static_cast<std::int64_t>(databaseId),
            //     static_cast<std::int64_t>(accountId),
            //     static_cast<std::int64_t>(masterData->m_nClass),
            //     static_cast<std::int64_t>(masterData->m_nLevel),
            //     static_cast<std::int64_t>(memberCount),
            //     static_cast<std::int64_t>(matchType),
            //     static_cast<std::int64_t>(partyIndex),
            //     masterName,
            //     accountName.c_str())
            Log.AddT(
                LOG_BOTH, u"%I64d,%I64d,%I64d,,,,,,,,,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,", INT64_C(0x37d),
                static_cast<std::int64_t>(databaseId), static_cast<std::int64_t>(accountId),
                static_cast<std::int64_t>(masterData->m_nClass), static_cast<std::int64_t>(masterData->m_nLevel),
                static_cast<std::int64_t>(memberCount), static_cast<std::int64_t>(matchType),
                static_cast<std::int64_t>(partyIndex), masterName, accountName.c_str()
            );
        }
    }
    return result;
}

void CUndergroundColosseum::ValidateParties() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        auto it = m_listRegisteredParties.begin();
        while (it != m_listRegisteredParties.end()) {
            if (FindParty(static_cast<std::uint32_t>(*it)) == nullptr) {
                it = m_listRegisteredParties.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void CUndergroundColosseum::UnregisterParty(std::uint32_t partyId, int reason) {
    L2_GUARD_FUNCTION();

    if (m_nLastWinnerPartyId == partyId) {
        m_nLastWinnerPartyId = 0;
        m_nStraightWinCount = 0;
    }
    ValidateParties();
    {
        L2_GUARD(m_Lock);
        const auto it =
            std::find(m_listRegisteredParties.begin(), m_listRegisteredParties.end(), static_cast<std::int32_t>(partyId));
        if (it != m_listRegisteredParties.end()) {
            m_listRegisteredParties.erase(it);
        }
    }

    CParty* const party = FindParty(partyId);
    User* const master = party != nullptr ? party->GetMaster() : nullptr;
    if (master != nullptr) {
        const int matchType = static_cast<int>(m_eType);
        const int memberCount = party->GetMemberCount();
        CSharedCreatureData* const masterData = master->m_data;
        const char16_t* const accountNameData = master->m_sAccountName.data();
        const char16_t* const masterName = master->GetName();
        const std::uint32_t accountId = master->GetAccountID();
        const std::uint32_t databaseId = master->GetDBID();
        const std::wstring accountName = Utf16ToWide(accountNameData);
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,,,,,,,,,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
        //     INT64_C(0x37e),
        //     static_cast<std::int64_t>(databaseId),
        //     static_cast<std::int64_t>(accountId),
        //     static_cast<std::int64_t>(masterData->m_nClass),
        //     static_cast<std::int64_t>(masterData->m_nLevel),
        //     static_cast<std::int64_t>(memberCount),
        //     static_cast<std::int64_t>(matchType),
        //     static_cast<std::int64_t>(reason),
        //     masterName,
        //     accountName.c_str())
        Log.AddT(
            LOG_BOTH, u"%I64d,%I64d,%I64d,,,,,,,,,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,", INT64_C(0x37e),
            static_cast<std::int64_t>(databaseId), static_cast<std::int64_t>(accountId),
            static_cast<std::int64_t>(masterData->m_nClass), static_cast<std::int64_t>(masterData->m_nLevel),
            static_cast<std::int64_t>(memberCount), static_cast<std::int64_t>(matchType), static_cast<std::int64_t>(reason),
            masterName, accountName.c_str()
        );
    }
}

void CUndergroundColosseum::OnDismissParty(CParty* party) {
    L2_GUARD_FUNCTION();
    if (party == nullptr) {
        return;
    }
    const std::uint32_t partyId = party->Id();
    if (partyId == m_nBlueTeamPartyId) {
        EndMatch(m_nRedTeamPartyId, m_nBlueTeamPartyId, 2);
    } else if (partyId == m_nRedTeamPartyId) {
        EndMatch(m_nBlueTeamPartyId, m_nRedTeamPartyId, 2);
    }
}

void CUndergroundColosseum::OnHandOverPartyMaster(CParty* party, User* newMaster) {
    L2_GUARD_FUNCTION();
    if (party != nullptr) {
        m_RecordBoard.OnHandOverPartyMaster(party->m_data->GetPVPMatchTeamIndex(), newMaster);
    }
}

int CUndergroundColosseum::GetRegisteredPartiesPacket(char* packet, int packetSize) {
    L2_GUARD_FUNCTION();
    int length = ::Assemble(packet, packetSize, "d", static_cast<int>(m_listRegisteredParties.size()));
    for (const int partyId : m_listRegisteredParties) {
        length += ::Assemble(packet + length, packetSize - length, "d", partyId);
    }
    length += ::Assemble(packet + length, packetSize - length, "d", static_cast<int>(m_nLastWinnerPartyId));
    return length;
}

bool CUndergroundColosseum::StartMatchByBuilder() {
    L2_GUARD_FUNCTION();
    const bool started = [&] {
        L2_GUARD(m_Lock);
        bool started = false;
        if (m_listRegisteredParties.size() == 2) {
            const std::uint32_t blue = static_cast<std::uint32_t>(runtime::CheckedVectorAt(m_listRegisteredParties, 0));
            const std::uint32_t red = static_cast<std::uint32_t>(runtime::CheckedVectorAt(m_listRegisteredParties, 1));
            if (CheckRegisterParty(blue) == PVPMR_SUCCESS && CheckRegisterParty(red) == PVPMR_SUCCESS &&
                StartMatchWithParty(blue, red)) {
                g_npcserver.Send("cdddd", 0x72, GetManagerNpcId(), blue, red, 1);
                started = true;
            }
        }
        return started;
    }();
    return started;
}

void CUndergroundColosseum::RegisterTeamToRecordBoad(int team) {
    L2_GUARD_FUNCTION();
    std::uint32_t partyId = 0;
    if (team == 1) {
        partyId = m_nBlueTeamPartyId;
    } else if (team == 2) {
        partyId = m_nRedTeamPartyId;
    } else {
        return;
    }
    CParty* const party = FindParty(partyId);
    if (party == nullptr) {
        return;
    }
    std::vector<User*> members;
    party->GetAllMember(members);
    for (User* member : members) {
        if (member != nullptr) {
            RefreshUser(member);
            m_RecordBoard.AddNewRecordData(team, member);
        }
    }
}

bool CUndergroundColosseum::StartMatchWithParty(std::uint32_t bluePartyId, std::uint32_t redPartyId) {
    L2_GUARD_FUNCTION();

    CParty* const blueParty = FindParty(bluePartyId);
    CParty* const redParty = FindParty(redPartyId);
    if (blueParty == nullptr || redParty == nullptr || !IsRegisteredParty(bluePartyId) || !IsRegisteredParty(redPartyId)) {
        return false;
    }
    m_nBlueTeamPartyId = bluePartyId;
    blueParty->m_data->SetPVPMatchStart(1);
    m_nRedTeamPartyId = redPartyId;
    redParty->m_data->SetPVPMatchStart(2);
    m_dwStartTime = runtime::GetTickCount();
    m_bGivePVPPoint = CPVPMatchSystem::inst().IsInGivePVPPointTime();
    m_RecordBoard.Init();
    RegisterTeamToRecordBoad(1);
    RegisterTeamToRecordBoad(2);
    SendCurrentRecordForUIToAllUser(0, 0, 0);
    m_dwEndTime = m_dwStartTime + 600000;

    CParty* const currentBlueParty = FindParty(bluePartyId);
    User* const blueMaster = currentBlueParty != nullptr ? currentBlueParty->GetMaster() : nullptr;
    if (blueMaster != nullptr) {
        const int matchType = static_cast<int>(m_eType);
        const int memberCount = currentBlueParty->GetMemberCount();
        CSharedCreatureData* const masterData = blueMaster->m_data;
        const char16_t* const accountNameData = blueMaster->m_sAccountName.data();
        const char16_t* const masterName = blueMaster->GetName();
        const std::uint32_t accountId = blueMaster->GetAccountID();
        const std::uint32_t databaseId = blueMaster->GetDBID();
        const std::wstring accountName = Utf16ToWide(accountNameData);
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,,,,,,,,,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
        //     INT64_C(0x37f),
        //     static_cast<std::int64_t>(databaseId),
        //     static_cast<std::int64_t>(accountId),
        //     static_cast<std::int64_t>(masterData->m_nClass),
        //     static_cast<std::int64_t>(masterData->m_nLevel),
        //     static_cast<std::int64_t>(memberCount),
        //     static_cast<std::int64_t>(matchType),
        //     INT64_C(1),
        //     masterName,
        //     accountName.c_str())
        Log.AddT(
            LOG_BOTH, u"%I64d,%I64d,%I64d,,,,,,,,,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,", INT64_C(0x37f),
            static_cast<std::int64_t>(databaseId), static_cast<std::int64_t>(accountId),
            static_cast<std::int64_t>(masterData->m_nClass), static_cast<std::int64_t>(masterData->m_nLevel),
            static_cast<std::int64_t>(memberCount), static_cast<std::int64_t>(matchType), INT64_C(1), masterName,
            accountName.c_str()
        );
    }

    CParty* const currentRedParty = FindParty(redPartyId);
    User* const redMaster = currentRedParty != nullptr ? currentRedParty->GetMaster() : nullptr;
    if (redMaster != nullptr) {
        const int matchType = static_cast<int>(m_eType);
        const int memberCount = currentRedParty->GetMemberCount();
        CSharedCreatureData* const masterData = redMaster->m_data;
        const char16_t* const accountNameData = redMaster->m_sAccountName.data();
        const char16_t* const masterName = redMaster->GetName();
        const std::uint32_t accountId = redMaster->GetAccountID();
        const std::uint32_t databaseId = redMaster->GetDBID();
        const std::wstring accountName = Utf16ToWide(accountNameData);
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,,,,,,,,,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
        //     INT64_C(0x37f),
        //     static_cast<std::int64_t>(databaseId),
        //     static_cast<std::int64_t>(accountId),
        //     static_cast<std::int64_t>(masterData->m_nClass),
        //     static_cast<std::int64_t>(masterData->m_nLevel),
        //     static_cast<std::int64_t>(memberCount),
        //     static_cast<std::int64_t>(matchType),
        //     INT64_C(2),
        //     masterName,
        //     accountName.c_str())
        Log.AddT(
            LOG_BOTH, u"%I64d,%I64d,%I64d,,,,,,,,,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,", INT64_C(0x37f),
            static_cast<std::int64_t>(databaseId), static_cast<std::int64_t>(accountId),
            static_cast<std::int64_t>(masterData->m_nClass), static_cast<std::int64_t>(masterData->m_nLevel),
            static_cast<std::int64_t>(memberCount), static_cast<std::int64_t>(matchType), INT64_C(2), masterName,
            accountName.c_str()
        );
    }

    AddTimer(200, 0);
    return true;
}

void CUndergroundColosseum::EndMatchByTimeOut() {
    L2_GUARD_FUNCTION();
    if (m_RecordBoard.GetWinnerTeamIndex() == 1) {
        EndMatch(m_nBlueTeamPartyId, m_nRedTeamPartyId, 0);
    } else {
        EndMatch(m_nRedTeamPartyId, m_nBlueTeamPartyId, 0);
    }
}

void CUndergroundColosseum::EndMatch(std::uint32_t winnerPartyId, std::uint32_t loserPartyId, int reason) {
    // Current L2Server 0x00b01244.
    L2_GUARD_FUNCTION();

    ValidateParties();
    const std::uint32_t now = runtime::GetTickCount();
    if (m_nLastWinnerPartyId == winnerPartyId) {
        if (now - m_dwStartTime >= m_dwIgnoreStraightWinTime) {
            ++m_nStraightWinCount;
        }
    } else {
        m_nLastWinnerPartyId = winnerPartyId;
        m_nStraightWinCount = 1;
    }

    if (now - m_dwStartTime >= m_dwIgnoreStraightWinTime) {
        CParty* const winnerParty = FindParty(winnerPartyId);
        User* const winnerMaster = winnerParty != nullptr ? winnerParty->GetMaster() : nullptr;
        if (winnerMaster != nullptr) {
            const FVector winnerPosition = winnerMaster->GetPos();
            const int winnerX = static_cast<int>(winnerPosition.x);
            const int winnerZ = static_cast<int>(winnerPosition.z);
            const int winnerY = static_cast<int>(winnerPosition.y);
            CSharedCreatureData* const winnerData = winnerMaster->m_data;
            const char16_t* const winnerName = winnerMaster->GetName();
            const int winnerPoint = winnerMaster->GetPoint(5);
            const std::uint32_t winnerAccountId = winnerMaster->GetAccountID();
            const std::uint32_t winnerDbId = winnerMaster->GetDBID();
            const std::wstring winnerAccountName = Utf16ToWide(winnerMaster->m_sAccountName.data());
            // Log.AddT(
            //     LOG_BOTH,
            //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
            //     INT64_C(0x1f48),
            //     static_cast<std::int64_t>(winnerDbId),
            //     static_cast<std::int64_t>(winnerAccountId),
            //     static_cast<std::int64_t>(winnerX),
            //     static_cast<std::int64_t>(winnerY),
            //     static_cast<std::int64_t>(winnerZ),
            //     static_cast<std::int64_t>(winnerData->m_nClass),
            //     static_cast<std::int64_t>(winnerData->m_Race),
            //     static_cast<std::int64_t>(winnerData->m_Sex),
            //     static_cast<std::int64_t>(winnerData->m_nLevel),
            //     INT64_C(1),
            //     static_cast<std::int64_t>(winnerPoint),
            //     winnerName,
            //     winnerAccountName.c_str())
            Log.AddT(
                LOG_BOTH, u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
                INT64_C(0x1f48), static_cast<std::int64_t>(winnerDbId), static_cast<std::int64_t>(winnerAccountId),
                static_cast<std::int64_t>(winnerX), static_cast<std::int64_t>(winnerY), static_cast<std::int64_t>(winnerZ),
                static_cast<std::int64_t>(winnerData->m_nClass), static_cast<std::int64_t>(winnerData->m_Race),
                static_cast<std::int64_t>(winnerData->m_Sex), static_cast<std::int64_t>(winnerData->m_nLevel), INT64_C(1),
                static_cast<std::int64_t>(winnerPoint), winnerName, winnerAccountName.c_str()
            );
        }
    }

    SendCurrentRecordForUIToAllUser(2, winnerPartyId, loserPartyId);

    CParty* const winnerParty = FindParty(winnerPartyId);
    CParty* const loserParty = FindParty(loserPartyId);
    if (winnerParty != nullptr && loserParty != nullptr) {
        User* const winnerMaster = winnerParty->GetMaster();
        User* const loserMaster = loserParty->GetMaster();
        if (winnerMaster != nullptr && loserMaster != nullptr) {
            std::array<char16_t, 28> reasonText{};
            static_cast<void>(FormatUtf16To(reasonText.data(), reasonText.size(), u"%d", reason));

            CSharedCreatureData* const winnerData = winnerMaster->m_data;
            const int winnerMemberCount = winnerParty->GetMemberCount();
            const char16_t* const loserName = loserMaster->GetName();
            const char16_t* const winnerName = winnerMaster->GetName();
            const int matchType = static_cast<int>(m_eType);
            const std::uint32_t loserAccountId = loserMaster->GetAccountID();
            const std::uint32_t loserDbId = loserMaster->GetDBID();
            const std::uint32_t winnerAccountId = winnerMaster->GetAccountID();
            const std::uint32_t winnerDbId = winnerMaster->GetDBID();
            const std::wstring winnerAccountName = Utf16ToWide(winnerMaster->m_sAccountName.data());
            const std::wstring loserAccountName = Utf16ToWide(loserMaster->m_sAccountName.data());

            // Log.AddT(
            //     LOG_BOTH,
            //     L"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%s,%s,%s,%s,",
            //     INT64_C(0x380),
            //     static_cast<std::int64_t>(winnerDbId),
            //     static_cast<std::int64_t>(winnerAccountId),
            //     static_cast<std::int64_t>(loserDbId),
            //     static_cast<std::int64_t>(loserAccountId),
            //     reasonText.data(),
            //     static_cast<std::int64_t>(winnerData->m_nClass),
            //     static_cast<std::int64_t>(winnerData->m_nLevel),
            //     static_cast<std::int64_t>(winnerMemberCount),
            //     static_cast<std::int64_t>(matchType),
            //     static_cast<std::int64_t>(
            //         (winnerPartyId != m_nBlueTeamPartyId) + 1),
            //     static_cast<std::int64_t>(
            //         m_RecordBoard.m_nBlueTeamTotalKillCount),
            //     static_cast<std::int64_t>(
            //         m_RecordBoard.m_nRedTeamTotalKillCount),
            //     static_cast<std::int64_t>(
            //         m_RecordBoard.m_nBlueTeamTotalDeathCount),
            //     static_cast<std::int64_t>(
            //         m_RecordBoard.m_nRedTeamTotalDeathCount),
            //     static_cast<std::int64_t>(reason),
            //     winnerName,
            //     winnerAccountName.c_str(),
            //     loserName,
            //     loserAccountName.c_str())
            Log.AddT(
                LOG_BOTH,
                u"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%s,%s,%"
                u"s,%s,",
                INT64_C(0x380), static_cast<std::int64_t>(winnerDbId), static_cast<std::int64_t>(winnerAccountId),
                static_cast<std::int64_t>(loserDbId), static_cast<std::int64_t>(loserAccountId), reasonText.data(),
                static_cast<std::int64_t>(winnerData->m_nClass), static_cast<std::int64_t>(winnerData->m_nLevel),
                static_cast<std::int64_t>(winnerMemberCount), static_cast<std::int64_t>(matchType),
                static_cast<std::int64_t>((winnerPartyId != m_nBlueTeamPartyId) + 1),
                static_cast<std::int64_t>(m_RecordBoard.m_nBlueTeamTotalKillCount),
                static_cast<std::int64_t>(m_RecordBoard.m_nRedTeamTotalKillCount),
                static_cast<std::int64_t>(m_RecordBoard.m_nBlueTeamTotalDeathCount),
                static_cast<std::int64_t>(m_RecordBoard.m_nRedTeamTotalDeathCount), static_cast<std::int64_t>(reason),
                winnerName, winnerAccountName.c_str(), loserName, loserAccountName.c_str()
            );

            std::vector<User*> winnerMembers;
            winnerParty->GetAllMember(winnerMembers);
            for (User* const member : winnerMembers) {
                if (member == nullptr) {
                    continue;
                }

                const FVector memberPosition = member->GetPos();
                const int memberZ = static_cast<int>(memberPosition.z);
                const int memberY = static_cast<int>(memberPosition.y);
                const int memberX = static_cast<int>(memberPosition.x);
                CSharedCreatureData* const memberData = member->m_data;
                const char16_t* const repeatedWinnerName = winnerMaster->GetName();
                const char16_t* const memberName = member->GetName();
                const std::uint32_t repeatedWinnerAccountId = winnerMaster->GetAccountID();
                const std::uint32_t repeatedWinnerDbId = winnerMaster->GetDBID();
                const std::uint32_t memberAccountId = member->GetAccountID();
                const std::uint32_t memberDbId = member->GetDBID();
                const std::wstring memberAccountName = Utf16ToWide(member->m_sAccountName.data());
                const std::wstring repeatedWinnerAccountName = Utf16ToWide(winnerMaster->m_sAccountName.data());

                // Log.AddT(
                //     LOG_BOTH,
                //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,%I64d,%I64d,%I64d,%I64d,,,,,,,%s,%s,%s,%s,",
                //     INT64_C(0x1f49),
                //     static_cast<std::int64_t>(memberDbId),
                //     static_cast<std::int64_t>(memberAccountId),
                //     static_cast<std::int64_t>(repeatedWinnerDbId),
                //     static_cast<std::int64_t>(repeatedWinnerAccountId),
                //     static_cast<std::int64_t>(memberX),
                //     static_cast<std::int64_t>(memberY),
                //     static_cast<std::int64_t>(memberZ),
                //     static_cast<std::int64_t>(memberData->m_nClass),
                //     static_cast<std::int64_t>(memberData->m_Race),
                //     static_cast<std::int64_t>(memberData->m_Sex),
                //     static_cast<std::int64_t>(memberData->m_nLevel),
                //     memberName,
                //     memberAccountName.c_str(),
                //     repeatedWinnerName,
                //     repeatedWinnerAccountName.c_str())
                Log.AddT(
                    LOG_BOTH,
                    u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,%I64d,%I64d,%I64d,%I64d,,,,,,,%s,%s,%s,%s,",
                    INT64_C(0x1f49), static_cast<std::int64_t>(memberDbId), static_cast<std::int64_t>(memberAccountId),
                    static_cast<std::int64_t>(repeatedWinnerDbId), static_cast<std::int64_t>(repeatedWinnerAccountId),
                    static_cast<std::int64_t>(memberX), static_cast<std::int64_t>(memberY),
                    static_cast<std::int64_t>(memberZ), static_cast<std::int64_t>(memberData->m_nClass),
                    static_cast<std::int64_t>(memberData->m_Race), static_cast<std::int64_t>(memberData->m_Sex),
                    static_cast<std::int64_t>(memberData->m_nLevel), memberName, memberAccountName.c_str(),
                    repeatedWinnerName, repeatedWinnerAccountName.c_str()
                );
            }
        }
    }

    int loserReason = 0;
    if (m_eType == PVPMT_UGC_EVENT) {
        UnregisterParty(winnerPartyId, 0);
    } else if (reason == 0 || reason == 1) {
        loserReason = 1;
    } else if (reason == 2) {
        loserReason = 2;
    }
    UnregisterParty(loserPartyId, loserReason);
    SendEndMatchPacket();
    PostEndMatch();
}

void CUndergroundColosseum::SendEndMatchPacket() {
    L2_GUARD_FUNCTION();
    char packet[256]{};
    const int length = GetRegisteredPartiesPacket(packet, static_cast<int>(sizeof(packet)));
    g_npcserver.Send("cdb", 0x73, GetManagerNpcId(), length, packet);
}

void CUndergroundColosseum::RefreshUser(User* user) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00afeea8 has no creature-lock pair here.
    if (user != nullptr) {
        user->m_data->SetCP(user->m_data->m_dBaseCP);
        user->m_data->SetHP(user->m_data->m_dBaseHP);
        user->m_data->SetMP(user->m_data->m_dBaseMP);
    }
}

void CUndergroundColosseum::ResurrectUser(User* user, FVector position) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    user->TeleportToLocation(
        static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z), 0, 0x10000,
        TeleportSourceType::UCResurrectUser, nullptr, 0
    );
    RefreshUser(user);
    db.SendSaveCharacterInfo(user, true, true);
    user->CheckAbnormalVisualEffect();
    user->m_data->SetAlive(true);
    user->m_lSaveCharacter = 0;
    db.SaveDieDropExp(*user, 0);
    if (user->GetSocket() != nullptr) {
        user->GetSocket()->Send("cd", 1, static_cast<int>(user->Id()));
    }
}

bool CUndergroundColosseum::RegisterResurrectionTower(std::uint32_t npcId, std::uint32_t partyId) {
    L2_GUARD_FUNCTION();
    if (!IsRegisteredParty(partyId)) {
        return false;
    }
    if (partyId == m_nRedTeamPartyId) {
        m_nRedTeamResurrectionTowerId = npcId;
    } else if (partyId == m_nBlueTeamPartyId) {
        m_nBlueTeamResurrectionTowerId = npcId;
    }
    return true;
}

bool CUndergroundColosseum::CanResurrectedByMatch(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    std::uint32_t towerId = 0;
    if (user->m_data->m_nPartyId == m_nBlueTeamPartyId) {
        towerId = m_nBlueTeamResurrectionTowerId;
    } else if (user->m_data->m_nPartyId == m_nRedTeamPartyId) {
        towerId = m_nRedTeamResurrectionTowerId;
    }
    CNPC* const tower = FindNpc(towerId);
    return tower != nullptr && tower->IsAlive();
}

FVector CUndergroundColosseum::GetResPoint(int team) {
    L2_GUARD_FUNCTION();
    int type = static_cast<int>(m_eType);
    if (type == PVPMT_UGC_EVENT) {
        type = PVPMT_UGC_40;
    }
    if (type < 0 || type > PVPMT_UGC_UL || team < 1 || team > 2) {
        return FVector(0.0, 0.0, 0.0);
    }
    int index = static_cast<int>(::genrand64_real2() * 9.0);
    if (index < 0) {
        index = 0;
    } else if (index > 8) {
        index = 8;
    }
    const std::int32_t* const point = kResurrectionPoints[type][team - 1][index];
    return FVector(point[0], point[1], point[2]);
}

void CUndergroundColosseum::CheckResurrection(int team) {
    L2_GUARD_FUNCTION();
    std::uint32_t partyId = 0;
    std::uint32_t towerId = 0;
    if (team == 1) {
        partyId = m_nBlueTeamPartyId;
        towerId = m_nBlueTeamResurrectionTowerId;
    } else if (team == 2) {
        partyId = m_nRedTeamPartyId;
        towerId = m_nRedTeamResurrectionTowerId;
    } else {
        return;
    }
    CParty* const party = FindParty(partyId);
    CNPC* const tower = FindNpc(towerId);
    if (party == nullptr || tower == nullptr) {
        return;
    }
    std::vector<User*> members;
    party->GetAllMember(members);
    const std::uint32_t now = runtime::GetTickCount();
    for (User* member : members) {
        if (member == nullptr || member->IsAlive()) {
            continue;
        }
        CUCRecordBoardData* const record = m_RecordBoard.GetRecordData(member);
        if (record == nullptr || record->GetUserDieTime() == 0) {
            continue;
        }
        const std::uint32_t elapsed = now - record->GetUserDieTime();
        if (elapsed / 1000 >= 10) {
            ResurrectUser(member, GetResPoint(team));
            record->SetUserDieTime(0);
        } else if (elapsed % 1000 < 200 && member->GetSocket() != nullptr) {
            member->GetSocket()->Send(
                "chccc", 0x62, SystemMessageId::RESURRECTION_IN_SECONDS, 1, 0x14, static_cast<int>(10 - elapsed / 1000)
            );
        }
    }
}

bool CUndergroundColosseum::CheckAllMemberDied(int team) {
    L2_GUARD_FUNCTION();
    const std::uint32_t partyId = team == 1 ? m_nBlueTeamPartyId : team == 2 ? m_nRedTeamPartyId : 0;
    CParty* const party = FindParty(partyId);
    if (party == nullptr) {
        return false;
    }
    std::vector<User*> members;
    party->GetAllMember(members);
    for (User* member : members) {
        if (member != nullptr && member->IsAlive()) {
            return false;
        }
    }
    return true;
}

void CUndergroundColosseum::UserDie(User* user, User* enemy) {
    L2_GUARD_FUNCTION();

    User* validEnemy = nullptr;
    if (enemy != nullptr && enemy->GetParty() != nullptr) {
        validEnemy = enemy;
    }
    CParty* const victimParty = user != nullptr ? user->GetParty() : nullptr;
    CParty* const enemyParty = validEnemy != nullptr ? validEnemy->GetParty() : nullptr;
    if (victimParty != nullptr && enemyParty != nullptr && victimParty->m_data->m_nId == enemyParty->m_data->m_nId) {
        validEnemy = nullptr;
    }
    m_RecordBoard.UserDie(user, validEnemy);
    SendSimpleRecordToAllUser();

    if (validEnemy != nullptr) {
        const int enemyPartyId = enemyParty != nullptr ? enemyParty->m_data->m_nId : 0;
        const int matchType = static_cast<int>(m_eType);
        CSharedCreatureData* const victimData = user->m_data;
        CSharedCreatureData* const enemyData = validEnemy->m_data;
        const char16_t* const victimAccountNameData = user->m_sAccountName.data();
        const char16_t* const victimName = user->GetName();
        const char16_t* const enemyAccountNameData = validEnemy->m_sAccountName.data();
        const char16_t* const enemyName = validEnemy->GetName();
        const std::uint32_t victimAccountId = user->GetAccountID();
        const std::uint32_t victimDatabaseId = user->GetDBID();
        const std::uint32_t enemyAccountId = validEnemy->GetAccountID();
        const std::uint32_t enemyDatabaseId = validEnemy->GetDBID();
        const std::wstring enemyAccountName = Utf16ToWide(enemyAccountNameData);
        const std::wstring victimAccountName = Utf16ToWide(victimAccountNameData);
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,%s,%s,%s,%s,",
        //     INT64_C(0x381),
        //     static_cast<std::int64_t>(enemyDatabaseId),
        //     static_cast<std::int64_t>(enemyAccountId),
        //     static_cast<std::int64_t>(victimDatabaseId),
        //     static_cast<std::int64_t>(victimAccountId),
        //     static_cast<std::int64_t>(enemyData->m_nClass),
        //     static_cast<std::int64_t>(enemyData->m_nLevel),
        //     static_cast<std::int64_t>(victimData->m_nClass),
        //     static_cast<std::int64_t>(victimData->m_nLevel),
        //     static_cast<std::int64_t>(enemyPartyId),
        //     static_cast<std::int64_t>(matchType),
        //     static_cast<std::int64_t>(
        //         m_RecordBoard.m_nBlueTeamTotalKillCount),
        //     static_cast<std::int64_t>(
        //         m_RecordBoard.m_nRedTeamTotalKillCount),
        //     enemyName,
        //     enemyAccountName.c_str(),
        //     victimName,
        //     victimAccountName.c_str())
        Log.AddT(
            LOG_BOTH, u"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,%s,%s,%s,%s,",
            INT64_C(0x381), static_cast<std::int64_t>(enemyDatabaseId), static_cast<std::int64_t>(enemyAccountId),
            static_cast<std::int64_t>(victimDatabaseId), static_cast<std::int64_t>(victimAccountId),
            static_cast<std::int64_t>(enemyData->m_nClass), static_cast<std::int64_t>(enemyData->m_nLevel),
            static_cast<std::int64_t>(victimData->m_nClass), static_cast<std::int64_t>(victimData->m_nLevel),
            static_cast<std::int64_t>(enemyPartyId), static_cast<std::int64_t>(matchType),
            static_cast<std::int64_t>(m_RecordBoard.m_nBlueTeamTotalKillCount),
            static_cast<std::int64_t>(m_RecordBoard.m_nRedTeamTotalKillCount), enemyName, enemyAccountName.c_str(),
            victimName, victimAccountName.c_str()
        );
    }

    const std::uint32_t now = runtime::GetTickCount();
    if (!CanResurrectedByMatch(user) || now - m_dwStartTime >= m_dwIgnoreStraightWinTime) {
        const bool blueDied = CheckAllMemberDied(1);
        const bool redDied = CheckAllMemberDied(2);
        if (blueDied) {
            EndMatch(m_nRedTeamPartyId, m_nBlueTeamPartyId, 1);
        } else if (redDied) {
            EndMatch(m_nBlueTeamPartyId, m_nRedTeamPartyId, 1);
        }
    }
}

void CUndergroundColosseum::TimerExpired(int) {
    L2_GUARD_FUNCTION();
    if (m_dwEndTime == 0) {
        return;
    }
    CheckResurrection(1);
    CheckResurrection(2);
    const std::uint32_t now = runtime::GetTickCount();
    if (m_dwEndTime < now) {
        EndMatchByTimeOut();
    } else {
        const std::uint32_t remaining = m_dwEndTime - now;
        const std::uint32_t seconds = remaining / 1000;
        if (remaining % 1000 < 200 && seconds < 6) {
            for (const std::uint32_t partyId : {m_nBlueTeamPartyId, m_nRedTeamPartyId}) {
                CParty* const party = FindParty(partyId);
                if (party == nullptr) {
                    continue;
                }
                std::vector<User*> members;
                party->GetAllMember(members);
                for (User* member : members) {
                    if (member != nullptr) {
                        member->SendSystemMessage_d(SystemMessageId::COLOSSEUM_GAME_END_SECONDS_REMAINING, seconds);
                    }
                }
            }
        }
    }
    if (m_dwEndTime != 0) {
        AddTimer(200, 0);
    }
}

void CUndergroundColosseum::PostEndMatch() {
    L2_GUARD_FUNCTION();
    for (CUCRecordBoardData* record : m_RecordBoard.m_vecBlueTeam) {
        User* const user = FindUser(record->m_nUserId);
        if (user == nullptr || user->IsAlive()) {
            RefreshUser(user);
        } else {
            ResurrectUser(user, user->GetPos());
        }
    }
    for (CUCRecordBoardData* record : m_RecordBoard.m_vecRedTeam) {
        User* const user = FindUser(record->m_nUserId);
        if (user == nullptr || user->IsAlive()) {
            RefreshUser(user);
        } else {
            ResurrectUser(user, user->GetPos());
        }
    }
    for (const std::uint32_t partyId : {m_nBlueTeamPartyId, m_nRedTeamPartyId}) {
        CParty* const party = FindParty(partyId);
        if (party != nullptr) {
            party->m_data->SetPVPMatchEnd();
        }
    }
    m_RecordBoard.Init();
    CPVPMatch2::PostEndMatch();
}

CUCRecordBoardData* CUndergroundColosseum::GetRecordData(User* user) { return m_RecordBoard.GetRecordData(user); }

void CUndergroundColosseum::SendCurrentRecordForSystemMessage(User* user) {
    m_RecordBoard.SendCurrentRecordForSystemMessage(user);
}

void CUndergroundColosseum::SendCurrentRecordForUI(User* user, int type) {
    m_RecordBoard.SendCurrentRecordForUI(user, type);
}

void CUndergroundColosseum::SendSimpleRecordToAllUser() {
    L2_GUARD_FUNCTION();
    for (const std::uint32_t partyId : {m_nBlueTeamPartyId, m_nRedTeamPartyId}) {
        CParty* const party = FindParty(partyId);
        if (party == nullptr) {
            continue;
        }
        std::vector<User*> members;
        party->GetAllMember(members);
        for (User* member : members) {
            if (member != nullptr && member->GetSocket() != nullptr) {
                member->GetSocket()->Send(
                    "chdd", 0xfe, 0x80, m_RecordBoard.m_nBlueTeamTotalKillCount, m_RecordBoard.m_nRedTeamTotalKillCount
                );
            }
        }
    }
    {
        L2_GUARD(m_Lock);
        auto it = m_ObserverList.begin();
        while (it != m_ObserverList.end()) {
            User* const observer = FindUser(*it);
            if (observer == nullptr) {
                it = m_ObserverList.erase(it);
            } else {
                if (observer->GetSocket() != nullptr) {
                    observer->GetSocket()->Send(
                        "chdd", 0xfe, 0x80, m_RecordBoard.m_nBlueTeamTotalKillCount, m_RecordBoard.m_nRedTeamTotalKillCount
                    );
                }
                ++it;
            }
        }
    }
}

void CUndergroundColosseum::SendCurrentRecordForUIToAllUser(
    int type, std::uint32_t firstPartyId, std::uint32_t secondPartyId
) {
    int first = 0;
    int second = 0;
    if (m_nBlueTeamPartyId == firstPartyId) {
        first = 2;
        second = 1;
    } else if (m_nBlueTeamPartyId == secondPartyId) {
        first = 1;
        second = 2;
    }
    char packet[1024]{};
    const int length = m_RecordBoard.MakeCurrentRecordPacket(packet, static_cast<int>(sizeof(packet)), type, second, first);
    if (length <= 0) {
        return;
    }
    for (const std::uint32_t partyId : {m_nBlueTeamPartyId, m_nRedTeamPartyId}) {
        CParty* const party = FindParty(partyId);
        if (party == nullptr) {
            continue;
        }
        std::vector<User*> members;
        party->GetAllMember(members);
        for (User* member : members) {
            if (member != nullptr && member->GetSocket() != nullptr) {
                member->GetSocket()->Send("chb", 0xfe, 0x7f, length, packet);
            }
        }
    }
    {
        L2_GUARD(m_Lock);
        auto it = m_ObserverList.begin();
        while (it != m_ObserverList.end()) {
            User* const observer = FindUser(*it);
            if (observer == nullptr) {
                it = m_ObserverList.erase(it);
            } else {
                if (observer->GetSocket() != nullptr) {
                    observer->GetSocket()->Send("chb", 0xfe, 0x7f, length, packet);
                }
                ++it;
            }
        }
    }
}

bool CUndergroundColosseum::IsPVPMatchRelateNpc(CNPC* npc) {
    L2_GUARD_FUNCTION();
    return npc != nullptr && (npc->Id() == m_nBlueTeamResurrectionTowerId || npc->Id() == m_nRedTeamResurrectionTowerId);
}

int CUndergroundColosseum::GetPVPMatchTeamIndex(CNPC* npc) {
    L2_GUARD_FUNCTION();
    if (npc == nullptr) {
        return 0;
    }
    if (npc->Id() == m_nBlueTeamResurrectionTowerId) {
        return 1;
    }
    return npc->Id() == m_nRedTeamResurrectionTowerId ? 2 : 0;
}

bool CUndergroundColosseum::IsPartyPVPPlayer(User& user) {
    CParty* const party = user.GetParty();
    return party != nullptr && party->m_data->IsPVPMatching();
}

void CUndergroundColosseum::UnregisterAll() {
    // Current L2Server 0x00b00e3c.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        m_listRegisteredParties.clear();
    }
}

void CUndergroundColosseum::RegisterObserve(User& user) {
    L2_GUARD_FUNCTION();
    SendCurrentRecordForUI(&user, 0);
    L2_GUARD(m_Lock);
    m_ObserverList.insert(user.Id());
}

void CUndergroundColosseum::UnregisterObserve(User& user) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    m_ObserverList.erase(user.Id());
}

bool CUndergroundColosseum::IsForEventMatch() { return m_eType == PVPMT_UGC_EVENT; }

void CUndergroundColosseum::GivePVPPointToWinnerParty(std::uint32_t) {
    L2_GUARD_FUNCTION();
}
