// CParserForDecoNpcData grammar.
// Bison generates DecoNPCDataParser.cpp and DecoNPCDataParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/DecoNPCDataParser.h"

#include "L2Server/src/DecoNpcData.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "DecoNPCDataParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForDecoNpcData::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"DecoNPCDataParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

// Exact current generated parser images at 0x00e20d10..0x00e21328.

} // namespace

CParserForDecoNpcData::CParserForDecoNpcData() :
    m_pStringVector(nullptr), m_pString(nullptr), m_pDecoNpcData(nullptr), m_lexer() {
}

CParserForDecoNpcData::~CParserForDecoNpcData() = default;

int CParserForDecoNpcData::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}
}

%token EQUAL
%token SEMICOLON
%token LBRACE
%token RBRACE
%token LPAREN
%token RPAREN
%token DECO_NPC_BEGIN
%token ID
%token NAME
%token TYPE
%token SUB_TYPE
%token LEVEL
%token PERIOD
%token COST_ADENA
%token COST_ITEM
%token NPC_ID
%token FACTION
%token RESIDENCE_GRADES
%token RESIDENCE_DOMAINS
%token CHANGEABLE
%token DECO_NPC_END
%token NAME_2
%token INTEGER
%start n1

%%

n1
    : deco_npc_list
    ;

deco_npc_list
    : deco_npc_list deco_npc
    | %empty
    ;

deco_npc
    : DECO_NPC_BEGIN
      { m_pDecoNpcData = new CDecoNpcData; }
      id_def_list DECO_NPC_END
      {
          if (m_pDecoNpcData != nullptr) {
              CDecoNpcDB::PrivateAddDecoNpc::Call(m_pDecoNpcData);
          }
          m_pDecoNpcData = nullptr;
      }
    ;

id_def_list
    : id_def_list id_def
    | %empty
    ;

id_def
    : ID EQUAL INTEGER
      {
          if (m_pDecoNpcData != nullptr) {
              m_pDecoNpcData->m_nCID = grammar::SemanticInt32($3);
          }
      }
    | NAME EQUAL NAME_2
      {
          if (m_pDecoNpcData != nullptr) {
              const auto* const source =
                  grammar::SemanticString($3);
              if (source != nullptr) {
                  std::size_t length = 0;
                  while (source[length] != u'\0') {
                      ++length;
                  }
                  if (length <= 0xff) {
                      std::copy_n(source, length + 1, m_pDecoNpcData->m_szName);
                  }
              }
          }
      }
    | TYPE EQUAL INTEGER
      {
          if (m_pDecoNpcData != nullptr) {
              m_pDecoNpcData->m_nType = grammar::SemanticInt32($3);
          }
      }
    | SUB_TYPE EQUAL INTEGER
      {
          if (m_pDecoNpcData != nullptr) {
              m_pDecoNpcData->m_nSubType = grammar::SemanticInt32($3);
          }
      }
    | LEVEL EQUAL INTEGER
      {
          if (m_pDecoNpcData != nullptr) {
              m_pDecoNpcData->m_nLevel = grammar::SemanticInt32($3);
          }
      }
    | PERIOD EQUAL INTEGER
      {
          if (m_pDecoNpcData != nullptr) {
              m_pDecoNpcData->m_nPeriod = grammar::SemanticInt32($3);
          }
      }
    | COST_ADENA EQUAL INTEGER
      {
          if (m_pDecoNpcData != nullptr) {
              m_pDecoNpcData->m_nCostAdena = grammar::SemanticInt32($3);
          }
      }
    | COST_ITEM EQUAL lbrace_def_3
    | NPC_ID EQUAL INTEGER
      {
          if (m_pDecoNpcData != nullptr) {
              m_pDecoNpcData->m_nNpcCID = grammar::SemanticInt32($3) + 1000000;
          }
      }
    | FACTION EQUAL INTEGER
      {
          if (m_pDecoNpcData != nullptr) {
              m_pDecoNpcData->m_nFactionType = grammar::SemanticInt32($3);
          }
      }
    | RESIDENCE_GRADES EQUAL lbrace_def
    | RESIDENCE_DOMAINS EQUAL lbrace_def_2
    | CHANGEABLE EQUAL INTEGER
      {
          if (m_pDecoNpcData != nullptr) {
              m_pDecoNpcData->m_isChangeable = grammar::SemanticInt32($3) != 0;
          }
      }
    ;

lbrace_def
    : LBRACE n8 RBRACE
    | LBRACE RBRACE
    ;

n8
    : n8 SEMICOLON integer_def
    | integer_def
    ;

integer_def
    : INTEGER
      {
          if (m_pDecoNpcData != nullptr) {
              m_pDecoNpcData->m_residenceGrades.push_back(grammar::SemanticInt32($1));
          }
      }
    ;

lbrace_def_2
    : LBRACE n11 RBRACE
    | LBRACE RBRACE
    ;

n11
    : n11 SEMICOLON integer_def_2
    | integer_def_2
    ;

integer_def_2
    : INTEGER
      {
          if (m_pDecoNpcData != nullptr) {
              m_pDecoNpcData->m_residenceDomains.push_back(grammar::SemanticInt32($1));
          }
      }
    ;

lbrace_def_3
    : LBRACE n14 RBRACE
    | LBRACE RBRACE
    ;

n14
    : n14 SEMICOLON integer_def_3
    | integer_def_3
    ;

integer_def_3
    : INTEGER LPAREN INTEGER RPAREN
      {
          if (m_pDecoNpcData != nullptr) {
              m_pDecoNpcData->m_vCostItems.push_back(
                  CDecoNpcData::CostItem{grammar::SemanticInt32($1), grammar::SemanticInt32($3)}
              );
          }
      }
    ;

%%
