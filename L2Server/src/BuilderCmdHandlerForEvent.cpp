#include "BalthusEvent.h"
#include "BuilderCmdHandler.h"
#include "DB.h"
#include "GameEventSystem.h"
#include "KalieEvent.h"
#include "LuckyGameSystem.h"
#include "NpcServer.h"
#include "Protocol/src/SystemMessageIds.h"
#include "User.h"
#include "UserBroadcaster.h"
#include "UserSocket.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TextRuntime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <cwchar>
#include <cwctype>
#include <string>
#include <string_view>

namespace {

int CompareWideCaseInsensitive(const char16_t* left, const char16_t* right) {
#if defined(_WIN32)
    return ::_wcsicmp(Utf16ToWide(left).c_str(), Utf16ToWide(right).c_str());
#else
    while (*left != u'\0' && *right != u'\0') {
        const wint_t foldedLeft = std::towlower(*left);
        const wint_t foldedRight = std::towlower(*right);
        if (foldedLeft != foldedRight) {
            return foldedLeft < foldedRight ? -1 : 1;
        }
        ++left;
        ++right;
    }
    if (*left == *right) {
        return 0;
    }
    return *left == u'\0' ? -1 : 1;
#endif
}


bool ParseFirstInt(char16_t* command, std::int32_t& value) noexcept {
    if (command == nullptr) {
        return false;
    }

    while (*command == u' ') {
        ++command;
    }
    if (*command == u'\0') {
        return false;
    }

    const char16_t* end = command;
    while (*end != u'\0' && *end != u' ') {
        ++end;
    }

    // The current handlers use char16_t[256] and wcscpy_s before _wtoi.
    // An oversized token clears that destination and converts as zero.
    const std::size_t length = static_cast<std::size_t>(end - command);
    if (length < 256) {
        value = WindowsWtol(std::u16string_view(command, length));
    } else {
        runtime::process::HandleInvalidParameter(L"wcscpy_s");
        value = 0;
    }
    return true;
}

template <std::size_t Capacity>
std::size_t ParseWordList(
    char16_t* command, std::array<std::u16string, Capacity>& values, char16_t** const remainder = nullptr
) {
    if (command == nullptr) {
        if (remainder != nullptr) {
            *remainder = nullptr;
        }
        return 0;
    }

    std::size_t count = 0;
    while (count < Capacity) {
        while (*command == u' ') {
            ++command;
        }
        if (*command == u'\0') {
            break;
        }
        const char16_t* const begin = command;
        while (*command != u'\0' && *command != u' ') {
            ++command;
        }
        const std::size_t length = static_cast<std::size_t>(command - begin);
        if (length < 256) {
            values[count] = std::u16string(begin, length);
        } else {
            runtime::process::HandleInvalidParameter(L"wcscpy_s");
            values[count].clear();
        }
        ++count;
        if (*command == u' ') {
            ++command;
        }
    }
    if (remainder != nullptr) {
        *remainder = command;
    }
    return count;
}

int SignExtendLow16(const int value) noexcept {
    return static_cast<int>(runtime::Int16FromBits(static_cast<std::uint16_t>(value)));
}

int AddLow16Wrapping(const int value, const std::int16_t increment) noexcept {
    return static_cast<int>(
        runtime::AddInt16Wrapping(runtime::Int16FromBits(static_cast<std::uint16_t>(value)), increment)
    );
}

CEvent_World* GetWorldEvent() { return static_cast<CEvent_World*>(CEventManager::inst().GetEvent(GAME_EVENT::ET_WORLD)); }

void SendToUser(User* user, const char16_t* message) { user->SendSystemMessage(u"SYS", message); }

void SendUsage(CUserSocket* socket, const char16_t* message) {
    char16_t context[] = u"SYS";
    socket->SendSystemMessage(context, message);
}

bool ShowEventSetting(const GAME_EVENT::EVENT_TYPE type, User* user) {
    CEvent* const event = CEventManager::inst().GetEvent(type);
    if (event != nullptr) {
        event->ShowSetting(user);
    }
    return false;
}

bool ShowEventSettings(const GAME_EVENT::EVENT_TYPE type, User* user) {
    std::vector<CEvent*> events;
    CEventManager::inst().GetEvent(type, events);
    for (CEvent* const event : events) {
        if (event != nullptr) {
            event->ShowSetting(user);
        }
    }
    return false;
}

bool ReloadEventSetting(
    CUserSocket* socket, const GAME_EVENT::EVENT_TYPE type, const char16_t* successMessage, const char16_t* missingMessage
) {
    const int result = CEventManager::inst().Reload(type);
    if (result == 0) {
        SendUsage(socket, successMessage);
    } else if (result == -1) {
        SendUsage(socket, missingMessage);
    } else {
        char16_t message[1024]{};
        FormatUtf16To(message, std::size(message), u"total [%d] event fail to load", result);
        SendUsage(socket, message);
    }
    return false;
}

} // namespace

namespace BuilderCmdForEvent {

bool load_event(CUserSocket* socket, User*, char16_t*) {
    // Current L2Server 0x00578bb0.
    L2_GUARD_FUNCTION();
    return ReloadEventSetting(
        socket, GAME_EVENT::ET_DROP, u"success to load event", u"fail to load event. there is no drop event"
    );
}

bool load_xmas_santa_event_setting(CUserSocket* socket, User*, char16_t*) {
    // Current L2Server 0x00578cbc.
    L2_GUARD_FUNCTION();
    return ReloadEventSetting(
        socket, GAME_EVENT::ET_XMAS_SANTA, u"success to load event", u"fail to load event. there is no drop event"
    );
}

bool load_pccafe_event_setting(CUserSocket* socket, User*, char16_t*) {
    // Current L2Server 0x00578dcc.
    L2_GUARD_FUNCTION();
    return ReloadEventSetting(
        socket, GAME_EVENT::ET_PCCAFE_SETTING, u"success to load event", u"fail to load event. there is no drop event"
    );
}

bool load_vote_system_event_setting(CUserSocket* socket, User*, char16_t*) {
    // Current L2Server 0x00578edc.
    L2_GUARD_FUNCTION();
    return ReloadEventSetting(
        socket, GAME_EVENT::ET_VOTE_SYSTEM, u"success to load event", u"fail to load event. there is no drop event"
    );
}

bool load_commondrop_event(CUserSocket* socket, User*, char16_t*) {
    // Current L2Server 0x00578fec.
    L2_GUARD_FUNCTION();
    return ReloadEventSetting(
        socket, GAME_EVENT::ET_COMMONDROP_SYSTEM, u"success to load common drop event", u"fail to load common drop event."
    );
}

bool load_invitation_event(CUserSocket* socket, User*, char16_t*) {
    // Current L2Server 0x005790fc.
    L2_GUARD_FUNCTION();
    return ReloadEventSetting(
        socket, GAME_EVENT::ET_INVITATION, u"success to load event", u"fail to load event. there is no invitation event"
    );
}

bool load_recipe_success_rate_event(CUserSocket* socket, User*, char16_t*) {
    // Current L2Server 0x0057920c.
    L2_GUARD_FUNCTION();
    return ReloadEventSetting(
        socket, GAME_EVENT::ET_RECIPE_SUCCESS_RATE, u"success to load event",
        u"fail to load event. there is no RecipeSuccessRate event"
    );
}

bool show_recipe_success_rate(CUserSocket* socket, User*, char16_t* command) {
    // Current L2Server 0x00579ac4.
    L2_GUARD_FUNCTION();
    CEvent_RecipeSuccessRate* const event =
        static_cast<CEvent_RecipeSuccessRate*>(CEventManager::inst().GetEvent(GAME_EVENT::ET_RECIPE_SUCCESS_RATE));
    if (event == nullptr) {
        return false;
    }
    if (command == nullptr) {
        return false;
    }

    std::array<std::u16string, 1> words;
    if (ParseWordList(command, words) != 1) {
        SendUsage(socket, u"//show_recipe_success_rate [RecipeId]");
        return false;
    }

    const int recipeId = WindowsWtol(words[0]);
    const int rate = event->FindRecipeMap(recipeId);
    char16_t message[516]{};
    if (rate == -1) {
        FormatUtf16To(message, std::size(message), u"[RecipeId:%d] no data", recipeId);
    } else {
        FormatUtf16To(message, std::size(message), u"[RecipeId:%d] %d%%", recipeId, rate);
    }
    SendUsage(socket, message);
    return false;
}

bool load_coretime_teleport_event_setting(CUserSocket* socket, User*, char16_t*) {
    // Current L2Server 0x0057931c.
    L2_GUARD_FUNCTION();
    return ReloadEventSetting(
        socket, GAME_EVENT::ET_CORETIME_TELEPORT, u"success to load event", u"fail to load event. there is no drop event"
    );
}

bool show_eventsetting(CUserSocket*, User* user, char16_t*) {
    // Current L2Server 0x0057942c.
    L2_GUARD_FUNCTION();
    std::vector<CEvent*> events;
    CEventManager::inst().GetEvent(GAME_EVENT::ET_DROP, events);

    const int pickItemMessage = CEventManager::inst().m_nPickItemMsg;
    const int pickItemId = CEventManager::inst().m_nPickItemId;
    char16_t message[512]{};
    FormatUtf16To(message, std::size(message), u"BroadcastPickItem id:%d msg:%d", pickItemId, pickItemMessage);
    SendToUser(user, message);
    for (CEvent* const event : events) {
        if (event != nullptr) {
            event->ShowSetting(user);
        }
    }

    CEventManager::inst().GetEvent(GAME_EVENT::ET_KALIE, events);
    for (CEvent* const event : events) {
        if (event != nullptr) {
            event->ShowSetting(user);
        }
    }
    return false;
}

bool show_eventsetting_one(CUserSocket* socket, User* user, char16_t* command) {
    // Current L2Server 0x00579618.
    L2_GUARD_FUNCTION();
    if (command == nullptr) {
        return false;
    }
    std::array<std::u16string, 1> words;
    if (ParseWordList(command, words) != 1) {
        SendUsage(socket, u"//show_eventsetting_one event_name");
        return false;
    }

    CEvent* const event = CEventManager::inst().GetEvent(words[0]);
    if (event == nullptr) {
        SendUsage(socket, u"Invalid EventName");
    } else {
        event->ShowSetting(user);
    }
    return false;
}

bool load_pccafe_coupon_event_setting(CUserSocket* socket, User*, char16_t*) {
    // Current L2Server 0x00579fe4.
    L2_GUARD_FUNCTION();
    return ReloadEventSetting(
        socket, GAME_EVENT::ET_PCCAFE_COUPON_EVENT, u"success to load event", u"fail to load event. there is no drop event"
    );
}

bool check_pccafe_coupon(CUserSocket* socket, User*, char16_t* command) {
    // Current L2Server 0x0057a1b4.
    L2_GUARD_FUNCTION();
    if (command == nullptr) {
        return false;
    }
    std::array<std::u16string, 1> words;
    if (ParseWordList(command, words) != 1) {
        SendUsage(socket, u"//check_pccafe_coupon [coupon]");
        return false;
    }

    CEvent_PCCafeCoupon* const event =
        static_cast<CEvent_PCCafeCoupon*>(CEventManager::inst().GetEvent(GAME_EVENT::ET_PCCAFE_COUPON_EVENT));
    if (event == nullptr) {
        return false;
    }

    const CCouponKey key = event->Reversed(words[0].c_str());
    SendUsage(socket, event->Find(key) ? u"Usable Number" : u"Unusable Number");
    return false;
}

bool use_pccafe_coupon(CUserSocket* socket, User* user, char16_t* command) {
    // Current L2Server 0x00577280.
    L2_GUARD_FUNCTION();
    if (command == nullptr) {
        return false;
    }
    std::array<std::u16string, 1> words;
    if (ParseWordList(command, words) == 1) {
        user->RequestPCCafeCouponUse(words[0].c_str());
    } else {
        SendUsage(socket, u"//use_pccafe_coupon [coupon]");
    }
    return false;
}

bool xmas_event(CUserSocket* socket, User* user, char16_t* command) {
    // Current L2Server 0x0057a3bc.
    L2_GUARD_FUNCTION();
    if (command == nullptr) {
        return false;
    }
    std::array<std::u16string, 3> words;
    char16_t* remainder = nullptr;
    const std::size_t count = ParseWordList(command, words, &remainder);
    if (count == 0) {
        SendUsage(socket, u"//xmas_event command [params...]");
        return false;
    }
    if (std::char_traits<char16_t>::length(remainder) >= 512) {
        runtime::process::HandleInvalidParameter(L"wcscpy_s");
    }

    if (CompareWideCaseInsensitive(words[0].c_str(), u"santa") == 0) {
        if (count < 3) {
            SendUsage(socket, u"//xmas_event santa [buffid] [on/off]");
            return false;
        }
        CEvent_XmasSanta* const event =
            static_cast<CEvent_XmasSanta*>(CEventManager::inst().GetEvent(GAME_EVENT::ET_XMAS_SANTA));
        if (event == nullptr) {
            return false;
        }
        XmasSantaEventInfo* const info = event->GetEventInfo(WindowsWtol(words[1]));
        if (info == nullptr) {
            SendUsage(socket, u"invalid event buff info");
            return false;
        }
        if (CompareWideCaseInsensitive(words[2].c_str(), u"on") == 0) {
            runtime::raw_atomic::ExchangeI32(&info->m_nOnStartTurky, 0);
        } else {
            runtime::raw_atomic::ExchangeI32(&info->m_nOnStartTurky, 1);
            runtime::raw_atomic::ExchangeI32(&info->m_nOnStartSanta, 1);
        }
        SendUsage(user->GetSocket(), u"xmas_event santa is accepted.");
        return false;
    }

    if (CompareWideCaseInsensitive(words[0].c_str(), u"status") != 0) {
        return false;
    }
    if (count < 2) {
        SendUsage(socket, u"//xmas_event status [buffid]");
        return false;
    }
    const int buffId = WindowsWtol(words[1]);
    CEvent_XmasSanta* const event =
        static_cast<CEvent_XmasSanta*>(CEventManager::inst().GetEvent(GAME_EVENT::ET_XMAS_SANTA));
    if (event == nullptr) {
        return false;
    }
    XmasSantaEventInfo* const info = event->GetEventInfo(buffId);
    if (info == nullptr) {
        SendUsage(socket, u"nBuffId is invalid");
        return false;
    }

    char16_t message[512]{};
    FormatUtf16To(
        message, std::size(message), u"nBuffId:%d CanSpawnSanta:%d ", buffId,
        static_cast<int>(runtime::raw_atomic::LoadI32Relaxed(&info->m_nOnStartTurky) == 0)
    );
    SendUsage(user->GetSocket(), message);
    return false;
}

bool XmasTreeConfirm(CUserSocket*, User* user, char16_t*) {
    // Current L2Server 0x0057c72c.
    L2_GUARD_FUNCTION();
    CEvent_OneTimeEvent* const event =
        static_cast<CEvent_OneTimeEvent*>(CEventManager::inst().GetEvent(GAME_EVENT::ET_ONETIME));
    if (event == nullptr) {
        return false;
    }

    if (event->IsUserReceived(static_cast<int>(user->GetDBID())) == XmasWishCardResult::XWCR_UNKNOWN) {
        user->SendDB("chd", 0xea, 0x171, static_cast<int>(user->GetDBID()));
    }
    return false;
}

bool XmasTreeRegist(CUserSocket* socket, User* user, char16_t* command) {
    // Current L2Server 0x0057c830.
    L2_GUARD_FUNCTION();
    if (command == nullptr) {
        return false;
    }
    std::array<std::u16string, 1> words;
    if (ParseWordList(command, words) != 1) {
        SendUsage(socket, u"//xmas_regist wish_msg");
        return false;
    }

    CEvent_OneTimeEvent* const event =
        static_cast<CEvent_OneTimeEvent*>(CEventManager::inst().GetEvent(GAME_EVENT::ET_ONETIME));
    if (event == nullptr) {
        return false;
    }

    const XmasWishCardResult::Enum result = event->IsUserReceived(static_cast<int>(user->GetDBID()));
    if (result == XmasWishCardResult::XWCR_UNKNOWN) {
        if (static_cast<double>(user->m_inv.GetCarryingWeight()) / user->CarryWeight() < 0.8 &&
            user->m_inv.GetTotalCount() < user->GetInventoryLimit()) {
            const std::u16string wish = words[0];
            user->SendDB("chdS", 0xea, 0x170, static_cast<int>(user->GetDBID()), wish.c_str());
            event->SetItemGiveToUser(static_cast<int>(user->GetDBID()));
            return false;
        }
        // Current 0x0057ca9d repeats both capacity tests. If the state changes
        // after the failed first test, it reports the generic failure rather
        // than the inventory-capacity message.
        const bool hasCapacityOnSecondCheck =
            static_cast<double>(user->m_inv.GetCarryingWeight()) / user->CarryWeight() < 0.8 &&
            user->m_inv.GetTotalCount() < user->GetInventoryLimit();
        user->SendSystemMessage(
            hasCapacityOnSecondCheck ? SystemMessageId::WISH_REGISTRATION_FAILED
                                     : SystemMessageId::CANNOT_REGISTER_WISH_OR_RECEIVE_REWARD_INVENTORY_RESTRICTION
        );
        return false;
    }

    user->SendSystemMessage(
        result == XmasWishCardResult::XWCR_ALREADY_RECEIVED ? SystemMessageId::WISH_ALREADY_REGISTERED_AND_REWARDED
                                                            : SystemMessageId::WISH_REGISTRATION_FAILED
    );
    return false;
}

bool daynight_event(CUserSocket* socket, User*, char16_t* command) {
    // Current L2Server 0x0057c0b4.
    L2_GUARD_FUNCTION();
    if (command == nullptr) {
        return false;
    }
    std::array<std::u16string, 1> words;
    if (ParseWordList(command, words) != 1) {
        SendUsage(socket, u"//daynight_event [On:1/Off:0]");
        return false;
    }

    const int enabled = WindowsWtol(words[0]);
    if (enabled != 0 && enabled != 1) {
        SendUsage(socket, u"//daynight_event [On:1/Off:0]");
        return false;
    }

    CEvent_AgathionDrop* const event =
        static_cast<CEvent_AgathionDrop*>(CEventManager::inst().GetEvent(GAME_EVENT::ET_AGATHION));
    event->SetForceReachGoal(enabled == 1);
    const bool conqueredForState = event->IsConquested();
    CUserBroadcaster::inst().BroadcastToAllUser("chh", 0xfe, 0x117, static_cast<int>(!conqueredForState));
    // Current 0x0057c2ac reads the event state again before selecting the
    // second packet's system-message ID.
    const bool conqueredForMessage = event->IsConquested();
    CUserBroadcaster::inst().BroadcastToAllUser(
        "chdddddddddddSSSSS", 0xfe, 0x39, 0, conqueredForMessage ? 0xe83 : 0xe82, 2, 0, 0, 0, 1, 0, 5000, 0, -1, u"", u"",
        u"", u"", u""
    );
    return false;
}

bool tersievent(CUserSocket* socket, User*, char16_t* command) {
    // Current L2Server 0x0057c3f0.
    L2_GUARD_FUNCTION();
    if (command == nullptr) {
        return false;
    }
    std::array<std::u16string, 1> words;
    if (ParseWordList(command, words) != 1) {
        SendUsage(socket, u"//tersievent number");
        return false;
    }

    CEvent_AgathionDrop* const event =
        static_cast<CEvent_AgathionDrop*>(CEventManager::inst().GetEvent(GAME_EVENT::ET_AGATHION));
    SharedEventInfo::Inst()->m_gameEvent.m_AgathionCount = WindowsWtol(words[0]);
    event->SetForceReachGoal(event->IsForceReachGoal());
    const bool conquered = event->IsConquested();
    CUserBroadcaster::inst().BroadcastToAllUser("chh", 0xfe, 0x117, static_cast<int>(!conquered));
    CUserBroadcaster::inst().BroadcastToAllUser(
        "chdddddddddddSSSSS", 0xfe, 0x39, 0, conquered ? 0xe83 : 0xe82, 2, 0, 0, 0, 1, 0, 5000, 0, -1, u"", u"", u"", u"",
        u""
    );
    return false;
}

bool load_economy_control_event(CUserSocket* socket, User*, char16_t*) {
    // Current L2Server 0x0057cc50.
    L2_GUARD_FUNCTION();
    return ReloadEventSetting(socket, GAME_EVENT::ET_ECONOMY_CONTROL, u"success to load event", u"fail to load event.");
}

bool load_lucky_game_event(CUserSocket* socket, User*, char16_t*) {
    // Current L2Server 0x0057ce20.
    L2_GUARD_FUNCTION();
    if (socket == nullptr) {
        return false;
    }
    return ReloadEventSetting(socket, GAME_EVENT::ET_LUCKY_GAME, u"success to load event", u"fail to load event.");
}

bool set_lucky_game(CUserSocket* socket, User* user, char16_t* command) {
    // Current L2Server 0x0057cf38.
    L2_GUARD_FUNCTION();
    if (socket == nullptr || user == nullptr) {
        return false;
    }

    auto* const event = static_cast<CEvent_LuckyGameEvent*>(CEventManager::inst().GetEvent(GAME_EVENT::ET_LUCKY_GAME));
    if (event == nullptr) {
        SendUsage(socket, u"LuckyGameEvent not exist.");
        return false;
    }
    if (command == nullptr) {
        return false;
    }

    std::array<std::u16string, 3> words;
    const std::size_t wordCount = ParseWordList(command, words);
    if (wordCount < 2) {
        SendUsage(socket, u"//set_lucky_game [param] value");
        return false;
    }

    if (CompareWideCaseInsensitive(words[0].c_str(), u"common_index") == 0) {
        const int value = WindowsWtol(words[1]);
        if (g_CLuckyGameDB.GetReward(value) == nullptr) {
            char16_t message[256]{};
            FormatUtf16To(message, std::size(message), u"index[%d] not exist.", value);
            SendUsage(socket, message);
            return false;
        }
        event->SetCommonIndex(value);
        SendUsage(socket, u"CommonIndex Setting Success.");
        return false;
    }

    if (CompareWideCaseInsensitive(words[0].c_str(), u"premium_index") == 0) {
        const int value = WindowsWtol(words[1]);
        if (g_CLuckyGameDB.GetReward(value) == nullptr) {
            char16_t message[256]{};
            FormatUtf16To(message, std::size(message), u"index[%d] not exist.", value);
            SendUsage(socket, message);
            return false;
        }
        event->SetPremiumIndex(value);
        SendUsage(socket, u"PremiumIndex Setting Success.");
        return false;
    }

    if (CompareWideCaseInsensitive(words[0].c_str(), u"common_turn") == 0) {
        const int value = WindowsWtol(words[1]);
        CLuckyGameSystem::Inst()->SetCurrentTurnCnt(LuckyGameSystem::BT_COMMON, value);
        SendUsage(socket, u"CommonGame Turn Setting Success.");
        return false;
    }

    if (CompareWideCaseInsensitive(words[0].c_str(), u"premium_turn") == 0) {
        const int value = WindowsWtol(words[1]);
        CLuckyGameSystem::Inst()->SetCurrentTurnCnt(LuckyGameSystem::BT_PREMIUM, value);
        SendUsage(socket, u"PremiumGame Turn Setting Success.");
        return false;
    }

    if (CompareWideCaseInsensitive(words[0].c_str(), u"personal_turn") != 0) {
        SendUsage(socket, u"//set_lucky_game undefined param");
        return false;
    }

    CObject* const target = user->m_spTarget.GetObjectW();
    if (target == nullptr) {
        SendUsage(socket, u"Target is invalid");
        return false;
    }
    if (!target->IsPC()) {
        SendUsage(socket, u"Target is not PC");
        return false;
    }
    if (wordCount < 3) {
        SendUsage(socket, u"//set_lucky_game personal_turn [index] [turn]");
        return false;
    }

    const int index = WindowsWtol(words[1]);
    if (index < 0) {
        SendUsage(socket, u"[index] is not positive number.");
        return false;
    }
    const int turn = WindowsWtol(words[2]);
    if (turn < 0) {
        SendUsage(socket, u"[turn] is not positive number.");
        return false;
    }

    // The current body validates the selected PC, but writes the builder's
    // own key instead of the selected target's key.
    CLuckyGameSystem::Inst()->SetPersonalPoint(user, index, turn, false);
    SendUsage(socket, u"Personal Turn Setting Success.");
    return false;
}

bool show_lucky_game_start(CUserSocket* socket, User* user, char16_t* command) {
    // Current L2Server 0x0057d4d0.
    L2_GUARD_FUNCTION();
    if (socket == nullptr || user == nullptr) {
        return false;
    }
    if (CEventManager::inst().GetEvent(GAME_EVENT::ET_LUCKY_GAME) == nullptr) {
        SendUsage(socket, u"LuckyGameEvent not exist.");
        return false;
    }
    if (command == nullptr) {
        return false;
    }

    std::array<std::u16string, 1> words;
    if (ParseWordList(command, words) != 1) {
        SendUsage(socket, u"//show_lucky_game_start [param]");
        return false;
    }

    if (CompareWideCaseInsensitive(words[0].c_str(), u"common") == 0) {
        user->SendStartLuckyGame(LuckyGameSystem::BT_COMMON);
    } else if (CompareWideCaseInsensitive(words[0].c_str(), u"premium") == 0) {
        user->SendStartLuckyGame(LuckyGameSystem::BT_PREMIUM);
    } else {
        SendUsage(socket, u"//show_lucky_game_start undefined param");
    }
    return false;
}

bool Test10YearEvent(CUserSocket* socket, User*, char16_t* command) {
    // Current L2Server 0x0057d720.
    L2_GUARD_FUNCTION();
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return false;
    }
    if (command == nullptr) {
        return false;
    }

    std::array<std::u16string, 2> words;
    if (ParseWordList(command, words) != 2) {
        SendUsage(socket, u"//yearevent reward|timeset number");
        return false;
    }

    if (CompareWideCaseInsensitive(words[0].c_str(), u"reward") == 0) {
        const int value = WindowsWtol(words[1]);
        auto* const event = static_cast<CEvent_AccountEvent*>(CEventManager::inst().GetEvent(GAME_EVENT::ET_ACCOUNT_EVENT));
        if (event == nullptr) {
            return false;
        }
        if (!event->InEventTime()) {
            return false;
        }
        const auto found = event->m_mapAttendReward.find(value);
        if (found != event->m_mapAttendReward.end()) {
            user->GiveItemOnAccount(
                0, 0, 0, found->second.nItemClassId, found->second.nAmount, 0, value, const_cast<wchar_t*>(L"NoEvent"), 0, 0
            );
        }
    } else if (CompareWideCaseInsensitive(words[0].c_str(), u"timeset") == 0) {
        const int value = WindowsWtol(words[1]);
        auto* const event = static_cast<CEvent_AccountEvent*>(CEventManager::inst().GetEvent(GAME_EVENT::ET_ACCOUNT_EVENT));
        if (event != nullptr) {
            event->m_nResetInterval = value;
        }
    }
    return false;
}

bool Test10YearEventStamp(CUserSocket* socket, User*, char16_t*) {
    // Current L2Server 0x0057da2c.
    L2_GUARD_FUNCTION();
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return false;
    }
    CEvent* const event = CEventManager::inst().GetEvent(GAME_EVENT::ET_ACCOUNT_EVENT);
    if (event != nullptr && event->InEventTime()) {
        user->RequestAccountStamp(0, true, false);
    }
    return false;
}

bool KalieStart(CUserSocket*, User*, char16_t*) {
    // Current L2Server 0x00576a5c.
    L2_GUARD_FUNCTION();
    KalieEvent::Inst().KalieEventStart();
    return false;
}

bool KalieUIOn(CUserSocket*, User* user, char16_t*) {
    // Current L2Server 0x00576af8.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        KalieEvent::Inst().KalieUIOn(*user);
    }
    return false;
}

bool KalieUIOff(CUserSocket*, User* user, char16_t*) {
    // Current L2Server 0x00576bac.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        KalieEvent::Inst().KalieUIOff(*user);
    }
    return false;
}

bool FreeKalieOn(CUserSocket*, User*, char16_t*) {
    // Current L2Server 0x00576c60.
    L2_GUARD_FUNCTION();
    KalieEvent::Inst().FreeKalieOn();
    return false;
}

bool FreeKalieOff(CUserSocket*, User*, char16_t*) {
    // Current L2Server 0x00576cfc.
    L2_GUARD_FUNCTION();
    KalieEvent::Inst().FreeKalieOff();
    return false;
}

bool MakeJackPot_event(CUserSocket*, User*, char16_t*) {
    // Current L2Server 0x00576d98.
    L2_GUARD_FUNCTION();
    KalieEvent::Inst().SetRewardValForBuilder();
    return false;
}

bool NextEventProgress(CUserSocket*, User*, char16_t*) {
    // Current L2Server 0x00576e34.
    L2_GUARD_FUNCTION();
    KalieEvent::Inst().IncreaseProgressForbuild();
    return false;
}

bool ResetKalie(CUserSocket*, User*, char16_t*) {
    // Current L2Server 0x00576ed0.
    L2_GUARD_FUNCTION();
    KalieEvent::Inst().ResetEventForBuild();
    return false;
}

bool SetEventState(CUserSocket* socket, User*, char16_t* command) {
    // Current L2Server 0x00577d64.
    L2_GUARD_FUNCTION();
    if (command == nullptr) {
        return false;
    }
    std::int32_t state = 0;
    if (!ParseFirstInt(command, state)) {
        SendUsage(socket, u"//kalie_state number");
        return false;
    }

    KalieEvent::Inst().SetStateForBuilder(state);
    return false;
}

bool attendance_reward_reset(CUserSocket* socket, User*, char16_t*) {
    // Current L2Server 0x00576f6c.
    L2_GUARD_FUNCTION();
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->RequestDeleteAccountStampLogBuilder(0);
    }
    return false;
}

bool attendance_stamp(CUserSocket* socket, User*, char16_t* command) {
    // Current L2Server 0x00577f28.
    L2_GUARD_FUNCTION();
    if (command == nullptr) {
        return false;
    }
    std::int32_t count = 0;
    if (!ParseFirstInt(command, count)) {
        SendUsage(socket, u"//attendance_stamp [stamp count]");
        return false;
    }

    User* const builder = socket->GetUser();
    if (builder == nullptr) {
        return false;
    }
    CObject* const target = builder->m_spTarget.GetObjectW();
    if (target != nullptr && target->IsPC()) {
        User* const targetUser = target->CastUser();
        if (targetUser != nullptr) {
            targetUser->RequestAccountStampBuilder(0, count);
        }
    }
    return false;
}

bool set_ink_energy(CUserSocket* socket, User* user, char16_t* command) {
    // Current L2Server 0x00577050.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    if (command == nullptr) {
        return false;
    }

    std::int32_t value = 0;
    if (!ParseFirstInt(command, value)) {
        SendUsage(socket, u"//set_ink_energy [value]");
        return false;
    }

    CObject* const target = user->m_spTarget.GetObjectW();
    if (target == nullptr) {
        SendUsage(socket, u"Target is invalid");
        return false;
    }
    if (!target->IsPC()) {
        SendUsage(socket, u"//Target is not PC");
        return false;
    }

    User* const targetUser = target->CastUser();
    if (targetUser != nullptr) {
        targetUser->UpdateInkEnergy(value, true);
    }
    return false;
}

bool setvotesystem(CUserSocket* socket, User* user, char16_t* command) {
    // Current L2Server 0x00577a2c.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    if (command == nullptr) {
        return false;
    }

    std::array<std::u16string, 2> words;
    if (ParseWordList(command, words) != 2) {
        SendUsage(socket, u"//setvotesystem [votecount/bonustimelimit/bonuscount] value");
        return false;
    }

    CObject* const target = user->m_spTarget.GetObjectW();
    if (target == nullptr) {
        SendUsage(socket, u"Target is invalid");
        return false;
    }
    if (!target->IsPC()) {
        SendUsage(socket, u"//Target is not PC");
        return false;
    }

    const int value = WindowsWtol(words[1]);
    if (CompareWideCaseInsensitive(words[0].c_str(), u"votecount") == 0) {
        target->CastUser()->SetVoteCount(value);
    } else if (CompareWideCaseInsensitive(words[0].c_str(), u"bonustimelimit") == 0) {
        target->CastUser()->SetBonusTimeLimit(value);
    } else if (CompareWideCaseInsensitive(words[0].c_str(), u"bonuscount") == 0) {
        target->CastUser()->SetBonusCount(value);
    } else {
        SendUsage(socket, u"//setvotesystem [votecount/bonustimelimit/bonusrate] value");
    }

    target->CastUser()->CalExpBonus();
    target->CastUser()->SendVoteSystemInfoPacket();
    target->CastUser()->SetRelationChanged(false);
    return false;
}

bool showvotesystem(CUserSocket* socket, User* user, char16_t*) {
    // Current L2Server 0x005767b0.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }

    CObject* const target = user->m_spTarget.GetObjectW();
    if (target == nullptr) {
        SendUsage(socket, u"Target is invalid");
        return false;
    }
    if (!target->IsPC()) {
        SendUsage(socket, u"//Target is not PC");
        return false;
    }

    // The current MSVC image evaluates the eleven format arguments from
    // right to left and performs CastUser() separately for every lane.
    const int second = target->CastUser()->GetLastInitTime()->wSecond;
    const int minute = target->CastUser()->GetLastInitTime()->wMinute;
    const int hour = target->CastUser()->GetLastInitTime()->wHour;
    const int day = target->CastUser()->GetLastInitTime()->wDay;
    const int month = target->CastUser()->GetLastInitTime()->wMonth;
    const int year = target->CastUser()->GetLastInitTime()->wYear;
    const int playTime = target->CastUser()->GetPlayTime();
    const int bonusTimeLimit = target->CastUser()->GetBonusTimeLimit();
    const int playTimeCount = target->CastUser()->GetPlayTimeCount();
    const int bonusCount = target->CastUser()->GetBonusCount();
    const int voteCount = target->CastUser()->GetVoteCount();
    char16_t message[2044]{};
    FormatUtf16To(
        message, std::size(message),
        u"Target "
        u"VoteCount[%d]/BonusCount[%d]/PlayTimeCount[%d]/BonusTimeLimit[%d]/PlayTime[%d]/InitialTime[%d-%d-%d-%d-%d-%d]",
        voteCount, bonusCount, playTimeCount, bonusTimeLimit, playTime, year, month, day, hour, minute, second
    );
    SendUsage(socket, message);
    return false;
}

bool event_debug_info(CUserSocket*, User* user, char16_t*) {
    // Current L2Server 0x005776cc.
    L2_GUARD_FUNCTION();
    for (std::int32_t index = 0; index < 10; ++index) {
        char16_t message[256]{};
        FormatUtf16To(
            message, std::size(message), u"event info[%d] = %d", index,
            SharedEventInfo::Inst()->m_gameEvent.m_DebugInfo[index]
        );
        user->SendSystemMessage(u"SYS", message);
    }
    return false;
}

bool set_event_value(CUserSocket* socket, User* user, char16_t* command) {
    // Current L2Server 0x005777dc.
    L2_GUARD_FUNCTION();
    if (command == nullptr) {
        return false;
    }
    std::array<std::u16string, 2> words;
    if (ParseWordList(command, words) != 2) {
        SendUsage(socket, u"//set_event_value [0~4] [value]");
        return false;
    }

    const int eventIndex = static_cast<int>(runtime::Int8FromBits(static_cast<std::uint8_t>(WindowsWtol(words[0]))));
    const int value = WindowsWtol(words[1]);
    g_npcserver.Send("ccd", 0xa6, eventIndex, value);
    char16_t message[256]{};
    FormatUtf16To(message, std::size(message), u"set_event_value ok event_value[%d] = %d", eventIndex, value);
    user->SendSystemMessage(u"SYS", message);
    return false;
}

bool set_hallo_event_value(CUserSocket* socket, User* user, char16_t* command) {
    // Current L2Server 0x00577448.
    L2_GUARD_FUNCTION();
    if (command == nullptr) {
        return false;
    }
    std::array<std::u16string, 5> words;
    if (ParseWordList(command, words) != 5) {
        SendUsage(socket, u"//set_hallo_event_value v1 v2 v3 v4 v5");
        return false;
    }
    std::array<std::int32_t, 5> values{};
    for (std::size_t index = 0; index < values.size(); ++index) {
        values[index] = WindowsWtol(words[index]);
    }

    user->SendSystemMessage(u"SYS", u"set_hallo_event_value ok");
    g_npcserver.Send("ccdcdcdcdcd", 0xa6, 1, values[0], 1, values[1], 1, values[2], 1, values[3], 1, values[4]);
    return false;
}

bool hallo_event_ranking_time(CUserSocket* socket, User* user, char16_t* command) {
    // Current L2Server 0x0057a760.
    L2_GUARD_FUNCTION();
    if (command == nullptr) {
        return false;
    }
    std::array<std::u16string, 1> words;
    if (ParseWordList(command, words) != 1) {
        SendUsage(socket, u"//hallo_event_ranking_time [show/check]");
        return false;
    }

    CEvent_World* const event = GetWorldEvent();
    if (event == nullptr) {
        SendToUser(user, u"set_hallo_event_ranking_time fail, <WorldEvent> doesn't exist int eventdata.int");
        return false;
    }

    const CEvent_World::EventStateInfo* const state = event->GetEventStateInfo();
    if (CompareWideCaseInsensitive(words[0].c_str(), u"show") == 0) {
        const std::int32_t now = runtime::crt::Time32Now();
        const int round = event->GetRoundNo(now);
        const std::tm* const local = runtime::crt::LocalTime32(state->currentCheckTime);
        char16_t message[256]{};
        FormatUtf16To(
            message, std::size(message), u"hallo_event_ranking_time = %d/%d/%d %02d:%02d, roundNo = %d",
            AddLow16Wrapping(local->tm_year, 1900), AddLow16Wrapping(local->tm_mon, 1), SignExtendLow16(local->tm_mday),
            SignExtendLow16(local->tm_hour), SignExtendLow16(local->tm_min), round
        );
        SendToUser(user, message);
    } else if (CompareWideCaseInsensitive(words[0].c_str(), u"check") == 0) {
        const std::int32_t now = runtime::crt::Time32Now();
        db.RequestUpdateEventRanking(state->eventId, event->GetRoundNo(now));
        SendToUser(user, u"hallo_event_ranking_time check ok");
    } else {
        SendToUser(user, u"hallo_event_ranking_time fail, //hallo_event_ranking_time [show/check]");
    }
    return false;
}

bool hallo_event_ranking_score(CUserSocket* socket, User* user, char16_t* command) {
    // Current L2Server 0x0057aaac.
    L2_GUARD_FUNCTION();
    CEvent_World* const event = GetWorldEvent();
    if (event == nullptr) {
        SendToUser(user, u"set_hallo_event_ranking_time fail, <WorldEvent> doesn't exist int eventdata.int");
        return false;
    }
    if (command == nullptr) {
        return false;
    }

    std::array<std::u16string, 3> words;
    const std::size_t count = ParseWordList(command, words);
    if (count == 0) {
        SendUsage(socket, u"//hallo_event_ranking_score [get/set]");
        return false;
    }

    const CEvent_World::EventStateInfo* const state = event->GetEventStateInfo();
    if (CompareWideCaseInsensitive(words[0].c_str(), u"get") == 0) {
        if (count < 2) {
            SendToUser(user, u"hallo_event_ranking_score get fail, //hallo_event_ranking_score get [charname]");
            return false;
        }
        const std::int32_t now = runtime::crt::Time32Now();
        const int databaseId = static_cast<int>(user->GetDBID());
        const int round = event->GetRoundNo(now);
        db.RequestGetEventScore(state->eventId, round, databaseId, words[1].c_str());
        return false;
    }

    if (CompareWideCaseInsensitive(words[0].c_str(), u"set") == 0) {
        if (count < 3) {
            SendToUser(user, u"hallo_event_ranking_score set fail, //hallo_event_ranking_score set [charname] [score]");
            return false;
        }
        char16_t characterName[25]{};
        if (words[1].size() < std::size(characterName)) {
            std::char_traits<char16_t>::copy(characterName, words[1].data(), words[1].size());
        } else {
            runtime::process::HandleInvalidParameter(L"wcscpy_s");
        }
        const int score = WindowsWtol(words[2]);
        SendToUser(user, u"hallo_event_ranking_score set ok");
        const std::int32_t now = runtime::crt::Time32Now();
        const int round = event->GetRoundNo(now);
        db.RequestInsertEventRanking(state->eventId, round, characterName, score);
        return false;
    }

    SendToUser(user, u"hallo_event_ranking_score fail, //hallo_event_ranking_score [get/set]");
    return false;
}

bool show_hallo_top_rankers(CUserSocket* socket, User* user, char16_t* command) {
    // Current L2Server 0x0057adfc.
    L2_GUARD_FUNCTION();
    if (command == nullptr) {
        return false;
    }
    std::int32_t dayBefore = 0;
    if (!ParseFirstInt(command, dayBefore)) {
        SendUsage(socket, u"//show_hallo_top_rankers [daybefore]");
        return false;
    }

    CEvent_World* const event = GetWorldEvent();
    if (event == nullptr) {
        return false;
    }
    const int round = event->GetRoundNo(runtime::crt::Time32Now());
    db.RequestLoadEventTopRankers(
        static_cast<int>(user->GetDBID()), 0x01329097, runtime::SubtractInt32Wrapping(round, dayBefore)
    );
    return false;
}

bool set_bless_day(CUserSocket* socket, User* user, char16_t* command) {
    // Current L2Server 0x0057b00c.
    L2_GUARD_FUNCTION();
    if (command == nullptr) {
        return false;
    }
    std::array<std::u16string, 1> words;
    if (ParseWordList(command, words) != 1) {
        SendUsage(socket, u"//setblessday 1~10000");
        return false;
    }

    CEvent_World* const event = GetWorldEvent();
    if (event == nullptr) {
        SendToUser(user, u"World Event doesn't exist, check eventdata.xml");
        return false;
    }
    const int day = WindowsWtol(words[0]);
    event->SetEventDay(day);
    char16_t message[256]{};
    FormatUtf16To(message, std::size(message), u"setblessday ok. day = %d", day);
    SendToUser(user, message);
    return false;
}

bool set_bless_off(CUserSocket*, User* user, char16_t*) {
    // Current L2Server 0x0057b268.
    L2_GUARD_FUNCTION();
    CEvent_Buff* const event = static_cast<CEvent_Buff*>(CEventManager::inst().GetEvent(GAME_EVENT::ET_BUFF));
    if (event == nullptr) {
        SendToUser(user, u"Buff Event doesn't exist, check eventdata.xml");
    } else if (!event->GetBuffEventInfo()->state) {
        SendToUser(user, u"setblessoff fail. bless state is not on");
    } else {
        event->SetBuffEventState(false);
        SendToUser(user, u"setblessoff ok");
    }
    return false;
}

bool set_bless_time(CUserSocket* socket, User* user, char16_t* command) {
    // Current L2Server 0x0057b384.
    L2_GUARD_FUNCTION();
    if (command == nullptr) {
        return false;
    }
    std::array<std::u16string, 1> words;
    if (ParseWordList(command, words) != 1) {
        SendUsage(socket, u"//setblesstime [1~999]");
        return false;
    }

    CEvent_Buff* const event = static_cast<CEvent_Buff*>(CEventManager::inst().GetEvent(GAME_EVENT::ET_BUFF));
    if (event == nullptr) {
        SendToUser(user, u"Buff Event doesn't exist, check eventdata.xml");
    } else {
        const int minutes = WindowsWtol(words[0]);
        if (static_cast<std::uint32_t>(minutes) - 1U < 999U) {
            event->SetBuffEventDuration(minutes * 60);
            SendToUser(user, u"setblesstime ok");
        } else {
            SendToUser(user, u"use //setblesstime [1~999]");
        }
    }
    return false;
}

bool event_ranking_time(CUserSocket* socket, User* user, char16_t* command) {
    // Current L2Server 0x0057b5c8.
    L2_GUARD_FUNCTION();
    if (command == nullptr) {
        return false;
    }
    std::array<std::u16string, 2> words;
    const std::size_t count = ParseWordList(command, words);
    if (count == 0) {
        SendUsage(socket, u"event_ranking_time fail, //event_ranking_time [show/check]");
        return false;
    }

    CEvent_World* const event = GetWorldEvent();
    if (event == nullptr) {
        SendToUser(user, u"set_event_ranking_time fail, <WorldEvent> doesn't exist int eventdata.int");
        return false;
    }
    const CEvent_World::EventStateInfo* const state = event->GetEventStateInfo();
    if (CompareWideCaseInsensitive(words[0].c_str(), u"show") == 0) {
        const std::int32_t now = runtime::crt::Time32Now();
        const int round = event->GetRoundNo(now);
        const std::tm* const local = runtime::crt::LocalTime32(state->currentCheckTime);
        char16_t message[256]{};
        FormatUtf16To(
            message, std::size(message), u"event_ranking_time = %d/%d/%d %02d:%02d, roundNo = %d",
            AddLow16Wrapping(local->tm_year, 1900), AddLow16Wrapping(local->tm_mon, 1), SignExtendLow16(local->tm_mday),
            SignExtendLow16(local->tm_hour), SignExtendLow16(local->tm_min), round
        );
        SendToUser(user, message);
    } else if (CompareWideCaseInsensitive(words[0].c_str(), u"check") == 0) {
        if (count < 2) {
            SendToUser(user, u"event_ranking_time check fail, //event_ranking_time check [round]");
            return false;
        }
        db.RequestUpdateEventRanking(state->eventId, WindowsWtol(words[1]));
        SendToUser(user, u"event_ranking_time check ok");
    } else {
        SendToUser(user, u"event_ranking_time fail, //event_ranking_time [show/check]");
    }
    return false;
}

bool event_ranking_score(CUserSocket* socket, User* user, char16_t* command) {
    // Current L2Server 0x0057b928.
    L2_GUARD_FUNCTION();
    CEvent_World* const event = GetWorldEvent();
    if (event == nullptr) {
        SendToUser(user, u"set_event_ranking_time fail, <WorldEvent> doesn't exist int eventdata.int");
        return false;
    }
    if (command == nullptr) {
        return false;
    }

    std::array<std::u16string, 3> words;
    const std::size_t count = ParseWordList(command, words);
    if (count == 0) {
        SendUsage(socket, u"event_ranking_score fail, //event_ranking_score [get/set]");
        return false;
    }

    const CEvent_World::EventStateInfo* const state = event->GetEventStateInfo();
    if (CompareWideCaseInsensitive(words[0].c_str(), u"get") == 0) {
        if (count < 2) {
            SendToUser(user, u"event_ranking_score get fail, //event_ranking_score get [charname]");
            return false;
        }
        const std::int32_t now = runtime::crt::Time32Now();
        const int databaseId = static_cast<int>(user->GetDBID());
        const int round = event->GetRoundNo(now);
        db.RequestGetEventScore(state->eventId, round, databaseId, words[1].c_str());
        return false;
    }

    if (CompareWideCaseInsensitive(words[0].c_str(), u"set") == 0) {
        if (count < 3) {
            SendToUser(user, u"event_ranking_score set fail, //event_ranking_score set [charname] [score]");
            return false;
        }
        char16_t characterName[25]{};
        if (words[1].size() < std::size(characterName)) {
            std::char_traits<char16_t>::copy(characterName, words[1].data(), words[1].size());
        } else {
            runtime::process::HandleInvalidParameter(L"wcscpy_s");
        }
        const int score = WindowsWtol(words[2]);
        SendToUser(user, u"event_ranking_score set ok");
        const std::int32_t now = runtime::crt::Time32Now();
        const int round = event->GetRoundNo(now);
        db.RequestInsertEventRanking(state->eventId, round, characterName, score);
        return false;
    }

    SendToUser(user, u"event_ranking_score fail, //event_ranking_score [get/set]");
    return false;
}

bool set_event_enable(CUserSocket* socket, User*, char16_t* command) {
    // Current L2Server 0x0057bc78.
    L2_GUARD_FUNCTION();
    if (command == nullptr) {
        return false;
    }
    std::array<std::u16string, 2> words;
    if (ParseWordList(command, words) != 2) {
        SendUsage(socket, u"set_event_enable event_type [on|off]");
        return false;
    }

    const auto type = static_cast<GAME_EVENT::EVENT_TYPE>(WindowsWtol(words[0]));
    const bool enabled = CompareWideCaseInsensitive(words[1].c_str(), u"on") == 0;
    SendUsage(
        socket, CEventManager::inst().SetEventEnable(type, enabled) ? u"command is accepted." : u"command is not accepted."
    );
    return false;
}

bool set_event_enable_by_name(CUserSocket* socket, User*, char16_t* command) {
    // Current L2Server 0x0057be7c.
    L2_GUARD_FUNCTION();
    if (command == nullptr) {
        return false;
    }
    std::array<std::u16string, 2> words;
    if (ParseWordList(command, words) != 2) {
        SendUsage(socket, u"set_event_enable_by_name event_name [on|off]");
        return false;
    }

    const bool enabled = CompareWideCaseInsensitive(words[1].c_str(), u"on") == 0;
    const std::u16string eventName = words[0];
    SendUsage(
        socket,
        CEventManager::inst().SetEventEnable(eventName, enabled) ? u"command is accepted." : u"command is not accepted."
    );
    return false;
}

bool show_pccafe_event_setting(CUserSocket*, User* user, char16_t*) {
    // Current L2Server 0x00579884.
    L2_GUARD_FUNCTION();
    return ShowEventSetting(GAME_EVENT::ET_PCCAFE_SETTING, user);
}

bool show_invitation_setting(CUserSocket*, User* user, char16_t*) {
    // Current L2Server 0x00579944.
    L2_GUARD_FUNCTION();
    return ShowEventSetting(GAME_EVENT::ET_INVITATION, user);
}

bool show_recipe_success_rate_setting(CUserSocket*, User* user, char16_t*) {
    // Current L2Server 0x00579a04.
    L2_GUARD_FUNCTION();
    return ShowEventSetting(GAME_EVENT::ET_RECIPE_SUCCESS_RATE, user);
}

bool show_vote_system_event_setting(CUserSocket*, User* user, char16_t*) {
    // Current L2Server 0x00579d3c.
    L2_GUARD_FUNCTION();
    return ShowEventSetting(GAME_EVENT::ET_VOTE_SYSTEM, user);
}

bool show_coretime_teleport_event_setting(CUserSocket*, User* user, char16_t*) {
    // Current L2Server 0x00579dfc.
    L2_GUARD_FUNCTION();
    return ShowEventSetting(GAME_EVENT::ET_CORETIME_TELEPORT, user);
}

bool show_commondrop_setting(CUserSocket*, User* user, char16_t*) {
    // Current L2Server 0x00579ebc.
    L2_GUARD_FUNCTION();
    return ShowEventSettings(GAME_EVENT::ET_COMMONDROP_SYSTEM, user);
}

bool show_pccafe_coupon_event_setting(CUserSocket*, User* user, char16_t*) {
    // Current L2Server 0x0057a0f4.
    L2_GUARD_FUNCTION();
    return ShowEventSetting(GAME_EVENT::ET_PCCAFE_COUPON_EVENT, user);
}

bool show_pccafe_coupon_ui(CUserSocket* socket, User*, char16_t*) {
    // Current L2Server 0x005766f4.
    L2_GUARD_FUNCTION();
    socket->Send("ch", 0xfe, 0x45);
    return false;
}

bool show_economy_control_event_setting(CUserSocket*, User* user, char16_t*) {
    // Current L2Server 0x0057cb40.
    L2_GUARD_FUNCTION();
    return ShowEventSettings(GAME_EVENT::ET_ECONOMY_CONTROL, user);
}

bool show_lucky_game_event_setting(CUserSocket*, User* user, char16_t*) {
    // Current L2Server 0x0057cd60.
    L2_GUARD_FUNCTION();
    return ShowEventSetting(GAME_EVENT::ET_LUCKY_GAME, user);
}

bool BalthusUI(CUserSocket* socket, User* user, char16_t* command) {
    // Current L2Server 0x0057840c.
    L2_GUARD_FUNCTION();
    if (command == nullptr) {
        return false;
    }
    std::int32_t value = 0;
    if (!ParseFirstInt(command, value)) {
        SendUsage(socket, u"//balthus_ui value[0:UI_OFF, 1:UI_ON, ELSE:NO_CHANGE]");
        return false;
    }

    GAME_EVENT::BALTHUS_EVENT_INFO info = GAME_EVENT::BALTHUS_OFF;
    if (value != 0) {
        info = value == 1 ? GAME_EVENT::BALTHUS_ON : GAME_EVENT::BALTHUS_NO_CHANGE;
    }
    BalthusEvent::inst().SendEventInfo(CSPointer<User>(user, ObjectRefType::FunctionChain), info);
    return false;
}

bool BalthusJackpot(CUserSocket*, User*, char16_t*) {
    // Current L2Server 0x00578648.
    L2_GUARD_FUNCTION();
    BalthusEvent::inst().BuilderCmd_Jackpot();
    return false;
}

bool BalthusState(CUserSocket* socket, User*, char16_t* command) {
    // Current L2Server 0x005786e4.
    L2_GUARD_FUNCTION();
    if (command == nullptr) {
        return false;
    }
    std::int32_t value = 0;
    if (ParseFirstInt(command, value)) {
        BalthusEvent::inst().BuilderCmd_State(value);
    } else {
        SendUsage(socket, u"//balthus_state state_number");
    }
    return false;
}

bool BalthusProgress(CUserSocket* socket, User*, char16_t* command) {
    // Current L2Server 0x005788a8.
    L2_GUARD_FUNCTION();
    if (command == nullptr) {
        return false;
    }
    std::int32_t value = 0;
    if (ParseFirstInt(command, value)) {
        BalthusEvent::inst().BuilderCmd_Progress(runtime::AddInt32Wrapping(value, -1));
    } else {
        SendUsage(socket, u"//balthus_progress progress_number");
    }
    return false;
}

bool BalthusNextProgress(CUserSocket*, User*, char16_t*) {
    // Current L2Server 0x00578a6c.
    L2_GUARD_FUNCTION();
    const std::int32_t current = BalthusEvent::inst().GetCurrentProgress();
    BalthusEvent::inst().BuilderCmd_Progress(runtime::AddInt32Wrapping(current, 1));
    return false;
}

bool BalthusReset(CUserSocket*, User*, char16_t*) {
    // Current L2Server 0x00578b14.
    L2_GUARD_FUNCTION();
    BalthusEvent::inst().BuilderCmd_Reset();
    return false;
}

} // namespace BuilderCmdForEvent
