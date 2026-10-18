#pragma once

#include "Shared/src/ItemOption.h"
#include "Shared/src/Obj.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"

#include <cstddef>
#include <cstdio>

class CParserForOption;

class Option : public MemoryObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(Option)

    static void* operator new(std::size_t requestedSize);

    explicit Option(int optionNo);
    ~Option() override;

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    bool IsVariationValueUsable() const;
    void SetIsVariationItemEquipReusedelay(bool enabled);
    bool IsValidIsVariationItemEquipReuseDelay() const;

    int optionNo; // +0x18
    CStats baseValue; // +0x1c
    OptionSkillData skillData; // +0x40
    bool m_bIsVariationItemEquipReusedelay; // +0xb0
};


class OptionDB : public Singleton<OptionDB> {
public:
    OptionDB();
    ~OptionDB() override;

    Option* FindByOptionNo(int optionNo, bool logError);
    Option* FindByOptionName(const std::u16string& optionName, bool logError);

    void Load();

private:
    friend class CParserForOption;

    bool IsValidOptionData();
    void RegisterNoToOption(int optionNo, Option* option);
    void RegisterNameToOption(const std::u16string& optionName, Option* option);

    std::FILE* m_option_pch_file; // +0x08
    bool m_bOnlyPrecompiledHeader; // +0x10
    RWLock m_lock; // +0x18
    std::map<int, Option*> optionMap; // +0x50
    int m_nMaxOptionNo; // +0x70
    std::map<std::u16string, int> optionNameMap; // +0x78
};
