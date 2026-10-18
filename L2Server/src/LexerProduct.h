#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>

class CParserForProduct;

class CLexerForProduct : public grammar::UTF16Scanner {
public:
    CLexerForProduct();
    ~CLexerForProduct() = default;
    int Lex(grammar::SemanticBits* lval);

    int yygetchar();

    int line; // +0xc0
    int comment_count; // +0xc4
};
