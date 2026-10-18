// CParserForDoor grammar (original L2Server/parser_door.y).
// Bison generates ParserDoor.cpp and ParserDoorTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserDoor.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/Area.h"
#include "L2Server/src/Chair.h"
#include "L2Server/src/ControlTower.h"
#include "L2Server/src/Door.h"
#include "L2Server/src/EventController.h"
#include "L2Server/src/ObjectDB.h"
#include "L2Server/src/SignBoard.h"
#include "L2Server/src/Trap.h"
#include "L2Server/src/WorldCollision.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/Territory.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "ParserDoorTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForDoor::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserDoorTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

char16_t AsciiLower(const char16_t value) {
    if (value >= u'A' && value <= u'Z') {
        return static_cast<char16_t>(value + (u'a' - u'A'));
    }
    return value;
}

bool EqualsAsciiIgnoreCase(const char16_t* value, const char16_t* expected) {
    if (value == nullptr || expected == nullptr) {
        return value == expected;
    }
    while (*value != u'\0' && *expected != u'\0') {
        if (AsciiLower(*value) != AsciiLower(*expected)) {
            return false;
        }
        ++value;
        ++expected;
    }
    return *value == u'\0' && *expected == u'\0';
}

std::size_t Utf16Length(const char16_t* value) {
    if (value == nullptr) {
        return 0;
    }
    const char16_t* cursor = value;
    while (*cursor != u'\0') {
        ++cursor;
    }
    return static_cast<std::size_t>(cursor - value);
}

template <std::size_t N>
void CopyUtf16(char16_t (&destination)[N], const char16_t* source) {
    if (source == nullptr) {
        destination[0] = u'\0';
        return;
    }
    const std::size_t count = std::min(Utf16Length(source), N - 1);
    std::copy_n(source, count, destination);
    destination[count] = u'\0';
}

void NormalizeDoorRange(FVector (&range)[4]) {
    if (LeftTurn(range[1], range[2], range[3]) * LeftTurn(range[0], range[1], range[2]) == -1) {
        SwapFVector(range[2], range[3]);
    } else if (LeftTurn(range[0], range[1], range[2]) * LeftTurn(range[2], range[3], range[0]) == -1) {
        SwapFVector(range[1], range[2]);
    }

    if (LeftTurn(range[0], range[1], range[2]) == -1) {
        SwapFVector(range[0], range[3]);
        SwapFVector(range[1], range[2]);
    }
}

} // namespace

CParserForDoor::CParserForDoor() : m_lexer(), m_EditIdSet(), m_TempVector(), m_Name(), m_TempStrings() {
    m_bFirstStep = false;
    m_pTrapInfo = nullptr;
    m_pTempTerritory = nullptr;
}

CParserForDoor::~CParserForDoor() = default;

int CParserForDoor::Create(CDoorDB* const doorDb, char16_t* const fileBuffer, const int fileSize) {
    m_pDoorDB = doorDb;
    return m_lexer.Create(fileBuffer, fileSize);
}
}

%token DOOR_BEGIN
%token DOOR_END
%token CHAIR_BEGIN
%token CHAIR_END
%token CHAIR_GROUP_ID
%token SIT_RANGE
%token SIGNBOARD_BEGIN
%token SIGNBOARD_END
%token CONTROLTOWER_BEGIN
%token CONTROLTOWER_END
%token CONTROL_AREA
%token CONTROLTOWER_TYPE
%token TOGGLE
%token TRAP_BEGIN
%token TRAP_END
%token TRAP_AREA
%token EDITOR_ID
%token HTML
%token DISPLAY_NPC
%token DISPLAY_NPC_WORKING
%token DISPLAY_NPC_NOT_WORKING
%token MASTER_NAME
%token MASTER_OPEN_EVENT
%token MASTER_CLOSE_EVENT
%token DEFAULT_STATUS
%token HP_SHOWABLE
%token INACTIVE_TIME
%token TEXTURE_NAME
%token MAP_POS
%token OBSERVER_POS
%token OBSERVER_GROUP
%token POS
%token RANGE
%token HEIGHT
%token HP
%token PHYSICAL_DEFENCE
%token MAGICAL_DEFENCE
%token PARENT
%token IS_OPEN
%token TYPE
%token LEVEL
%token OPEN_METHOD
%token STATIC_OBJECT_ID
%token CLOSE_TIME
%token OPEN_TIME
%token RANDOM_TIME
%token OPEN_BY_DOOR_NAME_SKILL
%token INSTANTZONE_ID
%token EMITTER_ID
%token TARGETABLE
%token STEALTH
%token CHECK_COLLISION
%token NEED_TO_LOG
%token NAME
%token IDENTIFIER
%token INTEGER
%token NORMAL_TYPE_GROUP
%token BY_VALUE
%token LIFE_CONTROL_TRAP_CONTROL_SUPPORT_CONTROL
%token ACT_VALUE
%start n1

%%

n1
    : n1 n2
    | n2
    ;

n2
    : door
    | signboard
    | controltower
    | chair
    | trap
    ;

door
    : DOOR_BEGIN NAME
      {
          m_pDoor = new CDoor;
          const char16_t* const name = grammar::SemanticPointer<char16_t>($2);
          m_Name = std::u16string(name != nullptr ? name : u"");
          CopyUtf16(m_pDoor->m_sDoorName, m_Name.c_str());
          m_nEmitterID = 0;
          m_nCollision = 1;
      }
      pos_def_3_list DOOR_END
      {
          m_pDoor->CalcCenterPos();
          m_pDoorDB->AddDoor(m_Name.c_str(), m_pDoor);
          if (m_nCollision >= 1) {
              FVector range[4] = {
                  m_pDoor->m_Range[0],
                  m_pDoor->m_Range[1],
                  m_pDoor->m_Range[2],
                  m_pDoor->m_Range[3],
              };
              NormalizeDoorRange(range);

              CPlaneCollision* collisions[4] = {
                  new CPlaneCollision(
                      range[0], range[1], static_cast<double>(m_pDoor->m_Height), m_pDoor->m_InstantZoneKey
                  ),
                  new CPlaneCollision(
                      range[1], range[2], static_cast<double>(m_pDoor->m_Height), m_pDoor->m_InstantZoneKey
                  ),
                  new CPlaneCollision(
                      range[2], range[3], static_cast<double>(m_pDoor->m_Height), m_pDoor->m_InstantZoneKey
                  ),
                  new CPlaneCollision(
                      range[3], range[0], static_cast<double>(m_pDoor->m_Height), m_pDoor->m_InstantZoneKey
                  ),
              };
              const char16_t* const collisionName = grammar::SemanticPointer<char16_t>($2);
              for (CPlaneCollision* const collision : collisions) {
                  g_WorldPlaneCollision.Add(std::u16string(collisionName != nullptr ? collisionName : u""), collision);
              }
          }
          if (m_nEmitterID == 0) {
              m_pDoor->m_pEventController = nullptr;
          } else {
              m_pDoor->m_pEventController = new CEventController;
              m_pDoor->m_pEventController->EstablishEventController(m_nEmitterID, m_pDoor->GetPos());
          }
      }
    ;

chair
    : CHAIR_BEGIN NAME
      { m_pChair = new CChair; }
      pos_def_2_list CHAIR_END
      {
          g_PendingStaticObjects.RegisterAsPending(m_pChair);
          if (m_pChair->m_nGroupId > 0) {
              m_pChair->m_nGroupType = 1;
              g_ChairManager.RegisterGroupChair(m_pChair->m_nGroupId, m_pChair);
          } else {
              m_pChair->m_nGroupType = 0;
          }
      }
    ;

signboard
    : SIGNBOARD_BEGIN NAME
      {
          m_pSignBoard = new CSignBoard;
          const char16_t* const name = grammar::SemanticPointer<char16_t>($2);
          m_Name = std::u16string(name != nullptr ? name : u"");
      }
      pos_def_list SIGNBOARD_END
      { g_SignBoardDb.Add(m_Name.c_str(), m_pSignBoard); }
    ;

trap
    : TRAP_BEGIN NAME
      {
          const char16_t* const name = grammar::SemanticPointer<char16_t>($2);
          m_Name = std::u16string(name != nullptr ? name : u"");
          const int classId = g_ObjectDB.GetClassIdFromName(name, false);
          if (classId < 1) {
              // const std::wstring hostName =
              //     target_utf16::ToHostCodeUnits(name)
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"cannot find npc[%s]",
              //     hostName.c_str())
              // L2_LOG_ERROR(Log,
              //     L"cannot find npc[%s]",
              //     hostName.c_str())
              L2_LOG_ERROR(
                  Log,
                  u"Door/trap data line %lld: trap NPC '%ls' was not found (class ID %d); trap creation continues with "
                  u"this ID.",
                  static_cast<long long>(m_lexer.line) + 1, name == nullptr ? L"<null>" : hostName.c_str(), classId
              );
          }
          m_pTrapInfo = new CWorldTrapInfo;
          m_pTrapInfo->m_Id = classId;
          m_pTempTerritory = new CTerritory;
          m_pTempTerritory->SetName(const_cast<char16_t*>(name));
      }
      trap_area_def_list TRAP_END
      {
          m_pTrapInfo->SetTrapArea(m_pTempTerritory);
          g_WorldTrapDB.AddTrapInfo(m_pTrapInfo);
          m_pTempTerritory = nullptr;
          m_pTrapInfo = nullptr;
      }
    ;

controltower
    : CONTROLTOWER_BEGIN NAME
      {
          m_pControlTower = new CControlTower;
          const char16_t* const name = grammar::SemanticPointer<char16_t>($2);
          if (Utf16Length(name) <= 63) {
              CopyUtf16(m_pControlTower->m_sControlTowerName, name);
          }
      }
      controltower_type_def_list CONTROLTOWER_END
      {
          CControlTowerDB::Inst().AddControlTower(grammar::SemanticPointer<char16_t>($2), m_pControlTower);
      }
    ;

pos_def_3_list
    : pos_def_3_list pos_def_3
    | %empty
    ;

pos_def_2_list
    : pos_def_2_list pos_def_2
    | %empty
    ;

pos_def_list
    : pos_def_list pos_def
    | %empty
    ;

trap_area_def_list
    : trap_area_def_list trap_area_def
    | %empty
    ;

controltower_type_def_list
    : controltower_type_def_list controltower_type_def
    | %empty
    ;

controltower_type_def
    : CONTROLTOWER_TYPE '=' LIFE_CONTROL_TRAP_CONTROL_SUPPORT_CONTROL
      {
          m_pControlTower->m_eControlTowerType = static_cast<ControlTowerTypeEnum>(grammar::SemanticInt32($3));
      }
    | TOGGLE '=' IDENTIFIER
      {
          m_pControlTower->m_bTogglable = EqualsAsciiIgnoreCase(grammar::SemanticPointer<char16_t>($3), u"true");
      }
    | POS '=' n29
      { m_pControlTower->SetPos(m_TempVector); }
    | EDITOR_ID '=' INTEGER
      {
          const auto registerEditorId = [&](CStaticObject* const object) {
              const int editorId = grammar::SemanticInt32($3);
              object->m_data->m_nEditorID = editorId;
              if (m_bFirstStep) {
                  return;
              }
              if (m_EditIdSet.count(editorId) != 0) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Duplicated Edit Id [%d] in line [%d]",
                  //     editorId,
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"Duplicated Edit Id [%d] in line [%d]",
                  //     editorId,
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      u"Door/trap data line %lld: duplicate editor ID %d; the ID remains assigned to this static object and "
                      u"parsing continues.",
                      static_cast<long long>(m_lexer.line) + 1, editorId
                  );
                  return;
              }
              m_EditIdSet.insert(editorId);
          };
          registerEditorId(m_pControlTower);
      }
    | DISPLAY_NPC_WORKING '=' NAME
      {
          const char16_t* const name = grammar::SemanticPointer<char16_t>($3);
          const int classId = g_ObjectDB.GetClassIdFromName(name, false);
          if (classId < 1) {
              // const std::wstring hostName =
              //     target_utf16::ToHostCodeUnits(name)
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"cannot find npc[%s]",
              //     hostName.c_str())
              // L2_LOG_ERROR(Log,
              //     L"cannot find npc[%s]",
              //     hostName.c_str())
              L2_LOG_ERROR(
                  Log,
                  u"Door/trap data line %lld: control tower working display NPC '%ls' was not found; class ID %d is still "
                  u"assigned.",
                  static_cast<long long>(m_lexer.line) + 1, name == nullptr ? L"<null>" : hostName.c_str(), classId
              );
          }
          m_pControlTower->m_nDisplayNPCID_Working = classId;
      }
    | DISPLAY_NPC_NOT_WORKING '=' NAME
      {
          const char16_t* const name = grammar::SemanticPointer<char16_t>($3);
          const int classId = g_ObjectDB.GetClassIdFromName(name, false);
          if (classId < 1) {
              // const std::wstring hostName =
              //     target_utf16::ToHostCodeUnits(name)
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"cannot find npc[%s]",
              //     hostName.c_str())
              // L2_LOG_ERROR(Log,
              //     L"cannot find npc[%s]",
              //     hostName.c_str())
              L2_LOG_ERROR(
                  Log,
                  u"Door/trap data line %lld: control tower inactive display NPC '%ls' was not found; class ID %d is "
                  u"still assigned.",
                  static_cast<long long>(m_lexer.line) + 1, name == nullptr ? L"<null>" : hostName.c_str(), classId
              );
          }
          m_pControlTower->m_nDisplayNPCID_NotWorking = classId;
      }
    | CONTROL_AREA
      { m_TempStrings.clear(); }
      '=' n30
      {
          for (const std::u16string& areaName : m_TempStrings) {
              CArea* const area = CAreaDB::inst().FindArea(areaName.c_str());
              if (area == nullptr) {
                  // const std::wstring hostName =
                  //     target_utf16::ToHostCodeUnits(areaName.c_str())
                  const std::wstring hostName = Utf16ToWide(areaName.c_str());
                  // Log.Add(
                  //     LOG_NORMAL,
                  //     L"cannot find field [%s]",
                  //     hostName.c_str())
                  // L2_LOG_INFO(Log,
                  //     L"cannot find field [%s]",
                  //     hostName.c_str())
                  L2_LOG_ERROR(
                      Log,
                      u"Door/trap data line %lld: control area '%ls' was not found; it is omitted from the control "
                      u"tower's area list.",
                      static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
                  );
              } else {
                  m_pControlTower->m_lstControlArea.push_back(area);
              }
          }
      }
    | HP '=' INTEGER
      {
          m_pControlTower->m_data->m_dMaxHP = static_cast<double>(grammar::SemanticInt32($3));
      }
    | PHYSICAL_DEFENCE '=' INTEGER
      {
          m_pControlTower->m_data->m_dPDefend = static_cast<double>(grammar::SemanticInt32($3));
      }
    | MAGICAL_DEFENCE '=' INTEGER
      {
          m_pControlTower->m_data->m_dMDefend = static_cast<double>(grammar::SemanticInt32($3));
      }
    ;

pos_def
    : POS '=' n29
      {
          const auto registerEditorId = [&](CStaticObject* const object) {
              const int editorId = grammar::SemanticInt32($3);
              object->m_data->m_nEditorID = editorId;
              if (m_bFirstStep) {
                  return;
              }
              if (m_EditIdSet.count(editorId) != 0) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Duplicated Edit Id [%d] in line [%d]",
                  //     editorId,
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"Duplicated Edit Id [%d] in line [%d]",
                  //     editorId,
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      u"Door/trap data line %lld: duplicate editor ID %d; the ID remains assigned to this static object and "
                      u"parsing continues.",
                      static_cast<long long>(m_lexer.line) + 1, editorId
                  );
                  return;
              }
              m_EditIdSet.insert(editorId);
          };
          registerEditorId(m_pSignBoard);
      }
    | EDITOR_ID '=' INTEGER
      { m_pSignBoard->SetPos(m_TempVector); }
    | HTML '=' NAME
      {
          m_pSignBoard->m_szHTML = grammar::SemanticPointer<char16_t>($3);
          m_pSignBoard->m_eAct = SBACT_HTML;
      }
    | DISPLAY_NPC '=' NAME
      {
          const char16_t* const name = grammar::SemanticPointer<char16_t>($3);
          const int classId = g_ObjectDB.GetClassIdFromName(name, false);
          if (classId < 1) {
              // const std::wstring hostName =
              //     target_utf16::ToHostCodeUnits(name)
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"cannot find npc[%s]",
              //     hostName.c_str())
              // L2_LOG_ERROR(Log,
              //     L"cannot find npc[%s]",
              //     hostName.c_str())
              L2_LOG_ERROR(
                  Log,
                  u"Door/trap data line %lld: signboard display NPC '%ls' was not found; class ID %d is still assigned.",
                  static_cast<long long>(m_lexer.line) + 1, name == nullptr ? L"<null>" : hostName.c_str(), classId
              );
          }
          m_pSignBoard->m_nDisplayNPCID = classId;
      }
    | TEXTURE_NAME '=' NAME
      {
          m_pSignBoard->m_szTextureName = grammar::SemanticPointer<char16_t>($3);
          m_pSignBoard->m_eAct = SBACT_TOWNMAP;
      }
    | MAP_POS '=' '{' INTEGER ';' INTEGER '}'
      {
          m_pSignBoard->m_nMapX = grammar::SemanticInt32($4);
          m_pSignBoard->m_nMapY = grammar::SemanticInt32($6);
      }
    | OBSERVER_GROUP '=' INTEGER
      { m_pSignBoard->m_vObserverGroup.push_back(grammar::SemanticInt32($3)); }
    | OBSERVER_POS '=' '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          ObserverPos observer{};
          observer.nNameID = grammar::SemanticInt32($4);
          observer.x = grammar::SemanticInt32($6);
          observer.y = grammar::SemanticInt32($8);
          observer.z = grammar::SemanticInt32($10);
          observer.yaw = grammar::SemanticInt32($12);
          observer.pitch = grammar::SemanticInt32($14);
          observer.price = static_cast<std::int64_t>(grammar::SemanticInt32($16));
          observer.nResidenceId = grammar::SemanticInt32($18);
          observer.nGroup = 0;
          m_pSignBoard->m_eAct = SBACT_OBSERVER;
          m_pSignBoard->AddObserverPos(observer);
      }
    ;

trap_area_def
    : TRAP_AREA '=' '{' n22 '}'
    ;

n22
    : n22 ';' n23
      { m_pTempTerritory->AddTerritoryPoint(grammar::SemanticPointer<FVector>($3)); }
    | n23
      { m_pTempTerritory->AddTerritoryPoint(grammar::SemanticPointer<FVector>($1)); }
    ;

n23
    : '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          const std::int64_t bottom = grammar::SemanticInt64($6);
          const std::int64_t top = grammar::SemanticInt64($8);
          if (top <= bottom) {
              // const std::wstring name =
              //     target_utf16::ToHostCodeUnits(m_pTempTerritory->GetName())
              const std::wstring name = Utf16ToWide(m_pTempTerritory->GetName());
              // Log.Add(
              //     LOG_ERROR,
              //     L"territory [%s] z1<z2",
              //     name.c_str())
              // L2_LOG_ERROR(Log,
              //     L"territory [%s] z1<z2",
              //     name.c_str())
              L2_LOG_ERROR(
                  Log,
                  u"Door/trap data line %lld: territory '%ls' has invalid Z bounds (bottom %lld, top %lld; expected "
                  u"bottom < top); range and point processing continues.",
                  static_cast<long long>(m_lexer.line) + 1, name.c_str(), static_cast<long long>(bottom),
                  static_cast<long long>(top)
              );
          }
          if (bottom < m_pTempTerritory->GetBottom()) {
              m_pTempTerritory->SetBottom(static_cast<int>(bottom));
          }
          if (top > m_pTempTerritory->GetTop()) {
              m_pTempTerritory->SetTop(static_cast<int>(top));
          }
          auto* const point = new FVector(grammar::SemanticInt32($2), grammar::SemanticInt32($4), 0);
          $$ = grammar::PointerSemantic(point);
      }
    ;

pos_def_2
    : POS '=' n29
      {
          const auto registerEditorId = [&](CStaticObject* const object) {
              const int editorId = grammar::SemanticInt32($3);
              object->m_data->m_nEditorID = editorId;
              if (m_bFirstStep) {
                  return;
              }
              if (m_EditIdSet.count(editorId) != 0) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Duplicated Edit Id [%d] in line [%d]",
                  //     editorId,
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"Duplicated Edit Id [%d] in line [%d]",
                  //     editorId,
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      u"Door/trap data line %lld: duplicate editor ID %d; the ID remains assigned to this static object and "
                      u"parsing continues.",
                      static_cast<long long>(m_lexer.line) + 1, editorId
                  );
                  return;
              }
              m_EditIdSet.insert(editorId);
          };
          registerEditorId(m_pChair);
      }
    | EDITOR_ID '=' INTEGER
      { m_pChair->SetPos(m_TempVector); }
    | CHAIR_GROUP_ID '=' INTEGER
      {
          if (grammar::SemanticInt64($3) > 0) {
              m_pChair->m_nGroupId = grammar::SemanticInt32($3);
          } else {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Chair group id must greater than 0!!!! line[%d]",
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Chair group id must greater than 0!!!! line[%d]",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log, u"Door/trap data line %lld: chair group ID %lld must be positive; the group ID is not changed.",
                  static_cast<long long>(m_lexer.line) + 1, static_cast<long long>(grammar::SemanticInt64($3))
              );
          }
      }
    | SIT_RANGE '=' INTEGER
      {
          if (grammar::SemanticInt64($3) > 0) {
              m_pChair->m_dSitRange = static_cast<double>(grammar::SemanticInt64($3));
          }
      }
    ;

pos_def_3
    : POS '=' n29
      { m_pDoor->m_HingePos = m_TempVector; }
    | RANGE '=' '{' n29
      { m_pDoor->m_Range[0] = m_TempVector; }
      ';' n29
      { m_pDoor->m_Range[1] = m_TempVector; }
      ';' n29
      { m_pDoor->m_Range[2] = m_TempVector; }
      ';' n29 '}'
      { m_pDoor->m_Range[3] = m_TempVector; }
    | HEIGHT '=' INTEGER
      { m_pDoor->m_Height = grammar::SemanticInt32($3); }
    | HP '=' INTEGER
      {
          const int value = grammar::SemanticInt32($3);
          m_pDoor->m_dBaseHp = static_cast<double>(value);
          m_pDoor->m_data->m_dMaxHP = static_cast<double>(value);
          m_pDoor->SetHP(static_cast<double>(value));
      }
    | PHYSICAL_DEFENCE '=' INTEGER
      { m_pDoor->m_data->m_dPDefend = static_cast<double>(grammar::SemanticInt32($3)); }
    | MAGICAL_DEFENCE '=' INTEGER
      { m_pDoor->m_data->m_dMDefend = static_cast<double>(grammar::SemanticInt32($3)); }
    | TYPE '=' NORMAL_TYPE_GROUP
      { m_pDoor->m_eType = static_cast<DoorTypeEnum>(grammar::SemanticInt32($3)); }
    | PARENT '=' NAME
      {
          if (!m_bFirstStep) {
              const char16_t* const parentName = grammar::SemanticPointer<char16_t>($3);
              CDoor* const parent = m_pDoorDB->GetDoor(parentName, 0);
              if (parent != nullptr) {
                  parent->m_vChilds.push_back(m_pDoor);
                  m_pDoor->m_pParent = parent;
              } else {
                  // const std::wstring doorName =
                  //     target_utf16::ToHostCodeUnits(m_pDoor->m_sDoorName)
                  const std::wstring doorName = Utf16ToWide(m_pDoor->m_sDoorName);
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"door[%s] couldn't find parent[%s]",
                  //     doorName.c_str(),
                  //     hostParent.c_str())
                  // L2_LOG_ERROR(Log,
                  //     L"door[%s] couldn't find parent[%s]",
                  //     doorName.c_str(),
                  //     hostParent.c_str())
                  L2_LOG_ERROR(
                      Log,
                      u"Door/trap data line %lld: door '%ls' references missing parent door '%ls'; the parent link is not "
                      u"assigned.",
                      static_cast<long long>(m_lexer.line) + 1, doorName.c_str(),
                      parentName == nullptr ? u"<null>" : parentName
                  );
              }
          }
      }
    | IS_OPEN '=' INTEGER
      {
          const bool open = grammar::SemanticInt64($3) != 0;
          m_pDoor->m_data->m_bIsOpen = open;
          m_pDoor->m_bDefaultOpened = open;
      }
    | MASTER_NAME '=' NAME
      {
          const char16_t* const masterName = grammar::SemanticPointer<char16_t>($3);
          if (EqualsAsciiIgnoreCase(masterName, u"sunrise")) {
              m_pDoorDB->m_vSunriseDoors.push_back(m_pDoor);
              break;
          }
          if (!m_bFirstStep) {
              CDoor* const master = m_pDoorDB->GetDoor(masterName, 0);
              if (master != nullptr) {
                  master->m_vSlaves.push_back(m_pDoor);
                  m_pDoor->m_pMaster = master;
              } else {
                  // const std::wstring doorName =
                  //     target_utf16::ToHostCodeUnits(m_pDoor->m_sDoorName)
                  const std::wstring doorName = Utf16ToWide(m_pDoor->m_sDoorName);
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"door[%s] couldn't find master [%s]",
                  //     doorName.c_str(),
                  //     hostMaster.c_str())
                  // L2_LOG_ERROR(Log,
                  //     L"door[%s] couldn't find master [%s]",
                  //     doorName.c_str(),
                  //     hostMaster.c_str())
                  L2_LOG_ERROR(
                      Log,
                      u"Door/trap data line %lld: door '%ls' references missing master door '%ls'; the master link is not "
                      u"assigned.",
                      static_cast<long long>(m_lexer.line) + 1, doorName.c_str(),
                      masterName == nullptr ? u"<null>" : masterName
                  );
              }
          }
      }
    | MASTER_OPEN_EVENT '=' ACT_VALUE
      {
          m_pDoor->m_eMasterOpenEvent = static_cast<DoorActionTypeEnum>(grammar::SemanticInt32($3));
      }
    | MASTER_CLOSE_EVENT '=' ACT_VALUE
      {
          m_pDoor->m_eMasterCloseEvent = static_cast<DoorActionTypeEnum>(grammar::SemanticInt32($3));
      }
    | LEVEL '=' INTEGER
      {
          const std::int64_t level = grammar::SemanticInt64($3);
          if (level >= 0 && level < 8) {
              m_pDoor->m_data->m_cLevel = static_cast<std::uint8_t>(level);
          } else {
              // Log.Add(
              //     LOG_ERROR,
              //     L"doorlevel too high")
              // L2_LOG_ERROR(Log,
              //     L"doorlevel too high")
              L2_LOG_ERROR(
                  Log,
                  u"Door/trap data line %lld: door '%ls' has invalid level %lld (expected 0..7); the level is not "
                  u"changed.",
                  static_cast<long long>(m_lexer.line) + 1, m_Name.c_str(),
                  static_cast<long long>(level)
              );
          }
      }
    | OPEN_METHOD '=' BY_VALUE
      {
          m_pDoor->m_eDoorOpenMethod = static_cast<DoorOpenMethodEnum>(grammar::SemanticInt32($3));
      }
    | OPEN_BY_DOOR_NAME_SKILL
      { m_pDoor->m_bOpenByDoorNameSkill = true; }
    | STATIC_OBJECT_ID '=' INTEGER
    | INSTANTZONE_ID '=' INTEGER
      {
          const int typeId = grammar::SemanticInt32($3);
          m_pDoor->m_InstantZoneKey = InstantZoneKey{typeId, 0};
          m_bIsInstantZone = typeId > 0;
      }
    | INSTANTZONE_ID '=' INTEGER ';' INTEGER
      {
          const int typeId = grammar::SemanticInt32($3);
          const int clusterId = grammar::SemanticInt32($5);
          m_pDoor->m_InstantZoneKey = InstantZoneKey{typeId, clusterId};
          m_bIsInstantZone = typeId > 0;
      }
    | EDITOR_ID '=' INTEGER
      {
          const int editorId = grammar::SemanticInt32($3);
          m_pDoor->m_data->m_nEditorID = editorId;
          if (m_bFirstStep || m_EditIdSet.count(editorId) != 0) {
              break;
          }
          m_EditIdSet.insert(editorId);
      }
    | CLOSE_TIME '=' INTEGER
      {
          m_pDoor->m_nCloseTime = runtime::MultiplyInt32Wrapping(grammar::SemanticInt32($3), 1000U);
      }
    | OPEN_TIME '=' INTEGER
      {
          m_pDoor->m_nOpenTime = runtime::MultiplyInt32Wrapping(grammar::SemanticInt32($3), 1000U);
      }
    | RANDOM_TIME '=' INTEGER
      {
          m_pDoor->m_nRandomTime = runtime::MultiplyInt32Wrapping(grammar::SemanticInt32($3), 1000U);
      }
    | DEFAULT_STATUS '=' IDENTIFIER
      {
          const char16_t* const status = grammar::SemanticPointer<char16_t>($3);
          if (EqualsAsciiIgnoreCase(status, u"open")) {
              m_pDoor->m_data->m_bIsOpen = true;
          } else if (EqualsAsciiIgnoreCase(status, u"close")) {
              m_pDoor->m_data->m_bIsOpen = false;
          } else {
              // const std::wstring hostStatus =
              //     target_utf16::ToHostCodeUnits(status)
              const std::wstring hostStatus = Utf16ToWide(status);
              // Log.Add(
              //     LOG_ERROR,
              //     L"syntax error at default status = [%s]",
              //     hostStatus.c_str())
              // L2_LOG_ERROR(Log,
              //     L"syntax error at default status = [%s]",
              //     hostStatus.c_str())
              L2_LOG_ERROR(
                  Log,
                  u"Door/trap data line %lld: door '%ls' has unknown status '%ls' (expected open or close); the open "
                  u"state is not changed.",
                  static_cast<long long>(m_lexer.line) + 1, m_Name.c_str(),
                  status == nullptr ? L"<null>" : hostStatus.c_str()
              );
          }
      }
    | HP_SHOWABLE '=' INTEGER
      { m_pDoor->m_nHpShowable = grammar::SemanticInt32($3); }
    | INACTIVE_TIME '=' INTEGER
      {
          m_pDoor->m_nInactiveTime = runtime::MultiplyInt32Wrapping(grammar::SemanticInt32($3), 1000U);
      }
    | EMITTER_ID '=' INTEGER
      { m_nEmitterID = grammar::SemanticInt32($3); }
    | TARGETABLE '=' INTEGER
      { m_pDoor->SetOrgTargetable(grammar::SemanticInt32($3) == 1); }
    | STEALTH '=' INTEGER
      { m_pDoor->m_data->m_nStealth = grammar::SemanticInt32($3); }
    | CHECK_COLLISION '=' INTEGER
      {
          m_nCollision = grammar::SemanticInt32($3);
          m_pDoor->m_isCheckCollisiion = grammar::SemanticInt64($3) != 0;
      }
    | NEED_TO_LOG '=' INTEGER
      { m_pDoor->m_bNeedToLog = grammar::SemanticInt32($3) == 1; }
    ;

n29
    : '{' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          m_TempVector =
              FVector(grammar::SemanticInt32($2), grammar::SemanticInt32($4), grammar::SemanticInt32($6));
      }
    ;

n30
    : '{' n31 '}'
    | '{' '}'
    ;

n31
    : n31 ';' NAME
      {
          const char16_t* const text = grammar::SemanticPointer<char16_t>($3);
          m_TempStrings.push_back(std::u16string(text != nullptr ? text : u""));
      }
    | NAME
      {
          const char16_t* const text = grammar::SemanticPointer<char16_t>($1);
          m_TempStrings.push_back(std::u16string(text != nullptr ? text : u""));
      }
    ;

%%
