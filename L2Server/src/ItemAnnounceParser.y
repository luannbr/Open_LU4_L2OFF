// CParserForItemAnnounceInfo grammar.
// Bison generates ItemAnnounceParser.cpp and ItemAnnounceParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ItemAnnounceParser.h"

#include "L2Server/src/ItemAnnounceInfo.h"

#include <cstdint>
#include <cstring>
#include <string_view>

#include "ItemAnnounceParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForItemAnnounceInfo::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ItemAnnounceParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

CParserForItemAnnounceInfo::CParserForItemAnnounceInfo() : m_lexer() {
    m_pStringVector = nullptr;
    m_pString = nullptr;
    m_pItemAnnounceItemInfo = nullptr;
    m_pItemAnnounceRandomBoxInfo = nullptr;
}

CParserForItemAnnounceInfo::~CParserForItemAnnounceInfo() = default;

int CParserForItemAnnounceInfo::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}
}

%token EQUAL
%token SEMICOLON
%token LBRACE
%token RBRACE
%token ITEM_BEGIN
%token ITEM_ID
%token ANNOUNCE_LEVEL
%token ITEM_END
%token RANDOM_BOX_BEGIN
%token ANNOUNCE_LEVEL_MIN
%token FIXED_ITEMS
%token RANDOM_BOX_END
%token NAME
%token INTEGER
%start n1

%%

n1
    : item_list random_box_list
    ;

item_list
    : item_list item
    | %empty
    ;

item
    : ITEM_BEGIN
      { m_pItemAnnounceItemInfo = new ItemAnnounceItemInfo; }
      item_id_def_list ITEM_END
      {
          if (m_pItemAnnounceItemInfo != nullptr) {
              ItemAnnounceDB::Loader::AddItemInfo(m_pItemAnnounceItemInfo);
              m_pItemAnnounceItemInfo = nullptr;
          }
      }
    ;

item_id_def_list
    : item_id_def_list item_id_def
    | %empty
    ;

item_id_def
    : ITEM_ID EQUAL INTEGER
      {
          if (m_pItemAnnounceItemInfo != nullptr) {
              m_pItemAnnounceItemInfo->m_idItem = grammar::SemanticInt32($3);
          }
      }
    | ANNOUNCE_LEVEL EQUAL INTEGER
      {
          if (m_pItemAnnounceItemInfo != nullptr) {
              m_pItemAnnounceItemInfo->m_nAnnounceLevel = grammar::SemanticInt32($3);
          }
      }
    ;

random_box_list
    : random_box_list random_box
    | %empty
    ;

random_box
    : RANDOM_BOX_BEGIN
      { m_pItemAnnounceRandomBoxInfo = new ItemAnnounceRandomBoxInfo; }
      item_id_def_2_list RANDOM_BOX_END
      {
          if (m_pItemAnnounceRandomBoxInfo != nullptr) {
              ItemAnnounceDB::Loader::AddRandomBoxInfo(m_pItemAnnounceRandomBoxInfo);
              m_pItemAnnounceRandomBoxInfo = nullptr;
          }
      }
    ;

item_id_def_2_list
    : item_id_def_2_list item_id_def_2
    | %empty
    ;

item_id_def_2
    : ITEM_ID EQUAL INTEGER
      {
          if (m_pItemAnnounceRandomBoxInfo != nullptr) {
              m_pItemAnnounceRandomBoxInfo->m_idItem = grammar::SemanticInt32($3);
          }
      }
    | ANNOUNCE_LEVEL_MIN EQUAL INTEGER
      {
          if (m_pItemAnnounceRandomBoxInfo != nullptr) {
              m_pItemAnnounceRandomBoxInfo->m_nAnnounceLevelMin = grammar::SemanticInt32($3);
          }
      }
      FIXED_ITEMS EQUAL lbrace_def
    ;

lbrace_def
    : LBRACE n14 RBRACE
    | LBRACE RBRACE
    ;

n14
    : n14 SEMICOLON integer_def
    | integer_def
    ;

integer_def
    : INTEGER
      {
          if (m_pItemAnnounceRandomBoxInfo != nullptr) {
              m_pItemAnnounceRandomBoxInfo->m_fixedItems.push_back(grammar::SemanticInt32($1));
          }
      }
    ;

%%
