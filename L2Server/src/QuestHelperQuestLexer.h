#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CParserForQuestHelperQuest;

class CLexerForQuestHelperQuest : public grammar::UTF16Scanner {
public:
    CLexerForQuestHelperQuest();
    ~CLexerForQuestHelperQuest();

    int Create(char16_t* fileBuffer, int fileSize);
    int Lex(grammar::SemanticBits* lval);
    int yygetchar();

private:
    char16_t* m_FileBuf; // +0x0c0
    std::int32_t m_nFilePointer; // +0x0c8
    std::int32_t m_nFileSize; // +0x0cc

public:
    std::int32_t line; // +0x0d0

private:
    std::int32_t comment_count; // +0x0d4
    std::int32_t string_mode; // +0x0d8
    [[maybe_unused]] std::byte m_alignment0dcTo0e0[4];
    std::u16string qstring; // +0x0e0

public:
    char16_t m_LastInput[100]; // +0x108
    std::int32_t m_nLastInput; // +0x1d0

private:
    [[maybe_unused]] std::byte m_targetPadding1d4[4];
};
