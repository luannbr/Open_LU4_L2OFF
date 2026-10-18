#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>
#include <cstdint>

class CParserForCursedWeapon;

class CLexerForCursedWeapon : public grammar::UTF16Scanner {
public:
    CLexerForCursedWeapon();
    ~CLexerForCursedWeapon();

    int Create(char16_t* fileBuffer, int fileSize);
    int Lex(grammar::SemanticBits* lval);

private:
    char16_t* m_FileBuf; // +0x0c0
    std::int32_t m_nFilePointer; // +0x0c8
    std::int32_t m_nFileSize; // +0x0cc

public:
    std::int32_t line; // +0x0d0
};
