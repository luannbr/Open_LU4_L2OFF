#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstdint>

namespace l2npc {

// L2NPC/lexer_spawn.h. The scanner is LexerSpawn.re. The source fields keep
// the caller's UTF-16 image and do not own or free it.
class CLexerForNPCSpawn : public grammar::UTF16Scanner {
public:
    CLexerForNPCSpawn();
    ~CLexerForNPCSpawn();

    int Create(std::uint16_t* text, int codeUnitCount);
    int Lex(grammar::SemanticBits* lval);

private:
    std::uint16_t* m_FileBuf = nullptr; // +0x0c0
    int m_nFilePointer = 0; // +0x0c8
    int m_nFileSize = 0; // +0x0cc
};


} // namespace l2npc
