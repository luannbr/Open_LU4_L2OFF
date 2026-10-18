#include "EnsoulData.h"
#include "Shared/src/Runtime/ResourceDirectories.h"

#include "EnsoulParser.h"

#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"
#include "Skill.h"

#include <cerrno>
#include <cstdio>
#include <cwchar>

EnsoulManager g_EnsoulManager;
EnsoulOptionDataMap g_mEnsoulOptionData;
EnsoulStoneDataMap g_mEnsoulStoneData;
EnsoulFeeDataMap g_EnsoulFeeData;

void EnsoulManager::Load(bool onlyPrecompiledHeader) {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kEnsoulPath = L"..\\Script\\ensouldata.txt";
    constexpr const wchar_t* kEnsoulPchPath = L"..\\Script\\ensouldata_pch.txt";
#else
    constexpr const wchar_t* kEnsoulPath = L"../script/ensouldata.txt";
    constexpr const char* kEnsoulPchPath = "../script/ensouldata_pch.txt";
#endif

    CSecureStream input(kEnsoulPath, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from EnsoulData file failed",
        //     L"EnsoulData.cpp",
        //     100)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from EnsoulData file failed",
        //     L"EnsoulData.cpp",
        //     100)
        L2_LOG_ERROR(Log, "Cannot read ensouldata.txt; ensoul-data loading stops.");
        return;
    }

    CParserForEnsoul parser;
    parser.m_bOnlyPrecompiledHeader = onlyPrecompiledHeader;

    if (onlyPrecompiledHeader) {
#if defined(_WIN32)
        const int openError =
            ::_wfopen_s(&PCHFile, runtime::ResolveResourcePath(kEnsoulPchPath).wstring().c_str(), L"wb");
#else
        PCHFile = std::fopen(runtime::ResolveResourcePath(kEnsoulPchPath).string().c_str(), "wb");
        const int openError = PCHFile == nullptr ? errno : 0;
#endif
        if (openError != 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Can't open %s, errno[%d]",
            //     L"..\\Script\\ensouldata_pch.txt",
            //     openError)
            // L2_LOG_ERROR(Log,
            //     L"Can't open %s, errno[%d]",
            //     L"..\\Script\\ensouldata_pch.txt",
            //     openError)
            L2_LOG_ERROR(
                Log, "Cannot open ensouldata_pch.txt for writing (error %d); PCH generation has no output file.", openError
            );
        }
        std::fputc(0xff, PCHFile);
        std::fputc(0xfe, PCHFile);
    }

    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForEnsoul::create failed",
        //     L"EnsoulData.cpp",
        //     124)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForEnsoul::create failed",
        //     L"EnsoulData.cpp",
        //     124)
        L2_LOG_ERROR(Log, "Cannot initialize the ensouldata.txt parser; loading stops.");
        return;
    }

    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing EnsoulData file failed after line[%d]",
        //     L"EnsoulData.cpp",
        //     131,
        //     parser.m_lexer.GetLine())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing EnsoulData file failed after line[%d]",
        //     L"EnsoulData.cpp",
        //     131,
        //     parser.m_lexer.GetLine())
        L2_LOG_ERROR(
            Log, "Cannot parse ensouldata.txt at script line %lld; already processed data is not rolled back.",
            static_cast<long long>(parser.m_lexer.GetLine()) + 1
        );
    }

    if (onlyPrecompiledHeader) {
        std::fclose(PCHFile);
    }
}

void EnsoulManager::Uniputs(char16_t* text) {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    std::fputws(reinterpret_cast<const wchar_t*>(text), PCHFile);
#else
    while (*text != u'\0') {
        const std::uint16_t codeUnit = *text++;
        std::fputc(codeUnit & 0xff, PCHFile);
        std::fputc(codeUnit >> 8, PCHFile);
    }
#endif
}

void EnsoulOptionData::SetEnsoulOptionSkills(std::vector<std::int64_t, std::allocator<std::int64_t>>& skills) {
    for (int index = 0; static_cast<std::size_t>(index) < skills.size(); ++index) {
        const std::int64_t encodedSkill = runtime::CheckedVectorAt(skills, index);
        const CSkillKey skillKey(
            static_cast<int>(encodedSkill >> 32), static_cast<int>(static_cast<std::uint16_t>(encodedSkill >> 16)),
            static_cast<int>(static_cast<std::uint16_t>(encodedSkill))
        );
        const CSkillInfo* const skillInfo = g_SkillDB.GetSkillInfo(skillKey, nullptr);
        if (skillInfo) {
            m_vSkillList.push_back(skillInfo);
        }
    }
}

EnsoulItem::EnsoulOptionType EnsoulOptionData::GetEnsoulOptionType(const wchar_t* name) {
    L2_GUARD_FUNCTION();

    if (std::wcscmp(name, L"phy_1") == 0) {
        return EnsoulItem::EOT_PHY1;
    }
    if (std::wcscmp(name, L"phy_2") == 0) {
        return EnsoulItem::EOT_PHY2;
    }
    if (std::wcscmp(name, L"phy_3") == 0) {
        return EnsoulItem::EOT_PHY3;
    }
    if (std::wcscmp(name, L"phy_4") == 0) {
        return EnsoulItem::EOT_PHY4;
    }
    if (std::wcscmp(name, L"phy_5") == 0) {
        return EnsoulItem::EOT_PHY5;
    }
    if (std::wcscmp(name, L"phy_6") == 0) {
        return EnsoulItem::EOT_PHY6;
    }
    if (std::wcscmp(name, L"phy_7") == 0) {
        return EnsoulItem::EOT_PHY7;
    }
    if (std::wcscmp(name, L"phy_8") == 0) {
        return EnsoulItem::EOT_PHY8;
    }
    if (std::wcscmp(name, L"phy_9") == 0) {
        return EnsoulItem::EOT_PHY9;
    }
    if (std::wcscmp(name, L"mag_1") == 0) {
        return EnsoulItem::EOT_MAG1;
    }
    if (std::wcscmp(name, L"mag_2") == 0) {
        return EnsoulItem::EOT_MAG2;
    }
    if (std::wcscmp(name, L"mag_3") == 0) {
        return EnsoulItem::EOT_MAG3;
    }
    if (std::wcscmp(name, L"mag_4") == 0) {
        return EnsoulItem::EOT_MAG4;
    }
    if (std::wcscmp(name, L"mag_5") == 0) {
        return EnsoulItem::EOT_MAG5;
    }
    if (std::wcscmp(name, L"mag_6") == 0) {
        return EnsoulItem::EOT_MAG6;
    }
    if (std::wcscmp(name, L"mag_7") == 0) {
        return EnsoulItem::EOT_MAG7;
    }
    if (std::wcscmp(name, L"mag_8") == 0) {
        return EnsoulItem::EOT_MAG8;
    }
    if (std::wcscmp(name, L"mag_9") == 0) {
        return EnsoulItem::EOT_MAG9;
    }
    if (std::wcscmp(name, L"pphy_1") == 0) {
        return EnsoulItem::EOT_PPHY1;
    }
    if (std::wcscmp(name, L"pphy_2") == 0) {
        return EnsoulItem::EOT_PPHY2;
    }
    if (std::wcscmp(name, L"pphy_3") == 0) {
        return EnsoulItem::EOT_PPHY3;
    }
    if (std::wcscmp(name, L"pphy_4") == 0) {
        return EnsoulItem::EOT_PPHY4;
    }
    if (std::wcscmp(name, L"pphy_5") == 0) {
        return EnsoulItem::EOT_PPHY5;
    }
    if (std::wcscmp(name, L"pphy_6") == 0) {
        return EnsoulItem::EOT_PPHY6;
    }
    if (std::wcscmp(name, L"pphy_7") == 0) {
        return EnsoulItem::EOT_PPHY7;
    }
    if (std::wcscmp(name, L"pphy_8") == 0) {
        return EnsoulItem::EOT_PPHY8;
    }
    if (std::wcscmp(name, L"pphy_9") == 0) {
        return EnsoulItem::EOT_PPHY9;
    }
    if (std::wcscmp(name, L"mmag_1") == 0) {
        return EnsoulItem::EOT_MMAG1;
    }
    if (std::wcscmp(name, L"mmag_2") == 0) {
        return EnsoulItem::EOT_MMAG2;
    }
    if (std::wcscmp(name, L"mmag_3") == 0) {
        return EnsoulItem::EOT_MMAG3;
    }
    if (std::wcscmp(name, L"mmag_4") == 0) {
        return EnsoulItem::EOT_MMAG4;
    }
    if (std::wcscmp(name, L"mmag_5") == 0) {
        return EnsoulItem::EOT_MMAG5;
    }
    if (std::wcscmp(name, L"mmag_6") == 0) {
        return EnsoulItem::EOT_MMAG6;
    }
    if (std::wcscmp(name, L"mmag_7") == 0) {
        return EnsoulItem::EOT_MMAG7;
    }
    if (std::wcscmp(name, L"mmag_8") == 0) {
        return EnsoulItem::EOT_MMAG8;
    }
    if (std::wcscmp(name, L"mmag_9") == 0) {
        return EnsoulItem::EOT_MMAG9;
    }
    if (std::wcscmp(name, L"pm_1") == 0) {
        return EnsoulItem::EOT_PM1;
    }
    if (std::wcscmp(name, L"pm_2") == 0) {
        return EnsoulItem::EOT_PM2;
    }
    if (std::wcscmp(name, L"pm_3") == 0) {
        return EnsoulItem::EOT_PM3;
    }
    if (std::wcscmp(name, L"pm_4") == 0) {
        return EnsoulItem::EOT_PM4;
    }
    if (std::wcscmp(name, L"pm_5") == 0) {
        return EnsoulItem::EOT_PM5;
    }
    if (std::wcscmp(name, L"pm_6") == 0) {
        return EnsoulItem::EOT_PM6;
    }
    if (std::wcscmp(name, L"pm_7") == 0) {
        return EnsoulItem::EOT_PM7;
    }
    if (std::wcscmp(name, L"pm_8") == 0) {
        return EnsoulItem::EOT_PM8;
    }
    if (std::wcscmp(name, L"pm_9") == 0) {
        return EnsoulItem::EOT_PM9;
    }
    if (std::wcscmp(name, L"tra_1") == 0) {
        return EnsoulItem::EOT_TRA1;
    }
    if (std::wcscmp(name, L"tra_2") == 0) {
        return EnsoulItem::EOT_TRA2;
    }
    if (std::wcscmp(name, L"tra_3") == 0) {
        return EnsoulItem::EOT_TRA3;
    }
    if (std::wcscmp(name, L"tra_4") == 0) {
        return EnsoulItem::EOT_TRA4;
    }
    if (std::wcscmp(name, L"tra_5") == 0) {
        return EnsoulItem::EOT_TRA5;
    }
    if (std::wcscmp(name, L"tra_6") == 0) {
        return EnsoulItem::EOT_TRA6;
    }
    if (std::wcscmp(name, L"tra_7") == 0) {
        return EnsoulItem::EOT_TRA7;
    }
    if (std::wcscmp(name, L"tra_8") == 0) {
        return EnsoulItem::EOT_TRA8;
    }
    if (std::wcscmp(name, L"tra_9") == 0) {
        return EnsoulItem::EOT_TRA9;
    }
    if (std::wcscmp(name, L"att_1") == 0) {
        return EnsoulItem::EOT_ATT1;
    }
    if (std::wcscmp(name, L"att_2") == 0) {
        return EnsoulItem::EOT_ATT2;
    }
    if (std::wcscmp(name, L"att_3") == 0) {
        return EnsoulItem::EOT_ATT3;
    }
    if (std::wcscmp(name, L"att_4") == 0) {
        return EnsoulItem::EOT_ATT4;
    }
    if (std::wcscmp(name, L"att_5") == 0) {
        return EnsoulItem::EOT_ATT5;
    }
    if (std::wcscmp(name, L"att_6") == 0) {
        return EnsoulItem::EOT_ATT6;
    }
    if (std::wcscmp(name, L"att_7") == 0) {
        return EnsoulItem::EOT_ATT7;
    }
    if (std::wcscmp(name, L"att_8") == 0) {
        return EnsoulItem::EOT_ATT8;
    }
    if (std::wcscmp(name, L"att_9") == 0) {
        return EnsoulItem::EOT_ATT9;
    }
    if (std::wcscmp(name, L"etc_1") == 0) {
        return EnsoulItem::EOT_ETC1;
    }
    if (std::wcscmp(name, L"etc_2") == 0) {
        return EnsoulItem::EOT_ETC2;
    }
    if (std::wcscmp(name, L"etc_3") == 0) {
        return EnsoulItem::EOT_ETC3;
    }
    if (std::wcscmp(name, L"etc_4") == 0) {
        return EnsoulItem::EOT_ETC4;
    }
    if (std::wcscmp(name, L"etc_5") == 0) {
        return EnsoulItem::EOT_ETC5;
    }
    if (std::wcscmp(name, L"etc_6") == 0) {
        return EnsoulItem::EOT_ETC6;
    }
    if (std::wcscmp(name, L"etc_7") == 0) {
        return EnsoulItem::EOT_ETC7;
    }
    if (std::wcscmp(name, L"etc_8") == 0) {
        return EnsoulItem::EOT_ETC8;
    }
    if (std::wcscmp(name, L"etc_9") == 0) {
        return EnsoulItem::EOT_ETC9;
    }
    if (std::wcscmp(name, L"sigel_defense") == 0) {
        return EnsoulItem::EOT_SIGEL_DEFENSE;
    }
    if (std::wcscmp(name, L"sigel_health") == 0) {
        return EnsoulItem::EOT_SIGEL_HEALTH;
    }
    if (std::wcscmp(name, L"tir_might") == 0) {
        return EnsoulItem::EOT_TIR_MIGHT;
    }
    if (std::wcscmp(name, L"tir_haste") == 0) {
        return EnsoulItem::EOT_TIR_HASTE;
    }
    if (std::wcscmp(name, L"othel_focus") == 0) {
        return EnsoulItem::EOT_OTHEL_FOCUS;
    }
    if (std::wcscmp(name, L"othel_crtdmg") == 0) {
        return EnsoulItem::EOT_OTHEL_CRTDMG;
    }
    if (std::wcscmp(name, L"yr_windwalk") == 0) {
        return EnsoulItem::EOT_YR_WINDWALK;
    }
    if (std::wcscmp(name, L"yr_guidance") == 0) {
        return EnsoulItem::EOT_YR_GUIDANCE;
    }
    if (std::wcscmp(name, L"feoh_magicpower") == 0) {
        return EnsoulItem::EOT_FEOH_MAGICPOWER;
    }
    if (std::wcscmp(name, L"feoh_empower") == 0) {
        return EnsoulItem::EOT_FEOH_EMPOWER;
    }
    if (std::wcscmp(name, L"is_mpregen") == 0) {
        return EnsoulItem::EOT_IS_MPREGEN;
    }
    if (std::wcscmp(name, L"is_mpup") == 0) {
        return EnsoulItem::EOT_IS_MPUP;
    }
    if (std::wcscmp(name, L"wynn_acumen") == 0) {
        return EnsoulItem::EOT_WYNN_ACUMEN;
    }
    if (std::wcscmp(name, L"wynn_light") == 0) {
        return EnsoulItem::EOT_WYNN_LIGHT;
    }
    if (std::wcscmp(name, L"eolh_hpregen") == 0) {
        return EnsoulItem::EOT_EOLH_HPREGEN;
    }
    if (std::wcscmp(name, L"eolh_heal") == 0) {
        return EnsoulItem::EOT_EOLH_HEAL;
    }
    if (std::wcscmp(name, L"tir_crtstun") == 0) {
        return EnsoulItem::EOT_TIR_CRTSTUN;
    }
    if (std::wcscmp(name, L"othel_mightmortal") == 0) {
        return EnsoulItem::EOT_OTHEL_MIGHTMORTAL;
    }
    if (std::wcscmp(name, L"yr_crtdmg") == 0) {
        return EnsoulItem::EOT_YR_CRTDMG;
    }
    if (std::wcscmp(name, L"is_acumen") == 0) {
        return EnsoulItem::EOT_IS_ACUMEN;
    }
    if (std::wcscmp(name, L"is_windwalk") == 0) {
        return EnsoulItem::EOT_IS_WINDWALK;
    }
    if (std::wcscmp(name, L"wynn_mpup") == 0) {
        return EnsoulItem::EOT_WYNN_MPUP;
    }
    if (std::wcscmp(name, L"eolh_mpregen") == 0) {
        return EnsoulItem::EOT_EOLH_MPREGEN;
    }
    return EnsoulItem::EOT_INVALID;
}

void EnsoulOptionData::SetExtractionStoneClassId(int classId) {
    L2_GUARD_FUNCTION();
    m_nExtractionStoneClassId = classId;
}

int EnsoulOptionData::GetExtractionStoneClassId() const {
    L2_GUARD_FUNCTION();
    return m_nExtractionStoneClassId;
}

bool EnsoulOptionData::CanExtraction() const {
    L2_GUARD_FUNCTION();
    return m_nExtractionStoneClassId > 0 && m_nId > 0;
}

void EnsoulFeeData::SetEnsoulFeeNormal(FeeItemVector& fees) { m_vFeeNormal.assign(fees.begin(), fees.end()); }

void EnsoulFeeData::SetEnsoulFeeBM(FeeItemVector& fees) { m_vFeeBM.assign(fees.begin(), fees.end()); }

void EnsoulFeeData::SetEnsoulReFeeNormal(FeeItemVector& fees) { m_vReFeeNormal.assign(fees.begin(), fees.end()); }

void EnsoulFeeData::SetEnsoulReFeeBM(FeeItemVector& fees) { m_vReFeeBM.assign(fees.begin(), fees.end()); }

void EnsoulFeeData::SetExtractionFeeNormal(FeeItemVector& fees) { m_vExtractionNormal.assign(fees.begin(), fees.end()); }

void EnsoulFeeData::SetExtractionFeeBM(FeeItemVector& fees) { m_vExtractionBM.assign(fees.begin(), fees.end()); }

void EnsoulStoneData::SetEnsoulOptions(OptionIdVector& options) { m_vOptionIdVector.assign(options.begin(), options.end()); }

bool EnsoulStoneData::CheckOption(std::int64_t optionId) {
    L2_GUARD_FUNCTION();

    bool found = false;
    for (const std::int64_t currentOptionId : m_vOptionIdVector) {
        if (currentOptionId == optionId) {
            found = true;
        }
    }
    return found;
}
