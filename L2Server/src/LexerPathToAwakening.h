#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>

class CParserForPathToAwakening;

class CLexerForPathToAwakening : public grammar::UTF16Scanner {
public:
    CLexerForPathToAwakening();
    ~CLexerForPathToAwakening() = default;

    int Create();
    int Lex(grammar::SemanticBits* lval);
    int yygetchar();

    int line; // +0xc0
    int comment_count; // +0xc4
};
