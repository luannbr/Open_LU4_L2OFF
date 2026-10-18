#pragma once

#include "CMoveToTargetShareData.h"
#include "NewAction.h"

enum MoveToTargetType : std::int32_t {
    MTTT_MOVE_TO_MELEE_POINT = 0,
    MTTT_MOVE_TO_PAWN = 1,
};

extern bool g_CheckCollisionForNPCMove;
// Current newaction_move.obj byte 0x173dd1a8. The Setting producer and
// collision consumers close the storage contract. The donor
// MAP supplies the original public spelling and physical owner.
extern bool g_bWorldCollision;

class CMoveActionBase : public CNewAction {
public:
    CMoveActionBase();

    void CalcMidDest(
        const FVector& destination,
        FVector targetPosition,
        double distance,
        FVector& middleDestination,
        int& tick,
        bool& moving,
        bool& arrived,
        bool force,
        CCreature* creature
    );

    static const int MoveTick;

    static bool NeedMove(CCreature& creature, CWorldObject& target, const double distance);
    static FVector GetTargetPos(CCreature& creature, CWorldObject& target);
    static FVector GetTargetDestAsNormal(CCreature& creature, CWorldObject& target, int distance);
    static FVector GetTargetDestAsFollow(CCreature& creature, CWorldObject& target);

    void OnEnd(bool success) override;

    FVector m_vDest;
    FVector m_vMidDest;
    FVector m_vMidRelDest;
    int m_nTick;
    bool m_bMoving;
    bool m_bArrived;
};

class CMoveAction : public CMoveActionBase {
public:
    CMoveAction();

    ActionType GetType() const override { return ACT_MOVE; }

    FVector m_vPathFindMidDest;
    bool m_bPathFind;
    bool m_bPathFinding;
    bool m_bResetDest;
    int m_nSkillId;
    int m_nSkillLevel;
    int m_nSkillSubLevel;
    bool m_bForceAttack;
    int m_nUsedItemSID;

    void Set(
        CSmartID actor,
        FVector destination,
        bool pathFind,
        bool forceAttack,
        int skillId,
        int skillLevel,
        int skillSubLevel,
        int usedItemSID
    );

    bool OnStart() override;
    bool OnTick() override;

    bool OnLostTarget(int, bool) override { return true; }

    bool OnTargetOutOfSight(int) override { return true; }

    bool OnDisappear() override { return true; }

    bool OnStopMove() override;

    bool OnDamaged(CSmartID, int) override { return true; }

    void OnEnd(bool success) override;

protected:
    bool SetMoving();
    bool SetMovingRelativeImpl();
    bool SetMovingCameraModeImpl();
    bool SetMovingFlyingImpl();

private:
    void SetValidPendingAction(CCreature* creature);
};

class CMoveRelativeAction : public CMoveActionBase {
public:
    ActionType GetType() const override { return ACT_MOVE_RELATIVE; }

    bool SetMovingIntoVehicleImpl();
    bool SetMovingIntoAirShipImpl();
    bool SetMovingIntoShuttleImpl();

protected:
    bool SetMoving();
    bool SetMovingInVehicleImpl();
    bool SetMovingInAirShipImpl();
    bool SetMovingInShuttleImpl();
    FVector GetDestination();

private:
    CSmartID m_spRelativeWorldObject;
    FVector m_vRelPos;

    void Set(CSmartID actor, CSmartID relativeWorldObject, FVector relativePosition);

    bool OnStart() override;
    bool OnTick() override;

    bool OnLostTarget(int, bool) override { return true; }

    bool OnTargetOutOfSight(int) override { return true; }

    bool OnDisappear() override { return true; }

    void OnEnd(bool success) override;
    bool OnStopMove() override;

    bool OnDamaged(CSmartID, int) override { return true; }

    friend class CCreatureController;
};

class CMoveToTargetActionBase : public CMoveActionBase {
public:
    bool IsTarget(CObject* target) override { return target->GetSmartID().Sid() == m_spTarget.Sid(); }

    bool OnDisappear() override { return false; }

    bool OnLostTarget(int, bool) override { return true; }

    bool OnTargetOutOfSight(int distance) override;

    bool OnStopMove() override { return false; }

    bool OnDamaged(CSmartID, int) override { return true; }

    void ComputeDistance(CCreature& creature, CWorldObject& target);
    FVector GetTargetDestination(CCreature& creature, CWorldObject& target);

    CSmartID GetActor();
    int GetDistance();
    int GetAcceptableDistance();
    void SetDistance(int distance);
    void SetAcceptableDistance(int acceptableDistance);
    int GetSkillId();
    int GetSkillRange();
    bool IsForceAttack();
    void SetSkillId(int skillId);
    void SetSkillRange(int skillRange);
    void SetForceAttack(bool forceAttack);

    CSmartID m_spTarget;
    MoveToTargetType m_MoveToTargetType;
    SkillUseInfo m_skilluseinfo;
    int m_nSkillLevel;
    int m_nSkillSubLevel;
    int m_nUsedItemSID;
    CMoveToTargetShareData m_Data;
};

class CMoveToTargetRelativeAction : public CMoveToTargetActionBase {
public:
    ActionType GetType() const override { return ACT_MOVE_TO_TARGET_RELATIVE; }

    CSmartID m_spAirShip;

    void Set(
        CSmartID actor,
        CSmartID target,
        bool forceAttack,
        int skillId,
        int skillLevel,
        int skillSubLevel,
        int skillRange,
        int usedItemSID,
        CSmartID airShip
    );
    bool OnStart() override;
    bool OnTick() override;
    void OnEnd(bool success) override;
    bool SetMoving();
    FVector GetDestination();
    void SetValidPendingAction();
    FVector GetMeleeAttackPosition(CCreature* creature, CWorldObject* target);
};

class CMoveToTargetAction : public CMoveToTargetActionBase {
public:
    ActionType GetType() const override { return ACT_MOVE_TO_TARGET; }

    FVector m_vTargetPosOnSetMoving;
    bool m_bPathFinding;

    void Set(CSmartID actor, CSmartID target, bool forceAttack, const SkillUseInfo& skillUseInfo, int usedItemSID);
    void Set(
        CSmartID actor,
        CSmartID target,
        bool forceAttack,
        int skillId,
        int skillLevel,
        int skillSubLevel,
        int skillRange,
        int usedItemSID
    );
    bool OnStart() override;
    bool OnTick() override;
    void OnEnd(bool success) override;
    bool SetMoving();
    void SetValidPendingAction();
    bool CondForHiddenTarget(CWorldObject* target) override;
    bool CheckValidTarget(bool flag);
};


class CMoveToTargetWaitAction : public CMoveActionBase {
public:
    ActionType GetType() const override { return ACT_MOVE_TO_TARGET_WAIT; }

    void Set(CSmartID actor, CSmartID target, int acceptableDistance);

    bool OnStart() override;
    bool OnTick() override;

    bool OnLostTarget(int, bool) override { return true; }

    bool OnTargetOutOfSight(int distance) override;

    bool OnDisappear() override { return false; }

    bool OnStopMove() override { return false; }

    bool OnDamaged(CSmartID, int) override { return true; }

private:
    CSmartID m_spTarget;
    int m_acceptableDist;
};
