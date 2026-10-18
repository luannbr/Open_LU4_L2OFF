// CParserForCombinationItem grammar.
// Bison generates ParserCombinationItem.cpp and ParserCombinationItemTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserCombinationItem.h"

#include "L2Server/src/Item.h"
#include "L2Server/src/ObjectDB.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Utils.h"

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string_view>

#include "ParserCombinationItemTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForCombinationItem::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserCombinationItemTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

CParserForCombinationItem::CParserForCombinationItem() : m_lexer() {}

CParserForCombinationItem::~CParserForCombinationItem() = default;

int CParserForCombinationItem::Create(CObjectDB* objectDb, char16_t* fileBuffer, int fileSize, bool onlyPrecompiledHeader) {
    m_pObjDB = objectDb;
    return m_lexer.Create(fileBuffer, fileSize, onlyPrecompiledHeader);
}

void CParserForCombinationItem::yysyntaxerror() {
    char16_t message[60]{};
    FormatUtf16To(message, std::size(message), u"CombinationItemDataChecker[%d] : syntax error", m_lexer.line + 1);
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

%token COMBINATIONITEM_BEGIN
%token COMBINATIONITEM_END
%token SLOTONE
%token SLOTTWO
%token PROB
%token RESULTITEM
%token INTEGER
%token ID
%token MATERIAL
%token RESULT
%token KEY_ITEM
%token COMBINATION
%start combinationitem_list

%%

combinationitem_list
    : combinationitem_list combinationitem
    | combinationitem
    ;

combinationitem
    : COMBINATIONITEM_BEGIN n3
    | COMBINATIONITEM_BEGIN n7
    ;

n3
    : integer_def slotone_def_list COMBINATIONITEM_END
      {
          if (!m_pCombinationItem->IsValidCombinationItem()) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"INVALID_SCRIPT - slotone={%d.%d} slottwo={%d.%d} prob={%d} resuleitem={%d.%d}{%d.%d}Line = [%d]",
              //     m_pCombinationItem->m_nSlotOne,
              //     m_pCombinationItem->m_nSlotOneAmount,
              //     m_pCombinationItem->m_nSlotTwo,
              //     m_pCombinationItem->m_nSlotTwoAmount,
              //     m_pCombinationItem->m_nProb,
              //     m_pCombinationItem->m_nSuccessResultItem,
              //     m_pCombinationItem->m_nSuccessResultItemAmount,
              //     m_pCombinationItem->m_nFailResultItem,
              //     m_pCombinationItem->m_nFailResultItemAmount,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log, u"INVALID_SCRIPT - slotone={%d;%d} slottwo={%d;%d} prob={%d} resuleitem={%d;%d}{%d;%d}Line = [%d]",
                  m_pCombinationItem->m_nSlotOne, m_pCombinationItem->m_nSlotOneAmount, m_pCombinationItem->m_nSlotTwo,
                  m_pCombinationItem->m_nSlotTwoAmount, m_pCombinationItem->m_nProb,
                  m_pCombinationItem->m_nSuccessResultItem, m_pCombinationItem->m_nSuccessResultItemAmount,
                  m_pCombinationItem->m_nFailResultItem, m_pCombinationItem->m_nFailResultItemAmount, m_lexer.line + 1
              );
          }
          if (m_pObjDB->IsExistCombination(m_pCombinationItem->m_nSlotOne, m_pCombinationItem->m_nSlotTwo)) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"INVALID_SCRIPT - [ALREADY_EXIST] slotone={%d.%d} slottwo={%d.%d}  Check CombinationItem!!! Line =
              //     [%d]", m_pCombinationItem->m_nSlotOne, m_pCombinationItem->m_nSlotOneAmount,
              //     m_pCombinationItem->m_nSlotTwo,
              //     m_pCombinationItem->m_nSlotTwoAmount,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"INVALID_SCRIPT - [ALREADY_EXIST] slotone={%d;%d} slottwo={%d;%d}  Check CombinationItem!!! Line = "
                  u"[%d]",
                  m_pCombinationItem->m_nSlotOne, m_pCombinationItem->m_nSlotOneAmount, m_pCombinationItem->m_nSlotTwo,
                  m_pCombinationItem->m_nSlotTwoAmount, m_lexer.line + 1
              );
          }
          m_pObjDB->SetCombinationItemStruct(m_pCombinationItem);
      }
    ;

integer_def
    : INTEGER
      { m_pCombinationItem = new CCombinationItem; }
    ;

slotone_def_list
    : slotone_def_list slotone_def
    | %empty
    ;

slotone_def
    : SLOTONE '=' '{' INTEGER ';' INTEGER '}'
      { m_pCombinationItem->SetOne(grammar::SemanticInt32($4), grammar::SemanticInt64($6)); }
    | SLOTTWO '=' '{' INTEGER ';' INTEGER '}'
      { m_pCombinationItem->SetTwo(grammar::SemanticInt32($4), grammar::SemanticInt64($6)); }
    | PROB '=' INTEGER
      { m_pCombinationItem->SetProb(grammar::SemanticInt32($3)); }
    | RESULTITEM '=' '{' '{' INTEGER ';' INTEGER '}' ';' '{' INTEGER ';' INTEGER '}' '}'
      {
          m_pCombinationItem->SetResult(
              grammar::SemanticInt32($5), grammar::SemanticInt64($7), grammar::SemanticInt32($11),
              grammar::SemanticInt64($13)
          );
      }
    ;

n7
    :
      {
          m_pCombItemSet = nullptr;
          m_pCombInfo = nullptr;
          m_nId = -1;
          m_pKeyItem = new CombinationItemKey(-1, -1);
      }
      id_def key_item_def combination_def COMBINATIONITEM_END
      {
          m_pCombItemSet->m_nId = m_nId;
          g_CombinationManager.AddCombinationSet(*m_pKeyItem, m_pCombItemSet);
      }
    ;

id_def
    : ID '=' INTEGER
      { m_nId = grammar::SemanticInt32($3); }
    ;

key_item_def
    : KEY_ITEM '=' '{' INTEGER ';' INTEGER '}'
      {
          m_pKeyItem->m_PrimaryKey = grammar::SemanticInt32($4);
          m_pKeyItem->m_SecondaryKey = grammar::SemanticInt64($6);
      }
    ;

combination_def
    :
      { m_pCombItemSet = new CombinationItemInfoSet; }
      COMBINATION '=' '{' n13 '}'
    ;

n13
    : n13 ';' n14
    | n14
    ;

n14
    : n15 '{' MATERIAL '=' '{' n16 '}' ';' prob_def ';' result_def '}'
      { m_pCombItemSet->m_vCombinationList.push_back(m_pCombInfo); }
    | n15 MATERIAL '=' '{' n16 '}' ';' prob_def ';' result_def
      { m_pCombItemSet->m_vCombinationList.push_back(m_pCombInfo); }
    ;

n15
    : %empty
      { m_pCombInfo = new CombinationItemInfo; }
    ;

n16
    : n16 ';' n17
    | n17
    ;

n17
    : '{' INTEGER ';' INTEGER '}'
      { m_pCombInfo->AddMaterialItem(grammar::SemanticInt32($2), grammar::SemanticInt64($4)); }
    | INTEGER ';' INTEGER
      { m_pCombInfo->AddMaterialItem(grammar::SemanticInt32($1), grammar::SemanticInt64($3)); }
    ;

prob_def
    : PROB '=' INTEGER
      { m_pCombInfo->SetProb(grammar::SemanticInt32($3)); }
    ;

result_def
    : RESULT '=' '{' '{' INTEGER ';' INTEGER '}' ';' '{' INTEGER ';' INTEGER '}' '}'
      {
          m_pCombInfo->SetSuccessItem(grammar::SemanticInt32($5), grammar::SemanticInt64($7));
          m_pCombInfo->SetFailItem(grammar::SemanticInt32($11), grammar::SemanticInt64($13));
      }
    ;

%%
