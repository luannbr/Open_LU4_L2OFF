// CParserForEnchantOption grammar (original L2Server/parser_enchantoption.y).
// Bison generates ParserEnchantOption.cpp and ParserEnchantOptionTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserEnchantOption.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/EnchantOption.h"
#include "L2Server/src/Item.h"
#include "L2Server/src/Object.h"
#include "L2Server/src/ObjectDB.h"
#include "L2Server/src/OptionDB.h"
#include "L2Server/src/Skill.h"

#include "Shared/src/Log.h"
#include "Shared/src/SharedFactory.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "ParserEnchantOptionTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForEnchantOption::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserEnchantOptionTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

CParserForEnchantOption::CParserForEnchantOption() :
    m_lexer(),
    m_pEnchantOption(nullptr),
    m_pItemEnchantOption(nullptr),
    m_pItem(nullptr),
    m_Enchant(0),
    m_itemNameList(),
    m_itemList() {
}

CParserForEnchantOption::~CParserForEnchantOption() = default;

int CParserForEnchantOption::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}
}

%token ENCHANT_OPTION_BEGIN
%token ENCHANT_OPTION_END
%token ITEM
%token OPTION_BEGIN
%token OPTION_END
%token ENCHANT
%token OPTION
%token SKILL
%token ENCHANT_MAX_LEVEL
%token ENCHANT_SUCCESS_RATE
%token NAME
%token INTEGER
%start n1

%%

n1
    : enchant_option_list
    ;

enchant_option_list
    : enchant_option_list enchant_option
    | enchant_option
    ;

enchant_option
    : ENCHANT_OPTION_BEGIN ITEM '=' n6
      {
          [&] {
              for (const std::u16string& itemName : m_itemNameList) {
                  CObject* const object = g_ObjectDB.GetObjectByName(itemName.c_str());
                  if (object == nullptr || !object->IsItem()) {
                      // Log.Add(
                      //     LOG_ERROR,
                      //     L"Undefined item[%s] in line[%d] while loading enchantOption",
                      //     hostName.c_str(),
                      //     m_lexer.line + 1)
                      // L2_LOG_ERROR(Log,
                      //     L"Undefined item[%s] in line[%d] while loading enchantOption",
                      //     hostName.c_str(),
                      //     m_lexer.line + 1)
                      L2_LOG_ERROR(
                          Log,
                          "Enchant-option item '%s' at script line %lld is missing or is not an item; resolving this "
                          "item-name batch stops, retaining already resolved items.",
                          Utf16ToUtf8(std::u16string_view(itemName.c_str() != nullptr ? itemName.c_str() : u"<null>"))
                              .c_str(),
                          static_cast<long long>(m_lexer.line) + 1
                      );
                      return;
                  }
                  m_itemList.push_back(object->CastItem());
              }
              m_itemNameList.clear();
          }();
      }
      OPTION_BEGIN
      {
          m_pItemEnchantOption = new CItemEnchantOption;
          m_pItemEnchantOption->m_bDeleted = false;
          m_pItemEnchantOption->m_EnchantSuccessInfo.Init(0);
      }
      enchant_def_list OPTION_END enchant_max_level_def enchant_success_rate_def ENCHANT_OPTION_END
      {
          if (m_pItemEnchantOption == nullptr) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"line[%d], m_pItemEnchantOption is null",
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"line[%d], m_pItemEnchantOption is null",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Cannot publish enchant options at script line %lld: no item-enchant definition is active; publication "
                  "is skipped.",
                  static_cast<long long>(m_lexer.line) + 1
              );
              break;
          }
          if (!m_pItemEnchantOption->IsValid()) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"line[%d], m_pItemEnchantOption->IsValid() failed. enchant_max_level[%d], number of
              //     enchant_success_rate[%d], number of options[%d]", m_lexer.line + 1,
              //     m_pItemEnchantOption->m_EnchantSuccessInfo.echantMaxLevel,
              //     m_pItemEnchantOption->m_EnchantSuccessInfo.index,
              //     static_cast<int>(m_pItemEnchantOption->m_EnchantOptionMap.size()))
              // L2_LOG_ERROR(Log,
              //     L"line[%d], m_pItemEnchantOption->IsValid() failed. enchant_max_level[%d], number of
              //     enchant_success_rate[%d], number of options[%d]", m_lexer.line + 1,
              //     m_pItemEnchantOption->m_EnchantSuccessInfo.echantMaxLevel,
              //     m_pItemEnchantOption->m_EnchantSuccessInfo.index,
              //     static_cast<int>(m_pItemEnchantOption->m_EnchantOptionMap.size()))
              L2_LOG_ERROR(
                  Log,
                  "Cannot publish enchant options at script line %lld: definition validation failed (maximum level %d, "
                  "success-rate count %d, option-level entries %zu); publication is skipped.",
                  static_cast<long long>(m_lexer.line) + 1, m_pItemEnchantOption->m_EnchantSuccessInfo.echantMaxLevel,
                  m_pItemEnchantOption->m_EnchantSuccessInfo.index,
                  static_cast<std::size_t>(m_pItemEnchantOption->m_EnchantOptionMap.size())
              );
              break;
          }
          for (CItem* const item : m_itemList) {
              m_pItem = item;
              if (!CEnchantOptionDB::Inst().AddItemEnchantOption(item->m_data->m_nClassID, m_pItemEnchantOption)) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Duplicated item(%s) in line[%d] while loading enchantOption",
                  //     item->GetName(),
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"Duplicated item(%s) in line[%d] while loading enchantOption",
                  //     item->GetName(),
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     "Item '%s' already has an enchant-option definition at script line %lld. Its existing definition
                  //     is retained.", Utf16ToUtf8(target_utf16::FromHostCodeUnits(item->GetName())).c_str(),
                  //     static_cast<long long>(m_lexer.line) + 1)
                  L2_LOG_ERROR(
                      Log,
                      "Item '%s' already has an enchant-option definition at script line %lld; its existing definition is "
                      "retained.",
                      Utf16ToUtf8(std::u16string(item->GetName())).c_str(), static_cast<long long>(m_lexer.line) + 1
                  );
              }
          }
          m_itemList.clear();
          m_pItemEnchantOption = nullptr;
          m_pItem = nullptr;
      }
    ;

n6
    : name_def
    | '{' n7 '}'
    ;

n7
    : n7 ';' name_def
    | name_def
    ;

name_def
    : NAME
      {
          m_itemNameList.push_back(std::u16string(grammar::SemanticString($1)));
      }
    ;

enchant_def_list
    : enchant_def_list enchant_def
    | enchant_def
    ;

enchant_def
    : ENCHANT '=' INTEGER
      {
          m_pEnchantOption = new CEnchantOption;
          m_Enchant = grammar::SemanticInt32($3);
      }
      n12 n16
      {
          if (!m_pItemEnchantOption->AddEnchantOption(m_Enchant, m_pEnchantOption)) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Duplicated enchant option and enchant(%d) in line[%d] while loading enchantOption",
              //     m_Enchant,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Duplicated enchant option and enchant(%d) in line[%d] while loading enchantOption",
              //     m_Enchant,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Enchant level %d already has an option definition at script line %lld; the existing definition is "
                  "retained.",
                  m_Enchant, static_cast<long long>(m_lexer.line) + 1
              );
          }
          m_Enchant = 0;
          m_pEnchantOption = nullptr;
      }
    ;

n12
    : option_def
    | %empty
    ;

option_def
    : OPTION '=' '{' n14 '}'
    ;

n14
    : n14 ';' name_def_2
    | name_def_2
    ;

name_def_2
    : NAME
      {
          const std::u16string optionName(grammar::SemanticString($1));
          Option* const option = OptionDB::inst().FindByOptionName(optionName, true);
          if (option == nullptr) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined option[%s] in line[%d] while loading enchantOption",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Undefined option[%s] in line[%d] while loading enchantOption",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log, "Undefined enchant option '%s' at script line %lld; the option is not added.",
                  Utf16ToUtf8(std::u16string_view(optionName.c_str() != nullptr ? optionName.c_str() : u"<null>")).c_str(),
                  static_cast<long long>(m_lexer.line) + 1
              );
              break;
          }
          if (!m_pEnchantOption->AddOption(option)) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Too many options. (max:%d) in line[%d] while loading enchantOption",
              //     3,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Too many options. (max:%d) in line[%d] while loading enchantOption",
              //     3,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Cannot add enchant option '%s' at script line %lld: all three option slots are occupied; the option is "
                  "skipped.",
                  Utf16ToUtf8(std::u16string_view(optionName.c_str() != nullptr ? optionName.c_str() : u"<null>")).c_str(),
                  static_cast<long long>(m_lexer.line) + 1
              );
          }
      }
    ;

n16
    : skill_def
    | %empty
    ;

skill_def
    : SKILL '=' '{' n18 '}'
    ;

n18
    : n18 ';' name_def_3
    | name_def_3
    ;

name_def_3
    : NAME
      {
          const char16_t* const targetName = grammar::SemanticString($1);
          const std::wstring skillName = target_utf16::ToHostCodeUnits(targetName);
          const CSkillInfo* const skillInfo = g_SkillDB.GetSkillInfoByName(skillName.c_str());
          if (skillInfo == nullptr) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined skill[%s] in line[%d] while loading enchantOption",
              //     skillName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Undefined skill[%s] in line[%d] while loading enchantOption",
              //     skillName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log, "Undefined acquired enchant skill '%s' at script line %lld; the skill is not added.",
                  Utf16ToUtf8(std::u16string_view(targetName != nullptr ? targetName : u"<null>")).c_str(),
                  static_cast<long long>(m_lexer.line) + 1
              );
          } else if (!m_pEnchantOption->AddAcquiredSkill(skillInfo)) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Duplicated skills(%s) int line[%d] while loading enchantOption",
              //     skillName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Duplicated skills(%s) int line[%d] while loading enchantOption",
              //     skillName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Acquired enchant skill '%s' duplicates an existing skill ID at script line %lld; the existing skill is "
                  "retained.",
                  Utf16ToUtf8(std::u16string_view(targetName != nullptr ? targetName : u"<null>")).c_str(),
                  static_cast<long long>(m_lexer.line) + 1
              );
          }
      }
    ;

enchant_max_level_def
    : ENCHANT_MAX_LEVEL '=' INTEGER
      {
          const int maxLevel = grammar::SemanticInt32($3);
          if (m_pItemEnchantOption == nullptr) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"line[%d], m_pItemEnchantOption is NULL",
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"line[%d], m_pItemEnchantOption is NULL",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Cannot set enchant maximum level %d at script line %lld: no item-enchant definition is active; the "
                  "setting is skipped.",
                  maxLevel, static_cast<long long>(m_lexer.line) + 1
              );
          } else if (!m_pItemEnchantOption->m_EnchantSuccessInfo.Init(maxLevel)) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"line[%d], info->Init(%d) failed",
              //     m_lexer.line + 1,
              //     maxLevel)
              // L2_LOG_ERROR(Log,
              //     L"line[%d], info->Init(%d) failed",
              //     m_lexer.line + 1,
              //     maxLevel)
              L2_LOG_ERROR(
                  Log, "Cannot initialize enchant success rates for maximum level %d at script line %lld: %s.", maxLevel,
                  static_cast<long long>(m_lexer.line) + 1,
                  maxLevel < 0 || maxLevel > 100 ? "expected 0 through 100; previous table retained"
                                                 : "allocation failed; success-rate table cleared"
              );
          }
      }
    ;

enchant_success_rate_def
    : ENCHANT_SUCCESS_RATE '=' '{' n22 '}'
    ;

n22
    : n22 ',' integer_def
    | integer_def
    | %empty
    ;

integer_def
    : INTEGER
      {
          const int successRate = grammar::SemanticInt32($1);
          if (m_pItemEnchantOption == nullptr) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"line[%d], m_pItemEnchantOption is NULL",
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"line[%d], m_pItemEnchantOption is NULL",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Cannot add enchant success rate %d at script line %lld: no item-enchant definition is active; the rate "
                  "is skipped.",
                  successRate, static_cast<long long>(m_lexer.line) + 1
              );
          } else if (!m_pItemEnchantOption->m_EnchantSuccessInfo.AddSuccessRate(successRate)) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"line[%d], info->AddSuccessRate(%d) failed",
              //     m_lexer.line + 1,
              //     successRate)
              // L2_LOG_ERROR(Log,
              //     L"line[%d], info->AddSuccessRate(%d) failed",
              //     m_lexer.line + 1,
              //     successRate)
              L2_LOG_ERROR(
                  Log,
                  "Cannot add enchant success rate %d at script line %lld (maximum level %d, next index %d): the table is "
                  "unavailable, full, or its maximum level is outside 1 through 99; the rate is skipped.",
                  successRate, static_cast<long long>(m_lexer.line) + 1,
                  m_pItemEnchantOption->m_EnchantSuccessInfo.echantMaxLevel,
                  m_pItemEnchantOption->m_EnchantSuccessInfo.index
              );
          }
      }
    ;

%%
