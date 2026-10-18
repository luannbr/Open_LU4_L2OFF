#include "PledgeGameSystem.h"
#include "AccountJobFinishSignal.h"
#include "Area.h"
#include "CConfig.h"
#include "CursedWeapon.h"
#include "DB.h"
#include "Duel.h"
#include "HTMLCache.h"
#include "HtmlActionParamParser.h"
#include "IOCPMessageServer.h"
#include "InstantZone.h"
#include "Item.h"
#include "MatchingPointDB.h"
#include "NPC.h"
#include "NpcServer.h"
#include "ObjectDB.h"
#include "OlympiadStuff.h"
#include "Party.h"
#include "Pet.h"
#include "Pledge.h"
#include "PledgeFunction.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Relay.h"
#include "Shared/src/DXPacket.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/UserConst.h"
#include "Shared/src/Utils.h"
#include "Shared/src/net/Serialize.h"
#include "Skill.h"
#include "ThreadLocalWorldMap.h"
#include "ToDoList.h"
#include "User.h"
#include "UserBroadcaster.h"
#include "UserSocket.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <optional>
#include <string>
#include <utility>

const std::int32_t PledgeGameDefine::MAX_USER_PER_GAME = 18;

namespace {

constexpr bool kPledgeGameStateTransitions[CPledgeGameUserStateMachine::PLEDGEGAME_MAX]
                                          [CPledgeGameUserStateMachine::PLEDGEGAME_MAX] = {
                                              {true, false, false, true, false, true},
                                              {true, false, false, false, false, false},
                                              {true, false, false, false, false, true},
                                              {true, false, false, false, true, false},
                                              {true, true, false, false, true, false},
                                              {true, false, true, false, false, false},
};

constexpr std::array<std::int32_t, 9> kWaitMatchBroadcastTimes{
    1,
    2,
    3,
    4,
    5,
    10,
    20,
    30,
    60,
};

constexpr std::array<std::int32_t, 12> kClearMatchBroadcastTimes{
    30,
    20,
    10,
    9,
    8,
    7,
    6,
    5,
    4,
    3,
    2,
    1,
};

std::uint64_t PledgeGameTargetRandRange(const std::uint64_t range) {
    // VS2015 _Rng_from_urng<__int64, _Rand_urng_from_func> used by the
    // current random_shuffle instantiation at L2Server.exe 0x008fd6d0.
    constexpr std::uint64_t kRandMask = 0x7fff;
    std::uint64_t value = 0;
    std::uint64_t mask = 0;
    do {
        value = 0;
        mask = 0;
        if (range != 1) {
            do {
                const std::uint64_t lane = static_cast<unsigned int>(std::rand()) & kRandMask;
                value = (value << 15) | lane;
                mask = (mask << 15) | kRandMask;
            } while (mask < range - 1);
        }
    } while (mask / range <= value / range && mask % range != range - 1);
    return value % range;
}

void PledgeGameTargetRandomShuffle(std::array<std::int32_t, 6>& fieldOrder) {
    for (std::size_t index = 1; index < fieldOrder.size(); ++index) {
        const std::size_t selected = static_cast<std::size_t>(PledgeGameTargetRandRange(index + 1));
        std::swap(fieldOrder[index], fieldOrder[selected]);
    }
}

} // namespace


CPledgeGameSystem g_PledgeGameSystem;

std::vector<std::int32_t> CPledgeGameSystem::s_lstWday;
std::vector<std::int32_t> CPledgeGameSystem::s_lstHour;

CPledgeGameFuncHandler::CPledgeGameFuncHandler(
    const std::uint8_t function, const std::int32_t npcIndex, const std::int32_t userIndex, const std::int32_t fieldId
) :
    m_Function(function), m_NpcIndex(npcIndex), m_UserIndex(userIndex), m_FieldId(fieldId - 1) {}

CPledgeGameFuncHandler::~CPledgeGameFuncHandler() = default;

bool CPledgeGameFuncHandler::PreCheck() {
    // Current L2Server 0x009a2bbc.
    L2_GUARD_FUNCTION();
    if (m_Function >= 8) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid func id(%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x18de,
        //     m_Function)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid func id(%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x18de,
        //     m_Function)
        L2_LOG_ERROR(
            Log, "Clan-game command rejected: function ID %u is outside the precheck range 0 through 7.",
            static_cast<unsigned>(m_Function)
        );
        return false;
    }

    m_User = CCreatureFactory::Inst().FindObjectSP(m_UserIndex);
    if (!m_User || !m_User->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid user index(%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x18e5,
        //     m_UserIndex)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid user index(%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x18e5,
        //     m_UserIndex)
        L2_LOG_ERROR(
            Log, "Clan-game command %u rejected: creature index %d is unavailable or does not identify a player.",
            static_cast<unsigned>(m_Function), m_UserIndex
        );
        return false;
    }
    if (m_Function == 6) {
        return true;
    }

    m_Npc = CCreatureFactory::Inst().FindObjectSP(m_NpcIndex);
    if (!m_Npc || !m_Npc->IsNPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid npc index(%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x18ee,
        //     m_NpcIndex)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid npc index(%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x18ee,
        //     m_NpcIndex)
        L2_LOG_ERROR(
            Log, "Clan-game command %u rejected: creature index %d is unavailable or does not identify an NPC.",
            static_cast<unsigned>(m_Function), m_NpcIndex
        );
        return false;
    }
    return m_User->IsTalkableDistance(m_Npc.get());
}

bool CPledgeGameFuncHandler::AddPledgeGame() {
    // Current L2Server 0x009ae5cc.
    L2_GUARD_FUNCTION();
    if (!g_PledgeGameSystem.RegisterEntry(m_User->CastUser())) {
        return false;
    }
    FVector p = m_User->CastUser()->GetPos();
    m_User->CastUser()->m_PledgeGameUser.SetReturnPos(p, true);

    m_User->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::REGISTERED_FOR_CLASS_INDEPENDENT_TEAM_MATCH);
    return true;
}

void CPledgeGameFuncHandler::RemovePledgeGame() {
    // Current L2Server 0x009ac66c.
    L2_GUARD_FUNCTION();
    if (g_PledgeGameSystem.LeaveGame(m_User->CastUser(), false)) {
        m_User->CastUser()->GetSocket()->SendSystemMessage(
            static_cast<unsigned int>(SystemMessageId::REMOVED_FROM_OLYMPIAD_WAITING_LIST)
        );
    } else {
        m_User->CastUser()->GetSocket()->SendSystemMessage(
            static_cast<unsigned int>(SystemMessageId::NOT_REGISTERED_FOR_OLYMPIAD)
        );
    }
}

void CPledgeGameFuncHandler::EscapeFromPledgeGame() {
    // Current L2Server 0x009ac80c.
    L2_GUARD_FUNCTION();
    if (m_User->CastUser()->m_PledgeGameUser.m_StateMachine.GetStatus() ==
        CPledgeGameUserStateMachine::PLEDGEGAME_OBSERVER) {
        if (auto* field = m_User->CastUser()->m_PledgeGameUser.GetField()) {
            field->RegisterMessage(4, static_cast<std::int32_t>(m_User->CastUser()->GetServerId()), 0, false);
        }
    } else {
        const auto s = m_User->CastUser()->m_PledgeGameUser.m_StateMachine.GetStatus();
        if (s == CPledgeGameUserStateMachine::PLEDGEGAME_PLAYER ||
            s == CPledgeGameUserStateMachine::PLEDGEGAME_PLAY_WAITING) {
            if (auto* field = m_User->CastUser()->m_PledgeGameUser.GetField()) {
                field->PlayerLeaving(static_cast<std::int32_t>(m_User->CastUser()->GetServerId()));
            }
        }
    }
    // Preserve every smart-pointer dereference: current emits 6 distinct CastUser calls in this handler.
}

void CPledgeGameFuncHandler::ObservePledgeGame() {
    // Current L2Server 0x009aca7c.
    L2_GUARD_FUNCTION();
    if (m_FieldId < 0 || m_FieldId > 5) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid field id(%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x196e,
        //     m_FieldId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid field id(%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x196e,
        //     m_FieldId)
        L2_LOG_ERROR(Log, "Cannot observe a clan game: field index %d is outside 0 through 5.", m_FieldId);
        return;
    }
    User* const user = m_User->CastUser();
    g_PledgeGameSystem.m_pPledgeGameFieldList[m_FieldId].AddObserver(static_cast<std::int32_t>(user->GetServerId()));
}

void CPledgeGameFuncHandler::AddWinnerRecord() {
    // Current L2Server 0x0099e854 performs only the cast.
    L2_GUARD_FUNCTION();
    static_cast<void>(m_User->CastUser());
}

void CPledgeGameFuncHandler::Execute() {
    // Current L2Server 0x009aea38.
    L2_GUARD_FUNCTION();
    if (m_Function < 3) {
        static_cast<void>(AddPledgeGame());
    } else if (m_Function == 3) {
        RemovePledgeGame();
    } else if (m_Function == 4) {
        EscapeFromPledgeGame();
    } else if (m_Function == 5) {
        ObservePledgeGame();
    } else if (m_Function == 6) {
        AddWinnerRecord();
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid func id(%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x1917,
        //     m_Function)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid func id(%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x1917,
        //     m_Function)
        L2_LOG_ERROR(
            Log, "Cannot execute clan-game function ID %u; only functions 0 through 6 are handled.",
            static_cast<unsigned>(m_Function)
        );
    }
}

CPledgeGameEntry::CPledgeGameEntry() { L2_GUARD_FUNCTION(); }

CPledgeGameEntry::~CPledgeGameEntry() = default;

bool CPledgeGameEntry::AddEntry(User* const user) {
    // Current L2Server 0x009a8180.
    L2_GUARD_FUNCTION();

    {
        L2_GUARD(m_Lock);
        m_EntryList.push_back(user);
        user->GetSocket()->Send("chd", 0xfe, 0x11f, 2);
        AddTimer(2000, static_cast<int>(user->m_nId));
    }
    return true;
}

void CPledgeGameEntry::ClearAllEntry() {
    // Current L2Server 0x009a32fc.
    L2_GUARD_FUNCTION();

    {
        L2_GUARD(m_Lock);
        m_EntryList.clear();
    }
}

std::int32_t CPledgeGameEntry::GetEntryCount() {
    // Current L2Server 0x0099d7e0.
    L2_GUARD_FUNCTION();

    const auto count = [&]() -> std::int32_t {
        L2_GUARD(m_Lock);
        const std::int32_t count = static_cast<std::int32_t>(m_EntryList.size());
        return count;
    }();
    return count;
}

bool CPledgeGameEntry::CheckUser(User* const user, const bool notify, const bool bypassEntryChecks) {
    // Current L2Server 0x009acbd0.
    L2_GUARD_FUNCTION();

    if (!bypassEntryChecks) {
        if (SharedPledgeGameInfo::GetInfo()->m_nStep != PledgeGameCondition::GAME_OPEN) {
            if (notify) {
                user->GetSocket()->SendSystemMessage(SystemMessageId::OUTSIDE_CEREMONY_OF_CHAOS_MATCH_PERIOD);
                user->GetSocket()->Send("chd", 0xfe, 0x11f, 0);
                user->m_PledgeGameUser.m_bGotInvitingMessage = false;
            }
            return false;
        }

        if (GetEntryCount() >= g_PledgeGameSystem.m_nActiveInstantZoneCount * 18) {
            if (notify) {
                user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_REGISTER_CAPACITY_EXCEEDED);
                user->GetSocket()->Send("chd", 0xfe, 0x11f, 0);
                user->m_PledgeGameUser.m_bGotInvitingMessage = false;
            }
            return false;
        }

        if (IsRegisteredUser(user, false)) {
            if (notify) {
                user->GetSocket()->SendSystemMessage(SystemMessageId::ALREADY_REGISTERED_FOR_CEREMONY_OF_CHAOS);
                user->GetSocket()->Send("chd", 0xfe, 0x11f, 2);
            }
            return false;
        }
    }

    std::int32_t reason = 0;
    if (g_PledgeGameSystem.IsValidUser(user, reason)) {
        return true;
    }
    if (notify) {
        user->GetSocket()->SendSystemMessage(reason);
        user->GetSocket()->Send("chd", 0xfe, 0x11f, 0);
        user->m_PledgeGameUser.m_bGotInvitingMessage = false;
    }
    return false;
}

bool CPledgeGameEntry::IsRegisteredUser(User* const user, const bool) {
    L2_GUARD_FUNCTION();
    const auto registered = [&]() -> bool {
        L2_GUARD(m_Lock);
        bool registered = false;
        for (User* const entry : m_EntryList) {
            if (entry != nullptr && entry == user) {
                registered = true;
                break;
            }
        }
        return registered;
    }();
    return registered;
}

bool CPledgeGameEntry::RemoveEntry(User* const user, const bool) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        for (auto position = m_EntryList.begin(); position != m_EntryList.end(); ++position) {
            if (*position == nullptr || *position != user) {
                continue;
            }
            if (CUserSocket* const socket = user->GetSocket()) {
                socket->Send("chd", 0xfe, 0x11f, 1);
                socket->SendSystemMessage(SystemMessageId::CHAOS_WAITING_LIST_REMOVED_WITH_SCHEDULE_AND_FORFEIT_RULES);
            }
            m_EntryList.erase(position);
            return true;
        }
    }
    return false;
}

void CPledgeGameEntry::TimerExpired(const int id) {
    L2_GUARD_FUNCTION();

    {
        L2_GUARD(m_Lock);
        for (User* const user : m_EntryList) {
            if (user && static_cast<int>(user->m_nId) == id) {
                user->SendSystemMessage(SystemMessageId::ALL_BUFFS_EXCEPT_VITALITY_WILL_BE_REMOVED);
                break;
            }
        }
    }
}

void CPledgeGameEntry::ArrangeUserToField(CPledgeGameField* const fields) {
    // Current L2Server 0x009a829c.
    L2_GUARD_FUNCTION();

    std::array<std::int32_t, 6> fieldOrder{0, 1, 2, 3, 4, 5};
    PledgeGameTargetRandomShuffle(fieldOrder);

    std::vector<User*> entries;
    {
        L2_GUARD(m_Lock);
        entries = m_EntryList;
    }

    const std::int32_t activeCount = g_PledgeGameSystem.m_nActiveInstantZoneCount;
    const std::int32_t entryCount = static_cast<std::int32_t>(entries.size());
    std::int32_t usersPerField = (entryCount - 1 + activeCount) / activeCount;
    usersPerField = std::max(usersPerField, 2);

    auto entry = entries.begin();
    for (std::int32_t order = 0; order < activeCount; ++order) {
        std::int32_t targetCount = std::rand() % 18 + usersPerField;
        if (targetCount > 17) {
            targetCount = PledgeGameDefine::MAX_USER_PER_GAME;
        }

        CPledgeGameField& field = fields[fieldOrder[order]];
        for (std::int32_t assigned = 0; assigned < targetCount; ++assigned) {
            if (entry == entries.end() || (*entry != nullptr && !field.AddEntry(*entry))) {
                break;
            }
            ++entry;
        }
    }

    for (std::int32_t fieldIndex = 0; fieldIndex < activeCount; ++fieldIndex) {
        if (!fields[fieldIndex].m_Players.m_MemberList.empty()) {
            g_MatchingPointDB.GenerateWinningRate(fields[fieldIndex].m_Players.m_MemberList);
        }
    }
}

CPledgeGamePlayers::CPledgeGamePlayers() : m_nFieldId(0), m_nMemberCount(0) {
    L2_GUARD_FUNCTION();
}

CPledgeGamePlayers::~CPledgeGamePlayers() = default;

L2_DEFINE_OBJECT_CLASS_ID(CPledgeGamePlayers)

L2_DEFINE_OBJECT_CLASS_NAME_LITERAL(CPledgeGamePlayers)

L2_DEFINE_OBJECT_ALLOCATOR(CPledgeGamePlayers, __GetClassName())

L2_DEFINE_OBJECT_NEW(CPledgeGamePlayers)

L2_DEFINE_OBJECT_RELEASE(CPledgeGamePlayers)

bool CPledgeGamePlayers::RegisterUser(User* const user) {
    // Current L2Server 0x009a7884.
    L2_GUARD_FUNCTION();

    if (!user->m_PledgeGameUser.RegisterPledgeGameEntry()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][%s] nStatus(%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x528,
        //     L"CPledgeGamePlayers::RegisterUser",
        //     static_cast<std::int32_t>(
        //         user->m_PledgeGameUser.m_StateMachine.GetStatus()))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][%s] nStatus(%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x528,
        //     L"CPledgeGamePlayers::RegisterUser",
        //     static_cast<std::int32_t>(
        //         user->m_PledgeGameUser.m_StateMachine.GetStatus()))
        L2_LOG_ERROR(
            Log,
            "Character %u failed the clan-game registration state transition (observed status %d); member-list registration "
            "still continues.",
            static_cast<unsigned>(user->GetDBID()), static_cast<int>(user->m_PledgeGameUser.m_StateMachine.GetStatus())
        );
    }

    const std::int32_t playNo = static_cast<std::int32_t>(m_MemberList.size()) + 1;
    if (playNo < 19) {
        PledgeGamePlayerInfo player;
        player.playNo = playNo;
        player.playerSID = user->GetServerId();
        player.playerDBID = user->GetDBID();
        player.playerIndex = user->m_data->index;
        player.classId = user->GetClass();
        player.killCount = 0;
        player.lifeTime = 300;
        player.alive = true;
        player.nMatchingPointBase = user->m_MatchingPoint.m_nMatchingPointBase[0];
        player.nMatchingPointDiff = user->m_MatchingPoint.m_nMatchingPointDiff[0];
        player.nMatchingPointTotal = user->m_MatchingPoint.m_nMatchingPointTotal[0];

        std::array<char16_t, 20> alias{};
        static_cast<void>(FormatUtf16To(alias.data(), alias.size(), u"Player%d", playNo));
        const std::u16string targetAlias = std::u16string(alias.data());
        const std::size_t aliasLength = std::min<std::size_t>(targetAlias.size(), 24);
        std::copy_n(targetAlias.data(), aliasLength, player.playerName);
        player.playerName[aliasLength] = u'\0';

        {
            L2_GUARD(m_Lock);
            m_MemberList.push_back(player);
        }
        user->m_PledgeGameUser.m_FieldId = m_nFieldId;
    }
    return playNo < 19;
}

void CPledgeGamePlayers::ObserverEnd() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        for (const PledgeGamePlayerInfo& info : m_MemberList) {
            CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(info.playerSID));
            if (user && user->m_data->m_bGMInvisible && user->m_bIsObserving) {
                user->ObserverEnd();
            }
        }
    }
}

void CPledgeGamePlayers::RestorePlayers() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        for (const PledgeGamePlayerInfo& info : m_MemberList) {
            CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(info.playerSID));
            if (!user) {
                continue;
            }

            {
                L2_GUARD(*user->m_lock);
                user->m_data->RestoreCP();
                user->m_data->RestoreHP();
                user->m_data->RestoreMP();
            }
            user->ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_PLEDGEGAME});

            CPledge* const pledge = user->GetPledge();
            if (pledge != nullptr) {
                pledge->AddPledgeSkillsTo(user.get(), false);
            }
        }
    }
}

void CPledgeGamePlayers::SendMatchMemberList(CPledgeGameObserverEntry&, const std::int32_t targetSid) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);

        std::array<char, 0x5000> packet{};
        std::int32_t packetLength = 0;
        std::int32_t playerCount = 0;
        for (const PledgeGamePlayerInfo& info : m_MemberList) {
            CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(info.playerSID));
            if (!user) {
                continue;
            }
            packetLength += ::Assemble(
                packet.data() + packetLength, static_cast<int>(packet.size()) - packetLength, "dddddd",
                static_cast<int>(info.playerSID), info.playNo, static_cast<int>(user->m_data->m_dBaseHP),
                static_cast<int>(user->m_data->m_dBaseCP), static_cast<int>(user->m_data->GetHP()),
                static_cast<int>(user->m_data->GetCP())
            );
            ++playerCount;
        }

        const auto sendList = [&](User* const user, const int displayNo) {
            if (user == nullptr || user->GetSocket() == nullptr) {
                return;
            }
            user->GetSocket()->Send(
                "chdddb", 0xfe, 0x122, displayNo, m_nMemberCount, playerCount, packetLength, packet.data()
            );
        };

        if (targetSid == 0) {
            for (const PledgeGamePlayerInfo& info : m_MemberList) {
                CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(info.playerSID));
                if (user) {
                    sendList(user.get(), user->m_PledgeGameUser.m_nShuffleNo);
                }
            }
        } else {
            CSPointer<User> target = g_UserMap.FindObject(targetSid);
            if (target) {
                const auto state = target->m_PledgeGameUser.m_StateMachine.GetStatus();
                const bool deadPlayer = (state == CPledgeGameUserStateMachine::PLEDGEGAME_PLAYER ||
                                            state == CPledgeGameUserStateMachine::PLEDGEGAME_PLAY_WAITING) &&
                    !target->m_PledgeGameUser.m_IsAlive;
                if (state == CPledgeGameUserStateMachine::PLEDGEGAME_OBSERVER || deadPlayer) {
                    sendList(target.get(), deadPlayer ? target->m_PledgeGameUser.m_nShuffleNo : 0);
                }
            }
        }
    }
}

void CPledgeGamePlayers::SendV(const char* const format, va_list arguments) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        for (const PledgeGamePlayerInfo& info : m_MemberList) {
            CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(info.playerSID));
            if (!user) {
                continue;
            }
            const auto state = user->m_PledgeGameUser.m_StateMachine.GetStatus();
            if (!g_PledgeGameSystem.IsRegisteredUser(user.get()) &&
                state != CPledgeGameUserStateMachine::PLEDGEGAME_PLAYER &&
                state != CPledgeGameUserStateMachine::PLEDGEGAME_PLAY_WAITING) {
                continue;
            }
            if (CUserSocket* const socket = user->GetSocket()) {
                va_list copy;
                va_copy(copy, arguments);
                socket->SendV(format, copy);
                va_end(copy);
            }
        }
    }
}

bool CPledgeGameObserverEntry::AddEntry(const std::int32_t userSid) {
    L2_GUARD_FUNCTION();
    return m_ObserverSet.insert(userSid).second;
}

bool CPledgeGameObserverEntry::RemoveEntry(const std::int32_t userSid) {
    L2_GUARD_FUNCTION();
    return m_ObserverSet.erase(userSid) != 0;
}

void CPledgeGameObserverEntry::Broadcast(const char* const format, va_list arguments) {
    L2_GUARD_FUNCTION();
    std::vector<std::int32_t> staleEntries;
    for (const std::int32_t sid : m_ObserverSet) {
        CSPointer<User> user = g_UserMap.FindObject(sid);
        if (!user) {
            staleEntries.push_back(sid);
            continue;
        }
        if (CUserSocket* const socket = user->GetSocket()) {
            va_list copy;
            va_copy(copy, arguments);
            socket->SendV(format, copy);
            va_end(copy);
        }
    }
    for (const std::int32_t sid : staleEntries) {
        m_ObserverSet.erase(sid);
    }
}

void CPledgeGameObserverEntry::Broadcast(const char* const format, ...) {
    L2_GUARD_FUNCTION();
    va_list arguments;
    va_start(arguments, format);
    Broadcast(format, arguments);
    va_end(arguments);
}

void CPledgeGameObserverEntry::RemoveEntryAll(const std::int32_t fieldId) {
    L2_GUARD_FUNCTION();

    for (auto position = m_ObserverSet.begin(); position != m_ObserverSet.end();) {
        CSPointer<User> user = g_UserMap.FindObject(*position);
        if (!user) {
            position = m_ObserverSet.erase(position);
            continue;
        }
        if (user->m_PledgeGameUser.m_FieldId == fieldId) {
            position = m_ObserverSet.erase(position);
            user->m_PledgeGameUser.EscapeFromPledgeGame();
            continue;
        }
        ++position;
    }
}

std::int32_t CPledgeGamePlayers::_GetAliveUserCount() {
    L2_GUARD_FUNCTION();
    std::int32_t count = 0;
    for (const PledgeGamePlayerInfo& info : m_MemberList) {
        if (info.playerSID == 0 || !info.alive) {
            continue;
        }
        CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(info.playerSID));
        if (user) {
            ++count;
        }
    }
    return count;
}

std::int32_t CPledgeGamePlayers::GetAliveUserCount() {
    // Current L2Server 0x0099f6b4.
    L2_GUARD_FUNCTION();

    const auto count = [&]() -> std::int32_t {
        L2_GUARD(m_Lock);
        const std::int32_t count = _GetAliveUserCount();
        return count;
    }();
    return count;
}

void CPledgeGamePlayers::_SetPlayersCount() {
    L2_GUARD_FUNCTION();
    m_nMemberCount = static_cast<std::int32_t>(m_MemberList.size());
}

User* CPledgeGamePlayers::_GetLiveLongUser() {
    L2_GUARD_FUNCTION();
    std::int32_t longestLife = -1;
    User* winner = nullptr;
    for (const PledgeGamePlayerInfo& info : m_MemberList) {
        if (!info.alive) {
            continue;
        }
        if (longestLife < info.lifeTime) {
            CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(info.playerSID));
            winner = user.get();
            longestLife = info.lifeTime;
            if (winner == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[_GetLiveLongUser][1] pUser is not valid")
                // L2_LOG_ERROR(Log,
                //     L"[_GetLiveLongUser][1] pUser is not valid")
                L2_LOG_WARN(
                    Log, "Clan-game survival-time winner candidate object %u is unavailable; candidate scanning continues.",
                    static_cast<unsigned>(info.playerSID)
                );
            }
        } else if (longestLife == info.lifeTime) {
            winner = nullptr;
        }
    }
    return winner;
}

User* CPledgeGamePlayers::_GetBestKillUser() {
    L2_GUARD_FUNCTION();
    std::int32_t bestKills = 0;
    User* winner = nullptr;
    for (const PledgeGamePlayerInfo& info : m_MemberList) {
        if (!info.alive) {
            continue;
        }
        if (bestKills < info.killCount) {
            CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(info.playerSID));
            winner = user.get();
            bestKills = info.killCount;
            if (winner == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[_GetBestKillUser][1] pUser is not valid")
                // L2_LOG_ERROR(Log,
                //     L"[_GetBestKillUser][1] pUser is not valid")
                L2_LOG_WARN(
                    Log, "Clan-game kill-count winner candidate object %u is unavailable; candidate scanning continues.",
                    static_cast<unsigned>(info.playerSID)
                );
            }
        } else if (bestKills == info.killCount) {
            winner = nullptr;
        }
    }
    return winner;
}

bool CPledgeGamePlayers::NoOneAlive() {
    L2_GUARD_FUNCTION();
    const auto result = [&]() -> bool {
        L2_GUARD(m_Lock);
        const bool result = _GetAliveUserCount() == 0;
        return result;
    }();
    return result;
}

bool CPledgeGamePlayers::IsAllUserOutExceptOne() {
    L2_GUARD_FUNCTION();
    const auto result = [&]() -> bool {
        L2_GUARD(m_Lock);
        const bool result = _GetAliveUserCount() == 1;
        return result;
    }();
    return result;
}

void CPledgeGamePlayers::CancelHide() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        for (const PledgeGamePlayerInfo& info : m_MemberList) {
            CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(info.playerSID));
            if (user &&
                user->m_PledgeGameUser.m_StateMachine.GetStatus() == CPledgeGameUserStateMachine::PLEDGEGAME_PLAYER &&
                user->IsInvisibleBySkill()) {
                user->SetVisibleBySkill(true, true);
                user->GetSocket()->SendSystemMessage(SystemMessageId::INVISIBILITY_CANCELLED);
            }
        }
    }
}

bool CPledgeGamePlayers::WaitingGame() {
    L2_GUARD_FUNCTION();

    {
        L2_GUARD(m_Lock);
        for (const PledgeGamePlayerInfo& info : m_MemberList) {
            CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(info.playerSID));
            if (!user || !user->m_PledgeGameUser.WaitingPledgeGamePlaying()) {
                continue;
            }

            // Current 0x009aa324 performs this second state change even though
            // WaitingPledgeGamePlaying has just moved the user to the same state.
            user->m_PledgeGameUser.m_StateMachine.ChangeState(CPledgeGameUserStateMachine::PLEDGEGAME_PLAY_WAITING);
            g_PledgeGameSystem.GiveItem(user.get(), 0x8c97, 1, false);
            g_PledgeGameSystem.GiveItem(user.get(), 0x8c98, 1, false);
            g_PledgeGameSystem.GiveItem(user.get(), 0x8c99, 1, false);
        }
    }
    return true;
}

void CPledgeGamePlayers::SetPlayer(const int fieldId, CInstantZone* const instantZone) {
    L2_GUARD_FUNCTION();

    std::vector<CSPointer<User>> invalidUsers;
    {
        L2_GUARD(m_Lock);
        for (const PledgeGamePlayerInfo& info : m_MemberList) {
            CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(info.playerSID));
            if (!user) {
                continue;
            }

            std::int32_t reason = 0;
            if (!g_PledgeGameSystem.IsValidUser(user.get(), reason)) {
                invalidUsers.push_back(user);
                user->ActionFailed(reason);
                continue;
            }
            user->m_PledgeGameUser.SetPlayer(fieldId, instantZone, info.playNo);
        }
    }

    for (std::size_t index = invalidUsers.size(); index != 0; --index) {
        if (runtime::CheckedVectorAt(invalidUsers, index - 1)) {
            g_PledgeGameSystem.LeaveGame(runtime::CheckedVectorAt(invalidUsers, index - 1).get(), false);
        }
    }
}

void CPledgeGamePlayers::StartGame() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        for (const PledgeGamePlayerInfo& info : m_MemberList) {
            CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(info.playerSID);
            User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
            if (!user) {
                continue;
            }

            user->m_PledgeGameUser.m_StateMachine.ChangeState(CPledgeGameUserStateMachine::PLEDGEGAME_PLAYER);
            {
                L2_GUARD(*user->m_lock);
                user->m_data->RestoreCP();
                user->m_data->RestoreHP();
                user->m_data->RestoreMP();
            }

            CPledge* const pledge = user->GetPledge();
            if (pledge != nullptr) {
                const auto matchingPoint = user->m_MatchingPoint.m_nMatchingPointTotal[0];
                const std::int64_t fieldId = m_nFieldId;
                const std::uint32_t pledgeLevel = pledge->GetPledgeLevel();
                const std::uint32_t pledgeId = pledge->GetDbId();
                const char16_t* const pledgeName = pledge->GetPledgeName();
                CLogFormatter formatter;
                formatter.WriteLog1(
                    0x7fb, user, nullptr, pledgeName, nullptr, nullptr, static_cast<std::int64_t>(pledgeId),
                    static_cast<std::int64_t>(pledgeLevel), fieldId, matchingPoint, 0, 0, 0
                );
                RelayClient::Inst()->SendEnterPledgeGame(user);
            }
        }
    }
}

void CPledgeGamePlayers::SendSummonHPMPInfo(User* const user, CSummon* const summon) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || summon == nullptr) {
        return;
    }
    user->GetSocket()->Send(
        "cddcccdcdcdcd", 0x18, static_cast<std::int32_t>(summon->m_nId), 0, 0, 4, 9,
        static_cast<std::int32_t>(summon->m_data->m_dHP), 10, static_cast<std::int32_t>(summon->m_data->m_dBaseHP), 0x0b,
        static_cast<std::int32_t>(summon->m_data->m_dMP), 0x0c, static_cast<std::int32_t>(summon->m_data->m_dBaseMP)
    );
}

void CPledgeGamePlayers::HideSummonHPMPInfo(User* const user, CSummon* const summon) {
    // Current L2Server 0x0099c1c0.
    L2_GUARD_FUNCTION();
    if (user == nullptr || summon == nullptr || user->IsMyParty(summon)) {
        return;
    }
    user->GetSocket()->Send("cddcccdcd", 0x18, static_cast<std::int32_t>(summon->m_nId), 0, 0, 2, 10, 0, 0x0c, 0);
}

void CPledgeGamePlayers::RecvSummonInfo(CSummon* const summon) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        for (const PledgeGamePlayerInfo& info : m_MemberList) {
            CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(info.playerSID));
            if (!user || summon == nullptr) {
                continue;
            }
            summon->SendNPCInfo(user.get(), false, false);
            if (!g_bSendSummonHPMPInfo) {
                SendSummonHPMPInfo(user.get(), summon);
            }
        }
    }
}

void CPledgeGamePlayers::RecvHideSummonInfo(CSummon* const summon) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        for (const PledgeGamePlayerInfo& info : m_MemberList) {
            CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(info.playerSID));
            if (user && summon != nullptr && !g_bSendSummonHPMPInfo) {
                HideSummonHPMPInfo(user.get(), summon);
            }
        }
    }
}

void CPledgeGamePlayers::SendSummonInfo(CPledgeGamePlayers& recipients) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        for (const PledgeGamePlayerInfo& info : m_MemberList) {
            CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(info.playerSID));
            if (!user) {
                continue;
            }
            FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(user, summon, recipients.RecvSummonInfo(summon););
        }
    }
}

void CPledgeGamePlayers::HideSummonInfo(CPledgeGamePlayers& recipients) {
    // Current L2Server 0x009a45a0.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        for (const PledgeGamePlayerInfo& info : m_MemberList) {
            CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(info.playerSID));
            if (!user) {
                continue;
            }
            // Recovered source used the following call, but current 0x009a45a0
            // calls GetAllSummon@00bb7adc, so the pet is excluded:
            // user->GetAllSummonAndPet(summons)
            FOR_ALL_SUMMON_IN_SUMMON_LIST(user, summon, recipients.RecvHideSummonInfo(summon););
        }
    }
}

void CPledgeGamePlayers::UpdateStatus(CPledgeGameField* const field, const int status) {
    L2_GUARD_FUNCTION();
    if (field == nullptr) {
        return;
    }
    {
        L2_GUARD(m_Lock);
        for (const PledgeGamePlayerInfo& info : m_MemberList) {
            field->AsyncUpdateUserStatus(static_cast<std::int32_t>(info.playerSID), status);
        }
    }
}

bool CPledgeGamePlayers::UpdatePlayerInfo(User* const user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || (!user->m_PledgeGameUser.IsPlayer() && !user->m_PledgeGameUser.IsRegistered())) {
        return false;
    }

    const CPledgeGameUser snapshot = user->m_PledgeGameUser;
    {
        L2_GUARD(m_Lock);
        for (PledgeGamePlayerInfo& info : m_MemberList) {
            if (info.playerSID == user->Id()) {
                info.killCount = snapshot.GetKillCount();
                info.lifeTime = snapshot.GetLifeTime();
                info.alive = snapshot.IsAlive();
                info.classId = user->GetClass();
                return true;
            }
        }
    }
    return false;
}

bool CPledgeGamePlayers::UpdateInvalidPlayerInfo(const std::int32_t userSid) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        for (PledgeGamePlayerInfo& info : m_MemberList) {
            if (static_cast<std::int32_t>(info.playerSID) != userSid) {
                continue;
            }
            info.playerSID = 0;
            info.killCount = 0;
            info.lifeTime = 0;
            info.alive = false;
            return true;
        }
    }
    return false;
}

bool CPledgeGamePlayers::IsRegisteredUser(User* const user) {
    L2_GUARD_FUNCTION();
    return user != nullptr && IsRegisteredUser(static_cast<std::int32_t>(user->m_nId));
}

bool CPledgeGamePlayers::IsRegisteredUser(const std::int32_t userSid) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        for (const PledgeGamePlayerInfo& info : m_MemberList) {
            if (static_cast<std::int32_t>(info.playerSID) == userSid) {
                return true;
            }
        }
    }
    return false;
}

bool CPledgeGamePlayers::UnRegisterUser(User* const user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    {
        L2_GUARD(m_Lock);
        for (PledgeGamePlayerInfo& info : m_MemberList) {
            if (info.playerSID != user->m_nId) {
                continue;
            }
            info.playerSID = 0;
            return true;
        }
    }
    return false;
}

User* CPledgeGamePlayers::GetWinner(const bool includeDrawFallback) {
    L2_GUARD_FUNCTION();
    const auto winner = [&]() -> User* {
        L2_GUARD(m_Lock);
        User* winner = nullptr;
        if (m_MemberList.empty()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[GetWinner][1] pWinner is not valid")
            // Noise: An empty clan-game member list has no winner. Reporting a null winner here does not identify an
            // operational failure. L2_LOG_ERROR(Log,
            //     L"[GetWinner][1] pWinner is not valid")
        } else if (m_MemberList.size() == 1) {
            const PledgeGamePlayerInfo& info = m_MemberList.front();
            if (!includeDrawFallback || info.playerIndex == 5) {
                CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(info.playerSID));
                winner = user.get();
                if (winner == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[GetWinner][2] pWinner is not valid")
                    // L2_LOG_ERROR(Log,
                    //     L"[GetWinner][2] pWinner is not valid")
                    L2_LOG_WARN(
                        Log, "The only clan-game winner candidate, object %u, is unavailable; no winner is returned.",
                        static_cast<unsigned>(info.playerSID)
                    );
                }
            }
        } else {
            winner = _GetLiveLongUser();
            if (winner == nullptr) {
                winner = _GetBestKillUser();
            }
        }
        return winner;
    }();
    return winner;
}

void CPledgeGamePlayers::InitShuffleNo() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        _SetPlayersCount();
        for (const PledgeGamePlayerInfo& info : m_MemberList) {
            CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(info.playerSID));
            if (user) {
                user->m_PledgeGameUser.InitShuffleNo(m_nMemberCount);
            }
        }
    }
}

void CPledgeGamePlayers::Freeze() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        for (const PledgeGamePlayerInfo& info : m_MemberList) {
            CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(std::uint32_t(info.playerSID));
            User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
            if (!user) {
                continue;
            }
            user->SetVisible(false);
            user->StopMove();
            user->DoNothing();
            FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
                user, summon, summon->SetVisible(false); summon->StopMove(); summon->DoNothing();
            );
        }
    }
}

void CPledgeGamePlayers::ResetFreeze(User* const user, const bool) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    user->SetVisible(true);
    user->DoNothing();
    FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(user, summon, summon->SetVisible(true); summon->DoNothing(););
}

void CPledgeGamePlayers::ResetFreeze(const bool activateSurvivorSkill) {
    L2_GUARD_FUNCTION();
    for (const PledgeGamePlayerInfo& info : m_MemberList) {
        CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(info.playerSID));
        ResetFreeze(user.get(), activateSurvivorSkill);
    }
}

void CPledgeGamePlayers::Draw(PledgeGameInfo&) { L2_GUARD_FUNCTION(); }

void CPledgeGamePlayers::Win(User* const winner, PledgeGameInfo&) {
    L2_GUARD_FUNCTION();
    if (winner == nullptr) {
        return;
    }
    winner->m_PledgeGameUser.m_nGameResult = PledgeGame::GRT_WIN;
    if (CPledgeGameField* const field = winner->m_PledgeGameUser.GetField()) {
        winner->m_PledgeGameUser.SetLifeTime(field->m_tNow - field->m_nBeginTime);
        UpdatePlayerInfo(winner);
    }
}

User* CPledgeGamePlayers::GetBestKiller() {
    L2_GUARD_FUNCTION();
    std::int32_t bestKills = 0;
    std::int32_t bestSid = 0;
    {
        L2_GUARD(m_Lock);
        for (const PledgeGamePlayerInfo& info : m_MemberList) {
            if (info.killCount > 0 && info.killCount == bestKills) {
                bestSid = 0;
            } else if (info.killCount > bestKills) {
                bestKills = info.killCount;
                bestSid = static_cast<std::int32_t>(info.playerSID);
            }
        }
    }
    CSPointer<User> user = g_UserMap.FindObject(bestSid);
    return user.get();
}

std::int32_t CPledgeGamePlayers::GetPlayersCount() const {
    L2_GUARD_FUNCTION();
    return m_nMemberCount;
}

void CPledgeGamePlayers::SendInformationPage() {
    L2_GUARD_FUNCTION();

    std::optional<std::u16string> htmlStorage;
    CSPointer<User> failedUser;
    {
        L2_GUARD(m_Lock);
        std::u16string& html = htmlStorage.emplace();
        bool loaded = false;
        for (const PledgeGamePlayerInfo& info : m_MemberList) {
            CSPointer<User> user = g_UserMap.FindObject(static_cast<std::int32_t>(info.playerSID));
            if (!user) {
                continue;
            }

            if (!loaded) {
                loaded = g_HTMLCache.GetHTMLFile(u"grankain_lumiere011.htm", html, user->m_data->m_CountryCode);
                if (!loaded) {
                    continue;
                }
                if (html.size() > 0x1ce8U) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d][%s] html.size > 3700 * 2",
                    //     L"PledgeGameSystem.cpp",
                    //     0x93a,
                    //     L"CPledgeGamePlayers::SendInformationPage")
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d][%s] html.size > 3700 * 2",
                    //     L"PledgeGameSystem.cpp",
                    //     0x93a,
                    //     L"CPledgeGamePlayers::SendInformationPage")
                    L2_LOG_ERROR(
                        Log,
                        "Clan-game HTML 'grankain_lumiere011.htm' contains %llu UTF-16 code units, above the 7400-unit "
                        "limit; the information-page broadcast stops.",
                        static_cast<unsigned long long>(html.size())
                    );
                    failedUser = std::move(user);
                    return;
                }
            }

            const std::u16string hostHtml(runtime::StringView(html));
            user->ShowHTML(nullptr, hostHtml.c_str(), 0, HTML::LAUNCH, true);
        }
    }
}

void CPledgeGamePlayers::SendWinnerName(User* const winner) {
    L2_GUARD_FUNCTION();
    if (winner == nullptr) {
        return;
    }
    {
        L2_GUARD(m_Lock);
        for (const PledgeGamePlayerInfo& info : m_MemberList) {
            CSPointer<User> user = g_UserMap.FindObject(static_cast<std::int32_t>(info.playerSID));
            if (!user) {
                continue;
            }
            const auto state = user->m_PledgeGameUser.m_StateMachine.GetStatus();
            if (!g_PledgeGameSystem.IsRegisteredUser(user.get()) &&
                state != CPledgeGameUserStateMachine::PLEDGEGAME_PLAYER &&
                state != CPledgeGameUserStateMachine::PLEDGEGAME_PLAY_WAITING) {
                continue;
            }
            if (CUserSocket* const socket = user->GetSocket()) {
                socket->Send("chccS", 0x62, SystemMessageId::CHARACTER_WON_MATCH, 1, 0x0c, winner->GetName2(user.get()));
            }
        }
    }
}

void CPledgeGamePlayers::SendMatchResult(User* const winner) {
    L2_GUARD_FUNCTION();
    std::array<char, 0x5000> packet{};
    std::int32_t packetLength = 0;
    std::int32_t entryCount = 0;

    {
        L2_GUARD(m_Lock);
        for (const PledgeGamePlayerInfo& info : m_MemberList) {
            if (info.playerSID == 0) {
                continue;
            }
            CSPointer<User> user = g_UserMap.FindObject(static_cast<std::int32_t>(info.playerSID));
            std::int32_t classId = info.classId;
            bool activePlayer = false;
            if (user) {
                const auto state = user->m_PledgeGameUser.m_StateMachine.GetStatus();
                if (state == CPledgeGameUserStateMachine::PLEDGEGAME_PLAYER ||
                    state == CPledgeGameUserStateMachine::PLEDGEGAME_PLAY_WAITING) {
                    classId = user->GetClass();
                    activePlayer = true;
                }
            }
            packetLength += ::Assemble(
                packet.data() + packetLength, static_cast<int>(packet.size()) - packetLength, "ddddd",
                static_cast<std::int32_t>(info.playerSID), info.playNo, classId, info.lifeTime, info.killCount
            );
            ++entryCount;
            if (activePlayer && info.alive) {
                user->PledgeMissionCheckAndAddCount(PledgeMission::PMGT_PLEDGE_GAME_SURVIVE, 0, 1);
            }
        }

        for (const PledgeGamePlayerInfo& info : m_MemberList) {
            CSPointer<User> user = g_UserMap.FindObject(static_cast<std::int32_t>(info.playerSID));
            if (!user) {
                continue;
            }
            const auto state = user->m_PledgeGameUser.m_StateMachine.GetStatus();
            if (state != CPledgeGameUserStateMachine::PLEDGEGAME_PLAYER &&
                state != CPledgeGameUserStateMachine::PLEDGEGAME_PLAY_WAITING) {
                continue;
            }

            std::int32_t result = 0;
            if (winner == nullptr) {
                if (m_nMemberCount == 1) {
                    result = 2;
                }
            } else {
                result = winner == user.get() ? 1 : 2;
            }
            user->GetSocket()->Send(
                "chdhddb", 0xfe, 0x125, user->m_PledgeGameUser.m_nShuffleNo, result, m_nMemberCount, entryCount,
                packetLength, packet.data()
            );

            CPledge* const pledge = user->GetPledge();
            if (pledge != nullptr) {
                const std::int64_t matchingPoint = g_MatchingPointDB.ValidateMatchingPoint(
                    MatchingPoint::MPT_BASE, info.nMatchingPointBase, info.nMatchingPointDiff, info.nWinningPoint
                );
                CLogFormatter resultStartFormatter{};
                const std::int64_t pledgeLevel = pledge->GetPledgeLevel();
                const std::int64_t pledgeDbId = pledge->GetDbId();
                const char16_t* const pledgeName = pledge->GetPledgeName();
                resultStartFormatter.WriteLog1(
                    0x7fc, user.get(), nullptr, pledgeName, nullptr, nullptr, pledgeDbId, pledgeLevel, info.lifeTime,
                    info.killCount, m_nFieldId, info.alive, 0
                );
                user->PledgeMissionCheckAndAddCount(PledgeMission::PMGT_PLEDGE_GAME_JOIN, 0, 1);

                if (result == 0) {
                    CLogFormatter resultFormatter{};
                    const std::int64_t resultPledgeLevel = pledge->GetPledgeLevel();
                    const std::int64_t resultPledgeDbId = pledge->GetDbId();
                    const char16_t* const resultPledgeName = pledge->GetPledgeName();
                    resultFormatter.WriteLog1(
                        0x7fd, user.get(), nullptr, resultPledgeName, nullptr, nullptr, resultPledgeDbId, resultPledgeLevel,
                        0, m_nFieldId, info.nWinningPoint, matchingPoint, 0
                    );
                } else if (result == 1) {
                    user->PledgeMissionCheckAndAddCount(PledgeMission::PMGT_PLEDGE_GAME_WIN, 0, 1);
                    CLogFormatter resultFormatter{};
                    const std::int64_t resultPledgeLevel = pledge->GetPledgeLevel();
                    const std::int64_t resultPledgeDbId = pledge->GetDbId();
                    const char16_t* const resultPledgeName = pledge->GetPledgeName();
                    resultFormatter.WriteLog1(
                        0x7fd, user.get(), nullptr, resultPledgeName, nullptr, nullptr, resultPledgeDbId, resultPledgeLevel,
                        1, m_nFieldId, info.nWinningPoint, matchingPoint, 0
                    );
                } else if (result == 2) {
                    CLogFormatter resultFormatter{};
                    const std::int64_t resultPledgeLevel = pledge->GetPledgeLevel();
                    const std::int64_t resultPledgeDbId = pledge->GetDbId();
                    const char16_t* const resultPledgeName = pledge->GetPledgeName();
                    resultFormatter.WriteLog1(
                        0x7fd, user.get(), nullptr, resultPledgeName, nullptr, nullptr, resultPledgeDbId, resultPledgeLevel,
                        2, m_nFieldId, info.nWinningPoint, matchingPoint, 0
                    );
                }
            }
        }
    }
}

void CPledgeGamePlayers::UnregisterAll() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        m_MemberList.clear();
    }
}

void CPledgeGamePlayers::FinishGame(const bool sendResult) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        for (const PledgeGamePlayerInfo& info : m_MemberList) {
            CSPointer<User> user = g_UserMap.FindObject(static_cast<std::int32_t>(info.playerSID));
            if (!user) {
                continue;
            }
            const auto state = user->m_PledgeGameUser.m_StateMachine.GetStatus();
            if (state != CPledgeGameUserStateMachine::PLEDGEGAME_PLAYER &&
                state != CPledgeGameUserStateMachine::PLEDGEGAME_PLAY_WAITING) {
                continue;
            }

            if (user->m_PledgeGameUser.m_nGameResult != PledgeGame::GRT_WIN && g_PledgeGameSystem.m_nLosePrizeItemType > 0) {
                g_PledgeGameSystem.GiveItem(
                    user.get(), g_PledgeGameSystem.m_nLosePrizeItemType, g_PledgeGameSystem.m_nLosePrizeItemCount, true
                );
            }

            if (user->HaveMemo(0x29fc) || user->HaveMemo(0x29fd) || user->HaveMemo(0x2a3d) || user->HaveMemo(0x2a43)) {
                g_npcserver.Send(
                    "cdd", 0xbb, static_cast<std::int32_t>(user->GetServerId()),
                    static_cast<std::int32_t>(user->m_PledgeGameUser.m_nGameResult)
                );
            }

            user->m_PledgeGameUser.ActivateSkillForEveryUser();
            if (info.alive) {
                user->m_PledgeGameUser.ActivateSkillForSurvivor();
            }
            if (sendResult) {
                user->m_PledgeGameUser.SetAlive(true);
            }
            CPledge* const pledge = user->GetPledge();
            if (pledge != nullptr) {
                const std::int64_t matchingPoint = g_MatchingPointDB.ValidateMatchingPoint(
                    MatchingPoint::MPT_BASE, info.nMatchingPointBase, info.nMatchingPointDiff, info.nWinningPoint
                );
                CLogFormatter formatter{};
                const std::int64_t pledgeLevel = pledge->GetPledgeLevel();
                const std::int64_t pledgeDbId = pledge->GetDbId();
                const char16_t* const pledgeName = pledge->GetPledgeName();
                formatter.WriteLog1(
                    0x7ff, user.get(), nullptr, pledgeName, nullptr, nullptr, pledgeDbId, pledgeLevel, m_nFieldId,
                    info.nWinningPoint, matchingPoint, 0, 0
                );
            }
            g_ToDoListManager.OnDailyMissionCheckAndUpdate(user.get(), E_DAILY_MISSION_TYPE_PLEDGEGAME);
        }
    }

    User* const bestKiller = GetBestKiller();
    if (bestKiller != nullptr && bestKiller != GetWinner(false)) {
        g_PledgeGameSystem.GiveItem(bestKiller, 0x8c8e, 1, false);
        bestKiller->PledgeMissionCheckAndAddCount(PledgeMission::PMGT_PLEDGE_GAME_ITEM, 0, 1);
    }
}

void CPledgeGamePlayers::EscapeFromPledgeGame() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        for (PledgeGamePlayerInfo& info : m_MemberList) {
            info.alive = false;
            CSPointer<User> user = g_UserMap.FindObject(static_cast<std::int32_t>(info.playerSID));
            if (user) {
                ResetFreeze(user.get(), false);
                user->m_PledgeGameUser.EscapeFromPledgeGame();
            }
            info.playerSID = 0;
        }
        UnregisterAll();
    }
}

CPledgeGameHtml::CPledgeGameHtml() : m_Html{} { m_mapMatchList.clear(); }

CPledgeGameHtml::~CPledgeGameHtml() = default;

void CPledgeGameHtml::Init() {
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(lockMapMatchList);
        m_mapMatchList.clear();
    }
}

void CPledgeGameHtml::OnGameChanged(const std::int32_t fieldId) {
    L2_GUARD_FUNCTION();
    SharedPledgeGameInfo* const shared = SharedPledgeGameInfo::GetInfo();
    if (fieldId < 0 || fieldId >= 6) {
        return;
    }

    const sPledgeGameFieldInfo* const field = &shared->arrFieldInfo[fieldId];
    {
        L2_WRITE_GUARD(lockMapMatchList);
        auto position = m_mapMatchList.find(fieldId);
        if (position == m_mapMatchList.end()) {
            if (field->fieldStatus != PGFIELD_NONE) {
                m_mapMatchList.emplace(fieldId, field);
            }
        } else if (field->fieldStatus == PGFIELD_NONE) {
            m_mapMatchList.erase(position);
        } else {
            position->second = field;
        }
    }
}

void CPledgeGameHtml::SendApplyPage(User* const user) {
    // Current L2Server 0x0099f200.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    std::u16string html;
    if (!g_HTMLCache.GetHTMLFile(u"grankain_invitation001.htm", html, user->m_data->m_CountryCode)) {
        return;
    }
    if (html.size() > 0x1ce8U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][%s] html.size > 3700 * 2",
        //     L"PledgeGameSystem.cpp",
        //     0x19e4,
        //     L"CPledgeGameHtml::SendApplyPage")
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][%s] html.size > 3700 * 2",
        //     L"PledgeGameSystem.cpp",
        //     0x19e4,
        //     L"CPledgeGameHtml::SendApplyPage")
        L2_LOG_ERROR(
            Log,
            "Clan-game HTML 'grankain_invitation001.htm' contains %llu UTF-16 code units, above the 7400-unit limit; the "
            "application page is not sent.",
            static_cast<unsigned long long>(html.size())
        );
        return;
    }

    const std::u16string hostHtml(runtime::StringView(html));
    user->ShowHTML(nullptr, hostHtml.c_str(), 0, HTML::LAUNCH, false);
}

void CPledgeGameHtml::SendCancelPage(User* const user) {
    // Current L2Server 0x0099d758 is a guarded no-op.
    L2_GUARD_FUNCTION();
    static_cast<void>(user);
}

void CPledgeGameHtml::SendGameList(User* const user) {
    // Current L2Server 0x009a2e68.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    const std::int32_t step = SharedPledgeGameInfo::GetInfo()->m_nStep;
    if (step < PledgeGameCondition::GAME_PREPARE || step > PledgeGameCondition::GAME_ENDED) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::OUTSIDE_CEREMONY_OF_CHAOS_MATCH_PERIOD);
        return;
    }

    CPacketHelperEx<20480> packet;
    {
        L2_READ_GUARD(lockMapMatchList);
        for (const auto& entry : m_mapMatchList) {
            const std::int32_t fieldId = entry.first;
            const sPledgeGameFieldInfo* const info = entry.second;
            if (info == nullptr) {
                continue;
            }

            std::int32_t aliveCount = 0;
            if (static_cast<std::uint32_t>(fieldId) < 6U) {
                aliveCount = g_PledgeGameSystem.m_pPledgeGameFieldList[fieldId].m_Players.GetAliveUserCount();
            }
            const std::u16string displayId = ::Utf8ToUtf16(std::to_string(fieldId + 1));
            packet.Assemble("dShd", fieldId, displayId.c_str(), static_cast<int>(info->fieldStatus) - 1, aliveCount);
        }
        user->GetSocket()->Send("chdb", 0xfe, 0x126, packet.GetAssembledCount(), packet.GetLength(), packet.GetPacket());
    }
}

void CPledgeGameHtmlCommandHandler::MoveObserverField(CSocket* const socket, const std::int32_t fieldId) {
    // Current L2Server 0x009a9604.
    L2_GUARD_FUNCTION();
    if (static_cast<std::uint32_t>(fieldId) >= 6U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][%s] Hack? nFieldId(%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x1aa5,
        //     L"CPledgeGameHtmlCommandHandler::MoveObserverField",
        //     fieldId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][%s] Hack? nFieldId(%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x1aa5,
        //     L"CPledgeGameHtmlCommandHandler::MoveObserverField",
        //     fieldId)
        L2_LOG_WARN(Log, "Clan-game observer movement rejected: field index %d is outside 0 through 5.", fieldId);
        return;
    }

    CUserSocket* const userSocket = socket != nullptr ? static_cast<CUserSocket*>(socket) : nullptr;
    User* const user = userSocket != nullptr ? userSocket->GetUser() : nullptr;
    if (user == nullptr ||
        user->m_PledgeGameUser.m_StateMachine.GetStatus() != CPledgeGameUserStateMachine::PLEDGEGAME_OBSERVER ||
        !user->IsActive()) {
        return;
    }

    CPledgeGameField* const currentField = user->m_PledgeGameUser.GetField();
    if (currentField != nullptr && fieldId != currentField->m_nFieldId) {
        currentField->RegisterMessage(8, static_cast<std::int32_t>(user->GetServerId()), fieldId, false);
    }
}

void CPledgeGameHtmlCommandHandler::Handle(CSocket* const socket, const char16_t* const command) {
    // Current L2Server 0x009ae7d8.
    L2_GUARD_FUNCTION();
    const CHTMLActionParamParser parameters = CHTMLActionParamParser::Parse(command);
    const char16_t* const action = parameters.GetStrValue(u"command");
    CUserSocket* const userSocket = socket != nullptr ? static_cast<CUserSocket*>(socket) : nullptr;
    User* const user = userSocket != nullptr ? userSocket->GetUser() : nullptr;
    if (action == nullptr || user == nullptr) {
        return;
    }

    if (target_utf16::CompareN(action, u"apply", 5) == 0) {
        if (!g_PledgeGameSystem.RegisterEntry(user)) {
            user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_REGISTER_AS_PARTICIPANT);
        }
        return;
    }

    if (target_utf16::CompareN(action, u"move_op_field", 12) == 0) {
        if (user->m_bOpenedNpcStoreUI) {
            user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_OBSERVE_DURING_COMBAT);
            return;
        }

        const std::uint32_t fieldId = static_cast<std::uint32_t>(parameters.GetIntValue(u"field") - 1);
        if (user->m_PledgeGameUser.m_StateMachine.GetStatus() == CPledgeGameUserStateMachine::PLEDGEGAME_OBSERVER) {
            MoveObserverField(socket, static_cast<std::int32_t>(fieldId));
        } else if (fieldId < 6U) {
            CPledgeGameField* const field = g_PledgeGameSystem.m_pPledgeGameFieldList + fieldId;
            if (field != nullptr) {
                field->AddObserver(static_cast<std::int32_t>(user->GetServerId()));
            }
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] invalid field id(%d)",
            //     L"PledgeGameSystem.cpp",
            //     0x1a8b,
            //     fieldId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] invalid field id(%d)",
            //     L"PledgeGameSystem.cpp",
            //     0x1a8b,
            //     fieldId)
            L2_LOG_WARN(
                Log, "Clan-game observation request rejected: converted field index %u is outside 0 through 5.",
                static_cast<unsigned>(fieldId)
            );
        }
        return;
    }

    if (target_utf16::CompareN(action, u"op_field_list", 13) == 0) {
        g_PledgeGameSystem.m_pledgeGameHtml.SendGameList(user);
    }
}

CPledgeGameField::CPledgeGameField() :
    m_bIsUnRegist(false),
    m_alignment019To01c{},
    m_nStatus(PledgeGameFieldStatus::NO_MATCH),
    m_nMessageRegistered(0),
    m_tStatusChanged(0),
    m_tPrev(0),
    m_tNow(0),
    m_nLoserState(0),
    m_bBeginTimeSet(false),
    m_alignment045To048{},
    m_nFieldId(-1),
    m_alignment04cTo050{},
    m_ObserverEntry(),
    m_bCheck1SecElapsed(false),
    m_alignment0b1To0b4{},
    m_nLivingCount(0),
    m_GameValidCondition{},
    m_pTerritory(nullptr),
    m_PlayerStartPos{},
    m_nBeginTime(0),
    m_bFastTesting(false),
    m_bOnePlayerMode(false),
    m_bNpcRelated(false),
    m_alignment117{},
    m_pInZone(nullptr) {
    L2_GUARD_FUNCTION();
}

CPledgeGameField::~CPledgeGameField() {
    m_pTerritory = nullptr;
    m_pInZone = nullptr;
}

void CPledgeGameField::Init(const std::int32_t fieldId) {
    L2_GUARD_FUNCTION();
    m_nFieldId = fieldId;
    m_Players.m_nFieldId = fieldId;

    std::int32_t typeId = 0;
    std::int32_t zoneId = 0;
    if (m_pInZone != nullptr) {
        typeId = m_pInZone->GetInstantZoneTypeID();
        zoneId = static_cast<std::int32_t>(m_pInZone->Id());
    }
    if (static_cast<std::uint32_t>(fieldId) < 6U) {
        sPledgeGameFieldInfo& field = SharedPledgeGameInfo::GetInfo()->arrFieldInfo[fieldId];
        field.fieldStatus = PGFIELD_NONE;
        field.nInzoneTypeId = typeId;
        field.nInzoneSId = zoneId;
    }
    m_tNow = 0;
}

void CPledgeGameField::DeleteNPCFromCreatureMap() {
    L2_GUARD_FUNCTION();
    if (m_pInZone != nullptr && !m_pInZone->IsDeleting() && m_pInZone->m_Deleted == 0) {
        m_pInZone->DeleteNPCNotRelatedToPCFromCreatureMap();
    }
}

void CPledgeGameField::EscapeAllObserver() {
    L2_GUARD_FUNCTION();
    m_ObserverEntry.RemoveEntryAll(m_nFieldId);
}

void CPledgeGameField::TimerExpired(const int id) {
    L2_GUARD_FUNCTION();

    if (m_nStatus == PledgeGameFieldStatus::NO_MATCH) {
        return;
    }
    if (id == 0x3ea) {
        RestorePlayers();
        return;
    }

    m_tPrev = m_tNow;
    m_tNow = runtime::crt::Time32Now();
    m_bCheck1SecElapsed = m_tPrev != m_tNow;
    const std::int32_t elapsed = m_tNow - m_tStatusChanged;
    const std::int64_t interval =
        m_nStatus >= PledgeGameFieldStatus::NO_UNRESIST_MATCH && m_nStatus <= PledgeGameFieldStatus::PROCESS_MATCH ? 200
                                                                                                                   : 1000;

    ProcessMessage();
    switch (m_nStatus) {
        case PledgeGameFieldStatus::PREPARE_MATCH:
            StatusPrepareMatch(elapsed);
            break;
        case PledgeGameFieldStatus::WAIT_MATCH:
            StatusWaitMatch(elapsed);
            break;
        case PledgeGameFieldStatus::START_MATCH:
            StatusStartMatch();
            break;
        case PledgeGameFieldStatus::PROCESS_MATCH:
            StatusProcessMatch(elapsed);
            break;
        case PledgeGameFieldStatus::FINISH_MATCH:
            StatusFinishMatch();
            break;
        case PledgeGameFieldStatus::CLEAR_MATCH:
            StatusClearMatch(elapsed);
            break;
        default:
            break;
    }

    if (m_nStatus != PledgeGameFieldStatus::NO_MATCH) {
        AddTimer(interval, 0);
    }
}

void CPledgeGameField::RestorePlayers() {
    L2_GUARD_FUNCTION();
    m_Players.RestorePlayers();
}

void CPledgeGameField::RegisterMessage(
    const std::uint8_t message, const std::int32_t parameter1, const std::int32_t parameter2, const bool flag
) {
    L2_GUARD_FUNCTION();
    if (m_nFieldId < 0) {
        return;
    }

    EntryMessage entry{};
    entry.byMessage = message;
    entry.nParam1 = parameter1;
    entry.nParam2 = parameter2;
    entry.bParam = flag;
    {
        L2_GUARD(m_MessageQueueLock);
        m_MessageQueue.push_back(entry);
    }
    runtime::raw_atomic::ExchangeI32(&m_nMessageRegistered, 1);
}

void CPledgeGameField::PlayerDied(User* const user, CCreature* const killer) {
    // Current L2Server 0x009a8a14.
    L2_GUARD_FUNCTION();

    if (user == nullptr) {
        return;
    }

    L2_GUARD(m_StatusLock);
    if (m_nStatus != PledgeGameFieldStatus::PROCESS_MATCH || !m_Players.IsRegisteredUser(user) ||
        !user->m_PledgeGameUser.m_IsAlive) {
        return;
    }

    user->m_PledgeGameUser.SetAlive(false);
    user->m_PledgeGameUser.SetLifeTime(m_tNow - m_nBeginTime);
    m_Players.UpdatePlayerInfo(user);

    if (!user->IsGMInvisible() && g_ptlUserMap != nullptr) {
        g_ptlUserMap->BroadcastToNeighbor(
            *user, 0x80000, user->GetPos(), "cddddddddddcddb", 0, user->Id(), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, nullptr
        );
    }

    User* killerUser = nullptr;
    if (killer != nullptr) {
        if (killer->IsSummonOrPet()) {
            CNPC* const summon = killer->CastNPC();
            killerUser = summon != nullptr ? summon->GetMaster() : nullptr;
        } else if (killer->IsPC()) {
            killerUser = killer->CastUser();
        } else if (killer->IsNPC()) {
            CNPC* const npc = killer->CastNPC();
            if (npc == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] CastNPC Fail",
                //     L"PledgeGameSystem.cpp",
                //     0x1006)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] CastNPC Fail",
                //     L"PledgeGameSystem.cpp",
                //     0x1006)
                L2_LOG_ERROR(
                    Log, "Clan-game death processing stopped: the killer reports NPC type but cannot be cast to an NPC."
                );
                return;
            }

            CPledge* const victimPledge = user->GetPledge();
            if (victimPledge != nullptr) {
                std::array<char16_t, 24> npcName{};
                const char16_t* const sourceName = npc->GetName();
                if (sourceName != nullptr) {
                    std::u16string_view(sourceName).copy(npcName.data(), npcName.size() - 1);
                }
                std::int64_t matchingPoint = g_MatchingPointDB.GuessMatchingPoint(
                    MatchingPoint::MPT_BASE, PledgeGame::GRT_BASE, user->m_MatchingPoint
                );
                matchingPoint = g_MatchingPointDB.ValidateMatchingPoint(
                    MatchingPoint::MPT_BASE, user->m_MatchingPoint.m_nMatchingPointBase[0],
                    user->m_MatchingPoint.m_nMatchingPointDiff[0], matchingPoint
                );

                CLogFormatter formatter{};
                formatter.WriteLog1(
                    0x07fe, user, nullptr, victimPledge->GetPledgeName(), npcName.data(), nullptr, victimPledge->GetDbId(),
                    victimPledge->GetPledgeLevel(), m_nFieldId, matchingPoint, 0, 0, 0
                );
            }
        }

        if (killerUser != nullptr && m_Players.IsRegisteredUser(killerUser)) {
            ++killerUser->m_PledgeGameUser.m_nKillCount;
            m_Players.UpdatePlayerInfo(killerUser);

            CPledge* const victimPledge = user->GetPledge();
            CPledge* const killerPledge = killerUser->GetPledge();
            if (victimPledge != nullptr && killerPledge != nullptr) {
                std::int64_t matchingPoint = g_MatchingPointDB.GuessMatchingPoint(
                    MatchingPoint::MPT_BASE, PledgeGame::GRT_BASE, user->m_MatchingPoint
                );
                matchingPoint = g_MatchingPointDB.ValidateMatchingPoint(
                    MatchingPoint::MPT_BASE, user->m_MatchingPoint.m_nMatchingPointBase[0],
                    user->m_MatchingPoint.m_nMatchingPointDiff[0], matchingPoint
                );

                CLogFormatter formatter{};
                formatter.WriteLog1(
                    0x07fe, user, killerUser, victimPledge->GetPledgeName(), killerPledge->GetPledgeName(), nullptr,
                    victimPledge->GetDbId(), victimPledge->GetPledgeLevel(), killerPledge->GetDbId(),
                    killerPledge->GetPledgeLevel(), m_nFieldId, matchingPoint, 0
                );
            }
        }
    }

    if (m_Players.IsAllUserOutExceptOne() || m_Players.NoOneAlive()) {
        ChangeStatus(PledgeGameFieldStatus::FINISH_MATCH, false, false);
    }
    user->DespawnAllSummon(true, false);
    user->DespawnPet(true, true, DESPAWN_PET_NONE, false);
    user->m_PledgeGameUser.ObserverHide(true);
    if (CUserSocket* const socket = user->GetSocket()) {
        socket->Send("cd", 1, user->Id());
    }
    m_Players.SendMatchMemberList(m_ObserverEntry, static_cast<std::int32_t>(user->Id()));
}

bool CPledgeGameField::IncreaseKillCountByKillingNPC(const std::int32_t userSid) {
    // Current L2Server 0x009a6e70.
    L2_GUARD_FUNCTION();

    CSPointer<User> user = g_UserMap.FindObject(userSid);
    if (!user || !m_bOnePlayerMode) {
        return false;
    }

    ++user->m_PledgeGameUser.m_nKillCount;
    user->m_PledgeGameUser.SetLifeTime(m_tNow - m_nBeginTime);
    m_Players.UpdatePlayerInfo(user.get());
    if (user->m_PledgeGameUser.m_nKillCount == 5) {
        ChangeStatus(PledgeGameFieldStatus::FINISH_MATCH, false, false);
    }
    return true;
}

void CPledgeGameField::ProcessMessage() {
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::CompareExchangeI32(&m_nMessageRegistered, 0, 1) != 1) {
        return;
    }

    for (;;) {
        std::optional<EntryMessage> entryStorage;
        {
            L2_GUARD(m_MessageQueueLock);
            if (m_MessageQueue.empty()) {
                break;
            }
            entryStorage.emplace(m_MessageQueue.front());
            m_MessageQueue.pop_front();
        }
        const EntryMessage& entry = *entryStorage;

        switch (entry.byMessage) {
            case 2:
                AsyncAddObserver(entry.nParam1);
                break;
            case 3:
                AsyncEscapePlayer(entry.nParam1);
                break;
            case 4:
                AsyncEscapeObserver(entry.nParam1);
                break;
            case 6:
                AsyncUpdateUserStatus(entry.nParam1, 0);
                break;
            case 8:
                AsyncChangeObserverField(entry.nParam1, entry.nParam2);
                break;
            case 9:
                AsyncUpdateAllPlayers(entry.nParam1);
                break;
            default:
                break;
        }
    }
}

bool CPledgeGameField::AsyncAddObserver(const std::int32_t userSid) {
    L2_GUARD_FUNCTION();
    CSPointer<User> user = g_UserMap.FindObject(userSid);
    if (!user) {
        return false;
    }
    if (m_pInZone == nullptr || m_pInZone->IsDeleting() || m_pInZone->m_Deleted != 0) {
        user->m_PledgeGameUser.PledgeGameStatusReset();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] there's no inzone (%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x1081,
        //     m_nFieldId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] there's no inzone (%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x1081,
        //     m_nFieldId)
        L2_LOG_WARN(
            Log,
            "Cannot add observer object %u to clan-game field %d: the instance is missing or being deleted; a user-state "
            "reset was attempted.",
            static_cast<unsigned>(userSid), m_nFieldId
        );
        return false;
    }
    if (!m_ObserverEntry.AddEntry(userSid)) {
        user->m_PledgeGameUser.PledgeGameStatusReset();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] already exists id(%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x1097,
        //     userSid)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] already exists id(%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x1097,
        //     userSid)
        L2_LOG_WARN(
            Log,
            "Observer object %u is already registered for clan-game field %d; another registration is rejected after "
            "attempting a user-state reset.",
            static_cast<unsigned>(userSid), m_nFieldId
        );
        return false;
    }
    if (!user->m_PledgeGameUser.SetObserver(m_nFieldId, m_pInZone)) {
        user->m_PledgeGameUser.PledgeGameStatusReset();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] already exists id(%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x108d,
        //     userSid)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] already exists id(%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x108d,
        //     userSid)
        L2_LOG_ERROR(
            Log,
            "Cannot set observer state for object %u in clan-game field %d; a user-state reset was attempted, but the "
            "observer entry remains and member-list sending continues.",
            static_cast<unsigned>(userSid), m_nFieldId
        );
    }
    m_Players.SendMatchMemberList(m_ObserverEntry, userSid);
    return true;
}

bool CPledgeGameField::AddObserver(const std::int32_t userSid) {
    L2_GUARD_FUNCTION();
    CSPointer<User> user = g_UserMap.FindObject(userSid);
    if (!user) {
        return true;
    }

    static_cast<void>(CCursedWeaponMgr::inst());
    if (user->m_nCursedWeaponClassId > 0) {
        return false;
    }
    if (user->IsNowTrade()) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::OLYMPIAD_PARTICIPANT_CANNOT_OBSERVE);
        return true;
    }
    if (user->m_data->m_bIsActive) {
        if (user->m_PledgeGameUser.WaitingPledgeGameObserving()) {
            RegisterMessage(2, userSid, 0, false);
        } else if (user->m_PledgeGameUser.m_StateMachine.GetStatus() != CPledgeGameUserStateMachine::PLEDGEGAME_OB_WAITING) {
            user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_OBSERVE_OLYMPIAD_WHILE_REGISTERED);
        }
    }
    return true;
}

void CPledgeGameField::AsyncEscapePlayer(const std::int32_t userSid) {
    L2_GUARD_FUNCTION();
    PlayerLeaving(userSid);
}

void CPledgeGameField::AsyncEscapeObserver(const std::int32_t userSid) {
    L2_GUARD_FUNCTION();
    m_ObserverEntry.RemoveEntry(userSid);
    CSPointer<User> user = g_UserMap.FindObject(userSid);
    if (user) {
        user->m_PledgeGameUser.EscapeFromPledgeGame();
    }
}

void CPledgeGameField::AsyncChangeObserverField(const std::int32_t userSid, const std::int32_t fieldId) {
    L2_GUARD_FUNCTION();
    m_ObserverEntry.RemoveEntry(userSid);
    auto* const message = new CPledgeGameObserverChangeFieldMessage(userSid, fieldId);
    message->Post(0);
}

void CPledgeGameField::PlayerLeaving(const std::int32_t userSid) {
    L2_GUARD_FUNCTION();
    CSPointer<User> user;
    {
        L2_GUARD(m_StatusLock);
        user = g_UserMap.FindObject(userSid);
        if (m_nStatus == PledgeGameFieldStatus::PREPARE_MATCH || m_nStatus == PledgeGameFieldStatus::WAIT_MATCH ||
            m_nStatus == PledgeGameFieldStatus::PROCESS_MATCH) {
            if (!user || !m_Players.IsRegisteredUser(user.get())) {
                m_Players.UpdateInvalidPlayerInfo(userSid);
            } else {
                user->m_PledgeGameUser.SetAlive(false);
                user->m_PledgeGameUser.SetLifeTime(
                    m_nStatus == PledgeGameFieldStatus::PROCESS_MATCH ? m_tNow - m_nBeginTime : 0
                );
                m_Players.UpdatePlayerInfo(user.get());
                m_Players.UnRegisterUser(user.get());
                user->m_PledgeGameUser.EscapeFromPledgeGame();
                if (m_nStatus != PledgeGameFieldStatus::PREPARE_MATCH) {
                    user->GetSocket()->SendSystemMessage(SystemMessageId::REMOVED_FROM_CEREMONY_OF_CHAOS_AFTER_FORFEIT);
                }
            }
            if (m_nStatus == PledgeGameFieldStatus::PROCESS_MATCH) {
                m_Players.SendMatchMemberList(m_ObserverEntry, 0);
                if (m_Players.IsAllUserOutExceptOne() || m_Players.NoOneAlive()) {
                    ChangeStatus(PledgeGameFieldStatus::FINISH_MATCH, false, false);
                }
            }
        }
    }
}

bool CPledgeGameField::AddEntry(User* const user) {
    // Current L2Server 0x009a7c0c.
    L2_GUARD_FUNCTION();
    return user != nullptr && m_Players.RegisterUser(user);
}

void CPledgeGameField::SendVToPlayers(const char* const format, va_list arguments) {
    // Current L2Server 0x009a2b00.
    L2_GUARD_FUNCTION();
    m_Players.SendV(format, arguments);
}

void CPledgeGameField::Broadcast(const char* const format, ...) {
    L2_GUARD_FUNCTION();
    va_list arguments;
    va_start(arguments, format);
    m_ObserverEntry.Broadcast(format, arguments);
    SendVToPlayers(format, arguments);
    va_end(arguments);
}

void CPledgeGameField::SendToPlayers(const char* const format, ...) {
    L2_GUARD_FUNCTION();
    va_list arguments;
    va_start(arguments, format);
    SendVToPlayers(format, arguments);
    va_end(arguments);
}

void CPledgeGameField::SetOnePlayerGame() {
    L2_GUARD_FUNCTION();
    m_bOnePlayerMode = m_Players.IsAllUserOutExceptOne();
}

void CPledgeGameField::SetNpcRelatedGame(const bool forceDisabled) {
    L2_GUARD_FUNCTION();
    std::int32_t random = static_cast<std::int32_t>(::genrand64_real2() * 100.0 + 1.0);
    if (forceDisabled) {
        random = 0;
    }
    m_bNpcRelated = random > 50;
}

bool CPledgeGameField::SpawnNpc(std::int32_t npcClassId, const std::int32_t count, User* const owner) {
    L2_GUARD_FUNCTION();

    if (npcClassId < 1000000) {
        npcClassId += 1000000;
    }
    const wchar_t* const ai = g_ObjectDB.GetAI(npcClassId);
    if (ai == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][PledgeGameSystem]AI of NPC(%d) doesn't exist",
        //     L"PledgeGameSystem.cpp",
        //     0xe70,
        //     npcClassId - 1000000)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][PledgeGameSystem]AI of NPC(%d) doesn't exist",
        //     L"PledgeGameSystem.cpp",
        //     0xe70,
        //     npcClassId - 1000000)
        L2_LOG_ERROR(Log, "Cannot spawn NPC class %d in clan-game field %d: no AI is registered.", npcClassId, m_nFieldId);
        return false;
    }

    FVector position{};
    if (owner != nullptr) {
        position = owner->GetPos();
    }
    const char16_t* const npcName = g_ObjectDB.GetName(npcClassId);
    if (npcName == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][PledgeGameSystem]Name of NPC(%d) doesn't exist",
        //     L"PledgeGameSystem.cpp",
        //     0xe7f,
        //     npcClassId - 1000000)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][PledgeGameSystem]Name of NPC(%d) doesn't exist",
        //     L"PledgeGameSystem.cpp",
        //     0xe7f,
        //     npcClassId - 1000000)
        L2_LOG_ERROR(
            Log, "Cannot spawn NPC class %d in clan-game field %d: no NPC name is registered.", npcClassId, m_nFieldId
        );
        return false;
    }

    const double headingStepRandom = ::genrand64_real2();
    std::int32_t startPositionIndex = static_cast<std::int32_t>(::genrand64_real2() * 18.0);
    for (std::int32_t index = 0; index < count; ++index) {
        if (owner == nullptr) {
            position = m_pInZone->GetConst()->GetStartPos(startPositionIndex);
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][PledgeGameSystem] [%s][%d] FieldId[%d] "
        //     L"NPC [%s] Spawn request in inzone [ID:%d, Type:%d] "
        //     L"at [xyz:%d,%d,%d (%d)]",
        //     L"PledgeGameSystem.cpp",
        //     0xe90,
        //     m_nFieldId,
        //     npcName,
        //     static_cast<std::int32_t>(m_pInZone->Id()),
        //     m_pInZone->GetKey().typeId,
        //     static_cast<std::int32_t>(position.x),
        //     static_cast<std::int32_t>(position.y),
        //     static_cast<std::int32_t>(position.z),
        //     startPositionIndex)
        // Noise: Per-NPC spawn requests are routine tracing. This record does not establish spawn completion. Missing NPC
        // definitions retain their diagnostics. L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][PledgeGameSystem] [%s][%d] FieldId[%d] "
        //     L"NPC [%s] Spawn request in inzone [ID:%d, Type:%d] "
        //     L"at [xyz:%d,%d,%d (%d)]",
        //     L"PledgeGameSystem.cpp",
        //     0xe90,
        //     m_nFieldId,
        //     npcName,
        //     static_cast<std::int32_t>(m_pInZone->Id()),
        //     m_pInZone->GetKey().typeId,
        //     static_cast<std::int32_t>(position.x),
        //     static_cast<std::int32_t>(position.y),
        //     static_cast<std::int32_t>(position.z),
        //     startPositionIndex)
        NpcServerUtil::SendRequestNpcSpawn(
            npcName, position, static_cast<std::int32_t>(m_pInZone->Id()), WideToUtf16(ai).c_str(), 0x10000
        );
        startPositionIndex += static_cast<std::int32_t>(headingStepRandom * 3.0 + 1.0);
        if (startPositionIndex > 17) {
            startPositionIndex -= 18;
        }
    }
    return true;
}

bool CPledgeGameField::CheckGameExit() {
    L2_GUARD_FUNCTION();

    if (m_pInZone == nullptr) {
        ChangeStatus(PledgeGameFieldStatus::NO_MATCH, true, false);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]field[%d] have no inzone",
        //     L"PledgeGameSystem.cpp",
        //     0xd40,
        //     m_nFieldId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d]field[%d] have no inzone",
        //     L"PledgeGameSystem.cpp",
        //     0xd40,
        //     m_nFieldId)
        L2_LOG_ERROR(
            Log, "Clan-game field %d exit check stopped: the instance is missing; a transition to NO_MATCH was requested.",
            m_nFieldId
        );
        return true;
    }
    if (m_pInZone->IsDeletingObject() || m_pInZone->m_Deleted != 0) {
        ChangeStatus(PledgeGameFieldStatus::NO_MATCH, true, false);
        m_pInZone = nullptr;
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]field[%d] have deleted Inzone",
        //     L"PledgeGameSystem.cpp",
        //     0xd49,
        //     m_nFieldId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d]field[%d] have deleted Inzone",
        //     L"PledgeGameSystem.cpp",
        //     0xd49,
        //     m_nFieldId)
        L2_LOG_ERROR(
            Log,
            "Clan-game field %d exit check stopped: the instance is being deleted; a transition to NO_MATCH was requested "
            "and the instance reference cleared.",
            m_nFieldId
        );
        return true;
    }
    return false;
}

void CPledgeGameField::StatusWaitMatch(const std::int32_t elapsed) {
    L2_GUARD_FUNCTION();

    const std::int32_t waitTime = m_bFastTesting ? 11 : 61;
    if (!CheckGameExit()) {
        if (waitTime < elapsed) {
            ChangeStatus(PledgeGameFieldStatus::START_MATCH, true, false);
        } else if (m_bCheck1SecElapsed) {
            const std::int32_t remaining = waitTime - elapsed;
            for (const std::int32_t time : kWaitMatchBroadcastTimes) {
                if (time == remaining) {
                    Broadcast(
                        "chccc", 0x62, SystemMessageId::MATCH_STARTS_IN_SECONDS, 1, 0x14,
                        static_cast<int>(static_cast<std::int8_t>(remaining))
                    );
                }
            }

            std::int32_t messageId = 0;
            if (remaining == 42) {
                messageId = SystemMessageId::WISHING_VICTORY_IN_LONELY_BATTLE;
            } else if (remaining == 46) {
                messageId = SystemMessageId::EVERYONE_ELSE_IS_AN_ENEMY;
            } else if (remaining == 50) {
                messageId = SystemMessageId::PROVE_YOUR_ABILITIES;
            }
            if (messageId != 0) {
                Broadcast("chc", 0x62, messageId, 0);
            }
        }
    }
    m_bIsUnRegist = false;
}

void CPledgeGameField::StatusPrepareMatch(const std::int32_t elapsed) {
    L2_GUARD_FUNCTION();

    const std::int32_t prepareTime = m_bFastTesting ? 11 : 121;
    if (m_pInZone == nullptr) {
        ChangeStatus(PledgeGameFieldStatus::NO_MATCH, true, false);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]field[%d] have no inzone",
        //     L"PledgeGameSystem.cpp",
        //     0xcee,
        //     m_nFieldId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d]field[%d] have no inzone",
        //     L"PledgeGameSystem.cpp",
        //     0xcee,
        //     m_nFieldId)
        L2_LOG_ERROR(
            Log, "Clan-game field %d preparation stopped: the instance is missing; a transition to NO_MATCH was requested.",
            m_nFieldId
        );
        return;
    }
    if (m_pInZone->IsDeletingObject() || m_pInZone->m_Deleted != 0) {
        ChangeStatus(PledgeGameFieldStatus::NO_MATCH, true, false);
        m_pInZone = nullptr;
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]field[%d] have deleted Inzone",
        //     L"PledgeGameSystem.cpp",
        //     0xcf7,
        //     m_nFieldId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d]field[%d] have deleted Inzone",
        //     L"PledgeGameSystem.cpp",
        //     0xcf7,
        //     m_nFieldId)
        L2_LOG_ERROR(
            Log,
            "Clan-game field %d preparation stopped: the instance is being deleted; a transition to NO_MATCH was requested "
            "and the instance reference cleared.",
            m_nFieldId
        );
        return;
    }

    if (m_pInZone->GetInstantZoneDoorSize() == 0) {
        m_pInZone->InitDoor();
        m_pInZone->SetAllDoorOpenClose(false);
        m_pInZone->InitArea();
    }
    if (elapsed < prepareTime) {
        if (!m_bCheck1SecElapsed) {
            return;
        }
        const std::int32_t remaining = prepareTime - elapsed;
        for (const std::int32_t time : kWaitMatchBroadcastTimes) {
            if (time == remaining) {
                SendToPlayers("chccc", 0x62, 0xe99, 1, 0x14, static_cast<int>(static_cast<std::int8_t>(remaining)));
                break;
            }
        }
        if (remaining < 11 && !m_bIsUnRegist) {
            m_bIsUnRegist = true;
            SendToPlayers("chd", 0xfe, 0x11f, 3);
        }
        return;
    }

    if (!SummonUser()) {
        ChangeStatus(PledgeGameFieldStatus::NO_MATCH, true, false);
        return;
    }
    ChangeStatus(PledgeGameFieldStatus::WAIT_MATCH, true, false);
    m_Players.WaitingGame();
    if (CArea* const area = m_pInZone->GetAreaByIndex(0)) {
        m_pInZone->SetAreaOnOff(area, false);
    }
}

bool CPledgeGameField::SummonUser() {
    L2_GUARD_FUNCTION();
    m_Players.SetPlayer(m_nFieldId, m_pInZone);
    return true;
}

void CPledgeGameField::AsyncUpdateUserStatus(const std::int32_t userSid, const int targetSid) {
    L2_GUARD_FUNCTION();
    if (m_nStatus != PledgeGameFieldStatus::PROCESS_MATCH) {
        return;
    }

    CSPointer<User> user = g_UserMap.FindObject(userSid);
    if (!user) {
        return;
    }
    const auto state = user->m_PledgeGameUser.m_StateMachine.GetStatus();
    if (state != CPledgeGameUserStateMachine::PLEDGEGAME_PLAYER &&
        state != CPledgeGameUserStateMachine::PLEDGEGAME_PLAY_WAITING) {
        return;
    }

    const std::int32_t sid = static_cast<std::int32_t>(user->m_nId);
    const std::int32_t maximumHp = static_cast<std::int32_t>(user->m_data->m_dBaseHP);
    const std::int32_t maximumCp = static_cast<std::int32_t>(user->m_data->m_dBaseCP);
    const std::int32_t currentHp = static_cast<std::int32_t>(user->m_data->m_dHP);
    const std::int32_t currentCp = static_cast<std::int32_t>(user->m_data->m_dCP);

    if (targetSid == 0) {
        Broadcast("chddddd", 0xfe, 0x123, sid, maximumHp, maximumCp, currentHp, currentCp);
        return;
    }

    CSPointer<User> target = g_UserMap.FindObject(targetSid);
    if (target && target->m_PledgeGameUser.m_StateMachine.GetStatus() == CPledgeGameUserStateMachine::PLEDGEGAME_OBSERVER) {
        target->GetSocket()->Send("chddddd", 0xfe, 0x123, sid, maximumHp, maximumCp, currentHp, currentCp);
    }
}

bool CPledgeGameField::AsyncUpdateAllPlayers(const int status) {
    L2_GUARD_FUNCTION();
    const bool processing = m_nStatus == PledgeGameFieldStatus::PROCESS_MATCH;
    if (processing) {
        m_Players.UpdateStatus(this, status);
    }
    return processing;
}

void CPledgeGameField::StatusStartMatch() {
    L2_GUARD_FUNCTION();
    m_nBeginTime = runtime::crt::Time32Now();
    m_bBeginTimeSet = true;
    if (CheckGameExit()) {
        return;
    }

    SetOnePlayerGame();
    SetNpcRelatedGame(false);
    if (m_bOnePlayerMode && m_bNpcRelated) {
        SpawnNpc(0x4b44, 5, nullptr);
    }
    m_Players.StartGame();

    if (m_pInZone != nullptr && !m_pInZone->IsDeletingObject() && m_pInZone->m_Deleted == 0) {
        if (CArea* const area = m_pInZone->GetAreaByIndex(0)) {
            m_pInZone->SetAreaOnOff(area, true);
        }
    }
    Broadcast("chc", 0x62, SystemMessageId::MATCH_STARTED, 0);
    ChangeStatus(PledgeGameFieldStatus::PROCESS_MATCH, true, false);
    m_Players.SendSummonInfo(m_Players);
    AsyncUpdateAllPlayers(0);
}

void CPledgeGameField::StatusProcessMatch(const std::int32_t elapsed) {
    L2_GUARD_FUNCTION();

    if (m_Players.NoOneAlive() || (m_bOnePlayerMode && !m_bNpcRelated)) {
        ChangeStatus(PledgeGameFieldStatus::FINISH_MATCH, true, false);
        return;
    }

    const std::int32_t matchTime = m_bFastTesting ? 121 : 301;
    if (CheckGameExit()) {
        return;
    }
    if (matchTime < elapsed) {
        ChangeStatus(PledgeGameFieldStatus::FINISH_MATCH, true, false);
        return;
    }
    if (!m_bCheck1SecElapsed) {
        return;
    }

    const std::int32_t remaining = matchTime - elapsed;
    SendToPlayers("chd", 0xfe, 0x124, remaining);
    for (const std::int32_t time : kWaitMatchBroadcastTimes) {
        if (time == remaining) {
            Broadcast(
                "chccc", 0x62, SystemMessageId::MATCH_END_SECONDS_REMAINING, 1, 0x14,
                static_cast<int>(static_cast<std::int8_t>(remaining))
            );
        }
    }

    if (elapsed == 60 || elapsed == 180) {
        m_Players.CancelHide();
    }
    if (elapsed == 20) {
        SpawnNpc(0x4b5c, 5, nullptr);
    } else if (elapsed == 120) {
        SpawnNpc(0x4b5c, 3, nullptr);
    } else if (elapsed == 240) {
        SpawnNpc(0x4b5c, 1, nullptr);
    }

    if (!m_bOnePlayerMode && m_bNpcRelated && (elapsed == 30 || elapsed == 40 || elapsed == 60 || elapsed == 120) &&
        static_cast<std::int32_t>(::genrand64_real2() * 100.0 + 1.0) < 21) {
        SetNpcRelatedGame(true);
        Broadcast("chc", 0x62, SystemMessageId::TIME_TO_CHOOSE, 0);
        SpawnNpc(0x4b44, 10, nullptr);
    }
}

void CPledgeGameField::StatusFinishMatch() {
    L2_GUARD_FUNCTION();
    if (m_nStatus != PledgeGameFieldStatus::FINISH_MATCH) {
        return;
    }

    ChangeStatus(PledgeGameFieldStatus::CLEAR_MATCH, false, true);
    std::int32_t lifeTime = 0;
    if (m_bBeginTimeSet) {
        lifeTime = runtime::crt::Time32Now() - m_nBeginTime;
        m_bBeginTimeSet = false;
    }

    User* const winner = GetWinner();
    if (m_Players.GetPlayersCount() > 1 && !m_bOnePlayerMode) {
        std::optional<std::vector<UserMatchingPoint>> usersStorage;
        {
            L2_GUARD(m_Players.m_Lock);
            const unsigned int winnerDbId = winner != nullptr ? winner->GetDBID() : 0;
            const PledgeGame::GAMERESULT_TYPE result = winner != nullptr ? PledgeGame::GRT_WIN : PledgeGame::GRT_DRAW;
            if (!g_MatchingPointDB.GenerateWinningRate(m_Players.m_MemberList) ||
                !g_MatchingPointDB.UpdateMatchingPoint(result, winnerDbId, m_Players.m_MemberList)) {
                return;
            }

            if (g_PledgeGameSystem.m_nDebugUserSID != 0) {
                g_PledgeGameSystem.DebugString(u"Request SavePoint");
            }
            auto& users = usersStorage.emplace();
            for (const PledgeGamePlayerInfo& info : m_Players.m_MemberList) {
                users.push_back(
                    UserMatchingPoint{info.playerSID, info.playerDBID, info.nMatchingPointDiff + info.nWinningPoint}
                );
                if (g_PledgeGameSystem.m_nDebugUserSID != 0) {
                    g_PledgeGameSystem.DebugString(
                        u"name: %s, base: %d, diff: %d, win: %d", info.playerName,
                        info.nMatchingPointTotal - info.nMatchingPointDiff, info.nMatchingPointDiff, info.nWinningPoint
                    );
                }
            }
            db.RequestSaveMatchingPoint(MatchingPoint::MPT_BASE, users);
        }
    }

    SendMatchResult();
    if (winner != nullptr) {
        DoFinishJob(winner, lifeTime);
    } else {
        DoFinishJobDraw(lifeTime);
    }

    if (m_pInZone != nullptr && !m_pInZone->IsDeletingObject() && m_pInZone->m_Deleted == 0) {
        if (CArea* const area = m_pInZone->GetAreaByIndex(0)) {
            m_pInZone->SetAreaOnOff(area, false);
        }
    }
    m_Players.HideSummonInfo(m_Players);

    if (winner == nullptr) {
        return;
    }
    DeleteNPCFromCreatureMap();

    std::int32_t normalCount = 0;
    if (g_PledgeGameSystem.m_nWinPrizeNormalItemType > 0) {
        const std::int32_t minimum =
            std::min(g_PledgeGameSystem.m_nWinPrizeNormalMinCnt, g_PledgeGameSystem.m_nWinPrizeNormalMaxCnt);
        const std::int32_t maximum =
            std::max(g_PledgeGameSystem.m_nWinPrizeNormalMinCnt, g_PledgeGameSystem.m_nWinPrizeNormalMaxCnt);
        normalCount = static_cast<std::int32_t>(
            ::genrand64_real2() * static_cast<double>(maximum - minimum + 1) + static_cast<double>(minimum)
        );
        g_PledgeGameSystem.GiveItemToWinner(
            static_cast<std::int32_t>(winner->GetServerId()), g_PledgeGameSystem.m_nWinPrizeNormalItemType, normalCount,
            false
        );
    }

    std::int32_t randomCount = 0;
    if (g_PledgeGameSystem.m_nWinPrizeRandItemType > 0) {
        const std::int32_t minimum =
            std::min(g_PledgeGameSystem.m_nWinPrizeRandItemMinCnt, g_PledgeGameSystem.m_nWinPrizeRandItemMaxCnt);
        const std::int32_t maximum =
            std::max(g_PledgeGameSystem.m_nWinPrizeRandItemMinCnt, g_PledgeGameSystem.m_nWinPrizeRandItemMaxCnt);
        randomCount = static_cast<std::int32_t>(
            ::genrand64_real2() * static_cast<double>(maximum - minimum + 1) + static_cast<double>(minimum)
        );
        g_PledgeGameSystem.GiveItem(winner, g_PledgeGameSystem.m_nWinPrizeRandItemType, randomCount, false);
    }

    if (CPledge* const pledge = winner->GetPledge()) {
        CLogFormatter formatter;
        formatter.WriteLog1(
            0x801, winner, nullptr, pledge->GetPledgeName(), nullptr, nullptr, static_cast<std::int64_t>(pledge->GetDbId()),
            static_cast<std::int64_t>(pledge->GetPledgeLevel()), normalCount, randomCount, 0, 0, 0
        );
    }
}

void CPledgeGameField::StatusClearMatch(const std::int32_t elapsed) {
    L2_GUARD_FUNCTION();
    if (m_nStatus != PledgeGameFieldStatus::CLEAR_MATCH) {
        return;
    }
    if (elapsed < 32) {
        if (!m_bCheck1SecElapsed) {
            return;
        }
        if (m_pInZone == nullptr || m_pInZone->IsDeletingObject() || m_pInZone->m_Deleted != 0) {
            m_pInZone = nullptr;
            return;
        }
        const std::int32_t remaining = 31 - elapsed;
        for (const std::int32_t time : kClearMatchBroadcastTimes) {
            if (time == remaining) {
                SendToPlayers("chccc", 0x62, 0xea5, 1, 0x14, static_cast<int>(static_cast<std::int8_t>(remaining)));
            }
        }
        return;
    }

    ChangeStatus(PledgeGameFieldStatus::NO_MATCH, true, false);
    m_Players.FinishGame(true);
    m_Players.EscapeFromPledgeGame();
    EscapeAllObserver();
    DeleteNPCFromCreatureMap();
}

void CPledgeGameField::SetWaitMatchTimer() {
    L2_GUARD_FUNCTION();
    m_tStatusChanged = runtime::crt::Time32Now();
}

void CPledgeGameField::FreezeAll() {
    L2_GUARD_FUNCTION();
    m_Players.Freeze();
}

void CPledgeGameField::ResetFreezeAll(const bool activateSurvivorSkill) {
    L2_GUARD_FUNCTION();
    m_Players.ResetFreeze(activateSurvivorSkill);
}

User* CPledgeGameField::GetWinner() {
    L2_GUARD_FUNCTION();
    return m_Players.GetWinner(m_bNpcRelated && m_bOnePlayerMode);
}

void CPledgeGameField::SendMatchResult() {
    L2_GUARD_FUNCTION();
    m_Players.SendMatchResult(GetWinner());
}

void CPledgeGameField::DoFinishJob(User* const winner, const std::int32_t) {
    L2_GUARD_FUNCTION();
    if (winner == nullptr) {
        return;
    }
    m_Players.SendWinnerName(winner);
    m_ObserverEntry.Broadcast("chccS", 0x62, SystemMessageId::CHARACTER_WON_MATCH, 1, 0x0c, winner->GetName2(nullptr));
    PledgeGameInfo info{};
    m_Players.Win(winner, info);
}

void CPledgeGameField::DoFinishJobDraw(const std::int32_t) {
    L2_GUARD_FUNCTION();
    Broadcast(
        "chc", 0x62, m_bOnePlayerMode ? SystemMessageId::SHOW_YOUR_ABILITY_NEXT_TIME : SystemMessageId::ENDED_IN_DRAW, 0
    );
    PledgeGameInfo info{};
    m_Players.Draw(info);
}

void CPledgeGameField::SetWinnerLifeTime() {
    L2_GUARD_FUNCTION();
    User* const winner = GetWinner();
    if (winner == nullptr) {
        return;
    }
    winner->m_PledgeGameUser.SetLifeTime(m_tNow - m_nBeginTime);
    m_Players.UpdatePlayerInfo(winner);
}

void CPledgeGameField::ChangeStatus(
    const PledgeGameFieldStatus::type status, const bool useLock, const bool forceSystemCheck
) {
    L2_GUARD_FUNCTION();

    const auto updateStatus = [&] {
        if (m_nStatus == status) {
            return false;
        }
        m_nStatus = status;
        m_tStatusChanged = runtime::crt::Time32Now();
        return true;
    };
    const bool changed = [&] {
        if (useLock) {
            L2_GUARD(m_StatusLock);
            return updateStatus();
        }
        return updateStatus();
    }();
    if (!changed) {
        return;
    }

    if (status == PledgeGameFieldStatus::PREPARE_MATCH) {
        AddTimer(80, 0);
    } else if (status == PledgeGameFieldStatus::WAIT_MATCH) {
        m_Players.ObserverEnd();
        m_Players.InitShuffleNo();
        m_Players.SendMatchMemberList(m_ObserverEntry, 0);
        m_Players.SendInformationPage();
        if (m_pInZone != nullptr && static_cast<std::uint32_t>(m_nFieldId) < 6U) {
            sPledgeGameFieldInfo& field = SharedPledgeGameInfo::GetInfo()->arrFieldInfo[m_nFieldId];
            field.fieldStatus = PGFIELD_WAIT;
            field.nInzoneTypeId = m_pInZone->GetKey().typeId;
            field.nInzoneSId = static_cast<std::int32_t>(m_pInZone->Id());
        }
        g_PledgeGameSystem.OnGameChanged(m_nFieldId);
        FreezeAll();
        SetWaitMatchTimer();
    } else if (status == PledgeGameFieldStatus::PROCESS_MATCH) {
        if (m_pInZone != nullptr && static_cast<std::uint32_t>(m_nFieldId) < 6U) {
            sPledgeGameFieldInfo& field = SharedPledgeGameInfo::GetInfo()->arrFieldInfo[m_nFieldId];
            field.fieldStatus = PGFIELD_MATCH;
            field.nInzoneTypeId = m_pInZone->GetKey().typeId;
            field.nInzoneSId = static_cast<std::int32_t>(m_pInZone->Id());
        }
        g_PledgeGameSystem.OnGameChanged(m_nFieldId);
        ResetFreezeAll(true);
    } else if (status == PledgeGameFieldStatus::FINISH_MATCH) {
        if (m_pInZone != nullptr && static_cast<std::uint32_t>(m_nFieldId) < 6U) {
            sPledgeGameFieldInfo& field = SharedPledgeGameInfo::GetInfo()->arrFieldInfo[m_nFieldId];
            field.fieldStatus = PGFIELD_NONE;
            field.nInzoneTypeId = m_pInZone->GetKey().typeId;
            field.nInzoneSId = static_cast<std::int32_t>(m_pInZone->Id());
        }
        SetWinnerLifeTime();
        g_PledgeGameSystem.OnGameChanged(m_nFieldId);
    } else if (status == PledgeGameFieldStatus::CLEAR_MATCH) {
        if (m_pInZone != nullptr && static_cast<std::uint32_t>(m_nFieldId) < 6U) {
            sPledgeGameFieldInfo& field = SharedPledgeGameInfo::GetInfo()->arrFieldInfo[m_nFieldId];
            field.fieldStatus = PGFIELD_NONE;
            field.nInzoneTypeId = m_pInZone->GetKey().typeId;
            field.nInzoneSId = static_cast<std::int32_t>(m_pInZone->Id());
        }
        g_PledgeGameSystem.OnGameChanged(m_nFieldId);
        m_bFastTesting = false;
    }

    if (forceSystemCheck || status == PledgeGameFieldStatus::CLEAR_MATCH || status == PledgeGameFieldStatus::NO_MATCH) {
        g_PledgeGameSystem.CheckStatusPledgeGameFieldAll(status);
    }
}

CPledgeGameSystem::CPledgeGameSystem() :
    m_tLastAccouncing(0),
    m_tNow(0),
    m_OpenTime(0),
    m_alignment024To028{},
    m_pWaitSkill(nullptr),
    m_bStartGameAutomatically(true),
    m_alignment041To044{},
    m_tSeasonStart(0),
    m_nDbProcessing(0),
    m_nMessageRegistered(0),
    m_msgReceived{},
    m_alignment05cTo060{},
    m_nMatchTime(0),
    m_nStartSec(0),
    m_bGetPenalty(false),
    m_alignment5ea1To5ea4{},
    m_TickCount(0),
    m_nActiveInstantZoneCount(6),
    m_alignment5f84To5f88{},
    m_pPledgeGameFieldList(new CPledgeGameField[6]),
    m_isLoadingPoint(0),
    m_nDebugUserSID(0),
    m_nLosePrizeItemType(0),
    m_nLosePrizeItemCount(0),
    m_nWinPrizeNormalItemType(0),
    m_nWinPrizeNormalMinCnt(0),
    m_nWinPrizeNormalMaxCnt(0),
    m_nWinPrizeRandItemType(0),
    m_nWinPrizeRandItemMinCnt(0),
    m_nWinPrizeRandItemMaxCnt(0) {}

CPledgeGameSystem::~CPledgeGameSystem() {
    delete[] m_pPledgeGameFieldList;
    m_pPledgeGameFieldList = nullptr;
}

void CPledgeGameSystem::SetFastTesting(const bool fastTesting) {
    // Current L2Server 0x0099c988.
    L2_GUARD_FUNCTION();
    for (std::int32_t index = 0; index < 6; ++index) {
        m_pPledgeGameFieldList[index].m_bFastTesting = fastTesting;
    }
}

void CPledgeGameSystem::InitPledgeGameInZone() {
    L2_GUARD_FUNCTION();
    constexpr std::array<std::int32_t, 6> typeIds{
        0xdc,
        0xdc,
        0xdd,
        0xde,
        0xde,
        0xdf,
    };
    for (std::size_t index = 0; index < typeIds.size(); ++index) {
        CPledgeGameField& field = m_pPledgeGameFieldList[index];
        if (field.m_pInZone != nullptr) {
            continue;
        }
        const std::int32_t typeId = typeIds[index];
        CreateFailReason reason = CFR_INTERNAL_ERROR;
        CInstantZone* const zoneTemplate = g_InstantZoneMgr.GetInstantZoneTemplate(typeId);
        if (zoneTemplate == nullptr) {
            continue;
        }
        if (zoneTemplate->m_nCurrentZoneCount >= zoneTemplate->MAX_ZONE_COUNT) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] [PledgeGameSystem] Instant zone couldn't be "
            //     L"created because of max channel(type:%d)",
            //     L"PledgeGameSystem.cpp",
            //     0x16cc,
            //     typeId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] [PledgeGameSystem] Instant zone couldn't be "
            //     L"created because of max channel(type:%d)",
            //     L"PledgeGameSystem.cpp",
            //     0x16cc,
            //     typeId)
            L2_LOG_WARN(
                Log,
                "Cannot create clan-game field %llu instance type %d: the channel limit has been reached (%d of %d); "
                "initialization continues with other fields.",
                static_cast<unsigned long long>(index), typeId, static_cast<int>(zoneTemplate->m_nCurrentZoneCount),
                static_cast<int>(zoneTemplate->MAX_ZONE_COUNT)
            );
            continue;
        }
        field.m_pInZone = g_InstantZoneMgr.CreateInstantZone(typeId, reason);
        if (field.m_pInZone == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] [PledgeGameSystem] Instant zone couldn't be "
            //     L"created(type:%d)",
            //     L"PledgeGameSystem.cpp",
            //     0x16c6,
            //     typeId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] [PledgeGameSystem] Instant zone couldn't be "
            //     L"created(type:%d)",
            //     L"PledgeGameSystem.cpp",
            //     0x16c6,
            //     typeId)
            L2_LOG_ERROR(
                Log,
                "Cannot create clan-game field %llu instance type %d (reason %d); initialization continues with other "
                "fields.",
                static_cast<unsigned long long>(index), typeId, static_cast<int>(reason)
            );
        }
    }
}

void CPledgeGameSystem::InitPledgeGameField() {
    L2_GUARD_FUNCTION();
    for (std::int32_t index = 0; index < 6; ++index) {
        m_pPledgeGameFieldList[index].m_nFieldId = index;
    }
    InitPledgeGameInZone();
    for (std::int32_t index = 0; index < 6; ++index) {
        m_pPledgeGameFieldList[index].Init(index);
    }
}

void CPledgeGameSystem::Init() {
    L2_GUARD_FUNCTION();
    DxLoadPledgeGame packet{};
    db.RequestTPacketProcess(g_nWorldId, 0x22, static_cast<int>(sizeof(packet)), packet);
    if (g_JobFinishSignal.WaitFinish(JFT_LOAD_PLEDGEGAME, 10)) {
        // Log.Add(LOG_NORMAL, L"Load Success[PledgeGame]")
        // L2_LOG_INFO(Log, L"Load Success[PledgeGame]")
        L2_LOG_INFO(Log, "Clan-game database load completion signal received; initializing fields.");
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Problem occurred in Loading PledgeGame info")
        // L2_LOG_ERROR(Log,
        //     L"Problem occurred in Loading PledgeGame info")
        L2_LOG_ERROR(
            Log,
            "Clan-game database load did not signal completion within the startup wait; field initialization still "
            "continues."
        );
    }
    InitPledgeGameField();
    (new CPledgeGameLoadingMessage())->Post(0);
    m_pWaitSkill = g_SkillDB.GetSkillInfo(CSkillKey(0x1792, 1, 0), nullptr);
    AddTimer(1000, 0);
}

bool CPledgeGameSystem::CheckUser(User* const user) {
    // Current L2Server 0x009ad530.
    L2_GUARD_FUNCTION();
    SharedPledgeGameInfo* const info = SharedPledgeGameInfo::GetInfo();
    if (info->m_nStep == PledgeGameCondition::GAME_OPEN) {
        if (!user->m_PledgeGameUser.m_bGotInvitingMessage && m_Entry.CheckUser(user, false, false)) {
            if (user->m_PledgeGameUser.m_nRecordSeason == info->m_nSeason) {
                user->GetSocket()->Send("chd", 0xfe, 0x11f, 1);
                user->m_PledgeGameUser.m_bGotInvitingMessage = true;
            } else {
                GetPledgeGameRecord(static_cast<int>(user->GetServerId()));
            }
        }
    } else if (info->m_nStep == PledgeGameCondition::GAME_PREPARE && user->m_PledgeGameUser.m_bGotInvitingMessage &&
        !m_Entry.IsRegisteredUser(user, true)) {
        user->GetSocket()->Send("chd", 0xfe, 0x11f, 0);
        user->m_PledgeGameUser.m_bGotInvitingMessage = false;
    }
    return true;
}

void CPledgeGameSystem::GetPledgeGameRecord(const int userSid) {
    // Current L2Server 0x0099eef4.
    L2_GUARD_FUNCTION();
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user != nullptr) {
        db.RequestGetPledgeGameRecord(SharedPledgeGameInfo::GetInfo()->m_nSeason, static_cast<int>(user->GetDBID()));
    }
}

char CPledgeGameSystem::GetUserFlag(User* const user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return 0;
    }
    if (DidWinnerOfLastSeasonGetItem() && IsWinnerOfLastSeason(user)) {
        return 100;
    }
    if (user->m_PledgeGameUser.IsPlayer()) {
        return static_cast<char>(static_cast<std::int16_t>(user->m_PledgeGameUser.GetPlayNo()));
    }
    return 0;
}

bool CPledgeGameSystem::IsWinnerOfLastSeason(User* const user) {
    L2_GUARD_FUNCTION();
    return user != nullptr &&
        user->GetDBID() == static_cast<unsigned int>(SharedPledgeGameInfo::GetInfo()->m_nPrevSeasonWinnerCharacterDbId);
}

bool CPledgeGameSystem::DidWinnerOfLastSeasonGetItem() {
    L2_GUARD_FUNCTION();
    return SharedPledgeGameInfo::GetInfo()->m_nPrevSeasonWinnerGotItem != 0;
}

bool CPledgeGameSystem::CheckTime() {
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    std::tm local{};
    if (!runtime::crt::LocalTime32(now, local)) {
        return false;
    }
    for (const std::int32_t weekday : s_lstWday) {
        if (weekday == local.tm_wday) {
            return true;
        }
    }
    return false;
}

bool CPledgeGameSystem::CanChangeStep(const PledgeGameCondition::type condition) {
    L2_GUARD_FUNCTION();

    const std::int32_t step = SharedPledgeGameInfo::GetInfo()->m_nStep;
    switch (step) {
        case PledgeGameCondition::NONE:
            return condition == PledgeGameCondition::SEASON_STARTED;
        case PledgeGameCondition::SEASON_STARTED:
            return condition == PledgeGameCondition::SEASON_ENDED || condition == PledgeGameCondition::GAME_OPEN;
        case PledgeGameCondition::GAME_OPEN:
            return condition == PledgeGameCondition::GAME_PREPARE;
        case PledgeGameCondition::SEASON_ENDED:
            return condition == PledgeGameCondition::SEASON_STARTED;
        default:
            return true;
    }
}

bool CPledgeGameSystem::SetStep(const PledgeGameCondition::type condition) {
    L2_GUARD_FUNCTION();

    if (!CanChangeStep(condition)) {
        return false;
    }

    SharedPledgeGameInfo::GetInfo()->m_nStep = condition;
    if (condition >= PledgeGameCondition::GAME_OPEN && condition <= PledgeGameCondition::GAME_ENDED) {
        m_nMatchTime = 1;
    } else if (condition < PledgeGameCondition::GAME_OPEN) {
        m_nMatchTime = 0;
    } else if (condition > PledgeGameCondition::GAME_ENDED) {
        m_nMatchTime = 2;
    }
    return true;
}

bool CPledgeGameSystem::StartSeason() {
    L2_GUARD_FUNCTION();
    return SetStep(PledgeGameCondition::SEASON_STARTED);
}

void CPledgeGameSystem::CheckAutoStart() {
    L2_GUARD_FUNCTION();
    StartSeason();
}

bool CPledgeGameSystem::IsAllFieldStatus(const PledgeGameFieldStatus::type status) {
    // Current L2Server 0x0099cce0.
    L2_GUARD_FUNCTION();
    for (std::int32_t index = 0; index < 6; ++index) {
        if (m_pPledgeGameFieldList[index].m_nStatus != status) {
            return false;
        }
    }
    return true;
}

void CPledgeGameSystem::ClearEntry() {
    // Current L2Server 0x009a3a7c.
    L2_GUARD_FUNCTION();
    m_Entry.ClearAllEntry();
}

void CPledgeGameSystem::ArrangeEntryToField() {
    // Current L2Server 0x009a9408.
    L2_GUARD_FUNCTION();
    m_Entry.ArrangeUserToField(m_pPledgeGameFieldList);
}

bool CPledgeGameSystem::CheckStatusPledgeGameFieldAll(const PledgeGameFieldStatus::type status) {
    L2_GUARD_FUNCTION();

    if (!IsAllFieldStatus(status)) {
        return false;
    }

    PledgeGameCondition::type condition{};
    switch (status) {
        case PledgeGameFieldStatus::NO_MATCH:
            ClearEntry();
            condition = PledgeGameCondition::SEASON_STARTED;
            break;
        case PledgeGameFieldStatus::OPEN_MATCH:
            condition = PledgeGameCondition::GAME_OPEN;
            break;
        case PledgeGameFieldStatus::PREPARE_MATCH:
            condition = PledgeGameCondition::GAME_PREPARE;
            break;
        case PledgeGameFieldStatus::WAIT_MATCH:
            ClearEntry();
            condition = PledgeGameCondition::GAME_READY;
            break;
        case PledgeGameFieldStatus::START_MATCH:
            condition = PledgeGameCondition::GAME_STARTED;
            break;
        case PledgeGameFieldStatus::FINISH_MATCH:
            condition = PledgeGameCondition::GAME_ENDED;
            break;
        case PledgeGameFieldStatus::CLEAR_MATCH:
            ClearEntry();
            condition = PledgeGameCondition::GAME_CLEAR;
            break;
        default:
            return true;
    }
    return SetStep(condition);
}

bool CPledgeGameSystem::ChangeStatusPledgeGameField(const PledgeGameFieldStatus::type status) {
    L2_GUARD_FUNCTION();
    for (std::int32_t index = 0; index < 6; ++index) {
        m_pPledgeGameFieldList[index].ChangeStatus(status, true, false);
    }
    return CheckStatusPledgeGameFieldAll(status);
}

bool CPledgeGameSystem::IncreaseKillCountByKillingNPC(CCreature* const killer) {
    // Current L2Server 0x009a70f0.
    L2_GUARD_FUNCTION();

    if (killer == nullptr || !killer->IsPCOrPCRelatedNPC()) {
        return false;
    }

    User* const user = killer->GetUserOrMaster();
    if (user == nullptr || !user->m_PledgeGameUser.IsPlaying()) {
        return false;
    }

    CPledgeGameField* const field = user->m_PledgeGameUser.GetField();
    if (field == nullptr || !field->m_bOnePlayerMode) {
        return false;
    }
    return field->IncreaseKillCountByKillingNPC(static_cast<std::int32_t>(user->GetServerId()));
}

bool CPledgeGameSystem::OpenGame() {
    L2_GUARD_FUNCTION();
    if (!ChangeStatusPledgeGameField(PledgeGameFieldStatus::OPEN_MATCH)) {
        return false;
    }

    CUserBroadcaster::inst().BroadcastToAllUser("chc", 0x62, 0xec5, 0);
    m_OpenTime = runtime::crt::Time32Now();
    m_bGetPenalty = false;
    return true;
}

bool CPledgeGameSystem::PrepareGame() {
    L2_GUARD_FUNCTION();

    if (runtime::raw_atomic::CompareExchangeI32(reinterpret_cast<volatile std::int32_t*>(&m_isLoadingPoint), 1, 0) != 0) {
        return false;
    }

    if (ChangeStatusPledgeGameField(PledgeGameFieldStatus::PREPARE_MATCH)) {
        std::optional<std::vector<UserMatchingPoint>> usersStorage;
        {
            L2_GUARD(m_Entry.m_Lock);
            if (m_nDebugUserSID != 0) {
                DebugString(u"Request LoadPoint");
            }

            auto& users = usersStorage.emplace();
            for (User* const user : m_Entry.m_EntryList) {
                if (user == nullptr) {
                    continue;
                }
                users.push_back(UserMatchingPoint{user->GetServerId(), user->GetDBID(), 0});
            }

            if (!db.RequestLoadMatchingPoint(MatchingPoint::MPT_BASE, users, 0)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[MatchPoint][%s][%d] fail to db.RequestLoadMatchPoint, "
                //     L"memberList.count: %d",
                //     L"CPledgeGameSystem::PrepareGame",
                //     0x12ac,
                //     static_cast<int>(users.size()))
                // L2_LOG_ERROR(Log,
                //     L"[MatchPoint][%s][%d] fail to db.RequestLoadMatchPoint, "
                //     L"memberList.count: %d",
                //     L"CPledgeGameSystem::PrepareGame",
                //     0x12ac,
                //     static_cast<int>(users.size()))
                L2_LOG_ERROR(
                    Log,
                    "Cannot request clan-game matching points for %llu participants; field cleanup is requested and the "
                    "point-loading flag is cleared.",
                    static_cast<unsigned long long>(users.size())
                );
                ChangeStatusPledgeGameField(PledgeGameFieldStatus::CLEAR_MATCH);
                runtime::raw_atomic::ExchangeI32(reinterpret_cast<volatile std::int32_t*>(&m_isLoadingPoint), 0);
                // Log.Add(
                //     LOG_ERROR,
                //     L"[MatchPoint][%s][%d] success",
                //     L"CPledgeGameSystem::PrepareGame",
                //     0x12b1)
                // Noise: This success trace is inside the failed request branch. The preceding diagnostic already describes
                // the request failure and cleanup. L2_LOG_ERROR(Log,
                //     L"[MatchPoint][%s][%d] success",
                //     L"CPledgeGameSystem::PrepareGame",
                //     0x12b1)
            }
        }
    }

    // The current 0x009a7e14 body returns false even after successfully
    // posting the asynchronous load. Completion arrives through DXPacket.
    return false;
}

bool CPledgeGameSystem::OnPrepareGameComplete(std::map<int, std::int64_t>& points) {
    // Current L2Server 0x009aae38.
    L2_GUARD_FUNCTION();

    if (SharedPledgeGameInfo::GetInfo()->m_nStep != PledgeGameCondition::GAME_PREPARE) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MatchPoint][%s][%d] can't change pledge game status ot "
        //     L"PREPARE_MATCH",
        //     kOwner,
        //     0x12d0)
        // L2_LOG_ERROR(Log,
        //     L"[MatchPoint][%s][%d] can't change pledge game status ot "
        //     L"PREPARE_MATCH",
        //     kOwner,
        //     0x12d0)
        L2_LOG_WARN(
            Log,
            "Clan-game matching-point callback arrived outside GAME_PREPARE; field cleanup is requested and the "
            "point-loading flag is cleared."
        );
        ChangeStatusPledgeGameField(PledgeGameFieldStatus::CLEAR_MATCH);
        runtime::raw_atomic::ExchangeI32(reinterpret_cast<volatile std::int32_t*>(&m_isLoadingPoint), 0);
        return false;
    }

    if (points.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MatchPoint][%s][%d] point load count is zero",
        //     kOwner,
        //     0x12da)
        // L2_LOG_ERROR(Log,
        //     L"[MatchPoint][%s][%d] point load count is zero",
        //     kOwner,
        //     0x12da)
        L2_LOG_ERROR(
            Log,
            "Clan-game matching-point response is empty; field cleanup is requested and the point-loading flag is cleared."
        );
        ChangeStatusPledgeGameField(PledgeGameFieldStatus::CLEAR_MATCH);
        runtime::raw_atomic::ExchangeI32(reinterpret_cast<volatile std::int32_t*>(&m_isLoadingPoint), 0);
        return false;
    }

    {
        L2_GUARD(m_Entry.m_Lock);
        for (User* const user : m_Entry.m_EntryList) {
            const int userDatabaseId = static_cast<int>(user->GetDBID());
            const auto point = points.find(userDatabaseId);
            if (point == points.end()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[MatchPoint][%s][%d] fail to find user in memberList",
                //     kOwner,
                //     0x1303)
                // L2_LOG_ERROR(Log,
                //     L"[MatchPoint][%s][%d] fail to find user in memberList",
                //     kOwner,
                //     0x1303)
                L2_LOG_ERROR(
                    Log,
                    "Clan-game matching-point response has no entry for character %u; preparation stops and field cleanup "
                    "is requested. Earlier matching-point updates are retained.",
                    static_cast<unsigned>(userDatabaseId)
                );
                ChangeStatusPledgeGameField(PledgeGameFieldStatus::CLEAR_MATCH);
                runtime::raw_atomic::ExchangeI32(reinterpret_cast<volatile std::int32_t*>(&m_isLoadingPoint), 0);
                return false;
            }

            std::int64_t basePoint = 0;
            if (!g_MatchingPointDB.GetMatchingPointBase(MatchingPoint::MPT_BASE, user->m_data->m_nLevel, basePoint)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[MatchPoint][%s][%d] fail to load matching point base "
                //     L"user_dbid:%d, user_level:%d",
                //     kOwner,
                //     0x12f6,
                //     userDatabaseId,
                //     user->m_data->m_nLevel)
                // L2_LOG_ERROR(Log,
                //     L"[MatchPoint][%s][%d] fail to load matching point base "
                //     L"user_dbid:%d, user_level:%d",
                //     kOwner,
                //     0x12f6,
                //     userDatabaseId,
                //     user->m_data->m_nLevel)
                L2_LOG_ERROR(
                    Log,
                    "Cannot obtain base matching points for character %u at level %d; clan-game preparation stops and field "
                    "cleanup is requested. Earlier updates are retained.",
                    static_cast<unsigned>(userDatabaseId), user->m_data->m_nLevel
                );
                ChangeStatusPledgeGameField(PledgeGameFieldStatus::CLEAR_MATCH);
                runtime::raw_atomic::ExchangeI32(reinterpret_cast<volatile std::int32_t*>(&m_isLoadingPoint), 0);
                return false;
            }

            user->m_MatchingPoint.m_nMatchingPointBase[MatchingPoint::MPT_BASE] = basePoint;
            user->m_MatchingPoint.ValidateMatchingPoint(MatchingPoint::MPT_BASE);
            user->m_MatchingPoint.m_nMatchingPointDiff[MatchingPoint::MPT_BASE] = point->second;
            user->m_MatchingPoint.ValidateMatchingPoint(MatchingPoint::MPT_BASE);
        }

        if (!g_MatchingPointDB.SortMembersWithMatchingPoint(MatchingPoint::MPT_BASE, m_Entry.m_EntryList)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[MatchPoint][%s][%d] fail to sort memberList",
            //     kOwner,
            //     0x130e)
            // L2_LOG_ERROR(Log,
            //     L"[MatchPoint][%s][%d] fail to sort memberList",
            //     kOwner,
            //     0x130e)
            L2_LOG_ERROR(
                Log,
                "Cannot sort clan-game participants by matching points; field cleanup is requested and the point-loading "
                "flag is cleared. Matching-point updates already made are retained."
            );
            ChangeStatusPledgeGameField(PledgeGameFieldStatus::CLEAR_MATCH);
            runtime::raw_atomic::ExchangeI32(reinterpret_cast<volatile std::int32_t*>(&m_isLoadingPoint), 0);
            return false;
        }
    }
    m_bGetPenalty = true;
    CUserBroadcaster::inst().BroadcastToAllUser("chd", 0x62, 0xec6, 0);
    ArrangeEntryToField();
    runtime::raw_atomic::ExchangeI32(reinterpret_cast<volatile std::int32_t*>(&m_isLoadingPoint), 0);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][MatchPoint][%s][%d] success",
    //     kOwner,
    //     0x131b)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][MatchPoint][%s][%d] success",
    //     kOwner,
    //     0x131b)
    L2_LOG_INFO(Log, "Clan-game matching points processed; field assignment ran and the point-loading flag is cleared.");
    return false;
}

void CPledgeGameSystem::SendDebugText(std::u16string text) {
    L2_GUARD_FUNCTION();
    if (m_nDebugUserSID == 0) {
        return;
    }

    CSPointer<User> user = g_UserMap.FindObject(static_cast<int>(m_nDebugUserSID));
    if (!user || user->GetSocket() == nullptr) {
        return;
    }

    // The original buffer holds at most 0x3ff text units.
    if (text.size() >= 0x3ff) {
        text.resize(0x3ff);
    }
    user->GetSocket()->SendSystemMessage(u"SYS", text.c_str());
}

void CPledgeGameSystem::OnGameChanged(const std::int32_t fieldId) {
    L2_GUARD_FUNCTION();
    m_pledgeGameHtml.OnGameChanged(fieldId);
}

void CPledgeGameSystem::InitPlayersName(const std::int32_t countryCode) {
    // Current L2Server 0x009ae050.
    if (m_mapPlayersName.find(countryCode) != m_mapPlayersName.end()) {
        return;
    }

    const wchar_t* const sysString = g_ObjectDB.GetSysString(static_cast<std::uint8_t>(countryCode), 0xb05);
    if (sysString == nullptr) {
        return;
    }

    auto* const names = new std::vector<std::u16string>();
    std::wstring base(sysString);
    if (base.size() > 24U) {
        base.resize(24U);
    }
    for (std::int32_t playerNumber = 1; playerNumber < 19; ++playerNumber) {
        std::wstring name = base + std::to_wstring(playerNumber);
        if (name.size() > 24U) {
            name.resize(24U);
        }
        const std::u16string targetName = ::WideToUtf16(name);
        names->push_back(std::u16string(std::u16string_view(targetName)));
    }
    m_mapPlayersName.emplace(countryCode, names);
}

void CPledgeGameSystem::CheckPlayerNameString(User* const user) {
    // Current L2Server 0x009ae348.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    const std::uint8_t countryCode = user->m_data->m_CountryCode;
    if (g_ObjectDB.GetSysString(countryCode, 0xb05) == nullptr) {
        user->GetSocket()->Send("chd", 0xfe, 0x128, 0xb05);
        return;
    }
    InitPlayersName(countryCode);
}

bool CPledgeGameSystem::RegisterEntry(User* const user) {
    // Current L2Server 0x009ae458.
    L2_GUARD_FUNCTION();
    if (!m_Entry.CheckUser(user, true, false)) {
        return false;
    }

    if (CPledge* const pledge = user->GetPledge()) {
        CLogFormatter formatter;
        formatter.WriteLog1(
            0x7fa, user, nullptr, pledge->GetPledgeName(), nullptr, nullptr, static_cast<std::int64_t>(pledge->GetDbId()),
            static_cast<std::int64_t>(pledge->GetPledgeLevel()), 0, 0, 0, 0, 0
        );
    }
    CheckPlayerNameString(user);
    return m_Entry.AddEntry(user);
}

bool CPledgeGameSystem::RegisterExitObserverEntry(User* const user) {
    // Current L2Server 0x009a9318.
    L2_GUARD_FUNCTION();

    const auto state = user->m_PledgeGameUser.m_StateMachine.GetStatus();
    const std::uint32_t fieldId = static_cast<std::uint32_t>(user->m_PledgeGameUser.m_FieldId);
    if (state == CPledgeGameUserStateMachine::PLEDGEGAME_PLAYER ||
        state == CPledgeGameUserStateMachine::PLEDGEGAME_PLAY_WAITING || fieldId >= 6U) {
        return false;
    }

    m_pPledgeGameFieldList[fieldId].RegisterMessage(4, static_cast<std::int32_t>(user->GetServerId()), 0, false);
    return true;
}

bool CPledgeGameSystem::RegisterObserverEntry(User* const user, const std::int32_t fieldId) {
    // Current L2Server 0x009ac4ec returns false after
    // dispatching the requested transition.
    L2_GUARD_FUNCTION();

    if (user == nullptr || static_cast<std::uint32_t>(fieldId) >= 6U) {
        return false;
    }
    const auto state = user->m_PledgeGameUser.m_StateMachine.GetStatus();
    if (state == CPledgeGameUserStateMachine::PLEDGEGAME_PLAYER ||
        state == CPledgeGameUserStateMachine::PLEDGEGAME_PLAY_WAITING) {
        return false;
    }

    CPledgeGameField& requestedField = m_pPledgeGameFieldList[fieldId];
    if (requestedField.m_nStatus < PledgeGameFieldStatus::WAIT_MATCH ||
        requestedField.m_nStatus > PledgeGameFieldStatus::FINISH_MATCH) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::OBSERVATION_TIME_EXPIRED);
        return false;
    }

    if (state == CPledgeGameUserStateMachine::PLEDGEGAME_OBSERVER) {
        const std::uint32_t currentFieldId = static_cast<std::uint32_t>(user->m_PledgeGameUser.m_FieldId);
        if (currentFieldId < 6U) {
            CPledgeGameField& currentField = m_pPledgeGameFieldList[currentFieldId];
            if (fieldId != currentField.m_nFieldId) {
                currentField.RegisterMessage(8, static_cast<std::int32_t>(user->GetServerId()), fieldId, false);
            }
        }
    } else {
        requestedField.AddObserver(static_cast<std::int32_t>(user->GetServerId()));
    }
    return false;
}

bool CPledgeGameSystem::RequestObserverList(User* const user) {
    // Current L2Server 0x009a39ac.
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->m_PledgeGameUser.IsPlayer()) {
        return false;
    }
    m_pledgeGameHtml.SendGameList(user);
    return true;
}

bool CPledgeGameSystem::PledgeGameHTML(User* const user) {
    // Current L2Server 0x009ad968.
    L2_GUARD_FUNCTION();
    if (user == nullptr || !m_Entry.CheckUser(user, true, false)) {
        return false;
    }

    if (g_PledgeGameSystem.IsRegisteredUser(user)) {
        m_pledgeGameHtml.SendCancelPage(user);
    } else {
        m_pledgeGameHtml.SendApplyPage(user);
    }
    return true;
}

void CPledgeGameSystem::SendRecordToUser(User* const user) {
    // Current L2Server 0x0099ca40.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        user->GetSocket()->SendSystemMessage_d(
            SystemMessageId::CEREMONY_OF_CHAOS_BATTLE_MARK_COUNT, user->m_PledgeGameUser.m_nPrizeCount
        );
    }
}

bool CPledgeGameSystem::UpdateTopRecord(const std::int32_t topRecord) {
    // Current L2Server 0x0099d1c4, original PledgeGameSystem.cpp owner.
    L2_GUARD_FUNCTION();
    SharedPledgeGameInfo* const info = SharedPledgeGameInfo::GetInfo();
    const bool updated = info->m_nTopRankerTotalPrize < topRecord;
    if (updated) {
        SharedPledgeGameInfo::GetInfo()->m_nTopRankerTotalPrize = topRecord;
    }
    return updated;
}

void CPledgeGameSystem::SetPledgeGameRecord(
    const std::int32_t season, const std::int32_t userSid, const std::int32_t prizeCount, const std::int32_t surrenderCount
) {
    // Current L2Server 0x0099eff0, original PledgeGameSystem.cpp owner.
    L2_GUARD_FUNCTION();
    if (SharedPledgeGameInfo::GetInfo()->m_nSeason != season) {
        return;
    }

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user != nullptr) {
        user->m_PledgeGameUser.m_nRecordSeason = season;
        user->m_PledgeGameUser.m_nPrizeCount = prizeCount;
        user->m_PledgeGameUser.m_nSurrenderCount = surrenderCount;
    }
}

const char16_t* CPledgeGameSystem::GetPlayersName(const std::int32_t country, const std::int32_t playerNumber) {
    if (playerNumber < 1 || playerNumber > 18) {
        return u"Player";
    }
    const auto position = m_mapPlayersName.find(country);
    if (position == m_mapPlayersName.end() || position->second == nullptr) {
        return u"Player";
    }
    const std::vector<std::u16string>& names = *position->second;
    const std::size_t index = static_cast<std::size_t>(playerNumber - 1);
    if (index >= names.size()) {
        return u"Player";
    }
    return runtime::CheckedVectorAt(names, index).c_str();
}

bool CPledgeGameSystem::IsRegisteredUser(User* const user) {
    L2_GUARD_FUNCTION();
    return user != nullptr && m_Entry.IsRegisteredUser(user, true);
}

bool CPledgeGameSystem::LeaveGame(User* const user, const bool includeWaitingPlayer) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }

    if (user->m_PledgeGameUser.IsPlaying() || (includeWaitingPlayer && user->m_PledgeGameUser.IsWaiting())) {
        IncreasePledgeGameSurrenderCount(static_cast<std::int32_t>(user->GetServerId()), 1);
        if (CPledge* const pledge = user->GetPledge()) {
            const std::int64_t guessed =
                g_MatchingPointDB.GuessMatchingPoint(MatchingPoint::MPT_BASE, PledgeGame::GRT_BASE, user->m_MatchingPoint);
            const std::int64_t validated = g_MatchingPointDB.ValidateMatchingPoint(
                MatchingPoint::MPT_BASE, user->m_MatchingPoint.m_nMatchingPointBase[0],
                user->m_MatchingPoint.m_nMatchingPointDiff[0], guessed
            );
            CLogFormatter formatter;
            formatter.WriteLog1(
                0x800, user, nullptr, pledge->GetPledgeName(), nullptr, nullptr,
                static_cast<std::int64_t>(pledge->GetDbId()), static_cast<std::int64_t>(pledge->GetPledgeLevel()), 1,
                user->m_PledgeGameUser.m_nSurrenderCount, validated, 0, 0
            );
        }
        if (CPledgeGameField* const field = user->m_PledgeGameUser.GetField()) {
            field->PlayerLeaving(static_cast<std::int32_t>(user->GetServerId()));
            return true;
        }
        return false;
    }

    if (m_Entry.IsRegisteredUser(user, true)) {
        return UnregisterEntry(user);
    }
    if (user->m_PledgeGameUser.IsObserver()) {
        if (CPledgeGameField* const field = user->m_PledgeGameUser.GetField()) {
            field->RegisterMessage(4, static_cast<std::int32_t>(user->GetServerId()), 0, false);
            return true;
        }
        return false;
    }

    if (CUserSocket* const socket = user->GetSocket()) {
        socket->Send("chd", 0xfe, 0x11f, 0);
    }
    return false;
}

bool CPledgeGameSystem::GiveItem(
    User* const user, const std::int32_t itemClassId, const std::int64_t count, const bool saveRecord
) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }

    InventoryParams parameters;
    parameters.nItemClassID = itemClassId;
    parameters.nCount = count;
    if (!user->AddItemToInventory(parameters)) {
        return false;
    }
    if (saveRecord) {
        user->m_PledgeGameUser.m_nPrizeCount += static_cast<std::int32_t>(count);
        SavePledgeGameRecord(static_cast<std::int32_t>(user->GetDBID()), static_cast<std::int32_t>(count));
    }
    return true;
}

std::int32_t CPledgeGameSystem::GiveItemToWinner(
    const std::int32_t userSid, const std::int32_t itemClassId, std::int32_t count, const bool force
) {
    L2_GUARD_FUNCTION();
    CSPointer<User> user = g_UserMap.FindObject(userSid);
    if (!user || (user->m_PledgeGameUser.m_nGameResult != PledgeGame::GRT_WIN && !force)) {
        return 0;
    }
    if (count == 0) {
        count = static_cast<std::int32_t>(::genrand64_real2() * 3.0 + 2.0);
    }
    return GiveItem(user.get(), itemClassId, count, true) ? count : 0;
}

void CPledgeGameSystem::RegisterPledgeGameMaker(const std::int32_t npcMakerId) {
    // Current L2Server 0x0099cf4c.
    L2_GUARD_FUNCTION();
    for (std::int32_t index = 0; index < 6; ++index) {
        CInstantZone* const zone = m_pPledgeGameFieldList[index].m_pInZone;
        if (zone != nullptr && zone->GetInstantZoneTypeID() == npcMakerId) {
            g_npcserver.Send("cdd", 0xb6, npcMakerId, static_cast<std::int32_t>(zone->Id()));
        }
    }
}

void CPledgeGameSystem::SetGotItemPledgeGame() {
    // Current L2Server 0x009a5074.
    L2_GUARD_FUNCTION();
    SharedPledgeGameInfo* const info = SharedPledgeGameInfo::GetInfo();
    db.SetGotItemPledgeGame(info->m_nSeason - 1);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, info->m_nPrevSeasonWinnerCharacterDbId));
    if (user) {
        user->ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_PLEDGEGAME});
        user->PledgeMissionCheckAndAddCount(PledgeMission::PMGT_PLEDGE_GAME_FIRST, 0, 1);
    }
}

void CPledgeGameSystem::SavePledgeGameRecord(const std::int32_t userDbId, const std::int32_t count) {
    L2_GUARD_FUNCTION();
    db.RequestUpdatePledgeGameResult(SharedPledgeGameInfo::GetInfo()->m_nSeason, userDbId, count);
}

void CPledgeGameSystem::IncreasePledgeGameSurrenderCount(const std::int32_t userSid, const std::int32_t amount) {
    L2_GUARD_FUNCTION();

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(std::uint32_t(userSid));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (!user) {
        return;
    }
    user->m_PledgeGameUser.m_nSurrenderCount =
        runtime::AddInt32Wrapping(user->m_PledgeGameUser.m_nSurrenderCount, amount);
    db.RequestIncreasePledgeGameSurrenderCount(
        SharedPledgeGameInfo::GetInfo()->m_nSeason, static_cast<int>(user->GetDBID()), amount
    );
}

std::int32_t CPledgeGameSystem::GetSurrenderCount(const std::int32_t userSid) {
    // Current L2Server 0x0099f0fc.
    L2_GUARD_FUNCTION();
    CSPointer<User> user = g_UserMap.FindObject(userSid);
    return user ? user->m_PledgeGameUser.m_nSurrenderCount : 0;
}

bool CPledgeGameSystem::UnregisterEntry(User* const user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || !m_Entry.RemoveEntry(user, true)) {
        return false;
    }

    if (m_bGetPenalty) {
        IncreasePledgeGameSurrenderCount(static_cast<int>(user->m_nId), 1);
        if (CPledge* const pledge = user->GetPledge()) {
            CLogFormatter formatter;
            formatter.WriteLog1(
                0x800, user, nullptr, pledge->GetPledgeName(), nullptr, nullptr,
                static_cast<std::int64_t>(pledge->GetDbId()), static_cast<std::int64_t>(pledge->GetPledgeLevel()), 0,
                user->m_PledgeGameUser.m_nSurrenderCount, 0, 0, 0
            );
        }
    }

    if (CPledgeGameField* const field = user->m_PledgeGameUser.GetField()) {
        field->PlayerLeaving(static_cast<int>(user->GetServerId()));
    }
    return true;
}

void CPledgeGameSystem::EndPledgeGameSeason(const bool setNextSeason) {
    L2_GUARD_FUNCTION();
    db.RequestEndPledgeGame(
        SharedPledgeGameInfo::GetInfo()->m_nSeason, runtime::crt::Time32Now(), 0, 0, setNextSeason ? 1 : 0
    );
}

bool CPledgeGameSystem::EndSeason(const bool setNextSeason) {
    L2_GUARD_FUNCTION();
    if (!SetStep(PledgeGameCondition::SEASON_ENDED)) {
        return false;
    }

    const std::int32_t season = SharedPledgeGameInfo::GetInfo()->m_nSeason;
    EndPledgeGameSeason(setNextSeason);
    CUserBroadcaster::inst().BroadcastToAllUser("chccd", 0x62, 0xe93, 1, 1, season);
    if (setNextSeason) {
        Init();
        CUserBroadcaster::inst().BroadcastToAllUser("chccd", 0x62, 0xe92, 1, 1, season + 1);
    }
    return true;
}

void CPledgeGameSystem::TimerExpired(int) {
    L2_GUARD_FUNCTION();

    if (!CheckTime()) {
        const std::int32_t now = runtime::crt::Time32Now();
        std::tm current{};
        std::tm season{};
        if (runtime::crt::LocalTime32(now, current) && runtime::crt::LocalTime32(m_tSeasonStart, season) &&
            current.tm_mon != season.tm_mon && CanChangeStep(PledgeGameCondition::SEASON_ENDED)) {
            EndSeason(true);
        }
        AddTimer(3600000, 0);
        m_TickCount += 3600;
        return;
    }

    if (runtime::raw_atomic::ExchangeI32(reinterpret_cast<volatile std::int32_t*>(&m_nMessageRegistered), 0) == 1) {
        SharedPledgeGameInfo* const info = SharedPledgeGameInfo::GetInfo();
        info->m_nSeason = m_msgReceived.nSeason;
        info->m_nStep = m_msgReceived.nStep;
        if (m_msgReceived.tSeasonStart != 0) {
            m_tSeasonStart = m_msgReceived.tSeasonStart;
        }
    }

    m_tNow = runtime::crt::Time32Now();
    SharedPledgeGameInfo* const info = SharedPledgeGameInfo::GetInfo();
    if (!m_bGetPenalty && info->m_nStep == 2 && m_tNow - m_OpenTime > 240) {
        m_bGetPenalty = true;
    }

    if (m_TickCount % 20 == 0) {
        std::tm current{};
        std::tm season{};
        if (runtime::crt::LocalTime32(m_tNow, current) && runtime::crt::LocalTime32(m_tSeasonStart, season)) {
            if (current.tm_mon != season.tm_mon && CanChangeStep(PledgeGameCondition::SEASON_ENDED)) {
                EndSeason(true);
            } else if (m_bStartGameAutomatically && m_Setting.IsPlayingTime(current.tm_hour * 60 + current.tm_min)) {
                {
                    L2_GUARD(m_PledgeGameLock);
                    if (current.tm_min == 0 || current.tm_min == 15 || current.tm_min == 30 || current.tm_min == 45) {
                        if (info->m_nStep == 1 || info->m_nStep == 7) {
                            OpenGame();
                        }
                    } else if ((current.tm_min == 5 || current.tm_min == 20 || current.tm_min == 35 ||
                                   current.tm_min == 50) &&
                        info->m_nStep == 2) {
                        PrepareGame();
                    }
                }
            }
        }
    }

    if (m_nDbProcessing == 0 && m_tSeasonStart == 0) {
        CheckAutoStart();
    }
    AddTimer(1000, 0);
    ++m_TickCount;
}

bool CPledgeGameUser::IsPlayer() {
    const CPledgeGameUserStateMachine::UserState status = m_StateMachine.GetStatus();
    return status == CPledgeGameUserStateMachine::PLEDGEGAME_PLAYER ||
        status == CPledgeGameUserStateMachine::PLEDGEGAME_PLAY_WAITING;
}

bool CPledgeGameUser::IsEnemyTo(CPledgeGameUser* target) {
    // Current L2Server.exe 0x0099e2c8.
    L2_GUARD_FUNCTION();

    if (target == nullptr || target->m_FieldId != m_FieldId) {
        return false;
    }
    const CPledgeGameUserStateMachine::UserState ownStatus = m_StateMachine.GetStatus();
    const CPledgeGameUserStateMachine::UserState targetStatus = target->m_StateMachine.GetStatus();
    const bool ownPlayer = ownStatus == CPledgeGameUserStateMachine::PLEDGEGAME_PLAYER ||
        ownStatus == CPledgeGameUserStateMachine::PLEDGEGAME_PLAY_WAITING;
    const bool targetPlayer = targetStatus == CPledgeGameUserStateMachine::PLEDGEGAME_PLAYER ||
        targetStatus == CPledgeGameUserStateMachine::PLEDGEGAME_PLAY_WAITING;
    if (!ownPlayer || !targetPlayer || target == this || ownStatus != targetStatus) {
        return false;
    }
    return GetField()->m_nStatus == PledgeGameFieldStatus::PROCESS_MATCH;
}

bool CPledgeGameUser::CanAttack() {
    // Current L2Server 0x0099e288.
    const CPledgeGameUserStateMachine::UserState status = m_StateMachine.GetStatus();
    if (status != CPledgeGameUserStateMachine::PLEDGEGAME_PLAYER &&
        status != CPledgeGameUserStateMachine::PLEDGEGAME_PLAY_WAITING) {
        return true;
    }
    CPledgeGameField* const field = GetField();
    return field == nullptr || field->m_nStatus == PledgeGameFieldStatus::PROCESS_MATCH ||
        m_nGameResult == PledgeGame::GRT_WIN;
}

bool CPledgeGameUser::IsWaiting() {
    return m_StateMachine.GetStatus() == CPledgeGameUserStateMachine::PLEDGEGAME_PLAY_WAITING;
}

bool CPledgeGameUser::IsObserver() { return m_StateMachine.GetStatus() == CPledgeGameUserStateMachine::PLEDGEGAME_OBSERVER; }

bool CPledgeGameUser::IsRegistered() {
    return m_StateMachine.GetStatus() == CPledgeGameUserStateMachine::PLEDGEGAME_ENTRY_REGISTER;
}

bool CPledgeGameUser::CanRegister() {
    // Current L2Server 0x0099dcec.
    L2_GUARD_FUNCTION();
    return m_StateMachine.CanChangeState(CPledgeGameUserStateMachine::PLEDGEGAME_ENTRY_REGISTER);
}

bool CPledgeGameUser::IsPlaying() { return m_StateMachine.GetStatus() == CPledgeGameUserStateMachine::PLEDGEGAME_PLAYER; }

void CPledgeGameUser::SetLifeTime(const std::int32_t lifeTime) {
    L2_GUARD_FUNCTION();
    m_nLifeTime = lifeTime;
}

void CPledgeGameUser::InitShuffleNo(const std::int32_t) {
    L2_GUARD_FUNCTION();
    // Current 0x0099baa8 ignores the play-count argument.
    m_nShuffleNo = 0;
}

std::int32_t CPledgeGameUser::GetShuffledPlayNo(const std::int32_t playNo) const {
    L2_GUARD_FUNCTION();
    std::int32_t shuffled = m_nShuffleNo + playNo;
    if (m_nPlayNoCount < shuffled) {
        shuffled -= m_nPlayNoCount;
    }
    return shuffled;
}

void CPledgeGameUser::SetPlayNo(const std::int32_t playNo) {
    L2_GUARD_FUNCTION();
    m_PlayNo = playNo;
}

bool CPledgeGameUser::WaitingPledgeGamePlaying() {
    L2_GUARD_FUNCTION();

    const CPledgeGameUserStateMachine::UserState originalStatus = m_StateMachine.GetStatus();
    if (!m_StateMachine.ChangeState(CPledgeGameUserStateMachine::PLEDGEGAME_PLAY_WAITING)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] To change status is failed[%s]. orgStatus(%d) "
        //     L"pvpMatchType(%d). pvpPlayerState(%d))",
        //     L"PledgeGameSystem.cpp",
        //     0x33c,
        //     m_pUser != nullptr ? m_pUser->GetName() : L"",
        //     static_cast<int>(originalStatus),
        //     m_pUser != nullptr
        //         ? static_cast<int>(m_pUser->m_data->m_PVPMatchType)
        //         : 0,
        //     m_pUser != nullptr
        //         ? static_cast<int>(m_pUser->m_data->m_PVPMatchPlayerState)
        //         : 0)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] To change status is failed[%s]. orgStatus(%d) "
        //     L"pvpMatchType(%d). pvpPlayerState(%d))",
        //     L"PledgeGameSystem.cpp",
        //     0x33c,
        //     m_pUser != nullptr ? m_pUser->GetName() : L"",
        //     static_cast<int>(originalStatus),
        //     m_pUser != nullptr
        //         ? static_cast<int>(m_pUser->m_data->m_PVPMatchType)
        //         : 0,
        //     m_pUser != nullptr
        //         ? static_cast<int>(m_pUser->m_data->m_PVPMatchPlayerState)
        //         : 0)
        // L2_LOG_ERROR(Log,
        //     "Cannot enter clan-game play-waiting state for '%s': original status %d, PvP match type %d, PvP player state
        //     %d.", m_pUser == nullptr ? "<unavailable>" : WideToUtf8(m_pUser->GetName()).c_str(),
        //     static_cast<int>(originalStatus), m_pUser == nullptr ? 0 : static_cast<int>(m_pUser->m_data->m_PVPMatchType),
        //     m_pUser == nullptr ? 0 : static_cast<int>(m_pUser->m_data->m_PVPMatchPlayerState))
        L2_LOG_ERROR(
            Log,
            "Cannot enter clan-game play-waiting state for '%s': original status %d, PvP match type %d, PvP player state "
            "%d.",
            m_pUser == nullptr ? "<unavailable>" : Utf16ToUtf8(m_pUser->GetName()).c_str(), static_cast<int>(originalStatus),
            m_pUser == nullptr ? 0 : static_cast<int>(m_pUser->m_data->m_PVPMatchType),
            m_pUser == nullptr ? 0 : static_cast<int>(m_pUser->m_data->m_PVPMatchPlayerState)
        );
        return false;
    }

    m_pUser->m_data->m_PVPMatchType = PVPMatchType::PLEDGEGAME;
    m_pUser->m_data->m_PVPMatchPlayerState = PVPMatchPlayerState::WAITING;
    return true;
}

bool CPledgeGameUser::WaitingPledgeGameObserving() {
    L2_GUARD_FUNCTION();
    return m_StateMachine.ChangeState(CPledgeGameUserStateMachine::PLEDGEGAME_OB_WAITING);
}

void CPledgeGameUser::LeaveParty() {
    L2_GUARD_FUNCTION();

    CParty* const party = m_pUser->GetParty();
    if (party == nullptr) {
        return;
    }
    if (m_pUser->IsPartyMaster()) {
        if (party->GetMemberCount() < 3) {
            party->Dismiss(false, false);
            return;
        }
        User* const candidate = party->GetMasterCandidate();
        if (candidate == nullptr) {
            return;
        }
        party->HandOverMaster(candidate);
    } else if (party->GetMemberCount() < 3) {
        party->Dismiss(false, false);
        return;
    }
    party->Withdraw(m_pUser, false);
}

void CPledgeGameUser::SetEnterInZone(CInstantZone* const instantZone, const std::int32_t startPositionIndex) {
    L2_GUARD_FUNCTION();
    m_pInstantZone = instantZone;
    m_nStartPosIndex = startPositionIndex;
}

bool CPledgeGameUser::SetReturnPos(FVector& position, const bool force) {
    L2_GUARD_FUNCTION();
    if (!m_ReturnPos.IsZero() && !force) {
        return false;
    }
    m_ReturnPos = position;
    return true;
}

bool CPledgeGameUser::RegisterPledgeGameEntry() {
    L2_GUARD_FUNCTION();
    if (!m_StateMachine.ChangeState(CPledgeGameUserStateMachine::PLEDGEGAME_ENTRY_REGISTER)) {
        return false;
    }
    m_pUser->m_data->m_PVPMatchType = PVPMatchType::PLEDGEGAME;
    m_pUser->m_data->m_PVPMatchPlayerState = PVPMatchPlayerState::WAITING;
    return true;
}

void CPledgeGameUser::ReadyForMatch(CInstantZone* const instantZone, const std::int32_t startPositionIndex) {
    L2_GUARD_FUNCTION();
    if (instantZone == nullptr) {
        return;
    }

    if (m_ReturnPos.IsZero()) {
        FVector position = m_pUser->GetPos();
        SetReturnPos(position, false);
    }
    if (m_pUser->IsInDuel()) {
        CDuelManager::inst().CancelDuel(m_pUser, 0);
    }
    m_pUser->TryDismounting(true, false);
    SetEnterInZone(instantZone, startPositionIndex);
    m_pUser->DespawnPet(true, true, DESPAWN_PET_PLEDGEGAME_PLAYER, false);
    if (m_pUser->m_bDominionRenamed) {
        m_pUser->RenameByDominionSiege(false, true);
    }
    runtime::raw_atomic::ExchangeI32(&m_nInField, 1);
}

bool CPledgeGameUser::SetPlayer(
    const std::int32_t fieldId, CInstantZone* const instantZone, const std::int32_t startPositionIndex
) {
    L2_GUARD_FUNCTION();
    if (fieldId >= 6) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][%s] nFieldId(%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x2f4,
        //     L"CPledgeGameUser::SetPlayer",
        //     fieldId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][%s] nFieldId(%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x2f4,
        //     L"CPledgeGameUser::SetPlayer",
        //     fieldId)
        L2_LOG_ERROR(Log, "Cannot assign a clan-game player: field index %d is at least 6.", fieldId);
        return false;
    }
    if (instantZone == nullptr || instantZone->IsDeleting() || instantZone->m_Deleted != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][%s] nFieldId(%d) No InZone",
        //     L"PledgeGameSystem.cpp",
        //     0x2fa,
        //     L"CPledgeGameUser::SetPlayer",
        //     fieldId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][%s] nFieldId(%d) No InZone",
        //     L"PledgeGameSystem.cpp",
        //     0x2fa,
        //     L"CPledgeGameUser::SetPlayer",
        //     fieldId)
        L2_LOG_ERROR(
            Log, "Cannot assign a player to clan-game field %d: the instance is missing or being deleted.", fieldId
        );
        return false;
    }
    if (!m_StateMachine.ChangeState(CPledgeGameUserStateMachine::PLEDGEGAME_PLAY_WAITING)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][%s] nStatus(%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x300,
        //     L"CPledgeGameUser::SetPlayer",
        //     static_cast<int>(m_StateMachine.GetStatus()))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][%s] nStatus(%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x300,
        //     L"CPledgeGameUser::SetPlayer",
        //     static_cast<int>(m_StateMachine.GetStatus()))
        L2_LOG_ERROR(
            Log, "Cannot assign a player to clan-game field %d: transition to play-waiting failed (observed status %d).",
            fieldId, static_cast<int>(m_StateMachine.GetStatus())
        );
        return false;
    }

    LeaveParty();
    RegisterPledgeGameEntry();
    m_FieldId = fieldId;
    SetPlayNo(startPositionIndex);
    ReadyForMatch(instantZone, startPositionIndex);
    return true;
}

bool CPledgeGameUser::SetObserver(const std::int32_t fieldId, CInstantZone* const instantZone) {
    L2_GUARD_FUNCTION();
    if (static_cast<std::uint32_t>(fieldId) >= 6U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][%s] nFieldId(%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x265,
        //     L"CPledgeGameUser::SetObserver",
        //     fieldId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][%s] nFieldId(%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x265,
        //     L"CPledgeGameUser::SetObserver",
        //     fieldId)
        L2_LOG_ERROR(Log, "Cannot assign a clan-game observer: field index %d is outside 0 through 5.", fieldId);
        return false;
    }
    if (instantZone == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][%s] No Pledge Game Inzone (%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x26b,
        //     L"CPledgeGameUser::SetObserver",
        //     fieldId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][%s] No Pledge Game Inzone (%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x26b,
        //     L"CPledgeGameUser::SetObserver",
        //     fieldId)
        L2_LOG_ERROR(Log, "Cannot assign an observer to clan-game field %d: the instance is unavailable.", fieldId);
        return false;
    }
    if (!m_StateMachine.ChangeState(CPledgeGameUserStateMachine::PLEDGEGAME_OBSERVER)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][%s] nStatus(%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x271,
        //     L"CPledgeGameUser::SetObserver",
        //     static_cast<int>(m_StateMachine.GetStatus()))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][%s] nStatus(%d)",
        //     L"PledgeGameSystem.cpp",
        //     0x271,
        //     L"CPledgeGameUser::SetObserver",
        //     static_cast<int>(m_StateMachine.GetStatus()))
        L2_LOG_ERROR(
            Log,
            "Cannot assign an observer to clan-game field %d: transition to observer state failed (observed status %d).",
            fieldId, static_cast<int>(m_StateMachine.GetStatus())
        );
        return false;
    }

    LeaveParty();
    m_FieldId = fieldId;
    if (m_pUser->IsInRealWorld()) {
        FVector position = m_pUser->GetPos();
        if (SetReturnPos(position, false)) {
            db.SendSaveCharacterInfo(m_pUser, false, true);
        }
    }
    m_pUser->DespawnAllSummon(true, false);
    m_pUser->TryDismounting(true, false);
    SetEnterInZone(instantZone, 6);
    m_pUser->DespawnPet(true, true, DESPAWN_PET_PLEDGEGAME_OBSERVER, false);
    return true;
}

void CPledgeGameUser::SetAlive(const bool alive) {
    L2_GUARD_FUNCTION();
    if (m_IsAlive != alive) {
        if (alive) {
            CPledgeGameField* const field = GetField();
            if (field == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d][%s] invalid field id(%d)",
                //     L"PledgeGameSystem.cpp",
                //     0x38f,
                //     L"CPledgeGameUser::SetAlive",
                //     m_FieldId)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d][%s] invalid field id(%d)",
                //     L"PledgeGameSystem.cpp",
                //     0x38f,
                //     L"CPledgeGameUser::SetAlive",
                //     m_FieldId)
                L2_LOG_ERROR(
                    Log,
                    "Cannot broadcast the player revival for clan-game field index %d: no field is available; the local "
                    "alive flag is still updated.",
                    m_FieldId
                );
            } else {
                field->Broadcast("cd", 1, static_cast<std::int32_t>(m_pUser->m_nId));
            }
        }
        m_IsAlive = alive;
    }
}

bool CPledgeGameUser::PledgeGameStatusReset() {
    L2_GUARD_FUNCTION();
    m_ReturnPos = FVector{};
    m_MovePos = FVector{};
    m_FieldId = -1;
    m_PlayNo = 0;
    m_Damage = 0;
    m_IsAlive = true;
    m_nStartPosIndex = 0;
    m_nKillCount = 0;
    m_nLifeTime = 300;
    m_bGotInvitingMessage = false;
    m_nGameResult = PledgeGame::GRT_NONE;
    m_pInstantZone = nullptr;
    runtime::raw_atomic::ExchangeI32(&m_nInField, 0);
    InitShuffleNo(0);
    if (!m_StateMachine.ChangeState(CPledgeGameUserStateMachine::PLEDGEGAME_NOT_USER)) {
        return false;
    }
    m_pUser->m_data->m_PVPMatchType = PVPMatchType::NONE;
    m_pUser->m_data->m_PVPMatchPlayerState = PVPMatchPlayerState::NONE;
    return true;
}

void CPledgeGameUser::ChangeObserverField(const std::int32_t fieldId) {
    L2_GUARD_FUNCTION();
    if (m_StateMachine.GetStatus() != CPledgeGameUserStateMachine::PLEDGEGAME_OBSERVER ||
        static_cast<std::uint32_t>(fieldId) >= 6U) {
        return;
    }
    CPledgeGameField* const field = g_PledgeGameSystem.m_pPledgeGameFieldList + fieldId;
    if (field != nullptr && fieldId != m_FieldId) {
        field->AddObserver(static_cast<std::int32_t>(m_pUser->m_nId));
    }
}

CPledgeGameField* CPledgeGameUser::GetField() {
    L2_GUARD_FUNCTION();
    if (static_cast<std::uint32_t>(m_FieldId) >= 6U) {
        return nullptr;
    }
    return g_PledgeGameSystem.m_pPledgeGameFieldList + m_FieldId;
}

const char16_t* CPledgeGameUser::GetAliasName(User* const viewer) {
    L2_GUARD_FUNCTION();

    std::int32_t country = 0;
    std::int32_t number = m_PlayNo;
    if (viewer == nullptr || (!viewer->m_PledgeGameUser.IsPlayer() && !viewer->m_PledgeGameUser.IsObserver())) {
        if (m_pUser != nullptr && m_pUser->m_data != nullptr) {
            country = m_pUser->m_data->m_CountryCode;
        }
    } else {
        if (viewer->m_data != nullptr) {
            country = viewer->m_data->m_CountryCode;
        }
        number = viewer->m_PledgeGameUser.GetShuffledPlayNo(m_PlayNo);
    }
    return g_PledgeGameSystem.GetPlayersName(country, number);
}

void CPledgeGameUser::UnequipForbiddenWeapon() {
    L2_GUARD_FUNCTION();

    std::array<CItem*, 64> forbiddenItems{};
    std::size_t forbiddenItemCount = 0;

    {
        L2_READ_GUARD(m_pUser->m_inv);
        for (const std::int32_t itemIndex : m_pUser->m_data->m_nSlotIndex) {
            CItem* const item = m_pUser->m_inv.GetByIndex(itemIndex);
            if (item != nullptr && item->m_const != nullptr && item->m_const->m_bCanNotPledgeGameUse) {
                forbiddenItems[forbiddenItemCount++] = item;
            }
        }
    }

    for (std::size_t index = 0; index < forbiddenItemCount; ++index) {
        m_pUser->UnequipItem(forbiddenItems[index], false);
    }
}

void CPledgeGameUser::ActivateSkill(const std::int32_t skillId, const std::int32_t skillLevel, const bool summonNpc) {
    L2_GUARD_FUNCTION();

    const CSkillInfo* const skillInfo = g_SkillDB.GetSkillInfo(CSkillKey(skillId, skillLevel, 0), nullptr);
    if (skillInfo == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][%s]PledgeGameSystem: Skill(ID:%d,LV:%d) "
        //     L"was not found.",
        //     L"PledgeGameSystem.cpp",
        //     0x462,
        //     L"CPledgeGameUser::ActivateSkill",
        //     skillId,
        //     skillLevel)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][%s]PledgeGameSystem: Skill(ID:%d,LV:%d) "
        //     L"was not found.",
        //     L"PledgeGameSystem.cpp",
        //     0x462,
        //     L"CPledgeGameUser::ActivateSkill",
        //     skillId,
        //     skillLevel)
        L2_LOG_ERROR(
            Log,
            "Cannot activate clan-game skill %d level %d: no definition is registered; reporting the stack and returning.",
            skillId, skillLevel
        );
        runtime::process::ReportCallStack(2, false);
        return;
    }

    if (!m_pUser->InfectedSkillBy(nullptr, skillInfo, nullptr, 0)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]PledgeGameSystem: Skill(ID:%d,LV:%d) "
        //     L"was not activated.",
        //     L"PledgeGameSystem.cpp",
        //     0x447,
        //     L"CPledgeGameUser::ActivateSkill",
        //     skillId,
        //     skillLevel)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d]PledgeGameSystem: Skill(ID:%d,LV:%d) "
        //     L"was not activated.",
        //     L"PledgeGameSystem.cpp",
        //     0x447,
        //     L"CPledgeGameUser::ActivateSkill",
        //     skillId,
        //     skillLevel)
        L2_LOG_ERROR(
            Log,
            "Clan-game skill %d level %d was not applied to the player; reporting the stack. Application to summoned "
            "doppelgangers still proceeds when requested.",
            skillId, skillLevel
        );
        runtime::process::ReportCallStack(2, false);
    }

    if (!summonNpc) {
        return;
    }

    std::vector<CNPC*> summonedNpcs;
    m_pUser->GetAllSummonedNpc(summonedNpcs);
    for (CNPC* const npc : summonedNpcs) {
        if (npc != nullptr && npc->IsDoppelganger()) {
            npc->InfectedSkillBy(nullptr, skillInfo, nullptr, 0);
        }
    }
}

void CPledgeGameUser::ActivateSkillForConceal() {
    L2_GUARD_FUNCTION();
    ActivateSkill(0x1bcb, 1, true);
}

void CPledgeGameUser::ActivateSkillForEveryUser() {
    L2_GUARD_FUNCTION();
    ActivateSkill(0x2544, 1, false);
    ActivateSkill(0x2545, 1, false);
    ActivateSkill(0x4a9e, 1, false);
}

void CPledgeGameUser::ActivateSkillForSurvivor() {
    L2_GUARD_FUNCTION();
    ActivateSkill(0x2546, 1, false);
}

void CPledgeGameUser::ObserverHide(const bool hide) {
    L2_GUARD_FUNCTION();

    m_pUser->m_bCameraMode = hide;
    m_pUser->ValidateOrgSpeed();
    if (hide) {
        if (!m_pUser->m_data->m_bGMInvisible) {
            {
                L2_GUARD(*m_pUser->m_lock);
                m_pUser->m_data->m_bGMInvisible = true;
            }
        }
        m_pUser->DoNothing();
        if (g_InstantZoneMgr.EnterInstantZone(
                m_pUser, m_pInstantZone, IZET_INDIVIDUAL, m_nStartPosIndex, false, 0, TeleportSourceType::InstantAgit
            )) {
            m_pUser->GetSocket()->Send("ch", 0xfe, 0x120);
            m_pUser->GetSocket()->Send("chc", 0xfe, 0x127, 0);
            if (CPledgeGameField* const field = GetField()) {
                field->RegisterMessage(9, m_pUser->GetServerId(), 0, false);
            }
        } else {
            m_pUser->m_bCameraMode = false;
            m_pUser->ValidateOrgSpeed();
            EscapeFromPledgeGame();
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][%s] Observing PledgeGame Zone Failed(%s) "
            //     L"current inzone type Id[%d]",
            //     L"PledgeGameSystem.cpp",
            //     0x48d,
            //     L"CPledgeGameUser::ObserverHide",
            //     m_pUser->GetName(),
            //     m_pUser->m_nInstantZoneTypeID)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d][%s] Observing PledgeGame Zone Failed(%s) "
            //     L"current inzone type Id[%d]",
            //     L"PledgeGameSystem.cpp",
            //     0x48d,
            //     L"CPledgeGameUser::ObserverHide",
            //     m_pUser->GetName(),
            //     m_pUser->m_nInstantZoneTypeID)
            // L2_LOG_ERROR(Log,
            //     "Clan-game observer entry failed for '%s'. Camera mode was disabled and escape handling invoked. Current
            //     instance type after escape: %d.", WideToUtf8(m_pUser->GetName()).c_str(), m_pUser->m_nInstantZoneTypeID)
            L2_LOG_ERROR(
                Log,
                "Clan-game observer entry failed for '%s'; camera mode was disabled and escape handling invoked. Current "
                "instance type after escape: %d.",
                Utf16ToUtf8(m_pUser->GetName()).c_str(), m_pUser->m_nInstantZoneTypeID
            );
        }
        return;
    }

    if (m_pUser->m_data->m_bGMInvisible && !m_pUser->IsBuilder()) {
        {
            L2_GUARD(*m_pUser->m_lock);
            m_pUser->m_data->m_bGMInvisible = false;
        }
    }
    m_pUser->DoNothing();
    if (!m_pUser->IsInRealWorld()) {
        if (!IsPlayer() && !IsWaiting()) {
            const FVector returnPosition = m_ReturnPos;
            CInstantZone* const zone = m_pUser->GetInstantZone();
            if (zone == nullptr) {
                if (!m_pUser->TeleportToLocation(
                        static_cast<int>(returnPosition.x), static_cast<int>(returnPosition.y),
                        static_cast<int>(returnPosition.z), 0, 0, TeleportSourceType::GoPoint, nullptr, 0
                    )) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"TeleportToLocation Fail in ObserverHide UserName: "
                    //     L"%s RetPos %d %d %d",
                    //     m_pUser->GetName(),
                    //     static_cast<int>(returnPosition.x),
                    //     static_cast<int>(returnPosition.y),
                    //     static_cast<int>(returnPosition.z))
                    // L2_LOG_ERROR(Log,
                    //     L"TeleportToLocation Fail in ObserverHide UserName: "
                    //     L"%s RetPos %d %d %d",
                    //     m_pUser->GetName(),
                    //     static_cast<int>(returnPosition.x),
                    //     static_cast<int>(returnPosition.y),
                    //     static_cast<int>(returnPosition.z))
                    // L2_LOG_ERROR(Log,
                    //     "Clan-game observer return teleport failed for '%s'. Saved return position (%.17g, %.17g, %.17g),
                    //     passed to movement as integers. Observer cleanup continues.",
                    //     WideToUtf8(m_pUser->GetName()).c_str(), returnPosition.x, returnPosition.y, returnPosition.z)
                    L2_LOG_ERROR(
                        Log,
                        "Clan-game observer return teleport failed for '%s'; saved return position (%.17g, %.17g, %.17g), "
                        "passed to movement as integers. Observer cleanup continues.",
                        Utf16ToUtf8(m_pUser->GetName()).c_str(), returnPosition.x, returnPosition.y, returnPosition.z
                    );
                }
            } else {
                if (!zone->LeaveZone(
                        m_pUser, true, static_cast<int>(returnPosition.x), static_cast<int>(returnPosition.y),
                        static_cast<int>(returnPosition.z), 0
                    )) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"LeaveZone Fail UserName: %s, ReturnPos: %d %d %d",
                    //     m_pUser->GetName(),
                    //     static_cast<int>(returnPosition.x),
                    //     static_cast<int>(returnPosition.y),
                    //     static_cast<int>(returnPosition.z))
                    // L2_LOG_ERROR(Log,
                    //     L"LeaveZone Fail UserName: %s, ReturnPos: %d %d %d",
                    //     m_pUser->GetName(),
                    //     static_cast<int>(returnPosition.x),
                    //     static_cast<int>(returnPosition.y),
                    //     static_cast<int>(returnPosition.z))
                    // L2_LOG_ERROR(Log,
                    //     "Clan-game observer instance exit failed for '%s'. Saved return position (%.17g, %.17g, %.17g),
                    //     passed to movement as integers. Observer cleanup continues.",
                    //     WideToUtf8(m_pUser->GetName()).c_str(), returnPosition.x, returnPosition.y, returnPosition.z)
                    L2_LOG_ERROR(
                        Log,
                        "Clan-game observer instance exit failed for '%s'; saved return position (%.17g, %.17g, %.17g), "
                        "passed to movement as integers. Observer cleanup continues.",
                        Utf16ToUtf8(m_pUser->GetName()).c_str(), returnPosition.x, returnPosition.y, returnPosition.z
                    );
                }
                m_pUser->GetSocket()->Send("ch", 0xfe, 0x121);
            }
        }
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][%s] Pledge Game observer is not in inzone ",
        //     L"PledgeGameSystem.cpp",
        //     0x4a0,
        //     L"CPledgeGameUser::ObserverHide")
        // Noise: Observer cleanup can run after the player has already returned to the real world. No additional instance
        // exit is needed. L2_LOG_ERROR(Log,
        //     L"[%s][%d][%s] Pledge Game observer is not in inzone ",
        //     L"PledgeGameSystem.cpp",
        //     0x4a0,
        //     L"CPledgeGameUser::ObserverHide")
    }
    m_pUser->GetSocket()->Send("chc", 0xfe, 0x127, 1);
    runtime::raw_atomic::CompareExchangeI32(&m_pUser->m_nRelationChanged, 1, 0);
}

void CPledgeGameUser::CompleteToDespawnPet(const bool observer) {
    L2_GUARD_FUNCTION();

    if (observer) {
        ObserverHide(true);
        return;
    }

    m_pUser->DispelByBuffProtectionFlag(2);
    {
        L2_GUARD(*m_pUser->m_lock);
        m_pUser->m_data->RestoreCP();
        m_pUser->m_data->RestoreHP();
        m_pUser->m_data->RestoreMP();
        m_pUser->m_data->UltimateSkillGauge(500);
    }
    m_pUser->ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_PLEDGEGAME});
    db.SendSaveCharacterInfo(m_pUser, false, true);

    if (!g_bUseAutoSoulShotUI) {
        m_pUser->GetSocket()->Send("chddd", 0xfe, 0x0c, 0, 0, -1);
    }

    UnequipForbiddenWeapon();

    {
        // The generated traversal macro ends before EraseCubicNotSelfCast in
        // current 0x009a9748, so keep its original lexical lifetime.
        FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
            m_pUser, summon,
            if (static_cast<std::int32_t>(summon->m_data->m_Race) == 0x18) {
                m_pUser->DespawnSummon(summon, true, false);
                continue;
            }

            summon->DispelByBuffProtectionFlag(2);
            {
                L2_GUARD(*summon->m_lock);
                summon->m_data->RestoreHP();
                summon->m_data->RestoreMP();
            }
        );
    }

    m_pUser->EraseCubicNotSelfCast();
    WOlympiadDefaultSetting::inst().ResetSkillReuseDelay(m_pUser);
    m_Damage = 0;
    m_pUser->DoNothing();
    m_pUser->SendBasicActionList();
    m_pUser->m_nTimeActivatePledgeSkill = 0;

    const bool entered = g_InstantZoneMgr.EnterInstantZone(
        m_pUser, m_pInstantZone, IZET_INDIVIDUAL, m_nStartPosIndex, true, 0, TeleportSourceType::PledgeGameStart
    );
    ActivateSkillForConceal();
    m_pUser->GetSocket()->Send("ch", 0xfe, 0x120);

    if (!entered) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][%s] Player failed to enter into inzone "
        //     L"Field ID(%d)",
        //     L"PledgeGameSystem.cpp",
        //     600,
        //     L"CPledgeGameUser::CompleteToDespawnPet",
        //     m_FieldId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][%s] Player failed to enter into inzone "
        //     L"Field ID(%d)",
        //     L"PledgeGameSystem.cpp",
        //     600,
        //     L"CPledgeGameUser::CompleteToDespawnPet",
        //     m_FieldId)
        L2_LOG_ERROR(
            Log,
            "Player entry to clan-game field %d failed; post-entry conceal-skill and client-notification calls have already "
            "run.",
            m_FieldId
        );
    }
}

void CPledgeGameUser::EscapeFromPledgeGame() {
    L2_GUARD_FUNCTION();

    if (IsObserver()) {
        ObserverHide(false);
        if (!PledgeGameStatusReset()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][%s] Status(%d)",
            //     L"PledgeGameSystem.cpp",
            //     0x3c0,
            //     L"CPledgeGameUser::EscapeFromPledgeGame",
            //     static_cast<int>(m_StateMachine.GetStatus()))
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d][%s] Status(%d)",
            //     L"PledgeGameSystem.cpp",
            //     0x3c0,
            //     L"CPledgeGameUser::EscapeFromPledgeGame",
            //     static_cast<int>(m_StateMachine.GetStatus()))
            L2_LOG_ERROR(
                Log,
                "Clan-game observer cleanup could not transition to NOT_USER (observed status %d); local game fields were "
                "already reset.",
                static_cast<int>(m_StateMachine.GetStatus())
            );
        }
    } else {
        if (!m_pUser->IsInRealWorld()) {
            {
                L2_GUARD(*m_pUser->m_lock);
                m_pUser->m_data->UltimateSkillGauge(0);
                m_pUser->m_data->RestoreCP();
                m_pUser->m_data->RestoreHP();
                m_pUser->m_data->RestoreMP();
            }
            m_pUser->ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_PLEDGEGAME});

            {
                FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(m_pUser, summon, {
                    L2_GUARD(*summon->m_lock);
                    summon->m_data->RestoreHP();
                    summon->m_data->RestoreMP();
                });
            }

            if (m_nInField != 0) {
                CInstantZone* const zone = m_pUser->GetInstantZone();
                if (zone != nullptr && zone->GetInstantZoneTypeID() == IZT_PLEDGEGAME) {
                    const FVector returnPosition = m_ReturnPos;
                    zone->LeaveZone(
                        m_pUser, true, static_cast<int>(returnPosition.x), static_cast<int>(returnPosition.y),
                        static_cast<int>(returnPosition.z), 1
                    );
                }
                runtime::raw_atomic::ExchangeI32(&m_nInField, 0);
                m_pUser->GetSocket()->Send("ch", 0xfe, 0x121);
            }
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][%s] Pledge game user is not in inzone (%d)",
            //     L"PledgeGameSystem.cpp",
            //     0x3c7,
            //     L"CPledgeGameUser::EscapeFromPledgeGame",
            //     static_cast<int>(m_StateMachine.GetStatus()))
            // Noise: Escape cleanup can run while the player is already in the real world. The remaining state reset still
            // runs. L2_LOG_ERROR(Log,
            //     L"[%s][%d][%s] Pledge game user is not in inzone (%d)",
            //     L"PledgeGameSystem.cpp",
            //     0x3c7,
            //     L"CPledgeGameUser::EscapeFromPledgeGame",
            //     static_cast<int>(m_StateMachine.GetStatus()))
        }

        if (m_pUser->IsCameraMode()) {
            ObserverHide(false);
        }
        if (!PledgeGameStatusReset()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][%s] Status(%d)",
            //     L"PledgeGameSystem.cpp",
            //     0x3f2,
            //     L"CPledgeGameUser::EscapeFromPledgeGame",
            //     static_cast<int>(m_StateMachine.GetStatus()))
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d][%s] Status(%d)",
            //     L"PledgeGameSystem.cpp",
            //     0x3f2,
            //     L"CPledgeGameUser::EscapeFromPledgeGame",
            //     static_cast<int>(m_StateMachine.GetStatus()))
            L2_LOG_ERROR(
                Log,
                "Clan-game player cleanup could not transition to NOT_USER (observed status %d); local game fields were "
                "already reset.",
                static_cast<int>(m_StateMachine.GetStatus())
            );
        }
        if (CPledge* const pledge = m_pUser->GetPledge()) {
            pledge->AddPledgeSkillsTo(m_pUser, true);
        }
        if (static_cast<int>(m_pUser->m_data->m_nClass) == 0 && m_pUser->IsHaveMentee() &&
            m_pUser->m_Mentor.m_nCanGetBuff > 0) {
            m_pUser->m_Mentor.m_nInfectedSkillByMentee = 0;
        }
        m_pUser->SendBasicActionList();
        m_pUser->SetVisible(true);

        {
            FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
                m_pUser, summon, summon->SetVisible(true); summon->SetVisibleBySkill(true, true); summon->StopMove();
                summon->DoNothing();
            );
        }
        m_pUser->DispelBySlot(ABNS_CURIOUS_HOUSE, nullptr, -1, nullptr, SPET_ETC, false);
        if (m_pUser->IsInvisibleBySkill()) {
            m_pUser->SetVisibleBySkill(true, true);
        }
    }

    m_pUser->GetSocket()->Send("chd", 0xfe, 0x11f, 0);
}

bool CPledgeGameSystem::IsValidUser(User* const user, std::int32_t& reason) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    if (!user->IsAlive()) {
        reason = SystemMessageId::CANNOT_ENTER_REQUIREMENTS_NOT_MET;
        return false;
    }

    const std::int32_t level = user->m_data->m_nLevel;
    const std::int32_t classId = static_cast<std::int32_t>(user->m_data->m_nClass);
    if (level < 85 || !((classId >= 148 && classId <= 181) || (classId >= 188 && classId <= 189))) {
        reason = SystemMessageId::MATCH_PARTICIPATION_REQUIRES_LEVEL_85;
        return false;
    }
    if (user->IsTransformed(TransformType::Flying) != nullptr) {
        reason = SystemMessageId::CANNOT_ENTER_CEREMONY_OF_CHAOS_WHILE_FLYING_TRANSFORMED;
        return false;
    }
    if (user->m_PledgeGameUser.m_nSurrenderCount >= 30) {
        reason = SystemMessageId::CEREMONY_OF_CHAOS_PARTICIPATION_BLOCKED_AFTER_REPEATED_FORFEITS;
        return false;
    }
    if (user->m_bIsObserving) {
        reason = SystemMessageId::CANNOT_ACT_WHILE_OBSERVING;
        return false;
    }
    if (user->m_bEinhasadeTransformed || user->m_data->m_nKarma < 0) {
        reason = SystemMessageId::CANNOT_JOIN_WAITING_LIST_WITH_CURSED_WEAPON_OR_CHAOTIC_STATUS;
        return false;
    }
    if (!user->IsInRealWorld()) {
        reason = SystemMessageId::CANNOT_REGISTER_WHILE_IN_INSTANCE;
        return false;
    }

    const FVector position = user->GetPos();
    if (CAreaDB::inst().IsFishingZone(position, 0)) {
        reason = SystemMessageId::CANNOT_ENTER_CEREMONY_OF_CHAOS_FROM_FISHING_AREA;
        return false;
    }

    const std::int32_t n = user->m_inv.GetTotalCount();
    if (static_cast<float>(user->GetInventoryLimit()) * 0.9F < static_cast<float>(n) ||
        // user->CastCreature()->m_nCurrentWeight > 2) {
        user->CastCreature()->m_nCurrentWeight > WEIGHT_HEAVY) {
        // BUG in shipping EXE at 009ac302: ordinary inventory checked, quest inventory reported.
        // reason = SystemMessageId::QUEST_INVENTORY_QUANTITY_MUST_BE_BELOW_90_PERCENT
        reason = SystemMessageId::INVENTORY_WEIGHT_MUST_BE_BELOW_80_AND_QUANTITY_BELOW_90_PERCENT;
        return false;
    }

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr || !pledge->GetFunction()->IsEnable(PledgeFunctionType::PLEDGE_GAME)) {
        reason = SystemMessageId::MATCH_PARTICIPATION_REQUIRES_MEMBERSHIP_IN_LEVEL_THREE_CLAN;
        return false;
    }
    if (user->m_bBeautyShop) {
        // BUG in shipping EXE at 009ac22e: beauty-shop rejection reports shutdown, missing $s1.
        // reason = SystemMessageId::SERVER_SHUTDOWN_IN_SECONDS, // Current 009ac22e: ID 1 on beauty-shop rejection.
        reason = SystemMessageId::CANNOT_REGISTER_OR_CANCEL_WHILE_USING_BEAUTY_SHOP;
        return false;
    }
    if (user->IsInPVPMatchExcept(PVPMatchType::PLEDGEGAME, PVPMatchPlayerState::NONE) || user->IsInDuel()) {
        reason = SystemMessageId::CANNOT_PARTICIPATE_WHILE_IN_ANOTHER_PVP_MATCH;
        return false;
    }
    if (user->HasFlagOfSiege()) {
        reason = SystemMessageId::TELEPORT_EFFECT_BLOCKED_WHILE_HOLDING_WARD;
        return false;
    }

    static_cast<void>(CCursedWeaponMgr::inst());
    if (user->m_nCursedWeaponClassId >= 1) {
        reason = SystemMessageId::CANNOT_REGISTER_WHILE_CARRYING_CURSED_WEAPON;
        return false;
    }
    if (user->m_bFlyMoving) {
        reason = SystemMessageId::CANNOT_ACT_WHILE_USING_SAYUNE;
        return false;
    }

    if (SharedPledgeGameInfo::GetInfo()->m_nStep == 2 && !user->m_PledgeGameUser.CanRegister()) {
        reason = SystemMessageId::CANNOT_REGISTER_AS_PARTICIPANT;
        return false;
    }
    if (user->m_data->m_nWorldId == 1) {
        reason = SystemMessageId::CANNOT_TAKE_OTHER_ACTIONS_WHILE_IN_TRAINING_CAMP;
        return false;
    }
    return true;
}

bool CPledgeGameUserStateMachine::CanChangeState(UserState state) {
    L2_GUARD_FUNCTION();

    return kPledgeGameStateTransitions[m_Status][state];
}

void CPledgeGameSystem::SetLosePrizeItem(const std::int32_t itemClassId, const std::int32_t count) {
    // Current L2Server 0x0099d3d0.
    L2_GUARD_FUNCTION();
    m_nLosePrizeItemType = itemClassId;
    m_nLosePrizeItemCount = count;
}

void CPledgeGameSystem::SetWinPrizeNormalItem(
    const std::int32_t itemClassId, const std::int32_t minimum, const std::int32_t maximum
) {
    // Current L2Server 0x0099d478.
    L2_GUARD_FUNCTION();
    m_nWinPrizeNormalItemType = itemClassId;
    m_nWinPrizeNormalMaxCnt = maximum;
    m_nWinPrizeNormalMinCnt = minimum;
}

void CPledgeGameSystem::SetWinPrizeRandItem(
    const std::int32_t itemClassId, const std::int32_t minimum, const std::int32_t maximum
) {
    // Current L2Server 0x0099d534.
    L2_GUARD_FUNCTION();
    m_nWinPrizeRandItemType = itemClassId;
    m_nWinPrizeRandItemMaxCnt = maximum;
    m_nWinPrizeRandItemMinCnt = minimum;
}

bool CPledgeGameUserStateMachine::ChangeState(UserState state) {
    L2_GUARD_FUNCTION();

    const bool canChange = kPledgeGameStateTransitions[m_Status][state];
    if (canChange) {
        m_Status = state;
    }
    return canChange;
}

void CPledgeGameUser::OnLeaveWorld() {
    // Current L2Server 0x009acf2c.
    L2_GUARD_FUNCTION();
    g_PledgeGameSystem.UnregisterEntry(m_pUser);
}
