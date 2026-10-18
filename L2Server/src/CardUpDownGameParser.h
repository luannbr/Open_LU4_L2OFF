#pragma once

#include "CardUpDownGameLexer.h"

#include <cstddef>

namespace CardUpDownGame {
class GameData;
}

class CParserForCardUpDownGame {
public:
    CParserForCardUpDownGame();
    ~CParserForCardUpDownGame();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

    CLexerForCardUpDownGame m_lexer; // +0x0f0

private:
    [[maybe_unused]] char16_t* m_pszDesc; // +0x1d8
    CardUpDownGame::GameData* m_pGameData; // +0x1e0
};
