#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>
#include <cstdint>

class CParserForSetting;

// The scanner is LexerSetting.re. The offset comments give the layout in the
// original image.
class CLexerForSetting final : public grammar::UTF16Scanner {
public:
    CLexerForSetting();
    ~CLexerForSetting();

    int Create(char16_t* fileBuffer, int fileSize);
    int Lex(grammar::SemanticBits* lval);

private:
    char16_t* m_FileBuf; // +0xc0
    std::int32_t m_nFilePointer; // +0xc8
    std::int32_t m_nFileSize; // +0xcc

public:
    std::int32_t line; // +0xd0
};
