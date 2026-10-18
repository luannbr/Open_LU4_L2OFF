#include "Artifact.h"

#include "ArtifactParser.h"

#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"

CArtifactDB::CArtifactDB() = default;
CArtifactDB::~CArtifactDB() = default;

const std::vector<const CSkillInfo*>* CArtifactDB::GetArtifactbookPageSkills(int page) {
    L2_GUARD_FUNCTION();

    const unsigned index = static_cast<unsigned>(page - 1);
    if (index <= 2) {
        return &m_ArtifactbookPageSkill[index];
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[CArtifactDB]Invalid Artifactbook Page Count [%d]",
    //     page)
    // L2_LOG_ERROR(Log,
    //     L"[CArtifactDB]Invalid Artifactbook Page Count [%d]",
    //     page)
    L2_LOG_ERROR(Log, "Artifact book skill lookup failed: page %d is outside 1..3; returning no skill list.", page);
    return nullptr;
}

int CArtifactDB::GetMaterialMinEnchantLevel(int enchant) {
    L2_GUARD_FUNCTION();

    if (static_cast<unsigned>(enchant) > 10) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[CArtifactDB][%s][%d] invalid enchant value [%d]",
        //     L"Artifact.cpp",
        //     0x5f,
        //     enchant)
        // L2_LOG_ERROR(Log,
        //     L"[CArtifactDB][%s][%d] invalid enchant value [%d]",
        //     L"Artifact.cpp",
        //     0x5f,
        //     enchant)
        L2_LOG_ERROR(
            Log, "Cannot look up artifact material enchant requirement: enchant %d is outside 0..10; returning -1.", enchant
        );
        return -1;
    }
    const auto position = m_MaterialMinEnchantLevelMap.find(enchant);
    return position == m_MaterialMinEnchantLevelMap.end() ? -1 : position->second;
}

const std::vector<int>* CArtifactDB::GetMaterialItemInfo(int groupId) {
    L2_GUARD_FUNCTION();

    const auto position = m_MaterialItemInfoMap.find(groupId);
    return position == m_MaterialItemInfoMap.end() ? nullptr : &position->second;
}

const ArtifactEnchantInfo* CArtifactDB::GetEnchantInfo(int itemId, int enchant) {
    L2_GUARD_FUNCTION();

    const auto position = m_EnchantInfoMap.find(std::CDualKey<int, int>(itemId, enchant));
    return position == m_EnchantInfoMap.end() ? nullptr : &position->second;
}

const std::vector<int>* CArtifactDB::GetMaterialItemInfo(int itemId, int enchant) {
    L2_GUARD_FUNCTION();

    const ArtifactEnchantInfo* const info = GetEnchantInfo(itemId, enchant);
    return info == nullptr ? nullptr : GetMaterialItemInfo(info->group_id);
}

void CArtifactDB::AddMaterialMinEnchantLevel(int enchant, int minimumEnchant) {
    L2_GUARD_FUNCTION();

    if (static_cast<unsigned>(enchant) <= 10) {
        m_MaterialMinEnchantLevelMap[enchant] = minimumEnchant;
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[CArtifactDB][%s][%d] invalid enchant value [%d]",
    //     L"Artifact.cpp",
    //     0x35,
    //     enchant)
    // L2_LOG_ERROR(Log,
    //     L"[CArtifactDB][%s][%d] invalid enchant value [%d]",
    //     L"Artifact.cpp",
    //     0x35,
    //     enchant)
    L2_LOG_ERROR(
        Log, "Artifact material requirement ignored: enchant %d is outside 0..10 (requested minimum %d).", enchant,
        minimumEnchant
    );
}

void CArtifactDB::AddEnchantInfo(int itemId, int enchant, const ArtifactEnchantInfo& info) {
    L2_GUARD_FUNCTION();

    if (static_cast<unsigned>(enchant) <= 10) {
        m_EnchantInfoMap[std::CDualKey<int, int>(itemId, enchant)] = info;
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[CArtifactDB][%s][%d] invalid enchant value [%d]",
    //     L"Artifact.cpp",
    //     0x4e,
    //     enchant)
    // L2_LOG_ERROR(Log,
    //     L"[CArtifactDB][%s][%d] invalid enchant value [%d]",
    //     L"Artifact.cpp",
    //     0x4e,
    //     enchant)
    L2_LOG_ERROR(Log, "Artifact enchant data ignored for item %d: enchant %d is outside 0..10.", itemId, enchant);
}

void CArtifactDB::AddArtifactbookPageSkill(int page, const CSkillInfo* skillInfo) {
    L2_GUARD_FUNCTION();

    if (static_cast<unsigned>(page) > 2) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[CArtifactDB]Invalid ArtifactBook Page Number [%d]",
        //     page)
        // L2_LOG_ERROR(Log,
        //     L"[CArtifactDB]Invalid ArtifactBook Page Number [%d]",
        //     page)
        L2_LOG_ERROR(Log, "Artifact book skill entry ignored: page index %d is outside 0..2.", page);
        return;
    }
    if (skillInfo == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[CArtifactDB]Invalid SkillInfo on ArtifactBook page[%d]",
        //     page)
        // L2_LOG_ERROR(Log,
        //     L"[CArtifactDB]Invalid SkillInfo on ArtifactBook page[%d]",
        //     page)
        L2_LOG_ERROR(Log, "Artifact book skill entry ignored for page index %d: skill data is missing.", page);
        return;
    }
    m_ArtifactbookPageSkill[page].push_back(skillInfo);
}

void CArtifactDB::LoadData() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\artifact.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/artifact.txt";
#endif
    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Read from %s file failed.",
        //     kFileName)
        // L2_LOG_ERROR(Log,
        //     L"Read from %s file failed.",
        //     kFileName)
        L2_LOG_ERROR(Log, "Cannot read artifact.txt; artifact data loading stops.");
        return;
    }

    CParserForArtifact parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForArtifact::Create failed",
        //     L"Artifact.cpp",
        //     0x1d)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForArtifact::Create failed",
        //     L"Artifact.cpp",
        //     0x1d)
        L2_LOG_ERROR(Log, "Cannot initialize artifact.txt parser; artifact data loading stops.");
        return;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing failed : line[%d]",
        //     L"Artifact.cpp",
        //     0x28,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing failed : line[%d]",
        //     L"Artifact.cpp",
        //     0x28,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "Cannot parse artifact.txt at script line %lld; data registered before the error is retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
}

void CArtifactDB::AddMaterialItemInfo(int groupId, std::vector<int>& itemIds) {
    L2_GUARD_FUNCTION();

    m_MaterialItemInfoMap[groupId] = itemIds;
}
