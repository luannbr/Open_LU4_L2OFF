#pragma once

#include "BuilderCmdLex.h"

#include <cstddef>

class CParserForBuilderCmd {
public:
    CParserForBuilderCmd();
    ~CParserForBuilderCmd();

    int Create();
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

    CLexerForBuilderCmd m_lexer; // +0xf0
};
