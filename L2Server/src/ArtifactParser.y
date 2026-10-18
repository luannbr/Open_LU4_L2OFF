// artifact.txt grammar.
// Bison generates ArtifactParser.cpp and ArtifactParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ArtifactParser.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/Artifact.h"
#include "L2Server/src/Skill.h"

#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <string_view>

#include "ArtifactParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForArtifact::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ArtifactParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

CParserForArtifact::CParserForArtifact() : m_lexer(), m_tmpIntVector(), m_SkillList() {}

CParserForArtifact::~CParserForArtifact() = default;

int CParserForArtifact::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}

void CParserForArtifact::yysyntaxerror() {
    char16_t message[60]{};
    FormatUtf16To(message, std::size(message), u"ArtifactDataChecker[%d] : syntax error", m_lexer.line + 1);
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
%token NAME
%token ARTIFACT_ENCHANT_SETTING_BEGIN
%token ARTIFACT_ENCHANT_SETTING_END
%token ENCHANT
%token MIN_ENCHANT_MATERIAL
%token ARTIFACT_TYPE_SETTING_BEGIN
%token ARTIFACT_TYPE_SETTING_END
%token ARTIFACT_GROUP_ID
%token MATERIAL_ITEMS
%token ARTIFACT_UPGRADE_BEGIN
%token ARTIFACT_UPGRADE_END
%token ARTIFACT_ITEM_ID
%token COUNT
%token RESULT_PROB
%token ARTIFACT_SET_BEGIN
%token ARTIFACT_SET_END
%token ARTIFACT_SET_EFFECT_VALUE
%start n1

%%

n1
    : n1 n2
    | n2
    ;

n2
    : artifact_enchant_setting
    | artifact_type_setting
    | artifact_upgrade
    | artifact_set
    ;

artifact_enchant_setting
    : ARTIFACT_ENCHANT_SETTING_BEGIN
      {
          m_nEnchant = 0;
          m_nMinEnchantMaterial = 0;
      }
      enchant_def_list ARTIFACT_ENCHANT_SETTING_END
      {
          Singleton<CArtifactDB>::inst().AddMaterialMinEnchantLevel(m_nEnchant, m_nMinEnchantMaterial);
      }
    ;

enchant_def_list
    : enchant_def_list enchant_def
    | enchant_def
    ;

enchant_def
    : ENCHANT '=' INTEGER
      { m_nEnchant = grammar::SemanticInt32($3); }
    | MIN_ENCHANT_MATERIAL '=' INTEGER
      { m_nMinEnchantMaterial = grammar::SemanticInt32($3); }
    ;

artifact_type_setting
    : ARTIFACT_TYPE_SETTING_BEGIN
      { m_tmpIntVector.clear(); }
      artifact_group_id_def_list ARTIFACT_TYPE_SETTING_END
      {
          Singleton<CArtifactDB>::inst().AddMaterialItemInfo(m_nGroupId, m_tmpIntVector);
      }
    ;

artifact_group_id_def_list
    : artifact_group_id_def_list artifact_group_id_def
    | artifact_group_id_def
    ;

artifact_group_id_def
    : ARTIFACT_GROUP_ID '=' INTEGER
      { m_nGroupId = grammar::SemanticInt32($3); }
    | MATERIAL_ITEMS '=' '{' n11 '}'
    ;

n11
    : n11 ';' INTEGER
      { m_tmpIntVector.push_back(grammar::SemanticInt32($3)); }
    | INTEGER
      { m_tmpIntVector.push_back(grammar::SemanticInt32($1)); }
    ;

artifact_upgrade
    : ARTIFACT_UPGRADE_BEGIN
      {
          m_nEnchant = 0;
          m_nMinEnchantMaterial = 0;
          m_nGroupId = 0;
          m_nItemId = 0;
          m_nCount = 0;
          m_nResultProb = 0;
      }
      artifact_item_id_def_list ARTIFACT_UPGRADE_END
      {
          const ArtifactEnchantInfo info{m_nGroupId, m_nCount, m_nResultProb};
          Singleton<CArtifactDB>::inst().AddEnchantInfo(m_nItemId, m_nEnchant, info);
      }
    ;

artifact_item_id_def_list
    : artifact_item_id_def_list artifact_item_id_def
    | artifact_item_id_def
    ;

artifact_item_id_def
    : ARTIFACT_ITEM_ID '=' '{' INTEGER ';' INTEGER '}'
      {
          m_nItemId = grammar::SemanticInt32($4);
          m_nEnchant = grammar::SemanticInt32($6);
      }
    | ARTIFACT_GROUP_ID '=' INTEGER
      { m_nGroupId = grammar::SemanticInt32($3); }
    | COUNT '=' INTEGER
      {
          m_nCount = grammar::SemanticInt32($3);
          if (m_nCount > 3) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[ArtifactParser] The number of material count [%d]",
              //     m_nCount)
              L2_LOG_ERROR(Log, u"[ArtifactParser] The number of material count [%d]", m_nCount);
          }
      }
    | RESULT_PROB '=' INTEGER
      {
          m_nResultProb = grammar::SemanticInt32($3);
          if (m_nResultProb < 0 || m_nResultProb > 100) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[ArtifactParser] result_prob must be between 0 and 100. "
              //     L"result_prob[%d], line[%d]",
              //     m_nResultProb,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"[ArtifactParser] result_prob must be between 0 and 100. "
                  u"result_prob[%d], line[%d]",
                  m_nResultProb, m_lexer.line + 1
              );
          }
      }
    ;

artifact_set
    : ARTIFACT_SET_BEGIN
      { m_SkillList.clear(); }
      artifact_set_effect_value_def_list ARTIFACT_SET_END
    ;

artifact_set_effect_value_def_list
    : artifact_set_effect_value_def_list artifact_set_effect_value_def
    | artifact_set_effect_value_def
    ;

artifact_set_effect_value_def
    : ARTIFACT_SET_EFFECT_VALUE '=' '{' n20 '}'
      {
          const int page = grammar::SemanticInt32($1);
          for (const std::int64_t packedSkill : m_SkillList) {
              const std::uint64_t bits = static_cast<std::uint64_t>(packedSkill);
              const CSkillKey key(
                  static_cast<int>(bits >> 32), static_cast<int>(static_cast<std::uint16_t>(bits >> 16)),
                  static_cast<int>(static_cast<std::uint16_t>(bits))
              );
              const CSkillInfo* const skillInfo = g_SkillDB.GetSkillInfo(key, nullptr);
              Singleton<CArtifactDB>::inst().AddArtifactbookPageSkill(page, skillInfo);
          }
          m_SkillList.clear();
      }
    ;

n20
    : n20 ';' name_def
    | name_def
    ;

name_def
    : NAME
      {
          const char16_t* const skillName = grammar::SemanticString($1);
          const std::wstring hostSkillName = target_utf16::ToHostCodeUnits(skillName);
          int skillId = 0;
          int skillLevel = 0;
          int skillSubLevel = 0;
          if (g_SkillDB.GetSkillIdLevelByName(hostSkillName.c_str(), &skillId, &skillLevel, &skillSubLevel)) {
              const std::uint64_t packedSkill = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(skillId)) << 32) |
                  (static_cast<std::uint64_t>(static_cast<std::uint16_t>(skillLevel)) << 16) |
                  static_cast<std::uint16_t>(skillSubLevel);
              m_SkillList.push_back(static_cast<std::int64_t>(packedSkill));
          } else {
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d] : Undefined skill(%s)",
              //     L"..\\Script\\artifact.txt",
              //     m_lexer.line + 1,
              //     hostSkillName.c_str())
              L2_LOG_ERROR(
                  Log, u"%s[%d] : Undefined skill(%s)", u"..\\Script\\artifact.txt", m_lexer.line + 1,
                  hostSkillName.c_str()
              );
          }
      }
    ;

%%
