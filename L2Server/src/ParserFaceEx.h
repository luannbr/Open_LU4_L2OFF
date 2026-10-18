#pragma once

#include "LexerFaceEx.h"

#include <cstddef>

class ParserForFaceEx {
public:
    ParserForFaceEx();
    ~ParserForFaceEx();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

    LexerForFaceEx m_lexer; // +0x0f0

};
