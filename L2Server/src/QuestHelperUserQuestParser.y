// UserQuestData.txt grammar.
// Bison generates QuestHelperUserQuestParser.cpp and QuestHelperUserQuestParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/QuestHelperUserQuestParser.h"

#include "L2Server/src/QuestHelper.h"
#include "Shared/src/Runtime/SourceLog.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "QuestHelperUserQuestParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForQuestHelperUserQuest::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror(yychar))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"QuestHelperUserQuestParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

char16_t* SemanticText(const grammar::SemanticBits value) {
    return grammar::SemanticString(value);
}

} // namespace

CParserForQuestHelperUserQuest::CParserForQuestHelperUserQuest() :
    m_lexer(), m_tempIntVector(), m_npcList(), QuestName(), strHtmlFileName() {
}

CParserForQuestHelperUserQuest::~CParserForQuestHelperUserQuest() = default;

int CParserForQuestHelperUserQuest::Create(char16_t* const fileBuffer, const int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}

// void CParserForQuestHelperUserQuest::yysyntaxerror() {
//     yl::yywparser::yysyntaxerror()
// }
void CParserForQuestHelperUserQuest::yysyntaxerror(const int lookahead) {
    // The original token codes start at 0x10001 in the order of the token list.
    const int code = lookahead >= TK_RBRACKET ? lookahead - TK_RBRACKET + 0x10001 : lookahead;
    const std::wstring token = lookahead == TK_YYEMPTY ? L"lookahead is unavailable"
        : lookahead == TK_YYEOF                        ? L"end of input"
                                                       : L"token code " + std::to_wstring(code);
    const std::wstring message = L"Syntax error in UserQuestData.txt for the quest helper at line " +
        std::to_wstring(static_cast<long long>(m_lexer.m_line) + 1) + L" (" + token + L").";
    const runtime::logging::SourceFileScope source(__FILE__);
    grammar::ReportError(message.c_str());
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
%token CANCEL_OPERATIONS
%token BEGIN_OPERATIONS
%token NPC_DIE_EVENT_START_TALK_SELECTED_EVENT_START_MENU_SELECTED_EVENT_START
%token NPC_LIST
%token EVENT_CONDITIONS
%token EVENT_OPERATIONS
%token NPC_DIE_EVENT_END_TALK_SELECTED_EVENT_END_MENU_SELECTED_EVENT_END
%token CONDITION_TARGET_TYPE
%token EXECUTION_TYPE
%token CHOICE_STRING
%token QUEST_ITEMS
%token EVENT_RANGE
%token ASK
%token REPLY
// The original grammar has these conflicts. Bison resolves them as the
// original tables did. A shift wins over a reduce.
%expect 3
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
          const auto assignText = [](std::u16string& target, char16_t* value) {
              target.assign(value == nullptr ? u"" : value);
          };
          QuestID = grammar::SemanticInt32($4);
          assignText(QuestName, SemanticText($7));
      }
      quest_items_def start_conditions_def accept_ok_operations_def accept_fail_operations_def complete_operations_def
      cancel_operations_def journal_list npc_die_event_start_talk_selected_event_start_menu_selected_event_list QUEST_END
    ;

start_conditions_def
    : START_CONDITIONS '=' n15
    ;

accept_ok_operations_def
    : ACCEPT_OK_OPERATIONS '=' n18
    ;

accept_fail_operations_def
    : ACCEPT_FAIL_OPERATIONS '=' n18
    ;

complete_operations_def
    : COMPLETE_OPERATIONS '=' n18
    ;

cancel_operations_def
    : CANCEL_OPERATIONS '=' n18
    ;

journal_list
    : journal_list journal
    | %empty
    ;

journal
    : JOURNAL_BEGIN begin_operations_def complete_conditions_def complete_operations_def_2 JOURNAL_END
    ;

begin_operations_def
    : BEGIN_OPERATIONS '=' n18
    ;

complete_conditions_def
    : COMPLETE_CONDITIONS '=' n15
    ;

complete_operations_def_2
    : COMPLETE_OPERATIONS '=' n18
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
    : IDENTIFIER '(' n21 ')' ';'
    ;

n18
    : '{' identifier_def_2_list '}'
    ;

identifier_def_2_list
    : identifier_def_2_list identifier_def_2
    | identifier_def_2
    | %empty
    ;

identifier_def_2
    : IDENTIFIER '(' n21 ')' ';'
      {
          bSaveHtml = false;
          const char16_t* const operation = SemanticText($1);
          if (operation != nullptr && std::u16string_view(operation) == u"op_show_html") {
              bSaveHtml = true;
          }
      }
    ;

n21
    : n21 ',' rbracket_def
    | rbracket_def
    | %empty
    ;

rbracket_def
    : RBRACKET
      {
          const auto assignText = [](std::u16string& target, char16_t* value) {
              target.assign(value == nullptr ? u"" : value);
          };
          if (bSaveHtml) {
              assignText(strHtmlFileName, SemanticText($1));
              bSaveHtml = false;
          }
      }
    | SYM SYM
    | SYM IDENTIFIER SYM
    | INTEGER
    ;

npc_die_event_start_talk_selected_event_start_menu_selected_event_list
    : npc_die_event_start_talk_selected_event_start_menu_selected_event_list
      npc_die_event_start_talk_selected_event_start_menu_selected_event
    | npc_die_event_start_talk_selected_event_start_menu_selected_event
    | %empty
    ;

npc_die_event_start_talk_selected_event_start_menu_selected_event
    : NPC_DIE_EVENT_START_TALK_SELECTED_EVENT_START_MENU_SELECTED_EVENT_START
      {
          m_eventType = GetQuestNpcEventType(SemanticText($1));
          m_line = static_cast<std::int64_t>(m_lexer.m_line + 1);
      }
      npc_list_def choice_string_def ask_def condition_target_type_def event_range_def event_conditions_def
      execution_type_def event_operations_def NPC_DIE_EVENT_END_TALK_SELECTED_EVENT_END_MENU_SELECTED_EVENT_END
      {
          if (m_eventType == QUEST_NPC_EVENT_TALK_SELECTED) {
              CUserQuestTalkSelectedParser parser(this);
              parser.parse(g_QuestHelper);
          } else if (m_eventType == QUEST_NPC_EVENT_MENU_SELECTED) {
              CUserQuestMenuSelectParser parser(this);
              parser.parse(g_QuestHelper);
          }
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
    : NPC_LIST '=' n35
      {
          m_npcList = m_tempIntVector;
          m_tempIntVector.clear();
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
    | %empty
    ;

event_operations_def
    : EVENT_OPERATIONS '=' n18
    ;

quest_items_def
    : QUEST_ITEMS '=' n36
    | %empty
    ;

event_range_def
    : EVENT_RANGE '=' INTEGER
    | %empty
    ;

condition_target_type_def
    : CONDITION_TARGET_TYPE '=' IDENTIFIER
    | %empty
    ;

execution_type_def
    : EXECUTION_TYPE '=' IDENTIFIER
    | %empty
    ;

n35
    : '{' n37 '}'
    ;

n36
    : '{' n39 '}'
    ;

n37
    : n37 ',' INTEGER
      {
          const auto pushLow = [this](const grammar::SemanticBits value) {
              m_tempIntVector.push_back(grammar::SemanticInt32(value));
          };
          pushLow($3);
      }
    | INTEGER
      {
          const auto pushLow = [this](const grammar::SemanticBits value) {
              m_tempIntVector.push_back(grammar::SemanticInt32(value));
          };
          m_tempIntVector.clear();
          pushLow($1);
      }
    ;

n39
    : n39 ',' rbracket_def_2
      {
          const auto pushLow = [this](const grammar::SemanticBits value) {
              m_tempIntVector.push_back(grammar::SemanticInt32(value));
          };
          if (grammar::SemanticInt64($3) != -1) {
              pushLow($3);
          }
      }
    | rbracket_def_2
      {
          const auto pushLow = [this](const grammar::SemanticBits value) {
              m_tempIntVector.push_back(grammar::SemanticInt32(value));
          };
          m_tempIntVector.clear();
          if (grammar::SemanticInt64($1) != -1) {
              pushLow($1);
          }
      }
    | %empty
      { m_tempIntVector.clear(); }
    ;

rbracket_def_2
    : RBRACKET
    ;

// The original tables have rules 59, 60 and 61 that no parser state reduces.
// They are left out.

%%
