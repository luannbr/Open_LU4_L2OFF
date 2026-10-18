#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>
#include <cstdint>

class PetDataParser;

class PetDataLexer : public grammar::UTF16Scanner {
public:
    PetDataLexer();
    ~PetDataLexer();

    int Create(char16_t* fileBuffer, int fileSize);
    int Lex(grammar::SemanticBits* lval);
    const char16_t* GetFileName() const;

private:
    char16_t* m_FileBuf; // +0xc0
    char16_t* m_pRawFileBuf; // +0xc8
    std::int32_t m_nFilePointer; // +0xd0
    std::int32_t m_nFileSize; // +0xd4
    char16_t m_FileName[132]; // +0xd8
};
