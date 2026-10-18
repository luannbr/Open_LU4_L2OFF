#pragma once

#include "LexerSkillSynergy.h"
#include "Skill.h"

#include <cstddef>
#include <cstdint>

class CParserForSkillSynergy {
public:
    CParserForSkillSynergy();
    ~CParserForSkillSynergy();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

    CLexerForSkillSynergy m_lexer; // +0x0f0

private:
    TriggerSynergySkillInfo* info; // +0x1d0
    std::int32_t synergyId; // +0x1d8
    [[maybe_unused]] std::int32_t m_targetPadding1dc{};
    std::set<AbnormalStatusSlotType> m_setAbnormalSlotType; // +0x1e0
    std::map<int, CSkillInfo*> m_tempSkillMap; // +0x200
};
