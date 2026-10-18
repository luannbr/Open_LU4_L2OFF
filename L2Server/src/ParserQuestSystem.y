// questsystem.txt grammar (original L2Server/parser_questsystem.y).
// Bison generates ParserQuestSystem.cpp and ParserQuestSystemTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserQuestSystem.h"

#include "L2Server/src/Item.h"
#include "L2Server/src/ObjectDB.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "ParserQuestSystemTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForQuestSystem::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserQuestSystemTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

const char16_t* SemanticText(const grammar::SemanticBits value) {
    return grammar::SemanticString(value);
}

} // namespace

CParserForQuestSystem::CParserForQuestSystem() :
    m_lexer(),
    m_QuestId(-1),
    m_OnetimeQuestId(-1),
    m_pQuest(nullptr),
    m_pFeedback(nullptr),
    m_pPage(nullptr),
    m_OperationList(),
    m_ConditionList(),
    m_pConditionItem(nullptr),
    m_pConditionInven(nullptr),
    m_ItemManiDataList(),
    m_ItemManiState(1) {
}

CParserForQuestSystem::~CParserForQuestSystem() = default;

int CParserForQuestSystem::Create(char16_t* const fileBuffer, const int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}
}

%token QUEST_SETTING_BEGIN
%token QUEST_SETTING_END
%token FEEDBACK_FULL_QUEST
%token FEEDBACK_FULL_INVENTORY
%token FEEDBACK_DUPLICATED_QUEST
%token FEEDBACK_FINISHED_ONETIME_QUEST
%token FEEDBACK_FINISHED_DAILY_QUEST
%token SOUND_QUEST_ACCEPTED
%token MSG
%token HTML
%token FHTML
%token QHTML
%token QFHTML
%token QUEST_BEGIN
%token QUEST_END
%token QUEST_ID
%token ONETIME_QUEST_ID
%token QUEST_START_BEGIN
%token QUEST_START_END
%token START_ITEM
%token START_NPC
%token SHOW_HTML
%token QUEST_ACCEPT_BEGIN
%token QUEST_ACCEPT_END
%token GIVE_ITEM
%token DELETE_ITEM
%token CONDITION_CHECK_BEGIN
%token CONDITION_CHECK_END
%token LEVEL
%token ITEM
%token FEEDBACK_ON_FAILED
%token FSTRING
%token NAME
%token STRING
%token INTEGER
%token FLOAT_NUMBER
%start n1

%%

n1
    : quest_setting quest_list
    ;

quest_setting
    : QUEST_SETTING_BEGIN feedback_full_quest_def feedback_full_inventory_def feedback_duplicated_quest_def
      feedback_finished_onetime_quest_def feedback_finished_daily_quest_def sound_quest_accepted_def QUEST_SETTING_END
    ;

feedback_full_quest_def
    : FEEDBACK_FULL_QUEST '=' n38
      {
          CQuestSystem& system = Singleton<CQuestSystem>::inst();
          system.m_DefaultSetting.m_DefaultFeedback[0] = m_pFeedback;
          m_pFeedback = nullptr;
      }
    ;

feedback_full_inventory_def
    : FEEDBACK_FULL_INVENTORY '=' n38
      {
          CQuestSystem& system = Singleton<CQuestSystem>::inst();
          system.m_DefaultSetting.m_DefaultFeedback[1] = m_pFeedback;
          m_pFeedback = nullptr;
      }
    ;

feedback_duplicated_quest_def
    : FEEDBACK_DUPLICATED_QUEST '=' n38
      {
          CQuestSystem& system = Singleton<CQuestSystem>::inst();
          system.m_DefaultSetting.m_DefaultFeedback[2] = m_pFeedback;
          m_pFeedback = nullptr;
      }
    ;

feedback_finished_onetime_quest_def
    : FEEDBACK_FINISHED_ONETIME_QUEST '=' n38
      {
          CQuestSystem& system = Singleton<CQuestSystem>::inst();
          system.m_DefaultSetting.m_DefaultFeedback[3] = m_pFeedback;
          m_pFeedback = nullptr;
      }
    ;

feedback_finished_daily_quest_def
    : FEEDBACK_FINISHED_DAILY_QUEST '=' n38
      {
          CQuestSystem& system = Singleton<CQuestSystem>::inst();
          system.m_DefaultSetting.m_DefaultFeedback[4] = m_pFeedback;
          m_pFeedback = nullptr;
      }
    ;

sound_quest_accepted_def
    : SOUND_QUEST_ACCEPTED '=' STRING
      {
          CQuestSystem& system = Singleton<CQuestSystem>::inst();
          const char16_t* const sound = SemanticText($3);
          std::fill(std::begin(system.m_DefaultSetting.m_SoundFile), std::end(system.m_DefaultSetting.m_SoundFile), u'\0');
          if (sound != nullptr) {
              const std::size_t length = std::min<std::size_t>(std::char_traits<char16_t>::length(sound), 49);
              std::copy_n(sound, length, system.m_DefaultSetting.m_SoundFile);
          }
      }
    ;

quest_list
    : quest_list quest
    | %empty
    ;

quest
    : QUEST_BEGIN n12
      { m_pQuest = new CQuest(m_QuestId, m_OnetimeQuestId); }
      quest_start quest_accept QUEST_END
      {
          CQuestSystem& system = Singleton<CQuestSystem>::inst();
          const auto inserted = system.m_QuestIdMap.emplace(m_pQuest->GetId(), m_pQuest);
          if (!inserted.second) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Duplicated quest id[%d] in line[%d] while loading a questsystem data",
              //     m_pQuest->GetId(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Duplicated quest id[%d] in line[%d] while loading a questsystem data",
              //     m_pQuest->GetId(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log, "questsystem.txt line %lld: duplicate quest ID %d; the existing quest-ID entry is retained.",
                  static_cast<long long>(m_lexer.line) + 1, m_pQuest->GetId()
              );
          }
          m_QuestId = -1;
          m_OnetimeQuestId = -1;
      }
    ;

n12
    : quest_id_def n14
    ;

quest_id_def
    : QUEST_ID '=' INTEGER
      {
          m_QuestId = grammar::SemanticInt32($3);
          m_lexer.questId = m_QuestId;
      }
    ;

n14
    : n14 n15
    | %empty
    ;

n15
    : onetime_quest_id_def
    ;

onetime_quest_id_def
    : ONETIME_QUEST_ID '=' INTEGER
      { m_OnetimeQuestId = grammar::SemanticInt32($3); }
    ;

quest_start
    : QUEST_START_BEGIN start_item_def n21 QUEST_START_END
    ;

start_item_def
    : START_ITEM '=' '{' n19 '}'
    ;

n19
    : n19 ';' name_def
    | name_def
    ;

name_def
    : NAME
      {
          CQuestSystem& system = Singleton<CQuestSystem>::inst();
          const char16_t* const itemName = SemanticText($1);
          CObject* const object = g_ObjectDB.GetObjectByName(itemName);
          CItem* const item = object != nullptr && object->IsItem() ? object->CastItem() : nullptr;
          // const std::wstring hostName = target_utf16::ToHostCodeUnits(itemName)
          const std::string hostName = Utf16ToUtf8(itemName);
          if (item == nullptr) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined item[%s] in line[%d] while loading a questsystem data",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Undefined item[%s] in line[%d] while loading a questsystem data",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "questsystem.txt line %lld, quest %d: '%s' does not resolve to an item; the start-item route is "
                  "skipped.",
                  static_cast<long long>(m_lexer.line) + 1, m_QuestId,
                  itemName == nullptr ? "<null>" : hostName.c_str()
              );
              break;
          }
          if (item->m_const == nullptr || item->m_const->m_enumDefaultAction != ITEM_ACTION_START_QUEST) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"default_action of item[%s] must be [action_start_quest] in line[%d] while loading a questsystem
              //     data", hostName.c_str(), m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"default_action of item[%s] must be [action_start_quest] in line[%d] while loading a questsystem
              //     data", hostName.c_str(), m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "questsystem.txt line %lld, quest %d: cannot register start item '%s': %s; the start-item route is "
                  "skipped.",
                  static_cast<long long>(m_lexer.line) + 1, m_QuestId,
                  itemName == nullptr ? "<null>" : hostName.c_str(),
                  item->m_const == nullptr ? "item metadata is missing" : "default_action must be action_start_quest"
              );
              break;
          }
          if (!system.m_QuestStartItemMap.emplace(item->GetClassId(), m_pQuest).second) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Duplicated quest start item[%s] in line[%d] while loading a questsystem data",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Duplicated quest start item[%s] in line[%d] while loading a questsystem data",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "questsystem.txt line %lld, quest %d: duplicate start item '%s'; the existing item-to-quest route is "
                  "retained.",
                  static_cast<long long>(m_lexer.line) + 1, m_QuestId,
                  itemName == nullptr ? "<null>" : hostName.c_str()
              );
          }
      }
    ;

n21
    : n22
      {
          const auto appendOperations = [this](CQuestBase::OperationList& destination) {
              destination.insert(destination.end(), m_OperationList.begin(), m_OperationList.end());
              m_OperationList.clear();
          };
          appendOperations(m_pQuest->m_StartOperationList);
      }
    ;

n22
    : n23
    ;

n23
    : show_html_def
    ;

quest_accept
    : QUEST_ACCEPT_BEGIN
      {
          CQuestSystem& system = Singleton<CQuestSystem>::inst();
          m_pConditionInven = new QuestCondition_FullInven(system.m_DefaultSetting.m_DefaultFeedback[1]);
      }
      n26 n27 QUEST_ACCEPT_END
    ;

n26
    : condition_check
      {
          const auto appendConditions = [this](CQuestBase::ConditionList& destination) {
              destination.insert(destination.end(), m_ConditionList.begin(), m_ConditionList.end());
              m_ConditionList.clear();
          };
          appendConditions(m_pQuest->m_AcceptConditionList);
      }
    ;

n27
    : n28 n30
      {
          const auto appendOperations = [this](CQuestBase::OperationList& destination) {
              destination.insert(destination.end(), m_OperationList.begin(), m_OperationList.end());
              m_OperationList.clear();
          };
          m_pQuest->m_AcceptConditionList.push_back(m_pConditionInven);
          m_pConditionInven = nullptr;
          appendOperations(m_pQuest->m_AcceptOperationList);
      }
    ;

n28
    : n29
    ;

n29
    : show_html_def
    ;

n30
    : n30 n31
    | %empty
    ;

n31
    : give_item_def
    | delete_item_def
    ;

give_item_def
    : GIVE_ITEM
      { m_ItemManiState = 1; }
      '=' '{' n36 '}'
      {
          const auto appendItems = [this](QuestOperation_Item* destination) {
              destination->m_ItemList.insert(
                  destination->m_ItemList.end(), m_ItemManiDataList.begin(), m_ItemManiDataList.end()
              );
          };
          appendItems(m_pQuest->m_pAcceptOperation_Item);
          for (const QuestOperation_Item::ItemDataType& item : m_ItemManiDataList) {
              m_pConditionInven->m_CheckAddableItem.Push(static_cast<std::int32_t>(item.itemClassId), item.itemCount);
          }
          m_ItemManiDataList.clear();
      }
    ;

delete_item_def
    : DELETE_ITEM
      { m_ItemManiState = -1; }
      '=' '{' n36 '}'
      {
          const auto appendItems = [this](QuestOperation_Item* destination) {
              destination->m_ItemList.insert(
                  destination->m_ItemList.end(), m_ItemManiDataList.begin(), m_ItemManiDataList.end()
              );
          };
          appendItems(m_pQuest->m_pAcceptOperation_Item);
          m_ItemManiDataList.clear();
      }
    ;

n36
    : n36 ';' n37
    | n37
    ;

n37
    : '{' NAME ';' INTEGER ';' INTEGER '}'
      {
          const char16_t* const itemName = SemanticText($2);
          CObject* const object = g_ObjectDB.GetObjectByName(itemName);
          CItem* const item = object != nullptr && object->IsItem() ? object->CastItem() : nullptr;
          if (item == nullptr) {
              // const std::wstring hostName = target_utf16::ToHostCodeUnits(itemName)
              const std::string hostName = Utf16ToUtf8(itemName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined item[%s] in line[%d] while loading a questsystem data",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Undefined item[%s] in line[%d] while loading a questsystem data",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "questsystem.txt line %lld, quest %d: '%s' does not resolve to an item; the item manipulation entry is "
                  "skipped.",
                  static_cast<long long>(m_lexer.line) + 1, m_QuestId, itemName == nullptr ? "<null>" : hostName.c_str()
              );
              break;
          }
          m_ItemManiDataList.push_back(QuestOperation_Item::ItemDataType{
              static_cast<unsigned int>(item->GetClassId()),
              static_cast<std::int64_t>(runtime::MultiplyInt32Wrapping(grammar::SemanticInt32($6), m_ItemManiState)),
              grammar::SemanticInt32($4)
          });
      }
    ;

n38
    : n39
    | n40
      {
          m_pFeedback = new QuestFeedback_HTML(m_pPage);
          m_pPage = nullptr;
      }
    ;

n39
    : '{' MSG ';' INTEGER '}'
      { m_pFeedback = new QuestFeedback_SystemMsg(grammar::SemanticInt32($4)); }
    ;

n40
    : '{' HTML ';' STRING '}'
      {
          auto* const page = new QuestPage_HTML;
          page->m_QuestPageName = std::u16string(SemanticText($4) != nullptr ? SemanticText($4) : u"");
          m_pPage = page;
      }
    | '{' FHTML ';' STRING
      {
          auto* const page = new QuestPage_FHTML;
          page->m_QuestPageName = std::u16string(SemanticText($4) != nullptr ? SemanticText($4) : u"");
          m_pPage = page;
      }
      ';' n43 '}'
    | '{' QHTML ';' STRING '}'
      {
          auto* const page = new QuestPage_QuestHTML;
          page->m_QuestPageName = std::u16string(SemanticText($4) != nullptr ? SemanticText($4) : u"");
          m_pPage = page;
      }
    | '{' QFHTML ';' STRING
      {
          auto* const page = new QuestPage_QuestFHTML;
          page->m_QuestPageName = std::u16string(SemanticText($4) != nullptr ? SemanticText($4) : u"");
          m_pPage = page;
      }
      ';' n43 '}'
    ;

n43
    : '{' n44 '}'
    ;

n44
    : n44 n45
    | n45
    ;

n45
    : '{' STRING ';' INTEGER '}'
      {
          static_cast<QuestPage_FHTML*>(m_pPage)
              ->m_Param[std::u16string(SemanticText($2) != nullptr ? SemanticText($2) : u"")] =
              IntToXStr(grammar::SemanticInt32($4));
      }
    | '{' STRING ';' STRING '}'
      {
          static_cast<QuestPage_FHTML*>(m_pPage)
              ->m_Param[std::u16string(SemanticText($2) != nullptr ? SemanticText($2) : u"")] =
              std::u16string(SemanticText($4) != nullptr ? SemanticText($4) : u"");
      }
    | '{' STRING ';' FSTRING ';' STRING '}'
      {
          std::u16string value(u"<fstring>");
          value += SemanticText($6) != nullptr ? SemanticText($6) : u"";
          value += u"</fstring>";
          static_cast<QuestPage_FHTML*>(m_pPage)
              ->m_Param[std::u16string(SemanticText($2) != nullptr ? SemanticText($2) : u"")] = value;
      }
    ;

condition_check
    : CONDITION_CHECK_BEGIN
      { m_ConditionList.clear(); }
      n48 CONDITION_CHECK_END
    ;

n48
    : n48 n49
    | n49
    ;

n49
    : n50
    | n53
    ;

n50
    : level_def feedback_on_failed_def
    ;

level_def
    : LEVEL '=' integer_def
    ;

integer_def
    : INTEGER '~' INTEGER
      {
          m_ConditionList.push_back(new QuestCondition_Level(grammar::SemanticInt32($1), grammar::SemanticInt32($3))
          );
      }
    | INTEGER '~'
      {
          m_ConditionList.push_back(new QuestCondition_Level(grammar::SemanticInt32($1), 120));
      }
    ;

n53
    : item_def feedback_on_failed_def
    ;

item_def
    : ITEM
      { m_pConditionItem = new QuestCondition_Item; }
      '=' '{' n56 '}'
      {
          m_ConditionList.push_back(m_pConditionItem);
          m_pConditionItem = nullptr;
      }
    ;

n56
    : n56 ';' n57
    | n57
    ;

n57
    : '{' NAME ';' INTEGER '}'
      {
          const char16_t* const itemName = SemanticText($2);
          CObject* const object = g_ObjectDB.GetObjectByName(itemName);
          CItem* const item = object != nullptr && object->IsItem() ? object->CastItem() : nullptr;
          if (item == nullptr) {
              // const std::wstring hostName = target_utf16::ToHostCodeUnits(itemName)
              const std::string hostName = Utf16ToUtf8(itemName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined item[%s] in line[%d] while loading a questsystem data",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Undefined item[%s] in line[%d] while loading a questsystem data",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "questsystem.txt line %lld, quest %d: '%s' does not resolve to an item; the item requirement is omitted "
                  "from the condition.",
                  static_cast<long long>(m_lexer.line) + 1, m_QuestId,
                  itemName == nullptr ? "<null>" : hostName.c_str()
              );
              break;
          }
          m_pConditionItem->m_NeedItem.Push(item->GetClassId(), static_cast<std::int64_t>(grammar::SemanticInt32($4)));
      }
    ;

feedback_on_failed_def
    : FEEDBACK_ON_FAILED '=' n38
      { m_ConditionList.back()->m_pFeedback = m_pFeedback; }
    ;

show_html_def
    : SHOW_HTML '=' n40
      {
          m_OperationList.push_back(new QuestOperation_ShowQuestPage(m_pPage));
          m_pPage = nullptr;
      }
    ;

%%
