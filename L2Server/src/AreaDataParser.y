// areadata.txt grammar (original L2Server/areadata_parser.y).
// Bison generates AreaDataParser.cpp and AreaDataParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/AreaDataParser.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/Skill.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <string_view>

#include "AreaDataParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForArea::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"AreaDataParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

char16_t* SemanticWord(const grammar::SemanticBits value) {
    return grammar::SemanticString(value);
}

char16_t AsciiLower(char16_t value) {
    if (value >= u'A' && value <= u'Z') {
        return static_cast<char16_t>(value + (u'a' - u'A'));
    }
    return value;
}

bool EqualsAsciiIgnoreCase(const char16_t* value, const char16_t* expected) {
    if (value == nullptr || expected == nullptr) {
        return value == expected;
    }
    while (*value != u'\0' && *expected != u'\0') {
        if (AsciiLower(*value) != AsciiLower(*expected)) {
            return false;
        }
        ++value;
        ++expected;
    }
    return *value == u'\0' && *expected == u'\0';
}

bool ResolveAreaSkill(const char16_t* skillName, const int line, const bool spacedPath, CSkillInfo*& result) {
    const std::wstring hostName = target_utf16::ToHostCodeUnits(skillName);
    int skillId = 0;
    int level = 0;
    int subLevel = 0;
    if (g_SkillDB.GetSkillIdLevelByName(hostName.c_str(), &skillId, &level, &subLevel)) {
        result = const_cast<CSkillInfo*>(g_SkillDB.GetSkillInfo(CSkillKey(skillId, level, subLevel), nullptr));
        return true;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     spacedPath
    //         ? L"%s [%d] : Undefined skill(%s)"
    //         : L"%s[%d] : Undefined skill(%s)",
    //     L"..\\Script\\areadata.txt",
    //     line,
    //     hostName.c_str())
    L2_LOG_ERROR(
        Log, spacedPath ? L"%s [%d] : Undefined skill(%s)" : L"%s[%d] : Undefined skill(%s)", L"..\\Script\\areadata.txt",
        line, hostName.c_str()
    );
    return false;
}

} // namespace

CParserForArea::CParserForArea() : m_vWaterPos{}, mvIntList(), m_lexer() {
    m_pTerritoryList = nullptr;
}

CParserForArea::~CParserForArea() = default;

int CParserForArea::Create(char16_t* const fileBuffer, const int fileSize) {
    // Current L2Server 0x00c71514.
    return m_lexer.Create(fileBuffer, fileSize);
}

// Original diagnostic body kept.
// void CParserForArea::yysyntaxerror() {
//     // Current L2Server 0x00c71584.
//     wchar_t message[56]{}
//     // std::swprintf(
//     //     message,
//     //     std::size(message),
//     //     L"%s [%d] : syntax error",
//     //     L"..\\Script\\areadata.txt",
//     //     m_lexer.line + 1)
//     std::swprintf(
//         message,
//         std::size(message),
//         L"%ls [%d] : syntax error",
//         L"..\\Script\\areadata.txt",
//         m_lexer.line + 1)
//     // Log.Add(LOG_ERROR, message)
//     // Log.Add(LOG_ERROR, L"%ls", message)
//     L2_LOG_ERROR(Log, L"%ls", message)
//
//     const std::wstring token = target_utf16::ToHostCodeUnits(m_lexer.yytext)
//     // Noise: The error is already logged above. Only Windows needs a modal copy.
//     // PopUpWindow(message)
// #if defined(_WIN32)
//     PopUpWindow(L"%ls", message)
// #endif
//     // PopUpWindow(token.c_str())
//     PopUpWindow(L"[scripts] areadata.txt: offending token \"%ls\".", token.c_str())
// }
void CParserForArea::yysyntaxerror() {
    // Decode the diagnostic token as text, instead of native-wide surrogate halves.
    const std::wstring token = Utf16ToWide(m_lexer.yytext);
    const long long line = static_cast<long long>(m_lexer.line) + 1;
    L2_LOG_ERROR(Log, u"[scripts] areadata.txt: syntax error at line %lld. Offending token: \"%ls\".", line, token.c_str());

    // Noise: One main-log record includes the line and token. Retain both Windows dialogs.
#if defined(_WIN32)
    char16_t message[56]{};
    FormatUtf16To(message, std::size(message), u"%ls [%lld] : syntax error", u"..\\Script\\areadata.txt", line);
    PopUpWindow(L"%ls", message);
    PopUpWindow(L"[scripts] areadata.txt: offending token \"%ls\".", token.c_str());
#endif
}
}

%token EQUAL
%token DEFAULT_SETTING_BEGIN
%token DEFAULT_SETTING_END
%token AREA_BEGIN
%token AREA_END
%token TYPE
%token NAME
%token MOVE_BONUS
%token MESSAGE_NO
%token ENTERING_MESSAGE_NO
%token LEAVING_MESSAGE_NO
%token DAMAGE_ON_HP
%token DAMAGE_ON_MP
%token SKILL_NAME
%token HP_REGEN_BONUS
%token MP_REGEN_BONUS
%token RANGE
%token BANNED_RANGE
%token WATER_RANGE
%token DEFAULT_STATUS
%token SHOW_DANGERZONE
%token EVENT_ID
%token INITIAL_DELAY
%token ON_TIME
%token OFF_TIME
%token RANDOM_TIME
%token INSTANTZONE_ID
%token SKILL_PROB
%token UNIT_TICK
%token TELEPORT_RANGE
%token TELEPORT_POINTS
%token MAP_NO
%token TARGET
%token AFFECT_RACE
%token SSQ
%token RESTART_TIME
%token BLOCKED_ACTIONS
%token SKILL_LIST
%token SKILL_ACTION_TYPE
%token EXP_PENALTY_PER
%token ITEM_DROP
%token HPMP_REGEN_ON_LEVELUP
%token RESTART_ALLOWED_TIME
%token RESTART_POINTS
%token SEC
%token MIN_KW
%token HOUR
%token DAY
%token ENTER_INZONE_ID
%token FLYMOVE_INFO
%token UNION_AS_PARTY
%token MPCC_AS_PARTY
%token NAME_2
%token IDENTIFIER
%token RANDOM1_SERIAL
%token FLOAT_NUMBER
%token ON_OFF
%token SSQ_ZONE_GROUP
%token PRIV_VALUE
%start n1

%%

n1
    : default_setting_list area_list
    ;

default_setting_list
    : default_setting_list default_setting
    | default_setting
    ;

default_setting
    : DEFAULT_SETTING_BEGIN type_def_list DEFAULT_SETTING_END
    ;

type_def_list
    : type_def_list type_def
    | type_def
    ;

type_def
    : TYPE EQUAL SSQ_ZONE_GROUP
    | SKILL_NAME EQUAL NAME_2
      {
          ResolveAreaSkill(SemanticWord($3), m_lexer.line + 1, false, m_pSkillInfoDef);
      }
    | MOVE_BONUS EQUAL RANDOM1_SERIAL
      { m_nMoveBonusDef = grammar::SemanticInt32($3); }
    | DAMAGE_ON_HP EQUAL RANDOM1_SERIAL
      { m_nDamageOnHPDef = grammar::SemanticInt32($3); }
    | DAMAGE_ON_MP EQUAL RANDOM1_SERIAL
      { m_nDamageOnMPDef = grammar::SemanticInt32($3); }
    | HP_REGEN_BONUS EQUAL RANDOM1_SERIAL
      { m_nHpRegenBonus = grammar::SemanticInt32($3); }
    | MP_REGEN_BONUS EQUAL RANDOM1_SERIAL
      { m_nMpRegenBonus = grammar::SemanticInt32($3); }
    ;

area_list
    : area_list area
    | %empty
    ;

area
    : AREA_BEGIN
      {
          m_pArea = new CArea;
          m_pArea->m_nMoveBonus = m_nMoveBonusDef;
          m_pArea->m_nDamageOnHP = m_nDamageOnHPDef;
          m_pArea->m_nDamageOnMP = m_nDamageOnMPDef;
          m_pArea->m_nHpRegenBonus = m_nHpRegenBonus;
          m_pArea->m_nMpRegenBonus = m_nMpRegenBonus;
          m_nEventID = 0;
      }
      name_def_list AREA_END
      {
          if (m_nEventID == 0) {
              m_pArea->m_pEventController = nullptr;
          } else {
              m_pArea->MakeEventController(m_nEventID);
          }
          CAreaDB::inst().AddArea(m_pArea);
      }
    ;

name_def_list
    : name_def_list name_def
    | name_def
    ;

name_def
    : NAME EQUAL NAME_2
      { m_pArea->m_FieldTerritory.SetName(SemanticWord($3)); }
    | MAP_NO EQUAL '{' RANDOM1_SERIAL ';' RANDOM1_SERIAL '}'
      {
          m_pArea->m_nMapX = grammar::SemanticInt32($4);
          m_pArea->m_nMapY = grammar::SemanticInt32($6);
      }
    | TYPE EQUAL SSQ_ZONE_GROUP
      { m_pArea->SetAreaType(static_cast<AreaTypeEnum>(grammar::SemanticInt32($3))); }
    | SSQ EQUAL ON_OFF
      { m_pArea->m_bSSQArea = grammar::SemanticInt64($3) != 0; }
    | RESTART_TIME EQUAL random1_serial_def
      { m_pArea->m_nRestartTime = grammar::SemanticInt32($3); }
    | RESTART_ALLOWED_TIME EQUAL random1_serial_def
      { m_pArea->mRestartAllowedTime = grammar::SemanticInt32($3); }
    | RANGE
      { m_pTerritory = &m_pArea->m_FieldTerritory; }
      EQUAL n27
    | BANNED_RANGE
      {
          m_pTerritoryList = &m_pArea->m_BannedFieldTerritories;
          m_pArea->m_bIsBannedTerritoryOn.clear();
          m_pTerritory = new CTerritory;
          m_pTerritoryList->push_back(m_pTerritory);
          m_pArea->m_bIsBannedTerritoryOn.push_back(true);
      }
      EQUAL '{' n25 '}'
      {
          if (m_pTerritoryList != nullptr && m_pTerritoryList->size() >= 6) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s [%d] : BannedTerritory size > 5!",
              //     L"..\\Script\\areadata.txt",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(Log, u"%s [%d] : BannedTerritory size > 5!", u"..\\Script\\areadata.txt", m_lexer.line + 1);
          }
      }
    | WATER_RANGE
      { m_pTerritory = &m_pArea->m_FieldTerritory; }
      EQUAL n30
    | TELEPORT_RANGE
      { m_pTerritory = &m_pArea->m_TeleportTerritory; }
      EQUAL n27
    | TELEPORT_POINTS
      { m_pPointsList = &m_pArea->m_vecTeleportPoints; }
      EQUAL n20
    | SKILL_NAME EQUAL NAME_2
      {
          CSkillInfo* skillInfo = nullptr;
          if (ResolveAreaSkill(SemanticWord($3), m_lexer.line + 1, true, skillInfo)) {
              m_pArea->AddSkillInfo(skillInfo);
          }
      }
    | SKILL_PROB EQUAL RANDOM1_SERIAL
      { m_pArea->SetSkillProb(grammar::SemanticInt32($3)); }
    | SKILL_LIST EQUAL n17
      { m_pArea->AddSkillInfo(mvIntList); }
    | SKILL_ACTION_TYPE EQUAL RANDOM1_SERIAL
      {
          m_pArea->SetSkillActionType(static_cast<AreaSkillActionType>(grammar::SemanticInt32($3)));
      }
    | TARGET EQUAL IDENTIFIER
      {
          const char16_t* const target = SemanticWord($3);
          if (EqualsAsciiIgnoreCase(target, u"pc")) {
              m_pArea->SetAreaTargetType(AREATT_PC);
          } else if (EqualsAsciiIgnoreCase(target, u"npc")) {
              m_pArea->SetAreaTargetType(AREATT_NPC);
          } else if (EqualsAsciiIgnoreCase(target, u"all")) {
              m_pArea->SetAreaTargetType(AREATT_ALL);
          } else if (EqualsAsciiIgnoreCase(target, u"only_pc")) {
              m_pArea->SetAreaTargetType(AREATT_ONLY_PC);
          } else {
              const std::wstring hostTarget = target_utf16::ToHostCodeUnits(target);
              // Log.Add(
              //     LOG_ERROR,
              //     L"invalid target type [%s]",
              //     hostTarget.c_str())
              L2_LOG_ERROR(Log, u"invalid target type [%s]", hostTarget.c_str());
          }
      }
    | MOVE_BONUS EQUAL RANDOM1_SERIAL
      { m_pArea->m_nMoveBonus = grammar::SemanticInt32($3); }
    | DAMAGE_ON_HP EQUAL RANDOM1_SERIAL
      { m_pArea->m_nDamageOnHP = grammar::SemanticInt32($3); }
    | DAMAGE_ON_MP EQUAL RANDOM1_SERIAL
      { m_pArea->m_nDamageOnMP = grammar::SemanticInt32($3); }
    | HP_REGEN_BONUS EQUAL RANDOM1_SERIAL
      { m_pArea->m_nHpRegenBonus = grammar::SemanticInt32($3); }
    | MP_REGEN_BONUS EQUAL RANDOM1_SERIAL
      { m_pArea->m_nMpRegenBonus = grammar::SemanticInt32($3); }
    | MESSAGE_NO EQUAL RANDOM1_SERIAL
      { m_pArea->m_nMessageNO = grammar::SemanticInt32($3); }
    | ENTERING_MESSAGE_NO EQUAL RANDOM1_SERIAL
      { m_pArea->m_nEnteringMessageNO = grammar::SemanticInt32($3); }
    | LEAVING_MESSAGE_NO EQUAL RANDOM1_SERIAL
      { m_pArea->m_nLeavingMessageNO = grammar::SemanticInt32($3); }
    | DEFAULT_STATUS EQUAL ON_OFF
      { m_pArea->m_bIsOn = grammar::SemanticInt64($3) == 1; }
    | SHOW_DANGERZONE EQUAL ON_OFF
      { m_pArea->m_bShowDangerZone = grammar::SemanticInt64($3) == 1; }
    | EVENT_ID EQUAL RANDOM1_SERIAL
      { m_nEventID = grammar::SemanticInt32($3); }
    | INITIAL_DELAY EQUAL RANDOM1_SERIAL
      {
          m_pArea->m_nInitialDelay = runtime::MultiplyInt32Wrapping(grammar::SemanticInt32($3), 1000U);
      }
    | ON_TIME EQUAL RANDOM1_SERIAL
      {
          m_pArea->m_nOnTime = runtime::MultiplyInt32Wrapping(grammar::SemanticInt32($3), 1000U);
      }
    | OFF_TIME EQUAL RANDOM1_SERIAL
      {
          m_pArea->m_nOffTime = runtime::MultiplyInt32Wrapping(grammar::SemanticInt32($3), 1000U);
      }
    | RANDOM_TIME EQUAL RANDOM1_SERIAL
      {
          m_pArea->m_nRandomTime = runtime::MultiplyInt32Wrapping(grammar::SemanticInt32($3), 1000U);
      }
    | EXP_PENALTY_PER EQUAL RANDOM1_SERIAL
      {
          m_pArea->mExpPenaltyPer = static_cast<double>(grammar::SemanticInt64($3)) / 100.0;
      }
    | ITEM_DROP EQUAL ON_OFF
      { m_pArea->mIsItemDrop = grammar::SemanticInt64($3) != 0; }
    | HPMP_REGEN_ON_LEVELUP EQUAL RANDOM1_SERIAL
      { m_pArea->mHPMPRegenOnLevelUp = grammar::SemanticInt64($3) != 0; }
    | UNIT_TICK EQUAL RANDOM1_SERIAL
      { m_pArea->SetUnitTick(grammar::SemanticInt32($3)); }
    | AFFECT_RACE EQUAL IDENTIFIER
      {
          const char16_t* const race = SemanticWord($3);
          if (EqualsAsciiIgnoreCase(race, u"all")) {
              std::fill_n(m_pArea->m_vAffectRace.begin(), 7, true);
          } else if (EqualsAsciiIgnoreCase(race, u"human")) {
              m_pArea->m_vAffectRace[0] = true;
          } else if (EqualsAsciiIgnoreCase(race, u"elf")) {
              m_pArea->m_vAffectRace[1] = true;
          } else if (EqualsAsciiIgnoreCase(race, u"darkelf")) {
              m_pArea->m_vAffectRace[2] = true;
          } else if (EqualsAsciiIgnoreCase(race, u"orc")) {
              m_pArea->m_vAffectRace[3] = true;
          } else if (EqualsAsciiIgnoreCase(race, u"dwarf")) {
              m_pArea->m_vAffectRace[4] = true;
          } else if (EqualsAsciiIgnoreCase(race, u"kamael")) {
              m_pArea->m_vAffectRace[5] = true;
          } else if (EqualsAsciiIgnoreCase(race, u"ertheia")) {
              m_pArea->m_vAffectRace[6] = true;
          }
      }
    | RESTART_POINTS
      { m_pPointsList = &m_pArea->m_vecRestartPoints; }
      EQUAL n20
    | BLOCKED_ACTIONS EQUAL n23
    | INSTANTZONE_ID EQUAL RANDOM1_SERIAL
      { m_pArea->m_InstantZoneKey = {grammar::SemanticInt32($3), 0}; }
    | INSTANTZONE_ID EQUAL RANDOM1_SERIAL ';' RANDOM1_SERIAL
      { m_pArea->m_InstantZoneKey = {grammar::SemanticInt32($3), grammar::SemanticInt32($5)}; }
    | ENTER_INZONE_ID EQUAL RANDOM1_SERIAL
      { m_pArea->m_EnterInzoneKey = {grammar::SemanticInt32($3), 0}; }
    | ENTER_INZONE_ID EQUAL RANDOM1_SERIAL ';' RANDOM1_SERIAL
      { m_pArea->m_EnterInzoneKey = {grammar::SemanticInt32($3), grammar::SemanticInt32($5)}; }
    | FLYMOVE_INFO EQUAL RANDOM1_SERIAL ';' RANDOM1_SERIAL
      { m_pArea->m_FlyMoveInfo = {grammar::SemanticInt32($3), grammar::SemanticInt32($5)}; }
    | UNION_AS_PARTY EQUAL ON_OFF
      { m_pArea->m_bMPCCAsParty = grammar::SemanticInt64($3) == 1; }
    | MPCC_AS_PARTY EQUAL ON_OFF
      { m_pArea->m_bMPCCAsParty = grammar::SemanticInt64($3) == 1; }
    ;

n17
    : '{'
      { mvIntList.clear(); }
      n19 '}'
    ;

n19
    : n19 ';' RANDOM1_SERIAL
      { mvIntList.push_back(grammar::SemanticInt64($3)); }
    | RANDOM1_SERIAL
      { mvIntList.push_back(grammar::SemanticInt64($1)); }
    | %empty
    ;

n20
    : '{' n21 '}'
    ;

n21
    : n21 ';' n22
    | n22
    ;

n22
    : '{' RANDOM1_SERIAL ';' RANDOM1_SERIAL ';' RANDOM1_SERIAL '}'
      {
          m_pPointsList->push_back(
              FVector(grammar::SemanticInt32($2), grammar::SemanticInt32($4), grammar::SemanticInt32($6))
          );
      }
    | %empty
    ;

n23
    : '{' '}'
    | '{' n24 '}'
    ;

n24
    : n24 ';' PRIV_VALUE
      {
          const std::uint32_t index = static_cast<std::uint32_t>(grammar::SemanticInt32($3));
          if (index <= 1U) {
              m_pArea->m_BlockedActionTable[index] = 1;
          }
      }
    | PRIV_VALUE
      {
          const std::uint32_t index = static_cast<std::uint32_t>(grammar::SemanticInt32($1));
          if (index <= 1U) {
              m_pArea->m_BlockedActionTable[index] = 1;
          }
      }
    ;

n25
    : n27
    | n25 ';'
      {
          m_pTerritory = new CTerritory;
          m_pTerritoryList->push_back(m_pTerritory);
          m_pArea->m_bIsBannedTerritoryOn.push_back(true);
      }
      n27
    ;

n27
    : '{' n28 '}'
    | '{' '}'
    ;

n28
    : n29
    | n28 ';' n29
    ;

n29
    : '{' RANDOM1_SERIAL ';' RANDOM1_SERIAL ';' RANDOM1_SERIAL ';' RANDOM1_SERIAL '}'
      {
          const std::int64_t bottom = grammar::SemanticInt64($6);
          const std::int64_t top = grammar::SemanticInt64($8);
          if (bottom < m_pTerritory->GetBottom()) {
              m_pTerritory->SetBottom(static_cast<int>(bottom));
          }
          if (top > m_pTerritory->GetTop()) {
              m_pTerritory->SetTop(static_cast<int>(top));
          }
          m_pVector = new FVector(grammar::SemanticInt32($2), grammar::SemanticInt32($4), 0);
          m_pTerritory->AddTerritoryPoint(m_pVector);
      }
    ;

n30
    : '{' IDENTIFIER ':' '(' FLOAT_NUMBER FLOAT_NUMBER FLOAT_NUMBER ')' IDENTIFIER ':' '(' FLOAT_NUMBER FLOAT_NUMBER
      FLOAT_NUMBER ')' '}'
      {
          const int firstX = runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($5));
          const int firstY = runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($6));
          const int secondX = runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($12));
          const int secondY = runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($13));
          m_pTerritory->SetBottom(runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($7)) - 7);
          m_pTerritory->SetTop(runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($14)) - 7);
          const FVector corners[4] = {
              FVector(firstX, firstY, 0),
              FVector(secondX, firstY, 0),
              FVector(secondX, secondY, 0),
              FVector(firstX, secondY, 0),
          };
          for (const FVector& corner : corners) {
              m_pVector = new FVector(corner);
              m_pTerritory->AddTerritoryPoint(m_pVector);
          }
      }
    ;

random1_serial_def
    : RANDOM1_SERIAL
      { $$ = $1; }
    | RANDOM1_SERIAL SEC
      { $$ = $1; }
    | RANDOM1_SERIAL MIN_KW
      { $$ = static_cast<std::uint64_t>(grammar::SemanticInt64($1)) * 60U; }
    | RANDOM1_SERIAL HOUR
      { $$ = static_cast<std::uint64_t>(grammar::SemanticInt64($1)) * 3600U; }
    | RANDOM1_SERIAL DAY
      { $$ = static_cast<std::uint64_t>(grammar::SemanticInt64($1)) * 86400U; }
    | RANDOM1_SERIAL HOUR RANDOM1_SERIAL MIN_KW RANDOM1_SERIAL SEC
      {
          const std::uint64_t hours = static_cast<std::uint64_t>(grammar::SemanticInt64($1));
          const std::uint64_t minutes = static_cast<std::uint64_t>(grammar::SemanticInt64($3));
          const std::uint64_t seconds = static_cast<std::uint64_t>(grammar::SemanticInt64($5));
          $$ = (hours * 60U + minutes) * 60U + seconds;
      }
    ;

%%
