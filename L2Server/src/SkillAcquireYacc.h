#pragma once

#include "SkillAcquire.h"
#include "SkillAcquireLex.h"

#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CParserForSkillAcquire {
public:
    using AcquireSkillMap = ::std::map<int, int>;
    using AutoGetSkillMap = ::std::map<std::u16string, bool, runtime::CStringLess<std::u16string>>;

    CParserForSkillAcquire();
    ~CParserForSkillAcquire();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

    CLexerForSkillAcquire m_lexer; // +0x0f0

private:
    ClassType classType; // +0x1c8
    EtcSkillAcquireType etcType; // +0x1cc
    RaceType raceType; // +0x1d0
    SkillAcquireInfo* pAcquireInfo; // +0x1d8
    std::vector<int> m_vNeededItem; // +0x1e0
    std::vector<int> m_vNeededItemCount; // +0x200
    char16_t* m_sSkillName; // +0x220
    AutoGetSkillMap m_mapAutoGetSkill; // +0x228
    std::array<AcquireSkillMap, CT_MAX> m_vAcquireSkillsByClass; // +0x248
    std::vector<int> m_vAcquireEtcSkills; // +0x1a48
    std::vector<char16_t*> m_TempStringVector; // +0x1a68
};
