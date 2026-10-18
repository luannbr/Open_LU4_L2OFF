#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>
#include <cstdint>

class ParserForHairEx;

class LexerForHairEx : public grammar::UTF16Scanner {
public:
    LexerForHairEx();
    ~LexerForHairEx();

    int Create(char16_t* fileBuffer, int fileSize);
    int Lex(grammar::SemanticBits* lval);

private:
    char16_t* m_FileBuf; // +0x0c0
    char* m_RawFileBuf; // +0x0c8
    std::int32_t m_FilePointer; // +0x0d0
    std::int32_t m_FileSize; // +0x0d4

public:
    std::int32_t comment_count; // +0x0d8
    std::int32_t line; // +0x0dc
};
