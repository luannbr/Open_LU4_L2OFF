#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>
#include <cstdint>

class CParserForExDrop;

class CLexerForExDrop : public grammar::UTF16Scanner {
public:
    CLexerForExDrop();
    ~CLexerForExDrop();

    int Create(CParserForExDrop* parser, char16_t* fileBuffer, int fileSize);
    int Lex(grammar::SemanticBits* lval);
    int getLine() const { return line; }

private:
    char16_t* m_FileBuf; // +0xc0
    char* m_pRawFileBuf; // +0xc8
    std::int32_t m_nFilePointer; // +0xd0
    std::int32_t m_nFileSize; // +0xd4
    bool m_bOnlyPrecompiledHeader; // +0xd8
    [[maybe_unused]] std::byte m_alignmentD9ToDC[3]; // +0xd9

public:
    std::int32_t line; // +0xdc
};
