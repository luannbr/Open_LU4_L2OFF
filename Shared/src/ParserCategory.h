#pragma once

#include "LexerCategory.h"

#include <array>
#include <cstddef>


// Shared/parser_category.h. The grammar is ParserCategory.y. Bison generates
// yyparse as a member, so the grammar actions use these fields. The offset
// comments give the layout in the original image.
class CParserForCategory {
public:
    CParserForCategory();
    ~CParserForCategory();

    int Create(CCategoryDataDB* categoryDataDB, char16_t* text, int codeUnitCount);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

    CLexerForCategory m_lexer; // +0x0f0
    CCategoryDataDB* m_pCategoryDataDB; // +0x1d0
    bool m_bOnlyPrecompiledHeader; // +0x1d8

private:
    [[maybe_unused]] std::array<std::byte, 7> m_onlyPchPadding; // +0x1d9
};
