// skillenchantdata.txt grammar (original L2Server/parser_skillenchantdata.y).
// Bison generates ParserSkillEnchantData.cpp and ParserSkillEnchantDataTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserSkillEnchantData.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ObjectDB.h"
#include "L2Server/src/Skill.h"
#include "L2Server/src/SkillEnchant.h"

#include "Shared/src/Log.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "ParserSkillEnchantDataTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForSkillEnchant::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserSkillEnchantDataTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

CParserForSkillEnchant::CParserForSkillEnchant() : m_lexer(), classType(0), m_alignment1bc(0), Info(nullptr) {}

CParserForSkillEnchant::~CParserForSkillEnchant() = default;

int CParserForSkillEnchant::Create() {
    return m_lexer.Create();
}
}

%token NAME
%token IDENTIFIER
%token INTEGER
%token FLOAT_NUMBER
%token ENCHANT_SKILL_BEGIN
%token ENCHANT_SKILL_END
%token ORIGINAL_SKILL
%token ROUTE_ID
%token ENCHANT_ID
%token SKILL_LEVEL
%token SKILL_SUB_LEVEL
%token ENCHANT_PROB
%token REQUIRED_SP
%token REQUIRED_ITEM
%token IMPORTANCE
%token ITEM_NEEDED
%token PROB_76
%token PROB_77
%token PROB_78
%start enchant_skill_list

%%

enchant_skill_list
    : enchant_skill_list enchant_skill
    | enchant_skill
    ;

enchant_skill
    : ENCHANT_SKILL_BEGIN
      {
          Info = new SkillEnchantInfo;
          Info->nSkillID = -1;
      }
      original_skill_def_list ENCHANT_SKILL_END
      {
          const CSkillInfo* const skillInfo =
              g_SkillDB.GetSkillInfo(CSkillKey(Info->nSkillID, Info->nMinSkillLevel, Info->nSkillSubLevel), nullptr);
          if (skillInfo == nullptr) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"invalid skillinfo id[%d] lev[%d] sub_lev[%d]",
              //     Info->nSkillID,
              //     Info->nMinSkillLevel,
              //     Info->nSkillSubLevel)
              // L2_LOG_ERROR(Log,
              //     L"invalid skillinfo id[%d] lev[%d] sub_lev[%d]",
              //     Info->nSkillID,
              //     Info->nMinSkillLevel,
              //     Info->nSkillSubLevel)
              L2_LOG_ERROR(
                  Log,
                  "skillenchantdata.txt line %lld: skill %d level %d sublevel %d has no available definition; this "
                  "enchant record is not registered.",
                  static_cast<long long>(m_lexer.line) + 1, Info->nSkillID, Info->nMinSkillLevel, Info->nSkillSubLevel
              );
          } else {
              g_SkillEnchantDB.Add(Info);
          }
          Info = nullptr;
      }
    ;

original_skill_def_list
    : original_skill_def_list original_skill_def
    | original_skill_def
    ;

original_skill_def
    : ORIGINAL_SKILL '=' NAME
      {
          const char16_t* const skillName = grammar::SemanticString($3);
          const std::wstring hostSkillName = target_utf16::ToHostCodeUnits(skillName);
          int skillId = 0;
          int skillLevel = 0;
          int skillSubLevel = 0;
          if (!g_SkillDB.GetSkillIdLevelByName(hostSkillName.c_str(), &skillId, &skillLevel, &skillSubLevel)) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"SkillEnchant.txt[%d] : Undefined skill(%s)",
              //     m_lexer.line + 1,
              //     hostSkillName.c_str())
              // L2_LOG_ERROR(Log,
              //     L"SkillEnchant.txt[%d] : Undefined skill(%s)",
              //     m_lexer.line + 1,
              //     hostSkillName.c_str())
              L2_LOG_ERROR(
                  Log,
                  "skillenchantdata.txt line %lld: cannot resolve skill '%s'; the record's skill ID and minimum level are "
                  "left unchanged.",
                  static_cast<long long>(m_lexer.line) + 1, Utf16ToUtf8(skillName).c_str()
              );
          } else {
              Info->nSkillID = skillId;
              Info->nMinSkillLevel = skillLevel;
          }
      }
    | ROUTE_ID '=' INTEGER
      { Info->nRouteId = grammar::SemanticInt32($3); }
    | ENCHANT_ID '=' INTEGER
      { Info->nEnchantId = grammar::SemanticInt32($3); }
    | SKILL_SUB_LEVEL '=' INTEGER
      { Info->nSkillSubLevel = grammar::SemanticInt32($3); }
    | SKILL_LEVEL '=' INTEGER
    | REQUIRED_SP '=' '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          Info->nSp[0] = grammar::SemanticInt64($4);
          Info->nSp[1] = grammar::SemanticInt64($6);
          Info->nSp[2] = grammar::SemanticInt64($8);
          Info->nSp[3] = grammar::SemanticInt64($10);
          Info->nSp[4] = grammar::SemanticInt64($12);
      }
    | REQUIRED_ITEM '=' '{' n7 ';' n9 ';' n11 ';' n13 ';' n15 '}'
    | IMPORTANCE '=' INTEGER
    | ENCHANT_PROB '=' '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          Info->dProb[0] = static_cast<double>(grammar::SemanticInt64($4));
          Info->dProb[1] = static_cast<double>(grammar::SemanticInt64($6));
          Info->dProb[2] = static_cast<double>(grammar::SemanticInt64($8));
          Info->dProb[3] = static_cast<double>(grammar::SemanticInt64($10));
          Info->dProb[4] = static_cast<double>(grammar::SemanticInt64($12));
      }
    | ITEM_NEEDED '=' { } n22
    | PROB_76 '=' INTEGER
    | PROB_77 '=' INTEGER
    | PROB_78 '=' INTEGER
    ;

n7
    : '{' n8 '}'
    | '{' '}'
    | n17
    ;

n8
    : n8 ';' n17
    | n17
    ;

n9
    : '{' n10 '}'
    | '{' '}'
    | n18
    ;

n10
    : n10 ';' n18
    | n18
    ;

n11
    : '{' n12 '}'
    | '{' '}'
    | n19
    ;

n12
    : n12 ';' n19
    | n19
    ;

n13
    : '{' n14 '}'
    | '{' '}'
    | n20
    ;

n14
    : n14 ';' n20
    | n20
    ;

n15
    : '{' n16 '}'
    | '{' '}'
    | n21
    ;

n16
    : n16 ';' n21
    | n21
    ;

n17
    : '{' NAME ';' INTEGER '}'
      {
          const int action = 20;
          const char16_t* const itemName = grammar::SemanticString($2);
          const int itemId = g_ObjectDB.GetClassIdFromName(itemName, false);
          if (itemId == -1) {
              // const std::wstring hostItemName = target_utf16::ToHostCodeUnits(itemName)
              const std::string hostItemName = Utf16ToUtf8(itemName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined word[%s] in line [%d]",
              //     hostItemName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Undefined word[%s] in line [%d]",
              //     hostItemName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "skillenchantdata.txt line %lld: required item '%s' is unknown for enchant operation %d; item ID -1 is "
                  "retained "
                  "in the required-item map.",
                  static_cast<long long>(m_lexer.line) + 1, hostItemName.c_str(), 0
              );
          }
          Info->RequiredItem[action - 20].emplace(itemId, grammar::SemanticInt32($4));
      }
    ;

n18
    : '{' NAME ';' INTEGER '}'
      {
          const int action = 21;
          const char16_t* const itemName = grammar::SemanticString($2);
          const int itemId = g_ObjectDB.GetClassIdFromName(itemName, false);
          if (itemId == -1) {
              // const std::wstring hostItemName = target_utf16::ToHostCodeUnits(itemName)
              const std::string hostItemName = Utf16ToUtf8(itemName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined word[%s] in line [%d]",
              //     hostItemName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Undefined word[%s] in line [%d]",
              //     hostItemName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "skillenchantdata.txt line %lld: required item '%s' is unknown for enchant operation %d; item ID -1 is "
                  "retained "
                  "in the required-item map.",
                  static_cast<long long>(m_lexer.line) + 1, hostItemName.c_str(), 1
              );
          }
          Info->RequiredItem[action - 20].emplace(itemId, grammar::SemanticInt32($4));
      }
    ;

n19
    : '{' NAME ';' INTEGER '}'
      {
          const int action = 22;
          const char16_t* const itemName = grammar::SemanticString($2);
          const int itemId = g_ObjectDB.GetClassIdFromName(itemName, false);
          if (itemId == -1) {
              // const std::wstring hostItemName = target_utf16::ToHostCodeUnits(itemName)
              const std::string hostItemName = Utf16ToUtf8(itemName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined word[%s] in line [%d]",
              //     hostItemName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Undefined word[%s] in line [%d]",
              //     hostItemName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "skillenchantdata.txt line %lld: required item '%s' is unknown for enchant operation %d; item ID -1 is "
                  "retained "
                  "in the required-item map.",
                  static_cast<long long>(m_lexer.line) + 1, hostItemName.c_str(), 2
              );
          }
          Info->RequiredItem[action - 20].emplace(itemId, grammar::SemanticInt32($4));
      }
    ;

n20
    : '{' NAME ';' INTEGER '}'
      {
          const int action = 23;
          const char16_t* const itemName = grammar::SemanticString($2);
          const int itemId = g_ObjectDB.GetClassIdFromName(itemName, false);
          if (itemId == -1) {
              // const std::wstring hostItemName = target_utf16::ToHostCodeUnits(itemName)
              const std::string hostItemName = Utf16ToUtf8(itemName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined word[%s] in line [%d]",
              //     hostItemName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Undefined word[%s] in line [%d]",
              //     hostItemName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "skillenchantdata.txt line %lld: required item '%s' is unknown for enchant operation %d; item ID -1 is "
                  "retained "
                  "in the required-item map.",
                  static_cast<long long>(m_lexer.line) + 1, hostItemName.c_str(), 3
              );
          }
          Info->RequiredItem[action - 20].emplace(itemId, grammar::SemanticInt32($4));
      }
    ;

n21
    : '{' NAME ';' INTEGER '}'
      {
          const int action = 24;
          const char16_t* const itemName = grammar::SemanticString($2);
          const int itemId = g_ObjectDB.GetClassIdFromName(itemName, false);
          if (itemId == -1) {
              // const std::wstring hostItemName = target_utf16::ToHostCodeUnits(itemName)
              const std::string hostItemName = Utf16ToUtf8(itemName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined word[%s] in line [%d]",
              //     hostItemName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Undefined word[%s] in line [%d]",
              //     hostItemName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "skillenchantdata.txt line %lld: required item '%s' is unknown for enchant operation %d; item ID -1 is "
                  "retained "
                  "in the required-item map.",
                  static_cast<long long>(m_lexer.line) + 1, hostItemName.c_str(), 4
              );
          }
          Info->RequiredItem[action - 20].emplace(itemId, grammar::SemanticInt32($4));
      }
    ;

n22
    : '{' n23 '}'
    | '{' '}'
    ;

n23
    : n23 ';' n24
    | n24
    ;

n24
    : '{' NAME ';' INTEGER '}'
    ;

%%
