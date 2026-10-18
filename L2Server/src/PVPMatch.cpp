#include "PVPMatch.h"
#include "BlockUpset.h"
#include "Cleft.h"
#include "CrataeCube.h"
#include "NPC.h"
#include "Object.h"
#include "Party.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/TextRuntime.h"
#include "UndergroundColosseum.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Config.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Utils.h"
#include "Shared/src/platform/Runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <string_view>

// Portable registration: the runtime ID is no longer a shipping number.
// static_assert(
//     L2ServerMemory::kCPVPMatchMemberInfoClassId ==
//     CPVPMatchMemberInfo::kMemoryClassId)

CPVPMatchMemberInfo::SetOrder::SetOrder() = default;

void CPVPMatchMemberInfo::SetOrder::operator()(CPVPMatchMemberInfo* memberInfo) {
    const int point = memberInfo->GetPoint();
    if (point < m_nPrevPoint) {
        ++m_nOrder;
        m_nPrevPoint = point;
    }
    memberInfo->m_nOrder = m_nOrder;
}

CPVPMatchMemberInfo::CPVPMatchMemberInfo(BlueRedTeam::Enum team, std::uint32_t userId, const char16_t* userName) :
    m_Team(team), m_nUserId(userId), m_UserName(userName ? userName : u""), m_nOrder(0) {}

CPVPMatchMemberInfo::~CPVPMatchMemberInfo() = default;

L2_DEFINE_OBJECT_CLASS_ID(CPVPMatchMemberInfo)

L2_DEFINE_OBJECT_CLASS_NAME_LITERAL(CPVPMatchMemberInfo)

L2_DEFINE_OBJECT_ALLOCATOR_LITERAL(CPVPMatchMemberInfo)

L2_DEFINE_OBJECT_NEW(CPVPMatchMemberInfo)

L2_DEFINE_OBJECT_ADD_REF(CPVPMatchMemberInfo)

L2_DEFINE_OBJECT_RELEASE(CPVPMatchMemberInfo)

void CPVPMatchMemberInfo::MakeListInfo(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    packet.Assemble("dS", m_nUserId, m_UserName.c_str());
}

void CPVPMatchMemberInfo::MakeStateInfo(IPacketHelper&) {}

int CPVPMatchMemberInfo::GetPoint() const { return 0; }

bool CPVPMatchMemberInfo::SortPlayerByPoint(CPVPMatchMemberInfo* left, CPVPMatchMemberInfo* right) {
    return left->GetPoint() > right->GetPoint();
}

CPVPMatch::CPVPMatch() :
    m_State(PVPMatchState::INVALID),
    m_CurrentStateStartTime(0),
    m_ManagerNpcId(0),
    m_targetPadding44(0),
    m_TimerTick(0),
    m_targetPaddingE4(0),
    m_StartingPos{},
    m_BanishingPos{},
    m_BannedUserIndex(0),
    m_targetPadding16c(0),
    m_BannedUsers{} {
    L2_GUARD_FUNCTION();
    InitSetting();
}

CPVPMatch::~CPVPMatch() = default;

void CPVPMatch::Init() {}

void CPVPMatch::InitSetting() {
    L2_GUARD_FUNCTION();
    for (std::vector<FVector>& positions : m_StartingPos) {
        positions.clear();
    }
    for (std::vector<FVector>& positions : m_BanishingPos) {
        positions.clear();
    }
    m_Territory.Clear();
    m_CurrentStateStartTime = 0;
    m_ManagerNpcId = 0;
    m_TimerTick = 0;
    for (auto& users : m_BannedUsers) {
        users.clear();
    }
}

bool CPVPMatch::IsPVPMatch() const { return true; }

bool CPVPMatch::IsPVPMatch2() const { return false; }

CPVPMatch2* CPVPMatch::CastPVPMatch2() { return nullptr; }

bool CPVPMatch::CanAddPlayer(User& user) const {
    L2_GUARD_FUNCTION();
    const bool alreadyInMatch = user.IsInPVPMatchExcept(GetPVPMatchType(), PVPMatchPlayerState::NONE);
    if (alreadyInMatch) {
        user.SendSystemMessage(SystemMessageId::CANNOT_REGISTER_FOR_MULTIPLE_PVP_MATCHES);
    }
    return !alreadyInMatch;
}

void CPVPMatch::SendCurrentRecordForUIToOneUser(std::uint32_t, int) {}

void CPVPMatch::UserDie(User*, User*) {}

void CPVPMatch::AddKillPoint(User*, int) {}

std::uint32_t CPVPMatch::GetManagerNpcId() const { return static_cast<std::uint32_t>(m_ManagerNpcId); }

void CPVPMatch::RegisterManagerNpcId(int npcId) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_NpcLock);
        if (m_ManagerNpcId == 0) {
            m_ManagerNpcId = npcId;
        }
        m_ManagerNpcs.push_back(npcId);
    }
}

BlueRedTeam::Enum CPVPMatch::GetTeamType(const char16_t* teamName, CUserSocket* socket) {
    L2_GUARD_FUNCTION();
    if (teamName != nullptr && std::u16string_view(teamName).compare(u"red") == 0) {
        return BlueRedTeam::RED;
    }
    if (teamName == nullptr || std::u16string_view(teamName).compare(u"blue") != 0) {
        if (socket != nullptr) {
            socket->SendSystemMessage(u"SYS", u"team type should be blue or red. default is blue");
        }
    }
    return BlueRedTeam::BLUE;
}

char CPVPMatch::GetMatchTeamID(BlueRedTeam::Enum team) {
    if (team == BlueRedTeam::BLUE) {
        return 1;
    }
    if (team == BlueRedTeam::RED) {
        return 2;
    }
    return 0;
}

BlueRedTeam::Enum CPVPMatch::GetOpposite(BlueRedTeam::Enum team) {
    if (team == BlueRedTeam::BLUE) {
        return BlueRedTeam::RED;
    }
    if (team == BlueRedTeam::RED) {
        return BlueRedTeam::BLUE;
    }
    return BlueRedTeam::MAX;
}

void CPVPMatch::TimerExpired(int) {
    L2_GUARD_FUNCTION();
    if (m_TimerTick % 5 == 0) {
        {
            L2_GUARD(m_Lock);
            m_BannedUserIndex = (m_BannedUserIndex + 1) % BanArraySize;
            m_BannedUsers[m_BannedUserIndex].clear();
        }
    }
    ++m_TimerTick;
    if (m_State == PVPMatchState::INVALID) {
        {
            L2_GUARD(m_Lock);
            ChangeState(PVPMatchState::REGISTER);
            _OnRegister();
        }
    }
}

int CPVPMatch::GetCombatElapsedSec() const {
    L2_GUARD_FUNCTION();
    if (m_CurrentStateStartTime == 0) {
        return -1;
    }
    return static_cast<int>((runtime::GetTickCount() - static_cast<std::uint32_t>(m_CurrentStateStartTime)) / 1000U);
}

bool CPVPMatch::GetRandomPos(const std::vector<FVector>& positions, FVector& position) {
    L2_GUARD_FUNCTION();
    if (positions.empty()) {
        return false;
    }
    auto index = static_cast<std::size_t>(::genrand64_real1() * static_cast<double>(positions.size()));
    if (index >= positions.size()) {
        index = positions.size() - 1;
    }
    position = runtime::CheckedVectorAt(positions, index);
    return true;
}

int CPVPMatch::GetMovingTeamCount(int blueCount, int redCount, int movingTeam) {
    L2_GUARD_FUNCTION();
    int remainingBlue = blueCount - movingTeam;
    if (remainingBlue < 0) {
        remainingBlue = 0;
        redCount -= movingTeam;
    } else {
        redCount = redCount - remainingBlue - blueCount;
    }
    return redCount / 2 + remainingBlue;
}

int CPVPMatch::GetRelation(User&, User&) { return 0; }

bool CPVPMatch::IsEnemyTo(User& user, User& target) {
    L2_GUARD_FUNCTION();
    return GetRelation(user, target) == 1;
}

bool CPVPMatch::_GetMemberInfo2(
    UserDB& users, int firstUserId, int secondUserId, CPVPMatchMemberInfo*& first, CPVPMatchMemberInfo*& second
) {
    L2_GUARD_FUNCTION();
    const auto firstIt = users.find(static_cast<std::uint32_t>(firstUserId));
    if (firstIt == users.end()) {
        return false;
    }
    const auto secondIt = users.find(static_cast<std::uint32_t>(secondUserId));
    if (secondIt == users.end()) {
        return false;
    }
    first = firstIt->second;
    second = secondIt->second;
    return true;
}

int CPVPMatch::_GetRelation(UserDB& users, User& user, User& target) {
    L2_GUARD_FUNCTION();
    CPVPMatchMemberInfo* first = nullptr;
    CPVPMatchMemberInfo* second = nullptr;
    if (!_GetMemberInfo2(users, static_cast<int>(user.Id()), static_cast<int>(target.Id()), first, second)) {
        return 0;
    }
    return first->m_Team == second->m_Team ? 2 : 1;
}

bool CPVPMatch::_IsEnemyTo(UserDB& users, User& user, User& target) { return _GetRelation(users, user, target) == 1; }

void CPVPMatch::_BroadcastToPlayer(UserDB& users, const char* format, ...) {
    L2_GUARD_FUNCTION();

    DefaultIOBuffer* const buffer = DefaultIOBuffer::Alloc();
    va_list arguments;
    va_start(arguments, format);
    const int bodySize = ::VAssemble(reinterpret_cast<char*>(buffer->m_Buffer.data() + 2), 0x4ffe, format, arguments);
    va_end(arguments);
    if (bodySize <= 0) {
        buffer->Release();
        return;
    }
    const std::uint16_t packetSize = static_cast<std::uint16_t>(bodySize + 2);
    std::memcpy(buffer->m_Buffer.data(), &packetSize, sizeof(packetSize));
    buffer->m_dwSize = packetSize;

    for (const auto& entry : users) {
        const std::CDualKey<int, int> key(g_nWorldId, static_cast<int>(entry.first));
        CSPointer<User> user = g_UserDBIDMap.FindObject(key);
        if (user && user->GetSocket() != nullptr) {
            user->GetSocket()->AsyncSendConstBuffer(buffer);
        }
    }
    buffer->Release();
}

void CPVPMatch::OnNpcServerClosed() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_NpcLock);
        m_ManagerNpcId = 0;
        m_ManagerNpcs.clear();
    }
}

PVPMatchState::Enum CPVPMatch::GetState() const { return m_State; }

void CPVPMatch::ChangeState(PVPMatchState::Enum state) {
    L2_GUARD_FUNCTION();
    if (m_State != state) {
        m_State = state;
        m_CurrentStateStartTime = static_cast<std::int32_t>(runtime::GetTickCount());
    }
}

wchar_t* CPVPMatch::GetName() const { return const_cast<wchar_t*>(L"PVPMatch"); }

void CPVPMatch::_OnRegister() {}
void CPVPMatch::_OnCombat() {}
void CPVPMatch::_OnReward() {}

void CPVPMatch::AddStartPos(BlueRedTeam::Enum team, const std::vector<FVector>& positions) {
    L2_GUARD_FUNCTION();
    m_StartingPos[static_cast<int>(team)] = positions;
}

void CPVPMatch::AddBanishPos(BlueRedTeam::Enum team, const std::vector<FVector>& positions) {
    L2_GUARD_FUNCTION();
    m_BanishingPos[static_cast<int>(team)] = positions;
}

bool CPVPMatch::CheckDistanceWithNpc(User& user) {
    L2_GUARD_FUNCTION();
    for (const int managerId : m_ManagerNpcs) {
        CObject* const object = CObject::GetObjectW(static_cast<std::uint32_t>(managerId));
        if (object == nullptr || !object->IsNPC()) {
            continue;
        }
        CNPC* const npc = object->CastNPC();
        if (npc != nullptr) {
            FVector userPosition = user.GetPos();
            FVector npcPosition = npc->GetPos();
            if (CheckDistance(userPosition, npcPosition, 500)) {
                return true;
            }
        }
    }
    return false;
}

bool CPVPMatch::CheckDistance(FVector& first, FVector& second, int distance) {
    const double dx = first.x - second.x;
    const double dy = first.y - second.y;
    const double dz = first.z - second.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz) <= static_cast<double>(distance);
}

void CPVPMatch::InsertBannedUser(int userId) {
    {
        L2_GUARD(m_Lock);
        _InsertBannedUser(userId);
    }
}

void CPVPMatch::_InsertBannedUser(int userId) {
    L2_GUARD_FUNCTION();
    m_BannedUsers[m_BannedUserIndex].insert(userId);
}

bool CPVPMatch::IsBannedUser(int userId) {
    L2_GUARD_FUNCTION();
    const bool banned = [&] {
        L2_GUARD(m_Lock);
        const bool banned = _IsBannedUser(userId);
        return banned;
    }();
    return banned;
}

bool CPVPMatch::_IsBannedUser(int userId) const {
    L2_GUARD_FUNCTION();
    for (const auto& users : m_BannedUsers) {
        if (users.find(userId) != users.end()) {
            return true;
        }
    }
    return false;
}

CPVPMatch2::CPVPMatch2(PVP_MATCH_TYPE type) :
    m_eType(type),
    m_targetPadding1d4(0),
    m_nBlueTeamPartyId(0),
    m_nRedTeamPartyId(0),
    m_nLastWinnerPartyId(0),
    m_nStraightWinCount(0),
    m_dwStartTime(0),
    m_dwEndTime(0),
    m_dwIgnoreStraightWinTime(0),
    m_bGivePVPPoint(false),
    m_targetPadding215{},
    m_nGateManNpcId(0),
    m_targetPadding23c(0) {}

CPVPMatch2::~CPVPMatch2() = default;

bool CPVPMatch2::IsPVPMatch2() const { return true; }
CPVPMatch2* CPVPMatch2::CastPVPMatch2() { return this; }
bool CPVPMatch2::IsCC() const { return false; }
CCrataeCube* CPVPMatch2::CastCC() { return nullptr; }
bool CPVPMatch2::IsUC() const { return false; }
CUndergroundColosseum* CPVPMatch2::CastUC() { return nullptr; }

PVP_MATCH_TYPE CPVPMatch2::GetMatchTypeDetail() { return m_eType; }

void CPVPMatch2::EndMatchByTimeOut() {}
bool CPVPMatch2::CanResurrectedByMatch(User*) { return false; }
void CPVPMatch2::SendCurrentRecordForSystemMessage(User*) {}
void CPVPMatch2::SendCurrentRecordForUI(User*, int) {}
bool CPVPMatch2::IsPVPMatchRelateNpc(CNPC*) { return false; }
int CPVPMatch2::GetPVPMatchTeamIndex(CNPC*) { return 0; }

void CPVPMatch2::OnNpcServerClosed() {
    L2_GUARD_FUNCTION();
    CPVPMatch::OnNpcServerClosed();
    PostEndMatch();
    m_nLastWinnerPartyId = 0;
    m_nStraightWinCount = 0;
    m_dwIgnoreStraightWinTime = 0;
    m_nGateManNpcId = 0;
    m_listRegisteredUsers.clear();
}

bool CPVPMatch2::CheckRegisterLevel(int) { return false; }

void CPVPMatch2::PostEndMatch() {
    L2_GUARD_FUNCTION();
    m_nBlueTeamPartyId = 0;
    m_nRedTeamPartyId = 0;
    m_dwStartTime = 0;
    m_dwEndTime = 0;
    m_bGivePVPPoint = false;
}

void CPVPMatch2::GivePVPPointToWinnerParty(std::uint32_t) {}

std::uint32_t CPVPMatch2::GetGateManNpcId() { return m_nGateManNpcId; }

void CPVPMatch2::SetGateManNpcId(std::uint32_t npcId) { m_nGateManNpcId = npcId; }

void CPVPMatch2::AddKillPoint(User*, int) {}

int CPVPMatch2::GetRegisteredUsersPacket(char* packet, int packetSize) {
    L2_GUARD_FUNCTION();
    int length = ::Assemble(packet, packetSize, "d", static_cast<int>(m_listRegisteredUsers.size()));
    for (const int userId : m_listRegisteredUsers) {
        length += ::Assemble(packet + length, packetSize - length, "d", userId);
    }
    return length;
}

int CPVPMatch2::RegisterUser(std::uint32_t) {
    L2_GUARD_FUNCTION();
    return 1;
}
bool CPVPMatch2::UnregisterUser(std::uint32_t, int) {
    L2_GUARD_FUNCTION();
    return false;
}

bool CPVPMatch2::ReadyMatch() {
    L2_GUARD_FUNCTION();
    m_dwStartTime = runtime::GetTickCount();
    return true;
}

bool CPVPMatch2::StartMatch() { return true; }
void CPVPMatch2::TriggerEndMatch() {}

int CPVPMatch2::CheckRegisterUser(std::uint32_t userId) {
    L2_GUARD_FUNCTION();
    const std::CDualKey<int, int> key(g_nWorldId, static_cast<int>(userId));
    CSPointer<User> user = g_UserDBIDMap.FindObject(key);
    if (!user) {
        return PVPMR_INVALID_USER;
    }
    if (!CheckRegisterLevel(user->m_data->m_nLevel)) {
        return PVPMR_INVALID_USER_LEVEL;
    }
    CObject* const object = CObject::GetObjectW(GetManagerNpcId());
    CNPC* const manager = object != nullptr && object->IsNPC() ? object->CastNPC() : nullptr;
    if (manager == nullptr) {
        return PVPMR_INVALID_MATCH;
    }
    FVector managerPosition = manager->GetPos();
    FVector userPosition = user->GetPos();
    if (!CheckDistance(managerPosition, userPosition, 1500)) {
        return PVPMR_INVALID_USER_DIST;
    }
    return CheckInParty(userId) ? PVPMR_SUCCESS : PVPMR_USER_IN_PARTY;
}

bool CPVPMatch2::CheckInParty(std::uint32_t userId) {
    L2_GUARD_FUNCTION();
    const std::CDualKey<int, int> key(g_nWorldId, static_cast<int>(userId));
    CSPointer<User> user = g_UserDBIDMap.FindObject(key);
    return user && user->m_data->m_nPartyId == 0;
}

void CPVPMatch2::SendCurrentRecordForUIToOneUser(std::uint32_t, int) {}

bool CPVPMatch2::IsRegisteredUser(std::uint32_t) {
    L2_GUARD_FUNCTION();
    return false;
}
bool CPVPMatch2::IsCrataeCubeUser(User&) {
    L2_GUARD_FUNCTION();
    return false;
}
int CPVPMatch2::GetRankUserPVPMatch(std::uint32_t) {
    L2_GUARD_FUNCTION();
    return 0;
}

CPVPMatchSystem& CPVPMatchSystem::inst() {
    static CPVPMatchSystem system;
    return system;
}

CPVPMatchSystem::CPVPPointTimeBlock::CPVPPointTimeBlock() :
    m_nStartHour(0), m_nStartMinute(0), m_nEndHour(0), m_nEndMinute(0), m_arrWeekDays{} {}

CPVPMatchSystem::CPVPMatchSystem() :
    m_bCheckPartyMemberCount(false),
    m_bCheckPartyMemberLevel(false),
    m_bCheckPartyMemberDist(false),
    m_bCheckPartyMemberDebuff(false),
    m_bCheckGivePVPPointTime(false),
    m_targetPadding5{},
    m_PVPMatch2Map{},
    m_CleftMatch(&Singleton<CCleft>::inst()),
    m_BlockUpsetManager(&Singleton<CBlockUpsetManager>::inst()) {
    PrepareAllPVPMatch();
}

CPVPMatchSystem::~CPVPMatchSystem() = default;

void CPVPMatchSystem::PrepareAllPVPMatch() {
    L2_GUARD_FUNCTION();
    for (int type = PVPMT_MIN; type < PVPMT_MAX; ++type) {
        if (IsValidUC(static_cast<PVP_MATCH_TYPE>(type))) {
            m_PVPMatch2Map[type] = new CUndergroundColosseum(static_cast<PVP_MATCH_TYPE>(type));
        } else {
            m_PVPMatch2Map[type] = new CCrataeCube(static_cast<PVP_MATCH_TYPE>(type));
        }
    }
}

void CPVPMatchSystem::Init() {
    L2_GUARD_FUNCTION();
    SetEnableCheckAllCondition(true);
    m_vecGivePVPPointTime.clear();
    for (int type = PVPMT_COC_70; type <= PVPMT_COC_80; ++type) {
        if (m_PVPMatch2Map[type] != nullptr) {
            m_PVPMatch2Map[type]->AddTimer(1000, 0);
        }
    }
}

void CPVPMatchSystem::OnNpcServerClosed() {
    L2_GUARD_FUNCTION();
    SetEnableCheckAllCondition(true);
    m_vecGivePVPPointTime.clear();
    for (CPVPMatch2* match : m_PVPMatch2Map) {
        if (match != nullptr) {
            match->OnNpcServerClosed();
        }
    }
    m_CleftMatch->OnNpcServerClosed();
    m_BlockUpsetManager->OnNpcServerClosed();
}

CPVPMatch* CPVPMatchSystem::CreateMatch(const std::uint32_t managerNpcId, const PVP_MATCH_TYPE type) {
    L2_GUARD_FUNCTION();
    CPVPMatch2* const match = m_PVPMatch2Map[type];
    if (match != nullptr && match->GetManagerNpcId() == 0) {
        match->RegisterManagerNpcId(static_cast<int>(managerNpcId));
    }
    return match;
}

CUndergroundColosseum* CPVPMatchSystem::GetMatchUCByPartyId(const std::uint32_t partyId) {
    L2_GUARD_FUNCTION();
    for (int type = PVPMT_UGC_40; type <= PVPMT_UGC_UL; ++type) {
        CUndergroundColosseum* const match = m_PVPMatch2Map[type]->CastUC();
        if (match != nullptr && match->IsRegisteredParty(partyId)) {
            return match;
        }
    }
    CUndergroundColosseum* const eventMatch = m_PVPMatch2Map[PVPMT_UGC_EVENT]->CastUC();
    return eventMatch != nullptr && eventMatch->IsRegisteredParty(partyId) ? eventMatch : nullptr;
}

bool CPVPMatchSystem::IsValidPvpMatch(const int matchId) {
    L2_GUARD_FUNCTION();
    return static_cast<unsigned int>(matchId) < PVPMT_MAX;
}

bool CPVPMatchSystem::IsValidUC(const PVP_MATCH_TYPE type) {
    L2_GUARD_FUNCTION();
    return static_cast<unsigned int>(type) <= PVPMT_UGC_UL || type == PVPMT_UGC_EVENT;
}

void CPVPMatchSystem::SetEnableCheckPartyMemberCount(const bool enabled) {
    L2_GUARD_FUNCTION();
    m_bCheckPartyMemberCount = enabled;
}

bool CPVPMatchSystem::IsCheckPartyMemberCount() const { return m_bCheckPartyMemberCount; }

void CPVPMatchSystem::SetEnableCheckPartyMemberLevel(const bool enabled) {
    L2_GUARD_FUNCTION();
    m_bCheckPartyMemberLevel = enabled;
}

bool CPVPMatchSystem::IsCheckPartyMemberLevel() const { return m_bCheckPartyMemberLevel; }

void CPVPMatchSystem::SetEnableCheckPartyMemberDist(const bool enabled) {
    L2_GUARD_FUNCTION();
    m_bCheckPartyMemberDist = enabled;
}

bool CPVPMatchSystem::IsCheckPartyMemberDist() const { return m_bCheckPartyMemberDist; }

void CPVPMatchSystem::SetEnableCheckPartyMemberDebuff(const bool enabled) {
    L2_GUARD_FUNCTION();
    m_bCheckPartyMemberDebuff = enabled;
}

bool CPVPMatchSystem::IsCheckPartyMemberDebuff() const { return m_bCheckPartyMemberDebuff; }

void CPVPMatchSystem::SetEnableCheckGivePVPPointTime(const bool enabled) {
    L2_GUARD_FUNCTION();
    m_bCheckGivePVPPointTime = enabled;
}

bool CPVPMatchSystem::IsCheckGivePVPPointTime() const { return m_bCheckGivePVPPointTime; }

void CPVPMatchSystem::SetEnableCheckAllCondition(const bool enabled) {
    L2_GUARD_FUNCTION();
    m_bCheckPartyMemberCount = enabled;
    m_bCheckPartyMemberLevel = enabled;
    m_bCheckPartyMemberDist = enabled;
    m_bCheckPartyMemberDebuff = enabled;
    m_bCheckGivePVPPointTime = enabled;
}

void CPVPMatchSystem::OnDismissParty(CParty* const party) {
    L2_GUARD_FUNCTION();
    if (party == nullptr || !party->m_data->IsPVPMatching()) {
        return;
    }
    if (CUndergroundColosseum* const match = GetMatchUCByPartyId(party->Id())) {
        match->OnDismissParty(party);
    }
}

void CPVPMatchSystem::OnHandOverPartyMaster(CParty* const party, User* const newMaster) {
    L2_GUARD_FUNCTION();
    if (newMaster == nullptr || party == nullptr || !party->m_data->IsPVPMatching()) {
        return;
    }
    if (CUndergroundColosseum* const match = GetMatchUCByPartyId(party->Id())) {
        match->OnHandOverPartyMaster(party, newMaster);
    }
}

void CPVPMatchSystem::SendCurrentRecordForSystemMessage(User* const user) {
    L2_GUARD_FUNCTION();
    CParty* const party = user != nullptr ? user->GetParty() : nullptr;
    if (party == nullptr || !party->m_data->IsPVPMatching()) {
        return;
    }
    if (CUndergroundColosseum* const match = GetMatchUCByPartyId(party->Id())) {
        match->SendCurrentRecordForSystemMessage(user);
    }
}

void CPVPMatchSystem::SendCurrentRecordForUI(User* const user) {
    L2_GUARD_FUNCTION();
    CParty* const party = user != nullptr ? user->GetParty() : nullptr;
    if (party == nullptr || !party->m_data->IsPVPMatching()) {
        return;
    }
    if (CUndergroundColosseum* const match = GetMatchUCByPartyId(party->Id())) {
        match->SendCurrentRecordForUI(user, 1);
    }
}

int CPVPMatchSystem::GetPVPMatchTeamIndex(CNPC* const npc) {
    L2_GUARD_FUNCTION();
    if (npc == nullptr) {
        return 0;
    }
    for (CPVPMatch2* const match : m_PVPMatch2Map) {
        if (match != nullptr && match->IsPVPMatchRelateNpc(npc)) {
            return match->GetPVPMatchTeamIndex(npc);
        }
    }
    return 0;
}

int CPVPMatchSystem::GetRelation(User& user, User& target) {
    L2_GUARD_FUNCTION();
    if (CUndergroundColosseum::IsPartyPVPPlayer(user) && CUndergroundColosseum::IsPartyPVPPlayer(target)) {
        CParty* const userParty = user.GetParty();
        CParty* const targetParty = target.GetParty();
        CUndergroundColosseum* const userMatch = userParty != nullptr ? GetMatchUCByPartyId(userParty->Id()) : nullptr;
        CUndergroundColosseum* const targetMatch = targetParty != nullptr ? GetMatchUCByPartyId(targetParty->Id()) : nullptr;
        if (userMatch != nullptr && targetMatch != nullptr &&
            userMatch->GetManagerNpcId() == targetMatch->GetManagerNpcId()) {
            return userParty->Id() == targetParty->Id() ? 2 : 1;
        }
    }
    if (!user.IsInSamePVPMatch(target, PVPMatchType::NONE, PVPMatchPlayerState::COMBAT)) {
        return 0;
    }
    const int cleftRelation = m_CleftMatch->GetRelation(user, target);
    if (cleftRelation != 0) {
        return cleftRelation;
    }
    return m_BlockUpsetManager->GetRelation(user, target);
}

CPVPMatch* CPVPMatchSystem::GetMatchByUser(User& user) {
    // Current L2Server 0x009f7470 obtains the DBID once before the loop.
    L2_GUARD_FUNCTION();
    const std::uint32_t userDatabaseId = user.GetDBID();
    for (CPVPMatch2* const match : m_PVPMatch2Map) {
        if (match != nullptr && match->IsRegisteredUser(userDatabaseId)) {
            return match;
        }
    }
    if (user.IsInPVPMatch(PVPMatchType::CLEFT, PVPMatchPlayerState::NONE) &&
        m_CleftMatch->IsCleftPlayer(static_cast<int>(user.Id()))) {
        return m_CleftMatch;
    }
    return nullptr;
}

CPVPMatch2* CPVPMatchSystem::GetMatchByMatchType(const PVP_MATCH_TYPE type) {
    L2_GUARD_FUNCTION();
    for (CPVPMatch2* const match : m_PVPMatch2Map) {
        if (match != nullptr && match->GetMatchTypeDetail() == type) {
            return match;
        }
    }
    return nullptr;
}

bool CPVPMatchSystem::IsCrataeCubeUser(User& user) {
    L2_GUARD_FUNCTION();
    for (CPVPMatch2* const match : m_PVPMatch2Map) {
        if (match != nullptr && match->IsCrataeCubeUser(user)) {
            return true;
        }
    }
    return false;
}

PVP_MATCH_TYPE CPVPMatchSystem::GetMatchTypeByUserLevel(const int level) {
    L2_GUARD_FUNCTION();
    if (level >= 70 && level <= 75) {
        return PVPMT_COC_70;
    }
    if (level >= 76 && level <= 79) {
        return PVPMT_COC_76;
    }
    if (level >= 80) {
        return PVPMT_COC_80;
    }
    return PVPMT_MAX;
}

void CPVPMatchSystem::CPVPPointTimeBlock::SetStartTime(const int hour, const int minute) {
    L2_GUARD_FUNCTION();
    m_nStartHour = hour;
    m_nStartMinute = minute;
}

void CPVPMatchSystem::CPVPPointTimeBlock::SetEndTime(const int hour, const int minute) {
    L2_GUARD_FUNCTION();
    m_nEndHour = hour;
    m_nEndMinute = minute;
}

void CPVPMatchSystem::CPVPPointTimeBlock::SetWeekDays(int* const weekDays) {
    L2_GUARD_FUNCTION();
    std::memcpy(m_arrWeekDays, weekDays, sizeof(m_arrWeekDays));
}

bool CPVPMatchSystem::CPVPPointTimeBlock::IsInTime(std::tm* const now) {
    L2_GUARD_FUNCTION();
    if (now == nullptr || m_arrWeekDays[now->tm_wday] == 0) {
        return false;
    }
    if (now->tm_hour < m_nStartHour || now->tm_hour > m_nEndHour) {
        return false;
    }
    if (now->tm_hour == m_nStartHour && now->tm_min < m_nStartMinute) {
        return false;
    }
    return now->tm_hour != m_nEndHour || now->tm_min <= m_nEndMinute;
}

bool CPVPMatchSystem::CPVPPointTimeBlock::IsValid() {
    L2_GUARD_FUNCTION();
    if (m_nStartHour > m_nEndHour || (m_nStartHour == m_nEndHour && m_nStartMinute > m_nEndMinute)) {
        return false;
    }
    return std::any_of(std::begin(m_arrWeekDays), std::end(m_arrWeekDays), [](const int enabled) { return enabled != 0; });
}

void CPVPMatchSystem::CPVPPointTimeBlock::ShowInfo(CUserSocket* const socket) {
    L2_GUARD_FUNCTION();
    if (socket == nullptr) {
        return;
    }
    static constexpr const char16_t* kWeekDays[7] = {u"Sun", u"Mon", u"Tue", u"Wed", u"Thu", u"Fri", u"Sat"};
    std::u16string weekDays;
    for (int day = 0; day < 7; ++day) {
        if (m_arrWeekDays[day] != 0) {
            weekDays += kWeekDays[day];
            weekDays += u' ';
        }
    }
    socket->SendSystemMessage(u"SYS", weekDays);
    char16_t buffer[1024]{};
    FormatUtf16To(
        buffer, std::size(buffer), u"StartTime = %02d:%02d, EndTime = %02d:%02d", m_nStartHour, m_nStartMinute, m_nEndHour,
        m_nEndMinute
    );
    socket->SendSystemMessage(u"SYS", buffer);
}

CUndergroundColosseum* CPVPMatchSystem::GetMatchUCByMatchType(const PVP_MATCH_TYPE type) {
    L2_GUARD_FUNCTION();
    return IsValidUC(type) ? m_PVPMatch2Map[type]->CastUC() : nullptr;
}

namespace UCMatchHelper {

PVPMatchResult RegisterParty(
    CUndergroundColosseum* const match, const UCMaxParty maximumParty, const std::uint32_t partyId
) {
    // Current L2Server 0x009f66d4.
    L2_GUARD_FUNCTION();
    return match == nullptr ? PVPMR_INVALID_MATCH : match->RegisterParty(maximumParty, partyId);
}

void UnregisterParty(CUndergroundColosseum* const match, const std::uint32_t partyId) {
    // Current L2Server 0x009f6798.
    L2_GUARD_FUNCTION();
    if (match != nullptr) {
        match->UnregisterParty(partyId, 0);
    }
}

PVPMatchResult IsRegisteredParty(const std::uint32_t partyId, const std::uint32_t matchType) {
    // Current L2Server 0x009faf74.
    L2_GUARD_FUNCTION();
    CUndergroundColosseum* const registered = CPVPMatchSystem::inst().GetMatchUCByPartyId(partyId);
    if (registered == nullptr) {
        return PVPMR_SUCCESS;
    }
    return static_cast<std::uint32_t>(registered->GetPVPMatchType()) == matchType ? PVPMR_ALREADY_REGISTERED
                                                                                  : PVPMR_ALREADY_REGISTERED_ANOTHER;
}

} // namespace UCMatchHelper

CUndergroundColosseum* CEventMatchHelper::GetUCMatch(const int matchId) {
    L2_GUARD_FUNCTION();
    const PVP_MATCH_TYPE type = static_cast<PVP_MATCH_TYPE>(matchId);
    if (!CPVPMatchSystem::IsValidUC(type)) {
        return nullptr;
    }
    return CPVPMatchSystem::inst().GetMatchUCByMatchType(type);
}

void CEventMatchHelper::RegisterParty(const int matchId, User& target, User& builder) {
    // Current L2Server 0x009fb224.
    L2_GUARD_FUNCTION();
    std::array<char16_t, 512> message{};
    PVPMatchResult result = PVPMR_SUCCESS;
    CParty* const party = target.GetParty();
    if (party == nullptr) {
        result = PVPMR_INVALID_PARTY_ID;
    } else {
        CUndergroundColosseum* const match = GetUCMatch(matchId);
        if (match == nullptr) {
            result = PVPMR_INVALID_MATCH;
        } else {
            result = UCMatchHelper::IsRegisteredParty(party->Id(), static_cast<std::uint32_t>(match->GetPVPMatchType()));
            if (result == PVPMR_SUCCESS) {
                result = UCMatchHelper::RegisterParty(match, UCM_BY_BUILDER, party->Id());
                if (result == PVPMR_SUCCESS) {
                    FormatUtf16To(message.data(), message.size(), u"Registering party(%s) is accepted.", target.GetName());
                }
            }
        }
    }
    if (result != PVPMR_SUCCESS) {
        FormatUtf16To(message.data(), message.size(), u"Failed to registration. error code(%d)", static_cast<int>(result));
    }
    builder.SendSystemMessage(u"SYS", message.data());
}

void CEventMatchHelper::UnregisterParty(const int matchId, User& target, User& builder) {
    // Current L2Server 0x009fb384.
    L2_GUARD_FUNCTION();
    PVPMatchResult result = PVPMR_SUCCESS;
    CParty* const party = target.GetParty();
    if (party == nullptr) {
        result = PVPMR_INVALID_PARTY_ID;
    } else {
        CUndergroundColosseum* const match = GetUCMatch(matchId);
        if (match == nullptr) {
            result = PVPMR_INVALID_MATCH;
        } else if (UCMatchHelper::IsRegisteredParty(party->Id(), static_cast<std::uint32_t>(match->GetPVPMatchType())) !=
            PVPMR_SUCCESS) {
            UCMatchHelper::UnregisterParty(match, party->Id());
            builder.SendSystemMessage(u"SYS", u"Unregistering party is accepted.");
            return;
        } else {
            result = PVPMR_INVALID_PARTY_ID;
        }
    }

    std::array<char16_t, 512> message{};
    FormatUtf16To(
        message.data(), message.size(), u"Failed!!! Result of unregistering party to UC. error code(%d)",
        static_cast<int>(result)
    );
    builder.SendSystemMessage(u"SYS", message.data());
}

void CEventMatchHelper::StartMatch(const int matchId, User& user) {
    // Current L2Server 0x009fb4dc.
    L2_GUARD_FUNCTION();
    CUndergroundColosseum* const match = GetUCMatch(matchId);
    if (match == nullptr) {
        user.SendSystemMessage(u"SYS", u"Invalid match.");
        return;
    }
    user.SendSystemMessage(
        u"SYS", match->StartMatchByBuilder() ? u"Starting match is accepted." : u"Failed to starting match."
    );
}

void CEventMatchHelper::UnregisterAll(const int matchId, User& user) {
    // Current L2Server 0x009fb5d0.
    L2_GUARD_FUNCTION();
    CUndergroundColosseum* const match = GetUCMatch(matchId);
    if (match == nullptr) {
        user.SendSystemMessage(u"SYS", u"Invalid match.");
        return;
    }
    match->UnregisterAll();
    user.SendSystemMessage(u"SYS", u"Reseting match is accepted.");
}

void CEventMatchHelper::StartObserve(const int matchId, User& user) {
    L2_GUARD_FUNCTION();
    CUndergroundColosseum* const match = GetUCMatch(matchId);
    if (match == nullptr) {
        if (user.IsBuilder()) {
            user.SendSystemMessage(u"SYS", u"Invalid match.");
        }
        return;
    }
    match->RegisterObserve(user);
}

void CEventMatchHelper::EndObserve(const int matchId, User& user) {
    // Current L2Server 0x009fb794.
    L2_GUARD_FUNCTION();
    CUndergroundColosseum* const match = GetUCMatch(matchId);
    if (match == nullptr) {
        if (user.IsBuilder()) {
            user.SendSystemMessage(u"SYS", u"Invalid match.");
        }
        return;
    }
    match->UnregisterObserve(user);
}

bool CPVPMatchSystem::IsPVPPlayer(User* const user) {
    L2_GUARD_FUNCTION();
    return user != nullptr &&
        (CUndergroundColosseum::IsPartyPVPPlayer(*user) || IsCrataeCubeUser(*user) ||
            user->IsInPVPMatch(PVPMatchType::NONE, PVPMatchPlayerState::COMBAT));
}

void CPVPMatchSystem::UserDie(User* const user, User* enemy) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || !IsPVPPlayer(user) || user->IsAlive()) {
        return;
    }
    CParty* const party = user->GetParty();
    CPVPMatch* match = party != nullptr ? GetMatchUCByPartyId(party->Id()) : nullptr;
    if (match == nullptr) {
        match = GetMatchByUser(*user);
        if (match == nullptr) {
            return;
        }
        if (enemy != nullptr && enemy->IsPet()) {
            enemy = nullptr;
        }
    }
    match->UserDie(user, enemy);
}

CPVPMatch2* CPVPMatchSystem::GetMatchByUserLevel(const int level) {
    L2_GUARD_FUNCTION();
    return GetMatchByMatchType(GetMatchTypeByUserLevel(level));
}

void CPVPMatchSystem::GetMyStatus(User& user, int& status) {
    L2_GUARD_FUNCTION();
    if (!user.IsInPVPMatch(PVPMatchType::CLEFT, PVPMatchPlayerState::COMBAT)) {
        return;
    }
    // status |= 0x2000.
    status |= MSB_PVP_MATCH;
    if (user.m_data->m_cPVPMatchTeamID == 1) {
        // status |= 0x4000.
        status |= MSB_PVP_MATCH_BLUE;
    } else if (user.m_data->m_cPVPMatchTeamID == 2) {
        // status |= 0x8000.
        status |= MSB_PVP_MATCH_RED;
    }
    if (m_CleftMatch->IsCat(static_cast<int>(user.Id()))) {
        // status |= 0x10000.
        status |= MSB_PVP_MATCH_MASTER;
    }
}

void CPVPMatchSystem::GetRelationTo(User& user, User& target, int& relation) {
    L2_GUARD_FUNCTION();
    if (user.m_data->m_PVPMatchType == PVPMatchType::NONE || user.m_data->m_PVPMatchType != target.m_data->m_PVPMatchType) {
        return;
    }
    const int matchRelation = GetRelation(user, target);
    if (matchRelation == 1) {
        // relation |= 0x400000.
        relation |= ERB_ENEMY_IN_PVP_MATCH;
    } else if (matchRelation == 2) {
        // relation |= 0x200000.
        relation |= ERB_COMRADE_IN_PVP_MATCH;
    }
    if (user.m_data->m_cPVPMatchTeamID == 1) {
        // relation |= 0x800000.
        relation |= ERB_BLUE_IN_PVP_MATCH;
    } else if (user.m_data->m_cPVPMatchTeamID == 2) {
        // relation |= 0x1000000.
        relation |= ERB_RED_IN_PVP_MATCH;
    }
    if (m_CleftMatch->IsCat(static_cast<int>(user.Id()))) {
        // relation |= 0x2000000.
        relation |= ERB_MASTER_IN_PVP_MATCH;
    }
}

CPVPMatch2* CPVPMatchSystem::GetMatchByManagerNpcId(const std::uint32_t npcId) {
    L2_GUARD_FUNCTION();
    CObject* const object = CObject::GetObjectW(npcId);
    if (object == nullptr || !object->IsNPC() || object->CastNPC() == nullptr) {
        return nullptr;
    }
    for (CPVPMatch2* const match : m_PVPMatch2Map) {
        if (match != nullptr && match->GetManagerNpcId() == npcId) {
            return match;
        }
    }
    return nullptr;
}

CUndergroundColosseum* CPVPMatchSystem::GetMatchUCByManagerNpcId(const std::uint32_t npcId) {
    L2_GUARD_FUNCTION();
    CObject* const object = CObject::GetObjectW(npcId);
    if (object == nullptr || !object->IsNPC() || object->CastNPC() == nullptr) {
        return nullptr;
    }
    for (int type = PVPMT_UGC_40; type <= PVPMT_UGC_UL; ++type) {
        CPVPMatch2* const match = m_PVPMatch2Map[type];
        if (match != nullptr && match->GetManagerNpcId() == npcId) {
            return match->CastUC();
        }
    }
    CPVPMatch2* const eventMatch = m_PVPMatch2Map[PVPMT_UGC_EVENT];
    return eventMatch != nullptr && eventMatch->GetManagerNpcId() == npcId ? eventMatch->CastUC() : nullptr;
}

CCrataeCube* CPVPMatchSystem::GetMatchCCByManagerNpcId(const std::uint32_t npcId) {
    L2_GUARD_FUNCTION();
    CObject* const object = CObject::GetObjectW(npcId);
    if (object == nullptr || !object->IsNPC() || object->CastNPC() == nullptr) {
        return nullptr;
    }
    for (int type = PVPMT_COC_70; type <= PVPMT_COC_80; ++type) {
        CPVPMatch2* const match = m_PVPMatch2Map[type];
        if (match != nullptr && match->GetManagerNpcId() == npcId) {
            return match->CastCC();
        }
    }
    return nullptr;
}

bool CPVPMatchSystem::RegisterResurrectionTower(
    const std::uint32_t team, const std::uint32_t matchId, const std::uint32_t npcId
) {
    L2_GUARD_FUNCTION();
    CUndergroundColosseum* const match = GetMatchUCByManagerNpcId(matchId);
    return match != nullptr && match->RegisterResurrectionTower(team, npcId);
}

bool CPVPMatchSystem::RegisterResurrectionNPC(const std::uint32_t npcId, const std::uint32_t matchId) {
    L2_GUARD_FUNCTION();
    CCrataeCube* const match = GetMatchCCByManagerNpcId(matchId);
    return match != nullptr && match->RegisterResurrectionNPC(npcId);
}

bool CPVPMatchSystem::UnregisterUser(const std::uint32_t matchId, User& user, int) {
    L2_GUARD_FUNCTION();
    CPVPMatch2* match = GetMatchByManagerNpcId(matchId);
    if (match == nullptr) {
        CPVPMatch* const genericMatch = GetMatchByUser(user);
        match = genericMatch != nullptr && genericMatch->IsPVPMatch2() ? genericMatch->CastPVPMatch2() : nullptr;
    }
    return match != nullptr && match->UnregisterUser(user.Id(), 0);
}

int CPVPMatchSystem::GetRankUserPVPMatch(const std::uint32_t matchId, const std::uint32_t userId) {
    L2_GUARD_FUNCTION();
    CPVPMatch2* const match = GetMatchByManagerNpcId(matchId);
    return match != nullptr ? match->GetRankUserPVPMatch(userId) : 0;
}

bool CPVPMatchSystem::CrataeMatchUsingBuilder(const std::uint32_t matchId, const int command) {
    L2_GUARD_FUNCTION();
    CPVPMatch2* const base = GetMatchByManagerNpcId(matchId);
    CCrataeCube* const match = base != nullptr ? base->CastCC() : nullptr;
    if (match == nullptr) {
        return false;
    }
    match->m_nStatus = command;
    if (command == 0) {
        return match->StartMatch();
    }
    if (command == 2) {
        return match->ReadyMatch();
    }
    match->TriggerEndMatch();
    return true;
}

void CPVPMatchSystem::UnregisterParty(const std::uint32_t matchId, const std::uint32_t partyId) {
    L2_GUARD_FUNCTION();
    UCMatchHelper::UnregisterParty(GetMatchUCByManagerNpcId(matchId), partyId);
}

bool CPVPMatchSystem::StartMatch(
    const std::uint32_t matchId, const std::uint32_t bluePartyId, const std::uint32_t redPartyId
) {
    L2_GUARD_FUNCTION();
    CUndergroundColosseum* const match = GetMatchUCByManagerNpcId(matchId);
    return match != nullptr && match->StartMatchWithParty(bluePartyId, redPartyId);
}

void CPVPMatchSystem::EndMatch(const std::uint32_t matchId) {
    L2_GUARD_FUNCTION();
    if (CPVPMatch2* const match = GetMatchByManagerNpcId(matchId)) {
        match->TriggerEndMatch();
    }
}

int CPVPMatchSystem::CheckRegisterParty(const std::uint32_t matchId, const std::uint32_t partyId) {
    L2_GUARD_FUNCTION();
    CUndergroundColosseum* const match = GetMatchUCByManagerNpcId(matchId);
    return match != nullptr ? match->CheckRegisterParty(partyId) : PVPMR_INVALID_MATCH;
}

void CPVPMatchSystem::ShowPVPPointRewardTime(CUserSocket* const socket) {
    L2_GUARD_FUNCTION();
    if (socket == nullptr) {
        return;
    }
    for (CPVPPointTimeBlock* const block : m_vecGivePVPPointTime) {
        if (block != nullptr) {
            block->ShowInfo(socket);
        }
    }
}

bool CPVPMatchSystem::LeaveWorld(User& user) {
    L2_GUARD_FUNCTION();
    if (!user.IsInPVPMatch(PVPMatchType::NONE, PVPMatchPlayerState::NONE)) {
        return false;
    }
    if (m_CleftMatch->LeaveWorld(user)) {
        return true;
    }
    if (m_BlockUpsetManager->LeaveWorld(user)) {
        return true;
    }
    if (IsCrataeCubeUser(user)) {
        UnregisterUser(0, user, 3);
    }
    return false;
}

bool CPVPMatchSystem::IsInGivePVPPointTime() {
    L2_GUARD_FUNCTION();
    const std::time_t value = std::time(nullptr);
    std::tm* const now = std::localtime(&value);
    for (CPVPPointTimeBlock* const block : m_vecGivePVPPointTime) {
        if (block != nullptr && block->IsInTime(now)) {
            return true;
        }
    }
    return false;
}

int CPVPMatchSystem::GetRegisteredPartiesPacket(const std::uint32_t matchId, char* const packet, const int packetSize) {
    L2_GUARD_FUNCTION();
    CUndergroundColosseum* const match = GetMatchUCByManagerNpcId(matchId);
    return match != nullptr ? match->GetRegisteredPartiesPacket(packet, packetSize) : 0;
}

int CPVPMatchSystem::RegisterUser(const std::uint32_t matchId, const std::uint32_t userId) {
    L2_GUARD_FUNCTION();
    const std::CDualKey<int, int> key(g_nWorldId, static_cast<int>(userId));
    CSPointer<User> user = g_UserDBIDMap.FindObject(key);
    if (!user) {
        return PVPMR_INVALID_USER;
    }
    CPVPMatch2* const match = GetMatchByMatchType(GetMatchTypeByUserLevel(user->Level()));
    if (match == nullptr) {
        return PVPMR_INVALID_MATCH;
    }
    if (match->GetGateManNpcId() == 0) {
        match->SetGateManNpcId(matchId);
    }
    return match->RegisterUser(userId);
}

int CPVPMatchSystem::CheckRegisterUser(const std::uint32_t matchId, const std::uint32_t userId) {
    L2_GUARD_FUNCTION();
    if (CPVPMatch2* const match = GetMatchByManagerNpcId(matchId)) {
        return match->CheckRegisterUser(userId);
    }
    const std::CDualKey<int, int> key(g_nWorldId, static_cast<int>(userId));
    CSPointer<User> user = g_UserDBIDMap.FindObject(key);
    if (!user) {
        return PVPMR_INVALID_USER;
    }
    CPVPMatch2* const match = GetMatchByUserLevel(user->Level());
    if (match == nullptr) {
        return PVPMR_INVALID_USER_LEVEL;
    }
    if (match->GetGateManNpcId() == 0) {
        match->SetGateManNpcId(matchId);
    }
    return match->CheckRegisterUser(userId);
}

PVPMatchResult CPVPMatchSystem::RegisterParty(const std::uint32_t matchId, const std::uint32_t partyId) {
    L2_GUARD_FUNCTION();
    PVPMatchResult result = UCMatchHelper::IsRegisteredParty(partyId, matchId);
    if (result == PVPMR_SUCCESS) {
        result = UCMatchHelper::RegisterParty(GetMatchUCByManagerNpcId(matchId), UCM_BY_NPC, partyId);
    }
    return result;
}

void CPVPMatchSystem::AddGivePVPPointTime(CPVPPointTimeBlock* const block) {
    L2_GUARD_FUNCTION();
    if (block != nullptr) {
        m_vecGivePVPPointTime.push_back(block);
    }
}
