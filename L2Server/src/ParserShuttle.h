#pragma once

#include "LexerShuttle.h"
#include "Shuttle.h"

#include <cstddef>
#include <cstdint>

class CDoor;

// The grammar is ParserShuttle.y. Bison generates yyparse as a member, so the
// grammar actions use these fields. The offset comments give the layout in the
// original image.
class CParserForShuttle {
public:
    CParserForShuttle();
    ~CParserForShuttle();

    int Create();
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

    CShuttle* m_pShuttle; // +0xf0
    CShuttleConst* m_pShuttleConst; // +0xf8
    CShuttlePathNode m_Node; // +0x100
    FVector m_Pos; // +0x170
    std::int32_t m_nNodeCount; // +0x188
    CDoor* m_pDoor; // +0x190
    InnerDoor m_InnerDoor; // +0x198
    CLexerForShuttle m_lexer; // +0x1e8
};
