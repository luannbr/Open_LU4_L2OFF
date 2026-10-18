#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>
#include <cstdint>

class CParserForDoor;

class CLexerForDoor : public grammar::UTF16Scanner {
public:
    CLexerForDoor();
    ~CLexerForDoor();

    int Create(char16_t* fileBuffer, int fileSize);
    int Lex(grammar::SemanticBits* lval);

private:
    char16_t* m_FileBuf; // +0xc0
    char* m_pRawFileBuf; // +0xc8
    std::int32_t m_nFilePointer; // +0xd0
    std::int32_t m_nFileSize; // +0xd4

public:
    std::int32_t line; // +0xd8
    bool m_bFirstTime; // +0xdc
    [[maybe_unused]] std::byte m_alignment0ddTo0e0[3];
};
