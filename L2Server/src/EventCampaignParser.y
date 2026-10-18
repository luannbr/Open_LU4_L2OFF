// eventcampaign.txt grammar.
// Bison generates EventCampaignParser.cpp and EventCampaignParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/EventCampaignParser.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/Area.h"
#include "L2Server/src/Item.h"
#include "L2Server/src/ObjectDB.h"

#include "Shared/src/Log.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "EventCampaignParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForEventCampaign::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"EventCampaignParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

const char16_t* SemanticText(grammar::SemanticBits value) {
    return grammar::SemanticString(value);
}

} // namespace

CParserForEventCampaign::CParserForEventCampaign() :
    m_lexer(),
    m_pEventCampaign(nullptr),
    m_pStep(nullptr),
    m_pPrevStep(nullptr),
    m_pNextStep(nullptr),
    m_pStepMultiInfo(nullptr),
    m_rewardItemData{},
    intList(),
    stringList() {
    Init();
}

CParserForEventCampaign::~CParserForEventCampaign() = default;

int CParserForEventCampaign::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}

void CParserForEventCampaign::Init() {
    m_pEventCampaign = nullptr;
    m_pStep = nullptr;
    m_pPrevStep = nullptr;
    m_pNextStep = nullptr;
    m_pStepMultiInfo = nullptr;
    intList.clear();
    stringList.clear();
}

void CParserForEventCampaign::AddEventCampaign() {
    if (!CEventCampaignManager::Inst()->AddEventCampaign(m_pEventCampaign)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[EventCampaign] Duplicated eventcamapign id[%d] in line[%d] while loading a eventcampaign data",
        //     m_pEventCampaign->GetID(),
        //     m_lexer.line + 1)
        // L2_LOG_ERROR(Log,
        //     L"[EventCampaign] Duplicated eventcamapign id[%d] in line[%d] while loading a eventcampaign data",
        //     m_pEventCampaign->GetID(),
        //     m_lexer.line + 1)
        L2_LOG_ERROR(
            Log,
            "eventcampaign.txt line %lld: duplicate campaign ID %d; the previous definition is kept and loading continues.",
            static_cast<long long>(m_lexer.line) + 1, m_pEventCampaign->GetID()
        );
    }
    Init();
}
}

%token EVENT_CAMPAIGN_BEGIN
%token EVENT_CAMPAIGN_END
%token EVENT_CAMPAIGN_ID
%token ELITE_COUNT
%token REWARD_PERIOD
%token START_CONDITION_CHECK_BEGIN
%token START_CONDITION_CHECK_END
%token LEVEL
%token AUTO_EXECUTION_BEGIN
%token AUTO_EXECUTION_END
%token PERIOD
%token TIME_DICE
%token STEP_LIST_BEGIN
%token STEP_LIST_END
%token STEP_BEGIN
%token STEP_END
%token STEP_ID
%token DURATION
%token GROUP_RANDOM_SELECT_RATE
%token START_HTML
%token NPC_MAKER
%token AREA_ON
%token GOAL_GROUP_LIST_BEGIN
%token GOAL_GROUP_LIST_END
%token GOAL_GROUP_BEGIN
%token GOAL_GROUP_END
%token GOAL_GROUP_ID
%token CONTEXT_HTML
%token FAILED_HTML
%token REWARD_HTML
%token ELITE_REWARD_HTML
%token REWARD_RECEIVED_HTML
%token GOAL
%token ACCEPT_ITEM
%token REWARD
%token ELITE_REWARD
%token TELEPORT_POS
%token NAME
%token STRING
%token INTEGER
// The original grammar has 14 reduce/reduce conflicts. Bison
// resolves them as the original tables did. A shift wins over a reduce,
// and the earlier rule wins over a later rule.
%start n1

%%

n1
    : event_campaign_list
    ;

event_campaign_list
    : event_campaign_list event_campaign
    | %empty
    ;

event_campaign
    : EVENT_CAMPAIGN_BEGIN EVENT_CAMPAIGN_ID '=' INTEGER
      { m_pEventCampaign = new CEventCampaign(grammar::SemanticInt32($4)); }
      n5 start_condition_check auto_execution step_list EVENT_CAMPAIGN_END
      { AddEventCampaign(); }
    ;

n5
    : n5 n6
    | %empty
    ;

n6
    : elite_count_def
    | reward_period_def
    ;

elite_count_def
    : ELITE_COUNT '=' INTEGER
      { m_pEventCampaign->SetEliteCount(grammar::SemanticInt32($3)); }
    ;

reward_period_def
    : REWARD_PERIOD '=' INTEGER
      { m_pEventCampaign->SetRewardPeriod(grammar::SemanticInt32($3)); }
    ;

start_condition_check
    : START_CONDITION_CHECK_BEGIN n10 START_CONDITION_CHECK_END
    | %empty
    ;

n10
    : n10 n11
    | %empty
    ;

n11
    : level_def
    ;

level_def
    : LEVEL '=' '~' INTEGER
      {
          int maxLevel = grammar::SemanticInt32($4);
          if (maxLevel > 120) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[EventCampaign] invalid max level(%d) in line[%d] while loading a eventcampaign data",
              //     maxLevel,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"[EventCampaign] invalid max level(%d) in line[%d] while loading a eventcampaign data",
              //     maxLevel,
              //     m_lexer.line + 1)
              L2_LOG_WARN(
                  Log,
                  "eventcampaign.txt line %lld: maximum level %d exceeds 120; it is reduced to 120 for the campaign start "
                  "condition.",
                  static_cast<long long>(m_lexer.line) + 1, maxLevel
              );
              maxLevel = 120;
          }
          m_pEventCampaign->SetStartConditionLevel(0, maxLevel);
      }
    | LEVEL '=' INTEGER '~' INTEGER
      {
          int minLevel = grammar::SemanticInt32($3);
          if (minLevel < 1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[EventCampaign] invalid min level(%d) in line[%d] while loading a eventcampaign data",
              //     minLevel,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"[EventCampaign] invalid min level(%d) in line[%d] while loading a eventcampaign data",
              //     minLevel,
              //     m_lexer.line + 1)
              L2_LOG_WARN(
                  Log,
                  "eventcampaign.txt line %lld: minimum level %d is below 1; it is raised to 1 for the campaign start "
                  "condition.",
                  static_cast<long long>(m_lexer.line) + 1, minLevel
              );
              minLevel = 1;
          }
          const int maxLevel = grammar::SemanticInt32($5);
          if (maxLevel > 120) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[EventCampaign] invalid max level(%d) in line[%d] while loading a eventcampaign data",
              //     maxLevel,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"[EventCampaign] invalid max level(%d) in line[%d] while loading a eventcampaign data",
              //     maxLevel,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "eventcampaign.txt line %lld: maximum level %d exceeds 120 in a level range; this value is retained for "
                  "the campaign start condition.",
                  static_cast<long long>(m_lexer.line) + 1, maxLevel
              );
          }
          m_pEventCampaign->SetStartConditionLevel(minLevel, maxLevel);
      }
    | LEVEL '=' INTEGER '~'
      {
          int minLevel = grammar::SemanticInt32($3);
          if (minLevel < 1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[EventCampaign] invalid min level(%d) in line[%d] while loading a eventcampaign data",
              //     minLevel,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"[EventCampaign] invalid min level(%d) in line[%d] while loading a eventcampaign data",
              //     minLevel,
              //     m_lexer.line + 1)
              L2_LOG_WARN(
                  Log,
                  "eventcampaign.txt line %lld: minimum level %d is below 1; it is raised to 1 for the campaign start "
                  "condition.",
                  static_cast<long long>(m_lexer.line) + 1, minLevel
              );
              minLevel = 1;
          }
          m_pEventCampaign->SetStartConditionLevel(minLevel, 120);
      }
    ;

auto_execution
    : AUTO_EXECUTION_BEGIN period_def time_dice_def AUTO_EXECUTION_END
    ;

period_def
    : PERIOD '=' STRING '~' STRING
      {
          auto* const period = new EVENTCAMPAIGN::ExecutionPeriod;
          TimePeriodInfo::ParseTime(SemanticText($3), period->m_PeriodInfo.m_nStartTime);
          TimePeriodInfo::ParseTime(SemanticText($5), period->m_PeriodInfo.m_nEndTime);
          m_pEventCampaign->SetAutoExecutionPeriod(period);
      }
    ;

time_dice_def
    : TIME_DICE '=' '{' n16 '}'
    ;

n16
    : n17
    | %empty
    ;

n17
    : n17 ';' n18
    | n18
    ;

n18
    : '{' INTEGER ':' INTEGER ';' INTEGER '}'
      {
          m_pEventCampaign->m_StartTimeList.push_back(EventCampaignStartTimeData{
              grammar::SemanticInt32($2) * 60 + grammar::SemanticInt32($4), grammar::SemanticInt32($6)
          });
      }
    ;

step_list
    : STEP_LIST_BEGIN step_list_2 STEP_LIST_END
    ;

step_list_2
    : step_list_2 step
    | %empty
    ;

step
    : STEP_BEGIN STEP_ID '=' INTEGER DURATION '=' INTEGER START_HTML '=' STRING
      {
          const auto targetStringAt = [](const grammar::SemanticBits slot) {
              const char16_t* const value = SemanticText(slot);
              return std::u16string(value != nullptr ? value : u"");
          };
          m_pStep = new CEventCampaignStep(grammar::SemanticInt32($4));
          m_pStep->SetActivateDuration(grammar::SemanticInt32($7));
          m_pStep->m_StartHTMLName = targetStringAt($10);
      }
      group_random_select_rate_def npc_maker_def area_on_def goal_group_list STEP_END
      {
          m_pEventCampaign->AddStep(m_pStep);
          m_pStep = nullptr;
      }
    ;

group_random_select_rate_def
    : GROUP_RANDOM_SELECT_RATE '=' '{' n24 '}'
    | %empty
    ;

n24
    : n24 ';' n25
    | n25
    ;

n25
    : '{' INTEGER ';' INTEGER '}'
      {
          m_pStep->m_AdjustedGoalRate[static_cast<std::size_t>(grammar::SemanticInt32($2) - 1)] =
              grammar::SemanticInt32($5);
      }
    ;

npc_maker_def
    : NPC_MAKER '=' '{' n45 '}'
      {
          m_pStep->m_NPCMakerList.insert(m_pStep->m_NPCMakerList.end(), stringList.begin(), stringList.end());
          stringList.clear();
      }
    ;

area_on_def
    : AREA_ON '=' '{' n45 '}'
      {
          for (const std::u16string& name : stringList) {
              CArea* const area = CAreaDB::inst().FindArea(name.c_str());
              if (area != nullptr) {
                  m_pStep->m_AreaList.push_back(area);
              }
          }
          stringList.clear();
      }
    | %empty
    ;

goal_group_list
    : GOAL_GROUP_LIST_BEGIN goal_group_list_2 GOAL_GROUP_LIST_END
    ;

goal_group_list_2
    : goal_group_list_2 goal_group
    | %empty
    ;

goal_group
    : GOAL_GROUP_BEGIN GOAL_GROUP_ID '=' INTEGER CONTEXT_HTML '=' STRING FAILED_HTML '=' STRING REWARD_HTML '=' STRING
      ELITE_REWARD_HTML '=' STRING REWARD_RECEIVED_HTML '=' STRING
      {
          const auto targetStringAt = [](const grammar::SemanticBits slot) {
              const char16_t* const value = SemanticText(slot);
              return std::u16string(value != nullptr ? value : u"");
          };
          m_pStepMultiInfo = new CEventCampaignStepMultiInfo(grammar::SemanticInt32($4) - 1);
          m_pStepMultiInfo->m_ContextHTMLName = targetStringAt($7);
          m_pStepMultiInfo->m_FailedHTMLName = targetStringAt($10);
          m_pStepMultiInfo->m_RewardHTMLName = targetStringAt($13);
          m_pStepMultiInfo->m_EliteRewardHTMLName = targetStringAt($16);
          m_pStepMultiInfo->m_RewardReceivedHTMLName = targetStringAt($19);
      }
      goal_def accept_item_def reward_def elite_reward_def teleport_pos_def GOAL_GROUP_END
      {
          const int group = grammar::SemanticInt32($4) - 1;
          if (group > 4) {
              break;
          }
          m_pStep->m_MultiInfo[static_cast<std::size_t>(group)] = m_pStepMultiInfo;
          m_pStepMultiInfo = nullptr;
      }
    ;

goal_def
    : GOAL '=' '{' n33 '}'
    ;

n33
    : n33 ';' n34
    | n34
    ;

n34
    : '{' INTEGER ';' INTEGER '}'
      {
          m_pStepMultiInfo->m_GoalMap.emplace(
              static_cast<unsigned int>(grammar::SemanticInt32($2)),
              EventCampaignGoalScore{grammar::SemanticInt32($4), 0}
          );
          ++m_pStepMultiInfo->m_GoalCount;
      }
    ;

accept_item_def
    : ACCEPT_ITEM '=' '{' n38 '}'
    | %empty
    ;

reward_def
    : REWARD '=' '{' n40 '}'
    ;

elite_reward_def
    : ELITE_REWARD '=' '{' n42 '}'
    ;

n38
    : n38 ';' n39
    | n39
    ;

n39
    : '{' NAME ';' INTEGER '}'
      {
          const char16_t* const itemName = SemanticText($2);
          CObject* const object = g_ObjectDB.GetObjectByName(itemName);
          if (object != nullptr && object->IsItem()) {
              CItem* const item = object->CastItem();
              if (item != nullptr) {
                  m_rewardItemData.itemClassId = static_cast<unsigned int>(item->GetClassId());
                  m_rewardItemData.itemCount = grammar::SemanticInt64($4);
                  m_pStepMultiInfo->m_AcceptList.push_back(m_rewardItemData);
                  break;
              }
          }
          // const std::wstring hostName = target_utf16::ToHostCodeUnits(itemName)
          const std::string hostName = itemName != nullptr ? Utf16ToUtf8(itemName) : "<null>";
          // Log.Add(
          //     LOG_ERROR,
          //     L"[EventCampaign] Undefined item[%s] in line[%d] while loading a eventcampaign data",
          //     hostName.c_str(),
          //     m_lexer.line + 1)
          // L2_LOG_ERROR(Log,
          //     L"[EventCampaign] Undefined item[%s] in line[%d] while loading a eventcampaign data",
          //     hostName.c_str(),
          //     m_lexer.line + 1)
          L2_LOG_ERROR(
              Log,
              "eventcampaign.txt line %lld: undefined item '%s' in the %s list; this entry is omitted and loading "
              "continues.",
              static_cast<long long>(m_lexer.line) + 1, hostName.c_str(),
              "start-item");
      }
    ;

n40
    : n40 ';' n41
    | n41
    ;

n41
    : '{' NAME ';' INTEGER '}'
      {
          const char16_t* const itemName = SemanticText($2);
          CObject* const object = g_ObjectDB.GetObjectByName(itemName);
          if (object != nullptr && object->IsItem()) {
              CItem* const item = object->CastItem();
              if (item != nullptr) {
                  m_rewardItemData.itemClassId = static_cast<unsigned int>(item->GetClassId());
                  m_rewardItemData.itemCount = grammar::SemanticInt64($4);
                  m_pStepMultiInfo->m_RewardList.push_back(m_rewardItemData);
                  break;
              }
          }
          // const std::wstring hostName = target_utf16::ToHostCodeUnits(itemName)
          const std::string hostName = itemName != nullptr ? Utf16ToUtf8(itemName) : "<null>";
          // Log.Add(
          //     LOG_ERROR,
          //     L"[EventCampaign] Undefined item[%s] in line[%d] while loading a eventcampaign data",
          //     hostName.c_str(),
          //     m_lexer.line + 1)
          // L2_LOG_ERROR(Log,
          //     L"[EventCampaign] Undefined item[%s] in line[%d] while loading a eventcampaign data",
          //     hostName.c_str(),
          //     m_lexer.line + 1)
          L2_LOG_ERROR(
              Log,
              "eventcampaign.txt line %lld: undefined item '%s' in the %s list; this entry is omitted and loading "
              "continues.",
              static_cast<long long>(m_lexer.line) + 1, hostName.c_str(),
              ("reward"));
      }
    ;

n42
    : n42 ';' n43
    | n43
    ;

n43
    : '{' NAME ';' INTEGER '}'
      {
          const char16_t* const itemName = SemanticText($2);
          CObject* const object = g_ObjectDB.GetObjectByName(itemName);
          if (object != nullptr && object->IsItem()) {
              CItem* const item = object->CastItem();
              if (item != nullptr) {
                  m_rewardItemData.itemClassId = static_cast<unsigned int>(item->GetClassId());
                  m_rewardItemData.itemCount = grammar::SemanticInt64($4);
                  m_pStepMultiInfo->m_EliteRewardList.push_back(m_rewardItemData);
                  break;
              }
          }
          // const std::wstring hostName = target_utf16::ToHostCodeUnits(itemName)
          const std::string hostName = itemName != nullptr ? Utf16ToUtf8(itemName) : "<null>";
          // Log.Add(
          //     LOG_ERROR,
          //     L"[EventCampaign] Undefined item[%s] in line[%d] while loading a eventcampaign data",
          //     hostName.c_str(),
          //     m_lexer.line + 1)
          // L2_LOG_ERROR(Log,
          //     L"[EventCampaign] Undefined item[%s] in line[%d] while loading a eventcampaign data",
          //     hostName.c_str(),
          //     m_lexer.line + 1)
          L2_LOG_ERROR(
              Log,
              "eventcampaign.txt line %lld: undefined item '%s' in the %s list; this entry is omitted and loading "
              "continues.",
              static_cast<long long>(m_lexer.line) + 1, hostName.c_str(),
              ("elite-reward"));
      }
    ;

teleport_pos_def
    : TELEPORT_POS '=' '{' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          m_pStepMultiInfo->SetStartPosition(
              grammar::SemanticInt32($4), grammar::SemanticInt32($6), grammar::SemanticInt32($8)
          );
      }
    | %empty
    ;

n45
    : n46
    | %empty
    ;

n46
    : n46 ';' name_def
    | name_def
    ;

name_def
    : NAME
      {
          const auto targetStringAt = [](const grammar::SemanticBits slot) {
              const char16_t* const value = SemanticText(slot);
              return std::u16string(value != nullptr ? value : u"");
          };
          stringList.push_back(targetStringAt($1));
      }
    ;

n5
    : %empty
    ;

event_campaign_list
    : %empty
    ;

n1
    : %empty
    ;

n5
    : %empty
    ;

event_campaign_list
    : %empty
    ;

n1
    : %empty
      { m_pEventCampaign = new CEventCampaign(grammar::SemanticInt32($0)); }
    ;

// The original tables have rules 70, 71, 72, 73, 75, 76, 77, 79, 80, 81, 82,
// 83, 84, 85, 86, 87, 88, 89, 91, 92, 93, 95, 96, 97, 99, 100, 101, 102, 103,
// 104, 105, 106, 107, 108, 109, 111 and 112 that no parser state reduces.
// They are left out.

%%
