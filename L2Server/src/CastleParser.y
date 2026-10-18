// CParserForResidence grammar (original L2Server/castle_parser.y).
// Bison generates CastleParser.cpp and CastleParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/CastleParser.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/Agit.h"
#include "L2Server/src/AuctionAgit.h"
#include "L2Server/src/BaseCampAgit.h"
#include "L2Server/src/CFortressFlagpole.h"
#include "L2Server/src/Castle.h"
#include "L2Server/src/CompeteAgit.h"
#include "L2Server/src/ControlTower.h"
#include "L2Server/src/Dominion.h"
#include "L2Server/src/Door.h"
#include "L2Server/src/Fortress.h"
#include "L2Server/src/HolyThing.h"
#include "L2Server/src/MiniGameAgit.h"
#include "L2Server/src/NPC.h"
#include "L2Server/src/NpcBattleAgit.h"
#include "L2Server/src/ObjectDB.h"
#include "L2Server/src/OwnThing.h"
#include "L2Server/src/Portable/ProcessRuntime.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Territory.h"
#include "Shared/src/Utils.h"
#include "L2Server/src/SiegeAgit.h"
#include "L2Server/src/SignBoard.h"
#include "L2Server/src/Skill.h"
#include "L2Server/src/TeamBattleAgit.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <list>
#include <string>
#include <string_view>

#include "CastleParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForResidence::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"CastleParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

char16_t* SemanticWord(const grammar::SemanticBits value) {
    return grammar::SemanticString(value);
}

const wchar_t* StableHostWide(const char16_t* value) {
    // The current Win64 parser stores pointers into its UTF-16 input image.
    // wchar_t is four bytes on the portable host, so keep an equivalent
    // process-lifetime copy for the only parser record that publishes the
    // pointer instead of consuming it synchronously.
    static std::list<std::wstring> values;
    values.push_back(target_utf16::ToHostCodeUnits(value));
    return values.back().c_str();
}

std::int32_t MakeTime32(const int year, const int month, const int day, const int hour, const int minute) {
    std::tm localTime{};
    localTime.tm_year = year - 1900;
    localTime.tm_mon = month - 1;
    localTime.tm_mday = day;
    localTime.tm_hour = hour;
    localTime.tm_min = minute;
    return runtime::crt::Mktime32(&localTime);
}

void CopyUtf16(char16_t* const destination, const std::size_t capacity, const char16_t* const source) {
    if (capacity == 0) {
        return;
    }
    if (source == nullptr) {
        destination[0] = u'\0';
        errno = EINVAL;
        runtime::process::HandleInvalidParameter(L"wcscpy_s");
        return;
    }
    std::size_t length = 0;
    while (length < capacity && source[length] != u'\0') {
        ++length;
    }
    if (length >= capacity) {
        destination[0] = u'\0';
        errno = ERANGE;
        runtime::process::HandleInvalidParameter(L"wcscpy_s");
        return;
    }
    std::copy_n(source, length + 1, destination);
}

} // namespace

CParserForResidence::CParserForResidence() :
    m_pResidenceData(nullptr),
    m_pResidence(nullptr),
    m_pLstTerritory(nullptr),
    m_pTerritory(nullptr),
    m_pVector(nullptr),
    m_pAngle(nullptr),
    m_pOwningNPCData(nullptr),
    m_pAdditionalNPCData(nullptr),
    m_bVectorVector(false),
    m_bPairVector(false),
    m_pVectorVector(nullptr),
    m_pStringVector(nullptr),
    m_TempStringVector(),
    m_pIntVector(nullptr),
    m_pPairVector(nullptr),
    m_TempString(),
    m_TempVector(),
    m_TempAngle(),
    m_tempStringVector(),
    m_tempIntVector(),
    m_tempTexRate(0),
    m_tempSysGetRate(0),
    m_tempTributeRate(0),
    m_tempBuyRate(0),
    m_tempBuySysGetRate(0),
    m_tempBuyTributeRate(0),
    m_tempCastleType(CastleType::CASTLE_NOTHING),
    m_lexer() {
}

CParserForResidence::~CParserForResidence() = default;

int CParserForResidence::Create(char16_t* const fileBuffer, const int fileSize) {
    // Current L2Server 0x00c73e58.
    return m_lexer.Create(fileBuffer, fileSize);
}

void CParserForResidence::yysyntaxerror() {
    // Current L2Server 0x00c73ec8.
    char16_t message[60];
    // std::swprintf(
    //     message,
    //     std::size(message),
    //     L"CastleDataChecker[%d] : syntax error")
    FormatUtf16To(
        message, std::size(message), u"CastleDataChecker[%lld] : syntax error", static_cast<long long>(m_lexer.line) + 1
    );
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

%token EQUAL
%token CASTLE_SIEGE_MUSIC_BEGIN
%token CASTLE_SIEGE_MUSIC_END
%token FORTRESS_SIEGE_MUSIC_BEGIN
%token FORTRESS_SIEGE_MUSIC_END
%token CASTLE_BEGIN
%token AGIT_BEGIN
%token FORTRESS_BEGIN
%token DOMINION_BEGIN
%token RESIDENCE_TERRITORY
%token BATTLEFIELD_TERRITORY
%token HEADQUARTER_TERRITORY
%token OWNER_RESTART_TERRITORY
%token DOMAIN_TERRITORY
%token MANOR_TERRITORY
%token OWNER_RESTART_POINT_LIST
%token CHAO_RESTART_POINT_LIST
%token MERCENARY_POINT_LIST
%token OTHER_RESTART_VILLAGE_LIST
%token BANISH_POINT_LIST
%token OWNING_NPC_LIST
%token BOSS_NPC_LIST
%token NEXT_SIEGE
%token SIEGE_TERM
%token RESIDENCE_STATE
%token RESIDENCE_GATE_LIST
%token CONTROL_TOWER_LIST
%token BREAKABLE_WALL_LIST
%token HOLYTHING
%token OWNTHING
%token RESIDENCE_GUARD_MAPPING
%token OBSERVER_IMPORT
%token BASIC_RATE
%token AGIT_AUCTION
%token NEXT_AUCTION
%token AGIT_AUCTION_MIN
%token AGIT_DEPOSIT
%token AGIT_LEASE
%token AGIT_SIZE
%token AGIT_PARENT_CASTLEID
%token BATTLE_ROYAL_FLAG_POINT_LIST
%token FINAL_FLAG_POINT_LIST
%token FINAL_OPEN_DOOR_LIST
%token DOOR_LIST
%token CASTLE_END
%token AGIT_END
%token FORTRESS_END
%token DOMINION_END
%token CASTLE_COMMON_BEGIN
%token CASTLE_COMMON_END
%token BASE_SIEGE_TIME
%token RESERVABLE_SIEGE_TIME
%token MAX_CONCURRENT_SIEGE
%token SAT_SUN
%token SIEGE_GROUP
%token CHALLENGER_RESTART_POINT_LIST
%token AGIT_PREPARATORY_TERM
%token AGIT_PROGRESS_TERM
%token AGIT_CLOSE_TERM
%token AGIT_ENTRANCE_POINT
%token AGIT_DELETE_ITEM_LIST
%token RESIDENCE_SKILL_LIST
%token DOMINION_MAIN_CASTLE
%token PICKUP_OWNTHING
%token HIDE_NAME_ITEM
%token SYS_CASTLE_BEGIN
%token SYS_CASTLE_END
%token GOOD_CASTLE_BEGIN
%token GOOD_CASTLE_END
%token BAD_CASTLE_BEGIN
%token BAD_CASTLE_END
%token TAX_RATE
%token TAX_SYSGET_RATE
%token TRIBUTE_RATE
%token BUY_TAX_RATE
%token BUY_TAX_SYSGET_RATE
%token BUY_TRIBUTE_RATE
%token EDITOR_POS
%token EDITOR_ID
%token AGIT_COMPETE_DAY
%token AGIT_COMPETE_HOUR
%token AGIT_COMPETE_MIN
%token AGIT_COMPETE_PROGRESS
%token FORTRESS_SCALE
%token FORTRESS_RELATED_CASTLE
%token FORTRESS_FLAGPOLE
%token FORTRESS_FLAG
%token RESIDENCE_GRADE
%token RESIDENCE_DOMAINS
%token DECO_NPC_SLOTS
%token NAME
%token IDENTIFIER
%token INTEGER
%token FLOAT_NUMBER
%token INITIAL_NORMAL
%token TELEPORTER_GROUP
%token NOTMOVE_MOVE
%token SSQ_VALUE
// The original grammar has 325 shift/reduce and 1356 reduce/reduce conflicts. Bison
// resolves them as the original tables did. A shift wins over a reduce,
// and the earlier rule wins over a later rule.
%start n1

%%

n1
    : castle_common_list
    ;

castle_common_list
    : castle_common_list castle_common
    | %empty
    ;

castle_common
    : CASTLE_COMMON_BEGIN { } base_siege_time_def_list { } CASTLE_COMMON_END
    | CASTLE_BEGIN NAME INTEGER SIEGE_GROUP
      {
          m_pResidenceData = g_ResidenceDB.InitializeResidenceData(grammar::SemanticInt32($3));
          m_pResidence = new CCastle();
          m_pResidence->SetData(m_pResidenceData);
          m_pResidence->SetName(SemanticWord($2));
          m_pResidence->SetResidenceID(grammar::SemanticInt32($3));
          m_pLstTerritory = nullptr;
      }
      residence_territory_def_list CASTLE_END
      { g_ResidenceDB.AddResidence(SemanticWord($2), m_pResidence); }
    | DOMINION_BEGIN NAME INTEGER SIEGE_GROUP
      {
          m_pResidenceData = g_ResidenceDB.InitializeResidenceData(grammar::SemanticInt32($3));
          auto* const dominion = new CDominion();
          m_pResidence = dominion;
          m_pResidence->SetData(m_pResidenceData);
          m_pResidence->SetName(SemanticWord($2));
          m_pResidence->SetResidenceID(grammar::SemanticInt32($3));
          g_ResidenceDB.AddResidence(SemanticWord($2), m_pResidence);
          CDominionManager::Inst()->Add(dominion);
      }
      residence_territory_def_list DOMINION_END
      { g_ResidenceDB.AddResidence(SemanticWord($2), m_pResidence); }
    | AGIT_BEGIN NAME INTEGER SIEGE_GROUP
      {
          m_pResidenceData = g_ResidenceDB.InitializeResidenceData(grammar::SemanticInt32($3));
          if (m_pResidenceData != nullptr) {
              m_pResidenceData->m_nAgitAuction = 0;
              m_pResidenceData->m_nAgitNextAuction = 0;
              m_pResidenceData->m_nAgitAuctionMin = 0;
              m_pResidenceData->m_nAgitDeposit = 0;
              m_pResidenceData->m_nAgitLease = 0;
              m_pResidenceData->m_nAgitSize = 0;
              m_pResidenceData->m_nAgitParentCastleId = 0;
          }
          switch (grammar::SemanticInt64($4)) {
              case 0:
                  m_pResidence = new CSiegeAgit();
                  break;
              case 1:
                  m_pResidence = new CNpcBattleAgit();
                  break;
              case 2:
                  m_pResidence = new CTeamBattleAgit();
                  break;
              case 3:
                  m_pResidence = new CAuctionAgit();
                  break;
              case 4:
                  m_pResidence = new CMiniGameAgit();
                  break;
              case 5:
                  m_pResidence = new CCompeteAgit();
                  break;
              case 6:
                  m_pResidence = new CBaseCampAgit();
                  break;
              default:
                  // Current leaves the previous parser lane untouched here.
                  break;
          }
          if (m_pResidence != nullptr) {
              m_pResidence->SetData(m_pResidenceData);
              m_pResidence->SetName(SemanticWord($2));
              m_pResidence->SetResidenceID(grammar::SemanticInt32($3));
              m_pLstTerritory = nullptr;
          }
          m_tempCastleType = CastleType::CASTLE_NOTHING;
          m_tempTexRate = 0;
      }
      residence_territory_def_list AGIT_END
      { g_ResidenceDB.AddResidence(SemanticWord($2), m_pResidence); }
    | FORTRESS_BEGIN NAME INTEGER SIEGE_GROUP
      {
          m_pResidenceData = g_ResidenceDB.InitializeResidenceData(grammar::SemanticInt32($3));
          if (m_pResidenceData != nullptr) {
              m_pResidenceData->m_nFortressSiegeStatus = 0;
              m_pResidenceData->m_bFortressIsInBoundary = false;
              m_pResidenceData->m_nFortressContractStatus = 0;
              m_pResidenceData->m_nFortressParentCastleId = 0;
              m_pResidenceData->m_nFortressCastleTreasureLevel = 0;
              m_pResidenceData->m_nFortressOwnerRewardCycleCount = 0;
              m_pResidenceData->m_nFacilityLevel.fill(0);
          }
          m_pResidence = new CFortress();
          m_pResidence->SetData(m_pResidenceData);
          m_pResidence->SetName(SemanticWord($2));
          m_pResidence->SetResidenceID(grammar::SemanticInt32($3));
          m_tempCastleType = CastleType::CASTLE_NOTHING;
          m_pLstTerritory = nullptr;
      }
      residence_territory_def_list
      { g_ResidenceDB.AddResidence(SemanticWord($2), m_pResidence); }
      fortress_scale_def_list FORTRESS_END
    | CASTLE_SIEGE_MUSIC_BEGIN n11 CASTLE_SIEGE_MUSIC_END
    | FORTRESS_SIEGE_MUSIC_BEGIN n13 FORTRESS_SIEGE_MUSIC_END
    ;

n11
    : n11 ';' n12
    | n12
    | %empty
    ;

n12
    : '{' INTEGER ';' NAME ';' INTEGER '}'
      {
          const int action = 10;
          CastleSiegeMusicRecord record{
              grammar::SemanticInt32($2), grammar::SemanticInt32($6), StableHostWide(SemanticWord($4))
          };
          g_CastleSiegeMusicPlayer[action - 10].m_MusicData.push_back(record);
      }
    ;

n13
    : n13 ';' n14
    | n14
    | %empty
    ;

n14
    : '{' INTEGER ';' NAME ';' INTEGER '}'
      {
          const int action = 11;
          CastleSiegeMusicRecord record{
              grammar::SemanticInt32($2), grammar::SemanticInt32($6), StableHostWide(SemanticWord($4))
          };
          g_CastleSiegeMusicPlayer[action - 10].m_MusicData.push_back(record);
      }
    ;

residence_territory_def_list
    : residence_territory_def_list residence_territory_def
    | %empty
    ;

residence_territory_def
    : RESIDENCE_TERRITORY
      { m_pLstTerritory = &m_pResidence->m_lstResidenceTerritory; }
      EQUAL n58
      { m_pLstTerritory = nullptr; }
    | BATTLEFIELD_TERRITORY
      { m_pLstTerritory = &m_pResidence->m_lstBattleFieldTerritory; }
      EQUAL n58
      { m_pLstTerritory = nullptr; }
    | HEADQUARTER_TERRITORY
      { m_pLstTerritory = &m_pResidence->m_lstBattleCampTerritory; }
      EQUAL n58
      { m_pLstTerritory = nullptr; }
    | DOMAIN_TERRITORY { } EQUAL n60
    | MANOR_TERRITORY { } EQUAL n60
    | OWNER_RESTART_TERRITORY
      { m_pTerritory = &m_pResidence->m_OwnerRestartTerritory; }
      EQUAL n60
    | OWNER_RESTART_POINT_LIST
      {
          m_bVectorVector = true;
          m_pVectorVector = &m_pResidence->m_OwnerRestartPointList;
      }
      EQUAL n64
      { m_bVectorVector = false; }
    | OTHER_RESTART_VILLAGE_LIST
      {
          m_bVectorVector = true;
          m_pVectorVector = &m_pResidence->m_OtherRestartVillageList;
      }
      EQUAL n64
      { m_bVectorVector = false; }
    | BANISH_POINT_LIST
      {
          m_bVectorVector = true;
          m_pVectorVector = &m_pResidence->m_BanishPointList;
      }
      EQUAL n64
      { m_bVectorVector = false; }
    | CHAO_RESTART_POINT_LIST
      {
          m_bVectorVector = true;
          m_pVectorVector = &m_pResidence->m_ChaoRestartPointList;
      }
      EQUAL n64
      { m_bVectorVector = false; }
    | MERCENARY_POINT_LIST { } EQUAL n64
      { m_bVectorVector = false; }
    | OWNING_NPC_LIST
      { m_pResidence->m_OwningNPCList.clear(); }
      EQUAL n80
    | BOSS_NPC_LIST
      {
          m_pStringVector = &m_pResidence->m_BossNpcList;
          m_pStringVector->clear();
      }
      EQUAL n83
      {
          for (char16_t* const name : *m_pStringVector) {
              CObject* const object = g_ObjectDB.GetObjectByName(name);
              if (object == nullptr) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     "[%s][%d] CreateNPC Failed. (GetObject Failed)",
                  //     "castle_parser.y",
                  //     0x1fd)
                  // Previous: L2_LOG_ERROR(Log,
                  //     "[%s][%d] CreateNPC Failed. (GetObject Failed)",
                  //     "castle_parser.y", 0x1fd)
                  L2_LOG_ERROR(
                      Log,
                      "[castle parser] Boss NPC object is undefined; the entry "
                      "is skipped (source %s:%d).",
                      "castle_parser.y", 0x1fd
                  );
                  continue;
              }
              CNPC* const npc = object->IsNPC() ? object->CastNPC() : nullptr;
              if (npc == nullptr) {
                  const std::wstring hostName = target_utf16::ToHostCodeUnits(name);
                  // Log.Add(
                  //     LOG_ERROR,
                  //     "[%s][%d] CObjectDB::CreateNPC failed. [%S] is not a NPC.",
                  //     "castle_parser.y",
                  //     0x203,
                  //     hostName.c_str())
                  // Previous: L2_LOG_ERROR(Log,
                  //     "[%s][%d] CObjectDB::CreateNPC failed. [%S] is not a NPC.",
                  //     "castle_parser.y", 0x203, hostName.c_str())
                  L2_LOG_ERROR(
                      Log,
                      "[castle parser] Source %s:%d: object '%S' is not an NPC; "
                      "the entry is skipped.",
                      "castle_parser.y", 0x203, hostName.c_str()
                  );
                  continue;
              }
              const int residenceId = m_pResidence->m_data == nullptr ? 0 : m_pResidence->m_data->m_nResidenceID;
              npc->m_data->m_nResidenceID = residenceId;
              npc->m_data->m_nRulingAgit = residenceId;
          }
      }
    | NEXT_SIEGE EQUAL INTEGER '/' INTEGER '/' INTEGER '/' INTEGER '/' INTEGER
      {
          m_pResidence->m_data->m_NextSiege = MakeTime32(
              grammar::SemanticInt32($3), grammar::SemanticInt32($5), grammar::SemanticInt32($7),
              grammar::SemanticInt32($9), grammar::SemanticInt32($11)
          );
      }
    | SIEGE_TERM EQUAL INTEGER
      { m_pResidence->m_data->m_nSiegeTerm = grammar::SemanticInt32($3); }
    | RESIDENCE_STATE EQUAL INITIAL_NORMAL
      {
          m_pResidence->m_data->m_ResidenceState = static_cast<ResidenceState>(grammar::SemanticInt32($3));
      }
    | CONTROL_TOWER_LIST
      {
          m_pStringVector = &m_pResidence->m_ControlTowerNameList;
          m_pStringVector->clear();
      }
      EQUAL n83
      {
          m_pResidence->m_ControlTowerList.clear();
          for (char16_t* const name : *m_pStringVector) {
              CControlTower* const tower = CControlTowerDB::Inst().GetControlTower(name);
              if (tower == nullptr) {
                  continue;
              }
              tower->m_pOwningResidence = m_pResidence;
              m_pResidence->m_ControlTowerList.push_back(tower);
              if (tower->m_eControlTowerType == ControlTowerTypeEnum::Life && m_pResidence->IsCastle()) {
                  m_pResidence->CastCastle()->m_lstLifeControlTower.push_back(tower);
              }
          }
      }
    | BREAKABLE_WALL_LIST
      { m_pStringVector->clear(); }
      EQUAL n83
    | HOLYTHING EQUAL n70
    | OWNTHING EQUAL n75
    | BASIC_RATE EQUAL INTEGER
      { m_pResidenceData->m_nBasicRate = grammar::SemanticInt32($3); }
    | RESIDENCE_GUARD_MAPPING EQUAL n55
    | OBSERVER_IMPORT EQUAL NAME
      { m_pResidence->m_pObserver = g_SignBoardDb.Find(SemanticWord($3)); }
    | NEXT_AUCTION EQUAL INTEGER '/' INTEGER '/' INTEGER '/' INTEGER
      {
          m_pResidenceData->m_nAgitNextAuction = MakeTime32(
              grammar::SemanticInt32($3), grammar::SemanticInt32($5), grammar::SemanticInt32($7),
              grammar::SemanticInt32($9), 0
          );
      }
    | AGIT_AUCTION EQUAL INTEGER
      { m_pResidenceData->m_nAgitAuction = grammar::SemanticInt32($3); }
    | AGIT_AUCTION_MIN EQUAL INTEGER
      { m_pResidenceData->m_nAgitAuctionMin = grammar::SemanticInt32($3); }
    | AGIT_DEPOSIT EQUAL INTEGER
      { m_pResidenceData->m_nAgitDeposit = grammar::SemanticInt32($3); }
    | AGIT_LEASE EQUAL INTEGER
      { m_pResidenceData->m_nAgitLease = grammar::SemanticInt32($3); }
    | AGIT_SIZE EQUAL INTEGER
      { m_pResidenceData->m_nAgitSize = grammar::SemanticInt32($3); }
    | AGIT_PARENT_CASTLEID EQUAL INTEGER
      { m_pResidenceData->m_nAgitParentCastleId = grammar::SemanticInt32($3); }
    | BATTLE_ROYAL_FLAG_POINT_LIST
      {
          m_bPairVector = true;
          CWorldAgit* const worldAgit = m_pResidence->CastWorldAgit();
          CTeamBattleAgit* const teamBattle = worldAgit == nullptr ? nullptr : worldAgit->CastTeamBattleAgit();
          m_pPairVector = teamBattle == nullptr ? nullptr : &teamBattle->m_BattleRoyalFlagList;
          if (m_pPairVector != nullptr) {
              m_pPairVector->clear();
          }
      }
      EQUAL n67
      { m_bPairVector = false; }
    | FINAL_FLAG_POINT_LIST
      {
          m_bPairVector = true;
          CWorldAgit* const worldAgit = m_pResidence->CastWorldAgit();
          CTeamBattleAgit* const teamBattle = worldAgit == nullptr ? nullptr : worldAgit->CastTeamBattleAgit();
          m_pPairVector = teamBattle == nullptr ? nullptr : &teamBattle->m_FinalFlagList;
          if (m_pPairVector != nullptr) {
              m_pPairVector->clear();
          }
      }
      EQUAL n67
      { m_bPairVector = false; }
    | FINAL_OPEN_DOOR_LIST
      {
          m_pStringVector = &m_pResidence->m_GateNameList;
          m_pStringVector->clear();
      }
      EQUAL n83
      {
          CWorldAgit* const worldAgit = m_pResidence->CastWorldAgit();
          CTeamBattleAgit* const teamBattle = worldAgit == nullptr ? nullptr : worldAgit->CastTeamBattleAgit();
          if (teamBattle == nullptr) {
              break;
          }
          teamBattle->m_setFinalOpenDoor.clear();
          for (char16_t* const name : *m_pStringVector) {
              const std::wstring hostName = target_utf16::ToHostCodeUnits(name);
              CDoor* const door = g_DoorDB.GetDoor(name);
              if (door != nullptr) {
                  teamBattle->m_setFinalOpenDoor.insert(door);
                  door->SetOwningResidence(teamBattle);
              }
          }
      }
    | RESIDENCE_GATE_LIST
      {
          m_pStringVector = &m_pResidence->m_GateNameList;
          m_pIntVector = &m_tempIntVector;
          m_pStringVector->clear();
          m_pIntVector->clear();
      }
      EQUAL n85
      {
          m_pResidence->m_DoorList.clear();
          std::size_t index = 0;
          for (char16_t* const name : *m_pStringVector) {
              const std::wstring hostName = target_utf16::ToHostCodeUnits(name);
              CDoor* const door = g_DoorDB.GetDoor(name);
              if (door == nullptr) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"[%s][%d] door is null!!! bertdesu fix plz!!!",
                  //     L"castle_parser.y",
                  //     0x2d5)
                  // Previous: L2_LOG_ERROR(Log,
                  //     L"[%s][%d] door is null!!! bertdesu fix plz!!!",
                  //     L"castle_parser.y", 0x2d5)
                  L2_LOG_ERROR(
                      Log,
                      u"[castle parser] Door '%ls' is undefined; the residence "
                      u"entry is skipped (source %ls:%d).",
                      hostName.c_str(), u"castle_parser.y", 0x2d5
                  );
              } else {
                  m_pResidence->m_DoorList.push_back(door);
                  door->SetOwningResidence(m_pResidence);
                  if ((*m_pIntVector)[index] != 0) {
                      door->SetSiegeProof(true);
                  }
              }
              ++index;
          }
      }
    | DOOR_LIST
      {
          m_pStringVector = &m_tempStringVector;
          m_pIntVector = &m_tempIntVector;
          m_pStringVector->clear();
          m_pIntVector->clear();
      }
      EQUAL n85
      {
          CWorldAgit* const worldAgit = m_pResidence->CastWorldAgit();
          std::size_t index = 0;
          for (char16_t* const name : *m_pStringVector) {
              const std::wstring hostName = target_utf16::ToHostCodeUnits(name);
              CDoor* const door = g_DoorDB.GetDoor(name);
              if (door == nullptr) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Cannot find door [%s]",
                  //     hostName.c_str())
                  // Previous: L2_LOG_ERROR(Log,
                  //     L"Cannot find door [%s]", hostName.c_str())
                  L2_LOG_ERROR(
                      Log,
                      u"[castle parser] Door '%s' is undefined; the world clan "
                      u"hall entry is skipped.",
                      hostName.c_str()
                  );
              } else {
                  door->SetOwningResidence(worldAgit);
                  if ((*m_pIntVector)[index] != 0) {
                      door->SetSiegeProof(true);
                  }
              }
              ++index;
          }
          m_pStringVector = nullptr;
          m_pIntVector = nullptr;
      }
    | AGIT_PREPARATORY_TERM EQUAL INTEGER
      { m_pResidenceData->m_nMiniGameAgitPrepareTerm = grammar::SemanticInt32($3); }
    | AGIT_PROGRESS_TERM EQUAL INTEGER
      { m_pResidenceData->m_nMiniGameAgitProgressTerm = grammar::SemanticInt32($3); }
    | AGIT_CLOSE_TERM EQUAL INTEGER
      { m_pResidenceData->m_nMiniGameAgitCloseTerm = grammar::SemanticInt32($3); }
    | AGIT_COMPETE_DAY EQUAL INTEGER
      { m_pResidenceData->m_nCompeteAgitSiegeDay = grammar::SemanticInt32($3); }
    | AGIT_COMPETE_HOUR EQUAL INTEGER
      { m_pResidenceData->m_nCompeteAgitSiegehour = grammar::SemanticInt32($3); }
    | AGIT_COMPETE_MIN EQUAL INTEGER
      { m_pResidenceData->m_nCompeteAgitSiegeMin = grammar::SemanticInt32($3); }
    | AGIT_COMPETE_PROGRESS EQUAL INTEGER
      { m_pResidenceData->m_nCompeteAgitSiegeProgress = grammar::SemanticInt32($3); }
    | CHALLENGER_RESTART_POINT_LIST
      {
          m_bVectorVector = true;
          m_pVectorVector = &m_pResidence->m_ChallengerRestartPointList;
      }
      EQUAL n64
      { m_bVectorVector = false; }
    | AGIT_ENTRANCE_POINT
      {
          m_bVectorVector = true;
          m_pVectorVector = &m_pResidence->m_AgitEntrancePointList;
      }
      EQUAL n64
      { m_bVectorVector = false; }
    | AGIT_DELETE_ITEM_LIST
      {
          m_pIntVector = &m_pResidence->m_AgitDeleteItemList;
          m_pIntVector->clear();
      }
      EQUAL '{' n94 '}'
      { m_pIntVector = nullptr; }
    | DOMINION_MAIN_CASTLE EQUAL NAME
      {
          if (m_pResidence->IsDominion()) {
              CDominion* const dominion = m_pResidence->CastDominion();
              CResidence* const related = g_ResidenceDB.GetResidence(SemanticWord($3));
              if (related != nullptr) {
                  dominion->SetMainCastle(static_cast<CCastle*>(related));
              } else {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     "[%s][%d] Set castle to dominion Failed. "
                  //     "(CastCastle Failed)",
                  //     "castle_parser.y",
                  //     0x341)
                  // Previous: L2_LOG_ERROR(Log,
                  //     "[%s][%d] Set castle to dominion Failed. "
                  //     "(CastCastle Failed)", "castle_parser.y", 0x341)
                  L2_LOG_ERROR(
                      Log,
                      "[castle parser] Dominion castle reference is undefined; "
                      "the main castle is not assigned (source %s:%d).",
                      "castle_parser.y", 0x341
                  );
              }
          }
      }
    | PICKUP_OWNTHING EQUAL NAME
      {
          if (m_pResidence->IsDominion()) {
              CDominion* const dominion = m_pResidence->CastDominion();
              std::wstring name = target_utf16::ToHostCodeUnits(SemanticWord($3));
              dominion->SetPickupOwnThingName(name.data());
          }
      }
    | HIDE_NAME_ITEM EQUAL NAME
      {
          if (m_pResidence->IsDominion()) {
              CDominion* const dominion = m_pResidence->CastDominion();
              std::wstring name = target_utf16::ToHostCodeUnits(SemanticWord($3));
              dominion->SetHideNameItemName(name.data());
          }
      }
    | EDITOR_POS EQUAL '{' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          if (m_pResidence->IsCastle()) {
              m_pResidence->CastCastle()->SetEditorPos(
                  FVector(grammar::SemanticInt32($1), grammar::SemanticInt32($3), grammar::SemanticInt32($5))
              );
          }
      }
    | sys_castle_list
    | RESIDENCE_GRADE EQUAL INTEGER
      { m_pResidence->SetResidenceGrade(grammar::SemanticInt32($3)); }
    | RESIDENCE_DOMAINS EQUAL n96
    | DECO_NPC_SLOTS EQUAL n99
    | %empty
    ;

sys_castle_list
    : sys_castle_list sys_castle
    | sys_castle
    | %empty
    ;

sys_castle
    : SYS_CASTLE_BEGIN tax_rate_def_list SYS_CASTLE_END
    | GOOD_CASTLE_BEGIN
      { m_tempCastleType = CastleType::CASTLE_GOOD; }
      tax_rate_def_list GOOD_CASTLE_END
      {
          const CastleType::Enum castleType = CastleType::CASTLE_GOOD;
          TaxInfo& tax = m_pResidenceData->m_TaxInfoData[castleType];
          tax.m_nTaxRate = static_cast<std::uint32_t>(m_tempTexRate);
          tax.m_nTaxTaxRate = static_cast<std::uint32_t>(m_tempSysGetRate);
          tax.m_nTaxForResidenceRate = static_cast<std::uint32_t>(m_tempTributeRate);
          tax.m_nBuyTaxRate = static_cast<std::uint32_t>(m_tempBuyRate);
          tax.m_nBuyTaxTaxRate = static_cast<std::uint32_t>(m_tempBuySysGetRate);
          tax.m_nBuyTaxForResidenceRate = static_cast<std::uint32_t>(m_tempBuyTributeRate);
          if (m_pResidence->IsCastle()) {
              m_pResidence->CastCastle()->MakeEventController(castleType, m_nEditorID);
          }
      }
    | BAD_CASTLE_BEGIN
      { m_tempCastleType = CastleType::CASTLE_BAD; }
      tax_rate_def_list BAD_CASTLE_END
      {
          const CastleType::Enum castleType = CastleType::CASTLE_BAD;
          TaxInfo& tax = m_pResidenceData->m_TaxInfoData[castleType];
          tax.m_nTaxRate = static_cast<std::uint32_t>(m_tempTexRate);
          tax.m_nTaxTaxRate = static_cast<std::uint32_t>(m_tempSysGetRate);
          tax.m_nTaxForResidenceRate = static_cast<std::uint32_t>(m_tempTributeRate);
          tax.m_nBuyTaxRate = static_cast<std::uint32_t>(m_tempBuyRate);
          tax.m_nBuyTaxTaxRate = static_cast<std::uint32_t>(m_tempBuySysGetRate);
          tax.m_nBuyTaxForResidenceRate = static_cast<std::uint32_t>(m_tempBuyTributeRate);
          if (m_pResidence->IsCastle()) {
              m_pResidence->CastCastle()->MakeEventController(castleType, m_nEditorID);
          }
      }
    | tax_rate_def_list
      {
          const CastleType::Enum castleType = CastleType::CASTLE_NOTHING;
          TaxInfo& tax = m_pResidenceData->m_TaxInfoData[castleType];
          tax.m_nTaxRate = static_cast<std::uint32_t>(m_tempTexRate);
          tax.m_nTaxTaxRate = static_cast<std::uint32_t>(m_tempSysGetRate);
          tax.m_nTaxForResidenceRate = static_cast<std::uint32_t>(m_tempTributeRate);
          tax.m_nBuyTaxRate = static_cast<std::uint32_t>(m_tempBuyRate);
          tax.m_nBuyTaxTaxRate = static_cast<std::uint32_t>(m_tempBuySysGetRate);
          tax.m_nBuyTaxForResidenceRate = static_cast<std::uint32_t>(m_tempBuyTributeRate);
          if (m_pResidence->IsCastle()) {
              m_pResidence->CastCastle()->MakeEventController(castleType, m_nEditorID);
          }
      }
    | %empty
    ;

tax_rate_def_list
    : tax_rate_def_list tax_rate_def
    | tax_rate_def
    | %empty
    ;

tax_rate_def
    : TAX_RATE EQUAL INTEGER
      {
          if (m_pResidence->IsExtended()) {
              yysyntaxerror();
          } else {
              m_tempTexRate = grammar::SemanticInt32($3);
          }
      }
    | TAX_SYSGET_RATE EQUAL INTEGER
      { m_tempSysGetRate = grammar::SemanticInt32($3); }
    | TRIBUTE_RATE EQUAL INTEGER
      { m_tempTributeRate = grammar::SemanticInt32($3); }
    | BUY_TAX_RATE EQUAL INTEGER
      { m_tempBuyRate = grammar::SemanticInt32($3); }
    | BUY_TAX_SYSGET_RATE EQUAL INTEGER
      { m_tempBuySysGetRate = grammar::SemanticInt32($3); }
    | BUY_TRIBUTE_RATE EQUAL INTEGER
      { m_tempBuyTributeRate = grammar::SemanticInt32($3); }
    | RESIDENCE_SKILL_LIST
      {
          m_pStringVector = &m_TempStringVector;
          m_pStringVector->clear();
      }
      EQUAL n83
      {
          for (char16_t* const skillName : *m_pStringVector) {
              const std::wstring hostName = target_utf16::ToHostCodeUnits(skillName);
              const CSkillInfo* const skill = g_SkillDB.GetSkillInfoByName(hostName.c_str());
              if (skill == nullptr) {
                  const std::wstring residenceName =
                      target_utf16::ToHostCodeUnits(m_pResidence->m_data->m_sResidenceName.data());
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"residence skill %s in %s is not valid",
                  //     hostName.c_str(),
                  //     residenceName.c_str())
                  // Previous: L2_LOG_ERROR(Log,
                  //     L"residence skill %s in %s is not valid",
                  //     hostName.c_str(), residenceName.c_str())
                  L2_LOG_ERROR(
                      Log,
                      u"[castle parser] Skill '%s' is undefined for residence "
                      u"'%s'; the skill is omitted.",
                      hostName.c_str(), residenceName.c_str()
                  );
                  continue;
              }
              m_pResidence->m_ResidenceSkills[m_tempCastleType].push_back(skill);
          }
      }
    | EDITOR_ID EQUAL INTEGER
      { m_nEditorID = grammar::SemanticInt32($3); }
    | %empty
    ;

fortress_scale_def_list
    : fortress_scale_def_list fortress_scale_def
    | %empty
    ;

fortress_scale_def
    : FORTRESS_SCALE EQUAL IDENTIFIER
      {
          if (m_pResidence != nullptr) {
              CFortress* const fortress = m_pResidence->CastFortress();
              if (fortress == nullptr) {
                  break;
              }
              const std::u16string_view scale(SemanticWord($3));
              if (scale == u"small") {
                  fortress->SetScale(FST_SMALL);
              } else if (scale == u"large") {
                  fortress->SetScale(FST_LARGE);
              }
          }
      }
    | FORTRESS_RELATED_CASTLE EQUAL '{' n50 '}'
      {
          if (m_pResidence != nullptr) {
              if (CFortress* const fortress = m_pResidence->CastFortress()) {
                  fortress->AddToRelatedCastles();
              }
          }
      }
    | FORTRESS_FLAGPOLE EQUAL '{' NAME ';' '{' INTEGER ';' INTEGER ';' INTEGER '}' ';' INTEGER '}'
      {
          if (m_pResidence != nullptr) {
              CFortress* const fortress = m_pResidence->CastFortress();
              if (fortress == nullptr) {
                  break;
              }
              auto* const flagpole = new CFortressFlagpole(fortress);
              flagpole->SetBreakable(false);
              flagpole->SetPos(
                  FVector(grammar::SemanticInt32($7), grammar::SemanticInt32($9), grammar::SemanticInt32($11))
              );
              flagpole->m_data->m_nStaticObjectStart = grammar::SemanticInt32($14);
              fortress->SetFlagpole(flagpole);
          }
      }
    | FORTRESS_FLAG EQUAL NAME ';'
      {
          const int classId = g_ObjectDB.GetClassIdFromName(SemanticWord($3), false);
          if (m_pResidence != nullptr && m_pResidence->CastFortress() != nullptr) {
              CFortress::SetFlagItemId(classId);
          }
      }
      '{' n51 '}'
    ;

n50
    : n50 ';' NAME
      {
          if (m_pResidence != nullptr) {
              CFortress* const fortress = m_pResidence->CastFortress();
              if (fortress == nullptr) {
                  break;
              }
              CResidence* const related = g_ResidenceDB.GetResidence(SemanticWord($3));
              if (related != nullptr) {
                  const int residenceId = related->m_data == nullptr ? 0 : related->m_data->m_nResidenceID;
                  fortress->AddRelatedCastle(residenceId);
              }
          }
      }
    | NAME
      {
          if (m_pResidence != nullptr) {
              CFortress* const fortress = m_pResidence->CastFortress();
              if (fortress == nullptr) {
                  break;
              }
              CResidence* const related = g_ResidenceDB.GetResidence(SemanticWord($1));
              if (related != nullptr) {
                  const int residenceId = related->m_data == nullptr ? 0 : related->m_data->m_nResidenceID;
                  fortress->AddRelatedCastle(residenceId);
              }
          }
      }
    | %empty
    ;

n51
    : n51 ';' n52
    | n52
    ;

n52
    : { } n66
      {
          if (m_pResidence != nullptr) {
              if (CFortress* const fortress = m_pResidence->CastFortress()) {
                  fortress->AddFlag(m_TempVector);
              }
          }
      }
    ;

name_def
    : NAME
      {
          const std::u16string_view scale(SemanticWord($1));
          if (scale == u"none") {
              std::fill(std::begin(m_SSQGuardType), std::end(m_SSQGuardType), false);
          }
          if (scale == u"all") {
              std::fill(std::begin(m_SSQGuardType), std::end(m_SSQGuardType), true);
          }
      }
    | SSQ_VALUE
      {
          std::fill(std::begin(m_SSQGuardType), std::end(m_SSQGuardType), false);
      }
    | name_def ';' SSQ_VALUE
      { m_SSQGuardType[grammar::SemanticInt32($3)] = true; }
    ;

n55
    : '{' n56 '}'
    | '{' '}'
    ;

n56
    : '{' integer_def '}'
    | n56 ';' '{' integer_def '}'
    ;

integer_def
    : INTEGER ';' TELEPORTER_GROUP ';' NOTMOVE_MOVE ';' INTEGER ';' INTEGER
      {
          m_pResidenceGuardInfo = new ResidenceGuardInfo(
              static_cast<ResidenceGuardTypeEnum>(grammar::SemanticInt32($3)), grammar::SemanticInt64($5) != 0,
              static_cast<unsigned int>(grammar::SemanticInt32($7)),
              static_cast<unsigned int>(grammar::SemanticInt32($9)), 1
          );
          m_pResidence->m_mapResidenceGuardItem.emplace(
              static_cast<std::uint32_t>(grammar::SemanticInt32($1)), m_pResidenceGuardInfo
          );
      }
    | INTEGER ';' TELEPORTER_GROUP ';' NOTMOVE_MOVE ';' INTEGER ';' INTEGER ';' '{' name_def '}'
      {
          char ssqType = 0;
          for (int index = 0; index < 3; ++index) {
              if (m_SSQGuardType[index]) {
                  ssqType = static_cast<char>(ssqType | (1 << index));
              }
          }
          m_pResidenceGuardInfo = new ResidenceGuardInfo(
              static_cast<ResidenceGuardTypeEnum>(grammar::SemanticInt32($3)), grammar::SemanticInt64($5) != 0,
              static_cast<unsigned int>(grammar::SemanticInt32($7)),
              static_cast<unsigned int>(grammar::SemanticInt32($9)), ssqType
          );
          const std::uint32_t key = static_cast<std::uint32_t>(grammar::SemanticInt32($1));
          m_pResidence->m_mapResidenceGuardItem.emplace(key, m_pResidenceGuardInfo);
          m_pResidence->m_GuardNumMap.emplace(static_cast<int>(key), 0);
      }
    ;

n58
    : '{' n59 '}'
    | '{' '}'
    ;

n59
    : n60
    | n59 ';' n60
    ;

n60
    : '{'
      {
          if (m_pLstTerritory != nullptr) {
              m_pTerritory = new CTerritory();
              m_pLstTerritory->push_back(m_pTerritory);
          }
      }
      n62 '}'
    | '{' '}'
    ;

n62
    : n63
    | n62 ';' n63
    ;

n63
    : '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          if (grammar::SemanticInt64($6) < m_pTerritory->m_Bottom) {
              m_pTerritory->m_Bottom = grammar::SemanticInt32($6);
          }
          if (m_pTerritory->m_Top < grammar::SemanticInt64($8)) {
              m_pTerritory->m_Top = grammar::SemanticInt32($8);
          }
          m_pVector = new FVector(grammar::SemanticInt32($2), grammar::SemanticInt32($4), 0);
          m_pTerritory->AddTerritoryPoint(m_pVector);
      }
    | '{' ';' ';' ';' '}'
    ;

n64
    : '{' n65 '}'
    | '{' '}'
    ;

n65
    : n66
    | n65 ';' n66
    ;

n66
    : '{' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          const FVector position(
              grammar::SemanticInt32($2), grammar::SemanticInt32($4), grammar::SemanticInt32($6)
          );
          if (m_bVectorVector && m_pVectorVector != nullptr) {
              m_pVectorVector->push_back(position);
          } else if (m_pVector != nullptr) {
              *m_pVector = position;
          }
      }
    | '{' ';' ';' '}'
    ;

n67
    : '{' n68 '}'
    | '{' '}'
    ;

n68
    : n69
    | n68 ';' n69
    ;

n69
    : '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          const FVector position(
              grammar::SemanticInt32($2), grammar::SemanticInt32($4), grammar::SemanticInt32($6)
          );
          FAngle angle{};
          angle.Yaw = grammar::SemanticInt32($8);
          if (m_bPairVector && m_pPairVector != nullptr) {
              m_pPairVector->push_back(std::make_pair(position, angle));
          } else if (m_pVector != nullptr && m_pAngle != nullptr) {
              *m_pVector = position;
              *m_pAngle = angle;
          }
      }
    | '{' ';' ';' '}'
    ;

n70
    : '{' n71 '}'
    | '{' '}'
    ;

n71
    : n72
    | n71 ';' n72
    ;

n72
    : '{' NAME
      {
          const char16_t* const name = SemanticWord($2);
          m_TempString.assign(std::u16string_view(name));
      }
      ';'
      {
          m_bVectorVector = false;
          m_TempVector = FVector();
          m_pVector = &m_TempVector;
      }
      n66 ';' integer_def_2 '}'
      {
          if (!m_TempString.empty()) {
              auto* const holyThing = new CHolyThing();
              holyThing->SetBreakable(false);
              holyThing->SetPos(m_TempVector);
              holyThing->SetDir(m_TempAngle);
              holyThing->SetCollisionRadius(m_dTemp1);
              holyThing->SetCollisionHeight(m_dTemp2);
              holyThing->m_pOwningResidence = m_pResidence;
              holyThing->m_nOwningResidenceID = m_pResidence->m_data == nullptr ? 0 : m_pResidence->m_data->m_nResidenceID;
              m_pResidence->m_HolyThingList.push_back(holyThing);
              const std::wstring className = target_utf16::ToHostCodeUnits(m_TempString.c_str());
              CHolyThing::AddHolyThing(className.c_str(), holyThing);
          }
      }
    ;

n75
    : '{' n76 '}'
    | '{' '}'
    ;

n76
    : n77
    | n76 ';' n77
    ;

n77
    : '{' NAME
      {
          const char16_t* const name = SemanticWord($2);
          m_TempString.assign(std::u16string_view(name));
      }
      ';'
      {
          m_bVectorVector = false;
          m_TempVector = FVector();
          m_pVector = &m_TempVector;
      }
      n66 ';' integer_def_2 '}'
      {
          if (!m_TempString.empty()) {
              auto* const ownThing = new COwnThing();
              ownThing->SetBreakable(true);
              ownThing->SetPos(m_TempVector);
              ownThing->SetDir(m_TempAngle);
              ownThing->SetCollisionRadius(m_dTemp1);
              ownThing->SetCollisionHeight(m_dTemp2);
              if (m_pResidence != nullptr) {
                  ownThing->SetOriginalResidenceID(
                      m_pResidence->m_data == nullptr ? 0 : m_pResidence->m_data->m_nResidenceID
                  );
                  if (!m_pResidence->IsDominion()) {
                      // Log.Add(
                      //     LOG_ERROR,
                      //     L"Residence that isn't dominion, has ownthing.")
                      // Previous: L2_LOG_ERROR(Log,
                      //     L"Residence that isn't dominion, has ownthing.")
                      L2_LOG_ERROR(
                          Log,
                          u"[castle parser] A non-dominion residence defines an "
                          u"ownthing; it remains registered without dominion "
                          u"ownership."
                      );
                  } else {
                      CDominion* const dominion = m_pResidence->CastDominion();
                      dominion->m_pMainOwnThing = ownThing;
                      dominion->AddPos(m_TempVector);
                  }
              }
              const std::wstring className = target_utf16::ToHostCodeUnits(m_TempString.c_str());
              COwnThing::AddOwnThing(className.c_str(), ownThing);
          } else if (m_pResidence != nullptr && m_pResidence->IsDominion()) {
              m_pResidence->CastDominion()->AddPos(m_TempVector);
          }
      }
    ;

n80
    : '{' n81 '}'
    | '{' '}'
    ;

n81
    : n82
    | n81 ';' n82
    ;

n82
    : '{' NAME ';' INTEGER '}'
      {
          m_pOwningNPCData = new OwningNPCData();
          CopyUtf16(m_pOwningNPCData->pszName, std::size(m_pOwningNPCData->pszName), SemanticWord($2));
          m_pOwningNPCData->bFunctional = grammar::SemanticInt64($4) != 0;
          CObject* const object = g_ObjectDB.GetObjectByName(SemanticWord($2));
          if (object == nullptr) {
              // Log.Add(
              //     LOG_ERROR,
              //     "[%s][%d] CreateNPC Failed. (GetObject Failed)",
              //     "castle_parser.y",
              //     0x5ec)
              // Previous: L2_LOG_ERROR(Log,
              //     "[%s][%d] CreateNPC Failed. (GetObject Failed)",
              //     "castle_parser.y", 0x5ec)
              L2_LOG_ERROR(
                  Log,
                  "[castle parser] Owning NPC object is undefined; the entry is "
                  "skipped (source %s:%d).",
                  "castle_parser.y", 0x5ec
              );
              break;
          }
          CNPC* const npc = object->IsNPC() ? object->CastNPC() : nullptr;
          if (npc == nullptr) {
              const std::wstring hostName = target_utf16::ToHostCodeUnits(SemanticWord($2));
              // Log.Add(
              //     LOG_ERROR,
              //     "[%s][%d] CObjectDB::CreateNPC failed. [%S] is not a NPC.",
              //     "castle_parser.y",
              //     0x5f2,
              //     hostName.c_str())
              // Previous: L2_LOG_ERROR(Log,
              //     "[%s][%d] CObjectDB::CreateNPC failed. [%S] is not a NPC.",
              //     "castle_parser.y", 0x5f2, hostName.c_str())
              L2_LOG_ERROR(
                  Log,
                  "[castle parser] Source %s:%d: object '%S' is not an NPC; "
                  "the entry is skipped.",
                  "castle_parser.y", 0x5f2, hostName.c_str()
              );
              break;
          }
          m_pOwningNPCData->pNPC = npc;
          m_pResidence->m_OwningNPCList.push_back(m_pOwningNPCData);
          npc->m_data->m_nResidenceID = m_pResidence->m_data == nullptr ? 0 : m_pResidence->m_data->m_nResidenceID;
          npc->m_data->m_bNotFunctionable = m_pOwningNPCData->bFunctional;
      }
    ;

n83
    : '{' name_def_2 '}'
    | '{' '}'
    ;

name_def_2
    : NAME
      { m_pStringVector->push_back(SemanticWord($1)); }
    | name_def_2 ';' NAME
      { m_pStringVector->push_back(SemanticWord($3)); }
    ;

n85
    : '{' n86 '}'
    | '{' '}'
    ;

n86
    : n87
    | n86 ';' n87
    ;

n87
    : '{' NAME ';' INTEGER '}'
      {
          m_pStringVector->push_back(SemanticWord($2));
          m_pIntVector->push_back(grammar::SemanticInt32($4));
      }
    ;

base_siege_time_def_list
    : base_siege_time_def_list base_siege_time_def
    | %empty
    ;

base_siege_time_def
    : BASE_SIEGE_TIME EQUAL INTEGER '/' INTEGER '/' INTEGER '/' INTEGER '/' INTEGER
      {
          CCastle::SetBaseSiegeTime(MakeTime32(
              grammar::SemanticInt32($3), grammar::SemanticInt32($5), grammar::SemanticInt32($7),
              grammar::SemanticInt32($9), grammar::SemanticInt32($11)
          ));
      }
    | RESERVABLE_SIEGE_TIME EQUAL n90
    | MAX_CONCURRENT_SIEGE EQUAL INTEGER
      { CCastle::SetMaxConcurrentSiege(grammar::SemanticInt32($3)); }
    ;

n90
    : '{' n91 '}'
    | '{' '}'
    ;

n91
    : n91 ';' n92
    | n92
    ;

n92
    : '{' SAT_SUN ';' INTEGER '}'
      {
          CCastle::AddReservableSiegeTime(grammar::SemanticInt32($2), grammar::SemanticInt32($4));
      }
    ;

integer_def_2
    : INTEGER ';' FLOAT_NUMBER ';' FLOAT_NUMBER
      {
          m_TempAngle.Yaw = grammar::SemanticInt32($1);
          m_dTemp1 = grammar::SemanticDouble($3);
          m_dTemp2 = grammar::SemanticDouble($5);
      }
    | ';' ';'
    ;

n94
    : name_def_3 ';' n94
    | name_def_3
    | %empty
    ;

name_def_3
    : NAME
      {
          const int classId = g_ObjectDB.GetClassIdFromName(SemanticWord($1), false);
          if (classId == -1) {
              const std::wstring hostName = target_utf16::ToHostCodeUnits(SemanticWord($1));
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined word[%s] in line [%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              // Previous: L2_LOG_ERROR(Log,
              //     L"Undefined word[%s] in line [%d]",
              //     hostName.c_str(), m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"[castle parser] Item '%s' is undefined at line %d; class ID "
                  u"-1 is retained in the residence list.",
                  hostName.c_str(), m_lexer.line + 1
              );
          }
          m_pIntVector->push_back(classId);
      }
    ;

n96
    : '{' n97 '}'
    | '{' '}'
    ;

n97
    : n97 ';' integer_def_3
    | integer_def_3
    ;

integer_def_3
    : INTEGER
      { m_pResidence->AddResidenceDomain(grammar::SemanticInt32($1)); }
    ;

n99
    : '{' n100 '}'
    | '{' '}'
    ;

n100
    : n100 ';' n101
    | n101
    ;

n101
    : '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          m_pResidence->AddDecoNpcSlot(
              FVector(grammar::SemanticInt32($2), grammar::SemanticInt32($4), grammar::SemanticInt32($6)),
              grammar::SemanticInt32($8)
          );
      }
    ;

%%
