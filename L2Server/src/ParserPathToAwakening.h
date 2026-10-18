#pragma once

#include "LexerPathToAwakening.h"

#include <cstddef>

class CParserForPathToAwakening {
public:
    CParserForPathToAwakening();
    ~CParserForPathToAwakening();

    int Create();
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

    CLexerForPathToAwakening m_lexer; // +0xf0
    int nCountryCode; // +0x1b8
};
