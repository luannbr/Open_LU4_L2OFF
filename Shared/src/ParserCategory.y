// categorydata.txt grammar (original Shared/parser_category.y).
// Bison generates ParserCategory.cpp and ParserCategoryTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "Shared/src/ParserCategory.h"

#include "Shared/src/CategoryDataDB.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Utils.h"

#include <cstdint>
#include <cstring>
#include <string_view>

#include "ParserCategoryTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForCategory::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserCategoryTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

// Current L2NPC.exe 0x00401e00 and Cached.exe 0x004016a0 use one
// process-global category counter. It is zero-initialized once and
// is not reset by Load().
int g_CurrentCategory = 0;

} // namespace

CParserForCategory::CParserForCategory() {
    // Current Cached.exe 0x00401578.
    m_pCategoryDataDB = nullptr;
    m_bOnlyPrecompiledHeader = false;
}

CParserForCategory::~CParserForCategory() {
    // Current Cached.exe 0x0040131c.
}

int CParserForCategory::Create(CCategoryDataDB* categoryDataDB, char16_t* text, int codeUnitCount) {
    // Current L2NPC.exe 0x00401c14 / Cached.exe 0x00401490 store the owner,
    // then pass the UTF-16 image to the embedded lexer.
    m_pCategoryDataDB = categoryDataDB;
    return m_lexer.Create(this, text, codeUnitCount);
}

void CParserForCategory::yysyntaxerror() {
    // Current Cached.exe vtable slot +0x70, 0x00401508.
    char16_t message[60]{};
    FormatUtf16To(message, std::size(message), u"CategoryDataChecker[%d] : syntax error", m_lexer.getLine() + 1);
    // Log.Add(
    //     LOG_ERROR,
    //     message)
    // Log.Add(LOG_ERROR, L"%ls", message)
    // L2_LOG_ERROR(Log, L"%ls", message)
    // L2_LOG_ERROR(Log,
    //     L"[category data] Syntax error at line %d.",
    //     m_lexer.getLine() + 1)
    L2_LOG_ERROR(Log, u"[category data] Syntax error in categorydata.txt at line %d.", m_lexer.getLine() + 1);
    // Noise: The error is already logged above. Only Windows needs a modal copy.
    // PopUpWindow(message)
#if defined(_WIN32)
    PopUpWindow(L"%ls", message);
#endif
}
}

%token CATEGORY_DEFINE_BEGIN CATEGORY_DEFINE_END CATEGORY NAME
%token INTEGER BRACKETED_NAME

%%

category_file
    : category_list
    ;

category_list
    : category_list category_entry
    | category_entry
    ;

category_entry
    : category_define
    ;

category_define
    : CATEGORY_DEFINE_BEGIN { } category_name CATEGORY_DEFINE_END
    ;

category_name
    : NAME '=' BRACKETED_NAME { } category_members
      { ++g_CurrentCategory; }
    ;

category_members
    : CATEGORY '=' '{' integer_list '}'
    ;

integer_list
    : integer_list ';' INTEGER
      { CCategoryDataDB::Inst().AddObjectInCategory(g_CurrentCategory, $3); }
    | INTEGER
      { CCategoryDataDB::Inst().AddObjectInCategory(g_CurrentCategory, $1); }
    ;

%%
