#pragma once

#include <cstdint>

// Original Protocol/L2Constants.h TU-local owners. The pinned current PE
// contains the same complete 0xc0-byte table in default.obj, Mentor.obj,
// SignBoard.obj, User.obj and UserBroadcast.obj. The User.obj image is
// 0x011f9ec0..0x011f9f7f with SHA-256
// e67f7c6b4dcb63b8614995ff50aae4647b286b3b500f4b3196c3c0815fdccce8.
const std::int32_t NicknameColorPreset[2][2] = {
    {0, 9},
    {10, 13},
};

const std::int32_t NicknameColorTable[16][3] = {
    {255, 147, 147},
    {255, 74, 125},
    {255, 251, 153},
    {240, 155, 253},
    {147, 93, 255},
    {162, 255, 0},
    {0, 170, 164},
    {175, 152, 120},
    {158, 103, 75},
    {155, 155, 155},
    {255, 204, 0},
    {255, 160, 32},
    {255, 104, 255},
    {255, 0, 0},
    {240, 214, 54},
    {162, 249, 236},
};

// Keep this a plain namespace-scope const, not extern/inline/constexpr. The
// current image has five separate writable-section cells and no writes. Its
// value is the target's original byte-pack of row 15.
const std::int32_t DEFAULT_NICKNAME_COLOR =
    NicknameColorTable[15][0] | (NicknameColorTable[15][1] << 8) | (NicknameColorTable[15][2] << 16);


namespace ClientINI {

enum InteractType : std::int32_t {
    IT_PARTY_INVITE = 0,
    IT_FRIEND_REQUEST = 1,
    IT_MAX = 2,
};

} // namespace ClientINI


namespace Alchemy_LollyCube {

// Current L2Server 0x00b03818. This remains the original inline protocol
// owner identified by the exact project/PDB structure. User::TryRollyCube is
// the current compiled call site.
inline int CalcRewardItemCountByPushItem(
    int* itemCount, std::int64_t* baseItemDefaultPrice, std::int64_t* totalDefaultPrice, std::int64_t* rewardItemCount
) {
    if (*totalDefaultPrice > 0 && *itemCount > 0 && *itemCount < 5 && *baseItemDefaultPrice != 0) {
        if (*itemCount < 3) {
            *rewardItemCount =
                static_cast<std::int64_t>(static_cast<double>(*totalDefaultPrice / *baseItemDefaultPrice) * 0.3);
        } else if (*itemCount > 2) {
            *rewardItemCount =
                static_cast<std::int64_t>(static_cast<double>(*totalDefaultPrice / *baseItemDefaultPrice) * 0.5);
        }
        if (*rewardItemCount < 1) {
            *rewardItemCount = 0;
        }
        return 1;
    }
    return 0;
}

} // namespace Alchemy_LollyCube

enum ClassType : std::int32_t {
    CT_NONE = -1,
    CT_FIGHTER = 0,
    CT_WARRIOR = 1,
    CT_GLADIATOR = 2,
    CT_WARLORD = 3,
    CT_KNIGHT = 4,
    CT_PALADIN = 5,
    CT_DARK_AVENGER = 6,
    CT_ROGUE = 7,
    CT_TREASURE_HUNTER = 8,
    CT_HAWKEYE = 9,
    CT_MAGE = 10,
    CT_WIZARD = 11,
    CT_SORCERER = 12,
    CT_NECROMANCER = 13,
    CT_WARLOCK = 14,
    CT_CLERIC = 15,
    CT_BISHOP = 16,
    CT_PROPHET = 17,
    CT_ELVEN_FIGHTER = 18,
    CT_ELVEN_KNIGHT = 19,
    CT_TEMPLE_KNIGHT = 20,
    CT_SWORDSINGER = 21,
    CT_ELVEN_SCOUT = 22,
    CT_PLAIN_WALKER = 23,
    CT_SILVER_RANGER = 24,
    CT_ELVEN_MAGE = 25,
    CT_ELVEN_WIZARD = 26,
    CT_SPELLSINGER = 27,
    CT_ELEMENTAL_SUMMONER = 28,
    CT_ORACLE = 29,
    CT_ELDER = 30,
    CT_DARK_FIGHTER = 31,
    CT_PALUS_KNIGHT = 32,
    CT_SHILLIEN_KNIGHT = 33,
    CT_BLADEDANCER = 34,
    CT_ASSASIN = 35,
    CT_ABYSS_WALKER = 36,
    CT_PHANTOM_RANGER = 37,
    CT_DARK_MAGE = 38,
    CT_DARK_WIZARD = 39,
    CT_SPELLHOWLER = 40,
    CT_PHANTOM_SUMMONER = 41,
    CT_SHILLIEN_ORACLE = 42,
    CT_SHILLIEN_ELDER = 43,
    CT_ORC_FIGHTER = 44,
    CT_ORC_RAIDER = 45,
    CT_DESTROYER = 46,
    CT_ORC_MONK = 47,
    CT_TYRANT = 48,
    CT_ORC_MAGE = 49,
    CT_ORC_SHAMAN = 50,
    CT_OVERLORD = 51,
    CT_WARCRYER = 52,
    CT_DWARVEN_FIGHTER = 53,
    CT_SCAVENGER = 54,
    CT_BOUNTY_HUNTER = 55,
    CT_ARTISAN = 56,
    CT_WARSMITH = 57,
    CT_SHAMAN = 58,
    CT_MONK = 59,
    CT_COLLECTOR = 60,
    CT_MERCHANT = 61,
    CT_TRAINER = 62,
    CT_TELEPORTER = 63,
    CT_WAREHOUSE_KEEPER = 64,
    CT_PACKAGE_KEEPER = 65,
    CT_NEWBIE_GUIDE = 66,
    CT_GUARD = 67,
    CT_CITIZEN = 68,
    CT_GUILD_MASTER = 69,
    CT_GUILD_COACH = 70,
    CT_SUMMON = 71,
    CT_OBJET = 72,
    CT_CHAMBERLAIN = 73,
    CT_TEMPTRAINER = 74,
    CT_HOLYTHING = 75,
    CT_CASTLE_GATE = 76,
    CT_COMMANDER = 77,
    CT_HOUSE_MASTER = 78,
    CT_BLACKSMITH = 79,
    CT_PET = 80,
    CT_BOSS = 81,
    CT_ZZOLDAGU = 82,
    CT_TREASURE = 83,
    CT_MONRACE = 84,
    CT_MONRACEKEEPER = 85,
    CT_XMASTREE = 86,
    CT_SIEGE_UNIT = 87,
    CT_DUELIST = 88,
    CT_DREADNOUGHT = 89,
    CT_PHOENIX_KNIGHT = 90,
    CT_HELL_KNIGHT = 91,
    CT_SAGITTARIUS = 92,
    CT_ADVENTURER = 93,
    CT_ARCHMAGE = 94,
    CT_SOULTAKER = 95,
    CT_ARCANA_LORD = 96,
    CT_CARDINAL = 97,
    CT_HIEROPHANT = 98,
    CT_EVAS_TEMPLAR = 99,
    CT_SWORD_MUSE = 100,
    CT_WIND_RIDER = 101,
    CT_MOONLIGHT_SENTINEL = 102,
    CT_MYSTIC_MUSE = 103,
    CT_ELEMENTAL_MASTER = 104,
    CT_EVAS_SAINT = 105,
    CT_SHILIEN_TEMPLAR = 106,
    CT_SPECTRAL_DANCER = 107,
    CT_GHOST_HUNTER = 108,
    CT_GHOST_SENTINEL = 109,
    CT_STORM_SCREAMER = 110,
    CT_SPECTRAL_MASTER = 111,
    CT_SHILIEN_SAINT = 112,
    CT_TITAN = 113,
    CT_GRAND_KHAVATARI = 114,
    CT_DOMINATOR = 115,
    CT_DOOMCRYER = 116,
    CT_FORTUNE_SEEKER = 117,
    CT_MAESTRO = 118,
    CT_WORLD_TRAP = 119,
    CT_PC_TRAP = 120,
    CT_DOPPELGANGER = 121,
    CT_SIEGE_ATTACKER = 122,
    CT_KAMAEL_M_SOLDIER = 123,
    CT_KAMAEL_F_SOLDIER = 124,
    CT_TROOPER = 125,
    CT_WARDER = 126,
    CT_BERSERKER = 127,
    CT_M_SOUL_BREAKER = 128,
    CT_F_SOUL_BREAKER = 129,
    CT_ARBALESTER = 130,
    CT_DOOMBRINGER = 131,
    CT_M_SOUL_HOUND = 132,
    CT_F_SOUL_HOUND = 133,
    CT_TRICKSTER = 134,
    CT_INSPECTOR = 135,
    CT_JUDICATOR = 136,
    CT_OWNTHING = 137,
    CT_COLLECTION = 138,
    CT_SIGEL_KNIGHT = 139,
    CT_TIR_WARRIOR = 140,
    CT_OTHEL_ROGUE = 141,
    CT_YR_ARCHER = 142,
    CT_FEOH_WIZARD = 143,
    CT_IS_ENCHANTER = 144,
    CT_WYNN_SUMMONER = 145,
    CT_EOLH_HEALER = 146,
    CT_DWARVEN_MAGE = 147,
    CT_SIGEL_PHOENIX_KNIGHT = 148,
    CT_SIGEL_HELL_KNIGHT = 149,
    CT_SIGEL_EVAS_TEMPLAR = 150,
    CT_SIGEL_SHILIEN_TEMPLAR = 151,
    CT_TIR_DUELIST = 152,
    CT_TIR_DREADNOUGHT = 153,
    CT_TIR_TITAN = 154,
    CT_TIR_GRAND_KHAVATARI = 155,
    CT_TIR_MAESTRO = 156,
    CT_TIR_DOOMBRINGER = 157,
    CT_OTHEL_ADVENTURER = 158,
    CT_OTHEL_WIND_RIDER = 159,
    CT_OTHEL_GHOST_HUNTER = 160,
    CT_OTHEL_FORTUNE_SEEKER = 161,
    CT_YR_SAGITTARIUS = 162,
    CT_YR_MOONLIGHT_SENTINEL = 163,
    CT_YR_GHOST_SENTINEL = 164,
    CT_YR_TRICKSTER = 165,
    CT_FEOH_ARCHMAGE = 166,
    CT_FEOH_SOULTAKER = 167,
    CT_FEOH_MYSTIC_MUSE = 168,
    CT_FEOH_STORM_SCREAMER = 169,
    CT_FEOH_SOUL_HOUND = 170,
    CT_IS_HIEROPHANT = 171,
    CT_IS_SWORD_MUSE = 172,
    CT_IS_SPECTRAL_DANCER = 173,
    CT_IS_DOMINATOR = 174,
    CT_IS_DOOMCRYER = 175,
    CT_WYNN_ARCANA_LORD = 176,
    CT_WYNN_ELEMENTAL_MASTER = 177,
    CT_WYNN_SPECTRAL_MASTER = 178,
    CT_EOLH_CARDINAL = 179,
    CT_EOLH_EVAS_SAINT = 180,
    CT_EOLH_SHILIEN_SAINT = 181,
    CT_ERTHEIA_FIGHTER = 182,
    CT_ERTHEIA_WIZARD = 183,
    CT_MARAUDER = 184,
    CT_MENACE = 185,
    CT_LENKER = 186,
    CT_STORM_MENACE = 187,
    CT_STRATO_LENKER = 188,
    CT_SAYHAS_SEER = 189,
    CT_MASS_LENKER = 190,
    CT_SEER = 191,
    CT_MAX = 192,
};


enum EtcItemType : std::int32_t {
    EIT_NONE = 0,
    EIT_SCROLL = 1,
    EIT_ARROW = 2,
    EIT_POTION = 3,
    EIT_SPELLBOOK = 4,
    EIT_RECIPE = 5,
    EIT_MATERIAL = 6,
    EIT_PET_COLLAR = 7,
    EIT_CASTLE_GUARD = 8,
    EIT_DYE = 9,
    EIT_SEED = 10,
    EIT_SEED2 = 11,
    EIT_HARVEST = 12,
    EIT_LOTTO = 13,
    EIT_RACE_TICKET = 14,
    EIT_TICKET_OF_LORD = 15,
    EIT_LURE = 16,
    EIT_CROP = 17,
    EIT_MATURECROP = 18,
    EIT_ENCHT_WP = 19,
    EIT_ENCHT_AM = 20,
    EIT_BLESS_ENCHT_WP = 21,
    EIT_BLESS_ENCHT_AM = 22,
    EIT_COUPON = 23,
    EIT_ELIXIR = 24,
    EIT_ENCHT_ATTR = 25,
    EIT_ENCHT_ATTR_CURSED = 26,
    EIT_BOLT = 27,
    EIT_INC_PROP_ENCHT_WP = 28,
    EIT_INC_PROP_ENCHT_AM = 29,
    EIT_CRYSTAL_ENCHANT_AM = 30,
    EIT_CRYSTAL_ENCHANT_WP = 31,
    EIT_ANCIENT_CRYSTAL_ENCHANT_AM = 32,
    EIT_ANCIENT_CRYSTAL_ENCHANT_WP = 33,
    EIT_RUNE = 34,
    EIT_RUNE_SELECT = 35,
    EIT_TELEPORTBOOKMARK = 36,
    EIT_CHANGE_ATTR = 37,
    EIT_SOULSHOT = 38,
    EIT_SHAPE_SHIFTING_WP = 39,
    EIT_BLESS_SHAPE_SHIFTING_WP = 40,
    EIT_SHAPE_SHIFTING_WP_FIXED = 41,
    EIT_SHAPE_SHIFTING_AM = 42,
    EIT_BLESS_SHAPE_SHIFTING_AM = 43,
    EIT_SHAPE_SHIFTING_AM_FIXED = 44,
    EIT_SHAPE_SHIFTING_HAIRACC = 45,
    EIT_BLESS_SHAPE_SHIFTING_HAIRACC = 46,
    EIT_SHAPE_SHIFTING_HAIRACC_FIXED = 47,
    EIT_RESTORE_SHAPE_SHIFTING_WP = 48,
    EIT_RESTORE_SHAPE_SHIFTING_AM = 49,
    EIT_RESTORE_SHAPE_SHIFTING_HAIRACC = 50,
    EIT_RESTORE_SHAPE_SHIFTING_ALLITEM = 51,
    EIT_BLESS_INC_PROP_ENCHT_WP = 52,
    EIT_BLESS_INC_PROP_ENCHT_AM = 53,
    EIT_CARD_EVENT = 54,
    EIT_SHAPE_SHIFTING_ALLITEM_FIXED = 55,
    EIT_MULTI_ENCHT_WP = 56,
    EIT_MULTI_ENCHT_AM = 57,
    EIT_MULTI_INC_PROB_ENCHT_WP = 58,
    EIT_MULTI_INC_PROB_ENCHT_AM = 59,
    EIT_ENSOUL_STONE = 60,
    EIT_NICK_COLOR_OLD = 61,
    EIT_NICK_COLOR_NEW = 62,
    EIT_ENCHT_AG = 63,
    EIT_BLESS_ENCHT_AG = 64,
    EIT_MULTI_ENCHT_AG = 65,
    EIT_ANCIENT_CRYSTAL_ENCHANT_AG = 66,
    EIT_INC_PROP_ENCHT_AG = 67,
    EIT_BLESS_INC_PROP_ENCHT_AG = 68,
    EIT_MULTI_INC_PROB_ENCHT_AG = 69,
    EIT_LOCK_ITEM = 70,
    EIT_UNLOCK_ITEM = 71,
    EIT_BULLET = 72,
    EIT_MAX = 73,
};


namespace HTML {

enum DisplayType : std::int32_t {
    LAUNCH = 0,
    LINK = 1,
    MAX = 2,
};

} // namespace HTML

namespace XmasWishCardResult {

enum Enum : std::int32_t {
    XWCR_SUCCESS = 0,
    XWCR_INVENTORY_FULL = 1,
    XWCR_ALREADY_RECEIVED = 2,
    XWCR_NOT_RECEVIED = 3,
    XWCR_UNKNOWN = 4,
};

} // namespace XmasWishCardResult

namespace LuckyGameSystem {

enum BettingType : std::int32_t {
    BT_NONE = 0,
    BT_COMMON = 1,
    BT_PREMIUM = 2,
    BT_NEW_NORMAL = 3,
    BT_NEW_PREMIUM = 4,
    BT_MAX = 5,
};

enum LuckyRewardType : std::int32_t {
    LRT_NONE = 0,
    LRT_COMMON = 1,
    LRT_UNIQUE = 2,
    LRT_MODIFY = 3,
    LRT_COMMON_EX = 4,
    LRT_BONUS = 5,
    LRT_MAX = 6,
};

} // namespace LuckyGameSystem

namespace SocialClass {

enum Type : std::int32_t {
    WANDERER = 0,
    SQUIRE = 1,
    MERCENARY = 2,
    KNIGHT = 3,
    BANNERET = 4,
    BARON = 5,
    COUNT = 6,
    DUKE = 7,
    KING = 8,
    EMPEROR = 9,
    MAX = 10,
};

} // namespace SocialClass

namespace DynamicQuestType {

enum Enum : std::int32_t {
    DYNAMIC_CONTENTS = 0,
    ZONE_QUEST = 1,
    MAX_TYPE = 2,
};

enum ProgressInfoType : std::int32_t {
    DURING_PROGRESS = 0,
    REWARD_START = 1,
    REWARD_RECEIVED = 2,
    FAILED = 3,
    MAX_INFOTYPE = 4,
};

enum PointType : std::int32_t {
    NORMAL_POINTTYPE = 0,
    EXTRA_POINTTYPE = 1,
    MAX_POINTTYPE = 2,
};

enum QuestType : std::int32_t {
    NORMAL_QUEST = 0,
    DAILY_QUEST = 1,
};

} // namespace DynamicQuestType

namespace PledgeMission {

enum State : std::int32_t {
    PMS_COMPLETE = 0,
    PMS_INVALID = 1,
    PMS_ING = 2,
    PMS_CAN_REWARD = 3,
};

enum GoalType : std::int32_t {
    PMGT_HUNTING_ANY = 0,
    PMGT_HUNTING = 1,
    PMGT_W_HUNTING = 2,
    PMGT_QUEST_ANY = 3,
    PMGT_QUEST = 4,
    PMGT_OLYMPIAD_JOIN = 5,
    PMGT_OLYMPIAD_WIN = 6,
    PMGT_OLYMPIAD_LOSE = 7,
    PMGT_OLYMPIAD_FIRST = 8,
    PMGT_PLEDGE_GAME_JOIN = 9,
    PMGT_PLEDGE_GAME_SURVIVE = 10,
    PMGT_PLEDGE_GAME_ITEM = 11,
    PMGT_PLEDGE_GAME_WIN = 12,
    PMGT_PLEDGE_GAME_FIRST = 13,
    PMGT_CASTLEWAR_ATK_WIN = 14,
    PMGT_CASTLEWAR_DEF_WIN = 15,
    PMGT_W_CASTLEWAR_ATK_WIN = 16,
    PMGT_W_CASTLEWAR_DEF_WIN = 17,
    PMGT_ITEM_USE_SKILL_REDUCE = 18,
    PMGT_ITEM_DISASSEMBLE = 19,
    PMGT_VP_GET_ITEM = 20,
    PMGT_PCCAFE_POINT_GET = 21,
    PMGT_MISSION_SUCCESS = 22,
    PMGT_PC_MAIN_LEVEL_UP = 23,
    PMGT_PC_DUAL_LEVEL_UP = 24,
    PMGT_PC_GET_NOBLE = 25,
    PMGT_PC_GET_HONORS = 26,
    PMGT_AI_CONTROL = 27,
    PMGT_FISHING_ANY = 28,
    PMGT_FISHING = 29,
    PMGT_FISHING_BAIT = 30,
    PMGT_ITEM_VARI_TRY = 31,
    PMGT_ITEM_VARI_TRY_ANY = 32,
    PMGT_ITEM_ENSOUL_TRY = 33,
    PMGT_ITEM_ENCHANT_TRY = 34,
    PMGT_ITEM_ENCHANT_NUM_SUC = 35,
    PMGT_ITEM_COMBI_TRY_ANY = 36,
    PMGT_ITEM_COMBI_TRY = 37,
    PMGT_ITEM_COMBI_GET = 38,
    PMGT_AUCTION_TRY = 39,
    PMGT_AUCTION_SUCCESS = 40,
    PMGT_MAX = 41,
};

} // namespace PledgeMission

namespace VALIDATE {

enum Enum : std::int32_t {
    PDEFEND = 0,
    MDEFEND = 1,
    PATTACK = 2,
    MATTACK = 3,
    PATTACKSPEED = 4,
    MATTACKSPEED = 5,
    PSKILLSPEED = 6,
    PHIT = 7,
    MHIT = 8,
    PCRITICAL = 9,
    MCRITICAL = 10,
    SPEED = 11,
    SHIELDDEFENSE = 12,
    SHIELDDEFENSERATE = 13,
    PAVOID = 14,
    MAVOID = 15,
    MAX = 16,
};

enum ExtraEnum : std::int32_t {
    EXTRA_NONE = -1,
    SOUL_SHOT = 0,
    SPIRIT_SHOT = 1,
    EXTRA_MAX = 2,
};

} // namespace VALIDATE

namespace PledgeActiveStatus {

enum Value : std::int32_t {
    locked = 0,
    inactivated = 1,
    activated = 2,
};

} // namespace PledgeActiveStatus

namespace MagicAttackInfo {

enum Enum : std::int32_t {
    None = 0,
    Critical = 1,
    HealCritical = 2,
    OverHit = 3,
    Avoid = 4,
    ShieldDefense = 5,
    Resistance = 6,
    ImmuneForGoodSkill = 7,
    ImmuneForBadSkill = 8,
    Recovery = 9,
    Max = 10,
};

} // namespace MagicAttackInfo

namespace PathToAwakening {

enum AwakeningType : std::int32_t {
    PTA_NONE = 0,
    PTA_LEVEL = 1,
    PTA_MAX = 2,
};

} // namespace PathToAwakening

namespace SubJob {

enum Event : std::int32_t {
    CREATE = 0,
    CHANGE = 1,
    RENEW_SUBJOB = 2,
    RENEW_DUAL_CLASS = 3,
    CREATE_DUALJOB = 4,
};

enum SubJobPacketType : std::int32_t {
    NOTIFY_CLASS_INFO = 0,
    CREATE_NEW_CLASS = 1,
    CHANGE_CLASS = 2,
};

} // namespace SubJob

namespace Post {

enum PostProcessType : std::int32_t {
    ProcessWaiting = 0,
    ProcessSent = 1,
    ProcessOpened = 2,
    ProcessReceived = 3,
};

} // namespace Post

namespace GItemType {

enum Enum : std::int32_t {
    TypeGeneral = 0,
    TypeRare = 1,
};

} // namespace GItemType

namespace PremiumUserType {

enum Enum : std::int32_t {
    PUT_FREE_USER = 0,
    PUT_RUSSIA_PREMIUM_USER = 1,
    PUT_KOREA_PAID_USER = 2,
};

} // namespace PremiumUserType

namespace BuySellConst {

enum BuySellFailType : std::int32_t {
    BSFT_0 = 0,
    BSFT_1 = 1,
    BSFT_LACK_ADENA = 2,
    BSFT_3 = 3,
    BSFT_INVALID_DEAL = 4,
    BSFT_NOT_STACKABLE = 5,
    BSFT_OVER_MILLION = 6,
    BSFT_STOCK_EMPTY = 7,
    BSFT_INVALID_PRICE = 8,
    BSFT_INVANTORY_FULL = 9,
    BSFT_AMOUNT_FULL = 10,
    BSFT_MAX = 11,
};

} // namespace BuySellConst

namespace PackageItemPacket {

enum PACKAGE_ITEM_PACKET_TYPE : std::int32_t {
    PIPT_DEFAULT = 0,
    PIPT_BASIC_INFO = 1,
    PIPT_ITEM_INFO = 2,
};

} // namespace PackageItemPacket


// Original Protocol/L2Constants.h: TotalChecker2015 IPI file 713, line 3223.
enum CreatureMoveType : std::int32_t {
    CMT_NONE = -1,
    CMT_WALK = 0,
    CMT_FLY = 1,
    CMT_BLOCK = 2,
    CMT_FOLLOWING = 3,
};

static_assert(CMT_NONE == -1);
static_assert(CMT_WALK == 0);
static_assert(CMT_FLY == 1);
static_assert(CMT_BLOCK == 2);
static_assert(CMT_FOLLOWING == 3);

// Original anonymous enum in Protocol/L2Constants.h: TotalChecker2015
// TPI 0x10AD / field list 0x10AC, IPI 0x105E, file 713, line 2616.
// Current L2Server SmartID registration and dispatch use these same values.
enum : std::int32_t {
    SCLS_SHUTTLE = 4,
    SCLS_FAKE_NPC = 5,
    SCLS_FAKE_ITEM = 6,
    SCLS_AIR_SHIP = 7,
    SCLS_ITEM = 8,
    SCLS_CREATURE = 9,
    SCLS_VEHICLE = 10,
    SCLS_STATIC_OBJECT = 11,
    SCLS_PLEDGE = 12,
    SCLS_PARTY = 13,
    SCLS_GENERAL_OBJECT = 14,
    SCLS_ALLIANCE = 15,
    SCLS_MAX = 16,
};

// place the PDB's namespace-scope encoding constants beside
// the class tags. Their S_CONSTANT records (11587900 / 11587604) establish
// the names, const unsigned type and values, but not their source header.
const unsigned CLASS_SHIFT = 27;
const unsigned CLASS_MASK = 0xf8000000U;

// same placement for PDB S_CONSTANT 11586368 (const unsigned).
const unsigned SCLS_MIN = 4;
static_assert(SCLS_MIN == SCLS_SHUTTLE);

static_assert(CLASS_MASK == (0x1fU << CLASS_SHIFT));

// restore missing enum declarations from TotalChecker2015.
// IPI source records bind these names to Protocol/l2constants.h (file 713).
// Each enum below records its TPI index and original header line. Values
// are donor symbol evidence. Declaring them does not import donor behavior.
// Numeric call sites require a separate current-source/current-image check.

namespace SERVER_CATEGORY {

// TotalChecker2015 TPI 0x4908e. L2constants.h:12.
enum : std::int32_t {
    SC_LIVE = 0,
    SC_CLASSIC = 1,
    SC_ARENA = 2,
};

} // namespace SERVER_CATEGORY

// TotalChecker2015 TPI 0x55c4e. L2constants.h:199.
enum GoodsInventoryResult : std::int32_t {
    GIR_S_OK = 0,
    GIR_INQUIRY_OK = 1,
    GIR_RETRIEV_OK = 2,
    GIR_INVLAID_REQ = -1,
    GIR_ETC_SYSTEM_ERROR = -2,
    GIR_INVENTORY_LIMIT_OVER = -3,
    GIR_SA_SERVER_DISCONNECTED = -4,
    GIR_ON_TRADING_ACTION = -5,
    GIR_NOT_EXIST_GOODS = -6,
    GIR_E_OK = -7,
    GIR_ARTIFACT_INVENTORY_LIMIT_OVER = -8,
    GIR_TOTAL_INVENTORY_LIMIT_OVER = -9,
    GIR_SA_SERVER_TOO_BUSY_INQUIRY_FAIL = -101,
    GIR_SA_SERVER_TOO_BUSY_RETRIEVE_FAIL = -102,
    GIR_PREV_REQ_ON_PROCESS = -103,
    GIR_INQUIRY_ABORTED = -104,
    GIR_GOODS_ALREADY_REFUND = -105,
    GIR_GOODS_ALREADY_RETRIEVED = -106,
    GIR_WORLD_ID_NOT_ALLOWED = -107,
    GIR_CHARACTER_ID_NOT_ALLOWED = -108,
};

// TotalChecker2015 TPI 0x4d645. L2constants.h:229.
enum GoodsType : std::int32_t {
    GT_PREMIUM_ITEM = 0,
    GT_GOODS = 1,
};

// TotalChecker2015 TPI 0x7215c. L2constants.h:235.
enum GoodsRefundType : std::int32_t {
    GRT_UNKNOWN = 0,
    GRT_CANNOT_REFUND = 1,
    GRT_CAN_REFUND = 2,
    GRT_CAN_REFUND_ALWAYS = 3,
};

// TotalChecker2015 TPI 0x6fd75. L2constants.h:246.
enum AttributeBitType : std::int32_t {
    ABT_NONE = 0,
    ABT_FIRE = 1,
    ABT_WATER = 2,
    ABT_WIND = 4,
    ABT_EARTH = 8,
    ABT_HOLY = 16,
    ABT_UNHOLY = 32,
};

// TotalChecker2015 TPI 0x5260c. L2constants.h:340.
enum BR_LECTURE_MARK_TYPE : std::int32_t {
    BR_LECTURE_MARK_UNDEFINE = 0,
    BR_LECTURE_MARK_STUDENT = 1,
    BR_LECTURE_MARK_TEACHER = 2,
    BR_LECTURE_MARK_NOTHING = 3,
};

// TotalChecker2015 TPI 0x75c99. L2constants.h:350.
enum MY_STATUS_TYPE : std::int32_t {
    MST_IN_BATTLE_FIELD = 0,
    MST_GUILTY = 1,
    MST_CHAOTIC = 2,
    MST_PARTY_MEMBER = 3,
    MST_PARTY_MASTER = 4,
    MST_PLEDGE_MEMBER = 5,
    MST_PLEDGE_MASTER = 6,
    MST_PLEDGE_IN_CASTLE_SIEGE = 7,
    MST_ATTACKER_IN_CASTLE_SIEGE = 8,
    MST_ALLIANCE_MEMBER = 9,
    MST_ALLIANCE_MASTER = 10,
    MST_ALLIANCE_IN_WAR = 11,
    MST_PARTICIPANT_IN_DOMINION_SIEGE = 12,
    MST_PVP_MATCH = 13,
    MST_PVP_MATCH_BLUE = 14,
    MST_PVP_MATCH_RED = 15,
    MST_PVP_MATCH_MASTER = 16,
    MST_MAX = 17,
};

// TotalChecker2015 TPI 0x51fd0. L2constants.h:372.
enum MY_STATUS_BITFLAG : std::int32_t {
    MSB_IN_BATTLE_FIELD = 1,
    MSB_GUILTY = 2,
    MSB_CHAOTIC = 4,
    MSB_PARTY_MEMBER = 8,
    MSB_PARTY_MASTER = 16,
    MSB_PLEDGE_MEMBER = 32,
    MSB_PLEDGE_MASTER = 64,
    MSB_PLEDGE_IN_CASTLE_SIEGE = 128,
    MSB_ATTACKER_IN_CASTLE_SIEGE = 256,
    MSB_ALLIANCE_MEMBER = 512,
    MSB_ALLIANCE_MASTER = 1024,
    MSB_ALLIANCE_IN_WAR = 2048,
    MSB_PARTICIPANT_IN_DOMINION_SIEGE = 4096,
    MSB_PVP_MATCH = 8192,
    MSB_PVP_MATCH_BLUE = 16384,
    MSB_PVP_MATCH_RED = 32768,
    MSB_PVP_MATCH_MASTER = 65536,
    MSB_MAX = 131072,
};

// TotalChecker2015 TPI 0x558f2. L2constants.h:394.
enum ENEMY_RELATION_TYPE : std::int32_t {
    ERT_IN_BATTLE_FIELD = 0,
    ERT_GUILTY = 1,
    ERT_CHAOTIC = 2,
    ERT_PARTY_MEMBER = 3,
    ERT_PARTY_MASTER = 4,
    ERT_PARTY_COMRADE = 5,
    ERT_PLEDGE_MEMBER = 6,
    ERT_PLEDGE_MASTER = 7,
    ERT_PLEDGE_COMRADE = 8,
    ERT_PLEDGE_IN_CASTLE_SIEGE = 9,
    ERT_ATTACKER_IN_CASTLE_SIEGE = 10,
    ERT_COMRADE_IN_CASTLE_SIEGE = 11,
    ERT_ENEMY_IN_CASTLE_SIEGE = 12,
    ERT_ENEMY_IN_PLEDGE_WAR = 13,
    ERT_DECLARE_WAR_TO_YOUR_PLEDGE = 14,
    ERT_DECLARE_WAR_TO_MY_PLEDGE = 15,
    ERT_ALLIANCE_MEMBER = 16,
    ERT_ALLIANCE_MASTER = 17,
    ERT_DUEL_ENEMY = 18,
    ERT_COMRADE_IN_DOMINION_SIEGE = 19,
    ERT_ENEMY_IN_DOMINION_SIEGE = 20,
    ERT_COMRADE_IN_PVP_MATCH = 21,
    ERT_ENEMY_IN_PVP_MATCH = 22,
    ERT_BLUE_IN_PVP_MATCH = 23,
    ERT_RED_IN_PVP_MATCH = 24,
    ERT_MASTER_IN_PVP_MATCH = 25,
    ERT_IN_WORLD_COLLISION_AREA = 26,
    ERT_COMRADE_IN_ARENA_MATCH = 27,
    ERT_ENEMY_IN_ARENA_MATCH = 28,
    ERT_MAX = 29,
};

// TotalChecker2015 TPI 0x54b04. L2constants.h:428.
enum ENEMY_RELATION_BITFLAG : std::int32_t {
    ERB_IN_BATTLE_FIELD = 1,
    ERB_GUILTY = 2,
    ERB_CHAOTIC = 4,
    ERB_PARTY_MEMBER = 8,
    ERB_PARTY_MASTER = 16,
    ERB_PARTY_COMRADE = 32,
    ERB_PLEDGE_MEMBER = 64,
    ERB_PLEDGE_MASTER = 128,
    ERB_PLEDGE_COMRADE = 256,
    ERB_PLEDGE_IN_CASTLE_SIEGE = 512,
    ERB_ATTACKER_IN_CASTLE_SIEGE = 1024,
    ERB_COMRADE_IN_CASTLE_SIEGE = 2048,
    ERB_ENEMY_IN_CASTLE_SIEGE = 4096,
    ERB_ENEMY_IN_PLEDGE_WAR = 8192,
    ERB_DECLARE_WAR_TO_YOUR_PLEDGE = 16384,
    ERB_DECLARE_WAR_TO_MY_PLEDGE = 32768,
    ERB_ALLIANCE_MEMBER = 65536,
    ERB_ALLIANCE_MASTER = 131072,
    ERB_DUEL_ENEMY = 262144,
    ERB_COMRADE_IN_DOMINION_SIEGE = 524288,
    ERB_ENEMY_IN_DOMINION_SIEGE = 1048576,
    ERB_COMRADE_IN_PVP_MATCH = 2097152,
    ERB_ENEMY_IN_PVP_MATCH = 4194304,
    ERB_BLUE_IN_PVP_MATCH = 8388608,
    ERB_RED_IN_PVP_MATCH = 16777216,
    ERB_MASTER_IN_PVP_MATCH = 33554432,
    ERB_IN_WORLD_COLLISION_AREA = 67108864,
    ERB_COMRADE_IN_ARENA_MATCH = 134217728,
    ERB_ENEMY_IN_ARENA_MATCH = 268435456,
    ERB_MAX = 536870912,
};

// TotalChecker2015 TPI 0x5a6d0. L2constants.h:463.
enum ServerObjectType : std::int32_t {
    SOT_GENERAL = 0,
    SOT_BATTLE_CAMP = 1,
    SOT_CONTROL_TOWER = 2,
    SOT_HOLY_THING = 3,
    SOT_SIGN_BOARD = 4,
    SOT_AIRSHIP_STEERING = 5,
    SOT_OWN_THING = 6,
    SOT_STATUE = 7,
    SOT_STATUE_PLEDGE = 8,
};

// TotalChecker2015 TPI 0x3c3ca. L2constants.h:476.
enum HolyThingStatus : std::int32_t {
    HTS_CLOSED = 0,
    HTS_OPENED = 1,
    HTS_CLOSING = 2,
    HTS_OPENING = 3,
};

// TotalChecker2015 TPI 0x4d6a7. L2constants.h:484.
enum WeightStatus : std::int32_t {
    WEIGHT_NORMAL = 0,
    WEIGHT_LIGHT = 1,
    WEIGHT_HEAVY = 2,
    WEIGHT_OVERWEIGHT = 3,
    WEIGHT_SUPEROVER = 4,
};

// TotalChecker2015 TPI 0x5a916. L2constants.h:493.
enum StaticObjectType : std::int32_t {
    STOT_NORMAL = 0,
    STOT_DOOR = 1,
    STOT_CHAIR = 2,
    STOT_STATUE = 4,
};

// TotalChecker2015 TPI 0x75d0c. L2constants.h:633.
enum ItemAttributionType : std::int32_t {
    IAT_Exception = -2,
    IAT_Cursed = -1,
    IAT_Normal = 0,
    IAT_Blessed = 1,
    IAT_MAX = 2,
};

// TotalChecker2015 TPI 0x6fec5. L2constants.h:692.
enum PreviewPrice : std::int32_t {
    PREVIEW_CRT_NONE = 10,
    PREVIEW_CRT_D = 50,
    PREVIEW_CRT_C = 100,
    PREVIEW_CRT_B = 200,
    PREVIEW_CRT_A = 500,
    PREVIEW_CRT_S = 1000,
    PREVIEW_CRT_S80 = 1000,
    PREVIEW_CRT_S84 = 1000,
    PREVIEW_CRT_R = 2000,
    PREVIEW_CRT_R95 = 2000,
    PREVIEW_CRT_R99 = 2000,
    PREVIEW_CRT_MAX = 2001,
};

namespace ClientINI {

// TotalChecker2015 TPI 0x75d6a. L2constants.h:863.
enum InteractRequesterType : std::int32_t {
    IRT_PUBLIC = 0,
    IRT_PLEDGE = 1,
    IRT_FRIEND = 2,
    IRT_MAX = 3,
};

// TotalChecker2015 TPI 0x56584. L2constants.h:872.
enum InteractRequesterBitflag : std::int32_t {
    IRB_PUBLIC = 1,
    IRB_PLEDGE = 2,
    IRB_FRIEND = 4,
    IRB_MAX = 8,
};

} // namespace ClientINI

// TotalChecker2015 TPI 0x75d76. L2constants.h:884.
enum SystemMsgType : std::int32_t {
    SMT_NONE = 0,
    SMT_BATTLE = 1,
    SMT_SERVER = 2,
    SMT_DAMAGE = 3,
    SMT_POPUP = 4,
    SMT_ERROR = 5,
    SMT_PETITION = 6,
    SMT_USEITEMS = 7,
    SMT_POPUPWITHMSG = 8,
    SMT_CLIENT_DEBUG_MSG = 9,
    SMT_MAX = 10,
};

// TotalChecker2015 TPI 0x75d7a. L2constants.h:899.
enum WaitTypeEnum : std::int32_t {
    WTE_SIT = 0,
    WTE_STAND = 1,
    WTE_FAKE_DEATH = 2,
    WTE_STAND_FROM_DEATH = 3,
    WTE_STAND_FROM_CHAIR = 4,
    WTE_MAX = 5,
};

// TotalChecker2015 TPI 0x48f91. L2constants.h:909.
enum ServerCloseReason : std::int32_t {
    SCR_LOBBY_TIMEOUT = 0,
    SCR_KICKEDBYAUTH_DUAL_LOGIN = 1,
    SCR_KICKEDBYAUTH_BY_WEB = 2,
    SCR_MAX = 3,
};

// TotalChecker2015 TPI 0x48f94. L2constants.h:953.
enum ItemEnchantResult : std::int32_t {
    IER_SUCCESS = 0,
    IER_FAIL = 1,
    IER_CANCELED = 2,
    IER_BLESSED_FAIL = 3,
    IER_EVENT_FAIL = 4,
    IER_ANCIENT_BLESSED_FAIL = 5,
    IER_MAX = 6,
};

// TotalChecker2015 TPI 0x4d6c5. L2constants.h:965.
enum ItemBaseAttributeEnchantResult : std::int32_t {
    IAER_SUCCESS = 0,
    IAER_FAIL = 1,
    IAER_CANCELED = 2,
    IAER_MAX = 3,
};

// TotalChecker2015 TPI 0x22ee1. L2constants.h:999.
enum L2ActionList : std::int32_t {
    LAE_NULL = -1,
    LAE_STAND_SIT = 0,
    LAE_WALK_RUN = 1,
    LAE_ATTACK = 2,
    LAE_TRADE = 3,
    LAE_NEXT_TARGET = 4,
    LAE_GET_ITEM = 5,
    LAE_ASSIST = 6,
    LAE_PARTY_INVITE = 7,
    LAE_PARTY_WITHDRAW = 8,
    LAE_PARTY_OUST = 9,
    LAE_PRIVATE_STORE = 10,
    LAE_PARTY_MATCH = 11,
    LAE_SOCIAL_GREET = 12,
    LAE_SOCIAL_VICTORY = 13,
    LAE_SOCIAL_ADVANCE = 14,
    LAE_PET_CHANGE_MOVE_MODE = 15,
    LAE_PET_ATTACK = 16,
    LAE_PET_STOP = 17,
    LAE_PET_GET_ITEM = 18,
    LAE_PET_DEPOSIT = 19,
    LAE_PET_DISMISS = 20,
    LAE_SUMMON_CHANGE_MOVE_MODE = 21,
    LAE_SUMMON_ATTACK = 22,
    LAE_SUMMON_STOP = 23,
    LAE_SOCIAL_YES = 24,
    LAE_SOCIAL_NO = 25,
    LAE_SOCIAL_BOW = 26,
    LAE_SUMMON_SPECIAL_SKILL = 27,
    LAE_PRIVATE_BUY = 28,
    LAE_SOCIAL_UNAWARE = 29,
    LAE_SOCIAL_WAITINGA = 30,
    LAE_SOCIAL_LAUGH = 31,
    LAE_SUMMON_SPECIAL_SKILL2 = 32,
    LAE_SOCIAL_CLAP = 33,
    LAE_SOCIAL_DANCE = 34,
    LAE_SOCIAL_SORROW = 35,
    LAE_SUMMON_SPECIAL_SKILL3 = 36,
    LAE_MAKEITEM = 37,
    LAE_RIDE = 38,
    LAE_SUMMON_SPECIAL_SKILL4 = 39,
    LAE_VOTE = 40,
    LAE_SUMMON_SPECIAL_SKILL5 = 41,
    LAE_SUMMON_SPECIAL_SKILL6 = 42,
    LAE_SUMMON_SPECIAL_SKILL7 = 43,
    LAE_SUMMON_SPECIAL_SKILL8 = 44,
    LAE_SUMMON_SPECIAL_SKILL9 = 45,
    LAE_SUMMON_SPECIAL_SKILL10 = 46,
    LAE_SUMMON_SPECIAL_SKILL11 = 47,
    LAE_SUMMON_SPECIAL_SKILL12 = 48,
    LAE_PET_SPECIAL_SKILL1 = 49,
    LAE_LEADER_CHANGE = 50,
    LAE_MAKEITEM2 = 51,
    LAE_SUMMON_DESPAWN = 52,
    LAE_SUMMON_MOVE_TO_TARGET = 53,
    LAE_PET_MOVE_TO_TARGET = 54,
    LAE_REC_START_STOP = 55,
    LAE_INVITE_CHANNEL = 56,
    LAE_STORE_FIND = 57,
    LAE_DUEL_CHALLENGE = 58,
    LAE_DUEL_CANCEL_CHALLENCE = 59,
    LAE_DUEL_CHALLENGE_PARTY = 60,
    LAE_PACKAGE_PRIVATE_STORE = 61,
    LAE_SOCIAL_CHARM = 62,
    LAE_MINIGAME1 = 63,
    LAE_TELEPORT_BOOKMARK = 64,
    LAE_BOT_REPORT = 65,
    LAE_SOCIAL_SHY = 66,
    LAE_AIRSHIP_NAVIGATEWHEEL = 67,
    LAE_AIRSHIP_LEAVEWHEEL = 68,
    LAE_AIRSHIP_STARTSHIP = 69,
    LAE_AIRSHIP_LEAVESHIP = 70,
    LAE_SOCIAL_COUPLE_BOW = 71,
    LAE_SOCIAL_COUPLE_HIGHFIVE = 72,
    LAE_SOCIAL_COUPLE_DANCE = 73,
    LAE_CONDINFO_TOGGLE = 74,
    LAE_SOCIAL_SHEATHING_WEAPON = 75,
    LAE_FRIEND_ADD = 76,
    LAE_MOVIE_REC_START_STOP = 77,
    LAE_SET_TACTICAL_SIGN1 = 78,
    LAE_SET_TACTICAL_SIGN2 = 79,
    LAE_SET_TACTICAL_SIGN3 = 80,
    LAE_SET_TACTICAL_SIGN4 = 81,
    LAE_TARGET_TACTICAL_SIGN1 = 82,
    LAE_TARGET_TACTICAL_SIGN2 = 83,
    LAE_TARGET_TACTICAL_SIGN3 = 84,
    LAE_TARGET_TACTICAL_SIGN4 = 85,
    LAE_REGIST_WAITING_SUBSTITUTE = 86,
    LAE_SOCIAL_PROPOSE = 87,
    LAE_SOCIAL_PROVOKE = 88,
    LAE_SOCIAL_BEAUTY = 89,
    LAE_VIEW_INSTANCEZONECOOLTIME = 90,
    LAE_FIXED_PKMODE_TOGGLE = 91,
    LAE_PREV_TARGET = 92,
    LAE_PET_SPECIAL_SKILL2 = 1000,
    LAE_MAX = 1001,
};

// TotalChecker2015 TPI 0x22ffc. L2constants.h:1738.
enum SocialActionTypeEnum : std::int32_t {
    SAT_MIN = 0,
    SAT_GREET = 2,
    SAT_VICTORY = 3,
    SAT_ADVANCE = 4,
    SAT_NO = 5,
    SAT_YES = 6,
    SAT_BOW = 7,
    SAT_UNAWARE = 8,
    SAT_WAITINGA = 9,
    SAT_LAUGH = 10,
    SAT_APPLAUS = 11,
    SAT_DANCE = 12,
    SAT_SAD = 13,
    SAT_CHARM = 14,
    SAT_SHY = 15,
    SAT_SHEATHING_WEAPON = 19,
    SAT_BEAUTY = 21,
    SAT_LEVEL_UP = 2122,
    SAT_HERO = 20016,
    SAT_CURSED_WEAPON_LEVEL_UP = 20017,
    SAT_FACTION_LEVEL_UP = 16730,
    SAT_PROPOSE = 28,
    SAT_PROVOKE = 29,
    SAT_MAX = 30,
};

// TotalChecker2015 TPI 0x49085. L2constants.h:1769.
enum CASTLE_SIEGE_TYPE : std::int32_t {
    CASTLE_SIEGE_TYPE_QUERY = 0,
    CASTLE_SIEGE_TYPE_ATTACK = 1,
    CASTLE_SIEGE_TYPE_DEFENSE = 2,
};

// TotalChecker2015 TPI 0x4d744. L2constants.h:1776.
enum PledgeStatusType : std::int32_t {
    PST_NORMAL = 0,
    PST_ATTACK_CASTLE = 1,
    PST_PROTECT_CASTLE = 2,
    PST_WAIT_DISMISS = 3,
    PST_IN_WAR = 4,
};

// TotalChecker2015 TPI 0x75ece. L2constants.h:1785.
enum PartyMatchEntryType : std::int32_t {
    PMET_OBSERVER = 0,
    PMET_CANDIDATE = 1,
    PMET_REFRESH = 2,
    PMET_CLOSE = 3,
};

// TotalChecker2015 TPI 0x55c56. L2constants.h:1808.
enum PrivateStoreActionType : std::int32_t {
    PSA_NONE = 0,
    PSA_SELL = 1,
    PSA_BUY = 2,
    PSA_SELL_MANAGE = 3,
    PSA_BUY_MANAGE = 4,
    PSA_RECIPE = 5,
    PSA_RECIPE_MANAGE = 6,
    PSA_WHOLE_SELL = 7,
    PSA_WHOLE_SELL_MANAGE = 8,
    PSA_MAX = 9,
};

// TotalChecker2015 TPI 0x75c45. L2constants.h:1881.
enum SayFStrIDType : std::int32_t {
    SIDT_NONE = 0,
    SIDT_ITEM = 1,
    SIDT_NPC = 2,
    SIDT_SKILL = 3,
    SIDT_QUEST = 4,
    SIDT_NPCSTRING = 5,
    SIDT_SYSSTRING = 6,
    SIDT_MAX = 7,
};

// TotalChecker2015 TPI 0x1014. L2constants.h:1893.
enum SkillCastingSourceType : std::int32_t {
    SCST_NOMAL = 0,
    SCST_ACTION = 1,
    SCST_ITEM = 2,
    SCST_MAXNUM = 3,
};

// TotalChecker2015 TPI 0x48dfb. L2constants.h:2185.
enum CommandType : std::int32_t {
    CMDT_NONE = -1,
    CMDT_LOCATION = 0,
    CMDT_PETITION = 1,
    CMDT_PETITIONCANCEL = 2,
    CMDT_GMLIST = 3,
    CMDT_SIT = 4,
    CMDT_STAND = 5,
    CMDT_WALK = 6,
    CMDT_RUN = 7,
    CMDT_ATTACK = 8,
    CMDT_ATTACKFORCE = 9,
    CMDT_ATTACKSTAND = 10,
    CMDT_TRADE = 11,
    CMDT_TARGET = 12,
    CMDT_TARGETNEXT = 13,
    CMDT_PICKUP = 14,
    CMDT_ASSIST = 15,
    CMDT_PARTYINVITE = 16,
    CMDT_PARTYLEAVE = 17,
    CMDT_PARTYDISMISS = 18,
    CMDT_VENDOR = 19,
    CMDT_PARTYMATCHING = 20,
    CMDT_SOCIALHELLO = 21,
    CMDT_SOCIALVICTORY = 22,
    CMDT_SOCIALCHARGE = 23,
    CMDT_USESKILL = 24,
    CMDT_USESKILLFORCE = 25,
    CMDT_USESKILLSTAND = 26,
    CMDT_USESHORTCUT = 27,
    CMDT_USESHORTCUTFORCE = 28,
    CMDT_USESHORTCUTSTAND = 29,
    CMDT_ALLYINVITE = 30,
    CMDT_ALLYLEAVE = 31,
    CMDT_ALLYOUST = 32,
    CMDT_ALLYDISMISS = 33,
    CMDT_ALLYCREST = 34,
    CMDT_ALLYINFO = 35,
    CMDT_FRIENDINVITE = 36,
    CMDT_FRIENDLIST = 37,
    CMDT_FRIENDDEL = 38,
    CMDT_BUY = 39,
    CMDT_ALLYWAR_START = 40,
    CMDT_ALLYWAR_STOP = 41,
    CMDT_ALLYWAR_SURRENDER = 42,
    CMDT_BLOCK_LIST = 43,
    CMDT_BLOCK = 44,
    CMDT_UNBLOCK = 45,
    CMDT_SOCIALNO = 46,
    CMDT_SOCIALYES = 47,
    CMDT_SOCIALBOW = 48,
    CMDT_SOCIALUNAWARE = 49,
    CMDT_SOCIALWAITINGA = 50,
    CMDT_SOCIALLAUGH = 51,
    CMDT_ESCAPE = 52,
    CMDT_REMAINTIME = 53,
    CMDT_SOCIALAPPLAUS = 54,
    CMDT_SOCIALDANCE = 55,
    CMDT_SOCIALSAD = 56,
    CMDT_SITSTAND = 57,
    CMDT_WALKRUN = 58,
    CMDT_MANUFACTURE = 59,
    CMDT_MOUNTDISMOUNT = 60,
    CMDT_MOUNT = 61,
    CMDT_DISMOUNT = 62,
    CMDT_PETHOLD = 63,
    CMDT_PETATTACK = 64,
    CMDT_PETSTOP = 65,
    CMDT_PETPICKUP = 66,
    CMDT_PETREVERT = 67,
    CMDT_PETSPECIAL = 68,
    CMDT_SUMMONHOLD = 69,
    CMDT_SUMMONATTACK = 70,
    CMDT_SUMMONSTOP = 71,
    CMDT_SUMMONSIEGE = 72,
    CMDT_SKILL4036 = 73,
    CMDT_SKILL4138 = 74,
    CMDT_RECOMMEND = 75,
    CMDT_MACROWAIT = 76,
    CMDT_TIME = 77,
    CMDT_ALLBLOCK = 78,
    CMDT_ALLUNBLOCK = 79,
    CMDT_EQUIP = 80,
    CMDT_PARTYINFO = 81,
    CMDT_PLEDGEWARINFO = 82,
    CMDT_ALLYWARIFNO = 83,
    CMDT_SEARCH = 84,
    CMDT_LEADERCHANGE = 85,
    CMDT_WARATTACK = 86,
    CMDT_WARSTOP = 87,
    CMDT_ATTACKLIST = 88,
    CMDT_UNDERATTACKLIST = 89,
    CMDT_WARLIST = 90,
    CMDT_ALLYCRESTDEL = 91,
    CMDT_OPENCH = 92,
    CMDT_CLOSECH = 93,
    CMDT_INVITECH = 94,
    CMDT_BANCH = 95,
    CMDT_WITHDRAWCH = 96,
    CMDT_CHANNELINFO = 97,
    CMDT_SELFNICKNAME = 98,
    CMDT_WARSTATUS = 99,
    CMDT_PLEDGEPENALTY = 100,
    CMDT_MANUFACTURE2 = 101,
    CMDT_CHINATIME = 102,
    CMDT_RECSTART = 103,
    CMDT_RECSTOP = 104,
    CMDT_RECSTARTSTOP = 105,
    CMDT_PETMOVE = 106,
    CMDT_SUMMONMOVE = 107,
    CMDT_UNSUMMON = 108,
    CMDT_OLYMPIADSTAT = 109,
    CMDT_GRADUATE = 110,
    CMDT_DUEL_CHALLENGE = 111,
    CMDT_DUEL_CANCEL_CHALLENGE = 112,
    CMDT_DUEL_CHALLENGE_PARTY = 113,
    CMDT_INSTANTZONE_REMAIN_TIME = 114,
    CMDT_PACKAGE_VENDOR = 115,
    CMDT_SOCIALCHARM = 116,
    CMDT_RESET_NICKNAME = 117,
    CMDT_PLAY_MINIGAME = 118,
    CMDT_TELEPORT_BOOKMARK = 119,
    CMDT_BOT_REPORT = 120,
    CMDT_SOCIALSHY = 121,
    CMDT_NAVIGATEWHEEL = 122,
    CMDT_LEAVEWHEEL = 123,
    CMDT_STARTSHIP = 124,
    CMDT_LEAVESHIP = 125,
    CMDT_BIRTHDAY = 126,
    CMDT_POSTWND = 127,
    CMDT_SOCIAL_COUPLE_BOW = 128,
    CMDT_SOCIAL_COUPLE_HIGHFIVE = 129,
    CMDT_SOCIAL_COUPLE_DANCE = 130,
    CMDT_ADD_POST_FRIEND = 131,
    CMDT_BR_INITIAL_DUMMY = 132,
    CMDT_BR_EVA_DUMMY = 133,
    CMDT_BR_LECTURE_MARK_DUMMY = 134,
    CMDT_CONDINFO_TOGGLE = 135,
    CMDT_CONDINFO_OFF = 136,
    CMDT_CONDINFO_ON = 137,
    CMDT_SOCIAL_SHEATHE = 138,
    CMDT_SOCIAL_PROPOSE = 139,
    CMDT_SOCIAL_PROVOKE = 140,
    CMDT_SURRENDER_PLEDGE_WAR = 141,
    CMDT_MOVIE_REC_START = 142,
    CMDT_MOVIE_REC_STOP = 143,
    CMDT_MOVIE_REC_TOGGLE = 144,
    CMDT_24HZ_ONOFF = 145,
    CMDT_REGIST_WAITING_SUBSTITUTE_ONOFF = 146,
    CMDT_REGIST_WAITING_SUBSTITUTE_ON = 147,
    CMDT_REGIST_WAITING_SUBSTITUTE_OFF = 148,
    CMDT_SET_TACTICAL_SIGN1 = 149,
    CMDT_SET_TACTICAL_SIGN2 = 150,
    CMDT_SET_TACTICAL_SIGN3 = 151,
    CMDT_SET_TACTICAL_SIGN4 = 152,
    CMDT_TARGET_TACTICAL_SIGN1 = 153,
    CMDT_TARGET_TACTICAL_SIGN2 = 154,
    CMDT_TARGET_TACTICAL_SIGN3 = 155,
    CMDT_TARGET_TACTICAL_SIGN4 = 156,
    CMDT_FIXED_PKMODE_TOGGLE = 157,
    CMDT_FIXED_PKMODE_ON = 158,
    CMDT_FIXED_PKMODE_OFF = 159,
    CMDT_USE_PET_SKILL = 160,
    CMDT_USE_SUMMON_SKILL = 161,
    CMDT_CURIOUS_HOUSE_REC = 162,
    CMDT_SOCIAL_BEAUTY = 163,
    CMDT_PLEDGE_JOIN_SYS = 164,
    CMDT_TARGETPREV = 165,
    CMDT_INGAMEWEB_OPEN_MAINPAGE = 166,
    MAX_COMMAND_TYPE = 167,
    CMDT_BR_INITIAL = 1001,
    CMDT_BR_EVA = 1002,
    CMDT_BR_LECTURE_MARK = 1003,
    CMDT_BUILDER = 268435456,
    CMDT_UNREAL = 268435457,
    CMDT_STRING = 536870912,
};

// TotalChecker2015 TPI 0x75d24. L2constants.h:2401.
enum ServerCloseType : std::int32_t {
    SVRCT_NONE = -1,
    SVRCT_AUTH_CONNECTION_LOST = 0,
    SVRCT_WORLD_CONNECTION_LOST = 1,
    SVRCT_WORLD_SERVER_CLOSE = 2,
    SVRCT_NPAUTH_CONNECTION_LOST = 3,
};

// TotalChecker2015 TPI 0x75d2e. L2constants.h:2410.
enum AuthLoginFailType : std::int32_t {
    ALFT_ALL_OK = 0,
    ALFT_DATABASE_FAIL = 1,
    ALFT_INVALID_ACCOUNT = 2,
    ALFT_INCORRECT_PWD = 3,
    ALFT_ACCOUNT_LOAD_FAIL = 4,
    ALFT_LOAD_SSN_ERROR = 5,
    ALFT_NO_SERVERLIST = 6,
    ALFT_ALREADY_LOGIN = 7,
    ALFT_SERVER_DOWN = 8,
    ALFT_INCORRECT_MD5Key = 9,
    ALFT_NO_LOGININFO = 10,
    ALFT_KICKED_BY_WEB = 11,
    ALFT_UNDER_AGE = 12,
    ALFT_KICKED_DOUBLE_LOGIN = 13,
    ALFT_ALREADY_PLAY_GAME = 14,
    ALFT_LIMIT_EXCEED = 15,
    ALFT_SERVER_CHECK = 16,
    ALFT_MODIFY_PASSWORD = 17,
    ALFT_NOT_PAID = 18,
    ALFT_NO_SPECIFICTIME = 19,
    ALFT_SYSTEM_ERROR = 20,
    ALFT_ALREADY_USED_IP = 21,
    ALFT_BLOCKED_IP = 22,
    ALFT_DELETE_CHRARACTER_OK = 23,
    ALFT_CREATE_CHARACTER_OK = 24,
    ALFT_INVALID_NAME = 25,
    ALFT_INVALID_GENDER = 26,
    ALFT_INVALID_CLASS = 27,
    ALFT_INVALID_ATTR = 28,
    ALFT_MAX_CHAR_NUM_OVER = 29,
    ALFT_TIME_SERVER_LIMIT_EXCEED = 30,
    ALFT_INVALID_SECURITYCARD = 31,
    ALFT_UNDER_AGE_TIME_LIMIT = 32,
    ALFT_RESTRICTED_SERVER = 33,
    ALFT_TIME_EXHAUSTED = 34,
    ALFT_DISALLOWED_MULTI_LOADING = 35,
    ALFT_DORMANT_USER = 36,
    ALFT_NOT_AGREED_GAME = 37,
    ALFT_WAITING_PARENTS_APPROVAL = 38,
    ALFT_WAITING_SECEDE = 39,
    ALFT_LINKED_GAME_ACCOUNT = 40,
    ALFT_MODIFY_QUIZ = 41,
    ALFT_EXCEED_CONCURRENT_PLAY_LIMIT = 42,
    ALFT_EXTERNAL_AUTH_BLOCKED = 43,
    DUMMY_1 = 44,
    DUMMY_2 = 45,
    E_NCTASD_AUTHENTICATIONFAILED = 46,
    E_NCTASD_IVRNOTAVAILABLE = 47,
    E_NCTASD_AUTHENTICATIONTIMEOUT = 48,
    E_NCTASD_PHONENOTAVAILABLE = 49,
    E_NCTASD_INVALIDCUSTOMERINFO = 50,
    E_NCTASD_NOTAVAILABLE = 51,
    E_NCTASD_TOOBUSY2 = 52,
    E_NCTASD_NOTPAID = 53,
    E_NCTASD_AUTNETICATIONBLOCKEDTEMPORALLY = 54,
    E_NCTASD_CALLLIMITEXCEEDED = 55,
    E_NCTASD_AUTHENTICATIONALREADYINROCESS = 56,
    E_NCTASD_RESERVED1 = 57,
    E_NCTASD_RESERVED2 = 58,
    ALFT_INVALID_REGISTER_PC = 59,
    ALFT_WAIT_FOR_EMAIL_ACTIVATION = 72,
    ALFT_NCOTP_FAIL = 73,
    ALFT_AUTHSERVER_FAIL = 74,
    ALFT_GRADE_SYSTEM_ERROR = 75,
    ALFT_DORMANT_USER2 = 77,
    ALFT_END_OF_NCOTP_SERVICE = 79,
    ALFT_VERISIGN_CREDENTIAL_LOCKED = 210,
    ALFT_VERISIGN_SYSTEM_ERROR = 211,
};

// TotalChecker2015 TPI 0x48ee6. L2constants.h:2506.
enum CharacterDeleteFailType : std::int32_t {
    CDFT_UNKNOWN = 1,
    CDFT_PLEDGE_MEMBER = 2,
    CDFT_PLEDGE_MASTER = 3,
    CDFT_PROHIBIT_CHAR_DELETION = 4,
    CDFT_COMMISSION = 5,
    CDFT_MENTOR = 6,
    CDFT_MENTEE = 7,
    CDFT_MAIL = 8,
};

// TotalChecker2015 TPI 0x75d68. L2constants.h:2518.
enum LanguageType : std::int32_t {
    LT_KOREAN = 0,
    LT_ENGLISH = 1,
    LT_JAPANESE = 2,
    LT_TAIWAN = 3,
    LT_CHINESE = 4,
    LT_THAI = 5,
    LT_PHILIPPINE = 6,
    LT_ID = 7,
    LT_RUSSIA = 8,
    LT_EUROPE = 9,
    LT_GERMANY = 10,
    LT_FRANCE = 11,
    LT_POLAND = 12,
    LT_TURKEY = 13,
    MT_MAX = 14,
};

// TotalChecker2015 TPI 0x52638. L2constants.h:2541.
enum SpecialItemClassType : std::int32_t {
    CALCULATOR_CLAS_ID = 4393,
    LOTTO_TICKET_CLASS_ID = 4442,
    MON_RACE_SINGLE_TICKET_CLASS_ID = 4443,
    MON_RACE_MULTIPLE_TICKET_CLASS_ID = 4444,
    ANCIENT_ADENA_CLASS_ID = 5575,
};

// TotalChecker2015 TPI 0x753e1. L2constants.h:2550.
enum SSQItemType : std::int32_t {
    BLUE_SEAL_STONE_CLASS_ID = 6360,
    GREEN_SEAL_STONE_CLASS_ID = 6361,
    RED_SEAL_STONE_CLASS_ID = 6362,
};

// TotalChecker2015 TPI 0x463b6. L2constants.h:2558.
enum RadarControlType : std::int32_t {
    RCT_SHOW = 0,
    RCT_DELETE = 1,
    RCT_DELETE_ALL = 2,
};

// TotalChecker2015 TPI 0x463b8. L2constants.h:2566.
enum RadarPositionType : std::int32_t {
    RPT_RADAR = 0,
    RPT_MAP = 1,
    RPT_BOTH = 2,
};

// TotalChecker2015 TPI 0x75dbd. L2constants.h:2687.
enum L2_SSQ_TEAM : std::int32_t {
    SSQT_NONE = 0,
    SSQT_DUSK = 1,
    SSQT_DAWN = 2,
};

// TotalChecker2015 TPI 0x3c3d5. L2constants.h:2694.
enum L2_SSQ_GUARD : std::int32_t {
    SSQG_NORMAL = 0,
    SSQG_TWILIGHT = 1,
    SSQG_DAWN = 2,
    SSQG_MAX = 3,
};

// TotalChecker2015 TPI 0x75dc9. L2constants.h:2702.
enum L2_SSQ_EVENT : std::int32_t {
    SSQE_TIMEATTACK = 0,
};

// TotalChecker2015 TPI 0x75dcb. L2constants.h:2707.
enum L2_SSQ_SEAL : std::int32_t {
    SSQS_NONE = 0,
    SSQS_GREED = 1,
    SSQS_REVEAL = 2,
    SSQS_STRIFE = 3,
};

// TotalChecker2015 TPI 0x75dcf. L2constants.h:2715.
enum L2_SSQ_TIMEATTACK_ROOM : std::int32_t {
    SSQTR_ROOM1 = 1,
    SSQTR_ROOM2 = 2,
    SSQTR_ROOM3 = 3,
    SSQTR_ROOM4 = 4,
    SSQTR_ROOM5 = 5,
};

// TotalChecker2015 TPI 0x6c3d0. L2constants.h:2724.
enum TUTORIAL_TYPE : std::int32_t {
    TUTORIAL_OLD = 1,
    TUTORIAL_NEW = 2,
};

// TotalChecker2015 TPI 0x558f6. L2constants.h:2730.
enum L2_SND_TYPE : std::int32_t {
    SND_EFFECT = 0,
    SND_MUSIC = 1,
    SND_STREAMING_VOICE = 2,
    SND_NPC_VOICE = 3,
    SND_ITEM_MUSIC = 4,
};

// TotalChecker2015 TPI 0x75ddd. L2constants.h:2746.
enum ColosseumIdentifierEnum : std::int32_t {
    COLOSSEUM_IDENTIFIER_NONE = 0,
    COLOSSEUM_IDENTIFIER_RED = 1,
    COLOSSEUM_IDENTIFIER_BLUE = 2,
};

// TotalChecker2015 TPI 0x75ddf. L2constants.h:2753.
enum AppendixVisualEffectBitType : std::int32_t {
    APPVEBT_NONE = 0,
    APPVEBT_COLOSSEUM_IDENTIFIER_RED = 4,
    APPVEBT_COLOSSEUM_IDENTIFIER_BLUE = 8,
    APPVEBT_COLOSSEUM_SELECTED_RED = 16,
    APPVEBT_COLOSSEUM_SELECTED_BLUE = 32,
    APPVEBT_MAX = 64,
};

// TotalChecker2015 TPI 0x75ded. L2constants.h:2796.
enum PageType : std::int32_t {
    PAGE_START = 1,
    PAGE_PREV = 2,
    PAGE_NEXT = 3,
};

// TotalChecker2015 TPI 0x75df1. L2constants.h:2820.
enum RecipeBookClass : std::int32_t {
    RBC_DWARF = 0,
    RBC_NORMAL = 1,
};

// TotalChecker2015 TPI 0x75e09. L2constants.h:2924.
enum : std::int32_t {
    PledgeTypeCountForSkill = 7,
};

// TotalChecker2015 TPI 0x75e0d. L2constants.h:2940.
enum SubPledgeBit : std::int32_t {
    SPB_FOR_ALL = 0,
    SPB_MASTER = 1,
    SPB_ACADEMY = 2,
    SPB_SUB_1 = 256,
    SPB_SUB_2 = 512,
    SPB_SUB_1_1 = 4096,
    SPB_SUB_1_2 = 8192,
    SPB_SUB_2_1 = 65536,
    SPB_SUB_2_2 = 131072,
};

// TotalChecker2015 TPI 0x5ce24. L2constants.h:3093.
enum UserRankType : std::int32_t {
    URT_WANDERER = 0,
    URT_CITIZEN = 1,
    URT_HEIR = 2,
    URT_KNIGHT = 3,
    URT_SAGE = 4,
    URT_BARON = 5,
    URT_VISCOUNT = 6,
    URT_COUNT = 7,
    URT_MARQUIS = 8,
    URT_DUKE = 9,
    URT_GRANDEE = 10,
    URT_KING = 11,
    URT_MAX = 12,
};

// TotalChecker2015 TPI 0x3929d. L2constants.h:3134.
enum SPECIAL_SKILL : std::int32_t {
    SS_POSSESS_HOLYTHING = 246,
    SS_BUILDING_HEADQUARTER = 247,
    SS_SUMMON_XMASTREE = 2138,
    SS_FISHING_CAST = 1601,
    SS_FISHING_PUMPING = 1602,
    SS_FISHING_REELING = 1603,
    SS_WOLF_COLLAR = 2046,
    SS_BATTLE_FILED_DEATH_PENALTY = 5076,
    SS_DEATH_PENALTY = 14571,
    SS_SSQ_CP_UP = 5074,
    SS_SSQ_CP_DOWN = 5075,
    SS_SOUL_SHOT = 2039,
    SS_SOUL_SHOT_D = 2150,
    SS_SOUL_SHOT_S = 2154,
    SS_SPIRIT_SHOT = 2047,
    SS_BLESSED_SPIRITSHOT = 2061,
    SS_SPIRIT_SHOT_D = 2155,
    SS_BLESSED_SPIRITSHOT_S = 2164,
    SS_BEAST_SOUL_SHOT = 2033,
    SS_BEAST_SPIRIT_SHOT = 2008,
    SS_BLESSED_BEAST_SPIRIT_SHOT = 2009,
    SS_FISHING_SHOT = 2181,
    SS_FISHING_SHOT_D = 2182,
    SS_FISHING_SHOT_S = 2186,
    SS_DEATH_PENALTY_BF = 5660,
    SS_BLOCK_CHAT = 6038,
    SS_BLOCK_PARTY = 6039,
    SS_VIOLET_BOY = 6040,
    SS_BLOCK_ACTION1 = 6055,
    SS_BLOCK_ACTION2 = 6056,
    SS_BLOCK_MOVE = 6057,
    SS_BUILDING_ADVANCE_BASE = 844,
    SS_REMOVAL_ADVANCE_BASE = 845,
    SS_BLOCK_ELIXER_PREMIUM = 7008,
    SS_CHANGE_CLASS_SLOT1 = 1566,
    SS_CHANGE_CLASS_SLOT2 = 1567,
    SS_CHANGE_CLASS_SLOT3 = 1568,
    SS_CHANGE_CLASS_SLOT4 = 1569,
    SS_CLASS_CHANGE_DEBUF = 1570,
    SS_POSSESS_HOLYTHING_LIGHT = 19034,
    SS_POSSESS_HOLYTHING_DARK = 19035,
    SS_s_pledge_clan_regeneration1 = 19074,
    SS_s_pledge_clan_regeneration2 = 19075,
    SS_BUFF_PCCAFE_EXP1 = 9279,
    SS_DEATH_PENALTY_WEAK = 23344,
    SS_s_br_cash_blessed_spiritshot_d = 26050,
    SS_s_br_cash_spiritshot_s = 26059,
    SS_s_br_cash_soulshot_d = 26060,
    SS_s_br_cash_soulshot_s = 26064,
    SS_SUMMON_COMPETE_AGIT1 = 15283,
    SS_SUMMON_COMPETE_AGIT2 = 15284,
    SS_SUMMON_COMPETE_AGIT3 = 15285,
    SS_EV_CARD_SNAKE_A = 17123,
    SS_EV_CARD_SNAKE_A_SUB = 17124,
    SS_EV_CARD_SNAKE_B = 17125,
    SS_EV_CARD_SNAKE_B_SUB = 17126,
    SS_RAID_CURSE1 = 4515,
    SS_RAID_CURSE2 = 4215,
    SS_GAMBLE_REWARD_SKILL_START = 16135,
    SS_GAMBLE_REWARD_SKILL_END = 16146,
    SS_MASTER_YOGI_A = 17214,
    SS_MASTER_YOGI_B = 17216,
    SS_FISHING_SHOT_NONE = 38153,
    SS_FISHING_SHOT_HIGH = 38154,
    SS_BLOCK_BOT_PENARTY = 55031,
};

// TotalChecker2015 TPI 0x75ea6. L2constants.h:3215.
enum AgitDecoActive : std::int32_t {
    ADA_NON_ACTIVE = 0,
    ADA_ACTIVE = 1,
};

// TotalChecker2015 TPI 0x75eac. L2constants.h:3233.
enum AirShipMoveType : std::int32_t {
    ASMT_MOVE = 0,
    ASMT_STOP = 1,
    ASMT_UP = 2,
    ASMT_DOWN = 3,
    ASMT_TELEPORT = 4,
    ASMT_MAX = 5,
};

// TotalChecker2015 TPI 0x596f2. L2constants.h:3259.
enum TradeUpdateType : std::int32_t {
    TR_UPDATE_ADD_ITEM = 1,
    TR_UPDATE_DELETE_ITEM = 2,
    TR_UPDATE_UPDATE_ITEM = 3,
};

// TotalChecker2015 TPI 0x4907c. L2constants.h:3267.
enum CharacterCreateFailType : std::int32_t {
    CFT_GENERAL = 0,
    CFT_EXCEED_MAX_NUM = 1,
    CFT_DUPLICATE_NAME = 2,
    CFT_WRONG_NAME_LENGTH = 3,
    CFT_WRONG_NAME_STRING = 4,
    CFT_PROHIBIT_CHAR_CREATION = 5,
    CFT_PROHIBIT_FIRST_CHAR_CREATION = 6,
    CFT_DUAL_COUNTRY_NAME = 7,
    CFT_EXCEED_MAX_NUM_FOR_FREE_USER = 8,
};

// TotalChecker2015 TPI 0x54b0e. L2constants.h:3299.
enum ReuseDelayIndex : std::int32_t {
    RDI_MainJob = 0,
    RDI_AllJob = 4,
    RDI_MAX = 5,
};

// TotalChecker2015 TPI 0x75ec0. L2constants.h:3334.
enum AttributeResult : std::int32_t {
    ATTR_ENCHAN_POSSIBLE = 0,
    ATTR_ENCHAN_OPPOSITE_ATTR = 1,
    ATTR_ENCHAN_MAX_NUM_WEAPON = 2,
    ATTR_ENCHAN_MAX_NUM_ARMOR = 3,
    ATTR_ENCHAN_MAX_ATTR = 4,
    ATTR_ENCHAN_IMPOSSIBLE = 5,
    ATTR_ENCHAN_FULL = 6,
};

// TotalChecker2015 TPI 0x49089. L2constants.h:3357.
enum ChinaFatigueSystemType : std::int32_t {
    CFST_NONE = 0,
    CFST_NO_FATIGUE = 1,
    CFST_SEMI_FATIGUE = 2,
    CFST_FULL_FATIGUE = 3,
    CFST_MAX = 4,
};

// TotalChecker2015 TPI 0x524c9. L2constants.h:3447.
enum UserBookmarkConstants : std::int32_t {
    UBC_MAX_ICON_TITLE_LENGTH = 4,
    UBC_MAX_SLOT_TITLE_LENGTH = 64,
};

// TotalChecker2015 TPI 0x75c51. L2constants.h:3604.
enum GAMEGUARD_CHECK_TYPE : std::int32_t {
    GGCT_NORMAL = 0,
    GGCT_QUICK_REPLAY = 1,
    GGCT_ERROR = 2,
};

// TotalChecker2015 TPI 0x75c55. L2constants.h:3615.
enum CLIENT_EFFECT_TYPE : std::int32_t {
    ZONE_INFO_EFFECT = 0,
    FOG_INFO_EFFECT = 1,
    SCREEN_EFFECT = 2,
};

// TotalChecker2015 TPI 0x75c5d. L2constants.h:3621.
enum ZONE_INFO_TYPE : std::int32_t {
    ZIT_FREA = 0,
    ZIT_MAX = 1,
};

// TotalChecker2015 TPI 0x687e6. L2constants.h:3627.
enum NPC_STATE : std::int32_t {
    NPC_STATE_HUNGRY = 100,
    NPC_STATE_NOT_HUNGRY = 101,
};

// TotalChecker2015 TPI 0x75c65. L2constants.h:3632.
enum AGIT_PACKET_TYPE : std::int32_t {
    AA_NONE = 0,
    AA_INITIALIZE_AGIT_WND = 1,
    AA_AGIT_DETAIL_INFO = 2,
    AA_MYAGIT = 3,
    AA_REGISTER_STEP1 = 4,
    AA_REGISTER_STEP2 = 5,
    AA_REGISTER_STEP3 = 6,
    AA_REGISTER_CANCEL_CONFIRM = 7,
    AA_REGISTER_CANCEL_PROC = 8,
    AA_APPLY_LOT_CANCEL_CONFIRM = 9,
    AA_APPLY_LOT_CANCEL_PROC = 10,
    AA_APPLY_BID_CANCEL_CONFIRM = 11,
    AA_APPLY_BID_CANCEL_PROC = 12,
    AA_BID_STEP1 = 13,
    AA_BID_STEP2 = 14,
    AA_BID_STEP3 = 15,
    AA_REBID_STEP1 = 16,
    AA_LOT_AGIT_LIST = 17,
    AA_LOT_AGIT_APPLY_STEP1 = 18,
    AA_LOT_AGIT_APPLY_STEP2 = 19,
    AA_AUCTION_AGIT_LIST = 20,
    AA_MAX = 21,
};

// TotalChecker2015 TPI 0x75c77. L2constants.h:3657.
enum AGIT_TYPE : std::int32_t {
    AGIT_TYPE_NONE = 0,
    AGIT_TYPE_MY_AUC = 1,
    AGIT_TYPE_MY_LOT = 2,
    AGIT_TYPE_REGISTERED = 3,
    AGIT_TYPE_WISH_AUC = 4,
    AGIT_TYPE_WISH_LOT = 5,
    AGIT_TYPE_LOT_LIST = 6,
};

// TotalChecker2015 TPI 0x69d78. L2constants.h:3668.
enum OLYMPIAD_PACKET_TYPE : std::int32_t {
    GO_GAME_LIST = 0,
    GO_GAME_RESULT = 1,
    GO_GAME_RESULT_V2 = 2,
    GO_MAX = 3,
};

// TotalChecker2015 TPI 0x75c85. L2constants.h:3684.
enum PledgeGameRuleType : std::int32_t {
    PGRT_SURVIVAL = 0,
    PGRT_MAX = 1,
};

// TotalChecker2015 TPI 0x75c8d. L2constants.h:3693.
enum DirectionFlag : std::int32_t {
    DF_NONE = 0,
    DF_NORTH = 1,
    DF_NORTH_EAST = 3,
    DF_EAST = 2,
    DF_SOUTH_EAST = 6,
    DF_SOUTH = 4,
    DF_SOUTH_WEST = 12,
    DF_WEST = 8,
    DF_NORTH_WEST = 9,
};

// TotalChecker2015 TPI 0x75c93. L2constants.h:3706.
enum TurningFlag : std::int32_t {
    TF_NONE = 0,
    TF_LEFT = -1,
    TF_RIGHT = 1,
};

namespace GaugeType {

// TotalChecker2015 TPI 0x5473c. L2constants.h:3715.
enum Enum : std::int32_t {
    SKILL = 0,
    BOW = 1,
    BREATH = 2,
    FOOD = 3,
};

} // namespace GaugeType

namespace AttackFlag {

// TotalChecker2015 TPI 0x75c97. L2constants.h:3726.
enum Enum : std::int32_t {
    MISS = 1,
    SHIELD_DEFENSE = 2,
    CRITICAL = 4,
    SPIRIT = 8,
};

} // namespace AttackFlag

namespace ClassLevel {

// TotalChecker2015 TPI 0x75c9d. L2constants.h:3738.
enum Enum : std::int32_t {
    LevelNone = 0,
    LevelOne = 1,
    LevelTwo = 2,
    LevelThree = 3,
    LevelFour = 4,
    LevelFive = 5,
};

} // namespace ClassLevel

namespace TeleportType {

// TotalChecker2015 TPI 0x5260e. L2constants.h:3751.
enum Enum : std::int32_t {
    TT_NORMAL = 0,
    SUMMON_ILLUSION = 1,
};

} // namespace TeleportType

namespace DoppelGangerType {

// TotalChecker2015 TPI 0x5473e. L2constants.h:3760.
enum Enum : std::int32_t {
    NONE = 0,
    DECOY = 1,
    HALLUCINATION = 2,
};

} // namespace DoppelGangerType

namespace UltimateSkill {

// TotalChecker2015 TPI 0x1c5d2. L2constants.h:3770.
enum Info : std::int32_t {
    LEVEL_ZERO = 0,
    LEVEL_ONE = 1,
    LEVEL_TWO = 2,
    LEVEL_THREE = 3,
    LEVEL_MAX = 4,
    DEFAULT_GAUGE = 3,
    MAX_ADD_ULTIMATE_SKILL_GAUGE = 150,
};

} // namespace UltimateSkill

namespace DynamicQuestType {

// TotalChecker2015 TPI 0x74c21. L2constants.h:3807.
enum JobType : std::int32_t {
    START = 0,
    FINISH = 1,
    PROGRESS_INFO = 2,
    SCORE_BOARD = 3,
    CLIENT_REQUEST_HTML = 4,
    CR_MAX = 5,
};

// TotalChecker2015 TPI 0x22e5e. L2constants.h:3839.
enum : std::int32_t {
    MAX_PROGRESS_ZONE_QUEST = 20,
};

} // namespace DynamicQuestType

namespace SubJob {

// TotalChecker2015 TPI 0x3e006. L2constants.h:3847.
enum SubJobType : std::int32_t {
    MAIN_CLASS = 0,
    DUAL_CLASS = 1,
    SUB_CLASS = 2,
};

} // namespace SubJob

namespace Post {

// TotalChecker2015 TPI 0x4d6a3. L2constants.h:3886.
enum PostUserBitType : std::int32_t {
    ConnectedNobody = 0,
    ConnectedSender = 1,
    ConnectedReceiver = 2,
};

// TotalChecker2015 TPI 0x75cbd. L2constants.h:3893.
enum PostWriteFailureType : std::int32_t {
    FailedByReceivedCount = -1,
    FailedByUserInvalid = -2,
    FailedByInventoryError = -3,
    FailedByBlocked = -4,
    FailedByUnknown = -5,
    FailedByOverMyMailCount = -6,
    FailedByMeToMe = -7,
    FailedBySentCount = -8,
    FailedByAlreadySent = -9,
    FailedByNotFriend = -10,
};

// TotalChecker2015 TPI 0x56576. L2constants.h:3907.
enum SenderType : std::int32_t {
    UserToUser = 0,
    NewsCaster = 1,
    NPC = 2,
    BirthdayMail = 3,
    CommissionToUserDelete = 4,
    CommissionToUserBuy = 5,
    Mentoring = 6,
    InGameShopGift = 7,
    KalieEventReward = 8,
    LevelUpReward = 9,
    PledgeJoinReject = 10,
    DormantUserGift = 11,
    NewbieGift = 12,
    ToAdminWeb = 13,
    MobileGift = 14,
    BalthusEventReward = 15,
};

// TotalChecker2015 TPI 0x75cd3. L2constants.h:3927.
enum PostSystemMsgNum : std::int32_t {
    CommissionSellerID = 2670,
    BuyTitle = 3490,
    BuyContents = 3491,
    DeleteTitle = 3492,
    DeleteContents = 3493,
};

// TotalChecker2015 TPI 0x75cdb. L2constants.h:3936.
enum PostNpcID : std::int32_t {
    PostNPCBirthdayMail = 32600,
    PostNPCMentoring = 33587,
};

} // namespace Post

namespace CommissionProcess {

// TotalChecker2015 TPI 0x75cf8. L2constants.h:4055.
enum Enum : std::int32_t {
    CpRegist = 0,
    CpModify = 1,
    CpDelete = 2,
    CpBuy = 3,
    CpList = 4,
    CpRegistered = 5,
    CpSearch = 6,
    CpTimerDelete = 7,
    CpProcessMAX = 8,
};

} // namespace CommissionProcess

namespace CommissionData {

// TotalChecker2015 TPI 0x75d0e. L2constants.h:4107.
enum Enum : std::int32_t {
    CdNothing = -2,
    CdFail = -1,
    CdDefault = 0,
    CdUserPass = 0,
    CdOK = 1,
    CdMAX = 2,
};

} // namespace CommissionData

namespace BR_CashShopProduct {

// TotalChecker2015 TPI 0x4d6b8. L2constants.h:4133.
enum Enum : std::int32_t {
    BR_None = -1,
    BR_Product = 0,
    BR_Recent = 1,
    BR_Basket = 2,
    BR_Shop_Max = 3,
};

// TotalChecker2015 TPI 0x75d20. L2constants.h:4144.
enum DailyVipGift : std::int32_t {
    GIFT_ID_MIN = 100001,
    GIFT_ID_MAX = 100007,
};

} // namespace BR_CashShopProduct

namespace BuySellType {

// TotalChecker2015 TPI 0x6c3c6. L2constants.h:4191.
enum Enum : std::int32_t {
    Nothing = 0,
    One = 1,
    Both = 2,
    Max = 3,
};

} // namespace BuySellType

namespace StatusUpdateType {

// TotalChecker2015 TPI 0x108c. L2constants.h:4202.
enum : std::int32_t {
    Normal = 0,
    HPUpdate = 1,
    CPUpdate = 2,
    MPUpdate = 3,
    DamageText = 4,
    EXPUpdate = 5,
    DotEffect = 6,
    Max = 7,
};

} // namespace StatusUpdateType

namespace VitalityDefine {

// TotalChecker2015 TPI 0x43435. L2constants.h:4247.
enum : std::int32_t {
    MAX_VITALITY = 140000,
    REGEN_VITALITY = 35000,
    RESET_WDAY = 3,
    RESET_HOUR = 6,
    RESET_MIN = 30,
    RESET_PERIOD_VP = 604800,
    RESET_PERIOD_RC = 604800,
};

} // namespace VitalityDefine

namespace TacticalSign {

// TotalChecker2015 TPI 0x3e06b. L2constants.h:4275.
enum TypeEnum : std::int32_t {
    NOT_SIGNED = 0,
    MAX_COUNT = 4,
};

} // namespace TacticalSign

namespace FlyMove {

// TotalChecker2015 TPI 0x52635. L2constants.h:4284.
enum MovingType : std::int32_t {
    START = 0,
    SELECT_MOVE = 1,
    ONE_WAY_MOVE = 2,
};

} // namespace FlyMove

namespace VitalityLevel {

// TotalChecker2015 TPI 0x75d4a. L2constants.h:4296.
enum UserVitalityLevel : std::int32_t {
    UVL_VITALITY_LEVEL_1 = 1,
    UVL_VITALITY_LEVEL_2 = 2,
    UVL_VITALITY_LEVEL_3 = 3,
    UVL_VITALITY_LEVEL_MAX = 4,
};

} // namespace VitalityLevel

namespace PremiumAccountType {

// TotalChecker2015 TPI 0x75d60. L2constants.h:4359.
enum Enum : std::int32_t {
    PAT_NORMAL_ACCOUNT = 0,
    PAT_PREMIUM_ACCOUNT = 1,
    PAT_MAX = 2,
};

} // namespace PremiumAccountType

namespace ShapeShiftingType {

// TotalChecker2015 TPI 0x10a3. L2constants.h:4372.
enum Enum : std::int32_t {
    SST_INVALID = 0,
    SST_WEAPON = 1,
    SST_ARMOR = 2,
    SST_HAIR_ACCESSARY = 3,
    SST_ALLITEM = 4,
    SST_MAX = 5,
};

} // namespace ShapeShiftingType

namespace ShapeShiftingDetailType {

// TotalChecker2015 TPI 0x10a5. L2constants.h:4386.
enum Enum : std::int32_t {
    SSDT_INVALID = 0,
    SSDT_NORMAL = 1,
    SSDT_BLESSED = 2,
    SSDT_FIXED = 3,
    SSDT_RESTORE = 4,
    SSDT_MAX = 5,
};

} // namespace ShapeShiftingDetailType

namespace ShapeShiftingTargetItemPutResult {

// TotalChecker2015 TPI 0x4d6bf. L2constants.h:4400.
enum Enum : std::int32_t {
    SSTIPR_FAIL = 0,
    SSTIPR_SUCCESS = 1,
    SSTIPR_MAX = 2,
};

} // namespace ShapeShiftingTargetItemPutResult

namespace ShapeShiftingExtractionItemPutResult {

// TotalChecker2015 TPI 0x4d6c1. L2constants.h:4410.
enum Enum : std::int32_t {
    SSEIPR_FAIL = 0,
    SSEIPR_SUCCESS = 1,
    SSEIPR_MAX = 2,
};

} // namespace ShapeShiftingExtractionItemPutResult

namespace ShapeShiftingResult {

// TotalChecker2015 TPI 0x4d6c3. L2constants.h:4420.
enum Enum : std::int32_t {
    SSR_FAIL = 0,
    SSR_SUCCESS = 1,
    SSR_CANCELED = 2,
    SSR_MAX = 3,
};

} // namespace ShapeShiftingResult

namespace MacroSystem {

// TotalChecker2015 TPI 0x75d82. L2constants.h:4455.
enum EditType : std::int32_t {
    MACRO_DELETE = 0,
    MACRO_CREATE = 1,
    MACRO_MODIFY = 2,
    MACRO_MAX = 3,
};

} // namespace MacroSystem

namespace CuriousHouse {

// TotalChecker2015 TPI 0x75d86. L2constants.h:4466.
enum : std::int32_t {
    CH_NONE = 0,
    CH_PLAYER1 = 1,
    CH_PLAYER2 = 2,
    CH_PLAYER3 = 3,
    CH_PLAYER4 = 4,
    CH_PLAYER5 = 5,
    CH_PLAYER6 = 6,
    CH_PLAYER7 = 7,
    CH_PLAYER8 = 8,
    CH_PLAYER9 = 9,
    CH_PLAYER10 = 10,
    CH_PLAYER11 = 11,
    CH_PLAYER12 = 12,
    CH_PLAYER13 = 13,
    CH_PLAYER14 = 14,
    CH_PLAYER15 = 15,
    CH_PLAYER16 = 16,
    CH_PLAYER17 = 17,
    CH_PLAYER18 = 18,
    CH_WINNER = 100,
};

} // namespace CuriousHouse

namespace Beautyshop {

// TotalChecker2015 TPI 0x75d92. L2constants.h:4492.
enum itemType : std::int32_t {
    BSIT_HAIR = 0,
    BSIT_FACE = 1,
    BSIT_ALL = 2,
    BSIT_MAX = 3,
};

// TotalChecker2015 TPI 0x75d96. L2constants.h:4499.
enum ViewType : std::int32_t {
    BSVT_CURRLOOK = 0,
    BSVT_PREVLOOK = 1,
    BSVT_ORIGLOOK = 2,
    BSVT_MAX = 3,
};

// TotalChecker2015 TPI 0x4d6c8. L2constants.h:4506.
enum ShopType : std::int32_t {
    BSST_BUYSHOP = 0,
    BSST_RESTORESHOP = 1,
    BSST_MAX = 2,
};

// TotalChecker2015 TPI 0x75d9c. L2constants.h:4512.
enum colorType : std::int32_t {
    BSCT_BASECOLOR = 101,
    BSCT_MAX = 102,
};

// TotalChecker2015 TPI 0x4d6cb. L2constants.h:4517.
enum RegistType : std::int32_t {
    BSRT_NPCSERVER_DISCONNECTED = -1,
    BSRT_FAIL = 0,
    BSRT_SUCCESS = 1,
};

} // namespace Beautyshop

namespace EventCampaignType {

// TotalChecker2015 TPI 0x75da6. L2constants.h:4532.
enum JobType : std::int32_t {
    START = 0,
    FINISH = 1,
    CLIENT_REQUEST_HTML = 2,
    PROGRESS_INFO = 3,
    SCORE_BOARD = 4,
    CR_MAX = 5,
};

// TotalChecker2015 TPI 0x75dad. L2constants.h:4542.
enum ProgressInfoType : std::int32_t {
    DURING_PROGRESS = 0,
    REWARD_START = 1,
    REWARD_RECEIVED = 2,
    FAILED = 3,
    MAX_INFOTYPE = 4,
};

} // namespace EventCampaignType

namespace PledgeJoinSystem {

// TotalChecker2015 TPI 0x75db5. L2constants.h:4567.
enum PledgeTrend : std::int32_t {
    PT_ALL = -1,
    PT_FAMILIAR = 0,
    PT_PLEDGE_WAR = 1,
    PT_HUNT = 2,
    PT_ETC = 3,
    PT_MAX = 4,
};

// TotalChecker2015 TPI 0x75dbf. L2constants.h:4577.
enum PledgeJoinSystemCode : std::int32_t {
    PJSC_EXCEED_ERROR = 0,
    PJSC_SET_LIMIT_PLEDGE_COUNT = 1,
    PJSC_GET_USER_TIME = 2,
    PJSC_GET_PLEDGE_TIME = 3,
    PJSC_MAX = 4,
};

// TotalChecker2015 TPI 0x75dc7. L2constants.h:4586.
enum PledgeJoinExist : std::int32_t {
    PJE_NONE = 0,
    PJE_PLEDGE = 1,
    PJE_WAITING = 2,
    PJE_DRAFT = 3,
    PJE_WAITING_AND_DRAFT = 4,
    PJE_MAX = 5,
};

} // namespace PledgeJoinSystem

namespace DailyPostSystem {

// TotalChecker2015 TPI 0x5263c. L2constants.h:4629.
enum Enum : std::int32_t {
    DPS_NONE = 0,
    DPS_SING_OF_AVENTURER = 1,
    DPS_FOR_FREE_USER = 2,
};

} // namespace DailyPostSystem

// TotalChecker2015 TPI 0x51fd7. L2constants.h:4702.
enum POTENTIAL_SKILL_TYPE : std::int32_t {
    PST_INVALID = -1,
    PST_TYPE1 = 1,
    PST_TYPE2 = 2,
    PST_TYPE3 = 3,
    PST_TYPE_MAX = 4,
};

// TotalChecker2015 TPI 0x68dfc. L2constants.h:4711.
enum PACKETMINIMIZE_PARTY_SMALL_WINDOW : std::int32_t {
    PPSW_CP = 0,
    PPSW_BASECP = 1,
    PPSW_HP = 2,
    PPSW_BASEHP = 3,
    PPSW_MP = 4,
    PPSW_BASEMP = 5,
    PPSW_LEVEL = 6,
    PPSW_CLASS = 7,
    PPSW_PARTYSUBSTITUTE = 8,
    PPSW_VITALPOINT = 9,
    PPSW_MAX = 10,
};

namespace ChattingRelation {

// TotalChecker2015 TPI 0x5263e. L2constants.h:4730.
enum Relation : std::int32_t {
    CRT_NONE = 0,
    CRT_FRIEND = 1,
    CRT_PLEDGE = 2,
    CRT_MENTORING = 4,
    CRT_ALLIANCE = 8,
    CRT_GM = 16,
    CRT_ME = 32,
    CRT_MOBILE = 64,
};

} // namespace ChattingRelation

namespace MultiSellPointItem {

// TotalChecker2015 TPI 0x18db9. L2constants.h:4748.
enum MultiSellPointItemType : std::int32_t {
    MSIT_NONE = 0,
    MSIT_RAID_POINT = -500,
    MSIT_FIELD_CYCLE_POINT = -400,
    MSIT_PVP_POINT = -300,
    MSIT_PLEDGE_POINT = -200,
    MSIT_PCCAFE_POINT = -100,
};

} // namespace MultiSellPointItem

namespace FishingInfo {

// TotalChecker2015 TPI 0x7347b. L2constants.h:4762.
enum BitFlag : std::int32_t {
    FI_DAY = 0,
    FI_BEGINNER = 1,
    FI_EVENT = 2,
    FI_MAX = 3,
};

} // namespace FishingInfo

namespace AlchemyConst {

// TotalChecker2015 TPI 0x22f11. L2constants.h:4775.
enum Type : std::int32_t {
    AT_NONE = -1,
    AT_ROLLY = 0,
    AT_CONV = 1,
    AT_MAX = 2,
};

// TotalChecker2015 TPI 0x5658c. L2constants.h:4783.
enum AlchemyConvResultType : std::int32_t {
    ACT_SUCCESS = 0,
    ACT_FAIL = 1,
    ACT_FAIL_ITEM = 2,
    ACT_FAIL_SKILL = 3,
    ACT_FAIL_INVALID_ACTION = 4,
    ACT_MAX = 5,
};

// TotalChecker2015 TPI 0x5658e. L2constants.h:4793.
enum AlchemyRollyResultType : std::int32_t {
    ARRT_SUCCESS_NORMAL = 0,
    ARRT_SUCCESS_RANDOM = 1,
    ARRT_FAIL_ITEM_AMOUNT = 2,
    ARRT_FAIL_ITEM_INVALID = 3,
    ARRT_FAIL_SKILL_INVALID = 4,
    ARRT_FAIL_SKILL_REMAIN_TIME = 5,
    ARRT_FAIL_INVALID_ACTION = 6,
    ARRT_MAX = 7,
};

// TotalChecker2015 TPI 0x56590. L2constants.h:4805.
enum AlchemyRollyRewardType : std::int32_t {
    ALRRT_NORMAL_SUCCESS = 0,
    ALRRT_MULTI_SUCCESS = 1,
    ALRRT_RANDOM_SUCCESS = 2,
};

} // namespace AlchemyConst

namespace ItemAssembleType {

// TotalChecker2015 TPI 0x42c8e. L2constants.h:4906.
enum Type : std::int32_t {
    NONE = 0,
    VAIRATION = 1,
    ATTRIBUTE = 2,
    ENCHANTOPTION = 4,
    SHAPESHIFT = 8,
    ENSOUL = 16,
    USECOUNT = 32,
    REUSEDELAY = 64,
    ALL = 127,
};

} // namespace ItemAssembleType

namespace RelationSendType {

// TotalChecker2015 TPI 0x51fd9. L2constants.h:4924.
enum Type : std::int32_t {
    NONE = 0,
    NOTHING = 1,
    ONE = 2,
    ALL = 4,
};

} // namespace RelationSendType

namespace PacketType {

// TotalChecker2015 TPI 0x75e1b. L2constants.h:4935.
enum NPCStateInfo : std::int32_t {
    SI_NONE = 0,
    SI_ISDEAD = 1,
    SI_ISCOMBATMODE = 2,
    SI_MOVEMODE = 4,
    SI_MOVETYPE1 = 8,
    SI_MOVETYPE2 = 16,
    SI_MOVETYPE3 = 32,
};

// TotalChecker2015 TPI 0x687ea. L2constants.h:4946.
enum PetInfo : std::int32_t {
    PI_NONE = 0,
    PI_ISENEMY = 1,
    PI_STOPMODE = 2,
    PI_MOVEMODE = 4,
    PI_ISCOMBATMODE = 8,
    PI_ISALIVE = 16,
    PI_ISRIDEABLE = 32,
};

// TotalChecker2015 TPI 0x56674. L2constants.h:4957.
enum EtcStatusUpdate : std::int32_t {
    ESU_NONE = 0,
    ESU_PROHIBITCHAT = 1,
    ESU_DANGERZONE = 2,
    ESU_KEEPEXP = 4,
};

} // namespace PacketType

// TotalChecker2015 TPI 0x75e2b. L2constants.h:4967.
enum CheckAbusingUserType : std::int32_t {
    CAUT_NONE = 0,
    CAUT_IP = 1,
    CAUT_MAX = 2,
};

namespace LuckyGameSystem {

// TotalChecker2015 TPI 0x75e3b. L2constants.h:5027.
enum RewardGrade : std::int32_t {
    RG_INVALID = -1,
    RG_NONE = 0,
    RG_BONUS = 1,
    RG_D = 2,
    RG_C = 3,
    RG_B = 4,
    RG_A = 5,
    RG_MAX = 6,
};

// TotalChecker2015 TPI 0x4d728. L2constants.h:5043.
enum BettingResultType : std::int32_t {
    BRT_FAIL_BY_TICKET = -1,
    BRT_FAIL_BY_INVEN = -2,
    BRT_FAIL_BY_NPC_DISTANCE = -3,
    BRT_FAIL_BY_SYSTEM = -4,
    BRT_FAIL_BY_ITEM_MANI = -5,
    BRT_FAIL_BY_LOADING_INFO = -6,
    BRT_FAIL = 0,
    BRT_SUCCESS = 1,
    BRT_MAX = 2,
};

// TotalChecker2015 TPI 0x6ea93. L2constants.h:5058.
enum RevCountType : std::int32_t {
    RCT_INFINITY = -1,
    RCT_CLOSING = -2,
    RCT_MAX = -3,
};

// TotalChecker2015 TPI 0x6ea95. L2constants.h:5066.
enum LuckyFactorType : std::int32_t {
    LFT_NONE = 0,
    LFT_COMMON_PICK = -1,
    LFT_PREMIUM_PICK = -2,
    LFT_COMMON_REV = -3,
    LFT_PREMIUM_REV = -4,
    LFT_MAX = -3,
};

} // namespace LuckyGameSystem

// TotalChecker2015 TPI 0x75e4f. L2constants.h:5082.
enum ServerPayType : std::int32_t {
    PAY_TYPE_NORMAL = 0,
    PAY_TYPE_DUAL = 1,
    PAY_TYPE_FREE = 2,
};

namespace Henna {

// TotalChecker2015 TPI 0x75e51. L2constants.h:5093.
enum HennaSlotType : std::int32_t {
    HST_NONE = 0,
    HST_HENNA1 = 1,
    HST_HENNA2 = 2,
    HST_HENNA3 = 3,
    HST_HENNA_PREMIUM = 10,
    HST_MAX = 11,
};

} // namespace Henna

namespace INIType {

// TotalChecker2015 TPI 0x48fb4. L2constants.h:5117.
enum Enum : std::int32_t {
    IT_WINDOWSINFO = 0,
    IT_CHATFILTER = 1,
    IT_OPTION = 2,
    IT_MAX = 3,
};

} // namespace INIType

namespace ChannelChattingType {

// TotalChecker2015 TPI 0x63add. L2constants.h:5128.
enum PledgeType : std::int32_t {
    PT_ENTERWORLD = 1,
    PT_CREATE = 2,
    PT_JOIN = 3,
    PT_WITHDRAW = 4,
    PT_OUST = 5,
    PT_DISSMISS = 6,
    PT_MAX = 7,
};

} // namespace ChannelChattingType

namespace NPSystem {

// TotalChecker2015 TPI 0x48fd3. L2constants.h:5549.
enum ELoginErrorCode : std::int32_t {
    LEC_NONE = 0,
    LEC_GP_SUCCESS = -1,
    LEC_NP_SUCCESS = 0,
    LEC_GPFAIL_AUTHPLAYGAME = 1,
    LEC_GPFAIL_DUPACCOUNT = 2,
    LEC_GPFAIL_APPTOKEN_REQ = 3,
    LEC_GPFAIL_APPTOKEN_ACK = 4,
    LEC_NPFAIL_LOGINSTEPINIT = 5,
    LEC_NPFAIL_GSLOGIN_REQ = 6,
    LEC_NPFAIL_GSLOGIN_ACK_INVALIDARG = 7,
    LEC_NPFAIL_GSLOGIN_ACK_INVALIDRESPONSECODE = 8,
    LEC_NPFAIL_GSLOGIN_ACK_INVALIDRESPONSEBODY = 9,
    LEC_NPFAIL_GSLOGIN_ACK_INVALIDUSERSOCKET = 10,
    LEC_NPFAIL_GSLOGIN_ACK_FAILTOSETLOGINCOMPLETE = 11,
    LEC_NPFAIL_GCTOKEN_REQ = 12,
    LEC_NPFAIL_GCTOKEN_ACK_INVALIDARG = 13,
    LEC_NPFAIL_GCTOKEN_ACK_INVALIDRESPONSECODE = 14,
    LEC_NPFAIL_GCTOKEN_ACK_INVALIDRESPONSEBODY = 15,
    LEC_NPFAIL_GCTOKEN_ACK_INVALIDUSERSOCKET = 16,
    LEC_NPFAIL_LOGINCOMPLETE = 17,
    LEC_NPFAIL_LOGINTIMEOUT = 18,
    LEC_NPFAIL_NOTIFY_DUPLIECATEDLOGIN = 19,
};

} // namespace NPSystem

namespace NEW_AUTH_USER_DATA {

// TotalChecker2015 TPI 0x75e75. L2constants.h:5589.
enum DATA_SECTION_INFO : std::int32_t {
    MAX_NUMBER_OF_SERVER = 50,
    NUMBER_OF_CHARACTER_SECTION_SIZE = 100,
    ATTENDANCE_SECTION_SIZE = 32,
    USED_ATTENDANCE_SECTION_SIZE = 14,
};

} // namespace NEW_AUTH_USER_DATA

namespace TutorialIcon {

// TotalChecker2015 TPI 0x4902e. L2constants.h:5649.
enum TutorialIconType : std::int32_t {
    TIT_NONE = -1,
    TIT_QUESTION_MARK = 0,
    TIT_NOTICE_MARK = 1,
    TIT_MAX = 2,
};

} // namespace TutorialIcon

namespace MultisellConst {

// TotalChecker2015 TPI 0x6d9f8. L2constants.h:5662.
enum MultisellTypeBit : std::int32_t {
    MTB_SHOW_ENSOUL = 1,
    MTB_KEEP_ENSOUL = 2,
    MTB_KEEP_ENCHANT = 4,
    MTB_SHAPE_SHIFT = 8,
    MTB_VARIATION_ITEM = 16,
    MTB_IS_SHOW_ALL = 32,
};

} // namespace MultisellConst

// TotalChecker2015 TPI 0x75e9c. L2constants.h:5689.
enum ARENA_MATCHING_RESULT : std::int32_t {
    AMR_FAIL_ETC = 0,
    AMR_SUCCESS = 1,
    AMR_ALREADY_MATCHING = 2,
    AMR_ALREADY_CONFIRM_MATCH = 3,
    AMR_NOT_IN_MATCHING = 4,
    AMR_ALREADY_CONFIRM_CLASS = 5,
    AMR_NO_ARENA = 6,
    AMR_NOT_IN_SELECT_CLASS_STATE = 7,
    AMR_MAX = 8,
};

// TotalChecker2015 TPI 0x43737. L2constants.h:5749.
enum E_TODOLIST_TIME_TYPE : std::int32_t {
    E_TODOLIST_TIME_TYPE_INVALID = 0,
    E_TODOLIST_TIME_TYPE_ALLDAY = 0,
    E_TODOLIST_TIME_TYPE_CONSTANT = 99,
    E_TODOLIST_TIME_TYPE_MAX = 100,
};

// TotalChecker2015 TPI 0x72a15. L2constants.h:5761.
enum E_TODOLIST_LEVEL_TYPE : std::int32_t {
    E_TODOLIST_LEVEL_TYPE_INVALID = 0,
    E_TODOLIST_LEVEL_TYPE_OVER = -1,
    E_TODOLIST_LEVEL_TYPE_UNLIMIT = 0,
    E_TODOLIST_LEVEL_TYPE_START = 1,
    E_TODOLIST_LEVEL_TYPE_MAX = 2,
};

// TotalChecker2015 TPI 0x4d73b. L2constants.h:5793.
enum E_AUTOSOULSHOT_UI_TYPE : std::int32_t {
    E_AUTOSOULSHOT_UI_TYPE_SLOT_X_ODER_O = 0,
    E_AUTOSOULSHOT_UI_TYPE_SLOT_O_ODER_O = 1,
    E_AUTOSOULSHOT_UI_TYPE_SLOT_X_ODER_X = 2,
    E_AUTOSOULSHOT_UI_TYPE_SLOT_O_ODER_X = 3,
};

// TotalChecker2015 TPI 0x75ebc. L2constants.h:5802.
enum E_AUTOSOULSHOT_TYPE : std::int32_t {
    E_AUTOSOULSHOT_TYPE_SOULSHOT = 0,
    E_AUTOSOULSHOT_TYPE_SPIRITSHOT = 1,
    E_AUTOSOULSHOT_TYPE_SUMMON_SOULSHOT = 2,
    E_AUTOSOULSHOT_TYPE_SUMMON_SPIRITSHOT = 3,
    E_AUTOSOULSHOT_TYPE_MAX = 4,
};

// TotalChecker2015 TPI 0x4d73d. L2constants.h:5816.
enum QUEUETICKET_REASONCODE : std::int32_t {
    QUEUETICKET_SYSTEMERROR = 0,
    QUEUETICKET_NO = 1,
    QUEUETICKET_WAIT = 2,
    QUEUETICKET_VIPPASS = 3,
    QUEUETICKET_FULL = 4,
};

namespace WorldAgit {

// TotalChecker2015 TPI 0x4d741. L2constants.h:5828.
enum WorldAgitType : std::int32_t {
    AUCTION = 0,
    COMPETE = 1,
    TEAMBATTLE = 2,
    MINIGAME = 3,
};

} // namespace WorldAgit

namespace MonsterBook {

// TotalChecker2015 TPI 0x75ec8. L2constants.h:5843.
enum OpenResultType : std::int32_t {
    MORT_FAIL = 0,
    MORT_SUCCESS = 1,
};

} // namespace MonsterBook

namespace UserStatusActive {

// TotalChecker2015 TPI 0x5670e. L2constants.h:5854.
enum UserStatusActiveType : std::int32_t {
    USA_NONE = 0,
    USA_SHOP = 1,
    USA_FISHING = 2,
};

} // namespace UserStatusActive

namespace UserStatusContents {

// TotalChecker2015 TPI 0x56710. L2constants.h:5864.
enum UserStatusContentsType : std::int32_t {
    USC_NONE = 0,
    USC_CASTLE_SIEGE = 1,
    USC_FORTRESS_SIEGE = 2,
    USC_AGIT_WAR = 3,
    USC_OLYMPIAD = 4,
    USC_PLEDGE_GAME = 5,
};

} // namespace UserStatusContents

namespace BuySellConst {

// TotalChecker2015 TPI 0x75ed7. L2constants.h:5879.
enum BuySellList : std::int32_t {
    BSL_NPC_SELL = 0,
    BSL_PC_SELL = 1,
    BSL_FAIL = 2,
};

} // namespace BuySellConst

namespace DormantConst {

// TotalChecker2015 TPI 0x56732. L2constants.h:5906.
enum DormantType : std::int32_t {
    DT_NORMAL = 0,
    DT_NEWBIE = 2,
    DT_DORMANT = 3,
};

} // namespace DormantConst

namespace CardUpDownGame {

// TotalChecker2015 TPI 0x75edf. L2constants.h:5917.
enum PickResultType : std::int32_t {
    PRT_FAIL_UP = 0,
    PRT_FAIL_DOWN = 1,
    PRT_FAIL_END_GAME = 2,
    PRT_SUCCESS = 3,
};

// TotalChecker2015 TPI 0x56734. L2constants.h:5933.
enum ResultFailType : std::int32_t {
    RFT_INVENTORY_FULL = 0,
    RFT_REQUEST_NOT_FINISHED = 1,
};

} // namespace CardUpDownGame

// TotalChecker2015 TPI 0x1019. L2constants.h:6025.
enum ActionFailEnum : std::int32_t {
    AF_DEFAULT = 0,
    AF_MOVE = 1,
    AF_USEGROUNDSKILL = 2,
};

// TotalChecker2015 TPI 0x4d649. L2constants.h:6033.
enum ArtifactEnchantResult : std::int32_t {
    AER_SUCCESS = 0,
    AER_FAIL = 1,
    AER_CONDITION_UNSATISFIED = 2,
};

// TotalChecker2015 TPI 0x4d68f. L2constants.h:6076.
enum HeroType : std::int32_t {
    HERO_NONE = 0,
    HERO_WAITING = 1,
    HERO_ACTIVE = 2,
    LEGENDARY_WAITING = 3,
    LEGENDARY_ACTIVE = 4,
};

// TotalChecker2015 TPI 0x4d698. L2constants.h:6086.
enum ChangeTargetType : std::int32_t {
    CTT_DEFAULT = 0,
    CTT_NEXT_TARGET = 1,
};

namespace EnsoulItem {

// TotalChecker2015 TPI 0x56571. L2constants.h:6103.
enum EnsoulItemSlotIndex : std::int32_t {
    EISI_INVALID = -1,
    EISI_START = 1,
};

} // namespace EnsoulItem

// TotalChecker2015 TPI 0x5c965. L2constants.h:6241.
enum AbnormalVisualEffectBitType : std::int32_t {
    AVEBT_NONE = 0,
    AVEBT_DOT_BLEEDING = 1,
    AVEBT_DOT_POISON = 2,
    AVEBT_DOT_FIRE = 3,
    AVEBT_DOT_WATER = 4,
    AVEBT_DOT_WIND = 5,
    AVEBT_DOT_SOIL = 6,
    AVEBT_STUN = 7,
    AVEBT_SLEEP = 8,
    AVEBT_SILENCE = 9,
    AVEBT_ROOT = 10,
    AVEBT_PARALYZE = 11,
    AVEBT_FLESH_STONE = 12,
    AVEBT_DOT_MP = 13,
    AVEBT_BIGHEAD = 14,
    AVEBT_DOT_FIRE_AREA = 15,
    AVEBT_CHANGE_TEXTURE = 16,
    AVEBT_BIGBODY = 17,
    AVEBT_FLOATING_ROOT = 18,
    AVEBT_DANCE_ROOT = 19,
    AVEBT_GHOST_STUN = 20,
    AVEBT_STEALTH = 21,
    AVEBT_SEIZURE1 = 22,
    AVEBT_SEIZURE2 = 23,
    AVEBT_MAGIC_SQUARE = 24,
    AVEBT_FREEZING = 25,
    AVEBT_SHAKE = 26,
    AVEBT_BLIND = 27,
    AVEBT_ULTIMATE_DEFENCE = 28,
    AVEBT_VP_UP = 29,
    AVEBT_REAL_TARGET = 30,
    AVEBT_DEATH_MARK = 31,
    AVEBT_TURN_FLEE = 32,
    AVEBT_INVINCIBILITY = 33,
    AVEBT_AIR_BATTLE_SLOW = 34,
    AVEBT_AIR_BATTLE_ROOT = 35,
    AVEBT_CHANGE_WP = 36,
    AVEBT_CHANGE_HAIR_G = 37,
    AVEBT_CHANGE_HAIR_P = 38,
    AVEBT_CHANGE_HAIR_B = 39,
    AVEBT_VP_KEEP = 40,
    AVEBT_STIGMA_OF_SILEN = 41,
    AVEBT_SPEED_DOWN = 42,
    AVEBT_FROZEN_PILLAR = 43,
    AVEBT_CHANGE_VES_S = 44,
    AVEBT_CHANGE_VES_C = 45,
    AVEBT_CHANGE_VES_D = 46,
    AVEBT_TIME_BOMB = 47,
    AVEBT_MP_SHIELD = 48,
    AVEBT_AIRBIND = 49,
    AVEBT_CHANGEBODY = 50,
    AVEBT_KNOCKDOWN = 51,
    AVEBT_NAVIT_ADVENT = 52,
    AVEBT_KNOCKBACK = 53,
    AVEBT_CHANGE_7ANNIVERSARY = 54,
    AVEBT_PAIN_ROOT = 55,
    AVEBT_DEPORT = 56,
    AVEBT_AURA_BUFF = 57,
    AVEBT_AURA_BUFF_SELF = 58,
    AVEBT_AURA_DEBUFF = 59,
    AVEBT_AURA_DEBUFF_SELF = 60,
    AVEBT_HURRICANE = 61,
    AVEBT_HURRICANE_SELF = 62,
    AVEBT_BLACK_MARK = 63,
    BR_AVEBT_SOUL_AVATAR = 64,
    AVEBT_CHANGE_GRADE_B = 65,
    BR_AVEBT_BEAM_SWORD_ONEHAND = 66,
    BR_AVEBT_BEAM_SWORD_DUAL = 67,
    AVEBT_NO_CHAT = 68,
    AVE_HERB_PA_UP = 69,
    AVE_HERB_MA_UP = 70,
    AVE_SEED_TALISMAN1 = 71,
    AVE_SEED_TALISMAN2 = 72,
    AVE_SEED_TALISMAN3 = 73,
    AVE_SEED_TALISMAN4 = 74,
    AVE_SEED_TALISMAN5 = 75,
    AVE_SEED_TALISMAN6 = 76,
    AVEBT_CURIOUS_HOUSE = 77,
    AVE_NGRADE_CHANGE = 78,
    AVE_DGRADE_CHANGE = 79,
    AVE_CGRADE_CHANGE = 80,
    AVE_BGRADE_CHANGE = 81,
    AVE_AGRADE_CHANGE = 82,
    AVEBT_SWEET_ICE_FLAKES = 83,
    AVEBT_FANTASY_ICE_FLAKES = 84,
    AVEBT_CHANGE_XMAS = 85,
    AVEBT_LEGEND_CARD = 86,
    AVEBT_DINOS = 87,
    AVEBT_WDAY_WEAPON = 88,
    AVEBT_WDAY_KING = 89,
    AVEBT_WDAY_QUEEN = 90,
    AVEBT_WDAY_ME = 91,
    AVEBT_STAR_GAMBLE_0 = 92,
    AVEBT_STAR_GAMBLE_1 = 93,
    AVEBT_STAR_GAMBLE_2 = 94,
    AVEBT_STAR_GAMBLE_3 = 95,
    AVEBT_STAR_GAMBLE_4 = 96,
    AVEBT_STAR_GAMBLE_5 = 97,
    AVEBT_DUALING_BUFF = 98,
    AVEBT_FREEZING_HOLD = 99,
    AVEBT_MASTER_YOGI = 100,
    AVEBT_MASTER_YOGI_BUFF = 101,
    AVEBT_HELLO_CAT_1 = 102,
    AVEBT_HELLO_CAT_2 = 103,
    AVEBT_HELLO_CAT_3 = 104,
    AVEBT_10TH_ANNIVERSARY = 105,
    AVEBT_SANTA_SOCKS = 106,
    AVEBT_SANTA_TREE = 107,
    AVEBT_SANTA_SNOWMAN = 108,
    AVEBT_BLUFF = 109,
    AVEBT_LIGHTING_SHIELD = 110,
    AVEBT_PHANTOM_CURSE = 111,
    AVEBT_SIGH_STORM1 = 112,
    AVEBT_SIGH_STORM2 = 113,
    AVEBT_SIGH_STORM3 = 114,
    AVEBT_STORM_HIDE = 115,
    AVEBT_DECEPTION_HIDE = 116,
    AVEBT_WIND_HIDE = 117,
    AVEBT_PSYCHIC_POWER = 118,
    AVEBT_ULTIMATE_SQUALL = 119,
    AVEBT_ILLUSION_WIND = 120,
    AVEBT_SAIHA_FURY = 121,
    AVEBT_GRAVITY_SPACE1 = 122,
    AVEBT_GRAVITY_SPACE2 = 123,
    AVEBT_LENGKER_SPECIAL = 124,
    AVEBT_SAIHA_LIGHTENING = 125,
    AVEBT_GRAVITY_SPACE3 = 126,
    AVEBT_GRAVITY_BUFF = 127,
    AVEBT_ASPECT = 128,
    AVEBT_RUNWAY_ARMOR_1 = 129,
    AVEBT_RUNWAY_ARMOR_2 = 130,
    AVEBT_RUNWAY_ARMOR_3 = 131,
    AVEBT_RUNWAY_ARMOR_4 = 132,
    AVEBT_RUNWAY_ARMOR_5 = 133,
    AVEBT_RUNWAY_ARMOR_6 = 134,
    AVEBT_RUNWAY_ARMOR_7 = 135,
    AVEBT_RUNWAY_ARMOR_8 = 136,
    AVEBT_RUNWAY_WEAPON_1 = 137,
    AVEBT_RUNWAY_WEAPON_2 = 138,
    AVEBT_RUNWAY_WEAPON_3 = 139,
    AVEBT_RUNWAY_WEAPON_4 = 140,
    AVEBT_PALADIN_DEFENCE = 141,
    AVEBT_GUARDIAN_DEFENCE = 142,
    AVEBT_REAL_TARGET2 = 143,
    AVEBT_HEALER_BUFFING = 144,
    AVEBT_HEALER_PROTECTION = 145,
    AVEBT_FESTIVAL_STAR0 = 146,
    AVEBT_FESTIVAL_STAR1 = 147,
    AVEBT_FESTIVAL_STAR2 = 148,
    AVEBT_FESTIVAL_STAR3 = 149,
    AVEBT_FESTIVAL_STAR4 = 150,
    AVEBT_FESTIVAL_STAR5 = 151,
    AVEBT_ABSORB_SHIELD = 152,
    AVEBT_PHOENIX_AURA_SELF = 153,
    AVEBT_HELL_AURA_SELF = 154,
    AVEBT_EVAS_AURA_SELF = 155,
    AVEBT_TEMPLA_AURA_SELF = 156,
    AVEBT_LONG_BLOW = 157,
    AVEBT_WIDE_SWORD = 158,
    AVEBT_BIG_FIST = 159,
    AVEBT_SHADOW_STEP = 160,
    AVEBT_TORNADO = 161,
    AVEBT_SNOW_SLOW = 162,
    AVEBT_SNOW_HOLD = 163,
    AVEBT_REFLECTION_ARMOR = 164,
    AVEBT_TORNADO_SLOW = 165,
    AVEBT_ASTATINE_WATER = 166,
    AVEBT_BIGBODY_H = 167,
    AVEBT_BIGBODY_E = 168,
    AVEBT_BIGBODY_DE = 169,
    AVEBT_BIGBODY_SUMMONER_H = 170,
    AVEBT_BIGBODY_SUMMONER_E = 171,
    AVEBT_BIGBODY_SUMMONER_DE = 172,
    AVEBT_BIGBODY_BIG = 173,
    AVEBT_BIGBODY_SMALL = 174,
    AVEBT_SPECIAL_TRANSFORM_WARRIOR_TYPE1 = 175,
    AVEBT_SPECIAL_TRANSFORM_WARRIOR_TYPE2 = 176,
    AVEBT_SPECIAL_TRANSFORM_WARRIOR_TYPE3 = 177,
    AVEBT_SPECIAL_TRANSFORM_WIZARD_TYPE1 = 178,
    AVEBT_SPECIAL_TRANSFORM_WIZARD_TYPE2 = 179,
    AVEBT_SPECIAL_TRANSFORM_WIZARD_TYPE3 = 180,
    AVEBT_DRAGON_ULTIMATE = 181,
    AVEBT_HOLLOWEEN = 182,
    AVEBT_INFINITE_SHIELD1 = 183,
    AVEBT_INFINITE_SHIELD2 = 184,
    AVEBT_INFINITE_SHIELD3 = 185,
    AVEBT_INFINITE_SHIELD4 = 186,
    AVEBT_TANZANITE_SHIELD = 187,
    AVEBT_APPEVENT_DYNASTY = 188,
    AVEBT_APPEVENT_SHRNOEN = 189,
    AVEBT_BAIUM_TALISMAN = 190,
    AVEBT_APPEVENT_DYNASTY_CLASSIC = 191,
    AVEBT_APPEVENT_SHRNOEN_CLASSIC = 192,
    AVEBT_BALTHUS_KNIGHT = 193,
    AVEBT_ANGRY_GOLEM = 194,
    AVEBT_UNBREAKABLE_SONIC = 195,
    AVEBT_DARK_KNIGHT = 196,
    AVEBT_HEROIC_PARTY_BUFF = 197,
    AVEBT_HEROIC_SILENCE = 198,
    AVEBT_HEROIC_TURN_FLEE = 199,
    AVEBT_BROOCH_NECK_1 = 200,
    AVEBT_BROOCH_NECK_2 = 201,
    AVEBT_BROOCH_NECK_3 = 202,
    AVEBT_BROOCH_NECK_4 = 203,
    AVEBT_BROOCH_NECK_5 = 204,
    AVEBT_BROOCH_NECK_6 = 205,
    AVEBT_TANZANITE_SHIELD1 = 206,
    AVEBT_BALTHUS_KNIGHT1 = 207,
    AVEBT_BALTHUS_KNIGHT2 = 208,
    AVEBT_BALTHUS_KNIGHT3 = 209,
    AVEBT_BALTHUS_KNIGHT4 = 210,
    AVEBT_RED_LIBRA1 = 211,
    AVEBT_RED_LIBRA2 = 212,
    AVEBT_RED_LIBRA3 = 213,
    AVEBT_RED_LIBRA4 = 214,
    AVEBT_ROGUE_COUNTER = 215,
    AVEBT_DISARMORED = 216,
    AVEBT_PHANTOMIZE = 217,
    AVEBT_2016_THANKSGIVING1 = 218,
    AVEBT_STUCKED_ICICLE1 = 219,
    AVEBT_DORMANT_USER = 220,
    AVEBT_NEWBIE_USER = 221,
    AVEBT_13TH_BUFF = 222,
    AVEBT_GREETING = 223,
    AVEBT_SEVEN_SIGN_1 = 224,
    AVEBT_SEVEN_SIGN_2 = 225,
    AVEBT_SEVEN_SIGN_3 = 226,
    AVEBT_SEVEN_SIGN_4 = 227,
    AVEBT_SEVEN_SIGN_5 = 228,
    AVEBT_BATTLE_RHAPSODY1 = 229,
    AVEBT_ENCHANTER_DEBUFF1 = 230,
    AVEBT_ENCHANTER_DEBUFF2 = 231,
    AVEBT_BM_TEDDYBEAR = 232,
    AVEBT_BIGBODY_2 = 233,
    AVEBT_WHITE_KNIGHT = 234,
    AVEBT_WIND_ABILITY_POWER = 235,
    AVEBT_STONE_CONVERSION_BLUE = 236,
    AVEBT_STONE_CONVERSION_RED = 237,
    AVEBT_WATER_SCREEN = 238,
    AVEBT_FIRE_SCREEN = 239,
    AVEBT_WIND_SCREEN = 240,
    AVEBT_EARTH_SCREEN = 241,
    AVEBT_WEAKNESS_AIM = 242,
    AVEBT_VAMPIRIC_AIM = 243,
    AVEBT_BLINDNESS_AIM = 244,
    AVEBT_SHIELD_DEFENCE1 = 247,
    AVEBT_SHIELD_DEFENCE2 = 248,
    AVEBT_TWOHAND_STANCE1 = 249,
    AVEBT_PROTECT_AREA1 = 250,
    AVEBT_REDLIBRA_FASTRUN1 = 251,
    AVEBT_OLYMPIAD_LEGEND_EFFECT = 252,
    AVEBT_CAT_EXPBUFF = 253,
    AVEBT_WIND_ABILITY_POWER2 = 254,
    AVEBT_FRINTESSA_BOSS_BIG_BODY = 255,
    AVEBT_ORFEN_SHILED1 = 256,
    AVEBT_ORFEN_SHILED2 = 257,
    AVEBT_ORFEN_SHILED3 = 258,
    AVEBT_ORFEN_SHILED4 = 259,
    AVEBT_ORFEN_SHILED5 = 260,
    AVEBT_BM_TRANSFORM_CAT = 261,
    AVEBT_BM_TRANSFORM_PANDA = 262,
    AVEBT_HARDIN_SHIELD1 = 263,
    AVE_ARENA_OCC_TEAM_HARDIN = 264,
    AVE_ARENA_OCC_TEAM_DEVIANNE = 265,
    AVEBT_XMAS_RUDOLPH1 = 266,
    AVEBT_XMAS_RUDOLPH2 = 267,
    AVEBT_XMAS_RUDOLPH3 = 268,
    AVEBT_NEWYEAR_MEDAL = 269,
    AVEBT_NEWYEAR_SPORT1 = 270,
    AVEBT_NEWYEAR_SPORT2 = 271,
    AVEBT_NEWYEAR_SPORT3 = 272,
    AVEBT_BM2018_DRAGON = 273,
    AVEBT_BM2018_NINJA = 274,
    AVEBT_BM2018_BLUEMUSKETEER = 275,
    AVEBT_BM2018_VALKYRIE = 276,
    AVEBT_BM2018_BARBARIAN = 277,
    AVEBT_BM2018_PIRATE = 278,
    AVEBT_BM2018_COWBOY = 279,
    AVEBT_BM2018_HEALER = 280,
    AVEBT_BM2018_REDNOBLESSE = 281,
    AVEBT_BM2018_WHITENOBLESSE = 282,
    AVEBT_DOT_HP = 283,
    AVEBT_DOT_HPMP = 284,
    AVEBT_RAID_BERSERKER = 285,
    AVEBT_WHITEDAY_BUFF = 286,
    AVEBT_HOLY_SHIELD = 287,
    AVEBT_UNHOLY_SHIELD = 288,
    AVEBT_EARTH_SHIELD1 = 289,
    AVEBT_EARTH_SHIELD2 = 290,
    AVEBT_DOT_MP2 = 291,
    AVEBT_MAX = 292,
    AVEBT_GLOBAL_NONE = 10000,
    AVEBT_GOD_ACCESSORY_RING_1 = 10001,
    AVEBT_GOD_ACCESSORY_EARRING_1 = 10002,
    AVEBT_GOD_ACCESSORY_NECKLACE_1 = 10003,
    AVEBT_GOD_ACCESSORY_RING_2 = 10004,
    AVEBT_GOD_ACCESSORY_EARRING_2 = 10005,
    AVEBT_GOD_ACCESSORY_NECKLACE_2 = 10006,
    AVEBT_GOD_ACCESSORY_RING_3 = 10007,
    AVEBT_GOD_ACCESSORY_EARRING_3 = 10008,
    AVEBT_GOD_ACCESSORY_NECKLACE_3 = 10009,
    AVEBT_GOD_ACCESSORY_RING_4 = 10010,
    AVEBT_GOD_ACCESSORY_EARRING_4 = 10011,
    AVEBT_GOD_ACCESSORY_NECKLACE_4 = 10012,
    AVEBT_GOD_ACCESSORY_RING_5 = 10013,
    AVEBT_GOD_ACCESSORY_EARRING_5 = 10014,
    AVEBT_GOD_ACCESSORY_NECKLACE_5 = 10015,
    AVEBT_GOD_ACCESSORY_RING_6 = 10016,
    AVEBT_GOD_ACCESSORY_EARRING_6 = 10017,
    AVEBT_GOD_ACCESSORY_NECKLACE_6 = 10018,
    AVEBT_GLOBAL_MAX = 10019,
};

namespace EItemAnnounce {

// TotalChecker2015 TPI 0x75d8c. L2constants.h:6717.
enum Type : std::int32_t {
    IA_ENCHANT = 0,
    IA_REAR_ITEM = 1,
};

} // namespace EItemAnnounce

// consolidate existing enum declarations in their donor
// Protocol/l2constants.h owner (TotalChecker2015 IPI file 713). Declarations
// below are copied unchanged from portable headers. Names, underlying types
// and values remain intact. This is source placement evidence, not a new
// current-executable behavior claim. See NamedConstantsRecovery.tsv NC0661.

// TotalChecker2015 TPI 0x4371e, l2constants.h:329.
enum BR_BUFF_EVENT_TYPE : std::int32_t {
    BR_BUFF_EVENT_NONE = 0,
    BR_BUFF_EVENT_EXP_PER = 1,
    BR_BUFF_EVENT_GIVE_ITEM = 2,
    BR_BUFF_EVENT_MAX = 3,
};


// TotalChecker2015 TPI 0x1076, l2constants.h:611.
enum CrystalType : std::int32_t {
    CRT_NONE = 0,
    CRT_D = 1,
    CRT_C = 2,
    CRT_B = 3,
    CRT_A = 4,
    CRT_S = 5,
    CRT_S80 = 6,
    CRT_S84 = 7,
    CRT_R = 8,
    CRT_R95 = 9,
    CRT_R99 = 10,
    CRT_EVENT = 11,
    CRT_MAX = 12,
    CRT_INVALID = -1,
};


// TotalChecker2015 TPI 0x5d15a, l2constants.h:644.
namespace Shot {

enum Type : std::int32_t {
    TypeNone = 0,
    SoulShot = 1,
    SpiritShot = 2,
    FishingShot = 3,
    TypeMax = 4,
};

} // namespace Shot


// TotalChecker2015 TPI 0x5d15c, l2constants.h:653.
namespace Shot {

enum Grade : std::int32_t {
    Invalid = -1,
    None = 0,
    D = 1,
    C = 2,
    B = 3,
    A = 4,
    S = 5,
    R = 6,
    GradeMax = 7,
};

} // namespace Shot


// TotalChecker2015 TPI 0x1089, l2constants.h:712.
enum ItemConsumeType : std::int32_t {
    ICT_UNKNOWN = -1,
    ICT_NORMAL = 0,
    ICT_CHARGE = 1,
    ICT_STACKABLE = 2,
    ICT_ASSET = 3,
};


// TotalChecker2015 TPI 0x1c66a, l2constants.h:984.
enum AdditionalArmorType : std::int32_t {
    AAMT_NONE = 0,
    AAMT_LIGHT = 1,
    AAMT_HEAVY = 2,
    AAMT_MAGIC = 3,
    AAMT_SIGIL = 4,
    AAMT_MAX = 5,
};


// TotalChecker2015 TPI 0x22f0d, l2constants.h:1133.
enum CastleUnderSiege : std::int32_t {
    CUS_NOT_IN_SIEGE = 0,
    CUS_OFFENCE = 1,
    CUS_DEFENCE = 2,
    CUS_MAX = 3,
};


// TotalChecker2015 TPI 0x4d72b, l2constants.h:1434.
namespace ClassGroup {

enum ClassRoleType : int {
    CRT_NONE = 0,
    CRT_KNIGHT = 1,
    CRT_WARRIOR = 2,
    CRT_ROGUE = 3,
    CRT_ARCHOR = 4,
    CRT_WIZARD = 5,
    CRT_SUMMONER = 6,
    CRT_ENCHANTER = 7,
    CRT_SUPPORTER = 8,
    CRT_NOVICE = 9,
    CRT_MAX = 10,
};

} // namespace ClassGroup


// TotalChecker2015 TPI 0x23003, l2constants.h:1793.
enum PrivateStoreType : std::int32_t {
    PST_NONE = 0,
    PST_OPEN = 1,
    PST_MANAGE = 2,
    PST_BUY_OPEN = 3,
    PST_BUY_MANAGE = 4,
    PST_RECIPE_SHOP_OPEN = 5,
    PST_RECIPE_SHOP_MANAGE = 6,
    PST_OBSERVING = 7,
    PST_WHOLESALE_OPEN = 8,
    PST_WHOLESALE_MANAGE = 9,
    PST_MAX = 10,
};


// TotalChecker2015 TPI 0x1c708, l2constants.h:1834.
enum ItemType : std::int32_t {
    IT_NOTITEM = -1,
    IT_WEAPON = 0,
    IT_ARMOR = 1,
    IT_ACCESSARY = 2,
    IT_QUESTITEM = 3,
    IT_ASSET = 4,
    IT_ETCITEM = 5,
};


// TotalChecker2015 TPI 0x1c662, l2constants.h:2491.
enum EffectSuccessType : std::int32_t {
    EFT_FATAL_BLOW = 0,
    EFT_DELETE_HATE = 1,
    EFT_RANDOMIZE_HATE = 2,
    EFT_CONFUSE = 3,
    EFT_RUN_AWAY = 4,
    EFT_SPOIL = 5,
    EFT_BACKSTAB = 6,
    EFT_DISTRUST = 7,
    EFT_DELETE_HATE_OF_ME = 8,
    EFT_DELETE_TOPAGRO = 9,
    EFT_KNOCK_DOWN = 10,
};


// TotalChecker2015 TPI 0x165e4, l2constants.h:2893.
enum PledgeType : int {
    ACADEMY = -1,
    MASTER_PLEDGE = 0,
    SUB_1 = 100,
    SUB_2 = 200,
    SUB_1_1 = 1001,
    SUB_1_2 = 1002,
    SUB_2_1 = 2001,
    SUB_2_2 = 2002,
    MAX_PLEDGE_TYPE = 2003,
};


// TotalChecker2015 TPI 0x463f7, l2constants.h:2981.
namespace PledgeFunctionType {

enum Value : std::int32_t {
    NONE = 0,
    MEMBER_COUNT = 1,
    PLEDGE_WAR = 2,
    PLEDGE_GAME = 3,
    ALLIANCE = 4,
    AUCTION_AGIT = 5,
    COMMUNITY = 6,
    CLAN_ADVENT = 7,
    MINIGAME = 8,
    CREST = 9,
    NICKNAME = 10,
    CASTLE_SIEGE = 11,
    AGIT_SIEGE = 12,
    FORTRESS_SIEGE = 13,
    WAREHOUSE = 14,
    MAX_COUNT = 15,
};

} // namespace PledgeFunctionType


// TotalChecker2015 TPI 0x1203, l2constants.h:3291.
enum ChangeTargetReason : std::int32_t {
    CTR_DEFAULT = 0,
    CTR_BY_USER = 1,
    CTR_BY_SKILL = 2,
    CTR_BY_CONTEXT_MENU = 3,
};


// TotalChecker2015 TPI 0x1211, l2constants.h:3344.
enum AttributeTypeEnum : int {
    ATTRT_NONE = -2,
    ATTRT_START = -1,
    ATTRT_FIRE = 0,
    ATTRT_WATER = 1,
    ATTRT_WIND = 2,
    ATTRT_EARTH = 3,
    ATTRT_HOLY = 4,
    ATTRT_UNHOLY = 5,
    ATTRT_MAX = 6,
};


// TotalChecker2015 TPI 0x23009, l2constants.h:3385.
enum PVP_MATCH_TYPE : std::int32_t {
    PVPMT_MIN = 0,
    PVPMT_UGC_40 = 0,
    PVPMT_UGC_50 = 1,
    PVPMT_UGC_60 = 2,
    PVPMT_UGC_70 = 3,
    PVPMT_UGC_UL = 4,
    PVPMT_COC_70 = 5,
    PVPMT_COC_76 = 6,
    PVPMT_COC_80 = 7,
    PVPMT_UGC_EVENT = 8,
    PVPMT_MAX = 9,
};


// TotalChecker2015 TPI 0xeb2e, l2constants.h:3676.
enum GameRuleType : std::int32_t {
    GRT_TEAM = 0,
    GRT_CLASSLESS = 1,
    GRT_CLASS = 2,
    GRT_MAX = 3,
};


// TotalChecker2015 TPI 0x4d6b3, l2constants.h:4037.
namespace CommissionStatus {

enum Enum : std::int32_t {
    CsInvalidValue = -4,
    CsSearchDelay = -3,
    CsRegisteredFail = -2,
    CsSearchFail = -1,
    CsFail = 0,
    CsOk = 1,
    CsRegisteredOk = 2,
    CsSearchOk = 3,
    CsStatusMAX = 4,
};

} // namespace CommissionStatus


// TotalChecker2015 TPI 0x22ea8, l2constants.h:4305.
enum NpcSummonType : std::int32_t {
    NST_NOTHING = 0,
    NST_SUMMON = 1,
    NST_PET = 2,
    NST_PCTRAP = 3,
    NST_DOPPELGANGER = 4,
    NST_UNIQUE = 5,
    NST_NOT_EVENT = 6,
};


// TotalChecker2015 TPI 0x22eb0, l2constants.h:4329.
namespace Beauty {

enum BeautyState : std::int32_t {
    HAIR = 0,
    FACE = 1,
    COLOR = 2,
    STATEMAX = 3,
};

} // namespace Beauty


// TotalChecker2015 TPI 0x4d6bc, l2constants.h:4343.
namespace CommissionItemClassId {

enum Enum : std::int32_t {
    CICI_NONE = 0,
    CICI_SEVENDAY_30PERCENT_DISCOUNT = 22351,
    CICI_SEVENDAY_NO_COMMISSION = 22352,
    CICI_FIFTEENDAY = 22353,
    CICI_THIRTYDAY = 22354,
};

} // namespace CommissionItemClassId


// TotalChecker2015 TPI 0x48f9d, l2constants.h:4602.
namespace CharacterDeactivateReason {

enum DeactivateReason : std::int32_t {
    CDR_DEACTIVATE_DEFAULT = 0,
    CDR_ACTIVATE = 1,
    CDR_DEACTIVATE_SLOT = 2,
    CDR_DEACTIVATE_OVER_LEVEL = 3,
    CDR_DEACTIVATE_CLASS = 4,
};

} // namespace CharacterDeactivateReason


// TotalChecker2015 TPI 0x22ff3, l2constants.h:5737.
enum E_TODOLIST_PROGRESS_TYPE : std::int32_t {
    E_TODOLIST_PROGRESS_TYPE_INVALID = 0,
    E_TODOLIST_PROGRESS_TYPE_BEFOR = 1,
    E_TODOLIST_PROGRESS_TYPE_PROGRESS = 2,
    E_TODOLIST_PROGRESS_TYPE_FINISH = 3,
    E_TODOLIST_PROGRESS_TYPE_MAX = 4,
};


// TotalChecker2015 TPI 0x22ff5, l2constants.h:5773.
enum E_TODOLIST_ENTER_TYPE : std::int32_t {
    E_TODOLIST_ENTER_TYPE_INVALID = 0,
    E_TODOLIST_ENTER_TYPE_POSSIBLE = 1,
    E_TODOLIST_ENTER_TYPE_IMPOSSIBLE = 2,
    E_TODOLIST_ENTER_TYPE_ALLTIME = 3,
    E_TODOLIST_ENTER_TYPE_MAX = 4,
};


// TotalChecker2015 TPI 0x1032, l2constants.h:6045.
enum AttackType : std::int32_t {
    AT_NONE = 0,
    AT_SWORD = 1,
    AT_TWOHANDSWORD = 2,
    AT_BUSTER = 3,
    AT_BLUNT = 4,
    AT_TWOHANDBLUNT = 5,
    AT_STAFF = 6,
    AT_TWOHANDSTAFF = 7,
    AT_DAGGER = 8,
    AT_POLE = 9,
    AT_FIST = 10,
    AT_BOW = 11,
    AT_ETC = 12,
    AT_DUAL = 13,
    AT_DUALFIST = 14,
    AT_FISHINGROD = 15,
    AT_RAPIER = 16,
    AT_CROSSBOW = 17,
    AT_ANCIENTSWORD = 18,
    AT_FLAG = 19,
    AT_DUALDAGGER = 20,
    AT_OWNTHING = 21,
    AT_TWOHANDCROSSBOW = 22,
    AT_DUALBLUNT = 23,
    AT_PISTOL = 24,
    AT_MAX = 25,
};


// TotalChecker2015 TPI 0x1038, l2constants.h:6095.
namespace EnsoulItem {

enum EnsoulItemSlotType : int {
    EIST_INVALID = 0,
    EIST_NORMAL = 1,
    EIST_BM = 2,
    EIST_MAX = 3,
};

} // namespace EnsoulItem


// TotalChecker2015 TPI 0x103a, l2constants.h:6110.
namespace EnsoulItem {

enum EnsoulOptionType : int {
    EOT_INVALID = -1,
    EOT_BASE = 0,
    EOT_PHY1 = 1,
    EOT_PHY2 = 2,
    EOT_PHY3 = 3,
    EOT_PHY4 = 4,
    EOT_PHY5 = 5,
    EOT_PHY6 = 6,
    EOT_PHY7 = 7,
    EOT_PHY8 = 8,
    EOT_PHY9 = 9,
    EOT_MAG1 = 10,
    EOT_MAG2 = 11,
    EOT_MAG3 = 12,
    EOT_MAG4 = 13,
    EOT_MAG5 = 14,
    EOT_MAG6 = 15,
    EOT_MAG7 = 16,
    EOT_MAG8 = 17,
    EOT_MAG9 = 18,
    EOT_PPHY1 = 19,
    EOT_PPHY2 = 20,
    EOT_PPHY3 = 21,
    EOT_PPHY4 = 22,
    EOT_PPHY5 = 23,
    EOT_PPHY6 = 24,
    EOT_PPHY7 = 25,
    EOT_PPHY8 = 26,
    EOT_PPHY9 = 27,
    EOT_MMAG1 = 28,
    EOT_MMAG2 = 29,
    EOT_MMAG3 = 30,
    EOT_MMAG4 = 31,
    EOT_MMAG5 = 32,
    EOT_MMAG6 = 33,
    EOT_MMAG7 = 34,
    EOT_MMAG8 = 35,
    EOT_MMAG9 = 36,
    EOT_PM1 = 37,
    EOT_PM2 = 38,
    EOT_PM3 = 39,
    EOT_PM4 = 40,
    EOT_PM5 = 41,
    EOT_PM6 = 42,
    EOT_PM7 = 43,
    EOT_PM8 = 44,
    EOT_PM9 = 45,
    EOT_TRA1 = 46,
    EOT_TRA2 = 47,
    EOT_TRA3 = 48,
    EOT_TRA4 = 49,
    EOT_TRA5 = 50,
    EOT_TRA6 = 51,
    EOT_TRA7 = 52,
    EOT_TRA8 = 53,
    EOT_TRA9 = 54,
    EOT_ATT1 = 55,
    EOT_ATT2 = 56,
    EOT_ATT3 = 57,
    EOT_ATT4 = 58,
    EOT_ATT5 = 59,
    EOT_ATT6 = 60,
    EOT_ATT7 = 61,
    EOT_ATT8 = 62,
    EOT_ATT9 = 63,
    EOT_ETC1 = 64,
    EOT_ETC2 = 65,
    EOT_ETC3 = 66,
    EOT_ETC4 = 67,
    EOT_ETC5 = 68,
    EOT_ETC6 = 69,
    EOT_ETC7 = 70,
    EOT_ETC8 = 71,
    EOT_ETC9 = 72,
    EOT_SIGEL_DEFENSE = 73,
    EOT_SIGEL_HEALTH = 74,
    EOT_TIR_MIGHT = 75,
    EOT_TIR_HASTE = 76,
    EOT_OTHEL_FOCUS = 77,
    EOT_OTHEL_CRTDMG = 78,
    EOT_YR_WINDWALK = 79,
    EOT_YR_GUIDANCE = 80,
    EOT_FEOH_MAGICPOWER = 81,
    EOT_FEOH_EMPOWER = 82,
    EOT_IS_MPREGEN = 83,
    EOT_IS_MPUP = 84,
    EOT_WYNN_ACUMEN = 85,
    EOT_WYNN_LIGHT = 86,
    EOT_EOLH_HPREGEN = 87,
    EOT_EOLH_HEAL = 88,
    EOT_TIR_CRTSTUN = 89,
    EOT_OTHEL_MIGHTMORTAL = 90,
    EOT_YR_CRTDMG = 91,
    EOT_IS_ACUMEN = 92,
    EOT_IS_WINDWALK = 93,
    EOT_WYNN_MPUP = 94,
    EOT_EOLH_MPREGEN = 95,
    EOT_ANAKIM_POWER = 96,
    EOT_LILITH_POWER = 97,
    EOT_MAX = 98,
};

} // namespace EnsoulItem
