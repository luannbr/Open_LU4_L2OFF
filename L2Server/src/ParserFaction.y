// factiondata.txt grammar.
// Bison generates ParserFaction.cpp and ParserFactionTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserFaction.h"

#include "L2Server/src/LexerFaction.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/MonsterBook.h"

#include "Shared/src/BaseStructure.h"

#include "ParserFactionTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForFaction::yyparse
#define yylex(value) m_pLexer->Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_pLexer->yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserFactionTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
// #include "Shared/Portable/OperatorLog.h"
#include "Shared/src/Runtime/SourceLog.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>

CParserForFaction::CParserForFaction() : m_nServerFactionID(-1) {}

CParserForFaction::~CParserForFaction() = default;

int CParserForFaction::Create(CLexerForFaction* const lexer) {
    m_pLexer = lexer;
    return 1;
}
}

%token INTEGER
%token NAME
%token FACTION_LEVEL_CAP
%token FACTION_BEGIN
%token FACTION_END
%token FACTION_ID
%token FACTION_NAME
%token FACTION_NAME_K
%token FACTION_MAX_LEVEL
%token FACTION_TABLE_BEGIN
%token FACTION_TABLE_END
%token FACTION_LEVEL
%token FACTION_POINT
%token MONSTER_BOOK_REWARD_LV
%start n1

%%

n1
    : faction_list
    ;

faction_list
    : faction_list faction
    | faction
    ;

faction
    : FACTION_BEGIN faction_id_def faction_name_def faction_name_k_def faction_max_level_def monster_book_reward_lv_def
      FACTION_TABLE_BEGIN
      {
          FactionByLevel[m_nServerFactionID] =
              new std::int64_t[static_cast<std::size_t>(FactionMaxLevel[m_nServerFactionID])];
      }
      faction_level_def_list FACTION_TABLE_END FACTION_END
    ;

faction_id_def
    : FACTION_ID '=' INTEGER
      {
          // Noise: successful numeric/separator values are already consumed by the parser.
          // std::wprintf(L"faction_id : %d\n", grammar::SemanticInt32($3))
          m_nServerFactionID = grammar::SemanticInt32($3) - 1;
      }
    ;

faction_name_def
    : FACTION_NAME '=' NAME
    ;

faction_name_k_def
    : FACTION_NAME_K '=' NAME
    ;

faction_max_level_def
    : FACTION_MAX_LEVEL '=' INTEGER
      {
          // Noise: successful numeric/separator values are already consumed by the parser.
          // std::wprintf(
          //     L"faction_max_level : %d\n",
          //     grammar::SemanticInt32($3))
          FactionMaxLevel[m_nServerFactionID] = grammar::SemanticInt32($3) + 1;
      }
    ;

monster_book_reward_lv_def
    : MONSTER_BOOK_REWARD_LV '=' INTEGER
      {
          auto* const info = new MonsterBook::Script::FactionInfo{};
          info->m_nFactionId = m_nServerFactionID;
          info->m_nRewardLevelBound = grammar::SemanticInt32($3);
          Singleton<MonsterBook::Script>::inst().AddFactionInfo(m_nServerFactionID, info);
      }
    ;

faction_level_def_list
    : faction_level_def_list faction_level_def
    | faction_level_def
    ;

faction_level_def
    : FACTION_LEVEL '=' INTEGER FACTION_POINT '=' INTEGER
      {
          const int level = grammar::SemanticInt32($3);
          const int point = grammar::SemanticInt32($6);
          // Noise: successful numeric/separator values are already consumed by the parser.
          // std::wprintf(L"faction_level: %d\n", level)
          // Noise: successful numeric/separator values are already consumed by the parser.
          // std::wprintf(L"faction_point: %d\n", point)
          if (level >= 0 && level < FactionMaxLevel[m_nServerFactionID]) {
              FactionByLevel[m_nServerFactionID][level] = point;
              break;
          }
          // wchar_t message[128]{}
          // std::swprintf(
          //     message,
          //     std::size(message),
          //     L"Invalid level number [%lld]",
          //     static_cast<long long>(level))
          // yyerror(message)
          // Keep the configured yyerror stream and its existing newline/exception policy.
          const bool knownFaction =
              m_nServerFactionID >= 0 && static_cast<std::size_t>(m_nServerFactionID) < std::size(FactionMaxLevel);
          const std::string limit = knownFaction ? std::to_string(FactionMaxLevel[m_nServerFactionID]) : "<unavailable>";
          const std::string detail = "factiondata.txt: faction ID " +
              std::to_string(static_cast<long long>(m_nServerFactionID) + 1) + " has invalid level " +
              std::to_string(level) + " (points " + std::to_string(point) + "); expected 0 <= level < " + limit +
              ". This point row is skipped; parsing continues.";
          // const auto record = runtime::logging::Format(
          //     nullptr, runtime::logging::Level::Error, detail, __FILE__)
          // // Format ends with one newline. Yyerror supplies that newline itself.
          // const std::string_view formatted = record.length != 0
          //     ? std::string_view(record.bytes.data(), record.length - 1)
          //     : std::string_view(detail)
          // const std::wstring message = Utf16ToWide(Utf8ToUtf16(formatted))
          // yyerror(message.c_str())
          const std::wstring message = Utf8ToWide(detail);
          const runtime::logging::SourceFileScope source(__FILE__);
          grammar::ReportError(message.c_str());
      }
    ;

%%
