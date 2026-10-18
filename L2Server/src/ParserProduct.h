#pragma once

#include "BRProductServer.h"
#include "LexerProduct.h"

#include <cstddef>

class CParserForProduct {
public:
    CParserForProduct();
    ~CParserForProduct();

    int create();
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

    CProduct* m_product; // +0x0f0
    SYSTEMTIME m_saleStartDate; // +0x0f8
    SYSTEMTIME m_saleEndDate; // +0x108
    SYSTEMTIME m_birthdayStart; // +0x118
    SYSTEMTIME m_birthdayEnd; // +0x128
    CLexerForProduct m_lexer; // +0x138
};
