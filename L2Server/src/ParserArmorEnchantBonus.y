// CParserForArmorEnchantBonus grammar (original L2Server/parser_armorenchantbonus.y).
// Bison generates ParserArmorEnchantBonus.cpp and ParserArmorEnchantBonusTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserArmorEnchantBonus.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Utils.h"

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string_view>

#include "ParserArmorEnchantBonusTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForArmorEnchantBonus::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserArmorEnchantBonusTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
extern int g_nEnchantedArmorBonus[][20];
extern int g_nOnepieceFactor;

namespace {

int nGrade = 0;
int nLevel = 0;

} // namespace

CParserForArmorEnchantBonus::CParserForArmorEnchantBonus() : m_lexer() {}

CParserForArmorEnchantBonus::~CParserForArmorEnchantBonus() = default;

int CParserForArmorEnchantBonus::Create() {
    return m_lexer.Create();
}

void CParserForArmorEnchantBonus::yysyntaxerror() {
    char16_t message[60]{};
    FormatUtf16To(message, std::size(message), u"ArmorEnchantBonus[%d] : syntax error", m_lexer.line + 1);
    // Log.Add(LOG_ERROR, message)
    // Log.Add(LOG_ERROR, L"%ls", message)
    L2_LOG_ERROR(Log, u"%ls", message);
    // Noise: The error is already logged above. Only Windows needs a modal copy.
    // PopUpWindow(message)
#if defined(_WIN32)
    PopUpWindow(L"%ls", message);
#endif
}
}

%token INTEGER
%token ONEPIECE_FACTOR
%token BONUS_GRADE_NONE
%token BONUS_GRADE_D
%token BONUS_GRADE_C
%token BONUS_GRADE_B
%token BONUS_GRADE_A
%token BONUS_GRADE_S
%token BONUS_GRADE_S80
%token BONUS_GRADE_S84
%token BONUS_GRADE_R
%token BONUS_GRADE_R95
%token BONUS_GRADE_R99
%start n1

%%

n1
    : bonus_grade_none_def_list
    ;

bonus_grade_none_def_list
    : bonus_grade_none_def_list bonus_grade_none_def
    | onepiece_factor_def
    ;

bonus_grade_none_def
    : BONUS_GRADE_NONE '=' '{' n4 '}'
      { nGrade = 1; }
    | BONUS_GRADE_D '=' '{' n4 '}'
      { nGrade = 2; }
    | BONUS_GRADE_C '=' '{' n4 '}'
      { nGrade = 3; }
    | BONUS_GRADE_B '=' '{' n4 '}'
      { nGrade = 4; }
    | BONUS_GRADE_A '=' '{' n4 '}'
      { nGrade = 5; }
    | BONUS_GRADE_S '=' '{' n4 '}'
      { nGrade = 6; }
    | BONUS_GRADE_S80 '=' '{' n4 '}'
      { nGrade = 7; }
    | BONUS_GRADE_S84 '=' '{' n4 '}'
      { nGrade = 8; }
    | BONUS_GRADE_R '=' '{' n4 '}'
      { nGrade = 9; }
    | BONUS_GRADE_R95 '=' '{' n4 '}'
      { nGrade = 10; }
    | BONUS_GRADE_R99 '=' '{' n4 '}'
      { nGrade = 11; }
    ;

n4
    : n4 ';' INTEGER
      {
          g_nEnchantedArmorBonus[nGrade][nLevel++] = $3;
          if (nLevel > 20) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Too big enchant level for armor bonus!")
              L2_LOG_ERROR(Log, u"Too big enchant level for armor bonus!");
              --nLevel;
          }
      }
    | INTEGER
      {
          nLevel = 1;
          g_nEnchantedArmorBonus[nGrade][0] = $1;
      }
    ;

onepiece_factor_def
    : ONEPIECE_FACTOR '=' INTEGER
      { g_nOnepieceFactor = $3; }
    ;

%%
