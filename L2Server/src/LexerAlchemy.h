#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>
#include <cstdint>

class CParserForAlchemy;

class CLexerForAlchemy : public grammar::UTF16Scanner {
public:
    CLexerForAlchemy();
    ~CLexerForAlchemy();

    int Create(char16_t* fileBuffer, int fileSize);
    int Lex(grammar::SemanticBits* lval);

private:
    char16_t* m_FileBuf; // +0xc0
    char* m_pRawFileBuf; // +0xc8
    std::int32_t m_nFilePointer; // +0xd0
    std::int32_t m_nFileSize; // +0xd4

public:
    std::int32_t line; // +0xd8

private:
    std::int32_t comment_count; // +0xdc
    [[maybe_unused]] bool m_bOnlyPrecompiledHeader; // +0xe0
    // Current CParserForAlchemy places its first owner field at +0x1e0,
    // proving the embedded lexer extent 0xf0. No current body assigns a
    // source-level field in this tail, so keep the exact byte boundary.
    [[maybe_unused]] std::byte m_targetTailE1ToF0[15]; // +0xe1
};
