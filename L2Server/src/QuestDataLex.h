#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CParserForQuestData;

// The scanner is QuestDataLex.re. The offset comments give the layout in the
// original image.
class CLexerForQuestData : public grammar::UTF16Scanner {
public:
    CLexerForQuestData();
    ~CLexerForQuestData();

    int Create(char16_t* fileBuffer, int fileSize);
    int Lex(grammar::SemanticBits* lval);
    int yygetchar();

    int ErrorLine() const noexcept;
    int ErrorAt() const noexcept;
    char16_t* ErrorText() const noexcept;

private:
    char16_t* m_FileBuf; // +0x0c0
    std::int32_t m_nFilePointer; // +0x0c8
    std::int32_t m_nFileSize; // +0x0cc
    std::int32_t m_line; // +0x0d0
    std::int32_t m_charCount; // +0x0d4
    std::int32_t comment_count; // +0x0d8
    std::int32_t string_mode; // +0x0dc
    std::u16string qstring; // +0x0e0
    char16_t m_LastInput[100]; // +0x108
    std::int32_t m_nLastInput; // +0x1d0
    [[maybe_unused]] std::byte m_targetPadding1d4[4];
};
