// UserQuestData.txt grammar.
// Bison generates QuestDataYacc.cpp and QuestDataYaccTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/QuestDataYacc.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/QuestManager.h"

#include "Shared/src/Log.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "QuestDataYaccTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForQuestData::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"QuestDataYaccTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

std::u16string MakePchLine(const char16_t* name, const std::int32_t id) {
    const std::string digits = std::to_string(id);
    std::u16string line;
    line.reserve(std::char_traits<char16_t>::length(name) + digits.size() + 5);
    line.push_back(u'[');
    line.append(name);
    line.append(u"]\t");
    for (const char digit : digits) {
        line.push_back(static_cast<char16_t>(digit));
    }
    line.append(u"\r\n");
    return line;
}

} // namespace

CParserForQuestData::CParserForQuestData() :
    m_lexer(),
    m_quests(),
    m_startConditions(),
    m_acceptOKOperations(),
    m_acceptFailOperations(),
    m_completeActions(),
    m_cancelOperations(),
    m_journals(),
    m_journalCompleteConditions(),
    m_journalCompleteOperations(),
    m_journalStartOperations(),
    m_journal(),
    m_conditions(),
    m_operations(),
    m_actions(),
    m_params(),
    m_npcEvent(QCTT_MAX, QET_MAX, 0),
    m_tempInts(),
    m_npcList(),
    m_relatedItems(),
    m_npcEventConditions(),
    m_npcEventActions(),
    m_QuestName() {
    m_bOnlyPrecompiledHeader = false;
}

CParserForQuestData::~CParserForQuestData() = default;

int CParserForQuestData::Create(char16_t* const fileBuffer, const int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}
}

%token RBRACKET
%token IDENTIFIER
%token INTEGER
%token SYM
%token QUEST_BEGIN
%token QUEST_END
%token QUEST_ID
%token QUEST_NAME
%token JOURNAL_BEGIN
%token JOURNAL_END
%token START_CONDITIONS
%token ACCEPT_OK_OPERATIONS
%token ACCEPT_FAIL_OPERATIONS
%token COMPLETE_CONDITIONS
%token COMPLETE_OPERATIONS
%token COMPLETE_ACTIONS
%token CANCEL_OPERATIONS
%token BEGIN_OPERATIONS
%token NPC_DIE_EVENT_START_TALK_SELECTED_EVENT_START_MENU_SELECTED_EVENT_START
%token NPC_LIST
%token EVENT_CONDITIONS
%token EVENT_ACTIONS
%token NPC_DIE_EVENT_END_TALK_SELECTED_EVENT_END_MENU_SELECTED_EVENT_END
%token CONDITION_TARGET_TYPE
%token EXECUTION_TYPE
%token CHOICE_STRING
%token QUEST_ITEMS
%token EVENT_RANGE
%token ASK
%token REPLY
%token ACTION_BEGIN
%token ACTION_END
%token CONDITIONS
%token OPERATIONS
// The original grammar has these conflicts. Bison resolves them as the
// original tables did. A shift wins over a reduce.
%expect 4
%start n1

%%

n1
    : quest_list
    ;

quest_list
    : quest_list quest
    | %empty
    ;

quest
    : QUEST_BEGIN QUEST_ID '=' INTEGER QUEST_NAME '=' RBRACKET
      {
          m_journalNum = 1;
          const std::int64_t questId = grammar::SemanticInt64($4);
          char16_t* const questName = grammar::SemanticPointer<char16_t>($7);
          if (!m_bOnlyPrecompiledHeader) {
              m_pQuest = new QuestData(questId);
              m_pQuest->SetQuestName(std::u16string(questName));
              break;
          }
          m_QuestID = static_cast<std::int32_t>(questId);
          m_QuestName.assign(questName);
          std::u16string line = MakePchLine(m_QuestName.c_str(), m_QuestID);
          QuestManager::inst().Uniputs(line.data());
      }
      quest_items_def start_conditions_def accept_ok_operations_def accept_fail_operations_def complete_actions_def
      cancel_operations_def journal_list npc_die_event_start_talk_selected_event_start_menu_selected_event_list QUEST_END
      {
          if (!m_bOnlyPrecompiledHeader) {
              m_pQuest->SetStartConditions(m_startConditions);
              m_pQuest->SetAcceptOkOperations(m_acceptOKOperations);
              m_pQuest->SetAcceptFailOperations(m_acceptFailOperations);
              m_pQuest->SetCompleteActions(m_completeActions);
              m_pQuest->SetCancelOperations(m_cancelOperations);
              m_pQuest->SetJournals(m_journals);
              m_pQuest->SetRelatedItems(m_relatedItems);
              m_pQuest->ChangeLastJournalCompleteSoundEffect();
              m_quests.push_back(m_pQuest);
          }
          m_journals.clear();
          m_pQuest = nullptr;
      }
    ;

start_conditions_def
    : START_CONDITIONS '=' n15
      {
          m_startConditions = m_conditions;
          m_conditions.clear();
      }
    ;

accept_ok_operations_def
    : ACCEPT_OK_OPERATIONS '=' n21
      {
          m_acceptOKOperations = m_operations;
          m_operations.clear();
      }
    ;

accept_fail_operations_def
    : ACCEPT_FAIL_OPERATIONS '=' n21
      {
          m_acceptFailOperations = m_operations;
          m_operations.clear();
      }
    ;

complete_actions_def
    : COMPLETE_ACTIONS '=' n18
      {
          m_completeActions = m_actions;
          m_actions.clear();
      }
    ;

cancel_operations_def
    : CANCEL_OPERATIONS '=' n21
      {
          m_cancelOperations = m_operations;
          m_operations.clear();
      }
    ;

journal_list
    : journal_list journal
    | %empty
    ;

journal
    : JOURNAL_BEGIN begin_operations_def complete_conditions_def complete_operations_def JOURNAL_END
      {
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          m_journal.m_nJournalNum = m_journalNum++;
          m_journal.m_nQuestID = static_cast<std::int32_t>(m_pQuest->m_questID);
          m_journal.SetBeginOperations(m_journalStartOperations);
          m_journal.SetCompleteConditions(m_journalCompleteConditions);
          m_journal.SetCompleteOperations(m_journalCompleteOperations);
          m_journals.push_back(m_journal);
          m_journalStartOperations.clear();
          m_journalCompleteConditions.clear();
          m_journalCompleteOperations.clear();
      }
    ;

begin_operations_def
    : BEGIN_OPERATIONS '=' n21
      {
          m_journalStartOperations = m_operations;
          m_operations.clear();
      }
    ;

complete_conditions_def
    : COMPLETE_CONDITIONS '=' n15
      {
          m_journalCompleteConditions = m_conditions;
          m_conditions.clear();
      }
    ;

complete_operations_def
    : COMPLETE_OPERATIONS '=' n21
      {
          m_journalCompleteOperations = m_operations;
          m_operations.clear();
      }
    ;

n15
    : '{' identifier_def_list '}'
    ;

identifier_def_list
    : identifier_def_list identifier_def
    | identifier_def
    | %empty
    ;

identifier_def
    : IDENTIFIER '(' n24 ')' ';'
      {
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          char16_t* const name = grammar::SemanticPointer<char16_t>($1);
          const QuestConditionInfo* const info = GetConditionInfoFromName(name);
          if (info->m_eType == QCT_INVALID) {
              // const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
              const std::string hostName = Utf16ToUtf8(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Unknown condition (%s) [line: %d].",
              //     hostName.c_str(),
              //     m_lexer.ErrorLine())
              // L2_LOG_ERROR(Log,
              //     L"Unknown condition (%s) [line: %d].",
              //     hostName.c_str(),
              //     m_lexer.ErrorLine())
              L2_LOG_ERROR(
                  Log,
                  "UserQuestData.txt line %d, quest %lld: unknown condition '%s'; the invalid definition is retained and "
                  "parsing continues.",
                  m_lexer.ErrorLine(), static_cast<long long>(m_pQuest->m_questID),
                  name == nullptr ? "<null>" : hostName.c_str()
              );
          } else if (info->m_nNumberOfParameters != static_cast<std::int32_t>(m_params.size())) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"The number of parameters is not correct[line: %d].",
              //     m_lexer.ErrorLine())
              // L2_LOG_ERROR(Log,
              //     L"The number of parameters is not correct[line: %d].",
              //     m_lexer.ErrorLine())
              L2_LOG_ERROR(
                  Log,
                  "UserQuestData.txt line %d, quest %lld: condition '%s' expects %d parameters but received %llu; the "
                  "definition is retained and parsing continues.",
                  m_lexer.ErrorLine(), static_cast<long long>(m_pQuest->m_questID),
                  name == nullptr ? "<null>" : Utf16ToUtf8(name).c_str(), info->m_nNumberOfParameters,
                  static_cast<unsigned long long>(m_params.size())
              );
          }
          m_conditions.push_back(
              QuestConditionData(info->m_eType, static_cast<std::int32_t>(m_pQuest->m_questID), m_params)
          );
          m_params.clear();
      }
    ;

n18
    : '{' action_list '}'
    ;

action_list
    : action_list action
    | action
    | %empty
    ;

action
    : ACTION_BEGIN CONDITIONS '=' n15 OPERATIONS '=' n21 ACTION_END
      {
          QuestActionData data{m_conditions, m_operations};
          m_actions.push_back(data);
          m_conditions.clear();
          m_operations.clear();
      }
    ;

n21
    : '{' identifier_def_2_list '}'
    ;

identifier_def_2_list
    : identifier_def_2_list identifier_def_2
    | identifier_def_2
    | %empty
    ;

identifier_def_2
    : IDENTIFIER '(' n24 ')' ';'
      {
          if (!m_bOnlyPrecompiledHeader) {
              char16_t* const name = grammar::SemanticPointer<char16_t>($1);
              const QuestOperationInfo* const info = GetOperationInfoFromName(name);
              if (info->m_eType == QOT_INVALID) {
                  // const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
                  const std::string hostName = Utf16ToUtf8(name);
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Unknown operation (%s) [line: %d].",
                  //     hostName.c_str(),
                  //     m_lexer.ErrorLine())
                  // L2_LOG_ERROR(Log,
                  //     L"Unknown operation (%s) [line: %d].",
                  //     hostName.c_str(),
                  //     m_lexer.ErrorLine())
                  L2_LOG_ERROR(
                      Log,
                      "UserQuestData.txt line %d, quest %lld: unknown operation '%s'; the invalid definition is retained "
                      "and parsing continues.",
                      m_lexer.ErrorLine(), static_cast<long long>(m_pQuest->m_questID),
                      name == nullptr ? "<null>" : hostName.c_str()
                  );
              } else if (info->m_nNumberOfParameters != static_cast<std::int32_t>(m_params.size())) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"The number of parameters is not correct[line: %d].",
                  //     m_lexer.ErrorLine())
                  // L2_LOG_ERROR(Log,
                  //     L"The number of parameters is not correct[line: %d].",
                  //     m_lexer.ErrorLine())
                  L2_LOG_ERROR(
                      Log,
                      "UserQuestData.txt line %d, quest %lld: operation '%s' expects %d parameters but received %llu; the "
                      "definition is retained and parsing continues.",
                      m_lexer.ErrorLine(), static_cast<long long>(m_pQuest->m_questID),
                      name == nullptr ? "<null>" : Utf16ToUtf8(name).c_str(), info->m_nNumberOfParameters,
                      static_cast<unsigned long long>(m_params.size())
                  );
              }
              m_operations.push_back(
                  QuestOperationData(info->m_eType, static_cast<std::int32_t>(m_pQuest->m_questID), m_params)
              );
          }
          m_params.clear();
      }
    ;

n24
    : n24 ',' rbracket_def
      { m_params.push_back(grammar::SemanticPointer<QuestDataVariable>($3)); }
    | rbracket_def
      { m_params.push_back(grammar::SemanticPointer<QuestDataVariable>($1)); }
    | %empty
    ;

rbracket_def
    : RBRACKET
      {
          auto* const value = new QuestDataVariable;
          value->SetString(grammar::SemanticPointer<char16_t>($1));
          $$ = grammar::PointerSemantic(value);
      }
    | SYM SYM
      {
          auto* const value = new QuestDataVariable;
          value->SetString(u"");
          $$ = grammar::PointerSemantic(value);
      }
    | SYM IDENTIFIER SYM
      {
          auto* const value = new QuestDataVariable;
          value->SetString(grammar::SemanticPointer<char16_t>($2));
          $$ = grammar::PointerSemantic(value);
      }
    | INTEGER
      {
          $$ = grammar::PointerSemantic(new QuestDataVariable(grammar::SemanticInt64($1)));
      }
    ;

npc_die_event_start_talk_selected_event_start_menu_selected_event_list
    : npc_die_event_start_talk_selected_event_start_menu_selected_event_list
      npc_die_event_start_talk_selected_event_start_menu_selected_event
    | npc_die_event_start_talk_selected_event_start_menu_selected_event
    | %empty
    ;

npc_die_event_start_talk_selected_event_start_menu_selected_event
    : NPC_DIE_EVENT_START_TALK_SELECTED_EVENT_START_MENU_SELECTED_EVENT_START npc_list_def choice_string_def ask_def
      condition_target_type_def event_range_def event_conditions_def execution_type_def event_actions_def
      NPC_DIE_EVENT_END_TALK_SELECTED_EVENT_END_MENU_SELECTED_EVENT_END
      {
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          m_npcEvent.SetConditions(m_npcEventConditions);
          m_npcEvent.SetActions(m_npcEventActions);
          m_npcEvent.m_npcList = m_npcList;
          m_npcEvent.m_choice = m_choice;
          m_npcEvent.m_eventType = GetQuestNpcEventType(grammar::SemanticPointer<char16_t>($1));
          m_npcEvent.m_conditionTargetType = m_conditionTargetType;
          m_npcEvent.m_executionType = m_executionType;
          m_npcEvent.m_eventRange = m_eventRange;
          m_npcEvent.m_questID = static_cast<std::int32_t>(m_pQuest->m_questID);
          m_npcEvent.m_questName = m_pQuest->GetQuestName();
          m_npcEvent.m_ask = m_ask;
          m_npcEvent.m_reply = m_reply;
          m_npcEvent.InjectToNpc();
      }
    ;

ask_def
    : ASK '=' INTEGER REPLY '=' INTEGER
      {
          m_ask = grammar::SemanticInt32($3);
          m_reply = grammar::SemanticInt32($6);
      }
    | %empty
      {
          m_ask = 0;
          m_reply = 0;
      }
    ;

npc_list_def
    : NPC_LIST '=' n37
      {
          m_npcList = m_tempInts;
          m_tempInts.clear();
      }
    ;

choice_string_def
    : CHOICE_STRING '=' INTEGER
      { m_choice = grammar::SemanticInt32($3); }
    | %empty
      { m_choice = 0; }
    ;

event_conditions_def
    : EVENT_CONDITIONS '=' n15
      {
          m_npcEventConditions = m_conditions;
          m_conditions.clear();
      }
    | %empty
    ;

event_actions_def
    : EVENT_ACTIONS '=' n18
      {
          m_npcEventActions = m_actions;
          m_actions.clear();
      }
    ;

quest_items_def
    : QUEST_ITEMS '=' n38
      {
          m_relatedItems = m_tempInts;
          m_tempInts.clear();
      }
    | %empty
      { m_relatedItems.clear(); }
    ;

event_range_def
    : EVENT_RANGE '=' INTEGER
      { m_eventRange = grammar::SemanticInt32($3); }
    | %empty
      { m_eventRange = 0; }
    ;

condition_target_type_def
    : CONDITION_TARGET_TYPE '=' IDENTIFIER
      {
          m_conditionTargetType = GetConditionTargetTypeFromName(grammar::SemanticPointer<char16_t>($3));
      }
    | %empty
      { m_conditionTargetType = QCTT_NONE; }
    ;

execution_type_def
    : EXECUTION_TYPE '=' IDENTIFIER
      {
          m_executionType = GetExecutionTypeFromName(grammar::SemanticPointer<char16_t>($3));
      }
    | %empty
      { m_executionType = QET_NONE; }
    ;

n37
    : '{' n39 '}'
    ;

n38
    : '{' n41 '}'
    ;

n39
    : n39 ',' INTEGER
      { m_tempInts.push_back(grammar::SemanticInt32($3)); }
    | INTEGER
      {
          m_tempInts.clear();
          m_tempInts.push_back(grammar::SemanticInt32($1));
      }
    ;

n41
    : n41 ',' rbracket_def_2
      {
          const std::int64_t value = grammar::SemanticInt64($3);
          if (value != -1) {
              m_tempInts.push_back(static_cast<std::int32_t>(value));
          }
      }
    | rbracket_def_2
      {
          m_tempInts.clear();
          const std::int64_t value = grammar::SemanticInt64($1);
          if (value != -1) {
              m_tempInts.push_back(static_cast<std::int32_t>(value));
          }
      }
    | %empty
      { m_tempInts.clear(); }
    ;

rbracket_def_2
    : RBRACKET
      {
          $$ = static_cast<grammar::SemanticBits>(
              static_cast<std::int64_t>((GetItemClassIDFromName(grammar::SemanticPointer<char16_t>($1))))
          );
      }
    ;

// The original tables have rules 63, 64 and 65 that no parser state reduces.
// They are left out.

%%
