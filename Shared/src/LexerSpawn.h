#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

// Shared/lexer_spawn.h, L2Server specialization. The scanner is
// LexerSpawn.re. Its rules match the L2NPC scanner, while its parser is the
// checker-only CParserForNPCSpawn of the L2Server product.
class CLexerForNPCSpawn : public grammar::UTF16Scanner {
public:
    CLexerForNPCSpawn();
    ~CLexerForNPCSpawn();

    int Create(char16_t* text, int codeUnitCount);
    int Lex(grammar::SemanticBits* lval);

private:
    char16_t* m_FileBuf = nullptr; // +0x0c0
    int m_nFilePointer = 0; // +0x0c8
    int m_nFileSize = 0; // +0x0cc
};
