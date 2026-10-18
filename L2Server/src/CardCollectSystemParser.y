// CParserForCardCollectSystem grammar.
// Bison generates CardCollectSystemParser.cpp and CardCollectSystemParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/CardCollectSystemParser.h"

#include "Shared/src/BaseStructure.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Utils.h"

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string_view>

#include "CardCollectSystemParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForCardCollectSystem::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"CardCollectSystemParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

char16_t* SemanticText(const grammar::SemanticBits value) {
    return grammar::SemanticString(value);
}

char16_t FoldAscii(const char16_t value) {
    return value >= u'A' && value <= u'Z' ? static_cast<char16_t>(value + (u'a' - u'A')) : value;
}

bool EqualsAsciiInsensitive(const char16_t* left, const char16_t* right) {
    if (left == nullptr || right == nullptr) {
        return left == right;
    }
    while (*left != u'\0' && *right != u'\0') {
        if (FoldAscii(*left++) != FoldAscii(*right++)) {
            return false;
        }
    }
    return *left == *right;
}

} // namespace

CParserForCardCollectSystem::CParserForCardCollectSystem() :
    m_lFixedManipItemDataList(),
    m_lRandomManipItemDataList(),
    m_lexer(),
    m_nLineNumber(0),
    m_bOnlyPrecompiledHeader(false),
    m_bReload(false) {
    // Current ctor leaves the two active-object pointers
    // uninitialized until actions 0 and 13 create them.
}

CParserForCardCollectSystem::~CParserForCardCollectSystem() = default;

int CParserForCardCollectSystem::Create(char16_t* fileBuffer, const int fileSize) {
    return m_lexer.Create(this, fileBuffer, fileSize);
}

void CParserForCardCollectSystem::yysyntaxerror() {
    char16_t message[60]{};
    // std::swprintf(
    //     message,
    //     std::size(message),
    //     L"ExDropItem Checker[%d] : syntax error",
    //     m_lexer.line + 1)
    FormatUtf16To(message, std::size(message), u"Card collection[%d] : syntax error", m_lexer.line + 1);
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
%token CARD_COLLECT_BEGIN
%token ID
%token CARD_COLLECT_END
%token SOURCE_CARD_LIST
%token FIXED_REWARD_LIST
%token RANDOM_REWARD_LIST
%token RANDOM_REWARD_SECOND_LIST
%token CARD_COLLECT_EVENT_BEGIN
%token CARD_COLLECT_EVENT_END
%token EVENT_ID
%token EVENT_START
%token EVENT_END
%token EVENT_TYPE
%token COLLECT_ID_LIST
%token NAME
%token INTEGER
%token FLOAT_NUMBER
%start n1

%%

n1
    : n2
    ;

n2
    : n2 n3
    | %empty
    ;

n3
    : card_collect
    | card_collect_event
    ;

card_collect
    : CARD_COLLECT_BEGIN ID EQUAL INTEGER
      {
          m_pCCardCollectSystemData = new CCardCollectSystemData(grammar::SemanticInt32($4));
      }
      source_card_list_def_list CARD_COLLECT_END
      {
          CCardCollectSystem::inst().AddData(m_pCCardCollectSystemData);
          ++m_nLineNumber;
      }
    ;

source_card_list_def_list
    : source_card_list_def_list source_card_list_def
    | %empty
    ;

source_card_list_def
    : SOURCE_CARD_LIST EQUAL '{' n8 '}'
      {
          m_pCCardCollectSystemData->SetSourceCardList(&m_lFixedManipItemDataList);
          m_lFixedManipItemDataList.clear();
      }
    | FIXED_REWARD_LIST EQUAL '{' n8 '}'
      {
          m_pCCardCollectSystemData->SetFixedCardList(&m_lFixedManipItemDataList);
          m_lFixedManipItemDataList.clear();
      }
    | RANDOM_REWARD_LIST EQUAL '{' n10 '}'
      {
          m_pCCardCollectSystemData->SetRandomCardList(&m_lRandomManipItemDataList);
          m_lRandomManipItemDataList.clear();
      }
    | RANDOM_REWARD_SECOND_LIST EQUAL '{' n10 '}'
      {
          m_pCCardCollectSystemData->SetSecondRandomCardList(&m_lRandomManipItemDataList);
          m_lRandomManipItemDataList.clear();
      }
    ;

n8
    : n8 ';' n9
    | n9
    | %empty
    ;

n9
    : '{' NAME ';' INTEGER '}'
      {
          m_lFixedManipItemDataList.push_back(
              new CManipulateItemData(SemanticText($2), grammar::SemanticInt32($4))
          );
      }
    | '{' NAME ';' INTEGER ';' FLOAT_NUMBER '}'
      {
          m_lFixedManipItemDataList.push_back(new CManipulateItemData(
              SemanticText($2), grammar::SemanticInt32($4), grammar::SemanticDouble($6)
          ));
      }
    ;

n10
    : n10 ';' n11
    | n11
    | %empty
    ;

n11
    : '{' NAME ';' INTEGER ';' FLOAT_NUMBER '}'
      {
          m_lRandomManipItemDataList.push_back(new CManipulateRandomItemData(
              SemanticText($2), grammar::SemanticInt32($4), grammar::SemanticDouble($6)
          ));
      }
    ;

card_collect_event
    : CARD_COLLECT_EVENT_BEGIN
      {
          m_pEventInfo = new CardCollectEventInfo;
          m_pEventInfo->nStartTime = 0;
          m_pEventInfo->nEndTime = 0;
          m_pEventInfo->nId = 0;
          m_pEventInfo->vIdList.clear();
      }
      event_id_def event event_end_def event_type_def collect_id_list_def CARD_COLLECT_EVENT_END
      {
          CCardCollectSystem::inst().AddEventInfo(m_pEventInfo);
          ++m_nLineNumber;
      }
    ;

event_id_def
    : EVENT_ID EQUAL INTEGER
      { m_pEventInfo->nId = grammar::SemanticInt32($3); }
    | %empty
    ;

event
    : EVENT_START EQUAL NAME
      {
          TimePeriodInfo::ParseTime(SemanticText($3), m_pEventInfo->nStartTime);
      }
    | %empty
    ;

event_end_def
    : EVENT_END EQUAL NAME
      { TimePeriodInfo::ParseTime(SemanticText($3), m_pEventInfo->nEndTime); }
    | %empty
    ;

event_type_def
    : EVENT_TYPE EQUAL NAME
      {
          m_pEventInfo->nType = EqualsAsciiInsensitive(u"ui", SemanticText($3)) ? EVENT_UI : EVENT_NPC;
      }
    | %empty
    ;

collect_id_list_def
    : COLLECT_ID_LIST EQUAL '{' n19 '}'
    | %empty
    ;

n19
    : n19 ';' INTEGER
      { m_pEventInfo->vIdList.push_back(grammar::SemanticInt32($3)); }
    | INTEGER
      { m_pEventInfo->vIdList.push_back(grammar::SemanticInt32($1)); }
    | %empty
    ;

%%
