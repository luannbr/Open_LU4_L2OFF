#pragma once

#include "LexerExDrop.h"

#include <cstddef>

class ExDropItemData;

class CParserForExDrop {
public:
    CParserForExDrop();
    ~CParserForExDrop();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

    ExDropItemData* m_pExDropData; // +0xf0
    CLexerForExDrop m_lexer; // +0xf8
    int m_nLineNumber; // +0x1d8
    bool m_bOnlyPrecompiledHeader; // +0x1dc
    bool m_bReload; // +0x1dd
};
