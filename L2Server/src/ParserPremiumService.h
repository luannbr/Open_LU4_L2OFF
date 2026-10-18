#pragma once

#include "LexerPremiumService.h"

#include <cstddef>

class CParserForPremiumService {
public:
    CParserForPremiumService();
    ~CParserForPremiumService();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

    CLexerForPremiumService m_lexer; // +0x0f0
};
