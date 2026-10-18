#pragma once

#include "L2NPC/src/FormationManager.h"
#include "L2NPC/src/LexerFormation.h"

#include <array>
#include <cstddef>


// L2NPC/parser_formation.h. The grammar is ParserFormation.y. Bison
// generates yyparse as a member, so the grammar actions use these fields.
// The offset comments give the layout in the original image.
class CParserForFormation {
public:
    CParserForFormation();
    ~CParserForFormation();

    int create();
    int yyparse();
    void yysyntaxerror();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

    CLexerForFormation m_lexer; // +0x0f0
    int nFormationID; // +0x1b8
    int nPosID; // +0x1bc
    int nPrevPosID; // +0x1c0

private:
    [[maybe_unused]] std::array<std::byte, 0x04> m_semanticPadding; // +0x1c4

public:
    FormationConstData sData; // +0x1c8
    int x; // +0x4d8
    int y; // +0x4dc
    int z; // +0x4e0

private:
    [[maybe_unused]] std::array<std::byte, 0x04> m_tailPadding; // +0x4e4
};
