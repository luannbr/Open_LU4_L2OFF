// npcpos.txt checker grammar (original Shared/parser_spawn.y). The productions
// match L2NPC/src/ParserSpawn.y.
// Bison generates ParserSpawn.cpp and ParserSpawnTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "Shared/src/ParserSpawn.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"

#include "L2Server/src/NPC.h"
#include "L2Server/src/Object.h"
#include "L2Server/src/ObjectDB.h"
#include "L2Server/src/TotalChecker.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include "ParserSpawnTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForNPCSpawn::yyparse
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
namespace {

using SpawnSemanticBits = grammar::SemanticBits;

char16_t kEmptyDatabaseName[] = u"";

bool EqualsAsciiCaseInsensitive(const char16_t* left, std::u16string_view right) {
    if (left == nullptr) {
        return false;
    }
    std::size_t index = 0;
    for (; left[index] != u'\0' && index < right.size(); ++index) {
        const auto Fold = [](char16_t value) {
            return value >= u'A' && value <= u'Z' ? static_cast<char16_t>(value + (u'a' - u'A')) : value;
        };
        if (Fold(left[index]) != Fold(right[index])) {
            return false;
        }
    }
    return left[index] == u'\0' && index == right.size();
}

std::int64_t SemanticInt64(SpawnSemanticBits bits) {
    if (bits <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return static_cast<std::int64_t>(bits);
    }
    return -1 - static_cast<std::int64_t>(~bits);
}

void LogUndefinedWord(const char16_t* name, int line) {
    const std::wstring hostName = Utf16ToWide(name);
    // Log.Add(
    //     LOG_ERROR,
    //     L"Undefined word[%s] in line [%d]",
    //     hostName.c_str(),
    //     line)
    // Log.Add(
    //     LOG_ERROR,
    //     L"[npc positions] Territory \"%ls\" has no precompiled definition near line %d. Reference not added.",
    //     hostName.c_str(), line)
    L2_LOG_ERROR(
        Log, u"[npc positions] Territory \"%ls\" has no precompiled definition near line %d; reference not added.",
        hostName.c_str(), line
    );
}

void LogInvalidAIParameter(int line, const char16_t* parameter, const AIClassInfo* info, const char16_t* maker) {
    const std::wstring hostParameter = Utf16ToWide(parameter);
    const std::wstring hostAI = Utf16ToWide(info != nullptr ? info->mName.c_str() : u"");
    const std::wstring hostMaker = Utf16ToWide(maker);
    // Log.Add(
    //     LOG_ERROR,
    //     L"line[%d] : Invalid parameter[%s] for ai[%s], maker[%s]",
    //     line,
    //     hostParameter.c_str(),
    //     hostAI.c_str(),
    //     hostMaker.c_str())
    // Log.Add(
    //     LOG_ERROR,
    //     L"[npc positions] AI \"%ls\" does not define parameter \"%ls\" for maker \"%ls\" near line %d.",
    //     hostAI.c_str(), hostParameter.c_str(), hostMaker.c_str(), line)
    L2_LOG_ERROR(
        Log, u"[npc positions] AI \"%ls\" does not define parameter \"%ls\" for maker \"%ls\" near line %d.", hostAI.c_str(),
        hostParameter.c_str(), hostMaker.c_str(), line
    );
}

} // namespace

void CParserForNPCSpawn::CNpcMakerInfoForChecker::Clear() { m_TerritoryInfo.Clear(); }

bool CParserForNPCSpawn::CNpcMakerInfoForChecker::Verify() { return m_TerritoryInfo.InitializeRandomPos(m_bFlying, 400); }

CParserForNPCSpawn::CParserForNPCSpawn() {
    // Current L2Server 0x00cbe894. Built-in grammar scratch members which
    // are not written by that constructor remain untouched.
    m_CurrentNpcMaker.Clear();
    m_CurrentNpcMaker.m_bAnyWhere = false;
    m_CurrentNpcMaker.m_bFlying = false;
    mCurrentClassInfo = nullptr;
    mCheckAI = false;
    m_bOnlyMakerEventList = false;
}

CParserForNPCSpawn::~CParserForNPCSpawn() = default;

int CParserForNPCSpawn::Create(char16_t* text, int codeUnitCount) {
    // Current L2Server 0x00cbdd60.
    return m_lexer.Create(text, codeUnitCount);
}

bool CParserForNPCSpawn::CheckAddTerritoryToTerritoryInfo(const char16_t* name) {
    if (g_PrecompiledHeaderDB.Search(name) == -1) {
        LogUndefinedWord(name, m_lexer.yylineno);
        return false;
    }

    const auto found = m_TerritoryDBForChecker.find(name);
    if (found != m_TerritoryDBForChecker.end()) {
        m_CurrentNpcMaker.m_TerritoryInfo.AddTerritory(found->second);
        return true;
    }

    const std::wstring hostName = Utf16ToWide(name);
    const std::wstring hostMaker = Utf16ToWide(mMakerName.c_str());
    // Log.Add(
    //     LOG_ERROR,
    //     L"Error : Territory Undefined! [%s] [%s] [%d]",
    //     hostName.c_str(),
    //     hostMaker.c_str(),
    //     m_lexer.yylineno)
    // Log.Add(
    //     LOG_ERROR,
    //     L"[npc positions] Territory \"%ls\" is not defined for maker \"%ls\" near line %d. Reference not added.",
    //     hostName.c_str(), hostMaker.c_str(), m_lexer.yylineno)
    L2_LOG_ERROR(
        Log, u"[npc positions] Territory \"%ls\" is not defined for maker \"%ls\" near line %d; reference not added.",
        hostName.c_str(), hostMaker.c_str(), m_lexer.yylineno
    );
    return false;
}

bool CParserForNPCSpawn::CheckAddBannedTerritoryToTerritoryInfo(const char16_t* name) {
    if (g_PrecompiledHeaderDB.Search(name) == -1) {
        LogUndefinedWord(name, m_lexer.yylineno);
        return false;
    }

    const auto found = m_TerritoryDBForChecker.find(name);
    if (found != m_TerritoryDBForChecker.end()) {
        m_CurrentNpcMaker.m_TerritoryInfo.AddBannedTerritory(found->second);
        return true;
    }

    const std::wstring hostName = Utf16ToWide(name);
    const std::wstring hostMaker = Utf16ToWide(mMakerName.c_str());
    // Log.Add(
    //     LOG_ERROR,
    //     L"Error2 : Territory Undefined! [%s][%s][%d]",
    //     hostName.c_str(),
    //     hostMaker.c_str(),
    //     m_lexer.yylineno)
    // Log.Add(
    //     LOG_ERROR,
    //     L"[npc positions] Banned territory \"%ls\" is not defined for maker \"%ls\" near line %d. Reference not added.",
    //     hostName.c_str(), hostMaker.c_str(), m_lexer.yylineno)
    L2_LOG_ERROR(
        Log, u"[npc positions] Banned territory \"%ls\" is not defined for maker \"%ls\" near line %d; reference not added.",
        hostName.c_str(), hostMaker.c_str(), m_lexer.yylineno
    );
    return false;
}

bool CParserForNPCSpawn::CheckSpawnPoint(const FVector* position) {
    if (mMakerName.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Error : maker name is empty. line : [%d]",
        //     m_lexer.yylineno)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[npc positions] Cannot check a spawn point near line %d: entry name is empty.",
        //     m_lexer.yylineno)
        L2_LOG_ERROR(
            Log, u"[npc positions] Cannot check a spawn point near line %d: entry name is empty.", m_lexer.yylineno
        );
        return false;
    }

    CObject* const object = g_ObjectDB.GetObjectW(g_ObjectDB.GetClassIdFromName(mMakerName.c_str(), false), false);
    if (object == nullptr || !object->IsNPC()) {
        const std::wstring hostMaker = Utf16ToWide(mMakerName.c_str());
        // Log.Add(
        //     LOG_ERROR,
        //     L"Error : maker name[%s] is not npc. line : [%d]",
        //     hostMaker.c_str(),
        //     m_lexer.yylineno)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[npc positions] Cannot check a spawn point for \"%ls\" near line %d: the name does not resolve to an NPC.",
        //     hostMaker.c_str(), m_lexer.yylineno)
        L2_LOG_ERROR(
            Log,
            u"[npc positions] Cannot check a spawn point for \"%ls\" near line %d: the name does not resolve to an NPC.",
            hostMaker.c_str(), m_lexer.yylineno
        );
        return false;
    }

    auto* const npc = static_cast<CNPC*>(object);
    if (npc->m_data != nullptr && npc->m_data->IsPrivateRespawnNoLog()) {
        return true;
    }

    const int x = runtime::Win64CvttDoubleToInt32(position->x);
    const int y = runtime::Win64CvttDoubleToInt32(position->y);
    const int z = runtime::Win64CvttDoubleToInt32(position->z);
    const std::wstring hostMaker = Utf16ToWide(mMakerName.c_str());
    if (m_CurrentNpcMaker.m_TerritoryInfo.IsInBannedTerritory(*position, true)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Error : pos[%d,%d,%d] is in BanTerritory. [%s] [%d]",
        //     x,
        //     y,
        //     z,
        //     hostMaker.c_str(),
        //     m_lexer.yylineno)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[npc positions] Spawn point (%d, %d, %d) for \"%ls\" is inside a banned territory near line %d.",
        //     x, y, z, hostMaker.c_str(), m_lexer.yylineno)
        L2_LOG_ERROR(
            Log, u"[npc positions] Spawn point (%d, %d, %d) for \"%ls\" is inside a banned territory near line %d.", x, y, z,
            hostMaker.c_str(), m_lexer.yylineno
        );
        return false;
    }
    if (m_CurrentNpcMaker.m_TerritoryInfo.IsInTerritory(*position, true)) {
        return true;
    }

    const std::wstring hostTerritory = Utf16ToWide(m_TerritoryName.c_str());
    // Log.Add(
    //     LOG_ERROR,
    //     L"Error : pos[%d,%d,%d] is not in Territory. [%s] [%s] [%d]",
    //     x,
    //     y,
    //     z,
    //     hostTerritory.c_str(),
    //     hostMaker.c_str(),
    //     m_lexer.yylineno)
    // Log.Add(
    //     LOG_ERROR,
    //     L"[npc positions] Spawn point (%d, %d, %d) for \"%ls\" is outside its allowed territories near line %d (last
    //     parsed territory \"%ls\").", x, y, z, hostMaker.c_str(), m_lexer.yylineno, hostTerritory.c_str())
    L2_LOG_ERROR(
        Log,
        u"[npc positions] Spawn point (%d, %d, %d) for \"%ls\" is outside its allowed territories near line %d (last parsed "
        u"territory \"%ls\").",
        x, y, z, hostMaker.c_str(), m_lexer.yylineno, hostTerritory.c_str()
    );
    return false;
}
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
          pTempTerritory->SetName(const_cast<char16_t*>(grammar::SemanticString($2)));
      }
      DOMAIN_ID '=' INTEGER '{' point_list '}' DOMAIN_END
    ;

territory
    : TERRITORY_BEGIN BRACKETED_NAME
      {
          const char16_t* const name = grammar::SemanticString($2);
          pTempTerritory = new CTerritory();
          pTempTerritory->SetName(const_cast<char16_t*>(name));
          g_PrecompiledHeaderDB.Set(name, 1);
      }
      '{' point_list '}' TERRITORY_END
      {
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
          const int x = runtime::Int32FromBits($2);
          const int y = runtime::Int32FromBits($4);
          const int bottom = runtime::Int32FromBits($6);
          const int top = runtime::Int32FromBits($8);
          if (top <= bottom) {
              const std::wstring hostName = Utf16ToWide(pTempTerritory->sName.c_str());
              // Log.Add(
              //     LOG_ERROR,
              //     L"territory [%s] z1 < z2",
              //     hostName.c_str())
              // Log.Add(
              //     LOG_ERROR,
              //     L"[npc positions] Invalid height range in territory \"%ls\" near line %d: bottom %d, top %d. Top must
              //     be greater than bottom. Parsing continues.", hostName.c_str(), m_lexer.yylineno, bottom, top)
              L2_LOG_ERROR(
                  Log,
                  u"[npc positions] Invalid height range in territory \"%ls\" near line %d: bottom %d, top %d; top must "
                  u"be greater than bottom. Parsing continues.",
                  hostName.c_str(), m_lexer.yylineno, bottom, top
              );
          }
          pTempTerritory->m_Bottom = std::min(pTempTerritory->m_Bottom, bottom);
          pTempTerritory->m_Top = std::max(pTempTerritory->m_Top, top);
          $$ = static_cast<SpawnSemanticBits>(reinterpret_cast<std::uintptr_t>((new FVector(x, y, 0))));
      }
    ;

npcmaker
    : NPCMAKER_BEGIN { } maker_territories banned_territories event_name inzone_classid initial_spawn spawn_time maximum_npc
      maker_domain_id flying_list npc_list NPCMAKER_END
      {
          if (m_bSpawnTime && !m_bMaxNpc) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[%d] spawn_time defined without maximum_npc",
              //     m_lexer.yylineno)
              // Log.Add(
              //     LOG_ERROR,
              //     L"[npc positions] spawn_time is set without maximum_npc near line %d.",
              //     m_lexer.yylineno)
              L2_LOG_ERROR(Log, u"[npc positions] spawn_time is set without maximum_npc near line %d.", m_lexer.yylineno);
          }
          m_CurrentNpcMaker.Clear();
          m_CurrentNpcMaker.m_bAnyWhere = false;
          m_CurrentNpcMaker.m_bFlying = false;
      }
    ;

flying_list
    : flying_list flying
    | %empty
    ;

flying
    : FLYING '=' INTEGER
      { m_CurrentNpcMaker.m_bFlying = runtime::Int32FromBits($3) == 1; }
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
          if (g_PrecompiledHeaderDB.Search(name) == -1) {
              // Noise: The following CheckAdd call reports this same missing
              // precompiled definition. Preserve the original lookup/branch.
              // LogUndefinedWord(name, m_lexer.yylineno)
          }
          CheckAddTerritoryToTerritoryInfo(name);
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
          if (g_PrecompiledHeaderDB.Search(name) == -1) {
              // Noise: The following CheckAdd call reports this same missing
              // precompiled definition. Preserve the original lookup/branch.
              // LogUndefinedWord(name, m_lexer.yylineno)
          }
          CheckAddBannedTerritoryToTerritoryInfo(name);
      }
    ;

event_name
    : EVENT_NAME '=' BRACKETED_NAME
    | %empty
    ;

initial_spawn
    : INITIAL_SPAWN '=' RANDOM3
    | INITIAL_SPAWN '=' ALL
    | %empty
    ;

spawn_time
    : SPAWN_TIME '=' SIEGE_WARFARE_START '(' INTEGER ')'
      { m_bSpawnTime = false; }
    | SPAWN_TIME '=' PC_SIEGE_WARFARE_START '(' INTEGER ')'
      { m_bSpawnTime = false; }
    | SPAWN_TIME '=' EXTENDED_SIEGE_WARFARE_START '(' INTEGER ')'
      { m_bSpawnTime = false; }
    | SPAWN_TIME '=' AGIT_ATTACK_WARFARE_START '(' INTEGER ')'
      { m_bSpawnTime = false; }
    | SPAWN_TIME '=' AGIT_DEFEND_WARFARE_START '(' INTEGER ')'
      { m_bSpawnTime = false; }
    | SPAWN_TIME '=' AGIT_BATTLE_ROYAL_START '(' INTEGER ';' TEAM_COLOR ';' INTEGER ')'
      { m_bSpawnTime = false; }
    | SPAWN_TIME '=' AGIT_FINAL_START '(' INTEGER ';' FINAL_SIDE ';' INTEGER ')'
      { m_bSpawnTime = false; }
    | SPAWN_TIME '=' DOOR_OPEN '(' BRACKETED_NAME ')'
      { m_bSpawnTime = false; }
    | %empty
      { m_bSpawnTime = false; }
    ;

maximum_npc
    : MAXIMUM_NPC '=' INTEGER
      { m_bMaxNpc = true; }
    | %empty
      { m_bMaxNpc = false; }
    ;

maker_domain_id
    : DOMAIN_ID '=' INTEGER
    | %empty
    ;

npc_list
    : npc_list npc
    | npc
    ;

npc
    : NPC_BEGIN BRACKETED_NAME
      {
          const int action = 41;
          if (action == 0x29) {
              m_dbname = kEmptyDatabaseName;
              m_dbsaving = 0;
          }
          const char16_t* const name = grammar::SemanticString($2);
          const std::size_t length = std::char_traits<char16_t>::length(name);
          if (length > 24) {
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Too long name[%s], length[%d] in line[%d]",
              //     hostName.c_str(),
              //     static_cast<int>(length),
              //     m_lexer.yylineno)
              // Log.Add(
              //     LOG_ERROR,
              //     L"[npc positions] NPC name \"%ls\" has %llu UTF-16 code units near line %d (limit 24). Parsing
              //     continues.", hostName.c_str(), static_cast<unsigned long long>(length), m_lexer.yylineno)
              L2_LOG_ERROR(
                  Log,
                  u"[npc positions] NPC name \"%ls\" has %llu UTF-16 code units near line %d (limit 24); parsing "
                  u"continues.",
                  hostName.c_str(), static_cast<unsigned long long>(length), m_lexer.yylineno
              );
          }
          if (g_PrecompiledHeaderDB.Search(name) == -1) {
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined npc[%s] in line [%d]",
              //     hostName.c_str(),
              //     m_lexer.yylineno)
              // Log.Add(
              //     LOG_ERROR,
              //     L"[npc positions] NPC \"%ls\" is missing from the precompiled headers near line %d. Parsing
              //     continues.", hostName.c_str(), m_lexer.yylineno)
              L2_LOG_ERROR(
                  Log,
                  u"[npc positions] NPC \"%ls\" is missing from the precompiled headers near line %d; parsing continues.",
                  hostName.c_str(), m_lexer.yylineno
              );
          }
          mMakerName = std::u16string(name);
      }
      npc_settings NPC_END
      { m_CurrentNpcMaker.Verify(); }
    ;

npc_settings
    : npc_settings npc_setting
    | npc_setting
    ;

npc_setting
    : POS '=' ANYWHERE
      { m_CurrentNpcMaker.m_bAnyWhere = true; }
    | POS '=' '{' spawn_positions '}'
      { m_CurrentNpcMaker.m_bAnyWhere = false; }
    | TOTAL '=' INTEGER
    | RESPAWN '=' INTEGER TIME_UNIT
    | RESPAWN '=' NO
    | RESPAWN_RAND '=' INTEGER TIME_UNIT
    | AI '=' BRACKETED_NAME
    | WAYPOINTS '=' '{' waypoints '}'
    | WAYPOINTDELAYS '=' '{' waypoint_delays '}'
    | NICKNAME '=' BRACKETED_NAME
    | PRIVATES '=' BRACKETED_NAME
    | AI_PARAMETERS '=' '{' ai_parameter_list '}'
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
              //     L"[npc positions] dbsaving is set without dbname near line %d.",
              //     m_lexer.yylineno)
              L2_LOG_ERROR(Log, u"[npc positions] dbsaving is set without dbname near line %d.", m_lexer.yylineno);
          }
      }
    | ON_DELETE_CREATE '(' INTEGER TIME_UNIT ',' BRACKETED_NAME ')'
    | ON_DELETE_CREATE '(' INTEGER TIME_UNIT ',' '{' created_npc_names '}' ')'
    | BOSS_RESPAWN_SET '=' YES
    | IS_CHASE_PC '=' INTEGER
    ;

created_npc_names
    : created_npc_names ';' BRACKETED_NAME
    | BRACKETED_NAME
    ;

dbsaving_items
    : dbsaving_items ';' dbsaving_item
    | dbsaving_item
    ;

dbsaving_item
    : DEATH_TIME
    | PARAMETERS
    | POS
    ;

waypoints
    : waypoints ';' waypoint
    | waypoint
    ;

waypoint
    : '(' INTEGER ';' INTEGER ';' INTEGER ')'
    ;

waypoint_delays
    : waypoint_delays ';' waypoint_delay
    | waypoint_delay
    ;

waypoint_delay
    : INTEGER TIME_UNIT
      {
          const std::uint64_t value = $1;
          switch (runtime::Int32FromBits($2)) {
              case 0:
                  $$ = (value);
                  break;
              case 1:
                  $$ = (value * UINT64_C(60));
                  break;
              case 2:
                  $$ = (value * UINT64_C(3600));
                  break;
              case 3:
                  $$ = (value * UINT64_C(86400));
                  break;
              default:
                  break;
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
          pSpawnPos = reinterpret_cast<FVector*>(static_cast<std::uintptr_t>($3));
      }
    | spawn_position
      {
          pSpawnPos = reinterpret_cast<FVector*>(static_cast<std::uintptr_t>($1));
      }
    | INTEGER ';' INTEGER ';' INTEGER ';' INTEGER
      {
          const FVector position(
              runtime::Int32FromBits($1), runtime::Int32FromBits($3), runtime::Int32FromBits($5)
          );
          CheckSpawnPoint(&position);
      }
    ;

spawn_position
    : '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '%' '}'
      {
          $$ = static_cast<SpawnSemanticBits>(reinterpret_cast<std::uintptr_t>(
              (new FVector(runtime::Int32FromBits($2), runtime::Int32FromBits($4), runtime::Int32FromBits($6)))
          ));
      }
    ;

npcmaker_ex
    : NPCMAKER_EX_BEGIN { } maker_ex_territories banned_territories_ex maker_ex_name maker_ex_ai maker_ex_ai_parameters
      inzone_classid_ex maximum_npc_ex maker_ex_domain_id flying_ex_list npc_ex_list NPCMAKER_EX_END
      {
          m_CurrentNpcMaker.Clear();
          m_CurrentNpcMaker.m_bAnyWhere = false;
          m_CurrentNpcMaker.m_bFlying = false;
      }
    ;

flying_ex_list
    : flying_ex_list flying_ex
    | %empty
    ;

flying_ex
    : FLYING '=' INTEGER
      { m_CurrentNpcMaker.m_bFlying = runtime::Int32FromBits($3) == 1; }
    ;

maker_ex_domain_id
    : DOMAIN_ID '=' INTEGER
    | %empty
    ;

maker_ex_name
    : NAME '=' BRACKETED_NAME
      {
          const char16_t* const name = grammar::SemanticString($3);
          const std::u16string key(name);
          if (mMakerNameSet.find(key) != mMakerNameSet.end()) {
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Duplicated maker name[%s]. line[%d]",
              //     hostName.c_str(),
              //     m_lexer.yylineno)
              // Log.Add(
              //     LOG_ERROR,
              //     L"[npc positions] Duplicate maker name \"%ls\" near line %d. Retaining the previous maker name.",
              //     hostName.c_str(), m_lexer.yylineno)
              L2_LOG_ERROR(
                  Log, u"[npc positions] Duplicate maker name \"%ls\" near line %d; retaining the previous maker name.",
                  hostName.c_str(), m_lexer.yylineno
              );
              break;
          }
          mMakerName = key;
          mMakerNameSet.insert(key);
      }
    ;

maker_ex_ai
    : AI '=' BRACKETED_NAME
      {
          if (!mCheckAI) {
              break;
          }
          const char16_t* const name = grammar::SemanticString($3);
          mCurrentClassInfo = AIClassInfoTable::inst()->GetClassInfoW(name);
          if (mCurrentClassInfo == nullptr) {
              const std::wstring hostName = Utf16ToWide(name);
              const std::wstring hostMaker = Utf16ToWide(mMakerName.c_str());
              // Log.Add(
              //     LOG_ERROR,
              //     L"line[%d] : Cannot find ai[%s] for maker[%s]",
              //     m_lexer.yylineno,
              //     hostName.c_str(),
              //     hostMaker.c_str())
              // Log.Add(
              //     LOG_ERROR,
              //     L"[npc positions] AI \"%ls\" for maker \"%ls\" was not found near line %d. AI parameter checks for
              //     this entry will be skipped.", hostName.c_str(), hostMaker.c_str(), m_lexer.yylineno)
              L2_LOG_ERROR(
                  Log,
                  u"[npc positions] AI \"%ls\" for maker \"%ls\" was not found near line %d; AI parameter checks for this "
                  u"entry will be skipped.",
                  hostName.c_str(), hostMaker.c_str(), m_lexer.yylineno
              );
          }
      }
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
          const char16_t* const parameter = grammar::SemanticString($1);
          if (mCurrentClassInfo != nullptr && !mCurrentClassInfo->HaveParameter(parameter)) {
              LogInvalidAIParameter(m_lexer.yylineno, parameter, mCurrentClassInfo, mMakerName.c_str());
          }
          if (!m_bOnlyMakerEventList || !EqualsAsciiCaseInsensitive(parameter, u"EventName")) {
              break;
          }
          const char16_t* const eventName = grammar::SemanticString($3);
          const std::u16string key(eventName);
          if (eventNameSet.find(key) != eventNameSet.end()) {
              break;
          }
          eventNameSet.insert(key);
          CNpcPosChecker::Inst()->Uniputs(eventName);
          CNpcPosChecker::Inst()->Uniputs(u"\r\n");
      }
    | BRACKETED_NAME '=' INTEGER
      {
          const char16_t* const parameter = grammar::SemanticString($1);
          if (mCurrentClassInfo != nullptr && !mCurrentClassInfo->HaveParameter(parameter)) {
              LogInvalidAIParameter(m_lexer.yylineno, parameter, mCurrentClassInfo, mMakerName.c_str());
          }
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
          CheckAddTerritoryToTerritoryInfo(name);
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
          if (g_PrecompiledHeaderDB.Search(name) == -1) {
              // Noise: The following CheckAdd call reports this same missing
              // precompiled definition. Preserve the original lookup/branch.
              // LogUndefinedWord(name, m_lexer.yylineno)
          }
          CheckAddBannedTerritoryToTerritoryInfo(name);
      }
    ;

maximum_npc_ex
    : MAXIMUM_NPC '=' INTEGER
      { m_bMaxNpc = true; }
    | %empty
      { m_bMaxNpc = false; }
    ;

npc_ex_list
    : npc_ex_list npc_ex
    | npc_ex
    ;

npc_ex
    : NPC_EX_BEGIN BRACKETED_NAME
      {
          const int action = 102;
          if (action == 0x29) {
              m_dbname = kEmptyDatabaseName;
              m_dbsaving = 0;
          }
          const char16_t* const name = grammar::SemanticString($2);
          const std::size_t length = std::char_traits<char16_t>::length(name);
          if (length > 24) {
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Too long name[%s], length[%d] in line[%d]",
              //     hostName.c_str(),
              //     static_cast<int>(length),
              //     m_lexer.yylineno)
              // Log.Add(
              //     LOG_ERROR,
              //     L"[npc positions] NPC name \"%ls\" has %llu UTF-16 code units near line %d (limit 24). Parsing
              //     continues.", hostName.c_str(), static_cast<unsigned long long>(length), m_lexer.yylineno)
              L2_LOG_ERROR(
                  Log,
                  u"[npc positions] NPC name \"%ls\" has %llu UTF-16 code units near line %d (limit 24); parsing "
                  u"continues.",
                  hostName.c_str(), static_cast<unsigned long long>(length), m_lexer.yylineno
              );
          }
          if (g_PrecompiledHeaderDB.Search(name) == -1) {
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined npc[%s] in line [%d]",
              //     hostName.c_str(),
              //     m_lexer.yylineno)
              // Log.Add(
              //     LOG_ERROR,
              //     L"[npc positions] NPC \"%ls\" is missing from the precompiled headers near line %d. Parsing
              //     continues.", hostName.c_str(), m_lexer.yylineno)
              L2_LOG_ERROR(
                  Log,
                  u"[npc positions] NPC \"%ls\" is missing from the precompiled headers near line %d; parsing continues.",
                  hostName.c_str(), m_lexer.yylineno
              );
          }
          mMakerName = std::u16string(name);
      }
      npc_ex_settings NPC_EX_END
      { m_CurrentNpcMaker.Verify(); }
    ;

npc_ex_settings
    : npc_ex_settings npc_ex_setting
    | npc_ex_setting
    ;

npc_ex_setting
    : POS '=' ANYWHERE
      { m_CurrentNpcMaker.m_bAnyWhere = true; }
    | POS '=' '{' spawn_ex_positions '}'
      { m_CurrentNpcMaker.m_bAnyWhere = false; }
    | TOTAL '=' INTEGER
    | RESPAWN '=' INTEGER TIME_UNIT
    | RESPAWN '=' NO
    | RESPAWN_RAND '=' INTEGER TIME_UNIT
    | AI '=' BRACKETED_NAME
    | WAYPOINTS '=' '{' named_waypoints_list '}'
    | NICKNAME '=' BRACKETED_NAME
    | PRIVATES '=' BRACKETED_NAME
    | AI_PARAMETERS '=' '{' ai_parameter_list '}'
    | DBNAME '=' BRACKETED_NAME
    | BOSS_RESPAWN_SET '=' YES
    | IS_CHASE_PC '=' INTEGER
    ;

named_waypoints_list
    : named_waypoints
    | named_waypoints_list ';' named_waypoints
    ;

named_waypoints
    : BRACKETED_NAME '=' '{' waypoints '}'
    ;

spawn_ex_positions
    : spawn_ex_positions ';' spawn_position
      {
          CheckSpawnPoint(reinterpret_cast<const FVector*>(static_cast<std::uintptr_t>($3)));
      }
    | spawn_position
      {
          CheckSpawnPoint(reinterpret_cast<const FVector*>(static_cast<std::uintptr_t>($1)));
      }
    | INTEGER ';' INTEGER ';' INTEGER ';' INTEGER
      {
          const FVector position(
              runtime::Int32FromBits($1), runtime::Int32FromBits($3), runtime::Int32FromBits($5)
          );
          CheckSpawnPoint(&position);
      }
    ;

%%
