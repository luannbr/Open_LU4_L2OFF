// CParserForAction grammar (original L2Server/parser_basicAction.y).
// Bison generates ParserBasicAction.cpp and ParserBasicActionTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserBasicAction.h"

#include "Shared/src/TextRuntime.h"

#include "L2Server/src/UserBasicAction.h"

#include "Shared/src/Log.h"

#include <cstdint>
#include <cstring>
#include <string_view>

#include "ParserBasicActionTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForAction::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserBasicActionTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

CParserForAction::CParserForAction() : m_lexer(), m_handlerOption(0), m_bAllowedRaidUser(false), m_targetPadding1d5{} {
}

CParserForAction::~CParserForAction() = default;

int CParserForAction::Create(char16_t* const fileBuffer, const int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}
}

%token ACTION_BEGIN
%token ACTION_END
%token ID
%token HANDLER
%token OPTION
%token ALLOW_RAID_USER
%token NAME
%token NONE_GROUP
%start n1

%%

n1
    : n1 n2
    | n2
    ;

n2
    : action
    | action_2
    ;

action
    : ACTION_BEGIN ID '=' NONE_GROUP HANDLER '=' NAME option_def allow_raid_user_def ACTION_END
      {
          const int actionId = grammar::SemanticInt32($4);
          const auto* const handlerName =
              grammar::SemanticString($7);
          if (!g_UserBasicAction.Add(actionId, handlerName, m_handlerOption, m_bAllowedRaidUser)) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[%s][%d] Duplicated UserBasicAction(id:%d)",
              //     L"parser_basicAction.y",
              //     0x4e,
              //     actionId)
              // L2_LOG_ERROR(Log,
              //     L"[%s][%d] Duplicated UserBasicAction(id:%d)",
              //     L"parser_basicAction.y",
              //     0x4e,
              //     actionId)
              L2_LOG_ERROR(
                  Log,
                  "Cannot register user action %d with handler '%s' at script line %lld: the ID is already registered or "
                  "the handler is unavailable; the action is not added.",
                  actionId, Utf16ToUtf8(std::u16string_view(handlerName != nullptr ? handlerName : u"<null>")).c_str(),
                  static_cast<long long>(m_lexer.line) + 1
              );
          }
      }
    ;

option_def
    : OPTION '=' NONE_GROUP
      { m_handlerOption = grammar::SemanticInt32($3); }
    | %empty
      { m_handlerOption = -1; }
    ;

allow_raid_user_def
    : ALLOW_RAID_USER '=' NONE_GROUP
      { m_bAllowedRaidUser = grammar::SemanticInt32($3) == 1; }
    | %empty
      { m_bAllowedRaidUser = false; }
    ;

action_2
    : ACTION_BEGIN ID '=' NONE_GROUP ACTION_END
      {
          const int actionId = grammar::SemanticInt32($4);
          if (!g_UserBasicAction.Add(actionId)) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[%s][%d] Duplicated UserBasicAction(id:%d)",
              //     L"parser_basicAction.y",
              //     0x73,
              //     actionId)
              // L2_LOG_ERROR(Log,
              //     L"[%s][%d] Duplicated UserBasicAction(id:%d)",
              //     L"parser_basicAction.y",
              //     0x73,
              //     actionId)
              L2_LOG_ERROR(
                  Log,
                  "Cannot register user action %d without a handler at script line %lld: the ID is already registered; "
                  "the existing entry is retained.",
                  actionId, static_cast<long long>(m_lexer.line) + 1
              );
          }
      }
    ;

%%
