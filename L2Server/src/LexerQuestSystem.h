#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>
#include <cstdint>

class CParserForQuestSystem;

class CLexerForQuestSystem : public grammar::UTF16Scanner {
public:
    CLexerForQuestSystem();
    ~CLexerForQuestSystem();

    int Create(char16_t* fileBuffer, int fileSize);
    int Lex(grammar::SemanticBits* lval);

private:
    char16_t* m_FileBuf; // +0xc0
    std::int32_t m_nFilePointer; // +0xc8
    std::int32_t m_nFileSize; // +0xcc
    std::int32_t comment_count; // +0xd0

public:
    std::int32_t line; // +0xd4
    std::int32_t questId; // +0xd8
};
