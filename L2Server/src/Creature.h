#pragma once

#include "CWorldObject.h"
#include "Cubic.h"
#include "Inventory.h"
#include "Skill.h"
#include "SkillReuseDelay.h"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>
#include "L2Core/src/inc/XStd.h"
#include "Protocol/src/L2Constants.h"
#include "Shared/src/AtomicJob.h"
#include "Shared/src/Obj.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/SlotType.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/UserConst.h"
#include "Shared/src/Utils.h"
#include "Shared/src/YieldLock.h"

class CCreature;
class CCreatureController;
class CWareHouseDeposit;
class CWareHouseWithdraw;
class CPledgeGamePlayers;
class CClassConstant;
class CContributeData;
class CItem;
class CParty;
class IPacketHelper;
class CSerializedMessage;
class CSkillInfo;
class CStaticObject;
class CUserSocket;
class CCubic;
template <std::size_t Capacity>
class CPacketHelperEx;
struct InventoryParams;
enum ItemChanceNpcType : std::int32_t;
enum PathFindResult : std::int32_t;

namespace ItemGetReason {
enum Enum : std::int32_t;
}

class CSharedCreatureData;
struct SkillAcquireInfo;

class CGeoSector;
class CGeoZone;

// Process-wide count updated by the current CCreature enter/leave/freeze
// paths through Win64 Interlocked increment/decrement operations.
extern volatile std::int32_t g_nHeartbeatingCreatureNum;
extern volatile std::int32_t g_nExistingCreatureNum;

// Current Creature_server.cpp byte at 0x015cd6aa controls the range-skill
// plane-collision gate.
extern bool g_CheckCollisionForRangeSkill;

// Current br_Creature_server.obj table at 015c4d10. Rows are shot grades
// 0..16. Each row stores up to 32 item class IDs selected by its bit mask.
extern int g_nSoulshotIDTables[17][32];

enum CreaturePointType : std::int32_t {
    CPT_HP = 0,
    CPT_MP = 1,
    CPT_CP = 2,
    CPT_MAX = 3,
};

enum SkillAffectObjectTypeEnum : std::int32_t {
    SAFOT_NONE = 0,
    SAFOT_FRIEND = 1,
    SAFOT_NOT_FRIEND = 2,
    SAFOT_UNDEAD_REAL_ENEMY = 3,
    SAFOT_WYVERN_OBJECT = 4,
    SAFOT_ALL = 5,
    SAFOT_CLAN = 6,
    SAFOT_NPC_DEAD_BODY = 7,
    SAFOT_INVISIBLE = 8,
    SAFOT_HIDDEN_PLACE = 9,
    SAFOT_FRIEND_PC = 10,
    SAFOT_NOT_FRIEND_PC = 11,
    SAFOT_NEUTRAL_INVISIBLE = 12,
    SAFOT_MAX = 13,
};

enum CHAR_PARAM_TYPE : std::int32_t {
    VCP_CLASS = 0,
    VCP_LEVEL = 1,
    VCP_STR = 3,
    VCP_DEX = 4,
    VCP_CON = 5,
    VCP_INT = 6,
    VCP_WIT = 7,
    VCP_MEN = 8,
    VCP_HP = 9,
    VCP_MAXHP = 10,
    VCP_MP = 11,
    VCP_MAXMP = 12,
    VCP_SP = 13,
    VCP_CARRINGWEIGHT = 14,
    VCP_CARRYWEIGHT = 15,
    VCP_ATTACKRANGE = 16,
    VCP_PHYSICALATTACK = 17,
    VCP_PHYSICALATTACKSPEED = 18,
    VCP_PHYSICALDEFENSE = 19,
    VCP_PHYSICALAVOID = 20,
    VCP_HITRATE = 21,
    VCP_CRITICALRATE = 22,
    VCP_MAGICALATTACK = 23,
    VCP_MAGICCASTINGSPEED = 24,
    VCP_MAGICDEFENSE = 25,
    VCP_ISGUILTY = 26,
    VCP_CRIMINAL_RATE = 27,
    VCP_COLLISION_RADIUS = 28,
    VCP_COLLISION_HEIGHT = 29,
    VCP_BASE_SPEED = 30,
    VCP_SPEED_MODIFIER = 31,
    VCP_ATTACK_TYPE = 32,
    VCP_CP = 33,
    VCP_MAXCP = 34,
    VCP_HP_REGEN = 35,
    VCP_ULTIMATE_SKILL_POINT = 36,
    VCP_SMALLWINDOW_UPDATE = 37,
    VCP_LUC = 38,
    VCP_CHA = 39,
    VCP_MAX = 40,
};


enum DebuffType : std::int32_t {
    DB_GOOD = 0,
    DB_BAD = 1,
    DB_PHYSICAL_MES = 2,
    DB_MENTAL_MES = 3,
    DB_MAX = 4,
};

enum EnemyType : std::int32_t {
    ENT_PLAYER = 0,
    ENT_MONSTER = 1,
    ENT_BOSS = 2,
    ENT_ALL = 3,
    ENT_MAX = 4,
};

enum FLY_TO_LOCATION_TYPE : std::int32_t {
    FTLT_BASE = 0,
    FTLT_PARABOLA = 0,
    FTLT_STRAIGHT = 1,
    FTLT_BLINK = 2,
    FTLT_CHARGE = 3,
    FTLT_KNOCKBACK = 4,
    FTLT_HALLUCINATION = 5,
    FTLT_FLYMOVE = 6,
    FTLT_KNOCKDOWN = 7,
    FTLT_BACKSTEP = 8,
    FTLT_PULL = 9,
    FTLT_FLOAT_STAND = 10,
    FTLT_FLOAT_LAYDOWN = 11,
    FTLT_MAX = 12,
};

// The spellings are donor-selected source vocabulary. Every numeric value is
// produced or consumed by the current L2Server image.
// enum ChangeTargetReason : std::int32_t {
//     CTR_DEFAULT = 0,
//     CTR_BY_USER = 1,
//     CTR_BY_SKILL = 2,
//     CTR_BY_CONTEXT_MENU = 3,
// }

enum DamageTypeEnum : std::int32_t {
    DAMAGE_NORMAL = 1,
    DAMAGE_HEIGHT = 2,
    DAMAGE_WATER = 3,
    DAMAGE_SKILL = 4,
    DAMAGE_SUICIDE = 5,
    DAMAGE_AREA = 6,
    DAMAGE_OVER_HIT = 7,
    DAMAGE_POISON = 8,
    DAMAGE_TRANSFER = 9,
    DAMAGE_CHRONO = 10,
    DAMAGE_CURSED_WEAPON_EXPIRED = 11,
    DAMAGE_FIST = 12,
    DAMAGE_SUICIDE_SKILL = 13,
    DAMAGE_SHIELD = 14,
    DAMAGE_EVENT = 15,
};

enum MoveModeEnum : std::int32_t {
    MVMD_SIT = 0,
    MVMD_STAND = 1,
    MVMD_WALK = 2,
    MVMD_RUN = 3,
    MVMD_MAX = 4,
};

enum SetL2ParamEnum : std::int32_t {
    STE_LEVEL = 0,
    STE_STR = 1,
    STE_DEX = 2,
    STE_CON = 3,
    STE_INT = 4,
    STE_MEN = 5,
    STE_WIT = 6,
    STE_EXP = 7,
    STE_UNDYING = 8,
    STE_ORG_HP = 9,
    STE_ORG_MP = 10,
    STE_ORG_HP_REGEN = 11,
    STE_ORG_SPEED = 12,
    STE_NOISE = 13,
    STE_BASE_PHYSICAL_ATTACK = 14,
    STE_BASE_ATTACK_SPEED = 15,
    STE_BASE_CRITICAL = 16,
    STE_MAGIC_ATTACK = 17,
    STE_MAGIC_DEFEND = 18,
    STE_PK_COUNTER = 19,
    STE_PK_GAUGE = 20,
    STE_WEIGHT_LIMIT = 21,
    STE_ALIGN = 22,
    STE_EVALUATION = 23,
    STE_SP = 24,
    STE_SOCIAL = 25,
    STE_HP = 26,
    STE_MP = 27,
    STE_CP = 28,
    STE_ENERGY_COUNT = 29,
    STE_SOUL_COUNT = 30,
    STE_VITALITY_POINT = 31,
    STE_FACTION_POINT = 32,
    STE_FACTION_LEVEL = 33,
    STE_FACTION_INFO = 34,
    STE_PVP_POINT = 35,
    STE_RACE = 36,
    STE_SEX = 37,
    STE_P_ABNORMAL_RESIST = 38,
    STE_M_ABNORMAL_RESIST = 39,
    STE_HP_PER = 40,
    STE_MAX_LEVEL = 41,
    STE_LUC = 42,
    STE_CHA = 43,
    STE_MAX = 44,
};

enum TshirtOptionType : std::int32_t {
    NONE = -1,
    ALL = 0,
    WP = 1,
    ARMOR = 2,
};

enum ActionTargetType : std::int32_t {
    ATT_ALL = 0,
    ATT_PC = 1,
    ATT_NPC = 2,
    ATT_SUMMON = 3,
    ATT_MAX = 4,
};


struct ChargeData {
    ChargeData() { SetZero(); }

    ChargeData(const ChargeData&) = default;
    ChargeData(ChargeData&&) = default;
    ChargeData& operator=(const ChargeData&) = default;
    ChargeData& operator=(ChargeData&&) = default;

    void SetData(const FVector& from, const FVector& to, const FVector& direction, double distance) {
        m_From = from;
        m_To = to;
        m_Dir = direction;
        m_Dist = distance;
    }

    void SetZero() {
        m_From.x = 0.0;
        m_From.y = 0.0;
        m_From.z = 0.0;
        m_To.x = 0.0;
        m_To.y = 0.0;
        m_To.z = 0.0;
        m_Dir.x = 0.0;
        m_Dir.y = 0.0;
        m_Dir.z = 0.0;
        m_Dist = 0.0;
    }

    bool IsZero() const {
        constexpr double epsilon = 9.999999747378752e-06;
        return std::fabs(m_To.x) < epsilon && std::fabs(m_To.y) < epsilon && std::fabs(m_To.z) < epsilon;
    }

    FVector GetFrom() const { return m_From; }

    FVector GetTo() const { return m_To; }

    FVector GetDir() const { return m_Dir; }

    double GetDist() const { return m_Dist; }

private:
    FVector m_From;
    FVector m_To;
    FVector m_Dir;
    double m_Dist;
};


struct AbnormalDataForTrigger {
    AbnormalDataForTrigger(std::uint8_t abnormalLevel, ::AbnormalStatusSlotType type);

    AbnormalDataForTrigger() : m_Type(::ABNS_INVALID), m_cAbnormalLevel(0) {}

    ::AbnormalStatusSlotType m_Type;
    std::uint8_t m_cAbnormalLevel;
};


class CCreatureSkillMod {
public:
    enum ReactionType : std::int32_t {
        AvoidSkill = 0,
        CounterSkill = 1,
        MaxReactionType = 2,
    };

    enum ModName : std::int32_t {
        PAttack = 0,
        PDefend = 1,
        MAttack = 2,
        MDefend = 3,
        HPRegen = 4,
        MPRegen = 5,
        CPRegen = 6,
        Speed = 7,
        AttackSpeed = 8,
        MagicSpeed = 9,
        PHit = 10,
        PAvoid = 11,
        MHit = 12,
        MAvoid = 13,
        CriticalProb = 14,
        MagicCriticalProb = 15,
        MaxHP = 16,
        MaxMP = 17,
        MaxCP = 18,
        Range = 19,
        CriticalDamage = 20,
        CancelDamage = 21,
        AttackSpeedByWeapon = 22,
        SafeHeight = 23,
        Breath = 24,
        AreaDamage = 25,
        HitBonusAtNight = 26,
        CriticalPositionFront = 27,
        CriticalPositionSide = 28,
        CriticalPositionBack = 29,
        CriticalDamagePositionFront = 30,
        CriticalDamagePositionSide = 31,
        CriticalDamagePositionBack = 32,
        MPCost0 = 33,
        MPCost1 = 34,
        MPCost2 = 35,
        MPCost3 = 36,
        MPCost4 = 37,
        MPCost5 = 38,
        MPCost6 = 39,
        MPCost7 = 40,
        FatalBlow = 41,
        Shield = 42,
        SkillCritical = 43,
        HealBonus = 44,
        DefenceCriticalProb = 45,
        DefenceCriticalDamage = 46,
        DefenceDDResist = 47,
        PSkillPower = 48,
        MagicDefenceCriticalProb = 49,
        MagicDefenceCriticalDamage = 50,
        MagicCriticalDamage = 51,
        WeightLimit = 52,
        WeightPenalty = 53,
        PSpellPower = 54,
        PAbnormalResist = 55,
        MAbnormalResist = 56,
        MCancelResist = 57,
        DefenceDamagePositionFront = 58,
        DefenceDamagePositionSide = 59,
        DefenceDamagePositionBack = 60,
        HateBonus = 61,
        PhysicalSkillCriticalProbByStat = 62,
        PositionDamageFront = 63,
        PositionDamageSide = 64,
        PositionDamageBack = 65,
        BlockActBonusSelf = 66,
        BlockActBonusTarget = 67,
        PotionBonus = 68,
        PhysicalSkillCriticalProb = 69,
        PhysicalSkillCriticalDamage = 70,
        HitSummon = 71,
        EnergyAttack = 72,
        HitNumber = 73,
        MaxModName = 74,
    };

    enum ModMoveMode : std::int32_t {
        HPRegenMoveMode = 0,
        MPRegenMoveMode = 1,
        CPRegenMoveMode = 2,
        PAvoidMoveMode = 3,
        MaxModMoveMode = 4,
    };

    enum SummonStatBonusMod : std::int32_t {
        SBMN_PAttack = 0,
        SBMN_PDefend = 1,
        SBMN_MAttack = 2,
        SBMN_MDefend = 3,
        SBMN_HP = 4,
        SBMN_MP = 5,
        SBMN_PUseSpeed = 6,
        SBMN_PAttackSpeed = 7,
        SBMN_MUseSpeed = 8,
        SBMN_Speed = 9,
        SBMN_CriticalProb = 10,
        SBMN_PHit = 11,
        SBMN_CriticalDamage = 12,
        SBMN_Max = 13,
    };

    enum PreventDeathParam : std::int32_t {
        PDP_NONE = -1,
        PDP_HP = 0,
        PDP_MP = 1,
        PDP_CP = 2,
        PDP_MAX = 3,
    };

    struct SkillBaseTriggerInfo {
        SkillBaseTriggerInfo(int probability, SkillTargetTypeEnum targetType, bool on) :
            m_Prob(probability), m_eTargetType(targetType), m_bOn(on) {}
        SkillBaseTriggerInfo(int probability, SkillTargetTypeEnum targetType) :
            SkillBaseTriggerInfo(probability, targetType, false) {}
        virtual ~SkillBaseTriggerInfo() = default;

        int m_Prob;
        SkillTargetTypeEnum m_eTargetType;
        bool m_bOn;
#if !defined(_MSC_VER)
        // The shipping MSVC ABI does not reuse this polymorphic base's
        // tail padding for derived members. Preserve that ABI rule on
        // Itanium-ABI hosts without introducing a stored field.
        std::uint64_t : 0;
#endif
    };

    struct SkillTriggerInfo : SkillBaseTriggerInfo {
        SkillTriggerInfo(int probability, SkillTargetTypeEnum targetType) :
            SkillBaseTriggerInfo(probability, targetType),
            m_nAttackerLevelMin(0),
            m_nAttackerLevelMax(0),
            m_nAttackedDamagePer(0.0),
            m_nAttackedDamageDiff(0.0),
            m_eEnemyType(ENT_MAX) {}

        int m_nAttackerLevelMin;
        int m_nAttackerLevelMax;
        double m_nAttackedDamagePer;
        double m_nAttackedDamageDiff;
        EnemyType m_eEnemyType;
    };

    struct SkillAttackTriggerInfo : SkillBaseTriggerInfo {
        SkillAttackTriggerInfo(
            EnemyType enemyType,
            int targetLevelMin,
            int targetLevelMax,
            CriticalType::Enum isCritical,
            int minimumDamage,
            int probability,
            SkillFxChangeType changeType,
            SkillTargetTypeEnum targetType,
            bool on
        ) :
            SkillBaseTriggerInfo(probability, targetType, on),
            m_eEnemyType(enemyType),
            m_nTargetLevelMin(targetLevelMin),
            m_nTargetLevelMax(targetLevelMax),
            m_MinimumDamage(minimumDamage),
            m_Sfct(changeType),
            m_IsCritical(isCritical) {}

        EnemyType m_eEnemyType;
        int m_nTargetLevelMin;
        int m_nTargetLevelMax;
        const int m_MinimumDamage;
        const SkillFxChangeType m_Sfct;
        const CriticalType::Enum m_IsCritical;
    };

    struct SkillSkillTriggerInfo : SkillBaseTriggerInfo {
        SkillSkillTriggerInfo(
            const std::vector<int>* skillIdList,
            int probability,
            SkillTargetTypeEnum targetType,
            bool checkCritical,
            EnemyType enemyType,
            int targetLevelMin,
            int targetLevelMax
        );
        SkillSkillTriggerInfo(
            const std::vector<int>* skillIdList, int probability, SkillTargetTypeEnum targetType, bool checkCritical
        );

        std::vector<int> m_skillIDList;
        const bool m_bCheckCritical;
        EnemyType m_eEnemyType;
        int m_nTargetLevelMin;
        int m_nTargetLevelMax;
    };

    struct SkillAvoidTriggerInfo : SkillBaseTriggerInfo {
        SkillAvoidTriggerInfo(int probability, SkillTargetTypeEnum targetType);
    };

    struct SkillDeathTriggerInfo : SkillBaseTriggerInfo {
        SkillDeathTriggerInfo(int probability, SkillTargetTypeEnum targetType);

        int m_nAttackerLevelMin;
        int m_nAttackerLevelMax;
        EnemyType m_eEnemyType;
    };

    struct SkillAuraInfo {
        SkillAuraInfo(SkillTargetTypeEnum targetType, int skillId) :
            m_eTargetType(targetType), m_nSkillId(skillId), m_bOn(false) {}

        SkillTargetTypeEnum m_eTargetType;
        int m_nSkillId;
        bool m_bOn;
    };

    struct SkillSynergyAuraInfo {
        SkillSynergyAuraInfo(SkillTargetTypeEnum targetType, int skillSynergyId) :
            m_eTargetType(targetType), m_nSkillSynergyId(skillSynergyId), m_bOn(false) {}

        SkillTargetTypeEnum m_eTargetType;
        int m_nSkillSynergyId;
        bool m_bOn;
    };

    struct SkillKillTriggerInfo : SkillBaseTriggerInfo {
        SkillKillTriggerInfo(EnemyType enemyType, int probability, SkillTargetTypeEnum targetType);

        EnemyType m_enemyType;
    };

    struct SkillMagicTypeTriggerInfo : SkillBaseTriggerInfo {
        SkillMagicTypeTriggerInfo(
            ::std::set<int> magicTypes, BadSKillType::Enum goodSkill, int probability, SkillTargetTypeEnum targetType
        ) :
            SkillBaseTriggerInfo(probability, targetType), m_sMagicType(std::move(magicTypes)), m_nGoodSkill(goodSkill) {}

        ::std::set<int> m_sMagicType;
        const BadSKillType::Enum m_nGoodSkill;
    };

    struct SkillAbnormalSlotChangeInfo : SkillBaseTriggerInfo {
        SkillAbnormalSlotChangeInfo(
            std::vector<AbnormalDataForTrigger> abnormalData, int probability, SkillTargetTypeEnum targetType
        );

        std::vector<AbnormalDataForTrigger> m_vAbnormalData;
    };

    struct SkillAbnormalSlotTriggerInfo : SkillBaseTriggerInfo {
        SkillAbnormalSlotTriggerInfo(
            int attackerLevelMin,
            int attackerLevelMax,
            EnemyType enemyType,
            std::vector<AbnormalDataForTrigger> abnormalData,
            int probability,
            SkillTargetTypeEnum targetType
        ) :
            SkillBaseTriggerInfo(probability, targetType),
            m_nAttackerLevelMin(attackerLevelMin),
            m_nAttackerLevelMax(attackerLevelMax),
            m_vAbnormalData(std::move(abnormalData)),
            m_eEnemyType(enemyType) {}

        int m_nAttackerLevelMin;
        int m_nAttackerLevelMax;
        std::vector<AbnormalDataForTrigger> m_vAbnormalData;
        EnemyType m_eEnemyType;
    };

    struct SkillAttackedSkillTriggerInfo : SkillBaseTriggerInfo {
        SkillAttackedSkillTriggerInfo(int probability, ::std::set<int> skillIds, SkillTargetTypeEnum targetType) :
            SkillBaseTriggerInfo(probability, targetType), m_sSkillId(std::move(skillIds)) {}

        ::std::set<int> m_sSkillId;
    };

    struct SkillBlockTriggerInfo : SkillBaseTriggerInfo {
        SkillBlockTriggerInfo(
            EnemyType enemyType,
            int attackerLevelMin,
            int attackerLevelMax,
            int skillId,
            int shieldDefense,
            int probability,
            SkillTargetTypeEnum targetType
        );

        EnemyType m_eEnemyType;
        int m_nAttackerLevelMin;
        int m_nAttackerLevelMax;
        int m_nSkillId;
        int m_nShieldDefense;
    };

    struct SkillInterceptInfo {
        SkillInterceptInfo(CSkillInfo* interceptSkill, double probability) :
            m_pInterceptSkill(interceptSkill), m_dProb(probability) {}

        CSkillInfo* m_pInterceptSkill;
        double m_dProb;
    };

    struct SkillReplaceInfo {
        SkillReplaceInfo() : m_replaceSkillID(0), m_dProb(0.0) {}
        SkillReplaceInfo(int replaceSkillId, double probability) : m_replaceSkillID(replaceSkillId), m_dProb(probability) {}
        bool IsValid() { return m_replaceSkillID > 0; }

        int m_replaceSkillID;
        double m_dProb;
    };

    struct SkillReact {
        int nProb[MaxReactionType];
    };

    struct SkillReflectProb {
        double m_Prob[8];
    };

    struct Mod {
        bool operator!=(const Mod& other) const { return m_Per != other.m_Per || m_Diff != other.m_Diff; }

        double m_Per;
        double m_Diff;
    };

    struct CriticalMaxUp {
        void init();

        double m_nPhysics;
        double m_nMagic;
    };

    struct MPVampiric {
        void init();

        double m_dProb;
        double m_dBonus;
    };

    struct ModLimit {
        SkillFxChangeType m_Type;
        double m_Limit;
    };

    struct DamageBonusInfo {
        double m_dAmountPer;
        double m_dAmountDiff;
    };

    struct AlterReceiveNormalDamage {
        int m_nAlterRatio;
        ActionTargetType m_eType;
    };

    struct BonusInfoInCondition {
        BonusInfoInCondition() = default;

        double m_dAmountPer;
        double m_dAmountDiff;
    };

    using SkillTriggerMap = std::map<CSkillInfo*, SkillTriggerInfo>;
    using SkillAttackTriggerMap = std::map<CSkillInfo*, SkillAttackTriggerInfo>;
    using SkillSkillTriggerMap = std::map<CSkillInfo*, SkillSkillTriggerInfo>;
    using SkillAvoidTriggerMap = std::map<CSkillInfo*, SkillAvoidTriggerInfo>;
    using SkillDeathTriggerMap = std::map<CSkillInfo*, SkillDeathTriggerInfo>;
    using SkillAuraMap = std::map<CSkillInfo*, SkillAuraInfo>;
    using SkillSynergyAuraMap = std::map<int, SkillSynergyAuraInfo>;
    using SkillKillTriggerMap = std::map<CSkillInfo*, SkillKillTriggerInfo>;
    using SkillMagicTypeTriggerMap = std::map<CSkillInfo*, SkillMagicTypeTriggerInfo>;
    using SkillAbnormalSlotChangeMap = std::map<CSkillInfo*, SkillAbnormalSlotChangeInfo>;
    using SkillAbnormalSlotTriggerMap = std::map<CSkillInfo*, SkillAbnormalSlotTriggerInfo>;
    using SkillAttackedSkillTriggerMap = std::map<CSkillInfo*, SkillAttackedSkillTriggerInfo>;
    using SkillBlockTriggerMap = std::map<CSkillInfo*, SkillBlockTriggerInfo>;
    using SkillInterceptMap = std::map<CSkillKey, SkillInterceptInfo>;
    using SkillReplacedMap = std::map<int, SkillReplaceInfo>;
    using SlotDB = std::set<::AbnormalStatusSlotType>;
    using BonusInfoMap = std::map<int, BonusInfoInCondition>;
    using BonusInfoAttMap = std::map<int, short*>;
    using BonusInfoDualMap = std::map<std::CDualKey<int, int>, BonusInfoInCondition>;

    CCreatureSkillMod();
    CCreatureSkillMod(const CCreatureSkillMod&) = default;
    CCreatureSkillMod(CCreatureSkillMod&&) = default;
    ~CCreatureSkillMod() = default;
    CCreatureSkillMod& operator=(const CCreatureSkillMod&) = default;
    CCreatureSkillMod& operator=(CCreatureSkillMod&&) = default;

    void InitAtConstructor();
    double ApplyBonus(double value, int modIndex);
    void InitializeHennaBonus();
    bool PushFixedVPLevel(int level);
    void PopFixedVPLevel();
    CSkillInfo* GetSynergySkillInfo(CCreature* creature, int synergyId);
    bool CanGetDamageHP(CCreature* attacker, CCreature* target);
    bool CanGetDamageMP(CCreature* attacker, CCreature* target);
    bool CanAvoidCanceling(::AbnormalStatusSlotType type);
    BonusInfoInCondition* GetPVEPhysicalDefenceBonus(int category);
    BonusInfoInCondition* GetPVEMagicDefenceBonus(int category);
    BonusInfoInCondition* GetInfectedSkillChangeTime(DebuffType debuffType, int magicType);
    BonusInfoInCondition* GetInfectedSkillChangeTime(int abnormalType, int magicType);
    short* GetPVEAttrDefenceBonus(int category);
    bool NeedToAlterNormalDamage(CCreature& target);
    double AlterNormalDamage(double& damage);
    bool IsCriticalCreateItem();
    bool IsChangeAttackPosition();
    bool HasAbsorbShield();

    SkillTriggerMap m_SkillTriggerMap;
    SkillAttackTriggerMap m_SkillAttackTriggerMap;
    SkillSkillTriggerMap m_SkillSkillTriggerMap;
    SkillAvoidTriggerMap m_SkillAvoidTriggerMap;
    SkillDeathTriggerMap m_SkillDeathTriggerMap;
    SkillAuraMap m_SkillAuraMap;
    SkillSynergyAuraMap m_SkillSynergyAuraMap;
    SkillKillTriggerMap m_SkillKillTriggerMap;
    SkillMagicTypeTriggerMap m_SkillMagicTypeTriggerMap;
    SkillAbnormalSlotChangeMap m_SkillAbnormalSlotChangeMap;
    SkillAbnormalSlotTriggerMap m_SkillAbnormalSlotTriggerMap;
    SkillAttackedSkillTriggerMap m_SkillAttackedSkillTriggerMap;
    SkillBlockTriggerMap m_SkillBlockTriggerMap;
    ::CYieldLock m_InterceptSkillLock;
    SkillInterceptMap m_SkillInterceptMap;
    bool m_bHasInterceptSkill;
    ::CYieldLock m_ReplacedSkillLock;
    SkillReplacedMap m_ReplacedSkillMap;
    bool m_bHasReplacedSkill;
    SkillReact m_SkillReacts[MaxReactionType];
    SkillReflectProb m_SkillRefectProb;
    Mod m_Mods[MaxModName];
    Mod m_MagicSpeedBonusByMagicType[SIMT_MAX];
    Mod m_bakModSpeed;
    Mod m_bakModAttackSpeed;
    struct CriticalMaxUp CriticalMaxUp;
    enum : int {
        MinUndefine = -1000,
    };
    static const double m_ModMinPer[MaxModName];
    int m_nBlockMoveCount;
    bool m_bCanAct;
    bool m_bCanAttack;
    int m_nBlockEquipChestArmorCount;
    bool m_bCanSpell;
    bool m_bCanPSkill;
    bool m_bCanSSkill;
    bool m_bCanTSkill;
    int m_nPreserveAbnormal;
    double m_dPKCountIncreaseProb;
    bool m_bCanGetDamageHP;
    bool m_bCanGetDamageMP;
    double m_dItemDropBonus[4];
    double m_dExpDropBonus[4];
    bool m_bIsReverseAttack;
    double m_dReverseDamagePer;
    bool m_bIsVampiricState;
    double m_dVampiricBonusPer;
    double m_dVampiricProb;
    double m_dVampiricProbNumerator;
    double m_dVampiricProbDenominator;
    double m_dVampiricDefencePer;
    bool m_bIsLowHPState;
    bool m_bIsHighHPState;
    bool m_bIsAvoidAgro;
    double m_dShieldDefenceModPer;
    ::CStats m_HennaBonus;
    ::AbnormalResist m_ARBonus;
    Mod m_ModsMove[MaxModMoveMode][MVMD_MAX];
    double m_dTradeBonusPer[12];
    int m_nManaChargeBonus;
    int m_nTransferDamageRatio;
    int m_TransferDamageTargetId;
    int m_nInfectedSkillDamageUPPer;
    int m_nInfectedSkillDamageUPDiff;
    int m_nAvoidAgroProb;
    bool m_bSeeEvent;
    double m_dPvpPhysicalAttackBonusPer;
    double m_dPvpPhysicalSkillBonusPer;
    double m_dPvpMagicalSkillBonusPer;
    double m_dPvePhysicalAttackBonusPer;
    double m_dPvePhysicalSkillBonusPer;
    double m_dPveMagicalSkillBonusPer;
    double m_dPvBossPhysicalAttackBonusPer;
    double m_dPvBossPhysicalSkillBonusPer;
    double m_dPvBossMagicalSkillBonusPer;
    double m_dPvpPhysicalAttackDefenceBonusPer;
    double m_dPvpPhysicalSkillDefenceBonusPer;
    double m_dPvpMagicalSkillDefenceBonusPer;
    bool m_bIsAllAngleShieldDefence;
    bool m_bIsSingleTarget;
    bool m_bAbnormalLimit;
    bool m_bBlockBuff;
    bool m_bBlockDeBuff;
    double m_dBlockDeBuffDist;
    std::set<SkillIsMagicType> m_setBlockMagicType;
    SetL2ParamEnum m_eParamType;
    double m_dGoddBuffDispelBonusPer;
    double m_dBadBuffDispelBonusPer;
    double m_dGoddBuffAbnormalBonusPer;
    double m_dBadBuffAbnormalBonusPer;
    bool m_bCanCreateMPCC;
    int m_nFishingChangeLevel;
    double m_dFishingChangeBonus;
    bool m_bIsPKProtect;
    int m_SoulMaxCount;
    int m_BarExpForSoulCollect;
    bool m_cannotTransform;
    int m_dMaxDecoSlotCount;
    int m_dMaxJewelSlotCount;
    int m_dMaxAgathionMainSlotCount;
    int m_dMaxAgathionSubSlotCount;
    int m_DivingDebuffCount;
    bool m_BlockTarget;
    double m_dReflectDDPer;
    int m_nMPVampiricIndex;
    MPVampiric m_MPVampiric[10];
    bool m_bCanPreserveHide;
    bool m_bNoMPConsumeByCheapshot;
    int m_MaxEnergyCount;
    int m_nRechargeParam;
    int m_nWeaponEnchantRate;
    int m_nArmorEnchantRate;
    int m_nMaxEnchantIncreaseRate;
    TshirtOptionType m_nEnchantOption;
    SkillFxChangeType m_EnchantType;
    ModLimit m_ModsRegenLimit[3];
    SlotDB m_BlockedSlotType;
    SlotDB m_BlockedAddSlotType;
    ::CYieldLock m_Lock;
    int m_SubJobExpAdjustmentPer;
    int m_SubJobExpAdjustmentDiff;
    int m_SubJobVpAdjustmentPer;
    int m_SubJobVpAdjustmentDiff;
    bool m_IsBerserkerMode;
    int m_MPShieldPer;
    int m_MPShieldCostPer;
    double m_ModSummonBonus[SBMN_Max];
    bool m_CanGetEnergy;
    bool m_CanBeRestarted;
    bool m_bAvoidCancelingSkill[::ABNS_MAX];
    double m_dDamageLimit;
    bool m_bIsDamageLimitedByStatus;
    int m_AbnormalShield;
    TriggerAlterSkillInfo* m_TriggerAlterSkill[4];
    bool m_OnProtectDP;
    bool m_SpeedOutOfCombat;
    bool m_bAppliedStatUpAtNight;
    bool m_bGetItemByExp;
    std::int64_t m_nGetItemByExpUnit;
    int m_nGetItemByExpItemType;
    std::int64_t m_nGetItemByExpItemAmount;
    DamageBonusInfo m_PVPDamageBonusInfo;
    DamageBonusInfo m_PVEDamageBonusInfo;
    DamageBonusInfo m_PVBossDamageBonusInfo;
    AlterReceiveNormalDamage m_stAlterNormalDamage;
    double m_dCreateItemCriticalProb;
    double m_dFishingMonsterProb;
    double m_dFishingStartTime;
    int m_nFishingInstantP;
    BonusInfoMap m_PVEPhysicalDefenceBonusMap;
    BonusInfoMap m_PVEMagicDefenceBonusMap;
    BonusInfoAttMap m_PVEAttrDefenceBonusMap;
    BonusInfoDualMap m_SkillChangeTimeToAbnormalType;
    BonusInfoDualMap m_SkillChangeTimeToMagicType;
    double m_dCanGetRangeDamageHPDistance;
    double m_dCanGetRangeDamageMPDistance;
    bool m_bCanGetRangeDamageHP;
    bool m_bCanGetRangeDamageMP;
    double m_dRealRegist;
    SkillFxPositionType m_eChangeAttackPosition;
    double m_dMpFriendlyPaybackPer;
    double m_dMpFriendlyPaybackProb;
    ::CYieldLock m_lockSkillCritical;
    std::map<int, int> m_mapSkillCritical;
    double m_dRealTargetDispelProb;
    bool m_bPreventDeathParam[PDP_MAX];
    bool m_bItemSkillTransfer;
    double m_dAbsorbShield;
    bool m_bLimitDamageInAbsorbShield;
    int m_nBlockedWeaponIdx;
    int m_nBlockedChestArmorIdx;
    Mod m_ModMpCostAfterEffect[2];
    double m_dEnsoulAddProb;
    double m_dArmorPenetrateFactor;
    double m_dShieldPenetrateFactor;
    double m_dMagicalPenetrateFactor;
    int m_nActiveArtifactSlotGroup;
};

inline CCreatureSkillMod::CCreatureSkillMod() : m_ARBonus{} { InitAtConstructor(); }

inline void CCreatureSkillMod::InitAtConstructor() {
    L2_GUARD_FUNCTION();
    m_MPShieldPer = -1;
    m_nBlockedWeaponIdx = -1;
    m_dRealTargetDispelProb = 3.0;
    m_nBlockedChestArmorIdx = -1;

    m_nBlockEquipChestArmorCount = 0;
    m_nBlockMoveCount = 0;
    m_DivingDebuffCount = 0;
    m_BlockTarget = false;
    m_bNoMPConsumeByCheapshot = false;
    m_SubJobExpAdjustmentPer = 0;
    m_SubJobExpAdjustmentDiff = 0;
    m_SubJobVpAdjustmentPer = 0;
    m_SubJobVpAdjustmentDiff = 0;
    m_IsBerserkerMode = false;
    m_MPShieldCostPer = 0;

    m_dDamageLimit = 0.0;
    m_bIsDamageLimitedByStatus = false;
    m_CanGetEnergy = true;
    m_CanBeRestarted = true;
    m_AbnormalShield = 0;
    m_OnProtectDP = false;
    m_SpeedOutOfCombat = false;
    m_bAppliedStatUpAtNight = false;
    m_bGetItemByExp = false;
    m_nGetItemByExpUnit = 100000000;
    m_nGetItemByExpItemType = 0;
    m_nGetItemByExpItemAmount = 0;

    m_bItemSkillTransfer = false;
    m_dAbsorbShield = 0.0;
    m_bLimitDamageInAbsorbShield = false;

    for (ModLimit& limit : m_ModsRegenLimit) {
        limit.m_Type = SFCT_NONE;
        limit.m_Limit = 0.0;
    }

    m_nPreserveAbnormal = -1;

    for (double& value : m_ModSummonBonus) {
        value = 0.0;
    }
    for (bool& value : m_bAvoidCancelingSkill) {
        value = false;
    }
    for (TriggerAlterSkillInfo*& value : m_TriggerAlterSkill) {
        value = nullptr;
    }

    m_PVPDamageBonusInfo.m_dAmountPer = 0.0;
    m_PVPDamageBonusInfo.m_dAmountDiff = 0.0;
    m_PVEDamageBonusInfo.m_dAmountPer = 0.0;
    m_PVEDamageBonusInfo.m_dAmountDiff = 0.0;
    m_PVBossDamageBonusInfo.m_dAmountPer = 0.0;
    m_PVBossDamageBonusInfo.m_dAmountDiff = 0.0;
    m_stAlterNormalDamage.m_nAlterRatio = 0;
    m_stAlterNormalDamage.m_eType = ATT_ALL;
    m_dCreateItemCriticalProb = 0.0;
    m_dFishingMonsterProb = 0.0;
    m_dFishingStartTime = 0.0;
    m_nFishingInstantP = 0;

    m_dCanGetRangeDamageHPDistance = 0.0;
    m_dCanGetRangeDamageMPDistance = 0.0;
    m_dMpFriendlyPaybackProb = 0.0;
    m_dMpFriendlyPaybackPer = 0.0;
    m_bHasInterceptSkill = false;
    m_bHasReplacedSkill = false;
    m_bCanGetRangeDamageHP = true;
    m_bCanGetRangeDamageMP = true;
    m_dRealRegist = 1.0;
    m_eChangeAttackPosition = SFPT_MAX;

    for (bool& value : m_bPreventDeathParam) {
        value = false;
    }

    m_dEnsoulAddProb = 0.0;
    m_nActiveArtifactSlotGroup = 0;
    m_dArmorPenetrateFactor = 1.0;
    m_dShieldPenetrateFactor = 1.0;
    m_dMagicalPenetrateFactor = 1.0;
}

inline double CCreatureSkillMod::ApplyBonus(double value, int modIndex) {
    L2_GUARD_FUNCTION();
    return value * m_Mods[modIndex].m_Per + m_Mods[modIndex].m_Diff;
}

inline void CCreatureSkillMod::InitializeHennaBonus() { m_HennaBonus.Init(); }

inline bool CCreatureSkillMod::CanAvoidCanceling(::AbnormalStatusSlotType type) { return m_bAvoidCancelingSkill[type]; }

inline double CCreatureSkillMod::AlterNormalDamage(double& damage) {
    damage += static_cast<double>(m_stAlterNormalDamage.m_nAlterRatio) * damage / 100.0;
    return damage;
}

inline bool CCreatureSkillMod::IsChangeAttackPosition() { return m_eChangeAttackPosition != SFPT_MAX; }

inline bool CCreatureSkillMod::HasAbsorbShield() {
    constexpr double epsilon = 9.999999747378752e-06;
    return m_dAbsorbShield > epsilon;
}


struct ItemChanceInfo {
    ItemChanceInfo() = default;

    ItemChanceNpcType m_NpcType;
    std::vector<ItemChanceType> m_vItemChanceTypeList;
};


// Current L2Server constructs this record at 0x006300a8, embeds one instance
// in CCreature and stores additional instances in its infected-skill vector.
// The current list owner is 0x18 bytes, moving the final three fields from the
// TotalChecker donor's legacy +0x30/+0x34/+0x38 positions to the exact current
// +0x38/+0x3c/+0x40 positions.
struct InfectedSkillType {
    using SkillActors = std::list<int>;

    InfectedSkillType() :
        pSkillInfo(nullptr),
        nStartTime(0),
        bSelfCast(false),
        nSourceCreatureId(0),
        m_pChannellingSkillInfo(nullptr),
        nDuration(0),
        nReduceCount(0),
        dReduceDamage(0.0) {}

    InfectedSkillType(const InfectedSkillType&) = default;
    InfectedSkillType(InfectedSkillType&&) = default;
    ~InfectedSkillType() = default;

    InfectedSkillType& operator=(const InfectedSkillType&) = default;
    InfectedSkillType& operator=(InfectedSkillType&&) = default;

    bool IsChannellingSkill() const;
    bool NeedSendVisualEffect() const;
    bool IsMentorMenteeSkill() const;
    bool IsExcludeInfectSkill() const;

    CSkillInfo* pSkillInfo;
    int nStartTime;
    bool bSelfCast;
    unsigned int nSourceCreatureId;
    CSkillInfo* m_pChannellingSkillInfo;
    SkillActors m_SourceCreatureIds;
    int nDuration;
    int nReduceCount;
    double dReduceDamage;
};


// Current CNPC class-constant construction initializes the exact 0x18-byte
// record to {0.0, -1, 0, 0, -1}. CCreature::SetMPReward copies all five
// fields into a list node, while ApplyMPReward increments m_Tick and erases a
// node when m_Tick is no longer below m_ExpireTick.
class MPRewardData {
public:
    enum RewardType : std::int32_t {
        TypeNone = -1,
        Per = 0,
        Diff = 1,
        TypeMax = 2,
    };

    enum RewardScope : std::int32_t {
        ScopeNone = -1,
        Solo = 0,
        Party = 1,
        ScopeMax = 2,
    };

    MPRewardData(double amount, RewardType type, int expireTick, RewardScope scope) :
        m_Amount(amount), m_Type(type), m_Tick(0), m_ExpireTick(expireTick), m_Scope(scope) {}

    MPRewardData() : m_Amount(0.0), m_Type(TypeNone), m_Tick(0), m_ExpireTick(0), m_Scope(ScopeNone) {}

    void Init(RewardType type, double amount, int expireTick, RewardScope scope) {
        m_Amount = amount;
        m_Type = type;
        m_Tick = 0;
        m_ExpireTick = expireTick;
        m_Scope = scope;
    }

    double Amount() { return m_Amount; }

    void Amount(double amount) { m_Amount = amount; }

    RewardType Type() { return m_Type; }

    int Tick() { return m_Tick; }

    int ExpireTick() { return m_ExpireTick; }

    RewardScope Scope() { return m_Scope; }

    bool Expired() { return m_Tick >= m_ExpireTick; }

    void TickInc() { ++m_Tick; }

    bool Empty() { return m_Type == TypeNone || m_Scope == ScopeNone; }


private:
    double m_Amount;
    RewardType m_Type;
    int m_Tick;
    int m_ExpireTick;
    RewardScope m_Scope;
};


// Exact current 0x550-byte modifier embedded in CCreature at +0x2588.
// TotalChecker contributes source vocabulary only. All offsets, extents and
// Clear writes below are fixed by current L2Server code.
class CCreatureSkillDurationMod {
public:
    CCreatureSkillDurationMod() { Clear(); }

    void Clear() {
        bItemChanceActivated = false;
        m_mapItemChanceInfo.clear();
        m_RechargeBoostPer = 0;
        m_VitalityBonusPer = 1.0;
        m_AddedVitalBonusPer = 0;
        m_nVitalitySubPer = 0;
        m_dExpBonusPercent = 0.0;
        m_dSPBonusPercent = 0.0;

        for (int& value : m_DropRateByRune) {
            value = 0;
        }
        for (int& value : m_DropAmountByRune) {
            value = 0;
        }

        runtime::raw_atomic::ExchangeI32(&m_nSpoilBonusPer, 0);
        m_nResurrectionSpecial = 0;
        m_dRecoverHPPer = 0.0;
        m_dRecoverMPPer = 0.0;
        m_dRecoverCPPer = 0.0;
        // Current Clear has no write to p_RecoverSkillInfo at +0x90.
        m_vResurrectionInstantZoneIds.clear();
        m_dRaidPointBonus = 0.0;
        m_StatBonus.Init();
        m_StatBonusLimited.Init();

        for (int& value : m_aStorageBonus) {
            value = 0;
        }
        m_nBuffSlotBonus = 0;
        for (int& value : m_nWrongCasting) {
            value = 0;
        }
        for (int& value : m_nWrongCastingProb) {
            value = 0;
        }

        m_dDamageShieldResist = 0.0;
        m_dInstantKillResist = 0.0;
        m_ConsumePointReducePer = 0;
        m_nAdditionalWorldChatCnt = 0;
        m_ExpBonusPer = 0;
        m_ExpBonusDiff = 0;
        m_nBlockEquipWeaponCount = 0;
        m_nBlockCanControllCount = 0;
        m_TargetMeCount = 0;
        m_AntiCubic = 0;
        m_SuspendConsume = 0;
        m_nFearCount = 0;
        m_nPsychicPowerCount = 0;
        m_RechargeVitalityPoint = 0;
        m_CombatSkillCount = 0;

        for (CCreatureSkillMod::Mod& mod : m_mpcost) {
            mod.m_Per = 1.0;
            mod.m_Diff = 0.0;
        }

        m_mapBlockSkillId.clear();
        m_mapIgnoreSkillId.clear();
        m_SkillCondMap.clear();
        m_ItemCondMap.clear();
        m_bBalthusBuff = false;
        m_bHide = false;
        m_dReflectDDResistPer = 0.0;
        m_dExpSpBonusFishingPercent = 0.0;
        m_dAddedHPRecovery = 0;
        m_nPolymorphID = 0;

        for (double& value : m_dReuseTimeBonusPer) {
            value = 1.0;
        }
    }

    bool IsInInzone(int inzoneId);
    void SetItemChanceInfo(int skillId, ItemChanceNpcType npcType, std::vector<ItemChanceType> itemChanceTypes) {
        // Current L2Server 0x00ace0a4.
        L2_GUARD(m_Lock);
        bItemChanceActivated = true;

        ItemChanceInfo info;
        info.m_NpcType = npcType;
        for (const ItemChanceType& itemChanceType : itemChanceTypes) {
            info.m_vItemChanceTypeList.push_back(itemChanceType);
        }
        m_mapItemChanceInfo.emplace(skillId, info);
    }

    void UnsetItemChanceInfo(int skillId) {
        // Current L2Server 0x00ace630.
        L2_GUARD(m_Lock);
        const auto found = m_mapItemChanceInfo.find(skillId);
        if (found != m_mapItemChanceInfo.end()) {
            m_mapItemChanceInfo.erase(skillId);
        }
        if (m_mapItemChanceInfo.empty()) {
            bItemChanceActivated = false;
        }
    }

    ::CYieldLock m_Lock;
    bool bItemChanceActivated;
    std::map<int, ItemChanceInfo> m_mapItemChanceInfo;
    volatile std::int32_t m_RechargeBoostPer;
    double m_VitalityBonusPer;
    volatile std::int32_t m_AddedVitalBonusPer;
    volatile std::int32_t m_nVitalitySubPer;
    double m_dExpBonusPercent;
    double m_dSPBonusPercent;
    int m_DropRateByRune[2];
    int m_DropAmountByRune[2];
    volatile std::int32_t m_nSpoilBonusPer;
    int m_nResurrectionSpecial;
    double m_dRecoverHPPer;
    double m_dRecoverMPPer;
    double m_dRecoverCPPer;
    CSkillInfo* p_RecoverSkillInfo;
    std::vector<int> m_vResurrectionInstantZoneIds;
    double m_dRaidPointBonus;
    ::CStats m_StatBonus;
    ::CStats m_StatBonusLimited;
    int m_aStorageBonus[8];
    int m_nBuffSlotBonus;
    int m_nWrongCasting[SIMT_MAX];
    int m_nWrongCastingProb[SIMT_MAX];
    double m_dDamageShieldResist;
    double m_dInstantKillResist;
    volatile std::int32_t m_ConsumePointReducePer;
    int m_nAdditionalWorldChatCnt;
    int m_ExpBonusPer;
    int m_ExpBonusDiff;
    int m_nBlockEquipWeaponCount;
    int m_nBlockCanControllCount;
    int m_TargetMeCount;
    int m_AntiCubic;
    volatile std::int32_t m_SuspendConsume;
    int m_nFearCount;
    int m_nPsychicPowerCount;
    int m_RechargeVitalityPoint;
    int m_CombatSkillCount;
    CCreatureSkillMod::Mod m_mpcost[SIMT_MAX];
    std::map<int, int> m_mapBlockSkillId;
    std::map<int, int> m_mapIgnoreSkillId;
    std::map<int, int> m_SkillCondMap;
    std::map<int, int> m_ItemCondMap;
    bool m_bBalthusBuff;
    int m_dAddedHPRecovery;
    int m_nPolymorphID;
    bool m_bHide;
    double m_dReflectDDResistPer;
    double m_dExpSpBonusFishingPercent;
    double m_dReuseTimeBonusPer[SIMT_MAX];
};


// Complete current three-byte item modifier embedded in CCreature at +0x2580.
// Its owning CCreature is not approximated here.
class CCreatureItemMod {
public:
    bool m_bOnlyUseEquipedWeaponActiveSkill;
    bool m_bNoAttackWeapon;
    bool m_bHpLimitBreak;
};


// Complete current four-byte builder-command modifier embedded in CCreature
// at +0x2ad8. The following four bytes belong to the enclosing CCreature's
// alignment before m_vSrcPos. They are not part of this value type.
class CCreatureBuilderCmdMod {
public:
    int m_nAbnormalChangeType;
};


// Complete current area modifier embedded in CCreature at +0x2b20. The
// enclosing CCreature is not approximated here.
class CCreatureAreaMod {
public:
    bool m_bIsUnderMotherTree;
    bool m_bWasUnderMotherTree;
    bool m_bWasInDamage;
    bool m_bIsInDamage;
    bool m_bWasInPoison;
    bool m_bIsInPoison;
    bool m_bWasInSwamp;
    bool m_bIsInSwamp;
    bool m_bWasFrozen;
    bool m_bWasInInstantSkill;
    bool m_bIsInInstantSkill;
    bool m_bIsInBattleField;
    bool m_bWasInBattleField;
    bool m_bWasInPvPZone;
    bool m_bIsInPvPZone;
    bool m_bWasInSSQZone;
    bool m_bIsInSSQZone;
    bool m_bWasInPeaceField;
    bool m_bIsInPeaceField;
    bool m_bWasNoRestart;
    bool m_bIsNoRestart;
    bool m_bWasUnderWater;
    bool m_bIsUnderWater;
    bool m_bIsNearUnderWater;
    int m_nWaterSurfaceHeight;
    double m_dSpeedModByArea;
    double m_dHpRegenAreaBonus;
    double m_dMpRegenAreaBonus;
    int m_nLeavingMessageNO;
    double m_dHeightDamage;
    bool m_bIsNearShuttle;
    bool m_bWasInDragonLair;
    bool m_bIsInDragonLair;
    bool m_bWasInNoSkill;
    bool m_bIsInNoSkill;
    bool m_bIsInTrainingZone;
    bool m_bIsInOnlyPvEZone;
    bool m_bMPCCAsParty;
};


// Exact current six-coordinate box embedded in CCreature. The default
// construction and half-open Include condition are both created in the
// current image. Set is inlined by the current SetTlmBox3 producer.
class FBox3 {
public:
    FBox3() noexcept : x1(0), y1(0), z1(0), x2(0), y2(0), z2(0) {}

    FBox3(int x1, int y1, int z1, int x2, int y2, int z2);

    bool Include(int x, int y, int z) const noexcept { return x1 <= x && x < x2 && y1 <= y && y < y2 && z1 <= z && z < z2; }

    void Set(int newX1, int newY1, int newZ1, int newX2, int newY2, int newZ2) noexcept {
        x1 = newX1;
        y1 = newY1;
        z1 = newZ1;
        x2 = newX2;
        y2 = newY2;
        z2 = newZ2;
    }

private:
    int x1;
    int y1;
    int z1;
    int x2;
    int y2;
    int z2;
};


class CClanHelpTable : public Singleton<CClanHelpTable> {
public:
    bool GetFlag(int sourceClassIndex, int targetClassIndex) const {
        return m_SlimTable[sourceClassIndex][targetClassIndex];
    }

    void SetFlag(int sourceClassIndex, int targetClassIndex, bool enabled) {
        m_SlimTable[sourceClassIndex].SetFlag(targetClassIndex, enabled);
    }

    void SaveTable();
    void LoadTable();

private:
    CBitStorage<15000, int, 469> m_SlimTable[15000];
};


// Complete current CCreature owner envelope. New virtuals are kept in exact
// slot order 163..421. Declarations inherited from CObject/CWorldObject are
// repeated only where the current CCreature vtable supplies an override.
class CCreature : public CWorldObject {
public:
    // Current CRT memory-class initializer is immediately before the
    // closed CCreatureController id 97.
    L2_DECLARE_OBJECT_REGISTRATION(CCreature)

    friend class CPledgeGamePlayers;
    friend class CWareHouseDeposit;
    friend class CWareHouseWithdraw;
    friend class CSkillEffect_i_set_reuse_delay;
    friend class CSkillEffect_i_call_my_skill_list;

    enum PathFindType : std::int32_t {
        UsePathFind = 0,
        NoPathFind = 1,
    };

    enum FOLLOW_TYPE : std::int32_t {
        FOLLOW_NONE_TYPE = 0,
        FOLLOW_DIST_TYPE = 1,
        FOLLOW_OFFSET_TYPE = 2,
        FOLLOW_ABSOLUTE_DIST_TYPE = 3,
    };

    enum Position : std::int32_t {
        POSITION_1 = 0,
        POSITION_2 = 1,
        POSITION_3 = 2,
        POSITION_4 = 3,
        MAX_POSITION = 4,
    };

    struct SkillTargetData {
        void Init();

        std::vector<::CSafePointer2<CCreature>> c;
        std::vector<double> d;
    };

    struct ChannelingSkillTargetData {
        void Init();

        std::vector<::CSafePointer2<CCreature>> c;
    };

    struct FollowData {
        FollowData();

        void Init();
        bool SetData(const char followType, const int first, const int second);

        char m_FollowType;
        int m_FollowDistance;
        int m_FollowAngle;
        int m_FollowOffsetX;
        int m_FollowOffsetY;
        int m_FollowerSID;
    };

    L2_ASYNC_DECLARE_IMPLICIT_EMPTY_2_SLOT(__async_arg_ResetNeighbor);

    L2_ASYNC_DECLARE_OUT_OF_LINE_CONST_REF_FIELDS_1(__async_arg_LookNeighbor, FVector, previousPosition);

    L2_ASYNC_DECLARE_IMPLICIT_FIELDS_1(__async_arg_LookNeighborByNPC, std::int32_t);

    L2_ASYNC_DECLARE_IMPLICIT_FIELDS_3(__async_arg_LookItems, std::int32_t, std::int64_t, std::int32_t);

    CCreature();
    CCreature(const CCreature& other);
    ~CCreature() override;

    static CSPointer<CCreature> FindObjectSP(std::uint32_t smartId);

    static bool CheckIsRealFriend(CCreature* first, CCreature* second);
    static bool CheckRelationWithCaster(
        CCreature* caster, CCreature* target, SkillAffectObjectTypeEnum affectObjectType, bool requireBattleField
    );

    void Init();
    void Unfreeze();
    void DoNothing();
    int GetPrivateID() const;
    void SetPrivateID(int privateId);
    FVector GetDetailLocation();
    void TurnDirToTarget(CWorldObject& target);
    bool GetBlinkDestPos(double distance, SkillDirectionType direction, FVector& destination);
    bool GetBlinkDestPos(double distance, const FVector& direction, FVector& destination);
    bool GetAttackFrontPos(CCreature* target, FVector& position, FAngle& direction);
    bool BlinkReturnPos(FVector& destination);
    bool GetTargetBackPos(CCreature* target, FVector& position);
    bool GetSidestepPos(CCreature* target, FVector& position, SkillFxPositionType positionType);
    void ValidateLocation();
    void ValidateLocationInVehicle(unsigned int vehicleId);
    void ValidateLocationInAirShip(unsigned int airShipId);
    void ValidateLocationInShuttle(unsigned int shuttleId);
    void StopMove();
    void InitializeBuilderCmdMod();
    void InitializeItemMod();
    void InitializeAreaMod();
    void InitializeSkillDurationMod();
    void InitializeSkillMod();
    void InitAcquiredSkills();
    void EnterWorldImpl();
    void AreaCheckForEveryTick(CGeoZone* zone, CGeoSector* sector, int tick);
    void GeoFieldEffect(CGeoZone* zone, CGeoSector* sector, int tick, const FVector& position);
    void ReflectDayAndNightEffect();
    void CheckOneGiWok();
    void CheckHighHPState();
    void CheckLowHPState();
    void FearAction(bool moveAwayFromSource);
    void CubicActionTimer();
    void CheckCubicExpired();
    void ReflectHeightDamage();
    void SetMaxMagicLevel();
    bool IsIgnoreSkill(int skillId);
    bool IsLockedBuff(int skillId);
    int PrepareSpelledInfoChangedBuffer(char* buffer, int& capacity);
    int GetSkillReuseDelayGroup(int skillId);
    double GetRemainReuseDelaySec(int skillId, int level, int subLevel);
    int CalcReuseDelay(CSkillInfo& skillInfo);
    int SetSkillReuseDelay(CSkillInfo& skillInfo);
    bool ConsumeMP(double amount);
    bool ConsumeHP(double amount);
    bool ConsumePledgeNameValue(int amount);
    bool CanUseBadSkill(CWorldObject* target, bool reportFailure);
    bool IsDoubleCasting(const int skillId, const int level, const int subLevel);
    bool RemoveSkillUsableTime(int skillId) { return m_SkillReuseDelay.RemoveSkillUsableTime(skillId); }
    CSkillInfo* GetActiveSkill(int skillId, int& level, int& subLevel);
    const CSkillInfo* GetUsableSkill(int skillId);
    int GetAlterSkill(const CSkillInfo** skillInfo);
    CSkillInfo* GetAuraSkill(int skillId, int& level, int& subLevel, bool& enabled);
    bool AuraSkillOnOff(int skillId, bool enabled);
    CSkillInfo* GetToggleSkill(int skillId, int& level, int& subLevel, bool& enabled);
    int GetAbnormalTime(const CSkillInfo* skillInfo, bool ignoreSkillMod);
    void ReportInfectedSkills(CUserSocket* socket);
    int CheckLastArrow(CrystalType crystalType);
    int CheckLastBolt(CrystalType crystalType);
    void GetInfectedSkillsToDBSave(std::vector<InfectedSkillType>& infectedSkills);
    void PackingInfectedSkillsToDBSave(const std::vector<InfectedSkillType>& infectedSkills, IPacketHelper& packet);
    bool HasReduceSkill(InfectedSkillType& infectedSkill);
    bool IsReduceSkill(InfectedSkillType& infectedSkill);
    bool HasAbnormalCategory(DebuffType debuffType);
    bool HasAbnormalStatusSlot(::AbnormalStatusSlotType abnormalType);
    int _FindFirstUnsharedInstantBuff(bool shared) const;
    int _FindFirstReplacableBuffSlot(DebuffType debuffType, bool selfDuration, bool passive) const;
    bool SetPassiveSkillPumpStart(int skillId, int level, int subLevel, SkillPumpStartType startType);
    bool SetPassiveSkillPumpEnd(int skillId, int level, int subLevel, SkillPumpEndType endType);
    void SetTacticalSignToMe(int creatureId);
    void BroadcastTacticalSignInfo();
    void BroadcastCubicSkillUse(CWorldObject& target, const CSkillInfo* skillInfo, int first, int second);
    DefaultIOBuffer* MakePacketForMagicSkillUse(
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
    void ResetAllTacticalSignToMe();
    void ResetAllTacticalSignToMeExcludeMyParty();
    bool DespawnCubicBySlot(int slot);
    void DespawnAllCubic(bool notifyParameterChange);
    void EraseCubicNotSelfCast();
    bool SummonCubic(int cubicId, int cubicLevel, bool bySelfCasting);
    void SendTacticalSignInfo(User& user);
    void SendTacticalSignInfo(User& user, int signId);
    void AccumulateContribute(CCreature* contributor, double contribution);
    void SafeAccumulateContribute(CCreature* attacker, double contribution);
    bool HasPassiveSkill(int skillId, int level, int subLevel);
    int GetOnGroupToggleSkillId(int groupId);
    bool _GroupToggleSkillOn(int skillId, int groupId);
    bool GroupAuraSkillOnPumpEnd(int skillId, int groupId);
    bool GroupToggleSkillOnPumpEnd(int skillId, int groupId);
    void OnGroupToggleState(int count, const unsigned char* packet);
    void LoadAbnormalData(unsigned long count, const unsigned char* packet);
    bool CheckTriggerSkillCondition(CSkillInfo& skillInfo, CWorldObject& target);
    int TriggerSkillByDamage(CCreature& attacker, double damage);
    int TriggerSkillByAttack(CCreature& target, double damage, bool critical);
    int TriggerSkillByAvoid(CCreature& attacker);
    int TriggerSkillByBlock(CCreature& attacker, int shieldDefense);
    int TriggerSkillByDeath(CCreature& killer);
    int TriggerSkillByKill(CObject& killedObject);
    int TriggerSkillBySkill(int skillId, CWorldObject& target, bool critical);
    int TriggerSkillByMagicType(int magicType, CWorldObject& target, bool harmful, bool multiTarget);
    int TriggerSkillByAttackedSkill(int skillId, CCreature* attacker);
    int _ChangeSkillByAbnormalSlot(CCreature* source, CWorldObject& target);
    int _TriggerSkillByAbnormalSlot(CCreature* source, CWorldObject& target);
    void TriggerAlterSkill(AlterSkill::Event event, AbnormalStatusSlotType abnormalType);
    bool IsPledgeBossNPC();
    bool InChaoPermissionBattleField(CCreature& creature);
    void _CheckBuffLock();
    bool _DeleteAbnormalStatus(
        const int index,
        CCreature* source,
        const CSkillInfo* skillInfo,
        bool notifyChange,
        SkillPumpEndType pumpEndType,
        bool unknown
    );
    bool _DeleteAbnormalStatus(
        ::AbnormalStatusSlotType abnormalType,
        CCreature* source,
        CSkillInfo* skillInfo,
        bool notifyChange,
        SkillPumpEndType pumpEndType,
        bool unknown
    );
    bool DispelByNames(const std::vector<wchar_t*>& skillNames, CCreature* source, CSkillInfo* skillInfo);
    int DispelBySlot(
        ::AbnormalStatusSlotType abnormalType,
        CCreature* source,
        int maximumAbnormalLevel,
        const CSkillInfo* skillInfo,
        SkillPumpEndType pumpEndType,
        bool sameCasterOnly
    );
    bool DispelBySlotProb(::AbnormalStatusSlotType abnormalType, CCreature* source, int probability, CSkillInfo* skillInfo);
    bool DispelByIdLevelMySelf(int skillId, int skillLevel, int skillSubLevel);
    bool DispelByIdLevelMySelf(const CSkillInfo* skillInfo, bool force);
    bool DispelByCategory(
        DebuffType debuffType, CCreature* source, int probability, const CSkillInfo* skillInfo, int maximumSlots
    );
    bool DispelAll(int magicLevel, CCreature* source, const CSkillInfo* skillInfo);
    bool DispelAllByGM(SkillPumpEndType pumpEndType, bool goodOnly);
    bool DispelByBuffProtectionFlag(unsigned long protectionFlag);
    bool DetachAllChannelling();
    void CancelCastingSkill(int magicType, CWorldObject* target);
    void PostJobAfterDispel(const std::vector<const CSkillInfo*>& skills, int skillId, int skillLevel, int skillSubLevel);
    int AuraSkillBySkill();
    int _SynergyAuraByAbnormalSlot();
    void _AddAbnormalStatus(
        const CSkillInfo& skillInfo, CCreature* source, bool third, bool fourth, SkillPumpStartType startType
    );
    void UpdateSummonStatBonus(CSummon* summon, CCreatureSkillMod::SummonStatBonusMod mod);
    void UpdateSummonStatBonus();
    bool ToggleSkillOnOff(int skillId, bool enabled);
    bool IsToggleSkillOnOff(int skillId);
    int ToggleSkillAllOff(bool notifyClient);
    int AuraSkillAllOff(bool notifyClient);
    bool _HaveSkill(int skillId, int level) const;
    bool _HaveSkill(int skillId, int level, int subLevel) const;
    void GetSkillListByType(
        std::vector<const CSkillInfo*>& activeSkills,
        std::vector<const CSkillInfo*>& passiveSkills,
        std::vector<const CSkillInfo*>& toggleSkills,
        std::vector<const CSkillInfo*>& auraSkills
    );
    int GetAcquiredSkillLevel(int skillId);
    int GetAcquiredSkillSubLevel(int skillId);
    bool _AcquireSkill(int skillId, int level, int subLevel, bool replace, bool saveToDatabase);
    bool AcquireSkill(int skillId, int level, int subLevel, bool replace, bool saveToDatabase);
    void ModifyAcquireSkill(int skillId, int level, int subLevel, bool replace);
    void AcquireSkills(const std::vector<SkillAcquireInfo*>& skills, bool autoOnly, bool replace);
    void _AcquireSkills(const std::vector<SkillAcquireInfo*>& skills, bool autoOnly, bool replace);
    void _OnDeletedSkill(AcquiredSkillInfo& skillInfo);
    bool _DeleteAcquireSkill(int skillId);
    void _DeleteAllAcquireSkills(bool preserveDatabaseSkills);
    void DeleteAllAcquireSkills(bool preserveDatabaseSkills);
    bool DeleteAcquireSkill(int skillId);
    int IsLockedSkill(int skillId);
    void SetSkillLock(int skillId, int skillLevel, int skillSubLevel, int lockValue);
    double GetUserPremiumExpBonus();
    double GetUserPremiumSpBonus();
    double GetPCCafeUserExpSPBonus();
    double GetUserPremiumSpoilBonus();
    double GetUserPremiumDropBonus();
    double GetUserPremiumVitalItemDropBonus();
    int GetUserDropRateByRune(int runeType);
    int GetUserDropAmountByRune(int runeType);
    std::uint32_t SkillEffectResetSkillReuseDelay(const std::vector<int>& skillIds, int reuseDelayLimit);
    std::uint32_t ResetSkillReuseDelay(int skillId);
    std::uint32_t ResetSkillReuseDelayAll();
    std::uint32_t OlympiadResetSkillReuseDelay(int reuseDelayLimitSeconds, std::vector<int>& excludedSkillIds);
    std::uint32_t SkillEffectReduceSkillReuseDelay(const std::deque<int>& skillIds, int reduction, int reuseDelayLimit);
    void SetMPReward(CCreature* victim, CCreature* contributor);
    void ApplyMPReward();
    double GetWeightPenalty(double weightPenalty);
    double GetWeightLimit(double weightLimit);
    double GetWeaponRandomDamage() const;
    double GetWeaponTraitBonus(::AttackType attackType);
    double GetGeneralTraitBonus(CCreature& creature, TraitTypeEnum traitType, bool weaponTrait);
    std::int64_t GetInt64Param(SetL2ParamEnum parameter);
    void GetAttackTypeValue(const CSkillInfo* skillInfo, int& attackType, int& attackValue);
    void GetDefendValue(CCreature* creature, int attackType, int& defendValue);
    double GetAttributeBonus(CObject* target, const CSkillInfo* skillInfo);
    double GetSkillAttributeBonus(CCreature* target, const CSkillInfo* skillInfo, bool weaponTrait);
    double GetSpritShotExtraBonus() const;
    double GetAttackTraitBonus(CCreature& creature);
    CSharedItemData* GetEquipedWeapon() const;
    double EquipedArmorSpeedByCrystal() const;
    int GetArmorGradePenalty() const;
    int GetWeaponGradePenalty() const;
    bool IsInItemGradePenalty() const;
    SkillFxPositionType GetDirectionAt(CCreature* creature);
    double CalcPositionDamage(CCreature& creature, CCreature& directionOwner, double damage);
    bool IsShieldDefend2(CCreature& attacker, double probability);
    bool IsShieldDefend(CCreature& attacker, double probability);
    double CalcTimeFactorBonusForSkill(double timeFactor, CSkillInfo& skillInfo);
    double CalcBasicCriticalRate(CObject* target) const;
    static double GetResurrectExpRatio(double resurrectExpRatio, double bonus);
    std::int64_t CalcResurrectExp(const double resurrectExpRatio, bool premium);
    void SetLevel(unsigned int level);
    void ChangeRace(RaceType race, int fighterMagicType);
    int LevelUp(unsigned int levelCount);
    int LevelDown(unsigned int levelCount);
    int UltimateSkillGaugeInc(int value);
    std::int64_t GetResurrectExpPremium(CCreature* creature, double resurrectExpRatio);
    std::int64_t GetResurrectExp(CCreature* creature, double resurrectExpRatio);
    bool ResurrectBy(CCreature* resurrector, std::int64_t resurrectExp);
    bool BeResurrectAsked(CCreature* resurrector, int resurrectExpRatio, bool immediately);
    void AttackTestByCount(CCreature& target, int count, User& builder);
    void AttackTestBySec(CCreature& target, int seconds, User& builder);
    void SkillTest(CCreature& target, CSkillInfo& skillInfo, int count, User& builder);
    void IncrementParam(ParameterType parameter, std::int64_t amount);
    void GetBonusBoundary(int bonusType, double& upperBoundary, double& lowerBoundary);
    double GetDamageByLimit(double damage);
    double GotDamageWithCp(
        CCreature* attacker, CCreature* attackerMaster, double damage, bool critical, DamageTypeEnum damageType, bool reflect
    );
    double CalcDamageWithAbsorbShield(double damage, bool& absorbed);
    double CalcDamageWithMPShield(double damage, double& absorbedMP);
    double ApplyIncreaseLimit(CreaturePointType pointType, double& difference, double maximum, double current);
    double IncreaseCP_diff(double difference, int reason, unsigned int objectId, bool force);
    double IncreaseCP_per(double ratio, int reason, unsigned int objectId);
    double DecreaseCP_diff(double difference, int reason, unsigned int objectId);
    double DecreaseCP_per(double ratio, int reason, unsigned int objectId);
    double IncreaseHP_diff(double difference, bool broadcast, int reason, unsigned int objectId, bool force);
    double IncreaseHP_per(double ratio, bool broadcast, int reason, unsigned int objectId);
    double IncreaseMP_diff(double difference, int reason, unsigned int objectId, bool force);
    double IncreaseMP_per(double ratio, int reason, unsigned int objectId);
    bool CanBeBuffed(const CSkillInfo* skillInfo, CCreature* source);
    bool CheckBlockDebuff(CCreature* creature);
    void AbandonAttackLeaderOf(CCreature* creature);
    void ResetPassingPos(FVector position);
    void AddPassingPos(const FVector& position);
    std::uint64_t GetPassingPosList(FVector* positions);
    bool BeClosedToPassingPos(const FVector& position);
    FVector GetPassingPosOnSight(
        const FVector& source, const FVector& destination, PathFindResult& result, CCreature* creature
    );
    Position GetMyPositionFrom(const CCreature* creature) const;
    bool IsCloserThan(CCreature* creature, const FVector& position);
    bool IsValidAttackLeaderCandidate();
    bool SetAttackLeaderRelated(CCreature* creature, CCreature* expectedCreature);
    bool GetStatusValue(CHAR_PARAM_TYPE parameter, int& value);
    bool IsTalkableDistance(CWorldObject* object) const;
    bool CanUseMoveToTargetAirShip(CObject* object, bool& canMove);
    void ValidateForceAction(CObject* target, bool& forceAction);
    bool ValidateTargetOnAction(CObject* target);
    int CriticalRateForInfo(VALIDATE::Enum type) const;
    bool IsCheckBot1(int first, int second);
    bool IsCheckBot2(int first, int second);
    bool IsCheckBot3(int value);
    SlotType IsEquiped(int itemIndex, SlotType slotType);
    bool IsEquipedItem(int itemIndex);
    bool IsEquipedItemByClassId(int classId);
    bool IsEquippedWithForcedItem(SlotType slotType);
    bool IsDebuffExist();
    std::uint64_t GetAbnormalCount();
    static void PushCreatureAfterCheckRangeAndCanSee(
        CCreature* creature,
        CCreature* target,
        int affectLimitBase,
        int affectLimitRandom,
        bool affectLimitToPc,
        int& targetCount,
        SkillTargetData& targetData,
        double distance,
        bool checkCanSee,
        FVector position
    );
    static void PopCreatureAfterCheckRangeAndCanSee(
        CCreature* creature,
        CCreature* target,
        int rangeTargetType,
        int affectLimit,
        int& targetCount,
        SkillTargetData& targetData,
        double distance,
        bool checkCanSee,
        FVector position
    );
    void GatherNeighborCreature(
        FVector position,
        int affectLimitBase,
        int affectLimitRandom,
        bool affectLimitToPc,
        SkillTargetData& targetData,
        int affectRange,
        int affectScopeMinZ,
        int affectScopeMaxZ,
        SkillAffectObjectTypeEnum affectObjectType,
        CCreature* creature,
        CCreature* exceptCreature,
        bool checkCanSee,
        bool unknown
    );
    void GatherNeighborParty(
        SkillTargetData& targetData,
        int affectRange,
        int affectScopeMinZ,
        int affectScopeMaxZ,
        CCreature* creature,
        bool unknown
    );
    void GatherNeighborPledge(
        SkillTargetData& targetData,
        int affectRange,
        int affectScopeMinZ,
        int affectScopeMaxZ,
        CCreature* creature,
        bool unknown
    );
    void GatherNeighborAirShip(FVector position, CCreature* creature, CSkillInfo* skillInfo, bool checkCanSee);
    void GatherNeighborCreatureArc(
        int rangeTargetType,
        int affectLimit,
        bool affectLimitToPc,
        SkillTargetData& targetData,
        const DamageRangeType& damageRange,
        int affectScopeMinZ,
        int affectScopeMaxZ,
        SkillAffectObjectTypeEnum affectObjectType,
        CCreature* creature,
        CCreature* exceptCreature,
        FVector position,
        int angle
    );
    void GatherNeighborCreatureMelee(
        int rangeTargetType,
        int affectLimit,
        bool affectLimitToPc,
        SkillTargetData& targetData,
        const DamageRangeType& damageRange,
        int affectScopeMinZ,
        int affectScopeMaxZ,
        SkillAffectObjectTypeEnum affectObjectType,
        CCreature* creature,
        CCreature* exceptCreature,
        FVector position,
        int angle
    );
    void GatherNeighborCreatureSquare(
        int rangeTargetType,
        int affectLimit,
        bool affectLimitToPc,
        SkillTargetData& targetData,
        const DamageRangeType& damageRange,
        int affectScopeMinZ,
        int affectScopeMaxZ,
        SkillAffectObjectTypeEnum affectObjectType,
        CCreature* creature,
        CCreature* exceptCreature,
        FVector position
    );
    void GatherNeighborCreatureByPoint(
        int rangeTargetType,
        SkillTargetData& targetData,
        int affectRange,
        int affectScopeMinZ,
        int affectScopeMaxZ,
        SkillAffectObjectTypeEnum affectObjectType,
        CCreature* creature,
        FVector position
    );
    void GatherNeighborStaticObject(
        FVector position,
        std::vector<::CSafePointer2<CStaticObject>>& targets,
        std::vector<double>& distances,
        int affectRange,
        int affectScopeMinZ,
        int affectScopeMaxZ,
        SkillAffectObjectTypeEnum affectObjectType,
        CCreature* creature
    );
    void GatherNeighborChargeTarget(
        int rangeTargetType,
        int affectLimit,
        SkillTargetData& targetData,
        int affectRange,
        int affectScopeMinZ,
        int affectScopeMaxZ,
        SkillAffectObjectTypeEnum affectObjectType,
        CCreature* creature,
        CCreature* exceptCreature
    );
    void GatherNeighborMPCC(SkillTargetData& targetData, int affectRange, CCreature* creature, bool deadOnly);
    void GatherAllSummonAndMaster(SkillTargetData& targetData, bool exceptMaster);
    void GatherNeighborCreatureRingRange(
        FVector position,
        int rangeTargetType,
        int affectLimit,
        SkillTargetData& targetData,
        int affectRange,
        const DamageRangeType& damageRange,
        int affectScopeMinZ,
        int affectScopeMaxZ,
        SkillAffectObjectTypeEnum affectObjectType,
        CCreature* creature,
        CCreature* exceptCreature,
        bool checkCanSee,
        bool unknown
    );
    bool GetItemReuseDelay(int itemId, std::uint32_t* reuseEndTick);
    std::uint32_t GetItemRemindTimeMs(int itemId);
    void SetItemReuseDelay(int itemId, std::uint32_t reuseDelay);
    void InsertItemReuseDelay(int itemId, std::uint32_t reuseDelay);
    CCreature* GetTargetWithoutFail();
    int GetHPToShow() const;
    bool ValidateItemSkills();
    void ReflectWaterEffect();
    void MesCheck();
    void InitMes(MesType::Enum type);
    bool ValidateExtraParam();
    int MesCount(MesType::Enum type);
    void InfectedMes(MesType::Enum type, bool infected);
    void InitMes();
    int MakeAbnormalVisualEffectPacket(CPacketHelperEx<20480>& packet);
    int _MakeAbnormalStatusPacket(CPacketHelperEx<20480>& packet);
    int MakeAbnormalStatusPacket(CPacketHelperEx<20480>& packet);
    void SendAbnormalStatusToUserTargetsMe(User& user);
    void _BroadCastAbnormalStatusToUsersTargetMe();
    void AddUserTargetsMe(User& user);
    void ResetListTargetMe();
    void DelUserTargetsMe(User& user);
    static bool IsSeeEvent(CCreature* actor, CCreature* target);
    bool _NeedLookNeightbor(CCreature& target, double distanceSquared, int range);
    bool _ForAllNeighborInLookNeighbor(CCreature* target, const FVector& previousPosition);
    bool _ForAllNeighborInLookNeighborByNPC(CCreature* target, int range, int& npcCount);
    void _CheckTrapInLookNeighbor(CCreature* target, const FVector& previousPosition);
    void __async_LookNeighbor(CExecutionArgumentPad* argument);
    void __async_LookNeighborByNPC(CExecutionArgumentPad* argument);
    void __async_LookItems(CExecutionArgumentPad* argument);
    void ResetNeighbor();
    void HandleSerializedMessages();
    void PushSerializedMessage(CSerializedMessage* message);
    void PumpStartAllPumpEffects();
    void OnInfectedSkillBy(const CSkillInfo& skillInfo, bool needSendVisualEffect);
    bool InfectedSkillBy(
        CCreature* attacker, const CSkillInfo* skillInfo, const CSkillInfo* parentSkillInfo, int infectedTime
    );
    bool InfectedInstantDurationSkillBy(CCreature* attacker, const CSkillInfo* skillInfo);
    bool AttachedAbnormalStatusByChannelling(CCreature* attacker, CSkillInfo* skillInfo, CObject* target);
    bool DeleteChannelingAbnormalStatus(
        CCreature* source, const CSkillInfo* skillInfo, bool notifyChange, SkillPumpEndType pumpEndType
    );
    bool DetachedAbnormalStatusByChannelling(CCreature* source, CSkillInfo* skillInfo);
    bool _InfectedSkillBy(
        CCreature* attacker,
        const CSkillInfo* skillInfo,
        const CSkillInfo* parentSkillInfo,
        int infectedTime,
        bool& needSendVisualEffect
    );
    InfectedSkillType CreateInfectedSkillType(
        CCreature* attacker, const CSkillInfo* skillInfo, const CSkillInfo* parentSkillInfo, int infectedTime
    );
    int _FindSameBuff(
        const CSkillInfo* skillInfo,
        bool& differentInstantItem,
        int& buffCount,
        int& debuffCount,
        int& songDanceCount,
        int& triggerCount,
        CCreature* attacker
    ) const;
    bool _FindReplacableBuffAndRemove(
        CCreature* attacker, const CSkillInfo* skillInfo, bool& needSendVisualEffect, int replaceIndex
    );
    bool IsCriticalInfected(const CSkillInfo* skillInfo) const;
    int ChangeScaleMode(int scaleMode);
    void CheckAbnormalVisualEffect();
    void ActivateRecoverSkill();
    void _CheckAbnormalVisualEffect();
    bool _CheckAbnormalVisualEffect_InfectedSkill(::std::set<int>& visualEffects);
    void _CheckAbnormalVisualEffect_ToggleSkill(::std::set<int>& visualEffects);
    void _CheckAbnormalVisualEffect_AuraSkill(::std::set<int>& visualEffects);
    void _ProcAbnormalVisualEffect(::std::set<int>& visualEffects, bool forceUpdate);
    void ValidateAbnormalResist();
    bool ValidateEnchantExtraBonus();
    void ClampMin(VALIDATE::Enum type);
    bool SetAllOverPointToDefault();
    void ValidateHPMPCP(bool preserveOverPoint);
    bool ValidateSkillMod();
    void ApplyGotDamageBySkill(CCreature* attacker, double damage, double appliedDamage);

    static void SendMagicAttckInfo(CCreature* source, CCreature* target, MagicAttackInfo::Enum info);
    static void SendMessageOnDamaged(
        CCreature* damaged,
        CCreature* attacker,
        int damage,
        bool sendToAttacker,
        bool sendToDamaged,
        int shieldDefence,
        int appliedDifference
    );

    static const double m_ParamMinValue[VALIDATE::MAX];

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void TimerExpired(int id) override;
    bool SetVisible(bool visible) override;
    bool IsVisible() override;
    bool SetTargetable(bool targetable, bool flag) override;
    bool IsTargetable() override;
    bool SetShowNametag(bool show) override;
    bool IsShowNametag() override;
    int GetContainerIndex() override;
    double GetCollisionRadius() const override;
    void SetInZoneID(unsigned int inZoneId) override;
    void SetInstantZoneTypeID(int instantZoneTypeId) override;
    void SetAlwaysSkillSuccess(bool enabled) { m_AlwaysSkillSuccess = enabled; }
    bool GetAlwaysSkillSuccess() const { return m_AlwaysSkillSuccess; }
    int Level() const override;
    FAngle GetDir() const override;
    int GetIndex() const override;
    CObject* Copy() override;
    bool IsCreature() override;
    CCreature* CastCreature() override;
    bool IsInVehicle() override;
    bool IsInAirShip() override;
    bool IsInShuttle() override;
    unsigned int GetMyAirShipID() override;
    bool IsRealEnemyTo(CCreature* creature) override;
    bool IsEnemyTo(CCreature* creature) override;
    bool IsPvpTo(CCreature* creature) override;
    bool IsAlive() override;
    bool IsRidingOnGround() override;
    bool IsRidingWolf() override;
    bool IsRidingWyvern() const override;
    bool IsRidingStrider() override;
    bool IsRiding() override;
    FVector GetMeleeAttackPosition(CCreature* attacker) override;
    const char16_t* GetName() const override;
    void DecideDistance(CMoveToTargetShareData* data, CSmartID creatureId) override;
    void SetCollisionHeight(double height) override;
    double GetCollisionHeight() const override;
    void SetCollisionRadius(double radius) override;
    bool IsActive() override;
    FVector GetPos() const override;
    void SetDir(const FAngle& direction) override;
    FVector GetPosAsFollowTarget() const override;

    // Slots 163..227.
    virtual const char16_t* GetName2(User* user);
    virtual bool SetVisibleBySkill(bool visible, bool broadcast);
    virtual bool IsInvisibleBySkill();
    virtual void SetPeaceZone(bool peaceZone);
    virtual void OnCreate();
    virtual void OnCreated();
    virtual void OnCreateFakeNPC();
    virtual void Delete();
    virtual void OnEnterWorld(FVector& position);
    virtual void Say(const char16_t* message);
    virtual CCreature* GetDelegate();
    virtual void SetSpelledInfoChanged();
    virtual bool ChangeClass(ClassType classType, int level);
    virtual double EquipedWeaponValidate(VALIDATE::Enum type) const;
    virtual double EquipedWeaponExtraValidate(VALIDATE::ExtraEnum type) const;
    virtual double EquipedArmorsValidate(VALIDATE::Enum type) const = 0;
    virtual double EquipedWeaponCriticalProb() const;
    virtual ::AttackType GetAttackType() const;
    virtual ::AttackType GetAttackType(CSharedItemData** itemData);
    virtual ::AttackType GetRealAttackType();
    virtual int EquipedWeaponSoulShotCount();
    virtual int EquipedWeaponSpiritShotCount();
    virtual int EquipedArmorsMpBonus() const;
    virtual double EquipedArmorsHpBonus() const;
    virtual std::int64_t SetExp(std::int64_t experience);
    virtual std::int64_t ExpInc(std::int64_t experience, bool applyBonus);
    virtual void SetGuilty(char guilty);
    virtual char GetGuilty();
    virtual bool IsGuilty();
    virtual std::int64_t SPInc(std::int64_t sp);
    std::int64_t SetSP(std::int64_t sp);
    virtual void SetKarma(int karma);
    virtual void ChangeKarmaByDelta(int delta, bool broadcast);
    virtual CrystalType GetMaxCrystal() const;
    virtual void OnGotDamage(CCreature* attacker, double damage);
    virtual double LevelBonus() const;
    virtual double RegenBonusByMotionEx();
    virtual double HPRegenBonusByMotionEx();
    virtual double MPRegenBonusByMotionEx();
    virtual double CPRegenBonusByMotionEx();
    virtual bool ValidateBaseHP();
    bool ValidateBaseMP();
    bool ValidateBaseCP();
    bool ValidateBaseData();
    bool ValidateBaseAttributeData();
    bool ValidateHPRegen();
    bool ValidateCPRegen();
    bool ValidateMPRegen();
    virtual bool ValidatePHit();
    virtual bool ValidatePAvoidRate();
    virtual bool ValidatePAttack();
    virtual bool ValidateMHit();
    virtual bool ValidateMAvoidRate();
    virtual bool ValidateMAttack();
    virtual bool ValidatePDefend();
    virtual bool ValidateShieldDefend();
    virtual bool ValidateShieldDefendRate();
    virtual void ValidateGradePenalty(VALIDATE::Enum type);
    virtual bool ValidateMDefend();
    virtual bool ValidateAttackRange();
    virtual bool ValidateAttackTime();
    virtual bool ValidateMUseSpeed();
    virtual bool ValidatePUseSpeed();
    virtual bool ValidatePCriticalProb();
    virtual bool ValidateMCriticalProb();
    virtual bool ValidateWeaponType();
    virtual bool ValidateArmorType();
    virtual bool ValidateOrgAttackSpeed();
    virtual bool ValidateCarryWeight();
    virtual bool ValidateStorageSlot();
    virtual bool ValidateAllOnWeaponArmors();
    virtual void ValidateAllOnSIM();
    virtual void ValidateParameters(int parameter, bool force);

    // Slots 228..289.
    virtual bool ValidateOrgHP() = 0;
    virtual bool ValidateOrgMP() = 0;
    virtual bool ValidateOrgCP() = 0;
    virtual bool ValidateOrgHPRegen() = 0;
    virtual bool ValidateOrgMPRegen() = 0;
    virtual bool ValidateOrgCPRegen() = 0;
    virtual bool ValidateOrgPAttack();
    virtual bool ValidateOrgPDefend();
    virtual bool ValidateOrgMAttack();
    virtual bool ValidateOrgMDefend();
    virtual bool ValidateOrgPCriticalProb();
    virtual bool ValidateOrgMCriticalProb();
    virtual bool ValidateOrgPHit();
    virtual bool ValidateOrgMUseSpeed();
    virtual bool ValidateOrgPAvoidRate();
    virtual bool ValidateOrgMAvoidRate();
    virtual bool ValidateOrgShieldDefend();
    virtual bool ValidateOrgShieldDefendRate();
    virtual bool ValidateOrgMHit();
    virtual bool Morph(int classId) = 0;
    virtual void UnMorph() = 0;
    virtual void SetMorphID(int classId);
    virtual int GetMorphID();
    virtual void ChangeMorph() = 0;
    virtual bool ValidateOrgSpeed() = 0;
    virtual bool ValidateLevel();
    virtual void ValidateAttackSpeedAndTime();
    virtual bool ValidateAttackSpeed();
    virtual bool ValidateSpeed() = 0;
    virtual bool ValidateLevelUP();
    virtual bool ValidateMoveToTargetSpeedAndSendInfo();
    virtual double GetAttackModifier();
    virtual double GetMoveModifier();
    virtual double GetPAttack();
    virtual double GetPHit();
    virtual double GetMHit();
    virtual double GetPAvoid();
    virtual double GetPDefend();
    virtual double GetShieldDefenseRate();
    virtual double GetShieldDefense();
    double GetSoulShotExtraBonus();
    virtual bool CanUseShot(const CSkillInfo* skillInfo);
    double GetBonusAndTurnNextSoulShotState();
    void TurnOffSoulshot(bool force);
    void TurnOffSpiritshot(bool force);
    void TurnOffFishingShot(bool force);
    void BroadcastEmitterSpawn(int emitterId, int creatureId, int unknown);
    void BroadcastFlyToLocation(int x, int y, int z, FLY_TO_LOCATION_TYPE type, int time, int unknown1, int unknown2);
    virtual void BroadcastWaitType(int waitType);
    virtual void BroadcastCombatModeStart();
    virtual void BroadcastCombatModeFinish();
    virtual bool IsGMInvisible() const;
    virtual void CheckSummonBattleMode();
    virtual void CheckSummonBattleTimeStamp();
    virtual void InitCoupleAction();
    void DisableSetItemSkill();
    void DisableNewSetItemSkill();
    void EnableSetItemSkill(int setItemId, bool additional2);
    void SetItemSkillEnDisable(int setItemId, bool additional2);
    bool ValidateSetItem(::SlotType slotType);
    void _ValidateSetItemOnEquipItem(CItem* item, ::SlotType slotType, bool force);
    void _ValidateSetItemOnUnequipItem(::SlotType slotType, bool force);
    void GetChestSlotNewSetItemInfo(::std::map<int, int>& setItems, bool& enchantedEffect);
    void GetEquipSlotNewSetItemInfo(::std::map<int, int>& setItems);
    ::std::map<int, int> NewSet(bool& enchantedEffect);
    void NewSetItemSkillEnDisable(const ::std::map<int, int>& setItems, bool enchantedEffect);
    void ValidateNewSetItem();
    ::std::map<int, int> ShapeShiftedSet(int* slotIndices);
    void ShapeShiftedSetItemSkillEnDisable(::std::map<int, int> setItems);
    void ValidateShapeShiftedSetItem();
    virtual bool CanEquip(CItem* item);
    virtual void TakeOffItems();
    ::SlotType equipItemWithoutDuplication(CItem* item, int firstSlot, int lastSlot);
    virtual int EquipItem(CItem* item, ::SlotType slotType);
    virtual ::SlotType UnequipItem(CItem* item, bool force);
    virtual ::SlotType UnequipItem(::SlotType slotType, bool force);
    virtual bool SwapAgathionSlotItems(CItem* first, ::SlotType firstSlot, CItem* second, ::SlotType secondSlot);
    virtual void _OnSwapAgathionSlotItems(CItem* first, ::SlotType firstSlot, CItem* second, ::SlotType secondSlot);
    virtual void UnequipWeapon(bool force);
    virtual void RestoreUnequipedWeapon();
    virtual void UnequipArmor(::SlotType slotType);
    virtual void RestoreUnequipedArmor(::SlotType slotType);
    virtual bool UseItem(CItem& item, int count, bool force, int unknown);
    virtual void UseETCItem(CItem* item, std::int64_t count, int unknown1, bool force, int unknown2);
    virtual bool DropItem(CItem* item, std::int64_t count, FVector& position, bool unknown) = 0;
    bool CanDropItem(CItem* item, std::int64_t count, FVector& position, bool unknown);
    bool PickItem(CItem* item);

    // Slots 290..357.
    virtual bool GetItem(CItem* item, bool unknown);
    virtual void DeleteAllItemsInInventory();
    virtual int GetInventoryLimit();
    virtual int GetQuestInventoryLimit();
    virtual int GetArtifactInventoryLimit();
    virtual bool StopStore(bool first, bool second);
    virtual bool CancelStore();
    virtual int GetPrivateStoreLimit();
    virtual int GetPrivateStoreBuyLimit();
    virtual int GetPrivateBankLimit();
    virtual void _OnEquipItem(CItem* item, ::SlotType slotType);
    virtual void _OnUnequipItemProcess(CItem* item, ::SlotType slotType);
    virtual void _OnUnequipItem(CItem* item, ::SlotType slotType);
    virtual void SendSystemMessage(const char16_t* message, const char16_t* argument);
    void SendSystemMessage(const char16_t* message, const std::u16string& argument) {
        SendSystemMessage(message, argument.c_str());
    }
    virtual void SendSystemMessage(unsigned int messageId);
    virtual void SendSystemMessage_d(unsigned int messageId, int value);
    virtual void SendSystemMessage_Q(unsigned int messageId, std::int64_t value);
    virtual void SendSystemMessageWithSelf(unsigned int messageId);
    bool AddItemToInventory3(
        int itemClassId,
        int count,
        bool reject,
        int enchanted,
        int blessed,
        int eroded,
        ItemOptionKey optionKey,
        int intensiveItemClassId,
        BaseAttribute attribute
    );
    bool AddItemToInventory(
        int itemClassId,
        std::int64_t count,
        ItemGetReason::Enum reason,
        int enchanted,
        int blessed,
        int eroded,
        ItemOptionKey optionKey,
        int intensiveItemClassId,
        BaseAttribute attribute
    );
    virtual bool AddItemToInventory(InventoryParams& parameters);
    virtual bool AddItemToInventory2(CItem* item);
    virtual bool DeleteItemInInventory(int itemClassId, std::int64_t count, ::AtomicJob* job, bool first, bool second);
    virtual bool DeleteItemInInventory(int itemIndex);
    virtual bool DeleteItemInInventoryBeforeCommit(int itemClassId, std::int64_t count);
    virtual void LeaveWorld(bool save);
    virtual void MoveToLocation(int x, int y, int z, PathFindType pathFindType);
    virtual void MoveToLocationRelative(CAbstractCarrier* carrier, int x, int y, int z);
    virtual bool TeleportToLocation(
        int x,
        int y,
        int z,
        unsigned int heading,
        int instanceId,
        TeleportSourceType::Enum source,
        CCreature* requestor,
        int unknown
    ) = 0;
    virtual bool TeleportWithAirShip(CAirShip* airShip, FVector position, unsigned int heading);
    CAirShip* GetMyAirShip();
    bool CheckShowMovePos(int id, const FVector& position);
    static void ShowLines(
        User& user,
        std::vector<FVector>& points,
        int duration,
        unsigned char red,
        unsigned char green,
        unsigned char blue,
        int emitterId
    );
    void ShowBlocks(const std::vector<FVector>& points);
    void ShowLoc(User* user);
    virtual bool GetOnAirShip(CAirShip* airShip);
    virtual bool GetOffAirShip(CAirShip* airShip, FVector position);
    virtual bool Die(CCreature* killer, int reason);
    virtual void Suicide(DamageTypeEnum damageType, int reason);
    virtual void SendResurrectionConfirmDlg();
    virtual void AttackOutOfRange();
    virtual void OnKillNpc(CCreature* creature);
    virtual void SocialAction(int actionId, int unknown, bool broadcast);
    virtual void ChangeMoveType(::MoveType moveType, unsigned int reason);
    virtual double HeightDamage(double height);
    virtual double GotDamage(
        CCreature* attacker, double damage, double unknown, bool critical, DamageTypeEnum damageType, bool reflect
    );
    virtual double GotDamageBy(CCreature* attacker, double damage, bool critical, DamageTypeEnum damageType, bool reflect);
    virtual double AttackedBy(
        CCreature* attacker, double damage, bool critical, int attackId, ::AttackType attackType, ::SlotType slotType
    );
    virtual double GotDamageByEnvironment(double damage, bool critical, DamageTypeEnum damageType, bool reflect);
    virtual void ParamChanged(std::int64_t parameterMask);
    virtual void ParamChanged();
    virtual void ValidateChanged(int parameterMask);
    virtual void ValidateClear();
    virtual void ValidateExChanged(int parameterMask);
    virtual void ValidateExClear();
    virtual void ValidateExParameters(int parameterMask);
    virtual bool CanNormalAttack() const;
    virtual void OnAreaCheckForEveryTick(int tick);
    virtual void OnLevelChanged(int oldLevel);
    virtual void ChangeTalkTarget(CObject* target);
    virtual void Action(CObject* target, bool first, bool second);
    virtual bool CoupleAction(int actionId, bool first, bool second, int unknown);
    virtual bool SkillAction(
        int skillId, bool first, bool second, CItem* item, const CSkillInfo* skillInfo, int unknown1, int unknown2
    );
    virtual void OnUseSkillFinished(int targetSid, int skillId, int skillLevel, int skillSubLevel, int success);
    virtual void OnUseSkillStarted(int targetSid, int skillId, int skillLevel, int skillSubLevel);
    virtual void ActionFailed(int reason);
    virtual void ChangeTarget(CObject* target, ChangeTargetReason reason);
    virtual bool HaveSkill(int skillId, int level);
    virtual bool HaveSkill(int skillId, int level, int subLevel);
    virtual int GetSkillLevel(int skillId);
    virtual int GetSkillSubLevel(int skillId);
    virtual AcquiredSkillInfo GetAcquiredSkillInfo(int skillId);
    virtual int GetBroadcastDist(const int unknown) const;
    virtual void ValidateSkillList();
    virtual bool FreezeIfNobodyNearMe() = 0;

    // Slots 358..421.
    virtual void SendFreezeStatusMessage();
    virtual int CalcCriminalRate();
    virtual void SendStatusUpdate(CHAR_PARAM_TYPE parameter, int value, unsigned int unknown);
    virtual void BroadcastHP(int value, unsigned int unknown);
    virtual void BroadcastHPMP(int value, unsigned int unknown);
    virtual int GetCorpseTime() const;
    virtual double IntBonusF() const;
    virtual double DexBonusF() const;
    virtual double MenBonusF() const;
    virtual double ConBonusF() const;
    virtual double StrBonusF() const;
    virtual double WitBonusF() const;
    virtual double LucBonusF() const;
    virtual double ChaBonusF() const;
    virtual int GetClass() const;
    virtual double HPRegen() const;
    virtual double BaseHP() const;
    virtual double MPRegen() const;
    virtual double BaseMP() const;
    virtual double PAttack() const;
    virtual double PDefend() const;
    virtual double PParry() const;
    virtual double CarryWeight() const;
    virtual MoveModeEnum GetMoveMode();
    virtual bool IsFrozen(bool unknown);
    virtual int InventoryWeight() const;
    virtual FVector RandomDirection();
    virtual int GetAttackRange() const;
    virtual ::ArmorType GetArmorType() const;
    virtual std::int64_t HasItem(unsigned int itemClassId);
    virtual void SetMyAirShipID(unsigned int airShipId);
    virtual unsigned int GetMyShuttleID();
    virtual void SetMyShuttleID(unsigned int shuttleId);
    virtual bool OnMagicSkillUsePacket(int skillId, bool first, bool second);
    virtual bool IsInDuel() const = 0;
    virtual bool IsDuelEnemy(CCreature* creature);
    virtual bool IsDuelAlly(CCreature* creature);
    virtual bool IncreaseSoulCount(int count, const int emitterId);
    virtual bool DecreaseSoulCount(int count);
    virtual bool CanSoulCollectByExp(std::int64_t experience) const;
    virtual bool IsCameraMode();
    bool IsInCombatMode();
    virtual void SetInCombatMode(bool inCombat);
    virtual CCreatureController* GetController();
    void SetController(CCreatureController* controller) { m_pController = controller; }
    virtual void CheckRelaxType();
    virtual void _OnAbnormalStatusChanged(
        const CSkillInfo& skillInfo, CCreature* caster, SkillPumpStartType startType, bool unknown
    );
    virtual void _OnAbnormalStatusDeleted(
        const CSkillInfo& skillInfo, CCreature* caster, SkillPumpEndType endType, int unknown
    );
    virtual void OnCorpseDecayed();
    virtual bool OnRegenTimer(FVector& position);
    virtual void OnActivate();
    virtual bool CanDoAction_china();
    virtual void AnnouncePickItem(CItem* item);
    virtual bool IsSkillDebugger() const;
    virtual bool IsSkillDebugMode() const;
    virtual bool CanForceAttack() const;
    virtual CParty* GetParty();
    virtual CUserSocket* GetSocket() const;
    virtual void SetNickName(const char16_t* nickname);
    virtual void SendMyInfo() = 0;
    virtual bool IsSummonedNpc() = 0;
    virtual bool IsSummonedFriendNpc() = 0;
    virtual void MakeEnterWorldPacket() = 0;
    virtual void ClearEnterWorldPacket() = 0;
    virtual double GetCriticalDamagePer();
    virtual double GetCriticalDamageDiff();

protected:
    void _ChangeAbnormalStatus(const int index, CCreature* creature, const CSkillInfo* skillInfo);
    void _GetPassiveSkillListByType(const CSkillInfo* skillInfo, std::vector<const CSkillInfo*>& passiveSkills);
    void _GetSkillListByType(
        const CSkillInfo* skillInfo,
        std::vector<const CSkillInfo*>& activeSkills,
        std::vector<const CSkillInfo*>& passiveSkills,
        std::vector<const CSkillInfo*>& toggleSkills,
        std::vector<const CSkillInfo*>& auraSkills
    );
    void ResetSkillsAfterValidateSkill(
        std::vector<const CSkillInfo*>& activeSkills,
        std::vector<const CSkillInfo*>& passiveSkills,
        std::vector<const CSkillInfo*>& toggleSkills,
        std::vector<const CSkillInfo*>& auraSkills
    );
    void ResetSkillsAfterValidateSkillForTransform(
        std::vector<const CSkillInfo*>& activeSkills,
        std::vector<const CSkillInfo*>& passiveSkills,
        std::vector<const CSkillInfo*>& toggleSkills,
        std::vector<const CSkillInfo*>& auraSkills
    );

public:
    RefHelpers m_RefHelpers;
    ChargeData m_ChargeData;
    ChargeData m_TargetChargeData;
    ::std::map<int, int> m_mapNewSetItem;
    ::CYieldLock m_NewSetItemSkillLock;
    ::std::map<int, int> m_mapShapeShiftedSetItem;
    ::CYieldLock m_ShapeShiftedSetItemSkillLock;
    ::CYieldLock m_lockValidate;
    ::CExecutionSerializer<CCreature, 32> m_LookNeighborSerializer;
    ::CYieldLock m_SerializerMessageLock;
    std::vector<CSerializedMessage*> m_SerializedMessages;
    std::int32_t m_nValidateChanged;
    std::int32_t m_nValidateExChanged;
    bool m_bValidateExChanged;

protected:
    CSkillReuseDelay m_SkillReuseDelay;
    volatile std::int32_t m_lSaveSkillReuseDelay;

public:
    bool m_bDietMode;
    double m_dGMSpeed;
    bool m_bFloatingMoveMode;
    CSharedCreatureData* m_data;
    void* m_reserved;
    ::CYieldLock* m_lock;
    CInventory m_inv;
    int m_nSetItemClassId;
    double m_dPenaltyTargetCanResist;
    bool m_bIsGMKill;
    std::int32_t m_nDead;
    CCreatureSkillMod m_SkillMod;
    CCreatureItemMod m_ItemMod;
    CCreatureSkillDurationMod m_SkillDurationMod;
    CCreatureBuilderCmdMod m_BuilderCmdMod;
    FVector m_vSrcPos;
    FVector m_vMoveSkillDest;
    bool m_bDecExp;
    std::uint32_t m_dwDamagedTick;
    std::uint32_t m_dwBossNPCHurtTick;
    CCreatureAreaMod m_AreaMod;
    int m_nCurrentWeight;
    double m_dSpeedModByWeight;
    double m_dHPRegenModByWeight;
    double m_dMPRegenModByWeight;
    double m_dCPRegenModByWeight;
    std::int64_t m_nDownExp;
    int m_bIsPetAlive;
    int m_nEnergyCount;
    std::uint32_t m_dwEnergyStartTime;
    int m_nSongDanceCount;
    std::uint32_t m_dwFakeDeathStartTime;
    bool m_bIsNight;
    bool m_bIsSysMsg;
    bool m_bIsLowHPLevel1;
    bool m_bIsLowHPLevel2;
    bool m_bIsHP1SysMsg;
    bool m_bIsHP2SysMsg;
    std::uint32_t m_dwMPowerStorage;
    std::int16_t m_sPowerCount;
    double m_dSpiritShotHealBonus;
    std::int16_t m_sNowSoulShot;
    bool m_bIsRestarting;
    bool m_bIsInitiativeViolet;
    bool m_bIsSkillViolet;
    std::uint32_t m_nSummonLifeLastTick;
    bool m_bMoving;
    FVector m_vMovingDirection;
    FVector m_vMovingDest;
    std::uint32_t m_nLastTick;
    std::uint32_t m_nMoveTick;
    ::std::set<int> m_NeighborCreatureSet;
    ::std::set<int> m_NeighborItemSet;
    int m_nNeighborMapResetCount;
    FVector m_vPrevLookPos;
    std::atomic<std::int32_t> PrepareLookNeighbor;
    int m_nAuraTick;
    int m_nTickCount;
    bool m_bIsFreezing;
    int m_nCommitTick;
    int m_nCommitTick2;
    std::atomic<std::int32_t> m_nDeathCount;
    std::atomic<std::int32_t> m_bParamChanged;
    bool m_bAbnormalStatusChanged;
    bool m_bValidateChanged;
    int m_eDeadDamage;
    std::int32_t m_nCombatModeTimeStamp;
    FVector m_vPosOnTick;
    CSmartID m_spTalkTarget;
    CSmartID m_spSittingChair;
    CSmartID m_spTarget;

private:
    CCreatureController* m_pController;

public:
    std::uint32_t m_nBowDelay;
    CSmartID m_spActionTarget;
    ::CYieldLock m_AcquiredSkillsSect;
    ::std::map<int, AcquiredSkillInfo> m_AcquiredSkills;
    ::CYieldLock m_SkillLockSect;
    ::std::map<int, int> m_SkillLockMap;
    ::CYieldLock m_BuffLockSect;
    ::std::map<int, const CSkillInfo*> m_BuffLockMap;
    ::CYieldLock m_InfectedSkillsSect;
    std::vector<InfectedSkillType> m_InfectedSkills;
    int m_maxCubic;
    ::CYieldLock m_spriteLock;
    ::std::map<int, std::pair<CCubic*, CubicActivationInfo>> m_cubicMap;
    InfectedSkillType m_InstantDurationSkill;
    ::CYieldLock m_InstantDurationSect;

protected:
    ::CYieldLock m_SkillListSect;
    ::std::map<int, const CSkillInfo*> m_ActiveSkills;
    std::vector<const CSkillInfo*> m_PassiveSkills;
    ::std::map<int, std::pair<const CSkillInfo*, bool>> m_ToggleSkills;
    ::std::map<int, std::pair<const CSkillInfo*, bool>> m_AuraSkills;
    ::std::map<int, int> m_GroupToggleSkills;
    ::std::map<int, std::pair<std::CDualKey<int, int>, bool>> m_PassivePumpInfo;

public:
    SkillTargetData m_vSkillTarget;
    ChannelingSkillTargetData m_vChannelingSkillTarget;
    std::vector<::CSafePointer2<CStaticObject>> m_vSkillStaticTarget;
    std::vector<double> m_vSkillStaticTargetDistance;
    ::std::set<int> m_AbnormalVisualEffectMap;
    ::std::set<int> m_AbnormalVisualEffectForTest;
    ::CYieldLock m_OptionSkillSect;
    std::vector<const CSkillInfo*> m_OptionSkills;
    ::CYieldLock m_UsedItemsSect;
    ::std::map<int, std::uint32_t> m_UsedItemsLastTimeMap;
    FBox3 m_tlmBox3;
    bool m_bzAttackingTrait[66];
    double m_dzAttackingTraitValue[66];
    bool m_bzDefendingTrait[66];
    bool m_bzDefendingTraitPerfect[66];
    double m_dzDefendingTraitValue[66];
    ::std::map<int, int> m_CorpseDropList;
    ::std::map<int, int> m_SeedDropList;
    std::uint32_t m_nResidenceGuardTick;
    std::uint32_t m_nMercenaryKey;
    CContributeData* m_pContribute;
    ::CYieldLock m_PledgeContributeSect;
    ::std::map<int, double> m_PledgeContributes;
    std::uint32_t m_nPledgeContributeSaveTick;
    std::uint32_t m_nBeforeEnterWorldTick;
    ::CYieldLock PassingPosSect;
    FVector m_vPassingPos[15];
    std::int16_t m_nCurrentPassingPos;
    std::int16_t m_nCurrentPassingPosSize;

protected:
    CClassConstant* pClassConstant;
    bool m_bClassChangeable;
    bool m_bLeft;

public:
    bool m_bCanEscape;
    FVector m_NonTargetSkillPos;
    std::atomic<std::uint32_t> m_attackLeaderId[4];

private:
    friend class CSkillInfo;
    friend class CSkillAction2;
    bool m_AlwaysSkillSuccess;

public:
    double m_dMeleeMultiTargetBias;

private:
    ::CYieldLock m_UsedTriggerSkillsLock;
    ::std::map<int, int> m_UsedTriggerSkillLastTimeDB;

public:
    bool m_bAttackedByRange;
    bool m_bIgnoreWeaponRandomDamage;
    bool m_bIgnoreRegeneration;
    int m_ShowPosTarget;
    int m_ShowActionChangeTarget;
    bool m_AvoidAgroWhenEnterWorld;
    bool m_bShowMovePosToGM;
    FollowData m_FollowData;
    char16_t m_sNickName[25];
    bool m_IsSheathingWeapon;
    bool m_bDoubleCastingMod;
    std::atomic<std::int32_t> m_nLaunchedSkills;

protected:
    bool m_OnSummonStatBonus;

public:
    std::vector<int> m_UsersTargetMe;
    std::list<MPRewardData> m_MPRewardList;

private:
    ::std::set<int> m_PartyListSetttingTacticalSignToMe;

public:
    int nCountAttacked;
    double dDPSBonusForBuilder;
    std::atomic<std::int32_t> m_InitializingSkillMod;
    int m_nWeaponPAttackBonusByLevel;
    int m_nWeaponMAttackBonusByLevel;
    std::uint16_t m_hMinNewSetItemEnchantedEffect;
    std::uint16_t m_hMinNewSetItemEnchanted;
    double m_dExtraParam[2];
    std::uint32_t m_nDamageExceptAttackerId;
    int m_nLastSkillId;
    std::uint32_t m_nCanIgnoreDamageTargetId;
    int m_nChainAttackCount;
    CCreatureSkillMod::Mod m_DefenceFatalBlowSuccessBonus;
};
