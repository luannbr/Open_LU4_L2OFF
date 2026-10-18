#include "BlockUpset.h"
#include "CreatureController.h"
#include "CursedWeapon.h"
#include "NPC.h"
#include "NpcServer.h"
#include "Object.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/TextRuntime.h"
#include "Skill.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/platform/Runtime.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwchar>

namespace {


char16_t str[0x400]{};

User* FindUserByObjectId(std::uint32_t userId) {
    CObject* const object = CObject::GetObjectW(userId);
    return object != nullptr && object->IsPC() ? object->CastUser() : nullptr;
}

bool IsTeam(BlueRedTeam::Enum team) { return team == BlueRedTeam::BLUE || team == BlueRedTeam::RED; }

std::size_t TeamIndex(BlueRedTeam::Enum team) { return static_cast<std::size_t>(team); }

void SetUserMatchState(User& user, BlueRedTeam::Enum team, PVPMatchPlayerState::Enum state) {
    user.m_data->m_PVPMatchType = PVPMatchType::BLOCK_UPSET;
    user.m_data->m_PVPMatchPlayerState = state;
    user.m_data->m_cPVPMatchTeamID = CPVPMatch::GetMatchTeamID(team);
}

struct SendBlockUpsetMemberState {
    explicit SendBlockUpsetMemberState(CUserSocket* const socket) : m_socket(socket), m_message{} {}

    void operator()(CPVPMatchMemberInfo* const memberInfo) {
        // Current L2Server 0x004b76d0.
        L2_GUARD_FUNCTION();
        User* const user = FindUserByObjectId(memberInfo->m_nUserId);
        if (user == nullptr) {
            return;
        }

        FormatUtf16To(
            m_message.data(), m_message.size(), u"%s [%d]\n", user->GetName(),
            static_cast<CBlockUpsetMemberInfo*>(memberInfo)->m_Point
        );
        m_socket->SendSystemMessage(u"SYS", m_message.data());
    }

    CUserSocket* m_socket;
    std::array<char16_t, 0x400> m_message;
};

struct ResetVote {
    void operator()(CPVPMatch::UserDB::value_type& entry) const {
        static_cast<CBlockUpsetMemberInfo*>(entry.second)->m_bVoted = false;
    }
};

} // namespace

// Portable registration: the runtime ID is no longer a shipping number.
// static_assert(
//     L2ServerMemory::kCBlockUpsetMemberInfoClassId ==
//     CBlockUpsetMemberInfo::kMemoryClassId)

CBlockUpsetMemberInfo::CBlockUpsetMemberInfo(
    CBlockUpset& blockUpset,
    CBlockUpsetManager& manager,
    BlueRedTeam::Enum team,
    std::uint32_t userId,
    const char16_t* userName
) :
    CPVPMatchMemberInfo(team, userId, userName),
    m_BlockUpset(blockUpset),
    m_BlockUpsetManager(manager),
    m_Point(0),
    m_bVoted(false) {}

CBlockUpsetMemberInfo::~CBlockUpsetMemberInfo() = default;

L2_DEFINE_OBJECT_CLASS_ID(CBlockUpsetMemberInfo)

L2_DEFINE_OBJECT_CLASS_NAME_LITERAL(CBlockUpsetMemberInfo)

L2_DEFINE_OBJECT_ALLOCATOR_LITERAL(CBlockUpsetMemberInfo)

L2_DEFINE_OBJECT_NEW(CBlockUpsetMemberInfo)

L2_DEFINE_OBJECT_ADD_REF(CBlockUpsetMemberInfo)

L2_DEFINE_OBJECT_RELEASE(CBlockUpsetMemberInfo)

int CBlockUpsetMemberInfo::GetPoint() const { return m_Point; }

CBlockUpset::BlockUpsetPoint::BlockUpsetPoint() : m_Point(0) {}

void CBlockUpset::BlockUpsetPoint::Init() { m_Point = 0; }

void CBlockUpset::ReleaseBlockUpsetInfo::operator()(CPVPMatchMemberInfo* memberInfo) {
    Do(*static_cast<CBlockUpsetMemberInfo*>(memberInfo));
}

void CBlockUpset::ReleaseBlockUpsetInfo::Do(CBlockUpsetMemberInfo& memberInfo) {
    L2_GUARD_FUNCTION();
    CObject* const object = CObject::GetObjectW(memberInfo.m_nUserId);
    User* user = nullptr;
    if (object != nullptr && object->IsPC()) {
        user = object->CastUser();
    }
    if (user != nullptr) {
        user->m_data->m_PVPMatchType = PVPMatchType::NONE;
        user->m_data->m_PVPMatchPlayerState = PVPMatchPlayerState::NONE;
        user->m_data->m_cPVPMatchTeamID = 0;
    }
    L2_RELEASE(&(memberInfo), ObjectRefType::CreateDelete);
}

void CBlockUpset::BanishCombatBlockUpsetUser::operator()(CPVPMatchMemberInfo* memberInfo) {
    Do(*static_cast<CBlockUpsetMemberInfo*>(memberInfo));
}

void CBlockUpset::BanishCombatBlockUpsetUser::Do(CBlockUpsetMemberInfo& memberInfo) {
    L2_GUARD_FUNCTION();
    CObject* const object = CObject::GetObjectW(memberInfo.m_nUserId);
    User* const user = object != nullptr && object->IsPC() ? object->CastUser() : nullptr;
    if (user == nullptr) {
        return;
    }
    for (const int itemClassId : memberInfo.m_BlockUpsetManager.m_DeleteItemsAfterMatch) {
        user->DeleteItemInInventory(itemClassId);
    }
    user->DispelByIdLevelMySelf(memberInfo.m_BlockUpsetManager.m_pEnterSkill[TeamIndex(memberInfo.m_Team)], true);

    FVector position{};
    if (!CPVPMatch::GetRandomPos(memberInfo.m_BlockUpset.m_BanishingPos[TeamIndex(memberInfo.m_Team)], position)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"banising Pos data error. team = %d",
        //     static_cast<int>(memberInfo.m_Team))
        // L2_LOG_ERROR(Log,
        //     L"banising Pos data error. team = %d",
        //     static_cast<int>(memberInfo.m_Team))
        L2_LOG_ERROR(
            Log,
            "Block Upset cannot choose an exit position for player object %u on team %d; teleport skipped after item and "
            "skill cleanup.",
            static_cast<unsigned>(memberInfo.m_nUserId), static_cast<int>(memberInfo.m_Team)
        );
        return;
    }
    user->DoNothing();
    user->TeleportToLocation(
        static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z),
        static_cast<std::uint32_t>(user->GetInZoneID()), 0x10000, TeleportSourceType::BlockUpsetPCBanish, nullptr, 0
    );
}

void CBlockUpset::OnCombatStart::operator()(CPVPMatchMemberInfo* memberInfo) {
    Do(*static_cast<CBlockUpsetMemberInfo*>(memberInfo));
}

void CBlockUpset::OnCombatStart::Do(CBlockUpsetMemberInfo& memberInfo) {
    L2_GUARD_FUNCTION();
    User* const user = FindUserByObjectId(memberInfo.m_nUserId);
    if (user == nullptr) {
        return;
    }
    user->DespawnAllSummon(true, false);
    user->DespawnPet(true, true, DESPAWN_PET_NONE, false);
    memberInfo.m_Point = 0;
    memberInfo.m_bVoted = false;
    memberInfo.m_BlockUpset.ReadyPlayerToStart(memberInfo.m_Team, *user);
    user->GetSocket()->Send("chd", 0xfe, 0x98, -1);
    SetUserMatchState(*user, memberInfo.m_Team, PVPMatchPlayerState::COMBAT);
    CLogFormatter formatter;
    formatter.WriteLog2(
        0x1f5d, user, nullptr, memberInfo.m_BlockUpset.m_Index, static_cast<int>(memberInfo.m_Team), 0, 0, 0, 0, 0
    );
}

CBlockUpset::CBlockUpset() :
    m_BlockUpsetPoints{},
    m_BlockUpsetWaitingLine{},
    m_BlueWaitingTeam(&m_BlockUpsetWaitingLine[BlueRedTeam::BLUE]),
    m_RedWaitingTeam(&m_BlockUpsetWaitingLine[BlueRedTeam::RED]),
    m_BlockUpsetCombatLine{},
    m_BlueCombatTeam(&m_BlockUpsetCombatLine[BlueRedTeam::BLUE]),
    m_RedCombatTeam(&m_BlockUpsetCombatLine[BlueRedTeam::RED]),
    m_bOnBalancing(false) {
    InitSetting();
}

CBlockUpset::~CBlockUpset() = default;

PVPMatchType::Enum CBlockUpset::GetPVPMatchType() const { return PVPMatchType::BLOCK_UPSET; }

void CBlockUpset::InitBlockUpset(int index, CBlockUpsetManager& manager) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x004baef0. The leading 0x006fe5d4
    // target call is an observable three-byte no-op.
    m_Index = index;
    m_pManager = &manager;
    m_WaitingNpcBlock.clear();
    m_CombatNpcBlock.clear();
    {
        L2_GUARD(m_Lock);
        _RemoveAllBlockUpsetWaitingPlayers();
        _RemoveAllBlockUpsetCombatPlayers();
    }
}

void CBlockUpset::InitSetting() {
    L2_GUARD_FUNCTION();
    CPVPMatch::InitSetting();
    m_MinRegisterCount = 2;
    m_MaxRegisterCount = 6;
    m_CombatStartTick = -1;
    m_bOnBalancing = false;
    m_VoteTryTime = 0;
    ResetVoteToStart();
    m_LastAddedUserId = 0;
    ChangeState(PVPMatchState::INVALID);
}

void CBlockUpset::OnNpcServerClosed() {
    L2_GUARD_FUNCTION();
    CPVPMatch::OnNpcServerClosed();
    m_WaitingNpcBlock.clear();
    m_CombatNpcBlock.clear();
}

void CBlockUpset::RemoveAllBlockUpsetWaitingPlayers() {
    {
        L2_GUARD(m_Lock);
        _RemoveAllBlockUpsetWaitingPlayers();
    }
}

void CBlockUpset::_RemoveAllBlockUpsetWaitingPlayers() {
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_BlockUpsetWaitingDB) {
        ReleaseBlockUpsetInfo()(entry.second);
    }
    m_BlockUpsetWaitingDB.clear();
    for (TeamLine& team : m_BlockUpsetWaitingLine) {
        team.clear();
    }
}

void CBlockUpset::RemoveAllBlockUpsetCombatPlayers() {
    {
        L2_GUARD(m_Lock);
        _RemoveAllBlockUpsetCombatPlayers();
    }
}

void CBlockUpset::_RemoveAllBlockUpsetCombatPlayers() {
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_BlockUpsetCombatDB) {
        auto* const member = static_cast<CBlockUpsetMemberInfo*>(entry.second);
        if (User* const user = FindUserByObjectId(member->m_nUserId)) {
            CLogFormatter formatter;
            formatter.WriteLog2(
                0x1f5e, user, nullptr, m_Index, static_cast<int>(member->m_Team), member->m_Point,
                m_BlockUpsetPoints[static_cast<std::size_t>(member->m_Team)].m_Point, 0, 0, 0
            );
        }
        BanishCombatBlockUpsetUser()(member);
        ReleaseBlockUpsetInfo()(member);
    }
    m_BlockUpsetCombatDB.clear();
    for (TeamLine& team : m_BlockUpsetCombatLine) {
        team.clear();
    }
}

void CBlockUpset::CheckLogoutPlayers(UserDB& users) {
    {
        L2_GUARD(m_Lock);
        _CheckLogoutPlayers(users);
    }
}

void CBlockUpset::_CheckLogoutPlayers(UserDB& users) {
    L2_GUARD_FUNCTION();
    for (const auto& entry : users) {
        const std::uint32_t userId = entry.second->m_nUserId;
        if (FindUserByObjectId(userId) == nullptr) {
            if (&users == &m_BlockUpsetWaitingDB) {
                _RemoveBlockUpsetWaitingPlayer(static_cast<int>(userId));
            } else if (&users == &m_BlockUpsetCombatDB) {
                _RemoveBlockUpsetCombatPlayer(static_cast<int>(userId));
            }
        }
    }
}

void CBlockUpset::RegisterManagerNpcId(int npcId) {
    L2_GUARD_FUNCTION();
    CPVPMatch::RegisterManagerNpcId(npcId);
    _CheckStartBlockUpsetCombat(m_pManager->m_nVoteWaitSec);
}

void CBlockUpset::ShowBlockUpsetInfo(User& user) {
    L2_GUARD_FUNCTION();
    FormatUtf16To(
        str, 0x3ff,
        u"BlockStage [%d] info \nMin/Max member = %d/%d\n"
        u"state [%d]\nCombat Player B/R[%d/%d]\n"
        u"Wait Player B/R[%d/%d]\nWaiting npcs[%d]\n"
        u"Combat npcs[%d]\n",
        m_Index, m_MinRegisterCount, m_MaxRegisterCount, static_cast<int>(m_State),
        runtime::Int32FromBits(static_cast<std::uint32_t>(m_BlueCombatTeam->size())),
        runtime::Int32FromBits(static_cast<std::uint32_t>(m_RedCombatTeam->size())),
        runtime::Int32FromBits(static_cast<std::uint32_t>(m_BlueWaitingTeam->size())),
        runtime::Int32FromBits(static_cast<std::uint32_t>(m_RedWaitingTeam->size())),
        runtime::Int32FromBits(static_cast<std::uint32_t>(m_WaitingNpcBlock.size())),
        runtime::Int32FromBits(static_cast<std::uint32_t>(m_CombatNpcBlock.size()))
    );
    user.GetSocket()->SendSystemMessage(u"SYS", str);
}

void CBlockUpset::ShowBlockUpsetState(User& user) {
    // Current L2Server 0x004b918c.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);

        CUserSocket* const socket = user.GetSocket();
        std::array<char16_t, 0x400> message{};
        FormatUtf16To(
            message.data(), message.size(),
            u"BlockUpset state\nstate [%d]\nremain sec [%d]\n"
            u"point = [%d/%d]\nCombat State\nRed Team num : [%d]\n",
            static_cast<int>(m_State), GetCombatFinishRemainSec(), m_BlockUpsetPoints[BlueRedTeam::RED].m_Point,
            m_BlockUpsetPoints[BlueRedTeam::BLUE].m_Point, static_cast<int>(m_RedCombatTeam->size())
        );
        socket->SendSystemMessage(u"SYS", message.data());

        std::for_each(m_RedCombatTeam->begin(), m_RedCombatTeam->end(), SendBlockUpsetMemberState(socket));

        FormatUtf16To(message.data(), message.size(), u"Blue Team num : [%d]\n", static_cast<int>(m_BlueCombatTeam->size()));
        socket->SendSystemMessage(u"SYS", message.data());
        std::for_each(m_BlueCombatTeam->begin(), m_BlueCombatTeam->end(), SendBlockUpsetMemberState(socket));

        FormatUtf16To(
            message.data(), message.size(), u"Waiting State\nRed Team num : [%d]\n",
            static_cast<int>(m_RedWaitingTeam->size())
        );
        socket->SendSystemMessage(u"SYS", message.data());
        std::for_each(m_RedWaitingTeam->begin(), m_RedWaitingTeam->end(), SendBlockUpsetMemberState(socket));

        FormatUtf16To(
            message.data(), message.size(), u"Blue Team num : [%d]\n",
            // Current 004b93e2 reads the combat-team count here.
            static_cast<int>(m_BlueCombatTeam->size())
        );
        socket->SendSystemMessage(u"SYS", message.data());
        std::for_each(m_BlueWaitingTeam->begin(), m_BlueWaitingTeam->end(), SendBlockUpsetMemberState(socket));
    }
}

void CBlockUpset::AddBlockUpsetNpc(int npcId) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        _AddBlockUpsetNpc(npcId);
    }
}

void CBlockUpset::_AddBlockUpsetNpc(int npcId) {
    L2_GUARD_FUNCTION();
    CObject* const object = CObject::GetObjectW(static_cast<std::uint32_t>(npcId));
    if (object == nullptr || !object->IsNPC()) {
        return;
    }
    if (CNPC* const npc = object->CastNPC()) {
        npc->SetVisible(false);
        m_WaitingNpcBlock.push_back(npcId);
    }
}

bool CBlockUpset::_RemoveBlockUpsetNpcFromGround(int amount) {
    L2_GUARD_FUNCTION();
    if (amount < 1) {
        return true;
    }
    for (int i = 0; i < amount; ++i) {
        if (m_CombatNpcBlock.empty()) {
            return false;
        }
        const int npcId = *m_CombatNpcBlock.begin();
        CObject* const object = CObject::GetObjectW(static_cast<std::uint32_t>(npcId));
        CNPC* npc = nullptr;
        if (object != nullptr && object->IsNPC()) {
            npc = object->CastNPC();
        }
        m_CombatNpcBlock.erase(m_CombatNpcBlock.begin());
        if (npc != nullptr) {
            npc->SetVisible(false);
            OnTeamScoreChanged(2, npc->m_data->m_EvolutionId);
            m_WaitingNpcBlock.push_back(npcId);
        }
    }
    return true;
}

bool CBlockUpset::_InsertBlockUpsetNpcToGround(int amount) {
    L2_GUARD_FUNCTION();
    if (amount < 1) {
        return true;
    }
    for (int i = 0; i < amount; ++i) {
        if (m_WaitingNpcBlock.empty()) {
            return false;
        }
        const int npcId = m_WaitingNpcBlock.back();
        m_WaitingNpcBlock.pop_back();
        CObject* const object = CObject::GetObjectW(static_cast<std::uint32_t>(npcId));
        if (object != nullptr && object->IsNPC()) {
            if (CNPC* const npc = object->CastNPC()) {
                const FVector position = m_Territory.PickRandomPos(false);
                npc->StopMove();
                npc->TeleportToLocation(
                    static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z),
                    npc->GetInZoneID(), 0x10000, TeleportSourceType::BlockUpsetNPC, nullptr, 0
                );
                npc->SetVisible(true);
                OnTeamScoreChanged(2, npc->m_data->m_EvolutionId);
                m_CombatNpcBlock.insert(npcId);
            }
        }
    }
    return true;
}

void CBlockUpset::GetNpcBlockCounts(int& waiting, int& combat) const {
    waiting = static_cast<int>(m_WaitingNpcBlock.size());
    combat = static_cast<int>(m_CombatNpcBlock.size());
}

void CBlockUpset::ShowWaitingNpcBlock(bool show) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        _ShowWaitingNpcBlock(show);
    }
}

void CBlockUpset::_ShowWaitingNpcBlock(bool show) {
    L2_GUARD_FUNCTION();
    for (const int npcId : m_WaitingNpcBlock) {
        CObject* const object = CObject::GetObjectW(static_cast<std::uint32_t>(npcId));
        if (object != nullptr && object->IsNPC()) {
            if (CNPC* const npc = object->CastNPC()) {
                npc->SetVisible(show);
            }
        }
    }
}

void CBlockUpset::ChangeBlockUpsetNpcAmount(int amount) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        if (m_State == PVPMatchState::COMBAT) {
            if (amount < 0) {
                _RemoveBlockUpsetNpcFromGround(runtime::NegateInt32Wrapping(amount));
            } else if (amount > 0) {
                _InsertBlockUpsetNpcToGround(amount);
            }
        }
    }
}

bool CBlockUpset::CanAddBlockUpsetWaitingPlayer(User& user) {
    L2_GUARD_FUNCTION();
    if (!CanAddPlayer(user) || m_Voting || m_VoteTryTime >= 2) {
        return false;
    }
    static_cast<void>(CCursedWeaponMgr::inst());
    if (user.m_nCursedWeaponClassId > 0 || user.m_bEinhasadeTransformed || user.m_data->m_nKarma < 0) {
        return false;
    }
    if (IsBannedUser(static_cast<int>(user.Id()))) {
        user.SendSystemMessage(SystemMessageId::CANNOT_REREGISTER_MATCH_IMMEDIATELY_AFTER_CANCELLATION);
        return false;
    }
    return m_BlockUpsetWaitingDB.size() < static_cast<std::size_t>(runtime::MultiplyInt32Wrapping(m_MaxRegisterCount, 2));
}

bool CBlockUpset::EnterBlockUpsetPlayer(User& user) {
    L2_GUARD_FUNCTION();
    if (!CheckDistanceWithNpc(user)) {
        user.SendSystemMessage(SystemMessageId::NPC_TOO_FAR);
        return false;
    }
    bool added;
    {
        L2_GUARD(m_Lock);
        added = _AddBlockUpsetWaitingPlayer(
            static_cast<BlueRedTeam::Enum>(m_RedWaitingTeam->size() <= m_BlueWaitingTeam->size()), user
        );
    }
    return added;
}

bool CBlockUpset::AddBlockUpsetWaitingPlayer(BlueRedTeam::Enum team, User& user) {
    L2_GUARD_FUNCTION();
    bool added;
    {
        L2_GUARD(m_Lock);
        added = _AddBlockUpsetWaitingPlayer(team, user);
    }
    return added;
}

bool CBlockUpset::_AddBlockUpsetWaitingPlayer(BlueRedTeam::Enum team, User& user) {
    L2_GUARD_FUNCTION();
    const std::uint32_t userId = user.Id();
    if (_IsBlockUpsetCombatPlayer(static_cast<int>(userId))) {
        user.SendSystemMessage(u"SYS", u"debug - blockupset. I'm in combat.");
        return false;
    }

    const auto found = m_BlockUpsetWaitingDB.find(userId);
    if (found == m_BlockUpsetWaitingDB.end()) {
        m_LastAddedUserId = static_cast<int>(userId);
        user.m_data->m_PVPMatchType = PVPMatchType::BLOCK_UPSET;
        user.m_data->m_PVPMatchPlayerState = PVPMatchPlayerState::WAITING;

        auto* const member = new CBlockUpsetMemberInfo(*this, *m_pManager, team, userId, user.GetName());
        m_BlockUpsetWaitingDB.insert(std::make_pair(userId, static_cast<CPVPMatchMemberInfo*>(member)));
        m_BlockUpsetWaitingLine[TeamIndex(team)].push_back(member);
        _CheckLogoutPlayers(m_BlockUpsetWaitingDB);

        CPacketHelperEx<20480> total;
        _MakeBlockUpsetListTotal(total);
        user.GetSocket()->Send("chb", 0xfe, 0x98, total.GetLength(), total.GetPacket());
        _BroadcastToPlayer(
            m_BlockUpsetWaitingDB, "chddddS", 0xfe, 0x98, 1, GetCombatStartingRemainSec(), static_cast<int>(team),
            static_cast<int>(userId), user.GetName()
        );
        _OnBlockUpsetPlayerAdded(member);
        return true;
    }

    auto* const member = static_cast<CBlockUpsetMemberInfo*>(found->second);
    if (member->m_Team == team) {
        return false;
    }
    const bool changed = _ChangeBlockUpsetWaitingPlayerTeam(member);
    if (!changed) {
        user.SendSystemMessage(u"SYS", u"debug - blockupset. Change Failed");
    }
    return changed;
}

bool CBlockUpset::RemoveBlockUpsetWaitingPlayer(int userId) {
    L2_GUARD_FUNCTION();
    bool removed;
    {
        L2_GUARD(m_Lock);
        removed = _RemoveBlockUpsetWaitingPlayer(userId);
    }
    return removed;
}

bool CBlockUpset::_RemoveBlockUpsetWaitingPlayer(int userId) {
    L2_GUARD_FUNCTION();
    const auto found = m_BlockUpsetWaitingDB.find(static_cast<std::uint32_t>(userId));
    if (found == m_BlockUpsetWaitingDB.end()) {
        return false;
    }
    auto* const member = static_cast<CBlockUpsetMemberInfo*>(found->second);
    _BroadcastToPlayer(
        m_BlockUpsetWaitingDB, "chdddd", 0xfe, 0x98, 2, GetCombatStartingRemainSec(), static_cast<int>(member->m_Team),
        userId
    );
    m_BlockUpsetWaitingDB.erase(found);
    _InsertBannedUser(userId);
    if (User* const user = FindUserByObjectId(static_cast<std::uint32_t>(userId))) {
        CLogFormatter formatter;
        formatter.WriteLog2(0x1f5a, user, nullptr, m_Index, 0, 0, 0, 0, 0, 0);
    }
    _RemoveBlockUpsetWaitingPlayerInfo(member);
    return true;
}

void CBlockUpset::_RemoveBlockUpsetWaitingPlayerInfo(CBlockUpsetMemberInfo* memberInfo) {
    L2_GUARD_FUNCTION();
    TeamLine& team = m_BlockUpsetWaitingLine[TeamIndex(memberInfo->m_Team)];
    for (auto it = team.begin(); it != team.end(); ++it) {
        if (*it == memberInfo) {
            team.erase(it);
            break;
        }
    }
    ReleaseBlockUpsetInfo().Do(*memberInfo);
    _CheckStartBlockUpsetCombat(m_pManager->m_nVoteWaitSec);
}

bool CBlockUpset::ChangeBlockUpsetWaitingPlayerTeam(int userId) {
    bool changed;
    {
        L2_GUARD(m_Lock);
        const auto found = m_BlockUpsetWaitingDB.find(static_cast<std::uint32_t>(userId));
        changed = found != m_BlockUpsetWaitingDB.end() &&
            _ChangeBlockUpsetWaitingPlayerTeam(static_cast<CBlockUpsetMemberInfo*>(found->second));
    }
    return changed;
}

bool CBlockUpset::_ChangeBlockUpsetWaitingPlayerTeam(CBlockUpsetMemberInfo* memberInfo) {
    L2_GUARD_FUNCTION();
    const BlueRedTeam::Enum oldTeam = memberInfo->m_Team;
    const BlueRedTeam::Enum newTeam = GetOpposite(oldTeam);
    if (static_cast<int>(oldTeam) >= 2 || static_cast<int>(newTeam) >= 2) {
        return false;
    }

    TeamLine& oldLine = m_BlockUpsetWaitingLine[TeamIndex(oldTeam)];
    const auto oldMember = std::find(oldLine.begin(), oldLine.end(), memberInfo);
    if (oldMember == oldLine.end()) {
        return false;
    }
    oldLine.erase(oldMember);

    TeamLine& newLine = m_BlockUpsetWaitingLine[TeamIndex(newTeam)];
    if (std::find(newLine.begin(), newLine.end(), memberInfo) != newLine.end()) {
        return false;
    }
    newLine.push_back(memberInfo);
    memberInfo->m_Team = newTeam;
    _BroadcastToPlayer(
        m_BlockUpsetWaitingDB, "chdddd", 0xfe, 0x98, 5, static_cast<int>(memberInfo->m_nUserId), static_cast<int>(oldTeam),
        static_cast<int>(newTeam)
    );
    return true;
}

bool CBlockUpset::VoteToStart(int userId, bool start) {
    L2_GUARD_FUNCTION();
    if (m_State != PVPMatchState::REGISTER || m_VoteTryTime >= 3 || !m_Voting) {
        return false;
    }
    {
        L2_GUARD(m_Lock);
        const auto found = m_BlockUpsetWaitingDB.find(static_cast<std::uint32_t>(userId));
        if (found == m_BlockUpsetWaitingDB.end()) {
            return false;
        }
        auto* const member = static_cast<CBlockUpsetMemberInfo*>(found->second);
        if (member->m_bVoted) {
            return false;
        }
        member->m_bVoted = true;
    }

    const int half = runtime::Int32FromBits(static_cast<std::uint32_t>(m_BlockUpsetWaitingDB.size() >> 1U));
    if (start) {
        m_VoteToStart = runtime::AddInt32Wrapping(m_VoteToStart, 1);
        if (half < m_VoteToStart) {
            {
                L2_GUARD(m_Lock);
                _StartCombat();
            }
        }
    } else {
        m_VoteToWait = runtime::AddInt32Wrapping(m_VoteToWait, 1);
        if (half < m_VoteToWait) {
            SetCombatStartingRemainSec(60);
            ResetVoteToStart();
        }
    }
    return true;
}

void CBlockUpset::ResetVoteToStart() {
    L2_GUARD_FUNCTION();
    m_Voting = false;
    m_VoteToStart = 0;
    m_VoteToWait = 0;
    std::for_each(m_BlockUpsetWaitingDB.begin(), m_BlockUpsetWaitingDB.end(), ResetVote());
}

bool CBlockUpset::_StartVoting() {
    L2_GUARD_FUNCTION();
    if (m_Voting || m_VoteTryTime >= 2) {
        return false;
    }
    m_Voting = true;
    m_VoteTryTime = runtime::AddInt32Wrapping(m_VoteTryTime, 1);
    _BroadcastToPlayer(m_BlockUpsetWaitingDB, "chd", 0xfe, 0x98, 4);
    return true;
}

bool CBlockUpset::RemoveBlockUpsetCombatPlayer(int userId) {
    bool removed;
    {
        L2_GUARD(m_Lock);
        removed = _RemoveBlockUpsetCombatPlayer(userId);
    }
    return removed;
}

bool CBlockUpset::_RemoveBlockUpsetCombatPlayer(int userId) {
    L2_GUARD_FUNCTION();
    const auto found = m_BlockUpsetCombatDB.find(static_cast<std::uint32_t>(userId));
    if (found == m_BlockUpsetCombatDB.end()) {
        return false;
    }
    auto* const member = static_cast<CBlockUpsetMemberInfo*>(found->second);
    _BroadcastToPlayer(
        m_BlockUpsetCombatDB, "chdddd", 0xfe, 0x98, 2, GetCombatStartingRemainSec(), static_cast<int>(member->m_Team), userId
    );
    m_BlockUpsetCombatDB.erase(found);
    if (User* const user = FindUserByObjectId(static_cast<std::uint32_t>(userId))) {
        CLogFormatter formatter;
        formatter.WriteLog2(
            0x1f5e, user, nullptr, m_Index, static_cast<int>(member->m_Team), member->m_Point,
            m_BlockUpsetPoints[TeamIndex(member->m_Team)].m_Point, 0, 0, 0
        );
        user->GetSocket()->Send("chd", 0xfe, 0x98, -1);
    }
    _RemoveBlockUpsetCombatPlayerInfo(member);
    return true;
}

void CBlockUpset::_RemoveBlockUpsetCombatPlayerInfo(CBlockUpsetMemberInfo* memberInfo) {
    L2_GUARD_FUNCTION();
    BanishCombatBlockUpsetUser().Do(*memberInfo);
    TeamLine& team = m_BlockUpsetCombatLine[TeamIndex(memberInfo->m_Team)];
    for (auto it = team.begin(); it != team.end(); ++it) {
        if (*it == memberInfo) {
            team.erase(it);
            break;
        }
    }
    if (m_BlockUpsetCombatDB.empty()) {
        SetBlockUpsetWinner(BlueRedTeam::MAX);
    } else {
        ReleaseBlockUpsetInfo().Do(*memberInfo);
    }
}

void CBlockUpset::_OnBlockUpsetPlayerAdded(CBlockUpsetMemberInfo* memberInfo) {
    L2_GUARD_FUNCTION();
    CObject* const object = CObject::GetObjectW(memberInfo->m_nUserId);
    User* user = nullptr;
    if (object != nullptr && object->IsPC()) {
        user = object->CastUser();
    }
    if (user != nullptr) {
        CLogFormatter formatter;
        formatter.WriteLog2(0x1f59, user, nullptr, m_Index, 0, 0, 0, 0, 0, 0);
    }
    if (m_State == PVPMatchState::REGISTER) {
        _CheckStartBlockUpsetCombat(m_pManager->m_nVoteWaitSec);
    }
}

bool CBlockUpset::IsBlockUpsetWaitingPlayer(int userId) {
    L2_GUARD_FUNCTION();
    bool found;
    {
        L2_GUARD(m_Lock);
        found = m_BlockUpsetWaitingDB.find(static_cast<std::uint32_t>(userId)) != m_BlockUpsetWaitingDB.end();
    }
    return found;
}

bool CBlockUpset::IsBlockUpsetCombatPlayer(int userId) {
    L2_GUARD_FUNCTION();
    bool found;
    {
        L2_GUARD(m_Lock);
        found = _IsBlockUpsetCombatPlayer(userId);
    }
    return found;
}

bool CBlockUpset::_IsBlockUpsetCombatPlayer(int userId) {
    L2_GUARD_FUNCTION();
    return m_BlockUpsetCombatDB.find(static_cast<std::uint32_t>(userId)) != m_BlockUpsetCombatDB.end();
}

int CBlockUpset::GetRelation(User& user, User& target) {
    L2_GUARD_FUNCTION();
    if (!user.IsInSamePVPMatch(target, PVPMatchType::BLOCK_UPSET, PVPMatchPlayerState::COMBAT)) {
        return 0;
    }
    int relation;
    {
        L2_GUARD(m_Lock);
        relation = _GetRelation(m_BlockUpsetCombatDB, user, target);
    }
    return relation;
}

bool CBlockUpset::LeaveWorld(User& user) {
    L2_GUARD_FUNCTION();
    bool removed;
    {
        L2_GUARD(m_Lock);
        removed = _RemoveBlockUpsetWaitingPlayer(static_cast<int>(user.Id()));
        if (!removed) {
            removed = _RemoveBlockUpsetCombatPlayer(static_cast<int>(user.Id()));
        }
    }
    return removed;
}

void CBlockUpset::GetBlockUpsetPlayerCount(int& blueWaiting, int& redWaiting, int& blueCombat, int& redCombat) const {
    blueWaiting = static_cast<int>(m_BlueWaitingTeam->size());
    redWaiting = static_cast<int>(m_RedWaitingTeam->size());
    blueCombat = static_cast<int>(m_BlueCombatTeam->size());
    redCombat = static_cast<int>(m_RedCombatTeam->size());
}

bool CBlockUpset::_CheckStartBlockUpsetCombat(int seconds) {
    L2_GUARD_FUNCTION();
    if (!m_bOnBalancing) {
        if (m_State == PVPMatchState::REGISTER && !m_WaitingNpcBlock.empty()) {
            const int playerCount = runtime::Int32FromBits(static_cast<std::uint32_t>(m_BlockUpsetWaitingDB.size()));
            if (playerCount == runtime::MultiplyInt32Wrapping(m_MaxRegisterCount, 2)) {
                _StartCombat();
            }
            if (runtime::MultiplyInt32Wrapping(m_MinRegisterCount, 2) <= playerCount) {
                if (!m_Voting) {
                    m_Voting = true;
                    m_VoteTryTime = 1;
                    _BroadcastToPlayer(m_BlockUpsetWaitingDB, "chd", 0xfe, 0x98, 4);
                }
                if (m_CombatStartTick == -1) {
                    SetCombatStartingRemainSec(seconds);
                }
                return true;
            }
        }
        SetCombatStartingRemainSec(-1);
        m_VoteTryTime = 0;
        ResetVoteToStart();
    }
    return false;
}

void CBlockUpset::ReadyPlayerToStart(BlueRedTeam::Enum team, User& user) {
    L2_GUARD_FUNCTION();
    FVector position{};
    if (!GetRandomPos(m_StartingPos[TeamIndex(team)], position)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"starting Pos data error. team = %d",
        //     static_cast<int>(team))
        // L2_LOG_ERROR(Log,
        //     L"starting Pos data error. team = %d",
        //     static_cast<int>(team))
        L2_LOG_ERROR(
            Log,
            "Block Upset cannot choose a starting position for player object %u on team %d; player preparation and teleport "
            "skipped.",
            user.Id(), static_cast<int>(team)
        );
        return;
    }
    m_pManager->ReadyPlayerToStart(team, user);
    user.DoNothing();
    user.TeleportToLocation(
        static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z), user.GetInZoneID(),
        0x10000, TeleportSourceType::BlockUpsetPCStart, nullptr, 0
    );
}

BlueRedTeam::Enum CBlockUpset::GetSmallTeam() const {
    const std::size_t blue = m_BlueWaitingTeam->size();
    const std::size_t red = m_RedWaitingTeam->size();
    return blue <= red ? BlueRedTeam::BLUE : BlueRedTeam::RED;
}

int CBlockUpset::AddBlockUpsetPoint(User& user, int point) {
    L2_GUARD_FUNCTION();
    if (m_State != PVPMatchState::COMBAT) {
        return -1;
    }
    int memberPoint;
    BlueRedTeam::Enum team;
    {
        L2_GUARD(m_Lock);
        const auto found = m_BlockUpsetCombatDB.find(user.Id());
        if (found == m_BlockUpsetCombatDB.end()) {
            return -1;
        }
        auto* const member = static_cast<CBlockUpsetMemberInfo*>(found->second);
        if (member == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[BlockUpset] member info error. userID = %d",
            //     user.Id())
            // L2_LOG_ERROR(Log,
            //     L"[BlockUpset] member info error. userID = %d",
            //     user.Id())
            L2_LOG_ERROR(
                Log, "Block Upset cannot add %d points to player object %u: combat member record is null; returning -1.",
                point, user.Id()
            );
            return -1;
        }
        member->m_Point = std::max(0, runtime::AddInt32Wrapping(member->m_Point, point));
        memberPoint = member->m_Point;
        team = member->m_Team;
    }
    if (static_cast<std::uint32_t>(team) >= 2U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[BlockUpset] team data error. userID = %d, team = %d, point = %d",
        //     user.Id(),
        //     static_cast<int>(team),
        //     memberPoint)
        // L2_LOG_ERROR(Log,
        //     L"[BlockUpset] team data error. userID = %d, team = %d, point = %d",
        //     user.Id(),
        //     static_cast<int>(team),
        //     memberPoint)
        L2_LOG_ERROR(
            Log,
            "Block Upset player object %u has team %d outside 0..1; updated personal score %d is retained, but team "
            "processing stops with a stack report.",
            user.Id(), static_cast<int>(team), memberPoint
        );
        runtime::process::ReportCallStack(LOG_ERROR, true);
        return -1;
    }
    BlockUpsetPoint& teamPoint = m_BlockUpsetPoints[static_cast<std::size_t>(team)];
    teamPoint.m_Point = std::max(0, teamPoint.m_Point);
    _BroadcastToPlayer(
        m_BlockUpsetCombatDB, "chddddddd", 0xfe, 0x99, 0, GetCombatFinishRemainSec(), m_BlockUpsetPoints[0].m_Point,
        m_BlockUpsetPoints[1].m_Point, static_cast<int>(team), static_cast<int>(user.Id()), memberPoint
    );
    CLogFormatter formatter;
    formatter.WriteLog2(
        0x1f5f, &user, nullptr, m_Index, static_cast<int>(team), point, memberPoint,
        m_BlockUpsetPoints[BlueRedTeam::RED].m_Point, m_BlockUpsetPoints[BlueRedTeam::BLUE].m_Point, 0
    );
    return teamPoint.m_Point;
}

int CBlockUpset::GetBlockUpsetPoint(BlueRedTeam::Enum team) const {
    return IsTeam(team) ? m_BlockUpsetPoints[TeamIndex(team)].m_Point : -1;
}

bool CBlockUpset::ChangeBlockColor(int userId, int color) {
    L2_GUARD_FUNCTION();
    User* const user = FindUserByObjectId(static_cast<std::uint32_t>(userId));
    if (user == nullptr) {
        return false;
    }
    const int oldColor = user->m_data->m_EvolutionId;
    if (oldColor == color) {
        return false;
    }
    user->m_data->m_EvolutionId = color;
    user->SendMyInfo();
    return OnTeamScoreChanged(oldColor, color);
}

bool CBlockUpset::OnTeamScoreChanged(int oldTeam, int newTeam) {
    L2_GUARD_FUNCTION();
    if (oldTeam < 2) {
        m_BlockUpsetPoints[oldTeam].m_Point = runtime::AddInt32Wrapping(m_BlockUpsetPoints[oldTeam].m_Point, -1);
    } else if (newTeam > 1) {
        return false;
    }
    if (newTeam < 2) {
        m_BlockUpsetPoints[newTeam].m_Point = runtime::AddInt32Wrapping(m_BlockUpsetPoints[newTeam].m_Point, 1);
    }
    _BroadcastToPlayer(
        m_BlockUpsetCombatDB, "chdddd", 0xfe, 0x99, 2, GetCombatFinishRemainSec(), m_BlockUpsetPoints[0].m_Point,
        m_BlockUpsetPoints[1].m_Point
    );
    return true;
}

bool CBlockUpset::SetBlockUpsetWinner(BlueRedTeam::Enum team) {
    L2_GUARD_FUNCTION();
    if (m_State != PVPMatchState::COMBAT) {
        return false;
    }
    if (team == BlueRedTeam::MAX) {
        m_BlockUpsetPoints[0].Init();
        m_BlockUpsetPoints[1].Init();
    }
    ChangeState(PVPMatchState::REWARD);
    _OnReward();
    return true;
}

void CBlockUpset::_StartCombat() {
    L2_GUARD_FUNCTION();
    if ((m_BlockUpsetWaitingDB.size() & 1U) != 0U && m_LastAddedUserId != 0) {
        _RemoveBlockUpsetWaitingPlayer(m_LastAddedUserId);
    }
    _BalanceTeam();
    ChangeState(PVPMatchState::COMBAT);
    _OnCombat();
}

void CBlockUpset::_OnRegister() {
    L2_GUARD_FUNCTION();
    _CheckStartBlockUpsetCombat(10);
}

void CBlockUpset::_OnCombat() {
    L2_GUARD_FUNCTION();
    m_CombatStartTick = -1;
    for (BlockUpsetPoint& point : m_BlockUpsetPoints) {
        point.Init();
    }
    m_LastAddedUserId = 0;
    m_VoteTryTime = 0;
    ResetVoteToStart();

    const int waitingNpcCount = runtime::Int32FromBits(static_cast<std::uint32_t>(m_WaitingNpcBlock.size()));
    for (int index = 0; index < waitingNpcCount; index = runtime::AddInt32Wrapping(index, 1)) {
        const int npcId = runtime::CheckedVectorAt(m_WaitingNpcBlock, static_cast<std::size_t>(index));
        CObject* const object = CObject::GetObjectW(static_cast<std::uint32_t>(npcId));
        if (object != nullptr && object->IsNPC()) {
            if (CNPC* const npc = object->CastNPC()) {
                npc->m_data->m_EvolutionId = index % 2;
                npc->SendMyInfo();
            }
        }
    }

    m_BlockUpsetCombatDB.insert(m_BlockUpsetWaitingDB.begin(), m_BlockUpsetWaitingDB.end());
    for (int team = 0; team < 2; ++team) {
        m_BlockUpsetCombatLine[team].clear();
        m_BlockUpsetCombatLine[team].insert(
            m_BlockUpsetCombatLine[team].end(), m_BlockUpsetWaitingLine[team].begin(), m_BlockUpsetWaitingLine[team].end()
        );
    }
    for (const auto& entry : m_BlockUpsetCombatDB) {
        OnCombatStart()(entry.second);
    }
    m_BlockUpsetWaitingDB.clear();
    m_BlockUpsetWaitingLine[BlueRedTeam::BLUE].clear();
    m_BlockUpsetWaitingLine[BlueRedTeam::RED].clear();

    CObject* const managerNpc = CObject::GetObjectW(static_cast<std::uint32_t>(m_ManagerNpcId));
    if (managerNpc != nullptr && managerNpc->IsNPC() && managerNpc->CastNPC() != nullptr) {
        g_npcserver.Send("cd", 0x89, m_ManagerNpcId);
    }
    CLogFormatter formatter;
    formatter.WriteLog2(
        0x1f5b, nullptr, nullptr, m_Index, runtime::Int32FromBits(static_cast<std::uint32_t>(m_RedCombatTeam->size())),
        runtime::Int32FromBits(static_cast<std::uint32_t>(m_BlueCombatTeam->size())), 0, 0, 0, 0
    );
}

void CBlockUpset::_OnReward() {
    L2_GUARD_FUNCTION();
    const BlueRedTeam::Enum winner = GetBlockUpsetWinner(m_BlockUpsetPoints);
    const BlueRedTeam::Enum loser = GetOpposite(winner);
    _SortByPoint();

    const int blueTotal = runtime::Int32FromBits(static_cast<std::uint32_t>(m_BlueCombatTeam->size()));
    const int redTotal = runtime::Int32FromBits(static_cast<std::uint32_t>(m_RedCombatTeam->size()));
    if (winner == BlueRedTeam::BLUE) {
        std::for_each(
            m_BlueCombatTeam->begin(), m_BlueCombatTeam->end(),
            CBlockUpsetManager::RewardHandler(m_pManager, PVPMatchGameResult::WIN, blueTotal)
        );
        std::for_each(
            m_RedCombatTeam->begin(), m_RedCombatTeam->end(),
            CBlockUpsetManager::RewardHandler(m_pManager, PVPMatchGameResult::LOSE, redTotal)
        );
    } else if (winner == BlueRedTeam::RED) {
        std::for_each(
            m_RedCombatTeam->begin(), m_RedCombatTeam->end(),
            CBlockUpsetManager::RewardHandler(m_pManager, PVPMatchGameResult::WIN, blueTotal)
        );
        std::for_each(
            m_BlueCombatTeam->begin(), m_BlueCombatTeam->end(),
            CBlockUpsetManager::RewardHandler(m_pManager, PVPMatchGameResult::LOSE, redTotal)
        );
    } else {
        std::for_each(
            m_BlueCombatTeam->begin(), m_BlueCombatTeam->end(),
            CBlockUpsetManager::RewardHandler(m_pManager, PVPMatchGameResult::DRAW, blueTotal)
        );
        std::for_each(
            m_RedCombatTeam->begin(), m_RedCombatTeam->end(),
            CBlockUpsetManager::RewardHandler(m_pManager, PVPMatchGameResult::DRAW, redTotal)
        );
    }

    CLogFormatter formatter;
    formatter.WriteLog2(
        0x1f5c, nullptr, nullptr, m_Index, static_cast<int>(winner), redTotal, blueTotal,
        m_BlockUpsetPoints[BlueRedTeam::RED].m_Point, m_BlockUpsetPoints[BlueRedTeam::BLUE].m_Point, 0
    );

    _BroadcastToPlayer(m_BlockUpsetCombatDB, "chddd", 0xfe, 0x99, 1, static_cast<int>(winner), static_cast<int>(loser));
    _RemoveAllBlockUpsetCombatPlayers();

    CObject* const managerNpc = CObject::GetObjectW(static_cast<std::uint32_t>(m_ManagerNpcId));
    if (managerNpc != nullptr && managerNpc->IsNPC() && managerNpc->CastNPC() != nullptr) {
        g_npcserver.Send("cd", 0x8a, m_ManagerNpcId);
    }
    for (BlockUpsetPoint& point : m_BlockUpsetPoints) {
        point.Init();
    }
    m_LastAddedUserId = 0;
    for (const int npcId : m_CombatNpcBlock) {
        _AddBlockUpsetNpc(npcId);
    }
    m_CombatNpcBlock.clear();
}

void CBlockUpset::_SortByPoint() {
    L2_GUARD_FUNCTION();
    m_BlueCombatTeam->sort(CPVPMatchMemberInfo::SortPlayerByPoint);
    m_RedCombatTeam->sort(CPVPMatchMemberInfo::SortPlayerByPoint);
    std::for_each(m_BlueCombatTeam->begin(), m_BlueCombatTeam->end(), CPVPMatchMemberInfo::SetOrder());
    std::for_each(m_RedCombatTeam->begin(), m_RedCombatTeam->end(), CPVPMatchMemberInfo::SetOrder());
}

void CBlockUpset::_BalanceTeam() {
    L2_GUARD_FUNCTION();
    if (m_BlueWaitingTeam->size() == m_RedWaitingTeam->size()) {
        return;
    }
    m_bOnBalancing = true;
    const std::size_t blueCount = m_BlueWaitingTeam->size();
    const std::size_t redCount = m_RedWaitingTeam->size();
    TeamLine* const larger = blueCount < redCount ? m_RedWaitingTeam : m_BlueWaitingTeam;
    TeamLine* const smaller = blueCount < redCount ? m_BlueWaitingTeam : m_RedWaitingTeam;
    const BlueRedTeam::Enum smallerTeam = redCount <= blueCount ? BlueRedTeam::RED : BlueRedTeam::BLUE;
    const int movingCount = GetMovingTeamCount(
        m_MinRegisterCount, runtime::Int32FromBits(static_cast<std::uint32_t>(larger->size())),
        runtime::Int32FromBits(static_cast<std::uint32_t>(smaller->size()))
    );
    for (int index = 0; index < movingCount; ++index) {
        auto* const member = static_cast<CBlockUpsetMemberInfo*>(larger->back());
        if (User* const user = FindUserByObjectId(member->m_nUserId)) {
            _AddBlockUpsetWaitingPlayer(smallerTeam, *user);
        }
    }
    m_bOnBalancing = false;
}

bool CBlockUpset::SetCombatStartingRemainSec(int seconds) {
    L2_GUARD_FUNCTION();
    if (m_State != PVPMatchState::REGISTER) {
        return false;
    }
    if (seconds == -1) {
        if (m_CombatStartTick == -1) {
            return false;
        }
        m_CombatStartTick = -1;
        _BroadcastToPlayer(m_BlockUpsetWaitingDB, "chdd", 0xfe, 0x98, 3, -1);
    } else {
        m_CombatStartTick =
            runtime::Int32FromBits(runtime::GetTickCount() + static_cast<std::uint32_t>(seconds) * UINT32_C(1000));
        _BroadcastToPlayer(m_BlockUpsetWaitingDB, "chdd", 0xfe, 0x98, 3, GetCombatStartingRemainSec());
    }
    return false;
}

int CBlockUpset::GetCombatStartingRemainSec() const {
    L2_GUARD_FUNCTION();
    if (m_State != PVPMatchState::REGISTER || m_CombatStartTick == -1) {
        return -1;
    }
    const int seconds =
        runtime::SubtractInt32Wrapping(m_CombatStartTick, runtime::Int32FromBits(runtime::GetTickCount())) / 1000;
    return std::max(seconds, 0);
}

int CBlockUpset::GetCombatFinishRemainSec() const {
    L2_GUARD_FUNCTION();
    if (m_State != PVPMatchState::COMBAT) {
        return -1;
    }
    return runtime::SubtractInt32Wrapping(
               runtime::AddInt32Wrapping(
                   runtime::MultiplyInt32Wrapping(m_pManager->m_GamePeriodSec, 1000), m_CurrentStateStartTime
               ),
               runtime::Int32FromBits(runtime::GetTickCount())
           ) /
        1000;
}

BlueRedTeam::Enum CBlockUpset::GetBlockUpsetWinner(const BlockUpsetPoint* points) {
    L2_GUARD_FUNCTION();
    if (points[0].m_Point > points[1].m_Point) {
        return BlueRedTeam::BLUE;
    }
    if (points[1].m_Point > points[0].m_Point) {
        return BlueRedTeam::RED;
    }
    return BlueRedTeam::MAX;
}

void CBlockUpset::SetMaxRegisterCount(int count) { m_MaxRegisterCount = std::max(count, m_MinRegisterCount); }

void CBlockUpset::SetMinRegisterCount(int count) {
    L2_GUARD_FUNCTION();
    if (m_MaxRegisterCount < m_MinRegisterCount) {
        m_MinRegisterCount = m_MaxRegisterCount;
    }
    m_MinRegisterCount = count;
    if (m_State == PVPMatchState::REGISTER) {
        m_VoteTryTime = 0;
        ResetVoteToStart();
    }
}

wchar_t* CBlockUpset::GetName() const { return const_cast<wchar_t*>(L"BlockUpset"); }

bool CBlockUpset::IsVoting() const { return m_Voting; }

void CBlockUpset::_MakeBlockUpsetListTotal(CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();
    packet.Assemble("ddd", 0, m_Index, GetCombatStartingRemainSec());
    packet.Assemble("d", runtime::Int32FromBits(static_cast<std::uint32_t>(m_BlueWaitingTeam->size())));
    for (CPVPMatchMemberInfo* member : *m_BlueWaitingTeam) {
        member->MakeListInfo(packet);
    }
    packet.Assemble("d", runtime::Int32FromBits(static_cast<std::uint32_t>(m_RedWaitingTeam->size())));
    for (CPVPMatchMemberInfo* member : *m_RedWaitingTeam) {
        member->MakeListInfo(packet);
    }
}

void CBlockUpset::TimerExpired(int id) {
    L2_GUARD_FUNCTION();
    CPVPMatch::TimerExpired(id);
    {
        L2_GUARD(m_Lock);
        if (m_State == PVPMatchState::REGISTER) {
            if (m_CombatStartTick != -1) {
                if (!m_Voting && m_VoteTryTime == 1) {
                    if (runtime::SubtractInt32Wrapping(
                            m_CombatStartTick, runtime::MultiplyInt32Wrapping(m_pManager->m_nVoteWaitSec, 1000)
                        ) < runtime::Int32FromBits(runtime::GetTickCount())) {
                        _StartVoting();
                    }
                } else if (GetCombatStartingRemainSec() < 1) {
                    _StartCombat();
                }
            }
            if ((m_TimerTick % 5) != 0) {
                _CheckLogoutPlayers(m_BlockUpsetWaitingDB);
            }
        } else if (m_State == PVPMatchState::COMBAT) {
            if (runtime::AddInt32Wrapping(
                    m_CurrentStateStartTime, runtime::MultiplyInt32Wrapping(m_pManager->m_GamePeriodSec, 1000)
                ) <= runtime::Int32FromBits(runtime::GetTickCount())) {
                ChangeState(PVPMatchState::REWARD);
                _OnReward();
            }
            if ((m_TimerTick % 5) != 0) {
                _CheckLogoutPlayers(m_BlockUpsetCombatDB);
            }
        } else if (m_State == PVPMatchState::REWARD &&
            runtime::AddInt32Wrapping(m_CurrentStateStartTime, 60000) <= runtime::Int32FromBits(runtime::GetTickCount())) {
            ChangeState(PVPMatchState::REGISTER);
            _OnRegister();
        }
    }
}

CBlockUpsetManager::WinLoseSetting::WinLoseSetting() : m_RewardItemClassId(0), m_RewardItemCount(0), m_RewardStep(0) {}

void CBlockUpsetManager::WinLoseSetting::Init() {
    m_RewardItemClassId = 0;
    m_RewardItemCount = 0;
    m_RewardStep = 0;
}

CBlockUpsetManager::RewardHandler::RewardHandler(
    CBlockUpsetManager* manager, PVPMatchGameResult::Enum result, int totalPlayer
) :
    m_pManager(manager), m_Result(result), m_TotalPlayer(totalPlayer) {}

void CBlockUpsetManager::RewardHandler::GiveItem(User& user, WinLoseSetting& setting) {
    L2_GUARD_FUNCTION();
    InventoryParams parameters;
    parameters.nItemClassID = setting.m_RewardItemClassId;
    parameters.nCount = setting.m_RewardItemCount;
    user.AddItemToInventory(parameters);
}

void CBlockUpsetManager::RewardHandler::operator()(CPVPMatchMemberInfo* memberInfo) {
    L2_GUARD_FUNCTION();
    User* const user = FindUserByObjectId(memberInfo->m_nUserId);
    if (user == nullptr || memberInfo->GetPoint() < m_pManager->m_nMinimumPoint) {
        return;
    }

    {
        L2_WRITE_GUARD(user->m_inv);
        GiveItem(*user, m_pManager->m_RewardSettting[BlockUpsetRewardType::DEFAULT]);
        if (user->m_data->m_bPCCafeUser) {
            GiveItem(*user, m_pManager->m_RewardSettting[BlockUpsetRewardType::PCBANG_DEFAULT]);
        }

        switch (m_Result) {
            case PVPMatchGameResult::WIN: {
                GiveItem(*user, m_pManager->m_RewardSettting[BlockUpsetRewardType::WIN_DEFAULT]);
                const int orderOffset = std::max(m_TotalPlayer - memberInfo->m_nOrder, 0);
                WinLoseSetting& stepped = m_pManager->m_RewardSettting[BlockUpsetRewardType::WIN];
                InventoryParams parameters;
                parameters.nItemClassID = stepped.m_RewardItemClassId;
                parameters.nCount = stepped.m_RewardItemCount + stepped.m_RewardStep * orderOffset;
                user->AddItemToInventory(parameters);
                break;
            }
            case PVPMatchGameResult::LOSE:
                GiveItem(*user, m_pManager->m_RewardSettting[BlockUpsetRewardType::LOSE]);
                break;
            case PVPMatchGameResult::DRAW:
                GiveItem(*user, m_pManager->m_RewardSettting[BlockUpsetRewardType::DRAW]);
                break;
            default:
                break;
        }
    }
}

CBlockUpsetManager::CBlockUpsetManager() :
    m_BlockUpset{},
    m_RewardSettting{},
    m_nVoteWaitSec(10),
    m_nMinimumPoint(10) {
    L2_GUARD_FUNCTION();
    InitSetting();
    InitBlockUpsetManager();
}

CBlockUpsetManager::~CBlockUpsetManager() = default;

void CBlockUpsetManager::InitBlockUpsetManager() {
    L2_GUARD_FUNCTION();
    for (int stage = 0; stage < MAX_BLOCK_GAME; ++stage) {
        m_BlockUpset[stage].InitBlockUpset(stage, *this);
    }
}

void CBlockUpsetManager::InitSetting() {
    L2_GUARD_FUNCTION();
    for (CBlockUpset& block : m_BlockUpset) {
        block.InitSetting();
    }
    m_GamePeriodSec = 600;
    m_nVoteWaitSec = 10;
    m_nMinimumPoint = 10;
    m_pEnterSkill[0] = nullptr;
    m_pEnterSkill[1] = nullptr;
    m_pWaitingSkill = nullptr;
    for (WinLoseSetting& reward : m_RewardSettting) {
        reward.Init();
    }
}

void CBlockUpsetManager::OnNpcServerClosed() {
    L2_GUARD_FUNCTION();
    for (CBlockUpset& block : m_BlockUpset) {
        block.OnNpcServerClosed();
    }
}

void CBlockUpsetManager::RemoveAllBlockUpsetWaitingPlayers() {
    for (CBlockUpset& block : m_BlockUpset) {
        block.RemoveAllBlockUpsetWaitingPlayers();
    }
}

void CBlockUpsetManager::RemoveAllBlockUpsetCombatPlayers() {
    for (CBlockUpset& block : m_BlockUpset) {
        block.RemoveAllBlockUpsetCombatPlayers();
    }
}

bool CBlockUpsetManager::IsInGameRange(int stage) { return stage >= 0 && stage < MAX_BLOCK_GAME; }

void CBlockUpsetManager::TimerExpired(int id) {
    L2_GUARD_FUNCTION();
    for (CBlockUpset& block : m_BlockUpset) {
        block.TimerExpired(id);
    }
    AddTimer(1000, 0);
}

void CBlockUpsetManager::ShowBlockUpsetInfo(User& user) {
    L2_GUARD_FUNCTION();
    user.GetSocket()->SendSystemMessage(u"SYS", u"BlockUpset info \n");
    for (CBlockUpset& block : m_BlockUpset) {
        block.ShowBlockUpsetInfo(user);
    }
}

void CBlockUpsetManager::ShowBlockUpsetState(int stage, User& user) {
    L2_GUARD_FUNCTION();
    if (IsInGameRange(stage)) {
        m_BlockUpset[stage].ShowBlockUpsetState(user);
    }
}

void CBlockUpsetManager::RegisterManagerNpcId(int stage, int npcId) {
    L2_GUARD_FUNCTION();
    if (IsInGameRange(stage)) {
        m_BlockUpset[stage].RegisterManagerNpcId(npcId);
    }
}

bool CBlockUpsetManager::AddBlockUpsetNpc(int stage, int npcId) {
    L2_GUARD_FUNCTION();
    if (!IsInGameRange(stage)) {
        return false;
    }
    m_BlockUpset[stage].AddBlockUpsetNpc(npcId);
    return true;
}

void CBlockUpsetManager::ShowWaitingNpcBlock(bool show) {
    L2_GUARD_FUNCTION();
    for (CBlockUpset& block : m_BlockUpset) {
        block.ShowWaitingNpcBlock(show);
    }
}

void CBlockUpsetManager::ChangeBlockUpsetNpcAmount(int stage, int amount) {
    L2_GUARD_FUNCTION();
    if (IsInGameRange(stage)) {
        m_BlockUpset[stage].ChangeBlockUpsetNpcAmount(amount);
    }
}

bool CBlockUpsetManager::CanAddBlockUpsetWaitingPlayer(int stage, User& user) {
    L2_GUARD_FUNCTION();
    return IsInGameRange(stage) && m_BlockUpset[stage].CanAddBlockUpsetWaitingPlayer(user);
}

bool CBlockUpsetManager::EnterBlockUpsetPlayer(int stage, User& user) {
    L2_GUARD_FUNCTION();
    if (!CanAddBlockUpsetWaitingPlayer(stage, user)) {
        return false;
    }
    RemoveBlockUpsetWaitingPlayer(static_cast<int>(user.Id()));
    return m_BlockUpset[stage].EnterBlockUpsetPlayer(user);
}

bool CBlockUpsetManager::AddBlockUpsetWaitingPlayer(int stage, BlueRedTeam::Enum team, User& user) {
    L2_GUARD_FUNCTION();
    if (!CanAddBlockUpsetWaitingPlayer(stage, user)) {
        return false;
    }
    const int previous = IsBlockUpsetWaitingPlayer(static_cast<int>(user.Id()));
    if (previous != -1 && previous != stage) {
        m_BlockUpset[previous].RemoveBlockUpsetWaitingPlayer(static_cast<int>(user.Id()));
    }
    return m_BlockUpset[stage].AddBlockUpsetWaitingPlayer(team, user);
}

bool CBlockUpsetManager::RemoveBlockUpsetWaitingPlayer(int userId) {
    L2_GUARD_FUNCTION();
    for (CBlockUpset& block : m_BlockUpset) {
        if (block.RemoveBlockUpsetWaitingPlayer(userId)) {
            return true;
        }
    }
    return false;
}

bool CBlockUpsetManager::RemoveBlockUpsetWaitingPlayer(int stage, int userId) {
    L2_GUARD_FUNCTION();
    return IsInGameRange(stage) && m_BlockUpset[stage].RemoveBlockUpsetWaitingPlayer(userId);
}

bool CBlockUpsetManager::RemoveBlockUpsetCombatPlayer(int userId) {
    for (CBlockUpset& block : m_BlockUpset) {
        if (block.RemoveBlockUpsetCombatPlayer(userId)) {
            return true;
        }
    }
    return false;
}

bool CBlockUpsetManager::VoteToStart(int stage, int userId, bool start) {
    L2_GUARD_FUNCTION();
    return IsInGameRange(stage) && m_BlockUpset[stage].VoteToStart(userId, start);
}

int CBlockUpsetManager::IsBlockUpsetWaitingPlayer(int userId) {
    L2_GUARD_FUNCTION();
    for (int stage = 0; stage < MAX_BLOCK_GAME; ++stage) {
        if (m_BlockUpset[stage].IsBlockUpsetWaitingPlayer(userId)) {
            return stage;
        }
    }
    return -1;
}

int CBlockUpsetManager::IsBlockUpsetCombatPlayer(int userId) {
    L2_GUARD_FUNCTION();
    for (int stage = 0; stage < MAX_BLOCK_GAME; ++stage) {
        if (m_BlockUpset[stage].IsBlockUpsetCombatPlayer(userId)) {
            return stage;
        }
    }
    return -1;
}

int CBlockUpsetManager::GetRelation(User& user, User& target) {
    L2_GUARD_FUNCTION();
    if (!user.IsInSamePVPMatch(target, PVPMatchType::BLOCK_UPSET, PVPMatchPlayerState::COMBAT)) {
        return 0;
    }
    for (CBlockUpset& block : m_BlockUpset) {
        const int relation = block.GetRelation(user, target);
        if (relation != 0) {
            return relation;
        }
    }
    return 0;
}

bool CBlockUpsetManager::IsEnemyTo(User& user, User& target) { return GetRelation(user, target) == 1; }

bool CBlockUpsetManager::LeaveWorld(User& user) {
    L2_GUARD_FUNCTION();
    if (!user.IsInPVPMatch(PVPMatchType::BLOCK_UPSET, PVPMatchPlayerState::NONE)) {
        return false;
    }
    for (CBlockUpset& block : m_BlockUpset) {
        if (block.LeaveWorld(user)) {
            return true;
        }
    }
    return false;
}

int CBlockUpsetManager::AddBlockUpsetPoint(User& user, int point) {
    L2_GUARD_FUNCTION();
    const int stage = IsBlockUpsetCombatPlayer(static_cast<int>(user.Id()));
    return stage != -1 ? m_BlockUpset[stage].AddBlockUpsetPoint(user, point) : -1;
}

int CBlockUpsetManager::GetBlockUpsetPoint(int stage, BlueRedTeam::Enum team) const {
    return IsInGameRange(stage) ? m_BlockUpset[stage].GetBlockUpsetPoint(team) : -1;
}

bool CBlockUpsetManager::ChangeBlockColor(int stage, int userId, int color) {
    L2_GUARD_FUNCTION();
    return IsInGameRange(stage) && m_BlockUpset[stage].ChangeBlockColor(userId, color);
}

bool CBlockUpsetManager::SetBlockUpsetWinner(int stage, BlueRedTeam::Enum team) {
    L2_GUARD_FUNCTION();
    return IsInGameRange(stage) && m_BlockUpset[stage].SetBlockUpsetWinner(team);
}

bool CBlockUpsetManager::SetMinRegisterCount(int stage, int count) {
    L2_GUARD_FUNCTION();
    if (IsInGameRange(stage)) {
        m_BlockUpset[stage].SetMinRegisterCount(count);
    }
    return IsInGameRange(stage);
}

void CBlockUpsetManager::SetVoteWaitingSec(int seconds) { m_nVoteWaitSec = std::max(seconds, 0); }

void CBlockUpsetManager::SetGamePeriod(int seconds) { m_GamePeriodSec = std::max(seconds, 0); }

void CBlockUpsetManager::ReadyPlayerToStart(BlueRedTeam::Enum team, User& user) {
    L2_GUARD_FUNCTION();
    user.DispelByBuffProtectionFlag(0x1f);
    if (user.m_data->m_StopMode == STP_STAND) {
        user.GetController()->Stand(0x9f6);
    }
    CSkillInfo* const enterSkill = m_pEnterSkill[static_cast<int>(team)];
    if (enterSkill != nullptr) {
        user.InfectedSkillBy(nullptr, enterSkill, nullptr, 0);
    }
    if (m_pWaitingSkill != nullptr) {
        user.InfectedSkillBy(nullptr, m_pWaitingSkill, nullptr, 0);
    }
}

CBlockUpset& CBlockUpsetManager::GetStage(int stage) {
    L2_GUARD_FUNCTION();
    return m_BlockUpset[IsInGameRange(stage) ? stage : 0];
}

void CBlockUpsetManager::SetReward(
    BlockUpsetRewardType::Enum type, int itemClassId, std::int64_t itemCount, std::int64_t step
) {
    L2_GUARD_FUNCTION();
    if (type < BlockUpsetRewardType::MAX) {
        WinLoseSetting& reward = m_RewardSettting[type];
        reward.m_RewardItemClassId = itemClassId;
        reward.m_RewardItemCount = itemCount;
        reward.m_RewardStep = step;
    }
}

void CBlockUpsetManager::DeleteItemsAfterMatch(int itemClassId) {
    L2_GUARD_FUNCTION();
    m_DeleteItemsAfterMatch.push_back(itemClassId);
}

void CBlockUpsetManager::SetSkills(CSkillInfo* blueEnterSkill, CSkillInfo* redEnterSkill, CSkillInfo* waitingSkill) {
    m_pEnterSkill[BlueRedTeam::BLUE] = blueEnterSkill;
    m_pEnterSkill[BlueRedTeam::RED] = redEnterSkill;
    m_pWaitingSkill = waitingSkill;
}
