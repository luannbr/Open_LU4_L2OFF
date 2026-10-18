// skilldata.txt grammar (original L2Server/parser_skill.y).
// Bison generates ParserSkill.cpp and ParserSkillTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserSkill.h"
#include "L2Server/src/ParserSkillTables.inc"

#include "Shared/src/SharedFactory.h"

#include "L2Server/src/Item.h"
#include "L2Server/src/PredefinedListDB.h"
#include "L2Server/src/SharedContainer.h"
#include "L2Server/src/SkillFxFunc.h"
#include "L2Server/src/Transform.h"
#include "L2Server/src/User.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/Territory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"
#include "Shared/src/Vitality.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "ParserSkillTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForSkill::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserSkillTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
using AbnormalStatus = CParserForSkill::AbnormalStatus;

namespace {

const char16_t* SemanticWord(grammar::SemanticBits value) {
    return grammar::SemanticString(value);
}

bool TargetWordEquals(const char16_t* value, std::u16string_view expected) { return std::u16string_view(value) == expected; }

bool TargetWordAsciiIEquals(const char16_t* value, std::u16string_view expected) {
    if (value == nullptr) {
        return false;
    }
    std::size_t index = 0;
    for (; index < expected.size(); ++index) {
        char16_t actual = value[index];
        char16_t wanted = expected[index];
        if (actual >= u'A' && actual <= u'Z') {
            actual = static_cast<char16_t>(actual - u'A' + u'a');
        }
        if (wanted >= u'A' && wanted <= u'Z') {
            wanted = static_cast<char16_t>(wanted - u'A' + u'a');
        }
        if (actual != wanted) {
            return false;
        }
    }
    return value[index] == u'\0';
}

wchar_t* DuplicateSkillName(const char16_t* source) {
    // Current helper 0x00cee248 is calloc(length + 1, 2) followed by a
    // checked UTF-16 copy. wchar_t is two bytes on the target. Widening the
    // allocation on POSIX is the required host representation difference.
    if (source == nullptr) {
        return nullptr;
    }
    const std::size_t length = std::char_traits<char16_t>::length(source);
    auto* const result = static_cast<wchar_t*>(std::calloc(length + 1, sizeof(wchar_t)));
    if (result == nullptr) {
        return nullptr;
    }
    for (std::size_t index = 0; index < length; ++index) {
        result[index] = static_cast<wchar_t>(source[index]);
    }
    return result;
}

bool ScopeRequiresPositiveRange(SkillAffectScopeEnum scope) {
    switch (scope) {
        case SAFS_PARTY:
        case SAFS_UNION:
        case SAFS_PLEDGE:
        case SAFS_POINT_BLANK:
        case SAFS_RANGE:
        case SAFS_RANGE_SORT_BY_DIST:
        case SAFS_STATIC_OBJECT:
        case SAFS_WYVERN_SCOPE:
        case SAFS_BALAKAS_SCOPE:
            return true;
        default:
            return false;
    }
}

} // namespace

CParserForSkill::CParserForSkill() :
    m_vMaterial(),
    m_SummonStBonusVector(),
    m_lexer(),
    m_vItemChanceType(),
    m_vWeaponBonusTypeList(),
    m_sMagicType(),
    m_vAbnormalData(),
    m_setAbnormalSlotType(),
    m_setSkillName(),
    m_vChangeBody(),
    m_vCondition(),
    m_CallTree() {

    m_pPrevSkillInfo = nullptr;
    m_pSkillInfo = nullptr;
    m_EffectActivateTiming = CSkillInfo::EAT_INVALID;
    m_ConditionTarget = CSkillInfo::OCT_INVALID;
    m_pTerritory = nullptr;
    m_nLineNumber = 0;
    m_bOnlyPrecompiledHeader = false;
    m_bReload = false;
    m_bNewSkill = true;
    m_bAllBattleField = false;

    m_mapToggleGroup.clear();
    m_vItemChanceType.clear();
    m_vWeaponBonusTypeList.clear();
    m_sMagicType.clear();
    m_vAbnormalData.clear();
    m_setAbnormalSlotType.clear();
    m_setSkillName.clear();
    m_vChangeBody.clear();

    pShapeShiftWpFixedEffect = nullptr;
    pShapeShiftAmFixedEffect = nullptr;
    pShapeShiftItemAutoMatchingEffect = nullptr;
    eShapeShiftItemAutoMatchingType = static_cast<ShapeShiftAutoMatchingType::type>(-1);
    pRandomCallSkillEffect = nullptr;
}

CParserForSkill::~CParserForSkill() {
    PostAlterSkillLoad();
}

// Current L2Server.exe CParserForSkill::s_AbnormalTypes at
// 0x015f5240. The source spellings are not derived from the
// enum: 31 current names differ from the matching enum identifier.
const CParserForSkill::AbnormalStatus CParserForSkill::s_AbnormalTypes[ABNS_MAX] = {
    AbnormalStatus{u"ab_hawk_eye", ABNS_AB_HAWK_EYE},
    AbnormalStatus{u"all_attack_down", ABNS_ALL_ATTACK_DOWN},
    AbnormalStatus{u"all_attack_up", ABNS_ALL_ATTACK_UP},
    AbnormalStatus{u"all_speed_down", ABNS_ALL_SPEED_DOWN},
    AbnormalStatus{u"all_speed_up", ABNS_ALL_SPEED_UP},
    AbnormalStatus{u"antaras_debuff", ABNS_ANTARAS_DEBUFF},
    AbnormalStatus{u"armor_earth", ABNS_ARMOR_EARTH},
    AbnormalStatus{u"armor_fire", ABNS_ARMOR_FIRE},
    AbnormalStatus{u"armor_holy", ABNS_ARMOR_HOLY},
    AbnormalStatus{u"armor_unholy", ABNS_ARMOR_UNHOLY},
    AbnormalStatus{u"armor_water", ABNS_ARMOR_WATER},
    AbnormalStatus{u"armor_wind", ABNS_ARMOR_WIND},
    AbnormalStatus{u"attack_speed_up_bow", ABNS_ATTACK_SPEED_UP_BOW},
    AbnormalStatus{u"attack_time_down", ABNS_ATTACK_TIME_DOWN},
    AbnormalStatus{u"attack_time_up", ABNS_ATTACK_TIME_UP},
    AbnormalStatus{u"avoid_down", ABNS_AVOID_DOWN},
    AbnormalStatus{u"avoid_up", ABNS_AVOID_UP},
    AbnormalStatus{u"avoid_up_special", ABNS_AVOID_UP_SPECIAL},
    AbnormalStatus{u"berserker", ABNS_BERSERKER},
    AbnormalStatus{u"big_body", ABNS_BIG_BODY},
    AbnormalStatus{u"big_head", ABNS_BIG_HEAD},
    AbnormalStatus{u"bleeding", ABNS_BLEEDING},
    AbnormalStatus{u"bow_range_up", ABNS_BOW_RANGE_UP},
    AbnormalStatus{u"buff_queen_of_cat", ABNS_BUFF_QUEEN_OF_CAT},
    AbnormalStatus{u"buff_unicorn_seraphim", ABNS_BUFF_UNICORN_SERAPHIM},
    AbnormalStatus{u"cancel_prob_down", ABNS_CANCEL_PROB_DOWN},
    AbnormalStatus{u"casting_time_down", ABNS_CASTING_TIME_DOWN},
    AbnormalStatus{u"casting_time_up", ABNS_CASTING_TIME_UP},
    AbnormalStatus{u"cheap_magic", ABNS_CHEAP_MAGIC},
    AbnormalStatus{u"critical_dmg_down", ABNS_CRITICAL_DMG_DOWN},
    AbnormalStatus{u"critical_dmg_up", ABNS_CRITICAL_DMG_UP},
    AbnormalStatus{u"critical_prob_down", ABNS_CRITICAL_PROB_DOWN},
    AbnormalStatus{u"critical_prob_up", ABNS_CRITICAL_PROB_UP},
    AbnormalStatus{u"dance_of_aqua_guard", ABNS_DANCE_OF_AQUA_GUARD},
    AbnormalStatus{u"dance_of_concentration", ABNS_DANCE_OF_CONCENTRATION},
    AbnormalStatus{u"dance_of_earth_guard", ABNS_DANCE_OF_EARTH_GUARD},
    AbnormalStatus{u"dance_of_fire", ABNS_DANCE_OF_FIRE},
    AbnormalStatus{u"dance_of_fury", ABNS_DANCE_OF_FURY},
    AbnormalStatus{u"dance_of_inspiration", ABNS_DANCE_OF_INSPIRATION},
    AbnormalStatus{u"dance_of_light", ABNS_DANCE_OF_LIGHT},
    AbnormalStatus{u"dance_of_mystic", ABNS_DANCE_OF_MYSTIC},
    AbnormalStatus{u"dance_of_protection", ABNS_DANCE_OF_PROTECTION},
    AbnormalStatus{u"dance_of_shadow", ABNS_DANCE_OF_SHADOW},
    AbnormalStatus{u"dance_of_siren", ABNS_DANCE_OF_SIREN},
    AbnormalStatus{u"dance_of_vampire", ABNS_DANCE_OF_VAMPIRE},
    AbnormalStatus{u"dance_of_warrior", ABNS_DANCE_OF_WARRIOR},
    AbnormalStatus{u"debuff_nightshade", ABNS_DEBUFF_NIGHTSHADE},
    AbnormalStatus{u"debuff_shield", ABNS_DEBUFF_SHIELD},
    AbnormalStatus{u"decrease_weight_penalty", ABNS_DECREASE_WEIGHT_PENALTY},
    AbnormalStatus{u"derangement", ABNS_DERANGEMENT},
    AbnormalStatus{u"detect_weakness", ABNS_DETECT_WEAKNESS},
    AbnormalStatus{u"dmg_shield", ABNS_DMG_SHIELD},
    AbnormalStatus{u"dot_attr", ABNS_DOT_ATTR},
    AbnormalStatus{u"dot_mp", ABNS_DOT_MP},
    AbnormalStatus{u"dragon_breath", ABNS_DRAGON_BREATH},
    AbnormalStatus{u"duelist_spirit", ABNS_DUELIST_SPIRIT},
    AbnormalStatus{u"fatal_poison", ABNS_FATAL_POISON},
    AbnormalStatus{u"fishing_mastery_down", ABNS_FISHING_MASTERY_DOWN},
    AbnormalStatus{u"fly_away", ABNS_FLY_AWAY},
    AbnormalStatus{u"focus_dagger", ABNS_FOCUS_DAGGER},
    AbnormalStatus{u"heal_effect_down", ABNS_HEAL_EFFECT_DOWN},
    AbnormalStatus{u"heal_effect_up", ABNS_HEAL_EFFECT_UP},
    AbnormalStatus{u"hero_buff", ABNS_HERO_BUFF},
    AbnormalStatus{u"hero_debuff", ABNS_HERO_DEBUFF},
    AbnormalStatus{u"hit_down", ABNS_HIT_DOWN},
    AbnormalStatus{u"hit_up", ABNS_HIT_UP},
    AbnormalStatus{u"holy_attack", ABNS_HOLY_ATTACK},
    AbnormalStatus{u"hp_recover", ABNS_HP_RECOVER},
    AbnormalStatus{u"hp_regen_down", ABNS_HP_REGEN_DOWN},
    AbnormalStatus{u"hp_regen_up", ABNS_HP_REGEN_UP},
    AbnormalStatus{u"life_force_orc", ABNS_LIFE_FORCE_ORC},
    AbnormalStatus{u"life_force_others", ABNS_LIFE_FORCE_OTHERS},
    AbnormalStatus{u"magic_critical_up", ABNS_MAGIC_CRITICAL_UP},
    AbnormalStatus{u"majesty", ABNS_MAJESTY},
    AbnormalStatus{u"max_breath_up", ABNS_MAX_BREATH_UP},
    AbnormalStatus{u"max_hp_down", ABNS_MAX_HP_DOWN},
    AbnormalStatus{u"max_hp_up", ABNS_MAX_HP_UP},
    AbnormalStatus{u"max_mp_up", ABNS_MAX_MP_UP},
    AbnormalStatus{u"ma_down", ABNS_MA_DOWN},
    AbnormalStatus{u"ma_up", ABNS_MA_UP},
    AbnormalStatus{u"ma_up_herb", ABNS_MA_UP_HERB},
    AbnormalStatus{u"md_down", ABNS_MD_DOWN},
    AbnormalStatus{u"md_up", ABNS_MD_UP},
    AbnormalStatus{u"md_up_attr", ABNS_MD_UP_ATTR},
    AbnormalStatus{u"might_mortal", ABNS_MIGHT_MORTAL},
    AbnormalStatus{u"mp_cost_down", ABNS_MP_COST_DOWN},
    AbnormalStatus{u"mp_cost_up", ABNS_MP_COST_UP},
    AbnormalStatus{u"mp_recover", ABNS_MP_RECOVER},
    AbnormalStatus{u"mp_regen_up", ABNS_MP_REGEN_UP},
    AbnormalStatus{u"multi_buff", ABNS_MULTI_BUFF},
    AbnormalStatus{u"multi_debuff", ABNS_MULTI_DEBUFF},
    AbnormalStatus{u"paralyze", ABNS_PARALYZE},
    AbnormalStatus{u"pa_down", ABNS_PA_DOWN},
    AbnormalStatus{u"pa_pd_up", ABNS_PA_PD_UP},
    AbnormalStatus{u"pa_up", ABNS_PA_UP},
    AbnormalStatus{u"pa_up_herb", ABNS_PA_UP_HERB},
    AbnormalStatus{u"pa_up_special", ABNS_PA_UP_SPECIAL},
    AbnormalStatus{u"pd_down", ABNS_PD_DOWN},
    AbnormalStatus{u"pd_up", ABNS_PD_UP},
    AbnormalStatus{u"pd_up_bow", ABNS_PD_UP_BOW},
    AbnormalStatus{u"pd_up_special", ABNS_PD_UP_SPECIAL},
    AbnormalStatus{u"pinch", ABNS_PINCH},
    AbnormalStatus{u"poison", ABNS_POISON},
    AbnormalStatus{u"polearm_attack", ABNS_POLEARM_ATTACK},
    AbnormalStatus{u"possession", ABNS_POSSESSION},
    AbnormalStatus{u"preserve_abnormal", ABNS_PRESERVE_ABNORMAL},
    AbnormalStatus{u"public_slot", ABNS_PUBLIC_SLOT},
    AbnormalStatus{u"rage_might", ABNS_RAGE_MIGHT},
    AbnormalStatus{u"reduce_drop_penalty", ABNS_REDUCE_DROP_PENALTY},
    AbnormalStatus{u"reflect_abnormal", ABNS_REFLECT_ABNORMAL},
    AbnormalStatus{u"resist_bleeding", ABNS_RESIST_BLEEDING},
    AbnormalStatus{u"resist_debuff_dispel", ABNS_RESIST_DEBUFF_DISPEL},
    AbnormalStatus{u"resist_derangement", ABNS_RESIST_DERANGEMENT},
    AbnormalStatus{u"resist_holy_unholy", ABNS_RESIST_HOLY_UNHOLY},
    AbnormalStatus{u"resist_poison", ABNS_RESIST_POISON},
    AbnormalStatus{u"resist_shock", ABNS_RESIST_SHOCK},
    AbnormalStatus{u"resist_spiritless", ABNS_RESIST_SPIRITLESS},
    AbnormalStatus{u"reuse_delay_down", ABNS_REUSE_DELAY_DOWN},
    AbnormalStatus{u"reuse_delay_up", ABNS_REUSE_DELAY_UP},
    AbnormalStatus{u"root_physically", ABNS_ROOT_PHYSICALLY},
    AbnormalStatus{u"root_magically", ABNS_ROOT_MAGICALLY},
    AbnormalStatus{u"shield_defence_up", ABNS_SHIELD_DEFENCE_UP},
    AbnormalStatus{u"shield_prob_up", ABNS_SHIELD_PROB_UP},
    AbnormalStatus{u"silence", ABNS_SILENCE},
    AbnormalStatus{u"silence_all", ABNS_SILENCE_ALL},
    AbnormalStatus{u"silence_physical", ABNS_SILENCE_PHYSICAL},
    AbnormalStatus{u"sleep", ABNS_SLEEP},
    AbnormalStatus{u"snipe", ABNS_SNIPE},
    AbnormalStatus{u"song_of_champion", ABNS_SONG_OF_CHAMPION},
    AbnormalStatus{u"song_of_earth", ABNS_SONG_OF_EARTH},
    AbnormalStatus{u"song_of_flame_guard", ABNS_SONG_OF_FLAME_GUARD},
    AbnormalStatus{u"song_of_hunter", ABNS_SONG_OF_HUNTER},
    AbnormalStatus{u"song_of_invocation", ABNS_SONG_OF_INVOCATION},
    AbnormalStatus{u"song_of_life", ABNS_SONG_OF_LIFE},
    AbnormalStatus{u"song_of_meditation", ABNS_SONG_OF_MEDITATION},
    AbnormalStatus{u"song_of_renewal", ABNS_SONG_OF_RENEWAL},
    AbnormalStatus{u"song_of_storm_guard", ABNS_SONG_OF_STORM_GUARD},
    AbnormalStatus{u"song_of_vengeance", ABNS_SONG_OF_VENGEANCE},
    AbnormalStatus{u"song_of_vitality", ABNS_SONG_OF_VITALITY},
    AbnormalStatus{u"song_of_warding", ABNS_SONG_OF_WARDING},
    AbnormalStatus{u"song_of_water", ABNS_SONG_OF_WATER},
    AbnormalStatus{u"song_of_wind", ABNS_SONG_OF_WIND},
    AbnormalStatus{u"spa_disease_a", ABNS_SPA_DISEASE_A},
    AbnormalStatus{u"spa_disease_b", ABNS_SPA_DISEASE_B},
    AbnormalStatus{u"spa_disease_c", ABNS_SPA_DISEASE_C},
    AbnormalStatus{u"spa_disease_d", ABNS_SPA_DISEASE_D},
    AbnormalStatus{u"speed_down", ABNS_SPEED_DOWN},
    AbnormalStatus{u"speed_up", ABNS_SPEED_UP},
    AbnormalStatus{u"speed_up_special", ABNS_SPEED_UP_SPECIAL},
    AbnormalStatus{u"ssq_town_blessing", ABNS_SSQ_TOWN_BLESSING},
    AbnormalStatus{u"ssq_town_curse", ABNS_SSQ_TOWN_CURSE},
    AbnormalStatus{u"stealth", ABNS_STEALTH},
    AbnormalStatus{u"stun", ABNS_STUN},
    AbnormalStatus{u"thrill_fight", ABNS_THRILL_FIGHT},
    AbnormalStatus{u"touch_of_death", ABNS_TOUCH_OF_DEATH},
    AbnormalStatus{u"touch_of_life", ABNS_TOUCH_OF_LIFE},
    AbnormalStatus{u"turn_flee", ABNS_TURN_FLEE},
    AbnormalStatus{u"turn_passive", ABNS_TURN_PASSIVE},
    AbnormalStatus{u"turn_stone", ABNS_TURN_STONE},
    AbnormalStatus{u"ultimate_buff", ABNS_ULTIMATE_BUFF},
    AbnormalStatus{u"ultimate_debuff", ABNS_ULTIMATE_DEBUFF},
    AbnormalStatus{u"valakas_item", ABNS_VALAKAS_ITEM},
    AbnormalStatus{u"vampiric_attack", ABNS_VAMPIRIC_ATTACK},
    AbnormalStatus{u"watcher_gaze", ABNS_WATCHER_GAZE},
    AbnormalStatus{u"resurrection_special", ABNS_RESURRECTION_SPECIAL},
    AbnormalStatus{u"counter_skill", ABNS_COUNTER_SKILL},
    AbnormalStatus{u"avoid_skill", ABNS_AVOID_SKILL},
    AbnormalStatus{u"cp_up", ABNS_CP_UP},
    AbnormalStatus{u"cp_down", ABNS_CP_DOWN},
    AbnormalStatus{u"cp_regen_up", ABNS_CP_REGEN_UP},
    AbnormalStatus{u"cp_regen_down", ABNS_CP_REGEN_DOWN},
    AbnormalStatus{u"invincibility", ABNS_INVINCIBILITY},
    AbnormalStatus{u"abnormal_invincibility", ABNS_ABNORMAL_INVINCIBILITY},
    AbnormalStatus{u"physical_stance", ABNS_PHYSICAL_STANCE},
    AbnormalStatus{u"magical_stance", ABNS_MAGICAL_STANCE},
    AbnormalStatus{u"combination", ABNS_COMBINATION},
    AbnormalStatus{u"anesthesia", ABNS_ANESTHESIA},
    AbnormalStatus{u"critical_poison", ABNS_CRITICAL_POISON},
    AbnormalStatus{u"seizure_penalty", ABNS_SEIZURE_PENALTY},
    AbnormalStatus{u"abnormal_item", ABNS_ABNORMAL_ITEM},
    AbnormalStatus{u"seizure_a", ABNS_SEIZURE_A},
    AbnormalStatus{u"seizure_b", ABNS_SEIZURE_B},
    AbnormalStatus{u"seizure_c", ABNS_SEIZURE_C},
    AbnormalStatus{u"force_meditation", ABNS_FORCE_MEDITATION},
    AbnormalStatus{u"mirage", ABNS_MIRAGE},
    AbnormalStatus{u"potion_of_genesis", ABNS_POTION_OF_GENESIS},
    AbnormalStatus{u"pvp_dmg_up", ABNS_PVP_DMG_UP},
    AbnormalStatus{u"pvp_dmg_down", ABNS_PVP_DMG_DOWN},
    AbnormalStatus{u"iron_shield", ABNS_IRON_SHIELD},
    AbnormalStatus{u"transfer_damage", ABNS_TRANSFER_DAMAGE},
    AbnormalStatus{u"song_of_elemental", ABNS_SONG_OF_ELEMENTAL},
    AbnormalStatus{u"dance_of_alignment", ABNS_DANCE_OF_ALIGNMENT},
    AbnormalStatus{u"archer_special", ABNS_ARCHER_SPECIAL},
    AbnormalStatus{u"spoil_bomb", ABNS_SPOIL_BOMB},
    AbnormalStatus{u"fire_dot", ABNS_FIRE_DOT},
    AbnormalStatus{u"water_dot", ABNS_WATER_DOT},
    AbnormalStatus{u"wind_dot", ABNS_WIND_DOT},
    AbnormalStatus{u"earth_dot", ABNS_EARTH_DOT},
    AbnormalStatus{u"heal_power_up", ABNS_HEAL_POWER_UP},
    AbnormalStatus{u"recharge_up", ABNS_RECHARGE_UP},
    AbnormalStatus{u"normal_attack_block", ABNS_NORMAL_ATTACK_BLOCK},
    AbnormalStatus{u"disarm", ABNS_DISARM},
    AbnormalStatus{u"death_mark", ABNS_DEATH_MARK},
    AbnormalStatus{u"kamael_special", ABNS_KAMAEL_SPECIAL},
    AbnormalStatus{u"transform", ABNS_TRANSFORM},
    AbnormalStatus{u"dark_seed", ABNS_DARK_SEED},
    AbnormalStatus{u"real_target", ABNS_REAL_TARGET},
    AbnormalStatus{u"freezing", ABNS_FREEZING},
    AbnormalStatus{u"time_check", ABNS_TIME_CHECK},
    AbnormalStatus{u"ma_md_up", ABNS_MA_MD_UP},
    AbnormalStatus{u"death_clack", ABNS_DEATH_CLACK},
    AbnormalStatus{u"hot_ground", ABNS_HOT_GROUND},
    AbnormalStatus{u"evil_blood", ABNS_EVIL_BLOOD},
    AbnormalStatus{u"all_regen_up", ABNS_ALL_REGEN_UP},
    AbnormalStatus{u"all_regen_down", ABNS_ALL_REGEN_DOWN},
    AbnormalStatus{u"iron_shield_i", ABNS_IRON_SHIELD_I},
    AbnormalStatus{u"archer_special_i", ABNS_ARCHER_SPECIAL_I},
    AbnormalStatus{u"t_crt_rate_up", ABNS_T_CRT_RATE_UP},
    AbnormalStatus{u"t_crt_rate_down", ABNS_T_CRT_RATE_DOWN},
    AbnormalStatus{u"t_crt_dmg_up", ABNS_T_CRT_DMG_UP},
    AbnormalStatus{u"t_crt_dmg_down", ABNS_T_CRT_DMG_DOWN},
    AbnormalStatus{u"instinct", ABNS_INSTINCT},
    AbnormalStatus{u"oblivion", ABNS_OBLIVION},
    AbnormalStatus{u"weak_constitution", ABNS_WEAK_CONSTITUTION},
    AbnormalStatus{u"thin_skin", ABNS_THIN_SKIN},
    AbnormalStatus{u"enervation", ABNS_ENERVATION},
    AbnormalStatus{u"spite", ABNS_SPITE},
    AbnormalStatus{u"mental_impoverish", ABNS_MENTAL_IMPOVERISH},
    AbnormalStatus{u"attribute_potion", ABNS_ATTRIBUTE_POTION},
    AbnormalStatus{u"talisman", ABNS_TALISMAN},
    AbnormalStatus{u"multi_debuff_fire", ABNS_MULTI_DEBUFF_FIRE},
    AbnormalStatus{u"multi_debuff_water", ABNS_MULTI_DEBUFF_WATER},
    AbnormalStatus{u"multi_debuff_wind", ABNS_MULTI_DEBUFF_WIND},
    AbnormalStatus{u"multi_debuff_earth", ABNS_MULTI_DEBUFF_EARTH},
    AbnormalStatus{u"multi_debuff_holy", ABNS_MULTI_DEBUFF_HOLY},
    AbnormalStatus{u"multi_debuff_unholy", ABNS_MULTI_DEBUFF_UNHOLY},
    AbnormalStatus{u"life_force_kamael", ABNS_LIFE_FORCE_KAMAEL},
    AbnormalStatus{u"ma_up_special", ABNS_MA_UP_SPECIAL},
    AbnormalStatus{u"pk_protect", ABNS_PK_PROTECT},
    AbnormalStatus{u"maximum_ability", ABNS_MAXIMUM_ABILITY},
    AbnormalStatus{u"target_lock", ABNS_TARGET_LOCK},
    AbnormalStatus{u"protection", ABNS_PROTECTION},
    AbnormalStatus{u"will", ABNS_WILL},
    AbnormalStatus{u"seed_of_knight", ABNS_SEED_OF_KNIGHT},
    AbnormalStatus{u"expose_weak_point", ABNS_EXPOSE_WEAK_POINT},
    AbnormalStatus{u"force_of_destruction", ABNS_FORCE_OF_DESTRUCTION},
    AbnormalStatus{u"elemental_armor", ABNS_ELEMENTAL_ARMOR},
    AbnormalStatus{u"summon_condition", ABNS_SUMMON_CONDITION},
    AbnormalStatus{u"improve_pa_pd_up", ABNS_IMPROVE_PA_PD_UP},
    AbnormalStatus{u"improve_ma_md_up", ABNS_IMPROVE_MA_MD_UP},
    AbnormalStatus{u"improve_hp_mp_up", ABNS_IMPROVE_HP_MP_UP},
    AbnormalStatus{u"improve_crt_rate_dmg_up", ABNS_IMPROVE_CRT_RATE_DMG_UP},
    AbnormalStatus{u"improve_shield_rate_defence_up", ABNS_IMPROVE_SHIELD_RATE_DEFENCE_UP},
    AbnormalStatus{u"improve_speed_avoid_up", ABNS_IMPROVE_SPEED_AVOID_UP},
    AbnormalStatus{u"limit", ABNS_LIMIT},
    AbnormalStatus{u"multi_debuff_soul", ABNS_MULTI_DEBUFF_SOUL},
    AbnormalStatus{u"curse_life_flow", ABNS_CURSE_LIFE_FLOW},
    AbnormalStatus{u"betrayal_mark", ABNS_BETRAYAL_MARK},
    AbnormalStatus{u"transform_hangover", ABNS_TRANSFORM_HANGOVER},
    AbnormalStatus{u"transform_scrifice", ABNS_TRANSFORM_SCRIFICE},
    AbnormalStatus{u"song_of_windstorm", ABNS_SONG_OF_WINDSTORM},
    AbnormalStatus{u"dance_of_bladestorm", ABNS_DANCE_OF_BLADESTORM},
    AbnormalStatus{u"improve_vampiric_haste", ABNS_IMPROVE_VAMPIRIC_HASTE},
    AbnormalStatus{u"weapon_mastery", ABNS_WEAPON_MASTERY},
    AbnormalStatus{u"apella", ABNS_APELLA},
    AbnormalStatus{u"transform_scrifice_p", ABNS_TRANSFORM_SCRIFICE_P},
    AbnormalStatus{u"sub_trigger_haste", ABNS_SUB_TRIGGER_HASTE},
    AbnormalStatus{u"sub_trigger_defence", ABNS_SUB_TRIGGER_DEFENCE},
    AbnormalStatus{u"sub_trigger_crt_rate_up", ABNS_SUB_TRIGGER_CRT_RATE_UP},
    AbnormalStatus{u"sub_trigger_spirit", ABNS_SUB_TRIGGER_SPIRIT},
    AbnormalStatus{u"mirage_trap", ABNS_MIRAGE_TRAP},
    AbnormalStatus{u"death_penalty", ABNS_DEATH_PENALTY},
    AbnormalStatus{u"entry_for_game", ABNS_FOR_GAME},
    AbnormalStatus{u"blood_constract", ABNS_BLOOD_CONSTRACT},
    AbnormalStatus{u"dwarf_attack_buff", ABNS_DWARF_ATTACK_BUFF},
    AbnormalStatus{u"dwarf_defence_buff", ABNS_DWARF_DEFENCE_BUFF},
    AbnormalStatus{u"evasion_buff", ABNS_EVASION_BUFF},
    AbnormalStatus{u"bless_the_blood", ABNS_BLESS_THE_BLOOD},
    AbnormalStatus{u"pvp_weapon_buff", ABNS_PVP_WEAPON_BUFF},
    AbnormalStatus{u"pvp_weapon_debuff", ABNS_PVP_WEAPON_DEBUFF},
    AbnormalStatus{u"seed_of_critical", ABNS_SEED_OF_CRITICAL},
    AbnormalStatus{u"vp_up", ABNS_VP_UP},
    AbnormalStatus{u"bot_penalty", ABNS_BOT_PENALTY},
    AbnormalStatus{u"hide", ABNS_HIDE},
    AbnormalStatus{u"dd_resist", ABNS_DD_RESIST},
    AbnormalStatus{u"song_of_purification", ABNS_SONG_OF_PURIFICATION},
    AbnormalStatus{u"dance_of_berserker", ABNS_DANCE_OF_BERSERKER},
    AbnormalStatus{u"reflect_magic_dd", ABNS_REFLECT_MAGIC_DD},
    AbnormalStatus{u"final_secret", ABNS_FINAL_SECRET},
    AbnormalStatus{u"stigma_of_silen", ABNS_STIGMA_OF_SILEN},
    AbnormalStatus{u"seed_debuff_a", ABNS_SEED_DEBUFF_A},
    AbnormalStatus{u"seed_debuff_b", ABNS_SEED_DEBUFF_B},
    AbnormalStatus{u"seed_debuff_c", ABNS_SEED_DEBUFF_C},
    AbnormalStatus{u"seed_debuff_d", ABNS_SEED_DEBUFF_D},
    AbnormalStatus{u"seed_debuff_e", ABNS_SEED_DEBUFF_E},
    AbnormalStatus{u"seed_debuff_f", ABNS_SEED_DEBUFF_F},
    AbnormalStatus{u"seed_debuff_g", ABNS_SEED_DEBUFF_G},
    AbnormalStatus{u"seed_debuff_h", ABNS_SEED_DEBUFF_H},
    AbnormalStatus{u"seed_debuff_i", ABNS_SEED_DEBUFF_I},
    AbnormalStatus{u"seed_buff_a", ABNS_SEED_BUFF_A},
    AbnormalStatus{u"agathion_buff", ABNS_AGATHION_BUFF},
    AbnormalStatus{u"counter_critical", ABNS_COUNTER_CRITICAL},
    AbnormalStatus{u"counter_critical_trigger", ABNS_COUNTER_CRITICAL_TRIGGER},
    AbnormalStatus{u"attack_time_down_special", ABNS_ATTACK_TIME_DOWN_SPECIAL},
    AbnormalStatus{u"block_speed_up", ABNS_BLOCK_SPEED_UP},
    AbnormalStatus{u"block_shield_up", ABNS_BLOCK_SHIELD_UP},
    AbnormalStatus{u"deathworm", ABNS_DEATHWORM},
    AbnormalStatus{u"multi_debuff_a", ABNS_MULTI_DEBUFF_A},
    AbnormalStatus{u"multi_debuff_b", ABNS_MULTI_DEBUFF_B},
    AbnormalStatus{u"multi_debuff_c", ABNS_MULTI_DEBUFF_C},
    AbnormalStatus{u"multi_debuff_d", ABNS_MULTI_DEBUFF_D},
    AbnormalStatus{u"multi_debuff_e", ABNS_MULTI_DEBUFF_E},
    AbnormalStatus{u"multi_debuff_f", ABNS_MULTI_DEBUFF_F},
    AbnormalStatus{u"multi_debuff_g", ABNS_MULTI_DEBUFF_G},
    AbnormalStatus{u"stigma_a", ABNS_STIGMA_A},
    AbnormalStatus{u"multi_buff_a", ABNS_MULTI_BUFF_A},
    AbnormalStatus{u"vampiric_attack_special", ABNS_VAMPIRIC_ATTACK_SPECIAL},
    AbnormalStatus{u"block_resurrection", ABNS_BLOCK_RESURRECTION},
    AbnormalStatus{u"improve_hit_defence_crt_rate_up", ABNS_IMPROVE_HIT_DEFENCE_CRT_RATE_UP},
    AbnormalStatus{u"improve_magic_speed_crt_rate_up", ABNS_IMPROVE_MAGIC_SPEED_CRT_RATE_UP},
    AbnormalStatus{u"event_gawi", ABNS_EVENT_GAWI},
    AbnormalStatus{u"event_bawi", ABNS_EVENT_BAWI},
    AbnormalStatus{u"event_bo", ABNS_EVENT_BO},
    AbnormalStatus{u"event_win", ABNS_EVENT_WIN},
    AbnormalStatus{u"event_territory", ABNS_EVENT_TERRITORY},
    AbnormalStatus{u"event_santa_reward", ABNS_EVENT_SANTA_REWARD},
    AbnormalStatus{u"vp_keep", ABNS_VP_KEEP},
    AbnormalStatus{u"wp_change_event", ABNS_WP_CHANGE_WEAPON},
    AbnormalStatus{u"signal_a", ABNS_SIGNAL_A},
    AbnormalStatus{u"signal_b", ABNS_SIGNAL_B},
    AbnormalStatus{u"signal_c", ABNS_SIGNAL_C},
    AbnormalStatus{u"signal_d", ABNS_SIGNAL_D},
    AbnormalStatus{u"signal_e", ABNS_SIGNAL_E},
    AbnormalStatus{u"event_buf1", ABNS_HALLO_EVENT_BUF1},
    AbnormalStatus{u"event_buf2", ABNS_HALLO_EVENT_BUF2},
    AbnormalStatus{u"event_buf3", ABNS_HALLO_EVENT_BUF3},
    AbnormalStatus{u"event_buf4", ABNS_HALLO_EVENT_BUF4},
    AbnormalStatus{u"event_buf5", ABNS_HALLO_EVENT_BUF5},
    AbnormalStatus{u"event_buf6", ABNS_HALLO_EVENT_BUF6},
    AbnormalStatus{u"event_buf7", ABNS_HALLO_EVENT_BUF7},
    AbnormalStatus{u"event_buf8", ABNS_HALLO_EVENT_BUF8},
    AbnormalStatus{u"event_buf9", ABNS_HALLO_EVENT_BUF9},
    AbnormalStatus{u"event_buf10", ABNS_HALLO_EVENT_BUF10},
    AbnormalStatus{u"change_attr_w", ABNS_CHANGE_ATTR_W},
    AbnormalStatus{u"chagne_attr_a", ABNS_CHANGE_ATTR_A},
    AbnormalStatus{u"soa_buff1", ABNS_SOA_BUFF1},
    AbnormalStatus{u"soa_buff2", ABNS_SOA_BUFF2},
    AbnormalStatus{u"soa_buff3", ABNS_SOA_BUFF3},
    AbnormalStatus{u"damage_amplify", ABNS_DAMAGE_AMPLIFY},
    AbnormalStatus{u"vibration", ABNS_VIBRATION},
    AbnormalStatus{u"block_transform", ABNS_BLOCK_TRANSFORM},
    AbnormalStatus{u"skill_ignore", ABNS_SKILL_IGNORE},
    AbnormalStatus{u"signal_g", ABNS_SIGNAL_G},
    AbnormalStatus{u"knight_aura", ABNS_KNIGHT_AURA},
    AbnormalStatus{u"patience", ABNS_PATIENCE},
    AbnormalStatus{u"vote", ABNS_VOTE},
    AbnormalStatus{u"mp_shield", ABNS_MP_SHIELD},
    AbnormalStatus{u"vp_change", ABNS_VP_CHANGE},
    AbnormalStatus{u"ability_change", ABNS_ABILITY_CHANGE},
    AbnormalStatus{u"wispering_of_battle", ABNS_WISPERING_OF_BATTLE},
    AbnormalStatus{u"motion_of_defence", ABNS_MOTION_OF_DEFENCE},
    AbnormalStatus{u"armor_element_all", ABNS_ARMOR_ELEMENT_ALL},
    AbnormalStatus{u"morale_up", ABNS_MORALE_UP},
    AbnormalStatus{u"time_bomb", ABNS_TIME_BOMB},
    AbnormalStatus{u"knight_shield", ABNS_KNIGHT_SHIELD},
    AbnormalStatus{u"airbind", ABNS_AIRBIND},
    AbnormalStatus{u"changebody", ABNS_CHANGEBODY},
    AbnormalStatus{u"knockdown", ABNS_KNOCKDOWN},
    AbnormalStatus{u"max_hp_up_k", ABNS_MAX_HP_UP_K},
    AbnormalStatus{u"br_event_buf1", ABNS_BR_EVENT_BUF1},
    AbnormalStatus{u"br_event_buf2", ABNS_BR_EVENT_BUF2},
    AbnormalStatus{u"br_event_buf3", ABNS_BR_EVENT_BUF3},
    AbnormalStatus{u"br_event_buf4", ABNS_BR_EVENT_BUF4},
    AbnormalStatus{u"br_event_buf5", ABNS_BR_EVENT_BUF5},
    AbnormalStatus{u"br_event_buf6", ABNS_BR_EVENT_BUF6},
    AbnormalStatus{u"br_event_buf7", ABNS_BR_EVENT_BUF7},
    AbnormalStatus{u"br_event_buf8", ABNS_BR_EVENT_BUF8},
    AbnormalStatus{u"br_event_buf9", ABNS_BR_EVENT_BUF9},
    AbnormalStatus{u"br_event_buf10", ABNS_BR_EVENT_BUF10},
    AbnormalStatus{u"possession_special", ABNS_POSSESSION_SPECIAL},
    AbnormalStatus{u"deport", ABNS_DEPORT},
    AbnormalStatus{u"force_hp_up", ABNS_FORCE_HP_HP},
    AbnormalStatus{u"special_berserker", ABNS_SPECIAL_BERSERKER},
    AbnormalStatus{u"battle_cry", ABNS_BATTLE_CRY},
    AbnormalStatus{u"avoid_skill_special", ABNS_AVOID_SKILL_SPECIAL},
    AbnormalStatus{u"super_move", ABNS_SUPER_MOVE},
    AbnormalStatus{u"confusion", ABNS_CONFUSION},
    AbnormalStatus{u"super_buff", ABNS_SUPER_BUFF},
    AbnormalStatus{u"super_avoid", ABNS_SUPER_AVOID},
    AbnormalStatus{u"movement", ABNS_MOVEMENT},
    AbnormalStatus{u"poison_self", ABNS_POISON_SELF},
    AbnormalStatus{u"t_crt_all", ABNS_T_CRY_ALL},
    AbnormalStatus{u"critical_special", ABNS_CRITICAL_SPECIAL},
    AbnormalStatus{u"weapon_master_special", ABNS_WEAPON_MASTER_SPECIAL},
    AbnormalStatus{u"class_change", ABNS_CLASS_CHANGE},
    AbnormalStatus{u"dc_mod", ABNS_DC_MOD},
    AbnormalStatus{u"absorb", ABNS_ABSORB},
    AbnormalStatus{u"enchanter_mod", ABNS_ENCHANTER_MOD},
    AbnormalStatus{u"final_buff", ABNS_FINAL_BUFF},
    AbnormalStatus{u"final_debuff", ABNS_FINAL_DEBUFF},
    AbnormalStatus{u"shield_attack", ABNS_SHIELD_ATTACK},
    AbnormalStatus{u"sacrifice", ABNS_SACRIFICE},
    AbnormalStatus{u"aura", ABNS_AURA},
    AbnormalStatus{u"life_force_healer", ABNS_LIFE_FORCE_HEALER},
    AbnormalStatus{u"life_force_healer_self", ABNS_LIFE_FORCE_HEALER_SELF},
    AbnormalStatus{u"turn_crystal", ABNS_TURN_CRYSTAL},
    AbnormalStatus{u"mark_of_lumi", ABNS_MARK_OF_LUMI},
    AbnormalStatus{u"energy_of_totem_1", ABNS_ENERGY_OF_TOTEM_1},
    AbnormalStatus{u"energy_of_totem_2", ABNS_ENERGY_OF_TOTEM_2},
    AbnormalStatus{u"energy_of_totem_3", ABNS_ENERGY_OF_TOTEM_3},
    AbnormalStatus{u"energy_of_totem_4", ABNS_ENERGY_OF_TOTEM_4},
    AbnormalStatus{u"death_penalty_gd", ABNS_DEATH_PENALTY_GD},
    AbnormalStatus{u"all_abns", ABNS_ALL},
    AbnormalStatus{u"debuff_special_attack", ABNS_DEBUFF_SPECIAL_ATTACK},
    AbnormalStatus{u"debuff_special_defence", ABNS_DEBUFF_SPECIAL_DEFENCE},
    AbnormalStatus{u"buff_special_attack", ABNS_BUFF_SPECIAL_ATTACK},
    AbnormalStatus{u"buff_special_defence", ABNS_BUFF_SPECIAL_DEFENCE},
    AbnormalStatus{u"buff_special_condition", ABNS_BUFF_SPECIAL_CONDITION},
    AbnormalStatus{u"buff_special_critical", ABNS_BUFF_SPECIAL_CRITICAL},
    AbnormalStatus{u"buff_special_hitavoid", ABNS_BUFF_SPECIAL_HITAVOID},
    AbnormalStatus{u"buff_special_move", ABNS_BUFF_SPECIAL_MOVE},
    AbnormalStatus{u"buff_special_class", ABNS_BUFF_SPECIAL_CLASS},
    AbnormalStatus{u"buff_special_aura", ABNS_BUFF_SPECIAL_AURA},
    AbnormalStatus{u"buff_special_multi", ABNS_BUFF_SPECIAL_MULTI},
    AbnormalStatus{u"earthworm_debuff", ABNS_EARTHWORM_DEBUFF},
    AbnormalStatus{u"special_ride", ABNS_SPECIAL_RIDE},
    AbnormalStatus{u"spirit_sharing", ABNS_SPIRIT_SHARING},
    AbnormalStatus{u"substitute_buff", ABNS_SUBSTITUTE_BUFF},
    AbnormalStatus{u"anti_summon", ABNS_ANTI_SUMMON},
    AbnormalStatus{u"npc_fury", ABNS_NPC_FURY},
    AbnormalStatus{u"pet_fury", ABNS_PET_FURY},
    AbnormalStatus{u"clan_advent", ABNS_CLAN_ADVENT},
    AbnormalStatus{u"death_penalty_block", ABNS_DEATH_PENALTY_BLOCK},
    AbnormalStatus{u"synergy_sigel", ABNS_SYNERGY_SIGEL},
    AbnormalStatus{u"synergy_tir", ABNS_SYNERGY_TIR},
    AbnormalStatus{u"synergy_othel", ABNS_SYNERGY_OTHEL},
    AbnormalStatus{u"synergy_yr", ABNS_SYNERGY_YR},
    AbnormalStatus{u"synergy_feoh", ABNS_SYNERGY_FEOH},
    AbnormalStatus{u"synergy_is", ABNS_SYNERGY_IS},
    AbnormalStatus{u"synergy_wynn", ABNS_SYNERGY_WYNN},
    AbnormalStatus{u"synergy_eolh", ABNS_SYNERGY_EOLH},
    AbnormalStatus{u"mark_debuf_a", ABNS_MARK_DEBUF_A},
    AbnormalStatus{u"mark_debuf_b", ABNS_MARK_DEBUF_B},
    AbnormalStatus{u"mark_debuf_c", ABNS_MARK_DEBUF_C},
    AbnormalStatus{u"mark_debuf_d", ABNS_MARK_DEBUF_D},
    AbnormalStatus{u"academy_up", ABNS_ACADEMY_UP},
    AbnormalStatus{u"clan_friend", ABNS_CLAN_FRIEND},
    AbnormalStatus{u"clan_boundary", ABNS_CLAN_BOUNDARY},
    AbnormalStatus{u"clan_prison", ABNS_CLAN_PRISON},
    AbnormalStatus{u"race_human1", ABNS_RACE_HUMAN1},
    AbnormalStatus{u"race_elf1", ABNS_RACE_ELF1},
    AbnormalStatus{u"race_darkelf1", ABNS_RACE_DARKELF1},
    AbnormalStatus{u"race_orc1", ABNS_RACE_ORC1},
    AbnormalStatus{u"race_orc2", ABNS_RACE_ORC2},
    AbnormalStatus{u"race_dwaf1", ABNS_RACE_DWAF1},
    AbnormalStatus{u"race_dwaf2", ABNS_RACE_DWAF2},
    AbnormalStatus{u"race_kamael1", ABNS_RACE_KAMAEL1},
    AbnormalStatus{u"npc_attack1", ABNS_NPC_ATTACK1},
    AbnormalStatus{u"npc_attack2", ABNS_NPC_ATTACK2},
    AbnormalStatus{u"npc_attack3", ABNS_NPC_ATTACK3},
    AbnormalStatus{u"weakened_death_penalty", ABNS_WEAKENED_DEATH_PENALTY},
    AbnormalStatus{u"vp_mentor_rune", ABNS_VP_MENTOR_RUNE},
    AbnormalStatus{u"invincibility_special", ABNS_INVINCIBILITY_SPECIAL},
    AbnormalStatus{u"buff_mentee1", ABNS_BUFF_MENTEE1},
    AbnormalStatus{u"buff_pccafe_exp1", ABNS_BUFF_PCCAFE_EXP1},
    AbnormalStatus{u"synergy_party_buf", ABNS_SYNERGY_PARTY_BUF},
    AbnormalStatus{u"notice_portal", ABNS_NOTICE_PORTAL},
    AbnormalStatus{u"exp_bottle", ABNS_EXP_BOTTLE},
    AbnormalStatus{u"resist_death", ABNS_RESIST_DEATH},
    AbnormalStatus{u"rhapsody", ABNS_BUFF_RHAPSODY},
    AbnormalStatus{u"nochat", ABNS_NO_CHAT},
    AbnormalStatus{u"flag_buf", ABNS_FLAG_BUF},
    AbnormalStatus{u"flag_debuf", ABNS_FLAG_DEBUF},
    AbnormalStatus{u"buff_tree", ABNS_BUFF_TREE},
    AbnormalStatus{u"instant_ev_buff1", ABNS_INSTANT_EVENT_BUFF1},
    AbnormalStatus{u"instant_ev_buff2", ABNS_INSTANT_EVENT_BUFF2},
    AbnormalStatus{u"instant_ev_buff3", ABNS_INSTANT_EVENT_BUFF3},
    AbnormalStatus{u"instant_ev_buff4", ABNS_INSTANT_EVENT_BUFF4},
    AbnormalStatus{u"instant_ev_buff5", ABNS_INSTANT_EVENT_BUFF5},
    AbnormalStatus{u"instant_ev_buff6", ABNS_INSTANT_EVENT_BUFF6},
    AbnormalStatus{u"seed_talisman1", ABNS_SEED_TALISMAN1},
    AbnormalStatus{u"seed_talisman2", ABNS_SEED_TALISMAN2},
    AbnormalStatus{u"seed_talisman3", ABNS_SEED_TALISMAN3},
    AbnormalStatus{u"seed_talisman4", ABNS_SEED_TALISMAN4},
    AbnormalStatus{u"seed_talisman5", ABNS_SEED_TALISMAN5},
    AbnormalStatus{u"seed_talisman6", ABNS_SEED_TALISMAN6},
    AbnormalStatus{u"lumiere_buff", ABNS_LUMIERE_BUFF},
    AbnormalStatus{u"brightness_bless", ABNS_BRIGHTNESS_BLESS},
    AbnormalStatus{u"curious_house", ABNS_CURIOUS_HOUSE},
    AbnormalStatus{u"special_move_up", ABNS_SPECIAL_MOVE_UP},
    AbnormalStatus{u"star_agathion_exp_sp_buff1", ABNS_STAR_AGATHION_EXP_SP_BUFF1},
    AbnormalStatus{u"npc_multi_buff1", ABNS_NPC_MULTI_BUFF1},
    AbnormalStatus{u"dual_max_body", ABNS_DUAL_MAX_BODY},
    AbnormalStatus{u"dual_attack_up", ABNS_DUAL_ATTACK_UP},
    AbnormalStatus{u"dual_skill_up", ABNS_DUAL_SKILL_UP},
    AbnormalStatus{u"dual_defence_up", ABNS_DUAL_DEFENCE_UP},
    AbnormalStatus{u"dual_dmg_shield", ABNS_DUAL_DMG_SHIELD},
    AbnormalStatus{u"kalie_buff", ABNS_KALIE_BUFF},
    AbnormalStatus{u"buff_special_exp", ABNS_BUFF_SPECIAL_EXP},
    AbnormalStatus{u"display_debuff1", ABNS_DISPLAY_DEBUFF1},
    AbnormalStatus{u"npc_buff1", ABNS_NPC_BUFF1},
    AbnormalStatus{u"soul_shield", ABNS_SOUL_SHIELD},
    AbnormalStatus{u"br_uthanka_buff", ABNS_UTHANKA_BUFF},
    AbnormalStatus{u"knight_buff1", ABNS_KNIGHT_BUFF1},
    AbnormalStatus{u"knight_buff2", ABNS_KNIGHT_BUFF2},
    AbnormalStatus{u"knight_buff3", ABNS_KNIGHT_BUFF3},
    AbnormalStatus{u"knight_buff4", ABNS_KNIGHT_BUFF4},
    AbnormalStatus{u"provoke", ABNS_PROVOKE},
    AbnormalStatus{u"archer_stance", ABNS_ARCHER_STANCE},
    AbnormalStatus{u"rage_buff", ABNS_RAGE_BUFF},
    AbnormalStatus{u"rogue_might_mortal", ABNS_ROGUE_MIGHT_MORTAL},
    AbnormalStatus{u"wizard_buff1", ABNS_WIZARD_BUFF1},
    AbnormalStatus{u"wizard_buff2", ABNS_WIZARD_BUFF2},
    AbnormalStatus{u"wizard_buff3", ABNS_WIZARD_BUFF3},
    AbnormalStatus{u"wizard_buff4", ABNS_WIZARD_BUFF4},
    AbnormalStatus{u"archer_buff1", ABNS_ARCHER_BUFF1},
    AbnormalStatus{u"archer_buff2", ABNS_ARCHER_BUFF2},
    AbnormalStatus{u"archer_buff3", ABNS_ARCHER_BUFF3},
    AbnormalStatus{u"archer_buff4", ABNS_ARCHER_BUFF4},
    AbnormalStatus{u"summon_buff1", ABNS_SUMMON_BUFF1},
    AbnormalStatus{u"summon_buff2", ABNS_SUMMON_BUFF2},
    AbnormalStatus{u"summon_buff3", ABNS_SUMMON_BUFF3},
    AbnormalStatus{u"summon_buff4", ABNS_SUMMON_BUFF4},
    AbnormalStatus{u"premium_buff", ABNS_PREMIUM_BUFF},
    AbnormalStatus{u"wp_change_event1", ABNS_WP_CHANGE_EVENT1},
    AbnormalStatus{u"lind_abnormal1", ABNS_LIND_ABNORMAL1},
    AbnormalStatus{u"lind_abnormal2", ABNS_LIND_ABNORMAL2},
    AbnormalStatus{u"lind_abnormal3", ABNS_LIND_ABNORMAL3},
    AbnormalStatus{u"field_raid_buff1", ABNS_FIELD_RAID_BUFF1},
    AbnormalStatus{u"pd_up_dmagic", ABNS_UP_DMAGIC},
    AbnormalStatus{u"legend_card_event", ABNS_LEGEND_CART_EVENT},
    AbnormalStatus{u"system_event_buf1", ABNS_SYSTEM_EVENT_BUF1},
    AbnormalStatus{u"system_event_buf2", ABNS_SYSTEM_EVENT_BUF2},
    AbnormalStatus{u"system_event_buf3", ABNS_SYSTEM_EVENT_BUF3},
    AbnormalStatus{u"system_event_buf4", ABNS_SYSTEM_EVENT_BUF4},
    AbnormalStatus{u"system_event_buf5", ABNS_SYSTEM_EVENT_BUF5},
    AbnormalStatus{u"rogue_debuff1", ABNS_ROGUE_DEBUFF1},
    AbnormalStatus{u"power_of_adven", ABNS_POWER_OF_ADVEN},
    AbnormalStatus{u"power_of_ghost", ABNS_POWER_OF_GHOST},
    AbnormalStatus{u"chance_of_wind", ABNS_CHANCE_OF_WIND},
    AbnormalStatus{u"knight_debuff1", ABNS_KNIGHT_DEBUFF1},
    AbnormalStatus{u"knight_debuff2", ABNS_KNIGHT_DEBUFF2},
    AbnormalStatus{u"warrior_buff1", ABNS_WARRIOR_BUFF1},
    AbnormalStatus{u"warrior_buff2", ABNS_WARRIOR_BUFF2},
    AbnormalStatus{u"warrior_debuff1", ABNS_WARRIOR_DEBUFF1},
    AbnormalStatus{u"warrior_debuff2", ABNS_WARRIOR_DEBUFF2},
    AbnormalStatus{u"thirst_desert", ABNS_THIRST_DESERT},
    AbnormalStatus{u"star_gamble", ABNS_STAR_GAMBLE},
    AbnormalStatus{u"dual_buff", ABNS_DUAL_BUFF},
    AbnormalStatus{u"dual_debuff", ABNS_DUAL_DEBUFF},
    AbnormalStatus{u"cubic_buff1", ABNS_CUBIC_BUFF1},
    AbnormalStatus{u"star_gamble2", ABNS_STAR_GAMBLE2},
    AbnormalStatus{u"special_potion1", ABNS_SPECIAL_POTION1},
    AbnormalStatus{u"special_potion2", ABNS_SPECIAL_POTION2},
    AbnormalStatus{u"special_potion3", ABNS_SPECIAL_POTION3},
    AbnormalStatus{u"system_event_buf6", ABNS_SYSTEM_EVENT_BUF6},
    AbnormalStatus{u"fishing_use", ABNS_FISHING_USE},
    AbnormalStatus{u"fishing_bless", ABNS_FISHING_BLESS},
    AbnormalStatus{u"fishing_food", ABNS_FISHING_FOOD},
    AbnormalStatus{u"raid_blessing", ABNS_RAID_BLESSING},
    AbnormalStatus{u"ex_raid_blessing", ABNS_EX_RAID_BLESSING},
    AbnormalStatus{u"bluff", ABNS_BLUFF},
    AbnormalStatus{u"tir_buffing1", ABNS_TIR_BUFFING1},
    AbnormalStatus{u"tir_buffing2", ABNS_TIR_BUFFING2},
    AbnormalStatus{u"sigel_buffing1", ABNS_SIGEL_BUFFING1},
    AbnormalStatus{u"sigel_buffing2", ABNS_SIGEL_BUFFING2},
    AbnormalStatus{u"other_buffing1", ABNS_OTHER_BUFFING1},
    AbnormalStatus{u"other_buffing2", ABNS_OTHER_BUFFING2},
    AbnormalStatus{u"yir_buffing1", ABNS_YIR_BUFFING1},
    AbnormalStatus{u"yir_buffing2", ABNS_YIR_BUFFING2},
    AbnormalStatus{u"feo_buffing1", ABNS_FEO_BUFFING1},
    AbnormalStatus{u"feo_buffing2", ABNS_FEO_BUFFING2},
    AbnormalStatus{u"wynn_buffing1", ABNS_WYNN_BUFFING1},
    AbnormalStatus{u"wynn_buffing2", ABNS_WYNN_BUFFING2},
    AbnormalStatus{u"wynn_buffing3", ABNS_WYNN_BUFFING3},
    AbnormalStatus{u"eolh_buffing1", ABNS_EOLH_BUFFING1},
    AbnormalStatus{u"eolh_buffing2", ABNS_EOLH_BUFFING2},
    AbnormalStatus{u"eolh_buffing3", ABNS_EOLH_BUFFING3},
    AbnormalStatus{u"is_buffing1", ABNS_IS_BUFFING1},
    AbnormalStatus{u"psychic_power", ABNS_PSYCHIC_POWER},
    AbnormalStatus{u"sigh_of_storm", ABNS_SIGH_OF_STORM},
    AbnormalStatus{u"wind_hide", ABNS_WIND_HIDE},
    AbnormalStatus{u"storm_eye", ABNS_STORM_EYE},
    AbnormalStatus{u"ultimate_squall", ABNS_ULTIMATE_SQUALL},
    AbnormalStatus{u"sayha_buff", ABNS_SAYHA_BUFF},
    AbnormalStatus{u"sayha_party_buff", ABNS_SAYHA_PARTY_BUFF},
    AbnormalStatus{u"menace_multi_debuff", ABNS_MENACE_MULTI_DEBUFF},
    AbnormalStatus{u"psychic_debuff", ABNS_PSYCHIC_DEBUFF},
    AbnormalStatus{u"wind_stun", ABNS_WIND_STUN},
    AbnormalStatus{u"synergy_lenker", ABNS_SYNERGY_LENKER},
    AbnormalStatus{u"synergy_seer", ABNS_SYNERGY_SEER},
    AbnormalStatus{u"gravity_power", ABNS_GRAVITY_POWER},
    AbnormalStatus{u"distortion", ABNS_DISTORTION},
    AbnormalStatus{u"gravity_debuff", ABNS_GRAVITY_DEBUFF},
    AbnormalStatus{u"gravity_multi_debuff", ABNS_GRAVITY_MULTI_DEBUFF},
    AbnormalStatus{u"speed_down_special", ABNS_SPEED_DOWN_SPECIAL},
    AbnormalStatus{u"speical_spirit", ABNS_SPEICAL_SPIRIT},
    AbnormalStatus{u"gravity_space", ABNS_GRAVITY_SPACE},
    AbnormalStatus{u"buff_special_exp_vital", ABNS_BUFF_SPECIAL_EXP_VITAL},
    AbnormalStatus{u"runway_armor", ABNS_ARMOR_VISUAL_EFFECT},
    AbnormalStatus{u"runway_weapon", ABNS_WEAPON_VISUAL_EFFECT},
    AbnormalStatus{u"g_ev_buff1", ABNS_INVITE_COOKIE_BUFF1},
    AbnormalStatus{u"vp_recover_boost", ABNS_VP_RECOVER_BOOST},
    AbnormalStatus{u"add_vital_bonus", ABNS_ADD_VITAL_BONUS},
    AbnormalStatus{u"vp_consume_reduce", ABNS_VP_CONSUME_REDUCE},
    AbnormalStatus{u"blocked_invincibility", ABNS_BLOCKED_INVINCIBILITY},
    AbnormalStatus{u"battle_tolerance", ABNS_BATTLE_TOLERANCE},
    AbnormalStatus{u"mysterious_dye", ABNS_MYSTERIOUS_DYE},
    AbnormalStatus{u"richness_talisman", ABNS_RICHNESS_TALISMAN},
    AbnormalStatus{u"sayha_debuff", ABNS_SAYHA_DEBUFF},
    AbnormalStatus{u"enchanter_might", ABNS_ENCHANTER_MIGHT},
    AbnormalStatus{u"enchanter_shield", ABNS_ENCHANTER_SHIELD},
    AbnormalStatus{u"enchanter_trait_buff1", ABNS_ENCHANTER_TRAIT_BUFF1},
    AbnormalStatus{u"enchanter_trait_buff2", ABNS_ENCHANTER_TRAIT_BUFF2},
    AbnormalStatus{u"enchanter_debuff1", ABNS_ENCHANTER_DEBUFF1},
    AbnormalStatus{u"enchanter_debuff2", ABNS_ENCHANTER_DEBUFF2},
    AbnormalStatus{u"healer_protection", ABNS_HEALER_PROTECTION},
    AbnormalStatus{u"healer_buffing", ABNS_HEALER_BUFFING},
    AbnormalStatus{u"healer_buff1", ABNS_HEALER_BUFF1},
    AbnormalStatus{u"healer_debuff1", ABNS_HEALER_DEBUFF1},
    AbnormalStatus{u"ability_change_itemskill", ABNS_ABILITY_CHANGE_ITEMSKILL},
    AbnormalStatus{u"prevention_death", ABNS_PREVENTION_DEATH},
    AbnormalStatus{u"absorb_shield", ABNS_ABSORB_SHIELD},
    AbnormalStatus{u"disrobe", ABNS_DISROBE},
    AbnormalStatus{u"dual_buff2", ABNS_DUAL_BUFF2},
    AbnormalStatus{u"br_pccafe_buff_j", ABNS_BR_PCCAFE_BUFF_J},
    AbnormalStatus{u"dmg_shield1", ABNS_DMG_SHIELD1},
    AbnormalStatus{u"assimilate_penalty", ABNS_ASSIMILATE_PENALTY},
    AbnormalStatus{u"g_lookchange_buff1", ABNS_LOOKCHANGE_BUFF1},
    AbnormalStatus{u"summoner_bigbody1", ABNS_SUMMONER_BIGBODY1},
    AbnormalStatus{u"summon_master_buff1", ABNS_SUMMON_MASTER_BUFF1},
    AbnormalStatus{u"common_bigbody1", ABNS_COMMON_BIGBODY1},
    AbnormalStatus{u"wizard_trigger_buff1", ABNS_WIZARD_TRIGGER_BUFF1},
    AbnormalStatus{u"t_blow_rate_up", ABNS_T_BLOW_RATE_UP},
    AbnormalStatus{u"all_defence_down", ABNS_ALL_DEFENCE_DOWN},
    AbnormalStatus{u"n20_free_buff01", ABNS_N20_FREE_BUFF01},
    AbnormalStatus{u"n20_free_buff02", ABNS_N20_FREE_BUFF02},
    AbnormalStatus{u"n20_free_buff03", ABNS_N20_FREE_BUFF03},
    AbnormalStatus{u"n20_buff_vital", ABNS_N20_BUFF_VITAL},
    AbnormalStatus{u"n20_buff_battle01", ABNS_N20_BUFF_BATTLE01},
    AbnormalStatus{u"n20_buff_battle02", ABNS_N20_BUFF_BATTLE02},
    AbnormalStatus{u"n20_buff_battle03", ABNS_N20_BUFF_BATTLE03},
    AbnormalStatus{u"n20_buff_world_chat_point", ABNS_N20_BUFF_WORLD_CHAT_POINT},
    AbnormalStatus{u"clan_bonus_buff1", ABNS_CLAN_BONUS_BUFF1},
    AbnormalStatus{u"buff_pccafe_exp2", ABNS_BUFF_PCCAFE_EXP2},
    AbnormalStatus{u"knight_debuff_aura1", ABNS_KNIGHT_DEBUFF_AURA1},
    AbnormalStatus{u"knight_debuff_aura2", ABNS_KNIGHT_DEBUFF_AURA2},
};

CParserForSkill::AbnormalMap CParserForSkill::s_AbnormalMap;
CParserForSkill::AlterSkillList CParserForSkill::m_AlterSkillVector;
CParserForSkill::AlterSkillList CParserForSkill::m_TriggerAlterSkillVector;

void CParserForSkill::PostAlterSkillLoad() {
    for (CSkillInfo* const originalSkill : m_AlterSkillVector) {
        originalSkill->m_IsOriginAlterSkill = true;

        const CSkillInfo::AbnormalAlterSkillMap alterSkillList = originalSkill->m_AlterSkillList;
        for (const auto& entry : alterSkillList) {
            const std::vector<AlterSkillInfo*> skillList = entry.second;
            for (AlterSkillInfo* const alterSkill : skillList) {
                const std::wstring skillName = Utf16ToWide(alterSkill->skillName);
                CSkillInfo* const resolvedSkill = const_cast<CSkillInfo*>(g_SkillDB.GetSkillInfoByName(skillName.c_str()));
                alterSkill->pSkillInfo = resolvedSkill;
                alterSkill->pOriSkillInfo = originalSkill;
                resolvedSkill->m_IsAlterSkill = true;
            }
        }
    }

    for (CSkillInfo* const originalSkill : m_TriggerAlterSkillVector) {
        const std::vector<AlterSkillInfo*> skillList = originalSkill->m_TriggerAlterSkillInfo.skillList;
        for (AlterSkillInfo* const alterSkill : skillList) {
            const std::wstring skillName = Utf16ToWide(alterSkill->skillName);
            CSkillInfo* const resolvedSkill = const_cast<CSkillInfo*>(g_SkillDB.GetSkillInfoByName(skillName.c_str()));
            alterSkill->pSkillInfo = resolvedSkill;
            alterSkill->pOriSkillInfo = nullptr;
            resolvedSkill->m_IsAlterSkill = true;
        }
    }
}

namespace skill_parser_detail {

bool CheckAbnormalStatusSlotType() {
    bool result = true;
    for (std::size_t index = 0; index < ABNS_MAX; ++index) {
        const AbnormalStatus& abnormal = CParserForSkill::s_AbnormalTypes[index];
        if (abnormal.m_Name == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"s_AbnormalTypes[%d] is null!!!!",
            //     static_cast<int>(index))
            // L2_LOG_ERROR(Log,
            //     L"s_AbnormalTypes[%d] is null!!!!",
            //     static_cast<int>(index))
            L2_LOG_ERROR(Log, u"[skills] Abnormal-status table entry %d has no name.", static_cast<int>(index));
            result = false;
        } else if (static_cast<int>(abnormal.m_Type) != static_cast<int>(index)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"AbnormalStatusSlotType (%d) and s_AbnormalTypes (%d), different order!!!!",
            //     static_cast<int>(index),
            //     static_cast<int>(abnormal.m_Type))
            // L2_LOG_ERROR(Log,
            //     L"AbnormalStatusSlotType (%d) and s_AbnormalTypes (%d), different order!!!!",
            //     static_cast<int>(index),
            //     static_cast<int>(abnormal.m_Type))
            L2_LOG_ERROR(
                Log, u"[skills] Abnormal-status table entry %d declares type %d; the type must match the entry index.",
                static_cast<int>(index), static_cast<int>(abnormal.m_Type)
            );
            result = false;
        }
    }
    return result;
}

void InitializeAbnormalStatusMap() {
    if (!CheckAbnormalStatusSlotType()) {
        L2_LOG_ERROR(
            Log,
            u"[skills] Abnormal-status table validation failed; initialization of the name map "
            u"will continue with the current entries."
        );
        // Noise: The error is recorded in the main log. Retain only the Windows modal copy.
        // PopUpWindow(
        //     L"CheckAbnormalStatusSlotType is failed.\n"
        //     L"Check AbnormalStatusSlotType and s_AbnormalTypes")
#if defined(_WIN32)
        PopUpWindow(
            L"CheckAbnormalStatusSlotType is failed.\n"
            L"Check AbnormalStatusSlotType and s_AbnormalTypes"
        );
#endif
    }

    for (const AbnormalStatus& abnormal : CParserForSkill::s_AbnormalTypes) {
        CParserForSkill::s_AbnormalMap[std::u16string(abnormal.m_Name)] = abnormal.m_Type;
    }
}

AbnormalStatusSlotType FindAbnormalByName(const char16_t* name, int line) {
    const auto found = CParserForSkill::s_AbnormalMap.find(std::u16string(name));
    if (found == CParserForSkill::s_AbnormalMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"%s is not in abnormal map at [%d]",
        //     name,
        //     line)
        // L2_LOG_ERROR(Log,
        //     L"%s is not in abnormal map at [%d]",
        //     name,
        //     line)
        L2_LOG_ERROR(
            Log,
            line > 0 ? "skilldata.txt line %d: unknown abnormal slot '%s'; returning unresolved slot %d."
                     : "skilldata.txt (line unavailable: %d): unknown abnormal slot '%s'; returning unresolved slot %d.",
            line, Utf16ToUtf8(name).c_str(), static_cast<int>(ABNS_MAX)
        );
        return ABNS_MAX;
    }
    return found->second;
}

} // namespace skill_parser_detail

CParserForSkill::i_call_my_skillNode::i_call_my_skillNode(int skillId) : id(skillId), fromList(), inStack(false) {}

CParserForSkill::i_call_my_skillNode::i_call_my_skillNode(int skillId, i_call_my_skillNode& from) :
    id(skillId), fromList(), inStack(false) {
    fromList.push_back(&from);
}

void CParserForSkill::i_call_my_skillNode::updateFromList(i_call_my_skillNode& from) { fromList.push_back(&from); }

bool CParserForSkill::CheckAbnormalStatusSlotType() { return skill_parser_detail::CheckAbnormalStatusSlotType(); }

int CParserForSkill::Create(char16_t* fileBuffer, int fileSize) {
    skill_parser_detail::InitializeAbnormalStatusMap();
    return m_lexer.Create(fileBuffer, fileSize);
}

AbnormalStatusSlotType CParserForSkill::FindAbnormalByName(const char16_t* name, int line) {
    return skill_parser_detail::FindAbnormalByName(name, line);
}

bool CParserForSkill::IsLower(const char16_t* name, int line) {
    const std::u16string_view value(name);
    if (value == u"down") {
        return true;
    }
    if (value == u"up") {
        return false;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"%s should be between down or up. at [%d]",
    //     name,
    //     line)
    // L2_LOG_ERROR(Log,
    //     L"%s should be between down or up. at [%d]",
    //     name,
    //     line)
    L2_LOG_ERROR(
        Log,
        line > 0 ? "skilldata.txt line %d: unknown direction '%s'; expected down or up. Using down."
                 : "skilldata.txt (line unavailable: %d): unknown direction '%s'; expected down or up. Using down.",
        line, Utf16ToUtf8(name).c_str()
    );
    return true;
}

CCreatureSkillMod::SummonStatBonusMod CParserForSkill::GetModIndexFromModName(const char16_t* name) {
    const std::u16string_view value(name);
    if (value == u"pattack") {
        return CCreatureSkillMod::SBMN_PAttack;
    }
    if (value == u"pdefend") {
        return CCreatureSkillMod::SBMN_PDefend;
    }
    if (value == u"mattack") {
        return CCreatureSkillMod::SBMN_MAttack;
    }
    if (value == u"mdefend") {
        return CCreatureSkillMod::SBMN_MDefend;
    }
    if (value == u"hp") {
        return CCreatureSkillMod::SBMN_HP;
    }
    if (value == u"mp") {
        return CCreatureSkillMod::SBMN_MP;
    }
    if (value == u"pattackspeed") {
        return CCreatureSkillMod::SBMN_PAttackSpeed;
    }
    if (value == u"musespeed") {
        return CCreatureSkillMod::SBMN_MUseSpeed;
    }
    if (value == u"speed") {
        return CCreatureSkillMod::SBMN_Speed;
    }
    if (value == u"criticalprob") {
        return CCreatureSkillMod::SBMN_CriticalProb;
    }
    if (value == u"pcriticaldamage") {
        return CCreatureSkillMod::SBMN_CriticalDamage;
    }
    return CCreatureSkillMod::SBMN_Max;
}

bool CParserForSkill::hasCycle(i_call_my_skillNode* node) {
    node->inStack = true;
    for (i_call_my_skillNode* source : node->fromList) {
        if (source->inStack || hasCycle(source)) {
            return true;
        }
    }
    node->inStack = false;
    return false;
}
}

%token EQUAL
%token SKILL_BEGIN
%token SKILL_NAME
%token IS_MAGIC
%token IS_HATE
%token MAGIC_LEVEL
%token SKILL_ID
%token LEVEL
%token SUB_LEVEL
%token OPERATE_TYPE
%token MP_CONSUME1
%token MP_CONSUME2
%token MP_CONSUME_TICK
%token HP_CONSUME_TICK
%token HP_CONSUME
%token ITEM_CONSUME
%token EFFECTIVE_RANGE
%token CAST_RANGE
%token SKILL_HIT_TIME
%token PLEDGE_NV_CONSUME
%token PVP_POINT_CONSUME
%token SKILL_COOL_TIME
%token SKILL_HIT_CANCEL_TIME
%token REUSE_DELAY
%token UNAVAILABLE_COND
%token OPERATE_COND
%token TARGET_OPERATE_COND
%token PASSIVE_CONDITIONS
%token ACTIVATE_RATE
%token PVP_ACTIVATE_RATE
%token PVE_ACTIVATE_RATE
%token LV_BONUS_RATE
%token BASIC_PROPERTY
%token CHANNELING_START
%token EFFECT
%token START_EFFECT
%token FAIL_EFFECT
%token TICK_EFFECT
%token TICK_INTERVAL
%token END_EFFECT
%token SELF_EFFECT
%token PVP_EFFECT
%token PVE_EFFECT
%token ATTACHED_SKILL
%token ABNORMAL_TIME
%token ABNORMAL_LV
%token ABNORMAL_TYPE
%token EFFECT_POINT
%token ABNORMAL_TIME_CRITICAL
%token PVP_ABNORMAL_TIME
%token PVE_ABNORMAL_TIME
%token ABNORMAL_VISUAL_EFFECT
%token MULTI_CLASS
%token ABNORMAL_INSTANT
%token DEBUFF
%token IRREPLACEABLE_BUFF
%token RIDE_STATE
%token TARGET_TYPE
%token AFFECT_SCOPE
%token AFFECT_LIMIT
%token AFFECT_RANGE
%token FAN_RANGE
%token AFFECT_OBJECT
%token AFFECT_SCOPE_HEIGHT
%token NEXT_ACTION
%token SKILL_END
%token CATEGORY
%token REUSE_DELAY_TYPE
%token ATTRIBUTE
%token TRAIT
%token REUSE_DELAY_LOCK
%token CONSUME_ETC
%token BUFF_PROTECT_LEVEL
%token ABNORMAL_DELETE_LEAVEWORLD
%token TRANSFORM_TYPE
%token BLOCK_ACTION_USE_SKILL
%token ALTER_SKILL_LIST
%token MAGIC_CRITICAL_RATE
%token OLYMPIAD_USE
%token REDUCESKILL
%token REUSE_DELAY_GROUP
%token NPC_NOTICE
%token IS_AGATHION_MAIN_SKILL
%token TOGGLE_GROUP_ID
%token ATTACH_TOGGLE_ID
%token ATTACH_TOGGLE_LIST
%token DOUBLE_CAST_SKILL
%token IS_DOUBLE
%token EVENT_TYPE
%token DAMAGE_PROPORTION
%token SUBORDINATION_ABNORMAL
%token CHANGE_SKILL_ID
%token RESIST_CAST
%token IS_NECESSARYTG
%token UNDELETABLE
%token AWAKEN_PENALTY
%token NO_TRANSFER_ABNORMAL
%token EXCEPT_BLOCK
%token EXCEPT_RESTORE
%token CHARGE_DIRECTION
%token AFTER_EFFECT_TYPE
%token LESS_GREATER
%token GREATER
%token GREATER_EQUAL
%token OP_EQUIP_ITEM
%token CAN_REFUEL_AIRSHIP
%token CAN_BOOKMARK_ADD_SLOT
%token CAN_ADD_MAX_ENTRANCE_INZONE
%token OP_TARGET_PC
%token OP_COMBAT
%token OP_SKILL
%token OP_NEED_AGATHION
%token CAN_TRANSFORM
%token CAN_UNTRANSFORM
%token CAN_USE_IN_TRANSFORM
%token CANNOT_USE_IN_TRANSFORM
%token NOT_IN_UNDERWATER
%token OP_CHECK_CAST_RANGE
%token OP_CHECK_ABNORMAL
%token OP_CHANGE_WEAPON
%token OP_CHECK_PCBANG_POINT
%token EQUIP_WEAPON
%token EQUIP_ARMOR
%token TARGET_ITEM_CRYSTAL_TYPE
%token TARGET_ITEM_CRYSTAL_DIVIDE_TYPE
%token TARGET_RACE
%token EQUIP_SHIELD
%token POSSESS_HOLYTHING
%token BUILD_CAMP
%token BUILD_ADVANCE_BASE
%token CAN_TRANSFORM_IN_DOMINION
%token REMAIN_HP_PER
%token REMAIN_MP_PER
%token REMAIN_CP_PER
%token TARGET_MY_PARTY
%token TARGET_MY_PLEDGE
%token OP_TARGET_MY_PLEDGE_ACADEMY
%token TARGET_MY_MENTOR
%token TARGET_MY_MENTEE
%token OP_IN_SIEGE_TIME
%token CONSUME_BODY
%token ENERGY_SAVED
%token SOUL_SAVED
%token CAN_SUMMON_PET
%token CAN_SUMMON
%token CAN_SUMMON_MULTI
%token CAN_SUMMON_CUBIC
%token CAN_SUMMON_SIEGE_GOLEM
%token OP_SWEEPER
%token OP_SIEGE_HAMMER
%token OP_RESURRECTION
%token OP_HARVEST
%token CHECK_SEX
%token CHECK_LEVEL
%token OP_ENERGY_MAX
%token OP_SOUL_MAX
%token OP_HOME
%token OP_WYVERN
%token OP_2H_WEAPON
%token OP_UNLOCK
%token OP_COMPANION
%token OP_USE_FIRECRACKER
%token OP_FISHING_CAST
%token OP_FISHING_PUMPING
%token OP_FISHING_REELING
%token OP_TERRITORY
%token OP_NOT_TERRITORY
%token OP_NOT_TERRITORY_LIST
%token OP_ALIGNMENT
%token OP_PKCOUNT
%token OP_ENCUMBERED
%token OP_CALL_PC
%token OP_CHECK_CLASS
%token OP_CHECK_CLASS_LIST
%token OP_CHECK_SKILL
%token OP_EXIST_NPC
%token OP_NEED_SUMMON
%token OP_TARGET_NPC
%token OP_CAN_ESCAPE
%token OP_ENCHANT_RANGE
%token OP_USE_PRASEED
%token OP_RESTART_POINT
%token OP_AGATHION_ENERGY
%token CAN_USE_SWOOP_CANNON
%token CAN_USE_IN_BATTLEFIELD
%token CAN_ENCHANT_ATTRIBUTE
%token OP_CHECK_FLAG
%token OP_CAN_DISARM
%token OP_NOT_CURSED
%token OP_CHECK_RESIDENCE
%token OP_BLINK
%token OP_CHARGE_SELF
%token OP_CAN_GO
%token CAN_MOUNT_FOR_EVENT
%token OP_COLLECTING
%token OP_SKILL_ACQUIRE
%token OP_CHECK_CRT_EFFECT
%token OP_HAVE_SUMMON
%token OP_CAN_NOT_USE_AIRSHIP
%token CAN_RESTORE_VITAL_POINT
%token CAN_STOP_CONSUME_VITAL_POINT
%token CAN_USE_VITALITY_CONSUME_ITEM
%token CAN_CHANGE_VITAL_ITEM_COUNT
%token OP_CHECK_STUCK_NPC
%token CAN_USE_IN_DRAGON_LAIR
%token OP_NEED_SUMMON_OR_PET
%token WITHOUT_ACTION
%token OP_RETURN_POS
%token OP_PEACEZONE
%token OP_HAVE_SUMMONED_NPC
%token OP_HAVE_NOT_SUMMONED_NPC
%token OP_MAINJOB
%token OP_SUBJOB
%token OP_TARGET_WEAPON_ATTACK_TYPE
%token OP_TARGET_ARMOR_TYPE
%token OP_TARGET_ALL_ITEM_TYPE
%token OP_CHECK_ACCOUNT_TYPE
%token OP_PLEDGE
%token OP_SOCIAL_CLASS
%token PLEDGEGAME_USE
%token OP_INSTANTZONE
%token OP_NOT_INSTANTZONE
%token OP_NOT_OLYMPIAD
%token OP_CANNOT_USE_TARGET_WITH_PRIVATE_STORE
%token OP_CHECK_ON_GOING_EVENT_CAMPAIGN
%token OP_CHECK_RELATIVE_HEIGHT
%token OP_CHECK_NOT_REGISTERED_PLEDGEGAME
%token OP_EQUIP_AGATHION_MAIN_SLOT
%token I_TRANSFER_ABNORMAL
%token I_REFUEL_AIRSHIP
%token I_BOOKMARK_ADD_SLOT
%token I_BOOKMARK_TELEPORT
%token I_ADD_MAX_ENTRANCE_INZONE
%token I_SUMMON_CUBIC
%token I_BLINK
%token I_STEAL_ABNORMAL
%token I_M_ATTACK_BY_ABNORMAL
%token I_FOCUS_SOUL
%token I_REMOVE_SOUL
%token I_SUMMON_TRAP
%token I_DETECT_TRAP
%token I_DEFUSE_TRAP
%token I_TRANSFER_HATE
%token I_CALL_SKILL
%token I_CALL_SKILL_BY_TARGET
%token I_M_ATTACK_BY_RANGE
%token I_SUMMON_AGATHION
%token I_UNSUMMON_AGATHION
%token I_COMPULSION_SOCIAL_ACTION
%token I_FORCE_SITDOWN
%token I_CALL_MY_SKILL_LIST
%token I_DISPEL_BY_NAME
%token I_DISPEL_BY_SLOT
%token I_DISPEL_BY_SLOT_MYSELF
%token I_P_ATTACK
%token I_EVENT_ATTACK
%token I_P_ATTACK_WEAPON_BONUS
%token I_P_SOUL_ATTACK
%token I_FATAL_BLOW
%token I_SOUL_BLOW
%token I_HEAL
%token I_HEAL_SPECIAL
%token I_HP_DRAIN
%token I_M_ATTACK
%token I_M_ATTACK_LINK
%token I_M_SOUL_ATTACK
%token I_ESCAPE
%token I_HP
%token I_HP_SPECIAL
%token I_MP
%token I_MP_BY_HP
%token I_INFORM
%token I_RESURRECTION
%token I_SUMMON
%token I_SUMMON_MULTI
%token I_DISPEL_BY_CATEGORY
%token I_ENCHANT_ARMOR
%token I_ENCHANT_WEAPON
%token I_ENCHANT_ITEM_MULTI
%token I_ENCHANT_ATTRIBUTE
%token I_ENCHANT_ATTRIBUTION_ITEM
%token I_ENCHANT_WEAPON_RATE
%token I_ENCHANT_ARMOR_RATE
%token I_LOCKED_ITEM
%token I_UNLOCKED_ITEM
%token I_ADD_ENCHANT_WEAPON_LEVEL_INC
%token I_ADD_ENCHANT_ARMOR_LEVEL_INC
%token I_SUB_ENCHANT_WEAPON_LEVEL_DEC
%token I_SUB_ENCHANT_ARMOR_LEVEL_DEC
%token I_CHANGE_ATTRIBUTE
%token I_HP_PER_MAX
%token I_MP_PER_MAX
%token I_MP_BY_LEVEL
%token I_CP_PER_MAX
%token I_PCBANG_POINT_UP
%token I_ADD_HATE
%token I_ADD_HATE_MYMASTER
%token I_HOLYTHING_POSSESS
%token I_INSTALL_CAMP
%token I_UNLOCK
%token I_DELETE_HATE
%token I_RANDOMIZE_HATE
%token I_FOCUS_ENERGY
%token I_FOCUS_MAX_ENERGY
%token I_ENERGY_ATTACK
%token I_SOUL_SHOT
%token I_SPIRIT_SHOT
%token I_SUMMON_SOUL_SHOT
%token I_SUMMON_SPIRIT_SHOT
%token I_CONFUSE
%token I_RUN_AWAY
%token I_CONSUME_BODY
%token I_HP_BY_LEVEL_SELF
%token I_MP_BY_LEVEL_SELF
%token I_SPOIL
%token I_SWEEPER
%token I_PLUNDER
%token I_BACKSTAB
%token I_DISTRUST
%token I_DELETE_HATE_OF_ME
%token I_DEATH_LINK
%token I_DEATH_LINK_MP
%token I_DISPEL_ALL
%token I_SUMMON_PET
%token I_FOOD_FOR_PET
%token I_FLY_AWAY
%token I_FLY_SELF
%token I_REGISTER_SIEGE_GOLEM
%token I_M_ATTACK_OVER_HIT
%token I_BLINK_RETURN
%token I_SET_RETURN_POS
%token I_CREATE_ITEM_RANDOM
%token I_RESTORATION
%token I_RESTORATION_RANDOM
%token I_RESTORATION_RANDOM_GROUP
%token I_RESTORATION_ADDRANDOM
%token I_SOWING
%token I_HARVESTING
%token I_HP_SELF
%token I_GIVE_CONTRIBUTION
%token I_CHANGE_FACE
%token I_CHANGE_HAIR_STYLE
%token I_CHANGE_HAIR_COLOR
%token I_SUMMON_NPC
%token I_SUMMON_NPC_NEAR
%token I_SUMMON_NPC_DYING
%token I_RESET_QUEST
%token I_AGATHION_ENERGY
%token I_SUMMON_UNIQUE_NPC
%token I_CP
%token I_SP
%token I_PHYSICAL_ATTACK_HP_LINK
%token I_M_ATTACK_BY_HP
%token I_M_ATTACK_BY_DIST
%token I_FISHING_CAST
%token I_FISHING_PUMPING
%token I_FISHING_REELING
%token I_FISHING_SHOT
%token I_OPEN_DWARF_RECIPEBOOK
%token I_OPEN_COMMON_RECIPEBOOK
%token I_ABNORMAL_TIME_CHANGE
%token I_DISPEL_BY_SLOT_PROBABILITY
%token I_ALIGN_DIRECTION
%token I_DEATH
%token I_REBALANCE_HP
%token I_REBALANCE_HP_SUMMON
%token I_REBALANCE_MP
%token I_TARGET_CANCEL
%token I_TARGET_ME
%token I_TELEPORT
%token I_INSTALL_CAMP_EX
%token I_M_ATTACK_MP
%token I_NPC_KILL
%token I_BETRAY
%token I_CALL_PC
%token I_CHANGE_SKILL_LEVEL
%token I_REMOVE_ENERGY
%token I_SKILL_TURNING
%token I_CALL_PARTY
%token I_CALL_TARGET_PARTY
%token I_M_ATTACK_RANGE
%token I_P_ATTACK_BY_DIST
%token I_M_ATTACK_OVER_HIT_RANGE
%token I_PLEDGE_SEND_SYSTEM_MESSAGE
%token I_REAL_DAMAGE
%token I_SET_VISIBLE
%token I_CAPTURE_FLAG_START
%token I_CAPTURE_FLAG
%token I_CONVERT_ITEM
%token I_DETECT_OBJECT
%token I_MOUNT_FOR_EVENT
%token I_DISMOUNT_FOR_EVENT
%token I_SET_SKILL
%token I_PULL
%token I_GET_AGRO
%token I_TELEPORT_TO_TARGET
%token I_TELEPORT_TO_SUMMON
%token I_INSTALL_ADVANCE_BASE
%token I_UNINSTALL_ADVANCE_BASE
%token I_CAPTURE_OWNTHING
%token I_CAPTURE_OWNTHING_START
%token I_COLLECTING
%token I_MY_SUMMON_KILL
%token I_DESPAWN
%token I_EVENT_AGATHION_REUSE_DELAY
%token I_SHOW_REUSE_DELAY
%token I_SET_REUSE_DELAY
%token I_REUSE_SKILL_RESET
%token I_REUSE_SKILL_REDUCE
%token I_RANDOM_TARGET
%token I_BONUSCOUNT_UP
%token I_HEAL_LINK
%token I_HP_DOWN
%token I_HP_DOWN_SELF
%token I_REMOTE_CONTROL_TRAP
%token I_KNOCKBACK
%token I_M_ATTACK_BY_ABNORMAL_SLOT
%token I_CALL_SKILL_BY_ABNORMAL_SLOT
%token I_POSITION_CHANGE
%token I_SUMMON_HALLUCINATION
%token I_GET_ULTIMATE_SKILL_GAUGE
%token I_CHANGE_CLASS
%token I_DELETE_TOPAGRO
%token I_CONSUME_SUMMON
%token I_SUMMON_NPC_POSITION
%token I_RESTORE_VITAL_POINT
%token I_RESTORE_VITAL_POINT_NONCOUNT
%token I_SUICIDE
%token I_P_ATTACK_SAVE_HP
%token I_CALL_SKILL_BY_FORCE
%token I_SHOW_USABLE_DAILY_SKILL_COUNT
%token I_TELEPORT_TO_NPC
%token I_PARTY_TELEPORT_TO_NPC
%token I_GET_EXP
%token I_SHAPE_SHIFT_WP
%token I_BLESS_SHAPE_SHIFT_WP
%token I_SHAPE_SHIFT_WP_FIXED
%token I_RESTORE_SHAPE_SHIFT_ITEM
%token I_SHAPE_SHIFT_AM
%token I_BLESS_SHAPE_SHIFT_AM
%token I_SHAPE_SHIFT_AM_FIXED
%token I_SHAPE_SHIFT_ITEM_AUTO_MATCHING
%token I_CHANGE_VITAL_ITEM_COUNT
%token I_SUMMON_PLEDGE
%token I_PVPPOINT
%token I_FATAL_BLOW_ABNORMAL
%token I_PK_COUNT
%token I_KARMA_COUNT
%token I_SIDESTEP_TO_TARGET
%token P_ATTACK_POSITION_CHANGE
%token I_P_ATTACK_ABNORMAL
%token P_BLOCK_GETDAMAGE_DISTANCE
%token P_REAL_RESIST
%token I_SUMMON_TERRITORY
%token I_ACC_CHANGE
%token OP_CHECK_EQUIP_SLOT
%token OP_CHECK_NOBLESS
%token T_101CA
%token I_EV_INK
%token I_EV_INK_PARTY
%token I_RANDOM_CALL_SKILL
%token I_ADD_EVENT_CONTRIBUTE
%token P_PREVENTION_DEATH
%token P_SKILL_CRITICAL_RATE_FIX
%token P_DISPEL_BY_REALTARGET
%token P_ABILITY_CHANGE_ITEMSKILL
%token P_BLOCK_SKILL_TRIGGER
%token CUB_M_ATTACK
%token CUB_HP_DRAIN
%token CUB_HEAL
%token CUB_HP
%token CUB_PHYSICAL_ATTACK
%token CUB_PHYSICAL_DEFENCE
%token CUB_ATTACK_SPEED
%token CUB_BLOCK_ACT
%token T_HP
%token T_MP
%token T_HP_FATAL
%token T_GET_ENERGY
%token T_CP
%token T_ENERGY
%token T_HP_BY_HP
%token T_PULL
%token T_HP_TO_OWNER
%token T_HP_IN_INVINCIBLE
%token T_HP_MAGICAL_ATTACK
%token A_SPEED
%token A_SYNERGY_SKILL
%token P_AURA
%token P_TRANSFORM
%token P_TRANSFORM_HANGOVER
%token P_TRANSFER_DAMAGE_PC
%token P_TRANSFER_DAMAGE_TO
%token P_SOUL_EATING
%token P_BLOCK_ATTACK
%token P_DISARM
%token P_DEFENCE_CRITICAL_RATE
%token P_DEFENCE_CRITICAL_DAMAGE
%token P_RESIST_DD_MAGIC
%token P_PHYSICAL_ATTACK
%token P_PHYSICAL_ATTACK_BY_DEFENCE
%token P_PHYSICAL_DEFENCE
%token P_HP_REGEN
%token P_MP_REGEN
%token P_MP_REGEN_ADD
%token P_MAX_MP_ADD
%token P_MAGICAL_DEFENCE_ADD
%token P_MAGICAL_ATTACK_ADD
%token P_HEAL_EFFECT_ADD
%token P_SPEED
%token P_CREATE_ITEM
%token P_CREATE_COMMON_ITEM
%token P_ATTACK_SPEED
%token P_MAGIC_SPEED
%token P_MAGIC_SPEED_BY_MAGIC_TYPE
%token P_REMOVE_EQUIP_PENALTY
%token P_CRYSTALLIZE
%token P_AVOID
%token P_CHARGE_VITALITY
%token P_LUCK
%token P_MAGICAL_DEFENCE
%token P_PHYSICAL_ATTACK_BY_MATERIAL
%token P_PHYSICAL_DEFENCE_BY_MATERIAL
%token P_ATTACK_ATTRIBUTE
%token P_DEFENCE_ATTRIBUTE
%token P_ATTACK_TRAIT
%token P_DEFENCE_TRAIT
%token P_BLOCK_MOVE
%token P_BLOCK_ACT
%token P_BLOCK_CONTROLL
%token P_BLOCK_SPELL
%token P_BLOCK_GETDAMAGE
%token P_FEAR
%token P_MAX_HP
%token P_MAX_MP
%token P_MAX_CP
%token P_HIT
%token P_MAGICAL_ATTACK
%token P_ATTACK_RANGE
%token P_CRITICAL_RATE
%token P_PHYSICAL_CRITICAL_RATE_MAXUP
%token P_MAGICAL_CRITICAL_RATE_MAXUP
%token P_CRITICAL_DAMAGE
%token P_REUSE_DELAY
%token P_REDUCE_CANCEL
%token P_ATTACK_SPEED_BY_WEAPON
%token P_MAGIC_SPEED_BY_WEAPON
%token P_SHIELD_DEFENCE_RATE
%token P_HP_REGEN_BY_MOVE_MODE
%token P_MP_REGEN_BY_MOVE_MODE
%token P_AVOID_BY_MOVE_MODE
%token P_SAFE_FALL_HEIGHT
%token P_BREATH
%token P_DAMAGE_SHIELD
%token P_PASSIVE
%token P_AVOID_AGRO
%token P_WEIGHT_LIMIT
%token P_TRADE
%token P_SET_COLLECTED
%token P_SET_CLOAK_SLOT
%token P_CUBIC_MASTERY
%token P_WEIGHT_PENALTY
%token P_MANA_CHARGE
%token P_TRANSFER_DAMAGE_SUMMON
%token P_HEAL_EFFECT
%token P_AREA_DAMAGE
%token P_HIT_AT_NIGHT
%token P_VAMPIRIC_ATTACK
%token P_VAMPIRIC_DEFENCE
%token P_CRITICAL_RATE_BY_HP1
%token P_CRITICAL_RATE_BY_HP2
%token P_PHYSICAL_ATTACK_BY_HP1
%token P_PHYSICAL_ATTACK_BY_HP2
%token P_PHYSICAL_DEFENCE_BY_HP1
%token P_PHYSICAL_DEFENCE_BY_HP2
%token P_AVOID_RATE_BY_HP1
%token P_AVOID_RATE_BY_HP2
%token P_ATTACK_SPEED_BY_HP1
%token P_ATTACK_SPEED_BY_HP2
%token P_FATAL_BLOW_RATE
%token P_DEFENCE_FATAL_BLOW_RATE
%token P_CRITICAL_RATE_POSITION_BONUS
%token P_REDUCE_DROP_PENALTY
%token P_PHYSICAL_SHIELD_DEFENCE
%token P_PHYSICAL_ARMOR_HIT
%token P_PHYSICAL_POLARM_TARGET_SINGLE
%token P_MAGIC_CRITICAL_RATE
%token P_MAGIC_MP_COST
%token P_AFTER_MP_COST
%token P_PHYSICAL_SHIELD_DEFENCE_ANGLE_ALL
%token P_PVE_PHYSICAL_ATTACK_DMG_BONUS
%token P_PVE_PHYSICAL_SKILL_DMG_BONUS
%token P_PVE_MAGICAL_SKILL_DMG_BONUS
%token P_PVP_PHYSICAL_ATTACK_DMG_BONUS
%token P_PVP_PHYSICAL_SKILL_DMG_BONUS
%token P_PVP_MAGICAL_SKILL_DMG_BONUS
%token P_PVP_MAGICAL_SKILL_DEFENCE_BONUS
%token P_PVP_PHYSICAL_ATTACK_DEFENCE_BONUS
%token P_PVP_PHYSICAL_SKILL_DEFENCE_BONUS
%token P_FISHING_MASTERY
%token P_ABNORMAL_RATE_LIMIT
%token P_ABNORMAL_REMOVE_BY_HIT
%token P_ABNORMAL_REMOVE_BY_DMG
%token P_BLOCK_SKILL_PHYSICAL
%token P_BLOCK_SKILL_SPECIAL
%token P_BLOCK_BUFF
%token P_BLOCK_DEBUFF
%token P_BLOCK_BUFF_SLOT
%token P_BLOCK_ADD_SLOT
%token P_BLOCK_SKILL
%token P_REFLECT_SKILL
%token P_SKILL_CRITICAL
%token P_SKILL_CRITICAL_PROBABILITY
%token P_CRITICAL_DAMAGE_POSITION
%token P_RESIST_DISPEL_BY_CATEGORY
%token P_RESIST_ABNORMAL_BY_CATEGORY
%token P_PRESERVE_ABNORMAL
%token P_ENLARGE_STORAGE
%token P_ENLARGE_ABNORMAL_SLOT
%token P_HIT_NUMBER
%token P_CP_REGEN
%token P_CP_REGEN_BY_MOVE_MODE
%token P_BETRAY
%token P_TRIGGER_SKILL_BY_DMG
%token P_TRIGGER_SKILL_BY_ATTACK
%token P_TRIGGER_SKILL_BY_SKILL
%token P_TRIGGER_SKILL_BY_AVOID
%token P_TRIGGER_SKILL_BY_DEATH
%token P_TRIGGER_SKILL_BY_MAGIC_TYPE
%token P_CHANGE_SLOT
%token P_TRIGGER_SKILL_BY_ABNORMAL_SLOT
%token P_TRIGGER_SKILL_BY_ATTACKED_SKILL
%token P_TRIGGER_SKILL_BY_BLOCK
%token P_CHANNEL_CLAN
%token P_CHANGE_FISHING_MASTERY
%token P_RESURRECTION_SPECIAL
%token P_AVOID_SKILL
%token P_COUNTER_SKILL
%token P_AVOID_COUNTER_SKILL
%token P_PK_PROTECT
%token P_EXPAND_DECO_SLOT
%token P_EXPAND_JEWEL_SLOT
%token P_EXPAND_AGATHION_MAIN_SLOT
%token P_EXPAND_AGATHION_SUB_SLOT
%token P_TARGET_ME
%token P_LIMIT_HP
%token P_LIMIT_MP
%token P_LIMIT_CP
%token P_ANTI_CUBIC
%token P_SKILL_POWER
%token P_MAGIC_DEFENCE_CRITICAL_RATE
%token P_MAGIC_DEFENCE_CRITICAL_DMG
%token P_MAGIC_CRITICAL_DMG
%token P_MAGIC_DEFENCE_CRITICAL_RATE_ADD
%token P_MAGIC_DEFENCE_CRITICAL_DMG_ADD
%token P_HIDE
%token P_HATE_ATTACK
%token P_REFLECT_DD
%token P_REFLECT_DD_RESIST
%token P_MP_VAMPIRIC_ATTACK
%token P_BLOCK_RESURRECTION
%token P_BLOCK_CHAT
%token P_BLOCK_PARTY
%token P_VIOLET_BOY
%token P_DOMINION_TRANSFORM
%token P_STAT_UP
%token P_STAT_UP_LIMITED
%token P_CHEAPSHOT
%token P_TRANSMUTE_ATTR_ATTACK
%token P_TRANSMUTE_ATTR_DEFENCE
%token P_CONDITION_BLOCK_ACT_ITEM
%token P_CONDITION_BLOCK_ACT_SKILL
%token P_BLOCK_USE_SKILL
%token P_IGNORE_SKILL
%token P_DEFENCE_DAMAGE
%token P_CHANGEBODY
%token P_ITEM_CHANCE
%token P_BONUSTIMELIMIT_UP
%token P_VITAL_ADD
%token P_VITAL_SUB
%token P_EXP_ADD
%token P_FOCUS_ENERGY
%token P_ABILITY_CHANGE
%token P_SUB_EXP
%token P_SUB_VP
%token P_BERSERKER
%token P_MP_SHIELD
%token P_ENERGY_HEAL
%token P_BLOCK_TARGET
%token P_DISAPPEAR_TARGET
%token P_KNOCKDOWN
%token P_BLOCK_GET_ENERGY
%token P_WRONG_CASTING
%token P_DOUBLE_SUMMON
%token P_DEATH_ATTACK
%token P_BLOCK_ELIXER_PREMIUM
%token P_SPELL_POWER
%token P_TRIGGER_SKILL_BY_KILL
%token P_MAGIC_CANCEL_RESIST
%token P_PHYSICAL_ABNORMAL_RESIST
%token P_MAGIC_ABNORMAL_RESIST
%token P_MAGIC_HIT
%token P_MAGIC_AVOID
%token P_EXP_MODIFY
%token P_SP_MODIFY
%token P_CRYSTAL_GRADE_MODIFY
%token P_PLAY_MUSIC
%token P_STOP_CONSUME_VITAL_POINT
%token P_STOP_CONSUME_VITAL_POINT_NONCOUNT
%token P_SUMMON_POINT
%token P_IGNORE_DEATH
%token P_DIRECTION_DAMAGE
%token P_GET_DAMAGE_LIMIT
%token P_GET_DAMAGE_LIMIT_STATUS
%token P_SPEED_OUT_OF_NIGHT
%token P_STAT_UP_AT_NIGHT
%token P_ABNORMAL_SHIELD
%token P_BLOCK_ESCAPE
%token P_RECHARGE_VITAL_POINT
%token P_RECHARGE_VITAL_POINT_NONCOUNT
%token P_ALTER_SKILL_BY_EVENT
%token P_PROTECT_DEATH_PENALTY
%token P_SPEED_OUT_OF_FIGHT
%token P_DAMAGE_SHIELD_RESIST
%token P_INSTANT_KILL_RESIST
%token P_GET_ITEM_BY_EXP
%token P_LUCKY_ENCHANT_RATE
%token P_WEAK_DEATH_PENALTY
%token P_POSITION_DAMAGE
%token P_BLOCK_ACT_BONUS
%token P_POTION_BONUS
%token P_PHYSICAL_SKILL_CRITICAL_RATE
%token P_PHYSICAL_SKILL_CRITICAL_DAMAGE
%token P_HIT_SUMMON
%token P_KALIE_BUFF
%token P_ALTER_RECEIVE_NORMAL_DAMAGE
%token P_NOPKCOUNT
%token P_DAMAGE_BY_ATTACK
%token P_STATBONUS_SPEED
%token P_STATBONUS_SKILLCRITICAL
%token P_PHYSICAL_ATTACK_SHIELD
%token P_ATTACK_SPEED_SHIELD
%token P_CRITICAL_RATE_SHIELD
%token P_CRITICAL_DAMAGE_SHIELD
%token P_STOP_VITALITY_EFFECT
%token P_DROPRATE_MODIFY
%token P_DROPAMOUNT_MODIFY
%token P_CREATE_ITEM_CRITICAL
%token P_PHYSICAL_ATTACK_PC_LEVEL
%token P_MAGICAL_ATTACK_PC_LEVEL
%token P_ATTACK_ATTRIBUTE_ADD
%token P_PASSIVE_BONUS_SKILL
%token P_BLOCK_GETDAMAGE_EXCEPT
%token P_ENERGY_SKILL_POWER
%token I_P_PERCENT_ATTACK
%token P_ALIGN_DIRECTION
%token I_CALL_SKILL_BY_TARGETDIRECTION
%token I_SUMMON_TARGET_TO_ME
%token I_BLINK_PARTY
%token I_M_ATTACK_DEBUFF_DMG
%token P_PSYCHIC_POWER
%token I_FLOAT_AWAY
%token P_HIDE_NO_INHERIT
%token P_VITAL_RECHARGE_BOOST
%token P_ADD_VITAL_BONUS
%token P_REDUCE_VITAL_CONSUME_POINT
%token P_INC_SPOIL_RATE
%token P_PHYSICAL_DEFENCE_PVP
%token P_SPEED_PVP
%token P_DAMAGE_SHIELD_RESIST_CASTLE_SIEGE
%token P_DAMAGE_BY_ATTACK_CASTLE_SIEGE
%token P_PVE_PHYSICAL_DEFENCE
%token P_PVE_MAGICAL_DEFENCE
%token P_PVE_DEFENCE_ATTRIBUTE
%token P_ABNORMAL_TIME_CHANGE_TO_ABTYPE
%token P_ABNORMAL_TIME_CHANGE_TO_DEBUFF
%token P_BLOCK_GETDAMAGE_EXCEPT_EACH_OTHER
%token P_MP_FRIENDLY_PAYBACK
%token P_INTERCEPT_SKILL
%token P_REPLACE_SKILL
%token C_AGATHION_ENERGY
%token I_CALL_SKILL_BY_TARGET_DIVIDE
%token C_MP
%token C_REST
%token C_CHAMELEON_REST
%token C_MP_BY_LEVEL
%token C_FAKE_DEATH
%token C_HP
%token P_FISHING_MONSTER_PROB
%token P_FISHING_START_TIME
%token P_FISHING_INSTANTKILL_PROB
%token P_FISHING_AREA_BUFF
%token P_WORLDCHATPOINT_ADD
%token I_FISHING_INSTANTKILL
%token P_RAID_POINT_MODIFY
%token P_FISHING_DAMAGE
%token P_REMOVE_GRADE_PENALTY
%token P_ACQUIRE_ALCHEMY_SKILL
%token I_CALL_RANDOM_SKILL_CONSUME_ITEM
%token P_MAGICAL_ATTACK_BY_WEAPON
%token P_ABSORB_SHIELD
%token P_ABSORB_SHIELD_STATUS
%token I_ENSOUL
%token P_ADD_ENSOUL_PROB
%token P_VITAL_RATE
%token SPECIAL_LEVEL
%token P_BALTHUS_BUFF
%token P_HP_SPECIAL_ADD
%token P_EXP_SP_FISHING
%token P_PHYSICAL_PENETRATE
%token P_MAGICAL_PENETRATE
%token P_EXPAND_ARTIFACT_SLOT_GROUP
%token ALL
%token NAME
%token IDENTIFIER
%token PREDEFINED
%token FLOAT_NUMBER
%token PER_GROUP
%token FRONT_GROUP
%token ATTR_VALUE
%token TRAIT_VALUE
%token SWORD_GROUP
%token ARMOR_VALUE
%token SELF_GROUP
%token RACE_VALUE
%token SINGLE_GROUP
%token UNDEAD_REAL_ENEMY_GROUP
%token INVENTORY_NORMAL_GROUP
%token PK_GROUP
%token IA_VALUE
%token OCT_VALUE
%token NOT_APPLIED_DIRECT_INVERSE
%token INSELF_INMASTER
%token SLOT_VALUE
%token COND_P_OLYMPIAD_PLEDGE
%token COND_P_CASTLE_SIEGE
%token COND_P_OVER_HP
%token I_GOTO_TARGET
%token P_BLOCK_GETDAMAGE_TARGET_ONLY
// The original grammar has these conflicts. Bison resolves them as the
// original tables did. A shift wins over a reduce.
%expect 2
%start n1

%%

n1
    : skill_list
    ;

skill_list
    : skill_list skill
    | %empty
    ;

skill
    : SKILL_BEGIN SKILL_NAME EQUAL NAME SKILL_ID EQUAL PREDEFINED LEVEL EQUAL PREDEFINED SUB_LEVEL EQUAL PREDEFINED
      {
          const int skillId = grammar::SemanticInt32($7);
          const int level = grammar::SemanticInt32($10);
          const int subLevel = grammar::SemanticInt32($13);
          wchar_t* const skillName = DuplicateSkillName(SemanticWord($4));
          if (skillName[0] == L'\0') {
              // Log.Add(
              //     LOG_ERROR,
              //     L"empty skill name. Line = %d !",
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"empty skill name. Line = %d !",
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log, "skilldata.txt line %lld: skill %d level %d sublevel %d has an empty name; parsing continues.",
                  static_cast<long long>(m_lexer.line) + 1, skillId, level, subLevel
              );
          }
          m_pSkillInfo = nullptr;
          m_bNewSkill = true;
          if (m_bReload) {
              m_pSkillInfo = const_cast<CSkillInfo*>(g_SkillDB.GetSkillInfo(CSkillKey(skillId, level, subLevel), nullptr));
              if (m_pSkillInfo != nullptr) {
                  m_bNewSkill = false;
                  m_pSkillInfo->Reset();
              }
          }
          if (m_pSkillInfo == nullptr) {
              m_pSkillInfo = new CSkillInfo;
          }
          m_pSkillInfo->m_nLevel = level;
          m_pSkillInfo->m_nSubLevel = subLevel;
          m_pSkillInfo->m_pszName = skillName;
          m_pSkillInfo->m_nSkillId = skillId;
          m_EffectActivateTiming = CSkillInfo::EAT_INVALID;
          m_ConditionTarget = CSkillInfo::OCT_INVALID;
          m_pTerritory = nullptr;
          ++m_nLineNumber;
      }
      category_def_list SKILL_END
      {
          // Current actions 1 and 3 share the complete skill_end path.
          CSkillInfo* const skill = m_pSkillInfo;
          if (!skill->IsDurationSkill()) {
              if (skill->m_nAbnormalType != ABNS_INVALID) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"invalid abnormal type at [%d] Line = [%d]",
                  //     m_lexer.line + 1,
                  //     m_nLineNumber)
                  // L2_LOG_ERROR(Log,
                  //     L"invalid abnormal type at [%d] Line = [%d]",
                  //     m_lexer.line + 1,
                  //     m_nLineNumber)
                  L2_LOG_ERROR(
                      Log, "skilldata.txt line %lld: non-duration skill %d has abnormal type %d; the value is retained.",
                      static_cast<long long>(m_lexer.line) + 1, skill->m_nSkillId, static_cast<int>(skill->m_nAbnormalType)
                  );
              }
          } else if (skill->m_nAbnormalType == ABNS_MAX || skill->m_nAbnormalType == ABNS_INVALID) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"invalid abnormal type at [%d] Line = [%d]",
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"invalid abnormal type at [%d] Line = [%d]",
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: duration skill %d has invalid abnormal type %d; using the public abnormal "
                  "slot.",
                  static_cast<long long>(m_lexer.line) + 1, skill->m_nSkillId, static_cast<int>(skill->m_nAbnormalType)
              );
              skill->m_nAbnormalType = ABNS_PUBLIC_SLOT;
          }
          if (skill->m_OperateType == SKILL_MAX) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"m_OperateType is empty() at [%d] Line = [%d]",
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"m_OperateType is empty() at [%d] Line = [%d]",
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log, "skilldata.txt line %lld: skill %d has no valid operate_type; skill_end finalization is skipped.",
                  static_cast<long long>(m_lexer.line) + 1, skill->m_nSkillId
              );
              break;
          }
          const int debuffType = static_cast<int>(skill->m_eDebuff);
          if (debuffType < DB_GOOD || debuffType >= DB_MAX) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[%s][%d][%d] skill buff type[%d] is wrong. [%d] Line = [%d]",
              //     skill->m_pszName,
              //     skill->m_nSkillId,
              //     skill->m_nLevel,
              //     debuffType,
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"[%s][%d][%d] skill buff type[%d] is wrong. [%d] Line = [%d]",
              //     skill->m_pszName,
              //     skill->m_nSkillId,
              //     skill->m_nLevel,
              //     debuffType,
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log, "skilldata.txt line %lld: skill %d has invalid buff type %d; the value is retained.",
                  static_cast<long long>(m_lexer.line) + 1, skill->m_nSkillId, debuffType
              );
          }
          if (skill->m_pszName == nullptr) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"skill name is null! Line = %d",
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"skill name is null! Line = %d",
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log, "skilldata.txt line %lld: skill %d has a null name; record finalization stops.",
                  static_cast<long long>(m_lexer.line) + 1, skill->m_nSkillId
              );
              m_pSkillInfo = nullptr;
              break;
          }
          if (skill->m_nLevel < 1 || skill->m_nSubLevel < 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[%s][%d][%d][%d] skill level setting has problem. line = %d",
              //     skill->m_pszName,
              //     skill->m_nSkillId,
              //     skill->m_nLevel,
              //     skill->m_nSubLevel,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"[%s][%d][%d][%d] skill level setting has problem. line = %d",
              //     skill->m_pszName,
              //     skill->m_nSkillId,
              //     skill->m_nLevel,
              //     skill->m_nSubLevel,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: skill %d has invalid level %d or sublevel %d; level must be positive and "
                  "sublevel nonnegative. Record finalization stops.",
                  static_cast<long long>(m_lexer.line) + 1, skill->m_nSkillId, skill->m_nLevel, skill->m_nSubLevel
              );
              m_pSkillInfo = nullptr;
              break;
          }
          if (ScopeRequiresPositiveRange(skill->m_AffectScope) && skill->m_nAffectRange < 1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[%s][%d][%d] skill range is less or equal to 0. line = %d",
              //     skill->m_pszName,
              //     skill->m_nSkillId,
              //     skill->m_nLevel,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"[%s][%d][%d] skill range is less or equal to 0. line = %d",
              //     skill->m_pszName,
              //     skill->m_nSkillId,
              //     skill->m_nLevel,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: skill %d has affect range %d for scope %d which requires a positive range; "
                  "record finalization stops.",
                  static_cast<long long>(m_lexer.line) + 1, skill->m_nSkillId, skill->m_nAffectRange,
                  static_cast<int>(skill->m_AffectScope)
              );
              m_pSkillInfo = nullptr;
              break;
          }
          if (skill->m_nSubAbnormal != ABNS_MAX && skill->m_nSubAbnormal != ABNS_INVALID && skill->m_eDebuff == DB_GOOD) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[%s] Duple Debuff is Not GOOD_BUF. line = %d",
              //     skill->m_pszName,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"[%s] Duple Debuff is Not GOOD_BUF. line = %d",
              //     skill->m_pszName,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: skill %d defines secondary abnormal type %d for a beneficial buff; the "
                  "settings are retained.",
                  static_cast<long long>(m_lexer.line) + 1, skill->m_nSkillId, static_cast<int>(skill->m_nSubAbnormal)
              );
          }
          if (!m_bNewSkill) {
              skill->PostLoad();
          } else {
              g_SkillDB.Add(skill);
              if (m_pPrevSkillInfo != nullptr && m_pPrevSkillInfo->m_nSkillId == skill->m_nSkillId &&
                  m_pPrevSkillInfo->m_nLevel + 1 == skill->m_nLevel) {
                  m_pPrevSkillInfo->m_pNextLevel = skill;
                  skill->m_pPrevLevel = m_pPrevSkillInfo;
              }
          }
          g_SkillDB.m_pPreviousSkillInfo = skill;
          m_pPrevSkillInfo = skill;
          m_pSkillInfo = nullptr;
          m_EffectActivateTiming = CSkillInfo::EAT_INVALID;
          m_ConditionTarget = CSkillInfo::OCT_INVALID;
          m_pTerritory = nullptr;
      }
    | SKILL_BEGIN SKILL_NAME EQUAL NAME SKILL_ID EQUAL PREDEFINED LEVEL EQUAL PREDEFINED
      {
          const int skillId = grammar::SemanticInt32($7);
          const int level = grammar::SemanticInt32($10);
          const int subLevel = 0;
          wchar_t* const skillName = DuplicateSkillName(SemanticWord($4));
          if (skillName[0] == L'\0') {
              // Log.Add(
              //     LOG_ERROR,
              //     L"empty skill name. Line = %d !",
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"empty skill name. Line = %d !",
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log, "skilldata.txt line %lld: skill %d level %d sublevel %d has an empty name; parsing continues.",
                  static_cast<long long>(m_lexer.line) + 1, skillId, level, subLevel
              );
          }
          m_pSkillInfo = nullptr;
          m_bNewSkill = true;
          if (m_bReload) {
              m_pSkillInfo = const_cast<CSkillInfo*>(g_SkillDB.GetSkillInfo(CSkillKey(skillId, level, subLevel), nullptr));
              if (m_pSkillInfo != nullptr) {
                  m_bNewSkill = false;
                  m_pSkillInfo->Reset();
              }
          }
          if (m_pSkillInfo == nullptr) {
              m_pSkillInfo = new CSkillInfo;
          }
          m_pSkillInfo->m_nLevel = level;
          m_pSkillInfo->m_nSubLevel = subLevel;
          m_pSkillInfo->m_pszName = skillName;
          m_pSkillInfo->m_nSkillId = skillId;
          m_EffectActivateTiming = CSkillInfo::EAT_INVALID;
          m_ConditionTarget = CSkillInfo::OCT_INVALID;
          m_pTerritory = nullptr;
          ++m_nLineNumber;
      }
      category_def_list SKILL_END
      {
          // Current actions 1 and 3 share the complete skill_end path.
          CSkillInfo* const skill = m_pSkillInfo;
          if (!skill->IsDurationSkill()) {
              if (skill->m_nAbnormalType != ABNS_INVALID) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"invalid abnormal type at [%d] Line = [%d]",
                  //     m_lexer.line + 1,
                  //     m_nLineNumber)
                  // L2_LOG_ERROR(Log,
                  //     L"invalid abnormal type at [%d] Line = [%d]",
                  //     m_lexer.line + 1,
                  //     m_nLineNumber)
                  L2_LOG_ERROR(
                      Log, "skilldata.txt line %lld: non-duration skill %d has abnormal type %d; the value is retained.",
                      static_cast<long long>(m_lexer.line) + 1, skill->m_nSkillId, static_cast<int>(skill->m_nAbnormalType)
                  );
              }
          } else if (skill->m_nAbnormalType == ABNS_MAX || skill->m_nAbnormalType == ABNS_INVALID) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"invalid abnormal type at [%d] Line = [%d]",
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"invalid abnormal type at [%d] Line = [%d]",
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: duration skill %d has invalid abnormal type %d; using the public abnormal "
                  "slot.",
                  static_cast<long long>(m_lexer.line) + 1, skill->m_nSkillId, static_cast<int>(skill->m_nAbnormalType)
              );
              skill->m_nAbnormalType = ABNS_PUBLIC_SLOT;
          }
          if (skill->m_OperateType == SKILL_MAX) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"m_OperateType is empty() at [%d] Line = [%d]",
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"m_OperateType is empty() at [%d] Line = [%d]",
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log, "skilldata.txt line %lld: skill %d has no valid operate_type; skill_end finalization is skipped.",
                  static_cast<long long>(m_lexer.line) + 1, skill->m_nSkillId
              );
              break;
          }
          const int debuffType = static_cast<int>(skill->m_eDebuff);
          if (debuffType < DB_GOOD || debuffType >= DB_MAX) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[%s][%d][%d] skill buff type[%d] is wrong. [%d] Line = [%d]",
              //     skill->m_pszName,
              //     skill->m_nSkillId,
              //     skill->m_nLevel,
              //     debuffType,
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"[%s][%d][%d] skill buff type[%d] is wrong. [%d] Line = [%d]",
              //     skill->m_pszName,
              //     skill->m_nSkillId,
              //     skill->m_nLevel,
              //     debuffType,
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log, "skilldata.txt line %lld: skill %d has invalid buff type %d; the value is retained.",
                  static_cast<long long>(m_lexer.line) + 1, skill->m_nSkillId, debuffType
              );
          }
          if (skill->m_pszName == nullptr) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"skill name is null! Line = %d",
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"skill name is null! Line = %d",
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log, "skilldata.txt line %lld: skill %d has a null name; record finalization stops.",
                  static_cast<long long>(m_lexer.line) + 1, skill->m_nSkillId
              );
              m_pSkillInfo = nullptr;
              break;
          }
          if (skill->m_nLevel < 1 || skill->m_nSubLevel < 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[%s][%d][%d][%d] skill level setting has problem. line = %d",
              //     skill->m_pszName,
              //     skill->m_nSkillId,
              //     skill->m_nLevel,
              //     skill->m_nSubLevel,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"[%s][%d][%d][%d] skill level setting has problem. line = %d",
              //     skill->m_pszName,
              //     skill->m_nSkillId,
              //     skill->m_nLevel,
              //     skill->m_nSubLevel,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: skill %d has invalid level %d or sublevel %d; level must be positive and "
                  "sublevel nonnegative. Record finalization stops.",
                  static_cast<long long>(m_lexer.line) + 1, skill->m_nSkillId, skill->m_nLevel, skill->m_nSubLevel
              );
              m_pSkillInfo = nullptr;
              break;
          }
          if (ScopeRequiresPositiveRange(skill->m_AffectScope) && skill->m_nAffectRange < 1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[%s][%d][%d] skill range is less or equal to 0. line = %d",
              //     skill->m_pszName,
              //     skill->m_nSkillId,
              //     skill->m_nLevel,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"[%s][%d][%d] skill range is less or equal to 0. line = %d",
              //     skill->m_pszName,
              //     skill->m_nSkillId,
              //     skill->m_nLevel,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: skill %d has affect range %d for scope %d which requires a positive range; "
                  "record finalization stops.",
                  static_cast<long long>(m_lexer.line) + 1, skill->m_nSkillId, skill->m_nAffectRange,
                  static_cast<int>(skill->m_AffectScope)
              );
              m_pSkillInfo = nullptr;
              break;
          }
          if (skill->m_nSubAbnormal != ABNS_MAX && skill->m_nSubAbnormal != ABNS_INVALID && skill->m_eDebuff == DB_GOOD) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[%s] Duple Debuff is Not GOOD_BUF. line = %d",
              //     skill->m_pszName,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"[%s] Duple Debuff is Not GOOD_BUF. line = %d",
              //     skill->m_pszName,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: skill %d defines secondary abnormal type %d for a beneficial buff; the "
                  "settings are retained.",
                  static_cast<long long>(m_lexer.line) + 1, skill->m_nSkillId, static_cast<int>(skill->m_nSubAbnormal)
              );
          }
          if (!m_bNewSkill) {
              skill->PostLoad();
          } else {
              g_SkillDB.Add(skill);
              if (m_pPrevSkillInfo != nullptr && m_pPrevSkillInfo->m_nSkillId == skill->m_nSkillId &&
                  m_pPrevSkillInfo->m_nLevel + 1 == skill->m_nLevel) {
                  m_pPrevSkillInfo->m_pNextLevel = skill;
                  skill->m_pPrevLevel = m_pPrevSkillInfo;
              }
          }
          g_SkillDB.m_pPreviousSkillInfo = skill;
          m_pPrevSkillInfo = skill;
          m_pSkillInfo = nullptr;
          m_EffectActivateTiming = CSkillInfo::EAT_INVALID;
          m_ConditionTarget = CSkillInfo::OCT_INVALID;
          m_pTerritory = nullptr;
      }
    ;

category_def_list
    : category_def_list category_def
    | %empty
    ;

category_def
    : CATEGORY EQUAL '{' n64 '}'
    | REUSE_DELAY_TYPE EQUAL IDENTIFIER
      {
          if (TargetWordEquals(SemanticWord($3), u"N")) {
              m_pSkillInfo->m_ReuseDelayType = SRDT_NORMAL;
          } else if (TargetWordEquals(SemanticWord($3), u"S")) {
              m_pSkillInfo->m_ReuseDelayType = SRDT_SUPER;
          }
      }
    | FAN_RANGE EQUAL '{' PREDEFINED ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED '}'
      {
          m_pSkillInfo->m_DamageRange.target = grammar::SemanticInt32($4);
          m_pSkillInfo->m_DamageRange.direction = grammar::SemanticInt32($6);
          m_pSkillInfo->m_DamageRange.distance = grammar::SemanticInt32($8);
          m_pSkillInfo->m_DamageRange.angle = grammar::SemanticInt32($10);
      }
    | CATEGORY EQUAL '{' '}'
    | ATTRIBUTE EQUAL '{' ATTR_VALUE ';' PREDEFINED '}'
      {
          m_pSkillInfo->m_eAttribute = static_cast<AttributeTypeEnum>(grammar::SemanticInt32($4));
          m_pSkillInfo->m_AttributePoint = grammar::SemanticInt32($6);
      }
    | TRAIT EQUAL '{' TRAIT_VALUE '}'
      {
          m_pSkillInfo->m_TraitType = static_cast<TraitTypeEnum>(grammar::SemanticInt32($4));
      }
    | IS_MAGIC EQUAL PREDEFINED
      {
          m_pSkillInfo->m_nIsMagic = grammar::SemanticInt32($3);
          if (const int magicType = m_pSkillInfo->IsMagic(); magicType < 0 || magicType >= SIMT_MAGIC_SKILL_MAX) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"is magic should be in 0 ~ 7, 10 ~ 13, 20 ~ 25 [%d] at [%d] Line = [%d]",
              //     grammar::SemanticInt32($3),
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"is magic should be in 0 ~ 7, 10 ~ 13, 20 ~ 25 [%d] at [%d] Line = [%d]",
              //     grammar::SemanticInt32($3),
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: is_magic %d resolves to unsupported magic type %d; the value is retained.",
                  static_cast<long long>(m_lexer.line) + 1, m_pSkillInfo->m_nIsMagic, magicType
              );
          }
      }
    | IS_HATE EQUAL PREDEFINED
      {
          m_pSkillInfo->m_nIsHate = grammar::SemanticInt32($3);
          if (m_pSkillInfo->m_nIsHate != 0 && m_pSkillInfo->m_nIsHate != 1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"is_hate(%d) should be in 0 or 1, at [%d] Line = [%d]",
              //     m_pSkillInfo->m_nIsHate,
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"is_hate(%d) should be in 0 or 1, at [%d] Line = [%d]",
              //     m_pSkillInfo->m_nIsHate,
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log, "skilldata.txt line %lld: is_hate is %d; expected 0 or 1. The value is retained.",
                  static_cast<long long>(m_lexer.line) + 1, m_pSkillInfo->m_nIsHate
              );
          }
      }
    | MAGIC_LEVEL EQUAL PREDEFINED
      { m_pSkillInfo->m_nMagicLevel = grammar::SemanticInt32($3); }
    | LEVEL EQUAL PREDEFINED
      { m_pSkillInfo->m_nLevel = grammar::SemanticInt32($3); }
    | SUB_LEVEL EQUAL PREDEFINED
      { m_pSkillInfo->m_nSubLevel = grammar::SemanticInt32($3); }
    | OPERATE_TYPE EQUAL IDENTIFIER
      {
          const char16_t* const name = SemanticWord($3);
          SkillOperateType operateType = SKILL_MAX;
          if (TargetWordEquals(name, u"A1")) {
              operateType = SKILL_INSTANT;
          } else if (TargetWordEquals(name, u"A2")) {
              operateType = SKILL_DURATION;
          } else if (TargetWordEquals(name, u"A3")) {
              operateType = SKILL_SELFDURATION;
          } else if (TargetWordEquals(name, u"P")) {
              operateType = SKILL_PASSIVE;
          } else if (TargetWordEquals(name, u"T")) {
              operateType = SKILL_TOGGLE;
          } else if (TargetWordEquals(name, u"CA1")) {
              operateType = SKILL_CHANNELLING_INSTANCE;
          } else if (TargetWordEquals(name, u"CA2")) {
              operateType = SKILL_CHANNELLING_DURATION;
          } else if (TargetWordEquals(name, u"CA5")) {
              operateType = SKILL_CHANNELLING_DURATION_LEVEL_UP;
          } else if (TargetWordEquals(name, u"DA1")) {
              operateType = SKILL_CHARGE_INSTANT;
          } else if (TargetWordEquals(name, u"DA2")) {
              operateType = SKILL_CHARGE_DURATION;
          } else if (TargetWordEquals(name, u"DA3")) {
              operateType = SKILL_BACK_STEP_INSTANT;
          } else if (TargetWordEquals(name, u"DA4")) {
              operateType = SKILL_BACK_STEP_DURATION;
          } else if (TargetWordEquals(name, u"DA5")) {
              operateType = SKILL_BLINK_INSTANT;
          } else if (TargetWordEquals(name, u"DA6")) {
              operateType = SKILL_BACK_STEP_DURATION_WITH_TARGET_KNOCKBACK;
          } else if (TargetWordEquals(name, u"AU")) {
              operateType = SKILL_AURA;
          } else if (TargetWordEquals(name, u"A4")) {
              operateType = SKILL_DURATION_LEVEL_UP;
          } else if (TargetWordEquals(name, u"A5")) {
              operateType = SKILL_AURA_ACTIVITY;
          } else if (TargetWordEquals(name, u"A6")) {
              operateType = SKILL_AURA_ACTIVITY2;
          } else if (TargetWordEquals(name, u"TG")) {
              operateType = SKILL_GROUP_TOGGLE;
          } else if (TargetWordEquals(name, u"AP")) {
              operateType = SKILL_PARTY_INSTANT;
          } else if (TargetWordEquals(name, u"DP1")) {
              operateType = SKILL_DUAL_PUMP1;
          } else if (TargetWordEquals(name, u"RP")) {
              operateType = SKILL_RACE_SPECIAL_PASSIVE;
          } else {
              const std::string hostName = Utf16ToUtf8(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Invalid OperateType [%s] at [%d] Line = [%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"Invalid OperateType [%s] at [%d] Line = [%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log, "skilldata.txt line %lld: unknown operate_type '%s'; the field is left unchanged.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
              );
              break;
          }
          m_pSkillInfo->m_OperateType = operateType;
      }
    | MP_CONSUME1 EQUAL predefined_def
      {
          m_pSkillInfo->m_nMpConsume1 = runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($3));
      }
    | MP_CONSUME2 EQUAL predefined_def
      {
          m_pSkillInfo->m_nMpConsume2 = runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($3));
      }
    | PLEDGE_NV_CONSUME EQUAL predefined_def
      {
          m_pSkillInfo->m_nPledgeNameValueConsume = runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($3));
      }
    | PVP_POINT_CONSUME EQUAL predefined_def
      { m_pSkillInfo->m_nPVPPointConsume = 0; }
    | MP_CONSUME_TICK EQUAL predefined_def
      {
          m_pSkillInfo->m_nMpConsumeTick = runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($3));
      }
    | HP_CONSUME_TICK EQUAL predefined_def
      {
          m_pSkillInfo->m_nHpConsumeTick = runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($3));
      }
    | HP_CONSUME EQUAL PREDEFINED
      { m_pSkillInfo->m_nHpConsume = grammar::SemanticInt32($3); }
    | ITEM_CONSUME EQUAL '{' NAME ';' PREDEFINED '}'
      {
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          const char16_t* const consumeItemName = SemanticWord($4);
          g_SkillInfoTempDataDB[m_pSkillInfo->GetKey()].m_sConsumeItemName.assign(consumeItemName);
          m_pSkillInfo->m_nConsumeItemCount = static_cast<std::int64_t>(grammar::SemanticInt32($6));
          if (g_PrecompiledHeaderDB.Search(consumeItemName) == -1) {
              const std::string hostName = Utf16ToUtf8(consumeItemName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"item_consume[%s] is not in PrecompiledDB. line = %d",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"item_consume[%s] is not in PrecompiledDB. line = %d",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: item_consume reference '%s' resolves to -1 in PCH; the name and count are "
                  "retained for post-load resolution.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
              );
          }
      }
    | ITEM_CONSUME EQUAL '{' NAME ';' PREDEFINED ';' INSELF_INMASTER '}'
      {
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          const char16_t* const consumeItemName = SemanticWord($4);
          g_SkillInfoTempDataDB[m_pSkillInfo->GetKey()].m_sConsumeItemName.assign(consumeItemName);
          m_pSkillInfo->m_nConsumeItemCount = static_cast<std::int64_t>(grammar::SemanticInt32($6));
          m_pSkillInfo->m_eConsumeItemSearchType = static_cast<ConsumeItemSearchType::Type>(grammar::SemanticInt32($8));
          if (g_PrecompiledHeaderDB.Search(consumeItemName) == -1) {
              const std::string hostName = Utf16ToUtf8(consumeItemName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"item_consume[%s] is not in PrecompiledDB. line = %d",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"item_consume[%s] is not in PrecompiledDB. line = %d",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: item_consume reference '%s' resolves to -1 in PCH; the name and count are "
                  "retained for post-load resolution.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
              );
          }
      }
    | CAST_RANGE EQUAL PREDEFINED
      { m_pSkillInfo->m_nCastRange = grammar::SemanticInt32($3); }
    | EFFECTIVE_RANGE EQUAL PREDEFINED
      { m_pSkillInfo->m_nEffectiveRange = grammar::SemanticInt32($3); }
    | SKILL_HIT_TIME EQUAL predefined_def
      { m_pSkillInfo->m_dSkillHitTime = grammar::SemanticDouble($3); }
    | SKILL_COOL_TIME EQUAL predefined_def
      { m_pSkillInfo->m_dSkillCoolTime = grammar::SemanticDouble($3); }
    | SKILL_HIT_CANCEL_TIME EQUAL predefined_def
      { m_pSkillInfo->m_dSkillHitCancelTime = grammar::SemanticDouble($3); }
    | REUSE_DELAY EQUAL predefined_def
      {
          // Current .rdata 0x00e461c0 is the exact double 1000.0. The
          // target performs CVTTSD2SI after multiplication, then widens the
          // resulting int32 back to this double field.
          m_pSkillInfo->m_nReuseDelay =
              static_cast<double>(runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($3) * 1000.0));
      }
    | TOGGLE_GROUP_ID EQUAL PREDEFINED
      {
          m_nToggleGroupId = grammar::SemanticInt32($3);
          if (m_nToggleGroupId < 1) {
              break;
          }
          m_pSkillInfo->m_nToggleGroupId = m_nToggleGroupId;
          const auto found = m_mapToggleGroup.find(m_nToggleGroupId);
          if (found != m_mapToggleGroup.end()) {
              found->second.push_back(m_pSkillInfo->m_nSkillId);
              break;
          }
          std::vector<int> skillIds;
          skillIds.push_back(m_pSkillInfo->m_nSkillId);
          m_mapToggleGroup.emplace(m_nToggleGroupId, skillIds);
      }
    | ATTACH_TOGGLE_ID EQUAL PREDEFINED
      { m_pSkillInfo->m_nAttachToggleGroupId = grammar::SemanticInt32($3); }
    | ATTACH_TOGGLE_LIST EQUAL n23
    | DOUBLE_CAST_SKILL EQUAL PREDEFINED
      { m_pSkillInfo->m_nAttachDoubleCastSkillId = grammar::SemanticInt32($3); }
    | IS_DOUBLE EQUAL PREDEFINED
      { m_pSkillInfo->m_bIsDoubleCasting = $3 == 1; }
    | CHANGE_SKILL_ID EQUAL PREDEFINED
      { m_pSkillInfo->m_nChangeBuffId = grammar::SemanticInt32($3); }
    | RESIST_CAST EQUAL '{' n66 '}'
      {
          auto* const names = grammar::SemanticPointer<std::vector<char16_t*>>($4);
          if (names == nullptr || names->empty()) {
              // The current body returns without deleting an empty semantic
              // vector. Preserve that generated-parser ownership quirk.
              break;
          }
          for (char16_t* const name : *names) {
              if (TargetWordEquals(name, u"all")) {
                  m_pSkillInfo->SetAvoidCanceling(true);
                  break;
              }

              const AbnormalStatusSlotType abnormal = FindAbnormalByName(name, 0);
              if (abnormal == ABNS_MAX) {
                  // Current passes the semantic vector pointer, not the
                  // current name, to the %s slot. Keep its raw vararg bits.
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"invalid resist_cast at [%d] (%s). Line = [%d]",
                  //     m_lexer.line + 1,
                  //     reinterpret_cast<const wchar_t*>(names),
                  //     m_nLineNumber)
                  // L2_LOG_ERROR(Log,
                  //     L"invalid resist_cast at [%d] (%s). Line = [%d]",
                  //     m_lexer.line + 1,
                  //     reinterpret_cast<const wchar_t*>(names),
                  //     m_nLineNumber)
                  L2_LOG_ERROR(
                      Log,
                      "skilldata.txt line %lld: resist_cast contains unresolved abnormal slot '%s'; this entry is "
                      "omitted.",
                      static_cast<long long>(m_lexer.line) + 1, Utf16ToUtf8(name).c_str()
                  );
              } else {
                  m_pSkillInfo->m_bAvoidCancelingSkill[abnormal] = true;
              }
          }
          for (char16_t* const name : *names) {
              std::free(name);
          }
          delete names;
      }
    | IS_NECESSARYTG EQUAL PREDEFINED
      { m_pSkillInfo->m_bIsNeccessaryTG = grammar::SemanticInt32($3) != 0; }
    | NO_TRANSFER_ABNORMAL EQUAL PREDEFINED
      { m_pSkillInfo->m_nTransferAbnormal = grammar::SemanticInt32($3); }
    | EXCEPT_BLOCK EQUAL PREDEFINED
      { m_pSkillInfo->m_bExceptBlock = grammar::SemanticInt32($3) != 0; }
    | EXCEPT_RESTORE EQUAL PREDEFINED
      { m_pSkillInfo->m_bExceptRestore = grammar::SemanticInt32($3) != 0; }
    | CHARGE_DIRECTION EQUAL FRONT_GROUP
      {
          m_pSkillInfo->m_eChargeDirection = static_cast<SkillFxPositionType>(grammar::SemanticInt32($3));
          if (m_pSkillInfo->m_eChargeDirection != SFPT_SIDE && m_pSkillInfo->m_eChargeDirection != SFPT_MAX) {
              break;
          }
          // The current binary feeds the raw numeric semantic value to this
          // diagnostic's %s conversion. This is a shipped invalid-input bug.
          // Log.Add(
          //     LOG_ERROR,
          //     L"invalid charge_direction type at [%d] (%s). Must be (left, right, front, back) Line = [%d]",
          //     m_lexer.line + 1,
          //     reinterpret_cast<const wchar_t*>(
          //         static_cast<std::uintptr_t>($3)),
          //     m_nLineNumber)
          // L2_LOG_ERROR(Log,
          //     L"invalid charge_direction type at [%d] (%s). Must be (left, right, front, back) Line = [%d]",
          //     m_lexer.line + 1,
          //     reinterpret_cast<const wchar_t*>(
          //         static_cast<std::uintptr_t>($3)),
          //     m_nLineNumber)
          L2_LOG_ERROR(
              Log,
              "skilldata.txt line %lld: charge_direction value %d is unsupported here; expected left, right, front or "
              "back. The value is retained.",
              static_cast<long long>(m_lexer.line) + 1, static_cast<int>(m_pSkillInfo->m_eChargeDirection)
          );
      }
    | AFTER_EFFECT_TYPE EQUAL PREDEFINED
      {
          m_pSkillInfo->m_nAfterEffectType = grammar::SemanticInt32($3);
          if (m_pSkillInfo->m_nAfterEffectType < 0 || m_pSkillInfo->m_nAfterEffectType > 1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"invalid after_effect_type [%d] at [%d] Line = [%d]",
              //     grammar::SemanticInt32($3),
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"invalid after_effect_type [%d] at [%d] Line = [%d]",
              //     grammar::SemanticInt32($3),
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log, "skilldata.txt line %lld: after_effect_type is %d; expected 0 or 1. The value is retained.",
                  static_cast<long long>(m_lexer.line) + 1, m_pSkillInfo->m_nAfterEffectType
              );
          }
      }
    | SUBORDINATION_ABNORMAL EQUAL IDENTIFIER
      {
          const char16_t* const name = SemanticWord($3);
          const AbnormalStatusSlotType abnormal = FindAbnormalByName(name, m_lexer.line + 1);
          if (abnormal == ABNS_MAX) {
              const std::string hostName = Utf16ToUtf8(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"invalid abnormal type at [%d] (%s). Line = [%d]",
              //     m_lexer.line + 1,
              //     hostName.c_str(),
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"invalid abnormal type at [%d] (%s). Line = [%d]",
              //     m_lexer.line + 1,
              //     hostName.c_str(),
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log, "skilldata.txt line %lld: unknown secondary abnormal slot '%s'; unresolved value %d is retained.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str(), static_cast<int>(abnormal)
              );
          }
          m_pSkillInfo->m_nSubAbnormal = abnormal;
      }
    | EVENT_TYPE EQUAL PREDEFINED
      { m_pSkillInfo->m_nEventType = grammar::SemanticInt32($3); }
    | REUSE_DELAY_GROUP EQUAL PREDEFINED
      { m_pSkillInfo->m_nReuseDelayGroup = grammar::SemanticInt32($3); }
    | NPC_NOTICE EQUAL PREDEFINED
      { m_pSkillInfo->m_nNpcNotice = grammar::SemanticInt32($3); }
    | IS_AGATHION_MAIN_SKILL EQUAL PREDEFINED
      {
          const int enabled = grammar::SemanticInt32($3);
          if (enabled < 1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"invalid [is_agathion_main_skill=%d] in line [%d]",
              //     enabled,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"invalid [is_agathion_main_skill=%d] in line [%d]",
              //     enabled,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: is_agathion_main_skill is %d; expected a positive value. The flag is still "
                  "enabled.",
                  static_cast<long long>(m_lexer.line) + 1, enabled
              );
          }
          // The current action sets true even after diagnosing zero/negative.
          m_pSkillInfo->SetIsAgathionMainSkill(true);
      }
    | UNAVAILABLE_COND EQUAL '{' n22 '}'
    | OPERATE_COND EQUAL
      { m_ConditionTarget = CSkillInfo::OCT_CASTER; }
      '{' n22 '}'
    | TARGET_OPERATE_COND EQUAL
      { m_ConditionTarget = CSkillInfo::OCT_TARGET; }
      '{' n22 '}'
    | PASSIVE_CONDITIONS EQUAL
      { m_ConditionTarget = CSkillInfo::OCT_PASSIVE; }
      '{' n22 '}'
    | ACTIVATE_RATE EQUAL predefined_def
      {
          m_pSkillInfo->m_nActivateRate = runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($3));
      }
    | PVP_ACTIVATE_RATE EQUAL predefined_def
      {
          m_pSkillInfo->m_nPVPActivateRate = runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($3));
      }
    | PVE_ACTIVATE_RATE EQUAL predefined_def
      {
          m_pSkillInfo->m_nPVEActivateRate = runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($3));
      }
    | LV_BONUS_RATE EQUAL predefined_def
      { m_pSkillInfo->m_dLevelBonusRate = grammar::SemanticDouble($3); }
    | BASIC_PROPERTY EQUAL IDENTIFIER
      {
          const char16_t* const property = SemanticWord($3);
          if (TargetWordEquals(property, u"str")) {
              m_pSkillInfo->m_BasicProperty = STE_STR;
          } else if (TargetWordEquals(property, u"dex")) {
              m_pSkillInfo->m_BasicProperty = STE_DEX;
          } else if (TargetWordEquals(property, u"con")) {
              m_pSkillInfo->m_BasicProperty = STE_CON;
          } else if (TargetWordEquals(property, u"int")) {
              m_pSkillInfo->m_BasicProperty = STE_INT;
          } else if (TargetWordEquals(property, u"men")) {
              m_pSkillInfo->m_BasicProperty = STE_MEN;
          } else if (TargetWordEquals(property, u"wit")) {
              m_pSkillInfo->m_BasicProperty = STE_WIT;
          } else if (TargetWordEquals(property, u"physical_abnormal_resist")) {
              m_pSkillInfo->m_BasicProperty = STE_P_ABNORMAL_RESIST;
          } else if (TargetWordEquals(property, u"magic_abnormal_resist")) {
              m_pSkillInfo->m_BasicProperty = STE_M_ABNORMAL_RESIST;
          } else if (TargetWordEquals(property, u"none")) {
              m_pSkillInfo->m_BasicProperty = STE_MAX;
          } else {
              const std::string hostProperty = Utf16ToUtf8(property);
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s : invalid property at [%d] (%s). Line = [%d]",
              //     L"..\\Script\\skilldata.txt",
              //     m_lexer.line + 1,
              //     hostProperty.c_str(),
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"%s : invalid property at [%d] (%s). Line = [%d]",
              //     L"..\\Script\\skilldata.txt",
              //     m_lexer.line + 1,
              //     hostProperty.c_str(),
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log, "skilldata.txt line %lld: unknown basic property '%s'; the field is left unchanged.",
                  static_cast<long long>(m_lexer.line) + 1, hostProperty.c_str()
              );
          }
      }
    | EFFECT EQUAL
      { m_EffectActivateTiming = CSkillInfo::EAT_SUCCESS; }
      '{' n47 '}'
    | START_EFFECT EQUAL
      { m_EffectActivateTiming = CSkillInfo::EAT_START; }
      '{' n47 '}'
    | FAIL_EFFECT EQUAL
      { m_EffectActivateTiming = CSkillInfo::EAT_CANCEL; }
      '{' n47 '}'
    | TICK_EFFECT EQUAL
      { m_EffectActivateTiming = CSkillInfo::EAT_TICK; }
      '{' n47 '}'
    | END_EFFECT EQUAL
      { m_EffectActivateTiming = CSkillInfo::EAT_END; }
      '{' n47 '}'
    | SELF_EFFECT EQUAL
      { m_EffectActivateTiming = CSkillInfo::EAT_SELF; }
      '{' n47 '}'
    | PVP_EFFECT EQUAL
      { m_EffectActivateTiming = CSkillInfo::EAT_PVP; }
      '{' n47 '}'
    | PVE_EFFECT EQUAL
      { m_EffectActivateTiming = CSkillInfo::EAT_PVE; }
      '{' n47 '}'
    | ATTACHED_SKILL EQUAL '{' NAME '}'
      {
          g_SkillInfoTempDataDB[m_pSkillInfo->GetKey()].m_sAttachedSkillName.assign(SemanticWord($4));
      }
    | TICK_INTERVAL EQUAL predefined_def
      { m_pSkillInfo->m_dTickInterval = grammar::SemanticDouble($3) * 1000.0; }
    | CHANNELING_START EQUAL predefined_def
      { m_pSkillInfo->m_dChannelingStart = grammar::SemanticDouble($3); }
    | ABNORMAL_TIME EQUAL PREDEFINED
      { m_pSkillInfo->m_nAbnormalTime = grammar::SemanticInt32($3); }
    | PVP_ABNORMAL_TIME EQUAL PREDEFINED
      { m_pSkillInfo->m_nPVPAbnormalTime = grammar::SemanticInt32($3); }
    | PVE_ABNORMAL_TIME EQUAL PREDEFINED
      { m_pSkillInfo->m_nPVEAbnormalTime = grammar::SemanticInt32($3); }
    | ABNORMAL_LV EQUAL PREDEFINED
      { m_pSkillInfo->m_nAbnormalLv = grammar::SemanticInt32($3); }
    | ABNORMAL_TYPE EQUAL IDENTIFIER
      {
          const char16_t* const name = SemanticWord($3);
          const AbnormalStatusSlotType abnormal = FindAbnormalByName(name, m_lexer.line + 1);
          if (abnormal == ABNS_MAX) {
              const std::string hostName = Utf16ToUtf8(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"invalid abnormal type at [%d] (%s). Line = [%d]",
              //     m_lexer.line + 1,
              //     hostName.c_str(),
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"invalid abnormal type at [%d] (%s). Line = [%d]",
              //     m_lexer.line + 1,
              //     hostName.c_str(),
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log, "skilldata.txt line %lld: unknown abnormal slot '%s'; unresolved value %d is retained.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str(), static_cast<int>(abnormal)
              );
          }
          m_pSkillInfo->m_nAbnormalType = abnormal;
      }
    | ABNORMAL_TIME_CRITICAL EQUAL PREDEFINED
      { m_pSkillInfo->m_bAbnormalTimeCritical = $3 == 1; }
    | ABNORMAL_VISUAL_EFFECT EQUAL '{' n66 '}'
      {
          auto* const names = grammar::SemanticPointer<std::vector<char16_t*>>($4);
          if (names == nullptr || names->empty()) {
              // Current generated ownership leaks an empty semantic vector.
              break;
          }
          for (char16_t* const name : *names) {
              const std::u16string stagedName(name);
              const int visualEffect = static_cast<int>(g_PrecompiledHeaderDB.Search(name));
              if (visualEffect == -1) {
                  const std::string hostName = Utf16ToUtf8(name);
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"abnormalvisualeffect = %s : Error. Line=[%d]",
                  //     hostName.c_str(),
                  //     m_nLineNumber)
                  // L2_LOG_ERROR(Log,
                  //     L"abnormalvisualeffect = %s : Error. Line=[%d]",
                  //     hostName.c_str(),
                  //     m_nLineNumber)
                  L2_LOG_ERROR(
                      Log,
                      "skilldata.txt line %lld: abnormalvisualeffect reference '%s' resolves to -1; this visual effect is "
                      "omitted.",
                      static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
                  );
              } else if (!TargetWordEquals(name, u"br_ave_none")) {
                  m_pSkillInfo->m_AbnormalVisualEffect.insert(visualEffect);
              }
              (void)stagedName;
          }
          for (char16_t* const name : *names) {
              std::free(name);
          }
          delete names;
      }
    | MULTI_CLASS EQUAL PREDEFINED
      { m_pSkillInfo->m_nMultiClass = grammar::SemanticInt32($3); }
    | ABNORMAL_INSTANT EQUAL PREDEFINED
      { m_pSkillInfo->m_bInstantItem = $3 == 1; }
    | EFFECT_POINT EQUAL PREDEFINED
      { m_pSkillInfo->m_nEffectPoint = grammar::SemanticInt32($3); }
    | DEBUFF EQUAL PREDEFINED
      {
          m_pSkillInfo->m_eDebuff = static_cast<DebuffType>(grammar::SemanticInt32($3));
          if (m_pSkillInfo->m_eDebuff < DB_GOOD || m_pSkillInfo->m_eDebuff > DB_MENTAL_MES) {
              m_pSkillInfo->m_eDebuff = DB_MAX;
          }
          if (m_pSkillInfo->m_OperateType == SKILL_DUAL_PUMP1 && m_pSkillInfo->m_eDebuff != DB_BAD) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"DP1 type must be DEBUFF!!!! at [%d]. Line=[%d]",
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"DP1 type must be DEBUFF!!!! at [%d]. Line=[%d]",
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log, "skilldata.txt line %lld: DP1 skill has buff type %d instead of DB_BAD; the value is retained.",
                  static_cast<long long>(m_lexer.line) + 1, static_cast<int>(m_pSkillInfo->m_eDebuff)
              );
          }
      }
    | IRREPLACEABLE_BUFF EQUAL PREDEFINED
      {
          const std::int64_t value = grammar::SemanticInt64($3);
          m_pSkillInfo->m_IrreplaceableBuff = value != 0;
          if (value < 0 || value > 1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"irreplaceable_buff should be 0 or 1. at [%d]. Line=[%d]",
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"irreplaceable_buff should be 0 or 1. at [%d]. Line=[%d]",
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: irreplaceable_buff is %lld; expected 0 or 1. Its nonzero value still enables "
                  "the flag.",
                  static_cast<long long>(m_lexer.line) + 1, static_cast<long long>(value)
              );
          }
      }
    | RIDE_STATE EQUAL '{' n63 '}'
      {
          auto* const rideStates = grammar::SemanticPointer<std::vector<std::int64_t>>($4);
          m_pSkillInfo->SetRideState(rideStates);
          delete rideStates;
      }
    | TARGET_TYPE EQUAL SELF_GROUP
      {
          m_pSkillInfo->m_TargetType = static_cast<SkillTargetTypeEnum>(grammar::SemanticInt32($3));
      }
    | TARGET_TYPE EQUAL IDENTIFIER
    | AFFECT_SCOPE EQUAL SINGLE_GROUP
      {
          m_pSkillInfo->m_AffectScope = static_cast<SkillAffectScopeEnum>(grammar::SemanticInt32($3));
      }
    | AFFECT_LIMIT EQUAL '{' PREDEFINED ';' PREDEFINED '}'
      {
          m_pSkillInfo->m_nAffectLimitBase = grammar::SemanticInt32($4);
          m_pSkillInfo->m_nAffectLimitRandom = grammar::SemanticInt32($6);
          m_pSkillInfo->m_AffectLimitToPC = false;
      }
    | AFFECT_LIMIT EQUAL '{' PREDEFINED ';' PREDEFINED ';' PREDEFINED '}'
      {
          m_pSkillInfo->m_nAffectLimitBase = grammar::SemanticInt32($4);
          m_pSkillInfo->m_nAffectLimitRandom = grammar::SemanticInt32($6);
          m_pSkillInfo->m_AffectLimitToPC = $8 == 1;
      }
    | AFFECT_SCOPE_HEIGHT EQUAL '{' PREDEFINED ';' PREDEFINED '}'
      {
          m_pSkillInfo->m_nAffectScopeMinZ = grammar::SemanticInt32($4);
          m_pSkillInfo->m_nAffectScopeMaxZ = grammar::SemanticInt32($6);
      }
    | AFFECT_SCOPE EQUAL IDENTIFIER
    | AFFECT_RANGE EQUAL PREDEFINED
      { m_pSkillInfo->m_nAffectRange = grammar::SemanticInt32($3); }
    | AFFECT_OBJECT EQUAL UNDEAD_REAL_ENEMY_GROUP
      {
          m_pSkillInfo->m_AffectObjectType = static_cast<SkillAffectObjectTypeEnum>(grammar::SemanticInt32($3));
      }
    | AFFECT_OBJECT EQUAL IDENTIFIER
      {
          if (TargetWordEquals(SemanticWord($3), u"none")) {
              m_pSkillInfo->m_AffectObjectType = SAFOT_NONE;
          }
      }
    | AFFECT_OBJECT EQUAL ALL
      { m_pSkillInfo->m_AffectObjectType = SAFOT_ALL; }
    | NEXT_ACTION EQUAL IDENTIFIER
      {
          const char16_t* const nextAction = SemanticWord($3);
          if (TargetWordEquals(nextAction, u"stand")) {
              m_pSkillInfo->m_NextAction = static_cast<ActionType>(1);
          } else if (TargetWordEquals(nextAction, u"attack")) {
              m_pSkillInfo->m_NextAction = static_cast<ActionType>(4);
          } else if (TargetWordEquals(nextAction, u"none")) {
              m_pSkillInfo->m_NextAction = static_cast<ActionType>(0);
          } else if (TargetWordEquals(nextAction, u"sit")) {
              m_pSkillInfo->m_NextAction = static_cast<ActionType>(2);
          } else if (TargetWordEquals(nextAction, u"fake_death")) {
              m_pSkillInfo->m_NextAction = static_cast<ActionType>(2);
              m_pSkillInfo->m_bIsFakeDeath = true;
          } else if (TargetWordEquals(nextAction, u"skill")) {
              if (m_pSkillInfo->m_OperateType == SKILL_INSTANT) {
                  m_pSkillInfo->m_NextAction = static_cast<ActionType>(5);
              } else {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"invalid next action at [%d]. Line=[%d] / Must be operate_type = A1",
                  //     m_lexer.line + 1,
                  //     m_nLineNumber)
                  // L2_LOG_ERROR(Log,
                  //     L"invalid next action at [%d]. Line=[%d] / Must be operate_type = A1",
                  //     m_lexer.line + 1,
                  //     m_nLineNumber)
                  L2_LOG_ERROR(
                      Log,
                      "skilldata.txt line %lld: next_action=skill requires operate_type=A1; the action is left unchanged.",
                      static_cast<long long>(m_lexer.line) + 1
                  );
              }
          } else {
              // Log.Add(
              //     LOG_ERROR,
              //     L"invalid next action at [%d]. Line=[%d]",
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"invalid next action at [%d]. Line=[%d]",
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log, "skilldata.txt line %lld: unknown next_action '%s'; the action is left unchanged.",
                  static_cast<long long>(m_lexer.line) + 1, Utf16ToUtf8(nextAction).c_str()
              );
          }
      }
    | REUSE_DELAY_LOCK EQUAL PREDEFINED
      {
          if (grammar::SemanticInt32($3) == 1) {
              m_pSkillInfo->m_bReuseDelayLock = true;
          }
      }
    | CONSUME_ETC EQUAL '{' PREDEFINED ';' PREDEFINED '}'
      {
          m_pSkillInfo->m_nEnergy = grammar::SemanticInt32($4);
          m_pSkillInfo->m_nSoul = grammar::SemanticInt32($6);
      }
    | CONSUME_ETC EQUAL '{' PREDEFINED ';' PREDEFINED ';' PREDEFINED '}'
      {
          m_pSkillInfo->m_nEnergy = grammar::SemanticInt32($4);
          m_pSkillInfo->m_nSoul = grammar::SemanticInt32($6);
          const std::int64_t ultimateLevel = grammar::SemanticInt64($8);
          if (ultimateLevel < 0 || ultimateLevel > 3) {
              // Current passes the raw integer semantic to the %s slot.
              // Log.Add(
              //     LOG_ERROR,
              //     L"invalid ultimate skill level at [%d] (%s). Line = [%d]",
              //     m_lexer.line + 1,
              //     reinterpret_cast<const wchar_t*>(
              //         static_cast<std::uintptr_t>($8)),
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"invalid ultimate skill level at [%d] (%s). Line = [%d]",
              //     m_lexer.line + 1,
              //     reinterpret_cast<const wchar_t*>(
              //         static_cast<std::uintptr_t>($8)),
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: ultimate skill level %lld is outside 0..3; its low 32-bit value %d is still "
                  "stored.",
                  static_cast<long long>(m_lexer.line) + 1, static_cast<long long>(ultimateLevel),
                  grammar::SemanticInt32($8)
              );
          }
          m_pSkillInfo->m_UltimateSkillLv = grammar::SemanticInt32($8);
      }
    | BUFF_PROTECT_LEVEL EQUAL PREDEFINED
      { m_pSkillInfo->m_nBuffProtectLevel = grammar::SemanticInt32($3); }
    | ABNORMAL_DELETE_LEAVEWORLD EQUAL PREDEFINED
      { m_pSkillInfo->m_bDeleteBuff = grammar::SemanticInt64($3) != 0; }
    | TRANSFORM_TYPE EQUAL '{' n66 '}'
      {
          auto* const names = grammar::SemanticPointer<std::vector<char16_t*>>($4);
          std::vector<std::wstring> hostStorage;
          hostStorage.reserve(names->size());
          for (const char16_t* const name : *names) {
              hostStorage.push_back(Utf16ToWide(name));
          }
          std::vector<wchar_t*> hostNames;
          for (std::wstring& name : hostStorage) {
              hostNames.push_back(name.data());
          }
          std::vector<int> transformTypes;
          CTransform::ConvertTransformTypes(hostNames, transformTypes);
          m_pSkillInfo->SetTransformType(transformTypes);
          // Current deletes only the vector owner here. Its pointed lexer
          // strings are not freed by this reduction.
          delete names;
      }
    | BLOCK_ACTION_USE_SKILL EQUAL PREDEFINED
      {
          if (grammar::SemanticInt32($3) == 1) {
              m_pSkillInfo->m_bReuseDelayLock = true;
          }
      }
    | OLYMPIAD_USE EQUAL PREDEFINED
      { m_pSkillInfo->m_nOlympiadUse = grammar::SemanticInt32($3); }
    | REDUCESKILL EQUAL '{' PREDEFINED ';' predefined_def '}'
      {
          m_pSkillInfo->m_nReduceCount = grammar::SemanticInt32($4);
          m_pSkillInfo->m_dReduceDamage = grammar::SemanticDouble($6);
      }
    | DAMAGE_PROPORTION EQUAL NOT_APPLIED_DIRECT_INVERSE
      {
          m_pSkillInfo->m_DamageProportionType = static_cast<DamageProportionType::Enum>(grammar::SemanticInt32($3));
      }
    | ALTER_SKILL_LIST EQUAL '{' n20 '}'
    | MAGIC_CRITICAL_RATE EQUAL predefined_def
      { m_pSkillInfo->m_MagicCriticalRate = grammar::SemanticDouble($3); }
    | WITHOUT_ACTION EQUAL PREDEFINED
      { m_pSkillInfo->m_bWithoutAction = grammar::SemanticInt32($3) == 1; }
    | UNDELETABLE EQUAL PREDEFINED
      { m_pSkillInfo->m_bUndeletable = grammar::SemanticInt32($3) == 1; }
    | AWAKEN_PENALTY EQUAL '{' PREDEFINED ';' PREDEFINED '}'
      {
          m_pSkillInfo->m_bHasAwakenPenalty = true;
          m_pSkillInfo->m_nAwakenPenaltyLevelBegin = grammar::SemanticInt32($4);
          m_pSkillInfo->m_nAwakenPenaltyFactorOnMP = grammar::SemanticInt32($6);
      }
    | SPECIAL_LEVEL EQUAL PREDEFINED
      { m_pSkillInfo->m_nSpecialLevel = grammar::SemanticInt32($3); }
    | PLEDGEGAME_USE EQUAL PREDEFINED
      { m_pSkillInfo->m_nPledgeGameUse = grammar::SemanticInt32($3); }
    ;

n19
    : n19 ';' ALL
      { m_bAllBattleField = true; }
    | %empty
    ;

n20
    : n20 ';' '{' IDENTIFIER '}' ';' '{' n21 '}'
      {
          const char16_t* const name = SemanticWord($4);
          const AbnormalStatusSlotType abnormal = FindAbnormalByName(name, m_lexer.line + 1);
          if (abnormal == ABNS_MAX) {
              const std::string hostName = Utf16ToUtf8(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"invalid abnormal type at [%d] (%s). Line = [%d]",
              //     m_lexer.line + 1,
              //     hostName.c_str(),
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"invalid abnormal type at [%d] (%s). Line = [%d]",
              //     m_lexer.line + 1,
              //     hostName.c_str(),
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: alter-skill list uses unresolved abnormal slot '%s'; entries are retained "
                  "under slot value %d.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str(), static_cast<int>(abnormal)
              );
          }
          auto* const alterSkills = grammar::SemanticPointer<std::vector<AlterSkillInfo*>>($8);
          m_pSkillInfo->m_AlterSkillList[abnormal] = *alterSkills;
          m_AlterSkillVector.push_back(m_pSkillInfo);
      }
    | '{' IDENTIFIER '}' ';' '{' n21 '}'
      {
          const char16_t* const name = SemanticWord($2);
          const AbnormalStatusSlotType abnormal = FindAbnormalByName(name, m_lexer.line + 1);
          if (abnormal == ABNS_MAX) {
              const std::string hostName = Utf16ToUtf8(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"invalid abnormal type at [%d] (%s). Line = [%d]",
              //     m_lexer.line + 1,
              //     hostName.c_str(),
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"invalid abnormal type at [%d] (%s). Line = [%d]",
              //     m_lexer.line + 1,
              //     hostName.c_str(),
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: alter-skill list uses unresolved abnormal slot '%s'; entries are retained "
                  "under slot value %d.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str(), static_cast<int>(abnormal)
              );
          }
          auto* const alterSkills = grammar::SemanticPointer<std::vector<AlterSkillInfo*>>($6);
          m_pSkillInfo->m_AlterSkillList[abnormal] = *alterSkills;
      }
    ;

n21
    : n21 ';' '{' IDENTIFIER ';' PREDEFINED '}'
      {
          auto* const alterSkill = new AlterSkillInfo;
          alterSkill->skillName.assign(SemanticWord($4));
          alterSkill->prob = static_cast<double>(grammar::SemanticInt64($6));
          grammar::SemanticPointer<std::vector<AlterSkillInfo*>>($1)->push_back(alterSkill);
      }
    | '{' IDENTIFIER ';' PREDEFINED '}'
      {
          auto* const alterSkill = new AlterSkillInfo;
          alterSkill->skillName.assign(SemanticWord($2));
          alterSkill->prob = static_cast<double>(grammar::SemanticInt64($4));
          auto* const alterSkills = new std::vector<AlterSkillInfo*>;
          alterSkills->push_back(alterSkill);
          $$ = grammar::PointerSemantic(alterSkills);
      }
    ;

n22
    : n22 ';' '{' equip_weapon_def '}'
      { m_pSkillInfo->PushConditionMsg(-1, m_ConditionTarget); }
    | n22 ';' '{' '(' PREDEFINED ')' equip_weapon_def '}'
      { m_pSkillInfo->PushConditionMsg(grammar::SemanticInt32($5), m_ConditionTarget); }
    | '{' equip_weapon_def '}'
      { m_pSkillInfo->PushConditionMsg(-1, m_ConditionTarget); }
    | '{' '(' PREDEFINED ')' equip_weapon_def '}'
      { m_pSkillInfo->PushConditionMsg(grammar::SemanticInt32($3), m_ConditionTarget); }
    | %empty
    ;

n23
    : '{' n24 '}'
    | '{' '}'
    ;

n24
    : n25
    | n24 ';' n25
    ;

n25
    : '{' PREDEFINED ';' PREDEFINED '}'
      {
          m_pSkillInfo->m_mapAttachSkill.insert(
              std::make_pair(grammar::SemanticInt32($2), grammar::SemanticInt32($4))
          );
      }
    ;

equip_weapon_def
    : EQUIP_WEAPON ';' '{' identifier_def '}'
      {
          auto* const condition = new CSkillOperateCondition_equip_weapon;
          std::copy_n(m_AttackTypeArray, AT_MAX, condition->bWeaponTypeArray);
          m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
      }
    | EQUIP_ARMOR ';' '{' identifier_def_2 '}'
      {
          auto* const condition = new CSkillOperateCondition_equip_armor;
          std::copy_n(m_ArmorTypeArray, AMT_MAX, condition->bArmorTypeArray);
          m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
      }
    | TARGET_ITEM_CRYSTAL_TYPE ';' IDENTIFIER ';' '{' identifier_def_5 '}'
      {
          const auto crystalTypeAt = [this](const grammar::SemanticBits value) {
              const std::wstring name = Utf16ToWide(SemanticWord(value));
              CrystalType crystalType = CItem::GetCrysalType(name.c_str());
              if (crystalType == static_cast<CrystalType>(-1)) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"CrystalType at [%d] [%s] isn't defined. line = [%d]",
                  //     m_lexer.line + 1,
                  //     name.c_str(),
                  //     m_nLineNumber)
                  // L2_LOG_ERROR(Log,
                  //     L"CrystalType at [%d] [%s] isn't defined. line = [%d]",
                  //     m_lexer.line + 1,
                  //     name.c_str(),
                  //     m_nLineNumber)
                  L2_LOG_ERROR(
                      Log, "skilldata.txt line %lld: unknown crystal type '%s'; using crystal type 0.",
                      static_cast<long long>(m_lexer.line) + 1, WideToUtf8(name).c_str()
                  );
                  crystalType = static_cast<CrystalType>(0);
              }
              return crystalType;
          };
          const auto copyItemTypes = [](CSkillOperateCondition_enchant& condition, const grammar::SemanticBits value) {
              auto* const itemTypes = grammar::SemanticPointer<std::vector<ItemType>>(value);
              for (const ItemType itemType : *itemTypes) {
                  condition.m_vItemType.push_back(itemType);
              }
              delete itemTypes;
          };
          auto* const condition = new CSkillOperateCondition_enchant;
          condition->m_nItemEnchantTypeOp = 1;
          condition->m_CrystalType = crystalTypeAt($3);
          copyItemTypes(*condition, $6);
          m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
      }
    | TARGET_ITEM_CRYSTAL_DIVIDE_TYPE ';' IDENTIFIER ';' '{' identifier_def_5 '}'
      {
          const auto crystalTypeAt = [this](const grammar::SemanticBits value) {
              const std::wstring name = Utf16ToWide(SemanticWord(value));
              CrystalType crystalType = CItem::GetCrysalType(name.c_str());
              if (crystalType == static_cast<CrystalType>(-1)) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"CrystalType at [%d] [%s] isn't defined. line = [%d]",
                  //     m_lexer.line + 1,
                  //     name.c_str(),
                  //     m_nLineNumber)
                  // L2_LOG_ERROR(Log,
                  //     L"CrystalType at [%d] [%s] isn't defined. line = [%d]",
                  //     m_lexer.line + 1,
                  //     name.c_str(),
                  //     m_nLineNumber)
                  L2_LOG_ERROR(
                      Log, "skilldata.txt line %lld: unknown crystal type '%s'; using crystal type 0.",
                      static_cast<long long>(m_lexer.line) + 1, WideToUtf8(name).c_str()
                  );
                  crystalType = static_cast<CrystalType>(0);
              }
              return crystalType;
          };
          auto* const condition = new CSkillOperateCondition_enchant_divide;
          condition->m_nItemEnchantTypeOp = 1;
          condition->m_CrystalType = crystalTypeAt($3);
          auto* const itemTypes = grammar::SemanticPointer<std::vector<ItemType>>($6);
          for (const ItemType itemType : *itemTypes) {
              condition->m_vItemType.push_back(itemType);
          }
          delete itemTypes;
          m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
      }
    | TARGET_ITEM_CRYSTAL_TYPE ';' IDENTIFIER ';' '{' identifier_def_5 '}' ';' PREDEFINED
      {
          const auto crystalTypeAt = [this](const grammar::SemanticBits value) {
              const std::wstring name = Utf16ToWide(SemanticWord(value));
              CrystalType crystalType = CItem::GetCrysalType(name.c_str());
              if (crystalType == static_cast<CrystalType>(-1)) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"CrystalType at [%d] [%s] isn't defined. line = [%d]",
                  //     m_lexer.line + 1,
                  //     name.c_str(),
                  //     m_nLineNumber)
                  // L2_LOG_ERROR(Log,
                  //     L"CrystalType at [%d] [%s] isn't defined. line = [%d]",
                  //     m_lexer.line + 1,
                  //     name.c_str(),
                  //     m_nLineNumber)
                  L2_LOG_ERROR(
                      Log, "skilldata.txt line %lld: unknown crystal type '%s'; using crystal type 0.",
                      static_cast<long long>(m_lexer.line) + 1, WideToUtf8(name).c_str()
                  );
                  crystalType = static_cast<CrystalType>(0);
              }
              return crystalType;
          };
          const auto copyItemTypes = [](CSkillOperateCondition_enchant& condition, const grammar::SemanticBits value) {
              auto* const itemTypes = grammar::SemanticPointer<std::vector<ItemType>>(value);
              for (const ItemType itemType : *itemTypes) {
                  condition.m_vItemType.push_back(itemType);
              }
              delete itemTypes;
          };
          auto* const condition = new CSkillOperateCondition_enchant;
          condition->m_nItemEnchantTypeOp = grammar::SemanticInt32($9);
          condition->m_CrystalType = crystalTypeAt($3);
          copyItemTypes(*condition, $6);
          m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
      }
    | TARGET_RACE ';' RACE_VALUE
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          auto* const condition = new CSkillOperateCondition_race;
          condition->m_RaceType = static_cast<RaceType>(grammar::SemanticInt32($3));
          publish(condition);
      }
    | EQUIP_SHIELD
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_equip_shield);
      }
    | POSSESS_HOLYTHING
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_possess_holything);
      }
    | BUILD_CAMP
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_build_camp);
      }
    | BUILD_ADVANCE_BASE
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_build_advance_base);
      }
    | CAN_TRANSFORM_IN_DOMINION ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_can_transform_in_dominion(grammar::SemanticInt32($3)));
      }
    | REMAIN_HP_PER ';' OCT_VALUE ';' PREDEFINED ';' IDENTIFIER
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_remain_hp_per(
              static_cast<CSkillInfo::OperateConditionTarget>(grammar::SemanticInt32($3)),
              grammar::SemanticInt32($5), IsLower(SemanticWord($7), m_lexer.line + 1)
          ));
      }
    | REMAIN_MP_PER ';' PREDEFINED ';' IDENTIFIER
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_remain_mp_per(
              grammar::SemanticInt32($3), IsLower(SemanticWord($5), m_lexer.line + 1)
          ));
      }
    | REMAIN_CP_PER ';' PREDEFINED ';' IDENTIFIER
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_remain_cp_per(
              grammar::SemanticInt32($3), IsLower(SemanticWord($5), m_lexer.line + 1)
          ));
      }
    | TARGET_MY_PARTY
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_target_my_party(true));
      }
    | TARGET_MY_PARTY ';' IDENTIFIER
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          const auto includeMeAt = [this](const grammar::SemanticBits slot) {
              const char16_t* const value = SemanticWord(slot);
              if (TargetWordEquals(value, u"except_me")) {
                  return false;
              }
              if (!TargetWordEquals(value, u"include_me")) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"type should be except_me or include_me at [%d] Line = [%d]",
                  //     m_lexer.line + 1,
                  //     m_nLineNumber)
                  // L2_LOG_ERROR(Log,
                  //     L"type should be except_me or include_me at [%d] Line = [%d]",
                  //     m_lexer.line + 1,
                  //     m_nLineNumber)
                  L2_LOG_ERROR(
                      Log,
                      "skilldata.txt line %lld: unknown target inclusion '%s'; expected except_me or include_me. Using "
                      "include_me.",
                      static_cast<long long>(m_lexer.line) + 1, Utf16ToUtf8(value).c_str()
                  );
              }
              return true;
          };
          publish(new CSkillOperateCondition_target_my_party(includeMeAt($3)));
      }
    | TARGET_MY_PLEDGE ';' IDENTIFIER
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          const auto includeMeAt = [this](const grammar::SemanticBits slot) {
              const char16_t* const value = SemanticWord(slot);
              if (TargetWordEquals(value, u"except_me")) {
                  return false;
              }
              if (!TargetWordEquals(value, u"include_me")) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"type should be except_me or include_me at [%d] Line = [%d]",
                  //     m_lexer.line + 1,
                  //     m_nLineNumber)
                  // L2_LOG_ERROR(Log,
                  //     L"type should be except_me or include_me at [%d] Line = [%d]",
                  //     m_lexer.line + 1,
                  //     m_nLineNumber)
                  L2_LOG_ERROR(
                      Log,
                      "skilldata.txt line %lld: unknown target inclusion '%s'; expected except_me or include_me. Using "
                      "include_me.",
                      static_cast<long long>(m_lexer.line) + 1, Utf16ToUtf8(value).c_str()
                  );
              }
              return true;
          };
          publish(new CSkillOperateCondition_target_my_pledge(includeMeAt($3)));
      }
    | TARGET_MY_MENTOR
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_target_my_mentor);
      }
    | TARGET_MY_MENTEE
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_target_my_mentee);
      }
    | OP_TARGET_MY_PLEDGE_ACADEMY
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_target_my_pledge_academy);
      }
    | OP_IN_SIEGE_TIME ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_in_siege_time(grammar::SemanticInt32($3)));
      }
    | CONSUME_BODY
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_consume_body);
      }
    | ENERGY_SAVED ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_energy_saved(grammar::SemanticInt32($3)));
      }
    | SOUL_SAVED ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_soul_saved(grammar::SemanticInt32($3)));
      }
    | CAN_SUMMON_PET
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_can_summon_pet);
      }
    | CAN_SUMMON_CUBIC
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_can_summon_cubic);
      }
    | CAN_SUMMON
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_can_summon);
      }
    | CAN_SUMMON_MULTI ';' NAME
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_can_summon_multi(SemanticWord($3)));
      }
    | CAN_SUMMON_SIEGE_GOLEM
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_can_summon_siege_golem);
          m_pSkillInfo->m_IsSummonSiegeSkill = true;
      }
    | OP_CHECK_ABNORMAL ';' IDENTIFIER ';' PREDEFINED ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_check_abnormal(
              FindAbnormalByName(SemanticWord($3), m_lexer.line + 1), grammar::SemanticInt32($5),
              grammar::SemanticInt32($7)
          ));
      }
    | OP_CHECK_CAST_RANGE ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_check_cast_range(static_cast<double>(grammar::SemanticInt64($3))));
      }
    | OP_CHECK_PCBANG_POINT ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_check_pcbang_point(grammar::SemanticInt32($3)));
      }
    | OP_SWEEPER
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_sweeper);
      }
    | OP_HARVEST
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_harvest);
      }
    | CHECK_SEX ';' IDENTIFIER
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          const char16_t* const value = SemanticWord($3);
          SexType sex = ST_MALE;
          if (TargetWordEquals(value, u"F")) {
              sex = ST_FEMALE;
          } else if (!TargetWordEquals(value, u"M")) {
              const std::string hostValue = Utf16ToUtf8(value);
              // Log.Add(
              //     LOG_ERROR,
              //     L"invalid sex type at [%d] (%s). Line=[%d]",
              //     m_lexer.line + 1,
              //     hostValue.c_str(),
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"invalid sex type at [%d] (%s). Line=[%d]",
              //     m_lexer.line + 1,
              //     hostValue.c_str(),
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log, "skilldata.txt line %lld: unknown sex '%s'; expected M or F. Using M for the condition.",
                  static_cast<long long>(m_lexer.line) + 1, hostValue.c_str()
              );
          }
          auto* const condition = new CSkillOperateCondition_check_sex;
          condition->m_nSex = sex;
          publish(condition);
      }
    | CHECK_LEVEL ';' PREDEFINED ';' PREDEFINED ';' SELF_GROUP
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_check_level(
              grammar::SemanticInt32($3), grammar::SemanticInt32($5),
              static_cast<SkillTargetTypeEnum>(grammar::SemanticInt32($7))
          ));
      }
    | OP_ENERGY_MAX ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_energy_max(grammar::SemanticInt32($3)));
      }
    | OP_SOUL_MAX
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_soul_max);
      }
    | OP_HOME ';' IDENTIFIER
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          const char16_t* const value = SemanticWord($3);
          RestartPoint restartPoint = static_cast<RestartPoint>(0);
          if (TargetWordEquals(value, u"agit")) {
              restartPoint = static_cast<RestartPoint>(1);
          } else if (TargetWordEquals(value, u"castle")) {
              restartPoint = static_cast<RestartPoint>(2);
          } else if (TargetWordEquals(value, u"fortress")) {
              restartPoint = static_cast<RestartPoint>(3);
          }
          publish(new CSkillOperateCondition_op_home(restartPoint));
      }
    | OP_WYVERN
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_wyvern);
      }
    | OP_2H_WEAPON ';' '{' identifier_def '}'
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          auto* const condition = new CSkillOperateCondition_op_2h_weapon;
          std::copy_n(m_AttackTypeArray, AT_MAX, condition->bWeaponTypeArray);
          publish(condition);
      }
    | OP_SIEGE_HAMMER
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_siege_hammer);
      }
    | OP_UNLOCK
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_unlock);
      }
    | OP_COMPANION ';' IDENTIFIER
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          const char16_t* const value = SemanticWord($3);
          int companionType = 0;
          if (TargetWordEquals(value, u"op_pet")) {
              companionType = 1;
          } else if (TargetWordEquals(value, u"op_summon")) {
              companionType = 2;
          } else if (TargetWordEquals(value, u"op_my_summon")) {
              companionType = 3;
          } else if (TargetWordEquals(value, u"op_pc")) {
              companionType = 4;
          } else if (!TargetWordEquals(value, u"op_all")) {
              const std::string hostValue = Utf16ToUtf8(value);
              // Log.Add(
              //     LOG_ERROR,
              //     L"invalid companion type at [%d] (%s). Line=[%d]",
              //     m_lexer.line + 1,
              //     hostValue.c_str(),
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"invalid companion type at [%d] (%s). Line=[%d]",
              //     m_lexer.line + 1,
              //     hostValue.c_str(),
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log, "skilldata.txt line %lld: unknown companion type '%s'; using op_all for the condition.",
                  static_cast<long long>(m_lexer.line) + 1, hostValue.c_str()
              );
          }
          auto* const condition = new CSkillOperateCondition_op_companion;
          condition->type = companionType;
          publish(condition);
      }
    | OP_RESURRECTION
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_resurrection);
      }
    | OP_USE_FIRECRACKER ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_use_firecracker(grammar::SemanticInt32($3)));
      }
    | OP_FISHING_CAST
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_fishing_cast);
      }
    | OP_FISHING_PUMPING
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_fishing_pumping);
      }
    | OP_FISHING_REELING
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_fishing_reeling);
      }
    | OP_TERRITORY ';' n30 ';' IDENTIFIER
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          const auto operationTargetAt = [](const grammar::SemanticBits slot) {
              const char16_t* const value = SemanticWord(slot);
              if (TargetWordEquals(value, u"t_self")) {
                  return SOTT_SELF;
              }
              if (TargetWordEquals(value, u"t_target")) {
                  return SOTT_TARGET;
              }
              if (TargetWordEquals(value, u"t_all")) {
                  return SOTT_ALL;
              }
              return SOTT_UNDEFINDED;
          };
          publish(new CSkillOperateCondition_op_territory(m_pTerritory, operationTargetAt($5)));
      }
    | OP_NOT_TERRITORY ';' n30 ';' IDENTIFIER
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          const auto operationTargetAt = [](const grammar::SemanticBits slot) {
              const char16_t* const value = SemanticWord(slot);
              if (TargetWordEquals(value, u"t_self")) {
                  return SOTT_SELF;
              }
              if (TargetWordEquals(value, u"t_target")) {
                  return SOTT_TARGET;
              }
              if (TargetWordEquals(value, u"t_all")) {
                  return SOTT_ALL;
              }
              return SOTT_UNDEFINDED;
          };
          publish(new CSkillOperateCondition_op_not_territory(m_pTerritory, operationTargetAt($5)));
      }
    | OP_NOT_TERRITORY_LIST ';' '{' NAME '}'
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          const char16_t* const listName = SemanticWord($4);
          const std::string hostListName = Utf16ToUtf8(listName);
          const int listId = static_cast<int>(g_PrecompiledHeaderDB.Search(listName));
          if (listId == -1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Invalid PredefinedList : Name[%s] at [%d] Line = [%d]",
              //     hostListName.c_str(),
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"Invalid PredefinedList : Name[%s] at [%d] Line = [%d]",
              //     hostListName.c_str(),
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: predefined territory reference '%s' resolves to -1; lookup still proceeds "
                  "with that ID.",
                  static_cast<long long>(m_lexer.line) + 1, hostListName.c_str()
              );
          }
          std::vector<TerritoryElement*>* const elements = PredefinedListDB::inst().GetPredefinedTerritoryList(listId);
          if (elements == nullptr) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"TerritoryElementList* is nullptr : Name[%s] at [%d] Line = [%d]",
              //     hostListName.c_str(),
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"TerritoryElementList* is nullptr : Name[%s] at [%d] Line = [%d]",
              //     hostListName.c_str(),
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: predefined territory list '%s' (ID %d) is unavailable; parsing still attempts "
                  "to traverse it.",
                  static_cast<long long>(m_lexer.line) + 1, hostListName.c_str(), listId
              );
          }
          // The current generated action logs null carriers but then keeps
          // dereferencing them. Preserve that failure behavior instead of
          // inventing a recovery branch.
          for (TerritoryElement* const element : *elements) {
              if (element == nullptr) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"TerritoryElement* is nullptr : Name[%s] at [%d] Line = [%d]",
                  //     hostListName.c_str(),
                  //     m_lexer.line + 1,
                  //     m_nLineNumber)
                  // L2_LOG_ERROR(Log,
                  //     L"TerritoryElement* is nullptr : Name[%s] at [%d] Line = [%d]",
                  //     hostListName.c_str(),
                  //     m_lexer.line + 1,
                  //     m_nLineNumber)
                  L2_LOG_ERROR(
                      Log,
                      "skilldata.txt line %lld: predefined territory list '%s' contains a null element; parsing has no "
                      "recovery for this entry.",
                      static_cast<long long>(m_lexer.line) + 1, hostListName.c_str()
                  );
              }
              const SkillOperationTargetType targetType = element->GetSkillOPTargetType();
              CTerritory* const territory = element->GetTerritory();
              if (territory == nullptr) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"CTerritory* is nullptr : Name[%s] at [%d] Line = [%d]",
                  //     hostListName.c_str(),
                  //     m_lexer.line + 1,
                  //     m_nLineNumber)
                  // L2_LOG_ERROR(Log,
                  //     L"CTerritory* is nullptr : Name[%s] at [%d] Line = [%d]",
                  //     hostListName.c_str(),
                  //     m_lexer.line + 1,
                  //     m_nLineNumber)
                  L2_LOG_ERROR(
                      Log,
                      "skilldata.txt line %lld: predefined territory list '%s' contains an unresolved territory; a null "
                      "territory is passed to op_not_territory.",
                      static_cast<long long>(m_lexer.line) + 1, hostListName.c_str()
                  );
              }
              publish(new CSkillOperateCondition_op_not_territory(territory, targetType));
          }
      }
    | OP_ALIGNMENT ';' IDENTIFIER ';' SELF_GROUP
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          const char16_t* const value = SemanticWord($3);
          int isChaotic = -1;
          if (TargetWordEquals(value, u"chaotic")) {
              isChaotic = 1;
          } else if (TargetWordEquals(value, u"lawful")) {
              isChaotic = 0;
          }
          publish(new CSkillOperateCondition_op_alignment(
              isChaotic, static_cast<SkillTargetTypeEnum>(grammar::SemanticInt32($5))
          ));
      }
    | OP_PKCOUNT ';' SELF_GROUP
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_pkcount(static_cast<SkillTargetTypeEnum>(grammar::SemanticInt32($3))));
      }
    | OP_ENCUMBERED ';' PREDEFINED ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_encumbered(grammar::SemanticInt32($3), grammar::SemanticInt32($5)));
      }
    | OP_CALL_PC
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_call_pc(false, m_pSkillInfo));
      }
    | OP_CALL_PC ';' IDENTIFIER
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_call_pc(TargetWordEquals(SemanticWord($3), u"include_dead"), m_pSkillInfo));
      }
    | OP_CHECK_CLASS ';' PREDEFINED ';' SELF_GROUP ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_check_class(
              static_cast<ClassType>(grammar::SemanticInt32($3)),
              static_cast<SkillTargetTypeEnum>(grammar::SemanticInt32($5)), $7 == 1
          ));
      }
    | OP_CHECK_CLASS_LIST ';' '{' n63 '}' ';' SELF_GROUP ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          auto* const classIds = grammar::SemanticPointer<std::vector<std::int64_t>>($4);
          publish(new CSkillOperateCondition_op_check_class_list(
              classIds, static_cast<SkillTargetTypeEnum>(grammar::SemanticInt32($7)), $9 == 1
          ));
          delete classIds;
      }
    | OP_CHECK_SKILL ';' PREDEFINED ';' SELF_GROUP
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_check_skill(
              grammar::SemanticInt32($3), static_cast<SkillTargetTypeEnum>(grammar::SemanticInt32($5))
          ));
      }
    | OP_EXIST_NPC ';' '{' n66 '}' ';' PREDEFINED ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          auto* const names = grammar::SemanticPointer<std::vector<char16_t*>>($4);
          auto* const condition = new CSkillOperateCondition_op_exist_npc(grammar::SemanticInt32($7), $9 == 1);
          for (const char16_t* const name : *names) {
              const int npcId = static_cast<int>(g_PrecompiledHeaderDB.Search(name));
              if (npcId < 0) {
                  const std::string hostName = Utf16ToUtf8(name);
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"[%s] NPC name is invalid at [%d] Line = [%d]",
                  //     hostName.c_str(),
                  //     m_lexer.line + 1,
                  //     m_nLineNumber)
                  // L2_LOG_ERROR(Log,
                  //     L"[%s] NPC name is invalid at [%d] Line = [%d]",
                  //     hostName.c_str(),
                  //     m_lexer.line + 1,
                  //     m_nLineNumber)
                  L2_LOG_ERROR(
                      Log,
                      "skilldata.txt line %lld: op_exist_npc reference '%s' resolves to negative NPC ID %d; this entry is "
                      "omitted.",
                      static_cast<long long>(m_lexer.line) + 1, hostName.c_str(), npcId
                  );
              } else {
                  condition->m_vTargetNPC.insert(npcId);
              }
          }
          publish(condition);
          // The current action destroys only the pointer vector. Its lexer
          // string pointees remain owned by the generated semantic runtime.
          delete names;
      }
    | OP_NEED_SUMMON ';' '{' n66 '}'
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          auto* const names = grammar::SemanticPointer<std::vector<char16_t*>>($4);
          auto* const condition = new CSkillOperateCondition_op_need_summon;
          for (const char16_t* const name : *names) {
              const int npcId = static_cast<int>(g_PrecompiledHeaderDB.Search(name));
              if (npcId < 0) {
                  const std::string hostName = Utf16ToUtf8(name);
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"[%s] NPC name is invalid at [%d] Line = [%d]",
                  //     hostName.c_str(),
                  //     m_lexer.line + 1,
                  //     m_nLineNumber)
                  // L2_LOG_ERROR(Log,
                  //     L"[%s] NPC name is invalid at [%d] Line = [%d]",
                  //     hostName.c_str(),
                  //     m_lexer.line + 1,
                  //     m_nLineNumber)
                  L2_LOG_ERROR(
                      Log,
                      "skilldata.txt line %lld: op_need_summon reference '%s' resolves to negative NPC ID %d; this entry "
                      "is omitted.",
                      static_cast<long long>(m_lexer.line) + 1, hostName.c_str(), npcId
                  );
              } else {
                  condition->m_SummonClassIds.insert(npcId);
              }
          }
          publish(condition);
          delete names;
      }
    | OP_TARGET_NPC ';' '{' n66 '}'
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          auto* const names = grammar::SemanticPointer<std::vector<char16_t*>>($4);
          auto* const condition = new CSkillOperateCondition_op_target_npc;
          for (const char16_t* const name : *names) {
              const int npcId = static_cast<int>(g_PrecompiledHeaderDB.Search(name));
              if (npcId < 0) {
                  const std::string hostName = Utf16ToUtf8(name);
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"[%s] NPC name is invalid",
                  //     hostName.c_str())
                  // L2_LOG_ERROR(Log,
                  //     L"[%s] NPC name is invalid",
                  //     hostName.c_str())
                  L2_LOG_ERROR(
                      Log,
                      "skilldata.txt line %lld: op_target_npc reference '%s' resolves to negative NPC ID %d; this entry "
                      "is omitted.",
                      static_cast<long long>(m_lexer.line) + 1, hostName.c_str(), npcId
                  );
              } else {
                  condition->m_vTargetNPC.push_back(npcId);
              }
          }
          publish(condition);
          delete names;
      }
    | OP_CAN_ESCAPE
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_can_escape);
      }
    | OP_ENCHANT_RANGE ';' PREDEFINED ';' PREDEFINED ';' IDENTIFIER
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          const char16_t* const value = SemanticWord($7);
          int type = -1;
          if (TargetWordEquals(value, u"none")) {
              type = 0;
          } else if (TargetWordEquals(value, u"normal")) {
              type = 1;
          } else if (TargetWordEquals(value, u"magic")) {
              type = 2;
          } else if (TargetWordEquals(value, u"onepiece")) {
              type = 3;
          } else if (TargetWordEquals(value, u"agathion")) {
              type = 4;
          }
          publish(
              new CSkillOperateCondition_enchant_range(grammar::SemanticInt32($3), grammar::SemanticInt32($5), type)
          );
      }
    | OP_USE_PRASEED ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_use_praseed(grammar::SemanticInt32($3)));
      }
    | OP_RESTART_POINT ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_restart_point(grammar::SemanticInt32($3)));
      }
    | OP_AGATHION_ENERGY ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_agathion_energy(grammar::SemanticInt32($3)));
      }
    | CAN_USE_SWOOP_CANNON
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_can_use_swoop_cannon);
      }
    | CAN_USE_IN_BATTLEFIELD
      { m_bAllBattleField = false; }
      n19
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_can_use_in_battlefield(m_bAllBattleField));
      }
    | CAN_ENCHANT_ATTRIBUTE
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_can_enchant_attribute);
      }
    | OP_CHECK_FLAG
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_check_flag);
      }
    | OP_CAN_DISARM
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_can_disarm);
      }
    | OP_NOT_CURSED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_not_cursed);
      }
    | OP_CHECK_RESIDENCE ';' '{' n63 '}' ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          if ($7 != 0 && $7 != 1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Invalid transformID in can_use_in_transform[line: %d]. It must be over 0",
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"Invalid transformID in can_use_in_transform[line: %d]. It must be over 0",
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: op_check_residence flag has raw value %llu; expected 0 or 1. Its nonzero "
                  "value is treated as true.",
                  static_cast<long long>(m_lexer.line) + 1, static_cast<unsigned long long>($7)
              );
          }
          auto* const residenceIds = grammar::SemanticPointer<std::vector<std::int64_t>>($4);
          publish(new CSkillOperateCondition_op_check_residence(residenceIds, $7 != 0));
          delete residenceIds;
      }
    | OP_RETURN_POS ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillEffect_op_return_pos(grammar::SemanticInt32($3)));
      }
    | OP_BLINK ';' IDENTIFIER ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          SkillDirectionType direction = DT_FRONT;
          if (TargetWordEquals(SemanticWord($3), u"_front")) {
              direction = DT_FRONT;
          } else if (TargetWordEquals(SemanticWord($3), u"_back")) {
              direction = DT_BACK;
          } else {
              break;
          }
          const double distance = static_cast<double>(grammar::SemanticInt64($5));
          publish(new CSkillEffect_op_blink(distance, direction));
      }
    | OP_CHARGE_SELF ';' IDENTIFIER ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          SkillDirectionType direction = DT_FRONT;
          if (TargetWordEquals(SemanticWord($3), u"_front")) {
              direction = DT_FRONT;
          } else if (TargetWordEquals(SemanticWord($3), u"_back")) {
              direction = DT_BACK;
          } else {
              break;
          }
          const double distance = static_cast<double>(grammar::SemanticInt64($5));
          publish(new CSkillEffect_op_charge_self(distance, direction));
      }
    | OP_CAN_GO ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_can_go(static_cast<double>(grammar::SemanticInt64($3))));
      }
    | OP_TARGET_PC
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillEffect_op_target_pc);
      }
    | OP_COMBAT ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillEffect_op_combat(grammar::SemanticInt32($3) == 1));
      }
    | OP_SKILL ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillEffect_op_skill(
              grammar::SemanticInt32($3), grammar::SemanticInt32($5), grammar::SemanticInt32($7) == 1
          ));
      }
    | OP_SKILL_ACQUIRE ';' PREDEFINED ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillEffect_op_skill_acquire(grammar::SemanticInt32($3), grammar::SemanticInt32($5)));
      }
    | OP_CHECK_CRT_EFFECT
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_check_crt_effect);
      }
    | OP_NEED_AGATHION
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_need_agathion);
      }
    | CAN_TRANSFORM ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_can_transform(grammar::SemanticInt32($3)));
      }
    | CAN_UNTRANSFORM
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_can_untransform);
      }
    | CAN_USE_IN_TRANSFORM
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_can_use_in_transform);
      }
    | CAN_USE_IN_TRANSFORM ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          const int transformId = grammar::SemanticInt32($3);
          if (transformId < 1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Invalid transformID in can_use_in_transform[line: %d]. It must be over 0",
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"Invalid transformID in can_use_in_transform[line: %d]. It must be over 0",
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: can_use_in_transform has nonpositive transform ID %d; the condition still "
                  "receives this ID.",
                  static_cast<long long>(m_lexer.line) + 1, transformId
              );
          }
          publish(new CSkillOperateCondition_can_use_in_transform(transformId));
      }
    | CANNOT_USE_IN_TRANSFORM
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_cannot_use_in_transform);
      }
    | CANNOT_USE_IN_TRANSFORM ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          const int transformId = grammar::SemanticInt32($3);
          if (transformId < 1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Invalid transformID in cannot_use_in_transform[line: %d]. It must be over 0",
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"Invalid transformID in cannot_use_in_transform[line: %d]. It must be over 0",
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: cannot_use_in_transform has nonpositive transform ID %d; the condition still "
                  "receives this ID.",
                  static_cast<long long>(m_lexer.line) + 1, transformId
              );
          }
          publish(new CSkillOperateCondition_cannot_use_in_transform(transformId));
      }
    | CAN_MOUNT_FOR_EVENT ';' NAME
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_can_mount_for_event(SemanticWord($3)));
      }
    | NOT_IN_UNDERWATER
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_not_in_underwater);
      }
    | OP_CHANGE_WEAPON
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_change_weapon);
      }
    | CAN_BOOKMARK_ADD_SLOT ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_can_bookmark_add_slot(grammar::SemanticInt32($3)));
      }
    | CAN_ADD_MAX_ENTRANCE_INZONE ';' PREDEFINED ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_can_add_max_entrance_inzone(
              grammar::SemanticInt32($3), grammar::SemanticInt32($5)
          ));
      }
    | OP_COLLECTING
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_collecting);
      }
    | OP_HAVE_SUMMON
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_have_summon);
      }
    | OP_HAVE_SUMMONED_NPC ';' NAME
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_have_summoned_npc(std::u16string(SemanticWord($3))));
      }
    | OP_HAVE_NOT_SUMMONED_NPC ';' NAME
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_have_not_summoned_npc(std::u16string(SemanticWord($3))));
      }
    | CAN_REFUEL_AIRSHIP ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_can_refuel_airship(grammar::SemanticInt32($3)));
      }
    | OP_EQUIP_ITEM ';' OCT_VALUE ';' NAME
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          const char16_t* const itemName = SemanticWord($5);
          const int itemClassId = static_cast<int>(g_PrecompiledHeaderDB.Search(itemName));
          if (itemClassId == -1) {
              const std::string hostName = Utf16ToUtf8(itemName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Invalid Item[%s] at [%d] Line = [%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"Invalid Item[%s] at [%d] Line = [%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: op_equip_item reference '%s' resolves to -1; the condition still receives "
                  "item ID -1.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
              );
          }
          publish(new CSkillOperateCondition_op_equip_item(
              static_cast<CSkillInfo::OperateConditionTarget>(grammar::SemanticInt32($3)), itemClassId
          ));
      }
    | OP_CAN_NOT_USE_AIRSHIP
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_can_not_use_airship);
      }
    | CAN_RESTORE_VITAL_POINT
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_can_restore_vital_point);
      }
    | CAN_STOP_CONSUME_VITAL_POINT
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_can_stop_consume_vital_point);
      }
    | CAN_USE_VITALITY_CONSUME_ITEM
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_can_use_vitality_consume_item);
      }
    | CAN_CHANGE_VITAL_ITEM_COUNT
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_can_change_vital_item_count);
      }
    | OP_CHECK_STUCK_NPC
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_check_stuck_npc);
      }
    | CAN_USE_IN_DRAGON_LAIR
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_can_use_in_dragon_lair);
      }
    | OP_NEED_SUMMON_OR_PET ';' '{' n66 '}'
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          auto* const names = grammar::SemanticPointer<std::vector<char16_t*>>($4);
          auto* const condition = new CSkillOperateCondition_op_need_summon_or_pet;
          for (const char16_t* const name : *names) {
              const int npcId = static_cast<int>(g_PrecompiledHeaderDB.Search(name));
              if (npcId < 0) {
                  const std::string hostName = Utf16ToUtf8(name);
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"[%s] NPC name is invalid at [%d] Line = [%d]",
                  //     hostName.c_str(),
                  //     m_lexer.line + 1,
                  //     m_nLineNumber)
                  // L2_LOG_ERROR(Log,
                  //     L"[%s] NPC name is invalid at [%d] Line = [%d]",
                  //     hostName.c_str(),
                  //     m_lexer.line + 1,
                  //     m_nLineNumber)
                  L2_LOG_ERROR(
                      Log,
                      "skilldata.txt line %lld: op_need_summon_or_pet reference '%s' resolves to negative NPC ID %d; this "
                      "entry is omitted.",
                      static_cast<long long>(m_lexer.line) + 1, hostName.c_str(), npcId
                  );
              } else {
                  condition->m_SummonOrPetClassIds.insert(npcId);
              }
          }
          publish(condition);
          // The generated action keeps the semantic pointer-vector in both
          // the only-PCH and normal paths.
      }
    | OP_PEACEZONE
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_need_peacezone);
      }
    | OP_MAINJOB
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_mainjob);
      }
    | OP_SUBJOB
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_subjob);
      }
    | OP_TARGET_WEAPON_ATTACK_TYPE ';' '{' '{' identifier_def '}' ';' '{' identifier_def_4 '}' '}'
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          auto* const condition = new CSkillOperateCondition_op_target_weapon_attack_type;
          std::copy_n(m_AttackTypeArray, AT_MAX, condition->bWeaponTypeArray);
          std::copy_n(m_SlotTypeArray, ST_MAX, condition->bWeaponSlotTypeArray);
          publish(condition);
      }
    | OP_TARGET_WEAPON_ATTACK_TYPE ';' '{' '{' identifier_def '}' ';' '{' identifier_def_4 '}' ';' PREDEFINED '}'
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          auto* const condition = new CSkillOperateCondition_op_target_weapon_attack_type;
          std::copy_n(m_AttackTypeArray, AT_MAX, condition->bWeaponTypeArray);
          std::copy_n(m_SlotTypeArray, ST_MAX, condition->bWeaponSlotTypeArray);
          condition->nMagicWeaponCheckType = grammar::SemanticInt32($12);
          publish(condition);
      }
    | OP_TARGET_ARMOR_TYPE ';' '{' '{' identifier_def_2 '}' ';' '{' identifier_def_4 '}' '}'
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          auto* const condition = new CSkillOperateCondition_op_target_armor_type;
          std::copy_n(m_ArmorTypeArray, AMT_MAX, condition->bArmorTypeArray);
          std::copy_n(m_SlotTypeArray, ST_MAX, condition->bArmorSlotTypeArray);
          publish(condition);
      }
    | OP_TARGET_ALL_ITEM_TYPE ';' '{' '{' identifier_def '}' ';' '{' identifier_def_2 '}' ';' '{' identifier_def_4 '}' '}'
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          auto* const condition = new CSkillOperateCondition_op_target_all_item_type;
          std::copy_n(m_AttackTypeArray, AT_MAX, condition->bWeaponTypeArray);
          std::copy_n(m_ArmorTypeArray, AMT_MAX, condition->bArmorTypeArray);
          std::copy_n(m_SlotTypeArray, ST_MAX, condition->bSlotTypeArray);
          publish(condition);
      }
    | OP_PLEDGE ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_pledge(grammar::SemanticInt32($3)));
      }
    | OP_SOCIAL_CLASS ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_social_class(grammar::SemanticInt32($3)));
      }
    | OP_INSTANTZONE ';' '{' n63 '}'
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(
              new CSkillOperateCondition_op_instantzone(grammar::SemanticPointer<std::vector<std::int64_t>>($4))
          );
      }
    | OP_INSTANTZONE
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_instantzone);
      }
    | OP_NOT_INSTANTZONE
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_not_instantzone);
      }
    | OP_NOT_OLYMPIAD
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_not_olympiad);
      }
    | OP_CHECK_ACCOUNT_TYPE ';' IDENTIFIER
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          if (TargetWordEquals(SemanticWord($3), u"pccafe")) {
              publish(new CSkillOperateCondition_account_type(CSkillOperateCondition_account_type::UAT_PCCAFE));
          }
      }
    | OP_CANNOT_USE_TARGET_WITH_PRIVATE_STORE
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_cannot_use_target_with_private_store);
      }
    | OP_CHECK_ON_GOING_EVENT_CAMPAIGN ';' '{' n63 '}'
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          auto* const campaignIds = grammar::SemanticPointer<std::vector<std::int64_t>>($4);
          publish(new CSkillOperateCondition_op_check_on_going_event_campaign(campaignIds));
          delete campaignIds;
      }
    | OP_CHECK_EQUIP_SLOT ';' '{' identifier_def_4 '}' ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          auto* const condition = new CSkillOperateCondition_op_check_equip_slot(grammar::SemanticInt32($7));
          publish(condition);
          std::copy_n(m_SlotTypeArray, ST_MAX, condition->m_bEquipSlotTypeArray);
      }
    | OP_CHECK_NOBLESS
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_check_nobless);
      }
    | T_101CA
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_check_honors);
      }
    | OP_CHECK_RELATIVE_HEIGHT ';' PREDEFINED
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_check_relative_height(grammar::SemanticInt32($3)));
      }
    | OP_CHECK_NOT_REGISTERED_PLEDGEGAME
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_check_not_registered_pledgegame);
      }
    | OP_EQUIP_AGATHION_MAIN_SLOT
      {
          const auto publish = [this](CSkillOperateCondition* condition) {
              m_pSkillInfo->PushCondition(condition, m_ConditionTarget);
          };
          publish(new CSkillOperateCondition_op_equip_agathion_main_slot);
      }
    ;

n30
    : '{'
      { m_pTerritory = new CTerritory; }
      n32 '}'
    | '{' '}'
    ;

n32
    : n33
    | n32 ';' n33
    ;

n33
    : '{' PREDEFINED ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED '}'
      {
          if (grammar::SemanticInt64($6) < m_pTerritory->GetBottom()) {
              m_pTerritory->SetBottom(grammar::SemanticInt32($6));
          }
          if (m_pTerritory->GetTop() < grammar::SemanticInt64($8)) {
              m_pTerritory->SetTop(grammar::SemanticInt32($8));
          }
          FVector point(
              static_cast<double>(grammar::SemanticInt32($2)), static_cast<double>(grammar::SemanticInt32($4)), 0.0
          );
          m_pTerritory->AddTerritoryPoint(&point);
      }
    | '{' ';' ';' ';' '}'
    ;

identifier_def
    : IDENTIFIER
    | ALL
      { std::fill_n(m_AttackTypeArray, AT_MAX, true); }
    | SWORD_GROUP
      {
          std::fill_n(m_AttackTypeArray, AT_MAX, false);
          m_AttackTypeArray[$1] = true;
      }
    | identifier_def ';' SWORD_GROUP
      { m_AttackTypeArray[$3] = true; }
    ;

identifier_def_2
    : IDENTIFIER
      {
          if (TargetWordEquals(SemanticWord($1), u"none")) {
              std::fill_n(m_ArmorTypeArray, AMT_MAX, false);
          }
      }
    | ALL
      { std::fill_n(m_ArmorTypeArray, AMT_MAX, true); }
    | ARMOR_VALUE
      {
          std::fill_n(m_ArmorTypeArray, AMT_MAX, false);
          m_ArmorTypeArray[$1] = true;
      }
    | identifier_def_2 ';' ARMOR_VALUE
      { m_ArmorTypeArray[$3] = true; }
    ;

identifier_def_3
    : IDENTIFIER
      {
          if (TargetWordEquals(SemanticWord($1), u"none")) {
              std::fill_n(m_AdditionalArmorTypeArray, AAMT_MAX, false);
          }
      }
    | ALL
      { std::fill_n(m_AdditionalArmorTypeArray, AAMT_MAX, true); }
    | ARMOR_VALUE
      {
          std::fill_n(m_AdditionalArmorTypeArray, AAMT_MAX, false);
          m_AdditionalArmorTypeArray[$1] = true;
      }
    | identifier_def_3 ';' ARMOR_VALUE
      { m_AdditionalArmorTypeArray[$3] = true; }
    ;

identifier_def_4
    : IDENTIFIER
      {
          if (TargetWordEquals(SemanticWord($1), u"none")) {
              std::fill_n(m_SlotTypeArray, ST_MAX, false);
          }
      }
    | ALL
      { std::fill_n(m_SlotTypeArray, ST_MAX, true); }
    | SLOT_VALUE
      {
          std::fill_n(m_SlotTypeArray, ST_MAX, false);
          m_SlotTypeArray[$1] = true;
      }
    | identifier_def_4 ';' SLOT_VALUE
      { m_SlotTypeArray[$3] = true; }
    ;

identifier_def_5
    : IDENTIFIER
      {
          const auto itemTypeAt = [](const grammar::SemanticBits value) {
              const char16_t* const itemType = SemanticWord(value);
              if (TargetWordEquals(itemType, u"weapon")) {
                  return IT_WEAPON;
              }
              if (TargetWordEquals(itemType, u"armor")) {
                  return IT_ARMOR;
              }
              if (TargetWordEquals(itemType, u"accessary")) {
                  return IT_ACCESSARY;
              }
              if (TargetWordEquals(itemType, u"questitem")) {
                  return IT_QUESTITEM;
              }
              if (TargetWordEquals(itemType, u"asset")) {
                  return IT_ASSET;
              }
              return IT_ETCITEM;
          };
          auto* const itemTypes = new std::vector<ItemType>;
          itemTypes->push_back(itemTypeAt($1));
          $$ = grammar::PointerSemantic(itemTypes);
      }
    | identifier_def_5 ';' IDENTIFIER
      {
          const auto itemTypeAt = [](const grammar::SemanticBits value) {
              const char16_t* const itemType = SemanticWord(value);
              if (TargetWordEquals(itemType, u"weapon")) {
                  return IT_WEAPON;
              }
              if (TargetWordEquals(itemType, u"armor")) {
                  return IT_ARMOR;
              }
              if (TargetWordEquals(itemType, u"accessary")) {
                  return IT_ACCESSARY;
              }
              if (TargetWordEquals(itemType, u"questitem")) {
                  return IT_QUESTITEM;
              }
              if (TargetWordEquals(itemType, u"asset")) {
                  return IT_ASSET;
              }
              return IT_ETCITEM;
          };
          auto* const itemTypes = grammar::SemanticPointer<std::vector<ItemType>>($1);
          itemTypes->push_back(itemTypeAt($3));
          $$ = grammar::PointerSemantic(itemTypes);
      }
    ;

n39
    : n39 ';' n40
    | n40
    ;

n40
    : '{' PREDEFINED ';' PREDEFINED '}'
      {
          ChangeBodyStruct changeBody;
          changeBody.TransformId = grammar::SemanticInt32($2);
          changeBody.Prob = grammar::SemanticInt32($4);
          m_vChangeBody.push_back(changeBody);
      }
    ;

n41
    : n41 ';' IDENTIFIER
      {
          const auto appendMaterial = [&]() {
              const char16_t* const material = SemanticWord($3);
              if (TargetWordEquals(material, u"mithril")) {
                  m_vMaterial.push_back(MATT_MITHRIL);
                  return;
              }

              const std::string hostMaterial = Utf16ToUtf8(material);
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d] : Undefined material(%s)",
              //     L"..\\Script\\skilldata.txt",
              //     m_lexer.line + 1,
              //     hostMaterial.c_str())
              // L2_LOG_ERROR(Log,
              //     L"%s[%d] : Undefined material(%s)",
              //     L"..\\Script\\skilldata.txt",
              //     m_lexer.line + 1,
              //     hostMaterial.c_str())
              L2_LOG_ERROR(
                  Log, "skilldata.txt line %lld: unknown material '%s'; expected mithril. This material entry is omitted.",
                  static_cast<long long>(m_lexer.line) + 1, hostMaterial.c_str()
              );
          };
          appendMaterial();
      }
    | IDENTIFIER
      {
          const auto appendMaterial = [&]() {
              const char16_t* const material = SemanticWord($1);
              if (TargetWordEquals(material, u"mithril")) {
                  m_vMaterial.push_back(MATT_MITHRIL);
                  return;
              }

              const std::string hostMaterial = Utf16ToUtf8(material);
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d] : Undefined material(%s)",
              //     L"..\\Script\\skilldata.txt",
              //     m_lexer.line + 1,
              //     hostMaterial.c_str())
              // L2_LOG_ERROR(Log,
              //     L"%s[%d] : Undefined material(%s)",
              //     L"..\\Script\\skilldata.txt",
              //     m_lexer.line + 1,
              //     hostMaterial.c_str())
              L2_LOG_ERROR(
                  Log, "skilldata.txt line %lld: unknown material '%s'; expected mithril. This material entry is omitted.",
                  static_cast<long long>(m_lexer.line) + 1, hostMaterial.c_str()
              );
          };
          m_vMaterial.clear();
          appendMaterial();
      }
    ;

n42
    : n42 ';' '{' '{' n43 '}' ';' predefined_def '}'
      {
          pRestorationUnitGroupList = new CRestorationUnitGroupList(grammar::SemanticDouble($8));
          pRestorationUnitGroupList->groupList = m_vRestorationList;
          m_vRestorationGroupList->push_back(pRestorationUnitGroupList);
      }
    | '{' '{' n43 '}' ';' predefined_def '}'
      {
          m_vRestorationGroupList = new CRestorationUnitGroupListVector;
          pRestorationUnitGroupList = new CRestorationUnitGroupList(grammar::SemanticDouble($6));
          pRestorationUnitGroupList->groupList = m_vRestorationList;
          m_vRestorationGroupList->push_back(pRestorationUnitGroupList);
      }
    ;

n43
    : n43 ';' '{' '{' n44 '}' ';' predefined_def '}'
      {
          pRestorationUnitList = new CRestorationUnitList(grammar::SemanticDouble($8));
          pRestorationUnitList->unitList = m_vRestorationUnit;
          m_vRestorationList->push_back(pRestorationUnitList);
      }
    | '{' '{' n44 '}' ';' predefined_def '}'
      {
          m_vRestorationList = new CRestorationUnitListVector;
          pRestorationUnitList = new CRestorationUnitList(grammar::SemanticDouble($6));
          pRestorationUnitList->unitList = m_vRestorationUnit;
          m_vRestorationList->push_back(pRestorationUnitList);
      }
    ;

n44
    : n44 ';' '{' NAME ';' PREDEFINED '}'
      {
          const auto validateRestorationItem = [this](const grammar::SemanticBits value) {
              const char16_t* const itemName = SemanticWord(value);
              if (g_PrecompiledHeaderDB.Search(itemName) != -1) {
                  return;
              }

              const std::string hostName = Utf16ToUtf8(itemName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Invalid Item[%s] while loading skilldata, Line = [%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Invalid Item[%s] while loading skilldata, Line = [%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: restoration item reference '%s' resolves to -1 in PCH; restoration entry "
                  "construction "
                  "still proceeds.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
              );
          };
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          validateRestorationItem($4);
          pRestorationUnit = new CRestorationUnit(SemanticWord($4), grammar::SemanticInt64($6), 0);
          m_vRestorationUnit->push_back(pRestorationUnit);
      }
    | '{' NAME ';' PREDEFINED '}'
      {
          const auto validateRestorationItem = [this](const grammar::SemanticBits value) {
              const char16_t* const itemName = SemanticWord(value);
              if (g_PrecompiledHeaderDB.Search(itemName) != -1) {
                  return;
              }

              const std::string hostName = Utf16ToUtf8(itemName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Invalid Item[%s] while loading skilldata, Line = [%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Invalid Item[%s] while loading skilldata, Line = [%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: restoration item reference '%s' resolves to -1 in PCH; restoration entry "
                  "construction "
                  "still proceeds.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
              );
          };
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          validateRestorationItem($2);
          m_vRestorationUnit = new CRestorationUnitVector;
          pRestorationUnit = new CRestorationUnit(SemanticWord($2), grammar::SemanticInt64($4), 0);
          m_vRestorationUnit->push_back(pRestorationUnit);
      }
    | n44 ';' '{' NAME ';' PREDEFINED ';' PREDEFINED '}'
      {
          const auto validateRestorationItem = [this](const grammar::SemanticBits value) {
              const char16_t* const itemName = SemanticWord(value);
              if (g_PrecompiledHeaderDB.Search(itemName) != -1) {
                  return;
              }

              const std::string hostName = Utf16ToUtf8(itemName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Invalid Item[%s] while loading skilldata, Line = [%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Invalid Item[%s] while loading skilldata, Line = [%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: restoration item reference '%s' resolves to -1 in PCH; restoration entry "
                  "construction "
                  "still proceeds.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
              );
          };
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          validateRestorationItem($4);
          pRestorationUnit =
              new CRestorationUnit(SemanticWord($4), grammar::SemanticInt64($6), grammar::SemanticInt32($8));
          m_vRestorationUnit->push_back(pRestorationUnit);
      }
    | '{' NAME ';' PREDEFINED ';' PREDEFINED '}'
      {
          const auto validateRestorationItem = [this](const grammar::SemanticBits value) {
              const char16_t* const itemName = SemanticWord(value);
              if (g_PrecompiledHeaderDB.Search(itemName) != -1) {
                  return;
              }

              const std::string hostName = Utf16ToUtf8(itemName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Invalid Item[%s] while loading skilldata, Line = [%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Invalid Item[%s] while loading skilldata, Line = [%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: restoration item reference '%s' resolves to -1 in PCH; restoration entry "
                  "construction "
                  "still proceeds.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
              );
          };
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          validateRestorationItem($2);
          m_vRestorationUnit = new CRestorationUnitVector;
          pRestorationUnit =
              new CRestorationUnit(SemanticWord($2), grammar::SemanticInt64($4), grammar::SemanticInt32($6));
          m_vRestorationUnit->push_back(pRestorationUnit);
      }
    ;

n45
    : n45 ';' '{' NAME ';' PREDEFINED '}'
      {
          pRestorationUnit = new CRestorationUnit(SemanticWord($4), grammar::SemanticInt64($6), 0);
          m_vRestorationListForRewardItem->push_back(pRestorationUnit);
      }
    | '{' NAME ';' PREDEFINED '}'
      {
          m_vRestorationListForRewardItem = new CRestorationUnitVector;
          pRestorationUnit = new CRestorationUnit(SemanticWord($2), grammar::SemanticInt64($4), 0);
          m_vRestorationListForRewardItem->push_back(pRestorationUnit);
      }
    ;

n46
    : n46 ';' '{' NAME ';' predefined_def '}'
      {
          auto* const randomUnit = new CSkillRandomUnit(std::u16string(SemanticWord($4)), grammar::SemanticDouble($6));
          m_vSkillRandomUnit->push_back(randomUnit);
      }
    | '{' NAME ';' predefined_def '}'
      {
          m_vSkillRandomUnit = new std::vector<CSkillRandomUnit*>;
          auto* const randomUnit = new CSkillRandomUnit(std::u16string(SemanticWord($2)), grammar::SemanticDouble($4));
          m_vSkillRandomUnit->push_back(randomUnit);
      }
    ;

n47
    : %empty
    | n48
    | n47 ';' n48
    ;

n48
    : '{'
      {
          m_vItemChanceType.clear();
          m_vWeaponBonusTypeList.clear();
          m_sMagicType.clear();
          m_vAbnormalData.clear();
          m_setAbnormalSlotType.clear();
          m_setSkillName.clear();
          m_vChangeBody.clear();
      }
      n50 '}'
    ;

n50
    : i_blink_def
    | '{' i_blink_def '}' ';' '{' n97 '}'
    ;

i_blink_def
    : I_BLINK ';' IDENTIFIER ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          SkillDirectionType direction = DT_FRONT;
          if (TargetWordEquals(SemanticWord($3), u"_front")) {
              direction = DT_FRONT;
          } else if (TargetWordEquals(SemanticWord($3), u"_back")) {
              direction = DT_BACK;
          } else {
              break;
          }
          publish(new CSkillEffect_i_blink(static_cast<double>(grammar::SemanticInt64($5)), direction));
      }
    | I_STEAL_ABNORMAL ';' IDENTIFIER ';' PREDEFINED ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          CSkillEffect_i_steal_abnormal::SlotGroup slotGroup = CSkillEffect_i_steal_abnormal::SG_BUFF;
          if (TargetWordEquals(SemanticWord($3), u"slot_buff")) {
              slotGroup = CSkillEffect_i_steal_abnormal::SG_BUFF;
          } else if (TargetWordEquals(SemanticWord($3), u"slot_debuff")) {
              slotGroup = CSkillEffect_i_steal_abnormal::SG_DEBUFF;
          } else if (TargetWordEquals(SemanticWord($3), u"slot_all")) {
              slotGroup = CSkillEffect_i_steal_abnormal::SG_ALL;
          } else {
              break;
          }
          publish(
              new CSkillEffect_i_steal_abnormal(slotGroup, grammar::SemanticInt32($5), grammar::SemanticInt32($7))
          );
      }
    | I_M_ATTACK_BY_ABNORMAL ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_m_attack_by_abnormal(grammar::SemanticInt32($3)));
      }
    | I_FOCUS_SOUL ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_focus_soul(grammar::SemanticInt32($3)));
      }
    | I_REMOVE_SOUL ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_remove_soul(grammar::SemanticInt32($3)));
      }
    | I_SUMMON_TRAP ';' NAME
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_summon_trap(std::u16string(SemanticWord($3))));
      }
    | I_DETECT_TRAP ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_detect_trap(grammar::SemanticInt32($3)));
      }
    | I_DEFUSE_TRAP ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_defuse_trap(grammar::SemanticInt32($3)));
      }
    | I_TRANSFER_HATE ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_transfer_hate(static_cast<double>(grammar::SemanticInt64($3))));
      }
    | I_CALL_SKILL ';' NAME
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          auto* const effect = new CSkillEffect_i_call_skill(std::u16string(SemanticWord($3)));
          publish(effect);
          if (m_EffectActivateTiming == CSkillInfo::EAT_SUCCESS) {
              g_SkillInfoVerifyDataDB[m_pSkillInfo->GetKey()].m_vEffectList.push_back(effect);
          }
      }
    | I_CALL_MY_SKILL_LIST ';' '{' n63 '}'
      {
          [&] {
              const auto publish = [this](CSkillEffect* effect) {
                  m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
              };
              auto* const skillIds = grammar::SemanticPointer<std::vector<std::int64_t>>($4);
              const int currentSkillId = m_pSkillInfo->m_nSkillId;
              const auto currentInsert =
                  m_CallTree.emplace(static_cast<std::int64_t>(currentSkillId), i_call_my_skillNode(currentSkillId));
              i_call_my_skillNode& currentNode = currentInsert.first->second;
              for (const std::int64_t skillId : *skillIds) {
                  i_call_my_skillNode calledNode(static_cast<int>(skillId), currentNode);
                  const auto found = m_CallTree.find(skillId);
                  if (found == m_CallTree.end()) {
                      m_CallTree.emplace(skillId, calledNode);
                      continue;
                  }

                  found->second.updateFromList(currentNode);
                  if (hasCycle(&found->second)) {
                      // Log.Add(
                      //     LOG_ERROR,
                      //     L"i_call_my_skill_list makes infinite loop. skill_id = [%d], Line = [%d]",
                      //     currentSkillId,
                      //     m_nLineNumber)
                      // L2_LOG_ERROR(Log,
                      //     L"i_call_my_skill_list makes infinite loop. skill_id = [%d], Line = [%d]",
                      //     currentSkillId,
                      //     m_nLineNumber)
                      L2_LOG_ERROR(
                          Log,
                          "skilldata.txt line %lld: i_call_my_skill_list links skill %d to skill %lld and creates a cycle; "
                          "the effect is omitted, but call-tree updates are retained.",
                          static_cast<long long>(m_lexer.line) + 1, currentSkillId, static_cast<long long>(skillId)
                      );
                      // Current returns before deleting the semantic vector.
                      return;
                  }
              }
              publish(new CSkillEffect_i_call_my_skill_list(*skillIds));
              delete skillIds;
          }();
      }
    | I_CALL_SKILL_BY_TARGET ';' NAME
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_call_skill_by_target(std::u16string(SemanticWord($3))));
      }
    | I_CALL_SKILL_BY_TARGET_DIVIDE ';' NAME ';' NAME
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_call_skill_by_target_divide(
              std::u16string(SemanticWord($3)), std::u16string(SemanticWord($5))
          ));
      }
    | I_CALL_SKILL_BY_ABNORMAL_SLOT ';' '{' n84 '}' ';' NAME
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_call_skill_by_abnormal_slot(m_setAbnormalSlotType, std::u16string(SemanticWord($7))));
      }
    | P_TRIGGER_SKILL_BY_DMG ';' '{' PK_GROUP ';' PREDEFINED ';' PREDEFINED '}' ';' '{' PREDEFINED ';' PREDEFINED ';'
      PER_GROUP '}' ';' NAME ';' SELF_GROUP ';' '{' identifier_def_2 '}'
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_trigger_skill_by_dmg(
              static_cast<EnemyType>(grammar::SemanticInt32($4)), grammar::SemanticInt32($6),
              grammar::SemanticInt32($8), grammar::SemanticInt32($12), grammar::SemanticInt32($14),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($16)), SemanticWord($19),
              static_cast<SkillTargetTypeEnum>(grammar::SemanticInt32($21))
          );
          effect->SetArmorTypeArray(m_ArmorTypeArray);
          publishPump(effect);
      }
    | P_TRIGGER_SKILL_BY_ABNORMAL_SLOT ';' '{' PK_GROUP ';' PREDEFINED ';' PREDEFINED '}' ';' '{' PREDEFINED '}' ';' NAME ';'
      SELF_GROUP ';' '{' n80 '}'
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_trigger_skill_by_abnormal_slot(
              static_cast<EnemyType>(grammar::SemanticInt32($4)), grammar::SemanticInt32($6),
              grammar::SemanticInt32($8), grammar::SemanticInt32($12), SemanticWord($15),
              static_cast<SkillTargetTypeEnum>(grammar::SemanticInt32($17)), m_vAbnormalData
          ));
      }
    | P_TRIGGER_SKILL_BY_ATTACK ';' '{' PK_GROUP ';' PREDEFINED ';' PREDEFINED '}' ';' PREDEFINED ';' '{' PREDEFINED ';'
      PREDEFINED ';' PER_GROUP '}' ';' NAME ';' SELF_GROUP ';' '{' identifier_def '}'
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_trigger_skill_by_attack(
              static_cast<EnemyType>(grammar::SemanticInt32($4)), grammar::SemanticInt32($6),
              grammar::SemanticInt32($8), static_cast<CriticalType::Enum>(grammar::SemanticInt32($11)),
              grammar::SemanticInt32($14), grammar::SemanticInt32($16),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($18)), SemanticWord($21),
              static_cast<SkillTargetTypeEnum>(grammar::SemanticInt32($23))
          );
          effect->SetAttackTypeArray(m_AttackTypeArray);
          publishPump(effect);
      }
    | P_TRIGGER_SKILL_BY_KILL ';' PK_GROUP ';' PREDEFINED ';' NAME
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_trigger_skill_by_kill(
              static_cast<EnemyType>(grammar::SemanticInt32($3)), grammar::SemanticInt32($5), SemanticWord($7)
          ));
      }
    | P_TRIGGER_SKILL_BY_SKILL ';' '{' PK_GROUP ';' PREDEFINED ';' PREDEFINED '}' ';' '{' n63 '}' ';' PREDEFINED ';' NAME ';'
      SELF_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_trigger_skill_by_skill(
              grammar::SemanticPointer<std::vector<std::int64_t>>($12), grammar::SemanticInt32($15),
              SemanticWord($17), static_cast<SkillTargetTypeEnum>(grammar::SemanticInt32($19)),
              static_cast<EnemyType>(grammar::SemanticInt32($4)), grammar::SemanticInt32($6),
              grammar::SemanticInt32($8)
          ));
      }
    | P_TRIGGER_SKILL_BY_SKILL ';' '{' n63 '}' ';' PREDEFINED ';' NAME ';' SELF_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_trigger_skill_by_skill(
              grammar::SemanticPointer<std::vector<std::int64_t>>($4), grammar::SemanticInt32($7), SemanticWord($9),
              static_cast<SkillTargetTypeEnum>(grammar::SemanticInt32($11))
          ));
      }
    | P_TRIGGER_SKILL_BY_AVOID ';' PREDEFINED ';' NAME ';' SELF_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_trigger_skill_by_avoid(
              grammar::SemanticInt32($3), SemanticWord($5),
              static_cast<SkillTargetTypeEnum>(grammar::SemanticInt32($7))
          ));
      }
    | P_TRIGGER_SKILL_BY_BLOCK ';' '{' PK_GROUP ';' PREDEFINED ';' PREDEFINED '}' ';' PREDEFINED ';' PREDEFINED ';' NAME ';'
      SELF_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          if (grammar::SemanticInt64($11) < 1 || 2 < grammar::SemanticInt64($11)) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"p_trigger_skill_by_block shield defense must be 1(normal) or 2(perfect)")
              // L2_LOG_ERROR(Log,
              //     L"p_trigger_skill_by_block shield defense must be 1(normal) or 2(perfect)")
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: p_trigger_skill_by_block type %lld is outside 1..2 (normal or perfect block); "
                  "the effect still receives its low 32-bit value %d.",
                  static_cast<long long>(m_lexer.line) + 1, static_cast<long long>(grammar::SemanticInt64($11)),
                  grammar::SemanticInt32($11)
              );
          }
          publishPump(new CSkillEffect_p_trigger_skill_by_block(
              static_cast<EnemyType>(grammar::SemanticInt32($4)), grammar::SemanticInt32($6),
              grammar::SemanticInt32($8), grammar::SemanticInt32($11), grammar::SemanticInt32($13),
              SemanticWord($15), static_cast<SkillTargetTypeEnum>(grammar::SemanticInt32($17))
          ));
      }
    | P_TRIGGER_SKILL_BY_DEATH ';' '{' PK_GROUP ';' PREDEFINED ';' PREDEFINED '}' ';' PREDEFINED ';' NAME ';' SELF_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_trigger_skill_by_death(
              static_cast<EnemyType>(grammar::SemanticInt32($4)), grammar::SemanticInt32($6),
              grammar::SemanticInt32($8), grammar::SemanticInt32($11), SemanticWord($13),
              static_cast<SkillTargetTypeEnum>(grammar::SemanticInt32($15))
          ));
      }
    | P_TRIGGER_SKILL_BY_MAGIC_TYPE ';' '{' n78 '}' ';' PREDEFINED ';' PREDEFINED ';' NAME ';' SELF_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_trigger_skill_by_magic_type(
              m_sMagicType, static_cast<BadSKillType::Enum>(grammar::SemanticInt32($7)), grammar::SemanticInt32($9),
              SemanticWord($11), static_cast<SkillTargetTypeEnum>(grammar::SemanticInt32($13))
          ));
      }
    | P_CHANGE_SLOT ';' '{' n80 '}' ';' NAME
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_change_slot(m_vAbnormalData, SemanticWord($7)));
      }
    | P_TRIGGER_SKILL_BY_ATTACKED_SKILL ';' PREDEFINED ';' '{' n82 '}' ';' NAME ';' SELF_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_trigger_skill_by_attacked_skill(
              grammar::SemanticInt32($3), m_setSkillName, SemanticWord($9),
              static_cast<SkillTargetTypeEnum>(grammar::SemanticInt32($11))
          ));
      }
    | P_WRONG_CASTING ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_wrong_casting(grammar::SemanticInt32($3), grammar::SemanticInt32($5)));
      }
    | P_PHYSICAL_ABNORMAL_RESIST ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_physical_abnormal_resist(
              grammar::SemanticInt32($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_MAGIC_ABNORMAL_RESIST ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_magic_abnormal_resist(
              grammar::SemanticInt32($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_MAGIC_CANCEL_RESIST ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_magic_cancel_resist(
              grammar::SemanticInt32($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_LUCKY_ENCHANT_RATE ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_lucky_enchant_rate(
              grammar::SemanticInt32($3), grammar::SemanticInt32($5), grammar::SemanticInt32($7),
              static_cast<SkillFxChangeType>(6)
          ));
      }
    | P_MAGIC_HIT ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_magic_hit(
              static_cast<double>(grammar::SemanticInt32($3)),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_MAGIC_HIT ';' '{' identifier_def_2 '}' ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_magic_hit(
              static_cast<double>(grammar::SemanticInt32($7)),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetArmorTypeArray(m_ArmorTypeArray);
          publishPump(effect);
      }
    | P_MAGIC_AVOID ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_magic_avoid(
              grammar::SemanticInt32($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_PHYSICAL_ATTACK_PC_LEVEL ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_physical_attack_pc_level(grammar::SemanticInt32($3)));
      }
    | P_MAGICAL_ATTACK_PC_LEVEL ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_magical_attack_pc_level(grammar::SemanticInt32($3)));
      }
    | I_M_ATTACK_BY_RANGE ';' predefined_def ';' IDENTIFIER
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          CSkillEffect_i_m_attack_by_range::AttackRangePosition position = CSkillEffect_i_m_attack_by_range::ARP_NEAR;
          if (TargetWordEquals(SemanticWord($5), u"near")) {
              position = CSkillEffect_i_m_attack_by_range::ARP_NEAR;
          } else if (TargetWordEquals(SemanticWord($5), u"far")) {
              position = CSkillEffect_i_m_attack_by_range::ARP_FAR;
          } else {
              break;
          }
          publish(new CSkillEffect_i_m_attack_by_range(static_cast<double>(grammar::SemanticInt64($3)), position));
      }
    | I_SUMMON_AGATHION ';' PREDEFINED ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_summon_agathion(grammar::SemanticInt32($3), grammar::SemanticInt32($5)));
      }
    | I_UNSUMMON_AGATHION
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_unsummon_agathion);
      }
    | I_COMPULSION_SOCIAL_ACTION ';' IDENTIFIER
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          const char16_t* const actionName = SemanticWord($3);
          const int actionId = static_cast<int>(g_PrecompiledHeaderDB.Search(actionName));
          if (actionId == -1) {
              const std::string hostName = Utf16ToUtf8(actionName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"i_compulsion_social_action type[%s] is not valid. line = %d",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"i_compulsion_social_action type[%s] is not valid. line = %d",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: i_compulsion_social_action reference '%s' resolves to -1; the effect is "
                  "omitted.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
              );
              break;
          }
          publish(new CSkillEffect_i_compulsion_social_action(actionId));
      }
    | I_FORCE_SITDOWN
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_force_sitdown);
      }
    | I_SUMMON_CUBIC ';' PREDEFINED ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_summon_cubic(grammar::SemanticInt32($3), grammar::SemanticInt32($5)));
      }
    | I_DISPEL_BY_NAME ';' '{' n65 '}'
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          auto* const effect = new CSkillEffect_i_dispel_by_name;
          auto* const names = grammar::SemanticPointer<std::vector<char16_t*>>($4);
          for (const char16_t* const name : *names) {
              // Target wchar_t is UTF-16. Keep the process-lifetime pointer
              // contract while widening only for the portable host.
              effect->m_vDispelSkillName.push_back(DuplicateSkillName(name));
          }
          publish(effect);
      }
    | I_DISPEL_BY_SLOT ';' IDENTIFIER ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_dispel_by_slot(
              GetAbnormalStatusSlotTypeByName(const_cast<char16_t*>(SemanticWord($3))), grammar::SemanticInt32($5)
          ));
      }
    | I_DISPEL_BY_SLOT_MYSELF ';' IDENTIFIER
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_dispel_by_slot_myself(const_cast<char16_t*>(SemanticWord($3))));
      }
    | I_P_ATTACK ';' predefined_def ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_p_attack(
              static_cast<double>(runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($3))),
              grammar::SemanticInt32($5), grammar::SemanticInt32($7), grammar::SemanticInt32($9)
          ));
      }
    | I_EVENT_ATTACK ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_event_attack(grammar::SemanticDouble($3)));
      }
    | I_P_ATTACK_WEAPON_BONUS ';' predefined_def ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED ';' '{' n76 '}'
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          auto* const effect = new CSkillEffect_i_p_attack_weapon_bonus(
              static_cast<double>(runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($3))),
              grammar::SemanticInt32($5), grammar::SemanticInt32($7), grammar::SemanticInt32($9)
          );
          for (const WeaponBonusType& bonus : m_vWeaponBonusTypeList) {
              effect->m_vWeaponBonusTypeList.push_back(bonus);
          }
          publish(effect);
      }
    | I_P_SOUL_ATTACK ';' predefined_def ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_p_soul_attack(
              static_cast<double>(runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($3))),
              grammar::SemanticInt32($5), grammar::SemanticInt32($7), grammar::SemanticInt32($9)
          ));
      }
    | I_FATAL_BLOW ';' predefined_def ';' predefined_def ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_fatal_blow(
              grammar::SemanticDouble($3), grammar::SemanticDouble($5), grammar::SemanticInt32($7)
          ));
      }
    | I_SOUL_BLOW ';' predefined_def ';' predefined_def ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_soul_blow(
              grammar::SemanticDouble($3), grammar::SemanticDouble($5), grammar::SemanticInt32($7)
          ));
      }
    | I_HEAL ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_heal(grammar::SemanticDouble($3)));
      }
    | I_HEAL_SPECIAL ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_heal_special(grammar::SemanticDouble($3)));
      }
    | I_HEAL_LINK ';' predefined_def ';' PER_GROUP ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_heal_link(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5)),
              grammar::SemanticInt32($7)
          ));
      }
    | I_HP_DOWN ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_hp_down(grammar::SemanticInt32($3)));
      }
    | I_HP_DOWN_SELF ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_hp_down_self(grammar::SemanticInt32($3)));
      }
    | I_HP_DRAIN ';' predefined_def ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_hp_drain(grammar::SemanticDouble($3), grammar::SemanticDouble($5)));
      }
    | I_M_ATTACK ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_m_attack(grammar::SemanticDouble($3)));
      }
    | I_M_ATTACK_LINK ';' predefined_def ';' PER_GROUP ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_m_attack_link(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5)),
              grammar::SemanticInt32($7)
          ));
      }
    | I_M_SOUL_ATTACK ';' predefined_def ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_m_soul_attack(grammar::SemanticDouble($3), grammar::SemanticDouble($5), false));
      }
    | I_ESCAPE ';' IDENTIFIER
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          auto* const effect = new CSkillEffect_i_escape;
          const char16_t* const point = SemanticWord($3);
          if (TargetWordEquals(point, u"village") || TargetWordEquals(point, u"town")) {
              effect->m_RestartPoint = RESTART_VILLAGE;
          } else if (TargetWordEquals(point, u"agit")) {
              effect->m_RestartPoint = RESTART_AGIT;
          } else if (TargetWordEquals(point, u"castle")) {
              effect->m_RestartPoint = RESTART_CASTLE;
          } else if (TargetWordEquals(point, u"fortress")) {
              effect->m_RestartPoint = RESTART_FORTRESS;
          } else {
              const std::string hostPoint = Utf16ToUtf8(point);
              // Log.Add(
              //     LOG_ERROR,
              //     L"i_escape invalid param [%s] at [%d]",
              //     hostPoint.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"i_escape invalid param [%s] at [%d]",
              //     hostPoint.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log, "skilldata.txt line %lld: i_escape has unknown restart point '%s'; using village.",
                  static_cast<long long>(m_lexer.line) + 1, hostPoint.c_str()
              );
              effect->m_RestartPoint = RESTART_VILLAGE;
          }
          publish(effect);
      }
    | I_ENCHANT_ARMOR ';' predefined_def ';' PER_GROUP
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_Enchant_Armor(
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($5)), grammar::SemanticDouble($3)
          ));
      }
    | I_ENCHANT_ARMOR
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_Enchant_Armor(SFCT_NONE, 0.0));
      }
    | I_ENCHANT_WEAPON ';' predefined_def ';' PER_GROUP
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_Enchant_Weapon(
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($5)), grammar::SemanticDouble($3)
          ));
      }
    | I_ENCHANT_WEAPON
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_Enchant_Weapon(SFCT_NONE, 0.0));
      }
    | I_LOCKED_ITEM
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_Locked_Item);
      }
    | I_UNLOCKED_ITEM
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_UnLocked_Item);
      }
    | I_ENCHANT_ITEM_MULTI
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_Enchant_Item_MultiLevel(57.0, 29.0, 14.0, SFCT_NONE, 0.0));
      }
    | I_ENCHANT_ITEM_MULTI ';' predefined_def ';' predefined_def ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_Enchant_Item_MultiLevel(
              grammar::SemanticDouble($3), grammar::SemanticDouble($5), grammar::SemanticDouble($7), SFCT_NONE,
              0.0
          ));
      }
    | I_ENCHANT_ITEM_MULTI ';' predefined_def ';' predefined_def ';' predefined_def ';' predefined_def ';' PER_GROUP
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_Enchant_Item_MultiLevel(
              grammar::SemanticDouble($3), grammar::SemanticDouble($5), grammar::SemanticDouble($7),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($11)), grammar::SemanticDouble($9)
          ));
      }
    | I_ENCHANT_ATTRIBUTE ';' IA_VALUE ';' IA_VALUE ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          const auto publishEnchantAttribute = [&](bool attributionItem) {
              const int grade = grammar::SemanticInt32($7);
              if (grade % 3 != 0) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"invalid grade value at [%d] (%d)",
                  //     m_lexer.line + 1,
                  //     grade)
                  // L2_LOG_ERROR(Log,
                  //     L"invalid grade value at [%d] (%d)",
                  //     m_lexer.line + 1,
                  //     grade)
                  // Noise: Grades above 9 are rejected by the next check. Report that failure once.
                  if (grade <= 9) {
                      L2_LOG_ERROR(
                          Log,
                          "skilldata.txt line %lld: attribute enchant grade %d is not divisible by 3; validation continues "
                          "with "
                          "this grade.",
                          static_cast<long long>(m_lexer.line) + 1, grade
                      );
                  }
              }
              if (grade > 9) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Invalid grade value at [%d] (%d). must be lesser than %d",
                  //     m_lexer.line + 1,
                  //     grade,
                  //     9)
                  // L2_LOG_ERROR(Log,
                  //     L"Invalid grade value at [%d] (%d). must be lesser than %d",
                  //     m_lexer.line + 1,
                  //     grade,
                  //     9)
                  L2_LOG_ERROR(
                      Log, "skilldata.txt line %lld: attribute enchant grade %d exceeds 9; the effect is omitted.",
                      static_cast<long long>(m_lexer.line) + 1, grade
                  );
                  return;
              }

              const int attackAttribute = grammar::SemanticInt32($3);
              if (attackAttribute < 0 || attackAttribute >= 6) {
                  // Current action tests distance 8 but prints distance 6.
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"invalid attribute value at [%d] (%d)",
                  //     m_lexer.line + 1,
                  //     grammar::SemanticInt32($5))
                  // L2_LOG_ERROR(Log,
                  //     L"invalid attribute value at [%d] (%d)",
                  //     m_lexer.line + 1,
                  //     grammar::SemanticInt32($5))
                  L2_LOG_ERROR(
                      Log,
                      "skilldata.txt line %lld: attribute enchant attack attribute %d is outside 0..5; the effect is "
                      "omitted.",
                      static_cast<long long>(m_lexer.line) + 1, attackAttribute
                  );
                  return;
              }

              if (attributionItem) {
                  publish(new CSkillEffect_i_Enchant_Attribution_Item(
                      static_cast<AttributeTypeEnum>(attackAttribute),
                      static_cast<AttributeTypeEnum>(grammar::SemanticInt32($5)), grade, grammar::SemanticInt32($9),
                      grammar::SemanticInt32($11)
                  ));
              } else {
                  publish(new CSkillEffect_i_Enchant_Attribute(
                      static_cast<AttributeTypeEnum>(attackAttribute),
                      static_cast<AttributeTypeEnum>(grammar::SemanticInt32($5)), grade, grammar::SemanticInt32($9),
                      grammar::SemanticInt32($11)
                  ));
              }
              m_pSkillInfo->SetEnchantAttributeType(static_cast<AttributeTypeEnum>(attackAttribute));
              m_pSkillInfo->m_nAttrEnchantLevel = grade;
          };
          publishEnchantAttribute(false);
      }
    | I_ENCHANT_ATTRIBUTION_ITEM ';' IA_VALUE ';' IA_VALUE ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          const auto publishEnchantAttribute = [&](bool attributionItem) {
              const int grade = grammar::SemanticInt32($7);
              if (grade % 3 != 0) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"invalid grade value at [%d] (%d)",
                  //     m_lexer.line + 1,
                  //     grade)
                  // L2_LOG_ERROR(Log,
                  //     L"invalid grade value at [%d] (%d)",
                  //     m_lexer.line + 1,
                  //     grade)
                  // Noise: Grades above 9 are rejected by the next check. Report that failure once.
                  if (grade <= 9) {
                      L2_LOG_ERROR(
                          Log,
                          "skilldata.txt line %lld: attribute enchant grade %d is not divisible by 3; validation continues "
                          "with "
                          "this grade.",
                          static_cast<long long>(m_lexer.line) + 1, grade
                      );
                  }
              }
              if (grade > 9) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Invalid grade value at [%d] (%d). must be lesser than %d",
                  //     m_lexer.line + 1,
                  //     grade,
                  //     9)
                  // L2_LOG_ERROR(Log,
                  //     L"Invalid grade value at [%d] (%d). must be lesser than %d",
                  //     m_lexer.line + 1,
                  //     grade,
                  //     9)
                  L2_LOG_ERROR(
                      Log, "skilldata.txt line %lld: attribute enchant grade %d exceeds 9; the effect is omitted.",
                      static_cast<long long>(m_lexer.line) + 1, grade
                  );
                  return;
              }

              const int attackAttribute = grammar::SemanticInt32($3);
              if (attackAttribute < 0 || attackAttribute >= 6) {
                  // Current action tests distance 8 but prints distance 6.
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"invalid attribute value at [%d] (%d)",
                  //     m_lexer.line + 1,
                  //     grammar::SemanticInt32($5))
                  // L2_LOG_ERROR(Log,
                  //     L"invalid attribute value at [%d] (%d)",
                  //     m_lexer.line + 1,
                  //     grammar::SemanticInt32($5))
                  L2_LOG_ERROR(
                      Log,
                      "skilldata.txt line %lld: attribute enchant attack attribute %d is outside 0..5; the effect is "
                      "omitted.",
                      static_cast<long long>(m_lexer.line) + 1, attackAttribute
                  );
                  return;
              }

              if (attributionItem) {
                  publish(new CSkillEffect_i_Enchant_Attribution_Item(
                      static_cast<AttributeTypeEnum>(attackAttribute),
                      static_cast<AttributeTypeEnum>(grammar::SemanticInt32($5)), grade, grammar::SemanticInt32($9),
                      grammar::SemanticInt32($11)
                  ));
              } else {
                  publish(new CSkillEffect_i_Enchant_Attribute(
                      static_cast<AttributeTypeEnum>(attackAttribute),
                      static_cast<AttributeTypeEnum>(grammar::SemanticInt32($5)), grade, grammar::SemanticInt32($9),
                      grammar::SemanticInt32($11)
                  ));
              }
              m_pSkillInfo->SetEnchantAttributeType(static_cast<AttributeTypeEnum>(attackAttribute));
              m_pSkillInfo->m_nAttrEnchantLevel = grade;
          };
          publishEnchantAttribute(true);
      }
    | I_ENCHANT_WEAPON_RATE ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_Enchant_Weapon_Rate(grammar::SemanticInt32($3)));
      }
    | I_ENCHANT_ARMOR_RATE ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_Enchant_Armor_Rate(grammar::SemanticInt32($3)));
      }
    | I_ADD_ENCHANT_WEAPON_LEVEL_INC ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          const int level = grammar::SemanticInt32($3);
          publish(new CSkillEffect_i_add_enchant_weapon_level_inc(level, level, level, 57.0, 29.0, 14.0));
      }
    | I_ADD_ENCHANT_WEAPON_LEVEL_INC ';' '{' PREDEFINED ';' PREDEFINED ';' PREDEFINED '}'
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_add_enchant_weapon_level_inc(
              grammar::SemanticInt32($4), grammar::SemanticInt32($6), grammar::SemanticInt32($8), 57.0, 29.0,
              14.0
          ));
      }
    | I_ADD_ENCHANT_WEAPON_LEVEL_INC ';' '{' PREDEFINED ';' PREDEFINED ';' PREDEFINED '}' ';' '{' predefined_def ';'
      predefined_def ';' predefined_def '}'
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_add_enchant_weapon_level_inc(
              grammar::SemanticInt32($4), grammar::SemanticInt32($6), grammar::SemanticInt32($8),
              grammar::SemanticDouble($12), grammar::SemanticDouble($14), grammar::SemanticDouble($16)
          ));
      }
    | I_ADD_ENCHANT_ARMOR_LEVEL_INC ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          const int level = grammar::SemanticInt32($3);
          publish(new CSkillEffect_i_add_enchant_armor_level_inc(level, level, level, 57.0, 29.0, 14.0));
      }
    | I_ADD_ENCHANT_ARMOR_LEVEL_INC ';' '{' PREDEFINED ';' PREDEFINED ';' PREDEFINED '}'
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_add_enchant_armor_level_inc(
              grammar::SemanticInt32($4), grammar::SemanticInt32($6), grammar::SemanticInt32($8), 57.0, 29.0,
              14.0
          ));
      }
    | I_ADD_ENCHANT_ARMOR_LEVEL_INC ';' '{' PREDEFINED ';' PREDEFINED ';' PREDEFINED '}' ';' '{' predefined_def ';'
      predefined_def ';' predefined_def '}'
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_add_enchant_armor_level_inc(
              grammar::SemanticInt32($4), grammar::SemanticInt32($6), grammar::SemanticInt32($8),
              grammar::SemanticDouble($12), grammar::SemanticDouble($14), grammar::SemanticDouble($16)
          ));
      }
    | I_SUB_ENCHANT_WEAPON_LEVEL_DEC ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_sub_enchant_weapon_level_dec(grammar::SemanticInt32($3)));
      }
    | I_SUB_ENCHANT_ARMOR_LEVEL_DEC ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_sub_enchant_armor_level_dec(grammar::SemanticInt32($3)));
      }
    | I_CHANGE_ATTRIBUTE
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_change_attribute);
      }
    | I_HP_PER_MAX ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_hp_per_max(grammar::SemanticDouble($3)));
      }
    | I_MP_PER_MAX ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_mp_per_max(grammar::SemanticDouble($3)));
      }
    | I_CP_PER_MAX ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_cp_per_max(grammar::SemanticDouble($3)));
      }
    | I_MP_BY_LEVEL ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_mp_by_level(grammar::SemanticInt32($3)));
      }
    | I_PCBANG_POINT_UP ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_pcbang_point_up(grammar::SemanticInt32($3)));
      }
    | I_ADD_HATE ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_add_hate(grammar::SemanticInt32($3)));
      }
    | I_ADD_HATE_MYMASTER ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_add_hate_mymaster(grammar::SemanticInt32($3)));
      }
    | I_HOLYTHING_POSSESS
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_holything_possess);
      }
    | I_HOLYTHING_POSSESS ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_holything_possess(grammar::SemanticInt32($3)));
      }
    | I_INSTALL_CAMP
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_install_camp);
      }
    | i_unlock_def
    | I_DELETE_HATE ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_delete_hate(grammar::SemanticInt32($3)));
      }
    | I_RANDOMIZE_HATE ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_randomize_hate(grammar::SemanticInt32($3)));
      }
    | I_FOCUS_ENERGY ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_focus_energy(grammar::SemanticInt32($3)));
      }
    | I_FOCUS_ENERGY
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_focus_energy);
      }
    | I_FOCUS_MAX_ENERGY
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_focus_max_energy);
      }
    | I_FOCUS_MAX_ENERGY ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_focus_max_energy(grammar::SemanticInt32($3)));
      }
    | I_ENERGY_ATTACK ';' predefined_def ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_energy_attack(
              grammar::SemanticDouble($3), grammar::SemanticInt32($5), grammar::SemanticInt32($7),
              grammar::SemanticInt32($9), grammar::SemanticInt32($11)
          ));
          m_pSkillInfo->m_bIsEnergyAttack = true;
      }
    | I_SOUL_SHOT ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_soul_shot(grammar::SemanticDouble($3)));
      }
    | I_SPIRIT_SHOT ';' predefined_def ';' predefined_def ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_spirit_shot(
              grammar::SemanticDouble($3), grammar::SemanticDouble($5), grammar::SemanticDouble($7)
          ));
      }
    | I_SUMMON_SOUL_SHOT ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_summon_soul_shot(
              static_cast<double>(runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($3)))
          ));
      }
    | I_SUMMON_SPIRIT_SHOT ';' predefined_def ';' predefined_def ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_summon_spirit_shot(
              grammar::SemanticDouble($3), grammar::SemanticDouble($5), grammar::SemanticDouble($7)
          ));
      }
    | I_CONFUSE ';' PREDEFINED ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_confuse(grammar::SemanticInt32($3), grammar::SemanticInt32($5)));
      }
    | I_RUN_AWAY ';' PREDEFINED ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_run_away(grammar::SemanticInt32($3), grammar::SemanticInt32($5)));
      }
    | I_CONSUME_BODY
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_consume_body);
      }
    | I_HP_BY_LEVEL_SELF ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_hp_by_level_self(grammar::SemanticInt32($3)));
      }
    | I_MP_BY_LEVEL_SELF ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_mp_by_level_self(grammar::SemanticInt32($3)));
      }
    | I_SPOIL
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_spoil);
      }
    | I_SWEEPER
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_sweeper);
      }
    | I_PLUNDER
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_plunder);
      }
    | I_SOWING
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_sowing);
      }
    | I_HARVESTING
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_harvesting);
      }
    | I_HP_SELF ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_hp_self(grammar::SemanticDouble($3)));
      }
    | I_BACKSTAB ';' predefined_def ';' predefined_def ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_backstab(
              grammar::SemanticDouble($3), grammar::SemanticDouble($5), grammar::SemanticInt32($7)
          ));
      }
    | I_DISTRUST ';' PREDEFINED ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_distrust(grammar::SemanticInt32($3), grammar::SemanticInt32($5)));
      }
    | I_DELETE_HATE_OF_ME ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_delete_hate_of_me(grammar::SemanticInt32($3)));
      }
    | I_DEATH_LINK ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_death_link(grammar::SemanticDouble($3)));
      }
    | I_DEATH_LINK_MP ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_death_link_mp(grammar::SemanticDouble($3)));
      }
    | I_DISPEL_ALL
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_dispel_all);
      }
    | I_FLY_AWAY
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_fly_away);
      }
    | I_FLY_AWAY ';' IDENTIFIER ';' IDENTIFIER ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          auto* const effect = new CSkillEffect_i_fly_away(grammar::SemanticInt32($7));
          effect->bIsCurve = TargetWordEquals(SemanticWord($3), u"curve");
          effect->bIsPush = TargetWordEquals(SemanticWord($5), u"push");
          publish(effect);
      }
    | I_FLY_SELF ';' IDENTIFIER ';' IDENTIFIER ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          auto* const effect = new CSkillEffect_i_fly_self(grammar::SemanticInt32($7));
          effect->bIsCurve = TargetWordEquals(SemanticWord($3), u"curve");
          effect->bIsPush = TargetWordEquals(SemanticWord($5), u"push");
          publish(effect);
      }
    | I_REGISTER_SIEGE_GOLEM
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_register_siege_golem);
      }
    | I_M_ATTACK_OVER_HIT ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_m_attack_over_hit(grammar::SemanticDouble($3)));
      }
    | I_BLINK_RETURN ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_blink_return(std::min(grammar::SemanticInt32($3), 1500)));
      }
    | I_SET_RETURN_POS ';' PREDEFINED ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_set_return_pos(grammar::SemanticInt32($3), grammar::SemanticInt32($5)));
      }
    | I_CREATE_ITEM_RANDOM
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_create_item_random);
      }
    | I_RESTORATION ';' NAME ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          const auto validateItem = [this](const char16_t* itemName) {
              const int classId = static_cast<int>(g_PrecompiledHeaderDB.Search(itemName));
              if (classId == -1) {
                  const std::string hostName = Utf16ToUtf8(itemName);
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Invalid Item[%s] while loading skilldata, Line = [%d]",
                  //     hostName.c_str(),
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"Invalid Item[%s] while loading skilldata, Line = [%d]",
                  //     hostName.c_str(),
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      "skilldata.txt line %lld: i_restoration item reference '%s' resolves to -1 in PCH; effect "
                      "construction "
                      "still proceeds.",
                      static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
                  );
              }
          };
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          const char16_t* const itemName = SemanticWord($3);
          validateItem(itemName);
          publish(new CSkillEffect_i_restoration(std::u16string(itemName), grammar::SemanticInt32($5), 0));
      }
    | I_RESTORATION ';' NAME ';' PREDEFINED ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          const auto validateItem = [this](const char16_t* itemName) {
              const int classId = static_cast<int>(g_PrecompiledHeaderDB.Search(itemName));
              if (classId == -1) {
                  const std::string hostName = Utf16ToUtf8(itemName);
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Invalid Item[%s] while loading skilldata, Line = [%d]",
                  //     hostName.c_str(),
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"Invalid Item[%s] while loading skilldata, Line = [%d]",
                  //     hostName.c_str(),
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      "skilldata.txt line %lld: i_restoration item reference '%s' resolves to -1 in PCH; effect "
                      "construction "
                      "still proceeds.",
                      static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
                  );
              }
          };
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          const char16_t* const itemName = SemanticWord($3);
          validateItem(itemName);
          publish(new CSkillEffect_i_restoration(
              std::u16string(itemName), grammar::SemanticInt32($5), grammar::SemanticInt32($7)
          ));
      }
    | I_RESTORATION_RANDOM ';' '{' ITEM_CONSUME EQUAL '{' NAME ';' PREDEFINED '}' '}' ';' '{' n43 '}'
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          auto* const effect =
              new CSkillEffect_i_restoration_random(std::u16string(SemanticWord($7)), grammar::SemanticInt32($9));
          effect->m_vRestorationList = m_vRestorationList;
          publish(effect);
      }
    | I_RESTORATION_RANDOM ';' '{' n43 '}'
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          auto* const effect = new CSkillEffect_i_restoration_random;
          effect->m_vRestorationList = m_vRestorationList;
          publish(effect);
      }
    | I_RESTORATION_RANDOM_GROUP ';' '{' n42 '}'
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          auto* const effect = new CSkillEffect_i_restoration_random_group;
          effect->m_vRestorationGroupList = m_vRestorationGroupList;
          publish(effect);
      }
    | I_RESTORATION_ADDRANDOM ';' '{' n45 '}' ';' '{' n43 '}'
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          auto* const effect = new CSkillEffect_i_restoration_AddRandom;
          effect->m_vRestorationList = m_vRestorationList;
          effect->m_vRewardItemList = m_vRestorationListForRewardItem;
          publish(effect);
      }
    | I_GIVE_CONTRIBUTION ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_give_contribution(grammar::SemanticInt32($3)));
      }
    | I_CHANGE_FACE ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_change_face(grammar::SemanticInt32($3)));
      }
    | I_CHANGE_HAIR_STYLE ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_change_hair_style(grammar::SemanticInt32($3)));
      }
    | I_CHANGE_HAIR_COLOR ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_change_hair_color(grammar::SemanticInt32($3)));
      }
    | I_SUMMON_NPC ';' NAME ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_summon_npc(std::u16string(SemanticWord($3)), grammar::SemanticInt32($5)));
      }
    | I_SUMMON_NPC_NEAR ';' NAME ';' PREDEFINED ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_summon_npc_near(
              std::u16string(SemanticWord($3)), grammar::SemanticInt32($5), grammar::SemanticInt32($7)
          ));
      }
    | I_SUMMON_NPC_DYING ';' NAME ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_summon_npc_dying(std::u16string(SemanticWord($3)), grammar::SemanticInt32($5)));
      }
    | I_RESET_QUEST ';' '{' n63 '}'
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          const auto* const questIds = grammar::SemanticPointer<std::vector<std::int64_t>>($4);
          publish(new CSkillEffect_i_reset_quest(*questIds));
      }
    | I_AGATHION_ENERGY ';' predefined_def ';' PER_GROUP
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_agathion_energy(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | I_SUMMON_UNIQUE_NPC ';' NAME
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_summon_unique_npc(std::u16string(SemanticWord($3))));
      }
    | I_GET_AGRO
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_get_agro);
      }
    | I_TELEPORT_TO_TARGET
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_teleport_to_target);
      }
    | I_TELEPORT_TO_SUMMON ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_teleport_to_summon(grammar::SemanticInt32($3)));
      }
    | I_INSTALL_ADVANCE_BASE
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_install_advance_base);
      }
    | I_UNINSTALL_ADVANCE_BASE
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_uninstall_advance_base);
      }
    | I_CAPTURE_OWNTHING
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_capture_ownthing);
      }
    | I_CAPTURE_OWNTHING_START
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_capture_ownthing_start);
      }
    | I_COLLECTING ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_collecting(grammar::SemanticInt32($3)));
      }
    | I_EVENT_AGATHION_REUSE_DELAY
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_event_agathion_reuse_delay);
      }
    | I_SHOW_REUSE_DELAY ';' '{' n63 '}'
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          auto* const skillIds = grammar::SemanticPointer<std::vector<std::int64_t>>($4);
          publish(new CSkillEffect_i_show_reuse_delay(*skillIds));
          delete skillIds;
      }
    | I_SET_REUSE_DELAY ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          int updateStyle = grammar::SemanticInt32($7);
          if (updateStyle < 0 || updateStyle > 2) {
              // The current binary supplies one extra vararg and, therefore,
              // prints the lexer line as the invalid value and updateStyle as
              // the final line value. Preserve that observable generated-code
              // bug literally.
              // Log.Add(
              //     LOG_ERROR,
              //     L"i_set_reuse_delay at [%d] Should 0 ~ 2. not [%d]. Line = [%d]",
              //     grammar::SemanticInt32($3),
              //     m_lexer.line + 1,
              //     updateStyle,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"i_set_reuse_delay at [%d] Should 0 ~ 2. not [%d]. Line = [%d]",
              //     grammar::SemanticInt32($3),
              //     m_lexer.line + 1,
              //     updateStyle,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log, "skilldata.txt line %lld: i_set_reuse_delay update style %d is outside 0..2; using 1.",
                  static_cast<long long>(m_lexer.line) + 1, updateStyle
              );
              updateStyle = 1;
          }
          publish(
              new CSkillEffect_i_set_reuse_delay(grammar::SemanticInt32($3), grammar::SemanticInt32($5), updateStyle)
          );
      }
    | I_REUSE_SKILL_RESET ';' '{' n63 '}' ';' FLOAT_NUMBER ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          auto* const skillIds = grammar::SemanticPointer<std::vector<std::int64_t>>($4);
          const double probability = grammar::SemanticDouble($7);
          const double delayLimitSec = grammar::SemanticDouble($9);
          const int delayLimitMSec = runtime::Win64CvttDoubleToInt32(delayLimitSec * 1000.0);
          if (probability < 0.0 || probability > 100.0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"i_reuse_skill_reset prob Should between [0.0, 100.0]. not [%f]. Line = [%d]",
              //     probability,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"i_reuse_skill_reset prob Should between [0.0, 100.0]. not [%f]. Line = [%d]",
              //     probability,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: i_reuse_skill_reset probability %.17g is outside 0..100; the effect is "
                  "omitted.",
                  static_cast<long long>(m_lexer.line) + 1, probability
              );
              break;
          }
          if (delayLimitMSec < 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"i_reuse_skill_reset HighDelayLimit Should bigger than 0. not [%f]. Line = [%d]",
              //     delayLimitSec,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"i_reuse_skill_reset HighDelayLimit Should bigger than 0. not [%f]. Line = [%d]",
              //     delayLimitSec,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: i_reuse_skill_reset delay limit %.17g seconds converts to negative "
                  "milliseconds (%d); the effect is omitted.",
                  static_cast<long long>(m_lexer.line) + 1, delayLimitSec, delayLimitMSec
              );
              break;
          }
          auto* const effect = new CSkillEffect_i_reuse_skill_reset;
          effect->Init(*skillIds, probability, delayLimitMSec);
          publish(effect);
          delete skillIds;
      }
    | I_REUSE_SKILL_REDUCE ';' '{' n63 '}' ';' FLOAT_NUMBER ';' predefined_def ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          auto* const skillIds = grammar::SemanticPointer<std::vector<std::int64_t>>($4);
          const double probability = grammar::SemanticDouble($7);
          const double reduceSec = grammar::SemanticDouble($9);
          const double delayLimitSec = grammar::SemanticDouble($11);
          const int reduceMSec = runtime::Win64CvttDoubleToInt32(reduceSec * 1000.0);
          const int delayLimitMSec = runtime::Win64CvttDoubleToInt32(delayLimitSec * 1000.0);
          if (probability < 0.0 || probability > 100.0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"i_reuse_skill_reduce prob Should between [0.0, 100.0]. not [%f]. Line = [%d]",
              //     probability,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"i_reuse_skill_reduce prob Should between [0.0, 100.0]. not [%f]. Line = [%d]",
              //     probability,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: i_reuse_skill_reduce probability %.17g is outside 0..100; the effect is "
                  "omitted.",
                  static_cast<long long>(m_lexer.line) + 1, probability
              );
              break;
          }
          if (reduceMSec < 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"i_reuse_skill_reduce Amount sec to be reduced Should bigger than 0. not [%f]. Line = [%d]",
              //     reduceSec,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"i_reuse_skill_reduce Amount sec to be reduced Should bigger than 0. not [%f]. Line = [%d]",
              //     reduceSec,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: i_reuse_skill_reduce reduction %.17g seconds converts to negative "
                  "milliseconds (%d); the effect is omitted.",
                  static_cast<long long>(m_lexer.line) + 1, reduceSec, reduceMSec
              );
              break;
          }
          if (delayLimitMSec < 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"i_reuse_skill_reduce HighDelayLimit Should bigger than 0. not [%f]. Line = [%d]",
              //     delayLimitSec,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"i_reuse_skill_reduce HighDelayLimit Should bigger than 0. not [%f]. Line = [%d]",
              //     delayLimitSec,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: i_reuse_skill_reduce delay limit %.17g seconds converts to negative "
                  "milliseconds (%d); the effect is omitted.",
                  static_cast<long long>(m_lexer.line) + 1, delayLimitSec, delayLimitMSec
              );
              break;
          }
          auto* const effect = new CSkillEffect_i_reuse_skill_reduce;
          effect->Init(*skillIds, probability, reduceMSec, delayLimitMSec);
          publish(effect);
          delete skillIds;
      }
    | I_RANDOM_TARGET
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_random_target);
      }
    | I_TRANSFER_ABNORMAL ';' IDENTIFIER ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_transfer_abnormal(
              GetAbnormalStatusSlotTypeByName(const_cast<char16_t*>(SemanticWord($3))), grammar::SemanticInt32($5)
          ));
      }
    | I_REMOTE_CONTROL_TRAP
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_remote_control_trap);
      }
    | I_KNOCKBACK ';' PREDEFINED ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_knockback(grammar::SemanticInt32($3), grammar::SemanticInt32($5), 0, 0));
          m_pSkillInfo->m_IsFlySkill = true;
      }
    | I_KNOCKBACK ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_knockback(
              grammar::SemanticInt32($3), grammar::SemanticInt32($5), grammar::SemanticInt32($7),
              grammar::SemanticInt32($9)
          ));
          m_pSkillInfo->m_IsFlySkill = true;
      }
    | %empty
    | I_POSITION_CHANGE ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_position_change(grammar::SemanticDouble($3)));
      }
    | I_SUMMON_HALLUCINATION ';' PREDEFINED ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_summon_hallucination(grammar::SemanticInt32($3), grammar::SemanticInt32($5)));
      }
    | I_GET_ULTIMATE_SKILL_GAUGE ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_get_ultimate_skill_gauge(grammar::SemanticInt32($3)));
      }
    | I_CHANGE_CLASS ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_change_class(grammar::SemanticInt32($3)));
          m_pSkillInfo->m_isClassChangeSkill = true;
      }
    | I_DELETE_TOPAGRO ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_delete_topagro(grammar::SemanticInt32($3)));
      }
    | I_CONSUME_SUMMON
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_consume_summon);
      }
    | I_SUMMON_NPC_POSITION ';' NAME
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_summon_npc_position(std::u16string(SemanticWord($3))));
      }
    | I_SHOW_USABLE_DAILY_SKILL_COUNT ';' '{' n63 '}' ';' '{' n63 '}'
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          auto* const skillIds = grammar::SemanticPointer<std::vector<std::int64_t>>($4);
          auto* const skillLevels = grammar::SemanticPointer<std::vector<std::int64_t>>($8);
          publish(new CSkillEffect_i_show_usable_daily_skill_count(skillIds, skillLevels));
      }
    | I_TELEPORT_TO_NPC ';' NAME
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_teleport_to_npc(std::u16string(SemanticWord($3))));
      }
    | I_PARTY_TELEPORT_TO_NPC ';' NAME
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_party_teleport_to_npc(std::u16string(SemanticWord($3))));
      }
    | I_SHAPE_SHIFT_WP ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_shape_shift_wp(grammar::SemanticInt32($3)));
      }
    | I_SHAPE_SHIFT_WP
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_shape_shift_wp);
      }
    | I_BLESS_SHAPE_SHIFT_WP ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_bless_shape_shift_wp(grammar::SemanticInt32($3)));
      }
    | I_BLESS_SHAPE_SHIFT_WP
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_bless_shape_shift_wp);
      }
    | I_SHAPE_SHIFT_WP_FIXED ';' PREDEFINED ';' '{'
      {
          pShapeShiftWpFixedEffect = new CSkillEffect_i_shape_shift_wp_fixed(grammar::SemanticInt32($3));
      }
      n86 '}'
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(pShapeShiftWpFixedEffect);
          pShapeShiftWpFixedEffect = nullptr;
      }
    | I_SHAPE_SHIFT_WP_FIXED ';' '{'
      { pShapeShiftWpFixedEffect = new CSkillEffect_i_shape_shift_wp_fixed; }
      n86 '}'
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(pShapeShiftWpFixedEffect);
          pShapeShiftWpFixedEffect = nullptr;
      }
    | I_RESTORE_SHAPE_SHIFT_ITEM
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_restore_shape_shift_item);
      }
    | I_SHAPE_SHIFT_AM
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_shape_shift_am);
      }
    | I_SHAPE_SHIFT_AM ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_shape_shift_am(grammar::SemanticInt32($3)));
      }
    | I_BLESS_SHAPE_SHIFT_AM
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_bless_shape_shift_am);
      }
    | I_BLESS_SHAPE_SHIFT_AM ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_bless_shape_shift_am(grammar::SemanticInt32($3)));
      }
    | I_SHAPE_SHIFT_AM_FIXED ';' '{'
      { pShapeShiftAmFixedEffect = new CSkillEffect_i_shape_shift_am_fixed; }
      n88 '}'
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(pShapeShiftAmFixedEffect);
          pShapeShiftAmFixedEffect = nullptr;
      }
    | I_SHAPE_SHIFT_AM_FIXED ';' PREDEFINED ';' '{'
      {
          pShapeShiftAmFixedEffect = new CSkillEffect_i_shape_shift_am_fixed(grammar::SemanticInt32($3));
      }
      n88 '}'
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(pShapeShiftAmFixedEffect);
          pShapeShiftAmFixedEffect = nullptr;
      }
    | I_SHAPE_SHIFT_ITEM_AUTO_MATCHING ';' '{'
      {
          pShapeShiftItemAutoMatchingEffect = new CSkillEffect_i_shape_shift_item_auto_matching;
      }
      n90 '}'
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(pShapeShiftItemAutoMatchingEffect);
          pShapeShiftItemAutoMatchingEffect = nullptr;
      }
    | I_SHAPE_SHIFT_ITEM_AUTO_MATCHING ';' PREDEFINED ';' '{'
      {
          pShapeShiftItemAutoMatchingEffect =
              new CSkillEffect_i_shape_shift_item_auto_matching(grammar::SemanticInt32($3));
      }
      n90 '}'
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(pShapeShiftItemAutoMatchingEffect);
          pShapeShiftItemAutoMatchingEffect = nullptr;
      }
    | I_CHANGE_VITAL_ITEM_COUNT ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(
              new CSkillEffect_i_change_vital_item_count(runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($3)))
          );
      }
    | I_FATAL_BLOW_ABNORMAL ';' predefined_def ';' predefined_def ';' PREDEFINED ';' IDENTIFIER ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_fatal_blow_abnormal(
              grammar::SemanticDouble($3), grammar::SemanticDouble($5), grammar::SemanticInt32($7),
              const_cast<char16_t*>(SemanticWord($9)), grammar::SemanticDouble($11)
          ));
      }
    | I_RANDOM_CALL_SKILL ';' '{'
      { pRandomCallSkillEffect = new CSkillEffect_i_random_call_skill; }
      n95 '}'
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(pRandomCallSkillEffect);
          pRandomCallSkillEffect = nullptr;
      }
    | I_ADD_EVENT_CONTRIBUTE ';' PREDEFINED ';' PREDEFINED
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_add_event_contribute(grammar::SemanticInt32($3), grammar::SemanticInt32($5)));
      }
    | I_ENSOUL ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_ensoul(grammar::SemanticDouble($3)));
      }
    | CUB_M_ATTACK ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_cub_m_attack(grammar::SemanticDouble($3)));
      }
    | CUB_HP_DRAIN ';' predefined_def ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_cub_hp_drain(grammar::SemanticDouble($3), grammar::SemanticDouble($5)));
      }
    | CUB_HEAL ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_cub_heal(runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($3))));
      }
    | I_GET_EXP ';' predefined_def ';' predefined_def ';' predefined_def
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_get_exp(
              runtime::Win64CvttDoubleToInt64(grammar::SemanticDouble($3)),
              runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($5)), grammar::SemanticDouble($7)
          ));
      }
    | I_SUMMON_PLEDGE ';' NAME
      {
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          publish(new CSkillEffect_i_summon_pledge(SemanticWord($3)));
      }
    | I_PVPPOINT ';' PREDEFINED
    | T_HP ';' predefined_def ';' PREDEFINED ';' PER_GROUP
      {
          const auto pushTick = [this](CSkillEffect* effect) { m_pSkillInfo->m_TickEffects.push_back(effect); };
          const auto publish = [this](CSkillEffect* effect) { m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming); };
          const double amount = grammar::SemanticDouble($3);
          pushTick(new CSkillEffect_t_hp(
              amount, grammar::SemanticInt32($5), static_cast<SkillFxChangeType>(grammar::SemanticInt32($7)), true
          ));
          publish(new CSkillEffect_i_t_hp_critical(runtime::Win64CvttDoubleToInt32(amount * 10.0)));
      }
    | T_HP ';' predefined_def ';' PREDEFINED ';' PER_GROUP ';' PREDEFINED
      {
          const auto pushTick = [this](CSkillEffect* effect) { m_pSkillInfo->m_TickEffects.push_back(effect); };
          pushTick(new CSkillEffect_t_hp(
              grammar::SemanticDouble($3), grammar::SemanticInt32($5),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7)), grammar::SemanticInt64($9) == 1
          ));
      }
    | CUB_HP ';' predefined_def ';' PREDEFINED ';' PER_GROUP
      {
          const auto pushTick = [this](CSkillEffect* effect) { m_pSkillInfo->m_TickEffects.push_back(effect); };
          pushTick(new CSkillEffect_t_cub_hp(
              grammar::SemanticDouble($3), grammar::SemanticInt32($5),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          ));
      }
    | T_MP ';' predefined_def ';' PREDEFINED ';' PER_GROUP
      {
          const auto pushTick = [this](CSkillEffect* effect) { m_pSkillInfo->m_TickEffects.push_back(effect); };
          pushTick(new CSkillEffect_t_mp(
              grammar::SemanticDouble($3), grammar::SemanticInt32($5),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          ));
      }
    | T_HP_FATAL ';' predefined_def ';' PREDEFINED ';' PER_GROUP
      {
          const auto pushTick = [this](CSkillEffect* effect) { m_pSkillInfo->m_TickEffects.push_back(effect); };
          pushTick(new CSkillEffect_t_hp_fatal(
              grammar::SemanticDouble($3), grammar::SemanticInt32($5),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          ));
      }
    | T_CP ';' predefined_def ';' PREDEFINED ';' PER_GROUP
      {
          const auto pushTick = [this](CSkillEffect* effect) { m_pSkillInfo->m_TickEffects.push_back(effect); };
          pushTick(new CSkillEffect_t_cp(
              grammar::SemanticDouble($3), grammar::SemanticInt32($5),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          ));
      }
    | T_HP_BY_HP ';' predefined_def ';' PREDEFINED ';' PER_GROUP ';' PREDEFINED
      {
          const auto pushTick = [this](CSkillEffect* effect) { m_pSkillInfo->m_TickEffects.push_back(effect); };
          pushTick(new CSkillEffect_t_hp_by_hp(
              grammar::SemanticDouble($3), grammar::SemanticInt32($5),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7)),
              static_cast<double>(grammar::SemanticInt64($9)) / 100.0
          ));
      }
    | T_HP_IN_INVINCIBLE ';' predefined_def ';' PREDEFINED ';' PER_GROUP
      {
          const auto pushTick = [this](CSkillEffect* effect) { m_pSkillInfo->m_TickEffects.push_back(effect); };
          pushTick(new CSkillEffect_t_hp_in_invincible(
              grammar::SemanticDouble($3), grammar::SemanticInt32($5),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7)), true
          ));
      }
    | T_HP_MAGICAL_ATTACK ';' predefined_def ';' PREDEFINED ';' PER_GROUP ';' PREDEFINED
      {
          const auto pushTick = [this](CSkillEffect* effect) { m_pSkillInfo->m_TickEffects.push_back(effect); };
          const double amount = grammar::SemanticDouble($3);
          const int baseMagicPower = grammar::SemanticInt32($9);
          if (!(amount >= 0.0)) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"t_hp_magical_attack [%.2f] should be bigger than 0. line = [%d]",
              //     amount,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"t_hp_magical_attack [%.2f] should be bigger than 0. line = [%d]",
              //     amount,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: t_hp_magical_attack amount %.17g fails the nonnegative-value check; the "
                  "effect is omitted.",
                  static_cast<long long>(m_lexer.line) + 1, amount
              );
              break;
          }
          if (baseMagicPower < 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"t_hp_magical_attack [%d] baseMagicPower should be bigger than 0. line = [%d]",
              //     baseMagicPower,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"t_hp_magical_attack [%d] baseMagicPower should be bigger than 0. line = [%d]",
              //     baseMagicPower,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: t_hp_magical_attack base magic power %d is negative; the effect is omitted.",
                  static_cast<long long>(m_lexer.line) + 1, baseMagicPower
              );
              break;
          }
          pushTick(new CSkillEffect_t_hp_magical_attack(
              amount, grammar::SemanticInt32($5), static_cast<SkillFxChangeType>(grammar::SemanticInt32($7)),
              baseMagicPower
          ));
      }
    | T_GET_ENERGY ';' PREDEFINED
      {
          const auto pushTick = [this](CSkillEffect* effect) { m_pSkillInfo->m_TickEffects.push_back(effect); };
          pushTick(new CSkillEffect_t_get_energy(1.0, grammar::SemanticInt32($3), SFCT_DIFF));
      }
    | T_ENERGY ';' PREDEFINED ';' PREDEFINED
      {
          const auto pushTick = [this](CSkillEffect* effect) { m_pSkillInfo->m_TickEffects.push_back(effect); };
          pushTick(new CSkillEffect_t_energy(1.0, grammar::SemanticInt32($3), SFCT_DIFF, grammar::SemanticInt32($5))
          );
      }
    | T_PULL ';' PREDEFINED ';' predefined_def
      {
          const auto pushTick = [this](CSkillEffect* effect) { m_pSkillInfo->m_TickEffects.push_back(effect); };
          pushTick(new CSkillEffect_t_pull(1.0, grammar::SemanticInt32($3), SFCT_DIFF, grammar::SemanticDouble($5)));
      }
    | T_HP_TO_OWNER ';' predefined_def ';' PREDEFINED ';' PER_GROUP ';' '{' n65 '}' ';' predefined_def
      {
          const auto pushTick = [this](CSkillEffect* effect) { m_pSkillInfo->m_TickEffects.push_back(effect); };
          auto* const effect = new CSkillEffect_t_hp_to_owner(
              grammar::SemanticDouble($3), grammar::SemanticInt32($5),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7)), grammar::SemanticDouble($13)
          );
          auto* const bonusNames = grammar::SemanticPointer<std::vector<char16_t*>>($10);
          for (const char16_t* const name : *bonusNames) {
              if (TargetWordEquals(name, u"hp")) {
                  effect->m_BonusStat.push_back(CSkillEffect_t_hp_to_owner::HP);
              } else if (TargetWordEquals(name, u"mp")) {
                  effect->m_BonusStat.push_back(CSkillEffect_t_hp_to_owner::MP);
              } else if (TargetWordEquals(name, u"cp")) {
                  effect->m_BonusStat.push_back(CSkillEffect_t_hp_to_owner::CP);
              }
          }
          delete bonusNames;
          pushTick(effect);
      }
    | A_SPEED ';' '{' identifier_def_2 '}' ';' predefined_def ';' PER_GROUP ';' NAME ';' SELF_GROUP
      {
          const auto pushAura = [this](CSkillEffect* effect) { m_pSkillInfo->m_vAuraEffect.push_back(effect); };
          auto* const effect = new CSkillEffect_a_speed(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9)),
              const_cast<char16_t*>(SemanticWord($11)), static_cast<SkillTargetTypeEnum>(grammar::SemanticInt32($13))
          );
          effect->SetArmorTypeArray(m_ArmorTypeArray);
          pushAura(effect);
      }
    | P_AURA ';' NAME
      {
          const auto pushAura = [this](CSkillEffect* effect) { m_pSkillInfo->m_vAuraEffect.push_back(effect); };
          pushAura(new CSkillEffect_p_aura(const_cast<char16_t*>(SemanticWord($3))));
      }
    | A_SYNERGY_SKILL ';' PREDEFINED ';' SELF_GROUP
      {
          const auto pushAura = [this](CSkillEffect* effect) { m_pSkillInfo->m_vAuraEffect.push_back(effect); };
          pushAura(new CSkillEffect_a_synergy_skill(
              grammar::SemanticInt32($3), static_cast<SkillTargetTypeEnum>(grammar::SemanticInt32($5))
          ));
      }
    | P_DEFENCE_CRITICAL_RATE
    | P_TRANSFER_DAMAGE_PC ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_transfer_damage_pc(static_cast<double>(grammar::SemanticInt64($3))));
      }
    | P_TRANSFER_DAMAGE_TO ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_transfer_damage_to(static_cast<double>(grammar::SemanticInt64($3))));
      }
    | P_SOUL_EATING ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_soul_eating(grammar::SemanticInt32($3), grammar::SemanticInt32($5)));
      }
    | P_BLOCK_ATTACK
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_block_attack);
      }
    | P_DISARM ';' SLOT_VALUE
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_disarm(static_cast<SlotType>(grammar::SemanticInt32($3))));
      }
    | P_TRANSFORM ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_transform(grammar::SemanticInt32($3)));
      }
    | P_TRANSFORM_HANGOVER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_transform_hangover);
      }
    | P_DEFENCE_CRITICAL_RATE ';' '{' identifier_def_2 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_defence_critical_rate(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetArmorTypeArray(m_ArmorTypeArray);
          publishPump(effect);
      }
    | P_DEFENCE_CRITICAL_DAMAGE ';' '{' identifier_def_2 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_defence_critical_damage(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetArmorTypeArray(m_ArmorTypeArray);
          publishPump(effect);
      }
    | P_RESIST_DD_MAGIC ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_resist_dd_magic(
              static_cast<double>(grammar::SemanticInt32($3)),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_PHYSICAL_ATTACK ';' '{' identifier_def '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_physical_attack(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetAttackTypeArray(m_AttackTypeArray);
          publishPump(effect);
      }
    | P_PHYSICAL_ATTACK_BY_DEFENCE ';' '{' identifier_def '}' ';' PREDEFINED ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const int ignoreDefence = grammar::SemanticInt32($7);
          if (ignoreDefence < 0 || ignoreDefence > 2) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"p_physical_attack_by_defence's 2nd paramater should be 0, 1, or 2. Line = [%d]",
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"p_physical_attack_by_defence's 2nd paramater should be 0, 1, or 2. Line = [%d]",
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: p_physical_attack_by_defence ignore-defence type %d is outside 0..2; the "
                  "effect is omitted.",
                  static_cast<long long>(m_lexer.line) + 1, ignoreDefence
              );
              break;
          }
          auto* const effect = new CSkillEffect_p_physical_attack_by_defence(ignoreDefence, grammar::SemanticDouble($9));
          effect->SetAttackTypeArray(m_AttackTypeArray);
          publishPump(effect);
      }
    | P_PHYSICAL_DEFENCE ';' '{' identifier_def_2 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_physical_defence(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetArmorTypeArray(m_ArmorTypeArray);
          publishPump(effect);
      }
    | P_HP_REGEN ';' '{' identifier_def_2 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_hp_regen(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetArmorTypeArray(m_ArmorTypeArray);
          publishPump(effect);
      }
    | P_MP_REGEN ';' '{' identifier_def_2 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_mp_regen(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetArmorTypeArray(m_ArmorTypeArray);
          publishPump(effect);
      }
    | P_MP_REGEN_ADD ';' '{' identifier_def_3 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_mp_regen_add(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetAdditionalArmorTypeArray(m_AdditionalArmorTypeArray);
          publishPump(effect);
      }
    | P_SPEED ';' '{' identifier_def_2 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_speed(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetArmorTypeArray(m_ArmorTypeArray);
          publishPump(effect);
      }
    | P_SPEED_OUT_OF_NIGHT ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_speed_out_of_night(grammar::SemanticDouble($3)));
      }
    | P_SPEED_OUT_OF_FIGHT ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_speed_out_of_fight(grammar::SemanticDouble($3)));
      }
    | P_STAT_UP_AT_NIGHT ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_stat_up_at_night(
              grammar::SemanticInt32($3), grammar::SemanticInt32($5), grammar::SemanticInt32($7),
              grammar::SemanticInt32($9), grammar::SemanticInt32($11), grammar::SemanticInt32($13)
          ));
      }
    | P_ABNORMAL_SHIELD ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_abnormal_shield(grammar::SemanticInt32($3)));
      }
    | P_CREATE_ITEM ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_create_item(grammar::SemanticInt32($3)));
      }
    | P_CREATE_COMMON_ITEM ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_create_common_item(grammar::SemanticInt32($3)));
      }
    | P_ATTACK_SPEED ';' '{' identifier_def_2 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_attack_speed(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetArmorTypeArray(m_ArmorTypeArray);
          publishPump(effect);
      }
    | P_MAGIC_SPEED ';' '{' identifier_def_2 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_magic_speed(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetArmorTypeArray(m_ArmorTypeArray);
          publishPump(effect);
      }
    | P_MAGIC_SPEED_BY_MAGIC_TYPE ';' '{' identifier_def_2 '}' ';' '{' n78 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          std::set<int> magicTypes;
          for (const int magicType : m_sMagicType) {
              magicTypes.insert(magicType);
          }
          auto* const effect = new CSkillEffect_p_magic_speed_by_magic_type(
              grammar::SemanticDouble($11), static_cast<SkillFxChangeType>(grammar::SemanticInt32($13)),
              std::move(magicTypes)
          );
          effect->SetArmorTypeArray(m_ArmorTypeArray);
          publishPump(effect);
      }
    | P_CHARGE_VITALITY ';' predefined_def
      {
          m_pSkillInfo->PushPumpEffect(
              new CSkillEffect_p_charge_vitality(runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($3))),
              CSkillInfo::EAT_SUCCESS
          );
      }
    | P_REMOVE_EQUIP_PENALTY ';' IDENTIFIER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto crystalTypeAt = [this](const grammar::SemanticBits value) {
              const std::wstring name = Utf16ToWide(SemanticWord(value));
              CrystalType crystalType = CItem::GetCrysalType(name.c_str());
              if (crystalType == CRT_INVALID) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"CrystalType at [%d] [%s] isn't defined. line = [%d]",
                  //     m_lexer.line + 1,
                  //     name.c_str(),
                  //     m_nLineNumber)
                  // L2_LOG_ERROR(Log,
                  //     L"CrystalType at [%d] [%s] isn't defined. line = [%d]",
                  //     m_lexer.line + 1,
                  //     name.c_str(),
                  //     m_nLineNumber)
                  L2_LOG_ERROR(
                      Log, "skilldata.txt line %lld: unknown crystal type '%s'; using CRT_NONE.",
                      static_cast<long long>(m_lexer.line) + 1, WideToUtf8(name).c_str()
                  );
                  crystalType = CRT_NONE;
              }
              return crystalType;
          };
          auto* const effect = new CSkillEffect_p_remove_equip_penalty;
          effect->eCrystalType = crystalTypeAt($3);
          publishPump(effect);
      }
    | P_CRYSTALLIZE ';' IDENTIFIER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto crystalTypeAt = [this](const grammar::SemanticBits value) {
              const std::wstring name = Utf16ToWide(SemanticWord(value));
              CrystalType crystalType = CItem::GetCrysalType(name.c_str());
              if (crystalType == CRT_INVALID) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"CrystalType at [%d] [%s] isn't defined. line = [%d]",
                  //     m_lexer.line + 1,
                  //     name.c_str(),
                  //     m_nLineNumber)
                  // L2_LOG_ERROR(Log,
                  //     L"CrystalType at [%d] [%s] isn't defined. line = [%d]",
                  //     m_lexer.line + 1,
                  //     name.c_str(),
                  //     m_nLineNumber)
                  L2_LOG_ERROR(
                      Log, "skilldata.txt line %lld: unknown crystal type '%s'; using CRT_NONE.",
                      static_cast<long long>(m_lexer.line) + 1, WideToUtf8(name).c_str()
                  );
                  crystalType = CRT_NONE;
              }
              return crystalType;
          };
          auto* const effect = new CSkillEffect_p_crystallize;
          effect->eCrystalType = crystalTypeAt($3);
          publishPump(effect);
      }
    | P_AVOID ';' '{' identifier_def_2 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_avoid(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetArmorTypeArray(m_ArmorTypeArray);
          publishPump(effect);
      }
    | P_LUCK
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_luck);
      }
    | P_MAGICAL_DEFENCE ';' '{' identifier_def_2 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_magical_defence(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetArmorTypeArray(m_ArmorTypeArray);
          publishPump(effect);
      }
    | P_MAGICAL_DEFENCE_ADD ';' '{' identifier_def_3 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_magical_defence_add(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetAdditionalArmorTypeArray(m_AdditionalArmorTypeArray);
          publishPump(effect);
      }
    | P_PHYSICAL_ATTACK_BY_MATERIAL ';' '{' n41 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_physical_attack_by_material(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->m_MaterialType = m_vMaterial.empty() ? MATT_MITHRIL : runtime::CheckedVectorAt(m_vMaterial, 0);
          publishPump(effect);
      }
    | P_PHYSICAL_DEFENCE_BY_MATERIAL ';' '{' n41 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_physical_defence_by_material(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->m_MaterialType = m_vMaterial.empty() ? MATT_MITHRIL : runtime::CheckedVectorAt(m_vMaterial, 0);
          publishPump(effect);
      }
    | P_ATTACK_ATTRIBUTE ';' ATTR_VALUE ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_attack_attribute(
              static_cast<AttributeTypeEnum>(grammar::SemanticInt32($3)), grammar::SemanticDouble($5)
          ));
      }
    | P_ATTACK_ATTRIBUTE_ADD ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_attack_attribute_add(grammar::SemanticInt32($3)));
      }
    | P_PASSIVE_BONUS_SKILL ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_passive_bonus_skill(
              grammar::SemanticInt32($3), grammar::SemanticInt32($5), grammar::SemanticInt32($7),
              grammar::SemanticInt32($9)
          ));
      }
    | P_DEFENCE_ATTRIBUTE ';' ATTR_VALUE ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_defence_attribute(
              static_cast<AttributeTypeEnum>(grammar::SemanticInt32($3)), grammar::SemanticDouble($5)
          ));
      }
    | P_ATTACK_TRAIT ';' '{' identifier_def '}' ';' TRAIT_VALUE ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_attack_trait(
              static_cast<TraitTypeEnum>(grammar::SemanticInt32($7)), grammar::SemanticDouble($9)
          );
          effect->SetAttackTypeArray(m_AttackTypeArray);
          publishPump(effect);
      }
    | P_DEFENCE_TRAIT ';' TRAIT_VALUE ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const double value = grammar::SemanticDouble($5);
          if (value < -100.0 || value > 100.0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Trait Value [%d] should be between -100 ~ 100. line = [%d]",
              //     value,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Trait Value [%d] should be between -100 ~ 100. line = [%d]",
              //     value,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: p_defence_trait value %.17g is outside -100..100; the effect retains this "
                  "value.",
                  static_cast<long long>(m_lexer.line) + 1, value
              );
          }
          publishPump(new CSkillEffect_p_defence_trait(static_cast<TraitTypeEnum>(grammar::SemanticInt32($3)), value));
      }
    | P_BLOCK_MOVE
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_block_move);
      }
    | P_BLOCK_ACT
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_block_act);
          m_pSkillInfo->PushEffect(new CSkillEffect_i_block_act, m_EffectActivateTiming);
      }
    | P_BLOCK_CONTROLL
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_block_controll);
      }
    | P_BLOCK_SPELL
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_block_spell);
      }
    | P_BLOCK_GETDAMAGE ';' IDENTIFIER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          std::wstring blockType = Utf16ToWide(SemanticWord($3));
          publishPump(new CSkillEffect_p_block_getdamage(blockType.data()));
      }
    | P_FEAR
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_fear);
      }
    | P_MAX_HP ';' '{' identifier_def_2 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectAddBonusByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_max_hp(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9)), false
          ));
      }
    | P_MAX_MP ';' '{' identifier_def_2 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectAddBonusByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_max_mp(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9)), false
          ));
      }
    | P_MAX_CP ';' '{' identifier_def_2 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectAddBonusByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_max_cp(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9)), false
          ));
      }
    | P_MAX_MP_ADD ';' '{' identifier_def_3 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_max_mp_add(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetAdditionalArmorTypeArray(m_AdditionalArmorTypeArray);
          publishPump(effect);
      }
    | P_MAX_HP ';' '{' identifier_def_2 '}' ';' predefined_def ';' PER_GROUP ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectAddBonusByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_max_hp(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9)),
              grammar::SemanticInt64($11) == 1
          ));
      }
    | P_MAX_MP ';' '{' identifier_def_2 '}' ';' predefined_def ';' PER_GROUP ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectAddBonusByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_max_mp(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9)),
              grammar::SemanticInt64($11) == 1
          ));
      }
    | P_MAX_CP ';' '{' identifier_def_2 '}' ';' predefined_def ';' PER_GROUP ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_max_cp(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9)),
              grammar::SemanticInt64($11) == 1
          );
          effect->SetArmorTypeArray(m_ArmorTypeArray);
          publishPump(effect);
      }
    | P_HIT ';' '{' identifier_def '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_hit(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetAttackTypeArray(m_AttackTypeArray);
          publishPump(effect);
      }
    | P_MAGICAL_ATTACK ';' '{' identifier_def_2 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_magical_attack(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetArmorTypeArray(m_ArmorTypeArray);
          publishPump(effect);
      }
    | P_MAGICAL_ATTACK_ADD ';' '{' identifier_def_3 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_magical_attack_add(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetAdditionalArmorTypeArray(m_AdditionalArmorTypeArray);
          publishPump(effect);
      }
    | P_ATTACK_RANGE ';' '{' identifier_def '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_attack_range(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetAttackTypeArray(m_AttackTypeArray);
          publishPump(effect);
      }
    | P_CRITICAL_RATE ';' '{' identifier_def '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_critical_rate(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetAttackTypeArray(m_AttackTypeArray);
          publishPump(effect);
      }
    | P_PHYSICAL_CRITICAL_RATE_MAXUP ';' '{' identifier_def '}' ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_physical_critical_rate_maxup(grammar::SemanticDouble($7), SFCT_NONE);
          effect->SetAttackTypeArray(m_AttackTypeArray);
          publishPump(effect);
      }
    | P_CRITICAL_DAMAGE ';' '{' identifier_def '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_critical_damage(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetAttackTypeArray(m_AttackTypeArray);
          publishPump(effect);
      }
    | P_REUSE_DELAY ';' PREDEFINED ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_reuse_delay(
              grammar::SemanticInt32($3), grammar::SemanticDouble($5),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          ));
      }
    | P_REDUCE_CANCEL ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_reduce_cancel(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_ATTACK_SPEED_BY_WEAPON ';' '{' identifier_def '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_attack_speed_by_weapon(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetAttackTypeArray(m_AttackTypeArray);
          publishPump(effect);
      }
    | P_MAGIC_SPEED_BY_WEAPON ';' '{' identifier_def '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_magic_speed_by_weapon(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetAttackTypeArray(m_AttackTypeArray);
          publishPump(effect);
      }
    | P_SHIELD_DEFENCE_RATE ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_shield_defence_rate(grammar::SemanticDouble($3)));
      }
    | P_HP_REGEN_BY_MOVE_MODE ';' IDENTIFIER ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto moveModeAt = [](const grammar::SemanticBits slot) {
              const char16_t* const value = SemanticWord(slot);
              if (TargetWordEquals(value, u"sit")) {
                  return MVMD_SIT;
              }
              if (TargetWordEquals(value, u"stand")) {
                  return MVMD_STAND;
              }
              if (TargetWordEquals(value, u"walk")) {
                  return MVMD_WALK;
              }
              if (TargetWordEquals(value, u"run")) {
                  return MVMD_RUN;
              }
              return MVMD_MAX;
          };
          auto* const effect = new CSkillEffect_p_hp_regen_by_move_mode(
              grammar::SemanticDouble($5), static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          );
          effect->eMoveModeType = moveModeAt($3);
          publishPump(effect);
      }
    | P_MP_REGEN_BY_MOVE_MODE ';' IDENTIFIER ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto moveModeAt = [](const grammar::SemanticBits slot) {
              const char16_t* const value = SemanticWord(slot);
              if (TargetWordEquals(value, u"sit")) {
                  return MVMD_SIT;
              }
              if (TargetWordEquals(value, u"stand")) {
                  return MVMD_STAND;
              }
              if (TargetWordEquals(value, u"walk")) {
                  return MVMD_WALK;
              }
              if (TargetWordEquals(value, u"run")) {
                  return MVMD_RUN;
              }
              return MVMD_MAX;
          };
          auto* const effect = new CSkillEffect_p_mp_regen_by_move_mode(
              grammar::SemanticDouble($5), static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          );
          effect->eMoveModeType = moveModeAt($3);
          publishPump(effect);
      }
    | P_AVOID_BY_MOVE_MODE ';' IDENTIFIER ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto moveModeAt = [](const grammar::SemanticBits slot) {
              const char16_t* const value = SemanticWord(slot);
              if (TargetWordEquals(value, u"sit")) {
                  return MVMD_SIT;
              }
              if (TargetWordEquals(value, u"stand")) {
                  return MVMD_STAND;
              }
              if (TargetWordEquals(value, u"walk")) {
                  return MVMD_WALK;
              }
              if (TargetWordEquals(value, u"run")) {
                  return MVMD_RUN;
              }
              return MVMD_MAX;
          };
          auto* const effect = new CSkillEffect_p_avoid_by_move_mode(
              grammar::SemanticDouble($5), static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          );
          effect->eMoveModeType = moveModeAt($3);
          publishPump(effect);
      }
    | P_SAFE_FALL_HEIGHT ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_safe_fall_height(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_BREATH ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_breath(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_DAMAGE_SHIELD ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_damage_shield(grammar::SemanticDouble($3)));
      }
    | P_PASSIVE
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_passive);
      }
    | P_AVOID_AGRO ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_avoid_agro(grammar::SemanticInt32($3)));
      }
    | P_WEIGHT_LIMIT ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_weight_limit(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_TRADE ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_trade(
              grammar::SemanticInt32($3), grammar::SemanticInt32($5), grammar::SemanticInt32($7),
              grammar::SemanticInt32($9), grammar::SemanticInt32($11), grammar::SemanticInt32($13)
          ));
      }
    | P_SET_COLLECTED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_set_collected);
      }
    | P_SET_CLOAK_SLOT
    | P_CUBIC_MASTERY ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_cubic_mastery(grammar::SemanticInt32($3)));
      }
    | P_WEIGHT_PENALTY ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_weight_penalty(grammar::SemanticDouble($3), SFCT_DIFF));
      }
    | P_MANA_CHARGE ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_mana_charge(grammar::SemanticInt32($3)));
      }
    | P_TRANSFER_DAMAGE_SUMMON ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_transfer_damage_summon(grammar::SemanticInt32($3)));
      }
    | P_HEAL_EFFECT ';' '{' identifier_def_2 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_heal_effect(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetArmorTypeArray(m_ArmorTypeArray);
          publishPump(effect);
      }
    | P_HEAL_EFFECT_ADD ';' '{' identifier_def_3 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_heal_effect_add(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetAdditionalArmorTypeArray(m_AdditionalArmorTypeArray);
          publishPump(effect);
      }
    | P_AREA_DAMAGE ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_area_damage(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_HIT_AT_NIGHT ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_hit_at_night(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_VAMPIRIC_ATTACK ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_vampiric_attack(grammar::SemanticDouble($3), 100.0));
      }
    | P_VAMPIRIC_ATTACK ';' predefined_def ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_vampiric_attack(grammar::SemanticDouble($3), grammar::SemanticDouble($5)));
      }
    | P_VAMPIRIC_DEFENCE ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_vampiric_defence(grammar::SemanticDouble($3)));
      }
    | P_CRITICAL_RATE_BY_HP1 ';' '{' identifier_def '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto publishWeaponPump = [this, &publishPump](CSkillEffectByWeaponType* effect) {
              effect->SetAttackTypeArray(m_AttackTypeArray);
              publishPump(effect);
          };
          publishWeaponPump(new CSkillEffect_p_critical_rate_by_hp1(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_CRITICAL_RATE_BY_HP2 ';' '{' identifier_def '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto publishWeaponPump = [this, &publishPump](CSkillEffectByWeaponType* effect) {
              effect->SetAttackTypeArray(m_AttackTypeArray);
              publishPump(effect);
          };
          publishWeaponPump(new CSkillEffect_p_critical_rate_by_hp2(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_PHYSICAL_ATTACK_BY_HP1 ';' '{' identifier_def '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto publishWeaponPump = [this, &publishPump](CSkillEffectByWeaponType* effect) {
              effect->SetAttackTypeArray(m_AttackTypeArray);
              publishPump(effect);
          };
          publishWeaponPump(new CSkillEffect_p_physical_attack_by_hp1(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_PHYSICAL_ATTACK_BY_HP2 ';' '{' identifier_def '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto publishWeaponPump = [this, &publishPump](CSkillEffectByWeaponType* effect) {
              effect->SetAttackTypeArray(m_AttackTypeArray);
              publishPump(effect);
          };
          publishWeaponPump(new CSkillEffect_p_physical_attack_by_hp2(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_PHYSICAL_DEFENCE_BY_HP1 ';' '{' identifier_def_2 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectAddBonusByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_physical_defence_by_hp1(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_PHYSICAL_DEFENCE_BY_HP2 ';' '{' identifier_def_2 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectAddBonusByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_physical_defence_by_hp2(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_AVOID_RATE_BY_HP1 ';' '{' identifier_def_2 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectAddBonusByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_avoid_rate_by_hp1(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_AVOID_RATE_BY_HP2 ';' '{' identifier_def_2 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectAddBonusByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_avoid_rate_by_hp2(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_ATTACK_SPEED_BY_HP1 ';' '{' identifier_def '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto publishWeaponPump = [this, &publishPump](CSkillEffectByWeaponType* effect) {
              effect->SetAttackTypeArray(m_AttackTypeArray);
              publishPump(effect);
          };
          publishWeaponPump(new CSkillEffect_p_attack_speed_by_hp1(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_ATTACK_SPEED_BY_HP2 ';' '{' identifier_def '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto publishWeaponPump = [this, &publishPump](CSkillEffectByWeaponType* effect) {
              effect->SetAttackTypeArray(m_AttackTypeArray);
              publishPump(effect);
          };
          publishWeaponPump(new CSkillEffect_p_attack_speed_by_hp2(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_FATAL_BLOW_RATE ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_fatal_blow_rate(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_DEFENCE_FATAL_BLOW_RATE ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_defence_fatal_blow_rate(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_CRITICAL_RATE_POSITION_BONUS ';' FRONT_GROUP ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_critical_rate_position_bonus(
              static_cast<SkillFxPositionType>(grammar::SemanticInt32($3)), grammar::SemanticDouble($5),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          ));
      }
    | P_REDUCE_DROP_PENALTY
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_reduce_drop_penalty(ENT_PLAYER, 0.0, 0.0));
      }
    | P_REDUCE_DROP_PENALTY ';' PK_GROUP ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_reduce_drop_penalty(
              static_cast<EnemyType>(grammar::SemanticInt32($3)), static_cast<double>(grammar::SemanticInt64($5)),
              static_cast<double>(grammar::SemanticInt64($7))
          ));
      }
    | P_PHYSICAL_SHIELD_DEFENCE_ANGLE_ALL
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_physical_shield_defence_angle_all);
      }
    | P_MAGIC_MP_COST ';' PREDEFINED ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_magic_mp_cost(
              grammar::SemanticInt32($3), static_cast<double>(grammar::SemanticInt64($5)),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          ));
      }
    | P_AFTER_MP_COST ';' PREDEFINED ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_after_mp_cost(
              grammar::SemanticInt32($3), static_cast<double>(grammar::SemanticInt64($5)),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          ));
      }
    | P_MAGIC_CRITICAL_RATE ';' '{' identifier_def '}' ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_magic_critical_rate(
              static_cast<double>(grammar::SemanticInt64($7)),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetAttackTypeArray(m_AttackTypeArray);
          publishPump(effect);
      }
    | P_PVE_PHYSICAL_ATTACK_DMG_BONUS ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_pve_physical_attack_dmg_bonus(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5)), ENT_ALL
          ));
      }
    | P_PVE_PHYSICAL_SKILL_DMG_BONUS ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_pve_physical_skill_dmg_bonus(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5)), ENT_ALL
          ));
      }
    | P_PVE_MAGICAL_SKILL_DMG_BONUS ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_pve_magical_skill_dmg_bonus(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5)), ENT_ALL
          ));
      }
    | P_PVE_PHYSICAL_ATTACK_DMG_BONUS ';' PK_GROUP ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_pve_physical_attack_dmg_bonus(
              grammar::SemanticDouble($5), static_cast<SkillFxChangeType>(grammar::SemanticInt32($7)),
              static_cast<EnemyType>(grammar::SemanticInt32($3))
          ));
      }
    | P_PVE_PHYSICAL_SKILL_DMG_BONUS ';' PK_GROUP ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_pve_physical_skill_dmg_bonus(
              grammar::SemanticDouble($5), static_cast<SkillFxChangeType>(grammar::SemanticInt32($7)),
              static_cast<EnemyType>(grammar::SemanticInt32($3))
          ));
      }
    | P_PVE_MAGICAL_SKILL_DMG_BONUS ';' PK_GROUP ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_pve_magical_skill_dmg_bonus(
              grammar::SemanticDouble($5), static_cast<SkillFxChangeType>(grammar::SemanticInt32($7)),
              static_cast<EnemyType>(grammar::SemanticInt32($3))
          ));
      }
    | P_PVP_PHYSICAL_ATTACK_DMG_BONUS ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_pvp_physical_attack_dmg_bonus(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_PVP_PHYSICAL_SKILL_DMG_BONUS ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_pvp_physical_skill_dmg_bonus(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_PVP_MAGICAL_SKILL_DMG_BONUS ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_pvp_magical_skill_dmg_bonus(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_PVP_MAGICAL_SKILL_DEFENCE_BONUS ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_pvp_magical_skill_defence_bonus(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_PVP_PHYSICAL_ATTACK_DEFENCE_BONUS ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_pvp_physical_attack_defence_bonus(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_PVP_PHYSICAL_SKILL_DEFENCE_BONUS ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_pvp_physical_skill_defence_bonus(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_PHYSICAL_ARMOR_HIT ';' '{' identifier_def_2 '}' ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_physical_armor_hit(
              static_cast<double>(grammar::SemanticInt64($7)),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetArmorTypeArray(m_ArmorTypeArray);
          publishPump(effect);
      }
    | P_PHYSICAL_SHIELD_DEFENCE ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_physical_shield_defence(
              static_cast<double>(grammar::SemanticInt64($3)),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_PHYSICAL_POLARM_TARGET_SINGLE
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_physical_polarm_target_single);
      }
    | P_FISHING_MASTERY ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_fishing_mastery(grammar::SemanticDouble($3)));
      }
    | P_ABNORMAL_RATE_LIMIT
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_abnormal_rate_limit);
      }
    | P_ABNORMAL_REMOVE_BY_HIT ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(
              new CSkillEffect_p_abnormal_remove_by_hit(grammar::SemanticInt32($3), grammar::SemanticInt32($5))
          );
      }
    | P_ABNORMAL_REMOVE_BY_DMG ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_abnormal_remove_by_dmg(grammar::SemanticInt32($3)));
      }
    | P_BLOCK_SKILL_PHYSICAL
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_block_skill_physical);
      }
    | P_BLOCK_SKILL_SPECIAL
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_block_skill_special);
      }
    | P_BLOCK_BUFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_block_buff);
      }
    | P_BLOCK_ADD_SLOT ';' '{' n66 '}'
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const names = grammar::SemanticPointer<std::vector<char16_t*>>($4);
          auto* const effect = new CSkillEffect_p_block_add_slot;
          for (const char16_t* const name : *names) {
              effect->m_AbnormalSlots.push_back(FindAbnormalByName(name, m_lexer.line + 1));
          }
          publishPump(effect);
          delete names;
      }
    | P_BLOCK_DEBUFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_block_debuff);
      }
    | P_BLOCK_DEBUFF ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_block_debuff(grammar::SemanticDouble($3)));
      }
    | P_BLOCK_BUFF_SLOT ';' '{' n66 '}'
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto fillAbnormalSlots = [&](CSkillEffect_p_block_buff_slot& effect) {
              auto* const names = grammar::SemanticPointer<std::vector<char16_t*>>($4);
              for (const char16_t* const name : *names) {
                  effect.m_AbnormalSlots.push_back(FindAbnormalByName(name, m_lexer.line + 1));
              }
              delete names;
          };
          auto* const effect = new CSkillEffect_p_block_buff_slot;
          fillAbnormalSlots(*effect);
          publishPump(effect);
      }
    | P_BLOCK_BUFF_SLOT ';' NAME ';' '{' n66 '}'
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto fillAbnormalSlots = [&](CSkillEffect_p_block_buff_slot& effect) {
              auto* const names = grammar::SemanticPointer<std::vector<char16_t*>>($6);
              for (const char16_t* const name : *names) {
                  effect.m_AbnormalSlots.push_back(FindAbnormalByName(name, m_lexer.line + 1));
              }
              delete names;
          };
          auto* const effect = new CSkillEffect_p_block_buff_slot;
          effect->m_bNotDispel = TargetWordEquals(SemanticWord($3), u"not_dispel");
          fillAbnormalSlots(*effect);
          publishPump(effect);
      }
    | P_BLOCK_SKILL ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_block_skill(static_cast<SkillIsMagicType>(grammar::SemanticInt32($3))));
      }
    | P_REFLECT_SKILL ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_reflect_skill(
              static_cast<double>(grammar::SemanticInt64($3)), static_cast<double>(grammar::SemanticInt64($5)),
              static_cast<double>(grammar::SemanticInt64($7)), static_cast<double>(grammar::SemanticInt64($9)),
              static_cast<double>(grammar::SemanticInt64($11)), static_cast<double>(grammar::SemanticInt64($13))
          ));
      }
    | P_SKILL_CRITICAL ';' '{' identifier_def '}' ';' IDENTIFIER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto parameterTypeAt = [](const grammar::SemanticBits slot) {
              const char16_t* const value = SemanticWord(slot);
              if (TargetWordEquals(value, u"str")) {
                  return STE_STR;
              }
              if (TargetWordEquals(value, u"dex")) {
                  return STE_DEX;
              }
              if (TargetWordEquals(value, u"con")) {
                  return STE_CON;
              }
              if (TargetWordEquals(value, u"int")) {
                  return STE_INT;
              }
              if (TargetWordEquals(value, u"men")) {
                  return STE_MEN;
              }
              if (TargetWordEquals(value, u"wit")) {
                  return STE_WIT;
              }
              return STE_MAX;
          };
          auto* const effect = new CSkillEffect_p_skill_critical;
          effect->m_eParamType = parameterTypeAt($7);
          effect->SetAttackTypeArray(m_AttackTypeArray);
          publishPump(effect);
      }
    | P_SKILL_CRITICAL_PROBABILITY ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_skill_critical_probability(
              grammar::SemanticInt32($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_CRITICAL_DAMAGE_POSITION ';' FRONT_GROUP ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_critical_damage_position(
              static_cast<SkillFxPositionType>(grammar::SemanticInt32($3)), grammar::SemanticInt32($5),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          ));
      }
    | P_RESIST_DISPEL_BY_CATEGORY ';' IDENTIFIER ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto debuffTypeAt = [](const grammar::SemanticBits slot) {
              const char16_t* const value = SemanticWord(slot);
              if (TargetWordEquals(value, u"slot_buff")) {
                  return DB_GOOD;
              }
              if (TargetWordEquals(value, u"slot_debuff")) {
                  return DB_BAD;
              }
              return DB_MAX;
          };
          auto* const effect = new CSkillEffect_p_resist_dispel_by_category(
              grammar::SemanticInt32($5), static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          );
          effect->eDeBuffType = debuffTypeAt($3);
          publishPump(effect);
      }
    | P_RESIST_ABNORMAL_BY_CATEGORY ';' IDENTIFIER ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto debuffTypeAt = [](const grammar::SemanticBits slot) {
              const char16_t* const value = SemanticWord(slot);
              if (TargetWordEquals(value, u"slot_buff")) {
                  return DB_GOOD;
              }
              if (TargetWordEquals(value, u"slot_debuff")) {
                  return DB_BAD;
              }
              return DB_MAX;
          };
          auto* const effect = new CSkillEffect_p_resist_abnormal_by_category(
              grammar::SemanticInt32($5), static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          );
          effect->eDeBuffType = debuffTypeAt($3);
          publishPump(effect);
      }
    | P_PRESERVE_ABNORMAL ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_preserve_abnormal(grammar::SemanticInt32($3)));
      }
    | P_PRESERVE_ABNORMAL
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_preserve_abnormal(0));
      }
    | P_ENLARGE_STORAGE ';' INVENTORY_NORMAL_GROUP ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_enlarge_storage(
              static_cast<SkillEnlargeStorageType>(grammar::SemanticInt32($3)), grammar::SemanticInt32($5)
          ));
      }
    | P_ENLARGE_ABNORMAL_SLOT ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_enlarge_abnormal_slot(grammar::SemanticInt32($3)));
      }
    | P_HIT_NUMBER ';' '{' identifier_def '}' ';' PREDEFINED ';' PER_GROUP
      {
          auto* const effect = new CSkillEffect_p_hit_number(
              grammar::SemanticInt32($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetAttackTypeArray(m_AttackTypeArray);
          m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
      }
    | P_BETRAY
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_betray);
      }
    | P_CP_REGEN ';' '{' identifier_def_2 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_cp_regen(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetArmorTypeArray(m_ArmorTypeArray);
          publishPump(effect);
      }
    | P_CP_REGEN_BY_MOVE_MODE ';' IDENTIFIER ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto moveModeAt = [](const grammar::SemanticBits slot) {
              const char16_t* const value = SemanticWord(slot);
              if (TargetWordEquals(value, u"sit")) {
                  return MVMD_SIT;
              }
              if (TargetWordEquals(value, u"stand")) {
                  return MVMD_STAND;
              }
              if (TargetWordEquals(value, u"walk")) {
                  return MVMD_WALK;
              }
              if (TargetWordEquals(value, u"run")) {
                  return MVMD_RUN;
              }
              return MVMD_MAX;
          };
          auto* const effect = new CSkillEffect_p_cp_regen_by_move_mode(
              grammar::SemanticDouble($5), static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          );
          effect->eMoveModeType = moveModeAt($3);
          publishPump(effect);
      }
    | P_CHANNEL_CLAN
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_channel_clan);
      }
    | P_CHANGE_FISHING_MASTERY ';' PREDEFINED ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(
              new CSkillEffect_p_change_fishing_mastery(grammar::SemanticInt32($3), grammar::SemanticDouble($5))
          );
      }
    | P_RESURRECTION_SPECIAL ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED ';' NAME ';' '{' n63 '}'
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_resurrection_special(
              grammar::SemanticInt32($3), grammar::SemanticInt32($5), grammar::SemanticInt32($7),
              SemanticWord($9), grammar::SemanticPointer<std::vector<std::int64_t>>($12)
          ));
      }
    | P_RESURRECTION_SPECIAL ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED ';' NAME
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_resurrection_special(
              grammar::SemanticInt32($3), grammar::SemanticInt32($5), grammar::SemanticInt32($7),
              SemanticWord($9)
          ));
      }
    | P_AVOID_SKILL ';' PREDEFINED ';' PREDEFINED
      {
          const int action = 679;
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto publishMagicTypeEffect = [&](int actionId, [[maybe_unused]] const wchar_t* error, auto makeEffect) {
              const int magicType = grammar::SemanticInt32($3);
              if (static_cast<unsigned int>(magicType) >= 2U) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     error,
                  //     magicType,
                  //     m_nLineNumber)
                  // L2_LOG_ERROR(Log,
                  //     error,
                  //     magicType,
                  //     m_nLineNumber)
                  L2_LOG_ERROR(
                      Log, "skilldata.txt line %lld: %s magic type %d is outside 0..1; the effect is omitted.",
                      static_cast<long long>(m_lexer.line) + 1,
                      actionId == 679       ? "p_avoid_skill"
                          : actionId == 680 ? "p_counter_skill"
                                            : "p_avoid_counter_skill",
                      magicType
                  );
                  return;
              }
              publishPump(makeEffect(magicType, grammar::SemanticInt32($5)));
              (void)actionId;
          };
          publishMagicTypeEffect(
              action, L"Magic type of p_avoid_skill should be 0 or 1, not %d. Line = [%d]",
              [](int magicType, int probability) -> CSkillEffect* {
                  return new CSkillEffect_p_avoid_skill(magicType, probability);
              }
          );
      }
    | P_COUNTER_SKILL ';' PREDEFINED ';' PREDEFINED
      {
          const int action = 680;
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto publishMagicTypeEffect = [&](int actionId, [[maybe_unused]] const wchar_t* error, auto makeEffect) {
              const int magicType = grammar::SemanticInt32($3);
              if (static_cast<unsigned int>(magicType) >= 2U) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     error,
                  //     magicType,
                  //     m_nLineNumber)
                  // L2_LOG_ERROR(Log,
                  //     error,
                  //     magicType,
                  //     m_nLineNumber)
                  L2_LOG_ERROR(
                      Log, "skilldata.txt line %lld: %s magic type %d is outside 0..1; the effect is omitted.",
                      static_cast<long long>(m_lexer.line) + 1,
                      actionId == 679       ? "p_avoid_skill"
                          : actionId == 680 ? "p_counter_skill"
                                            : "p_avoid_counter_skill",
                      magicType
                  );
                  return;
              }
              publishPump(makeEffect(magicType, grammar::SemanticInt32($5)));
              (void)actionId;
          };
          publishMagicTypeEffect(
              action, L"Magic type of p_counter_skill should be 0 or 1, not %d. Line = [%d]",
              [](int magicType, int probability) -> CSkillEffect* {
                  return new CSkillEffect_p_counter_skill(magicType, probability);
              }
          );
      }
    | P_AVOID_COUNTER_SKILL ';' PREDEFINED ';' PREDEFINED
      {
          const int action = 681;
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto publishMagicTypeEffect = [&](int actionId, [[maybe_unused]] const wchar_t* error, auto makeEffect) {
              const int magicType = grammar::SemanticInt32($3);
              if (static_cast<unsigned int>(magicType) >= 2U) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     error,
                  //     magicType,
                  //     m_nLineNumber)
                  // L2_LOG_ERROR(Log,
                  //     error,
                  //     magicType,
                  //     m_nLineNumber)
                  L2_LOG_ERROR(
                      Log, "skilldata.txt line %lld: %s magic type %d is outside 0..1; the effect is omitted.",
                      static_cast<long long>(m_lexer.line) + 1,
                      actionId == 679       ? "p_avoid_skill"
                          : actionId == 680 ? "p_counter_skill"
                                            : "p_avoid_counter_skill",
                      magicType
                  );
                  return;
              }
              publishPump(makeEffect(magicType, grammar::SemanticInt32($5)));
              (void)actionId;
          };
          publishMagicTypeEffect(
              action, L"Magic type of p_avoid_counter_skill should be 0 or 1, not %d. Line = [%d]",
              [](int magicType, int probability) -> CSkillEffect* {
                  return new CSkillEffect_p_avoid_counter_skill(magicType, probability);
              }
          );
      }
    | P_PK_PROTECT
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_pk_protect);
      }
    | P_EXPAND_DECO_SLOT ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_expand_deco_slot(grammar::SemanticInt32($3)));
      }
    | P_EXPAND_JEWEL_SLOT ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_expand_jewel_slot(grammar::SemanticInt32($3)));
      }
    | P_EXPAND_AGATHION_MAIN_SLOT ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_expand_agathion_main_slot(grammar::SemanticInt32($3)));
      }
    | P_EXPAND_AGATHION_SUB_SLOT ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_expand_agathion_sub_slot(grammar::SemanticInt32($3)));
      }
    | P_TARGET_ME
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_target_me);
      }
    | P_LIMIT_HP ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_limit_hp(
              grammar::SemanticInt32($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_LIMIT_MP ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_limit_mp(
              grammar::SemanticInt32($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_LIMIT_CP ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_limit_cp(
              grammar::SemanticInt32($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_ANTI_CUBIC
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_anti_cubic);
      }
    | CUB_PHYSICAL_ATTACK ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_cub_physical_attack(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | CUB_PHYSICAL_DEFENCE ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_cub_physical_defence(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | CUB_ATTACK_SPEED ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_cub_attack_speed(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | CUB_BLOCK_ACT
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_cub_block_act);
      }
    | P_SKILL_POWER ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_skill_power(
              static_cast<double>(grammar::SemanticInt32($3)),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_MAGIC_DEFENCE_CRITICAL_RATE ';' '{' identifier_def_2 '}' ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_magic_defence_critical_rate(
              static_cast<double>(grammar::SemanticInt32($7)),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetArmorTypeArray(m_ArmorTypeArray);
          publishPump(effect);
      }
    | P_MAGIC_DEFENCE_CRITICAL_DMG ';' '{' identifier_def_2 '}' ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_magic_defence_critical_dmg(
              static_cast<double>(grammar::SemanticInt32($7)),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetArmorTypeArray(m_ArmorTypeArray);
          publishPump(effect);
      }
    | P_MAGIC_DEFENCE_CRITICAL_RATE_ADD ';' '{' identifier_def_3 '}' ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_magic_defence_critical_rate_add(
              static_cast<double>(grammar::SemanticInt32($7)),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetAdditionalArmorTypeArray(m_AdditionalArmorTypeArray);
          publishPump(effect);
      }
    | P_MAGIC_DEFENCE_CRITICAL_DMG_ADD ';' '{' identifier_def_3 '}' ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_magic_defence_critical_dmg_add(
              static_cast<double>(grammar::SemanticInt32($7)),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetAdditionalArmorTypeArray(m_AdditionalArmorTypeArray);
          publishPump(effect);
      }
    | P_MAGIC_CRITICAL_DMG ';' '{' identifier_def '}' ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_magic_critical_dmg(
              static_cast<double>(grammar::SemanticInt32($7)),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetAttackTypeArray(m_AttackTypeArray);
          publishPump(effect);
      }
    | P_EXP_MODIFY ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_exp_modify(grammar::SemanticDouble($3)));
      }
    | P_SP_MODIFY ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_sp_modify(grammar::SemanticDouble($3)));
      }
    | P_CRYSTAL_GRADE_MODIFY ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_crystal_grade_modify(grammar::SemanticDouble($3)));
      }
    | P_PLAY_MUSIC ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_play_music(grammar::SemanticInt32($3)));
      }
    | P_HIDE
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_hide(true));
      }
    | P_HIDE ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_hide(grammar::SemanticInt32($3) != 0));
      }
    | P_HATE_ATTACK ';' '{' identifier_def '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_hate_attack(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetAttackTypeArray(m_AttackTypeArray);
          publishPump(effect);
      }
    | P_REFLECT_DD ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_reflect_dd(grammar::SemanticDouble($3)));
      }
    | P_REFLECT_DD_RESIST ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_reflect_dd_resist(static_cast<double>(grammar::SemanticInt64($3))));
      }
    | P_MP_VAMPIRIC_ATTACK ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_mp_vampiric_attack(grammar::SemanticDouble($3)));
      }
    | P_MP_VAMPIRIC_ATTACK ';' predefined_def ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_mp_vampiric_attack(grammar::SemanticDouble($3), grammar::SemanticDouble($5))
          );
      }
    | P_BLOCK_RESURRECTION
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_block_resurrection);
      }
    | P_BLOCK_CHAT
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_block_chat);
      }
    | P_BLOCK_PARTY
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_block_party);
      }
    | P_VIOLET_BOY
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_violet_boy);
      }
    | P_DOMINION_TRANSFORM
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_dominion_transform);
      }
    | P_STAT_UP ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_stat_up(grammar::SemanticInt32($3), grammar::SemanticInt32($5)));
      }
    | P_STAT_UP_LIMITED ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_stat_up_limited(grammar::SemanticInt32($3), grammar::SemanticInt32($5)));
      }
    | P_CHEAPSHOT
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_cheapshot);
      }
    | P_TRANSMUTE_ATTR_ATTACK ';' ATTR_VALUE
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_transmute_attr_attack(static_cast<AttributeTypeEnum>(grammar::SemanticInt32($3))
          ));
      }
    | P_TRANSMUTE_ATTR_DEFENCE ';' ATTR_VALUE ';' ATTR_VALUE
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_transmute_attr_defence(
              static_cast<AttributeTypeEnum>(grammar::SemanticInt32($3)),
              static_cast<AttributeTypeEnum>(grammar::SemanticInt32($5))
          ));
      }
    | P_CONDITION_BLOCK_ACT_SKILL ';' '{' n63 '}'
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto publishOwnedListEffect = [&publishPump](const grammar::SemanticBits value, auto makeEffect) {
              auto* const values = grammar::SemanticPointer<std::vector<std::int64_t>>(value);
              publishPump(makeEffect(values));
              delete values;
          };
          publishOwnedListEffect($4, [](std::vector<std::int64_t>* values) {
              return new CSkillEffect_p_condition_block_act_skill(values);
          });
      }
    | P_CONDITION_BLOCK_ACT_ITEM ';' '{' n63 '}'
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto publishOwnedListEffect = [&publishPump](const grammar::SemanticBits value, auto makeEffect) {
              auto* const values = grammar::SemanticPointer<std::vector<std::int64_t>>(value);
              publishPump(makeEffect(values));
              delete values;
          };
          publishOwnedListEffect($4, [](std::vector<std::int64_t>* values) {
              return new CSkillEffect_p_condition_block_act_item(values);
          });
      }
    | P_CONDITION_BLOCK_ACT_SKILL ';' '{' NAME '}'
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          const char16_t* const listName = grammar::SemanticPointer<const char16_t>($4);
          const std::string hostListName = Utf16ToUtf8(listName);
          const int listId = static_cast<int>(g_PrecompiledHeaderDB.Search(listName));
          if (listId == -1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"invalid PredefinedList : Name[%s] at [%d] Line = [%d]",
              //     hostListName.c_str(),
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"invalid PredefinedList : Name[%s] at [%d] Line = [%d]",
              //     hostListName.c_str(),
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: p_condition_block_act_%s list reference '%s' resolves to -1; lookup still "
                  "proceeds with that ID.",
                  static_cast<long long>(m_lexer.line) + 1, "skill", hostListName.c_str()
              );
          }
          std::vector<std::int64_t>* const values = PredefinedListDB::inst().GetPredefinedSkillList(listId);
          if (values == nullptr) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"undefined PredefinedList : Name[%s] at [%d] Line = [%d]",
              //     hostListName.c_str(),
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"undefined PredefinedList : Name[%s] at [%d] Line = [%d]",
              //     hostListName.c_str(),
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: p_condition_block_act_%s list '%s' (ID %d) is unavailable; effect "
                  "construction still receives the null list.",
                  static_cast<long long>(m_lexer.line) + 1, "skill", hostListName.c_str(), listId
              );
          }
          publishPump(new CSkillEffect_p_condition_block_act_skill(values));
      }
    | P_CONDITION_BLOCK_ACT_ITEM ';' '{' NAME '}'
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          const char16_t* const listName = grammar::SemanticPointer<const char16_t>($4);
          const std::string hostListName = Utf16ToUtf8(listName);
          const int listId = static_cast<int>(g_PrecompiledHeaderDB.Search(listName));
          if (listId == -1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"invalid PredefinedList : Name[%s] at [%d] Line = [%d]",
              //     hostListName.c_str(),
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"invalid PredefinedList : Name[%s] at [%d] Line = [%d]",
              //     hostListName.c_str(),
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: p_condition_block_act_%s list reference '%s' resolves to -1; lookup still "
                  "proceeds with that ID.",
                  static_cast<long long>(m_lexer.line) + 1, "item", hostListName.c_str()
              );
          }
          std::vector<std::int64_t>* const values = PredefinedListDB::inst().GetPredefinedItemList(listId);
          if (values == nullptr) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"undefined PredefinedList : Name[%s] at [%d] Line = [%d]",
              //     hostListName.c_str(),
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"undefined PredefinedList : Name[%s] at [%d] Line = [%d]",
              //     hostListName.c_str(),
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: p_condition_block_act_%s list '%s' (ID %d) is unavailable; effect "
                  "construction still receives the null list.",
                  static_cast<long long>(m_lexer.line) + 1, "item", hostListName.c_str(), listId
              );
          }
          publishPump(new CSkillEffect_p_condition_block_act_item(values));
      }
    | P_BLOCK_USE_SKILL ';' '{' n63 '}'
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto publishOwnedListEffect = [&publishPump](const grammar::SemanticBits value, auto makeEffect) {
              auto* const values = grammar::SemanticPointer<std::vector<std::int64_t>>(value);
              publishPump(makeEffect(values));
              delete values;
          };
          publishOwnedListEffect($4, [](std::vector<std::int64_t>* values) {
              return new CSkillEffect_p_block_use_skill(values);
          });
      }
    | P_IGNORE_SKILL ';' '{' n63 '}'
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const values = grammar::SemanticPointer<std::vector<std::int64_t>>($4);
          publishPump(new CSkillEffect_p_ignore_skill(values));
          delete values;
      }
    | P_DEFENCE_DAMAGE ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_defence_damage(
              grammar::SemanticInt32($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_ITEM_CHANCE ';' '{' n73 '}' ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect =
              new CSkillEffect_p_item_chance(static_cast<ItemChanceNpcType>(grammar::SemanticInt32($7)));
          std::copy(m_vItemChanceType.begin(), m_vItemChanceType.end(), std::back_inserter(effect->m_vItemChanceType));
          publishPump(effect);
      }
    | P_BONUSTIMELIMIT_UP ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_bonustimelimit_up(grammar::SemanticInt32($3)));
      }
    | P_CHANGEBODY ';' '{' n39 '}'
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_changebody(m_vChangeBody));
      }
    | P_VITAL_ADD ';' FLOAT_NUMBER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_vital_add(grammar::SemanticDouble($3)));
      }
    | P_VITAL_SUB ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_vital_sub(grammar::SemanticInt32($3)));
          if (grammar::SemanticInt64($3) < 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"p_vital_sub is minus at [%d]. Line = [%d]",
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"p_vital_sub is minus at [%d]. Line = [%d]",
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: p_vital_sub has negative input %lld; the effect has already received its low "
                  "32-bit value %d and is retained.",
                  static_cast<long long>(m_lexer.line) + 1, static_cast<long long>(grammar::SemanticInt64($3)),
                  grammar::SemanticInt32($3)
              );
          }
      }
    | P_EXP_ADD ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_exp_add(
              grammar::SemanticInt32($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_FOCUS_ENERGY ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_focus_energy(grammar::SemanticInt32($3)));
      }
    | P_ABILITY_CHANGE ';' '{' n67 '}'
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_ability_change;
          publishPump(effect);
          for (const SummonStatBonus& bonus : m_SummonStBonusVector) {
              effect->SetEnableStat(GetModIndexFromModName(bonus.statName.c_str()), bonus.bonusRate);
          }
          m_SummonStBonusVector.clear();
      }
    | P_SUB_EXP ';' PREDEFINED ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_sub_exp(
              grammar::SemanticInt32($3), grammar::SemanticInt32($5),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          ));
      }
    | P_SUB_VP ';' PREDEFINED ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_sub_vp(
              grammar::SemanticInt32($3), grammar::SemanticInt32($5),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          ));
      }
    | P_BERSERKER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_berserker);
      }
    | P_MP_SHIELD ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_mp_shield(grammar::SemanticInt32($3), grammar::SemanticInt32($5)));
      }
    | P_ENERGY_HEAL ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_energy_heal(grammar::SemanticInt64($3) != 0));
      }
    | P_DISAPPEAR_TARGET
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_disappear_target);
      }
    | P_BLOCK_TARGET
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_block_target);
      }
    | P_KNOCKDOWN
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_knockdown);
      }
    | P_BLOCK_GET_ENERGY
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_block_get_energy);
      }
    | P_DOUBLE_SUMMON
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_double_summon);
      }
    | P_DEATH_ATTACK ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_death_attack(
              grammar::SemanticInt32($3), grammar::SemanticInt32($5), grammar::SemanticInt32($7),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_BLOCK_ELIXER_PREMIUM
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_block_elixer_premium);
      }
    | P_SPELL_POWER ';' PREDEFINED ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_spell_power(
              static_cast<double>(grammar::SemanticInt64($3)),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_STOP_CONSUME_VITAL_POINT
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_stop_consume_vital_point(VitalityRestore::BY_SKILL));
      }
    | P_STOP_CONSUME_VITAL_POINT_NONCOUNT
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_stop_consume_vital_point(VitalityRestore::BY_SKILL_NONCOUNT));
      }
    | P_SUMMON_POINT ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_summon_point(grammar::SemanticInt32($3)));
      }
    | P_IGNORE_DEATH
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_ignore_death);
      }
    | P_DIRECTION_DAMAGE ';' FRONT_GROUP ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_direction_damage(
              static_cast<SkillFxPositionType>(grammar::SemanticInt32($3)), grammar::SemanticDouble($5),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          ));
      }
    | P_GET_DAMAGE_LIMIT ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_get_damage_limit(static_cast<double>(grammar::SemanticInt64($3))));
      }
    | P_GET_DAMAGE_LIMIT_STATUS ';' IDENTIFIER ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const char16_t* const statusName = grammar::SemanticPointer<const char16_t>($3);
          VALIDATE::Enum statusType = VALIDATE::PDEFEND;
          if (TargetWordEquals(statusName, u"pdefend")) {
              statusType = VALIDATE::PDEFEND;
          } else if (TargetWordEquals(statusName, u"mdefend")) {
              statusType = VALIDATE::MDEFEND;
          } else if (TargetWordEquals(statusName, u"pattack")) {
              statusType = VALIDATE::PATTACK;
          } else if (TargetWordEquals(statusName, u"mattack")) {
              statusType = VALIDATE::MATTACK;
          } else {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Status type of p_get_damage_limit_status should be one of [pdefend, mdefend, pattack, mattack]. Line
              //     = [%d]", m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"Status type of p_get_damage_limit_status should be one of [pdefend, mdefend, pattack, mattack]. Line
              //     = [%d]", m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: p_get_damage_limit_status has unknown status '%s'; expected pdefend, mdefend, "
                  "pattack or mattack. The effect is omitted.",
                  static_cast<long long>(m_lexer.line) + 1, Utf16ToUtf8(statusName).c_str()
              );
              break;
          }
          publishPump(new CSkillEffect_p_get_damage_limit_status(
              statusType, grammar::SemanticInt32($5), grammar::SemanticInt32($7)
          ));
      }
    | P_BLOCK_ESCAPE
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_block_escape);
      }
    | P_RECHARGE_VITAL_POINT
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_recharge_vital_point(VitalityRestore::BY_SKILL));
      }
    | P_RECHARGE_VITAL_POINT_NONCOUNT
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_recharge_vital_point(VitalityRestore::BY_SKILL_NONCOUNT));
      }
    | P_ALTER_SKILL_BY_EVENT ';' IDENTIFIER ';' PREDEFINED ';' predefined_def ';' '{' n75 '}'
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const char16_t* const eventName = grammar::SemanticPointer<const char16_t>($3);
          AbnormalStatusSlotType abnormalType = ABNS_INVALID;
          AlterSkill::Event event = AlterSkill::Critical;
          if (TargetWordEquals(eventName, u"critical")) {
              event = AlterSkill::Critical;
          } else if (TargetWordEquals(eventName, u"block")) {
              event = AlterSkill::Block;
          } else if (TargetWordEquals(eventName, u"avoid")) {
              event = AlterSkill::Avoid;
          } else {
              abnormalType = FindAbnormalByName(eventName, m_lexer.line + 1);
              event = AlterSkill::Abnormal;
              if (abnormalType == ABNS_MAX) {
                  const std::string hostEventName = Utf16ToUtf8(eventName);
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"invalid abnormal type at [%d] (%s). Line = [%d]",
                  //     m_lexer.line + 1,
                  //     hostEventName.c_str(),
                  //     m_nLineNumber)
                  // L2_LOG_ERROR(Log,
                  //     L"invalid abnormal type at [%d] (%s). Line = [%d]",
                  //     m_lexer.line + 1,
                  //     hostEventName.c_str(),
                  //     m_nLineNumber)
                  L2_LOG_ERROR(
                      Log,
                      "skilldata.txt line %lld: alter-skill event '%s' is neither critical, block, avoid nor a resolved "
                      "abnormal slot; the effect is omitted.",
                      static_cast<long long>(m_lexer.line) + 1, hostEventName.c_str()
                  );
                  break;
              }
          }
          const double probability = grammar::SemanticDouble($7);
          if (probability < 0.0 || probability > 100.0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Invalid alter skill probability [%f] at [%d] Line = [%d]",
              //     probability,
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"Invalid alter skill probability [%f] at [%d] Line = [%d]",
              //     probability,
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: alter-skill event probability %.17g is outside 0..100; the effect is omitted.",
                  static_cast<long long>(m_lexer.line) + 1, probability
              );
              break;
          }
          auto* const effect = new CSkillEffect_p_alter_skill_by_event(event);
          TriggerAlterSkillInfo& trigger = m_pSkillInfo->m_TriggerAlterSkillInfo;
          trigger.prob = probability;
          trigger.oriSkillID = grammar::SemanticInt32($5);
          trigger.skillList = *grammar::SemanticPointer<std::vector<AlterSkillInfo*>>($10);
          trigger.abnormalType = abnormalType;
          trigger.expiredSec = 5;
          publishPump(effect);
          m_TriggerAlterSkillVector.push_back(m_pSkillInfo);
      }
    | P_ALTER_SKILL_BY_EVENT ';' IDENTIFIER ';' PREDEFINED ';' PREDEFINED ';' predefined_def ';' '{' n75 '}'
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const char16_t* const eventName = grammar::SemanticPointer<const char16_t>($3);
          AbnormalStatusSlotType abnormalType = ABNS_INVALID;
          AlterSkill::Event event = AlterSkill::Critical;
          if (TargetWordEquals(eventName, u"critical")) {
              event = AlterSkill::Critical;
          } else if (TargetWordEquals(eventName, u"block")) {
              event = AlterSkill::Block;
          } else if (TargetWordEquals(eventName, u"avoid")) {
              event = AlterSkill::Avoid;
          } else {
              abnormalType = FindAbnormalByName(eventName, m_lexer.line + 1);
              event = AlterSkill::Abnormal;
              if (abnormalType == ABNS_MAX) {
                  const std::string hostEventName = Utf16ToUtf8(eventName);
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"invalid abnormal type at [%d] (%s). Line = [%d]",
                  //     m_lexer.line + 1,
                  //     hostEventName.c_str(),
                  //     m_nLineNumber)
                  // L2_LOG_ERROR(Log,
                  //     L"invalid abnormal type at [%d] (%s). Line = [%d]",
                  //     m_lexer.line + 1,
                  //     hostEventName.c_str(),
                  //     m_nLineNumber)
                  L2_LOG_ERROR(
                      Log,
                      "skilldata.txt line %lld: alter-skill event '%s' is neither critical, block, avoid nor a resolved "
                      "abnormal slot; the effect is omitted.",
                      static_cast<long long>(m_lexer.line) + 1, hostEventName.c_str()
                  );
                  break;
              }
          }
          const double probability = grammar::SemanticDouble($9);
          if (probability < 0.0 || probability > 100.0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Invalid alter skill probability [%f] at [%d] Line = [%d]",
              //     probability,
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"Invalid alter skill probability [%f] at [%d] Line = [%d]",
              //     probability,
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: alter-skill event probability %.17g is outside 0..100; the effect is omitted.",
                  static_cast<long long>(m_lexer.line) + 1, probability
              );
              break;
          }
          auto* const effect = new CSkillEffect_p_alter_skill_by_event(event);
          TriggerAlterSkillInfo& trigger = m_pSkillInfo->m_TriggerAlterSkillInfo;
          trigger.prob = probability;
          trigger.oriSkillID = grammar::SemanticInt32($7);
          trigger.skillList = *grammar::SemanticPointer<std::vector<AlterSkillInfo*>>($12);
          trigger.abnormalType = abnormalType;
          trigger.expiredSec = grammar::SemanticInt32($5);
          publishPump(effect);
          m_TriggerAlterSkillVector.push_back(m_pSkillInfo);
      }
    | P_PROTECT_DEATH_PENALTY
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_protect_death_penalty);
      }
    | P_DAMAGE_SHIELD_RESIST ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_damage_shield_resist(grammar::SemanticInt32($3)));
      }
    | P_INSTANT_KILL_RESIST ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_instant_kill_resist(static_cast<double>(grammar::SemanticInt32($3))));
      }
    | P_GET_ITEM_BY_EXP ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_get_item_by_exp(
              grammar::SemanticInt64($3), grammar::SemanticInt32($5), grammar::SemanticInt64($7)
          ));
      }
    | P_NOPKCOUNT ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_nopkcount(grammar::SemanticInt32($3)));
      }
    | P_DAMAGE_BY_ATTACK ';' PK_GROUP ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_damage_by_attack(
              static_cast<EnemyType>(grammar::SemanticInt32($3)), grammar::SemanticDouble($5),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          ));
      }
    | P_STATBONUS_SPEED ';' '{' identifier_def_2 '}' ';' IDENTIFIER ';' predefined_def
      {
          const auto parameterTypeAt = [](const grammar::SemanticBits value) {
              const char16_t* const name = grammar::SemanticPointer<const char16_t>(value);
              if (TargetWordEquals(name, u"str")) {
                  return PARAM_STR;
              }
              if (TargetWordEquals(name, u"dex")) {
                  return PARAM_DEX;
              }
              if (TargetWordEquals(name, u"con")) {
                  return PARAM_CON;
              }
              if (TargetWordEquals(name, u"int")) {
                  return PARAM_INT;
              }
              if (TargetWordEquals(name, u"men")) {
                  return PARAM_MEN;
              }
              if (TargetWordEquals(name, u"wit")) {
                  return PARAM_WIT;
              }
              return PARAM_EXP;
          };
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const ParameterType parameterType = parameterTypeAt($7);
          if (parameterType == PARAM_EXP) {
              break;
          }
          CSkillEffectAddBonusByArmorType* effect = nullptr;
          effect = new CSKillEffect_p_statbonus_speed(grammar::SemanticDouble($9), parameterType);
          effect->SetArmorTypeArray(m_ArmorTypeArray);
          publishPump(effect);
      }
    | P_STATBONUS_SKILLCRITICAL ';' '{' identifier_def_2 '}' ';' IDENTIFIER ';' predefined_def
      {
          const auto parameterTypeAt = [](const grammar::SemanticBits value) {
              const char16_t* const name = grammar::SemanticPointer<const char16_t>(value);
              if (TargetWordEquals(name, u"str")) {
                  return PARAM_STR;
              }
              if (TargetWordEquals(name, u"dex")) {
                  return PARAM_DEX;
              }
              if (TargetWordEquals(name, u"con")) {
                  return PARAM_CON;
              }
              if (TargetWordEquals(name, u"int")) {
                  return PARAM_INT;
              }
              if (TargetWordEquals(name, u"men")) {
                  return PARAM_MEN;
              }
              if (TargetWordEquals(name, u"wit")) {
                  return PARAM_WIT;
              }
              return PARAM_EXP;
          };
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const ParameterType parameterType = parameterTypeAt($7);
          if (parameterType == PARAM_EXP) {
              break;
          }
          CSkillEffectAddBonusByArmorType* effect = nullptr;
          effect = new CSkillEffect_p_statbonus_skillcritical(grammar::SemanticDouble($9), parameterType);
          effect->SetArmorTypeArray(m_ArmorTypeArray);
          publishPump(effect);
      }
    | P_WEAK_DEATH_PENALTY
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_weak_death_penalty);
      }
    | P_POSITION_DAMAGE ';' FRONT_GROUP ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_position_damage(
              static_cast<SkillFxPositionType>(grammar::SemanticInt32($3)), grammar::SemanticDouble($5),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          ));
      }
    | P_BLOCK_ACT_BONUS ';' PREDEFINED ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_block_act_bonus(
              grammar::SemanticInt32($3), grammar::SemanticDouble($5),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          ));
      }
    | P_POTION_BONUS ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_potion_bonus(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_PHYSICAL_SKILL_CRITICAL_RATE ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_physical_skill_critical_rate(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_PHYSICAL_SKILL_CRITICAL_DAMAGE ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_physical_skill_critical_damage(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_HIT_SUMMON ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_hit_summon(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_STOP_VITALITY_EFFECT
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_stop_vitality_effect(VitalityRestore::BY_SKILL));
      }
    | P_DROPRATE_MODIFY ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_droprate_modify(grammar::SemanticInt32($3), grammar::SemanticInt32($5)));
      }
    | P_DROPAMOUNT_MODIFY ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_dropamount_modify(grammar::SemanticInt32($3), grammar::SemanticInt32($5)));
      }
    | P_PHYSICAL_ATTACK_SHIELD ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_physical_attack_shield(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_ATTACK_SPEED_SHIELD ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_attack_speed_shield(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_CRITICAL_RATE_SHIELD ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_critical_rate_shield(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_CRITICAL_DAMAGE_SHIELD ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_critical_damage_shield(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_KALIE_BUFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_kalie_buff);
      }
    | P_BALTHUS_BUFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_balthus_buff);
      }
    | P_HP_SPECIAL_ADD ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_hp_special_add(grammar::SemanticDouble($3)));
      }
    | P_ALTER_RECEIVE_NORMAL_DAMAGE ';' PK_GROUP ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_alter_receive_normal_damage(
              static_cast<ActionTargetType>(grammar::SemanticInt32($3)), grammar::SemanticInt32($5)
          ));
      }
    | P_CREATE_ITEM_CRITICAL ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_create_item_critical(grammar::SemanticDouble($3)));
      }
    | P_BLOCK_GETDAMAGE_EXCEPT
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_block_getdamage_except);
      }
    | P_FISHING_MONSTER_PROB ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_Fishing_Monster_prob(grammar::SemanticDouble($3)));
      }
    | P_FISHING_START_TIME ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_Fishing_start_time(grammar::SemanticDouble($3)));
      }
    | P_FISHING_INSTANTKILL_PROB ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_Fishing_instantkill_prob(grammar::SemanticInt32($3)));
      }
    | P_FISHING_AREA_BUFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_Fishing_Area_buff);
      }
    | P_WORLDCHATPOINT_ADD ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_Worldchatpoint_Add(grammar::SemanticInt32($3)));
      }
    | I_FISHING_INSTANTKILL ';' PREDEFINED
      {
          m_pSkillInfo->PushEffect(
              new CSkillEffect_i_Fishing_instantkill(grammar::SemanticInt32($3)), m_EffectActivateTiming
          );
      }
    | P_RAID_POINT_MODIFY ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_raid_point_modify(grammar::SemanticDouble($3)));
      }
    | P_FISHING_DAMAGE ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_fishing_damage(grammar::SemanticDouble($3)));
      }
    | P_REMOVE_GRADE_PENALTY ';' IDENTIFIER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const std::wstring crystalName = Utf16ToWide(SemanticWord($3));
          publishPump(new CSkillEffect_p_remove_grade_penalty(CItem::GetCrysalType(crystalName.c_str())));
      }
    | P_ENERGY_SKILL_POWER ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_energy_skill_power(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | I_P_PERCENT_ATTACK ';' predefined_def ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          const int powerPercent = runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($3));
          if (!m_bOnlyPrecompiledHeader && (powerPercent < -99 || powerPercent > 2000)) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[i_p_percent_attack][%d] invalid nPowerPercent: %d",
              //     m_nLineNumber,
              //     powerPercent)
              // L2_LOG_ERROR(Log,
              //     L"[i_p_percent_attack][%d] invalid nPowerPercent: %d",
              //     m_nLineNumber,
              //     powerPercent)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: i_p_percent_attack power %.17g converts to %d percent, outside -99..2000; the "
                  "effect retains the converted value.",
                  static_cast<long long>(m_lexer.line) + 1, grammar::SemanticDouble($3), powerPercent
              );
          }
          publishInstant(new CSkillEffect_i_p_percent_attack(
              static_cast<double>(powerPercent), grammar::SemanticInt32($5), grammar::SemanticInt32($7),
              grammar::SemanticInt32($9)
          ));
      }
    | P_ALIGN_DIRECTION
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_align_direction);
      }
    | P_ADD_ENSOUL_PROB ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_add_ensoul_prob(grammar::SemanticDouble($3)));
      }
    | I_CALL_SKILL_BY_TARGETDIRECTION ';' NAME ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          const int direction = grammar::SemanticInt32($5);
          const int range = grammar::SemanticInt32($7);
          publishInstant(new CSkillEffect_i_call_skill_by_targetdirection(std::u16string(SemanticWord($3)), direction, range)
          );
          if (!m_bOnlyPrecompiledHeader && (direction < 0 || direction >= 360 || range < 0 || range >= 181)) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[i_call_skill_by_targetdirection][%d] invalid parameters nDirection:%d, nRange: %d",
              //     m_nLineNumber,
              //     direction,
              //     range)
              // L2_LOG_ERROR(Log,
              //     L"[i_call_skill_by_targetdirection][%d] invalid parameters nDirection:%d, nRange: %d",
              //     m_nLineNumber,
              //     direction,
              //     range)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: i_call_skill_by_targetdirection has direction %d or range %d outside 0..359 "
                  "and 0..180 respectively; the published effect is retained.",
                  static_cast<long long>(m_lexer.line) + 1, direction, range
              );
          }
      }
    | I_SUMMON_TARGET_TO_ME
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_summon_target_to_me);
      }
    | I_BLINK_PARTY ';' IDENTIFIER ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          SkillDirectionType direction = DT_FRONT;
          const char16_t* const directionName = SemanticWord($3);
          if (TargetWordEquals(directionName, u"_front")) {
              direction = DT_FRONT;
          } else if (TargetWordEquals(directionName, u"_back")) {
              direction = DT_BACK;
          } else {
              const std::string hostDirection = Utf16ToUtf8(directionName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"[i_blink_party][%d] invalid parameters direction:%s",
              //     m_nLineNumber,
              //     hostDirection.c_str())
              // L2_LOG_ERROR(Log,
              //     L"[i_blink_party][%d] invalid parameters direction:%s",
              //     m_nLineNumber,
              //     hostDirection.c_str())
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: i_blink_party has unknown direction '%s'; expected _front or _back. The "
                  "effect is omitted.",
                  static_cast<long long>(m_lexer.line) + 1, hostDirection.c_str()
              );
              break;
          }
          publishInstant(new CSkillEffect_i_blink_party(
              static_cast<double>(grammar::SemanticInt64($5)), direction,
              static_cast<double>(grammar::SemanticInt64($7))
          ));
      }
    | I_M_ATTACK_DEBUFF_DMG ';' predefined_def ';' predefined_def
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(
              new CSkillEffect_i_m_attack_debuff_dmg(grammar::SemanticDouble($3), grammar::SemanticDouble($5))
          );
      }
    | P_PSYCHIC_POWER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_psychic_power);
      }
    | I_FLOAT_AWAY ';' IDENTIFIER ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          int flyType = 0;
          const char16_t* const flyTypeName = SemanticWord($3);
          if (TargetWordAsciiIEquals(flyTypeName, u"float_stand")) {
              flyType = 10;
          } else if (TargetWordAsciiIEquals(flyTypeName, u"float_laydown")) {
              flyType = 11;
          } else {
              const std::string hostFlyType = Utf16ToUtf8(flyTypeName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"[i_float_away][%d] invalid parameters fly_type:%s",
              //     m_nLineNumber,
              //     hostFlyType.c_str())
              // L2_LOG_ERROR(Log,
              //     L"[i_float_away][%d] invalid parameters fly_type:%s",
              //     m_nLineNumber,
              //     hostFlyType.c_str())
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: i_float_away has unknown fly type '%s'; expected float_stand or "
                  "float_laydown. The effect is omitted.",
                  static_cast<long long>(m_lexer.line) + 1, hostFlyType.c_str()
              );
              break;
          }
          const int flyHeight = grammar::SemanticInt32($5);
          if (static_cast<std::uint32_t>(flyHeight) >= 1000U) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[i_float_away][%d] invalid parameters fly_height:%d",
              //     m_nLineNumber,
              //     flyHeight)
              // L2_LOG_ERROR(Log,
              //     L"[i_float_away][%d] invalid parameters fly_height:%d",
              //     m_nLineNumber,
              //     flyHeight)
              L2_LOG_ERROR(
                  Log, "skilldata.txt line %lld: i_float_away fly height %d is outside 0..999; the effect is omitted.",
                  static_cast<long long>(m_lexer.line) + 1, flyHeight
              );
              break;
          }
          publishInstant(new CSkillEffect_i_float_away(flyType, flyHeight));
      }
    | P_HIDE_NO_INHERIT ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_hide_no_inherit(grammar::SemanticInt32($3) != 0));
      }
    | P_MAGICAL_CRITICAL_RATE_MAXUP ';' '{' identifier_def '}' ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_magical_critical_rate_maxup(grammar::SemanticDouble($7), SFCT_NONE);
          effect->SetAttackTypeArray(m_AttackTypeArray);
          publishPump(effect);
      }
    | P_ACQUIRE_ALCHEMY_SKILL ';' IDENTIFIER ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          int skillType = 0;
          const char16_t* const skillTypeName = SemanticWord($3);
          if (TargetWordEquals(skillTypeName, u"rolly")) {
              skillType = 0;
          } else if (TargetWordEquals(skillTypeName, u"conv")) {
              skillType = 1;
          } else {
              // Log.Add(
              //     LOG_ERROR,
              //     L"p_acquire_alchemy_skill skill type error!!!")
              // L2_LOG_ERROR(Log,
              //     L"p_acquire_alchemy_skill skill type error!!!")
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: p_acquire_alchemy_skill has unknown skill type '%s'; expected rolly or conv. "
                  "Using rolly.",
                  static_cast<long long>(m_lexer.line) + 1, Utf16ToUtf8(skillTypeName).c_str()
              );
              skillType = 0;
          }
          publishPump(new CSKillEffetct_p_acquire_alchemy_skill(skillType, $5 == 1));
      }
    | P_VITAL_RECHARGE_BOOST ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_vital_recharge_boost(grammar::SemanticInt32($3)));
      }
    | P_ADD_VITAL_BONUS ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_add_vital_bonus(grammar::SemanticInt32($3)));
      }
    | P_REDUCE_VITAL_CONSUME_POINT ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_reduce_vital_consume_point(grammar::SemanticInt32($3)));
      }
    | P_INC_SPOIL_RATE ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_inc_spoil_rate(grammar::SemanticInt32($3)));
      }
    | P_PHYSICAL_DEFENCE_PVP ';' '{' identifier_def_2 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_physical_defence_pvp(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetArmorTypeArray(m_ArmorTypeArray);
          publishPump(effect);
      }
    | P_SPEED_PVP ';' '{' identifier_def_2 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_speed_pvp(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetArmorTypeArray(m_ArmorTypeArray);
          publishPump(effect);
      }
    | P_DAMAGE_SHIELD_RESIST_CASTLE_SIEGE ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_damage_shield_resist_castle_siege(
              runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($3))
          ));
      }
    | P_DAMAGE_BY_ATTACK_CASTLE_SIEGE ';' PK_GROUP ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_damage_by_attack_castle_siege(
              static_cast<EnemyType>(grammar::SemanticInt32($3)), grammar::SemanticDouble($5),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          ));
      }
    | P_PVE_PHYSICAL_DEFENCE ';' '{' n66 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto publishPveDefence = [&](bool physical) {
              auto* const npcIds = new std::vector<std::int64_t>;
              auto* const npcNames = grammar::SemanticPointer<std::vector<char16_t*>>($4);
              if (!m_bOnlyPrecompiledHeader) {
                  for (const char16_t* const npcName : *npcNames) {
                      const int npcId = static_cast<int>(g_PrecompiledHeaderDB.Search(npcName));
                      if (npcId >= 0) {
                          npcIds->push_back(npcId);
                      }
                  }
                  delete npcNames;
              }

              CSkillEffect* const effect = physical
                  ? static_cast<CSkillEffect*>(new CSkillEffect_p_pve_physical_defence(
                        grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9)),
                        npcIds
                    ))
                  : static_cast<CSkillEffect*>(new CSkillEffect_p_pve_magical_defence(
                        grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9)),
                        npcIds
                    ));
              publishPump(effect);
              delete npcIds;
          };
          publishPveDefence(true);
      }
    | P_PVE_MAGICAL_DEFENCE ';' '{' n66 '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const auto publishPveDefence = [&](bool physical) {
              auto* const npcIds = new std::vector<std::int64_t>;
              auto* const npcNames = grammar::SemanticPointer<std::vector<char16_t*>>($4);
              if (!m_bOnlyPrecompiledHeader) {
                  for (const char16_t* const npcName : *npcNames) {
                      const int npcId = static_cast<int>(g_PrecompiledHeaderDB.Search(npcName));
                      if (npcId >= 0) {
                          npcIds->push_back(npcId);
                      }
                  }
                  delete npcNames;
              }

              CSkillEffect* const effect = physical
                  ? static_cast<CSkillEffect*>(new CSkillEffect_p_pve_physical_defence(
                        grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9)),
                        npcIds
                    ))
                  : static_cast<CSkillEffect*>(new CSkillEffect_p_pve_magical_defence(
                        grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9)),
                        npcIds
                    ));
              publishPump(effect);
              delete npcIds;
          };
          publishPveDefence(false);
      }
    | P_PVE_DEFENCE_ATTRIBUTE ';' PK_GROUP ';' ATTR_VALUE ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_pve_defence_attribute(
              static_cast<EnemyType>(grammar::SemanticInt32($3)),
              static_cast<AttributeTypeEnum>(grammar::SemanticInt32($5)), grammar::SemanticDouble($7)
          ));
      }
    | P_ABNORMAL_TIME_CHANGE_TO_ABTYPE ';' IDENTIFIER ';' PREDEFINED ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_abnormal_time_change_to_abType(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9)),
              static_cast<int>(GetAbnormalStatusSlotTypeByName(const_cast<char16_t*>(SemanticWord($3)))),
              grammar::SemanticInt32($5)
          ));
      }
    | P_ABNORMAL_TIME_CHANGE_TO_DEBUFF ';' IDENTIFIER ';' PREDEFINED ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          DebuffType debuffType = DB_MAX;
          const char16_t* const typeName = SemanticWord($3);
          if (TargetWordEquals(typeName, u"slot_buff")) {
              debuffType = DB_GOOD;
          } else if (TargetWordEquals(typeName, u"slot_debuff")) {
              debuffType = DB_BAD;
          } else {
              const std::string hostType = Utf16ToUtf8(typeName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"wrong debuff Type [%s] !",
              //     hostType.c_str())
              // L2_LOG_ERROR(Log,
              //     L"wrong debuff Type [%s] !",
              //     hostType.c_str())
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: p_abnormal_time_change_to_debuff has unknown slot type '%s'; expected "
                  "slot_buff or slot_debuff. The effect retains DB_MAX.",
                  static_cast<long long>(m_lexer.line) + 1, hostType.c_str()
              );
          }
          publishPump(new CSkillEffect_p_abnormal_time_change_to_debuff(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9)), debuffType,
              grammar::SemanticInt32($5)
          ));
      }
    | P_BLOCK_GETDAMAGE_EXCEPT_EACH_OTHER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_block_getdamage_except_each_other);
      }
    | P_MP_FRIENDLY_PAYBACK ';' predefined_def ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          if (grammar::SemanticDouble($3) > 100.0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"p_mp_friendly_payback payback percent over 100")
              // L2_LOG_ERROR(Log,
              //     L"p_mp_friendly_payback payback percent over 100")
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: p_mp_friendly_payback percentage %.17g exceeds 100; the effect retains this "
                  "value.",
                  static_cast<long long>(m_lexer.line) + 1, grammar::SemanticDouble($3)
              );
          }
          publishPump(
              new CSkillEffect_p_mp_friendly_payback(grammar::SemanticDouble($3), grammar::SemanticDouble($5))
          );
      }
    | P_INTERCEPT_SKILL ';' '{' PREDEFINED ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED ';' predefined_def '}'
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_intercept_skill(
              grammar::SemanticInt32($4), grammar::SemanticInt32($6), grammar::SemanticInt32($8),
              grammar::SemanticInt32($10), grammar::SemanticDouble($12)
          ));
      }
    | P_REPLACE_SKILL ';' '{' PREDEFINED ';' PREDEFINED ';' predefined_def '}'
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          const double probability = grammar::SemanticDouble($8);
          if (probability < 0.0 || probability > 100.0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"p_replace_skill prob should between [0.0, 100.0]. not [%f]. Line = [%d]",
              //     probability,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"p_replace_skill prob should between [0.0, 100.0]. not [%f]. Line = [%d]",
              //     probability,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: p_replace_skill probability %.17g is outside 0..100; the effect is omitted.",
                  static_cast<long long>(m_lexer.line) + 1, probability
              );
              break;
          }
          publishPump(
              new CSkillEffect_p_replace_skill(grammar::SemanticInt32($4), grammar::SemanticInt32($6), probability)
          );
      }
    | P_VITAL_RATE ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_vital_rate(grammar::SemanticInt32($3)));
      }
    | C_MP ';' predefined_def ';' PREDEFINED
      {
          m_pSkillInfo->m_pConsumeEffect =
              new CSkillEffect_c_mp(grammar::SemanticDouble($3), grammar::SemanticInt32($5));
      }
    | C_HP ';' predefined_def ';' PREDEFINED
      {
          m_pSkillInfo->m_pConsumeEffect =
              new CSkillEffect_c_hp(grammar::SemanticDouble($3), grammar::SemanticInt32($5));
      }
    | C_REST ';' predefined_def ';' PREDEFINED
      {
          m_pSkillInfo->m_pConsumeEffect =
              new CSkillEffect_c_rest(grammar::SemanticDouble($3), grammar::SemanticInt32($5));
      }
    | C_CHAMELEON_REST ';' predefined_def ';' PREDEFINED
      {
          m_pSkillInfo->m_pConsumeEffect =
              new CSkillEffect_c_chameleon_rest(grammar::SemanticDouble($3), grammar::SemanticInt32($5));
      }
    | C_MP_BY_LEVEL ';' predefined_def ';' PREDEFINED
      {
          m_pSkillInfo->m_pConsumeEffect =
              new CSkillEffect_c_mp_by_level(grammar::SemanticDouble($3), grammar::SemanticInt32($5));
      }
    | C_FAKE_DEATH ';' predefined_def ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          m_pSkillInfo->m_pConsumeEffect =
              new CSkillEffect_c_fake_death(grammar::SemanticDouble($3), grammar::SemanticInt32($5));
          publishPump(m_pSkillInfo->m_pConsumeEffect);
      }
    | C_AGATHION_ENERGY ';' predefined_def ';' PREDEFINED
      {
          m_pSkillInfo->m_pConsumeEffect =
              new CSkillEffect_c_agathion_energy(grammar::SemanticDouble($3), grammar::SemanticInt32($5));
      }
    | I_SUMMON ';' NAME ';' PREDEFINED ';' NAME ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_summon(
              SemanticWord($3), grammar::SemanticInt32($5), SemanticWord($7), grammar::SemanticInt32($9),
              grammar::SemanticInt32($11)
          ));
          m_pSkillInfo->m_IsSummonSkill = true;
      }
    | I_SUMMON_MULTI ';' NAME ';' PREDEFINED ';' NAME ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_summon_multi(
              SemanticWord($3), grammar::SemanticInt32($5), SemanticWord($7), grammar::SemanticInt32($9)
          ));
          m_pSkillInfo->m_IsSummonSkill = true;
      }
    | I_SUMMON_PET
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_summon_pet);
          m_pSkillInfo->m_IsSummonSkill = true;
      }
    | I_FOOD_FOR_PET ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_food_for_pet(
              grammar::SemanticInt32($3), grammar::SemanticInt32($5), grammar::SemanticInt32($7)
          ));
          m_pSkillInfo->m_bPetFoodSkill = true;
      }
    | I_HP ';' predefined_def ';' PER_GROUP
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_hp(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | I_HP_SPECIAL ';' predefined_def ';' PER_GROUP
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_hp_special(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | I_MP ';' predefined_def ';' PER_GROUP
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_mp(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | I_MP_BY_HP ';' predefined_def ';' PER_GROUP
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_mp_by_hp(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | I_RESURRECTION ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_resurrection(grammar::SemanticInt32($3)));
      }
    | I_INFORM
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_inform);
      }
    | I_CP ';' predefined_def ';' PER_GROUP
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_cp(
              runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($3)),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | I_SP ';' predefined_def ';' PER_GROUP
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_sp(
              runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($3)),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | I_PHYSICAL_ATTACK_HP_LINK ';' predefined_def ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_physical_attack_hp_link(
              grammar::SemanticDouble($3), grammar::SemanticInt32($5), grammar::SemanticInt32($7),
              grammar::SemanticInt32($9)
          ));
      }
    | I_M_ATTACK_BY_HP ';' predefined_def
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_m_attack_by_hp(grammar::SemanticDouble($3)));
      }
    | I_M_ATTACK_BY_DIST ';' predefined_def
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_m_attack_by_dist(grammar::SemanticDouble($3)));
      }
    | I_FISHING_CAST
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_fishing_cast);
      }
    | I_FISHING_PUMPING ';' predefined_def
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_fishing_pumping(grammar::SemanticDouble($3)));
      }
    | I_FISHING_REELING ';' predefined_def
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_fishing_reeling(grammar::SemanticDouble($3)));
      }
    | I_FISHING_SHOT ';' predefined_def
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_fishing_shot(grammar::SemanticDouble($3)));
      }
    | I_OPEN_DWARF_RECIPEBOOK
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_open_dwarf_recipebook);
      }
    | I_OPEN_COMMON_RECIPEBOOK
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_open_common_recipebook);
      }
    | I_M_ATTACK_BY_ABNORMAL_SLOT ';' IDENTIFIER ';' predefined_def
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_m_attack_by_abnormal_slot(
              const_cast<char16_t*>(SemanticWord($3)), grammar::SemanticDouble($5)
          ));
      }
    | I_ABNORMAL_TIME_CHANGE ';' IDENTIFIER ';' predefined_def ';' PER_GROUP ';' IDENTIFIER ';' PREDEFINED
      {
          const auto slotTypeAt = [](const grammar::SemanticBits value) {
              const char16_t* const name = SemanticWord(value);
              if (TargetWordEquals(name, u"slot_buff")) {
                  return DB_GOOD;
              }
              if (TargetWordEquals(name, u"slot_debuff")) {
                  return DB_BAD;
              }
              return DB_MAX;
          };
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_abnormal_time_change(
              const_cast<char16_t*>(SemanticWord($3)), slotTypeAt($9), grammar::SemanticDouble($5),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7)), grammar::SemanticInt32($11)
          ));
      }
    | I_ABNORMAL_TIME_CHANGE ';' IDENTIFIER ';' PER_GROUP ';' IDENTIFIER ';' PREDEFINED
      {
          const auto slotTypeAt = [](const grammar::SemanticBits value) {
              const char16_t* const name = SemanticWord(value);
              if (TargetWordEquals(name, u"slot_buff")) {
                  return DB_GOOD;
              }
              if (TargetWordEquals(name, u"slot_debuff")) {
                  return DB_BAD;
              }
              return DB_MAX;
          };
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          const auto logInvalidReset = [this]() {
              // Log.Add(
              //     LOG_ERROR,
              //     L"{i_abnormal_time_change.[slot].reset} or {i_abnormal_time_change.[slot].[timeBonus].[type]}. Line = %d
              //     !", m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"{i_abnormal_time_change.[slot].reset} or {i_abnormal_time_change.[slot].[timeBonus].[type]}. Line = %d
              //     !", m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: i_abnormal_time_change requires reset in this form; use "
                  "{i_abnormal_time_change;[slot];reset} or {i_abnormal_time_change;[slot];[timeBonus];[type]}. The effect "
                  "is "
                  "omitted.",
                  static_cast<long long>(m_lexer.line) + 1
              );
          };
          const SkillFxChangeType changeType = static_cast<SkillFxChangeType>(grammar::SemanticInt32($5));
          if (changeType != SFCT_RESET) {
              logInvalidReset();
              break;
          }
          publishInstant(new CSkillEffect_i_abnormal_time_change(
              const_cast<char16_t*>(SemanticWord($3)), slotTypeAt($7), changeType, grammar::SemanticInt32($9)
          ));
      }
    | I_ABNORMAL_TIME_CHANGE ';' IDENTIFIER ';' predefined_def ';' PER_GROUP ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_abnormal_time_change(
              const_cast<char16_t*>(SemanticWord($3)), DB_MAX, grammar::SemanticDouble($5),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7)), grammar::SemanticInt32($9)
          ));
      }
    | I_ABNORMAL_TIME_CHANGE ';' IDENTIFIER ';' predefined_def ';' PER_GROUP ';' IDENTIFIER
      {
          const auto slotTypeAt = [](const grammar::SemanticBits value) {
              const char16_t* const name = SemanticWord(value);
              if (TargetWordEquals(name, u"slot_buff")) {
                  return DB_GOOD;
              }
              if (TargetWordEquals(name, u"slot_debuff")) {
                  return DB_BAD;
              }
              return DB_MAX;
          };
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_abnormal_time_change(
              const_cast<char16_t*>(SemanticWord($3)), slotTypeAt($9), grammar::SemanticDouble($5),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7)), -100
          ));
      }
    | I_ABNORMAL_TIME_CHANGE ';' IDENTIFIER ';' predefined_def ';' PER_GROUP
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_abnormal_time_change(
              const_cast<char16_t*>(SemanticWord($3)), DB_MAX, grammar::SemanticDouble($5),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7)), -100
          ));
      }
    | I_ABNORMAL_TIME_CHANGE ';' IDENTIFIER ';' PER_GROUP ';' IDENTIFIER
      {
          const auto slotTypeAt = [](const grammar::SemanticBits value) {
              const char16_t* const name = SemanticWord(value);
              if (TargetWordEquals(name, u"slot_buff")) {
                  return DB_GOOD;
              }
              if (TargetWordEquals(name, u"slot_debuff")) {
                  return DB_BAD;
              }
              return DB_MAX;
          };
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          const auto logInvalidReset = [this]() {
              // Log.Add(
              //     LOG_ERROR,
              //     L"{i_abnormal_time_change.[slot].reset} or {i_abnormal_time_change.[slot].[timeBonus].[type]}. Line = %d
              //     !", m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"{i_abnormal_time_change.[slot].reset} or {i_abnormal_time_change.[slot].[timeBonus].[type]}. Line = %d
              //     !", m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: i_abnormal_time_change requires reset in this form; use "
                  "{i_abnormal_time_change;[slot];reset} or {i_abnormal_time_change;[slot];[timeBonus];[type]}. The effect "
                  "is "
                  "omitted.",
                  static_cast<long long>(m_lexer.line) + 1
              );
          };
          const SkillFxChangeType changeType = static_cast<SkillFxChangeType>(grammar::SemanticInt32($5));
          if (changeType != SFCT_RESET) {
              logInvalidReset();
              break;
          }
          publishInstant(new CSkillEffect_i_abnormal_time_change(
              const_cast<char16_t*>(SemanticWord($3)), slotTypeAt($7), changeType, -100
          ));
      }
    | I_DISPEL_BY_CATEGORY ';' IDENTIFIER ';' PREDEFINED ';' PREDEFINED
      {
          const auto slotTypeAt = [](const grammar::SemanticBits value) {
              const char16_t* const name = SemanticWord(value);
              if (TargetWordEquals(name, u"slot_buff")) {
                  return DB_GOOD;
              }
              if (TargetWordEquals(name, u"slot_debuff")) {
                  return DB_BAD;
              }
              return DB_MAX;
          };
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect =
              new CSkillEffect_i_dispel_by_category(grammar::SemanticInt32($5), grammar::SemanticInt32($7));
          effect->eDeBuffType = slotTypeAt($3);
          publishInstant(effect);
      }
    | I_DISPEL_BY_SLOT_PROBABILITY ';' IDENTIFIER ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_dispel_by_slot_probability(
              const_cast<char16_t*>(SemanticWord($3)), grammar::SemanticInt32($5)
          ));
      }
    | I_ALIGN_DIRECTION
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_align_direction(0));
      }
    | I_ALIGN_DIRECTION ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_align_direction(grammar::SemanticInt32($3)));
      }
    | I_DEATH ';' predefined_def
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_death(grammar::SemanticDouble($3), grammar::SemanticDouble($3)));
      }
    | I_DEATH ';' predefined_def ';' predefined_def
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_death(grammar::SemanticDouble($3), grammar::SemanticDouble($5)));
      }
    | I_REBALANCE_HP
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_rebalance_hp);
      }
    | I_REBALANCE_HP_SUMMON
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_rebalance_hp_summon);
      }
    | I_REBALANCE_MP
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_rebalance_mp);
      }
    | I_TARGET_CANCEL
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_target_cancel(0));
      }
    | I_TARGET_CANCEL ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_target_cancel(grammar::SemanticInt32($3)));
      }
    | I_TARGET_ME
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_target_me(0, false));
      }
    | I_TARGET_ME ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_target_me(grammar::SemanticInt32($3), false));
      }
    | I_TARGET_ME ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_target_me(grammar::SemanticInt32($3), true));
      }
    | I_TELEPORT ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_teleport(
              FVector(grammar::SemanticInt32($3), grammar::SemanticInt32($5), grammar::SemanticInt32($7))
          ));
      }
    | I_INSTALL_CAMP_EX
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_install_camp_ex);
      }
    | I_M_ATTACK_MP ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_m_attack_mp(
              static_cast<double>(grammar::SemanticInt64($3)), grammar::SemanticInt32($5),
              grammar::SemanticInt32($7)
          ));
      }
    | I_NPC_KILL ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_npc_kill(static_cast<double>(grammar::SemanticInt64($3))));
      }
    | I_BETRAY ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_betray(grammar::SemanticInt32($3), grammar::SemanticInt32($5)));
      }
    | I_CALL_PC
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_call_pc);
      }
    | I_CALL_PC ';' NAME ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          const char16_t* const itemName = SemanticWord($3);
          const int itemClassId = static_cast<int>(g_PrecompiledHeaderDB.Search(itemName));
          if (itemClassId == -1) {
              const std::string hostName = Utf16ToUtf8(itemName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Invalid Item[%s] at [%d] Line = [%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"Invalid Item[%s] at [%d] Line = [%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1,
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: i_call_pc item reference '%s' resolves to -1; the effect still receives item "
                  "ID -1.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
              );
          }
          publishInstant(new CSkillEffect_i_call_pc(itemClassId, grammar::SemanticInt32($5)));
      }
    | I_CHANGE_SKILL_LEVEL ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(
              new CSkillEffect_i_change_skill_level(grammar::SemanticInt32($3), grammar::SemanticInt32($5))
          );
      }
    | I_REMOVE_ENERGY ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_remove_energy(grammar::SemanticInt32($3)));
      }
    | I_SKILL_TURNING ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_skill_turning(grammar::SemanticInt32($3), grammar::SemanticInt32($5)));
      }
    | I_SKILL_TURNING ';' '{' n63 '}' ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          auto* const magicTypes = grammar::SemanticPointer<std::vector<std::int64_t>>($4);
          publishInstant(new CSkillEffect_i_skill_turning(magicTypes, grammar::SemanticInt32($7)));
          delete magicTypes;
      }
    | I_CALL_PARTY
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_call_party);
      }
    | I_CALL_TARGET_PARTY
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_call_target_party);
      }
    | I_M_ATTACK_RANGE ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(
              new CSkillEffect_i_m_attack_range(grammar::SemanticInt32($3), grammar::SemanticInt32($5), false)
          );
      }
    | I_P_ATTACK_BY_DIST ';' predefined_def ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_p_attack_by_dist(
              static_cast<double>(runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($3))),
              grammar::SemanticInt32($5), grammar::SemanticInt32($7), grammar::SemanticInt32($9)
          ));
      }
    | I_M_ATTACK_OVER_HIT_RANGE ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_m_attack_over_hit_range(
              grammar::SemanticInt32($3), grammar::SemanticInt32($5), false
          ));
      }
    | I_PLEDGE_SEND_SYSTEM_MESSAGE ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_pledge_send_system_message(grammar::SemanticInt32($3)));
      }
    | I_REAL_DAMAGE ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_real_damage(
              grammar::SemanticInt32($3), grammar::SemanticInt32($5), grammar::SemanticInt32($7)
          ));
      }
    | I_SET_VISIBLE
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_set_visible);
      }
    | I_CAPTURE_FLAG_START
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_capture_flag_start);
      }
    | I_CAPTURE_FLAG
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_capture_flag);
      }
    | I_CONVERT_ITEM
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_convert_item);
      }
    | I_DETECT_OBJECT
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_detect_object);
      }
    | I_MOUNT_FOR_EVENT ';' NAME
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_mount_for_event(SemanticWord($3)));
      }
    | I_DISMOUNT_FOR_EVENT ';' NAME
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_dismount_for_event(SemanticWord($3)));
      }
    | I_SET_SKILL ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_set_skill(grammar::SemanticInt32($3), grammar::SemanticInt32($5)));
      }
    | I_BOOKMARK_ADD_SLOT ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_bookmark_add_slot(grammar::SemanticInt32($3)));
      }
    | I_BOOKMARK_TELEPORT
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_bookmark_teleport);
      }
    | I_PULL ';' predefined_def
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          const auto addCanGoCondition = [this](double distance) {
              m_pSkillInfo->PushCondition(new CSkillOperateCondition_op_can_go(distance), CSkillInfo::OCT_CASTER);
              m_pSkillInfo->PushConditionMsg(-1, CSkillInfo::OCT_CASTER);
          };
          publishInstant(new CSkillEffect_i_pull(runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($3)), 0));
          m_pSkillInfo->m_IsFlySkill = true;
          addCanGoCondition(grammar::SemanticDouble($3));
      }
    | I_PULL ';' predefined_def ';' predefined_def
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          const auto addCanGoCondition = [this](double distance) {
              m_pSkillInfo->PushCondition(new CSkillOperateCondition_op_can_go(distance), CSkillInfo::OCT_CASTER);
              m_pSkillInfo->PushConditionMsg(-1, CSkillInfo::OCT_CASTER);
          };
          publishInstant(new CSkillEffect_i_pull(
              runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($3)),
              runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($5))
          ));
          m_pSkillInfo->m_IsFlySkill = true;
          addCanGoCondition(grammar::SemanticDouble($3));
      }
    | I_ADD_MAX_ENTRANCE_INZONE ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(
              new CSkillEffect_i_add_max_entrance_inzone(grammar::SemanticInt32($3), grammar::SemanticInt32($5))
          );
      }
    | I_REFUEL_AIRSHIP ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_refuel_airship(grammar::SemanticInt32($3)));
      }
    | I_MY_SUMMON_KILL
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_my_summon_kill);
      }
    | I_DESPAWN ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_despawn(static_cast<double>(grammar::SemanticInt64($3))));
      }
    | I_BONUSCOUNT_UP ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_bonuscount_up(grammar::SemanticInt32($3)));
      }
    | I_RESTORE_VITAL_POINT ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_restore_vital_point(grammar::SemanticInt32($3), VitalityRestore::BY_SKILL));
      }
    | I_RESTORE_VITAL_POINT_NONCOUNT ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(
              new CSkillEffect_i_restore_vital_point(grammar::SemanticInt32($3), VitalityRestore::BY_SKILL_NONCOUNT)
          );
      }
    | I_SUICIDE
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_suicide);
      }
    | I_P_ATTACK_SAVE_HP ';' predefined_def ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_p_attack_save_hp(
              static_cast<double>(runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($3))),
              grammar::SemanticInt32($5), grammar::SemanticInt32($7), grammar::SemanticInt32($9),
              grammar::SemanticInt32($11)
          ));
      }
    | I_CALL_SKILL_BY_FORCE
      {
          m_pCSkillEffect_i_call_skill_by_force = new CSkillEffect_i_call_skill_by_force;
      }
      ';' '{' n60 '}'
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(m_pCSkillEffect_i_call_skill_by_force);
          m_pCSkillEffect_i_call_skill_by_force = nullptr;
      }
    | I_PK_COUNT ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_pk_count(grammar::SemanticInt32($3)));
      }
    | I_KARMA_COUNT ';' PER_GROUP ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_karma_count(
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($3)), grammar::SemanticInt32($5)
          ));
      }
    | I_SIDESTEP_TO_TARGET ';' FRONT_GROUP
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(
              new CSkillEffect_i_sidestep_to_target(static_cast<SkillFxPositionType>(grammar::SemanticInt32($3)))
          );
      }
    | P_ATTACK_POSITION_CHANGE ';' FRONT_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(
              new CSKillEffetct_p_attack_position_change(static_cast<SkillFxPositionType>(grammar::SemanticInt32($3)))
          );
      }
    | I_P_ATTACK_ABNORMAL ';' predefined_def ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED ';' IDENTIFIER ';' predefined_def
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_p_attack_abnormal(
              static_cast<double>(runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($3))),
              grammar::SemanticInt32($5), grammar::SemanticInt32($7), grammar::SemanticInt32($9),
              const_cast<char16_t*>(SemanticWord($11)), grammar::SemanticDouble($13)
          ));
      }
    | P_BLOCK_GETDAMAGE_DISTANCE ';' PREDEFINED ';' IDENTIFIER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          std::wstring blockType = Utf16ToWide(SemanticWord($5));
          publishPump(new CSkillEffect_p_block_getdamage_distance(
              static_cast<double>(grammar::SemanticInt64($3)), blockType.data()
          ));
      }
    | P_REAL_RESIST ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSKillEffetct_p_real_resist(grammar::SemanticDouble($3)));
      }
    | I_SUMMON_TERRITORY ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSKillEffetct_i_summon_territory(grammar::SemanticInt32($3), grammar::SemanticInt32($5))
          );
      }
    | I_ACC_CHANGE
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_acc_change);
          m_pSkillInfo->m_bHairAccFlag = true;
      }
    | I_CALL_RANDOM_SKILL_CONSUME_ITEM ';' '{' n46 '}'
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_i_call_random_skill_consume_item;
          effect->SetRandomUnitList(m_vSkillRandomUnit);
          publishInstant(effect);
      }
    | P_MAGICAL_ATTACK_BY_WEAPON ';' '{' identifier_def '}' ';' predefined_def ';' PER_GROUP
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const effect = new CSkillEffect_p_magical_attack_by_weapon(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetAttackTypeArray(m_AttackTypeArray);
          publishPump(effect);
      }
    | I_EV_INK ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_ev_ink(grammar::SemanticInt32($3)));
      }
    | I_EV_INK_PARTY ';' PREDEFINED
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_ev_ink_party(grammar::SemanticInt32($3)));
      }
    | P_SKILL_CRITICAL_RATE_FIX ';' '{' n63 '}' ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          auto* const skillIds = grammar::SemanticPointer<std::vector<std::int64_t>>($4);
          auto* const effect = new CSkillEffect_p_skill_critical_rate_fix(grammar::SemanticInt32($7));
          effect->SetSkillList(skillIds);
          publishPump(effect);
          delete skillIds;
      }
    | P_DISPEL_BY_REALTARGET ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_dispel_by_realtarget(grammar::SemanticDouble($3)));
      }
    | P_PREVENTION_DEATH ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, m_EffectActivateTiming);
          };
          publishPump(new CSkillEffect_p_prevention_death(
              grammar::SemanticInt64($3) > 0, grammar::SemanticInt64($5) > 0, grammar::SemanticInt64($7) > 0
          ));
      }
    | P_ABILITY_CHANGE_ITEMSKILL
      {
          const auto publishPump = [this](CSkillEffect* effect, CSkillInfo::EffectActivateTiming timing) {
              m_pSkillInfo->PushPumpEffect(effect, timing);
          };
          publishPump(new CSkillEffect_p_ability_change_itemskill, m_EffectActivateTiming);
      }
    | P_BLOCK_SKILL_TRIGGER
      {
          const auto publishPump = [this](CSkillEffect* effect, CSkillInfo::EffectActivateTiming timing) {
              m_pSkillInfo->PushPumpEffect(effect, timing);
          };
          publishPump(new CSkillEffect_p_block_skill_trigger, m_EffectActivateTiming);
      }
    | I_GOTO_TARGET
      {
          const auto publishInstant = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushEffect(effect, m_EffectActivateTiming);
          };
          publishInstant(new CSkillEffect_i_goto_target);
      }
    | P_ABSORB_SHIELD ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect, CSkillInfo::EffectActivateTiming timing) {
              m_pSkillInfo->PushPumpEffect(effect, timing);
          };
          publishPump(new CSkillEffect_p_absorb_shield(grammar::SemanticInt32($3), false), CSkillInfo::EAT_SUCCESS);
      }
    | P_ABSORB_SHIELD ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect, CSkillInfo::EffectActivateTiming timing) {
              m_pSkillInfo->PushPumpEffect(effect, timing);
          };
          publishPump(
              new CSkillEffect_p_absorb_shield(grammar::SemanticInt32($3), grammar::SemanticInt64($5) > 0),
              CSkillInfo::EAT_SUCCESS
          );
      }
    | P_ABSORB_SHIELD_STATUS ';' IDENTIFIER ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect, CSkillInfo::EffectActivateTiming timing) {
              m_pSkillInfo->PushPumpEffect(effect, timing);
          };
          const char16_t* const statusName = SemanticWord($3);
          VALIDATE::Enum statusType = VALIDATE::PDEFEND;
          if (TargetWordEquals(statusName, u"pdefend")) {
              statusType = VALIDATE::PDEFEND;
          } else if (TargetWordEquals(statusName, u"mdefend")) {
              statusType = VALIDATE::MDEFEND;
          } else if (TargetWordEquals(statusName, u"pattack")) {
              statusType = VALIDATE::PATTACK;
          } else if (TargetWordEquals(statusName, u"mattack")) {
              statusType = VALIDATE::MATTACK;
          } else {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Status type of p_absorb_shield_status should be one of [pdefend, mdefend, pattack, mattack]. Line =
              //     [%d]", m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Status type of p_absorb_shield_status should be one of [pdefend, mdefend, pattack, mattack]. Line =
              //     [%d]", m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: p_absorb_shield_status has unknown status '%s'; expected pdefend, mdefend, "
                  "pattack or mattack. The effect is omitted.",
                  static_cast<long long>(m_lexer.line) + 1, Utf16ToUtf8(statusName).c_str()
              );
              break;
          }
          publishPump(
              new CSkillEffect_p_absorb_shield_status(statusType, grammar::SemanticDouble($5)), CSkillInfo::EAT_SUCCESS
          );
      }
    | P_BLOCK_GETDAMAGE_TARGET_ONLY
      {
          const auto publishPump = [this](CSkillEffect* effect, CSkillInfo::EffectActivateTiming timing) {
              m_pSkillInfo->PushPumpEffect(effect, timing);
          };
          publishPump(new CSkillEffect_p_block_getdamage_target_only, CSkillInfo::EAT_SUCCESS);
      }
    | P_EXP_SP_FISHING ';' predefined_def
      {
          const auto publishPump = [this](CSkillEffect* effect, CSkillInfo::EffectActivateTiming timing) {
              m_pSkillInfo->PushPumpEffect(effect, timing);
          };
          publishPump(new CSkillEffect_p_exp_sp_fishing(grammar::SemanticDouble($3)), m_EffectActivateTiming);
      }
    | P_PHYSICAL_PENETRATE ';' '{' identifier_def '}' ';' PREDEFINED ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect, CSkillInfo::EffectActivateTiming timing) {
              m_pSkillInfo->PushPumpEffect(effect, timing);
          };
          const std::int64_t defenceType = grammar::SemanticInt64($7);
          const std::int64_t penetrationRate = grammar::SemanticInt64($9);
          if (defenceType < 0 || defenceType > 2) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Defence type must be 0, 1 or 2. line[%d]",
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Defence type must be 0, 1 or 2. line[%d]",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: p_physical_penetrate defence type %lld is outside 0..2; the effect still "
                  "receives its low 32-bit value %d.",
                  static_cast<long long>(m_lexer.line) + 1, static_cast<long long>(defenceType),
                  grammar::SemanticInt32($7)
              );
          }
          if (penetrationRate < 0 || penetrationRate > 50) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Penetration Rate must be between 0 and 50. line[%d]",
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Penetration Rate must be between 0 and 50. line[%d]",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: p_physical_penetrate rate %lld is outside 0..50; the effect still receives "
                  "its low 32-bit value %d.",
                  static_cast<long long>(m_lexer.line) + 1, static_cast<long long>(penetrationRate),
                  grammar::SemanticInt32($9)
              );
          }
          auto* const effect =
              new CSkillEffect_p_physical_penetrate(grammar::SemanticInt32($7), grammar::SemanticInt32($9));
          effect->SetAttackTypeArray(m_AttackTypeArray);
          publishPump(effect, CSkillInfo::EAT_SUCCESS);
      }
    | P_MAGICAL_PENETRATE ';' '{' identifier_def '}' ';' PREDEFINED
      {
          const auto publishPump = [this](CSkillEffect* effect, CSkillInfo::EffectActivateTiming timing) {
              m_pSkillInfo->PushPumpEffect(effect, timing);
          };
          const std::int64_t penetrationRate = grammar::SemanticInt64($7);
          if (penetrationRate < 0 || penetrationRate > 50) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Penetration Rate must be between 0 and 50. line[%d]",
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Penetration Rate must be between 0 and 50. line[%d]",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: p_magical_penetrate rate %lld is outside 0..50; the effect still receives its "
                  "low 32-bit value %d.",
                  static_cast<long long>(m_lexer.line) + 1, static_cast<long long>(penetrationRate),
                  grammar::SemanticInt32($7)
              );
          }
          auto* const effect = new CSkillEffect_p_magical_penetrate(grammar::SemanticInt32($7));
          effect->SetAttackTypeArray(m_AttackTypeArray);
          publishPump(effect, CSkillInfo::EAT_SUCCESS);
      }
    | P_EXPAND_ARTIFACT_SLOT_GROUP ';' PREDEFINED
      {
          const std::int64_t level = grammar::SemanticInt64($3);
          if (level < 0 || level > 3) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Artifactbook Level must be between 0 and 3. line[%d]",
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Artifactbook Level must be between 0 and 3. line[%d]",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: p_expand_artifact_slot_group level %lld is outside 0..3; the effect still "
                  "receives its low 32-bit value %d.",
                  static_cast<long long>(m_lexer.line) + 1, static_cast<long long>(level), grammar::SemanticInt32($3)
              );
          }
          m_pSkillInfo->PushPumpEffect(
              new CSkillEffect_p_expand_artifact_slot_group(grammar::SemanticInt32($3)), CSkillInfo::EAT_SUCCESS
          );
      }
    ;

n60
    : n60 ';' n61
    | n61
    ;

n61
    : '{' PREDEFINED ';' PREDEFINED ';' NAME ';' SELF_GROUP '}'
      {
          m_pCSkillEffect_i_call_skill_by_force->AddForceSkill(
              grammar::SemanticInt32($2), grammar::SemanticInt32($4), std::u16string(SemanticWord($6)),
              static_cast<SkillTargetTypeEnum>(grammar::SemanticInt32($8))
          );
      }
    ;

predefined_def
    : PREDEFINED
      {
          const double value = static_cast<double>(grammar::SemanticInt64($1));
          grammar::SemanticBits encoded = 0;
          std::memcpy(&encoded, &value, sizeof(value));
          $$ = encoded;
      }
    | FLOAT_NUMBER
      { $$ = $1; }
    ;

n63
    : n63 ';' PREDEFINED
      {
          auto* const values = grammar::SemanticPointer<std::vector<std::int64_t>>($1);
          values->push_back(grammar::SemanticInt64($3));
          $$ = grammar::PointerSemantic(values);
      }
    | PREDEFINED
      {
          auto* const values = new std::vector<std::int64_t>;
          values->push_back(grammar::SemanticInt64($1));
          $$ = grammar::PointerSemantic(values);
      }
    ;

n64
    : n64 ';' PREDEFINED
    | PREDEFINED
      {
          if (m_pSkillInfo->m_nSkillId == 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"wrong skill id [%d] !",
              //     0)
              // L2_LOG_ERROR(Log,
              //     L"wrong skill id [%d] !",
              //     0)
              L2_LOG_ERROR(
                  Log, "skilldata.txt line %lld: skill ID is 0; the ID is retained and parsing continues.",
                  static_cast<long long>(m_lexer.line) + 1
              );
          }
      }
    ;

n65
    : n65 ';' NAME
      {
          auto* const values = grammar::SemanticPointer<std::vector<char16_t*>>($1);
          values->push_back(const_cast<char16_t*>(SemanticWord($3)));
          $$ = grammar::PointerSemantic(values);
      }
    | NAME
      {
          auto* const values = new std::vector<char16_t*>;
          values->push_back(const_cast<char16_t*>(SemanticWord($1)));
          $$ = grammar::PointerSemantic(values);
      }
    ;

n66
    : n66 ';' IDENTIFIER
      {
          auto* const values = grammar::SemanticPointer<std::vector<char16_t*>>($1);
          values->push_back(const_cast<char16_t*>(SemanticWord($3)));
          $$ = grammar::PointerSemantic(values);
      }
    | IDENTIFIER
      {
          auto* const values = new std::vector<char16_t*>;
          values->push_back(const_cast<char16_t*>(SemanticWord($1)));
          $$ = grammar::PointerSemantic(values);
      }
    ;

n67
    : n67 ';' n68
    | n68
    ;

n68
    : '{' IDENTIFIER ';' PREDEFINED '}'
      {
          SummonStatBonus bonus;
          bonus.statName.assign(SemanticWord($2));
          bonus.bonusRate = grammar::SemanticInt32($4);
          m_SummonStBonusVector.push_back(bonus);
      }
    ;

i_unlock_def
    : I_UNLOCK ';' IDENTIFIER ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED ';' '{'
      {
          pUnlockEffect = new CSkillEffect_i_unlock(
              TargetWordEquals(SemanticWord($3), u"byitem"), grammar::SemanticInt32($5), grammar::SemanticInt32($7),
              grammar::SemanticInt32($9)
          );
      }
      n71 '}'
      {
          m_pSkillInfo->PushEffect(pUnlockEffect, m_EffectActivateTiming);
          pUnlockEffect = nullptr;
      }
    | I_UNLOCK ';' IDENTIFIER ';' PREDEFINED ';' PREDEFINED ';' PREDEFINED
      {
          pUnlockEffect = new CSkillEffect_i_unlock(
              TargetWordEquals(SemanticWord($3), u"byitem"), grammar::SemanticInt32($5), grammar::SemanticInt32($7),
              grammar::SemanticInt32($9)
          );
          m_pSkillInfo->PushEffect(pUnlockEffect, m_EffectActivateTiming);
          pUnlockEffect = nullptr;
      }
    ;

n71
    : n71 ';' n72
    | n72
    ;

n72
    : '{' NAME ';' PREDEFINED '}'
      {
          pUnlockEffect->m_vDoorNames.emplace_back(SemanticWord($2));
          pUnlockEffect->m_vProbs.push_back(grammar::SemanticInt32($4));
      }
    ;

n73
    : n73 ';' n74
    | n74
    ;

n74
    : '{' PREDEFINED ';' PREDEFINED ';' PREDEFINED ';' FLOAT_NUMBER ';' FLOAT_NUMBER ';' FLOAT_NUMBER '}'
      {
          m_vItemChanceType.emplace_back(
              static_cast<ItemChanceLevelType>(grammar::SemanticInt32($2)), grammar::SemanticInt32($4),
              grammar::SemanticInt32($6), grammar::SemanticDouble($8) / 100.0,
              grammar::SemanticDouble($10) / 100.0, grammar::SemanticDouble($12) / 100.0
          );
      }
    ;

n75
    : n75 ';' '{' IDENTIFIER ';' PREDEFINED '}'
      {
          auto* const alterSkill = new AlterSkillInfo;
          alterSkill->skillName.assign(SemanticWord($4));
          alterSkill->prob = static_cast<double>(grammar::SemanticInt64($6));
          grammar::SemanticPointer<std::vector<AlterSkillInfo*>>($1)->push_back(alterSkill);
      }
    | '{' IDENTIFIER ';' PREDEFINED '}'
      {
          auto* const alterSkill = new AlterSkillInfo;
          alterSkill->skillName.assign(SemanticWord($2));
          alterSkill->prob = static_cast<double>(grammar::SemanticInt64($4));
          auto* const alterSkills = new std::vector<AlterSkillInfo*>;
          alterSkills->push_back(alterSkill);
          $$ = grammar::PointerSemantic(alterSkills);
      }
    ;

n76
    : n76 ';' n77
    | n77
    ;

n77
    : '{' '{' identifier_def '}' ';' PREDEFINED ';' PER_GROUP '}'
      {
          m_vWeaponBonusTypeList.emplace_back(
              m_AttackTypeArray, grammar::SemanticInt32($6),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($8))
          );
      }
    ;

n78
    : n78 ';' predefined_def_2
    | predefined_def_2
    ;

predefined_def_2
    : PREDEFINED
      { m_sMagicType.insert(grammar::SemanticInt32($1)); }
    | ALL
      { m_sMagicType.insert(-1); }
    ;

n80
    : n80 ';' n81
    | n81
    ;

n81
    : '{' IDENTIFIER ';' PREDEFINED '}'
      {
          AbnormalDataForTrigger abnormal;
          abnormal.m_Type = FindAbnormalByName(SemanticWord($2), 0);
          abnormal.m_cAbnormalLevel = static_cast<std::uint8_t>($4);
          m_vAbnormalData.push_back(abnormal);
      }
    ;

n82
    : n82 ';' name_def
    | %empty
    ;

name_def
    : NAME
      { m_setSkillName.insert(std::u16string(SemanticWord($1))); }
    ;

n84
    : n84 ';' identifier_def_6
    | identifier_def_6
    | %empty
    ;

identifier_def_6
    : IDENTIFIER
      {
          m_setAbnormalSlotType.insert(FindAbnormalByName(SemanticWord($1), m_lexer.line + 1));
      }
    ;

n86
    : n86 ';' n87
    | n87
    ;

n87
    : '{' NAME ';' PREDEFINED '}'
      {
          FixedShapeShiftExtractionItemInfo item;
          item.xwsItemName.assign(SemanticWord($2));
          item.nProb = grammar::SemanticInt32($4);
          pShapeShiftWpFixedEffect->m_vExtractionItems.push_back(item);
      }
    ;

n88
    : n88 ';' n89
    | n89
    ;

n89
    : '{' NAME ';' PREDEFINED '}'
      {
          FixedShapeShiftExtractionItemInfo item;
          item.xwsItemName.assign(SemanticWord($2));
          item.nProb = grammar::SemanticInt32($4);
          pShapeShiftAmFixedEffect->m_vExtractionItems.push_back(item);
      }
    ;

n90
    : n90 ';' n91
    | n91
    ;

n91
    : '{' IDENTIFIER ';' '{'
      {
          [&] {
              using ShapeShiftAutoMatchingType::type;
              static constexpr std::pair<std::u16string_view, type> mappings[] = {
                  {u"t_one_hand_sword", ShapeShiftAutoMatchingType::SSAMT_ONE_HAND_SWORD},
                  {u"t_one_hand_magic_sword", ShapeShiftAutoMatchingType::SSAMT_ONE_HAND_MAGIC_SWORD},
                  {u"t_dagger", ShapeShiftAutoMatchingType::SSAMT_DAGGER},
                  {u"t_two_hand_sword", ShapeShiftAutoMatchingType::SSAMT_TWO_HAND_SWORD},
                  {u"t_dual_fist", ShapeShiftAutoMatchingType::SSAMT_DUAL_FIST},
                  {u"t_pole", ShapeShiftAutoMatchingType::SSAMT_POLE},
                  {u"t_bow", ShapeShiftAutoMatchingType::SSAMT_BOW},
                  {u"t_crossbow", ShapeShiftAutoMatchingType::SSAMT_CROSSBOW},
                  {u"t_one_hand_blunt", ShapeShiftAutoMatchingType::SSAMT_ONE_HAND_BLUNT},
                  {u"t_two_hand_blunt", ShapeShiftAutoMatchingType::SSAMT_TWO_HAND_BLUNT},
                  {u"t_one_hand_magic_blunt", ShapeShiftAutoMatchingType::SSAMT_ONE_HAND_MAGIC_BLUNT},
                  {u"t_two_hand_magic_blunt", ShapeShiftAutoMatchingType::SSAMT_TWO_HAND_MAGIC_BLUNT},
                  {u"t_dual_sword", ShapeShiftAutoMatchingType::SSAMT_DUAL_SWORD},
                  {u"t_dual_dagger", ShapeShiftAutoMatchingType::SSAMT_DUAL_DAGGER},
                  {u"t_dual_blunt", ShapeShiftAutoMatchingType::SSAMT_DUAL_BLUNT},
                  {u"t_shield", ShapeShiftAutoMatchingType::SSAMT_SHIELD},
                  {u"t_sigil", ShapeShiftAutoMatchingType::SSAMT_SIGIL},
                  {u"t_heavy_chest", ShapeShiftAutoMatchingType::SSAMT_HEAVY_CHEST},
                  {u"t_heavy_gloves", ShapeShiftAutoMatchingType::SSAMT_HEAVY_GLOVES},
                  {u"t_heavy_legs", ShapeShiftAutoMatchingType::SSAMT_HEAVY_LEGS},
                  {u"t_heavy_feet", ShapeShiftAutoMatchingType::SSAMT_HEAVY_FEET},
                  {u"t_heavy_onepiece", ShapeShiftAutoMatchingType::SSAMT_HEAVY_ONEPIECE},
                  {u"t_heavy_alldress", ShapeShiftAutoMatchingType::SSAMT_HEAVY_ALLDRESS},
                  {u"t_light_chest", ShapeShiftAutoMatchingType::SSAMT_LIGHT_CHEST},
                  {u"t_light_gloves", ShapeShiftAutoMatchingType::SSAMT_LIGHT_GLOVES},
                  {u"t_light_legs", ShapeShiftAutoMatchingType::SSAMT_LIGHT_LEGS},
                  {u"t_light_feet", ShapeShiftAutoMatchingType::SSAMT_LIGHT_FEET},
                  {u"t_light_onepiece", ShapeShiftAutoMatchingType::SSAMT_LIGHT_ONEPIECE},
                  {u"t_light_alldress", ShapeShiftAutoMatchingType::SSAMT_LIGHT_ALLDRESS},
                  {u"t_magic_chest", ShapeShiftAutoMatchingType::SSAMT_MAGIC_CHEST},
                  {u"t_magic_gloves", ShapeShiftAutoMatchingType::SSAMT_MAGIC_GLOVES},
                  {u"t_magic_legs", ShapeShiftAutoMatchingType::SSAMT_MAGIC_LEGS},
                  {u"t_magic_feet", ShapeShiftAutoMatchingType::SSAMT_MAGIC_FEET},
                  {u"t_magic_onepiece", ShapeShiftAutoMatchingType::SSAMT_MAGIC_ONEPIECE},
                  {u"t_magic_alldress", ShapeShiftAutoMatchingType::SSAMT_MAGIC_ALLDRESS},
              };
              const char16_t* const property = SemanticWord($2);
              for (const auto& mapping : mappings) {
                  if (TargetWordEquals(property, mapping.first)) {
                      eShapeShiftItemAutoMatchingType = mapping.second;
                      return;
                  }
              }
              const std::string hostProperty = Utf16ToUtf8(property);
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s : invalid property at [%d] (%s). Line = [%d]",
              //     L"..\\Script\\skilldata.txt",
              //     m_lexer.line + 1,
              //     hostProperty.c_str(),
              //     m_nLineNumber)
              // L2_LOG_ERROR(Log,
              //     L"%s : invalid property at [%d] (%s). Line = [%d]",
              //     L"..\\Script\\skilldata.txt",
              //     m_lexer.line + 1,
              //     hostProperty.c_str(),
              //     m_nLineNumber)
              L2_LOG_ERROR(
                  Log,
                  "skilldata.txt line %lld: unknown shape-shift auto-matching property '%s'; the matching type is left "
                  "unchanged.",
                  static_cast<long long>(m_lexer.line) + 1, hostProperty.c_str()
              );
          }();
      }
      n93 '}' '}'
      {
          eShapeShiftItemAutoMatchingType = static_cast<ShapeShiftAutoMatchingType::type>(-1);
      }
    ;

n93
    : n93 ';' n94
    | n94
    ;

n94
    : '{' NAME ';' PREDEFINED '}'
      {
          FixedShapeShiftExtractionItemInfo item;
          item.xwsItemName.assign(SemanticWord($2));
          item.nProb = grammar::SemanticInt32($4);
          pShapeShiftItemAutoMatchingEffect->m_vExtractionItems[eShapeShiftItemAutoMatchingType].push_back(item);
      }
    ;

n95
    : n95 ';' n96
    | n96
    ;

n96
    : '{' NAME ';' predefined_def '}'
      {
          pRandomCallSkillEffect->m_vRandomSkillNameList.push_back(std::u16string(SemanticWord($2)));
          pRandomCallSkillEffect->m_nRandomSkillProbList.push_back(grammar::SemanticDouble($4));
      }
    ;

n97
    : n97 ';' n98
    | n98
    ;

n98
    : '{' COND_P_OLYMPIAD_PLEDGE '}'
      {
          const auto lastPumpEffect = [this]() {
              const CSkillInfo::SkillEffects* const effects = m_pSkillInfo->GetPumpEffects(m_EffectActivateTiming);
              // The current generated action calls vector::back directly. Its
              // grammar guarantees a preceding pump effect for these reductions.
              return effects->back();
          };
          CSkillEffect* const effect = lastPumpEffect();
          if (effect != nullptr) {
              effect->effectCondList.push_back(new CEffectCond_p_olympiad_pledge);
          }
      }
    | '{' COND_P_CASTLE_SIEGE '}'
      {
          const auto lastPumpEffect = [this]() {
              const CSkillInfo::SkillEffects* const effects = m_pSkillInfo->GetPumpEffects(m_EffectActivateTiming);
              // As in actions 1007 and the current generated body, the grammar
              // guarantees that this vector already contains a pump effect.
              return effects->back();
          };
          CSkillEffect* const effect = lastPumpEffect();
          if (effect == nullptr) {
              break;
          }
          effect->effectCondList.push_back(new CEffectCond_p_castle_siege);
      }
    | '{' COND_P_OVER_HP ';' predefined_def '}'
      {
          const auto lastPumpEffect = [this]() {
              const CSkillInfo::SkillEffects* const effects = m_pSkillInfo->GetPumpEffects(m_EffectActivateTiming);
              // As in actions 1007 and the current generated body, the grammar
              // guarantees that this vector already contains a pump effect.
              return effects->back();
          };
          CSkillEffect* const effect = lastPumpEffect();
          if (effect == nullptr) {
              break;
          }
          effect->effectCondList.push_back(new CEffectCond_p_over_hp(grammar::SemanticDouble($4)));
      }
    ;

// The original tables have rules 277, 278, 279, 280, 281, 282, 283, 284, 285,
// 286, 287, 288, 289 and 960 that no parser state reduces.
// They are left out.

%%
