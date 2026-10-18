#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>
#include <cstdint>

class CParserForItemAttrChange;

class CLexerForItemAttrChange : public grammar::UTF16Scanner {
public:
    CLexerForItemAttrChange();
    ~CLexerForItemAttrChange() = default;

    int create();
    int Lex(grammar::SemanticBits* lval);
    int getLine() const { return line; }
    int yygetchar();

    std::int32_t line; // +0xc0
    std::int32_t comment_count; // +0xc4
};
