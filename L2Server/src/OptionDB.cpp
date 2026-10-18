#include "OptionDB.h"

#include "ParserOption.h"
#include "Shared/src/MemorySub.h"
#include "Skill.h"

#include "Shared/src/Log.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"
#include "Shared/src/platform/Runtime.h"

#include <cstdint>
#include <string>

namespace {

// std::wstring ToHostWide(const std::xwstring& source) {
//     std::wstring result
//     result.reserve(source.size())
//     for (const char16_t codeUnit : source) {
//         result.push_back(static_cast<wchar_t>(codeUnit))
//     }
//     return result
// }
} // namespace

// Portable registration: the runtime ID is no longer a shipping number.
// static_assert(Option::kMemoryClassId == L2ServerMemory::kOptionClassId)

L2_DEFINE_OBJECT_SINGLE_ALLOCATOR_LITERAL(Option)

Option::Option(int optionNumber) :
    optionNo(optionNumber), m_bIsVariationItemEquipReusedelay(false) {}

Option::~Option() = default;

L2_DEFINE_OBJECT_RELEASE(Option)

bool Option::IsVariationValueUsable() const {
    L2_GUARD_FUNCTION();
    int activeSkillCount = 0;
    for (const CSkillInfo* const skillInfo : skillData.acquiredSkills) {
        if (skillInfo != nullptr && skillInfo->IsActiveSkill()) {
            ++activeSkillCount;
        }
    }
    return activeSkillCount != 0;
}

void Option::SetIsVariationItemEquipReusedelay(bool enabled) {
    L2_GUARD_FUNCTION();
    m_bIsVariationItemEquipReusedelay = enabled;
    for (const CSkillInfo* const skillInfo : skillData.acquiredSkills) {
        if (skillInfo != nullptr && skillInfo->IsActiveSkill()) {
            const_cast<CSkillInfo*>(skillInfo)->m_bIsVariationItemEquipReusedelay = enabled;
        }
    }
}

bool Option::IsValidIsVariationItemEquipReuseDelay() const {
    L2_GUARD_FUNCTION();
    for (const CSkillInfo* const skillInfo : skillData.acquiredSkills) {
        if (skillInfo != nullptr && skillInfo->IsActiveSkill() &&
            m_bIsVariationItemEquipReusedelay != skillInfo->m_bIsVariationItemEquipReusedelay) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[INVALID_OPTION_DATA] optionNo(%d) active_skill={[%s]} : Undefined
            //     [is_variation_item_equip_reusedelay=1]", optionNo, skillInfo->m_pszName)
            // L2_LOG_ERROR(Log,
            //     L"[INVALID_OPTION_DATA] optionNo(%d) active_skill={[%s]} : Undefined
            //     [is_variation_item_equip_reusedelay=1]", optionNo, skillInfo->m_pszName)
            L2_LOG_ERROR(
                Log,
                u"Option %d and active skill '%ls' disagree on is_variation_item_equip_reusedelay (option %d, skill %d); "
                u"validation failed.",
                optionNo,
                skillInfo->m_pszName == nullptr ? u"<null>" : target_utf16::FromHostCodeUnits(skillInfo->m_pszName),
                static_cast<int>(m_bIsVariationItemEquipReusedelay),
                static_cast<int>(skillInfo->m_bIsVariationItemEquipReusedelay)
            );
            return false;
        }
    }
    return true;
}

OptionDB::OptionDB() :
    m_option_pch_file(nullptr), m_bOnlyPrecompiledHeader(false), m_nMaxOptionNo(0) {
    L2_WRITE_GUARD(m_lock);
    optionNameMap.clear();
    optionMap.clear();
    m_nMaxOptionNo = 0;
}

OptionDB::~OptionDB() { m_option_pch_file = nullptr; }

Option* OptionDB::FindByOptionNo(int optionNo, bool logError) {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(m_lock);

    if (optionNo > m_nMaxOptionNo) {
        if (logError) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] option no[%d] : invalid option number OR no option of this number exist",
            //     L"OptionDB.cpp",
            //     0x88,
            //     optionNo)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] option no[%d] : invalid option number OR no option of this number exist",
            //     L"OptionDB.cpp",
            //     0x88,
            //     optionNo)
            L2_LOG_ERROR(
                Log, "Option number %d exceeds the highest registered number %d; lookup failed.", optionNo, m_nMaxOptionNo
            );
        }
        return nullptr;
    }

    Option* option = nullptr;
    const auto found = optionMap.find(optionNo);
    if (found == optionMap.end()) {
        if (logError) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] option no[%d] : Invalid option date",
            //     L"OptionDB.cpp",
            //     0x99,
            //     optionNo)
            // Noise: the final null-result diagnostic below already reports this missing option once.
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] option no[%d] : Invalid option date",
            //     L"OptionDB.cpp",
            //     0x99,
            //     optionNo)
        }
    } else {
        option = found->second;
    }

    if (option == nullptr && logError) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] option no[%d] : invalid option number OR no option of this number exist",
        //     L"OptionDB.cpp",
        //     0xa3,
        //     optionNo)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] option no[%d] : invalid option number OR no option of this number exist",
        //     L"OptionDB.cpp",
        //     0xa3,
        //     optionNo)
        L2_LOG_ERROR(
            Log, "Cannot find option %d: %s.", optionNo,
            found == optionMap.end() ? "no entry is registered" : "the registered entry is null"
        );
    }
    return option;
}

Option* OptionDB::FindByOptionName(const std::u16string& optionName, bool logError) {
    L2_GUARD_FUNCTION();

    const auto found = optionNameMap.find(optionName);
    if (found != optionNameMap.end()) {
        return FindByOptionNo(found->second, logError);
    }

    if (logError) {
        const std::wstring hostName = Utf16ToWide(optionName);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid option name[%s] OR no option of this name exist",
        //     L"OptionDB.cpp",
        //     0x79,
        //     hostName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid option name[%s] OR no option of this name exist",
        //     L"OptionDB.cpp",
        //     0x79,
        //     hostName.c_str())
        L2_LOG_ERROR(Log, u"No option is registered for name '%ls'; lookup failed.", hostName.c_str());
    }
    return nullptr;
}

bool OptionDB::IsValidOptionData() {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(m_lock);
    for (const auto& entry : optionMap) {
        Option* const option = entry.second;
        if (option != nullptr && !option->IsValidIsVariationItemEquipReuseDelay()) {
            return false;
        }
    }
    return true;
}

void OptionDB::RegisterNoToOption(int optionNumber, Option* option) {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_lock);
    if (m_nMaxOptionNo < optionNumber) {
        m_nMaxOptionNo = optionNumber;
    }
    optionMap.emplace(optionNumber, option);
}

void OptionDB::RegisterNameToOption(const std::u16string& optionName, Option* option) {
    L2_GUARD_FUNCTION();
    optionNameMap.emplace(optionName, option->optionNo);
}

void OptionDB::Load() {
    L2_GUARD_FUNCTION();
    // Log.Add(LOG_NORMAL, "Loading OptionDB")
    // L2_LOG_INFO(Log, "Loading OptionDB")
    L2_LOG_INFO(Log, "Loading optiondata.txt...");

    const std::int64_t memoryBefore = g_Memory.GetTotalAllocSize();
    const std::uint32_t tickBefore = runtime::GetTickCount();
#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\optiondata.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/optiondata.txt";
#endif
    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from optiondata file failed",
        //     L"OptionDB.cpp",
        //     0xc9)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from optiondata file failed",
        //     L"OptionDB.cpp",
        //     0xc9)
        L2_LOG_ERROR(Log, "Cannot read optiondata.txt from the configured scripts directory; option loading stops.");
        return;
    }

    CParserForOption parser;
    parser.m_bOnlyPrecompiledHeader = m_bOnlyPrecompiledHeader;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] optionParser.create failed",
        //     L"OptionDB.cpp",
        //     0xd2)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] optionParser.create failed",
        //     L"OptionDB.cpp",
        //     0xd2)
        L2_LOG_ERROR(Log, "Cannot initialize the optiondata.txt parser; option loading stops.");
        return;
    }

    parser.m_lexer.yybegin(4);
    parser.m_lexer.yyin = nullptr;
    // if (parser.yyparse() != 0) {
    const int parseResult = parser.yyparse();
    if (parseResult != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing optiondata file failed after line[%d]",
        //     L"OptionDB.cpp",
        //     0xdd,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing optiondata file failed after line[%d]",
        //     L"OptionDB.cpp",
        //     0xdd,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "Cannot parse optiondata.txt at line %d; validation continues on any options already registered.",
            parser.m_lexer.line
        );
    }

    // if (!IsValidOptionData()) {
    const bool validOptions = IsValidOptionData();
    if (!validOptions) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing optiondata file failed - Undefined [is_variation_item_equip_reusedelay=1]",
        //     L"OptionDB.cpp",
        //     0xe4)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing optiondata file failed - Undefined [is_variation_item_equip_reusedelay=1]",
        //     L"OptionDB.cpp",
        //     0xe4)
        L2_LOG_ERROR(
            Log,
            "Option data validation failed: option and active-skill equip reuse-delay settings disagree. Registered options "
            "remain in memory."
        );
    }

    const std::uint32_t tickAfter = runtime::GetTickCount();
    const std::int64_t memoryAfter = g_Memory.GetTotalAllocSize();
    // Log.Add(
    //     LOG_NORMAL,
    //     "Complete to load OptionDB [%u MB, %ds]",
    //     static_cast<unsigned>(memoryAfter - memoryBefore) >> 20,
    //     static_cast<int>(tickAfter - tickBefore) / 1000)
    // L2_LOG_INFO(Log,
    //     "Complete to load OptionDB [%u MB, %ds]",
    //     static_cast<unsigned>(memoryAfter - memoryBefore) >> 20,
    //     static_cast<int>(tickAfter - tickBefore) / 1000)
    L2_LOG(
        Log, parseResult == 0 && validOptions ? LOG_NORMAL : LOG_ERROR,
        "%s %s; allocation change %lld bytes, elapsed %u ms.",
        m_bOnlyPrecompiledHeader ? "Option header generation" : "Option data loading",
        parseResult == 0 && validOptions ? "completed" : "finished with errors",
        static_cast<long long>(memoryAfter - memoryBefore), static_cast<unsigned>(tickAfter - tickBefore)
    );
}
