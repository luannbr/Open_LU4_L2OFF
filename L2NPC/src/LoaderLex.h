#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <array>
#include <cstddef>


// L2NPC/loader_lex.h. The scanner is LoaderLex.re. The offset comments give
// the layout in the original image.
class CLexerForLoader : public grammar::UTF16Scanner {
public:
    CLexerForLoader();
    ~CLexerForLoader();

    int create(char16_t* fileName);
    int Lex(grammar::SemanticBits* lval);

private:
    int yygetchar();

public:
    // The current constructor leaves the input carrier and
    // byte cursor untouched. create()/the first yygetchar() publish them
    // before use.
    char* m_FileBuf; // +0xc0
    int m_nFilePointer; // +0xc8
    int m_nFileSize; // +0xcc
    int line; // +0xd0
    bool m_bFirstTime; // +0xd4

private:
    [[maybe_unused]] std::array<std::byte, 3> m_firstTimePadding; // +0xd5, uninitialized

public:
    int mWordSize; // +0xd8
};
