#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>
#include <cstdint>

class CParserForTeleportQuestPath;

class CLexerForTeleportQuestPath : public grammar::UTF16Scanner {
public:
    CLexerForTeleportQuestPath();
    ~CLexerForTeleportQuestPath();

    int Create();
    int Lex(grammar::SemanticBits* lval);
    int yygetchar();

    std::int32_t comment_count; // +0xc0
    std::int32_t line; // +0xc4
};
