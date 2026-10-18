#pragma once

#include "ItemAttrChange.h"
#include "ItemAttrChangeLexer.h"

#include <cstddef>

class CParserForItemAttrChange {
public:
    CParserForItemAttrChange();
    ~CParserForItemAttrChange();

    int create();
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    int FindItemClassId(const char16_t* name);

    CItemAttrChangeList* m_pItemAttrChangeList; // +0xf0
    CItemAttrChangeStruct* m_pItemAttrChangeStruct; // +0xf8
    CLexerForItemAttrChange m_lexer; // +0x100
};
