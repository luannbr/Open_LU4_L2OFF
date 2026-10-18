// PledgeMission.txt grammar.
// Bison generates PledgeMissionParser.cpp and PledgeMissionParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/PledgeMissionParser.h"

#include "Shared/src/Log.h"
#include "Shared/src/TextRuntime.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "PledgeMissionParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForPledgeMission::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"PledgeMissionParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

CParserForPledgeMission::CParserForPledgeMission() : m_pStringVector(nullptr), m_pString(nullptr), m_lexer(), nMissionId(0) {
}

CParserForPledgeMission::~CParserForPledgeMission() = default;

int CParserForPledgeMission::Create(char16_t* fileBuffer, const int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}
}

%token EQUAL
%token PLEDGE_MISSION_BEGIN
%token PLEDGE_MISSION_END
%token ID
%token CATEGORY
%token IS_REPEAT
%token PLEDGE_LEVEL
%token S_PLEDGE_MASTERY
%token MIN_LEVEL
%token MAX_LEVEL
%token JOB_MAIN
%token JOB_DUAL
%token JOB_SUB
%token PRE_MISSION_ID
%token START_DATE
%token END_DATE
%token START_TIME
%token END_TIME
%token ACTIVATE_TIME
%token DEACTIVATE_TIME
%token AVAILABLE_DAY
%token GOAL_TYPE
%token GOAL_TARGET
%token GOAL_SUB_TARGET
%token GOAL_COUNT
%token REWARD_PLEDGE_NAME_VALUE
%token REWARD_PVP_POINT
%token REWARD_ITEMS
%token NAME
%token IDENTIFIER
%token INTEGER
%token FLOAT_NUMBER
// The original grammar has these conflicts. Bison resolves them as the
// original tables did. A shift wins over a reduce.
%expect 20
%start n1

%%

n1
    : pledge_mission_list
    ;

pledge_mission_list
    : pledge_mission_list pledge_mission
    | %empty
    ;

pledge_mission
    : PLEDGE_MISSION_BEGIN
      {
          pInfo = new PledgeMissionInfo;
          nMissionId = 0;
          pInfo->eCategory_ = static_cast<PledgeMission::Category>(0);
          pInfo->bRepeat_ = false;
          pInfo->eGoalType_ = PledgeMission::PMGT_MAX;
          pInfo->nGoalCount_ = 0;
          pInfo->nRewardPledgeNameValue_ = 0;
          pInfo->nRewardPVPPoint_ = 0;
          pInfo->vMissionConditionList_.push_back(new UserInPledge_MissionCondition);
      }
      id_def_list PLEDGE_MISSION_END
      {
          if (nMissionId != 0) {
              PledgeMissionDB::inst().mPledgeMissionInfoMap_.emplace(nMissionId, pInfo);
          }
          pInfo = nullptr;
      }
    ;

id_def_list
    : id_def_list id_def
    | %empty
    ;

id_def
    : ID EQUAL INTEGER
      { nMissionId = grammar::SemanticInt32($3); }
    | CATEGORY EQUAL INTEGER
      {
          pInfo->eCategory_ = static_cast<PledgeMission::Category>(grammar::SemanticInt32($3));
      }
    | IS_REPEAT EQUAL INTEGER
      { pInfo->bRepeat_ = $3 != 0; }
    | PLEDGE_LEVEL EQUAL INTEGER
      {
          if ($3 != 0) {
              pInfo->vMissionConditionList_.push_back(new PledgeLevel_MissionCondition(grammar::SemanticInt32($3)));
          }
      }
    | S_PLEDGE_MASTERY EQUAL '{' '}'
    | S_PLEDGE_MASTERY EQUAL '{' IDENTIFIER '}'
      {
          char16_t* const masteryName = grammar::SemanticString($4);
          if (*masteryName != u'\0') {
              pInfo->vMissionConditionList_.push_back(new PledgeMastery_MissionCondition(masteryName));
          }
      }
    | MIN_LEVEL EQUAL INTEGER
      {
          if ($3 != 0) {
              pInfo->vMissionConditionList_.push_back(new UserMinLevel_MissionCondition(grammar::SemanticInt32($3)));
          }
      }
    | MAX_LEVEL EQUAL INTEGER
      {
          if ($3 != 0) {
              pInfo->vMissionConditionList_.push_back(new UserMaxLevel_MissionCondition(grammar::SemanticInt32($3)));
          }
      }
    | JOB_MAIN EQUAL INTEGER JOB_DUAL EQUAL INTEGER JOB_SUB EQUAL INTEGER
      {
          pInfo->vMissionConditionList_.push_back(
              new UserJob_MissionCondition($3 != 0, $6 != 0, $9 != 0)
          );
      }
    | PRE_MISSION_ID EQUAL INTEGER
      {
          if ($3 != 0) {
              pInfo->vMissionConditionList_.push_back(new CompleteMission_MissionCondition(grammar::SemanticInt32($3)));
          }
      }
    | START_DATE EQUAL INTEGER START_TIME EQUAL INTEGER END_DATE EQUAL INTEGER END_TIME EQUAL INTEGER
      {
          const std::int64_t startDate = static_cast<std::int64_t>($3);
          const std::int64_t endDate = static_cast<std::int64_t>($9);
          if (startDate != 0) {
              if (endDate != 0) {
                  if (endDate <= startDate) {
                      // Log.Add(
                      //     LOG_ERROR,
                      //     L"[PledgeMission] must be start_date < end_date in line[%d]",
                      //     m_lexer.line + 1)
                      // L2_LOG_ERROR(Log,
                      //     L"[PledgeMission] must be start_date < end_date in line[%d]",
                      //     m_lexer.line + 1)
                      L2_LOG_ERROR(
                          Log,
                          "PledgeMission.txt line %lld, mission %d: end_date %lld is not greater than start_date %lld; "
                          "the date-time condition is still added using their low 32-bit values.",
                          static_cast<long long>(m_lexer.line) + 1, nMissionId, static_cast<long long>(endDate),
                          static_cast<long long>(startDate)
                      );
                  }
                  pInfo->vMissionConditionList_.push_back(new StartEndDateTime_MissionCondition(
                      grammar::SemanticInt32($3), grammar::SemanticInt32($6), grammar::SemanticInt32($9),
                      grammar::SemanticInt32($12)
                  ));
              } else {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"[PledgeMission] set end_date in line[%d]",
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"[PledgeMission] set end_date in line[%d]",
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      "PledgeMission.txt line %lld, mission %d: start_date %lld has no end_date; no date-time condition "
                      "is added.",
                      static_cast<long long>(m_lexer.line) + 1, nMissionId, static_cast<long long>(startDate)
                  );
              }
          } else if (endDate != 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[PledgeMission] set start_date in line[%d]",
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"[PledgeMission] set start_date in line[%d]",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "PledgeMission.txt line %lld, mission %d: end_date %lld has no start_date; no date-time condition is "
                  "added.",
                  static_cast<long long>(m_lexer.line) + 1, nMissionId, static_cast<long long>(endDate)
              );
          }
      }
    | ACTIVATE_TIME EQUAL INTEGER DEACTIVATE_TIME EQUAL INTEGER
      {
          const std::int64_t activate = static_cast<std::int64_t>($3);
          const std::int64_t deactivate = static_cast<std::int64_t>($6);
          if (activate != 0) {
              if (deactivate != 0) {
                  if (deactivate <= activate) {
                      // Log.Add(
                      //     LOG_ERROR,
                      //     L"[PledgeMission] must be activate_time < deactivate_time in line[%d]",
                      //     m_lexer.line + 1)
                      // L2_LOG_ERROR(Log,
                      //     L"[PledgeMission] must be activate_time < deactivate_time in line[%d]",
                      //     m_lexer.line + 1)
                      L2_LOG_ERROR(
                          Log,
                          "PledgeMission.txt line %lld, mission %d: deactivate_time %lld is not greater than "
                          "activate_time %lld; the time condition is still added using their low 32-bit values.",
                          static_cast<long long>(m_lexer.line) + 1, nMissionId, static_cast<long long>(deactivate),
                          static_cast<long long>(activate)
                      );
                  }
                  pInfo->vMissionConditionList_.push_back(
                      new StartEndTime_MissionCondition(grammar::SemanticInt32($3), grammar::SemanticInt32($6))
                  );
              } else {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"[PledgeMission] set deactivate_time in line[%d]",
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"[PledgeMission] set deactivate_time in line[%d]",
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      "PledgeMission.txt line %lld, mission %d: activate_time %lld has no deactivate_time; no time "
                      "condition is added.",
                      static_cast<long long>(m_lexer.line) + 1, nMissionId, static_cast<long long>(activate)
                  );
              }
          } else if (deactivate != 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[PledgeMission] set activate_time in line[%d]",
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"[PledgeMission] set activate_time in line[%d]",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "PledgeMission.txt line %lld, mission %d: deactivate_time %lld has no activate_time; no time condition "
                  "is added.",
                  static_cast<long long>(m_lexer.line) + 1, nMissionId, static_cast<long long>(deactivate)
              );
          }
      }
    | AVAILABLE_DAY
      {
          bEmpty = true;
          for (bool& day : bAvailableDay) {
              day = false;
          }
      }
      EQUAL '{' n10 '}'
      {
          if (!bEmpty) {
              pInfo->vMissionConditionList_.push_back(new AvailableDay_MissionCondition(bAvailableDay));
          }
      }
    | GOAL_TYPE EQUAL '{' IDENTIFIER '}'
      {
          char16_t* const goalName = grammar::SemanticString($4);
          pInfo->eGoalType_ = PledgeMissionStuff::ConvertGoalTypeStringToEnum(goalName);
          if (pInfo->eGoalType_ == PledgeMission::PMGT_MAX) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[PledgeMission] GoalType Unknown [%s] in line[%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"[PledgeMission] GoalType Unknown [%s] in line[%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log, "PledgeMission.txt line %lld, mission %d: unknown goal type '%s'; the goal type remains PMGT_MAX.",
                  static_cast<long long>(m_lexer.line) + 1, nMissionId, Utf16ToUtf8(goalName).c_str()
              );
          }
      }
    | GOAL_TARGET
      { pGoalCondition = new IsInIntList_MissionCondition; }
      EQUAL '{' n12 '}'
      {
          if (pGoalCondition->vIntList_.empty()) {
              delete pGoalCondition;
          } else {
              pInfo->vMissionGoalConditionList_.push_back(pGoalCondition);
          }
          pGoalCondition = nullptr;
      }
    | GOAL_SUB_TARGET
      { pGoalCondition = new IsInIntList_MissionCondition; }
      EQUAL '{' n12 '}'
      {
          if (!pGoalCondition->vIntList_.empty() && !pInfo->vMissionGoalConditionList_.empty()) {
              pInfo->vMissionGoalConditionList_.push_back(pGoalCondition);
          } else {
              if (!pGoalCondition->vIntList_.empty() && pInfo->vMissionGoalConditionList_.empty()) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"[PledgeMission] Goal Target Empty!! in line[%d]",
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"[PledgeMission] Goal Target Empty!! in line[%d]",
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      "PledgeMission.txt line %lld, mission %d: an additional goal-value list has no preceding goal "
                      "condition; the additional list is discarded.",
                      static_cast<long long>(m_lexer.line) + 1, nMissionId
                  );
              }
              delete pGoalCondition;
          }
          pGoalCondition = nullptr;
      }
    | GOAL_COUNT EQUAL INTEGER
      {
          if ($3 != 0) {
              pInfo->nGoalCount_ = grammar::SemanticInt32($3);
          } else {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[PledgeMission] Goal count zero in line[%d]",
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"[PledgeMission] Goal count zero in line[%d]",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "PledgeMission.txt line %lld, mission %d: goal count is zero; the current goal count %d is retained.",
                  static_cast<long long>(m_lexer.line) + 1, nMissionId, pInfo->nGoalCount_
              );
          }
      }
    | REWARD_PLEDGE_NAME_VALUE EQUAL INTEGER
      {
          if ($3 != 0) {
              pInfo->nRewardPledgeNameValue_ = grammar::SemanticInt32($3);
          }
      }
    | REWARD_PVP_POINT EQUAL INTEGER
      {
          if ($3 != 0) {
              pInfo->nRewardPVPPoint_ = grammar::SemanticInt32($3);
          }
      }
    | REWARD_ITEMS EQUAL '{' n14 '}'
    | %empty
    ;

n10
    : n10 ';' integer_def
    | integer_def
    | %empty
    ;

integer_def
    : INTEGER
      {
          bEmpty = false;
          bAvailableDay[$1] = true;
      }
    ;

n12
    : n12 ';' integer_def_2
    | integer_def_2
    | %empty
    ;

integer_def_2
    : INTEGER
      { pGoalCondition->vIntList_.push_back(grammar::SemanticInt32($1)); }
    ;

n14
    : n14 ';' '{' integer_def_3 '}'
    | '{' integer_def_3 '}'
    | %empty
    ;

integer_def_3
    : INTEGER ';' INTEGER
      { pInfo->mRewardItemMap_.emplace(grammar::SemanticInt32($1), grammar::SemanticInt32($3)); }
    ;

%%
