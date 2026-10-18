// CParserForInstantZone grammar (original L2Server/parser_instantzone.y).
// Bison generates ParserInstantZone.cpp and ParserInstantZoneTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserInstantZone.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/Door.h"
#include "L2Server/src/ObjectDB.h"

#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>
#include <utility>

#include "ParserInstantZoneTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForInstantZone::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserInstantZoneTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

char16_t FoldAscii(const char16_t value) {
    return value >= u'A' && value <= u'Z' ? static_cast<char16_t>(value + (u'a' - u'A')) : value;
}

bool EqualsAsciiInsensitive(const char16_t* value, const std::u16string_view expected) {
    if (value == nullptr) {
        return false;
    }
    std::size_t index = 0;
    while (value[index] != u'\0' && index < expected.size()) {
        if (FoldAscii(value[index]) != FoldAscii(expected[index])) {
            return false;
        }
        ++index;
    }
    return index == expected.size() && value[index] == u'\0';
}

void LogInvalidYesNo(const char16_t* value, const int line) {
    const std::wstring word = target_utf16::ToHostCodeUnits(value);
    // Log.Add(
    //     LOG_ERROR,
    //     L"Undefined word[%s] in line [%d]",
    //     word.c_str(),
    //     line)
    // Previous: L2_LOG_ERROR(Log,
    //     L"Undefined word[%s] in line [%d]", word.c_str(), line)
    L2_LOG_ERROR(
        Log,
        u"[instant-zone parser] Boolean value '%s' is invalid at line %d; "
        u"expected yes or no.",
        word.c_str(), line
    );
}

CInstantZoneEntranceCondition_check_exclusive_point_inzone::IterationDay ParseIterationDay(
    const char16_t* value, bool& valid
) {
    using Condition = CInstantZoneEntranceCondition_check_exclusive_point_inzone;
    valid = true;
    if (EqualsAsciiInsensitive(value, u"sun")) {
        return Condition::Sun;
    }
    if (EqualsAsciiInsensitive(value, u"mon")) {
        return Condition::Mon;
    }
    if (EqualsAsciiInsensitive(value, u"tue")) {
        return Condition::Tue;
    }
    if (EqualsAsciiInsensitive(value, u"wed")) {
        return Condition::Wed;
    }
    if (EqualsAsciiInsensitive(value, u"thu")) {
        return Condition::Thu;
    }
    if (EqualsAsciiInsensitive(value, u"fri")) {
        return Condition::Fri;
    }
    if (EqualsAsciiInsensitive(value, u"sat")) {
        return Condition::Sat;
    }
    valid = false;
    return Condition::Wed;
}

} // namespace

CParserForInstantZone::CParserForInstantZone() :
    m_pInstantZone(nullptr),
    m_pInstantZoneConst(nullptr),
    m_pInstantZoneName(nullptr),
    m_Vector{},
    m_bAppendingVector(false),
    m_padding121{},
    m_pVectorList(nullptr),
    m_pIntegerList(nullptr),
    m_ItemList(),
    m_pCluster(nullptr),
    m_pGroup(nullptr),
    m_nExtraEntrance(0),
    m_padding16c{},
    m_lexer(),
    m_nInstantZoneCount(0),
    m_nMaximumZoneCount(0) {
}

CParserForInstantZone::~CParserForInstantZone() = default;

int CParserForInstantZone::Create(char16_t* fileBuffer, const int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}

void CParserForInstantZone::yysyntaxerror() {
    char16_t message[60]{};
    FormatUtf16To(message, std::size(message), u"InstantZoneDataChecker[%d] : syntax error", m_lexer.line + 1);
    // Log.Add(LOG_ERROR, message)
    // Log.Add(LOG_ERROR, L"%ls", message)
    L2_LOG_ERROR(Log, u"%ls", message);
    // Noise: The error is already logged above. Only Windows needs a modal copy.
    // PopUpWindow(message)
#if defined(_WIN32)
    PopUpWindow(L"%ls", message);
#endif
}

void CParserForInstantZone::AddInstantZone() {
    if (m_pInstantZone == nullptr || m_pInstantZoneConst == nullptr) {
        return;
    }

    const wchar_t* error = nullptr;
    if (m_pInstantZoneConst->m_eOrderEndPosType == EPT_DESIGNATORY && m_pInstantZoneConst->m_vOrderEndPos.empty()) {
        error = L"InstantZoneDataChecker[%d] : must have order_end_pos";
    } else if (m_pInstantZoneConst->m_eDisorderEndPosType == EPT_DESIGNATORY &&
        m_pInstantZoneConst->m_vDisorderEndPos.empty()) {
        error = L"InstantZoneDataChecker[%d] : must have disorder_end_pos";
    }

    if (error != nullptr) {
        wchar_t message[56]{};
        std::swprintf(message, std::size(message), error, m_lexer.line + 1);
        // Log.Add(LOG_ERROR, message)
        // Log.Add(LOG_ERROR, L"%ls", message)
        L2_LOG_ERROR(Log, u"%ls", message);
        return;
    }

    m_pInstantZone->m_const = m_pInstantZoneConst;
    const std::wstring name = target_utf16::ToHostCodeUnits(m_pInstantZoneName);
    g_InstantZoneDB.AddZone(m_pInstantZone, name.c_str());
    if (m_pCluster != nullptr) {
        g_InstantZoneDB.AddCluster(m_pCluster);
    }
    m_pInstantZone = nullptr;
    m_pInstantZoneConst = nullptr;
    m_pCluster = nullptr;
}
}

%token INSTANTZONE_BEGIN
%token INSTANTZONE_END
%token OLYMPIADZONE_BEGIN
%token OLYMPIADZONE_END
%token PLEDGEGAMEZONE_BEGIN
%token PLEDGEGAMEZONE_END
%token INSTANTAGITZONE_BEGIN
%token INSTANTAGITZONE_END
%token PLEDGERAIDZONE_BEGIN
%token PLEDGERAIDZONE_END
%token CLUSTER_ID
%token ID
%token NAME
%token MAX_CHANNEL
%token MAX_USER
%token MIN_USER
%token DURATION
%token TIME_LIMIT
%token ENTRANCE_COND
%token DELETE_NO_USER
%token REQUIRED_ITEM
%token DELETED_ITEM
%token REQUIRED_POINT
%token START_POS_TYPE
%token START_POS
%token ORDER_END_POS_TYPE
%token ORDER_END_POS
%token DISORDER_END_POS_TYPE
%token DISORDER_END_POS
%token DOOR_LIST
%token AREA_LIST
%token SPAWN_NPC
%token ASSOCIATIVE
%token TODOLIST_CHECK
%token TODAY_PRIORITY
%token CHECK_LEVEL
%token CHECK_QUEST
%token CHECK_PLEDGE
%token CHECK_PARTY_CHAIR
%token CHECK_EXCLUSIVE_POINT_INZONE
%token RESET_BINDING
%token START_BINDING
%token REMOVE_BUFF
%token MAX_PARTY
%token GROUP_BEGIN
%token GROUP_END
%token GROUP_ID
%token MAX_ENTRANCE
%token INSTANTZONE_LIST
%token EXTRA_ENTRANCE
%token DESIGNATORY_RANDOM
%token NOMINATION_ORIGIN_ORIGIN_RESTART
%token INTEGER
%token NAME_2
%token IDENTIFIER
%start n1

%%

n1
    : n2 group_list
    ;

n2
    : n2 n3
    | %empty
    ;

n3
    : instantzone
    | olympiadzone
    | pledgegamezone
    | instantagitzone
    | pledgeraidzone
    ;

instantzone
    : INSTANTZONE_BEGIN
      {
          const auto beginZone = [this](const InZoneType type, const bool applyDefaults) {
              m_pInstantZone = new CInstantZone;
              m_pInstantZoneConst = new CInstantZoneConst;
              if (m_pInstantZoneConst != nullptr) {
                  m_pInstantZoneConst->m_InZoneType = type;
                  if (applyDefaults) {
                      m_pInstantZoneConst->m_bIsAssociative = false;
                      m_pInstantZoneConst->m_eStartPosType = SPT_DESIGNATORY;
                      m_pInstantZoneConst->m_eOrderEndPosType = EPT_ORIGIN;
                      m_pInstantZoneConst->m_eDisorderEndPosType = EPT_ORIGIN;
                  }
              }
              ++m_nInstantZoneCount;
          };
          beginZone(IZT_NORMAL, true);
      }
      n14 INSTANTZONE_END
      { AddInstantZone(); }
    ;

olympiadzone
    : OLYMPIADZONE_BEGIN
      {
          const auto beginZone = [this](const InZoneType type, const bool applyDefaults) {
              m_pInstantZone = new CInstantZone;
              m_pInstantZoneConst = new CInstantZoneConst;
              if (m_pInstantZoneConst != nullptr) {
                  m_pInstantZoneConst->m_InZoneType = type;
                  if (applyDefaults) {
                      m_pInstantZoneConst->m_bIsAssociative = false;
                      m_pInstantZoneConst->m_eStartPosType = SPT_DESIGNATORY;
                      m_pInstantZoneConst->m_eOrderEndPosType = EPT_ORIGIN;
                      m_pInstantZoneConst->m_eDisorderEndPosType = EPT_ORIGIN;
                  }
              }
              ++m_nInstantZoneCount;
          };
          beginZone(IZT_OLYMPIAD, true);
      }
      n14 OLYMPIADZONE_END
      { AddInstantZone(); }
    ;

pledgegamezone
    : PLEDGEGAMEZONE_BEGIN
      {
          const auto beginZone = [this](const InZoneType type, const bool applyDefaults) {
              m_pInstantZone = new CInstantZone;
              m_pInstantZoneConst = new CInstantZoneConst;
              if (m_pInstantZoneConst != nullptr) {
                  m_pInstantZoneConst->m_InZoneType = type;
                  if (applyDefaults) {
                      m_pInstantZoneConst->m_bIsAssociative = false;
                      m_pInstantZoneConst->m_eStartPosType = SPT_DESIGNATORY;
                      m_pInstantZoneConst->m_eOrderEndPosType = EPT_ORIGIN;
                      m_pInstantZoneConst->m_eDisorderEndPosType = EPT_ORIGIN;
                  }
              }
              ++m_nInstantZoneCount;
          };
          beginZone(IZT_PLEDGEGAME, true);
      }
      n14 PLEDGEGAMEZONE_END
      { AddInstantZone(); }
    ;

instantagitzone
    : INSTANTAGITZONE_BEGIN
      {
          const auto beginZone = [this](const InZoneType type, const bool applyDefaults) {
              m_pInstantZone = new CInstantZone;
              m_pInstantZoneConst = new CInstantZoneConst;
              if (m_pInstantZoneConst != nullptr) {
                  m_pInstantZoneConst->m_InZoneType = type;
                  if (applyDefaults) {
                      m_pInstantZoneConst->m_bIsAssociative = false;
                      m_pInstantZoneConst->m_eStartPosType = SPT_DESIGNATORY;
                      m_pInstantZoneConst->m_eOrderEndPosType = EPT_ORIGIN;
                      m_pInstantZoneConst->m_eDisorderEndPosType = EPT_ORIGIN;
                  }
              }
              ++m_nInstantZoneCount;
          };
          beginZone(IZT_INSTANT_AGIT, true);
      }
      n14 INSTANTAGITZONE_END
      { AddInstantZone(); }
    ;

pledgeraidzone
    : PLEDGERAIDZONE_BEGIN
      {
          const auto beginZone = [this](const InZoneType type, const bool applyDefaults) {
              m_pInstantZone = new CInstantZone;
              m_pInstantZoneConst = new CInstantZoneConst;
              if (m_pInstantZoneConst != nullptr) {
                  m_pInstantZoneConst->m_InZoneType = type;
                  if (applyDefaults) {
                      m_pInstantZoneConst->m_bIsAssociative = false;
                      m_pInstantZoneConst->m_eStartPosType = SPT_DESIGNATORY;
                      m_pInstantZoneConst->m_eOrderEndPosType = EPT_ORIGIN;
                      m_pInstantZoneConst->m_eDisorderEndPosType = EPT_ORIGIN;
                  }
              }
              ++m_nInstantZoneCount;
          };
          beginZone(IZT_PLEDGE_RAID, false);
      }
      n14 PLEDGERAIDZONE_END
      { AddInstantZone(); }
    ;

n14
    : n14 n15
    | %empty
    ;

n15
    : id_def
    | NAME '=' IDENTIFIER
      { m_pInstantZoneName = grammar::SemanticPointer<char16_t>($3); }
    | MAX_CHANNEL '=' INTEGER
      {
          m_pInstantZone->MAX_ZONE_COUNT = grammar::SemanticInt32($3);
          m_nMaximumZoneCount += grammar::SemanticInt32($3);
      }
    | MAX_USER '=' INTEGER
      { m_pInstantZoneConst->m_nMaxUser = grammar::SemanticInt32($3); }
    | MIN_USER '=' INTEGER
      { m_pInstantZoneConst->m_nMinUser = grammar::SemanticInt32($3); }
    | DURATION '=' INTEGER
      { m_pInstantZoneConst->m_nDuration = grammar::SemanticInt32($3) * 60; }
    | TIME_LIMIT '=' INTEGER
      { m_pInstantZoneConst->m_nTimeLimit = grammar::SemanticInt32($3) * 60; }
    | ENTRANCE_COND '=' '{' n22 '}'
    | DELETE_NO_USER '=' INTEGER
      { m_pInstantZoneConst->m_nDeleteNoUser = grammar::SemanticInt32($3) * 60; }
    | REQUIRED_ITEM
      { m_ItemList.clear(); }
      '=' '{' n26 '}'
      { m_pInstantZoneConst->m_RequiredItemList = m_ItemList; }
    | DELETED_ITEM
      { m_ItemList.clear(); }
      '=' '{' n26 '}'
      { m_pInstantZoneConst->m_DeletedItemList = m_ItemList; }
    | REQUIRED_POINT '=' INTEGER
      { m_pInstantZoneConst->m_pccafePoint = grammar::SemanticInt32($3); }
    | START_POS_TYPE '=' DESIGNATORY_RANDOM
      {
          m_pInstantZoneConst->m_eStartPosType = static_cast<StartPositionType>(grammar::SemanticInt32($3));
      }
    | START_POS
      {
          m_bAppendingVector = true;
          m_pVectorList = &m_pInstantZoneConst->m_vStartPos;
          m_pVectorList->clear();
      }
      '=' '{' n24 '}'
      { m_bAppendingVector = false; }
    | ORDER_END_POS_TYPE '=' NOMINATION_ORIGIN_ORIGIN_RESTART
      {
          m_pInstantZoneConst->m_eOrderEndPosType = static_cast<EndPositionType>(grammar::SemanticInt32($3));
      }
    | ORDER_END_POS
      {
          m_bAppendingVector = true;
          m_pVectorList = &m_pInstantZoneConst->m_vOrderEndPos;
          m_pVectorList->clear();
      }
      '=' '{' n24 '}'
      { m_bAppendingVector = false; }
    | DISORDER_END_POS_TYPE '=' NOMINATION_ORIGIN_ORIGIN_RESTART
      {
          m_pInstantZoneConst->m_eDisorderEndPosType = static_cast<EndPositionType>(grammar::SemanticInt32($3));
      }
    | DISORDER_END_POS
      {
          m_bAppendingVector = true;
          m_pVectorList = &m_pInstantZoneConst->m_vDisorderEndPos;
          m_pVectorList->clear();
      }
      '=' '{' n24 '}'
      { m_bAppendingVector = false; }
    | DOOR_LIST '=' '{' n28 '}'
      {
          auto* const names = grammar::SemanticPointer<std::vector<char16_t*>>($4);
          if (names == nullptr) {
              break;
          }
          for (char16_t* const name : *names) {
              const std::wstring hostName = target_utf16::ToHostCodeUnits(name);
              if (g_DoorDB.GetDoor(name, 0) == nullptr) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"invalid inzone door name[%s] in line [%d]",
                  //     hostName.c_str(),
                  //     m_lexer.line + 1)
                  // Previous: L2_LOG_ERROR(Log,
                  //     L"invalid inzone door name[%s] in line [%d]",
                  //     hostName.c_str(), m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      u"[instant-zone parser] Door '%s' is undefined at line "
                      u"%d; it is omitted from the zone.",
                      hostName.c_str(), m_lexer.line + 1
                  );
              } else {
                  m_pInstantZoneConst->m_vDoorNameList.push_back(name);
              }
          }
      }
    | AREA_LIST '=' '{' n29 '}'
      {
          auto* const names = grammar::SemanticPointer<std::vector<char16_t*>>($4);
          if (names != nullptr) {
              for (char16_t* const name : *names) {
                  m_pInstantZoneConst->m_vAreaNameList.push_back(name);
              }
          }
      }
    | SPAWN_NPC '=' IDENTIFIER
      {
          char16_t* const word = grammar::SemanticPointer<char16_t>($3);
          const bool yes = EqualsAsciiInsensitive(word, u"yes");
          m_pInstantZoneConst->m_bSpawnNpc = yes;
          if (!yes && !EqualsAsciiInsensitive(word, u"no")) {
              LogInvalidYesNo(word, m_lexer.line + 1);
          }
      }
    | ASSOCIATIVE '=' IDENTIFIER
      {
          char16_t* const word = grammar::SemanticPointer<char16_t>($3);
          const bool yes = EqualsAsciiInsensitive(word, u"yes");
          m_pInstantZoneConst->m_bIsAssociative = yes;
          if (!yes && !EqualsAsciiInsensitive(word, u"no")) {
              LogInvalidYesNo(word, m_lexer.line + 1);
          }
      }
    | TODOLIST_CHECK '=' IDENTIFIER
      {
          char16_t* const word = grammar::SemanticPointer<char16_t>($3);
          const bool yes = EqualsAsciiInsensitive(word, u"yes");
          m_pInstantZoneConst->m_bToDoListCheck = yes;
          if (!yes && !EqualsAsciiInsensitive(word, u"no")) {
              LogInvalidYesNo(word, m_lexer.line + 1);
          }
      }
    | TODAY_PRIORITY '=' INTEGER
      { m_pInstantZoneConst->m_ToDayPriority = grammar::SemanticInt32($3); }
    | START_BINDING '=' INTEGER
      { m_pInstantZoneConst->m_bindingTiming = grammar::SemanticInt32($3) * 60; }
    | RESET_BINDING '=' '{' INTEGER ':' INTEGER ';' '{' n38 '}' '}'
      {
          const auto applyResetDays = [this](const std::vector<int>* days, int& mask, const wchar_t* message) {
              mask = 0;
              if (days == nullptr) {
                  return;
              }
              for (const int day : *days) {
                  if (static_cast<unsigned int>(day) >= 7U) {
                      // Log.Add(
                      //     LOG_ERROR,
                      //     message,
                      //     day,
                      //     m_lexer.line + 1)
                      L2_LOG_ERROR(Log, message, day, m_lexer.line + 1);
                      return;
                  }
                  mask |= 1 << day;
              }
          };
          m_pInstantZoneConst->m_resetBindingTime = grammar::SemanticInt32($4) * 60 + grammar::SemanticInt32($6);
          const auto* const days = grammar::SemanticPointer<std::vector<int>>($9);
          applyResetDays(
              days, m_pInstantZoneConst->m_resetBindingDay,
              L"reset_binding day(%d) must be between 0 and 6 "
              L"in line [%d]"
          );
      }
    | REMOVE_BUFF '=' INTEGER
      { m_pInstantZoneConst->m_removeBuff = grammar::SemanticInt32($3); }
    | MAX_PARTY '=' INTEGER
      {
          m_pInstantZoneConst->m_MaxParty = static_cast<std::uint32_t>(grammar::SemanticInt32($3));
      }
    ;

id_def
    : ID '=' INTEGER
      {
          const int typeId = grammar::SemanticInt32($3);
          m_pInstantZone->m_nTypeId = typeId;
          m_pInstantZoneConst->m_nTypeId = typeId;
          if (typeId <= 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"id must be positive number. line[%d]",
              //     m_lexer.line + 1)
              // Previous: L2_LOG_ERROR(Log,
              //     L"id must be positive number. line[%d]", m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"[instant-zone parser] Type ID must be positive at line %d; "
                  u"the supplied value is retained.",
                  m_lexer.line + 1
              );
          }
      }
    | ID '=' INTEGER CLUSTER_ID '=' INTEGER
      {
          const int typeId = grammar::SemanticInt32($3);
          const int clusterId = grammar::SemanticInt32($6);
          m_pInstantZone->m_nTypeId = typeId;
          m_pInstantZoneConst->m_nTypeId = typeId;
          m_pCluster = g_InstantZoneDB.GetCluster(typeId);
          if (m_pCluster == nullptr) {
              m_pCluster = new CInstantZoneCluster(typeId);
          }
          if (m_pCluster != nullptr) {
              m_pCluster->AddZone(clusterId, m_pInstantZone);
          }
          m_pInstantZoneConst->m_clusterId = clusterId;
          m_pInstantZoneConst->m_instantZoneCluster = m_pCluster;
          if (typeId <= 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"id must be positive number. line[%d]",
              //     m_lexer.line + 1)
              // Previous: L2_LOG_ERROR(Log,
              //     L"id must be positive number. line[%d]", m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"[instant-zone parser] Type ID must be positive at line %d; "
                  u"the supplied value is retained.",
                  m_lexer.line + 1
              );
          } else if (clusterId <= 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"cluster_id must be positive number. line[%d]",
              //     m_lexer.line + 1)
              // Previous: L2_LOG_ERROR(Log,
              //     L"cluster_id must be positive number. line[%d]",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"[instant-zone parser] Cluster ID must be positive at line %d; "
                  u"the supplied value is retained.",
                  m_lexer.line + 1
              );
          }
      }
    ;

n22
    : n22 ';' '{' check_level_def '}'
    | '{' check_level_def '}'
    | %empty
    ;

check_level_def
    : CHECK_LEVEL '(' INTEGER ')' ';' INTEGER ';' INTEGER
      {
          const auto appendCondition = [this](CInstantZoneEntranceCondition* condition) {
              if (m_pInstantZoneConst != nullptr && condition != nullptr) {
                  m_pInstantZoneConst->m_vEntranceCond.push_back(condition);
              }
          };
          auto* const condition = new CInstantZoneEntranceCondition_check_level(grammar::SemanticInt32($3) == 1);
          condition->nMinLevel = grammar::SemanticInt32($6);
          condition->nMaxLevel = grammar::SemanticInt32($8);
          appendCondition(condition);
          m_pInstantZoneConst->m_LevelMin = condition->nMinLevel;
          m_pInstantZoneConst->m_LevelMax = condition->nMaxLevel;
      }
    | CHECK_QUEST '(' INTEGER ')' ';' INTEGER
      {
          const auto appendCondition = [this](CInstantZoneEntranceCondition* condition) {
              if (m_pInstantZoneConst != nullptr && condition != nullptr) {
                  m_pInstantZoneConst->m_vEntranceCond.push_back(condition);
              }
          };
          auto* const condition = new CInstantZoneEntranceCondition_check_quest(grammar::SemanticInt32($3) == 1);
          condition->nQuestID = grammar::SemanticInt32($6);
          appendCondition(condition);
      }
    | CHECK_PLEDGE
      {
          const auto appendCondition = [this](CInstantZoneEntranceCondition* condition) {
              if (m_pInstantZoneConst != nullptr && condition != nullptr) {
                  m_pInstantZoneConst->m_vEntranceCond.push_back(condition);
              }
          };
          appendCondition(new CInstantZoneEntranceCondition_check_pledge);
      }
    | CHECK_PARTY_CHAIR '(' INTEGER ')' ';' '{' n38 '}'
      {
          const auto appendCondition = [this](CInstantZoneEntranceCondition* condition) {
              if (m_pInstantZoneConst != nullptr && condition != nullptr) {
                  m_pInstantZoneConst->m_vEntranceCond.push_back(condition);
              }
          };
          auto* const condition = new CInstantZoneEntranceCondition_check_party_chair(grammar::SemanticInt32($3) == 1);
          auto* const groups = grammar::SemanticPointer<std::vector<int>>($7);
          if (groups != nullptr) {
              for (const int group : *groups) {
                  condition->m_vChairGroupList.push_back(group);
              }
          }
          appendCondition(condition);
      }
    | CHECK_EXCLUSIVE_POINT_INZONE '(' INTEGER ')' ';' '{' n28 ':' INTEGER ':' INTEGER '}' ';' '{' n38 '}'
      {
          const auto appendCondition = [this](CInstantZoneEntranceCondition* condition) {
              if (m_pInstantZoneConst != nullptr && condition != nullptr) {
                  m_pInstantZoneConst->m_vEntranceCond.push_back(condition);
              }
          };
          auto* const condition =
              new CInstantZoneEntranceCondition_check_exclusive_point_inzone(grammar::SemanticInt32($3) == 1);
          auto* const days = grammar::SemanticPointer<std::vector<char16_t*>>($7);
          bool valid = days != nullptr && days->size() == 1;
          if (valid) {
              condition->m_itDayOfWeek = ParseIterationDay(runtime::CheckedVectorAt((*days), 0), valid);
          }
          if (!valid) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"invalid day value, one day need "
              //     L"[sun, mon, tue, wed, thu, fri, sat] line[%d]",
              //     m_lexer.line + 1)
              // Previous: L2_LOG_ERROR(Log,
              //     L"invalid day value, one day need "
              //     L"[sun, mon, tue, wed, thu, fri, sat] line[%d]",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"[instant-zone parser] Iteration day at line %d must contain "
                  u"exactly one of sun, mon, tue, wed, thu, fri, or sat; the "
                  u"condition is omitted.",
                  m_lexer.line + 1
              );
              break;
          }
          condition->m_itHour = grammar::SemanticInt32($9);
          condition->m_itMin = grammar::SemanticInt32($11);
          auto* const exclusive = grammar::SemanticPointer<std::vector<int>>($15);
          if (exclusive != nullptr) {
              for (const int typeId : *exclusive) {
                  condition->m_vExclusiveList.push_back(typeId);
              }
          }
          appendCondition(condition);
      }
    ;

n24
    : n24 ';' '{' integer_def '}'
    | '{' integer_def '}'
    | %empty
    ;

integer_def
    : INTEGER ',' INTEGER ',' INTEGER
      {
          const FVector value{
              static_cast<double>(grammar::SemanticInt32($1)), static_cast<double>(grammar::SemanticInt32($3)),
              static_cast<double>(grammar::SemanticInt32($5))
          };
          if (m_bAppendingVector && m_pVectorList != nullptr) {
              m_pVectorList->push_back(value);
          } else {
              m_Vector = value;
          }
      }
    | %empty
      { m_Vector = FVector{}; }
    ;

n26
    : n26 ';' '{' name_2_def '}'
    | '{' name_2_def '}'
    | %empty
    ;

name_2_def
    : NAME_2 ';' INTEGER
      {
          char16_t* const name = grammar::SemanticPointer<char16_t>($1);
          const int classId = g_ObjectDB.GetClassIdFromName(name, false);
          if (classId == -1) {
              const std::wstring hostName = target_utf16::ToHostCodeUnits(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined word[%s] in line[%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              // Previous: L2_LOG_ERROR(Log,
              //     L"Undefined word[%s] in line[%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"[instant-zone parser] Item '%s' is undefined at line %d; "
                  u"this item entry is omitted.",
                  hostName.c_str(), m_lexer.line + 1
              );
              break;
          }
          m_ItemList.emplace_back(classId, grammar::SemanticInt32($3));
      }
    | NAME_2
      {
          char16_t* const name = grammar::SemanticPointer<char16_t>($1);
          const int classId = g_ObjectDB.GetClassIdFromName(name, false);
          if (classId == -1) {
              const std::wstring hostName = target_utf16::ToHostCodeUnits(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined word[%s] in line[%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              // Previous: L2_LOG_ERROR(Log,
              //     L"Undefined word[%s] in line[%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"[instant-zone parser] Item '%s' is undefined at line %d; "
                  u"this item entry is omitted.",
                  hostName.c_str(), m_lexer.line + 1
              );
              break;
          }
          m_ItemList.emplace_back(classId, -1);
      }
    ;

n28
    : n28 ';' IDENTIFIER
      {
          auto* const values = grammar::SemanticPointer<std::vector<char16_t*>>($1);
          values->push_back(grammar::SemanticPointer<char16_t>($3));
          $$ = grammar::PointerSemantic(values);
      }
    | IDENTIFIER
      {
          auto* const values = new std::vector<char16_t*>;
          values->push_back(grammar::SemanticPointer<char16_t>($1));
          $$ = grammar::PointerSemantic(values);
      }
    | %empty
      {
          $$ = grammar::PointerSemantic(new std::vector<char16_t*>);
      }
    ;

n29
    : n29 ';' NAME_2
      {
          auto* const values = grammar::SemanticPointer<std::vector<char16_t*>>($1);
          values->push_back(grammar::SemanticPointer<char16_t>($3));
          $$ = grammar::PointerSemantic(values);
      }
    | NAME_2
      {
          auto* const values = new std::vector<char16_t*>;
          values->push_back(grammar::SemanticPointer<char16_t>($1));
          $$ = grammar::PointerSemantic(values);
      }
    | %empty
      {
          $$ = grammar::PointerSemantic(new std::vector<char16_t*>);
      }
    ;

group_list
    : group_list group
    | %empty
    ;

group
    : GROUP_BEGIN
      { m_nExtraEntrance = 0; }
      GROUP_ID '=' INTEGER MAX_ENTRANCE '=' INTEGER extra_entrance_def_list INSTANTZONE_LIST '=' '{' n38 '}'
      {
          const int groupId = grammar::SemanticInt32($5);
          const int maxEntrance = grammar::SemanticInt32($8);
          m_pGroup = new CInstantZoneGroup(groupId, maxEntrance);
          if (m_pGroup != nullptr) {
              m_pGroup->m_extraEntrance = m_nExtraEntrance;
              auto* const typeIds = grammar::SemanticPointer<std::vector<int>>($13);
              if (typeIds != nullptr) {
                  for (const int typeId : *typeIds) {
                      m_pGroup->AddZone(typeId);
                  }
              }
          }
          ++m_nInstantZoneCount;
      }
      time_limit_def_list GROUP_END
      {
          g_InstantZoneDB.AddGroup(m_pGroup);
          m_pGroup = nullptr;
      }
    ;

extra_entrance_def_list
    : extra_entrance_def_list extra_entrance_def
    | %empty
    ;

extra_entrance_def
    : EXTRA_ENTRANCE '=' INTEGER
      { m_nExtraEntrance = grammar::SemanticInt32($3); }
    ;

time_limit_def_list
    : time_limit_def_list time_limit_def
    | %empty
    ;

time_limit_def
    : TIME_LIMIT '=' INTEGER
      { m_pGroup->m_timeLimit = grammar::SemanticInt32($3) * 60; }
    | RESET_BINDING '=' '{' INTEGER ':' INTEGER ';' '{' n38 '}' '}'
      {
          const auto applyResetDays = [this](const std::vector<int>* days, int& mask, const wchar_t* message) {
              mask = 0;
              if (days == nullptr) {
                  return;
              }
              for (const int day : *days) {
                  if (static_cast<unsigned int>(day) >= 7U) {
                      // Log.Add(
                      //     LOG_ERROR,
                      //     message,
                      //     day,
                      //     m_lexer.line + 1)
                      L2_LOG_ERROR(Log, message, day, m_lexer.line + 1);
                      return;
                  }
                  mask |= 1 << day;
              }
          };
          m_pGroup->m_resetBindingTime = grammar::SemanticInt32($4) * 60 + grammar::SemanticInt32($6);
          const auto* const days = grammar::SemanticPointer<std::vector<int>>($9);
          applyResetDays(
              days, m_pGroup->m_resetBindingDay,
              L"group reset_binding day(%d) must be between 0 and 6 "
              L"in line [%d]"
          );
      }
    ;

n38
    : n38 ';' INTEGER
      {
          auto* const values = grammar::SemanticPointer<std::vector<int>>($1);
          values->push_back(grammar::SemanticInt32($3));
          $$ = grammar::PointerSemantic(values);
      }
    | INTEGER
      {
          auto* const values = new std::vector<int>;
          values->push_back(grammar::SemanticInt32($1));
          $$ = grammar::PointerSemantic(values);
      }
    | %empty
      {
          $$ = grammar::PointerSemantic(new std::vector<int>);
      }
    ;

// The original tables have rule 95 that no parser state reduces.
// It is left out.

%%
