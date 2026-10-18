#pragma once

#include "Protocol/src/L2Constants.h"

#include "Shared/src/BaseAttribute.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

enum SkillTime : std::int32_t {
    SKL_NOT_STARTED = 0,
    SKL_HITCANCELTIME = 1,
    SKL_CHANNELLING_CASTING = 2,
    SKL_CHANNELLING_TICK = 3,
    SKL_HITTIME = 4,
    SKL_COOLTIME = 5,
};

class CSkillKey {
public:
    CSkillKey(const CSkillKey&) = default;

    CSkillKey(std::uint16_t skillId, std::uint16_t level, std::uint16_t subLevel) :
        m_nSkillId(skillId), m_nLevel(level), m_nSubLevel(subLevel) {}

    CSkillKey(int skillId, int level, int subLevel) :
        m_nSkillId(skillId),
        m_nLevel(static_cast<std::uint16_t>(level)),
        m_nSubLevel(static_cast<std::uint16_t>(subLevel)) {}

    CSkillKey(int skillId, std::uint16_t level) : m_nSkillId(skillId), m_nLevel(level), m_nSubLevel(0) {}

    CSkillKey(int skillId, int level) : m_nSkillId(skillId), m_nLevel(static_cast<std::uint16_t>(level)), m_nSubLevel(0) {}

    CSkillKey() : m_nSkillId(0), m_nLevel(0), m_nSubLevel(0) {}

    CSkillKey& operator=(const CSkillKey& other) {
        if (this != &other) {
            m_nSkillId = other.m_nSkillId;
            m_nLevel = other.m_nLevel;
            m_nSubLevel = other.m_nSubLevel;
        }
        return *this;
    }

    bool operator==(const CSkillKey& other) const {
        return m_nSkillId == other.m_nSkillId && m_nLevel == other.m_nLevel && m_nSubLevel == other.m_nSubLevel;
    }

    bool operator!=(const CSkillKey& other) const { return !(*this == other); }

    bool IsValid() const { return m_nSkillId > 0; }

    int m_nSkillId;
    std::uint16_t m_nLevel;
    std::uint16_t m_nSubLevel;
};

inline bool operator<(const CSkillKey& left, const CSkillKey& right) {
    if (left.m_nSkillId != right.m_nSkillId) {
        return left.m_nSkillId < right.m_nSkillId;
    }
    if (left.m_nLevel != right.m_nLevel) {
        return left.m_nLevel < right.m_nLevel;
    }
    return left.m_nSubLevel < right.m_nSubLevel;
}


namespace ConsumeItemSearchType {

enum Type : std::int32_t {
    CIST_MY_INVEN = 0,
    CIST_MASTER_INVEN = 1,
    CIST_MAX = 2,
};

} // namespace ConsumeItemSearchType

enum SkillOperateType : std::int32_t {
    SKILL_INSTANT = 0,
    SKILL_DURATION = 1,
    SKILL_DURATION_LEVEL_UP = 2,
    SKILL_SELFDURATION = 3,
    SKILL_PASSIVE = 4,
    SKILL_TOGGLE = 5,
    SKILL_CHANNELLING_INSTANCE = 6,
    SKILL_CHANNELLING_DURATION = 7,
    SKILL_CHANNELLING_DURATION_LEVEL_UP = 8,
    SKILL_CHARGE_INSTANT = 9,
    SKILL_CHARGE_DURATION = 10,
    SKILL_AURA = 11,
    SKILL_AURA_ACTIVITY = 12,
    SKILL_GROUP_TOGGLE = 13,
    SKILL_BACK_STEP_INSTANT = 14,
    SKILL_BACK_STEP_DURATION = 15,
    SKILL_AURA_ACTIVITY2 = 16,
    SKILL_PARTY_INSTANT = 17,
    SKILL_DUAL_PUMP1 = 18,
    SKILL_RACE_SPECIAL_PASSIVE = 19,
    SKILL_BLINK_INSTANT = 20,
    SKILL_BACK_STEP_DURATION_WITH_TARGET_KNOCKBACK = 21,
    SKILL_MAX = 22,
};

enum SkillIsMagicType : std::int32_t {
    SIMT_PHYSICAL_SKILL = 0,
    SIMT_MAGIC_SKILL = 1,
    SIMT_ETC_SKILL = 2,
    SIMT_SONGDANCE_SKILL = 3,
    SIMT_TRIGGER_SKILL = 4,
    SIMT_SUMMON_SKILL = 5,
    SIMT_PHYSICAL_SKILL_NOCANCEL = 6,
    SIMT_MAGIC_SKILL_NOCANCEL = 7,
    SIMT_MAGIC_SKILL_MAX = 8,
    SIMT_PHYSICAL_ATTACK_SKILL = 10,
    SIMT_PHYSICAL_DEFENSE_SKILL = 11,
    SIMT_PHYSICAL_SHOOT_SKILL = 12,
    SIMT_PHYSICAL_ENCHANT_SKILL = 13,
    SIMT_MAGIC_ELEMENT_SKILL = 20,
    SIMT_MAGIC_DIMENSION_SKILL = 21,
    SIMT_MAGIC_ENCHANT_SKILL = 22,
    SIMT_MAGIC_HOLY_SKILL = 23,
    SIMT_MAGIC_DARKNESS_SKILL = 24,
    SIMT_MAGIC_VOODOO_SKILL = 25,
    SIMT_MAX = 26,
};

namespace CriticalType {

enum Enum : std::int32_t {
    CTY_NORMAL = 0,
    CTY_CRITICAL = 1,
    CTY_BOTH = 2,
};

} // namespace CriticalType

namespace BadSKillType {

enum Enum : std::int32_t {
    BST_ALL = 0,
    BST_GOOD = 1,
    BST_BAD = 2,
};

} // namespace BadSKillType


enum SkillFxChangeType : std::int32_t {
    SFCT_PER = 0,
    SFCT_DIFF = 1,
    SFCT_VEL = 2,
    SFCT_ABS = 3,
    SFCT_NONE = 4,
    SFCT_RESET = 5,
    SFCT_PER_DISTRICT = 6,
    SFCT_MAX = 7,
};

struct WeaponBonusType {
    WeaponBonusType(const bool* attackTypes, int bonus, SkillFxChangeType changeType) :
        m_arrAttackType{}, m_nBonus(bonus), m_ChangeType(changeType) {
        // Current L2Server 0x00ca11c8.
        for (int index = 0; index < AT_MAX; ++index) {
            m_arrAttackType[index] = attackTypes[index];
        }
    }

    bool m_arrAttackType[AT_MAX];
    int m_nBonus;
    SkillFxChangeType m_ChangeType;
};


enum SkillFxPositionType : std::int32_t {
    SFPT_FRONT = 0,
    SFPT_SIDE = 1,
    SFPT_BACK = 2,
    SFPT_LEFT = 3,
    SFPT_RIGHT = 4,
    SFPT_MAX = 5,
};

FAngle::Enum ConvertSkillFxPositionTypeToAngleDir(SkillFxPositionType positionType);

enum SkillPumpStartType : std::int32_t {
    SPST_ENTER_WORLD = 0,
    SPST_CASTING = 1,
    SPST_ACQUIRE_SKILL = 2,
    SPST_CHANNELING_START = 3,
    SPST_CHANNELING_END = 4,
    SPST_ETC = 5,
    SPST_SKILL_UNLOCK = 6,
    SPST_AURA = 7,
    SPST_DUEL = 8,
    SPST_TOGGLE = 9,
};

enum SkillPumpEndType : std::int32_t {
    SPET_NONE = 0,
    SPET_TIMEOUT = 1,
    SPET_DISPEL = 2,
    SPET_DIE = 3,
    SPET_CURSED = 4,
    SPET_REPLACE = 5,
    SPET_CHANNELING_END = 6,
    SPET_STEAL = 7,
    SPET_DELETE_SKILL = 8,
    SPET_DAMAGE = 9,
    SPET_RIDING_WYVERN = 10,
    SPET_ETC = 11,
    SPET_SELF_DISPEL = 12,
    SPET_ATTACK_OR_SKILL = 13,
    SPET_SKILL_LOCK = 14,
    SPET_AURA = 15,
    SPET_REDUCE = 16,
    SPET_TRANSFER = 17,
    SPET_TOGGLE = 18,
    SPET_TRIGGER = 19,
    SPET_ADMIN = 20,
    SPET_DUEL_END = 21,
    SPET_MAX = 22,
};

enum TraitTypeEnum : std::int32_t {
    TRAIT_INVALID = -1,
    TRAIT_START = 0,
    TRAIT_AT_SWORD = 1,
    TRAIT_AT_BLUNT = 2,
    TRAIT_AT_DAGGER = 3,
    TRAIT_AT_POLE = 4,
    TRAIT_AT_FIST = 5,
    TRAIT_AT_BOW = 6,
    TRAIT_AT_ETC = 7,
    TRAIT_GENERIC_START = 8,
    TRAIT_POISON = 9,
    TRAIT_HOLD = 10,
    TRAIT_BLEED = 11,
    TRAIT_SLEEP = 12,
    TRAIT_SHOCK = 13,
    TRAIT_DERANGEMENT = 14,
    TRAIT_BUG_WEAKNESS = 15,
    TRAIT_ANIMAL_WEAKNESS = 16,
    TRAIT_PLANT_WEAKNESS = 17,
    TRAIT_BEAST_WEAKNESS = 18,
    TRAIT_DRAGON_WEAKNESS = 19,
    TRAIT_PARALYZE = 20,
    TRAIT_DUAL = 21,
    TRAIT_DUALFIST = 22,
    TRAIT_BOSS = 23,
    TRAIT_GIANT_WEAKNESS = 24,
    TRAIT_CONSTRUCT_WEAKNESS = 25,
    TRAIT_DEATH = 26,
    TRAIT_VALAKAS = 27,
    TRAIT_ANESTHESIA = 28,
    TRAIT_CRITICAL_POISON = 29,
    TRAIT_ROOT_PHYSICALLY = 30,
    TRAIT_ROOT_MAGICALLY = 31,
    TRAIT_RAPIER = 32,
    TRAIT_CROSSBOW = 33,
    TRAIT_ANCIENTSWORD = 34,
    TRAIT_TURN_STONE = 35,
    TRAIT_GUST = 36,
    TRAIT_PHYSICAL_BLOCKADE = 37,
    TRAIT_TARGET = 38,
    TRAIT_PHYSICAL_WEAKNESS = 39,
    TRAIT_MAGICAL_WEAKNESS = 40,
    TRAIT_DUALDAGGER = 41,
    TRAIT_AIRBIND = 42,
    TRAIT_CHANGEBODY = 43,
    TRAIT_KNOCKBACK = 44,
    TRAIT_KNOCKDOWN = 45,
    TRAIT_PULL = 46,
    TRAIT_DEMONIC_WEAKNESS = 47,
    TRAIT_DIVINE_WEAKNESS = 48,
    TRAIT_ELEMENTAL_WEAKNESS = 49,
    TRAIT_FAIRY_WEAKNESS = 50,
    TRAIT_HUMAN_WEAKNESS = 51,
    TRAIT_HUMANOID_WEAKNESS = 52,
    TRAIT_UNDEAD_WEAKNESS = 53,
    TRAIT_TWOHANDCROSSBOW = 54,
    TRAIT_DUALBLUNT = 55,
    TRAIT_DEPORT = 56,
    TRAIT_DISARM = 57,
    TRAIT_BLUFF = 58,
    TRAIT_GRAVITY = 59,
    TRAIT_AIR_DEPORT = 60,
    TRAIT_PSYCHIC = 61,
    TRAIT_HATE = 62,
    TRAIT_WIND_STUN = 63,
    TRAIT_ZONE = 64,
    TRAIT_SILENCE = 65,
    TRAIT_MAX = 66,
};

enum SkillTargetTypeEnum : std::int32_t {
    STGT_SELF = 0,
    STGT_TARGET = 1,
    STGT_OTHERS = 2,
    STGT_MY_PARTY = 3,
    STGT_ENEMY = 4,
    STGT_ENEMY_ONLY = 5,
    STGT_ITEM = 6,
    STGT_SUMMON = 7,
    STGT_HOLYTHING = 8,
    STGT_MY_PLEDGE = 9,
    STGT_DOOR_TREASURE = 10,
    STGT_PC_DEAD_BODY = 11,
    STGT_NPC_DEAD_BODY = 12,
    STGT_WYVERN_TARGET = 13,
    STGT_GROUND = 14,
    STGT_ARTILLERY = 15,
    STGT_FORTRESS_FLAGPOLE = 16,
    STGT_SHIP_CANNON = 17,
    STGT_ADVANCE_BASE = 18,
    STGT_ENEMY_NOT = 19,
    STGT_EXCEPTION_SELF = 20,
    STGT_MY_MENTOR = 21,
    STGT_MAX = 22,
};

enum RangeTargetType : std::int32_t {
    RTT_WHITE = 0,
    RTT_VIOLET = 1,
    RTT_CHAO = 2,
    RTT_PEACE_ZONE = 3,
    RTT_MAX = 4,
};

enum SkillAffectScopeEnum : std::int32_t {
    SAFS_SINGLE = 0,
    SAFS_PARTY = 1,
    SAFS_RANGE = 2,
    SAFS_POINT_BLANK = 3,
    SAFS_PET = 4,
    SAFS_FAN = 5,
    SAFS_PLEDGE = 6,
    SAFS_SQUARE = 7,
    SAFS_FAN_PB = 8,
    SAFS_SQUARE_PB = 9,
    SAFS_DEAD_PLEDGE = 10,
    SAFS_WYVERN_SCOPE = 11,
    SAFS_BALAKAS_SCOPE = 12,
    SAFS_CHARGE = 13,
    SAFS_STATIC_OBJECT = 14,
    SAFS_RING_RANGE = 15,
    SAFS_PARTY_PLEDGE = 16,
    SAFS_UNION = 17,
    SAFS_DEAD_UNION = 18,
    SAFS_RANGE_SORT_BY_HP = 19,
    SAFS_ALL_SUMMON_AND_MASTER = 20,
    SAFS_ALL_SUMMON_EXCEPT_MASTER = 21,
    SAFS_DEAD_PARTY = 22,
    SAFS_DEAD_PARTY_PLEDGE = 23,
    SAFS_RANGE_SORT_BY_DIST = 24,
    SAFS_MAX = 25,
};

enum SkillReuseDelayType : std::int32_t {
    SRDT_NORMAL = 0,
    SRDT_SUPER = 1,
};

namespace DamageProportionType {

enum Enum : std::int32_t {
    DPT_NOT_APPLIED = 0,
    DPT_DIRECT = 1,
    DPT_INVERSE = 2,
};

} // namespace DamageProportionType

enum SkillDirectionType : std::int32_t {
    DT_FRONT = 0,
    DT_BACK = 1,
};

// Current L2Server item-chance consumers distinguish the absolute case from
// the relative case at +0x00 and consume 0x28-byte records. The matching
// TotalChecker type stream supplies the original enum and member spellings.
enum ItemChanceLevelType : std::int32_t {
    ICL_ABSOLUTE_LEVEL = 0,
    ICL_RELATIVE_LEVEL = 1,
};

struct ItemChanceType {
    ItemChanceType(
        ItemChanceLevelType levelType, int minLevel, int maxLevel, double corpseProb, double multiProb, double exProb
    ) :
        m_LevelType(levelType),
        m_nMinLevel(minLevel),
        m_nMaxLevel(maxLevel),
        m_dCorpseProb(corpseProb),
        m_dMultiProb(multiProb),
        m_dExProb(exProb) {}

    ItemChanceType();

    ItemChanceLevelType m_LevelType;
    int m_nMinLevel;
    int m_nMaxLevel;
    double m_dCorpseProb;
    double m_dMultiProb;
    double m_dExProb;
};


struct SkillUseInfo {
    SkillUseInfo(int, int, int, int, int, int);
    SkillUseInfo(int, int, int, int, int);
    SkillUseInfo() : m_skillID(0), m_skillLevel(0), m_skillSubLevel(0), m_castRange(0), m_type(0), m_param1(0) {}

    int m_skillID;
    int m_skillLevel;
    int m_skillSubLevel;
    int m_castRange;
    int m_type;
    int m_param1;
};


class CCreature;
class CObject;
class CParserForSkill;
class CSkillAction2;
class CSkillConsumeEffect;
class CSkillInfo;
class CSkillOperateCondition;
class CSkillTickEffect;
class CWorldObject;
class User;

struct AlterSkillInfo {
    CSkillInfo* pOriSkillInfo;
    CSkillInfo* pSkillInfo;
    std::u16string skillName;
    double prob;
};


AbnormalStatusSlotType GetAbnormalStatusSlotTypeByName(char16_t* slotName);
void GetAbnormalStatusSlotTypeByName(const std::vector<char16_t*>& words, std::vector<int>& abnormalTypes);

struct TriggerSynergySkillInfo {
    TriggerSynergySkillInfo();

    std::set<AbnormalStatusSlotType> m_RequiredSlotType;
    std::set<AbnormalStatusSlotType> m_SynergyOptionSlotType;
    std::map<int, const CSkillInfo*> m_mapSynergySkillByCount;
};


class CEffectCondition {
public:
    CEffectCondition();

    virtual bool CheckEffectCondition(CCreature* creature);
    virtual ~CEffectCondition();
};

class CSkillEffect {
public:
    using vEffectCondList = std::vector<CEffectCondition*>;

    CSkillEffect();

    virtual void CalcSuccess(CCreature* creature, CObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction);
    virtual void CalcSuccess(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction
    );
    virtual bool InstantByArea(CObject* target, const CSkillInfo* skillInfo);
    virtual bool InstantByArea(CWorldObject* target, const CSkillInfo* skillInfo);
    virtual bool Instant(
        CCreature* creature, CObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    );
    virtual bool Instant(
        CCreature* creature, CWorldObject* target, const CSkillInfo* skillInfo, CSkillAction2* skillAction, double distance
    );
    virtual bool Tick(CObject* target, double amount, unsigned tick, const CSkillInfo* skillInfo);
    virtual bool Tick(CWorldObject* target, double amount, unsigned tick, const CSkillInfo* skillInfo);
    virtual double Amount();
    virtual bool CheckPumpCondition(CCreature* creature);
    virtual bool Pump(CCreature* creature, const CSkillInfo* skillInfo, int unknown);
    virtual bool PumpStart(
        CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType type
    ) const;
    virtual bool PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpEndType type) const;
    virtual bool Consume(CObject* target, double amount);
    virtual bool IsTickEffect() const;
    virtual bool IsConsumeEffect() const;
    virtual CSkillTickEffect* CastTickEffect();
    virtual CSkillConsumeEffect* CastConsumeEffect();
    virtual void OnPostLoad();
    virtual ~CSkillEffect();
    virtual bool IsConsumeAgathionEnergyEffect() const;

    int nUnitTick;
    EffectSuccessType eEffectType;
    vEffectCondList effectCondList;
};

// Current Skill.cpp startup carriers. TotalChecker keeps the source
// names. Current map-node strides fix the shipping xwstring/xvector
// extents. Portable members use standard containers. Offsets below are historical.
struct CSkillInfoTempData {
    std::u16string m_sAttachedSkillName; // +0x00
    std::u16string m_sConsumeItemName; // +0x28
};

struct CSkillInfoVerifyData {
    using VerifySkillEffects = std::vector<CSkillEffect*>;

    VerifySkillEffects m_vEffectList; // +0x00
};


class CSkillTickEffect : public CSkillEffect {
public:
    CSkillTickEffect(double amount, SkillFxChangeType changeType);

    bool IsTickEffect() const override;
    CSkillTickEffect* CastTickEffect() override;
    double Amount() override;
    virtual int GetTickEffectType() = 0;

protected:
    const double dAmount;
    const SkillFxChangeType m_Sfct;
    // MSVC does not reuse the four alignment bytes following m_Sfct for
    // derived members. Making the current physical lane explicit keeps
    // every SkillFxFunc_tick derived field at its observed +0x40 offset on
    // the portable Itanium ABI as well.
    std::uint32_t m_TargetAlignmentPadding;
};

class CSkillConsumeEffect : public CSkillEffect {
public:
    CSkillConsumeEffect(double amount);

    bool IsConsumeEffect() const override;
    CSkillConsumeEffect* CastConsumeEffect() override;
    double Amount() override;

protected:
    double dAmount;
};


enum DebuffType : std::int32_t;
enum SetL2ParamEnum : std::int32_t;
enum SkillAffectObjectTypeEnum : std::int32_t;

struct TriggerAlterSkillInfo {
    AbnormalStatusSlotType abnormalType = ABNS_INVALID;
    double prob = 0.0;
    std::vector<AlterSkillInfo*> skillList;
    std::int32_t oriSkillID = 0;
    std::int32_t expiredSec = 0;
};


class CSkillInfo {
public:
    enum EffectActivateTiming : std::int32_t {
        EAT_INVALID = -1,
        EAT_SUCCESS = 0,
        EAT_START = 1,
        EAT_CANCEL = 2,
        EAT_TICK = 3,
        EAT_END = 4,
        EAT_SELF = 5,
        EAT_PVP = 6,
        EAT_PVE = 7,
        EAT_MAX = 8,
    };

    enum OperateConditionTarget : std::int32_t {
        OCT_INVALID = -1,
        OCT_CASTER = 0,
        OCT_TARGET = 1,
        OCT_PASSIVE = 2,
        OCT_BOTH = 3,
    };

    using SkillConditions = std::vector<CSkillOperateCondition*>;
    using SkillEffects = std::vector<CSkillEffect*>;
    struct PumpSkillData {
        CSkillEffect* m_pSkillEffect;
        CSkillInfo* m_pSkillInfo;
        std::int32_t m_SourceId;
        std::int32_t m_TargetAlignmentPadding;
    };
    using ToggleAttachMap = std::map<int, int>;
    using AbnormalAlterSkillMap = std::map<AbnormalStatusSlotType, std::vector<AlterSkillInfo*>>;

    CSkillInfo();
    CSkillInfo(const CSkillInfo& other);
    virtual ~CSkillInfo();

    void Reset();
    void PostLoad();
    int IsMagic() const;
    bool IsDurationSkill() const;
    void SetAvoidCanceling(bool enabled) {
        std::memset(m_bAvoidCancelingSkill, enabled ? 1 : 0, sizeof(m_bAvoidCancelingSkill));
    }
    void SetIsAgathionMainSkill(bool enabled) { m_bIsAgathionMainSkill = enabled; }
    CSkillKey GetKey() const { return CSkillKey(m_nSkillId, m_nLevel, m_nSubLevel); }
    void CheckAttachedDoubleCast();
    void CheckGroupSkillInfo();
    void CheckChangeSkillInfo();

    const SkillEffects* GetEffects(EffectActivateTiming timing) const;
    const SkillEffects* GetPumpEffects(EffectActivateTiming timing) const;
    void CollectPumpEffects(std::vector<PumpSkillData>* pumpSkillData);
    bool CanUsedBy(CCreature* creature, bool infectedSkill) const;
    bool PumpStart(CCreature& creature, CCreature* source, SkillPumpStartType startType) const;
    bool PumpEnd(CCreature& creature, CCreature* source, SkillPumpEndType endType) const;
    bool IsCursedRelation(CCreature& creature, CObject& target) const;
    bool IsValidTarget(CCreature* creature, CWorldObject* target, bool force) const;
    bool CanActivateSkill(CCreature* creature, CObject* target) const;
    bool CheckActivateSkillEffects(CCreature* creature, CObject* target, EffectActivateTiming timing) const;
    void PostActivateSkillEffects(CCreature* creature, CWorldObject* target, EffectActivateTiming timing) const;
    void SetAvoidSkillCanceling(CCreature& creature, bool enabled) const;
    void ActivateSkillEffects(
        CCreature* creature, CWorldObject* target, EffectActivateTiming timing, CSkillAction2* action, double distance
    ) const;
    double GetMesAdjustProb(CCreature* creature) const;
    double GetDurationSkillProb(CCreature* creature, CCreature* target) const;
    static AlterSkillInfo* GetAlterSkillByProb(std::vector<AlterSkillInfo*>& skillList);
    bool ActivateDurationSkill(CCreature* creature, CObject* target, bool* reflected) const;
    void SkillAvoidOrCounter(CCreature& creature, CObject& target, bool& avoided, bool& countered) const;
    bool ActivateSkill(
        CCreature* creature,
        CWorldObject* target,
        double distance,
        CSkillAction2* action,
        EffectActivateTiming timing,
        bool durationRequired
    ) const;
    bool CubicActivate(CCreature* creature, CWorldObject* target, double power);
    bool ActivateAuraSkill(CCreature& creature, SkillTargetTypeEnum targetType) const;
    bool ActivateTriggerSkill(CCreature& creature, CWorldObject* target, SkillTargetTypeEnum targetType) const;
    void CheckAndTurnOffSoulSpiritShot(CCreature& creature) const;
    int CheckOperateCond(CCreature* creature, CWorldObject* target, OperateConditionTarget conditionTarget) const;
    int GetNormalReuseDelay(CCreature* creature) const;
    void SetEnchantAttributeType(AttributeTypeEnum attributeType);
    bool GetEnchantAttributeType(AttributeTypeEnum attributeType);
    void SetRideState(std::vector<std::int64_t>* rideStates);
    bool HasAlterSkills() const;
    void SetTransformType(std::vector<int>& transformTypes);
    int GetAttachSkillId(int skillId) const;
    bool PushConditionMsg(int messageId, OperateConditionTarget target);
    void PushRelatedItem(int itemId);
    bool PushPumpEffect(CSkillEffect* effect, EffectActivateTiming timing);
    bool PushEffect(CSkillEffect* effect, EffectActivateTiming timing);
    bool PushCondition(CSkillOperateCondition* condition, OperateConditionTarget target);
    bool IsActiveSkill() const;
    double GetConsumeFactor(CCreature* creature);
    void ShowSkillInfo(User& user);
    void SendRegenPacket(User* user, int remainingTime) const;
    double GetDamageProportionBonus(CCreature& creature) const;

    wchar_t* m_pszName;
    std::int32_t m_nSkillId;
    std::int32_t m_nLevel;
    std::int32_t m_nSubLevel;
    std::int32_t m_nMagicLevel;
    std::int32_t m_nIsMagic;
    std::int32_t m_nIsHate;
    std::int32_t m_nSpecialLevel;
    bool m_bIsEnergyAttack;
    SkillOperateType m_OperateType;
    std::int32_t m_nReuseDelayGroup;
    std::int32_t m_nNpcNotice;
    std::int32_t m_nMpConsume1;
    std::int32_t m_nMpConsume2;
    std::int32_t m_nMpConsumeTick;
    std::int32_t m_nHpConsumeTick;
    std::int32_t m_nHpConsume;
    std::u16string m_sConsumeItemName;
    std::int32_t m_nConsumeItem;
    std::int64_t m_nConsumeItemCount;
    std::int32_t m_nPledgeNameValueConsume;
    std::int32_t m_nPVPPointConsume;
    ConsumeItemSearchType::Type m_eConsumeItemSearchType;
    std::int32_t m_nCastRange;
    std::int32_t m_nEffectiveRange;
    DamageRangeType m_DamageRange;
    double m_dSkillHitTime;
    double m_dSkillCoolTime;
    double m_dSkillHitCancelTime;
    double m_nReuseDelay;
    bool m_bReuseDelayLock;
    bool m_bBlockActionUseSkill;
    std::int32_t m_nOlympiadUse;
    std::int32_t m_nPledgeGameUse;
    std::int32_t m_nReduceCount;
    double m_dReduceDamage;
    std::int32_t m_nTransferAbnormal;
    SkillConditions m_vOperateCondForCaster;
    SkillConditions m_vOperateCondForTarget;
    SkillConditions m_vPassiveConditions;
    std::vector<int> m_vOperateCondMsgForCaster;
    std::vector<int> m_vOperateCondMsgForTarget;
    std::vector<int> m_vOperateCondMsgForPassiveConditions;
    bool m_bCondIncludeDead;
    double m_dLevelBonusRate;
    std::int32_t m_nActivateRate;
    std::int32_t m_nPVPActivateRate;
    std::int32_t m_nPVEActivateRate;
    SetL2ParamEnum m_BasicProperty;
    std::int32_t m_nAbnormalTime;
    std::int32_t m_nPVPAbnormalTime;
    std::int32_t m_nPVEAbnormalTime;
    std::int32_t m_nAbnormalLv;
    bool m_bAbnormalTimeCritical;
    AbnormalStatusSlotType m_nAbnormalType;
    std::set<int> m_AbnormalVisualEffect;
    bool m_bAvoidCancelingSkill[ABNS_MAX];
    CSkillInfo* m_pAttachedSkill;
    AttributeTypeEnum m_eAttribute;
    std::int32_t m_AttributePoint;
    TraitTypeEnum m_TraitType;
    std::int32_t m_nEffectPoint;
    bool m_IsSummonSkill;
    bool m_IsSummonSiegeSkill;
    DebuffType m_eDebuff;
    bool m_IrreplaceableBuff;
    ActionType m_NextAction;
    bool m_bIsFakeDeath;
    std::int32_t m_bfRideState;
    bool m_bDeleteBuff;
    CSkillEffect* m_pConsumeEffect;
    SkillEffects m_TickEffects;
    SkillEffects m_vPumpEffect[EAT_MAX];
    SkillEffects m_vInstantEffects[EAT_MAX];
    SkillEffects m_vAuraEffect;
    std::vector<int> m_vRelatedItems;
    bool m_bAttrEnchantType[ATTRT_MAX];
    std::int32_t m_nAttrEnchantLevel;
    bool m_bPetFoodSkill;
    std::int32_t m_nAfterEffectType;
    double m_dTickInterval;
    double m_dChannelingStart;
    SkillTargetTypeEnum m_TargetType;
    SkillAffectScopeEnum m_AffectScope;
    SkillAffectObjectTypeEnum m_AffectObjectType;
    std::int32_t m_nAffectScopeMinZ;
    std::int32_t m_nAffectScopeMaxZ;
    std::int32_t m_nAffectRange;
    std::int32_t m_nAffectLimitBase;
    std::int32_t m_nAffectLimitRandom;
    bool m_AffectLimitToPC;
    std::int32_t m_nCapsuleReduceItemID;
    std::int32_t m_nMultiClass;
    std::int32_t m_nEnergy;
    std::int32_t m_nSoul;
    std::int32_t m_nBuffProtectLevel;
    bool m_bInstantItem;
    SkillReuseDelayType m_ReuseDelayType;
    CSkillInfo* m_pPrevLevel;
    CSkillInfo* m_pNextLevel;
    std::int32_t m_nToggleGroupId;
    std::int32_t m_nAttachToggleGroupId;
    std::int32_t m_nAttachDoubleCastSkillId;
    bool m_bIsDoubleCasting;
    bool m_IsFlySkill;
    bool m_isClassChangeSkill;
    bool m_bIsNeccessaryTG;
    bool m_bHairAccFlag;
    bool m_bExceptBlock;
    bool m_bExceptRestore;
    std::int32_t m_nChangeBuffId;
    ToggleAttachMap m_mapAttachSkill;
    double m_MagicCriticalRate;
    AbnormalStatusSlotType m_nSubAbnormal;
    std::int32_t m_nEventType;
    bool m_bWithoutAction;
    bool m_bUndeletable;
    std::int32_t m_nTransformType;
    DamageProportionType::Enum m_DamageProportionType;
    AbnormalAlterSkillMap m_AlterSkillList;
    TriggerAlterSkillInfo m_TriggerAlterSkillInfo;
    bool m_IsOriginAlterSkill;
    bool m_IsAlterSkill;
    std::int32_t m_UltimateSkillLv;
    bool m_bHasAwakenPenalty;
    std::int32_t m_nAwakenPenaltyLevelBegin;
    std::int32_t m_nAwakenPenaltyFactorOnMP;
    SkillFxPositionType m_eChargeDirection;
    bool m_bSaveToDB;
    bool m_bIsVariationItemEquipReusedelay;
    bool m_bIsAgathionMainSkill;
};


class CSkillDB {
public:
    CSkillDB();
    ~CSkillDB();

    int GetNormalSkillMaxLevel(int skillId);
    int GetSkillIdLevelByName(const wchar_t* name, int* skillId, int* level, int* subLevel);
    CSkillKey GetSkillKeyByName(wchar_t* name);
    void Add(CSkillInfo* skillInfo);

    const CSkillInfo* GetSkillInfo(CSkillKey skillKey, CCreature* creature);
    const CSkillInfo* GetSkillInfoByName(const wchar_t* name);
    bool Load();
    bool LoadSkillData(char16_t* data, int dataLength, bool reload);
    bool LoadSkillSynergy();
    bool LoadSkillSynergyData(char16_t* data, int dataLength, bool reload);
    void PostLoad();

private:
    friend class CParserForSkill;

    std::map<CSkillKey, CSkillInfo*> m_SkillMap; // +0x00
    std::map<std::u16string, CSkillInfo*,
        runtime::CStringLess<std::u16string>> m_SkillNameMap; // +0x20
    std::map<int, int> m_NormalSkillMaxLevelMap; // +0x40
    bool m_bNameLookupMiss; // +0x60
    bool m_bKeyLookupMiss; // +0x61
    void* m_pReserved68; // +0x68
    void* m_pReserved70; // +0x70
    void* m_pReserved78; // +0x78
    bool m_bOnlyPrecompiledHeader; // +0x80
    CSkillInfo* m_pPreviousSkillInfo; // +0x88
};


extern CSkillDB g_SkillDB;
extern CSkillDB g_OptionSkillDB;
extern std::map<CSkillKey, CSkillInfoTempData> g_SkillInfoTempDataDB;
extern std::map<CSkillKey, CSkillInfoVerifyData> g_SkillInfoVerifyDataDB;
// Current Skill.cpp owner at 20c82de0. Setting parser action 125 inserts
// packed int64 skill keys into this process-lifetime set-like map.
extern std::map<std::int64_t, bool> g_ItemSkillTransfer;
extern std::map<int, std::vector<int>> m_mapToggleGroup;
extern std::map<int, TriggerSynergySkillInfo*> m_mapSynergySkill;
