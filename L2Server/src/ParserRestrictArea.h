#pragma once

#include "LexerRestrictArea.h"

#include <cstddef>

class CAxisAlgnBB;
class CTerritory;
class CGeoData;

class CParserForRestrictArea {
public:
    CParserForRestrictArea();
    ~CParserForRestrictArea();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

    CLexerForRestrictArea m_lexer; // +0x0f0

private:
    CGeoData* m_pGeoData; // +0x1c8
    CTerritory* pTempTerritory; // +0x1d0
    CAxisAlgnBB* m_pServerBlock; // +0x1d8
};
