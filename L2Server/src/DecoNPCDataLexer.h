#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>

class CParserForDecoNpcData;

class CLexerForDecoNpcData : public grammar::UTF16Scanner {
public:
    CLexerForDecoNpcData();
    ~CLexerForDecoNpcData();

    int Create(char16_t* fileBuffer, int fileSize);
    int Lex(grammar::SemanticBits* lval);

private:
    char16_t* m_FileBuf; // +0xc0
    int m_nFilePointer; // +0xc8
    int m_nFileSize; // +0xcc

public:
    int line; // +0xd0
    int comment_count; // +0xd4
};
