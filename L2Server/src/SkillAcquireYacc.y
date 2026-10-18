// skillacquire.txt grammar (original L2Server/skillacquire_yacc.y).
// Bison generates SkillAcquireYacc.cpp and SkillAcquireYaccTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/SkillAcquireYacc.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ObjectDB.h"
#include "L2Server/src/Skill.h"

#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

#include "SkillAcquireYaccTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForSkillAcquire::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"SkillAcquireYaccTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

CParserForSkillAcquire::CParserForSkillAcquire() :
    m_lexer(),
    m_vNeededItem(),
    m_vNeededItemCount(),
    m_mapAutoGetSkill(),
    m_vAcquireSkillsByClass(),
    m_vAcquireEtcSkills(),
    m_TempStringVector() {
    pAcquireInfo = nullptr;
    m_sSkillName = const_cast<char16_t*>(u"");
}

CParserForSkillAcquire::~CParserForSkillAcquire() = default;

int CParserForSkillAcquire::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}
}

%token FIGHTER_BEGIN_GROUP
%token FIGHTER_END_GROUP
%token FISHING_BEGIN_GROUP
%token FISHING_END_GROUP
%token HUMAN_BEGIN_GROUP
%token HUMAN_END_GROUP
%token NAME
%token INTEGER
%token TRUE_FALSE
%token SKILL_BEGIN
%token SKILL_END
%token SKILL_NAME
%token GET_LV
%token GET_DUAL_LV
%token GET_MAIN_DUAL_LV
%token ITEM_NEEDED
%token LV_UP_SP
%token NEED_QUEST
%token AUTO_GET
%token SOCIAL_CLASS
%token PLEDGE_TYPE
%token RACE
%token PREREQUISITE_SKILL
%token REPLACEABLE_SKILL
%token RESIDENCE_SKILL
%token QUEST_NEEDED
%token EXCLUDED_FROM_LIST
%token INCLUDE_VALUE
%start n1

%%

n1
    : fighter_begin_group_def_list
    ;

fighter_begin_group_def_list
    : fighter_begin_group_def_list fighter_begin_group_def
    | fighter_begin_group_def
    ;

fighter_begin_group_def
    : FIGHTER_BEGIN_GROUP
      {
          classType = static_cast<ClassType>(grammar::SemanticInt32($1));
          etcType = ESAT_MAX;
          raceType = RT_MAX;
      }
      skill_3_list FIGHTER_END_GROUP
    | FISHING_BEGIN_GROUP
      {
          etcType = static_cast<EtcSkillAcquireType>(grammar::SemanticInt32($1));
          classType = CT_MAX;
          raceType = RT_MAX;
      }
      skill_2_list FISHING_END_GROUP
    | HUMAN_BEGIN_GROUP
      {
          raceType = static_cast<RaceType>(grammar::SemanticInt32($1));
          etcType = ESAT_MAX;
          classType = CT_MAX;
      }
      skill_list HUMAN_END_GROUP
    ;

skill_list
    : skill_list skill
    | skill
    ;

skill
    : SKILL_BEGIN
      {
          pAcquireInfo = new SkillAcquireInfo;
          pAcquireInfo->nSkillID = -1;
          m_TempStringVector.clear();
      }
      skill_name_def_list SKILL_END
      {
          if (pAcquireInfo->nSkillID < 0) {
              // Log.Add(LOG_ERROR, L"invalid race skill")
              // L2_LOG_ERROR(Log, L"invalid race skill")
              L2_LOG_ERROR(
                  Log,
                  "skillacquire.txt line %lld: race %d has an acquisition rule with invalid skill ID %d; the rule is not "
                  "registered.",
                  static_cast<long long>(m_lexer.line) + 1, static_cast<int>(raceType), pAcquireInfo->nSkillID
              );
          } else {
              g_SkillAcquireDB.AddRaceSkill(pAcquireInfo);
          }
          pAcquireInfo = nullptr;
      }
    ;

skill_2_list
    : skill_2_list skill_2
    | skill_2
    ;

skill_2
    : SKILL_BEGIN
      {
          pAcquireInfo = new SkillAcquireInfo;
          pAcquireInfo->nSkillID = -1;
          m_TempStringVector.clear();
      }
      skill_name_def_list SKILL_END
      {
          if (etcType == ESAT_MENTOR || etcType == ESAT_MENTEE) {
              pAcquireInfo->m_bMentorMenteeSkill = true;
          }
          if (pAcquireInfo->nSkillID < 0) {
              // Log.Add(LOG_ERROR, L"invalid etc skill")
              // L2_LOG_ERROR(Log, L"invalid etc skill")
              L2_LOG_ERROR(
                  Log,
                  "skillacquire.txt line %lld: acquisition type %d has a rule with invalid skill ID %d; the rule is not "
                  "registered.",
                  static_cast<long long>(m_lexer.line) + 1, static_cast<int>(etcType), pAcquireInfo->nSkillID
              );
          } else {
              g_SkillAcquireDB.AddEtcSkill(pAcquireInfo);
          }
          m_vAcquireEtcSkills.push_back(pAcquireInfo->nSkillID);
          pAcquireInfo = nullptr;
      }
    ;

skill_3_list
    : skill_3_list skill_3
    | skill_3
    ;

skill_3
    : SKILL_BEGIN
      {
          pAcquireInfo = new SkillAcquireInfo;
          pAcquireInfo->nSkillID = -1;
          m_TempStringVector.clear();
      }
      skill_name_def_list SKILL_END
      {
          if (pAcquireInfo->nSkillID < 0) {
              // Log.Add(LOG_ERROR, L"invalid skill")
              // L2_LOG_ERROR(Log, L"invalid skill")
              L2_LOG_ERROR(
                  Log,
                  "skillacquire.txt line %lld: class %d has an acquisition rule with invalid skill ID %d; the rule is not "
                  "registered.",
                  static_cast<long long>(m_lexer.line) + 1, static_cast<int>(classType), pAcquireInfo->nSkillID
              );
          } else {
              g_SkillAcquireDB.Add(pAcquireInfo);
          }
          m_vAcquireSkillsByClass[static_cast<std::size_t>(classType)].emplace(pAcquireInfo->nSkillID, 1);
          pAcquireInfo = nullptr;
      }
    | INCLUDE_VALUE
      {
          const ClassType sourceClass = static_cast<ClassType>(grammar::SemanticInt32($1));
          const CSkillAcquireDB::SkillAcquireInfoVector sourceSkills = g_SkillAcquireDB.GetAcquirableSkills(sourceClass);
          for (SkillAcquireInfo* const info : sourceSkills) {
              pAcquireInfo = info;
              info->cls = classType;
              info->etcType = etcType;
              info->raceType = raceType;
              g_SkillAcquireDB.Add(info);
          }
          const AcquireSkillMap& source = m_vAcquireSkillsByClass[static_cast<std::size_t>(sourceClass)];
          AcquireSkillMap& destination = m_vAcquireSkillsByClass[static_cast<std::size_t>(classType)];
          for (const auto& entry : source) {
              destination.emplace(entry);
          }
      }
    ;

skill_name_def_list
    : skill_name_def_list skill_name_def
    | %empty
    ;

skill_name_def
    : SKILL_NAME '=' NAME
      {
          char16_t* const skillName = grammar::SemanticString($3);
          const std::wstring hostSkillName = target_utf16::ToHostCodeUnits(skillName);
          int skillId = 0;
          int skillLevel = 0;
          int subLevel = 0;
          if (!g_SkillDB.GetSkillIdLevelByName(hostSkillName.c_str(), &skillId, &skillLevel, &subLevel)) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d] : Undefined skill(%s)",
              //     L"..\\Script\\skillacquire.txt",
              //     m_lexer.line + 1,
              //     hostSkillName.c_str())
              // L2_LOG_ERROR(Log,
              //     L"%s[%d] : Undefined skill(%s)",
              //     L"..\\Script\\skillacquire.txt",
              //     m_lexer.line + 1,
              //     hostSkillName.c_str())
              L2_LOG_ERROR(
                  Log,
                  "skillacquire.txt line %lld: cannot resolve skill '%s'; the rule's skill ID and level are left "
                  "unchanged.",
                  static_cast<long long>(m_lexer.line) + 1, Utf16ToUtf8(skillName).c_str()
              );
          } else {
              pAcquireInfo->nSkillID = skillId;
              pAcquireInfo->nSkillLevel = skillLevel;
              pAcquireInfo->cls = classType;
              pAcquireInfo->etcType = etcType;
              pAcquireInfo->raceType = raceType;
          }
          m_sSkillName = skillName;
      }
    | GET_LV '=' INTEGER
      { pAcquireInfo->nGetLevel = grammar::SemanticInt32($3); }
    | GET_DUAL_LV '=' INTEGER
      {
          pAcquireInfo->m_GetMainDualClassLevel = grammar::SemanticInt32($3);
      }
    | GET_MAIN_DUAL_LV '=' INTEGER
      {
          pAcquireInfo->m_GetMainDualClassLevel = grammar::SemanticInt32($3);
      }
    | ITEM_NEEDED '='
      {
          pAcquireInfo->vNeededItem.clear();
          pAcquireInfo->vNeededItemCount.clear();
      }
      n23
    | LV_UP_SP '=' INTEGER
      { pAcquireInfo->nLevelUP_SP = static_cast<std::int64_t>($3); }
    | NEED_QUEST '=' TRUE_FALSE
      { pAcquireInfo->bNeedQuest = static_cast<std::uint8_t>($3) != 0; }
    | QUEST_NEEDED '='
      { pAcquireInfo->vNeededQuest.clear(); }
      n26
    | AUTO_GET '=' TRUE_FALSE
      {
          pAcquireInfo->bAutoGet = static_cast<std::uint8_t>($3) != 0;
          const auto found = m_mapAutoGetSkill.find(m_sSkillName);
          if (!pAcquireInfo->bAutoGet) {
              if (found != m_mapAutoGetSkill.end()) {
                  // const std::wstring hostSkillName =
                  //     target_utf16::ToHostCodeUnits(m_sSkillName)
                  const std::string hostSkillName = Utf16ToUtf8(m_sSkillName);
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"AutoGetSkill can be acquired by NPC. Skill[%s] Line[%d]",
                  //     hostSkillName.c_str(),
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"AutoGetSkill can be acquired by NPC. Skill[%s] Line[%d]",
                  //     hostSkillName.c_str(),
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      "skillacquire.txt line %lld: skill '%s' uses manual acquisition after an earlier automatic "
                      "acquisition entry; the manual setting is retained.",
                      static_cast<long long>(m_lexer.line) + 1, hostSkillName.c_str()
                  );
              }
              break;
          }
          if (found == m_mapAutoGetSkill.end()) {
              m_mapAutoGetSkill.emplace(std::u16string(m_sSkillName), true);
          }
      }
    | SOCIAL_CLASS '=' INTEGER
      { pAcquireInfo->m_SocialClass = grammar::SemanticInt32($3); }
    | PLEDGE_TYPE '=' NAME
      {
          char16_t* const pledgeName = grammar::SemanticString($3);
          const std::int64_t searched = g_PrecompiledHeaderDB.Search(pledgeName);
          const int pledgeType = static_cast<int>(searched);
          // const std::wstring hostPledgeName =
          //     target_utf16::ToHostCodeUnits(pledgeName)
          const std::string hostPledgeName = Utf16ToUtf8(pledgeName);
          if (pledgeType == -1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined word[%s] in line [%d]",
              //     hostPledgeName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Undefined word[%s] in line [%d]",
              //     hostPledgeName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "skillacquire.txt line %lld: pledge_type '%s' resolves to -1; expected MASTER_PLEDGE, SUB_2 or SUB_2_2. "
                  "Type -1 is retained.",
                  static_cast<long long>(m_lexer.line) + 1, hostPledgeName.c_str()
              );
          }
          if (pledgeType != MASTER_PLEDGE && pledgeType != SUB_2 && pledgeType != SUB_2_2) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"pledge_type should be MASTER_PLEDGE, SUB_2, or SUB_2_2 in line [%d]",
              //     hostPledgeName.c_str(),
              //     m_lexer.line + 1)
              // Noise: The -1 result is already explained above. Report other rejected values here.
              // L2_LOG_ERROR(Log,
              //     L"pledge_type should be MASTER_PLEDGE, SUB_2, or SUB_2_2 in line [%d]",
              //     hostPledgeName.c_str(),
              //     m_lexer.line + 1)
              if (pledgeType != -1) {
                  L2_LOG_ERROR(
                      Log,
                      "skillacquire.txt line %lld: pledge_type '%s' resolves to %d; expected MASTER_PLEDGE, SUB_2 or "
                      "SUB_2_2. This value is retained.",
                      static_cast<long long>(m_lexer.line) + 1, hostPledgeName.c_str(), pledgeType
                  );
              }
          }
          pAcquireInfo->m_MaximumPledgeType = static_cast<PledgeType>(pledgeType);
      }
    | RACE
      { m_TempStringVector.clear(); }
      '=' n29
      {
          for (char16_t* const raceName : m_TempStringVector) {
              const std::int64_t searched = g_PrecompiledHeaderDB.Search(raceName);
              const int raceValue = static_cast<int>(searched);
              // const std::wstring hostRaceName =
              //     target_utf16::ToHostCodeUnits(raceName)
              const std::string hostRaceName = Utf16ToUtf8(raceName);
              if (raceValue == -1) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined word[%s] in line [%d]",
                  //     hostRaceName.c_str(),
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"Undefined word[%s] in line [%d]",
                  //     hostRaceName.c_str(),
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log, "skillacquire.txt line %lld: race condition '%s' resolves to -1; this condition is omitted.",
                      static_cast<long long>(m_lexer.line) + 1, hostRaceName.c_str()
                  );
              }
              if (static_cast<unsigned int>(raceValue) < 7U) {
                  pAcquireInfo->m_RaceConditions.insert(static_cast<RaceType>(raceValue));
              } else {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"raceType [%s] in line [%d] isn't for User",
                  //     hostRaceName.c_str(),
                  //     m_lexer.line + 1)
                  // Noise: The -1 result is already explained above. Report other rejected values here.
                  // L2_LOG_ERROR(Log,
                  //     L"raceType [%s] in line [%d] isn't for User",
                  //     hostRaceName.c_str(),
                  //     m_lexer.line + 1)
                  if (raceValue != -1) {
                      L2_LOG_ERROR(
                          Log,
                          "skillacquire.txt line %lld: race condition '%s' resolves to %d, outside the playable race "
                          "range 0..6; this condition is omitted.",
                          static_cast<long long>(m_lexer.line) + 1, hostRaceName.c_str(), raceValue
                      );
                  }
              }
          }
      }
    | PREREQUISITE_SKILL
      { m_TempStringVector.clear(); }
      '=' n29
      {
          for (char16_t* const skillName : m_TempStringVector) {
              const std::wstring hostSkillName = target_utf16::ToHostCodeUnits(skillName);
              pAcquireInfo->m_PrerequisiteSkills.push_back(g_SkillDB.GetSkillInfoByName(hostSkillName.c_str()));
          }
      }
    | REPLACEABLE_SKILL
      { m_TempStringVector.clear(); }
      '=' n29
      {
          for (char16_t* const skillName : m_TempStringVector) {
              const std::wstring hostSkillName = target_utf16::ToHostCodeUnits(skillName);
              pAcquireInfo->m_ReplaceableSkills.push_back(g_SkillDB.GetSkillInfoByName(hostSkillName.c_str()));
          }
      }
    | RESIDENCE_SKILL '=' TRUE_FALSE
      { pAcquireInfo->m_bResidenceSkill = static_cast<std::uint8_t>($3) != 0; }
    | EXCLUDED_FROM_LIST '=' TRUE_FALSE
      {
          pAcquireInfo->m_bExcludedFromListOfSkillsToLearn = static_cast<std::uint8_t>($3) != 0;
      }
    ;

n23
    : '{' n24 '}'
    | '{' '}'
    ;

n24
    : n24 ';' n25
    | n25
    ;

n25
    : '{' NAME ';' INTEGER '}'
      {
          char16_t* const itemName = grammar::SemanticString($2);
          const int itemId = g_ObjectDB.GetClassIdFromName(itemName, false);
          if (itemId == -1) {
              // const std::wstring hostItemName =
              //     target_utf16::ToHostCodeUnits(itemName)
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
                  Log, "skillacquire.txt line %lld: required item '%s' is unknown; item ID -1 is retained in the rule.",
                  static_cast<long long>(m_lexer.line) + 1, hostItemName.c_str()
              );
          }
          pAcquireInfo->vNeededItem.push_back(itemId);
          pAcquireInfo->vNeededItemCount.push_back(grammar::SemanticInt32($4));
      }
    ;

n26
    : '{' n27 '}'
    | '{' '}'
    ;

n27
    : n27 ';' integer_def
    | integer_def
    ;

integer_def
    : INTEGER
      { pAcquireInfo->vNeededQuest.push_back(grammar::SemanticInt32($1)); }
    ;

n29
    : '{' name_def '}'
    | '{' '}'
    ;

name_def
    : NAME
      {
          m_TempStringVector.push_back(grammar::SemanticString($1));
      }
    | name_def ';' NAME
      {
          m_TempStringVector.push_back(grammar::SemanticString($3));
      }
    ;

// The original tables have rule 60 that no parser state reduces.
// It is left out.

%%
