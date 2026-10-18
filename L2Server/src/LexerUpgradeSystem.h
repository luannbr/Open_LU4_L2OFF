#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>
#include <cstdint>

class CParserForUpgradeSystem;

class CLexerForUpgradeSystem : public grammar::UTF16Scanner {
public:
    CLexerForUpgradeSystem();
    ~CLexerForUpgradeSystem();

    int Create(char16_t* fileBuffer, int fileSize);
    int Lex(grammar::SemanticBits* lval);

private:
    char16_t* m_FileBuf; // +0xc0
    char* m_pRawFileBuf; // +0xc8
    std::int32_t m_nFilePointer; // +0xd0
    std::int32_t m_nFileSize; // +0xd4

public:
    std::int32_t line; // +0xd8
    std::int32_t comment_count; // +0xdc
    bool m_bOnlyPrecompiledHeader; // +0xe0
};
