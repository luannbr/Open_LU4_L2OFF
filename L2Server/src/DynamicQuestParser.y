// DynamicQuest.txt grammar (original L2Server/dynamicquest_parser.y).
// Bison generates DynamicQuestParser.cpp and DynamicQuestParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/DynamicQuestParser.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/Area.h"
#include "L2Server/src/CConfig.h"
#include "L2Server/src/Door.h"
#include "L2Server/src/Item.h"
#include "L2Server/src/ObjectDB.h"
#include "L2Server/src/Residence.h"

#include "Shared/src/Log.h"
#include "Shared/src/Territory.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "DynamicQuestParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForDynamicQuest::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"DynamicQuestParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

std::int32_t SignedInt32(std::uint32_t bits) {
    std::int32_t result = 0;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

const char16_t* SemanticText(grammar::SemanticBits value) {
    return grammar::SemanticString(value);
}

char16_t FoldAsciiCase(char16_t value) {
    return value >= u'A' && value <= u'Z' ? static_cast<char16_t>(value + (u'a' - u'A')) : value;
}

bool EqualsAsciiCaseInsensitive(const char16_t* value, std::u16string_view expected) {
    if (value == nullptr) {
        return false;
    }
    std::size_t index = 0;
    while (value[index] != u'\0' && index < expected.size()) {
        if (FoldAsciiCase(value[index]) != FoldAsciiCase(expected[index])) {
            return false;
        }
        ++index;
    }
    return value[index] == u'\0' && index == expected.size();
}

// void LogInvalidCount(
//     const wchar_t* kind,
//     int value,
//     int line) {
//     // Log.Add(
//     //     LOG_ERROR,
//     //     L"invalid %s count(%d) in line[%d] while loading a dynamicquest data",
//     //     kind,
//     //     value,
//     //     line)
//     L2_LOG_ERROR(Log,
//         L"invalid %s count(%d) in line[%d] while loading a dynamicquest data",
//         kind,
//         value,
//         line)
// }
void LogInvalidCount(const wchar_t* kind, int value, long long line) {
    // Log.Add(
    //     LOG_ERROR,
    //     L"invalid %s count(%d) in line[%d] while loading a dynamicquest data",
    //     kind,
    //     value,
    //     line)
    L2_LOG_WARN(
        Log, "DynamicQuest.txt line %lld: %s count %d exceeds 1000; using 1000 and continuing parsing.", line,
        kind == nullptr ? "<null>" : WideToUtf8(kind).c_str(), value
    );
}

// unsigned int CheckedCount(
//     const wchar_t* kind,
//     int value,
//     int line) {
//     if (value > 1000) {
//         LogInvalidCount(kind, value, line)
//         value = 1000
//     }
//     return static_cast<unsigned int>(value)
// }
unsigned int CheckedCount(const wchar_t* kind, int value, long long line) {
    if (value > 1000) {
        LogInvalidCount(kind, value, line);
        value = 1000;
    }
    return static_cast<unsigned int>(value);
}

} // namespace

CParserForDynamicQuest::CParserForDynamicQuest() :
    m_lexer(),
    m_DynamicQuestType(DynamicQuestType::DYNAMIC_CONTENTS),
    m_pDynamicQuest(nullptr),
    m_pAutoExecutionByTimer(nullptr),
    m_pCampaignData(nullptr),
    m_pCampaign(nullptr),
    m_pPrevCampaign(nullptr),
    m_pNextCampaign(nullptr),
    intList(),
    stringList(),
    m_StartConditionList(),
    m_ItemManiDataList(),
    m_pFeedback(nullptr),
    m_pPage(nullptr),
    m_pTerritory(nullptr),
    m_EnableAdjGoal(true) {
    Init();
}

CParserForDynamicQuest::~CParserForDynamicQuest() = default;

int CParserForDynamicQuest::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}

void CParserForDynamicQuest::Init() {
    m_DynamicQuestType = DynamicQuestType::DYNAMIC_CONTENTS;
    m_pDynamicQuest = nullptr;
    m_pAutoExecutionByTimer = nullptr;
    m_pCampaignData = nullptr;
    m_pCampaign = nullptr;
    m_pPrevCampaign = nullptr;
    m_pNextCampaign = nullptr;
    intList.clear();
    stringList.clear();
    m_StartConditionList.clear();
    m_ItemManiDataList.clear();
    m_pFeedback = nullptr;
    m_pPage = nullptr;
    m_pTerritory = nullptr;
    m_EnableAdjGoal = true;
}

void CParserForDynamicQuest::AddDynamicQuest(DynamicQuestType::Enum type) {
    if (m_pDynamicQuest->m_pExecutionSetting == nullptr) {
        m_pDynamicQuest->m_pExecutionSetting = new DynamicQuestSetting::ManualExecution;
    }
    m_pDynamicQuest->m_StartConditionList.insert(
        m_pDynamicQuest->m_StartConditionList.end(), m_StartConditionList.begin(), m_StartConditionList.end()
    );
    if (!DynamicQuestManager::Inst()->AddDynamicQuest(m_pDynamicQuest, type)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Duplicated dynamic quest id[%d] in line[%d] while loading a dynamicquest data",
        //     m_pDynamicQuest->GetId(),
        //     m_lexer.line + 1)
        // L2_LOG_ERROR(Log,
        //     L"Duplicated dynamic quest id[%d] in line[%d] while loading a dynamicquest data",
        //     m_pDynamicQuest->GetId(),
        //     m_lexer.line + 1)
        L2_LOG_ERROR(
            Log,
            "DynamicQuest.txt line %lld: duplicate dynamic quest ID %d; the existing definition is retained and this "
            "definition is not registered.",
            static_cast<long long>(m_lexer.line) + 1, m_pDynamicQuest->GetId()
        );
    }
    Init();
}
}

%token DEFAULT_SETTING_BEGIN
%token DEFAULT_SETTING_END
%token FHTML_PARAM_SETTING_BEGIN
%token FHTML_PARAM_SETTING_END
%token FEEDBACK_FULL_INVENTORY
%token MSG
%token HTML
%token FHTML
%token QHTML
%token QFHTML
%token DYNAMIC_CONTENTS_SETTING_BEGIN
%token DYNAMIC_CONTENTS_SETTING_END
%token ZONE_QUEST_SETTING_BEGIN
%token ZONE_QUEST_SETTING_END
%token REWARD_PERIOD
%token ELITE_COUNT
%token ELITE_COUNT1
%token ELITE_COUNT2
%token REWARD_COUNT1
%token DYNAMIC_CONTENT_BEGIN
%token DYNAMIC_CONTENT_END
%token ZONE_QUEST_BEGIN
%token ZONE_QUEST_END
%token DYNAMIC_QUEST_ID
%token START_CONDITION_CHECK_BEGIN
%token START_CONDITION_CHECK_END
%token LEVEL
%token CLASS
%token GROUP_TYPE
%token CASTLE_PLEDGE_TYPE
%token AUTO_EXECUTION_BEGIN
%token AUTO_EXECUTION_END
%token WEEKDAY
%token TIME_DICE
%token PERIOD
%token CASTLE_TYPE
%token STEP_LIST_BEGIN
%token STEP_LIST_END
%token TYPE
%token STEP_BEGIN
%token STEP_END
%token STEP_ID
%token DURATION
%token GOAL
%token ENABLE_ADJUSTED_GOAL
%token NPC_MAKER
%token ACCEPT_ITEM
%token REWARD
%token REWARD1
%token REWARD2
%token ELITE_REWARD
%token ELITE_REWARD1
%token ELITE_REWARD2
%token BASIC_REWARD
%token OPEN_DOOR
%token AREA_ON
%token START_HTML
%token CONTEXT_HTML
%token REWARD_HTML
%token ELITE_REWARD_HTML
%token REWARD_RECEIVED_HTML
%token FAILED_HTML
%token RANGE
%token ADJUSTED_ITEM_BEGIN
%token ADJUSTED_ITEM_END
%token EXCLUDED_ID
%token NAME
%token ONETIME_CYCLE
%token INTEGER
%token FLOAT_NUMBER
// The original grammar has 3 reduce/reduce conflicts. Bison
// resolves them as the original tables did. A shift wins over a reduce,
// and the earlier rule wins over a later rule.
%start n1

%%

n1
    : default_setting n20
    ;

default_setting
    : DEFAULT_SETTING_BEGIN n3 DEFAULT_SETTING_END
    ;

n3
    : n3 n4
    | %empty
    ;

n4
    : fhtml_param_setting
    | feedback_full_inventory_def
    | dynamic_contents_setting
    | zone_quest_setting
    | adjusted_item
    ;

fhtml_param_setting
    : FHTML_PARAM_SETTING_BEGIN DYNAMIC_QUEST_ID '=' ONETIME_CYCLE STEP_ID '=' ONETIME_CYCLE FHTML_PARAM_SETTING_END
      {
          DynamicQuestManager* const manager = DynamicQuestManager::Inst();
          manager->FHTMLParam.dQuestId = std::u16string(SemanticText($4) != nullptr ? SemanticText($4) : u"");
          manager->FHTMLParam.campaignId = std::u16string(SemanticText($7) != nullptr ? SemanticText($7) : u"");
      }
    ;

feedback_full_inventory_def
    : FEEDBACK_FULL_INVENTORY '=' n93
      {
          DynamicQuestManager* const manager = DynamicQuestManager::Inst();
          manager->m_pDefaultFeedback[0] = m_pFeedback;
          m_pFeedback = nullptr;
      }
    ;

dynamic_contents_setting
    : DYNAMIC_CONTENTS_SETTING_BEGIN
      { m_DynamicQuestType = DynamicQuestType::DYNAMIC_CONTENTS; }
      reward_period_def_list DYNAMIC_CONTENTS_SETTING_END
    ;

zone_quest_setting
    : ZONE_QUEST_SETTING_BEGIN
      { m_DynamicQuestType = DynamicQuestType::ZONE_QUEST; }
      reward_period_def_list ZONE_QUEST_SETTING_END
    ;

adjusted_item
    : ADJUSTED_ITEM_BEGIN n14 ADJUSTED_ITEM_END
    ;

reward_period_def_list
    : reward_period_def_list reward_period_def
    | reward_period_def
    ;

reward_period_def
    : REWARD_PERIOD '=' INTEGER
      {
          DynamicQuestManager* const manager = DynamicQuestManager::Inst();
          manager->DefaultSetting[m_DynamicQuestType].REWARD_PERIOD =
              static_cast<unsigned int>(grammar::SemanticInt32($3)) * 60U;
      }
    | ELITE_COUNT1 '=' INTEGER
      {
          DynamicQuestManager* const manager = DynamicQuestManager::Inst();
          manager->DefaultSetting[m_DynamicQuestType].ELITE_COUNT1 =
              // CheckedCount(L"elite1", grammar::SemanticInt32($3), m_lexer.line + 1)
              CheckedCount(L"elite1", grammar::SemanticInt32($3), static_cast<long long>(m_lexer.line) + 1);
      }
    | ELITE_COUNT2 '=' INTEGER
      {
          DynamicQuestManager* const manager = DynamicQuestManager::Inst();
          manager->DefaultSetting[m_DynamicQuestType].ELITE_COUNT2 =
              // CheckedCount(L"elite2", grammar::SemanticInt32($3), m_lexer.line + 1)
              CheckedCount(L"elite2", grammar::SemanticInt32($3), static_cast<long long>(m_lexer.line) + 1);
      }
    | REWARD_COUNT1 '=' INTEGER
      {
          DynamicQuestManager* const manager = DynamicQuestManager::Inst();
          manager->DefaultSetting[m_DynamicQuestType].REWARD_COUNT1 =
              // CheckedCount(L"reward1", grammar::SemanticInt32($3), m_lexer.line + 1)
              CheckedCount(L"reward1", grammar::SemanticInt32($3), static_cast<long long>(m_lexer.line) + 1);
      }
    | ELITE_COUNT '=' INTEGER
      { }
    ;

n14
    : goal_def reward_def excluded_id_def
    | goal_def excluded_id_def
    | reward_def excluded_id_def
    ;

goal_def
    : GOAL '=' '{' n16 '}'
    ;

n16
    : n16 ';' goal_def
    | goal_def
    ;

goal_def
    : '{' INTEGER ';' INTEGER '}'
      {
          DynamicQuestManager* const manager = DynamicQuestManager::Inst();
          manager->m_AdjustedGoalRateMap[grammar::SemanticInt32($2)] = grammar::SemanticInt32($4);
      }
    ;

reward_def
    : REWARD '=' '{' n18 '}'
    ;

n18
    : n18 ';' reward_def
    | reward_def
    ;

reward_def
    : '{' INTEGER ';' INTEGER '}'
      {
          DynamicQuestManager* const manager = DynamicQuestManager::Inst();
          manager->m_AdjustedRewardRateMap[grammar::SemanticInt32($2)] = grammar::SemanticInt32($4);
      }
    ;

excluded_id_def
    : EXCLUDED_ID '=' '{' n101 '}'
      {
          DynamicQuestManager* const manager = DynamicQuestManager::Inst();
          for (const int questId : intList) {
              manager->m_DynamicQuestIdExcludedFromAdjusting.insert(questId);
          }
          intList.clear();
      }
    | %empty
    ;

n20
    : n20 n21
    | %empty
    ;

n21
    : dynamic_content
    | zone_quest
    ;

dynamic_content
    : DYNAMIC_CONTENT_BEGIN DYNAMIC_QUEST_ID '=' INTEGER
      { m_pDynamicQuest = new DynamicContent(grammar::SemanticInt32($4)); }
      n26 n74 step_list DYNAMIC_CONTENT_END
      { AddDynamicQuest(DynamicQuestType::DYNAMIC_CONTENTS); }
    ;

zone_quest
    : ZONE_QUEST_BEGIN DYNAMIC_QUEST_ID '=' INTEGER
      { m_pDynamicQuest = new ZoneQuest(grammar::SemanticInt32($4)); }
      n26 n86 step_list ZONE_QUEST_END
      { AddDynamicQuest(DynamicQuestType::ZONE_QUEST); }
    ;

n26
    : n27
    ;

n27
    : n28
    | %empty
    ;

n28
    : n28 n29
    | n29
    ;

n29
    : elite_count1_def
    | reward_period_def_2
    | start_condition_check
    ;

elite_count1_def
    : ELITE_COUNT1 '=' INTEGER
      {
          m_pDynamicQuest->m_Setting.ELITE_COUNT1 =
              // CheckedCount(L"elite1", grammar::SemanticInt32($3), m_lexer.line + 1)
              CheckedCount(L"elite1", grammar::SemanticInt32($3), static_cast<long long>(m_lexer.line) + 1);
      }
    | ELITE_COUNT2 '=' INTEGER
      {
          m_pDynamicQuest->m_Setting.ELITE_COUNT2 =
              // CheckedCount(L"elite2", grammar::SemanticInt32($3), m_lexer.line + 1)
              CheckedCount(L"elite2", grammar::SemanticInt32($3), static_cast<long long>(m_lexer.line) + 1);
      }
    | REWARD_COUNT1 '=' INTEGER
      {
          m_pDynamicQuest->m_Setting.REWARD_COUNT1 =
              // CheckedCount(L"reward1", grammar::SemanticInt32($3), m_lexer.line + 1)
              CheckedCount(L"reward1", grammar::SemanticInt32($3), static_cast<long long>(m_lexer.line) + 1);
      }
    | ELITE_COUNT '=' INTEGER
      { }
    ;

reward_period_def_2
    : REWARD_PERIOD '=' INTEGER
      {
          m_pDynamicQuest->m_Setting.REWARD_PERIOD = static_cast<unsigned int>(grammar::SemanticInt32($3)) * 60U;
      }
    ;

start_condition_check
    : START_CONDITION_CHECK_BEGIN n33 START_CONDITION_CHECK_END
    ;

n33
    : n33 n34
    | n34
    ;

n34
    : level_def
    | class_def
    | group_type_def
    | type_def
    | castle_pledge_type_def
    ;

level_def
    : LEVEL '=' '~' INTEGER
      {
          int maximum = grammar::SemanticInt32($4);
          if (maximum > 120) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"invalid max level(%d) in line[%d] while loading a dynamicquest data",
              //     maximum,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"invalid max level(%d) in line[%d] while loading a dynamicquest data",
              //     maximum,
              //     m_lexer.line + 1)
              L2_LOG_WARN(
                  Log,
                  "DynamicQuest.txt line %lld, quest %d: maximum start level %d exceeds 120; using 120 and continuing "
                  "parsing.",
                  static_cast<long long>(m_lexer.line) + 1, m_pDynamicQuest != nullptr ? m_pDynamicQuest->GetId() : 0,
                  maximum
              );
              maximum = 120;
          }
          m_StartConditionList.push_back(new QuestCondition_Level(1, maximum));
      }
    | LEVEL '=' INTEGER '~' INTEGER
      {
          int minimum = grammar::SemanticInt32($3);
          if (minimum < 1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"invalid min level(%d) in line[%d] while loading a dynamicquest data",
              //     minimum,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"invalid min level(%d) in line[%d] while loading a dynamicquest data",
              //     minimum,
              //     m_lexer.line + 1)
              L2_LOG_WARN(
                  Log,
                  "DynamicQuest.txt line %lld, quest %d: minimum start level %d is below 1; using 1 and continuing "
                  "parsing.",
                  static_cast<long long>(m_lexer.line) + 1, m_pDynamicQuest != nullptr ? m_pDynamicQuest->GetId() : 0,
                  minimum
              );
              minimum = 1;
          }
          const int maximum = grammar::SemanticInt32($5);
          if (maximum > 120) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"invalid max level(%d) in line[%d] while loading a dynamicquest data",
              //     maximum,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"invalid max level(%d) in line[%d] while loading a dynamicquest data",
              //     maximum,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "DynamicQuest.txt line %lld, quest %d: start-level range %d..%d has a maximum above 120; this range is "
                  "retained without clamping the maximum.",
                  static_cast<long long>(m_lexer.line) + 1, m_pDynamicQuest != nullptr ? m_pDynamicQuest->GetId() : 0,
                  minimum, maximum
              );
          }
          m_StartConditionList.push_back(new QuestCondition_Level(minimum, maximum));
      }
    | LEVEL '=' INTEGER '~'
      {
          int minimum = grammar::SemanticInt32($3);
          if (minimum < 1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"invalid min level(%d) in line[%d] while loading a dynamicquest data",
              //     minimum,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"invalid min level(%d) in line[%d] while loading a dynamicquest data",
              //     minimum,
              //     m_lexer.line + 1)
              L2_LOG_WARN(
                  Log,
                  "DynamicQuest.txt line %lld, quest %d: minimum start level %d is below 1; using 1 and continuing "
                  "parsing.",
                  static_cast<long long>(m_lexer.line) + 1, m_pDynamicQuest != nullptr ? m_pDynamicQuest->GetId() : 0,
                  minimum
              );
              minimum = 1;
          }
          m_StartConditionList.push_back(new QuestCondition_Level(minimum, 120));
      }
    ;

class_def
    : CLASS '=' '{' n101 '}'
      {
          auto* const condition = new QuestCondition_Class;
          for (const int classId : intList) {
              condition->m_ClassList.insert(classId);
          }
          intList.clear();
          m_StartConditionList.push_back(condition);
      }
    ;

group_type_def
    : GROUP_TYPE '=' ONETIME_CYCLE
      {
          if (EqualsAsciiCaseInsensitive(SemanticText($3), u"party")) {
              m_StartConditionList.push_back(new QuestCondition_Party);
          }
      }
    ;

type_def
    : TYPE '=' ONETIME_CYCLE
      {
          if (EqualsAsciiCaseInsensitive(SemanticText($3), u"daily")) {
              m_StartConditionList.push_back(new QuestCondition_DailyQuest);
              m_pDynamicQuest->m_QuestType = DynamicQuestType::DAILY_QUEST;
          } else {
              m_pDynamicQuest->m_QuestType = DynamicQuestType::NORMAL_QUEST;
          }
      }
    ;

castle_pledge_type_def
    : CASTLE_PLEDGE_TYPE '=' '{' n101 '}'
      {
          auto* const condition = new QuestCondition_Pledge;
          for (const int pledgeType : intList) {
              condition->m_PledgeTypeList.insert(pledgeType);
          }
          intList.clear();
          m_StartConditionList.push_back(condition);
      }
    ;

step_list
    : STEP_LIST_BEGIN TYPE '=' ONETIME_CYCLE
      {
          const char16_t* const type = SemanticText($4);
          if (EqualsAsciiCaseInsensitive(type, u"onetime")) {
              m_pCampaignData = new CCampaignDataOneTime;
          } else if (EqualsAsciiCaseInsensitive(type, u"cycle")) {
              m_pCampaignData = new CCampaignDataCycle;
          } else {
              // const std::wstring hostType = target_utf16::ToHostCodeUnits(type)
              const std::string hostType = Utf16ToUtf8(type);
              // Log.Add(
              //     LOG_ERROR,
              //     L"invalid campaign type[%s] in line[%d] while loading a dynamicquest data",
              //     hostType.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"invalid campaign type[%s] in line[%d] while loading a dynamicquest data",
              //     hostType.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_WARN(
                  Log,
                  "DynamicQuest.txt line %lld, quest %d: unknown campaign type '%s'; expected onetime or cycle, using "
                  "onetime.",
                  static_cast<long long>(m_lexer.line) + 1, m_pDynamicQuest != nullptr ? m_pDynamicQuest->GetId() : 0,
                  type == nullptr ? "<null>" : hostType.c_str()
              );
              m_pCampaignData = new CCampaignDataOneTime;
          }
      }
      step_list_2 STEP_LIST_END
      {
          m_pDynamicQuest->AddCampaignData(m_pCampaignData);
          m_pCampaignData = nullptr;
      }
    ;

step_list_2
    : step_list_2 step
    | step
    ;

step
    : STEP_BEGIN step_id_def n45 STEP_END
      {
          m_pCampaign->m_StartConditionList.insert(
              m_pCampaign->m_StartConditionList.end(), m_StartConditionList.begin(), m_StartConditionList.end()
          );
          if (!m_pCampaignData->AddCampaign(m_pCampaign)) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"duplicated campaign id[%d] in line[%d] while loading a dynamicquest data",
              //     m_pCampaign->GetId(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"duplicated campaign id[%d] in line[%d] while loading a dynamicquest data",
              //     m_pCampaign->GetId(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "DynamicQuest.txt line %lld, quest %d: duplicate campaign ID %d; this campaign is not linked into the "
                  "campaign list.",
                  static_cast<long long>(m_lexer.line) + 1, m_pDynamicQuest != nullptr ? m_pDynamicQuest->GetId() : 0,
                  m_pCampaign->GetId()
              );
          }
          m_pCampaign = nullptr;
          m_EnableAdjGoal = true;
      }
    ;

step_id_def
    : STEP_ID '=' INTEGER DURATION '=' INTEGER
      {
          m_pCampaign = new CCampaign(
              grammar::SemanticInt32($3), SignedInt32(static_cast<std::uint32_t>(grammar::SemanticInt32($6)) * 60U)
          );
      }
    ;

n45
    : enable_adjusted_goal_def npc_maker_def n52 accept_item_def n60 n64 basic_reward_def open_door_def area_on_def
    ;

enable_adjusted_goal_def
    : ENABLE_ADJUSTED_GOAL '=' INTEGER
      { m_EnableAdjGoal = $3 != 0; }
      GOAL '=' '{' n48 '}'
    | GOAL '=' '{' n48 '}'
    ;

n48
    : n49
    ;

n49
    : n49 ';' n50
    | n50
    ;

n50
    : '{' INTEGER ';' INTEGER '}'
      {
          const int signedGoalId = grammar::SemanticInt32($2);
          if (signedGoalId < 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"invalid goal ID[%d] of campaign[%d], dynamic quest[%d] in line[%d] while loading a dynamicquest
              //     data", signedGoalId, m_pCampaign->GetId(), m_pDynamicQuest->GetId(), m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"invalid goal ID[%d] of campaign[%d], dynamic quest[%d] in line[%d] while loading a dynamicquest
              //     data", signedGoalId, m_pCampaign->GetId(), m_pDynamicQuest->GetId(), m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "DynamicQuest.txt line %lld, quest %d, campaign %d: negative goal ID %d; insertion still uses unsigned "
                  "key %u.",
                  static_cast<long long>(m_lexer.line) + 1, m_pDynamicQuest->GetId(), m_pCampaign->GetId(), signedGoalId,
                  static_cast<unsigned int>(signedGoalId)
              );
          }
          std::uint32_t amount = static_cast<std::uint32_t>(grammar::SemanticInt32($4));
          if (m_EnableAdjGoal) {
              amount = static_cast<std::uint32_t>(amount * g_DQuestGoalAdjRate) / 100U;
          }
          const auto inserted =
              m_pCampaign->m_GoalMap.emplace(static_cast<unsigned int>(signedGoalId), SignedInt32(amount));
          if (!inserted.second) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Duplicated goal ID[%d] of campaign[%d], dynamic quest[%d] in line[%d] while loading a dynamicquest
              //     data", signedGoalId, m_pCampaign->GetId(), m_pDynamicQuest->GetId(), m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Duplicated goal ID[%d] of campaign[%d], dynamic quest[%d] in line[%d] while loading a dynamicquest
              //     data", signedGoalId, m_pCampaign->GetId(), m_pDynamicQuest->GetId(), m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "DynamicQuest.txt line %lld, quest %d, campaign %d: duplicate goal ID %d (unsigned key %u); keeping "
                  "amount %d and ignoring amount %d.",
                  static_cast<long long>(m_lexer.line) + 1, m_pDynamicQuest->GetId(), m_pCampaign->GetId(), signedGoalId,
                  static_cast<unsigned int>(signedGoalId), inserted.first->second, SignedInt32(amount)
              );
          } else {
              ++m_pCampaign->m_GoalCount;
          }
      }
    ;

npc_maker_def
    : NPC_MAKER '=' '{' n104 '}'
      {
          m_pCampaign->m_MakerList.insert(m_pCampaign->m_MakerList.end(), stringList.begin(), stringList.end());
          stringList.clear();
      }
    ;

n52
    : start_html_def context_html_def reward_html_def elite_reward_html_def reward_received_html_def failed_html_def
    ;

start_html_def
    : START_HTML '=' ONETIME_CYCLE
      {
          DynamicQuestManager* const manager = DynamicQuestManager::Inst();
          const auto makeCampaignPage = [this, manager](const char16_t* pageName) {
              auto* const page = new QuestPage_FHTML;
              page->m_QuestPageName = std::u16string(pageName != nullptr ? pageName : u"");
              page->m_Param[manager->FHTMLParam.dQuestId] = IntToXStr(m_pDynamicQuest->GetId());
              page->m_Param[manager->FHTMLParam.campaignId] = IntToXStr(m_pCampaign->GetId());
              return new QuestOperation_ShowQuestPage(page);
          };
          m_pCampaign->m_StartOperationList.push_back(makeCampaignPage(SemanticText($3)));
      }
    ;

context_html_def
    : CONTEXT_HTML '=' ONETIME_CYCLE
      {
          DynamicQuestManager* const manager = DynamicQuestManager::Inst();
          const auto makeCampaignPage = [this, manager](const char16_t* pageName) {
              auto* const page = new QuestPage_FHTML;
              page->m_QuestPageName = std::u16string(pageName != nullptr ? pageName : u"");
              page->m_Param[manager->FHTMLParam.dQuestId] = IntToXStr(m_pDynamicQuest->GetId());
              page->m_Param[manager->FHTMLParam.campaignId] = IntToXStr(m_pCampaign->GetId());
              return new QuestOperation_ShowQuestPage(page);
          };
          QuestOperation* const operation = makeCampaignPage(SemanticText($3));
          m_pCampaign->m_AcceptOperationList.push_back(operation);
          m_pCampaign->m_InquiryOperation.push_back(operation);
      }
    ;

reward_html_def
    : REWARD_HTML '=' ONETIME_CYCLE
      {
          DynamicQuestManager* const manager = DynamicQuestManager::Inst();
          const auto makeCampaignPage = [this, manager](const char16_t* pageName) {
              auto* const page = new QuestPage_FHTML;
              page->m_QuestPageName = std::u16string(pageName != nullptr ? pageName : u"");
              page->m_Param[manager->FHTMLParam.dQuestId] = IntToXStr(m_pDynamicQuest->GetId());
              page->m_Param[manager->FHTMLParam.campaignId] = IntToXStr(m_pCampaign->GetId());
              return new QuestOperation_ShowQuestPage(page);
          };
          m_pCampaign->m_SuggestRewardOperation.push_back(makeCampaignPage(SemanticText($3)));
      }
    ;

elite_reward_html_def
    : ELITE_REWARD_HTML '=' ONETIME_CYCLE
      {
          DynamicQuestManager* const manager = DynamicQuestManager::Inst();
          const auto makeCampaignPage = [this, manager](const char16_t* pageName) {
              auto* const page = new QuestPage_FHTML;
              page->m_QuestPageName = std::u16string(pageName != nullptr ? pageName : u"");
              page->m_Param[manager->FHTMLParam.dQuestId] = IntToXStr(m_pDynamicQuest->GetId());
              page->m_Param[manager->FHTMLParam.campaignId] = IntToXStr(m_pCampaign->GetId());
              return new QuestOperation_ShowQuestPage(page);
          };
          m_pCampaign->m_SuggestRewardOperation_Elite.push_back(makeCampaignPage(SemanticText($3)));
      }
    ;

reward_received_html_def
    : REWARD_RECEIVED_HTML '=' ONETIME_CYCLE
      {
          DynamicQuestManager* const manager = DynamicQuestManager::Inst();
          const auto makeCampaignPage = [this, manager](const char16_t* pageName) {
              auto* const page = new QuestPage_FHTML;
              page->m_QuestPageName = std::u16string(pageName != nullptr ? pageName : u"");
              page->m_Param[manager->FHTMLParam.dQuestId] = IntToXStr(m_pDynamicQuest->GetId());
              page->m_Param[manager->FHTMLParam.campaignId] = IntToXStr(m_pCampaign->GetId());
              return new QuestOperation_ShowQuestPage(page);
          };
          m_pCampaign->m_SuggestRewardOperation_Received.push_back(makeCampaignPage(SemanticText($3)));
      }
    ;

failed_html_def
    : FAILED_HTML '=' ONETIME_CYCLE
      {
          DynamicQuestManager* const manager = DynamicQuestManager::Inst();
          const auto makeCampaignPage = [this, manager](const char16_t* pageName) {
              auto* const page = new QuestPage_FHTML;
              page->m_QuestPageName = std::u16string(pageName != nullptr ? pageName : u"");
              page->m_Param[manager->FHTMLParam.dQuestId] = IntToXStr(m_pDynamicQuest->GetId());
              page->m_Param[manager->FHTMLParam.campaignId] = IntToXStr(m_pCampaign->GetId());
              return new QuestOperation_ShowQuestPage(page);
          };
          m_pCampaign->m_FailedOperation.push_back(makeCampaignPage(SemanticText($3)));
      }
    ;

accept_item_def
    : ACCEPT_ITEM '=' '{' n69 '}'
      {
          const auto appendItems = [this](QuestOperation_Item* operation) {
              operation->m_ItemList.insert(
                  operation->m_ItemList.end(), m_ItemManiDataList.begin(), m_ItemManiDataList.end()
              );
          };
          const auto fillCondition = [this](QuestCondition_FullInven* condition) {
              for (const auto& item : m_ItemManiDataList) {
                  condition->m_CheckAddableItem.Push(static_cast<int>(item.itemClassId), item.itemCount);
              }
          };
          DynamicQuestManager* const manager = DynamicQuestManager::Inst();
          if (!m_ItemManiDataList.empty()) {
              appendItems(m_pCampaign->m_pAcceptOperation_Item);
              auto* const condition = new QuestCondition_FullInven(manager->m_pDefaultFeedback[0]);
              fillCondition(condition);
              m_pCampaign->m_AcceptConditionList.push_back(condition);
          }
          m_ItemManiDataList.clear();
      }
    ;

n60
    : reward_def_2
    | reward1_def reward2_def
    ;

reward_def_2
    : REWARD '=' '{' n69 '}'
      { }
    ;

reward1_def
    : REWARD1 '=' '{' n69 '}'
      {
          const auto appendItems = [this](QuestOperation_Item* operation) {
              operation->m_ItemList.insert(
                  operation->m_ItemList.end(), m_ItemManiDataList.begin(), m_ItemManiDataList.end()
              );
          };
          const auto fillCondition = [this](QuestCondition_FullInven* condition) {
              for (const auto& item : m_ItemManiDataList) {
                  condition->m_CheckAddableItem.Push(static_cast<int>(item.itemClassId), item.itemCount);
              }
          };
          DynamicQuestManager* const manager = DynamicQuestManager::Inst();
          if (!m_ItemManiDataList.empty()) {
              appendItems(m_pCampaign->m_pFinishOperation_Item1);
              auto* const condition = new QuestCondition_FullInven(manager->m_pDefaultFeedback[0]);
              fillCondition(condition);
              m_pCampaign->m_FinishConditionList.push_back(condition);
          }
          m_ItemManiDataList.clear();
      }
    ;

reward2_def
    : REWARD2 '=' '{' n69 '}'
      {
          const auto appendItems = [this](QuestOperation_Item* operation) {
              operation->m_ItemList.insert(
                  operation->m_ItemList.end(), m_ItemManiDataList.begin(), m_ItemManiDataList.end()
              );
          };
          const auto fillCondition = [this](QuestCondition_FullInven* condition) {
              for (const auto& item : m_ItemManiDataList) {
                  condition->m_CheckAddableItem.Push(static_cast<int>(item.itemClassId), item.itemCount);
              }
          };
          DynamicQuestManager* const manager = DynamicQuestManager::Inst();
          if (!m_ItemManiDataList.empty()) {
              appendItems(m_pCampaign->m_pFinishOperation_Item2);
              auto* const condition = new QuestCondition_FullInven(manager->m_pDefaultFeedback[0]);
              fillCondition(condition);
              m_pCampaign->m_FinishConditionList.push_back(condition);
          }
          m_ItemManiDataList.clear();
      }
    ;

n64
    : elite_reward_def
    | elite_reward1_def elite_reward2_def
    ;

elite_reward_def
    : ELITE_REWARD '=' '{' n69 '}'
      { }
    ;

elite_reward1_def
    : ELITE_REWARD1 '=' '{' n69 '}'
      {
          const auto appendItems = [this](QuestOperation_Item* operation) {
              operation->m_ItemList.insert(
                  operation->m_ItemList.end(), m_ItemManiDataList.begin(), m_ItemManiDataList.end()
              );
          };
          const auto fillCondition = [this](QuestCondition_FullInven* condition) {
              for (const auto& item : m_ItemManiDataList) {
                  condition->m_CheckAddableItem.Push(static_cast<int>(item.itemClassId), item.itemCount);
              }
          };
          DynamicQuestManager* const manager = DynamicQuestManager::Inst();
          if (!m_ItemManiDataList.empty()) {
              appendItems(m_pCampaign->m_pFinishOperation_Item_Elite1);
              if (m_pCampaign->m_pConditionInven_Elite1 == nullptr) {
                  m_pCampaign->m_pConditionInven_Elite1 = new QuestCondition_FullInven(manager->m_pDefaultFeedback[0]);
              }
              fillCondition(m_pCampaign->m_pConditionInven_Elite1);
          }
          m_ItemManiDataList.clear();
      }
    ;

elite_reward2_def
    : ELITE_REWARD2 '=' '{' n69 '}'
      {
          const auto appendItems = [this](QuestOperation_Item* operation) {
              operation->m_ItemList.insert(
                  operation->m_ItemList.end(), m_ItemManiDataList.begin(), m_ItemManiDataList.end()
              );
          };
          const auto fillCondition = [this](QuestCondition_FullInven* condition) {
              for (const auto& item : m_ItemManiDataList) {
                  condition->m_CheckAddableItem.Push(static_cast<int>(item.itemClassId), item.itemCount);
              }
          };
          DynamicQuestManager* const manager = DynamicQuestManager::Inst();
          if (!m_ItemManiDataList.empty()) {
              appendItems(m_pCampaign->m_pFinishOperation_Item_Elite2);
              if (m_pCampaign->m_pConditionInven_Elite2 == nullptr) {
                  m_pCampaign->m_pConditionInven_Elite2 = new QuestCondition_FullInven(manager->m_pDefaultFeedback[0]);
              }
              fillCondition(m_pCampaign->m_pConditionInven_Elite2);
          }
          m_ItemManiDataList.clear();
      }
    ;

basic_reward_def
    : BASIC_REWARD '=' '{' n69 '}'
      {
          const auto appendItems = [this](QuestOperation_Item* operation) {
              operation->m_ItemList.insert(
                  operation->m_ItemList.end(), m_ItemManiDataList.begin(), m_ItemManiDataList.end()
              );
          };
          const auto fillCondition = [this](QuestCondition_FullInven* condition) {
              for (const auto& item : m_ItemManiDataList) {
                  condition->m_CheckAddableItem.Push(static_cast<int>(item.itemClassId), item.itemCount);
              }
          };
          DynamicQuestManager* const manager = DynamicQuestManager::Inst();
          if (!m_ItemManiDataList.empty()) {
              appendItems(m_pCampaign->m_pFinishOperation_Item_Basic);
              if (m_pCampaign->m_pConditionInven_Basic == nullptr) {
                  m_pCampaign->m_pConditionInven_Basic = new QuestCondition_FullInven(manager->m_pDefaultFeedback[0]);
              }
              fillCondition(m_pCampaign->m_pConditionInven_Basic);
          }
          m_ItemManiDataList.clear();
      }
    | %empty
    ;

n69
    : n70
    | %empty
    ;

n70
    : n70 ';' n71
    | n71
    ;

n71
    : '{' NAME ';' INTEGER ';' INTEGER '}'
      {
          const char16_t* const itemName = SemanticText($2);
          CObject* const object = g_ObjectDB.GetObjectByName(itemName);
          if (object != nullptr && object->IsItem()) {
              CItem* const item = object->CastItem();
              if (item != nullptr) {
                  m_ItemManiDataList.push_back(
                      QuestOperation_Item::ItemDataType{static_cast<unsigned int>(item->GetClassId()),
                          grammar::SemanticInt64($4), grammar::SemanticInt32($6)}
                  );
                  break;
              }
          }
          // const std::wstring hostName = target_utf16::ToHostCodeUnits(itemName)
          const std::string hostName = Utf16ToUtf8(itemName);
          // Log.Add(
          //     LOG_ERROR,
          //     L"Undefined item[%s] in line[%d] while loading a dynamicquest data",
          //     hostName.c_str(),
          //     m_lexer.line + 1)
          // L2_LOG_ERROR(Log,
          //     L"Undefined item[%s] in line[%d] while loading a dynamicquest data",
          //     hostName.c_str(),
          //     m_lexer.line + 1)
          L2_LOG_ERROR(
              Log,
              "DynamicQuest.txt line %lld, quest %d, campaign %d: no usable item prototype for '%s'; the "
              "item-manipulation entry for %lld items is omitted.",
              static_cast<long long>(m_lexer.line) + 1, m_pDynamicQuest != nullptr ? m_pDynamicQuest->GetId() : 0,
              m_pCampaign != nullptr ? m_pCampaign->GetId() : 0,
              itemName == nullptr ? "<null>" : hostName.c_str(), static_cast<long long>(grammar::SemanticInt64($4))
          );
      }
    | '{' NAME ';' INTEGER '}'
      {
          const char16_t* const itemName = SemanticText($2);
          CObject* const object = g_ObjectDB.GetObjectByName(itemName);
          if (object != nullptr && object->IsItem()) {
              CItem* const item = object->CastItem();
              if (item != nullptr) {
                  m_ItemManiDataList.push_back(
                      QuestOperation_Item::ItemDataType{static_cast<unsigned int>(item->GetClassId()),
                          grammar::SemanticInt64($4), 0}
                  );
                  break;
              }
          }
          // const std::wstring hostName = target_utf16::ToHostCodeUnits(itemName)
          const std::string hostName = Utf16ToUtf8(itemName);
          // Log.Add(
          //     LOG_ERROR,
          //     L"Undefined item[%s] in line[%d] while loading a dynamicquest data",
          //     hostName.c_str(),
          //     m_lexer.line + 1)
          // L2_LOG_ERROR(Log,
          //     L"Undefined item[%s] in line[%d] while loading a dynamicquest data",
          //     hostName.c_str(),
          //     m_lexer.line + 1)
          L2_LOG_ERROR(
              Log,
              "DynamicQuest.txt line %lld, quest %d, campaign %d: no usable item prototype for '%s'; the "
              "item-manipulation entry for %lld items is omitted.",
              static_cast<long long>(m_lexer.line) + 1, m_pDynamicQuest != nullptr ? m_pDynamicQuest->GetId() : 0,
              m_pCampaign != nullptr ? m_pCampaign->GetId() : 0,
              itemName == nullptr ? "<null>" : hostName.c_str(), static_cast<long long>(grammar::SemanticInt64($4))
          );
      }
    ;

open_door_def
    : OPEN_DOOR '=' '{' n104 '}'
      {
          for (const std::u16string& name : stringList) {
              CDoor* const door = g_DoorDB.GetDoor(name.c_str());
              if (door != nullptr) {
                  m_pCampaign->m_OpenDoorList.push_back(door);
              } else {
                  L2_LOG_ERROR(
                      Log,
                      "DynamicQuest.txt line %lld, quest %d, campaign %d: unknown door '%s'; this reference is omitted "
                      "from the campaign door list.",
                      static_cast<long long>(m_lexer.line) + 1, m_pDynamicQuest != nullptr ? m_pDynamicQuest->GetId() : 0,
                      m_pCampaign != nullptr ? m_pCampaign->GetId() : 0, Utf16ToUtf8(name.c_str()).c_str()
                  );
              }
          }
          stringList.clear();
      }
    ;

area_on_def
    : AREA_ON '=' '{' n104 '}'
      {
          for (const std::u16string& name : stringList) {
              CArea* const area = CAreaDB::inst().FindArea(name.c_str());
              if (area != nullptr) {
                  m_pCampaign->m_AreaOnList.push_back(area);
              } else {
                  L2_LOG_ERROR(
                      Log,
                      "DynamicQuest.txt line %lld, quest %d, campaign %d: unknown area '%s'; this reference is omitted "
                      "from the campaign activation list.",
                      static_cast<long long>(m_lexer.line) + 1, m_pDynamicQuest != nullptr ? m_pDynamicQuest->GetId() : 0,
                      m_pCampaign != nullptr ? m_pCampaign->GetId() : 0, Utf16ToUtf8(name.c_str()).c_str()
                  );
              }
          }
          stringList.clear();
      }
    ;

n74
    : auto_execution
    ;

auto_execution
    : AUTO_EXECUTION_BEGIN
      {
          m_pAutoExecutionByTimer = new DynamicQuestSetting::AutoExecutionByTimer;
      }
      n77 n83 AUTO_EXECUTION_END
      { m_pDynamicQuest->m_pExecutionSetting = m_pAutoExecutionByTimer; }
    ;

n77
    : weekday_def time_dice_def
    ;

weekday_def
    : WEEKDAY '=' '{' n101 '}'
      {
          if (intList.size() > 7U) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"invalid weekday in line[%d] while loading a dynamicquest data",
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"invalid weekday in line[%d] while loading a dynamicquest data",
              //     m_lexer.line + 1)
              L2_LOG_WARN(
                  Log,
                  "DynamicQuest.txt line %lld, quest %d: automatic execution has %llu numeric weekday entries, exceeding "
                  "seven; all entries are still processed and valid day bits are kept.",
                  static_cast<long long>(m_lexer.line) + 1, m_pDynamicQuest != nullptr ? m_pDynamicQuest->GetId() : 0,
                  static_cast<unsigned long long>(intList.size())
              );
          }
          for (const unsigned int day : intList) {
              if (day < 7U) {
                  m_pAutoExecutionByTimer->m_ExecuteDayList |= 1 << day;
              } else {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"invalid weekday in line[%d] while loading a dynamicquest data",
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"invalid weekday in line[%d] while loading a dynamicquest data",
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      "DynamicQuest.txt line %lld, quest %d: invalid automatic-execution weekday %d (unsigned %u); "
                      "expected 0..6 (Sunday..Saturday), this entry is ignored.",
                      static_cast<long long>(m_lexer.line) + 1, m_pDynamicQuest != nullptr ? m_pDynamicQuest->GetId() : 0,
                      SignedInt32(day), day
                  );
              }
          }
      }
    | WEEKDAY '=' '{' n104 '}'
      {
          for (const std::u16string& day : stringList) {
              int bit = 0;
              if (EqualsAsciiCaseInsensitive(day.c_str(), u"mon")) {
                  bit = 0x02;
              } else if (EqualsAsciiCaseInsensitive(day.c_str(), u"tue")) {
                  bit = 0x04;
              } else if (EqualsAsciiCaseInsensitive(day.c_str(), u"wed")) {
                  bit = 0x08;
              } else if (EqualsAsciiCaseInsensitive(day.c_str(), u"thu")) {
                  bit = 0x10;
              } else if (EqualsAsciiCaseInsensitive(day.c_str(), u"fri")) {
                  bit = 0x20;
              } else if (EqualsAsciiCaseInsensitive(day.c_str(), u"sat")) {
                  bit = 0x40;
              } else if (EqualsAsciiCaseInsensitive(day.c_str(), u"sun")) {
                  bit = 0x01;
              } else {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"invalid weekday in line[%d] while loading a dynamicquest data",
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"invalid weekday in line[%d] while loading a dynamicquest data",
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      "DynamicQuest.txt line %lld, quest %d: unknown automatic-execution weekday '%s'; expected sun, mon, "
                      "tue, wed, thu, fri or sat, this entry adds no day bit.",
                      static_cast<long long>(m_lexer.line) + 1, m_pDynamicQuest != nullptr ? m_pDynamicQuest->GetId() : 0,
                      Utf16ToUtf8(day.c_str()).c_str()
                  );
              }
              m_pAutoExecutionByTimer->m_ExecuteDayList |= bit;
          }
          stringList.clear();
      }
    ;

time_dice_def
    : TIME_DICE '=' '{' n80 '}'
    ;

n80
    : n81
    | %empty
    ;

n81
    : n81 ';' n82
    | n82
    ;

n82
    : '{' INTEGER ':' INTEGER ';' integer_def_2 '}'
      {
          m_pAutoExecutionByTimer->m_StartTimeData.push_back(DynamicQuestSetting::StartTimeData(
              grammar::SemanticInt32($2), grammar::SemanticInt32($4), grammar::SemanticDouble($6)
          ));
      }
    ;

n83
    : period_def castle_type_def
    ;

period_def
    : PERIOD '=' ONETIME_CYCLE '~' ONETIME_CYCLE
      {
          auto* const period = new DynamicQuestSetting::ExecutionPeriod;
          TimePeriodInfo::ParseTime(SemanticText($3), period->m_PeriodInfo.m_nStartTime);
          TimePeriodInfo::ParseTime(SemanticText($5), period->m_PeriodInfo.m_nEndTime);
          m_pAutoExecutionByTimer->m_pPeriod = period;
      }
    ;

castle_type_def
    : CASTLE_TYPE '=' NAME ';' INTEGER
      {
          CResidence* const residence = g_ResidenceDB.GetResidence(SemanticText($3));
          const int castleType = grammar::SemanticInt32($5);
          if (residence == nullptr || static_cast<unsigned int>(castleType - 1) > 1U) {
              L2_LOG_ERROR(
                  Log,
                  "DynamicQuest.txt line %lld, quest %d: cannot assign castle execution condition for '%s', type %d "
                  "(expected 1 or 2), residence found: %s; the condition is unchanged.",
                  static_cast<long long>(m_lexer.line) + 1, m_pDynamicQuest != nullptr ? m_pDynamicQuest->GetId() : 0,
                  SemanticText($3) == nullptr ? "<null>" : Utf16ToUtf8(SemanticText($3)).c_str(), castleType,
                  residence != nullptr ? "yes" : "no"
              );
              break;
          }
          const int residenceId = residence->m_data != nullptr ? residence->m_data->m_nResidenceID : 0;
          m_pAutoExecutionByTimer->m_CastleCond.m_nCastleId = residenceId;
          m_pAutoExecutionByTimer->m_CastleCond.m_nCastleType = castleType;
          m_pDynamicQuest->m_nRelatedCastleID = residenceId;
      }
    | %empty
    ;

n86
    : range_def
    ;

range_def
    : RANGE '=' '{' n88 '}'
    ;

n88
    : n88 ';' n89
    | n89
    ;

n89
    : '{'
      { m_pTerritory = new CTerritory; }
      n91 '}'
      { m_pDynamicQuest->AddRange(m_pTerritory); }
    ;

n91
    : n91 ';' n92
    | n92
    ;

n92
    : '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          const std::int64_t bottom = grammar::SemanticInt64($6);
          if (bottom < m_pTerritory->GetBottom()) {
              m_pTerritory->SetBottom(grammar::SemanticInt32($6));
          }
          const std::int64_t top = grammar::SemanticInt64($8);
          if (top > m_pTerritory->GetTop()) {
              m_pTerritory->SetTop(grammar::SemanticInt32($8));
          }
          auto* const point = new FVector(grammar::SemanticInt32($2), grammar::SemanticInt32($4), 0);
          m_pTerritory->AddTerritoryPoint(point);
      }
    ;

n93
    : n94
    | n95
      {
          m_pFeedback = new QuestFeedback_HTML(m_pPage);
          m_pPage = nullptr;
      }
    ;

n94
    : '{' MSG ';' INTEGER '}'
      { m_pFeedback = new QuestFeedback_SystemMsg(grammar::SemanticInt32($4)); }
    ;

n95
    : '{' HTML ';' ONETIME_CYCLE '}'
      {
          auto* const page = new QuestPage_HTML;
          page->m_QuestPageName = std::u16string(SemanticText($4) != nullptr ? SemanticText($4) : u"");
          m_pPage = page;
      }
    | '{' FHTML ';' ONETIME_CYCLE
      {
          auto* const page = new QuestPage_FHTML;
          page->m_QuestPageName = std::u16string(SemanticText($4) != nullptr ? SemanticText($4) : u"");
          m_pPage = page;
      }
      ';' n98 '}'
    | '{' QHTML ';' ONETIME_CYCLE '}'
      {
          auto* const page = new QuestPage_QuestHTML;
          page->m_QuestPageName = std::u16string(SemanticText($4) != nullptr ? SemanticText($4) : u"");
          m_pPage = page;
      }
    | '{' QFHTML ';' ONETIME_CYCLE
      {
          auto* const page = new QuestPage_QuestFHTML;
          page->m_QuestPageName = std::u16string(SemanticText($4) != nullptr ? SemanticText($4) : u"");
          m_pPage = page;
      }
      ';' n98 '}'
    ;

n98
    : '{' n99 '}'
    ;

n99
    : n99 n100
    | n100
    ;

n100
    : '{' ONETIME_CYCLE ';' INTEGER '}'
      {
          static_cast<QuestPage_FHTML*>(m_pPage)
              ->m_Param[std::u16string(SemanticText($2) != nullptr ? SemanticText($2) : u"")] =
              IntToXStr(grammar::SemanticInt32($4));
      }
    | '{' ONETIME_CYCLE ';' ONETIME_CYCLE '}'
      {
          static_cast<QuestPage_FHTML*>(m_pPage)
              ->m_Param[std::u16string(SemanticText($2) != nullptr ? SemanticText($2) : u"")] =
              std::u16string(SemanticText($4) != nullptr ? SemanticText($4) : u"");
      }
    ;

n101
    : n102
    | %empty
    ;

n102
    : n102 ';' integer_def
    | integer_def
    ;

integer_def
    : INTEGER
      { intList.push_back(grammar::SemanticInt32($1)); }
    ;

n104
    : n105
    | %empty
    ;

n105
    : n105 ';' name_def
    | name_def
    ;

name_def
    : NAME
      {
          stringList.push_back(std::u16string(SemanticText($1) != nullptr ? SemanticText($1) : u""));
      }
    ;

integer_def_2
    : INTEGER
      {
          $$ = grammar::DoubleSemantic(static_cast<double>(grammar::SemanticInt64($1)));
      }
    | FLOAT_NUMBER
      { $$ = $1; }
    ;

// The original tables have rule 167 that no parser state reduces.
// It is left out.

%%
