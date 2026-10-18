#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>

class CParserForCardUpDownGame;

class CLexerForCardUpDownGame : public grammar::UTF16Scanner {
public:
    CLexerForCardUpDownGame();
    ~CLexerForCardUpDownGame();

    int Create(char16_t* fileBuffer, int fileSize);
    int Lex(grammar::SemanticBits* lval);

private:
    char16_t* m_FileBuf; // +0xc0
    char* m_pRawFileBuf; // +0xc8
    int m_nFilePointer; // +0xd0
    int m_nFileSize; // +0xd4

public:
    int line; // +0xd8
    int comment_count; // +0xdc
    bool m_bOnlyPrecompiledHeader; // +0xe0
};
