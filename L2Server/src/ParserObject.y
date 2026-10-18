// CParserForObject grammar (original L2Server/parser_object.y).
// Bison generates ParserObject.cpp and ParserObjectTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserObject.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/CommissionTrade.h"
#include "L2Server/src/Object.h"
#include "L2Server/src/ObjectDB.h"
#include "L2Server/src/Transform.h"

#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

#include "ParserObjectTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForObject::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserObjectTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

char16_t FoldAscii(char16_t value) {
    return value >= u'A' && value <= u'Z' ? static_cast<char16_t>(value - u'A' + u'a') : value;
}

bool EqualsIgnoreCase(const char16_t* left, std::u16string_view right) {
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

struct WordValue {
    std::u16string_view word;
    std::int64_t value;
};

template <std::size_t Size>
std::int64_t LookupWord(const char16_t* word, const WordValue (&entries)[Size]) {
    for (const WordValue& entry : entries) {
        if (EqualsIgnoreCase(word, entry.word)) {
            return entry.value;
        }
    }
    return -1;
}

constexpr WordValue kArmorTypes[] = {
    {u"none", 0},
    {u"light", 1},
    {u"heavy", 2},
    {u"magic", 3},
    {u"sigil", 4},
};
constexpr WordValue kItemTypes[] = {
    {u"weapon", 0},
    {u"accessary", 2},
    {u"armor", 1},
    {u"asset", 4},
    {u"etcitem", 5},
    {u"questitem", 3},
};
constexpr WordValue kConsumeTypes[] = {
    {u"consume_type_normal", 0},
    {u"consume_type_charge", 1},
    {u"consume_type_stackable", 2},
    {u"consume_type_asset", 3},
};
constexpr WordValue kSlotTypes[] = {
    {u"none", 0},
    {u"underwear", 1},
    {u"rear", 2},
    {u"lear", 4},
    {u"neck", 8},
    {u"rfinger", 16},
    {u"lfinger", 32},
    {u"head", 64},
    {u"rhand", 128},
    {u"lhand", 256},
    {u"rlhand", 16384},
    {u"gloves", 512},
    {u"chest", 1024},
    {u"legs", 2048},
    {u"feet", 4096},
    {u"back", 8192},
    {u"lrhand", 16384},
    {u"onepiece", 32768},
    {u"hair", 65536},
    {u"alldress", 131072},
    {u"hair2", 262144},
    {u"hairall", 524288},
    {u"rbracelet", 1048576},
    {u"lbracelet", 2097152},
    {u"deco1", 4194304},
    {u"deco2", 8388608},
    {u"deco3", 16777216},
    {u"deco4", 33554432},
    {u"deco5", 67108864},
    {u"deco6", 134217728},
    {u"waist", 268435456},
    {u"brooch", 536870912},
    {u"jewel1", 1073741824LL},
    {u"jewel2", 2147483648LL},
    {u"jewel3", 4294967296LL},
    {u"jewel4", 8589934592LL},
    {u"jewel5", 17179869184LL},
    {u"jewel6", 34359738368LL},
    {u"agathion_main", 68719476736LL},
    {u"agathion_sub1", 137438953472LL},
    {u"agathion_sub2", 274877906944LL},
    {u"agathion_sub3", 549755813888LL},
    {u"agathion_sub4", 1099511627776LL},
    {u"artifactbook", 2199023255552LL},
    {u"artifact_a1", 4398046511104LL},
    {u"artifact_a2", 8796093022208LL},
    {u"artifact_a3", 17592186044416LL},
    {u"artifact_a4", 35184372088832LL},
    {u"artifact_a5", 70368744177664LL},
    {u"artifact_a6", 140737488355328LL},
    {u"artifact_a7", 281474976710656LL},
    {u"artifact_a8", 562949953421312LL},
    {u"artifact_a9", 1125899906842624LL},
    {u"artifact_a10", 2251799813685248LL},
    {u"artifact_a11", 4503599627370496LL},
    {u"artifact_a12", 9007199254740992LL},
    {u"artifact_b1", 18014398509481984LL},
    {u"artifact_b2", 36028797018963968LL},
    {u"artifact_b3", 72057594037927936LL},
    {u"artifact_c1", 144115188075855872LL},
    {u"artifact_c2", 288230376151711744LL},
    {u"artifact_c3", 576460752303423488LL},
    {u"artifact_d1", 1152921504606846976LL},
    {u"artifact_d2", 2305843009213693952LL},
    {u"artifact_d3", 4611686018427387904LL},
};
constexpr WordValue kMaterialTypes[] = {
    {u"oriharukon", 1},
    {u"mithril", 2},
    {u"gold", 3},
    {u"silver", 4},
    {u"brass", 5},
    {u"bronze", 6},
    {u"diamond", 7},
    {u"steel", 8},
    {u"damascus_steel", 9},
    {u"bone_of_dragon", 10},
    {u"air", 11},
    {u"water", 12},
    {u"wood", 13},
    {u"bone", 14},
    {u"branch_of_orbis_arbor", 15},
    {u"branch_of_worldtree", 16},
    {u"cloth", 17},
    {u"paper", 18},
    {u"leather", 19},
    {u"skull", 20},
    {u"feather", 21},
    {u"sand", 22},
    {u"crystal", 23},
    {u"fur", 24},
    {u"parchment", 25},
    {u"eyeball", 26},
    {u"insect", 27},
    {u"corpse", 28},
    {u"crow", 29},
    {u"hatching_egg", 30},
    {u"skull_of_wyvern", 31},
    {u"drug", 32},
    {u"cotton", 33},
    {u"silk", 34},
    {u"wool", 35},
    {u"flax", 36},
    {u"cobweb", 37},
    {u"dyestuff", 38},
    {u"leather_of_puma", 39},
    {u"leather_of_lion", 40},
    {u"leather_of_manticore", 41},
    {u"leather_of_drake", 42},
    {u"leather_of_salamander", 43},
    {u"leather_of_unicorn", 44},
    {u"leather_of_dragon", 45},
    {u"scale_of_dragon", 46},
    {u"adamantaite", 47},
    {u"blood_steel", 48},
    {u"chrysolite", 49},
    {u"damascus", 50},
    {u"fine_steel", 51},
    {u"horn", 52},
    {u"liquid", 53},
    {u"fish", 54},
    {u"rune_xp", 55},
    {u"rune_sp", 56},
    {u"rune_remove_penalty", 57},
};
constexpr WordValue kWeaponTypes[] = {
    {u"none", 0},
    {u"sword", 1},
    {u"twohandsword", 2},
    {u"buster", 3},
    {u"blunt", 4},
    {u"twohandblunt", 5},
    {u"staff", 6},
    {u"twohandstaff", 7},
    {u"dagger", 8},
    {u"pole", 9},
    {u"fist", 10},
    {u"bow", 11},
    {u"etc", 12},
    {u"dual", 13},
    {u"dualfist", 14},
    {u"fishingrod", 15},
    {u"rapier", 16},
    {u"crossbow", 17},
    {u"ancientsword", 18},
    {u"flag", 19},
    {u"dualdagger", 20},
    {u"ownthing", 21},
    {u"dualblunt", 23},
    {u"twohandcrossbow", 22},
};
constexpr WordValue kEtcItemTypes[] = {
    {u"none", 0},
    {u"scroll", 1},
    {u"arrow", 2},
    {u"potion", 3},
    {u"spellbook", 4},
    {u"recipe", 5},
    {u"material", 6},
    {u"pet_collar", 7},
    {u"castle_guard", 8},
    {u"dye", 9},
    {u"seed", 10},
    {u"seed2", 11},
    {u"harvest", 12},
    {u"lotto", 13},
    {u"race_ticket", 14},
    {u"ticket_of_lord", 15},
    {u"lure", 16},
    {u"crop", 17},
    {u"maturecrop", 18},
    {u"scrl_enchant_wp", 19},
    {u"scrl_enchant_am", 20},
    {u"bless_scrl_enchant_wp", 21},
    {u"bless_scrl_enchant_am", 22},
    {u"coupon", 23},
    {u"elixir", 24},
    {u"scrl_enchant_attr", 25},
    {u"scrl_enchant_attr_cursed", 26},
    {u"bolt", 27},
    {u"scrl_inc_enchant_prop_wp", 28},
    {u"scrl_inc_enchant_prop_am", 29},
    {u"crystal_enchant_am", 30},
    {u"crystal_enchant_wp", 31},
    {u"ancient_crystal_enchant_am", 32},
    {u"ancient_crystal_enchant_wp", 33},
    {u"rune", 34},
    {u"rune_select", 35},
    {u"teleportbookmark", 36},
    {u"scrl_lock_item", 70},
    {u"scrl_unlock_item", 71},
    {u"scrl_change_attr", 37},
    {u"scrl_shape_shifting_wp", 39},
    {u"bless_scrl_shape_shifting_wp", 40},
    {u"scrl_shape_shifting_wp_fixed", 41},
    {u"scrl_shape_shifting_am", 42},
    {u"bless_scrl_shape_shifting_am", 43},
    {u"scrl_shape_shifting_am_fixed", 44},
    {u"scrl_shape_shifting_hair_acc", 45},
    {u"bless_scrl_shape_shifting_hair_acc", 46},
    {u"scrl_shape_shifting_hair_acc_fixed", 47},
    {u"scrl_shape_shifting_all_item_fixed", 55},
    {u"scrl_restore_shape_shifting_wp", 48},
    {u"scrl_restore_shape_shifting_am", 49},
    {u"scrl_restore_shape_shifting_hair_acc", 50},
    {u"scrl_restore_shape_shifting_all_item", 51},
    {u"bless_scrl_inc_enchant_prop_wp", 52},
    {u"bless_scrl_inc_enchant_prop_am", 53},
    {u"scrl_multi_enchant_wp", 56},
    {u"scrl_multi_enchant_am", 57},
    {u"scrl_multi_inc_enchant_prob_wp", 58},
    {u"scrl_multi_inc_enchant_prob_am", 59},
    {u"ensoul_stone", 60},
    {u"nick_color_old", 61},
    {u"nick_color_new", 62},
    {u"scrl_enchant_ag", 63},
    {u"bless_scrl_enchant_ag", 64},
    {u"scrl_multi_enchant_ag", 65},
    {u"ancient_crystal_enchant_ag", 66},
    {u"scrl_inc_enchant_prop_ag", 67},
    {u"bless_scrl_inc_enchant_prop_ag", 68},
    {u"scrl_multi_inc_enchant_prob_ag", 69},
};
constexpr WordValue kSetSlots[] = {
    {u"slot_underwear", 0},
    {u"slot_rear", 1},
    {u"slot_lear", 2},
    {u"slot_neck", 3},
    {u"slot_rfinger", 4},
    {u"slot_lfinger", 5},
    {u"slot_head", 6},
    {u"slot_rhand", 7},
    {u"slot_lhand", 8},
    {u"slot_gloves", 9},
    {u"slot_chest", 10},
    {u"slot_legs", 11},
    {u"slot_feet", 12},
    {u"slot_back", 13},
    {u"slot_rlhand", 14},
    {u"none", 63},
};
constexpr WordValue kBaseAttributes[] = {
    {u"none", -2},
    {u"fire", 0},
    {u"water", 1},
    {u"wind", 2},
    {u"earth", 3},
    {u"holy", 4},
    {u"unholy", 5},
};
constexpr WordValue kCountries[] = {
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
};

std::int64_t FindInTable(ObjectFieldType field, const char16_t* word) {
    switch (field) {
        case OBJECT_FIELD_ARMOR_TYPE:
            return LookupWord(word, kArmorTypes);
        case OBJECT_FIELD_ITEM_TYPE:
            return LookupWord(word, kItemTypes);
        case OBJECT_FIELD_CONSUME_TYPE:
            return LookupWord(word, kConsumeTypes);
        case OBJECT_FIELD_SLOT_BIT_TYPE:
            return LookupWord(word, kSlotTypes);
        case OBJECT_FIELD_MATERIAL_TYPE:
            return LookupWord(word, kMaterialTypes);
        case OBJECT_FIELD_WEAPON_TYPE:
            return LookupWord(word, kWeaponTypes);
        case OBJECT_FIELD_ETCITEM_TYPE:
            return LookupWord(word, kEtcItemTypes);
        case OBJECT_FIELD_SETITEM_SLOT_TYPE:
            return LookupWord(word, kSetSlots);
        case OBJECT_FIELD_BASE_ATTRIBUTE_TYPE:
            return LookupWord(word, kBaseAttributes);
        default:
            return -1;
    }
}

int FindCountryCode(const char16_t* word) { return static_cast<int>(LookupWord(word, kCountries)); }

std::vector<int> ConvertStrList32(ObjectFieldType field, const std::vector<char16_t*>& words) {
    std::vector<int> values;
    for (int index = 0; index < static_cast<int>(words.size()); ++index) {
        const char16_t* const word = runtime::CheckedVectorAt(words, index);
        const std::int32_t value = static_cast<std::int32_t>(FindInTable(field, word));
        if (value == -1) {
            const std::wstring host = target_utf16::ToHostCodeUnits(word);
            // Log.Add(LOG_ERROR, L"Undefined word[%ls]", host.c_str())
            // Previous: L2_LOG_ERROR(Log, L"Undefined word[%ls]", host.c_str())
            L2_LOG_ERROR(
                Log,
                u"[object parser] Value '%ls' is undefined; it is omitted "
                u"from this integer list.",
                host.c_str()
            );
        } else {
            values.push_back(value);
        }
    }
    return values;
}

std::vector<std::int64_t> ConvertStrList64(ObjectFieldType field, const std::vector<char16_t*>& words) {
    std::vector<std::int64_t> values;
    for (int index = 0; index < static_cast<int>(words.size()); ++index) {
        const char16_t* const word = runtime::CheckedVectorAt(words, index);
        const std::int64_t value = FindInTable(field, word);
        if (value == -1) {
            const std::wstring host = target_utf16::ToHostCodeUnits(word);
            // Log.Add(LOG_ERROR, L"Undefiend word[%ls]", host.c_str())
            // Previous: L2_LOG_ERROR(Log, L"Undefiend word[%ls]", host.c_str())
            L2_LOG_ERROR(
                Log,
                u"[object parser] Value '%ls' is undefined; it is omitted "
                u"from this 64-bit integer list.",
                host.c_str()
            );
        } else {
            values.push_back(value);
        }
    }
    return values;
}

std::vector<int> ConvertTransformTypes(const std::vector<char16_t*>* names) {
    std::vector<int> values;
    if (names == nullptr) {
        return values;
    }
    for (const char16_t* name : *names) {
        const std::wstring host = target_utf16::ToHostCodeUnits(name);
        values.push_back(CTransform::GetTransformType(host.c_str()));
    }
    return values;
}

std::vector<int> ConvertAbnormalTypes(const std::vector<char16_t*>* names) {
    std::vector<int> values;
    if (names == nullptr) {
        return values;
    }
    for (const char16_t* name : *names) {
        const AbnormalStatusSlotType type = GetAbnormalStatusSlotTypeByName(const_cast<char16_t*>(name));
        if (type != ABNS_INVALID) {
            values.push_back(static_cast<int>(type));
        }
    }
    return values;
}

CSkillKey ResolveSkillKey(const char16_t* name) {
    std::wstring host = target_utf16::ToHostCodeUnits(name);
    return g_SkillDB.GetSkillKeyByName(host.data());
}

int ResolveItemClassId(const char16_t* name, bool onlyPrecompiledHeader, int line) {
    if (onlyPrecompiledHeader) {
        return -1;
    }
    const int classId = static_cast<int>(g_PrecompiledHeaderDB.Search(name));
    if (classId == -1) {
        const std::wstring host = target_utf16::ToHostCodeUnits(name);
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid Item[%ls] while loading itemdata, Line = [%d]",
        //     host.c_str(),
        //     line)
        // Previous: L2_LOG_ERROR(Log,
        //     L"Invalid Item[%ls] while loading itemdata, Line = [%d]",
        //     host.c_str(), line)
        L2_LOG_ERROR(
            Log,
            u"[object parser] Item '%ls' is undefined at line %d; class ID "
            u"-1 is returned.",
            host.c_str(), line
        );
    }
    return classId;
}

bool AppliesToCurrentCountry(const char16_t* country, int line) {
    const int code = FindCountryCode(country);
    if (code == -1) {
        const std::wstring host = target_utf16::ToHostCodeUnits(country);
        // Log.Add(
        //     LOG_ERROR,
        //     L"Undefined CountryCode[%ls] in line[%d]",
        //     host.c_str(),
        //     line)
        // Previous: L2_LOG_ERROR(Log,
        //     L"Undefined CountryCode[%ls] in line[%d]",
        //     host.c_str(),
        //     line)
        L2_LOG_ERROR(
            Log,
            u"[object parser] Country code '%ls' is undefined at line %d; "
            u"the entry does not apply to this server.",
            host.c_str(), line
        );
    }
    return code == 0 || code == g_nCountryCode;
}

} // namespace

CParserForObject::CParserForObject() : m_lexer(), m_itemRandomListGroup(), m_itemRandomList() {
    m_pItemConstInfo = nullptr;
    m_nItemClassId = 0;
    m_pItem = nullptr;
    m_bOnlyPrecompiledHeader = false;
    m_bReload = false;
    m_bNewItem = true;
    m_pSetItem = nullptr;
    m_pCapsuledItem = nullptr;
    m_itemRandomListGroup.clear();
    m_itemRandomList.clear();
}

CParserForObject::~CParserForObject() = default;

int CParserForObject::Create(CObjectDB* objectDB, char16_t* fileBuffer, int fileSize) {
    m_pObjDB = objectDB;
    return m_lexer.Create(this, fileBuffer, fileSize);
}

void CParserForObject::yysyntaxerror() {
    char16_t message[60]{};
    FormatUtf16To(message, std::size(message), u"ItemDataChecker[%d] : syntax error", m_lexer.line + 1);
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

%token ITEM_BEGIN
%token ITEM_END
%token NAME
%token IDENTIFIER
%token ITEM_ID_GROUP
%token WEAPON_GROUP
%token NO_ATTACK_FORCE_EQUIP_ONLY_USE_WEAPON_SKILL
%token ACTION_VALUE
%token FLOAT_NUMBER
%token CATEGORY
%token DEFAULT_ACTION
%token RECIPE_ID
%token ITEM_EQUIP_OPTION
%token IS_PREMIUM
%token ENCHANT_ENABLE
%token CAPSULED_ITEMS
%token IS_OLYMPIAD_CAN_USE
%token IS_PLEDGEGAME_CAN_USE
%token IS_HERO_ENTRY
%token IS_CHANCE_AFFECTED
%token IS_HP_LIMIT_BREAK
%token SET_BEGIN
%token SET_END
%token P_DEF_INC_GROUP
%token SLOT_UNDERWEAR
%token SLOT_REAR
%token SLOT_LEAR
%token SLOT_NECK
%token SLOT_RFINGER
%token SLOT_LFINGER
%token SLOT_HEAD
%token SLOT_RHAND
%token SLOT_LHAND
%token SLOT_GLOVES
%token SLOT_CHEST
%token SLOT_LEGS
%token SLOT_FEET
%token SLOT_BACK
%token SLOT_RLHAND
%token SLOT_ADDITIONAL
%token SET_SKILL
%token SET_EFFECT_SKILL
%token SET_ADDITIONAL_EFFECT_SKILL
%token SET_ADDITIONAL2_CONDITION
%token SET_ADDITIONAL2_EFFECT_SKILL
%token SLOT_BIT_TYPE
%token USE_CONDITION
%token UC_IN_RESIDENCE_SIEGE_FIELD
%token UC_RACE
%token UC_REQUIREDLEVEL
%token UC_CLASSID
%token UC_CATEGORY
%token UC_INZONE
%token UC_INZONE_NUM
%token UC_TRANSMODE_INCLUDE
%token UC_TRANSMODE_EXCLUDE
%token UC_LEVEL
%token UC_SP
%token UC_ADENA
%token UC_TRANSFORMID
%token UC_VITAL_RESTORE
%token UC_VITAL_ITEM_COUNT
%token UC_VITAL_POINT_REMAIN
%token UC_RESTART_POINT
%token UC_PEACEZONE
%token UC_SUBJOB
%token UC_HP_MAX
%token UC_MP_MAX
%token UC_CP_MAX
%token UC_PREVENTDUPLICATION
%token UC_KALIEBUFF
%token UC_RESTOREVITAL
%token UC_RESTOREVITAL_DOWN
%token EQUIP_CONDITION
%token EC_SEX
%token EC_RACE
%token EC_CLASSID
%token EC_REQUIREDLEVEL
%token EC_CATEGORY
%token EC_AGIT
%token EC_AGIT_NUM
%token EC_CASTLE
%token EC_CASTLE_NUM
%token EC_FORTRESS
%token EC_FORTRESS_NUM
%token EC_CLAN_LEADER
%token EC_CLAN_LEVEL
%token EC_HERO
%token EC_NOBLESS
%token EC_CHAO
%token EC_ACADEMY
%token EC_SOCIAL_CLASS
%token EC_SUBJOB
%token T_1005E
%token EC_INZONE
%token EC_INZONE_NUM
%token EC_TRANSMODE_INCLUDE
%token EC_TRANSMODE_EXCLUDE
%token EC_CASTLE_TYPE
%token EC_PLEDGE
%token EC_MAIN_DUAL_LEVEL
%token EC_JOB_GROUP
%token DEFAULT_PRICE
%token IMMEDIATE_EFFECT
%token EX_IMMEDIATE_EFFECT
%token ITEM_MULTI_SKILL_LIST
%token UNEQUIP_SKILL
%token BASE_ATTRIBUTE_DEFEND
%token DURATION
%token USE_SKILL_DISTIME
%token PERIOD
%token EQUIP_SKILL
%token ITEM_SKILL
%token CRITICAL_ATTACK_SKILL
%token ATTACK_SKILL
%token MAGIC_SKILL
%token T_10075
%token KEEP_TYPE
%token CAN_MOVE
%token IS_TRADE
%token IS_PRIVATE_STORE
%token IS_DROP
%token IS_DESTRUCT
%token IS_NPCTRADE
%token IS_ATTRIBUTION
%token ATTR_GROUPID
%token IS_LOCKED
%token CRYSTAL_TYPE
%token CRYSTAL_ITEM
%token CREATE_ITEMS
%token CREATE_ITEM_GROUPS
%token VALIDATE_PARAMS
%token PROPERTY_PARAMS
%token AUCTION_ITEMTYPE
%token GENERAL_ITEMTYPE
%token SHAPE_SHIFTABLE
%token ITEM_OPTION
// The original grammar has these conflicts. Bison resolves them as the
// original tables did. A shift wins over a reduce.
%expect 1
%start set_list

%%

set_list
    : set_list n6
    | set_list set
    | %empty
    ;

set
    : SET_BEGIN action_value_def slot_underwear_def_list SET_END
      { m_pObjDB->SetRegisterObject(m_pSetItem); }
    ;

action_value_def
    : ACTION_VALUE
      {
          m_pSetItem = new CSetItem(false);
          if (m_pSetItem != nullptr) {
              m_pSetItem->Set(OBJECT_FIELD_ITEM_ID, grammar::SemanticInt32($1));
          }
          nSetID = grammar::SemanticInt32($1);
      }
    ;

slot_underwear_def_list
    : slot_underwear_def_list slot_underwear_def
    | %empty
    ;

slot_underwear_def
    : SLOT_UNDERWEAR '=' '{' n40 '}'
      {
          m_pSetItem->SetItemSlot(
              static_cast<ObjectFieldType>(OBJECT_FIELD_SETITEM_SLOT_UNDERWEAR),
              grammar::SemanticPointer<std::vector<int>>($4)
          );
      }
    | SLOT_REAR '=' '{' n40 '}'
      {
          m_pSetItem->SetItemSlot(
              static_cast<ObjectFieldType>(OBJECT_FIELD_SETITEM_SLOT_UNDERWEAR + 1),
              grammar::SemanticPointer<std::vector<int>>($4)
          );
      }
    | SLOT_LEAR '=' '{' n40 '}'
      {
          m_pSetItem->SetItemSlot(
              static_cast<ObjectFieldType>(OBJECT_FIELD_SETITEM_SLOT_UNDERWEAR + 2),
              grammar::SemanticPointer<std::vector<int>>($4)
          );
      }
    | SLOT_NECK '=' '{' n40 '}'
      {
          m_pSetItem->SetItemSlot(
              static_cast<ObjectFieldType>(OBJECT_FIELD_SETITEM_SLOT_UNDERWEAR + 3),
              grammar::SemanticPointer<std::vector<int>>($4)
          );
      }
    | SLOT_RFINGER '=' '{' n40 '}'
      {
          m_pSetItem->SetItemSlot(
              static_cast<ObjectFieldType>(OBJECT_FIELD_SETITEM_SLOT_UNDERWEAR + 4),
              grammar::SemanticPointer<std::vector<int>>($4)
          );
      }
    | SLOT_LFINGER '=' '{' n40 '}'
      {
          m_pSetItem->SetItemSlot(
              static_cast<ObjectFieldType>(OBJECT_FIELD_SETITEM_SLOT_UNDERWEAR + 5),
              grammar::SemanticPointer<std::vector<int>>($4)
          );
      }
    | SLOT_HEAD '=' '{' n40 '}'
      {
          m_pSetItem->SetItemSlot(
              static_cast<ObjectFieldType>(OBJECT_FIELD_SETITEM_SLOT_UNDERWEAR + 6),
              grammar::SemanticPointer<std::vector<int>>($4)
          );
      }
    | SLOT_RHAND '=' '{' n40 '}'
      {
          m_pSetItem->SetItemSlot(
              static_cast<ObjectFieldType>(OBJECT_FIELD_SETITEM_SLOT_UNDERWEAR + 7),
              grammar::SemanticPointer<std::vector<int>>($4)
          );
      }
    | SLOT_LHAND '=' '{' n40 '}'
      {
          m_pSetItem->SetItemSlot(
              static_cast<ObjectFieldType>(OBJECT_FIELD_SETITEM_SLOT_UNDERWEAR + 8),
              grammar::SemanticPointer<std::vector<int>>($4)
          );
      }
    | SLOT_GLOVES '=' '{' n40 '}'
      {
          m_pSetItem->SetItemSlot(
              static_cast<ObjectFieldType>(OBJECT_FIELD_SETITEM_SLOT_UNDERWEAR + 9),
              grammar::SemanticPointer<std::vector<int>>($4)
          );
      }
    | SLOT_CHEST '=' '{' n40 '}'
      {
          m_pSetItem->SetItemSlot(
              static_cast<ObjectFieldType>(OBJECT_FIELD_SETITEM_SLOT_UNDERWEAR + 10),
              grammar::SemanticPointer<std::vector<int>>($4)
          );
      }
    | SLOT_LEGS '=' '{' n40 '}'
      {
          m_pSetItem->SetItemSlot(
              static_cast<ObjectFieldType>(OBJECT_FIELD_SETITEM_SLOT_UNDERWEAR + 11),
              grammar::SemanticPointer<std::vector<int>>($4)
          );
      }
    | SLOT_FEET '=' '{' n40 '}'
      {
          m_pSetItem->SetItemSlot(
              static_cast<ObjectFieldType>(OBJECT_FIELD_SETITEM_SLOT_UNDERWEAR + 12),
              grammar::SemanticPointer<std::vector<int>>($4)
          );
      }
    | SLOT_BACK '=' '{' n40 '}'
      {
          m_pSetItem->SetItemSlot(
              static_cast<ObjectFieldType>(OBJECT_FIELD_SETITEM_SLOT_UNDERWEAR + 13),
              grammar::SemanticPointer<std::vector<int>>($4)
          );
      }
    | SLOT_RLHAND '=' '{' n40 '}'
      {
          m_pSetItem->SetItemSlot(
              static_cast<ObjectFieldType>(OBJECT_FIELD_SETITEM_SLOT_UNDERWEAR + 14),
              grammar::SemanticPointer<std::vector<int>>($4)
          );
      }
    | SLOT_ADDITIONAL '=' NAME
      {
          const char16_t* const name = grammar::SemanticPointer<char16_t>($3);
          std::uint32_t slot = 0;
          if (EqualsIgnoreCase(name, u"slot_rear")) {
              slot = 1;
          } else if (EqualsIgnoreCase(name, u"slot_lear")) {
              slot = 2;
          } else if (EqualsIgnoreCase(name, u"slot_neck")) {
              slot = 3;
          } else if (EqualsIgnoreCase(name, u"slot_rfinger")) {
              slot = 4;
          } else if (EqualsIgnoreCase(name, u"slot_lfinger")) {
              slot = 5;
          } else if (EqualsIgnoreCase(name, u"slot_head")) {
              slot = 6;
          } else if (EqualsIgnoreCase(name, u"slot_rhand")) {
              slot = 7;
          } else if (EqualsIgnoreCase(name, u"slot_lhand")) {
              slot = 8;
          } else if (EqualsIgnoreCase(name, u"slot_gloves")) {
              slot = 9;
          } else if (EqualsIgnoreCase(name, u"slot_chest")) {
              slot = 10;
          } else if (EqualsIgnoreCase(name, u"slot_legs")) {
              slot = 11;
          } else if (EqualsIgnoreCase(name, u"slot_feet")) {
              slot = 12;
          } else if (EqualsIgnoreCase(name, u"slot_back")) {
              slot = 13;
          } else if (EqualsIgnoreCase(name, u"slot_rlhand")) {
              slot = 14;
          } else if (!EqualsIgnoreCase(name, u"none") && !EqualsIgnoreCase(name, u"slot_underwear")) {
              break;
          }
          m_pSetItem->SetItemSlot2(OBJECT_FIELD_SETITEM_ADDITIONAL, slot);
      }
    | SET_SKILL '=' NAME
      {
          std::wstring skill = target_utf16::ToHostCodeUnits(grammar::SemanticPointer<char16_t>($3));
          m_pSetItem->SetSkill(
              OBJECT_FIELD_SETITEM_SET_SKILL, skill.data()
          );
      }
    | SET_EFFECT_SKILL '=' NAME
      {
          std::wstring skill = target_utf16::ToHostCodeUnits(grammar::SemanticPointer<char16_t>($3));
          m_pSetItem->SetSkill(
              OBJECT_FIELD_SETITEM_EFFECT_SKILL, skill.data()
          );
      }
    | SET_ADDITIONAL_EFFECT_SKILL '=' NAME
      {
          std::wstring skill = target_utf16::ToHostCodeUnits(grammar::SemanticPointer<char16_t>($3));
          m_pSetItem->SetSkill(OBJECT_FIELD_SETITEM_ADDITIONAL_EFFECT_SKILL, skill.data());
      }
    | SET_ADDITIONAL2_CONDITION '=' ACTION_VALUE
      {
          m_pSetItem->SetItemSlot2(
              OBJECT_FIELD_SETITEM_ADDITIONAL2, static_cast<std::uint32_t>(grammar::SemanticInt32($3))
          );
      }
    | SET_ADDITIONAL2_EFFECT_SKILL '=' NAME
      {
          std::wstring skill = target_utf16::ToHostCodeUnits(grammar::SemanticPointer<char16_t>($3));
          m_pSetItem->SetSkill(OBJECT_FIELD_SETITEM_ADDITIONAL2_EFFECT_SKILL, skill.data());
      }
    | P_DEF_INC_GROUP '=' '{' ACTION_VALUE ';' ACTION_VALUE '}'
      {
          m_pSetItem->SetEffectParam(
              static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), grammar::SemanticInt32($4),
              grammar::SemanticInt32($6)
          );
      }
    | P_DEF_INC_GROUP '=' '{' FLOAT_NUMBER ';' FLOAT_NUMBER '}'
      {
          m_pSetItem->SetEffectParamDouble(
              static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), grammar::SemanticDouble($4),
              grammar::SemanticDouble($6)
          );
      }
    ;

n6
    : item
    ;

item
    : ITEM_BEGIN
      {
          m_bHasMultiSkills = false;
          m_bImmediateItem = false;
          m_bIsDuration = false;
          m_nSkillDistime = 0;
          m_bHaveItemSkill = false;
          m_nPremium = 0;
          m_nEnchantType = 0;
          m_nKeepType = 0;
          m_nCanMove = 0;
          m_bIsTrade = false;
          m_bIsNPCTrade = false;
          m_nAttribution = 0;
          m_bIsDrop = false;
          m_bIsDestruct = false;
          m_bIsPeelAction = false;
          m_nCapsuledItemCount = 0;
          m_bIsPeriodicItem = false;
          m_bIsStackable = false;
          m_bCanOlympiadUse = false;
          m_bCanNotPledgeGameUse = false;
      }
      weapon_group_def default_price_def_list ITEM_END
      {
          if (!m_bNewItem) {
              m_pItem->PostLoad();
          } else {
              g_ItemConstInfoDB.AddItemConstInfo(m_pItem->m_data->m_nClassID, m_pItemConstInfo);
              m_pObjDB->RegisterObject(m_pItem);
          }
      }
    ;

weapon_group_def
    : WEAPON_GROUP ACTION_VALUE NAME
      {
          m_pszDesc = nullptr;
          m_pItem = nullptr;
          m_pItemConstInfo = nullptr;
          m_nItemClassId = grammar::SemanticInt32($2);
          m_bNewItem = true;
          if (m_nItemClassId > 100000) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"MAX_ITEM_CLASS_ID must not be [100000] under. line[%d]",
              //     m_lexer.line + 1)
              // Previous: L2_LOG_ERROR(Log,
              //     L"MAX_ITEM_CLASS_ID must not be [100000] under. line[%d]",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"[object parser] Item class ID exceeds 100000 at line %d; "
                  u"parsing continues with the supplied ID.",
                  m_lexer.line + 1
              );
          }
          if (m_bReload) {
              CObject* const object = m_pObjDB->GetObjectW(m_nItemClassId, false);
              m_pItem = object != nullptr ? object->CastItem() : nullptr;
              m_pItemConstInfo = g_ItemConstInfoDB.GetItemConstInfo(m_nItemClassId);
              if (m_pItem != nullptr) {
                  m_bNewItem = false;
              }
          }
          if (m_pItemConstInfo == nullptr) {
              m_pItemConstInfo = new CItemConstInfo;
          }
          if (m_pItem == nullptr) {
              switch (grammar::SemanticInt32($1)) {
                  case 0:
                      m_pItem = new CWeapon(false);
                      break;
                  case 1:
                      m_pItem = new CAccessary(false);
                      break;
                  case 2:
                      m_pItem = new CArmor(false);
                      break;
                  case 3:
                      m_pItem = new CAsset(false);
                      break;
                  case 4:
                      m_pItem = new CEtcItem(false);
                      break;
                  case 5:
                      m_pItem = new CQuestItem(false);
                      break;
                  default:
                      m_pItem = nullptr;
                      break;
              }
          }
          if (m_pItem != nullptr) {
              m_pItem->Set(OBJECT_FIELD_ITEM_ID, m_nItemClassId);
              std::wstring name = target_utf16::ToHostCodeUnits(grammar::SemanticPointer<char16_t>($3));
              m_pObjDB->RegisterName(m_pItem, name.data());
              m_pItem->m_const->m_bIsPrivateStore = true;
          }
      }
    ;

default_price_def_list
    : default_price_def_list default_price_def
    | %empty
    ;

default_price_def
    : DEFAULT_PRICE '=' ACTION_VALUE
      {
          m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), grammar::SemanticInt64($3));
      }
    | IMMEDIATE_EFFECT '=' ACTION_VALUE
      {
          const auto setItemInt = [&]() {
              m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), grammar::SemanticInt32($3));
          };
          setItemInt();
          if (grammar::SemanticInt32($3) > 0) {
              m_bImmediateItem = true;
          }
      }
    | EX_IMMEDIATE_EFFECT '=' ACTION_VALUE
      {
          const auto setItemInt = [&]() {
              m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), grammar::SemanticInt32($3));
          };
          setItemInt();
          if (grammar::SemanticInt32($3) > 0) {
              m_bImmediateItem = true;
          }
      }
    | ITEM_ID_GROUP '=' WEAPON_GROUP
      {
          static constexpr int kClassMap[] = {0, 2, 1, 4, 5, 3};
          const int source = grammar::SemanticInt32($3);
          const int value = source >= 0 && source < 6 ? kClassMap[source] : -1;
          m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), value);
      }
    | ITEM_SKILL '=' NAME
      {
          const int action = 36;
          char16_t* const targetWord = grammar::SemanticPointer<char16_t>($3);
          std::wstring host = target_utf16::ToHostCodeUnits(targetWord);
          m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), host.data());
          if (!m_bOnlyPrecompiledHeader && !EqualsIgnoreCase(targetWord, u"none")) {
              if (action == 39) {
                  if (g_PrecompiledHeaderDB.Search(targetWord) == -1) {
                      // Log.Add(
                      //     LOG_ERROR,
                      //     L"Undefined category name [%ls] in line [%d]",
                      //     host.c_str(),
                      //     m_lexer.line + 1)
                      // Previous: L2_LOG_ERROR(Log,
                      //     L"Undefined category name [%ls] in line [%d]",
                      //     host.c_str(), m_lexer.line + 1)
                      L2_LOG_ERROR(
                          Log,
                          u"[object parser] Category '%ls' is undefined at line "
                          u"%d; the item keeps the supplied category name.",
                          host.c_str(), m_lexer.line + 1
                      );
                  }
              } else if (g_SkillDB.GetSkillInfoByName(host.c_str()) == nullptr) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined skill name [%ls] in line [%d]",
                  //     host.c_str(),
                  //     m_lexer.line + 1)
                  // Previous: L2_LOG_ERROR(Log,
                  //     L"Undefined skill name [%ls] in line [%d]",
                  //     host.c_str(),
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      u"[object parser] Skill '%ls' is undefined at line %d; "
                      u"the item keeps the supplied skill name.",
                      host.c_str(), m_lexer.line + 1
                  );
              }
              if (!EqualsIgnoreCase(targetWord, u"s_polearm_multi_attack")) {
                  m_bHaveItemSkill = true;
              }
          }
      }
    | CRITICAL_ATTACK_SKILL '=' NAME
      {
          const int action = 37;
          char16_t* const targetWord = grammar::SemanticPointer<char16_t>($3);
          std::wstring host = target_utf16::ToHostCodeUnits(targetWord);
          m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), host.data());
          if (!m_bOnlyPrecompiledHeader && !EqualsIgnoreCase(targetWord, u"none")) {
              if (action == 39) {
                  if (g_PrecompiledHeaderDB.Search(targetWord) == -1) {
                      // Log.Add(
                      //     LOG_ERROR,
                      //     L"Undefined category name [%ls] in line [%d]",
                      //     host.c_str(),
                      //     m_lexer.line + 1)
                      // Previous: L2_LOG_ERROR(Log,
                      //     L"Undefined category name [%ls] in line [%d]",
                      //     host.c_str(), m_lexer.line + 1)
                      L2_LOG_ERROR(
                          Log,
                          u"[object parser] Category '%ls' is undefined at line "
                          u"%d; the item keeps the supplied category name.",
                          host.c_str(), m_lexer.line + 1
                      );
                  }
              } else if (g_SkillDB.GetSkillInfoByName(host.c_str()) == nullptr) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined skill name [%ls] in line [%d]",
                  //     host.c_str(),
                  //     m_lexer.line + 1)
                  // Previous: L2_LOG_ERROR(Log,
                  //     L"Undefined skill name [%ls] in line [%d]",
                  //     host.c_str(),
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      u"[object parser] Skill '%ls' is undefined at line %d; "
                      u"the item keeps the supplied skill name.",
                      host.c_str(), m_lexer.line + 1
                  );
              }
          }
      }
    | ATTACK_SKILL '=' NAME
      {
          const int action = 38;
          char16_t* const targetWord = grammar::SemanticPointer<char16_t>($3);
          std::wstring host = target_utf16::ToHostCodeUnits(targetWord);
          m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), host.data());
          if (!m_bOnlyPrecompiledHeader && !EqualsIgnoreCase(targetWord, u"none")) {
              if (action == 39) {
                  if (g_PrecompiledHeaderDB.Search(targetWord) == -1) {
                      // Log.Add(
                      //     LOG_ERROR,
                      //     L"Undefined category name [%ls] in line [%d]",
                      //     host.c_str(),
                      //     m_lexer.line + 1)
                      // Previous: L2_LOG_ERROR(Log,
                      //     L"Undefined category name [%ls] in line [%d]",
                      //     host.c_str(), m_lexer.line + 1)
                      L2_LOG_ERROR(
                          Log,
                          u"[object parser] Category '%ls' is undefined at line "
                          u"%d; the item keeps the supplied category name.",
                          host.c_str(), m_lexer.line + 1
                      );
                  }
              } else if (g_SkillDB.GetSkillInfoByName(host.c_str()) == nullptr) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined skill name [%ls] in line [%d]",
                  //     host.c_str(),
                  //     m_lexer.line + 1)
                  // Previous: L2_LOG_ERROR(Log,
                  //     L"Undefined skill name [%ls] in line [%d]",
                  //     host.c_str(),
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      u"[object parser] Skill '%ls' is undefined at line %d; "
                      u"the item keeps the supplied skill name.",
                      host.c_str(), m_lexer.line + 1
                  );
              }
          }
      }
    | T_10075 '=' NAME
      {
          const int action = 39;
          char16_t* const targetWord = grammar::SemanticPointer<char16_t>($3);
          std::wstring host = target_utf16::ToHostCodeUnits(targetWord);
          m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), host.data());
          if (!m_bOnlyPrecompiledHeader && !EqualsIgnoreCase(targetWord, u"none")) {
              if (action == 39) {
                  if (g_PrecompiledHeaderDB.Search(targetWord) == -1) {
                      // Log.Add(
                      //     LOG_ERROR,
                      //     L"Undefined category name [%ls] in line [%d]",
                      //     host.c_str(),
                      //     m_lexer.line + 1)
                      // Previous: L2_LOG_ERROR(Log,
                      //     L"Undefined category name [%ls] in line [%d]",
                      //     host.c_str(), m_lexer.line + 1)
                      L2_LOG_ERROR(
                          Log,
                          u"[object parser] Category '%ls' is undefined at line "
                          u"%d; the item keeps the supplied category name.",
                          host.c_str(), m_lexer.line + 1
                      );
                  }
              } else if (g_SkillDB.GetSkillInfoByName(host.c_str()) == nullptr) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined skill name [%ls] in line [%d]",
                  //     host.c_str(),
                  //     m_lexer.line + 1)
                  // Previous: L2_LOG_ERROR(Log,
                  //     L"Undefined skill name [%ls] in line [%d]",
                  //     host.c_str(),
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      u"[object parser] Skill '%ls' is undefined at line %d; "
                      u"the item keeps the supplied skill name.",
                      host.c_str(), m_lexer.line + 1
                  );
              }
          }
      }
    | ITEM_ID_GROUP '=' NAME
      {
          const int action = 40;
          char16_t* const targetWord = grammar::SemanticPointer<char16_t>($3);
          std::wstring host = target_utf16::ToHostCodeUnits(targetWord);
          m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), host.data());
          if (!m_bOnlyPrecompiledHeader && !EqualsIgnoreCase(targetWord, u"none")) {
              if (action == 39) {
                  if (g_PrecompiledHeaderDB.Search(targetWord) == -1) {
                      // Log.Add(
                      //     LOG_ERROR,
                      //     L"Undefined category name [%ls] in line [%d]",
                      //     host.c_str(),
                      //     m_lexer.line + 1)
                      // Previous: L2_LOG_ERROR(Log,
                      //     L"Undefined category name [%ls] in line [%d]",
                      //     host.c_str(), m_lexer.line + 1)
                      L2_LOG_ERROR(
                          Log,
                          u"[object parser] Category '%ls' is undefined at line "
                          u"%d; the item keeps the supplied category name.",
                          host.c_str(), m_lexer.line + 1
                      );
                  }
              } else if (action != 40 && g_SkillDB.GetSkillInfoByName(host.c_str()) == nullptr) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined skill name [%ls] in line [%d]",
                  //     host.c_str(),
                  //     m_lexer.line + 1)
                  // Previous: L2_LOG_ERROR(Log,
                  //     L"Undefined skill name [%ls] in line [%d]",
                  //     host.c_str(),
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      u"[object parser] Skill '%ls' is undefined at line %d; "
                      u"the item keeps the supplied skill name.",
                      host.c_str(), m_lexer.line + 1
                  );
              }
          }
          std::free(targetWord);
      }
    | MAGIC_SKILL '=' NAME
      {
          char16_t* const targetWord = grammar::SemanticPointer<char16_t>($3);
          std::wstring host = target_utf16::ToHostCodeUnits(targetWord);
          const int probability = 0;
          m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), host.data(), probability);
          if (!EqualsIgnoreCase(targetWord, u"none")) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Magic skill must have percentage in line [%d]",
              //     m_lexer.line + 1)
              // Previous: L2_LOG_ERROR(Log,
              //     L"Magic skill must have percentage in line [%d]",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"[object parser] Magic skill at line %d has no activation "
                  u"percentage; zero is retained.",
                  m_lexer.line + 1
              );
          }
          std::free(targetWord);
      }
    | MAGIC_SKILL '=' NAME ';' ACTION_VALUE
      {
          char16_t* const targetWord = grammar::SemanticPointer<char16_t>($3);
          std::wstring host = target_utf16::ToHostCodeUnits(targetWord);
          const int probability = grammar::SemanticInt32($5);
          m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), host.data(), probability);
          if (!m_bOnlyPrecompiledHeader && !EqualsIgnoreCase(targetWord, u"none") &&
              g_SkillDB.GetSkillInfoByName(host.c_str()) == nullptr) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined skill name [%ls] in line [%d]",
              //     host.c_str(),
              //     m_lexer.line + 1)
              // Previous: L2_LOG_ERROR(Log,
              //     L"Undefined skill name [%ls] in line [%d]",
              //     host.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"[object parser] Skill '%ls' is undefined at line %d; "
                  u"the item keeps the supplied skill name.",
                  host.c_str(), m_lexer.line + 1
              );
          }
          std::free(targetWord);
      }
    | ITEM_ID_GROUP '=' NAME ';' ACTION_VALUE
      {
          char16_t* const targetWord = grammar::SemanticPointer<char16_t>($3);
          std::wstring host = target_utf16::ToHostCodeUnits(targetWord);
          const int probability = grammar::SemanticInt32($5);
          m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), host.data(), probability);
          std::free(targetWord);
      }
    | ITEM_ID_GROUP '=' IDENTIFIER
      {
          const ObjectFieldType field = static_cast<ObjectFieldType>(grammar::SemanticInt32($1));
          const std::int64_t value = FindInTable(field, grammar::SemanticPointer<char16_t>($3));
          if (value == -1) {
              const std::wstring host = target_utf16::ToHostCodeUnits(grammar::SemanticPointer<char16_t>($3));
              // Log.Add(LOG_ERROR, L"Undefined id. id=word[%ls]", host.c_str())
              // Previous: L2_LOG_ERROR(Log,
              //     L"Undefined id. id=word[%ls]", host.c_str())
              L2_LOG_ERROR(
                  Log,
                  u"[object parser] Value '%ls' is undefined for this item "
                  u"field; the field is not assigned.",
                  host.c_str()
              );
              break;
          }
          // Current yyaction@00c8c490 action 44 narrows FindInTable's
          // 64-bit carrier to int and calls virtual slot +0x5b0. Calling
          // the adjacent int64 slot changes enum fields into CItem::Set(double)
          // fallbacks and is observably wrong for armor/material/weapon/etc.
          m_pItem->Set(field, static_cast<int>(value));
          if (field == OBJECT_FIELD_CONSUME_TYPE && (value == 2 || value == 3)) {
              m_bIsStackable = true;
          }
      }
    | USE_SKILL_DISTIME '=' ACTION_VALUE
      {
          const auto setItemInt = [&]() {
              m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), grammar::SemanticInt32($3));
          };
          setItemInt();
          m_nSkillDistime = grammar::SemanticInt32($3);
      }
    | ITEM_ID_GROUP '=' ACTION_VALUE
      {
          const auto setItemInt = [&]() {
              m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), grammar::SemanticInt32($3));
          };
          setItemInt();
      }
    | ITEM_ID_GROUP '=' FLOAT_NUMBER
      {
          m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), grammar::SemanticDouble($3));
      }
    | DURATION '=' ACTION_VALUE
      {
          const auto setItemInt = [&]() {
              m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), grammar::SemanticInt32($3));
          };
          if (grammar::SemanticInt32($3) > 0) {
              m_bIsDuration = true;
          }
          setItemInt();
      }
    | DURATION '=' '{' n40 '}'
      {
          auto* const values = grammar::SemanticPointer<std::vector<int>>($4);
          if (values != nullptr && !values->empty() && values->front() > 0) {
              m_bIsDuration = true;
          }
          m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), values);
      }
    | BASE_ATTRIBUTE_DEFEND '=' '{' n40 '}'
      {
          auto* const values = grammar::SemanticPointer<std::vector<int>>($4);
          if (values != nullptr && values->size() != 6) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"line[%d] - base_attribute_defend must have 6 params.",
              //     m_lexer.line)
              // Previous: L2_LOG_ERROR(Log,
              //     L"line[%d] - base_attribute_defend must have 6 params.",
              //     m_lexer.line)
              L2_LOG_ERROR(
                  Log,
                  u"[object parser] base_attribute_defend has an invalid "
                  u"parameter count at line %d; expected 6. The supplied list "
                  u"is still assigned.",
                  m_lexer.line
              );
          }
          m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), values);
      }
    | SLOT_BIT_TYPE '=' '{' n42 '}'
      {
          auto* const words = grammar::SemanticPointer<std::vector<char16_t*>>($4);
          std::vector<std::int64_t> values =
              ConvertStrList64(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), *words);
          m_pItem->Set2(&values);
          for (int index = 0; index < static_cast<int>(words->size()); ++index) {
              std::free(runtime::CheckedVectorAt((*words), index));
          }
          delete words;
      }
    | ITEM_ID_GROUP '=' '{' n40 '}'
      {
          m_pItem->Set(
              static_cast<ObjectFieldType>(grammar::SemanticInt32($1)),
              grammar::SemanticPointer<std::vector<int>>($4)
          );
      }
    | ITEM_ID_GROUP '=' '{' '}'
    | CATEGORY '=' '{' n41 '}'
    | CATEGORY '=' '{' '}'
    | DEFAULT_ACTION '=' ACTION_VALUE
      {
          m_pItem->SetDefaultAction(static_cast<ItemDefaultActionEnum>(grammar::SemanticInt32($3)));
          if (grammar::SemanticInt32($3) == ITEM_ACTION_PEEL) {
              m_bIsPeelAction = true;
          }
      }
    | RECIPE_ID '=' ACTION_VALUE
      { m_pItem->SetRecipeId(grammar::SemanticInt32($3)); }
    | ITEM_MULTI_SKILL_LIST '=' '{' n42 '}'
      {
          const auto stageSkillList = [this](std::vector<char16_t*>* names, std::vector<CSkillKey> CItemTempData::* member) {
              if (names == nullptr) {
                  return;
              }
              if (!m_bOnlyPrecompiledHeader) {
                  // Current yyaction@0x00c8c490 actions 58/59/60 read the staging
                  // key from CObject +0x18 on the concrete item, not from the
                  // parser's separate class-id carrier at +0x1e8.
                  CItemTempData& staged = g_ItemTempDataDB[static_cast<int>(m_pItem->m_nId)];
                  for (char16_t* name : *names) {
                      const CSkillKey key = ResolveSkillKey(name);
                      if (key.IsValid()) {
                          (staged.*member).push_back(key);
                      } else {
                          const std::wstring host = target_utf16::ToHostCodeUnits(name);
                          // Log.Add(
                          //     LOG_ERROR,
                          //     L"Undefined skill name [%ls] in line[%d]",
                          //     host.c_str(),
                          //     m_lexer.line + 1)
                          // Previous: L2_LOG_ERROR(Log,
                          //     L"Undefined skill name [%ls] in line[%d]",
                          //     host.c_str(),
                          //     m_lexer.line + 1)
                          L2_LOG_ERROR(
                              Log,
                              u"[object parser] Skill '%ls' is undefined at line %d; "
                              u"this skill entry is omitted.",
                              host.c_str(), m_lexer.line + 1
                          );
                      }
                  }
              }
              for (char16_t* name : *names) {
                  std::free(name);
              }
              delete names;
          };
          m_bHasMultiSkills = grammar::SemanticPointer<std::vector<char16_t*>>($4) != nullptr &&
              !grammar::SemanticPointer<std::vector<char16_t*>>($4)->empty();
          stageSkillList(grammar::SemanticPointer<std::vector<char16_t*>>($4), &CItemTempData::m_ItemSkillList);
      }
    | UNEQUIP_SKILL '=' '{' n42 '}'
      {
          const auto stageSkillList = [this](std::vector<char16_t*>* names, std::vector<CSkillKey> CItemTempData::* member) {
              if (names == nullptr) {
                  return;
              }
              if (!m_bOnlyPrecompiledHeader) {
                  // Current yyaction@0x00c8c490 actions 58/59/60 read the staging
                  // key from CObject +0x18 on the concrete item, not from the
                  // parser's separate class-id carrier at +0x1e8.
                  CItemTempData& staged = g_ItemTempDataDB[static_cast<int>(m_pItem->m_nId)];
                  for (char16_t* name : *names) {
                      const CSkillKey key = ResolveSkillKey(name);
                      if (key.IsValid()) {
                          (staged.*member).push_back(key);
                      } else {
                          const std::wstring host = target_utf16::ToHostCodeUnits(name);
                          // Log.Add(
                          //     LOG_ERROR,
                          //     L"Undefined skill name [%ls] in line[%d]",
                          //     host.c_str(),
                          //     m_lexer.line + 1)
                          // Previous: L2_LOG_ERROR(Log,
                          //     L"Undefined skill name [%ls] in line[%d]",
                          //     host.c_str(),
                          //     m_lexer.line + 1)
                          L2_LOG_ERROR(
                              Log,
                              u"[object parser] Skill '%ls' is undefined at line %d; "
                              u"this skill entry is omitted.",
                              host.c_str(), m_lexer.line + 1
                          );
                      }
                  }
              }
              for (char16_t* name : *names) {
                  std::free(name);
              }
              delete names;
          };
          stageSkillList(
              grammar::SemanticPointer<std::vector<char16_t*>>($4), &CItemTempData::m_ItemUnequipSkillList
          );
      }
    | EQUIP_SKILL '=' '{' n42 '}'
      {
          const auto stageSkillList = [this](std::vector<char16_t*>* names, std::vector<CSkillKey> CItemTempData::* member) {
              if (names == nullptr) {
                  return;
              }
              if (!m_bOnlyPrecompiledHeader) {
                  // Current yyaction@0x00c8c490 actions 58/59/60 read the staging
                  // key from CObject +0x18 on the concrete item, not from the
                  // parser's separate class-id carrier at +0x1e8.
                  CItemTempData& staged = g_ItemTempDataDB[static_cast<int>(m_pItem->m_nId)];
                  for (char16_t* name : *names) {
                      const CSkillKey key = ResolveSkillKey(name);
                      if (key.IsValid()) {
                          (staged.*member).push_back(key);
                      } else {
                          const std::wstring host = target_utf16::ToHostCodeUnits(name);
                          // Log.Add(
                          //     LOG_ERROR,
                          //     L"Undefined skill name [%ls] in line[%d]",
                          //     host.c_str(),
                          //     m_lexer.line + 1)
                          // Previous: L2_LOG_ERROR(Log,
                          //     L"Undefined skill name [%ls] in line[%d]",
                          //     host.c_str(),
                          //     m_lexer.line + 1)
                          L2_LOG_ERROR(
                              Log,
                              u"[object parser] Skill '%ls' is undefined at line %d; "
                              u"this skill entry is omitted.",
                              host.c_str(), m_lexer.line + 1
                          );
                      }
                  }
              }
              for (char16_t* name : *names) {
                  std::free(name);
              }
              delete names;
          };
          stageSkillList(
              grammar::SemanticPointer<std::vector<char16_t*>>($4), &CItemTempData::m_ItemEquipSkillList
          );
      }
    | ITEM_ID_GROUP '=' '{' n42 '}'
      {
          auto* const words = grammar::SemanticPointer<std::vector<char16_t*>>($4);
          std::vector<int> values = ConvertStrList32(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), *words);
          m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), &values);
          for (int index = 0; index < static_cast<int>(words->size()); ++index) {
              std::free(runtime::CheckedVectorAt((*words), index));
          }
          delete words;
      }
    | ITEM_ID_GROUP '=' '{' n43 '}'
      {
          m_pItem->Set(
              static_cast<ObjectFieldType>(grammar::SemanticInt32($1)),
              grammar::SemanticPointer<std::vector<int>>($4)
          );
      }
    | ITEM_EQUIP_OPTION '=' '{' n37 '}'
    | USE_CONDITION '=' '{' n33 '}'
    | EQUIP_CONDITION '=' '{' n35 '}'
    | IS_PREMIUM '=' ACTION_VALUE
      {
          const auto setItemInt = [&]() {
              m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), grammar::SemanticInt32($3));
          };
          setItemInt();
          m_nPremium = grammar::SemanticInt32($3);
      }
    | IS_CHANCE_AFFECTED '=' ACTION_VALUE
      {
          const auto setItemInt = [&]() {
              m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), grammar::SemanticInt32($3));
          };
          setItemInt();
      }
    | ENCHANT_ENABLE '=' ACTION_VALUE
      {
          const auto setItemInt = [&]() {
              m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), grammar::SemanticInt32($3));
          };
          m_nEnchantType = grammar::SemanticInt32($3);
          setItemInt();
      }
    | KEEP_TYPE '=' ACTION_VALUE
      {
          const auto setItemInt = [&]() {
              m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), grammar::SemanticInt32($3));
          };
          setItemInt();
          m_nKeepType = grammar::SemanticInt32($3);
      }
    | CAN_MOVE '=' ACTION_VALUE
      { m_nCanMove = grammar::SemanticInt32($3); }
    | IS_TRADE '=' ACTION_VALUE
      {
          const auto setItemInt = [&]() {
              m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), grammar::SemanticInt32($3));
          };
          setItemInt();
          m_bIsTrade = grammar::SemanticInt32($3) == 1;
      }
    | IS_NPCTRADE '=' ACTION_VALUE
      {
          const auto setItemInt = [&]() {
              m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), grammar::SemanticInt32($3));
          };
          setItemInt();
          m_bIsNPCTrade = grammar::SemanticInt32($3) == 1;
      }
    | IS_ATTRIBUTION '=' ACTION_VALUE
      {
          const auto setItemInt = [&]() {
              m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), grammar::SemanticInt32($3));
          };
          setItemInt();
          m_nAttribution = grammar::SemanticInt32($3);
      }
    | ATTR_GROUPID '=' ACTION_VALUE
      {
          const auto setItemInt = [&]() {
              m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), grammar::SemanticInt32($3));
          };
          setItemInt();
      }
    | IS_LOCKED '=' ACTION_VALUE
      {
          const auto setItemInt = [&]() {
              m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), grammar::SemanticInt32($3));
          };
          if (m_bIsPeriodicItem || m_bIsDuration) {
              // Log.Add(
              //     LOG_ERROR,
              //     m_bIsPeriodicItem
              //         ? L"PeriodicItem don't set Locked in line [%d]"
              //         : L"DurationItem don't set Locked in line [%d]",
              //     m_lexer.line + 1)
              // Previous: L2_LOG_ERROR(Log,
              //     m_bIsPeriodicItem
              //         ? L"PeriodicItem don't set Locked in line [%d]"
              //         : L"DurationItem don't set Locked in line [%d]",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  m_bIsPeriodicItem ? L"[object parser] Periodic item sets Locked at line %d; "
                                      L"the supplied value is still assigned."
                                    : L"[object parser] Duration item sets Locked at line %d; "
                                      L"the supplied value is still assigned.",
                  m_lexer.line + 1
              );
          }
          setItemInt();
      }
    | IS_PRIVATE_STORE '=' ACTION_VALUE
      { m_pItem->m_const->m_bIsPrivateStore = grammar::SemanticInt32($3) != 0; }
    | IS_DROP '=' ACTION_VALUE
      {
          const auto setItemInt = [&]() {
              m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), grammar::SemanticInt32($3));
          };
          setItemInt();
          m_bIsDrop = grammar::SemanticInt32($3) == 1;
      }
    | IS_DESTRUCT '=' ACTION_VALUE
      {
          const auto setItemInt = [&]() {
              m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), grammar::SemanticInt32($3));
          };
          setItemInt();
          m_bIsDestruct = grammar::SemanticInt32($3) == 1;
      }
    | CAPSULED_ITEMS '=' '{' n12 '}'
      {
          auto* const items = grammar::SemanticPointer<std::vector<TempCapsuledItem*>>($4);
          if (items != nullptr) {
              g_CapsuledItemTempDataDB[m_nItemClassId] = items;
          }
      }
    | PERIOD '=' ACTION_VALUE
      {
          const auto setItemInt = [&]() {
              m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), grammar::SemanticInt32($3));
          };
          if (grammar::SemanticInt32($3) > 0) {
              m_bIsPeriodicItem = true;
          }
          setItemInt();
      }
    | CRYSTAL_TYPE '=' IDENTIFIER
      {
          std::wstring host = target_utf16::ToHostCodeUnits(grammar::SemanticPointer<char16_t>($3));
          CrystalType type = CItem::GetCrysalType(host.c_str());
          if (type == CRT_INVALID) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined crystal_type[%ls] in line [%d]",
              //     host.c_str(),
              //     m_lexer.line + 1)
              // Previous: L2_LOG_ERROR(Log,
              //     L"Undefined crystal_type[%ls] in line [%d]",
              //     host.c_str(), m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"[object parser] Crystal type '%ls' is undefined at line %d; "
                  u"CRT_NONE is used.",
                  host.c_str(), m_lexer.line + 1
              );
              type = CRT_NONE;
          }
          m_pItem->m_const->SetCrystalType(type);
      }
    | IS_OLYMPIAD_CAN_USE '=' ACTION_VALUE
      {
          const auto setItemInt = [&]() {
              m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), grammar::SemanticInt32($3));
          };
          setItemInt();
          m_bCanOlympiadUse = grammar::SemanticInt32($3) == 1;
      }
    | IS_PLEDGEGAME_CAN_USE '=' ACTION_VALUE
      {
          const auto setItemInt = [&]() {
              m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), grammar::SemanticInt32($3));
          };
          setItemInt();
          m_bCanNotPledgeGameUse = grammar::SemanticInt32($3) == 0;
      }
    | IS_HERO_ENTRY '=' ACTION_VALUE
      {
          const auto setItemInt = [&]() {
              m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), grammar::SemanticInt32($3));
          };
          setItemInt();
      }
    | IS_HP_LIMIT_BREAK '=' ACTION_VALUE
      {
          const auto setItemInt = [&]() {
              m_pItem->Set(static_cast<ObjectFieldType>(grammar::SemanticInt32($1)), grammar::SemanticInt32($3));
          };
          setItemInt();
      }
    | CREATE_ITEMS '=' '{' n26 '}'
    | CREATE_ITEM_GROUPS '=' '{' n28 '}'
      {
          m_pItemConstInfo->SetCreateItemRandomGroup(m_itemRandomListGroup);
          m_itemRandomListGroup.clear();
          m_itemRandomList.clear();
      }
    | CRYSTAL_ITEM '=' '{' n14 '}'
    | VALIDATE_PARAMS '=' '{' n31 '}'
    | PROPERTY_PARAMS '=' '{' n16 '}'
    | AUCTION_ITEMTYPE '=' IDENTIFIER
      {
          const std::wstring host = target_utf16::ToHostCodeUnits(grammar::SemanticPointer<char16_t>($3));
          Commission::Enum type = CItem::GetCommissionItemType(host.c_str());
          if (type == Commission::Invalid) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined Auction_ItemType[%ls] in line [%d]",
              //     host.c_str(),
              //     m_lexer.line + 1)
              // Previous: L2_LOG_ERROR(Log,
              //     L"Undefined Auction_ItemType[%ls] in line [%d]",
              //     host.c_str(), m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"[object parser] Auction item type '%ls' is undefined at "
                  u"line %d; Commission::None is used.",
                  host.c_str(), m_lexer.line + 1
              );
              type = Commission::None;
          }
          m_pItem->m_const->SetCommissionItemType(type);
      }
    | GENERAL_ITEMTYPE '=' ACTION_VALUE
      {
          if (!m_pItem->m_const->SetGeneralItemType(grammar::SemanticInt32($3))) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined General_ItemType[%d] in line [%d]",
              //     grammar::SemanticInt32($3),
              //     m_lexer.line + 1)
              // Previous: L2_LOG_ERROR(Log,
              //     L"Undefined General_ItemType[%d] in line [%d]",
              //     grammar::SemanticInt32($3), m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"[object parser] General item type %d is undefined at line "
                  u"%d; SetGeneralItemType rejected it.",
                  grammar::SemanticInt32($3), m_lexer.line + 1
              );
          }
      }
    | SHAPE_SHIFTABLE '=' '{' action_value_def_3 '}'
    | ITEM_OPTION '=' '{' ACTION_VALUE ';' ACTION_VALUE ';' ACTION_VALUE ';' ACTION_VALUE ';' ACTION_VALUE '}'
      {
          m_pItem->Set(OBJECT_FIELD_IS_TRADE, grammar::SemanticInt32($4));
          m_bIsTrade = grammar::SemanticInt32($4) == 1;
          m_pItem->Set(OBJECT_FIELD_IS_DROP, grammar::SemanticInt32($6));
          m_bIsDrop = grammar::SemanticInt32($6) == 1;
          m_pItem->Set(OBJECT_FIELD_IS_DESTRUCT, grammar::SemanticInt32($8));
          m_bIsDestruct = grammar::SemanticInt32($8) == 1;
          m_pItem->m_const->m_bIsPrivateStore = grammar::SemanticInt32($10) != 0;
          m_pItem->Set(OBJECT_FIELD_KEEP_TYPE, grammar::SemanticInt32($12));
          m_nKeepType = grammar::SemanticInt32($12);
      }
    | ITEM_OPTION '=' '{' n18 '}'
    | DEFAULT_PRICE '=' '{' n20 '}'
    | AUCTION_ITEMTYPE '=' '{' n22 '}'
    | IS_NPCTRADE '=' '{' n24 '}'
    ;

n12
    : n12 ';' n13
      {
          auto* const items = grammar::SemanticPointer<std::vector<TempCapsuledItem*>>($1);
          items->push_back(m_pCapsuledItem);
          $$ = grammar::PointerSemantic(items);
          ++m_nCapsuledItemCount;
      }
    | n13
      {
          auto* const items = new std::vector<TempCapsuledItem*>;
          items->push_back(m_pCapsuledItem);
          $$ = grammar::PointerSemantic(items);
          ++m_nCapsuledItemCount;
      }
    | %empty
      {
          $$ = grammar::PointerSemantic(new std::vector<TempCapsuledItem*>);
      }
    ;

n13
    : '{' NAME ';' ACTION_VALUE ';' ACTION_VALUE ';' FLOAT_NUMBER '}'
      {
          m_pCapsuledItem = new TempCapsuledItem;
          m_pCapsuledItem->m_ItemName.assign(std::u16string_view(grammar::SemanticPointer<char16_t>($2)));
          m_pCapsuledItem->nMin = grammar::SemanticInt64($4);
          m_pCapsuledItem->nMax = grammar::SemanticInt64($6);
          m_pCapsuledItem->nProb = grammar::SemanticDouble($8);
          if (!m_bOnlyPrecompiledHeader && g_PrecompiledHeaderDB.Search(grammar::SemanticPointer<char16_t>($2)) == -1) {
              const std::wstring host = target_utf16::ToHostCodeUnits(grammar::SemanticPointer<char16_t>($2));
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined item name[%ls] in line [%d]",
              //     host.c_str(),
              //     m_lexer.line + 1)
              // Previous: L2_LOG_ERROR(Log,
              //     L"Undefined item name[%ls] in line [%d]",
              //     host.c_str(), m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"[object parser] Capsule item '%ls' is undefined at line %d; "
                  u"the supplied name is retained.",
                  host.c_str(), m_lexer.line + 1
              );
          }
      }
    | '{' NAME ';' ACTION_VALUE ';' ACTION_VALUE ';' ACTION_VALUE '}'
      {
          m_pCapsuledItem = new TempCapsuledItem;
          m_pCapsuledItem->m_ItemName.assign(std::u16string_view(grammar::SemanticPointer<char16_t>($2)));
          m_pCapsuledItem->nMin = grammar::SemanticInt64($4);
          m_pCapsuledItem->nMax = grammar::SemanticInt64($6);
          m_pCapsuledItem->nProb = static_cast<double>(grammar::SemanticInt64($8));
          if (!m_bOnlyPrecompiledHeader && g_PrecompiledHeaderDB.Search(grammar::SemanticPointer<char16_t>($2)) == -1) {
              const std::wstring host = target_utf16::ToHostCodeUnits(grammar::SemanticPointer<char16_t>($2));
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined item name[%ls] in line [%d]",
              //     host.c_str(),
              //     m_lexer.line + 1)
              // Previous: L2_LOG_ERROR(Log,
              //     L"Undefined item name[%ls] in line [%d]",
              //     host.c_str(), m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"[object parser] Capsule item '%ls' is undefined at line %d; "
                  u"the supplied name is retained.",
                  host.c_str(), m_lexer.line + 1
              );
          }
      }
    ;

n14
    : n14 ';' '{' n15 '}'
    | '{' n15 '}'
    | %empty
    ;

n15
    : action_value_def_2 ';' action_value_def_2 ';' action_value_def_2
      {
          m_pItemConstInfo->SetCrystalItem(
              static_cast<int>(grammar::SemanticDouble($1)), static_cast<int>(grammar::SemanticDouble($3)),
              grammar::SemanticDouble($5)
          );
      }
    ;

n16
    : n16 ';' '{' identifier_def '}'
    | '{' identifier_def '}'
    | %empty
    ;

identifier_def
    : IDENTIFIER
      {
          const std::wstring host = target_utf16::ToHostCodeUnits(grammar::SemanticPointer<char16_t>($1));
          m_pItemConstInfo->SetItemProperty(CItem::GetValidateType(host.c_str()));
      }
    ;

n18
    : n18 ';' '{' identifier_def_2 '}'
    | '{' identifier_def_2 '}'
    | %empty
    ;

identifier_def_2
    : IDENTIFIER ';' ACTION_VALUE ';' ACTION_VALUE ';' ACTION_VALUE ';' ACTION_VALUE ';' ACTION_VALUE
      {
          if (AppliesToCurrentCountry(grammar::SemanticPointer<char16_t>($1), m_lexer.line + 1)) {
              m_pItem->Set(OBJECT_FIELD_IS_TRADE, grammar::SemanticInt32($3));
              m_bIsTrade = grammar::SemanticInt32($3) == 1;
              m_pItem->Set(OBJECT_FIELD_IS_DROP, grammar::SemanticInt32($5));
              m_bIsDrop = grammar::SemanticInt32($5) == 1;
              m_pItem->Set(OBJECT_FIELD_IS_DESTRUCT, grammar::SemanticInt32($7));
              m_bIsDestruct = grammar::SemanticInt32($7) == 1;
              m_pItem->m_const->m_bIsPrivateStore = grammar::SemanticInt32($9) != 0;
              m_pItem->Set(OBJECT_FIELD_KEEP_TYPE, grammar::SemanticInt32($11));
              m_nKeepType = grammar::SemanticInt32($11);
          }
      }
    ;

n20
    : n20 ';' '{' identifier_def_3 '}'
    | '{' identifier_def_3 '}'
    | %empty
    ;

identifier_def_3
    : IDENTIFIER ';' ACTION_VALUE
      {
          if (AppliesToCurrentCountry(grammar::SemanticPointer<char16_t>($1), m_lexer.line + 1)) {
              m_pItem->Set(OBJECT_FIELD_DEFAULT_PRICE, grammar::SemanticInt64($3));
          }
      }
    ;

n22
    : n22 ';' '{' identifier_def_4 '}'
    | '{' identifier_def_4 '}'
    | %empty
    ;

identifier_def_4
    : IDENTIFIER ';' IDENTIFIER
      {
          if (AppliesToCurrentCountry(grammar::SemanticPointer<char16_t>($1), m_lexer.line + 1)) {
              const std::wstring host = target_utf16::ToHostCodeUnits(grammar::SemanticPointer<char16_t>($3));
              Commission::Enum type = CItem::GetCommissionItemType(host.c_str());
              if (type == Commission::Invalid) {
                  type = Commission::None;
              }
              m_pItem->m_const->SetCommissionItemType(type);
          }
      }
    ;

n24
    : n24 ';' '{' identifier_def_5 '}'
    | '{' identifier_def_5 '}'
    | %empty
    ;

identifier_def_5
    : IDENTIFIER ';' ACTION_VALUE
      {
          if (AppliesToCurrentCountry(grammar::SemanticPointer<char16_t>($1), m_lexer.line + 1)) {
              m_pItem->Set(OBJECT_FIELD_IS_NPCTRADE, grammar::SemanticInt32($3));
              m_bIsNPCTrade = grammar::SemanticInt32($3) == 1;
          }
      }
    ;

n26
    : n26 ';' '{' name_def '}'
    | '{' name_def '}'
    | %empty
    ;

name_def
    : NAME ';' ACTION_VALUE ';' action_value_def_2
      {
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          const int enchant = 0;
          const int itemId = ResolveItemClassId(grammar::SemanticPointer<char16_t>($1), false, m_lexer.line + 1);
          const double probability = grammar::SemanticDouble($5);
          if (probability != -1.0) {
              m_pItemConstInfo->SetCreateItemRandom(itemId, grammar::SemanticInt64($3), probability, enchant);
          } else {
              m_pItemConstInfo->SetCreateItem(itemId, grammar::SemanticInt64($3), enchant);
          }
      }
    | NAME ';' ACTION_VALUE ';' action_value_def_2 ';' ACTION_VALUE
      {
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          const int enchant = grammar::SemanticInt32($7);
          const int itemId = ResolveItemClassId(grammar::SemanticPointer<char16_t>($1), false, m_lexer.line + 1);
          const double probability = grammar::SemanticDouble($5);
          if (probability != -1.0) {
              m_pItemConstInfo->SetCreateItemRandom(itemId, grammar::SemanticInt64($3), probability, enchant);
          } else {
              m_pItemConstInfo->SetCreateItem(itemId, grammar::SemanticInt64($3), enchant);
          }
      }
    ;

n28
    : n28 ';' '{' n29 '}'
      {
          m_itemRandomListGroup.push_back(m_itemRandomList);
          m_itemRandomList.clear();
      }
    | '{' n29 '}'
      {
          m_itemRandomListGroup.push_back(m_itemRandomList);
          m_itemRandomList.clear();
      }
    | %empty
    ;

n29
    : n29 ';' '{' name_def_2 '}'
    | '{' name_def_2 '}'
    | %empty
    ;

name_def_2
    : NAME ';' ACTION_VALUE ';' action_value_def_2
      {
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          const int itemId = ResolveItemClassId(grammar::SemanticPointer<char16_t>($1), false, m_lexer.line + 1);
          m_itemRandomList.push_back(
              CreateItemRandomStruct{
                  itemId, grammar::SemanticInt64($3), grammar::SemanticDouble($5), 0}
          );
      }
    | NAME ';' ACTION_VALUE ';' action_value_def_2 ';' ACTION_VALUE
      {
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          const int itemId = ResolveItemClassId(grammar::SemanticPointer<char16_t>($1), false, m_lexer.line + 1);
          m_itemRandomList.push_back(
              CreateItemRandomStruct{
                  itemId, grammar::SemanticInt64($3), grammar::SemanticDouble($5), grammar::SemanticInt32($7)}
          );
      }
    ;

n31
    : n31 ';' '{' identifier_def_6 '}'
    | '{' identifier_def_6 '}'
    | %empty
    ;

identifier_def_6
    : IDENTIFIER ';' FLOAT_NUMBER
      {
          const std::wstring host = target_utf16::ToHostCodeUnits(grammar::SemanticPointer<char16_t>($1));
          const VALIDATE::Enum type = CItem::GetValidateType(host.c_str());
          m_pItem->SetValidateParam(type, grammar::SemanticDouble($3));
      }
    | IDENTIFIER ';' ACTION_VALUE
      {
          const std::wstring host = target_utf16::ToHostCodeUnits(grammar::SemanticPointer<char16_t>($1));
          const VALIDATE::Enum type = CItem::GetValidateType(host.c_str());
          m_pItem->SetValidateParam(type, static_cast<double>(grammar::SemanticInt64($3)));
      }
    ;

n33
    : n33 ';' '{' uc_in_residence_siege_field_def '}'
    | '{' uc_in_residence_siege_field_def '}'
    | %empty
    ;

uc_in_residence_siege_field_def
    : UC_IN_RESIDENCE_SIEGE_FIELD
      {
          const auto pushUseCondition = [this](CItemUseCondition* condition) {
              m_pItemConstInfo->m_vItemUseCondition.push_back(condition);
          };
          pushUseCondition(new CItemUseCondition_is_in_residence_siege_field);
      }
    | UC_RACE ';' '{' n40 '}'
      {
          const auto pushUseCondition = [this](CItemUseCondition* condition) {
              m_pItemConstInfo->m_vItemUseCondition.push_back(condition);
          };
          pushUseCondition(new CItemUseCondition_race(grammar::SemanticPointer<std::vector<int>>($4)));
      }
    | UC_REQUIREDLEVEL ';' ACTION_VALUE
      {
          const auto pushUseCondition = [this](CItemUseCondition* condition) {
              m_pItemConstInfo->m_vItemUseCondition.push_back(condition);
          };
          pushUseCondition(new CItemUseCondition_requiredlevel(grammar::SemanticInt32($3)));
      }
    | UC_CLASSID ';' ACTION_VALUE
      {
          const auto pushUseCondition = [this](CItemUseCondition* condition) {
              m_pItemConstInfo->m_vItemUseCondition.push_back(condition);
          };
          pushUseCondition(new CItemUseCondition_classid(grammar::SemanticInt32($3)));
      }
    | UC_TRANSFORMID ';' '{' n40 '}'
      {
          const auto pushUseCondition = [this](CItemUseCondition* condition) {
              m_pItemConstInfo->m_vItemUseCondition.push_back(condition);
          };
          pushUseCondition(new CItemUseCondition_transformid(grammar::SemanticPointer<std::vector<int>>($4)));
      }
    | UC_VITAL_RESTORE
      {
          const auto pushUseCondition = [this](CItemUseCondition* condition) {
              m_pItemConstInfo->m_vItemUseCondition.push_back(condition);
          };
          pushUseCondition(new CItemUseCondition_vital_restore);
      }
    | UC_VITAL_ITEM_COUNT
      {
          const auto pushUseCondition = [this](CItemUseCondition* condition) {
              m_pItemConstInfo->m_vItemUseCondition.push_back(condition);
          };
          pushUseCondition(new CItemUseCondition_vital_item_count);
      }
    | UC_VITAL_POINT_REMAIN
      {
          const auto pushUseCondition = [this](CItemUseCondition* condition) {
              m_pItemConstInfo->m_vItemUseCondition.push_back(condition);
          };
          pushUseCondition(new CItemUseCondition_vital_point_remain);
      }
    | UC_CATEGORY ';' '{' n40 '}'
      {
          const auto pushUseCondition = [this](CItemUseCondition* condition) {
              m_pItemConstInfo->m_vItemUseCondition.push_back(condition);
          };
          pushUseCondition(new CItemUseCondition_category(grammar::SemanticPointer<std::vector<int>>($4)));
      }
    | UC_INZONE ';' ACTION_VALUE
      {
          const auto pushUseCondition = [this](CItemUseCondition* condition) {
              m_pItemConstInfo->m_vItemUseCondition.push_back(condition);
          };
          pushUseCondition(new CItemUseCondition_inzone(grammar::SemanticInt32($3) == 1));
      }
    | UC_INZONE_NUM ';' '{' n40 '}'
      {
          const auto pushUseCondition = [this](CItemUseCondition* condition) {
              m_pItemConstInfo->m_vItemUseCondition.push_back(condition);
          };
          pushUseCondition(new CItemUseCondition_inzone_num(grammar::SemanticPointer<std::vector<int>>($4)));
      }
    | UC_TRANSMODE_INCLUDE ';' '{' n42 '}'
      {
          const auto pushUseCondition = [this](CItemUseCondition* condition) {
              m_pItemConstInfo->m_vItemUseCondition.push_back(condition);
          };
          auto* const names = grammar::SemanticPointer<std::vector<char16_t*>>($4);
          std::vector<int> values = ConvertTransformTypes(names);
          auto* condition = new CItemUseCondition_transmode_include;
          condition->SetData(&values);
          pushUseCondition(condition);
      }
    | UC_TRANSMODE_EXCLUDE ';' '{' n42 '}'
      {
          const auto pushUseCondition = [this](CItemUseCondition* condition) {
              m_pItemConstInfo->m_vItemUseCondition.push_back(condition);
          };
          auto* const names = grammar::SemanticPointer<std::vector<char16_t*>>($4);
          std::vector<int> values = ConvertTransformTypes(names);
          auto* condition = new CItemUseCondition_transmode_exclude;
          condition->SetData(&values);
          pushUseCondition(condition);
      }
    | UC_LEVEL ';' '{' ACTION_VALUE ';' ACTION_VALUE '}'
      {
          const auto pushUseCondition = [this](CItemUseCondition* condition) {
              m_pItemConstInfo->m_vItemUseCondition.push_back(condition);
          };
          pushUseCondition(new CItemUseCondition_level(grammar::SemanticInt32($4), grammar::SemanticInt32($6)));
      }
    | UC_SP ';' ACTION_VALUE
      {
          const auto pushUseCondition = [this](CItemUseCondition* condition) {
              m_pItemConstInfo->m_vItemUseCondition.push_back(condition);
          };
          pushUseCondition(new CItemUseCondition_sp(grammar::SemanticInt32($3)));
      }
    | UC_ADENA ';' ACTION_VALUE
      {
          const auto pushUseCondition = [this](CItemUseCondition* condition) {
              m_pItemConstInfo->m_vItemUseCondition.push_back(condition);
          };
          pushUseCondition(new CItemUseCondition_adena(grammar::SemanticInt32($3)));
      }
    | UC_RESTART_POINT ';' ACTION_VALUE
      {
          const auto pushUseCondition = [this](CItemUseCondition* condition) {
              m_pItemConstInfo->m_vItemUseCondition.push_back(condition);
          };
          pushUseCondition(new CItemUseCondition_restart_point(grammar::SemanticInt32($3)));
      }
    | UC_PEACEZONE
      {
          const auto pushUseCondition = [this](CItemUseCondition* condition) {
              m_pItemConstInfo->m_vItemUseCondition.push_back(condition);
          };
          pushUseCondition(new CItemUseCondition_peacezone);
      }
    | UC_SUBJOB
      {
          const auto pushUseCondition = [this](CItemUseCondition* condition) {
              m_pItemConstInfo->m_vItemUseCondition.push_back(condition);
          };
          pushUseCondition(new CItemUseCondition_subjob);
      }
    | UC_HP_MAX
      {
          const auto pushUseCondition = [this](CItemUseCondition* condition) {
              m_pItemConstInfo->m_vItemUseCondition.push_back(condition);
          };
          pushUseCondition(new CItemUseCondtion_Maxhp);
      }
    | UC_MP_MAX
      {
          const auto pushUseCondition = [this](CItemUseCondition* condition) {
              m_pItemConstInfo->m_vItemUseCondition.push_back(condition);
          };
          pushUseCondition(new CItemUseCondtion_Maxmp);
      }
    | UC_CP_MAX
      {
          const auto pushUseCondition = [this](CItemUseCondition* condition) {
              m_pItemConstInfo->m_vItemUseCondition.push_back(condition);
          };
          pushUseCondition(new CItemUseCondtion_Maxcp);
      }
    | UC_PREVENTDUPLICATION ';' '{' n42 '}'
      {
          const auto pushUseCondition = [this](CItemUseCondition* condition) {
              m_pItemConstInfo->m_vItemUseCondition.push_back(condition);
          };
          auto* const names = grammar::SemanticPointer<std::vector<char16_t*>>($4);
          std::vector<int> values = ConvertAbnormalTypes(names);
          auto* const condition = new CItemUseCondtion_PreventDuplicationABNS;
          condition->SetData(&values);
          pushUseCondition(condition);
      }
    | UC_KALIEBUFF
      {
          const auto pushUseCondition = [this](CItemUseCondition* condition) {
              m_pItemConstInfo->m_vItemUseCondition.push_back(condition);
          };
          pushUseCondition(new CItemUseCondtion_KalieEventBuff);
      }
    | UC_RESTOREVITAL ';' ACTION_VALUE
      {
          const auto pushUseCondition = [this](CItemUseCondition* condition) {
              m_pItemConstInfo->m_vItemUseCondition.push_back(condition);
          };
          pushUseCondition(new CItemUseCondtion_VitalRestore(grammar::SemanticInt32($3)));
      }
    | UC_RESTOREVITAL_DOWN ';' ACTION_VALUE
      {
          const auto pushUseCondition = [this](CItemUseCondition* condition) {
              m_pItemConstInfo->m_vItemUseCondition.push_back(condition);
          };
          pushUseCondition(new CItemUseCondtion_VitalRestore_Down(grammar::SemanticInt32($3)));
      }
    ;

n35
    : n35 ';' '{' ec_sex_def '}'
    | '{' ec_sex_def '}'
    | %empty
    ;

ec_sex_def
    : EC_SEX ';' ACTION_VALUE
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          pushEquipCondition(new CItemEquipCondition_sex(grammar::SemanticInt32($3)));
      }
    | EC_RACE ';' '{' n40 '}'
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          pushEquipCondition(new CItemEquipCondition_race(grammar::SemanticPointer<std::vector<int>>($4)));
      }
    | EC_CLASSID ';' ACTION_VALUE
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          pushEquipCondition(new CItemEquipCondition_classid(grammar::SemanticInt32($3)));
      }
    | EC_REQUIREDLEVEL ';' ACTION_VALUE
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          pushEquipCondition(new CItemEquipCondition_requiredlevel(grammar::SemanticInt32($3)));
      }
    | EC_CATEGORY ';' '{' n40 '}'
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          pushEquipCondition(new CItemEquipCondition_category(grammar::SemanticPointer<std::vector<int>>($4)));
      }
    | EC_AGIT ';' ACTION_VALUE
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          pushEquipCondition(new CItemEquipCondition_agit(grammar::SemanticInt32($3) == 1));
      }
    | EC_AGIT_NUM ';' '{' n40 '}'
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          pushEquipCondition(new CItemEquipCondition_some_agit(grammar::SemanticPointer<std::vector<int>>($4)));
      }
    | EC_CASTLE ';' ACTION_VALUE
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          pushEquipCondition(new CItemEquipCondition_castle(grammar::SemanticInt32($3) == 1));
      }
    | EC_CASTLE_NUM ';' '{' n40 '}'
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          pushEquipCondition(new CItemEquipCondition_some_castle(grammar::SemanticPointer<std::vector<int>>($4)));
      }
    | EC_FORTRESS ';' ACTION_VALUE
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          pushEquipCondition(new CItemEquipCondition_fortress(grammar::SemanticInt32($3) == 1));
      }
    | EC_FORTRESS_NUM ';' '{' n40 '}'
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          pushEquipCondition(new CItemEquipCondition_some_fortress(grammar::SemanticPointer<std::vector<int>>($4)));
      }
    | EC_CLAN_LEADER ';' ACTION_VALUE
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          pushEquipCondition(new CItemEquipCondition_clan_leader(grammar::SemanticInt32($3) == 1));
      }
    | EC_CLAN_LEVEL ';' ACTION_VALUE
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          pushEquipCondition(new CItemEquipCondition_clan_level(grammar::SemanticInt32($3)));
      }
    | EC_HERO ';' ACTION_VALUE
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          pushEquipCondition(new CItemEquipCondition_hero(grammar::SemanticInt32($3) == 1));
      }
    | EC_NOBLESS ';' ACTION_VALUE
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          pushEquipCondition(new CItemEquipCondition_nobless(grammar::SemanticInt32($3) == 1));
      }
    | T_1005E ';' ACTION_VALUE
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          pushEquipCondition(new CItemEquipCondition_honors(grammar::SemanticInt32($3) == 1));
      }
    | EC_CHAO ';' ACTION_VALUE
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          pushEquipCondition(new CItemEquipCondition_chao(grammar::SemanticInt32($3) == 1));
      }
    | EC_ACADEMY ';' ACTION_VALUE
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          pushEquipCondition(new CItemEquipCondition_academy(grammar::SemanticInt32($3) == 1));
      }
    | EC_SOCIAL_CLASS ';' ACTION_VALUE
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          pushEquipCondition(new CItemEquipCondition_social_class(grammar::SemanticInt32($3)));
      }
    | EC_SUBJOB ';' ACTION_VALUE
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          pushEquipCondition(new CItemEquipCondition_subjob(grammar::SemanticInt32($3)));
          if (static_cast<unsigned int>(grammar::SemanticInt32($3)) >= 4) {
              // Log.Add(LOG_ERROR, L"out of subjob id in line[%d]", m_lexer.line + 1)
              // Previous: L2_LOG_ERROR(Log,
              //     L"out of subjob id in line[%d]", m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"[object parser] Subjob ID is outside 0..3 at line %d; "
                  u"the condition was still added.",
                  m_lexer.line + 1
              );
          }
      }
    | EC_INZONE ';' ACTION_VALUE
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          pushEquipCondition(new CItemEquipCondition_inzone(grammar::SemanticInt32($3) == 1));
      }
    | EC_INZONE_NUM ';' '{' n40 '}'
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          pushEquipCondition(new CItemEquipCondition_inzone_num(grammar::SemanticPointer<std::vector<int>>($4)));
      }
    | EC_TRANSMODE_INCLUDE ';' '{' n42 '}'
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          auto* const names = grammar::SemanticPointer<std::vector<char16_t*>>($4);
          std::vector<int> values = ConvertTransformTypes(names);
          auto* condition = new CItemEquipCondition_transmode_include;
          condition->SetData(&values);
          pushEquipCondition(condition);
      }
    | EC_TRANSMODE_EXCLUDE ';' '{' n42 '}'
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          auto* const names = grammar::SemanticPointer<std::vector<char16_t*>>($4);
          std::vector<int> values = ConvertTransformTypes(names);
          auto* condition = new CItemEquipCondition_transmode_exclude;
          condition->SetData(&values);
          pushEquipCondition(condition);
      }
    | EC_CASTLE_TYPE ';' ACTION_VALUE
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          pushEquipCondition(new CItemEquipCondition_castle_type(grammar::SemanticInt32($3)));
      }
    | EC_PLEDGE ';' ACTION_VALUE
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          pushEquipCondition(new CItemEquipCondition_pledge_member(grammar::SemanticInt32($3)));
      }
    | EC_MAIN_DUAL_LEVEL ';' ACTION_VALUE
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          pushEquipCondition(new CItemEquipCondition_mainduallevel(grammar::SemanticInt32($3)));
      }
    | EC_JOB_GROUP ';' ACTION_VALUE
      {
          const auto pushEquipCondition = [this](CItemEquipCondition* condition) {
              m_pItemConstInfo->m_vItemEquipCondition.push_back(condition);
          };
          pushEquipCondition(new CItemEquipCondition_jobgroup(grammar::SemanticInt32($3)));
      }
    ;

n37
    : n37 ';' no_attack_force_equip_only_use_weapon_skill_def
    | no_attack_force_equip_only_use_weapon_skill_def
    | %empty
    ;

no_attack_force_equip_only_use_weapon_skill_def
    : NO_ATTACK_FORCE_EQUIP_ONLY_USE_WEAPON_SKILL
      { m_pItem->Set(OBJECT_FIELD_ITEM_EQUIP_OPTION, grammar::SemanticInt32($1)); }
    ;

action_value_def_2
    : ACTION_VALUE
      {
          const double result = static_cast<double>(grammar::SemanticInt64($1));
          std::uint64_t bits = 0;
          std::memcpy(&bits, &result, sizeof(bits));
          $$ = bits;
      }
    | FLOAT_NUMBER
      { $$ = $1; }
    ;

n40
    : n40 ';' ACTION_VALUE
      {
          auto* const values = grammar::SemanticPointer<std::vector<int>>($1);
          values->push_back(grammar::SemanticInt32($3));
          $$ = grammar::PointerSemantic(values);
      }
    | ACTION_VALUE
      {
          auto* const values = new std::vector<int>;
          values->push_back(grammar::SemanticInt32($1));
          $$ = grammar::PointerSemantic(values);
      }
    ;

n41
    : n41 ';' ACTION_VALUE
    | ACTION_VALUE
    ;

n42
    : n42 ';' IDENTIFIER
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

n43
    : n42 ';' ACTION_VALUE
      {
          auto* const names = grammar::SemanticPointer<std::vector<char16_t*>>($1);
          if (names != nullptr && names->size() != 1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"line[%d], base_attribute_attack has invalid param",
              //     m_lexer.line + 1)
              // Previous: L2_LOG_ERROR(Log,
              //     L"line[%d], base_attribute_attack has invalid param",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"[object parser] base_attribute_attack has an invalid "
                  u"parameter count at line %d; expected 1. Parsing continues.",
                  m_lexer.line + 1
              );
          }
          const std::int64_t attribute =
              names != nullptr && !names->empty() ? FindInTable(OBJECT_FIELD_BASE_ATTRIBUTE_TYPE, names->front()) : -1;
          if (attribute == -1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"line[%d], base_attribute_attack has invalid param.",
              //     m_lexer.line + 1)
              // Previous: L2_LOG_ERROR(Log,
              //     L"line[%d], base_attribute_attack has invalid param.",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"[object parser] base_attribute_attack has an unknown "
                  u"attribute at line %d; value -1 is retained.",
                  m_lexer.line + 1
              );
          }
          auto* const values = new std::vector<int>;
          values->push_back(static_cast<int>(attribute));
          values->push_back(grammar::SemanticInt32($3));
          $$ = grammar::PointerSemantic(values);
      }
    ;

action_value_def_3
    : ACTION_VALUE ';' ACTION_VALUE
      {
          m_pItem->m_const->SetShapeShiftableTargetItem(grammar::SemanticInt64($1) > 0);
          m_pItem->m_const->SetShapeShiftableExtractionItem(grammar::SemanticInt64($3) > 0);
      }
    | %empty
    ;

%%
