#pragma once

#include "Creature.h"

namespace CreatureCommonFunc {

void SaveUnEquipedItemAndSetNull(CCreature* creature, std::map<int, CItem*>& unequippedItems, SlotType slotType);

void DoNothingSkillActors(int& creatureId);

double GetDispelProbability(
    double baseProbability,
    const CSkillInfo* dispelSkill,
    const CSkillInfo* infectedSkill,
    const CCreatureSkillMod& skillMod,
    int targetLevel
);

void BroadcastValidateLocation(CCreature& creature, CAbstractCarrier& carrier, int packetCode);

bool IsNotDuplicated(CCreature* creature, std::vector<CSafePointer2<CCreature>>& creatures);

bool IsNotDuplicated(CCreature* creature, CCreature::SkillTargetData& targetData);

bool PushEffectsIfInTime(
    InfectedSkillType& infectedSkill,
    int tick,
    std::vector<CSkillEffect*>& effects,
    std::vector<unsigned int>& sourceIds,
    std::vector<const CSkillInfo*>& skills,
    int sourceId,
    int level,
    int abnormalTime
);

} // namespace CreatureCommonFunc
