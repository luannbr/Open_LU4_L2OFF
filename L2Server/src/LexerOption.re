// optiondata.txt scanner (original L2Server/lexer_option.l).
// re2c generates LexerOption.cpp.

#include "L2Server/src/LexerOption.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ParserOption.h"

#include "L2Server/src/Portable/ProcessRuntime.h"
#include "Shared/src/Log.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "ParserOptionTokens.h"

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
    if (errno == ERANGE) {
        return parsed < 0 ? INT64_MIN : INT64_MAX;
    }
    return static_cast<std::int64_t>(parsed);
}

std::uint64_t ParseWindowsDoubleBits(std::u16string_view value) {
    const std::string ascii = NarrowAscii(value);
    double parsed = 0.0;
    if (!ascii.empty() || value.empty()) {
        char* end = nullptr;
        parsed = std::strtod(ascii.c_str(), &end);
        if (end == ascii.c_str()) {
            parsed = 0.0;
        }
    }

    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(parsed));
    std::memcpy(&bits, &parsed, sizeof(bits));
    return bits;
}

char16_t* DuplicateInnerText(const char16_t* text, int length) {
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
    auto* const duplicate = static_cast<char16_t*>(std::malloc(static_cast<std::size_t>(length + 1) * sizeof(char16_t)));
    if (duplicate == nullptr) {
        return nullptr;
    }
    std::copy_n(text, length, duplicate);
    duplicate[length] = u'\0';
    return duplicate;
}

} // namespace

CLexerForOption::CLexerForOption() :
    m_FileBuf(nullptr), m_pRawFileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), line(0), comment_count(0) {
}

CLexerForOption::~CLexerForOption() {
    if (m_pRawFileBuf != nullptr) {
        std::free(m_pRawFileBuf);
    }
}

int CLexerForOption::Create(char16_t* fileBuffer, int fileSize) {
    m_nFilePointer = 0;
    m_FileBuf = fileBuffer;
    m_nFileSize = fileSize;
    return 1;
}

/*!conditions:re2c*/

namespace {

// Original yybegin value / 2 -> re2c condition.
constexpr YYCONDTYPE kConditionByStart[] = {yycINITIAL, yycCOMMENT, yycMAIN};

} // namespace

int CLexerForOption::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        // The original lexer read m_FileBuf one code unit at a time.
        SetInput(
            m_FileBuf + m_nFilePointer,
            m_nFilePointer < m_nFileSize ? static_cast<std::size_t>(m_nFileSize - m_nFilePointer) : 0
        );
    }
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
            <MAIN> "option_begin" { EndToken(); return TK_OPTION_BEGIN; }
            <MAIN> "option_end" { EndToken(); return TK_OPTION_END; }
            <MAIN> "str_inc" { EndToken(); return TK_STR_INC; }
            <MAIN> "con_inc" { EndToken(); return TK_CON_INC; }
            <MAIN> "dex_inc" { EndToken(); return TK_DEX_INC; }
            <MAIN> "int_inc" { EndToken(); return TK_INT_INC; }
            <MAIN> "men_inc" { EndToken(); return TK_MEN_INC; }
            <MAIN> "wit_inc" { EndToken(); return TK_WIT_INC; }
            <MAIN> "all_stat_inc" { EndToken(); return TK_ALL_STAT_INC; }
            <MAIN> "effect" { EndToken(); return TK_EFFECT; }
            <MAIN> "p_physical_attack" { EndToken(); return TK_P_PHYSICAL_ATTACK; }
            <MAIN> "p_physical_defence" { EndToken(); return TK_P_PHYSICAL_DEFENCE; }
            <MAIN> "p_magical_defence" { EndToken(); return TK_P_MAGICAL_DEFENCE; }
            <MAIN> "p_hp_regen" { EndToken(); return TK_P_HP_REGEN; }
            <MAIN> "p_mp_regen" { EndToken(); return TK_P_MP_REGEN; }
            <MAIN> "p_speed" { EndToken(); return TK_P_SPEED; }
            <MAIN> "p_create_item" { EndToken(); return TK_P_CREATE_ITEM; }
            <MAIN> "p_create_common_item" { EndToken(); return TK_P_CREATE_COMMON_ITEM; }
            <MAIN> "p_attack_speed" { EndToken(); return TK_P_ATTACK_SPEED; }
            <MAIN> "p_magic_speed" { EndToken(); return TK_P_MAGIC_SPEED; }
            <MAIN> "p_remove_equip_penalty" { EndToken(); return TK_P_REMOVE_EQUIP_PENALTY; }
            <MAIN> "p_crystallize" { EndToken(); return TK_P_CRYSTALLIZE; }
            <MAIN> "p_avoid" { EndToken(); return TK_P_AVOID; }
            <MAIN> "p_luck" { EndToken(); return TK_P_LUCK; }
            <MAIN> "p_physical_attack_by_material" { EndToken(); return TK_P_PHYSICAL_ATTACK_BY_MATERIAL; }
            <MAIN> "p_physical_defence_by_material" { EndToken(); return TK_P_PHYSICAL_DEFENCE_BY_MATERIAL; }
            <MAIN> "p_attack_attribute" { EndToken(); return TK_P_ATTACK_ATTRIBUTE; }
            <MAIN> "p_defence_attribute" { EndToken(); return TK_P_DEFENCE_ATTRIBUTE; }
            <MAIN> "p_attack_trait" { EndToken(); return TK_P_ATTACK_TRAIT; }
            <MAIN> "p_defence_trait" { EndToken(); return TK_P_DEFENCE_TRAIT; }
            <MAIN> "p_block_act" { EndToken(); return TK_P_BLOCK_ACT; }
            <MAIN> "p_block_move" { EndToken(); return TK_P_BLOCK_MOVE; }
            <MAIN> "p_block_controll" { EndToken(); return TK_P_BLOCK_CONTROLL; }
            <MAIN> "p_block_spell" { EndToken(); return TK_P_BLOCK_SPELL; }
            <MAIN> "p_block_getdamage" { EndToken(); return TK_P_BLOCK_GETDAMAGE; }
            <MAIN> "p_fear" { EndToken(); return TK_P_FEAR; }
            <MAIN> "p_max_hp" { EndToken(); return TK_P_MAX_HP; }
            <MAIN> "p_max_mp" { EndToken(); return TK_P_MAX_MP; }
            <MAIN> "p_max_cp" { EndToken(); return TK_P_MAX_CP; }
            <MAIN> "p_hit" { EndToken(); return TK_P_HIT; }
            <MAIN> "p_magical_attack" { EndToken(); return TK_P_MAGICAL_ATTACK; }
            <MAIN> "p_attack_range" { EndToken(); return TK_P_ATTACK_RANGE; }
            <MAIN> "p_critical_rate" { EndToken(); return TK_P_CRITICAL_RATE; }
            <MAIN> "p_critical_damage" { EndToken(); return TK_P_CRITICAL_DAMAGE; }
            <MAIN> "p_reuse_delay" { EndToken(); return TK_P_REUSE_DELAY; }
            <MAIN> "p_reduce_cancel" { EndToken(); return TK_P_REDUCE_CANCEL; }
            <MAIN> "p_attack_speed_by_weapon" { EndToken(); return TK_P_ATTACK_SPEED_BY_WEAPON; }
            <MAIN> "p_magic_speed_by_weapon" { EndToken(); return TK_P_MAGIC_SPEED_BY_WEAPON; }
            <MAIN> "p_shield_defence_rate" { EndToken(); return TK_P_SHIELD_DEFENCE_RATE; }
            <MAIN> "p_hp_regen_by_move_mode" { EndToken(); return TK_P_HP_REGEN_BY_MOVE_MODE; }
            <MAIN> "p_mp_regen_by_move_mode" { EndToken(); return TK_P_MP_REGEN_BY_MOVE_MODE; }
            <MAIN> "p_avoid_by_move_mode" { EndToken(); return TK_P_AVOID_BY_MOVE_MODE; }
            <MAIN> "p_safe_fall_height" { EndToken(); return TK_P_SAFE_FALL_HEIGHT; }
            <MAIN> "p_breath" { EndToken(); return TK_P_BREATH; }
            <MAIN> "p_damage_shield" { EndToken(); return TK_P_DAMAGE_SHIELD; }
            <MAIN> "p_passive" { EndToken(); return TK_P_PASSIVE; }
            <MAIN> "p_avoid_agro" { EndToken(); return TK_P_AVOID_AGRO; }
            <MAIN> "p_weight_limit" { EndToken(); return TK_P_WEIGHT_LIMIT; }
            <MAIN> "p_trade" { EndToken(); return TK_P_TRADE; }
            <MAIN> "p_set_collected" { EndToken(); return TK_P_SET_COLLECTED; }
            <MAIN> "p_set_cloak_slot" { EndToken(); return TK_P_SET_CLOAK_SLOT; }
            <MAIN> "p_cubic_mastery" { EndToken(); return TK_P_CUBIC_MASTERY; }
            <MAIN> "p_weight_penalty" { EndToken(); return TK_P_WEIGHT_PENALTY; }
            <MAIN> "p_mana_charge" { EndToken(); return TK_P_MANA_CHARGE; }
            <MAIN> "p_transfer_damage_summon" { EndToken(); return TK_P_TRANSFER_DAMAGE_SUMMON; }
            <MAIN> "p_heal_effect" { EndToken(); return TK_P_HEAL_EFFECT; }
            <MAIN> "p_area_damage" { EndToken(); return TK_P_AREA_DAMAGE; }
            <MAIN> "p_hit_at_night" { EndToken(); return TK_P_HIT_AT_NIGHT; }
            <MAIN> "p_vampiric_attack" { EndToken(); return TK_P_VAMPIRIC_ATTACK; }
            <MAIN> "p_vampiric_defence" { EndToken(); return TK_P_VAMPIRIC_DEFENCE; }
            <MAIN> "p_critical_rate_by_hp1" { EndToken(); return TK_P_CRITICAL_RATE_BY_HP1; }
            <MAIN> "p_critical_rate_by_hp2" { EndToken(); return TK_P_CRITICAL_RATE_BY_HP2; }
            <MAIN> "p_physical_attack_by_hp1" { EndToken(); return TK_P_PHYSICAL_ATTACK_BY_HP1; }
            <MAIN> "p_physical_attack_by_hp2" { EndToken(); return TK_P_PHYSICAL_ATTACK_BY_HP2; }
            <MAIN> "p_physical_defence_by_hp1" { EndToken(); return TK_P_PHYSICAL_DEFENCE_BY_HP1; }
            <MAIN> "p_physical_defence_by_hp2" { EndToken(); return TK_P_PHYSICAL_DEFENCE_BY_HP2; }
            <MAIN> "p_avoid_rate_by_hp1" { EndToken(); return TK_P_AVOID_RATE_BY_HP1; }
            <MAIN> "p_avoid_rate_by_hp2" { EndToken(); return TK_P_AVOID_RATE_BY_HP2; }
            <MAIN> "p_attack_speed_by_hp1" { EndToken(); return TK_P_ATTACK_SPEED_BY_HP1; }
            <MAIN> "p_attack_speed_by_hp2" { EndToken(); return TK_P_ATTACK_SPEED_BY_HP2; }
            <MAIN> "p_fatal_blow_rate" { EndToken(); return TK_P_FATAL_BLOW_RATE; }
            <MAIN> "p_critical_rate_position_bonus" { EndToken(); return TK_P_CRITICAL_RATE_POSITION_BONUS; }
            <MAIN> "p_reduce_drop_penalty" { EndToken(); return TK_P_REDUCE_DROP_PENALTY; }
            <MAIN> "p_physical_shield_defence" { EndToken(); return TK_P_PHYSICAL_SHIELD_DEFENCE; }
            <MAIN> "p_physical_armor_hit" { EndToken(); return TK_P_PHYSICAL_ARMOR_HIT; }
            <MAIN> "p_physical_polarm_target_single" { EndToken(); return TK_P_PHYSICAL_POLARM_TARGET_SINGLE; }
            <MAIN> "p_magic_critical_rate" { EndToken(); return TK_P_MAGIC_CRITICAL_RATE; }
            <MAIN> "p_magic_mp_cost" { EndToken(); return TK_P_MAGIC_MP_COST; }
            <MAIN> "p_after_mp_cost" { EndToken(); return TK_P_AFTER_MP_COST; }
            <MAIN> "p_physical_shield_defence_angle_all" { EndToken(); return TK_P_PHYSICAL_SHIELD_DEFENCE_ANGLE_ALL; }
            <MAIN> "p_pvp_physical_attack_dmg_bonus" { EndToken(); return TK_P_PVP_PHYSICAL_ATTACK_DMG_BONUS; }
            <MAIN> "p_pvp_physical_skill_dmg_bonus" { EndToken(); return TK_P_PVP_PHYSICAL_SKILL_DMG_BONUS; }
            <MAIN> "p_pvp_magical_skill_dmg_bonus" { EndToken(); return TK_P_PVP_MAGICAL_SKILL_DMG_BONUS; }
            <MAIN> "p_fishing_mastery" { EndToken(); return TK_P_FISHING_MASTERY; }
            <MAIN> "p_abnormal_rate_limit" { EndToken(); return TK_P_ABNORMAL_RATE_LIMIT; }
            <MAIN> "p_abnormal_remove_by_hit" { EndToken(); return TK_P_ABNORMAL_REMOVE_BY_HIT; }
            <MAIN> "p_abnormal_remove_by_dmg" { EndToken(); return TK_P_ABNORMAL_REMOVE_BY_DMG; }
            <MAIN> "p_block_skill_physical" { EndToken(); return TK_P_BLOCK_SKILL_PHYSICAL; }
            <MAIN> "p_block_skill_special" { EndToken(); return TK_P_BLOCK_SKILL_SPECIAL; }
            <MAIN> "p_block_buff" { EndToken(); return TK_P_BLOCK_BUFF; }
            <MAIN> "p_block_debuff" { EndToken(); return TK_P_BLOCK_DEBUFF; }
            <MAIN> "p_block_skill" { EndToken(); return TK_P_BLOCK_SKILL; }
            <MAIN> "p_skill_critical" { EndToken(); return TK_P_SKILL_CRITICAL; }
            <MAIN> "p_skill_critical_probability" { EndToken(); return TK_P_SKILL_CRITICAL_PROBABILITY; }
            <MAIN> "p_critical_damage_position" { EndToken(); return TK_P_CRITICAL_DAMAGE_POSITION; }
            <MAIN> "p_resist_dispel_by_category" { EndToken(); return TK_P_RESIST_DISPEL_BY_CATEGORY; }
            <MAIN> "p_resist_abnormal_by_category" { EndToken(); return TK_P_RESIST_ABNORMAL_BY_CATEGORY; }
            <MAIN> "p_preserve_abnormal" { EndToken(); return TK_P_PRESERVE_ABNORMAL; }
            <MAIN> "p_hit_number" { EndToken(); return TK_P_HIT_NUMBER; }
            <MAIN> "p_cp_regen" { EndToken(); return TK_P_CP_REGEN; }
            <MAIN> "p_cp_regen_by_move_mode" { EndToken(); return TK_P_CP_REGEN_BY_MOVE_MODE; }
            <MAIN> "p_betray" { EndToken(); return TK_P_BETRAY; }
            <MAIN> "p_channel_clan" { EndToken(); return TK_P_CHANNEL_CLAN; }
            <MAIN> "p_change_fishing_mastery" { EndToken(); return TK_P_CHANGE_FISHING_MASTERY; }
            <MAIN> "p_skill_power" { EndToken(); return TK_P_SKILL_POWER; }
            <MAIN> "p_lucky_enchant_rate" { EndToken(); return TK_P_LUCKY_ENCHANT_RATE; }
            <MAIN> "p_damage_shield_resist" { EndToken(); return TK_P_DAMAGE_SHIELD_RESIST; }
            <MAIN> "p_instant_kill_resist" { EndToken(); return TK_P_INSTANT_KILL_RESIST; }
            <MAIN> "p_stat_up" { EndToken(); return TK_P_STAT_UP; }
            <MAIN> "p_get_item_by_exp" { EndToken(); return TK_P_GET_ITEM_BY_EXP; }
            <MAIN> "p_magic_avoid" { EndToken(); return TK_P_MAGIC_AVOID; }
            <MAIN> "passive_skill" { EndToken(); return TK_PASSIVE_SKILL; }
            <MAIN> "attack_skill" { EndToken(); return TK_ATTACK_SKILL; }
            <MAIN> "critical_skill" { EndToken(); return TK_CRITICAL_SKILL; }
            <MAIN> "magic_skill" { EndToken(); return TK_MAGIC_SKILL; }
            <MAIN> "active_skill" { EndToken(); return TK_ACTIVE_SKILL; }
            <MAIN> "p_physical_skill_critical_damage" { EndToken(); return TK_P_PHYSICAL_SKILL_CRITICAL_DAMAGE; }
            <MAIN> "p_magic_critical_dmg" { EndToken(); return TK_P_MAGIC_CRITICAL_DMG; }
            <MAIN> "p_magic_hit" { EndToken(); return TK_P_MAGIC_HIT; }
            <MAIN> "is_variation_item_equip_reusedelay" { EndToken(); return TK_IS_VARIATION_ITEM_EQUIP_REUSEDELAY; }
            <MAIN> "per" {
                EndToken();
                *lval = 0;
                return TK_PER_ABS_DIFF;
            }
            <MAIN> "abs" {
                EndToken();
                *lval = 3;
                return TK_PER_ABS_DIFF;
            }
            <MAIN> "diff" {
                EndToken();
                *lval = 1;
                return TK_PER_ABS_DIFF;
            }
            <MAIN> "front" {
                EndToken();
                *lval = 0;
                return TK_FRONT_SIDE_BACK;
            }
            <MAIN> "side" {
                EndToken();
                *lval = 1;
                return TK_FRONT_SIDE_BACK;
            }
            <MAIN> "back" {
                EndToken();
                *lval = 2;
                return TK_FRONT_SIDE_BACK;
            }
            <MAIN> "attr_none" {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(static_cast<std::int64_t>(-2));
                return TK_ATTR_VALUE;
            }
            <MAIN> "attr_earth" {
                EndToken();
                *lval = 3;
                return TK_ATTR_VALUE;
            }
            <MAIN> "attr_water" {
                EndToken();
                *lval = 1;
                return TK_ATTR_VALUE;
            }
            <MAIN> "attr_wind" {
                EndToken();
                *lval = 2;
                return TK_ATTR_VALUE;
            }
            <MAIN> "attr_fire" {
                EndToken();
                *lval = 0;
                return TK_ATTR_VALUE;
            }
            <MAIN> "attr_holy" {
                EndToken();
                *lval = 4;
                return TK_ATTR_VALUE;
            }
            <MAIN> "attr_unholy" {
                EndToken();
                *lval = 5;
                return TK_ATTR_VALUE;
            }
            <MAIN> "trait_none" {
                EndToken();
                *lval = 66;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_sword" {
                EndToken();
                *lval = 1;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_blunt" {
                EndToken();
                *lval = 2;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_dagger" {
                EndToken();
                *lval = 3;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_pole" {
                EndToken();
                *lval = 4;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_fist" {
                EndToken();
                *lval = 5;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_bow" {
                EndToken();
                *lval = 6;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_etc" {
                EndToken();
                *lval = 7;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_poison" {
                EndToken();
                *lval = 9;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_hold" {
                EndToken();
                *lval = 10;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_bleed" {
                EndToken();
                *lval = 11;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_sleep" {
                EndToken();
                *lval = 12;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_shock" {
                EndToken();
                *lval = 13;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_derangement" {
                EndToken();
                *lval = 14;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_bug_weakness" {
                EndToken();
                *lval = 15;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_animal_weakness" {
                EndToken();
                *lval = 16;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_plant_weakness" {
                EndToken();
                *lval = 17;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_beast_weakness" {
                EndToken();
                *lval = 18;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_dragon_weakness" {
                EndToken();
                *lval = 19;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_paralyze" {
                EndToken();
                *lval = 20;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_dual" {
                EndToken();
                *lval = 21;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_dualfist" {
                EndToken();
                *lval = 22;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_boss" {
                EndToken();
                *lval = 23;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_giant_weakness" {
                EndToken();
                *lval = 24;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_construct_weakness" {
                EndToken();
                *lval = 25;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_death" {
                EndToken();
                *lval = 26;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_valakas" {
                EndToken();
                *lval = 27;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_anesthesia" {
                EndToken();
                *lval = 28;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_critical_poison" {
                EndToken();
                *lval = 29;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_root_physically" {
                EndToken();
                *lval = 30;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_root_magically" {
                EndToken();
                *lval = 31;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_rapier" {
                EndToken();
                *lval = 32;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_crossbow" {
                EndToken();
                *lval = 33;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_ancientsword" {
                EndToken();
                *lval = 34;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_airbind" {
                EndToken();
                *lval = 42;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_changebody" {
                EndToken();
                *lval = 43;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_knockback" {
                EndToken();
                *lval = 44;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_knockdown" {
                EndToken();
                *lval = 45;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_pull" {
                EndToken();
                *lval = 46;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_demonic_weakness" {
                EndToken();
                *lval = 47;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_divine_weakness" {
                EndToken();
                *lval = 48;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_elemental_weakness" {
                EndToken();
                *lval = 49;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_fairy_weakness" {
                EndToken();
                *lval = 50;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_human_weakness" {
                EndToken();
                *lval = 51;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_humanoid_weakness" {
                EndToken();
                *lval = 52;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_undead_weakness" {
                EndToken();
                *lval = 53;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_deport" {
                EndToken();
                *lval = 56;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_disarm" {
                EndToken();
                *lval = 57;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_bluff" {
                EndToken();
                *lval = 58;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_gravity" {
                EndToken();
                *lval = 59;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_air_deport" {
                EndToken();
                *lval = 60;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_psychic" {
                EndToken();
                *lval = 61;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_hate" {
                EndToken();
                *lval = 62;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_wind_stun" {
                EndToken();
                *lval = 63;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_zone" {
                EndToken();
                *lval = 64;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "trait_silence" {
                EndToken();
                *lval = 65;
                return TK_TRAIT_VALUE;
            }
            <MAIN> "all" { EndToken(); return TK_ALL; }
            <MAIN> "sword" {
                EndToken();
                *lval = 1;
                return TK_SWORD_GROUP;
            }
            <MAIN> "twohandsword" {
                EndToken();
                *lval = 2;
                return TK_SWORD_GROUP;
            }
            <MAIN> "buster" {
                EndToken();
                *lval = 3;
                return TK_SWORD_GROUP;
            }
            <MAIN> "blunt" {
                EndToken();
                *lval = 4;
                return TK_SWORD_GROUP;
            }
            <MAIN> "twohandblunt" {
                EndToken();
                *lval = 5;
                return TK_SWORD_GROUP;
            }
            <MAIN> "staff" {
                EndToken();
                *lval = 6;
                return TK_SWORD_GROUP;
            }
            <MAIN> "twohandstaff" {
                EndToken();
                *lval = 7;
                return TK_SWORD_GROUP;
            }
            <MAIN> "dagger" {
                EndToken();
                *lval = 8;
                return TK_SWORD_GROUP;
            }
            <MAIN> "pole" {
                EndToken();
                *lval = 9;
                return TK_SWORD_GROUP;
            }
            <MAIN> "fist" {
                EndToken();
                *lval = 10;
                return TK_SWORD_GROUP;
            }
            <MAIN> "bow" {
                EndToken();
                *lval = 11;
                return TK_SWORD_GROUP;
            }
            <MAIN> "dual" {
                EndToken();
                *lval = 13;
                return TK_SWORD_GROUP;
            }
            <MAIN> "dualfist" {
                EndToken();
                *lval = 14;
                return TK_SWORD_GROUP;
            }
            <MAIN> "twohandcrossbow" {
                EndToken();
                *lval = 22;
                return TK_SWORD_GROUP;
            }
            <MAIN> "dualblunt" {
                EndToken();
                *lval = 23;
                return TK_SWORD_GROUP;
            }
            <MAIN> "dualdagger" {
                EndToken();
                *lval = 20;
                return TK_SWORD_GROUP;
            }
            <MAIN> "rapier" {
                EndToken();
                *lval = 16;
                return TK_SWORD_GROUP;
            }
            <MAIN> "crossbow" {
                EndToken();
                *lval = 17;
                return TK_SWORD_GROUP;
            }
            <MAIN> "ancientsword" {
                EndToken();
                *lval = 18;
                return TK_SWORD_GROUP;
            }
            <MAIN> "weapon_etc" {
                EndToken();
                *lval = 12;
                return TK_SWORD_GROUP;
            }
            <MAIN> "armor_none" {
                EndToken();
                *lval = 2;
                return TK_ARMOR_VALUE;
            }
            <MAIN> "armor_heavy" {
                EndToken();
                *lval = 0;
                return TK_ARMOR_VALUE;
            }
            <MAIN> "armor_light" {
                EndToken();
                *lval = 1;
                return TK_ARMOR_VALUE;
            }
            <MAIN> "armor_magic" {
                EndToken();
                *lval = 3;
                return TK_ARMOR_VALUE;
            }
            <MAIN> "armor_sigil" {
                EndToken();
                *lval = 4;
                return TK_ARMOR_VALUE;
            }
            <MAIN> "pk" {
                EndToken();
                *lval = 0;
                return TK_PK_MOB_BOSS;
            }
            <MAIN> "mob" {
                EndToken();
                *lval = 1;
                return TK_PK_MOB_BOSS;
            }
            <MAIN> "boss" {
                EndToken();
                *lval = 2;
                return TK_PK_MOB_BOSS;
            }
            <MAIN> "[" [^[\]]* "]" {
                EndToken();
                char16_t* const text = DuplicateInnerText(yytext, yyleng);
                if (text != nullptr) {
                    *lval = grammar::PointerSemantic(text);
                }
                return TK_NAME;
            }
            <MAIN> "=" { EndToken(); return TK_EQUAL; }
            <MAIN> "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                L2_GUARD_FUNCTION();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_INTEGER;
            }
            <MAIN> "$" ("0" | [1-9][0-9]*) {
                EndToken();
                L2_GUARD_FUNCTION();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext + 1, static_cast<std::size_t>(yyleng - 1)))
                );
                return TK_INTEGER;
            }
            <MAIN> "-"{0,2} ("0" | [1-9][0-9]*) "." [0-9]* {
                EndToken();
                L2_GUARD_FUNCTION();
                *lval = ParseWindowsDoubleBits(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                return TK_FLOAT_NUMBER;
            }
            <MAIN> [&;{}] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <MAIN> [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                *lval =
                    grammar::PointerSemantic(DuplicateText(yytext, yyleng));
                return TK_IDENTIFIER;
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
                    Log, L"Invalid option-data token '%ls' at line %lld; the token is skipped.", invalid.c_str(),
                    static_cast<long long>(line) + 1
                );
                continue;
            }
            <INITIAL> * { EndToken(); continue; }
            <INITIAL,COMMENT,MAIN> $ { return 0; }
        */
    }
}
