#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>
#include <cstdint>

class CParserForShuttle;

class CLexerForShuttle : public grammar::UTF16Scanner {
public:
    CLexerForShuttle();
    ~CLexerForShuttle() = default;

    int Create();
    int Lex(grammar::SemanticBits* lval);
    int yygetchar();

    std::int32_t line; // +0xc0
};
