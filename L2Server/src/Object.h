#pragma once

#include "Shared/src/BaseStructure.h"
#include "Shared/src/IOC.h"

#include <cstddef>
#include <cstdint>


class CInstantZone;

extern double ArrayPVPPenalty[121];
extern double ArrayExpPenalty[121];
extern double ArrayPVPHighLevelPenalty[121];
extern double ArrayLevelPenaltyModifier[239];

class CPreCalculate {
public:
    CPreCalculate();
};

extern CPreCalculate preCalculate;

// Current L2Server lexer/parser and Set bodies fix the numeric ABI below.
// TotalChecker2015 supplies identifier spelling only after its ordered field
// literal streams match the current image and the near-build TotalChecker.
enum ObjectFieldType : std::int32_t {
    OBJECT_FIELD_ITEM_ID = 0,
    OBJECT_FIELD_NAME = 1,
    OBJECT_FIELD_NAME_ID = 2,
    OBJECT_FIELD_DESCRIPTION_ID = 3,
    OBJECT_FIELD_ARMOR_TYPE = 4,
    OBJECT_FIELD_ITEM_TYPE = 5,
    OBJECT_FIELD_SLOT_BIT_TYPE = 6,
    OBJECT_FIELD_BLESSED = 7,
    OBJECT_FIELD_WEIGHT = 8,
    OBJECT_FIELD_CONSUME_TYPE = 9,
    OBJECT_FIELD_INITIAL_COUNT = 10,
    OBJECT_FIELD_MAXIMUM_COUNT = 11,
    OBJECT_FIELD_SOUL_SHOT = 12,
    OBJECT_FIELD_SPIRIT_SHOT = 13,
    OBJECT_FIELD_REDUCED_SOUL_SHOT = 14,
    OBJECT_FIELD_REDUCED_SPIRIT_SHOT = 15,
    OBJECT_FIELD_IMMEDIATE_EFFECT = 16,
    OBJECT_FIELD_PRICE = 17,
    OBJECT_FIELD_DEFAULT_PRICE = 18,
    OBJECT_FIELD_RESTRICTION_RACE_LIMIT = 19,
    OBJECT_FIELD_RESTRICTION_CLASS_LIMIT = 20,
    OBJECT_FIELD_RESTRICTION_STATUS_LIMIT = 21,
    OBJECT_FIELD_RESTRICTION_INT_LIMIT = 22,
    OBJECT_FIELD_RESTRICTION_WIT_LIMIT = 23,
    OBJECT_FIELD_RESTRICTION_MEN_LIMIT = 24,
    OBJECT_FIELD_RESTRICTION_STR_LIMIT = 25,
    OBJECT_FIELD_RESTRICTION_DEX_LIMIT = 26,
    OBJECT_FIELD_RESTRICTION_CON_LIMIT = 27,
    OBJECT_FIELD_RESTRICTION_SKILL_LIMIT = 28,
    OBJECT_FIELD_RESTRICTION_HP_LIMIT = 29,
    OBJECT_FIELD_RESTRICTION_MP_LIMIT = 30,
    OBJECT_FIELD_RESTRICTION_RANK_LIMIT = 31,
    OBJECT_FIELD_RESTRICTION_TENDENCY_LIMIT = 32,
    OBJECT_FIELD_RESTRICTION_ENVIRONMENT_LIMIT = 33,
    OBJECT_FIELD_RESTRICTION_LEVEL_LIMIT = 34,
    OBJECT_FIELD_ITEM_SKILL = 35,
    OBJECT_FIELD_MATERIAL_TYPE = 36,
    OBJECT_FIELD_CRYSTAL_COUNT = 37,
    OBJECT_FIELD_IS_TRADE = 38,
    OBJECT_FIELD_IS_DROP = 39,
    OBJECT_FIELD_IS_DESTRUCT = 40,
    OBJECT_FIELD_PHYSICAL_DAMAGE = 41,
    OBJECT_FIELD_RANDOM_DAMAGE = 42,
    OBJECT_FIELD_WEAPON_TYPE = 43,
    OBJECT_FIELD_CAN_PENETRATE = 44,
    OBJECT_FIELD_CRITICAL = 45,
    OBJECT_FIELD_ATTACK_RANGE = 46,
    OBJECT_FIELD_EFFECTIVE_RANGE = 47,
    OBJECT_FIELD_BASE_ATTACK_RANGE = 48,
    OBJECT_FIELD_DAMAGE_RANGE = 49,
    OBJECT_FIELD_ATTACK_SPEED = 50,
    OBJECT_FIELD_REUSE_DELAY = 51,
    OBJECT_FIELD_MP_CONSUME = 52,
    OBJECT_FIELD_REDUCED_MP_CONSUME = 53,
    OBJECT_FIELD_MAGICAL_DAMAGE = 54,
    OBJECT_FIELD_DURABILITY = 55,
    OBJECT_FIELD_DAMAGED = 56,
    OBJECT_FIELD_PHYSICAL_DEFENSE = 57,
    OBJECT_FIELD_MAGICAL_DEFENSE = 58,
    OBJECT_FIELD_ENCHANTED = 59,
    OBJECT_FIELD_MP_BONUS = 60,
    OBJECT_FIELD_CRITICAL_SKILL = 61,
    OBJECT_FIELD_ATTACK_SKILL = 62,
    OBJECT_FIELD_MAGIC_SKILL = 63,
    OBJECT_FIELD_ITEM_SKILL_ENCHANTED_FOUR = 64,
    OBJECT_FIELD_FOR_NPC = 65,
    OBJECT_FIELD_IS_ATTRIBUTION = 66,
    OBJECT_FIELD_IS_NPCTRADE = 67,
    OBJECT_FIELD_ATTR_GROUP_ID = 68,
    OBJECT_FIELD_IS_LOCKED = 69,
    OBJECT_FIELD_NPC_ID = 70,
    OBJECT_FIELD_LEVEL = 71,
    OBJECT_FIELD_RACE = 72,
    OBJECT_FIELD_CLASS = 73,
    OBJECT_FIELD_SEX = 74,
    OBJECT_FIELD_TEND = 75,
    OBJECT_FIELD_INT = 76,
    OBJECT_FIELD_STR = 77,
    OBJECT_FIELD_CON = 78,
    OBJECT_FIELD_MEN = 79,
    OBJECT_FIELD_DEX = 80,
    OBJECT_FIELD_WIT = 81,
    OBJECT_FIELD_EXP = 82,
    OBJECT_FIELD_HP_INCREASE = 83,
    OBJECT_FIELD_MP_INCREASE = 84,
    OBJECT_FIELD_BASE_HP_RECOVER = 85,
    OBJECT_FIELD_BASE_MP_RECOVER = 86,
    OBJECT_FIELD_WEAPON = 87,
    OBJECT_FIELD_ARMOR = 88,
    OBJECT_FIELD_PHYSICAL_HIT_MODIFY = 89,
    OBJECT_FIELD_BASE_PHYSICAL_ATTACK = 90,
    OBJECT_FIELD_BASE_CRITICAL = 91,
    OBJECT_FIELD_BASE_ATTACK_TYPE = 92,
    OBJECT_FIELD_BASE_ATTACK_SPEED = 93,
    OBJECT_FIELD_BASE_DEFEND = 94,
    OBJECT_FIELD_BASE_HIT = 95,
    OBJECT_FIELD_BASE_AVOID = 96,
    OBJECT_FIELD_PHYSICAL_AVOID_MODIFY = 97,
    OBJECT_FIELD_BASE_MAGIC_ATTACK = 98,
    OBJECT_FIELD_BASE_MAGIC_DEFEND = 99,
    OBJECT_FIELD_MAGIC_USE_SPEED = 100,
    OBJECT_FIELD_MAGIC_USE_SPEED_MODIFY = 101,
    OBJECT_FIELD_GROUND_LOW = 102,
    OBJECT_FIELD_GROUND_HIGH = 103,
    OBJECT_FIELD_UNDERWATER_LOW = 104,
    OBJECT_FIELD_UNDERWATER_HIGH = 105,
    OBJECT_FIELD_FLY_LOW = 106,
    OBJECT_FIELD_FLY_HIGH = 107,
    OBJECT_FIELD_FLOAT_LOW = 108,
    OBJECT_FIELD_FLOAT_HIGH = 109,
    OBJECT_FIELD_BASE_JUMP = 110,
    OBJECT_FIELD_BASE_EARSHOT = 111,
    OBJECT_FIELD_ITEM_ADDITIONAL_DROP_LIST = 112,
    OBJECT_FIELD_SELLING_GOODS = 113,
    OBJECT_FIELD_BUYING_GOODS = 114,
    OBJECT_FIELD_SKILL_LIST = 115,
    OBJECT_FIELD_MAGIC_LIST = 116,
    OBJECT_FIELD_WEIGHT_LIMIT = 117,
    OBJECT_FIELD_SATIETY = 118,
    OBJECT_FIELD_STATUS = 119,
    OBJECT_FIELD_TENDENCY = 120,
    OBJECT_FIELD_EVALUATION = 121,
    OBJECT_FIELD_FRIENDS = 122,
    OBJECT_FIELD_GUILD = 123,
    OBJECT_FIELD_PLEDGE = 124,
    OBJECT_FIELD_ALLIANCE = 125,
    OBJECT_FIELD_ORG_HP = 126,
    OBJECT_FIELD_ORG_MP = 127,
    OBJECT_FIELD_HP_RECOVER = 128,
    OBJECT_FIELD_MP_RECOVER = 129,
    OBJECT_FIELD_HIT = 130,
    OBJECT_FIELD_PHYSICAL_ATTACK = 131,
    OBJECT_FIELD_ATTACK_INTERVAL = 132,
    OBJECT_FIELD_PHYSICAL_AVOID = 133,
    OBJECT_FIELD_MAGIC_ATTACK = 134,
    OBJECT_FIELD_MAGIC_DEFEND = 135,
    OBJECT_FIELD_JUMP = 136,
    OBJECT_FIELD_EARSHOT = 137,
    OBJECT_FIELD_RAND_DAM = 138,
    OBJECT_FIELD_UNDYING = 139,
    OBJECT_FIELD_CAN_BE_ATTACKED = 140,
    OBJECT_FIELD_ACQUIRE_SP = 141,
    OBJECT_FIELD_TEMPER = 142,
    OBJECT_FIELD_SAFE_HEIGHT = 143,
    OBJECT_FIELD_ORG_JUMP = 144,
    OBJECT_FIELD_MAX_ITEM = 145,
    OBJECT_FIELD_CORPSE_MAKE_LIST = 146,
    OBJECT_FIELD_SLOT_CHEST = 147,
    OBJECT_FIELD_SLOT_RHAND = 148,
    OBJECT_FIELD_SLOT_LHAND = 149,
    OBJECT_FIELD_SIGHT = 150,
    OBJECT_FIELD_AGRO_RANGE = 151,
    OBJECT_FIELD_ALIGN = 152,
    OBJECT_FIELD_COLLISION_RADIUS = 153,
    OBJECT_FIELD_COLLISION_HEIGHT = 154,
    OBJECT_FIELD_GUILD_LEADER = 155,
    OBJECT_FIELD_PLEDGE_LEADER = 156,
    OBJECT_FIELD_ALLIANCE_LEADER = 157,
    OBJECT_FIELD_RULER = 158,
    OBJECT_FIELD_RULLING_DOMAIN = 159,
    OBJECT_FIELD_ORG_HP_REGEN = 160,
    OBJECT_FIELD_ORG_MP_REGEN = 161,
    OBJECT_FIELD_AI = 162,
    OBJECT_FIELD_CLAN = 163,
    OBJECT_FIELD_CLAN_HELP_RANGE = 164,
    OBJECT_FIELD_IGNORE_CLAN_LIST = 165,
    OBJECT_FIELD_HIT_MODIFY = 166,
    OBJECT_FIELD_AVOIDE_MODIFY = 167,
    OBJECT_FIELD_SHIELD_DEFEND_RATE = 168,
    OBJECT_FIELD_SHIELD_DEFEND = 169,
    OBJECT_FIELD_ETCITEM_TYPE = 170,
    OBJECT_FIELD_ACQUIRE_EXP_RATE = 171,
    OBJECT_FIELD_BASE_REUSE_DELAY = 172,
    OBJECT_FIELD_DUALFHIT_RATE = 173,
    OBJECT_FIELD_CORPSE_TIME = 174,
    OBJECT_FIELD_ITEM_HTML = 175,
    OBJECT_FIELD_SETITEM_SLOT_UNDERWEAR = 176,
    OBJECT_FIELD_SETITEM_SLOT_REAR = 177,
    OBJECT_FIELD_SETITEM_SLOT_LEAR = 178,
    OBJECT_FIELD_SETITEM_SLOT_NECK = 179,
    OBJECT_FIELD_SETITEM_SLOT_RFINGER = 180,
    OBJECT_FIELD_SETITEM_SLOT_LFINGER = 181,
    OBJECT_FIELD_SETITEM_SLOT_HEAD = 182,
    OBJECT_FIELD_SETITEM_SLOT_RHAND = 183,
    OBJECT_FIELD_SETITEM_SLOT_LHAND = 184,
    OBJECT_FIELD_SETITEM_SLOT_GLOVES = 185,
    OBJECT_FIELD_SETITEM_SLOT_CHEST = 186,
    OBJECT_FIELD_SETITEM_SLOT_LEGS = 187,
    OBJECT_FIELD_SETITEM_SLOT_FEET = 188,
    OBJECT_FIELD_SETITEM_SLOT_BACK = 189,
    OBJECT_FIELD_SETITEM_SLOT_RLHAND = 190,
    OBJECT_FIELD_SETITEM_ADDITIONAL = 191,
    OBJECT_FIELD_SETITEM_SLOT_TYPE = 192,
    OBJECT_FIELD_SETITEM_SET_SKILL = 193,
    OBJECT_FIELD_SETITEM_EFFECT_SKILL = 194,
    OBJECT_FIELD_SETITEM_ADDITIONAL_EFFECT_SKILL = 195,
    OBJECT_FIELD_SETITEM_P_DEF_INC = 196,
    OBJECT_FIELD_SETITEM_M_DEF_INC = 197,
    OBJECT_FIELD_SETITEM_HP_INC = 198,
    OBJECT_FIELD_SETITEM_MP_INC = 199,
    OBJECT_FIELD_SETITEM_BREATHE_INC = 200,
    OBJECT_FIELD_SETITEM_MOVE_SPEED_INC = 201,
    OBJECT_FIELD_SETITEM_MR_INC = 202,
    OBJECT_FIELD_SETITEM_CAST_SPEED_INC = 203,
    OBJECT_FIELD_SETITEM_P_ATTACK_INC = 204,
    OBJECT_FIELD_SETITEM_M_ATTACK_INC = 205,
    OBJECT_FIELD_SETITEM_HP_REGEN_INC = 206,
    OBJECT_FIELD_SETITEM_MP_REGEN_INC = 207,
    OBJECT_FIELD_SETITEM_STR_INC = 208,
    OBJECT_FIELD_SETITEM_CON_INC = 209,
    OBJECT_FIELD_SETITEM_DEX_INC = 210,
    OBJECT_FIELD_SETITEM_INT_INC = 211,
    OBJECT_FIELD_SETITEM_MEN_INC = 212,
    OBJECT_FIELD_SETITEM_WIT_INC = 213,
    OBJECT_FIELD_SETITEM_AVOID_INC = 214,
    OBJECT_FIELD_SETITEM_SHIELD_DEF_PROB_INC = 215,
    OBJECT_FIELD_SETITEM_WEIGHT_LIMIT_INC = 216,
    OBJECT_FIELD_SETITEM_RESIST_POISON_INC = 217,
    OBJECT_FIELD_SETITEM_P_DEF_VS_DAGGER_INC = 218,
    OBJECT_FIELD_ITEM_ADDITIONAL_DROP_LIST_MULTI = 219,
    OBJECT_FIELD_NO_SLEEP_MODE = 220,
    OBJECT_FIELD_SOULSHOT_COUNT = 221,
    OBJECT_FIELD_SPIRITSHOT_COUNT = 222,
    OBJECT_FIELD_MAGIC_WEAPON = 223,
    OBJECT_FIELD_HIT_TIME_FACTOR = 224,
    OBJECT_FIELD_HIT_TIME_FACTOR_SKILL = 225,
    OBJECT_FIELD_ENCHANT_ENABLE = 226,
    OBJECT_FILED_UNSOWING = 227,
    HENNA_NAME = 228,
    HENNA_INT_ID = 229,
    HENNA_ITEM_ID = 230,
    HENNA_LEVEL = 231,
    HENNA_STR = 232,
    HENNA_CON = 233,
    HENNA_DEX = 234,
    HENNA_INT = 235,
    HENNA_MEN = 236,
    HENNA_WIT = 237,
    HENNA_LUC = 238,
    HENNA_CHA = 239,
    HENNA_NEED_COUNT = 240,
    HENNA_WERA_FEE = 241,
    HENNA_CANCEL_COUNT = 242,
    HENNA_CANCEL_FEE = 243,
    HENNA_CAN_WEAR_CLASS = 244,
    HENNA_SKILL = 245,
    HENNA_PREMIUM = 246,
    HENNA_PERIOD = 247,
    HENNA_UNEQUIP_SKILL = 248,
    HENNA_COND = 249,
    MANOR_ID = 250,
    RESIDENCE_ID = 251,
    OBJECT_FIELD_MANOR_NAME = 252,
    PROCURE_LIST = 253,
    ITEM_MANOR_ID = 254,
    ITEM_MANOR_LEV = 255,
    ITEM_MANOR_CROP_ID = 256,
    ITEM_MANOR_CROP_ID_REPLACE = 257,
    OBJECT_FIELD_ACQUIRE_RAID_BOSS_POINT = 258,
    OBJECT_FIELD_EX_ITEM_DROP_LIST = 259,
    OBJECT_FIELD_EX_IMMEDIATE_EFFECT = 260,
    OBJECT_FIELD_S_NPC_PROP_HP_RATE = 261,
    OBJECT_FIELD_SETITEM_ADDITIONAL2 = 262,
    OBJECT_FIELD_SETITEM_ADDITIONAL2_EFFECT_SKILL = 263,
    OBJECT_FIELD_EX_CRT_EFFECT = 264,
    OBJECT_FIELD_DROP_PERIOD = 265,
    OBJECT_FIELD_MULTI_SKILL_LIST = 266,
    OBJECT_FIELD_ITEM_ENCHANT_OPTION_SKILL = 267,
    OBJECT_FIELD_DELAY_SHARE_GROUP = 268,
    OBJECT_FIELD_KEEP_TYPE = 269,
    OBJECT_FIELD_REQUIRED_LEVEL = 270,
    OBJECT_FIELD_DURATION = 271,
    OBJECT_FIELD_CAN_MOVE = 272,
    OBJECT_FIELD_BASE_ATTRIBUTE_ATTACK = 273,
    OBJECT_FIELD_BASE_ATTRIBUTE_DEFEND = 274,
    OBJECT_FIELD_BASE_ATTRIBUTE_TYPE = 275,
    OBJECT_FIELD_PASSABLE_DOOR = 276,
    OBJECT_FIELD_UNEQUIP_SKILL = 277,
    OBJECT_FIELD_EQUIP_SKILL = 278,
    OBJECT_FIELD_ELEMENTAL_ENABLE = 279,
    OBJECT_FIELD_UNIQUE = 280,
    OBJECT_FIELD_TEMPORARY_EVENT_USE = 281,
    OBJECT_FIELD_NPC_CAN_MOVE = 282,
    OBJECT_FIELD_EVENT_FLAG = 283,
    OBJECT_FIELD_ITEM_EQUIP_OPTION = 284,
    OBJECT_FIELD_HAS_SUMMONER = 285,
    OBJECT_FIELD_USE_SKILL_DISTIME = 286,
    OBJECT_FIELD_EQUIP_REUSE_DELAY = 287,
    OBJECT_FIELD_IS_PREMIUM = 288,
    OBJECT_FIELD_PERIOD = 289,
    OBJECT_FIELD_TARGETABLE = 290,
    OBJECT_FIELD_SHOW_NAME_TAG = 291,
    OBJECT_FIELD_PRIVATE_RESPAWN_NO_LOG = 292,
    OBJECT_FIELD_IS_OLYMPIAD_CAN_USE = 293,
    OBJECT_FIELD_IS_PLEDGEGAME_CAN_USE = 294,
    OBJECT_FIELD_FAKE_CLASS_ID = 295,
    OBJECT_FIELD_IS_CHANCE_AFFECTED = 296,
    OBJECT_FIELD_AGATHION_ENERGY = 297,
    OBJECT_FIELD_AGATHION_MAX_ENERGY = 298,
    OBJECT_FIELD_ABNORMAL_RESIST = 299,
    OBJECT_FIELD_VITALITY_ITEM_DROP_LIST = 300,
    OBJECT_FIELD_MP_REWARD = 301,
    OBJECT_FIELD_IS_HERO_ENTRY = 302,
    OBJECT_FIELD_IS_DEATH_PENALTY = 303,
    OBJECT_FIELD_ENABLE_MOVE_AFTER_TALK = 304,
    OBJECT_FILED_IS_HP_LIMIT_BREAK = 305,
    OBJECT_FIELD_LUC = 306,
    OBJECT_FIELD_CHA = 307,
    OBJECT_FIELD_LUCKY_STAT_ITEM_LIST = 308,
    OBJECT_FIELD_ITEM_G_ADDITIONAL_DROP_LIST_MULTI = 309,
    OBJECT_FIELD_ENSOUL_SLOT_COUNT = 310,
    OBJECT_FIELD_NPC_CAN_CHANGE_DIR = 311,
};


class CAbstractCarrier;
class CAirShip;
class CAirShipPart;
class CAirShipSteering;
class CAdvanceBase;
class CArmor;
class CAsset;
class CBattleCamp;
class CBoss;
class CChair;
class CCollection;
class CContributeData;
class CControlTower;
class CCreature;
class CDoor;
class CDoppelganger;
class CDuel;
class CEventController;
class CEventMatch;
class CFortressFlagpole;
class CHolyThing;
class CItem;
class CItemReleaseVerifier;
class CMerchant;
class CMultiPartyCommandChannel;
class CNPC;
class CObject;
class CObserverRemains;
class COwnThing;
class CPCTrap;
class CPet;
class CResidenceDecoVisible;
class CShuttle;
class CSignBoard;
class CStaticObject;
class CStatue;
class CSummon;
class CTeamBattleFlag;
class CTeleporter;
class CTrap;
class CVehicle;
class CWorldObject;
class CWorldTrap;
class CWorldTrapDB;
class CZzoldagu;
class User;

// Four-byte smart-ID value returned by the exact current GetSmartID body.
// Resolution is product-owned and follows CObject::GetObjectW class dispatch.
class CSmartID final {
public:
    CSmartID() = default;
    explicit CSmartID(std::uint32_t smartId) noexcept : sid_(smartId) {}
    explicit CSmartID(CObject* object) noexcept;

    CSmartID& operator=(CObject* object) noexcept;

    CObject* GetObjectW() const;
    CWorldObject* GetWorldObject() const;
    CVehicle* GetVehicle() const;
    CAirShip* GetAirShip() const;
    CShuttle* GetShuttle() const;
    std::uint32_t GetId() const;
    CCreature* GetCreature() const;
    CNPC* GetNPC() const;
    User* GetUser() const;
    CItem* GetItem() const;
    CStaticObject* GetStaticObject() const;
    CDoor* GetDoor() const;
    bool IsValid() const;
    std::uint32_t Sid() const noexcept;

private:
    std::uint32_t sid_ = 0;
};


// Exact current L2Server base envelope. Main Shared remains the sole
// physical owner of MemoryObject and CIOObject.
class CObject : public ::CIOObject {
public:
    CObject();
    ~CObject() override;

    L2_DECLARE_OBJECT_REGISTRATION(CObject)

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    L2_DECLARE_OBJECT_ALLOCATORS()
    static void operator delete(void* allocation, bool objectAllocation) noexcept;
    static void operator delete(void* allocation) noexcept;
    static void operator delete(CObject* object, std::destroying_delete_t) noexcept;

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    CSmartID GetSmartID();
    FVector GetNormalizedDirVector(int angle) const;
    static FVector GetNormalizedDirVector(const FAngle& direction, int angle);
    unsigned int Id() const noexcept { return m_nId; }
    bool IsDeleting() const { return m_bDeleting; }
    void SetDeleting(bool deleting) { m_bDeleting = deleting; }
    void SetDelete();
    int GetInstantZoneIndex();
    int GetInstantZoneTypeID() const noexcept { return m_nInstantZoneTypeID; }
    static CObject* GetObjectW(std::uint32_t sid);

    virtual bool SetVisible(bool visible);
    virtual bool IsVisible();
    virtual bool CanBeSeenBy(CObject& object);
    virtual bool SetTargetable(bool targetable, bool flag);
    virtual bool IsTargetable();
    virtual bool SetShowNametag(bool show);
    virtual bool IsShowNametag();
    virtual int GetContainerIndex();
    virtual double GetCollisionRadius() const;
    virtual int GetClassId() const;
    virtual CInstantZone* GetInstantZone();
    virtual unsigned int GetInZoneID() const;
    virtual void SetInZoneID(unsigned int inZoneId);
    virtual bool IsInRealWorld() const;
    virtual bool IsInInZone(unsigned int inZoneId);
    virtual unsigned int GetNextInZoneID() const;
    virtual void SetNextInZoneID(unsigned int inZoneId);
    virtual void SetInstantZoneTypeID(int instantZoneTypeId);
    virtual int Level() const;
    virtual FAngle GetDir() const;
    virtual int GetIndex() const = 0;
    virtual CObject* Copy() = 0;
    virtual bool IsObject();
    virtual bool IsVehicle();
    virtual bool IsAirShip();
    virtual bool IsShuttle();
    virtual bool IsAbstractCarrier();
    virtual CVehicle* CastVehicle();
    virtual CAirShip* CastAirShip();
    virtual CShuttle* CastShuttle();
    virtual CAbstractCarrier* CastAbstractCarrier();
    virtual bool IsWorldObject();
    virtual bool IsCreature();
    virtual bool IsPC() const;
    virtual bool IsNPC();
    virtual bool IsPet();
    virtual bool IsSummon() const;
    virtual bool IsSummonOrPet() const;
    virtual bool IsMerchant();
    virtual bool IsBoss();
    virtual bool IsZzoldagu();
    virtual bool IsTrap();
    virtual bool IsWorldTrap();
    virtual bool IsPCTrap();
    virtual bool IsPCOrSummon() const;
    virtual bool IsPCOrPCRelatedNPC() const;
    virtual bool IsDoppelganger() const;
    virtual bool IsTeleporter() const;
    virtual bool IsCollection() const;
    virtual CWorldObject* CastWorldObject();
    virtual CCreature* CastCreature();
    virtual CSummon* CastSummon();
    virtual CPet* CastPet();
    virtual User* CastUser();
    virtual CNPC* CastNPC();
    virtual CMerchant* CastMerchant();
    virtual CBoss* CastBoss();
    virtual CTrap* CastTrap();
    virtual CWorldTrap* CastWorldTrap();
    virtual CPCTrap* CastPCTrap();
    virtual CDoppelganger* CastDoppelganger();
    virtual CTeleporter* CastTeleporter();
    virtual CCollection* CastCollection();
    virtual User* GetUserOrMaster();
    virtual bool IsItem() const;
    virtual bool IsWeapon() const;
    virtual bool IsArmor() const;
    virtual bool IsAccessary() const;
    virtual bool IsQuestItem() const;
    virtual bool IsAsset() const;
    virtual bool IsEtcItem() const;
    virtual bool IsArtifact() const;
    virtual CItem* CastItem();
    virtual bool IsStaticObject();
    virtual bool IsDoor();
    virtual bool IsHolyThing();
    virtual bool IsBattleCamp();
    virtual bool IsSignBoard();
    virtual bool IsObserverRemains();
    virtual bool IsChair();
    virtual bool IsControlTower();
    virtual bool IsEventController();
    virtual bool IsTeamBattleFlag();
    virtual bool IsAirShipPart();
    virtual bool IsResidenceDecoVisible();
    virtual bool IsEventMatch();
    virtual bool IsFortressFlagpole();
    virtual bool IsOwnThing();
    virtual bool IsAirShipSteering();
    virtual bool IsAdvanceBase();
    virtual bool IsContributeData();
    virtual bool IsMPCC();
    virtual bool IsInZone();
    virtual bool IsDuel();
    virtual bool IsStatue();
    virtual CStaticObject* CastStaticObject();
    virtual CDoor* CastDoor();
    virtual CHolyThing* CastHolyThing();
    virtual CBattleCamp* CastBattleCamp();
    virtual CSignBoard* CastSignBoard();
    virtual CObserverRemains* CastObserverRemains();
    virtual CChair* CastChair();
    virtual CControlTower* CastControlTower();
    virtual CEventController* CastEventController();
    virtual CTeamBattleFlag* CastTeamBattleFlag();
    virtual CAirShipPart* CastAirShipPart();
    virtual CResidenceDecoVisible* CastResidenceDecoVisible();
    virtual CEventMatch* CastEventMatch();
    virtual CFortressFlagpole* CastFortressFlagpole();
    virtual CAirShipSteering* CastAirShipSteering();
    virtual COwnThing* CastOwnThing();
    virtual CContributeData* CastContributeData();
    virtual CMultiPartyCommandChannel* CastMPCC();
    virtual CInstantZone* CastInZone();
    virtual CDuel* CastDuel();
    virtual CStatue* CastStatue();
    virtual bool IsInVehicle();
    virtual bool IsInAirShip();
    virtual bool IsInShuttle();
    virtual unsigned int GetMyAirShipID();
    virtual bool IsRealEnemyTo(CCreature* creature);
    virtual bool IsEnemyTo(CCreature* creature);
    virtual bool IsPvpTo(CCreature* creature);
    virtual bool IsMakeAttackerGuilty(CCreature* attacker, bool flag);
    virtual bool IsMakeAttackerChao(CCreature* attacker);
    virtual bool IsAlive();
    virtual bool IsMovable();
    virtual bool IsFriend();
    virtual bool IsRidingOnGround();
    virtual bool IsRidingWolf();
    virtual bool IsRidingWyvern() const;
    virtual bool IsRidingStrider();
    virtual bool IsRiding();
    virtual void InSight(CObject* object, bool flag);
    virtual void OutOfSight(CObject* object, bool flag);
    virtual FVector GetMeleeAttackPosition(CCreature* attacker);
    virtual const char16_t* GetName() const;
    virtual unsigned int GetDBID() const;
    virtual void SetDBID(unsigned int dbId);
    virtual void SendDB(const char* format, ...) const;
    virtual bool PostLoad();


    // Current InstantAgit/NPC owner helpers read this target byte directly
    // before issuing a second despawn. Keep the portable source boundary
    // typed without changing storage or the vtable.
    bool IsDeletingObject() const noexcept { return m_bDeleting; }

    std::uint32_t m_nId;
    std::int32_t m_nClassId;
    std::int32_t m_Deleted;

protected:
    CObject(const CObject& other);

    bool m_bDeleting;
    std::byte padding_25[3];

public:
    // Current/donor CodeView marks all three instant-zone identifiers public.
    // Several original gameplay owners, including SkillFxFunc.cpp, consume the
    // type ID directly instead of through the nonvirtual getter.
    std::uint32_t m_nInstantZoneID;
    std::uint32_t m_nNextInstantZoneID;
    std::int32_t m_nInstantZoneTypeID;

protected:
    volatile std::int32_t m_nDeleteCount;
    std::uint32_t nDBID;
    std::byte padding_3c[4];

private:
    friend class CSmartID;
    // Current CInstantZone::LeaveZone reads the per-object in-zone type at
    // +0x30 directly before computing the return route.
    friend class CInstantZone;
    // The current CWorldTrapDB diagnostic reads the object's raw instant-
    // zone type at +0x30 while reporting a failed registration removal.
    friend class CWorldTrapDB;
    // Item.cpp writes the private deleting flag before retaining an item for
    // delayed release. The current body performs that write directly.
    friend class CItemReleaseVerifier;

    inline static volatile std::int32_t m_nObjectCount = 0;
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CObject)
};


namespace L2Common {

bool IsAliveCreature(CObject* object);
bool IsAliveCreature2(CObject* object);
bool IsAliveNPC(CObject* object);
bool IsAlivePC(CObject* object);
bool IsDeadCreature(CObject* object);
bool IsDeadNPC(CObject* object);

} // namespace L2Common
