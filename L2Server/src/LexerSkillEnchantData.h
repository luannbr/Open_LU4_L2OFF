#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>

class CParserForSkillEnchant;

class CLexerForSkillEnchant : public grammar::UTF16Scanner {
public:
    CLexerForSkillEnchant();
    ~CLexerForSkillEnchant() = default;

    int Create();
    int Lex(grammar::SemanticBits* lval);
    int yygetchar();

    std::int32_t line; // +0xc0
};
