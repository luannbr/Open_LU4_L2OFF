#pragma once

#include "LexerArmorEnchantBonus.h"

#include <cstddef>

class CParserForArmorEnchantBonus {
public:
    CParserForArmorEnchantBonus();
    ~CParserForArmorEnchantBonus();

    int Create();
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

    CLexerForArmorEnchantBonus m_lexer; // +0xf0
};
