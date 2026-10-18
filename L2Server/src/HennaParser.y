// CParserForHenna grammar (original L2Server/henna_parser.y).
// Bison generates HennaParser.cpp and HennaParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/HennaParser.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/Henna.h"
#include "L2Server/src/Object.h"

#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <string_view>

#include "HennaParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForHenna::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"HennaParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

CParserForHenna::CParserForHenna() : m_lexer() {}

CParserForHenna::~CParserForHenna() = default;

int CParserForHenna::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}

void CParserForHenna::yysyntaxerror() {
    char16_t message[60]{};
    FormatUtf16To(message, std::size(message), u"HennaDataChecker[%d] : syntax error", m_lexer.line + 1);
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

%token EQUAL
%token DYE_BEGIN
%token DYE_NAME
%token DYE_ID
%token DYE_ITEM_ID
%token DYE_LEVEL
%token STR
%token CON
%token DEX
%token INT_KW
%token MEN
%token WIT
%token LUC
%token CHA
%token DYE_SKILL
%token NEED_COUNT
%token WEAR_FEE
%token CANCEL_COUNT
%token CANCEL_FEE
%token WEAR_CLASS
%token DYE_PREMIUM
%token DYE_PERIOD
%token DYE_UNEQUIP_SKILL
%token DYE_COND
%token HC_IS_IN_BATTLE_FIELD
%token HC_IS_IN_CASTLE_WAR
%token HC_IS_IN_AGIT_WAR
%token HC_IS_IN_FORTRESS_SIEGE
%token HC_IS_IN_OLYMPIAD
%token HC_IS_IN_PLEDGE_GAME
%token DYE_END
%token NAME
%token IDENTIFIER
%token INTEGER
%start dye_list

%%

dye_list
    : dye_list dye
    | %empty
    ;

dye
    : DYE_BEGIN DYE_NAME EQUAL NAME
      {
          const auto setStringField = [this](ObjectFieldType field, const char16_t* value) {
              const std::wstring hostValue = target_utf16::ToHostCodeUnits(value);
              m_pHenna->Set(field, hostValue.c_str());
          };
          m_pHenna = new CHenna;
          setStringField(HENNA_NAME, grammar::SemanticPointer<char16_t>($4));
      }
      dye_id_def_list DYE_END
      {
          g_HennaDB.Add(m_pHenna);
          m_pHenna = nullptr;
      }
    ;

dye_id_def_list
    : dye_id_def_list dye_id_def
    | %empty
    ;

dye_id_def
    : DYE_ID EQUAL INTEGER
      { m_pHenna->Set(HENNA_INT_ID, grammar::SemanticInt32($3)); }
    | DYE_ITEM_ID EQUAL INTEGER
      { m_pHenna->Set(HENNA_ITEM_ID, grammar::SemanticInt32($3)); }
    | DYE_LEVEL EQUAL INTEGER
      { m_pHenna->Set(HENNA_LEVEL, grammar::SemanticInt32($3)); }
    | STR EQUAL INTEGER
      { m_pHenna->Set(HENNA_STR, grammar::SemanticInt32($3)); }
    | CON EQUAL INTEGER
      { m_pHenna->Set(HENNA_CON, grammar::SemanticInt32($3)); }
    | DEX EQUAL INTEGER
      { m_pHenna->Set(HENNA_DEX, grammar::SemanticInt32($3)); }
    | INT_KW EQUAL INTEGER
      { m_pHenna->Set(HENNA_INT, grammar::SemanticInt32($3)); }
    | MEN EQUAL INTEGER
      { m_pHenna->Set(HENNA_MEN, grammar::SemanticInt32($3)); }
    | WIT EQUAL INTEGER
      { m_pHenna->Set(HENNA_WIT, grammar::SemanticInt32($3)); }
    | LUC EQUAL INTEGER
      { m_pHenna->Set(HENNA_LUC, grammar::SemanticInt32($3)); }
    | CHA EQUAL INTEGER
      { m_pHenna->Set(HENNA_CHA, grammar::SemanticInt32($3)); }
    | NEED_COUNT EQUAL INTEGER
      { m_pHenna->Set(HENNA_NEED_COUNT, grammar::SemanticInt32($3)); }
    | WEAR_FEE EQUAL INTEGER
      { m_pHenna->Set(HENNA_WERA_FEE, grammar::SemanticInt32($3)); }
    | CANCEL_COUNT EQUAL INTEGER
      { m_pHenna->Set(HENNA_CANCEL_COUNT, grammar::SemanticInt32($3)); }
    | CANCEL_FEE EQUAL INTEGER
      { m_pHenna->Set(HENNA_CANCEL_FEE, grammar::SemanticInt32($3)); }
    | WEAR_CLASS EQUAL '{' n8 '}'
      {
          m_pHenna->Set(HENNA_CAN_WEAR_CLASS, grammar::SemanticPointer<std::vector<int>>($4));
      }
    | DYE_SKILL EQUAL '{' n9 '}'
      {
          const auto setStringField = [this](ObjectFieldType field, const char16_t* value) {
              const std::wstring hostValue = target_utf16::ToHostCodeUnits(value);
              m_pHenna->Set(field, hostValue.c_str());
          };
          auto* const values = grammar::SemanticPointer<std::vector<char16_t*>>($4);
          for (char16_t* const value : *values) {
              setStringField(HENNA_SKILL, value);
              std::free(value);
          }
          delete values;
      }
    | DYE_PREMIUM EQUAL INTEGER
      { m_pHenna->Set(HENNA_PREMIUM, grammar::SemanticInt32($3)); }
    | DYE_PERIOD EQUAL INTEGER
      {
          const std::int32_t period = grammar::SemanticInt32($3);
          if (period >= 1) {
              m_pHenna->Set(HENNA_PERIOD, period);
          }
      }
    | DYE_UNEQUIP_SKILL EQUAL '{' n9 '}'
      {
          const auto setStringField = [this](ObjectFieldType field, const char16_t* value) {
              const std::wstring hostValue = target_utf16::ToHostCodeUnits(value);
              m_pHenna->Set(field, hostValue.c_str());
          };
          auto* const values = grammar::SemanticPointer<std::vector<char16_t*>>($4);
          for (char16_t* const value : *values) {
              setStringField(HENNA_UNEQUIP_SKILL, value);
              std::free(value);
          }
          delete values;
      }
    | DYE_COND EQUAL '{' n6 '}'
    ;

n6
    : n6 ';' '{' hc_is_in_battle_field_def '}'
    | '{' hc_is_in_battle_field_def '}'
    | %empty
    ;

hc_is_in_battle_field_def
    : HC_IS_IN_BATTLE_FIELD
      { m_pHenna->AddHennaCondition(new CHennaCondition_is_in_battle_field); }
    | HC_IS_IN_CASTLE_WAR
      { m_pHenna->AddHennaCondition(new CHennaCondition_is_in_castle_war); }
    | HC_IS_IN_AGIT_WAR
      { m_pHenna->AddHennaCondition(new CHennaCondition_hc_is_in_agit_war); }
    | HC_IS_IN_FORTRESS_SIEGE
      {
          m_pHenna->AddHennaCondition(new CHennaCondition_is_in_fortress_siege);
      }
    | HC_IS_IN_OLYMPIAD
      { m_pHenna->AddHennaCondition(new CHennaCondition_is_in_olympiad); }
    | HC_IS_IN_PLEDGE_GAME
      { m_pHenna->AddHennaCondition(new CHennaCondition_is_in_pledge_game); }
    ;

n8
    : n8 ';' INTEGER
      {
          auto* const values = grammar::SemanticPointer<std::vector<int>>($1);
          values->push_back(grammar::SemanticInt32($3));
          $$ = grammar::PointerSemantic(values);
      }
    | INTEGER
      {
          auto* const values = new std::vector<int>;
          values->push_back(grammar::SemanticInt32($1));
          $$ = grammar::PointerSemantic(values);
      }
    ;

n9
    : n9 ';' IDENTIFIER
      {
          auto* const values = grammar::SemanticPointer<std::vector<char16_t*>>($1);
          values->push_back(grammar::SemanticPointer<char16_t>($3));
          $$ = grammar::PointerSemantic(values);
      }
    | IDENTIFIER
      {
          auto* const values = new std::vector<char16_t*>;
          values->push_back(grammar::SemanticPointer<char16_t>($1));
          $$ = grammar::PointerSemantic(values);
      }
    | %empty
      {
          $$ = grammar::PointerSemantic(new std::vector<char16_t*>);
      }
    ;

%%
