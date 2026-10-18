#pragma once

#include "Shared/src/BaseStructure.h"
#include "Shared/src/TemplateHelper.h"

#include <cstddef>

class CNewActionHelper;

// Stateless range condition compiled by the current NewActionHelper.cpp.
class CAttackActionHelper {
public:
    static bool CheckAttackable(
        double attackRange,
        double sourceCollisionRadius,
        double targetCollisionRadius,
        double squaredPlanarDistance,
        double sourceCollisionHeight,
        double targetCollisionHeight,
        int sourceZ,
        int targetZ,
        ::AttackType attackType
    );
};

// The current object is a Singleton<CNewActionHelper> followed by 121
// precomputed damage factors. Its only virtual slot is the deleting
// destructor inherited from the singleton base.
class CNewActionHelper : public ::Singleton<CNewActionHelper> {
public:
    CNewActionHelper();
    ~CNewActionHelper() override;

    double GetHighLevelDamageFactor(int characterLevel, int targetLevel);

private:
    double highLevelDamageFactor[121];
};
