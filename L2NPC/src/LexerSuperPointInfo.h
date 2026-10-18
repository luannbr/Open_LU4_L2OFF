#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

namespace l2geo {

// L2NPC/lexer_superpointinfo.h. The scanner is LexerSuperPointInfo.re. The
// source line counter is the only derived field.
class CLexerForSuperPointInfo : public grammar::UTF16Scanner {
public:
    CLexerForSuperPointInfo();
    ~CLexerForSuperPointInfo();

    int create();
    int Lex(grammar::SemanticBits* lval);

private:
    int yygetchar();

public:
    int line = 0; // +0xc0
};


} // namespace l2geo
