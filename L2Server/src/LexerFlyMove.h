#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>
#include <cstdint>

class CParserForFlyMove;

class CLexerForFlyMove : public grammar::UTF16Scanner {
public:
    CLexerForFlyMove();
    ~CLexerForFlyMove() = default;

    int create();
    int Lex(grammar::SemanticBits* lval);
    int yygetchar();

    std::int32_t line; // +0xc0
};
