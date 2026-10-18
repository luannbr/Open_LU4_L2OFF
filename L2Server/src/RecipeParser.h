#pragma once

#include "RecipeLex.h"

#include <cstddef>

class CRecipeInfo;

class CParserForRecipe {
public:
    CParserForRecipe();
    ~CParserForRecipe();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

    CRecipeInfo* m_pRecipeInfo; // +0xf0
    int nProb; // +0xf8
    CLexerForRecipe m_lexer; // +0x100
};
