#pragma once

#include "L2NPC/src/LexerSuperPointInfo.h"

#include <array>
#include <cstddef>

namespace l2geo {

class CSuperPointInfo;

// L2NPC/parser_superpointinfo.h. The grammar is ParserSuperPointInfo.y.
// Bison generates yyparse as a member, so the grammar actions use these
// fields. The offset comments give the layout in the original image.
class CParserForSuperPointInfo {
public:
    CParserForSuperPointInfo();
    ~CParserForSuperPointInfo();

    int create();
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

    CLexerForSuperPointInfo m_lexer; // +0x0f0
    int classType; // +0x1b8
    int class_id; // +0x1bc
    int nodeid; // +0x1c0

private:
    [[maybe_unused]] std::array<std::byte, 0x04> m_semanticPadding; // +0x1c4

public:
    CSuperPointInfo* pInfo; // +0x1c8
};


} // namespace l2geo
