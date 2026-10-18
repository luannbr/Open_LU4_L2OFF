#pragma once

#include "Skill.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

enum RandomActivationSkillType : std::int32_t {
    RAST_MIN = 0,
    RAST_ATTACK = 0,
    RAST_CRITICAL = 1,
    RAST_MAGIC = 2,
    RAST_MAX = 3,
};


class CCreature;
class CItem;
class CSkillAction2;
class CWorldObject;
struct RandomActivationSkill;

namespace ItemSkillHelper {

void GetSkillInfo(const std::vector<const CSkillInfo*>& skillInfos, std::map<int, const CSkillInfo*>& skills);
void GetAcquiredSkillsFromEnchantOption(CItem& item, std::map<int, const CSkillInfo*>& skills);
void GetEffectSkillsFromEnchantOption(CItem& item, std::map<int, const CSkillInfo*>& skills);
void ActivatOptionSkills(const RandomActivationSkill& skill, CCreature& actor, CWorldObject& target);

} // namespace ItemSkillHelper

namespace ItemOptionHandler {

void GetAcquiredSkills(CItem& item, std::map<int, const CSkillInfo*>& skills);
void GetOptionSkills(CItem& item, std::map<int, const CSkillInfo*>& skills);
void GetEnchantOptionSkills(
    CItem& item, std::map<int, const CSkillInfo*>& acquiredSkills, std::map<int, const CSkillInfo*>& effectSkills
);

struct ActivateSkillInfo {
    ActivateSkillInfo(CItem* item, CCreature* actor, CWorldObject* target) : item(item), actor(actor), target(target) {}

    CItem* item;
    CCreature* actor;
    CWorldObject* target;
};

static_assert(std::is_trivially_copyable_v<ActivateSkillInfo>);
static_assert(std::is_trivially_destructible_v<ActivateSkillInfo>);
static_assert(!std::is_default_constructible_v<ActivateSkillInfo>);

void ActivateMagicSkills(ActivateSkillInfo& info, CSkillAction2& action, CSkillInfo::EffectActivateTiming timing);
void ActivateSkills(RandomActivationSkillType skillType, ActivateSkillInfo& info);
void ActivateAttackSkills(ActivateSkillInfo& info);
void ActivateCriticalSkills(ActivateSkillInfo& info);

} // namespace ItemOptionHandler
