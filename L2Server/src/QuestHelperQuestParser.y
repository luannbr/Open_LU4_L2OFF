// questdata.txt grammar.
// Bison generates QuestHelperQuestParser.cpp and QuestHelperQuestParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/QuestHelperQuestParser.h"

#include "L2Server/src/QuestHelper.h"
#include "Shared/src/Runtime/SourceLog.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "QuestHelperQuestParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForQuestHelperQuest::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror(yychar))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"QuestHelperQuestParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

char16_t* SemanticText(const grammar::SemanticBits value) {
    return grammar::SemanticString(value);
}

} // namespace

CParserForQuestHelperQuest::CParserForQuestHelperQuest() :
    m_lexer(), QuestName(), NPCName(), Condition(), Handler(), WordSet() {
}

CParserForQuestHelperQuest::~CParserForQuestHelperQuest() = default;

int CParserForQuestHelperQuest::Create(char16_t* const fileBuffer, const int fileSize) {
    QuestID = -1;
    QuestName.assign(u"-_-0");
    NPCName.assign(u"-_-0");
    Condition.assign(u"what condition?");
    Choice = 0;
    Handler.assign(u"what handler");
    return m_lexer.Create(fileBuffer, fileSize);
}

// void CParserForQuestHelperQuest::yysyntaxerror() {
//     yl::yywparser::yysyntaxerror()
// }
void CParserForQuestHelperQuest::yysyntaxerror(const int lookahead) {
    // The original token codes start at 0x10001 in the order of the token list.
    const int code = lookahead >= TK_RBRACKET ? lookahead - TK_RBRACKET + 0x10001 : lookahead;
    const std::wstring token = lookahead == TK_YYEMPTY ? L"lookahead is unavailable"
        : lookahead == TK_YYEOF                        ? L"end of input"
                                                       : L"token code " + std::to_wstring(code);
    const std::wstring message = L"Syntax error in questdata.txt for the quest helper at line " +
        std::to_wstring(static_cast<long long>(m_lexer.line) + 1) + L" (" + token + L").";
    const runtime::logging::SourceFileScope source(__FILE__);
    grammar::ReportError(message.c_str());
}
}

%token RBRACKET
%token IDENTIFIER
%token ALWAYS_GROUP
%token FLOAT_NUMBER
%token QUEST_BEGIN
%token QUEST_ID
%token QUEST_NAME
%token NPC_BEGIN
%token NPC_NAME
%token TALKED_EVENT_BEGIN
%token TALKED_EVENT_END
%token TALK_SELECTED_EVENT_BEGIN
%token TALK_SELECTED_EVENT_END
%token CONDITION
%token CHOICE_STRING
%token HANDLER
%token QUEST_ACCEPTED_EVENT_BEGIN
%token QUEST_ACCEPTED_EVENT_END
%token MY_DYING_EVENT_BEGIN
%token MY_DYING_EVENT_END
%token ATTACKED_EVENT_BEGIN
%token ATTACKED_EVENT_END
%token SPELLED_EVENT_BEGIN
%token SPELLED_EVENT_END
%token TIMER_FIRED_EVENT_BEGIN
%token TIMER_FIRED_EVENT_END
%token TIMER_FIRED_EX_EVENT_BEGIN
%token TIMER_FIRED_EX_EVENT_END
%token CREATED_EVENT_BEGIN
%token CREATED_EVENT_END
%token MENU_SELECTED_EVENT_BEGIN
%token MENU_SELECTED_EVENT_END
%token SCRIPT_EVENT_BEGIN
%token SCRIPT_EVENT_END
%token NPC_END
%token QUEST_END
%token QUEST_ITEMS
%token CONDITION_TARGET
%token EXECUTION_TYPE
%token CONDITION_RANGE
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
    : QUEST_BEGIN QUEST_ID '=' ALWAYS_GROUP QUEST_NAME '=' RBRACKET
      {
          const auto assignText = [](std::u16string& target, char16_t* value) {
              target.assign(value == nullptr ? u"" : value);
          };
          QuestID = grammar::SemanticInt32($4);
          assignText(QuestName, SemanticText($7));
      }
      n5 npc_list QUEST_END
      {
          QuestID = -1;
          QuestName.assign(u"-_-1");
      }
    ;

n5
    : quest_items_def
    | %empty
    ;

quest_items_def
    : QUEST_ITEMS '=' '{' n7 '}'
    ;

n7
    : n8
    | %empty
    ;

n8
    : n8 ',' RBRACKET
    | RBRACKET
    ;

npc_list
    : npc_list npc
    | %empty
    ;

npc
    : NPC_BEGIN NPC_NAME '=' RBRACKET
      {
          const auto assignText = [](std::u16string& target, char16_t* value) {
              target.assign(value == nullptr ? u"" : value);
          };
          assignText(NPCName, SemanticText($4));
      }
      talked_event_list NPC_END
      { NPCName.assign(u"-_-2"); }
    ;

talked_event_list
    : talked_event_list talked_event
    | %empty
    ;

talked_event
    : TALKED_EVENT_BEGIN CONDITION
      { ConditionLine = m_lexer.line + 1; }
      '=' RBRACKET CHOICE_STRING '=' always_group_def HANDLER '(' n33 ')'
      { HandlerLine = m_lexer.line + 1; }
      '=' RBRACKET TALKED_EVENT_END
    | TALK_SELECTED_EVENT_BEGIN CONDITION
      { ConditionLine = m_lexer.line + 1; }
      '=' RBRACKET CHOICE_STRING '=' always_group_def HANDLER '(' n33 ')'
      { HandlerLine = m_lexer.line + 1; }
      '=' RBRACKET TALK_SELECTED_EVENT_END
      {
          const auto assignText = [](std::u16string& target, char16_t* value) {
              target.assign(value == nullptr ? u"" : value);
          };
          assignText(Condition, SemanticText($5));
          Choice = grammar::SemanticInt32($8);
          assignText(Handler, SemanticText($15));
          CQuestTalkSelectedParser parser(this);
          parser.parse(g_QuestHelper);
      }
    | QUEST_ACCEPTED_EVENT_BEGIN HANDLER '(' n33 ')'
      { HandlerLine = m_lexer.line + 1; }
      '=' RBRACKET QUEST_ACCEPTED_EVENT_END
      {
          CQuestQuestAccpetedParser parser(this);
          parser.parse(g_QuestHelper);
      }
    | ATTACKED_EVENT_BEGIN HANDLER '(' n33 ')'
      { HandlerLine = m_lexer.line + 1; }
      '=' RBRACKET ATTACKED_EVENT_END
    | SPELLED_EVENT_BEGIN HANDLER '(' n33 ')'
      { HandlerLine = m_lexer.line + 1; }
      '=' RBRACKET SPELLED_EVENT_END
    | TIMER_FIRED_EVENT_BEGIN HANDLER '(' n33 ')'
      { HandlerLine = m_lexer.line + 1; }
      '=' RBRACKET TIMER_FIRED_EVENT_END
    | TIMER_FIRED_EX_EVENT_BEGIN HANDLER '(' n33 ')'
      { HandlerLine = m_lexer.line + 1; }
      '=' RBRACKET TIMER_FIRED_EX_EVENT_END
    | CREATED_EVENT_BEGIN HANDLER '(' n33 ')'
      { HandlerLine = m_lexer.line + 1; }
      '=' RBRACKET CREATED_EVENT_END
    | MENU_SELECTED_EVENT_BEGIN HANDLER '(' n33 ')'
      { HandlerLine = m_lexer.line + 1; }
      '=' RBRACKET MENU_SELECTED_EVENT_END
      {
          const auto assignText = [](std::u16string& target, char16_t* value) {
              target.assign(value == nullptr ? u"" : value);
          };
          assignText(Handler, SemanticText($8));
          CQuestMenuSelectParser parser(this);
          parser.parse(g_QuestHelper);
      }
    | SCRIPT_EVENT_BEGIN HANDLER '(' n33 ')'
      { HandlerLine = m_lexer.line + 1; }
      '=' RBRACKET SCRIPT_EVENT_END
    | my_dying_event
    ;

my_dying_event
    : MY_DYING_EVENT_BEGIN HANDLER '(' n33 ')'
      { HandlerLine = m_lexer.line + 1; }
      '=' RBRACKET MY_DYING_EVENT_END
    | MY_DYING_EVENT_BEGIN CONDITION_TARGET '=' ALWAYS_GROUP CONDITION
      { ConditionLine = m_lexer.line + 1; }
      '=' RBRACKET EXECUTION_TYPE '=' ALWAYS_GROUP HANDLER '(' n33 ')'
      { HandlerLine = m_lexer.line + 1; }
      '=' RBRACKET MY_DYING_EVENT_END
    | MY_DYING_EVENT_BEGIN CONDITION_TARGET '=' ALWAYS_GROUP CONDITION_RANGE '=' ALWAYS_GROUP CONDITION
      { ConditionLine = m_lexer.line + 1; }
      '=' RBRACKET EXECUTION_TYPE '=' ALWAYS_GROUP HANDLER '(' n33 ')'
      { HandlerLine = m_lexer.line + 1; }
      '=' RBRACKET MY_DYING_EVENT_END
    ;

always_group_def
    : ALWAYS_GROUP
      { grammar::SetLow32($$, grammar::SemanticInt32($1)); }
    ;

n33
    : n33 ',' IDENTIFIER
      { WordSet.insert(std::u16string(SemanticText($3))); }
    | n33 ',' QUEST_ID
      { WordSet.insert(std::u16string(u"quest_id")); }
    | IDENTIFIER
      {
          WordSet.clear();
          WordSet.insert(std::u16string(SemanticText($1)));
      }
    | QUEST_ID
      {
          WordSet.clear();
          WordSet.insert(std::u16string(u"quest_id"));
      }
    | %empty
      { WordSet.clear(); }
    ;

%%
