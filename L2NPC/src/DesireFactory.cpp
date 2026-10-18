#include "L2NPC/src/DesireFactory.h"

#include "L2Core/src/inc/Guard.h"
#include "Runtime.h"
#include "Shared/src/SharedFactory.h"

#include <cstdint>


CDesire CDesireFactory::MakeMoveAround(float value, int range, std::int64_t contextValue) {
    L2_GUARD_FUNCTION();

    CDesire desire(DESIRE_MOVE_AROUND);
    desire.value = value;
    desire.range = range;
    desire.contextValue = contextValue;
    return desire;
}

CDesire CDesireFactory::MakeDoNothing(int seconds, float value) {
    L2_GUARD_FUNCTION();

    CDesire desire(DESIRE_DO_NOTHING);
    desire.deadlineTick = runtime::GetTickCount() + static_cast<std::uint32_t>(seconds) * UINT32_C(1000);
    desire.value = value;
    return desire;
}

CDesire CDesireFactory::MakeAttack(
    int targetId, NpcAttackMoveType attackMoveType, bool force, float value, float boost, int levelDifference
) {
    L2_GUARD_FUNCTION();

    CDesire desire(DESIRE_ATTACK);
    desire.targetId = targetId;
    desire.attackMoveType = attackMoveType;
    desire.value = value;
    desire.boost = boost;
    desire.force = force;
    desire.levelDifference = levelDifference;
    return desire;
}

CDesire CDesireFactory::MakeAttack(int targetId, int levelDifference) {
    L2_GUARD_FUNCTION();

    return MakeAttack(targetId, AMT_STAND, false, 0.0F, 0.0F, levelDifference);
}

CDesire CDesireFactory::MakeEffectAction(
    int creatureId, std::int64_t contextValue, int deadlineTick, float value, float boost
) {
    L2_GUARD_FUNCTION();

    CDesire desire(DESIRE_EFFECT_ACTION);
    desire.creatureId = creatureId;
    desire.contextValue = contextValue;
    desire.deadlineTick = deadlineTick;
    desire.value = value;
    desire.boost = boost;
    return desire;
}

CDesire CDesireFactory::MakeEffectAction2(
    int creatureId, std::int64_t contextValue, int deadlineTick, float value, float boost, int range
) {
    L2_GUARD_FUNCTION();

    CDesire desire(DESIRE_EFFECT_ACTION2);
    desire.creatureId = creatureId;
    desire.contextValue = contextValue;
    desire.deadlineTick = deadlineTick;
    desire.value = value;
    desire.boost = boost;
    desire.range = range;
    return desire;
}

CDesire CDesireFactory::MakeChase(int creatureId, float value) {
    L2_GUARD_FUNCTION();

    CDesire desire(DESIRE_CHASE);
    desire.creatureId = creatureId;
    desire.value = value;
    return desire;
}

CDesire CDesireFactory::MakeFlee(int creatureId, std::int64_t contextValue, float value) {
    L2_GUARD_FUNCTION();

    CDesire desire(DESIRE_FLEE);
    desire.creatureId = creatureId;
    desire.contextValue = contextValue;
    desire.value = value;
    return desire;
}

CDesire CDesireFactory::MakeGetItem(CSharedItemData* item, float value) {
    L2_GUARD_FUNCTION();

    CDesire desire(DESIRE_GET_ITEM);
    desire.item = item;
    desire.value = value;
    return desire;
}

CDesire CDesireFactory::MakeFollow(
    CSharedCreatureData& source, int targetCreatureId, float value, int followType, int followData1, int followData2
) {
    L2_GUARD_FUNCTION();

    CDesire desire(DESIRE_FOLLOW);
    desire.creatureId = source.m_nId;
    desire.value = value;
    desire.followType = followType;
    desire.followData1 = followData1;
    desire.followData2 = followData2;
    desire.targetCreatureId = targetCreatureId;
    return desire;
}

CDesire CDesireFactory::MakeDecaying(float value) {
    L2_GUARD_FUNCTION();

    CDesire desire(DESIRE_DECAYING);
    desire.value = value;
    return desire;
}

CDesire CDesireFactory::MakeMoveToWayPoint(
    std::vector<FVector>* waypoints, std::vector<int>* waypointDelays, int waypointIndex, float value
) {
    L2_GUARD_FUNCTION();

    CDesire desire(DESIRE_MOVE_WAYPOINT);
    desire.waypoints = waypoints;
    desire.waypointDelays = waypointDelays;
    desire.waypointIndex = waypointIndex;
    desire.value = value;
    return desire;
}

CDesire CDesireFactory::MakeUseSkill(
    std::uint32_t targetId,
    std::int64_t skillNameId,
    NpcAttackMoveType attackMoveType,
    int force,
    float value,
    float boost,
    std::uint32_t flags,
    int originId,
    int actionEvent
) {
    L2_GUARD_FUNCTION();

    CDesire desire(DESIRE_USE_SKILL);
    desire.targetId = static_cast<int>(targetId);
    desire.contextValue = skillNameId;
    desire.value = value;
    desire.boost = boost;
    desire.boosted = false;
    desire.boostCount = 0;
    desire.attackMoveType = attackMoveType;
    desire.force = force != 0;
    desire.flags = flags;
    // The current revision added these two parameters without growing
    // CDesire: UseSkill owns the payload slots used by Follow at +0xbc/+0xb8.
    desire.followData1 = originId;
    desire.followType = actionEvent;
    return desire;
}

CDesire CDesireFactory::MakeUseSkill(std::uint32_t targetId, std::int64_t skillNameId) {
    L2_GUARD_FUNCTION();

    return MakeUseSkill(targetId, skillNameId, AMT_STAND, 0, 0.0F, 0.0F, 0, 0, 0);
}

CDesire CDesireFactory::MakeMoveTo(FVector position, float value) {
    L2_GUARD_FUNCTION();

    CDesire desire(DESIRE_MOVE_TO);
    desire.position = position;
    desire.value = value;
    return desire;
}

CDesire CDesireFactory::MakePetDefault() {
    L2_GUARD_FUNCTION();

    return CDesire(DESIRE_PET_DEFAULT);
}

CDesire CDesireFactory::MakePetDefault_Follow(float value) {
    L2_GUARD_FUNCTION();

    CDesire desire(DESIRE_PET_DEFAULT);
    desire.contextValue = 0;
    desire.value = value;
    return desire;
}

CDesire CDesireFactory::MakeMoveToTarget(int targetId, std::int64_t contextValue, std::uint32_t flags, float value) {
    L2_GUARD_FUNCTION();

    CDesire desire(DESIRE_MOVE_TO_TARGET);
    desire.targetId = targetId;
    desire.contextValue = contextValue;
    desire.flags = flags;
    desire.value = value;
    return desire;
}

CDesire CDesireFactory::MakeMoveSuperPoint(const char16_t* name, std::int64_t contextValue, int contextCode, float value) {
    L2_GUARD_FUNCTION();

    CDesire desire(DESIRE_MOVE_SUPER_PT);
    desire.name = name;
    desire.contextValue = contextValue;
    desire.value = value;
    desire.m_MoveSuperPointContext.m_Order = contextCode;
    return desire;
}

CDesire CDesireFactory::MakeMoveFreeway(int id, std::int64_t contextValue, float value) {
    L2_GUARD_FUNCTION();

    CDesire desire(DESIRE_MOVE_FREEWAY);
    desire.id = id;
    desire.contextValue = contextValue;
    desire.value = value;
    return desire;
}

CDesire CDesireFactory::MakeMoveFormation(
    const char16_t* name,
    int id,
    std::int64_t contextValue,
    int uniqueId,
    int formationId,
    int formationIndex,
    int flags,
    float value
) {
    L2_GUARD_FUNCTION();

    CDesire desire(DESIRE_MOVE_FORMATION);
    desire.name = name;
    desire.id = id;
    desire.contextValue = contextValue;
    desire.uniqueId = uniqueId;
    desire.formationId = formationId;
    desire.formationIndex = formationIndex;
    desire.flags = flags;
    desire.value = value;
    return desire;
}
