#pragma once

#include "UseCountItemLexer.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CItemConstInfo;

class CParserForUseCountItem {
public:
    CParserForUseCountItem();
    ~CParserForUseCountItem();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

private:
    std::vector<char16_t*>* m_pStringVector; // +0x0f0
    std::u16string* m_pString; // +0x0f8

public:
    CLexerForUseCountItem m_lexer; // +0x100
    CItemConstInfo* pItemConstInfo; // +0x1d8
    std::int32_t nFixedNumber; // +0x1e0

private:
    [[maybe_unused]] std::uint32_t m_alignment1e4To1e8;
};
