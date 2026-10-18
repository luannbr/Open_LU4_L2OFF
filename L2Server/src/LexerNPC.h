#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>
#include <cstdint>

class CParserForNPC;

// Manifest-original lexer_npc owner. The physical layout is fixed by the
// current constructor, yygetchar ring-buffer writes, and parser Create path.
class CLexerForNPC : public grammar::UTF16Scanner {
public:
    CLexerForNPC();
    ~CLexerForNPC();

    int Create(CParserForNPC* parser, char16_t* fileBuffer, int fileSize);
    int Lex(grammar::SemanticBits* lval);
    int yygetchar();

    char16_t* m_FileBuf; // +0x0c0
    char* m_pRawFileBuf; // +0x0c8
    std::int32_t m_nFilePointer; // +0x0d0
    std::int32_t m_nFileSize; // +0x0d4
    char16_t m_LastInput[100]; // +0x0d8
    std::int32_t m_nLastInput; // +0x1a0
    std::int32_t line; // +0x1a4
    std::int32_t m_nNpcId; // +0x1a8
    bool m_bOnlyPrecompiledHeader; // +0x1ac
    [[maybe_unused]] std::byte m_targetPadding1ad[3];
};
