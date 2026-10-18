#pragma once

#include "MultiSellLex.h"

#include <cstddef>

class CMultiSellList;

class CParserForMultiSell {
public:
    CParserForMultiSell();
    ~CParserForMultiSell();

    int Create();
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    int Create(char16_t* fileBuffer, int fileSize);
    int FindItemClassId(const char16_t* itemName);

    CMultiSellList* m_pMultiSellList; // +0xf0
    CLexerForMultiSell m_lexer; // +0xf8
    bool bExistMultiSell; // +0x1d8

};
