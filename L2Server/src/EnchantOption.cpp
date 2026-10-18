#include "Shared/src/Runtime/ResourceDirectories.h"

#include "EnchantOption.h"
#include "Shared/src/TextRuntime.h"

#include "Item.h"
#include "Object.h"
#include "ObjectDB.h"
#include "OptionDB.h"
#include "ParserEnchantOption.h"
#include "Skill.h"

#include "Shared/src/Log.h"
#include "Shared/src/SharedFactory.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <vector>

EnchantOptionId::EnchantOptionId() : at{} {}

EnchantSuccessInfo::EnchantSuccessInfo() : index(0), echantMaxLevel(0), successRate(nullptr) {}

EnchantSuccessInfo::~EnchantSuccessInfo() { Clear(); }

bool EnchantSuccessInfo::Init(int enchantMaxLevelValue) {
    if (enchantMaxLevelValue < 0 || enchantMaxLevelValue > 100) {
        return false;
    }

    if (enchantMaxLevelValue == 0) {
        Clear();
        return true;
    }

    Clear();
    echantMaxLevel = enchantMaxLevelValue;
    index = 0;
    successRate = static_cast<int*>(std::malloc(static_cast<std::size_t>(echantMaxLevel) * sizeof(int)));
    if (successRate != nullptr) {
        std::memset(successRate, 0xff, static_cast<std::size_t>(echantMaxLevel) * sizeof(int));
        return true;
    }

    Clear();
    return false;
}

void EnchantSuccessInfo::Clear() {
    if (echantMaxLevel != 0 && successRate != nullptr) {
        std::free(successRate);
    }

    echantMaxLevel = 0;
    index = 0;
    successRate = nullptr;
}

bool EnchantSuccessInfo::IsValid() {
    if (echantMaxLevel < 0 || echantMaxLevel >= 100) {
        return false;
    }
    if (echantMaxLevel == 0) {
        return true;
    }
    if (successRate == nullptr || index != echantMaxLevel) {
        return false;
    }

    for (int successRateIndex = 0; successRateIndex < echantMaxLevel; ++successRateIndex) {
        if (successRate[successRateIndex] == -1) {
            return false;
        }
    }
    return true;
}

bool EnchantSuccessInfo::AddSuccessRate(int successRateValue) {
    if (echantMaxLevel >= 100 || echantMaxLevel <= 0 || successRate == nullptr || index >= echantMaxLevel) {
        return false;
    }

    successRate[index] = successRateValue;
    ++index;
    return true;
}

CEnchantOption::CEnchantOption() {
    L2_GUARD_FUNCTION();
    for (Option*& option : m_OptionList) {
        option = nullptr;
    }
}

CEnchantOption::~CEnchantOption() = default;

bool CEnchantOption::AddOption(Option* option) {
    L2_GUARD_FUNCTION();
    for (Option*& optionSlot : m_OptionList) {
        if (optionSlot == nullptr) {
            optionSlot = option;
            return true;
        }
    }
    return false;
}

bool CEnchantOption::AddAcquiredSkill(const CSkillInfo* skillInfo) {
    L2_GUARD_FUNCTION();
    if (skillInfo == nullptr) {
        return false;
    }
    return m_OrgAcquiredSkillMap.emplace(skillInfo->m_nSkillId, skillInfo).second;
}

namespace DuplicatationChecker {

// Current L2Server.exe: 0x0070f870. The map is passed by
// value, so the successful insertion is discarded when this function exits.
void CheckRandomActivationSkill(
    int enchantLevel, const RandomActivationSkill& skill, std::map<int, RandomActivationSkill> destination
) {
    L2_GUARD_FUNCTION();
    const auto inserted = destination.emplace(skill.pSkillInfo->m_nSkillId, skill);
    if (!inserted.second) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Duplicated skills(%s) on loading enchant option(%d)",
        //     L"EnchantOption.cpp",
        //     0xd7,
        //     skill.pSkillInfo->m_pszName,
        //     enchantLevel)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Duplicated skills(%s) on loading enchant option(%d)",
        //     L"EnchantOption.cpp",
        //     0xd7,
        //     skill.pSkillInfo->m_pszName,
        //     enchantLevel)
        L2_LOG_ERROR(
            Log, "Duplicate activation skill %d ('%s') at enchant level %d; the checked map already contains this skill ID.",
            skill.pSkillInfo->m_nSkillId,
            skill.pSkillInfo->m_pszName == nullptr
                ? "<null>"
                : Utf16ToUtf8(target_utf16::FromHostCodeUnits(skill.pSkillInfo->m_pszName)).c_str(),
            enchantLevel
        );
    }
}

// Current L2Server.exe: 0x0070f000.
void CheckSkillInfo(
    int enchantLevel, const std::vector<const CSkillInfo*>& source, std::map<int, const CSkillInfo*>& destination
) {
    L2_GUARD_FUNCTION();
    for (const CSkillInfo* const skillInfo : source) {
        const auto inserted = destination.emplace(skillInfo->m_nSkillId, skillInfo);
        if (!inserted.second) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Duplicated skills(%s) on loading enchant option(%d)",
            //     L"EnchantOption.cpp",
            //     0xeb,
            //     skillInfo->m_pszName,
            //     enchantLevel)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Duplicated skills(%s) on loading enchant option(%d)",
            //     L"EnchantOption.cpp",
            //     0xeb,
            //     skillInfo->m_pszName,
            //     enchantLevel)
            L2_LOG_ERROR(
                Log, "Duplicate skill %d ('%s') at enchant level %d; the earlier entry is retained.", skillInfo->m_nSkillId,
                skillInfo->m_pszName == nullptr ? "<null>"
                                                : Utf16ToUtf8(target_utf16::FromHostCodeUnits(skillInfo->m_pszName)).c_str(),
                enchantLevel
            );
        }
    }
}

void Map2List(const std::map<int, RandomActivationSkill>& source, std::vector<RandomActivationSkill>& destination) {
    L2_GUARD_FUNCTION();
    for (const auto& entry : source) {
        destination.push_back(entry.second);
    }
}

void Map2List(const std::map<int, const CSkillInfo*>& source, std::vector<const CSkillInfo*>& destination) {
    L2_GUARD_FUNCTION();
    for (const auto& entry : source) {
        destination.push_back(entry.second);
    }
}

} // namespace DuplicatationChecker

void CEnchantOption::PostLoad(int enchantLevel) {
    L2_GUARD_FUNCTION();

    std::array<std::map<int, RandomActivationSkill>, 3> activationSkillMaps;
    std::map<int, const CSkillInfo*> effectSkillMap;

    for (std::size_t optionIndex = 0; optionIndex < std::size(m_OptionList); ++optionIndex) {
        Option* const option = m_OptionList[optionIndex];
        if (option == nullptr) {
            continue;
        }

        m_Stats += option->baseValue;
        for (std::size_t activationType = 0; activationType < activationSkillMaps.size(); ++activationType) {
            RandomActivationSkill& activation = option->skillData.skills[activationType];
            if (activation.IsValid()) {
                DuplicatationChecker::CheckRandomActivationSkill(
                    enchantLevel, activation, activationSkillMaps[activationType]
                );
            }
        }

        DuplicatationChecker::CheckSkillInfo(enchantLevel, option->skillData.acquiredSkills, m_OrgAcquiredSkillMap);
        DuplicatationChecker::CheckSkillInfo(enchantLevel, option->skillData.effectSkills, effectSkillMap);
        m_OptionId.at[optionIndex] = option->optionNo;
    }

    for (std::size_t activationType = 0; activationType < activationSkillMaps.size(); ++activationType) {
        DuplicatationChecker::Map2List(activationSkillMaps[activationType], m_ActivationSkills[activationType]);
    }
    DuplicatationChecker::Map2List(m_OrgAcquiredSkillMap, m_AcquiredSkills);
    m_OrgAcquiredSkillMap.clear();
    DuplicatationChecker::Map2List(effectSkillMap, m_EffectSkills);
}

CItemEnchantOption::CItemEnchantOption() : m_bDeleted(false) {}

CItemEnchantOption::~CItemEnchantOption() {
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_EnchantOptionMap) {
        delete entry.second;
    }
    m_bDeleted = true;
    m_EnchantOptionMap.clear();
}

bool CItemEnchantOption::IsValid() {
    return m_EnchantSuccessInfo.IsValid() &&
        (m_EnchantSuccessInfo.echantMaxLevel == 0 ||
            static_cast<std::size_t>(m_EnchantSuccessInfo.echantMaxLevel + 1) == m_EnchantOptionMap.size());
}

const CEnchantOption* CItemEnchantOption::GetEnchantOption(int enchantLevel) {
    L2_GUARD_FUNCTION();

    auto position = m_EnchantOptionMap.lower_bound(enchantLevel);
    if (position != m_EnchantOptionMap.end() && position->first == enchantLevel) {
        return position->second;
    }
    if (position == m_EnchantOptionMap.begin()) {
        return nullptr;
    }
    --position;
    return position->second;
}

bool CItemEnchantOption::AddEnchantOption(int enchantLevel, CEnchantOption* enchantOption) {
    L2_GUARD_FUNCTION();
    const bool inserted = m_EnchantOptionMap.emplace(enchantLevel, enchantOption).second;
    if (inserted) {
        enchantOption->PostLoad(enchantLevel);
    }
    return inserted;
}

CEnchantOptionDB::~CEnchantOptionDB() {
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_ItemEnchantOptionMap) {
        CItemEnchantOption* const itemEnchantOption = entry.second;
        if (itemEnchantOption != nullptr && !itemEnchantOption->m_bDeleted) {
            delete itemEnchantOption;
        }
    }
    m_ItemEnchantOptionMap.clear();
}

const CEnchantOption* CEnchantOptionDB::GetEnchantOption(int itemClassId, int enchantLevel) {
    L2_GUARD_FUNCTION();
    const auto found = m_ItemEnchantOptionMap.find(itemClassId);
    return found == m_ItemEnchantOptionMap.end() ? nullptr : found->second->GetEnchantOption(enchantLevel);
}

int CEnchantOptionDB::GetMaxEnchantLevel(int itemClassId) {
    const auto found = m_ItemEnchantOptionMap.find(itemClassId);
    return found == m_ItemEnchantOptionMap.end() ? -1 : found->second->m_EnchantSuccessInfo.echantMaxLevel;
}

int CEnchantOptionDB::GetEnchantSuccessRate(int itemClassId, int enchantLevel) {
    if (enchantLevel < 0) {
        return 0;
    }
    const auto found = m_ItemEnchantOptionMap.find(itemClassId);
    if (found == m_ItemEnchantOptionMap.end() || enchantLevel >= found->second->m_EnchantSuccessInfo.echantMaxLevel) {
        return -1;
    }
    return found->second->m_EnchantSuccessInfo.successRate[enchantLevel];
}

void CEnchantOptionDB::SetEnchantOption(CItem& item) {
    L2_GUARD_FUNCTION();
    item.m_pEnchantOption =
        const_cast<CEnchantOption*>(GetEnchantOption(item.m_data->m_nClassID, item.m_data->GetEnchanted()));
}

bool CEnchantOptionDB::AddItemEnchantOption(int itemClassId, CItemEnchantOption* itemEnchantOption) {
    L2_GUARD_FUNCTION();
    const bool inserted = m_ItemEnchantOptionMap.emplace(itemClassId, itemEnchantOption).second;
    if (!inserted) {
        return false;
    }

    CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
    if (object != nullptr) {
        CItem* const item = object->CastItem();
        if (item != nullptr) {
            SetEnchantOption(*item);
        }
    }
    return true;
}

void CEnchantOptionDB::Load() {
    L2_GUARD_FUNCTION();

    std::ifstream input(runtime::ResolveResourcePath("../Script/enchantOption.txt"), std::ios::binary);
    if (!input) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from enchantOption file failed",
        //     L"EnchantOption.cpp",
        //     0x1b7)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from enchantOption file failed",
        //     L"EnchantOption.cpp",
        //     0x1b7)
        L2_LOG_ERROR(Log, "Cannot open enchantOption.txt; enchant-option loading stops.");
        return;
    }

    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if ((bytes.size() & 1U) != 0U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from enchantOption file failed",
        //     L"EnchantOption.cpp",
        //     0x1b7)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from enchantOption file failed",
        //     L"EnchantOption.cpp",
        //     0x1b7)
        L2_LOG_ERROR(
            Log, "Cannot decode enchantOption.txt: odd byte count %zu is not valid UTF-16 input; loading stops.",
            static_cast<std::size_t>(bytes.size())
        );
        return;
    }

    std::vector<char16_t> fileBuffer;
    fileBuffer.reserve(bytes.size() / 2U);
    for (std::size_t offset = 0; offset < bytes.size(); offset += 2U) {
        fileBuffer.push_back(
            static_cast<char16_t>(
                static_cast<std::uint16_t>(bytes[offset]) | (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U)
            )
        );
    }

    const std::size_t firstCodeUnit = !fileBuffer.empty() && fileBuffer.front() == u'\ufeff' ? 1U : 0U;
    CParserForEnchantOption parser;
    if (!parser.Create(fileBuffer.data() + firstCodeUnit, static_cast<int>(fileBuffer.size() - firstCodeUnit))) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] EnchantOptionParser.create failed",
        //     L"EnchantOption.cpp",
        //     0x1c0)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] EnchantOptionParser.create failed",
        //     L"EnchantOption.cpp",
        //     0x1c0)
        L2_LOG_ERROR(Log, "Cannot initialize the enchantOption.txt parser; loading stops.");
        return;
    }

    // Current Load@007102d1 selects the script lexer state before parsing.
    parser.m_lexer.yybegin(4);
    parser.m_lexer.yyin = nullptr;
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing enchantOption file failed after line[%d]",
        //     L"EnchantOption.cpp",
        //     0x1cd,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing enchantOption file failed after line[%d]",
        //     L"EnchantOption.cpp",
        //     0x1cd,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "Cannot parse enchantOption.txt at script line %lld; already inserted enchant options are retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
}
