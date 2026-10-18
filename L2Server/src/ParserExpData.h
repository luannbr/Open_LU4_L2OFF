#pragma once

#include "LexerExpData.h"

#include <cstddef>
#include <cstdint>

extern std::int64_t ExpByLevel[122];
extern double ExpByLevelTraining[122];

class ParserForExp {
public:
    ParserForExp();
    ~ParserForExp();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

    LexerForExp m_lexer; // +0x0f0
};
