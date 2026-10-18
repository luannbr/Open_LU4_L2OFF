// npcdata.txt grammar (original L2Server/parser_npc.y).
// Bison generates ParserNPC.cpp and ParserNPCTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserNPC.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/Boss.h"
#include "L2Server/src/Collection.h"
#include "L2Server/src/Doppelganger.h"
#include "L2Server/src/Item.h"
#include "L2Server/src/Merchant.h"
#include "L2Server/src/NPC.h"
#include "L2Server/src/ObjectDB.h"
#include "L2Server/src/Pet.h"
#include "L2Server/src/TeleportManager.h"
#include "L2Server/src/Trap.h"
#include "L2Server/src/Zzoldagu.h"

#include "Shared/src/BaseStructure.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Utils.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>
#include <utility>

#include "ParserNPCTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForNPC::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserNPCTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

char16_t FoldAscii(char16_t value) {
    if (value >= u'A' && value <= u'Z') {
        return static_cast<char16_t>(value + (u'a' - u'A'));
    }
    return value;
}

bool EqualAsciiInsensitive(const char16_t* left, std::u16string_view right) {
    if (left == nullptr) {
        return right.empty();
    }
    std::size_t index = 0;
    while (left[index] != u'\0' && index < right.size()) {
        if (FoldAscii(left[index]) != FoldAscii(right[index])) {
            return false;
        }
        ++index;
    }
    return left[index] == u'\0' && index == right.size();
}

struct FieldWord {
    ObjectFieldType field;
    std::u16string_view word;
    std::int32_t value;
};

constexpr std::array<FieldWord, 103> kFieldWords{{
    {OBJECT_FIELD_RACE, u"human", 0},
    {OBJECT_FIELD_RACE, u"elf", 1},
    {OBJECT_FIELD_RACE, u"darkelf", 2},
    {OBJECT_FIELD_RACE, u"orc", 3},
    {OBJECT_FIELD_RACE, u"dwarf", 4},
    {OBJECT_FIELD_RACE, u"kamael", 5},
    {OBJECT_FIELD_RACE, u"ertheia", 6},
    {OBJECT_FIELD_RACE, u"humanoid", 7},
    {OBJECT_FIELD_RACE, u"animal", 8},
    {OBJECT_FIELD_RACE, u"monster", 9},
    {OBJECT_FIELD_RACE, u"creature", 10},
    {OBJECT_FIELD_RACE, u"undead", 11},
    {OBJECT_FIELD_RACE, u"elemental", 12},
    {OBJECT_FIELD_RACE, u"devil", 13},
    {OBJECT_FIELD_RACE, u"dragon", 14},
    {OBJECT_FIELD_RACE, u"angel", 15},
    {OBJECT_FIELD_RACE, u"construct", 16},
    {OBJECT_FIELD_RACE, u"beast", 17},
    {OBJECT_FIELD_RACE, u"plant", 18},
    {OBJECT_FIELD_RACE, u"divine", 19},
    {OBJECT_FIELD_RACE, u"demonic", 20},
    {OBJECT_FIELD_RACE, u"giant", 21},
    {OBJECT_FIELD_RACE, u"bug", 22},
    {OBJECT_FIELD_RACE, u"fairy", 23},
    {OBJECT_FIELD_RACE, u"siege_weapon", 24},
    {OBJECT_FIELD_RACE, u"mercenary", 25},
    {OBJECT_FIELD_RACE, u"castle_guard", 26},
    {OBJECT_FIELD_RACE, u"friend", 29},
    {OBJECT_FIELD_RACE, u"etc", 28},
    {OBJECT_FIELD_CLASS, u"warrior", 1},
    {OBJECT_FIELD_CLASS, u"knight", 4},
    {OBJECT_FIELD_CLASS, u"rogue", 7},
    {OBJECT_FIELD_CLASS, u"wizard", 11},
    {OBJECT_FIELD_CLASS, u"cleric", 15},
    {OBJECT_FIELD_CLASS, u"shaman", 58},
    {OBJECT_FIELD_CLASS, u"monk", 59},
    {OBJECT_FIELD_CLASS, u"collector", 60},
    {OBJECT_FIELD_CLASS, u"artisan", 56},
    {OBJECT_FIELD_CLASS, u"merchant", 61},
    {OBJECT_FIELD_CLASS, u"trainer", 62},
    {OBJECT_FIELD_CLASS, u"teleporter", 63},
    {OBJECT_FIELD_CLASS, u"warehouse_keeper", 64},
    {OBJECT_FIELD_CLASS, u"package_keeper", 65},
    {OBJECT_FIELD_CLASS, u"newbie_guide", 66},
    {OBJECT_FIELD_CLASS, u"guard", 67},
    {OBJECT_FIELD_CLASS, u"citizen", 68},
    {OBJECT_FIELD_CLASS, u"guild_master", 69},
    {OBJECT_FIELD_CLASS, u"guild_coach", 70},
    {OBJECT_FIELD_CLASS, u"summon", 71},
    {OBJECT_FIELD_CLASS, u"object", 72},
    {OBJECT_FIELD_CLASS, u"chamberlain", 73},
    {OBJECT_FIELD_CLASS, u"temptrainer", 74},
    {OBJECT_FIELD_CLASS, u"holything", 75},
    {OBJECT_FIELD_CLASS, u"ownthing", 137},
    {OBJECT_FIELD_CLASS, u"castle_gate", 76},
    {OBJECT_FIELD_CLASS, u"commander", 77},
    {OBJECT_FIELD_CLASS, u"house_master", 78},
    {OBJECT_FIELD_CLASS, u"blacksmith", 79},
    {OBJECT_FIELD_CLASS, u"pet", 80},
    {OBJECT_FIELD_CLASS, u"boss", 81},
    {OBJECT_FIELD_CLASS, u"fieldboss", 81},
    {OBJECT_FIELD_CLASS, u"zzoldagu", 82},
    {OBJECT_FIELD_CLASS, u"treasure", 83},
    {OBJECT_FIELD_CLASS, u"monrace", 84},
    {OBJECT_FIELD_CLASS, u"mrkeeper", 85},
    {OBJECT_FIELD_CLASS, u"xmastree", 86},
    {OBJECT_FIELD_CLASS, u"siege_unit", 87},
    {OBJECT_FIELD_CLASS, u"world_trap", 119},
    {OBJECT_FIELD_CLASS, u"pc_trap", 120},
    {OBJECT_FIELD_CLASS, u"doppelganger", 121},
    {OBJECT_FIELD_CLASS, u"siege_attacker", 122},
    {OBJECT_FIELD_CLASS, u"collection", 138},
    {OBJECT_FIELD_SEX, u"male", 0},
    {OBJECT_FIELD_SEX, u"female", 1},
    {OBJECT_FIELD_SEX, u"etc", 2},
    {OBJECT_FIELD_BASE_ATTACK_TYPE, u"none", 0},
    {OBJECT_FIELD_BASE_ATTACK_TYPE, u"sword", 1},
    {OBJECT_FIELD_BASE_ATTACK_TYPE, u"blunt", 4},
    {OBJECT_FIELD_BASE_ATTACK_TYPE, u"dagger", 8},
    {OBJECT_FIELD_BASE_ATTACK_TYPE, u"pole", 9},
    {OBJECT_FIELD_BASE_ATTACK_TYPE, u"fist", 10},
    {OBJECT_FIELD_BASE_ATTACK_TYPE, u"bow", 11},
    {OBJECT_FIELD_BASE_ATTACK_TYPE, u"etc", 12},
    {OBJECT_FIELD_BASE_ATTACK_TYPE, u"dual", 13},
    {OBJECT_FIELD_BASE_ATTACK_TYPE, u"dualfist", 14},
    {OBJECT_FIELD_BASE_ATTACK_TYPE, u"dualdagger", 20},
    {OBJECT_FIELD_BASE_ATTACK_TYPE, u"dualblunt", 23},
    // Current table contains the same key twice. The second value is
    // unreachable because lookup stops at the first match.
    {OBJECT_FIELD_BASE_ATTACK_TYPE, u"dualblunt", 22},
    {OBJECT_FIELD_BASE_ATTACK_TYPE, u"rapier", 16},
    {OBJECT_FIELD_BASE_ATTACK_TYPE, u"crossbow", 17},
    {OBJECT_FIELD_BASE_ATTACK_TYPE, u"ancientsword", 18},
    {OBJECT_FIELD_STATUS, u"normal", 1},
    {OBJECT_FIELD_STATUS, u"berserk", 2},
    {OBJECT_FIELD_STATUS, u"confuse", 4},
    {OBJECT_FIELD_STATUS, u"silence", 8},
    {OBJECT_FIELD_STATUS, u"drunken", 16},
    {OBJECT_FIELD_BASE_ATTRIBUTE_TYPE, u"none", -2},
    {OBJECT_FIELD_BASE_ATTRIBUTE_TYPE, u"fire", 0},
    {OBJECT_FIELD_BASE_ATTRIBUTE_TYPE, u"water", 1},
    {OBJECT_FIELD_BASE_ATTRIBUTE_TYPE, u"wind", 2},
    {OBJECT_FIELD_BASE_ATTRIBUTE_TYPE, u"earth", 3},
    {OBJECT_FIELD_BASE_ATTRIBUTE_TYPE, u"holy", 4},
    {OBJECT_FIELD_BASE_ATTRIBUTE_TYPE, u"unholy", 5},
}};

std::int32_t FindInTable(ObjectFieldType field, const char16_t* word) {
    for (const FieldWord& entry : kFieldWords) {
        if (entry.field == field && EqualAsciiInsensitive(word, entry.word)) {
            return entry.value;
        }
    }
    return -1;
}

struct CountryWord {
    std::u16string_view word;
    int value;
};

constexpr std::array<CountryWord, 14> kCountryWords{{
    {u"k", 0},
    {u"e", 1},
    {u"j", 2},
    {u"tw", 3},
    {u"cn", 4},
    {u"th", 5},
    {u"ph", 6},
    {u"id", 7},
    {u"ru", 8},
    {u"eu", 9},
    {u"de", 10},
    {u"fr", 11},
    {u"pl", 12},
    {u"tr", 13},
}};

int FindCountryCode(const char16_t* word) {
    for (const CountryWord& entry : kCountryWords) {
        if (EqualAsciiInsensitive(word, entry.word)) {
            return entry.value;
        }
    }
    return -1;
}

bool IsBaseNpcClass(const char16_t* className) {
    constexpr std::array<std::u16string_view, 33> names{{
        u"warrior",
        u"knight",
        u"rogue",
        u"wizard",
        u"cleric",
        u"shaman",
        u"monk",
        u"collector",
        u"artisan",
        u"trainer",
        u"warehouse_keeper",
        u"package_keeper",
        u"newbie_guide",
        u"guard",
        u"citizen",
        u"guild_master",
        u"guild_coach",
        u"object",
        u"chamberlain",
        u"temptrainer",
        u"holything",
        u"ownthing",
        u"castle_gate",
        u"commander",
        u"house_master",
        u"blacksmith",
        u"monrace",
        u"mrkeeper",
        u"xmastree",
        u"treasure",
        u"siege_unit",
        u"siege_attacker",
        u"",
    }};
    for (const std::u16string_view name : names) {
        if (!name.empty() && EqualAsciiInsensitive(className, name)) {
            return true;
        }
    }
    return false;
}

CNPC* CreateNpcPrototype(const char16_t* className, bool& collection) {
    if (IsBaseNpcClass(className)) {
        return new CNPC();
    }
    if (EqualAsciiInsensitive(className, u"merchant")) {
        return new CMerchant();
    }
    if (EqualAsciiInsensitive(className, u"teleporter")) {
        return new CTeleporter();
    }
    if (EqualAsciiInsensitive(className, u"summon")) {
        return new CSummon();
    }
    if (EqualAsciiInsensitive(className, u"pet")) {
        return new CPet();
    }
    if (EqualAsciiInsensitive(className, u"boss") || EqualAsciiInsensitive(className, u"fieldboss")) {
        auto* const boss = new CBoss();
        if (boss != nullptr && EqualAsciiInsensitive(className, u"fieldboss") && g_nCountryCode != 0) {
            boss->m_bIsFieldBoss = true;
        }
        return boss;
    }
    if (EqualAsciiInsensitive(className, u"zzoldagu")) {
        return new CZzoldagu();
    }
    if (EqualAsciiInsensitive(className, u"world_trap")) {
        return new CWorldTrap();
    }
    if (EqualAsciiInsensitive(className, u"pc_trap")) {
        return new CPCTrap();
    }
    if (EqualAsciiInsensitive(className, u"doppelganger")) {
        return new CDoppelganger();
    }
    if (EqualAsciiInsensitive(className, u"collection")) {
        collection = true;
        return new CCollection();
    }
    return nullptr;
}

std::vector<std::int64_t> ConvertStrList64(ObjectFieldType field, std::vector<char16_t*>& words) {
    std::vector<std::int64_t> result;
    for (char16_t* const word : words) {
        const std::int32_t value = FindInTable(field, word);
        if (value == -1) {
            // const std::wstring hostWord = target_utf16::ToHostCodeUnits(word)
            const std::wstring hostWord = Utf16ToWide(word);
            // Log.Add(LOG_ERROR, L"Undefiend word[%ls]", hostWord.c_str())
            // L2_LOG_ERROR(Log, L"Undefiend word[%ls]", hostWord.c_str())
            L2_LOG_ERROR(
                Log, u"NPC field %d contains unknown value '%ls'; the value is omitted from the list.",
                static_cast<int>(field), word == nullptr ? L"<null>" : hostWord.c_str()
            );
        } else {
            result.push_back(value);
        }
    }
    return result;
}

void DeleteStringList(std::vector<char16_t*>* words) {
    if (words == nullptr) {
        return;
    }
    for (char16_t* const word : *words) {
        std::free(word);
    }
    delete words;
}

void ValidateMultiDropProbabilities(const std::vector<ItemDropMultiStruct*>* values, int npcId) {
    if (values == nullptr) {
        return;
    }
    for (const ItemDropMultiStruct* const multi : *values) {
        double sum = 0.0;
        if (multi != nullptr && multi->pItemDropList != nullptr) {
            for (const ItemDropStruct* const item : *multi->pItemDropList) {
                if (item != nullptr) {
                    sum += item->DropProb;
                }
            }
        }
        if (runtime::Win64CvttDoubleToInt32(sum * 100.0 - 10000.0) != 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Error. drop multi prob sum is not 100.0 [%f], "
            //     L"npc id = %d \n\r",
            //     sum,
            //     npcId)
            // L2_LOG_ERROR(Log,
            //     L"Error. drop multi prob sum is not 100.0 [%f], "
            //     L"npc id = %d \n\r",
            //     sum,
            //     npcId)
            L2_LOG_ERROR(
                Log,
                u"NPC %d: a multi-drop group failed the 100%% probability-total check (sum %.17g); the group is retained.",
                npcId, sum
            );
        }
    }
}

std::u16string FormatInt64(std::int64_t value) {
    char16_t buffer[100]{};
    FormatUtf16To(buffer, std::size(buffer), u"%lld", static_cast<long long>(value));
    return std::u16string(buffer);
}

std::u16string FormatDouble(double value) {
    char16_t buffer[100]{};
    FormatUtf16To(buffer, std::size(buffer), u"%f", value);
    return std::u16string(buffer);
}

} // namespace

CParserForNPC::CParserForNPC() :
    m_lexer(),
    m_bOnlyPrecompiledHeader(false),
    m_bReload(false),
    m_bNewNpc(true),
    m_bCheckVitalityDropItem(false),
    m_pNpc(nullptr),
    m_mapParameter(),
    mCheckAI(false),
    mNpcName(),
    mAIName(),
    m_bIsCollection(false),
    m_bIsgMultiList(false) {
}

CParserForNPC::~CParserForNPC() = default;

int CParserForNPC::Create(CObjectDB* objectDB, char16_t* fileBuffer, int fileSize) {
    m_pObjDB = objectDB;
    m_lexer.m_bOnlyPrecompiledHeader = m_bOnlyPrecompiledHeader;
    return m_lexer.Create(this, fileBuffer, fileSize);
}
}

%token NPC_BEGIN
%token NPC_END
%token NAME
%token IDENTIFIER
%token NAME_ID_GROUP
%token INTEGER
%token FLOAT_NUMBER
%token CATEGORY
%token FLYING
%token MP_REWARD
%token EVENT_DROP
%token EX_DROP
%token BROADCAST_COND
%token G_ADDITIONAL_MAKE_MULTI_LIST
// The original grammar has these conflicts. Bison resolves them as the
// original tables did. A shift wins over a reduce.
%expect 1
%start n1

%%

n1
    : n1 n2
    | %empty
    ;

n2
    : npc
    ;

npc
    : NPC_BEGIN identifier_def
      {
          m_mapParameter.clear();
          m_bCheckVitalityDropItem = false;
          m_bIsgMultiList = false;
      }
      flying_def_list
      {
          if (m_pNpc == nullptr) {
              break;
          }
          if (m_bNewNpc) {
              m_pObjDB->RegisterObject(m_pNpc);
          }
          m_pNpc->m_pClassConst->m_mapParameter = m_mapParameter;
      }
      NPC_END
      {
          if (m_pNpc == nullptr || m_pNpc->m_pClassConst == nullptr) {
              break;
          }
          CNPCClassConst& classConst = *m_pNpc->m_pClassConst;
          const int classId = m_pNpc->m_data->m_nClassID;
          const char16_t* const name = m_pNpc->GetName();
          if (static_cast<int>(m_pNpc->m_data->m_moveType) == CMT_FLY) {
              if (classId != 0x8a && !classConst.m_ItemAdditionalMakeListMulti.empty()) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"flying npc[%ls][%d] can't have "
                  //     L"additional_make_multi_list",
                  //     name,
                  //     classId)
                  // L2_LOG_ERROR(Log,
                  //     L"flying npc[%ls][%d] can't have "
                  //     L"additional_make_multi_list",
                  //     name,
                  //     classId)
                  // L2_LOG_ERROR(Log,
                  //     L"npcdata.txt line %lld: flying NPC '%ls' (class ID %d) has forbidden additional_make_multi_list
                  //     entries, the list is retained.", static_cast<long long>(m_lexer.line) + 1, name == nullptr ?
                  //     L"<null>" : name, classId)
                  L2_LOG_ERROR(
                      Log,
                      u"npcdata.txt line %lld: flying NPC '%ls' (class ID %d) has forbidden additional_make_multi_list "
                      u"entries; the list is retained.",
                      static_cast<long long>(m_lexer.line) + 1, name == nullptr ? u"<null>" : name,
                      classId
                  );
              }
              if (!classConst.m_ExItemDropListMulti.empty()) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"flying npc[%ls][%d] can't have ex_item_drop_list",
                  //     name,
                  //     classId)
                  // L2_LOG_ERROR(Log,
                  //     L"flying npc[%ls][%d] can't have ex_item_drop_list",
                  //     name,
                  //     classId)
                  // L2_LOG_ERROR(Log,
                  //     L"npcdata.txt line %lld: flying NPC '%ls' (class ID %d) has forbidden ex_item_drop_list entries.
                  //     the list is retained.", static_cast<long long>(m_lexer.line) + 1, name == nullptr ? L"<null>" :
                  //     name, classId)
                  L2_LOG_ERROR(
                      Log,
                      u"npcdata.txt line %lld: flying NPC '%ls' (class ID %d) has forbidden ex_item_drop_list entries; "
                      u"the list is retained.",
                      static_cast<long long>(m_lexer.line) + 1, name == nullptr ? u"<null>" : name,
                      classId
                  );
              }
              if (!classConst.m_ItemVitalityDropListMulti.empty()) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"flying npc[%ls][%d] can't have "
                  //     L"vitality_item_drop_list",
                  //     name,
                  //     classId)
                  // L2_LOG_ERROR(Log,
                  //     L"flying npc[%ls][%d] can't have "
                  //     L"vitality_item_drop_list",
                  //     name,
                  //     classId)
                  // L2_LOG_ERROR(Log,
                  //     L"npcdata.txt line %lld: flying NPC '%ls' (class ID %d) has forbidden vitality_item_drop_list
                  //     entries, the list is retained.", static_cast<long long>(m_lexer.line) + 1, name == nullptr ?
                  //     L"<null>" : name, classId)
                  L2_LOG_ERROR(
                      Log,
                      u"npcdata.txt line %lld: flying NPC '%ls' (class ID %d) has forbidden vitality_item_drop_list "
                      u"entries; the list is retained.",
                      static_cast<long long>(m_lexer.line) + 1, name == nullptr ? u"<null>" : name,
                      classId
                  );
              }
              if (!classConst.m_ItemLuckyStatListMulti.empty()) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"flying npc[%ls][%d] can't have lucky_stat_item_list",
                  //     name,
                  //     classId)
                  // L2_LOG_ERROR(Log,
                  //     L"flying npc[%ls][%d] can't have lucky_stat_item_list",
                  //     name,
                  //     classId)
                  // L2_LOG_ERROR(Log,
                  //     L"npcdata.txt line %lld: flying NPC '%ls' (class ID %d) has forbidden lucky_stat_item_list
                  //     entries, the list is retained.", static_cast<long long>(m_lexer.line) + 1, name == nullptr ?
                  //     L"<null>" : name, classId)
                  L2_LOG_ERROR(
                      Log,
                      u"npcdata.txt line %lld: flying NPC '%ls' (class ID %d) has forbidden lucky_stat_item_list entries; "
                      u"the list is retained.",
                      static_cast<long long>(m_lexer.line) + 1, name == nullptr ? u"<null>" : name,
                      classId
                  );
              }
          }
          for (ItemDropMultiStruct& multi : classConst.m_ItemAdditionalMakeListMulti) {
              if (multi.pItemDropList == nullptr) {
                  continue;
              }
              for (ItemDropStruct* const drop : *multi.pItemDropList) {
                  if (drop == nullptr) {
                      continue;
                  }
                  CObject* const object = m_pObjDB->GetObjectW(drop->ItemClassId, false);
                  if (object != nullptr && object->IsItem()) {
                      CItem* const item = object->CastItem();
                      if (item != nullptr && item->m_const != nullptr && !item->m_const->m_bIsItemChanceAffected) {
                          // Literal current behavior: the generated action only
                          // clears this byte and never raises it here.
                          multi.m_bChanceAffected = false;
                          break;
                      }
                  }
              }
          }
          if (!m_bCheckVitalityDropItem) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"npc[%ls][%d] must have vitality_item_drop_list",
              //     name,
              //     classId)
              // L2_LOG_ERROR(Log,
              //     L"npc[%ls][%d] must have vitality_item_drop_list",
              //     name,
              //     classId)
              // L2_LOG_ERROR(Log,
              //     L"npcdata.txt line %lld: NPC '%ls' (class ID %d) has no vitality_item_drop_list declaration. Parsing
              //     continues.", static_cast<long long>(m_lexer.line) + 1, name == nullptr ? L"<null>" : name, classId)
              L2_LOG_ERROR(
                  Log,
                  u"npcdata.txt line %lld: NPC '%ls' (class ID %d) has no vitality_item_drop_list declaration; parsing "
                  u"continues.",
                  static_cast<long long>(m_lexer.line) + 1, name == nullptr ? u"<null>" : name,
                  classId
              );
          }
      }
    ;

identifier_def
    : IDENTIFIER INTEGER NAME
      {
          m_nNpcId = grammar::SemanticInt32($2);
          m_lexer.m_nNpcId = m_nNpcId;
          char16_t* const className = grammar::SemanticPointer<char16_t>($1);
          char16_t* const npcName = grammar::SemanticPointer<char16_t>($3);
          const std::size_t nameLength = npcName == nullptr ? 0 : std::char_traits<char16_t>::length(npcName);
          bool nameTooLong = false;
          if (nameLength > 24) {
              // const std::wstring hostName = target_utf16::ToHostCodeUnits(npcName)
              const std::wstring hostName = Utf16ToWide(npcName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"[PARSER_NPC] npc name is too long [%ls]",
              //     hostName.c_str())
              // L2_LOG_ERROR(Log,
              //     L"[PARSER_NPC] npc name is too long [%ls]",
              //     hostName.c_str())
              L2_LOG_ERROR(
                  Log,
                  u"npcdata.txt line %lld, NPC %d: name '%ls' is %llu UTF-16 code units long (maximum 24); a parser error "
                  u"is flagged.",
                  static_cast<long long>(m_lexer.line) + 1, m_nNpcId, hostName.c_str(),
                  static_cast<unsigned long long>(nameLength)
              );
              // yythrowerror(1)
              nameTooLong = true;
          }
          m_pNpc = nullptr;
          m_bNewNpc = true;
          if (m_bReload) {
              m_pNpc = static_cast<CNPC*>(m_pObjDB->GetObjectW(m_nNpcId + 1000000, false));
              if (m_pNpc != nullptr) {
                  m_bNewNpc = false;
              }
          }
          if (m_pNpc == nullptr) {
              m_pNpc = CreateNpcPrototype(className, m_bIsCollection);
              if (m_pNpc == nullptr) {
                  // const std::wstring hostClass = target_utf16::ToHostCodeUnits(className)
                  const std::wstring hostClass = Utf16ToWide(className);
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"[PARSER_NPC] class_define: undefined [%ls], line[%d]",
                  //     hostClass.c_str(),
                  //     m_lexer.line)
                  // L2_LOG_ERROR(Log,
                  //     L"[PARSER_NPC] class_define: undefined [%ls], line[%d]",
                  //     hostClass.c_str(),
                  //     m_lexer.line)
                  L2_LOG_ERROR(
                      Log,
                      u"npcdata.txt line %lld, NPC %d: cannot create a prototype for unknown class '%ls'; this record has "
                      u"no NPC object.",
                      static_cast<long long>(m_lexer.line) + 1, m_nNpcId,
                      className == nullptr ? L"<null>" : hostClass.c_str()
                  );
              }
          }
          if (m_pNpc != nullptr) {
              const std::int32_t classValue = FindInTable(OBJECT_FIELD_CLASS, className);
              if (classValue == -1) {
                  // const std::wstring hostClass = target_utf16::ToHostCodeUnits(className)
                  const std::wstring hostClass = Utf16ToWide(className);
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined class [%ls]",
                  //     hostClass.c_str())
                  // L2_LOG_ERROR(Log,
                  //     L"Undefined class [%ls]",
                  //     hostClass.c_str())
                  L2_LOG_ERROR(
                      Log, u"npcdata.txt line %lld, NPC %d: unknown class '%ls'; the class field is not assigned.",
                      static_cast<long long>(m_lexer.line) + 1, m_nNpcId,
                      className == nullptr ? L"<null>" : hostClass.c_str()
                  );
              } else {
                  m_pNpc->Set(OBJECT_FIELD_CLASS, static_cast<std::int64_t>(classValue));
                  if (!m_pNpc->IsSetResult()) {
                      // Log.Add(
                      //     LOG_ERROR,
                      //     L"[PARSER_NPC] set fail : line[%d], "
                      //     L"OBJECT_FIELD_CLASS",
                      //     m_lexer.line)
                      // L2_LOG_ERROR(Log,
                      //     L"[PARSER_NPC] set fail : line[%d], "
                      //     L"OBJECT_FIELD_CLASS",
                      //     m_lexer.line)
                      L2_LOG_ERROR(
                          Log, u"npcdata.txt line %lld, NPC %d: assigning class value %d failed; parsing continues.",
                          static_cast<long long>(m_lexer.line) + 1, m_nNpcId, classValue
                      );
                      m_pNpc->SetResult(true);
                  }
              }

              m_pNpc->Set(OBJECT_FIELD_NPC_ID, static_cast<std::int64_t>(m_nNpcId));
              if (!m_pNpc->IsSetResult()) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"[PARSER_NPC] set fail : line[%d], "
                  //     L"OBJECT_FIELD_NPC_ID",
                  //     m_lexer.line)
                  // L2_LOG_ERROR(Log,
                  //     L"[PARSER_NPC] set fail : line[%d], "
                  //     L"OBJECT_FIELD_NPC_ID",
                  //     m_lexer.line)
                  L2_LOG_ERROR(
                      Log, u"npcdata.txt line %lld, NPC %d: assigning the NPC ID failed; parsing continues.",
                      static_cast<long long>(m_lexer.line) + 1, m_nNpcId
                  );
                  m_pNpc->SetResult(true);
              }

              std::wstring hostName = target_utf16::ToHostCodeUnits(npcName);
              m_pNpc->Set(*m_pObjDB, OBJECT_FIELD_NAME, hostName.data());
          }
          std::free(className);
          if (nameTooLong) {
              // The original parser ran the whole action and then started the error
              // recovery. The grammar has no error rules, so yyparse returns 1.
              ++yyerrorcount;
              YYERROR;
          }
      }
    ;

flying_def_list
    : flying_def_list flying_def
      { $$ = $1; }
    | %empty
    ;

flying_def
    : FLYING '=' INTEGER
      {
          if (m_pNpc != nullptr && m_pNpc->m_data != nullptr) {
              m_pNpc->m_data->m_moveType = static_cast<CreatureMoveType>(grammar::SemanticInt32($3));
          }
      }
    | MP_REWARD '=' '{' INTEGER ';' IDENTIFIER ';' INTEGER ';' IDENTIFIER '}'
      {
          const int expireTick = grammar::SemanticInt32($8);
          const double amount = static_cast<double>(grammar::SemanticInt64($4));
          if (expireTick < 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"line[%d], Invalid mp_reward parameter",
              //     m_lexer.line)
              // L2_LOG_ERROR(Log,
              //     L"line[%d], Invalid mp_reward parameter",
              //     m_lexer.line)
              L2_LOG_ERROR(
                  Log,
                  u"npcdata.txt line %lld, NPC %d: mp_reward expiration is negative (%d ticks); the value is still passed "
                  u"to reward initialization.",
                  static_cast<long long>(m_lexer.line) + 1, m_nNpcId, expireTick
              );
          }
          if (amount < 0.0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"line[%d], Invalid mp_reward parameter",
              //     m_lexer.line)
              // L2_LOG_ERROR(Log,
              //     L"line[%d], Invalid mp_reward parameter",
              //     m_lexer.line)
              L2_LOG_ERROR(
                  Log, u"npcdata.txt line %lld, NPC %d: mp_reward amount is negative (%.17g), expiration %d ticks; %ls.",
                  static_cast<long long>(m_lexer.line) + 1, m_nNpcId, amount, expireTick,
                  expireTick == 0 ? L"reward initialization is skipped because expiration is zero"
                                  : L"reward initialization continues with this amount"
              );
          }
          if (expireTick == 0) {
              break;
          }
          char16_t* const typeName = grammar::SemanticPointer<char16_t>($6);
          char16_t* const scopeName = grammar::SemanticPointer<char16_t>($10);
          MPRewardData::RewardType type = MPRewardData::TypeNone;
          if (EqualAsciiInsensitive(typeName, u"per")) {
              type = MPRewardData::Per;
          } else if (EqualAsciiInsensitive(typeName, u"diff")) {
              type = MPRewardData::Diff;
          }
          MPRewardData::RewardScope scope = MPRewardData::ScopeNone;
          if (EqualAsciiInsensitive(scopeName, u"solo")) {
              scope = MPRewardData::Solo;
          } else if (EqualAsciiInsensitive(scopeName, u"party")) {
              scope = MPRewardData::Party;
          }
          m_pNpc->m_pClassConst->m_MPRewardData.Init(type, amount / static_cast<double>(expireTick), expireTick, scope);
      }
    | NAME_ID_GROUP '=' NAME
      {
          const auto field = static_cast<ObjectFieldType>(grammar::SemanticInt32($1));
          char16_t* const value = grammar::SemanticPointer<char16_t>($3);
          std::wstring hostValue = target_utf16::ToHostCodeUnits(value);
          m_pNpc->Set(*m_pObjDB, field, hostValue.data());
          std::free(value);
      }
    | NAME_ID_GROUP '=' IDENTIFIER
      {
          const auto checkSetResult = [this](ObjectFieldType field) {
              if (m_pNpc != nullptr && !m_pNpc->IsSetResult()) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"[PARSER_NPC] set fail : line[%d], field(%d)",
                  //     m_lexer.line,
                  //     static_cast<int>(field))
                  // L2_LOG_ERROR(Log,
                  //     L"[PARSER_NPC] set fail : line[%d], field(%d)",
                  //     m_lexer.line,
                  //     static_cast<int>(field))
                  L2_LOG_ERROR(
                      Log, u"npcdata.txt line %lld, NPC %d: assigning field %d failed; parsing continues.",
                      static_cast<long long>(m_lexer.line) + 1, m_nNpcId, static_cast<int>(field)
                  );
                  m_pNpc->SetResult(true);
              }
          };
          const auto field = static_cast<ObjectFieldType>(grammar::SemanticInt32($1));
          char16_t* const value = grammar::SemanticPointer<char16_t>($3);
          if (field == OBJECT_FIELD_AI) {
              std::wstring hostValue = target_utf16::ToHostCodeUnits(value);
              m_pNpc->Set(*m_pObjDB, field, hostValue.data());
              if (!m_pNpc->IsSetResult()) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"[PARSER_NPC] set fail : line[%d], OBJECT_FIELD_AI",
                  //     m_lexer.line)
                  // L2_LOG_ERROR(Log,
                  //     L"[PARSER_NPC] set fail : line[%d], OBJECT_FIELD_AI",
                  //     m_lexer.line)
                  L2_LOG_ERROR(
                      Log, u"npcdata.txt line %lld, NPC %d: assigning AI '%ls' failed; parsing continues.",
                      static_cast<long long>(m_lexer.line) + 1, m_nNpcId,
                      value == nullptr ? u"<null>" : value
                  );
                  m_pNpc->SetResult(true);
              }
          } else {
              const std::int32_t parsed = FindInTable(field, value);
              if (parsed == -1) {
                  // const std::wstring hostValue = target_utf16::ToHostCodeUnits(value)
                  const std::wstring hostValue = Utf16ToWide(value);
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"[PARSER_NPC] Undefined id[%d,%ls]",
                  //     static_cast<int>(field),
                  //     hostValue.c_str())
                  // L2_LOG_ERROR(Log,
                  //     L"[PARSER_NPC] Undefined id[%d,%ls]",
                  //     static_cast<int>(field),
                  //     hostValue.c_str())
                  L2_LOG_ERROR(
                      Log, u"npcdata.txt line %lld, NPC %d: unknown value '%ls' for field %d; the field is not assigned.",
                      static_cast<long long>(m_lexer.line) + 1, m_nNpcId, value == nullptr ? L"<null>" : hostValue.c_str(),
                      static_cast<int>(field)
                  );
              } else {
                  m_pNpc->Set(field, static_cast<std::int64_t>(parsed));
                  checkSetResult(field);
              }
          }
          std::free(value);
      }
    | NAME_ID_GROUP '=' INTEGER
      {
          const auto checkSetResult = [this](ObjectFieldType field) {
              if (m_pNpc != nullptr && !m_pNpc->IsSetResult()) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"[PARSER_NPC] set fail : line[%d], field(%d)",
                  //     m_lexer.line,
                  //     static_cast<int>(field))
                  // L2_LOG_ERROR(Log,
                  //     L"[PARSER_NPC] set fail : line[%d], field(%d)",
                  //     m_lexer.line,
                  //     static_cast<int>(field))
                  L2_LOG_ERROR(
                      Log, u"npcdata.txt line %lld, NPC %d: assigning field %d failed; parsing continues.",
                      static_cast<long long>(m_lexer.line) + 1, m_nNpcId, static_cast<int>(field)
                  );
                  m_pNpc->SetResult(true);
              }
          };
          const auto field = static_cast<ObjectFieldType>(grammar::SemanticInt32($1));
          m_pNpc->Set(field, grammar::SemanticInt64($3));
          checkSetResult(field);
      }
    | NAME_ID_GROUP '=' FLOAT_NUMBER
      {
          const auto checkSetResult = [this](ObjectFieldType field) {
              if (m_pNpc != nullptr && !m_pNpc->IsSetResult()) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"[PARSER_NPC] set fail : line[%d], field(%d)",
                  //     m_lexer.line,
                  //     static_cast<int>(field))
                  // L2_LOG_ERROR(Log,
                  //     L"[PARSER_NPC] set fail : line[%d], field(%d)",
                  //     m_lexer.line,
                  //     static_cast<int>(field))
                  L2_LOG_ERROR(
                      Log, u"npcdata.txt line %lld, NPC %d: assigning field %d failed; parsing continues.",
                      static_cast<long long>(m_lexer.line) + 1, m_nNpcId, static_cast<int>(field)
                  );
                  m_pNpc->SetResult(true);
              }
          };
          const auto field = static_cast<ObjectFieldType>(grammar::SemanticInt32($1));
          m_pNpc->Set(field, grammar::SemanticDouble($3));
          checkSetResult(field);
      }
    | NAME_ID_GROUP '=' '{' n9 '}'
      {
          const auto field = static_cast<ObjectFieldType>(grammar::SemanticInt32($1));
          auto* const values = grammar::SemanticPointer<std::vector<double>>($4);
          m_pNpc->Set(field, values);
          delete values;
      }
    | NAME_ID_GROUP '=' '{' '}'
      {
          if (grammar::SemanticInt32($1) == OBJECT_FIELD_VITALITY_ITEM_DROP_LIST) {
              m_bCheckVitalityDropItem = true;
          }
      }
    | NAME_ID_GROUP '=' '{' n11 '}'
      {
          const auto field = static_cast<ObjectFieldType>(grammar::SemanticInt32($1));
          auto* const words = grammar::SemanticPointer<std::vector<char16_t*>>($4);
          if (words == nullptr) {
              break;
          }
          std::vector<std::int64_t> values = ConvertStrList64(field, *words);
          m_pNpc->Set(field, &values);
          DeleteStringList(words);
      }
    | NAME_ID_GROUP '=' '{' n12 '}'
      {
          const auto field = static_cast<ObjectFieldType>(grammar::SemanticInt32($1));
          auto* const values = grammar::SemanticPointer<std::vector<ItemDropMultiStruct*>>($4);
          if (values == nullptr) {
              break;
          }
          m_pNpc->Set(field, values);
          ValidateMultiDropProbabilities(values, m_nNpcId);
          if (field == OBJECT_FIELD_VITALITY_ITEM_DROP_LIST) {
              m_bCheckVitalityDropItem = true;
          }
          delete values;
      }
    | NAME_ID_GROUP '=' '{' n14 '}'
      {
          auto* const values = grammar::SemanticPointer<std::vector<ItemDropStruct*>>($4);
          if (values == nullptr) {
              break;
          }
          m_pNpc->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), values);
          delete values;
      }
    | NAME_ID_GROUP '=' '{' n16 '}'
      {
          m_pNpc->Set(
              static_cast<ObjectFieldType>(grammar::SemanticInt32($1)),
              grammar::SemanticPointer<std::vector<GoodStruct*>>($4)
          );
      }
    | BROADCAST_COND '=' '{' '}'
    | BROADCAST_COND '=' '{' n10 '}'
      { m_pNpc->SetBCCondList(grammar::SemanticPointer<std::vector<int>>($4)); }
    | NAME_ID_GROUP '=' '{' name_def '}'
      {
          const auto field = static_cast<ObjectFieldType>(grammar::SemanticInt32($1));
          char16_t* const value = grammar::SemanticPointer<char16_t>($4);
          mAIName.assign(value == nullptr ? u"" : value);
          std::wstring hostValue = target_utf16::ToHostCodeUnits(value);
          m_pNpc->Set(*m_pObjDB, field, hostValue.data());
          if (!m_pNpc->IsSetResult()) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[PARSER_NPC] set fail : line[%d]",
              //     m_lexer.line)
              // L2_LOG_ERROR(Log,
              //     L"[PARSER_NPC] set fail : line[%d]",
              //     m_lexer.line)
              L2_LOG_ERROR(
                  Log, u"npcdata.txt line %lld, NPC %d: assigning field %d to '%ls' failed; parsing continues.",
                  static_cast<long long>(m_lexer.line) + 1, m_nNpcId, static_cast<int>(field),
                  value == nullptr ? u"<null>" : value
              );
              m_pNpc->SetResult(true);
          }
      }
    | NAME_ID_GROUP '=' '{' n21 '}'
      {
          const auto field = static_cast<ObjectFieldType>(grammar::SemanticInt32($1));
          m_pNpc->Set(field, grammar::SemanticPointer<std::vector<double>>($4));
          if (!m_pNpc->IsSetResult()) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[PARSER_NPC] set fail : line[%d]",
              //     m_lexer.line)
              // L2_LOG_ERROR(Log,
              //     L"[PARSER_NPC] set fail : line[%d]",
              //     m_lexer.line)
              L2_LOG_ERROR(
                  Log, u"npcdata.txt line %lld, NPC %d: assigning the numeric list to field %d failed; parsing continues.",
                  static_cast<long long>(m_lexer.line) + 1, m_nNpcId, static_cast<int>(field)
              );
              m_pNpc->SetResult(true);
          }
      }
    | CATEGORY '=' '{' '}'
    | EVENT_DROP '=' INTEGER
      { m_pNpc->setEventDropPorbType(grammar::SemanticInt32($3)); }
    | EX_DROP '=' '{' '}'
    | EX_DROP '=' '{' n10 '}'
      { m_pNpc->setExDropItem(grammar::SemanticPointer<std::vector<int>>($4)); }
    | G_ADDITIONAL_MAKE_MULTI_LIST '=' '{' n22 '}'
    ;

n9
    : n9 ';' FLOAT_NUMBER
      {
          auto* const values = grammar::SemanticPointer<std::vector<ItemDropMultiStruct*>>($1);
          values->push_back(grammar::SemanticPointer<ItemDropMultiStruct>($3));
          $$ = $1;
      }
    | FLOAT_NUMBER
      {
          auto* const values = new std::vector<double>();
          values->push_back(grammar::SemanticDouble($1));
          $$ = grammar::PointerSemantic(values);
      }
    | INTEGER
      {
          auto* const values = new std::vector<double>();
          values->push_back(static_cast<double>(grammar::SemanticInt64($1)));
          $$ = grammar::PointerSemantic(values);
      }
    | n9 ';' INTEGER
      {
          auto* const values = grammar::SemanticPointer<std::vector<double>>($1);
          values->push_back(static_cast<double>(grammar::SemanticInt64($3)));
          $$ = $1;
      }
    ;

n10
    : n10 ';' INTEGER
      {
          auto* const values = grammar::SemanticPointer<std::vector<int>>($1);
          values->push_back(grammar::SemanticInt32($3));
          $$ = $1;
      }
    | INTEGER
      {
          auto* const values = new std::vector<int>();
          values->push_back(grammar::SemanticInt32($1));
          $$ = grammar::PointerSemantic(values);
      }
    ;

n11
    : n11 ';' IDENTIFIER
      {
          auto* const values = grammar::SemanticPointer<std::vector<char16_t*>>($1);
          values->push_back(grammar::SemanticPointer<char16_t>($3));
          $$ = $1;
      }
    | IDENTIFIER
      {
          auto* const values = new std::vector<char16_t*>();
          values->push_back(grammar::SemanticPointer<char16_t>($1));
          $$ = grammar::PointerSemantic(values);
      }
    ;

n12
    : n12 ';' n13
      {
          auto* const values = grammar::SemanticPointer<std::vector<ItemDropMultiStruct*>>($1);
          values->push_back(grammar::SemanticPointer<ItemDropMultiStruct>($3));
          $$ = $1;
      }
    | n13
      {
          auto* const values = new std::vector<ItemDropMultiStruct*>();
          values->push_back(grammar::SemanticPointer<ItemDropMultiStruct>($1));
          $$ = grammar::PointerSemantic(values);
      }
    ;

n13
    : '{' '{' n14 '}' ';' FLOAT_NUMBER '}'
      {
          auto* const value = new ItemDropMultiStruct();
          value->pItemDropList = grammar::SemanticPointer<std::vector<ItemDropStruct*>>($3);
          value->DropProb = grammar::SemanticDouble($6);
          value->m_bChanceAffected = false;
          $$ = grammar::PointerSemantic(value);
      }
    | '{' '{' n14 '}' ';' INTEGER '}'
      {
          auto* const value = new ItemDropMultiStruct();
          value->pItemDropList = grammar::SemanticPointer<std::vector<ItemDropStruct*>>($3);
          value->DropProb = static_cast<double>(grammar::SemanticInt64($6));
          value->m_bChanceAffected = false;
          $$ = grammar::PointerSemantic(value);
      }
    ;

n14
    : n14 ';' n15
      {
          auto* const values = grammar::SemanticPointer<std::vector<ItemDropStruct*>>($1);
          values->push_back(grammar::SemanticPointer<ItemDropStruct>($3));
          $$ = $1;
      }
    | n15
      {
          auto* const values = new std::vector<ItemDropStruct*>();
          values->push_back(grammar::SemanticPointer<ItemDropStruct>($1));
          $$ = grammar::PointerSemantic(values);
      }
    ;

n15
    : '{' NAME ';' INTEGER ';' INTEGER ';' FLOAT_NUMBER '}'
      {
          char16_t* const itemName = grammar::SemanticPointer<char16_t>($2);
          auto* const value = new ItemDropStruct();
          value->ItemClassId = m_pObjDB->GetClassIdFromName(itemName, false);
          value->DropMin = grammar::SemanticInt64($4);
          value->DropMax = grammar::SemanticInt64($6);
          value->DropProb = grammar::SemanticDouble($8);
          std::free(itemName);
          $$ = grammar::PointerSemantic(value);
      }
    | '{' NAME ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          char16_t* const itemName = grammar::SemanticPointer<char16_t>($2);
          auto* const value = new ItemDropStruct();
          value->ItemClassId = m_pObjDB->GetClassIdFromName(itemName, false);
          value->DropMin = grammar::SemanticInt64($4);
          value->DropMax = grammar::SemanticInt64($6);
          value->DropProb = static_cast<double>(grammar::SemanticInt64($8));
          std::free(itemName);
          $$ = grammar::PointerSemantic(value);
      }
    ;

n16
    : n16 ';' n17
      {
          auto* const values = grammar::SemanticPointer<std::vector<GoodStruct*>>($1);
          values->push_back(grammar::SemanticPointer<GoodStruct>($3));
          $$ = $1;
      }
    | n17
      {
          auto* const values = new std::vector<GoodStruct*>();
          values->push_back(grammar::SemanticPointer<GoodStruct>($1));
          $$ = grammar::PointerSemantic(values);
      }
    ;

n17
    : '{' NAME ';' INTEGER '}'
      {
          char16_t* const itemName = grammar::SemanticPointer<char16_t>($2);
          auto* const value = new GoodStruct();
          value->ItemClassId = m_pObjDB->GetClassIdFromName(itemName, false);
          value->PriceChange = grammar::SemanticInt32($4);
          std::free(itemName);
          $$ = grammar::PointerSemantic(value);
      }
    ;

name_def
    : NAME ';' n19
      { $$ = $1; }
    | NAME ';' n19 ';'
      { $$ = $1; }
    | NAME
    ;

n19
    : n19 ';' '{' name_def_2 '}'
    | '{' name_def_2 '}'
    ;

name_def_2
    : NAME '=' NAME
      {
          char16_t* const key = grammar::SemanticPointer<char16_t>($1);
          char16_t* const value = grammar::SemanticPointer<char16_t>($3);
          m_mapParameter[std::u16string(key)] = std::u16string(value);
      }
    | NAME '=' INTEGER
      {
          char16_t* const key = grammar::SemanticPointer<char16_t>($1);
          m_mapParameter[std::u16string(key)] = FormatInt64(grammar::SemanticInt64($3));
      }
    | NAME '=' FLOAT_NUMBER
      {
          char16_t* const key = grammar::SemanticPointer<char16_t>($1);
          m_mapParameter[std::u16string(key)] = FormatDouble(grammar::SemanticDouble($3));
      }
    | NAME '=' IDENTIFIER
    ;

n21
    : n11 ';' INTEGER
      {
          auto* const words = grammar::SemanticPointer<std::vector<char16_t*>>($1);
          if (words->size() != 1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"line[%d], base_attribute_attack has invalid param",
              //     m_lexer.line)
              // L2_LOG_ERROR(Log,
              //     L"line[%d], base_attribute_attack has invalid param",
              //     m_lexer.line)
              L2_LOG_ERROR(
                  Log,
                  u"npcdata.txt line %lld, NPC %d: base_attribute_attack expects one attribute name, got %llu; parsing "
                  u"still attempts to read the first entry.",
                  static_cast<long long>(m_lexer.line) + 1, m_nNpcId, static_cast<unsigned long long>(words->size())
              );
          }
          const std::int32_t type = FindInTable(OBJECT_FIELD_BASE_ATTRIBUTE_TYPE, runtime::CheckedVectorAt((*words), 0));
          if (type == -1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"line[%d], base_attribute_attack has invalid param.",
              //     m_lexer.line)
              // L2_LOG_ERROR(Log,
              //     L"line[%d], base_attribute_attack has invalid param.",
              //     m_lexer.line)
              L2_LOG_ERROR(
                  Log,
                  u"npcdata.txt line %lld, NPC %d: unknown base_attribute_attack name '%ls'; the attribute type is "
                  u"retained as -1.",
                  static_cast<long long>(m_lexer.line) + 1, m_nNpcId,
                  runtime::CheckedVectorAt((*words), 0) == nullptr ? u"<null>"
                                                              : runtime::CheckedVectorAt((*words), 0)
              );
          }
          auto* const values = new std::vector<double>();
          values->push_back(static_cast<double>(type));
          values->push_back(static_cast<double>(grammar::SemanticInt64($3)));
          $$ = grammar::PointerSemantic(values);
      }
    ;

n22
    : n22 ';' '{' n23 '}'
    | '{' n23 '}'
    | n23
    | %empty
    ;

n23
    : n12
      {
          auto* const values = grammar::SemanticPointer<std::vector<ItemDropMultiStruct*>>($1);
          if (values == nullptr) {
              break;
          }
          if (g_nCountryCode != 0) {
              m_pNpc->m_pClassConst->m_nItemDropValue = 0;
              m_pNpc->m_pClassConst->m_ItemAdditionalMakeListMulti.clear();
              m_pNpc->Set(OBJECT_FIELD_ITEM_ADDITIONAL_DROP_LIST_MULTI, values);
              ValidateMultiDropProbabilities(values, m_nNpcId);
          }
          delete values;
      }
    | '{' n24 '}' ';' '{' n12 '}'
      {
          if (!m_bIsgMultiList) {
              break;
          }
          auto* const values = grammar::SemanticPointer<std::vector<ItemDropMultiStruct*>>($6);
          if (values == nullptr) {
              break;
          }
          m_pNpc->m_pClassConst->m_nItemDropValue = 0;
          m_pNpc->m_pClassConst->m_ItemAdditionalMakeListMulti.clear();
          m_pNpc->Set(OBJECT_FIELD_ITEM_ADDITIONAL_DROP_LIST_MULTI, values);
          ValidateMultiDropProbabilities(values, m_nNpcId);
          delete values;
      }
    ;

n24
    : n11
      {
          m_bIsgMultiList = false;
          auto* const countries = grammar::SemanticPointer<std::vector<char16_t*>>($1);
          for (char16_t* const country : *countries) {
              const int code = FindCountryCode(country);
              if (code == -1) {
                  // const std::wstring hostCountry = target_utf16::ToHostCodeUnits(country)
                  const std::wstring hostCountry = Utf16ToWide(country);
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined CountryCode[%ls]",
                  //     hostCountry.c_str(),
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"Undefined CountryCode[%ls]",
                  //     hostCountry.c_str(),
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log, u"npcdata.txt line %lld, NPC %d: unknown country '%ls'; lookup returns country code -1.",
                      static_cast<long long>(m_lexer.line) + 1, m_nNpcId,
                      country == nullptr ? L"<null>" : hostCountry.c_str()
                  );
              }
              if (code == g_nCountryCode) {
                  m_bIsgMultiList = true;
                  break;
              }
          }
      }
    ;

// The original tables have rule 67 that no parser state reduces.
// It is left out.

%%
