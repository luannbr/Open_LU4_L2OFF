#include "MentorSetting.h"


#include <cstring>

namespace {

struct DecodedMentorSkill final {
    std::int32_t skillId;
    std::CDualKey<std::int32_t, std::int32_t> level;
};

DecodedMentorSkill DecodeMentorSkill(std::int64_t packedSkill) noexcept {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &packedSkill, sizeof(bits));

    const std::uint32_t skillIdBits = static_cast<std::uint32_t>(bits >> 32);
    std::int32_t skillId = 0;
    std::memcpy(&skillId, &skillIdBits, sizeof(skillId));

    return {skillId,
        std::CDualKey<std::int32_t, std::int32_t>(
            static_cast<std::int32_t>((bits >> 16) & 0xffff), static_cast<std::int32_t>(bits & 0xffff)
        )};
}

} // namespace

CMentorSetting::CMentorSetting() {
    L2_GUARD_FUNCTION();

    // The current constructor keeps these three extra clears after
    // all five map owners have already been constructed.
    m_MentorSkills.clear();
    m_nMenteeCondItemType = 0;
    m_nMenteeCondItemCount = 0;
    m_mapLevReward.clear();
    m_mapLevRewardForAcademy.clear();

    std::memset(m_sLevUpTitle, 0, sizeof(m_sLevUpTitle));
    std::memset(m_sLevUpContents, 0, sizeof(m_sLevUpContents));
    m_nGraduateRewardItem = 0;
    m_nGraduateRewardItemCount = 0;
    std::memset(m_sGraduateTitle, 0, sizeof(m_sGraduateTitle));
    std::memset(m_sGraduateContents, 0, sizeof(m_sGraduateContents));
    m_nJoinRewardItem = 0;
    m_nJoinRewardItemCount = 0;
    std::memset(m_sJoinTitle, 0, sizeof(m_sJoinTitle));
    std::memset(m_sJoinContents, 0, sizeof(m_sJoinContents));
}

CMentorSetting::~CMentorSetting() = default;

void CMentorSetting::AddMentorSkill(std::int64_t packedSkill) {
    L2_GUARD_FUNCTION();

    const DecodedMentorSkill skill = DecodeMentorSkill(packedSkill);
    m_MentorSkills.emplace(skill.skillId, skill.level);
}

void CMentorSetting::AddMenteeSkill(std::int64_t packedSkill) {
    L2_GUARD_FUNCTION();

    const DecodedMentorSkill skill = DecodeMentorSkill(packedSkill);
    m_MenteeSkills.emplace(skill.skillId, skill.level);
}

void CMentorSetting::AddMenteeSkill_withoutMentor(std::int64_t packedSkill) {
    L2_GUARD_FUNCTION();

    const DecodedMentorSkill skill = DecodeMentorSkill(packedSkill);
    m_MenteeSkills_withoutMentor.emplace(skill.skillId, skill.level);
}

bool CMentorSetting::IsMentorBuff(std::int32_t skillId) {
    L2_GUARD_FUNCTION();

    return m_MentorSkills.find(skillId) != m_MentorSkills.end();
}

bool CMentorSetting::IsMenteeBuff(std::int32_t skillId) {
    L2_GUARD_FUNCTION();

    if (m_MenteeSkills.find(skillId) != m_MenteeSkills.end()) {
        return true;
    }
    return m_MenteeSkills_withoutMentor.find(skillId) != m_MenteeSkills_withoutMentor.end();
}

std::int32_t CMentorSetting::GetLevUpRewardItem(std::int32_t level, bool academy) {
    L2_GUARD_FUNCTION();

    if (!academy) {
        const auto found = m_mapLevReward.find(level);
        if (found != m_mapLevReward.end()) {
            return found->second.first;
        }
        return 0;
    }

    const auto found = m_mapLevRewardForAcademy.find(level);
    if (found != m_mapLevRewardForAcademy.end()) {
        return found->second.first;
    }
    return 0;
}

std::int32_t CMentorSetting::GetLevUpRewardItemCount(std::int32_t level, bool academy) {
    L2_GUARD_FUNCTION();

    if (!academy) {
        const auto found = m_mapLevReward.find(level);
        if (found != m_mapLevReward.end()) {
            return found->second.second;
        }
        return 0;
    }

    const auto found = m_mapLevRewardForAcademy.find(level);
    if (found != m_mapLevRewardForAcademy.end()) {
        return found->second.second;
    }
    return 0;
}
