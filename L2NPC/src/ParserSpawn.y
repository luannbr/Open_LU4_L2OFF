// npcpos.txt grammar (original L2NPC/parser_spawn.y). The productions match
// Shared/src/ParserSpawn.y.
// Bison generates ParserSpawn.cpp and ParserSpawnTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2NPC/src/ParserSpawn.h"

#include "L2NPC/src/NpcMaker.h"
#include "L2NPC/src/NpcMakerDB.h"
#include "L2NPC/src/NpcMakerEx.h"
#include "L2NPC/src/NpcSpawnDefineEx.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Runtime.h"
#include "Shared/src/Log.h"
#include "TextRuntime.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include "ParserSpawnTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse l2npc::CParserForNPCSpawn::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserSpawnTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace l2npc {
namespace {

char16_t kEmptyDatabaseName[] = u"";

using SpawnSemanticBits = grammar::SemanticBits;

std::int64_t SemanticInt64(SpawnSemanticBits bits) {
    if (bits <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return static_cast<std::int64_t>(bits);
    }
    return -1 - static_cast<std::int64_t>(~bits);
}

int ScaleSeconds32(int value, int unit) {
    switch (unit) {
        case 0:
            return value;
        case 1:
            return runtime::MultiplyInt32Wrapping(value, 60U);
        case 2:
            return runtime::MultiplyInt32Wrapping(value, 3600U);
        case 3:
            return runtime::MultiplyInt32Wrapping(value, 86400U);
        default:
            return value;
    }
}

std::uint64_t ScaleSeconds64Bits(std::uint64_t value, int unit) {
    switch (unit) {
        case 0:
            return value;
        case 1:
            return value * UINT64_C(60);
        case 2:
            return value * UINT64_C(3600);
        case 3:
            return value * UINT64_C(86400);
        default:
            return value;
    }
}

// void LogUndefinedTerritory(
//     const char16_t* name) {
void LogUndefinedTerritory(const char16_t* name, int line) {
    const std::string hostName = Utf16ToUtf8(name);
    // Log.Add(
    //     LOG_ERROR,
    //     L"Error : Territory Undefined! [%s]",
    //     hostName.c_str())
    // Log.Add(
    //     LOG_ERROR,
    //     "[NPC spawn] Line %d: undefined territory \"%s\". The territory reference was not added.",
    //     line, WideToUtf8(hostName).c_str())
    L2_LOG_ERROR(
        Log, "[NPC spawn] Line %d: undefined territory \"%s\"; the territory reference was not added.", line,
        hostName.c_str()
    );
}

// void LogInvalidTerritoryHeight(
//     const char16_t* name) {
void LogInvalidTerritoryHeight(const char16_t* name, int line, int bottom, int top) {
    const std::string hostName = Utf16ToUtf8(name);
    // Log.Add(
    //     LOG_ERROR,
    //     L"territory [%s] z1 < z2",
    //     hostName.c_str())
    // Log.Add(
    //     LOG_ERROR,
    //     "[NPC spawn] Line %d: territory \"%s\" has top %d <= bottom %d. Parsing continues with the supplied bounds.",
    //     line, WideToUtf8(hostName).c_str(), top, bottom)
    L2_LOG_ERROR(
        Log, "[NPC spawn] Line %d: territory \"%s\" has top %d <= bottom %d; parsing continues with the supplied bounds.",
        line, hostName.c_str(), top, bottom
    );
}

// void LogOnDeleteCreateTooBigWithName(
//     int seconds,
//     const char16_t* name) {
void LogOnDeleteCreateTooBigWithName(int seconds, const char16_t* name, int line) {
    // Log.Add(
    //     LOG_ERROR,
    //     L"on_delete_create respawn time is too big "
    //     L"(%d) (%S)!!!",
    //     seconds,
    //     reinterpret_cast<const char*>(name))
    // Log.Add(
    //     LOG_ERROR,
    //     "[NPC spawn] Line %d: on_delete_create delay exceeds the supported range (low 32-bit seconds %d, NPC \"%s\"). This
    //     respawn setting was not applied.", line, seconds, Utf16ToUtf8(name).c_str())
    L2_LOG_ERROR(
        Log,
        "[NPC spawn] Line %d: on_delete_create delay exceeds the supported range (low 32-bit seconds %d, NPC \"%s\"); this "
        "respawn setting was not applied.",
        line, seconds, Utf16ToUtf8(name).c_str()
    );
}

// void LogOnDeleteCreateTooBig(
//     int seconds) {
void LogOnDeleteCreateTooBig(int seconds, int line) {
    // Log.Add(
    //     LOG_ERROR,
    //     L"on_delete_create respawn time is too big "
    //     L"(%d) !!!",
    //     seconds)
    // Log.Add(
    //     LOG_ERROR,
    //     "[NPC spawn] Line %d: on_delete_create delay exceeds the supported range (low 32-bit seconds %d). This respawn
    //     setting was not applied.", line, seconds)
    L2_LOG_ERROR(
        Log,
        "[NPC spawn] Line %d: on_delete_create delay exceeds the supported range (low 32-bit seconds %d); this respawn "
        "setting was not applied.",
        line, seconds
    );
}

bool IsSiegeRelatedSpawnTime(SpawnTimeType spawnTimeType) {
    return (spawnTimeType > SPAWN_NORMAL && spawnTimeType < SPAWN_DOOR_OPEN) ||
        (spawnTimeType > SPAWN_DOOR_OPEN && spawnTimeType < SPAWN_POS_EVENT);
}

int FindMakerDomain(CNpcMakerDB& database, TerritoryInfo& territoryInfo) {
    CTerritory* const territory = territoryInfo.GetTerritory(0);
    return database.FindDomain(territory->GetCenterPos());
}

} // namespace

CParserForNPCSpawn::CNpcMakerInfoForChecker::CNpcMakerInfoForChecker() { Clear(); }

void CParserForNPCSpawn::CNpcMakerInfoForChecker::Clear() {
    m_TerritoryInfo.Clear();
    m_bAnyWhere = false;
    m_bFlying = false;
}

bool CParserForNPCSpawn::CNpcMakerInfoForChecker::Verify() { return m_TerritoryInfo.InitializeRandomPos(m_bFlying, 400); }

CParserForNPCSpawn::CParserForNPCSpawn() {
    pNpcMakerEx = nullptr;
    pNpcSpawnDefineEx = nullptr;
    mCheckAI = false;
    m_bOnlyMakerEventList = false;
}

CParserForNPCSpawn::~CParserForNPCSpawn() = default;

int CParserForNPCSpawn::Create(CNpcMakerDB* npcMakerDatabase, std::uint16_t* text, int codeUnitCount) {
    m_pNpcMakerDB = npcMakerDatabase;
    return m_lexer.Create(text, codeUnitCount);
}

void CParserForNPCSpawn::CheckAI(bool check) { mCheckAI = check; }

} // namespace l2npc
}

%token FLOAT_NUMBER
%token TERRITORY_BEGIN
%token TERRITORY_END
%token DOMAIN_BEGIN
%token DOMAIN_END
%token DOMAIN_ID
%token NPCMAKER_BEGIN
%token NPCMAKER_END
%token BANNED_TERRITORY
%token INZONE_CLASSID
%token NPC_BEGIN
%token NPC_END
%token NPCMAKER_EX_BEGIN
%token NPCMAKER_EX_END
%token NPC_EX_BEGIN
%token NPC_EX_END
%token INTEGER
%token INITIAL_SPAWN
%token MAXIMUM_NPC
%token FLYING
%token YES
%token NO
%token AI
%token ALL
%token NICKNAME
%token WAYPOINTS
%token WAYPOINTDELAYS
%token PRIVATES
%token AI_PARAMETERS
%token RANDOM3
%token SPAWN_TIME
%token SIEGE_WARFARE_START
%token PC_SIEGE_WARFARE_START
%token EXTENDED_SIEGE_WARFARE_START
%token DEATH_TIME
%token PARAMETERS
%token DBNAME
%token DBSAVING
%token BRACKETED_NAME
%token TIME_UNIT
%token ANYWHERE
%token POS
%token TOTAL
%token RESPAWN
%token RESPAWN_RAND
%token NAME
%token DOOR_OPEN
%token AGIT_ATTACK_WARFARE_START
%token AGIT_DEFEND_WARFARE_START
%token AGIT_BATTLE_ROYAL_START
%token AGIT_FINAL_START
%token TEAM_COLOR
%token FINAL_SIDE
%token ON_DELETE_CREATE
%token EVENT_NAME
%token BOSS_RESPAWN_SET
%token IS_CHASE_PC
%start spawn_file

%%

spawn_file
    : spawn_definitions
    ;

spawn_definitions
    : spawn_definitions spawn_definition
    | spawn_definition
    ;

spawn_definition
    : territory
    | npcmaker
    | npcmaker_ex
    | domain
    ;

domain
    : DOMAIN_BEGIN BRACKETED_NAME
      {
          pTempTerritory = new CTerritory();
          pTempTerritory->sName = std::u16string(grammar::SemanticString($2));
      }
      DOMAIN_ID '=' INTEGER '{' point_list '}' DOMAIN_END
      {
          m_pNpcMakerDB->AddDomainTerritory(grammar::SemanticInt32($6), pTempTerritory);
      }
    ;

territory
    : TERRITORY_BEGIN BRACKETED_NAME
      {
          pTempTerritory = new CTerritory();
          pTempTerritory->sName = std::u16string(grammar::SemanticString($2));
      }
      '{' point_list '}' TERRITORY_END
      {
          m_pNpcMakerDB->InsertTerritory(pTempTerritory);
          m_TerritoryDBForChecker.emplace(pTempTerritory->GetNameString(), pTempTerritory);
          pTempTerritory = nullptr;
      }
    ;

point_list
    : point_list ';' point
      {
          pTempTerritory->AddTerritoryPoint(reinterpret_cast<FVector*>(static_cast<std::uintptr_t>($3)));
      }
    | point
      {
          pTempTerritory->AddTerritoryPoint(reinterpret_cast<FVector*>(static_cast<std::uintptr_t>($1)));
      }
    ;

point
    : '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          const int x = grammar::SemanticInt32($2);
          const int y = grammar::SemanticInt32($4);
          const int bottom = grammar::SemanticInt32($6);
          const int top = grammar::SemanticInt32($8);
          if (top <= bottom) {
              // LogInvalidTerritoryHeight(
              //     pTempTerritory->
              //         sName.c_str())
              LogInvalidTerritoryHeight(pTempTerritory->sName.c_str(), m_lexer.yylineno, bottom, top);
          }
          pTempTerritory->m_Bottom = std::min(pTempTerritory->m_Bottom, bottom);
          pTempTerritory->m_Top = std::max(pTempTerritory->m_Top, top);
          $$ = static_cast<SpawnSemanticBits>(
              reinterpret_cast<std::uintptr_t>((new FVector{static_cast<double>(x), static_cast<double>(y), 0.0}))
          );
      }
    ;

npcmaker
    : NPCMAKER_BEGIN
      { pTempNpcMaker = new CNpcMaker(); }
      maker_territories banned_territories event_name inzone_classid initial_spawn spawn_time maximum_npc maker_domain_id
      flying_list npc_list NPCMAKER_END
      {
          if (IsSiegeRelatedSpawnTime(pTempNpcMaker->GetSpawnTimeType())) {
              m_pNpcMakerDB->AddSiegeRelatedCount();
          }
          m_pNpcMakerDB->AddNpcMaker(pTempNpcMaker);
          pTempNpcMaker = nullptr;
          m_CurrentNpcMaker.Clear();
      }
    ;

flying_list
    : flying_list flying
    | %empty
    ;

flying
    : FLYING '=' INTEGER
      {
          m_CurrentNpcMaker.m_bFlying = grammar::SemanticInt32($3) == 1;
          pTempNpcMaker->SetFlying(m_CurrentNpcMaker.m_bFlying);
      }
    ;

inzone_classid
    : INZONE_CLASSID '=' INTEGER
    | %empty
    ;

maker_territories
    : maker_territories ';' maker_territory
    | maker_territory
    ;

maker_territory
    : BRACKETED_NAME
      {
          const char16_t* const name = grammar::SemanticString($1);
          CTerritory* const territory = m_pNpcMakerDB->GetTerritory(std::u16string(name));
          if (territory) {
              pTempNpcMaker->GetTerritoryInfo()->AddTerritory(territory);
          } else {
              // LogUndefinedTerritory(name)
              LogUndefinedTerritory(name, m_lexer.yylineno);
          }
          m_TerritoryName = std::u16string(name);
      }
    ;

banned_territories
    : BANNED_TERRITORY '=' '{' banned_territory_list '}'
    | %empty
    ;

banned_territory_list
    : banned_territory_list ';' banned_territory
    | banned_territory
    ;

banned_territory
    : BRACKETED_NAME
      {
          const char16_t* const name = grammar::SemanticString($1);
          CTerritory* const territory = m_pNpcMakerDB->GetTerritory(std::u16string(name));
          if (territory) {
              pTempNpcMaker->GetTerritoryInfo()->AddBannedTerritory(territory);
          } else {
              // LogUndefinedTerritory(name)
              LogUndefinedTerritory(name, m_lexer.yylineno);
          }
      }
    ;

event_name
    : EVENT_NAME '=' BRACKETED_NAME
      { pTempNpcMaker->SetEventName(grammar::SemanticString($3)); }
    | %empty
    ;

initial_spawn
    : INITIAL_SPAWN '=' RANDOM3
      { pTempNpcMaker->SetInitialSpawn(INITIAL_SPAWN_RANDOM3); }
    | INITIAL_SPAWN '=' ALL
      { pTempNpcMaker->SetInitialSpawn(INITIAL_SPAWN_ALL); }
    | %empty
      { pTempNpcMaker->SetInitialSpawn(INITIAL_SPAWN_ALL); }
    ;

spawn_time
    : SPAWN_TIME '=' SIEGE_WARFARE_START '(' INTEGER ')'
      {
          pTempNpcMaker->SetSpawnTimeType(SPAWN_SIEGE_EVENT);
          pTempNpcMaker->SetSiegeType(NPC_DEFEND_SIEGE);
          pTempNpcMaker->SetCastleID(grammar::SemanticInt32($5));
      }
    | SPAWN_TIME '=' PC_SIEGE_WARFARE_START '(' INTEGER ')'
      {
          pTempNpcMaker->SetSpawnTimeType(SPAWN_SIEGE_EVENT);
          pTempNpcMaker->SetSiegeType(PC_DEFEND_SIEGE);
          pTempNpcMaker->SetCastleID(grammar::SemanticInt32($5));
      }
    | SPAWN_TIME '=' EXTENDED_SIEGE_WARFARE_START '(' INTEGER ')'
      {
          pTempNpcMaker->SetSpawnTimeType(SPAWN_SIEGE_EVENT);
          pTempNpcMaker->SetSiegeType(EXTENDED_DOMINION_SIEGE);
          pTempNpcMaker->SetCastleID(grammar::SemanticInt32($5));
      }
    | SPAWN_TIME '=' AGIT_ATTACK_WARFARE_START '(' INTEGER ')'
      {
          pTempNpcMaker->SetSpawnTimeType(SPAWN_AGIT_ATTACK_EVENT);
          pTempNpcMaker->SetAgitId(grammar::SemanticInt32($5));
      }
    | SPAWN_TIME '=' AGIT_DEFEND_WARFARE_START '(' INTEGER ')'
      {
          pTempNpcMaker->SetSpawnTimeType(SPAWN_AGIT_DEFEND_EVENT);
          pTempNpcMaker->SetAgitId(grammar::SemanticInt32($5));
      }
    | SPAWN_TIME '=' AGIT_BATTLE_ROYAL_START '(' INTEGER ';' TEAM_COLOR ';' INTEGER ')'
      {
          pTempNpcMaker->SetSpawnTimeType(

              SPAWN_AGIT_BATTLE_ROYAL_EVENT
          );
          pTempNpcMaker->SetAgitId(grammar::SemanticInt32($5));
          pTempNpcMaker->SetColor(grammar::SemanticInt32($7));
          pTempNpcMaker->SetNpcType(grammar::SemanticInt32($9));
      }
    | SPAWN_TIME '=' AGIT_FINAL_START '(' INTEGER ';' FINAL_SIDE ';' INTEGER ')'
      {
          pTempNpcMaker->SetSpawnTimeType(SPAWN_AGIT_FINAL_EVENT);
          pTempNpcMaker->SetAgitId(grammar::SemanticInt32($5));
          pTempNpcMaker->SetColor(grammar::SemanticInt32($7));
          pTempNpcMaker->SetNpcType(grammar::SemanticInt32($9));
      }
    | SPAWN_TIME '=' DOOR_OPEN '(' BRACKETED_NAME ')'
      {
          pTempNpcMaker->SetSpawnTimeType(SPAWN_DOOR_OPEN);
          pTempNpcMaker->DoorName(grammar::SemanticString($5));
      }
    | %empty
    ;

maximum_npc
    : MAXIMUM_NPC '=' INTEGER
      { pTempNpcMaker->SetMaximumNPC(grammar::SemanticInt32($3)); }
    | %empty
      { pTempNpcMaker->SetMaximumNPC(0); }
    ;

maker_domain_id
    : DOMAIN_ID '=' INTEGER
      { pTempNpcMaker->SetDomainID(grammar::SemanticInt32($3)); }
    | %empty
      {
          if (pTempNpcMaker->GetSpawnTimeType() == SPAWN_SIEGE_EVENT ||
              pTempNpcMaker->GetTerritoryInfo()->GetTerritorySize() < 1) {
              pTempNpcMaker->SetDomainID(0);
          } else {
              pTempNpcMaker->SetDomainID(FindMakerDomain(*m_pNpcMakerDB, *pTempNpcMaker->GetTerritoryInfo()));
          }
      }
    ;

npc_list
    : npc_list npc
    | npc
    ;

npc
    : NPC_BEGIN BRACKETED_NAME
      {
          m_dbname = kEmptyDatabaseName;
          m_dbsaving = 0;
          pTempNpcSpawnDefine = new CNpcSpawnDefine();
          pTempNpcSpawnDefine->SetName(const_cast<char16_t*>(grammar::SemanticString($2)));
      }
      npc_settings NPC_END
      {
          pTempNpcSpawnDefine->CheckAndModifyByBossRespawn();
          pTempNpcMaker->AddNpcSpawnDefine(pTempNpcSpawnDefine);
          pTempNpcSpawnDefine = nullptr;
      }
    ;

npc_settings
    : npc_settings npc_setting
    | npc_setting
    ;

npc_setting
    : POS '=' ANYWHERE
      {
          pTempNpcSpawnDefine->SetAnywhere(true);
          m_CurrentNpcMaker.m_bAnyWhere = true;
      }
    | POS '=' '{' spawn_positions '}'
      {
          pTempNpcSpawnDefine->SetAnywhere(false);
          m_CurrentNpcMaker.m_bAnyWhere = false;
      }
    | TOTAL '=' INTEGER
      { pTempNpcSpawnDefine->SetTotal(grammar::SemanticInt32($3)); }
    | RESPAWN '=' INTEGER TIME_UNIT
      {
          const int unit = grammar::SemanticInt32($4);
          if (unit >= 0 && unit <= 3) {
              pTempNpcSpawnDefine->SetRespawnTime(ScaleSeconds32(grammar::SemanticInt32($3), unit));
          }
      }
    | RESPAWN '=' NO
      { pTempNpcSpawnDefine->SetRespawnTime(-1); }
    | RESPAWN_RAND '=' INTEGER TIME_UNIT
      {
          const int unit = grammar::SemanticInt32($4);
          if (unit >= 0 && unit <= 3) {
              pTempNpcSpawnDefine->SetRespawnRandTime(ScaleSeconds32(grammar::SemanticInt32($3), unit));
          }
      }
    | AI '=' BRACKETED_NAME
      { pTempNpcSpawnDefine->SetAI(grammar::SemanticString($3)); }
    | WAYPOINTS '=' '{' waypoints '}'
      { pTempNpcSpawnDefine->SetWayPoints(m_WayPoints); }
    | WAYPOINTDELAYS '=' '{' waypoint_delays '}'
      { pTempNpcSpawnDefine->SetWayPointDelays(m_WayPointDelays); }
    | NICKNAME '=' BRACKETED_NAME
      { pTempNpcSpawnDefine->SetNickName(grammar::SemanticString($3)); }
    | PRIVATES '=' BRACKETED_NAME
      { pTempNpcSpawnDefine->SetPrivates(grammar::SemanticString($3)); }
    | AI_PARAMETERS '=' '{' ai_parameter_list '}'
      {
          pTempNpcSpawnDefine->SetAIParameters(m_AIParameters);
          m_AIParameters.clear();
      }
    | DBNAME '=' BRACKETED_NAME
      { m_dbname = const_cast<char16_t*>(grammar::SemanticString($3)); }
    | DBSAVING '=' '{' dbsaving_items '}'
      {
          if (m_dbname == kEmptyDatabaseName) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"dbsaving field without dbname!!!")
              // Log.Add(
              //     LOG_ERROR,
              //     "[NPC spawn] Line %d: dbsaving was specified without dbname. Continuing with the empty database
              //     name.", m_lexer.yylineno)
              L2_LOG_ERROR(
                  Log,
                  "[NPC spawn] Line %d: dbsaving was specified without dbname; continuing with the empty database name.",
                  m_lexer.yylineno
              );
          }
          pTempNpcSpawnDefine->SetDBSaving(m_dbname, m_dbsaving);
      }
    | ON_DELETE_CREATE '(' INTEGER TIME_UNIT ',' BRACKETED_NAME ')'
      {
          if (grammar::SemanticInt32($4) == 0) {
              stringlist.clear();
              stringlist.push_back(std::u16string(grammar::SemanticString($6)));
              pTempNpcSpawnDefine->SetOnDeleteCreate(grammar::SemanticInt32($3), stringlist);
          } else {
              // LogOnDeleteCreateTooBigWithName(
              //     grammar::SemanticInt32($3),
              //     grammar::SemanticString($6))
              LogOnDeleteCreateTooBigWithName(
                  grammar::SemanticInt32($3), grammar::SemanticString($6), m_lexer.yylineno
              );
          }
      }
    | ON_DELETE_CREATE '(' INTEGER TIME_UNIT ',' '{' created_npc_names '}' ')'
      {
          if (grammar::SemanticInt32($4) == 0) {
              pTempNpcSpawnDefine->SetOnDeleteCreate(grammar::SemanticInt32($3), stringlist);
          } else {
              // LogOnDeleteCreateTooBig(
              //     grammar::SemanticInt32($3))
              LogOnDeleteCreateTooBig(grammar::SemanticInt32($3), m_lexer.yylineno);
          }
      }
    | BOSS_RESPAWN_SET '=' YES
      { pTempNpcSpawnDefine->SetBossRespawn(true); }
    | IS_CHASE_PC '=' INTEGER
      { pTempNpcSpawnDefine->SetChasePC(grammar::SemanticInt32($3)); }
    ;

created_npc_names
    : created_npc_names ';' BRACKETED_NAME
      {
          stringlist.push_back(std::u16string(grammar::SemanticString($3)));
      }
    | BRACKETED_NAME
      {
          stringlist.clear();
          stringlist.push_back(std::u16string(grammar::SemanticString($1)));
      }
    ;

dbsaving_items
    : dbsaving_items ';' dbsaving_item
    | dbsaving_item
    ;

dbsaving_item
    : DEATH_TIME
      { m_dbsaving |= 1; }
    | PARAMETERS
      { m_dbsaving |= 2; }
    | POS
      { m_dbsaving |= 4; }
    ;

waypoints
    : waypoints ';' waypoint
      { m_WayPoints.push_back(m_Point); }
    | waypoint
      {
          m_WayPoints.clear();
          m_WayPoints.push_back(m_Point);
      }
    ;

waypoint
    : '(' INTEGER ';' INTEGER ';' INTEGER ')'
      {
          m_Point = FVector{
              static_cast<double>(grammar::SemanticInt32($2)), static_cast<double>(grammar::SemanticInt32($4)),
              static_cast<double>(grammar::SemanticInt32($6))
          };
      }
    ;

waypoint_delays
    : waypoint_delays ';' waypoint_delay
      { m_WayPointDelays.push_back(grammar::SemanticInt32($3)); }
    | waypoint_delay
      {
          m_WayPointDelays.clear();
          m_WayPointDelays.push_back(grammar::SemanticInt32($1));
      }
    ;

waypoint_delay
    : INTEGER TIME_UNIT
      {
          const int unit = grammar::SemanticInt32($2);
          if (unit >= 0 && unit <= 3) {
              $$ = (ScaleSeconds64Bits($1, unit));
          }
      }
    ;

ai_parameter_list
    : ai_parameter
    | ai_parameter_list ';' ai_parameter
    ;

ai_parameter
    : BRACKETED_NAME '=' BRACKETED_NAME
      {
          m_AIParameters[std::u16string(grammar::SemanticString($1))] = std::u16string(grammar::SemanticString($3));
      }
    | BRACKETED_NAME '=' INTEGER
      {
          m_AIParameters[std::u16string(grammar::SemanticString($1))] =
              std::u16string(Utf8ToUtf16(std::to_string(SemanticInt64($3))));
      }
    ;

spawn_positions
    : spawn_positions ';' spawn_position
      {
          pTempNpcSpawnDefine->AddSpawnPosition(
              reinterpret_cast<SpawnPos*>(static_cast<std::uintptr_t>($3))
          );
      }
    | spawn_position
      {
          pTempNpcSpawnDefine->AddSpawnPosition(
              reinterpret_cast<SpawnPos*>(static_cast<std::uintptr_t>($1))
          );
      }
    | INTEGER ';' INTEGER ';' INTEGER ';' INTEGER
      {
          pTempNpcSpawnDefine->AddSpawnPosition(new SpawnPos{
              grammar::SemanticInt32($1), grammar::SemanticInt32($3), grammar::SemanticInt32($5),
              grammar::SemanticInt32($7), 100
          });
      }
    ;

spawn_position
    : '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '%' '}'
      {
          $$ = static_cast<SpawnSemanticBits>(reinterpret_cast<std::uintptr_t>((new SpawnPos{
              grammar::SemanticInt32($2), grammar::SemanticInt32($4), grammar::SemanticInt32($6),
              grammar::SemanticInt32($8), grammar::SemanticInt32($10)
          })));
      }
    ;

npcmaker_ex
    : NPCMAKER_EX_BEGIN
      { pNpcMakerEx = new NpcMakerEx(); }
      maker_ex_territories banned_territories_ex maker_ex_name maker_ex_ai maker_ex_ai_parameters inzone_classid_ex
      maximum_npc_ex maker_ex_domain_id flying_ex_list npc_ex_list NPCMAKER_EX_END
      {
          m_pNpcMakerDB->AddNpcMakerEx(pNpcMakerEx);
          pNpcMakerEx = nullptr;
          m_CurrentNpcMaker.Clear();
      }
    ;

flying_ex_list
    : flying_ex_list flying_ex
    | %empty
    ;

flying_ex
    : FLYING '=' INTEGER
      {
          m_CurrentNpcMaker.m_bFlying = grammar::SemanticInt32($3) == 1;
          pNpcMakerEx->SetFlying(m_CurrentNpcMaker.m_bFlying);
      }
    ;

maker_ex_domain_id
    : DOMAIN_ID '=' INTEGER
      { pNpcMakerEx->SetDomainID(grammar::SemanticInt32($3)); }
    | %empty
      {
          if (pNpcMakerEx->GetTerritoryInfo()->GetTerritorySize() < 1) {
              pNpcMakerEx->SetDomainID(0);
          } else {
              pNpcMakerEx->SetDomainID(FindMakerDomain(*m_pNpcMakerDB, *pNpcMakerEx->GetTerritoryInfo()));
          }
      }
    ;

maker_ex_name
    : NAME '=' BRACKETED_NAME
      { pNpcMakerEx->SetName(grammar::SemanticString($3)); }
    ;

maker_ex_ai
    : AI '=' BRACKETED_NAME
      { pNpcMakerEx->SetAI(grammar::SemanticString($3)); }
    ;

maker_ex_ai_parameters
    : AI_PARAMETERS '=' '{' maker_ex_ai_parameter_list '}'
    | %empty
    ;

maker_ex_ai_parameter_list
    : maker_ex_ai_parameter
    | maker_ex_ai_parameter_list ';' maker_ex_ai_parameter
    ;

maker_ex_ai_parameter
    : BRACKETED_NAME '=' BRACKETED_NAME
      {
          pNpcMakerEx->SetParameter(grammar::SemanticString($1), grammar::SemanticString($3));
      }
    | BRACKETED_NAME '=' INTEGER
      {
          const std::u16string value = Utf8ToUtf16(std::to_string(SemanticInt64($3)));
          pNpcMakerEx->SetParameter(grammar::SemanticString($1), value.c_str());
      }
    ;

inzone_classid_ex
    : INZONE_CLASSID '=' INTEGER
    | %empty
    ;

maker_ex_territories
    : maker_ex_territories ';' maker_ex_territory
    | maker_ex_territory
    ;

maker_ex_territory
    : BRACKETED_NAME
      {
          const char16_t* const name = grammar::SemanticString($1);
          CTerritory* const territory = m_pNpcMakerDB->GetTerritory(std::u16string(name));
          if (territory) {
              pNpcMakerEx->AddTerritory(territory);
          } else {
              // LogUndefinedTerritory(name)
              LogUndefinedTerritory(name, m_lexer.yylineno);
          }
          m_TerritoryName = std::u16string(name);
      }
    ;

banned_territories_ex
    : BANNED_TERRITORY '=' '{' banned_territory_ex_list '}'
    | %empty
    ;

banned_territory_ex_list
    : banned_territory_ex_list ';' banned_territory_ex
    | banned_territory_ex
    ;

banned_territory_ex
    : BRACKETED_NAME
      {
          const char16_t* const name = grammar::SemanticString($1);
          CTerritory* const territory = m_pNpcMakerDB->GetTerritory(std::u16string(name));
          if (territory) {
              pNpcMakerEx->AddBannedTerritory(territory);
          } else {
              // LogUndefinedTerritory(name)
              LogUndefinedTerritory(name, m_lexer.yylineno);
          }
      }
    ;

maximum_npc_ex
    : MAXIMUM_NPC '=' INTEGER
      { pNpcMakerEx->SetMaximumNPC(grammar::SemanticInt32($3)); }
    | %empty
      { pNpcMakerEx->SetMaximumNPC(0); }
    ;

npc_ex_list
    : npc_ex_list npc_ex
    | npc_ex
    ;

npc_ex
    : NPC_EX_BEGIN BRACKETED_NAME
      {
          pNpcSpawnDefineEx = new NpcSpawnDefineEx(grammar::SemanticString($2));
      }
      npc_ex_settings NPC_EX_END
      {
          pNpcSpawnDefineEx->CheckAndModifyByBossRespawn();
          pNpcMakerEx->AddSpawnDefineEx(pNpcSpawnDefineEx);
          pNpcSpawnDefineEx = nullptr;
      }
    ;

npc_ex_settings
    : npc_ex_settings npc_ex_setting
    | npc_ex_setting
    ;

npc_ex_setting
    : POS '=' ANYWHERE
      {
          pNpcSpawnDefineEx->SetAnywhere(true);
          m_CurrentNpcMaker.m_bAnyWhere = true;
      }
    | POS '=' '{' spawn_ex_positions '}'
      {
          pNpcSpawnDefineEx->SetAnywhere(false);
          m_CurrentNpcMaker.m_bAnyWhere = false;
      }
    | TOTAL '=' INTEGER
      { pNpcSpawnDefineEx->SetTotal(grammar::SemanticInt32($3)); }
    | RESPAWN '=' INTEGER TIME_UNIT
      {
          const int unit = grammar::SemanticInt32($4);
          if (unit >= 0 && unit <= 3) {
              pNpcSpawnDefineEx->SetRespawnTime(ScaleSeconds32(grammar::SemanticInt32($3), unit));
          }
      }
    | RESPAWN '=' NO
      { pNpcSpawnDefineEx->SetRespawnTime(0); }
    | RESPAWN_RAND '=' INTEGER TIME_UNIT
      {
          const int unit = grammar::SemanticInt32($4);
          if (unit >= 0 && unit <= 3) {
              pNpcSpawnDefineEx->SetRespawnRandTime(ScaleSeconds32(grammar::SemanticInt32($3), unit));
          }
      }
    | AI '=' BRACKETED_NAME
      { pNpcSpawnDefineEx->SetAI(grammar::SemanticString($3)); }
    | WAYPOINTS '=' '{' named_waypoints_list '}'
    | NICKNAME '=' BRACKETED_NAME
      { pNpcSpawnDefineEx->SetNickName(grammar::SemanticString($3)); }
    | PRIVATES '=' BRACKETED_NAME
      { pNpcSpawnDefineEx->SetPrivates(grammar::SemanticString($3)); }
    | AI_PARAMETERS '=' '{' ai_parameter_list '}'
      {
          pNpcSpawnDefineEx->SetAIParameters(m_AIParameters);
          m_AIParameters.clear();
      }
    | DBNAME '=' BRACKETED_NAME
      { pNpcSpawnDefineEx->SetDBName(grammar::SemanticString($3)); }
    | BOSS_RESPAWN_SET '=' YES
      { pNpcSpawnDefineEx->SetBossRespawn(true); }
    | IS_CHASE_PC '=' INTEGER
      { pNpcSpawnDefineEx->SetChasePC(grammar::SemanticInt32($3)); }
    ;

named_waypoints_list
    : named_waypoints
    | named_waypoints_list ';' named_waypoints
    ;

named_waypoints
    : BRACKETED_NAME '=' '{' waypoints '}'
      {
          pNpcSpawnDefineEx->AddWayPoints(grammar::SemanticString($1), m_WayPoints);
      }
    ;

spawn_ex_positions
    : spawn_ex_positions ';' spawn_position
      {
          pNpcSpawnDefineEx->AddSpawnPosition(
              reinterpret_cast<SpawnPos*>(static_cast<std::uintptr_t>($3))
          );
      }
    | spawn_position
      {
          pNpcSpawnDefineEx->AddSpawnPosition(
              reinterpret_cast<SpawnPos*>(static_cast<std::uintptr_t>($1))
          );
      }
    | INTEGER ';' INTEGER ';' INTEGER ';' INTEGER
      {
          pNpcSpawnDefineEx->AddSpawnPosition(new SpawnPos{
              grammar::SemanticInt32($1), grammar::SemanticInt32($3), grammar::SemanticInt32($5),
              grammar::SemanticInt32($7), 100
          });
      }
    ;

%%
