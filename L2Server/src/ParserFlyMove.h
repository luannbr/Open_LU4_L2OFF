#pragma once

#include "FlyMove.h"
#include "LexerFlyMove.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CParserForFlyMove {
public:
    CParserForFlyMove();
    ~CParserForFlyMove();

    int create();
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

    FVector m_Pos; // +0xf0
    std::int32_t m_nNodeCount; // +0x108
    [[maybe_unused]] std::array<std::byte, 0x04> m_alignment10cTo110; // +0x10c
    CFlyMovePath* m_path; // +0x110
    CFlyMovePathNode* m_node; // +0x118
    CLexerForFlyMove m_lexer; // +0x120
};
