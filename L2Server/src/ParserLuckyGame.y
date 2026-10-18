// CParserForLuckyGame grammar.
// Bison generates ParserLuckyGame.cpp and ParserLuckyGameTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserLuckyGame.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/Portable/ProcessRuntime.h"
#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "Shared/src/Utils.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <string_view>

#include "ParserLuckyGameTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForLuckyGame::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserLuckyGameTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

LuckyGameReward::LuckyGameReward(int index) :
    m_nIndex(index),
    m_nTurningPoint(100000000),
    m_nRevCount(0),
    m_nModify_min(0),
    m_nModify_max(0),
    m_vCommonReward(),
    m_mapUniqueReward(),
    m_vModifyReward() {}

CParserForLuckyGame::CParserForLuckyGame() : m_lexer() {}

CParserForLuckyGame::~CParserForLuckyGame() = default;

int CParserForLuckyGame::create() {
    return 1;
}

void CParserForLuckyGame::yysyntaxerror() {
    char16_t message[60]{};
    FormatUtf16To(message, std::size(message), u"LuckyGame Checker[%d] : syntax error", m_lexer.line + 1);
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
%token NAME
%token INTEGER
%token FLOAT_NUMBER
%token LUCKYGAME_BEGIN
%token LUCKYGAME_END
%token INDEX
%token TURNING_POINT
%token COMMON_REWARD
%token UNIQUE_REWARD
%token MODIFY_REWARD
%start luckygame_list

%%

luckygame_list
    : luckygame_list luckygame
    | luckygame
    ;

luckygame
    : LUCKYGAME_BEGIN INDEX EQUAL INTEGER TURNING_POINT EQUAL INTEGER
      {
          m_nIndex = grammar::SemanticInt32($4);
          m_pReward = new LuckyGameReward(m_nIndex);
          m_pReward->SetTurningPoint(grammar::SemanticInt32($7));
      }
      common_reward_def_list LUCKYGAME_END
      { g_CLuckyGameDB.AddReward(m_nIndex, m_pReward); }
    ;

common_reward_def_list
    : common_reward_def_list common_reward_def
    | %empty
    ;

common_reward_def
    : COMMON_REWARD { } EQUAL '{' n9 '}'
    | UNIQUE_REWARD EQUAL '{' n10 '}'
    | MODIFY_REWARD { } EQUAL n8
    ;

n8
    : '{' INTEGER ';' INTEGER ';' '{' n11 '}' '}'
      {
          m_pReward->m_nModify_min = grammar::SemanticInt32($2);
          m_pReward->m_nModify_max = grammar::SemanticInt32($4);
      }
    ;

n9
    : n9 ';' '{' name_def '}'
    | '{' name_def '}'
    ;

n10
    : n10 ';' '{' name_def_2 '}'
    | '{' name_def_2 '}'
    ;

n11
    : n11 ';' '{' name_def_3 '}'
    | '{' name_def_3 '}'
    ;

name_def
    : NAME ';' INTEGER ';' FLOAT_NUMBER
      {
          char16_t* const itemName = grammar::SemanticString($1);
          const std::int64_t itemType = g_PrecompiledHeaderDB.Search(itemName);
          if (itemType == -1) {
              const std::wstring hostName = target_utf16::ToHostCodeUnits(itemName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"[PARSER_ERROR] not Exist Item[%s]",
              //     hostName.c_str())
              L2_LOG_ERROR(Log, u"[PARSER_ERROR] not Exist Item[%s]", hostName.c_str());
          } else {
              auto* const item = new LuckyGameItem;
              item->nItemType = static_cast<int>(itemType);
              item->nAmount = grammar::SemanticInt32($3);
              item->dPer = grammar::SemanticDouble($5);
              m_pReward->AddCommonReward(item);
          }
          std::free(itemName);
      }
    | NAME ';' INTEGER ';' INTEGER
      {
          char16_t* const itemName = grammar::SemanticString($1);
          const std::int64_t itemType = g_PrecompiledHeaderDB.Search(itemName);
          if (itemType == -1) {
              const std::wstring hostName = target_utf16::ToHostCodeUnits(itemName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"[PARSER_ERROR] not Exist Item[%s]",
              //     hostName.c_str())
              L2_LOG_ERROR(Log, u"[PARSER_ERROR] not Exist Item[%s]", hostName.c_str());
          } else {
              auto* const item = new LuckyGameItem;
              item->nItemType = static_cast<int>(itemType);
              item->nAmount = grammar::SemanticInt32($3);
              item->dPer = static_cast<double>(grammar::SemanticInt64($5));
              m_pReward->AddCommonReward(item);
          }
          std::free(itemName);
      }
    ;

name_def_2
    : NAME ';' INTEGER ';' '{' n15 '}'
      {
          char16_t* const itemName = grammar::SemanticString($1);
          const std::int64_t itemType = g_PrecompiledHeaderDB.Search(itemName);
          if (itemType == -1) {
              const std::wstring hostName = target_utf16::ToHostCodeUnits(itemName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"[PARSER_ERROR] not Exist Item[%s]",
              //     hostName.c_str())
              L2_LOG_ERROR(Log, u"[PARSER_ERROR] not Exist Item[%s]", hostName.c_str());
          } else {
              auto* const item = new LuckyGameItem;
              item->nItemType = static_cast<int>(itemType);
              item->nAmount = grammar::SemanticInt32($3);
              auto* const turns = grammar::SemanticPointer<std::vector<int>>($6);
              const int duplicateTurn = m_pReward->AddUniqueReward(item, turns);
              if (duplicateTurn > 0) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"[PARSER_ERROR] Exist Turn[%d]",
                  //     duplicateTurn)
                  L2_LOG_ERROR(Log, u"[PARSER_ERROR] Exist Turn[%d]", duplicateTurn);
              }
              if (duplicateTurn == -1) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"[PARSER_ERROR] not Exist Item[%d]",
                  //     item->nItemType)
                  L2_LOG_ERROR(Log, u"[PARSER_ERROR] not Exist Item[%d]", item->nItemType);
              }
          }
          std::free(itemName);
      }
    ;

name_def_3
    : NAME ';' INTEGER ';' FLOAT_NUMBER
      {
          char16_t* const itemName = grammar::SemanticString($1);
          const std::int64_t itemType = g_PrecompiledHeaderDB.Search(itemName);
          if (itemType == -1) {
              const std::wstring hostName = target_utf16::ToHostCodeUnits(itemName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"[PARSER_ERROR] not Exist Item[%s]",
              //     hostName.c_str())
              L2_LOG_ERROR(Log, u"[PARSER_ERROR] not Exist Item[%s]", hostName.c_str());
          } else {
              auto* const item = new LuckyGameItem;
              item->nItemType = static_cast<int>(itemType);
              item->nAmount = grammar::SemanticInt32($3);
              item->dPer = grammar::SemanticDouble($5);
              m_pReward->AddModifyReward(item);
          }
          std::free(itemName);
      }
    | NAME ';' INTEGER ';' INTEGER
      {
          char16_t* const itemName = grammar::SemanticString($1);
          const std::int64_t itemType = g_PrecompiledHeaderDB.Search(itemName);
          if (itemType == -1) {
              const std::wstring hostName = target_utf16::ToHostCodeUnits(itemName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"[PARSER_ERROR] not Exist Item[%s]",
              //     hostName.c_str())
              L2_LOG_ERROR(Log, u"[PARSER_ERROR] not Exist Item[%s]", hostName.c_str());
          } else {
              auto* const item = new LuckyGameItem;
              item->nItemType = static_cast<int>(itemType);
              item->nAmount = grammar::SemanticInt32($3);
              item->dPer = static_cast<double>(grammar::SemanticInt64($5));
              m_pReward->AddModifyReward(item);
          }
          std::free(itemName);
      }
    ;

n15
    : n15 ';' INTEGER
      {
          L2_GUARD_FUNCTION();
          auto* const turns = grammar::SemanticPointer<std::vector<int>>($1);
          turns->push_back(grammar::SemanticInt32($3));
          $$ = $1;
      }
    | INTEGER
      {
          L2_GUARD_FUNCTION();
          auto* const turns = new std::vector<int>;
          turns->push_back(grammar::SemanticInt32($1));
          $$ =
              grammar::PointerSemantic(turns);
      }
    ;

%%
