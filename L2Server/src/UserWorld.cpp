#include "Agent.h"
#include "AgentGameGuard.h"
#include "Agit.h"
#include "AgitDeco.h"
#include "AirShip.h"
#include "AirShipPart.h"
#include "Area.h"
#include "Auth.h"
#include "BalthusEvent.h"
#include "BattleCamp.h"
#include "BeautyManager.h"
#include "BotChecker.h"
#include "CConfig.h"
#include "CFortressFlagpole.h"
#include "CGeneralObject.h"
#include "CWorld.h"
#include "Captcha.h"
#include "Castle.h"
#include "Chair.h"
#include "ChannelChatting.h"
#include "ControlTower.h"
#include "CreatureController.h"
#include "CursedWeapon.h"
#include "DB.h"
#include "Default.h"
#include "DeferredJob.h"
#include "Dominion.h"
#include "Door.h"
#include "Duel.h"
#include "DuelUserInfo.h"
#include "DynamicQuest.h"
#include "EventCampaign.h"
#include "EventController.h"
#include "EventMatch.h"
#include "External/NCGuardServerLibCompat.h"
#include "FieldCycle.h"
#include "Fishing.h"
#include "FlyMove.h"
#include "Fortress.h"
#include "Friends.h"
#include "GameEventSystem.h"
#include "HolyThing.h"
#include "InstantAgit.h"
#include "InstantZone.h"
#include "Item.h"
#include "NPC.h"
#include "NewInspector.h"
#include "NoblessSystem.h"
#include "NpcServer.h"
#include "ObjectDB.h"
#include "OlympiadService.h"
#include "OlympiadServiceProxy.h"
#include "OlympiadStuff.h"
#include "OwnThing.h"
#include "PVPMatch.h"
#include "Packet.h"
#include "PartyMatchingRequest.h"
#include "PartyMatchingWaitingRoom.h"
#include "Pet.h"
#include "PetitionCli.h"
#include "Pledge.h"
#include "PledgeGameSystem.h"
#include "PledgeMemberService.h"
#include "PledgeRaid.h"
#include "PledgeRaidNetwork.h"
#include "Portable/ClientProtocolRuntime.h"
#include "Post.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Raid.h"
#include "RelationManager.h"
#include "Relay.h"
#include "Residence.h"
#include "RestartOriginal.h"
#include "SSQ.h"
#include "SerializedMessage.h"
#include "Setting.h"
#include "Shared/src/Config.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/TimeAnalysis.h"
#include "Shared/src/UserConst.h"
#include "Shared/src/Utils.h"
#include "Shared/src/Vitality.h"
#include "Shared/src/platform/Runtime.h"
#include "SharedContainer.h"
#include "Shuttle.h"
#include "SiegeAgit.h"
#include "SignBoard.h"
#include "Skill.h"
#include "StaticObject.h"
#include "TeamBattleAgit.h"
#include "TeamBattleFlag.h"
#include "ThreadLocalWorldMap.h"
#include "ToDoList.h"
#include "Trade.h"
#include "Trap.h"
#include "User.h"
#include "UserBroadcaster.h"
#include "UserSocket.h"
#include "UserUtil.h"
#include "Vehicle.h"
#include "VipAttendance.h"
#include "WorldCollision.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <string>
#include <string_view>
#include <utility>

namespace {

std::tm* LocalTime32Pointer(const std::int32_t value) noexcept {
#if defined(_WIN32)
    const __time32_t targetValue = static_cast<__time32_t>(value);
    return ::_localtime32(&targetValue);
#else
    const std::time_t hostValue = static_cast<std::time_t>(value);
    return std::localtime(&hostValue);
#endif
}


constexpr std::uint32_t kRestartAtCurrentPositionMask = 0x006000a0U;

bool IsRestartAtCurrentPosition(const RestartPoint point) {
    const auto value = static_cast<std::uint32_t>(point);
    return value < 32U && ((kRestartAtCurrentPositionMask >> value) & 1U) != 0U;
}

bool IsZeroRestartPoint(const FVector& point) {
    return static_cast<int>(point.x) == 0 && static_cast<int>(point.y) == 0 && static_cast<int>(point.z) == 0;
}

template <typename TList>
FVector PickRestartPoint(const TList& points, const FVector& fallback) {
    if (points.empty()) {
        return fallback;
    }
    return points[static_cast<std::size_t>(::rand()) % points.size()];
}

bool EqualsAsciiCaseInsensitive(const char16_t* left, const char16_t* right) {
    if (left == nullptr || right == nullptr) {
        return left == right;
    }
    for (;;) {
        char16_t lhs = *left++;
        char16_t rhs = *right++;
        if (lhs >= u'A' && lhs <= u'Z') {
            lhs += u'a' - u'A';
        }
        if (rhs >= u'A' && rhs <= u'Z') {
            rhs += u'a' - u'A';
        }
        if (lhs != rhs) {
            return false;
        }
        if (lhs == 0) {
            return true;
        }
    }
}

int ResidenceId(const CResidence* residence) {
    return residence != nullptr && residence->m_data != nullptr ? residence->m_data->m_nResidenceID : 0;
}

bool HasUsableBattleCamp(CPledge* pledge, CResidence* residence) {
    if (pledge == nullptr || residence == nullptr) {
        return false;
    }
    CSPointer<CBattleCamp> battleCamp = pledge->GetBattleCamp();
    CBattleCamp* const camp = battleCamp.get();
    return camp != nullptr && camp->IsEnabled() && camp->IsAdvanceBase() &&
        camp->GetBaseResidenceId() == ResidenceId(residence);
}

double PlanarDistanceSquared(const FVector& left, const FVector& right) {
    const double x = right.x - left.x;
    const double y = right.y - left.y;
    return x * x + y * y;
}


std::u16string LowerUserNameKey(const char16_t* name) { return LowercaseGameName(std::u16string(name)); }

void SayPledgeInternal(
    CSPointer<User> receiver,
    int talkerId,
    const char16_t* talkerName,
    const char16_t* message,
    std::uint8_t relation,
    bool raid
) {
    if (!receiver || receiver->IsDeleting() || receiver->m_Deleted != 0) {
        return;
    }
    receiver->GetSocket()->Send("cddSdSc", 0x4a, talkerId, raid ? 0x1a : 4, talkerName, -1, message, relation);
}

void SayPledge(
    CSPointer<User> receiver,
    const int talkerId,
    const char16_t* const talkerName,
    const char16_t* const message,
    const std::uint8_t relation
) {
    // Current L2Server 0x00bbeb18 copies the FunctionChain
    // reference into the 0x00bbe9f8 inner delivery boundary.
    SayPledgeInternal(receiver, talkerId, talkerName, message, relation, false);
}

void SayPledgeInRaid(
    CSPointer<User> receiver, const int talkerId, const char16_t* const talkerName, const char16_t* const message
) {
    // Current L2Server 0x00bbebb4 has the same physical copy boundary and
    // selects the raid chat type in 0x00bbe9f8.
    SayPledgeInternal(receiver, talkerId, talkerName, message, 0, true);
}

void SayAllianceInternal(
    CSPointer<User> receiver,
    int talkerId,
    const char16_t* talkerName,
    const char16_t* message,
    std::uint8_t relation,
    bool raid
) {
    if (!receiver || receiver->IsDeleting() || receiver->m_Deleted != 0) {
        return;
    }
    receiver->GetSocket()->Send("cddSdSc", 0x4a, talkerId, raid ? 0x1b : 9, talkerName, -1, message, relation);
}

void SayAlliance(
    CSPointer<User> receiver,
    const int talkerId,
    const char16_t* const talkerName,
    const char16_t* const message,
    const std::uint8_t relation
) {
    // Current L2Server 0x00bbed68 copies the FunctionChain
    // reference into the 0x00bbec48 inner delivery boundary.
    SayAllianceInternal(receiver, talkerId, talkerName, message, relation, false);
}

void SayAllianceInRaid(
    CSPointer<User> receiver, const int talkerId, const char16_t* const talkerName, const char16_t* const message
) {
    // Current L2Server 0x00bbee04 has the same physical copy boundary and
    // selects the raid chat type in 0x00bbec48.
    SayAllianceInternal(receiver, talkerId, talkerName, message, 0, true);
}

} // namespace

bool User::AdjustStartPoint(FVector& position) {
    // Current L2Server.exe 0x00bc38a0, original user_world.cpp owner.
    L2_GUARD_FUNCTION();

    bool started = false;
    const FVector current = m_data->GetPos();
    if (CWorld::Inst().GetGeo()->IsBattleField(current)) {
        for (const auto& entry : g_ResidenceDB.m_ResidenceIDMap) {
            CResidence* const residence = entry.second;
            if (residence != nullptr && residence->IsInBattleField(current, true)) {
                residence->OnStartInBattleField(this, position, started);
                break;
            }
        }
    }
    return started;
}

void User::ScreenNotice(const char16_t* message) {
    // Current L2Server.exe 0x00bc04e8.
    L2_GUARD_FUNCTION();

    CUserBroadcaster::inst().BroadcastToAllUser("cddSdS", 0x4a, 0, 0x13, u"", -1, message);
}

void User::Say(const char16_t* message) {
    // Current L2Server.exe 0x00bc06ac.
    L2_GUARD_FUNCTION();

    if (g_bQuietWorld) {
        SendSystemMessage(SystemMessageId::CHAT_PAUSED_FOR_GM_ANNOUNCEMENT);
        return;
    }
    if (m_bIsObserving) {
        SendSystemMessage(SystemMessageId::CANNOT_USE_GENERAL_CHAT_WHILE_OBSERVING);
        return;
    }
    if (m_PledgeGameUser.IsPlayer()) {
        SendSystemMessage(SystemMessageId::CANNOT_CHAT_IN_CEREMONY_OF_CHAOS);
        return;
    }
    if (IsGMInvisible() && !IsOlympiadObserver() && !m_PledgeGameUser.IsObserver()) {
        SendSystemMessage(SystemMessageId::CANNOT_USE_GENERAL_CHAT_WHILE_INVISIBLE);
        return;
    }
    BroadcastSay(message, SPT_NORMAL, 1000);
}

void User::Shout(const char16_t* message) {
    // Current L2Server.exe 0x00bc9b98.
    L2_GUARD_FUNCTION();

    if (g_bQuietWorld) {
        SendSystemMessage(SystemMessageId::CHAT_PAUSED_FOR_GM_ANNOUNCEMENT);
        return;
    }
    if (m_bIsObserving) {
        SendSystemMessage(SystemMessageId::CANNOT_USE_GENERAL_CHAT_WHILE_OBSERVING);
        return;
    }
    if (m_PledgeGameUser.IsPlayer()) {
        SendSystemMessage(SystemMessageId::CANNOT_CHAT_IN_CEREMONY_OF_CHAOS);
        return;
    }
    if (IsGMInvisible() && !IsOlympiadObserver()) {
        SendSystemMessage(SystemMessageId::CANNOT_USE_GENERAL_CHAT_WHILE_INVISIBLE);
        return;
    }

    static_cast<void>(Singleton<CCursedWeaponMgr>::inst());
    if (m_nCursedWeaponClassId < 1) {
        BroadcastSay(message, SPT_SHOUT, 0x4000);
        return;
    }
    SendSystemMessage(SystemMessageId::CANNOT_USE_SHOUT_OR_TRADE_CHAT_WITH_CURSED_WEAPON);
}

void User::Tell(User* target, const char16_t* message) {
    // Current L2Server.exe 0x00bc35f8.
    L2_GUARD_FUNCTION();

    if (g_bQuietWorld) {
        SendSystemMessage(SystemMessageId::CHAT_PAUSED_FOR_GM_ANNOUNCEMENT);
        return;
    }
    if (g_nCountryCode == 3 && GetSocket()->m_bEconomicRestriction) {
        GetSocket()->SendSystemMessage(SystemMessageId::ACCOUNT_NOT_PAID);
        return;
    }
    if (target == nullptr) {
        SendSystemMessage(SystemMessageId::TARGET_PLAYER_NOT_ONLINE);
        return;
    }

    CUserSocket* const targetSocket = target->GetSocket();
    if (targetSocket != nullptr && g_bChatLimitWhisperReplyOnly && target->m_data->m_nLevel <= g_nChatLimitLevelWhisper) {
        CSPointer<CFriends> friends = target->GetFriends();
        if (friends) {
            const char16_t* const sourceName = GetName();
            const unsigned int sourceDbId = GetDBID();
            friends->AddWhisperFriend(sourceDbId, sourceName);
        }
    }

    if (!target->m_bBlockAll) {
        if (!target->IsInBlockList(*this)) {
            target->Hear(this, GetName(), message, 2);

            // This is game text, not native printf output. In the C locale,
            // macOS swprintf cannot format non-ASCII names even with %ls.
            const std::u16string replyName = std::u16string(u"->") + target->GetName();
            Hear(this, replyName.c_str(), message, 2);
            return;
        }

        CUserSocket* const socket = GetSocket();
        const char16_t* const targetName = target->GetName();
        socket->SendSystemMessage_Char(SystemMessageId::CHARACTER_BLOCKED_YOU, targetName);
        return;
    }

    CUserSocket* const socket = GetSocket();
    const char16_t* const targetName = target->GetName();
    socket->SendSystemMessage_Char(SystemMessageId::TARGET_BLOCKS_ALL, targetName);
}

void User::Market(const char16_t* message) {
    // Current L2Server.exe 0x00bc9cf8.
    L2_GUARD_FUNCTION();

    if (g_bQuietWorld) {
        SendSystemMessage(SystemMessageId::CHAT_PAUSED_FOR_GM_ANNOUNCEMENT);
        return;
    }
    if (m_bIsObserving) {
        SendSystemMessage(SystemMessageId::CANNOT_USE_GENERAL_CHAT_WHILE_OBSERVING);
        return;
    }
    if (IsGMInvisible()) {
        SendSystemMessage(SystemMessageId::CANNOT_USE_GENERAL_CHAT_WHILE_INVISIBLE);
        return;
    }
    if ((g_nCountryCode == 3 || g_nCountryCode == 1) && GetSocket() != nullptr && GetSocket()->m_bEconomicRestriction) {
        SendSystemMessage(SystemMessageId::ACCOUNT_NOT_PAID);
        return;
    }
    if (m_data->m_nLevel <= g_nChatLimitLevelMarket &&
        (g_nChatLimitTarget == 0 || (g_nChatLimitTarget == 1 && CheckPremiumType(PremiumUserType::PUT_FREE_USER)))) {
        GetSocket()->Send(
            "chccc", 0x62, SystemMessageId::FREE_USER_TRADE_CHAT_LEVEL_RESTRICTION, 1, 0x14,
            static_cast<char>(g_nChatLimitLevelMarket)
        );
        return;
    }

    static_cast<void>(Singleton<CCursedWeaponMgr>::inst());
    if (m_nCursedWeaponClassId >= 1) {
        SendSystemMessage(SystemMessageId::CANNOT_USE_SHOUT_OR_TRADE_CHAT_WITH_CURSED_WEAPON);
        return;
    }

    std::array<SharedUtilsWideChar, 1024> filteredMessage;
    filteredMessage[0] = SharedUtilsWideChar{};
    std::memset(filteredMessage.data() + 1, 0, 0x7fe);
    const int messageLength = static_cast<int>(target_utf16::Length(message));
    const SharedUtilsWideChar* const targetMessage = reinterpret_cast<const SharedUtilsWideChar*>(message);
    std::int64_t inputIndex = 0;
    std::int64_t outputIndex = 0;
    while (inputIndex < messageLength) {
        SharedUtilsWideChar value = targetMessage[inputIndex];
        if (value == static_cast<SharedUtilsWideChar>(',')) {
            filteredMessage[outputIndex++] = static_cast<SharedUtilsWideChar>(' ');
        } else if (value != static_cast<SharedUtilsWideChar>('\b')) {
            filteredMessage[outputIndex++] = value;
        } else {
            bool copyFormattingSegment = false;
            for (;;) {
                ++inputIndex;
                value = targetMessage[inputIndex];
                if (value == static_cast<SharedUtilsWideChar>('\b') || value == SharedUtilsWideChar{}) {
                    break;
                }
                if (value == static_cast<SharedUtilsWideChar>(0x1b)) {
                    copyFormattingSegment = !copyFormattingSegment;
                }
                if (copyFormattingSegment) {
                    filteredMessage[outputIndex++] = value;
                }
            }
        }
        ++inputIndex;
    }

#if defined(_WIN32)
    wchar_t* const hostFilteredMessage = reinterpret_cast<wchar_t*>(filteredMessage.data());
#else
    std::array<wchar_t, 1024> hostFilteredMessageStorage{};
    wchar_t* const hostFilteredMessage =
        CopyUtf16ToWide(filteredMessage.data(), filteredMessage.size(), hostFilteredMessageStorage);
#endif
    const FVector position = m_data->m_Pos;
    const char16_t* const actorName = GetName();
    const unsigned int actorDbId = GetDBID();
    CLogFormatter formatter;
    // formatter.WriteChatLog(
    //     8,
    //     static_cast<std::int64_t>(actorDbId),
    //     0,
    //     runtime::Win64CvttDoubleToInt32(position.x),
    //     runtime::Win64CvttDoubleToInt32(position.y),
    //     runtime::Win64CvttDoubleToInt32(position.z),
    //     hostFilteredMessage,
    //     const_cast<wchar_t*>(actorName),
    //     const_cast<wchar_t*>(L""))
    formatter.WriteChatLog(
        8, static_cast<std::int64_t>(actorDbId), 0, runtime::Win64CvttDoubleToInt32(position.x),
        runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z),
        WideToUtf16(hostFilteredMessage), actorName, u""
    );
    BroadcastSay(message, SPT_MARKET, 0x4000);
}

void User::Harangue(const char16_t* message) {
    // Current L2Server.exe 0x00bc3bf8.
    L2_GUARD_FUNCTION();

    if (g_bQuietWorld) {
        SendSystemMessage(SystemMessageId::CHAT_PAUSED_FOR_GM_ANNOUNCEMENT);
        return;
    }
    if (m_bIsObserving) {
        SendSystemMessage(SystemMessageId::CANNOT_USE_GENERAL_CHAT_WHILE_OBSERVING);
        return;
    }
    if (IsGMInvisible()) {
        SendSystemMessage(SystemMessageId::CANNOT_USE_GENERAL_CHAT_WHILE_INVISIBLE);
        return;
    }
    if (!IsHero()) {
        GetSocket()->SendSystemMessage(SystemMessageId::HERO_CHAT_REQUIRES_HERO);
        return;
    }
    if (m_dwStopHeroSay != 0) {
        SendSystemMessage(SystemMessageId::HERO_CHAT_BANNED);
        return;
    }

    const std::int32_t now = runtime::crt::Time32Now();
    if (runtime::SubtractInt32Wrapping(now, m_tHarangueTimeStamp) <= 9) {
        return;
    }

    std::array<SharedUtilsWideChar, 1024> filteredMessage;
    filteredMessage[0] = SharedUtilsWideChar{};
    std::memset(filteredMessage.data() + 1, 0, 0x7fe);
    const int messageLength = static_cast<int>(target_utf16::Length(message));
    const SharedUtilsWideChar* const targetMessage = reinterpret_cast<const SharedUtilsWideChar*>(message);
    const char16_t* const packetMessage = message;
    std::int64_t inputIndex = 0;
    std::int64_t outputIndex = 0;
    while (inputIndex < messageLength) {
        SharedUtilsWideChar value = targetMessage[inputIndex];
        if (value == static_cast<SharedUtilsWideChar>(',')) {
            filteredMessage[outputIndex++] = static_cast<SharedUtilsWideChar>(' ');
        } else if (value != static_cast<SharedUtilsWideChar>('\b')) {
            filteredMessage[outputIndex++] = value;
        } else {
            bool copyFormattingSegment = false;
            for (;;) {
                ++inputIndex;
                value = targetMessage[inputIndex];
                if (value == static_cast<SharedUtilsWideChar>('\b') || value == SharedUtilsWideChar{}) {
                    break;
                }
                if (value == static_cast<SharedUtilsWideChar>(0x1b)) {
                    copyFormattingSegment = !copyFormattingSegment;
                }
                if (copyFormattingSegment) {
                    filteredMessage[outputIndex++] = value;
                }
            }
        }
        ++inputIndex;
    }

#if defined(_WIN32)
    wchar_t* const hostFilteredMessage = reinterpret_cast<wchar_t*>(filteredMessage.data());
#else
    std::array<wchar_t, 1024> hostFilteredMessageStorage{};
    wchar_t* const hostFilteredMessage =
        CopyUtf16ToWide(filteredMessage.data(), filteredMessage.size(), hostFilteredMessageStorage);
#endif
    const double z = GetPos().z;
    const double y = GetPos().y;
    const double x = GetPos().x;
    const char16_t* const actorName = GetName();
    const unsigned int actorDbId = GetDBID();
    CLogFormatter formatter;
    // formatter.WriteChatLog(
    //     0x11,
    //     static_cast<std::int64_t>(actorDbId),
    //     0,
    //     runtime::Win64CvttDoubleToInt32(x),
    //     runtime::Win64CvttDoubleToInt32(y),
    //     runtime::Win64CvttDoubleToInt32(z),
    //     hostFilteredMessage,
    //     const_cast<wchar_t*>(actorName),
    //     const_cast<wchar_t*>(L""))
    formatter.WriteChatLog(
        0x11, static_cast<std::int64_t>(actorDbId), 0, runtime::Win64CvttDoubleToInt32(x),
        runtime::Win64CvttDoubleToInt32(y), runtime::Win64CvttDoubleToInt32(z), WideToUtf16(hostFilteredMessage), actorName,
        u""
    );
    m_tHarangueTimeStamp = now;

    const char16_t* const broadcastActorName = GetName();
    const std::u16string packetActorName = std::u16string(broadcastActorName);
    BroadcastToAllowedUser("cddSdS", 0x4a, static_cast<int>(Id()), 0x11, packetActorName.c_str(), -1, packetMessage);
}

void User::WorldSay(const char16_t* message) {
    // Current L2Server.exe 0x00bbd81c.
    L2_GUARD_FUNCTION();

    if (g_bQuietWorld) {
        SendSystemMessage(SystemMessageId::CHAT_PAUSED_FOR_GM_ANNOUNCEMENT);
        return;
    }
    if (m_bIsObserving) {
        SendSystemMessage(SystemMessageId::CANNOT_USE_GENERAL_CHAT_WHILE_OBSERVING);
        return;
    }
    if (IsGMInvisible()) {
        SendSystemMessage(SystemMessageId::CANNOT_USE_GENERAL_CHAT_WHILE_INVISIBLE);
        return;
    }
    if (!CanWorldSay()) {
        return;
    }

    const std::int32_t now = runtime::crt::Time32Now();
    std::array<SharedUtilsWideChar, 1024> filteredMessage;
    filteredMessage[0] = SharedUtilsWideChar{};
    std::memset(filteredMessage.data() + 1, 0, 0x7fe);
    const int messageLength = static_cast<int>(target_utf16::Length(message));
    const SharedUtilsWideChar* const targetMessage = reinterpret_cast<const SharedUtilsWideChar*>(message);
    const char16_t* const packetMessage = message;
    std::int64_t inputIndex = 0;
    std::int64_t outputIndex = 0;
    while (inputIndex < messageLength) {
        SharedUtilsWideChar value = targetMessage[inputIndex];
        if (value == static_cast<SharedUtilsWideChar>(',')) {
            filteredMessage[outputIndex++] = static_cast<SharedUtilsWideChar>(' ');
        } else if (value != static_cast<SharedUtilsWideChar>('\b')) {
            filteredMessage[outputIndex++] = value;
        } else {
            bool copyFormattingSegment = false;
            for (;;) {
                ++inputIndex;
                value = targetMessage[inputIndex];
                if (value == static_cast<SharedUtilsWideChar>('\b') || value == SharedUtilsWideChar{}) {
                    break;
                }
                if (value == static_cast<SharedUtilsWideChar>(0x1b)) {
                    copyFormattingSegment = !copyFormattingSegment;
                }
                if (copyFormattingSegment) {
                    filteredMessage[outputIndex++] = value;
                }
            }
        }
        ++inputIndex;
    }

    m_tWorldSayTimeStamp = now;
    const char16_t* const worldChatActorName = GetName();
    const std::u16string packetActorName = std::u16string(worldChatActorName);
    WorldChatData data(static_cast<int>(Id()), packetActorName.c_str(), packetMessage);
    WorldChatManager::Inst().ReserveSendChat(data);
    const int usedWorldChatPoint = IncUsedWorldChatPoint();
    db.RequestIncWorldChatPoint(this);

#if defined(_WIN32)
    wchar_t* const hostFilteredMessage = reinterpret_cast<wchar_t*>(filteredMessage.data());
#else
    std::array<wchar_t, 1024> hostFilteredMessageStorage{};
    wchar_t* const hostFilteredMessage =
        CopyUtf16ToWide(filteredMessage.data(), filteredMessage.size(), hostFilteredMessageStorage);
#endif
    const double z = GetPos().z;
    const double y = GetPos().y;
    const double x = GetPos().x;
    const char16_t* const logActorName = GetName();
    const unsigned int actorDbId = GetDBID();
    CLogFormatter formatter;
    // formatter.WriteChatLog(
    //     0x19,
    //     static_cast<std::int64_t>(actorDbId),
    //     0,
    //     runtime::Win64CvttDoubleToInt32(x),
    //     runtime::Win64CvttDoubleToInt32(y),
    //     runtime::Win64CvttDoubleToInt32(z),
    //     hostFilteredMessage,
    //     const_cast<wchar_t*>(logActorName),
    //     const_cast<wchar_t*>(L""))
    formatter.WriteChatLog(
        0x19, static_cast<std::int64_t>(actorDbId), 0, runtime::Win64CvttDoubleToInt32(x),
        runtime::Win64CvttDoubleToInt32(y), runtime::Win64CvttDoubleToInt32(z), WideToUtf16(hostFilteredMessage),
        logActorName, u""
    );
    formatter.WriteLog1_1(
        0x1fc6, this, nullptr, nullptr, nullptr, nullptr, usedWorldChatPoint, m_nUsedWorldChatPoint, 0, 0, 0, 0, 0
    );

    if (HaveMemo(g_WorldChat.nQuestID1)) {
        g_npcserver.Send("cdd", 0xbc, static_cast<int>(Id()));
    }
    CUserSocket* const socket = GetSocket();
    if (socket != nullptr) {
        socket->Send("chd", 0xfe, 0x175, GetWorldChatCount());
    }
}

void User::SayToFriend(CSPointer<User>& target, const char16_t* message) {
    // Current L2Server.exe 0x00bbdb9c, original owner user_world.cpp.
    L2_GUARD_FUNCTION();

    if (g_nCountryCode == 3 && GetSocket()->m_bEconomicRestriction) {
        GetSocket()->SendSystemMessage(SystemMessageId::ACCOUNT_NOT_PAID);
        return;
    }
    if (m_PledgeGameUser.IsPlayer()) {
        GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_CHAT_IN_CEREMONY_OF_CHAOS);
        return;
    }
    if (target->IsInBlockList(*this)) {
        const std::u16string targetName = std::u16string(target->GetName());
        const std::u16string sourceName = std::u16string(GetName());
        GetSocket()->Send("cdSSS", 0x78, 0x26c, targetName.c_str(), sourceName.c_str(), message);
        return;
    }
    const std::u16string targetName = std::u16string(target->GetName());
    const std::u16string sourceName = std::u16string(GetName());
    target->GetSocket()->Send("cdSSS", 0x78, 0, targetName.c_str(), sourceName.c_str(), message);
}

void User::BroadcastSay(const char16_t* message, SayPacketType type, int distance) {
    // Current L2Server.exe 0x00bbee98 and local TLMFunc::CanDo 0x00bc312c.
    L2_GUARD_FUNCTION();

    class TLMFunc final : public CTLMFunc {
    public:
        TLMFunc(User* sender, const char16_t* message, SayPacketType type) noexcept :
            m_sender(sender), m_message(message), m_type(type) {}

        bool CanDo(CObject* object, double) override {
            if (object->IsPC() && !object->IsDeleting() && object->m_Deleted == 0) {
                User* const receiver = object->CastUser();
                if (receiver->Id() != m_sender->Id() && receiver->GetSocket()->PendingWrite() > 0x7ffff) {
                    return true;
                }

                const char16_t* const chatName = m_sender->m_sChatName;
                receiver->Hear(
                    m_sender, chatName, m_message, static_cast<int>(m_type)
                );
            }
            return true;
        }

    private:
        User* m_sender;
        const char16_t* m_message;
        SayPacketType m_type;
    };

    const FVector position = GetPos();
    const int inZoneId = static_cast<int>(GetInZoneID());
    CTLMFilterDist filter(inZoneId, position, static_cast<double>(distance));
    TLMFunc function(this, message, type);
    FOR_ALL_NEIGHBOR_USER_DIST(&filter, &function);
}

int User::GetAffectedBattleCampType() {
    // Current L2Server 0x00ba2928.
    L2_GUARD_FUNCTION();

    if (CPledge* const pledge = GetPledge()) {
        CSPointer<CBattleCamp> battleCamp = pledge->GetBattleCamp();
        CBattleCamp* const camp = battleCamp.get();
        if (camp != nullptr && camp->IsEnabled()) {
            const int radius = camp->IsAdvanceBase() ? 200 : 125;
            if (PlanarDistanceSquared(GetPos(), camp->GetPos()) < static_cast<double>(radius * radius)) {
                return camp->IsAdvanceBase() ? 2 : 1;
            }
        }
    }

    const int residenceId = m_data->m_nResidenceID;
    if (residenceId != 0) {
        CDominion* const dominion = g_ResidenceDB.GetDominion(residenceId);
        CPledge* const owner = dominion != nullptr ? dominion->GetResidenceOwnerPledge() : nullptr;
        if (owner != nullptr) {
            CSPointer<CBattleCamp> battleCamp = owner->GetBattleCamp();
            CBattleCamp* const camp = battleCamp.get();
            if (camp != nullptr && camp->IsAdvanceBase() && camp->IsEnabled() &&
                PlanarDistanceSquared(GetPos(), camp->GetPos()) < 40000.0) {
                return 2;
            }
        }
    }
    return 0;
}

FVector User::GetRestartPointInSiegeField(CResidence* residence) {
    L2_GUARD_FUNCTION();

    if (residence == nullptr) {
        return FVector{};
    }

    const std::uint32_t random = static_cast<std::uint32_t>(::rand());
    const bool chao = m_data->m_nKarma < 0;
    const auto& points = chao ? residence->m_ChaoRestartPointList : residence->m_OtherRestartVillageList;
    if (!points.empty()) {
        return points[static_cast<std::size_t>(random % static_cast<std::uint32_t>(points.size()))];
    }
    return Singleton<CSetting>::inst().GetRestartPoint(
        m_data->m_Race, m_data->m_Pos, chao, m_data->m_nPKCount
    );
}

bool User::SendDiePacket(char save) {
    L2_GUARD_FUNCTION();

    bool village = true;
    bool agit = false;
    bool castle = false;
    bool battleCamp = false;
    bool originalItem = false;
    bool fortress = false;
    bool limitedOriginalItem = false;
    bool restartSkillAvailable = false;

    const FVector originalPosition = m_data->m_Pos;
    CPledge* const pledge = GetPledge();
    CInstantAgit* const instantAgit = Singleton<CInstantAgitManager>::inst().GetInstantAgit(*this);
    if (pledge != nullptr) {
        CCastle* const ownedCastle = pledge->GetCastle();
        castle = ownedCastle != nullptr;
        if (ownedCastle == nullptr) {
            fortress = pledge->m_pFortress != nullptr;
        }
        agit = pledge->GetWorldAgit() != nullptr || instantAgit != nullptr;
    }

    if (m_AreaMod.m_bIsInBattleField) {
        if (pledge == nullptr) {
            if (m_data->m_nDominionSiegeId != 0) {
                CDominion* const dominion = g_ResidenceDB.GetDominion(m_data->m_nDominionSiegeId);
                CPledge* const owner = dominion == nullptr ? nullptr : dominion->GetResidenceOwnerPledge();
                if (owner != nullptr) {
                    CResidence* const residence = g_ResidenceDB.GetWhoseBattleFieldWithBattleCampZone(originalPosition);
                    battleCamp = HasUsableBattleCamp(owner, residence);
                }
            }
        } else {
            CResidence* const residence = g_ResidenceDB.GetWhoseBattleFieldWithBattleCampZone(originalPosition);
            CCastle* const battlefieldCastle = residence == nullptr ? nullptr : residence->CastCastle();
            CWorldAgit* const battlefieldWorldAgit = residence == nullptr ? nullptr : residence->CastWorldAgit();
            CSiegeAgit* const battlefieldSiegeAgit =
                battlefieldWorldAgit == nullptr ? nullptr : battlefieldWorldAgit->CastSiegeAgit();
            CFortress* const battlefieldFortress = residence == nullptr ? nullptr : residence->CastFortress();

            if (battlefieldCastle != nullptr) {
                CDominionManager* const dominionManager = CDominionManager::Inst();
                if (battlefieldCastle->IsUnderDominionSiege() && dominionManager != nullptr &&
                    dominionManager->GetPledgeDominionSiegeID(m_data->m_nPledgeID) != 0) {
                    battleCamp = HasUsableBattleCamp(pledge, residence);
                } else if (pledge->IsDefender(battlefieldCastle)) {
                    const ResidenceState state =
                        battlefieldCastle->m_data == nullptr ? RS_MAX : battlefieldCastle->m_data->m_ResidenceState;
                    castle = state != RS_SIEGE || GetSSQPart() != SSQPart::TWILIGHT;
                } else if (pledge->IsAttacker(battlefieldCastle)) {
                    battleCamp = HasUsableBattleCamp(pledge, residence);
                }
            } else if (battlefieldSiegeAgit != nullptr) {
                if (battlefieldSiegeAgit->IsAttackerPledge(pledge)) {
                    battleCamp = HasUsableBattleCamp(pledge, residence);
                }
            } else if (battlefieldFortress != nullptr) {
                const int residenceId = ResidenceId(residence);
                CDominionManager* const dominionManager = CDominionManager::Inst();
                if (dominionManager != nullptr && dominionManager->m_nState == DSST_DOMINION_SIEGE &&
                    dominionManager->GetPledgeDominionSiegeID(m_data->m_nPledgeID) != 0) {
                    battleCamp = HasUsableBattleCamp(pledge, residence);
                } else if (pledge->m_data->m_nFortressUnderDefenceSiegeId == static_cast<std::uint32_t>(residenceId)) {
                    fortress = true;
                } else if (pledge->m_data->m_nFortressUnderOffenceSiegeId == static_cast<std::uint32_t>(residenceId)) {
                    battleCamp = HasUsableBattleCamp(pledge, residence);
                }
            }
        }
    }

    if (CPVPMatchSystem::inst().IsPVPPlayer(this)) {
        village = false;
        agit = false;
        castle = false;
        battleCamp = false;
        fortress = false;
    }
    if (IsTransformed(TransformType::Flying) != nullptr) {
        agit = false;
        castle = false;
        battleCamp = false;
        fortress = false;
    }

    const int restartItemClassId = g_RestartOriginalManager.HaveRestartItem(this);
    {
        L2_WRITE_GUARD(m_inv);
        if (restartItemClassId > 0 && m_inv.HaveItemByClassId(restartItemClassId, 1, false, false)) {
            originalItem = true;
        }
        if (save == 2 && m_inv.HaveItemByClassId(0x574c, 1, false, false)) {
            limitedOriginalItem = m_nRestartItemCount < 5;
        }
    }

    if (save == 1) {
        restartSkillAvailable = m_RestartSkillId != 0;
    } else {
        CSkillInfo* const restartSkill = g_RestartOriginalManager.HaveRestartSkill(this);
        m_RestartSkillId = 0;
        m_RestartSkillLevel = 0;
        m_bSoulOfPhoenix = false;
        if (HaveAgathion() && restartSkill != nullptr) {
            bool usable = false;
            if (restartSkill->m_nSkillId == 0x5a86) {
                bool differentInstantItem = false;
                int buffCount = 0;
                int debuffCount = 0;
                int songDanceCount = 0;
                int triggerCount = 0;
                L2_GUARD(m_InfectedSkillsSect);
                const int index = _FindSameBuff(
                    restartSkill, differentInstantItem, buffCount, debuffCount, songDanceCount, triggerCount, this
                );
                usable = index >= 0 && static_cast<std::size_t>(index) < m_InfectedSkills.size() &&
                    runtime::CheckedVectorAt(m_InfectedSkills, static_cast<std::size_t>(index)).pSkillInfo == restartSkill;
            } else {
                usable =
                    GetRemainReuseDelaySec(restartSkill->m_nSkillId, restartSkill->m_nLevel, restartSkill->m_nSubLevel) ==
                    0.0;
            }
            if (usable) {
                m_RestartSkillId = restartSkill->m_nSkillId;
                m_RestartSkillLevel = restartSkill->m_nLevel;
                restartSkillAvailable = true;
            }
        }
    }

    std::vector<int> restartNpcItems;
    CPacketHelperEx<20480> restartNpcPacket;
    int restartNpcItemCount = 0;
    if (g_RestartOriginalManager.HaveRestartNpcItem(this, restartNpcItems)) {
        for (const int itemClassId : restartNpcItems) {
            restartNpcPacket.Assemble("d", itemClassId);
            ++restartNpcItemCount;
        }
        restartNpcPacket.Assemble("d", NpcServer::Inst()->IsConnect() && !IsInAirShip());
    }

    int resurrectionRemainTime = 0;
    {
        L2_GUARD(m_InfectedSkillsSect);
        for (const InfectedSkillType& infected : m_InfectedSkills) {
            if (infected.pSkillInfo != nullptr && infected.pSkillInfo->m_nSkillId == 0x1b60) {
                resurrectionRemainTime = runtime::AddInt32Wrapping(
                    runtime::SubtractInt32Wrapping(infected.nStartTime, runtime::crt::Time32Now()),
                    GetAbnormalTime(infected.pSkillInfo, false)
                );
                break;
            }
        }
    }

    CheckAndResetPendingUseETCItem(PUIT_ENCHANT);
    CheckAndResetPendingUseETCItem(PUIT_ATTRIBUTE_ENCHANT);
    CheckAndResetPendingUseETCItem(PUIT_INC_ENCHANT_PROP);
    CheckAndResetPendingUseETCItem(PUIT_CRYSTAL);
    CheckAndResetPendingUseETCItem(PUIT_ATTRIBUTE_CHANGE);
    CheckAndResetPendingUseETCItem(PUIT_SHAPE_SHIFTING);
    CheckAndResetPendingUseETCItem(PUIT_HLOCKED);
    CheckAndResetPendingUseETCItem(PUIT_ONE);
    CheckAndResetPendingUseETCItem(PUIT_TWO);

    const bool resurrectionSpecialApplies = m_SkillDurationMod.m_nResurrectionSpecial > 0 &&
        (m_SkillDurationMod.m_vResurrectionInstantZoneIds.empty() ||
            std::find(
                m_SkillDurationMod.m_vResurrectionInstantZoneIds.begin(),
                m_SkillDurationMod.m_vResurrectionInstantZoneIds.end(), m_data->m_nInstantZoneTypeID
            ) != m_SkillDurationMod.m_vResurrectionInstantZoneIds.end());

    GetSocket()->Send(
        "cddddddddddcddb", 0, Id(), village, agit, castle, battleCamp, 0, originalItem, fortress, resurrectionRemainTime,
        limitedOriginalItem, static_cast<int>(save), restartSkillAvailable, restartNpcItemCount,
        restartNpcPacket.GetLength(), restartNpcPacket.GetPacket()
    );

    if (resurrectionSpecialApplies && m_RestartSkillId != 0x5a86) {
        m_RestartSkillId = 0;
        m_RestartSkillLevel = 0;
    }

    if (!IsGMInvisible() && g_ptlUserMap != nullptr) {
        g_ptlUserMap->BroadcastToNeighborExceptSelf(
            this, 0x80000, originalPosition, "cddddddddddcddb", 0, Id(), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, nullptr
        );
    }
    return false;
}

void User::Suicide(DamageTypeEnum, int) {
    // Current L2Server.exe 0x00bbca6c.
    L2_GUARD_FUNCTION();

    {
        L2_GUARD(*m_lock);
        m_data->m_LastAttackerServerID = 0;
        m_data->SetHP(0.0);
    }

    m_eDeadDamage = DAMAGE_SUICIDE;
    Die(nullptr, 0);
}

bool User::Restart(RestartPoint restartPoint, int restartItemId) {
    // Current L2Server.exe 0x00bc6fd4.
    L2_GUARD_FUNCTION();

    RestartPoint selectedPoint = restartPoint;
    if (selectedPoint == RESTART_ORIGINAL_PLACE && g_RestartOriginalManager.HaveRestartItem(this) == 0) {
        selectedPoint = RESTART_VILLAGE;
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] cannot restart on original place. user[%s / %d]",
        //     "user_world.cpp",
        //     0xf64,
        //     GetName(),
        //     GetDBID())
        // Log.Add(
        //     LOG_WARN,
        //     "[restart] Cannot revive character DB ID %u at the death location: no eligible restart item. Selecting the
        //     village restart route.", static_cast<unsigned>(GetDBID()))
        L2_LOG_WARN(
            Log,
            "[restart] Cannot revive character DB ID %u at the death location: no eligible restart item; selecting the "
            "village restart route.",
            static_cast<unsigned>(GetDBID())
        );
    } else if (selectedPoint == RESTART_ORIGINAL_PLACE_LIMIT && !m_inv.HaveItemByClassId(0x574c, 1, false, false)) {
        selectedPoint = RESTART_VILLAGE;
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] cannot restart on original place. user[%s / %d]",
        //     "user_world.cpp",
        //     0xf70,
        //     GetName(),
        //     GetDBID())
        // Log.Add(
        //     LOG_WARN,
        //     "[restart] Cannot revive character DB ID %u at the death location: required item class 22348 is unavailable.
        //     selecting the village restart route.", static_cast<unsigned>(GetDBID()))
        L2_LOG_WARN(
            Log,
            "[restart] Cannot revive character DB ID %u at the death location: required item class 22348 is unavailable; "
            "selecting the village restart route.",
            static_cast<unsigned>(GetDBID())
        );
    }

    std::vector<int> restartNpcItems;
    if (selectedPoint == RESTART_BY_NPC) {
        const bool hasRestartNpcItem = g_RestartOriginalManager.HaveRestartNpcItem(this, restartNpcItems);
        if (!hasRestartNpcItem ||
            std::find(restartNpcItems.begin(), restartNpcItems.end(), restartItemId) == restartNpcItems.end()) {
            selectedPoint = RESTART_VILLAGE;
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] cannot restart by npc. user[%s / %d]",
            //     "user_world.cpp",
            //     0xf7e,
            //     GetName(),
            //     GetDBID())
            // Log.Add(
            //     LOG_WARN,
            //     "[restart] NPC revival item class %d is not eligible for character DB ID %u. Selecting the village restart
            //     route.", restartItemId, static_cast<unsigned>(GetDBID()))
            L2_LOG_WARN(
                Log,
                "[restart] NPC revival item class %d is not eligible for character DB ID %u; selecting the village restart "
                "route.",
                restartItemId, static_cast<unsigned>(GetDBID())
            );
        }
    } else if (selectedPoint == RESTART_BY_AGATHION && !CanRestartByAgathion()) {
        selectedPoint = RESTART_VILLAGE;
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] cannot restart by agathion. user[%s / %d]",
        //     "user_world.cpp",
        //     0xf89,
        //     GetName(),
        //     GetDBID())
        // Log.Add(
        //     LOG_WARN,
        //     "[restart] Agathion revival is unavailable for character DB ID %u. Selecting the village restart route.",
        //     static_cast<unsigned>(GetDBID()))
        L2_LOG_WARN(
            Log, "[restart] Agathion revival is unavailable for character DB ID %u; selecting the village restart route.",
            static_cast<unsigned>(GetDBID())
        );
    }

    SetPrivateID(0);
    CSetting& setting = Singleton<CSetting>::inst();
    const bool chao = m_data->m_nKarma < 0;
    const RaceType race = m_data->m_Race;
    FVector destination = setting.GetRestartPoint(race, m_data->m_Pos, chao, m_data->m_nPKCount);
    if (IsZeroRestartPoint(destination) && static_cast<CObject*>(this)->IsInRealWorld()) {
        return false;
    }

    if (CSetting::RestartPointGroup* const group = setting.GetRestartPointGroup(race, destination)) {
        const char16_t* const name = group->m_sName.c_str();
        if (EqualsAsciiCaseInsensitive(name, u"gm_room") || EqualsAsciiCaseInsensitive(name, u"gm_room_solo1") ||
            EqualsAsciiCaseInsensitive(name, u"gm_room_solo2")) {
            selectedPoint = RESTART_VILLAGE;
        }
    }

    CResidence* battlefieldResidence = nullptr;
    CCastle* battlefieldCastle = nullptr;
    CWorldAgit* battlefieldWorldAgit = nullptr;
    CSiegeAgit* battlefieldSiegeAgit = nullptr;
    CFortress* battlefieldFortress = nullptr;
    if (CGeoData* const geo = CWorld::Inst().GetGeo(); geo != nullptr && geo->IsBattleField(m_data->m_Pos)) {
        battlefieldResidence = g_ResidenceDB.GetWhoseBattleFieldWithBattleCampZone(m_data->m_Pos);
        if (battlefieldResidence != nullptr) {
            battlefieldCastle = battlefieldResidence->CastCastle();
            battlefieldWorldAgit = battlefieldResidence->CastWorldAgit();
            battlefieldSiegeAgit = battlefieldWorldAgit == nullptr ? nullptr : battlefieldWorldAgit->CastSiegeAgit();
            battlefieldFortress = battlefieldResidence->CastFortress();
        }
    }

    const FVector siegeRestart = GetRestartPointInSiegeField(battlefieldResidence);
    CPledge* const pledge = GetPledge();
    bool residenceOwnedRestart = false;
    bool restartRouteValid = true;
    CInstantAgit* instantAgit = nullptr;

    if (battlefieldWorldAgit != nullptr) {
        if (selectedPoint == RESTART_VILLAGE_BY_DISMOUNT) {
            selectedPoint = RESTART_VILLAGE;
        } else if (battlefieldWorldAgit->IsTeamBattleAgit() && GetPledge() != nullptr) {
            const std::uint32_t userDatabaseId = static_cast<std::uint32_t>(GetDBID());
            const std::uint32_t pledgeDatabaseId = GetPledge()->GetDbId();
            CTeamBattleAgit* const membershipTeamBattle = battlefieldWorldAgit->CastTeamBattleAgit();
            if (membershipTeamBattle->IsBattleRoyalEntryMember(pledgeDatabaseId, userDatabaseId)) {
                const int challengerPledgeDatabaseId = static_cast<int>(GetPledge()->GetDbId());
                CTeamBattleAgit* const restartPointTeamBattle = battlefieldWorldAgit->CastTeamBattleAgit();
                const FVector challenger = restartPointTeamBattle->GetChallengerRestartPoint(challengerPledgeDatabaseId);
                if (!IsZeroRestartPoint(challenger)) {
                    destination = challenger;
                    residenceOwnedRestart = true;
                }
            }
        }
    }

    if (m_data->m_nWorldId != g_nWorldId) {
        (void)Singleton<RaidServerManager>::inst().GetReviveType();
    }

    if (!residenceOwnedRestart) {
        switch (selectedPoint) {
            case RESTART_VILLAGE:
                destination = setting.GetRestartPoint(race, m_data->m_Pos, chao, m_data->m_nPKCount);
                if (battlefieldCastle != nullptr) {
                    const SSQPart::type sealOwner = Singleton<SSQSystem>::inst().GetSealOwner(SSQSeal::STRIFE);
                    if (sealOwner == SSQPart::DAWN) {
                        destination = siegeRestart;
                    }
                } else if (battlefieldResidence != nullptr) {
                    destination = siegeRestart;
                }
                break;

            case RESTART_AGIT:
                if (pledge == nullptr) {
                    restartRouteValid = battlefieldWorldAgit != nullptr;
                    if (restartRouteValid) {
                        destination = siegeRestart;
                    }
                } else if (CWorldAgit* const agit = pledge->GetWorldAgit()) {
                    destination = PickRestartPoint(
                        agit->m_OwnerRestartPointList, setting.GetRestartPoint(race, m_data->m_Pos, chao, m_data->m_nPKCount)
                    );
                    residenceOwnedRestart = true;
                } else if (battlefieldWorldAgit != nullptr) {
                    destination = siegeRestart;
                } else {
                    restartRouteValid = false;
                }
                instantAgit = Singleton<CInstantAgitManager>::inst().GetInstantAgit(*this);
                if (instantAgit != nullptr) {
                    residenceOwnedRestart = true;
                }
                break;

            case RESTART_CASTLE:
                if (pledge == nullptr) {
                    restartRouteValid = false;
                } else if (CCastle* const castle = pledge->GetCastle()) {
                    destination = PickRestartPoint(
                        castle->m_OwnerRestartPointList,
                        setting.GetRestartPoint(race, m_data->m_Pos, chao, m_data->m_nPKCount)
                    );
                    residenceOwnedRestart = true;
                } else if (battlefieldCastle != nullptr && pledge->IsDefender(battlefieldCastle)) {
                    const ResidenceState state =
                        battlefieldCastle->m_data == nullptr ? RS_MAX : battlefieldCastle->m_data->m_ResidenceState;
                    if (state == RS_SIEGE && GetSSQPart() == SSQPart::TWILIGHT &&
                        Singleton<SSQSystem>::inst().GetSealOwner(SSQSeal::STRIFE) == SSQPart::DAWN) {
                        destination = siegeRestart;
                    } else {
                        destination = PickRestartPoint(battlefieldCastle->m_OwnerRestartPointList, destination);
                    }
                } else if (battlefieldResidence != nullptr) {
                    destination = siegeRestart;
                } else {
                    restartRouteValid = false;
                }
                break;

            case RESTART_FORTRESS:
                if (pledge == nullptr) {
                    restartRouteValid = false;
                } else if (pledge->m_pFortress != nullptr) {
                    destination = PickRestartPoint(pledge->m_pFortress->m_OwnerRestartPointList, destination);
                    residenceOwnedRestart = true;
                } else if (battlefieldFortress != nullptr) {
                    const std::uint32_t residenceId =
                        battlefieldFortress->m_data == nullptr ? 0U : battlefieldFortress->m_data->m_nResidenceID;
                    if (residenceId == pledge->m_data->m_nFortressUnderOffenceSiegeId ||
                        residenceId == pledge->m_data->m_nFortressUnderDefenceSiegeId) {
                        destination = siegeRestart;
                    } else {
                        destination = setting.GetRestartPoint(race, m_data->m_Pos, chao, m_data->m_nPKCount);
                    }
                } else {
                    restartRouteValid = false;
                }
                break;

            case RESTART_BATTLE_CAMP: {
                CSPointer<CBattleCamp> battleCamp;
                CDominionManager* const dominionManager = CDominionManager::Inst();
                if (pledge != nullptr && dominionManager != nullptr && dominionManager->m_nState == DSST_DOMINION_SIEGE) {
                    battleCamp = pledge->GetBattleCamp();
                    if (!battleCamp) {
                        if (CDominion* const dominion = g_ResidenceDB.GetDominion(m_data->m_nDominionSiegeId)) {
                            if (CPledge* const owner = dominion->GetResidenceOwnerPledge()) {
                                battleCamp = owner->GetBattleCamp();
                                CBattleCamp* const ownerCamp = battleCamp.get();
                                if (ownerCamp != nullptr && !ownerCamp->IsAdvanceBase()) {
                                    battleCamp.Release();
                                }
                            }
                        }
                    }
                } else if (pledge == nullptr && dominionManager != nullptr &&
                    dominionManager->m_nState == DSST_DOMINION_SIEGE && m_data->m_nDominionSiegeId != 0) {
                    if (CDominion* const dominion = g_ResidenceDB.GetDominion(m_data->m_nDominionSiegeId)) {
                        if (CPledge* const owner = dominion->GetResidenceOwnerPledge()) {
                            battleCamp = owner->GetBattleCamp();
                            CBattleCamp* const ownerCamp = battleCamp.get();
                            if (ownerCamp != nullptr && !ownerCamp->IsAdvanceBase()) {
                                battleCamp.Release();
                            }
                        }
                    }
                } else if (pledge != nullptr) {
                    const bool attacker = battlefieldCastle != nullptr && pledge->IsAttacker(battlefieldCastle);
                    const bool agitAttacker =
                        battlefieldSiegeAgit != nullptr && battlefieldSiegeAgit->IsAttackerPledge(pledge);
                    const bool fortressAttacker = battlefieldFortress != nullptr && battlefieldFortress->m_data != nullptr &&
                        pledge->m_data->m_nFortressUnderOffenceSiegeId ==
                            static_cast<std::uint32_t>(battlefieldFortress->m_data->m_nResidenceID);
                    if (attacker || agitAttacker || fortressAttacker) {
                        battleCamp = pledge->GetBattleCamp();
                    }
                }

                CBattleCamp* const camp = battleCamp.get();
                if (camp != nullptr && camp->IsEnabled() &&
                    (battlefieldResidence == nullptr ||
                        camp->GetBaseResidenceId() ==
                            static_cast<int>(
                                battlefieldResidence->m_data == nullptr ? 0U : battlefieldResidence->m_data->m_nResidenceID
                            ))) {
                    destination = camp->GetTeleportPos();
                } else if (battlefieldResidence != nullptr) {
                    destination = siegeRestart;
                } else {
                    restartRouteValid = false;
                }
                break;
            }

            default:
                if (IsRestartAtCurrentPosition(selectedPoint)) {
                    destination = m_data->m_Pos;
                } else {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] unknown restart point.[%d]",
                    //     "user_world.cpp",
                    //     0x1123,
                    //     static_cast<int>(selectedPoint))
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "[restart] Unrecognized restart type %d for user object %u. Continuing with the destination
                    //     selected so far.", static_cast<int>(selectedPoint), static_cast<unsigned>(m_nId))
                    L2_LOG_ERROR(
                        Log,
                        "[restart] Unrecognized restart type %d for user object %u; continuing with the destination "
                        "selected so far.",
                        static_cast<int>(selectedPoint), static_cast<unsigned>(m_nId)
                    );
                }
                break;
        }
    }

    m_RestartPoint = selectedPoint;
    m_RestartItemId = restartItemId;

    const int mapX = ((static_cast<int>(destination.x) + 0x50000) >> 15) + 10;
    if ((IsRidingWyvern() || IsInAirShip() || static_cast<std::int32_t>(m_data->m_moveType) == CMT_FLY) && mapX > 14) {
        return false;
    }

    if (!IsRestartAtCurrentPosition(selectedPoint)) {
        if (selectedPoint == RESTART_VILLAGE || !restartRouteValid) {
            if (CInstantZone* const instantZone = GetInstantZone()) {
                if (m_DuelUserInfo.IsInPartyDuel()) {
                    Singleton<CDuelManager>::inst().DuelPlayerLeaveWorld(this);
                }
                FVector returnPosition{};
                if (instantZone->GetRetPos(*this, true, returnPosition)) {
                    destination = returnPosition;
                }
            }
        } else if (GetInstantZone() != nullptr && m_DuelUserInfo.IsInPartyDuel()) {
            Singleton<CDuelManager>::inst().DuelPlayerLeaveWorld(this);
        }
    }

    CGeoData* const geo = CWorld::Inst().GetGeo();
    if (geo != nullptr) {
        const int zoneX = ((static_cast<int>(destination.x) + 0x50000) >> 15) + 10;
        const int zoneY = ((static_cast<int>(destination.y) + 0x40000) >> 15) + 10;
        if (geo->GetZone(zoneX, zoneY) == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     "ResPoint is not valid file. res type = [%d]. "
            //     "[%d,%d] [%s] - [%d]",
            //     static_cast<int>(selectedPoint),
            //     zoneX,
            //     zoneY,
            //     "user_world.cpp",
            //     0x115c)
            // Log.Add(
            //     LOG_ERROR,
            //     "[restart] No geodata zone (%d, %d) for restart type %d of user object %u. Restart processing continues.",
            //     zoneX,
            //     zoneY,
            //     static_cast<int>(selectedPoint),
            //     static_cast<unsigned>(m_nId))
            L2_LOG_ERROR(
                Log,
                "[restart] No geodata zone (%d, %d) for restart type %d of user object %u; restart processing continues.",
                zoneX, zoneY, static_cast<int>(selectedPoint), static_cast<unsigned>(m_nId)
            );
        }
        if (static_cast<std::int32_t>(m_data->m_moveType) != CMT_FLY &&
            (!IsInAirShip() || selectedPoint == RESTART_VILLAGE || selectedPoint == RESTART_AGIT ||
                selectedPoint == RESTART_CASTLE || selectedPoint == RESTART_FORTRESS ||
                selectedPoint == RESTART_BATTLE_CAMP)) {
            destination.z = static_cast<double>(geo->FindGround(
                static_cast<int>(destination.x), static_cast<int>(destination.y), static_cast<int>(destination.z)
            ));
        }
    }

    if (!IsRestartAtCurrentPosition(selectedPoint)) {
        m_bIsRestarting = true;
        if (instantAgit == nullptr) {
            if (!static_cast<CCreature*>(this)->TeleportToLocation(
                    static_cast<int>(destination.x), static_cast<int>(destination.y), static_cast<int>(destination.z), 0,
                    0x10000, TeleportSourceType::UserRestart, nullptr, 0
                )) {
                m_bIsRestarting = false;
                return false;
            }
        } else if (!instantAgit->TryEnter(*this)) {
            m_bIsRestarting = false;
            return false;
        }
    }

    bool wasAlive;
    {
        L2_GUARD(*m_lock);

        wasAlive = m_data->m_bIsAlive != 0;
        if (!wasAlive) {
            m_data->SetHP(m_data->m_dBaseHP * 0.65);
            m_bIsNonChaoInNow = false;
        }
    }

    if (instantAgit == nullptr) {
        m_data->SetPos(destination);
    }
    db.SendSaveCharacterInfo(this, true, true);

    if (IsPC()) {
        const int destinationX = runtime::Win64CvttDoubleToInt32(destination.x);
        const int destinationY = runtime::Win64CvttDoubleToInt32(destination.y);
        const int destinationZ = runtime::Win64CvttDoubleToInt32(destination.z);
        const FVector loggedPosition = m_data->m_Pos;
        const int loggedPositionX = runtime::Win64CvttDoubleToInt32(loggedPosition.x);
        const int loggedPositionY = runtime::Win64CvttDoubleToInt32(loggedPosition.y);
        const int loggedPositionZ = runtime::Win64CvttDoubleToInt32(loggedPosition.z);
        const int level = m_data->m_nLevel;
        const char16_t* const actorName = GetName();
        const std::uint32_t accountId = GetAccountID();
        const std::uint32_t databaseId = GetDBID();
#if defined(_WIN32)
        const wchar_t* const accountName = reinterpret_cast<const wchar_t*>(m_sAccountName.data());
#else
        std::array<wchar_t, 16> accountNameBuffer{};
        const wchar_t* const accountName = CopyUtf16ToWide(m_sAccountName.data(), m_sAccountName.size(), accountNameBuffer);
#endif
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,"
        //     L"%s,%s,,,",
        //     0x45b,
        //     databaseId,
        //     accountId,
        //     destinationX,
        //     destinationY,
        //     destinationZ,
        //     m_data->m_Race,
        //     m_data->m_nClass,
        //     m_data->m_Sex,
        //     level,
        //     loggedPositionX,
        //     loggedPositionY,
        //     level,
        //     loggedPositionZ,
        //     actorName,
        //     accountName)
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,"
        //     L"%s,%s,,,",
        //     static_cast<long long>(0x45b),
        //     static_cast<long long>(databaseId),
        //     static_cast<long long>(accountId),
        //     static_cast<long long>(destinationX),
        //     static_cast<long long>(destinationY),
        //     static_cast<long long>(destinationZ),
        //     static_cast<long long>(m_data->m_Race),
        //     static_cast<long long>(m_data->m_nClass),
        //     static_cast<long long>(m_data->m_Sex),
        //     static_cast<long long>(level),
        //     static_cast<long long>(loggedPositionX),
        //     static_cast<long long>(loggedPositionY),
        //     static_cast<long long>(level),
        //     static_cast<long long>(loggedPositionZ),
        //     actorName,
        //     accountName)
        Log.AddT(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,"
            u"%s,%s,,,",
            static_cast<long long>(0x45b), static_cast<long long>(databaseId), static_cast<long long>(accountId),
            static_cast<long long>(destinationX), static_cast<long long>(destinationY), static_cast<long long>(destinationZ),
            static_cast<long long>(m_data->m_Race), static_cast<long long>(m_data->m_nClass),
            static_cast<long long>(m_data->m_Sex), static_cast<long long>(level), static_cast<long long>(loggedPositionX),
            static_cast<long long>(loggedPositionY), static_cast<long long>(level), static_cast<long long>(loggedPositionZ),
            actorName, accountName
        );
    }

    if (!wasAlive) {
        std::int64_t restoredExperience = 0;
        if (residenceOwnedRestart && pledge != nullptr) {
            CResidence* residence = nullptr;
            if (selectedPoint == RESTART_CASTLE) {
                residence = pledge->GetCastle();
            } else if (selectedPoint == RESTART_AGIT) {
                residence = pledge->GetWorldAgit();
            } else if (selectedPoint == RESTART_FORTRESS) {
                residence = pledge->m_pFortress;
            }
            if (residence != nullptr && (residence->m_nDecoFunc & 0x40U) != 0U) {
                restoredExperience =
                    static_cast<std::int64_t>(static_cast<double>(-m_nDownExp) * residence->m_AgitDecoEffect.dExpRestore);
            } else if (instantAgit != nullptr) {
                restoredExperience =
                    static_cast<std::int64_t>(static_cast<double>(-m_nDownExp) * instantAgit->GetExpRestore());
            }
        } else if (selectedPoint == RESTART_ORIGINAL_PLACE) {
            const int itemClassId = g_RestartOriginalManager.HaveRestartItem(this);
            if (itemClassId > 0) {
                if (CItem* const item = m_inv.GetFirstItemByClassID(itemClassId, false, false)) {
                    db.RequestDestroyItem(item, 1, this, 0);
                }
                restoredExperience = GetResurrectExpPremium(this, 200.0);
                SendSystemMessage(SystemMessageId::RESURRECTED_USING_FEATHER_OF_BLESSING);
                if (restoredExperience > 0) {
                    static_cast<CCreature*>(this)->ExpInc(restoredExperience, false);
                    m_nDownExp = 0;
                    db.SaveDieDropExp(*this, 0);
                    db.SendSaveCharacterInfo(this, true, true);
                }
                if (const CSkillInfo* const skill = g_SkillDB.GetSkillInfo(CSkillKey(0x1b60, 1, 0), this)) {
                    InfectedSkillBy(nullptr, skill, nullptr, runtime::crt::Time32Now());
                }
            }
        } else if (selectedPoint == RESTART_ORIGINAL_PLACE_LIMIT) {
            if (CItem* const item = m_inv.GetFirstItemByClassID(0x574c, false, false)) {
                db.RequestDestroyItem(item, 1, this, 0);
            }
            ++m_nRestartItemCount;
            db.RequestSetDailyCount(this, 1);
            restoredExperience = GetResurrectExp(this, 200.0);
            if (CUserSocket* const socket = GetSocket()) {
                socket->Send(
                    "chccc", 0x62, SystemMessageId::ADVENTURER_SONG_USED_WITH_REMAINING_USES, 1, 0x14,
                    static_cast<char>(5 - m_nRestartItemCount)
                );
            }
        } else {
            if (selectedPoint == RESTART_BY_NPC && !NpcServer::Inst()->IsConnect()) {
                SendSystemMessage(SystemMessageId::CANNOT_SUMMON_PET_NPC_SERVER_UNAVAILABLE);
                SendDiePacket(true);
                m_bIsRestarting = false;
                return false;
            }
            restoredExperience = GetResurrectExp(this, 200.0);
        }

        if (restoredExperience > 0) {
            static_cast<CCreature*>(this)->ExpInc(restoredExperience, false);
            m_nDownExp = 0;
            db.SaveDieDropExp(*this, 0);
            db.SendSaveCharacterInfo(this, true, true);
        } else if (selectedPoint != RESTART_ORIGINAL_PLACE && selectedPoint != RESTART_ORIGINAL_PLACE_LIMIT) {
            m_nDownExp = 0;
            db.SaveDieDropExp(*this, 0);
        }
    }

    ValidateChanged(VB_MAX);
    CheckAbnormalVisualEffect();
    m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_data->m_bIsAlive, 1, 0x61);
    m_nTimeToDie = 0;
    if (m_nDownExp != 0) {
        m_nDownExp = 0;
        db.SaveDieDropExp(*this, 0);
    }

    if (CUserSocket* const socket = GetSocket()) {
        // Current 0x00bc8f4b loads User::Id() from +0x18 as the S_REVIVE
        // object-id payload. Sending zero leaves the client-side actor in its
        // death pose even though the server has restored HP and position.
        socket->Send("cd", S_REVIVE, Id());
        socket->Send("cdddd", 0xf3, 0, 0, 0, 0);
    }
    if (m_nRestartItemCount > 0 && selectedPoint != RESTART_ORIGINAL_PLACE_LIMIT) {
        m_nRestartItemCount = 0;
    }

    if (IsRestartAtCurrentPosition(selectedPoint) && g_ptlUserMap != nullptr) {
        g_ptlUserMap->BroadcastToNeighborExceptSelf(this, 0x80000, GetPos(), "cd", 1, Id());
    }

    if (m_data->m_bRestartEventSet) {
        class TLMFilter final : public CTLMFilterDist {
        public:
            TLMFilter(const int inZoneId, const FVector& position, const double distance) :
                CTLMFilterDist(inZoneId, position, distance) {}

            bool CanDo(CObject* object) override { return CTLMFilterDist::CanDo(object) && object->IsNPC(); }
        };

        class TLMFunc final : public CTLMFunc {
        public:
            explicit TLMFunc(std::vector<CSafePointer2<CCreature>>* creatures) : creatures_(creatures) {}

            bool CanDo(CObject* object, double) override {
                if (!object->IsPC() || (!object->IsDeleting() && object->m_Deleted == 0)) {
                    creatures_->push_back(CSafePointer2<CCreature>(object->CastCreature()));
                }
                return true;
            }

        private:
            std::vector<CSafePointer2<CCreature>>* creatures_;
        };


        const FVector position = GetPos();
        std::vector<CSafePointer2<CCreature>> creatures;
        if (m_data->m_nRestartEventDistance > 0x800 || m_data->m_nRestartEventDistance < 0) {
            m_data->m_nRestartEventDistance = 0x800;
        }
        TLMFilter filter(static_cast<int>(GetInZoneID()), position, static_cast<double>(m_data->m_nRestartEventDistance));
        TLMFunc function(&creatures);
        FOR_ALL_NEIGHBOR_CREATURE_DIST(&filter, &function);

        for (int index = 0; index < static_cast<int>(creatures.size()); ++index) {
            CSPointer<CCreature> neighbor = runtime::CheckedVectorAt(creatures, index).FindObjectSP();
            if (neighbor && neighbor->IsNPC()) {
                g_npcserver.Send("cdd", 0x8d, static_cast<int>(neighbor->Id()), static_cast<int>(Id()));
            }
        }
        m_data->m_bRestartEventSet = false;
    }

    if (m_RestartPoint == RESTART_BY_NPC) {
        if (CItem* const item = m_inv.GetFirstItemByClassID(m_RestartItemId, false, false)) {
            static_cast<CCreature*>(this)->UseItem(*item, 0, false, 0);
        }
    } else if (m_RestartPoint == RESTART_BY_AGATHION && m_RestartSkillId > 0 && m_RestartSkillId != 0x5a86) {
        static_cast<CCreature*>(this)->SkillAction(m_RestartSkillId, false, false, nullptr, nullptr, 0, 0);
    }

    m_RestartItemId = 0;
    m_RestartSkillId = 0;
    m_RestartSkillLevel = 0;
    return true;
}

void User::ValidateLocation(const FVector& clientPosition, char validationType) {
    // Current L2Server 0x00bbc560.
    L2_GUARD_FUNCTION();
    if (!m_data->m_bIsActive || m_data->container_index < 0) {
        return;
    }

    const FVector serverPosition = m_data->GetPos();
    CGeoData* const geo = CWorld::Inst().GetGeo();
    const int zoneX = (runtime::AddInt32Wrapping(static_cast<int>(clientPosition.x), 0x50000) >> 15) + 10;
    const int zoneY = (runtime::AddInt32Wrapping(static_cast<int>(clientPosition.y), 0x40000) >> 15) + 10;
    if (geo->GetZone(zoneX, zoneY) == nullptr) {
        TeleportToLocation(
            static_cast<int>(serverPosition.x), static_cast<int>(serverPosition.y), static_cast<int>(serverPosition.z),
            GetInZoneID(), 0x10000, TeleportSourceType::UserValidateLocation1, nullptr, 0
        );
        return;
    }

    const double dx = serverPosition.x - clientPosition.x;
    const double dy = serverPosition.y - clientPosition.y;
    const double dz = serverPosition.z - clientPosition.z;
    if (dx * dx + dy * dy + dz * dz < 250000.0) {
        return;
    }

    if (m_nSpeedHackSuspectCount < 0x1f) {
        ++m_nSpeedHackSuspectCount;
    } else {
        GetSocket()->Send("ch", 0xfe, 0x139);
        m_nSpeedHackSuspectCount = 0;
    }

    if ((IsRidingWyvern() && m_data->m_bIsAlive != 0) || IsCameraMode()) {
        CWorld::Inst().MoveTo(this, clientPosition, false);
        return;
    }
    if (m_bFlyMoving || m_AreaMod.m_bIsNearShuttle) {
        return;
    }

    if (dx * dx + dy * dy > 1048576.0) {
        TeleportToLocation(
            static_cast<int>(serverPosition.x), static_cast<int>(serverPosition.y), static_cast<int>(serverPosition.z),
            GetInZoneID(), 0x10000, TeleportSourceType::UserValidateLocation2, nullptr, 0
        );
        // Log.Add(
        //     LOG_ERROR,
        //     L"MyPOS [%d/%d/%d] ClientPos[%d][%d][%d]",
        //     static_cast<int>(serverPosition.x),
        //     static_cast<int>(serverPosition.y),
        //     static_cast<int>(serverPosition.z),
        //     static_cast<int>(clientPosition.x),
        //     static_cast<int>(clientPosition.y),
        //     static_cast<int>(clientPosition.z))
        // Log.Add(
        //     LOG_WARN,
        //     "[movement] Position correction requested for user object %u: server (%d, %d, %d), client (%d, %d, %d)
        //     horizontal difference exceeds 1024 units.", static_cast<unsigned>(m_nId), static_cast<int>(serverPosition.x),
        //     static_cast<int>(serverPosition.y),
        //     static_cast<int>(serverPosition.z),
        //     static_cast<int>(clientPosition.x),
        //     static_cast<int>(clientPosition.y),
        //     static_cast<int>(clientPosition.z))
        L2_LOG_WARN(
            Log,
            "[movement] Position correction requested for user object %u: server (%d, %d, %d), client (%d, %d, %d); "
            "horizontal difference exceeds 1024 units.",
            static_cast<unsigned>(m_nId), static_cast<int>(serverPosition.x), static_cast<int>(serverPosition.y),
            static_cast<int>(serverPosition.z), static_cast<int>(clientPosition.x), static_cast<int>(clientPosition.y),
            static_cast<int>(clientPosition.z)
        );
        // ActionFailed(0)
        ActionFailed(AF_DEFAULT);
    }

    GetSocket()->Send(
        "cdddddc", 0x79, static_cast<int>(m_nId), static_cast<int>(serverPosition.x), static_cast<int>(serverPosition.y),
        static_cast<int>(serverPosition.z), m_data->GetDir().Yaw & 0xffff, static_cast<int>(validationType)
    );
}

void User::ValidateLocationInVehicle(const FVector& position, int, int vehicleServerId) {
    // Current L2Server 0x00bbfd3c.
    L2_GUARD_FUNCTION();
    if (vehicleServerId <= 0) {
        return;
    }
    const int containerVehicleId = CContainerDB::Inst().GetVehicleID(m_data->container_index);
    if (containerVehicleId != vehicleServerId) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[Vehicle] vehicleServerId is not equal to "
        //     "Container's nVehicleID")
        // Log.Add(
        //     LOG_WARN,
        //     "[movement] Ignoring vehicle-position update for user object %u: requested vehicle ID %d differs from
        //     container vehicle ID %d.", static_cast<unsigned>(m_nId), vehicleServerId, containerVehicleId)
        L2_LOG_WARN(
            Log,
            "[movement] Ignoring vehicle-position update for user object %u: requested vehicle ID %d differs from container "
            "vehicle ID %d.",
            static_cast<unsigned>(m_nId), vehicleServerId, containerVehicleId
        );
        return;
    }

    CSPointer<CVehicle> vehicle = CVehicle::FindObjectSP(static_cast<std::uint32_t>(vehicleServerId));
    if (vehicle && vehicle->IsVehicle()) {
        CWorld::Inst().MoveToRelatively(this, vehicle->CastVehicle(), position);
        CCreature::ValidateLocationInVehicle(static_cast<unsigned int>(vehicleServerId));
    }
}

void User::ValidateLocationInAirShip(const FVector& position, int, int airShipServerId) {
    // Current L2Server 0x00bbfef8.
    L2_GUARD_FUNCTION();
    if (airShipServerId <= 0) {
        return;
    }
    // if (static_cast<int>(GetMyAirShipID()) != airShipServerId) {
    const unsigned int currentAirShipId = GetMyAirShipID();
    if (static_cast<int>(currentAirShipId) != airShipServerId) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[AirShip][%s][%d] airshipServerId is not equal "
        //     "to Container's nAirShipID ",
        //     L"user_world.cpp",
        //     0x1360)
        // Log.Add(
        //     LOG_WARN,
        //     "[movement] Ignoring airship-position update for user object %u: requested airship ID %d differs from current
        //     airship ID %u.", static_cast<unsigned>(m_nId), airShipServerId, currentAirShipId)
        L2_LOG_WARN(
            Log,
            "[movement] Ignoring airship-position update for user object %u: requested airship ID %d differs from current "
            "airship ID %u.",
            static_cast<unsigned>(m_nId), airShipServerId, currentAirShipId
        );
        return;
    }

    CAirShip* const airShip = g_AirShipSmartIdManager.GetObjectW(static_cast<std::uint32_t>(airShipServerId));
    if (airShip == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[AirShip][%s][%d] pAirShip is null ",
        //     L"user_world.cpp",
        //     0x1359)
        // Log.Add(
        //     LOG_WARN,
        //     "[movement] Ignoring airship-position update for user object %u: airship ID %d was not found.",
        //     static_cast<unsigned>(m_nId),
        //     airShipServerId)
        L2_LOG_WARN(
            Log, "[movement] Ignoring airship-position update for user object %u: airship ID %d was not found.",
            static_cast<unsigned>(m_nId), airShipServerId
        );
        return;
    }
    CWorld::Inst().MoveToRelatively(this, airShip, position);
    CCreature::ValidateLocationInAirShip(static_cast<unsigned int>(airShipServerId));
}

void User::ValidateLocationInShuttle(const FVector& position, int, int shuttleServerId) {
    // Current L2Server 0x00bc0058.
    L2_GUARD_FUNCTION();
    if (shuttleServerId <= 0) {
        return;
    }
    const int containerShuttleId = CContainerDB::Inst().GetShuttleID(m_data->container_index);
    if (containerShuttleId != shuttleServerId) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[Shuttle] ShuttleServerId is not equal to "
        //     "Container's nShuttleID")
        // Log.Add(
        //     LOG_WARN,
        //     "[movement] Ignoring shuttle-position update for user object %u: requested shuttle ID %d differs from
        //     container shuttle ID %d.", static_cast<unsigned>(m_nId), shuttleServerId, containerShuttleId)
        L2_LOG_WARN(
            Log,
            "[movement] Ignoring shuttle-position update for user object %u: requested shuttle ID %d differs from container "
            "shuttle ID %d.",
            static_cast<unsigned>(m_nId), shuttleServerId, containerShuttleId
        );
        return;
    }

    CShuttle* const shuttle = CShuttle::s_ShuttleSmartIdManager.GetObjectW(static_cast<std::uint32_t>(shuttleServerId));
    if (shuttle != nullptr) {
        CWorld::Inst().MoveToRelatively(this, shuttle, position);
        CCreature::ValidateLocationInShuttle(static_cast<unsigned int>(shuttleServerId));
    }
}

bool User::OnValidateLocationPacket(int x, int y, int z, int heading, int carrierId, char validationType) {
    // Current L2Server 0x00bcae98.
    L2_GUARD_FUNCTION();
    if (g_WorldPlaneCollision.IsInPlaneCollisionZone(GetPos())) {
        return false;
    }

    const FVector position{x, y, z};
    if (IsInAirShip()) {
        if (carrierId == 0) {
            CAirShip* const airShip = g_AirShipSmartIdManager.GetObjectW(GetMyAirShipID());
            if (airShip != nullptr && airShip->IsDriver(this)) {
                airShip->OnValidateLocation(position);
            }
        } else {
            ValidateLocationInAirShip(position, heading, carrierId);
        }
        return false;
    }

    if (IsInVehicle() && carrierId != 0) {
        ValidateLocationInVehicle(position, heading, carrierId);
        return false;
    }

    if (IsInShuttle()) {
        if (carrierId != 0) {
            CShuttle* const shuttle = CShuttleManager::Inst().GetShuttle(carrierId);
            if (shuttle != nullptr) {
                ValidateLocationInShuttle(position, heading, static_cast<int>(shuttle->Id()));
            }
            return false;
        }
    }

    if (!IsInVehicle() && carrierId == 0) {
        ValidateLocation(position, validationType);
    }
    return false;
}

void User::OnTeleport() {
    // Current L2Server.exe 0x00bbf90c.
    L2_GUARD_FUNCTION();

    StopStore(false, false);
    StopMove();
    DoNothing();
    DetachAllChannelling();

    CFishingController* const fishing = m_pFishingController;
    if (fishing != nullptr && (fishing->m_FishingState != FISHINGS_NONE || fishing->m_bIsAutoFishing == true)) {
        GetController()->DoAutoFishing(false);
    }

    CStaticObject* const sittingObject = m_spSittingChair.GetStaticObject();
    CChair* const chair = sittingObject != nullptr ? sittingObject->CastChair() : nullptr;
    if (chair == nullptr) {
        return;
    }

    DoNothing();
    {
        L2_GUARD(*m_lock);
        if (m_data->m_StopMode != STP_STAND && m_data->m_StopMode != STP_STORE) {
            m_data->m_StopMode = STP_STAND;
            ValidateOrgSpeed();
            ValidateHPRegen();
            chair->ClearBy(GetSmartID());
            m_spSittingChair = nullptr;
            BroadcastWaitType(0);
            m_data->m_bIsFakeDeath = false;
        }
    }
}

bool User::TeleportWithAirShip(CAirShip* airShip, FVector position, unsigned int inZoneId) {
    // Current L2Server 0x00bbfb0c.
    L2_GUARD_FUNCTION();
    GetController()->Disappear();
    // Current 0x00bbfbb0..0x00bbfbb5 uses CObject slot +0xe0 and stages
    // the destination for the later world transition.
    SetNextInZoneID(inZoneId);

    const int yaw = airShip->GetDir().Yaw & 0xffff;
    const FVector origin = GetPos();
    const FVector axis{0, 0, 1};
    const FVector rotated = FVector::RotateAngleAxis(&position, yaw, &axis);
    const FVector destination{
        origin.x + rotated.x,
        origin.y + rotated.y,
        origin.z + rotated.z,
    };

    GetSocket()->Send(
        "cddddddd", 0x22, static_cast<int>(Id()), runtime::Win64CvttDoubleToInt32(destination.x),
        runtime::Win64CvttDoubleToInt32(destination.y), runtime::Win64CvttDoubleToInt32(destination.z), 0,
        GetDir().Yaw & 0xffff, 0
    );
    m_nCommitTick = 0xc3;

    const bool leaving = CWorld::Inst().LeaveWorld(this, destination, false);
    if (leaving) {
        GetSocket()->Send(
            "chddddddd", 0xfe, 0x14a, static_cast<int>(Id()), runtime::Win64CvttDoubleToInt32(destination.x),
            runtime::Win64CvttDoubleToInt32(destination.y), runtime::Win64CvttDoubleToInt32(destination.z), 0,
            GetDir().Yaw & 0xffff, 0
        );
    }
    return leaving;
}

bool User::TeleportToLocation(
    int x,
    int y,
    int z,
    unsigned int inZoneId,
    int heading,
    TeleportSourceType::Enum source,
    CCreature* requestor,
    int unknown
) {
    // Current L2Server 0x00bc5f24.
    L2_GUARD_FUNCTION();
    CPledgeGameField* const pledgeGameField = m_PledgeGameUser.GetField();
    if (pledgeGameField != nullptr && pledgeGameField->m_bIsUnRegist && source != TeleportSourceType::PledgeGameStart) {
        return false;
    }

    CGeoData* const geo = CWorld::Inst().GetGeo();
    if ((static_cast<int>(m_data->m_moveType) != CMT_FLY || m_data->m_bIsAlive == 0) && !IsCameraMode() && !m_bIsObserving) {
        z = geo->FindGround(x, y, z);
    }
    if (static_cast<int>(m_data->m_moveType) == CMT_FLY) {
        z = std::clamp(z, -1000, 6000);
    }

    const int zoneX = (runtime::AddInt32Wrapping(x, 0x50000) >> 15) + 10;
    const int zoneY = (runtime::AddInt32Wrapping(y, 0x40000) >> 15) + 10;
    if (geo->GetZone(zoneX, zoneY) == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Try to teleport to the invalid Position[%d, %d, %d, %d] "
        //     "at file[%s], line[%d]",
        //     x,
        //     y,
        //     z,
        //     static_cast<int>(inZoneId),
        //     "user_world.cpp",
        //     0xe99)
        // Log.Add(
        //     LOG_ERROR,
        //     "[teleport] Rejected for user object %u: geodata zone (%d, %d) is missing for destination (%d, %d, %d),
        //     instance %u, source %d.", static_cast<unsigned>(m_nId), zoneX, zoneY, x, y, z, inZoneId,
        //     static_cast<int>(source))
        L2_LOG_ERROR(
            Log,
            "[teleport] Rejected for user object %u: geodata zone (%d, %d) is missing for destination (%d, %d, %d), "
            "instance %u, source %d.",
            static_cast<unsigned>(m_nId), zoneX, zoneY, x, y, z, inZoneId, static_cast<int>(source)
        );
        runtime::process::ReportCallStack(LOG_ERROR, false);
        m_bIsRestarting = false;
        if (source == TeleportSourceType::UserCallPCVerified) {
            _DoneManiItemJob(ManipulateType::MANI_CALL_PC, 0);
        } else {
            runtime::raw_atomic::ExchangeI32(&m_nWaitGetItemOnce, 0);
        }
        return false;
    }

    if (CAirShip* const airShip = GetMyAirShip()) {
        runtime::raw_atomic::ExchangeI32(&m_nWaitGetItemOnce, 0);
        return TeleportWithAirShip(airShip, FVector{x, y, z}, inZoneId);
    }

    if (!m_data->m_bIsActive) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] invalid m_bIsActive, teleport quit",
        //     "user_world.cpp",
        //     0xebf)
        // Log.Add(
        //     LOG_WARN,
        //     "[teleport] Rejected for inactive user object %u: destination (%d, %d, %d), instance %u, source %d.",
        //     static_cast<unsigned>(m_nId),
        //     x,
        //     y,
        //     z,
        //     inZoneId,
        //     static_cast<int>(source))
        L2_LOG_WARN(
            Log, "[teleport] Rejected for inactive user object %u: destination (%d, %d, %d), instance %u, source %d.",
            static_cast<unsigned>(m_nId), x, y, z, inZoneId, static_cast<int>(source)
        );
        if (source == TeleportSourceType::InzoneEnterZone && (GetParty() != nullptr || GetMPCC() != nullptr)) {
            // Noise: The same inactive-user refusal is already reported above with destination, instance and source.
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d] teleport error to enter inzone, "
            //     "name[%s] dbid[%d]",
            //     "user_world.cpp",
            //     0xec5,
            //     GetName(),
            //     static_cast<int>(GetDBID()))
        }
        return false;
    }

    GetController()->Disappear();
    m_data->m_bIsActive = false;
    // Current 0x00bc62a7..0x00bc62b0 calls CObject vslot +0xe0,
    // which stages the destination through SetNextInZoneID (slot 28).
    SetNextInZoneID(inZoneId);
    if (heading < 0x10000) {
        m_data->SetDirByInt(heading);
    }

    CUserSocket* const socket = GetSocket();
    socket->Send("cddddddd", 0x22, static_cast<int>(Id()), x, y, z, unknown, m_data->GetDirByInt() & 0xffff, 0);
    // ActionFailed(0)
    ActionFailed(AF_DEFAULT);

    CLogFormatter formatter;
    formatter.WriteLog2_1(0x32b, this, requestor, x, y, z, static_cast<int>(m_nId), inZoneId, static_cast<int>(source), 0);
    m_nCommitTick = 0xc3;

    {
        FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
            this, summon,
            // Recovered source entered the action directly here. Current
            // 0x00bc5f24 wraps every non-null summon in "in_code" first.
            int summonZ = z;
            if (static_cast<int>(summon->m_data->m_moveType) != CMT_FLY) { summonZ = geo->FindGround(x, y, z); } summon
                ->TeleportToLocation(
                    x, y, summonZ, inZoneId, 0x10000, static_cast<TeleportSourceType::Enum>(0x28), this, unknown
                );
            g_npcserver.Send("cdd", 0x37, static_cast<int>(summon->Id()), 5);
        );
    }

    const bool leaving = CWorld::Inst().LeaveWorld(this, FVector{x, y, z}, false);
    if (!leaving) {
        m_data->m_bIsActive = true;
    }
    socket->Send("chddddddd", 0xfe, 0x14a, static_cast<int>(Id()), x, y, z, unknown, m_data->GetDirByInt() & 0xffff, 0);
    return leaving;
}

bool User::OnAppearingPacket() {
    // Current L2Server.exe 0x00bc0184.
    L2_GUARD_FUNCTION();
    if (IsDeleting() || m_Deleted != 0) {
        // Noise: A late appearing packet after logout is ignored. It is not a new world-entry failure.
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] enter world after logout",
        //     L"user_world.cpp",
        //     0x1389)
        return false;
    }

    if (m_data->m_bIsActive) {
        LogAbnormalAction(AUA_DUPLICATED_APPEARING, false, 0, 0, 0, 0);
        if (GetInstantZone() != nullptr && GetNextInZoneID() != GetInZoneID()) {
            // Noise: Duplicate appearing packets are already recorded by LogAbnormalAction above. The pending zone
            // transition does not need another error. Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR] Before User leave inzone, "
            //     L"C_APPEARING packet is arrived")
        }
        return false;
    }

    m_data->m_bIsActive = true;
    if (CInstantZone* const instantZone = GetInstantZone()) {
        if (GetNextInZoneID() != GetInZoneID()) {
            FVector returnPosition{};
            instantZone->LeaveZoneWithoutTeleport(this, returnPosition, false);
        }
    }
    static_cast<void>(GetInZoneID());
    static_cast<void>(GetNextInZoneID());

    if (m_data->m_bIsAlive == 0 && m_data->m_bCanBeResurrected) {
        m_data->m_dwResurrectTime = runtime::GetTickCount();
        SendResurrectionConfirmDlg();
    }

    const FVector position = GetPos();
    CLogFormatter formatter;
    formatter.WriteLog2_1(
        0x32b, this, this, static_cast<std::int64_t>(position.x), static_cast<std::int64_t>(position.y),
        static_cast<std::int64_t>(position.z), 0, GetNextInZoneID(), 0, 1
    );

    if (IsInAirShip()) {
        CAirShip* const airShip = g_AirShipSmartIdManager.GetObjectW(GetMyAirShipID());
        if (airShip != nullptr) {
            airShip->OnAppearingPacket(this);
            return false;
        }
    }

    CWorld::Inst().EnterWorld(this, position, GetNextInZoneID(), false, false);
    SendItemList(false, false, nullptr);
    SendRelationChanged(nullptr);
    ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_TELEPORT});
    if (m_data->m_nDominionSiegeStatus != 0) {
        SendDominionStartEnd(true, nullptr);
    }
    PayPendingTeleportItem();
    g_FieldCycleSystem.OnUserTeleport(this);
    return false;
}

void User::SetDieWithSpecialResurrection(const double recoverHp, const double recoverMp, const double recoverCp) {
    // Current L2Server.exe 0x00bbe50c, original owner user_world.cpp.
    L2_GUARD_FUNCTION();

    m_SkillDurationMod.m_dRecoverHPPer = recoverHp;
    m_SkillDurationMod.m_dRecoverMPPer = recoverMp;
    m_SkillDurationMod.m_dRecoverCPPer = recoverCp;
    ++m_SkillDurationMod.m_nResurrectionSpecial;
    runtime::raw_atomic::ExchangeI32(&m_data->m_bCanResurrection, 1);

    constexpr std::size_t nameCapacity =
        sizeof(m_data->m_sResurrectSpellerName) / sizeof(m_data->m_sResurrectSpellerName[0]);
    std::fill_n(m_data->m_sResurrectSpellerName, nameCapacity, char16_t{});
    const std::u16string name = std::u16string(GetName());
    std::copy_n(name.begin(), std::min(name.size(), nameCapacity - 1), m_data->m_sResurrectSpellerName);
    m_data->m_nResurrectExp = GetResurrectExp(this, 200.0);
    m_data->m_dwResurrectTime = runtime::GetTickCount();
    SendResurrectionConfirmDlg();
}

void User::OnDiePenaltyInBF() {
    // Current L2Server.exe 0x00bbe668, original owner user_world.cpp.
    L2_GUARD_FUNCTION();
    if (m_data->m_nPKCount == m_data->m_nPKPardonCount && m_data->m_nLevel < 10) {
        return;
    }

    int level = 0;
    {
        L2_GUARD(m_InfectedSkillsSect);
        for (const InfectedSkillType& infected : m_InfectedSkills) {
            const CSkillInfo* const skill = infected.pSkillInfo;
            if (skill != nullptr && skill->m_nSkillId == 0x161c) {
                level = skill->m_nLevel;
                if (skill->m_pNextLevel != nullptr) {
                    ++level;
                }
            }
        }
    }
    if (level < 1) {
        level = 1;
    }

    const CSkillInfo* const skill = g_SkillDB.GetSkillInfo(CSkillKey(0x161c, level, 0), this);
    if (skill != nullptr) {
        InfectedSkillBy(nullptr, skill, nullptr, 0);
    }
}

void User::ChangeKillScoreInSiege() {
    // Current L2Server.exe 0x00bbe804, original owner user_world.cpp.
    L2_GUARD_FUNCTION();
    CResidence* const residence = g_ResidenceDB.GetWhoseBattleFieldWithBattleCampZone(GetPos());
    if (residence == nullptr) {
        return;
    }

    const int residenceId = ResidenceId(residence);
    if (!residence->IsCastle()) {
        return;
    }

    const std::uint32_t castleIndex = static_cast<std::uint32_t>(residenceId) - 1U;
    int userSiegeId = -1;
    if (castleIndex < 9U) {
        userSiegeId = m_nCastleSiegeId[castleIndex];
    } else if (castleIndex == 9U) {
        // Current code accepts ten unsigned indices. Physical slot nine is
        // the first m_nCastleSiegeStatus lane.
        userSiegeId = m_nCastleSiegeStatus[0];
    } else {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[%s][%d] castle_index is wrong!! [%d]",
        //     L"c:\\work\\lineage2build\\server\\main\\l2server\\User.h",
        //     0xd1d,
        //     castleIndex)
        // Log.Add(
        //     LOG_ERROR,
        //     "[siege] Invalid castle index %u (residence ID %d) when updating the score for user object %u. Valid indices
        //     are 0..9.", static_cast<unsigned>(castleIndex), residenceId, static_cast<unsigned>(m_nId))
        L2_LOG_ERROR(
            Log,
            "[siege] Invalid castle index %u (residence ID %d) when updating the score for user object %u; valid indices "
            "are 0..9.",
            static_cast<unsigned>(castleIndex), residenceId, static_cast<unsigned>(m_nId)
        );
        runtime::process::ReportNamedCallStack(L"Siege", LOG_ERROR, false);
    }

    if (residenceId != userSiegeId) {
        return;
    }
    CCastle* const castle = residence->CastCastle();
    CPledge* const pledge = GetPledge();
    if (castle == nullptr || pledge == nullptr) {
        return;
    }

    if (castle->IsAttackerPledge(pledge)) {
        castle->IncreaseAttakerScore();
    } else if (castle->IsDefenderPledge(pledge)) {
        castle->IncreaseDefenderScore();
    }
    RelayClient::Inst()->SendKillScoreInSeigePacket(castle);
}

void User::DieCursedUser() {
    // Current L2Server.exe 0x00bca090, original owner user_world.cpp.
    L2_GUARD_FUNCTION();

    const int initialRace = static_cast<int>(m_data->m_Race);
    const int initialSex = static_cast<int>(m_data->m_Sex);
    const int initialClass = static_cast<int>(m_data->m_nClass);
    const int initialLevel = m_data->m_nLevel;

    CSharedItemData* const equippedWeapon = GetEquipedWeapon();
    if (equippedWeapon != nullptr) {
        CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(equippedWeapon->index);
        if (item && item->m_data != nullptr && CCursedWeaponMgr::inst().FindConst(item->m_data->m_nClassID) != nullptr) {
            const int itemClassId = item->m_data->m_nClassID;
            const int originalPk = CCursedWeaponMgr::inst().GetOriginalPKCount(itemClassId);

            if (CCursedWeaponMgr::inst().OnDropByUser(this, item.get())) {
                FVector position = GetPos();
                CGeoData* const geo = CWorld::Inst().GetGeo();
                position.z = static_cast<double>(geo->FindGround(
                    runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
                    runtime::Win64CvttDoubleToInt32(position.z)
                ));

                const int direction = runtime::Win64CvttDoubleToInt32(::genrand64_real2() * 360.0 + 0.0);
                const double angle = (static_cast<double>(direction) * 3.14) / 360.0;
                const int distance = runtime::Win64CvttDoubleToInt32(::genrand64_real2() * 16.0 + 30.0);
                const int offsetY = runtime::Win64CvttDoubleToInt32(std::sin(angle) * static_cast<double>(distance));
                const int offsetX = runtime::Win64CvttDoubleToInt32(std::cos(angle) * static_cast<double>(distance));

                FVector destination{};
                destination.x = position.x + static_cast<double>(offsetX);
                destination.y = position.y + static_cast<double>(offsetY);
                destination.z = position.z + 0.0;
                position = destination;
                position.z = static_cast<double>(geo->FindNearGround(
                    runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
                    runtime::Win64CvttDoubleToInt32(position.z)
                ));

                const std::int64_t itemCount = item->m_data->m_nCount;
                const std::uint32_t lowCountBits = static_cast<std::uint32_t>(itemCount);
                std::int32_t signedLowCount = 0;
                std::memcpy(&signedLowCount, &lowCountBits, sizeof(signedLowCount));
                const std::int64_t loggedDroppedCount = signedLowCount;

                if (!DropItem(item.get(), itemCount, position, true)) {
                    CCursedWeaponMgr::inst().OnDeleted(item.get(), this, DR_DYING);
                }

                GetSocket()->Send("chccd", 0x62, SystemMessageId::DROPPED_ITEM, 1, 3, static_cast<int>(item->m_nId));

                std::array<char16_t, 256> shapeInfo{};
                std::array<char16_t, 256> attributeInfo{};
                std::array<char16_t, 256> ensoulInfo{};
                item->MakeShapeShiftAndVariationLogInfo(shapeInfo.data());
                item->m_itemBaseAttribute.GetLogString(attributeInfo.data());
                item->MakeEnsoulLogInfo(ensoulInfo.data());

                const std::int64_t remainingCount = item->m_data->m_nCount - loggedDroppedCount;
                CLogFormatter formatter;
                formatter.WriteLog4(
                    0x393, this, nullptr, shapeInfo.data(), attributeInfo.data(), ensoulInfo.data(), initialRace, initialSex,
                    initialClass, initialLevel, 1, 0, item->m_data->GetEnchanted(), item->m_data->m_nClassID,
                    loggedDroppedCount, remainingCount, item->GetDBID()
                );

                const int logZ = runtime::Win64CvttDoubleToInt32(position.z);
                const char16_t* const actorName = GetName();
                const unsigned int accountId = GetAccountID();
                const unsigned int databaseId = GetDBID();
#if defined(_WIN32)
                const wchar_t* const accountName = reinterpret_cast<const wchar_t*>(m_sAccountName.data());
#else
                std::array<wchar_t, 16> accountNameBuffer{};
                const wchar_t* const accountName =
                    CopyUtf16ToWide(m_sAccountName.data(), m_sAccountName.size(), accountNameBuffer);
#endif
                // Log.AddT(
                //     LOG_BOTH,
                //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
                //     L"%s,%s,,,",
                //     0x45f,
                //     databaseId,
                //     accountId,
                //     runtime::Win64CvttDoubleToInt32(position.x),
                //     runtime::Win64CvttDoubleToInt32(position.y),
                //     logZ,
                //     initialRace,
                //     initialSex,
                //     initialClass,
                //     initialLevel,
                //     1,
                //     1,
                //     0,
                //     actorName,
                //     accountName)
                // Log.AddT(
                //     LOG_BOTH,
                //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
                //     L"%s,%s,,,",
                //     static_cast<long long>(0x45f),
                //     static_cast<long long>(databaseId),
                //     static_cast<long long>(accountId),
                //     static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.x)),
                //     static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.y)),
                //     static_cast<long long>(logZ),
                //     static_cast<long long>(initialRace),
                //     static_cast<long long>(initialSex),
                //     static_cast<long long>(initialClass),
                //     static_cast<long long>(initialLevel),
                //     static_cast<long long>(1),
                //     static_cast<long long>(1),
                //     static_cast<long long>(0),
                //     actorName,
                //     accountName)
                Log.AddT(
                    LOG_BOTH,
                    u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                    u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
                    u"%s,%s,,,",
                    static_cast<long long>(0x45f), static_cast<long long>(databaseId), static_cast<long long>(accountId),
                    static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.x)),
                    static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.y)), static_cast<long long>(logZ),
                    static_cast<long long>(initialRace), static_cast<long long>(initialSex),
                    static_cast<long long>(initialClass), static_cast<long long>(initialLevel), static_cast<long long>(1),
                    static_cast<long long>(1), static_cast<long long>(0), actorName, accountName
                );
            }

            if (originalPk != -1) {
                m_data->SetPKCount(originalPk);
                SetKarma(0);
                m_nDownExp = 1;
                db.SaveDieDropExp(*this, 1);
            }

            FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(this, summon, static_cast<CCreature*>(summon)->ParamChanged(););
        }
    }

    db.SendSaveCharacterInfo(this, false, true);
    ValidateChanged(VB_ITEMSKILL | VB_SKILLLIST);
    CancelTransform(0, true, SPET_DIE);

    std::uint32_t damagedSeconds = 0;
    if (m_dwDamagedTick != 0) {
        damagedSeconds = (runtime::GetTickCount() - m_dwDamagedTick) / 1000U;
    }
    const int logZ = runtime::Win64CvttDoubleToInt32(GetPos().z);
    const int logY = runtime::Win64CvttDoubleToInt32(GetPos().y);
    const int logX = runtime::Win64CvttDoubleToInt32(GetPos().x);
    const char16_t* const actorNameWide = GetName();
    const int currentLevel = m_data->m_nLevel;
    const std::u16string actorName = std::u16string(actorNameWide);
    const unsigned int accountId = GetAccountID();
    const unsigned int databaseId = GetDBID();

    CLogFormatter formatter;
    formatter.WriteLog(
        0x44d, databaseId, accountId, 0, 0, logX, logY, logZ, nullptr, nullptr, nullptr, initialRace, initialSex,
        initialClass, initialLevel, 0, m_data->m_nExp, currentLevel, damagedSeconds, 0, 0, actorName.c_str(),
        m_sAccountName.data(), nullptr, nullptr, 0
    );
}

void User::DieByCursedUser(User* killer) {
    // Current L2Server.exe 0x00bcabc0, original owner user_world.cpp.
    L2_GUARD_FUNCTION();

    CCursedWeaponMgr::inst().KillPC(killer, this);
    m_nDownExp = 1;
    db.SaveDieDropExp(*this, 1);

    std::uint32_t damagedSeconds = 0;
    if (m_dwDamagedTick != 0) {
        damagedSeconds = (runtime::GetTickCount() - m_dwDamagedTick) / 1000U;
    }
    const int logZ = runtime::Win64CvttDoubleToInt32(GetPos().z);
    const int logY = runtime::Win64CvttDoubleToInt32(GetPos().y);
    const int logX = runtime::Win64CvttDoubleToInt32(GetPos().x);
    const int currentRace = static_cast<int>(m_data->m_Race);
    const int currentSex = static_cast<int>(m_data->m_Sex);
    const int currentClass = static_cast<int>(m_data->m_nClass);
    const int currentLevel = m_data->m_nLevel;
    const char16_t* const actorNameWide = GetName();
    const std::u16string actorName = std::u16string(actorNameWide);
    const unsigned int accountId = GetAccountID();
    const unsigned int databaseId = GetDBID();

    CLogFormatter formatter;
    formatter.WriteLog(
        0x44d, databaseId, accountId, 0, 0, logX, logY, logZ, nullptr, nullptr, nullptr, currentRace, currentSex,
        currentClass, currentLevel, 0, m_data->m_nExp, currentLevel, damagedSeconds, 0, 0, actorName.c_str(),
        m_sAccountName.data(), nullptr, nullptr, 0
    );
}

bool User::Die(CCreature* killer, const int reason) {
    // Current L2Server.exe 0x00bcb2b4, original owner user_world.cpp.
    L2_GUARD_FUNCTION();

    FVector deathPosition = GetPos();
    if (IsRidingWyvern()) {
        FVector groundedPosition = GetPos();
        groundedPosition.z = static_cast<double>(CWorld::Inst().GetGeo()->FindGround(
            runtime::Win64CvttDoubleToInt32(groundedPosition.x), runtime::Win64CvttDoubleToInt32(groundedPosition.y),
            runtime::Win64CvttDoubleToInt32(groundedPosition.z)
        ));
        CWorld::Inst().MoveTo(this, groundedPosition, false);
        deathPosition = groundedPosition;
    }

    if (IsNowTrade()) {
        TradeCancel(false);
    }
    if (killer != nullptr && killer->IsPCOrPCRelatedNPC() &&
        (AreaBasedRule::KilledByPC(GetInZoneID(), &deathPosition) || m_bIsRestarting)) {
        m_data->SetHP(1.0);
        return false;
    }

    int killerCategory = 1;
    if (killer != nullptr) {
        if (killer->IsPCOrPCRelatedNPC()) {
            killerCategory = 0;
        } else if (killer->IsNPC() && (killer->IsBoss() || killer->IsZzoldagu())) {
            killerCategory = 2;
        }
    }
    const double itemDropBonus = m_SkillMod.m_dItemDropBonus[killerCategory];
    const double experienceDropBonus = m_SkillMod.m_dExpDropBonus[killerCategory];
    bool allowItemDrop = true;

    const int resurrectionSpecialCount = m_SkillDurationMod.m_nResurrectionSpecial;
    const auto& resurrectionZoneIds = m_SkillDurationMod.m_vResurrectionInstantZoneIds;
    const bool resurrectionZoneMatches = resurrectionZoneIds.empty() ||
        std::find(resurrectionZoneIds.begin(), resurrectionZoneIds.end(), static_cast<int>(m_data->m_nInstantZoneTypeID)) !=
            resurrectionZoneIds.end();
    const double recoverHp = m_SkillDurationMod.m_dRecoverHPPer;
    const double recoverMp = m_SkillDurationMod.m_dRecoverMPPer;
    const double recoverCp = m_SkillDurationMod.m_dRecoverCPPer;

    if (!CCreature::Die(killer, reason) || m_data->m_bIsAlive != 0) {
        return false;
    }

    ResetAllTacticalSignToMe();
    SendAbnormalStatusInfo();
    g_npcserver.Send("cddd", 0x30, static_cast<int>(Id()), 0, 2);

    if (m_data->m_bDieEventSet) {
        class TLMFilter final : public CTLMFilterDist {
        public:
            TLMFilter(const int inZoneId, const FVector& position, const double distance) :
                CTLMFilterDist(inZoneId, position, distance) {}

            bool CanDo(CObject* object) override { return CTLMFilterDist::CanDo(object) && object->IsNPC(); }
        };

        class TLMFunc final : public CTLMFunc {
        public:
            explicit TLMFunc(std::vector<CSafePointer2<CCreature>>* creatures) : creatures_(creatures) {}

            bool CanDo(CObject* object, double) override {
                if (!object->IsPC() || (!object->IsDeleting() && object->m_Deleted == 0)) {
                    creatures_->push_back(CSafePointer2<CCreature>(object->CastCreature()));
                }
                return true;
            }

        private:
            std::vector<CSafePointer2<CCreature>>* creatures_;
        };

        const FVector eventPosition = GetPos();
        std::vector<CSafePointer2<CCreature>> creatures;
        if (m_data->m_nDieEventDistance > 0x800 || m_data->m_nDieEventDistance < 0) {
            m_data->m_nDieEventDistance = 0x800;
        }
        TLMFilter filter(static_cast<int>(GetInZoneID()), eventPosition, static_cast<double>(m_data->m_nDieEventDistance));
        TLMFunc function(&creatures);
        FOR_ALL_NEIGHBOR_CREATURE_DIST(&filter, &function);

        for (int index = 0; index < static_cast<int>(creatures.size()); ++index) {
            CSPointer<CCreature> neighbor = runtime::CheckedVectorAt(creatures, index).FindObjectSP();
            if (neighbor && neighbor->IsNPC()) {
                g_npcserver.Send("cdd", 0x8c, static_cast<int>(neighbor->Id()), static_cast<int>(Id()));
            }
        }
        m_data->m_bDieEventSet = false;
    }

    const bool inOwnSiegeBattleField = IsInMySiegeBattleField() != 0;
    if (!inOwnSiegeBattleField && !m_AreaMod.m_bIsInBattleField && m_EventMatchUser.m_byEventMatchTeamId == 0) {
        g_npcserver.Send("cdddd", 0xc6, m_data->index, 1, killerCategory, 0);
    }

    if (m_data->m_nDominionSiegeStatus != 0 && killer != nullptr) {
        User* dominionKiller = nullptr;
        if (killer->IsPC()) {
            dominionKiller = killer->CastUser();
        } else if (killer->IsPet()) {
            CPet* const pet = killer->CastPet();
            dominionKiller = pet->GetMaster();
        } else if (killer->IsSummonOrPet()) {
            CSummon* const summon = killer->CastSummon();
            dominionKiller = summon->GetMaster();
        }
        if (dominionKiller != nullptr) {
            g_npcserver.Send(
                "cdddd", 0x86, 2, static_cast<int>(dominionKiller->Id()), m_data->m_nDominionSiegeId, static_cast<int>(Id())
            );
        }
    }

    CFortress::HandleDeleteFlag(this, true);
    static_cast<void>(CCursedWeaponMgr::inst());
    if (m_nCursedWeaponClassId > 0) {
        DieCursedUser();
        if (resurrectionSpecialCount > 0 && resurrectionZoneMatches && m_RestartSkillId != 0x5a86) {
            SetDieWithSpecialResurrection(recoverHp, recoverMp, recoverCp);
        }
        return true;
    }
    if (killer != nullptr && killer->IsPC()) {
        User* cursedKiller = killer->CastUser();
        static_cast<void>(CCursedWeaponMgr::inst());
        if (cursedKiller != nullptr && cursedKiller->m_nCursedWeaponClassId > 0) {
            cursedKiller = killer->CastUser();
            DieByCursedUser(cursedKiller);
            if (resurrectionSpecialCount > 0 && resurrectionZoneMatches && m_RestartSkillId != 0x5a86) {
                SetDieWithSpecialResurrection(recoverHp, recoverMp, recoverCp);
            }
            return true;
        }
    }
    if (killer != nullptr && killer->IsSummonOrPet()) {
        CSummon* summon = killer->CastSummon();
        User* cursedKiller = summon->GetMaster();
        if (cursedKiller != nullptr) {
            summon = killer->CastSummon();
            cursedKiller = summon->GetMaster();
            static_cast<void>(CCursedWeaponMgr::inst());
            if (cursedKiller != nullptr && cursedKiller->m_nCursedWeaponClassId > 0) {
                summon = killer->CastSummon();
                cursedKiller = summon->GetMaster();
                DieByCursedUser(cursedKiller);
                if (resurrectionSpecialCount > 0 && resurrectionZoneMatches && m_RestartSkillId != 0x5a86) {
                    SetDieWithSpecialResurrection(recoverHp, recoverMp, recoverCp);
                }
                return true;
            }
        }
    }

    User* const killerUser = killer != nullptr ? killer->GetUserOrMaster() : nullptr;
    bool mutualWar = false;
    bool oneWayWarAgainst = false;
    bool realEnemy = false;
    bool killerDeclaredVictimOnly = false;
    if (killerUser != nullptr) {
        mutualWar = IsInPledgeWarBoth(killerUser) && m_data->m_nPledgeType != -1 && killerUser->m_data->m_nPledgeType != -1;
        oneWayWarAgainst = IsInPledgeWarAgainst(killerUser) && m_data->m_nPledgeType != -1 &&
            killerUser->m_data->m_nPledgeType != -1 && !mutualWar;
        realEnemy = IsRealEnemyTo(killerUser) || mutualWar || oneWayWarAgainst;
        killerDeclaredVictimOnly = !CheckMyPledgeDeclareWar(killerUser) && killerUser->CheckMyPledgeDeclareWar(this);
    }

    if (m_pOwnThing != nullptr) {
        m_pOwnThing->BroadcastMessage(COwnThing::OTBT_DEAD, EST_CHAR_NAME, GetName(), false);
        HandleOwnThing(killerUser);
    }

    const bool chao = m_data->m_nKarma < 0 &&
        runtime::SubtractInt32Wrapping(m_data->m_nPKCount, m_data->m_nPKPardonCount) >= g_nPKPenaltyCount;

    bool battleContext = realEnemy;
    bool eventOrBattleField = false;
    bool pvpBattleSector = false;
    if (m_AreaMod.m_bIsInBattleField || m_EventMatchUser.m_byEventMatchTeamId != 0) {
        battleContext = true;
        eventOrBattleField = true;

        const int x = runtime::Win64CvttDoubleToInt32(deathPosition.x);
        const int y = runtime::Win64CvttDoubleToInt32(deathPosition.y);
        CGeoZone* const zone = CWorld::Inst().GetGeo()->GetZone(
            (runtime::AddInt32Wrapping(x, 0x50000) >> 15) + 10, (runtime::AddInt32Wrapping(y, 0x40000) >> 15) + 10
        );
        CGeoSector* sector = nullptr;
        if (zone != nullptr) {
            sector = zone->GetSector((x >> 7) & 0xff, (y >> 7) & 0xff);
        }
        const std::uint16_t pvpMask = GeoFieldMask[GEO_FIELD_BATTLE_PVP];
        if (sector != nullptr && (sector->m_Field & pvpMask) == pvpMask) {
            pvpBattleSector = true;
            if (killer != nullptr) {
                battleContext = killer->IsPCOrPCRelatedNPC();
            }
        }
    }

    std::int64_t loggedExperience = m_data->m_nExp;
    const int deathLevel = m_data->m_nLevel;
    if (m_bDecExp && g_nNotDecExp == 0) {
        bool applyExperienceBonus = true;
        if (CPVPMatchSystem::inst().IsPVPPlayer(this) ||
            (killer != nullptr && killer->IsPCOrPCRelatedNPC() && m_AreaMod.m_bIsInPvPZone)) {
            m_nDownExp = 0;
            applyExperienceBonus = false;
        } else {
            const std::int64_t initialDefaultPenalty = static_cast<std::int64_t>(GetDefaultExpDownValue(deathLevel));
            if (AreaBasedRule::ExpDownPanelty(this, initialDefaultPenalty, &m_nDownExp)) {
                applyExperienceBonus = m_nDownExp != 0;
            } else if (killer != nullptr && killer->IsPCOrPCRelatedNPC() &&
                (m_AreaMod.m_bIsInPeaceField ||
                    CAreaDB::inst().FindArea(static_cast<int>(GetInZoneID()), GetPos(), AREAT_PEACE, true) != nullptr)) {
                m_nDownExp = 0;
                applyExperienceBonus = false;
            } else {
                bool handledBySsqArea = false;
                if (m_AreaMod.m_bIsInSSQZone) {
                    CArea* const ssqArea =
                        CAreaDB::inst().FindArea(static_cast<int>(GetInZoneID()), deathPosition, AREAT_SSQ, true);
                    if (ssqArea != nullptr) {
                        m_nDownExp = static_cast<std::int64_t>(GetDefaultExpDownValue(deathLevel) * ssqArea->mExpPenaltyPer);
                        handledBySsqArea = true;
                        allowItemDrop = ssqArea->mIsItemDrop;
                    }
                }

                if (!handledBySsqArea) {
                    if (!battleContext) {
                        m_nDownExp = static_cast<std::int64_t>(GetDefaultExpDownValue(deathLevel));
                    } else {
                        if (pvpBattleSector) {
                            m_nDownExp = 0;
                            applyExperienceBonus = false;
                        } else if (eventOrBattleField) {
                            if (IsInMySiegeBattleField() != 0) {
                                m_nDownExp = 0;
                                applyExperienceBonus = false;
                                OnDiePenaltyInBF();
                            } else {
                                m_nDownExp = static_cast<std::int64_t>(GetDefaultExpDownValue(deathLevel));
                            }
                        } else {
                            double penalty = GetDefaultExpDownValue(deathLevel);
                            if (mutualWar) {
                                penalty *= 0.25;
                            }
                            m_nDownExp = static_cast<std::int64_t>(penalty);
                        }

                        if (!pvpBattleSector && killerUser != nullptr) {
                            CPledge* const killerPledge = killerUser->GetPledge();
                            CPledge* const victimPledge = GetPledge();
                            if (killerPledge != nullptr || victimPledge != nullptr) {
                                CPledge::UpdateSiegeKillDeathPoint(killerPledge, victimPledge);
                            }
                            if (killerPledge != nullptr && victimPledge != nullptr) {
                                if (mutualWar) {
                                    killerPledge->UpdatePledgeWarPoint(
                                        static_cast<int>(victimPledge->Id()), static_cast<int>(victimPledge->GetDbId()), 1,
                                        false
                                    );
                                    victimPledge->UpdatePledgeWarPoint(
                                        static_cast<int>(killerPledge->Id()), static_cast<int>(killerPledge->GetDbId()), -1,
                                        false
                                    );
                                } else if (victimPledge->DidDeclareWar(static_cast<int>(killerPledge->Id())) &&
                                    !IsEinhasadeTransformed() && m_data->m_nKarma >= 0 && m_eDeadDamage != 0xe &&
                                    (!eventOrBattleField || IsInMySiegeBattleField() == 0)) {
                                    killerPledge->CheckDeclareWarBoth(victimPledge);
                                }
                            }
                        }
                    }
                }
            }
        }

        if (!g_bDiePenalty && !IsEinhasadeTransformed() && m_data->m_nKarma >= 0) {
            m_nDownExp = 0;
        }
        if (applyExperienceBonus) {
            m_nDownExp = static_cast<std::int64_t>((experienceDropBonus / 100.0 + 1.0) * static_cast<double>(m_nDownExp));
            if (m_nDownExp != 0) {
                loggedExperience = ExpDown(m_nDownExp);
            }
        }
    }

    if (killerUser != nullptr) {
        CPledge* const killerPledge = killerUser->GetPledge();
        CPledge* const victimPledge = GetPledge();
        if (killerPledge != nullptr && victimPledge != nullptr) {
            const bool fortressEnemies = (killerPledge->m_data->m_nFortressUnderDefenceSiegeId != 0 &&
                                             killerPledge->m_data->m_nFortressUnderDefenceSiegeId ==
                                                 victimPledge->m_data->m_nFortressUnderOffenceSiegeId) ||
                (killerPledge->m_data->m_nFortressUnderOffenceSiegeId != 0 &&
                    killerPledge->m_data->m_nFortressUnderOffenceSiegeId ==
                        victimPledge->m_data->m_nFortressUnderDefenceSiegeId);
            if ((((killerPledge->IsCastleSiegeEnemy(victimPledge) || fortressEnemies) && m_data->m_nPledgeType != -1 &&
                     killerUser->m_data->m_nPledgeType != -1) ||
                    mutualWar) &&
                killerPledge->IsValidNameValue() && victimPledge->IsValidNameValue() && victimPledge->GetNameValue() > 0 &&
                !m_AreaMod.m_bIsInPvPZone && deathLevel > 9) {
                victimPledge->UpdateNameValueByPledgeWar(GetServerId(), -1, killerPledge->GetDbId());
                killerPledge->UpdateNameValueByPledgeWar(killerUser->GetServerId(), 1, victimPledge->GetDbId());
            }
        }
    }

    int deathReason = realEnemy ? 1 : 0;
    if (eventOrBattleField) {
        deathReason += 2;
    }
    if (m_eDeadDamage == 3) {
        deathReason += 4;
    }
    if (m_eDeadDamage == 4) {
        deathReason += 8;
    }
    if (m_eDeadDamage == 5) {
        deathReason += 0x10;
    }
    if (killer != nullptr) {
        if (killer->IsPCOrSummon()) {
            deathReason += 0x20;
        }
        if (killer->IsTrap() || killer->IsPCTrap()) {
            deathReason += 0x40;
        }
    }

    std::uint32_t damagedSeconds = 0;
    if (m_dwDamagedTick != 0) {
        damagedSeconds = (runtime::GetTickCount() - m_dwDamagedTick) / 1000U;
    }
    int penaltyLevel = 0;
    if (killer != nullptr && killer->IsNPC() && killer->CastNPC()->m_pClassConst->m_IsDeathPenalty) {
        penaltyLevel = OnDiePenalty(chao, itemDropBonus);
    }

    const int logX = runtime::Win64CvttDoubleToInt32(deathPosition.x);
    const int logY = runtime::Win64CvttDoubleToInt32(deathPosition.y);
    const int logZ = runtime::Win64CvttDoubleToInt32(deathPosition.z);
    const std::u16string actorName = std::u16string(GetName());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x44d, GetDBID(), GetAccountID(), 0, 0, logX, logY, logZ, nullptr, nullptr, nullptr,
        static_cast<int>(m_data->m_Race), static_cast<int>(m_data->m_Sex), static_cast<int>(m_data->m_nClass), deathLevel,
        m_nDownExp, loggedExperience, m_data->m_nLevel, damagedSeconds, deathReason, penaltyLevel, actorName.c_str(),
        m_sAccountName.data(), nullptr, nullptr, 0
    );

    if (allowItemDrop) {
        OnDieDropItem(eventOrBattleField, itemDropBonus);
    }

    if (m_nDownExp < 0) {
        const std::uint32_t databaseId = GetDBID();
        if (databaseId < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"SaveDieDropExp : DBID(%d) DownExp(%I64d) . (%s)",
            //     static_cast<int>(databaseId),
            //     m_nDownExp,
            //     GetName())
            // Log.Add(
            //     LOG_ERROR,
            //     "[death] Character DB ID %u is invalid for user object %u while saving experience change %lld. Continuing
            //     with the save requests.", static_cast<unsigned>(databaseId), static_cast<unsigned>(m_nId),
            //     static_cast<long long>(m_nDownExp))
            L2_LOG_ERROR(
                Log,
                "[death] Character DB ID %u is invalid for user object %u while saving experience change %lld; continuing "
                "with the save requests.",
                static_cast<unsigned>(databaseId), static_cast<unsigned>(m_nId), static_cast<long long>(m_nDownExp)
            );
        }
        db.SaveDieDropExp(*this, m_nDownExp);
        db.SendSaveCharacterInfo(this, false, true);

        const bool skipKarmaPenalty = RaidServerManager::inst().GetPKKarmaPenaltyType() != RSOT_NORMAL ||
            (realEnemy && !killerDeclaredVictimOnly) || eventOrBattleField;
        if (!skipKarmaPenalty && m_data->m_nKarma < 0) {
            if (m_data->m_nKarma < -720) {
                ChangeKarmaByDelta(720, false);
            } else {
                ChangeKarmaByDelta(-m_data->m_nKarma, false);
                m_bIsNonChaoInNow = true;
                SendSystemMessage_d(SystemMessageId::CASTING_INTERRUPTED, 0);
                SetRelationChanged(false);
            }
            ParamChanged(PCB_CHAR_DIE);
            GetSocket()->Send("chccd", 0x62, SystemMessageId::REPUTATION_CHANGED, 1, 1, m_data->m_nKarma);
            db.SendSaveCharacterInfo(this, false, true);
        }
    } else if (m_nDownExp == 0) {
        m_nDownExp = 1;
        db.SaveDieDropExp(*this, 1);
    }

    if (eventOrBattleField && IsInMySiegeBattleField() != 0) {
        if (m_bKeepExp) {
            runtime::raw_atomic::ExchangeI32(&m_data->m_bCanResurrection, 1);
            m_data->m_nResurrectExp = GetResurrectExp(this, 200.0);
            GetSocket()->Send(
                "cdddSdQdddd", 0xf3, 0x902, 3, 0xc, GetName(), 6, m_data->m_nResurrectExp, 1, m_data->m_nResurrectRatio,
                60000, m_data->m_dwResurrectTime
            );
            BroadcastEmitterSpawn(static_cast<int>(Id()), static_cast<int>(Id()), 1);
        }
        ChangeKillScoreInSiege();
    }

    m_bKeepExp = false;
    m_dwKeepExpTick = 0;
    SendAbnormalStatusInfo();
    if (resurrectionSpecialCount > 0 && resurrectionZoneMatches && m_RestartSkillId != 0x5a86) {
        SetDieWithSpecialResurrection(recoverHp, recoverMp, recoverCp);
    }

    CPVPMatchSystem::inst().UserDie(this, killerUser);
    AbandonAirShipDriver();
    if (!IsInRealWorld()) {
        CInstantZone* const instantZone = GetInstantZone();
        if (instantZone != nullptr &&
            (instantZone->GetConst()->GetZoneType() == IZT_NORMAL ||
                instantZone->GetConst()->GetZoneType() == IZT_PLEDGE_RAID)) {
            GetSocket()->Send(
                "chccc", 0x62, SystemMessageId::INSTANCE_EXPULSION_UNLESS_RESURRECTED_WITHIN_MINUTES, 1, 0x14, 1
            );
            m_nTimeToDie = runtime::GetTickCount();
        }
    }
    if (IsAlreadyHaveMentor() && m_Mentee.CanBeBuffed()) {
        runtime::raw_atomic::ExchangeI32(&m_Mentee.m_nInfectedSkillByMentor, 0);
    }
    if (m_EventMatchUser.m_byEventMatchTeamId != 0) {
        ParamChanged(PCB_CHAR);
    }
    return true;
}

void User::OnEnterWorld(FVector& position) {
    // Current L2Server 0x00bc0f68, original owner user_world.cpp.
    g_ptlUserMap->Push(m_data->index, position, this);
    ResetAreaFlag();
    CheckCompassRegion();
    m_UserStatData.OnEnterWorld();

    if (!m_bIsObserving) {
        return;
    }

    if (AreaBasedRule::SpendPlaytime(m_AreaMod) && !m_AreaMod.m_bIsInBattleField) {
        if (m_bStopTime) {
            GetSocket()->SetEndSitTimeStart();
            m_bStopTime = false;
            return;
        }
    } else if (!m_bStopTime) {
        m_bStopTime = true;
        GetSocket()->SetSitTimeStart();
        return;
    }

    GetSocket()->ShowRemainTime();
}

bool User::OnEnterWorldPacket(CUserSocket* socket, const unsigned char* packet) {
    // Current L2Server.exe 0x00bc6634, original owner user_world.cpp.
    L2_GUARD_FUNCTION();

    std::array<std::uint32_t, 5> clientValues{};
    const unsigned char* cursor = packet;
    for (std::uint32_t& value : clientValues) {
        std::uint8_t byte0 = 0;
        std::uint8_t byte1 = 0;
        std::uint8_t byte2 = 0;
        std::uint8_t byte3 = 0;
        cursor = ::Disassemble(cursor, "cccc", &byte0, &byte1, &byte2, &byte3);
        value = (static_cast<std::uint32_t>(byte0) << 24U) | (static_cast<std::uint32_t>(byte1) << 16U) |
            (static_cast<std::uint32_t>(byte2) << 8U) | static_cast<std::uint32_t>(byte3);
    }

    const std::u16string macAddress = socket->GetMacAddressString();
#if defined(_WIN32)
    const wchar_t* const socketAccountName = reinterpret_cast<const wchar_t*>(socket->GetAccountName());
    // Noise: Account-name preparation belonged only to the replaced operator logs.
    // const wchar_t* const userAccountName =
    //     reinterpret_cast<const wchar_t*>(m_sAccountName.data())
#else
    std::array<wchar_t, 15> socketAccountNameBuffer{};
    const wchar_t* const socketAccountName = CopyUtf16ToWide(
        reinterpret_cast<const char16_t*>(socket->GetAccountName()), socketAccountNameBuffer.size(), socketAccountNameBuffer
    );
    // Noise: Account-name preparation belonged only to the replaced operator logs.
    // std::array<wchar_t, 16> userAccountNameBuffer{}
    // const wchar_t* const userAccountName = TargetUtf16ToHostWide(
    //     m_sAccountName.data(),
    //     m_sAccountName.size(),
    //     userAccountNameBuffer)
#endif
    // Log.Add(
    //     LOG_AUDIT,
    //     L"%I64d,,%I64d,,,,,,%s,,,%I64d,%I64d,%I64d,%I64d,%I64d"
    //     L",,,,,,,%s,,,",
    //     std::int64_t{0x599},
    //     static_cast<std::int64_t>(socket->GetAccountID()),
    //     macAddress.c_str(),
    //     static_cast<std::int64_t>(clientValues[0]),
    //     static_cast<std::int64_t>(clientValues[1]),
    //     static_cast<std::int64_t>(clientValues[2]),
    //     static_cast<std::int64_t>(clientValues[3]),
    //     static_cast<std::int64_t>(clientValues[4]),
    //     socketAccountName)
    Log.Add(
        LOG_AUDIT,
        u"%I64d,,%I64d,,,,,,%s,,,%I64d,%I64d,%I64d,%I64d,%I64d"
        u",,,,,,,%s,,,",
        static_cast<long long>(std::int64_t{0x599}),
        static_cast<long long>(static_cast<std::int64_t>(socket->GetAccountID())), macAddress.c_str(),
        static_cast<long long>(static_cast<std::int64_t>(clientValues[0])),
        static_cast<long long>(static_cast<std::int64_t>(clientValues[1])),
        static_cast<long long>(static_cast<std::int64_t>(clientValues[2])),
        static_cast<long long>(static_cast<std::int64_t>(clientValues[3])),
        static_cast<long long>(static_cast<std::int64_t>(clientValues[4])), socketAccountName
    );

    if (g_bCheckNewInspectorAuth && socket->GetInspectorCRC() != g_NewInspectorDB.GetCRC()) {
        if (!IsBuilder()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NewInspector] [%s] [%s] new inspector data crc error "
            //     L"[ClientCRC:%d, ServerCRC:%d]",
            //     GetName(),
            //     userAccountName,
            //     socket->GetInspectorCRC(),
            //     g_NewInspectorDB.GetCRC())
            // Log.Add(
            //     LOG_WARN,
            //     "[world entry] Rejecting entry for user object %u: inspector data checksum mismatch (client 0x%08x, server
            //     0x%08x), requesting socket closure.", static_cast<unsigned>(m_nId),
            //     static_cast<unsigned>(socket->GetInspectorCRC()),
            //     static_cast<unsigned>(g_NewInspectorDB.GetCRC()))
            L2_LOG_WARN(
                Log,
                "[world entry] Rejecting entry for user object %u: inspector data checksum mismatch (client 0x%08x, server "
                "0x%08x); requesting socket closure.",
                static_cast<unsigned>(m_nId), static_cast<unsigned>(socket->GetInspectorCRC()),
                static_cast<unsigned>(g_NewInspectorDB.GetCRC())
            );
            return true;
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NewInspector] builder char[%s] [%s] new inspector data "
        //     L"crc error [ClientCRC:%d, ServerCRC:%d]",
        //     GetName(),
        //     userAccountName,
        //     socket->GetInspectorCRC(),
        //     g_NewInspectorDB.GetCRC())
        // Log.Add(
        //     LOG_WARN,
        //     "[world entry] Inspector data checksum mismatch for builder user object %u (client 0x%08x, server 0x%08x). The
        //     builder exception allows entry processing to continue.", static_cast<unsigned>(m_nId),
        //     static_cast<unsigned>(socket->GetInspectorCRC()),
        //     static_cast<unsigned>(g_NewInspectorDB.GetCRC()))
        L2_LOG_WARN(
            Log,
            "[world entry] Inspector data checksum mismatch for builder user object %u (client 0x%08x, server 0x%08x); the "
            "builder exception allows entry processing to continue.",
            static_cast<unsigned>(m_nId), static_cast<unsigned>(socket->GetInspectorCRC()),
            static_cast<unsigned>(g_NewInspectorDB.GetCRC())
        );
    }

    std::vector<int> inspectorResults;
    if (!m_pInspectors.empty() && g_nBotInspectorTestCount > 0) {
        inspectorResults.reserve(static_cast<std::size_t>(g_nBotInspectorTestCount));
        for (int index = 0; index < g_nBotInspectorTestCount; ++index) {
            int result = 0;
            cursor = ::Disassemble(cursor, "d", &result);
            inspectorResults.push_back(result);
        }
    }

    if (!m_pAgent->ResponseFirst(&cursor)) {
        m_bKickByAgent = true;
        return false;
    }

    int response = 0;
    cursor = ::Disassemble(cursor, "d", &response);
    (void)cursor;
    (void)response;

    const std::uint32_t currentTick = runtime::GetTickCount();
    const std::uint32_t elapsed = currentTick - socket->HackInfo.SendListTick;
    if (m_data->m_nWorldId == g_nWorldId && !socket->IsUnderReloading() && elapsed < 1000U) {
        const std::int32_t checkCount = runtime::raw_atomic::IncrementI32(&socket->HackInfo.EnterWorldHackCheckCount);
        User* const checkedUser = socket->GetUser();
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Bot checking, type[1], [%s][%s][%d]",
        //     socketAccountName,
        //     checkedUser != nullptr ? checkedUser->GetName() : L"",
        //     checkCount)
        // Log.Add(
        //     LOG_WARN,
        //     "[world entry] Early request for user object %u on socket %u: %u ms since the character-load timestamp
        //     (threshold 1000 ms), timing-check count %d, processing continues.", static_cast<unsigned>(m_nId),
        //     static_cast<unsigned>(socket->m_nId),
        //     static_cast<unsigned>(elapsed),
        //     static_cast<int>(checkCount))
        L2_LOG_WARN(
            Log,
            "[world entry] Early request for user object %u on socket %u: %u ms since the character-load timestamp "
            "(threshold 1000 ms), timing-check count %d; processing continues.",
            static_cast<unsigned>(m_nId), static_cast<unsigned>(socket->m_nId), static_cast<unsigned>(elapsed),
            static_cast<int>(checkCount)
        );
        // Noise: The timing observation is reported above. A fast request alone does not establish use of a prohibited
        // client. The original audit record remains active. Log.Add(
        //     LOG_ERROR,
        //     L"This account and character is using abnormal or illegal "
        //     L"client program, [%s][%s]",
        //     socketAccountName,
        //     checkedUser != nullptr ? checkedUser->GetName() : L"")

        CLogFormatter audit;
        if (checkedUser == nullptr) {
            audit.WriteAuditLog(
                0x585, 0, 0, 0, 0, 0, 0, 0, u"", u"", u"", 1, checkCount, socket->HackInfo.SendListTick, currentTick,
                elapsed, 0, 0, 0, 0, 0, u"null name", u"null account", u"", u"", 0
            );
        } else {
            const FVector checkedPosition = checkedUser->GetPos();
            const std::u16string ipAddress = checkedUser->GetSocket()->GetIPAddressString();
            const std::u16string ipAddress16 = std::u16string(ipAddress.c_str());
            const std::u16string checkedName16 = std::u16string(checkedUser->GetName());
            audit.WriteAuditLog(
                0x585, checkedUser->GetDBID(), checkedUser->GetAccountID(), 0, 0,
                runtime::Win64CvttDoubleToInt32(checkedPosition.x), runtime::Win64CvttDoubleToInt32(checkedPosition.y),
                runtime::Win64CvttDoubleToInt32(checkedPosition.z), ipAddress16.c_str(), u"", u"", 1, checkCount,
                socket->HackInfo.SendListTick, currentTick, elapsed, 0, 0, 0, 0, checkedUser->Level(), checkedName16.c_str(),
                checkedUser->m_sAccountName.data(), u"", u"", 0
            );
        }
    }

    if (!socket->IsUnderReloading()) {
        CSPointer<CUserSocket> existing = g_UserSocketMapEnterChar.FindObject(static_cast<int>(socket->GetAccountID()));
        if (existing) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Duplicate character? !!! close socket. account id[%d]. "
            //     L"exists[%d] entering[%d]",
            //     socket->GetAccountID(),
            //     existing->m_nId,
            //     socket->m_nId)
            // Log.Add(
            //     LOG_WARN,
            //     "[world entry] Rejecting entry for account %u: socket %u is already registered for character entry.
            //     requesting closure of entering socket %u.", static_cast<unsigned>(socket->GetAccountID()),
            //     static_cast<unsigned>(existing->m_nId),
            //     static_cast<unsigned>(socket->m_nId))
            L2_LOG_WARN(
                Log,
                "[world entry] Rejecting entry for account %u: socket %u is already registered for character entry; "
                "requesting closure of entering socket %u.",
                static_cast<unsigned>(socket->GetAccountID()), static_cast<unsigned>(existing->m_nId),
                static_cast<unsigned>(socket->m_nId)
            );
            return true;
        }
        g_UserSocketMapEnterChar.Insert(
            static_cast<int>(socket->GetAccountID()), socket, const_cast<char*>("user_world.cpp"), 0x147f
        );
    }

    const std::int32_t priorPackageJob = runtime::raw_atomic::ExchangeI32(&m_lPackageItemPacketJobFlag, 1);
    if (priorPackageJob == 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] UsePackageItemPacketWait. ACCOUNT(id[%d]/name[%s]) "
        //     L"// CHAR[%s] // ItemCount(%d/%d)",
        //     L"user_world.cpp",
        //     0x1485,
        //     GetAccountID(),
        //     userAccountName,
        //     GetName(),
        //     m_lCurrentReceivedPackageItemCount,
        //     m_lTotalPackageItemCount)
        // Log.Add(
        //     LOG_WARN,
        //     "[world entry] Ignoring another entry request for account %u, user object %u: package-item loading is already
        //     pending (%d of %d items received), no new item load is requested.", static_cast<unsigned>(GetAccountID()),
        //     static_cast<unsigned>(m_nId),
        //     static_cast<int>(m_lCurrentReceivedPackageItemCount),
        //     static_cast<int>(m_lTotalPackageItemCount))
        L2_LOG_WARN(
            Log,
            "[world entry] Ignoring another entry request for account %u, user object %u: package-item loading is already "
            "pending (%d of %d items received); no new item load is requested.",
            static_cast<unsigned>(GetAccountID()), static_cast<unsigned>(m_nId),
            static_cast<int>(m_lCurrentReceivedPackageItemCount), static_cast<int>(m_lTotalPackageItemCount)
        );
        return false;
    }

    const bool forceReload = m_data->m_nWorldId != g_nWorldId || socket->IsUnderReloading();
    db.RequestLoadItems(this, forceReload);
    db.LoadPeriodicHennaInfo(this);

    if (CUserSocket* const userSocket = GetSocket()) {
        userSocket->Send(
            "chdddhd", 0xfe, 0x17c, g_nWorldId, m_data->m_nWorldId,
            g_stChannelChattingInfo.m_stChannelChattingClientAddress.s_addr,
            g_stChannelChattingInfo.m_hChannelChattingPortForClient, 0
        );
        userSocket->Send("chddd", 0xfe, 0x17d, 1, m_data->m_nPledgeID, m_data->m_nWorldId);
        // Current shipping L2Server 0x00bc41d9-0x00bc4205 appends the current
        // time DWORD. The complete loaded NA 5.0.0.35 Engine.dll independently
        // proves the same payload: S_EX_ENTER_WORLD@0x2035c720 reads format
        // descriptor "d" at 0x20729b48. The former opcode-only P152 branch was
        // an Etina's Fate donor projection and left this exact client's loading
        // gate waiting for its required value.
        userSocket->Send("chd", 0xfe, 0x1c7, runtime::crt::Time32Now());
    }
    return false;
}

bool User::OnRestartPointPacket(RestartPoint restartPoint, int itemId) {
    // Current L2Server 0x00bc938c.
    L2_GUARD_FUNCTION();
    if (static_cast<std::uint32_t>(restartPoint) > static_cast<std::uint32_t>(RESTART_BY_NPC)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]User [%s] Requested Unknown Restart point[%d].",
        //     "user_world.cpp",
        //     0x19a6,
        //     GetName(),
        //     static_cast<int>(restartPoint))
        // Log.Add(
        //     LOG_WARN,
        //     "[restart] Rejecting request for user object %u: restart type %d is outside 0..%d. Requesting socket
        //     closure.", static_cast<unsigned>(m_nId), static_cast<int>(restartPoint), static_cast<int>(RESTART_BY_NPC))
        L2_LOG_WARN(
            Log,
            "[restart] Rejecting request for user object %u: restart type %d is outside 0..%d; requesting socket closure.",
            static_cast<unsigned>(m_nId), static_cast<int>(restartPoint), static_cast<int>(RESTART_BY_NPC)
        );
        return true;
    }

    if (m_data->m_bIsAlive != 0) {
        LogAbnormalAction(AUA_TRY_TO_REVIVE_BUT_ALIVE, false, 0, 0, 0, 0);
        return false;
    }

    if (restartPoint == RESTART_BY_NPC && itemId > 0) {
        int remainingMilliseconds = 1;
        CObject* const object = g_ObjectDB.GetObjectW(itemId, false);
        CItem* const item = object != nullptr && object->IsItem() ? object->CastItem() : nullptr;
        if (item != nullptr && item->m_const != nullptr) {
            if (item->m_const->m_nReuseDelay == 0) {
                remainingMilliseconds = 0;
            } else if (item->m_const->m_nDelayShareGroup < 1) {
                std::uint32_t reuseEndTick = 0;
                if (GetItemReuseDelay(itemId, &reuseEndTick)) {
                    remainingMilliseconds = runtime::SubtractInt32Wrapping(
                        static_cast<std::int32_t>(reuseEndTick), static_cast<std::int32_t>(runtime::GetTickCount())
                    );
                }
            } else {
                remainingMilliseconds =
                    runtime::MultiplyInt32Wrapping(GetReuseRemainTime(item->m_const->m_nDelayShareGroup), 1000);
            }
        }

        if (remainingMilliseconds > 0) {
            SendRemainTime(remainingMilliseconds / 1000, EST_ITEMID, itemId, 0, 0, 0);
            SendDiePacket(true);
            ValidateChanged(VB_MAX);
            CheckAbnormalVisualEffect();
            return false;
        }
    }

    if (IsRidingWyvern()) {
        TryDismounting(true, false);
    }
    Restart(restartPoint, itemId);
    return false;
}

int TalkChannelCache::ResetCache(std::uint32_t userServerId) {
    L2_GUARD_FUNCTION();
    int removed = 0;
    for (SubChannel& channel : mChannels) {
        if (channel.ResetCache(userServerId)) {
            ++removed;
        }
    }
    return removed;
}

int User::ComputeChattingRelation(User* talker, User* listener) {
    L2_GUARD_FUNCTION();

    if (talker == nullptr || listener == nullptr || talker->Id() == listener->Id()) {
        // return 0.
        return ChattingRelation::CRT_NONE;
    }

    // std::uint32_t relation = 0
    std::uint32_t relation = ChattingRelation::CRT_NONE;
    if (talker->m_data->m_nWorldId == listener->m_data->m_nWorldId) {
        CSPointer<CFriends> friends = listener->GetFriends();
        if (friends && friends->HasFriend(talker->GetDBID())) {
            // relation |= 0x01U.
            relation |= static_cast<std::uint32_t>(ChattingRelation::CRT_FRIEND);
        }
        if (listener->m_data->m_nPledgeID != 0 && listener->m_data->m_nPledgeID == talker->m_data->m_nPledgeID) {
            // relation |= 0x02U.
            relation |= static_cast<std::uint32_t>(ChattingRelation::CRT_PLEDGE);
        }
        if (listener->m_data->m_nAllianceId != 0 && listener->m_data->m_nAllianceId == talker->m_data->m_nAllianceId) {
            // relation |= 0x08U.
            relation |= static_cast<std::uint32_t>(ChattingRelation::CRT_ALLIANCE);
        }
        if (listener->IsMyMentee(talker) || listener->IsMyMentor(talker)) {
            // relation |= 0x04U.
            relation |= static_cast<std::uint32_t>(ChattingRelation::CRT_MENTORING);
        }
    }
    if (talker->IsBuilder()) {
        // relation |= 0x10U.
        relation |= static_cast<std::uint32_t>(ChattingRelation::CRT_GM);
    }
    return static_cast<int>(relation);
}

HearResult User::Hear(User* talker, const char16_t* talkerName, const char16_t* message, int type) {
    L2_GUARD_FUNCTION();

    if (talker == nullptr) {
        const char16_t* const packetTalkerName = talkerName;
        const char16_t* const packetMessage = message;
        switch (type) {
            case 2:
                GetSocket()->Send("cddSdScc", 0x4a, 0, 2, packetTalkerName, -1, packetMessage, 0, 0);
                break;
            case 4:
                SayPledge(CSPointer<User>(this, ObjectRefType::FunctionChain), 0, packetTalkerName, packetMessage, 0);
                break;
            case 9:
                SayAlliance(CSPointer<User>(this, ObjectRefType::FunctionChain), 0, packetTalkerName, packetMessage, 0);
                break;
            case 0x1a:
                SayPledgeInRaid(CSPointer<User>(this, ObjectRefType::FunctionChain), 0, packetTalkerName, packetMessage);
                break;
            case 0x1b:
                SayAllianceInRaid(CSPointer<User>(this, ObjectRefType::FunctionChain), 0, packetTalkerName, packetMessage);
                break;
            default:
                GetSocket()->Send("cddSdS", 0x4a, 0, type, packetTalkerName, -1, packetMessage);
                break;
        }
    } else if (!m_bBlockAll && !IsInBlockList(*talker)) {
        const char16_t* const packetTalkerName = talkerName;
        const char16_t* const packetMessage = message;
        switch (type) {
            case 2: {
                const int relation = ComputeChattingRelation(talker, this);
                if ((relation & 0x10) == 0) {
                    CUserSocket* const socket = GetSocket();
                    const unsigned char talkerLevel = static_cast<unsigned char>(talker->Level());
                    const int talkerId = static_cast<int>(talker->GetServerId());
                    socket->Send(
                        "cddSdScc", 0x4a, talkerId, 2, packetTalkerName, -1, packetMessage,
                        static_cast<unsigned char>(relation), talkerLevel
                    );
                } else {
                    CUserSocket* const socket = GetSocket();
                    const int talkerId = static_cast<int>(talker->GetServerId());
                    socket->Send(
                        "cddSdSc", 0x4a, talkerId, 2, packetTalkerName, -1, packetMessage,
                        static_cast<unsigned char>(relation)
                    );
                }
                break;
            }
            case 4: {
                CSPointer<User> receiver(this, ObjectRefType::FunctionChain);
                const int talkerId = static_cast<int>(talker->GetServerId());
                SayPledge(std::move(receiver), talkerId, packetTalkerName, packetMessage, 0);
                break;
            }
            case 9: {
                CSPointer<User> receiver(this, ObjectRefType::FunctionChain);
                const int talkerId = static_cast<int>(talker->GetServerId());
                SayAlliance(std::move(receiver), talkerId, packetTalkerName, packetMessage, 0);
                break;
            }
            case 0x1a:
                SayPledgeInRaid(CSPointer<User>(this, ObjectRefType::FunctionChain), 0, packetTalkerName, packetMessage);
                break;
            case 0x1b:
                SayAllianceInRaid(CSPointer<User>(this, ObjectRefType::FunctionChain), 0, packetTalkerName, packetMessage);
                break;
            default: {
                CUserSocket* const socket = GetSocket();
                const int talkerId = static_cast<int>(talker->GetServerId());
                socket->Send("cddSdS", 0x4a, talkerId, type, packetTalkerName, -1, packetMessage);
                break;
            }
        }
    }

    if (m_nSnoopGmId > 0) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nSnoopGmId));
        User* const snoopGm = creature != nullptr && creature->IsPC() ? creature->GetUserOrMaster() : nullptr;
        if (snoopGm == nullptr || talker == nullptr) {
            SetSnoop(0);
        } else {
            CUserSocket* const snoopSocket = snoopGm->GetSocket();
            const int talkerId = static_cast<int>(talker->GetServerId());
            const std::u16string listenerName = std::u16string(GetName());
            const char16_t* const packetTalkerName = talkerName;
            const char16_t* const packetMessage = message;
            snoopSocket->Send(
                "cdSddSS", 0xdb, static_cast<int>(Id()), listenerName.c_str(), talkerId, type, packetTalkerName,
                packetMessage
            );
        }
    }
    return HEAR_OK;
}

HearResult User::Hear(
    const int serverId,
    const int characterDatabaseId,
    const char16_t* const characterName,
    const char16_t* const message,
    const int type
) {
    // Current L2Server 0x00bbefbc. Relay-originated non-raid chat carries a
    // zero talker server id. Only the two raid types keep serverId.
    L2_GUARD_FUNCTION();
    if (!m_bBlockAll && !IsInBlockList(characterDatabaseId)) {
        const char16_t* const packetCharacterName = characterName;
        const char16_t* const packetMessage = message;
        switch (type) {
            case 4:
                SayPledge(CSPointer<User>(this, ObjectRefType::FunctionChain), 0, packetCharacterName, packetMessage, 0);
                break;
            case 9:
                SayAlliance(CSPointer<User>(this, ObjectRefType::FunctionChain), 0, packetCharacterName, packetMessage, 0);
                break;
            case 0x1a:
                SayPledgeInRaid(
                    CSPointer<User>(this, ObjectRefType::FunctionChain), serverId, packetCharacterName, packetMessage
                );
                break;
            case 0x1b:
                SayAllianceInRaid(
                    CSPointer<User>(this, ObjectRefType::FunctionChain), serverId, packetCharacterName, packetMessage
                );
                break;
            default:
                GetSocket()->Send("cddSdS", 0x4a, 0, type, packetCharacterName, -1, packetMessage);
                break;
        }
    }
    return HEAR_OK;
}

HearResult User::HearMobile(
    const int serverId,
    const int characterDatabaseId,
    const char16_t* const characterName,
    const char16_t* const message,
    const SayPacketType type
) {
    // Current L2Server 0x00bbf1d8. Mobile chat skips the
    // ordinary snoop path and publishes relation byte 0x40.
    L2_GUARD_FUNCTION();
    if (!m_bBlockAll && !IsInBlockList(characterDatabaseId)) {
        const char16_t* const packetCharacterName = characterName;
        const char16_t* const packetMessage = message;
        if (type == SPT_PLEDGE) {
            SayPledge(
                CSPointer<User>(this, ObjectRefType::FunctionChain), serverId, packetCharacterName, packetMessage, 0x40
            );
        } else if (type == SPT_ALLIANCE) {
            SayAlliance(
                CSPointer<User>(this, ObjectRefType::FunctionChain), serverId, packetCharacterName, packetMessage, 0x40
            );
        }
    }
    return HEAR_OK;
}

void User::PetitionSayBySystem(const char16_t* const message) {
    // Current L2Server 0x00bc31b4.
    L2_GUARD_FUNCTION();
    char16_t emptyTalkerName[256];
    FormatUtf16To(emptyTalkerName, std::size(emptyTalkerName), u"");
    Hear(this, emptyTalkerName, message, 7);
}

void User::EnterWorld() {
    // Current L2Server.exe 0x00bcd034.
    L2_GUARD_FUNCTION();

    CUserSocket* const gateSocket = GetSocket();
    if (gateSocket == nullptr || (!gateSocket->IsUnderReloading() && !m_pAgent->QuerySecond())) {
        return;
    }

    g_nSkillActorDBID[CThreadManager::GetThreadID()] = static_cast<int>(GetDBID());

    const std::u16string lowerOriginName = LowercaseGameName(GetName());

    g_UserMap.Insert(static_cast<int>(GetServerId()), this, const_cast<char*>("user_world.cpp"), 0x724);
    g_UserDBIDMap.Insert(
        std::CDualKey<int, int>(m_data->m_nWorldId, static_cast<int>(GetDBID())), this, const_cast<char*>("user_world.cpp"),
        0x725
    );
    UserUtil::RegisterToRandomUser(static_cast<int>(Id()));
    if (IsBuilder()) {
        g_GMCharMap.Insert(static_cast<int>(Id()), this, const_cast<char*>("user_world.cpp"), 0x72d);
    }
    g_UserAccountDBIDMap.Insert(static_cast<int>(GetAccountID()), this, const_cast<char*>("user_world.cpp"), 0x730);
    g_UserCharNameMap.Insert(m_data->m_nWorldId, lowerOriginName, this);

    if (m_data->m_nWorldId == g_nWorldId) {
        g_NewUserCharNameMap.Insert(lowerOriginName, this);
    } else {
        std::array<char16_t, 30> raidName{};
        FormatUtf16To(raidName.data(), raidName.size(), u"%s_%d", GetOriginName(), m_data->m_nWorldId);
        SetRaidUserName(raidName.data());
        g_NewUserCharNameMap.Insert(LowerUserNameKey(raidName.data()), this);
    }

    CCreature::EnterWorldImpl();
    m_pRidingInfo->OnEnterWorld();
    ValidateHenna();
    m_vBotList.clear();

    m_nRelationSlot = CRelationManager::inst().GetEmptySlot();
    if (m_nRelationSlot == -1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d],[PledgeWar], User Relation slot error, %s(%u)",
        //     L"user_world.cpp",
        //     0x75a,
        //     GetName(),
        //     Id())
        // Log.Add(
        //     LOG_ERROR,
        //     "[world entry] No relation slot was allocated for user object %u. World entry continues with relation slot
        //     -1.", static_cast<unsigned>(Id()))
        L2_LOG_ERROR(
            Log,
            "[world entry] No relation slot was allocated for user object %u; world entry continues with relation slot -1.",
            static_cast<unsigned>(Id())
        );
    }
    m_nNpcCountInSight = 0;
    m_nUserCountInSight = 0;
    m_bIsNonChaoInNow = false;
    m_nTimeToDie = 0;

    if (g_bInitedNCGuard) {
        // const NCGuardServerResult result =
        //     CreateNCGuardUserInfo(GetDBID())
        const std::uint32_t ncGuardCharacterId = GetDBID();
        const NCGuardServerResult result = CreateNCGuardUserInfo(ncGuardCharacterId);
        if (result != 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"create NCGuard user fail[%d]",
            //     result)
            // Log.Add(
            //     LOG_ERROR,
            //     "[NCGuard] Cannot create user state for character DB ID %u: error 0x%08x. World entry continues.",
            //     static_cast<unsigned>(ncGuardCharacterId),
            //     static_cast<unsigned>(result))
            L2_LOG_ERROR(
                Log, "[NCGuard] Cannot create user state for character DB ID %u: error 0x%08x; world entry continues.",
                static_cast<unsigned>(ncGuardCharacterId), static_cast<unsigned>(result)
            );
        }
    }

    if (m_data->m_nBuilder < 1 || m_data->m_nBuilder > 7) {
        // Current L2Server User::EnterWorld@0x00bcd034 reads this+0x2b6d,
        // which is CCreatureAreaMod::m_bIsInTrainingZone at +0x4d. Using
        // m_bDecExp (+0x2b10) incorrectly put every ordinary user into the
        // training state and forced the right-hand appearance to class 0x87.
        if (m_AreaMod.m_bIsInTrainingZone) {
            m_data->m_TrainingState = 1;
        }
    } else {
        m_data->m_bGMInvisible = true;
        SendSystemMessage(u"SYS", u"hide is default for builder.");
        m_bFriendAddEnabled = false;
        SendSystemMessage(u"SYS", u"FriendAddOff is default for builder.");
        m_bCanHearWhisper = false;
        SendSystemMessage(u"SYS", u"whisperoff is default for builder.");
    }

    db.RequestLoadTraniningZone(this);
    m_pEconomy = nullptr;
    m_pObserverRemains = nullptr;
    m_bIsObserving = false;
    if (m_data->m_dHP <= 0.0) {
        m_data->SetAlive(false);
    }

    if (!WOlympiadServiceConfig::inst().IsHost()) {
        NoblessSystem::inst().LinkNobless(CSPointer<User>(this, ObjectRefType::FunctionChain));
    } else {
        WOlympiadServiceUtil::LinkNobless(CSPointer<User>(this, ObjectRefType::FunctionChain));
    }

    if (m_data->m_nWorldId == g_nWorldId) {
        if (m_data->m_nNoblessType == 1 || m_data->m_nNoblessType == 2) {
            NoblessSystemUtil::SetNoblessSkillToUser(CSPointer<User>(this, ObjectRefType::FunctionChain));
        }
        if (!IsHero()) {
            ImpoundHeroWeapon();
        }
        if (!IsHeroEntry(true)) {
            ImpoundHeroEntryItem(true);
        }
        if (!IsHeroEntry(false)) {
            ImpoundHeroEntryItem(false);
        }
    } else {
        if (NoblessSystemUtil::SetNoblessSkillToUser(CSPointer<User>(this, ObjectRefType::FunctionChain))) {
            m_data->SetNoblessType(1);
        }
        BalthusEvent::inst().SendEventUIOff(CSPointer<User>(this, ObjectRefType::FunctionChain));
    }

    ValidateCastleStateInfo();
    ValidateHeroSkills();
    ValidateAbilitySkills();
    ValidateSetItem(static_cast<SlotType>(0x3f));
    ValidateNewSetItem();
    ValidateShapeShiftedSetItem();
    ValidateChanged(VB_MAX);
    g_DeferredJob.DoEnterWorldJob(this);

    const std::uint32_t pledgeId = m_data->m_nPledgeID;
    if (pledgeId == 0) {
        m_data->m_nPledgeType = 0;
    } else {
        CSPointer<CPledge> pledge = CPledge::FindPledge(m_data->m_nWorldId, static_cast<int>(pledgeId));
        if (!pledge) {
            db.RequestLoadPledge(m_data->m_nWorldId, pledgeId, false);
        } else {
            SetPledge(pledge.get());
            if (IsPledgeMaster()) {
                pledge->SetMaster(this);
                if (pledge->HasSubPledgeType(ACADEMY)) {
                    pledge->AcquireSkillForAcademy(GetDBID(), false);
                }
            }
            pledge->AddDbMember(this);
            pledge->AddMember(this);
            pledge->Online(this);
            pledge->OpenAllWindowPacket(this, false, nullptr);
            pledge->SendPledgeSkillsTo(this);
            pledge->ApplyPledgeSkillsTo(this, false, false);

            SystemMessage_S<SameMemberCheck, User> joined(
                SystemMessageId::CLAN_MEMBER_LOGGED_IN, static_cast<eSMsgType>(0x0c), GetName(), this
            );
            pledge->ServeForAllMember(joined, true);
            pledge->SendPledgeOnlineMemberCount();

            if (!IsPledgeMaster() && pledge->HasAcademyStudent(GetDBID())) {
                pledge->AcquireSkillForAcademy(GetDBID(), false);
            }
        }

        tPledgeBonusTimeStamp[PledgeBonus::ACCESS_MISSION_COMPLETE] = runtime::crt::Time32Now();
        db.RequestLoadUserPledgeBonus(this);
    }

    ValidateChanged(VB_MAX);
    if (!m_bIsDBFactionInfoLoaded) {
        db.RequestLoadFactionPoint(this);
    }
    if (!WOlympiadServiceConfig::inst().IsHost() && m_data->m_nWorldId == g_nWorldId) {
        m_monsterBook.OnUserEnterWorld(*this);
    }

    g_DeferredJob.ReserveNextDoer(this);
    m_inv.ValidateCarringWeight();
    CWorld::Inst().EnterWorld(this, GetPos(), 0, true, false);

    if (m_data->m_nDominionSiegeId != 0) {
        SendDominionStartEnd(true, nullptr);
    }
    m_nTradeModeTimeStamp = 0;
    // m_cLectureMark = 0.
    m_cLectureMark = BR_LECTURE_MARK_UNDEFINE;
    if (g_bUseLecture) {
        db.RequestGetLectureMark(this);
    }

    g_PetManager.CreatePet(this, nullptr);
    SendSystemMessage(SystemMessageId::WELCOME_TO_LINEAGE_II);
    if (g_nChinaFatigueSystemType == 1) {
        SendSystemMessage(SystemMessageId::ADULT_SERVER_ENTRY_NOTICE);
    } else if (g_nChinaFatigueSystemType == 2) {
        SendSystemMessage(SystemMessageId::ENTERED_LIVE_SERVER);
    } else if (g_nChinaFatigueSystemType == 3) {
        SendSystemMessage(SystemMessageId::ENTERED_JUVENILE_SERVER);
    }
    db.RequestGetLoginAnnounce(*this);

    CUserSocket* const raidSocket = GetSocket();
    if (!raidSocket->IsUnderReloading() && m_data->m_nWorldId == g_nWorldId) {
        db.ReqLoginCharacter(*this);
        m_nUseTimeStamp = 0;
        CIOObject::SetTimeStamp(&m_nUseTimeStamp);
        m_nUseTimeStampForToday = 0;
        CIOObject::SetTimeStamp(&m_nUseTimeStampForToday);
    }
    db.RequestGetPunish(*this);

    const bool wasPlaying = m_bPlaying;
    {
        L2_GUARD(*m_lock);
        if (!m_bPlaying) {
            m_bPlaying = true;
        }
    }
    if (!wasPlaying) {
        const int playingUserCount = runtime::raw_atomic::IncrementI32(&g_nPlayingUserCount);
        SharedEventInfo::Inst()->m_gameEvent.m_PlayingUserAverage = playingUserCount;
        CEventManager::inst().UpdateDropTick(runtime::raw_atomic::LoadI32Relaxed(&g_nPlayingUserCount));
    }

    User::IsNewChar(static_cast<int>(GetDBID()));
    db.RequestLoadFriendList(this);
    if (m_data->m_bIsAlive == 0) {
        SendDiePacket(false);
    }
    DeleteYongmaAbnormalStatus(false);
    CheckAbnormalVisualEffect();
    CIOObject::SetShortTimeStamp(&m_nLastTimeCheck, 0);
    GetSocket()->SetSocketCharacterUseTime();
    m_nPetitionId = petitionCli.GetMyPetition(static_cast<int>(GetDBID()));

    db.LoadRecipeBookChapter(this);
    db.RequestNewbieCheck(this);
    db.RequestUreadMailCount(this);
    SendETCBuffStatusInfo();
    SetMaxMagicLevel();
    std::fill(std::begin(m_nAutoSoulshotFlags), std::end(m_nAutoSoulshotFlags), 0U);
    LevelDownSkillWhenLevelIsTooHigh();

    GetSocket()->Send("cddd", 0x40, static_cast<int>(Id()), static_cast<int>(GetDBID()), 0xb2586);
    SendStorageMaxCount();
    SendPledgeAnnounce();

    if (CEvent* const event = CEventManager::inst().GetEvent(GAME_EVENT::ET_WORLD)) {
        const CEvent_World::EventStateInfo* const info = static_cast<CEvent_World*>(event)->GetEventStateInfo();
        if (info != nullptr) {
            GetSocket()->Send(
                "chdddddddSS", 0xfe, 0xbd, info->eventId, static_cast<int>(info->state), info->param[0], info->param[1],
                info->param[2], info->param[3], info->param[4], info->paramString1.data(), info->paramString2.data()
            );
        }
    }
    if (CEvent* const event = CEventManager::inst().GetEvent(GAME_EVENT::ET_BUFF)) {
        CEvent_Buff* const buffEvent = static_cast<CEvent_Buff*>(event);
        const CEvent_Buff::BuffEventInfo* const info = buffEvent->GetBuffEventInfo();
        if (info != nullptr && info->broadcast != 0) {
            const TimePeriodInfo period = buffEvent->GetTimePeriodInfo();
            GetSocket()->Send(
                "chdddd", 0xfe, 0xdc, static_cast<int>(info->buffType), info->value, period.m_nEndTime,
                static_cast<int>(info->state)
            );
        }
    }
    CEventManager::inst().SendEnterSystemMsg(this);
    m_dwDropItemTick = runtime::GetTickCount();

    if (CanUsePCCafePoint()) {
        ResetPCCafeUseTime(true);
        LoadPCCafePoint(1);
        LoadPCCafePoint(3);
        m_data->m_bCanUsePCCafePoint = true;
    }
    if (F2PConfig::IsContainPCCafeUserBonus(GetSocket())) {
        m_data->m_bPremiumPCCafeUser = 1;
    }
    GivePremiumItem();
    if (m_data->m_bPCCafeUser) {
        if (g_PCCafeBonusBuff) {
            const CSkillInfo* const skillInfo = g_SkillDB.GetSkillInfo(CSkillKey(0x243f, 1, 0), nullptr);
            if (skillInfo != nullptr) {
                skillInfo->ActivateSkill(this, this, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
            }
        }
        if (CEvent* const event = CEventManager::inst().GetEvent(GAME_EVENT::ET_PCCAFE_SETTING)) {
            const int skillId = static_cast<CEvent_PCCafe*>(event)->GetPCCafeBuffSkillBonus(CEvent_PCCafe::BT_BUFFSKILL);
            if (skillId > 0) {
                const CSkillInfo* const skillInfo = g_SkillDB.GetSkillInfo(CSkillKey(skillId, 1, 0), nullptr);
                if (skillInfo != nullptr) {
                    skillInfo->ActivateSkill(this, this, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
                }
            }
        }
        db.RequestSaveDailyPCCafePoint(this, 0, 0);
    }

    CheckSystemBuff(BUFF_PREMIUM_SPECIAL);
    LoadPoint(5);
    LoadPoint(8);
    KillPVPPointChecker.LoadGiveCount(this);

    db.RequestLoadBossRecord(*this);
    db.RequestLoadSharedReuseDelaysOfItems(*this);
    db.RequestLoadDuelRecord(this);
    db.RequestGetTutorial(*this);
    db.RequestLoadNRMemo(*this);
    db.RequestLoadUserPremiumItems(this);
    db.GetLastBirthdayGiftDate(*this);
    db.RequestLoadBirthInfo(this);

    {
        L2_GUARD(*m_lock);
        m_data->InitDailyQuest();
    }
    db.RequestLoadDailyQuest(*this);
    m_tNextBindTime = runtime::AddInt32Wrapping(m_data->GetDailyBindTime(), 0x15180);
    db.RequestLoad5YearInfo(this);
    db.RequestLoadBotReport(this);
    db.RequestDecayBotReport(this);

    SendGIPCharacterInfo(true);
    LoadMentorMenteeInfo();
    RemoveOwnThing();
    const int voteElapsedDays = CalInitialVoteSystem();
    if (voteElapsedDays != 0) {
        InitVoteParam(voteElapsedDays);
    }
    SendVoteSystemInfoPacket();
    if (m_nAssociatedInstantZoneDBId != 0) {
        g_InstantZoneMgr.LoadInzoneFlag(m_nAssociatedInstantZoneDBId, 0);
    }

    db.RequestLoadPostList(*this);
    db.RequestLoadPetItemCount(*this);
    CUserSocket* const postLoadSocket = GetSocket();
    runtime::raw_atomic::ExchangeI32(&postLoadSocket->m_RaidInfo.m_nIsUnderReloading, 0);
    m_pPostInfo->GetPostFriend().LoadAndSendPostFriend(*this);

    DynamicQuestManager::Inst()->OnUserEnterWorld(*this);
    db.RequestLoadUserTimeToGiveKarmaPointToKiller(this);
    db.RequestGetDailyCount(this, 1);
    db.RequestLoadPetitionExternalNotify(*this);
    g_PledgeGameSystem.GetPledgeGameRecord(static_cast<int>(Id()));
    CEventCampaignManager::Inst()->OnUserEnterWorld(*this);

    if (CEventManager::inst().GetEvent(GAME_EVENT::ET_KALIE) != nullptr) {
        db.RequestLoadKalieEventForUser(*this);
    }
    if (CEventManager::inst().GetEvent(GAME_EVENT::ET_BALTHUS) != nullptr) {
        db.RequestLoadBalthusEventUserInfo(*this);
    }
    if (CEvent* const event = CEventManager::inst().GetEvent(GAME_EVENT::ET_INVITATION);
        event != nullptr && !event->InEventTime()) {
        SendInvitationEventUISetting(static_cast<CEvent_InvitationEvent*>(event)->GetMaxInkEnergy());
        db.RequestGetEventScore(GAME_EVENT::ET_INVITATION, 1, static_cast<int>(GetDBID()), GetName());
    }

    CheckDormantAndGiveGift();
    LoadExDropCount();
    LoadWorldChat();
    GetSocket()->Send("ch", 0xfe, 0x147);
    CBeautyManager::Inst()->SendAllDataList(this);

    if (g_bUseAutoSoulShotUI) {
        if (g_L2ServerType != L2ServerType::WORLDSERVER && !WOlympiadServiceConfig::inst().IsHost()) {
            db.RequestLoadAutoSoulShotUI(this);
        } else {
            std::fill(std::begin(m_nAutoSoulshotFlags), std::end(m_nAutoSoulshotFlags), 0U);
            m_nAutoSoulshotFlag = 0;
            for (int index = 0; index < 4; ++index) {
                AutoSoulShotUi& info = m_AutoSoulShotUi[static_cast<std::size_t>(index)];
                info.m_ItemClassID = 0;
                info.m_IsActiveState = 1;
                GetSocket()->Send("chddd", 0xfe, 0x0c, info.m_ItemClassID, info.m_IsActiveState, index);
            }
        }
    }

    if (CEventManager::inst().GetEvent(GAME_EVENT::ET_LUCKY_GAME) != nullptr) {
        CEvent* const event = CEventManager::inst().GetEvent(GAME_EVENT::ET_LUCKY_GAME);
        CEvent_LuckyGameEvent* const luckyGame = static_cast<CEvent_LuckyGameEvent*>(event);
        db.RequestGetEventScore(
            GAME_EVENT::ET_LUCKY_GAME, luckyGame->m_nCommonIndex, static_cast<int>(GetDBID()), GetName()
        );
        db.RequestGetEventScore(
            GAME_EVENT::ET_LUCKY_GAME, luckyGame->m_nPremiumIndex, static_cast<int>(GetDBID()), GetName()
        );
    }

    ValidateChanged(std::int64_t{VB_MAX});
    SendAdenaInvenCount();

    if (g_bUseVipAttendance) {
        AuthUserDataInfo info;
        if (!authd.GetAuthUserDataInfo(static_cast<int>(GetAccountID()), info)) {
            // Noise: GetAuthUserDataInfo already reports the account and precise missing/not-loaded reason.
            // Log.Add(
            //     LOG_ERROR,
            //     L"FILE[%s] LINE[%d] VipAttendance AuthInfo get fail ToAuth AccountId[%d]",
            //     L"user_world.cpp",
            //     0xa11,
            //     GetAccountID())
        } else {
            CLogFormatter formatter;
            formatter.WriteLog4(
                0x1fce, this, nullptr, u"LogIn", nullptr, nullptr, static_cast<int>(info.eAttendanceType),
                info.nAttendanceDays, info.nPCCafeAttendanceFlag, info.nVIPAttendanceFlag, info.tLastAttendanceCheckTime,
                GetSocket()->GetDormantType(), g_nWorldId, 0, 0, 0, 0
            );

            VipAttendanceSystem::Inst().AddUserAttendanceInfo(GetSocket(), info);
            bool resetDormantAttendance = false;
            if (info.eAttendanceType != NEW_AUTH_USER_DATA::AT_DORMANT && GetSocket()->IsDormantUser()) {
                VipAttendanceSystem::Inst().SendToAuthResetAttendance(
                    GetAccountID(), static_cast<char>(NEW_AUTH_USER_DATA::AT_DORMANT), const_cast<wchar_t*>(L"Login_Dormant")
                );
                resetDormantAttendance = true;
            }
            if (!resetDormantAttendance && Level() >= g_nAttendanceMinimumLev) {
                const int result = VipAttendanceSystem::Inst().GetCheckableVipAttendance(this);
                if (result == 2) {
                    VipAttendanceSystem::Inst().SendVipAttendanceItemList(this);
                } else if (result == 3) {
                    runtime::raw_atomic::ExchangeI32(&GetSocket()->m_VipAttendnaceCheck, 1);
                }
            }
        }
    }

    if (PledgeRaidNetworkUtil::IsHost()) {
        PushSerializedMessage(MakeLambdaMessage([](CCreature* const creature) {
            GuardHelper guard(L"enter_olympiad_after_enter_world", CThreadManager::GetThreadID());
            User* const target = creature->CastUser();
            CSPointer<User> user(target, ObjectRefType::FunctionChain);
            if (!WOlympiadService::inst().AssignConnectedUser(user)) {
                WOlympiadService::inst().ReturnToOrigin(user);
            }
        }));
    } else if (WOlympiadServiceConfig::inst().IsPeer()) {
        CSPointer<User> user(this, ObjectRefType::FunctionChain);
        if (WOlympiadStuff::IsAllowedUser(user)) {
            PushSerializedMessage(MakeLambdaMessage([](CCreature* const creature) {
                creature->PushSerializedMessage(MakeLambdaMessage([](CCreature* const serializedCreature) {
                    User* const target = serializedCreature->CastUser();
                    if (target != nullptr) {
                        WOlympiadServiceProxy::inst().RequestLastMatchResult(
                            CSPointer<User>(target, ObjectRefType::FunctionChain)
                        );
                    }
                }));
            }));
        }
    }

    RelayClient::Inst()->SendLogOnPacket(this);
    g_FieldCycleSystem.OnUserEnterWorld(this);
    db.RequestLoadCardUpdownGameRecord(*this);
    if (GetPledge() != nullptr) {
        PledgeRaidSystem::inst().CheckPledgeRaidInzoneBinding(this);
    }
    PledgeMissionInit();
    if (WOlympiadServiceConfig::inst().IsPeer()) {
        WOlympiadServiceProxy::inst().SendOlympiadInfoTo(CSPointer<User>(this, ObjectRefType::FunctionChain));
    }
    if (m_AreaMod.m_bIsInPeaceField) {
        runtime::raw_atomic::ExchangeI32(&m_nStartPeaceZoneTime, runtime::crt::Time32Now());
    }
}

void User::SaveCharExtraInfo() {
    // Current L2Server 0x00bbc470.
    L2_GUARD_FUNCTION();
    double carryingWeightPercent = 0.0;
    if (m_data->m_dCarryWeight > 0.0) {
        carryingWeightPercent = static_cast<double>(m_inv.GetCarryingWeight()) / m_data->m_dCarryWeight * 100.0;
    }
    db.RequestSaveCharExtraInfo(this, GetSocialClass(), carryingWeightPercent);
}

void User::LogPlayTimeOnLeaveWorld() {
    // Current L2Server 0x00bbe0d0.
    L2_GUARD_FUNCTION();
    if (m_AreaMod.m_bIsInPeaceField) {
        UpdateTotalPeaceZoneTime();
    }
    if (m_nStartCombatTime > 0 && m_data->m_bIsInCombatMode) {
        m_nTotalCombatTime =
            runtime::AddInt32Wrapping(m_nTotalCombatTime, runtime::crt::Time32Now() - m_nStartCombatTime);
    }

    std::array<char16_t, 20> addressText{};
    if (CUserSocket* const socket = GetSocket()) {
        const std::uint32_t address = socket->GetAddress().s_addr;
        FormatUtf16To(
            addressText.data(), 19, u"%d.%d.%d.%d", static_cast<int>(address & 0xffU),
            static_cast<int>((address >> 8U) & 0xffU), static_cast<int>((address >> 16U) & 0xffU),
            static_cast<int>((address >> 24U) & 0xffU)
        );
    }

    const FVector position = GetPos();
    const int onlineSeconds = m_nUseTimeStamp < 1 ? 0 : runtime::crt::Time32Now() - m_nUseTimeStamp;
#if defined(_WIN32)
    const wchar_t* const accountName = reinterpret_cast<const wchar_t*>(m_sAccountName.data());
#else
    std::array<wchar_t, 16> accountNameBuffer{};
    const wchar_t* const accountName = CopyUtf16ToWide(m_sAccountName.data(), m_sAccountName.size(), accountNameBuffer);
#endif
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,%s,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,"
    //     L"%s,%s,,,",
    //     0x37b,
    //     GetDBID(),
    //     GetAccountID(),
    //     runtime::Win64CvttDoubleToInt32(position.x),
    //     runtime::Win64CvttDoubleToInt32(position.y),
    //     runtime::Win64CvttDoubleToInt32(position.z),
    //     addressText.data(),
    //     m_data->m_Race,
    //     m_data->m_Sex,
    //     m_data->m_nClass,
    //     m_data->m_nLevel,
    //     onlineSeconds,
    //     m_nTotalPeaceZoneTime,
    //     m_nTotalPrivateStoreSecond,
    //     m_nTotalCombatTime,
    //     GetName(),
    //     accountName)
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,%s,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,"
    //     L"%s,%s,,,",
    //     static_cast<long long>(0x37b),
    //     static_cast<long long>(GetDBID()),
    //     static_cast<long long>(GetAccountID()),
    //     static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.x)),
    //     static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.y)),
    //     static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.z)),
    //     addressText.data(),
    //     static_cast<long long>(m_data->m_Race),
    //     static_cast<long long>(m_data->m_Sex),
    //     static_cast<long long>(m_data->m_nClass),
    //     static_cast<long long>(m_data->m_nLevel),
    //     static_cast<long long>(onlineSeconds),
    //     static_cast<long long>(m_nTotalPeaceZoneTime),
    //     static_cast<long long>(m_nTotalPrivateStoreSecond),
    //     static_cast<long long>(m_nTotalCombatTime),
    //     GetName(),
    //     accountName)
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,%s,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,"
        u"%s,%s,,,",
        static_cast<long long>(0x37b), static_cast<long long>(GetDBID()), static_cast<long long>(GetAccountID()),
        static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.x)),
        static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.y)),
        static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.z)), addressText.data(),
        static_cast<long long>(m_data->m_Race), static_cast<long long>(m_data->m_Sex),
        static_cast<long long>(m_data->m_nClass), static_cast<long long>(m_data->m_nLevel),
        static_cast<long long>(onlineSeconds), static_cast<long long>(m_nTotalPeaceZoneTime),
        static_cast<long long>(m_nTotalPrivateStoreSecond), static_cast<long long>(m_nTotalCombatTime), GetName(),
        accountName
    );
}

void User::LogEquippedItemOnLeaveWorld() {
    // Current L2Server.exe 0x00bc1034. Hold every selected item through all
    // seven records: the target takes the same 45 references first and only
    // releases them after the final artifact-group record.
    L2_GUARD_FUNCTION();

    constexpr std::array<int, 45> slots{
        10,
        6,
        7,
        8,
        9,
        11,
        12,
        14,
        33,
        13,
        34,
        35,
        36,
        37,
        38,
        39,
        40,
        21,
        22,
        23,
        24,
        25,
        26,
        41,
        42,
        43,
        44,
        45,
        46,
        47,
        48,
        49,
        50,
        51,
        52,
        53,
        54,
        55,
        56,
        57,
        58,
        59,
        60,
        61,
        62,
    };
    std::array<CSPointer<CItem>, slots.size()> heldItems{};
    std::array<int, 63> classIds{};
    for (std::size_t index = 0; index < slots.size(); ++index) {
        const int slot = slots[index];
        heldItems[index] = CItemFactory::Inst().FindObjectSP(m_data->m_nSlotIndex[slot]);
        if (heldItems[index]) {
            classIds[slot] = heldItems[index]->m_nClassId;
        }
    }

    std::array<char16_t, 256> weaponName{};
    std::array<char16_t, 256> weaponDetail{};
    CSharedItemData* const weaponData = GetEquipedWeapon();
    CItem* const weapon = weaponData != nullptr ? CItemFactory::Inst().GetObjectW(weaponData->index) : nullptr;
    if (weapon != nullptr) {
        weapon->m_itemBaseAttribute.GetLogString(weaponName.data());
        FormatUtf16To(
            weaponDetail.data(), weaponDetail.size(), u"%d:%d:%d", weapon->m_itemOption.intensiveItemClassID,
            weapon->m_itemOption.itemOptionKey.variationOption1, weapon->m_itemOption.itemOptionKey.variationOption2
        );
    }

    const FVector position = GetPos();
    const int positionX = runtime::Win64CvttDoubleToInt32(position.x);
    const int positionY = runtime::Win64CvttDoubleToInt32(position.y);
    const int positionZ = runtime::Win64CvttDoubleToInt32(position.z);
#if defined(_WIN32)
    const wchar_t* const accountName = reinterpret_cast<const wchar_t*>(m_sAccountName.data());
#else
    std::array<wchar_t, 16> accountNameBuffer{};
    const wchar_t* const accountName = CopyUtf16ToWide(m_sAccountName.data(), m_sAccountName.size(), accountNameBuffer);
#endif

    const int leftHand = classIds[14] == 0 ? classIds[7] : 0;
    const int rightHand = classIds[14] == 0 ? classIds[8] : 0;
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,%s,%s,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
    //     L"%I64d,%I64d,%s,%s,,,",
    //     0x341,
    //     GetDBID(),
    //     GetAccountID(),
    //     positionX,
    //     positionY,
    //     positionZ,
    //     weaponName.data(),
    //     weaponDetail.data(),
    //     m_data->m_nLevel,
    //     m_data->m_nClass,
    //     classIds[10],
    //     classIds[6],
    //     leftHand,
    //     rightHand,
    //     classIds[9],
    //     classIds[11],
    //     classIds[12],
    //     classIds[14],
    //     GetName(),
    //     accountName)
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,%s,%s,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
    //     L"%I64d,%I64d,%s,%s,,,",
    //     static_cast<long long>(0x341),
    //     static_cast<long long>(GetDBID()),
    //     static_cast<long long>(GetAccountID()),
    //     static_cast<long long>(positionX),
    //     static_cast<long long>(positionY),
    //     static_cast<long long>(positionZ),
    //     weaponName.data(),
    //     weaponDetail.data(),
    //     static_cast<long long>(m_data->m_nLevel),
    //     static_cast<long long>(m_data->m_nClass),
    //     static_cast<long long>(classIds[10]),
    //     static_cast<long long>(classIds[6]),
    //     static_cast<long long>(leftHand),
    //     static_cast<long long>(rightHand),
    //     static_cast<long long>(classIds[9]),
    //     static_cast<long long>(classIds[11]),
    //     static_cast<long long>(classIds[12]),
    //     static_cast<long long>(classIds[14]),
    //     GetName(),
    //     accountName)
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,%s,%s,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
        u"%I64d,%I64d,%s,%s,,,",
        static_cast<long long>(0x341), static_cast<long long>(GetDBID()), static_cast<long long>(GetAccountID()),
        static_cast<long long>(positionX), static_cast<long long>(positionY), static_cast<long long>(positionZ),
        weaponName.data(), weaponDetail.data(), static_cast<long long>(m_data->m_nLevel),
        static_cast<long long>(m_data->m_nClass), static_cast<long long>(classIds[10]), static_cast<long long>(classIds[6]),
        static_cast<long long>(leftHand), static_cast<long long>(rightHand), static_cast<long long>(classIds[9]),
        static_cast<long long>(classIds[11]), static_cast<long long>(classIds[12]), static_cast<long long>(classIds[14]),
        GetName(), accountName
    );
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,%s,%s,"
    //     L"%I64d,%I64d,%I64d,%I64d,,,,,,,%s,%s,,,",
    //     0x1f6a,
    //     GetDBID(),
    //     GetAccountID(),
    //     positionX,
    //     positionY,
    //     positionZ,
    //     weaponName.data(),
    //     weaponDetail.data(),
    //     m_data->m_nLevel,
    //     m_data->m_nClass,
    //     classIds[33],
    //     classIds[13],
    //     GetName(),
    //     accountName)
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,%s,%s,"
    //     L"%I64d,%I64d,%I64d,%I64d,,,,,,,%s,%s,,,",
    //     static_cast<long long>(0x1f6a),
    //     static_cast<long long>(GetDBID()),
    //     static_cast<long long>(GetAccountID()),
    //     static_cast<long long>(positionX),
    //     static_cast<long long>(positionY),
    //     static_cast<long long>(positionZ),
    //     weaponName.data(),
    //     weaponDetail.data(),
    //     static_cast<long long>(m_data->m_nLevel),
    //     static_cast<long long>(m_data->m_nClass),
    //     static_cast<long long>(classIds[33]),
    //     static_cast<long long>(classIds[13]),
    //     GetName(),
    //     accountName)
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,%s,%s,"
        u"%I64d,%I64d,%I64d,%I64d,,,,,,,%s,%s,,,",
        static_cast<long long>(0x1f6a), static_cast<long long>(GetDBID()), static_cast<long long>(GetAccountID()),
        static_cast<long long>(positionX), static_cast<long long>(positionY), static_cast<long long>(positionZ),
        weaponName.data(), weaponDetail.data(), static_cast<long long>(m_data->m_nLevel),
        static_cast<long long>(m_data->m_nClass), static_cast<long long>(classIds[33]), static_cast<long long>(classIds[13]),
        GetName(), accountName
    );
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,%s,%s,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
    //     L"%I64d,,%s,%s,,,",
    //     0x1fbc,
    //     GetDBID(),
    //     GetAccountID(),
    //     positionX,
    //     positionY,
    //     positionZ,
    //     weaponName.data(),
    //     weaponDetail.data(),
    //     m_data->m_nLevel,
    //     m_data->m_nClass,
    //     classIds[34],
    //     classIds[35],
    //     classIds[36],
    //     classIds[37],
    //     classIds[38],
    //     classIds[39],
    //     classIds[40],
    //     GetName(),
    //     accountName)
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,%s,%s,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
    //     L"%I64d,,%s,%s,,,",
    //     static_cast<long long>(0x1fbc),
    //     static_cast<long long>(GetDBID()),
    //     static_cast<long long>(GetAccountID()),
    //     static_cast<long long>(positionX),
    //     static_cast<long long>(positionY),
    //     static_cast<long long>(positionZ),
    //     weaponName.data(),
    //     weaponDetail.data(),
    //     static_cast<long long>(m_data->m_nLevel),
    //     static_cast<long long>(m_data->m_nClass),
    //     static_cast<long long>(classIds[34]),
    //     static_cast<long long>(classIds[35]),
    //     static_cast<long long>(classIds[36]),
    //     static_cast<long long>(classIds[37]),
    //     static_cast<long long>(classIds[38]),
    //     static_cast<long long>(classIds[39]),
    //     static_cast<long long>(classIds[40]),
    //     GetName(),
    //     accountName)
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,%s,%s,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
        u"%I64d,,%s,%s,,,",
        static_cast<long long>(0x1fbc), static_cast<long long>(GetDBID()), static_cast<long long>(GetAccountID()),
        static_cast<long long>(positionX), static_cast<long long>(positionY), static_cast<long long>(positionZ),
        weaponName.data(), weaponDetail.data(), static_cast<long long>(m_data->m_nLevel),
        static_cast<long long>(m_data->m_nClass), static_cast<long long>(classIds[34]), static_cast<long long>(classIds[35]),
        static_cast<long long>(classIds[36]), static_cast<long long>(classIds[37]), static_cast<long long>(classIds[38]),
        static_cast<long long>(classIds[39]), static_cast<long long>(classIds[40]), GetName(), accountName
    );
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,%s,%s,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
    //     L",,%s,%s,,,",
    //     0x1fbe,
    //     GetDBID(),
    //     GetAccountID(),
    //     positionX,
    //     positionY,
    //     positionZ,
    //     weaponName.data(),
    //     weaponDetail.data(),
    //     m_data->m_nLevel,
    //     m_data->m_nClass,
    //     classIds[21],
    //     classIds[22],
    //     classIds[23],
    //     classIds[24],
    //     classIds[25],
    //     classIds[26],
    //     GetName(),
    //     accountName)
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,%s,%s,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
    //     L",,%s,%s,,,",
    //     static_cast<long long>(0x1fbe),
    //     static_cast<long long>(GetDBID()),
    //     static_cast<long long>(GetAccountID()),
    //     static_cast<long long>(positionX),
    //     static_cast<long long>(positionY),
    //     static_cast<long long>(positionZ),
    //     weaponName.data(),
    //     weaponDetail.data(),
    //     static_cast<long long>(m_data->m_nLevel),
    //     static_cast<long long>(m_data->m_nClass),
    //     static_cast<long long>(classIds[21]),
    //     static_cast<long long>(classIds[22]),
    //     static_cast<long long>(classIds[23]),
    //     static_cast<long long>(classIds[24]),
    //     static_cast<long long>(classIds[25]),
    //     static_cast<long long>(classIds[26]),
    //     GetName(),
    //     accountName)
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,%s,%s,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
        u",,%s,%s,,,",
        static_cast<long long>(0x1fbe), static_cast<long long>(GetDBID()), static_cast<long long>(GetAccountID()),
        static_cast<long long>(positionX), static_cast<long long>(positionY), static_cast<long long>(positionZ),
        weaponName.data(), weaponDetail.data(), static_cast<long long>(m_data->m_nLevel),
        static_cast<long long>(m_data->m_nClass), static_cast<long long>(classIds[21]), static_cast<long long>(classIds[22]),
        static_cast<long long>(classIds[23]), static_cast<long long>(classIds[24]), static_cast<long long>(classIds[25]),
        static_cast<long long>(classIds[26]), GetName(), accountName
    );

    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
    //     L"%I64d,%I64d,%I64d,%s,%s,,,",
    //     0x1ff7,
    //     GetDBID(),
    //     GetAccountID(),
    //     positionX,
    //     positionY,
    //     positionZ,
    //     m_SkillMod.m_nActiveArtifactSlotGroup,
    //     1,
    //     classIds[41],
    //     classIds[54],
    //     classIds[57],
    //     classIds[60],
    //     classIds[42],
    //     classIds[43],
    //     classIds[44],
    //     classIds[45],
    //     GetName(),
    //     accountName)
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
    //     L"%I64d,%I64d,%I64d,%s,%s,,,",
    //     static_cast<long long>(0x1ff7),
    //     static_cast<long long>(GetDBID()),
    //     static_cast<long long>(GetAccountID()),
    //     static_cast<long long>(positionX),
    //     static_cast<long long>(positionY),
    //     static_cast<long long>(positionZ),
    //     static_cast<long long>(m_SkillMod.m_nActiveArtifactSlotGroup),
    //     static_cast<long long>(1),
    //     static_cast<long long>(classIds[41]),
    //     static_cast<long long>(classIds[54]),
    //     static_cast<long long>(classIds[57]),
    //     static_cast<long long>(classIds[60]),
    //     static_cast<long long>(classIds[42]),
    //     static_cast<long long>(classIds[43]),
    //     static_cast<long long>(classIds[44]),
    //     static_cast<long long>(classIds[45]),
    //     GetName(),
    //     accountName)
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
        u"%I64d,%I64d,%I64d,%s,%s,,,",
        static_cast<long long>(0x1ff7), static_cast<long long>(GetDBID()), static_cast<long long>(GetAccountID()),
        static_cast<long long>(positionX), static_cast<long long>(positionY), static_cast<long long>(positionZ),
        static_cast<long long>(m_SkillMod.m_nActiveArtifactSlotGroup), static_cast<long long>(1),
        static_cast<long long>(classIds[41]), static_cast<long long>(classIds[54]), static_cast<long long>(classIds[57]),
        static_cast<long long>(classIds[60]), static_cast<long long>(classIds[42]), static_cast<long long>(classIds[43]),
        static_cast<long long>(classIds[44]), static_cast<long long>(classIds[45]), GetName(), accountName
    );
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
    //     L"%I64d,%I64d,%I64d,%s,%s,,,",
    //     0x1ff7,
    //     GetDBID(),
    //     GetAccountID(),
    //     positionX,
    //     positionY,
    //     positionZ,
    //     m_SkillMod.m_nActiveArtifactSlotGroup,
    //     2,
    //     classIds[41],
    //     classIds[49],
    //     classIds[48],
    //     classIds[47],
    //     classIds[46],
    //     classIds[61],
    //     classIds[58],
    //     classIds[55],
    //     GetName(),
    //     accountName)
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
    //     L"%I64d,%I64d,%I64d,%s,%s,,,",
    //     static_cast<long long>(0x1ff7),
    //     static_cast<long long>(GetDBID()),
    //     static_cast<long long>(GetAccountID()),
    //     static_cast<long long>(positionX),
    //     static_cast<long long>(positionY),
    //     static_cast<long long>(positionZ),
    //     static_cast<long long>(m_SkillMod.m_nActiveArtifactSlotGroup),
    //     static_cast<long long>(2),
    //     static_cast<long long>(classIds[41]),
    //     static_cast<long long>(classIds[49]),
    //     static_cast<long long>(classIds[48]),
    //     static_cast<long long>(classIds[47]),
    //     static_cast<long long>(classIds[46]),
    //     static_cast<long long>(classIds[61]),
    //     static_cast<long long>(classIds[58]),
    //     static_cast<long long>(classIds[55]),
    //     GetName(),
    //     accountName)
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
        u"%I64d,%I64d,%I64d,%s,%s,,,",
        static_cast<long long>(0x1ff7), static_cast<long long>(GetDBID()), static_cast<long long>(GetAccountID()),
        static_cast<long long>(positionX), static_cast<long long>(positionY), static_cast<long long>(positionZ),
        static_cast<long long>(m_SkillMod.m_nActiveArtifactSlotGroup), static_cast<long long>(2),
        static_cast<long long>(classIds[41]), static_cast<long long>(classIds[49]), static_cast<long long>(classIds[48]),
        static_cast<long long>(classIds[47]), static_cast<long long>(classIds[46]), static_cast<long long>(classIds[61]),
        static_cast<long long>(classIds[58]), static_cast<long long>(classIds[55]), GetName(), accountName
    );
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
    //     L"%I64d,%I64d,%I64d,%s,%s,,,",
    //     0x1ff7,
    //     GetDBID(),
    //     GetAccountID(),
    //     positionX,
    //     positionY,
    //     positionZ,
    //     m_SkillMod.m_nActiveArtifactSlotGroup,
    //     3,
    //     classIds[41],
    //     classIds[56],
    //     classIds[59],
    //     classIds[62],
    //     classIds[50],
    //     classIds[51],
    //     classIds[52],
    //     classIds[53],
    //     GetName(),
    //     accountName)
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
    //     L"%I64d,%I64d,%I64d,%s,%s,,,",
    //     static_cast<long long>(0x1ff7),
    //     static_cast<long long>(GetDBID()),
    //     static_cast<long long>(GetAccountID()),
    //     static_cast<long long>(positionX),
    //     static_cast<long long>(positionY),
    //     static_cast<long long>(positionZ),
    //     static_cast<long long>(m_SkillMod.m_nActiveArtifactSlotGroup),
    //     static_cast<long long>(3),
    //     static_cast<long long>(classIds[41]),
    //     static_cast<long long>(classIds[56]),
    //     static_cast<long long>(classIds[59]),
    //     static_cast<long long>(classIds[62]),
    //     static_cast<long long>(classIds[50]),
    //     static_cast<long long>(classIds[51]),
    //     static_cast<long long>(classIds[52]),
    //     static_cast<long long>(classIds[53]),
    //     GetName(),
    //     accountName)
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
        u"%I64d,%I64d,%I64d,%s,%s,,,",
        static_cast<long long>(0x1ff7), static_cast<long long>(GetDBID()), static_cast<long long>(GetAccountID()),
        static_cast<long long>(positionX), static_cast<long long>(positionY), static_cast<long long>(positionZ),
        static_cast<long long>(m_SkillMod.m_nActiveArtifactSlotGroup), static_cast<long long>(3),
        static_cast<long long>(classIds[41]), static_cast<long long>(classIds[56]), static_cast<long long>(classIds[59]),
        static_cast<long long>(classIds[62]), static_cast<long long>(classIds[50]), static_cast<long long>(classIds[51]),
        static_cast<long long>(classIds[52]), static_cast<long long>(classIds[53]), GetName(), accountName
    );
}

void User::LeaveWorld(bool save) {
    // Current L2Server.exe 0x00bcf540.
    L2_GUARD_FUNCTION();

    g_FieldCycleSystem.OnUserLeaveWorld(this);
    g_PledgeGameSystem.LeaveGame(this, true);
    if (save) {
        LeaveWorldForRaid();
    }

    RelayClient::Inst()->SendLogOffPacket(this);
    RelayClient::Inst()->SendUserInfo(this);

    if (save && m_RaidInfo.m_nRaidWorldId > 0 && !Singleton<WOlympiadServiceProxy>::inst().IsMember(GetDBID())) {
        Singleton<RaidClientManager>::inst().DeleteRaidReserved(m_RaidInfo.m_nRaidWorldId, GetDBID(), true);
    }

    if (m_EventMatchUser.m_ManagingMatchId != 0) {
        CSPointer<CEventMatch> match = m_EventMatchUser.GetManagingEventMatch();
        if (match) {
            match->LeaveManager();
        }
    }

    m_inv.UpdateRuneItem();
    SendLogoffMsgToFriend();
    ReleaseAllPremiumItemList();
    ItemRefundClear();
    ProcessMyParty(true);
    ProcessMyMPCC(true);
    SetMentorMenteeLogout();

    if (CPledge* const pledge = GetPledge()) {
        if (IsPledgeMaster()) {
            pledge->SetMaster(nullptr);
        }
        pledge->AcademyOfflineMsg(this);
        pledge->DeleteMember(this);
        pledge->Offline(this);
        pledge->SendPledgeOnlineMemberCount();
    }

    if (g_bInitedNCGuard) {
        // const NCGuardServerResult result =
        //     DeleteNCGuardUserInfo(GetDBID())
        const std::uint32_t ncGuardCharacterId = GetDBID();
        const NCGuardServerResult result = DeleteNCGuardUserInfo(ncGuardCharacterId);
        if (result != 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"delete NCGuard user fail[%d]",
            //     result)
            // Log.Add(
            //     LOG_ERROR,
            //     "[NCGuard] Cannot delete user state for character DB ID %u: error 0x%08x. World exit continues.",
            //     static_cast<unsigned>(ncGuardCharacterId),
            //     static_cast<unsigned>(result))
            L2_LOG_ERROR(
                Log, "[NCGuard] Cannot delete user state for character DB ID %u: error 0x%08x; world exit continues.",
                static_cast<unsigned>(ncGuardCharacterId), static_cast<unsigned>(result)
            );
        }
    }

    const std::u16string lowerName = LowercaseGameName(GetName());
    const int worldId = m_data->m_nWorldId;
    const int accountId = static_cast<int>(GetAccountID());
    g_UserMap.DeleteObject(static_cast<int>(GetServerId()), const_cast<char*>("user_world.cpp"), 0xad4);
    g_UserDBIDMap.DeleteObject(
        std::CDualKey<int, int>(worldId, static_cast<int>(GetDBID())), const_cast<char*>("user_world.cpp"), 0xad5
    );
    g_GMCharMap.DeleteObject(static_cast<int>(GetServerId()), const_cast<char*>("user_world.cpp"), 0xad6);
    g_UserAccountDBIDMap.DeleteObject(accountId, const_cast<char*>("user_world.cpp"), 0xad7);
    g_UserCharNameMap.DeleteObject(worldId, lowerName, const_cast<char*>("user_world.cpp"), 0xad8);

    if (worldId == g_nWorldId) {
        g_NewUserCharNameMap.DeleteObject(lowerName);
    } else {
        if (CPet* const pet = GetPet(); pet != nullptr && !pet->IsAlive()) {
            g_PetManager.DeletePet(static_cast<int>(GetDBID()), static_cast<int>(GetServerId()));
        }
        // For a raid-world user GetName() is the alias installed by
        // EnterWorld. Current formats that value through L"%s" and removes
        // its lowercase form from the cross-world index.
        g_NewUserCharNameMap.DeleteObject(LowercaseGameName(GetName()));
    }

    m_BotChecker.ReleaseOwner(this);
    PostUtil::DeletePostByUserDisconnected(this);
    LogPlayTimeOnLeaveWorld();
    LogEquippedItemOnLeaveWorld();
    ChannelChattingClient::Inst().SendLogOut(this);

    if (GetSocket()->m_VipAttendnaceCheck == 0) {
        CUserSocket* const socket = GetSocket();
        const int elapsed = m_nUseTimeStamp > 0 ? runtime::crt::Time32Now() - m_nUseTimeStamp : 0;
        socket->m_nAttendanceDelayTime = runtime::AddInt32Wrapping(socket->m_nAttendanceDelayTime, elapsed);
    }

    if (m_nCaptchaConfirmTransactionId != 0) {
        CSPointer<CCaptchaJob> captchaJob = g_CaptchaManager.PopCaptchaJob(m_nCaptchaConfirmTransactionId);
    }
    SaveCaptchaConfirm();
    if (g_bUseAutoSoulShotUI) {
        db.RequestSaveAutoSoulShotUI(this);
    }
    UpdateMonsterBookToDB();
    PledgeMissionSendToDBIfChanged();
}

void User::CheckOneDayReward(bool sendPacket) {
    // Current L2Server 0x00bc2f90. The bool lane is unused.
    L2_GUARD_FUNCTION();
    static_cast<void>(sendPacket);

    std::int32_t canReceiveCount = 0;
    const std::int32_t now = runtime::crt::Time32Now();
    for (const auto& entry : m_DailyMissionMap) {
        ICDailyMission* const mission = entry.second;
        if (mission != nullptr && mission->GetOneDayRewardState(now) == E_ONE_DAY_REWARD_STATE_TYPE_CAN) {
            ++canReceiveCount;
        }
    }

    m_CheckTimeDailyMission = runtime::crt::Time32Now();
    GetSocket()->Send("chdddddddddddd", 0xfe, 0x1a9, 0, canReceiveCount, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

void User::SendToDoList(E_TODOLIST_TYPE type, bool entering) {
    // Current L2Server 0x00bcfbac.
    L2_GUARD_FUNCTION();

    if (type == E_TODOLIST_TYPE_DAILY_REWARD) {
        const std::int32_t now = runtime::crt::Time32Now();
        std::uint32_t totalCount = 0;
        std::uint32_t canReceiveCount = 0;
        CPacketHelperEx<8192> packet;

        if (!m_bLoadDBOneDayReward) {
            m_bSendOneDayReward = true;
        } else {
            for (const auto& entry : m_DailyMissionMap) {
                ICDailyMission* const mission = entry.second;
                if (mission == nullptr) {
                    continue;
                }
                const E_ONE_DAY_REWARD_STATE_TYPE state = mission->GetOneDayRewardState(now);
                if (state == E_ONE_DAY_REWARD_STATE_TYPE_INVALID) {
                    continue;
                }
                if (state == E_ONE_DAY_REWARD_STATE_TYPE_CAN) {
                    ++canReceiveCount;
                }
                packet.Assemble(
                    "hccdd", static_cast<int>(static_cast<std::int16_t>(mission->m_DBID)),
                    static_cast<int>(static_cast<std::int8_t>(state)),
                    static_cast<int>(static_cast<std::int8_t>(mission->IsShortMakeName())),
                    static_cast<std::int32_t>(mission->m_AccumulateCount), mission->GetConditionCount()
                );
                ++totalCount;
            }

            for (const auto& entry : m_CompleteDailyMissionMap) {
                ICDailyMission* const mission = entry.second;
                if (mission == nullptr) {
                    continue;
                }
                const E_ONE_DAY_REWARD_STATE_TYPE state = mission->GetOneDayRewardState(now);
                if (state == E_ONE_DAY_REWARD_STATE_TYPE_INVALID) {
                    continue;
                }

                // The current body calls GetConditionCount twice and passes
                // the second result before the first result.
                const int firstConditionCount = mission->GetConditionCount();
                const int secondConditionCount = mission->GetConditionCount();
                const bool shortName = mission->IsShortMakeName();
                packet.Assemble(
                    "hccdd", static_cast<int>(static_cast<std::int16_t>(mission->m_DBID)),
                    static_cast<int>(static_cast<std::int8_t>(state)), static_cast<int>(static_cast<std::int8_t>(shortName)),
                    secondConditionCount, firstConditionCount
                );
                ++totalCount;
            }

            m_CheckTimeDailyMission = runtime::crt::Time32Now();
            GetSocket()->Send("chdddddddddddd", 0xfe, 0x1a9, 0, canReceiveCount, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        }

        if (g_bUseOneDayReward) {
            std::tm* const local = LocalTime32Pointer(now);
            GetSocket()->Send(
                "chcdddb", 0xfe, 0x1a8,
                static_cast<int>(static_cast<std::int8_t>(g_InstantZoneDB.GetInstantZoneToDoListMapCount())),
                static_cast<int>(m_data->m_nClass), local->tm_wday, totalCount, packet.GetLength(), packet.GetPacket()
            );
        }
        return;
    }

    if (type == E_TODOLIST_TYPE_INSTANTZONE) {
        g_InstantZoneDB.SendToToList(this, entering);
        return;
    }

    ToDoListRecommandInfoGroupMap information;
    const std::int32_t now = runtime::crt::Time32Now();
    static_cast<void>(LocalTime32Pointer(now));
    static_cast<void>(runtime::GetLocalTimeNow());
    CEventManager::inst().GetToDoListInfo(information, entering, this);

    if (g_bUseOneDayReward) {
        short count = 0;
        std::array<char, 0x5000> buffer{};
        unsigned int length = 0;
        ToDoListRecommandInfoMap groupInformation;
        ToDoListRecommandInfo info;
        for (const auto& group : information) {
            groupInformation = group.second;
            for (const auto& entry : groupInformation) {
                info = entry.second;
                info.AssembleToDoInfo(buffer.data(), static_cast<int>(buffer.size()), length, count);
            }
        }
        GetSocket()->Send("chhb", 0xfe, 0x1aa, static_cast<int>(count), length, buffer.data());
    }
}

void User::LeaveWorldForRaid() {
    // Current L2Server.exe 0x00bceccc, original owner user_world.cpp.
    L2_GUARD_FUNCTION();

    CCreature::LeaveWorld(true);
    bool saveInfectedSkills = false;
    m_spTarget = CSmartID{};
    QuitPrivateStore(true);
    GetController()->Disappear();
    Singleton<TalkChannelCache>::inst().ResetCache(Id());

    CInstantZone* instantZone = nullptr;
    if (!IsInRealWorld()) {
        instantZone = GetInstantZone();
    } else if (m_nNextInstantZoneID != 0U) {
        CGeneralObject* const object = CGeneralObject::GetObjectW(m_nNextInstantZoneID);
        if (object != nullptr && object->IsInZone()) {
            instantZone = object->CastInZone();
        }
    }
    if (instantZone != nullptr) {
        FVector returnPosition{};
        if (instantZone->LeaveZoneWithoutTeleport(this, returnPosition, true)) {
            m_data->SetPos(returnPosition);
        }
    }

    if (CPledge* const pledge = GetPledge()) {
        pledge->RemovePledgeSkillByEliteFrom(this);
    }

    if (m_data->m_bIsAlive != 0) {
        if (m_data->m_MoveMode == MT_FAST) {
            CGeoData* const geo = CWorld::Inst().GetGeo();
            const FVector positionForY = GetPos();
            const FVector positionForX = GetPos();
            CGeoZone* const zone = geo->GetZone(
                ((static_cast<int>(positionForX.x) + 0x50000) >> 15) + 10,
                ((static_cast<int>(positionForY.y) + 0x40000) >> 15) + 10
            );
            if (zone != nullptr && zone->IsNoFlyArea(GetPos())) {
                const FVector restart = Singleton<CSetting>::inst().GetRestartPoint(
                    m_data->m_Race, m_data->m_Pos, m_data->m_nKarma < 0, m_data->m_nPKCount
                );
                m_data->SetPos(restart);
            }
        }

        db.SendSaveCharacterInfo(this, false, true);
        db.RequestLogCharInfo(*this);
        LogNPBuffStatus();
        saveInfectedSkills = m_nChangeSubjobFlag == 1;
        SaveCharExtraInfo();
    }

    SaveSkillUsedTime(ReuseDelaySaveReason::SR_DUMP, true);
    if (m_nChangeSubjobFlag != 1 && !m_bKickByAgent) {
        saveInfectedSkills = true;
    }

    if (!m_bDoubleSummon) {
        db.RequestSaveSummon(*this);
        DespawnAllSummon(true, true);
    } else {
        DespawnAllSummon(true, false);
        db.RequestSaveSummon(*this);
    }
    DespawnPet(true, false, DESPAWN_PET_NONE, false);
    if (CPet* const pet = GetPet(); pet != nullptr && !pet->IsAlive()) {
        pet->OnMasterLeaveWorld();
    }

    DespawnPCTrap();
    DespawnAgathion(1);
    ToggleSkillAllOff(false);
    AuraSkillAllOff(false);

    if (m_nTrade > 0) {
        CSPointer<CTrade> trade = g_TradeManager.GetTradeSP(m_nTrade);
        if (!trade) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] Leave Trade Done Error. "
            //     L"m_nTrade = [%d], spTrade is NULL",
            //     "user_world.cpp",
            //     0xbe5,
            //     m_nTrade)
            // Log.Add(
            //     LOG_ERROR,
            //     "[world exit] Cannot cancel trade %d for user object %u: the trade was not found. Remaining world-exit
            //     cleanup continues.", m_nTrade, static_cast<unsigned>(m_nId))
            L2_LOG_ERROR(
                Log,
                "[world exit] Cannot cancel trade %d for user object %u: the trade was not found; remaining world-exit "
                "cleanup continues.",
                m_nTrade, static_cast<unsigned>(m_nId)
            );
        } else {
            trade->Canceled(this, false);
        }
    }

    CObserverRemains* observerRemains;
    bool wasAlive;
    {
        L2_GUARD(*m_lock);
        observerRemains = m_pObserverRemains;
        m_pObserverRemains = nullptr;
        wasAlive = m_data->m_bIsAlive != 0;
    }

    if (wasAlive) {
        CWorld::Inst().LeaveWorld(this, GetPos(), false);
    }
    if (observerRemains != nullptr) {
        observerRemains->RemoveRemains();
        L2_RELEASE(observerRemains, ObjectRefType::Anonymous);
        observerRemains->Delete();
    }

    {
        L2_GUARD(*m_lock);
        ReleaseEconomy();
    }

    g_PartyMatchingWaitingRoom.LeaveWaitingRoom(this);
    CSPointer<User> self(this, ObjectRefType::FunctionChain);
    Singleton<CPartyMatchingRequest>::inst().WithdrawMPCCRoom(self);
    Singleton<CPVPMatchSystem>::inst().LeaveWorld(*this);

    if (Singleton<WOlympiadServiceConfig>::inst().IsHost() && !m_pWOlympiadUser->m_IsObserver) {
        DispelByBuffProtectionFlag(2);
        saveInfectedSkills = true;
    }
    m_pWOlympiadUser->OnLeaveWorld();
    m_PledgeGameUser.OnLeaveWorld();

    if (IsInDuel()) {
        Singleton<CDuelManager>::inst().DuelPlayerLeaveWorld(this);
    }
    CFortress::HandleDeleteFlag(this, true);
    if (m_pOwnThing != nullptr) {
        DeleteOwnThing(true, false);
    }

    UserUtil::UnregisterFromRandomUser(static_cast<int>(Id()));
    SavePunishTime();
    AbandonAirShipDriver();
    SaveVoteSystem();

    const bool wasPlaying = m_bPlaying;
    {
        L2_GUARD(*m_lock);
        if (m_bPlaying == true) {
            m_bPlaying = false;
        }
    }
    if (wasPlaying == true) {
        const int playingUserCount = runtime::raw_atomic::DecrementI32(&g_nPlayingUserCount);
        SharedEventInfo::Inst()->m_gameEvent.m_PlayingUserAverage = playingUserCount;
        Singleton<CEventManager>::inst().UpdateDropTick(runtime::raw_atomic::LoadI32Relaxed(&g_nPlayingUserCount));
    }

    if (m_nChangeSubjobFlag != 1 &&
        (m_data->m_nWorldId != g_nWorldId || (GetSocket() != nullptr && GetSocket()->GetRaidStatus() == URS_NONE))) {
        m_pVitality->OnLeaveWorld();
    }

    CEvent* const event = Singleton<CEventManager>::inst().GetEvent(GAME_EVENT::ET_ACCOUNT_EVENT);
    if (event != nullptr && event->InEventTime()) {
        static_cast<CEvent_AccountEvent*>(event)->SetSendMsg(static_cast<int>(GetAccountID()), true);
    }

    for (bool& status : m_data->m_StatusFlag) {
        status = true;
    }
    if (m_pAgent != nullptr) {
        static_cast<GameGuardAgent*>(m_pAgent)->m_csa.Close();
    }
    if (m_nRelationSlot != -1) {
        Singleton<CRelationManager>::inst().ReturnSlot(static_cast<unsigned int>(m_nRelationSlot));
    }

    m_monsterBook.OnUserLeaveWorldForRaid(GetSocket());
    if (saveInfectedSkills) {
        SaveInfectedSkills();
    }
    m_UpDownGame.OnLeaveWorld(*this);
    PledgeMissionSendToDBIfChanged();
}

void User::SendInSight(CObject* object, bool first) {
    // Current L2Server.exe 0x00bbb7f4. Keep the original ordered type
    // dispatch: several static-object families overlap at the CObject layer,
    // so independent broadcasts would change the selected packet.
    L2_GUARD_FUNCTION();
    if (object->IsCreature()) {
        if (object->IsPC()) {
            object->CastUser()->SendCreateObject(this, false);
            object->CastUser()->SendRelationChanged(GetSocket());
            if (object->CastUser()->IsTransformed(TransformType::Max) != nullptr) {
                object->CastUser()->SendCreateObject(this, false);
            }
            if (m_data->m_nDominionSiegeId != 0 || object->CastUser()->m_data->m_nDominionSiegeId != 0) {
                object->CastUser()->SendDominionStartEnd(true, GetSocket());
            }
        } else if (object->IsNPC()) {
            CNPC* const npc = object->CastNPC();
            if (npc->IsDoppelganger()) {
                User* const master = object->CastNPC()->GetMaster();
                if (master != nullptr && master->Id() != Id()) {
                    master->SendCreateObject(this, false);
                }
            }
            if (!npc->m_isDroidNPC) {
                npc->SendNPCInfo(this, first, false);
            } else {
                npc->SendCreateObject(this);
            }
            if (npc->IsSummonOrPet()) {
                object->CastSummon()->SendMyInfo();
            }
        }

        CCreature* const creature = object->CastCreature();
        if (creature != nullptr && creature->m_bMoving) {
            const FVector detail = creature->GetDetailLocation();
            const FVector destination = creature->m_vMovingDest;
            GetSocket()->Send(
                "cddddddd", 0x2f, static_cast<int>(creature->Id()), static_cast<int>(destination.x),
                static_cast<int>(destination.y), static_cast<int>(destination.z), static_cast<int>(detail.x),
                static_cast<int>(detail.y), static_cast<int>(detail.z)
            );
        }

        if (object->IsPC()) {
            User* const target = object->CastUser();
            const CSkillInfo* const skill = target->m_pNowSkill;
            const int targetId = target->m_nSkillTargetId;
            const int remaining = target->m_nSkillEndTime - runtime::crt::Time32Now();
            if (skill != nullptr && targetId != 0 && remaining > 0) {
                CStaticObject* const staticTarget = CStaticObject::GetObjectW(static_cast<std::uint32_t>(targetId));
                if (staticTarget != nullptr && staticTarget->IsHolyThing()) {
                    staticTarget->CastHolyThing()->SendHolyThingInfo(GetSocket(), false, 0);
                    GetSocket()->Send(
                        "cddddddd", 0xd8, static_cast<int>(target->Id()), targetId, skill->m_nSkillId,
                        (skill->m_nSubLevel << 16) | (skill->m_nLevel & 0xffff),
                        runtime::Win64CvttDoubleToInt32(skill->m_dSkillHitTime * 1000.0),
                        runtime::Win64CvttDoubleToInt32(skill->m_dSkillCoolTime * 1000.0), remaining
                    );
                }
            }
        }
        return;
    }

    if (object->IsItem()) {
        object->CastItem()->SendItemSpawn(GetSocket());
    } else if (object->IsDoor()) {
        object->CastDoor()->SendDoorStatus(GetSocket());
    } else if (object->IsHolyThing()) {
        object->CastHolyThing()->SendHolyThingInfo(GetSocket(), false, 0);
    } else if (object->IsBattleCamp()) {
        object->CastBattleCamp()->SendBattleCampInfo(GetSocket());
    } else if (object->IsVehicle()) {
        object->CastVehicle()->SendVehicleInfo(GetSocket());
    } else if (object->IsAirShip()) {
        object->CastAirShip()->SendAirShipInfo(GetSocket());
    } else if (object->IsShuttle()) {
        object->CastShuttle()->SendShuttleInfo(GetSocket(), false, 0);
    } else if (object->IsObserverRemains()) {
        object->CastObserverRemains()->SendObserverRemainsInfo(GetSocket());
    } else if (object->IsSignBoard()) {
        object->CastSignBoard()->SendSignBoardInfo(GetSocket());
    } else if (object->IsChair()) {
        object->CastChair()->SendChairInfo(GetSocket());
    } else if (object->IsControlTower()) {
        object->CastControlTower()->SendControlTowerInfo(GetSocket());
    } else if (object->IsEventController()) {
        object->CastEventController()->SendEventControllerInfo(GetSocket(), true);
    } else if (object->IsTeamBattleFlag()) {
        object->CastTeamBattleFlag()->SendTeamBattleFlagInfo(GetSocket());
    } else if (object->IsResidenceDecoVisible()) {
        object->CastResidenceDecoVisible()->SendResidenceDecoVisibleInfo(GetSocket());
    } else if (object->IsEventMatch()) {
        object->CastEventMatch()->SendEventMatchInfo(GetSocket());
    } else if (object->IsFortressFlagpole()) {
        object->CastFortressFlagpole()->SendFortressFlagpoleInfo(GetSocket());
    } else if (object->IsAirShipPart()) {
        object->CastAirShipPart()->SendMySelfInfoTo(GetSocket());
    } else if (object->IsOwnThing()) {
        object->CastOwnThing()->SendOwnThingInfo(GetSocket());
    } else if (!object->IsAirShipSteering()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] What object?",
        //     L"user_world.cpp",
        //     0x585)
        // Log.Add(
        //     LOG_ERROR,
        //     "[visibility] Cannot send visibility information for object %u (class %d) to user object %u: no matching
        //     object-type handler.", static_cast<unsigned>(object->m_nId), object->m_nClassId,
        //     static_cast<unsigned>(m_nId))
        L2_LOG_ERROR(
            Log,
            "[visibility] Cannot send visibility information for object %u (class %d) to user object %u: no matching "
            "object-type handler.",
            static_cast<unsigned>(object->m_nId), object->m_nClassId, static_cast<unsigned>(m_nId)
        );
    }
}

void User::SendOutOfSight(CObject* object, bool flag) {
    // Current L2Server.exe 0x00bbc0e8.
    if (!object->IsEventController()) {
        SendDeleteObject(object, flag);
    } else {
        object->CastEventController()->SendEventControllerInfo(GetSocket(), false);
    }
}

void User::InSight(CObject* object, bool flag) {
    // Current L2Server.exe 0x00bbf348 and local callbacks
    // 0x00bbc204..0x00bbc2fc.
    L2_GUARD_FUNCTION();
    g_Timer.BeginJob(JOB_USER_INSIGHT);

    User* const targetUser = object->GetUserOrMaster();
    if (targetUser != nullptr && targetUser->GetDBID() != GetDBID() && IsInFifthClassGroup(targetUser->m_data->m_nClass)) {
        g_Timer.EndJob(JOB_USER_INSIGHT);
        return;
    }

    if (object->IsVehicle()) {
        CContainer* const container = object->CastVehicle()->GetContainer();
        SendInSight(object, flag);

        class VehicleFilter final : public CContainerFilter {
        public:
            bool CanDo(CObject* candidate) override { return !candidate->IsVehicle(); }
        } filter;
        class VehicleFunc final : public CContainerFunc {
        public:
            explicit VehicleFunc(User* viewer) : viewer_(viewer) {}

            void Do(CObject* candidate) override {
                if (!candidate->IsPC() || (!candidate->IsDeleting() && candidate->m_Deleted == 0)) {
                    viewer_->InSight(candidate, false);
                }
            }

        private:
            User* viewer_;
        } function(this);
        FOR_ALL_OBJECT_IN_CONTAINER__INSTANT(container, filter, function);
        g_Timer.EndJob(JOB_USER_INSIGHT);
        return;
    }

    if (object->IsAirShip()) {
        SendInSight(object, flag);
        CContainer* const container = object->CastAirShip()->m_pContainer;

        class AirShipFilter final : public CContainerFilter {
        public:
            explicit AirShipFilter(User* viewer) : viewer_(viewer) {}

            bool CanDo(CObject* candidate) override { return !candidate->IsAirShip() && candidate != viewer_; }

        private:
            User* viewer_;
        } filter(this);
        class AirShipFunc final : public CContainerFunc {
        public:
            explicit AirShipFunc(User* viewer) : viewer_(viewer) {}

            void Do(CObject* candidate) override {
                viewer_->InSight(candidate, false);
                if (candidate->IsPC() && !candidate->IsDeleting() && candidate->m_Deleted == 0) {
                    candidate->CastUser()->InSight(viewer_, false);
                }
            }

        private:
            User* viewer_;
        } function(this);
        FOR_ALL_OBJECT_IN_CONTAINER__INSTANT(container, filter, function);
        g_Timer.EndJob(JOB_USER_INSIGHT);
        return;
    }

    if (object->IsShuttle()) {
        SendInSight(object, flag);
        CContainer* const container = object->CastShuttle()->m_pContainer;

        class ShuttleFilter final : public CContainerFilter {
        public:
            explicit ShuttleFilter(User* viewer) : viewer_(viewer) {}

            bool CanDo(CObject* candidate) override { return !candidate->IsShuttle() && candidate != viewer_; }

        private:
            User* viewer_;
        } filter(this);
        class ShuttleFunc final : public CContainerFunc {
        public:
            explicit ShuttleFunc(User* viewer) : viewer_(viewer) {}

            void Do(CObject* candidate) override {
                viewer_->InSight(candidate, false);
                if (candidate->IsPC() && !candidate->IsDeleting() && candidate->m_Deleted == 0) {
                    candidate->CastUser()->InSight(viewer_, false);
                }
            }

        private:
            User* viewer_;
        } function(this);
        FOR_ALL_OBJECT_IN_CONTAINER__INSTANT(container, filter, function);
        g_Timer.EndJob(JOB_USER_INSIGHT);
        return;
    }

    bool canBeSeen = true;
    if (!object->IsVisible()) {
        canBeSeen = false;
        if (object->IsPCTrap()) {
            User* const master = object->CastPCTrap()->GetMaster();
            if (master != nullptr && (master->Id() == Id() || (m_nPartySid != 0 && master->m_nPartySid == m_nPartySid))) {
                canBeSeen = true;
            }
        }
        if (!canBeSeen && object->CanBeSeenBy(*this)) {
            canBeSeen = true;
        }
        if (!canBeSeen) {
            g_Timer.EndJob(JOB_USER_INSIGHT);
            return;
        }
    }

    if (object->IsPC()) {
        runtime::raw_atomic::IncrementI32(&m_nUserCountInSight);
    } else if (object->IsNPC()) {
        runtime::raw_atomic::IncrementI32(&m_nNpcCountInSight);
    }
    SendInSight(object, flag);
    g_Timer.EndJob(JOB_USER_INSIGHT);
}

void User::OutOfSight(CObject* object, bool flag) {
    // Current L2Server.exe 0x00bbde50 and local callbacks
    // 0x00bbc33c..0x00bbc454.
    L2_GUARD_FUNCTION();
    g_Timer.BeginJob(JOB_USER_OUTOFSIGHT);

    if (object->IsVehicle()) {
        CContainer* const container = object->CastVehicle()->GetContainer();
        class VehicleFilter final : public CContainerFilter {
        public:
            bool CanDo(CObject* candidate) override { return !candidate->IsVehicle(); }
        } filter;
        class VehicleFunc final : public CContainerFunc {
        public:
            VehicleFunc(User* viewer, bool flag) : viewer_(viewer), flag_(flag) {}

            void Do(CObject* candidate) override {
                if (!viewer_->IsDeleting() && viewer_->m_Deleted == 0 &&
                    (!candidate->IsPC() || (!candidate->IsDeleting() && candidate->m_Deleted == 0))) {
                    candidate->OutOfSight(viewer_, flag_);
                    viewer_->SendOutOfSight(candidate, flag_);
                }
            }

        private:
            User* viewer_;
            bool flag_;
        } function(this, flag);
        FOR_ALL_OBJECT_IN_CONTAINER__INSTANT(container, filter, function);
        SendOutOfSight(object, flag);
        g_Timer.EndJob(JOB_USER_OUTOFSIGHT);
        return;
    }

    if (object->IsAirShip()) {
        CContainer* const container = object->CastAirShip()->m_pContainer;
        class AirShipFilter final : public CContainerFilter {
        public:
            bool CanDo(CObject* candidate) override { return !candidate->IsAirShip(); }
        } filter;
        class AirShipFunc final : public CContainerFunc {
        public:
            AirShipFunc(User* viewer, bool flag) : viewer_(viewer), flag_(flag) {}

            void Do(CObject* candidate) override {
                if (!viewer_->IsDeleting() && viewer_->m_Deleted == 0 &&
                    (!candidate->IsPC() || (!candidate->IsDeleting() && candidate->m_Deleted == 0))) {
                    viewer_->SendOutOfSight(candidate, flag_);
                    candidate->OutOfSight(viewer_, flag_);
                }
            }

        private:
            User* viewer_;
            bool flag_;
        } function(this, flag);
        FOR_ALL_OBJECT_IN_CONTAINER__INSTANT(container, filter, function);
        SendOutOfSight(object, flag);
        g_Timer.EndJob(JOB_USER_OUTOFSIGHT);
        return;
    }

    if (object->IsShuttle()) {
        CContainer* const container = object->CastShuttle()->m_pContainer;
        class ShuttleFilter final : public CContainerFilter {
        public:
            bool CanDo(CObject* candidate) override { return !candidate->IsShuttle(); }
        } filter;
        class ShuttleFunc final : public CContainerFunc {
        public:
            ShuttleFunc(User* viewer, bool flag) : viewer_(viewer), flag_(flag) {}

            void Do(CObject* candidate) override {
                if (!viewer_->IsDeleting() && viewer_->m_Deleted == 0 &&
                    (!candidate->IsPC() || (!candidate->IsDeleting() && candidate->m_Deleted == 0))) {
                    viewer_->SendOutOfSight(candidate, flag_);
                    candidate->OutOfSight(viewer_, flag_);
                }
            }

        private:
            User* viewer_;
            bool flag_;
        } function(this, flag);
        FOR_ALL_OBJECT_IN_CONTAINER__INSTANT(container, filter, function);
        SendOutOfSight(object, flag);
        g_Timer.EndJob(JOB_USER_OUTOFSIGHT);
        return;
    }

    if (object->IsPC()) {
        runtime::raw_atomic::DecrementI32(&m_nUserCountInSight);
    } else if (object->IsNPC()) {
        runtime::raw_atomic::DecrementI32(&m_nNpcCountInSight);
    }
    SendOutOfSight(object, flag);
    g_Timer.EndJob(JOB_USER_OUTOFSIGHT);
}

void User::ReadyToFlyMove() {
    // Current L2Server.exe 0x00bbcc94.
    m_nFlyMoveReadyCount = 1;
}

void User::StartFlyMove() {
    // Current L2Server 0x00bc4324.
    L2_GUARD_FUNCTION();

    if (HaveSummon() || HavePet()) {
        return;
    }
    if (!IsInFifthClassGroup(static_cast<ClassType>(GetClass())) &&
        !IsInSixthClassGroup(static_cast<ClassType>(GetClass()))) {
        return;
    }
    if (!CanStartFlyMove()) {
        SendSystemMessage(SystemMessageId::CANNOT_USE_SAYUNE_DURING_OTHER_ACTION);
        return;
    }

    const std::uint32_t inZoneId = GetInZoneID();
    const FVector position = GetPos();
    if (!CAreaDB::inst().IsFlyMoveZone(position, inZoneId) || GetMorphID() != 0 || m_data->m_bIsFakeDeath) {
        return;
    }

    if (m_bMoving) {
        StopMove();
        DoNothing();
    }
    if (m_bAskCoupleAction) {
        return;
    }

    CFlyMoveManager& manager = CFlyMoveManager::Inst();
    const int pathId = m_FlyMoveInfo.path_id;
    const int nodeId = m_FlyMoveInfo.node_id;
    CFlyMovePathNode* const node = manager.GetPathNode(pathId, nodeId);
    if (node == nullptr) {
        // Current MSVC evaluates these three position arguments in reverse.
        const int z = static_cast<int>(GetPos().z);
        const int y = static_cast<int>(GetPos().y);
        const int x = static_cast<int>(GetPos().x);
        // Log.Add(
        //     LOG_ERROR,
        //     L"no node for [%d/%d] at [%d/%d/%d]",
        //     pathId,
        //     nodeId,
        //     x,
        //     y,
        //     z)
        // Log.Add(
        //     LOG_ERROR,
        //     "[fly move] Cannot start Sayune movement for user object %u: path %d has no node %d. Position (%d, %d, %d),
        //     instance %u.", static_cast<unsigned>(m_nId), pathId, nodeId, x, y, z, static_cast<unsigned>(inZoneId))
        L2_LOG_ERROR(
            Log,
            "[fly move] Cannot start Sayune movement for user object %u: path %d has no node %d; position (%d, %d, %d), "
            "instance %u.",
            static_cast<unsigned>(m_nId), pathId, nodeId, x, y, z, static_cast<unsigned>(inZoneId)
        );
        return;
    }

    m_bFlyMoving = true;
    CUserSocket* const socket = GetSocket();
    const int currentZ = static_cast<int>(GetPos().z);
    const int currentY = static_cast<int>(GetPos().y);
    const int currentX = static_cast<int>(GetPos().x);
    const int nodeZ = static_cast<int>(node->GetPos().z);
    const int nodeY = static_cast<int>(node->GetPos().y);
    const int nodeX = static_cast<int>(node->GetPos().x);
    socket->Send("cdddddddddd", 0xd4, static_cast<int>(m_nId), nodeX, nodeY, nodeZ, currentX, currentY, currentZ, 2, 0, 0);
    manager.FlyMove(this, pathId, nodeId, true);
}

void User::BroadcastToAllowedUser(const char* const format, ...) {
    L2_GUARD_FUNCTION();

    va_list arguments;
    va_start(arguments, format);
    DefaultIOBuffer* const buffer = MakeConstBuffer(format, arguments);
    va_end(arguments);
    if (buffer == nullptr) {
        return;
    }

    g_Timer.BeginJob(JOB_AsyncSendConstBuffer);
    int count = 0;
    FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, candidate, {
        if (candidate->m_bBlockAll || candidate->IsInBlockList(*this)) {
            continue;
        }
        buffer->AddRef();
        candidate->GetSocket()->AsyncSendConstBuffer(buffer);
        ++count;
    });

    if (CUserSocket* const socket = GetSocket()) {
        socket->AccumulateWorldChatPacket(count, buffer->m_dwSize);
    }
    buffer->Release();
    g_Timer.EndJob(JOB_AsyncSendConstBuffer);
}
