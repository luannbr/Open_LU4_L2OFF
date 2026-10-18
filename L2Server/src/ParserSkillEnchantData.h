#pragma once

#include "LexerSkillEnchantData.h"

#include <cstddef>
#include <cstdint>

struct SkillEnchantInfo;

class CParserForSkillEnchant {
public:
    CParserForSkillEnchant();
    ~CParserForSkillEnchant();

    int Create();
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

    CLexerForSkillEnchant m_lexer; // +0x0f0
    std::int32_t classType; // +0x1b8

private:
    [[maybe_unused]] std::int32_t m_alignment1bc; // +0x1bc

public:
    SkillEnchantInfo* Info; // +0x1c0
};
