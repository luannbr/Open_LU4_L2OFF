#pragma once

#include "NewActionMove.h"

#include "Shared/src/ThreadSafeLibrary.h"

#include <cstddef>
#include <cstdint>

class CCreatureController : public ::CIOObject {
    friend class CNewAction;
    friend class CAttackAction;
    friend class CAttackWaitAction;
    friend class CMoveAction;
    friend class CMoveToTargetActionBase;
    friend class CMoveToTargetAction;
    friend class CMoveToTargetRelativeAction;
    friend class CMoveToTargetWaitAction;
    friend class CSkillAction2;

public:
    struct ActionData {
        ActionData();
        ActionData(const ActionData&) = default;

        ActionData& operator=(const ActionData& other);
        CSkillAction2& GetSkillAction(int& index, bool flag, int requestedIndex);

        CNothingAction m_Nothing;
        CStandAction m_Stand;
        CSitAction m_Sit;
        CMoveAction m_Move;
        CAttackAction m_Attack;
        CSkillAction2 m_Skill[2];
        CTalkAction m_Talk;
        CGeneralAction m_General;
        CGetItemAction2 m_GetItem;
        COpenCloseAction2 m_OpenClose;
        CMoveToTargetAction m_MoveToTarget;
        CMoveToTargetRelativeAction m_MoveToTargetRelative;
        CMoveToTargetWaitAction m_MoveToTargetWait;
        CMoveRelativeAction m_MoveRelative;
        CAttackWaitAction m_AttackWait;
        CPrivateStoreAction m_PrivateStore;
        CCoupleAction m_CoupleAction;
        ActionType m_ActionType;
        CSmartID m_spPostTarget;
    };

    using CreatureSP2VectorType = std::vector<::CSafePointer2<CCreature>>;

    struct OptionData {
        OptionData();
        OptionData(int skillId, int skillLevel, int skillSubLevel, bool forceAttack, int skillRange, int usedItemSID);
        OptionData(int skillId, int skillLevel, bool forceAttack, int skillRange, int usedItemSID) :
            m_nSkillId(skillId),
            m_nSkillLevel(skillLevel),
            m_nSkillSubLevel(0),
            m_bForceAttack(forceAttack),
            m_nSkillRange(skillRange),
            m_nUsedItemSID(usedItemSID) {}

        int m_nSkillId;
        int m_nSkillLevel;
        int m_nSkillSubLevel;
        bool m_bForceAttack;
        int m_nSkillRange;
        int m_nUsedItemSID;
    };

    L2_ASYNC_DECLARE_FIELDS_1(__async_arg_DoNothing, bool);

    L2_ASYNC_DECLARE_FIELDS_1(__async_arg_Stand, int);

    L2_ASYNC_DECLARE_FIELDS_2(__async_arg_Sit, int, CObject*);

    L2_ASYNC_DECLARE_FIELDS_3(__async_arg_Move, FVector, bool, OptionData*);

    L2_ASYNC_DECLARE_FIELDS_4(__async_arg_MoveToTarget, CObject*, bool, SkillUseInfo, int);

    L2_ASYNC_DECLARE_FIELDS_4(__async_arg_MoveToTargetRelative, CObject*, bool, SkillUseInfo, int);

    L2_ASYNC_DECLARE_FIELDS_2(__async_arg_Attack, CObject*, bool);

    L2_ASYNC_DECLARE_FIELDS_4(__async_arg_Skill, SkillUseInfo, bool, bool, unsigned int);

    L2_ASYNC_DECLARE_FIELDS_1(__async_arg_Talk, CCreature*);

    class __async_arg_GetItem : public CExecutionArgument {
    public:
        __async_arg_GetItem(__async_arg_GetItem* other);
        __async_arg_GetItem();
        explicit __async_arg_GetItem(CObject* a1);
        void CopyToPad(CExecutionArgumentPad* destination) const override;

        CObject* __a1;
    };

    class __async_arg_OpenClose : public CExecutionArgument {
    public:
        __async_arg_OpenClose(__async_arg_OpenClose* other);
        __async_arg_OpenClose();
        explicit __async_arg_OpenClose(CObject* a1);
        void CopyToPad(CExecutionArgumentPad* destination) const override;

        CObject* __a1;
    };

    class __async_arg_ChangeWeapon : public CExecutionArgument {
    public:
        __async_arg_ChangeWeapon();
        void CopyToPad(CExecutionArgumentPad* destination) const override;
    };

    L2_ASYNC_DECLARE_FIELDS_2(__async_arg_MoveRelatively, CAbstractCarrier*, FVector);

    class __async_arg_AttackWait : public CExecutionArgument {
    public:
        __async_arg_AttackWait(__async_arg_AttackWait* other);
        __async_arg_AttackWait();
        __async_arg_AttackWait(CObject* a1, int a2);
        void CopyToPad(CExecutionArgumentPad* destination) const override;

        CObject* __a1;
        int __a2;
    };

    L2_ASYNC_DECLARE_FIELDS_1(__async_arg_PrivateStore, int);

    class __async_arg_Couple : public CExecutionArgument {
    public:
        __async_arg_Couple(__async_arg_Couple* other);
        __async_arg_Couple();
        __async_arg_Couple(int a1, CObject* a2);
        void CopyToPad(CExecutionArgumentPad* destination) const override;

        int __a1;
        CObject* __a2;
    };

    L2_ASYNC_DECLARE_EMPTY_2_SLOT(__async_arg_OnStopMove);

    L2_ASYNC_DECLARE_FIELDS_2(__async_arg_LostTarget, int, bool);

    L2_ASYNC_DECLARE_EMPTY_2_SLOT(__async_arg_Disappear);

    L2_ASYNC_DECLARE_FIELDS_1(__async_arg_TargetOutOfSight, int);

    L2_ASYNC_DECLARE_FIELDS_2(__async_arg_OnDamagedMessage, CSmartID, int);

    L2_ASYNC_DECLARE_EMPTY_2_SLOT(__async_arg_CancelReservedAction);

    L2_ASYNC_DECLARE_FIELDS_1(__async_arg_DoAutoFishing, bool);

    L2_ASYNC_DECLARE_FIELDS_3(__async_arg_AsyncTimerExpired, int, int, int);

private:
    static const wchar_t* m_pClassName_CCreatureController;

public:
    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    L2_DECLARE_OBJECT_ALLOCATORS()

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    CCreatureController();
    ~CCreatureController() override;

    CCreature* GetActor() { return m_spActor.GetCreature(); }
    void SetActor(CCreature* actor);
    void ActionBroadcastMove(const FVector& destination, const FVector& middleDestination);
    void ActionBroadcastMoveToTarget(CWorldObject& target, int distance);
    void ActionBroadcastMoveToTargetInAirShip(
        const FVector& destination, const FVector& middleDestination, int distance, int moveType, unsigned int airShipId
    );
    void ActionBroadcastStopMove();
    void ActionBroadcastSkillUse(
        CWorldObject& target,
        CSkillInfo* skillInfo,
        int hitTime,
        int reuseDelay,
        CSkillAction2* skillAction,
        std::vector<FVector>* targets,
        const SkillUseInfo& skillUseInfo
    );
    void ActionBroadcastEarthQuake(FVector position, int intensity, int duration);
    void ActionBroadcastSkillCanceled(CSkillAction2* skillAction);
    void ActionBroadcastSkillLaunched(int skillId, int skillLevel, int skillSubLevel, int targetId, int skillIndex);
    void ActionBroadcastSkillLaunched(
        int skillId, int skillLevel, int skillSubLevel, CreatureSP2VectorType targets, int skillIndex
    );
    void ActionBroadcastAttack(
        const AttackTargetDataType& primaryTarget, int targetCount, AttackTargetDataType* targets, CWorldObject& target
    );
    void BroadcastFlySelfDestination(CSkillInfo* skillInfo, int destinationType);
    void DoAttack(
        AttackTargetDataType& primaryTarget,
        int targetCount,
        AttackTargetDataType* targets,
        int attackRange,
        AttackType attackType,
        SlotType slotType
    );

    ActionType GetCurrentActionType();
    ActionType GetNextActionType();
    ActionState::Enum GetCurrentActionState() { return GetCurrentAction(-1)->m_ActionState; }
    int GetCurrentActionSkillId();
    int GetCurrentActionSkillLevel();
    int GetCurrentActionSkillSubLevel();
    int GetCurrentActionSkillMagicType();
    void SetJustUnfrozen();

protected:
    virtual CNewAction* GetCurrentAction(int actionNumber);
    virtual CNewAction* GetPendingAction();
    virtual CNewAction* GetAction(ActionType actionType, int actionNumber, int skillIndex);
    virtual CNewAction* GetAction(int actionType, int actionNumber);

    void DoFirstAction();
    void DoPendingAction(int actionNumber, bool flag);

public:
    void EndCurrentAction(bool success);

private:
    bool IsActorAndTargetFrozen(CCreature* actor);

protected:
    void SetAsNothing(int actionNumber);
    void SetAsStand(int actionNumber, int delayTime);
    void SetAsSit(int actionNumber, int delayTime, CSmartID target, bool sitType);
    void SetAsMove(
        int actionNumber,
        FVector destination,
        bool pathFind,
        bool forceAttack,
        int skillId,
        int skillLevel,
        int skillSubLevel,
        int usedItemSID
    );
    void SetAsMoveToTarget(
        int actionNumber, CSmartID target, bool forceAttack, const SkillUseInfo& skillUseInfo, int usedItemSID
    );
    void SetAsMoveToTarget(
        int actionNumber,
        CSmartID target,
        bool forceAttack,
        int skillId,
        int skillLevel,
        int skillSubLevel,
        int skillRange,
        int usedItemSID
    );
    void SetAsMoveToTargetRelative(
        int actionNumber,
        CSmartID target,
        bool forceAttack,
        int skillId,
        int skillLevel,
        int skillSubLevel,
        int skillRange,
        int usedItemSID,
        CSmartID airShip
    );
    void SetAsMoveToTargetWait(int actionNumber, CSmartID target, int acceptableDistance);
    void SetAsAttack(int actionNumber, CSmartID target, bool surely);
    void SetAsSkill(
        int actionNumber,
        CSmartID target,
        const SkillUseInfo& skillUseInfo,
        bool forceAttack,
        bool flag,
        unsigned int skillIndex
    );
    void SetAsSkill(
        int actionNumber,
        CSmartID target,
        int skillId,
        int skillLevel,
        int skillSubLevel,
        bool forceAttack,
        bool flag,
        unsigned int skillIndex
    );
    void SetAsTalk(int actionNumber, CSmartID target);
    void SetAsGeneral(int actionNumber, CSmartID target, CGeneralAction::GeneralActionType actionType);
    void SetAsGetItem(int actionNumber, CSmartID target);
    void SetAsOpenClose(int actionNumber, CSmartID target);
    void SetAsMoveRelative(int actionNumber, CAbstractCarrier* carrier, FVector relativePosition);
    void SetAsAttackWait(int actionNumber, CSmartID target, int delay, bool surely);
    void SetAsPrivateStore(int actionNumber, int mode);
    void SetAsCouple(int actionNumber, int delayTime, CSmartID target);

    static bool IsMoveLikeAction(ActionType actionType);
    void ShowActionChange(int actionNumber, wchar_t* actionName);

public:
    void ERROR___async_arg_DoNothingTooBigArgument(Loki::CompileTimeError<1>);
    void __async_DoNothing(CExecutionArgumentPad* argument);
    void DoNothing(bool success);
    void ERROR___async_arg_StandTooBigArgument(Loki::CompileTimeError<1>);
    void __async_Stand(CExecutionArgumentPad* argument);
    void Stand(int delayTime);
    void ERROR___async_arg_SitTooBigArgument(Loki::CompileTimeError<1>);
    void __async_Sit(CExecutionArgumentPad* argument);
    void Sit(int delayTime, CObject* target);
    void ERROR___async_arg_MoveTooBigArgument(Loki::CompileTimeError<1>);
    void __async_Move(CExecutionArgumentPad* argument);
    void Move(FVector destination, bool pathFind, OptionData* optionData);
    void ERROR___async_arg_MoveToTargetTooBigArgument(Loki::CompileTimeError<1>);
    void __async_MoveToTarget(CExecutionArgumentPad* argument);
    void MoveToTarget(CObject* target, bool forceAttack, SkillUseInfo skillUseInfo, int usedItemSID);
    void ERROR___async_arg_MoveToTargetRelativeTooBigArgument(Loki::CompileTimeError<1>);
    void __async_MoveToTargetRelative(CExecutionArgumentPad* argument);
    void MoveToTargetRelative(CObject* target, bool forceAttack, SkillUseInfo skillUseInfo, int usedItemSID);
    void ERROR___async_arg_AttackTooBigArgument(Loki::CompileTimeError<1>);
    void __async_Attack(CExecutionArgumentPad* argument);
    void Attack(CObject* target, bool surely);
    void ERROR___async_arg_SkillTooBigArgument(Loki::CompileTimeError<1>);
    void __async_Skill(CExecutionArgumentPad* argument);
    void Skill(SkillUseInfo skillUseInfo, bool forceAttack, bool flag, unsigned int skillIndex);
    void ERROR___async_arg_TalkTooBigArgument(Loki::CompileTimeError<1>);
    void __async_Talk(CExecutionArgumentPad* argument);
    void Talk(CCreature* target);
    void ERROR___async_arg_GetItemTooBigArgument(Loki::CompileTimeError<1>);
    void __async_GetItem(CExecutionArgumentPad* argument);
    void GetItem(CObject* target);
    void ERROR___async_arg_OpenCloseTooBigArgument(Loki::CompileTimeError<1>);
    void __async_OpenClose(CExecutionArgumentPad* argument);
    void OpenClose(CObject* target);
    void __async_ChangeWeapon(CExecutionArgumentPad* argument);
    void ChangeWeapon();
    void ERROR___async_arg_MoveRelativelyTooBigArgument(Loki::CompileTimeError<1>);
    void __async_MoveRelatively(CExecutionArgumentPad* argument);
    void MoveRelatively(CAbstractCarrier* carrier, FVector relativePosition);
    void ERROR___async_arg_AttackWaitTooBigArgument(Loki::CompileTimeError<1>);
    void __async_AttackWait(CExecutionArgumentPad* argument);
    void AttackWait(CObject* target, int delay);
    void ERROR___async_arg_PrivateStoreTooBigArgument(Loki::CompileTimeError<1>);
    void __async_PrivateStore(CExecutionArgumentPad* argument);
    void PrivateStore(int mode);
    void ERROR___async_arg_CoupleTooBigArgument(Loki::CompileTimeError<1>);
    void __async_Couple(CExecutionArgumentPad* argument);
    void Couple(int delayTime, CObject* target);
    void __async_OnStopMove(CExecutionArgumentPad* argument);
    void OnStopMove();
    void ERROR___async_arg_LostTargetTooBigArgument(Loki::CompileTimeError<1>);
    void __async_LostTarget(CExecutionArgumentPad* argument);
    void LostTarget(int targetId, bool flag);
    void __async_Disappear(CExecutionArgumentPad* argument);
    void Disappear();
    void ERROR___async_arg_TargetOutOfSightTooBigArgument(Loki::CompileTimeError<1>);
    void __async_TargetOutOfSight(CExecutionArgumentPad* argument);
    void TargetOutOfSight(int distance);
    void ERROR___async_arg_OnDamagedMessageTooBigArgument(Loki::CompileTimeError<1>);
    void __async_OnDamagedMessage(CExecutionArgumentPad* argument);
    void OnDamagedMessage(CSmartID attacker, int damage);
    virtual void OnDamaged(CSmartID attacker, int damage);
    void __async_CancelReservedAction(CExecutionArgumentPad* argument);
    void CancelReservedAction();
    void ERROR___async_arg_DoAutoFishingTooBigArgument(Loki::CompileTimeError<1>);
    void __async_DoAutoFishing(CExecutionArgumentPad* argument);
    void DoAutoFishing(bool start);

protected:
    virtual void AddTimer2(int delayMilliseconds, std::int64_t actionNumber);
    void ERROR___async_arg_AsyncTimerExpiredTooBigArgument(Loki::CompileTimeError<1>);
    void __async_AsyncTimerExpired(CExecutionArgumentPad* argument);
    void AsyncTimerExpired(int id, int actionNumber, int actionSequence);
    void AsyncTimerExpired2(int id, int actionNumber, int actionSequence);
    void TimerExpired(int id) override;
    void TimerExpired2(int id, std::int64_t uid) override;

    int m_nActionNumber[2];
    int m_nBlockCount;
    CSmartID m_spActor;
    ActionData m_Action[2];

public:
    CExecutionSerializer<CCreatureController, 64> Serializer;
};
