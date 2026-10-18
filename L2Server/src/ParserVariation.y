// variationdata.txt grammar (original L2Server/parser_variation.y).
// Bison generates ParserVariation.cpp and ParserVariationTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserVariation.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ObjectDB.h"
#include "L2Server/src/OptionDB.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/ThreadManager.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <string_view>

#include "ParserVariationTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForVariation::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserVariationTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

const char16_t* SemanticWord(grammar::SemanticBits value) {
    return grammar::SemanticString(value);
}

} // namespace

CParserForVariation::CParserForVariation() : m_lexer(), m_tempItemGroupNameMap() {}

CParserForVariation::~CParserForVariation() = default;

int CParserForVariation::Create(VariationDB* variationDb) {
    m_pVariationDB = variationDb;
    return m_lexer.Create();
}
}

%token VARIATION_BEGIN
%token VARIATION_END
%token WEAPON_TYPE
%token MAGE
%token WARRIOR
%token VARIATION_EXCEPTION_WEAPON
%token MINERAL
%token VARIATION1
%token VARIATION2
%token FEE_ITEM_NAME
%token FEE_ITEM_COUNT
%token CANCEL_FEE
%token ITEM_GROUP_BEGIN
%token ITEM_GROUP_END
%token ITEM_LIST
%token ITEM_GROUP_NAME
%token FEE_BEGIN
%token FEE_END
%token NAME
%token INTEGER
%token FLOAT_NUMBER
%start n1

%%

n1
    : fee_list
    ;

fee_list
    : fee_list variation
    | fee_list variation_exception_weapon_def
    | fee_list item_group
    | fee_list fee
    | %empty
    ;

variation
    : VARIATION_BEGIN
      { m_pTempVariation = new Variation; }
      weapon_type_def fee_list VARIATION_END
      {
          m_pVariationDB->keyToVariationMap.emplace(m_pTempVariation->key, m_pTempVariation);
      }
    ;

weapon_type_def
    : WEAPON_TYPE '=' WARRIOR MINERAL '=' NAME
      {
          GuardHelper actionGuard(L"variation key_define", CThreadManager::GetThreadID());
          const int classId = g_ObjectDB.GetClassIdFromName(SemanticWord($6), false);
          if (classId == -1) {
              ForceCrash(CrashInfo::LEX_YACC_FAILED);
          }
          m_pVariationDB->RegisterIntenSet(classId);
          m_pTempVariation->key = VariationKey(classId, VariationKey::WARRIOR);
      }
    | WEAPON_TYPE '=' MAGE MINERAL '=' NAME
      {
          GuardHelper actionGuard(L"variation key_define", CThreadManager::GetThreadID());
          const int classId = g_ObjectDB.GetClassIdFromName(SemanticWord($6), false);
          if (classId == -1) {
              ForceCrash(CrashInfo::LEX_YACC_FAILED);
          }
          m_pVariationDB->RegisterIntenSet(classId);
          m_pTempVariation->key = VariationKey(classId, VariationKey::MAGE);
      }
    ;

fee_list
    : variation1_def variation2_def
    ;

variation1_def
    : VARIATION1
      {
          m_groupRate = 0.0;
          m_variType = VARI_NORMAL;
      }
      '=' '{' n10 '}'
      {
          if (std::abs(m_groupRate - 100.0) <= 1.0e-6) {
              break;
          }
          GuardHelper actionGuard(L"parser_variation_variation1_define", CThreadManager::GetThreadID());
          // Log.Add(
          //     LOG_ERROR,
          //     L"Invalid GroupRate line [%d]",
          //     m_lexer.line + 1)
          // L2_LOG_ERROR(Log,
          //     L"Invalid GroupRate line [%d]",
          //     m_lexer.line + 1)
          L2_LOG_ERROR(
              Log,
              "Variation normal group-rate total %.17g%% at script line %lld is not within 0.000001 of 100%%; invoking "
              "the fatal parser failure handler.",
              m_groupRate, static_cast<long long>(m_lexer.line) + 1
          );
          ForceCrash(CrashInfo::LEX_YACC_FAILED);
      }
    ;

variation2_def
    : VARIATION2
      {
          m_groupRate = 0.0;
          m_variType = VARI_RANDOM;
      }
      '=' '{' n10 '}'
      {
          if (std::abs(m_groupRate - 100.0) <= 1.0e-6) {
              break;
          }
          GuardHelper actionGuard(L"parser_variation_variation2_define", CThreadManager::GetThreadID());
          // Log.Add(
          //     LOG_ERROR,
          //     L"Invalid GroupRate Sum [%f]%% line [%d]",
          //     m_groupRate,
          //     m_lexer.line + 1)
          // L2_LOG_ERROR(Log,
          //     L"Invalid GroupRate Sum [%f]%% line [%d]",
          //     m_groupRate,
          //     m_lexer.line + 1)
          L2_LOG_ERROR(
              Log,
              "Variation random group-rate total %.17g%% at script line %lld is not within 0.000001 of 100%%; invoking "
              "the fatal parser failure handler.",
              m_groupRate, static_cast<long long>(m_lexer.line) + 1
          );
          ForceCrash(CrashInfo::LEX_YACC_FAILED);
      }
    ;

n10
    : n10 ';' n11
    | n11
    ;

n11
    : '{' '{'
      {
          m_setRate = 0.0;
          m_pTempOptionGroup = new OptionGroup;
      }
      n13 '}' ';' FLOAT_NUMBER '}'
      {
          m_groupRate += grammar::SemanticDouble($7);
          if (std::abs(m_setRate - 100.0) > 1.0e-6) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Invalid SetRate Sum [%f]%% , line [%d]",
              //     m_setRate,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Invalid SetRate Sum [%f]%% , line [%d]",
              //     m_setRate,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Variation option-set rate total %.17g%% at script line %lld differs from 100%% by more than 0.000001; "
                  "invoking the fatal parser failure handler.",
                  m_setRate, static_cast<long long>(m_lexer.line) + 1
              );
              ForceCrash(CrashInfo::LEX_YACC_FAILED);
              break;
          }
          m_pTempVariation->InsertOptionGroup(m_pTempOptionGroup, m_groupRate, m_variType);
      }
    ;

n13
    : n13 ';' n14
    | n14
    ;

n14
    : '{' NAME ';' FLOAT_NUMBER '}'
      {
          if (!m_pTempOptionGroup->IsMaxOptionList()) {
              GuardHelper actionGuard(L"parser_variation_option_set", CThreadManager::GetThreadID());
              // Log.Add(
              //     LOG_ERROR,
              //     L"invalid max option list %d, [%d]",
              //     m_pTempOptionGroup->GetSizeOfOptionList(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"invalid max option list %d, [%d]",
              //     m_pTempOptionGroup->GetSizeOfOptionList(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Variation option group already contains %d options at script line %lld; maximum 500, invoking the "
                  "fatal parser failure handler.",
                  m_pTempOptionGroup->GetSizeOfOptionList(), static_cast<long long>(m_lexer.line) + 1
              );
              ForceCrash(CrashInfo::LEX_YACC_FAILED);
              break;
          }
          const char16_t* const optionName = SemanticWord($2);
          Option* const option = OptionDB::inst().FindByOptionName(std::u16string(optionName), true);
          if (option == nullptr) {
              GuardHelper actionGuard(L"parser_variation_option_set", CThreadManager::GetThreadID());
              // Log.Add(
              //     LOG_ERROR,
              //     L"invalid option name %s, [%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"invalid option name %s, [%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log, "Undefined variation option '%s' at script line %lld; invoking the fatal parser failure handler.",
                  Utf16ToUtf8(std::u16string_view(optionName != nullptr ? optionName : u"<null>")).c_str(),
                  static_cast<long long>(m_lexer.line) + 1
              );
              ForceCrash(CrashInfo::LEX_YACC_FAILED);
              break;
          }
          m_setRate += grammar::SemanticDouble($4);
          m_pTempOptionGroup->InsertOption(option->optionNo, m_setRate);
      }
    ;

variation_exception_weapon_def
    : VARIATION_EXCEPTION_WEAPON '=' '{' n16 '}'
    ;

n16
    : n16 ';' name_def
    | name_def
    ;

name_def
    : NAME
      {
          GuardHelper actionGuard(L"variation_exception_item_define", CThreadManager::GetThreadID());
          const int classId = g_ObjectDB.GetClassIdFromName(SemanticWord($1), false);
          if (classId == -1) {
              ForceCrash(CrashInfo::LEX_YACC_FAILED);
          }
          m_pVariationDB->RegisterExceptionItem(classId);
      }
    ;

item_group
    : ITEM_GROUP_BEGIN NAME INTEGER
      {
          m_tempItemGroupNo = static_cast<std::uint16_t>($3);
          m_tempItemGroupNameMap.emplace(std::u16string(SemanticWord($2)), m_tempItemGroupNo);
      }
      ITEM_LIST '=' '{' n20 '}' ITEM_GROUP_END
    ;

n20
    : n20 ';' name_def_2
    | name_def_2
    ;

name_def_2
    : NAME
      {
          GuardHelper actionGuard(L"item_class_id_define", CThreadManager::GetThreadID());
          const int classId = g_ObjectDB.GetClassIdFromName(SemanticWord($1), false);
          if (classId == -1) {
              ForceCrash(CrashInfo::LEX_YACC_FAILED);
          }
          m_pVariationDB->RegisterItemToGroup(static_cast<std::uint32_t>(classId), m_tempItemGroupNo);
      }
    ;

fee
    : FEE_BEGIN ITEM_GROUP_NAME '=' NAME MINERAL '=' NAME
      {
          const std::u16string groupName(SemanticWord($4));
          const auto found = m_tempItemGroupNameMap.find(groupName);
          if (found == m_tempItemGroupNameMap.end()) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Invalid ItemGroupName line [%d]",
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Invalid ItemGroupName line [%d]",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Undefined variation fee item group '%s' at script line %lld; invoking the fatal parser failure "
                  "handler.",
                  Utf16ToUtf8(std::u16string_view(groupName.c_str() != nullptr ? groupName.c_str() : u"<null>")).c_str(),
                  static_cast<long long>(m_lexer.line) + 1
              );
              ForceCrash(CrashInfo::LEX_YACC_FAILED);
              break;
          }
          m_feeItemGroupNo = found->second;
          const int intensiveClassId = g_ObjectDB.GetClassIdFromName(SemanticWord($7), false);
          if (intensiveClassId == -1) {
              GuardHelper actionGuard(L"fee_define", CThreadManager::GetThreadID());
              // Log.Add(
              //     LOG_ERROR,
              //     L"Invalid intensive class name in fee_define [%d]",
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Invalid intensive class name in fee_define [%d]",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Undefined intensive item class in variation fee definition for group '%s' at script line %lld; "
                  "invoking the fatal parser failure handler.",
                  Utf16ToUtf8(std::u16string_view(groupName.c_str() != nullptr ? groupName.c_str() : u"<null>")).c_str(),
                  static_cast<long long>(m_lexer.line) + 1
              );
              ForceCrash(CrashInfo::LEX_YACC_FAILED);
          }
          m_feeIntensiveClassID = static_cast<std::uint32_t>(intensiveClassId);
      }
      fee_item_name_def FEE_END
    ;

fee_item_name_def
    : FEE_ITEM_NAME '=' NAME FEE_ITEM_COUNT '=' '{' INTEGER '}' CANCEL_FEE '=' '{' INTEGER '}'
      {
          const int commissionItemClassId = g_ObjectDB.GetClassIdFromName(SemanticWord($3), false);
          if (commissionItemClassId == -1) {
              GuardHelper actionGuard(L"commission_define", CThreadManager::GetThreadID());
              // Log.Add(
              //     LOG_ERROR,
              //     L"Invalid commission class name [%d]",
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Invalid commission class name [%d]",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Undefined commission item class in variation fee definition at script line %lld; invoking the fatal "
                  "parser failure handler.",
                  static_cast<long long>(m_lexer.line) + 1
              );
              ForceCrash(CrashInfo::LEX_YACC_FAILED);
          }
          const CommissionKey key(static_cast<int>(m_feeIntensiveClassID), m_feeItemGroupNo);
          const CommissionInfo info(
              grammar::SemanticInt64($12), commissionItemClassId,
              static_cast<std::int64_t>(grammar::SemanticInt32($7))
          );
          m_pVariationDB->RegisterCommissionInfo(key, info);
      }
    ;

%%
