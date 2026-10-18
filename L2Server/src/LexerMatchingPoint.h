#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>
#include <cstdint>

class CMatchingPointDB;

class CLexerForMatchingPoint : public grammar::UTF16Scanner {
public:
    CLexerForMatchingPoint();
    ~CLexerForMatchingPoint();
    int Lex(grammar::SemanticBits* lval);


private:
    friend class CMatchingPointDB;

    char16_t* m_inputBuffer; // +0xc0
    std::uint64_t m_inputBufferOffset; // +0xc8
    std::uint64_t m_inputBufferLength; // +0xd0
};
