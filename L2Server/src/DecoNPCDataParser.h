#pragma once

#include "DecoNPCDataLexer.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>

class CDecoNpcData;

class CParserForDecoNpcData {
public:
    CParserForDecoNpcData();
    ~CParserForDecoNpcData();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

private:

    [[maybe_unused]] std::vector<char16_t*>* m_pStringVector; // +0xf0
    [[maybe_unused]] std::u16string* m_pString; // +0xf8
    CDecoNpcData* m_pDecoNpcData; // +0x100

public:
    CLexerForDecoNpcData m_lexer; // +0x108
};
