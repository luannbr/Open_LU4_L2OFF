#include "Protocol/src/SystemMessageIds.h"
#include "User.h"
#include "UserSocket.h"

#include "Alliance.h"
#include "Auction.h"
#include "DB.h"
#include "DynamicQuest.h"
#include "EventCampaign.h"
#include "GameEventSystem.h"
#include "HTMLCache.h"
#include "HtmlActionParamParser.h"
#include "HtmlCommand.h"
#include "Manor.h"
#include "NpcServer.h"
#include "OlympiadService.h"
#include "OlympiadServiceProxy.h"
#include "PetitionCli.h"
#include "Pledge.h"
#include "PledgeFunction.h"
#include "PledgeGameSystem.h"
#include "Quest.h"
#include "QuestHelper.h"
#include "Recipe.h"
#include "Residence.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "SignBoard.h"
#include "SkillEnchant.h"
#include "TeleportManager.h"
#include "WareHouse.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Config.h"
#include "Shared/src/FHtml.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/StringTokenizer.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"
#include "Shared/src/platform/Runtime.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

using HtmlCmdCallback = bool (*)(CUserSocket*, User*, const char16_t*, const char16_t*);

// Current User.cpp TLS owner consumed by HtmlCmdObserverGroup's exact
// anti-hack branch after TalkChannelCache::IsValidChannel fails.
extern thread_local std::uint32_t g_TalkChannelError;

struct HtmlCmdDefineType {
    const char16_t* m_pCmdName;
    bool m_bAllowedRaidUser;
    std::array<std::byte, 0x07> m_alignment09To10{};
    HtmlCmdCallback m_pCallback;

    bool operator==(const HtmlCmdDefineType& other) const {
        return std::u16string_view(m_pCmdName) == std::u16string_view(other.m_pCmdName);
    }
};


#define DECLARE_HTML_CMD(name) bool name(CUserSocket*, User*, const char16_t*, const char16_t*)

DECLARE_HTML_CMD(HtmlCmdHeroes);
DECLARE_HTML_CMD(HtmlCmdDiary);
DECLARE_HTML_CMD(HtmlCmdBookmark2);
DECLARE_HTML_CMD(HtmlCmdBookmark3);
DECLARE_HTML_CMD(HtmlCmdBookmark4);
DECLARE_HTML_CMD(HtmlCmdBBS);
DECLARE_HTML_CMD(HtmlCmdClanBBS);
DECLARE_HTML_CMD(HtmlCmdMemo);
DECLARE_HTML_CMD(HtmlCmdMail);
DECLARE_HTML_CMD(HtmlCmdFriend);
DECLARE_HTML_CMD(HtmlCmdOlympiad);
DECLARE_HTML_CMD(HtmlCmdAnnounce);
DECLARE_HTML_CMD(HtmlCmdRecipeNpc);
DECLARE_HTML_CMD(HtmlCmdRecipe);
DECLARE_HTML_CMD(HtmlCmdObserverGroup);
DECLARE_HTML_CMD(HtmlCmdObserver);
DECLARE_HTML_CMD(HtmlCmdAgit);
DECLARE_HTML_CMD(HtmlCmdMonsterRace);
DECLARE_HTML_CMD(HtmlCmdMultiSell);
DECLARE_HTML_CMD(HtmlCmdWrite);
DECLARE_HTML_CMD(HtmlCmdTeleportRequest);
DECLARE_HTML_CMD(HtmlCmdTeleportToNpc);
DECLARE_HTML_CMD(HtmlCmdTeleport);
DECLARE_HTML_CMD(HtmlCmdBuy);
DECLARE_HTML_CMD(HtmlCmdSell);
DECLARE_HTML_CMD(HtmlCmdDepositCastle);
DECLARE_HTML_CMD(HtmlCmdWithdrawCastle);
DECLARE_HTML_CMD(HtmlCmdDepositPledge);
DECLARE_HTML_CMD(HtmlCmdWithdrawPledge);
DECLARE_HTML_CMD(HtmlCmdDeposit);
DECLARE_HTML_CMD(HtmlCmdWithdraw);
DECLARE_HTML_CMD(HtmlCmdPacketDeposit);
DECLARE_HTML_CMD(HtmlCmdPacketWithdraw);
DECLARE_HTML_CMD(HtmlCmdQuestAccept);
DECLARE_HTML_CMD(HtmlCmdQuestStart);
DECLARE_HTML_CMD(HtmlCmdDynamicQuestAccept);
DECLARE_HTML_CMD(HtmlCmdDynamicQuestReward);
DECLARE_HTML_CMD(HtmlCmdDynamicQuestRanking);
DECLARE_HTML_CMD(HtmlCmdManorMenuSelect);
DECLARE_HTML_CMD(HtmlCmdMenuSelect);
DECLARE_HTML_CMD(HtmlCmdQuestChoice);
DECLARE_HTML_CMD(HtmlCmdLearnSkill);
DECLARE_HTML_CMD(HtmlCmdEnchantSkill);
DECLARE_HTML_CMD(HtmlCmdSkillQuestAccept);
DECLARE_HTML_CMD(HtmlCmdClassChange);
DECLARE_HTML_CMD(HtmlCmdTalkSelect);
DECLARE_HTML_CMD(HtmlCmdTransferMasterCancel);
DECLARE_HTML_CMD(HtmlCmdTransferMaster);
DECLARE_HTML_CMD(HtmlCmdCreateAcademy);
DECLARE_HTML_CMD(HtmlCmdRenameSubPledge);
DECLARE_HTML_CMD(HtmlCmdCreateSubPledge);
DECLARE_HTML_CMD(HtmlCmdUpdateSubPledgeMaster);
DECLARE_HTML_CMD(HtmlCmdUpgradeSubPledgeMemberCount);
DECLARE_HTML_CMD(HtmlCmdCreatePledge);
DECLARE_HTML_CMD(HtmlCmdDismissPledge);
DECLARE_HTML_CMD(HtmlCmdRevivePledge);
DECLARE_HTML_CMD(HtmlCmdPledgeLevelUp);
DECLARE_HTML_CMD(HtmlCmdConfirmBattle);
DECLARE_HTML_CMD(HtmlCmdCreateAlliance);
DECLARE_HTML_CMD(HtmlCmdShowRadar);
DECLARE_HTML_CMD(HtmlCmdShowPcCafeCouponUI);
DECLARE_HTML_CMD(HtmlCmdMatch);
DECLARE_HTML_CMD(HtmlCmdItemAuction);
DECLARE_HTML_CMD(HtmlCmdPetitionLink);
DECLARE_HTML_CMD(HtmlCmdPledgeGame);
DECLARE_HTML_CMD(HtmlCmdEventCampaignAccept);
DECLARE_HTML_CMD(HtmlCmdEventCampaignReward);
DECLARE_HTML_CMD(HtmlCmdEventCampaignRanking);
DECLARE_HTML_CMD(HtmlCmdOneTimeXmasTree);
DECLARE_HTML_CMD(HtmlCmdShowMultisell);
DECLARE_HTML_CMD(HtmlCmdWeddingPropose);

#undef DECLARE_HTML_CMD

#define HTML_CMD(name, allowed, callback) {u##name, allowed, {}, callback}

static HtmlCmdDefineType HtmlCmdDefineTable[] = {
    HTML_CMD("_heroes", true, HtmlCmdHeroes),
    HTML_CMD("_diary", true, HtmlCmdDiary),
    HTML_CMD("_bk2", true, HtmlCmdBookmark2),
    HTML_CMD("_bk3", true, HtmlCmdBookmark3),
    HTML_CMD("_bk4", true, HtmlCmdBookmark4),
    HTML_CMD("_bbs", true, HtmlCmdBBS),
    HTML_CMD("_clbbs", true, HtmlCmdClanBBS),
    HTML_CMD("_mm", true, HtmlCmdMemo),
    HTML_CMD("_mail", true, HtmlCmdMail),
    HTML_CMD("_friend", true, HtmlCmdFriend),
    HTML_CMD("_olympiad", true, HtmlCmdOlympiad),
    HTML_CMD("_announce", true, HtmlCmdAnnounce),
    HTML_CMD("recipeNPC", true, HtmlCmdRecipeNpc),
    HTML_CMD("recipe", true, HtmlCmdRecipe),
    HTML_CMD("observer_group_", true, HtmlCmdObserverGroup),
    HTML_CMD("observer_", true, HtmlCmdObserver),
    HTML_CMD("_agit", true, HtmlCmdAgit),
    HTML_CMD("_mr", true, HtmlCmdMonsterRace),
    HTML_CMD("multisell", true, HtmlCmdMultiSell),
    HTML_CMD("write", true, HtmlCmdWrite),
    HTML_CMD("teleport_request", true, HtmlCmdTeleportRequest),
    HTML_CMD("teleporttonpc_", true, HtmlCmdTeleportToNpc),
    HTML_CMD("teleport_", true, HtmlCmdTeleport),
    HTML_CMD("buy", true, HtmlCmdBuy),
    HTML_CMD("buy_request", true, HtmlCmdBuy),
    HTML_CMD("sell", true, HtmlCmdSell),
    HTML_CMD("sell_request", true, HtmlCmdSell),
    HTML_CMD("deposit_castle", true, HtmlCmdDepositCastle),
    HTML_CMD("withdraw_castle", true, HtmlCmdWithdrawCastle),
    HTML_CMD("deposit_pledge", true, HtmlCmdDepositPledge),
    HTML_CMD("withdraw_pledge", true, HtmlCmdWithdrawPledge),
    HTML_CMD("deposit", true, HtmlCmdDeposit),
    HTML_CMD("withdraw", true, HtmlCmdWithdraw),
    HTML_CMD("package_deposit", true, HtmlCmdPacketDeposit),
    HTML_CMD("package_withdraw", true, HtmlCmdPacketWithdraw),
    HTML_CMD("quest_accept", true, HtmlCmdQuestAccept),
    HTML_CMD("quest_start", true, HtmlCmdQuestStart),
    HTML_CMD("dynamic_quest_accept", true, HtmlCmdDynamicQuestAccept),
    HTML_CMD("dynamic_quest_reward", true, HtmlCmdDynamicQuestReward),
    HTML_CMD("dynamic_quest_ranking", true, HtmlCmdDynamicQuestRanking),
    HTML_CMD("manor_menu_select", false, HtmlCmdManorMenuSelect),
    HTML_CMD("menu_select", true, HtmlCmdMenuSelect),
    HTML_CMD("quest_choice", false, HtmlCmdQuestChoice),
    HTML_CMD("learn_skill", true, HtmlCmdLearnSkill),
    HTML_CMD("enchant_skill", true, HtmlCmdEnchantSkill),
    HTML_CMD("skillquest_accept", true, HtmlCmdSkillQuestAccept),
    HTML_CMD("class_change", true, HtmlCmdClassChange),
    HTML_CMD("talk_select", true, HtmlCmdTalkSelect),
    HTML_CMD("transfer_master_cancel", false, HtmlCmdTransferMasterCancel),
    HTML_CMD("transfer_master", false, HtmlCmdTransferMaster),
    HTML_CMD("create_academy", false, HtmlCmdCreateAcademy),
    HTML_CMD("rename_subpledge", false, HtmlCmdRenameSubPledge),
    HTML_CMD("create_subpledge", false, HtmlCmdCreateSubPledge),
    HTML_CMD("update_subpledge_master", false, HtmlCmdUpdateSubPledgeMaster),
    HTML_CMD("upgrade_subpledge_member_count", false, HtmlCmdUpgradeSubPledgeMemberCount),
    HTML_CMD("create_pledge", false, HtmlCmdCreatePledge),
    HTML_CMD("dismiss_pledge", false, HtmlCmdDismissPledge),
    HTML_CMD("revive_pledge", false, HtmlCmdRevivePledge),
    HTML_CMD("pledge_levelup", false, HtmlCmdPledgeLevelUp),
    HTML_CMD("confirm_battle", false, HtmlCmdConfirmBattle),
    HTML_CMD("create_alliance", false, HtmlCmdCreateAlliance),
    HTML_CMD("show_radar", true, HtmlCmdShowRadar),
    HTML_CMD("show_pccafe_coupon_ui", true, HtmlCmdShowPcCafeCouponUI),
    HTML_CMD("_match", true, HtmlCmdMatch),
    HTML_CMD("item_auction", true, HtmlCmdItemAuction),
    HTML_CMD("petitionlink", true, HtmlCmdPetitionLink),
    HTML_CMD("pledgegame", true, HtmlCmdPledgeGame),
    HTML_CMD("event_campaign_accept", true, HtmlCmdEventCampaignAccept),
    HTML_CMD("event_campaign_reward", true, HtmlCmdEventCampaignReward),
    HTML_CMD("event_campaign_ranking", true, HtmlCmdEventCampaignRanking),
    HTML_CMD("one_time_xmas_tree", true, HtmlCmdOneTimeXmasTree),
    HTML_CMD("show_multisell", true, HtmlCmdShowMultisell),
    HTML_CMD("wedding_propose", false, HtmlCmdWeddingPropose),
};

#undef HTML_CMD

static_assert(std::size(HtmlCmdDefineTable) == 73);

std::vector<HtmlCmdDefineType> g_HtmlCmds;

namespace {

std::u16string PacketText(const std::array<SharedUtilsWideChar, 1024>& source) {
    std::size_t length = 0;
    while (length < source.size() && source[length] != SharedUtilsWideChar{}) {
        ++length;
    }
    return std::u16string(reinterpret_cast<const char16_t*>(source.data()), length);
}


CFHTML::StrStrMapType ToFhtmlParameters(const CHTMLActionParamParser::StrStrMapType& source) {
    CFHTML::StrStrMapType result;
    for (const auto& parameter : source) {
        const std::u16string name = std::u16string(runtime::StringView(parameter.first));
        const std::u16string value = std::u16string(runtime::StringView(parameter.second));
        result[std::u16string(std::u16string_view(name))] = std::u16string(std::u16string_view(value));
    }
    return result;
}

void ParseHtmlInt(const char16_t* source, int* value) {
    if (!source || !value) {
        return;
    }
    ParseHTMLParam(reinterpret_cast<const SharedUtilsWideChar*>(source), const_cast<char*>("d"), value);
}

const SharedUtilsWideChar* AsTargetText(const char16_t* source) {
    return reinterpret_cast<const SharedUtilsWideChar*>(source);
}


std::int64_t WindowsWtoi64(const std::u16string& value) {
    const std::string narrow = Utf16ToUtf8(runtime::StringView(value));
    return static_cast<std::int64_t>(std::strtoll(narrow.c_str(), nullptr, 10));
}

CNPC* ResolveNearbyTalkNpc(User* user, const int, const bool actionFailedOnDistance = true) {
    L2_GUARD(*user->m_lock);
    CObject* const target = user->m_spTalkTarget.GetObjectW();
    if (target == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] User[%s]'s talk target is NULL m_spTalkTarget[%d]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     sourceLine,
        //     user->GetName(),
        //     user->m_spTalkTarget.GetId())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] User[%s]'s talk target is NULL m_spTalkTarget[%d]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     sourceLine,
        //     user->GetName(),
        //     user->m_spTalkTarget.GetId())
        // L2_LOG_WARN(Log,
        //     "Cannot handle NPC HTML action for %s: talk target object %u is missing.",
        //     WideToUtf8(user->GetName()).c_str(), static_cast<unsigned>(user->m_spTalkTarget.GetId()))
        L2_LOG_WARN(
            Log, "Cannot handle NPC HTML action for %s: talk target object %u is missing.",
            Utf16ToUtf8(user->GetName()).c_str(), static_cast<unsigned>(user->m_spTalkTarget.GetId())
        );
        return nullptr;
    }
    if (!target->IsNPC()) {
        if (target->IsPC()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] User[%s]'s talk target is PC[%s]",
            //     L"UserSocket_HTMLCmd.cpp",
            //     sourceLine,
            //     user->GetName(),
            //     target->GetName())
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] User[%s]'s talk target is PC[%s]",
            //     L"UserSocket_HTMLCmd.cpp",
            //     sourceLine,
            //     user->GetName(),
            //     target->GetName())
            // L2_LOG_WARN(Log,
            //     "Cannot handle NPC HTML action for %s: talk target %s is a player.",
            //     WideToUtf8(user->GetName()).c_str(), WideToUtf8(target->GetName()).c_str())
            L2_LOG_WARN(
                Log, "Cannot handle NPC HTML action for %s: talk target %s is a player.",
                Utf16ToUtf8(user->GetName()).c_str(), Utf16ToUtf8(target->GetName()).c_str()
            );
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] User[%s]'s talk target is neither PC nor NPC [sid:%d]",
            //     L"UserSocket_HTMLCmd.cpp",
            //     sourceLine,
            //     user->GetName(),
            //     user->m_spTalkTarget.GetId())
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] User[%s]'s talk target is neither PC nor NPC [sid:%d]",
            //     L"UserSocket_HTMLCmd.cpp",
            //     sourceLine,
            //     user->GetName(),
            //     user->m_spTalkTarget.GetId())
            // L2_LOG_WARN(Log,
            //     "Cannot handle NPC HTML action for %s: talk target object %u is neither a player nor an NPC.",
            //     WideToUtf8(user->GetName()).c_str(), static_cast<unsigned>(user->m_spTalkTarget.GetId()))
            L2_LOG_WARN(
                Log, "Cannot handle NPC HTML action for %s: talk target object %u is neither a player nor an NPC.",
                Utf16ToUtf8(user->GetName()).c_str(), static_cast<unsigned>(user->m_spTalkTarget.GetId())
            );
        }
        return nullptr;
    }
    CNPC* const npc = target->CastNPC();
    if (npc == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] TalkNpc is not NPC User[%s]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     sourceLine,
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] TalkNpc is not NPC User[%s]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     sourceLine,
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     "Cannot handle NPC HTML action for %s: NPC target could not be cast to CNPC.",
        //     WideToUtf8(user->GetName()).c_str())
        L2_LOG_ERROR(
            Log, "Cannot handle NPC HTML action for %s: NPC target could not be cast to CNPC.",
            Utf16ToUtf8(user->GetName()).c_str()
        );
        return nullptr;
    }
    const FVector npcPosition = npc->GetPos();
    const FVector userPosition = user->GetPos();
    const double x = userPosition.x - npcPosition.x;
    const double y = userPosition.y - npcPosition.y;
    if (x * x + y * y > 62500.0) {
        if (actionFailedOnDistance) {
            // user->ActionFailed(0)
            user->ActionFailed(AF_DEFAULT);
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] DISTANCE OVER!! User[%s]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     sourceLine,
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] DISTANCE OVER!! User[%s]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     sourceLine,
        //     user->GetName())
        // L2_LOG_WARN(Log,
        //     "Cannot handle NPC HTML action for %s: horizontal distance exceeds 250 units (squared distance %.2f).",
        //     WideToUtf8(user->GetName()).c_str(), x * x + y * y)
        L2_LOG_WARN(
            Log, "Cannot handle NPC HTML action for %s: horizontal distance exceeds 250 units (squared distance %.2f).",
            Utf16ToUtf8(user->GetName()).c_str(), x * x + y * y
        );
        return nullptr;
    }
    return npc;
}

const wchar_t* ObserverAccountName(const std::array<char16_t, 15>& source, std::array<wchar_t, 16>& storage) {
#if defined(_WIN32)
    static_cast<void>(storage);
    return reinterpret_cast<const wchar_t*>(source.data());
#else
    std::size_t length = 0;
    while (length < source.size() && source[length] != u'\0') {
        ++length;
    }
    const std::wstring converted = ::Utf16ToWide(std::u16string_view(source.data(), length));
    const std::size_t copyLength = std::min(converted.size(), storage.size() - 1U);
    std::copy_n(converted.data(), copyLength, storage.data());
    storage[copyLength] = L'\0';
    return storage.data();
#endif
}

void HandleHtmlCommandInPeer(CSPointer<User> user, const char16_t* command) {
    // Current L2Server 0x00c37a00.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x1c2,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x1c2,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_ERROR(Log, "Cannot handle Olympiad peer HTML command: user is missing.");
        return;
    }
    if (user->GetSocket() == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x1c3,
        //     L"WO_ASSERT_NOT_NULL")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x1c3,
        //     L"WO_ASSERT_NOT_NULL")
        L2_LOG_WARN(Log, "Cannot handle Olympiad peer HTML command: user socket is missing.");
        return;
    }
    if (command == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x1c4,
        //     L"WO_ASSERT_NOT_NULL")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x1c4,
        //     L"WO_ASSERT_NOT_NULL")
        L2_LOG_WARN(Log, "Cannot handle Olympiad peer HTML command: command text is missing.");
        return;
    }
    if (Singleton<WOlympiadServiceConfig>::inst().IsHost()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x1c8)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x1c8)
        L2_LOG_ERROR(Log, "Cannot handle Olympiad peer HTML command: server is configured as an Olympiad host.");
        return;
    }

    const CHTMLActionParamParser parameters = CHTMLActionParamParser::Parse(command);
    const char16_t* const action = parameters.GetStrValue(u"command");
    if (action == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x1d1)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x1d1)
        L2_LOG_WARN(Log, "Cannot handle Olympiad peer HTML command: command parameter is missing.");
        return;
    }
    if (user->m_data->m_bIsInCombatMode) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_OBSERVE_DURING_COMBAT);
        return;
    }
    if (user->IsOlympiadObserver()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x1df)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x1df)
        L2_LOG_WARN(Log, "Cannot handle Olympiad peer HTML command: user is already an Olympiad observer.");
        return;
    }

    if (target_utf16::CompareN(action, u"move_op_field", 0x0c) == 0) {
        const int fieldId = parameters.GetIntValue(u"field") - 1;
        if (static_cast<unsigned int>(fieldId) >= static_cast<unsigned int>(SharedOlympiadInfo::FIELD_COUNT)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]invalid field id: %d, %d",
            //     L"UserSocket_HTMLCmd.cpp",
            //     0x1e9,
            //     user->GetDBID(),
            //     fieldId)
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]invalid field id: %d, %d",
            //     L"UserSocket_HTMLCmd.cpp",
            //     0x1e9,
            //     user->GetDBID(),
            //     fieldId)
            L2_LOG_WARN(
                Log, "Cannot observe Olympiad field for character DB ID %u: zero-based field ID %d is outside 0..%d.",
                static_cast<unsigned>(user->GetDBID()), fieldId, static_cast<int>(SharedOlympiadInfo::FIELD_COUNT) - 1
            );
            return;
        }
        if (user->m_AreaMod.m_bIsInPeaceField) {
            Singleton<WOlympiadServiceProxy>::inst().ObserveMatch(user, fieldId);
        } else {
            user->SendSystemMessage(SystemMessageId::OLYMPIAD_OBSERVATION_REQUIRES_PEACE_ZONE);
        }
        return;
    }
    if (target_utf16::CompareN(action, u"op_field_list", 0x0d) == 0) {
        Singleton<WOlympiadServiceProxy>::inst().SendMatchListToUser(user);
    }
}

void HandleHtmlCommandInHost(CSPointer<User> user, const char16_t* command) {
    // Current L2Server 0x00c37f20.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x204,
        //     L"WO_ASSERT_TRUE")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x204,
        //     L"WO_ASSERT_TRUE")
        L2_LOG_ERROR(Log, "Cannot handle Olympiad host HTML command: user is missing.");
        return;
    }
    if (user->GetSocket() == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x205,
        //     L"WO_ASSERT_NOT_NULL")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x205,
        //     L"WO_ASSERT_NOT_NULL")
        L2_LOG_WARN(Log, "Cannot handle Olympiad host HTML command: user socket is missing.");
        return;
    }
    if (command == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x206,
        //     L"WO_ASSERT_NOT_NULL")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x206,
        //     L"WO_ASSERT_NOT_NULL")
        L2_LOG_WARN(Log, "Cannot handle Olympiad host HTML command: command text is missing.");
        return;
    }
    if (!Singleton<WOlympiadServiceConfig>::inst().IsHost()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x20a)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x20a)
        L2_LOG_ERROR(Log, "Cannot handle Olympiad host HTML command: server is configured as an Olympiad peer.");
        return;
    }

    const CHTMLActionParamParser parameters = CHTMLActionParamParser::Parse(command);
    const char16_t* const action = parameters.GetStrValue(u"command");
    if (action == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x213)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x213)
        L2_LOG_WARN(Log, "Cannot handle Olympiad host HTML command: command parameter is missing.");
        return;
    }
    if (user->m_data->m_bIsInCombatMode) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_OBSERVE_DURING_COMBAT);
        return;
    }
    if (!user->IsOlympiadObserver()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x221)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x221)
        L2_LOG_WARN(Log, "Cannot handle Olympiad host HTML command: user is not an Olympiad observer.");
        return;
    }

    if (target_utf16::CompareN(action, u"move_op_field", 0x0c) == 0) {
        Singleton<WOlympiadService>::inst().ChangeMatchInObserving(user, parameters.GetIntValue(u"field") - 1);
        return;
    }
    if (target_utf16::CompareN(action, u"op_field_list", 0x0d) == 0) {
        Singleton<WOlympiadService>::inst().SendGameList(user);
    }
}

} // namespace

bool CUserSocket::HtmlCMDPacket(CSocket* const socket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c34c18.
    L2_GUARD_FUNCTION();

    std::array<SharedUtilsWideChar, 1024> decoded{};
    ::Disassemble(packet, "S", 0x800, decoded.data());
    std::u16string command = PacketText(decoded);

    CUserSocket* const userSocket = static_cast<CUserSocket*>(socket);
    User* const user = userSocket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] No user... on HtmlCMDPacket",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0xaab)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] No user... on HtmlCMDPacket",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0xaab)
        L2_LOG_WARN(Log, "Ignoring HTML command: socket has no user.");
        return true;
    }

    if (g_bHtmlLinkCheck && user->m_bPrevHTMLLinkCheckState &&
        g_HTMLCache.IsValidLink(user->m_ExpectedHtmlReplyList, std::u16string(command.data()), 0) == 0) {
        if (g_bLogOnInvalidLink) {
#if defined(_WIN32)
            const wchar_t* const accountName = reinterpret_cast<const wchar_t*>(user->m_sAccountName.data());
#else
            const std::wstring accountNameStorage = FixedUtf16ToWide(user->m_sAccountName);
            const wchar_t* const accountName = accountNameStorage.c_str();
#endif
            // Log.Add(
            //     LOG_ERROR,
            //     L"[User:%d, %s][recived: %s] : INVALID link on HTML[%s][%d]",
            //     user->GetAccountID(),
            //     accountName,
            //     command.c_str(),
            //     L"UserSocket_HTMLCmd.cpp",
            //     0xab5)
            // L2_LOG_ERROR(Log,
            //     L"[User:%d, %s][recived: %s] : INVALID link on HTML[%s][%d]",
            //     user->GetAccountID(),
            //     accountName,
            //     command.c_str(),
            //     L"UserSocket_HTMLCmd.cpp",
            //     0xab5)
            L2_LOG_WARN(
                Log, "HTML command failed link validation for account %u (%s); stop processing: %s; command: %s",
                static_cast<unsigned>(user->GetAccountID()), WideToUtf8(accountName).c_str(),
                g_bKickOnInvalidLink ? "yes" : "no", Utf16ToUtf8(command).c_str()
            );
        }
        if (g_bKickOnInvalidLink) {
            return false;
        }
    }

    for (const HtmlCmdDefineType& definition : g_HtmlCmds) {
        const std::size_t commandLength = target_utf16::Length(definition.m_pCmdName);
        if (target_utf16::CompareN(command.c_str(), definition.m_pCmdName, commandLength) != 0) {
            continue;
        }

        if (user->m_data->m_nWorldId == g_nWorldId || definition.m_bAllowedRaidUser) {
            definition.m_pCallback(userSocket, user, command.c_str() + commandLength, command.c_str());
        }
        return false;
    }

    // user->ActionFailed(0)
    user->ActionFailed(AF_DEFAULT);
    return false;
}

bool LinkHtmlPacket(CSocket* const socket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c37530.
    L2_GUARD_FUNCTION();

    std::array<SharedUtilsWideChar, 1024> decoded{};
    ::Disassemble(packet, "S", 0x800, decoded.data());
    std::u16string command = PacketText(decoded);

    CUserSocket* const userSocket = static_cast<CUserSocket*>(socket);
    User* const user = userSocket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (target_utf16::Length(command.c_str()) > 1000U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid HTML File Name (Too Long). [user : %d, %s][%s][%d]",
        //     user->GetAccountID(),
        //     user->m_sAccountName.data(),
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x58)
        // L2_LOG_ERROR(Log,
        //     L"Invalid HTML File Name (Too Long). [user : %d, %s][%s][%d]",
        //     user->GetAccountID(),
        //     user->m_sAccountName.data(),
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x58)
        L2_LOG_WARN(
            Log, "Ignoring HTML file request for account %u (%s): filename length %zu exceeds 1000 characters.",
            static_cast<unsigned>(user->GetAccountID()), Utf16ToUtf8(FixedUtf16View(user->m_sAccountName)).c_str(),
            target_utf16::Length(command.c_str())
        );
        return false;
    }

    std::u16string formattedHtml;
    if (g_bHtmlLinkCheck && user->m_bPrevHTMLLinkCheckState) {
        L2_GUARD(user->m_HTMLLock);
        if (g_HTMLCache.IsValidLink(user->m_ExpectedHtmlReplyList, std::u16string(command.data()), 0) == 0) {
            if (g_bLogOnInvalidLink) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[User:%d, %s][recived: %s] : INVALID link on HTML[%s][%d]",
                //     user->GetAccountID(),
                //     user->m_sAccountName.data(),
                //     command.c_str(),
                //     L"UserSocket_HTMLCmd.cpp",
                //     0x66)
                // L2_LOG_ERROR(Log,
                //     L"[User:%d, %s][recived: %s] : INVALID link on HTML[%s][%d]",
                //     user->GetAccountID(),
                //     user->m_sAccountName.data(),
                //     command.c_str(),
                //     L"UserSocket_HTMLCmd.cpp",
                //     0x66)
                L2_LOG_WARN(
                    Log, "HTML file request failed link validation for account %u (%s); stop processing: %s; request: %s",
                    static_cast<unsigned>(user->GetAccountID()), Utf16ToUtf8(FixedUtf16View(user->m_sAccountName)).c_str(),
                    g_bKickOnInvalidLink ? "yes" : "no", Utf16ToUtf8(command).c_str()
                );
            }
            if (g_bKickOnInvalidLink) {
                return true;
            }
        }
    }

    if (target_utf16::Find(command.data(), u'?') == nullptr) {
        unsigned int page = 0;
        char16_t* const fragment = target_utf16::Find(command.data(), u'#');
        if (fragment != nullptr && target_utf16::Length(fragment) != 0U) {
            page = static_cast<unsigned int>(WindowsWtol(std::u16string_view(fragment + 1)));
            *fragment = u'\0';
        }

        std::u16string html;
        if (g_HTMLCache.GetHTMLFile(std::u16string(command.c_str()).c_str(), html, user->m_data->m_CountryCode)) {
            const std::u16string hostHtml(runtime::StringView(html));
            user->ShowHTML(std::u16string(command.c_str()).c_str(), hostHtml.c_str(), page, HTML::LINK, true);
        }
    } else {
        char16_t* const question = target_utf16::Find(command.data(), u'?');
        const CHTMLActionParamParser parameters = CHTMLActionParamParser::Parse(question);
        *question = u'\0';
        const CFHTML::StrStrMapType targetParameters = ToFhtmlParameters(parameters.GetStrStrMap());
        formattedHtml =
            CFHTML::MakeHTMLFromStrMap(std::u16string(command.c_str()).c_str(), targetParameters, user->m_data->m_CountryCode);

        // Current 0x00c3781f passes the truncated source buffer, not the
        // formatted result produced immediately above.
        user->ShowHTML(u"Formated HTML", std::u16string(command.c_str()).c_str(), 0, HTML::LINK, true);
    }
    return false;
}

void CUserSocket::LoadHtmlCmdHandler() {
    // Current L2Server 0x00c3538c.
    L2_GUARD_FUNCTION();
    g_HtmlCmds.clear();
    g_HtmlCmds.reserve(std::size(HtmlCmdDefineTable));
    for (const HtmlCmdDefineType& definition : HtmlCmdDefineTable) {
        const auto duplicate = std::find(g_HtmlCmds.begin(), g_HtmlCmds.end(), definition);
        if (duplicate == g_HtmlCmds.end()) {
            g_HtmlCmds.push_back(definition);
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"HtmlCmd [%s] duplicated",
            //     definition.m_pCmdName)
            // L2_LOG_ERROR(Log,
            //     L"HtmlCmd [%s] duplicated",
            //     definition.m_pCmdName)
            L2_LOG_ERROR(
                Log, "Skipping duplicate HTML command registration: %s.", Utf16ToUtf8(definition.m_pCmdName).c_str()
            );
        }
    }
}

bool RecipeCommand(User* user, const char16_t* command, const bool dwarfRecipe) {
    // Current L2Server 0x00c38440.
    L2_GUARD_FUNCTION();
    const CHTMLActionParamParser parameters = CHTMLActionParamParser::Parse(command);
    const int recipeId = parameters.GetIntValue(u"id");
    CRecipeInfo* const recipe = g_RecipeDB.find(recipeId);
    if (recipe == nullptr) {
        return false;
    }

    unsigned int failureMessage = 0;
    bool makeProduct = false;
    {
        L2_GUARD(*user->m_lock);
        if (dwarfRecipe && user->GetDwarfCreateItemLevel() < recipe->m_nLevel) {
            failureMessage = SystemMessageId::CREATE_ITEM_SKILL_MISSING_OR_LEVEL_TOO_LOW;
        } else if (!user->HaveRecipe(recipeId)) {
            failureMessage = SystemMessageId::NO_REGISTERED_RECIPES;
        } else if (user->m_inv.GetFirstItemByClassID(recipe->m_nItemId, false, false) == nullptr) {
            failureMessage = SystemMessageId::NOT_ENOUGH_MATERIALS;
        } else {
            makeProduct = true;
        }
    }

    if (failureMessage != 0) {
        user->GetSocket()->SendSystemMessage(failureMessage);
    } else if (makeProduct) {
        std::vector<RecipeOfferingItemStruct*> offeringItems;
        recipe->MakeProduct(user, offeringItems, false);
    }
    return false;
}

bool HtmlCmdRecipeNpc(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c38674.
    L2_GUARD_FUNCTION();
    if (ResolveNearbyTalkNpc(user, 0x298, false) != nullptr) {
        RecipeCommand(user, command, false);
    }
    return false;
}

bool HtmlCmdRecipe(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c38990.
    L2_GUARD_FUNCTION();
    return RecipeCommand(user, command, true);
}

bool HtmlCmdMail(CUserSocket* socket, User*, const char16_t*, const char16_t* command) {
    // Current L2Server 0x00c34834.
    L2_GUARD_FUNCTION();
    CHtmlCommandSP htmlCommand = CHtmlCommandFactory::GetHtmlCommand(socket, AsTargetText(command));
    htmlCommand->Execute();
    return false;
}

bool HtmlCmdFriend(CUserSocket* socket, User*, const char16_t*, const char16_t* command) {
    // Current L2Server 0x00c34980.
    L2_GUARD_FUNCTION();
    CHtmlCommandSP htmlCommand = CHtmlCommandFactory::GetHtmlCommand(socket, AsTargetText(command));
    htmlCommand->Execute();
    return false;
}

bool HtmlCmdAnnounce(CUserSocket* socket, User*, const char16_t*, const char16_t* command) {
    // Current L2Server 0x00c34acc.
    L2_GUARD_FUNCTION();
    CHtmlCommandSP htmlCommand = CHtmlCommandFactory::GetHtmlCommand(socket, AsTargetText(command));
    htmlCommand->Execute();
    return false;
}

bool HtmlCmdHeroes(CUserSocket*, User* user, const char16_t*, const char16_t*) {
    // Current L2Server 0x00c34750.
    L2_GUARD_FUNCTION();
    CSPointer<User> retained(user, ObjectRefType::FunctionChain);
    Singleton<WOlympiadServiceProxy>::inst().SendHeroListToUser(retained);
    return false;
}

bool HtmlCmdDiary(CUserSocket*, User*, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c37948 parses and discards.
    L2_GUARD_FUNCTION();
    const CHTMLActionParamParser parameters = CHTMLActionParamParser::Parse(command);
    (void)parameters;
    return false;
}

bool HtmlCmdOlympiad(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c38324.
    L2_GUARD_FUNCTION();
    CSPointer<User> retained(user, ObjectRefType::FunctionChain);
    if (Singleton<WOlympiadServiceConfig>::inst().IsHost()) {
        HandleHtmlCommandInHost(std::move(retained), command);
    } else {
        HandleHtmlCommandInPeer(std::move(retained), command);
    }
    return false;
}

bool HtmlCmdBookmark2(CUserSocket*, User* user, const char16_t*, const char16_t* fullCommand) {
    // Current L2Server 0x00c2f20c.
    L2_GUARD_FUNCTION();
    if (user->IsBuilder()) {
        int page = 0;
        ParseHtmlInt(fullCommand + 5, &page);
        if (page < 1) {
            page = 1;
        }
        db.RequestBuilderBookMarkList(user, page);
    }
    return false;
}

bool HtmlCmdBookmark3(CUserSocket*, User* user, const char16_t*, const char16_t* fullCommand) {
    // Current L2Server 0x00c2f304.
    L2_GUARD_FUNCTION();
    if (user->IsBuilder()) {
        int bookmarkId = 0;
        ParseHtmlInt(fullCommand + 5, &bookmarkId);
        db.RequestBuilderBookMarkOne(user, bookmarkId);
    }
    return false;
}

bool HtmlCmdBookmark4(CUserSocket*, User* user, const char16_t*, const char16_t* fullCommand) {
    // Current L2Server 0x00c2f3ec.
    L2_GUARD_FUNCTION();
    if (user->IsBuilder()) {
        int bookmarkId = 0;
        ParseHtmlInt(fullCommand + 5, &bookmarkId);
        db.RequestBuilderBookMarkDel(user, bookmarkId);
    }
    return false;
}

bool HtmlCmdConfirmBattle(CUserSocket*, User*, const char16_t*, const char16_t*) {
    // Current L2Server 0x0046c3fc: guarded false-return ICF body.
    L2_GUARD_FUNCTION();
    return false;
}

bool HtmlCmdQuestAccept(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server.exe 0x00c38b30.
    L2_GUARD_FUNCTION();
    CNPC* const npc = ResolveNearbyTalkNpc(user, 0x6e6);
    if (npc != nullptr && npc->m_data != nullptr) {
        const CHTMLActionParamParser parameters = CHTMLActionParamParser::Parse(command);
        const int questId = parameters.GetIntValue(u"quest_id");
        NpcServer::Inst()->Send("cddd", 10, user->GetIndex(), npc->GetIndex(), questId);
        if (user->IsBuilder()) {
            g_QuestHelper.checkAndOpenQuest(QuestHelper::QuestAccpetedEvent, npc->GetName(), questId);
        }
    }
    return false;
}

bool HtmlCmdMenuSelect(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server.exe 0x00c39530.
    L2_GUARD_FUNCTION();
    CNPC* const npc = ResolveNearbyTalkNpc(user, 0x771, false);
    if (npc != nullptr && npc->m_data != nullptr) {
        const CHTMLActionParamParser parameters = CHTMLActionParamParser::Parse(command);
        const std::int64_t reply = parameters.GetInt64Value(u"reply");
        const int ask = parameters.GetIntValue(u"ask");
        const int state = parameters.GetIntValue(u"state");
        QuestNpcEventController* const controller = npc->GetEventController(QUEST_NPC_EVENT_MENU_SELECTED, false);
        if (controller != nullptr) {
            controller->Do(*user, npc, ask, reply);
        }
        NpcServer::Inst()->Send(
            "cddddQd", 0x0c, user->m_data->index, static_cast<int>(user->Id()), npc->m_data->index, ask, reply, state
        );
        if (user->IsBuilder()) {
            if (const char16_t* const askValue = parameters.GetStrValue(u"ask")) {
                user->SendSystemMessage(u"Ask", std::u16string(askValue));
            }
            if (const char16_t* const replyValue = parameters.GetStrValue(u"reply")) {
                user->SendSystemMessage(u"Reply", std::u16string(replyValue));
            }
            g_QuestHelper.checkAndOpenQuest(QuestHelper::MenuSelectedEvent, npc->GetName(), ask, reply);
        }
    }
    return false;
}

bool HtmlCmdQuestChoice(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server.exe 0x00c39ad4.
    L2_GUARD_FUNCTION();
    CNPC* const npc = ResolveNearbyTalkNpc(user, 0x7c7, false);
    if (npc != nullptr && npc->m_data != nullptr) {
        const CHTMLActionParamParser parameters = CHTMLActionParamParser::Parse(command);
        const int choice = parameters.GetIntValue(u"choice");
        const int option = parameters.GetIntValue(u"option");
        QuestNpcEventController* const controller = npc->GetEventController(QUEST_NPC_EVENT_TALK_SELECTED, false);
        if (controller != nullptr) {
            controller->Do(*user, npc, 0, 0);
        }
        NpcServer::Inst()->Send(
            "cddddd", 0x10, static_cast<int>(user->Id()), user->m_data->index, static_cast<int>(npc->Id()), choice, option
        );
    }
    return false;
}

bool HtmlCmdQuestStart(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c38f30.
    L2_GUARD_FUNCTION();
    if (user != nullptr && command != nullptr) {
        const CHTMLActionParamParser parameters = CHTMLActionParamParser::Parse(command);
        Singleton<CQuestSystem>::inst().OnStarted(*user, parameters.GetIntValue(u"quest_id"));
    }
    return false;
}

namespace {

template <typename TCallback>
bool HandleDynamicQuestHtml(User* user, const char16_t* command, TCallback callback) {
    if (user == nullptr || command == nullptr) {
        return false;
    }
    const CHTMLActionParamParser parameters = CHTMLActionParamParser::Parse(command);
    std::pair<int, int> ids{parameters.GetIntValue(u"dquest_id"), parameters.GetIntValue(u"campaignId")};
    DynamicQuestManager* const manager = DynamicQuestManager::Inst();
    (manager->*callback)(*user, ids);
    return false;
}

} // namespace

bool HtmlCmdDynamicQuestAccept(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c39020.
    L2_GUARD_FUNCTION();
    return HandleDynamicQuestHtml(user, command, &DynamicQuestManager::OnAcceptDynamicQuest);
}

bool HtmlCmdDynamicQuestReward(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c391d0.
    L2_GUARD_FUNCTION();
    return HandleDynamicQuestHtml(user, command, &DynamicQuestManager::OnRequestReward);
}

bool HtmlCmdDynamicQuestRanking(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c39380.
    L2_GUARD_FUNCTION();
    return HandleDynamicQuestHtml(user, command, &DynamicQuestManager::OnRequestRanking);
}

bool HtmlCmdMatch(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c3b43c.
    L2_GUARD_FUNCTION();
    const CHTMLActionParamParser parameters = CHTMLActionParamParser::Parse(command);
    CSPointer<User> retained(user, ObjectRefType::FunctionChain);
    Singleton<WOlympiadServiceProxy>::inst().SendHeroMatchResult(
        retained, parameters.GetIntValue(u"class"), parameters.GetIntValue(u"page")
    );
    return false;
}

bool HtmlCmdWeddingPropose(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c3b948.
    L2_GUARD_FUNCTION();
    enum class TalkStatus { Ready, Rejected, Missing, Unknown, TooFar };
    std::uint32_t unknownTargetId;
    std::optional<FVector> npcPositionStorage;
    std::optional<FVector> userPositionStorage;
    const TalkStatus talkStatus = [&] {
        L2_GUARD(*user->m_lock);
        CCreature* const target = user->m_spTalkTarget.GetCreature();
        if (target == nullptr) {
            return TalkStatus::Missing;
        }
        if (!target->IsNPC()) {
            if (target->IsPC()) {
                const char16_t* const targetName = target->GetName();
                const char16_t* const userName = user->GetName();
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] User[%s]'s talk target is PC[%s]",
                //     L"UserSocket_HTMLCmd.cpp",
                //     0xa16,
                //     userName,
                //     targetName)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] User[%s]'s talk target is PC[%s]",
                //     L"UserSocket_HTMLCmd.cpp",
                //     0xa16,
                //     userName,
                //     targetName)
                // L2_LOG_WARN(Log,
                //     "Cannot process wedding proposal for %s: talk target %s is a player.",
                //     WideToUtf8(userName).c_str(), WideToUtf8(targetName).c_str())
                L2_LOG_WARN(
                    Log, "Cannot process wedding proposal for %s: talk target %s is a player.",
                    Utf16ToUtf8(userName).c_str(), Utf16ToUtf8(targetName).c_str()
                );
            } else {
                unknownTargetId = user->m_spTalkTarget.GetId();
                return TalkStatus::Unknown;
            }
            return TalkStatus::Rejected;
        }

        CNPC* const talkNpc = target->CastNPC();
        if (talkNpc == nullptr) {
            const char16_t* const userName = user->GetName();
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] TalkNpc is not NPC User[%s]",
            //     L"UserSocket_HTMLCmd.cpp",
            //     0xa16,
            //     userName)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] TalkNpc is not NPC User[%s]",
            //     L"UserSocket_HTMLCmd.cpp",
            //     0xa16,
            //     userName)
            // L2_LOG_ERROR(Log,
            //     "Cannot process wedding proposal for %s: NPC target could not be cast to CNPC.",
            //     WideToUtf8(userName).c_str())
            L2_LOG_ERROR(
                Log, "Cannot process wedding proposal for %s: NPC target could not be cast to CNPC.",
                Utf16ToUtf8(userName).c_str()
            );
            return TalkStatus::Rejected;
        }

        const FVector& npcPosition = npcPositionStorage.emplace(talkNpc->GetPos());
        const FVector& userPosition = userPositionStorage.emplace(user->GetPos());
        const double deltaX = userPosition.x - npcPosition.x;
        const double deltaY = userPosition.y - npcPosition.y;
        if (deltaX * deltaX + deltaY * deltaY > 62500.0) {
            return TalkStatus::TooFar;
        }
        return TalkStatus::Ready;
    }();
    if (talkStatus == TalkStatus::Missing) {
        const std::uint32_t targetId = user->m_spTalkTarget.Sid();
        const char16_t* const userName = user->GetName();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] User[%s]'s talk target is NULL m_spTalkTarget[%d]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0xa16,
        //     userName,
        //     targetId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] User[%s]'s talk target is NULL m_spTalkTarget[%d]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0xa16,
        //     userName,
        //     targetId)
        // L2_LOG_WARN(Log,
        //     "Cannot process wedding proposal for %s: talk target object %u is missing.",
        //     WideToUtf8(userName).c_str(), static_cast<unsigned>(targetId))
        L2_LOG_WARN(
            Log, "Cannot process wedding proposal for %s: talk target object %u is missing.", Utf16ToUtf8(userName).c_str(),
            static_cast<unsigned>(targetId)
        );
        runtime::process::ReportCallStack(2, false);
        return false;
    }
    if (talkStatus == TalkStatus::Unknown) {
        const std::uint32_t targetId = unknownTargetId;
        const char16_t* const userName = user->GetName();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] User[%s]'s talk target is neither PC nor NPC [sid:%d]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0xa16,
        //     userName,
        //     targetId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] User[%s]'s talk target is neither PC nor NPC [sid:%d]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0xa16,
        //     userName,
        //     targetId)
        // L2_LOG_WARN(Log,
        //     "Cannot process wedding proposal for %s: talk target object %u is neither a player nor an NPC.",
        //     WideToUtf8(userName).c_str(), static_cast<unsigned>(targetId))
        L2_LOG_WARN(
            Log, "Cannot process wedding proposal for %s: talk target object %u is neither a player nor an NPC.",
            Utf16ToUtf8(userName).c_str(), static_cast<unsigned>(targetId)
        );
        return false;
    }
    if (talkStatus == TalkStatus::Rejected) {
        return false;
    }
    if (talkStatus == TalkStatus::TooFar) {
        const char16_t* const userName = user->GetName();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] DISTANCE OVER!! User[%s]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0xa16,
        //     userName)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] DISTANCE OVER!! User[%s]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0xa16,
        //     userName)
        // L2_LOG_WARN(Log,
        //     "Cannot process wedding proposal for %s: horizontal distance to NPC exceeds 250 units.",
        //     WideToUtf8(userName).c_str())
        L2_LOG_WARN(
            Log, "Cannot process wedding proposal for %s: horizontal distance to NPC exceeds 250 units.",
            Utf16ToUtf8(userName).c_str()
        );
        return false;
    }

    const CHTMLActionParamParser parameters = CHTMLActionParamParser::Parse(command);
    const char16_t* const brideName = parameters.GetStrValue(u"bride_name");
    if (brideName != nullptr) {
        user->AddWedding(std::u16string(brideName).c_str());
    }
    return false;
}

bool HtmlCmdOneTimeXmasTree(CUserSocket* socket, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c3bd3c.
    L2_GUARD_FUNCTION();
    if (user == nullptr || command == nullptr) {
        return false;
    }
    CEvent* const event = Singleton<CEventManager>::inst().GetEvent(GAME_EVENT::ET_ONETIME);
    auto* const oneTimeEvent = static_cast<CEvent_OneTimeEvent*>(event);
    if (oneTimeEvent == nullptr) {
        return false;
    }
    const XmasWishCardResult::Enum result = oneTimeEvent->IsUserReceived(static_cast<int>(user->GetDBID()));
    if (result == XmasWishCardResult::XWCR_ALREADY_RECEIVED) {
        user->SendSystemMessage(SystemMessageId::WISH_ALREADY_REGISTERED_AND_REWARDED);
        socket->Send("chd", 0xfe, 0x14c, 2);
    } else if (result == XmasWishCardResult::XWCR_UNKNOWN) {
        user->SendDB("chd", 0xea, 0x171, user->GetDBID());
    }
    return false;
}

bool HtmlCmdShowPcCafeCouponUI(CUserSocket* socket, User* user, const char16_t*, const char16_t*) {
    // Current L2Server 0x00c33abc.
    L2_GUARD_FUNCTION();
    if (ResolveNearbyTalkNpc(user, 0x978) != nullptr) {
        socket->Send("ch", 0xfe, 0x45);
    }
    return false;
}

bool HtmlCmdItemAuction(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c33ddc.
    L2_GUARD_FUNCTION();
    enum class TalkStatus { Ready, Rejected, Missing, Unknown, TooFar };
    std::uint32_t unknownTargetId;
    std::optional<FVector> npcPositionStorage;
    std::optional<FVector> userPositionStorage;
    CNPC* npc = nullptr;
    const TalkStatus talkStatus = [&] {
        L2_GUARD(*user->m_lock);
        CObject* const target = user->m_spTalkTarget.GetObjectW();
        if (target == nullptr) {
            return TalkStatus::Missing;
        }

        if (!target->IsNPC()) {
            if (!target->IsPC()) {
                unknownTargetId = user->m_spTalkTarget.GetId();
                return TalkStatus::Unknown;
            } else {
                const char16_t* const targetName = target->GetName();
                const char16_t* const userName = user->GetName();
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] User[%s]'s talk target is PC[%s]",
                //     L"UserSocket_HTMLCmd.cpp",
                //     0x99a,
                //     userName,
                //     targetName)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] User[%s]'s talk target is PC[%s]",
                //     L"UserSocket_HTMLCmd.cpp",
                //     0x99a,
                //     userName,
                //     targetName)
                // L2_LOG_WARN(Log,
                //     "Cannot handle item auction command for %s: talk target %s is a player.",
                //     WideToUtf8(userName).c_str(), WideToUtf8(targetName).c_str())
                L2_LOG_WARN(
                    Log, "Cannot handle item auction command for %s: talk target %s is a player.",
                    Utf16ToUtf8(userName).c_str(), Utf16ToUtf8(targetName).c_str()
                );
            }
            return TalkStatus::Rejected;
        }

        npc = target->CastNPC();
        if (npc == nullptr) {
            const char16_t* const userName = user->GetName();
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] TalkNpc is not NPC User[%s]",
            //     L"UserSocket_HTMLCmd.cpp",
            //     0x99a,
            //     userName)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] TalkNpc is not NPC User[%s]",
            //     L"UserSocket_HTMLCmd.cpp",
            //     0x99a,
            //     userName)
            // L2_LOG_ERROR(Log,
            //     "Cannot handle item auction command for %s: NPC target could not be cast to CNPC.",
            //     WideToUtf8(userName).c_str())
            L2_LOG_ERROR(
                Log, "Cannot handle item auction command for %s: NPC target could not be cast to CNPC.",
                Utf16ToUtf8(userName).c_str()
            );
            return TalkStatus::Rejected;
        }

        const FVector& npcPosition = npcPositionStorage.emplace(npc->GetPos());
        const FVector& userPosition = userPositionStorage.emplace(user->GetPos());
        const double x = userPosition.x - npcPosition.x;
        const double y = userPosition.y - npcPosition.y;
        if (!(x * x + y * y > 62500.0)) {
            return TalkStatus::Ready;
        }

        return TalkStatus::TooFar;
    }();
    if (talkStatus == TalkStatus::Missing) {
        const std::uint32_t targetId = user->m_spTalkTarget.GetId();
        const char16_t* const userName = user->GetName();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] User[%s]'s talk target is NULL m_spTalkTarget[%d]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x99a,
        //     userName,
        //     targetId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] User[%s]'s talk target is NULL m_spTalkTarget[%d]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x99a,
        //     userName,
        //     targetId)
        // L2_LOG_WARN(Log,
        //     "Cannot handle item auction command for %s: talk target object %u is missing.",
        //     WideToUtf8(userName).c_str(), static_cast<unsigned>(targetId))
        L2_LOG_WARN(
            Log, "Cannot handle item auction command for %s: talk target object %u is missing.",
            Utf16ToUtf8(userName).c_str(), static_cast<unsigned>(targetId)
        );
        runtime::process::ReportCallStack(LOG_ERROR, false);
        return false;
    }
    if (talkStatus == TalkStatus::Unknown) {
        const std::uint32_t targetId = unknownTargetId;
        const char16_t* const userName = user->GetName();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] User[%s]'s talk target is neither PC nor NPC [sid:%d]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x99a,
        //     userName,
        //     targetId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] User[%s]'s talk target is neither PC nor NPC [sid:%d]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x99a,
        //     userName,
        //     targetId)
        // L2_LOG_WARN(Log,
        //     "Cannot handle item auction command for %s: talk target object %u is neither a player nor an NPC.",
        //     WideToUtf8(userName).c_str(), static_cast<unsigned>(targetId))
        L2_LOG_WARN(
            Log, "Cannot handle item auction command for %s: talk target object %u is neither a player nor an NPC.",
            Utf16ToUtf8(userName).c_str(), static_cast<unsigned>(targetId)
        );
        return false;
    }
    if (talkStatus == TalkStatus::Rejected) {
        return false;
    }
    if (talkStatus == TalkStatus::Ready) {
        CAuctionHtmlCommandHandler::Handle(npc, user, command);
        return false;
    }
    const char16_t* const userName = user->GetName();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] DISTANCE OVER!! User[%s]",
    //     L"UserSocket_HTMLCmd.cpp",
    //     0x99a,
    //     userName)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] DISTANCE OVER!! User[%s]",
    //     L"UserSocket_HTMLCmd.cpp",
    //     0x99a,
    //     userName)
    // L2_LOG_WARN(Log,
    //     "Cannot handle item auction command for %s: NPC is not within the allowed horizontal distance of 250 units.",
    //     WideToUtf8(userName).c_str())
    L2_LOG_WARN(
        Log, "Cannot handle item auction command for %s: NPC is not within the allowed horizontal distance of 250 units.",
        Utf16ToUtf8(userName).c_str()
    );
    return false;
}

bool HtmlCmdPetitionLink(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c399e8.
    L2_GUARD_FUNCTION();
    const CHTMLActionParamParser parameters = CHTMLActionParamParser::Parse(command);
    petitionCli.ShowConTents(user, parameters.GetIntValue(u"message_id"));
    return false;
}

bool HtmlCmdPledgeGame(CUserSocket* socket, User*, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c340e8.
    L2_GUARD_FUNCTION();
    CPledgeGameHtmlCommandHandler::Handle(socket, command);
    return false;
}

bool HtmlCmdEventCampaignAccept(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c3b570.
    L2_GUARD_FUNCTION();
    if (user != nullptr && command != nullptr) {
        const CHTMLActionParamParser parameters = CHTMLActionParamParser::Parse(command);
        CEventCampaignManager::Inst()->OnAcceptEventCampaign(
            *user, parameters.GetIntValue(u"event_campaign_id"), parameters.GetIntValue(u"step_id"),
            parameters.GetIntValue(u"goal_group_id")
        );
    }
    return false;
}

bool HtmlCmdEventCampaignReward(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c3b6c0.
    L2_GUARD_FUNCTION();
    if (user != nullptr && command != nullptr) {
        const CHTMLActionParamParser parameters = CHTMLActionParamParser::Parse(command);
        CEventCampaignManager::Inst()->OnRequestReward(
            *user, parameters.GetIntValue(u"event_campaign_id"), parameters.GetIntValue(u"step_id"),
            parameters.GetIntValue(u"goal_group_id")
        );
    }
    return false;
}

bool HtmlCmdEventCampaignRanking(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c3b810.
    L2_GUARD_FUNCTION();
    if (user != nullptr && command != nullptr) {
        const CHTMLActionParamParser parameters = CHTMLActionParamParser::Parse(command);
        CEventCampaignManager::Inst()->OnRequestRanking(
            *user, parameters.GetIntValue(u"event_campaign_id"), parameters.GetIntValue(u"step_id")
        );
    }
    return false;
}

bool HtmlCmdWrite(CUserSocket*, User*, const char16_t*, const char16_t*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool HtmlCmdMultiSell(CUserSocket*, User*, const char16_t*, const char16_t*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] don't use multisell html cmd",
    //     L"UserSocket_HTMLCmd.cpp",
    //     0x367)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] don't use multisell html cmd",
    //     L"UserSocket_HTMLCmd.cpp",
    //     0x367)
    L2_LOG_WARN(Log, "Ignoring unsupported multisell HTML command.");
    return false;
}

bool HtmlCmdShowMultisell(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c38a48.
    L2_GUARD_FUNCTION();
    const CHTMLActionParamParser parameters = CHTMLActionParamParser::Parse(command);
    user->ShowMultiSell(0, parameters.GetIntValue(u"groupid"));
    return false;
}

bool HtmlCmdShowRadar(CUserSocket* socket, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c3af70.
    L2_GUARD_FUNCTION();
    if (ResolveNearbyTalkNpc(user, 0x956) == nullptr) {
        return false;
    }

    const CHTMLActionParamParser parameters = CHTMLActionParamParser::Parse(command);
    const std::int64_t id = parameters.GetInt64Value(u"id");
    const std::int32_t index = parameters.GetIntValue(u"index");

    {
        L2_GUARD(s_mapTelPosListSect);
        const auto found = s_mapTelPosList.find(id);
        if (found != s_mapTelPosList.end() && index >= 0 && static_cast<std::size_t>(index) < found->second->size()) {
            const TelPos& position = runtime::CheckedVectorAt((*found->second), static_cast<std::size_t>(index));
            socket->Send("cddddd", 0xf1, 2, 2, position.m_nX, position.m_nY, position.m_nZ);
            socket->Send("cddddd", 0xf1, 0, 1, position.m_nX, position.m_nY, position.m_nZ);
        }
    }
    return false;
}

bool CommunityBBS(CSocket*, const SharedUtilsWideChar*);
bool ClanBBS(CSocket*, const SharedUtilsWideChar*);
bool MemoHTML(CSocket*, const SharedUtilsWideChar*);
bool AgitHTML(CSocket*, const SharedUtilsWideChar*);
bool MonRaceHTML(CSocket*, const SharedUtilsWideChar*);

bool HtmlCmdBBS(CUserSocket* socket, User*, const char16_t*, const char16_t* fullCommand) {
    // Current L2Server 0x00c2f4d4.
    L2_GUARD_FUNCTION();
    static_cast<void>(CommunityBBS(socket, AsTargetText(fullCommand)));
    return false;
}

bool HtmlCmdClanBBS(CUserSocket* socket, User*, const char16_t*, const char16_t* fullCommand) {
    // Current L2Server 0x00c2f588.
    L2_GUARD_FUNCTION();
    static_cast<void>(ClanBBS(socket, AsTargetText(fullCommand)));
    return false;
}

bool HtmlCmdMemo(CUserSocket* socket, User*, const char16_t*, const char16_t* fullCommand) {
    // Current L2Server 0x00c2f63c.
    L2_GUARD_FUNCTION();
    static_cast<void>(MemoHTML(socket, AsTargetText(fullCommand)));
    return false;
}

bool HtmlCmdAgit(CUserSocket* socket, User* user, const char16_t*, const char16_t* fullCommand) {
    // Current L2Server 0x00c2f6f0.
    L2_GUARD_FUNCTION();
    enum class TalkStatus { Ready, Rejected, Missing, Unknown, TooFar };
    std::uint32_t unknownTargetId;
    std::optional<FVector> npcPositionStorage;
    std::optional<FVector> userPositionStorage;
    const TalkStatus talkStatus = [&] {
        L2_GUARD(*user->m_lock);
        CObject* const target = user->m_spTalkTarget.GetObjectW();
        if (target == nullptr) {
            return TalkStatus::Missing;
        }
        if (!target->IsNPC()) {
            if (!target->IsPC()) {
                unknownTargetId = user->m_spTalkTarget.GetId();
                return TalkStatus::Unknown;
            } else {
                const char16_t* const targetName = target->GetName();
                const char16_t* const userName = user->GetName();
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] User[%s]'s talk target is PC[%s]",
                //     L"UserSocket_HTMLCmd.cpp",
                //     0x34d,
                //     userName,
                //     targetName)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] User[%s]'s talk target is PC[%s]",
                //     L"UserSocket_HTMLCmd.cpp",
                //     0x34d,
                //     userName,
                //     targetName)
                // L2_LOG_WARN(Log,
                //     "Cannot handle clan hall command for %s: talk target %s is a player.",
                //     WideToUtf8(userName).c_str(), WideToUtf8(targetName).c_str())
                L2_LOG_WARN(
                    Log, "Cannot handle clan hall command for %s: talk target %s is a player.",
                    Utf16ToUtf8(userName).c_str(), Utf16ToUtf8(targetName).c_str()
                );
            }
            return TalkStatus::Rejected;
        }

        CNPC* const npc = target->CastNPC();
        if (npc == nullptr) {
            const char16_t* const userName = user->GetName();
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] TalkNpc is not NPC User[%s]",
            //     L"UserSocket_HTMLCmd.cpp",
            //     0x34d,
            //     userName)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] TalkNpc is not NPC User[%s]",
            //     L"UserSocket_HTMLCmd.cpp",
            //     0x34d,
            //     userName)
            // L2_LOG_ERROR(Log,
            //     "Cannot handle clan hall command for %s: NPC target could not be cast to CNPC.",
            //     WideToUtf8(userName).c_str())
            L2_LOG_ERROR(
                Log, "Cannot handle clan hall command for %s: NPC target could not be cast to CNPC.",
                Utf16ToUtf8(userName).c_str()
            );
            return TalkStatus::Rejected;
        }

        const FVector& npcPosition = npcPositionStorage.emplace(npc->GetPos());
        const FVector& userPosition = userPositionStorage.emplace(user->GetPos());
        const double x = userPosition.x - npcPosition.x;
        const double y = userPosition.y - npcPosition.y;
        if (x * x + y * y <= 62500.0) {
            return TalkStatus::Ready;
        }

        return TalkStatus::TooFar;
    }();
    if (talkStatus == TalkStatus::Missing) {
        const std::uint32_t targetId = user->m_spTalkTarget.GetId();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] User[%s]'s talk target is NULL m_spTalkTarget[%d]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x34d,
        //     user->GetName(),
        //     targetId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] User[%s]'s talk target is NULL m_spTalkTarget[%d]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x34d,
        //     user->GetName(),
        //     targetId)
        // L2_LOG_WARN(Log,
        //     "Cannot handle clan hall command for %s: talk target object %u is missing.",
        //     WideToUtf8(user->GetName()).c_str(), static_cast<unsigned>(targetId))
        L2_LOG_WARN(
            Log, "Cannot handle clan hall command for %s: talk target object %u is missing.",
            Utf16ToUtf8(user->GetName()).c_str(), static_cast<unsigned>(targetId)
        );
        return false;
    }
    if (talkStatus == TalkStatus::Unknown) {
        const std::uint32_t targetId = unknownTargetId;
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] User[%s]'s talk target is neither PC nor NPC [sid:%d]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x34d,
        //     user->GetName(),
        //     targetId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] User[%s]'s talk target is neither PC nor NPC [sid:%d]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x34d,
        //     user->GetName(),
        //     targetId)
        // L2_LOG_WARN(Log,
        //     "Cannot handle clan hall command for %s: talk target object %u is neither a player nor an NPC.",
        //     WideToUtf8(user->GetName()).c_str(), static_cast<unsigned>(targetId))
        L2_LOG_WARN(
            Log, "Cannot handle clan hall command for %s: talk target object %u is neither a player nor an NPC.",
            Utf16ToUtf8(user->GetName()).c_str(), static_cast<unsigned>(targetId)
        );
        return false;
    }
    if (talkStatus == TalkStatus::Rejected) {
        return false;
    }
    if (talkStatus == TalkStatus::Ready) {
        static_cast<void>(AgitHTML(socket, AsTargetText(fullCommand)));
        return false;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] DISTANCE OVER!! User[%s]",
    //     L"UserSocket_HTMLCmd.cpp",
    //     0x34d,
    //     user->GetName())
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] DISTANCE OVER!! User[%s]",
    //     L"UserSocket_HTMLCmd.cpp",
    //     0x34d,
    //     user->GetName())
    // L2_LOG_WARN(Log,
    //     "Cannot handle clan hall command for %s: NPC is not within the allowed horizontal distance of 250 units.",
    //     WideToUtf8(user->GetName()).c_str())
    L2_LOG_WARN(
        Log, "Cannot handle clan hall command for %s: NPC is not within the allowed horizontal distance of 250 units.",
        Utf16ToUtf8(user->GetName()).c_str()
    );
    return false;
}

bool HtmlCmdMonsterRace(CUserSocket* socket, User* user, const char16_t*, const char16_t* fullCommand) {
    // Current L2Server 0x00c2fa08.
    L2_GUARD_FUNCTION();
    enum class TalkStatus { Ready, Rejected, Missing, Unknown, TooFar };
    std::uint32_t unknownTargetId;
    std::optional<FVector> npcPositionStorage;
    std::optional<FVector> userPositionStorage;
    const TalkStatus talkStatus = [&] {
        L2_GUARD(*user->m_lock);
        CObject* const target = user->m_spTalkTarget.GetObjectW();
        if (target == nullptr) {
            return TalkStatus::Missing;
        }
        if (!target->IsNPC()) {
            if (!target->IsPC()) {
                unknownTargetId = user->m_spTalkTarget.GetId();
                return TalkStatus::Unknown;
            } else {
                const char16_t* const targetName = target->GetName();
                const char16_t* const userName = user->GetName();
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] User[%s]'s talk target is PC[%s]",
                //     L"UserSocket_HTMLCmd.cpp",
                //     0x35a,
                //     userName,
                //     targetName)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] User[%s]'s talk target is PC[%s]",
                //     L"UserSocket_HTMLCmd.cpp",
                //     0x35a,
                //     userName,
                //     targetName)
                // L2_LOG_WARN(Log,
                //     "Cannot handle monster race command for %s: talk target %s is a player.",
                //     WideToUtf8(userName).c_str(), WideToUtf8(targetName).c_str())
                L2_LOG_WARN(
                    Log, "Cannot handle monster race command for %s: talk target %s is a player.",
                    Utf16ToUtf8(userName).c_str(), Utf16ToUtf8(targetName).c_str()
                );
            }
            return TalkStatus::Rejected;
        }

        CNPC* const npc = target->CastNPC();
        if (npc == nullptr) {
            const char16_t* const userName = user->GetName();
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] TalkNpc is not NPC User[%s]",
            //     L"UserSocket_HTMLCmd.cpp",
            //     0x35a,
            //     userName)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] TalkNpc is not NPC User[%s]",
            //     L"UserSocket_HTMLCmd.cpp",
            //     0x35a,
            //     userName)
            // L2_LOG_ERROR(Log,
            //     "Cannot handle monster race command for %s: NPC target could not be cast to CNPC.",
            //     WideToUtf8(userName).c_str())
            L2_LOG_ERROR(
                Log, "Cannot handle monster race command for %s: NPC target could not be cast to CNPC.",
                Utf16ToUtf8(userName).c_str()
            );
            return TalkStatus::Rejected;
        }

        const FVector& npcPosition = npcPositionStorage.emplace(npc->GetPos());
        const FVector& userPosition = userPositionStorage.emplace(user->GetPos());
        const double x = userPosition.x - npcPosition.x;
        const double y = userPosition.y - npcPosition.y;
        if (x * x + y * y <= 62500.0) {
            return TalkStatus::Ready;
        }

        return TalkStatus::TooFar;
    }();
    if (talkStatus == TalkStatus::Missing) {
        const std::uint32_t targetId = user->m_spTalkTarget.GetId();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] User[%s]'s talk target is NULL m_spTalkTarget[%d]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x35a,
        //     user->GetName(),
        //     targetId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] User[%s]'s talk target is NULL m_spTalkTarget[%d]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x35a,
        //     user->GetName(),
        //     targetId)
        // L2_LOG_WARN(Log,
        //     "Cannot handle monster race command for %s: talk target object %u is missing.",
        //     WideToUtf8(user->GetName()).c_str(), static_cast<unsigned>(targetId))
        L2_LOG_WARN(
            Log, "Cannot handle monster race command for %s: talk target object %u is missing.",
            Utf16ToUtf8(user->GetName()).c_str(), static_cast<unsigned>(targetId)
        );
        return false;
    }
    if (talkStatus == TalkStatus::Unknown) {
        const std::uint32_t targetId = unknownTargetId;
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] User[%s]'s talk target is neither PC nor NPC [sid:%d]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x35a,
        //     user->GetName(),
        //     targetId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] User[%s]'s talk target is neither PC nor NPC [sid:%d]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x35a,
        //     user->GetName(),
        //     targetId)
        // L2_LOG_WARN(Log,
        //     "Cannot handle monster race command for %s: talk target object %u is neither a player nor an NPC.",
        //     WideToUtf8(user->GetName()).c_str(), static_cast<unsigned>(targetId))
        L2_LOG_WARN(
            Log, "Cannot handle monster race command for %s: talk target object %u is neither a player nor an NPC.",
            Utf16ToUtf8(user->GetName()).c_str(), static_cast<unsigned>(targetId)
        );
        return false;
    }
    if (talkStatus == TalkStatus::Rejected) {
        return false;
    }
    if (talkStatus == TalkStatus::Ready) {
        static_cast<void>(MonRaceHTML(socket, AsTargetText(fullCommand)));
        return false;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] DISTANCE OVER!! User[%s]",
    //     L"UserSocket_HTMLCmd.cpp",
    //     0x35a,
    //     user->GetName())
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] DISTANCE OVER!! User[%s]",
    //     L"UserSocket_HTMLCmd.cpp",
    //     0x35a,
    //     user->GetName())
    // L2_LOG_WARN(Log,
    //     "Cannot handle monster race command for %s: NPC is not within the allowed horizontal distance of 250 units.",
    //     WideToUtf8(user->GetName()).c_str())
    L2_LOG_WARN(
        Log, "Cannot handle monster race command for %s: NPC is not within the allowed horizontal distance of 250 units.",
        Utf16ToUtf8(user->GetName()).c_str()
    );
    return false;
}

bool HtmlCmdTeleportRequest(CUserSocket* socket, User* user, const char16_t*, const char16_t*) {
    // Current L2Server 0x00c2fe58. Unlike the buy/sell routes, the current
    // distance-failure branch logs without sending ActionFailed.
    L2_GUARD_FUNCTION();
    CNPC* const npc = ResolveNearbyTalkNpc(user, 0x3a2, false);
    if (npc != nullptr) {
        g_npcserver.Send("cdd", 7, socket->GetUser()->GetIndex(), npc->m_data->index);
    }
    return false;
}

bool HtmlCmdBuy(CUserSocket* socket, User* user, const char16_t*, const char16_t*) {
    // Current L2Server 0x00c301a8.
    L2_GUARD_FUNCTION();
    CNPC* const npc = ResolveNearbyTalkNpc(user, 0x4ab);
    if (npc == nullptr) {
        return false;
    }
    int privateStoreType = 0;
    CSharedCreatureData* npcData = nullptr;
    {
        L2_GUARD(*user->m_lock);
        privateStoreType = user->GetPrivateStoreType();
        npcData = npc->m_data;
    }
    if (privateStoreType != 0) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::ALREADY_TRADING);
        // user->ActionFailed(0)
        user->ActionFailed(AF_DEFAULT);
        return false;
    }
    if (npcData == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] what?",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x4bf)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] what?",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x4bf)
        L2_LOG_ERROR(Log, "Cannot process buy HTML command: NPC shared data is missing.");
        return false;
    }
    g_npcserver.Send("cdd", 8, socket->GetUser()->GetIndex(), npcData->index);
    return false;
}

bool HtmlCmdSell(CUserSocket* socket, User* user, const char16_t*, const char16_t*) {
    // Current L2Server 0x00c30594.
    L2_GUARD_FUNCTION();
    CNPC* const npc = ResolveNearbyTalkNpc(user, 0x4cc);
    if (npc == nullptr) {
        return false;
    }
    int privateStoreType = 0;
    CSharedCreatureData* npcData = nullptr;
    {
        L2_GUARD(*user->m_lock);
        privateStoreType = user->GetPrivateStoreType();
        npcData = npc->m_data;
    }
    if (privateStoreType != 0) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::ALREADY_TRADING);
        // user->ActionFailed(0)
        user->ActionFailed(AF_DEFAULT);
        return false;
    }
    if (npcData == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] what?",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x4e0)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] what?",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x4e0)
        L2_LOG_ERROR(Log, "Cannot process sell HTML command: NPC shared data is missing.");
        return false;
    }
    g_npcserver.Send("cdd", 9, socket->GetUser()->GetIndex(), npcData->index);
    return false;
}

bool HtmlCmdDeposit(CUserSocket*, User* user, const char16_t*, const char16_t*) {
    // Current L2Server 0x00c30f28.
    L2_GUARD_FUNCTION();
    CNPC* const npc = ResolveNearbyTalkNpc(user, 0x632);
    if (npc == nullptr || npc->m_data == nullptr) {
        return false;
    }

    bool privateStore = false;
    CWareHouseDeposit* warehouse = nullptr;
    {
        L2_GUARD(*user->m_lock);
        if (user->GetPrivateStoreType() != 0) {
            privateStore = true;
        } else {
            const int npcClass = npc->m_data->m_nClass;
            if (npcClass == CT_TELEPORTER || npcClass == CT_WAREHOUSE_KEEPER) {
                warehouse = new CWareHouseDeposit(user, npc, 0, 0);
                L2_ADD_REF(warehouse, ObjectRefType::Anonymous);
                warehouse->SetCreatureData(npc);
                user->ReleaseEconomy();
                user->m_pEconomy = warehouse;
            }
        }
    }
    if (privateStore) {
        user->GetSocket()->SendSystemMessage(
            SystemMessageId::CANNOT_DROP_DESTROY_OR_TRADE_ITEMS_DURING_PRIVATE_STORE_OR_WORKSHOP
        );
        // user->ActionFailed(0)
        user->ActionFailed(AF_DEFAULT);
        return false;
    }
    if (warehouse == nullptr) {
        return false;
    }
    warehouse->SendList(WHC_NORMAL);
    L2_RELEASE(warehouse, ObjectRefType::Anonymous);
    return false;
}

bool HtmlCmdWithdraw(CUserSocket*, User* user, const char16_t*, const char16_t*) {
    // Current L2Server 0x00c31378.
    L2_GUARD_FUNCTION();
    CNPC* const npc = ResolveNearbyTalkNpc(user, 0x65f);
    if (npc == nullptr || npc->m_data == nullptr) {
        return false;
    }

    bool privateStore = false;
    CWareHouseWithdraw* warehouse = nullptr;
    {
        L2_GUARD(*user->m_lock);
        if (user->GetPrivateStoreType() != 0) {
            privateStore = true;
        } else {
            const int npcClass = npc->m_data->m_nClass;
            if (npcClass == CT_TELEPORTER || npcClass == CT_WAREHOUSE_KEEPER || npcClass == CT_PACKAGE_KEEPER) {
                warehouse = new CWareHouseWithdraw(user, npc, 0, 0);
                L2_ADD_REF(warehouse, ObjectRefType::Anonymous);
                warehouse->SetCreatureData(npc);
                user->ReleaseEconomy();
                user->m_pEconomy = warehouse;
            }
        }
    }
    if (privateStore) {
        user->GetSocket()->SendSystemMessage(
            SystemMessageId::CANNOT_DROP_DESTROY_OR_TRADE_ITEMS_DURING_PRIVATE_STORE_OR_WORKSHOP
        );
        // user->ActionFailed(0)
        user->ActionFailed(AF_DEFAULT);
        return false;
    }
    if (warehouse == nullptr) {
        return false;
    }
    warehouse->SendList(WHC_NORMAL);
    L2_RELEASE(warehouse, ObjectRefType::Anonymous);
    return false;
}

bool HtmlCmdPacketDeposit(CUserSocket*, User* user, const char16_t*, const char16_t*) {
    // Current L2Server 0x00c317cc.
    L2_GUARD_FUNCTION();
    CNPC* const npc = ResolveNearbyTalkNpc(user, 0x68c);
    if (npc == nullptr || npc->m_data == nullptr) {
        return false;
    }

    bool privateStore = false;
    CWareHouseDeposit* warehouse = nullptr;
    {
        L2_GUARD(*user->m_lock);
        if (user->GetPrivateStoreType() != 0) {
            privateStore = true;
        } else {
            const int npcClass = npc->m_data->m_nClass;
            if (npcClass == CT_TELEPORTER || npcClass == CT_PACKAGE_KEEPER) {
                warehouse = new CWareHouseDeposit(user, npc, 3, npc->m_data->m_nClassID);
                L2_ADD_REF(warehouse, ObjectRefType::Anonymous);
                warehouse->SetCreatureData(npc);
                user->ReleaseEconomy();
                user->m_pEconomy = warehouse;
            }
        }
    }
    if (privateStore) {
        user->GetSocket()->SendSystemMessage(
            SystemMessageId::CANNOT_DROP_DESTROY_OR_TRADE_ITEMS_DURING_PRIVATE_STORE_OR_WORKSHOP
        );
        // user->ActionFailed(0)
        user->ActionFailed(AF_DEFAULT);
        return false;
    }
    if (warehouse == nullptr) {
        return false;
    }
    user->SendSendableCharacterList();
    L2_RELEASE(warehouse, ObjectRefType::Anonymous);
    return false;
}

bool HtmlCmdPacketWithdraw(CUserSocket*, User* user, const char16_t*, const char16_t*) {
    // Current L2Server 0x00c31c24.
    L2_GUARD_FUNCTION();
    CNPC* const npc = ResolveNearbyTalkNpc(user, 0x6b9);
    if (npc == nullptr || npc->m_data == nullptr) {
        return false;
    }

    bool privateStore = false;
    CWareHouseWithdraw* warehouse = nullptr;
    {
        L2_GUARD(*user->m_lock);
        if (user->GetPrivateStoreType() != 0) {
            privateStore = true;
        } else {
            const int npcClass = npc->m_data->m_nClass;
            if (npcClass == CT_TELEPORTER || npcClass == CT_PACKAGE_KEEPER) {
                warehouse = new CWareHouseWithdraw(user, npc, 3, npc->m_data->m_nClassID);
                L2_ADD_REF(warehouse, ObjectRefType::Anonymous);
                warehouse->SetCreatureData(npc);
                user->ReleaseEconomy();
                user->m_pEconomy = warehouse;
            }
        }
    }
    if (privateStore) {
        user->GetSocket()->SendSystemMessage(
            SystemMessageId::CANNOT_DROP_DESTROY_OR_TRADE_ITEMS_DURING_PRIVATE_STORE_OR_WORKSHOP
        );
        // user->ActionFailed(0)
        user->ActionFailed(AF_DEFAULT);
        return false;
    }
    if (warehouse == nullptr) {
        return false;
    }
    warehouse->SendList(WHC_ETC);
    L2_RELEASE(warehouse, ObjectRefType::Anonymous);
    return false;
}

bool HtmlCmdDepositCastle(CUserSocket*, User* user, const char16_t*, const char16_t*) {
    // Current L2Server 0x00c3419c.
    L2_GUARD_FUNCTION();
    CNPC* const npc = ResolveNearbyTalkNpc(user, 0x4ee);
    if (npc == nullptr || npc->m_data == nullptr) {
        return false;
    }

    {
        L2_GUARD(*user->m_lock);
        if (user->GetPrivateStoreType() != 0) {
            user->GetSocket()->SendSystemMessage(
                SystemMessageId::CANNOT_DROP_DESTROY_OR_TRADE_ITEMS_DURING_PRIVATE_STORE_OR_WORKSHOP
            );
            // user->ActionFailed(0)
            user->ActionFailed(AF_DEFAULT);
            return false;
        }
        if (user->GetPledge() == nullptr) {
            user->GetSocket()->SendSystemMessage(SystemMessageId::NO_AUTHORITY_TO_USE_CASTLE_WAREHOUSE);
            return false;
        }
        const int npcClass = npc->m_data->m_nClass;
        if (npcClass != CT_TELEPORTER && npcClass != CT_WAREHOUSE_KEEPER && npcClass != CT_PACKAGE_KEEPER) {
            return false;
        }
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(npc->m_data->m_nResidenceID);
    CPledge* const ownerPledge = residence != nullptr ? residence->GetResidenceOwnerPledge() : nullptr;
    CPledge* const userPledge = user->GetPledge();
    if (ownerPledge == nullptr || userPledge == nullptr) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::NO_AUTHORITY_TO_USE_CASTLE_WAREHOUSE);
        return false;
    }
    const std::uint32_t userPledgeId = userPledge->GetDbId();
    const std::uint32_t ownerPledgeId = ownerPledge->GetDbId();
    if (ownerPledgeId != userPledgeId) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::NO_AUTHORITY_TO_USE_CASTLE_WAREHOUSE);
        return false;
    }
    const std::uint32_t userDatabaseId = static_cast<std::uint32_t>(user->GetDBID());
    const std::uint32_t ownerMasterId = ownerPledge->GetMasterDbId();
    if (ownerMasterId != userDatabaseId) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::NO_AUTHORITY_TO_USE_CASTLE_WAREHOUSE);
        return false;
    }

    CWareHouseDeposit* warehouse = nullptr;
    {
        L2_GUARD(*user->m_lock);
        warehouse = new CWareHouseDeposit(user, npc, 1, residence->GetResidenceID());
        L2_ADD_REF(warehouse, ObjectRefType::Anonymous);
        warehouse->SetCreatureData(npc);
        user->ReleaseEconomy();
        user->m_pEconomy = warehouse;
    }
    warehouse->SendListEx(WHC_CASTLE);
    L2_RELEASE(warehouse, ObjectRefType::Anonymous);
    return false;
}

bool HtmlCmdWithdrawCastle(CUserSocket*, User* user, const char16_t*, const char16_t*) {
    // Current L2Server 0x00c30980.
    L2_GUARD_FUNCTION();
    CNPC* const npc = ResolveNearbyTalkNpc(user, 0x540);
    if (npc == nullptr || npc->m_data == nullptr) {
        return false;
    }

    bool privateStore = false;
    {
        L2_GUARD(*user->m_lock);
        if (user->GetPrivateStoreType() != 0) {
            privateStore = true;
        } else {
            if (user->GetPledge() == nullptr) {
                user->GetSocket()->SendSystemMessage(SystemMessageId::NO_AUTHORITY_TO_USE_CASTLE_WAREHOUSE);
                return false;
            }
            const int npcClass = npc->m_data->m_nClass;
            if (npcClass != CT_TELEPORTER && npcClass != CT_WAREHOUSE_KEEPER && npcClass != CT_PACKAGE_KEEPER) {
                return false;
            }
        }
    }
    if (privateStore) {
        user->GetSocket()->SendSystemMessage(
            SystemMessageId::CANNOT_DROP_DESTROY_OR_TRADE_ITEMS_DURING_PRIVATE_STORE_OR_WORKSHOP
        );
        // user->ActionFailed(0)
        user->ActionFailed(AF_DEFAULT);
        return false;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(npc->m_data->m_nResidenceID);
    // The current withdrawal route performs no null-residence
    // guard before asking the residence for its owner pledge.
    CPledge* const ownerPledge = residence->GetResidenceOwnerPledge();
    CPledge* const userPledge = user->GetPledge();
    if (ownerPledge == nullptr || userPledge == nullptr) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::NO_AUTHORITY_TO_USE_CASTLE_WAREHOUSE);
        return false;
    }
    const std::uint32_t userPledgeId = userPledge->GetDbId();
    const std::uint32_t ownerPledgeId = ownerPledge->GetDbId();
    if (ownerPledgeId != userPledgeId) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::NO_AUTHORITY_TO_USE_CASTLE_WAREHOUSE);
        return false;
    }
    const std::uint32_t userDatabaseId = static_cast<std::uint32_t>(user->GetDBID());
    const std::uint32_t ownerMasterId = ownerPledge->GetMasterDbId();
    if (ownerMasterId != userDatabaseId) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::NO_AUTHORITY_TO_USE_CASTLE_WAREHOUSE);
        return false;
    }

    CWareHouseWithdraw* warehouse = nullptr;
    {
        L2_GUARD(*user->m_lock);
        warehouse = new CWareHouseWithdraw(user, npc, 1, residence->GetResidenceID());
        L2_ADD_REF(warehouse, ObjectRefType::Anonymous);
        warehouse->SetCreatureData(npc);
        user->ReleaseEconomy();
        user->m_pEconomy = warehouse;
    }
    warehouse->SendListEx(WHC_CASTLE);
    L2_RELEASE(warehouse, ObjectRefType::Anonymous);
    return false;
}

bool HtmlCmdDepositPledge(CUserSocket*, User* user, const char16_t*, const char16_t*) {
    // Current L2Server 0x00c354d8.
    L2_GUARD_FUNCTION();
    CNPC* const npc = ResolveNearbyTalkNpc(user, 0x592);
    if (npc == nullptr || npc->m_data == nullptr) {
        return false;
    }

    {
        L2_GUARD(*user->m_lock);
        if (user->GetSocket()->m_bEconomicRestriction) {
            user->GetSocket()->SendSystemMessage(SystemMessageId::ACCOUNT_CANNOT_USE_CLAN_WAREHOUSE);
            return false;
        }
        if (user->GetPrivateStoreType() != 0) {
            user->GetSocket()->SendSystemMessage(
                SystemMessageId::CANNOT_DROP_DESTROY_OR_TRADE_ITEMS_DURING_PRIVATE_STORE_OR_WORKSHOP
            );
            // user->ActionFailed(0)
            user->ActionFailed(AF_DEFAULT);
            return false;
        }
        const int npcClass = npc->m_data->m_nClass;
        if (npcClass != CT_TELEPORTER && npcClass != CT_WAREHOUSE_KEEPER && npcClass != CT_PACKAGE_KEEPER) {
            return false;
        }
    }

    int deferredSystemMessage = 0;
    CWareHouseDeposit* warehouse = nullptr;
    {
        L2_GUARD(*user->m_lock);
        CPledge* const pledge = user->GetPledge();
        if (pledge == nullptr) {
            deferredSystemMessage = SystemMessageId::NO_AUTHORITY_TO_USE_CLAN_WAREHOUSE;
        } else {
            PledgeFunction* const function = pledge->GetFunction();
            if (!function->IsEnable(PledgeFunctionType::WAREHOUSE)) {
                const std::string bits =
                    std::bitset<PledgeFunctionType::MAX_COUNT>(PledgeFunction::Builder::GetBits(function)).to_string();
                // Log.Add(
                //     LOG_ERROR,
                //     L"[PLEDGE][%s][%d][NO_ERROR][FUNCTION] pledge warehouse failed. Function Bits : %S]",
                //     L"UserSocket_HTMLCmd.cpp",
                //     0x5c4,
                //     bits.c_str())
                // L2_LOG_ERROR(Log,
                //     L"[PLEDGE][%s][%d][NO_ERROR][FUNCTION] pledge warehouse failed. Function Bits : %S]",
                //     L"UserSocket_HTMLCmd.cpp",
                //     0x5c4,
                //     bits.c_str())
                L2_LOG_WARN(
                    Log, "Cannot deposit into clan warehouse: warehouse function is disabled; function bits %s.",
                    bits.c_str()
                );
                deferredSystemMessage = SystemMessageId::CLAN_WAREHOUSE_REQUIRES_CLAN_LEVEL_ONE;
            } else {
                warehouse = new CWareHouseDeposit(user, npc, 2, static_cast<int>(pledge->GetDbId()));
                L2_ADD_REF(warehouse, ObjectRefType::Anonymous);
                warehouse->SetCreatureData(npc);
                user->ReleaseEconomy();
                user->m_pEconomy = warehouse;
            }
        }
    }
    if (deferredSystemMessage != 0) {
        user->GetSocket()->SendSystemMessage(deferredSystemMessage);
        return false;
    }
    warehouse->SendListEx(WHC_PLEDGE);
    L2_RELEASE(warehouse, ObjectRefType::Anonymous);
    return false;
}

bool HtmlCmdWithdrawPledge(CUserSocket*, User* user, const char16_t*, const char16_t*) {
    // Current L2Server 0x00c35abc.
    L2_GUARD_FUNCTION();
    CNPC* const npc = ResolveNearbyTalkNpc(user, 0x5dd);
    if (npc == nullptr || npc->m_data == nullptr) {
        return false;
    }

    int deferredSystemMessage = 0;
    bool deferredActionFailed = false;
    CWareHouseWithdraw* warehouse = nullptr;
    {
        L2_GUARD(*user->m_lock);
        if (user->GetSocket()->m_bEconomicRestriction) {
            deferredSystemMessage = SystemMessageId::ACCOUNT_CANNOT_USE_CLAN_WAREHOUSE;
        } else if (user->GetPrivateStoreType() != 0) {
            deferredSystemMessage = SystemMessageId::CANNOT_DROP_DESTROY_OR_TRADE_ITEMS_DURING_PRIVATE_STORE_OR_WORKSHOP;
            deferredActionFailed = true;
        } else {
            const int npcClass = npc->m_data->m_nClass;
            if (npcClass != CT_TELEPORTER && npcClass != CT_WAREHOUSE_KEEPER && npcClass != CT_PACKAGE_KEEPER) {
                return false;
            }

            CPledge* const pledge = user->GetPledge();
            if (pledge == nullptr) {
                deferredSystemMessage = SystemMessageId::NO_AUTHORITY_TO_USE_CLAN_WAREHOUSE;
            } else {
                PledgeFunction* const function = pledge->GetFunction();
                if (!function->IsEnable(PledgeFunctionType::WAREHOUSE)) {
                    const std::string bits =
                        std::bitset<PledgeFunctionType::MAX_COUNT>(PledgeFunction::Builder::GetBits(function)).to_string();
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[PLEDGE][%s][%d][NO_ERROR][FUNCTION] pledge warehouse failed. Function Bits : %S]",
                    //     L"UserSocket_HTMLCmd.cpp",
                    //     0x60e,
                    //     bits.c_str())
                    // L2_LOG_ERROR(Log,
                    //     L"[PLEDGE][%s][%d][NO_ERROR][FUNCTION] pledge warehouse failed. Function Bits : %S]",
                    //     L"UserSocket_HTMLCmd.cpp",
                    //     0x60e,
                    //     bits.c_str())
                    L2_LOG_WARN(
                        Log, "Cannot withdraw from clan warehouse: warehouse function is disabled; function bits %s.",
                        bits.c_str()
                    );
                    deferredSystemMessage = SystemMessageId::CLAN_WAREHOUSE_REQUIRES_CLAN_LEVEL_ONE;
                } else {
                    warehouse = new CWareHouseWithdraw(user, npc, 2, static_cast<int>(pledge->GetDbId()));
                    L2_ADD_REF(warehouse, ObjectRefType::Anonymous);
                    warehouse->SetCreatureData(npc);
                    user->ReleaseEconomy();
                    user->m_pEconomy = warehouse;
                }
            }
        }
    }
    if (deferredSystemMessage != 0) {
        user->GetSocket()->SendSystemMessage(deferredSystemMessage);
        if (deferredActionFailed) {
            // user->ActionFailed(0)
            user->ActionFailed(AF_DEFAULT);
        }
        return false;
    }
    warehouse->SendListEx(WHC_PLEDGE);
    L2_RELEASE(warehouse, ObjectRefType::Anonymous);
    return false;
}

bool HtmlCmdTeleportToNpc(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c34f00. The current image uses GetUniqNPC (not
    // the donor's older GetBossNPC spelling) and source type zero.
    L2_GUARD_FUNCTION();
    if (ResolveNearbyTalkNpc(user, 0x494, false) == nullptr) {
        return false;
    }

    const std::u16string commandUtf16 = std::u16string(command);
    StringTokenizer tokenizer(std::u16string(std::u16string_view(commandUtf16)), std::u16string(u"_"));
    const std::u16string token = tokenizer.NextToken();
    const std::int32_t npcClassId = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(WindowsWtol(Utf16ToUtf8(runtime::StringView(token)))) + UINT32_C(1000000)
    );
    CNPC* const destination = g_UniqNPCDB.GetUniqNPC(0, npcClassId);
    if (destination != nullptr) {
        const FVector position = destination->GetPos();
        user->TeleportToLocation(
            runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
            runtime::Win64CvttDoubleToInt32(position.z), 0, 0x10000, TeleportSourceType::Unknown, nullptr, 0
        );
    }
    return false;
}

bool HtmlCmdTeleport(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c36900.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }

    enum class TalkStatus { Ready, Rejected, Missing, Unknown, TooFar };
    std::uint32_t unknownTargetId;
    std::optional<FVector> npcPositionStorage;
    std::optional<FVector> userPositionStorage;
    const TalkStatus talkStatus = [&] {
        L2_GUARD(*user->m_lock);
        CCreature* const target = user->m_spTalkTarget.GetCreature();
        if (target == nullptr) {
            return TalkStatus::Missing;
        }
        if (!target->IsNPC()) {
            if (target->IsPC()) {
                const char16_t* const targetName = target->GetName();
                const char16_t* const userName = user->GetName();
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] User[%s]'s talk target is PC[%s]",
                //     L"UserSocket_HTMLCmd.cpp",
                //     0x3c0,
                //     userName,
                //     targetName)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] User[%s]'s talk target is PC[%s]",
                //     L"UserSocket_HTMLCmd.cpp",
                //     0x3c0,
                //     userName,
                //     targetName)
                // L2_LOG_WARN(Log,
                //     "Cannot process teleport command for %s: talk target %s is a player.",
                //     WideToUtf8(userName).c_str(), WideToUtf8(targetName).c_str())
                L2_LOG_WARN(
                    Log, "Cannot process teleport command for %s: talk target %s is a player.",
                    Utf16ToUtf8(userName).c_str(), Utf16ToUtf8(targetName).c_str()
                );
            } else {
                unknownTargetId = user->m_spTalkTarget.GetId();
                return TalkStatus::Unknown;
            }
            return TalkStatus::Rejected;
        }

        CNPC* const talkNpc = target->CastNPC();
        if (talkNpc == nullptr) {
            const char16_t* const userName = user->GetName();
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] TalkNpc is not NPC User[%s]",
            //     L"UserSocket_HTMLCmd.cpp",
            //     0x3c0,
            //     userName)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] TalkNpc is not NPC User[%s]",
            //     L"UserSocket_HTMLCmd.cpp",
            //     0x3c0,
            //     userName)
            // L2_LOG_ERROR(Log,
            //     "Cannot process teleport command for %s: NPC target could not be cast to CNPC.",
            //     WideToUtf8(userName).c_str())
            L2_LOG_ERROR(
                Log, "Cannot process teleport command for %s: NPC target could not be cast to CNPC.",
                Utf16ToUtf8(userName).c_str()
            );
            return TalkStatus::Rejected;
        }

        const FVector& npcPosition = npcPositionStorage.emplace(talkNpc->GetPos());
        const FVector& userPosition = userPositionStorage.emplace(user->GetPos());
        const double deltaX = userPosition.x - npcPosition.x;
        const double deltaY = userPosition.y - npcPosition.y;
        if (deltaX * deltaX + deltaY * deltaY > 62500.0) {
            return TalkStatus::TooFar;
        }
        return TalkStatus::Ready;
    }();
    if (talkStatus == TalkStatus::Missing) {
        const std::uint32_t targetId = user->m_spTalkTarget.Sid();
        const char16_t* const userName = user->GetName();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] User[%s]'s talk target is NULL m_spTalkTarget[%d]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x3c0,
        //     userName,
        //     targetId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] User[%s]'s talk target is NULL m_spTalkTarget[%d]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x3c0,
        //     userName,
        //     targetId)
        // L2_LOG_WARN(Log,
        //     "Cannot process teleport command for %s: talk target object %u is missing.",
        //     WideToUtf8(userName).c_str(), static_cast<unsigned>(targetId))
        L2_LOG_WARN(
            Log, "Cannot process teleport command for %s: talk target object %u is missing.", Utf16ToUtf8(userName).c_str(),
            static_cast<unsigned>(targetId)
        );
        runtime::process::ReportCallStack(2, false);
        return false;
    }
    if (talkStatus == TalkStatus::Unknown) {
        const std::uint32_t targetId = unknownTargetId;
        const char16_t* const userName = user->GetName();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] User[%s]'s talk target is neither PC nor NPC [sid:%d]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x3c0,
        //     userName,
        //     targetId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] User[%s]'s talk target is neither PC nor NPC [sid:%d]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x3c0,
        //     userName,
        //     targetId)
        // L2_LOG_WARN(Log,
        //     "Cannot process teleport command for %s: talk target object %u is neither a player nor an NPC.",
        //     WideToUtf8(userName).c_str(), static_cast<unsigned>(targetId))
        L2_LOG_WARN(
            Log, "Cannot process teleport command for %s: talk target object %u is neither a player nor an NPC.",
            Utf16ToUtf8(userName).c_str(), static_cast<unsigned>(targetId)
        );
        return false;
    }
    if (talkStatus == TalkStatus::Rejected) {
        return false;
    }
    if (talkStatus == TalkStatus::TooFar) {
        const char16_t* const userName = user->GetName();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] DISTANCE OVER!! User[%s]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x3c0,
        //     userName)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] DISTANCE OVER!! User[%s]",
        //     L"UserSocket_HTMLCmd.cpp",
        //     0x3c0,
        //     userName)
        // L2_LOG_WARN(Log,
        //     "Cannot process teleport command for %s: horizontal distance to NPC exceeds 250 units.",
        //     WideToUtf8(userName).c_str())
        L2_LOG_WARN(
            Log, "Cannot process teleport command for %s: horizontal distance to NPC exceeds 250 units.",
            Utf16ToUtf8(userName).c_str()
        );
        return false;
    }

    const std::u16string commandUtf16 = std::u16string(command);
    StringTokenizer tokenizer(std::u16string(std::u16string_view(commandUtf16)), std::u16string(u"_"));
    static_cast<void>(tokenizer.CountTokens());

    const std::u16string listIdToken = tokenizer.NextToken();
    const std::int64_t teleportListId = WindowsWtoi64(listIdToken);
    const std::u16string positionToken = tokenizer.NextToken();
    const std::int32_t positionIndex = WindowsWtol(Utf16ToUtf8(runtime::StringView(positionToken)));
    const std::u16string itemToken = tokenizer.NextToken();
    const std::int32_t itemClassId = WindowsWtol(Utf16ToUtf8(runtime::StringView(itemToken)));
    const std::u16string teleporterToken = tokenizer.NextToken();
    const std::uint32_t teleporterServerId =
        static_cast<std::uint32_t>(WindowsWtol(Utf16ToUtf8(runtime::StringView(teleporterToken))));
    const std::u16string typeToken = tokenizer.NextToken();
    const std::int32_t requestedType = WindowsWtol(Utf16ToUtf8(runtime::StringView(typeToken)));

    if (!TalkChannelCache::inst().mChannels[5].IsValidChannel(
            user->Id(), user->m_spTalkTarget.Sid(), 0xfa, teleportListId
        )) {
        if ((g_TalkChannelError & 0x0eU) == 0) {
            return false;
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"Teleport hack!!! char_name[%s] reason[%d]",
        //     user->GetName(),
        //     g_TalkChannelError)
        // L2_LOG_ERROR(Log,
        //     L"Teleport hack!!! char_name[%s] reason[%d]",
        //     user->GetName(),
        //     g_TalkChannelError)
        // L2_LOG_WARN(Log,
        //     "Teleport rejected for %s: talk channel validation failed (reason mask 0x%x).",
        //     WideToUtf8(user->GetName()).c_str(), static_cast<unsigned>(g_TalkChannelError))
        L2_LOG_WARN(
            Log, "Teleport rejected for %s: talk channel validation failed (reason mask 0x%x).",
            Utf16ToUtf8(user->GetName()).c_str(), static_cast<unsigned>(g_TalkChannelError)
        );
        return true;
    }

    CCreature* const teleporterObject = g_CreatureSmartIdManager.GetObjectW(teleporterServerId);
    CNPC* teleporterNpc = nullptr;
    if (teleporterObject != nullptr && teleporterObject->IsNPC()) {
        teleporterNpc = teleporterObject->CastNPC();
    }
    if (teleporterNpc == nullptr || !teleporterNpc->IsTeleporter()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Teleport hack2!!! char_name[%s]",
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     L"Teleport hack2!!! char_name[%s]",
        //     user->GetName())
        // L2_LOG_WARN(Log,
        //     "Teleport rejected for %s: object %u is not an available teleporter NPC.",
        //     WideToUtf8(user->GetName()).c_str(), static_cast<unsigned>(teleporterServerId))
        L2_LOG_WARN(
            Log, "Teleport rejected for %s: object %u is not an available teleporter NPC.",
            Utf16ToUtf8(user->GetName()).c_str(), static_cast<unsigned>(teleporterServerId)
        );
        return false;
    }
    if (teleporterNpc != user->m_spTalkTarget.GetNPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Teleport hack3!!! char_name[%s]",
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     L"Teleport hack3!!! char_name[%s]",
        //     user->GetName())
        // L2_LOG_WARN(Log,
        //     "Teleport rejected for %s: requested teleporter %u differs from the current talk target.",
        //     WideToUtf8(user->GetName()).c_str(), static_cast<unsigned>(teleporterServerId))
        L2_LOG_WARN(
            Log, "Teleport rejected for %s: requested teleporter %u differs from the current talk target.",
            Utf16ToUtf8(user->GetName()).c_str(), static_cast<unsigned>(teleporterServerId)
        );
        return false;
    }
    if (static_cast<std::uint32_t>(requestedType) - 1U > 2U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Teleport hack4!!! char_name[%s]",
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     L"Teleport hack4!!! char_name[%s]",
        //     user->GetName())
        // L2_LOG_WARN(Log,
        //     "Teleport rejected for %s: requested type %d is outside 1..3.",
        //     WideToUtf8(user->GetName()).c_str(), requestedType)
        L2_LOG_WARN(
            Log, "Teleport rejected for %s: requested type %d is outside 1..3.", Utf16ToUtf8(user->GetName()).c_str(),
            requestedType
        );
        return false;
    }

    CTeleporter* const teleporter = teleporterNpc->CastTeleporter();
    const TeleporterType allowedType = teleporter->CanTeleport(user);
    if (requestedType != static_cast<int>(allowedType)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Teleport hack or something changed!!!
        //     char_name[%s]/usertelType[%d]/teleporterType[%d]/TeleporterClassID[%d]", user->GetName(),
        //     static_cast<int>(allowedType),
        //     requestedType,
        //     teleporterNpc->GetClassId())
        // L2_LOG_ERROR(Log,
        //     L"Teleport hack or something changed!!!
        //     char_name[%s]/usertelType[%d]/teleporterType[%d]/TeleporterClassID[%d]", user->GetName(),
        //     static_cast<int>(allowedType),
        //     requestedType,
        //     teleporterNpc->GetClassId())
        // L2_LOG_WARN(Log,
        //     "Teleport rejected for %s: requested type %d differs from allowed type %d. NPC class %d.",
        //     WideToUtf8(user->GetName()).c_str(), requestedType, static_cast<int>(allowedType),
        //     teleporterNpc->GetClassId())
        L2_LOG_WARN(
            Log, "Teleport rejected for %s: requested type %d differs from allowed type %d; NPC class %d.",
            Utf16ToUtf8(user->GetName()).c_str(), requestedType, static_cast<int>(allowedType), teleporterNpc->GetClassId()
        );
        return false;
    }

    {
        L2_GUARD(s_mapTelPosListSect);
        const auto found = s_mapTelPosList.find(teleportListId);
        if (found == s_mapTelPosList.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] what?",
            //     L"UserSocket_HTMLCmd.cpp",
            //     0x487)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] what?",
            //     L"UserSocket_HTMLCmd.cpp",
            //     0x487)
            L2_LOG_ERROR(Log, "Cannot teleport: destination list %lld is missing.", static_cast<long long>(teleportListId));
        } else {
            const std::size_t index = static_cast<std::size_t>(positionIndex);
            if (index >= s_mapTelPosList[teleportListId]->size()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] what?",
                //     L"UserSocket_HTMLCmd.cpp",
                //     0x482)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] what?",
                //     L"UserSocket_HTMLCmd.cpp",
                //     0x482)
                L2_LOG_WARN(
                    Log, "Cannot teleport: destination index %d is outside list %lld.", positionIndex,
                    static_cast<long long>(teleportListId)
                );
            } else {
                const std::int32_t x = runtime::CheckedVectorAt((*s_mapTelPosList[teleportListId]), index).m_nX;
                const std::int32_t y = runtime::CheckedVectorAt((*s_mapTelPosList[teleportListId]), index).m_nY;
                const std::int32_t z = runtime::CheckedVectorAt((*s_mapTelPosList[teleportListId]), index).m_nZ;
                std::int32_t selectedPrice = runtime::CheckedVectorAt((*s_mapTelPosList[teleportListId]), index).m_nPrice;
                std::int32_t price = 0;
                if (requestedType == TELEPORTER_COMMON) {
                    selectedPrice = runtime::CheckedVectorAt((*s_mapTelPosList[teleportListId]), index).m_nPrice;
                    price = selectedPrice;
                } else if (requestedType == TELEPORTER_CORETIME) {
                    selectedPrice = runtime::CheckedVectorAt((*s_mapTelPosList[teleportListId]), index).m_nCoretimePrice;
                    price = selectedPrice;
                }

                if (price > 0 && user->Level() < 41 && user->m_data->m_nSubJobId == 0) {
                    const int param0 = teleporter->GetParam(0);
                    const int param1 = teleporter->GetParam(1);
                    const int param2 = teleporter->GetParam(2);
                    const int param3 = teleporter->GetParam(3);
                    const char16_t* const teleporterName = teleporterNpc->GetName();
                    const int subJobId = user->m_data->m_nSubJobId;
                    const int level = user->Level();
                    const char16_t* const userName = user->GetName();
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"Teleport Price is not Zero char_name[%s], char_level[%d], m_nSubjobId[%d], Teleporter(%s)
                    //     param[%d, %d, %d, %d], TeleportType[%d]", userName, level, subJobId, teleporterName, param3,
                    //     param2,
                    //     param1,
                    //     param0,
                    //     requestedType)
                    // L2_LOG_ERROR(Log,
                    //     L"Teleport Price is not Zero char_name[%s], char_level[%d], m_nSubjobId[%d], Teleporter(%s)
                    //     param[%d, %d, %d, %d], TeleportType[%d]", userName, level, subJobId, teleporterName, param3,
                    //     param2,
                    //     param1,
                    //     param0,
                    //     requestedType)
                    // L2_LOG_WARN(Log,
                    //     "Teleport price %d is positive for %s (level %d, subjob %d). NPC %s, parameters [3..0]: %d, %d,
                    //     %d, %d. Type %d.", price, WideToUtf8(userName).c_str(), level, subJobId,
                    //     WideToUtf8(teleporterName).c_str(), param3, param2, param1, param0, requestedType)
                    L2_LOG_WARN(
                        Log,
                        "Teleport price %d is positive for %s (level %d, subjob %d); NPC %s, parameters [3..0]: %d, %d, %d, "
                        "%d; type %d.",
                        price, Utf16ToUtf8(userName).c_str(), level, subJobId, Utf16ToUtf8(teleporterName).c_str(), param3,
                        param2, param1, param0, requestedType
                    );
                }

                const std::uint32_t now = runtime::GetTickCount();
                if (now - user->GetLastHTMLCmdTeleportTime() < 0x1389U) {
                    user->SendSystemMessage(SystemMessageId::ANOTHER_TELEPORT_IN_PROGRESS);
                } else {
                    if (price < 1) {
                        user->TeleportToLocation(
                            x, y, z, user->GetInZoneID(), 0x10000, TeleportSourceType::HtmlCmdTeleport, nullptr, 0
                        );
                    } else {
                        const std::int32_t payment = static_cast<std::int32_t>(0U - static_cast<std::uint32_t>(price));
                        if (itemClassId == -100) {
                            if (!user->CheckPointAddable(1, payment)) {
                                user->SendSystemMessage(SystemMessageId::NOT_ENOUGH_PC_POINTS);
                            } else if (user->CheckAndSetPendingTeleportItem(
                                           -100, static_cast<std::int64_t>(payment), x, y, z
                                       )) {
                                user->TeleportToLocation(
                                    x, y, z, user->GetInZoneID(), 0x10000, TeleportSourceType::HtmlCmdTeleport, nullptr, 0
                                );
                            }
                        } else {
                            ItemManip request(0, itemClassId, static_cast<std::int64_t>(payment), 0, 0, 0, true);
                            ItemManipList manipulations;
                            manipulations.Push(1, &request);
                            manipulations.Serialize();

                            const bool valid = [&] {
                                L2_READ_GUARD(user->m_inv);
                                return user->m_inv.IsValidManipulation(&manipulations);
                            }();
                            if (!valid) {
                                user->SendSystemMessage(
                                    itemClassId == 0x39 ? SystemMessageId::NOT_ENOUGH_ADENA
                                                        : SystemMessageId::NOT_ENOUGH_REQUIRED_ITEMS
                                );
                            } else if (user->CheckAndSetPendingTeleportItem(
                                           itemClassId, static_cast<std::int64_t>(payment), x, y, z
                                       )) {
                                user->TeleportToLocation(
                                    x, y, z, user->GetInZoneID(), 0x10000, TeleportSourceType::HtmlCmdTeleport, nullptr, 0
                                );
                            }
                        }
                    }
                    user->SetLastHTMLCmdTeleportTime(now);
                }
            }
        }
    }
    return false;
}

bool HtmlCmdObserverGroup(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c36068.
    L2_GUARD_FUNCTION();
    CSignBoard* signBoard = nullptr;
    {
        L2_GUARD(*user->m_lock);
        CStaticObject* const target = user->m_spTalkTarget.GetStaticObject();
        if (target == nullptr || !target->IsSignBoard()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid talk target",
            //     L"UserSocket_HTMLCmd.cpp",
            //     0x2ae)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid talk target",
            //     L"UserSocket_HTMLCmd.cpp",
            //     0x2ae)
            L2_LOG_WARN(Log, "Cannot show observer groups: talk target is missing or is not a signboard.");
            return false;
        }
        signBoard = target->CastSignBoard();
        if (signBoard == nullptr) {
            return false;
        }

        const FVector boardPosition = signBoard->GetPos();
        const FVector userPosition = user->GetPos();
        const double x = userPosition.x - boardPosition.x;
        const double y = userPosition.y - boardPosition.y;
        if (x * x + y * y > 62500.0) {
            return false;
        }
    }

    const int npcServerId = static_cast<int>(WindowsWtol(std::u16string_view(command)));
    const char16_t* const separator = target_utf16::Find(command, u'_');
    if (separator == nullptr) {
        return false;
    }
    const int group = static_cast<int>(WindowsWtol(std::u16string_view(separator + 1)));
    const std::uint32_t userServerId = user->Id();
    if (TalkChannelCache::inst().mChannels[5].IsValidChannel(
            userServerId, static_cast<std::uint32_t>(npcServerId), 0xfa, 0
        )) {
        if (static_cast<int>(signBoard->Id()) == npcServerId) {
            signBoard->SendObserverListHTML(user, group);
        }
        return false;
    }
    if ((g_TalkChannelError & 0x06U) != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Observer hack!!! char_name[%s]",
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     L"Observer hack!!! char_name[%s]",
        //     user->GetName())
        // L2_LOG_WARN(Log,
        //     "Observer group request rejected for %s: talk channel validation failed (reason mask 0x%x).",
        //     WideToUtf8(user->GetName()).c_str(), static_cast<unsigned>(g_TalkChannelError))
        L2_LOG_WARN(
            Log, "Observer group request rejected for %s: talk channel validation failed (reason mask 0x%x).",
            Utf16ToUtf8(user->GetName()).c_str(), static_cast<unsigned>(g_TalkChannelError)
        );
        return true;
    }
    return false;
}

bool HtmlCmdObserver(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c362dc.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }

    CSignBoard* signBoard = nullptr;
    {
        L2_GUARD(*user->m_lock);
        CStaticObject* const target = user->m_spTalkTarget.GetStaticObject();
        if (target == nullptr || !target->IsSignBoard()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid talk target",
            //     L"UserSocket_HTMLCmd.cpp",
            //     0x2db)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid talk target",
            //     L"UserSocket_HTMLCmd.cpp",
            //     0x2db)
            L2_LOG_WARN(Log, "Cannot start observation: talk target is missing or is not a signboard.");
            return false;
        }
        signBoard = target->CastSignBoard();
        if (signBoard == nullptr) {
            return false;
        }

        const FVector boardPosition = signBoard->GetPos();
        const FVector userPosition = user->GetPos();
        const double deltaX = userPosition.x - boardPosition.x;
        const double deltaY = userPosition.y - boardPosition.y;
        if (deltaX * deltaX + deltaY * deltaY > 62500.0) {
            return false;
        }
    }

    const int signBoardServerId = static_cast<int>(WindowsWtol(std::u16string_view(command)));
    const char16_t* const separator = target_utf16::Find(command, u'_');
    if (separator == nullptr) {
        return false;
    }
    const int observerIndex = static_cast<int>(WindowsWtol(std::u16string_view(separator + 1)));

    const std::uint32_t userServerId = user->Id();
    const std::uint32_t actualSignBoardServerId = signBoard->Id();
    if (!TalkChannelCache::inst().mChannels[5].IsValidChannel(userServerId, actualSignBoardServerId, 0xfa, 0)) {
        if ((g_TalkChannelError & 0x06U) != 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Observer hack!!! char_name[%s]",
            //     user->GetName())
            // L2_LOG_ERROR(Log,
            //     L"Observer hack!!! char_name[%s]",
            //     user->GetName())
            // L2_LOG_WARN(Log,
            //     "Observation request rejected for %s: talk channel validation failed (reason mask 0x%x).",
            //     WideToUtf8(user->GetName()).c_str(), static_cast<unsigned>(g_TalkChannelError))
            L2_LOG_WARN(
                Log, "Observation request rejected for %s: talk channel validation failed (reason mask 0x%x).",
                Utf16ToUtf8(user->GetName()).c_str(), static_cast<unsigned>(g_TalkChannelError)
            );
        }
        return true;
    }
    if (static_cast<int>(actualSignBoardServerId) != signBoardServerId) {
        return false;
    }
    if (user->HavePet() || user->HaveSummon() || user->IsRiding()) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_OBSERVE_WITH_PET_OR_SERVITOR_SUMMONED);
        return false;
    }
    if (user->IsOlympiadObserver()) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::OLYMPIAD_PARTICIPANT_CANNOT_OBSERVE);
        return false;
    }
    if (g_PledgeGameSystem.IsRegisteredUser(user)) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_OBSERVE_OLYMPIAD_WHILE_REGISTERED);
        return false;
    }
    if (user->m_data->m_bIsInCombatMode) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_OBSERVE_DURING_COMBAT);
        return false;
    }
    if (user->IsNowTrade()) {
        return false;
    }

    const int observerCount = static_cast<int>(signBoard->m_vObserverPosList.size());
    if (!(observerIndex < observerCount) || observerIndex < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Observer hack!!! char_name[%s]",
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     L"Observer hack!!! char_name[%s]",
        //     user->GetName())
        // L2_LOG_WARN(Log,
        //     "Observation request rejected for %s: index %d is invalid for %d available positions.",
        //     WideToUtf8(user->GetName()).c_str(), observerIndex, observerCount)
        L2_LOG_WARN(
            Log, "Observation request rejected for %s: index %d is invalid for %d available positions.",
            Utf16ToUtf8(user->GetName()).c_str(), observerIndex, observerCount
        );
        return true;
    }

    const ObserverPos observer = runtime::CheckedVectorAt(signBoard->m_vObserverPosList, observerIndex);
    CResidence* const residence = observer.nResidenceId < 1 ? nullptr : g_ResidenceDB.GetResidence(observer.nResidenceId);
    if (observer.nResidenceId >= 1 && residence != nullptr && residence->IsCastle() && !residence->IsUnderSiege()) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CAN_ONLY_OBSERVE_DURING_SIEGE);
        return false;
    }

    const std::int64_t adenaBefore = user->m_inv.GetAdenaAmount();
    if (observer.price > adenaBefore) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::NOT_ENOUGH_ADENA);
        return false;
    }

    user->DeleteItemInInventoryBeforeCommit(0x39, observer.price);
    user->GetSocket()->Send("chccQ", 0x62, SystemMessageId::ADENA_DISAPPEARED, 1, 6, observer.price);
    user->StopMove();
    user->ObserverStart(observer.x, observer.y, observer.z, observer.yaw, observer.pitch, 0xe10);

    const FVector position = user->GetPos();
    const std::int64_t adenaAfter = user->m_inv.GetAdenaAmount();
    std::array<wchar_t, 16> accountNameStorage{};
    const wchar_t* const accountName = ObserverAccountName(user->m_sAccountName, accountNameStorage);
#if defined(_WIN32)
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,%s,%s,,,",
        0x34b, user->GetDBID(), user->GetAccountID(), runtime::Win64CvttDoubleToInt32(position.x),
        runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z), user->m_data->m_Race,
        user->m_data->m_Sex, user->m_data->m_nClass, user->m_data->m_nLevel, observerIndex, observer.price, adenaAfter,
        user->GetName(), accountName
    );
#else
    // Portable: %I64d becomes %lld. Pass matching 64-bit values to varargs.
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,%s,%s,,,",
    //     0x34b,
    //     user->GetDBID(),
    //     user->GetAccountID(),
    //     runtime::Win64CvttDoubleToInt32(position.x),
    //     runtime::Win64CvttDoubleToInt32(position.y),
    //     runtime::Win64CvttDoubleToInt32(position.z),
    //     user->m_data->m_Race,
    //     user->m_data->m_Sex,
    //     user->m_data->m_nClass,
    //     user->m_data->m_nLevel,
    //     observerIndex,
    //     observer.price,
    //     adenaAfter,
    //     user->GetName(),
    //     accountName)
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,%s,%s,,,",
    //     static_cast<long long>(0x34b),
    //     static_cast<long long>(user->GetDBID()),
    //     static_cast<long long>(user->GetAccountID()),
    //     static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.x)),
    //     static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.y)),
    //     static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.z)),
    //     static_cast<long long>(user->m_data->m_Race),
    //     static_cast<long long>(user->m_data->m_Sex),
    //     static_cast<long long>(user->m_data->m_nClass),
    //     static_cast<long long>(user->m_data->m_nLevel),
    //     static_cast<long long>(observerIndex),
    //     static_cast<long long>(observer.price),
    //     static_cast<long long>(adenaAfter),
    //     user->GetName(),
    //     accountName)
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,%s,%s,,,",
        static_cast<long long>(0x34b), static_cast<long long>(user->GetDBID()), static_cast<long long>(user->GetAccountID()),
        static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.x)),
        static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.y)),
        static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.z)), static_cast<long long>(user->m_data->m_Race),
        static_cast<long long>(user->m_data->m_Sex), static_cast<long long>(user->m_data->m_nClass),
        static_cast<long long>(user->m_data->m_nLevel), static_cast<long long>(observerIndex),
        static_cast<long long>(observer.price), static_cast<long long>(adenaAfter), user->GetName(), accountName
    );
#endif
    return false;
}

bool HtmlCmdCreateAcademy(CUserSocket*, User*, const char16_t*, const char16_t*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[PLEDGE][%s][%d]DEPRECATED!!!!!",
    //     L"UserSocket_HTMLCmd.cpp",
    //     0x87b)
    // L2_LOG_ERROR(Log,
    //     L"[PLEDGE][%s][%d]DEPRECATED!!!!!",
    //     L"UserSocket_HTMLCmd.cpp",
    //     0x87b)
    L2_LOG_WARN(Log, "Ignoring deprecated HTML command: create academy.");
    return false;
}

bool HtmlCmdRenameSubPledge(CUserSocket*, User*, const char16_t*, const char16_t*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[PLEDGE][%s][%d]DEPRECATED!!!!!",
    //     L"UserSocket_HTMLCmd.cpp",
    //     0x892)
    // L2_LOG_ERROR(Log,
    //     L"[PLEDGE][%s][%d]DEPRECATED!!!!!",
    //     L"UserSocket_HTMLCmd.cpp",
    //     0x892)
    L2_LOG_WARN(Log, "Ignoring deprecated HTML command: rename subpledge.");
    return false;
}

bool HtmlCmdCreateSubPledge(CUserSocket*, User*, const char16_t*, const char16_t*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[PLEDGE][%s][%d]DEPRECATED!!!!!",
    //     L"UserSocket_HTMLCmd.cpp",
    //     0x8aa)
    // L2_LOG_ERROR(Log,
    //     L"[PLEDGE][%s][%d]DEPRECATED!!!!!",
    //     L"UserSocket_HTMLCmd.cpp",
    //     0x8aa)
    L2_LOG_WARN(Log, "Ignoring deprecated HTML command: create subpledge.");
    return false;
}

bool HtmlCmdUpdateSubPledgeMaster(CUserSocket*, User*, const char16_t*, const char16_t*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[PLEDGE][%s][%d]DEPRECATED!!!!!",
    //     L"UserSocket_HTMLCmd.cpp",
    //     0x8c3)
    // L2_LOG_ERROR(Log,
    //     L"[PLEDGE][%s][%d]DEPRECATED!!!!!",
    //     L"UserSocket_HTMLCmd.cpp",
    //     0x8c3)
    L2_LOG_WARN(Log, "Ignoring deprecated HTML command: update subpledge master.");
    return false;
}

bool HtmlCmdUpgradeSubPledgeMemberCount(CUserSocket*, User*, const char16_t*, const char16_t*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[PLEDGE][%s][%d]DEPRECATED!!!!!",
    //     L"UserSocket_HTMLCmd.cpp",
    //     0x8db)
    // L2_LOG_ERROR(Log,
    //     L"[PLEDGE][%s][%d]DEPRECATED!!!!!",
    //     L"UserSocket_HTMLCmd.cpp",
    //     0x8db)
    L2_LOG_WARN(Log, "Ignoring deprecated HTML command: upgrade subpledge member count.");
    return false;
}

bool HtmlCmdPledgeLevelUp(CUserSocket*, User*, const char16_t*, const char16_t*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[PLEDGE][%s][%d]DEPRECATED!!!!!",
    //     L"UserSocket_HTMLCmd.cpp",
    //     0x91e)
    // L2_LOG_ERROR(Log,
    //     L"[PLEDGE][%s][%d]DEPRECATED!!!!!",
    //     L"UserSocket_HTMLCmd.cpp",
    //     0x91e)
    L2_LOG_WARN(Log, "Ignoring deprecated HTML command: level up clan.");
    return false;
}

bool HtmlCmdLearnSkill(CUserSocket*, User* user, const char16_t*, const char16_t*) {
    // Current L2Server 0x00c3238c.
    L2_GUARD_FUNCTION();
    CNPC* const npc = ResolveNearbyTalkNpc(user, 0x7f3);
    if (npc != nullptr && npc->m_data != nullptr) {
        g_npcserver.Send("cddd", 0x13, static_cast<int>(user->Id()), user->m_data->index, npc->m_data->index);
    }
    return false;
}

bool HtmlCmdEnchantSkill(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c326cc.
    L2_GUARD_FUNCTION();
    CNPC* const npc = ResolveNearbyTalkNpc(user, 0x803);
    if (npc != nullptr && npc->m_data != nullptr) {
        g_npcserver.Send(
            "cdddd", 0x4d, static_cast<int>(user->Id()), user->m_data->index, npc->m_data->index,
            static_cast<int>(SkillEnchantOperator::GetSkillEnchantTypeByCmd(command))
        );
    }
    return false;
}

bool HtmlCmdSkillQuestAccept(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c39e90.
    L2_GUARD_FUNCTION();
    CNPC* const npc = ResolveNearbyTalkNpc(user, 0x815);
    if (npc != nullptr && npc->m_data != nullptr) {
        const CHTMLActionParamParser parameters = CHTMLActionParamParser::Parse(command);
        g_npcserver.Send(
            "cdddd", 0x14, static_cast<int>(user->Id()), user->m_data->index, npc->m_data->index,
            parameters.GetIntValue(u"quest_id")
        );
    }
    return false;
}

bool HtmlCmdClassChange(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c3a228.
    L2_GUARD_FUNCTION();
    CNPC* const npc = ResolveNearbyTalkNpc(user, 0x82a);
    if (npc != nullptr && npc->m_data != nullptr) {
        const CHTMLActionParamParser parameters = CHTMLActionParamParser::Parse(command);
        g_npcserver.Send(
            "cdddd", 0x16, static_cast<int>(user->Id()), user->m_data->index, npc->m_data->index,
            parameters.GetIntValue(u"class_name")
        );
    }
    return false;
}

bool HtmlCmdCreateAlliance(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c3ac2c.
    L2_GUARD_FUNCTION();
    CNPC* const npc = ResolveNearbyTalkNpc(user, 0x943);
    if (npc != nullptr) {
        const CHTMLActionParamParser parameters = CHTMLActionParamParser::Parse(command);
        const char16_t* const name = parameters.GetStrValue(u"alliance_name");
        if (name != nullptr) {
            CAlliance::CreateAllianceByNpc(npc, user, std::u16string(name).c_str());
        }
    }
    return false;
}

bool HtmlCmdManorMenuSelect(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c32084.
    L2_GUARD_FUNCTION();
    if (ResolveNearbyTalkNpc(user, 0x764) != nullptr) {
        CManorMenuParser::Parse(user, command);
    }
    return false;
}

bool HtmlCmdTalkSelect(CUserSocket*, User* user, const char16_t*, const char16_t*) {
    // Current L2Server 0x00c32a24.
    L2_GUARD_FUNCTION();
    CNPC* const npc = ResolveNearbyTalkNpc(user, 0x848);
    if (npc != nullptr && npc->m_data != nullptr) {
        g_npcserver.Send("cddd", 0x1a, static_cast<int>(user->Id()), user->m_data->index, static_cast<int>(npc->Id()));
    }
    return false;
}

bool HtmlCmdTransferMasterCancel(CUserSocket*, User* user, const char16_t*, const char16_t*) {
    // Current L2Server 0x00c32d54.
    L2_GUARD_FUNCTION();
    CNPC* const npc = ResolveNearbyTalkNpc(user, 0x859);
    if (npc != nullptr) {
        CPledge::PledgeMasterTransferCancelByNpc(npc, user);
    }
    return false;
}

bool HtmlCmdTransferMaster(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c3a5a4.
    L2_GUARD_FUNCTION();
    CNPC* const npc = ResolveNearbyTalkNpc(user, 0x867);
    if (npc != nullptr) {
        const CHTMLActionParamParser parameters = CHTMLActionParamParser::Parse(command);
        const char16_t* const masterName = parameters.GetStrValue(u"master_name");
        if (masterName != nullptr) {
            CPledge::PledgeMasterTransferByNpc(npc, user, std::u16string(masterName).c_str());
        }
    }
    return false;
}

bool HtmlCmdCreatePledge(CUserSocket*, User* user, const char16_t* command, const char16_t*) {
    // Current L2Server 0x00c3a8e8.
    L2_GUARD_FUNCTION();
    CNPC* const npc = ResolveNearbyTalkNpc(user, 0x8ee);
    if (npc != nullptr) {
        const CHTMLActionParamParser parameters = CHTMLActionParamParser::Parse(command);
        const char16_t* const pledgeName = parameters.GetStrValue(u"pledge_name");
        if (pledgeName != nullptr) {
            CPledge::CreatePledgeByNpc(npc, user, std::u16string(pledgeName).c_str());
        }
    }
    return false;
}

bool HtmlCmdDismissPledge(CUserSocket*, User* user, const char16_t*, const char16_t*) {
    // Current L2Server 0x00c333f4.
    L2_GUARD_FUNCTION();
    CNPC* const npc = ResolveNearbyTalkNpc(user, 0x901);
    if (npc != nullptr) {
        CPledge::DismissPledgeByNpc(npc, user);
    }
    return false;
}

bool HtmlCmdRevivePledge(CUserSocket*, User* user, const char16_t*, const char16_t*) {
    // Current L2Server 0x00c336fc.
    L2_GUARD_FUNCTION();
    CNPC* const npc = ResolveNearbyTalkNpc(user, 0x90f);
    if (npc != nullptr) {
        CPledge::RevivePledgeByNpc(npc, user);
    }
    return false;
}
