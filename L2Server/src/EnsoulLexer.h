#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>
#include <cstdint>

class CParserForEnsoul;

class CLexerForEnsoul : public grammar::UTF16Scanner {
public:
    CLexerForEnsoul();
    ~CLexerForEnsoul();

    int Create(CParserForEnsoul* parser, char16_t* fileBuffer, int fileSize);
    int Lex(grammar::SemanticBits* lval);
    void Initialize();
    int GetLine();

private:
    char16_t* m_FileBuf; // +0xc0
    [[maybe_unused]] char* m_pRawFileBuf; // +0xc8
    std::int32_t m_nFilePointer; // +0xd0
    std::int32_t m_nFileSize; // +0xd4
    bool m_bOnlyPrecompiledHeader; // +0xd8

public:
    std::int32_t m_nLine; // +0xdc
    std::int32_t m_nCommentCount; // +0xe0
    bool m_bReadFirstCharacter; // +0xe4
};
