// CParserForMonsterBook grammar.
// Bison generates ParserMonsterBook.cpp and ParserMonsterBookTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserMonsterBook.h"
#include "L2Server/src/Inventory.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Utils.h"

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <new>
#include <string_view>

#include "ParserMonsterBookTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForMonsterBook::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserMonsterBookTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

CParserForMonsterBook::CParserForMonsterBook() :
    m_lexer(), m_pCard(nullptr), m_vIntList(), m_vItemList(), m_tableName(), m_pTrophyTable(nullptr) {
    m_vIntList.clear();
    m_vItemList.clear();
    m_tableName.assign(u"");
}

CParserForMonsterBook::~CParserForMonsterBook() = default;

int CParserForMonsterBook::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}

void CParserForMonsterBook::yysyntaxerror() {
    char16_t message[60]{};
    FormatUtf16To(message, std::size(message), u"CParserForMonsterBook[%d] : syntax error", m_lexer.line + 1);
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
%token NAME_2
%token CARD_BEGIN
%token CARD_END
%token ID
%token FACTION_ID
%token NPC_GROUP
%token TROPHY_GROUP
%token REWARD_FP
%token REWARD_EXP
%token REWARD_SP
%token REWARD_ITEM_1
%token REWARD_ITEM_2
%token REWARD_ITEM_3
%token REWARD_ITEM_4
%token TROPHY_BEGIN
%token TROPHY_END
%token NAME
%token LEVEL_1
%token LEVEL_2
%token LEVEL_3
%token LEVEL_4
// The original grammar has 43 shift/reduce and 19 reduce/reduce conflicts. Bison
// resolves them as the original tables did. A shift wins over a reduce,
// and the earlier rule wins over a later rule.
%start n1

%%

n1
    : n1 n2
    | n2
    ;

n2
    : card_list
    | trophy_list
    ;

card_list
    : card_list card
    | card
    ;

card
    : CARD_BEGIN
      {
          m_vIntList.clear();
          m_vItemList.clear();
          m_pCard = new (std::nothrow) MonsterBook::Script::Card;
      }
      n6 CARD_END
      { Singleton<MonsterBook::Script>::inst().AddCard(m_pCard); }
    ;

n6
    : n6 n7
    | n7
    | %empty
    ;

n7
    : id_def
    | faction_id_def
    | npc_group_def
    | trophy_group_def
    | reward_fp_def
    | reward_exp_def
    | reward_sp_def
    | reward_item_1_def
    | reward_item_2_def
    | reward_item_3_def
    | reward_item_4_def
    | %empty
    ;

id_def
    : ID '=' INTEGER
      {
          m_pCard->m_nCardId = grammar::SemanticInt32($3);
          if (static_cast<std::uint32_t>(m_pCard->m_nCardId - 1) >= 399U) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[MonsterBook][CardId] invalid card id, card id must greater than 0!!")
              L2_LOG_ERROR(Log, u"[MonsterBook][CardId] invalid card id, card id must greater than 0!!");
          }
      }
    ;

faction_id_def
    : FACTION_ID '=' INTEGER
      {
          const std::uint32_t inputFaction = static_cast<std::uint32_t>(grammar::SemanticInt32($3) - 1);
          const int factionId = inputFaction < 16U ? static_cast<int>(inputFaction) : -1;
          m_pCard->m_pFactionInfo = Singleton<MonsterBook::Script>::inst().GetFactionInfo(factionId);
          if (m_pCard->m_pFactionInfo == nullptr) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[MonsterBook][FactionId] invalid faction id [%d], card id[%d]",
              //     grammar::SemanticInt32($3),
              //     m_pCard->m_nCardId)
              L2_LOG_ERROR(
                  Log, u"[MonsterBook][FactionId] invalid faction id [%d], card id[%d]", grammar::SemanticInt32($3),
                  m_pCard->m_nCardId
              );
          }
      }
    ;

npc_group_def
    : NPC_GROUP '=' '{' n19 '}'
      {
          if (m_vIntList.empty()) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[MonsterBook][npc list] invalid npc id list! card[%d]",
              //     m_pCard->m_nCardId)
              L2_LOG_ERROR(Log, u"[MonsterBook][npc list] invalid npc id list! card[%d]", m_pCard->m_nCardId);
              break;
          }
          m_pCard->AddNPCIdSet(m_vIntList);
          m_vIntList.clear();
      }
    ;

trophy_group_def
    : TROPHY_GROUP '=' NAME_2
      {
          const grammar::SemanticBits semantic = $3;
          m_pCard->m_pTrophyTable =
              Singleton<MonsterBook::Script>::inst().GetTrophyTable(grammar::SemanticString(semantic));
          if (m_pCard->m_pTrophyTable == nullptr) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[MonsterBook][Trophy table] invalid trophy table id[%d] card[%d]",
              //     grammar::SemanticInt32(semantic),
              //     m_pCard->m_nCardId)
              L2_LOG_ERROR(
                  Log, u"[MonsterBook][Trophy table] invalid trophy table id[%d] card[%d]",
                  grammar::SemanticInt32(semantic), m_pCard->m_nCardId
              );
          }
      }
    ;

reward_fp_def
    : REWARD_FP '=' '{' n19 '}'
      {
          m_pCard->AddFactionPoints(m_vIntList);
          m_vIntList.clear();
      }
    ;

reward_exp_def
    : REWARD_EXP '=' '{' n19 '}'
      {
          m_pCard->AddExpPoints(m_vIntList);
          m_vIntList.clear();
      }
    ;

reward_sp_def
    : REWARD_SP '=' '{' n19 '}'
      {
          m_pCard->AddSPPoints(m_vIntList);
          m_vIntList.clear();
      }
    ;

reward_item_1_def
    : REWARD_ITEM_1 '=' '{' n20 '}'
      {
          m_pCard->AddItems(0, m_vItemList);
          m_vItemList.clear();
      }
    ;

reward_item_2_def
    : REWARD_ITEM_2 '=' '{' n20 '}'
      {
          m_pCard->AddItems(1, m_vItemList);
          m_vItemList.clear();
      }
    ;

reward_item_3_def
    : REWARD_ITEM_3 '=' '{' n20 '}'
      {
          m_pCard->AddItems(2, m_vItemList);
          m_vItemList.clear();
      }
    ;

reward_item_4_def
    : REWARD_ITEM_4 '=' '{' n20 '}'
      {
          m_pCard->AddItems(3, m_vItemList);
          m_vItemList.clear();
      }
    ;

n19
    : n19 ';' INTEGER
      { m_vIntList.push_back(grammar::SemanticInt64($3)); }
    | INTEGER
      { m_vIntList.push_back(grammar::SemanticInt64($1)); }
    ;

n20
    : n20 ';' n21
    | n21
    | %empty
    ;

n21
    : '{' INTEGER ';' INTEGER '}'
      {
          m_vItemList.push_back(ItemAmountData(grammar::SemanticInt32($2), grammar::SemanticInt64($4)));
      }
    | INTEGER ';' INTEGER
      {
          m_vItemList.push_back(ItemAmountData(grammar::SemanticInt32($1), grammar::SemanticInt64($3)));
      }
    ;

trophy_list
    : trophy_list trophy
    | trophy
    | %empty
    ;

trophy
    : TROPHY_BEGIN
      { m_pTrophyTable = new (std::nothrow) MonsterBook::Script::TrophyTable; }
      name_def_list TROPHY_END
      {
          for (int level = 0; level < 4; ++level) {
              if (m_pTrophyTable->GetKillCount(level) == 0) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"[MonsterBookTrophy][Trophy Kill count] kill counat lev[%d] is 0!",
                  //     level + 1)
                  L2_LOG_ERROR(Log, u"[MonsterBookTrophy][Trophy Kill count] kill counat lev[%d] is 0!", level + 1);
              }
          }
          Singleton<MonsterBook::Script>::inst().AddKillTable(m_tableName, m_pTrophyTable);
          m_tableName.assign(u"");
      }
    ;

name_def_list
    : name_def_list name_def
    | name_def
    | %empty
    ;

name_def
    : NAME '=' NAME_2
      { m_tableName.assign(grammar::SemanticString($3)); }
    | ID '=' INTEGER
    | LEVEL_1 '=' INTEGER
      { m_pTrophyTable->m_nKillData[0] = grammar::SemanticInt32($3); }
    | LEVEL_2 '=' INTEGER
      { m_pTrophyTable->m_nKillData[1] = grammar::SemanticInt32($3); }
    | LEVEL_3 '=' INTEGER
      { m_pTrophyTable->m_nKillData[2] = grammar::SemanticInt32($3); }
    | LEVEL_4 '=' INTEGER
      { m_pTrophyTable->m_nKillData[3] = grammar::SemanticInt32($3); }
    | %empty
    ;

%%
