#pragma once

#include "LexerBasicAction.h"

#include <cstddef>
#include <cstdint>

class CParserForAction {
public:
    CParserForAction();
    ~CParserForAction();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

    CLexerForAction m_lexer; // +0x0f0
    std::int32_t m_handlerOption; // +0x1d0
    bool m_bAllowedRaidUser; // +0x1d4
    [[maybe_unused]] std::byte m_targetPadding1d5[3]; // +0x1d5
};
