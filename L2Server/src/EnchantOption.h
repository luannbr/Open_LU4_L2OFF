#pragma once

#include "ItemOptionServer.h"

#include "Shared/src/ItemOption.h"
#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

class CSkillInfo;
class CItem;
class Option;
class CParserForEnchantOption;

struct EnchantOptionId {
    EnchantOptionId();

    int at[3];
};


class EnchantSuccessInfo {
public:
    EnchantSuccessInfo();
    ~EnchantSuccessInfo();

    bool Init(int enchantMaxLevel);
    void Clear();
    bool IsValid();
    bool AddSuccessRate(int successRate);

private:
    friend class CItemEnchantOption;
    friend class CEnchantOptionDB;
    friend class CParserForEnchantOption;

    int index; // +0x00
    int echantMaxLevel; // +0x04. Spelling kept from the same-source type donor.
    int* successRate; // +0x08
};


class CEnchantOption {
public:
    CEnchantOption();
    ~CEnchantOption();

    bool AddOption(Option* option);
    bool AddAcquiredSkill(const CSkillInfo* skillInfo);
    void PostLoad(int enchantLevel);

    const CStats& GetStatBonus() const { return m_Stats; }

    const std::vector<const CSkillInfo*>& GetAcquiredSkills() const { return m_AcquiredSkills; }

    const std::vector<const CSkillInfo*>& GetEffectSkills() const { return m_EffectSkills; }

    const std::vector<RandomActivationSkill>& GetActivationSkills(RandomActivationSkillType skillType) const {
        return m_ActivationSkills[static_cast<std::size_t>(skillType)];
    }

    const EnchantOptionId& GetOptionId() const { return m_OptionId; }

private:
    Option* m_OptionList[3]; // +0x00
    CStats m_Stats; // +0x18
    std::vector<RandomActivationSkill> m_ActivationSkills[3]; // +0x38
    std::vector<const CSkillInfo*> m_AcquiredSkills; // +0x98
    std::vector<const CSkillInfo*> m_EffectSkills; // +0xb8
    std::map<int, const CSkillInfo*> m_OrgAcquiredSkillMap; // +0xd8
    EnchantOptionId m_OptionId; // +0xf8
};


class CItemEnchantOption {
public:
    CItemEnchantOption();
    ~CItemEnchantOption();

    bool IsValid();
    const CEnchantOption* GetEnchantOption(int enchantLevel);
    bool AddEnchantOption(int enchantLevel, CEnchantOption* enchantOption);

private:
    friend class CParserForEnchantOption;

    std::map<int, CEnchantOption*> m_EnchantOptionMap; // +0x00

public:
    bool m_bDeleted; // +0x20
    EnchantSuccessInfo m_EnchantSuccessInfo; // +0x28
};


class CEnchantOptionDB {
public:
    CEnchantOptionDB() = default;
    ~CEnchantOptionDB();

    static CEnchantOptionDB& Inst() {
        static CEnchantOptionDB m_Inst;
        return m_Inst;
    }

    void Load();
    const CEnchantOption* GetEnchantOption(int itemClassId, int enchantLevel);
    int GetMaxEnchantLevel(int itemClassId);
    int GetEnchantSuccessRate(int itemClassId, int enchantLevel);
    void SetEnchantOption(CItem& item);
    bool AddItemEnchantOption(int itemClassId, CItemEnchantOption* itemEnchantOption);

private:
    std::map<int, CItemEnchantOption*> m_ItemEnchantOptionMap;
};
