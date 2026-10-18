#pragma once

#include "L2NPC/src/LexerFreeway.h"

#include <array>
#include <cstddef>


class CFreeway;
struct CFreewayNode;

// L2NPC/parser_freeway.h. The grammar is ParserFreeway.y. Bison generates
// yyparse as a member, so the grammar actions use these fields. The offset
// comments give the layout in the original image.
class CParserForFreeway {
public:
    CParserForFreeway();
    ~CParserForFreeway();

    int create();
    int yyparse();
    void yysyntaxerror();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

    CLexerForFreeway m_lexer; // +0x0f0
    int freewayID; // +0x1b8
    int nodeID; // +0x1bc
    int prevNodeID; // +0x1c0
    int x; // +0x1c4
    int y; // +0x1c8
    int z; // +0x1cc
    int delay; // +0x1d0
    int social; // +0x1d4
    int state; // +0x1d8

private:
    [[maybe_unused]] std::array<std::byte, 0x04> m_actionPadding; // +0x1dc

public:
    CFreeway* freeway; // +0x1e0
    CFreewayNode* node; // +0x1e8
};
