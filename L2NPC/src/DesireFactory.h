#pragma once

#include "Desire.h"
#include "Shared/src/StandardContainers.h"

#include <cstdint>

class CSharedCreatureData;
class CSharedItemData;

class CDesireFactory {
public:
    static CDesire MakeMoveAround(float value, int range, std::int64_t contextValue);
    static CDesire MakeDoNothing(int seconds, float value);
    static CDesire MakeAttack(
        int targetId, NpcAttackMoveType attackMoveType, bool force, float value, float boost, int levelDifference
    );
    static CDesire MakeAttack(int targetId, int levelDifference);
    static CDesire MakeChase(int creatureId, float value);
    static CDesire MakeFlee(int creatureId, std::int64_t contextValue, float value);
    static CDesire MakeEffectAction(int creatureId, std::int64_t contextValue, int deadlineTick, float value, float boost);
    static CDesire MakeEffectAction2(
        int creatureId, std::int64_t contextValue, int deadlineTick, float value, float boost, int range
    );
    static CDesire MakeGetItem(CSharedItemData* item, float value);
    static CDesire MakeFollow(
        CSharedCreatureData& source, int targetCreatureId, float value, int followType, int followData1, int followData2
    );
    static CDesire MakeDecaying(float value);
    static CDesire MakeMoveToWayPoint(
        std::vector<FVector>* waypoints, std::vector<int>* waypointDelays, int waypointIndex, float value
    );
    static CDesire MakeUseSkill(
        std::uint32_t targetId,
        std::int64_t skillNameId,
        NpcAttackMoveType attackMoveType,
        int force,
        float value,
        float boost,
        std::uint32_t flags,
        int originId = 0,
        int actionEvent = 0
    );
    static CDesire MakeUseSkill(std::uint32_t targetId, std::int64_t skillNameId);
    static CDesire MakeMoveTo(FVector position, float value);
    static CDesire MakeMoveToTarget(int targetId, std::int64_t contextValue, std::uint32_t flags, float value);
    static CDesire MakePetDefault();
    static CDesire MakePetDefault_Follow(float value);
    static CDesire MakeMoveSuperPoint(const char16_t* name, std::int64_t contextValue, int contextCode, float value);
    static CDesire MakeMoveFreeway(int id, std::int64_t contextValue, float value);
    static CDesire MakeMoveFormation(
        const char16_t* name,
        int id,
        std::int64_t contextValue,
        int uniqueId,
        int formationId,
        int formationIndex,
        int flags,
        float value
    );
};
