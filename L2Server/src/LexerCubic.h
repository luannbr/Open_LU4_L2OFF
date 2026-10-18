#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>
#include <cstdint>

class CParserForCubic;

class CLexerForCubic : public grammar::UTF16Scanner {
public:
    CLexerForCubic();
    ~CLexerForCubic();

    int Create(char16_t* fileBuffer, int fileSize);
    int Lex(grammar::SemanticBits* lval);

private:
    char16_t* m_FileBuf; // +0x0c0
    std::int32_t m_nFilePointer; // +0x0c8
    std::int32_t m_nFileSize; // +0x0cc

public:
    std::int32_t line; // +0x0d0
};
