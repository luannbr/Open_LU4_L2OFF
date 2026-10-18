#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>
#include <cstdint>

class CParserForMultiSell;

class CLexerForMultiSell : public grammar::UTF16Scanner {
public:
    CLexerForMultiSell();
    ~CLexerForMultiSell();

    int Create();
    int Lex(grammar::SemanticBits* lval);
    int Create(char16_t* fileBuffer, int fileSize);

private:
    char16_t* m_FileBuf; // +0xc0
    char* m_RawFileBuf; // +0xc8
    std::int32_t m_FilePointer; // +0xd0
    std::int32_t m_FileSize; // +0xd4

public:
    std::int32_t line; // +0xd8
    std::int32_t comment_count; // +0xdc
};
