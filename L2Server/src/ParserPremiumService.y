// CParserForPremiumService grammar (original L2Server/parser_premiumservice.y).
// Bison generates ParserPremiumService.cpp and ParserPremiumServiceTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserPremiumService.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/Item.h"
#include "L2Server/src/Object.h"
#include "L2Server/src/ObjectDB.h"
#include "L2Server/src/RestartOriginal.h"
#include "L2Server/src/Skill.h"
#include "L2Server/src/UserBookmark.h"
#include "L2Server/src/UserNickColor.h"

#include "Shared/src/Log.h"
#include "Shared/src/SharedFactory.h"

#include <cstdint>
#include <string>
#include <string_view>

#include "ParserPremiumServiceTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForPremiumService::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserPremiumServiceTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

CItem* ResolveItem(const char16_t* name) {
    CObject* const object = g_ObjectDB.GetObjectByName(name);
    if (object == nullptr || !object->IsItem()) {
        return nullptr;
    }
    return object->CastItem();
}

} // namespace

CParserForPremiumService::CParserForPremiumService() : m_lexer() {}

CParserForPremiumService::~CParserForPremiumService() = default;

int CParserForPremiumService::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}
}

%token BOOKMARK_BEGIN
%token BOOKMARK_END
%token TELEPORT_ITEM
%token TELEPORT_SKILL
%token MAX_SLOT
%token NICKCOLOR_BEGIN
%token NICKCOLOR_END
%token CHANGE_ITEM
%token RESTARTORIGIN_BEGIN
%token RESTART_ITEM
%token RESTARTORIGIN_END
%token ADD_BOOKMARK_ITEM
%token RESTART_SKILL
%token RESTART_NPC_ITEM
%token NAME
%token INTEGER
%token FLOAT_NUMBER
%start n1

%%

n1
    : n1 n2
    | n2
    ;

n2
    : bookmark
    | nickcolor
    | restartorigin
    ;

bookmark
    : BOOKMARK_BEGIN TELEPORT_ITEM '=' '{' n5 '}' TELEPORT_SKILL '=' NAME MAX_SLOT '=' INTEGER ADD_BOOKMARK_ITEM '=' '{' n7
      '}'
      {
          const char16_t* const skillName = grammar::SemanticString($9);
          const std::wstring hostSkillName = target_utf16::ToHostCodeUnits(skillName);
          const CSkillInfo* const skillInfo = g_SkillDB.GetSkillInfoByName(hostSkillName.c_str());
          if (skillInfo != nullptr) {
              g_UserBookmarkSetting.SetTeleportSkill(skillInfo);
              g_UserBookmarkSetting.SetMaxSlotCount(static_cast<std::int32_t>(static_cast<std::uint32_t>($12)));
              break;
          }
          // Log.Add(
          //     LOG_ERROR,
          //     L"Undefined Skill[%s] in line[%d]",
          //     hostSkillName.c_str(),
          //     m_lexer.line + 1)
          // L2_LOG_ERROR(Log,
          //     L"Undefined Skill[%s] in line[%d]",
          //     hostSkillName.c_str(),
          //     m_lexer.line + 1)
          L2_LOG_ERROR(
              Log, "Cannot configure %s at script line %lld: skill '%s' is undefined; the setting is skipped.",
              "bookmark teleport skill and slot count",
              static_cast<long long>(m_lexer.line) + 1,
              Utf16ToUtf8(std::u16string_view(skillName != nullptr ? skillName : u"<null>")).c_str()
          );
      }
      BOOKMARK_END
    ;

n5
    : n5 ';' name_def
    | name_def
    ;

name_def
    : NAME
      {
          const char16_t* const itemName = grammar::SemanticString($1);
          CItem* const item = ResolveItem(itemName);
          if (item == nullptr) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined Item[%s] in line[%d]",
              //     hostItemName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Undefined Item[%s] in line[%d]",
              //     hostItemName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Cannot configure premium-service item '%s' at script line %lld (action %d): the object is missing or is "
                  "not an "
                  "item; the setting is skipped.",
                  Utf16ToUtf8(std::u16string_view(itemName != nullptr ? itemName : u"<null>")).c_str(),
                  static_cast<long long>(m_lexer.line) + 1, 1
              );
              break;
          }
          g_UserBookmarkSetting.SetTeleportItem(item);
      }
    ;

n7
    : n7 ';' name_def_2
    | name_def_2
    ;

name_def_2
    : NAME
      {
          const char16_t* const itemName = grammar::SemanticString($1);
          CItem* const item = ResolveItem(itemName);
          if (item == nullptr) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined Item[%s] in line[%d]",
              //     hostItemName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Undefined Item[%s] in line[%d]",
              //     hostItemName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Cannot configure premium-service item '%s' at script line %lld (action %d): the object is missing or is "
                  "not an "
                  "item; the setting is skipped.",
                  Utf16ToUtf8(std::u16string_view(itemName != nullptr ? itemName : u"<null>")).c_str(),
                  static_cast<long long>(m_lexer.line) + 1, 2
              );
              break;
          }
          g_UserBookmarkSetting.SetAddItem(item);
      }
    ;

nickcolor
    : NICKCOLOR_BEGIN CHANGE_ITEM '=' '{' n10 '}' NICKCOLOR_END
    ;

n10
    : n10 ';' name_def_3
    | name_def_3
    ;

name_def_3
    : NAME
      {
          const char16_t* const itemName = grammar::SemanticString($1);
          CItem* const item = ResolveItem(itemName);
          if (item == nullptr) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined Item[%s] in line[%d]",
              //     hostItemName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Undefined Item[%s] in line[%d]",
              //     hostItemName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Cannot configure premium-service item '%s' at script line %lld (action %d): the object is missing or is "
                  "not an "
                  "item; the setting is skipped.",
                  Utf16ToUtf8(std::u16string_view(itemName != nullptr ? itemName : u"<null>")).c_str(),
                  static_cast<long long>(m_lexer.line) + 1, 3
              );
              break;
          }
          g_NickColorManager.AddNickColorItem(item->m_data->m_nClassID);
      }
    ;

restartorigin
    : RESTARTORIGIN_BEGIN RESTART_ITEM '=' '{' n13 '}'
    | RESTART_SKILL '=' '{' n15 '}'
    | RESTART_NPC_ITEM '=' '{' n17 '}' RESTARTORIGIN_END
    ;

n13
    : n13 ';' name_def_4
    | name_def_4
    ;

name_def_4
    : NAME
      {
          const char16_t* const itemName = grammar::SemanticString($1);
          CItem* const item = ResolveItem(itemName);
          if (item == nullptr) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined Item[%s] in line[%d]",
              //     hostItemName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Undefined Item[%s] in line[%d]",
              //     hostItemName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Cannot configure premium-service item '%s' at script line %lld (action %d): the object is missing or is "
                  "not an "
                  "item; the setting is skipped.",
                  Utf16ToUtf8(std::u16string_view(itemName != nullptr ? itemName : u"<null>")).c_str(),
                  static_cast<long long>(m_lexer.line) + 1, 4
              );
              break;
          }
          g_RestartOriginalManager.AddRestartItem(item->m_data->m_nClassID);
      }
    ;

n15
    : n15 ';' name_def_5
    | name_def_5
    ;

name_def_5
    : NAME
      {
          const char16_t* const skillName = grammar::SemanticString($1);
          const std::wstring hostSkillName = target_utf16::ToHostCodeUnits(skillName);
          const CSkillInfo* const skillInfo = g_SkillDB.GetSkillInfoByName(hostSkillName.c_str());
          if (skillInfo != nullptr) {
              g_RestartOriginalManager.AddRestartSkill(const_cast<CSkillInfo*>(skillInfo));
              break;
          }
          // Log.Add(
          //     LOG_ERROR,
          //     L"Undefined Skill[%s] in line[%d]",
          //     hostSkillName.c_str(),
          //     m_lexer.line + 1)
          // L2_LOG_ERROR(Log,
          //     L"Undefined Skill[%s] in line[%d]",
          //     hostSkillName.c_str(),
          //     m_lexer.line + 1)
          L2_LOG_ERROR(
              Log, "Cannot configure %s at script line %lld: skill '%s' is undefined; the setting is skipped.",
              "restart skill",
              static_cast<long long>(m_lexer.line) + 1,
              Utf16ToUtf8(std::u16string_view(skillName != nullptr ? skillName : u"<null>")).c_str()
          );
      }
    ;

n17
    : n17 ';' name_def_6
    | name_def_6
    ;

name_def_6
    : NAME
      {
          const char16_t* const itemName = grammar::SemanticString($1);
          CItem* const item = ResolveItem(itemName);
          if (item == nullptr) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined Item[%s] in line[%d]",
              //     hostItemName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Undefined Item[%s] in line[%d]",
              //     hostItemName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Cannot configure premium-service item '%s' at script line %lld (action %d): the object is missing or is "
                  "not an "
                  "item; the setting is skipped.",
                  Utf16ToUtf8(std::u16string_view(itemName != nullptr ? itemName : u"<null>")).c_str(),
                  static_cast<long long>(m_lexer.line) + 1, 6
              );
              break;
          }
          g_RestartOriginalManager.AddRestartNpcItem(item->m_data->m_nClassID);
      }
    ;

%%
