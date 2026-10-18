#pragma once

#include "LexerLuckyGame.h"
#include "LuckyGameSystem.h"

#include <cstddef>

class CParserForLuckyGame {
public:
    CParserForLuckyGame();
    ~CParserForLuckyGame();

    int create();
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

    int m_nIndex; // +0xf0
    LuckyGameReward* m_pReward; // +0xf8
    double m_dPer; // +0x100
    CLexerForLuckyGame m_lexer; // +0x108
};
