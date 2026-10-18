#pragma once

#include "Shared/src/SharedType.h"

#include <cstddef>

class CLexerForMatchingPoint;

class CParserForMatchingPoint {
public:
    CParserForMatchingPoint();
    ~CParserForMatchingPoint();

    // Read the tokens of the next yyparse call from `lexer`. The caller keeps it alive.
    int Create(CLexerForMatchingPoint* lexer);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

private:
    CLexerForMatchingPoint* m_pLexer = nullptr;

    MatchingPoint::MATCHINGPOINT_TYPE m_nGameType; // +0xf0
};
