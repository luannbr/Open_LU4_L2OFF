// CParserForAirShip grammar (original L2Server/parser_airship.y).
// Bison generates ParserAirShip.cpp and ParserAirShipTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserAirShip.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ObjectDB.h"

#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <string_view>

#include "ParserAirShipTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForAirShip::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserAirShipTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

void CrashOnInvalidRegularAirport() {
    ForceCrash(CrashInfo::LEX_YACC_FAILED);
}

} // namespace

CParserForAirShip::CParserForAirShip() :
    m_lexer(), m_Pos(), m_PosList(), m_pAirport(nullptr), m_pPlatform(nullptr), m_pArea(nullptr) {
}

CParserForAirShip::~CParserForAirShip() = default;

int CParserForAirShip::Create(char16_t* buffer, std::int32_t size) {
    return m_lexer.Create(buffer, size);
}
}

%token AIRPORT_BEGIN
%token AIRPORT_END
%token AIRPORT_TYPE
%token REGULAR
%token FREE
%token AIRSHIP_POS
%token GETOFF_POS
%token STOPOVER
%token TELEPORT_BEGIN
%token TELEPORT_END
%token FUEL
%token TEL_POS
%token PLATFORM_BEGIN
%token PLATFORM_END
%token LANDING_PATH
%token TAKEOFF_PATH
%token ID
%token AIRSHIP_BEGIN
%token AIRSHIP_END
%token AIRSHIP_TYPE
%token REGULAR_2
%token SPEED
%token COLLISION
%token MAX_PASSENGER
%token START_POS
%token PATH
%token RETURN_TO_START
%token MOVE
%token TEL
%token AIRPORT
%token STEERING_BEGIN
%token STEERING_END
%token STEERING_POS
%token DRIVER_POS
%token STEERING_ITEM
%token FUEL_MAX
%token FUEL_CONSUME
%token BUY_COST
%token SUMMON_COST
%token AIRSHIP_AREA_BEGIN
%token AIRSHIP_AREA_END
%token MAP_NO
%token AREA_TYPE
%token AREA_RANGE
%token NAME
%token EXPEL_GROUP
%token FLOAT_NUMBER
%start n1

%%

n1
    : airport_list airship_list n55
    ;

airport_list
    : airport_list airport
    | airport
    ;

airport
    : AIRPORT_BEGIN n4 AIRPORT_END
      {
          CAirShipManager::Inst().m_AirportMap.emplace(m_pAirport->m_Id, m_pAirport);
          m_pAirport = nullptr;
      }
    ;

n4
    : id_def
    | id_def_2
    ;

id_def
    : ID '=' EXPEL_GROUP AIRPORT_TYPE '=' REGULAR
      {
          const int id = grammar::SemanticInt32($3);
          if (id < 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"airport id[%d] must be >= 0. at line[%d]",
              //     id,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"airport id[%d] must be >= 0. at line[%d]",
              //     id,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Invalid regular airport ID %d at script line %lld; expected a nonnegative ID, but construction "
                  "continues with this ID.",
                  id, static_cast<long long>(m_lexer.line) + 1
              );
          }
          m_pAirport = new CAirportForRegular(id);
      }
      AIRSHIP_POS '=' n52
      { m_pAirport->CastForRegular()->m_AirShipPos = m_Pos; }
      GETOFF_POS '=' '{' n8 '}' stopover_def
    ;

n8
    : n8 ';' n9
    | n9
    ;

n9
    : '{' EXPEL_GROUP ';' n52 '}'
      {
          m_pAirport->AddGetOffInfo(m_pPlatform, m_Pos, grammar::SemanticInt32($2), m_lexer.line + 1);
      }
    | '{' n52 '}'
      { m_pAirport->AddGetOffInfo(m_pPlatform, m_Pos, 0, m_lexer.line + 1); }
    ;

stopover_def
    : STOPOVER '=' EXPEL_GROUP
      { m_pAirport->m_Stopover = grammar::SemanticInt32($3); }
    ;

id_def_2
    : ID '=' EXPEL_GROUP AIRPORT_TYPE '=' FREE
      { m_pAirport = new CAirportForFree(grammar::SemanticInt32($3)); }
      teleport platform_list stopover_def
    ;

teleport
    : TELEPORT_BEGIN id_def_3_list TELEPORT_END
    ;

id_def_3_list
    : id_def_3_list id_def_3
    | id_def_3
    ;

id_def_3
    : ID '=' EXPEL_GROUP FUEL '=' EXPEL_GROUP TEL_POS '=' n52
      {
          const int id = grammar::SemanticInt32($3);
          if (id < 0 && id != -1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"teleport id[%d] must be >= 0. at line[%d]",
              //     id,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"teleport id[%d] must be >= 0. at line[%d]",
              //     id,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Invalid airport teleport ID %d at script line %lld; expected -1 or a nonnegative ID, but construction "
                  "continues with this ID.",
                  id, static_cast<long long>(m_lexer.line) + 1
              );
          }
          auto* const teleport = new CAirportTeleport(id);
          teleport->m_FuelConsume = grammar::SemanticInt32($6);
          teleport->m_Pos = m_Pos;
          m_pAirport->CastForFree()->m_TeleportMap.emplace(id, teleport);
      }
    ;

platform_list
    : platform_list platform
    | platform
    ;

platform
    : PLATFORM_BEGIN ID '=' EXPEL_GROUP
      {
          const int id = grammar::SemanticInt32($4);
          if (id < 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"platform id[%d] must be >= 0. at line[%d]",
              //     id,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"platform id[%d] must be >= 0. at line[%d]",
              //     id,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Invalid airport platform ID %d at script line %lld; expected a nonnegative ID, but construction "
                  "continues with this ID.",
                  id, static_cast<long long>(m_lexer.line) + 1
              );
          }
          m_pPlatform = new CAirportPlatform(id);
      }
      LANDING_PATH '=' '{' n53 '}'
      { m_pPlatform->SetLandingPath(m_PosList); }
      AIRSHIP_POS '=' n52
      { m_pPlatform->SetShipPos(m_Pos); }
      TAKEOFF_PATH '=' '{' n53 '}'
      { m_pPlatform->SetTakeOffPath(m_PosList); }
      GETOFF_POS '=' n9 PLATFORM_END
      {
          m_pAirport->CastForFree()->AddPlatform(m_pPlatform);
          m_pPlatform = nullptr;
      }
    ;

airship_list
    : airship_list airship
    | airship
    ;

airship
    : AIRSHIP_BEGIN n24 AIRSHIP_END
      {
          CAirShipManager::Inst().AddConst(m_AirShipConst);
          m_AirShipConst = nullptr;
      }
    ;

n24
    : id_def_4
    | id_def_5
    ;

id_def_4
    : ID '=' EXPEL_GROUP AIRSHIP_TYPE '=' REGULAR_2
      {
          const int id = grammar::SemanticInt32($3);
          if (id < 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"airship id[%d] must be >= 0. line[%d]",
              //     id,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"airship id[%d] must be >= 0. line[%d]",
              //     id,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Invalid regular airship ID %d at script line %lld; expected a nonnegative ID, but construction "
                  "continues with this ID.",
                  id, static_cast<long long>(m_lexer.line) + 1
              );
          }
          m_AirShipConst = new CAirShipConstRegular(id);
          m_AirShipConst->SetType(ASTE_REGULAR);
      }
      speed_def start_pos_def path_def return_to
    ;

speed_def
    : SPEED '=' '{' EXPEL_GROUP ';' EXPEL_GROUP ';' EXPEL_GROUP '}'
      {
          m_AirShipConst->SetSpeed(0, static_cast<double>(grammar::SemanticInt32($4)));
          m_AirShipConst->SetSpeed(1, static_cast<double>(grammar::SemanticInt32($6)));
          m_AirShipConst->SetSpeed(2, static_cast<double>(grammar::SemanticInt32($8)));
      }
      collision_def max_passenger_def
    ;

collision_def
    : COLLISION '=' '{' n30 '}'
    ;

n30
    : n30 ';' n31
    | n31
    ;

n31
    : '{' n52 ';' expel_group_def ';' expel_group_def '}'
      {
          m_AirShipConst->AddCollision(m_Pos, grammar::SemanticDouble($4), grammar::SemanticDouble($6));
      }
    ;

max_passenger_def
    : MAX_PASSENGER '=' EXPEL_GROUP
      { m_AirShipConst->SetMaxPassenger(grammar::SemanticInt32($3)); }
    | %empty
    ;

start_pos_def
    : START_POS '=' '{' n52 ';' EXPEL_GROUP '}'
      {
          AirShipPathNode node;
          node.nodeType = ASPNT_START;
          node.pos = m_Pos;
          node.dir = grammar::SemanticInt32($6);
          m_AirShipConst->CastForRegular()->AddPathNode(node);
      }
    ;

path_def
    : PATH '=' '{' n35 '}'
    ;

n35
    : n35 ';' '{' n36 '}'
    | '{' n36 '}'
    ;

n36
    : move_def
    | tel_def
    | airport_def
    ;

move_def
    : MOVE ';' n52
      {
          AirShipPathNode node;
          node.nodeType = ASPNT_MOVE;
          node.pos = m_Pos;
          node.dir = 0;
          m_AirShipConst->CastForRegular()->AddPathNode(node);
      }
    ;

tel_def
    : TEL ';' n52 ';' EXPEL_GROUP
      {
          AirShipPathNode node;
          node.nodeType = ASPNT_TELEPORT;
          node.pos = m_Pos;
          node.dir = grammar::SemanticInt32($5);
          m_AirShipConst->CastForRegular()->AddPathNode(node);
      }
    ;

airport_def
    : AIRPORT ';' EXPEL_GROUP
      {
          const int airportId = grammar::SemanticInt32($3);
          CAirport* const airport = CAirShipManager::Inst().GetAirport(airportId);
          if (airport == nullptr || !airport->IsForRegular()) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"airport[%d] is not regular type at line[%d]",
              //     airportId,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"airport[%d] is not regular type at line[%d]",
              //     airportId,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Airship route references missing or non-regular airport %d at script line %lld; invoking the fatal "
                  "parser failure handler.",
                  airportId, static_cast<long long>(m_lexer.line) + 1
              );
              CrashOnInvalidRegularAirport();
              break;
          }
          AirShipPathNode node;
          node.nodeType = ASPNT_INTO_AIRPORT;
          node.pos = airport->CastForRegular()->GetAirShipPos();
          node.dir = airportId;
          m_AirShipConst->CastForRegular()->AddPathNode(node);
      }
    ;

return_to
    : RETURN_TO_START '=' EXPEL_GROUP
      {
          m_AirShipConst->CastForRegular()->SetReturnInfo(static_cast<AirShipPathNodeType>(grammar::SemanticInt32($3)));
      }
    ;

id_def_5
    : ID '=' EXPEL_GROUP AIRSHIP_TYPE '=' EXPEL_GROUP
      {
          const int id = grammar::SemanticInt32($3);
          if (id < 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"airship id[%d] must be >= 0",
              //     id,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"airship id[%d] must be >= 0",
              //     id,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Invalid free airship ID %d at script line %lld; expected a nonnegative ID, but construction continues "
                  "with this ID.",
                  id, static_cast<long long>(m_lexer.line) + 1
              );
          }
          m_AirShipConst = new CAirShipConstFree(id);
          m_AirShipConst->SetType(static_cast<AirShipTypeEnum>(grammar::SemanticInt32($6)));
      }
      speed_def n43
    ;

n43
    : steering fuel_max_def buy_cost_def summon_cost_def
    ;

steering
    : STEERING_BEGIN STEERING_POS '=' n52
      { m_AirShipConst->CastForFree()->SetSteeringPos(m_Pos); }
      DRIVER_POS '=' n52
      { m_AirShipConst->CastForFree()->SetDriverPos(m_Pos); }
      STEERING_ITEM '=' NAME
      { m_AirShipConst->CastForFree()->SetSteeringItem(grammar::SemanticString($12)); }
      STEERING_END
    ;

fuel_max_def
    : FUEL_MAX '=' EXPEL_GROUP FUEL_CONSUME '=' EXPEL_GROUP
      {
          m_AirShipConst->CastForFree()->SetFuel(grammar::SemanticInt32($3), grammar::SemanticInt32($6));
      }
    ;

buy_cost_def
    : BUY_COST '=' '{' NAME ';' EXPEL_GROUP '}'
      {
          const char16_t* const itemName = grammar::SemanticString($4);
          CObject* const object = g_ObjectDB.GetObjectByName(itemName);
          if (object == nullptr || !object->IsItem()) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined Item[%s] in line[%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Undefined Item[%s] in line[%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Cannot set airship purchase cost at script line %lld: '%s' is missing or is not an item; the cost "
                  "remains unchanged.",
                  static_cast<long long>(m_lexer.line) + 1,
                  Utf16ToUtf8(std::u16string_view(itemName != nullptr ? itemName : u"<null>")).c_str()
              );
              break;
          }
          const int count = grammar::SemanticInt32($6);
          if (count <= 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Number of the item[%d] must be over 0 in line[%d]",
              //     count,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Number of the item[%d] must be over 0 in line[%d]",
              //     count,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Cannot set airship purchase cost for item '%s' at script line %lld: count %d must be positive; the "
                  "cost remains unchanged.",
                  Utf16ToUtf8(std::u16string_view(itemName != nullptr ? itemName : u"<null>")).c_str(),
                  static_cast<long long>(m_lexer.line) + 1, count
              );
              break;
          }
          m_AirShipConst->CastForFree()->SetBuyCost(itemName, count);
      }
    | %empty
    ;

summon_cost_def
    : SUMMON_COST '=' '{' NAME ';' EXPEL_GROUP '}'
      {
          const char16_t* const itemName = grammar::SemanticString($4);
          CObject* const object = g_ObjectDB.GetObjectByName(itemName);
          if (object == nullptr || !object->IsItem()) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined Item[%s] in line[%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Undefined Item[%s] in line[%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Cannot set airship summon cost at script line %lld: '%s' is missing or is not an item; the cost "
                  "remains unchanged.",
                  static_cast<long long>(m_lexer.line) + 1,
                  Utf16ToUtf8(std::u16string_view(itemName != nullptr ? itemName : u"<null>")).c_str()
              );
              break;
          }
          const int count = grammar::SemanticInt32($6);
          if (count <= 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Number of the item[%d] must be over 0 in line[%d]",
              //     count,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Number of the item[%d] must be over 0 in line[%d]",
              //     count,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Cannot set airship summon cost for item '%s' at script line %lld: count %d must be positive; the cost "
                  "remains unchanged.",
                  Utf16ToUtf8(std::u16string_view(itemName != nullptr ? itemName : u"<null>")).c_str(),
                  static_cast<long long>(m_lexer.line) + 1, count
              );
              break;
          }
          m_AirShipConst->CastForFree()->SetSummonCost(itemName, count);
      }
    | %empty
    ;

expel_group_def
    : EXPEL_GROUP
      {
          $$ = grammar::DoubleSemantic(static_cast<double>(grammar::SemanticInt64($1)));
      }
    | FLOAT_NUMBER
      { $$ = $1; }
    ;

n52
    : '{' EXPEL_GROUP ';' EXPEL_GROUP ';' EXPEL_GROUP '}'
      {
          m_Pos = FVector{
              static_cast<double>(grammar::SemanticInt32($2)), static_cast<double>(grammar::SemanticInt32($4)),
              static_cast<double>(grammar::SemanticInt32($6))
          };
      }
    ;

n53
    : n53 ';' n54
    | n54
    ;

n54
    : n52
      { m_PosList.push_back(m_Pos); }
    ;

n55
    : airship_area
    | %empty
    ;

airship_area
    : AIRSHIP_AREA_BEGIN id_def_6_list AIRSHIP_AREA_END
    ;

id_def_6_list
    : id_def_6_list id_def_6
    | id_def_6
    ;

id_def_6
    : ID '=' EXPEL_GROUP
      { m_pArea = new CAirShipArea(grammar::SemanticInt32($3)); }
      map_no_def area_type_def AREA_RANGE '=' '{' n62 '}'
      {
          CAirShipAreaDB::Inst().AddArea(m_pArea);
          m_pArea = nullptr;
      }
    ;

map_no_def
    : MAP_NO '=' '{' EXPEL_GROUP ';' EXPEL_GROUP '}'
      {
          m_pArea->m_MapX = grammar::SemanticInt32($4);
          m_pArea->m_MapY = grammar::SemanticInt32($6);
      }
    ;

area_type_def
    : AREA_TYPE '=' '{' EXPEL_GROUP ';' EXPEL_GROUP '}'
      {
          m_pArea->m_Type = static_cast<AirShipAreaType>(grammar::SemanticInt32($4));
          m_pArea->m_Option = grammar::SemanticInt32($6);
      }
    ;

n62
    : n62 ';' n63
    | n63
    ;

n63
    : '{' EXPEL_GROUP ';' EXPEL_GROUP ';' EXPEL_GROUP ';' EXPEL_GROUP '}'
      {
          const int bottom = grammar::SemanticInt32($6);
          const int top = grammar::SemanticInt32($8);
          if (bottom < m_pArea->m_Territory.GetBottom()) {
              m_pArea->m_Territory.SetBottom(bottom);
          }
          if (m_pArea->m_Territory.GetTop() < top) {
              m_pArea->m_Territory.SetTop(top);
          }
          FVector point{
              static_cast<double>(grammar::SemanticInt32($2)), static_cast<double>(grammar::SemanticInt32($4)), 0.0
          };
          m_pArea->m_Territory.AddTerritoryPoint(&point);
      }
    ;

%%
