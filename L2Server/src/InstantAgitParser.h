#pragma once

#include "InstantAgitLexer.h"

#include <cstddef>

class CInstantAgitData;

class CParserForInstantAgit {
public:
    CParserForInstantAgit();
    ~CParserForInstantAgit();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

    CLexerForInstantAgit m_lexer; // +0x0f0

private:
    CInstantAgitData* m_pInstantAgitData; // +0x1c8
};
