#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>

class CParserForRecipe;

class CLexerForRecipe : public grammar::UTF16Scanner {
public:
    CLexerForRecipe();
    ~CLexerForRecipe();

    int Create(char16_t* fileBuffer, int fileSize);
    int Lex(grammar::SemanticBits* lval);
    int getLine();

private:
    char16_t* m_FileBuf; // +0xc0
    int m_nFilePointer; // +0xc8
    int m_nFileSize; // +0xcc

public:
    int line; // +0xd0
    int comment_count; // +0xd4
};
