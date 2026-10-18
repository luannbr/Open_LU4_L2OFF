#include "NewActionHelper.h"



#include "Shared/src/Log.h"


#include <cmath>

bool CAttackActionHelper::CheckAttackable(
    double attackRange,
    double sourceCollisionRadius,
    double targetCollisionRadius,
    double squaredPlanarDistance,
    double sourceCollisionHeight,
    double targetCollisionHeight,
    int sourceZ,
    int targetZ,
    ::AttackType attackType [[maybe_unused]]
) {
    L2_GUARD_FUNCTION();


    const double combinedCollisionRadius = sourceCollisionRadius + targetCollisionRadius;
    const bool insideCombinedRadius = combinedCollisionRadius * combinedCollisionRadius - squaredPlanarDistance >= 0.0;

    const double sourceCenterZ = sourceCollisionHeight * 0.5 + static_cast<double>(sourceZ);
    const double targetCenterZ = targetCollisionHeight * 0.5 + static_cast<double>(targetZ);
    const double sourceBottomZ = sourceCenterZ - sourceCollisionHeight;
    const double targetBottomZ = targetCenterZ - targetCollisionHeight;

    double verticalGap = 0.0;
    bool verticallyOverlapping = false;
    if (sourceCenterZ + 25.0 < targetBottomZ || targetCenterZ + 25.0 < sourceBottomZ) {
        verticalGap = targetBottomZ - sourceCenterZ;
        const double reverseGap = sourceBottomZ - targetCenterZ;
        if (verticalGap <= reverseGap) {
            verticalGap = reverseGap;
        }
        if (verticalGap < 0.0) {
            verticalGap = 0.0;
        }
    } else {
        verticallyOverlapping = true;
    }

    if (insideCombinedRadius) {
        if (verticallyOverlapping) {
            return true;
        }
        return attackRange >= verticalGap;
    }

    if (verticallyOverlapping) {
        const double reachableRadius = combinedCollisionRadius + attackRange;
        return reachableRadius * reachableRadius >= squaredPlanarDistance;
    }

    if (squaredPlanarDistance < 0.0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]distance is minus[%f],",
        //     "NewActionHelper.cpp",
        //     0x3f,
        //     squaredPlanarDistance)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]distance is minus[%f],",
        //     "NewActionHelper.cpp",
        //     0x3f,
        //     squaredPlanarDistance)
        L2_LOG_ERROR(
            Log,
            "Attack-range check received negative squared planar distance %.17g; the square-root calculation continues.",
            squaredPlanarDistance
        );
    }

    const double planarGap = std::sqrt(squaredPlanarDistance) - combinedCollisionRadius;
    const double requiredDistanceSquared = verticalGap * verticalGap + planarGap * planarGap;
    return attackRange * attackRange >= requiredDistanceSquared;
}

CNewActionHelper::CNewActionHelper() {
    L2_GUARD_FUNCTION();

    for (int index = 0; index < 121; ++index) {
        const int oneBasedIndex = index + 1;
        const double normalizedIndex = static_cast<double>(index) / static_cast<double>(oneBasedIndex);
        const double normalizedPower = std::pow(normalizedIndex, 2.35);
        const int numerator = (index - 1) * oneBasedIndex * index;
        const double divisorPower = std::pow(static_cast<double>(index + 2), 6.0);
        highLevelDamageFactor[index] = static_cast<double>(numerator) / divisorPower + (1.0 - normalizedPower);
    }
}

CNewActionHelper::~CNewActionHelper() = default;

double CNewActionHelper::GetHighLevelDamageFactor(int characterLevel, int targetLevel) {
    L2_GUARD_FUNCTION();

    int levelDifference = 1;
    if (characterLevel > 84) {
        levelDifference = characterLevel - targetLevel;
        if (levelDifference > 121) {
            levelDifference = 121;
        }
        if (levelDifference < 1) {
            levelDifference = 1;
        }
    }
    return highLevelDamageFactor[levelDifference - 1];
}

// Current CRT slot 6407 (cell 0x00de9038, root 0x00d9c8a8) is a
// five-byte tail jump to Singleton<CNewActionHelper>::inst. The historical
// source name/type and NewActionHelper.obj ownership are kept here. The
// empty carrier has no observable state or teardown and exists solely to
// keep the target's eager pre-main singleton creation.
class CDummy {
public:
    CDummy() { static_cast<void>(CNewActionHelper::inst()); }
};


CDummy _just_init_do_not_use_directly_;
