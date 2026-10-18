#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CCategoryDataDB;

class CParserForCategory;

// Shared/lexer_category.h. The scanner is LexerCategory.re. The offset
// comments give the layout in the original image. The added only-PCH flag at
// +0xd8 is newer than the adjacent PDB.
class CLexerForCategory : public grammar::UTF16Scanner {
public:
    CLexerForCategory();
    ~CLexerForCategory();

    int Create(CParserForCategory* parser, char16_t* text, int codeUnitCount);
    int Lex(grammar::SemanticBits* lval);
    int getLine() const noexcept;

private:
    char16_t* m_FileBuf = nullptr; // +0xc0
    char* m_pRawFileBuf = nullptr; // +0xc8
    int m_nFilePointer = 0; // +0xd0
    int m_nFileSize = 0; // +0xd4
    bool m_bOnlyPrecompiledHeader = false; // +0xd8
    [[maybe_unused]] std::array<std::byte, 3> m_onlyPchPadding{}; // +0xd9
    int line = 0; // +0xdc
};
