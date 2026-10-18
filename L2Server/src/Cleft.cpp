#include "Cleft.h"
#include "NpcServer.h"
#include "Object.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Skill.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/ItemMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/platform/Runtime.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwchar>
#include <optional>
#include <string>

namespace {


constexpr std::array<int, 101> kRankByPercent = [] {
    std::array<int, 101> ranks{};
    for (std::size_t percent = 0; percent <= 10; ++percent) {
        ranks[percent] = 2;
    }
    for (std::size_t percent = 11; percent <= 25; ++percent) {
        ranks[percent] = 3;
    }
    for (std::size_t percent = 26; percent <= 50; ++percent) {
        ranks[percent] = 4;
    }
    for (std::size_t percent = 51; percent <= 100; ++percent) {
        ranks[percent] = 5;
    }
    return ranks;
}();

constexpr std::array<double, 12> kSpMultiplier = {
    0.0,
    2.5,
    2.3,
    2.2,
    2.0,
    1.8,
    0.0,
    1.0,
    0.9,
    0.8,
    0.6,
    0.5,
};

bool IsTeam(const BlueRedTeam::Enum team) { return team == BlueRedTeam::BLUE || team == BlueRedTeam::RED; }

std::size_t TeamIndex(const BlueRedTeam::Enum team) { return static_cast<std::size_t>(team); }

User* FindUser(const std::uint32_t userId) {
    CObject* const object = CObject::GetObjectW(userId);
    return object != nullptr && object->IsPC() ? object->CastUser() : nullptr;
}

void SetMatchState(User& user, const BlueRedTeam::Enum team, const PVPMatchPlayerState::Enum state) {
    user.m_data->m_cPVPMatchTeamID = CPVPMatch::GetMatchTeamID(team);
    user.m_data->m_PVPMatchType = PVPMatchType::CLEFT;
    user.m_data->m_PVPMatchPlayerState = state;
}

void RemoveFromCatQueue(CCleft::CatData::UserIds& queue, const int userId) {
    CCleft::CatData::UserIds retained;
    while (!queue.empty()) {
        const int current = queue.front();
        queue.pop_front();
        if (current != userId) {
            retained.push_back(current);
        }
    }
    queue.swap(retained);
}

} // namespace

// Portable registration: the runtime ID is no longer a shipping number.
// static_assert(
//     L2ServerMemory::kCCleftMemberInfoClassId ==
//     CCleftMemberInfo::kMemoryClassId)

CCleftMemberInfo::CCleftMemberInfo(
    CCleft* const match, const BlueRedTeam::Enum team, const std::uint32_t userId, const char16_t* const userName
) :
    CPVPMatchMemberInfo(team, userId, userName),
    m_CleftMatch(match),
    m_StartTime(0),
    m_DieTime(0),
    m_CleftTowerCount(0),
    m_nKillCount(0),
    m_nDeathCount(0),
    m_nRank(0) {}

CCleftMemberInfo::~CCleftMemberInfo() = default;

L2_DEFINE_OBJECT_CLASS_ID(CCleftMemberInfo)

L2_DEFINE_OBJECT_CLASS_NAME_LITERAL(CCleftMemberInfo)

L2_DEFINE_OBJECT_ALLOCATOR_LITERAL(CCleftMemberInfo)

L2_DEFINE_OBJECT_NEW(CCleftMemberInfo)

L2_DEFINE_OBJECT_ADD_REF(CCleftMemberInfo)

L2_DEFINE_OBJECT_RELEASE(CCleftMemberInfo)

int CCleftMemberInfo::GetPoint() const { return m_nKillCount + m_CleftTowerCount; }

void CCleftMemberInfo::MakeStateInfo(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    packet.Assemble("dddd", static_cast<int>(m_nUserId), m_CleftTowerCount, m_nKillCount, m_nDeathCount);
}

CCleft::CleftPoint::CleftPoint() { Init(); }

void CCleft::CleftPoint::Init() {
    m_Point = 0;
    m_CenterTowerPoint = 0;
    m_DefenceTowerPoint = 0;
    m_CatKillPoint = 0;
}

void CCleft::BanishCleftUser::operator()(CPVPMatchMemberInfo* const memberInfo) const {
    if (memberInfo != nullptr) {
        Do(*static_cast<CCleftMemberInfo*>(memberInfo));
    }
}

void CCleft::BanishCleftUser::Do(CCleftMemberInfo& memberInfo) const {
    L2_GUARD_FUNCTION();
    CObject* const object = CObject::GetObjectW(memberInfo.m_nUserId);
    User* const user = object != nullptr && object->IsPC() ? object->CastUser() : nullptr;
    if (user == nullptr) {
        return;
    }
    FVector position{};
    if (!CPVPMatch::GetRandomPos(memberInfo.m_CleftMatch->m_BanishingPos[TeamIndex(memberInfo.m_Team)], position)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"banising Pos data error. team = %d",
        //     static_cast<int>(memberInfo.m_Team))
        // L2_LOG_ERROR(Log,
        //     L"banising Pos data error. team = %d",
        //     static_cast<int>(memberInfo.m_Team))
        L2_LOG_ERROR(
            Log,
            "Cannot banish Cleft player object %u from team %d: no random exit position could be selected; teleport "
            "skipped.",
            memberInfo.m_nUserId, static_cast<int>(memberInfo.m_Team)
        );
        return;
    }
    user->DoNothing();
    user->TeleportToLocation(
        static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z), user->GetInZoneID(),
        0x10000, TeleportSourceType::CleftPCBanishWhenCleftEnd, nullptr, 0
    );
}

void CCleft::ReleaseCleftInfo::operator()(CPVPMatchMemberInfo* const memberInfo) const {
    if (memberInfo != nullptr) {
        Do(*static_cast<CCleftMemberInfo*>(memberInfo));
    }
}

void CCleft::ReleaseCleftInfo::Do(CCleftMemberInfo& memberInfo) const {
    // Current L2Server 0x005a1ef0.
    L2_GUARD_FUNCTION();
    CObject* const object = CObject::GetObjectW(memberInfo.m_nUserId);
    if (const User* const user = object != nullptr && object->IsPC() ? object->CastUser() : nullptr; user != nullptr) {
        user->m_data->m_cPVPMatchTeamID = 0;
        user->m_data->m_PVPMatchType = PVPMatchType::NONE;
        user->m_data->m_PVPMatchPlayerState = PVPMatchPlayerState::NONE;
    }
    L2_RELEASE(&(memberInfo), ObjectRefType::CreateDelete);
}

CCleft::_OnCombatStart::_OnCombatStart(const bool sendTotalState) : m_bSendTotalState(sendTotalState) {}

void CCleft::_OnCombatStart::operator()(CPVPMatchMemberInfo* const memberInfo) const {
    if (memberInfo != nullptr) {
        Do(*static_cast<CCleftMemberInfo*>(memberInfo));
    }
}

CCleft::CatData::CatData() : m_UserId(0), m_CatSelectedTick(0) {}

CCleft::CatData::~CatData() = default;

void CCleft::CatData::Init() {
    m_UserId = 0;
    m_CatSelectedTick = 0;
    m_NextCat.clear();
}

void CCleft::CatData::SetNewCat(const int userId) {
    m_UserId = userId;
    m_CatSelectedTick = runtime::GetTickCount();
}

CCleft::WinLoseSetting::WinLoseSetting() { Init(); }

void CCleft::WinLoseSetting::Init() {
    m_RewardItemClassId = 0;
    m_RewardItemCount = 0;
    m_SpBonus = 0;
}

CCleft::RewardHandler::RewardHandler(
    WinLoseSetting& setting, const PVPMatchGameResult::Enum result, const bool ignore15Min
) :
    m_Setting(setting), m_Result(result), m_bIgnore15Min(ignore15Min), m_targetPaddingD{} {}

CCleft::CCleft() :
    m_CombatTime(25),
    m_RewardTime(5),
    m_CatPeriod(3),
    m_MinRegisterCount(9),
    m_TeamBalance(true),
    m_pWaitingSkill(nullptr),
    m_OldCombatRemainSec(1500),
    m_bIgnore15Min(false),
    m_CleftPoints{},
    m_CleftTeamLine{},
    m_BlueTeam(&m_CleftTeamLine[BlueRedTeam::BLUE]),
    m_RedTeam(&m_CleftTeamLine[BlueRedTeam::RED]),
    m_CatData{} {
    InitSetting();
    _RemoveAllCleftPlayers();
}

CCleft::~CCleft() { _RemoveAllCleftPlayers(); }

PVPMatchType::Enum CCleft::GetPVPMatchType() const { return PVPMatchType::CLEFT; }

void CCleft::Init() {
    L2_GUARD_FUNCTION();
    CPVPMatch::Init();
    RemoveAllCleftPlayers();
}

void CCleft::InitSetting() {
    L2_GUARD_FUNCTION();
    CPVPMatch::InitSetting();
    m_CombatTime = 25;
    m_RewardTime = 5;
    m_CatPeriod = 3;
    m_MinRegisterCount = 9;
    m_TeamBalance = true;
    m_pWaitingSkill = nullptr;
    m_WinSetting.Init();
    m_LoseSetting.Init();
    m_bIgnore15Min = false;
    m_OldCombatRemainSec = m_CombatTime * 60;
    ChangeState(PVPMatchState::INVALID);
}

void CCleft::OnNpcServerClosed() { CPVPMatch::OnNpcServerClosed(); }

int CCleft::GetCombatRemainSec() const {
    if (m_State != PVPMatchState::COMBAT) {
        return -1;
    }
    return (m_CombatTime * 60000 + m_CurrentStateStartTime - static_cast<std::int32_t>(runtime::GetTickCount())) / 1000;
}

void CCleft::ChangeState(const PVPMatchState::Enum state) {
    L2_GUARD_FUNCTION();
    if (state == m_State) {
        return;
    }
    CPVPMatch::ChangeState(state);
    SharedCleftInfo::Inst()->m_nCleftState = static_cast<int>(m_State);
    if (m_State != PVPMatchState::INVALID) {
        // Current 005a1c39 uses the creature manager directly, including SID 0.
        CObject* const object = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_ManagerNpcId));
        CNPC* const npc = object != nullptr && object->IsNPC() ? object->CastNPC() : nullptr;
        if (npc != nullptr) {
            g_npcserver.Send("cd", 0x88, m_ManagerNpcId);
        }
    }
}

void CCleft::ReadyPlayerToStart(const BlueRedTeam::Enum team, User& user) {
    L2_GUARD_FUNCTION();
    if (!IsTeam(team)) {
        return;
    }
    FVector position{};
    if (!GetRandomPos(m_StartingPos[TeamIndex(team)], position)) {
        return;
    }
    if (m_pWaitingSkill != nullptr) {
        user.InfectedSkillBy(nullptr, m_pWaitingSkill, nullptr, 0);
    }
    user.DoNothing();
    user.TeleportToLocation(
        static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z), user.GetInZoneID(),
        0x10000, TeleportSourceType::CleftStart, nullptr, 0
    );
}

bool CCleft::IsCleftPlayer(const int userId) {
    L2_GUARD(m_Lock);
    return _IsCleftPlayer(userId);
}

bool CCleft::_IsCleftPlayer(const int userId) {
    L2_GUARD_FUNCTION();
    return m_CleftTeamDB.find(static_cast<std::uint32_t>(userId)) != m_CleftTeamDB.end();
}

BlueRedTeam::Enum CCleft::GetSmallTeam() const {
    return m_BlueTeam->size() <= m_RedTeam->size() ? BlueRedTeam::BLUE : BlueRedTeam::RED;
}

int CCleft::GetTeamMemberCount(const BlueRedTeam::Enum team) const {
    return IsTeam(team) ? static_cast<int>(m_CleftTeamLine[TeamIndex(team)].size()) : 0;
}

void CCleft::GetCleftPlayerCount(int& blueCount, int& redCount) const {
    blueCount = static_cast<int>(m_BlueTeam->size());
    redCount = static_cast<int>(m_RedTeam->size());
}

bool CCleft::CanAddCleftPlayer(const BlueRedTeam::Enum team, User& user) {
    L2_GUARD_FUNCTION();
    if (!CanAddPlayer(user) || !IsTeam(team) || m_State == PVPMatchState::REWARD || user.m_data->m_nLevel <= 74) {
        return false;
    }
    if (!CheckDistanceWithNpc(user)) {
        user.SendSystemMessage(SystemMessageId::NPC_TOO_FAR);
        return false;
    }
    if (user.GetParty() != nullptr) {
        user.SendSystemMessage(SystemMessageId::CANNOT_ENTER_BATTLEFIELD_WHILE_IN_PARTY);
        return false;
    }
    return m_CleftTeamLine[TeamIndex(team)].size() < 45;
}

bool CCleft::EnterCleftPlayer(User& user) {
    L2_GUARD_FUNCTION();
    return AddCleftPlayer(GetSmallTeam(), user);
}

bool CCleft::AddCleftPlayer(const BlueRedTeam::Enum team, User& user) {
    L2_GUARD_FUNCTION();
    if (!CanAddCleftPlayer(team, user)) {
        return false;
    }
    bool result;
    {
        L2_GUARD(m_Lock);
        result = _AddCleftPlayer(team, user);
    }
    return result;
}

bool CCleft::_AddCleftPlayer(BlueRedTeam::Enum team, User& user) {
    L2_GUARD_FUNCTION();
    if (!IsTeam(team) || !CanAddCleftPlayer(team, user)) {
        return false;
    }

    const std::uint32_t userId = user.Id();
    const auto found = m_CleftTeamDB.find(userId);
    if (found != m_CleftTeamDB.end()) {
        auto* const member = static_cast<CCleftMemberInfo*>(found->second);
        if (member == nullptr || member->m_Team == team) {
            return false;
        }
        return _ChangeCleftPlayerTeam(member);
    }

    if (m_State == PVPMatchState::COMBAT && m_TeamBalance) {
        team = GetSmallTeam();
    } else if (m_State == PVPMatchState::REGISTER) {
        SetMatchState(user, team, PVPMatchPlayerState::WAITING);
    }

    auto* const member = new CCleftMemberInfo(this, team, userId, user.GetName());
    m_CleftTeamDB[userId] = member;
    m_CleftTeamLine[TeamIndex(team)].push_back(member);

    if (CUserSocket* const socket = user.GetSocket()) {
        CPacketHelperEx<20480> total;
        _MakeCleftListTotal(total);
        socket->Send("chb", 0xfe, 0x95, total.GetLength(), total.GetPacket());
    }
    _BroadcastToPlayer(
        m_CleftTeamDB, "chdddS", 0xfe, 0x95, 1, static_cast<int>(team), static_cast<int>(userId), user.GetName()
    );
    _OnCleftPlayerAdded(member);
    return true;
}

void CCleft::_OnCleftPlayerAdded(CCleftMemberInfo* const memberInfo) {
    L2_GUARD_FUNCTION();
    if (memberInfo == nullptr) {
        return;
    }
    if (m_State == PVPMatchState::REGISTER && m_CleftTeamDB.size() >= static_cast<std::size_t>(m_MinRegisterCount * 2)) {
        if (m_TeamBalance) {
            _BalanceTeam();
        }
        ChangeState(PVPMatchState::COMBAT);
        _OnCombat();
        return;
    }
    if (m_State == PVPMatchState::COMBAT) {
        _OnCombatStart(true).Do(*memberInfo);
    }
}

bool CCleft::ChangeCleftPlayerTeam(const int userId) {
    bool result;
    {
        L2_GUARD(m_Lock);
        const auto found = m_CleftTeamDB.find(static_cast<std::uint32_t>(userId));
        result = found != m_CleftTeamDB.end() && _ChangeCleftPlayerTeam(static_cast<CCleftMemberInfo*>(found->second));
    }
    return result;
}

bool CCleft::_ChangeCleftPlayerTeam(CCleftMemberInfo* const memberInfo) {
    L2_GUARD_FUNCTION();
    if (memberInfo == nullptr || m_State == PVPMatchState::COMBAT || !IsTeam(memberInfo->m_Team) ||
        FindUser(memberInfo->m_nUserId) == nullptr) {
        return false;
    }

    const BlueRedTeam::Enum oldTeam = memberInfo->m_Team;
    const BlueRedTeam::Enum newTeam = GetOpposite(oldTeam);
    TeamLine& oldLine = m_CleftTeamLine[TeamIndex(oldTeam)];
    auto oldPosition = oldLine.end();
    for (auto it = oldLine.begin(); it != oldLine.end(); ++it) {
        if (*it == memberInfo) {
            oldPosition = it;
            break;
        }
    }
    if (oldPosition == oldLine.end()) {
        return false;
    }
    oldLine.erase(oldPosition);
    m_CleftTeamLine[TeamIndex(newTeam)].push_back(memberInfo);
    memberInfo->m_Team = newTeam;
    if (User* const user = FindUser(memberInfo->m_nUserId)) {
        user->m_data->m_cPVPMatchTeamID = GetMatchTeamID(newTeam);
    }
    _BroadcastToPlayer(
        m_CleftTeamDB, "chdddd", 0xfe, 0x95, 3, static_cast<int>(memberInfo->m_nUserId), static_cast<int>(oldTeam),
        static_cast<int>(newTeam)
    );
    return true;
}

bool CCleft::RemoveCleftPlayer(const int userId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    return _RemoveCleftPlayer(userId);
}

bool CCleft::_RemoveCleftPlayer(const int userId) {
    L2_GUARD_FUNCTION();
    const auto found = m_CleftTeamDB.find(static_cast<std::uint32_t>(userId));
    if (found == m_CleftTeamDB.end()) {
        return false;
    }
    auto* const member = static_cast<CCleftMemberInfo*>(found->second);
    if (member == nullptr) {
        m_CleftTeamDB.erase(found);
        return false;
    }

    _BroadcastToPlayer(
        m_CleftTeamDB, "chddd", 0xfe, 0x95, 2, static_cast<int>(member->m_Team), static_cast<int>(member->m_nUserId)
    );
    m_CleftTeamDB.erase(found);
    RemoveFromCatQueue(m_CatData[BlueRedTeam::BLUE].m_NextCat, userId);
    RemoveFromCatQueue(m_CatData[BlueRedTeam::RED].m_NextCat, userId);
    _RemoveCleftPlayerInfo(member);
    return true;
}

void CCleft::_RemoveCleftPlayerInfo(CCleftMemberInfo* const memberInfo) {
    L2_GUARD_FUNCTION();
    if (memberInfo == nullptr || !IsTeam(memberInfo->m_Team)) {
        return;
    }
    const BlueRedTeam::Enum team = memberInfo->m_Team;
    TeamLine& line = m_CleftTeamLine[TeamIndex(team)];
    for (auto it = line.begin(); it != line.end(); ++it) {
        if (*it == memberInfo) {
            line.erase(it);
            break;
        }
    }

    if (m_State == PVPMatchState::COMBAT) {
        BanishCleftUser().Do(*memberInfo);
        if (m_CatData[TeamIndex(team)].m_UserId == static_cast<int>(memberInfo->m_nUserId)) {
            std::vector<User*> changed;
            _SelectRandomCat(team, changed);
            SendInfosToCats(changed);
        }
        if (line.empty()) {
            const bool earlyAbort =
                !m_bIgnore15Min && runtime::GetTickCount() <= static_cast<std::uint32_t>(m_CurrentStateStartTime + 900000);
            if (earlyAbort) {
                const int elapsed = GetCombatElapsedSec();
                _BroadcastToPlayer(m_CleftTeamDB, "chcchch", 0x62, 0xae0, 2, 0x15, elapsed / 60, 0x15, elapsed % 60);
                _BroadcastToPlayer(m_CleftTeamDB, "chddd", 0xfe, 0x96, 3, BlueRedTeam::MAX, BlueRedTeam::MAX);
                ChangeState(PVPMatchState::REGISTER);
                _OnRegister();
            } else {
                _BroadcastToPlayer(m_CleftTeamDB, "chc", 0x62, 0xadf, 0);
                ChangeState(PVPMatchState::REWARD);
                _OnReward();
            }
        }
    }
    ReleaseCleftInfo().Do(*memberInfo);
}

void CCleft::RemoveAllCleftPlayers() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        _RemoveAllCleftPlayers();
    }
}

void CCleft::_RemoveAllCleftPlayers() {
    L2_GUARD_FUNCTION();
    for (CleftPoint& point : m_CleftPoints) {
        point.Init();
    }
    for (CatData& cat : m_CatData) {
        cat.Init();
    }
    for (const auto& entry : m_CleftTeamDB) {
        if (entry.second != nullptr) {
            ReleaseCleftInfo()(entry.second);
        }
    }
    m_CleftTeamDB.clear();
    for (TeamLine& line : m_CleftTeamLine) {
        line.clear();
    }
}

void CCleft::_RemoveTeamPlayers(const BlueRedTeam::Enum team, const bool banish) {
    L2_GUARD_FUNCTION();
    if (team >= BlueRedTeam::MAX) {
        return;
    }

    for (auto position = m_CleftTeamDB.begin(); position != m_CleftTeamDB.end();) {
        auto* const member = static_cast<CCleftMemberInfo*>(position->second);
        if (member->m_Team != team) {
            ++position;
            continue;
        }

        position = m_CleftTeamDB.erase(position);
        _RemoveCleftPlayerInfo(member);
        if (banish) {
            BanishCleftUser().Do(*member);
        }
    }
    m_CleftTeamLine[static_cast<int>(team)].clear();
}

void CCleft::CheckLogoutPlayers() {
    std::optional<std::vector<std::uint32_t>> disconnectedStorage;
    {
        L2_GUARD(m_Lock);
        auto& disconnected = disconnectedStorage.emplace();
        for (const auto& entry : m_CleftTeamDB) {
            if (FindUser(entry.first) == nullptr) {
                disconnected.push_back(entry.first);
            }
        }
        for (const std::uint32_t id : disconnected) {
            _RemoveCleftPlayer(static_cast<int>(id));
        }
    }
}

void CCleft::_OnRegister() {
    L2_GUARD_FUNCTION();
    for (CleftPoint& point : m_CleftPoints) {
        point.Init();
    }
    _RemoveAllCleftPlayers();
}

void CCleft::_OnCombatStart::Do(CCleftMemberInfo& memberInfo) const {
    L2_GUARD_FUNCTION();
    CCleft* const match = memberInfo.m_CleftMatch;
    User* const user = FindUser(memberInfo.m_nUserId);
    if (match == nullptr || user == nullptr || !IsTeam(memberInfo.m_Team)) {
        return;
    }

    if (user->m_data->GetHP() == 0.0) {
        user->ResurrectBy(user, 100);
    }
    if (user->IsTransformed(TransformType::Flying) != nullptr) {
        user->DispelBySlot(ABNS_TRANSFORM, user, -1, nullptr, SPET_DISPEL, false);
        user->m_bAbnormalStatusChanged = true;
        user->ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
        user->CheckAbnormalVisualEffect();
    }

    match->ReadyPlayerToStart(memberInfo.m_Team, *user);
    memberInfo.m_StartTime = runtime::GetTickCount();
    SetMatchState(*user, memberInfo.m_Team, PVPMatchPlayerState::COMBAT);
    user->BroadcastCharInfoUpdate(false);
    match->m_CatData[TeamIndex(memberInfo.m_Team)].m_NextCat.push_back(static_cast<int>(memberInfo.m_nUserId));

    if (CUserSocket* const socket = user->GetSocket()) {
        socket->Send("chd", 0xfe, 0x95, -1);
    }
    user->SendSystemMessage(SystemMessageId::AERIAL_CLEFT_ACTIVATED_FLIGHT_TRANSFORMATION_AVAILABLE_SOON);
    if (m_bSendTotalState) {
        CPacketHelperEx<20480> packet;
        match->_MakeCleftStateTotal(packet);
        if (CUserSocket* const socket = user->GetSocket()) {
            socket->Send("chb", 0xfe, 0x96, packet.GetLength(), packet.GetPacket());
        }
    }
}

void CCleft::RewardHandler::operator()(CPVPMatchMemberInfo* const memberInfo) const {
    auto* const member = static_cast<CCleftMemberInfo*>(memberInfo);
    if (member == nullptr) {
        return;
    }
    User* const user = FindUser(member->m_nUserId);
    if (user == nullptr) {
        return;
    }

    if (member->m_DieTime != 0) {
        user->ResurrectBy(user, 100);
    }

    int rankSp = 0;
    if (m_bIgnore15Min || member->m_StartTime + 900000U <= runtime::GetTickCount()) {
        InventoryParams parameters;
        parameters.nItemClassID = m_Setting.m_RewardItemClassId;
        parameters.nCount = m_Setting.m_RewardItemCount;
        {
            L2_WRITE_GUARD(user->m_inv);
            user->AddItemToInventory(parameters);
        }

        const int rank = std::clamp(member->m_nRank, 0, 5);
        const std::size_t multiplierIndex = static_cast<std::size_t>(rank + (m_Result == PVPMatchGameResult::WIN ? 0 : 6));
        rankSp = static_cast<int>(static_cast<double>(m_Setting.m_SpBonus) * kSpMultiplier[multiplierIndex]);
    }
    user->IncrementParam(
        PARAM_SP,
        static_cast<std::int64_t>(rankSp) + static_cast<std::int64_t>(m_Setting.m_SpKillBonus) * member->m_nKillCount
    );
}

void CCleft::_OnCombat() {
    L2_GUARD_FUNCTION();
    class TLMFunc final : public CTLMFunc {
    public:
        explicit TLMFunc(const FVector& position) : position_(&position) {}

        bool CanDo(CObject* object, double) override {
            User* const user = object->CastUser();
            if ((user == nullptr || (!object->IsDeleting() && object->m_Deleted == 0)) &&
                !user->IsInPVPMatch(PVPMatchType::CLEFT, PVPMatchPlayerState::NONE)) {
                user->StopMove();
                user->TeleportToLocation(
                    static_cast<int>(position_->x), static_cast<int>(position_->y), static_cast<int>(position_->z),
                    static_cast<unsigned int>(user->GetDir().Yaw), 0x10000, TeleportSourceType::CleftPCBanishWhenCleftStart,
                    nullptr, 0
                );
            }
            return true;
        }

    private:
        const FVector* position_;
    };

    m_OldCombatRemainSec = m_CombatTime * 60;
    const _OnCombatStart start(false);
    for (const auto& entry : m_CleftTeamDB) {
        if (entry.second != nullptr) {
            start(entry.second);
        }
    }

    CPacketHelperEx<20480> packet;
    _MakeCleftStateTotal(packet);
    _BroadcastToPlayer(m_CleftTeamDB, "chb", 0xfe, 0x96, packet.GetLength(), packet.GetPacket());

    std::vector<User*> changedCats;
    _SelectRandomCat(BlueRedTeam::BLUE, changedCats);
    _SelectRandomCat(BlueRedTeam::RED, changedCats);
    SendInfosToCats(changedCats);

    if (m_Territory.GetTerritoryPointSize() > 0) {
        CTLMFilterTerritory filter(0, &m_Territory);
        FVector banishPosition{};
        if (GetRandomPos(m_BanishingPos[BlueRedTeam::BLUE], banishPosition)) {
            TLMFunc banish(banishPosition);
            FOR_ALL_NEIGHBOR_USER_IN_TERRITORY(&filter, &banish);
        }
    }
}

void CCleft::_OnReward() {
    L2_GUARD_FUNCTION();
    BlueRedTeam::Enum winner = GetCleftWinner(m_CleftPoints);
    if (IsTeam(winner) && m_CleftTeamLine[TeamIndex(winner)].empty()) {
        winner = GetOpposite(winner);
    }
    _SortByPoint();

    if (IsTeam(winner)) {
        const BlueRedTeam::Enum loser = GetOpposite(winner);
        RewardHandler winnerReward(m_WinSetting, PVPMatchGameResult::WIN, m_bIgnore15Min);
        for (CPVPMatchMemberInfo* member : m_CleftTeamLine[TeamIndex(winner)]) {
            winnerReward(member);
        }
        RewardHandler loserReward(m_LoseSetting, PVPMatchGameResult::LOSE, m_bIgnore15Min);
        for (CPVPMatchMemberInfo* member : m_CleftTeamLine[TeamIndex(loser)]) {
            loserReward(member);
        }
        _BroadcastToPlayer(m_CleftTeamDB, "chddd", 0xfe, 0x96, 3, static_cast<int>(winner), static_cast<int>(loser));
        _RemoveTeamPlayers(loser, true);
        return;
    }

    RewardHandler drawReward(m_LoseSetting, PVPMatchGameResult::DRAW, m_bIgnore15Min);
    for (TeamLine& line : m_CleftTeamLine) {
        for (CPVPMatchMemberInfo* member : line) {
            drawReward(member);
        }
    }
    _BroadcastToPlayer(m_CleftTeamDB, "chddd", 0xfe, 0x96, 3, BlueRedTeam::MAX, BlueRedTeam::MAX);
    ChangeState(PVPMatchState::REGISTER);
    _OnRegister();
}

void CCleft::_SortByPoint() {
    L2_GUARD_FUNCTION();
    for (TeamLine& line : m_CleftTeamLine) {
        std::vector<CPVPMatchMemberInfo*> sorted;
        for (CPVPMatchMemberInfo* member : line) {
            sorted.push_back(member);
        }
        std::stable_sort(sorted.begin(), sorted.end(), CPVPMatchMemberInfo::SortPlayerByPoint);
        line.clear();
        CPVPMatchMemberInfo::SetOrder setOrder;
        for (CPVPMatchMemberInfo* member : sorted) {
            line.push_back(member);
            setOrder(member);
        }

        const int teamSize = static_cast<int>(line.size());
        for (CPVPMatchMemberInfo* base : line) {
            auto* const member = static_cast<CCleftMemberInfo*>(base);
            if (member->m_nOrder == 1) {
                member->m_nRank = 1;
                continue;
            }
            const int percent = teamSize > 0
                ? std::clamp(
                      static_cast<int>((static_cast<double>(member->m_nOrder) / static_cast<double>(teamSize)) * 100.0), 0,
                      100
                  )
                : 100;
            member->m_nRank = kRankByPercent[static_cast<std::size_t>(percent)];
        }
    }
}

void CCleft::BalanceTeam() {
    {
        L2_GUARD(m_Lock);
        _BalanceTeam();
    }
}

void CCleft::_BalanceTeam() {
    L2_GUARD_FUNCTION();
    const std::size_t blueSize = m_BlueTeam->size();
    const std::size_t redSize = m_RedTeam->size();
    if (blueSize == redSize) {
        return;
    }

    TeamLine* const larger = blueSize > redSize ? m_BlueTeam : m_RedTeam;
    TeamLine* const smaller = blueSize > redSize ? m_RedTeam : m_BlueTeam;
    const BlueRedTeam::Enum smallerTeam = blueSize > redSize ? BlueRedTeam::RED : BlueRedTeam::BLUE;
    int movingCount =
        GetMovingTeamCount(m_MinRegisterCount, static_cast<int>(larger->size()), static_cast<int>(smaller->size()));
    while (movingCount-- > 0 && !larger->empty()) {
        CPVPMatchMemberInfo* const member = larger->front();
        if (member == nullptr) {
            break;
        }
        User* const user = FindUser(member->m_nUserId);
        if (user == nullptr || !_AddCleftPlayer(smallerTeam, *user)) {
            break;
        }
    }
}

int CCleft::AddPoint(const BlueRedTeam::Enum team, const int point) {
    L2_GUARD_FUNCTION();
    if (!IsTeam(team)) {
        return 0;
    }
    CleftPoint& cleftPoint = m_CleftPoints[TeamIndex(team)];
    cleftPoint.m_Point = std::max(0, cleftPoint.m_Point + point);
    return cleftPoint.m_Point;
}

void CCleft::GetPoint(const BlueRedTeam::Enum team, CleftPoint& point) const {
    if (IsTeam(team)) {
        point = m_CleftPoints[TeamIndex(team)];
    } else {
        point.Init();
    }
}

int CCleft::GetPoint(const BlueRedTeam::Enum team) const {
    return IsTeam(team) ? m_CleftPoints[TeamIndex(team)].m_Point : 0;
}

bool CCleft::SetCleftWinner(const BlueRedTeam::Enum) {
    L2_GUARD_FUNCTION();
    if (m_State != PVPMatchState::COMBAT) {
        return false;
    }
    ChangeState(PVPMatchState::REWARD);
    OnReward();
    return true;
}

void CCleft::SetTimeProperty(const int combatTime, const int rewardTime, const int catPeriod) {
    m_CombatTime = combatTime;
    m_RewardTime = rewardTime;
    m_CatPeriod = catPeriod;
}

void CCleft::SetMinRegisterCount(const int count) { m_MinRegisterCount = count; }

void CCleft::SetTeamBalance(const bool enabled) { m_TeamBalance = enabled; }

void CCleft::SetWinnerReward(const int itemClassId, const std::int64_t itemCount) {
    m_WinSetting.m_RewardItemClassId = itemClassId;
    m_WinSetting.m_RewardItemCount = itemCount;
}

void CCleft::SetLoserReward(const int itemClassId, const std::int64_t itemCount) {
    m_LoseSetting.m_RewardItemClassId = itemClassId;
    m_LoseSetting.m_RewardItemCount = itemCount;
}

void CCleft::SetWaitingSkill(CSkillInfo* const skillInfo) { m_pWaitingSkill = skillInfo; }

void CCleft::SetSpBonus(const int winnerBonus, const int loserBonus, const int killBonus) {
    m_WinSetting.m_SpBonus = winnerBonus;
    m_LoseSetting.m_SpBonus = loserBonus;
    m_WinSetting.m_SpKillBonus = killBonus;
    m_LoseSetting.m_SpKillBonus = killBonus;
}

BlueRedTeam::Enum CCleft::GetCleftWinner(const CleftPoint* const points) {
    L2_GUARD_FUNCTION();
    if (points[0].m_Point != points[1].m_Point) {
        return points[0].m_Point > points[1].m_Point ? BlueRedTeam::BLUE : BlueRedTeam::RED;
    }
    if (points[0].m_CenterTowerPoint != points[1].m_CenterTowerPoint) {
        return points[0].m_CenterTowerPoint > points[1].m_CenterTowerPoint ? BlueRedTeam::BLUE : BlueRedTeam::RED;
    }
    if (points[0].m_DefenceTowerPoint != points[1].m_DefenceTowerPoint) {
        return points[0].m_DefenceTowerPoint > points[1].m_DefenceTowerPoint ? BlueRedTeam::BLUE : BlueRedTeam::RED;
    }
    if (points[0].m_CatKillPoint != points[1].m_CatKillPoint) {
        return points[0].m_CatKillPoint > points[1].m_CatKillPoint ? BlueRedTeam::BLUE : BlueRedTeam::RED;
    }
    return BlueRedTeam::MAX;
}

bool CCleft::IsCat(const int userId) const {
    L2_GUARD_FUNCTION();
    return m_CatData[BlueRedTeam::BLUE].m_UserId == userId || m_CatData[BlueRedTeam::RED].m_UserId == userId;
}

bool CCleft::SelectRandomCat(const BlueRedTeam::Enum team) {
    L2_GUARD_FUNCTION();
    std::vector<User*> users;
    bool result;
    {
        L2_GUARD(m_Lock);
        result = _SelectRandomCat(team, users);
    }
    SendInfosToCats(users);
    return result;
}

void CCleft::SendInfosToCats(std::vector<User*>& users) {
    L2_GUARD_FUNCTION();
    for (User* const user : users) {
        if (user != nullptr) {
            user->ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_CLEFT});
            user->SendRelationChanged(nullptr);
        }
    }
}

bool CCleft::_SelectRandomCat(const BlueRedTeam::Enum team, std::vector<User*>& users) {
    L2_GUARD_FUNCTION();
    if (!IsTeam(team)) {
        return false;
    }
    CatData::UserIds& queue = m_CatData[TeamIndex(team)].m_NextCat;
    if (queue.empty()) {
        _ResetNextCat(team);
    }
    while (!queue.empty()) {
        const int userId = queue.front();
        queue.pop_front();
        if (_SelectCat(team, userId, users)) {
            return true;
        }
    }
    return false;
}

bool CCleft::_SelectCat(const BlueRedTeam::Enum team, const int userId, std::vector<User*>& users) {
    L2_GUARD_FUNCTION();
    CatData& cat = m_CatData[TeamIndex(team)];
    CObject* const previousObject = CObject::GetObjectW(static_cast<std::uint32_t>(cat.m_UserId));
    User* const previous = previousObject != nullptr && previousObject->IsPC() ? previousObject->CastUser() : nullptr;
    if (previous != nullptr) {
        users.push_back(previous);
    }
    cat.SetNewCat(userId);
    CObject* const selectedObject = CObject::GetObjectW(static_cast<std::uint32_t>(userId));
    User* const selected = selectedObject != nullptr && selectedObject->IsPC() ? selectedObject->CastUser() : nullptr;
    if (selected == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"No Cat! [%s][%d]",
        //     L"Cleft.cpp",
        //     0x6f8)
        // L2_LOG_ERROR(Log,
        //     L"No Cat! [%s][%d]",
        //     L"Cleft.cpp",
        //     0x6f8)
        L2_LOG_ERROR(
            Log,
            "Cleft team %d: selected player object %u is unavailable; selection state was already updated, but notification "
            "is skipped.",
            static_cast<int>(team), static_cast<unsigned>(userId)
        );
        return false;
    }
    users.push_back(selected);
    _BroadcastToPlayer(
        m_CleftTeamDB, "chddddS", 0xfe, 0x96, 2, GetCombatRemainSec(), static_cast<int>(team), userId, selected->GetName()
    );
    return true;
}

void CCleft::_ResetNextCat(const BlueRedTeam::Enum team) {
    L2_GUARD_FUNCTION();
    if (!IsTeam(team)) {
        return;
    }
    CatData::UserIds& queue = m_CatData[TeamIndex(team)].m_NextCat;
    queue.clear();
    std::vector<int> userIds;
    for (CPVPMatchMemberInfo* const member : m_CleftTeamLine[TeamIndex(team)]) {
        if (member != nullptr) {
            userIds.push_back(static_cast<int>(member->m_nUserId));
        }
    }
    for (std::size_t i = userIds.size(); i > 1; --i) {
        std::size_t selected = static_cast<std::size_t>(genrand64_real1() * static_cast<double>(i));
        if (selected >= i) {
            selected = i - 1;
        }
        std::swap(runtime::CheckedVectorAt(userIds, i - 1), runtime::CheckedVectorAt(userIds, selected));
    }
    for (const int userId : userIds) {
        queue.push_back(userId);
    }
}

void CCleft::_MakeCleftListTotal(CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();
    packet.Assemble("ddd", m_State == PVPMatchState::COMBAT ? 4 : 0, m_MinRegisterCount, m_TeamBalance ? 1 : 0);
    packet.Assemble("d", static_cast<int>(m_BlueTeam->size()));
    for (CPVPMatchMemberInfo* const member : *m_BlueTeam) {
        if (member != nullptr) {
            member->MakeListInfo(packet);
        }
    }
    packet.Assemble("d", static_cast<int>(m_RedTeam->size()));
    for (CPVPMatchMemberInfo* const member : *m_RedTeam) {
        if (member != nullptr) {
            member->MakeListInfo(packet);
        }
    }
}

void CCleft::_MakeCleftStateTotal(CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();
    User* const blueCat = FindUser(static_cast<std::uint32_t>(m_CatData[BlueRedTeam::BLUE].m_UserId));
    User* const redCat = FindUser(static_cast<std::uint32_t>(m_CatData[BlueRedTeam::RED].m_UserId));
    packet.Assemble(
        "ddddddSS", 0, GetCombatRemainSec(), m_CleftPoints[BlueRedTeam::BLUE].m_Point,
        m_CleftPoints[BlueRedTeam::RED].m_Point, m_CatData[BlueRedTeam::BLUE].m_UserId, m_CatData[BlueRedTeam::RED].m_UserId,
        blueCat != nullptr ? blueCat->GetName() : u"", redCat != nullptr ? redCat->GetName() : u""
    );
    packet.Assemble("d", static_cast<int>(m_BlueTeam->size()));
    for (CPVPMatchMemberInfo* const member : *m_BlueTeam) {
        if (member != nullptr) {
            member->MakeStateInfo(packet);
        }
    }
    packet.Assemble("d", static_cast<int>(m_RedTeam->size()));
    for (CPVPMatchMemberInfo* const member : *m_RedTeam) {
        if (member != nullptr) {
            member->MakeStateInfo(packet);
        }
    }
}

void CCleft::TowerDestroyed(User& user, const int towerType, const int point) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        const auto found = m_CleftTeamDB.find(user.Id());
        if (found != m_CleftTeamDB.end() && found->second != nullptr) {
            auto* const member = static_cast<CCleftMemberInfo*>(found->second);
            if (IsTeam(member->m_Team)) {
                CleftPoint& teamPoint = m_CleftPoints[TeamIndex(member->m_Team)];
                AddPoint(member->m_Team, point);
                if (towerType == 0) {
                    ++teamPoint.m_CenterTowerPoint;
                } else {
                    ++teamPoint.m_DefenceTowerPoint;
                }
                ++member->m_CleftTowerCount;
                CPacketHelperEx<20480> state;
                member->MakeStateInfo(state);
                _BroadcastToPlayer(
                    m_CleftTeamDB, "chddddddb", 0xfe, 0x96, 1, GetCombatRemainSec(),
                    m_CleftPoints[BlueRedTeam::BLUE].m_Point, m_CleftPoints[BlueRedTeam::RED].m_Point,
                    static_cast<int>(member->m_Team), towerType, state.GetLength(), state.GetPacket()
                );
            }
        }
    }
}

void CCleft::UserDie(User* const user, User* const enemy) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    std::vector<User*> victimCatChanges;
    std::vector<User*> killerCatChanges;
    std::optional<CPacketHelperEx<20480>> victimStateStorage;
    std::optional<CPacketHelperEx<20480>> killerStateStorage;
    enum class CatNotification { None, Victim, VictimAndKiller };
    const CatNotification notification = [&]() {
        L2_GUARD(m_Lock);
        const auto victimIt = m_CleftTeamDB.find(user->Id());
        if (victimIt == m_CleftTeamDB.end() || victimIt->second == nullptr) {
            return CatNotification::None;
        }
        auto* const victim = static_cast<CCleftMemberInfo*>(victimIt->second);
        victim->m_DieTime = runtime::GetTickCount();
        const BlueRedTeam::Enum victimTeam = victim->m_Team;
        if (!IsTeam(victimTeam)) {
            return CatNotification::None;
        }

        if (enemy == nullptr) {
            if (m_CatData[TeamIndex(victimTeam)].m_UserId == static_cast<int>(user->Id())) {
                _SelectRandomCat(victimTeam, victimCatChanges);
            }
            return CatNotification::Victim;
        }

        const auto killerIt = m_CleftTeamDB.find(enemy->Id());
        if (killerIt == m_CleftTeamDB.end() || killerIt->second == nullptr) {
            return CatNotification::None;
        }
        auto* const killer = static_cast<CCleftMemberInfo*>(killerIt->second);
        const BlueRedTeam::Enum killerTeam = killer->m_Team;
        if (!IsTeam(killerTeam) || killerTeam == victimTeam) {
            return CatNotification::None;
        }

        int killPoint = 1;
        if (m_CatData[TeamIndex(victimTeam)].m_UserId == static_cast<int>(user->Id())) {
            killPoint = 10;
            _SelectRandomCat(victimTeam, killerCatChanges);
        }
        AddPoint(killerTeam, killPoint);
        ++victim->m_nDeathCount;
        ++killer->m_nKillCount;

        auto& victimState = victimStateStorage.emplace();
        auto& killerState = killerStateStorage.emplace();
        victim->MakeStateInfo(victimState);
        killer->MakeStateInfo(killerState);
        _BroadcastToPlayer(
            m_CleftTeamDB, "chdddddbdb", 0xfe, 0x96, 4, GetCombatRemainSec(), m_CleftPoints[BlueRedTeam::BLUE].m_Point,
            m_CleftPoints[BlueRedTeam::RED].m_Point, static_cast<int>(victimTeam), victimState.GetLength(),
            victimState.GetPacket(), static_cast<int>(killerTeam), killerState.GetLength(), killerState.GetPacket()
        );
        return CatNotification::VictimAndKiller;
    }();
    if (notification != CatNotification::None) {
        SendInfosToCats(victimCatChanges);
    }
    if (notification == CatNotification::VictimAndKiller) {
        SendInfosToCats(killerCatChanges);
    }
}

void CCleft::CheckDieUser(const std::uint32_t tick) {
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_CleftTeamDB) {
        auto* const member = static_cast<CCleftMemberInfo*>(entry.second);
        if (member == nullptr || member->m_DieTime == 0) {
            continue;
        }
        User* const user = FindUser(member->m_nUserId);
        if (user == nullptr) {
            continue;
        }
        const std::uint32_t elapsed = tick - member->m_DieTime;
        if (elapsed < 10000U) {
            if (CUserSocket* const socket = user->GetSocket()) {
                socket->Send(
                    "chcch", 0x62, SystemMessageId::WAITING_ROOM_RESURRECTION_IN_SECONDS, 1, 0x15,
                    10 - static_cast<int>(elapsed / 1000U)
                );
            }
            continue;
        }
        user->ResurrectBy(user, 100);
        member->m_DieTime = 0;
        ReadyPlayerToStart(member->m_Team, *user);
    }
}

void CCleft::TimerExpired(const int id) {
    L2_GUARD_FUNCTION();
    CPVPMatch::TimerExpired(id);
    const PVPMatchState::Enum state = m_State;
    const std::uint32_t tick = runtime::GetTickCount();
    if (m_TimerTick % 5 != 0) {
        CheckLogoutPlayers();
    }

    if (state == PVPMatchState::REGISTER) {
        std::optional<std::vector<std::uint32_t>> tooFarStorage;
        {
            L2_GUARD(m_Lock);
            auto& tooFar = tooFarStorage.emplace();
            for (const auto& entry : m_CleftTeamDB) {
                User* const user = FindUser(entry.first);
                if (user != nullptr && !CheckDistanceWithNpc(*user)) {
                    tooFar.push_back(entry.first);
                }
            }
            for (const std::uint32_t userId : tooFar) {
                User* const user = FindUser(userId);
                _RemoveCleftPlayer(static_cast<int>(userId));
                if (user != nullptr && user->GetSocket() != nullptr) {
                    user->GetSocket()->Send("chd", 0xfe, 0x95, -1);
                }
            }
        }
    } else if (state == PVPMatchState::COMBAT) {
        const int remain = GetCombatRemainSec();
        if (remain < 1) {
            ChangeState(PVPMatchState::REWARD);
            OnReward();
        } else {
            for (int team = 0; team < 2; ++team) {
                CatData& cat = m_CatData[team];
                if (cat.m_CatSelectedTick + static_cast<std::uint32_t>(m_CatPeriod * 60000) < tick) {
                    SelectRandomCat(static_cast<BlueRedTeam::Enum>(team));
                }
            }
            CheckDieUser(tick);
            if (remain < m_OldCombatRemainSec) {
                m_OldCombatRemainSec = remain;
            }
        }
    } else if (state == PVPMatchState::REWARD) {
        if (static_cast<std::uint32_t>(m_CurrentStateStartTime) + static_cast<std::uint32_t>(m_RewardTime * 60000) <= tick) {
            ChangeState(PVPMatchState::REGISTER);
            OnRegister();
        }
        CheckDieUser(tick);
    }
    AddTimer(1000, 0);
}

void CCleft::ShowCleftInfo(User& user) {
    L2_GUARD_FUNCTION();
    const std::u16string state = GetStateString(m_State);
    const char16_t* const stateText = state.c_str();
    std::wstring hostState;
    for (const char16_t* cursor = stateText; *cursor != 0; ++cursor) {
        hostState.push_back(static_cast<wchar_t>(*cursor));
    }
    char16_t message[1024]{};
    FormatUtf16To(
        message, std::size(message),
        u"Cleft info \nstatus : %ls \nteam_balance : %ls "
        u"\ntime_set : \ncombat[%d], reward[%d], cat[%d) min"
        u"\nstate elapsed time = %d sec\nnow, min user = [%zu, %d]"
        u"\nignore 15 min = %ls\n",
        hostState.c_str(), m_TeamBalance ? L"true" : L"false", m_CombatTime, m_RewardTime, m_CatPeriod,
        GetCombatElapsedSec(), m_CleftTeamDB.size(), m_MinRegisterCount * 2, m_bIgnore15Min ? L"true" : L"false"
    );
    if (CUserSocket* const socket = user.GetSocket()) {
        socket->SendSystemMessage(u"SYS", message);
    }
}

void CCleft::ShowCleftState(User& user) {
    L2_GUARD_FUNCTION();
    CObject* const blueObject = CObject::GetObjectW(static_cast<std::uint32_t>(m_CatData[BlueRedTeam::BLUE].m_UserId));
    User* const blueCat = blueObject != nullptr && blueObject->IsPC() ? blueObject->CastUser() : nullptr;
    CObject* const redObject = CObject::GetObjectW(static_cast<std::uint32_t>(m_CatData[BlueRedTeam::RED].m_UserId));
    User* const redCat = redObject != nullptr && redObject->IsPC() ? redObject->CastUser() : nullptr;
    char16_t message[1024]{};
    FormatUtf16To(
        message, std::size(message),
        u"Cleft state\npoint = [%d/%d]\nRed Cat : %ls"
        u"\nBlue Cat : %ls\nRed Team num : [%zu]\n",
        m_CleftPoints[BlueRedTeam::BLUE].m_Point, m_CleftPoints[BlueRedTeam::RED].m_Point,
        redCat != nullptr ? Utf16ToWide(redCat->GetName()).c_str() : L"not exist",
        blueCat != nullptr ? Utf16ToWide(blueCat->GetName()).c_str() : L"not exist", m_RedTeam->size()
    );
    if (CUserSocket* const socket = user.GetSocket()) {
        socket->SendSystemMessage(u"SYS", message);
        FormatUtf16To(message, std::size(message), u"Blue Team num : [%zu]\n", m_BlueTeam->size());
        socket->SendSystemMessage(u"SYS", message);
    }
}

std::u16string CCleft::GetStateString(const PVPMatchState::Enum state) {
    switch (state) {
        case PVPMatchState::REGISTER:
            return std::u16string(u"register");
        case PVPMatchState::COMBAT:
            return std::u16string(u"Combat");
        case PVPMatchState::REWARD:
            return std::u16string(u"reward");
        default:
            return std::u16string(u"unknown");
    }
}

int CCleft::GetRelation(User& user, User& target) {
    L2_GUARD_FUNCTION();
    if (!user.IsInSamePVPMatch(target, PVPMatchType::CLEFT, PVPMatchPlayerState::COMBAT)) {
        return 0;
    }
    int relation;
    {
        L2_GUARD(m_Lock);
        relation = _GetRelation(m_CleftTeamDB, user, target);
    }
    return relation;
}

bool CCleft::LeaveWorld(User& user) {
    L2_GUARD_FUNCTION();
    if (!user.IsInPVPMatch(PVPMatchType::CLEFT, PVPMatchPlayerState::NONE)) {
        return false;
    }
    return RemoveCleftPlayer(static_cast<int>(user.Id()));
}

wchar_t* CCleft::GetName() const { return const_cast<wchar_t*>(L"Cleft"); }
