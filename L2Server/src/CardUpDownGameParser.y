// CParserForCardUpDownGame grammar.
// Bison generates CardUpDownGameParser.cpp and CardUpDownGameParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/CardUpDownGameParser.h"

#include "L2Server/src/CardUpDownGame.h"

#include "Shared/src/BaseStructure.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Utils.h"

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string_view>

#include "CardUpDownGameParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForCardUpDownGame::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"CardUpDownGameParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

char16_t* SemanticText(grammar::SemanticBits value) {
    return grammar::SemanticString(value);
}

} // namespace

CParserForCardUpDownGame::CParserForCardUpDownGame() : m_lexer(), m_pGameData(nullptr) {}

CParserForCardUpDownGame::~CParserForCardUpDownGame() = default;

int CParserForCardUpDownGame::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}

void CParserForCardUpDownGame::yysyntaxerror() {
    char16_t message[60]{};
    FormatUtf16To(message, std::size(message), u"CParserForCardUpDownGame[%d] : syntax error", m_lexer.line + 1);
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

%token INTEGER
%token FLOAT_NUMBER
%token NAME
%token GAME_BEGIN
%token GAME_END
%token ID
%token NPC_NAME
%token CARD_COUNT
%token TRY_COUNT
%token DURATION
%token CONSUME_ITEM
%token FIXED_REWARD
%token RANDOM_REWARD
%token START_TIME
%token END_TIME
%token REWARD_DURATION
// The original grammar has 24 shift/reduce and 19 reduce/reduce conflicts. Bison
// resolves them as the original tables did. A shift wins over a reduce,
// and the earlier rule wins over a later rule.
%start n1

%%

n1
    : game_list
    | %empty
    ;

game_list
    : game_list game
    | game
    | %empty
    ;

game
    : GAME_BEGIN
      { m_pGameData = new CardUpDownGame::GameData; }
      id_def_list GAME_END
      {
          CardUpDownGame::Script::inst().AddGameData(m_pGameData);
          m_pGameData = nullptr;
      }
    ;

id_def_list
    : id_def_list id_def
    | id_def
    | %empty
    ;

id_def
    : ID '=' INTEGER
      { m_pGameData->m_nId = grammar::SemanticInt32($3); }
    | NPC_NAME '=' NAME
      { m_pGameData->SetNpc(SemanticText($3)); }
    | START_TIME '=' NAME
      {
          TimePeriodInfo::ParseTime(SemanticText($3), m_pGameData->m_nStartTime);
      }
    | END_TIME '=' NAME
      { TimePeriodInfo::ParseTime(SemanticText($3), m_pGameData->m_nEndTime); }
    | CARD_COUNT '=' INTEGER
      { m_pGameData->m_nCardCount = grammar::SemanticInt32($3); }
    | TRY_COUNT '=' INTEGER
      { m_pGameData->m_nTryCount = grammar::SemanticInt32($3); }
    | DURATION '=' INTEGER
      { m_pGameData->m_nDuration = grammar::SemanticInt32($3); }
    | CONSUME_ITEM '=' '{' n7 '}'
    | FIXED_REWARD '=' '{' n9 '}'
    | RANDOM_REWARD '=' '{' n11 '}'
    | REWARD_DURATION '=' INTEGER
      { m_pGameData->m_nRewardDuration = grammar::SemanticInt32($3); }
    | %empty
    ;

n7
    : n7 ';' name_def
    | name_def
    | %empty
    ;

name_def
    : NAME ';' INTEGER
      {
          m_pGameData->m_vConsumeItem.push_back(
              new CardUpDownGame::ConsumeItem(SemanticText($1), grammar::SemanticInt32($3))
          );
      }
    | '{' NAME ';' INTEGER '}'
      {
          m_pGameData->m_vConsumeItem.push_back(
              new CardUpDownGame::ConsumeItem(SemanticText($2), grammar::SemanticInt32($4))
          );
      }
    | %empty
    ;

n9
    : n9 ';' n10
    | n10
    | %empty
    ;

n10
    : '{' NAME ';' INTEGER ';' FLOAT_NUMBER '}'
      {
          m_pGameData->m_vFixedReward.push_back(new CardUpDownGame::RewardItem(
              SemanticText($2), grammar::SemanticInt32($4), grammar::SemanticDouble($6)
          ));
      }
    | NAME ';' INTEGER ';' FLOAT_NUMBER
      {
          m_pGameData->m_vFixedReward.push_back(new CardUpDownGame::RewardItem(
              SemanticText($1), grammar::SemanticInt32($3), grammar::SemanticDouble($5)
          ));
      }
    | %empty
    ;

n11
    : n11 ';' n12
    | n12
    | %empty
    ;

n12
    : '{' NAME ';' INTEGER ';' FLOAT_NUMBER '}'
      {
          m_pGameData->m_vRandomReward.push_back(new CardUpDownGame::RewardItem(
              SemanticText($2), grammar::SemanticInt32($4), grammar::SemanticDouble($6)
          ));
      }
    | NAME ';' INTEGER ';' FLOAT_NUMBER
      {
          m_pGameData->m_vRandomReward.push_back(new CardUpDownGame::RewardItem(
              SemanticText($1), grammar::SemanticInt32($3), grammar::SemanticDouble($5)
          ));
      }
    | %empty
    ;

%%
