// CParserForUseCountItem grammar.
// Bison generates UseCountItemParser.cpp and UseCountItemParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/UseCountItemParser.h"

#include "L2Server/src/Item.h"

#include <cstdint>
#include <cstring>
#include <string_view>

#include "UseCountItemParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForUseCountItem::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"UseCountItemParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

CParserForUseCountItem::CParserForUseCountItem() : m_lexer() {
    m_pStringVector = nullptr;
    m_pString = nullptr;
    pItemConstInfo = nullptr;
    nFixedNumber = -1;
}

CParserForUseCountItem::~CParserForUseCountItem() = default;

int CParserForUseCountItem::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}
}

%token EQUAL
%token USE_COUNT_ITEM_BEGIN
%token USE_COUNT_ITEM_END
%token ITEM_ID
%token MAX_USE_COUNT
%token CREATE_ITEMS_LIST
%token FIXED_CREATE_ITEMS_LIST
%token INTEGER
// The original grammar has these conflicts. Bison resolves them as the
// original tables did. A shift wins over a reduce.
%expect 5
%start n1

%%

n1
    : use_count_item_list
    ;

use_count_item_list
    : use_count_item_list use_count_item
    | %empty
    ;

use_count_item
    : USE_COUNT_ITEM_BEGIN { } item_id_def_list USE_COUNT_ITEM_END
      { pItemConstInfo = nullptr; }
    ;

item_id_def_list
    : item_id_def_list item_id_def
    | %empty
    ;

item_id_def
    : ITEM_ID EQUAL INTEGER
      {
          pItemConstInfo = g_ItemConstInfoDB.GetItemConstInfo(grammar::SemanticInt32($3));
      }
    | MAX_USE_COUNT EQUAL INTEGER
      {
          if (pItemConstInfo != nullptr) {
              pItemConstInfo->m_cMaxUseCount = static_cast<std::int16_t>(grammar::SemanticInt32($3));
          }
      }
    | CREATE_ITEMS_LIST EQUAL n7
    | FIXED_CREATE_ITEMS_LIST EQUAL n10
    | %empty
    ;

n7
    : '{' n8 '}'
    ;

n8
    : n8 ';' n9
    | n9
    | %empty
    ;

n9
    : '{' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          if (pItemConstInfo != nullptr) {
              pItemConstInfo->m_vCreateItemsInUseCount.push_back(
                  CreateItemStructInUseCount{grammar::SemanticInt32($2), grammar::SemanticInt64($4),
                      static_cast<double>(grammar::SemanticInt32($6))}
              );
          }
      }
    ;

n10
    : n10 ';' n11
    | n11
    | %empty
    ;

n11
    : '{' INTEGER
      { nFixedNumber = grammar::SemanticInt32($2); }
      ';' '{' n13 '}' '}'
      { nFixedNumber = -1; }
    ;

n13
    : n13 ';' n14
    | n14
    | %empty
    ;

n14
    : '{' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          if (pItemConstInfo != nullptr && nFixedNumber != -1) {
              pItemConstInfo->AddFixedCreateItemsInUseCount(
                  static_cast<std::int16_t>(nFixedNumber),
                  CreateItemStructInUseCount{grammar::SemanticInt32($2), grammar::SemanticInt64($4),
                      static_cast<double>(grammar::SemanticInt32($6))}
              );
          }
      }
    ;

// The original tables have rule 27 that no parser state reduces.
// It is left out.

%%
