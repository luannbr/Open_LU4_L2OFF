#pragma once

#include "L2Core/src/inc/XStd.h"

#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"

#include <cstddef>

class CSkillInfo;

struct ArtifactEnchantInfo {
    int group_id;
    int count;
    int probability;
};


class CArtifactDB : public Singleton<CArtifactDB> {
public:
    CArtifactDB();
    ~CArtifactDB() override;

    void LoadData();
    void AddMaterialMinEnchantLevel(int enchant, int minimumEnchant);
    void AddMaterialItemInfo(int groupId, std::vector<int>& itemIds);
    void AddEnchantInfo(int itemId, int enchant, const ArtifactEnchantInfo& info);
    int GetMaterialMinEnchantLevel(int enchant);
    const std::vector<int>* GetMaterialItemInfo(int groupId);
    const std::vector<int>* GetMaterialItemInfo(int itemId, int enchant);
    const ArtifactEnchantInfo* GetEnchantInfo(int itemId, int enchant);
    void AddArtifactbookPageSkill(int page, const CSkillInfo* skillInfo);
    const std::vector<const CSkillInfo*>* GetArtifactbookPageSkills(int page);

private:
    std::map<int, int> m_MaterialMinEnchantLevelMap; // +0x08
    std::map<int, std::vector<int>> m_MaterialItemInfoMap; // +0x28
    std::map<std::CDualKey<int, int>, ArtifactEnchantInfo> m_EnchantInfoMap; // +0x48
    std::vector<const CSkillInfo*> m_ArtifactbookPageSkill[3]; // +0x68
};
