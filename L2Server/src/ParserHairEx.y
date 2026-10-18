// ParserForHairEx grammar.
// Bison generates ParserHairEx.cpp and ParserHairExTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserHairEx.h"

#include "L2Server/src/BeautyManager.h"
#include "L2Server/src/Setting.h"

#include <cstdint>
#include <cstring>
#include <string_view>

#include "ParserHairExTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse ParserForHairEx::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserHairExTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

ParserForHairEx::ParserForHairEx() : m_lexer() {}

ParserForHairEx::~ParserForHairEx() = default;

int ParserForHairEx::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}
}

%token HAIREX_BEGIN
%token HAIREX_END
%token ID
%token MESH_PRICE_ADENA
%token MESH_PRICE_ITEM
%token LIMIT
%token ENABLE
%token RESET_PRICE_ADENA
%token INTEGER
%start n1

%%

n1
    : hairex_list
    ;

hairex_list
    : hairex_list hairex
    | hairex
    ;

hairex
    : HAIREX_BEGIN ID '=' INTEGER MESH_PRICE_ADENA '=' INTEGER RESET_PRICE_ADENA '=' INTEGER MESH_PRICE_ITEM '=' INTEGER
      LIMIT '=' INTEGER ENABLE '=' INTEGER HAIREX_END
      {
          CSetting& setting = Singleton<CSetting>::inst();
          const int second = grammar::SemanticInt32($4);
          if (setting.IsExistKey(Beauty::HAIR, 0, second)) {
              setting.DeleteBeautyEx(Beauty::HAIR, 0, second);
          }
          setting.SetBeautyEx(
              Beauty::HAIR, 0, second, grammar::SemanticInt64($7), grammar::SemanticInt64($10),
              grammar::SemanticInt64($13), grammar::SemanticInt32($16), static_cast<short>($19)
          );
      }
    ;

%%
