#include "Creature.h"
#include "MentorSetting.h"

bool InfectedSkillType::IsChannellingSkill() const {
    return m_pChannellingSkillInfo != nullptr &&
        (m_pChannellingSkillInfo->m_OperateType == SKILL_CHANNELLING_INSTANCE ||
            m_pChannellingSkillInfo->m_OperateType == SKILL_CHANNELLING_DURATION ||
            m_pChannellingSkillInfo->m_OperateType == SKILL_CHANNELLING_DURATION_LEVEL_UP);
}

bool InfectedSkillType::IsExcludeInfectSkill() const { return pSkillInfo->m_bExceptRestore; }

bool InfectedSkillType::IsMentorMenteeSkill() const {
    return CMentorSetting::inst().IsMentorBuff(pSkillInfo->m_nSkillId) ||
        CMentorSetting::inst().IsMenteeBuff(pSkillInfo->m_nSkillId);
}

bool InfectedSkillType::NeedSendVisualEffect() const { return !pSkillInfo->m_AbnormalVisualEffect.empty(); }
