#pragma once

#include "CombinationItem.h"
#include "LexerCombinationItem.h"

#include <cstddef>
#include <cstdint>

class CCombinationItem;
class CObjectDB;

class CParserForCombinationItem {
public:
    CParserForCombinationItem();
    ~CParserForCombinationItem();

    int Create(CObjectDB* objectDb, char16_t* fileBuffer, int fileSize, bool onlyPrecompiledHeader);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

    CLexerForCombinationItem m_lexer; // +0xf0

private:
    CObjectDB* m_pObjDB; // +0x1d8
    CCombinationItem* m_pCombinationItem; // +0x1e0
    [[maybe_unused]] char16_t* m_pszDesc; // +0x1e8
    CombinationItemInfoSet* m_pCombItemSet; // +0x1f0
    CombinationItemInfo* m_pCombInfo; // +0x1f8
    std::int32_t m_nId; // +0x200
    CombinationItemKey* m_pKeyItem; // +0x208
};
