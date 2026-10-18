#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"


// L2NPC/lexer_formation.h. The scanner is LexerFormation.re.
// Lexer_formation.l adds the public line counter.
class CLexerForFormation : public grammar::UTF16Scanner {
public:
    CLexerForFormation();
    ~CLexerForFormation();

    int create();
    int Lex(grammar::SemanticBits* lval);

private:
    int yygetchar();

public:
    int line = 0; // +0xc0
};
