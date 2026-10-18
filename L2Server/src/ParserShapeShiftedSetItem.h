#pragma once

#include "LexerShapeShiftedSetItem.h"

#include <cstddef>

class CItem;
class CNewSetItem;
class CShapeShiftedSetItem;
class CObjectDB;

// The grammar is ParserShapeShiftedSetItem.y. Bison generates yyparse as a
// member, so the grammar actions use these fields. The offset comments give
// the layout in the original image.
class CParserForShapeShiftedSetItem {
public:
    CParserForShapeShiftedSetItem();
    ~CParserForShapeShiftedSetItem();

    int Create(CObjectDB* objectDb, char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

    CLexerForShapeShiftedSetItem m_lexer; // +0xf0

private:
    [[maybe_unused]] CItem* m_pItem; // +0x1d8, unused by current actions
    CObjectDB* m_pObjDB; // +0x1e0
    CShapeShiftedSetItem* m_pShapeShiftedSetItem; // +0x1e8
    [[maybe_unused]] char16_t* m_pszDesc; // +0x1f0, unused by current actions
};
