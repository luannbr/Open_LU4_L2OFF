// skillsynergydata.txt grammar.
// Bison generates ParserSkillSynergy.cpp and ParserSkillSynergyTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserSkillSynergy.h"
#include "Shared/src/TextRuntime.h"

#include "Shared/src/Log.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "ParserSkillSynergyTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForSkillSynergy::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserSkillSynergyTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

TriggerSynergySkillInfo::TriggerSynergySkillInfo() :
    m_RequiredSlotType(), m_SynergyOptionSlotType(), m_mapSynergySkillByCount() {}

CParserForSkillSynergy::CParserForSkillSynergy() :
    m_lexer(), info(nullptr), synergyId(0), m_setAbnormalSlotType(), m_tempSkillMap() {
    m_mapSynergySkill.clear();
    m_setAbnormalSlotType.clear();
}

CParserForSkillSynergy::~CParserForSkillSynergy() = default;

int CParserForSkillSynergy::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}
}

%token NAME
%token IDENTIFIER
%token INTEGER
%token SKILL_SYNERGY_BEGIN
%token SKILL_SYNERGY_END
%token SYNERGY_ID
%token REQUIRED_SLOT
%token SYNERGY_SLOT
%token SYNERGY_SKILLS
%start skill_synergy_list

%%

skill_synergy_list
    : skill_synergy_list skill_synergy
    | skill_synergy
    ;

skill_synergy
    : SKILL_SYNERGY_BEGIN
      {
          info = new TriggerSynergySkillInfo;
          synergyId = 0;
      }
      synergy_id_def_list SKILL_SYNERGY_END
      {
          if (synergyId > 0) {
              m_mapSynergySkill.insert(std::make_pair(synergyId, info));
          }
          info = nullptr;
      }
    ;

synergy_id_def_list
    : synergy_id_def_list synergy_id_def
    | synergy_id_def
    ;

synergy_id_def
    : SYNERGY_ID '=' INTEGER
      { synergyId = grammar::SemanticInt32($3); }
    | REQUIRED_SLOT '=' '{' n6 '}'
      {
          info->m_RequiredSlotType = m_setAbnormalSlotType;
          m_setAbnormalSlotType.clear();
      }
    | REQUIRED_SLOT '=' '{' '}'
      { m_setAbnormalSlotType.clear(); }
    | SYNERGY_SLOT '=' '{' n6 '}'
      {
          info->m_SynergyOptionSlotType = m_setAbnormalSlotType;
          m_setAbnormalSlotType.clear();
      }
    | SYNERGY_SLOT '=' '{' '}'
      { m_setAbnormalSlotType.clear(); }
    | SYNERGY_SKILLS '=' '{' n8 '}'
    ;

n6
    : n6 ';' identifier_def
    | identifier_def
    ;

identifier_def
    : IDENTIFIER
      {
          m_setAbnormalSlotType.insert(GetAbnormalStatusSlotTypeByName(grammar::SemanticString($1)));
      }
    ;

n8
    : n8 ';' n9
    | n9
    ;

n9
    : '{' INTEGER ';' NAME '}'
      {
          const std::wstring skillName = target_utf16::ToHostCodeUnits(grammar::SemanticString($4));
          const CSkillInfo* const skillInfo = g_SkillDB.GetSkillInfoByName(skillName.c_str());
          if (skillInfo != nullptr) {
              info->m_mapSynergySkillByCount.insert(std::make_pair(grammar::SemanticInt32($2), skillInfo));
              break;
          }
          // Log.Add(
          //     LOG_ERROR,
          //     L"SkillSynergyData.txt[%d] : Undefined skill(name : %s)",
          //     m_lexer.line + 1,
          //     grammar::SemanticString($4))
          // L2_LOG_ERROR(Log,
          //     L"SkillSynergyData.txt[%d] : Undefined skill(name : %s)",
          //     m_lexer.line + 1,
          //     grammar::SemanticString($4))
          L2_LOG_ERROR(
              Log,
              "skillsynergydata.txt line %lld, synergy %d: skill '%s' cannot be resolved for count %d; this "
              "count-to-skill entry is omitted.",
              static_cast<long long>(m_lexer.line) + 1, synergyId, Utf16ToUtf8(grammar::SemanticString($4)).c_str(),
              grammar::SemanticInt32($2)
          );
      }
    ;

%%
