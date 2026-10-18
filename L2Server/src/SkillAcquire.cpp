#include "SkillAcquire.h"

#include "Skill.h"
#include "SkillAcquireYacc.h"

#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"

#include <algorithm>

CSkillAcquireDB g_SkillAcquireDB;

SkillAcquireInfo::SkillAcquireInfo() :
    nSkillID(-1),
    nSkillLevel(-1),
    m_GetMainDualClassLevel(0),
    m_SocialClass(-1),
    m_MaximumPledgeType(ACADEMY),
    m_bResidenceSkill(false),
    m_bMentorMenteeSkill(false),
    m_bExcludedFromListOfSkillsToLearn(false) {}

CSkillAcquireDB::CSkillAcquireDB() = default;

CSkillAcquireDB::~CSkillAcquireDB() = default;

void CSkillAcquireDB::Load() {
    m_mapClsSkill_to_info.clear();
    for (SkillAcquireInfoVector& skills : m_vectorClsSkill) {
        skills.clear();
    }

#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\skillacquire.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/skillacquire.txt";
#endif

    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from %s file failed",
        //     L"SkillAcquire.cpp",
        //     0x22,
        //     kFileName)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from %s file failed",
        //     L"SkillAcquire.cpp",
        //     0x22,
        //     kFileName)
        L2_LOG_ERROR(Log, "Cannot read skillacquire.txt; loading stops after the class skill rules were cleared.");
        return;
    }

    CParserForSkillAcquire parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForSkillAcquire::create failed",
        //     L"SkillAcquire.cpp",
        //     0x2a)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForSkillAcquire::create failed",
        //     L"SkillAcquire.cpp",
        //     0x2a)
        L2_LOG_ERROR(
            Log, "Cannot initialize the skillacquire.txt parser; loading stops after the class skill rules were cleared."
        );
        return;
    }

    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing %s file failed after line[%d]",
        //     L"SkillAcquire.cpp",
        //     0x35,
        //     kFileName,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing %s file failed after line[%d]",
        //     L"SkillAcquire.cpp",
        //     0x35,
        //     kFileName,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "Parsing skillacquire.txt stopped at line %lld; entries already published by this load are retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
}

void CSkillAcquireDB::Add(SkillAcquireInfo* info) {
    m_mapClsSkill_to_info[ClassSkillKey(info->cls, info->nSkillID, info->nSkillLevel)] = info;
    m_vectorClsSkill[static_cast<std::size_t>(info->cls)].push_back(info);

    const ClassMinimumLevelKey key(info->cls, info->nSkillID);
    auto [position, inserted] = m_mapClsSkill_to_minLevel.emplace(key, info->nSkillLevel);
    if (!inserted) {
        position->second = std::min(position->second, info->nSkillLevel);
    }
}

void CSkillAcquireDB::AddEtcSkill(SkillAcquireInfo* info) {
    m_mapEtcSkill_to_info[EtcSkillKey(info->etcType, info->nSkillID, info->nSkillLevel)] = info;
    m_vectorEsatSkill[static_cast<std::size_t>(info->etcType)].push_back(info);

    const EtcMinimumLevelKey key(info->etcType, info->nSkillID);
    auto [position, inserted] = m_mapEtcSkill_to_minLevel.emplace(key, info->nSkillLevel);
    if (!inserted) {
        position->second = std::min(position->second, info->nSkillLevel);
    }
}

void CSkillAcquireDB::AddRaceSkill(SkillAcquireInfo* info) {
    m_vectorRaceSkill[static_cast<std::size_t>(info->raceType)].push_back(info);
}

const CSkillAcquireDB::SkillAcquireInfoVector& CSkillAcquireDB::GetAcquirableSkills(ClassType classType) const {
    L2_GUARD_FUNCTION();
    return m_vectorClsSkill[static_cast<std::size_t>(classType)];
}

const CSkillAcquireDB::SkillAcquireInfoVector& CSkillAcquireDB::GetAcquirableEtcSkills(EtcSkillAcquireType type) const {
    L2_GUARD_FUNCTION();
    return m_vectorEsatSkill[static_cast<std::size_t>(type)];
}

const CSkillAcquireDB::SkillAcquireInfoVector& CSkillAcquireDB::GetAcquirableRaceSkills(RaceType raceType) const {
    L2_GUARD_FUNCTION();
    return m_vectorRaceSkill[static_cast<std::size_t>(raceType)];
}

const CSkillAcquireDB::SkillAcquireInfoVector& CSkillAcquireDB::GetAcquirableSkills(
    EtcSkillAcquireType type, ClassType classType
) const {
    L2_GUARD_FUNCTION();
    return type == ESAT_NORMAL ? GetAcquirableSkills(classType) : GetAcquirableEtcSkills(type);
}

SkillAcquireInfo* CSkillAcquireDB::GetSkillAcquireInfo(ClassType classType, int skillId, int level) {
    // Current L2Server 0x00a6c818.
    L2_GUARD_FUNCTION();
    const auto found = m_mapClsSkill_to_info.find(ClassSkillKey(classType, skillId, level));
    return found == m_mapClsSkill_to_info.end() ? nullptr : found->second;
}

SkillAcquireInfo* CSkillAcquireDB::GetSkillAcquireInfo(
    EtcSkillAcquireType type, ClassType classType, int skillId, int level
) {
    // Current L2Server 0x00a6cc88.
    L2_GUARD_FUNCTION();
    return type == ESAT_NORMAL ? GetSkillAcquireInfo(classType, skillId, level)
                               : GetEtcSkillAcquireInfo(type, skillId, level);
}

SkillAcquireInfo* CSkillAcquireDB::GetEtcSkillAcquireInfo(EtcSkillAcquireType type, int skillId, int level) {
    // Current L2Server 0x00a6c9b8.
    L2_GUARD_FUNCTION();
    const auto found = m_mapEtcSkill_to_info.find(EtcSkillKey(type, skillId, level));
    if (found != m_mapEtcSkill_to_info.end()) {
        return found->second;
    }

    if (g_SkillDB.GetSkillInfo(CSkillKey(skillId, level, 0), nullptr) != nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] no EtcSkillAcquireInfo exist! skillId[%d], level[%d]",
        //     L"SkillAcquire.cpp",
        //     0x8a,
        //     skillId,
        //     level)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] no EtcSkillAcquireInfo exist! skillId[%d], level[%d]",
        //     L"SkillAcquire.cpp",
        //     0x8a,
        //     skillId,
        //     level)
        L2_LOG_ERROR(
            Log, "No skill acquisition rule exists for type %d, skill %d level %d; lookup returns no rule.",
            static_cast<int>(type), skillId, level
        );
    }
    return nullptr;
}

int CSkillAcquireDB::GetMinimumLevel(ClassType classType, int skillId) const {
    // Current L2Server 0x00a6cb0c.
    const auto found = m_mapClsSkill_to_minLevel.find(ClassMinimumLevelKey(classType, skillId));
    if (found == m_mapClsSkill_to_minLevel.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Undefind skill!  cls[%d] skillid[%d] skilllevel",
        //     L"SkillAcquire.cpp",
        //     0x104,
        //     static_cast<int>(classType),
        //     skillId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Undefind skill!  cls[%d] skillid[%d] skilllevel",
        //     L"SkillAcquire.cpp",
        //     0x104,
        //     static_cast<int>(classType),
        //     skillId)
        L2_LOG_ERROR(
            Log, "No minimum skill level is registered for class %d, skill %d; lookup returns 0.",
            static_cast<int>(classType), skillId
        );
        return 0;
    }
    return found->second;
}

int CSkillAcquireDB::GetEtcMinimumLevel(EtcSkillAcquireType type, int skillId) const {
    // Current L2Server 0x00a6cb8c.
    const auto found = m_mapEtcSkill_to_minLevel.find(EtcMinimumLevelKey(type, skillId));
    if (found == m_mapEtcSkill_to_minLevel.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Undefind Etc skill!  skillid[%d] skilllevel",
        //     L"SkillAcquire.cpp",
        //     0x114,
        //     skillId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Undefind Etc skill!  skillid[%d] skilllevel",
        //     L"SkillAcquire.cpp",
        //     0x114,
        //     skillId)
        L2_LOG_ERROR(
            Log, "No minimum skill level is registered for acquisition type %d, skill %d; lookup returns 0.",
            static_cast<int>(type), skillId
        );
        return 0;
    }
    return found->second;
}

bool CSkillAcquireDB::IsMatchSkillAcquireType(EtcSkillAcquireType type, int skillId, int level) {
    L2_GUARD_FUNCTION();
    return m_mapEtcSkill_to_info.find(EtcSkillKey(type, skillId, level)) != m_mapEtcSkill_to_info.end();
}
