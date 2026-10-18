#pragma once

#include "LexerSoulshotEffect.h"

#include <cstddef>

class CItem;
class CObjectDB;
class CSoulShoteffect;

class CParserForSoulShotEffect {
public:
    CParserForSoulShotEffect();
    ~CParserForSoulShotEffect();

    int Create(CObjectDB* objectDb, char16_t* fileBuffer, int fileSize, bool onlyPrecompiledHeader);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

    CLexerForSoulShotEffect m_lexer; // +0xf0
    CItem* m_pItem; // +0x1d8

private:
    CObjectDB* m_pObjDB; // +0x1e0
    CSoulShoteffect* m_pSoulShotEffect; // +0x1e8
    [[maybe_unused]] char16_t* m_pszDesc; // +0x1f0
};
