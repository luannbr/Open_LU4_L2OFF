#pragma once

#include "Object.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"
#include "Skill.h"

#include <cstdint>

enum ActionType : std::int32_t {
    ACT_NOTHING = 0,
    ACT_STAND = 1,
    ACT_SIT = 2,
    ACT_MOVE = 3,
    ACT_ATTACK = 4,
    ACT_SKILL = 5,
    ACT_TALK = 6,
    ACT_GENERAL = 7,
    ACT_GETITEM = 8,
    ACT_OPENCLOSE = 9,
    ACT_MOVE_TO_TARGET = 10,
    ACT_MOVE_TO_TARGET_WAIT = 11,
    ACT_MOVE_RELATIVE = 12,
    ACT_ATTACK_WAIT = 13,
    ACT_PRIVATE_STORE = 14,
    ACT_MOVE_TO_TARGET_RELATIVE = 15,
    ACT_COUPLE = 16,
    ACT_MAX = 17,
};

extern int g_ActionTable[18][18];

namespace ActionState {

enum Enum : std::int32_t {
    Start = 0,
    Tick = 1,
    End = 2,
};

} // namespace ActionState

class CCreatureController;
class CMoveToTargetAction;
class CSharedItemData;
class CSkillEffect_i_fatal_blow;
class CSkillEffect_i_block_act;
enum CrystalType : std::int32_t;
enum SlotType : std::int32_t;

// Current NewAction.cpp owner: process-wide positions of active Christmas
// trees. The public signatures pass FVector by value, matching
// the original x64 ABI and the surviving current signatures.
class CTreeLoc {
public:
    static ::CYieldLock m_lock;
    static ::std::map<int, FVector> m_treelocs;

    static void Init();
    static bool UseTree(int objectId, FVector position, int inZoneId);
    static void DeleteTree(int objectId);
    static bool CanUseTree(FVector position, int inZoneId);
    static bool _NearTree(FVector position, int inZoneId);
};

enum AttackState : std::int32_t {
    ATK_HITTIME = 0,
    ATK_SECONDHITTIME = 1,
    ATK_COOLTIME = 2,
    ATK_WAITINGTIME = 3,
};

struct AttackTargetDataType {
    enum ShieldDefenseType : std::int32_t {
        SDT_NONE = 0,
        SDT_NORMAL = 1,
        SDT_PERFECT = 2,
    };

    AttackTargetDataType() :
        dDamage(0.0), nTargetId(0), eShieldDefense(SDT_NONE), nIsSoulShot(0), bIsHit(false), bIsCritical(false) {}

    void SetNoDamage(const bool isHit) {
        dDamage = 0.0;
        eShieldDefense = SDT_NONE;
        nIsSoulShot = 0;
        bIsHit = isHit;
        bIsCritical = false;
    }

    double dDamage;
    int nTargetId;
    ShieldDefenseType eShieldDefense;
    int nIsSoulShot;
    bool bIsHit;
    bool bIsCritical;
};


class CNewAction {
public:
    CNewAction();
    virtual ~CNewAction();

    virtual ActionType GetType() const = 0;

    void SetCreatureController(CCreatureController* controller) { m_pController = controller; }

    ActionState::Enum m_ActionState;
    CCreatureController* m_pController;

protected:
    static bool CheckAttackValidityInOlympia(CCreature* creature, CObject* target);
    static bool CheckAttackValidityInPledgeGame(CCreature* creature, CObject* target);

    virtual bool IsTarget(CObject*) { return false; }

    virtual bool OnStart() {
        m_ActionState = ActionState::Start;
        return true;
    }

    virtual bool OnTick() {
        m_ActionState = ActionState::Tick;
        return false;
    }

    virtual bool OnLostTarget(int, bool) = 0;
    virtual bool OnDisappear() = 0;
    virtual bool OnStopMove() = 0;
    virtual bool OnDamaged(CSmartID, int) = 0;

    virtual void OnEnd(bool) { m_ActionState = ActionState::End; }

    virtual bool OnTargetOutOfSight(int) = 0;
    virtual void AddTimer(int delayMilliseconds, int actionNumber, int actionSequence);
    virtual bool CondForHiddenTarget(CWorldObject* target);

    void SetInterruptActionType(const ActionType actionType) { m_eInterruptActionType = actionType; }

    ActionType GetInterruptActionType() const { return m_eInterruptActionType; }

    void ResetInterruptActionType() { m_eInterruptActionType = ACT_MAX; }

    CSmartID m_spActor;
    ActionType m_eInterruptActionType;

    friend class CCreatureController;
};

class CNothingAction : public CNewAction {
public:
    ActionType GetType() const override { return ACT_NOTHING; }

private:
    void Set(CSmartID actor);
    bool OnStart() override;

    bool OnLostTarget(int, bool) override { return true; }

    bool OnDisappear() override { return true; }

    bool OnStopMove() override { return true; }

    bool OnDamaged(CSmartID, int) override;

    bool OnTargetOutOfSight(int) override { return true; }

    friend class CCreatureController;
};

class CStandAction : public CNewAction {
public:
    ActionType GetType() const override { return ACT_STAND; }

protected:
    void Set(CSmartID actor, int delayTime);
    bool OnStart() override;

    bool OnLostTarget(int, bool) override { return true; }

    bool OnDisappear() override { return true; }

    bool OnStopMove() override { return true; }

    bool OnDamaged(CSmartID, int) override { return true; }

    bool OnTargetOutOfSight(int) override { return true; }

    int m_nDelayTime;

    friend class CCreatureController;
};

class CSitAction : public CNewAction {
public:
    ActionType GetType() const override { return ACT_SIT; }

    int GetSitType() const { return m_SitType; }

protected:
    void Set(CSmartID actor, int delayTime, CSmartID target, int sitType);
    bool OnStart() override;

    bool OnLostTarget(int, bool) override { return true; }

    bool OnDisappear() override { return true; }

    bool OnStopMove() override { return true; }

    bool OnDamaged(CSmartID, int) override { return true; }

    bool OnTargetOutOfSight(int) override { return true; }

    CSmartID m_spTarget;
    int m_SitType;
    int m_nDelayTime;

    friend class CCreatureController;
};

class CAttackAction : public CNewAction {
    friend class CMoveToTargetAction;

public:
    struct TimeData {
        TimeData() : m_nHitTime(0), m_nSecondHitTime(0), m_nCoolTime(0) {}

        unsigned int GetAllTime() const { return m_nHitTime + m_nSecondHitTime + m_nCoolTime; }

        unsigned int m_nHitTime;
        unsigned int m_nSecondHitTime;
        unsigned int m_nCoolTime;
    };

    ActionType GetType() const override { return ACT_ATTACK; }

    unsigned int GetCoolTime() const;
    unsigned int GetHitTime() const;
    unsigned int GetSecondHitTime() const;
    void SetAttackType(AttackType attackType);
    void SetSlotType(SlotType slotType);

    void Set(CSmartID actor, CSmartID target, bool surely);
    bool IsTarget(CObject* target) override { return target->GetSmartID().Sid() == m_spTarget.Sid(); }

    bool OnStart() override;
    bool OnTick() override;

    bool OnLostTarget(int, bool) override { return true; }

    bool OnTargetOutOfSight(int) override { return true; }

    bool OnDisappear() override { return true; }

    bool OnStopMove() override { return true; }

    bool OnDamaged(CSmartID, int) override { return true; }

    void OnEnd(bool success) override;

    int GetMultiTargetCount() const;
    const AttackTargetDataType& GetTargetData() const;
    static void GetTimeData(CCreature& creature, TimeData& timeData);
    static void GenerateAttackTargetData(
        CCreature* creature,
        CWorldObject* target,
        AttackTargetDataType& targetData,
        bool& soulShot,
        double damageFactor,
        bool surelyCritical
    );

    static double CalcCriticalHeightBonus(const FVector& source, const FVector& target);
    static void SetPositionBonus(
        CCreature* creature,
        CObject* target,
        double& positionBonusRate,
        double& criticalDamagePositionBonusPer,
        double& criticalDamagePositionBonusDiff
    );
    static double CalcFatalBlowSuccessProb(double probability, CCreature* attacker, CCreature* target, double bonus);

private:
    static double CalcCriticalLocBonus(
        CCreature* creature, const FVector& source, const FVector& target, const FAngle& direction
    );

    static bool CheckAttackable(
        double attackRange,
        double sourceCollisionRadius,
        double targetCollisionRadius,
        double squaredPlanarDistance,
        double sourceCollisionHeight,
        double targetCollisionHeight,
        int sourceZ,
        int targetZ,
        AttackType attackType
    );

    static double GetAttackCriticalProb(
        CCreature* creature,
        int targetLevel,
        double criticalProbability,
        const FVector& source,
        const FVector& target,
        const FAngle& direction,
        bool applyLevelDifference
    );
    static double GetHitRatio(
        bool flag,
        double physicalHit,
        double physicalAvoid,
        const FVector& source,
        const FVector& target,
        const FAngle& direction,
        CCreature* creature
    );
    static bool CheckIsHit(
        bool flag,
        double physicalHit,
        double physicalAvoid,
        const FVector& source,
        const FVector& target,
        const FAngle& direction,
        CCreature* creature
    );
    static double GetHitTimeFactor(CCreature& creature);
    static double CalcHitLocBonus(
        const FVector& source, const FVector& target, const FAngle& direction, CCreature* creature
    );
    static double CalcHitHeightBonus(const FVector& source, const FVector& target);
    bool IsTargetInSameInZone();
    bool CheckBowCondition(CCreature* creature, int consumeMP, int reuseDelay, bool surely, CSharedItemData* weaponData);
    static void SetDefencePositionBonus(
        CCreature* creature, CObject* target, double& defencePositionBonusPer, double& defencePositionBonusDiff
    );
    void ConvertSkillTargetToMultiTarget(CCreature* creature);
    void CheckItemSkill(int targetId, bool critical);
    void OnHitTarget(int targetId, bool critical, bool hit);
    void OnHitTimeNotDual();
    void OnFirstHitTimeForDual();
    void OnSecondHitTimeForDual();
    static bool CheckAttackValidity(CCreature* creature, CObject* target);

    CSmartID m_spTarget;
    bool m_bSurely;
    AttackState m_CurrentAction;
    TimeData m_Time;
    DamageRangeType m_DamageRange;
    int m_WeaponClassID;
    AttackType m_eAttackType;
    SlotType m_eSlotType;
    CrystalType m_eWeaponCrystal;
    double m_dSoulShotPerBonus;
    bool m_bIsSoulShot;
    AttackTargetDataType m_TargetData;
    int m_nMultiCount;

public:
    AttackTargetDataType m_MultiTargetData[20];

private:
    friend class CCreatureController;
};


class CSkillAction2 : public CNewAction {
public:
    enum SpenItemMement : std::int32_t {
        BEFORE_REUSE_DELAY = 0,
        AFTER_REUSE_DELAY = 1,
        AFTER_INSTANT_EFFECT = 2,
    };

    CSkillAction2();

    ActionType GetType() const override { return ACT_SKILL; }

private:
    // Current Skill.cpp writes m_Infected directly from
    // CSkillInfo::ActivateSkill@0x00a69724.
    friend class CSkillInfo;
    // Current SkillFxFunc_instant.cpp reads the infection result while
    // deciding whether to send the shield-defence notification.
    friend class CSkillEffect_i_fatal_blow;
    // Current i_backstab@0x00a95d7c and
    // i_physical_attack_hp_link@0x00a96ea4 use the same direct infection
    // result when selecting the shield-defence notification path.
    friend class CSkillEffect_i_backstab;
    friend class CSkillEffect_i_physical_attack_hp_link;
    // Current SkillFxFunc_instant.cpp reads the infection result before
    // applying i_block_act.
    friend class CSkillEffect_i_block_act;
    // Current SkillFxFunc_instant.cpp reads the infection result while
    // deciding whether a duration skill may apply i_knockback.
    friend class CSkillEffect_i_knockback;
    // Current SkillFxFunc_instant.cpp reads the infection result while
    // deciding whether i_energy_attack reports shield defence.
    friend class CSkillEffect_i_energy_attack;
    // Current SkillFxFunc_instant.cpp reads the infection result before
    // applying the duration variants of i_fly_away.
    friend class CSkillEffect_i_fly_away;
    // Current SkillFxFunc_instant.cpp reads the infection result before
    // applying the duration variants of i_pull.
    friend class CSkillEffect_i_pull;
    // Current CreatureController.cpp consumes the target/action state directly
    // for the skill-use and skill-cancel broadcasts.
    friend class CCreatureController;

    static CSkillInfo* GetSkillInfo(
        CCreature* creature,
        int skillId,
        int& skillLevel,
        int& skillSubLevel,
        bool& activeSkill,
        bool& toggleSkill,
        bool& auraSkill,
        bool& databaseSkill
    );
    bool ResolveSkillForSet(CCreature* creature, int& skillId, int& skillLevel, int& skillSubLevel);

public:
    static int CalcPaybackMP(CCreature& creature, CSkillInfo& skillInfo, double consumedMP);
    static double ApplyConsumeMPAwakenPenalty(int consumeMP, CCreature* creature, CSkillInfo* skillInfo);
    static int CalcConsumeMP(CCreature* creature, CSkillInfo* skillInfo, int baseConsumeMP, double consumeFactor);

private:
    static bool CheckConsumePoint(CCreature* creature, CSkillInfo* skillInfo);
    static bool CheckAndConsumeAgathionEnergy(CCreature* creature, CSkillInfo* skillInfo, double consumeFactor);
    static bool CheckAndConsumeHPMP(
        CCreature* creature, CSkillInfo* skillInfo, double consumeFactor, bool reportFailure, bool consumeFirstPhase
    );
    static bool CheckEnergySoul(CCreature* creature, CSkillInfo* skillInfo, bool consume);
    bool CheckCondition(CCreature* creature);
    bool CanUseBadSkill(CCreature* creature);
    bool IsChangeClassSkill() const;
    void ActivateAttachedItemSkills(CCreature& creature, CWorldObject& target, CSkillInfo::EffectActivateTiming timing);
    bool CheckValidActionState(CCreature& creature);
    bool SetValidSkillPendingAction(CCreature* creature, bool allowSit, bool allowFakeDeath);
    bool CheckAndConsumeItemDuration(CCreature* creature);
    bool CheckAndSetReuseDelay(CCreature* creature, int& reuseDelay);
    bool CheckConditionsOnStart(CCreature* creature, bool reportFailure, int& reuseDelay);
    static bool CompareByHP(CSafePointer2<CCreature>& first, CSafePointer2<CCreature>& second);
    bool IsTargetAttackable(CCreature* creature, CWorldObject* target, int castRange);
    bool ValidateSkillTarget(CCreature* creature, bool toggleSkill, bool auraSkill);
    bool Activate(CCreature& creature, CWorldObject& target, CSkillInfo::EffectActivateTiming timing);
    void GatherSkillTarget(CCreature* creature, CWorldObject* target);
    bool OnTickHandle();
    void CalcMeteoPointList();
    void BroadcastSkillUse(CCreature* creature, int reuseDelay);
    static bool CheckAttackable(
        double range,
        double actorRadius,
        double targetRadius,
        double squaredDistance,
        double actorHeight,
        double targetHeight,
        int actorZ,
        int targetZ
    );
    bool BeReadyToActivate(
        CCreature* creature, int consumeMP, int consumeHP, int consumePledgeNameValue, int consumeSoul, bool recalculateMP
    );

public:
    static double CalcTimeFactor(CCreature& creature, CSkillInfo& skillInfo);
    static double GetCancelTime(CCreature& creature, CSkillInfo& skillInfo);
    static void CalcSkillTiming(CCreature& creature, CSkillInfo& skillInfo, int& hitTime, int& hitCancelTime, int& coolTime);

private:
    bool IsTargetInSameInZone();
    bool IsBlockedSkill(CCreature* creature, int skillId);
    bool IsBlockedMagicType(CCreature* creature, int magicType);
    bool IsCondBlockActSkill(CCreature* creature, int skillId);

    CSmartID m_spTarget;
    bool m_bForce;
    bool m_bSurely;
    double m_dConsumeFactor;
    FVector vCastingStartPos;
    std::vector<FVector> m_vMeteoPointList;
    SkillUseInfo m_skillUseInfo;
    CSkillInfo* m_pSkillInfoA;
    SkillTime m_CurrentAction;
    int m_nHitCancelTime;
    int m_nHitTime;
    int m_nCoolTime;
    int m_nMyIndex;
    bool m_Infected;

public:
    int GetSkillId() const;
    int GetSkillLevel() const;
    int GetSkillSubLevel() const;
    int GetHitCancelTime() const;
    int GetHitTime() const;
    int GetMagicType() const;
    bool IsDoubleCastingAction() const;
    static bool CheckOrDeleteItem(
        CCreature* creature,
        int itemClassId,
        std::int64_t count,
        SpenItemMement moment,
        ConsumeItemSearchType::Type searchType
    );
    bool SpendItem(CCreature* creature, SpenItemMement moment);
    void OnInstantEffectSuccess(CCreature* creature);
    void ActivateAttachedItemSkill(
        CCreature& creature,
        CWorldObject& target,
        const CSkillInfo* skillInfo,
        float probability,
        CSkillInfo::EffectActivateTiming timing
    );

private:
    int m_nChannellingStartTime;

public:
    CrystalType m_eWeaponCrystal;
    double m_dSoulShotPerBonus;

    void Set(CSmartID, CSmartID, const SkillUseInfo&, bool, bool, unsigned int, int);
    void Set(CSmartID, CSmartID, int, int, int, bool, bool, unsigned int, int);

    bool OnStart() override;
    bool OnTick() override;
    bool OnLostTarget(int, bool) override;
    bool OnTargetOutOfSight(int) override;
    bool OnDisappear() override;
    bool OnStopMove() override;
    bool OnDamaged(CSmartID, int) override;
    void OnEnd(bool) override;
    bool CondForHiddenTarget(CWorldObject*) override;

    int m_nUsedItemSID;
    int m_nUsedItemClassId;
    int m_nFatalBlowSuccess;
    int m_nSlotNo;
    bool bCritical;

    friend class CCreatureController;
};


class CTalkAction : public CNewAction {
public:
    ActionType GetType() const override { return ACT_TALK; }

private:
    CSmartID m_spTarget;

    void Set(CSmartID actor, CSmartID target);
    bool OnStart() override;

    bool OnLostTarget(int, bool) override { return true; }

    bool OnDisappear() override { return true; }

    bool OnStopMove() override { return true; }

    bool OnDamaged(CSmartID, int) override { return true; }

    bool OnTargetOutOfSight(int) override { return true; }

    friend class CCreatureController;
};

class CGeneralAction : public CNewAction {
public:
    enum GeneralActionType : std::int32_t {
        GAT_PRIVATE_STORE = 0,
        GAT_MAX = 1,
    };

    ActionType GetType() const override { return ACT_GENERAL; }

private:
    CSmartID m_spTarget;
    GeneralActionType type;

    void Set(CSmartID actor, CSmartID target, GeneralActionType actionType);
    bool OnStart() override;

    bool OnLostTarget(int, bool) override { return true; }

    bool OnDisappear() override { return true; }

    bool OnStopMove() override { return true; }

    bool OnDamaged(CSmartID, int) override { return true; }

    bool OnTargetOutOfSight(int) override { return true; }

    friend class CCreatureController;
};

class CGetItemAction2 : public CNewAction {
public:
    ActionType GetType() const override { return ACT_GETITEM; }

private:
    CSmartID m_spTarget;

    void Set(CSmartID actor, CSmartID target);
    bool OnStart() override;

    bool OnLostTarget(int, bool) override { return true; }

    bool OnDisappear() override { return true; }

    bool OnStopMove() override { return true; }

    bool OnDamaged(CSmartID, int) override { return true; }

    bool OnTargetOutOfSight(int) override { return true; }

    friend class CCreatureController;
};

class COpenCloseAction2 : public CNewAction {
public:
    ActionType GetType() const override { return ACT_OPENCLOSE; }

private:
    CSmartID m_spTarget;

    void Set(CSmartID actor, CSmartID target);
    bool OnStart() override;

    bool OnLostTarget(int, bool) override { return true; }

    bool OnDisappear() override { return true; }

    bool OnStopMove() override { return true; }

    bool OnDamaged(CSmartID, int) override { return true; }

    bool OnTargetOutOfSight(int) override { return true; }

    friend class CCreatureController;
};

class CAttackWaitAction : public CNewAction {
public:
    ActionType GetType() const override { return ACT_ATTACK_WAIT; }

private:
    CSmartID m_spTarget;
    int m_nDelay;
    bool m_bSurely;

    void Set(CSmartID actor, CSmartID target, int delay, bool surely);
    bool OnStart() override;
    bool OnTick() override;

    bool OnLostTarget(int, bool) override { return true; }

    bool OnDisappear() override { return true; }

    bool OnStopMove() override { return true; }

    bool OnDamaged(CSmartID, int) override { return true; }

    bool OnTargetOutOfSight(int) override { return true; }

    friend class CCreatureController;
};

class CPrivateStoreAction : public CNewAction {
public:
    ActionType GetType() const override { return ACT_PRIVATE_STORE; }

private:
    int m_nMode;

    void Set(CSmartID actor, int mode);
    bool OnStart() override;

    bool OnLostTarget(int, bool) override { return true; }

    bool OnDisappear() override { return true; }

    bool OnStopMove() override { return true; }

    bool OnDamaged(CSmartID, int) override { return true; }

    bool OnTargetOutOfSight(int) override { return true; }

    friend class CCreatureController;
};

class CCoupleAction : public CNewAction {
public:
    ActionType GetType() const override { return ACT_COUPLE; }

protected:
    CSmartID m_spTarget;
    int m_nDelayTime;

private:
    void Set(CSmartID actor, CSmartID target, int delayTime);
    bool OnStart() override;

    bool OnLostTarget(int, bool) override { return true; }

    bool OnDisappear() override { return true; }

    bool OnStopMove() override { return true; }

    bool OnDamaged(CSmartID, int) override { return true; }

    bool OnTargetOutOfSight(int) override { return true; }

    friend class CCreatureController;
};
