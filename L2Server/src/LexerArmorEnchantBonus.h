#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>

class CParserForArmorEnchantBonus;

class CLexerForArmorEnchantBonus : public grammar::UTF16Scanner {
public:
    CLexerForArmorEnchantBonus();
    ~CLexerForArmorEnchantBonus() = default;

    int Create();
    int Lex(grammar::SemanticBits* lval);
    int yygetchar();

    int line; // +0xc0
};
