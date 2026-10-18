#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>
#include <cstdint>

class CParserForBuilderCmd;

class CLexerForBuilderCmd : public grammar::UTF16Scanner {
public:
    CLexerForBuilderCmd();
    ~CLexerForBuilderCmd();

    int Create();
    int Lex(grammar::SemanticBits* lval);
    int yygetchar();

    std::int32_t line; // +0xc0
};
