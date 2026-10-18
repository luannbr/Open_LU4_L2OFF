#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>
#include <cstdint>

class CParserForAction;

class CLexerForAction : public grammar::UTF16Scanner {
public:
    CLexerForAction();
    ~CLexerForAction();

    int Create(char16_t* fileBuffer, int fileSize);
    int Lex(grammar::SemanticBits* lval);

private:
    char16_t* m_FileBuf; // +0x0c0
    char* m_pRawFileBuf; // +0x0c8
    std::int32_t m_nFilePointer; // +0x0d0
    std::int32_t m_nFileSize; // +0x0d4

public:
    std::int32_t comment_count; // +0x0d8
    std::int32_t line; // +0x0dc
};
