#pragma once

#include "ManorLex.h"

#include <cstddef>
#include <cstdint>

class CManor;

class CParserForManor {
public:
    CParserForManor();
    ~CParserForManor();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

private:
    CManor* m_pManor; // +0x0f0
    std::int32_t nManorID; // +0x0f8
    [[maybe_unused]] std::byte m_targetPadding0fc[4]; // +0x0fc

public:
    CLexerForManor m_lexer; // +0x100
};
