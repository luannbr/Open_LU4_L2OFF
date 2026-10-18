// CParserForBuilderCmd grammar (original L2Server/buildercmd_parser.y).
// Bison generates BuilderCmdParser.cpp and BuilderCmdParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/BuilderCmdParser.h"

#include "L2Server/src/BuilderCmdHandler.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "BuilderCmdParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForBuilderCmd::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"BuilderCmdParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

CParserForBuilderCmd::CParserForBuilderCmd() : m_lexer() {}

CParserForBuilderCmd::~CParserForBuilderCmd() = default;

int CParserForBuilderCmd::Create() {
    return m_lexer.Create();
}
}

%token COMMAND_BEGIN
%token COMMAND_END
%token COMMAND
%token ALIAS
%token NAME
%start command_list

%%

command_list
    : command_list command
    | command
    ;

command
    : COMMAND_BEGIN COMMAND '=' NAME ALIAS '=' NAME COMMAND_END
      {
          AddBuilderCmdAlias(
              grammar::SemanticString($4),
              grammar::SemanticString($7)
          );
      }
    ;

%%
