#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>
#include <cstdint>

class CParserForAirShip;

class CLexerForAirShip : public grammar::UTF16Scanner {
public:
    CLexerForAirShip();
    ~CLexerForAirShip();

    int Create(char16_t* buffer, std::int32_t size);
    int Lex(grammar::SemanticBits* lval);

    char16_t* m_FileBuf; // +0xc0
    std::int32_t m_nFilePointer; // +0xc8
    std::int32_t m_nFileSize; // +0xcc
    std::int32_t comment_count; // +0xd0
    std::int32_t line; // +0xd4
};
