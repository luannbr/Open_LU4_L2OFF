// CParserForShuttle grammar (original L2Server/parser_shuttle.y).
// Bison generates ParserShuttle.cpp and ParserShuttleTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserShuttle.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/Door.h"

#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <string_view>

#include "ParserShuttleTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForShuttle::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserShuttleTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

void CrashOnShuttleParserFailure() {
    ForceCrash(CrashInfo::LEX_YACC_FAILED);
}

} // namespace

CParserForShuttle::CParserForShuttle() : m_Node(), m_Pos{}, m_InnerDoor{}, m_lexer() {}

CParserForShuttle::~CParserForShuttle() = default;

int CParserForShuttle::Create() {
    return m_lexer.Create();
}

void CParserForShuttle::yysyntaxerror() {
    char16_t message[60]{};
    FormatUtf16To(message, std::size(message), u"Shuttle[%d] : syntax error", m_lexer.line + 1);
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

%token INTEGER
%token FLOAT_NUMBER
%token NAME
%token ELEVATOR_DATA_BEGIN
%token ELEVATOR_DATA_END
%token ELEVATOR_ID
%token ELEVATOR_TYPE
%token NODE_LIST_BEGIN
%token NODE_LIST_END
%token NODE_ID
%token STOP_POS
%token MOVE_SPEED
%token WAIT_TIME
%token OUTER_DOOR_NAME
%token BAN_POS
%token RELATED_INNER_DOOR_LIST
%token ELEVATOR_CONST_BEGIN
%token ELEVATOR_CONST_END
%token CONST_TYPE
%token CONST_RADIUS
%token INNER_DOOR_LIST_BEGIN
%token INNER_DOOR_LIST_END
%token INNER_DOOR_ID
%token OPEN_POS
%token CLOSE_POS
%token SAFE_POS
%start n1

%%

n1
    : elevator_const elevator_data
    ;

elevator_data
    : ELEVATOR_DATA_BEGIN elevator_id_def_list ELEVATOR_DATA_END
    ;

elevator_const
    : ELEVATOR_CONST_BEGIN const_type_def_list ELEVATOR_CONST_END
    ;

elevator_id_def_list
    : elevator_id_def_list elevator_id_def
    | elevator_id_def
    ;

const_type_def_list
    : const_type_def_list const_type_def
    | const_type_def
    ;

elevator_id_def
    : ELEVATOR_ID '=' INTEGER ELEVATOR_TYPE '=' INTEGER
      {
          m_pShuttle = CShuttleManager::Inst().CreateShuttle(grammar::SemanticInt32($3), grammar::SemanticInt32($6));
          if (m_pShuttle == nullptr) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Shuttle create failed! id [%d] type [%d]",
              //     grammar::SemanticInt32($3),
              //     grammar::SemanticInt32($6))
              L2_LOG_ERROR(
                  Log, u"Shuttle create failed! id [%d] type [%d]", grammar::SemanticInt32($3),
                  grammar::SemanticInt32($6)
              );
              CrashOnShuttleParserFailure();
              break;
          }
          m_nNodeCount = 0;
      }
      NODE_LIST_BEGIN node_id_def_list NODE_LIST_END
    ;

node_id_def_list
    : node_id_def_list node_id_def
    | node_id_def
    ;

node_id_def
    : NODE_ID '=' INTEGER WAIT_TIME '=' INTEGER MOVE_SPEED '=' INTEGER OUTER_DOOR_NAME '=' NAME STOP_POS '=' n20
      {
          m_Node.m_Type = CShuttlePathNode::MOVE;
          m_Node.m_vPos = m_Pos;
          if (m_nNodeCount != grammar::SemanticInt32($3)) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"shuttle node count invalid! id [%d]",
              //     grammar::SemanticInt32($3))
              L2_LOG_ERROR(Log, u"shuttle node count invalid! id [%d]", grammar::SemanticInt32($3));
          }
          m_Node.m_nWaitTime = grammar::SemanticInt32($6) * 1000;
          m_Node.m_nSpeed = grammar::SemanticInt32($9);
          const char16_t* const doorName = grammar::SemanticPointer<char16_t>($12);
          const std::wstring hostDoorName = target_utf16::ToHostCodeUnits(doorName);
          m_pDoor = g_DoorDB.GetDoor(doorName, false);
          if (m_pDoor == nullptr) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"shuttle outer door invalid! [%s]",
              //     hostDoorName.c_str())
              L2_LOG_ERROR(Log, u"shuttle outer door invalid! [%s]", hostDoorName.c_str());
          }
          m_Node.AddOuterDoor(m_pDoor);
      }
      BAN_POS '=' n20
      { m_Node.m_vBanPos = m_Pos; }
      RELATED_INNER_DOOR_LIST '=' '{' n21 '}'
      {
          m_pShuttle->AddPathNode(m_Node);
          ++m_nNodeCount;
          m_Node.m_vInnerDoorList.clear();
      }
    ;

const_type_def
    : CONST_TYPE '=' INTEGER CONST_RADIUS '=' integer_def
      {
          m_pShuttleConst =
              CShuttleManager::Inst().CreateShuttleConst(grammar::SemanticInt32($3), grammar::SemanticDouble($6));
          if (m_pShuttleConst == nullptr) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[%d] Shuttle const create failed!",
              //     grammar::SemanticInt32($3))
              L2_LOG_ERROR(Log, u"[%d] Shuttle const create failed!", grammar::SemanticInt32($3));
              CrashOnShuttleParserFailure();
          }
      }
      INNER_DOOR_LIST_BEGIN inner_door_id_def_list INNER_DOOR_LIST_END
    ;

inner_door_id_def_list
    : inner_door_id_def_list inner_door_id_def
    | inner_door_id_def
    ;

inner_door_id_def
    : INNER_DOOR_ID '=' INTEGER
      { m_InnerDoor.id = grammar::SemanticInt32($3); }
      OPEN_POS '=' n20
      { m_InnerDoor.open_pos = m_Pos; }
      CLOSE_POS '=' n20
      { m_InnerDoor.close_pos = m_Pos; }
      SAFE_POS '=' n20
      {
          m_InnerDoor.safe_pos = m_Pos;
          m_pShuttleConst->AddInnerDoor(m_InnerDoor);
      }
    ;

integer_def
    : INTEGER
      {
          $$ = grammar::DoubleSemantic(static_cast<double>(grammar::SemanticInt64($1)));
      }
    | FLOAT_NUMBER
      { $$ = $1; }
    ;

n20
    : '{' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          m_Pos = FVector{
              static_cast<double>(grammar::SemanticInt32($2)), static_cast<double>(grammar::SemanticInt32($4)),
              static_cast<double>(grammar::SemanticInt32($6))
          };
      }
    ;

n21
    : n21 ';' INTEGER
      { m_Node.m_vInnerDoorList.push_back(grammar::SemanticInt32($3)); }
    | INTEGER
      { m_Node.m_vInnerDoorList.push_back(grammar::SemanticInt32($1)); }
    ;

%%
