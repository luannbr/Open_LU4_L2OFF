#pragma once

#include "Shared/src/BaseStructure.h"
#include "Shared/src/ItemOptionKey.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>

class CSkillInfo;

// Exact current Shared value used by L2Server's ItemOption. The skill pointer
// is borrowed. Neither assignment nor destruction owns the pointed object.
struct RandomActivationSkill {
    RandomActivationSkill() = default;
    RandomActivationSkill(const RandomActivationSkill&) = default;

    RandomActivationSkill& operator=(const RandomActivationSkill& other);
    bool IsValid();

    double rate = 0.0;
    const CSkillInfo* pSkillInfo = nullptr;
};


// Current Shared layout uses the four-word xvector carrier, not the donor's
// older three-word carrier. Both vectors own only their backing allocations.
// all CSkillInfo pointers are borrowed.
struct OptionSkillData {
    OptionSkillData() = default;
    OptionSkillData(const OptionSkillData& other);

    OptionSkillData& operator=(const OptionSkillData& other);

    RandomActivationSkill skills[3];
    std::vector<const CSkillInfo*> acquiredSkills;
    std::vector<const CSkillInfo*> effectSkills;
};


// Exact current ItemOption value. ItemOptionKey and CStats are main
// Shared values. OptionSkillData owns the two vector backings above.
class ItemOption {
public:
    explicit ItemOption(const ItemOptionKey& key);
    ItemOption(const ItemOption& other);
    ItemOption& operator=(const ItemOption& other);

    ItemOptionKey itemOptionKey;
    int intensiveItemClassID;
    CStats baseValue;
    OptionSkillData skillData;
};
