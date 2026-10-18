#pragma once

#include "LexerSetItem.h"

#include <cstddef>

class CItem;
class CNewSetItem;
class CShapeShiftedSetItem;
class CObjectDB;

class CParserForSetItem {
public:
    CParserForSetItem();
    ~CParserForSetItem();

    int Create(CObjectDB* objectDb, char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

    CLexerForSetItem m_lexer; // +0xf0

private:
    [[maybe_unused]] CItem* m_pItem; // +0x1d8, unused by the current SetItem actions
    CObjectDB* m_pObjDB; // +0x1e0
    CNewSetItem* m_pNewSetItem; // +0x1e8
    [[maybe_unused]] char16_t* m_pszDesc; // +0x1f0, unused by the current SetItem actions
};

