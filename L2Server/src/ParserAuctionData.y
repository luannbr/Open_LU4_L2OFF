// auctiondata.txt grammar (original L2Server/parser_auctiondata.y).
// Bison generates ParserAuctionData.cpp and ParserAuctionDataTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserAuctionData.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ObjectDB.h"

#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <string_view>

#include "ParserAuctionDataTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForAuction::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserAuctionDataTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

const char16_t* SemanticWord(grammar::SemanticBits value) {
    return grammar::SemanticString(value);
}

} // namespace

CParserForAuction::CParserForAuction() :
    m_pAuctionNPC(nullptr),
    m_nItemType(0),
    m_paddingFcTo100{},
    m_nItemAmount(0),
    m_nItemPrice(0),
    m_nItemCount(0),
    m_padding114To118{},
    m_lexer() {
}

CParserForAuction::~CParserForAuction() = default;

int CParserForAuction::create() {
    return m_lexer.Create();
}

void CParserForAuction::yysyntaxerror() {
    char16_t message[1024]{};
#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\auctiondata.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/auctiondata.txt";
#endif
    // std::swprintf(
    //     message,
    //     std::size(message),
    //     L"%s[%d] : syntax error",
    //     kFileName,
    //     m_lexer.line + 1)
    FormatUtf16To(message, std::size(message), u"%ls[%d] : syntax error", kFileName, m_lexer.line + 1);
    // Log.Add(LOG_ERROR, message)
    // Log.Add(LOG_ERROR, L"%ls", message)
    // L2_LOG_ERROR(Log, L"%ls", message)
    L2_LOG_ERROR(Log, "auctiondata.txt line %lld: syntax error.", static_cast<long long>(m_lexer.line) + 1);
    // Noise: The error is already logged above. Only Windows needs a modal copy.
    // PopUpWindow(message)
#if defined(_WIN32)
    PopUpWindow(L"%ls", message);
#endif
}
}

%token AUCTION_BEGIN
%token AUCTION_END
%token NPC_ID
%token NEXTINFO_OPEN_TIME
%token AUCTION_DOING
%token AUCTION_START_TIME
%token AUCTION_DURATION
%token AUCTION_WEEK_DAY
%token AUCTION_DATE_BEGIN
%token AUCTION_DATE_END
%token ITEM_COUNT
%token ITEM_BEGIN
%token ITEM_END
%token ITEM
%token AMOUNT
%token PRICE
%token INDEX
%token INTEGER
%token NAME
%start n1

%%

n1
    : auction_list
    ;

auction_list
    : auction_list auction
    | %empty
    ;

auction
    : AUCTION_BEGIN
      { m_pAuctionNPC = new CAuctionNPC; }
      npc_id_def_list AUCTION_END
      {
          CAuctionNPCDB::inst().m_mapAuctionNPC[m_pAuctionNPC->m_nNPCClassID] = m_pAuctionNPC;
          if (m_nItemCount != static_cast<int>(m_pAuctionNPC->m_AuctionItemList.size())) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"item_count is wrong!!!. NPC[%d]",
              //     m_pAuctionNPC->m_nNPCClassID)
              // L2_LOG_ERROR(Log,
              //     L"item_count is wrong!!!. NPC[%d]",
              //     m_pAuctionNPC->m_nNPCClassID)
              L2_LOG_ERROR(
                  Log,
                  "auctiondata.txt line %lld, NPC class %d: declared item_count %d differs from the published list size "
                  "%llu; fatal parser validation failed.",
                  static_cast<long long>(m_lexer.line) + 1, m_pAuctionNPC->m_nNPCClassID, m_nItemCount,
                  static_cast<unsigned long long>(m_pAuctionNPC->m_AuctionItemList.size())
              );
              ForceCrash(CrashInfo::LEX_YACC_FAILED);
          }
      }
    ;

npc_id_def_list
    : npc_id_def_list npc_id_def
    | %empty
    ;

npc_id_def
    : NPC_ID '=' INTEGER
      { m_pAuctionNPC->m_nNPCClassID = grammar::SemanticInt32($3); }
    | AUCTION_DOING '=' INTEGER
      { m_pAuctionNPC->m_bDoing = grammar::SemanticInt32($3) == 1; }
    | NEXTINFO_OPEN_TIME '=' INTEGER
      { m_pAuctionNPC->m_tNextInfoOpenTime = grammar::SemanticInt32($3) * 3600; }
    | ITEM_COUNT '=' INTEGER
      { m_nItemCount = grammar::SemanticInt32($3); }
      auction_date_list { } item_list
    ;

auction_date_list
    : auction_date_list auction_date
    | auction_date
    ;

auction_date
    : AUCTION_DATE_BEGIN { } auction_week_day_def AUCTION_DATE_END
    ;

auction_week_day_def
    : AUCTION_WEEK_DAY '=' INTEGER AUCTION_START_TIME '=' INTEGER AUCTION_DURATION '=' INTEGER
      {
          [&] {
              const int weekDay = grammar::SemanticInt32($3);
              const int hourAndMinute = grammar::SemanticInt32($6);
              AuctionDate date{hourAndMinute / 100, hourAndMinute % 100, grammar::SemanticInt32($9) * 60, weekDay};
              if (weekDay < 0 || weekDay > 6) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"[%d] auction_week_day must be between 0 and 6!!!",
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"[%d] auction_week_day must be between 0 and 6!!!",
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log, "auctiondata.txt line %lld: auction_week_day %d is outside 0..6; fatal parser validation failed.",
                      static_cast<long long>(m_lexer.line) + 1, weekDay
                  );
                  ForceCrash(CrashInfo::LEX_YACC_FAILED);
                  return;
              }
              const int endTime = (date.m_nStartHour * 60 + date.m_nStartMin) * 60 + date.m_nDuration;
              if (endTime > 0x1517f) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"[%d] auction ends tomorrow!!!",
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"[%d] auction ends tomorrow!!!",
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      "auctiondata.txt line %lld: auction end time %d seconds exceeds the day limit 86399; fatal parser "
                      "validation failed.",
                      static_cast<long long>(m_lexer.line) + 1, endTime
                  );
                  ForceCrash(CrashInfo::LEX_YACC_FAILED);
                  return;
              }
              for (const AuctionDate& existing : m_pAuctionNPC->m_AuctionDateList) {
                  if (existing.m_nWeekDay == weekDay) {
                      // Log.Add(
                      //     LOG_ERROR,
                      //     L"[%d] One day two auction!!! one day one auction plz",
                      //     m_lexer.line + 1)
                      // L2_LOG_ERROR(Log,
                      //     L"[%d] One day two auction!!! one day one auction plz",
                      //     m_lexer.line + 1)
                      L2_LOG_ERROR(
                          Log,
                          "auctiondata.txt line %lld, NPC class %d: more than one auction is defined for weekday %d; fatal "
                          "parser validation failed.",
                          static_cast<long long>(m_lexer.line) + 1, m_pAuctionNPC->m_nNPCClassID, weekDay
                      );
                      ForceCrash(CrashInfo::LEX_YACC_FAILED);
                      return;
                  }
              }
              m_pAuctionNPC->m_AuctionDateList.push_back(date);
          }();
      }
    ;

item_list
    : item_list item
    | %empty
    ;

item
    : ITEM_BEGIN { } item_def_list ITEM_END
      {
          m_pAuctionNPC->m_AuctionItemList.push_back(NPCAuctionItem{m_nItemType, {}, m_nItemAmount, m_nItemPrice});
      }
    ;

item_def_list
    : item_def_list item_def
    | %empty
    ;

item_def
    : ITEM '=' NAME
      {
          const char16_t* const name = SemanticWord($3);
          m_nItemType = g_ObjectDB.GetClassIdFromName(name, false);
          if (m_nItemType == -1) {
              // const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined Item[%s] in line[%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Undefined Item[%s] in line[%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log, u"auctiondata.txt line %lld: unknown item '%ls'; parsing continues with item class ID -1.",
                  static_cast<long long>(m_lexer.line) + 1, name == nullptr ? L"<null>" : hostName.c_str()
              );
          }
      }
    | AMOUNT '=' INTEGER
      { m_nItemAmount = grammar::SemanticInt64($3); }
    | PRICE '=' INTEGER
      { m_nItemPrice = grammar::SemanticInt64($3); }
    | INDEX '=' INTEGER
      {
          const int currentIndex = static_cast<int>(m_pAuctionNPC->m_AuctionItemList.size());
          const int expectedIndex = grammar::SemanticInt32($3);
          if (currentIndex != expectedIndex) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"item_index is wrong!!!. NPC[%d] ItemIndex[%d != %d]",
              //     m_pAuctionNPC->m_nNPCClassID,
              //     currentIndex,
              //     expectedIndex)
              // L2_LOG_ERROR(Log,
              //     L"item_index is wrong!!!. NPC[%d] ItemIndex[%d != %d]",
              //     m_pAuctionNPC->m_nNPCClassID,
              //     currentIndex,
              //     expectedIndex)
              L2_LOG_ERROR(
                  Log,
                  "auctiondata.txt line %lld, NPC class %d: item_index %d differs from the current item count %d; fatal "
                  "parser validation failed.",
                  static_cast<long long>(m_lexer.line) + 1, m_pAuctionNPC->m_nNPCClassID, expectedIndex, currentIndex
              );
              ForceCrash(CrashInfo::LEX_YACC_FAILED);
          }
      }
    ;

%%
