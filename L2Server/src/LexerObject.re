// CLexerForObject scanner (original L2Server/lexer_object.l).
// re2c generates LexerObject.cpp.

#include "L2Server/src/LexerObject.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ParserObject.h"

#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "ParserObjectTokens.h"

namespace {

std::string NarrowAscii(std::u16string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char16_t codeUnit : value) {
        if (codeUnit > 0x7f) {
            return {};
        }
        result.push_back(static_cast<char>(codeUnit));
    }
    return result;
}

std::int64_t ParseWindowsInt64(std::u16string_view value) {
    const std::string ascii = NarrowAscii(value);
    if (ascii.empty() && !value.empty()) {
        return 0;
    }

    errno = 0;
    char* end = nullptr;
    const long long parsed = std::strtoll(ascii.c_str(), &end, 10);
    if (end == ascii.c_str()) {
        return 0;
    }
    if (errno == ERANGE || parsed > LLONG_MAX) {
        return LLONG_MAX;
    }
    if (parsed < LLONG_MIN) {
        return LLONG_MIN;
    }
    return static_cast<std::int64_t>(parsed);
}

std::uint64_t ParseWindowsDoubleBits(std::u16string_view value) {
    const std::string ascii = NarrowAscii(value);
    char* end = nullptr;
    const double parsed = std::strtod(ascii.c_str(), &end);
    const double normalized = end == ascii.c_str() ? 0.0 : parsed;
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(normalized));
    std::memcpy(&bits, &normalized, sizeof(bits));
    return bits;
}

char16_t* DuplicateInnerText(const char16_t* text, int length) {
    if (text == nullptr || length < 2) {
        return nullptr;
    }
    const int destinationLength = length - 1;
    auto* const duplicate =
        static_cast<char16_t*>(std::malloc(static_cast<std::size_t>(destinationLength) * sizeof(char16_t)));
    if (duplicate == nullptr) {
        return nullptr;
    }
    std::copy_n(text + 1, length - 2, duplicate);
    duplicate[length - 2] = u'\0';
    return duplicate;
}

char16_t* DuplicateText(const char16_t* text, int length) {
    if (text == nullptr || length < 0) {
        return nullptr;
    }
    auto* const duplicate = static_cast<char16_t*>(std::malloc((static_cast<std::size_t>(length) + 1) * sizeof(char16_t)));
    if (duplicate == nullptr) {
        return nullptr;
    }
    std::copy_n(text, length, duplicate);
    duplicate[length] = u'\0';
    return duplicate;
}

} // namespace

CLexerForObject::CLexerForObject() :
    m_FileBuf(nullptr),
    m_pRawFileBuf(nullptr),
    m_nFilePointer(0),
    m_nFileSize(0),
    line(0),
    comment_count(0),
    m_bOnlyPrecompiledHeader(false) {
}

CLexerForObject::~CLexerForObject() {
    if (m_pRawFileBuf != nullptr) {
        std::free(m_pRawFileBuf);
    }
    m_pRawFileBuf = nullptr;
}

int CLexerForObject::Create(CParserForObject* parser, char16_t* fileBuffer, int fileSize) {
    m_nFilePointer = 0;
    m_FileBuf = fileBuffer;
    m_nFileSize = fileSize;
    m_bOnlyPrecompiledHeader = parser->m_bOnlyPrecompiledHeader;
    return 1;
}

/*!conditions:re2c*/

namespace {

// Original yybegin value / 2 -> re2c condition.
constexpr YYCONDTYPE kConditionByStart[] = {yycINITIAL, yycCOMMENT, yycMAIN};

} // namespace

int CLexerForObject::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        // The original lexer read m_FileBuf one code unit at a time.
        SetInput(
            m_FileBuf + m_nFilePointer,
            m_nFilePointer < m_nFileSize ? static_cast<std::size_t>(m_nFileSize - m_nFilePointer) : 0
        );
    }
    auto* const semanticSlot = lval;
    auto* const semanticInt = reinterpret_cast<std::int32_t*>(semanticSlot);
    for (;;) {
        BeginToken();
        /*!include:re2c "UTF16Scanner.re" */
        /*!re2c
            re2c:define:YYGETCONDITION = "kConditionByStart[yystart / 2]";
            <COMMENT> "/*" {
                EndToken();
                ++comment_count;
                continue;
            }
            <COMMENT> "*/" {
                EndToken();
                --comment_count;
                if (comment_count == 0) {
                    yybegin(4);
                }
                continue;
            }
            <COMMENT> "\n" {
                EndToken();
                ++line;
                continue;
            }
            <COMMENT> . { EndToken(); continue; }
            <MAIN> "//" [^\n]* / "\n" { EndToken(); continue; }
            <MAIN> "/*" {
                EndToken();
                yybegin(2);
                comment_count = 1;
                continue;
            }
            <MAIN> "item_begin" { EndToken(); return TK_ITEM_BEGIN; }
            <MAIN> "item_end" { EndToken(); return TK_ITEM_END; }
            <MAIN> "weapon" {
                EndToken();
                *semanticInt = 0;
                return TK_WEAPON_GROUP;
            }
            <MAIN> "accessary" {
                EndToken();
                *semanticInt = 1;
                return TK_WEAPON_GROUP;
            }
            <MAIN> "armor" {
                EndToken();
                *semanticInt = 2;
                return TK_WEAPON_GROUP;
            }
            <MAIN> "asset" {
                EndToken();
                *semanticInt = 3;
                return TK_WEAPON_GROUP;
            }
            <MAIN> "etcitem" {
                EndToken();
                *semanticInt = 4;
                return TK_WEAPON_GROUP;
            }
            <MAIN> "questitem" {
                EndToken();
                *semanticInt = 5;
                return TK_WEAPON_GROUP;
            }
            <MAIN> "item_id" {
                EndToken();
                *semanticInt = 0;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "name" {
                EndToken();
                *semanticInt = 1;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "name_id" {
                EndToken();
                *semanticInt = 2;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "description_id" {
                EndToken();
                *semanticInt = 3;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "html" {
                EndToken();
                *semanticInt = 0xaf;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "armor_type" {
                EndToken();
                *semanticInt = 4;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "item_type" {
                EndToken();
                *semanticInt = 5;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "slot_bit_type" {
                EndToken();
                *semanticInt = 6;
                return TK_SLOT_BIT_TYPE;
            }
            <MAIN> "blessed" {
                EndToken();
                *semanticInt = 7;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "weight" {
                EndToken();
                *semanticInt = 8;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "consume_type" {
                EndToken();
                *semanticInt = 9;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "initial_count" {
                EndToken();
                *semanticInt = 10;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "maximum_count" {
                EndToken();
                *semanticInt = 11;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "soulshot_count" {
                EndToken();
                *semanticInt = 12;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "spiritshot_count" {
                EndToken();
                *semanticInt = 13;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "price" {
                EndToken();
                *semanticInt = 0x11;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "default_price" {
                EndToken();
                *semanticInt = 0x12;
                return TK_DEFAULT_PRICE;
            }
            <MAIN> "immediate_effect" {
                EndToken();
                *semanticInt = 0x10;
                return TK_IMMEDIATE_EFFECT;
            }
            <MAIN> "ex_immediate_effect" {
                EndToken();
                *semanticInt = 0x104;
                return TK_EX_IMMEDIATE_EFFECT;
            }
            <MAIN> "item_skill" {
                EndToken();
                *semanticInt = 0x23;
                return TK_ITEM_SKILL;
            }
            <MAIN> "material_type" {
                EndToken();
                *semanticInt = 0x24;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "crystal_type" { EndToken(); return TK_CRYSTAL_TYPE; }
            <MAIN> "crystal_count" {
                EndToken();
                *semanticInt = 0x25;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "is_trade" {
                EndToken();
                *semanticInt = 0x26;
                return TK_IS_TRADE;
            }
            <MAIN> "is_npctrade" {
                EndToken();
                *semanticInt = 0x43;
                return TK_IS_NPCTRADE;
            }
            <MAIN> "is_attribution" {
                EndToken();
                *semanticInt = 0x42;
                return TK_IS_ATTRIBUTION;
            }
            <MAIN> "attr_groupid" {
                EndToken();
                *semanticInt = 0x44;
                return TK_ATTR_GROUPID;
            }
            <MAIN> "is_locked" {
                EndToken();
                *semanticInt = 0x45;
                return TK_IS_LOCKED;
            }
            <MAIN> "is_private_store" { EndToken(); return TK_IS_PRIVATE_STORE; }
            <MAIN> "is_drop" {
                EndToken();
                *semanticInt = 0x27;
                return TK_IS_DROP;
            }
            <MAIN> "is_destruct" {
                EndToken();
                *semanticInt = 0x28;
                return TK_IS_DESTRUCT;
            }
            <MAIN> "physical_damage" {
                EndToken();
                *semanticInt = 0x29;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "random_damage" {
                EndToken();
                *semanticInt = 0x2a;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "weapon_type" {
                EndToken();
                *semanticInt = 0x2b;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "can_penetrate" {
                EndToken();
                *semanticInt = 0x2c;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "critical" {
                EndToken();
                *semanticInt = 0x2d;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "attack_range" {
                EndToken();
                *semanticInt = 0x2e;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "effective_range" {
                EndToken();
                *semanticInt = 0x2f;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "damage_range" {
                EndToken();
                *semanticInt = 0x31;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "attack_speed" {
                EndToken();
                *semanticInt = 0x32;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "reuse_delay" {
                EndToken();
                *semanticInt = 0x33;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "mp_consume" {
                EndToken();
                *semanticInt = 0x34;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "magical_damage" {
                EndToken();
                *semanticInt = 0x36;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "durability" {
                EndToken();
                *semanticInt = 0x37;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "damaged" {
                EndToken();
                *semanticInt = 0x38;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "physical_defense" {
                EndToken();
                *semanticInt = 0x39;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "magical_defense" {
                EndToken();
                *semanticInt = 0x3a;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "enchanted" {
                EndToken();
                *semanticInt = 0x3b;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "item_multi_skill_list" {
                EndToken();
                *semanticInt = 0x10a;
                return TK_ITEM_MULTI_SKILL_LIST;
            }
            <MAIN> "keep_type" {
                EndToken();
                *semanticInt = 0x10d;
                return TK_KEEP_TYPE;
            }
            <MAIN> "required_level" {
                EndToken();
                *semanticInt = 0x10e;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "can_move" {
                EndToken();
                *semanticInt = 0x110;
                return TK_CAN_MOVE;
            }
            <MAIN> "category" { EndToken(); return TK_CATEGORY; }
            <MAIN> "default_action" { EndToken(); return TK_DEFAULT_ACTION; }
            <MAIN> "action_none" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(0);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_skill_reduce" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(1);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_equip" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(2);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_soulshot" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(3);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_bless_soulshot" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(30);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_spiritshot" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(4);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_bless_spiritshot" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(5);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_skill_maintain" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(6);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_map_open" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(7);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_xmas_open" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(14);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_show_html" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(15);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_show_tutorial" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(16);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_dice" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(8);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_recipe" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(9);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_capsule" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(10);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_calc" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(11);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_seed" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(12);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_harvest" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(13);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_show_ssq_status" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(17);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_summon_soulshot" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(18);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_summon_spiritshot" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(19);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_fishingshot" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(20);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_show_adventurer_guide_book" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(21);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_keep_exp" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(22);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_call_skill" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(23);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_create_mpcc" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(24);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_peel" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(25);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_nick_color" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(26);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_hide_name" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(27);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_start_quest" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(28);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_skill_reduce_on_skill_success" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(29);
                return TK_ACTION_VALUE;
            }
            <MAIN> "action_use_count" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(31);
                return TK_ACTION_VALUE;
            }
            <MAIN> "hit_modify" {
                EndToken();
                *semanticInt = 0xa6;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "avoid_modify" {
                EndToken();
                *semanticInt = 0xa7;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "shield_defense_rate" {
                EndToken();
                *semanticInt = 0xa8;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "shield_defense" {
                EndToken();
                *semanticInt = 0xa9;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "etcitem_type" {
                EndToken();
                *semanticInt = 0xaa;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "dual_fhit_rate" {
                EndToken();
                *semanticInt = 0xad;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "mp_bonus" {
                EndToken();
                *semanticInt = 0x3c;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "critical_attack_skill" {
                EndToken();
                *semanticInt = 0x3d;
                return TK_CRITICAL_ATTACK_SKILL;
            }
            <MAIN> "attack_skill" {
                EndToken();
                *semanticInt = 0x3e;
                return TK_ATTACK_SKILL;
            }
            <MAIN> "magic_skill" {
                EndToken();
                *semanticInt = 0x3f;
                return TK_MAGIC_SKILL;
            }
            <MAIN> "item_skill_enchanted_four" {
                EndToken();
                *semanticInt = 0x40;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "for_npc" {
                EndToken();
                *semanticInt = 0x41;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "recipe_id" { EndToken(); return TK_RECIPE_ID; }
            <MAIN> "reduced_soulshot" {
                EndToken();
                *semanticInt = 0x0e;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "reduced_spiritshot" {
                EndToken();
                *semanticInt = 0x0f;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "reduced_mp_consume" {
                EndToken();
                *semanticInt = 0x35;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "enchant_enable" {
                EndToken();
                *semanticInt = 0xe2;
                return TK_ENCHANT_ENABLE;
            }
            <MAIN> "is_premium" {
                EndToken();
                *semanticInt = 0x120;
                return TK_IS_PREMIUM;
            }
            <MAIN> "capsuled_items" { EndToken(); return TK_CAPSULED_ITEMS; }
            <MAIN> "is_olympiad_can_use" {
                EndToken();
                *semanticInt = 0x125;
                return TK_IS_OLYMPIAD_CAN_USE;
            }
            <MAIN> "is_pledgegame_can_use" {
                EndToken();
                *semanticInt = 0x126;
                return TK_IS_PLEDGEGAME_CAN_USE;
            }
            <MAIN> "is_hero_entry" {
                EndToken();
                *semanticInt = 0x12e;
                return TK_IS_HERO_ENTRY;
            }
            <MAIN> "is_hp_limit_break" {
                EndToken();
                *semanticInt = 0x131;
                return TK_IS_HP_LIMIT_BREAK;
            }
            <MAIN> "item_option" { EndToken(); return TK_ITEM_OPTION; }
            <MAIN> "is_chance_affected" {
                EndToken();
                *semanticInt = 0x128;
                return TK_IS_CHANCE_AFFECTED;
            }
            <MAIN> "ensoul_slot_cnt" {
                EndToken();
                *semanticInt = 0x136;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "create_items" { EndToken(); return TK_CREATE_ITEMS; }
            <MAIN> "create_item_groups" { EndToken(); return TK_CREATE_ITEM_GROUPS; }
            <MAIN> "validate_params" { EndToken(); return TK_VALIDATE_PARAMS; }
            <MAIN> "property_params" { EndToken(); return TK_PROPERTY_PARAMS; }
            <MAIN> "set_begin" { EndToken(); return TK_SET_BEGIN; }
            <MAIN> "set_end" { EndToken(); return TK_SET_END; }
            <MAIN> "slot_underwear" { EndToken(); return TK_SLOT_UNDERWEAR; }
            <MAIN> "slot_rear" { EndToken(); return TK_SLOT_REAR; }
            <MAIN> "slot_lear" { EndToken(); return TK_SLOT_LEAR; }
            <MAIN> "slot_neck" { EndToken(); return TK_SLOT_NECK; }
            <MAIN> "slot_rfinger" { EndToken(); return TK_SLOT_RFINGER; }
            <MAIN> "slot_lfinger" { EndToken(); return TK_SLOT_LFINGER; }
            <MAIN> "slot_head" { EndToken(); return TK_SLOT_HEAD; }
            <MAIN> "slot_rhand" { EndToken(); return TK_SLOT_RHAND; }
            <MAIN> "slot_lhand" { EndToken(); return TK_SLOT_LHAND; }
            <MAIN> "slot_gloves" { EndToken(); return TK_SLOT_GLOVES; }
            <MAIN> "slot_chest" { EndToken(); return TK_SLOT_CHEST; }
            <MAIN> "slot_legs" { EndToken(); return TK_SLOT_LEGS; }
            <MAIN> "slot_feet" { EndToken(); return TK_SLOT_FEET; }
            <MAIN> "slot_back" { EndToken(); return TK_SLOT_BACK; }
            <MAIN> "slot_rlhand" { EndToken(); return TK_SLOT_RLHAND; }
            <MAIN> "slot_additional" { EndToken(); return TK_SLOT_ADDITIONAL; }
            <MAIN> "set_skill" { EndToken(); return TK_SET_SKILL; }
            <MAIN> "set_effect_skill" { EndToken(); return TK_SET_EFFECT_SKILL; }
            <MAIN> "set_additional_effect_skill" { EndToken(); return TK_SET_ADDITIONAL_EFFECT_SKILL; }
            <MAIN> "set_additional2_condition" { EndToken(); return TK_SET_ADDITIONAL2_CONDITION; }
            <MAIN> "set_additional2_effect_skill" { EndToken(); return TK_SET_ADDITIONAL2_EFFECT_SKILL; }
            <MAIN> "p_def_inc" {
                EndToken();
                *semanticInt = 196;
                return TK_P_DEF_INC_GROUP;
            }
            <MAIN> "m_def_inc" {
                EndToken();
                *semanticInt = 197;
                return TK_P_DEF_INC_GROUP;
            }
            <MAIN> "hp_inc" {
                EndToken();
                *semanticInt = 198;
                return TK_P_DEF_INC_GROUP;
            }
            <MAIN> "mp_inc" {
                EndToken();
                *semanticInt = 199;
                return TK_P_DEF_INC_GROUP;
            }
            <MAIN> "move_speed_inc" {
                EndToken();
                *semanticInt = 201;
                return TK_P_DEF_INC_GROUP;
            }
            <MAIN> "breath_inc" {
                EndToken();
                *semanticInt = 200;
                return TK_P_DEF_INC_GROUP;
            }
            <MAIN> "magic_resist_inc" {
                EndToken();
                *semanticInt = 202;
                return TK_P_DEF_INC_GROUP;
            }
            <MAIN> "casting_speed_inc" {
                EndToken();
                *semanticInt = 203;
                return TK_P_DEF_INC_GROUP;
            }
            <MAIN> "p_attack_inc" {
                EndToken();
                *semanticInt = 204;
                return TK_P_DEF_INC_GROUP;
            }
            <MAIN> "m_attack_inc" {
                EndToken();
                *semanticInt = 205;
                return TK_P_DEF_INC_GROUP;
            }
            <MAIN> "hp_regen_inc" {
                EndToken();
                *semanticInt = 206;
                return TK_P_DEF_INC_GROUP;
            }
            <MAIN> "mp_regen_inc" {
                EndToken();
                *semanticInt = 207;
                return TK_P_DEF_INC_GROUP;
            }
            <MAIN> "str_inc" {
                EndToken();
                *semanticInt = 208;
                return TK_P_DEF_INC_GROUP;
            }
            <MAIN> "con_inc" {
                EndToken();
                *semanticInt = 209;
                return TK_P_DEF_INC_GROUP;
            }
            <MAIN> "dex_inc" {
                EndToken();
                *semanticInt = 210;
                return TK_P_DEF_INC_GROUP;
            }
            <MAIN> "int_inc" {
                EndToken();
                *semanticInt = 211;
                return TK_P_DEF_INC_GROUP;
            }
            <MAIN> "men_inc" {
                EndToken();
                *semanticInt = 212;
                return TK_P_DEF_INC_GROUP;
            }
            <MAIN> "wit_inc" {
                EndToken();
                *semanticInt = 213;
                return TK_P_DEF_INC_GROUP;
            }
            <MAIN> "avoid_inc" {
                EndToken();
                *semanticInt = 214;
                return TK_P_DEF_INC_GROUP;
            }
            <MAIN> "shield_def_prob_inc" {
                EndToken();
                *semanticInt = 215;
                return TK_P_DEF_INC_GROUP;
            }
            <MAIN> "weight_limit_inc" {
                EndToken();
                *semanticInt = 216;
                return TK_P_DEF_INC_GROUP;
            }
            <MAIN> "resist_poison_inc" {
                EndToken();
                *semanticInt = 217;
                return TK_P_DEF_INC_GROUP;
            }
            <MAIN> "p_def_vs_dagger_inc" {
                EndToken();
                *semanticInt = 218;
                return TK_P_DEF_INC_GROUP;
            }
            <MAIN> "magic_weapon" {
                EndToken();
                *semanticInt = 0xdf;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "drop_period" {
                EndToken();
                *semanticInt = 0x109;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "delay_share_group" {
                EndToken();
                *semanticInt = 0x10c;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "duration" {
                EndToken();
                *semanticInt = 0x10f;
                return TK_DURATION;
            }
            <MAIN> "base_attribute_attack" {
                EndToken();
                *semanticInt = 0x111;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "base_attribute_defend" {
                EndToken();
                *semanticInt = 0x112;
                return TK_BASE_ATTRIBUTE_DEFEND;
            }
            <MAIN> "unequip_skill" {
                EndToken();
                *semanticInt = 0x115;
                return TK_UNEQUIP_SKILL;
            }
            <MAIN> "equip_skill" {
                EndToken();
                *semanticInt = 0x116;
                return TK_EQUIP_SKILL;
            }
            <MAIN> "elemental_enable" {
                EndToken();
                *semanticInt = 0x117;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "use_skill_distime" {
                EndToken();
                *semanticInt = 0x11e;
                return TK_USE_SKILL_DISTIME;
            }
            <MAIN> "equip_reuse_delay" {
                EndToken();
                *semanticInt = 0x11f;
                return TK_ITEM_ID_GROUP;
            }
            <MAIN> "period" {
                EndToken();
                *semanticInt = 0x121;
                return TK_PERIOD;
            }
            <MAIN> "item_equip_option" { EndToken(); return TK_ITEM_EQUIP_OPTION; }
            <MAIN> "no_attack" {
                EndToken();
                *semanticInt = 0;
                return TK_NO_ATTACK_FORCE_EQUIP_ONLY_USE_WEAPON_SKILL;
            }
            <MAIN> "force_equip" {
                EndToken();
                *semanticInt = 1;
                return TK_NO_ATTACK_FORCE_EQUIP_ONLY_USE_WEAPON_SKILL;
            }
            <MAIN> "only_use_weapon_skill" {
                EndToken();
                *semanticInt = 2;
                return TK_NO_ATTACK_FORCE_EQUIP_ONLY_USE_WEAPON_SKILL;
            }
            <MAIN> "use_condition" { EndToken(); return TK_USE_CONDITION; }
            <MAIN> "uc_in_residence_siege_field" { EndToken(); return TK_UC_IN_RESIDENCE_SIEGE_FIELD; }
            <MAIN> "uc_race" { EndToken(); return TK_UC_RACE; }
            <MAIN> "uc_classid" { EndToken(); return TK_UC_CLASSID; }
            <MAIN> "uc_category" { EndToken(); return TK_UC_CATEGORY; }
            <MAIN> "uc_inzone" { EndToken(); return TK_UC_INZONE; }
            <MAIN> "uc_inzone_num" { EndToken(); return TK_UC_INZONE_NUM; }
            <MAIN> "uc_requiredlevel" { EndToken(); return TK_UC_REQUIREDLEVEL; }
            <MAIN> "uc_transmode_include" { EndToken(); return TK_UC_TRANSMODE_INCLUDE; }
            <MAIN> "uc_transmode_exclude" { EndToken(); return TK_UC_TRANSMODE_EXCLUDE; }
            <MAIN> "uc_level" { EndToken(); return TK_UC_LEVEL; }
            <MAIN> "uc_sp" { EndToken(); return TK_UC_SP; }
            <MAIN> "uc_adena" { EndToken(); return TK_UC_ADENA; }
            <MAIN> "uc_transformid" { EndToken(); return TK_UC_TRANSFORMID; }
            <MAIN> "uc_vital_restore" { EndToken(); return TK_UC_VITAL_RESTORE; }
            <MAIN> "uc_vital_item_count" { EndToken(); return TK_UC_VITAL_ITEM_COUNT; }
            <MAIN> "uc_vital_point_remain" { EndToken(); return TK_UC_VITAL_POINT_REMAIN; }
            <MAIN> "uc_restart_point" { EndToken(); return TK_UC_RESTART_POINT; }
            <MAIN> "uc_peacezone" { EndToken(); return TK_UC_PEACEZONE; }
            <MAIN> "uc_subjob" { EndToken(); return TK_UC_SUBJOB; }
            <MAIN> "uc_hp_max" { EndToken(); return TK_UC_HP_MAX; }
            <MAIN> "uc_mp_max" { EndToken(); return TK_UC_MP_MAX; }
            <MAIN> "uc_cp_max" { EndToken(); return TK_UC_CP_MAX; }
            <MAIN> "uc_preventduplication" { EndToken(); return TK_UC_PREVENTDUPLICATION; }
            <MAIN> "uc_kaliebuff" { EndToken(); return TK_UC_KALIEBUFF; }
            <MAIN> "uc_restorevital" { EndToken(); return TK_UC_RESTOREVITAL; }
            <MAIN> "uc_restorevital_down" { EndToken(); return TK_UC_RESTOREVITAL_DOWN; }
            <MAIN> "equip_condition" { EndToken(); return TK_EQUIP_CONDITION; }
            <MAIN> "ec_sex" { EndToken(); return TK_EC_SEX; }
            <MAIN> "ec_race" { EndToken(); return TK_EC_RACE; }
            <MAIN> "ec_classid" { EndToken(); return TK_EC_CLASSID; }
            <MAIN> "ec_category" { EndToken(); return TK_EC_CATEGORY; }
            <MAIN> "ec_agit" { EndToken(); return TK_EC_AGIT; }
            <MAIN> "ec_agit_num" { EndToken(); return TK_EC_AGIT_NUM; }
            <MAIN> "ec_castle" { EndToken(); return TK_EC_CASTLE; }
            <MAIN> "ec_castle_num" { EndToken(); return TK_EC_CASTLE_NUM; }
            <MAIN> "ec_fortress" { EndToken(); return TK_EC_FORTRESS; }
            <MAIN> "ec_fortress_num" { EndToken(); return TK_EC_FORTRESS_NUM; }
            <MAIN> "ec_clan_leader" { EndToken(); return TK_EC_CLAN_LEADER; }
            <MAIN> "ec_clan_level" { EndToken(); return TK_EC_CLAN_LEVEL; }
            <MAIN> "ec_hero" { EndToken(); return TK_EC_HERO; }
            <MAIN> "ec_nobless" { EndToken(); return TK_EC_NOBLESS; }
            <MAIN> "ec_chao" { EndToken(); return TK_EC_CHAO; }
            <MAIN> "ec_academy" { EndToken(); return TK_EC_ACADEMY; }
            <MAIN> "ec_social_class" { EndToken(); return TK_EC_SOCIAL_CLASS; }
            <MAIN> "ec_subjob" { EndToken(); return TK_EC_SUBJOB; }
            <MAIN> "ec_inzone" { EndToken(); return TK_EC_INZONE; }
            <MAIN> "ec_inzone_num" { EndToken(); return TK_EC_INZONE_NUM; }
            <MAIN> "ec_requiredlevel" { EndToken(); return TK_EC_REQUIREDLEVEL; }
            <MAIN> "ec_transmode_include" { EndToken(); return TK_EC_TRANSMODE_INCLUDE; }
            <MAIN> "ec_transmode_exclude" { EndToken(); return TK_EC_TRANSMODE_EXCLUDE; }
            <MAIN> "ec_castle_type" { EndToken(); return TK_EC_CASTLE_TYPE; }
            <MAIN> "ec_pledge" { EndToken(); return TK_EC_PLEDGE; }
            <MAIN> "ec_main_dual_level" { EndToken(); return TK_EC_MAIN_DUAL_LEVEL; }
            <MAIN> "ec_job_group" { EndToken(); return TK_EC_JOB_GROUP; }
            <MAIN> "crystal_item" { EndToken(); return TK_CRYSTAL_ITEM; }
            <MAIN> "auction_itemtype" { EndToken(); return TK_AUCTION_ITEMTYPE; }
            <MAIN> "general_itemtype" { EndToken(); return TK_GENERAL_ITEMTYPE; }
            <MAIN> "shape_shiftable" { EndToken(); return TK_SHAPE_SHIFTABLE; }
            <MAIN> "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_ACTION_VALUE;
            }
            <MAIN> "$" ("0" | [1-9][0-9]*) {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext + 1, static_cast<std::size_t>(yyleng - 1)))
                );
                return TK_ACTION_VALUE;
            }
            // The original lex scanner trimmed this trailing context back to the
            // longest float. A float followed by "." or by the end of the input
            // keeps its last digit there, so these two rules do the same.
            <MAIN> "-"? [0-9]+ "." ([0-9]+ | [0-9]* [eE] [+-]? [0-9][0-9]+) / "." {
                EndToken();
                *semanticSlot = ParseWindowsDoubleBits(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                return TK_FLOAT_NUMBER;
            }
            <MAIN> "-"? [0-9]+ "." [0-9]* ([eE] [+-]? [0-9]+)? / [^.] {
                if (limit_ - cursor_ == 1 && *cursor_ >= u'0' && *cursor_ <= u'9') {
                    ++cursor_;
                }
                EndToken();
                *semanticSlot = ParseWindowsDoubleBits(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                return TK_FLOAT_NUMBER;
            }
            <MAIN> [,;=[\]{}~] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <MAIN> "[" [^[\]]* "]" {
                EndToken();
                char16_t* const text = DuplicateInnerText(yytext, yyleng);
                if (text != nullptr) {
                    *semanticSlot = grammar::PointerSemantic(text);
                }
                return TK_NAME;
            }
            <MAIN> [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                char16_t* const text = DuplicateText(yytext, yyleng);
                *semanticSlot = grammar::PointerSemantic(text);
                return TK_IDENTIFIER;
            }
            <MAIN> "@" [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                std::int64_t value = -1;
                if (!m_bOnlyPrecompiledHeader) {
                    value = g_PrecompiledHeaderDB.Search(yytext + 1);
                    if (value == -1) {
                        // const std::wstring hostText = target_utf16::ToHostCodeUnits(yytext)
                        const std::wstring hostText = Utf16ToWide(yytext);
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"Undefined word[%s] in precompiled header",
                        //     hostText.c_str())
                        // L2_LOG_ERROR(Log,
                        //     L"Undefined word[%s] in precompiled header",
                        //     hostText.c_str())
                        L2_LOG_ERROR(
                            Log,
                            L"Item-data reference '%ls' at line %lld is missing from the precompiled header; parsing "
                            L"continues "
                            L"with ID -1.",
                            hostText.c_str(), static_cast<long long>(line) + 1
                        );
                    }
                }
                *semanticSlot = static_cast<grammar::SemanticBits>(value);
                return TK_ACTION_VALUE;
            }
            <MAIN> [ \t\r] { EndToken(); continue; }
            <MAIN> "\n" {
                EndToken();
                ++line;
                continue;
            }
            <MAIN> . {
                EndToken();
                // const std::wstring invalid = target_utf16::ToHostCodeUnits(yytext)
                const std::wstring invalid = Utf16ToWide(yytext);
                // Log.Add(
                //     LOG_ERROR,
                //     L"[LEXER_ERROR] Invalid char = %s",
                //     invalid.c_str())
                // L2_LOG_ERROR(Log,
                //     L"[LEXER_ERROR] Invalid char = %s",
                //     invalid.c_str())
                L2_LOG_ERROR(
                    Log, L"Invalid item-data token '%ls' at line %lld; the token is skipped.", invalid.c_str(),
                    static_cast<long long>(line) + 1
                );
                continue;
            }
            <INITIAL> * { EndToken(); continue; }
            <INITIAL,COMMENT,MAIN> $ { return 0; }
        */
    }
}
