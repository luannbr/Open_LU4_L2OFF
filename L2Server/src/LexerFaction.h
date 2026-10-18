#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>
#include <cstdint>

class CFactionManager;

class CLexerForFaction : public grammar::UTF16Scanner {
public:
    CLexerForFaction();
    ~CLexerForFaction();
    int Lex(grammar::SemanticBits* lval);


private:
    friend class CFactionManager;

    char16_t* m_inputBuffer; // +0x0c0
    std::uint64_t m_inputBufferOffset; // +0x0c8
    std::uint64_t m_inputBufferLength; // +0x0d0
    int comment_count; // +0x0d8
    int line; // +0x0dc
};
