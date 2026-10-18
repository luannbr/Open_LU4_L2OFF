// FieldCycle.txt grammar (original L2Server/parser_fieldCycle.y).
// Bison generates ParserFieldCycle.cpp and ParserFieldCycleTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserFieldCycle.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/Area.h"
#include "L2Server/src/Door.h"

#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/Territory.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

#include "ParserFieldCycleTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForFieldCycle::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserFieldCycleTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

const char16_t* SemanticWord(const grammar::SemanticBits value) {
    return grammar::SemanticString(value);
}

char16_t FoldAscii(const char16_t value) {
    return value >= u'A' && value <= u'Z' ? static_cast<char16_t>(value + (u'a' - u'A')) : value;
}

bool EqualsAsciiInsensitive(const char16_t* value, const std::u16string_view expected) {
    if (value == nullptr) {
        return false;
    }
    std::size_t index = 0;
    while (value[index] != u'\0' && index < expected.size()) {
        if (FoldAscii(value[index]) != FoldAscii(expected[index])) {
            return false;
        }
        ++index;
    }
    return index == expected.size() && value[index] == u'\0';
}

} // namespace

CParserForFieldCycle::CParserForFieldCycle() :
    m_lexer(),
    m_nCycleID(0),
    m_nPrevCycleID(0),
    m_nPrevStep(0),
    m_nIntervalTime(0),
    m_nIntervalPoint(0),
    m_nStepChangeDay(-1),
    m_nStepChangeHour(-1),
    m_nStepChangeMinute(-1),
    m_stringList() {
}

CParserForFieldCycle::~CParserForFieldCycle() = default;

int CParserForFieldCycle::Create(char16_t* const fileBuffer, const int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}

// void CParserForFieldCycle::yysyntaxerror() {
//     wchar_t message[60]{}
//     std::swprintf(
//         message,
//         std::size(message),
//         L"FieldCycleDataChecker[%d] : syntax error",
//         m_lexer.line)
//     // Log.Add(LOG_ERROR, message)
//     // L2_LOG_ERROR(Log, message)
//     L2_LOG_ERROR(Log, L"%ls", message)
// }
void CParserForFieldCycle::yysyntaxerror() {
    L2_LOG_ERROR(
        Log, "FieldCycle.txt line %lld: syntax error (cycle ID %d, last accepted step %d).",
        static_cast<long long>(m_lexer.line) + 1, m_nCycleID, m_nPrevStep
    );
}
}

%token CYCLE_BEGIN
%token CYCLE_END
%token ID
%token MISSION_START_STEP
%token MISSION_TIME_LIMIT
%token MISSION_SUCCESS_STEP
%token MISSION_FAILED_STEP
%token MISSION_AREA
%token STEP_BEGIN
%token STEP_END
%token STEP
%token STEP_POINT
%token LOCK_TIME
%token DROP_TIME
%token OPEN_DOOR
%token AREA_ON
%token STEP_CHANGE_TIME
%token STEP_CHANGE_TIME_IN_RANGE
%token STEP_CHANGE_TIME_BY_WEEKDAY
%token EXPIRATION_TIME
%token RESTART_BEGIN
%token RESTART_END
%token RANGE
%token NORMAL_POINT
%token CHAO_POINT
%token INTERVAL_POINT
%token INTERVAL_TIME
%token MAP_POINT
%token MAP_STRING
%token MAP_HUNTING_ZONE_ID
%token NAME
%token INTEGER
%start cycle_list

%%

cycle_list
    : cycle_list cycle
    | cycle
    ;

cycle
    : CYCLE_BEGIN id_def step_list CYCLE_END
      {
          if (m_nCycleID > 0 && m_nCycleID == m_nPrevCycleID + 1) {
              m_nPrevCycleID = m_nCycleID;
              g_FieldCycleSystem.AddField(m_nCycleID, m_pFieldCycle);
          } else {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[%s] Invalid FieldCycle ID while loading FieldCycleData. (prevID[%d], nowID[%d], expectedID[%d])",
              //     L"parser_fieldCycle.y",
              //     m_nPrevCycleID,
              //     m_nCycleID,
              //     m_nPrevCycleID + 1)
              // L2_LOG_ERROR(Log,
              //     L"[%s] Invalid FieldCycle ID while loading FieldCycleData. (prevID[%d], nowID[%d], expectedID[%d])",
              //     L"parser_fieldCycle.y",
              //     m_nPrevCycleID,
              //     m_nCycleID,
              //     m_nPrevCycleID + 1)
              L2_LOG_ERROR(
                  Log,
                  "FieldCycle.txt line %lld: cycle ID %d must be positive and follow ID %d (expected %lld); this cycle is "
                  "not registered.",
                  static_cast<long long>(m_lexer.line) + 1, m_nCycleID, m_nPrevCycleID,
                  static_cast<long long>(m_nPrevCycleID) + 1
              );
          }
      }
    ;

id_def
    : ID '=' INTEGER
      {
          m_nCycleID = grammar::SemanticInt32($3);
          m_nPrevStep = 0;
          m_pFieldCycle = new CFieldCycle(m_nCycleID);
          m_pStepInfo = new StepInfo;
          m_pStepInfo->stepNumber = 0;
          m_pFieldCycle->AddStep(0, 0, m_pStepInfo);
      }
      mission_start_step_def mission_time_limit_def mission_success_step_def mission_failed_step_def mission_area_def
    ;

mission_start_step_def
    : MISSION_START_STEP '=' INTEGER
      { m_pFieldCycle->SetMissionStartStep(grammar::SemanticInt32($3)); }
    | %empty
    ;

mission_time_limit_def
    : MISSION_TIME_LIMIT '=' INTEGER
      { m_pFieldCycle->SetMissionLimitTime(grammar::SemanticInt32($3)); }
    | %empty
    ;

mission_success_step_def
    : MISSION_SUCCESS_STEP '=' INTEGER
      { m_pFieldCycle->SetMissionSuccessStep(grammar::SemanticInt32($3)); }
    | %empty
    ;

mission_failed_step_def
    : MISSION_FAILED_STEP '=' INTEGER
      { m_pFieldCycle->SetMissionFailedStep(grammar::SemanticInt32($3)); }
    | %empty
    ;

mission_area_def
    : MISSION_AREA
      { m_stringList.clear(); }
      '=' '{' n27 '}'
      {
          std::vector<CArea*> areas;
          for (const std::u16string& name : m_stringList) {
              CArea* const area = CAreaDB::inst().FindArea(name.c_str());
              if (area != nullptr) {
                  areas.push_back(area);
              }
          }
          m_pFieldCycle->SetMissionArea(std::move(areas));
      }
    | %empty
    ;

step_list
    : step_list step
    | step
    ;

step
    : STEP_BEGIN step_def STEP_END
      {
          if (m_nStep < 1 || m_nStep != m_nPrevStep + 1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[%s] Invalid FieldCycle Step while loading FieldCycleData. (prevStep[%d], nowStep[%d],
              //     expectedStep[%d], cycleID[%d])", L"parser_fieldCycle.y", m_nPrevStep, m_nStep, m_nPrevStep + 1,
              //     m_nCycleID)
              // L2_LOG_ERROR(Log,
              //     L"[%s] Invalid FieldCycle Step while loading FieldCycleData. (prevStep[%d], nowStep[%d],
              //     expectedStep[%d], cycleID[%d])", L"parser_fieldCycle.y", m_nPrevStep, m_nStep, m_nPrevStep + 1,
              //     m_nCycleID)
              L2_LOG_ERROR(
                  Log,
                  "FieldCycle.txt line %lld, cycle %d: step %d must be positive and follow step %d (expected %lld); this "
                  "step is not added.",
                  static_cast<long long>(m_lexer.line) + 1, m_nCycleID, m_nStep, m_nPrevStep,
                  static_cast<long long>(m_nPrevStep) + 1
              );
              break;
          }
          m_nPrevStep = m_nStep;
          m_pFieldCycle->AddStep(m_nStep, m_nPoint, m_pStepInfo);
      }
    ;

step_def
    : STEP '=' INTEGER STEP_POINT '=' INTEGER
      {
          m_nStep = grammar::SemanticInt32($3);
          m_nPoint = grammar::SemanticInt32($6);
          m_pStepInfo = new StepInfo;
          m_pStepInfo->stepNumber = static_cast<char>(m_nStep);
      }
      lock_time_def drop_time_def interval_time_def expiration_time_def step_change_time_def step_change_time_in_range_def
      step_change_time_by_weekday_def open_door_def area_on_def restart map_point_def map_string_def map_hunting_zone_id_def
    ;

lock_time_def
    : LOCK_TIME '=' INTEGER
      { m_pStepInfo->lockTime = grammar::SemanticInt32($3); }
    | %empty
    ;

drop_time_def
    : DROP_TIME '=' INTEGER
      { m_pStepInfo->dropTime = grammar::SemanticInt32($3); }
    | %empty
    ;

interval_time_def
    : INTERVAL_TIME '=' INTEGER INTERVAL_POINT '=' INTEGER
      {
          m_pStepInfo->intervalTime = grammar::SemanticInt32($3);
          m_pStepInfo->intervalPoint = grammar::SemanticInt32($6);
      }
    | %empty
    ;

expiration_time_def
    : EXPIRATION_TIME '=' INTEGER
      { m_pStepInfo->expirationTime = grammar::SemanticInt32($3); }
    | %empty
    ;

step_change_time_def
    : STEP_CHANGE_TIME '=' '{' NAME ';' '{' INTEGER ':' INTEGER '}' '}'
      {
          const char16_t* const day = SemanticWord($4);
          if (EqualsAsciiInsensitive(day, u"mon")) {
              m_pStepInfo->stepChangeTime.day = 1;
          } else if (EqualsAsciiInsensitive(day, u"tue")) {
              m_pStepInfo->stepChangeTime.day = 2;
          } else if (EqualsAsciiInsensitive(day, u"wed")) {
              m_pStepInfo->stepChangeTime.day = 3;
          } else if (EqualsAsciiInsensitive(day, u"thu")) {
              m_pStepInfo->stepChangeTime.day = 4;
          } else if (EqualsAsciiInsensitive(day, u"fri")) {
              m_pStepInfo->stepChangeTime.day = 5;
          } else if (EqualsAsciiInsensitive(day, u"sat")) {
              m_pStepInfo->stepChangeTime.day = 6;
          } else if (EqualsAsciiInsensitive(day, u"sun")) {
              m_pStepInfo->stepChangeTime.day = 0;
          } else {
              m_pStepInfo->stepChangeTime.day = -1;
          }
          m_pStepInfo->stepChangeTime.hour = grammar::SemanticInt32($7);
          m_pStepInfo->stepChangeTime.min = grammar::SemanticInt32($9);
      }
    | %empty
    ;

step_change_time_in_range_def
    : STEP_CHANGE_TIME_IN_RANGE '=' '{' '{' INTEGER '~' INTEGER '}' ':' '{' INTEGER '~' INTEGER '}' '}'
      {
          // Log.Add(
          //     LOG_ERROR,
          //     L"[FIELD_CYCLE] step_change_time_in_range is deprecated")
          // L2_LOG_ERROR(Log,
          //     L"[FIELD_CYCLE] step_change_time_in_range is deprecated")
          L2_LOG_WARN(
              Log,
              "FieldCycle.txt line %lld, cycle %d, step %d: deprecated step_change_time_in_range syntax; parsing "
              "continues.",
              static_cast<long long>(m_lexer.line) + 1, m_nCycleID, m_nStep
          );
      }
    | %empty
    ;

step_change_time_by_weekday_def
    : STEP_CHANGE_TIME_BY_WEEKDAY '=' '{' '{' INTEGER '}' ':' '{' INTEGER '~' INTEGER '}' ':' '{' n22 '}' '}'
      {
          m_pStepInfo->stepChangeTimeInRange.nWeek = grammar::SemanticInt32($5);
          m_pStepInfo->stepChangeTimeInRange.hour.first = grammar::SemanticInt32($9);
          m_pStepInfo->stepChangeTimeInRange.hour.second = grammar::SemanticInt32($11);
          if (m_pStepInfo->stepChangeTimeInRange.nWeek < 1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[FIELD_CYCLE] week < 1: %d, %d",
              //     m_nCycleID,
              //     static_cast<int>(m_pStepInfo->stepNumber))
              // L2_LOG_ERROR(Log,
              //     L"[FIELD_CYCLE] week < 1: %d, %d",
              //     m_nCycleID,
              //     static_cast<int>(m_pStepInfo->stepNumber))
              L2_LOG_ERROR(
                  Log,
                  "FieldCycle.txt line %lld, cycle %d, step %d: step_change_time_in_range week value %d is below 1; fatal "
                  "parser error.",
                  static_cast<long long>(m_lexer.line) + 1, m_nCycleID, m_nStep, m_pStepInfo->stepChangeTimeInRange.nWeek
              );
              ForceCrash(CrashInfo::LEX_YACC_FAILED);
          }
          if (m_pStepInfo->stepChangeTimeInRange.hour.second < m_pStepInfo->stepChangeTimeInRange.hour.first) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[FIELD_CYCLE] hour range is invalid: %d, %d, %d, %d",
              //     m_nCycleID,
              //     static_cast<int>(m_pStepInfo->stepNumber),
              //     m_pStepInfo->stepChangeTimeInRange.hour.first,
              //     m_pStepInfo->stepChangeTimeInRange.hour.second)
              // L2_LOG_ERROR(Log,
              //     L"[FIELD_CYCLE] hour range is invalid: %d, %d, %d, %d",
              //     m_nCycleID,
              //     static_cast<int>(m_pStepInfo->stepNumber),
              //     m_pStepInfo->stepChangeTimeInRange.hour.first,
              //     m_pStepInfo->stepChangeTimeInRange.hour.second)
              L2_LOG_ERROR(
                  Log,
                  "FieldCycle.txt line %lld, cycle %d, step %d: step_change_time_in_range end hour %d is below start hour "
                  "%d; fatal parser error.",
                  static_cast<long long>(m_lexer.line) + 1, m_nCycleID, m_nStep,
                  m_pStepInfo->stepChangeTimeInRange.hour.second, m_pStepInfo->stepChangeTimeInRange.hour.first
              );
              ForceCrash(CrashInfo::LEX_YACC_FAILED);
          }
          if (m_pStepInfo->stepChangeTimeInRange.wdayFilter.empty()) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[FIELD_CYCLE] week day filter is empty: %d, %d",
              //     m_nCycleID,
              //     static_cast<int>(m_pStepInfo->stepNumber))
              // L2_LOG_ERROR(Log,
              //     L"[FIELD_CYCLE] week day filter is empty: %d, %d",
              //     m_nCycleID,
              //     static_cast<int>(m_pStepInfo->stepNumber))
              L2_LOG_ERROR(
                  Log,
                  "FieldCycle.txt line %lld, cycle %d, step %d: step_change_time_in_range weekday filter is empty; fatal "
                  "parser error.",
                  static_cast<long long>(m_lexer.line) + 1, m_nCycleID, m_nStep
              );
              ForceCrash(CrashInfo::LEX_YACC_FAILED);
          }
      }
    | %empty
    ;

n22
    : n22 ';' NAME
      {
          const char16_t* const day = SemanticWord($3);
          const L2DateTime::DayOfWeek value = L2DateTime::ConvertDayOfWeek(std::u16string(day));
          if (value == L2DateTime::NONE) {
              // const std::wstring hostDay = target_utf16::ToHostCodeUnits(day)
              const std::string hostDay = Utf16ToUtf8(day);
              // Log.Add(
              //     LOG_ERROR,
              //     L"[FIELD_CYCLE] week day is invalid: %d, %d, %s",
              //     m_nCycleID,
              //     static_cast<int>(m_pStepInfo->stepNumber),
              //     hostDay.c_str())
              // L2_LOG_ERROR(Log,
              //     L"[FIELD_CYCLE] week day is invalid: %d, %d, %s",
              //     m_nCycleID,
              //     static_cast<int>(m_pStepInfo->stepNumber),
              //     hostDay.c_str())
              L2_LOG_ERROR(
                  Log,
                  "FieldCycle.txt line %lld, cycle %d, step %d: invalid weekday '%s' in step_change_time_in_range; fatal "
                  "parser error.",
                  static_cast<long long>(m_lexer.line) + 1, m_nCycleID, m_nStep,
                  day == nullptr ? "<null>" : hostDay.c_str()
              );
              ForceCrash(CrashInfo::LEX_YACC_FAILED);
              break;
          }
          m_pStepInfo->stepChangeTimeInRange.wdayFilter.push_back(value);
      }
    | NAME
      {
          const char16_t* const day = SemanticWord($1);
          const L2DateTime::DayOfWeek value = L2DateTime::ConvertDayOfWeek(std::u16string(day));
          if (value == L2DateTime::NONE) {
              // const std::wstring hostDay = target_utf16::ToHostCodeUnits(day)
              const std::string hostDay = Utf16ToUtf8(day);
              // Log.Add(
              //     LOG_ERROR,
              //     L"[FIELD_CYCLE] week day is invalid: %d, %d, %s",
              //     m_nCycleID,
              //     static_cast<int>(m_pStepInfo->stepNumber),
              //     hostDay.c_str())
              // L2_LOG_ERROR(Log,
              //     L"[FIELD_CYCLE] week day is invalid: %d, %d, %s",
              //     m_nCycleID,
              //     static_cast<int>(m_pStepInfo->stepNumber),
              //     hostDay.c_str())
              L2_LOG_ERROR(
                  Log,
                  "FieldCycle.txt line %lld, cycle %d, step %d: invalid weekday '%s' in step_change_time_in_range; fatal "
                  "parser error.",
                  static_cast<long long>(m_lexer.line) + 1, m_nCycleID, m_nStep,
                  day == nullptr ? "<null>" : hostDay.c_str()
              );
              ForceCrash(CrashInfo::LEX_YACC_FAILED);
              break;
          }
          m_pStepInfo->stepChangeTimeInRange.wdayFilter.push_back(value);
      }
    ;

open_door_def
    : OPEN_DOOR
      { m_stringList.clear(); }
      '=' '{' n27 '}'
      {
          for (const std::u16string& name : m_stringList) {
              CDoor* const door = g_DoorDB.GetDoor(name.c_str(), 0);
              if (door != nullptr) {
                  m_pStepInfo->openDoorList.push_back(door);
              }
          }
      }
    | %empty
    ;

area_on_def
    : AREA_ON
      { m_stringList.clear(); }
      '=' '{' n27 '}'
      {
          for (const std::u16string& name : m_stringList) {
              CArea* const area = CAreaDB::inst().FindArea(name.c_str());
              if (area != nullptr) {
                  m_pStepInfo->areaOnList.push_back(area);
              }
          }
      }
    ;

n27
    : n27 ';' NAME
      { m_stringList.emplace_back(SemanticWord($3)); }
    | NAME
      { m_stringList.emplace_back(SemanticWord($1)); }
    | %empty
    ;

restart
    : RESTART_BEGIN n29 RESTART_END
      {
          m_pStepInfo->restartInfo = m_pRestartInfo;
          m_pRestartInfo->pTerritory = m_pTerritory;
      }
    | %empty
    ;

n29
    : range_def normal_point_def
    ;

range_def
    : RANGE
      {
          m_pRestartInfo = new RestartInfo;
          m_pTerritory = new CTerritory;
      }
      '=' '{' n33 '}'
    ;

normal_point_def
    : NORMAL_POINT '=' '{' INTEGER ';' INTEGER ';' INTEGER '}' CHAO_POINT '=' '{' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          m_pRestartInfo->normalPoint =
              FVector(grammar::SemanticInt32($4), grammar::SemanticInt32($6), grammar::SemanticInt32($8));
          m_pRestartInfo->chaoPoint =
              FVector(grammar::SemanticInt32($13), grammar::SemanticInt32($15), grammar::SemanticInt32($17));
      }
    ;

n33
    : n33 ';' n34
    | n34
    ;

n34
    : '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          m_pTerritory->SetBottom(std::min(m_pTerritory->GetBottom(), grammar::SemanticInt32($6)));
          m_pTerritory->SetTop(std::max(m_pTerritory->GetTop(), grammar::SemanticInt32($8)));
          FVector point(grammar::SemanticInt32($2), grammar::SemanticInt32($4), grammar::SemanticInt32($9));
          m_pTerritory->AddTerritoryPoint(&point);
      }
    | %empty
    ;

map_point_def
    : MAP_POINT '=' '{' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          m_pStepInfo->mapPoint =
              FVector(grammar::SemanticInt32($4), grammar::SemanticInt32($6), grammar::SemanticInt32($8));
      }
    | %empty
    ;

map_string_def
    : MAP_STRING '=' INTEGER
      { m_pStepInfo->mapStringNo = grammar::SemanticInt32($3); }
    | %empty
    ;

map_hunting_zone_id_def
    : MAP_HUNTING_ZONE_ID '=' INTEGER
      { m_pStepInfo->mapHuntingZoneID = grammar::SemanticInt32($3); }
    | %empty
    ;

%%
