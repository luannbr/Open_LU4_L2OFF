#pragma once

#include "LexerHairEx.h"

#include <cstddef>

class CSetting;

class ParserForHairEx {
public:
    ParserForHairEx();
    ~ParserForHairEx();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

    LexerForHairEx m_lexer; // +0x0f0
    CSetting* m_pSetting; // +0x1d0

};
