#pragma once

#include "Shared/src/MemorySub.h"

#include "Protocol/src/L2Constants.h"

#include "Agent.h"
#include "Boss.h"
#include "BotChecker.h"
#include "Captcha.h"
#include "CardUpDownGame.h"
#include "Creature.h"
#include "DailyMission.h"
#include "DuelUserInfo.h"
#include "EnsoulData.h"
#include "EventMatch.h"
#include "EventMonitor.h"
#include "ExDrop.h"
#include "FlyMove.h"
#include "Friends.h"
#include "GoodsInventory.h"
#include "HTMLCache.h"
#include "KillPVPPointChecker.h"
#include "L2Packet.h"
#include "Mentor.h"
#include "MonsterBook.h"
#include "OlympiadPacketHandlers.h"
#include "Pledge.h"
#include "PledgeGameSystem.h"
#include "PledgeMission.h"
#include "QuestData.h"
#include "QuestionAnswer.h"
#include "ShortCut.h"
#include "StatData.h"
#include "Transform.h"
#include "UserBookmark.h"

#include "L2Core/src/inc/XStd.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/BaseAttribute.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/BrBuyProductResultType.h"
#include "Shared/src/BuySellResult.h"
#include "Shared/src/IOCs.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/SSQ.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadSafeObjectMap.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

class CETC_MANI_USE_COUNT;
#include <vector>

class CSkillInfo;
struct SkillAcquireInfo;
enum EtcSkillAcquireType : std::int32_t;
enum SayPacketType : std::int32_t;

namespace CommissionExpired {
enum Enum : std::int32_t;
}

namespace CommissionDiscountInfo {
enum Enum : std::int32_t;
}

namespace PledgeNS {
class PledgeSkillAcquireCallback;
}

struct CommissionHistory {
    std::int64_t m_nItemPrice; // +0x00
    std::int64_t m_nItemAmount; // +0x08
    CommissionExpired::Enum m_eExpiredType; // +0x10
    CommissionDiscountInfo::Enum m_eDiscountType; // +0x14
};


enum SkillType : std::int32_t {
    SKT_ACTIVE = 0,
    SKT_PASSIVE = 1,
};

enum ItemRoutingType : std::int32_t;

struct SendSkillListData {
    std::int32_t m_Count;
    std::array<char, 0x4f9c> m_Packet;
    std::int32_t m_Length;
};

class CRecipeInfo;
struct CRecipeInfoData;
class AtomicJob;
class AtomicAlchemyResult;
class AtomicMonsterBookReward;
class AtomicNpcSetSkillAll;
class CAtomicItemJobCheck;
class CAlliance;
class BalthusEvent;
class CCardCollectSystem;
class CBuy;
class CBuySell;
class CWareHouseDeposit;
class CWareHouseWithdraw;
class CCommissionTradeManager;
class CEconomy;
class CDeferredJob;
class CHolyThing;
class CInspector;
class CTrade;
class CResidence;
class EtcManiCardUpDownGame;
class EtcManiRemoveInzoneBindingByEvent;
class EtcManiUpdateUserEventTime;
class CFishingController;
class CMoveAction;
class CNotice_LevelCond;
class CPetitionCli;
class Say;
class CSkillAction2;
class CSummon;
class CSkillEffect_i_bookmark_teleport;
class CSkillEffect_i_bless_shape_shift_am;
class CSkillEffect_i_bless_shape_shift_wp;
class CSkillEffect_i_restore_shape_shift_item;
class CSkillEffect_i_shape_shift_am;
class CSkillEffect_i_shape_shift_am_fixed;
class CSkillEffect_i_shape_shift_wp;
class CSkillEffect_i_shape_shift_wp_fixed;
class CSkillEffect_p_create_item;
class CSkillOperateCondition_soul_saved;
class CSkillOperateCondition_op_soul_max;
class CSkillOperateCondition_op_not_cursed;
class CSkillOperateCondition_restart_point;
class CNPC;
class COwnThing;
class CPledge;
class CPremiumItem;
class CPostInfoForUser;
class CMultiSellList;
class CMultiPartyCommandChannel;
class CManorFuncHandler;
class CPartyMatchingRequest;
class CPartyMatchingWaitingRoomListFilter;
class CPledgePenaltyViewer;
class CPet;
class CBasicActionHandler_PET_DEPOSIT;
class CBasicActionHandler_TELEPORT_BOOKMARK;
class CBotReportAgent;
class AllowedWorldChatBroadcastVisitor;
class User;
class CUserBookmark;
class CUserBookmarkHandler;
namespace BuilderCmdHandler {
void OnAddSlot(User* user, char16_t* command);
void OnSendList(User* user, char16_t* command);
void OnChangeSlot(User* user, char16_t* command);
} // namespace BuilderCmdHandler
class UserRequestTimeSetter;
class CUserZoneQuestHandler;
struct QuestOperation_Item;
class CUserNewGoods;
class CUserRidingInfo;
class CVitality;
class Duel_SetRelationChanged;
class DismissPledge;
class SetRelationChanged;
class ItemManipList;
class NpcSocket;
class CDBSocket;
class RaidClientManager;
class RaidClientSocket;
class CUserSocket;

bool BuilderCmd_subjob(CUserSocket*, User*, char16_t*);
bool BuilderCmd_Premium_item(CUserSocket*, User*, char16_t*);

namespace BuilderCmd {
bool CSCharacterInfoPacket(CUserSocket*, char16_t*);
bool answer_captcha(CUserSocket*, User*, char16_t*);
bool inc_bot_count(CUserSocket*, User*, char16_t*);
bool reset_skill_reuse_time(CUserSocket*, User*, char16_t*);
bool talk_npc(CUserSocket*, User*, char16_t*);
bool save_external_notify(CUserSocket*, User*, char16_t*);
bool addpledgemem(CUserSocket*, User*, char16_t*);
bool set_henna_period(CUserSocket*, User*, char16_t*);
bool see_bot_count(CUserSocket*, User*, char16_t*);
bool see_bot_top10(CUserSocket*, User*, char16_t*);
bool reset_bot_count(CUserSocket*, User*, char16_t*);
bool reset_bot_restrain(CUserSocket*, User*, char16_t*);
bool set_bot_count(CUserSocket*, User*, char16_t*);
bool set_bot_rcount(CUserSocket*, User*, char16_t*);
bool make_chao(CUserSocket*, User*, char16_t*);
bool screenNotice(CUserSocket*, User*, char16_t*);
bool PledgeBonus(CUserSocket*, User*, char16_t*);
} // namespace BuilderCmd

namespace UserPacketEx {
bool FlyMove(CSocket* socket, const std::uint8_t* packet);
bool ReturnFromRaid(CSocket* socket, const std::uint8_t* packet);
bool PartyLootingModify(CSocket* socket, const std::uint8_t* packet);
bool PartyLootingModifyAgreement(CSocket* socket, const std::uint8_t* packet);
bool RequestPartyMatchingRoomHistory(CSocket* socket, const std::uint8_t* packet);
bool ChangeToAwakenedClass(CSocket* socket, const std::uint8_t* packet);
bool RequestAcquirePotentailSkill(CSocket* socket, const std::uint8_t* packet);
bool RequestPotentialSkillList(CSocket* socket, const std::uint8_t* packet);
bool RequestResetPotentialSkill(CSocket* socket, const std::uint8_t* packet);
bool RequestTryEnsoul(CSocket* socket, const std::uint8_t* packet);
bool RequestEnsoulExtraction(CSocket* socket, const std::uint8_t* packet);
bool RequestOneDayReceiveReward(CSocket* socket, const std::uint8_t* packet);
} // namespace UserPacketEx

inline CSPointer<CPledge> GetPledgeFromNameMap(const int worldId, const char16_t* const pledgeName) {
    // Current L2Server 0x00ba1e10: lowercase into a fixed 32-unit carrier.
    const std::u16string name = LowercaseGameName(pledgeName);
    std::array<SharedUtilsWideChar, 32> lowerPledgeName;
    std::copy_n(name.data(), name.size() + 1, lowerPledgeName.data());
    return CPledge::FindPledge(worldId, lowerPledgeName.data());
}

bool ReplySavePremiumItemForChar(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadUserPremiumItemPacket(CDBSocket* socket, const unsigned char* packet);
bool ReplyManipulatePremiumItemsPacket(CDBSocket* socket, const unsigned char* packet);
bool ReplyRenewSubJob(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadLastColorPacket(CDBSocket* socket, const unsigned char* packet);
bool ReplySendBirthdayMail(CDBSocket* socket, const unsigned char* packet);
bool ShortCutInfoPacket(CDBSocket* socket, const unsigned char* packet);
bool ReplyCreateSubJob(CDBSocket* socket, const unsigned char* packet);
bool ReplyDeleteSubJob(CDBSocket* socket, const unsigned char* packet);
bool ReplyGiveItemToPet(CDBSocket* socket, const unsigned char* packet);
bool AcquiredSkillList(CDBSocket* socket, const unsigned char* packet);
bool ReplyPrivateStoreCommit(CDBSocket* socket, const std::uint8_t* packet);
bool ReplyCreatePledgeByNpc(CDBSocket* socket, const std::uint8_t* packet);
bool LoadSelectedCharacterPacket(CDBSocket* socket, const std::uint8_t* packet);

namespace DBPacketHandler {
bool DropItemPacket(CDBSocket* socket, const std::uint8_t* packet);
bool PickItem(CDBSocket* socket, const std::uint8_t* packet);
bool ReplyUpdateUserPledge(CDBSocket* socket, const std::uint8_t* packet);
bool _ReplyWithdrawPet(
    std::uint32_t userServerId,
    std::uint32_t petCollarServerId,
    int result,
    CPet* pet,
    std::int64_t sp,
    int meal,
    int slot0,
    int slot1,
    int slot2,
    std::int64_t experience,
    double hp,
    double mp,
    wchar_t* name,
    int loadItems,
    std::int64_t downExp,
    int alive,
    int deathCount
);
bool ReplyChangeSubJob(CDBSocket* socket, const unsigned char* packet);
bool ReplyReloadSkill(CDBSocket* socket, const unsigned char* packet);
} // namespace DBPacketHandler

namespace DBPacketExHandler {
bool ResponseLoadOneDayRewardInfo(CDBSocket* socket, const unsigned char* packet);
bool ResponseLoadAutoSoulShotUI(CDBSocket* socket, const unsigned char* packet);
bool ResponseLoadUserPledgeBonus(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadCardUpdownGameRecord(CDBSocket* socket, const unsigned char* packet);
bool ReplyUpdateCardUpdownGameRecord(CDBSocket* socket, const unsigned char* packet);
bool DeleteBuffByAdmin(CDBSocket* socket, const unsigned char* packet);
bool ResponseCancelDualClass(CDBSocket* socket, const unsigned char* packet);
bool ReplyCreateDualJob(CDBSocket* socket, const unsigned char* packet);
bool ResponseShapeShiftItemEx(CDBSocket* socket, const unsigned char* packet);
bool ResponseShapeShiftItemFixedEx(CDBSocket* socket, const unsigned char* packet);
bool ResponseRestoreShapeShiftItemEx(CDBSocket* socket, const unsigned char* packet);
bool ResponseExpireShapeShiftItemEx(CDBSocket* socket, const unsigned char* packet);
bool ResponseBuyBeautyItem(CDBSocket* socket, const unsigned char* packet);
bool ResponseBeautyPurchaseList(CDBSocket* socket, const unsigned char* packet);
bool TradeDivideAdena(CDBSocket* socket, const unsigned char* packet);
bool NewManipulateItemPacket(CDBSocket* socket, const unsigned char* packet);
bool ReplyAcquirePotentialSkill(CDBSocket* socket, const unsigned char* packet);
bool AcquiredAbilitySkillList(CDBSocket* socket, const unsigned char* packet);
bool ResetAbilitySkill(CDBSocket* socket, const unsigned char* packet);
bool Reply_RP_EX_CHANGE_AP(CDBSocket* socket, const unsigned char* packet);
bool ReplyLoadFactionPoint(CDBSocket* socket, const unsigned char* packet);
bool ResponseLoadBalthusEventUserInfoEx(CDBSocket* socket, const unsigned char* packet);
bool ReplySetQuest(CDBSocket* socket, const unsigned char* packet);
bool ResponseResetBeautyItem(CDBSocket* socket, const unsigned char* packet);
} // namespace DBPacketExHandler

bool EnableTutorialEvent(NpcSocket* socket, const unsigned char* packet);
bool NpcAtomicDelGiveItemParam(NpcSocket* socket, const unsigned char* packet);
bool NpcMoveTaxToResidenceBank(NpcSocket* socket, const unsigned char* packet);
bool NpcMoveMoneyToResidenceBank(NpcSocket* socket, const unsigned char* packet);
bool GiveItemByCastleSiegeDefence(NpcSocket* socket, const unsigned char* packet);
bool NpcShowSellPreviewPagePacket(NpcSocket* socket, const unsigned char* packet);
bool NpcSunRise(NpcSocket* socket, const unsigned char* packet);
void LogNpcAtomicDelGiveItemResult(User* user, int relatedQuestId, int flag);

namespace NpcSocketEx {

bool ShowBeautyShop(NpcSocket* socket, const unsigned char* packet);
bool MoveToServer(NpcSocket* socket, const unsigned char* packet);

} // namespace NpcSocketEx

// Exact three-dword capture used by pledge DB/manipulation callbacks. The
// smart id is the current fast path. World/database ids keep lookup
// across an object re-creation.
struct UserCapture {
    std::uint32_t m_sid = 0;
    std::int32_t m_worldid = 0;
    std::int32_t m_dbid = 0;

    User* GetUser() const;
};


struct CVitalData;
struct SharedNoblessInfo;
class SharedNoblessInfoMap;
struct SkillUseInfo;
struct FVector;
class WOlympiadUser;
class WOlympiadUsers;
class WOlympiadServiceNode;
template <typename TObject>
class CSPointer;
enum ItemDefaultActionEnum : std::int32_t;
enum EtcSkillAcquireType : std::int32_t;
enum MPCCCreateType : std::int32_t;
enum PetCheckCond : std::int32_t;
enum PetCommandType : std::int32_t;
enum eSMsgType : std::int32_t;
enum DuelStartErrorType : std::int32_t;
enum UserPointType : std::int32_t;
enum ServerTriggeredUserAction : std::int32_t;

namespace ClientINI {
enum InteractType : std::int32_t;
}

namespace protocol {
struct ItemInfo;
struct ItemServerInfo;
struct C_EX_ENCHANT_RETRY_TO_PUT_ITEMS_INFO;
struct C_EX_UPGRADE_SYSTEM_REQUEST;
struct C_EX_SWAP_AGATHION_SLOT_ITEMS_INFO;
struct C_EX_REQUEST_LOCKED_ITEM;
struct C_EX_REQUEST_UNLOCKED_ITEM;
} // namespace protocol

namespace UserPacketEx {
bool RequesPledgeBonusReceiveReward(CSocket* socket, const std::uint8_t* packet);
}

namespace VitalityRestore {
enum Type : std::int32_t;
}

namespace PremiumUserType {
enum Enum : std::int32_t;
}

namespace PetSummonActionHelper {
bool CheckMasterPKCount(User* user, PetCommandType command);
}

bool IsAutoSoulShotAction(ItemDefaultActionEnum action);

enum JobOnDespawnPet : std::int32_t {
    DESPAWN_PET_NONE = 0,
    DESPAWN_PET_OLYMPIAD_PLAYER = 1,
    DESPAWN_PET_OLYMPIAD_OBSERVER = 2,
    DESPAWN_PET_BLOCK_UPSET = 3,
    DESPAWN_PET_PLEDGEGAME_PLAYER = 4,
    DESPAWN_PET_PLEDGEGAME_OBSERVER = 5,
    DESPAWN_PET_SUBJOB_CHANGE = 6,
    DESPAWN_PET_OLYMPIAD_IN_PEER = 7,
};

enum NoblessWinType : std::int32_t {
    WIN_RAID = 1,
    WIN_CASTLE_DEFENCE = 2,
    WIN_CASTLE_ATTACK = 3,
    WIN_HERO = 4,
};

// User.cpp's current private-store snapshot. The three target xmaps grew
// from the donor's 0x10-byte carriers to 0x20 bytes in this image, making the
// physical current extent 0xc0.
struct CPrivateStoreData {
    CPrivateStoreData();
    CPrivateStoreData(const CPrivateStoreData&) = default;
    CPrivateStoreData(CPrivateStoreData&&) noexcept = default;
    CPrivateStoreData& operator=(const CPrivateStoreData&) = default;
    CPrivateStoreData& operator=(CPrivateStoreData&&) noexcept = default;
    ~CPrivateStoreData() = default;

    CPrivateStoreData(
        int itemServerId,
        int internalId,
        int enchanted,
        int damaged,
        std::int64_t count,
        int classId,
        std::int64_t price,
        int intensiveItemClassId,
        int variation1,
        int variation2,
        const BaseAttribute& attribute,
        int shapeShiftingClassId
    );
    CPrivateStoreData(
        int itemServerId,
        int internalId,
        int isBlessed,
        int enchanted,
        int damaged,
        int itemDbId,
        std::int64_t count,
        int classId,
        std::int64_t price,
        int intensiveItemClassId,
        int variation1,
        int variation2,
        const BaseAttribute& attribute,
        int shapeShiftingClassId
    );

    void Init(
        int itemServerId,
        int internalId,
        int isBlessed,
        int enchanted,
        int damaged,
        int itemDbId,
        std::int64_t count,
        int classId,
        std::int64_t price,
        int intensiveItemClassId,
        int variation1,
        int variation2,
        const BaseAttribute& attribute,
        int shapeShiftingClassId
    );
    void SetEnsoulOption(std::map<int, EnsoulOptionData*> options[3]);
    void MakeEnsoulLogInfo(char16_t* output);
    std::int16_t GetItemDamage() const;
    void MakeShapeShiftAndVariationLogInfo(char16_t* output);

    std::int32_t nOrderNo; // +0x00
    std::int32_t nIndex; // +0x04
    std::int32_t nItemServerId; // +0x08
    std::int32_t nIsBlessed; // +0x0c
    std::int32_t nEnchanted; // +0x10
    std::int32_t nDamaged; // +0x14
    std::int32_t nItemDBID; // +0x18
    std::int64_t nCount; // +0x20
    std::int32_t nClassId; // +0x28
    std::int64_t nPrice; // +0x30
    std::int32_t nInternalId; // +0x38
    std::int32_t nIntensiveItemClassID; // +0x3c
    std::int32_t nVariation1; // +0x40
    std::int32_t nVariation2; // +0x44
    BaseAttribute m_BaseAttribute; // +0x48
    std::int32_t nShapeShiftingClassId; // +0x5c
    std::map<int, EnsoulOptionData*> mEnsoulOptionMap[3]; // +0x60
};


struct SummonReservedInfo {
    std::int32_t m_nClassId; // +0x00
    std::int32_t m_nDbId; // +0x04
    std::int32_t m_nLevel; // +0x08
    std::int32_t m_nOrgRemainTime; // +0x0c
    std::int32_t m_nRemainTime; // +0x10
    std::int32_t m_nExpRate; // +0x14
    double m_dHP; // +0x18
    double m_dMP; // +0x20
    std::int32_t m_nConsumeItemId; // +0x28
    std::int32_t m_nConsumeItemCount; // +0x2c
    std::int32_t m_nIsSummon; // +0x30
    std::array<std::byte, 0x04> m_alignment0034To0038;
};


// Current User::CallPCTry stores the pending recall transaction in this
// exact five-field carrier at User+0x5eb0.
struct RecallInfo {
    RecallInfo();

    std::int32_t m_nRecallerSID; // +0x00
    std::uint32_t m_nRecalledTime; // +0x04
    std::int32_t m_nItemClassID; // +0x08
    std::int32_t m_nItemCount; // +0x0c
    bool m_bIncludeDead; // +0x10
    std::array<std::byte, 0x03> m_alignment0011To0014; // +0x11..+0x13
};


enum TutorialClientEventEnum : std::int32_t {
    TCE_MOVE = 1,
    TCE_VIEW_CHANGE = 2,
    TCE_WHEEL_SCROLL = 4,
    TCE_RIGHT_CLICK = 8,
    TCE_WHEEL_CLICK = 16,
    TCE_LEVEL_36 = 32,
    TCE_LEVEL_61 = 64,
    TCE_LEVEL_73 = 128,
    TCE_HP_1OVER3 = 256,
    TCE_DIE_1_9 = 512,
    TCE_LEVEL_5 = 1024,
    TCE_LEVEL_7 = 2048,
    TCE_LEVEL_20 = 4096,
    TCE_QUEST_TAB_CLICK = 8192,
    TCE_LEVEL_39 = 16384,
    TCE_LEVEL_76 = 32768,
    TCE_PICK_BLUE_GEM = 1048576,
    TCE_PICK_ADENA = 2097152,
    TCE_LEVEL_4 = 4194304,
    TCE_SIT_DOWN = 8388608,
    TCE_LEVEL_35 = 16777216,
    TCE_LEVEL_75 = 33554432,
    TCE_LEVEL_15 = 67108864,
    TCE_LEVEL_6 = 134217728,
    TCE_LEVEL_9 = 268435456,
    TCE_LEVEL_10 = 536870912,
    TCE_LEVEL_12 = 1073741824,
};

enum ClassCategory : std::int32_t {
    CC_INVALID = -1,
    CC_TANKER = 0,
    CC_HEALER = 1,
    CC_ATTACKER = 2,
};

enum CompassRegionCodeEnum : std::int32_t {
    CRCE_NORMAL_FIELD = 15,
    CRCE_PVP = 14,
    CRCE_SSQ = 13,
    CRCE_PEACE = 12,
    CRCE_SIEGE = 11,
    CRCE_NO_RESTART = 10,
    CRCE_BUFF = 9,
    CRCE_DEBUFF = 8,
};

enum DoJobWithPoint : std::int32_t {
    DJWP_ADD = 0,
    DJWP_ATTENTION_ADD = 1,
    DJWP_DOUBLEADD = 2,
    DJWP_DECREASE = 3,
    DJWP_FAILED_BY_INVALID_USER = -1,
    DJWP_FAILED_BY_INVALID_WAREHOUSE = -2,
    DJWP_FAILED_BY_NOT_ENOUGH_POINT = -3,
    DJWP_FAILED_BY_TRANSACTION_FAIL = -4,
};

enum AbnormalUserAction : std::int32_t {
    AUA_INSPECTOR_FORCE_CLOSE = 0,
    AUA_TOO_FAST_TRADE = 1,
    AUA_ACQUIRE_INSTANT_SKILL = 2,
    AUA_TRY_TO_REVIVE_BUT_ALIVE = 3,
    AUA_DUPLICATED_APPEARING = 4,
    AUA_INVALID_NPC_OR_CHANNEL = 5,
    AUA_UNOWNED_RECIPE = 6,
    AUA_TRY_ITEM_NOT_ITEM = 7,
    AUA_TRY_GM_COMMAND = 8,
    AUA_TRY_TO_INVALID_TELEPORT = 9,
    AUA_TRY_TO_REQUEST_MULTISELL_WITH_INVALID_TALK_CHANNEL = 10,
    AUA_TRY_TO_PET_USE_INVALID_ITEM = 11,
    AUA_TRY_TO_USE_ZEROSIZE_ITEM = 12,
    AUA_TRY_TO_USE_UNREGISTERED_SKILL = 13,
    AUA_TRY_TO_DROP_ZERO_ITEM = 14,
    AUA_TRY_TO_ACQUIRE_SKILL_INFO_WITH_INVALID_TALK_CHANNEL = 15,
    AUA_TRY_TO_PET_GET_INVALID_ITEM = 16,
    AUA_INVALID_SERVER_ID = 17,
    AUA_NOT_EXIST_ITEM_IN_USERDB = 18,
    AUA_MAX = 19,
};

enum CoupleActionErrorType : std::int32_t {
    CAET_SUCCESS = 0,
    CAET_IN_STORE_MODE = 3123,
    CAET_FISHING = 3124,
    CAET_IN_COMBAT_MODE = 3125,
    CAET_CHAO_VIOLET = 3127,
    CAET_IN_COUPLE_ACTION = 3126,
    CAET_OLYMPIAD_REGISTERED = 3128,
    CAET_IN_AGIT_WAR = 3129,
    CAET_IN_CASTLE_WAR = 3130,
    CAET_ON_VEHICLE = 3131,
    CAET_TRANSFORMED = 3133,
    CAET_TELEPORT = 3132,
    CAET_ALIVE = 3139,
    CAET_IN_MODIFYING_PARTY_LOOTING = 3141,
    CAET_FAKE_DEATH = 4346,
    CAET_IN_CASTING = 4453,
    CAET_FLYMOVING = 4454,
};

enum PendingUseItemType : std::int32_t {
    PUIT_ENCHANT = 0,
    PUIT_MPCC_CREATE = 1,
    PUIT_ATTRIBUTE_ENCHANT = 2,
    PUIT_INC_ENCHANT_PROP = 3,
    PUIT_CRYSTAL = 4,
    PUIT_COMMISSION = 5,
    PUIT_ATTRIBUTE_CHANGE = 6,
    PUIT_SHAPE_SHIFTING = 7,
    PUIT_SHAPE_SHIFTING_EXTRACTION = 8,
    PUIT_ONE = 9,
    PUIT_TWO = 10,
    PUIT_HLOCKED = 11,
    PUIT_MAX = 12,
};

// enum PrivateStoreType : std::int32_t {
//     PST_NONE = 0,
//     PST_OPEN = 1,
//     PST_MANAGE = 2,
//     PST_BUY_OPEN = 3,
//     PST_BUY_MANAGE = 4,
//     PST_RECIPE_SHOP_OPEN = 5,
//     PST_RECIPE_SHOP_MANAGE = 6,
//     PST_OBSERVING = 7,
//     PST_WHOLESALE_OPEN = 8,
//     PST_WHOLESALE_MANAGE = 9,
//     PST_MAX = 10,
// }

enum RestartPoint : std::int32_t {
    RESTART_VILLAGE = 0,
    RESTART_AGIT = 1,
    RESTART_CASTLE = 2,
    RESTART_FORTRESS = 3,
    RESTART_BATTLE_CAMP = 4,
    RESTART_ORIGINAL_PLACE = 5,
    RESTART_VILLAGE_BY_DISMOUNT = 6,
    RESTART_ORIGINAL_PLACE_LIMIT = 7,
    RESTART_ARENA = 8,
    RESTART_BRANCH_START = 20,
    RESTART_BY_AGATHION = 21,
    RESTART_BY_NPC = 22,
};

enum FootEffectEnum : std::int32_t {
    FOOT_EFFECT_NONE = 0,
    COLOSSEUM_1 = 1,
    COLOSSEUM_2 = 2,
    COLOSSEUM_3 = 3,
    COLOSSEUM_4 = 4,
    COLOSSEUM_5 = 5,
    COLOSSEUM_6 = 6,
    COLOSSEUM_7 = 7,
    COLOSSEUM_8 = 8,
    COLOSSEUM_9 = 9,
    COLOSSEUM_10 = 10,
};

enum HearResult : std::int32_t {
    HEAR_OK = 0,
    HEAR_FAIL = 1,
    HEAR_EXCLUDE = 2,
    HEAR_REFUSE = 3,
};


struct AutoSoulShotUi {
    int m_ItemClassID; // +0x00
    int m_IsActiveState; // +0x04
};

static_assert(std::is_aggregate_v<AutoSoulShotUi>);
static_assert(std::is_trivially_default_constructible_v<AutoSoulShotUi>);
static_assert(std::is_trivially_copyable_v<AutoSoulShotUi>);
static_assert(std::is_trivially_destructible_v<AutoSoulShotUi>);

struct AcquiredAlchemySkillInfo {
    AcquiredAlchemySkillInfo(const CSkillInfo* skillInfo, bool master) : pSkillInfo(skillInfo), bMaster(master) {}

    const CSkillInfo* pSkillInfo; // +0x00
    bool bMaster; // +0x08
};

static_assert(std::is_trivially_copyable_v<AcquiredAlchemySkillInfo>);
static_assert(std::is_trivially_destructible_v<AcquiredAlchemySkillInfo>);

struct AcquiredPotentialSkillInfo {
    AcquiredPotentialSkillInfo() : m_nId(0), m_nLevel(0) {}

    AcquiredPotentialSkillInfo(int id, int level) : m_nId(id), m_nLevel(level) {}

    int m_nId; // +0x00
    int m_nLevel; // +0x04
};

static_assert(std::is_trivially_copyable_v<AcquiredPotentialSkillInfo>);
static_assert(std::is_trivially_destructible_v<AcquiredPotentialSkillInfo>);

struct WorldChatData {
    WorldChatData(int nUserID, const char16_t* sName, const char16_t* msg) {
        m_nUserID = nUserID;
        std::memset(m_sName, 0, sizeof(m_sName));
        std::memset(m_Msg, 0, sizeof(m_Msg));

        if (sName) {
            std::size_t index = 0;
            while (index < 24 && sName[index] != u'\0') {
                m_sName[index] = sName[index];
                ++index;
            }
            m_sName[index] = u'\0';
        } else {
            errno = EINVAL;
            runtime::process::HandleInvalidParameter();
        }

        if (msg) {
            std::size_t index = 0;
            while (index < 104 && msg[index] != u'\0') {
                m_Msg[index] = msg[index];
                ++index;
            }
            m_Msg[index] = u'\0';
        } else {
            errno = EINVAL;
            runtime::process::HandleInvalidParameter();
        }
    }

    int m_nUserID; // +0x000
    char16_t m_sName[25]; // +0x004
    char16_t m_Msg[105]; // +0x036
};

static_assert(std::is_trivially_copyable_v<WorldChatData>);
static_assert(std::is_trivially_destructible_v<WorldChatData>);
static_assert(!std::is_default_constructible_v<WorldChatData>);

// Current User.cpp process-lifetime queue owner. The target std::queue
// carrier is 0x30 and the following CYieldLock fixes the complete current
// extent at 0x40.
class WorldChatManager {
public:
    WorldChatManager();
    ~WorldChatManager();

    static WorldChatManager& Inst();

    void ReserveSendChat(WorldChatData& data);
    void ProcessSendChat();

private:
    static WorldChatManager* m_WorldChatManager;

    std::queue<WorldChatData> m_WorldChatQueue; // +0x00
    CYieldLock m_lock; // +0x30
};


struct GroupInzoneEnterableInfo {
    int current; // +0x00
    int max; // +0x04
};


class SharedReuseDelaysOfItems {
public:
    SharedReuseDelaysOfItems();
    ~SharedReuseDelaysOfItems() = default;

    void AddReuseDelay(int userDatabaseId, int sharedReuseGroupId, int reuseEndTime);
    int GetReuseRemainTime(int sharedReuseGroupId);
    bool RemoveReuseDelay(User& user, int sharedReuseGroupId);
    void RemoveReuseDelayAll(User& user);
    bool UseDelayShareItem(User& user, int sharedReuseGroupId, int reuseDelay, int* reuseEndTime);

private:
    std::map<int, int> m_SharedReuseDelays; // +0x00..+0x1f
    int MAX_COUNT; // +0x20..+0x23
    CYieldLock m_lock; // +0x24..+0x33
};


struct BlockListInfo {
    std::u16string szCharName; // +0x00..+0x27
    std::u16string szBlockMemo; // +0x28..+0x4f
};


namespace MatchingPoint {

// Current Win64 image stores each signed matching-point accumulator as a
// two-qword publication cell, followed by the two persisted rate values.
// The complete carrier is 0x38 bytes and sits immediately after
// CPledgeGameUser in User.
struct MatchingPointModel {
    std::int64_t m_nMatchingPointBase[2]; // +0x00..+0x0f
    std::int64_t m_nMatchingPointDiff[2]; // +0x10..+0x1f
    std::int64_t m_nMatchingPointTotal[2]; // +0x20..+0x2f
    float m_fWinningRate[2]; // +0x30..+0x37

    void ValidateMatchingPoint(MATCHINGPOINT_TYPE type);
};


} // namespace MatchingPoint

namespace FriendsEnum {

enum Enum : std::int32_t {
    FRIEND_LOGOUT = 0,
    FRIEND_LOGIN = 1,
    FRIEND_LEVEL_CHANGED = 2,
    FRIEND_CLASS_CHANGED = 3,
};

} // namespace FriendsEnum


// Current L2Server concrete owner at vtable 0x01218bc0. The four slots are
// MemoryObject::AddRef, this Release override, the deleting destructor, and
// inherited MemoryObject::ReportOrt.
class BuilderCmdHistory : public MemoryObject {

public:
    using Entry = std::pair<std::u16string, std::u16string>;

    L2_DECLARE_OBJECT_REGISTRATION(BuilderCmdHistory)

    explicit BuilderCmdHistory(User* user) : myUser(user), m_queueSize(8) {}

    ~BuilderCmdHistory() override { myUser = nullptr; }

    static int __GetClassID() noexcept { return L2_MEMORY_CLASS_ID(BuilderCmdHistory); }

    L2_DECLARE_OBJECT_CLASS_NAME_LITERAL(BuilderCmdHistory)

    L2_DECLARE_OBJECT_SINGLE_ALLOCATOR(BuilderCmdHistory, __GetClassName())

    L2_DECLARE_OBJECT_RELEASE(BuilderCmdHistory)

    void ClearList();
    void AddCmd(const char16_t* command, const char16_t* argument);
    void ListUp();
    Entry GetCmd(int commandNo);

private:
    User* myUser; // +0x18
    std::uint64_t m_queueSize; // +0x20
    std::deque<Entry> m_builderCmdDeque; // +0x28
};


class TalkChannelCache : public Singleton<TalkChannelCache> {
public:
    struct SubChannel {
        struct ChannelData {
            std::uint32_t m_nNpcServerId; // +0x00
            std::array<std::byte, 0x04> m_alignment0004To0008; // +0x04..+0x07
            std::int64_t m_nToken; // +0x08
        };

        bool IsValidChannel(std::uint32_t userServerId, std::uint32_t npcServerId, std::uint32_t unused, std::int64_t token);
        bool ResetCache(std::uint32_t userServerId);
        std::uint32_t Get(std::uint32_t userServerId);
        bool Cache(std::uint32_t userServerId, std::uint32_t npcServerId, std::int64_t token);

        CYieldLock m_Lock; // +0x00..+0x0f
        std::map<std::uint32_t, ChannelData> mUserToNPC; // +0x10..+0x2f
    };

    TalkChannelCache() = default;
    ~TalkChannelCache() override = default;

    int ResetCache(std::uint32_t userServerId);

    SubChannel mChannels[25]; // +0x08..+0x4b7
};


class CUserCloser final : public CIOObject {

public:
    explicit CUserCloser(std::uint32_t userServerId);
    void TimerExpired(int id) override;

private:
    std::uint32_t m_UserSID;
};


class User : public CCreature {
    friend class AtomicAlchemyResult;
    friend class AtomicMonsterBookReward;
    friend class CCaptchaManager;
    friend class ViewCastleInfo;
    friend class ViewCastleList;
    friend class NpcSocket;
    friend class RaidServerInfo;
    friend class RaidClientManager;
    friend class CManorFuncHandler;
    friend class CUserZoneQuestHandler;
    friend class CUserSocket;
    friend class CPetitionCli;
    friend class Say;
    friend class WOlympiadServiceNode;
    friend class WOlympiadServiceProxy;
    friend class WOlympiadTeam;
    friend class WOlympiadMatch;
    friend class WOlympiadUser;
    friend class WOlympiadUsers;
    friend class COlympiadObserverChangeFieldMessage;
    friend class SharedNoblessInfoMap;
    friend bool AutoSoulshotEXPacket(CSocket*, const std::uint8_t*);
    friend bool UserPacketEx::FlyMove(CSocket*, const std::uint8_t*);
    friend bool UserPacketEx::ReturnFromRaid(CSocket*, const std::uint8_t*);
    friend bool UserPacketEx::PartyLootingModify(CSocket*, const std::uint8_t*);
    friend bool UserPacketEx::PartyLootingModifyAgreement(CSocket*, const std::uint8_t*);
    friend bool UserPacketEx::RequestPartyMatchingRoomHistory(CSocket*, const std::uint8_t*);
    friend bool UserPacketEx::ChangeToAwakenedClass(CSocket*, const std::uint8_t*);
    friend bool UserPacketEx::RequestAcquirePotentailSkill(CSocket*, const std::uint8_t*);
    friend bool UserPacketEx::RequestResetPotentialSkill(CSocket*, const std::uint8_t*);
    friend bool UserPacketEx::RequestTryEnsoul(CSocket*, const std::uint8_t*);
    friend bool UserPacketEx::RequestEnsoulExtraction(CSocket*, const std::uint8_t*);
    friend bool UserPacketEx::RequestOneDayReceiveReward(CSocket*, const std::uint8_t*);
    friend bool RestartPacket(CSocket*, const std::uint8_t*);
    friend bool SayPacket2(CSocket*, const std::uint8_t*);
    friend bool RequestAcquireSkill(CSocket*, const std::uint8_t*);
    friend bool LinkHtmlPacket(CSocket*, const std::uint8_t*);
    friend bool RequestPetitionVote(CSocket*, const unsigned char*);
    friend bool GiveItemToPetPacket(CSocket*, const unsigned char*);
    friend bool GetItemFromPetPacket(CSocket*, const unsigned char*);
    friend bool SnoopQuit(CSocket*, const unsigned char*);
    friend bool ObserverEndPacket(CSocket*, const unsigned char*);
    friend bool Handle_OSQ_RETURN(WOlympiadSocketHost*, const std::uint8_t*);
    friend bool Handle_OSQ_RESTART(WOlympiadSocketHost*, const std::uint8_t*);
    friend bool Handle_OSA_SET_HERO_TYPE(WOlympiadSocketHost*, const std::uint8_t*);
    // Current UserSocket_HTMLCmd.cpp warehouse routes replace the private
    // economy owner directly after releasing the preceding one.
    friend bool HtmlCmdDeposit(CUserSocket*, User*, const char16_t*, const char16_t*);
    friend bool HtmlCmdWithdraw(CUserSocket*, User*, const char16_t*, const char16_t*);
    friend bool HtmlCmdPacketDeposit(CUserSocket*, User*, const char16_t*, const char16_t*);
    friend bool HtmlCmdPacketWithdraw(CUserSocket*, User*, const char16_t*, const char16_t*);
    friend bool HtmlCmdDepositCastle(CUserSocket*, User*, const char16_t*, const char16_t*);
    friend bool HtmlCmdWithdrawCastle(CUserSocket*, User*, const char16_t*, const char16_t*);
    friend bool HtmlCmdDepositPledge(CUserSocket*, User*, const char16_t*, const char16_t*);
    friend bool HtmlCmdWithdrawPledge(CUserSocket*, User*, const char16_t*, const char16_t*);
    // Current BuilderCmdHandler.obj writes these two private switches
    // directly in the four matching command bodies.
    friend bool BuilderCmd_friendaddoff(CUserSocket*, User*, char16_t*);
    friend bool BuilderCmd_friendaddon(CUserSocket*, User*, char16_t*);
    friend bool BuilderCmd_tradeon(CUserSocket*, User*, char16_t*);
    friend bool BuilderCmd_tradeoff(CUserSocket*, User*, char16_t*);
    friend bool BuilderCmd_gmon(CUserSocket*, User*, char16_t*);
    // Current premium-item and bot-report commands mutate their physical
    // User-owned maps/counters directly.
    friend bool BuilderCmd_Premium_item(CUserSocket*, User*, char16_t*);
    friend bool BuilderCmd::inc_bot_count(CUserSocket*, User*, char16_t*);
    friend bool BuilderCmd::reset_bot_count(CUserSocket*, User*, char16_t*);
    friend bool BuilderCmd::set_bot_count(CUserSocket*, User*, char16_t*);
    // Current hide/whisper command bodies write their physical User-owned
    // visibility UI and whisper switches directly.
    friend bool BuilderCmd_hide(CUserSocket*, User*, char16_t*);
    friend bool BuilderCmd_whisper(CUserSocket*, User*, char16_t*);
    friend bool BuilderCmd_petivotetest(CUserSocket*, User*, char16_t*);
    friend bool BuilderCmd_actorcount(CUserSocket*, User*, char16_t*);
    // Current BuilderCmd_snoop reads and repairs both physical snoop IDs
    // directly before delegating the bidirectional update to SetSnoop.
    friend bool BuilderCmd_snoop(CUserSocket*, User*, char16_t*);
    // Current BuilderCmd_blockuser reads the physical block-list count
    // directly for its distinct 0x201 overflow boundary.
    friend bool BuilderCmd_blockuser(CUserSocket*, User*, char16_t*);
    // Current BuilderCmd_subjob uses the physical User-owned transition
    // flag for each create/delete/change/renew/cancel request.
    friend bool BuilderCmd_subjob(CUserSocket*, User*, char16_t*);
    // Current builder callback writes the physical per-user party-room
    // waiting-list limit directly.
    friend bool BuilderCmd_set_partyroom_waiting_count(CUserSocket*, User*, char16_t*);
    // Current BuilderCmd_check_bot calls the physical per-user checker
    // directly after resolving the optional selected target.
    friend bool BuilderCmd_check_bot(CUserSocket*, User*, char16_t*);
    // Current BuilderCmd_InZone resolves the physical associated in-zone
    // server ID directly for the status command.
    friend bool BuilderCmd_InZone(CUserSocket*, User*, char16_t*);
    friend bool BuilderCmd::CSCharacterInfoPacket(CUserSocket*, char16_t*);
    // Current BuilderCmd::answer_captcha reads the physical transaction ID
    // directly before calling the already-owned captcha manager terminal.
    friend bool BuilderCmd::answer_captcha(CUserSocket*, User*, char16_t*);
    // Current BuilderCmd::reset_skill_reuse_time writes the physical
    // User-owned daily reuse reset time after clearing and publishing reuse.
    friend bool BuilderCmd::reset_skill_reuse_time(CUserSocket*, User*, char16_t*);
    // Current BuilderCmd::talk_npc reads the physical polymorph NPC owner
    // directly before publishing the polymorph dialog packet.
    friend bool BuilderCmd::talk_npc(CUserSocket*, User*, char16_t*);
    // Current save_external_notify formats the physical chat-name field
    // directly before publishing the DB request.
    friend bool BuilderCmd::save_external_notify(CUserSocket*, User*, char16_t*);
    // Current pledge-member and periodic-henna builder callbacks write their
    // User-owned selection/henna state directly.
    friend bool BuilderCmd::addpledgemem(CUserSocket*, User*, char16_t*);
    friend bool BuilderCmd::set_henna_period(CUserSocket*, User*, char16_t*);
    // Current make_chao and screen_notice callbacks write these two
    // User-owned state contracts directly.
    friend bool BuilderCmd::make_chao(CUserSocket*, User*, char16_t*);
    friend bool BuilderCmd::screenNotice(CUserSocket*, User*, char16_t*);
    // Current pledge-bonus builder callback mutates the three physical
    // per-user timestamp lanes directly.
    friend bool BuilderCmd::PledgeBonus(CUserSocket*, User*, char16_t*);

    struct PeriodicItemData {
        std::int32_t itemDBId;
        std::int32_t startTime;
        std::int32_t itemPeriod;
        std::int32_t itemClassId;
    };

    struct UserPeriodicItemMapDataType {
        std::int32_t itemClassId;
        std::int32_t expireTime;
    };

    using UserPeriodicItemMapType = std::map<std::int32_t, UserPeriodicItemMapDataType>;


    void _InsertPeriodicShapeShiftedItem(PeriodicItemData& itemData);
    void _InsertPeriodicItem(PeriodicItemData& itemData);
    bool _CheckPeriodicItemOnTick(const UserPeriodicItemMapType& periodicItems, int currentTime);
    bool _CheckPeriodicShapeShiftedItemOnTick(const UserPeriodicItemMapType& periodicItems, int currentTime);
    void _SendPeriodicShapeShiftedItemList();

public:
    L2_DECLARE_MEMORY_ALLOCATION(User)

    enum SYSTEM_BUFF_TYPE : std::int32_t {
        BUFF_ALL = 0,
        BUFF_PREMIUM_SPECIAL = 1,
    };

    struct UpdateFriendData {
        FriendsEnum::Enum eType; // +0x00
        std::int32_t nWorldID; // +0x04
        std::uint32_t nDBID; // +0x08
    };

    struct UserRaidInfo {
        std::int32_t m_nRaidWorldId; // +0x00
        std::array<std::byte, 0x04> m_alignment0004To0008; // +0x04..+0x07
        FVector m_OriginPos; // +0x08..+0x1f
    };

    struct OnGoingZoneQuest {
        std::int32_t dQuestId; // +0x00
        std::int32_t serialId; // +0x04
    };

    struct AcquireInfo {
        AcquireInfo() :
            m_Id(0),
            m_Level(0),
            max_level(0),
            m_nSubLevel(0),
            sp_consume(0),
            m_ItemConsume(false),
            m_MaximumPledgeType(ACADEMY),
            m_pAcquireInfo(nullptr) {}

        int m_Id; // +0x00
        int m_Level; // +0x04
        int max_level; // +0x08
        int m_nSubLevel; // +0x0c
        std::int64_t sp_consume; // +0x10
        bool m_ItemConsume; // +0x18
        PledgeType m_MaximumPledgeType; // +0x1c
        SkillAcquireInfo* m_pAcquireInfo; // +0x20
    };


    enum PrivateStoreMessageType : std::int32_t {
        PSMT_NO_BROADCAST = 0,
        PSMT_BROADCAST_EXCEPT_ME = 1,
        PSMT_CHANGE_MESSAGE = 2,
    };

    enum CheckCrystalType : std::int32_t {
        CCT_WITHIN_CRYSTAL_LEVEL = 0,
        CCT_BEYOND_CRYSTAL_LEVEL = 1,
        CCT_ANY_CRYSTAL_LEVEL = 2,
    };

    enum CallPCResult : std::int32_t {
        CPR_SUCCESS = 0,
        CPR_UNENABLEAREA = 4,
        CPR_ETC = 5,
    };

    enum OneSummonType : std::int32_t {
        SUMMON_SINGLE = 0,
        SUMMON_MULTI = 1,
        SUMMON_ANY = 2,
    };

    using PrivateStoreMap = std::map<int, CPrivateStoreData*>;

    struct UnEquipItemQueue {
        std::uint32_t UnEquipItemServerID; // +0x00
        SlotType UnEquipSlotType; // +0x04
        bool bDelete; // +0x08
    };

    struct ModifySkillList {
        std::int32_t nSkillID;
        std::int32_t nSkillLevel;
        std::int32_t nSkillSubLevel;
        std::int32_t nSourceId;
    };

    using ModifySkillMap = std::map<std::CTriKey<AbnormalStatusSlotType, int, int>, ModifySkillList>;

    struct ItemRefund {
        CItem* m_pItem; // +0x00
        std::int64_t m_Amount; // +0x08
        std::int64_t m_Price; // +0x10

        ~ItemRefund();
    };

    struct UserInzoneBindingData {
        std::int32_t inzoneTypeId; // +0x00
        bool groupRestriction; // +0x04
        std::array<std::byte, 0x03> alignment05To08; // +0x05..+0x07
        std::int32_t latestBindingTime; // +0x08
        std::int32_t entranceCount; // +0x0c
        std::int32_t maxEntranceCount; // +0x10
    };

    using UserInzoneBindingMap = std::map<int, UserInzoneBindingData>;

    using InZoneRestrictionMap = std::map<int, int>;
    using GroupInZoneRestrictionMap = std::map<int, GroupInzoneEnterableInfo>;

    struct UserInzonePoint {
        std::int32_t m_nTypeId; // +0x00
        std::int32_t m_nPoint; // +0x04
        std::int32_t m_nTimeStamp; // +0x08
    };

    using UserInzonePointMap = std::map<int, UserInzonePoint>;

    struct etcMsg {
        etcMsg();
        etcMsg(const etcMsg& other);
        etcMsg(etcMsg&& other);
        ~etcMsg();

        etcMsg& operator=(const etcMsg& other);
        etcMsg& operator=(etcMsg&& other);

        bool bParamFirst; // +0x00
        bool bParamSecond; // +0x01
        std::int32_t nParamFirst; // +0x04
        std::int32_t nParamSecond; // +0x08
        std::int64_t dParamFirst; // +0x10
        std::u16string strParamFirst; // +0x18
        std::u16string strParamSecond; // +0x40
        std::uint32_t dwParam; // +0x68
    };

    struct ManipulateQueue {
        ManipulateQueue();
        ManipulateQueue(const ManipulateQueue& other);
        ManipulateQueue(ManipulateQueue&& other);
        ~ManipulateQueue();

        ManipulateQueue& operator=(const ManipulateQueue& other);
        ManipulateQueue& operator=(ManipulateQueue&& other);

        ManipulateType::Enum eType; // +0x0000
        std::int32_t nId; // +0x0004
        std::int32_t nCount; // +0x0008
        std::array<ItemManip, 100> aRequestList; // +0x0010..+0x12cf
        AtomicJob* pAtomicJob; // +0x12d0
        std::int32_t nEtcManiCount; // +0x12d8
        CPacketHelperEx<20480> pEtcManiPacket; // +0x12e0..+0x62ef
        bool bIsPremium; // +0x62f0
        ItemGetReason::Enum reason; // +0x62f4
        etcMsg eMsg; // +0x62f8..+0x6367
    };

    enum ManiItemResultType : std::int32_t {
        MRT_NOJOB = 0,
        MRT_JOBMAP = 1,
        MRT_JOBNOTMAP = 2,
    };

    using ManiItemJobKey = std::CDualKey<ManipulateType::Enum, int>;
    using ManiItemJobMap = std::map<ManiItemJobKey, ManipulateQueue>;

    struct BeautyPurchaseData {
        BeautyPurchaseData();
        BeautyPurchaseData(const BeautyPurchaseData& other);
        BeautyPurchaseData(BeautyPurchaseData&& other);
        ~BeautyPurchaseData();

        BeautyPurchaseData& operator=(const BeautyPurchaseData& other);
        BeautyPurchaseData& operator=(BeautyPurchaseData&& other);

        // L2_GUARD passes the caller location. The original inline Enter
        // recorded its own User.h line 0x567 instead.
        void Enter(int index, const wchar_t* file, int line);
        void Leave(int index);

        CYieldLock m_lock[3]; // +0x00..+0x2f
        std::map<std::CDualKey<int, int>, bool> m_BuyInfo[3]; // +0x30..+0x8f
        volatile std::int32_t m_nLoaded; // +0x90
    };

    User(
        char16_t* characterName,
        char16_t* accountName,
        unsigned int databaseId,
        unsigned int accountId,
        unsigned int pledgeId,
        int builderLevel,
        int sex,
        int race,
        int classType,
        int worldId,
        int x,
        int y,
        int z,
        double hp,
        double mp,
        std::int64_t sp,
        std::int64_t experience,
        int level,
        int karma,
        int pkCount,
        unsigned int duelCount,
        unsigned int pkPardonCount,
        int face,
        int hairShape,
        int hairColor,
        char16_t* nickname,
        unsigned char* opaqueCharacterData,
        unsigned int pledgeDismissTime,
        unsigned int pledgeOustedTime,
        unsigned int pledgeWithdrawTime,
        int subJobId,
        int subJob0Class,
        int subJob1Class,
        int subJob2Class,
        int subJob3Class,
        int dualClassId,
        int cp,
        int ssqDawnRound,
        int pledgeType,
        int gradeId,
        int academyPledgeDatabaseId,
        bool pcCafeUser,
        int premiumLevel,
        int characterNameColor,
        int nicknameColor,
        int associatedInstantZoneId,
        int associatedInstantZoneParam,
        int bookmarkSlotCount,
        _SYSTEMTIME* lastSocialityCheck,
        int socialityCount,
        int socialityArgument,
        CVitalData& vitalityData,
        int hennaId1,
        int hennaId2,
        int hennaId3,
        int voteBonusTimeLimit,
        int votePlayTime,
        int voteCount,
        int voteBonusCount,
        int votePlayTimeCount,
        _SYSTEMTIME* voteTime,
        int jobGroup,
        int faceEx,
        int hairShapeEx,
        int hairColorEx,
        int abilityPoint
    );
    ~User() override;

    void InitAtConstructor(
        char16_t* characterName, char16_t* accountName, unsigned int databaseId, unsigned int accountId, unsigned int worldId
    );
    void InitUser();
    void SetAtConstructor(
        unsigned int pledgeId,
        int builderLevel,
        int sex,
        int race,
        int classType,
        int worldId,
        int x,
        int y,
        int z,
        double hp,
        double mp,
        std::int64_t sp,
        std::int64_t experience,
        int level,
        int karma,
        int pkCount,
        unsigned int duelCount,
        unsigned int pkPardonCount,
        int face,
        int hairShape,
        int hairColor,
        char16_t* nickname,
        unsigned char* opaqueCharacterData,
        unsigned int pledgeDismissTime,
        unsigned int pledgeOustedTime,
        unsigned int pledgeWithdrawTime,
        int subJobId,
        int subJob0Class,
        int subJob1Class,
        int subJob2Class,
        int subJob3Class,
        int dualClassId,
        int cp,
        int ssqDawnRound,
        int pledgeType,
        int gradeId,
        int academyPledgeDatabaseId,
        bool pcCafeUser,
        int premiumLevel,
        int characterNameColor,
        int nicknameColor,
        int associatedInstantZoneId,
        int associatedInstantZoneParam,
        int bookmarkSlotCount,
        _SYSTEMTIME* lastSocialityCheck,
        int socialityCount,
        int socialityArgument,
        CVitalData& vitalityData,
        int hennaId1,
        int hennaId2,
        int hennaId3,
        int voteBonusTimeLimit,
        int votePlayTime,
        int voteCount,
        int voteBonusCount,
        int votePlayTimeCount,
        _SYSTEMTIME* voteTime,
        int jobGroup,
        int faceEx,
        int hairShapeEx,
        int hairColorEx,
        int abilityPoint
    );

    User(const User&) = delete;
    User& operator=(const User&) = delete;

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void ReportOrt(const char* callerFile, int callerLine) override;
    void TimerExpired(int id) override;
    bool CanBeSeenBy(CObject& object) override;
    bool IsMakeAttackerGuilty(CCreature* attacker, bool flag) override;
    bool IsMakeAttackerChao(CCreature* attacker) override;
    bool IsPC() const override;
    bool IsPCOrSummon() const override;
    bool IsPCOrPCRelatedNPC() const override;
    User* CastUser() override;
    User* GetUserOrMaster() override;

    bool IsRealEnemyTo(CCreature* creature) override;
    bool IsEnemyTo(CCreature* creature) override;
    bool IsAlive() override;
    void InSight(CObject* object, bool flag) override;
    void SendInSight(CObject* object, bool flag);
    void SendOutOfSight(CObject* object, bool flag);
    void SendDeleteObject(CObject* object, bool flag);
    void OutOfSight(CObject* object, bool flag) override;
    const char16_t* GetName() const override;
    void SendDB(const char* format, ...) const override;
    const char16_t* GetName2(User* user) override;
    AttackType GetAttackType(CSharedItemData** itemData) override;
    void SetDBID(unsigned int dbId) override;
    void SetPeaceZone(bool peaceZone) override;
    void OnCreate() override;
    void Delete() override;
    void OnEnterWorld(FVector& position) override; // Current L2Server 0x00bc0f68.
    void LeaveWorldForRaid();
    void Say(const char16_t* message) override;
    void Shout(const char16_t* message);
    void Market(const char16_t* message);
    void Harangue(const char16_t* message);
    void WorldSay(const char16_t* message);
    void Tell(User* target, const char16_t* message);

private:
    void BroadcastSay(const char16_t* message, SayPacketType type, int distance);

public:
    bool ChangeClass(ClassType classType, int level) override;
    double EquipedArmorsValidate(VALIDATE::Enum type) const override;
    void ChangeInventorySlot(std::map<int, int>& slotIndices);
    std::int64_t ExpInc(std::int64_t experience, bool applyBonus) override;
    void AccumulateExpForItem(std::int64_t experience);
    void SetGuilty(char guilty) override;
    char GetGuilty() override;
    bool IsGuilty() override;
    void SetKarma(int karma) override;
    CrystalType GetMaxCrystal() const override;
    void OnGotDamage(CCreature* attacker, double damage) override;
    double RegenBonusByMotionEx() override;
    double HPRegenBonusByMotionEx() override;
    double MPRegenBonusByMotionEx() override;
    double CPRegenBonusByMotionEx() override;
    bool ValidateBaseHP() override;
    bool ValidateOrgAttackSpeed() override;
    bool ValidateStorageSlot() override;
    bool ValidateAllOnWeaponArmors() override;
    void ValidateAllOnSIM() override;
    void ValidateParameters(int parameter, bool force) override;
    bool ValidateOrgHP() override;
    bool ValidateOrgMP() override;
    bool ValidateOrgCP() override;
    bool ValidateOrgHPRegen() override;
    bool ValidateOrgMPRegen() override;
    bool ValidateOrgCPRegen() override;
    bool ValidateOrgPAttack() override;
    bool ValidateOrgMAttack() override;
    CUserSocket* GetSocket() const override;
    void Set(CUserSocket* socket);
    bool AdjustStartPoint(FVector& position);
    int GetInventoryLimit() override;
    int GetQuestInventoryLimit() override;
    int GetArtifactInventoryLimit() override;
    int GetPrivateStoreLimit() override;
    int GetPrivateStoreBuyLimit() override;
    int GetPrivateBankLimit() override;
    bool ValidateMUseSpeed() override;
    bool ValidatePUseSpeed() override;
    bool CanUseShot(const CSkillInfo* skillInfo) override;
    bool Morph(int classId) override;
    void UnMorph() override;
    void ChangeMorph() override;
    bool ValidateOrgSpeed() override;
    void InitCoupleAction() override;
    bool ValidateAttackSpeed() override;
    bool ValidateSpeed() override;
    bool ValidateLevelUP() override;
    double GetMoveModifier() override;
    void CheckSummonBattleMode() override;
    void CheckSummonBattleTimeStamp() override;
    bool CanEquip(CItem* item) override;
    void UnequipItemWhenBeChao(int previousKarma);
    void TakeOffItems() override;
    void ActivateEquipSkills();
    int EquipItem(CItem* item, SlotType slotType) override;
    SlotType UnequipItem(CItem* item, bool force) override;
    SlotType UnequipItem(SlotType slotType, bool force) override;
    void AutoEquipArrowOrBolt(CItem* item);
    void UnequipArmor(SlotType slotType) override;
    bool SwapAgathionSlotItems(CItem* firstItem, SlotType firstSlot, CItem* secondItem, SlotType secondSlot) override;
    void _OnSwapAgathionSlotItems(CItem* firstItem, SlotType firstSlot, CItem* secondItem, SlotType secondSlot) override;
    void _OnEquipItem(CItem* item, SlotType slotType) override;
    void _OnUnequipItemProcess(CItem* item, SlotType slotType) override;
    void _OnUnequipItem(CItem* item, SlotType slotType) override;
    void UnequipWeapon(bool force) override;
    void RestoreUnequipedWeapon() override;
    void RestoreUnequipedArmor(SlotType slotType) override;
    bool UseItem(CItem& item, int count, bool force, int unknown) override;
    bool DropItem(CItem* item, std::int64_t count, FVector& position, bool unknown) override;
    int DropItemAll(bool force);
    int DestroyItemAll();
    bool BuyMonRaceTicket(int itemClassId, int raceId, int laneAndFee, int amount);
    bool CancelEquipmentAddedEffectAndDropItem(
        CItem* item, std::int64_t count, int effectMask, FVector* position, bool setContributeData
    );
    bool GetItem(CItem* item, bool unknown) override;
    bool StopStore(bool first, bool second) override;
    bool CancelStore() override;
    using CCreature::SendSystemMessage;
    void SendSystemMessage(const char16_t* message, const char16_t* argument) override;
    void SendSystemMessage(unsigned int messageId) override;
    void SendSystemMessage_d(unsigned int messageId, int value) override;
    void SendSystemMessage_Q(unsigned int messageId, std::int64_t value) override;
    void SendSystemMessageWithSelf(unsigned int messageId) override;
    bool AddItemToInventory2(CItem* item) override;
    bool DeleteItemInInventory(int itemClassId, std::int64_t count, AtomicJob* job, bool first, bool second) override;
    bool DeleteItemInInventory(int itemIndex) override;
    bool DeleteItemInInventoryBeforeCommit(int itemClassId, std::int64_t count) override;
    bool DeleteItemInInventory2(int itemClassId, std::int64_t count, AtomicJob* job);
    std::int64_t HasItem(unsigned int itemClassId) override;
    void EnterWorld();
    void LeaveWorld(bool save) override;
    bool TeleportToLocation(
        int x,
        int y,
        int z,
        unsigned int inZoneId,
        int heading,
        TeleportSourceType::Enum source,
        CCreature* requestor,
        int unknown
    ) override;
    bool GetOnAirShip(CAirShip* airShip) override;
    bool GetOffAirShip(CAirShip* airShip, FVector position) override;
    bool TeleportWithAirShip(CAirShip* airShip, FVector position, unsigned int inZoneId) override;
    bool Die(CCreature* killer, int reason) override;
    void SetDieWithSpecialResurrection(double recoverHp, double recoverMp, double recoverCp);
    void OnDiePenaltyInBF();
    void ChangeKillScoreInSiege();
    void DieCursedUser();
    void DieByCursedUser(User* killer);
    void SendResurrectionConfirmDlg() override;
    void AttackOutOfRange() override;
    void Suicide(DamageTypeEnum damageType, int reason) override;
    void OnKillNpc(CCreature* creature) override;
    void SocialAction(int actionId, int unknown, bool broadcast) override;
    void BroadcastSocialAction(int actionId, int distance, int parameter);
    void ActionFailed(int reason) override;
    bool BasicAction(ServerTriggeredUserAction action, bool controlPressed, bool shiftPressed, StopType stopType);
    bool BasicAction(int actionId, bool controlPressed, bool shiftPressed, StopType stopType);
    bool IsValidAction(int actionId);
    void ChangeTarget(CObject* target, ChangeTargetReason reason) override;
    int GetSkillSubLevel(int skillId) override;

    void ParamChanged(std::int64_t parameterMask) override;
    void ParamChanged() override;
    void ObserverStart(int x, int y, int z, int yaw, int pitch, int period);
    void ObserverEnd();
    void ShowHTML(
        const char16_t* fileName,
        const char16_t* html,
        unsigned int npcId,
        HTML::DisplayType displayType,
        bool useUserObjectId
    );
    void ShowQuestHTML(const char16_t* fileName, const char16_t* html, int questId);
    void ShowHTML(const char16_t* fileName);
    bool ShowMultiSell(int npcIndex, int listId);
    void ValidateExParameters(int parameterMask) override;
    bool CanNormalAttack() const override;
    void OnAreaCheckForEveryTick(int tick) override;
    void OnLevelChanged(int oldLevel) override;
    void ChangeTalkTarget(CObject* target) override;
    int GetSkillLevel(int skillId) override;
    void ValidateSkillList() override;
    int GetBroadcastDist(const int distance) const override;
    bool FreezeIfNobodyNearMe() override;
    void SendFreezeStatusMessage() override;
    int CalcCriminalRate() override;
    void BroadcastHP(int value, unsigned int unknown) override;
    void BroadcastHPMP(int value, unsigned int unknown) override;
    int GetCorpseTime() const override;
    double IntBonusF() const override;
    double DexBonusF() const override;
    double MenBonusF() const override;
    double ConBonusF() const override;
    double StrBonusF() const override;
    double WitBonusF() const override;
    double LucBonusF() const override;
    double ChaBonusF() const override;
    bool OnMagicSkillUsePacket(int skillId, bool first, bool second) override;
    bool IsInDuel() const override;
    bool IsOlympiadObserver() const;
    bool IsAliveInOlympiad() const;
    bool IsOlympiadPlayer() const;
    int GetOlympiadFieldID() const;
    int GetOlympiadTeamID();
    bool CanAttackInOlympiad() const;
    bool IsOlympiadPlaying() const;
    bool IsRegisteredAtOlympiad() const;
    bool CanRegisterAtOlympiad();
    bool IsDuelEnemy(CCreature* creature) override;
    bool IsDuelAlly(CCreature* creature) override;
    bool IncreaseSoulCount(int count, const int emitterId) override;
    bool DecreaseSoulCount(int count) override;
    bool IsFrozen(bool unknown) override;
    bool IsCameraMode() override;
    void SetInCombatMode(bool inCombat) override;
    void CheckRelaxType() override;
    void _OnAbnormalStatusChanged(
        const CSkillInfo& skillInfo, CCreature* caster, SkillPumpStartType startType, bool unknown
    ) override;
    void _OnAbnormalStatusDeleted(
        const CSkillInfo& skillInfo, CCreature* caster, SkillPumpEndType endType, int unknown
    ) override;
    void OnActivate() override;
    bool IsSkillDebugger() const override;
    bool IsSkillDebugMode() const override;
    bool CanDoAction_china() override;
    bool CanForceAttack() const override;
    bool CanSoulCollectByExp(std::int64_t experience) const override;
    void AnnouncePickItem(CItem* item) override;
    CParty* GetParty() override;
    void SendMyInfo() override;
    bool IsSummonedNpc() override;
    bool IsSummonedFriendNpc() override;
    void MakeEnterWorldPacket() override;
    void ClearEnterWorldPacket() override;

    // User-added virtual slots 422..502. Keep this declaration order: the
    // current image fixes every target through the 503-entry User vtable.
    virtual void SendDB(RQ_PROTOCOL protocol, RQ_EX_PROTOCOL extendedProtocol, network::IPacket& packet) const;
    virtual std::uint32_t GetAccountID();
    virtual std::uint32_t GetServerId();
    virtual void SetCloseLater();
    virtual void SendSkillList(CUserSocket* socket, bool includePreviousSkills, int unknown);
    void _PackingSkillList(SendSkillListData& data, const CSkillInfo* skillInfo, SkillType skillType);
    virtual bool DecreaseEnergyCount(int count);
    virtual void SetItemCrystalEquipPenalty(CrystalType crystalType);
    virtual void SetItemCrystalOriginal(CrystalType crystalType);
    virtual CrystalType GetOriginalCrystal();
    virtual bool HandleItemManipulation(
        int count,
        ItemManip requestList[],
        AtomicJob* atomicJob,
        int etcManipulationCount,
        CPacketHelperEx<20480>* etcManipulationPacket,
        bool premium,
        ItemGetReason::Enum reason
    );
    virtual bool _HandleItemManipulation(ManipulateQueue queue);
    virtual CPledge* GetPledge();
    void SetPledge(CPledge* pledge);
    void SetAlliance(CAlliance* alliance, bool masterPledge);
    void SetPledgeMasterSkill();
    CAlliance* GetAlliance();
    int GetAllianceDbId();
    bool HasWorldAgit();
    bool HasCastle();
    bool DeclareLord();
    bool HasFortress();
    int GetAffectedBattleCampType();
    bool IsInFortressSiege();
    bool IsInAgitWar(bool includeAnyWorldAgit);
    bool IsNoSiegeRelationForBookmark();
    bool IsInNoCallArea();
    static bool IsCallPCEnable(User* source, User* target, bool includeDead, CallPCResult& result);
    void CallPCTry(User* caller, int itemClassId, int itemCount, bool includeDead);
    void CallPCAnswer(L2ServerLong requestId, bool answer);
    void CallPCVerified(L2ServerLong recallerRequestId);
    bool CheckMyPledgeDeclareWar(User* user);
    bool IsInPledgeWarAgainst(User* user);
    bool IsInPledgeWarBoth(User* user);
    void HandleOwnThing(User* killer);
    void SendPledgeAnnounce();
    void RemoveOwnThing();
    int IsInMySiegeBattleField();
    void CheckPledgeMasterOnline();
    void GetPVPPointByTimerInBattleField();
    bool CanInstallBattleCamp();
    bool CanInstallAdvancedBattleCamp();
    void InstallBattleCamp(bool advanced);
    void InstallAdvancedBattleCamp();
    void UninstallAdvancedBattleCamp();
    bool CanResurrection(int& castleSiegeStatus, int& fortressSiegeStatus);
    int GetMyStatus();
    std::uint32_t GetPledgePowerByGrade();
    bool HavePledgePower(PledgePowerEnum power);
    bool DropCastleGuard(CItem* item, unsigned int amount);
    bool HasFlagOfSiege();
    void CheckHasFlag();
    void ProcessUnderWaterEffect();
    void ProcessVioletBoy();
    void DeleteOwnThing(bool appear, bool returnToDominion);
    bool CanPossessHolyThing(CHolyThing* holyThing, const CSkillInfo* skillInfo);
    virtual bool SummonCreature(
        const wchar_t* npcName,
        int masterExpRatio,
        const wchar_t* consumeItemName,
        std::int64_t consumeItemCount,
        int originalDuration,
        int summonLifeTime,
        CSkillInfo* skillInfo
    );
    bool SummonCreature(
        CSummon* summon,
        int masterExpRatio,
        int consumeItemClassId,
        std::int64_t consumeItemCount,
        int originalDuration,
        int summonLifeTime,
        CSkillInfo* skillInfo,
        bool fixedIndex,
        int summonIndex
    );
    virtual bool SummonNPCRelatedToPledge(const wchar_t* npcName, CSkillInfo* skillInfo);
    bool SummonNPCRelatedToPledge(CNPC* npc, CSkillInfo* skillInfo);
    virtual bool ValidateBaseMP();
    virtual bool GatherSkillAcquireList(
        std::map<CSkillKey, AcquireInfo>& acquireList,
        int& count,
        EtcSkillAcquireType acquireType,
        bool includePreviousSkills
    );
    virtual bool ValidateItemSkills(); // Current L2Server 0x00bafb00.
    virtual void OnTeleport();
    virtual int GetBeltInventoryLimit();
    virtual void SendUserInfo(CUserSocket* socket, CVariableBitSet* updateBits);
    void SendUserInfo2(bool gmPacket, CUserSocket* socket);
    virtual bool SendUserHPInfo(int currentHp, int maximumHp, bool force);
    virtual bool SendUserMPInfo(int currentMp, int maximumMp, bool force);
    virtual bool SendUserSubJobInfo(SubJob::SubJobPacketType packetType);
    virtual void BroadcastCP(int currentCp, std::uint32_t objectId);
    virtual void SendAbnormalStatusInfo();
    virtual void SendETCBuffStatusInfo();
    virtual void _SendShortBuffStatusInfo();
    virtual void SendShortBuffStatusInfo();
    virtual void SendSystemMessage_SysString(std::uint32_t messageId, std::uint32_t parameter);
    virtual bool OnActionPacket(int, bool);
    virtual bool OnMoveToLocationPacket(int, int, int, int, int, int, int);
    virtual bool OnRequestAcquireSkillPacket(int, int, EtcSkillAcquireType, const std::uint8_t*);
    virtual bool OnRestartPointPacket(RestartPoint, int);
    virtual bool OnStartPledgeWarPacket(const wchar_t*);
    bool CanStopPledgeWar(const wchar_t* pledgeName);
    virtual bool OnStopPledgeWarPacket(const wchar_t*);
    virtual bool OnDuelAskStart(const wchar_t*, DuelType);
    virtual bool OnDuelAcceptStart(DuelType, int, int);
    virtual bool OnTargetUnselectedPacket(std::int16_t);
    virtual bool OnUseItemPacket(std::uint32_t, int);
    virtual bool OnExAutoSoulShotPacket(int, bool);
    virtual bool OnExAutoSoulShotPacket(int, bool, int, int);
    virtual bool OnVoteSocialityPacket(int);
    virtual bool OnAppearingPacket();
    virtual bool OnAcceptJoinMPCC(int, int);
    virtual bool OnEnterWorldPacket(CUserSocket*, const std::uint8_t*);
    virtual bool OnRequestUserCommand(int);
    virtual bool OnValidateLocationPacket(int, int, int, int, int, char);
    virtual bool OnDropItemPacket(std::uint32_t, std::int64_t, int, int, int);
    virtual bool OnDestroyItemPacket(int, std::int64_t);
    virtual bool OnRequestEnchantItemPacket(int);
    virtual bool OnMoveToLocationAirShipPacket(const std::uint8_t*);
    virtual bool OnStartRotating(int, int);
    virtual bool OnFinishRotating(int);
    virtual bool OnTradeRequestPacket(int);
    virtual bool OnTradeStartPacket(int);
    virtual bool OnTradeDonePacket(std::uint32_t);
    virtual bool OnGetItemPacket(int, std::int64_t);
    virtual bool OnPledgeEmblem(std::uint32_t, std::uint32_t);
    virtual bool OnPledgeCrest(std::uint32_t, std::uint32_t);
    virtual bool OnPrivateStoreBuyBuyListSend(const std::uint8_t*);
    virtual bool OnSellPacket(const std::uint8_t*);
    virtual bool OnBuyPacket(const std::uint8_t*);
    virtual bool OnRefundPacket(const std::uint8_t*);
    virtual bool OnUnequipItemPacket(std::int64_t);
    virtual bool OnAttackPacket(std::uint32_t, char);
    virtual bool OnDestroyQuest(int);
    virtual bool OnRequestChangeAttributePacket(int, int, int);
    virtual bool OnChangeAttributeItemPacket(int, int);
    virtual bool OnRequestShapeShifting(int);
    virtual bool OnRequestKalieToken();
    virtual bool OnRequestBalthusToken();
    virtual bool OnReplyPickCursedWeapon(CItem&);
    virtual bool OnReplyUseItemPacket(int, std::int64_t, int);
    virtual bool OnReplyUseRecipeItem(int);
    virtual bool OnSellItemsPacket(int, int, std::int64_t, BuySellResult, const std::uint8_t*);
    virtual bool OnNpcCreateSubJobPacket(int, int);
    virtual bool OnNpcChangeSubJobPacket(int, int);
    virtual bool OnNpcRenewSubJobPacket(int, int, int, int);
    virtual bool OnNpcCreateDualJobPacket(int, int);
    virtual bool OnNpcStartScenePlayerPacket(int, int, int, int, int, int, int);
    virtual bool OnNpcShowBuySellPagePacket(CNPC&, std::int64_t, std::int64_t, double);

    CTransform* IsTransformed(TransformType::Enum transformType) const;
    CTransform* IsTransformedExcept(TransformType::Enum transformType) const;
    CTransform* IsTransformedExceptModeChange() const;
    CTransform* IsCombatTransformed() const;
    TransformType::Enum GetTransformType() const;
    bool CanGetOnVehicle();
    int GetEquipDurationItemCount();
    int DecreaseEquippedItemDurationOnTimerExpired();
    void UpdateSlotItemInInventory(SlotType slotType, InventoryUpdateType updateType);
    void SaveDurationItem(CItem* item);
    bool _AddEquipDurationItem(CItem* item);
    bool _DelEquipDurationItem(CItem* item);
    bool CheckAndConsumeDuration(int itemClassId);
    bool CheckAndSetPendingTeleportItem(int itemClassId, std::int64_t amount, int x, int y, int z);
    void SetPendingTeleportItem(int itemClassId, std::int64_t amount);
    int GetPendingTeleportItemClassID();
    std::int64_t GetPendingTeleportItemAmount();
    std::uint32_t GetLastHTMLCmdTeleportTime() { return m_dwLastHTMLCmdTeleportTime; }
    void SetLastHTMLCmdTeleportTime(std::uint32_t time) { m_dwLastHTMLCmdTeleportTime = time; }
    void PayPendingTeleportItem();
    void PayTeleportAddItem(int itemClassId, int amount);
    bool SetItemPeriod(CItem& item, int periodMinutes);
    int GetPeriodLeft(int itemDbId);
    int GetPeriodLeftForShapeShiftedItem(int itemDbId);
    void OnGetPeriodicItem(CItem* item);
    void OnLoadPeriodicItemInfo(int itemCount, const unsigned char* packet);
    void CheckPeriodicItemOnTick();
    void SendPeriodicItemList();
    bool OnDestroyItem(CItem* item, std::int64_t amount, bool& inUserInventory);
    bool DestroyItemReturn(CItem* item, std::int64_t amount);
    void OnPeriodicItemDestroyed(int itemDatabaseId, bool destroyed);
    void OnPeriodicItemDestroyed(CItem* item, int itemDatabaseId);
    void OnGetPeriodicShapeShiftedItem(CItem* item, int itemPeriod);
    void OnLoadPeriodicShapeShiftedItemInfo(int itemCount, const unsigned char* packet);
    void OnRestorePeriodicShapeShiftedItem(CItem* item);
    void OnExpiredPeriodicShapeShiftedItem(CItem* item);
    void CheckPeriodicShapeShiftedItemOnTick();
    void SendPeriodicShapeShiftedItemList();
    bool ReplyShapeShiftItem(
        CItem* sourceItem,
        std::int64_t sourceAmount,
        CItem& targetItem,
        CItem* extraItem,
        int consumeExtraItem,
        int restoreOnly,
        int shapeShiftingClassId,
        std::int64_t adenaAmount,
        int itemPeriod
    );
    bool ReplyCancelEquipmentAddedEffectAndDropItem(
        FVector* position,
        CItem* beforeItem,
        CItem* afterItem,
        int beforeOwnerId,
        int afterOwnerId,
        std::int64_t beforeAmount,
        std::int64_t afterAmount,
        int effectMask,
        bool setContributeData
    );
    int GetPeriodLeft(CItem& item);
    int GetPeriodLeft(int itemDbId, int shapeShifted);
    bool HandlePremiumItemManipulation(std::int64_t warehouseNo, CItem* item, std::int64_t amount);
    void WithdrawPremiumItem(std::int64_t warehouseNo, std::int64_t amount);
    void ShowPremiumItemList();
    void ReleaseAllPremiumItemList();
    void AddSharedReuseDelayOfItem(int sharedReuseGroupId, int reuseEndTime);
    int GetReuseRemainTime(int sharedReuseGroupId);
    bool UseDelayShareItem(int sharedReuseGroupId, int reuseDelay, int* reuseEndTime);

    void SetIgnoreGradePenalty(CrystalType crystalType);
    void SetCrystalGradepBonus(double bonus);
    CrystalType CalcCrystalEquipPenalty();
    int GetPremiumLevel();
    bool IsPremiumUser();
    bool IsInPremiumGroup();
    void SetPremiumLevel(int premiumLevel);
    bool CheckPremiumType(PremiumUserType::Enum type) const;
    void SendRuneItemSkillList();
    int GetDropRateByRune(int runeType);
    int GetDropAmountByRune(int runeType);
    void AddDropRateByRune(int runeType, int amount);
    void AddDropAmountByRune(int runeType, int amount);
    CrystalType GetIgnoreGradePenalty();
    void InitMentorMentee();
    void LoadMentorMenteeInfo();
    void EndMentoring(int userDbId, int type);
    void SetMentorPenaltyEndTime(std::int32_t penaltyEnd);
    int GetMentorPenaltyEndTimeLeft();
    void AddMentee(User* mentee);
    void AddMentee(int menteeDatabaseId, char16_t* menteeName, int menteeClass, int menteeLevel);
    void ReplyAddMentee(User* mentee);
    void ReplyEndMentoring(int userDbId, int endType);
    void SetMentor(User* mentor);
    void ResetMentor();
    void DeleteMentee(int menteeDatabaseId);
    void SetMyMentor(int mentorDatabaseId, char16_t* mentorName, int mentorClass, int mentorLevel);
    void AcquireMentorMenteeSkills(EtcSkillAcquireType type);
    bool DeleteAcquireSkills(EtcSkillAcquireType type);
    void DeleteMentorMenteeSkills(EtcSkillAcquireType type);
    void SetMenteeLoginStatus(int menteeSId, int loginStatus);
    void ShowMenteeList();
    void SetMentorLoginStatus(int loginStatus);
    bool IsAlreadyHaveMentor();
    bool IsMyMentor(int mentorDbId);
    bool IsMyMentor(User* mentor);
    void ShowMentorInfo();
    void SetMentoringGraduate(bool graduate);
    int GetMenteeCount();
    bool IsHaveMentee();
    bool IsMyMentee(int menteeSId);
    bool IsMyMentee(User* mentee);
    bool CanBeMentor(int& reason);
    bool CanBeMentee(int& reason);
    int GetMenteeDBId(wchar_t* name);
    void UpdateMenteePlaytime(std::uint32_t playtime);
    void UpdateMentorPlaytime(std::uint32_t playtime);
    void SetMentorMenteeLogout();
    void UpdateMentorMentee();
    void CheckMentorMenteeTimer();
    void UpdateMentorByLevelUp(bool graduate, int originalLevel);
    void SendMentorList();
    UserPledgeMissionInfo* PledgeMissionFind(int missionId);
    bool IsBuilder();
    bool UpdateBuilder(int builderLevel);
    void LogAbnormalAction(AbnormalUserAction action, bool forceClose, int value1, int value2, int value3, int value4);
    bool IsLowLevelBuilder();
    int GetSocialityCount();
    int GetInkEnergy();
    void SetInkEnergy(int value);
    void UpdateInkEnergy(int value, bool setDirectly);
    const char16_t* GetOriginName() const;
    bool CanUseDice();
    void SetUseDice();
    void CameraMode(bool enabled);
    void CloseAllPledgeWindowPacket();
    void PlaySound(const wchar_t* soundName, int value1, int value2, int value3, int value4, int value5);
    void PlayVoice(
        const char16_t* voiceName, int value1, int value2, int value3, int value4, int value5, int value6, bool flag
    );
    void PlayMusic(const char16_t* musicName);
    void PlayScene(int sceneId);
    bool StartScenePlay(int sceneId, int argument, int npcSid);
    bool StopScenePlay(int sceneId);
    void ShowPreview(int itemCount, int itemIds[]);
    void SetFootEffect(FootEffectEnum effect, int duration);
    const unsigned char* SetSSQInfo(const unsigned char* packet);
    void SetPetitionSeq(wchar_t* petitionSequence);
    void SendPetitionVote(int* petitionIds, int petitionCount, wchar_t* message, int messageLength);
    SSQSeal::type GetSealSelectionNo() const;
    FVector GetShelterPos();
    std::int64_t GetSSQPoint() const;
    std::int64_t GetDepositedSSQItemCount(int part, int itemType);
    SSQPart::type GetSSQPart() const;
    bool IsMyManor(int manorId);
    bool IsHero();
    void ImpoundHeroWeapon();
    void ImpoundHeroEntryItem(bool olympiadEntry);
    SharedNoblessInfo* GetNoblessInfo();
    int GetPrivateStoreType();
    void SendPrivateStoreMessage(CUserSocket* socket, PrivateStoreMessageType messageType);
    bool IsShopOpen();
    bool IsShopManaging();
    bool CanOpenPrivateStore(PrivateStoreType privateStoreType);
    bool IsItemUsable(int itemClassId);
    bool ReleaseEconomy();
    CSPointer<CBuySell> GetBuySell();
    bool CanUseNpcMerchant(CSPointer<CBuySell> buySell);
    short GetItemBuyFromNpcList(std::int64_t listId, CPacketHelperEx<20480>& packet);
    short GetItemSellToNpcList(std::int64_t listId, double ratio, CPacketHelperEx<20480>& packet);
    int ShowBuySellList(char refresh);
    PrivateStoreMap& GetPrivateStoreMap(bool wholesale);
    PrivateStoreMap& GetPrivateStoreBuyMap();
    bool SetPrivateStoreList(const unsigned char* packet);
    void SetPrivateStoreBuyList(const unsigned char* packet);
    void SetPrivateStoreMsg(const unsigned char* packet);
    void SetPrivateStoreWholeMsg(const unsigned char* packet);
    void SetPrivateStoreBuyMsg(const unsigned char* packet);
    void PrivateStoreBuy(const unsigned char* packet, User* buyer);
    void PrivateStoreBuyBuy(const unsigned char* packet, User* buyer);
    int SendPrivateStoreManageList(bool wholesale, CUserSocket* socket);
    int SendPrivateStoreBuyManageList(CUserSocket* socket);
    bool _GetPrivateStoreManagerList(PrivateStoreMap& store, CPacketHelperEx<20480>& packet, int& itemCount);
    bool GetPrivateStoreItems(CPacketHelperEx<20480>& packet, int& itemCount, int itemClassId);
    bool _GetPrivateStoreItems(CPacketHelperEx<20480>& packet, int& itemCount, int itemClassId);
    void ShowMarketAll(int unused);
    void ShowMarketItem(int itemClassId, int unused);
    void PrivateStoreListReq(int requesterServerId, int privateStoreType);
    void PrivateStoreBuyListReq(int requesterServerId);
    void SendRecipeBookList(int recipeType);
    void DeleteRecipeBook(int recipeId);
    void InitRecipeBook();
    void AddRecipeBook(int recipeId, bool loading);
    void SendRecipeInfo(int recipeId, int result);
    CRecipeInfo* GetRecipeInfo(int recipeId);
    bool HaveRecipe(int recipeId);
    int GetDwarfCreateItemLevel() const { return m_nDwarfCreateItemLevel; }
    int GetCommonCreateItemLevel() const { return m_nCommonCreateItemLevel; }
    void SetRecipeStoreMsg(const unsigned char* packet);
    int SetRecipeStoreList(const unsigned char* packet);
    void CancelRecipeStore();
    void RecipeStoreBuy(const unsigned char* packet, User* requester);
    bool MultiSellChoose(
        int listId,
        int entryId,
        std::int64_t amount,
        int enchanted,
        ItemOptionKey optionKey,
        BaseAttribute attribute,
        std::map<int, EnsoulOptionData*> ensoulOptions[3]
    );
    void RecipeStoreShowInfo(User* requester, int recipeId, int result);
    void RecipeStoreListReq(int requesterServerId);
    void StartBuyProduct();
    void EndBuyProduct();
    bool IsBuyingProduct();
    BrBuyProductResultType CheckBuyProduct(CSocket* socket, int productId, int productAmount, bool gift);
    void RequestBuyProduct(
        int productId, int productAmount, bool gift, wchar_t* receiverName, wchar_t* title, wchar_t* content
    );
    void LoadPCCafePoint(int pointType);
    void SetDailyPCCafeInfo(int point, int checkLevel);
    int CheckDailyPCCafeBonus(int point);
    void CheckPCCafeEvent();
    std::int32_t GetPCCafeUseTime();
    void ResetPCCafeUseTime(bool reset);
    void AnnounceHomeMax();
    bool CheckDayPassed();
    void CheckXMasSantaEvent();
    void CheckYearCatchEvent();
    void RequestPCCafeCouponUse(const char16_t* coupon);
    void RepliedPCCafeCouponResult(char result, int couponPart1, int couponPart2, int couponPart3, int couponPart4);
    void GetItemByPCCafePoint(int point, int itemClassId, int enchanted, std::int64_t amount);
    int GetPoint(int pointType);
    int GetVitalPoint();
    void ResetRestoreCount(); // No current compiled body or caller. Donor declaration only.
    bool RestoreVitalPoint(int point, VitalityRestore::Type type, int option, int limit);
    bool DecVitalRestoreCount(int count, int option, int limit);
    void InitVitality(CVitalData& data);
    double GetVitalityItemDropBonus();
    int GetVitalityRestoreCount();
    int GetVitalityRemainRestoreCount();
    void SetVitalityRestoreCount(int restoreCount);
    bool CanUseVitalConsumeItem();
    bool CanChangeVitalItemCount();
    bool IncVitalItemConsumeCount(VitalityRestore::Type type, int count, std::int64_t amount);
    bool SuspendVitalityEffect(VitalityRestore::Type type);
    bool StopSuspendVitaliltyEffect();
    void SetVitalityRechargeBoost(const int rechargeBoost);
    void AddAddedVitalityBonusPer(const int bonusPercent);
    void SetVitalityConsumePointReducePer(CSPointer<User>& user, const int reducePercent);
    void InitVitalResetDate();
    bool CheckPointAddable(int pointType, int point);
    void AddRaidPointBonus(double bonus);
    void LoadPoint(int pointType);
    void ManipulatePVPPointWithItem(int point, int itemCount, int itemClassId, std::int64_t amount, int option);
    void SendItemIncreaseSystemMessage(int itemClassId, std::int64_t count, int enchant);
    void SendItemDecreaseSystemMessage(int itemClassId, std::int64_t count, int enchant, bool showAdenaMessage);
    void SendAdenaInvenCount();
    void InitializeBossRecord(bool loadFromDb);
    void SetBossRecordRound(int round);
    void AddBossRecord(CBossRecord* record);
    void SendCompassRegionCode(CompassRegionCodeEnum regionCode, bool enabled, bool reset);
    void ValidateCastleStateInfo();
    bool CheckRidingFoodItem(int striderClassId, int itemClassId);
    int GetStriderClassID();
    std::int16_t CheckUseable(CItem* item);
    void OnRequestVipAttendanceReward(char result);
    int OnRequestVipAttendanceCheck();
    void RequestSendVipAttendanceResult();
    void RequestAccountStamp(int stampType, bool checkCanSend, bool force);
    void TurnOnAutoSoulshot(int itemClassId, int activeState, int uiSlot);
    void TurnOffAutoSoulshot(int itemClassId, int activeState, int uiSlot, bool clearItemClassId);
    void TurnOffSummonSoulShot();
    void AddVoteCount(int count);
    void SetVoteCount(int count);
    void SetBonusTimeLimit(int value) { runtime::raw_atomic::ExchangeI32(&m_nBonusTimeLimit, value); }
    int GetVoteCount() const { return m_nVoteCount; }
    int GetBonusTimeLimit() const { return m_nBonusTimeLimit; }
    int GetBonusCount() const { return m_nBonusCount; }
    int GetPlayTime() const { return m_nPlayTime; }
    int GetPlayTimeCount() const { return m_nPlayTimeCount; }
    _SYSTEMTIME* GetLastInitTime() { return &m_tmLastInitTime; }
    void CalcVoteSystemTime();
    void SaveVoteSystem();
    void SendVoteSystemInfoPacket();
    int CalInitialVoteSystem();
    void InitVoteParam(int elapsedDays);
    void CheckVoteSystemTime();
    void CheckInitVoteSystem();
    bool VoteCount(int targetServerId);
    bool OnRequestVoteSystem(int targetServerId);
    void SetBonusCount(int count);
    void CalExpBonus();
    void AddExpBonusPercent(double bonusPercent);
    void AddSPBonusPercent(double bonusPercent);
    void CheckVoteSystemGetExp();
    void InitCoupleParam();
    void IsCoupleUserRotation();
    void SetAssociatedInstantZoneId(std::uint32_t, int, int, bool);
    FVector GetInstantZoneReturnPoint(const FVector& origin);
    void EnterInZoneLock();
    void LeaveInZoneLock();
    void OnCoupleActionPacket(int, int);
    void SetLastKillTime(int lastKillNpcSId, std::uint32_t lastKillTime);
    bool CheckItemRestrictInEchant(const int);
    void SendLevelUpNPCEvent();
    void SendStorageMaxCount();
    void SendRemainTime(int remainTime, eSMsgType messageType, int value1, int value2, int value3, int value4);
    void GetAndSendSkillRemainTime(CCreature& creature, int skillId, int level, int subLevel);
    void InitAllEnchantBonusOptions();
    void SaveGet5YearGiftTime();
    void SetDailyQuest(int questId, bool complete);
    static DefaultIOBuffer* MakeConstBuffer(const char* format, va_list arguments);
    static int BroadcastWithConstBuffer(const std::set<unsigned int>& userIds, const char* format, ...);
    bool CanGetBirthdayGift(_SYSTEMTIME* currentTime);
    void SetGetbirthdayGiftDate(int year, int month, int day);
    void SaveGetBirthdayGiftTime();
    void ReportCreateDate();
    void CelebrateBirthday(int daysSinceBirthday, bool leapDayAdjusted);
    void CheckBotReportEvent(int eventId);
    void CheckBotReportEventCaptcha(int eventId);
    void StartCaptchaConfirm(std::int64_t transactionId, CAPTCHA::JOB_TYPE jobType);
    void EndCaptchaConfirm();
    void ActivateCaptchaDebuff();
    void SetCaptchaConfirmSec(int seconds);
    int GetCaptchaConfirmSec();
    void CheckCaptchaConfirmExpired();
    void ConfirmCaptchaAnswerSuccess();
    void ConfirmCaptchaAnswerFail();
    void SaveCaptchaConfirm();
    int GetCaptchaDelaySec();
    void SetCaptchaDelaySec(int delaySeconds);
    int SetChatLimitValue(int value);
    void AbandonAirShipDriver();
    bool IsAirShipDriver();
    void SendUserConnectedEvent();
    static bool CheckCharacterName(const char16_t* name, int code, bool extended);
    static bool CheckCharacterNameDual(const char16_t* name, int code, bool extended);
    static void AddNewCharID(int characterId);
    static bool IsNewChar(int characterId);
    void SetAvoidAgroFalse();
    int GetMemoCount();
    bool IsFullMemo();
    bool HaveMemo(int questId);
    void LoadQuestProgressFromMemo();
    void RetrieveAcceptedQuestIDs(std::vector<int>& questIds);
    bool AcceptQuest(int questId);
    bool AddQuestProgress(int questId, int progress);
    bool IsQuestProgressCompleted(int questId);
    bool CompleteQuest(int questId);
    bool DeleteQuest(int questId);
    bool StartJournal(int questId, int journalId);
    bool _StartJournal(int questId, int journalId);
    bool CompleteJournal(int questId, int journalId);
    bool _CompleteJournal(int questId, int journalId);
    bool UpdateProgress(int questId, int progress);
    void SendPremiumItemNotification();
    void ResetPrivateStoreTick();
    bool SetTacticalSign(int signId);
    bool ChangeTargetToTacticalSignedCreature(int signId);
    int GetTacticalSignId(CCreature& creature);
    void StartGoodsInventoryJob();
    void EndGoodsInventoryJob();
    void RequestInquiryGoodsInventory(bool premium);
    void RequestInquiryExistGoods();
    void RequestRetrieveGoodsInventory(std::int64_t goodsUid);
    void DeleteNewGoodsInfo(std::int64_t goodsUid);
    void RetrieveGoods(std::int64_t goodsUid);
    void AddNewGoodsInfo(CUserNewGoods goods);
    void SetKilledTimeByUserAsChao(std::int32_t killedTime);
    void SaveGiveKillerKarmaPoint();
    void ResetDismissPenalty();
    void ResetWithdrawPenalty();
    void ResetOustedPenalty();
    int GetLastDismissProposeTime();
    void TakeDismissPenalty();
    void TakeWithdrawPenalty();
    void TakeOustedPenalty();
    std::uint32_t GetPledgeOutTime();
    bool CanJoinPledge();
    bool CanCreatePledge();
    bool CanDismissPledge();
    void KalieTimerExpired();
    bool IsTokenProcressing();
    void EndTokenProcress();
    void LoadExDropCount();
    ClassCategory GetClassCategory();
    void SetRaidUserName(const char16_t* name);
    void SetPreInitData();
    void AcquireAutoGetSkills(int type);
    void AcquirePledgeSkillsAfterReload(bool online);
    void AcquireSubjobCertSkills();
    void BeforeAcquireSubjobSkills(bool initializeShortcuts);
    void AcquireSubjobSkills();
    void ValidateSubJobParameters();
    void InitDualClass(int dualClassId, int classId);
    void InitSubJob(int subJobId, int classId);
    void InitDualJob(int dualJobId, int classId);
    void OnChangeSubJobEnd(SubJob::Event event, CVitalData vitalData);
    void MentorSubjobChange();
    bool CheckSystemBuff(SYSTEM_BUFF_TYPE type);
    void AddSpoilBonusPer(int bonus);
    double GetSpoilBonusPer();
    void _OnRuneItemsChange();
    void OnRuneItemsChange();
    void SetFieldCycleID(int fieldCycleId);
    int GetFieldCycleID() const;
    void SetFieldCycleQuest(int questId, int fieldCycleId);
    int GetFieldCycleIDOfQuest() const;
    int GetFieldCycleQuestID() const;
    void SetNCGuardBotDetected(bool detected) noexcept { m_bIsNCGuardBotDetected = detected; }
    bool IsNCGuardBotDetected() const noexcept { return m_bIsNCGuardBotDetected; }
    void SetSkillAll(int classType);
    void ResetAllEnchantSkill(CUserSocket* socket);
    void SendAcquiredSkillToDB(int skillId, int skillLevel, int skillSubLevel, bool saveToDatabase);
    void AcquireAlchemySkill(AcquiredAlchemySkillInfo& skill, int type);
    void DeleteAlchemySkill(int skillId, int type);
    bool PacketAcceptable(int packetNo);
    bool PacketExAcceptable(int packetNo);
    bool CheckForSkillAcquireList(EtcSkillAcquireType type, const SkillAcquireInfo& acquireInfo);
    bool IsLearnableSkill(const SkillAcquireInfo& acquireInfo, bool sendMessage);
    bool IsLearnableAbilitySkill(int skillId, int skillLevel, int type, int acquiredCount);
    void SendSkillAcquireList(EtcSkillAcquireType type, bool sendNoSkillMessage);
    bool SendNewSkillByLevelUp();
    void SendTotalSkillEnchantCountToNpcServer(int requestId);
    void SendDeletedSkillToDB(int skillId);
    void SendDeletedPledgeSkillToDB(int skillId);
    void SendStartLuckyGame(int type);
    void SendSuccessLuckyGame(int type, int result);
    void OnShowCardCollectSystem();
    void RewardTraining();
    void StartTraining();
    void EndTraining(TrainingZone::EndReason reason, int endTime);
    bool IsTraining() { return m_data->m_TrainingState == 1; }
    bool BaseAttributeCancel(CItem* item, AttributeTypeEnum attribute);
    void GiveEventItem(
        int deleteItemClassId,
        std::int64_t deleteItemAmount,
        int addItemClassId,
        std::int64_t addItemAmount,
        int addItemEnchant,
        int timeGap,
        int eventType,
        int eventVariable
    );
    void GiveItemOnAccount(
        int condition,
        int deleteItemClassId,
        std::int64_t deleteItemAmount,
        int addItemClassId,
        std::int64_t addItemAmount,
        int addItemEnchant,
        int type,
        wchar_t* eventName,
        int npcId,
        int eventStartTime
    );
    void GiveItemsOnAccount(
        int condition,
        int deleteItemClassId,
        std::int64_t deleteItemAmount,
        int addItemCount,
        int* addItemClassIds,
        std::int64_t* addItemAmounts,
        int* addItemEnchants,
        int type
    );
    void RequestAccountStampBuilder(int value1, int value2);
    void RequestDeleteAccountStampLogBuilder(int stampId);
    void SendSendableCharacterList();
    void LoadWorldChat();
    void BroadcastToAllowedUser(const char* format, ...);
    void SetUsedWorldChatPoint(int usedPoint) { runtime::raw_atomic::ExchangeI32(&m_nUsedWorldChatPoint, usedPoint); }
    int GetUsedWorldChatPoint() const { return m_nUsedWorldChatPoint; }
    int GetWorldChatCount();
    int IncUsedWorldChatPoint();
    int GetSelectedQuestZoneID() const { return m_nNowSelectedQuestZone; }
    void SetSelectedQuestZoneID(int zoneId) { m_nNowSelectedQuestZone = zoneId; }
    int GetRelationTo(User* user);
    bool CheckDeclareWarBoth(User* user);
    bool IsPledgeMaster();
    bool IsNoSiegeRelation();
    bool TeleportToLocationWithItem(FVector& position, int direction, int itemClassId, std::int64_t amount);
    bool ItemBonusByLuckyStat(std::int64_t experience);
    bool IsErtheia();
    static bool IsInThirdClassGroup(ClassType classType);
    static bool IsInFourthClassGroup(ClassType classType);
    static bool IsInFifthClassGroup(ClassType classType);
    static bool IsInSixthClassGroup(ClassType classType);
    bool IsThirdClassChangedOrMore() const {
        // Current L2Server 0x00a6d5e8, original inline User.h owner.
        return IsInFourthClassGroup(m_data->m_nClass) || IsInFifthClassGroup(m_data->m_nClass) ||
            IsInSixthClassGroup(m_data->m_nClass);
    }
    static bool IsFirstOrSecondClassGroup(ClassType classType);
    static bool CanJoinAcademyClass(ClassType classType);
    static ClassType* GetBaseJobClass(int classType);
    static bool GetJobClassLevel(int classType, int* level);
    int GetSubJobIdByClass(int classType) const {
        for (int subJobId = 0; subJobId < 4; ++subJobId) {
            int subJobClass = -1;
            switch (subJobId) {
                case 0:
                    subJobClass = m_data->m_nSubJob0Class;
                    break;
                case 1:
                    subJobClass = m_data->m_nSubJob1Class;
                    break;
                case 2:
                    subJobClass = m_data->m_nSubJob2Class;
                    break;
                case 3:
                    subJobClass = m_data->m_nSubJob3Class;
                    break;
                default:
                    break;
            }

            if (subJobClass == classType) {
                return subJobId;
            }
            if (IsInFourthClassGroup(static_cast<ClassType>(subJobClass))) {
                ClassType* const baseJobs = GetBaseJobClass(subJobClass);
                if (baseJobs != nullptr) {
                    for (int index = 0; baseJobs[index] != CT_NONE; ++index) {
                        if (baseJobs[index] == classType) {
                            return subJobId;
                        }
                    }
                }
            }
        }
        return -1;
    }
    void GetBaseJobClass(std::vector<ClassType>& classes);
    static int GetClassGroupNoByClassType(ClassType classType);
    static bool HasToGraduateAcademy(ClassType currentClass, ClassType nextClass);
    void SetAcquireSkillListSendClass(EtcSkillAcquireType type);
    bool IsMatchAcquireSkillListSendClass(EtcSkillAcquireType type);
    bool SetHonorsSkillList(int skillId, int level, bool fromDatabase);
    bool DeleteHonorsSkill(int skillId);
    double ValidateBaseHPAndGetLimitlessValue();
    void ReleaseSocket();
    CSPointer<CFriends> GetFriends();
    bool OnAddFriendRequest(const char16_t* targetName, bool weddingProposal);
    bool OnAddFriendReply(int result, bool weddingProposal);
    void SayToFriend(CSPointer<User>& target, const char16_t* message);
    void AddWedding(const char16_t* targetName);
    void ReplyAddFriendResult(User* requestee);
    void BreakFriendship(int characterId);
    void OnRequestL2FriendList();
    void SetNeedUpdateFriendsInfo(FriendsEnum::Enum type, int worldId, unsigned int characterId);
    void UpdateFriendsInfo();
    void InformClassChangedInfoToFriends();
    void InformLevelChangedInfoToFriends();
    void SendLogonMsgToFriend();
    void SendLogoffMsgToFriend();
    void OnRequestUpdateFriendMemo(const char16_t* characterName, const char16_t* memo);
    void SendFriendDetailInfo(int characterId, bool requested);
    void OnReplyUpdateFriendMemo(int characterId, const char16_t* memo);
    void UpdateBlockMemo(int characterId, const char16_t* memo);
    int GetBlockCharList(CPacketHelperEx<20480>& packet);
    void BuildBlockListPacket(CPacketHelperEx<20480>& packet);
    void SendBlockList();
    std::u16string GetBlockCharInfo(int characterId, bool getCharacterName);
    int GetBlockCharDBID(const char16_t* characterName);
    void OnRequestUpdateBlockMemo(const char16_t* characterName, const char16_t* memo);
    void OnRequestBlock(int requestType, const unsigned char* packet);
    void AddBlockList(int characterId, const char16_t* characterName, const char16_t* memo);
    void DeleteBlockList(int characterId);
    bool IsInBlockList(int characterId);
    bool IsInBlockList(const char16_t* characterName);
    bool IsInBlockList(User& user);
    bool IsBlockAll() const { return m_bBlockAll; }
    void OnReplyLoadUserMonsterBook(const unsigned char* packet);
    void RequestOpenMonsterBook();
    void RequestCloseMonsterBook();
    void IncrementMonsterBookKillCount(CCreature* creature);
    void MergeMonsterBook();
    void UpdateMonsterBookToDB();
    void SendMonsterBook();
    void SendMonsterBookNoti();
    bool CheckMonsterBookRewardJob();
    void TryGetMonsterBookReward(int cardId);
    void UpdateMonsterCardForBuilder(int cardId, int level, int killCount);
    void OnMonsterBookRewardFinish(int cardId, int factionIndex);
    bool CheckMonsterBookFactionTrophyQuest(int factionId, int level, int value);
    int ValidateFactionLevel(int factionId);
    void UserStatusLog();
    void PickCardOnCardUpDownGame(int card);
    bool CheckCardGameItemJob();
    void TryGetCardUpdownGameReward();
    void PrepareCardUpDownGame(int npcSid, int gameId, bool builder);
    void StartCardUpDownGame(int gameId, bool builder);
    void ShowCardUpdownGame(int npcSid, int gameId);
    bool CheckCombinationItemJob();
    bool CheckArtifactEnchantJob();
    bool OnEnchantTry();
    bool TryEnchantArtifact(const std::uint8_t* packet);
    bool OnEnchantOne(int itemServerId);
    bool OnEnchantOneRemove(int itemServerId);
    bool OnEnchantTwo(int itemServerId);
    bool OnEnchantTwoRemove(int itemServerId);
    bool OnEnchantRetryToPutItems(protocol::C_EX_ENCHANT_RETRY_TO_PUT_ITEMS_INFO& packet);
    int CheckUpgradeSystemJob();
    void RequestUpgradeSystem(protocol::C_EX_UPGRADE_SYSTEM_REQUEST& packet);
    bool SendFactionInfoList(int requestType);
    void QuitCardUpDownGame();
    bool OnSwapAgathionSlotItemsPacket(protocol::C_EX_SWAP_AGATHION_SLOT_ITEMS_INFO& packet);
    void WriteLogSwapAgathionSlotItems(CItem* item, SlotType slot);
    bool HasEquipped(CItem* item);
    void ForceEquipItem(CItem* item);
    void CalcChaoDropRate(double* itemDropRate, double* adenaDropRate, bool applyQuarterPenalty);
    bool CheckDropItemCondition();
    int DecideDropItem(int* itemIndices, double dropRateBonus, bool applyQuarterPenalty);
    void DropAndDestroyItem(CItem& item, bool chao, bool equipped, int* itemDropRate, int* adenaDropRate, bool* destroyed);
    void OnDieDropItem(bool applyQuarterPenalty, double dropRateBonus);
    int OnDiePenalty(bool unknown, double unknownValue);
    std::int64_t CalcShapeShiftingFee(CItem* shapeShiftingItem, CItem* targetItem) const;
    bool IsSkillAcquireableItem(CItem& item, CheckCrystalType checkCrystalType);
    bool AcquireSkillByItem(const CSkillInfo* skillInfo);
    void GetSkillsFromItem(CItem& item, std::map<int, const CSkillInfo*>& skills, CheckCrystalType checkCrystalType);
    void ValidateEquippedAgathionSubSlotItemSkills(
        CItem& item,
        std::map<int, const CSkillInfo*>& withinSkills,
        std::map<int, const CSkillInfo*>& beyondSkills,
        std::map<int, const CSkillInfo*>& optionSkills
    );
    void GetOptionSkillsFromItem(CItem& item, std::map<int, const CSkillInfo*>& skills, CheckCrystalType checkCrystalType);
    void DeleteEnchantOptionSkills(CItem& item);
    int GetEquipedFishingRodId();
    int GetEquipedLureId();
    int GetPendingUseETCItemID(PendingUseItemType type);
    void SetPendingUseETCItem(CItem* item, PendingUseItemType type);
    bool ReplyEnchantItem(
        CItem& scrollItem,
        std::int64_t scrollAmount,
        CItem* supportItem,
        std::int64_t supportAmount,
        CItem& targetItem,
        int enchant,
        bool lucky
    );
    void ReplyEnchantItemFail(
        CItem& scrollItem,
        std::int64_t scrollAmount,
        CItem* supportItem,
        std::int64_t supportAmount,
        CItem& targetItem,
        int crystalItemDatabaseId,
        std::int64_t crystalAmount
    );
    void BroadcastItemGet2Party(int messageId, int itemClassId, std::int64_t amount, int enchant, bool flag);
    bool ReplyCancelVariationItem(CItem* targetItem, std::int64_t adenaAmount);
    bool ReplyVariationItem(
        CItem* targetItem,
        CItem* mineralItem,
        CItem* supportItem,
        std::int64_t supportItemAmount,
        ItemOptionKey optionKey,
        std::int64_t mineralItemAmount
    );
    bool ReplyChangeAttributeItem(CItem& sourceItem, std::int64_t amount, CItem& targetItem);
    void SendItemList(bool first, bool second, CUserSocket* socket);
    void RequestPackageSend(const unsigned char* packet);
    void SendPackageSendableList(int requestValue);
    void InitPackageItemPackets();
    void PushPackageItemPacket(CPacketHelperEx<20480>& packet);
    void SendPackageItemPackets();
    CItem* GetPendingUseETCItem(PendingUseItemType type);
    bool CheckAndSetPendingUseETCItem(CItem* item, PendingUseItemType type);
    bool CheckAndResetPendingUseETCItem(PendingUseItemType type);
    void _ItemRefundFrontClear();
    bool _ItemRefundPush(CItem* item, std::int64_t itemAmount, std::int64_t totalPrice);
    void ItemRefundClear();
    short ItemRefundShow(CPacketHelperEx<20480>& packet);
    bool ItemRefundGet(const std::vector<int>& indices, CSPointer<CBuySell> buySell);
    void SendMagicSkill(
        const int& parameter1,
        const CWorldObject* target,
        const SkillUseInfo& skill,
        const int& parameter2,
        const int& parameter3,
        const int& parameter4,
        const int& parameter5,
        const int& parameter6,
        char* buffer
    );
    void SendSystemMessageEquipItem(CItem* item);
    void SendSystemMessageUnEquipItem(CItem* item, SlotType slot);
    void PledgeMissionInit();
    void PledgeMissionLoadingCompleted();
    bool PledgeMissionCheckAndAddCount(PledgeMission::GoalType goalType, int firstValue, int secondValue, int count);
    bool PledgeMissionCheckAndAddCountInRaid(PledgeMission::GoalType goalType, int firstValue, int secondValue, int count);
    bool PledgeMissionCheckConditionAndAddCount(
        int missionId, UserPledgeMissionInfo& missionInfo, int firstValue, int secondValue, int count
    );
    void PledgeMissionReward(int missionId);
    void PledgeMissionSuccessReward(int missionId, std::int32_t rewardTime);
    void PledgeMissionSendRewardCountOfOneWeekToUserWithType(int userType);
    UserPledgeMission& GetUserPledgeMission() noexcept { return m_UserPledgeMission; }
    void PledgeMissionSendToDBIfChanged();
    void PledgeMissionSendToUserIfChanged();
    void PledgeMissionIntervalCheck();
    void ResetCheckEquipedFlag();
    void CheckDieTickOnTimerExpired();
    void TimerExpiredAgathion();
    void CheckCompassRegionOnTimerExpired();
    int GetBBSLocation();
    void SetBBSLocation();
    void DecreaseKarma(bool byUser, User* killer);
    void DecreaseKarma_KillSummon();
    void IncreaseDuelCount();
    void IncreaseKarmaByKillingChao();
    void LogWhenPCDie(CCreature* killer, FVector& position);
    int CalcKarma(int newPkCount, int pkPardon);
    void LevelDownSkillWhenLevelIsTooHigh();
    void CheckCompassRegion(); // Current L2Server 0x00b1c73c.
    void ResetAreaFlag();
    UserInzoneBindingData _GetInZoneRestrictionByTimeSlice(const UserInzoneBindingMap& bindings, int startTime, int endTime);
    UserInzoneBindingData GetInZoneRestrictionByTimeSlice(int startTime, int endTime);
    void ResetInzoneBinding(int enabled, int startTime, int endTime, int timeGap, int eventType, int eventVariable);
    void UpdateInZoneRestrictionTime(int inzoneTypeId, int latestBindingTime, bool groupRestriction, int inzoneDatabaseId);
    void _ResetInZoneRestriction(int inzoneTypeId, bool groupRestriction);
    bool _GetInZoneRestriction(
        int inzoneTypeId, bool groupRestriction, int& latestBindingTime, int& entranceCount, int& maxEntranceCount
    );
    bool CanAddInZoneMaxEntrance(int inzoneTypeId, int count);
    bool AddInZoneMaxEntrance(int inzoneTypeId, int count);
    bool ResetInZoneExtraEntrance(int inzoneTypeId);
    bool ResetAllInZoneExtraEntrance();
    void ClearInZoneRestriction();
    void MarkInZoneRestriction(
        int inzoneTypeId, int latestBindingTime, bool groupRestriction, int maxEntranceCount, int inzoneDatabaseId
    );
    void SetInZoneRestriction(int count, const unsigned char* packet);
    void ResetInZoneRestriction(int inzoneTypeId, bool groupRestriction);
    void ResetAllInZoneRestriction();
    bool GetInZoneRestriction(
        int inzoneTypeId, bool groupRestriction, int& latestBindingTime, int& entranceCount, int& maxEntranceCount
    );
    void GetAllInZoneRestriction(InZoneRestrictionMap& restrictions, GroupInZoneRestrictionMap* groupRestrictions);
    void SetInzonePoint(int inzoneTypeId, int point, int timestamp);
    void AddInzonePoint(int inzoneTypeId, int point);
    void SaveInzonePointToDB(int inzoneTypeId);
    UserInzonePoint GetInzonePointInfo(int inzoneTypeId);
    void SendInvenCarryingWeight(CUserSocket* socket);
    bool TradeDivideAdena(User* user, int objectId, std::int64_t amount, bool update);
    bool SetStopBotReport(std::uint32_t duration, bool save);
    void SendCompletedDailyQuest(int questId);
    void SendCompletedDailyQuestList();
    void SendBuySellFail(BuySellConst::BuySellFailType type);
    void SendUIEventPacket(const unsigned char* packet);
    void SendUSMEventPacket(int value);
    void SendVoiceEventPacket(const unsigned char* packet);
    void SendInvitationEventUISetting(int value);
    void SendInkEnergyInvitationEventPacket(int value);
    double GetDefaultExpDownValue(int level);
    void QuitPrivateStore(bool force);
    void QuitRecipeStore();
    void EquipHenna(int hennaId);
    void CheckPeriodicHennaOnTick();
    void WriteNoblessAchievement(NoblessWinType winType, int value);
    void SetPoint(int pointType, int value);
    int GetRealAddablePoint(int point, int pointType);
    bool CanGetPVPPoint();
    void SendDormantGift(int dormantType);
    void CheckDormantAndGiveGift();
    void GivePremiumItem();
    void CheckDormantUser(bool force);
    void ActivateDormantEventBuff(int oldDormantType, int newDormantType);
    bool UpdatePoint(int point, UserPointType pointType, DoJobWithPoint operation, int line);
    bool CheckHomeCountAddable();
    void SetHomePCCafeCount(int count, int savedDate);
    bool IsPCCafeEventOn();
    bool CanUsePCCafePoint();
    bool CanGetPCCafePoint();
    void SendPCCafePointInfo(int point, int messageType, bool loaded, bool clear, int messageId);
    int HandlePointItemManipulation(
        int point, int pointType, int operation, int itemCount, ItemManip* manipulations, AtomicJob* atomicJob, int line
    );
    void SetKillPVPPointChecker(int giveCount, int lastGiveTime) {
        KillPVPPointChecker.SetGiveCount(giveCount, lastGiveTime, this);
    }
    void SavePoint(int pointType, int currentPoint, int delta);
    void AddBonusCount(int count);
    void SetVoteSystem(
        int bonusTimeLimit, int playTime, int voteCount, int bonusCount, int playTimeCount, _SYSTEMTIME* lastInitTime
    );
    CoupleActionErrorType CheckCoupleActionStartCondition(User* user);
    DuelStartErrorType CheckDuelStartCondition(bool partyDuel);
    bool CheckDuelCancelCondition(int& reason, bool partyDuel);
    bool CheckAndSendPledgeEmblem(std::uint32_t emblemId, std::uint32_t pledgeServerId, std::uint32_t worldId);
    void SetOriginalBaseStat();
    void ValidateAbilitySkills();
    void SendGIPCharacterInfo(bool force);
    bool SetNicknameAndColor(int selectedColorIndex, const char16_t* nickname, int itemClassId);
    void ResetNicknameAndColor();
    bool BotReport(User* user);
    void OnPolymorph(CNPC* npc);
    void ShowTraceEmiter(std::int16_t emitterId, const std::vector<FVector>& points, int duration);
    bool ChangeUserMapDensity(const FVector& position);
    void DeleteBeautyItem(std::int64_t itemId, std::int64_t count);
    void DeleteCommissionItem(int itemId, std::int64_t count);
    void DeleteCommissionFee(std::int64_t fee);
    void InsertCommissionItem(const unsigned char*& packet, int& itemLane, int& itemClassId);
    void GetCommissionHistory(
        CItem* item,
        int& itemClassId,
        std::int64_t& itemPrice,
        std::int64_t& itemAmount,
        CommissionExpired::Enum& expiredType,
        CommissionDiscountInfo::Enum& discountType
    );
    void RegisterCommissionHistory(
        int itemClassId,
        std::int64_t itemPrice,
        std::int64_t itemAmount,
        CommissionExpired::Enum expiredType,
        CommissionDiscountInfo::Enum discountType
    );
    bool CanCallToChangeClass();
    bool CanGiveKillerKarmaPoint(User* killer);
    bool IsEinhasadeTransformed() const { return m_bEinhasadeTransformed; }
    void ReadyToFlyMove();
    void ProcessFlyMove();
    void StartFlyMove();
    bool CanStartFlyMove();
    bool CheckWeight(int classId);
    bool CheckSubjobCommon(int classId, bool force);
    bool CheckSubjobCommon(int npcSid, int classId);
    bool CheckDualClassjobCommon(int npcSid, int dualClassId);
    bool CanChangeSubClass(int classId);
    void SetChatName(const char16_t* chatName);
    bool CheckAbilityCond();
    bool OnRequestResetAbilitySkillPacket(const unsigned char* packet);
    void SendPathToAwakeningLevelNotice();
    bool EventByLuckyStat(bool premium);
    void LogNPBuffStatus();
    void OnResponseLockedItem(const unsigned char* packet);
    void OnResponseUpgradeSystem(const unsigned char* packet);
    bool AgathionSwapItemForBuilder(int sourceSlot, int destinationSlot);
    bool PledgeMissionCheckAndAddCount(PledgeMission::GoalType goalType, int value, int count);
    bool PledgeMissionCheckAndAddCountInRaid(PledgeMission::GoalType goalType, int value, int count);
    bool SetInteractRejectType(int type, int value);
    void VitalityConsume(CCreature* creature, int experience, double bonus);
    double GetVitalityExpBonus();
    void SendLogout();
    void SendAbnormalVisualEffectInfo(CUserSocket* socket);
    bool SetSnoop(int snoopGmId);
    int ComputeChattingRelation(User* talker, User* listener);
    HearResult Hear(User* talker, const char16_t* talkerName, const char16_t* message, int type);
    HearResult Hear(int serverId, int characterDatabaseId, const char16_t* characterName, const char16_t* message, int type);
    HearResult HearMobile(
        int serverId, int characterDatabaseId, const char16_t* characterName, const char16_t* message, SayPacketType type
    );
    bool CanSuspendConsumingVitalPoint();
    bool SuspendConsumingVitalPoint(VitalityRestore::Type type);
    bool StopSuspendConsumingVitalPoint();
    void AddPoint(int pointType, int point, bool save);
    bool OnCoupleActionAskStart(int actionId, User* user);
    void GetSavingPos(int& x, int& y, int& z);
    bool Polymorph(int npcClassId);
    bool CanChangeToAwakenClass(int classId);
    void SendVitalityEffectInfo(CUserSocket* socket);
    bool ManipulateItemAndDoAtomicJob(
        ItemManipList& manipulationList, AtomicJob* atomicJob, ItemGetReason::Enum reason, bool premium
    );
    bool OnRequestAbilitySkillPacket(const unsigned char* packet);
    void ValidateAP();
    void OnRequestAutoFishing(bool start);
    CTrade* GetTrade();
    void InitTrade();
    bool TradeStart(bool synchronize);
    bool TradeItemAdd(const unsigned char* packet);
    void TradeCancel(bool notify);
    void TradeDone();
    void TradeEnd();
    bool IsNowTrade(); // Current L2Server 0x00b2f8c4.
    void CancelPrivateStore(int privateStoreType);
    void UnequipHenna(int hennaId);
    void AcquireHennaSkill(int hennaId);
    void DeleteAcquireHennaSkill(int hennaId);
    void AcquireUnEquipHennaSkill(int hennaId);
    void CheckPremiumHennaConditionOnTick();
    double GetAdjustedRaidPoint(int point);
    int AddRaidPoint(int point);
    void AddBossPoint(int bossId, int point);
    void ValidateHeroSkills();
    bool CanUseItem(int itemClassId);
    bool RequestLockedItem(protocol::C_EX_REQUEST_LOCKED_ITEM& packet);
    bool RequestUnLockedItem(protocol::C_EX_REQUEST_UNLOCKED_ITEM& packet);
    bool ChangeAttributeItem(CItem* scrollItem, CItem* targetItem);
    bool EnchantAttrItem(CItem* scrollItem, CItem* targetItem);
    bool EnchantItem(CItem* scrollItem, CItem* targetItem);
    bool ShapeShiftItem(CItem* scrollItem, CItem* targetItem);
    bool CheckSwapEquippedAgathionItemJob(CItem* sourceItem, CItem* targetItem);
    bool hLockedItem(CItem* scrollItem, CItem* targetItem);
    bool hUnLockedItem(CItem* scrollItem, CItem* targetItem);
    CObject* SetSkillTarget(CObject* target);
    bool CheckCrystalItem(CItem* item, int itemType);
    void AccmulateCharInfoStat(bool reset, int value);
    bool SendAlchemySkillList();
    bool CheckAlchemyJob();
    bool TryRollyCube(std::list<protocol::ItemServerInfo>& itemInfoList);
    bool TryAlchemyConversion(int skillId, int skillLevel, std::list<protocol::ItemInfo>& materialList, int tryCount);
    bool CheckEnsoulJob();
    bool OnEnsoulTry(int targetItemSid, std::vector<EnsoulTryData>& ensoulTryData);
    bool OnEnsoulExtractionTry(int targetItemSid, EnsoulItem::EnsoulItemSlotType slotType, int slotIndex);
    bool AllowInteractWith(User* user, ClientINI::InteractType type);
    void SendEquipSlotInfo(CUserSocket* socket);
    void SendCubicInfo(CUserSocket* socket);
    bool RequestTrade(User* user);
    bool IsValidCursedWeapon();
    void SendQuestList(CUserSocket* socket);
    bool UpdateQuestState();
    bool CheckPrivateStoreInventory();
    void SendInvenHennaList();
    void ValidateHenna();
    void OnPremiumHennaExpired();
    void AddBossRelatedBonus(CCreature& creature, int point);
    bool CanUseItem(CItem* item);
    bool Transform(int transformId, bool broadcast);
    bool CancelTransform(int targetTransformId, bool force, SkillPumpEndType pumpEndType);
    bool _OnTransform(bool broadcast);
    void _OnCancelTransform();
    void PumpStartTransformSkill();
    void PumpEndTransformSkill();
    void SendBasicActionList();
    bool RequestPrivateStore();
    bool RequestPrivateStoreWhole();
    bool RequestPrivateStoreBuy();
    bool RequestRecipeShopManageList(int shopType);
    void SendRecipeStoreManageList();
    bool CanRide(CPet* pet, bool sendMessage);
    bool CanRideForEvent(int npcClassId);
    bool Ride(CPet* pet);
    void RidePet(CPet* pet);
    bool RideWyvern(CPet* pet);
    bool RideWyvern(int npcClassId);
    bool TryRiding();
    bool Dismount(bool force, bool broadcast);
    bool CanDismountOnAir(bool force, bool& restart);
    bool CanDismountOnGround(bool force, bool& restart);
    void DeleteYongmaAbnormalStatus(bool unknown);
    void DismountFromPet();
    bool TryDismounting(bool force, bool broadcast);
    bool CheckRidingOnTimer();
    bool CheckWyvernOnTimer();
    void SetReturnValue(int skillId, int expireTime);
    void CheckAndDeleteReturnValue();
    FVector FindReturnValue(int skillId);
    FVector GetRestartPointInSiegeField(CResidence* residence);
    bool SendDiePacket(char save);
    bool Restart(RestartPoint restartPoint, int unknown);
    using CCreature::ValidateLocation;
    void ValidateLocation(const FVector& position, char validationType);
    void ValidateLocationInVehicle(const FVector& position, int heading, int vehicleId);
    void ValidateLocationInAirShip(const FVector& position, int heading, int airShipId);
    void ValidateLocationInShuttle(const FVector& position, int heading, int shuttleId);
    RestartPoint GetRestartPoint() const noexcept { return m_RestartPoint; }
    void DespawnPet(bool first, bool second, JobOnDespawnPet job, bool fourth);
    void DespawnAllSummon(bool force, bool saveInfectedSkills);
    void DespawnSummon(CSummon* summon, bool first, bool second);
    bool InstantZoneWaitingTime(bool sendPacket);
    void ValidateSkillListInTransform();
    void _GetTransformSkillListByType(
        const CSkillInfo* skillInfo,
        std::vector<const CSkillInfo*>& activeSkills,
        std::vector<const CSkillInfo*>& passiveSkills,
        std::vector<const CSkillInfo*>& toggleSkills,
        std::vector<const CSkillInfo*>& auraSkills
    );
    bool CheckRetrieveGoodsCondition(CUserNewGoods* goods);
    void ResetAbilitySkillList();
    bool SetAbilitySkillList(int skillId, int skillLevel);
    void SendAbilitySkillList(int type);
    void AcquireExtraSkills();
    void TransInfectedSkills(User* target, AbnormalStatusSlotType abnormalType, int maximumSlots);
    void StealInfectedSkills(User* target, int slotGroup, int maximumSlots, int probability, const CSkillInfo* skillInfo);
    void _AcquireAllHonorsSkill();
    void AcquireAllHonorsSkill();
    void SetFishingDamageBonus(double damageBonus);
    bool QueryEnemy(int relation);
    bool QueryRealEnemy(int relation);
    void StartSpelling(const CSkillInfo* skillInfo, CObject* target);
    void StopSpelling();
    bool IsRidingOf(PetCheckCond condition, int value);
    bool IsRidingForEvent();
    bool RideForEvent(int npcClassId, int durationMinutes, int eventId);
    void EatStriderFood(int delta);
    int GetJobGroup() const noexcept { return m_nJobGroup; }

protected:
    void ShowStriderHungryGauge();
    void BeTransInfectedSkills(
        User* target,
        AbnormalStatusSlotType abnormalType,
        int maximumSlots,
        std::vector<InfectedSkillType>& transferredSkills
    );
    void BeStealedInfectedSkills(
        User* source,
        int slotGroup,
        int maximumSlots,
        int probability,
        const CSkillInfo* skillInfo,
        std::vector<InfectedSkillType>& stolenSkills
    );

public:
    enum PetSpawnCmd : std::int32_t {
        PETSPAWNCMD_DESPAWN = -1,
        PETSPAWNCMD_NONE = 0,
        PETSPAWNCMD_SPAWN = 1,
    };

    enum PetSpawnStep : std::int32_t {
        PETSPAWNSTEP_NONE = 0,
        PETSPAWNSTEP_DESPAWN = 1,
        PETSPAWNSTEP_DESPAWN_BASE = 2,
        PETSPAWNSTEP_DESPAWN_SAVE_INFECTED_SKILL = 2,
        PETSPAWNSTEP_DESPAWN_DEPOSITPET = 3,
        PETSPAWNSTEP_DESPAWN_DELETE_SUMMON = 4,
        PETSPAWNSTEP_DESPAWN_SAVE_PET = 5,
        PETSPAWNSTEP_DESPAWN_MAX = 6,
        PETSPAWNSTEP_WITHDRAWPET_START = 6,
        PETSPAWNSTEP_WITHDRAWPETITEMS_START = 7,
        PETSPAWNSTEP_WITHDRAWPETITEMS_COMPLETE = 8,
        PETSPAWNSTEP_MAX = 9,
    };

    void ClearPetSpawn();
    bool IsPetSummoning();
    bool OnPetStep(int step);
    void OnDespawnPet();
    bool BeginSpawnPet(std::uint32_t petCollarSid, bool newlySummoned);
    bool SummonPet(CItem* petCollar, bool newlySummoned);
    bool EndSpawnPet();
    bool BeginDespawnPet();
    bool EndDespawnPet();
    void OnChangeSubJobBegin();
    void OnReplyEvolvePet(const unsigned char* packet);
    void PetLevelChanged(int petDatabaseId, int level);
    void PetNameChanged(int petDatabaseId);
    void ReserveSummon(int count, const unsigned char* packet);
    void ClearSummonReserved();
    void SetMaxSummonPoint(int summonPoint) { m_nMaxSummonPoint = summonPoint; }
    int GetMaxSummonPoint() { return m_nMaxSummonPoint; }
    int GetSummonPoint() { return m_nSummonPoint; }
    bool CheckSummonPointAddable(int summonPoint) {
        const std::uint32_t totalBits = static_cast<std::uint32_t>(m_nSummonPoint) + static_cast<std::uint32_t>(summonPoint);
        std::int32_t total = 0;
        std::memcpy(&total, &totalBits, sizeof(total));
        return total <= m_nMaxSummonPoint;
    }
    void DecreaseSummonPointByDespawn(CSummon* summon);
    CSummon* GetOneSummon(OneSummonType summonType);
    CSummon* GetOneSummonLive(OneSummonType summonType);
    bool SummonPendingSummonOrPet(
        int classId,
        int dbId,
        int level,
        int originalRemainTime,
        int remainTime,
        int experienceRate,
        double hp,
        double mp,
        int consumeItemId,
        int consumeItemCount,
        int isSummon
    );
    void SummonAllPending();
    bool HavePCTrap();
    void ActivatePCTrap();
    void SummonPCTrap(int classId);
    bool DespawnPCTrap();
    void OnSpawnPCTrap(CPCTrap& trap);
    void OnDespawnPCTrap(long trapId);
    bool SummonNpc(int classId, int count, const CSkillInfo* skillInfo, NpcSummonType summonType, int distance);
    bool GetSummonedNpcPos(const CSkillInfo* skillInfo, FVector& position);
    bool GetHallucinationPos(CCreature* target, FVector& position);
    bool CopyUserBuffToNPC_Reserve(CNPC* npc);
    void SetAndSendNPCParameters(
        CNPC* npc, NpcSummonType summonType, FVector& position, int distance, const wchar_t* aiName, int hp, int mp
    );
    bool LinkSummonedNpc(CNPC* npc);
    bool SummonHallucination(
        int classId, int count, const CSkillInfo* skillInfo, NpcSummonType summonType, CCreature* target
    );
    int AgathionSkillDelayShow();
    void AgathionSkillDelayDel(const int elapsedTime);
    void AgathionSkillDelayReset();
    int GetReuseDelayInitTime() const { return m_nReuseDelayInitTime; }
    void InitAgathionDailySkillResetTime(int resetHour, int resetMinute, bool usePreviousDay);
    bool SummonAgathion(int agathionClassId, int level);
    void DespawnAgathion(int reason);
    bool HaveAgathion();
    bool HaveAgathion(int agathionId);
    void SetAgathionElapsedTime(int agathionId, int elapsedTime);
    void EnterOneDayRewardLock(const wchar_t* callerFile, int callerLine);
    void LeaveOneDayRewardLock();
    int GetLevelForOneDayReward() const;
    void OnDailyMissionCheckAndUpdate(E_DAILY_MISSION_TYPE missionType);
    void _DailyMissionMapRelease(std::multimap<E_DAILY_MISSION_TYPE, ICDailyMission*>& missions);
    void DailyMissionMapRelease();
    bool CanSummon(bool summon, bool pet);
    bool SetOneDayRewardMapForDB(
        int databaseId,
        std::int32_t timeRewarded,
        std::int32_t updateCheckTime,
        int accumulateCount,
        int stateType,
        const SYSTEMTIME& createTime
    );
    void CheckOneDayReward(bool sendPacket);
    void SendToDoList(E_TODOLIST_TYPE type, bool entering);
    void SendToDoListHtml(E_TODOLIST_TYPE type, const wchar_t* name);
    bool OnRequestOneDayReceiveReward(int databaseId);
    bool HaveSummonedNpc();
    int GetSummonCount();
    int _MakeSummonIndex();
    int _GetSummonIndex(int summonServerId);
    int GetSummonIndex(int summonServerId);
    void DeleteSummonedNpc(int summonedNpcId);
    void ResetPet(int petCollarDatabaseId);
    void _UnlinkSummon(CSummon* summon);
    void UnlinkSummon(CSummon* summon);
    bool HaveSummon();
    CPet* GetPet();
    void GetAllSummon(std::vector<CSummon*>& summons);
    void GetAllSummonAndPet(std::vector<CSummon*>& summons);
    void CopyUserBuffToNPC(CNPC* npc);
    void OnSummonAbnormalState(int summonClassId, int summonDatabaseId, const unsigned char* packet);
    void GetAllSummonedNpc(std::vector<CNPC*>& summonedNpcs);
    void CacheMasterInfoToAll();
    bool IsMySummonOrPet(CCreature* creature);
    User* GetTargetOrMe();
    bool NeedToHideIdentityInfo();
    bool HavePet();
    bool HaveSummonedNpc(int npcClassId);
    CNPC* GetSummonedNPC(int npcClassId);
    bool CanWorldSay();
    void SaveCharExtraInfo();
    void UpdateTotalPeaceZoneTime();
    void CheckPeaceZoneTimeOnTimerExpired();
    void LogPlayTimeOnLeaveWorld();
    void LogEquippedItemOnLeaveWorld();
    bool IsHeroEntry(bool olympiadEntry);
    bool CanResetTimeStamp(PledgeBonus::TimeStamp timeStamp, L2ServerLong currentTime);
    PledgeBonus::Activity GetPledgeActivity();
    bool HasAcademyStudentMasterRelation();
    void ScreenNotice(const char16_t* message);
    void PetitionSayBySystem(const char16_t* message);
    void SetKalieToken(std::int32_t token);
    int GetKalieToken();
    bool SetSSQPart(SSQPart::type part);
    bool SetSSQPosition(SSQPosition::type position);
    bool SetSealSelectionNo(SSQSeal::type seal);
    bool SetSSQRoundNumber(int roundNumber);
    bool SetSSQPoint(std::int64_t point);
    bool SetDepositedSSQItemCount(SSQPart::type part, SSQItem::type itemType, std::int64_t count);
    bool SetTicketBuyCount(int count);
    void JoinEventCampaign(int campaignId, int campaignStepId, int goalGroup);
    void InitEventCampaignInfo();
    int GetEventCampaignGoalGroup() const;
    bool CanRestartByAgathion();
    void SetBeautyShop(bool active) { m_bBeautyShop = active; }
    bool GetBeautyShop() const { return m_bBeautyShop; }
    bool ConsumeAgathionEnergy(double energy);
    void SendBrAgathionEnergyInfo();
    int GetRecipeCount(int recipeType);
    int GetExDropCount(int countId);
    void ResetDropCount();
    double GetExDropAdditionalProb(int countId);
    void briefingExDropPorb();
    void UpdateExDropCount(ExDropItemData* dropData, bool acquired);
    L2ServerLong EnterItemManipulation(int manipulation);
    void LeaveItemManipulation();
    L2ServerLong GetItemManipulation();

    bool IsMyParty(CCreature* creature);
    bool IsPartyMaster();
    CMultiPartyCommandChannel* GetMPCC();
    bool IsMPCCMaster();
    bool IsMPCCPartyMaster();
    bool IsMPCCMember();
    bool IsPledgeMemberOrAcademyMemberPledgeRelationAffected();
    void ProcessMyParty(bool sendPacket);
    void SetParty(CParty* party, User* partyMaster);
    void ProcessMyMPCC(bool sendPacket);
    void SetPM_WindowOpen(bool open);
    bool GetPM_WindowOpen() const;
    void SendPunishmentHistoryToDB(int reason);
    bool UpdateChar(
        std::int64_t first, std::int64_t second, int third, int fourth, std::uint32_t fifth, std::uint32_t sixth
    );
    bool SetStopLogin(std::uint32_t duration);
    bool SetStopSay(std::uint32_t duration, bool save);
    bool SetStopWorldSay(std::uint32_t duration, bool save);
    bool SetStopHeroSay(std::uint32_t duration, bool save);
    bool SetStopPartyMatch(std::uint32_t duration, bool save);
    std::uint32_t ReducePunishTime();
    bool SavePunishTime();
    bool CheckCreatePartyMatchLevelLimit();
    void SendCompletedDailyQuestListClear();
    void SendQuestNPCLogList(int questId, CUserSocket* socket);
    void SaveQuestNPCLog(int questId);
    int IncreaseQuestNPCLog(int questId, UserNPCLogType userLogType, int npcId, int maximum);
    int GetQuestNPCLog(int questId, UserNPCLogType userLogType, int npcId);
    bool IsDoneQuest(int questId);
    std::int64_t ExpDown(std::int64_t experience);
    bool IsValidPrivateStoreItem(std::int64_t count, std::int64_t price, CItem* item);
    void CheckTutorialEvent(TutorialClientEventEnum event);
    bool SetSnoopClient(int snoopClientId);
    void SendCoolTime();
    bool RemoveItemReuseDelay(int itemClassId);
    void RemoveItemReuseDelayAll();
    void SendFishingInfo(CUserSocket* socket);
    bool TrySaveSkillUsedTime(ReuseDelaySaveReason::Type reason);
    bool SaveSkillUsedTime(ReuseDelaySaveReason::Type reason, bool clearAfterSave);
    int SaveInfectedSkills();
    int GetDispelBuffLevel();
    bool DispelBuff(int targetServerId, int skillId, int skillLevel, int skillSubLevel);
    bool IsInPVPMatch(PVPMatchType::Enum matchType, PVPMatchPlayerState::Enum playerState) const;
    bool IsInSamePVPMatch(User& user, PVPMatchType::Enum matchType, PVPMatchPlayerState::Enum playerState) const;
    bool IsInPVPMatchExcept(PVPMatchType::Enum matchType, PVPMatchPlayerState::Enum playerState) const;
    bool IsCharmOfCourageAvailable(CrystalType crystalType);
    int GetPrivateStoreTime();
    static int GetSulffrageCountByLevel(int level);
    static int GetSocialityFaintByLevel(int level, int sociality);
    void VoteDayElapse();
    void SetSocailityData(_SYSTEMTIME* lastCheck, int sulffrageUsedCount, int sociality);
    void CheckSocailityFaint();
    int GetRemainSulffrageCount();
    bool IncSociality();
    bool VoteSociality(int targetServerId);
    int GetSocialClass();
    void SendHennaItemInfo(int hennaId);
    int GetEquipedHennaCount();
    void SendEquipedHennaListForUnequip();
    void SendHennaUnequipInfo(int hennaId);
    void SendUserHennaInfo(CUserSocket* socket);
    void InitHenna();
    void AddHennaFromBD(int hennaId, bool periodic, int expireTime);
    void DelHennaFromBD(int hennaId, bool periodic);
    void OnLoadPeriodicHennaInfo(const std::uint8_t* packet);
    void _CheckPeriodicHennaOnTick(int currentTime);
    void SendPeriodicHennaInfo(int hennaId);
    bool SetHennaPeriod(int unknown, int period);
    void TimerExpiredOnPartyMatching();
    bool CreateMPCCWithItem(User* user);
    bool IsPartyJoinable();
    bool AskJoinMPCC(User* user, MPCCCreateType createType);
    void SendDominionRenamedToParty(bool renamed);
    void SendDominionChannelSet(bool enabled);
    void RenameByDominionSiege(bool renamed, bool saveToDatabase);
    bool IsDominionTransformed() { return m_bDominionTransformed; }
    void SetDominionTransformed(bool transformed) {
        m_bDominionTransformed = transformed;
        CLogFormatter formatter;
        formatter.WriteLog2(0x7ec, this, nullptr, m_data->m_nDominionSiegeId, transformed, 0, 0, 0, 0, 0);
    }
    void SetDominionRenamedID(int dominionId) { m_nDominionRenamedID = dominionId; }
    int GetDominionRenamedID() { return m_nDominionRenamedID; }
    void GatherRelationChanged();
    void AffectedByWar();
    void GatherSpelledInfoChanged();
    void SendRelationChanged(CUserSocket* socket);
    bool SpelledInfoChangedBuffer(
        char* retainedBuffer,
        int& retainedLength,
        int& retainedCount,
        char* deletedBuffer,
        int& deletedLength,
        int& deletedCount
    );
    void SendSpelledInfoChanged();
    void SendExSpelledInfoChanged();
    void SendNicknameChanged();
    void SendPledgeStatusChanged();
    void SetRelationChanged(bool immediately) {
        if (immediately) {
            runtime::raw_atomic::ExchangeI32(&m_nRelationChanged, 2);
            return;
        }
        runtime::raw_atomic::CompareExchangeI32(&m_nRelationChanged, 1, 0);
    }
    void BroadcastCharInfoUpdate(bool isPolymorph);
    int GetFaceForBroadcast();
    int GetHairShapeForBroadcast();
    void SetOrgSpeed(
        int& groundRun,
        int& groundWalk,
        int& underwaterRun,
        int& underwaterWalk,
        int& hoverRun,
        int& hoverWalk,
        int& airRun,
        int& airWalk
    );
    char GetEventMatchTeamId();
    void SendCharInfo_internal(User* target, bool isPolymorph, bool directSend);
    void SendCreateObject(User* target, bool isPolymorph);
    void SendDominionStartEnd(bool start, CUserSocket* socket);
    void DespawnPetByOlympiadInPeer();

    UnEquipItemQueue SaveUnEquipItemQueue(std::uint32_t itemServerId, SlotType slotType, bool deleteItem);
    etcMsg SaveEtcMsg(
        bool paramFirst,
        bool paramSecond,
        std::int32_t first,
        std::int32_t second,
        std::int64_t first64,
        std::u16string firstString,
        std::u16string secondString,
        std::uint32_t param
    );
    ManipulateQueue SaveManipulateQueue(
        ManipulateType::Enum type,
        int id,
        int count,
        ItemManip* requestList,
        AtomicJob* atomicJob,
        int etcManipulationCount,
        CPacketHelperEx<20480> packet,
        bool premium,
        ItemGetReason::Enum reason,
        etcMsg message
    );
    bool ManipuateEnsoulItems(ItemManip* requestList, int count, CPacketHelperEx<20480> packet, AtomicJob* atomicJob);
    bool ManipuateAlchemyItems(ItemManip* requestList, int count, AtomicJob* atomicJob);
    void CheckManipulateQueue();
    ManiItemResultType _SetManiItemJob(ManipulateQueue queue);
    void _DoneManiItemJob(ManipulateType::Enum type, int id);
    void GatherBeautyPurchaseData(int type, CPacketHelperEx<20480>& packet);
    void SetHairAccFlag(bool enabled);
    void AddBeautyPurchaseData(int type, int primary, int secondary);
    bool HaveBeautyItem(int type, int primary, int secondary);

private:
    void UpdateArtifactbookPageSkills();

    friend bool LoadSelectedCharacterPacket(CDBSocket* socket, const std::uint8_t* packet);
    friend bool WarehouseDepositPacket(CSocket* socket, const std::uint8_t* packet);
    friend bool WarehouseWithdrawPacket(CSocket* socket, const std::uint8_t* packet);
    friend bool DestroyQuest(CSocket* socket, const std::uint8_t* packet);
    friend bool GMCommand(CSocket* socket, const std::uint8_t* packet);
    friend bool RequestPackageSend(CSocket* socket, const std::uint8_t* packet);
    friend bool BuilderCmd2Handler(CUserSocket* socket, const unsigned char* packet);
    friend bool BuilderCmd_recommand(CUserSocket* socket, User* user, char16_t* command);
    // Current BuilderCmd_DeleteBuff writes the private +0x5e18 packet mask
    // after deleting the selected user's abnormal statuses.
    friend bool BuilderCmd_DeleteBuff(CUserSocket* socket, User* user, char16_t* command);
    friend bool BuyPreviewPacket(CSocket* socket, const std::uint8_t* packet);
    friend bool DBPacketHandler::DropItemPacket(CDBSocket* socket, const std::uint8_t* packet);
    friend bool DBPacketHandler::PickItem(CDBSocket* socket, const std::uint8_t* packet);
    friend bool ReplyPrivateStoreCommit(CDBSocket* socket, const std::uint8_t* packet);
    friend bool DBPacketHandler::_ReplyWithdrawPet(
        std::uint32_t userServerId,
        std::uint32_t petCollarServerId,
        int result,
        CPet* pet,
        std::int64_t sp,
        int meal,
        int slot0,
        int slot1,
        int slot2,
        std::int64_t experience,
        double hp,
        double mp,
        wchar_t* name,
        int loadItems,
        std::int64_t downExp,
        int alive,
        int deathCount
    );
    friend bool WithdrawWarehouseItemsPacket(CDBSocket* socket, const unsigned char* packet);
    friend bool LoadItemsPacket(CDBSocket* socket, const std::uint8_t* packet);
    friend bool LoadWarehouseItemsPacket(CDBSocket* socket, const std::uint8_t* packet);
    friend bool ReplyWarehouseItemsEx(CDBSocket* socket, const std::uint8_t* packet);
    friend bool DepositWarehouseItemsPacket(CDBSocket* socket, const unsigned char* packet);
    friend bool ReplyWithdrawWarehouseItemsEx(CDBSocket* socket, const unsigned char* packet);
    friend bool ReplyDepositWarehouseItemsEx(CDBSocket* socket, const unsigned char* packet);
    friend bool ReplyLoadWarehouseItemList(CDBSocket* socket, const unsigned char* packet);
    friend bool ManipulateItemPacket(CDBSocket* socket, const unsigned char* packet);
    friend bool ShortCutInfoPacket(CDBSocket* socket, const unsigned char* packet);
    friend bool ReplyCreateSubJob(CDBSocket* socket, const unsigned char* packet);
    friend bool ReplyDeleteSubJob(CDBSocket* socket, const unsigned char* packet);
    friend bool ReplyGiveItemToPet(CDBSocket* socket, const unsigned char* packet);
    friend bool AcquiredSkillList(CDBSocket* socket, const unsigned char* packet);
    friend bool DBPacketHandler::ReplyChangeSubJob(CDBSocket* socket, const unsigned char* packet);
    friend bool DBPacketHandler::ReplyReloadSkill(CDBSocket* socket, const unsigned char* packet);
    friend bool ReplyRenewSubJob(CDBSocket* socket, const unsigned char* packet);
    friend bool ReplyLoadLastColorPacket(CDBSocket* socket, const unsigned char* packet);
    friend bool ReplyReorganizePledgeMember(CDBSocket* socket, const unsigned char* packet);
    friend bool ReplyConvertItem(CDBSocket* socket, const unsigned char* packet);
    friend bool ReplyLoadBotReportPacket(CDBSocket* socket, const unsigned char* packet);
    friend bool ReplySendBirthdayMail(CDBSocket* socket, const unsigned char* packet);
    friend bool DBPacketExHandler::ResponseLoadOneDayRewardInfo(CDBSocket* socket, const unsigned char* packet);
    friend bool DBPacketExHandler::ResponseLoadAutoSoulShotUI(CDBSocket* socket, const unsigned char* packet);
    friend bool DBPacketExHandler::ResponseLoadUserPledgeBonus(CDBSocket* socket, const unsigned char* packet);
    friend bool DBPacketExHandler::ReplyLoadCardUpdownGameRecord(CDBSocket* socket, const unsigned char* packet);
    friend bool DBPacketExHandler::ReplyUpdateCardUpdownGameRecord(CDBSocket* socket, const unsigned char* packet);
    friend bool DBPacketExHandler::DeleteBuffByAdmin(CDBSocket* socket, const unsigned char* packet);
    friend bool DBPacketExHandler::ResponseCancelDualClass(CDBSocket* socket, const unsigned char* packet);
    friend bool DBPacketExHandler::ReplyCreateDualJob(CDBSocket* socket, const unsigned char* packet);
    friend bool DBPacketExHandler::ResponseBuyBeautyItem(CDBSocket* socket, const unsigned char* packet);
    friend bool DBPacketExHandler::ResponseBeautyPurchaseList(CDBSocket* socket, const unsigned char* packet);
    friend bool DBPacketExHandler::TradeDivideAdena(CDBSocket* socket, const unsigned char* packet);
    friend bool DBPacketExHandler::NewManipulateItemPacket(CDBSocket* socket, const unsigned char* packet);
    friend bool DBPacketExHandler::ReplyAcquirePotentialSkill(CDBSocket* socket, const unsigned char* packet);
    friend bool DBPacketExHandler::ResetAbilitySkill(CDBSocket* socket, const unsigned char* packet);
    friend bool DBPacketExHandler::Reply_RP_EX_CHANGE_AP(CDBSocket* socket, const unsigned char* packet);
    friend bool DBPacketExHandler::ReplyLoadFactionPoint(CDBSocket* socket, const unsigned char* packet);
    friend bool DBPacketExHandler::ResponseLoadBalthusEventUserInfoEx(CDBSocket* socket, const unsigned char* packet);
    friend bool DBPacketExHandler::ReplySetQuest(CDBSocket* socket, const unsigned char* packet);
    friend bool DBPacketExHandler::ResponseResetBeautyItem(CDBSocket* socket, const unsigned char* packet);
    friend class AtomicNpcSetSkillAll;
    friend class AtomicChangeClassAndSetSkill;
    friend bool NpcSetSkillAll(NpcSocket* socket, const unsigned char* packet);
    friend bool EnableTutorialEvent(NpcSocket* socket, const unsigned char* packet);
    friend bool NpcAtomicDelGiveItemParam(NpcSocket* socket, const unsigned char* packet);
    friend bool NpcMoveTaxToResidenceBank(NpcSocket* socket, const unsigned char* packet);
    friend bool NpcMoveMoneyToResidenceBank(NpcSocket* socket, const unsigned char* packet);
    friend bool GiveItemByCastleSiegeDefence(NpcSocket* socket, const unsigned char* packet);
    friend bool NpcShowSellPreviewPagePacket(NpcSocket* socket, const unsigned char* packet);
    friend bool NpcSunRise(NpcSocket* socket, const unsigned char* packet);
    friend void LogNpcAtomicDelGiveItemResult(User* user, int relatedQuestId, int flag);
    friend bool UserPacketEx::RequesPledgeBonusReceiveReward(CSocket* socket, const std::uint8_t* packet);
    friend bool ShortCutRegisterPacket(CSocket* socket, const std::uint8_t* packet);
    friend bool ShortCutDeletePacket(CSocket* socket, const std::uint8_t* packet);
    friend bool ReplyCreatePledgeByNpc(CDBSocket* socket, const std::uint8_t* packet);
    friend bool DBPacketHandler::ReplyUpdateUserPledge(CDBSocket* socket, const std::uint8_t* packet);
    friend bool RequestRecipeBookItemDelete(CSocket* socket, const std::uint8_t* packet);
    friend bool RequestMultiSellChoose(CSocket* socket, const std::uint8_t* packet);
    friend bool NpcSocketEx::ShowBeautyShop(NpcSocket*, const unsigned char*);
    friend bool NpcSocketEx::MoveToServer(NpcSocket*, const unsigned char*);
    friend class CETC_MANI_USE_COUNT;
    friend class CAtomicItemJobCheck;
    friend class CSkillEffect_i_bookmark_teleport;
    friend class CSkillEffect_i_convert_item;
    friend class BalthusEvent;
    friend class CCardCollectSystem;
    friend class CLuckyGameSystem;
    friend class CQuestSystem;
    friend class CEventCampaignStepMultiInfo;
    friend struct QuestOperation_Item;
    friend struct QuestOperation_ItemOnFinishDynamicQuest;
    friend class QuestOperation_MonitorStart_KillNpc;
    friend class QuestOperation_MonitorEnd_KillNpc;
    friend class QuestOperation_ShowHTML_If;
    friend class UserQuest;
    friend class CCreature;
    // Current Doppelganger.cpp copies the master's crest identifiers
    // directly into the CNPC appearance carrier during OnCreated.
    friend class CDoppelganger;
    friend class CUserBroadcaster;
    friend class CCommunity;
    friend class SunsetBroadcastVisitor;
    friend class AllowedWorldChatBroadcastVisitor;
    friend class CCreatureController;
    friend class CContributeData;
    friend class CCrataeCube;
    friend class CObserverRemains;
    friend class CCastle;
    friend class CResidence;
    friend class CWorld;
    friend class CBlockUpset;
    friend class CDominionSiegeEntry;
    friend class COwnThing;
    friend class CDB;
    friend class CBuySell;
    friend class CWareHouseWithdraw;
    friend class CCommissionTradeManager;
    friend class CGoodsInventory;
    friend class CItem;
    friend class COneDayRewardData;
    friend class CToDoListManager;
    friend class CDeferredJob;
    friend class CFortress;
    friend class CInventory;
    friend class CMultiSellList;
    friend class CFieldCycle;
    friend class CFieldCycleSystem;
    friend class CInstantZone;
    friend class CInstantZoneManager;
    friend class CInstantZoneNpcAIEventHandler;
    friend class WOlympiadService;
    friend class CArea;
    friend class CFlyMoveManager;
    friend class CMoveAction;
    friend class CNotice_LevelCond;
    friend class CSkillAction2;
    friend class CSkillEffect_p_create_item;
    friend class CSkillEffect_p_create_common_item;
    friend class CSkillEffect_p_soul_eating;
    friend class CSkillEffect_i_energy_attack;
    friend class CSkillEffect_i_restoration;
    friend class CSkillEffect_i_restoration_random;
    friend class CSkillEffect_i_restoration_AddRandom;
    friend class CSkillEffect_i_restoration_random_group;
    friend class CSkillEffect_i_call_random_skill_consume_item;
    friend class CSkillEffect_i_change_skill_level;
    friend class CSkillEffect_i_death_link_mp;
    // Current i_m_attack_mp@0x00a98370 directly marks the same spelled-info
    // and update-packet fields after deleting infected skills.
    friend class CSkillEffect_i_m_attack_mp;
    friend class CSkillEffect_i_capture_ownthing;
    friend class CSkillEffect_i_capture_ownthing_start;
    friend class CSkillEffect_i_soul_shot;
    friend class CSkillEffect_i_spirit_shot;
    friend class CSkillEffect_i_summon_soul_shot;
    friend class CSkillEffect_i_summon_spirit_shot;
    friend class CSkillEffect_i_fishing_shot;
    friend class CSkillEffect_p_double_summon;
    friend class CSkillEffect_p_violet_boy;
    friend class CSkillEffect_p_bonustimelimit_up;
    friend class CSkillEffect_i_sub_enchant_weapon_level_dec;
    friend class CSkillEffect_i_Enchant_Weapon_Rate;
    friend class CSkillEffect_i_Enchant_Armor_Rate;
    friend class CSkillEffect_i_add_enchant_weapon_level_inc;
    friend class CSkillEffect_i_add_enchant_armor_level_inc;
    friend class EtcManiUpdateUserItemBaseAttribute;
    friend class EtcManiRollbackUserItemBaseAttribute;
    friend class EtcManiUpdateUserItemCombination;
    friend class EtcManiEnsoul;
    friend class EtcManiMultisellResult;
    friend class EtcManiEnchantArtifact;
    friend class EtcManiCreateItemAuctionBid;
    friend class EtcManiWithdrawItemAuctionBid;
    friend class EtcManiSuccessfulItemAuctionBid;
    friend class EtcManiGiveItemByCastleSiegeCount;
    friend class CAuction;
    friend class EtcManiCardUpDownGame;
    friend class EtcManiRemoveInzoneBindingByEvent;
    friend class EtcManiUpdateUserEventTime;
    friend class CSkillEffect_i_sub_enchant_armor_level_dec;
    friend class CSkillEffect_i_pk_count;
    friend class CSkillEffect_i_sp;
    friend class CSkillEffect_i_collecting;
    friend class CSkillEffect_i_bonuscount_up;
    friend class CSkillEffect_i_acc_change;
    friend class CSkillEffect_i_bless_shape_shift_am;
    friend class CSkillEffect_i_bless_shape_shift_wp;
    friend class CSkillEffect_i_restore_shape_shift_item;
    friend class CSkillEffect_i_shape_shift_am;
    friend class CSkillEffect_i_shape_shift_am_fixed;
    friend class CSkillEffect_i_shape_shift_wp;
    friend class CSkillEffect_i_shape_shift_wp_fixed;
    // Current effect bodies access these fields directly. Keep the recovered
    // access boundary narrow without changing User's physical layout.
    friend class CSkillEffect_i_shape_shift_item_auto_matching;
    friend class CSkillEffect_i_abnormal_time_change;
    friend class CSkillOperateCondition_soul_saved;
    friend class CSkillOperateCondition_op_soul_max;
    friend class CSkillOperateCondition_op_not_cursed;
    friend class CSkillOperateCondition_restart_point;
    friend class CPartyMatchingRequest;
    friend class CPartyMatchingWaitingRoomListFilter;
    friend class CParty;
    friend class CMultiPartyCommandChannel;
    friend class CPledge;
    friend class PledgeNS::PledgeSkillAcquireCallback;
    friend class CPledgePenaltyViewer;
    friend class DismissPledge;
    friend class CPledgeMember;
    friend class CPledgeMemberList;
    friend class CPledgeGameUser;
    friend class CPledgeGamePlayers;
    friend class CPledgeGameFuncHandler;
    friend class CPledgeGameSystem;
    friend class CBeautyManager;
    friend class CBasicActionHandler_PET_DEPOSIT;
    friend class CBasicActionHandler_TELEPORT_BOOKMARK;
    friend class CBotReportAgent;
    friend class CUserBookmark;
    friend class CUserBookmarkHandler;
    friend void BuilderCmdHandler::OnAddSlot(User* user, char16_t* command);
    friend void BuilderCmdHandler::OnSendList(User* user, char16_t* command);
    friend void BuilderCmdHandler::OnChangeSlot(User* user, char16_t* command);
    friend class UserRequestTimeSetter;
    friend class PledgeSkill;
    friend class SkillEnchantOperator;
    friend class SkillEnchantNormal;
    friend class SkillEnchantSafe;
    friend class SkillEnchantUntrain;
    friend class SkillEnchantRouteChange;
    friend class SkillEnchantForce;
    friend class SetParamChanged;
    friend class SetCrest;
    friend class SetEmblem;
    friend class SetAllianceCrest;
    friend class SetRelationChanged;
    friend bool Raid_RCS_REQUEST_SYNC_PLEDGE_TYPE(RaidClientSocket* socket, const unsigned char* packet);
    friend class DxSaveNoblessType;
    friend class DxSaveHeroType;
    friend class CRelationManager;
    friend class CVitality;
    friend class RelayClient;
    friend class Duel_SetRelationChanged;
    friend class CancelRenamed;
    friend void _ContainValidPremiumItem(
        User* user, int itemCount, std::list<InquiryPremiumItemAckPacketInfo>& premiumItems
    );
    friend bool IBInquiryGoodsInventoryAckPacket(CGoodsInventorySocket* socket, const std::uint8_t* packet);
    friend bool IBInquiryExistGoodsAck(CGoodsInventorySocket* socket, const std::uint8_t* packet);
    friend bool ReplySavePremiumItemForChar(CDBSocket* socket, const unsigned char* packet);
    friend bool ReplyLoadUserPremiumItemPacket(CDBSocket* socket, const unsigned char* packet);
    friend bool ReplyManipulatePremiumItemsPacket(CDBSocket* socket, const unsigned char* packet);
    friend bool PetSummonActionHelper::CheckMasterPKCount(User* user, PetCommandType command);

public:
    std::int32_t m_nOldTargetId; // +0x38d8
    CrystalType m_CrystallizeLevel; // +0x38dc

private:
    CYieldLock m_UnEquipQueueLock; // +0x38e0..+0x38ef
    std::queue<UnEquipItemQueue> m_UnEquipItemQueue; // +0x38f0..+0x391f

public:
    bool m_bSpelledInfoChanged; // +0x3920

private:
    std::array<std::byte, 0x03> m_alignment3921To3924; // +0x3921..+0x3923
public:
    CYieldLock m_ModifySkillLock; // +0x3924..+0x3933

private:
    std::array<std::byte, 0x04> m_alignment3934To3938; // +0x3934..+0x3937

public:
    ModifySkillMap m_InsertSkill; // +0x3938..+0x3957
    ModifySkillMap m_DeleteSkill; // +0x3958..+0x3977

private:
    CYieldLock m_ManipulateQueueLock; // +0x3978..+0x3987
    std::queue<ManipulateQueue> m_ManipulateQueue; // +0x3988..+0x39b7
    ManiItemJobMap m_mapManiItemJob; // +0x39b8..+0x39d7
    volatile std::int32_t m_nWaitGetItemOnce; // +0x39d8..+0x39db
public:
    std::int32_t m_nBeautyTickCount; // +0x39dc..+0x39df
    BeautyPurchaseData m_BeautyPurchase; // +0x39e0..+0x3a77
    bool m_bHairAccFlag; // +0x3a78
    std::array<std::byte, 0x03> m_alignment3a79To3a7c; // +0x3a79..+0x3a7b
    std::int32_t m_nCommissionTickCount; // +0x3a7c
    std::int32_t m_nPledgeJoinTimeStamp; // +0x3a80..+0x3a83
    std::int32_t m_nPledgeJoinSearchTickCount; // +0x3a84..+0x3a87
    std::int32_t m_nWaitListTickCount; // +0x3a88..+0x3a8b
    std::int32_t m_nDraftSearchTickCount; // +0x3a8c..+0x3a8f
    std::int32_t m_nPledgeJoinIOTickCount; // +0x3a90..+0x3a93
    volatile std::int32_t m_PledgeSearchDelay; // +0x3a94..+0x3a97
    volatile std::int32_t m_WaitListDelay; // +0x3a98..+0x3a9b
    volatile std::int32_t m_DraftSearchDelay; // +0x3a9c..+0x3a9f
    volatile std::int32_t m_PledgeIODelay; // +0x3aa0..+0x3aa3
    std::int32_t m_tNextBindTime; // +0x3aa4

    CYieldLock m_UpdateFriendsDequeLock; // +0x3aa8..+0x3ab7
    std::deque<UpdateFriendData> m_UpdateFriendsDeque; // +0x3ab8..+0x3ae7

private:
    bool m_bMentoringGraduate; // +0x3ae8
    std::array<std::byte, 0x03> m_alignment3ae9To3aec; // +0x3ae9..+0x3aeb
public:
    std::int32_t m_nLoginTimeStamp; // +0x3aec..+0x3aef
private:
    std::int32_t m_nCommLocalCommunityBid; // +0x3af0..+0x3af3
    std::int32_t m_UsedEnergyCount; // +0x3af4

public:
    std::int32_t m_UsedSoulCount; // +0x3af8

private:
    std::array<std::byte, 0x04> m_alignment3afcTo3b00; // +0x3afc..+0x3aff
    std::int64_t m_nAccumulatedExpForItem; // +0x3b00..+0x3b07
    std::int64_t m_nAccumulatedExpForItemBefore; // +0x3b08..+0x3b0f
    std::int32_t m_SoulCount; // +0x3b10
    std::uint32_t m_SoulStartTime; // +0x3b14
    std::int32_t m_nDwarfCreateItemLevel; // +0x3b18..+0x3b1b
    std::int32_t m_nCommonCreateItemLevel; // +0x3b1c..+0x3b1f
    CEconomy* m_pEconomy; // +0x3b20..+0x3b27
    CUserBookmark m_bookmark; // +0x3b28..+0x3b77
    volatile std::int32_t m_nUserDeleteCount; // +0x3b78..+0x3b7b
    std::int32_t m_nNpcCountInSight; // +0x3b7c..+0x3b7f
    std::int32_t m_nUserCountInSight; // +0x3b80
    std::array<std::byte, 0x04> m_partialGap3b84To3b88; // +0x3b84..+0x3b87
    CNPC* m_pPolymorphNPC; // +0x3b88..+0x3b8f
    bool m_bStopTime; // +0x3b90
    bool m_bStopTimeByObserving; // +0x3b91
    bool m_bKeepExp; // +0x3b92
    std::byte m_alignment3b93To3b94; // +0x3b93
    std::uint32_t m_dwKeepExpTick; // +0x3b94..+0x3b97
public:
    // Directly synchronized by the current item/post job owners.
    volatile std::int32_t m_lWaitItemJob; // +0x3b98..+0x3b9b
    volatile std::int32_t m_lWaitWarehouse; // +0x3b9c..+0x3b9f
    CFishingController* m_pFishingController; // +0x3ba0..+0x3ba7
private:
    double m_dFishingDmgBonus; // +0x3ba8..+0x3baf
    bool m_bWaitFishingShotUse; // +0x3bb0
    std::array<std::byte, 0x03> m_alignment3bb1To3bb4; // +0x3bb1..+0x3bb3
    CrystalType m_eIgnoreGradePenalty; // +0x3bb4..+0x3bb7
    bool m_bWaitSoulShotUse; // +0x3bb8
    bool m_bWaitSpiritShotUse; // +0x3bb9
    bool m_bWaitSummonSoulShotUse; // +0x3bba
    bool m_bWaitSummonSpiritShotUse; // +0x3bbb
    bool m_bNpcNotified; // +0x3bbc
    std::array<std::byte, 0x03> m_alignment3bbdTo3bc0; // +0x3bbd..+0x3bbf
    std::int32_t m_TutorialEventSet; // +0x3bc0..+0x3bc3
    std::int32_t m_nSnoopGmId; // +0x3bc4..+0x3bc7
    std::int32_t m_nSnoopClientId; // +0x3bc8..+0x3bcb
public:
    bool m_bWaitRecipeMake; // +0x3bcc
private:
    bool m_bIsNonChaoInNow; // +0x3bcd
    std::array<std::byte, 0x02> m_alignment3bceTo3bd0; // +0x3bce..+0x3bcf
    std::uint32_t m_dwDropItemTick; // +0x3bd0..+0x3bd3
    std::array<std::byte, 0x04> m_alignment3bd4To3bd8; // +0x3bd4..+0x3bd7
    CBotChecker m_BotChecker; // +0x3bd8..+0x3c47
public:
    // Current CUserSocketReleaseVerifier@00c26a7c reads this byte directly
    // before delaying a combat-mode disconnect. TotalChecker's matching
    // User field list supplies the original public identifier immediately
    // after m_BotChecker. The current CBotChecker expansion moves it to
    // +0x3c48.
    bool m_bBeingCheckedIfBot; // +0x3c48
private:
    std::array<std::byte, 0x03> m_alignment3c49To3c4c; // +0x3c49..+0x3c4b
    std::int32_t m_nLastKillNpcSId; // +0x3c4c
    std::uint32_t m_dwLastKillTime; // +0x3c50
    CYieldLock m_BotCheckTimeLock; // +0x3c54..+0x3c63
    std::array<std::byte, 0x04> m_partialGap3c64To3c68; // +0x3c64..+0x3c67
public:
    // Current CheckWyvernOnTimer reads, clears, increments and signed-compares
    // this exact DWORD. TotalChecker CodeView supplies the original public
    // identifier without supplying the current offset.
    std::int32_t m_nNoFlyingAreaCount; // +0x3c68..+0x3c6b
private:
    std::int32_t m_nLastUseDice; // +0x3c6c
public:
    std::int32_t m_nLastAddPCCafePointTime; // +0x3c70..+0x3c73
private:
    FootEffectEnum m_FootEffect; // +0x3c74..+0x3c77
    std::int32_t m_FootEffectEndTick; // +0x3c78..+0x3c7b
    std::uint32_t m_nAutoSoulshotFlags[17]; // +0x3c7c..+0x3cbf
    std::uint32_t m_nAutoSoulshotFlag; // +0x3cc0..+0x3cc3
    AutoSoulShotUi m_AutoSoulShotUi[4]; // +0x3cc4..+0x3ce3
    bool m_bWaitCrystal; // +0x3ce4
    std::array<std::byte, 0x03> m_alignment3ce5To3ce8; // +0x3ce5..+0x3ce7
public:
    volatile std::int32_t m_lWaitVariation; // +0x3ce8..+0x3ceb
    bool m_bCanGetPCCafePoint; // +0x3cec
    bool m_bCameraMode; // +0x3ced
private:
    char16_t m_PetitionSeq[512]; // +0x3cee..+0x40ed
    bool m_bIsNewInWorld; // +0x40ee
    std::byte m_alignment40efTo40f0; // +0x40ef
public:
    std::uint32_t m_nPledgeSid; // +0x40f0..+0x40f3
    std::uint32_t m_nPartySid; // +0x40f4..+0x40f7
    std::uint32_t m_nTradeTimeStamp; // +0x40f8..+0x40fb
    bool m_CanPetitionVote; // +0x40fc
    bool m_bCanSay; // +0x40fd
    std::array<std::byte, 0x02> m_alignment40feTo4100; // +0x40fe..+0x40ff
    std::uint32_t m_dwStopSay; // +0x4100..+0x4103
    std::uint32_t m_dwStopHeroSay; // +0x4104..+0x4107
    std::uint32_t m_dwStopWorldSay; // +0x4108..+0x410b
    std::uint32_t m_dwStopPartyMatch; // +0x410c..+0x410f
    std::uint32_t dwFreezeRemainTime; // +0x4110..+0x4113
    bool m_bIsFreezedByGM; // +0x4114
    bool m_bConnected; // +0x4115
private:
    std::array<std::byte, 0x02> m_alignment4116To4118; // +0x4116..+0x4117
public:
    std::uint32_t m_dwSitTick; // +0x4118..+0x411b
private:
    std::array<std::byte, 0x04> m_partialGap411cTo4120; // +0x411c..+0x411f
    FVector m_vOriginalPositionBeforeObserving; // +0x4120..+0x4137
    CObserverRemains* m_pObserverRemains; // +0x4138..+0x413f
    std::int32_t m_nObservingStartTime; // +0x4140..+0x4143
    std::int32_t m_nNextObservingAnnounceTime; // +0x4144..+0x4147
    std::int32_t m_nObservingPeriod; // +0x4148..+0x414b
    std::array<std::byte, 0x04> m_alignment414cTo4150; // +0x414c..+0x414f
    CShortCut m_ShortCut; // +0x4150..+0x5127
public:
    bool m_bIsObserving; // +0x5128
    bool m_bInstantMoveReady; // +0x5129
    bool m_bInstantMoveReady2; // +0x512a
    bool m_bAirShipMove; // +0x512b
    bool m_bFindDistanceReady; // +0x512c
    bool m_bItemDebugReady; // +0x512d
    bool m_bPlaying; // +0x512e
private:
    std::byte m_alignment512fTo5130; // +0x512f
public:
    std::int32_t m_nCastleSiegeId[9]; // +0x5130..+0x5153
    std::int32_t m_nCastleSiegeStatus[9]; // +0x5154..+0x5177
    std::int32_t m_nFortressUnderOffenceSiegeId; // +0x5178..+0x517b
    std::int32_t m_nFortressUnderDefenceSiegeId; // +0x517c..+0x517f
    std::int32_t m_bAllianceMaster; // +0x5180..+0x5183
    // TotalChecker CodeView names this original public DWORD. The current
    // image keeps the same field immediately before the private-store tick.
    std::uint32_t m_nLastTimeCheck; // +0x5184..+0x5187
    std::uint32_t m_dwPrivateStoreTick; // +0x5188..+0x518b
    std::uint32_t m_dwLastSendTick; // +0x518c..+0x518f
    std::int32_t m_nTotalPrivateStoreSecond; // +0x5190..+0x5193
    std::uint32_t dwLastSendTime; // +0x5194..+0x5197
private:
    std::int32_t m_nPCCafeTimeStamp; // +0x5198..+0x519b
    std::array<std::byte, 0x04> m_alignment519cTo51a0; // +0x519c..+0x519f
    QuestionAnswer m_QuestionAnswer; // +0x51a0..+0x5707
    COwnThing* m_pOwnThing; // +0x5708..+0x570f
    bool m_bHasFortressFlag; // +0x5710
    // Current User construction and _ReplyWithdrawPet both write one here.
    // No current artifact binds the historical identifier yet.
    std::byte m_target5711PetWithdrawByte; // +0x5711
    std::array<std::byte, 0x02> m_alignment5712To5714; // +0x5712..+0x5713
public:
    std::uint32_t m_PetSID; // +0x5714..+0x5717
    std::uint32_t m_PetActionLastTick; // +0x5718..+0x571b
    JobOnDespawnPet m_jobOnDespawnPet; // +0x571c..+0x571f
    std::int32_t m_nJobOnDespawnPetCompleteArgument; // +0x5720..+0x5723
    CYieldLock m_SummonLock; // +0x5724..+0x5733
    std::array<std::byte, 0x04> m_alignment5734To5738; // +0x5734..+0x5737
    std::map<std::int32_t, std::uint32_t> m_mapSummonSID; // +0x5738..+0x5757
    std::int32_t m_nMaxSummonPoint; // +0x5758..+0x575b
    std::int32_t m_nSummonPoint; // +0x575c..+0x575f
protected:
    CUserRidingInfo* m_pRidingInfo; // +0x5760..+0x5767
private:
    volatile std::int32_t m_initRidingInfo; // +0x5768..+0x576b
    std::int32_t m_nHomePCCafeCount; // +0x576c..+0x576f
    bool m_bAnnouncedHomeMax; // +0x5770
    std::array<std::byte, 0x03> m_alignment5771To5774; // +0x5771..+0x5773
    std::int32_t m_nSavedDate; // +0x5774..+0x5777
public:
    // Current no_limit_fly command and CheckWyvernOnTimer independently
    // access this byte. TotalChecker CodeView supplies the original public
    // spelling. The current target fixes its placement and behavior.
    bool m_NoLimitFlying; // +0x5778
private:
    std::array<std::byte, 0x03> m_alignment5779To577c; // +0x5779..+0x577b
public:
    // Current CommSysinfoPacket reads this DWORD at +0x577c. The original
    // spelling and public access are kept by the TotalChecker CodeView
    // layout. Only the offset/consumer are asserted as current facts.
    std::uint32_t m_dwL2CommTick; // +0x577c..+0x577f
    CEventMatchUser m_EventMatchUser; // +0x5780..+0x578b
    std::array<std::byte, 0x04> m_alignment578cTo5790; // +0x578c..+0x578f
    CDuelUserInfo m_DuelUserInfo; // +0x5790..+0x57b7
    bool m_bReceiveUserPVPMatchRecord; // +0x57b8
private:
    std::array<std::byte, 0x03> m_alignment57b9To57bc; // +0x57b9..+0x57bb
    CYieldLock m_HTMLLock; // +0x57bc..+0x57cb
    bool m_bPrevHTMLLinkCheckState; // +0x57cc
    std::array<std::byte, 0x03> m_alignment57cdTo57d0; // +0x57cd..+0x57cf
    std::vector<HTMLCmdData> m_ExpectedHtmlReplyList; // +0x57d0..+0x57ef
    WOlympiadUser* m_pWOlympiadUser; // +0x57f0..+0x57f7
public:
    CPledgeGameUser m_PledgeGameUser; // +0x57f8..+0x5877
    MatchingPoint::MatchingPointModel m_MatchingPoint; // +0x5878..+0x58af
private:
    bool m_bFriendAddEnabled; // +0x58b0
    bool m_bTradeEnabled; // +0x58b1
    bool m_bBlockAll; // +0x58b2
    std::array<std::byte, 0x05> m_alignment58b3To58b8; // +0x58b3..+0x58b7
public:
    std::vector<CInspector*> m_pInspectors; // +0x58b8..+0x58d7
    std::vector<wchar_t*> m_vBotList; // +0x58d8..+0x58f7
    std::vector<int> m_vUsedVoteTarget; // +0x58f8..+0x5917
    std::int32_t m_nCursedWeaponClassId; // +0x5918..+0x591b
private:
    std::array<std::byte, 0x04> m_alignment591cTo5920; // +0x591c..+0x591f
protected:
    CUserSocket* m_pSocket; // +0x5920..+0x5927
    std::int32_t m_nTrade; // +0x5928..+0x592b
    std::int32_t m_nPetitionId; // +0x592c..+0x592f
    std::int32_t m_nWaitPetitionVote; // +0x5930..+0x5933
    std::array<std::byte, 0x04> m_alignment5934To5938; // +0x5934..+0x5937
    RWLock m_recipeBookMapLock; // +0x5938..+0x596f
    std::map<int, CRecipeInfo*> m_recipeBookMap; // +0x5970..+0x598f
    char16_t m_wszPrivateStoreMsg[30]; // +0x5990..+0x59cb
    char16_t m_wszPrivateStoreWholeMsg[30]; // +0x59cc..+0x5a07
    char16_t m_wszPrivateStoreBuyMsg[30]; // +0x5a08..+0x5a43
    char16_t m_wszRecipeStoreMsg[30]; // +0x5a44..+0x5a7f
    RWLock m_recipeShopMapLock; // +0x5a80..+0x5ab7
    std::map<int, CRecipeInfoData*> m_recipeShopMap; // +0x5ab8..+0x5ad7
    CBossRecordList m_BossRecordList; // +0x5ad8..+0x5b27
    PrivateStoreMap m_PrivateStoreMap; // +0x5b28..+0x5b47
    PrivateStoreMap m_PrivateStoreBuyMap; // +0x5b48..+0x5b67
    PrivateStoreMap m_PrivateStoreWholeMap; // +0x5b68..+0x5b87
    bool m_bCanHearWhisper; // +0x5b88
private:
    std::array<std::byte, 0x03> m_alignment5b89To5b8c; // +0x5b89..+0x5b8b
    L2ServerLong m_nItemManipulation; // +0x5b8c..+0x5b8f
    std::uint32_t m_nLastItemManipulateTick; // +0x5b90..+0x5b93
public:
    std::int32_t m_nItemManipulationID; // +0x5b94..+0x5b97
    std::int32_t m_nItemManipulationCount; // +0x5b98..+0x5b9b
private:
    std::uint32_t m_nAccountID; // +0x5b9c
public:
    std::array<char16_t, 15> m_sAccountName; // +0x5ba0..+0x5bbd
private:
    std::array<char, 15> m_sAccountNameChar; // +0x5bbe..+0x5bcc
    std::array<std::byte, 0x03> m_alignment5bcdTo5bd0; // +0x5bcd..+0x5bcf

public:
    Agent* m_pAgent; // +0x5bd0..+0x5bd7
    std::map<AgentType, Agent*> m_AgentMap; // +0x5bd8..+0x5bf7

private:
    BuilderCmdHistory* m_pBuilderCmdHistory; // +0x5bf8..+0x5bff
    std::array<std::byte, 0x04> m_partialGap5c00To5c04; // +0x5c00..+0x5c03
    PledgeType m_pledgeTypeWhenAskJoin; // +0x5c04..+0x5c07
public:
    std::int32_t m_nUseTimeStamp; // +0x5c08..+0x5c0b
private:
    std::int32_t m_nPreviewTimeStamp; // +0x5c0c..+0x5c0f
    std::int32_t m_tHarangueTimeStamp; // +0x5c10..+0x5c13
    std::int32_t m_nUseTimeStampForToday; // +0x5c14..+0x5c17
    _SYSTEMTIME
    m_tmLastSocialityCheck; // +0x5c18..+0x5c27
public:
    CObjectStorage<CFriends> m_osFriends; // +0x5c28..+0x5c2f
private:
    CYieldLock m_ItemManipulationLock; // +0x5c30..+0x5c3f
    std::int32_t m_nPledgeDismissTime; // +0x5c40..+0x5c43
    std::int32_t m_nPledgeOustedTime; // +0x5c44..+0x5c47
    std::int32_t m_nPledgeWithdrawTime; // +0x5c48..+0x5c4b
    std::uint32_t m_nBreathLimit; // +0x5c4c..+0x5c4f
public:
    CMentor m_Mentor; // +0x5c50..+0x5c97
    CMentee m_Mentee; // +0x5c98..+0x5cfb
    std::int32_t m_nCoupleID; // +0x5cfc..+0x5cff
    std::int32_t m_nCoupleOption; // +0x5d00..+0x5d03
    bool m_bAskCoupleAction; // +0x5d04
private:
    std::array<std::byte, 0x03> m_alignment5d05To5d08; // +0x5d05..+0x5d07
    volatile std::int32_t m_nVoteCount; // +0x5d08..+0x5d0b
    volatile std::int32_t m_nBonusTimeLimit; // +0x5d0c..+0x5d0f
    volatile std::int32_t m_nBonusCount; // +0x5d10..+0x5d13
    volatile std::int32_t m_nPlayTime; // +0x5d14..+0x5d17
    volatile std::int32_t m_nPlayTimeCount; // +0x5d18..+0x5d1b
    _SYSTEMTIME m_tmLastInitTime; // +0x5d1c..+0x5d2b
    std::int32_t m_nVoteTimeStamp; // +0x5d2c..+0x5d2f
    float m_nRate; // +0x5d30..+0x5d33
    volatile std::int32_t m_nVoteBonusTime; // +0x5d34..+0x5d37
    bool m_bVotePeaceZone; // +0x5d38
    std::array<std::byte, 0x03> m_alignment5d39To5d3c; // +0x5d39..+0x5d3b

public:
    ItemRoutingType m_ItemRoutingType; // +0x5d3c..+0x5d3f

private:
    std::int32_t m_nMultisellNpcSid; // +0x5d40..+0x5d43
    std::int32_t m_nMaxInventoryLimit; // +0x5d44
    std::int32_t m_nMaxBeltInventoryLimit; // +0x5d48
    std::int32_t m_nMaxQuestInventoryLimit; // +0x5d4c
    std::int32_t m_nMaxPrivateBankCount; // +0x5d50
    std::int32_t m_nMaxDwarvenRecipeCount; // +0x5d54
    std::int32_t m_nMaxCommonRecipeCount; // +0x5d58
    std::int32_t m_nMaxDwarvenRecipeStore; // +0x5d5c
    std::int32_t m_nMaxCommonRecipeStore; // +0x5d60
    std::int32_t m_nMaxArtifactInventoryLimit; // +0x5d64
    std::int32_t m_nDwarvenRecipeCount; // +0x5d68
    std::int32_t m_nCommonRecipeCount; // +0x5d6c
    CSmartID m_PendingItem[PUIT_MAX]; // +0x5d70..+0x5d9f
    RWLock m_PendingItemLock; // +0x5da0..+0x5dd7
    CObject* m_pSkillTargetObject; // +0x5dd8..+0x5ddf
    volatile std::int32_t m_nSunRiseForce; // +0x5de0..+0x5de3
    std::int32_t m_nPM_Tick; // +0x5de4..+0x5de7
    bool m_bPM_WindowOpen; // +0x5de8
    std::array<std::byte, 0x03> m_alignment5de9To5dec; // +0x5de9..+0x5deb
    std::int32_t m_nBBSLocation; // +0x5dec..+0x5def
    std::int64_t m_nParamChanged; // +0x5df0
    volatile std::int32_t m_nRelationChanged; // +0x5df8..+0x5dfb
    std::uint32_t m_nPledgeCrestDbId; // +0x5dfc..+0x5dff

public:
    std::uint32_t m_nPledgeEmblemDbId; // +0x5e00..+0x5e03

private:
    std::uint32_t m_nAllianceCrestDbId; // +0x5e04..+0x5e07
    const CSkillInfo* m_pNowSkill; // +0x5e08..+0x5e0f
    std::int32_t m_nSkillEndTime; // +0x5e10..+0x5e13
    std::int32_t m_nSkillTargetId; // +0x5e14..+0x5e17

public:
    std::uint32_t m_NeedUpdatePacketBits; // +0x5e18..+0x5e1b

private:
    std::int32_t m_nRelationSlot; // +0x5e1c..+0x5e1f
    std::uint32_t m_currentRequestTime; // +0x5e20..+0x5e23
    std::int32_t m_currentRequestTimeFlag; // +0x5e24..+0x5e27
    std::int32_t m_nCharNameColor; // +0x5e28..+0x5e2b
    std::int32_t m_nNickNameColor; // +0x5e2c..+0x5e2f
    bool m_bRestartState; // +0x5e30
    std::byte m_alignment5e31To5e32; // +0x5e31
    std::uint16_t m_CompassRegionMask; // +0x5e32..+0x5e33
    std::array<std::byte, 0x0c> m_partialGap5e34To5e40; // +0x5e34..+0x5e3f
    SharedReuseDelaysOfItems m_SharedReuseDelaysOfItems; // +0x5e40..+0x5e77
    RWLock m_partyMatchingLock; // +0x5e78..+0x5eaf
    RecallInfo m_RecallInfo; // +0x5eb0..+0x5ec3
    CYieldLock m_RecallLock; // +0x5ec4..+0x5ed3
    CYieldLock m_InZoneLock; // +0x5ed4..+0x5ee3
    std::int32_t m_nAssociatedInstantZoneId; // +0x5ee4
    std::int32_t m_nAssociatedInstantZoneDBId; // +0x5ee8
    volatile std::int32_t m_nCreateInstantZoneCount; // +0x5eec..+0x5eef
    UserInzoneBindingMap m_mapInzoneBinding; // +0x5ef0..+0x5f0f
    UserInzoneBindingMap m_mapInzoneGroupBinding; // +0x5f10..+0x5f2f
    CYieldLock m_InzonePointLock; // +0x5f30..+0x5f3f
    UserInzonePointMap m_mapInzonePoint; // +0x5f40..+0x5f5f
    std::int32_t m_nPartyWaitingRoomLimitCount; // +0x5f60..+0x5f63
    volatile std::int32_t m_nStartPeaceZoneTime; // +0x5f64..+0x5f67
    std::int32_t m_nTotalPeaceZoneTime; // +0x5f68..+0x5f6b
    std::array<std::byte, 0x04> m_partialGap5f6cTo5f70; // +0x5f6c..+0x5f6f
    CVitality* m_pVitality; // +0x5f70..+0x5f77
    std::array<OnGoingZoneQuest, 20> m_OnGoingZoneQuest; // +0x5f78..+0x6017
    std::int32_t m_nStartCombatTime; // +0x6018..+0x601b
    std::int32_t m_nTotalCombatTime; // +0x601c..+0x601f
    UserStatData m_UserStatData; // +0x6020..+0x603f
    bool m_bWaitPrivateStoreCommit; // +0x6040
    std::array<std::byte, 0x03> m_alignment6041To6044; // +0x6041..+0x6043
    CYieldLock m_PrivateStoreLock; // +0x6044..+0x6053
public:
    bool m_bHadBotCheckItem; // +0x6054
private:
    std::array<std::byte, 0x03> m_alignment6055To6058; // +0x6055..+0x6057
public:
    CYieldLock m_BotCheckItemLock; // +0x6058..+0x6067
    bool m_bHadFakeItem; // +0x6068
private:
    std::array<std::byte, 0x03> m_alignment6069To606c; // +0x6069..+0x606b
public:
    CYieldLock m_HadFakeItemLock; // +0x606c..+0x607b
    bool m_bFakeItemState; // +0x607c
private:
    std::array<std::byte, 0x03> m_alignment607dTo6080; // +0x607d..+0x607f
public:
    CYieldLock m_FakeItemStateLock; // +0x6080..+0x608f
private:
    std::array<std::byte, 0x04> m_alignment6090To6094; // +0x6090..+0x6093
public:
    CrystalType m_CrystalLevel; // +0x6094
    CrystalType m_OriginalCrystalLevel; // +0x6098
    std::array<std::byte, 0x04> m_partialGap609cTo60a0; // +0x609c..+0x609f
    CAgathion* m_pAgathion; // +0x60a0..+0x60a7
    CubicActivationInfo* m_pAgathionActivationInfo; // +0x60a8..+0x60af
    volatile std::int32_t m_lAgathionFlag; // +0x60b0..+0x60b3
    CYieldLock m_TransformLock; // +0x60b4..+0x60c3
    std::array<std::byte, 0x04> m_alignment60c4To60c8; // +0x60c4..+0x60c7
    CTransform* m_transform; // +0x60c8..+0x60cf
private:
    std::int32_t m_nChangeSubjobFlag; // +0x60d0..+0x60d3
    bool m_bConvertingItem; // +0x60d4
    std::array<std::byte, 0x03> m_alignment60d5To60d8; // +0x60d5..+0x60d7
    std::multimap<int, int> m_mmapEquipDurationItem; // +0x60d8..+0x60f7
    std::int32_t m_nLastEnterBattleFieldTime; // +0x60f8..+0x60fb
    bool m_bCheckPVPTimerInBattleField; // +0x60fc
    std::array<std::byte, 0x03> m_alignment60fdTo6100; // +0x60fd..+0x60ff
    std::int32_t m_nSavedBattleFieldTime; // +0x6100..+0x6103
    CKillPVPPointChecker KillPVPPointChecker; // +0x6104..+0x610f
    std::array<std::int32_t, 3> m_EnchantBonusOptions; // +0x6110..+0x611b
    bool m_bDominionRenamed; // +0x611c
    bool m_bDominionTransformed; // +0x611d
    std::array<std::byte, 0x02> m_alignment611eTo6120; // +0x611e..+0x611f
    std::int32_t m_nBotReportPoint; // +0x6120..+0x6123
    std::int32_t m_nDominionRenamedID; // +0x6124..+0x6127
    std::array<std::byte, 0x08> m_partialGap6128To6130; // +0x6128..+0x612f
    std::int32_t m_nTimeActivatePledgeSkill; // +0x6130..+0x6133
    std::array<std::byte, 0x04> m_partialGap6134To6138; // +0x6134..+0x6137
public:
    CPostInfoForUser* m_pPostInfo; // +0x6138..+0x613f
private:
    UserRaidInfo m_RaidInfo; // +0x6140..+0x615f
    bool m_bKickByAgent; // +0x6160
    std::array<std::byte, 0x03> m_alignment6161To6164; // +0x6161..+0x6163
    std::uint32_t m_nTimeToDie; // +0x6164..+0x6167
    bool m_bDoubleSummon; // +0x6168
    std::array<std::byte, 0x03> m_alignment6169To616c; // +0x6169..+0x616b
    CYieldLock m_SummonedNpcLock; // +0x616c..+0x617b
    std::array<std::byte, 0x04> m_alignment617cTo6180; // +0x617c..+0x617f
    std::vector<std::int32_t,
        std::allocator<std::int32_t>> m_vSummonedNpcList; // +0x6180..+0x6197
    std::array<std::byte, 0x08> m_partialGap6198To61a0; // +0x6198..+0x619f
    std::int32_t m_nJobGroup; // +0x61a0..+0x61a3
    std::int32_t m_nPendingTeleportItemClassID; // +0x61a4..+0x61a7
    std::int64_t m_nPendingTeleportItemAmount; // +0x61a8..+0x61af
    std::uint32_t m_dwLastHTMLCmdTeleportTime; // +0x61b0..+0x61b3
    std::array<std::byte, 0x04> m_alignment61b4To61b8; // +0x61b4..+0x61b7
    RWLock m_PendingTeleportItemLock; // +0x61b8..+0x61ef
    CYieldLock m_PeriodicItemLock; // +0x61f0..+0x61ff
    std::int32_t m_periodicItemManageStart; // +0x6200..+0x6203
    volatile std::int32_t m_PeriodicItemExpired; // +0x6204..+0x6207
    UserPeriodicItemMapType m_periodicItemMap; // +0x6208..+0x6227
    CYieldLock m_PeriodicShapeShiftedItemLock; // +0x6228..+0x6237
    std::int32_t m_periodicShapeShiftedItemManageStart; // +0x6238..+0x623b
    std::array<std::byte, 0x04> m_alignment623cTo6240; // +0x623c..+0x623f
    UserPeriodicItemMapType m_periodicShapeShiftedItemMap; // +0x6240..+0x625f
    CYieldLock m_PremiumItemLock; // +0x6260..+0x626f
    std::map<std::int64_t, CPremiumItem*> m_mapPremiumItem; // +0x6270..+0x628f
public:
    std::int32_t m_nPetItemCount; // +0x6290..+0x6293
    std::int32_t m_nTradeModeTimeStamp; // +0x6294..+0x6297
protected:
    double m_dCrystalGradeBonus; // +0x6298..+0x629f
    bool m_bFlyMoving; // +0x62a0
    std::array<std::byte, 0x03> m_alignment62a1To62a4; // +0x62a1..+0x62a3
    FlyMovePair m_FlyMoveInfo; // +0x62a4..+0x62ab
    std::int32_t m_nPrevNode; // +0x62ac..+0x62af
    bool m_bAbnormalStatusLoaded; // +0x62b0
    std::array<std::byte, 0x03> m_partialGap62b1To62b4; // +0x62b1..+0x62b3
    std::int32_t m_nFlyMoveReadyCount; // +0x62b4..+0x62b7
    std::int32_t m_nAwakenClassID; // +0x62b8..+0x62bb
    std::int32_t m_nAwakenNpcID; // +0x62bc..+0x62bf
    ClassType m_nBaseClass; // +0x62c0..+0x62c3
    std::int32_t m_nAreaMessageTick; // +0x62c4..+0x62c7
public:
    bool m_bSkillDebugger; // +0x62c8
    bool m_bSkillDebugMode; // +0x62c9
    std::array<std::byte, 0x02> m_partialGap62caTo62cc; // +0x62ca..+0x62cb
    std::int32_t m_SceneId; // +0x62cc..+0x62cf
    std::int32_t m_SceneNpcId; // +0x62d0..+0x62d3
private:
    bool m_bLoadDBOneDayReward; // +0x62d4
    bool m_bSendOneDayReward; // +0x62d5
    std::array<std::byte, 0x02> m_alignment62d6To62d8; // +0x62d6..+0x62d7
    CYieldLock m_DailyMissionLock; // +0x62d8..+0x62e7
    std::int32_t m_CheckTimeDailyMission; // +0x62e8..+0x62eb
    std::array<std::byte, 0x04> m_alignment62ecTo62f0; // +0x62ec..+0x62ef
    std::multimap<E_DAILY_MISSION_TYPE, ICDailyMission*> m_DailyMissionMap; // +0x62f0..+0x630f
    std::multimap<E_DAILY_MISSION_TYPE, ICDailyMission*> m_CompleteDailyMissionMap; // +0x6310..+0x632f
    std::multimap<E_DAILY_MISSION_TYPE, ICDailyMission*> m_InvalidDailyMissionMap; // +0x6330..+0x634f
    std::multimap<int, ICDailyMission*> m_DailyMissionByDBIDMap; // +0x6350..+0x636f
    std::int32_t m_nPCTrapId; // +0x6370..+0x6373
    std::int32_t m_nPendingPCTrapClassId; // +0x6374..+0x6377
public:
    volatile std::int32_t m_lSaveCharacter; // +0x6378..+0x637b
    bool IsSaveCharacter() const { return m_lSaveCharacter == 1; }
    std::int32_t m_nCurCastleId; // +0x637c..+0x637f
private:
    std::int32_t m_nUpdateWindowChange; // +0x6380..+0x6383
    bool m_bParamHPCPMPChanged; // +0x6384
    std::array<std::byte, 0x03> m_alignment6385To6388; // +0x6385..+0x6387
    std::uint32_t m_nParamHPCPMPChanged; // +0x6388
    std::int32_t m_HennaId_premium; // +0x638c..+0x638f
    std::int32_t m_HennaExpireTime; // +0x6390..+0x6393
    std::int32_t m_periodicHennaManageStart; // +0x6394..+0x6397
    std::map<int, BlockListInfo> m_BlockListMap; // +0x6398..+0x63b7
    std::map<std::u16string, int, runtime::CStringLess<std::u16string>> m_BlockListMapRev; // +0x63b8..+0x63d7
    RWLock m_lockBlockListMap; // +0x63d8..+0x640f
public:
    volatile std::int32_t m_lUnreadMail; // +0x6410..+0x6413
private:
    std::int32_t m_nPreCP; // +0x6414..+0x6417
    std::int32_t m_nPreBaseCP; // +0x6418..+0x641b
    std::int32_t m_nPreHP; // +0x641c..+0x641f
    std::int32_t m_nPreBaseHP; // +0x6420..+0x6423
    std::int32_t m_nPreMP; // +0x6424..+0x6427
    std::int32_t m_nPreBaseMP; // +0x6428..+0x642b
    std::int8_t m_nPreLevel; // +0x642c
    std::array<std::byte, 0x01> m_alignment642dTo642e; // +0x642d
    std::int16_t m_nPreClass; // +0x642e..+0x642f
    std::array<std::byte, 0x04> m_partialGap6430To6434; // +0x6430..+0x6433
    std::int32_t m_nPreVitalPoint; // +0x6434..+0x6437
public:
    std::int64_t nAttributeCount; // +0x6438..+0x643f
    std::int64_t nAttributeFailCount; // +0x6440..+0x6447
    bool m_bShowClickPos; // +0x6448
    bool m_ShowItemInfo; // +0x6449
    bool m_ShowSkillInfo; // +0x644a
    bool m_ShowEquipItemInfo; // +0x644b
    // Current path_finder writes +0x644c and User initialization clears it.
    // matching CodeView supplies the original physical identifier.
    bool m_bIsPathFinder; // +0x644c
    bool m_bPartyBuilder; // +0x644d
private:
    std::array<std::byte, 0x02> m_alignment644eTo6450; // +0x644e..+0x644f
    std::list<ItemRefund*> m_ItemRefunds; // +0x6450..+0x645f
    std::array<std::byte, 0x08> m_partialGap6460To6468; // +0x6460..+0x6467
    volatile std::int32_t m_nAtomicItemJob; // +0x6468..+0x646b
public:
    bool m_ItemMasterBuilder; // +0x646c
private:
    std::array<std::byte, 0x03> m_alignment646dTo6470; // +0x646d..+0x646f
public:
    volatile std::int32_t m_PendingEquipItemID; // +0x6470..+0x6473
    bool m_bOpenedNpcStoreUI; // +0x6474
    bool m_bShowPacketSend; // +0x6475
    bool m_bShowItemDropProb; // +0x6476
private:
    bool m_bPetLinked; // +0x6477
    CYieldLock m_SummonReservedLock; // +0x6478..+0x6487
    ::std::vector<SummonReservedInfo> m_vSummonReserved; // +0x6488..+0x649f
    std::array<std::byte, 0x08> m_partialGap64a0To64a8; // +0x64a0..+0x64a7
    volatile std::int32_t m_bNeedUpdateSummon; // +0x64a8..+0x64ab
    std::array<std::byte, 0x04> m_partialGap64acTo64b0; // +0x64ac..+0x64af
    FVector m_vUserMapDensityPosition; // +0x64b0..+0x64c7
    bool m_bBeautyShop; // +0x64c8
    std::array<std::byte, 0x03> m_alignment64c9To64cc; // +0x64c9..+0x64cb
public:
    CYieldLock m_ReturnLock; // +0x64cc..+0x64db
private:
    std::array<std::byte, 0x04> m_alignment64dcTo64e0; // +0x64dc..+0x64df
public:
    using ReturnSkillMap = std::map<int, FVector>;
    ReturnSkillMap m_ReturnSkillPosMap; // +0x64e0..+0x64ff
    using ReturnTimeMap = std::map<int, int>;
    ReturnTimeMap m_ReturnTimeSkillMap; // +0x6500..+0x651f
private:
    bool m_bIsBuyingProduct; // +0x6520
    std::array<std::byte, 0x07> m_partialGap6521To6528; // +0x6521..+0x6527
public:
    std::int64_t m_GamePoint; // +0x6528..+0x652f
    char m_cLectureMark; // +0x6530
private:
    std::array<std::byte, 0x03> m_alignment6531To6534; // +0x6531..+0x6533
    RestartPoint m_RestartPoint; // +0x6534..+0x6537
    std::int32_t m_RestartItemId; // +0x6538..+0x653b
    std::int32_t m_RestartSkillId; // +0x653c
    std::int32_t m_RestartSkillLevel; // +0x6540
    bool m_bSoulOfPhoenix; // +0x6544
    std::array<std::byte, 0x03> m_alignment6545To6548; // +0x6545..+0x6547
    std::int32_t m_nReuseDelayInitTime; // +0x6548
    std::int32_t m_nGiftDayIn7Days; // +0x654c
    CYieldLock m_CommissionHistoryLock; // +0x6550..+0x655f
    std::map<int, CommissionHistory> m_CommissionHistory; // +0x6560..+0x657f
public:
    volatile std::int32_t m_CommissionDelay; // +0x6580..+0x6583
    volatile std::int32_t m_CommissionRelease; // +0x6584..+0x6587
    volatile std::int32_t m_CommissionExpired; // +0x6588..+0x658b
    volatile std::int32_t m_BeautyDelay; // +0x658c..+0x658f
    volatile std::int32_t m_BeautyRelease; // +0x6590..+0x6593
private:
    bool m_bDoingGoodsInventoryJob; // +0x6594
    std::array<std::byte, 0x03> m_alignment6595To6598; // +0x6595..+0x6597
    std::map<std::int64_t, CUserNewGoods> m_mapNewGoods; // +0x6598..+0x65b7
    std::array<std::byte, 0x04> m_alignment65b8To65bc; // +0x65b8..+0x65bb
    std::int32_t m_nTimeToGiveKarmaPointToKiller; // +0x65bc
    CYieldLock m_questListLock; // +0x65c0..+0x65cf
    std::map<int, UserQuestProgress> m_acceptedQuest; // +0x65d0..+0x65ef
    std::int32_t m_questError; // +0x65f0..+0x65f3
    std::array<std::byte, 0x04> m_alignment65f4To65f8; // +0x65f4..+0x65f7
    EventMonitor m_EventMonitor; // +0x65f8..+0x6647
    std::array<std::byte, 0x08> m_partialGap6648To6650; // +0x6648..+0x664f
    bool m_bEinhasadeTransformed; // +0x6650
    std::array<std::byte, 0x03> m_partialGap6651To6654; // +0x6651..+0x6653
    std::int32_t m_nEinhasadeRenameId; // +0x6654..+0x6657
public:
    std::int32_t m_nRestartItemCount; // +0x6658..+0x665b
private:
    bool m_bScreenNotice; // +0x665c
    std::array<std::byte, 0x03> m_partialGap665dTo6660; // +0x665d..+0x665f
    std::int32_t m_nSpeedHackSuspectCount; // +0x6660..+0x6663
    char16_t m_sChatName[25]; // +0x6664..+0x6695
    std::array<std::byte, 0x02> m_partialGap6696To6698; // +0x6696..+0x6697
    std::int32_t m_nDoingEventCampaignId; // +0x6698
    std::int32_t m_nDoingEventCampaignStepId; // +0x669c
    std::int32_t m_nDoingEventCampaignGoalGroup; // +0x66a0
    volatile std::int32_t m_nKalieToken; // +0x66a4
    volatile std::int32_t m_nTokenRewarding; // +0x66a8
    bool m_bKalieRewardForEveryone; // +0x66ac
    std::array<std::byte, 0x03> m_alignment66adTo66b0; // +0x66ad..+0x66af
public:
    std::int32_t m_nKalieBuff; // +0x66b0..+0x66b3
private:
    volatile std::int32_t m_nBalthusTokenCount; // +0x66b4..+0x66b7
    volatile std::int32_t m_nBalthusTokenRewarding; // +0x66b8..+0x66bb
    bool m_bBalthusTokenReady; // +0x66bc
    std::array<std::byte, 0x03> m_alignment66bdTo66c0; // +0x66bd..+0x66bf
public:
    ExDropInfoMap m_ExDropMap; // +0x66c0..+0x66df
    std::int32_t m_tmExDropItemGetTime; // +0x66e0..+0x66e3
    // Current BuilderCmd_despawn_Pet is a direct free-function consumer.
    volatile std::int32_t m_nPetSpawnCommand; // +0x66e4..+0x66e7
private:
    volatile std::int32_t m_nPetSpawnStep; // +0x66e8..+0x66eb
    volatile std::int32_t m_nPetDespawnSteps; // +0x66ec..+0x66ef
    std::uint32_t m_nPetCollarSID; // +0x66f0..+0x66f3
    bool m_bNewlySummon; // +0x66f4
    std::array<std::byte, 0x03> m_alignment66f5To66f8; // +0x66f5..+0x66f7
    std::uint32_t m_dRaidBlockMsgInterval; // +0x66f8..+0x66fb
    char16_t m_sRaidName[35]; // +0x66fc..+0x6741
    bool m_bSendShowCradCollectSystem; // +0x6742
    std::byte m_alignment6743To6744; // +0x6743
public:
    std::int32_t m_nSendShowCardCollectSystemTime; // +0x6744..+0x6747
private:
    std::int32_t m_nReloadPet; // +0x6748..+0x674b
public:
    std::int32_t m_TrainingEndTime; // +0x674c..+0x674f
private:
public:
    std::array<std::int32_t, 8> m_nOrgSpeed; // +0x6750..+0x676f
    double m_dAttackSpeedModifier; // +0x6770..+0x6777
    double m_dMoveSpeedModifier; // +0x6778..+0x677f
    double m_dCollisionRadius; // +0x6780..+0x6787
    double m_dCollisionHeight; // +0x6788..+0x678f
    FVector m_MyPos; // +0x6790..+0x67a7
    FVector m_InformingPos; // +0x67a8..+0x67bf
    std::int32_t m_nVehicleID; // +0x67c0..+0x67c3
    std::int32_t m_nMyStatus; // +0x67c4..+0x67c7
    std::int32_t m_nCriminalRate; // +0x67c8..+0x67cb
    std::int32_t m_pledgePower; // +0x67cc..+0x67cf
    char m_cEventMatchTeamID; // +0x67d0
    std::byte m_alignment67d1To67d2; // +0x67d1
    std::int16_t m_snEnchant; // +0x67d2..+0x67d3
    std::int16_t m_sBonusCount; // +0x67d4..+0x67d5
    std::array<std::byte, 0x02> m_alignment67d6To67d8; // +0x67d6..+0x67d7
    std::int32_t m_nVitalPoint; // +0x67d8..+0x67db
    std::int32_t m_nSocialClass; // +0x67dc..+0x67df
    std::int32_t m_nCursedWeaponLevel; // +0x67e0..+0x67e3
    std::int32_t m_nTransformId; // +0x67e4..+0x67e7
    char m_cPVPRestrainStatus; // +0x67e8
    char m_cPledgeGameMemberFlag; // +0x67e9
    char m_cShowPartyWantedMessage; // +0x67ea
    std::byte m_alignment67ebTo67ec; // +0x67eb
    std::int32_t m_nDominionSiegeID; // +0x67ec..+0x67ef
    char m_cDominionIsRenamed; // +0x67f0
    std::array<std::byte, 0x03> m_alignment67f1To67f4; // +0x67f1..+0x67f3
    std::int32_t m_nDominionRenamedId; // +0x67f4..+0x67f7
    std::array<std::byte, 0x04> m_partialGap67f8To67fc; // +0x67f8..+0x67fb
private:
public:
    volatile std::int32_t m_nAbilityClass; // +0x67fc..+0x67ff
private:
    CYieldLock m_AcquiredAbilitySkillsLock; // +0x6800..+0x680f
    ::std::map<int, AcquiredPotentialSkillInfo> m_AcquiredAbilitySkills; // +0x6810..+0x682f
    ::std::map<int, AcquiredSkillInfo> m_AcquiredHonorsSkills; // +0x6830..+0x684f
    CYieldLock m_AcquiredHonorsSkillsLock; // +0x6850..+0x685f
    CYieldLock m_QuestSaveLock; // +0x6860..+0x686f
    bool m_bWaitForSavingQuest; // +0x6870
    std::array<std::byte, 0x03> m_alignment6871To6874; // +0x6871..+0x6873
    volatile std::int32_t m_bNeedSaveQuest; // +0x6874..+0x6877
    protocol::S_UserInfo_EquipInfo m_equipInfo; // +0x6878..+0x6d47
    std::array<bool, 13> m_bNeedUpdatePacketBitsStat; // +0x6d48..+0x6d54
    std::array<std::byte, 0x03> m_alignment6d55To6d58; // +0x6d55..+0x6d57
    CVariableBitSet m_NeedUpdatePacketBitsStat; // +0x6d58..+0x6d67
    std::uint64_t m_nParamChangedStat; // +0x6d68..+0x6d6f
    std::int32_t m_tWorldSayTimeStamp; // +0x6d70..+0x6d73
    volatile std::int32_t m_nUsedWorldChatPoint; // +0x6d74..+0x6d77
    std::int32_t m_nNowSelectedQuestZone; // +0x6d78..+0x6d7b
    CYieldLock m_AlchemySkillsLock[2]; // +0x6d7c..+0x6d9b
    std::array<std::byte, 0x04> m_alignment6d9cTo6da0; // +0x6d9c..+0x6d9f
    ::std::map<int, AcquiredAlchemySkillInfo> m_AlchemySkills[2]; // +0x6da0..+0x6ddf
    std::int32_t m_nAlchemyRollySkillId; // +0x6de0..+0x6de3
    std::int32_t m_nAlchemyRollyLev; // +0x6de4..+0x6de7
    std::array<std::byte, 0x04> m_partialGap6de8To6dec; // +0x6de8..+0x6deb
    std::array<std::int32_t, 142> m_hAcquireSkillListSendClassByType; // +0x6dec..+0x7023
public:
    std::int32_t m_nSpouseDbID; // +0x7024..+0x7027
private:
    std::int64_t m_nCaptchaConfirmTransactionId; // +0x7028..+0x702f
    std::int32_t m_tCaptchaConfirmStartTime; // +0x7030..+0x7033
    volatile std::int32_t m_CaptchaConfirmTimes; // +0x7034..+0x7037
    std::int32_t m_nCaptchaConfirmSec; // +0x7038..+0x703b
    std::int32_t m_tCaptchaDelayStartTime; // +0x703c..+0x703f
    bool m_bIsDBFactionInfoLoaded; // +0x7040
    bool m_bIsSendUserInfoCompleted; // +0x7041
    bool m_bIsSendFactionInfoCompleted; // +0x7042
    std::byte m_alignment7043To7044; // +0x7043
    std::int32_t nChatLimitValue; // +0x7044..+0x7047
    std::int32_t nLastChatTime; // +0x7048..+0x704b
    std::array<std::int32_t, PledgeBonus::MAX_TIME_STAMP> tPledgeBonusTimeStamp; // +0x704c..+0x7057
    MonsterBook::UserMonsterBook m_monsterBook; // +0x7058..+0x869f
    std::int32_t m_nFieldCycleID; // +0x86a0..+0x86a3
    std::int32_t m_nFieldCycleIDOfQuest; // +0x86a4..+0x86a7
    std::int32_t m_nFieldCycleQuestID; // +0x86a8..+0x86ab
    bool m_bIsNCGuardBotDetected; // +0x86ac
    std::array<std::byte, 0x03> m_alignment86adTo86b0; // +0x86ad..+0x86af
public:
    std::int32_t m_nPrevTalkSid; // +0x86b0
    std::int32_t m_nPrevTalkTick; // +0x86b4
private:
    friend class CompleteMission_MissionCondition;
    CardUpDownGame::UserGame m_UpDownGame; // +0x86b8..+0x8737
    UserPledgeMission m_UserPledgeMission; // +0x8738..+0x8c7f
    volatile std::int32_t m_lPackageItemPacketJobFlag; // +0x8c80..+0x8c83
    std::int32_t m_lTotalPackageItemCount; // +0x8c84..+0x8c87
    std::int32_t m_lCurrentReceivedPackageItemCount; // +0x8c88..+0x8c8b
    std::array<std::byte, 0x04> m_alignment8c8cTo8c90; // +0x8c8c..+0x8c8f
    std::vector<CPacketHelperEx<20480>> m_PackageItemPackets; // +0x8c90..+0x8caf
    CYieldLock m_PackageItemPacketsLock; // +0x8cb0..+0x8cbf
public:
    bool m_bBowEquiped; // +0x8cc0
    bool m_bCrossBowEquiped; // +0x8cc1
    bool m_bCheckBrooch; // +0x8cc2
    bool m_bCheckRBrace; // +0x8cc3
    bool m_bCheckLBracelet; // +0x8cc4
    bool m_bCheckArtifactbook; // +0x8cc5
private:
    std::array<std::byte, 0x02> m_alignment8cc6To8cc8; // +0x8cc6..+0x8cc7
    CYieldLock m_InteractRejectTypesLock; // +0x8cc8..+0x8cd7
    std::array<std::int32_t, 2> m_InteractRejectTypes; // +0x8cd8..+0x8cdf
    volatile std::int32_t m_ArtifactSlotChanged; // +0x8ce0..+0x8ce3
    std::int32_t m_nFullArtifactSlotGroupCount; // +0x8ce4..+0x8ce7

    static std::set<int> s_NewCharIDs;
    static CIOCriticalSection s_NewCharIDSect;
};

using UserDatabaseIdMap = CObjectMap<User, 8192, 5, RWLock, std::CDualKey<int, int>>;
using UserAccountDatabaseIdMap = CObjectMap<User, 8192, 5, RWLock, int>;
using UserObjectMap = CObjectMap<User, 8192, 5, RWLock, int>;
// Current g_GMCharMap proves an 0x08-byte checker prefix, 8192 target
// xmap nodes at 0x20 each, then 8192 RWLocks at 0x38 each.

using UserCharNameMap = CObjectByIntKeyStringMap<User>;
using NewUserCharNameMap = CObjectStringMap<User>;


extern UserObjectMap g_UserMap;
extern UserDatabaseIdMap g_UserDBIDMap;
extern UserAccountDatabaseIdMap g_UserAccountDBIDMap;
extern UserObjectMap g_GMCharMap;
extern UserCharNameMap g_UserCharNameMap;
extern NewUserCharNameMap g_NewUserCharNameMap;
CSPointer<User> GetUserFromNameMap(int worldId, std::u16string_view name);
using BuilderAccountList = std::vector<char*>;
extern BuilderAccountList g_mapBuilderAccount;
extern bool g_bLuckyTest;
// Current User.obj BOT policy carriers. CConfig::LoadSettingSection writes
// these exact addresses and User/BOT-check paths consume them. The matching
// donor MAP supplies the original names and User.obj ownership.
extern bool g_bBOTMessage;
extern bool g_bBOTLog;
extern int g_nBOTCheckTime;
extern int g_nBOTCheckWaitTimeLimit;
// Current User.obj storage 0x015e7ada starts at 1. Writer
// BuilderCmd::log_vp_pzone@0x0050e4a0, consumer OnAreaCheckForEveryTick.
extern bool g_bLogVitalInPeaceZone;


// The current L2Server image has no callable symbol for either traversal.
// Every use expands in its caller as: outer macro guard, one snapshot from
// User, null rejection, per-entry "in_code" guard, then the caller body.
// Keep this as a statement macro: return/continue/break in body must keep
// caller/loop meaning and therefore cannot be hidden in a callback helper.
//
#define L2_FOR_ALL_SUMMON_FUNCTION_GUARD(guardPrefix)                                                                       \
    L2_GUARD_PREFIXED_FUNCTION(guardPrefix, l2ForAllSummonFunctionName, l2ForAllSummonGuard)

#define L2_FOR_ALL_SUMMON_IN_LIST_IMPL(userExpression, summonVariable, snapshotMethod, guardPrefix, ...)                    \
    do {                                                                                                                    \
        L2_FOR_ALL_SUMMON_FUNCTION_GUARD(guardPrefix);                                                                      \
        std::vector<CSummon*> l2ForAllSummonSnapshot;                                                                       \
        (userExpression)->snapshotMethod(l2ForAllSummonSnapshot);                                                           \
        for (CSummon* const summonVariable : l2ForAllSummonSnapshot) {                                                      \
            if ((summonVariable) == nullptr) {                                                                              \
                continue;                                                                                                   \
            }                                                                                                               \
            GuardHelper l2ForAllSummonInCodeGuard(L"in_code", CThreadManager::GetThreadID());                               \
            __VA_ARGS__                                                                                                     \
        }                                                                                                                   \
    } while (false)

#define FOR_ALL_SUMMON_IN_SUMMON_LIST(userExpression, summonVariable, ...)                                                  \
    L2_FOR_ALL_SUMMON_IN_LIST_IMPL(                                                                                         \
        userExpression, summonVariable, GetAllSummon, L"FOR_ALL_SUMMON_IN_SUMMON_LIST <=== ", __VA_ARGS__                   \
    )

#define FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(userExpression, summonVariable, ...)                                              \
    L2_FOR_ALL_SUMMON_IN_LIST_IMPL(                                                                                         \
        userExpression, summonVariable, GetAllSummonAndPet, L"FOR_ALL_SUMMON_PET_IN_SUMMON_LIST <=== ", __VA_ARGS__         \
    )
