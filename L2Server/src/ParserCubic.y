// CParserForCubic grammar (original L2Server/parser_cubic.y).
// Bison generates ParserCubic.cpp and ParserCubicTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserCubic.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ObjectDB.h"
#include "L2Server/src/Skill.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <string_view>

#include "ParserCubicTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForCubic::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserCubicTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

grammar::SemanticBits DoubleSemantic(double value) {
    grammar::SemanticBits result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

CubicSkillInfo* NewCubicSkillInfo() { return new CubicSkillInfo{}; }

} // namespace

CParserForCubic::CParserForCubic() : m_lexer(), m_TimeSkillInfo{} {}

CParserForCubic::~CParserForCubic() = default;

int CParserForCubic::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}

void CParserForCubic::yysyntaxerror() {
    char16_t message[60]{};
    FormatUtf16To(message, std::size(message), u"CubicDataChecker[%d] : syntax error", m_lexer.line + 1);
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

%token CUBIC_BEGIN
%token CUBIC_END
%token ID
%token LEVEL
%token SLOT
%token DURATION
%token DELAY
%token MAX_COUNT
%token POWER
%token TARGET_TYPE
%token HEAL
%token TARGET
%token BY_SKILL
%token MASTER
%token OP_COND
%token DEBUFF
%token SKILL1
%token SKILL2
%token SKILL3
%token TIMESKILL1
%token TIMESKILL2
%token TIMESKILL3
%token AGATHION_BEGIN
%token AGATHION_END
%token NPC_NAME
%token USE_UP
%token PERIOD
%token DAILY
%token TIMESKILL4
%token TIMESKILL5
%token ITEM_IDS
%token ENERGY
%token MAX_ENERGY
%token NAME
%token INTEGER
%token FLOAT_NUMBER
%start n1

%%

n1
    : cubic_list agathion_list
    | cubic_list
    | agathion_list
    ;

cubic_list
    : cubic_list cubic
    | cubic
    ;

cubic
    : CUBIC_BEGIN
      {
          m_bIsCubic = true;
          m_pCubic = nullptr;
          m_prevProb = 0;
          m_bUseUp = false;
      }
      id_def n12 n16 CUBIC_END
      {
          if (m_pCubic != nullptr) {
              SpriteDB::Inst().Add(SPRITE_CUBIC, m_pCubic, m_id, m_level);
          }
      }
    ;

id_def
    : ID '=' INTEGER LEVEL '=' INTEGER SLOT '=' INTEGER
      {
          m_id = grammar::SemanticInt32($3);
          m_level = grammar::SemanticInt32($6);
          m_slot = grammar::SemanticInt32($9) - 1;
          if ((m_bIsCubic && m_slot >= 0 && m_slot < 20) || (!m_bIsCubic && m_slot >= 0 && m_slot < 5)) {
              break;
          }
          // Log.Add(
          //     LOG_ERROR,
          //     L"Invalid Slot while loading CubicData. (id[%d] level[%d]). Line = %d",
          //     m_id,
          //     m_level,
          //     m_lexer.line + 1)
          // Previous: L2_LOG_ERROR(Log,
          //     L"Invalid Slot while loading CubicData. (id[%d] level[%d]). Line = %d",
          //     m_id, m_level, m_lexer.line + 1)
          L2_LOG_ERROR(
              Log,
              u"[cubic parser] Slot is outside the allowed range for cubic %d "
              u"level %d at line %d; this record is not constructed.",
              m_id, m_level, m_lexer.line + 1
          );
          // Log.Add(LOG_ERROR, L"--- CubicData checking FAILED ---")
          // Previous: L2_LOG_ERROR(Log, L"--- CubicData checking FAILED ---")
          // Noise: the preceding diagnostic already identifies this validation failure.
      }
      use_up_def_list
      {
          if (m_bIsCubic) {
              m_pCubic = new CCubic(m_id, m_level, m_slot, m_duration, m_delay, m_maxCount, m_power);
          } else {
              m_pCubic = new CAgathion(m_id, m_level, m_slot, m_duration, m_delay, m_maxCount, m_power);
          }
          m_pCubic->m_UseUp = m_bUseUp;
      }
      target_type_def op_cond_def
    ;

use_up_def_list
    : use_up_def_list use_up_def
    | %empty
    ;

use_up_def
    : USE_UP '=' INTEGER
      { m_bUseUp = grammar::SemanticInt64($3) == 1; }
    | DURATION '=' INTEGER
      { m_duration = grammar::SemanticInt32($3); }
    | DELAY '=' INTEGER
      { m_delay = grammar::SemanticInt32($3); }
    | MAX_COUNT '=' INTEGER
      { m_maxCount = grammar::SemanticInt32($3); }
    | POWER '=' integer_def_3
      { m_power = grammar::SemanticDouble($3); }
    ;

target_type_def
    : TARGET_TYPE '=' TARGET
      {
          m_pCubic->m_targetType = STT_TARGET;
          std::fill_n(m_pCubic->m_targetTypeOption, 4, 0.0);
          m_needSkillTarget = false;
          m_caseOfTargetHeal = false;
      }
    | TARGET_TYPE '=' BY_SKILL
      {
          m_pCubic->m_targetType = STT_BY_SKILL;
          std::fill_n(m_pCubic->m_targetTypeOption, 4, 0.0);
          m_needSkillTarget = true;
          m_caseOfTargetHeal = false;
      }
    | TARGET_TYPE '=' '{' HEAL ';' INTEGER '%' ';' INTEGER '%' ';' INTEGER '%' ';' INTEGER '%' '}'
      {
          m_pCubic->m_targetType = STT_HEAL;
          m_pCubic->m_targetTypeOption[0] = static_cast<double>(grammar::SemanticInt32($9)) / 100.0;
          m_pCubic->m_targetTypeOption[1] = static_cast<double>(grammar::SemanticInt32($12)) / 100.0;
          m_pCubic->m_targetTypeOption[2] = static_cast<double>(grammar::SemanticInt32($15)) / 100.0;
          m_pCubic->m_targetTypeOption[3] = static_cast<double>(grammar::SemanticInt32($6)) / 100.0;
          m_needSkillTarget = false;
          m_caseOfTargetHeal = true;
      }
    | TARGET_TYPE '=' MASTER
      {
          m_pCubic->m_targetType = STT_MASTER;
          std::fill_n(m_pCubic->m_targetTypeOption, 4, 0.0);
          m_needSkillTarget = false;
          m_caseOfTargetHeal = false;
      }
    ;

op_cond_def
    : OP_COND '=' '{' INTEGER ';' INTEGER '%' ';' INTEGER '}'
      {
          const std::uint32_t operation = static_cast<std::uint32_t>(grammar::SemanticInt32($4));
          SpriteSkillOp result = SSO_NULL;
          if (operation < 3) {
              result = static_cast<SpriteSkillOp>(operation + 1);
          } else {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Invalid op_cond while loading CubicData. (id[%d] level[%d])",
              //     m_id,
              //     m_level)
              // Previous: L2_LOG_ERROR(Log,
              //     L"Invalid op_cond while loading CubicData. (id[%d] level[%d])",
              //     m_id, m_level)
              L2_LOG_ERROR(
                  Log,
                  u"[cubic parser] Operation condition is invalid for cubic %d "
                  u"level %d; SSO_NULL is used.",
                  m_id, m_level
              );
              // Log.Add(
              //     LOG_ERROR,
              //     L"--- CubicData checking FAILED ---")
              // Previous: L2_LOG_ERROR(Log, L"--- CubicData checking FAILED ---")
              // Noise: the preceding diagnostic already identifies this validation failure.
          }
          m_pCubic->m_opCond.op = result;
          m_pCubic->m_opCond.hpRatio = grammar::SemanticInt32($6);
          m_pCubic->m_opCond.hpAbs = grammar::SemanticInt32($9);
      }
    | OP_COND '=' '{' DEBUFF '}'
      { m_pCubic->m_opCond = CubicOpCond{SSO_DEBUFF, 0, 0}; }
    | OP_COND '=' '{' '}'
      { m_pCubic->m_opCond = CubicOpCond{SSO_NULL, 0, 0}; }
    ;

n12
    : skill1_def skill2_def skill3_def
    ;

skill1_def
    : SKILL1 '=' '{' n22 '}'
      {
          const auto updateMaxActivateProb = [this](CubicSkillInfo* skill) {
              if (skill != nullptr && m_pCubic->m_maxActivateProb < skill->m_ActivateProb) {
                  m_pCubic->m_maxActivateProb = skill->m_ActivateProb;
              }
          };
          m_pCubic->m_skillList[0] = m_cubicSkillInfo;
          updateMaxActivateProb(m_cubicSkillInfo);
      }
    | SKILL1 '=' '{' '}'
      { m_pCubic->m_skillList[0] = nullptr; }
    ;

skill2_def
    : SKILL2 '=' '{' n22 '}'
      {
          const auto updateMaxActivateProb = [this](CubicSkillInfo* skill) {
              if (skill != nullptr && m_pCubic->m_maxActivateProb < skill->m_ActivateProb) {
                  m_pCubic->m_maxActivateProb = skill->m_ActivateProb;
              }
          };
          m_pCubic->m_skillList[1] = m_cubicSkillInfo;
          updateMaxActivateProb(m_cubicSkillInfo);
      }
    | SKILL2 '=' '{' '}'
      { m_pCubic->m_skillList[1] = nullptr; }
    ;

skill3_def
    : SKILL3 '=' '{' n22 '}'
      {
          const auto updateMaxActivateProb = [this](CubicSkillInfo* skill) {
              if (skill != nullptr && m_pCubic->m_maxActivateProb < skill->m_ActivateProb) {
                  m_pCubic->m_maxActivateProb = skill->m_ActivateProb;
              }
          };
          m_pCubic->m_skillList[2] = m_cubicSkillInfo;
          updateMaxActivateProb(m_cubicSkillInfo);
      }
    | SKILL3 '=' '{' '}'
      { m_pCubic->m_skillList[2] = nullptr; }
    ;

n16
    : timeskill1_def timeskill2_def timeskill3_def timeskill4_def timeskill5_def
    | timeskill1_def timeskill2_def timeskill3_def timeskill4_def
    | timeskill1_def timeskill2_def timeskill3_def
    | timeskill1_def timeskill2_def
    | timeskill1_def
    | %empty
    ;

timeskill1_def
    : TIMESKILL1 '=' '{' name_def '}'
      { m_pCubic->PushTimeSkill(m_TimeSkillInfo); }
    | TIMESKILL1 '=' '{' '}'
    ;

timeskill2_def
    : TIMESKILL2 '=' '{' name_def '}'
      { m_pCubic->PushTimeSkill(m_TimeSkillInfo); }
    | TIMESKILL2 '=' '{' '}'
    ;

timeskill3_def
    : TIMESKILL3 '=' '{' name_def '}'
      { m_pCubic->PushTimeSkill(m_TimeSkillInfo); }
    | TIMESKILL3 '=' '{' '}'
    ;

timeskill4_def
    : TIMESKILL4 '=' '{' name_def '}'
      { m_pCubic->PushTimeSkill(m_TimeSkillInfo); }
    | TIMESKILL4 '=' '{' '}'
    ;

timeskill5_def
    : TIMESKILL5 '=' '{' name_def '}'
      { m_pCubic->PushTimeSkill(m_TimeSkillInfo); }
    | TIMESKILL5 '=' '{' '}'
    ;

n22
    : integer_def ';' n24
    | integer_def
    ;

integer_def
    : INTEGER '%' ';' NAME ';' INTEGER '%' ';' INTEGER
      {
          const auto skillByName = [this](const char16_t* name) {
              const std::wstring hostName = target_utf16::ToHostCodeUnits(name);
              const CSkillInfo* const skill = g_SkillDB.GetSkillInfoByName(hostName.c_str());
              if (skill == nullptr) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined skill has found while loading CubicData. (id[%d] level[%d] skill[%s])",
                  //     m_id,
                  //     m_level,
                  //     hostName.c_str())
                  // Previous: L2_LOG_ERROR(Log,
                  //     L"Undefined skill has found while loading CubicData. (id[%d] level[%d] skill[%s])",
                  //     m_id, m_level, hostName.c_str())
                  L2_LOG_ERROR(
                      Log,
                      u"[cubic parser] Skill '%s' is undefined for cubic %d level "
                      u"%d; the skill pointer remains null.",
                      hostName.c_str(), m_id, m_level
                  );
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"--- CubicData checking FAILED ---")
                  // Previous: L2_LOG_ERROR(Log, L"--- CubicData checking FAILED ---")
                  // Noise: the preceding diagnostic already identifies this validation failure.
              }
              return const_cast<CSkillInfo*>(skill);
          };
          if (m_caseOfTargetHeal) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"There is no activation prob of skill while loading CubicData. (id[%d] level[%d])",
              //     m_id,
              //     m_level)
              // Previous: L2_LOG_ERROR(Log,
              //     L"There is no activation prob of skill while loading CubicData. (id[%d] level[%d])",
              //     m_id, m_level)
              L2_LOG_ERROR(
                  Log,
                  u"[cubic parser] Heal-target skill for cubic %d level %d "
                  u"unexpectedly specifies an activation probability; parsing "
                  u"continues with the supplied value.",
                  m_id, m_level
              );
              // Log.Add(LOG_ERROR, L"--- CubicData checking FAILED ---")
              // Previous: L2_LOG_ERROR(Log, L"--- CubicData checking FAILED ---")
              // Noise: the preceding diagnostic already identifies this validation failure.
          }
          m_prevProb = runtime::AddInt32Wrapping(m_prevProb, grammar::SemanticInt32($1));
          if (m_prevProb > 100) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Sum of skill activate prob is too large while loading CubicData. (id[%d] level[%d] sum of
              //     prob[%d])", m_id, m_level, m_prevProb)
              // Previous: L2_LOG_ERROR(Log,
              //     L"Sum of skill activate prob is too large while loading CubicData. (id[%d] level[%d] sum of
              //     prob[%d])", m_id, m_level, m_prevProb)
              L2_LOG_ERROR(
                  Log,
                  u"[cubic parser] Activation probabilities total %d for cubic "
                  u"%d level %d, exceeding 100; parsing continues with that "
                  u"total.",
                  m_prevProb, m_id, m_level
              );
              // Log.Add(LOG_ERROR, L"--- CubicData checking FAILED ---")
              // Previous: L2_LOG_ERROR(Log, L"--- CubicData checking FAILED ---")
              // Noise: the preceding diagnostic already identifies this validation failure.
          }
          m_cubicSkillInfo = NewCubicSkillInfo();
          m_cubicSkillInfo->m_ActivateProb = m_prevProb;
          m_cubicSkillInfo->m_pSkillInfo = skillByName(grammar::SemanticString($4));
          m_cubicSkillInfo->m_HitProb = grammar::SemanticInt32($6);
          m_cubicSkillInfo->m_bAttackStaticObject = grammar::SemanticInt32($9) != 0;
      }
    | NAME ';' INTEGER '%' ';' INTEGER
      {
          const auto skillByName = [this](const char16_t* name) {
              const std::wstring hostName = target_utf16::ToHostCodeUnits(name);
              const CSkillInfo* const skill = g_SkillDB.GetSkillInfoByName(hostName.c_str());
              if (skill == nullptr) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined skill has found while loading CubicData. (id[%d] level[%d] skill[%s])",
                  //     m_id,
                  //     m_level,
                  //     hostName.c_str())
                  // Previous: L2_LOG_ERROR(Log,
                  //     L"Undefined skill has found while loading CubicData. (id[%d] level[%d] skill[%s])",
                  //     m_id, m_level, hostName.c_str())
                  L2_LOG_ERROR(
                      Log,
                      u"[cubic parser] Skill '%s' is undefined for cubic %d level "
                      u"%d; the skill pointer remains null.",
                      hostName.c_str(), m_id, m_level
                  );
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"--- CubicData checking FAILED ---")
                  // Previous: L2_LOG_ERROR(Log, L"--- CubicData checking FAILED ---")
                  // Noise: the preceding diagnostic already identifies this validation failure.
              }
              return const_cast<CSkillInfo*>(skill);
          };
          if (!m_caseOfTargetHeal) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Heal target type do not need Activation prob of skill while loading CubicData. (id[%d] level[%d])",
              //     m_id,
              //     m_level)
              // Previous: L2_LOG_ERROR(Log,
              //     L"Heal target type do not need Activation prob of skill while loading CubicData. (id[%d] level[%d])",
              //     m_id, m_level)
              L2_LOG_ERROR(
                  Log,
                  u"[cubic parser] Non-heal skill for cubic %d level %d omits "
                  u"its activation probability; zero is used.",
                  m_id, m_level
              );
              // Log.Add(LOG_ERROR, L"--- CubicData checking FAILED ---")
              // Previous: L2_LOG_ERROR(Log, L"--- CubicData checking FAILED ---")
              // Noise: the preceding diagnostic already identifies this validation failure.
          }
          m_cubicSkillInfo = NewCubicSkillInfo();
          m_cubicSkillInfo->m_ActivateProb = 0;
          m_cubicSkillInfo->m_pSkillInfo = skillByName(grammar::SemanticString($1));
          m_cubicSkillInfo->m_HitProb = grammar::SemanticInt32($3);
          m_cubicSkillInfo->m_bAttackStaticObject = grammar::SemanticInt32($6) != 0;
      }
    ;

n24
    : heal_def ';' n28
      {
          if (!m_needSkillTarget) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Invalid TargetType while loading CubicData. (id[%d] level[%d])",
              //     m_id,
              //     m_level)
              // Previous: L2_LOG_ERROR(Log,
              //     L"Invalid TargetType while loading CubicData. (id[%d] level[%d])",
              //     m_id, m_level)
              L2_LOG_ERROR(
                  Log,
                  u"[cubic parser] Cubic %d level %d specifies a skill target "
                  u"where none is allowed; parsing continues.",
                  m_id, m_level
              );
              // Log.Add(LOG_ERROR, L"--- CubicData checking FAILED ---")
              // Previous: L2_LOG_ERROR(Log, L"--- CubicData checking FAILED ---")
              // Noise: the preceding diagnostic already identifies this validation failure.
          }
      }
    | n28
      {
          if (m_needSkillTarget) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Invalid TargetType while loading CubicData. (id[%d] level[%d])",
              //     m_id,
              //     m_level)
              // Previous: L2_LOG_ERROR(Log,
              //     L"Invalid TargetType while loading CubicData. (id[%d] level[%d])",
              //     m_id, m_level)
              L2_LOG_ERROR(
                  Log,
                  u"[cubic parser] Cubic %d level %d omits a required skill "
                  u"target; parsing continues.",
                  m_id, m_level
              );
              // Log.Add(LOG_ERROR, L"--- CubicData checking FAILED ---")
              // Previous: L2_LOG_ERROR(Log, L"--- CubicData checking FAILED ---")
              // Noise: the preceding diagnostic already identifies this validation failure.
          }
      }
    ;

name_def
    : NAME ';' INTEGER ';' heal_def ';' INTEGER
      {
          const auto skillByName = [this](const char16_t* name) {
              const std::wstring hostName = target_utf16::ToHostCodeUnits(name);
              const CSkillInfo* const skill = g_SkillDB.GetSkillInfoByName(hostName.c_str());
              if (skill == nullptr) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined skill has found while loading CubicData. (id[%d] level[%d] skill[%s])",
                  //     m_id,
                  //     m_level,
                  //     hostName.c_str())
                  // Previous: L2_LOG_ERROR(Log,
                  //     L"Undefined skill has found while loading CubicData. (id[%d] level[%d] skill[%s])",
                  //     m_id, m_level, hostName.c_str())
                  L2_LOG_ERROR(
                      Log,
                      u"[cubic parser] Skill '%s' is undefined for cubic %d level "
                      u"%d; the skill pointer remains null.",
                      hostName.c_str(), m_id, m_level
                  );
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"--- CubicData checking FAILED ---")
                  // Previous: L2_LOG_ERROR(Log, L"--- CubicData checking FAILED ---")
                  // Noise: the preceding diagnostic already identifies this validation failure.
              }
              return const_cast<CSkillInfo*>(skill);
          };
          const auto checkTimeSkillTarget = [this] {
              if (m_TimeSkillInfo.m_TargetType == STT_HEAL) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"time skill cannot be heal type. (id[%d] level[%d])",
                  //     m_id,
                  //     m_level)
                  // Previous: L2_LOG_ERROR(Log,
                  //     L"time skill cannot be heal type. (id[%d] level[%d])",
                  //     m_id, m_level)
                  L2_LOG_ERROR(
                      Log,
                      u"[cubic parser] Timed skill for cubic %d level %d uses the "
                      u"unsupported heal target type; the value is retained.",
                      m_id, m_level
                  );
              }
          };
          m_TimeSkillInfo.m_pSkillInfo = skillByName(grammar::SemanticString($1));
          m_TimeSkillInfo.m_bAttackStaticObject = grammar::SemanticInt32($3) != 0;
          checkTimeSkillTarget();
          m_TimeSkillInfo.m_CycleSec = grammar::SemanticInt32($7);
          m_TimeSkillInfo.m_eTimeSkillType = CTST_PERIOD;
      }
    | NAME ';' INTEGER ';' heal_def ';' INTEGER ';' period_def
      {
          const auto skillByName = [this](const char16_t* name) {
              const std::wstring hostName = target_utf16::ToHostCodeUnits(name);
              const CSkillInfo* const skill = g_SkillDB.GetSkillInfoByName(hostName.c_str());
              if (skill == nullptr) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined skill has found while loading CubicData. (id[%d] level[%d] skill[%s])",
                  //     m_id,
                  //     m_level,
                  //     hostName.c_str())
                  // Previous: L2_LOG_ERROR(Log,
                  //     L"Undefined skill has found while loading CubicData. (id[%d] level[%d] skill[%s])",
                  //     m_id, m_level, hostName.c_str())
                  L2_LOG_ERROR(
                      Log,
                      u"[cubic parser] Skill '%s' is undefined for cubic %d level "
                      u"%d; the skill pointer remains null.",
                      hostName.c_str(), m_id, m_level
                  );
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"--- CubicData checking FAILED ---")
                  // Previous: L2_LOG_ERROR(Log, L"--- CubicData checking FAILED ---")
                  // Noise: the preceding diagnostic already identifies this validation failure.
              }
              return const_cast<CSkillInfo*>(skill);
          };
          const auto checkTimeSkillTarget = [this] {
              if (m_TimeSkillInfo.m_TargetType == STT_HEAL) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"time skill cannot be heal type. (id[%d] level[%d])",
                  //     m_id,
                  //     m_level)
                  // Previous: L2_LOG_ERROR(Log,
                  //     L"time skill cannot be heal type. (id[%d] level[%d])",
                  //     m_id, m_level)
                  L2_LOG_ERROR(
                      Log,
                      u"[cubic parser] Timed skill for cubic %d level %d uses the "
                      u"unsupported heal target type; the value is retained.",
                      m_id, m_level
                  );
              }
          };
          m_TimeSkillInfo.m_pSkillInfo = skillByName(grammar::SemanticString($1));
          m_TimeSkillInfo.m_bAttackStaticObject = grammar::SemanticInt32($3) != 0;
          checkTimeSkillTarget();
          m_TimeSkillInfo.m_CycleSec = grammar::SemanticInt32($7);
      }
    ;

heal_def
    : HEAL
      {
          m_cubicSkillInfo->m_TargetType = STT_HEAL;
          m_TimeSkillInfo.m_TargetType = STT_HEAL;
      }
    | TARGET
      {
          m_cubicSkillInfo->m_TargetType = STT_TARGET;
          m_TimeSkillInfo.m_TargetType = STT_TARGET;
      }
    | MASTER
      {
          m_cubicSkillInfo->m_TargetType = STT_MASTER;
          m_TimeSkillInfo.m_TargetType = STT_MASTER;
      }
    ;

period_def
    : PERIOD
      { m_TimeSkillInfo.m_eTimeSkillType = CTST_PERIOD; }
    | DAILY
      { m_TimeSkillInfo.m_eTimeSkillType = CTST_DAILY; }
    ;

n28
    : '{' INTEGER ';' INTEGER '%' ';' INTEGER '}'
      {
          m_cubicSkillInfo->m_OpCond.op = static_cast<SpriteSkillOp>(grammar::SemanticInt32($2));
          m_cubicSkillInfo->m_OpCond.hpRatio = grammar::SemanticInt32($4);
          m_cubicSkillInfo->m_OpCond.hpAbs = grammar::SemanticInt32($7);
      }
    | '{' DEBUFF '}'
      { m_cubicSkillInfo->m_OpCond = CubicOpCond{SSO_DEBUFF, 0, 0}; }
    | '{' '}'
      { m_cubicSkillInfo->m_OpCond = CubicOpCond{SSO_NULL, 0, 0}; }
    ;

agathion_list
    : agathion_list agathion
    | agathion
    ;

agathion
    : AGATHION_BEGIN
      {
          m_bIsCubic = false;
          m_pCubic = nullptr;
          m_prevProb = 0;
          m_bUseUp = false;
      }
      npc_name_def id_def n12 n16 item_ids_def_list AGATHION_END
      {
          const auto checkTimeSkillTarget = [this] {
              if (m_TimeSkillInfo.m_TargetType == STT_HEAL) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"time skill cannot be heal type. (id[%d] level[%d])",
                  //     m_id,
                  //     m_level)
                  // Previous: L2_LOG_ERROR(Log,
                  //     L"time skill cannot be heal type. (id[%d] level[%d])",
                  //     m_id, m_level)
                  L2_LOG_ERROR(
                      Log,
                      u"[cubic parser] Timed skill for cubic %d level %d uses the "
                      u"unsupported heal target type; the value is retained.",
                      m_id, m_level
                  );
              }
          };
          if (m_pCubic != nullptr) {
              checkTimeSkillTarget();
              m_pCubic->SetNPCClassID(m_agathionNpcClassId);
              SpriteDB::Inst().Add(SPRITE_AGATHION, m_pCubic, m_id, m_level);
          }
      }
    ;

npc_name_def
    : NPC_NAME '=' NAME
      {
          const char16_t* const npcName = grammar::SemanticString($3);
          m_agathionNpcClassId = g_ObjectDB.GetClassIdFromName(npcName, false);
          if (m_agathionNpcClassId <= 999999) {
              const std::wstring hostName = target_utf16::ToHostCodeUnits(npcName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Invalid NPC name while loading AgathionData. (id[%d] level[%d] npc_name[%s]
              //     agathionNpcClassID[%d])", m_id, m_level, hostName.c_str(), m_agathionNpcClassId)
              // Previous: L2_LOG_ERROR(Log,
              //     L"Invalid NPC name while loading AgathionData. (id[%d] level[%d] npc_name[%s]
              //     agathionNpcClassID[%d])", m_id, m_level, hostName.c_str(), m_agathionNpcClassId)
              L2_LOG_ERROR(
                  Log,
                  u"[cubic parser] Agathion NPC '%s' resolved to invalid class "
                  u"ID %d for cubic %d level %d; the value is retained.",
                  hostName.c_str(), m_agathionNpcClassId, m_id, m_level
              );
          }
      }
    ;

item_ids_def_list
    : item_ids_def_list item_ids_def
    | %empty
    ;

item_ids_def
    : ITEM_IDS '=' '{' n35 '}'
    | ENERGY '=' INTEGER
      {
          if (!m_pCubic->SetEnergy(grammar::SemanticInt32($3))) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Fail to SetEnergy. (id[%d] agathionNpcClassID[%d])",
              //     m_id,
              //     m_agathionNpcClassId)
              // Previous: L2_LOG_ERROR(Log,
              //     L"Fail to SetEnergy. (id[%d] agathionNpcClassID[%d])",
              //     m_id,
              //     m_agathionNpcClassId)
              L2_LOG_ERROR(
                  Log,
                  u"[cubic parser] Cannot set energy for cubic %d "
                  u"(Agathion NPC class %d); the supplied value is rejected.",
                  m_id, m_agathionNpcClassId
              );
          }
      }
    | MAX_ENERGY '=' INTEGER
      {
          if (!m_pCubic->SetMaxEnergy(grammar::SemanticInt32($3))) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Fail to SetMaxEnergy. (id[%d] agathionNpcClassID[%d])",
              //     m_id,
              //     m_agathionNpcClassId)
              // Previous: L2_LOG_ERROR(Log,
              //     L"Fail to SetMaxEnergy. (id[%d] agathionNpcClassID[%d])",
              //     m_id,
              //     m_agathionNpcClassId)
              L2_LOG_ERROR(
                  Log,
                  u"[cubic parser] Cannot set maximum energy for cubic %d "
                  u"(Agathion NPC class %d); the supplied value is rejected.",
                  m_id, m_agathionNpcClassId
              );
          }
      }
    ;

n35
    : n35 ';' integer_def_2
    | integer_def_2
    ;

integer_def_2
    : INTEGER
      { m_pCubic->AddItemId(grammar::SemanticInt32($1)); }
    ;

integer_def_3
    : INTEGER
      { $$ = DoubleSemantic(static_cast<double>(grammar::SemanticInt64($1))); }
    | FLOAT_NUMBER
      { $$ = $1; }
    ;

%%
