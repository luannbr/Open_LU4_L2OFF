#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>
#include <cstdint>

class CParserForVariation;

class CLexerForVariation : public grammar::UTF16Scanner {
public:
    CLexerForVariation();
    ~CLexerForVariation();

    int Create();
    int Lex(grammar::SemanticBits* lval);

    char16_t* m_FileBuf; // +0x0c0
    char* m_pRawFileBuf; // +0x0c8
    std::int32_t m_nFilePointer; // +0x0d0
    std::int32_t m_nFileSize; // +0x0d4
    std::int32_t line; // +0x0d8
    std::int32_t comment_count; // +0x0dc
};
