#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>

class CParserForEnchantOption;

class CLexerForEnchantOption : public grammar::UTF16Scanner {
public:
    CLexerForEnchantOption();
    ~CLexerForEnchantOption();

    int Create(char16_t* fileBuffer, int fileSize);
    int Lex(grammar::SemanticBits* lval);

private:
    char16_t* m_FileBuf; // +0xc0
    int m_nFilePointer; // +0xc8
    int m_nFileSize; // +0xcc

public:
    int comment_count; // +0xd0
    int line; // +0xd4
};
