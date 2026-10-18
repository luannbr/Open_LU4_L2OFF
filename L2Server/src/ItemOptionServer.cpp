#include "ItemOptionServer.h"
#include "Creature.h"
#include "EnchantOption.h"
#include "Item.h"
#include "NewAction.h"
#include "Packet.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Skill.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/ItemOption.h"
#include "Shared/src/Utils.h"

void ItemSkillHelper::GetSkillInfo(
    const std::vector<const CSkillInfo*>& skillInfos, std::map<int, const CSkillInfo*>& skills
) {
    L2_GUARD_FUNCTION();
    for (const CSkillInfo* const skill : skillInfos) {
        if (skill != nullptr) {
            skills[skill->m_nSkillId] = skill;
        }
    }
}

void ItemSkillHelper::GetAcquiredSkillsFromEnchantOption(CItem& item, std::map<int, const CSkillInfo*>& skills) {
    L2_GUARD_FUNCTION();
    if (item.m_pEnchantOption != nullptr) {
        GetSkillInfo(item.m_pEnchantOption->GetAcquiredSkills(), skills);
    }
}

void ItemSkillHelper::GetEffectSkillsFromEnchantOption(CItem& item, std::map<int, const CSkillInfo*>& skills) {
    L2_GUARD_FUNCTION();
    if (item.m_pEnchantOption != nullptr) {
        GetSkillInfo(item.m_pEnchantOption->GetEffectSkills(), skills);
    }
}

void ItemSkillHelper::ActivatOptionSkills(const RandomActivationSkill& skill, CCreature& actor, CWorldObject& target) {
    // Current L2Server 0x007f429c. The misspelling is the original symbol.
    L2_GUARD_FUNCTION();

    if (skill.pSkillInfo == nullptr || ::genrand64_real1() * 100.0 >= skill.rate) {
        return;
    }

    const SkillOperateType operateType = skill.pSkillInfo->m_OperateType;
    if (operateType == SKILL_SELFDURATION || operateType == SKILL_CHANNELLING_DURATION_LEVEL_UP ||
        operateType == SKILL_CHARGE_INSTANT || operateType == SKILL_BACK_STEP_DURATION) {
        return;
    }

    User* const user = actor.CastUser();
    if (user != nullptr && user->GetSocket() != nullptr) {
        user->GetSocket()->Send(
            "chcccdd", 0x62, SystemMessageId::NAMED_EFFECT_ACTIVATED, 1, EST_SKILLID, skill.pSkillInfo->m_nSkillId,
            (skill.pSkillInfo->m_nSubLevel << 16) | static_cast<std::uint16_t>(skill.pSkillInfo->m_nLevel)
        );
    }

    skill.pSkillInfo->ActivateSkill(&actor, &target, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
    skill.pSkillInfo->CheckAndTurnOffSoulSpiritShot(actor);
}

void ItemOptionHandler::GetAcquiredSkills(CItem& item, std::map<int, const CSkillInfo*>& skills) {
    L2_GUARD_FUNCTION();
    if (item.m_itemOption.itemOptionKey.variationOption1 > 0 && item.m_itemOption.itemOptionKey.variationOption2 > 0) {
        ItemSkillHelper::GetSkillInfo(item.m_itemOption.skillData.acquiredSkills, skills);
    }
    ItemSkillHelper::GetAcquiredSkillsFromEnchantOption(item, skills);
}

void ItemOptionHandler::GetOptionSkills(CItem& item, std::map<int, const CSkillInfo*>& skills) {
    L2_GUARD_FUNCTION();
    if (item.m_itemOption.itemOptionKey.variationOption1 > 0 && item.m_itemOption.itemOptionKey.variationOption2 > 0) {
        ItemSkillHelper::GetSkillInfo(item.m_itemOption.skillData.effectSkills, skills);
    }
    ItemSkillHelper::GetEffectSkillsFromEnchantOption(item, skills);
}

void ItemOptionHandler::GetEnchantOptionSkills(
    CItem& item, std::map<int, const CSkillInfo*>& acquiredSkills, std::map<int, const CSkillInfo*>& effectSkills
) {
    L2_GUARD_FUNCTION();
    ItemSkillHelper::GetAcquiredSkillsFromEnchantOption(item, acquiredSkills);
    ItemSkillHelper::GetEffectSkillsFromEnchantOption(item, effectSkills);
}

void ItemOptionHandler::ActivateMagicSkills(
    ActivateSkillInfo& info, CSkillAction2& action, const CSkillInfo::EffectActivateTiming timing
) {
    // Current L2Server 0x007f454c.
    L2_GUARD_FUNCTION();

    if (info.item->m_itemOption.itemOptionKey.variationOption1 > 0 &&
        info.item->m_itemOption.itemOptionKey.variationOption2 > 0) {
        const RandomActivationSkill& skill = info.item->m_itemOption.skillData.skills[RAST_MAGIC];
        action.ActivateAttachedItemSkill(
            *info.actor, *info.target, skill.pSkillInfo, static_cast<float>(skill.rate), timing
        );
    }

    if (info.item->m_pEnchantOption == nullptr) {
        return;
    }
    for (const RandomActivationSkill& skill : info.item->m_pEnchantOption->GetActivationSkills(RAST_MAGIC)) {
        action.ActivateAttachedItemSkill(
            *info.actor, *info.target, skill.pSkillInfo, static_cast<float>(skill.rate), timing
        );
    }
}

void ItemOptionHandler::ActivateSkills(const RandomActivationSkillType skillType, ActivateSkillInfo& info) {
    // Current L2Server 0x007f4448.
    L2_GUARD_FUNCTION();

    if (info.item->m_itemOption.itemOptionKey.variationOption1 > 0 &&
        info.item->m_itemOption.itemOptionKey.variationOption2 > 0) {
        ItemSkillHelper::ActivatOptionSkills(info.item->m_itemOption.skillData.skills[skillType], *info.actor, *info.target);
    }

    if (info.item->m_pEnchantOption == nullptr) {
        return;
    }
    for (const RandomActivationSkill& skill : info.item->m_pEnchantOption->GetActivationSkills(skillType)) {
        ItemSkillHelper::ActivatOptionSkills(skill, *info.actor, *info.target);
    }
}

void ItemOptionHandler::ActivateAttackSkills(ActivateSkillInfo& info) {
    // Current L2Server 0x007f4680.
    L2_GUARD_FUNCTION();
    ActivateSkills(RAST_ATTACK, info);
}

void ItemOptionHandler::ActivateCriticalSkills(ActivateSkillInfo& info) {
    // Current L2Server 0x007f4724.
    L2_GUARD_FUNCTION();
    ActivateSkills(RAST_CRITICAL, info);
}

RandomActivationSkill& RandomActivationSkill::operator=(const RandomActivationSkill& other) {
    L2_GUARD_FUNCTION();
    rate = other.rate;
    pSkillInfo = other.pSkillInfo;
    return *this;
}

bool RandomActivationSkill::IsValid() {
    L2_GUARD_FUNCTION();
    return pSkillInfo != nullptr;
}

OptionSkillData::OptionSkillData(const OptionSkillData& other) :
    skills{other.skills[0], other.skills[1], other.skills[2]},
    acquiredSkills(other.acquiredSkills),
    effectSkills(other.effectSkills) {}

OptionSkillData& OptionSkillData::operator=(const OptionSkillData& other) {
    L2_GUARD_FUNCTION();
    for (std::size_t index = 0; index < 3; ++index) {
        skills[index] = other.skills[index];
    }
    acquiredSkills = other.acquiredSkills;
    effectSkills = other.effectSkills;
    return *this;
}

ItemOption::ItemOption(const ItemOptionKey& key) : itemOptionKey(key), intensiveItemClassID(0) {}

ItemOption::ItemOption(const ItemOption& other) :
    itemOptionKey(other.itemOptionKey),
    intensiveItemClassID(other.intensiveItemClassID),
    baseValue(other.baseValue),
    skillData(other.skillData) {}

ItemOption& ItemOption::operator=(const ItemOption& other) {
    if (this != &other) {
        itemOptionKey = other.itemOptionKey;
        baseValue = other.baseValue;
        skillData = other.skillData;
        intensiveItemClassID = other.intensiveItemClassID;
    }
    return *this;
}
