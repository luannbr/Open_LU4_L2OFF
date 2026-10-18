// CParserForInstantAgit grammar (original L2Server/instantagit_parser.y).
// Bison generates InstantAgitParser.cpp and InstantAgitParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/InstantAgitParser.h"

#include "L2Server/src/InstantAgit.h"

#include <cstdint>
#include <cstring>
#include <ctime>
#include <string_view>

#include "InstantAgitParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForInstantAgit::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"InstantAgitParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

CParserForInstantAgit::CParserForInstantAgit() : m_lexer(), m_pInstantAgitData(nullptr) {}

CParserForInstantAgit::~CParserForInstantAgit() = default;

int CParserForInstantAgit::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}
}

%token INSTANT_AGIT_BEGIN
%token INSTANT_AGIT_END
%token ID
%token RENTAL_FEE
%token COMMISSION
%token RENTAL_PERIOD
%token APPLY_PERIOD
%token FIRST_LOTTERY_DATE
%token MIN_PLEDGE_LEVEL
%token INSTANT_ZONE
%token ORIGIN
%token DAY
%token HOUR
%token RESIDENCE_GRADE
%token DECO_NPC_SLOTS
%token NAME
%token STRING
%token INTEGER
%token FLOAT_NUMBER
%start n1

%%

n1
    : instant_agit_list
    ;

instant_agit_list
    : instant_agit_list instant_agit
    | instant_agit
    ;

instant_agit
    : INSTANT_AGIT_BEGIN ID '=' INTEGER RENTAL_FEE '=' INTEGER COMMISSION '=' INTEGER
      {
          const auto SemanticInt64 = [](const grammar::SemanticBits slot) {
              const std::uint64_t bits = slot;
              std::int64_t value = 0;
              std::memcpy(&value, &bits, sizeof(value));
              return value;
          };
          const auto SemanticDouble = [](const grammar::SemanticBits slot) {
              const std::uint64_t bits = slot;
              double value = 0.0;
              std::memcpy(&value, &bits, sizeof(value));
              return value;
          };
          m_pInstantAgitData = new CInstantAgitData(grammar::SemanticInt32($4));
          m_pInstantAgitData->m_RentalFee = SemanticInt64($7);
          m_pInstantAgitData->m_Commission = SemanticDouble($10);
      }
      rental_period_def first_lottery_date_def MIN_PLEDGE_LEVEL '=' INTEGER INSTANT_ZONE '=' INTEGER
      {
          m_pInstantAgitData->m_MinPledgeLevel = grammar::SemanticInt32($16);
          m_pInstantAgitData->m_InstantZoneTypeId = grammar::SemanticInt32($19);
      }
      origin_def RESIDENCE_GRADE '=' INTEGER
      {
          m_pInstantAgitData->m_nResidenceGrade = grammar::SemanticInt32($24);
      }
      DECO_NPC_SLOTS '=' n11 { } INSTANT_AGIT_END
      {
          CInstantAgitManager::inst().AddInstantAgit(m_pInstantAgitData);
          m_pInstantAgitData = nullptr;
      }
    ;

rental_period_def
    : RENTAL_PERIOD '=' INTEGER DAY APPLY_PERIOD '=' INTEGER HOUR
      {
          m_pInstantAgitData->m_RetalPeriod = grammar::SemanticInt32($3);
          m_pInstantAgitData->m_ApplyPeriod = grammar::SemanticInt32($7) * 0xe10;
      }
    ;

first_lottery_date_def
    : FIRST_LOTTERY_DATE '=' INTEGER '/' INTEGER '/' INTEGER
      {
          std::tm lottery{};
          lottery.tm_hour = 0x15;
          lottery.tm_mday = grammar::SemanticInt32($7);
          lottery.tm_mon = grammar::SemanticInt32($5) - 1;
          lottery.tm_year = grammar::SemanticInt32($3) - 1900;
          const std::time_t value = std::mktime(&lottery);
          m_pInstantAgitData->SetNextLotteryDate(static_cast<std::int32_t>(value));
      }
    ;

origin_def
    : ORIGIN '=' '{' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          m_pInstantAgitData->m_InstantAgitPos.x = static_cast<double>(grammar::SemanticInt32($4));
          m_pInstantAgitData->m_InstantAgitPos.y = static_cast<double>(grammar::SemanticInt32($6));
          m_pInstantAgitData->m_InstantAgitPos.z = static_cast<double>(grammar::SemanticInt32($8));
      }
    ;

n11
    : '{' n12 '}'
    | '{' '}'
    ;

n12
    : n12 ';' n13
    | n13
    ;

n13
    : '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          FVector position{
              static_cast<double>(grammar::SemanticInt32($2)), static_cast<double>(grammar::SemanticInt32($4)),
              static_cast<double>(grammar::SemanticInt32($6))
          };
          m_pInstantAgitData->AddDecoNpcSlot(position, grammar::SemanticInt32($8));
      }
    ;

%%
