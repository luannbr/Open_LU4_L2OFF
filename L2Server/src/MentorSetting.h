#pragma once

#include "L2Core/src/inc/XStd.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"

#include <cstddef>
#include <cstdint>
#include <utility>


// Exact current L2Server mentor-setting owner. The target's wchar_t is a
// two-byte UTF-16 code unit, represented by char16_t in the portable build.
class CMentorSetting : public ::Singleton<CMentorSetting> {
public:
    using SkillMap = ::std::map<std::int32_t, std::CDualKey<std::int32_t, std::int32_t>>;
    using LevelRewardMap = ::std::map<std::int32_t, std::pair<std::int32_t, std::int32_t>>;

    static constexpr std::size_t kTextCapacity = 0x100;

    CMentorSetting();
    ~CMentorSetting() override;

    void AddMentorSkill(std::int64_t packedSkill);
    void AddMenteeSkill(std::int64_t packedSkill);
    void AddMenteeSkill_withoutMentor(std::int64_t packedSkill);

    bool IsMentorBuff(std::int32_t skillId);
    bool IsMenteeBuff(std::int32_t skillId);

    std::int32_t GetLevUpRewardItem(std::int32_t level, bool academy);
    std::int32_t GetLevUpRewardItemCount(std::int32_t level, bool academy);

    // Both setters are compiled inline by the current setting parser.
    void SetLevUpReward(std::int32_t level, std::int32_t itemId, std::int32_t itemCount) {
        m_mapLevReward.emplace(level, std::make_pair(itemId, itemCount));
    }

    void SetLevUpRewardForAcademy(std::int32_t level, std::int32_t itemId, std::int32_t itemCount) {
        m_mapLevRewardForAcademy.emplace(level, std::make_pair(itemId, itemCount));
    }


    // Current parser and mentor consumers directly address these members.
    SkillMap m_MentorSkills;
    SkillMap m_MenteeSkills;
    SkillMap m_MenteeSkills_withoutMentor;
    std::int32_t m_nMenteeCondItemType;
    std::int64_t m_nMenteeCondItemCount;
    LevelRewardMap m_mapLevReward;
    LevelRewardMap m_mapLevRewardForAcademy;
    char16_t m_sLevUpTitle[kTextCapacity];
    char16_t m_sLevUpContents[kTextCapacity];
    std::int32_t m_nGraduateRewardItem;
    std::int64_t m_nGraduateRewardItemCount;
    char16_t m_sGraduateTitle[kTextCapacity];
    char16_t m_sGraduateContents[kTextCapacity];
    std::int32_t m_nJoinRewardItem;
    std::int64_t m_nJoinRewardItemCount;
    char16_t m_sJoinTitle[kTextCapacity];
    char16_t m_sJoinContents[kTextCapacity];
};
