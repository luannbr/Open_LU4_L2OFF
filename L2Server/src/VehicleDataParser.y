// CParserForVehicle grammar (original L2Server/vehicledata_parser.y).
// Bison generates VehicleDataParser.cpp and VehicleDataParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/VehicleDataParser.h"

#include "L2Server/src/ObjectDB.h"

#include <cstdint>
#include <cstring>
#include <string_view>

#include "VehicleDataParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForVehicle::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"VehicleDataParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

CParserForVehicle::CParserForVehicle() : m_lexer() {}

CParserForVehicle::~CParserForVehicle() = default;

int CParserForVehicle::Create(const wchar_t* fileName) {
    return m_lexer.Create(fileName);
}
}

%token EQUAL
%token VEHICLE_BEGIN
%token VEHICLE_END
%token NAME
%token CAPTAIN
%token ROUTE_BEGIN
%token ROUTE_END
%token STATION_BEGIN
%token STATION_END
%token ACTION_BEGIN
%token ACTION_END
%token MESSAGE
%token DEPARTURE
%token CHECK_TICKET
%token WAIT
%token SET_KICK_POS
%token PLAY_SOUND
%token STATION_ID
%token POINT
%token BROADCAST_POINT
%token NONE
%token NAME_2
%token INTEGER
%start n1

%%

n1
    : vehicle_list
    ;

vehicle_list
    : vehicle_list vehicle
    | %empty
    ;

vehicle
    : VEHICLE_BEGIN
      {
          m_pVehicle = new CVehicle;
          m_pController = new CVehicleController;
      }
      name_def_list VEHICLE_END
      {
          m_pVehicle->SetController(m_pController);
          m_pController->SetVehicle(m_pVehicle);
      }
    ;

name_def_list
    : name_def_list name_def
    | name_def
    ;

name_def
    : NAME EQUAL NAME_2
      { g_VehicleDB.AddVehicle(grammar::SemanticString($3), m_pVehicle); }
    | CAPTAIN EQUAL '{' NAME_2 ';' '{' INTEGER ';' INTEGER ';' INTEGER '}' '}'
      {
          m_pController->SetCaptainId(
              g_ObjectDB.GetClassIdFromName(grammar::SemanticString($4), false),
              FVector{
                  static_cast<double>(grammar::SemanticInt32($7)), static_cast<double>(grammar::SemanticInt32($9)),
                  static_cast<double>(grammar::SemanticInt32($11))
              }
          );
      }
    | route
    | BROADCAST_POINT EQUAL '{' '{' INTEGER ';' INTEGER ';' INTEGER '}' ';' '{' INTEGER ';' INTEGER ';' INTEGER '}' '}'
      {
          m_pController->SetBroadcastPoint(
              FVector{
                  static_cast<double>(grammar::SemanticInt32($5)), static_cast<double>(grammar::SemanticInt32($7)),
                  static_cast<double>(grammar::SemanticInt32($9))
              },
              FVector{
                  static_cast<double>(grammar::SemanticInt32($13)), static_cast<double>(grammar::SemanticInt32($15)),
                  static_cast<double>(grammar::SemanticInt32($17))
              }
          );
      }
    ;

route
    : ROUTE_BEGIN station_list ROUTE_END
    ;

station_list
    : station
    | station_list station
    ;

station
    : STATION_BEGIN STATION_ID EQUAL INTEGER POINT EQUAL '{' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          m_pStation = new CVehicleStationInfo;
          m_pStation->m_vPoint = FVector{
              static_cast<double>(grammar::SemanticInt32($8)), static_cast<double>(grammar::SemanticInt32($10)),
              static_cast<double>(grammar::SemanticInt32($12))
          };
          m_pStation->m_nStationID = grammar::SemanticInt32($4);
      }
      action STATION_END
      { m_pController->AddStation(m_pStation); }
    ;

n11
    : n11 ';' INTEGER
      { m_pMessageList->push_back(grammar::SemanticInt32($3)); }
    | INTEGER
      { m_pMessageList->push_back(grammar::SemanticInt32($1)); }
    ;

n12
    : '{'
      { m_pMessageList = &m_pAction->m_vMessageID; }
      n11 '}'
      { m_pMessageList = nullptr; }
    | '{' '}'
    ;

integer_def
    : INTEGER ';' n12 ';' INTEGER
      {
          m_pAction->m_nSpeakerID = grammar::SemanticInt32($1);
          m_pAction->m_nNextActionTime = grammar::SemanticInt32($5);
      }
    ;

action
    : ACTION_BEGIN message_def_list ACTION_END
    ;

message_def_list
    : message_def
    | message_def_list message_def
    ;

n17
    : '{' NAME_2 ';' '{' INTEGER ';' INTEGER ';' INTEGER '}' ';' INTEGER '}'
      {
          m_pAction->m_nTicketID = g_ObjectDB.GetClassIdFromName(grammar::SemanticString($2), false);
          m_pAction->m_vBanningPos = FVector{
              static_cast<double>(grammar::SemanticInt32($5)), static_cast<double>(grammar::SemanticInt32($7)),
              static_cast<double>(grammar::SemanticInt32($9))
          };
          m_pAction->m_nNextActionTime = grammar::SemanticInt32($12);
      }
    | NONE
      { m_pAction->m_nTicketID = 0; }
    ;

message_def
    : MESSAGE
      {
          m_pAction = new VehicleActionInfo;
          m_pAction->m_eType = VATE_MESSAGE;
      }
      EQUAL '{' integer_def '}'
      { m_pStation->AddActionInfo(m_pAction); }
    | DEPARTURE EQUAL '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          m_pAction = new VehicleActionInfo;
          m_pAction->m_eType = VATE_DEPARTURE;
          m_pAction->m_nMovingSpeed = grammar::SemanticInt32($4);
          m_pAction->m_nRotatingSpeed = grammar::SemanticInt32($6);
          m_pAction->m_nNextActionTime = grammar::SemanticInt32($8);
          m_pAction->m_nSpeakerID = grammar::SemanticInt32($10);
          m_pAction->m_nWaitingMessageID = grammar::SemanticInt32($12);
          // Current L2Server 0x00ccfbbf..0x00ccfd1e reaches the common
          // CVehicleStationInfo::AddActionInfo tail after filling this object.
          m_pStation->AddActionInfo(m_pAction);
      }
    | CHECK_TICKET EQUAL
      {
          m_pAction = new VehicleActionInfo;
          m_pAction->m_eType = VATE_CHECK_TICKET;
      }
      n17
      { m_pStation->AddActionInfo(m_pAction); }
    | WAIT EQUAL '{' INTEGER '}'
      {
          m_pAction = new VehicleActionInfo;
          m_pAction->m_eType = VATE_WAIT;
          m_pAction->m_nNextActionTime = grammar::SemanticInt32($4);
          // Current L2Server reaches the same common AddActionInfo tail.
          m_pStation->AddActionInfo(m_pAction);
      }
    | SET_KICK_POS EQUAL '{' '{' INTEGER ';' INTEGER ';' INTEGER '}' ';' INTEGER '}'
      {
          m_pAction = new VehicleActionInfo;
          m_pAction->m_eType = VATE_SET_KICK_POS;
          m_pAction->m_vBanningPos = FVector{
              static_cast<double>(grammar::SemanticInt32($5)), static_cast<double>(grammar::SemanticInt32($7)),
              static_cast<double>(grammar::SemanticInt32($9))
          };
          m_pAction->m_nNextActionTime = grammar::SemanticInt32($12);
          // Current L2Server reaches the same common AddActionInfo tail.
          m_pStation->AddActionInfo(m_pAction);
      }
    | PLAY_SOUND EQUAL '{' NAME_2 ';' INTEGER '}'
      {
          m_pAction = new VehicleActionInfo;
          m_pAction->m_eType = VATE_PLAY_SOUND;
          m_pAction->m_sFileName = const_cast<char16_t*>(grammar::SemanticString($4));
          m_pAction->m_nNextActionTime = grammar::SemanticInt32($6);
          // Current L2Server reaches the same common AddActionInfo tail.
          m_pStation->AddActionInfo(m_pAction);
      }
    ;

// The original tables have rule 36 that no parser state reduces.
// It is left out.

%%
