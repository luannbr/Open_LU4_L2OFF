#pragma once

#include "L2Core/src/inc/XStd.h"
#include "PledgeInfo.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"

#include <cstddef>

struct XMLElement;

struct PledgeSkillInfo {
    PledgeSkillInfo();

    bool LoadFromXML(const XMLElement* element);

    std::u16string name_id;
    int skill_id;
    int skill_level;
    PledgeAddType::Value add_type;
    SocialClass::Type social_class;
    int expiry_time;
    PledgeCostInfo Cost;
    PledgeConditionInfo Condition;
};


class PledgeSkillIndexer {
public:
    using SkillKey = std::CDualKey<int, int>;
    using SkillMap = std::map<SkillKey, PledgeSkillInfo*>;

    const PledgeSkillInfo* Find(int skillId, int skillLevel) const;
    void Setup(const std::vector<PledgeSkillInfo*>& skills);

private:
    SkillMap m_RewardSkill;
};


class PledgeSkillDB : public ::Singleton<PledgeSkillDB> {
public:
    struct Builder {
        static void Reload(PledgeSkillDB* database);
    };

    PledgeSkillDB();
    ~PledgeSkillDB() override;

    const std::vector<PledgeSkillInfo*>& GetInfos() const;
    const PledgeSkillInfo* FindSkill(int skillId, int skillLevel) const;
    void Load();

private:
    void MakeRewardSkillIndex();

    std::vector<PledgeSkillInfo*> Skills;
    PledgeSkillIndexer m_RewardSkills;
};
