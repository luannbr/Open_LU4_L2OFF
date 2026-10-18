#include "AdminSocket.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TemplateHelper.h"

#include "AdminUtil.h"
#include "AllianceDb.h"
#include "CAccount.h"
#include "CAnnounce.h"
#include "CReadFile.h"
#include "Cached.h"
#include "CastleDb.h"
#include "EventDataHelper.h"
#include "FriendsDb.h"
#include "Item.h"
#include "Pet.h"
#include "PledgeContributionInCache.h"
#include "PledgeCrestDb.h"
#include "PledgeDb.h"
#include "SevenSignQuestDB.h"
#include "UserDb.h"
#include "WareHouse.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/DelimitedText.h"
#include "Shared/src/BaseAttribute.h"
#include "Shared/src/DBConn.h"
#include "Shared/src/ItemOptionKey.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/TargetFormatting.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/StringTokenizer.h"
#include "Shared/src/UtilsString.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/OlympiadDefine.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/platform/Runtime.h"
#include "Shared/src/TextRuntime.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <Ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

AdminCallbackMap gPacketTableFromAdmin{DummyPacket, u"AQ_PROTOCOL"};

namespace {

const bool IsRegistered_AQ_ADMIN_VERSION = gPacketTableFromAdmin.Bind(0, AdminVersionPacket);
const bool IsRegistered_AQ_CHECK_CHARACTER = gPacketTableFromAdmin.Bind(1, CheckCharacterPacket);
const bool IsRegistered_AQ_SET_CHARACTER_LOCATION = gPacketTableFromAdmin.Bind(2, SetCharacterLocationPacket);
const bool IsRegistered_AQ_SET_BUILDER_CHARACTER = gPacketTableFromAdmin.Bind(3, SetBuilderCharacterPacket);
const bool IsRegistered_AQ_CHANGE_CHARACTER_NAME = gPacketTableFromAdmin.Bind(4, ChangeCharacterNamePacket);
const bool IsRegistered_AQ_KICK_CHARACTER = gPacketTableFromAdmin.Bind(5, KickCharacterPacket);
const bool IsRegistered_AQ_ADD_SKILL = gPacketTableFromAdmin.Bind(6, AddSkillPacket);
const bool IsRegistered_AQ_DEL_SKILL = gPacketTableFromAdmin.Bind(7, DelSkillPacket);
const bool IsRegistered_AQ_MOD_SKILL = gPacketTableFromAdmin.Bind(8, ModSkillPacket);
const bool IsRegistered_AQ_SET_ONETIME_QUEST = gPacketTableFromAdmin.Bind(9, SetOnetimeQuestPacket);
const bool IsRegistered_AQ_SET_QUEST = gPacketTableFromAdmin.Bind(10, SetQuestPacket);
const bool IsRegistered_AQ_DEL_QUEST = gPacketTableFromAdmin.Bind(11, DelQuestPacket);
const bool IsRegistered_AQ_ADD_ITEM = gPacketTableFromAdmin.Bind(12, AddItemPacket);
const bool IsRegistered_AQ_DEL_ITEM = gPacketTableFromAdmin.Bind(13, DelItemPacket);
const bool IsRegistered_AQ_MOD_ITEM = gPacketTableFromAdmin.Bind(14, ModItemPacket);
const bool IsRegistered_AQ_MOD_CHAR = gPacketTableFromAdmin.Bind(15, ModCharPacket);
const bool IsRegistered_AQ_MOD_CHAR_2 = gPacketTableFromAdmin.Bind(16, ModChar2Packet);
const bool IsRegistered_AQ_MOD_CHAR_PLEDGE = gPacketTableFromAdmin.Bind(17, ModCharPledgePacket);
const bool IsRegistered_AQ_PUNISH_CHAR = gPacketTableFromAdmin.Bind(18, PunishCharPacket);
const bool IsRegistered_AQ_SET_BUILDER_ACCOUNT = gPacketTableFromAdmin.Bind(19, SetBuilderAccountPacket);
const bool IsRegistered_AQ_DISABLE_CHARACTER = gPacketTableFromAdmin.Bind(20, DisableCharacterPacket);
const bool IsRegistered_AQ_ENABLE_CHARACTER = gPacketTableFromAdmin.Bind(21, EnableCharacterPacket);
const bool IsRegistered_AQ_GET_CHARACTERS = gPacketTableFromAdmin.Bind(22, GetCharactersPacket);
const bool IsRegistered_AQ_SET_BOOKMARK = gPacketTableFromAdmin.Bind(23, SetBookMarkPacket);
const bool IsRegistered_AQ_DELETE_BOOKMARK = gPacketTableFromAdmin.Bind(24, DelBookMarkPacket);
const bool IsRegistered_AQ_DELETE_PLEDGE_CREST = gPacketTableFromAdmin.Bind(25, DelPledgeCrestPacket);
const bool IsRegistered_AQ_MODIFY_PLEDGE_NAME = gPacketTableFromAdmin.Bind(26, ModPledgeNamePacket);
const bool IsRegistered_AQ_SET_PROHIBITED = gPacketTableFromAdmin.Bind(27, SetProhibitedPacket);
const bool IsRegistered_AQ_SEIZE_ITEM = gPacketTableFromAdmin.Bind(28, SeizeItemPacket);
const bool IsRegistered_AQ_MOD_CHAR_3 = gPacketTableFromAdmin.Bind(29, ModChar3Packet);
const bool IsRegistered_AQ_MOVE_ITEM = gPacketTableFromAdmin.Bind(30, MoveItemPacket);
const bool IsRegistered_AQ_MOVE_CHARACTER = gPacketTableFromAdmin.Bind(31, MoveCharacterPacket);
const bool IsRegistered_AQ_COMMENT_WRITE = gPacketTableFromAdmin.Bind(32, CommentWritePacket);
const bool IsRegistered_AQ_COMMENT_DELETE = gPacketTableFromAdmin.Bind(33, CommentDeletePacket);
const bool IsRegistered_AQ_DELETE_CHARACTER = gPacketTableFromAdmin.Bind(34, DeleteCharPacket);
const bool IsRegistered_AQ_RESTORE_CHARACTER = gPacketTableFromAdmin.Bind(35, RestoreCharPacket);
const bool IsRegistered_AQ_PLEDGE_OUST = gPacketTableFromAdmin.Bind(36, PledgeOustPacket);
const bool IsRegistered_AQ_PRINT_ALL_ACCOUNT_DATA = gPacketTableFromAdmin.Bind(41, PrintAllAccountData);
const bool IsRegistered_AQ_PRINT_ALL_ITEM_DATA = gPacketTableFromAdmin.Bind(42, PrintAllItemData);
const bool IsRegistered_AQ_COPY_CHARACTER = gPacketTableFromAdmin.Bind(43, CopyCharPacket);
const bool IsRegistered_AQ_PLEDGE_CHANGE_OWNER = gPacketTableFromAdmin.Bind(37, PledgeChangeOwnerPacket);
const bool IsRegistered_AQ_PLEDGE_DELETE = gPacketTableFromAdmin.Bind(38, PledgeDeletePacket);
const bool IsRegistered_AQ_BAN_CHARACTER = gPacketTableFromAdmin.Bind(39, BanCharPacket);
const bool IsRegistered_AQ_MOVE_ITEM_2 = gPacketTableFromAdmin.Bind(40, MoveItem2Packet);
const bool IsRegistered_AQ_CREATE_PET = gPacketTableFromAdmin.Bind(44, CreatePetPacket);
const bool IsRegistered_AQ_SEND_HOME = gPacketTableFromAdmin.Bind(45, SendHomePacket);
const bool IsRegistered_AQ_CHANGE_PLEDGE_LEVEL = gPacketTableFromAdmin.Bind(46, ChangePledgeLevelPacket);
const bool IsRegistered_AQ_CREATE_PLEDGE = gPacketTableFromAdmin.Bind(47, CreatePledgePacket);
const bool IsRegistered_AQ_SET_SKILL_ALL = gPacketTableFromAdmin.Bind(48, SetSkillAllPacket);
const bool IsRegistered_AQ_RESTORE_CHARACTER_2 = gPacketTableFromAdmin.Bind(49, RestoreChar2Packet);
const bool IsRegistered_AQ_MAN_ANNOUNCE = gPacketTableFromAdmin.Bind(50, ManAnnouncePacket);
const bool IsRegistered_AQ_MAN_INT_ANNOUNCE = gPacketTableFromAdmin.Bind(51, ManIntAnnouncePacket);
const bool IsRegistered_AQ_MOD_WEEK_PLAY = gPacketTableFromAdmin.Bind(52, ModWeekPlayPacket);
const bool IsRegistered_AQ_SEIZE_ITEM_2 = gPacketTableFromAdmin.Bind(53, SeizeItem2Packet);
const bool IsRegistered_AQ_GET_CHARACTERS_2 = gPacketTableFromAdmin.Bind(56, GetCharacters2Packet);
const bool IsRegistered_AQ_DEL_MON_RACE_TICKET = gPacketTableFromAdmin.Bind(58, DelMonRaceTicketPacket);
const bool IsRegistered_AQ_DEL_ITEM_2 = gPacketTableFromAdmin.Bind(54, DelItem2Packet);
const bool IsRegistered_AQ_ADD_ITEM_2 = gPacketTableFromAdmin.Bind(55, AddItem2Packet);
const bool IsRegistered_AQ_DEL_MACRO = gPacketTableFromAdmin.Bind(57, DelMacroPacket);
const bool IsRegistered_AQ_DEL_RECIPE = gPacketTableFromAdmin.Bind(59, DelRecipePacket);
const bool IsRegistered_AQ_DEL_LOTTO = gPacketTableFromAdmin.Bind(60, DelLottoPacket);
const bool IsRegistered_AQ_DUMMY_61 = gPacketTableFromAdmin.Bind(61, DummyPacket);
const bool IsRegistered_AQ_EVENT_POINT = gPacketTableFromAdmin.Bind(62, EventPointPacket);
const bool IsRegistered_AQ_GET_PLEDGE = gPacketTableFromAdmin.Bind(63, GetPledgePacket);
const bool IsRegistered_AQ_GET_CHARACTERS_3 = gPacketTableFromAdmin.Bind(68, GetCharacters3Packet);
const bool IsRegistered_AQ_ALL_EVENT_POINT = gPacketTableFromAdmin.Bind(64, AllEventPointPacket);
const bool IsRegistered_AQ_CREATE_CHARACTER = gPacketTableFromAdmin.Bind(65, CreateCharacterPacket);
const bool IsRegistered_AQ_ADD_ITEMS = gPacketTableFromAdmin.Bind(66, AddItemsPacket);
const bool IsRegistered_AQ_ADD_SKILLS = gPacketTableFromAdmin.Bind(67, AddSkillsPacket);
const bool IsRegistered_AQ_SET_SOCIALITY = gPacketTableFromAdmin.Bind(69, SetSocialityPacket);
const bool IsRegistered_AQ_SET_INSTANT_ANNOUNCE = gPacketTableFromAdmin.Bind(70, SetInstantAnnouncePacket);
const bool IsRegistered_AQ_DEL_HENNA = gPacketTableFromAdmin.Bind(71, DelHennaPacket);
const bool IsRegistered_AQ_ADD_HENNA = gPacketTableFromAdmin.Bind(72, AddHennaPacket);
const bool IsRegistered_AQ_MANAGE_USER_NAME_RESERVED = gPacketTableFromAdmin.Bind(73, ManageUserNameReservedPacket);
const bool IsRegistered_AQ_ADD_HENNAS = gPacketTableFromAdmin.Bind(74, AddHennasPacket);
const bool IsRegistered_AQ_GET_CASTLE_LIST = gPacketTableFromAdmin.Bind(75, GetCastleListPacket);
const bool IsRegistered_AQ_MODIFY_CHAR_PROPERTY = gPacketTableFromAdmin.Bind(76, ModifyCharPropertyPacket);
const bool IsRegistered_AQ_MODIFY_CHAR_ABILITY_DELTA = gPacketTableFromAdmin.Bind(77, ModifyCharAbilityDeltaPacket);
const bool IsRegistered_AQ_MODIFY_CHAR_ABILITY = gPacketTableFromAdmin.Bind(78, ModifyCharAbilityPacket);
const bool IsRegistered_AQ_DEL_HENNA_2 = gPacketTableFromAdmin.Bind(79, DelHenna2Packet);
const bool IsRegistered_AQ_ADD_HENNA_2 = gPacketTableFromAdmin.Bind(80, AddHenna2Packet);
const bool IsRegistered_AQ_ADD_SKILL_2 = gPacketTableFromAdmin.Bind(81, AddSkill2Packet);
const bool IsRegistered_AQ_DEL_SKILL_2 = gPacketTableFromAdmin.Bind(82, DelSkill2Packet);
const bool IsRegistered_AQ_MODIFY_SKILL_2 = gPacketTableFromAdmin.Bind(83, ModifySkill2Packet);
const bool IsRegistered_AQ_ADD_MACRO = gPacketTableFromAdmin.Bind(84, AddMacroPacket);
const bool IsRegistered_AQ_GET_SSQ_STATUS = gPacketTableFromAdmin.Bind(85, GetSSQStatusPacket);
const bool IsRegistered_AQ_GET_SSQ_MAIN_EVENT_RECORD = gPacketTableFromAdmin.Bind(86, GetSSQMainEventRecordPacket);
const bool IsRegistered_AQ_MODIFY_DEPOSITED_SSQ_ITEM = gPacketTableFromAdmin.Bind(87, ModifyDepositedSSQItemPacket);
const bool IsRegistered_AQ_CHANGE_PET_NAME = gPacketTableFromAdmin.Bind(88, ChangePetNamePacket);
const bool IsRegistered_AQ_CHANGE_SUBJOB = gPacketTableFromAdmin.Bind(89, ChangeSubJobPacket);
const bool IsRegistered_AQ_STOP_CHAR = gPacketTableFromAdmin.Bind(90, StopCharPacket);
const bool IsRegistered_AQ_CANCEL_PERSONAL_SHOP = gPacketTableFromAdmin.Bind(91, CancelPersonalShopPacket);
const bool IsRegistered_AQ_ADD_MACRO_INFO = gPacketTableFromAdmin.Bind(92, AddMacroInfoPacket);
const bool IsRegistered_AQ_CREATE_CHARACTER_2 = gPacketTableFromAdmin.Bind(93, CreateCharacter2Packet);
const bool IsRegistered_AQ_ADD_SKILLS_2 = gPacketTableFromAdmin.Bind(94, AddSkills2Packet);
const bool IsRegistered_AQ_ADD_HENNAS_2 = gPacketTableFromAdmin.Bind(95, AddHennas2Packet);
const bool IsRegistered_AQ_ADD_SUBJOBS = gPacketTableFromAdmin.Bind(96, AddSubjobsPacket);
const bool IsRegistered_AQ_GET_PLEDGE_2 = gPacketTableFromAdmin.Bind(97, GetPledge2Packet);
const bool IsRegistered_AQ_DELETE_PLEDGE_EMBLEM = gPacketTableFromAdmin.Bind(98, DelPledgeEmblemPacket);
const bool IsRegistered_AQ_REGISTER_ACCOUNT = gPacketTableFromAdmin.Bind(99, RegisterAccountPacket);
const bool IsRegistered_AQ_DELETE_PLEDGE_ANNOUNCE = gPacketTableFromAdmin.Bind(100, DelPledgeAnnouncePacket);
const bool IsRegistered_AQ_SEND_PRIVATE_ANNOUNCE = gPacketTableFromAdmin.Bind(101, SendPrivateAnnouncePacket);
const bool IsRegistered_AQ_GET_AGIT_LIST = gPacketTableFromAdmin.Bind(102, GetAgitListPacket);
const bool IsRegistered_AQ_GET_PLEDGE_MEMBER = gPacketTableFromAdmin.Bind(103, GetPledgeMemberPacket);
const bool IsRegistered_AQ_ETERNAL_BAN = gPacketTableFromAdmin.Bind(104, EternalBanPacket);
const bool IsRegistered_AQ_GET_CHARACTERS_4 = gPacketTableFromAdmin.Bind(105, GetCharacters4Packet);
const bool IsRegistered_AQ_SET_NOBLESS = gPacketTableFromAdmin.Bind(106, SetNoblessPacket);
const bool IsRegistered_AQ_SET_HERO = gPacketTableFromAdmin.Bind(107, SetHeroPacket);
const bool IsRegistered_AQ_SET_PARTY_LOCATION = gPacketTableFromAdmin.Bind(108, SetPartyLocationPacket);
const bool IsRegistered_AQ_MOD_OLYMPIAD_POINT = gPacketTableFromAdmin.Bind(109, ModOlympiadPointPacket);
const bool IsRegistered_AQ_GM_REG = gPacketTableFromAdmin.Bind(110, GmRegPacket);
const bool IsRegistered_AQ_CREATE_SUBJOB = gPacketTableFromAdmin.Bind(111, CreateSubjobPacket);
const bool IsRegistered_AQ_CHECK_PCBANG_CHAR = gPacketTableFromAdmin.Bind(112, CheckPCBangCharPacket);
const bool IsRegistered_AQ_SET_PCCAFE_POINT = gPacketTableFromAdmin.Bind(113, SetPccafePointPacket);
const bool IsRegistered_AQ_SET_PCCAFE_POINT_RELATIVE = gPacketTableFromAdmin.Bind(114, SetPccafePointRelativePacket);
const bool IsRegistered_AQ_SET_QUEST_2 = gPacketTableFromAdmin.Bind(115, SetQuest2Packet);
const bool IsRegistered_AQ_SET_PLEDGE_NAME_VALUE = gPacketTableFromAdmin.Bind(116, SetPledgeNameValuePacket);
const bool IsRegistered_AQ_MOVE_ITEM_3 = gPacketTableFromAdmin.Bind(117, MoveItem3Packet);
const bool IsRegistered_AQ_SEIZE_ITEM_3 = gPacketTableFromAdmin.Bind(118, SeizeItem3Packet);
const bool IsRegistered_AQ_ADD_PLEDGE_SKILL = gPacketTableFromAdmin.Bind(119, AddPledgeSkillPacket);
const bool IsRegistered_AQ_MOD_PLEDGE_SKILL = gPacketTableFromAdmin.Bind(120, ModPledgeSkillPacket);
const bool IsRegistered_AQ_DEL_PLEDGE_SKILL = gPacketTableFromAdmin.Bind(121, DelPledgeSkillPacket);
const bool IsRegistered_AQ_SET_COLOR = gPacketTableFromAdmin.Bind(122, SetColorPacket);
const bool IsRegistered_AQ_GET_CHARACTERS_EXCEPT_BAN = gPacketTableFromAdmin.Bind(124, GetCharactersExceptBanPacket);
const bool IsRegistered_AQ_TRANSFER_PLEDGE_MASTER = gPacketTableFromAdmin.Bind(123, TransferPledgeMaster);
const bool IsRegistered_AQ_ADD_RECIPE_BOOK = gPacketTableFromAdmin.Bind(125, AddRecipeBookPacket);
const bool IsRegistered_AQ_DISABLE_ADDED_SERVICE = gPacketTableFromAdmin.Bind(126, DisableAddedServicePacket);
const bool IsRegistered_AQ_GET_PLEDGE_MEMBER_2 = gPacketTableFromAdmin.Bind(127, GetPledgeMemberPacket2);
const bool IsRegistered_AQ_GET_PLEDGE_3 = gPacketTableFromAdmin.Bind(128, GetPledge3Packet);
const bool IsRegistered_AQ_GET_FRIEND = gPacketTableFromAdmin.Bind(129, GetFriendPacket);
const bool IsRegistered_AQ_DELETE_SEIZED_ITEM = gPacketTableFromAdmin.Bind(130, DelSeizedItemPacket);
const bool IsRegistered_AQ_GET_ACCOUNT_ID = gPacketTableFromAdmin.Bind(131, GetAccountId);
const bool IsRegistered_AQ_GET_PET_LIST = gPacketTableFromAdmin.Bind(132, GetPetListPacket);
const bool IsRegistered_AQ_GET_SUBJOB_LEVEL = gPacketTableFromAdmin.Bind(133, GetSubjobLevelPacket);
const bool IsRegistered_AQ_ADD_SUBPLEDGE_SKILL = gPacketTableFromAdmin.Bind(134, AddSubPledgeSKillPacket);
const bool IsRegistered_AQ_DEL_SUBPLEDGE_SKILL = gPacketTableFromAdmin.Bind(135, DelSubPledgeSKillPacket);
const bool IsRegistered_AQ_MOD_SUBPLEDGE_SKILL = gPacketTableFromAdmin.Bind(136, ModSubPledgeSkillPacket);
const bool IsRegistered_AQ_MOD_PVP_POINT = gPacketTableFromAdmin.Bind(137, ModPvpPointPacket);
const bool IsRegistered_AQ_MOD_VITALITY_POINT = gPacketTableFromAdmin.Bind(138, ModVitalityPointPacket);
const bool IsRegistered_AQ_MOD_ITEM_PERIOD = gPacketTableFromAdmin.Bind(139, ModItemPeriod);
const bool IsRegistered_AQ_ADD_PREMIUM_ITEM = gPacketTableFromAdmin.Bind(140, AddPremiumItemPacket);
const bool IsRegistered_AQ_SEIZE_PREMIUM_ITEM = gPacketTableFromAdmin.Bind(141, SeizePremiumItemPacket);
const bool IsRegistered_AQ_DEL_PREMIUM_ITEM = gPacketTableFromAdmin.Bind(142, DelPremiumItemPacket);
const bool IsRegistered_AQ_MOVE_PREMIUM_ITEM = gPacketTableFromAdmin.Bind(143, MovePremiumItemPacket);
const bool IsRegistered_AQ_SET_USER_BOOKMARK = gPacketTableFromAdmin.Bind(144, SetUserBookMarkPacket);
const bool IsRegistered_AQ_DELETE_USER_BOOKMARK = gPacketTableFromAdmin.Bind(145, DelUserBookMarkPacket);
const bool IsRegistered_AQ_MOD_USER_COLOR_PREMIUM = gPacketTableFromAdmin.Bind(146, ModUserColorPremiumPacket);
const bool IsRegistered_AQ_MOD_BOT_POINT = gPacketTableFromAdmin.Bind(147, ModBOTPointPacket);
const bool IsRegistered_AQ_DELETE_RECEIVED_MAIL = gPacketTableFromAdmin.Bind(148, DeleteReceivedMail);
const bool IsRegistered_AQ_GET_CHAR_CREATE_DATE = gPacketTableFromAdmin.Bind(149, GetCharCreateDatePacket);
const bool IsRegistered_AQ_GET_SSQ_MAIN_EVENT_RECORD_2 = gPacketTableFromAdmin.Bind(150, GetSSQMainEventRecordPacket2);
const bool IsRegistered_AQ_DELETE_POST = gPacketTableFromAdmin.Bind(151, DeletePostPacket);
const bool IsRegistered_AQ_SEIZE_POST_ITEM = gPacketTableFromAdmin.Bind(152, SeizePostItemPacket);
const bool IsRegistered_AQ_DELETE_POST_ITEM = gPacketTableFromAdmin.Bind(153, DeletePostItemPacket);
const bool IsRegistered_AQ_MOVE_POST_ITEM = gPacketTableFromAdmin.Bind(154, MovePostItemPacket);
const bool IsRegistered_AQ_CHECK_CACHED = gPacketTableFromAdmin.Bind(155, CheckCacheD);
const bool IsRegistered_AQ_BROADCAST_INSTANT_ANNOUNCE = gPacketTableFromAdmin.Bind(156, BroadcastInstantAnnounce);
const bool IsRegistered_AQ_RELOAD_CHAR_INFO = gPacketTableFromAdmin.Bind(157, ReloadCharInfo);
const bool IsRegistered_AQ_WITHDRAW_PREMIUM_ITEM_AT_NPC = gPacketTableFromAdmin.Bind(158, WithdrawPremiumItemAtNPC);
const bool IsRegistered_AQ_SET_PETITION_DISABLE_TIME = gPacketTableFromAdmin.Bind(159, SetPetitionDisableTime);
const bool IsRegistered_AQ_UPDATE_EVENT_DATA = gPacketTableFromAdmin.Bind(160, AQHandler::RequestUpdateEventData);
const bool IsRegistered_AQ_SET_REWARD_NPC = gPacketTableFromAdmin.Bind(161, SetRewardNpc);
const bool IsRegistered_AQ_SEIZE_COMMISSION_ITEM = gPacketTableFromAdmin.Bind(162, AQHandler::RequestSeizeCommissionItem);
const bool IsRegistered_AQ_DELETE_COMMISSION_ITEM = gPacketTableFromAdmin.Bind(163, AQHandler::RequestDeleteCommissionItem);
const bool IsRegistered_AQ_DELETE_BUFF = gPacketTableFromAdmin.Bind(164, AQHandler::RequestDeleteBuff);
const bool IsRegistered_AQ_DUMMY_165 = gPacketTableFromAdmin.Bind(165, DummyPacket);
const bool IsRegistered_AQ_ADD_DUAL_CLASS = gPacketTableFromAdmin.Bind(166, AQHandler::RequestAddDualClass);
const bool IsRegistered_AQ_CANCEL_DUAL_CLASS = gPacketTableFromAdmin.Bind(167, AQHandler::RequestCancelDualClass);
const bool IsRegistered_AQ_RESET_INZONE_RESTRICT = gPacketTableFromAdmin.Bind(168, AQHandler::RequestResetInzoneRestrict);
const bool IsRegistered_AQ_SEND_POST = gPacketTableFromAdmin.Bind(169, AQHandler::RequestSendPost);
const bool IsRegistered_AQ_VALIDATE_POST_USER = gPacketTableFromAdmin.Bind(170, AQHandler::RequestValidatePostUser);
const bool IsRegistered_AQ_BROADCAST_SCREEN_ANNOUNCE = gPacketTableFromAdmin.Bind(171, BroadcastScreenAnnounce);
const bool IsRegistered_AQ_ADD_RECIPE_LIST = gPacketTableFromAdmin.Bind(172, AQHandler::RequestAddRecipeListPacket);
const bool IsRegistered_AQ_ADD_SHORTCUT_LIST = gPacketTableFromAdmin.Bind(173, AQHandler::RequestAddShortcutListPacket);
const bool IsRegistered_AQ_MODIFY_PLEDGE_GAME_INFO =
    gPacketTableFromAdmin.Bind(174, AQHandler::RequestModifyPledgeGameInfoPacket);
const bool IsRegistered_AQ_CHECK_FREE_USER = gPacketTableFromAdmin.Bind(175, CheckFreeUserPacket);
const bool IsRegistered_AQ_ADD_ACTIVE_SKILL = gPacketTableFromAdmin.Bind(176, AQHandler::RequestAddActiveSkill);
const bool IsRegistered_AQ_DELETE_SKILL_DELAY = gPacketTableFromAdmin.Bind(177, AQHandler::RequestDeleteSkillDelay);
const bool IsRegistered_AQ_CHECK_RAID_USER = gPacketTableFromAdmin.Bind(178, AQHandler::RequestCheckRaidUser);
const bool IsRegistered_AQ_SET_INFORMAR = gPacketTableFromAdmin.Bind(179, AQHandler::RequestSetInformar);
const bool IsRegistered_AQ_RESET_ABILITY_POINT = gPacketTableFromAdmin.Bind(180, AQHandler::RequestResetAbilityPoint);
const bool IsRegistered_AQ_MODIFY_RAID_POINT = gPacketTableFromAdmin.Bind(181, AQHandler::RequestModifyRaidPoint);
const bool IsRegistered_AQ_MODIFY_WORLDCHAT_POINT = gPacketTableFromAdmin.Bind(182, AQHandler::RequestModifyWorldchatPoint);
const bool IsRegistered_AQ_ADD_ABILITY_SKILL = gPacketTableFromAdmin.Bind(183, AQHandler::RequestAddAbilitySkill);
const bool IsRegistered_AQ_ADD_ABILITY_POINT = gPacketTableFromAdmin.Bind(184, AQHandler::RequestAddAbilityPoint);
const bool IsRegistered_AQ_MODIFY_VIP_POINT = gPacketTableFromAdmin.Bind(185, AQHandler::RequestModifyVipPoint);
const bool IsRegistered_AQ_RESET_WORLD_OLYMPIAD_POINT =
    gPacketTableFromAdmin.Bind(186, AQHandler::RequestResetWorldOlympiadPoint);
const bool IsRegistered_AQ_SET_REWARD_NPC_V2 = gPacketTableFromAdmin.Bind(187, AQHandler::RequestSetRewardNpcVer2);
const bool IsRegistered_AQ_GM_ANNOUNCE_SEND = gPacketTableFromAdmin.Bind(188, GMAnnounceSend);
const bool IsRegistered_AQ_GM_ANNOUNCE_DELETE = gPacketTableFromAdmin.Bind(189, GMAnnounceDel);
const bool IsRegistered_AQ_GM_ANNOUNCE_TO_SERVER = gPacketTableFromAdmin.Bind(190, GMAnnounceSendToServer);

// Current Cached.exe DAT_007ec1f0. GMAnnounceSend compares this raw
// process-wide GetTickCount value without synchronization or wrap handling.
std::uint32_t g_lastGmAnnounceSendTick = 0;

std::u16string AdminLogText(const wchar_t* value) {
    if (!value)
        return {};
    return WideToUtf16(value);
}

std::u16string AdminLogText(const char16_t* value) { return value ? std::u16string(value) : std::u16string(); }

void WriteAdminQuestAudit(
    const CUser& user, int logId, int questId, bool includeQuestState, int questState, const char16_t* auditText
) {
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(logId);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user.GetId());
    fields[4] = runtime::DecimalUtf16(user.GetAccountID());
    fields[11] = runtime::DecimalUtf16(questId);
    if (includeQuestState) {
        fields[12] = runtime::DecimalUtf16(questState);
    }
    fields[21] = AdminLogText(auditText);
    fields[22] = AdminLogText(auditText);
    fields[23] = AdminLogText(user.GetCharName());
    fields[24] = AdminLogText(user.GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

// Current Cached.exe helper 0x00411fb8 has no surviving PDB name. This
// semantic name keeps its exact force-add owner and mode-1 L2Server frame.
bool SetAdminAcquireSkillAndNotifyAdd(const CUserSP& user, int skillId, int skillLevel, int skillSubLevel) {
    const bool updated = user->SetAquireSkill(skillId, skillLevel, skillSubLevel, true, -1);
    if (!updated || !user->IsLoggedIn()) {
        return updated;
    }

    CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (server != nullptr) {
        const std::uint32_t packedBits = ((static_cast<std::uint32_t>(skillSubLevel) & 0xffffU) << 16) |
            (static_cast<std::uint32_t>(skillLevel) & 0xffffU);
        int packedLevel = 0;
        static_assert(sizeof(packedLevel) == sizeof(packedBits));
        std::memcpy(&packedLevel, &packedBits, sizeof(packedLevel));
        server->Send("cdddd", 0x22, user->GetId(), 1, skillId, packedLevel);
    }
    return true;
}

std::u16string MakeAdminVariationLogText(int intensiveItemType, const ItemOptionKey& optionKey) {
    return Utf8ToUtf16(
        std::to_string(intensiveItemType) + ":" + std::to_string(optionKey.variationOption1) + ":" +
        std::to_string(optionKey.variationOption2)
    );
}

std::u16string MakeAdminMoveItemLogText(int intensiveItemType, CItem& item) {
    std::array<char16_t, 256> baseAttributeText{};
    BaseAttribute baseAttribute(item.Attribute());
    baseAttribute.GetLogString(baseAttributeText.data());

    std::array<char16_t, 256> shapeShiftText{};
    item.MakeShapeShiftLogInfo(shapeShiftText.data());

    return FormatUtf16(
        u"%d:%d:%d:%s:%s", intensiveItemType, item.OptionKey().variationOption1, item.OptionKey().variationOption2,
        baseAttributeText.data(), shapeShiftText.data()
    );
}

std::u16string MakeAdminMoveItem2LogText(int intensiveItemType, CItem& item) {
    // AQ 40 predates the fifth shape-shift component used by the newer
    // MoveItemProc owner. Cached.exe 0x00419af0 formats exactly four fields.
    std::array<char16_t, 256> baseAttributeText{};
    BaseAttribute baseAttribute(item.Attribute());
    baseAttribute.GetLogString(baseAttributeText.data());

    return FormatUtf16(
        u"%d:%d:%d:%s", intensiveItemType, item.OptionKey().variationOption1, item.OptionKey().variationOption2,
        baseAttributeText.data()
    );
}

void WriteAdminMoveItemAudit(
    CUser& sourceUser,
    CUser& destinationUser,
    const std::u16string& itemLog,
    int itemType,
    std::int64_t amount,
    const wchar_t* auditText,
    int itemId
) {
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x280);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[8] = AdminLogText(sourceUser.GetCharName());
    fields[9] = AdminLogText(destinationUser.GetCharName());
    fields[10] = itemLog;
    fields[11] = runtime::DecimalUtf16(sourceUser.GetId());
    fields[12] = runtime::DecimalUtf16(destinationUser.GetId());
    fields[13] = runtime::DecimalUtf16(itemType);
    fields[14] = runtime::DecimalUtf16(amount);
    fields[21] = AdminLogText(auditText);
    fields[22] = AdminLogText(auditText);
    fields[25] = runtime::DecimalUtf16(itemId);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteAdminDeleteItemAudit(
    CUser* user,
    const std::u16string& baseAttribute,
    const std::u16string& variation,
    int itemType,
    std::int64_t removedAmount,
    std::int64_t remainingAmount,
    int itemId
) {
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x27f);
    if (user) {
        fields[1] = runtime::DecimalUtf16(user->GetId());
        fields[2] = runtime::DecimalUtf16(user->GetAccountID());
        fields[5] = runtime::DecimalUtf16(user->GetX());
        fields[6] = runtime::DecimalUtf16(user->GetY());
        fields[7] = runtime::DecimalUtf16(user->GetZ());
        fields[21] = AdminLogText(user->GetCharName());
        fields[22] = AdminLogText(user->GetAccountName());
    }
    fields[9] = baseAttribute;
    fields[10] = variation;
    fields[18] = runtime::DecimalUtf16(itemType);
    fields[19] = runtime::DecimalUtf16(removedAmount);
    fields[20] = runtime::DecimalUtf16(remainingAmount);
    fields[25] = runtime::DecimalUtf16(itemId);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteAdminSeizeItemAudit(
    int ownerId,
    const std::u16string& baseAttribute,
    const std::u16string& variation,
    int option,
    int originalWarehouse,
    int itemType,
    std::int64_t amount,
    const wchar_t* auditText,
    const std::u16string& ownerName,
    const std::u16string& accountName,
    int itemId
) {
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x271);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(ownerId);
    fields[9] = baseAttribute;
    fields[10] = variation;
    fields[11] = runtime::DecimalUtf16(option);
    fields[12] = runtime::DecimalUtf16(originalWarehouse);
    fields[13] = runtime::DecimalUtf16(itemType);
    fields[14] = runtime::DecimalUtf16(amount);
    fields[21] = AdminLogText(auditText);
    fields[22] = AdminLogText(auditText);
    fields[23] = ownerName;
    fields[24] = accountName;
    fields[25] = runtime::DecimalUtf16(itemId);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteAdminCreateCharacter2Audit(
    const CUser& user,
    int builder,
    int gender,
    int race,
    int characterClass,
    double hp,
    double mp,
    std::int64_t sp,
    std::int64_t experience,
    int level,
    int subjobId,
    const char16_t* auditText
) {
    // Current CreateCharacter2Packet 0x0043ccf0 emits formatter record
    // 0x297 after the new CUser has been created. Floating HP/MP are
    // converted with CVTTSD2SI before entering the integer-only formatter.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x297);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user.GetId());
    fields[4] = runtime::DecimalUtf16(user.GetAccountID());
    fields[11] = runtime::DecimalUtf16(builder);
    fields[12] = runtime::DecimalUtf16(gender);
    fields[13] = runtime::DecimalUtf16(race);
    fields[14] = runtime::DecimalUtf16(characterClass);
    fields[15] = runtime::DecimalUtf16(static_cast<int>(hp));
    fields[16] = runtime::DecimalUtf16(static_cast<int>(mp));
    fields[17] = runtime::DecimalUtf16(sp);
    fields[18] = runtime::DecimalUtf16(experience);
    fields[19] = runtime::DecimalUtf16(level);
    fields[20] = runtime::DecimalUtf16(subjobId);
    fields[21] = AdminLogText(auditText);
    fields[22] = AdminLogText(auditText);
    fields[23] = AdminLogText(user.GetAccountName());
    fields[24] = AdminLogText(user.GetCharNameLow());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

bool ExecuteAdminSeizeItem2(int option, int warehouse, int itemId) {
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SeizeItem2 (?, ?, ? )}";
    connection.Prepare(statement);
    connection.BindParameter(&option);
    connection.BindParameter(&warehouse);
    connection.BindParameter(&itemId);
    // return connection.ExecutePrepared(statement)
    const bool executed = connection.ExecutePrepared(statement);
    if (!executed) {
        // Log.Add(LOG_ERROR,
        //     "[admin seizure] Item %d, option %d, destination warehouse %d: database execution failed. Item seizure reports failure.",
        //     itemId, option, warehouse)
        L2_LOG_ERROR(Log,
            "[admin seizure] Item %d, option %d, destination warehouse %d: database execution failed; item seizure reports failure.",
            itemId, option, warehouse);
    }
    return executed;
}

} // namespace

L2_DEFINE_OBJECT_CLASS_ID(CAdminSocket)

L2_DEFINE_OBJECT_CLASS_NAME(CAdminSocket, m_pClassName_CAdminSocket)

L2_DEFINE_OBJECT_NEW(CAdminSocket)

// Current allocator 0x0040e698 publishes 0x190 bytes. The complete CRT
// table makes 0x0069ba40 the eighth MemoryObject class initializer.
L2_DEFINE_OBJECT_ALLOCATOR(CAdminSocket, m_pClassName_CAdminSocket)

L2_DEFINE_OBJECT_RELEASE(CAdminSocket)

CAdminSocket::CAdminSocket(std::uint64_t socket, int bufferSize) : CSocket(socket, bufferSize) {
    L2_GUARD_FUNCTION();
    m_packetTable = reinterpret_cast<CacheDPacketCallback*>(gPacketTableFromAdmin.GetData());
}

CAdminSocket::~CAdminSocket() { L2_GUARD_FUNCTION(); }

CSocket* CAdminSocket::Allocate(std::uint64_t socket) {
    L2_GUARD_FUNCTION();
    return new CAdminSocket(socket, 0x5000);
}

void CAdminSocket::SetAddress(::in_addr address) {
    L2_GUARD_FUNCTION();
    m_address = address;
}

void CAdminSocket::OnCreate() {
    L2_GUARD_FUNCTION();
    CacheDIOSocket::OnCreate();
}

void CAdminSocket::OnClose() {
    L2_GUARD_FUNCTION();
    const int error = GetSocketLastError();
    std::array<char, INET_ADDRSTRLEN> address{};
    const char* const converted = ::inet_ntop(AF_INET, &m_address, address.data(), static_cast<socklen_t>(address.size()));
    // Log.Add(LOG_NORMAL, "admin server closed(%s) error: %d", converted ? converted : "0.0.0.0", error)
    // Log.Add(LOG_NORMAL,
    //     "[admin] Connection from %s closed. Last socket error observed: %d. This saved value may not identify the cause.",
    //     converted ? converted : "unknown address", error)
    L2_LOG_INFO(Log,
        "[admin] Connection from %s closed. Last socket error observed: %d; this saved value may not identify the cause.",
        converted ? converted : "unknown address", error);
    runtime::raw_atomic::StoreEnum32Relaxed(&m_mode, SocketMode::SM_CLOSE);
    runtime::raw_atomic::ExchangeI32(reinterpret_cast<volatile std::int32_t*>(&m_socket), 0);
}

void CAdminSocket::OnRead() {
    L2_GUARD_FUNCTION();
    const auto* peerBytes = reinterpret_cast<const std::uint8_t*>(&m_address.s_addr);
    if (runtime::raw_atomic::LoadEnum32Relaxed(&m_mode) == SocketMode::SM_CLOSE) {
        Close();
        return;
    }
    // if (m_nPendingWrite.load(std::memory_order_relaxed) >= 0x1000001) {
    //     Close()
    //     return.
    // }
    const int pendingWrite = m_nPendingWrite.load(std::memory_order_relaxed);
    if (pendingWrite >= 0x1000001) {
        // Log.Add(LOG_ERROR,
        //     "[admin] Closing connection %llu from %u.%u.%u.%u: pending writes reached %d bytes (limit 16777216).",
        //     static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]), pendingWrite)
        L2_LOG_ERROR(Log,
            "[admin] Closing connection %llu from %u.%u.%u.%u: pending writes reached %d bytes (limit 16777216).",
            static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]), pendingWrite);
        Close();
        return;
    }

    const int readSize = static_cast<int>(m_pReadBuf->m_dwSize);
    int packetOffset = 0;
    while (packetOffset < readSize) {
        if (runtime::raw_atomic::LoadEnum32Relaxed(&m_mode) == SocketMode::SM_READ_LEN) {
            if (readSize - packetOffset < 2) {
                Read(static_cast<std::uint32_t>(readSize - packetOffset));
                return;
            }
            const std::uint8_t* const header = m_pReadBuf->m_Buffer.data() + packetOffset;
            m_packetLength =
                static_cast<int>(static_cast<std::uint16_t>(header[0]) | (static_cast<std::uint16_t>(header[1]) << 8U)) - 2;
            if (m_packetLength < 1 || m_packetLength > 0x4fff) {
                // Log.Add(
                //     LOG_ERROR, "admin socket %d: bad packet size %d",
                //     static_cast<int>(reinterpret_cast<std::intptr_t>(&::socket)), m_packetLength
                // )
                // Log.Add(LOG_WARN,
                //     "[admin] Closing connection %llu from %u.%u.%u.%u: invalid incoming body length %d bytes (accepted range 1..20479).",
                //     static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]), m_packetLength)
                L2_LOG_WARN(Log,
                    "[admin] Closing connection %llu from %u.%u.%u.%u: invalid incoming body length %d bytes (accepted range 1..20479).",
                    static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]), m_packetLength);
                runtime::raw_atomic::StoreEnum32Relaxed(&m_mode, SocketMode::SM_CLOSE);
                Close();
                return;
            }
            runtime::raw_atomic::StoreEnum32Relaxed(&m_mode, SocketMode::SM_READ_BODY);
            packetOffset += 2;
        }

        if (runtime::raw_atomic::LoadEnum32Relaxed(&m_mode) != SocketMode::SM_READ_BODY) {
            // Log.Add(LOG_ERROR,
            //     "[admin] Closing connection %llu from %u.%u.%u.%u: packet processing found an unexpected read state.",
            //     static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]))
            L2_LOG_ERROR(Log,
                "[admin] Closing connection %llu from %u.%u.%u.%u: packet processing found an unexpected read state.",
                static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]));
            Close();
            return;
        }
        if (readSize - packetOffset < m_packetLength) {
            Read(static_cast<std::uint32_t>(readSize - packetOffset));
            return;
        }

        ++m_packetCount;
        if (m_packetCount == 0) {
            m_createdTick = runtime::GetTickCount();
        }
        std::uint8_t* const packet = m_pReadBuf->m_Buffer.data() + packetOffset;
        Decrypt(packet, static_cast<std::int64_t>(m_key), m_packetLength);
        m_key += static_cast<std::uint64_t>(m_packetLength);

        const std::uint8_t protocol = packet[0];
        auto* const callbacks = reinterpret_cast<AdminPacketCallback*>(m_packetTable);
        if (protocol >= 191 || callbacks[protocol](this, packet + 1)) {
            // Log.Add(protocol >= 191 ? LOG_WARN : LOG_NORMAL,
            //     "[admin] Closing connection %llu from %u.%u.%u.%u: %s (protocol %u).",
            //     static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]), protocol >= 191 ? "unknown protocol, accepted range is 0..190" : "the command handler requested closure", static_cast<unsigned>(protocol))
            L2_LOG(Log, protocol >= 191 ? LOG_WARN : LOG_NORMAL,
                "[admin] Closing connection %llu from %u.%u.%u.%u: %s (protocol %u).",
                static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]), protocol >= 191 ? "unknown protocol; accepted range is 0..190" : "the command handler requested closure", static_cast<unsigned>(protocol));
            runtime::raw_atomic::StoreEnum32Relaxed(&m_mode, SocketMode::SM_CLOSE);
            Close();
            return;
        }

        packetOffset += m_packetLength;
        runtime::raw_atomic::StoreEnum32Relaxed(&m_mode, SocketMode::SM_READ_LEN);
    }
    Read(0);
}

void CAdminSocket::Send(const char* format, ...) {
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::LoadEnum32Relaxed(&m_mode) == SocketMode::SM_CLOSE) {
        return;
    }

    va_list arguments;
    va_start(arguments, format);
    SendV(format, arguments);
    va_end(arguments);
}

void CAdminSocket::SendV(const char* format, va_list arguments) {
    DefaultIOBuffer* const buffer = DefaultIOBuffer::Alloc();
    const int bodySize = ::VAssemble(reinterpret_cast<char*>(buffer->m_Buffer.data() + 2), 0x4ffe, format, arguments);
    if (bodySize == 0) {
        // Log.Add(
        //     LOG_ERROR, "%d: assemble too large packet. format %s",
        //     static_cast<int>(reinterpret_cast<std::intptr_t>(&::socket)), format
        // )
        // Log.Add(LOG_ERROR,
        //     "[admin] Cannot assemble a reply on connection %llu within the 20478-byte body buffer (format %s). Discarding this reply.",
        //     static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), format)
        L2_LOG_ERROR(Log,
            "[admin] Cannot assemble a reply on connection %llu within the 20478-byte body buffer (format %s); discarding this reply.",
            static_cast<unsigned long long>(m_hSocket.load(std::memory_order_relaxed)), format);
        buffer->Release();
        return;
    }

    const int packetSize = bodySize + 2;
    buffer->m_Buffer[0] = static_cast<std::uint8_t>(packetSize);
    buffer->m_Buffer[1] = static_cast<std::uint8_t>(static_cast<unsigned>(packetSize) >> 8U);

    {
        L2_GUARD(m_sendLock);
        buffer->m_dwSize = static_cast<std::uint32_t>(packetSize);
        L2_ADD_REF(this, ObjectRefType::IoSocketWrite);
        SyncWrite(buffer);
    }
}

bool DummyPacket(CAdminSocket* socket, const std::uint8_t*) {
    // Current Cached.exe 0x00411d88 sends the fixed AQ fallback reply
    // cd(0xbe,1) and returns false.
    L2_GUARD_FUNCTION();
    socket->Send("cd", 0xbe, 1);
    return false;
}

bool AdminVersionPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00411e44.
    L2_GUARD_FUNCTION();

    std::array<char, 80> revisionLabel{};
    int currentRevision = 0;
    std::sscanf("$Revision: #56 $", "%s #%d", revisionLabel.data(), &currentRevision);

    int requestedRevision = 0;
    ::Disassemble(packet, "d", &requestedRevision);
    if (requestedRevision == -1) {
        socket->m_rejectAdminRequests = true;
        // Log.Add(LOG_NORMAL,
        //     "[admin] Revision -1 requested, administrative request rejection is enabled for this connection.")
        L2_LOG_INFO(Log,
            "[admin] Revision -1 requested; administrative request rejection is enabled for this connection.");
        socket->Send("cd", 0, 1);
    } else if (requestedRevision == currentRevision) {
        socket->Send("cd", 0, 1);
    } else {
        // Log.Add(LOG_WARN,
        //     "[admin] Protocol revision %d rejected. Required revision %d. Sending reason 28 and keeping the connection open.",
        //     requestedRevision, currentRevision)
        L2_LOG_WARN(Log,
            "[admin] Protocol revision %d rejected; required revision %d. Sending reason 28 and keeping the connection open.",
            requestedRevision, currentRevision);
        socket->Send("cdd", 0, 0, 0x1c);
    }
    return false;
}

bool CheckCharacterPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00413468, AQ 1.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 1, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dS", &characterId, 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[adminsocket.cpp][%d] Cannot find user, char id[%d]", 0x18d, characterId)
        // Log.Add(
        //     LOG_WARN,
        //     "[admin] Cannot check character %d: character unavailable. Returning failure.",
        //     characterId
        // )
        L2_LOG_WARN(Log,
            "[admin] Cannot check character %d: character unavailable; returning failure.",
            characterId
        );
        socket->Send("cdd", 1, 0, 4);
        return false;
    }
    if (!user->IsLoggedIn()) {
        socket->Send("cdd", 1, 0, 0x0f);
        return false;
    }

    socket->Send("cd", 1, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x259);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[11] = runtime::DecimalUtf16(user->IsLoggedIn() ? 1 : 0);
    fields[21] = AdminLogText(auditTextUtf16.data());
    fields[22] = AdminLogText(auditTextUtf16.data());
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    // Noise: Routine successful character checks are already recorded in the accounting log and returned to the requester.
    // Log.Add(LOG_NORMAL, "*check character:%d", characterId)
    return false;
}

bool SetCharacterLocationPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004138d0, AQ 2.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 2, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int worldId = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dddddS", &characterId, &worldId, &x, &y, &z, 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[adminsocket.cpp][%d] Cannot find user, char id[%d]", 0x1d0, characterId)
        // Log.Add(
        //     LOG_WARN,
        //     "[admin] Cannot move character %d to world %d at (%d, %d, %d): character unavailable. Returning failure.",
        //     characterId, worldId, x, y, z
        // )
        L2_LOG_WARN(Log,
            "[admin] Cannot move character %d to world %d at (%d, %d, %d): character unavailable; returning failure.",
            characterId, worldId, x, y, z
        );
        socket->Send("cdd", 2, 0, 4);
        return false;
    }

    bool changed = false;
    // if (user->IsLoggedIn()) {
    const bool wasLoggedIn = user->IsLoggedIn();
    if (wasLoggedIn) {
        user->SetPos(x, y, z);
        CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
        if (server != nullptr) {
            server->Send("cddddd", 0x2a, user->GetId(), worldId, x, y, z);
            changed = true;
        }
    } else {
        changed = user->ChangeLocationExtern(user->GetCharName(), x, y, z, true);
    }

    if (!changed) {
        // Log.Add(
        //     LOG_ERROR, "*cannot change location:%ls (%d,%d,%d,%d)", user->GetCharName(), worldId, x, y, z
        // )
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot complete move for character %d (%s) to world %d at (%d, %d, %d): %s. Returning failure.",
        //     characterId, WideToUtf8(user->GetCharName()).c_str(), worldId, x, y, z,
        //     wasLoggedIn ? "L2Server unavailable after updating the cached position" : "database location change did not return a valid character"
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot complete move for character %d (%s) to world %d at (%d, %d, %d): %s; returning failure.",
            characterId, Utf16ToUtf8(user->GetCharName()).c_str(), worldId, x, y, z,
            wasLoggedIn ? "L2Server unavailable after updating the cached position" : "database location change did not return a valid character"
        );
        socket->Send("cdd", 2, 0, 1);
        return false;
    }

    socket->Send("cd", 2, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x25a);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[5] = runtime::DecimalUtf16(user->GetX());
    fields[6] = runtime::DecimalUtf16(user->GetY());
    fields[7] = runtime::DecimalUtf16(user->GetZ());
    fields[11] = runtime::DecimalUtf16(worldId);
    fields[12] = runtime::DecimalUtf16(x);
    fields[13] = runtime::DecimalUtf16(y);
    fields[14] = runtime::DecimalUtf16(z);
    fields[21] = AdminLogText(auditTextUtf16.data());
    fields[22] = AdminLogText(auditTextUtf16.data());
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    // Log.Add(LOG_NORMAL, "*location changed:%ls (%d,%d,%d,%d)", user->GetCharName(), worldId, x, y, z)
    // Log.Add(
    //     LOG_NORMAL,
    //     "[admin] Move for character %d (%s) to world %d at (%d, %d, %d): %s.",
    //     characterId, WideToUtf8(user->GetCharName()).c_str(), worldId, x, y, z,
    //     wasLoggedIn ? "cached position updated and command sent to L2Server. Remote completion is not confirmed" : "database location change returned a valid character and cached position was updated"
    // )
    L2_LOG_INFO(Log,
        "[admin] Move for character %d (%s) to world %d at (%d, %d, %d): %s.",
        characterId, Utf16ToUtf8(user->GetCharName()).c_str(), worldId, x, y, z,
        wasLoggedIn ? "cached position updated and command sent to L2Server; remote completion is not confirmed" : "database location change returned a valid character and cached position was updated"
    );
    return false;
}

bool SetBuilderCharacterPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00413f8c, AQ 3.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 3, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int builderLevel = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddS", &characterId, &builderLevel, 0x200, auditTextUtf16.data());
    if (static_cast<std::uint32_t>(builderLevel) + 2U >= 0x0dU) {
        socket->Send("cdd", 3, 0, 3);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 3, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 3, 0, 7);
        return false;
    }
    if (!user->SetBuilderLevExtern(user->GetCharName(), builderLevel)) {
        socket->Send("cdd", 3, 0, 1);
        return false;
    }

    socket->Send("cd", 3, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x25b);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[11] = runtime::DecimalUtf16(builderLevel);
    fields[21] = AdminLogText(auditTextUtf16.data());
    fields[22] = AdminLogText(auditTextUtf16.data());
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool ChangeCharacterNamePacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00432854, AQ 4.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 4, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::array<char16_t, 256> newNameUtf16{};
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dSS", &characterId, 0x200, newNameUtf16.data(), 0x200, auditTextUtf16.data());

    std::wstring newName = Utf16ToWide(newNameUtf16.data());
    std::wstring newNameLow = newName;
    Utils::CopyStrInLowerCase(newName.c_str(), newNameLow.data());
    if (!CUser::CheckCharacterName(newNameLow.c_str(), GetCacheDConfiguration().country, false) || newNameLow.empty()) {
        socket->Send("cdd", 4, 0, 3);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 4, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 4, 0, 7);
        return false;
    }

    CUserSP existing = g_userDb.GetUser(WideToUtf16(newNameLow.c_str()).c_str(), true);
    if (existing && existing->GetId() != static_cast<std::uint32_t>(characterId)) {
        socket->Send("cdd", 4, 0, 0x0c);
        return false;
    }

    const std::u16string oldNameLow(user->GetCharNameLow());
    if (!user->ChangeCharacternameExtern(newNameUtf16.data())) {
        socket->Send("cdd", 4, 0, 1);
        return false;
    }

    CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (server != nullptr) {
        FriendIdSet friendIds;
        g_friendsDb.GetFriendIdList(user->GetId(), friendIds);
        CPacketHelperEx<20480> friendPacket;
        for (const unsigned int friendId : friendIds) {
            friendPacket.Assemble("d", static_cast<int>(friendId));
        }

        const std::u16string changedNameUtf16 = AdminLogText(user->GetCharName());
        const std::u16string oldNameUtf16 = AdminLogText(oldNameLow.c_str());
        server->Send(
            "cddSSdddb", 0x94, 6, user->GetId(), changedNameUtf16.c_str(), oldNameUtf16.c_str(), user->GetPledgeID(),
            user->GetMainClass(), friendPacket.GetAssembledCount(), friendPacket.GetLength(), friendPacket.GetPacket()
        );
    }

    socket->Send("cd", 4, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x25c);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[8] = AdminLogText(newNameLow.c_str());
    fields[21] = AdminLogText(auditTextUtf16.data());
    fields[22] = AdminLogText(auditTextUtf16.data());
    fields[23] = AdminLogText(oldNameLow.c_str());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool KickCharacterPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0041440c, AQ 5.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 5, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dS", &characterId, 0x200, auditTextUtf16.data());
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 5, 0, 4);
        return false;
    }

    if (user->IsLoggedIn()) {
        CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
        if (server != nullptr) {
            server->Send("cdd", 0x21, user->GetAccountID(), 0);
        }
    }
    socket->Send("cd", 5, 1);

    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x25d);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[21] = AdminLogText(auditTextUtf16.data());
    fields[22] = AdminLogText(auditTextUtf16.data());
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool AddSkillPacket(CAdminSocket* socket, const std::uint8_t*) {
    L2_GUARD_FUNCTION();
    socket->Send("cdd", 6, 0, 0x38);
    return false;
}

bool DelSkillPacket(CAdminSocket* socket, const std::uint8_t*) {
    L2_GUARD_FUNCTION();
    socket->Send("cdd", 7, 0, 0x38);
    return false;
}

bool ModSkillPacket(CAdminSocket* socket, const std::uint8_t*) {
    L2_GUARD_FUNCTION();
    socket->Send("cdd", 8, 0, 0x38);
    return false;
}

bool SetOnetimeQuestPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004147bc, AQ 9.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 9, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int questId = 0;
    int questState = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dddS", &characterId, &questId, &questState, 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 9, 0, 4);
        return false;
    }

    user->SetOneTimeQuest(questId, questState == 1);
    user->SaveOneTimeQuest();
    if (user->m_loggedIn) {
        CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
        if (server != nullptr) {
            server->Send("cddddd", 0x23, user->GetId(), 1, questId, questState, 0);
        }
    }

    socket->Send("cd", 9, 1);
    WriteAdminQuestAudit(*user.get(), 0x261, questId, true, questState, auditTextUtf16.data());
    return false;
}

bool SetQuestPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00414c30, AQ 10.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 10, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::uint32_t questId = 0;
    std::uint32_t questState = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dddS", &characterId, &questId, &questState, 0x200, auditTextUtf16.data());
    if (questId > 0x10000U || questState > 0x100U) {
        socket->Send("cdd", 10, 0, 3);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 10, 0, 4);
        return false;
    }

    const int signedQuestId = static_cast<int>(questId);
    const int signedQuestState = static_cast<int>(questState);
    if (!user->SetSelectedQuest(signedQuestId, signedQuestState)) {
        socket->Send("cdd", 10, 0, 1);
        return false;
    }

    user->SaveMemo();
    if (user->m_loggedIn) {
        CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
        if (server != nullptr) {
            server->Send("cddddd", 0x23, user->GetId(), 2, signedQuestId, signedQuestState, 0);
        }
    }

    socket->Send("cd", 10, 1);
    // Current adapter 0x0040fe84 is one vararg short for its format. Keep
    // every explicit value and leave the unmatched numeric field blank.
    WriteAdminQuestAudit(*user.get(), 0x262, signedQuestId, true, signedQuestState, auditTextUtf16.data());
    return false;
}

bool DelQuestPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00415154, AQ 11.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 11, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::uint32_t questId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddS", &characterId, &questId, 0x200, auditTextUtf16.data());
    if (questId > 0x10000U) {
        socket->Send("cdd", 11, 0, 3);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 11, 0, 4);
        return false;
    }

    const int signedQuestId = static_cast<int>(questId);
    int nrQuestMarker = 0;
    if (user->DeleteSelectedQuest(signedQuestId)) {
        user->SaveMemo();
    } else {
        if (!user->DeleteSelectedNRQuest(signedQuestId)) {
            socket->Send("cdd", 11, 0, 1);
            return false;
        }
        nrQuestMarker = 1;
    }

    if (user->m_loggedIn) {
        CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
        if (server != nullptr) {
            server->Send("cddddd", 0x23, user->GetId(), 3, signedQuestId, 0, nrQuestMarker);
        }
    }

    socket->Send("cd", 11, 1);
    WriteAdminQuestAudit(*user.get(), 0x263, signedQuestId, false, 0, auditTextUtf16.data());
    return false;
}

bool SetQuest2Packet(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00425e4c, AQ 115. This body has no
    // RejectsAdminRequests gate.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    std::uint32_t questId = 0;
    int questState = 0;
    int questState2 = 0;
    int questState3 = 0;
    int questState4 = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(
        packet, "ddddddS", &characterId, &questId, &questState, &questState2, &questState3, &questState4, 0x200,
        auditTextUtf16.data()
    );
    if (questId > 0x10000U) {
        socket->Send("cdd", 115, 0, 3);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // The current handler really uses AQ 10 on this failure branch.
        socket->Send("cdd", 10, 0, 4);
        return false;
    }

    const int signedQuestId = static_cast<int>(questId);
    if (!user->SetSelectedQuest(signedQuestId, questState, questState2, questState3, questState4)) {
        // The current handler also uses AQ 10 for memo exhaustion.
        socket->Send("cdd", 10, 0, 1);
        return false;
    }

    user->SaveMemo();
    if (user->m_loggedIn) {
        CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
        if (server != nullptr) {
            server->Send(
                "chddddddd", 0xc6, 0x32, user->GetId(), 2, signedQuestId, questState, questState2, questState3, questState4
            );
        }
    }

    socket->Send("cd", 115, 1);
    // The parsed S buffer is absent from current 0x004262eb.
    CLogFormatter formatter;
    formatter.WriteAuditLog(
        0x262, user->GetId(), user->GetAccountID(), 0, 0, user->GetX(), user->GetY(), user->GetZ(), nullptr, nullptr,
        nullptr, signedQuestId, questState, questState2, questState3, questState4, 0, 0, 0, 0, 0, nullptr, nullptr, nullptr,
        nullptr, 0
    );
    return false;
}

bool AddItemPacket(CAdminSocket* socket, const std::uint8_t*) {
    L2_GUARD_FUNCTION();
    socket->Send("cdd", 12, 0, 4);
    return false;
}

bool AddItemProc(
    CAdminSocket* socket,
    int replyOpcode,
    int ownerId,
    int warehouseType,
    int itemType,
    std::int64_t amount,
    int enchant,
    int eroded,
    int bless,
    int ident,
    int wished,
    int variationOption1,
    int variationOption2,
    int intensiveItemType,
    int duration,
    short attackType,
    short attackValue,
    short defendValues[],
    int appearanceType,
    int period,
    CItem::EnsoulInfo ensoulInfo[],
    short hLocked,
    short useCount,
    const wchar_t* auditText
) {
    // Exact current Cached.exe 0x0043ae24. AQ 55 and AQ 140 share this
    // owner. The latter rejects pledge/castle/pet warehouses.
    L2_GUARD_FUNCTION();

    DBConn durationConnection(runtime::process::CurrentDatabaseId());
    if (!auditText || std::wcscmp(auditText, L"") == 0) {
        // Log.Add(LOG_ERROR, "[adminsocket.cpp][4691] No Admin Id")
        // Log.Add(
        //     LOG_WARN,
        //     "[admin] Add-item request rejected: audit text is empty (owner %d, warehouse %d, item type %d, amount %lld, command %d).",
        //     ownerId, warehouseType, itemType, static_cast<long long>(amount), replyOpcode
        // )
        L2_LOG_WARN(Log,
            "[admin] Add-item request rejected: audit text is empty (owner %d, warehouse %d, item type %d, amount %lld, command %d).",
            ownerId, warehouseType, itemType, static_cast<long long>(amount), replyOpcode
        );
        socket->Send("cdd", replyOpcode, 0, 1);
        return false;
    }

    constexpr std::int64_t maximumStackAmount = 999999999999LL;
    int result = 1;
    int itemId = 0;
    bool added = false;
    ItemOptionKey optionKey{variationOption1, variationOption2};
    BaseAttribute baseAttribute(attackType, attackValue, defendValues);

    const auto exceedsStackLimit = [=](const auto& warehouse) {
        CItemSP existing = warehouse->GetItemByType(itemType, 0);
        return existing && runtime::AddInt64Wrapping(existing->Amount(), amount) > maximumStackAmount;
    };
    const auto updateDuration = [&]() {
        if (!added || duration < 0)
            return;
        constexpr const wchar_t* statement = L"{Call lin_UpdateItemDuration (?, ?)}";
        durationConnection.Prepare(statement);
        durationConnection.BindParameter(&itemId);
        durationConnection.BindParameter(&duration);
        // durationConnection.ExecutePrepared(statement)
        if (!durationConnection.ExecutePrepared(statement)) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[admin] Cannot store duration %d for added item %d (owner %d, warehouse %d, type %d, command %d). Item addition succeeded. The command still replies with success.",
            //     duration, itemId, ownerId, warehouseType, itemType, replyOpcode
            // )
            // L2_LOG_ERROR(Log,
            //     "[admin] Cannot store duration %d for added item %d (owner %d, warehouse %d, type %d, command %d). Item addition succeeded. The command still replies with success.",
            //     duration, itemId, ownerId, warehouseType, itemType, replyOpcode
            // )
            L2_LOG_ERROR(Log,
                "[admin] Cannot store duration %d for item %d (owner %d, warehouse %d, type %d, command %d). The addition helper reported success; the command still replies with success despite this save failure.",
                duration, itemId, ownerId, warehouseType, itemType, replyOpcode
            );
        }
    };

    if (warehouseType == 0 || warehouseType == 1) {
        CUserSP user = g_userDb.GetUser(ownerId, true, false, runtime::process::CurrentDatabaseId());
        if (!user) {
            result = 4;
        } else if (user->IsLoggedIn()) {
            result = 7;
        } else if (user->GetReloadFlag()) {
            result = 0x3c;
        } else {
            CWareHouseSP warehouse = user->GetWareHouse(warehouseType, true);
            if (!warehouse) {
                result = 5;
            } else if (exceedsStackLimit(warehouse)) {
                result = 3;
            } else {
                added = warehouse->AddItemByAdmin(
                    &itemId, itemType, amount, enchant, eroded, bless, ident, wished, warehouseType, optionKey,
                    intensiveItemType, baseAttribute, ensoulInfo, hLocked, useCount, appearanceType, 0, period, 0
                );
                updateDuration();
            }
        }
    } else if (warehouseType == 2 && replyOpcode != 0x8c) {
        CPledgeSP pledge = g_pledgeDb.GetPledge(ownerId, false, runtime::process::CurrentDatabaseId());
        if (!pledge) {
            result = 0x18;
        } else {
            CWareHouseSP warehouse = pledge->GetWareHouse();
            if (!warehouse) {
                result = 5;
            } else if (exceedsStackLimit(warehouse)) {
                result = 3;
            } else {
                added = warehouse->AddItemByAdmin(
                    &itemId, itemType, amount, enchant, eroded, bless, ident, wished, 2, optionKey, intensiveItemType,
                    baseAttribute, ensoulInfo, hLocked, useCount, 0, 0, 0, 0
                );
                updateDuration();
            }
        }
    } else if (warehouseType == 3 && replyOpcode != 0x8c) {
        CCastleSP castle = g_castleDb.GetCastle(ownerId);
        if (!castle) {
            result = 0x19;
        } else {
            CWareHouseSP warehouse = castle->GetWareHouse();
            if (!warehouse) {
                result = 5;
            } else if (exceedsStackLimit(warehouse)) {
                result = 3;
            } else {
                added = warehouse->AddItemByAdmin(
                    &itemId, itemType, amount, enchant, eroded, bless, ident, wished, 3, optionKey, intensiveItemType,
                    baseAttribute, ensoulInfo, hLocked, useCount, 0, 0, 0, 0
                );
                updateDuration();
            }
        }
    } else if (warehouseType == 5 && replyOpcode != 0x8c) {
        CPetSP pet = g_petDb.GetPet(ownerId, runtime::process::CurrentDatabaseId());
        if (!pet) {
            result = 0x1a;
        } else {
            CPetWarehouseSP warehouse = pet->GetWarehouse();
            if (!warehouse) {
                result = 5;
            } else if (exceedsStackLimit(warehouse)) {
                result = 3;
            } else {
                added = warehouse->AddItemByAdmin(
                    &itemId, itemType, amount, enchant, eroded, bless, ident, wished, 5, optionKey, intensiveItemType,
                    baseAttribute, ensoulInfo, hLocked, useCount, 0, 0, 0, 0
                );
                updateDuration();
            }
        }
    } else if (warehouseType > 1000 && warehouseType < 1005) {
        result = 5;
    }

    if (added) {
        socket->Send("cd", replyOpcode, 1);
    } else {
        // Log.Add(
        //     LOG_WARN,
        //     "[admin] Add-item command %d failed for owner %d, warehouse %d, type %d, amount %lld: reply code %d (%s).",
        //     replyOpcode, ownerId, warehouseType, itemType, static_cast<long long>(amount), result,
        //     result == 4 ? "character unavailable" : result == 7 ? "character is online" :
        //     result == 0x3c ? "character is reloading" : result == 5 ? "warehouse unavailable or unsupported" :
        //     result == 3 ? "stack limit exceeded" : result == 0x18 ? "clan unavailable" :
        //     result == 0x19 ? "castle unavailable" : result == 0x1a ? "pet unavailable" : "item addition did not complete"
        // )
        L2_LOG_WARN(Log,
            "[admin] Add-item command %d failed for owner %d, warehouse %d, type %d, amount %lld: reply code %d (%s).",
            replyOpcode, ownerId, warehouseType, itemType, static_cast<long long>(amount), result,
            result == 4 ? "character unavailable" : result == 7 ? "character is online" :
            result == 0x3c ? "character is reloading" : result == 5 ? "warehouse unavailable or unsupported" :
            result == 3 ? "stack limit exceeded" : result == 0x18 ? "clan unavailable" :
            result == 0x19 ? "castle unavailable" : result == 0x1a ? "pet unavailable" : "item addition did not complete"
        );
        socket->Send("cdd", replyOpcode, 0, result);
    }
    return false;
}

bool AddItem2Packet(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0044977c, AQ 55.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 55, 0, 0x1e);
        return false;
    }

    int ownerId = 0;
    int warehouse = 0;
    int itemType = 0;
    std::uint64_t amountBits = 0;
    int enchant = 0;
    int eroded = 0;
    int bless = 0;
    int ident = 0;
    int wished = 0;
    int variationOption1 = 0;
    int variationOption2 = 0;
    int intensiveItemType = 0;
    int duration = -1;
    short attackType = 0;
    short attackValue = 0;
    std::array<short, ATTRT_MAX> defendValues{};
    int appearanceType = 0;
    int period = 0;
    int ensoulSlotOne = 0;
    int ensoulSlotTwo = 0;
    int ensoulSlotThree = 0;
    short hLocked = 0;
    short useCount = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(
        packet, "dddQdddddddddhhhhhhhhdddddhhS", &ownerId, &warehouse, &itemType, &amountBits, &enchant, &eroded, &bless,
        &ident, &wished, &variationOption1, &variationOption2, &intensiveItemType, &duration, &attackType, &attackValue,
        &defendValues[0], &defendValues[1], &defendValues[2], &defendValues[3], &defendValues[4], &defendValues[5],
        &appearanceType, &period, &ensoulSlotOne, &ensoulSlotTwo, &ensoulSlotThree, &hLocked, &useCount, 0x200,
        auditTextUtf16.data()
    );

    CItem::EnsoulInfo ensoulInfo[3];
    if (ensoulSlotOne > 0) {
        ensoulInfo[1][1] = ensoulSlotOne;
        if (ensoulSlotTwo > 0) {
            ensoulInfo[1][2] = ensoulSlotTwo;
        }
    }
    if (ensoulSlotThree > 0) {
        ensoulInfo[2][1] = ensoulSlotThree;
    }
    const std::wstring auditText = Utf16ToWide(auditTextUtf16.data());
    return AddItemProc(
        socket, 55, ownerId, warehouse, itemType, runtime::Int64FromBits(amountBits), enchant, eroded, bless, ident, wished,
        variationOption1, variationOption2, intensiveItemType, duration, attackType, attackValue, defendValues.data(),
        appearanceType, period, ensoulInfo, hLocked, useCount, auditText.c_str()
    );
}

bool AddPremiumItemPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0043bc40, AQ 140. Its decode body is byte-for-byte
    // equivalent to AQ 55 apart from the reply opcode and guard identity.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 140, 0, 0x1e);
        return false;
    }

    int ownerId = 0;
    int warehouse = 0;
    int itemType = 0;
    std::uint64_t amountBits = 0;
    int enchant = 0;
    int eroded = 0;
    int bless = 0;
    int ident = 0;
    int wished = 0;
    int variationOption1 = 0;
    int variationOption2 = 0;
    int intensiveItemType = 0;
    int duration = -1;
    short attackType = 0;
    short attackValue = 0;
    std::array<short, ATTRT_MAX> defendValues{};
    int appearanceType = 0;
    int period = 0;
    int ensoulSlotOne = 0;
    int ensoulSlotTwo = 0;
    int ensoulSlotThree = 0;
    short hLocked = 0;
    short useCount = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(
        packet, "dddQdddddddddhhhhhhhhdddddhhS", &ownerId, &warehouse, &itemType, &amountBits, &enchant, &eroded, &bless,
        &ident, &wished, &variationOption1, &variationOption2, &intensiveItemType, &duration, &attackType, &attackValue,
        &defendValues[0], &defendValues[1], &defendValues[2], &defendValues[3], &defendValues[4], &defendValues[5],
        &appearanceType, &period, &ensoulSlotOne, &ensoulSlotTwo, &ensoulSlotThree, &hLocked, &useCount, 0x200,
        auditTextUtf16.data()
    );

    CItem::EnsoulInfo ensoulInfo[3];
    if (ensoulSlotOne > 0) {
        ensoulInfo[1][1] = ensoulSlotOne;
        if (ensoulSlotTwo > 0) {
            ensoulInfo[1][2] = ensoulSlotTwo;
        }
    }
    if (ensoulSlotThree > 0) {
        ensoulInfo[2][1] = ensoulSlotThree;
    }
    const std::wstring auditText = Utf16ToWide(auditTextUtf16.data());
    return AddItemProc(
        socket, 140, ownerId, warehouse, itemType, runtime::Int64FromBits(amountBits), enchant, eroded, bless, ident, wished,
        variationOption1, variationOption2, intensiveItemType, duration, attackType, attackValue, defendValues.data(),
        appearanceType, period, ensoulInfo, hLocked, useCount, auditText.c_str()
    );
}

bool AddItemsPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0041d778, AQ 66. This batch form accepts only a
    // CUser owner and reports the first failing item for the whole request.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 66, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    int itemCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "dSd", &characterId, 0x200, auditTextUtf16.data(), &itemCount);
    const std::wstring auditText = Utf16ToWide(auditTextUtf16.data());
    if (auditText.empty()) {
        // Log.Add(LOG_ERROR, "[adminsocket.cpp][5454] No Admin Id")
        // Log.Add(
        //     LOG_WARN,
        //     "[admin] Add-items request for character %d rejected: audit text is empty (requested items %d).",
        //     characterId, itemCount
        // )
        L2_LOG_WARN(Log,
            "[admin] Add-items request for character %d rejected: audit text is empty (requested items %d).",
            characterId, itemCount
        );
        socket->Send("cdd", 66, 0, 1);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[adminsocket.cpp][5449]Cannot find user, char id[%d]", characterId)
        // Log.Add(
        //     LOG_WARN,
        //     "[admin] Cannot add %d requested items to character %d: character unavailable. Returning failure.",
        //     itemCount, characterId
        // )
        L2_LOG_WARN(Log,
            "[admin] Cannot add %d requested items to character %d: character unavailable; returning failure.",
            itemCount, characterId
        );
        socket->Send("cdd", 66, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 66, 0, 7);
        return false;
    }
    if (user->GetReloadFlag()) {
        socket->Send("cdd", 66, 0, 0x3c);
        return false;
    }

    constexpr std::int64_t maximumStackAmount = 999999999999LL;
    for (int index = 0; index < itemCount; ++index) {
        int warehouseType = 0;
        int itemType = 0;
        std::uint64_t amountBits = 0;
        int enchant = 0;
        int eroded = 0;
        int bless = 0;
        int ident = 0;
        int wished = 0;
        int variationOption1 = 0;
        int variationOption2 = 0;
        int intensiveItemType = 0;
        short attackType = static_cast<short>(ATTRT_NONE);
        short attackValue = 0;
        std::array<short, ATTRT_MAX> defendValues{};
        int appearanceType = 0;
        int modifyPeriod = 0;
        int period = 0;
        short hLocked = 0;
        short useCount = 0;
        cursor = ::Disassemble(
            cursor, "ddQddddddddhhhhhhhhdddhh", &warehouseType, &itemType, &amountBits, &enchant, &eroded, &bless, &ident,
            &wished, &variationOption1, &variationOption2, &intensiveItemType, &attackType, &attackValue, &defendValues[0],
            &defendValues[1], &defendValues[2], &defendValues[3], &defendValues[4], &defendValues[5], &appearanceType,
            &modifyPeriod, &period, &hLocked, &useCount
        );

        CWareHouseSP warehouse = user->GetWareHouse(warehouseType, true);
        if (!warehouse) {
            socket->Send("cdd", 66, 0, 1);
            return false;
        }

        const std::int64_t amount = runtime::Int64FromBits(amountBits);
        CItemSP existing = warehouse->GetItemByType(itemType, 0);
        if (existing && runtime::AddInt64Wrapping(existing->Amount(), amount) > maximumStackAmount) {
            socket->Send("cdd", 66, 0, 3);
            return false;
        }

        int itemId = 0;
        ItemOptionKey optionKey{variationOption1, variationOption2};
        BaseAttribute baseAttribute(attackType, attackValue, defendValues.data());
        if (!warehouse->AddItemByAdmin(
                &itemId, itemType, amount, enchant, eroded, bless, ident, wished, warehouseType, optionKey,
                intensiveItemType, baseAttribute, hLocked, useCount, appearanceType, modifyPeriod, period, 0
            )) {
            socket->Send("cdd", 66, 0, 1);
            return false;
        }
    }

    socket->Send("cd", 66, 1);
    return false;
}

bool DelItemPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004157d4, AQ 13, codec dddQS. A logged-in
    // inventory mutation is delegated to L2Server. Every other reachable
    // case uses the original CUser/CWareHouse owners in CacheD.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 13, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::uint32_t warehouseType = 0;
    int itemId = 0;
    std::uint64_t amount = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dddQS", &characterId, &warehouseType, &itemId, &amount, 0x200, auditTextUtf16.data());
    if (warehouseType > 0x100U || amount > 1000000000ULL) {
        socket->Send("cdd", 13, 0, 3);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 13, 0, 4);
        return false;
    }

    bool deleted = false;
    CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (user->IsLoggedIn() && warehouseType == 0 && server != nullptr) {
        server->Send("cdddddQddddd", 0x24, user->GetId(), 2, 0, itemId, 0, static_cast<std::int64_t>(amount), 0, 0, 0, 0, 0);
        deleted = true;
    } else {
        if (!user->GetReloadFlag()) {
            if (user->GetItemWare(itemId) == 1001) {
                deleted = user->DelItem(itemId) > 0;
            } else {
                CWareHouseSP warehouse = user->GetWareHouse(static_cast<int>(warehouseType), true);
                if (warehouse) {
                    deleted = warehouse->DeleteItem(itemId, static_cast<std::int64_t>(amount));
                }
            }
        }
        if (!deleted) {
            socket->Send("cdd", 13, 0, user->GetReloadFlag() ? 0x3c : 1);
            return false;
        }
    }

    socket->Send("cd", 13, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x265);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[11] = runtime::DecimalUtf16(warehouseType);
    fields[12] = runtime::DecimalUtf16(itemId);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool ModItemPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00415f38, AQ 14, codec ddddQdddddhhS. The current
    // protocol supplies no variation or elemental values: both value objects
    // are constructed empty before CWareHouse::ModifyItem.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 14, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::uint32_t warehouseType = 0;
    int itemId = 0;
    std::uint32_t itemType = 0;
    std::uint64_t amount = 0;
    std::uint32_t enchant = 0;
    std::uint32_t eroded = 0;
    std::uint32_t bless = 0;
    std::uint32_t ident = 0;
    std::uint32_t wished = 0;
    std::uint16_t hLocked = 0;
    std::uint16_t useCount = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(
        packet, "ddddQdddddhhS", &characterId, &warehouseType, &itemId, &itemType, &amount, &enchant, &eroded, &bless,
        &ident, &wished, &hLocked, &useCount, 0x200, auditTextUtf16.data()
    );
    if (warehouseType > 0x100U || itemType > 0x10000U || amount > 1000000000ULL || enchant > 0x100U || eroded > 0x100U ||
        bless > 0x100U || ident > 0x100U || wished > 0x100U || hLocked > 1U || useCount > 0x100U) {
        socket->Send("cdd", 14, 0, 3);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 14, 0, 4);
        return false;
    }

    CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (user->IsLoggedIn() && warehouseType == 0 && server != nullptr) {
        server->Send(
            "cdddddQddddd", 0x24, user->GetId(), 3, 0, itemId, itemType, static_cast<std::int64_t>(amount), enchant, eroded,
            bless, ident, wished
        );
        socket->Send("cd", 14, 1);

        std::array<std::u16string, 26> fields{};
        fields[0] = runtime::DecimalUtf16(0x266);
        fields[1] = runtime::DecimalUtf16(0);
        fields[2] = runtime::DecimalUtf16(0);
        fields[3] = runtime::DecimalUtf16(user->GetId());
        fields[4] = runtime::DecimalUtf16(user->GetAccountID());
        fields[11] = runtime::DecimalUtf16(warehouseType);
        fields[12] = runtime::DecimalUtf16(itemId);
        fields[13] = runtime::DecimalUtf16(itemType);
        fields[14] = runtime::DecimalUtf16(static_cast<std::int64_t>(amount));
        fields[15] = runtime::DecimalUtf16(enchant);
        fields[16] = runtime::DecimalUtf16(eroded);
        fields[17] = runtime::DecimalUtf16(bless);
        fields[18] = runtime::DecimalUtf16(ident);
        fields[19] = runtime::DecimalUtf16(wished);
        fields[21] = auditTextUtf16.data();
        fields[22] = auditTextUtf16.data();
        fields[23] = AdminLogText(user->GetCharName());
        fields[24] = AdminLogText(user->GetAccountName());
        runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
        return false;
    }

    if (user->GetReloadFlag()) {
        socket->Send("cdd", 14, 0, 0x3c);
        return false;
    }

    CWareHouseSP warehouse = user->GetWareHouse(static_cast<int>(warehouseType), true);
    if (!warehouse) {
        socket->Send("cdd", 14, 0, 5);
        return false;
    }

    ItemOptionKey optionKey{};
    std::array<short, ATTRT_MAX> defendValues{};
    BaseAttribute baseAttribute(static_cast<short>(ATTRT_NONE), 0, defendValues.data());
    if (!warehouse->ModifyItem(
            itemId, itemType, static_cast<std::int64_t>(amount), static_cast<int>(enchant), static_cast<int>(eroded),
            static_cast<int>(bless), static_cast<int>(ident), static_cast<int>(wished), static_cast<int>(warehouseType),
            optionKey, 0, baseAttribute, static_cast<short>(hLocked), static_cast<short>(useCount)
        )) {
        socket->Send("cdd", 14, 0, 1);
        return false;
    }

    socket->Send("cd", 14, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x266);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[11] = runtime::DecimalUtf16(warehouseType);
    fields[12] = runtime::DecimalUtf16(itemId);
    fields[13] = runtime::DecimalUtf16(itemType);
    fields[14] = runtime::DecimalUtf16(static_cast<std::int64_t>(amount));
    fields[15] = runtime::DecimalUtf16(enchant);
    fields[16] = runtime::DecimalUtf16(eroded);
    fields[17] = runtime::DecimalUtf16(bless);
    fields[18] = runtime::DecimalUtf16(ident);
    fields[19] = runtime::DecimalUtf16(wished);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool ModCharPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00416a1c, AQ 15, codec dQQdddddS. AP is passed to
    // CUser but is absent from both the 0x25 server frame and
    // audit record 0x267.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 15, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::int64_t sp = 0;
    std::int64_t experience = 0;
    int alignment = 0;
    int pkCount = 0;
    std::uint32_t pkPardon = 0;
    std::uint32_t duelCount = 0;
    int abilityPoints = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(
        packet, "dQQdddddS", &characterId, &sp, &experience, &alignment, &pkCount, &pkPardon, &duelCount, &abilityPoints,
        0x200, auditTextUtf16.data()
    );

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 15, 0, 4);
        return false;
    }
    if (!user->ModCharExtern(sp, experience, alignment, pkCount, pkPardon, duelCount, abilityPoints)) {
        socket->Send("cdd", 15, 0, 1);
        return false;
    }

    if (user->IsLoggedIn()) {
        CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
        if (server != nullptr) {
            server->Send("cdQQdddd", 0x25, user->GetId(), sp, experience, alignment, pkCount, pkPardon, duelCount);
        }
    }

    socket->Send("cd", 15, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x267);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[11] = runtime::DecimalUtf16(sp);
    fields[12] = runtime::DecimalUtf16(experience);
    fields[13] = runtime::DecimalUtf16(alignment);
    fields[14] = runtime::DecimalUtf16(pkCount);
    fields[15] = runtime::DecimalUtf16(pkPardon);
    fields[16] = runtime::DecimalUtf16(duelCount);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool ModChar2Packet(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00416f28, AQ 16, codec dddddddS. Every property
    // comparison is unsigned, so negative wire values also select reason 3.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 16, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::uint32_t gender = 0;
    std::uint32_t race = 0;
    std::uint32_t characterClass = 0;
    std::uint32_t face = 0;
    std::uint32_t hairShape = 0;
    std::uint32_t hairColor = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(
        packet, "dddddddS", &characterId, &gender, &race, &characterClass, &face, &hairShape, &hairColor, 0x200,
        auditTextUtf16.data()
    );
    if (gender > 0x10000U || race > 0x10000U || characterClass > 0x10000U || face > 0x10000U || hairShape > 0x10000U ||
        hairColor > 0x10000U) {
        socket->Send("cdd", 16, 0, 3);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 16, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 16, 0, 7);
        return false;
    }

    if (!user->ModChar2Extern(
            static_cast<int>(gender), static_cast<int>(race), static_cast<int>(characterClass), static_cast<int>(face),
            static_cast<int>(hairShape), static_cast<int>(hairColor), user->GetJobGroup()
        )) {
        socket->Send("cdd", 16, 0, 1);
        return false;
    }

    socket->Send("cd", 16, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x268);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[11] = runtime::DecimalUtf16(gender);
    fields[12] = runtime::DecimalUtf16(race);
    fields[13] = runtime::DecimalUtf16(characterClass);
    fields[14] = runtime::DecimalUtf16(face);
    fields[15] = runtime::DecimalUtf16(hairShape);
    fields[16] = runtime::DecimalUtf16(hairColor);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool ModCharPledgePacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00417508, AQ 17, codec dSS. Despite its historical
    // name this is the nickname mutation and it is valid for a logged-in user.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 17, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::array<char16_t, 25> nicknameUtf16{};
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dSS", &characterId, 0x32, nicknameUtf16.data(), 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 17, 0, 4);
        return false;
    }

    std::wstring nickname = Utf16ToWide(nicknameUtf16.data());
    if (!user->ModCharPledgeExtern(nickname.data())) {
        socket->Send("cdd", 17, 0, 1);
        return false;
    }

    if (user->IsLoggedIn()) {
        CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
        if (server != nullptr) {
            server->Send("cdS", 0x27, user->GetId(), nicknameUtf16.data());
        }
    }

    socket->Send("cd", 17, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x269);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[8] = nicknameUtf16.data();
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool PunishCharPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004337c4, AQ 18, codec dddS. Punishment IDs 2..6
    // share one persistence path. The history action is selected by both the
    // ID and whether the supplied remaining value clears the punishment.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 18, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int punishmentId = 0;
    int punishmentValue = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dddS", &characterId, &punishmentId, &punishmentValue, 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 18, 0, 4);
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    if (punishmentId < 2 || punishmentId > 6) {
        socket->Send("cdd", 18, 0, 1);
        return false;
    }

    int userId = static_cast<int>(user->GetId());
    int punishmentOn = 1;
    constexpr const wchar_t* setPunishment = L"{Call lin_SetPunish (?, ?, ?, ?)}";
    connection.Prepare(setPunishment);
    connection.BindParameter(&userId);
    connection.BindParameter(&punishmentId);
    connection.BindParameter(&punishmentOn);
    connection.BindParameter(&punishmentValue);
    // connection.ExecutePrepared(setPunishment)
    if (!connection.ExecutePrepared(setPunishment)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot store punishment %d with value %d for character %d. History and notification handling continue, and this command still replies with success.",
        //     punishmentId, punishmentValue, userId
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot store punishment %d with value %d for character %d; history and notification handling continue, and this command still replies with success.",
            punishmentId, punishmentValue, userId
        );
    }
    connection.ResetParam();

    int historyAction = 0;
    if (punishmentValue == 0) {
        switch (punishmentId) {
        case 2:
            historyAction = 0x16;
            break;
        case 3:
            historyAction = 0x1c;
            break;
        case 4:
            historyAction = 0x1d;
            break;
        case 5:
            historyAction = 0x7a;
            break;
        case 6:
            historyAction = 0x84;
            break;
        default:
            break;
        }
    } else {
        switch (punishmentId) {
        case 2:
            historyAction = 0x0c;
            break;
        case 3:
            historyAction = 0x1b;
            break;
        case 4:
            historyAction = 0x13;
            break;
        case 5:
            historyAction = 0x79;
            break;
        case 6:
            historyAction = 0x83;
            break;
        default:
            break;
        }
    }

    constexpr const wchar_t* addHistory = L"{Call lin_AddPunishmentHistory (?, ?, ?, ?)}";
    connection.Prepare(addHistory);
    connection.BindParameter(user->GetCharName());
    connection.BindParameter(&userId);
    connection.BindParameter(&historyAction);
    connection.BindParameter(user->GetAccountName());
    // connection.ExecutePrepared(addHistory)
    if (!connection.ExecutePrepared(addHistory)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot record punishment history for character %d (punishment %d, value %d, history action %d). This command still replies with success.",
        //     userId, punishmentId, punishmentValue, historyAction
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot record punishment history for character %d (punishment %d, value %d, history action %d); this command still replies with success.",
            userId, punishmentId, punishmentValue, historyAction
        );
    }

    if (punishmentValue == 0 && punishmentId == 2 && GetCacheDConfiguration().blockPenaltyCount > 0) {
        connection.ResetParam();
        int pointType = 9;
        constexpr const wchar_t* deletePoint = L"{Call lin_DeleteUserPoint (?, ?)}";
        connection.Prepare(deletePoint);
        connection.BindParameter(&userId);
        connection.BindParameter(&pointType);
        // connection.ExecutePrepared(deletePoint)
        if (!connection.ExecutePrepared(deletePoint)) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[admin] Cannot clear penalty points of type %d for character %d after the punishment-clear attempt. This command still replies with success.",
            //     pointType, userId
            // )
            L2_LOG_ERROR(Log,
                "[admin] Cannot clear penalty points of type %d for character %d after the punishment-clear attempt; this command still replies with success.",
                pointType, userId
            );
        }
    }

    if (user->IsLoggedIn()) {
        CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
        if (server != nullptr) {
            server->Send("cddddd", 0x2b, userId, 1, punishmentId, 1, punishmentValue);
        } else {
            // Log.Add(
            //     LOG_WARN,
            //     "[admin] Cannot notify L2Server of punishment %d with value %d for online character %d: no active connection. Database operations were already attempted. This command still replies with success.",
            //     punishmentId, punishmentValue, userId
            // )
            L2_LOG_WARN(Log,
                "[admin] Cannot notify L2Server of punishment %d with value %d for online character %d: no active connection. Database operations were already attempted; this command still replies with success.",
                punishmentId, punishmentValue, userId
            );
        }
    }

    socket->Send("cd", 18, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x26a);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(userId);
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[11] = runtime::DecimalUtf16(punishmentId);
    fields[12] = runtime::DecimalUtf16(punishmentValue);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool SetBuilderAccountPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0043443c, AQ 19, codec SdS. The unsigned range
    // check rejects negative wire values as well as values above 255.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 19, 0, 0x1e);
        return false;
    }

    std::array<char16_t, 256> accountNameUtf16{};
    int builderLevel = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "SdS", 0x200, accountNameUtf16.data(), &builderLevel, 0x200, auditTextUtf16.data());
    if (static_cast<std::uint32_t>(builderLevel) > 0xffU) {
        socket->Send("cdd", 19, 0, 3);
        return false;
    }

    const std::u16string accountName(accountNameUtf16.data());
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetBuilderAccount (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(accountName.c_str());
    connection.BindParameter(&builderLevel);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot set builder level %d for account %s: database operation failed. Returning failure.",
        //     builderLevel, WideToUtf8(accountName).c_str()
        // )
        // L2_LOG_ERROR(Log,
        //     "[admin] Cannot set builder level %d for account %s: database operation failed. Returning failure.",
        //     builderLevel, WideToUtf8(accountName).c_str()
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot set builder level %d for account %s: database operation failed; returning failure.",
            builderLevel, Utf16ToUtf8(accountName).c_str()
        );
        socket->Send("cdd", 19, 0, 6);
        return false;
    }

    socket->Send("cd", 19, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x26b);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[8] = accountNameUtf16.data();
    fields[11] = runtime::DecimalUtf16(builderLevel);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool DisableCharacterPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004346dc, AQ 20, codec ddS. Option one moves the
    // offline character to disabled account ID -2 and reloads the owning
    // account before withdrawing the resident CUser from its indexes.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 20, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int option = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddS", &characterId, &option, 0x200, auditTextUtf16.data());
    if (static_cast<std::uint32_t>(option) > 0xffU) {
        socket->Send("cdd", 20, 0, 3);
        return false;
    }
    if (option != 1) {
        socket->Send("cdd", 20, 0, 1);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 20, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 20, 0, 7);
        return false;
    }

    const int accountId = user->GetAccountID();
    CAccountSP account = CAccount::Load(static_cast<unsigned int>(accountId));
    if (!account) {
        g_userDb.DeleteChar(user.get(), false);
        socket->Send("cdd", 20, 0, 6);
        return false;
    }

    int userId = static_cast<int>(user->GetId());
    int historyAction = 0x0b;
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* addHistory = L"{Call lin_AddPunishmentHistory (?, ?, ?, ?)}";
    connection.Prepare(addHistory);
    connection.BindParameter(user->GetCharName());
    connection.BindParameter(&userId);
    connection.BindParameter(&historyAction);
    connection.BindParameter(user->GetAccountName());
    // connection.ExecutePrepared(addHistory)
    if (!connection.ExecutePrepared(addHistory)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot record disable history for character %d in account %d. The disable attempt continues.",
        //     userId, accountId
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot record disable history for character %d in account %d; the disable attempt continues.",
            userId, accountId
        );
    }
    connection.ResetParam();

    bool disableExecuted;
    {
        L2_WRITE_GUARD(*account.operator->());
        constexpr const wchar_t* disableCharacter = L"{Call lin_DisableChar (?)}";
        connection.Prepare(disableCharacter);
        connection.BindParameter(user->GetCharName());
        // connection.ExecutePrepared(disableCharacter)
        disableExecuted = connection.ExecutePrepared(disableCharacter);
        CAccount::Reload(static_cast<unsigned int>(accountId));
    }
    if (!disableExecuted) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot disable character %d in account %d: database operation failed. The account was reloaded. Cached character removal and a success reply still follow.",
        //     userId, accountId
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot disable character %d in account %d: database operation failed. The account was reloaded; cached character removal and a success reply still follow.",
            userId, accountId
        );
    }

    g_userDb.DeleteChar(user.get(), false);
    socket->Send("cd", 20, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x26c);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(userId);
    fields[4] = runtime::DecimalUtf16(accountId);
    fields[11] = runtime::DecimalUtf16(option);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool EnableCharacterPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00434e44, AQ 21, codec ddS. The first integer is
    // the disabled character ID. The second selects the account to restore.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 21, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int accountId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddS", &characterId, &accountId, 0x200, auditTextUtf16.data());

    CAccountSP account = CAccount::Load(static_cast<unsigned int>(accountId));
    if (!account) {
        socket->Send("cdd", 21, 0, 6);
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    bool enableExecuted;
    {
        L2_WRITE_GUARD(*account.operator->());
        constexpr const wchar_t* enableCharacter = L"{Call lin_EnableChar2 (?, ?)}";
        connection.Prepare(enableCharacter);
        connection.BindParameter(&characterId);
        connection.BindParameter(&accountId);
        // connection.ExecutePrepared(enableCharacter)
        enableExecuted = connection.ExecutePrepared(enableCharacter);
        CAccount::Reload(static_cast<unsigned int>(accountId));
    }
    if (!enableExecuted) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot enable character %d in account %d: database operation failed. The account was reloaded. The command continues and its reply depends on the character lookup.",
        //     characterId, accountId
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot enable character %d in account %d: database operation failed. The account was reloaded; the command continues and its reply depends on the character lookup.",
            characterId, accountId
        );
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 21, 0, 1);
        return false;
    }

    connection.ResetParam();
    int userId = static_cast<int>(user->GetId());
    int historyAction = 0x15;
    constexpr const wchar_t* addHistory = L"{Call lin_AddPunishmentHistory (?, ?, ?, ?)}";
    connection.Prepare(addHistory);
    connection.BindParameter(user->GetCharName());
    connection.BindParameter(&userId);
    connection.BindParameter(&historyAction);
    connection.BindParameter(user->GetAccountName());
    // connection.ExecutePrepared(addHistory)
    if (!connection.ExecutePrepared(addHistory)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot record enable history for character %d in account %d. This command still replies with success.",
        //     userId, accountId
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot record enable history for character %d in account %d; this command still replies with success.",
            userId, accountId
        );
    }

    socket->Send("cd", 21, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x26d);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(userId);
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[11] = runtime::DecimalUtf16(accountId);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool GetCharactersPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0042fd84, AQ 22, codec dS. Unlike the neighboring
    // mutation handlers, this read path does not check the admin-rejection
    // flag. Its 0x5000-byte reply contains one exact 21-field row per user.
    L2_GUARD_FUNCTION();

    int accountId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dS", &accountId, 0x200, auditTextUtf16.data());

    CAccountSP account = CAccount::Load(static_cast<unsigned int>(accountId));
    if (!account || account->GetCharCount() == 0) {
        socket->Send("cdd", 22, 0, 0);
        return false;
    }

    struct CharacterRowAssembler {
        bool Do(CUser* user) {
            if (!user)
                return true;

            user->LoadLastLogout();
            const SYSTEMTIME& logout = user->GetLastLogoutTime();
            std::array<char, 32> logoutText{};
            std::snprintf(
                logoutText.data(), logoutText.size(), "%04d-%02d-%02d %02d:%02d:%02d", static_cast<int>(logout.wYear),
                static_cast<int>(logout.wMonth), static_cast<int>(logout.wDay), static_cast<int>(logout.wHour),
                static_cast<int>(logout.wMinute), static_cast<int>(logout.wSecond)
            );
            const std::u16string logoutTextUtf16 = Utf8ToUtf16(logoutText.data());

            CPledgeSP pledge =
                g_pledgeDb.GetPledge(static_cast<int>(user->GetPledgeID()), false, runtime::process::CurrentDatabaseId());
            const char16_t* pledgeName = pledge ? pledge->GetName() : u"";
            const int pledgeRuler = pledge && pledge->rullerId() == user->GetId() ? 1 : 0;

            user->LoadUserNobless();
            int status = 0;
            if (user->GetStatus() == 2) {
                status = 2;
            } else if (user->GetStatus() == 1 && runtime::crt::Time32Now() < user->GetBanEnd()) {
                status = 1;
            }
            if (user->GetServiceFlag() > 0) {
                status = 3;
            }

            const std::u16string characterName = AdminLogText(user->GetCharName());
            const std::u16string accountName = AdminLogText(user->GetAccountName());
            const std::array<int, 4>& subjobClasses = user->GetSubjobClasses();
            const int assembled = ::Assemble(
                rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dSdSdddddddddSdSddddd",
                static_cast<int>(user->GetId()), characterName.c_str(), user->GetAccountID(), accountName.c_str(),
                user->GetLevel(), user->GetClass(), user->GetGender(), subjobClasses[0], subjobClasses[1], subjobClasses[2],
                subjobClasses[3], pledgeRuler, static_cast<int>(user->GetPledgeID()), pledgeName, user->IsLoggedIn() ? 1 : 0,
                logoutTextUtf16.c_str(), user->GetNoblessType(), user->GetHeroType(), status,
                user->GetDeletedElapsedSecond(), user->GetRace()
            );
            rowBytes += assembled;
            ++rowCount;
            return true;
        }

        std::array<char, 0x5000> rows{};
        int rowBytes = 0;
        int rowCount = 0;
    } assembler;

    {
        L2_READ_GUARD(*account.operator->());
        account->GetChars().FOR_ALL(&assembler, L"adminsocket.cpp", 0x742);
    }

    socket->Send("cddb", 22, assembler.rowCount, assembler.rowBytes, assembler.rows.data());

    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x26e);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[11] = runtime::DecimalUtf16(accountId);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[24] = AdminLogText(account->GetName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool GetCharacters2Packet(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00430558, AQ 56, codec dS. This historical list
    // shape adds four subjob-class slots and exposes a pledge name only for
    // the pledge ruler. Its fixed-buffer row count is uncapped.
    L2_GUARD_FUNCTION();

    int accountId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dS", &accountId, 0x200, auditTextUtf16.data());

    CAccountSP account = CAccount::Load(static_cast<unsigned int>(accountId));
    if (!account || account->GetCharCount() == 0) {
        socket->Send("cd", 56, 0);
    } else {
        struct Character2RowAssembler {
            bool Do(CUser* user) {
                if (!user)
                    return true;

                CPledgeSP pledge =
                    g_pledgeDb.GetPledge(static_cast<int>(user->GetPledgeID()), false, runtime::process::CurrentDatabaseId());
                const char16_t* pledgeName = pledge && pledge->rullerId() == user->GetId() ? pledge->GetName() : u"";
                const std::u16string characterName = AdminLogText(user->GetCharName());
                const std::u16string accountName = AdminLogText(user->GetAccountName());
                const std::array<int, 4>& subjobClasses = user->GetSubjobClasses();
                rowBytes += ::Assemble(
                    rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dSdSdddSdddd",
                    static_cast<int>(user->GetId()), characterName.c_str(), user->GetAccountID(), accountName.c_str(),
                    user->GetLevel(), user->GetClass(), static_cast<int>(user->GetPledgeID()), pledgeName, subjobClasses[0],
                    subjobClasses[1], subjobClasses[2], subjobClasses[3]
                );
                ++rowCount;
                return true;
            }

            std::array<char, 0x5000> rows{};
            int rowBytes = 0;
            int rowCount = 0;
        } assembler;

        {
            L2_READ_GUARD(*account.operator->());
            account->GetChars().FOR_ALL(&assembler, L"adminsocket.cpp", 0x1439);
        }
        socket->Send("cdb", 56, assembler.rowCount, assembler.rowBytes, assembler.rows.data());
    }

    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x26e);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[11] = runtime::DecimalUtf16(accountId);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[24] = account ? AdminLogText(account->GetName()) : std::u16string{};
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool GetCharacters3Packet(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00430bb0, AQ 68, codec dS. This list shape adds
    // the resident login flag to AQ 56's core identity fields and keeps the
    // ruler-only pledge-name rule.
    L2_GUARD_FUNCTION();

    int accountId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dS", &accountId, 0x200, auditTextUtf16.data());

    CAccountSP account = CAccount::Load(static_cast<unsigned int>(accountId));
    if (!account || account->GetCharCount() == 0) {
        socket->Send("cd", 68, 0);
    } else {
        struct Character3RowAssembler {
            bool Do(CUser* user) {
                if (!user)
                    return true;

                CPledgeSP pledge =
                    g_pledgeDb.GetPledge(static_cast<int>(user->GetPledgeID()), false, runtime::process::CurrentDatabaseId());
                const char16_t* pledgeName = pledge && pledge->rullerId() == user->GetId() ? pledge->GetName() : u"";
                const std::u16string characterName = AdminLogText(user->GetCharName());
                const std::u16string accountName = AdminLogText(user->GetAccountName());
                rowBytes += ::Assemble(
                    rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dSdSdddSd",
                    static_cast<int>(user->GetId()), characterName.c_str(), user->GetAccountID(), accountName.c_str(),
                    user->GetLevel(), user->GetClass(), static_cast<int>(user->GetPledgeID()), pledgeName,
                    user->IsLoggedIn() ? 1 : 0
                );
                ++rowCount;
                return true;
            }

            std::array<char, 0x5000> rows{};
            int rowBytes = 0;
            int rowCount = 0;
        } assembler;

        {
            L2_READ_GUARD(*account.operator->());
            account->GetChars().FOR_ALL(&assembler, L"adminsocket.cpp", 0x15ae);
        }
        socket->Send("cdb", 68, assembler.rowCount, assembler.rowBytes, assembler.rows.data());
    }

    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x26e);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[11] = runtime::DecimalUtf16(accountId);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[24] = account ? AdminLogText(account->GetName()) : std::u16string{};
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool GetCharacters4Packet(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0043f12c, AQ 105, codec dS. Each row performs the
    // shipping LastLogout and six-column Nobless query before assembling its
    // 12-field historical response. No resident Nobless state is updated.
    L2_GUARD_FUNCTION();

    int accountId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dS", &accountId, 0x200, auditTextUtf16.data());

    CAccountSP account = CAccount::Load(static_cast<unsigned int>(accountId));
    if (!account || account->GetCharCount() == 0) {
        socket->Send("cd", 105, 0);
    } else {
        struct Character4RowAssembler {
            bool Do(CUser* user) {
                if (!user)
                    return true;

                user->LoadLastLogout();
                const SYSTEMTIME& logout = user->GetLastLogoutTime();
                std::array<char, 64> logoutText{};
                std::snprintf(
                    logoutText.data(), logoutText.size(), "%04d-%02d-%02d %02d:%02d:%02d", static_cast<int>(logout.wYear),
                    static_cast<int>(logout.wMonth), static_cast<int>(logout.wDay), static_cast<int>(logout.wHour),
                    static_cast<int>(logout.wMinute), static_cast<int>(logout.wSecond)
                );
                const std::u16string logoutTextUtf16 = Utf8ToUtf16(logoutText.data());

                int noblessType = 0;
                int heroType = 0;
                int ignoredWinCount = 0;
                int ignoredPreviousPoint = 0;
                int ignoredOlympiadPoint = 0;
                int ignoredMatchCount = 0;
                DBConn connection(runtime::process::CurrentDatabaseId());
                connection.Bind(&noblessType);
                connection.Bind(&heroType);
                connection.Bind(&ignoredWinCount);
                connection.Bind(&ignoredPreviousPoint);
                connection.Bind(&ignoredOlympiadPoint);
                connection.Bind(&ignoredMatchCount);
                int characterId = static_cast<int>(user->GetId());
                constexpr const wchar_t* statement = L"{Call lin_GetNoblessById (?)}";
                connection.Prepare(statement);
                connection.BindParameter(&characterId);
                // connection.ExecutePrepared(statement)
                const bool noblessQueryExecuted = connection.ExecutePrepared(statement);
                connection.Fetch();
                if (!noblessQueryExecuted) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "[admin] Cannot load nobless/hero data for character %d in the account character list: database operation failed. The result fetch was still attempted. The reply uses nobless %d and hero %d.",
                    //     characterId, noblessType, heroType
                    // )
                    L2_LOG_ERROR(Log,
                        "[admin] Cannot load nobless/hero data for character %d in the account character list: database operation failed. The result fetch was still attempted; the reply uses nobless %d and hero %d.",
                        characterId, noblessType, heroType
                    );
                }

                CPledgeSP pledge =
                    g_pledgeDb.GetPledge(static_cast<int>(user->GetPledgeID()), false, runtime::process::CurrentDatabaseId());
                const char16_t* pledgeName = pledge ? pledge->GetName() : u"";
                const std::u16string characterName = AdminLogText(user->GetCharName());
                const std::u16string accountName = AdminLogText(user->GetAccountName());
                rowBytes += ::Assemble(
                    rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dSdSdddSdSdd", characterId,
                    characterName.c_str(), user->GetAccountID(), accountName.c_str(), user->GetLevel(), user->GetClass(),
                    static_cast<int>(user->GetPledgeID()), pledgeName, user->IsLoggedIn() ? 1 : 0, logoutTextUtf16.c_str(),
                    noblessType, heroType
                );
                ++rowCount;
                return true;
            }

            std::array<char, 0x5000> rows{};
            int rowBytes = 0;
            int rowCount = 0;
        } assembler;

        {
            L2_READ_GUARD(*account.operator->());
            account->GetChars().FOR_ALL(&assembler, L"adminsocket.cpp", 0x1ffd);
        }
        socket->Send("cdb", 105, assembler.rowCount, assembler.rowBytes, assembler.rows.data());
    }

    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x26e);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[11] = runtime::DecimalUtf16(accountId);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[24] = account ? AdminLogText(account->GetName()) : std::u16string{};
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool GetCharactersExceptBanPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00431204, AQ 124, codec dS. The four-field rows
    // include only characters outside permanent, active timed and added-
    // service bans. This path emits no administrative audit.
    L2_GUARD_FUNCTION();

    int accountId = 0;
    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(packet, "dS", &accountId, 0x200, ignoredAuditTextUtf16.data());

    CAccountSP account = CAccount::Load(static_cast<unsigned int>(accountId));
    if (!account || account->GetCharCount() == 0) {
        socket->Send("cd", 124, 0);
        return false;
    }

    struct UnbannedCharacterRowAssembler {
        bool Do(CUser* user) {
            if (!user)
                return true;

            const int status = user->GetStatus();
            const bool activeTimedBan = status == 1 && user->GetBanEnd() > runtime::crt::Time32Now();
            if (status == 2 || activeTimedBan || user->GetServiceFlag() != 0) {
                return true;
            }

            const std::u16string characterName = AdminLogText(user->GetCharName());
            rowBytes += ::Assemble(
                rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dSdd", static_cast<int>(user->GetId()),
                characterName.c_str(), user->GetDeletedElapsedSecond(), user->GetGender()
            );
            ++rowCount;
            return true;
        }

        std::array<char, 0x5000> rows{};
        int rowBytes = 0;
        int rowCount = 0;
    } assembler;

    {
        L2_READ_GUARD(*account.operator->());
        account->GetChars().FOR_ALL(&assembler, L"adminsocket.cpp", 0x2782);
    }
    socket->Send("cdb", 124, assembler.rowCount, assembler.rowBytes, assembler.rows.data());
    return false;
}

bool GetFriendPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00433100, AQ 129, codec dS. The returned S field
    // is the friend's account name at CUser+0x230, not the character name.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(packet, "dS", &characterId, 0x200, ignoredAuditTextUtf16.data());
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cd", 129, 0);
        return false;
    }

    FriendIdSet friendIds;
    g_friendsDb.GetFriendIdList(user->GetId(), friendIds);
    CPacketHelperEx<20480> rows;
    for (const unsigned int friendCharacterId : friendIds) {
        CUserSP friendUser =
            g_userDb.GetUser(static_cast<int>(friendCharacterId), true, false, runtime::process::CurrentDatabaseId());
        if (!friendUser)
            continue;

        const std::u16string accountName = AdminLogText(friendUser->GetAccountName());
        rows.Assemble(
            "dSddd", static_cast<int>(friendUser->GetId()), accountName.c_str(), friendUser->GetLevel(),
            friendUser->GetClass(), friendUser->IsLoggedIn() ? 1 : 0
        );
    }

    socket->Send("cdb", 129, rows.GetAssembledCount(), rows.GetLength(), rows.GetPacket());
    return false;
}

bool GetPetListPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0043d9d8, AQ 132, codec dS. SQL supplies the pet
    // collar's item type/enchant/eroded fields while CPet remains the owner
    // of the pet ID, nickname and NPC class ID.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(packet, "dS", &characterId, 0x200, ignoredAuditTextUtf16.data());
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cd", 132, 0);
        return false;
    }

    int petId = 0;
    int itemType = 0;
    int enchant = 0;
    int eroded = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&petId);
    connection.Bind(&itemType);
    connection.Bind(&enchant);
    connection.Bind(&eroded);
    if (characterId == 0) {
        socket->Send("cd", 132, 0);
        return false;
    }

    constexpr const wchar_t* statement = L"{Call lin_LoadPetList (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot load the pet list for character %d: database operation failed. Returning a zero-count reply.",
        //     characterId
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot load the pet list for character %d: database operation failed; returning a zero-count reply.",
            characterId
        );
        socket->Send("cd", 132, 0);
        return false;
    }

    std::array<char, 0x5000> rows{};
    int rowBytes = 0;
    int rowCount = 0;
    bool rowAssemblyFailureReported = false;
    while (connection.Fetch()) {
        CPetSP pet = g_petDb.GetPet(petId, runtime::process::CurrentDatabaseId());
        if (!pet)
            continue;

        // rowBytes += ::Assemble(
        //     rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dSdddd", static_cast<int>(pet->m_id),
        //     pet->m_nickName.data(), static_cast<int>(pet->m_npcClassId), itemType, enchant, eroded
        // )
        const int assembledRowBytes = ::Assemble(
            rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dSdddd", static_cast<int>(pet->m_id),
            pet->m_nickName.data(), static_cast<int>(pet->m_npcClassId), itemType, enchant, eroded
        );
        rowBytes += assembledRowBytes;
        if (assembledRowBytes <= 0 && !rowAssemblyFailureReported) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[admin] Cannot assemble pet list response entry %lld for character %d, pet %d (assembler result %d, accumulated %d bytes in the 20480-byte row buffer). Processing continues and the advertised count still includes failed entries.",
            //     static_cast<long long>(rowCount) + 1, characterId, petId, assembledRowBytes, rowBytes
            // )
            L2_LOG_ERROR(Log,
                "[admin] Cannot assemble pet list response entry %lld for character %d, pet %d (assembler result %d, accumulated %d bytes in the 20480-byte row buffer). Processing continues and the advertised count still includes failed entries.",
                static_cast<long long>(rowCount) + 1, characterId, petId, assembledRowBytes, rowBytes
            );
            rowAssemblyFailureReported = true;
        }
        ++rowCount;
    }

    socket->Send("cdb", 132, rowCount, rowBytes, rows.data());
    return false;
}

bool DelMonRaceTicketPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0041cae0, AQ 58, codec dddS. The third integer
    // and audit text are decoded but unused. Only a fully offline,
    // non-reloading CUser reaches the warehouse deletion owner.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 58, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int ticketItemId = 0;
    int ignoredValue = 0;
    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(packet, "dddS", &characterId, &ticketItemId, &ignoredValue, 0x200, ignoredAuditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 58, 0, 4);
        return false;
    }

    if (!user->IsLoggedIn() && !user->GetReloadFlag()) {
        CWareHouseSP inventory = user->GetWareHouse(0, true);
        inventory->DelMonRaceTicketWithoutReward(static_cast<unsigned int>(ticketItemId));
        socket->Send("cd", 58, 1);
        return false;
    }

    if (!user->GetReloadFlag()) {
        socket->Send("cdd", 58, 0, 7);
    } else {
        socket->Send("cdd", 58, 0, 0x3c);
    }
    return false;
}

bool ModChar3Packet(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004189dc, AQ 29, codec dQQdddddS. AP reaches the
    // delta owner but is omitted from current audit record 0x272.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 29, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::int64_t spDelta = 0;
    std::int64_t experienceDelta = 0;
    int alignmentDelta = 0;
    int pkCountDelta = 0;
    int pkPardonDelta = 0;
    int duelCountDelta = 0;
    int abilityPointDelta = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(
        packet, "dQQdddddS", &characterId, &spDelta, &experienceDelta, &alignmentDelta, &pkCountDelta, &pkPardonDelta,
        &duelCountDelta, &abilityPointDelta, 0x200, auditTextUtf16.data()
    );

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 29, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 29, 0, 7);
        return false;
    }

    if (!user->ModChar3Extern(
            spDelta, experienceDelta, alignmentDelta, pkCountDelta, pkPardonDelta, duelCountDelta, abilityPointDelta
        )) {
        socket->Send("cdd", 29, 0, 1);
        return false;
    }

    socket->Send("cd", 29, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x272);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[11] = runtime::DecimalUtf16(spDelta);
    fields[12] = runtime::DecimalUtf16(experienceDelta);
    fields[13] = runtime::DecimalUtf16(alignmentDelta);
    fields[14] = runtime::DecimalUtf16(pkCountDelta);
    fields[15] = runtime::DecimalUtf16(pkPardonDelta);
    fields[16] = runtime::DecimalUtf16(duelCountDelta);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool SetBookMarkPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00435780, AQ 23. This is the original named
    // builder bookmark path. It is distinct from AQ 144 user slots.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 23, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::array<char16_t, 256> bookmarkNameUtf16{};
    int worldId = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(
        packet, "dSddddS", &characterId, 0x200, bookmarkNameUtf16.data(), &worldId, &x, &y, &z, 0x200, auditTextUtf16.data()
    );

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 23, 0, 4);
        return false;
    }

    const std::u16string bookmarkName(bookmarkNameUtf16.data());
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_AddBookMark (?, ?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(bookmarkName.c_str());
    connection.BindParameter(&worldId);
    connection.BindParameter(&x);
    connection.BindParameter(&y);
    connection.BindParameter(&z);
    // connection.ExecutePrepared(statement)
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot store bookmark %s for character %d in world %d at (%d, %d, %d): database operation failed. This command still replies with success.",
        //     WideToUtf8(bookmarkName).c_str(), characterId, worldId, x, y, z
        // )
        // L2_LOG_ERROR(Log,
        //     "[admin] Cannot store bookmark %s for character %d in world %d at (%d, %d, %d): database operation failed. This command still replies with success.",
        //     WideToUtf8(bookmarkName).c_str(), characterId, worldId, x, y, z
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot store bookmark %s for character %d in world %d at (%d, %d, %d): database operation failed; this command still replies with success.",
            Utf16ToUtf8(bookmarkName).c_str(), characterId, worldId, x, y, z
        );
    }

    socket->Send("cd", 23, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x26f);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[8] = AdminLogText(user->GetCharName());
    fields[9] = bookmarkNameUtf16.data();
    fields[11] = runtime::DecimalUtf16(x);
    fields[12] = runtime::DecimalUtf16(y);
    fields[13] = runtime::DecimalUtf16(z);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool DelBookMarkPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004361d0, AQ 24. SQL execution is intentionally
    // not used to choose the reply in the shipping handler.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 24, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::array<char16_t, 256> bookmarkNameUtf16{};
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dSS", &characterId, 0x200, bookmarkNameUtf16.data(), 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 24, 0, 4);
        return false;
    }

    const std::u16string bookmarkName(bookmarkNameUtf16.data());
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DelBookMark (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(bookmarkName.c_str());
    // connection.ExecutePrepared(statement)
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot delete bookmark %s for character %d: database operation failed. This command still replies with success.",
        //     WideToUtf8(bookmarkName).c_str(), characterId
        // )
        // L2_LOG_ERROR(Log,
        //     "[admin] Cannot delete bookmark %s for character %d: database operation failed. This command still replies with success.",
        //     WideToUtf8(bookmarkName).c_str(), characterId
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot delete bookmark %s for character %d: database operation failed; this command still replies with success.",
            Utf16ToUtf8(bookmarkName).c_str(), characterId
        );
    }

    socket->Send("cd", 24, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x270);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[8] = AdminLogText(user->GetCharName());
    fields[9] = bookmarkNameUtf16.data();
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool DelPledgeCrestPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00417978, AQ 25, codec dS. With an active
    // L2Server the mutation is delegated through opcode 0x4a. Otherwise the
    // two local crest owners run in their target order.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 25, 0, 0x1e);
        return false;
    }

    int pledgeId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dS", &pledgeId, 0x200, auditTextUtf16.data());

    CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        socket->Send("cdd", 25, 0, 1);
        return false;
    }

    bool deleted = false;
    if (server != nullptr) {
        server->Send("cdd", 0x4a, static_cast<int>(pledge->GetId()), 1);
        deleted = true;
    } else if (g_pledgeCrestDb.DeleteCrest(pledge->crestId())) {
        deleted = pledge->DeleteCrest();
    }

    if (!deleted) {
        socket->Send("cdd", 25, 0, 1);
        return false;
    }

    socket->Send("cd", 25, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x2a0);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(pledge->rullerId());
    fields[4] = runtime::DecimalUtf16(pledge->GetId());
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(pledge->rullerName());
    fields[24] = AdminLogText(pledge->GetName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool ModPledgeNamePacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00417edc, AQ 26, codec dSS. The trailing audit
    // carrier is decoded but is not consumed by the shipping body.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 26, 0, 0x1e);
        return false;
    }

    int pledgeId = 0;
    std::array<char16_t, 64> newNameUtf16{};
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dSS", &pledgeId, 0x80, newNameUtf16.data(), 0x200, auditTextUtf16.data());

    std::wstring newName = Utf16ToWide(newNameUtf16.data());
    if (!CUser::CheckCharacterName(newName.c_str(), GetCacheDConfiguration().country, false)) {
        socket->Send("cdd", 26, 0, 3);
        return false;
    }

    CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        socket->Send("cdd", 26, 0, 1);
        return false;
    }

    if (server != nullptr) {
        server->Send("cddS", 0x4b, static_cast<int>(pledge->GetId()), 1, newNameUtf16.data());
        socket->Send("cd", 26, 1);
        return false;
    }

    if (!g_pledgeDb.ModifyPledgeName(pledge->GetId(), newName.data())) {
        socket->Send("cdd", 26, 0, 1);
        return false;
    }

    socket->Send("cd", 26, 1);
    return false;
}

bool SetProhibitedPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004364f4, AQ 27, codec dSS. Options 1/3 add a
    // complete prohibited name/substring. Options 2/4 delete respectively.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 27, 0, 0x1e);
        return false;
    }

    int option = 0;
    std::array<char16_t, 64> prohibitedTextUtf16{};
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dSS", &option, 0x80, prohibitedTextUtf16.data(), 0x200, auditTextUtf16.data());

    const std::wstring prohibitedText = Utf16ToWide(prohibitedTextUtf16.data());
    if (!CUser::CheckCharacterName(prohibitedText.c_str(), GetCacheDConfiguration().country, false)) {
        socket->Send("cdd", 27, 0, 3);
        return false;
    }

    const wchar_t* statement = nullptr;
    switch (option) {
    case 1:
    case 3:
        statement = L"{Call lin_AddProhibit (?, ? )}";
        break;
    case 2:
    case 4:
        statement = L"{Call lin_DelProhibit (?, ? )}";
        break;
    default:
        socket->Send("cdd", 27, 0, 1);
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Prepare(statement);
    connection.BindParameter(prohibitedText.c_str());
    connection.BindParameter(&option);
    // connection.ExecutePrepared(statement)
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot apply prohibited-name option %d to text %s: database operation failed. This command still replies with success.",
        //     option, WideToUtf8(prohibitedText).c_str()
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot apply prohibited-name option %d to text %s: database operation failed; this command still replies with success.",
            option, WideToUtf8(prohibitedText).c_str()
        );
    }
    socket->Send("cd", 27, 1);
    return false;
}

bool SeizeItemPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00418264, AQ 28, codec ddddS. This is the original
    // whole-item path and calls the older SeizeItem overload
    // with destination warehouse/use-SeizeItem2 both zero.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 28, 0, 0x1e);
        return false;
    }

    int option = 0;
    int characterId = 0;
    int sourceWarehouse = 0;
    int itemId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddddS", &option, &characterId, &sourceWarehouse, &itemId, 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    CItemSP item = CItem::Load(static_cast<unsigned int>(itemId));
    if (!user) {
        socket->Send("cdd", 28, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 28, 0, 7);
        return false;
    }
    if (user->GetReloadFlag()) {
        socket->Send("cdd", 28, 0, 0x3c);
        return false;
    }

    CWareHouseSP warehouse = user->GetWareHouse(sourceWarehouse, true);
    if (!warehouse) {
        socket->Send("cdd", 28, 0, 5);
        return false;
    }

    const bool seized = warehouse->SeizeItem(option, itemId, 0, 0);

    // Shipping reports this null CSPointer and then dereferences it. Keep
    // valid traffic identical while containing that malformed-item crash at
    // the process boundary, consistent with the other recovered null paths.
    if (!item) {
        runtime::process::ReportNullSmartPointerDereference();
        runtime::process::ReportCallStack(2, false);
        return false;
    }

    std::array<char16_t, 256> baseAttributeText{};
    BaseAttribute baseAttribute(item->Attribute());
    baseAttribute.GetLogString(baseAttributeText.data());
    const std::u16string baseAttributeLog = AdminLogText(baseAttributeText.data());
    const std::u16string variationLog = MakeAdminVariationLogText(item->m_intensiveItemType, item->OptionKey());

    if (!seized) {
        socket->Send("cdd", 28, 0, 1);
        return false;
    }

    socket->Send("cd", 28, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x271);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[9] = baseAttributeLog;
    fields[10] = variationLog;
    fields[11] = runtime::DecimalUtf16(option);
    fields[12] = runtime::DecimalUtf16(sourceWarehouse);
    fields[13] = runtime::DecimalUtf16(itemId);
    fields[14] = runtime::DecimalUtf16(0);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    fields[25] = runtime::DecimalUtf16(itemId);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool MoveItemPacket(CAdminSocket*, const std::uint8_t*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool MoveCharacterPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00436898, AQ 31, codec ddSS. The character is
    // disabled under the source account lock, then restored under the target
    // account lock. Both reloads and the direct CUser account fields occur in
    // that order in the shipping body.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 31, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int targetAccountId = 0;
    std::array<char16_t, 128> targetAccountNameUtf16{};
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(
        packet, "ddSS", &characterId, &targetAccountId, 0x100, targetAccountNameUtf16.data(), 0x200, auditTextUtf16.data()
    );

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 31, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 31, 0, 7);
        return false;
    }

    const int sourceAccountId = user->GetAccountID();
    if (sourceAccountId == targetAccountId) {
        socket->Send("cdd", 31, 0, 0x1d);
        return false;
    }

    CAccountSP sourceAccount = CAccount::Load(static_cast<unsigned int>(sourceAccountId));
    CAccountSP targetAccount = CAccount::Load(static_cast<unsigned int>(targetAccountId));
    if (!sourceAccount) {
        g_userDb.DeleteChar(user.get(), false);
    }
    if (!sourceAccount || !targetAccount) {
        socket->Send("cdd", 31, 0, 6);
        return false;
    }
    if (targetAccount->GetCharCount() >= 7) {
        socket->Send("cdd", 31, 0, 8);
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    bool sourceDisableExecuted;
    {
        L2_WRITE_GUARD(*sourceAccount.operator->());
        constexpr const wchar_t* disableStatement = L"{Call lin_DisableChar (?)}";
        connection.Prepare(disableStatement);
        connection.BindParameter(user->GetCharName());
        // connection.ExecutePrepared(disableStatement)
        sourceDisableExecuted = connection.ExecutePrepared(disableStatement);
        CAccount::Reload(static_cast<unsigned int>(sourceAccountId));
    }
    if (!sourceDisableExecuted) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot disable character %d in source account %d while moving to account %d: database operation failed. The source account was reloaded. The destination update still follows.",
        //     characterId, sourceAccountId, targetAccountId
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot disable character %d in source account %d while moving to account %d: database operation failed. The source account was reloaded; the destination update still follows.",
            characterId, sourceAccountId, targetAccountId
        );
    }
    connection.ResetParam();

    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x275);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(targetAccountId);
    fields[11] = runtime::DecimalUtf16(user->GetId());
    fields[12] = runtime::DecimalUtf16(targetAccountId);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = targetAccountNameUtf16.data();
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));

    int movedCharacterId;
    std::optional<std::wstring> targetNameStorage;
    bool targetRestoreExecuted;
    {
        L2_WRITE_GUARD(*targetAccount.operator->());
        user->m_accountId = targetAccountId;
        const std::size_t targetNameLength = std::char_traits<char16_t>::length(targetAccountNameUtf16.data());
        user->m_accountName.clear();
        if (targetNameLength >= 24) {
            // Current wcscpy_s(CUser+0x264, 0x18, source) clears the first
            // destination unit before invoking the process invalid-parameter
            // owner, then execution continues if that owner returns.
            runtime::process::HandleInvalidParameter(L"wcscpy_s");
        } else {
            user->m_accountName = targetAccountNameUtf16.data();
        }

        movedCharacterId = static_cast<int>(user->GetId());
        constexpr const wchar_t* restoreStatement = L"{Call lin_RestoreChar (?, ?, ? )}";
        connection.Prepare(restoreStatement);
        connection.BindParameter(&targetAccountId);
        connection.BindParameter(&movedCharacterId);
        const auto& targetAccountName = targetNameStorage.emplace(Utf16ToWide(targetAccountNameUtf16.data()));
        connection.BindParameter(targetAccountName.c_str());
        // connection.ExecutePrepared(restoreStatement)
        targetRestoreExecuted = connection.ExecutePrepared(restoreStatement);
        CAccount::Reload(static_cast<unsigned int>(targetAccountId));
    }
    if (!targetRestoreExecuted) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot restore character %d into destination account %d after the disable attempt on account %d. Cached account fields were already changed and the destination account reloaded. This command still replies with success.",
        //     characterId, targetAccountId, sourceAccountId
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot restore character %d into destination account %d after the disable attempt on account %d. Cached account fields were already changed and the destination account reloaded; this command still replies with success.",
            characterId, targetAccountId, sourceAccountId
        );
    }

    socket->Send("cd", 31, 1);
    return false;
}

bool CommentWritePacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00437234, AQ 32, codec ddSSSSS. The packet now
    // carries both identity fields, so this revision writes the supplied
    // character name/ID directly and performs no CUser lookup.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 32, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int accountId = 0;
    std::array<char16_t, 256> characterNameUtf16{};
    std::array<char16_t, 256> accountNameUtf16{};
    std::array<char16_t, 256> commentUtf16{};
    std::array<char16_t, 64> writerUtf16{};
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(
        packet, "ddSSSSS", &characterId, &accountId, 0x200, characterNameUtf16.data(), 0x200, accountNameUtf16.data(), 0x200,
        commentUtf16.data(), 0x80, writerUtf16.data(), 0x200, auditTextUtf16.data()
    );

    const std::u16string characterName(characterNameUtf16.data());
    const std::u16string comment(commentUtf16.data());
    const std::u16string writer(writerUtf16.data());
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_CommentWrite (?, ?, ?, ? )}";
    connection.Prepare(statement);
    connection.BindParameter(characterName.c_str());
    connection.BindParameter(&characterId);
    connection.BindParameter(comment.c_str());
    connection.BindParameter(writer.c_str());
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot store an administrative comment for character %d (%s), account %d: database operation failed. Returning failure.",
        //     characterId, WideToUtf8(characterName).c_str(), accountId
        // )
        // L2_LOG_ERROR(Log,
        //     "[admin] Cannot store an administrative comment for character %d (%s), account %d: database operation failed. Returning failure.",
        //     characterId, WideToUtf8(characterName).c_str(), accountId
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot store an administrative comment for character %d (%s), account %d: database operation failed; returning failure.",
            characterId, Utf16ToUtf8(characterName).c_str(), accountId
        );
        socket->Send("cdd", 32, 0, 1);
        return false;
    }

    socket->Send("cd", 32, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x276);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(characterId);
    fields[4] = runtime::DecimalUtf16(accountId);
    fields[8] = commentUtf16.data();
    fields[9] = writerUtf16.data();
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = characterNameUtf16.data();
    fields[24] = accountNameUtf16.data();
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool CommentDeletePacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0043755c, AQ 33. The packet carries comment ID
    // before delete mode, while lin_CommentDelete receives delete mode first.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 33, 0, 0x1e);
        return false;
    }

    int commentId = 0;
    int deleteMode = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddS", &commentId, &deleteMode, 0x200, auditTextUtf16.data());

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_CommentDelete (?, ? )}";
    connection.Prepare(statement);
    connection.BindParameter(&deleteMode);
    connection.BindParameter(&commentId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot delete administrative comment %d (mode %d): database operation failed. Returning failure.",
        //     commentId, deleteMode
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot delete administrative comment %d (mode %d): database operation failed; returning failure.",
            commentId, deleteMode
        );
        socket->Send("cdd", 33, 0, 1);
        return false;
    }

    socket->Send("cd", 33, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x277);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[11] = runtime::DecimalUtf16(commentId);
    fields[12] = runtime::DecimalUtf16(deleteMode);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool DeleteCharPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004377b0, AQ 34, codec dS. The original
    // CAccount::DeleteCharacter owner performs persistent deletion. This
    // handler then removes friendships and emits the optional account-summary
    // frame only while an L2Server connection is active.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 34, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dS", &characterId, 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 34, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 34, 0, 7);
        return false;
    }

    CPledgeSP pledge =
        g_pledgeDb.GetPledge(static_cast<int>(user->GetPledgeID()), false, runtime::process::CurrentDatabaseId());
    if (pledge && pledge->rullerId() == user->GetId()) {
        socket->Send("cdd", 34, 0, 0x12);
        return false;
    }

    const int userId = static_cast<int>(user->GetId());
    const int accountId = user->GetAccountID();
    const std::u16string characterName(user->GetCharName());
    const std::u16string accountName(user->GetAccountName());
    CAccountSP account = CAccount::Load(static_cast<unsigned int>(accountId));
    if (!account) {
        g_userDb.DeleteChar(user.get(), false);
        socket->Send("cdd", 34, 0, 6);
        return false;
    }

    if (account->DeleteCharacter(static_cast<unsigned int>(userId)) < 1) {
        socket->Send("cdd", 34, 0, 1);
        return false;
    }

    FriendIdSet friendIds;
    g_friendsDb.GetFriendIdList(user->GetId(), friendIds);
    g_friendsDb.DeleteFriends(user->GetId(), friendIds);
    socket->Send("cd", 34, 1);

    if (g_pActiveServerSocket.load(std::memory_order_acquire) != nullptr) {
        int highestCharacterId = 0;
        int highestLevel = 0;
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Bind(&highestCharacterId);
        connection.Bind(&highestLevel);
        constexpr const wchar_t* statement = L"{Call lin_ReloadAccountHighestExpAndLevel (?)}";
        connection.Prepare(statement);
        connection.BindParameter(&accountId);
        if (connection.ExecutePrepared(statement)) {
            connection.Fetch();
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     "[admin] Cannot reload highest-character statistics for account %d after deleting character %d. The success reply was already sent. The follow-up reply still uses the current result fields (character %d, level %d).",
            //     accountId, userId, highestCharacterId, highestLevel
            // )
            L2_LOG_ERROR(Log,
                "[admin] Cannot reload highest-character statistics for account %d after deleting character %d. The success reply was already sent; the follow-up reply still uses the current result fields (character %d, level %d).",
                accountId, userId, highestCharacterId, highestLevel
            );
        }
        socket->Send("cddddddd", 6, 0, accountId, userId, user->GetLevel(), highestCharacterId, highestLevel, 0);
    }

    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x278);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(userId);
    fields[4] = runtime::DecimalUtf16(accountId);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(characterName.c_str());
    fields[24] = AdminLogText(accountName.c_str());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool RestoreCharPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004380ac, AQ 35, codec ddSS. SQL receives the
    // loaded CAccount name, while the direct resident CUser copy and audit
    // keep the supplied packet account name.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 35, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int accountId = 0;
    std::array<char16_t, 256> accountNameUtf16{};
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddSS", &characterId, &accountId, 0x200, accountNameUtf16.data(), 0x200, auditTextUtf16.data());

    CAccountSP account = CAccount::Load(static_cast<unsigned int>(accountId));
    if (!account) {
        socket->Send("cdd", 35, 0, 6);
        return false;
    }
    if (account->GetCharCount() >= 7) {
        socket->Send("cdd", 35, 0, 8);
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    bool restoreExecuted;
    std::optional<CUserSP> userStorage;
    {
        L2_WRITE_GUARD(*account.operator->());
        constexpr const wchar_t* statement = L"{Call lin_RestoreChar (?, ?, ? )}";
        connection.Prepare(statement);
        connection.BindParameter(&accountId);
        connection.BindParameter(&characterId);
        connection.BindParameter(account->GetName());
        // connection.ExecutePrepared(statement)
        restoreExecuted = connection.ExecutePrepared(statement);

        auto& user = userStorage.emplace(g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId()));
        if (user) {
            user->m_accountId = accountId;
            const std::size_t accountNameLength = std::char_traits<char16_t>::length(accountNameUtf16.data());
            user->m_accountName.clear();
            if (accountNameLength >= 24) {
                runtime::process::HandleInvalidParameter(L"wcscpy_s");
            } else {
                user->m_accountName = accountNameUtf16.data();
            }
            CAccount::Reload(static_cast<unsigned int>(accountId));
        }
    }
    auto& user = *userStorage;
    if (!restoreExecuted) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot restore character %d into account %d: database operation failed. If the character lookup succeeded, cached account fields were still changed and the account reloaded. The reply follows that lookup result.",
        //     characterId, accountId
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot restore character %d into account %d: database operation failed. If the character lookup succeeded, cached account fields were still changed and the account reloaded; the reply follows that lookup result.",
            characterId, accountId
        );
    }

    if (!user) {
        socket->Send("cdd", 35, 0, 1);
        return false;
    }

    socket->Send("cd", 35, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x279);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(accountId);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = accountNameUtf16.data();
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool PledgeOustPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00418ea4, AQ 36, codec dS. This administrative
    // path mutates the original CPledge/CUser owners directly and notifies
    // L2Server through opcode 0x94/subcode 10 when connected.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 36, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dS", &characterId, 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 36, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 36, 0, 7);
        return false;
    }

    const int pledgeId = static_cast<int>(user->GetPledgeID());
    if (pledgeId < 1) {
        socket->Send("cdd", 36, 0, 0x11);
        return false;
    }
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        socket->Send("cdd", 36, 0, 0x18);
        return false;
    }
    if (pledge->rullerId() == user->GetId()) {
        socket->Send("cdd", 36, 0, 0x12);
        return false;
    }

    pledge->RemoveMember(user->GetId(), user->GetPledgeType());
    user->SetPledge(0, MASTER_PLEDGE);
    user->m_gradeId = 0;

    CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (server != nullptr) {
        const std::u16string characterNameUtf16 = AdminLogText(user->GetCharName());
        server->Send("cdddS", 0x94, 10, pledgeId, characterId, characterNameUtf16.c_str());
    }

    socket->Send("cd", 36, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x27a);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));

    if (!PledgeContributionInCache::DeleteInfo(characterId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "PLEDGE[adminsocket.cpp][%d]Failed delete contribution "
        //     "info: %d",
        //     0xb04, characterId
        // )
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot delete contribution data for character %d after removal from clan %d. The success reply was already sent.",
        //     characterId, pledgeId
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot delete contribution data for character %d after removal from clan %d; the success reply was already sent.",
            characterId, pledgeId
        );
    }
    return false;
}

bool BanCharPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004386e4, AQ 39, codec ddS. Hours are deliberately
    // not range-checked in this revision. The x64 IMUL/ADD path wraps at 32
    // bits for both the database timestamp and the later resident copy.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 39, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int hours = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddS", &characterId, &hours, 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 39, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 39, 0, 7);
        return false;
    }

    const std::uint32_t durationBits = static_cast<std::uint32_t>(hours) * 0xe10U;
    const std::uint32_t databaseBanEndBits = static_cast<std::uint32_t>(runtime::crt::Time32Now()) + durationBits;
    int databaseBanEnd = 0;
    std::memcpy(&databaseBanEnd, &databaseBanEndBits, sizeof(databaseBanEnd));
    int status = 1;
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* setBanStatement = L"{Call lin_SetUserBan (?, ?, ?, ? )}";
    connection.Prepare(setBanStatement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&status);
    connection.BindParameter(&hours);
    connection.BindParameter(&databaseBanEnd);
    const bool saved = connection.ExecutePrepared(setBanStatement);
    connection.ResetParam();

    int historyAction = 0;
    if (hours == 0) {
        if (user->GetStatus() == 1) {
            historyAction = 0x15;
        } else if (user->GetStatus() == 2) {
            historyAction = 0x19;
        }
    } else {
        historyAction = 0x0b;
    }
    if (historyAction != 0) {
        const unsigned int historyCharacterId = user->GetId();
        constexpr const wchar_t* historyStatement = L"{Call lin_AddPunishmentHistory (?, ?, ?, ?)}";
        connection.Prepare(historyStatement);
        connection.BindParameter(user->GetCharName());
        connection.BindParameter(&historyCharacterId);
        connection.BindParameter(&historyAction);
        connection.BindParameter(user->GetAccountName());
        // connection.ExecutePrepared(historyStatement)
        if (!connection.ExecutePrepared(historyStatement)) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[admin] Cannot record ban history for character %d (hours %d, history action %d). The ban database operation %s and determines the reply.",
            //     characterId, hours, historyAction, saved ? "executed" : "failed"
            // )
            L2_LOG_ERROR(Log,
                "[admin] Cannot record ban history for character %d (hours %d, history action %d); the ban database operation %s and determines the reply.",
                characterId, hours, historyAction, saved ? "executed" : "failed"
            );
        }
    }

    if (!saved) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot store a ban of %d hours for character %d (database end timestamp %d). History handling already ran. Returning failure without changing the cached ban.",
        //     hours, characterId, databaseBanEnd
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot store a ban of %d hours for character %d (database end timestamp %d). History handling already ran; returning failure without changing the cached ban.",
            hours, characterId, databaseBanEnd
        );
        socket->Send("cdd", 39, 0, 0x0b);
        return false;
    }

    const std::uint32_t residentBanEndBits = static_cast<std::uint32_t>(runtime::crt::Time32Now()) + durationBits;
    int residentBanEnd = 0;
    std::memcpy(&residentBanEnd, &residentBanEndBits, sizeof(residentBanEnd));
    user->SetStatus(1);
    user->SetBanEnd(residentBanEnd);
    socket->Send("cd", 39, 1);

    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x27d);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[5] = runtime::DecimalUtf16(user->GetX());
    fields[6] = runtime::DecimalUtf16(user->GetY());
    fields[7] = runtime::DecimalUtf16(user->GetZ());
    fields[11] = runtime::DecimalUtf16(hours);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool MoveItem2Packet(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00419af0, AQ 40, codec ddddS. This is the older
    // whole-item administrative path. It must not inherit the amount-aware
    // and shape-shift behavior of the later MoveItemProc implementation.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 40, 0, 0x1e);
        return false;
    }

    int sourceCharacterId = 0;
    int itemId = 0;
    int destinationCharacterId = 0;
    int destinationWarehouse = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(
        packet, "ddddS", &sourceCharacterId, &itemId, &destinationCharacterId, &destinationWarehouse, 0x200,
        auditTextUtf16.data()
    );

    const auto fail = [&](int reason) {
        socket->Send("cdd", 40, 0, reason);
        return false;
    };
    if (sourceCharacterId == destinationCharacterId) {
        return fail(0x10);
    }

    CUserSP sourceUser = g_userDb.GetUser(sourceCharacterId, true, false, runtime::process::CurrentDatabaseId());
    CUserSP destinationUser = g_userDb.GetUser(destinationCharacterId, true, false, runtime::process::CurrentDatabaseId());
    if (!sourceUser || !destinationUser)
        return fail(4);
    if (sourceUser->IsLoggedIn() || destinationUser->IsLoggedIn()) {
        return fail(7);
    }
    if (sourceUser->GetReloadFlag() || destinationUser->GetReloadFlag()) {
        return fail(0x3c);
    }

    const int sourceWarehouse = sourceUser->GetItemWare(itemId);
    if (sourceWarehouse < 0)
        return fail(1);

    CItemSP item;
    int itemType = 0;
    bool moved = false;
    if (sourceWarehouse == 0 || sourceWarehouse == 1) {
        CWareHouseSP source = sourceUser->GetWareHouse(sourceWarehouse, true);
        CWareHouseSP destination = destinationUser->GetWareHouse(destinationWarehouse, true);
        if (!source || !destination)
            return fail(5);

        item = source->GetItem(itemId);
        if (!item) {
            // The target reports a null CSPointer and then dereferences it.
            // Keep valid behavior exact while containing malformed state.
            runtime::process::ReportNullSmartPointerDereference();
            runtime::process::ReportCallStack(2, false);
            return fail(1);
        }
        itemType = static_cast<int>(item->ItemType());
        moved = source->MoveItemToChar(itemId, destination.get());
    } else if (sourceWarehouse == 1001) {
        CWareHouseSP destination = destinationUser->GetWareHouse(destinationWarehouse, true);
        if (!destination)
            return fail(5);

        item = CItem::Load(static_cast<unsigned int>(itemId));
        if (!item) {
            runtime::process::ReportNullSmartPointerDereference();
            runtime::process::ReportCallStack(2, false);
            return fail(1);
        }
        itemType = static_cast<int>(item->ItemType());
        moved = sourceUser->SetSeizedItemOwner(itemId, static_cast<int>(destinationUser->GetId()), destination.get());
    } else {
        return fail(1);
    }

    const std::u16string itemLog = MakeAdminMoveItem2LogText(item->m_intensiveItemType, *item.get());
    if (!moved)
        return fail(1);

    socket->Send("cd", 40, 1);
    const std::wstring auditText = Utf16ToWide(auditTextUtf16.data());
    WriteAdminMoveItemAudit(*sourceUser.get(), *destinationUser.get(), itemLog, itemType, 0, auditText.c_str(), itemId);
    return false;
}

bool CreatePetPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0041a9d4, AQ 44, codec dddS. Unlike the adjacent
    // administrative handlers, this shipping body has no request-rejection
    // gate: it decodes and validates the collar ID immediately.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int collarId = 0;
    int npcClassId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dddS", &characterId, &collarId, &npcClassId, 0x200, auditTextUtf16.data());
    if (collarId < 0) {
        socket->Send("cdd", 44, 0, 3);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 44, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 44, 0, 7);
        return false;
    }

    CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (server == nullptr) {
        socket->Send("cdd", 44, 0, 0x13);
        return false;
    }
    server->Send("cdddd", 0x94, 9, user->GetId(), collarId, npcClassId);
    socket->Send("cd", 44, 1);

    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x282);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[5] = runtime::DecimalUtf16(user->GetX());
    fields[6] = runtime::DecimalUtf16(user->GetY());
    fields[7] = runtime::DecimalUtf16(user->GetZ());
    fields[11] = runtime::DecimalUtf16(collarId);
    fields[12] = runtime::DecimalUtf16(npcClassId);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool SendHomePacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0041af20, AQ 45, codec dS.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 45, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dS", &characterId, 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 45, 0, 4);
        return false;
    }

    CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (server == nullptr) {
        socket->Send("cdd", 45, 0, 0x13);
        return false;
    }

    const std::u16string characterNameUtf16 = AdminLogText(user->GetCharName());
    // The current x64 call supplies only four of the five trailing dwords in
    // cdddSddddd. Preserve the wire width and make only that undefined stack
    // disclosure deterministic. The eight confirmed values keep their
    // exact current X/Y/Z order.
    server->Send(
        "cdddSddddd", 0x94, 1, user->GetId(), user->GetRace(), characterNameUtf16.c_str(), user->GetX(), user->GetY(),
        user->GetZ(), user->GetAlign(), 0
    );
    socket->Send("cd", 45, 1);
    return false;
}

bool CreatePledgePacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0041b494, AQ 47, codec SdS.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 47, 0, 0x1e);
        return false;
    }

    std::array<char16_t, 256> pledgeNameUtf16{};
    int rulerCharacterId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "SdS", 0x200, pledgeNameUtf16.data(), &rulerCharacterId, 0x200, auditTextUtf16.data());

    const std::wstring pledgeName = Utf16ToWide(pledgeNameUtf16.data());
    std::wstring lowercasePledgeName = pledgeName;
    Utils::CopyStrInLowerCase(pledgeName.c_str(), lowercasePledgeName.data());

    CPledgeSP existingPledge = g_pledgeDb.GetPledge(lowercasePledgeName.data(), false);
    if (existingPledge) {
        socket->Send("cdd", 47, 0, 0x15);
        return false;
    }

    CUserSP user = g_userDb.GetUser(rulerCharacterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 47, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 47, 0, 7);
        return false;
    }
    if (static_cast<std::int32_t>(user->GetPledgeID()) >= 1) {
        socket->Send("cdd", 47, 0, 9);
        return false;
    }
    if (!CUser::CheckCharacterName(lowercasePledgeName.c_str(), GetCacheDConfiguration().country, false)) {
        socket->Send("cdd", 47, 0, 3);
        return false;
    }

    const int pledgeId = g_pledgeDb.CreatePledge(lowercasePledgeName.data(), static_cast<int>(user->GetId()));
    if (pledgeId < 1) {
        socket->Send("cdd", 47, 0, 1);
        return false;
    }
    user->SetPledge(pledgeId, MASTER_PLEDGE);
    socket->Send("cd", 47, 1);
    return false;
}

bool SetSkillAllPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0041ba5c, AQ 48, codec dS.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 48, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dS", &characterId, 0x200, auditTextUtf16.data());
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 48, 0, 4);
        return false;
    }

    CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (server == nullptr) {
        socket->Send("cdd", 48, 0, 0x13);
        return false;
    }
    const std::u16string characterNameUtf16 = AdminLogText(user->GetCharName());
    server->Send(
        "cdddddS", 0x94, 3, user->GetId(), user->GetClass(), user->GetLevel(), user->GetRace(), characterNameUtf16.c_str()
    );
    socket->Send("cd", 48, 1);
    return false;
}

bool RestoreChar2Packet(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004395f8, AQ 49, codec dSdSS. Unlike AQ 35 this
    // path also consumes the 23 restored-item result columns returned by
    // lin_RestoreChar2 and recreates each item through CWareHouse.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 49, 0, 0x1e);
        return false;
    }

    int deletedItemId = 0;
    int itemType = 0;
    std::int64_t amount = 0;
    int enchant = 0;
    int eroded = 0;
    int bless = 0;
    int ident = 0;
    int wished = 0;
    int warehouseNumber = 0;
    int variationOption1 = 0;
    int variationOption2 = 0;
    int intensiveItemType = 0;
    short hLocked = 0;
    short useCount = 0;
    short attackType = static_cast<short>(ATTRT_NONE);
    short attackValue = 0;
    std::array<short, 6> defenseValues{};
    int duration = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&deletedItemId);
    connection.Bind(&itemType);
    connection.Bind(&amount);
    connection.Bind(&enchant);
    connection.Bind(&eroded);
    connection.Bind(&bless);
    connection.Bind(&ident);
    connection.Bind(&wished);
    connection.Bind(&warehouseNumber);
    connection.Bind(&variationOption1);
    connection.Bind(&variationOption2);
    connection.Bind(&intensiveItemType);
    connection.Bind(&hLocked);
    connection.Bind(&useCount);
    connection.Bind(&attackType);
    connection.Bind(&attackValue);
    for (short& defenseValue : defenseValues) {
        connection.Bind(&defenseValue);
    }
    connection.Bind(&duration);

    int characterId = 0;
    std::array<char16_t, 256> newCharacterNameUtf16{};
    int accountId = 0;
    std::array<char16_t, 256> accountNameUtf16{};
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(
        packet, "dSdSS", &characterId, 0x200, newCharacterNameUtf16.data(), &accountId, 0x200, accountNameUtf16.data(),
        0x200, auditTextUtf16.data()
    );

    const std::wstring newCharacterName = Utf16ToWide(newCharacterNameUtf16.data());
    std::wstring lowercaseName = newCharacterName;
    Utils::CopyStrInLowerCase(newCharacterName.c_str(), lowercaseName.data());
    if (!CUser::CheckCharacterName(lowercaseName.c_str(), GetCacheDConfiguration().country, false)) {
        socket->Send("cdd", 49, 0, 3);
        return false;
    }

    CAccountSP account = CAccount::Load(static_cast<unsigned int>(accountId));
    if (!account) {
        socket->Send("cdd", 49, 0, 6);
        return false;
    }
    if (account->GetCharCount() >= 7) {
        socket->Send("cdd", 49, 0, 8);
        return false;
    }

    bool restoreExecuted;
    std::optional<CUserSP> userStorage;
    std::optional<std::wstring> packetNameStorage;
    {
        L2_WRITE_GUARD(*account.operator->());
        constexpr const wchar_t* restoreStatement = L"{Call lin_RestoreChar2 (?, ?, ?, ? )}";
        connection.Prepare(restoreStatement);
        connection.BindParameter(&accountId);
        connection.BindParameter(&characterId);
        connection.BindParameter(newCharacterName.c_str());
        connection.BindParameter(account->GetName());
        // connection.ExecutePrepared(restoreStatement)
        restoreExecuted = connection.ExecutePrepared(restoreStatement);

        auto& user = userStorage.emplace(g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId()));
        auto& packetAccountName = packetNameStorage.emplace(Utf16ToWide(accountNameUtf16.data()));
        if (user) {
            user->m_accountId = accountId;
            const std::size_t accountNameLength = std::char_traits<char16_t>::length(accountNameUtf16.data());
            user->m_accountName.clear();
            if (accountNameLength >= 24) {
                runtime::process::HandleInvalidParameter(L"wcscpy_s");
            } else {
                user->m_accountName = WideToUtf16(packetAccountName);
            }
            CAccount::Reload(static_cast<unsigned int>(accountId));
        }
    }
    auto& user = *userStorage;
    auto& packetAccountName = *packetNameStorage;
    if (!restoreExecuted) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot restore character %d (%s) and items into account %d: database operation failed. Cached account fields may already have changed. Processing continues according to the character lookup and item result rows.",
        //     characterId, WideToUtf8(newCharacterName).c_str(), accountId
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot restore character %d (%s) and items into account %d: database operation failed. Cached account fields may already have changed; processing continues according to the character lookup and item result rows.",
            characterId, WideToUtf8(newCharacterName).c_str(), accountId
        );
    }

    if (!user) {
        socket->Send("cdd", 49, 0, 1);
        return false;
    }

    while (connection.Fetch()) {
        CWareHouseSP warehouse = user->GetWareHouse(warehouseNumber, true);
        // if (!warehouse)
        //     continue.
        if (!warehouse) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[admin] Cannot restore deleted item %d (type %d, amount %lld) for character %d: warehouse %d unavailable. Skipping this item. The command still replies with success.",
            //     deletedItemId, itemType, static_cast<long long>(amount), characterId, warehouseNumber
            // )
            L2_LOG_ERROR(Log,
                "[admin] Cannot restore deleted item %d (type %d, amount %lld) for character %d: warehouse %d unavailable. Skipping this item; the command still replies with success.",
                deletedItemId, itemType, static_cast<long long>(amount), characterId, warehouseNumber
            );
            continue;
        }

        int restoredItemId = 0;
        ItemOptionKey optionKey{variationOption1, variationOption2};
        BaseAttribute baseAttribute(attackType, attackValue, defenseValues.data());
        const bool restored = warehouse->AddItemByAdmin(
            &restoredItemId, itemType, amount, enchant, eroded, bless, ident, wished, warehouseNumber, optionKey,
            intensiveItemType, baseAttribute, hLocked, useCount, 0, 0, 0, 0
        );
        // if (!restored)
        //     continue.
        if (!restored) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[admin] Cannot recreate deleted item %d (type %d, amount %lld) for character %d in warehouse %d. Skipping its duration and pet restoration. The command still replies with success.",
            //     deletedItemId, itemType, static_cast<long long>(amount), characterId, warehouseNumber
            // )
            L2_LOG_ERROR(Log,
                "[admin] Cannot recreate deleted item %d (type %d, amount %lld) for character %d in warehouse %d. Skipping its duration and pet restoration; the command still replies with success.",
                deletedItemId, itemType, static_cast<long long>(amount), characterId, warehouseNumber
            );
            continue;
        }

        if (duration >= 0) {
            DBConn durationConnection(runtime::process::CurrentDatabaseId());
            constexpr const wchar_t* durationStatement = L"{Call lin_UpdateItemDuration (?, ?)}";
            durationConnection.Prepare(durationStatement);
            durationConnection.BindParameter(&deletedItemId);
            durationConnection.BindParameter(&duration);
            // durationConnection.ExecutePrepared(durationStatement)
            if (!durationConnection.ExecutePrepared(durationStatement)) {
                // Log.Add(
                //     LOG_ERROR,
                //     "[admin] Cannot restore duration %d for deleted item ID %d (new item ID %d, character %d). The new item remains and this command still replies with success.",
                //     duration, deletedItemId, restoredItemId, characterId
                // )
                L2_LOG_ERROR(Log,
                    "[admin] Cannot restore duration %d for deleted item ID %d (new item ID %d, character %d); the new item remains and this command still replies with success.",
                    duration, deletedItemId, restoredItemId, characterId
                );
            }
        }

        if (g_readFile.IsPetCollar(itemType)) {
            DBConn petConnection(runtime::process::CurrentDatabaseId());
            constexpr const wchar_t* petStatement = L"{Call lin_RestorePet (?, ?, ?)}";
            petConnection.Prepare(petStatement);
            petConnection.BindParameter(&characterId);
            petConnection.BindParameter(&deletedItemId);
            petConnection.BindParameter(&restoredItemId);
            // petConnection.ExecutePrepared(petStatement)
            if (!petConnection.ExecutePrepared(petStatement)) {
                // Log.Add(
                //     LOG_ERROR,
                //     "[admin] Cannot restore pet data for character %d from deleted item ID %d to new collar item ID %d. The new item remains and this command still replies with success.",
                //     characterId, deletedItemId, restoredItemId
                // )
                L2_LOG_ERROR(Log,
                    "[admin] Cannot restore pet data for character %d from deleted item ID %d to new collar item ID %d; the new item remains and this command still replies with success.",
                    characterId, deletedItemId, restoredItemId
                );
            }
        }
    }

    socket->Send("cd", 49, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x279);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(accountId);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(packetAccountName.c_str());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool ManAnnouncePacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0043a100, AQ 50, codec ddSS.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 50, 0, 0x1e);
        return false;
    }

    int option = 0;
    int announceId = 0;
    std::array<char16_t, 256> announceTextUtf16{};
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddSS", &option, &announceId, 0x200, announceTextUtf16.data(), 0x200, auditTextUtf16.data());
    if (announceId < 1) {
        socket->Send("cdd", 50, 0, 0x17);
        return false;
    }

    const std::wstring announceText = Utf16ToWide(announceTextUtf16.data());
    if (option == 1 || option == 2) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_SetLoginAnnounce (?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&announceId);
        connection.BindParameter(announceText.c_str());
        if (connection.ExecutePrepared(statement)) {
            L2_GUARD(g_announce);
            g_announce.ReadAnnounce(true);
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     "[admin] Login-announcement update failed for ID %d (option %d): database operation failed. The local announcement cache is not reloaded. This command still replies with success.",
            //     announceId, option
            // )
            L2_LOG_ERROR(Log,
                "[admin] Login-announcement update failed for ID %d (option %d): database operation failed. The local announcement cache is not reloaded; this command still replies with success.",
                announceId, option
            );
        }
    } else if (option == 3) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_DelLoginAnnounce (?)}";
        connection.Prepare(statement);
        connection.BindParameter(&announceId);
        if (connection.ExecutePrepared(statement)) {
            L2_GUARD(g_announce);
            g_announce.ReadAnnounce(true);
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     "[admin] Login-announcement deletion failed for ID %d (option %d): database operation failed. The local announcement cache is not reloaded. This command still replies with success.",
            //     announceId, option
            // )
            L2_LOG_ERROR(Log,
                "[admin] Login-announcement deletion failed for ID %d (option %d): database operation failed. The local announcement cache is not reloaded; this command still replies with success.",
                announceId, option
            );
        }
    } else {
        socket->Send("cdd", 50, 0, 1);
        return false;
    }

    std::wstring logText = announceText;
    for (wchar_t& character : logText) {
        if (character == L',')
            character = L' ';
    }
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x28f);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[5] = runtime::DecimalUtf16(0);
    fields[6] = runtime::DecimalUtf16(0);
    fields[7] = runtime::DecimalUtf16(0);
    fields[8] = AdminLogText(logText.c_str());
    fields[11] = runtime::DecimalUtf16(option);
    fields[12] = runtime::DecimalUtf16(announceId);
    fields[13] = runtime::DecimalUtf16(0);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    socket->Send("cd", 50, 1);
    return false;
}

bool ManIntAnnouncePacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0043a4d4, AQ 51, codec dddSS.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 51, 0, 0x1e);
        return false;
    }

    int option = 0;
    int interval = 0;
    int announceId = 0;
    std::array<char16_t, 256> announceTextUtf16{};
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(
        packet, "dddSS", &option, &interval, &announceId, 0x200, announceTextUtf16.data(), 0x200, auditTextUtf16.data()
    );
    if (announceId < 1) {
        socket->Send("cdd", 51, 0, 0x17);
        return false;
    }

    const std::wstring announceText = Utf16ToWide(announceTextUtf16.data());
    if (option == 1 || option == 2) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_SetIntervalAnnounce (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&interval);
        connection.BindParameter(&announceId);
        connection.BindParameter(announceText.c_str());
        // connection.ExecutePrepared(statement)
        if (!connection.ExecutePrepared(statement)) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[admin] Interval-announcement update failed for ID %d, interval %d (option %d): database operation failed. This command still replies with success.",
            //     announceId, interval, option
            // )
            L2_LOG_ERROR(Log,
                "[admin] Interval-announcement update failed for ID %d, interval %d (option %d): database operation failed; this command still replies with success.",
                announceId, interval, option
            );
        }
    } else if (option == 3) {
        bool deleted = false;
        {
            DBConn connection(runtime::process::CurrentDatabaseId());
            constexpr const wchar_t* statement = L"{Call lin_DelIntervalAnnounce (?, ?)}";
            connection.Prepare(statement);
            connection.BindParameter(&interval);
            connection.BindParameter(&announceId);
            deleted = connection.ExecutePrepared(statement);
            if (!deleted) {
                // Log.Add(
                //     LOG_ERROR, "[%s][%d] del interval announce failed [%d][%d]", "adminsocket.cpp", 0xe84,
                //     interval, announceId
                // )
                // Log.Add(
                //     LOG_ERROR,
                //     "[admin] Cannot delete interval announcement %d (interval %d): database operation failed. Returning failure.",
                //     announceId, interval
                // )
                L2_LOG_ERROR(Log,
                    "[admin] Cannot delete interval announcement %d (interval %d): database operation failed; returning failure.",
                    announceId, interval
                );
            }
        }
        if (!deleted) {
            socket->Send("cdd", 51, 0, 1);
            return false;
        }
    } else if (option == 4) {
        CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
        if (server == nullptr) {
            // Log.Add(
            //     LOG_WARN,
            //     "[admin] Interval-announcement reload not requested: L2Server is unavailable. Returning failure."
            // )
            L2_LOG_WARN(Log,
                "[admin] Interval-announcement reload not requested: L2Server is unavailable; returning failure."
            );
            socket->Send("cdd", 51, 0, 1);
            return false;
        }
        server->Send("cd", 0x94, 4);
    } else {
        socket->Send("cdd", 51, 0, 1);
        return false;
    }

    std::wstring logText = announceText;
    for (wchar_t& character : logText) {
        if (character == L',')
            character = L' ';
    }
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x28f);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[5] = runtime::DecimalUtf16(0);
    fields[6] = runtime::DecimalUtf16(0);
    fields[7] = runtime::DecimalUtf16(0);
    fields[8] = AdminLogText(logText.c_str());
    fields[11] = runtime::DecimalUtf16(option);
    fields[12] = runtime::DecimalUtf16(announceId);
    fields[13] = runtime::DecimalUtf16(interval);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    socket->Send("cd", 51, 1);
    return false;
}

bool ModWeekPlayPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0043a90c, AQ 52, codec dddS.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 52, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int type = 0;
    int changeMinutes = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dddS", &characterId, &type, &changeMinutes, 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 52, 0, 6);
        return false;
    }

    CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (user->IsLoggedIn() && server != nullptr) {
        server->Send("cdddd", 0x94, 5, user->GetId(), type, changeMinutes);
    }

    int accountId = 0;
    int usedSeconds = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&accountId);
    connection.Bind(&usedSeconds);
    constexpr const wchar_t* statement = L"{Call lin_ChangeTimeData (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(user->GetCharName());
    connection.BindParameter(&type);
    connection.BindParameter(&changeMinutes);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
        // Log.Add(LOG_ERROR,
        //     "[admin play time] Cannot obtain the time-change result for character %d, type %d, requested change %d minutes: execution or fetch failed. The game server may already have been notified. Cached time is not updated and failure reason 1 is returned.",
        //     characterId, type, changeMinutes)
        L2_LOG_ERROR(Log,
            "[admin play time] Cannot obtain the time-change result for character %d, type %d, requested change %d minutes: execution or fetch failed. The game server may already have been notified; cached time is not updated and failure reason 1 is returned.",
            characterId, type, changeMinutes);
        socket->Send("cdd", 52, 0, 1);
        return false;
    }

    CAccountSP account = CAccount::Load(static_cast<unsigned int>(accountId));
    if (!account) {
        // Log.Add(LOG_ERROR,
        //     "[admin play time] Time-change query returned account %d and %d used seconds for character %d, but the account could not be loaded. Cache updates are skipped and failure reason 6 is returned.",
        //     accountId, usedSeconds, characterId)
        L2_LOG_ERROR(Log,
            "[admin play time] Time-change query returned account %d and %d used seconds for character %d, but the account could not be loaded; cache updates are skipped and failure reason 6 is returned.",
            accountId, usedSeconds, characterId);
        socket->Send("cdd", 52, 0, 6);
        return false;
    }

    const runtime::SystemTime localTime = runtime::GetLocalTimeNow();
    const SYSTEMTIME targetLocalTime{localTime.wYear, localTime.wMonth, localTime.wDayOfWeek, localTime.wDay,
        localTime.wHour, localTime.wMinute, localTime.wSecond, localTime.wMilliseconds};
    account->SetWeekPlayTime(usedSeconds);
    account->SetLastSaveTIme(&targetLocalTime);
    socket->Send("cd", 52, 1);
    return false;
}

bool SeizeItem2Packet(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0042c1b8, AQ 53, codec ddS. This older whole-item
    // owner deletes the process-cache entry first and routes through the
    // four-int SeizeItem overload with lin_SeizeItem2 selected.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 53, 0, 0x1e);
        return false;
    }

    int option = 0;
    int itemId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddS", &option, &itemId, 0x200, auditTextUtf16.data());

    CItemSP item = CItem::Load(static_cast<unsigned int>(itemId));
    if (!item) {
        socket->Send("cdd", 53, 0, 1);
        return false;
    }

    const int ownerId = static_cast<int>(item->OwnerID());
    const int originalWarehouse = item->Warehouse();
    const int itemType = static_cast<int>(item->ItemType());
    std::array<char16_t, 256> baseAttributeText{};
    BaseAttribute baseAttribute(item->Attribute());
    baseAttribute.GetLogString(baseAttributeText.data());
    const std::u16string baseAttributeLog = AdminLogText(baseAttributeText.data());
    const std::u16string variationLog = MakeAdminVariationLogText(item->m_intensiveItemType, item->OptionKey());
    item->Delete(false, false);

    const auto destinationWarehouse = [option](int seizedWarehouse, int liveWarehouse) {
        if (option == 1)
            return seizedWarehouse;
        if (option == 2)
            return liveWarehouse;
        return 0;
    };

    bool seized = false;
    int reason = 1;
    std::u16string ownerName;
    std::u16string accountName;

    if (originalWarehouse == 0 || originalWarehouse == 1 || originalWarehouse == 1001) {
        CUserSP user = g_userDb.GetUser(ownerId, true, false, runtime::process::CurrentDatabaseId());
        if (user) {
            if (user->IsLoggedIn()) {
                reason = 7;
            } else if (user->GetReloadFlag()) {
                reason = 0x3c;
            } else {
                const int liveWarehouse = originalWarehouse == 1001 ? 1 : originalWarehouse;
                CWareHouseSP warehouse = user->GetWareHouse(liveWarehouse, true);
                if (warehouse) {
                    seized = warehouse->SeizeItem(option, itemId, destinationWarehouse(1001, 1), 1);
                    ownerName = AdminLogText(user->GetCharName());
                    accountName = AdminLogText(user->GetAccountName());
                }
            }
        }
    } else if (originalWarehouse == 2 || originalWarehouse == 1002) {
        CPledgeSP pledge = g_pledgeDb.GetPledge(ownerId, false, runtime::process::CurrentDatabaseId());
        if (pledge) {
            CWareHouseSP warehouse = pledge->GetWareHouse();
            if (warehouse) {
                seized = warehouse->SeizeItem(option, itemId, destinationWarehouse(1002, 2), 1);
                ownerName = AdminLogText(pledge->GetName());
                accountName = ownerName;
            }
        }
    } else if (originalWarehouse == 3 || originalWarehouse == 1003) {
        CCastleSP castle = g_castleDb.GetCastle(ownerId);
        if (castle) {
            CWareHouseSP warehouse = castle->GetWareHouse();
            if (warehouse) {
                seized = warehouse->SeizeItem(option, itemId, destinationWarehouse(1003, 3), 1);
            }
        }
    } else if (originalWarehouse == 5 || originalWarehouse == 1004) {
        CPetSP pet = g_petDb.GetPet(ownerId, runtime::process::CurrentDatabaseId());
        if (pet) {
            CPetWarehouseSP warehouse = pet->GetWarehouse();
            if (warehouse) {
                seized = warehouse->SeizeItem(option, itemId, destinationWarehouse(1004, 5), 1);
            }
        }
    }

    if (!seized) {
        socket->Send("cdd", 53, 0, reason);
        return false;
    }

    socket->Send("cd", 53, 1);
    const std::wstring auditText = Utf16ToWide(auditTextUtf16.data());
    WriteAdminSeizeItemAudit(
        ownerId, baseAttributeLog, variationLog, option, originalWarehouse, itemType, 0, auditText.c_str(), ownerName,
        accountName, itemId
    );
    return false;
}

bool PrintAllAccountData(CAdminSocket* socket, const std::uint8_t*) {
    // Current Cached.exe 0x00412720, AQ 41. The current packet body is not
    // read. The handler walks the live CUserDb only after the admin gate.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 41, 0, 0x1e);
        return false;
    }
    g_userDb.PrintAllAccountData();
    socket->Send("cd", 41, 1);
    return false;
}

bool PrintAllItemData(CAdminSocket* socket, const std::uint8_t*) {
    // Current Cached.exe 0x00412814, AQ 42. Despite the handler name, the
    // target asks every resident CUser to create warehouses 0 and 1.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 42, 0, 0x1e);
        return false;
    }
    g_userDb.PrintAllAccountWarehouseData();
    socket->Send("cd", 42, 1);
    return false;
}

bool CopyCharPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00438e80, AQ 43, codec dSdSS. The builder flag is
    // resolved from the packet account name. CopyChar then reloads the target
    // account under the same account lock regardless of its return value.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 43, 0, 0x1e);
        return false;
    }

    int sourceCharacterId = 0;
    std::array<char16_t, 128> newCharacterNameUtf16{};
    int accountId = 0;
    std::array<char16_t, 128> accountNameUtf16{};
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(
        packet, "dSdSS", &sourceCharacterId, 0x100, newCharacterNameUtf16.data(), &accountId, 0x100, accountNameUtf16.data(),
        0x200, auditTextUtf16.data()
    );

    const std::wstring newCharacterName = Utf16ToWide(newCharacterNameUtf16.data());
    std::wstring lowercaseName = newCharacterName;
    Utils::CopyStrInLowerCase(newCharacterName.c_str(), lowercaseName.data());
    if (!CUser::CheckCharacterName(lowercaseName.c_str(), GetCacheDConfiguration().country, false)) {
        socket->Send("cdd", 43, 0, 3);
        return false;
    }

    CUserSP sourceUser = g_userDb.GetUser(sourceCharacterId, true, false, runtime::process::CurrentDatabaseId());
    if (!sourceUser) {
        socket->Send("cdd", 43, 0, 4);
        return false;
    }

    CUserSP existingUser = g_userDb.GetUser(WideToUtf16(lowercaseName.c_str()).c_str(), false);
    if (existingUser) {
        socket->Send("cdd", 43, 0, 0x0c);
        return false;
    }

    CAccountSP account = CAccount::Load(static_cast<unsigned int>(accountId));
    if (!account) {
        g_userDb.DeleteChar(sourceUser.get(), false);
        socket->Send("cdd", 43, 0, 6);
        return false;
    }
    if (account->GetCharCount() >= 7) {
        socket->Send("cdd", 43, 0, 8);
        return false;
    }

    int builder = 0;
    std::wstring accountName = Utf16ToWide(accountNameUtf16.data());
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&builder);
    constexpr const wchar_t* builderStatement = L"{Call lin_GetBuilderAccount (? )}";
    connection.Prepare(builderStatement);
    connection.BindParameter(accountName.c_str());
    // if (!connection.ExecutePrepared(builderStatement) || !connection.Fetch()) {
    const bool builderQuerySucceeded = connection.ExecutePrepared(builderStatement);
    if (!builderQuerySucceeded || !connection.Fetch()) {
        builder = 0;
    }
    if (!builderQuerySucceeded) {
        // Log.Add(LOG_WARN,
        //     "[admin creation] Character copying for account %d continues with builder level zero because the builder-account query failed.",
        //     accountId)
        L2_LOG_WARN(Log,
            "[admin creation] Character copying for account %d continues with builder level zero because the builder-account query failed.",
            accountId);
    }

    const bool copied = g_userDb.CopyChar(
        static_cast<int>(sourceUser->GetId()), builder, lowercaseName.data(), accountName.data(), accountId
    );
    {
        L2_WRITE_GUARD(*account.operator->());
        CAccount::Reload(static_cast<unsigned int>(accountId));
    }

    if (!copied) {
        socket->Send("cdd", 43, 0, 1);
        return false;
    }
    socket->Send("cd", 43, 1);
    return false;
}

bool CreateCharacterPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0043c2f4, AQ 65. Unlike the primary L2Server
    // creation request, this administrative path permits a missing resident
    // CAccount object and attaches the new CUser only when one was loaded.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 65, 0, 0x1e);
        return false;
    }

    std::array<char16_t, 24> characterNameUtf16{};
    std::array<char16_t, 24> accountNameUtf16{};
    unsigned int accountId = 0;
    int gender = 0;
    int race = 0;
    int characterClass = 0;
    int worldId = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    double hp = 0.0;
    double mp = 0.0;
    std::uint64_t spBits = 0;
    std::uint64_t experienceBits = 0;
    int level = 1;
    int alignment = 0;
    int pkCount = 0;
    unsigned int duelCount = 0;
    unsigned int pkPardon = 0;
    int faceIndex = 0;
    int hairShapeIndex = 0;
    int hairColorIndex = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(
        packet, "SSddddddddffQQddddddddS", 0x30, characterNameUtf16.data(), 0x30, accountNameUtf16.data(), &accountId,
        &gender, &race, &characterClass, &worldId, &x, &y, &z, &hp, &mp, &spBits, &experienceBits, &level, &alignment,
        &pkCount, &duelCount, &pkPardon, &faceIndex, &hairShapeIndex, &hairColorIndex, 0x200, auditTextUtf16.data()
    );

    std::wstring characterName = Utf16ToWide(characterNameUtf16.data());
    if (!CUser::CheckCharacterName(characterName.c_str(), GetCacheDConfiguration().country, false)) {
        socket->Send("cdd", 65, 0, 3);
        return false;
    }

    CAccountSP account = CAccount::Load(accountId);
    if (account && account->GetCharCount() >= 7) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[adminsocket.cpp][5295] can not create char. too many chars "
        //     "[%d] account name[%ls]",
        //     account->GetCharCount(), account->GetName()
        // )
        // Log.Add(
        //     LOG_WARN,
        //     "[admin] Character creation rejected for account %u (%s): %d characters already present, limit 7 (command 65).",
        //     accountId, WideToUtf8(account->GetName()).c_str(), account->GetCharCount()
        // )
        // L2_LOG_WARN(Log,
        //     "[admin] Character creation rejected for account %u (%s): %d characters already present, limit 7 (command 65).",
        //     accountId, WideToUtf8(account->GetName()).c_str(), account->GetCharCount()
        // )
        L2_LOG_WARN(Log,
            "[admin] Character creation rejected for account %u (%s): %d characters already present, limit 7 (command 65).",
            accountId, Utf16ToUtf8(account->GetName()).c_str(), account->GetCharCount()
        );
        socket->Send("cdd", 65, 0, 8);
        return false;
    }

    std::wstring accountName = Utf16ToWide(accountNameUtf16.data());
    int builder = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&builder);
    constexpr const wchar_t* builderStatement = L"{Call lin_GetBuilderAccount (? )}";
    connection.Prepare(builderStatement);
    connection.BindParameter(accountName.c_str());
    // if (!connection.ExecutePrepared(builderStatement) || !connection.Fetch()) {
    const bool builderQuerySucceeded = connection.ExecutePrepared(builderStatement);
    if (!builderQuerySucceeded || !connection.Fetch()) {
        builder = 0;
    }
    if (!builderQuerySucceeded) {
        // Log.Add(LOG_WARN,
        //     "[admin creation] Character creation (command 65) for account %u continues with builder level zero because the builder-account query failed.",
        //     accountId)
        L2_LOG_WARN(Log,
            "[admin creation] Character creation (command 65) for account %u continues with builder level zero because the builder-account query failed.",
            accountId);
    }

    const int characterId = g_userDb.CreateUser(
        characterName.data(), accountName.data(), accountId, 0, builder, gender, race, characterClass, worldId, x, y, z, hp,
        mp, runtime::Int64FromBits(spBits), runtime::Int64FromBits(experienceBits), level, alignment, pkCount, duelCount, pkPardon,
        faceIndex, hairShapeIndex, hairColorIndex, nullptr
    );
    if (characterId == 0) {
        socket->Send("cdd", 65, 0, 0x1f);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 65, 0, 0x1f);
        return false;
    }
    if (account)
        account->SetCharacter(user.get());
    socket->Send("cdd", 65, 1, static_cast<int>(user->GetId()));
    return false;
}

bool CreateCharacter2Packet(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0043ccf0, AQ 93. The target omits
    // CUser::CheckCharacterName here and creates the requested subjob state
    // through CUserDb's 28-argument overload.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 93, 0, 0x1e);
        return false;
    }

    std::array<char16_t, 24> characterNameUtf16{};
    std::array<char16_t, 24> accountNameUtf16{};
    unsigned int accountId = 0;
    int gender = 0;
    int race = 0;
    int characterClass = 0;
    int worldId = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    double hp = 0.0;
    double mp = 0.0;
    std::uint64_t spBits = 0;
    std::uint64_t experienceBits = 0;
    int level = 1;
    int alignment = 0;
    int pkCount = 0;
    unsigned int duelCount = 0;
    unsigned int pkPardon = 0;
    int faceIndex = 0;
    int hairShapeIndex = 0;
    int hairColorIndex = 0;
    int subjobId = 0;
    int mainClass = 0;
    int dualClassId = -1;
    double cp = 0.0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(
        packet, "SSddddddddffQQdddddddddddfS", 0x30, characterNameUtf16.data(), 0x30, accountNameUtf16.data(), &accountId,
        &gender, &race, &characterClass, &worldId, &x, &y, &z, &hp, &mp, &spBits, &experienceBits, &level, &alignment,
        &pkCount, &duelCount, &pkPardon, &faceIndex, &hairShapeIndex, &hairColorIndex, &subjobId, &mainClass, &dualClassId,
        &cp, 0x200, auditTextUtf16.data()
    );

    if (race == 6 && subjobId != 0 && subjobId != 1) {
        socket->Send("cdd", 93, 0, 0x35);
        return false;
    }

    CAccountSP account = CAccount::Load(accountId);
    if (account && account->GetCharCount() >= 7) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[adminsocket.cpp][7139] can not create char. too many chars "
        //     "[%d] account name[%ls]",
        //     account->GetCharCount(), account->GetName()
        // )
        // Log.Add(
        //     LOG_WARN,
        //     "[admin] Character creation rejected for account %u (%s): %d characters already present, limit 7 (command 93).",
        //     accountId, WideToUtf8(account->GetName()).c_str(), account->GetCharCount()
        // )
        // L2_LOG_WARN(Log,
        //     "[admin] Character creation rejected for account %u (%s): %d characters already present, limit 7 (command 93).",
        //     accountId, WideToUtf8(account->GetName()).c_str(), account->GetCharCount()
        // )
        L2_LOG_WARN(Log,
            "[admin] Character creation rejected for account %u (%s): %d characters already present, limit 7 (command 93).",
            accountId, Utf16ToUtf8(account->GetName()).c_str(), account->GetCharCount()
        );
        socket->Send("cdd", 93, 0, 8);
        return false;
    }

    std::wstring characterName = Utf16ToWide(characterNameUtf16.data());
    std::wstring accountName = Utf16ToWide(accountNameUtf16.data());
    int builder = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&builder);
    constexpr const wchar_t* builderStatement = L"{Call lin_GetBuilderAccount (? )}";
    connection.Prepare(builderStatement);
    connection.BindParameter(accountName.c_str());
    // if (!connection.ExecutePrepared(builderStatement) || !connection.Fetch()) {
    const bool builderQuerySucceeded = connection.ExecutePrepared(builderStatement);
    if (!builderQuerySucceeded || !connection.Fetch()) {
        builder = 0;
    }
    if (!builderQuerySucceeded) {
        // Log.Add(LOG_WARN,
        //     "[admin creation] Character creation (command 93) for account %u continues with builder level zero because the builder-account query failed.",
        //     accountId)
        L2_LOG_WARN(Log,
            "[admin creation] Character creation (command 93) for account %u continues with builder level zero because the builder-account query failed.",
            accountId);
    }

    const std::int64_t sp = runtime::Int64FromBits(spBits);
    const std::int64_t experience = runtime::Int64FromBits(experienceBits);
    const int characterId = g_userDb.CreateUser(
        characterName.data(), accountName.data(), accountId, 0, builder, gender, race, characterClass, worldId, x, y, z, hp,
        mp, sp, experience, level, alignment, pkCount, duelCount, pkPardon, faceIndex, hairShapeIndex, hairColorIndex,
        subjobId, mainClass, dualClassId, cp
    );
    if (characterId == 0) {
        socket->Send("cdd", 93, 0, 0x1f);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 93, 0, 0x1f);
        return false;
    }
    if (account)
        account->SetCharacter(user.get());
    socket->Send("cdd", 93, 1, static_cast<int>(user->GetId()));
    WriteAdminCreateCharacter2Audit(
        *user.get(), builder, gender, race, characterClass, hp, mp, sp, experience, level, subjobId, auditTextUtf16.data()
    );
    return false;
}

bool SetInstantAnnouncePacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00412c70, AQ 70, codec SS. The first UTF-16
    // string is limited to 100 bytes and forwarded through server opcode
    // 0x94/subcode 7. The second is the administrative audit identity.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 70, 0, 0x1e);
        return false;
    }

    std::array<char16_t, 50> messageUtf16{};
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "SS", 100, messageUtf16.data(), 0x200, auditTextUtf16.data());

    CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (server != nullptr) {
        server->Send("cdS", 0x94, 7, messageUtf16.data());

        std::u16string sanitizedMessage(messageUtf16.data());
        for (char16_t& character : sanitizedMessage) {
            if (character == u',')
                character = u' ';
        }
        std::array<std::u16string, 26> fields{};
        fields[0] = runtime::DecimalUtf16(0x209);
        fields[1] = runtime::DecimalUtf16(0);
        fields[2] = runtime::DecimalUtf16(0);
        fields[5] = runtime::DecimalUtf16(0);
        fields[6] = runtime::DecimalUtf16(0);
        fields[7] = runtime::DecimalUtf16(0);
        fields[8] = sanitizedMessage;
        fields[21] = auditTextUtf16.data();
        fields[22] = auditTextUtf16.data();
        runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    } else {
        // Log.Add(
        //     LOG_WARN,
        //     "[admin] Instant announcement was not forwarded: L2Server is unavailable. No announcement audit record is written. This command still replies with success."
        // )
        L2_LOG_WARN(Log,
            "[admin] Instant announcement was not forwarded: L2Server is unavailable. No announcement audit record is written; this command still replies with success."
        );
    }

    socket->Send("cd", 70, 1);
    return false;
}

bool SetSocialityPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0041decc, AQ 69, codec ddS. The target permits
    // only an offline CUser and writes audit record 0x283 after the locked
    // owner call succeeds.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 69, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int sociality = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddS", &characterId, &sociality, 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 69, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 69, 0, 7);
        return false;
    }
    if (!user->SetSocialityWithLock(sociality)) {
        socket->Send("cdd", 69, 0, 1);
        return false;
    }

    socket->Send("cd", 69, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x283);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[13] = runtime::DecimalUtf16(sociality);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool DelHennaPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0041e308, AQ 71, codec ddS.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 71, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int hennaId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddS", &characterId, &hennaId, 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 71, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 71, 0, 7);
        return false;
    }

    // The target ignores DelHenna's boolean result.
    user->DelHenna(hennaId);
    socket->Send("cd", 71, 1);

    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x284);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    // The shipping CLog call underfills three coordinate varargs. Preserve
    // the record's confirmed CUser coordinate slots deterministically rather
    // than reading unrelated host stack bytes.
    fields[5] = runtime::DecimalUtf16(user->GetX());
    fields[6] = runtime::DecimalUtf16(user->GetY());
    fields[7] = runtime::DecimalUtf16(user->GetZ());
    fields[13] = runtime::DecimalUtf16(hennaId);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool AddHennaPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0041e6f8, AQ 72. It is structurally paired with
    // AQ 71 but keeps its distinct audit ID.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 72, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int hennaId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddS", &characterId, &hennaId, 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 72, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 72, 0, 7);
        return false;
    }

    user->AddHenna(hennaId);
    socket->Send("cd", 72, 1);

    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x285);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[5] = runtime::DecimalUtf16(user->GetX());
    fields[6] = runtime::DecimalUtf16(user->GetY());
    fields[7] = runtime::DecimalUtf16(user->GetZ());
    fields[13] = runtime::DecimalUtf16(hennaId);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool AddHennasPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0041eae8, AQ 74. The counted stream is a flat
    // sequence: every fourth dword selects a subjob and each following
    // dword is added through CUser::AddHenna(int,int).
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 74, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int valueCount = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    const std::uint8_t* cursor = ::Disassemble(packet, "dSd", &characterId, 0x200, auditTextUtf16.data(), &valueCount);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 74, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 74, 0, 7);
        return false;
    }

    int subjobId = 0;
    for (int index = 0; index < valueCount; ++index) {
        int value = 0;
        cursor = ::Disassemble(cursor, "d", &value);
        if (index % 4 == 0) {
            subjobId = value;
            continue;
        }

        // The current body ignores every individual mutation result and
        // still reports aggregate success after the declared count.
        user->AddHenna(value, subjobId);

        std::array<std::u16string, 26> fields{};
        fields[0] = runtime::DecimalUtf16(0x285);
        fields[1] = runtime::DecimalUtf16(0);
        fields[2] = runtime::DecimalUtf16(0);
        fields[3] = runtime::DecimalUtf16(user->GetId());
        fields[4] = runtime::DecimalUtf16(user->GetAccountID());
        fields[5] = runtime::DecimalUtf16(user->GetX());
        fields[6] = runtime::DecimalUtf16(user->GetY());
        fields[7] = runtime::DecimalUtf16(user->GetZ());
        fields[13] = runtime::DecimalUtf16(value);
        fields[21] = auditTextUtf16.data();
        fields[22] = auditTextUtf16.data();
        fields[23] = AdminLogText(user->GetCharName());
        fields[24] = AdminLogText(user->GetAccountName());
        runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    }

    socket->Send("cd", 74, 1);
    return false;
}

bool ManageUserNameReservedPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0043ca28, AQ 73, codec dSddS. The target's reject
    // branch anomalously replies with opcode 72. All later branches use 73.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 72, 0, 0x1e);
        return false;
    }

    int option = 0;
    std::array<char16_t, 50> characterNameUtf16{};
    int accountId = 0;
    int used = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dSddS", &option, 100, characterNameUtf16.data(), &accountId, &used, 0x200, auditTextUtf16.data());

    const std::wstring characterName = Utf16ToWide(characterNameUtf16.data());
    if (!CUser::CheckCharacterName(characterName.c_str(), GetCacheDConfiguration().country, false)) {
        socket->Send("cdd", 73, 0, 3);
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_ManageUserNameReserved (?, ?, ?, ? )}";
    connection.Prepare(statement);
    connection.BindParameter(&option);
    connection.BindParameter(characterName.c_str());
    connection.BindParameter(&accountId);
    connection.BindParameter(&used);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR,
        //     "[admin names] Name-reservation query failed for account %d, option %d, used flag %d. Sending failure reason 1.",
        //     accountId, option, used)
        L2_LOG_ERROR(Log,
            "[admin names] Name-reservation query failed for account %d, option %d, used flag %d; sending failure reason 1.",
            accountId, option, used);
        socket->Send("cdd", 73, 0, 1);
        return false;
    }

    socket->Send("cd", 73, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x286);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(0);
    fields[4] = runtime::DecimalUtf16(0);
    fields[8] = characterNameUtf16.data();
    fields[13] = runtime::DecimalUtf16(accountId);
    fields[14] = runtime::DecimalUtf16(used);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool GetCastleListPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00412ed0, AQ 75, codec S. The current ten-column
    // ABI and the unanimous compatible donor body fix the castle, pledge,
    // ruler, alliance, tax and next-war row without an admin-reject gate.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(packet, "S", 0x200, ignoredAuditTextUtf16.data());

    int castleId = 0;
    int pledgeId = 0;
    std::array<char16_t, 24> pledgeName{};
    int rulerId = 0;
    std::array<char16_t, 50> rulerName{};
    int allianceId = 0;
    std::array<char16_t, 50> allianceName{};
    int taxRate = 0;
    int nextTaxRate = 0;
    int nextWarTime = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&castleId);
    connection.Bind(&pledgeId);
    connection.Bind(pledgeName.data(), 0x30);
    connection.Bind(&rulerId);
    connection.Bind(rulerName.data(), 100);
    connection.Bind(&allianceId);
    connection.Bind(allianceName.data(), 100);
    connection.Bind(&taxRate);
    connection.Bind(&nextTaxRate);
    connection.Bind(&nextWarTime);

    constexpr const wchar_t* statement = L"{Call lin_GetCastleList}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot load the castle list: database operation failed. Returning a zero-count reply."
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot load the castle list: database operation failed; returning a zero-count reply."
        );
        socket->Send("cd", 75, 0);
        return false;
    }

    std::array<char, 0x5000> rows{};
    int rowBytes = 0;
    int rowCount = 0;
    bool rowAssemblyFailureReported = false;
    while (connection.Fetch()) {
        const std::u16string targetPledgeName = std::u16string(pledgeName.data());
        const std::u16string targetRulerName = std::u16string(rulerName.data());
        const std::u16string targetAllianceName = std::u16string(allianceName.data());
        // rowBytes += ::Assemble(
        //     rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "ddSdSdSddd", castleId, pledgeId,
        //     targetPledgeName.c_str(), rulerId, targetRulerName.c_str(), allianceId, targetAllianceName.c_str(), taxRate,
        //     nextTaxRate, nextWarTime
        // )
        const int assembledRowBytes = ::Assemble(
            rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "ddSdSdSddd", castleId, pledgeId,
            targetPledgeName.c_str(), rulerId, targetRulerName.c_str(), allianceId, targetAllianceName.c_str(), taxRate,
            nextTaxRate, nextWarTime
        );
        rowBytes += assembledRowBytes;
        if (assembledRowBytes <= 0 && !rowAssemblyFailureReported) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[admin] Cannot assemble castle list response entry %lld, castle %d (assembler result %d, accumulated %d bytes in the 20480-byte row buffer). Processing continues and the advertised count still includes failed entries.",
            //     static_cast<long long>(rowCount) + 1, castleId, assembledRowBytes, rowBytes
            // )
            L2_LOG_ERROR(Log,
                "[admin] Cannot assemble castle list response entry %lld, castle %d (assembler result %d, accumulated %d bytes in the 20480-byte row buffer). Processing continues and the advertised count still includes failed entries.",
                static_cast<long long>(rowCount) + 1, castleId, assembledRowBytes, rowBytes
            );
            rowAssemblyFailureReported = true;
        }
        ++rowCount;
    }

    socket->Send("cdb", 75, rowCount, rowBytes, rows.data());
    return false;
}

bool ModifyCharPropertyPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0041ef94, AQ 76, codec dddddddddS. The wire puts
    // subjob ID before job group. The CUser wrapper receives them reversed.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 76, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::uint32_t gender = 0;
    std::uint32_t race = 0;
    std::uint32_t characterClass = 0;
    std::uint32_t face = 0;
    std::uint32_t hairShape = 0;
    std::uint32_t hairColor = 0;
    std::uint32_t subjobId = 0;
    std::uint32_t jobGroup = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(
        packet, "dddddddddS", &characterId, &gender, &race, &characterClass, &face, &hairShape, &hairColor, &subjobId,
        &jobGroup, 0x200, auditTextUtf16.data()
    );
    if (gender > 0x10000U || race > 0x10000U || characterClass > 0x10000U || face > 0x10000U || hairShape > 0x10000U ||
        hairColor > 0x10000U || subjobId > 10U || jobGroup > 1U) {
        socket->Send("cdd", 76, 0, 3);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 76, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 76, 0, 7);
        return false;
    }

    if (!user->ModChar2Extern(
            static_cast<int>(gender), static_cast<int>(race), static_cast<int>(characterClass), static_cast<int>(face),
            static_cast<int>(hairShape), static_cast<int>(hairColor), static_cast<int>(jobGroup), static_cast<int>(subjobId)
        )) {
        socket->Send("cdd", 76, 0, 1);
        return false;
    }

    socket->Send("cd", 76, 1);
    const std::u16string characterName = AdminLogText(user->GetCharName());
    const std::u16string accountName = AdminLogText(user->GetAccountName());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x287, 0, 0, user->GetId(), user->GetAccountID(), 0, 0, 0, nullptr, nullptr, nullptr, gender, race, characterClass,
        face, hairShape, hairColor, subjobId, jobGroup, 0, 0, auditTextUtf16.data(), auditTextUtf16.data(),
        characterName.c_str(), accountName.c_str(), 0
    );
    return false;
}

bool ModifyCharAbilityDeltaPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0041f670, AQ 77. The six dwords after SP/EXP are
    // alignment, PK, PK-pardon, duel, subjob ID and AP delta in that order.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 77, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::int64_t spDelta = 0;
    std::int64_t experienceDelta = 0;
    int alignmentDelta = 0;
    int pkCountDelta = 0;
    int pkPardonDelta = 0;
    int duelCountDelta = 0;
    int subjobId = 0;
    int abilityPointDelta = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(
        packet, "dQQddddddS", &characterId, &spDelta, &experienceDelta, &alignmentDelta, &pkCountDelta, &pkPardonDelta,
        &duelCountDelta, &subjobId, &abilityPointDelta, 0x200, auditTextUtf16.data()
    );

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 77, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 77, 0, 7);
        return false;
    }

    if (!user->ModChar3Extern(
            spDelta, experienceDelta, alignmentDelta, pkCountDelta, pkPardonDelta, duelCountDelta, subjobId,
            abilityPointDelta
        )) {
        socket->Send("cdd", 77, 0, 1);
        return false;
    }

    socket->Send("cd", 77, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x288);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[11] = runtime::DecimalUtf16(spDelta);
    fields[12] = runtime::DecimalUtf16(experienceDelta);
    fields[13] = runtime::DecimalUtf16(alignmentDelta);
    fields[14] = runtime::DecimalUtf16(pkCountDelta);
    fields[15] = runtime::DecimalUtf16(pkPardonDelta);
    fields[16] = runtime::DecimalUtf16(duelCountDelta);
    fields[17] = runtime::DecimalUtf16(subjobId);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool ModifyCharAbilityPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0041fb54, AQ 78. It shares the wire layout with
    // AQ 77 but selects the absolute CUser owner and record 0x289.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 78, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::int64_t sp = 0;
    std::int64_t experience = 0;
    int alignment = 0;
    int pkCount = 0;
    std::uint32_t pkPardon = 0;
    std::uint32_t duelCount = 0;
    int subjobId = 0;
    int abilityPoints = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(
        packet, "dQQddddddS", &characterId, &sp, &experience, &alignment, &pkCount, &pkPardon, &duelCount, &subjobId,
        &abilityPoints, 0x200, auditTextUtf16.data()
    );

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 78, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 78, 0, 7);
        return false;
    }

    if (!user->ModCharExtern(sp, experience, alignment, pkCount, pkPardon, duelCount, subjobId, abilityPoints)) {
        socket->Send("cdd", 78, 0, 1);
        return false;
    }

    socket->Send("cd", 78, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x289);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[11] = runtime::DecimalUtf16(sp);
    fields[12] = runtime::DecimalUtf16(experience);
    fields[13] = runtime::DecimalUtf16(alignment);
    fields[14] = runtime::DecimalUtf16(pkCount);
    fields[15] = runtime::DecimalUtf16(pkPardon);
    fields[16] = runtime::DecimalUtf16(duelCount);
    fields[17] = runtime::DecimalUtf16(subjobId);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool DelHenna2Packet(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00420038, AQ 79, codec dddS.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 79, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int hennaId = 0;
    int subjobId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dddS", &characterId, &hennaId, &subjobId, 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 79, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 79, 0, 7);
        return false;
    }

    user->DelHenna(hennaId, subjobId);
    socket->Send("cd", 79, 1);

    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x28a);
    fields[1] = runtime::DecimalUtf16(user->GetId());
    fields[2] = runtime::DecimalUtf16(user->GetAccountID());
    fields[13] = runtime::DecimalUtf16(hennaId);
    fields[14] = runtime::DecimalUtf16(subjobId);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool AddHenna2Packet(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00420440, AQ 80. Like AQ 79, the wrapper result
    // is ignored before the unconditional success reply and audit record.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 80, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int hennaId = 0;
    int subjobId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dddS", &characterId, &hennaId, &subjobId, 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 80, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 80, 0, 7);
        return false;
    }

    user->AddHenna(hennaId, subjobId);
    socket->Send("cd", 80, 1);

    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x28b);
    fields[1] = runtime::DecimalUtf16(user->GetId());
    fields[2] = runtime::DecimalUtf16(user->GetAccountID());
    fields[13] = runtime::DecimalUtf16(hennaId);
    fields[14] = runtime::DecimalUtf16(subjobId);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool AddSkill2Packet(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00420848, AQ 81, codec ddddS.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 81, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::uint32_t skillId = 0;
    int packedSkillLevel = 0;
    int subjobId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddddS", &characterId, &skillId, &packedSkillLevel, &subjobId, 0x200, auditTextUtf16.data());

    if (skillId > 0x10000U) {
        socket->Send("cdd", 81, 0, 3);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 81, 0, 4);
        return false;
    }

    const std::uint32_t packedBits = static_cast<std::uint32_t>(packedSkillLevel);
    const int skillLevel = static_cast<int>(packedBits & 0xffffU);
    int skillSubLevel = static_cast<int>((packedBits >> 16) & 0xffffU);
    if (skillSubLevel >= 0x8000) {
        skillSubLevel -= 0x10000;
    }

    const bool updated = subjobId == user->GetSubjobId()
        ? SetAdminAcquireSkillAndNotifyAdd(user, static_cast<int>(skillId), skillLevel, skillSubLevel)
        : user->SetSubJobAcquireSkill(subjobId, static_cast<int>(skillId), skillLevel, skillSubLevel);
    if (!updated) {
        socket->Send("cdd", 81, 0, 1);
        return false;
    }

    socket->Send("cd", 81, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x28c);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[11] = runtime::DecimalUtf16(skillId);
    fields[12] = runtime::DecimalUtf16(packedSkillLevel);
    fields[13] = runtime::DecimalUtf16(subjobId);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool DelSkill2Packet(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00420c9c, AQ 82, codec dddS.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 82, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::uint32_t skillId = 0;
    int subjobId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dddS", &characterId, &skillId, &subjobId, 0x200, auditTextUtf16.data());

    if (skillId > 0x10000U) {
        // Shipping branch 0x004211ce replies with the legacy AQ 7 opcode.
        socket->Send("cdd", 7, 0, 3);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 82, 0, 4);
        return false;
    }

    const int signedSkillId = static_cast<int>(skillId);
    const int oldPackedSkillLevel = user->GetAcquireSkillLevelSet(signedSkillId);
    bool deleted = false;
    if (subjobId == user->GetSubjobId()) {
        deleted = user->DeleteAquireSkill(signedSkillId);
        if (deleted && user->IsLoggedIn()) {
            CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
            if (server != nullptr) {
                server->Send("cdddd", 0x22, user->GetId(), 2, signedSkillId, 0);
            }
        }
    } else {
        deleted = user->DeleteSubJobSkill(subjobId, signedSkillId);
    }
    if (!deleted) {
        socket->Send("cdd", 82, 0, 1);
        return false;
    }

    socket->Send("cd", 82, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x28d);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[11] = runtime::DecimalUtf16(skillId);
    fields[12] = runtime::DecimalUtf16(oldPackedSkillLevel);
    fields[13] = runtime::DecimalUtf16(subjobId);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool ModifySkill2Packet(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00421220, AQ 83, codec ddddS.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 83, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::uint32_t skillId = 0;
    int packedSkillLevel = 0;
    int subjobId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddddS", &characterId, &skillId, &packedSkillLevel, &subjobId, 0x200, auditTextUtf16.data());

    if (skillId > 0x10000U) {
        // Shipping branch 0x004216d8 replies with the legacy AQ 8 opcode.
        socket->Send("cdd", 8, 0, 3);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 83, 0, 4);
        return false;
    }

    const std::uint32_t packedBits = static_cast<std::uint32_t>(packedSkillLevel);
    const int skillLevel = static_cast<int>(packedBits & 0xffffU);
    int skillSubLevel = static_cast<int>((packedBits >> 16) & 0xffffU);
    if (skillSubLevel >= 0x8000) {
        skillSubLevel -= 0x10000;
    }

    bool updated = false;
    if (subjobId == user->GetSubjobId()) {
        updated = user->SetAquireSkill(static_cast<int>(skillId), skillLevel, skillSubLevel, true, -1);
        if (updated && user->IsLoggedIn()) {
            CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
            if (server != nullptr) {
                server->Send("cdddd", 0x22, user->GetId(), 3, static_cast<int>(skillId), packedSkillLevel);
            }
        }
    } else {
        updated = user->SetSubJobAcquireSkill(subjobId, static_cast<int>(skillId), skillLevel, skillSubLevel);
    }
    if (!updated) {
        socket->Send("cdd", 83, 0, 1);
        return false;
    }

    socket->Send("cd", 83, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x28e);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[11] = runtime::DecimalUtf16(skillId);
    fields[12] = runtime::DecimalUtf16(packedSkillLevel);
    fields[13] = runtime::DecimalUtf16(subjobId);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool AddMacroPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0042172c, AQ 84, codec dSSSdS.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 84, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::array<char16_t, 128> macroNameUtf16{};
    std::array<char16_t, 128> macroTooltipUtf16{};
    std::array<char16_t, 128> macroIconNameUtf16{};
    int macroIconType = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(
        packet, "dSSSdS", &characterId, 0x100, macroNameUtf16.data(), 0x100, macroTooltipUtf16.data(), 0x100,
        macroIconNameUtf16.data(), &macroIconType, 0x200, auditTextUtf16.data()
    );

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 84, 0, 4);
        return false;
    }
    if (user->m_loggedIn) {
        socket->Send("cdd", 84, 0, 7);
        return false;
    }

    std::wstring macroName = Utf16ToWide(macroNameUtf16.data());
    std::wstring macroTooltip = Utf16ToWide(macroTooltipUtf16.data());
    std::wstring macroIconName = Utf16ToWide(macroIconNameUtf16.data());
    int macroId = 0;
    user->AddUserMacro(characterId, macroName.data(), macroTooltip.data(), macroIconName.data(), macroIconType, &macroId);
    socket->Send("cdd", 84, 1, macroId);

    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x295);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[8] = macroNameUtf16.data();
    fields[9] = macroTooltipUtf16.data();
    fields[10] = macroIconNameUtf16.data();
    fields[11] = runtime::DecimalUtf16(macroId);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool ModifyDepositedSSQItemPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00422044, AQ 87, codec dddQS. This handler has no
    // admin-reject gate. It updates the offline user's resident SSQ state and,
    // only for the user's current side and round, the process-wide collected
    // point followed by the L2Server notification.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int partValue = 0;
    int itemClassId = 0;
    std::int64_t amount = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dddQS", &characterId, &partValue, &itemClassId, &amount, 0x200, auditTextUtf16.data());

    if (partValue != SSQPart::TWILIGHT && partValue != SSQPart::DAWN) {
        socket->Send("cdd", 87, 0, 3);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 87, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 87, 0, 7);
        return false;
    }
    if (!user->m_ssqInfoLoaded) {
        socket->Send("cdd", 87, 0, 0x20);
        return false;
    }

    SSQItem::type itemType = SSQItem::TYPE_A;
    std::int64_t pointDelta = amount;
    switch (itemClassId) {
    // case 0x18d8:
    case BLUE_SEAL_STONE_CLASS_ID:
        itemType = SSQItem::TYPE_A;
        break;
    // case 0x18d9:
    case GREEN_SEAL_STONE_CLASS_ID:
        itemType = SSQItem::TYPE_B;
        break;
    // case 0x18da:
    case RED_SEAL_STONE_CLASS_ID:
        itemType = SSQItem::TYPE_C;
        pointDelta = runtime::MultiplyInt64Wrapping(amount, 10);
        break;
    default:
        socket->Send("cdd", 87, 0, 3);
        return false;
    }

    const SSQPart::type part = static_cast<SSQPart::type>(partValue);
    const std::size_t itemIndex = static_cast<std::size_t>(itemType);
    const std::int64_t previousItemCount =
        part == SSQPart::TWILIGHT ? user->m_ssqInfo.twilightItemCount[itemIndex] : user->m_ssqInfo.dawnItemCount[itemIndex];
    const std::int64_t updatedItemCount = runtime::AddInt64Wrapping(previousItemCount, amount);
    if (updatedItemCount < 0) {
        socket->Send("cdd", 87, 0, 3);
        return false;
    }

    const std::int64_t previousUserPoint = user->m_ssqInfo.point;
    const std::int64_t updatedUserPoint = runtime::AddInt64Wrapping(previousUserPoint, pointDelta);
    if (updatedUserPoint < 0) {
        socket->Send("cdd", 87, 0, 3);
        return false;
    }

    {
        L2_WRITE_GUARD(*user.operator->());
        user->SetDepositedSSQItemCount(part, itemType, updatedItemCount);
        user->SetSSQPoint(updatedUserPoint);
        user->SaveSSQInfo();
    }

    if (user->IsCurrentSSQRound() && user->m_ssqInfo.part == partValue) {
        SevenSignQuestDB& sevenSignQuestDb = SevenSignQuestDB::Instance();
        L2_WRITE_GUARD(sevenSignQuestDb);
        const std::int64_t collectedPoint = runtime::AddInt64Wrapping(sevenSignQuestDb.m_collectedPoint[partValue], pointDelta);
        sevenSignQuestDb.SetCollectedPoint(part, collectedPoint);
        sevenSignQuestDb.SaveJoinInfo(part);
    }

    CSocket* const serverSocket = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (serverSocket) {
        if (user->IsCurrentSSQRound() && user->m_ssqInfo.part == partValue) {
            const int wirePointDelta = runtime::Int32FromBits(static_cast<std::uint32_t>(pointDelta));
            serverSocket->Send("cddd", 0x94, 8, partValue, wirePointDelta);
        }
    }

    socket->Send("cd", 87, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x290);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[5] = runtime::DecimalUtf16(0);
    fields[6] = runtime::DecimalUtf16(0);
    fields[7] = runtime::DecimalUtf16(0);
    fields[11] = runtime::DecimalUtf16(characterId);
    fields[12] = runtime::DecimalUtf16(itemClassId);
    fields[13] = runtime::DecimalUtf16(partValue);
    fields[14] = runtime::DecimalUtf16(amount);
    fields[15] = runtime::DecimalUtf16(previousItemCount);
    fields[16] = runtime::DecimalUtf16(updatedItemCount);
    fields[17] = runtime::DecimalUtf16(previousUserPoint);
    fields[18] = runtime::DecimalUtf16(updatedUserPoint);
    fields[19] = runtime::DecimalUtf16(user->m_ssqInfo.roundNumber);
    fields[20] = runtime::DecimalUtf16(SevenSignQuestDB::Instance().GetRoundNumber());
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool ChangePetNamePacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00422824, AQ 88, codec ddSS. This handler has no
    // admin-reject gate. It requires an offline user, validates the target
    // name, delegates persistence to CPet::SetName, then marks the collar's
    // directly observed CItem+0x34 field before Save(true).
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int petId = 0;
    std::array<char16_t, 128> petNameUtf16{};
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddSS", &characterId, &petId, 0x100, petNameUtf16.data(), 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 88, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 88, 0, 7);
        return false;
    }

    CPetSP pet = g_petDb.GetPet(petId, runtime::process::CurrentDatabaseId());
    if (!pet) {
        socket->Send("cdd", 88, 0, 0x1a);
        return false;
    }

    const std::wstring petName = Utf16ToWide(petNameUtf16.data());
    if (!CUser::CheckCharacterName(petName.c_str(), GetCacheDConfiguration().country, false)) {
        socket->Send("cdd", 88, 0, 3);
        return false;
    }

    int petNameResult = -1;
    if (!pet->SetName(petName.c_str(), petNameResult)) {
        socket->Send("cdd", 88, 0, 1);
        return false;
    }

    CWareHouseSP inventory = user->GetWareHouse(0, true);
    if (inventory) {
        CItemSP collar = inventory->GetItem(petId);
        if (collar) {
            collar->m_eroded = 1;
            collar->Save(true);
        }
    }

    socket->Send("cd", 88, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x291);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[5] = runtime::DecimalUtf16(0);
    fields[6] = runtime::DecimalUtf16(0);
    fields[7] = runtime::DecimalUtf16(0);
    fields[8] = petNameUtf16.data();
    fields[11] = runtime::DecimalUtf16(characterId);
    fields[12] = runtime::DecimalUtf16(petId);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool AddMacroInfoPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00421bc4, AQ 92, codec ddddddSS.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 92, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int macroId = 0;
    int macroOrder = 0;
    int macroInt1 = 0;
    int macroInt2 = 0;
    int macroInt3 = 0;
    std::array<char16_t, 512> macroTextUtf16{};
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(
        packet, "ddddddSS", &characterId, &macroId, &macroOrder, &macroInt1, &macroInt2, &macroInt3, 0x400,
        macroTextUtf16.data(), 0x200, auditTextUtf16.data()
    );

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 92, 0, 4);
        return false;
    }
    if (user->m_loggedIn) {
        socket->Send("cdd", 92, 0, 7);
        return false;
    }

    std::wstring macroText = Utf16ToWide(macroTextUtf16.data());
    user->AddUserMacroInfo(macroId, macroOrder, macroInt1, macroInt2, macroInt3, macroText.data());
    socket->Send("cd", 92, 1);

    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x296);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[8] = macroTextUtf16.data();
    fields[11] = runtime::DecimalUtf16(macroId);
    fields[12] = runtime::DecimalUtf16(macroOrder);
    fields[13] = runtime::DecimalUtf16(macroInt1);
    fields[14] = runtime::DecimalUtf16(macroInt2);
    fields[15] = runtime::DecimalUtf16(macroInt3);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool ChangeSubJobPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00422df0, AQ 89, codec ddS. Unlike the adjacent
    // administrative mutations this handler has no RejectsAdminRequests gate.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int subjobId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddS", &characterId, &subjobId, 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 89, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 89, 0, 7);
        return false;
    }
    if (!user->ChangeSubJob(subjobId)) {
        socket->Send("cdd", 89, 0, 1);
        return false;
    }

    socket->Send("cd", 89, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x292);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[5] = runtime::DecimalUtf16(0);
    fields[6] = runtime::DecimalUtf16(0);
    fields[7] = runtime::DecimalUtf16(0);
    fields[11] = runtime::DecimalUtf16(characterId);
    fields[12] = runtime::DecimalUtf16(subjobId);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool AddSkills2Packet(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004230e0, AQ 94. Only the final row controls the
    // reply. Earlier SQL failures are not accumulated.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 94, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int rowCount = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    const std::uint8_t* cursor = ::Disassemble(packet, "dSd", &characterId, 0x200, auditTextUtf16.data(), &rowCount);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[adminsocket.cpp][7249]Cannot find user, char id[%d]", characterId)
        // Log.Add(
        //     LOG_WARN,
        //     "[admin] Cannot add %d skill rows to character %d: character unavailable. Returning failure.",
        //     rowCount, characterId
        // )
        L2_LOG_WARN(Log,
            "[admin] Cannot add %d skill rows to character %d: character unavailable; returning failure.",
            rowCount, characterId
        );
        socket->Send("cdd", 94, 0, 4);
        return false;
    }

    const int residentSubjobId = user->GetSubjobId();
    bool lastRowSucceeded = true;
    for (int index = 0; index < rowCount; ++index) {
        int skillId = 0;
        int packedSkillLevel = 0;
        int subjobId = 0;
        cursor = ::Disassemble(cursor, "ddd", &skillId, &packedSkillLevel, &subjobId);

        const std::uint32_t packedBits = static_cast<std::uint32_t>(packedSkillLevel);
        const int skillLevel = static_cast<int>(packedBits & 0xffffU);
        int skillSubLevel = static_cast<int>((packedBits >> 16) & 0xffffU);
        if (skillSubLevel >= 0x8000) {
            skillSubLevel -= 0x10000;
        }

        lastRowSucceeded = subjobId == residentSubjobId
            ? SetAdminAcquireSkillAndNotifyAdd(user, skillId, skillLevel, skillSubLevel)
            : user->SetSubJobAcquireSkill(subjobId, skillId, skillLevel, skillSubLevel);
    }

    if (rowCount > 0 && !lastRowSucceeded) {
        socket->Send("cdd", 94, 0, 1);
        return false;
    }

    socket->Send("cd", 94, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x298);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[11] = runtime::DecimalUtf16(rowCount);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool AddHennas2Packet(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004235bc, AQ 95. Each row is
    // {subjobId, henna1, henna2, henna3}, unlike AQ 74, this body checks all
    // three CUser dispatch results and keeps partial earlier mutations.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 95, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int rowCount = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    const std::uint8_t* cursor = ::Disassemble(packet, "dSd", &characterId, 0x200, auditTextUtf16.data(), &rowCount);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 95, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 95, 0, 7);
        return false;
    }

    for (int index = 0; index < rowCount; ++index) {
        int subjobId = 0;
        int henna1 = 0;
        int henna2 = 0;
        int henna3 = 0;
        cursor = ::Disassemble(cursor, "dddd", &subjobId, &henna1, &henna2, &henna3);
        if (!user->AddHenna(henna1, subjobId) || !user->AddHenna(henna2, subjobId) || !user->AddHenna(henna3, subjobId)) {
            socket->Send("cdd", 95, 0, 1);
            return false;
        }
    }

    socket->Send("cd", 95, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x299);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[11] = runtime::DecimalUtf16(rowCount);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool AddSubjobsPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00423b20, AQ 96. The root codec is dSd and each
    // row is dddQQddddd. As in AQ94, only the final row result controls the
    // failure reply. Earlier successful inserts are not rolled back here.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 96, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int rowCount = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    const std::uint8_t* cursor = ::Disassemble(packet, "dSd", &characterId, 0x200, auditTextUtf16.data(), &rowCount);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 96, 0, 4);
        return false;
    }
    if (user->GetRace() == 6) {
        socket->Send("cdd", 96, 0, 0x35);
        return false;
    }

    bool lastRowSucceeded = true;
    for (int index = 0; index < rowCount; ++index) {
        int newClass = 0;
        int hp = 0;
        int mp = 0;
        std::int64_t sp = 0;
        std::int64_t experience = 0;
        int level = 0;
        int subjobId = 0;
        int henna1 = 0;
        int henna2 = 0;
        int henna3 = 0;
        cursor = ::Disassemble(
            cursor, "dddQQddddd", &newClass, &hp, &mp, &sp, &experience, &level, &subjobId, &henna1, &henna2, &henna3
        );
        lastRowSucceeded = user->AddSubJob(
            newClass, static_cast<double>(hp), static_cast<double>(mp), sp, experience, level, subjobId, henna1, henna2,
            henna3
        );
    }
    if (rowCount > 0 && !lastRowSucceeded) {
        socket->Send("cdd", 96, 0, 1);
        return false;
    }

    socket->Send("cd", 96, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x29a);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[11] = runtime::DecimalUtf16(rowCount);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool GetSSQStatusPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0040e71c, AQ 85. All twelve result columns and
    // their reply order are fixed by the current Bind sequence.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(packet, "S", 0x200, ignoredAuditTextUtf16.data());

    int roundNumber = 0;
    int twilightPoint = 0;
    int dawnPoint = 0;
    int seal1 = 0;
    int seal2 = 0;
    int seal3 = 0;
    int seal4 = 0;
    int seal5 = 0;
    int seal6 = 0;
    int seal7 = 0;
    int currentTime = 0;
    int status = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&roundNumber);
    connection.Bind(&twilightPoint);
    connection.Bind(&dawnPoint);
    connection.Bind(&seal1);
    connection.Bind(&seal2);
    connection.Bind(&seal3);
    connection.Bind(&seal4);
    connection.Bind(&seal5);
    connection.Bind(&seal6);
    connection.Bind(&seal7);
    connection.Bind(&currentTime);
    connection.Bind(&status);
    constexpr const wchar_t* statement = L"{Call lin_GetSSQStatus}";
    connection.Prepare(statement);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot load Seven Signs status: database operation failed. The status reply still uses the initialized zero values."
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot load Seven Signs status: database operation failed; the status reply still uses the initialized zero values."
        );
    }

    socket->Send(
        "cdddddddddddd", 85, roundNumber, twilightPoint, dawnPoint, seal1, seal2, seal3, seal4, seal5, seal6, seal7,
        currentTime, status
    );
    return false;
}

bool GetSSQMainEventRecordPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0040e998, AQ 86. The target returns every current
    // main-event row with the database member-name field unchanged.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(packet, "S", 0x200, ignoredAuditTextUtf16.data());

    int ssqRound = 0;
    int roomNumber = 0;
    int recordType = 0;
    int point = 0;
    int recordTime = 0;
    int elapsedTime = 0;
    int memberCount = 0;
    std::array<char16_t, 0x200> memberNames{};
    int currentTime = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&ssqRound);
    connection.Bind(&roomNumber);
    connection.Bind(&recordType);
    connection.Bind(&point);
    connection.Bind(&recordTime);
    connection.Bind(&elapsedTime);
    connection.Bind(&memberCount);
    connection.Bind(memberNames.data(), 0x400);
    connection.Bind(&currentTime);

    constexpr const wchar_t* statement = L"{Call lin_GetSSQMainEventRecord}";
    connection.Prepare(statement);
    const bool executed = connection.ExecutePrepared(statement);
    if (!executed) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot load Seven Signs main-event records (command 86): database operation failed. Returning an empty record list."
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot load Seven Signs main-event records (command 86): database operation failed; returning an empty record list."
        );
    }

    std::array<char, 0x5000> rows{};
    int rowBytes = 0;
    int rowCount = 0;
    bool rowAssemblyFailureReported = false;
    while (executed && connection.Fetch()) {
        // rowBytes += ::Assemble(
        //     rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dddddddSd", ssqRound, roomNumber, recordType,
        //     point, recordTime, elapsedTime, memberCount, memberNamesUtf16.c_str(), currentTime
        // )
        const int assembledRowBytes = ::Assemble(
            rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dddddddSd", ssqRound, roomNumber, recordType,
            point, recordTime, elapsedTime, memberCount, memberNames.data(), currentTime
        );
        rowBytes += assembledRowBytes;
        if (assembledRowBytes <= 0 && !rowAssemblyFailureReported) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[admin] Cannot assemble Seven Signs records (command 86) response entry %lld, round %d, room %d, record type %d (assembler result %d, accumulated %d bytes in the 20480-byte row buffer). Processing continues and the advertised count still includes failed entries.",
            //     static_cast<long long>(rowCount) + 1, ssqRound, roomNumber, recordType, assembledRowBytes, rowBytes
            // )
            L2_LOG_ERROR(Log,
                "[admin] Cannot assemble Seven Signs records (command 86) response entry %lld, round %d, room %d, record type %d (assembler result %d, accumulated %d bytes in the 20480-byte row buffer). Processing continues and the advertised count still includes failed entries.",
                static_cast<long long>(rowCount) + 1, ssqRound, roomNumber, recordType, assembledRowBytes, rowBytes
            );
            rowAssemblyFailureReported = true;
        }
        ++rowCount;
    }

    socket->Send("cdb", 86, rowCount, rowBytes, rows.data());
    return false;
}

bool GetAgitListPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0040ec6c, AQ 102, codec S. The target increments
    // the row count even when its fixed 0x5000-byte buffer can no longer
    // accept a row. Only the accumulated byte count stops growing.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(packet, "S", 0x200, ignoredAuditTextUtf16.data());

    int agitId = 0;
    std::array<char16_t, 50> agitName{};
    int pledgeId = 0;
    std::array<char16_t, 24> pledgeName{};
    int rulerId = 0;
    std::array<char16_t, 50> rulerName{};
    int auctionTime = 0;
    int nextWarTime = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&agitId);
    connection.Bind(agitName.data(), 100);
    connection.Bind(&pledgeId);
    connection.Bind(pledgeName.data(), 0x30);
    connection.Bind(&rulerId);
    connection.Bind(rulerName.data(), 100);
    connection.Bind(&auctionTime);
    connection.Bind(&nextWarTime);
    constexpr const wchar_t* statement = L"{Call lin_GetAgitList}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot load the clan-hall list: database operation failed. Returning a zero-count reply."
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot load the clan-hall list: database operation failed; returning a zero-count reply."
        );
        socket->Send("cd", 102, 0);
        return false;
    }

    std::array<char, 0x5000> rows{};
    int rowBytes = 0;
    int rowCount = 0;
    bool rowAssemblyFailureReported = false;
    while (connection.Fetch()) {
        // rowBytes += ::Assemble(
        //     rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dSdSdSdd", agitId, agitNameUtf16.c_str(),
        //     pledgeId, pledgeNameUtf16.c_str(), rulerId, rulerNameUtf16.c_str(), auctionTime, nextWarTime
        // )
        const int assembledRowBytes = ::Assemble(
            rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dSdSdSdd", agitId, agitName.data(),
            pledgeId, pledgeName.data(), rulerId, rulerName.data(), auctionTime, nextWarTime
        );
        rowBytes += assembledRowBytes;
        if (assembledRowBytes <= 0 && !rowAssemblyFailureReported) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[admin] Cannot assemble clan-hall list response entry %lld, residence %d (assembler result %d, accumulated %d bytes in the 20480-byte row buffer). Processing continues and the advertised count still includes failed entries.",
            //     static_cast<long long>(rowCount) + 1, agitId, assembledRowBytes, rowBytes
            // )
            L2_LOG_ERROR(Log,
                "[admin] Cannot assemble clan-hall list response entry %lld, residence %d (assembler result %d, accumulated %d bytes in the 20480-byte row buffer). Processing continues and the advertised count still includes failed entries.",
                static_cast<long long>(rowCount) + 1, agitId, assembledRowBytes, rowBytes
            );
            rowAssemblyFailureReported = true;
        }
        ++rowCount;
    }

    socket->Send("cdb", 102, rowCount, rowBytes, rows.data());
    return false;
}

bool GetCharCreateDatePacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004243e4, AQ 149, codec dS. The audit string is
    // decoded but not consumed by the shipping body.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(packet, "dS", &characterId, 0x200, ignoredAuditTextUtf16.data());
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (characterId == 0 || !user) {
        socket->Send("cd", 149, 0);
        return false;
    }

    const SYSTEMTIME* const createDate = user->GetBirthday();
    std::array<wchar_t, 20> createDateText{};
    std::swprintf(
        createDateText.data(), createDateText.size(), L"%d-%d-%d %d:%d:%d", static_cast<int>(createDate->wYear),
        static_cast<int>(createDate->wMonth), static_cast<int>(createDate->wDay), static_cast<int>(createDate->wHour),
        static_cast<int>(createDate->wMinute), static_cast<int>(createDate->wSecond)
    );
    socket->Send("cdSS", 149, characterId, user->GetCharName(), createDateText.data());
    return false;
}

bool GetSSQMainEventRecordPacket2(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00431810, AQ 150. This version resolves every
    // comma-delimited member name through CUserDb and emits name|characterId
    // for each user that can be created.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(packet, "S", 0x200, ignoredAuditTextUtf16.data());

    int ssqRound = 0;
    int roomNumber = 0;
    int recordType = 0;
    int point = 0;
    int recordTime = 0;
    int elapsedTime = 0;
    int memberCount = 0;
    std::array<char16_t, 0x200> memberNames{};
    int currentTime = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&ssqRound);
    connection.Bind(&roomNumber);
    connection.Bind(&recordType);
    connection.Bind(&point);
    connection.Bind(&recordTime);
    connection.Bind(&elapsedTime);
    connection.Bind(&memberCount);
    connection.Bind(memberNames.data(), 0x400);
    connection.Bind(&currentTime);

    constexpr const wchar_t* statement = L"{Call lin_GetSSQMainEventRecord}";
    connection.Prepare(statement);
    const bool executed = connection.ExecutePrepared(statement);
    if (!executed) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot load Seven Signs main-event records (command 150): database operation failed. Returning an empty record list."
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot load Seven Signs main-event records (command 150): database operation failed; returning an empty record list."
        );
    }

    std::array<char, 0x5000> rows{};
    int rowBytes = 0;
    int rowCount = 0;
    bool rowAssemblyFailureReported = false;
    while (executed && connection.Fetch()) {
        const std::u16string databaseMemberNames = std::u16string(memberNames.data());
        const std::u16string source{std::u16string_view(databaseMemberNames)};
        const std::u16string delimiter{u","};
        StringTokenizer tokenizer(source, delimiter);
        const int tokenCount = tokenizer.CountTokens();

        std::u16string resolvedMemberNames;
        for (int index = 0; index < tokenCount; ++index) {
            const std::u16string token = tokenizer.NextToken();
            CUserSP user = g_userDb.GetUser(token.c_str(), false);
            if (user) {
                resolvedMemberNames.append(runtime::StringView(token));
                resolvedMemberNames.push_back(u'|');
                resolvedMemberNames.append(runtime::DecimalUtf16(static_cast<int>(user->GetId())));
            }
            if (index != tokenCount - 1) {
                resolvedMemberNames.push_back(u',');
            }
        }

        // rowBytes += ::Assemble(
        //     rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dddddddSd", ssqRound, roomNumber, recordType,
        //     point, recordTime, elapsedTime, memberCount, resolvedMemberNames.c_str(), currentTime
        // )
        const int assembledRowBytes = ::Assemble(
            rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dddddddSd", ssqRound, roomNumber, recordType,
            point, recordTime, elapsedTime, memberCount, resolvedMemberNames.c_str(), currentTime
        );
        rowBytes += assembledRowBytes;
        if (assembledRowBytes <= 0 && !rowAssemblyFailureReported) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[admin] Cannot assemble Seven Signs records (command 150) response entry %lld, round %d, room %d, record type %d (assembler result %d, accumulated %d bytes in the 20480-byte row buffer). Processing continues and the advertised count still includes failed entries.",
            //     static_cast<long long>(rowCount) + 1, ssqRound, roomNumber, recordType, assembledRowBytes, rowBytes
            // )
            L2_LOG_ERROR(Log,
                "[admin] Cannot assemble Seven Signs records (command 150) response entry %lld, round %d, room %d, record type %d (assembler result %d, accumulated %d bytes in the 20480-byte row buffer). Processing continues and the advertised count still includes failed entries.",
                static_cast<long long>(rowCount) + 1, ssqRound, roomNumber, recordType, assembledRowBytes, rowBytes
            );
            rowAssemblyFailureReported = true;
        }
        ++rowCount;
    }

    socket->Send("cdb", 150, rowCount, rowBytes, rows.data());
    return false;
}

bool SetUserBookMarkPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00435484, AQ 144. The current packet places the
    // icon title before the slot title and carries one decoded integer that
    // the shipping SQL call does not consume.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 144, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int slotId = 0;
    std::array<char16_t, 256> iconTitleUtf16{};
    std::array<char16_t, 256> slotTitleUtf16{};
    int iconId = 0;
    int ignoredPacketInteger = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(
        packet, "ddSSdddddS", &characterId, &slotId, 0x200, iconTitleUtf16.data(), 0x200, slotTitleUtf16.data(), &iconId,
        &ignoredPacketInteger, &x, &y, &z, 0x200, ignoredAuditTextUtf16.data()
    );

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 144, 0, 4);
        return false;
    }

    const std::u16string slotTitle(slotTitleUtf16.data());
    const std::u16string iconTitle(iconTitleUtf16.data());
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveUserBookmarkSlot (?, ?, ?, ?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&slotId);
    connection.BindParameter(&x);
    connection.BindParameter(&y);
    connection.BindParameter(&z);
    connection.BindParameter(&iconId);
    connection.BindParameter(slotTitle.c_str());
    connection.BindParameter(iconTitle.c_str());
    // connection.ExecutePrepared(statement)
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot store bookmark slot %d for character %d at (%d, %d, %d): database operation failed. This command still replies with success.",
        //     slotId, characterId, x, y, z
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot store bookmark slot %d for character %d at (%d, %d, %d): database operation failed; this command still replies with success.",
            slotId, characterId, x, y, z
        );
    }

    socket->Send("cd", 144, 1);
    return false;
}

bool ModUserColorPremiumPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00435aec, AQ 146, codec ddS. The SQL type is the
    // shipping nickname-color selector (1). The audit text is decoded but
    // otherwise unused by this handler.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 146, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int nicknameColor = 0;
    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(packet, "ddS", &characterId, &nicknameColor, 0x200, ignoredAuditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 146, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 146, 0, 7);
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    const int colorType = 1;
    constexpr const wchar_t* statement = L"{Call lin_SetColor (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&nicknameColor);
    connection.BindParameter(&colorType);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR,
        //     "[admin color] Cannot set nickname color %d for character %d: database execution failed. The cached color is unchanged and failure reason 1 is returned.",
        //     nicknameColor, characterId)
        L2_LOG_ERROR(Log,
            "[admin color] Cannot set nickname color %d for character %d: database execution failed; the cached color is unchanged and failure reason 1 is returned.",
            nicknameColor, characterId);
        socket->Send("cdd", 146, 0, 1);
        return false;
    }

    user->m_nicknameColor = nicknameColor;
    socket->Send("cd", 146, 1);
    return false;
}

bool DelUserBookMarkPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00435e9c, AQ 145. Every requested slot is
    // attempted. One failed execution makes the final reply fail without
    // stopping the remaining deletes.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 145, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int slotCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "dd", &characterId, &slotCount);

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 145, 0, 4);
        return false;
    }

    bool allDeleted = true;
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeleteUserBookmarkSlot (?, ?)}";
    for (int index = 0; index < slotCount; ++index) {
        int slotId = 0;
        cursor = ::Disassemble(cursor, "d", &slotId);
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.BindParameter(&slotId);
        // allDeleted = connection.ExecutePrepared(statement) && allDeleted
        const bool slotDeleted = connection.ExecutePrepared(statement);
        allDeleted = slotDeleted && allDeleted;
        if (!slotDeleted) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[admin] Cannot delete bookmark slot %d for character %d (entry %lld of %d): database operation failed. Other requested slots are still attempted. The final reply will report failure.",
            //     slotId, characterId, static_cast<long long>(index) + 1, slotCount
            // )
            L2_LOG_ERROR(Log,
                "[admin] Cannot delete bookmark slot %d for character %d (entry %lld of %d): database operation failed. Other requested slots are still attempted; the final reply will report failure.",
                slotId, characterId, static_cast<long long>(index) + 1, slotCount
            );
        }
    }

    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(cursor, "S", 0x200, ignoredAuditTextUtf16.data());
    if (!allDeleted) {
        socket->Send("cdd", 145, 0, 1);
        return false;
    }

    socket->Send("cd", 145, 1);
    return false;
}

bool ReloadCharInfo(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0042e0c4, AQ 157, codec dS. Missing users and
    // failed reloads are logged but still receive the target's success reply.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dS", &characterId, 0x200, auditTextUtf16.data());
    const std::string auditText = Utf16ToUtf8(std::u16string_view(auditTextUtf16.data()));
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "cannot reload account[%d] by [%s]", characterId, auditText.c_str())
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot reload character %d: character unavailable before reload. The existing handler still replies with success (audit note: %s).",
        //     characterId, auditText.c_str()
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot reload character %d: character unavailable before reload; the existing handler still replies with success (audit note: %s).",
            characterId, auditText.c_str()
        );
        socket->Send("cd", 157, 1);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 157, 0, 7);
        return false;
    }

    user = g_userDb.ReLoadFromDb(user.get(), static_cast<CUser::INCLUDE_WAREHOUSE>(1));
    if (!user) {
        // Log.Add(LOG_ERROR, "cannot reload account[%d] by [%s]", characterId, auditText.c_str())
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot reload character %d: database reload returned no character. The existing handler still replies with success (audit note: %s).",
        //     characterId, auditText.c_str()
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot reload character %d: database reload returned no character; the existing handler still replies with success (audit note: %s).",
            characterId, auditText.c_str()
        );
    }
    socket->Send("cd", 157, 1);
    return false;
}

bool PledgeChangeOwnerPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00419620. The handler validates both kept
    // objects but performs no ownership mutation.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 37, 0, 0x1e);
        return false;
    }

    int pledgeId = 0;
    int characterId = 0;
    std::array<char16_t, 256> ignoredCharacterNameUtf16{};
    ::Disassemble(packet, "ddS", &pledgeId, &characterId, 0x200, ignoredCharacterNameUtf16.data());
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        socket->Send("cdd", 37, 0, 0x18);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 37, 0, 4);
        return false;
    }

    socket->Send("cd", 37, 1);
    return false;
}

bool PledgeDeletePacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004198f8. The C1 donor's wider dsss request is
    // not used: the current packet has one dword only.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 38, 0, 0x1e);
        return false;
    }

    int pledgeId = 0;
    ::Disassemble(packet, "d", &pledgeId);
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        socket->Send("cdd", 38, 0, 0x18);
        return false;
    }

    socket->Send("cd", 38, 1);
    return false;
}

bool ChangePledgeLevelPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0041b22c.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 46, 0, 0x1e);
        return false;
    }

    int pledgeId = 0;
    int pledgeLevel = 0;
    std::array<char16_t, 256> ignoredPledgeNameUtf16{};
    ::Disassemble(packet, "ddS", &pledgeId, &pledgeLevel, 0x200, ignoredPledgeNameUtf16.data());
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        socket->Send("cdd", 46, 0, 0x18);
        return false;
    }

    CSocket* const serverSocket = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (serverSocket != nullptr) {
        serverSocket->Send("cddd", 0x94, 2, pledge->GetId(), pledgeLevel);
    } else {
        g_pledgeDb.SetPledgeInfo(pledge->GetId(), 5, pledgeLevel);
    }

    socket->Send("cd", 46, 1);
    return false;
}

bool DelSeizedItemPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0041bd3c, AQ 130, codec dQS. Only administrative
    // user/pledge warehouses 1001 and 1002 are accepted. All post-gate
    // replies keep the shipping legacy opcode 54.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 130, 0, 0x1e);
        return false;
    }

    int itemId = 0;
    std::int64_t amount = 0;
    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(packet, "dQS", &itemId, &amount, 0x200, ignoredAuditTextUtf16.data());

    CItemSP item = CItem::Load(static_cast<std::uint32_t>(itemId));
    if (!item) {
        socket->Send("cdd", 54, 0, 1);
        return false;
    }

    const int ownerId = static_cast<int>(item->OwnerID());
    const int originalWarehouse = item->Warehouse();
    const int itemType = static_cast<int>(item->ItemType());
    std::array<char16_t, 256> baseAttributeText{};
    BaseAttribute baseAttribute(item->Attribute());
    baseAttribute.GetLogString(baseAttributeText.data());
    const std::u16string baseAttributeLog = AdminLogText(baseAttributeText.data());
    const std::u16string variationLog = MakeAdminVariationLogText(item->m_intensiveItemType, item->OptionKey());

    if (originalWarehouse != 1001 && originalWarehouse != 1002) {
        socket->Send("cdd", 54, 0, 3);
        return false;
    }

    const std::int64_t remainingAmount = runtime::SubtractInt64Wrapping(item->Amount(), amount);
    if (remainingAmount < 1) {
        if (!item->Delete(true, true)) {
            // Log.Add(
            //     LOG_ERROR, "[adminsocket.cpp][%d] item delete fail[%d]", originalWarehouse == 1001 ? 0xfb8 : 0xfdf,
            //     itemId
            // )
            // Log.Add(
            //     LOG_ERROR,
            //     "[admin] Cannot delete seized item %d (owner %d, warehouse %d, requested amount %lld, remaining amount %lld). The existing handler continues to a success reply.",
            //     itemId, ownerId, originalWarehouse, static_cast<long long>(amount), static_cast<long long>(remainingAmount)
            // )
            L2_LOG_ERROR(Log,
                "[admin] Cannot delete seized item %d (owner %d, warehouse %d, requested amount %lld, remaining amount %lld); the existing handler continues to a success reply.",
                itemId, ownerId, originalWarehouse, static_cast<long long>(amount), static_cast<long long>(remainingAmount)
            );
        }
    } else {
        item->SetAmount(remainingAmount);
        // item->Save(true)
        if (!item->Save(true)) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[admin] Cannot save seized item %d after changing its amount (owner %d, warehouse %d, requested removal %lld, computed remainder %lld). The existing handler continues to a success reply.",
            //     itemId, ownerId, originalWarehouse, static_cast<long long>(amount), static_cast<long long>(remainingAmount)
            // )
            L2_LOG_ERROR(Log,
                "[admin] Cannot save seized item %d after changing its amount (owner %d, warehouse %d, requested removal %lld, computed remainder %lld); the existing handler continues to a success reply.",
                itemId, ownerId, originalWarehouse, static_cast<long long>(amount), static_cast<long long>(remainingAmount)
            );
        }
    }

    CUserSP auditUser;
    if (originalWarehouse == 1001) {
        auditUser = g_userDb.GetUser(ownerId, true, false, runtime::process::CurrentDatabaseId());
    } else {
        CPledgeSP pledge = g_pledgeDb.GetPledge(ownerId, false, runtime::process::CurrentDatabaseId());
        if (pledge) {
            auditUser =
                g_userDb.GetUser(static_cast<int>(pledge->rullerId()), true, false, runtime::process::CurrentDatabaseId());
        }
    }
    if (auditUser) {
        WriteAdminDeleteItemAudit(
            auditUser.get(), baseAttributeLog, variationLog, itemType, amount, remainingAmount, itemId
        );
    }

    socket->Send("cd", 54, 1);
    return false;
}

bool DelMacroPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0041c864.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 57, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int macroId = 0;
    std::array<char16_t, 256> ignoredCharacterNameUtf16{};
    ::Disassemble(packet, "ddS", &characterId, &macroId, 0x200, ignoredCharacterNameUtf16.data());
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 57, 0, 4);
        return false;
    }
    // Current instruction 0x0041c9bb reads the logged-in byte at
    // CUser+0x24a4 directly. It is not the item-check state at +0x25c8.
    if (user->m_loggedIn) {
        socket->Send("cdd", 57, 0, 7);
        return false;
    }

    user->DeleteUserMacro(macroId);
    socket->Send("cd", 57, 1);
    return false;
}

bool AddRecipeBookPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0041ceac, AQ 125, codec ddS. This is the additive
    // counterpart of DelRecipePacket and uses the same offline-only policy.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 125, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int recipeId = 0;
    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(packet, "ddS", &characterId, &recipeId, 0x200, ignoredAuditTextUtf16.data());
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 125, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 125, 0, 7);
        return false;
    }
    if (!user->SaveRecipeBookInfo(recipeId)) {
        socket->Send("cdd", 125, 0, 0x1b);
        return false;
    }

    socket->Send("cd", 125, 1);
    return false;
}

bool DelRecipePacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0041d178.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 59, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int recipeId = 0;
    std::array<char16_t, 256> ignoredCharacterNameUtf16{};
    ::Disassemble(packet, "ddS", &characterId, &recipeId, 0x200, ignoredCharacterNameUtf16.data());
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 59, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 59, 0, 7);
        return false;
    }
    if (!user->DeleteRecipeBookInfo(recipeId)) {
        socket->Send("cdd", 59, 0, 0x1b);
        return false;
    }

    socket->Send("cd", 59, 1);
    return false;
}

bool DelLottoPacket(CAdminSocket*, const std::uint8_t*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool EventPointPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0043c088, AQ 62. This is the administrative
    // counterpart of primary RequestLoadEventPoint, with opcode 62.
    L2_GUARD_FUNCTION();

    std::array<char, 0x5000> response{};
    std::uint32_t groupCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "d", &groupCount);
    int responseSize = ::Assemble(response.data(), static_cast<int>(response.size()), "d", groupCount);

    std::uint32_t failedGroupQueries = 0;
    std::uint32_t firstFailedGroupId = 0;
    bool rowAssemblyFailureReported = false;
    DBConn connection(runtime::process::CurrentDatabaseId());
    std::int64_t nullableIndicator = -1;
    for (std::uint32_t index = 0; index < groupCount; ++index) {
        std::uint32_t groupId = 0;
        double groupPoint = 0.0;
        cursor = ::Disassemble(cursor, "d", &groupId);

        connection.BindNullable(&groupPoint, &nullableIndicator);
        constexpr const wchar_t* statement = L"{Call lin_GetGroupPoint (? )}";
        connection.Prepare(statement);
        connection.BindParameter(&groupId);
        if (connection.ExecutePrepared(statement)) {
            connection.Fetch();
        } else {
            if (failedGroupQueries == 0) firstFailedGroupId = groupId;
            ++failedGroupQueries;
        }

        // responseSize += ::Assemble(
        //     response.data() + responseSize, static_cast<int>(response.size()) - responseSize, "df", groupId, groupPoint
        // )
        const int assembledRowBytes = ::Assemble(
            response.data() + responseSize, static_cast<int>(response.size()) - responseSize, "df", groupId, groupPoint
        );
        responseSize += assembledRowBytes;
        if (assembledRowBytes <= 0 && !rowAssemblyFailureReported) {
            // Log.Add(LOG_ERROR,
            //     "[admin event points] Cannot assemble response entry %u for group %u (assembler result %d, accumulated %d bytes). Processing continues and the advertised group count %u still includes failed entries.",
            //     index + 1, groupId, assembledRowBytes, responseSize, groupCount)
            L2_LOG_ERROR(Log,
                "[admin event points] Cannot assemble response entry %u for group %u (assembler result %d, accumulated %d bytes). Processing continues and the advertised group count %u still includes failed entries.",
                index + 1, groupId, assembledRowBytes, responseSize, groupCount);
            rowAssemblyFailureReported = true;
        }
        connection.UnBind();
    }

    if (failedGroupQueries != 0) {
        // Log.Add(LOG_ERROR,
        //     "[admin event points] Queries failed for %u of %u requested groups. First failed group %u. Those groups retain zero points and response processing continues.",
        //     failedGroupQueries, groupCount, firstFailedGroupId)
        L2_LOG_ERROR(Log,
            "[admin event points] Queries failed for %u of %u requested groups; first failed group %u. Those groups retain zero points and response processing continues.",
            failedGroupQueries, groupCount, firstFailedGroupId);
    }

    socket->Send("cb", 62, responseSize, response.data());
    return false;
}

bool AllEventPointPacket(CAdminSocket* socket, const std::uint8_t*) {
    // Current Cached.exe 0x00412988.
    L2_GUARD_FUNCTION();

    std::array<char, 0x5000> rows{};
    int assembledSize = 0;
    int rowCount = 0;
    bool rowAssemblyFailureReported = false;
    int groupId = 0;
    double groupPoint = 0.0;
    std::int64_t nullableIndicator = -1;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.BindNullable(&groupId, &nullableIndicator);
    connection.BindNullable(&groupPoint, &nullableIndicator);
    constexpr const wchar_t* statement = L"{Call lin_GetAllGroupPoint}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot load event-group points: database operation failed. Returning failure."
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot load event-group points: database operation failed; returning failure."
        );
        socket->Send("cdd", 64, 0, 1);
        return false;
    }

    while (connection.Fetch()) {
        const int written = ::Assemble(
            rows.data() + assembledSize, static_cast<int>(rows.size()) - assembledSize, "df", groupId, groupPoint
        );
        assembledSize += written;
        if (written <= 0 && !rowAssemblyFailureReported) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[admin] Cannot assemble event-group points response entry %lld, group %d (assembler result %d, accumulated %d bytes in the 20480-byte row buffer). Processing continues and the advertised count still includes failed entries.",
            //     static_cast<long long>(rowCount) + 1, groupId, written, assembledSize
            // )
            L2_LOG_ERROR(Log,
                "[admin] Cannot assemble event-group points response entry %lld, group %d (assembler result %d, accumulated %d bytes in the 20480-byte row buffer). Processing continues and the advertised count still includes failed entries.",
                static_cast<long long>(rowCount) + 1, groupId, written, assembledSize
            );
            rowAssemblyFailureReported = true;
        }
        ++rowCount;
    }
    socket->Send("cdb", 64, rowCount, assembledSize, rows.data());
    return false;
}

bool GetPledgePacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0041d444, AQ 63. The target reads the resident
    // pledge/alliance objects directly and does not apply the admin gate.
    L2_GUARD_FUNCTION();

    int pledgeId = 0;
    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(packet, "dS", &pledgeId, 0x200, ignoredAuditTextUtf16.data());

    const int worldId = runtime::process::CurrentDatabaseId();
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, worldId);
    if (!pledge) {
        socket->Send("cd", 63, 0);
        return false;
    }

    CAllianceSP alliance = g_allianceDb.GetAlliance(pledge->allianceId(), static_cast<std::uint32_t>(worldId));
    socket->Send(
        "cdSdSdddS", 63, pledgeId, pledge->GetName(), static_cast<int>(pledge->rullerId()), pledge->rullerName(),
        static_cast<int>(pledge->allianceId()), pledge->skillLevel(), static_cast<int>(pledge->GetMemberCount()),
        alliance ? alliance->name() : u""
    );
    return false;
}

bool GetPledge2Packet(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0042409c, AQ 97. This is AQ 63 with the alliance
    // master pledge ID appended as the final d.
    L2_GUARD_FUNCTION();

    int pledgeId = 0;
    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(packet, "dS", &pledgeId, 0x200, ignoredAuditTextUtf16.data());

    const int worldId = runtime::process::CurrentDatabaseId();
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, worldId);
    if (!pledge) {
        socket->Send("cd", 97, 0);
        return false;
    }

    CAllianceSP alliance = g_allianceDb.GetAlliance(pledge->allianceId(), static_cast<std::uint32_t>(worldId));
    socket->Send(
        "cdSdSdddSd", 97, pledgeId, pledge->GetName(), static_cast<int>(pledge->rullerId()), pledge->rullerName(),
        static_cast<int>(pledge->allianceId()), pledge->skillLevel(), static_cast<int>(pledge->GetMemberCount()),
        alliance ? alliance->name() : u"", alliance ? static_cast<int>(alliance->masterPledgeId()) : 0
    );
    return false;
}

bool GetPledgeMemberPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0043e4a0, AQ 103. Only the first result column is
    // bound here. CPledgeDb separately consumes the procedure's pledge type.
    L2_GUARD_FUNCTION();

    int pledgeId = 0;
    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(packet, "dS", &pledgeId, 0x200, ignoredAuditTextUtf16.data());

    int memberId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&memberId);
    constexpr const wchar_t* statement = L"{Call lin_LoadPledgeMember (?)}";
    if (pledgeId <= 0) {
        socket->Send("cd", 103, 0);
        return false;
    }
    connection.Prepare(statement);
    connection.BindParameter(&pledgeId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot load members of clan %d (command 103): database operation failed. Returning a zero-count reply.",
        //     pledgeId
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot load members of clan %d (command 103): database operation failed; returning a zero-count reply.",
            pledgeId
        );
        socket->Send("cd", 103, 0);
        return false;
    }

    std::array<char, 0x5000> rows{};
    int rowBytes = 0;
    int rowCount = 0;
    bool rowAssemblyFailureReported = false;
    while (connection.Fetch()) {
        CUserSP member = g_userDb.GetUser(memberId, true, false, runtime::process::CurrentDatabaseId());
        if (!member)
            continue;

        const std::u16string characterName = AdminLogText(member->GetCharName());
        // rowBytes += ::Assemble(
        //     rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dSdd", static_cast<int>(member->GetId()),
        //     characterName.c_str(), member->GetClass(), member->IsLoggedIn() ? 1 : 0
        // )
        const int assembledRowBytes = ::Assemble(
            rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dSdd", static_cast<int>(member->GetId()),
            characterName.c_str(), member->GetClass(), member->IsLoggedIn() ? 1 : 0
        );
        rowBytes += assembledRowBytes;
        if (assembledRowBytes <= 0 && !rowAssemblyFailureReported) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[admin] Cannot assemble clan members (command 103) response entry %lld, clan %d, character %d (assembler result %d, accumulated %d bytes in the 20480-byte row buffer). Processing continues and the advertised count still includes failed entries.",
            //     static_cast<long long>(rowCount) + 1, pledgeId, memberId, assembledRowBytes, rowBytes
            // )
            L2_LOG_ERROR(Log,
                "[admin] Cannot assemble clan members (command 103) response entry %lld, clan %d, character %d (assembler result %d, accumulated %d bytes in the 20480-byte row buffer). Processing continues and the advertised count still includes failed entries.",
                static_cast<long long>(rowCount) + 1, pledgeId, memberId, assembledRowBytes, rowBytes
            );
            rowAssemblyFailureReported = true;
        }
        ++rowCount;
    }

    socket->Send("cdb", 103, rowCount, rowBytes, rows.data());
    return false;
}

bool EternalBanPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0043eaa8, AQ 104, codec dS. The shipping path
    // persists status 2 with ban-hour -1 and database ban-end 0, records
    // punishment action 0x0f regardless of the first call's result, then
    // stores resident status 2 and ban-end -1 only on SQL success.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 104, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dS", &characterId, 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 104, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 104, 0, 7);
        return false;
    }

    int userId = static_cast<int>(user->GetId());
    int status = 2;
    int banHours = -1;
    int databaseBanEnd = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* setBanStatement = L"{Call lin_SetUserBan (?, ?, ?, ? )}";
    connection.Prepare(setBanStatement);
    connection.BindParameter(&userId);
    connection.BindParameter(&status);
    connection.BindParameter(&banHours);
    connection.BindParameter(&databaseBanEnd);
    const bool saved = connection.ExecutePrepared(setBanStatement);
    connection.ResetParam();

    int historyAction = 0x0f;
    constexpr const wchar_t* historyStatement = L"{Call lin_AddPunishmentHistory (?, ?, ?, ?)}";
    connection.Prepare(historyStatement);
    connection.BindParameter(user->GetCharName());
    connection.BindParameter(&userId);
    connection.BindParameter(&historyAction);
    connection.BindParameter(user->GetAccountName());
    // connection.ExecutePrepared(historyStatement)
    if (!connection.ExecutePrepared(historyStatement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot record permanent-ban history for character %d. The ban database operation %s and determines the reply.",
        //     userId, saved ? "executed" : "failed"
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot record permanent-ban history for character %d; the ban database operation %s and determines the reply.",
            userId, saved ? "executed" : "failed"
        );
    }

    if (!saved) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot store a permanent ban for character %d. History recording was already attempted. Returning failure without changing the cached ban.",
        //     userId
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot store a permanent ban for character %d. History recording was already attempted; returning failure without changing the cached ban.",
            userId
        );
        socket->Send("cdd", 104, 0, 0x0b);
        return false;
    }

    user->SetStatus(2);
    user->SetBanEnd(-1);
    socket->Send("cd", 104, 1);

    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x29d);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[5] = runtime::DecimalUtf16(user->GetX());
    fields[6] = runtime::DecimalUtf16(user->GetY());
    fields[7] = runtime::DecimalUtf16(user->GetZ());
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool GetPledgeMemberPacket2(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0043e790, AQ 127. The second form appends level
    // and narrows class/logged-in/level to target 16-bit h fields.
    L2_GUARD_FUNCTION();

    int pledgeId = 0;
    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(packet, "dS", &pledgeId, 0x200, ignoredAuditTextUtf16.data());

    int memberId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&memberId);
    constexpr const wchar_t* statement = L"{Call lin_LoadPledgeMember (?)}";
    if (pledgeId <= 0) {
        socket->Send("cd", 127, 0);
        return false;
    }
    connection.Prepare(statement);
    connection.BindParameter(&pledgeId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot load members of clan %d (command 127): database operation failed. Returning a zero-count reply.",
        //     pledgeId
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot load members of clan %d (command 127): database operation failed; returning a zero-count reply.",
            pledgeId
        );
        socket->Send("cd", 127, 0);
        return false;
    }

    std::array<char, 0x5000> rows{};
    int rowBytes = 0;
    int rowCount = 0;
    bool rowAssemblyFailureReported = false;
    while (connection.Fetch()) {
        CUserSP member = g_userDb.GetUser(memberId, true, false, runtime::process::CurrentDatabaseId());
        if (!member)
            continue;

        const std::u16string characterName = AdminLogText(member->GetCharName());
        // rowBytes += ::Assemble(
        //     rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dShhh", static_cast<int>(member->GetId()),
        //     characterName.c_str(), member->GetClass(), member->IsLoggedIn() ? 1 : 0, member->GetLevel()
        // )
        const int assembledRowBytes = ::Assemble(
            rows.data() + rowBytes, static_cast<int>(rows.size()) - rowBytes, "dShhh", static_cast<int>(member->GetId()),
            characterName.c_str(), member->GetClass(), member->IsLoggedIn() ? 1 : 0, member->GetLevel()
        );
        rowBytes += assembledRowBytes;
        if (assembledRowBytes <= 0 && !rowAssemblyFailureReported) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[admin] Cannot assemble clan members (command 127) response entry %lld, clan %d, character %d (assembler result %d, accumulated %d bytes in the 20480-byte row buffer). Processing continues and the advertised count still includes failed entries.",
            //     static_cast<long long>(rowCount) + 1, pledgeId, memberId, assembledRowBytes, rowBytes
            // )
            L2_LOG_ERROR(Log,
                "[admin] Cannot assemble clan members (command 127) response entry %lld, clan %d, character %d (assembler result %d, accumulated %d bytes in the 20480-byte row buffer). Processing continues and the advertised count still includes failed entries.",
                static_cast<long long>(rowCount) + 1, pledgeId, memberId, assembledRowBytes, rowBytes
            );
            rowAssemblyFailureReported = true;
        }
        ++rowCount;
    }

    socket->Send("cdb", 127, rowCount, rowBytes, rows.data());
    return false;
}

bool GetPledge3Packet(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0042465c, AQ 128. Unlike AQ 63/97, this form
    // places skill/member count before alliance ID and appends both
    // residence IDs. It does not apply the administrative request gate.
    L2_GUARD_FUNCTION();

    int pledgeId = 0;
    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(packet, "dS", &pledgeId, 0x200, ignoredAuditTextUtf16.data());

    const int worldId = runtime::process::CurrentDatabaseId();
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, worldId);
    if (!pledge) {
        socket->Send("cd", 128, 0);
        return false;
    }

    CAllianceSP alliance = g_allianceDb.GetAlliance(pledge->allianceId(), static_cast<std::uint32_t>(worldId));
    socket->Send(
        "cdSdSdddSddd", 128, pledgeId, pledge->GetName(), static_cast<int>(pledge->rullerId()), pledge->rullerName(),
        pledge->skillLevel(), static_cast<int>(pledge->GetMemberCount()), static_cast<int>(pledge->allianceId()),
        alliance ? alliance->name() : u"", alliance ? static_cast<int>(alliance->masterPledgeId()) : 0,
        static_cast<int>(pledge->castleId()), static_cast<int>(pledge->agitId())
    );
    return false;
}

bool GetSubjobLevelPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0043d620, AQ 133, codec dS. The procedure returns
    // sparse {subjob_id, level} rows. Index zero must contain a positive
    // level before the four-row result is accepted.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(packet, "dS", &characterId, 0x200, ignoredAuditTextUtf16.data());
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (characterId == 0 || !user) {
        socket->Send("cd", 133, 0);
        return false;
    }

    int subjobId = 0;
    int level = 0;
    std::array<int, 4> levels{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&subjobId);
    connection.Bind(&level);
    constexpr const wchar_t* statement = L"{Call lin_GetSubjobLevel (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    // if (connection.ExecutePrepared(statement) && connection.Fetch()) {
    const bool subjobQueryExecuted = connection.ExecutePrepared(statement);
    if (subjobQueryExecuted && connection.Fetch()) {
        do {
            // The current target trusts the database subjob index and uses it
            // directly as the four-element array index.
            levels[static_cast<std::size_t>(subjobId)] = level;
        } while (connection.Fetch());

        if (levels[0] >= 1) {
            socket->Send("cdSdddd", 133, characterId, user->GetCharName(), levels[0], levels[1], levels[2], levels[3]);
            return false;
        }
    }

    if (!subjobQueryExecuted) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot load subjob levels for character %d: database operation failed. The reply falls back to the cached main level and zero subjob levels.",
        //     characterId
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot load subjob levels for character %d: database operation failed; the reply falls back to the cached main level and zero subjob levels.",
            characterId
        );
    }
    socket->Send("cdSdddd", 133, characterId, user->GetCharName(), user->GetLevel(), 0, 0, 0);
    return false;
}

bool AddSkillsPacket(CAdminSocket* socket, const std::uint8_t*) {
    L2_GUARD_FUNCTION();
    socket->Send("cdd", 67, 0, 0x38);
    return false;
}

bool StopCharPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 90, 0, 0x1e);
        return false;
    }

    int first = 0;
    int second = 0;
    std::array<char16_t, 256> characterName{};
    ::Disassemble(packet, "ddS", &first, &second, 0x200, characterName.data());
    return false;
}

bool CancelPersonalShopPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 91, 0, 0x1e);
        return false;
    }

    int first = 0;
    int second = 0;
    std::array<char16_t, 256> characterName{};
    ::Disassemble(packet, "ddS", &first, &second, 0x200, characterName.data());
    return false;
}

bool CreateSubjobPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0042587c, AQ 111, codec dddS. Offline users are
    // changed in CacheD and reloaded. Logged-in users are delegated to the
    // active L2Server through request 0x94/0x10.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int newSubjobId = 0;
    int newClass = -1;
    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(packet, "dddS", &characterId, &newSubjobId, &newClass, 0x200, ignoredAuditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 111, 0, 4);
        return false;
    }

    if (!user->IsLoggedIn()) {
        user->CreateSubJob(
            newSubjobId, newClass, user->GetSubjobId(), user->GetHP(), user->GetMP(), user->GetSP(), user->GetExp(),
            user->GetLevel(), user->m_henna[0], user->m_henna[1], user->m_henna[2], user->GetAP()
        );
        {
            CUserSP reloaded = g_userDb.ReLoadFromDb(user.get(), static_cast<CUser::INCLUDE_WAREHOUSE>(1));
        }
        socket->Send("cd", 111, 1);
        return false;
    }

    CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (server == nullptr) {
        socket->Send("cdd", 111, 0, 0x13);
        return false;
    }
    server->Send("cdddd", 0x94, 0x10, user->GetId(), newSubjobId, newClass);
    socket->Send("cd", 111, 1);
    return false;
}

bool CheckPCBangCharPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00425c20.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    std::array<char16_t, 256> ignoredCharacterNameUtf16{};
    ::Disassemble(packet, "dS", &characterId, 0x200, ignoredCharacterNameUtf16.data());
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 112, 0, 4);
        return false;
    }
    if (!user->m_sessionLogFlag) {
        socket->Send("cdd", 112, 0, 0x0f);
        return false;
    }

    socket->Send("cd", 112, 1);
    return false;
}

bool SetNoblessPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0043f90c, AQ 106, codec ddS. A zero result from
    // lin_SaveNoblessType falls through to lin_NewNobless even when the
    // first execution itself failed. Only the final result controls failure.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 106, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int noblessType = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddS", &characterId, &noblessType, 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 106, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 106, 0, 7);
        return false;
    }

    int result = 0;
    int residentCharacterId = static_cast<int>(user->GetId());
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    constexpr const wchar_t* saveNoblessType = L"{Call lin_SaveNoblessType (?, ?)}";
    connection.Prepare(saveNoblessType);
    connection.BindParameter(&residentCharacterId);
    connection.BindParameter(&noblessType);
    if (connection.ExecutePrepared(saveNoblessType)) {
        // connection.Fetch()
        if (!connection.Fetch()) {
            // Log.Add(LOG_WARN,
            //     "[admin noblesse] No update result was returned for character %d, requested type %d. Trying to create the noblesse record.",
            //     characterId, noblessType)
            L2_LOG_WARN(Log,
                "[admin noblesse] No update result was returned for character %d, requested type %d; trying to create the noblesse record.",
                characterId, noblessType);
        }
    } else {
        // Log.Add(LOG_WARN,
        //     "[admin noblesse] Update query failed for character %d, requested type %d. The request continues with a create attempt.",
        //     characterId, noblessType)
        L2_LOG_WARN(Log,
            "[admin noblesse] Update query failed for character %d, requested type %d; the request continues with a create attempt.",
            characterId, noblessType);
    }

    if (result == 0) {
        connection.UnBind();
        connection.Bind(&result);
        const int initialOlympiadPoint = 0x12;
        constexpr const wchar_t* newNobless = L"{Call lin_NewNobless (?, ?, ?)}";
        connection.Prepare(newNobless);
        connection.BindParameter(&residentCharacterId);
        connection.BindParameter(&noblessType);
        connection.BindParameter(&initialOlympiadPoint);
        if (connection.ExecutePrepared(newNobless)) {
            connection.Fetch();
        }
        if (result == 0) {
            // Log.Add(LOG_ERROR,
            //     "[admin noblesse] Cannot set type %d for character %d: neither the update nor the create attempt reported success. Sending failure reason 1.",
            //     noblessType, characterId)
            L2_LOG_ERROR(Log,
                "[admin noblesse] Cannot set type %d for character %d: neither the update nor the create attempt reported success; sending failure reason 1.",
                noblessType, characterId);
            socket->Send("cdd", 106, 0, 1);
            return false;
        }
    }

    CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (server != nullptr) {
        server->Send("cdd", 0x94, 0x0d, characterId);
    } else {
        // Log.Add(LOG_WARN,
        //     "[admin noblesse] Database operation reported success for character %d, type %d, but no game-server connection is available for the refresh notification. The administrative reply still reports success.",
        //     characterId, noblessType)
        L2_LOG_WARN(Log,
            "[admin noblesse] Database operation reported success for character %d, type %d, but no game-server connection is available for the refresh notification; the administrative reply still reports success.",
            characterId, noblessType);
    }
    socket->Send("cd", 106, 1);

    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x29e);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[5] = runtime::DecimalUtf16(user->GetX());
    fields[6] = runtime::DecimalUtf16(user->GetY());
    fields[7] = runtime::DecimalUtf16(user->GetZ());
    fields[11] = runtime::DecimalUtf16(noblessType);
    fields[12] = runtime::DecimalUtf16(user->GetLevel());
    fields[13] = runtime::DecimalUtf16(user->GetClass());
    fields[14] = runtime::DecimalUtf16(user->GetGender());
    fields[15] = runtime::DecimalUtf16(user->GetRace());
    fields[21] = AdminLogText(auditTextUtf16.data());
    fields[22] = AdminLogText(auditTextUtf16.data());
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool SetHeroPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004400d8, AQ 107, codec ddS. The invalid-class
    // and logged-in branches keep the shipping AQ-106 reply
    // opcode. All other replies use AQ 107.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 107, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int heroType = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddS", &characterId, &heroType, 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 107, 0, 4);
        return false;
    }

    const int heroClassIndex = OlympiadDefine::GetHeroClassIndex(static_cast<ClassType>(user->GetClass()));
    if (heroClassIndex == -1) {
        socket->Send("cdd", 106, 0, 3);
        return false;
    }

    // if (heroType >= 1) {
    if (heroType >= HERO_WAITING) {
        int existingHeroId = 0;
        const int heroClass = static_cast<int>(OlympiadDefine::GetHeroClassType(heroClassIndex));
        {
            DBConn connection(runtime::process::CurrentDatabaseId());
            connection.Bind(&existingHeroId);
            constexpr const wchar_t* getHeroByClass = L"{Call lin_GetHeroByClassId (?)}";
            connection.Prepare(getHeroByClass);
            connection.BindParameter(&heroClass);
            if (connection.ExecutePrepared(getHeroByClass)) {
                connection.Fetch();
            } else {
                // Log.Add(LOG_ERROR,
                //     "[admin hero] Hero lookup failed for class %d while setting type %d for character %d. Validation continues with hero ID zero, without confirming whether the class already has a hero.",
                //     heroClass, heroType, characterId)
                L2_LOG_ERROR(Log,
                    "[admin hero] Hero lookup failed for class %d while setting type %d for character %d; validation continues with hero ID zero, without confirming whether the class already has a hero.",
                    heroClass, heroType, characterId);
            }
        }

        if (existingHeroId >= 1) {
            socket->Send("cdd", 107, 0, 0x22);
            return false;
        }
        if (!user->IsNoblessOrHonors()) {
            socket->Send("cdd", 107, 0, 0x21);
            return false;
        }
    }

    if (user->IsLoggedIn()) {
        socket->Send("cdd", 106, 0, 7);
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* saveHeroType = L"{Call lin_SaveHeroType (?, ?)}";
    connection.Prepare(saveHeroType);
    connection.BindParameter(&characterId);
    connection.BindParameter(&heroType);
    if (!connection.ExecutePrepared(saveHeroType)) {
        // Log.Add(LOG_ERROR,
        //     "[admin hero] Cannot save hero type %d for character %d: database execution failed. Sending failure reason 1 and skipping refresh notification.",
        //     heroType, characterId)
        L2_LOG_ERROR(Log,
            "[admin hero] Cannot save hero type %d for character %d: database execution failed; sending failure reason 1 and skipping refresh notification.",
            heroType, characterId);
        socket->Send("cdd", 107, 0, 1);
        return false;
    }

    CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (server != nullptr) {
        server->Send("cdddd", 0x94, 0x0e, characterId, user->GetClass(), heroType);
    } else {
        // Log.Add(LOG_WARN,
        //     "[admin hero] Hero-type query succeeded for character %d, type %d, but no game-server connection is available for the refresh notification. The administrative reply still reports success.",
        //     characterId, heroType)
        L2_LOG_WARN(Log,
            "[admin hero] Hero-type query succeeded for character %d, type %d, but no game-server connection is available for the refresh notification; the administrative reply still reports success.",
            characterId, heroType);
    }
    socket->Send("cd", 107, 1);

    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x29f);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[5] = runtime::DecimalUtf16(user->GetX());
    fields[6] = runtime::DecimalUtf16(user->GetY());
    fields[7] = runtime::DecimalUtf16(user->GetZ());
    fields[11] = runtime::DecimalUtf16(heroType);
    fields[12] = runtime::DecimalUtf16(user->GetLevel());
    fields[13] = runtime::DecimalUtf16(user->GetClass());
    fields[14] = runtime::DecimalUtf16(user->GetGender());
    fields[15] = runtime::DecimalUtf16(user->GetRace());
    fields[21] = AdminLogText(auditTextUtf16.data());
    fields[22] = AdminLogText(auditTextUtf16.data());
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool ModOlympiadPointPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00440a50, AQ 109, codec ddddS. The audit text is
    // decoded but unused. A successful DB mutation emits the shipping
    // 0x94/0x14 refresh notification without changing the resident CUser.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int previousPoint = 0;
    int olympiadPoint = 0;
    int modificationType = 0;
    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(
        packet, "ddddS", &characterId, &previousPoint, &olympiadPoint, &modificationType, 0x200, ignoredAuditTextUtf16.data()
    );

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 109, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 109, 0, 7);
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_ModOlympiadPoint (?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&previousPoint);
    connection.BindParameter(&olympiadPoint);
    connection.BindParameter(&modificationType);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR,
        //     "[admin Olympiad] Cannot modify points for character %d: query failed (previous %d, requested %d, modification type %d). Sending failure reason 1.",
        //     characterId, previousPoint, olympiadPoint, modificationType)
        L2_LOG_ERROR(Log,
            "[admin Olympiad] Cannot modify points for character %d: query failed (previous %d, requested %d, modification type %d); sending failure reason 1.",
            characterId, previousPoint, olympiadPoint, modificationType);
        socket->Send("cdd", 109, 0, 1);
        return false;
    }

    CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (server != nullptr) {
        server->Send("cdd", 0x94, 0x14, user->GetId());
    } else {
        // Log.Add(LOG_WARN,
        //     "[admin Olympiad] Point query succeeded for character %d, requested %d, modification type %d, but no game-server connection is available for the refresh notification. The administrative reply still reports success.",
        //     characterId, olympiadPoint, modificationType)
        L2_LOG_WARN(Log,
            "[admin Olympiad] Point query succeeded for character %d, requested %d, modification type %d, but no game-server connection is available for the refresh notification; the administrative reply still reports success.",
            characterId, olympiadPoint, modificationType);
    }
    socket->Send("cd", 109, 1);
    return false;
}

bool SetPccafePointPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00440ddc, AQ 113. The current body performs no
    // admin-request gate or audit write and keeps the account read lock held
    // across both the short L2Server notification and database execution.
    L2_GUARD_FUNCTION();

    int accountId = 0;
    int point = 0;
    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(packet, "ddS", &accountId, &point, 0x200, ignoredAuditTextUtf16.data());

    CAccountSP account = CAccount::Load(static_cast<unsigned int>(accountId));
    if (!account || account->GetCharCount() == 0) {
        socket->Send("cdd", 113, 0, 4);
        return false;
    }

    CSocket* serverSocket;
    std::optional<DBConn> connectionStorage;
    bool executed;
    const bool hasCharacters = [&]() {
        L2_READ_GUARD(*account.operator->());
        auto* const characters = &account->GetChars();
        if (characters == nullptr) {
            // The shipping call returns CAccount+0x18, making this branch
            // unreachable for a valid account, but its observable reply is exact.
            return false;
        }

        serverSocket = g_pActiveServerSocket.load(std::memory_order_acquire);
        if (serverSocket != nullptr) {
            // This remains the target's short packet. The paired
            // current L2Server decoder asks for six dwords. CacheD supplies two.
            serverSocket->Send("cdd", 0x21, accountId, 0);
        }

        auto& connection = connectionStorage.emplace(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_SaveAccountPCCafePoint (?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&accountId);
        connection.BindParameter(&point);
        executed = connection.ExecutePrepared(statement);
        return true;
    }();
    if (!hasCharacters) {
        socket->Send("cdd", 113, 0, 0x16);
        return false;
    }

    if (!executed) {
        // Log.Add(LOG_ERROR,
        //     "[admin PC cafe] Cannot set points to %d for account %d: database execution failed. The preceding game-server notification was %s. Sending failure reason 1.",
        //     point, accountId, serverSocket ? "attempted" : "unavailable")
        L2_LOG_ERROR(Log,
            "[admin PC cafe] Cannot set points to %d for account %d: database execution failed; the preceding game-server notification was %s. Sending failure reason 1.",
            point, accountId, serverSocket ? "attempted" : "unavailable");
        socket->Send("cdd", 113, 0, 1);
        return false;
    }
    if (!serverSocket) {
        // Log.Add(LOG_WARN,
        //     "[admin PC cafe] Query to set points to %d for account %d succeeded without a game-server notification because no connection was available. The administrative reply reports success.",
        //     point, accountId)
        L2_LOG_WARN(Log,
            "[admin PC cafe] Query to set points to %d for account %d succeeded without a game-server notification because no connection was available; the administrative reply reports success.",
            point, accountId);
    }
    socket->Send("cd", 113, 1);
    return false;
}

bool SetPccafePointRelativePacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00441240, AQ 114. This is the relative SQL form.
    // all gates, lock boundaries, notifications and replies remain distinct
    // target code instead of a synthesized shared helper.
    L2_GUARD_FUNCTION();

    int accountId = 0;
    int pointDelta = 0;
    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(packet, "ddS", &accountId, &pointDelta, 0x200, ignoredAuditTextUtf16.data());

    CAccountSP account = CAccount::Load(static_cast<unsigned int>(accountId));
    if (!account || account->GetCharCount() == 0) {
        socket->Send("cdd", 114, 0, 4);
        return false;
    }

    CSocket* serverSocket;
    std::optional<DBConn> connectionStorage;
    bool executed;
    const bool hasCharacters = [&]() {
        L2_READ_GUARD(*account.operator->());
        auto* const characters = &account->GetChars();
        if (characters == nullptr) {
            return false;
        }

        serverSocket = g_pActiveServerSocket.load(std::memory_order_acquire);
        if (serverSocket != nullptr) {
            serverSocket->Send("cdd", 0x21, accountId, 0);
        }

        auto& connection = connectionStorage.emplace(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_SaveAccountPCCafePointRelative (?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&accountId);
        connection.BindParameter(&pointDelta);
        executed = connection.ExecutePrepared(statement);
        return true;
    }();
    if (!hasCharacters) {
        socket->Send("cdd", 114, 0, 0x16);
        return false;
    }

    if (!executed) {
        // Log.Add(LOG_ERROR,
        //     "[admin PC cafe] Cannot change points by %d for account %d: database execution failed. The preceding game-server notification was %s. Sending failure reason 1.",
        //     pointDelta, accountId, serverSocket ? "attempted" : "unavailable")
        L2_LOG_ERROR(Log,
            "[admin PC cafe] Cannot change points by %d for account %d: database execution failed; the preceding game-server notification was %s. Sending failure reason 1.",
            pointDelta, accountId, serverSocket ? "attempted" : "unavailable");
        socket->Send("cdd", 114, 0, 1);
        return false;
    }
    if (!serverSocket) {
        // Log.Add(LOG_WARN,
        //     "[admin PC cafe] Query to change points by %d for account %d succeeded without a game-server notification because no connection was available. The administrative reply reports success.",
        //     pointDelta, accountId)
        L2_LOG_WARN(Log,
            "[admin PC cafe] Query to change points by %d for account %d succeeded without a game-server notification because no connection was available; the administrative reply reports success.",
            pointDelta, accountId);
    }
    socket->Send("cd", 114, 1);
    return false;
}

bool AddPledgeSkillPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004285c4.
    L2_GUARD_FUNCTION();

    int pledgeId = 0;
    int skillId = 0;
    int skillLevel = 0;
    std::array<char16_t, 256> ignoredPledgeNameUtf16{};
    const std::uint8_t* cursor =
        ::Disassemble(packet, "dddS", &pledgeId, &skillId, &skillLevel, 0x200, ignoredPledgeNameUtf16.data());
    std::int32_t expirationTime = 0;
    ::Disassemble(cursor, "d", &expirationTime);
    if (expirationTime != 0) {
        expirationTime = runtime::AddInt32Wrapping(
            expirationTime,
            runtime::crt::Time32Now());
    }

    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        socket->Send("cdd", 119, 0, 0x18);
        return false;
    }
    if (!pledge->AddPledgeSkill(static_cast<std::uint32_t>(skillId), skillLevel, expirationTime, false)) {
        socket->Send("cdd", 119, 0, 1);
        return false;
    }

    CSocket* const serverSocket = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (serverSocket != nullptr) {
        serverSocket->Send("cddddd", 0x94, 0x11, pledgeId, skillId, skillLevel, expirationTime);
    }
    socket->Send("cd", 119, 1);
    return false;
}

bool ModPledgeSkillPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00428cec.
    L2_GUARD_FUNCTION();

    int pledgeId = 0;
    int skillId = 0;
    int skillLevel = 0;
    std::array<char16_t, 256> ignoredPledgeNameUtf16{};
    const std::uint8_t* cursor =
        ::Disassemble(packet, "dddS", &pledgeId, &skillId, &skillLevel, 0x200, ignoredPledgeNameUtf16.data());
    std::int32_t expirationTime = 0;
    ::Disassemble(cursor, "d", &expirationTime);
    if (expirationTime != 0) {
        expirationTime = runtime::AddInt32Wrapping(
            expirationTime,
            runtime::crt::Time32Now());
    }

    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        socket->Send("cdd", 120, 0, 0x18);
        return false;
    }
    if (!pledge->AddPledgeSkill(static_cast<std::uint32_t>(skillId), skillLevel, expirationTime, true)) {
        socket->Send("cdd", 120, 0, 1);
        return false;
    }

    CSocket* const serverSocket = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (serverSocket != nullptr) {
        serverSocket->Send("cddddd", 0x94, 0x12, pledgeId, skillId, skillLevel, expirationTime);
    }
    socket->Send("cd", 120, 1);
    return false;
}

bool DelPledgeSkillPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00429168.
    L2_GUARD_FUNCTION();

    int pledgeId = 0;
    int skillId = 0;
    std::array<char16_t, 256> ignoredPledgeNameUtf16{};
    ::Disassemble(packet, "ddS", &pledgeId, &skillId, 0x200, ignoredPledgeNameUtf16.data());
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        socket->Send("cdd", 121, 0, 0x18);
        return false;
    }
    if (!pledge->DelPledgeSkill(static_cast<std::uint32_t>(skillId))) {
        socket->Send("cdd", 121, 0, 1);
        return false;
    }

    CSocket* const serverSocket = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (serverSocket != nullptr) {
        serverSocket->Send("cddd", 0x94, 0x13, pledgeId, skillId);
    }
    socket->Send("cd", 121, 1);
    return false;
}

bool RegisterAccountPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0043dd44.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 256> accountNameUtf16{};
    std::array<char16_t, 256> ignoredPasswordUtf16{};
    ::Disassemble(packet, "SS", 0x200, accountNameUtf16.data(), 0x200, ignoredPasswordUtf16.data());

    const std::u16string accountName(std::u16string_view(accountNameUtf16.data()));
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_RegisterAccount (?)}";
    connection.Prepare(statement);
    connection.BindParameter(accountName.c_str());
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR,
        //     "[admin account] Account registration query failed, sending failure reason 1 (command 99).")
        L2_LOG_ERROR(Log,
            "[admin account] Account registration query failed; sending failure reason 1 (command 99).");
        socket->Send("cdd", 99, 0, 1);
        return false;
    }

    socket->Send("cd", 99, 1);
    return false;
}

bool DelPledgeAnnouncePacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0043df08, AQ 100, codec dS. The connected path
    // forwards to L2Server and skips SQL. The disconnected path
    // writes show_flag=0 and an empty narrow content string directly.
    L2_GUARD_FUNCTION();

    int pledgeId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dS", &pledgeId, 0x200, auditTextUtf16.data());

    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        // Log.Add(
        //     LOG_WARN,
        //     "[admin] Cannot delete announcement for clan %d: clan is unavailable. Returning failure.",
        //     pledgeId
        // )
        L2_LOG_WARN(Log,
            "[admin] Cannot delete announcement for clan %d: clan is unavailable; returning failure.",
            pledgeId
        );
        socket->Send("cdd", 100, 0, 0x18);
        return false;
    }

    CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (server != nullptr) {
        server->Send("cddddSd", 0x94, 0x0b, 0, 0, pledgeId, u"", 1);
    } else {
        int showFlag = 0;
        static const char emptyContent[] = "";
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_SetPledgeAnnounce (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&pledgeId);
        connection.BindParameter(&showFlag);
        connection.BindParameter(emptyContent);
        if (!connection.ExecutePrepared(statement)) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[admin] Cannot clear the stored announcement for clan %d while L2Server is disconnected: database operation failed. Returning failure.",
            //     pledgeId
            // )
            L2_LOG_ERROR(Log,
                "[admin] Cannot clear the stored announcement for clan %d while L2Server is disconnected: database operation failed; returning failure.",
                pledgeId
            );
            socket->Send("cdd", 100, 0, 1);
            return false;
        }
    }

    socket->Send("cd", 100, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x29c);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(pledge->rullerId());
    fields[4] = runtime::DecimalUtf16(pledge->GetId());
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(pledge->rullerName());
    fields[24] = AdminLogText(pledge->GetName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool DelPledgeEmblemPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00424a4c, AQ 98, codec dS. The connected path
    // delegates to L2Server. The disconnected path uses the legacy crest DB
    // for the stored new-emblem ID before clearing CPledge's emblem field.
    L2_GUARD_FUNCTION();

    int pledgeId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dS", &pledgeId, 0x200, auditTextUtf16.data());

    CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        socket->Send("cdd", 98, 0, 0x18);
        return false;
    }

    if (server != nullptr) {
        server->Send("chdd", 0xc6, 0x18, static_cast<int>(pledge->GetId()), 1);
    } else if (!g_pledgeCrestDb.DeleteCrest(pledge->m_newEmblemId) || !pledge->DeleteEmblem()) {
        socket->Send("cdd", 98, 0, 1);
        return false;
    }

    socket->Send("cd", 98, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x29b);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(pledge->rullerId());
    fields[4] = runtime::DecimalUtf16(pledge->GetId());
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(pledge->rullerName());
    fields[24] = AdminLogText(pledge->GetName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    return false;
}

bool SendPrivateAnnouncePacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00424f8c, AQ 101, codec dSS. This handler
    // bypasses the ordinary administrative rejection flag.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    std::array<char16_t, 256> messageUtf16{};
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dSS", &characterId, 0x200, messageUtf16.data(), 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(
        //     LOG_WARN,
        //     "[admin] Cannot send a private announcement to character %d: character is unavailable. Returning failure.",
        //     characterId
        // )
        L2_LOG_WARN(Log,
            "[admin] Cannot send a private announcement to character %d: character is unavailable; returning failure.",
            characterId
        );
        socket->Send("cdd", 101, 0, 4);
        return false;
    }
    if (!user->IsLoggedIn()) {
        // Log.Add(
        //     LOG_WARN,
        //     "[admin] Cannot send a private announcement to character %d: character is offline. Returning failure.",
        //     characterId
        // )
        L2_LOG_WARN(Log,
            "[admin] Cannot send a private announcement to character %d: character is offline; returning failure.",
            characterId
        );
        socket->Send("cdd", 101, 0, 0x0f);
        return false;
    }

    CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (server == nullptr) {
        // Log.Add(
        //     LOG_WARN,
        //     "[admin] Cannot send a private announcement to online character %d: L2Server is unavailable. Returning failure.",
        //     characterId
        // )
        L2_LOG_WARN(Log,
            "[admin] Cannot send a private announcement to online character %d: L2Server is unavailable; returning failure.",
            characterId
        );
        socket->Send("cdd", 101, 0, 0x13);
        return false;
    }

    server->Send("cSS", 0xbe, user->GetCharName(), messageUtf16.data());
    socket->Send("cd", 101, 1);
    return false;
}

bool SetPartyLocationPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0042527c, AQ 108, codec ddddS. Like AQ 101,
    // this current body does not check CAdminSocket's rejection flag.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddddS", &characterId, &x, &y, &z, 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, char id[%d]", "adminsocket.cpp", 0x20c5, characterId)
        // Log.Add(
        //     LOG_WARN,
        //     "[admin] Cannot move the party of character %d to (%d, %d, %d): character unavailable. Returning failure.",
        //     characterId, x, y, z
        // )
        L2_LOG_WARN(Log,
            "[admin] Cannot move the party of character %d to (%d, %d, %d): character unavailable; returning failure.",
            characterId, x, y, z
        );
        socket->Send("cdd", 108, 0, 4);
        return false;
    }
    if (!user->IsLoggedIn()) {
        // Log.Add(LOG_ERROR, "*cannot change location:%ls (%d,%d,%d)", user->GetCharName(), x, y, z)
        // Log.Add(
        //     LOG_WARN,
        //     "[admin] Cannot move the party of character %d (%s) to (%d, %d, %d): the character must be online. Returning failure.",
        //     characterId, WideToUtf8(user->GetCharName()).c_str(), x, y, z
        // )
        L2_LOG_WARN(Log,
            "[admin] Cannot move the party of character %d (%s) to (%d, %d, %d): the character must be online; returning failure.",
            characterId, Utf16ToUtf8(user->GetCharName()).c_str(), x, y, z
        );
        socket->Send("cdd", 108, 0, 1);
        return false;
    }

    CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (server != nullptr) {
        server->Send("chdddd", 0xc6, 0x1e, user->GetId(), x, y, z);
        socket->Send("cd", 108, 1);
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot forward party move for character %d to (%d, %d, %d): L2Server unavailable. This handler sends no reply.",
        //     characterId, x, y, z
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot forward party move for character %d to (%d, %d, %d): L2Server unavailable; this handler sends no reply.",
            characterId, x, y, z
        );
    }
    return false;
}

bool GmRegPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0042559c.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int builderLevel = 0;
    std::array<char16_t, 256> ignoredCharacterNameUtf16{};
    ::Disassemble(packet, "ddS", &characterId, &builderLevel, 0x200, ignoredCharacterNameUtf16.data());
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 110, 0, 4);
        return false;
    }
    if (!user->m_itemCheckPending) {
        socket->Send("cdd", 110, 0, 0x0f);
        return false;
    }

    CSocket* const serverSocket = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (serverSocket == nullptr) {
        socket->Send("cdd", 110, 0, 0x13);
        return false;
    }

    serverSocket->Send("cddd", 0x94, 0x0f, user->GetId(), builderLevel);
    socket->Send("cd", 110, 1);
    return false;
}

bool SetPledgeNameValuePacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0042640c, AQ 116, codec ddS. This body has no
    // ordinary request gate and forwards both the old value and requested
    // delta after CPledge's existing locked mutation owner succeeds.
    L2_GUARD_FUNCTION();

    int pledgeId = 0;
    int nameValueDelta = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddS", &pledgeId, &nameValueDelta, 0x200, auditTextUtf16.data());

    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        socket->Send("cdd", 116, 0, 0x18);
        return false;
    }

    const int oldNameValue = pledge->rootNameValue();
    if (!pledge->UpdateNameValue(nameValueDelta, 100)) {
        socket->Send("cdd", 116, 0, 1);
        return false;
    }

    CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (server != nullptr) {
        server->Send("chdddddd", 0xc6, 0x2c, 0, 0, pledgeId, oldNameValue, nameValueDelta, 0);
    }
    socket->Send("cd", 116, 1);
    return false;
}

bool ModVitalityPointPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00442a9c, AQ 138, codec dddddS. The target loads
    // the account before its later user-null branch. Preserving that source
    // order also keeps the shipping null-dereference defect for an
    // unknown character ID.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int subjobId = 0;
    int vitalityPoint = 0;
    int restoreCount = 0;
    int modificationType = 0;
    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(
        packet, "dddddS", &characterId, &subjobId, &vitalityPoint, &restoreCount, &modificationType, 0x200,
        ignoredAuditTextUtf16.data()
    );

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    CAccountSP account = CAccount::Load(static_cast<unsigned int>(user->GetAccountID()));
    if (!account) {
        socket->Send("cdd", 138, 0, 6);
        return false;
    }
    if (!user) {
        socket->Send("cdd", 138, 0, 4);
        return false;
    }

    CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (server != nullptr) {
        server->Send("cdd", 0x21, user->GetAccountID(), 0);
    }

    int loadedVitalityPoint = 0;
    int loadedRestoreCount = 0;
    int latestResetDateVP = 0;
    int latestResetDateRC = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&loadedVitalityPoint);
    connection.Bind(&loadedRestoreCount);
    connection.Bind(&latestResetDateVP);
    connection.Bind(&latestResetDateRC);
    constexpr const wchar_t* statement = L"{Call lin_LoadCharVitality (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&subjobId);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
        // Log.Add(LOG_ERROR,
        //     "[admin vitality] Cannot load current vitality for character %d, subjob %d, before applying points %d and restore count %d (modification type %d): execution or fetch failed. The game server may already have been notified. The vitality update is skipped and failure reason 22 is returned.",
        //     characterId, subjobId, vitalityPoint, restoreCount, modificationType)
        L2_LOG_ERROR(Log,
            "[admin vitality] Cannot load current vitality for character %d, subjob %d, before applying points %d and restore count %d (modification type %d): execution or fetch failed. The game server may already have been notified; the vitality update is skipped and failure reason 22 is returned.",
            characterId, subjobId, vitalityPoint, restoreCount, modificationType);
        socket->Send("cdd", 138, 0, 0x16);
        return false;
    }

    CVitalData data;
    if (modificationType == 0) {
        data.Init(
            runtime::AddInt32Wrapping(
                vitalityPoint,
                loadedVitalityPoint),
            runtime::AddInt32Wrapping(
                restoreCount,
                loadedRestoreCount),
            latestResetDateVP, latestResetDateRC
        );
    } else {
        data.Init(vitalityPoint, restoreCount, latestResetDateVP, latestResetDateRC);
    }
    user->UpdateVitalityByAdmin(data, characterId, subjobId);
    socket->Send("cd", 138, 1);
    return false;
}

bool ModItemPeriod(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00443038, AQ 139, codec dddQSddS. Only the
    // character ID, item ID and final two period components affect the
    // shipping body. Their sum follows the target's 32-bit wraparound.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int itemId = 0;
    int ignoredItemType = 0;
    std::int64_t ignoredAmount = 0;
    std::array<char16_t, 30> ignoredItemNameUtf16{};
    int firstPeriodComponent = 0;
    int secondPeriodComponent = 0;
    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(
        packet, "dddQSddS", &characterId, &itemId, &ignoredItemType, &ignoredAmount, 0x3c, ignoredItemNameUtf16.data(),
        &firstPeriodComponent, &secondPeriodComponent, 0x200, ignoredAuditTextUtf16.data()
    );

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 139, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 139, 0, 1);
        return false;
    }

    const int newPeriod = runtime::AddInt32Wrapping(
        firstPeriodComponent,
        secondPeriodComponent);
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{ Call lin_ModItemPeriod (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&itemId);
    connection.BindParameter(&newPeriod);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR,
        //     "[admin item duration] Cannot save period %d for item %d, requested by character %d: database execution failed. Sending failure reason 1.",
        //     newPeriod, itemId, characterId)
        L2_LOG_ERROR(Log,
            "[admin item duration] Cannot save period %d for item %d, requested by character %d: database execution failed; sending failure reason 1.",
            newPeriod, itemId, characterId);
        socket->Send("cdd", 139, 0, 1);
        return false;
    }

    socket->Send("cd", 139, 1);
    return false;
}

bool SetColorPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00443388, AQ 122, codec dddS. Selector zero owns
    // the character-name color and selector one owns the nickname color.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int color = 0;
    int colorType = 0;
    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(packet, "dddS", &characterId, &color, &colorType, 0x200, ignoredAuditTextUtf16.data());
    if (static_cast<std::uint32_t>(colorType) >= 2U) {
        socket->Send("cdd", 122, 0, 3);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 122, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 122, 0, 7);
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetColor (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&color);
    connection.BindParameter(&colorType);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR,
        //     "[admin color] Cannot save color %d (selector %d) for character %d: database execution failed. The cached color is unchanged and failure reason 1 is returned.",
        //     color, colorType, characterId)
        L2_LOG_ERROR(Log,
            "[admin color] Cannot save color %d (selector %d) for character %d: database execution failed; the cached color is unchanged and failure reason 1 is returned.",
            color, colorType, characterId);
        socket->Send("cdd", 122, 0, 1);
        return false;
    }

    if (colorType == 0) {
        user->m_characterNameColor = color;
    } else {
        user->m_nicknameColor = color;
    }
    socket->Send("cd", 122, 1);
    return false;
}

bool DisableAddedServicePacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00443c54, AQ 126, codec ddS. The second SQL call
    // is best-effort: only lin_SetUserService controls the admin result.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int serviceFlag = 0;
    std::array<char16_t, 256> ignoredAuditTextUtf16{};
    ::Disassemble(packet, "ddS", &characterId, &serviceFlag, 0x200, ignoredAuditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 126, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 126, 0, 7);
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* setService = L"{Call lin_SetUserService (?, ?)}";
    connection.Prepare(setService);
    connection.BindParameter(&characterId);
    connection.BindParameter(&serviceFlag);
    if (!connection.ExecutePrepared(setService)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot set service flag %d for character %d: database operation failed. Returning failure.",
        //     serviceFlag, characterId
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot set service flag %d for character %d: database operation failed; returning failure.",
            serviceFlag, characterId
        );
        socket->Send("cdd", 126, 0, 1);
        return false;
    }

    connection.ResetParam();
    int residentCharacterId = static_cast<int>(user->GetId());
    int historyAction = serviceFlag == 0 ? 0x1a : 0x10;
    constexpr const wchar_t* addHistory = L"{Call lin_AddPunishmentHistory (?, ?, ?, ?)}";
    connection.Prepare(addHistory);
    connection.BindParameter(user->GetCharName());
    connection.BindParameter(&residentCharacterId);
    connection.BindParameter(&historyAction);
    connection.BindParameter(user->GetAccountName());
    // connection.ExecutePrepared(addHistory)
    if (!connection.ExecutePrepared(addHistory)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot record service-change history for character %d (service flag %d, history action %d). The service database operation already executed. Cached state will still be updated and a success reply sent.",
        //     characterId, serviceFlag, historyAction
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot record service-change history for character %d (service flag %d, history action %d). The service database operation already executed; cached state will still be updated and a success reply sent.",
            characterId, serviceFlag, historyAction
        );
    }

    user->m_loadedInfo.serviceFlag = serviceFlag;
    socket->Send("cd", 126, 1);
    return false;
}

bool TransferPledgeMaster(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00443790, AQ 123, codec ddS. The admin path uses
    // a direct two-parameter DBConn call. It does not call
    // CPledgeDb's richer L2Server request helper.
    L2_GUARD_FUNCTION();

    int pledgeId = 0;
    int characterId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddS", &pledgeId, &characterId, 0x200, auditTextUtf16.data());

    const int worldId = runtime::process::CurrentDatabaseId();
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, worldId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, worldId);
    if (!pledge) {
        socket->Send("cdd", 123, 0, 0x18);
        return false;
    }
    if (!user) {
        socket->Send("cdd", 123, 0, 4);
        return false;
    }

    DBConn connection(worldId);
    if (user->GetPledgeID() != static_cast<std::uint32_t>(pledgeId)) {
        socket->Send("cdd", 123, 0, 0x11);
        return false;
    }

    const int newMasterId = static_cast<int>(user->GetId());
    constexpr const wchar_t* statement = L"{Call lin_RequestPledgeMasterTransfer (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&newMasterId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR,
        //     "[admin clan] Cannot request leadership transfer for clan %d to character %d in world %d: database execution failed. Sending failure reason 1.",
        //     pledgeId, newMasterId, worldId)
        L2_LOG_ERROR(Log,
            "[admin clan] Cannot request leadership transfer for clan %d to character %d in world %d: database execution failed; sending failure reason 1.",
            pledgeId, newMasterId, worldId);
        socket->Send("cdd", 123, 0, 1);
        return false;
    }

    socket->Send("cd", 123, 1);
    return false;
}

bool GetAccountId(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00428fc4.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 256> characterNameUtf16{};
    std::array<char16_t, 256> ignoredAccountNameUtf16{};
    ::Disassemble(packet, "SS", 0x200, characterNameUtf16.data(), 0x200, ignoredAccountNameUtf16.data());

    CUserSP user = g_userDb.GetUser(characterNameUtf16.data(), false);
    socket->Send("cd", 131, user ? user->GetAccountID() : 0);
    return false;
}

bool AddSubPledgeSKillPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int first = 0;
    int second = 0;
    int third = 0;
    int fourth = 0;
    std::array<char16_t, 256> characterName{};
    ::Disassemble(packet, "ddddS", &first, &second, &third, &fourth, 0x200, characterName.data());
    socket->Send("cdd", 134, 0, 0x0d);
    return false;
}

bool DelSubPledgeSKillPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int first = 0;
    int second = 0;
    int third = 0;
    std::array<char16_t, 256> characterName{};
    ::Disassemble(packet, "dddS", &first, &second, &third, 0x200, characterName.data());
    socket->Send("cdd", 135, 0, 0x0d);
    return false;
}

bool ModSubPledgeSkillPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int first = 0;
    int second = 0;
    int third = 0;
    int fourth = 0;
    std::array<char16_t, 256> characterName{};
    ::Disassemble(packet, "ddddS", &first, &second, &third, &fourth, 0x200, characterName.data());
    socket->Send("cdd", 136, 0, 0x0d);
    return false;
}

bool ModPvpPointPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0042889c.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int point = 0;
    int replace = 0;
    std::array<char16_t, 256> ignoredCharacterNameUtf16{};
    ::Disassemble(packet, "dddS", &characterId, &point, &replace, 0x200, ignoredCharacterNameUtf16.data());
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 137, 0, 4);
        return false;
    }

    int resultingPoint = point;
    if (replace == 0) {
        resultingPoint = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(user->LoadUserPoint(5)) + static_cast<std::uint32_t>(point)
        );
    }
    user->SaveUserPoint(5, resultingPoint);
    socket->Send("cd", 137, 1);
    return false;
}

bool ModBOTPointPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00428ac8.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int point = 0;
    int replace = 0;
    std::array<char16_t, 256> ignoredCharacterNameUtf16{};
    ::Disassemble(packet, "dddS", &characterId, &point, &replace, 0x200, ignoredCharacterNameUtf16.data());
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 147, 0, 4);
        return false;
    }

    int resultingPoint = point;
    if (replace == 0) {
        resultingPoint = runtime::AddInt32Wrapping(
            user->GetBotReport(),
            point);
    }
    user->SetBotReport(resultingPoint);
    socket->Send("cd", 147, 1);
    return false;
}

bool DeleteReceivedMail(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00442518, AQ 148, codec ddS. There is no request
    // gate. The third SQL parameter is the constant received-mailbox type 0.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int mailId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddS", &characterId, &mailId, 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 148, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 148, 0, 7);
        return false;
    }

    int mailboxType = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeleteMail (?, ?, ? )}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&mailId);
    connection.BindParameter(&mailboxType);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR,
        //     "[admin mail] Cannot delete received mail %d for character %d: database execution failed. Sending failure reason 1 and skipping the deletion audit.",
        //     mailId, characterId)
        L2_LOG_ERROR(Log,
            "[admin mail] Cannot delete received mail %d for character %d: database execution failed; sending failure reason 1 and skipping the deletion audit.",
            mailId, characterId);
        socket->Send("cdd", 148, 0, 1);
        return false;
    }

    socket->Send("cd", 148, 1);
    CLogFormatter formatter;
    formatter.WriteLog(
        0x2a3, 0, 0, user->GetId(), user->GetAccountID(), user->GetX(), user->GetY(), user->GetZ(), u"", u"", u"", mailId, 0,
        0, 0, 0, 0, 0, 0, 0, 0, auditTextUtf16.data(), auditTextUtf16.data(), AdminLogText(user->GetCharName()).c_str(),
        AdminLogText(user->GetAccountName()).c_str(), 0
    );
    return false;
}

bool DeletePostPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004441c0, AQ 151, codec dddSS. This direct
    // administrative path deletes only attachment-free posts. The distinct
    // AdminUtil::CancelPostProc owner performs attachment restoration.
    L2_GUARD_FUNCTION();

    int postId = 0;
    int ignoredRequestValue1 = 0;
    int ignoredRequestValue2 = 0;
    std::array<char16_t, 256> requestedTargetNameUtf16{};
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(
        packet, "dddSS", &postId, &ignoredRequestValue1, &ignoredRequestValue2, 0x200, requestedTargetNameUtf16.data(),
        0x200, auditTextUtf16.data()
    );

    int senderId = 0;
    int receiverId = 0;
    int trade = 0;
    std::array<char16_t, 61> title{};
    std::array<char16_t, 1001> contents{};
    int process = 0;
    int returnable = 0;
    int returned = 0;
    int senderDeleted = 0;
    int receiverDeleted = 0;
    int sentTime = 0;
    int itemCount = 0;
    std::int64_t tradeMoney = 0;
    std::int64_t postFee = 0;
    int senderType = 0;
    int recordedId = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&senderId);
    connection.Bind(&receiverId);
    connection.Bind(&trade);
    connection.Bind(title.data(), 0x7a);
    connection.Bind(contents.data(), 0x7d2);
    connection.Bind(&process);
    connection.Bind(&returnable);
    connection.Bind(&returned);
    connection.Bind(&senderDeleted);
    connection.Bind(&receiverDeleted);
    connection.Bind(&sentTime);
    connection.Bind(&itemCount);
    connection.BindNullable(&tradeMoney, nullptr);
    connection.BindNullable(&postFee, nullptr);
    connection.BindNullable(&senderType, nullptr);
    connection.BindNullable(&recordedId, nullptr);
    constexpr const wchar_t* loadStatement = L"{Call lin_LoadPostByPostId (?)}";
    connection.Prepare(loadStatement);
    connection.BindParameter(&postId);
    // if (!connection.ExecutePrepared(loadStatement) || !connection.Fetch()) {
    const bool postQuerySucceeded = connection.ExecutePrepared(loadStatement);
    if (!postQuerySucceeded || !connection.Fetch()) {
        // Log.Add(postQuerySucceeded ? LOG_WARN : LOG_ERROR,
        //     "[admin mail] Cannot obtain mail %d for deletion: %s. Deletion is skipped and failure reason 1 is returned.",
        //     postId, postQuerySucceeded ? "no mail record was fetched" : "database execution failed")
        L2_LOG(Log, postQuerySucceeded ? LOG_WARN : LOG_ERROR,
            "[admin mail] Cannot obtain mail %d for deletion: %s; deletion is skipped and failure reason 1 is returned.",
            postId, postQuerySucceeded ? "no mail record was fetched" : "database execution failed");
        socket->Send("cdd", 151, 0, 1);
        return false;
    }
    connection.UnBind();

    if (itemCount > 0) {
        // Log.Add(LOG_WARN,
        //     "[admin mail] Direct deletion of mail %d rejected: it has %d attachments. Use the cancellation operation to handle attachments. Sending failure reason 1.",
        //     postId, itemCount)
        L2_LOG_WARN(Log,
            "[admin mail] Direct deletion of mail %d rejected: it has %d attachments. Use the cancellation operation to handle attachments; sending failure reason 1.",
            postId, itemCount);
        socket->Send("cdd", 151, 0, 1);
        return false;
    }

    CUserSP sender;
    if (senderId > 0) {
        sender = g_userDb.GetUser(senderId, true, false, runtime::process::CurrentDatabaseId());
        if (!sender) {
            socket->Send("cdd", 151, 0, 4);
            return false;
        }
        if (sender->IsLoggedIn()) {
            socket->Send("cdd", 151, 0, 7);
            return false;
        }
    }

    CUserSP receiver = g_userDb.GetUser(receiverId, true, false, runtime::process::CurrentDatabaseId());
    if (!receiver) {
        socket->Send("cdd", 151, 0, 4);
        return false;
    }
    if (receiver->IsLoggedIn()) {
        socket->Send("cdd", 151, 0, 7);
        return false;
    }

    DBConn deleteConnection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* deleteStatement = L"{Call lin_DeletePost (?)}";
    deleteConnection.Prepare(deleteStatement);
    deleteConnection.BindParameter(&postId);
    if (!deleteConnection.ExecutePrepared(deleteStatement)) {
        // Log.Add(LOG_ERROR,
        //     "[admin mail] Cannot delete mail %d (sender %d, receiver %d): database execution failed. Sending failure reason 1 and skipping the deletion audit.",
        //     postId, senderId, receiverId)
        L2_LOG_ERROR(Log,
            "[admin mail] Cannot delete mail %d (sender %d, receiver %d): database execution failed; sending failure reason 1 and skipping the deletion audit.",
            postId, senderId, receiverId);
        socket->Send("cdd", 151, 0, 1);
        return false;
    }

    socket->Send("cd", 151, 1);
    const CUserSP& logUser = senderId < 1 ? receiver : sender;
    const std::u16string titleUtf16 = std::u16string(title.data());
    const std::u16string receiverNameUtf16 = senderId < 1 ? std::u16string{} : AdminLogText(receiver->GetCharName());
    const std::u16string logUserNameUtf16 = AdminLogText(logUser->GetCharName());
    const std::u16string logAccountNameUtf16 = AdminLogText(logUser->GetAccountName());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x2a2, 0, 0, logUser->GetId(), logUser->GetAccountID(), logUser->GetX(), logUser->GetY(), logUser->GetZ(),
        titleUtf16.c_str(), requestedTargetNameUtf16.data(), receiverNameUtf16.c_str(), postId, trade, itemCount, tradeMoney,
        process, 0, 0, 0, 0, 0, auditTextUtf16.data(), auditTextUtf16.data(), logUserNameUtf16.c_str(),
        logAccountNameUtf16.c_str(), 0
    );
    return false;
}

bool CheckCacheD(CAdminSocket* socket, const std::uint8_t*) {
    L2_GUARD_FUNCTION();
    socket->Send(
        "cddd", 155, 1, CUserDb::s_maxUsersOnline.load(std::memory_order_relaxed),
        CUserDb::s_usersOnline.load(std::memory_order_relaxed)
    );
    return false;
}

bool SetPetitionDisableTime(CAdminSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int first = 0;
    int second = 0;
    std::array<char16_t, 256> characterName{};
    ::Disassemble(packet, "ddS", &first, &second, 0x200, characterName.data());

    CSocket* const serverSocket = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (serverSocket == nullptr) {
        socket->Send("cdd", 159, 0, 0x13);
        return false;
    }

    serverSocket->Send("cddd", 0x94, 0x19, first, second);
    socket->Send("cd", 159, 1);
    return false;
}

bool WithdrawPremiumItemAtNPC(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00444df4, AQ 158, codec dQS. An absent or empty
    // account falls through to SQL. Only resident characters
    // found on a nonempty account participate in the online rejection.
    L2_GUARD_FUNCTION();

    int accountId = 0;
    std::int64_t warehouseNumber = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dQS", &accountId, &warehouseNumber, 0x200, auditTextUtf16.data());

    CAccountSP account = CAccount::Load(static_cast<unsigned int>(accountId));
    if (account && account->GetCharCount() != 0) {
        struct FindLoggedInAccountCharacter {
            bool Do(CUser* user) {
                if (user && user->IsLoggedIn())
                    loggedIn = true;
                return true;
            }

            bool loggedIn = false;
        } finder;

        {
            L2_READ_GUARD(*account.operator->());
            account->GetChars().FOR_ALL(&finder, L"adminsocket.cpp", 0x28a4);
        }
        if (finder.loggedIn) {
            socket->Send("cdd", 158, 0, 7);
            return false;
        }
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_WithdrawPremiumItemAtNPC (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&accountId);
    connection.BindParameter(&warehouseNumber);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR,
        //     "[admin premium items] Withdrawal query failed for account %d, warehouse number %lld. Sending failure reason 6 and skipping the withdrawal audit.",
        //     accountId, static_cast<long long>(warehouseNumber))
        L2_LOG_ERROR(Log,
            "[admin premium items] Withdrawal query failed for account %d, warehouse number %lld; sending failure reason 6 and skipping the withdrawal audit.",
            accountId, static_cast<long long>(warehouseNumber));
        socket->Send("cdd", 158, 0, 6);
        return false;
    }

    socket->Send("cd", 158, 1);
    CLogFormatter formatter;
    formatter.WriteLog(
        0x2a4, 0, 0, 0, accountId, 0, 0, 0, u"", u"", u"", warehouseNumber, 0, 0, 0, 0, 0, 0, 0, 0, 0, auditTextUtf16.data(),
        auditTextUtf16.data(), u"", u"", 0
    );
    return false;
}

bool SetRewardNpc(CAdminSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int first = 0;
    int second = 0;
    int third = 0;
    ::Disassemble(packet, "ddd", &first, &second, &third);
    socket->Send("cdd", 161, 0, 0x0d);
    return false;
}

bool MoveItemProc(
    CAdminSocket* socket,
    int replyOpcode,
    int sourceCharacterId,
    int itemId,
    std::int64_t amount,
    int destinationCharacterId,
    int destinationWarehouse,
    const wchar_t* auditText
) {
    // Exact current Cached.exe 0x004266ac. The source warehouse encoded in
    // the item selects ordinary inventory/warehouse, premium seizure or
    // pledge seizure. All successful branches share the same 0x280 audit.
    L2_GUARD_FUNCTION();

    const auto fail = [&](int reason) {
        socket->Send("cdd", replyOpcode, 0, reason);
        return false;
    };

    if (sourceCharacterId == destinationCharacterId) {
        return fail(0x10);
    }

    CUserSP sourceUser = g_userDb.GetUser(sourceCharacterId, true, false, runtime::process::CurrentDatabaseId());
    CUserSP destinationUser = g_userDb.GetUser(destinationCharacterId, true, false, runtime::process::CurrentDatabaseId());
    if (!sourceUser || !destinationUser)
        return fail(4);
    if (sourceUser->m_loggedIn || destinationUser->m_loggedIn) {
        return fail(7);
    }
    if (sourceUser->GetReloadFlag() || destinationUser->GetReloadFlag()) {
        return fail(0x3c);
    }

    const int sourceWarehouse = sourceUser->GetItemWare(itemId);
    if (sourceWarehouse < 0)
        return fail(1);

    CItemSP item;
    int itemType = 0;
    bool moved = false;

    if (sourceWarehouse == 0 || sourceWarehouse == 1) {
        CWareHouseSP source = sourceUser->GetWareHouse(sourceWarehouse, true);
        CWareHouseSP destination = destinationUser->GetWareHouse(destinationWarehouse, true);
        if (!source || !destination)
            return fail(5);

        item = source->GetItem(itemId);
        if (!item)
            return fail(1);
        itemType = static_cast<int>(item->ItemType());
        moved = source->MoveItemToChar(itemId, amount, destination.get());
    } else if (sourceWarehouse == 1001) {
        CWareHouseSP destination = destinationUser->GetWareHouse(destinationWarehouse, true);
        if (!destination)
            return fail(5);

        item = CItem::Load(static_cast<unsigned int>(itemId));
        if (!item)
            return fail(1);
        itemType = static_cast<int>(item->ItemType());
        moved = sourceUser->SetSeizedItemOwner(itemId, amount, static_cast<int>(destinationUser->GetId()), destination);
    } else if (sourceWarehouse == 1002) {
        CPledgeSP pledge =
            g_pledgeDb.GetPledge(static_cast<int>(sourceUser->GetPledgeID()), false, runtime::process::CurrentDatabaseId());
        if (!pledge)
            return fail(0x18);

        CWareHouseSP source = pledge->GetWareHouse();
        CWareHouseSP destination = destinationUser->GetWareHouse(destinationWarehouse, true);
        if (!source || !destination)
            return fail(5);

        item = CItem::Load(static_cast<unsigned int>(itemId));
        if (!item)
            return fail(1);
        itemType = static_cast<int>(item->ItemType());
        if (!source->SeizeItem(2, itemId, 2, 1))
            return fail(1);
        moved = source->MoveItemToChar(itemId, amount, destination.get());
    } else {
        return fail(1);
    }

    // Shipping composes this field after the mutation call, even on its
    // failure path, while the kept CItemSP keeps the object alive.
    const std::u16string itemLog = MakeAdminMoveItemLogText(item->m_intensiveItemType, *item.get());
    if (!moved)
        return fail(1);

    socket->Send("cd", replyOpcode, 1);
    WriteAdminMoveItemAudit(*sourceUser.get(), *destinationUser.get(), itemLog, itemType, amount, auditText, itemId);
    return false;
}

bool MovePremiumItemPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00428114, AQ 143, codec ddQddS. This wrapper does
    // not test the request-rejection byte.
    L2_GUARD_FUNCTION();
    int sourceCharacterId = 0;
    int itemId = 0;
    std::int64_t amount = 0;
    int destinationCharacterId = 0;
    int destinationWarehouse = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(
        packet, "ddQddS", &sourceCharacterId, &itemId, &amount, &destinationCharacterId, &destinationWarehouse, 0x200,
        auditTextUtf16.data()
    );
    std::wstring auditText = Utf16ToWide(auditTextUtf16.data());
    return MoveItemProc(
        socket, 143, sourceCharacterId, itemId, amount, destinationCharacterId, destinationWarehouse, auditText.c_str()
    );
}

bool MoveItem3Packet(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00428284, AQ 117, codec ddQddS.
    L2_GUARD_FUNCTION();
    int sourceCharacterId = 0;
    int itemId = 0;
    std::int64_t amount = 0;
    int destinationCharacterId = 0;
    int destinationWarehouse = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(
        packet, "ddQddS", &sourceCharacterId, &itemId, &amount, &destinationCharacterId, &destinationWarehouse, 0x200,
        auditTextUtf16.data()
    );
    std::wstring auditText = Utf16ToWide(auditTextUtf16.data());
    return MoveItemProc(
        socket, 117, sourceCharacterId, itemId, amount, destinationCharacterId, destinationWarehouse, auditText.c_str()
    );
}

bool SeizePremiumItemPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00442288, AQ 141, codec ddQS.
    L2_GUARD_FUNCTION();
    int option = 0;
    int itemId = 0;
    std::int64_t amount = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddQS", &option, &itemId, &amount, 0x200, auditTextUtf16.data());
    std::wstring auditText = Utf16ToWide(auditTextUtf16.data());
    return SeizeItemProc(socket, 141, option, itemId, amount, auditText.data());
}

bool SeizeItem3Packet(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004423d0, AQ 118, codec ddQS.
    L2_GUARD_FUNCTION();
    int option = 0;
    int itemId = 0;
    std::int64_t amount = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddQS", &option, &itemId, &amount, 0x200, auditTextUtf16.data());
    std::wstring auditText = Utf16ToWide(auditTextUtf16.data());
    return SeizeItemProc(socket, 118, option, itemId, amount, auditText.data());
}

bool DelPremiumItemPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0042dc1c, AQ 142, codec dQdS.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 142, 0, 0x1e);
        return false;
    }
    int itemId = 0;
    std::int64_t amount = 0;
    int deletePet = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dQdS", &itemId, &amount, &deletePet, 0x200, auditTextUtf16.data());
    std::wstring auditText = Utf16ToWide(auditTextUtf16.data());
    return DelItemProc(socket, 142, itemId, amount, deletePet, auditText.data());
}

bool DelItem2Packet(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0042dd8c, AQ 54, codec dQdS.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 54, 0, 0x1e);
        return false;
    }
    int itemId = 0;
    std::int64_t amount = 0;
    int deletePet = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dQdS", &itemId, &amount, &deletePet, 0x200, auditTextUtf16.data());
    std::wstring auditText = Utf16ToWide(auditTextUtf16.data());
    return DelItemProc(socket, 54, itemId, amount, deletePet, auditText.data());
}

bool SeizePostItemPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004420e4, AQ 152, codec ddQdS. There is no
    // request-rejection gate on this wrapper.
    L2_GUARD_FUNCTION();
    int option = 0;
    int itemId = 0;
    std::int64_t amount = 0;
    int postId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddQdS", &option, &itemId, &amount, &postId, 0x200, auditTextUtf16.data());
    std::wstring auditText = Utf16ToWide(auditTextUtf16.data());
    int reason = 1;
    if (!AdminUtil::CancelPostProc(postId, auditText.data(), &reason)) {
        socket->Send("cdd", 152, 0, reason);
        return false;
    }
    return SeizeItemProc(socket, 152, option, itemId, amount, auditText.data());
}

bool DeletePostItemPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0042defc, AQ 153, codec dQddS.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 153, 0, 0x1e);
        return false;
    }
    int itemId = 0;
    std::int64_t amount = 0;
    int deletePet = 0;
    int postId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dQddS", &itemId, &amount, &deletePet, &postId, 0x200, auditTextUtf16.data());
    std::wstring auditText = Utf16ToWide(auditTextUtf16.data());
    int reason = 1;
    if (!AdminUtil::CancelPostProc(postId, auditText.data(), &reason)) {
        socket->Send("cdd", 153, 0, reason);
        return false;
    }
    return DelItemProc(socket, 153, itemId, amount, deletePet, auditText.data());
}

bool MovePostItemPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004283f4, AQ 154, codec ddQdddS. There is no
    // request-rejection gate on this wrapper.
    L2_GUARD_FUNCTION();
    int sourceCharacterId = 0;
    int itemId = 0;
    std::int64_t amount = 0;
    int destinationCharacterId = 0;
    int destinationWarehouse = 0;
    int postId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(
        packet, "ddQdddS", &sourceCharacterId, &itemId, &amount, &destinationCharacterId, &destinationWarehouse, &postId,
        0x200, auditTextUtf16.data()
    );
    std::wstring auditText = Utf16ToWide(auditTextUtf16.data());
    int reason = 1;
    if (!AdminUtil::CancelPostProc(postId, auditText.data(), &reason)) {
        socket->Send("cdd", 154, 0, reason);
        return false;
    }
    return MoveItemProc(
        socket, 154, sourceCharacterId, itemId, amount, destinationCharacterId, destinationWarehouse, auditText.c_str()
    );
}

bool SeizeItemProc(CAdminSocket* socket, int replyOpcode, int option, int itemId, std::int64_t amount, wchar_t auditText[]) {
    // Current Cached.exe 0x004416a4. This helper removes the process-cache
    // entry before routing by the item's original warehouse and keeps the
    // current option-to-commission-warehouse mapping unchanged.
    L2_GUARD_FUNCTION();

    CItemSP item = CItem::Load(static_cast<unsigned int>(itemId));
    if (!item) {
        socket->Send("cdd", replyOpcode, 0, 1);
        return false;
    }

    const int ownerId = static_cast<int>(item->OwnerID());
    const int originalWarehouse = item->Warehouse();
    const int itemType = static_cast<int>(item->ItemType());
    std::array<char16_t, 256> baseAttributeText{};
    BaseAttribute baseAttribute(item->Attribute());
    baseAttribute.GetLogString(baseAttributeText.data());
    const std::u16string baseAttributeLog = AdminLogText(baseAttributeText.data());
    const std::u16string variationLog = MakeAdminVariationLogText(item->m_intensiveItemType, item->OptionKey());
    item->Delete(false, false);

    bool seized = false;
    int reason = 1;
    std::u16string ownerName;
    std::u16string accountName;

    const auto mappedWarehouse = [&](int optionOneWarehouse, int optionTwoWarehouse) {
        if (option == 1)
            return optionOneWarehouse;
        if (option == 2)
            return optionTwoWarehouse;
        return 0;
    };

    if (originalWarehouse == 0 || originalWarehouse == 1 || originalWarehouse == 1001) {
        CUserSP user = g_userDb.GetUser(ownerId, true, false, runtime::process::CurrentDatabaseId());
        if (user) {
            if (user->IsLoggedIn()) {
                reason = 7;
            } else if (user->GetReloadFlag()) {
                reason = 0x3c;
            } else {
                const int liveWarehouse = originalWarehouse == 1001 ? 1 : originalWarehouse;
                CWareHouseSP warehouse = user->GetWareHouse(liveWarehouse, true);
                if (!warehouse) {
                    reason = 5;
                } else {
                    seized = warehouse->SeizeItem(option, itemId, amount, mappedWarehouse(1001, 1), 1);
                    if (!seized)
                        reason = 0x36;
                    ownerName = AdminLogText(user->GetCharName());
                    accountName = AdminLogText(user->GetAccountName());
                }
            }
        }
    } else if (originalWarehouse == 2 || originalWarehouse == 1002) {
        CPledgeSP pledge = g_pledgeDb.GetPledge(ownerId, false, runtime::process::CurrentDatabaseId());
        if (pledge) {
            CWareHouseSP warehouse = pledge->GetWareHouse();
            if (!warehouse) {
                reason = 5;
            } else {
                seized = warehouse->SeizeItem(option, itemId, amount, mappedWarehouse(1002, 2), 1);
                if (!seized)
                    reason = 0x36;
                ownerName = AdminLogText(pledge->GetName());
                accountName = ownerName;
            }
        }
    } else if (originalWarehouse == 3 || originalWarehouse == 1003) {
        CCastleSP castle = g_castleDb.GetCastle(ownerId);
        if (castle) {
            CWareHouseSP warehouse = castle->GetWareHouse();
            if (!warehouse) {
                reason = 5;
            } else {
                seized = warehouse->SeizeItem(option, itemId, amount, mappedWarehouse(1003, 3), 1);
                if (!seized)
                    reason = 0x36;
            }
        }
    } else if (originalWarehouse == 5 || originalWarehouse == 1004) {
        int destinationWarehouse = 1001;
        if (option != 1) {
            destinationWarehouse = 0;
            if (option == 2) {
                // Log.Add(LOG_ERROR, "unavailable entry [%d]", originalWarehouse)
                // Log.Add(
                //     LOG_WARN,
                //     "[admin] Item seizure request rejected for item %d (owner %d): option %d is unsupported for warehouse %d.",
                //     itemId, ownerId, option, originalWarehouse
                // )
                L2_LOG_WARN(Log,
                    "[admin] Item seizure request rejected for item %d (owner %d): option %d is unsupported for warehouse %d.",
                    itemId, ownerId, option, originalWarehouse
                );
                socket->Send("cdd", replyOpcode, 0, reason);
                return false;
            }
        }

        CUserSP user = g_userDb.GetUser(ownerId, true, false, runtime::process::CurrentDatabaseId());
        if (user) {
            if (user->IsLoggedIn()) {
                reason = 7;
            } else {
                seized = ExecuteAdminSeizeItem2(option, destinationWarehouse, itemId);
                if (!seized)
                    reason = 0x36;
            }
        }
    }

    if (!seized) {
        socket->Send("cdd", replyOpcode, 0, reason);
        return false;
    }

    socket->Send("cd", replyOpcode, 1);
    WriteAdminSeizeItemAudit(
        ownerId, baseAttributeLog, variationLog, option, originalWarehouse, itemType, amount, auditText, ownerName,
        accountName, itemId
    );
    return false;
}

bool DelItemProc(
    CAdminSocket* socket,
    int replyOpcode,
    int itemId,
    std::int64_t amount,
    int deletePet,
    wchar_t* auditText [[maybe_unused]]
) {
    // Current Cached.exe 0x0042cb64. auditText is kept in the exact
    // source signature but the shipping body does not read it.
    L2_GUARD_FUNCTION();

    CItemSP item = CItem::Load(static_cast<unsigned int>(itemId));
    if (!item) {
        socket->Send("cdd", replyOpcode, 0, 1);
        return false;
    }

    const int ownerId = static_cast<int>(item->OwnerID());
    if (ownerId == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "DelItemProc nCharId=0. Item already deleted "
        //     "[adminsocket.cpp][%d]",
        //     0x1022
        // )
        // Log.Add(
        //     LOG_WARN,
        //     "[admin] Cannot delete item %d: owner ID is zero. Returning failure.",
        //     itemId
        // )
        L2_LOG_WARN(Log,
            "[admin] Cannot delete item %d: owner ID is zero; returning failure.",
            itemId
        );
        socket->Send("cdd", replyOpcode, 0, 1);
        return false;
    }

    const int originalWarehouse = item->Warehouse();
    const int itemType = static_cast<int>(item->ItemType());
    std::array<char16_t, 256> baseAttributeText{};
    BaseAttribute baseAttribute(item->Attribute());
    baseAttribute.GetLogString(baseAttributeText.data());
    const std::u16string baseAttributeLog = AdminLogText(baseAttributeText.data());
    const std::u16string variationLog = MakeAdminVariationLogText(item->m_intensiveItemType, item->OptionKey());

    bool deleted = false;
    int reason = 1;

    if (originalWarehouse == 0 || originalWarehouse == 1) {
        CUserSP user = g_userDb.GetUser(ownerId, true, false, runtime::process::CurrentDatabaseId());
        if (!user) {
            // Log.Add(
            //     LOG_ERROR, "DelItemProc GetUser error [adminsocket.cpp][%d]",
            //     originalWarehouse == 0 ? 0x104c : 0x106c
            // )
            // Log.Add(
            //     LOG_ERROR,
            //     "[admin] Cannot delete item %d from warehouse %d: owner character %d unavailable. Returning failure.",
            //     itemId, originalWarehouse, ownerId
            // )
            L2_LOG_ERROR(Log,
                "[admin] Cannot delete item %d from warehouse %d: owner character %d unavailable; returning failure.",
                itemId, originalWarehouse, ownerId
            );
        } else if (user->IsLoggedIn()) {
            reason = 7;
        } else if (user->GetReloadFlag()) {
            reason = 0x3c;
        } else {
            CWareHouseSP warehouse = user->GetWareHouse(originalWarehouse, true);
            if (warehouse) {
                deleted = warehouse->DeleteItem(itemId, amount);
            }
            if (deleted)
                item->Delete(false, false);
        }
    } else if (originalWarehouse == 2) {
        CPledgeSP pledge = g_pledgeDb.GetPledge(ownerId, false, runtime::process::CurrentDatabaseId());
        if (pledge) {
            CWareHouseSP warehouse = pledge->GetWareHouse();
            if (warehouse) {
                deleted = warehouse->DeleteItem(itemId, amount);
            }
            if (deleted)
                item->Delete(false, false);
        }
    } else if (originalWarehouse == 3) {
        CCastleSP castle = g_castleDb.GetCastle(ownerId);
        if (castle) {
            CWareHouseSP warehouse = castle->GetWareHouse();
            if (warehouse) {
                deleted = warehouse->DeleteItem(itemId, amount);
            }
            if (deleted)
                item->Delete(false, false);
        }
    } else if (originalWarehouse == 5) {
        CPetSP pet = g_petDb.GetPet(ownerId, runtime::process::CurrentDatabaseId());
        if (pet) {
            CPetWarehouseSP warehouse = pet->GetWarehouse();
            if (warehouse) {
                deleted = warehouse->DeleteItem(itemId, amount);
            }
            if (deleted)
                item->Delete(false, false);
        }
    } else if (originalWarehouse == 1001 || originalWarehouse == 1002) {
        const std::int64_t remainingAmount = runtime::SubtractInt64Wrapping(item->Amount(), amount);
        if (remainingAmount < 1) {
            if (!item->Delete(true, false)) {
                // Log.Add(
                //     LOG_ERROR, "[adminsocket.cpp][%d] item delete fail[%d]",
                //     originalWarehouse == 1001 ? 0x10b1 : 0x10dc, itemId
                // )
                // Log.Add(
                //     LOG_ERROR,
                //     "[admin] Cannot delete item %d (owner %d, warehouse %d, requested amount %lld, remaining amount %lld). The existing handler continues as if the item was deleted.",
                //     itemId, ownerId, originalWarehouse, static_cast<long long>(amount), static_cast<long long>(remainingAmount)
                // )
                L2_LOG_ERROR(Log,
                    "[admin] Cannot delete item %d (owner %d, warehouse %d, requested amount %lld, remaining amount %lld); the existing handler continues as if the item was deleted.",
                    itemId, ownerId, originalWarehouse, static_cast<long long>(amount), static_cast<long long>(remainingAmount)
                );
            }
        } else {
            item->SetAmount(remainingAmount);
            // item->Save(true)
            if (!item->Save(true)) {
                // Log.Add(
                //     LOG_ERROR,
                //     "[admin] Cannot save item %d after changing its amount (owner %d, warehouse %d, requested removal %lld, computed remainder %lld). The existing handler continues as if deletion succeeded.",
                //     itemId, ownerId, originalWarehouse, static_cast<long long>(amount), static_cast<long long>(remainingAmount)
                // )
                L2_LOG_ERROR(Log,
                    "[admin] Cannot save item %d after changing its amount (owner %d, warehouse %d, requested removal %lld, computed remainder %lld); the existing handler continues as if deletion succeeded.",
                    itemId, ownerId, originalWarehouse, static_cast<long long>(amount), static_cast<long long>(remainingAmount)
                );
            }
        }

        CUserSP auditUser;
        if (originalWarehouse == 1001) {
            auditUser = g_userDb.GetUser(ownerId, true, false, runtime::process::CurrentDatabaseId());
            if (!auditUser) {
                // Log.Add(LOG_ERROR, "DelItemProc GetUser error [adminsocket.cpp][%d]", 0x10cc)
                // Log.Add(
                //     LOG_ERROR,
                //     "[admin] Deletion audit omitted for item %d: owner character %d unavailable after the item operation (warehouse %d).",
                //     itemId, ownerId, originalWarehouse
                // )
                L2_LOG_ERROR(Log,
                    "[admin] Deletion audit omitted for item %d: owner character %d unavailable after the item operation (warehouse %d).",
                    itemId, ownerId, originalWarehouse
                );
            }
        } else {
            CPledgeSP pledge = g_pledgeDb.GetPledge(ownerId, false, runtime::process::CurrentDatabaseId());
            if (pledge) {
                auditUser =
                    g_userDb.GetUser(static_cast<int>(pledge->rullerId()), true, false, runtime::process::CurrentDatabaseId());
                if (!auditUser) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "DelItemProc GetUser error "
                    //     "[adminsocket.cpp][%d]",
                    //     0x10fb
                    // )
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "[admin] Deletion audit omitted for item %d: leader character of clan %d unavailable after the item operation (warehouse %d).",
                    //     itemId, ownerId, originalWarehouse
                    // )
                    L2_LOG_ERROR(Log,
                        "[admin] Deletion audit omitted for item %d: leader character of clan %d unavailable after the item operation (warehouse %d).",
                        itemId, ownerId, originalWarehouse
                    );
                }
            }
        }

        if (auditUser) {
            WriteAdminDeleteItemAudit(
                auditUser.get(), baseAttributeLog, variationLog, itemType, amount, remainingAmount, itemId
            );
        }
        deleted = true;
    } else {
        reason = 3;
    }

    if (!deleted) {
        socket->Send("cdd", replyOpcode, 0, reason);
        return false;
    }

    if (deletePet != 0 && !g_petDb.DeletePet(itemId, true, runtime::process::CurrentDatabaseId())) {
        socket->Send("cdd", replyOpcode, 0, reason);
        return false;
    }

    socket->Send("cd", replyOpcode, 1);
    return false;
}

bool RequestWritePost(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00446d6c. This is the administrative post owner,
    // not the distinct L2Server RequestWritePost at 0x00560884.
    L2_GUARD_FUNCTION();

    int senderId = 0;
    std::array<char16_t, 25> receiverNameUtf16{};
    int trade = 0;
    std::array<char16_t, 61> titleUtf16{};
    std::array<char16_t, 1001> contentsUtf16{};
    // The seventh decoded field is kept because it is part of the
    // shipping dSdSSSd wire contract, although the current body never reads
    // it after Disassemble.
    std::array<char16_t, 256> ignoredTextUtf16{};
    int itemCount = 0;
    const std::uint8_t* cursor = ::Disassemble(
        packet, "dSdSSSd", &senderId, 0x32, receiverNameUtf16.data(), &trade, 0x7a, titleUtf16.data(), 0x7d2,
        contentsUtf16.data(), 0x200, ignoredTextUtf16.data(), &itemCount
    );

    if (itemCount >= 9) {
        // Log.Add(LOG_ERROR, "Admin post exceeds max count!")
        // Log.Add(
        //     LOG_WARN,
        //     "[admin post] Request rejected: %d attachments exceed the limit of 8 (sender %d). No reply is sent.",
        //     itemCount, senderId
        // )
        L2_LOG_WARN(Log,
            "[admin post] Request rejected: %d attachments exceed the limit of 8 (sender %d); no reply is sent.",
            itemCount, senderId
        );
        return false;
    }

    std::array<int, 8> itemTypes{};
    std::array<std::int64_t, 8> itemAmounts{};
    for (int index = 0; index < itemCount; ++index) {
        cursor = ::Disassemble(
            cursor, "dQ", &itemTypes[static_cast<std::size_t>(index)], &itemAmounts[static_cast<std::size_t>(index)]
        );
    }

    const std::string receiverName = Utf16ToUtf8(receiverNameUtf16.data());
    const std::u16string title(titleUtf16.data());
    const std::u16string contents(contentsUtf16.data());
    CUserSP receiver = g_userDb.GetUser(receiverNameUtf16.data(), true);
    if (!receiver) {
        // Log.Add(
        //     LOG_ERROR, "[AdminSendPost] Receiver(%s) dose not Exist! ", WideToUtf8(receiverName).c_str()
        // )
        // Log.Add(
        //     LOG_WARN,
        //     "[admin post] Cannot load receiver %s for sender %d. No post is created and no reply is sent.",
        //     WideToUtf8(receiverName).c_str(), senderId
        // )
        L2_LOG_WARN(Log,
            "[admin post] Cannot load receiver %s for sender %d; no post is created and no reply is sent.",
            receiverName.c_str(), senderId
        );
        return false;
    }

    // The sender pointer is only kept. The current body
    // loads it for every sender other than -1 but does not reject a miss.
    CUserSP sender;
    if (senderId != -1) {
        sender = g_userDb.GetUser(senderId, true, false, runtime::process::CurrentDatabaseId());
    }

    if (receiver->GetReloadFlag()) {
        socket->Send("cdd", 169, 0, 0x3c);
        return false;
    }

    std::array<int, 8> createdItemIds{};
    int createdItemCount = 0;
    const ItemOptionKey emptyOptionKey{};
    const BaseAttribute emptyBaseAttribute{};
    for (int index = 0; index < itemCount; ++index) {
        CWareHouseSP postWarehouse = receiver->GetWareHouse(4, true);
        const std::int64_t amount = itemAmounts[static_cast<std::size_t>(index)];
        if (!postWarehouse || amount <= 0)
            continue;

        const unsigned int itemOwnerId = senderId < 0 ? receiver->GetId() : static_cast<unsigned int>(senderId);
        CItemSP item = postWarehouse->MakeNewItem(
            itemOwnerId, static_cast<unsigned int>(itemTypes[static_cast<std::size_t>(index)]), amount, 0, 0, 0, 0, 0, 4,
            emptyOptionKey, 0, emptyBaseAttribute, 0, 0
        );
        if (!item)
            continue;

        {
            L2_WRITE_GUARD(*postWarehouse.operator->());
            postWarehouse->PushItem(item.get());
        }
        createdItemIds[static_cast<std::size_t>(createdItemCount++)] = static_cast<int>(item->ItemID());
    }

    if (createdItemCount != itemCount) {
        // Log.Add(
        //     LOG_WARN,
        //     "[admin post] Created %d of %d requested attachments for receiver %s (sender %d). Unavailable warehouses, invalid amounts or item creation failures can skip entries. Continuing with the original requested count in the post record.",
        //     createdItemCount, itemCount, WideToUtf8(receiverName).c_str(), senderId
        // )
        L2_LOG_WARN(Log,
            "[admin post] Created %d of %d requested attachments for receiver %s (sender %d); unavailable warehouses, invalid amounts or item creation failures can skip entries. Continuing with the original requested count in the post record.",
            createdItemCount, itemCount, receiverName.c_str(), senderId
        );
    }

    int postId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&postId);
    constexpr const wchar_t* createPostStatement = L"{Call lin_CreatePost (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)}";
    int process = 0;
    int returnable = 0;
    int senderDeleted = 0;
    int receiverDeleted = 0;
    int sentTime = runtime::crt::Time32Now();
    long long tradeMoney = 0;
    long long postFee = 0;
    // int senderType = 0x0d.
    int senderType = Post::ToAdminWeb;
    int recordedId = senderId;
    const unsigned int receiverId = receiver->GetId();
    connection.Prepare(createPostStatement);
    connection.BindParameter(&senderId);
    connection.BindParameter(&receiverId);
    connection.BindParameter(&trade);
    connection.BindParameter(title.c_str());
    connection.BindParameter(contents.c_str());
    connection.BindParameter(&process);
    connection.BindParameter(&returnable);
    connection.BindParameter(&senderDeleted);
    connection.BindParameter(&receiverDeleted);
    connection.BindParameter(&sentTime);
    connection.BindParameter(&itemCount);
    connection.BindParameter(&tradeMoney);
    connection.BindParameter(&postFee);
    connection.BindParameter(&senderType);
    connection.BindParameter(&recordedId);
    // if (!connection.ExecutePrepared(createPostStatement) || !connection.Fetch()) {
    const bool postQueryExecuted = connection.ExecutePrepared(createPostStatement);
    if (!postQueryExecuted || !connection.Fetch()) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin post] Cannot create post from sender %d to receiver %u: %s. Already created %d attachments. This handler does not undo them and returns failure.",
        //     senderId, receiverId, postQueryExecuted ? "database operation returned no post row" : "database operation failed", createdItemCount
        // )
        L2_LOG_ERROR(Log,
            "[admin post] Cannot create post from sender %d to receiver %u: %s. Already created %d attachments; this handler does not undo them and returns failure.",
            senderId, receiverId, postQueryExecuted ? "database operation returned no post row" : "database operation failed", createdItemCount
        );
        socket->Send("cdd", 169, 0, 0x2e);
        return false;
    }

    std::array<char, 0x5000> responseItems{};
    int responseBytes = 0;
    constexpr const wchar_t* createPostItemStatement = L"{Call lin_CreatePostItemForAdmin (?, ?, ?)}";
    for (int index = 0; index < createdItemCount; ++index) {
        const int itemId = createdItemIds[static_cast<std::size_t>(index)];
        responseBytes += ::Assemble(
            responseItems.data() + responseBytes, static_cast<int>(responseItems.size()) - responseBytes, "dQ", itemId,
            static_cast<std::int64_t>(0)
        );

        connection.UnBind();
        connection.Prepare(createPostItemStatement);
        connection.BindParameter(&itemId);
        connection.BindParameter(&postId);
        connection.BindParameter(&senderId);
        if (!connection.ExecutePrepared(createPostItemStatement)) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[adminsocket.cpp][%d] Admin CreatePostItem failed. "
            //     "item id[%d] mail id[%d] sender id [%d]",
            //     0x2b2c, itemId, postId, senderId
            // )
            // Log.Add(
            //     LOG_ERROR,
            //     "[admin post] Cannot associate item %d with post %d (sender %d, receiver %u). The item and post were already created. Processing continues and this item remains in the notification payload.",
            //     itemId, postId, senderId, receiverId
            // )
            L2_LOG_ERROR(Log,
                "[admin post] Cannot associate item %d with post %d (sender %d, receiver %u). The item and post were already created; processing continues and this item remains in the notification payload.",
                itemId, postId, senderId, receiverId
            );
        }
    }

    CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (server != nullptr) {
        server->Send(
            "chddddddddddbd", 0xc6, 0x71, 1, postId, senderId, static_cast<int>(receiverId), trade, 0, 0, 0, 0,
            createdItemCount, responseBytes, responseItems.data(), 0
        );
    } else {
        // Log.Add(
        //     LOG_WARN,
        //     "[admin post] Post %d from sender %d to receiver %u was created, but L2Server is unavailable. No new-post notification is sent.",
        //     postId, senderId, receiverId
        // )
        L2_LOG_WARN(Log,
            "[admin post] Post %d from sender %d to receiver %u was created, but L2Server is unavailable; no new-post notification is sent.",
            postId, senderId, receiverId
        );
    }
    return true;
}

bool CheckFreeUserPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0042a6a8, AQ 175.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 175, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dS", &characterId, 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        std::array<char, INET_ADDRSTRLEN> address{};
        const char* const converted =
            ::inet_ntop(AF_INET, &socket->m_address, address.data(), static_cast<socklen_t>(address.size()));
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find user, char id[%d] from ip[%s]", "adminsocket.cpp", 0x2c58, characterId,
        //     converted ? converted : "0.0.0.0"
        // )
        // Log.Add(
        //     LOG_WARN,
        //     "[admin] Cannot perform free-user check for character %d requested from %s: character unavailable. Returning failure.",
        //     characterId, converted ? converted : "unknown address"
        // )
        L2_LOG_WARN(Log,
            "[admin] Cannot perform free-user check for character %d requested from %s: character unavailable; returning failure.",
            characterId, converted ? converted : "unknown address"
        );
        socket->Send("cdd", 175, 0, 4);
        return false;
    }

    const bool loggedIn = user->m_loggedIn;
    const int sessionLogSource = user->m_sessionLogSource;
    if (!loggedIn) {
        socket->Send("cdd", 175, 0, 0x0f);
        return false;
    }
    if (sessionLogSource != 0) {
        socket->Send("cdd", 175, 0, 0);
        return false;
    }

    socket->Send("cd", 175, 1);
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(0x259);
    fields[1] = runtime::DecimalUtf16(0);
    fields[2] = runtime::DecimalUtf16(0);
    fields[3] = runtime::DecimalUtf16(user->GetId());
    fields[4] = runtime::DecimalUtf16(user->GetAccountID());
    fields[11] = runtime::DecimalUtf16(loggedIn ? 1 : 0);
    fields[21] = auditTextUtf16.data();
    fields[22] = auditTextUtf16.data();
    fields[23] = AdminLogText(user->GetCharName());
    fields[24] = AdminLogText(user->GetAccountName());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
    // Noise: Routine successful free-user checks are already recorded in the accounting log and returned to the requester.
    // Log.Add(LOG_NORMAL, "*check character:%d", characterId)
    return false;
}

namespace AQHandler {

bool RequestSeizeCommissionItem(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00445444, AQ 162, codec ddQdS.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 162, 0, 0x1e);
        return false;
    }

    int option = 0;
    int itemId = 0;
    std::int64_t amount = 0;
    int commissionId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddQdS", &option, &itemId, &amount, &commissionId, 0x200, auditTextUtf16.data());
    std::wstring auditText = Utf16ToWide(std::u16string_view(auditTextUtf16.data()));

    int reason = 1;
    if (!AdminUtil::CancelCommissionProc(commissionId, auditText.data(), &reason)) {
        socket->Send("cdd", 162, 0, reason);
        return false;
    }
    return SeizeItemProc(socket, 162, option, itemId, amount, auditText.data());
}

bool RequestDeleteCommissionItem(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0042e348, AQ 163, codec dQddS.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 163, 0, 0x1e);
        return false;
    }

    int itemId = 0;
    std::int64_t amount = 0;
    int deletePet = 0;
    int commissionId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dQddS", &itemId, &amount, &deletePet, &commissionId, 0x200, auditTextUtf16.data());
    std::wstring auditText = Utf16ToWide(std::u16string_view(auditTextUtf16.data()));

    int reason = 1;
    if (!AdminUtil::CancelCommissionProc(commissionId, auditText.data(), &reason)) {
        socket->Send("cdd", 163, 0, reason);
        return false;
    }
    return DelItemProc(socket, 163, itemId, amount, deletePet, auditText.data());
}

bool RequestDeleteBuff(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00445618, AQ 164, codec ddS. This handler does
    // not test CAdminSocket's request-rejection byte.
    L2_GUARD_FUNCTION();

    int characterId = 0;
    int skillId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "ddS", &characterId, &skillId, 0x200, auditTextUtf16.data());

    if (skillId != 0x38eb) {
        socket->Send("cdd", 164, 0, 3);
        return false;
    }

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 164, 0, 3);
        return false;
    }

    bool usedDatabaseFallback = false;
    bool succeeded;
    {
        L2_WRITE_GUARD(*user.operator->());
        succeeded = false;
        if (!user->IsLoggedIn()) {
            DBConn connection(runtime::process::CurrentDatabaseId());
            constexpr const wchar_t* statement = L"{Call lin_DeleteOneNewActiveSkill (?, ?)}";
            connection.Prepare(statement);
            connection.BindParameter(&characterId);
            connection.BindParameter(&skillId);
            if (connection.ExecutePrepared(statement)) {
                user->LoadActiveSkillFromDB();
                succeeded = true;
            }
        } else {
            CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
            if (server != nullptr) {
                // Exact call frame at 0x00445831..0x00445853.
                server->Send("chdd", 0xc6, 0xb4, characterId, skillId);
                succeeded = true;
            } else {
                usedDatabaseFallback = true;
                DBConn connection(runtime::process::CurrentDatabaseId());
                constexpr const wchar_t* statement = L"{Call lin_DeleteOneNewActiveSkill (?, ?)}";
                connection.Prepare(statement);
                connection.BindParameter(&characterId);
                connection.BindParameter(&skillId);
                if (connection.ExecutePrepared(statement)) {
                    user->LoadActiveSkillFromDB();
                    succeeded = true;
                }
            }
        }

    }
    if (!succeeded) {
        // Log.Add(LOG_ERROR,
        //     "[admin skills] Cannot delete active skill %d for character %d: %s. Cached-skill reload was skipped and failure reason 3 is returned.",
        //     skillId, characterId, usedDatabaseFallback ? "game-server connection unavailable and database fallback failed" : "database execution failed")
        L2_LOG_ERROR(Log,
            "[admin skills] Cannot delete active skill %d for character %d: %s; cached-skill reload was skipped and failure reason 3 is returned.",
            skillId, characterId, usedDatabaseFallback ? "game-server connection unavailable and database fallback failed" : "database execution failed");
        socket->Send("cdd", 164, 0, 3);
        return false;
    }

    if (usedDatabaseFallback) {
        // Log.Add(LOG_WARN,
        //     "[admin skills] No game-server connection was available to delete active skill %d for online character %d. The database fallback query succeeded and cached-skill reload was attempted. Success is returned without a game-server notification.",
        //     skillId, characterId)
        L2_LOG_WARN(Log,
            "[admin skills] No game-server connection was available to delete active skill %d for online character %d. The database fallback query succeeded and cached-skill reload was attempted; success is returned without a game-server notification.",
            skillId, characterId);
    }

    const std::u16string characterName = AdminLogText(user->GetCharName());
    const std::u16string accountName = AdminLogText(user->GetAccountName());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x2a5, 0, 0, user->GetId(), user->GetAccountID(), user->GetX(), user->GetY(), user->GetZ(), nullptr, nullptr,
        nullptr, skillId, 0, 0, 0, 0, 0, 0, 0, 0, 0, auditTextUtf16.data(), auditTextUtf16.data(), characterName.c_str(),
        accountName.c_str(), 0
    );
    socket->Send("cd", 164, 1);
    return false;
}

bool RequestAddDualClass(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00445de4, AQ 166, codec dddS.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 166, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int subjobId = 0;
    int newClass = -1;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dddS", &characterId, &subjobId, &newClass, 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 166, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 166, 0, 7);
        return false;
    }

    const int race = user->GetRace();
    const int mainClass = user->GetMainClass();
    const bool standardAwakenedClass = mainClass >= 0x94 && mainClass <= 0xb5;
    const bool ertheiaAwakenedClass = race == 6 && mainClass >= 0xbc && mainClass <= 0xbd;
    if (!standardAwakenedClass && !ertheiaAwakenedClass) {
        socket->Send("cdd", 166, 0, 0x24);
        return false;
    }
    if (user->GetDualClassId() != -1) {
        socket->Send("cdd", 166, 0, 0x25);
        return false;
    }
    if (user->GetLevel() < 80) {
        socket->Send("cdd", 166, 0, 0x26);
        return false;
    }

    const int oldSubjobId = user->GetSubjobId();
    if (oldSubjobId == 0 && race != 6) {
        socket->Send("cdd", 166, 0, 0x27);
        return false;
    }

    bool succeeded = false;
    if (race == 6) {
        if (newClass < 0x94 || newClass > 0xb5) {
            socket->Send("cdd", 166, 0, 0x35);
            return false;
        }

        const CVitalData vitality = user->GetVitalData();
        DBConn vitalityConnection(runtime::process::CurrentDatabaseId());
        const long vitalPoint = vitality.vitalPoint;
        const long restoreCount = vitality.restoreCount;
        const long latestResetDateVP = vitality.latestResetDate_VP;
        const long latestResetDateRC = vitality.latestResetDate_RC;
        constexpr const wchar_t* vitalityStatement = L"{Call lin_SaveCharVitality (?, ?, ?, ?, ?, ?)}";
        vitalityConnection.Prepare(vitalityStatement);
        vitalityConnection.BindParameter(&characterId);
        vitalityConnection.BindParameter(&oldSubjobId);
        vitalityConnection.BindParameter(&vitalPoint);
        vitalityConnection.BindParameter(&restoreCount);
        vitalityConnection.BindParameter(&latestResetDateVP);
        vitalityConnection.BindParameter(&latestResetDateRC);
        // vitalityConnection.ExecutePrepared(vitalityStatement)
        if (!vitalityConnection.ExecutePrepared(vitalityStatement)) {
            // Log.Add(LOG_ERROR,
            //     "[admin dual class] Cannot save vitality for character %d, current subjob %d. Dual-class creation with class %d is still attempted. Vitality persistence and the class-change result may differ.",
            //     characterId, oldSubjobId, newClass)
            L2_LOG_ERROR(Log,
                "[admin dual class] Cannot save vitality for character %d, current subjob %d; dual-class creation with class %d is still attempted. Vitality persistence and the class-change result may differ.",
                characterId, oldSubjobId, newClass);
        }

        succeeded = user->CreateDualJob(
                        1, newClass, oldSubjobId, user->GetHP(), user->GetMP(), user->GetSP(), user->GetExp(),
                        user->GetLevel(), user->m_henna[0], user->m_henna[1], user->m_henna[2], 0
                    ) != 0;
    } else {
        succeeded = user->RenewSubJob(
            subjobId, 1, newClass, oldSubjobId, user->GetHP(), user->GetMP(), user->GetSP(), user->GetExp(),
            user->GetLevel(), user->m_henna[0], user->m_henna[1], user->m_henna[2], user->GetAP()
        );
    }

    if (!succeeded) {
        socket->Send("cdd", 166, 0, 1);
        return false;
    }

    socket->Send("cd", 166, 1);
    const std::u16string characterName = AdminLogText(user->GetCharName());
    const std::u16string accountName = AdminLogText(user->GetAccountName());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x2a6, 0, 0, user->GetId(), user->GetAccountID(), user->GetX(), user->GetY(), user->GetZ(), nullptr, nullptr,
        nullptr, user->GetRace(), user->GetGender(), user->GetClass(), user->GetLevel(), 0, subjobId, newClass, 0, 0, 0,
        auditTextUtf16.data(), auditTextUtf16.data(), characterName.c_str(), accountName.c_str(), 0
    );
    return false;
}

bool RequestCancelDualClass(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004293ec, AQ 167, codec dS.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 167, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(packet, "dS", &characterId, 0x200, auditTextUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 167, 0, 4);
        return false;
    }
    if (user->IsLoggedIn()) {
        socket->Send("cdd", 167, 0, 7);
        return false;
    }

    const int dualClassId = user->GetDualClassId();
    if (dualClassId == -1) {
        socket->Send("cdd", 167, 0, 0x28);
        return false;
    }
    if (dualClassId != user->GetSubjobId()) {
        socket->Send("cdd", 167, 0, 0x29);
        return false;
    }
    if (user->GetLevel() >= 81) {
        socket->Send("cdd", 167, 0, 0x2a);
        return false;
    }

    const int oldClass = user->GetClass();
    if (oldClass >= 0x8b && oldClass <= 0x92) {
        socket->Send("cdd", 167, 0, 0x2b);
        return false;
    }
    if (user->GetRace() == 6) {
        socket->Send("cdd", 167, 0, 0x35);
        return false;
    }

    if (user->CancelDualClass() != -1) {
        socket->Send("cdd", 167, 0, 1);
        return false;
    }

    socket->Send("cd", 167, 1);
    const std::u16string characterName = AdminLogText(user->GetCharName());
    const std::u16string accountName = AdminLogText(user->GetAccountName());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x2a7, 0, 0, user->GetId(), user->GetAccountID(), user->GetX(), user->GetY(), user->GetZ(), nullptr, nullptr,
        nullptr, user->GetRace(), user->GetGender(), user->GetClass(), user->GetLevel(), 0, dualClassId, oldClass, 0, 0, 0,
        auditTextUtf16.data(), auditTextUtf16.data(), characterName.c_str(), accountName.c_str(), 0
    );
    return false;
}

bool RequestUpdateEventData(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00431d9c. Unlike the adjacent AQ handlers, this
    // function does not test CAdminSocket's request-rejection byte.
    L2_GUARD_FUNCTION();

    int index = 0;
    int total = 0;
    std::array<char16_t, 129> eventNameUtf16{};
    std::array<char16_t, 257> commentUtf16{};
    std::array<char16_t, 4097> contentsUtf16{};
    ::Disassemble(
        packet, "ddSSS", &index, &total, 0x102, eventNameUtf16.data(), 0x202, commentUtf16.data(), 0x2002,
        contentsUtf16.data()
    );

    const std::u16string eventName(std::u16string_view(eventNameUtf16.data()));
    const std::u16string comment(std::u16string_view(commentUtf16.data()));
    const std::u16string contents(std::u16string_view(contentsUtf16.data()));
    if (!CurrentEventDataHelper().Save(index, total, eventName, comment, contents)) {
        socket->Send("cdd", 160, 0, 0x23);
        return false;
    }

    socket->Send("cd", 160, 1);
    return false;
}

bool RequestResetInzoneRestrict(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00429d34.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 168, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int inzoneId = 0;
    std::array<char16_t, 256> ignoredCharacterNameUtf16{};
    ::Disassemble(packet, "ddS", &characterId, &inzoneId, 0x200, ignoredCharacterNameUtf16.data());
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        // The current handler emits no response on this path.
        return false;
    }
    if (user->m_itemCheckPending) {
        socket->Send("cdd", 168, 0, 7);
        return false;
    }
    if (!user->ResetInzoneOnDB(inzoneId)) {
        socket->Send("cdd", 168, 0, 0x2c);
        return false;
    }

    socket->Send("cd", 168, 1);
    return false;
}

bool RequestSendPost(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00449bc0, AQ 169. The successful response really
    // carries 0xa8 at 0x00449c5e. Preserving that released-wire anomaly is
    // required for a drop-in replacement.
    L2_GUARD_FUNCTION();
    if (::RequestWritePost(socket, packet)) {
        socket->Send("cd", 168, 1);
    }
    return false;
}

bool RequestValidatePostUser(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004479d0, AQ 170, codec dS. This handler does not
    // test CAdminSocket's request-rejection byte.
    L2_GUARD_FUNCTION();

    int senderId = 0;
    std::array<char16_t, 25> receiverNameUtf16{};
    ::Disassemble(packet, "dS", &senderId, 0x32, receiverNameUtf16.data());

    CUserSP receiver = g_userDb.GetUser(receiverNameUtf16.data(), true);
    CUserSP sender;
    if (senderId != -1) {
        sender = g_userDb.GetUser(senderId, true, false, runtime::process::CurrentDatabaseId());
    }
    if (!sender || !receiver || (receiver->GetBuilder() > 0 && sender->GetBuilder() == 0)) {
        socket->Send("cdd", 170, 0, 0x0e);
        return false;
    }

    const unsigned int receiverId = receiver->GetId();
    int receivedPostCount = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&receivedPostCount);
    constexpr const wchar_t* countStatement = L"{Call lin_GetReceivedPostCount (?)}";
    connection.Prepare(countStatement);
    connection.BindParameter(&receiverId);
    // if (connection.ExecutePrepared(countStatement) && connection.Fetch() && receivedPostCount >= 0xf0) {
    const bool countQuerySucceeded = connection.ExecutePrepared(countStatement);
    const bool countFetched = countQuerySucceeded && connection.Fetch();
    if (countFetched && receivedPostCount >= 0xf0) {
        socket->Send("cdd", 170, 0, 0x2f);
        return false;
    }

    if (!countFetched) {
        // Log.Add(LOG_ERROR,
        //     "[admin mail] Cannot confirm mailbox capacity for receiver %u while validating sender %d: the count query or fetch failed. Validation continues to the block-list check.",
        //     receiverId, senderId)
        L2_LOG_ERROR(Log,
            "[admin mail] Cannot confirm mailbox capacity for receiver %u while validating sender %d: the count query or fetch failed. Validation continues to the block-list check.",
            receiverId, senderId);
    }

    connection.UnBind();
    int blocked = 0;
    connection.Bind(&blocked);
    constexpr const wchar_t* blockedStatement = L"{Call lin_LoadBlocked (?, ?)}";
    connection.Prepare(blockedStatement);
    connection.BindParameter(&receiverId);
    connection.BindParameter(&senderId);
    // if (connection.ExecutePrepared(blockedStatement) && connection.Fetch()) {
    const bool blockQuerySucceeded = connection.ExecutePrepared(blockedStatement);
    if (blockQuerySucceeded && connection.Fetch()) {
        // The fetched value itself is ignored: any returned
        // row is the current target's blocked result.
        socket->Send("cdd", 170, 0, 0x2d);
        return false;
    }

    if (!blockQuerySucceeded) {
        // Log.Add(LOG_ERROR,
        //     "[admin mail] Block-list query failed for receiver %u and sender %d. Validation still reports success without confirming that the sender is allowed.",
        //     receiverId, senderId)
        L2_LOG_ERROR(Log,
            "[admin mail] Block-list query failed for receiver %u and sender %d; validation still reports success without confirming that the sender is allowed.",
            receiverId, senderId);
    }
    socket->Send("cd", 170, 1);
    return false;
}

bool RequestAddRecipeListPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0042a054.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 172, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int recipeCount = 0;
    std::array<char16_t, 256> ignoredCharacterNameUtf16{};
    const std::uint8_t* cursor =
        ::Disassemble(packet, "dSd", &characterId, 0x200, ignoredCharacterNameUtf16.data(), &recipeCount);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 172, 0, 4);
        return false;
    }
    if (user->m_itemCheckPending) {
        socket->Send("cdd", 172, 0, 7);
        return false;
    }

    for (int index = 0; index < recipeCount; ++index) {
        int recipeId = 0;
        cursor = ::Disassemble(cursor, "d", &recipeId);
        user->SaveRecipeBookInfo(recipeId);
    }
    socket->Send("cd", 172, 1);
    return false;
}

bool RequestAddShortcutListPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0042a350.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 173, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int shortcutCount = 0;
    std::array<char16_t, 256> ignoredCharacterNameUtf16{};
    const std::uint8_t* cursor =
        ::Disassemble(packet, "dSd", &characterId, 0x200, ignoredCharacterNameUtf16.data(), &shortcutCount);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 173, 0, 4);
        return false;
    }
    if (user->m_itemCheckPending) {
        socket->Send("cdd", 173, 0, 7);
        return false;
    }

    for (int index = 0; index < shortcutCount; ++index) {
        int slot = 0;
        int type = 0;
        int id = 0;
        int subjobId = 0;
        std::array<char16_t, 256> macroUtf16{};
        cursor = ::Disassemble(cursor, "dddSd", &slot, &type, &id, 0x200, macroUtf16.data(), &subjobId);
        std::wstring macro = Utf16ToWide(std::u16string_view(macroUtf16.data()));
        user->SaveShortcut(subjobId, slot, type, id, macro.data());
    }
    socket->Send("cd", 173, 1);
    return false;
}

bool RequestModifyPledgeGameInfoPacket(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00447f58.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 174, 0, 0x1e);
        return false;
    }

    std::uint32_t characterId = 0;
    int season = 0;
    int prizeCount = 0;
    int surrenderCount = 0;
    std::array<char16_t, 256> ignoredCharacterNameUtf16{};
    ::Disassemble(
        packet, "ddddS", &characterId, &season, &prizeCount, &surrenderCount, 0x200, ignoredCharacterNameUtf16.data()
    );
    CUserSP user = g_userDb.GetUser(static_cast<int>(characterId), true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        if (season != 0) {
            socket->Send("cdd", 174, 0, 4);
        } else {
            socket->Send("cd", 174, 1);
        }
        return false;
    }
    if (season == 0) {
        socket->Send("cd", 174, 1);
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    if (prizeCount != 0) {
        constexpr const wchar_t* statement = L"{Call lin_UpdatePledgeGameResult (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&season);
        connection.BindParameter(&characterId);
        connection.BindParameter(&prizeCount);
        if (!connection.ExecutePrepared(statement)) {
            // Log.Add(LOG_ERROR,
            //     "[admin clan game] Cannot save prize count %d for character %u in season %d: database execution failed. Surrender-count processing is skipped and failure reason 48 is returned.",
            //     prizeCount, characterId, season)
            L2_LOG_ERROR(Log,
                "[admin clan game] Cannot save prize count %d for character %u in season %d: database execution failed; surrender-count processing is skipped and failure reason 48 is returned.",
                prizeCount, characterId, season);
            socket->Send("cdd", 174, 0, 0x30);
            return false;
        }
        connection.ResetParam();
    }

    if (surrenderCount != 0) {
        constexpr const wchar_t* statement = L"{Call lin_IncreasePledgeGameSurrenderCount (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&season);
        connection.BindParameter(&characterId);
        connection.BindParameter(&surrenderCount);
        if (!connection.ExecutePrepared(statement)) {
            // Log.Add(LOG_ERROR,
            //     "[admin clan game] Cannot change surrender count by %d for character %u in season %d: database execution failed. %s. Sending failure reason 48.",
            //     surrenderCount, characterId, season, prizeCount != 0 ? "the preceding prize-count query succeeded" : "no prize-count query was requested")
            L2_LOG_ERROR(Log,
                "[admin clan game] Cannot change surrender count by %d for character %u in season %d: database execution failed; %s. Sending failure reason 48.",
                surrenderCount, characterId, season, prizeCount != 0 ? "the preceding prize-count query succeeded" : "no prize-count query was requested");
            socket->Send("cdd", 174, 0, 0x30);
            return false;
        }
    }

    socket->Send("cd", 174, 1);
    return false;
}

bool RequestAddActiveSkill(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004483ac.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 176, 0, 0x1e);
        return false;
    }

    int operation = 0;
    int characterId = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "dd", &operation, &characterId);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());

    int skillId = 0;
    int packedSkillLevel = 0;
    int skillDuration = 0;
    std::array<char16_t, 256> ignoredCharacterNameUtf16{};
    if (operation == 0) {
        ::Disassemble(cursor, "dS", &skillId, 0x200, ignoredCharacterNameUtf16.data());
    } else {
        ::Disassemble(cursor, "dddS", &skillId, &packedSkillLevel, &skillDuration, 0x200, ignoredCharacterNameUtf16.data());
    }

    if (!user) {
        socket->Send("cdd", 176, 0, 4);
        return false;
    }

    if (operation == 0) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_Admin_DeleteNewActiveSkill (?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.BindParameter(&skillId);
        if (connection.ExecutePrepared(statement)) {
            socket->Send("cd", 176, 1);
        } else {
            // Log.Add(LOG_ERROR,
            //     "[admin skills] Cannot delete active skill %d for character %d: database execution failed. This request returns without an administrative reply.",
            //     skillId, characterId)
            L2_LOG_ERROR(Log,
                "[admin skills] Cannot delete active skill %d for character %d: database execution failed; this request returns without an administrative reply.",
                skillId, characterId);
        }
        return false;
    }

    if (user->m_itemCheckPending) {
        socket->Send("cdd", 176, 0, 7);
        return false;
    }

    const std::uint32_t packedBits = static_cast<std::uint32_t>(packedSkillLevel);
    const int skillLevel = static_cast<int>(packedBits & 0xffffU);
    const std::uint32_t subLevelBits = packedBits >> 16U;
    const int skillSubLevel =
        subLevelBits < 0x8000U ? static_cast<int>(subLevelBits) : static_cast<int>(subLevelBits) - 0x10000;
    const int selfCast = 1;
    int rowCount = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&rowCount);
    constexpr const wchar_t* statement = L"{Call lin_Admin_AddNewActiveSkill (?, ?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&skillId);
    connection.BindParameter(&skillLevel);
    connection.BindParameter(&skillSubLevel);
    connection.BindParameter(&skillDuration);
    connection.BindParameter(&selfCast);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
        // Log.Add(LOG_ERROR,
        //     "[admin skills] Cannot obtain the add-skill result for character %d, skill %d, level %d/%d, duration %d: execution or result fetch failed. The database may already have changed. No cache reload or administrative reply follows.",
        //     characterId, skillId, skillLevel, skillSubLevel, skillDuration)
        L2_LOG_ERROR(Log,
            "[admin skills] Cannot obtain the add-skill result for character %d, skill %d, level %d/%d, duration %d: execution or result fetch failed. The database may already have changed; no cache reload or administrative reply follows.",
            characterId, skillId, skillLevel, skillSubLevel, skillDuration);
        return false;
    }
    if (rowCount >= 12) {
        // Log.Add(LOG_WARN,
        //     "[admin skills] Add request rejected for character %d, skill %d: returned active-skill count %d reached the limit 12. Sending failure reason 49.",
        //     characterId, skillId, rowCount)
        L2_LOG_WARN(Log,
            "[admin skills] Add request rejected for character %d, skill %d: returned active-skill count %d reached the limit 12; sending failure reason 49.",
            characterId, skillId, rowCount);
        socket->Send("cdd", 176, 0, 0x31);
        return false;
    }

    user = g_userDb.ReLoadFromDb(user.get(), static_cast<CUser::INCLUDE_WAREHOUSE>(1));
    if (!user) {
        // Log.Add(LOG_ERROR,
        //     "[admin skills] Add-skill query completed for character %d, skill %d, but character reload returned no user. The administrative reply still reports success.",
        //     characterId, skillId)
        L2_LOG_ERROR(Log,
            "[admin skills] Add-skill query completed for character %d, skill %d, but character reload returned no user; the administrative reply still reports success.",
            characterId, skillId);
    }
    socket->Send("cd", 176, 1);
    return false;
}

bool RequestDeleteSkillDelay(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00448894.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 177, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int rowCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "dd", &characterId, &rowCount);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());

    if (!user) {
        for (int index = 0; index < rowCount; ++index) {
            int ignoredSkillId = 0;
            cursor = ::Disassemble(cursor, "d", &ignoredSkillId);
        }
        std::array<char16_t, 256> ignoredCharacterNameUtf16{};
        ::Disassemble(cursor, "S", 0x200, ignoredCharacterNameUtf16.data());
        socket->Send("cdd", 177, 0, 4);
        return false;
    }

    int failedDeletions = 0;
    int firstFailedSkillId = 0;
    int firstFailedSubjobId = 0;
    for (int index = 0; index < rowCount; ++index) {
        int skillId = 0;
        int subjobId = 0;
        cursor = ::Disassemble(cursor, "dd", &skillId, &subjobId);

        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_DelSkillCoolTime (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&characterId);
        connection.BindParameter(&subjobId);
        connection.BindParameter(&skillId);
        // connection.ExecutePrepared(statement)
        if (!connection.ExecutePrepared(statement)) {
            if (failedDeletions == 0) {
                firstFailedSkillId = skillId;
                firstFailedSubjobId = subjobId;
            }
            ++failedDeletions;
        }
    }

    if (failedDeletions != 0) {
        // Log.Add(LOG_ERROR,
        //     "[admin skills] Cooldown deletion queries failed for %d of %d entries for character %d. First failure was skill %d, subjob %d. All entries were attempted and the administrative reply still reports success.",
        //     failedDeletions, rowCount, characterId, firstFailedSkillId, firstFailedSubjobId)
        L2_LOG_ERROR(Log,
            "[admin skills] Cooldown deletion queries failed for %d of %d entries for character %d; first failure was skill %d, subjob %d. All entries were attempted and the administrative reply still reports success.",
            failedDeletions, rowCount, characterId, firstFailedSkillId, firstFailedSubjobId);
    }

    std::array<char16_t, 256> ignoredCharacterNameUtf16{};
    ::Disassemble(cursor, "S", 0x200, ignoredCharacterNameUtf16.data());
    socket->Send("cd", 177, 1);
    return false;
}

bool RequestCheckRaidUser(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0042ab78.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 178, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::array<char16_t, 256> ignoredCharacterNameUtf16{};
    ::Disassemble(packet, "dS", &characterId, 0x200, ignoredCharacterNameUtf16.data());
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        std::array<char, INET_ADDRSTRLEN> address{};
        const char* const converted =
            ::inet_ntop(AF_INET, &socket->m_address, address.data(), static_cast<socklen_t>(address.size()));
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]Cannot find user, char id[%d] from ip[%s]", "adminsocket.cpp", 0x2d03, characterId,
        //     converted ? converted : "0.0.0.0"
        // )
        // Log.Add(
        //     LOG_WARN,
        //     "[admin] Cannot perform raid eligibility check for character %d requested from %s: character unavailable. Returning failure.",
        //     characterId, converted ? converted : "unknown address"
        // )
        L2_LOG_WARN(Log,
            "[admin] Cannot perform raid eligibility check for character %d requested from %s: character unavailable; returning failure.",
            characterId, converted ? converted : "unknown address"
        );
        socket->Send("cdd", 178, 0, 4);
        return false;
    }

    if (!user->m_itemCheckPending) {
        socket->Send("cdd", 178, 0, 0x0f);
        return false;
    }
    if (!user->GetReloadFlag()) {
        socket->Send("cdd", 178, 0, 0x33);
        return false;
    }

    socket->Send("cd", 178, 1);
    // Noise: A successful read-only raid eligibility check is returned to the requester. The generic character message adds no diagnostic context.
    // Log.Add(LOG_NORMAL, "*check character:%d", characterId)
    return false;
}

bool RequestSetInformar(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00448bb4, AQ 179.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 179, 0, 0x1e);
        return false;
    }

    int option = 0;
    std::array<char16_t, 4096> informarTextUtf16{};
    std::array<char16_t, 256> ignoredTextUtf16{};
    ::Disassemble(packet, "dSS", &option, 0x2000, informarTextUtf16.data(), 0x200, ignoredTextUtf16.data());

    DBConn connection(runtime::process::CurrentDatabaseId());
    if (option == 0) {
        constexpr const wchar_t* statement = L"{Call lin_DeleteInformar}";
        connection.Prepare(statement);
        if (!connection.ExecutePrepared(statement)) {
            // Log.Add(LOG_ERROR,
            //     "[admin informar] Delete query failed, sending failure reason 52.")
            L2_LOG_ERROR(Log,
                "[admin informar] Delete query failed; sending failure reason 52.");
            socket->Send("cdd", 179, 0, 0x34);
            return false;
        }
        socket->Send("cd", 179, 1);
        return false;
    }

    if (option == 1) {
        const std::u16string informarText(informarTextUtf16.data());
        constexpr const wchar_t* statement = L"{Call lin_SetInformar(?)}";
        connection.Prepare(statement);
        connection.BindParameter(informarText.c_str());
        if (!connection.ExecutePrepared(statement)) {
            // Log.Add(LOG_ERROR,
            //     "[admin informar] Save query failed, sending failure reason 52.")
            L2_LOG_ERROR(Log,
                "[admin informar] Save query failed; sending failure reason 52.");
            socket->Send("cdd", 179, 0, 0x34);
            return false;
        }
        socket->Send("cd", 179, 1);
    }
    if (option != 0 && option != 1) {
        // Log.Add(LOG_WARN,
        //     "[admin informar] Unsupported option %d. No database operation or administrative reply is issued.",
        //     option)
        L2_LOG_WARN(Log,
            "[admin informar] Unsupported option %d; no database operation or administrative reply is issued.",
            option);
    }
    return false;
}

bool RequestResetAbilityPoint(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00448e30.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 180, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int subjobId = 0;
    std::array<char16_t, 256> ignoredCharacterNameUtf16{};
    ::Disassemble(packet, "ddS", &characterId, &subjobId, 0x200, ignoredCharacterNameUtf16.data());
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 180, 0, 4);
        return false;
    }
    if (user->m_itemCheckPending) {
        socket->Send("cdd", 180, 0, 7);
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_AdminResetAbilityPoint(?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    connection.BindParameter(&subjobId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR,
        //     "[admin ability] Cannot reset ability points for character %d, subjob %d: database execution failed. Sending failure reason 52 and skipping character reload.",
        //     characterId, subjobId)
        L2_LOG_ERROR(Log,
            "[admin ability] Cannot reset ability points for character %d, subjob %d: database execution failed; sending failure reason 52 and skipping character reload.",
            characterId, subjobId);
        socket->Send("cdd", 180, 0, 0x34);
        return false;
    }

    // g_userDb.ReLoadFromDb(user.get(), static_cast<CUser::INCLUDE_WAREHOUSE>(1))
    const bool characterReloaded = static_cast<bool>(
        g_userDb.ReLoadFromDb(user.get(), static_cast<CUser::INCLUDE_WAREHOUSE>(1)));
    if (!characterReloaded) {
        // Log.Add(LOG_ERROR,
        //     "[admin ability] Reset query completed for character %d, subjob %d, but character reload returned no user. The administrative reply still reports success.",
        //     characterId, subjobId)
        L2_LOG_ERROR(Log,
            "[admin ability] Reset query completed for character %d, subjob %d, but character reload returned no user; the administrative reply still reports success.",
            characterId, subjobId);
    }
    socket->Send("cd", 180, 1);
    return false;
}

bool RequestResetWorldOlympiadPoint(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004491bc.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 186, 0, 0x1e);
        return false;
    }

    int worldId = 0;
    int characterId = 0;
    std::array<char16_t, 256> ignoredCharacterNameUtf16{};
    ::Disassemble(packet, "ddS", &worldId, &characterId, 0x200, ignoredCharacterNameUtf16.data());
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (user && user->m_itemCheckPending) {
        socket->Send("cdd", 186, 0, 7);
        return false;
    }

    const int point = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_WOlympiadResetPoint (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&worldId);
    connection.BindParameter(&characterId);
    connection.BindParameter(&point);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR,
        //     "[admin world Olympiad] Cannot reset points to zero for character %d in world %d: database execution failed. Sending failure reason 52.",
        //     characterId, worldId)
        L2_LOG_ERROR(Log,
            "[admin world Olympiad] Cannot reset points to zero for character %d in world %d: database execution failed; sending failure reason 52.",
            characterId, worldId);
        socket->Send("cdd", 186, 0, 0x34);
        return false;
    }

    socket->Send("cd", 186, 1);
    return false;
}

bool RequestModifyRaidPoint(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0042b07c.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 181, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::uint8_t modifyMode = 0;
    int point = 0;
    std::array<char16_t, 256> ignoredCharacterNameUtf16{};
    ::Disassemble(packet, "dcdS", &characterId, &modifyMode, &point, 0x200, ignoredCharacterNameUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 181, 0, 4);
        return false;
    }
    if (user->m_itemCheckPending) {
        socket->Send("cdd", 181, 0, 7);
        return false;
    }

    if (modifyMode != 0) {
        point = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(user->LoadUserPoint(8)) + static_cast<std::uint32_t>(point)
        );
    }
    if (point < 0) {
        point = 0;
    } else if (point > 20'000'000) {
        point = 20'000'000;
    }
    if (!user->SaveUserPoint(8, point)) {
        socket->Send("cdd", 181, 0, 1);
        return false;
    }

    socket->Send("cd", 181, 1);
    return false;
}

bool RequestModifyWorldchatPoint(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0042b460.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 182, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    std::uint8_t modifyMode = 0;
    int point = 0;
    std::array<char16_t, 256> ignoredCharacterNameUtf16{};
    ::Disassemble(packet, "dcdS", &characterId, &modifyMode, &point, 0x200, ignoredCharacterNameUtf16.data());

    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user) {
        socket->Send("cdd", 182, 0, 4);
        return false;
    }

    if (modifyMode == 1) {
        point = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(user->LoadWorldChatPoint(-1)) - static_cast<std::uint32_t>(point)
        );
    }
    if (point < 0) {
        point = 0;
    }
    if (point > 30'000) {
        point = 30'000;
    }
    if (!user->UpdateWorldChatPoint(point)) {
        socket->Send("cdd", 182, 0, 0x34);
        return false;
    }

    CSocket* const serverSocket = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (user->m_itemCheckPending && serverSocket != nullptr) {
        serverSocket->Send("chdd", 0xc6, 0xff, characterId, point);
    }
    socket->Send("cd", 182, 1);
    return false;
}

bool RequestAddAbilitySkill(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0042b7d8.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 183, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int rowCount = 0;
    std::array<char16_t, 256> ignoredCharacterNameUtf16{};
    const std::uint8_t* cursor =
        ::Disassemble(packet, "dSd", &characterId, 0x200, ignoredCharacterNameUtf16.data(), &rowCount);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user || user->m_itemCheckPending) {
        socket->Send("cdd", 183, 0, 7);
        return false;
    }

    for (int index = 0; index < rowCount; ++index) {
        int subjobId = 0;
        int potentialType = 0;
        int skillId = 0;
        int skillLevel = 0;
        cursor = ::Disassemble(cursor, "dddd", &subjobId, &potentialType, &skillId, &skillLevel);
        user->SaveAbilitySkill(subjobId, potentialType, skillId, skillLevel);
    }

    socket->Send("cd", 183, 1);
    return false;
}

bool RequestAddAbilityPoint(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0042bb14.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 184, 0, 0x1e);
        return false;
    }

    int characterId = 0;
    int rowCount = 0;
    std::array<char16_t, 256> ignoredCharacterNameUtf16{};
    const std::uint8_t* cursor =
        ::Disassemble(packet, "dSd", &characterId, 0x200, ignoredCharacterNameUtf16.data(), &rowCount);
    CUserSP user = g_userDb.GetUser(characterId, true, false, runtime::process::CurrentDatabaseId());
    if (!user || user->m_itemCheckPending) {
        socket->Send("cdd", 184, 0, 7);
        return false;
    }

    for (int index = 0; index < rowCount; ++index) {
        int subjobId = 0;
        int typeOnePoint = 0;
        int typeTwoPoint = 0;
        int typeThreePoint = 0;
        cursor = ::Disassemble(cursor, "dddd", &subjobId, &typeOnePoint, &typeTwoPoint, &typeThreePoint);
        user->SaveAbilityPoint(subjobId, typeOnePoint, typeTwoPoint, typeThreePoint);
    }

    socket->Send("cd", 184, 1);
    return false;
}

bool RequestModifyVipPoint(CAdminSocket*, const std::uint8_t*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool RequestSetRewardNpcVer2(CAdminSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    std::array<int, 9> values{};
    ::Disassemble(
        packet, "ddddddddd", &values[0], &values[1], &values[2], &values[3], &values[4], &values[5], &values[6], &values[7],
        &values[8]
    );

    CSocket* const serverSocket = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (serverSocket == nullptr) {
        socket->Send("cdd", 187, 0, 0x13);
        return false;
    }

    serverSocket->Send(
        "cdddddddddd", 0x94, 0x1d, values[0], values[1], values[2], values[3], values[4], values[5], values[6], values[7],
        values[8]
    );
    socket->Send("cd", 187, 1);
    return false;
}

} // namespace AQHandler

bool BroadcastInstantAnnounce(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0044a970, AQ 156. Its cooldown failure really
    // replies on AQ 171. That shipping protocol quirk is kept here.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 156, 0, 0x1e);
        return false;
    }

    int messageCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "d", &messageCount);
    if (static_cast<std::uint32_t>(messageCount) - 1U > 9U) {
        socket->Send("cdd", 156, 0, 3);
        return false;
    }

    const std::uint32_t now = runtime::GetTickCount();
    if (now <= g_lastGmAnnounceSendTick + 5000U) {
        socket->Send("cdd", 171, 0, 3);
        return false;
    }
    g_lastGmAnnounceSendTick = now;

    std::vector<std::u16string> messages;
    messages.reserve(static_cast<std::size_t>(messageCount));
    for (int index = 0; index < messageCount; ++index) {
        std::array<char16_t, 128> messageUtf16{};
        cursor = ::Disassemble(cursor, "S", 0x100, messageUtf16.data());
        messages.emplace_back(messageUtf16.data());
    }

    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(cursor, "S", 0x200, auditTextUtf16.data());

    std::array<char, 0x1000> messagePacket{};
    int messagePacketBytes = 0;
    int messageIndex = 0;
    for (const std::u16string& message : messages) {
        const int assembled = ::Assemble(
            messagePacket.data() + messagePacketBytes, static_cast<int>(messagePacket.size()) - messagePacketBytes, "dS",
            messageIndex, message.c_str()
        );
        if (assembled > 0) {
            messagePacketBytes += assembled;
        }
        ++messageIndex;
    }

    CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (server != nullptr) {
        server->Send("cddb", 0x94, 0x18, messageCount, messagePacketBytes, messagePacket.data());

        for (std::u16string message : messages) {
            for (char16_t& character : message) {
                if (character == u',')
                    character = u' ';
            }
            std::array<std::u16string, 26> fields{};
            fields[0] = runtime::DecimalUtf16(0x209);
            fields[1] = runtime::DecimalUtf16(0);
            fields[2] = runtime::DecimalUtf16(0);
            fields[5] = runtime::DecimalUtf16(0);
            fields[6] = runtime::DecimalUtf16(0);
            fields[7] = runtime::DecimalUtf16(0);
            fields[8] = message;
            fields[21] = auditTextUtf16.data();
            fields[22] = auditTextUtf16.data();
            runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
        }
    } else {
        // Log.Add(
        //     LOG_WARN,
        //     "[admin] Cannot broadcast %d instant announcement lines: L2Server is unavailable. No announcement audit records are written. The shared cooldown was already advanced and this command still replies with success.",
        //     messageCount
        // )
        L2_LOG_WARN(Log,
            "[admin] Cannot broadcast %d instant announcement lines: L2Server is unavailable. No announcement audit records are written; the shared cooldown was already advanced and this command still replies with success.",
            messageCount
        );
    }

    socket->Send("cd", 156, 1);
    return false;
}

bool BroadcastScreenAnnounce(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0044adbc, AQ 171.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 171, 0, 0x1e);
        return false;
    }

    int messageCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "d", &messageCount);
    if (static_cast<std::uint32_t>(messageCount) - 1U > 9U) {
        socket->Send("cdd", 171, 0, 3);
        return false;
    }

    const std::uint32_t now = runtime::GetTickCount();
    if (now <= g_lastGmAnnounceSendTick + 5000U) {
        socket->Send("cdd", 171, 0, 3);
        return false;
    }
    g_lastGmAnnounceSendTick = now;

    std::vector<std::u16string> messages;
    messages.reserve(static_cast<std::size_t>(messageCount));
    for (int index = 0; index < messageCount; ++index) {
        std::array<char16_t, 128> messageUtf16{};
        cursor = ::Disassemble(cursor, "S", 0x100, messageUtf16.data());
        messages.emplace_back(messageUtf16.data());
    }

    std::array<char16_t, 256> auditTextUtf16{};
    ::Disassemble(cursor, "S", 0x200, auditTextUtf16.data());

    std::array<char, 0x1000> messagePacket{};
    int messagePacketBytes = 0;
    int messageIndex = 0;
    for (const std::u16string& message : messages) {
        const int assembled = ::Assemble(
            messagePacket.data() + messagePacketBytes, static_cast<int>(messagePacket.size()) - messagePacketBytes, "dS",
            messageIndex, message.c_str()
        );
        if (assembled > 0) {
            messagePacketBytes += assembled;
            ++messageIndex;
        }
    }

    CSocket* const server = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (server != nullptr) {
        server->Send("cddb", 0x94, 0x1c, messageCount, messagePacketBytes, messagePacket.data());

        for (std::u16string message : messages) {
            for (char16_t& character : message) {
                if (character == u',')
                    character = u' ';
            }
            std::array<std::u16string, 26> fields{};
            fields[0] = runtime::DecimalUtf16(0x209);
            fields[1] = runtime::DecimalUtf16(0);
            fields[2] = runtime::DecimalUtf16(0);
            fields[5] = runtime::DecimalUtf16(0);
            fields[6] = runtime::DecimalUtf16(0);
            fields[7] = runtime::DecimalUtf16(0);
            fields[8] = message;
            fields[21] = auditTextUtf16.data();
            fields[22] = auditTextUtf16.data();
            runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
        }
    } else {
        // Log.Add(
        //     LOG_WARN,
        //     "[admin] Cannot broadcast %d screen announcement lines: L2Server is unavailable. No announcement audit records are written. The shared cooldown was already advanced and this command still replies with success.",
        //     messageCount
        // )
        L2_LOG_WARN(Log,
            "[admin] Cannot broadcast %d screen announcement lines: L2Server is unavailable. No announcement audit records are written; the shared cooldown was already advanced and this command still replies with success.",
            messageCount
        );
    }

    socket->Send("cd", 171, 1);
    return false;
}

bool GMAnnounceSend(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0044a00c.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 188, 0, 0x1e);
        return false;
    }

    int messageCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "d", &messageCount);
    if (static_cast<std::uint32_t>(messageCount) - 1U > 9U) {
        socket->Send("cdd", 188, 0, 3);
        return false;
    }

    const std::uint32_t now = runtime::GetTickCount();
    if (now <= g_lastGmAnnounceSendTick + 5000U) {
        socket->Send("cdd", 188, 0, 0x3d);
        return false;
    }
    g_lastGmAnnounceSendTick = now;

    std::vector<std::u16string> messages;
    for (int index = 0; index < messageCount; ++index) {
        std::array<char16_t, 256> messageUtf16{};
        int messageLength = 0;
        cursor = ::Disassemble(cursor, "Sd", 0x200, messageUtf16.data(), &messageLength);
        if (messageLength > 0x100) {
            socket->Send("cdd", 188, 0, 3);
            return false;
        }
        messages.emplace_back(messageUtf16.data());
    }

    int startDate = 0;
    int endDate = 0;
    int period = 0;
    int announceId = 0;
    int location = 0;
    std::array<char16_t, 256> ignoredMessageUtf16{};
    ::Disassemble(
        cursor, "dddddS", &startDate, &endDate, &period, &announceId, &location, 0x200, ignoredMessageUtf16.data()
    );

    if (announceId == 0) {
        std::array<char, 0x2000> messageBytes{};
        int messageByteCount = 0;
        for (const std::u16string& message : messages) {
            const int assembled = ::Assemble(
                messageBytes.data() + messageByteCount, static_cast<int>(messageBytes.size()) - messageByteCount, "S",
                message.c_str()
            );
            if (assembled > 0) {
                messageByteCount += assembled;
            }
        }

        CSocket* const serverSocket = g_pActiveServerSocket.load(std::memory_order_acquire);
        if (serverSocket == nullptr) {
            // Log.Add(LOG_ERROR, "[%s][%d] server connection failed ", "adminsocket.cpp", 0x2f3c)
            // Log.Add(
            //     LOG_ERROR,
            //     "[admin] Cannot send %d immediate GM announcement lines (location %d): L2Server unavailable. Returning failure.",
            //     messageCount, location
            // )
            L2_LOG_ERROR(Log,
                "[admin] Cannot send %d immediate GM announcement lines (location %d): L2Server unavailable; returning failure.",
                messageCount, location
            );
            socket->Send("cdd", 188, 0, 0x3e);
            return false;
        }

        if (location == 0 || location == 1) {
            const int serverOpcode = location == 0 ? 0x1e : 0x18;
            serverSocket->Send("cddb", 0x94, serverOpcode, messageCount, messageByteCount, messageBytes.data());
        } else {
            // Log.Add(
            //     LOG_WARN,
            //     "[admin] Immediate GM announcement has unsupported location %d. No lines are forwarded, but the existing handler still replies with success.",
            //     location
            // )
            L2_LOG_WARN(Log,
                "[admin] Immediate GM announcement has unsupported location %d; no lines are forwarded, but the existing handler still replies with success.",
                location
            );
        }
    } else {
        constexpr const wchar_t* deleteStatement = L"{Call lin_DelGMAnnounce (?)}";
        DBConn deleteConnection(runtime::process::CurrentDatabaseId());
        deleteConnection.Prepare(deleteStatement);
        deleteConnection.BindParameter(&announceId);
        if (!deleteConnection.ExecutePrepared(deleteStatement)) {
            // Log.Add(
            //     LOG_ERROR, "[%s][%d] del GM screen announce failed [%d]", "adminsocket.cpp", 0x2f4f, announceId
            // )
            // Log.Add(
            //     LOG_ERROR,
            //     "[admin] Cannot replace scheduled GM announcement %d: deleting the previous database rows failed. Returning failure.",
            //     announceId
            // )
            L2_LOG_ERROR(Log,
                "[admin] Cannot replace scheduled GM announcement %d: deleting the previous database rows failed; returning failure.",
                announceId
            );
            socket->Send("cdd", 188, 0, 0x3f);
            return false;
        }

        constexpr const wchar_t* setStatement = L"{Call lin_SetGMAnnounce (?, ?, ?, ?, ?, ?, ?)}";
        for (std::size_t index = 0; index < messages.size(); ++index) {
            const std::u16string message(messages[index]);
            const std::uint64_t lineNumber = index + 1U;
            DBConn setConnection(runtime::process::CurrentDatabaseId());
            setConnection.Prepare(setStatement);
            setConnection.BindParameter(&announceId);
            setConnection.BindParameter(message.c_str());
            setConnection.BindParameter(&lineNumber);
            setConnection.BindParameter(&startDate);
            setConnection.BindParameter(&endDate);
            setConnection.BindParameter(&period);
            setConnection.BindParameter(&location);
            if (!setConnection.ExecutePrepared(setStatement)) {
                // Log.Add(
                //     LOG_ERROR,
                //     "[%s][%d] set reservation announce failed "
                //     "[announceId : %d][message : %s] "
                //     "[line_number : %d]",
                //     "adminsocket.cpp", 0x2f59, announceId, WideToUtf8(message).c_str(), static_cast<int>(lineNumber)
                // )
                // Log.Add(
                //     LOG_ERROR,
                //     "[admin] Cannot store line %llu of %d for scheduled GM announcement %d (message: %s). Previous rows were already deleted. Earlier replacement lines are not rolled back. Returning failure.",
                //     static_cast<unsigned long long>(lineNumber), messageCount, announceId, WideToUtf8(message).c_str()
                // )
                // L2_LOG_ERROR(Log,
                //     "[admin] Cannot store line %llu of %d for scheduled GM announcement %d (message: %s). Previous rows were already deleted. Earlier replacement lines are not rolled back. Returning failure.",
                //     static_cast<unsigned long long>(lineNumber), messageCount, announceId, WideToUtf8(message).c_str()
                // )
                L2_LOG_ERROR(Log,
                    "[admin] Cannot store line %llu of %d for scheduled GM announcement %d (message: %s). Previous rows were already deleted; earlier replacement lines are not rolled back. Returning failure.",
                    static_cast<unsigned long long>(lineNumber), messageCount, announceId, Utf16ToUtf8(message).c_str()
                );
                socket->Send("cdd", 188, 0, 0x3f);
                return false;
            }
        }

        CSocket* const serverSocket = g_pActiveServerSocket.load(std::memory_order_acquire);
        if (serverSocket != nullptr) {
            serverSocket->Send("cd", 0x94, 0x1f);
        } else {
            // Log.Add(
            //     LOG_WARN,
            //     "[admin] Scheduled GM announcement %d was stored, but L2Server is unavailable. No reload notification is sent and the handler still replies with success.",
            //     announceId
            // )
            L2_LOG_WARN(Log,
                "[admin] Scheduled GM announcement %d was stored, but L2Server is unavailable; no reload notification is sent and the handler still replies with success.",
                announceId
            );
        }
    }

    socket->Send("cd", 188, 1);
    return false;
}

bool GMAnnounceDel(CAdminSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x004494b8.
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 189, 0, 0x1e);
        return false;
    }

    std::array<char16_t, 256> ignoredMessageUtf16{};
    int announceId = 0;
    ::Disassemble(packet, "Sd", 0x200, ignoredMessageUtf16.data(), &announceId);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DelGMAnnounce (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&announceId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] del GM screen announce failed [%d]", "adminsocket.cpp", 0x2f7e, announceId
        // )
        // Log.Add(
        //     LOG_ERROR,
        //     "[admin] Cannot delete scheduled GM announcement %d: database operation failed. Returning failure.",
        //     announceId
        // )
        L2_LOG_ERROR(Log,
            "[admin] Cannot delete scheduled GM announcement %d: database operation failed; returning failure.",
            announceId
        );
        socket->Send("cdd", 189, 0, 1);
        return false;
    }

    CSocket* const serverSocket = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (serverSocket == nullptr) {
        // Log.Add(
        //     LOG_WARN,
        //     "[admin] Scheduled GM announcement %d was deleted, but L2Server is unavailable. No reload notification is sent. Returning failure without undoing the deletion.",
        //     announceId
        // )
        L2_LOG_WARN(Log,
            "[admin] Scheduled GM announcement %d was deleted, but L2Server is unavailable; no reload notification is sent. Returning failure without undoing the deletion.",
            announceId
        );
        // The current instructions reply on AQ 188 here, not AQ 189.
        socket->Send("cdd", 188, 0, 1);
        return false;
    }

    serverSocket->Send("cd", 0x94, 0x1f);
    socket->Send("cd", 189, 1);
    return false;
}

bool GMAnnounceSendToServer(CAdminSocket* socket, const std::uint8_t*) {
    L2_GUARD_FUNCTION();
    if (socket->RejectsAdminRequests()) {
        socket->Send("cdd", 190, 0, 0x1e);
        return false;
    }

    CSocket* const serverSocket = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (serverSocket == nullptr) {
        // Log.Add(
        //     LOG_WARN,
        //     "[admin] Cannot request a GM announcement reload: L2Server unavailable. Returning failure."
        // )
        L2_LOG_WARN(Log,
            "[admin] Cannot request a GM announcement reload: L2Server unavailable; returning failure."
        );
        socket->Send("cdd", 190, 0, 1);
        return false;
    }

    serverSocket->Send("cd", 0x94, 0x1f);
    socket->Send("cd", 190, 1);
    return false;
}
