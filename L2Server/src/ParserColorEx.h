#pragma once

#include "LexerColorEx.h"

#include <cstddef>

class CSetting;

class ParserForColorEx {
public:
    ParserForColorEx();
    ~ParserForColorEx();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

    LexerForColorEx m_lexer; // +0x0f0
    CSetting* m_pSetting; // +0x1d0

};
