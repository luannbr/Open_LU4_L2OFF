#pragma once

#include "HennaLex.h"

#include <cstddef>
#include <cstdint>

class CHenna;

class CParserForHenna {
public:
    CParserForHenna();
    ~CParserForHenna();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

    CHenna* m_pHenna; // +0x0f0
    std::int32_t nHennaID; // +0x0f8

private:
    [[maybe_unused]] std::uint32_t m_alignmentFcTo100;

public:
    CLexerForHenna m_lexer; // +0x100
};
