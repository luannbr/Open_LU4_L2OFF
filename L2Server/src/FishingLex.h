#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <array>
#include <cstddef>

class CParserForFishing;

class CLexerForFishing : public grammar::UTF16Scanner {
public:
    CLexerForFishing();
    ~CLexerForFishing();

    int Create(char16_t* fileBuffer, int fileSize);
    int Lex(grammar::SemanticBits* lval);
    int getLine();

private:
    char16_t* m_FileBuf; // +0xc0
    int m_nFilePointer; // +0xc8
    int m_nFileSize; // +0xcc

public:
    int line; // +0xd0
    [[maybe_unused]] std::array<std::byte, 0x04> m_tailPadding00d4;
};
