// ParserForColorEx grammar.
// Bison generates ParserColorEx.cpp and ParserColorExTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserColorEx.h"

#include "L2Server/src/BeautyManager.h"
#include "L2Server/src/Setting.h"

#include <cstdint>
#include <cstring>
#include <string_view>

#include "ParserColorExTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse ParserForColorEx::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserColorExTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

ParserForColorEx::ParserForColorEx() : m_lexer() {}

ParserForColorEx::~ParserForColorEx() = default;

int ParserForColorEx::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}
}

%token COLOREX_BEGIN
%token COLOREX_END
%token HAIR_ID
%token ID
%token COLOR_PRICE_ADENA
%token COLOR_PRICE_ITEM
%token LIMIT
%token ENABLE
%token INTEGER
%start n1

%%

n1
    : colorex_list
    ;

colorex_list
    : colorex_list colorex
    | colorex
    ;

colorex
    : COLOREX_BEGIN HAIR_ID '=' INTEGER ID '=' INTEGER COLOR_PRICE_ADENA '=' INTEGER COLOR_PRICE_ITEM '=' INTEGER LIMIT '='
      INTEGER ENABLE '=' INTEGER COLOREX_END
      {
          CSetting& setting = Singleton<CSetting>::inst();
          const int first = grammar::SemanticInt32($4);
          const int second = grammar::SemanticInt32($7);
          if (setting.IsExistKey(Beauty::COLOR, first, second)) {
              setting.DeleteBeautyEx(Beauty::COLOR, first, second);
          }
          setting.SetBeautyEx(
              Beauty::COLOR, first, second, grammar::SemanticInt64($10), 0, grammar::SemanticInt64($13),
              grammar::SemanticInt32($16), static_cast<short>($19)
          );
      }
    ;

%%
