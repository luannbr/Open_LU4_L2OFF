// ParserForFaceEx grammar.
// Bison generates ParserFaceEx.cpp and ParserFaceExTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserFaceEx.h"

#include "L2Server/src/BeautyManager.h"
#include "L2Server/src/Setting.h"

#include <cstdint>
#include <cstring>
#include <string_view>

#include "ParserFaceExTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse ParserForFaceEx::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserFaceExTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

ParserForFaceEx::ParserForFaceEx() : m_lexer() {}

ParserForFaceEx::~ParserForFaceEx() = default;

int ParserForFaceEx::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}
}

%token FACEEX_BEGIN
%token FACEEX_END
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
    : faceex_list
    ;

faceex_list
    : faceex_list faceex
    | faceex
    ;

faceex
    : FACEEX_BEGIN ID '=' INTEGER MESH_PRICE_ADENA '=' INTEGER RESET_PRICE_ADENA '=' INTEGER MESH_PRICE_ITEM '=' INTEGER
      LIMIT '=' INTEGER ENABLE '=' INTEGER FACEEX_END
      {
          CSetting& setting = Singleton<CSetting>::inst();
          const int second = grammar::SemanticInt32($4);
          if (setting.IsExistKey(Beauty::FACE, 0, second)) {
              setting.DeleteBeautyEx(Beauty::FACE, 0, second);
          }
          setting.SetBeautyEx(
              Beauty::FACE, 0, second, grammar::SemanticInt64($7), grammar::SemanticInt64($10),
              grammar::SemanticInt64($13), grammar::SemanticInt32($16), static_cast<short>($19)
          );
      }
    ;

%%
