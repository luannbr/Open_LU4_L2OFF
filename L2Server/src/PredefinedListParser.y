// predefinedlistdata.txt grammar.
// Bison generates PredefinedListParser.cpp and PredefinedListParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/PredefinedListParser.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/SharedContainer.h"

#include "Shared/src/BaseStructure.h"
#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "Shared/src/Territory.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "PredefinedListParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForPredefinedList::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"PredefinedListParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

CParserForPredefinedList::CParserForPredefinedList() :
    m_lexer(),
    m_bOnlyPrecompiledHeader(false),
    m_pPredefinedSkillList(nullptr),
    m_pPredefinedItemList(nullptr),
    m_pTerritory(nullptr),
    m_eSkillOPTargetType(SOTT_UNDEFINDED),
    m_pTerritoryElement(nullptr),
    m_pPredefinedTerritoryList(nullptr),
    m_setPredefinedListID(),
    m_setPredefinedListName(),
    tempPredefinedListName() {
}

CParserForPredefinedList::~CParserForPredefinedList() = default;

int CParserForPredefinedList::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}

void CParserForPredefinedList::StartList(int predefinedListId, const char16_t* predefinedListName, PredefinedListType type) {
    const char16_t* const name = predefinedListName == nullptr ? u"" : predefinedListName;
    if (*name == u'\0') {
        // Log.Add(
        //     LOG_ERROR,
        //     L"empty name. Line = [%d]",
        //     m_lexer.line + 1)
        // L2_LOG_ERROR(Log,
        //     L"empty name. Line = [%d]",
        //     m_lexer.line + 1)
        L2_LOG_ERROR(
            Log, "predefinedlistdata.txt line %lld: list %d has an empty name; parsing continues.",
            static_cast<long long>(m_lexer.line) + 1, predefinedListId
        );
    }
    if (m_setPredefinedListID.count(predefinedListId) != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"duplicated skill_list_id. [%d] Line = [%d]",
        //     predefinedListId,
        //     m_lexer.line + 1)
        // L2_LOG_ERROR(Log,
        //     L"duplicated skill_list_id. [%d] Line = [%d]",
        //     predefinedListId,
        //     m_lexer.line + 1)
        L2_LOG_ERROR(
            Log, "predefinedlistdata.txt line %lld: list ID %d is repeated in this file; parsing continues.",
            static_cast<long long>(m_lexer.line) + 1, predefinedListId
        );
    }

    const std::u16string listName(name);
    if (m_setPredefinedListName.count(listName) != 0) {
        // const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
        const std::string hostName = Utf16ToUtf8(name);
        // Log.Add(
        //     LOG_ERROR,
        //     L"duplicated skill_list_name [%s] Line = [%d]",
        //     hostName.c_str(),
        //     m_lexer.line + 1)
        // L2_LOG_ERROR(Log,
        //     L"duplicated skill_list_name [%s] Line = [%d]",
        //     hostName.c_str(),
        //     m_lexer.line + 1)
        L2_LOG_ERROR(
            Log, "predefinedlistdata.txt line %lld: list name '%s' is repeated for ID %d; parsing continues.",
            static_cast<long long>(m_lexer.line) + 1, hostName.c_str(), predefinedListId
        );
    }
    m_setPredefinedListID.insert(predefinedListId);
    m_setPredefinedListName.insert(listName);
    tempPredefinedListName.assign(name);

    switch (type) {
        case PREDEFINED_LIST_SKILL:
            m_pPredefinedSkillList = new PredefinedSkillList(predefinedListId);
            break;
        case PREDEFINED_LIST_ITEM:
            m_pPredefinedItemList = new PredefinedItemList(predefinedListId);
            break;
        case PREDEFINED_LIST_TERRITORY:
            m_pPredefinedTerritoryList = new PredefinedTerritoryList(predefinedListId);
            break;
    }
}
}

%token EQUAL
%token PREDEFINED_SKILL_LIST_BEGIN
%token PREDEFINED_SKILL_LIST_END
%token SKILL_LIST
%token PREDEFINED_ITEM_LIST_BEGIN
%token PREDEFINED_ITEM_LIST_END
%token ITEM_LIST
%token PREDEFINED_TERRITORY_LIST_BEGIN
%token PREDEFINED_TERRITORY_LIST_END
%token TERRITORY_LIST
%token OP_NOT_TERRITORY
%token NAME
%token IDENTIFIER
%token INTEGER
%start n1

%%

n1
    : n1 n2
    | %empty
    ;

n2
    : predefined_skill_list
    | predefined_item_list
    | predefined_territory_list
    ;

predefined_skill_list
    : PREDEFINED_SKILL_LIST_BEGIN NAME INTEGER
      {
          StartList(grammar::SemanticInt32($3), grammar::SemanticString($2), PREDEFINED_LIST_SKILL);
      }
      skill_list_def PREDEFINED_SKILL_LIST_END
      {
          PredefinedListDB::inst().RegisterNoToPredefinedList(grammar::SemanticInt32($3), m_pPredefinedSkillList);
          PredefinedListDB::inst().RegisterNameToPredefinedList(tempPredefinedListName, m_pPredefinedSkillList);
      }
    ;

skill_list_def
    : SKILL_LIST EQUAL '{' n6 '}'
    ;

n6
    : n6 ';' name_def
    | name_def
    ;

name_def
    : NAME
      {
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          const char16_t* const name = grammar::SemanticString($1);
          const std::int64_t value = g_PrecompiledHeaderDB.Search(name);
          if (value == -1) {
              // const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
              const std::string hostName = Utf16ToUtf8(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Invalid SkillName[%s] at [%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Invalid SkillName[%s] at [%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "predefinedlistdata.txt line %lld: skill '%s' has no PCH entry; ID -1 is retained in skill list '%s'.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str(),
                  Utf16ToUtf8(tempPredefinedListName.c_str()).c_str()
              );
          }
          m_pPredefinedSkillList->m_vPredefinedSkillIDList.push_back(
              static_cast<std::int64_t>(static_cast<std::int32_t>(static_cast<std::uint64_t>(value) >> 32))
          );
          m_pPredefinedSkillList->AddName(name);
      }
    ;

predefined_item_list
    : PREDEFINED_ITEM_LIST_BEGIN NAME INTEGER
      {
          StartList(grammar::SemanticInt32($3), grammar::SemanticString($2), PREDEFINED_LIST_ITEM);
      }
      item_list_def PREDEFINED_ITEM_LIST_END
      {
          PredefinedListDB::inst().RegisterNoToPredefinedList(grammar::SemanticInt32($3), m_pPredefinedItemList);
          PredefinedListDB::inst().RegisterNameToPredefinedList(tempPredefinedListName, m_pPredefinedItemList);
      }
    ;

item_list_def
    : ITEM_LIST EQUAL '{' n11 '}'
    ;

n11
    : n11 ';' name_def_2
    | name_def_2
    ;

name_def_2
    : NAME
      {
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          const char16_t* const name = grammar::SemanticString($1);
          const std::int64_t value = g_PrecompiledHeaderDB.Search(name);
          if (static_cast<std::int32_t>(value) == -1) {
              // const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
              const std::string hostName = Utf16ToUtf8(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Invalid ItemName[%s] at [%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Invalid ItemName[%s] at [%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "predefinedlistdata.txt line %lld: item '%s' resolves to invalid item ID -1; the entry is retained in "
                  "item list '%s'.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str(),
                  Utf16ToUtf8(tempPredefinedListName.c_str()).c_str()
              );
          }
          m_pPredefinedItemList->m_vPredefinedItemIDList.push_back(
              static_cast<std::int64_t>(static_cast<std::int32_t>(value))
          );
          m_pPredefinedItemList->AddName(name);
      }
    ;

predefined_territory_list
    : PREDEFINED_TERRITORY_LIST_BEGIN NAME INTEGER
      {
          StartList(grammar::SemanticInt32($3), grammar::SemanticString($2), PREDEFINED_LIST_TERRITORY);
      }
      territory_list_def PREDEFINED_TERRITORY_LIST_END
      {
          PredefinedListDB::inst().RegisterNoToPredefinedList(grammar::SemanticInt32($3), m_pPredefinedTerritoryList);
          PredefinedListDB::inst().RegisterNameToPredefinedList(tempPredefinedListName, m_pPredefinedTerritoryList);
      }
    ;

territory_list_def
    : TERRITORY_LIST EQUAL '{' n16 '}'
    ;

n16
    : n16 ';' n17
    | n17
    ;

n17
    : '{'
      { m_pTerritoryElement = new TerritoryElement; }
      op_not_territory_def '}'
      {
          m_pPredefinedTerritoryList->m_vPredefinedTerritoryList.push_back(m_pTerritoryElement);
      }
    ;

op_not_territory_def
    : OP_NOT_TERRITORY ';' n20 ';' IDENTIFIER
      {
          const char16_t* const target = grammar::SemanticString($5);
          if (std::u16string_view(target == nullptr ? u"" : target) == u"t_self") {
              m_eSkillOPTargetType = SOTT_SELF;
          } else if (std::u16string_view(target == nullptr ? u"" : target) == u"t_target") {
              m_eSkillOPTargetType = SOTT_TARGET;
          } else if (std::u16string_view(target == nullptr ? u"" : target) == u"t_all") {
              m_eSkillOPTargetType = SOTT_ALL;
          } else {
              m_eSkillOPTargetType = SOTT_UNDEFINDED;
              // const std::wstring hostTarget = target_utf16::ToHostCodeUnits(target)
              const std::string hostTarget = Utf16ToUtf8(target);
              // Log.Add(
              //     LOG_ERROR,
              //     L"undefined SkillOperationTargetType [%s] at [%d]",
              //     hostTarget.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"undefined SkillOperationTargetType [%s] at [%d]",
              //     hostTarget.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "predefinedlistdata.txt line %lld: territory list '%s' has unknown target '%s'; the element uses the "
                  "undefined target type.",
                  static_cast<long long>(m_lexer.line) + 1,
                  Utf16ToUtf8(tempPredefinedListName.c_str()).c_str(), hostTarget.c_str()
              );
          }
          m_pTerritoryElement->SetSkillOPTargetType(m_eSkillOPTargetType);
      }
    ;

n20
    : '{'
      { m_pTerritory = new CTerritory; }
      n23 { } '}'
      { m_pTerritoryElement->SetTerritory(m_pTerritory); }
    ;

n23
    : n23 ';' n24
    | n24
    ;

n24
    : '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          const int x = grammar::SemanticInt32($2);
          const int y = grammar::SemanticInt32($4);
          const int bottom = grammar::SemanticInt32($6);
          const int top = grammar::SemanticInt32($8);
          if (bottom < m_pTerritory->GetBottom()) {
              m_pTerritory->SetBottom(bottom);
          }
          if (m_pTerritory->GetTop() < top) {
              m_pTerritory->SetTop(top);
          }
          FVector point(x, y, 0);
          m_pTerritory->AddTerritoryPoint(&point);
      }
    ;

%%
