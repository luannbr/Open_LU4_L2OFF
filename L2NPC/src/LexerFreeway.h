#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"


// L2NPC/lexer_freeway.h. The scanner is LexerFreeway.re. The source adds its
// public line counter.
class CLexerForFreeway : public grammar::UTF16Scanner {
public:
    CLexerForFreeway();
    ~CLexerForFreeway();

    int create();
    int Lex(grammar::SemanticBits* lval);

private:
    int yygetchar();

public:
    int line = 0; // +0xc0
};
