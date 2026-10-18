// skilldata.txt scanner (original L2Server/lexer_skill.l).
// re2c generates LexerSkill.cpp.

#include "L2Server/src/LexerSkill.h"
#include "Shared/src/TextRuntime.h"

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
#include <cstdint>

#include "ParserSkillTokens.h"

namespace {

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverlength-strings"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverlength-strings"
#endif

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

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

struct ConstantAction {
    int token;
    std::int64_t value;
    bool hasValue;
};

} // namespace

CLexerForSkill::CLexerForSkill() :
    m_FileBuf(nullptr),
    m_pRawFileBuf(nullptr),
    m_nFilePointer(0),
    m_nFileSize(0),
    line(0),
    comment_count(0),
    m_bOnlyPrecompiledHeader(false) {
}

CLexerForSkill::~CLexerForSkill() {
    if (m_pRawFileBuf != nullptr) {
        std::free(m_pRawFileBuf);
    }
    m_pRawFileBuf = nullptr;
}

int CLexerForSkill::Create(char16_t* fileBuffer, int fileSize) {
    m_nFilePointer = 0;
    m_FileBuf = fileBuffer;
    m_nFileSize = fileSize;
    return 1;
}

namespace {

// Keywords of the identifier rule, sorted by their UTF-16 code units.
struct Keyword {
    std::u16string_view text;
    int token;
    bool hasValue;
    std::int64_t value;
};

constexpr std::array<Keyword, 1090> kKeywords{{
    Keyword{u"a_speed", TK_A_SPEED, false, 0},
    Keyword{u"a_synergy_skill", TK_A_SYNERGY_SKILL, false, 0},
    Keyword{u"abnormal_delete_leaveworld", TK_ABNORMAL_DELETE_LEAVEWORLD, false, 0},
    Keyword{u"abnormal_instant", TK_ABNORMAL_INSTANT, false, 0},
    Keyword{u"abnormal_lv", TK_ABNORMAL_LV, false, 0},
    Keyword{u"abnormal_time", TK_ABNORMAL_TIME, false, 0},
    Keyword{u"abnormal_time_critical", TK_ABNORMAL_TIME_CRITICAL, false, 0},
    Keyword{u"abnormal_type", TK_ABNORMAL_TYPE, false, 0},
    Keyword{u"abnormal_visual_effect", TK_ABNORMAL_VISUAL_EFFECT, false, 0},
    Keyword{u"abs", TK_PER_GROUP, true, 3},
    Keyword{u"activate_rate", TK_ACTIVATE_RATE, false, 0},
    Keyword{u"advance_base", TK_SELF_GROUP, true, 18},
    Keyword{u"affect_limit", TK_AFFECT_LIMIT, false, 0},
    Keyword{u"affect_object", TK_AFFECT_OBJECT, false, 0},
    Keyword{u"affect_range", TK_AFFECT_RANGE, false, 0},
    Keyword{u"affect_scope", TK_AFFECT_SCOPE, false, 0},
    Keyword{u"affect_scope_height", TK_AFFECT_SCOPE_HEIGHT, false, 0},
    Keyword{u"after_effect_type", TK_AFTER_EFFECT_TYPE, false, 0},
    Keyword{u"all", TK_ALL, false, 0},
    Keyword{u"alter_skill_list", TK_ALTER_SKILL_LIST, false, 0},
    Keyword{u"ancientsword", TK_SWORD_GROUP, true, 18},
    Keyword{u"armor_heavy", TK_ARMOR_VALUE, true, 2},
    Keyword{u"armor_light", TK_ARMOR_VALUE, true, 1},
    Keyword{u"armor_magic", TK_ARMOR_VALUE, true, 3},
    Keyword{u"armor_none", TK_ARMOR_VALUE, true, 0},
    Keyword{u"armor_sigil", TK_ARMOR_VALUE, true, 4},
    Keyword{u"artillery", TK_SELF_GROUP, true, 15},
    Keyword{u"attach_toggle_id", TK_ATTACH_TOGGLE_ID, false, 0},
    Keyword{u"attach_toggle_list", TK_ATTACH_TOGGLE_LIST, false, 0},
    Keyword{u"attached_skill", TK_ATTACHED_SKILL, false, 0},
    Keyword{u"attr_earth", TK_ATTR_VALUE, true, 3},
    Keyword{u"attr_fire", TK_ATTR_VALUE, true, 0},
    Keyword{u"attr_holy", TK_ATTR_VALUE, true, 4},
    Keyword{u"attr_none", TK_ATTR_VALUE, true, -2},
    Keyword{u"attr_unholy", TK_ATTR_VALUE, true, 5},
    Keyword{u"attr_water", TK_ATTR_VALUE, true, 1},
    Keyword{u"attr_wind", TK_ATTR_VALUE, true, 2},
    Keyword{u"attribute", TK_ATTRIBUTE, false, 0},
    Keyword{u"awaken_penalty", TK_AWAKEN_PENALTY, false, 0},
    Keyword{u"back", TK_FRONT_GROUP, true, 2},
    Keyword{u"balakas_scope", TK_SINGLE_GROUP, true, 12},
    Keyword{u"basic_property", TK_BASIC_PROPERTY, false, 0},
    Keyword{u"block_action_use_skill", TK_BLOCK_ACTION_USE_SKILL, false, 0},
    Keyword{u"blunt", TK_SWORD_GROUP, true, 4},
    Keyword{u"boss", TK_PK_GROUP, true, 2},
    Keyword{u"bow", TK_SWORD_GROUP, true, 11},
    Keyword{u"buff_protect_level", TK_BUFF_PROTECT_LEVEL, false, 0},
    Keyword{u"build_advance_base", TK_BUILD_ADVANCE_BASE, false, 0},
    Keyword{u"build_camp", TK_BUILD_CAMP, false, 0},
    Keyword{u"buster", TK_SWORD_GROUP, true, 3},
    Keyword{u"c_agathion_energy", TK_C_AGATHION_ENERGY, false, 0},
    Keyword{u"c_chameleon_rest", TK_C_CHAMELEON_REST, false, 0},
    Keyword{u"c_fake_death", TK_C_FAKE_DEATH, false, 0},
    Keyword{u"c_hp", TK_C_HP, false, 0},
    Keyword{u"c_mp", TK_C_MP, false, 0},
    Keyword{u"c_mp_by_level", TK_C_MP_BY_LEVEL, false, 0},
    Keyword{u"c_rest", TK_C_REST, false, 0},
    Keyword{u"can_add_max_entrance_inzone", TK_CAN_ADD_MAX_ENTRANCE_INZONE, false, 0},
    Keyword{u"can_bookmark_add_slot", TK_CAN_BOOKMARK_ADD_SLOT, false, 0},
    Keyword{u"can_change_vital_item_count", TK_CAN_CHANGE_VITAL_ITEM_COUNT, false, 0},
    Keyword{u"can_enchant_attribute", TK_CAN_ENCHANT_ATTRIBUTE, false, 0},
    Keyword{u"can_mount_for_event", TK_CAN_MOUNT_FOR_EVENT, false, 0},
    Keyword{u"can_refuel_airship", TK_CAN_REFUEL_AIRSHIP, false, 0},
    Keyword{u"can_restore_vital_point", TK_CAN_RESTORE_VITAL_POINT, false, 0},
    Keyword{u"can_stop_consume_vital_point", TK_CAN_STOP_CONSUME_VITAL_POINT, false, 0},
    Keyword{u"can_summon", TK_CAN_SUMMON, false, 0},
    Keyword{u"can_summon_cubic", TK_CAN_SUMMON_CUBIC, false, 0},
    Keyword{u"can_summon_multi", TK_CAN_SUMMON_MULTI, false, 0},
    Keyword{u"can_summon_pet", TK_CAN_SUMMON_PET, false, 0},
    Keyword{u"can_summon_siege_golem", TK_CAN_SUMMON_SIEGE_GOLEM, false, 0},
    Keyword{u"can_transform", TK_CAN_TRANSFORM, false, 0},
    Keyword{u"can_transform_in_dominion", TK_CAN_TRANSFORM_IN_DOMINION, false, 0},
    Keyword{u"can_untransform", TK_CAN_UNTRANSFORM, false, 0},
    Keyword{u"can_use_in_battlefield", TK_CAN_USE_IN_BATTLEFIELD, false, 0},
    Keyword{u"can_use_in_dragon_lair", TK_CAN_USE_IN_DRAGON_LAIR, false, 0},
    Keyword{u"can_use_in_transform", TK_CAN_USE_IN_TRANSFORM, false, 0},
    Keyword{u"can_use_swoop_cannon", TK_CAN_USE_SWOOP_CANNON, false, 0},
    Keyword{u"can_use_vitality_consume_item", TK_CAN_USE_VITALITY_CONSUME_ITEM, false, 0},
    Keyword{u"cannot_use_in_transform", TK_CANNOT_USE_IN_TRANSFORM, false, 0},
    Keyword{u"cast_range", TK_CAST_RANGE, false, 0},
    Keyword{u"category", TK_CATEGORY, false, 0},
    Keyword{u"change_skill_id", TK_CHANGE_SKILL_ID, false, 0},
    Keyword{u"channeling_start", TK_CHANNELING_START, false, 0},
    Keyword{u"charge", TK_SINGLE_GROUP, true, 13},
    Keyword{u"charge_direction", TK_CHARGE_DIRECTION, false, 0},
    Keyword{u"check_level", TK_CHECK_LEVEL, false, 0},
    Keyword{u"check_sex", TK_CHECK_SEX, false, 0},
    Keyword{u"clan", TK_UNDEAD_REAL_ENEMY_GROUP, true, 6},
    Keyword{u"cond_p_castle_siege", TK_COND_P_CASTLE_SIEGE, false, 0},
    Keyword{u"cond_p_olympiad_pledge", TK_COND_P_OLYMPIAD_PLEDGE, false, 0},
    Keyword{u"cond_p_over_hp", TK_COND_P_OVER_HP, false, 0},
    Keyword{u"consume_body", TK_CONSUME_BODY, false, 0},
    Keyword{u"consume_etc", TK_CONSUME_ETC, false, 0},
    Keyword{u"crossbow", TK_SWORD_GROUP, true, 17},
    Keyword{u"cub_attack_speed", TK_CUB_ATTACK_SPEED, false, 0},
    Keyword{u"cub_block_act", TK_CUB_BLOCK_ACT, false, 0},
    Keyword{u"cub_heal", TK_CUB_HEAL, false, 0},
    Keyword{u"cub_hp", TK_CUB_HP, false, 0},
    Keyword{u"cub_hp_drain", TK_CUB_HP_DRAIN, false, 0},
    Keyword{u"cub_m_attack", TK_CUB_M_ATTACK, false, 0},
    Keyword{u"cub_physical_attack", TK_CUB_PHYSICAL_ATTACK, false, 0},
    Keyword{u"cub_physical_defence", TK_CUB_PHYSICAL_DEFENCE, false, 0},
    Keyword{u"dagger", TK_SWORD_GROUP, true, 8},
    Keyword{u"damage_proportion", TK_DAMAGE_PROPORTION, false, 0},
    Keyword{u"dead_party", TK_SINGLE_GROUP, true, 22},
    Keyword{u"dead_party_pledge", TK_SINGLE_GROUP, true, 23},
    Keyword{u"dead_pledge", TK_SINGLE_GROUP, true, 10},
    Keyword{u"dead_union", TK_SINGLE_GROUP, true, 18},
    Keyword{u"debuff", TK_DEBUFF, false, 0},
    Keyword{u"diff", TK_PER_GROUP, true, 1},
    Keyword{u"direct", TK_NOT_APPLIED_DIRECT_INVERSE, true, 1},
    Keyword{u"door_treasure", TK_SELF_GROUP, true, 10},
    Keyword{u"double_cast_skill", TK_DOUBLE_CAST_SKILL, false, 0},
    Keyword{u"dual", TK_SWORD_GROUP, true, 13},
    Keyword{u"dualblunt", TK_SWORD_GROUP, true, 23},
    Keyword{u"dualdagger", TK_SWORD_GROUP, true, 20},
    Keyword{u"dualfist", TK_SWORD_GROUP, true, 14},
    Keyword{u"effect", TK_EFFECT, false, 0},
    Keyword{u"effect_point", TK_EFFECT_POINT, false, 0},
    Keyword{u"effective_range", TK_EFFECTIVE_RANGE, false, 0},
    Keyword{u"end_effect", TK_END_EFFECT, false, 0},
    Keyword{u"enemy", TK_SELF_GROUP, true, 4},
    Keyword{u"enemy_all", TK_PK_GROUP, true, 3},
    Keyword{u"enemy_not", TK_SELF_GROUP, true, 19},
    Keyword{u"enemy_only", TK_SELF_GROUP, true, 5},
    Keyword{u"energy_saved", TK_ENERGY_SAVED, false, 0},
    Keyword{u"equip_armor", TK_EQUIP_ARMOR, false, 0},
    Keyword{u"equip_shield", TK_EQUIP_SHIELD, false, 0},
    Keyword{u"equip_weapon", TK_EQUIP_WEAPON, false, 0},
    Keyword{u"event_type", TK_EVENT_TYPE, false, 0},
    Keyword{u"except_block", TK_EXCEPT_BLOCK, false, 0},
    Keyword{u"except_restore", TK_EXCEPT_RESTORE, false, 0},
    Keyword{u"exception_self", TK_SELF_GROUP, true, 20},
    Keyword{u"fail_effect", TK_FAIL_EFFECT, false, 0},
    Keyword{u"fan", TK_SINGLE_GROUP, true, 5},
    Keyword{u"fan_pb", TK_SINGLE_GROUP, true, 8},
    Keyword{u"fan_range", TK_FAN_RANGE, false, 0},
    Keyword{u"fist", TK_SWORD_GROUP, true, 10},
    Keyword{u"fortress_flagpole", TK_SELF_GROUP, true, 16},
    Keyword{u"friend", TK_UNDEAD_REAL_ENEMY_GROUP, true, 1},
    Keyword{u"friend_pc", TK_UNDEAD_REAL_ENEMY_GROUP, true, 10},
    Keyword{u"front", TK_FRONT_GROUP, true, 0},
    Keyword{u"ground", TK_SELF_GROUP, true, 14},
    Keyword{u"hidden_place", TK_UNDEAD_REAL_ENEMY_GROUP, true, 9},
    Keyword{u"holything", TK_SELF_GROUP, true, 8},
    Keyword{u"hp_consume", TK_HP_CONSUME, false, 0},
    Keyword{u"hp_consume_tick", TK_HP_CONSUME_TICK, false, 0},
    Keyword{u"i_abnormal_time_change", TK_I_ABNORMAL_TIME_CHANGE, false, 0},
    Keyword{u"i_acc_change", TK_I_ACC_CHANGE, false, 0},
    Keyword{u"i_add_enchant_armor_level_inc", TK_I_ADD_ENCHANT_ARMOR_LEVEL_INC, false, 0},
    Keyword{u"i_add_enchant_weapon_level_inc", TK_I_ADD_ENCHANT_WEAPON_LEVEL_INC, false, 0},
    Keyword{u"i_add_event_contribute", TK_I_ADD_EVENT_CONTRIBUTE, false, 0},
    Keyword{u"i_add_hate", TK_I_ADD_HATE, false, 0},
    Keyword{u"i_add_hate_mymaster", TK_I_ADD_HATE_MYMASTER, false, 0},
    Keyword{u"i_add_max_entrance_inzone", TK_I_ADD_MAX_ENTRANCE_INZONE, false, 0},
    Keyword{u"i_agathion_energy", TK_I_AGATHION_ENERGY, false, 0},
    Keyword{u"i_align_direction", TK_I_ALIGN_DIRECTION, false, 0},
    Keyword{u"i_backstab", TK_I_BACKSTAB, false, 0},
    Keyword{u"i_betray", TK_I_BETRAY, false, 0},
    Keyword{u"i_bless_shape_shift_am", TK_I_BLESS_SHAPE_SHIFT_AM, false, 0},
    Keyword{u"i_bless_shape_shift_wp", TK_I_BLESS_SHAPE_SHIFT_WP, false, 0},
    Keyword{u"i_blink", TK_I_BLINK, false, 0},
    Keyword{u"i_blink_party", TK_I_BLINK_PARTY, false, 0},
    Keyword{u"i_blink_return", TK_I_BLINK_RETURN, false, 0},
    Keyword{u"i_bonuscount_up", TK_I_BONUSCOUNT_UP, false, 0},
    Keyword{u"i_bookmark_add_slot", TK_I_BOOKMARK_ADD_SLOT, false, 0},
    Keyword{u"i_bookmark_teleport", TK_I_BOOKMARK_TELEPORT, false, 0},
    Keyword{u"i_call_my_skill_list", TK_I_CALL_MY_SKILL_LIST, false, 0},
    Keyword{u"i_call_party", TK_I_CALL_PARTY, false, 0},
    Keyword{u"i_call_pc", TK_I_CALL_PC, false, 0},
    Keyword{u"i_call_random_skill_consume_item", TK_I_CALL_RANDOM_SKILL_CONSUME_ITEM, false, 0},
    Keyword{u"i_call_skill", TK_I_CALL_SKILL, false, 0},
    Keyword{u"i_call_skill_by_abnormal_slot", TK_I_CALL_SKILL_BY_ABNORMAL_SLOT, false, 0},
    Keyword{u"i_call_skill_by_force", TK_I_CALL_SKILL_BY_FORCE, false, 0},
    Keyword{u"i_call_skill_by_target", TK_I_CALL_SKILL_BY_TARGET, false, 0},
    Keyword{u"i_call_skill_by_target_divide", TK_I_CALL_SKILL_BY_TARGET_DIVIDE, false, 0},
    Keyword{u"i_call_skill_by_targetdirection", TK_I_CALL_SKILL_BY_TARGETDIRECTION, false, 0},
    Keyword{u"i_call_target_party", TK_I_CALL_TARGET_PARTY, false, 0},
    Keyword{u"i_capture_flag", TK_I_CAPTURE_FLAG, false, 0},
    Keyword{u"i_capture_flag_start", TK_I_CAPTURE_FLAG_START, false, 0},
    Keyword{u"i_capture_ownthing", TK_I_CAPTURE_OWNTHING, false, 0},
    Keyword{u"i_capture_ownthing_start", TK_I_CAPTURE_OWNTHING_START, false, 0},
    Keyword{u"i_change_attribute", TK_I_CHANGE_ATTRIBUTE, false, 0},
    Keyword{u"i_change_class", TK_I_CHANGE_CLASS, false, 0},
    Keyword{u"i_change_face", TK_I_CHANGE_FACE, false, 0},
    Keyword{u"i_change_hair_color", TK_I_CHANGE_HAIR_COLOR, false, 0},
    Keyword{u"i_change_hair_style", TK_I_CHANGE_HAIR_STYLE, false, 0},
    Keyword{u"i_change_skill_level", TK_I_CHANGE_SKILL_LEVEL, false, 0},
    Keyword{u"i_change_vital_item_count", TK_I_CHANGE_VITAL_ITEM_COUNT, false, 0},
    Keyword{u"i_collecting", TK_I_COLLECTING, false, 0},
    Keyword{u"i_compulsion_social_action", TK_I_COMPULSION_SOCIAL_ACTION, false, 0},
    Keyword{u"i_confuse", TK_I_CONFUSE, false, 0},
    Keyword{u"i_consume_body", TK_I_CONSUME_BODY, false, 0},
    Keyword{u"i_consume_summon", TK_I_CONSUME_SUMMON, false, 0},
    Keyword{u"i_convert_item", TK_I_CONVERT_ITEM, false, 0},
    Keyword{u"i_cp", TK_I_CP, false, 0},
    Keyword{u"i_cp_per_max", TK_I_CP_PER_MAX, false, 0},
    Keyword{u"i_create_item_random", TK_I_CREATE_ITEM_RANDOM, false, 0},
    Keyword{u"i_death", TK_I_DEATH, false, 0},
    Keyword{u"i_death_link", TK_I_DEATH_LINK, false, 0},
    Keyword{u"i_death_link_mp", TK_I_DEATH_LINK_MP, false, 0},
    Keyword{u"i_defuse_trap", TK_I_DEFUSE_TRAP, false, 0},
    Keyword{u"i_delete_hate", TK_I_DELETE_HATE, false, 0},
    Keyword{u"i_delete_hate_of_me", TK_I_DELETE_HATE_OF_ME, false, 0},
    Keyword{u"i_delete_topagro", TK_I_DELETE_TOPAGRO, false, 0},
    Keyword{u"i_despawn", TK_I_DESPAWN, false, 0},
    Keyword{u"i_detect_object", TK_I_DETECT_OBJECT, false, 0},
    Keyword{u"i_detect_trap", TK_I_DETECT_TRAP, false, 0},
    Keyword{u"i_dismount_for_event", TK_I_DISMOUNT_FOR_EVENT, false, 0},
    Keyword{u"i_dispel_all", TK_I_DISPEL_ALL, false, 0},
    Keyword{u"i_dispel_by_category", TK_I_DISPEL_BY_CATEGORY, false, 0},
    Keyword{u"i_dispel_by_name", TK_I_DISPEL_BY_NAME, false, 0},
    Keyword{u"i_dispel_by_slot", TK_I_DISPEL_BY_SLOT, false, 0},
    Keyword{u"i_dispel_by_slot_myself", TK_I_DISPEL_BY_SLOT_MYSELF, false, 0},
    Keyword{u"i_dispel_by_slot_probability", TK_I_DISPEL_BY_SLOT_PROBABILITY, false, 0},
    Keyword{u"i_distrust", TK_I_DISTRUST, false, 0},
    Keyword{u"i_enchant_armor", TK_I_ENCHANT_ARMOR, false, 0},
    Keyword{u"i_enchant_armor_rate", TK_I_ENCHANT_ARMOR_RATE, false, 0},
    Keyword{u"i_enchant_attribute", TK_I_ENCHANT_ATTRIBUTE, false, 0},
    Keyword{u"i_enchant_attribution_item", TK_I_ENCHANT_ATTRIBUTION_ITEM, false, 0},
    Keyword{u"i_enchant_item_multi", TK_I_ENCHANT_ITEM_MULTI, false, 0},
    Keyword{u"i_enchant_weapon", TK_I_ENCHANT_WEAPON, false, 0},
    Keyword{u"i_enchant_weapon_rate", TK_I_ENCHANT_WEAPON_RATE, false, 0},
    Keyword{u"i_energy_attack", TK_I_ENERGY_ATTACK, false, 0},
    Keyword{u"i_ensoul", TK_I_ENSOUL, false, 0},
    Keyword{u"i_escape", TK_I_ESCAPE, false, 0},
    Keyword{u"i_ev_ink", TK_I_EV_INK, false, 0},
    Keyword{u"i_ev_ink_party", TK_I_EV_INK_PARTY, false, 0},
    Keyword{u"i_event_agathion_reuse_delay", TK_I_EVENT_AGATHION_REUSE_DELAY, false, 0},
    Keyword{u"i_event_attack", TK_I_EVENT_ATTACK, false, 0},
    Keyword{u"i_fatal_blow", TK_I_FATAL_BLOW, false, 0},
    Keyword{u"i_fatal_blow_abnormal", TK_I_FATAL_BLOW_ABNORMAL, false, 0},
    Keyword{u"i_fishing_cast", TK_I_FISHING_CAST, false, 0},
    Keyword{u"i_fishing_instantkill", TK_I_FISHING_INSTANTKILL, false, 0},
    Keyword{u"i_fishing_pumping", TK_I_FISHING_PUMPING, false, 0},
    Keyword{u"i_fishing_reeling", TK_I_FISHING_REELING, false, 0},
    Keyword{u"i_fishing_shot", TK_I_FISHING_SHOT, false, 0},
    Keyword{u"i_float_away", TK_I_FLOAT_AWAY, false, 0},
    Keyword{u"i_fly_away", TK_I_FLY_AWAY, false, 0},
    Keyword{u"i_fly_self", TK_I_FLY_SELF, false, 0},
    Keyword{u"i_focus_energy", TK_I_FOCUS_ENERGY, false, 0},
    Keyword{u"i_focus_max_energy", TK_I_FOCUS_MAX_ENERGY, false, 0},
    Keyword{u"i_focus_soul", TK_I_FOCUS_SOUL, false, 0},
    Keyword{u"i_food_for_pet", TK_I_FOOD_FOR_PET, false, 0},
    Keyword{u"i_force_sitdown", TK_I_FORCE_SITDOWN, false, 0},
    Keyword{u"i_get_agro", TK_I_GET_AGRO, false, 0},
    Keyword{u"i_get_exp", TK_I_GET_EXP, false, 0},
    Keyword{u"i_get_ultimate_skill_gauge", TK_I_GET_ULTIMATE_SKILL_GAUGE, false, 0},
    Keyword{u"i_give_contribution", TK_I_GIVE_CONTRIBUTION, false, 0},
    Keyword{u"i_goto_target", TK_I_GOTO_TARGET, false, 0},
    Keyword{u"i_harvesting", TK_I_HARVESTING, false, 0},
    Keyword{u"i_heal", TK_I_HEAL, false, 0},
    Keyword{u"i_heal_link", TK_I_HEAL_LINK, false, 0},
    Keyword{u"i_heal_special", TK_I_HEAL_SPECIAL, false, 0},
    Keyword{u"i_holything_possess", TK_I_HOLYTHING_POSSESS, false, 0},
    Keyword{u"i_hp", TK_I_HP, false, 0},
    Keyword{u"i_hp_by_level_self", TK_I_HP_BY_LEVEL_SELF, false, 0},
    Keyword{u"i_hp_down", TK_I_HP_DOWN, false, 0},
    Keyword{u"i_hp_down_self", TK_I_HP_DOWN_SELF, false, 0},
    Keyword{u"i_hp_drain", TK_I_HP_DRAIN, false, 0},
    Keyword{u"i_hp_per_max", TK_I_HP_PER_MAX, false, 0},
    Keyword{u"i_hp_self", TK_I_HP_SELF, false, 0},
    Keyword{u"i_hp_special", TK_I_HP_SPECIAL, false, 0},
    Keyword{u"i_inform", TK_I_INFORM, false, 0},
    Keyword{u"i_install_advance_base", TK_I_INSTALL_ADVANCE_BASE, false, 0},
    Keyword{u"i_install_camp", TK_I_INSTALL_CAMP, false, 0},
    Keyword{u"i_install_camp_ex", TK_I_INSTALL_CAMP_EX, false, 0},
    Keyword{u"i_karma_count", TK_I_KARMA_COUNT, false, 0},
    Keyword{u"i_knockback", TK_I_KNOCKBACK, false, 0},
    Keyword{u"i_locked_item", TK_I_LOCKED_ITEM, false, 0},
    Keyword{u"i_m_attack", TK_I_M_ATTACK, false, 0},
    Keyword{u"i_m_attack_by_abnormal", TK_I_M_ATTACK_BY_ABNORMAL, false, 0},
    Keyword{u"i_m_attack_by_abnormal_slot", TK_I_M_ATTACK_BY_ABNORMAL_SLOT, false, 0},
    Keyword{u"i_m_attack_by_dist", TK_I_M_ATTACK_BY_DIST, false, 0},
    Keyword{u"i_m_attack_by_hp", TK_I_M_ATTACK_BY_HP, false, 0},
    Keyword{u"i_m_attack_by_range", TK_I_M_ATTACK_BY_RANGE, false, 0},
    Keyword{u"i_m_attack_debuff_dmg", TK_I_M_ATTACK_DEBUFF_DMG, false, 0},
    Keyword{u"i_m_attack_link", TK_I_M_ATTACK_LINK, false, 0},
    Keyword{u"i_m_attack_mp", TK_I_M_ATTACK_MP, false, 0},
    Keyword{u"i_m_attack_over_hit", TK_I_M_ATTACK_OVER_HIT, false, 0},
    Keyword{u"i_m_attack_over_hit_range", TK_I_M_ATTACK_OVER_HIT_RANGE, false, 0},
    Keyword{u"i_m_attack_range", TK_I_M_ATTACK_RANGE, false, 0},
    Keyword{u"i_m_soul_attack", TK_I_M_SOUL_ATTACK, false, 0},
    Keyword{u"i_mount_for_event", TK_I_MOUNT_FOR_EVENT, false, 0},
    Keyword{u"i_mp", TK_I_MP, false, 0},
    Keyword{u"i_mp_by_hp", TK_I_MP_BY_HP, false, 0},
    Keyword{u"i_mp_by_level", TK_I_MP_BY_LEVEL, false, 0},
    Keyword{u"i_mp_by_level_self", TK_I_MP_BY_LEVEL_SELF, false, 0},
    Keyword{u"i_mp_per_max", TK_I_MP_PER_MAX, false, 0},
    Keyword{u"i_my_summon_kill", TK_I_MY_SUMMON_KILL, false, 0},
    Keyword{u"i_npc_kill", TK_I_NPC_KILL, false, 0},
    Keyword{u"i_open_common_recipebook", TK_I_OPEN_COMMON_RECIPEBOOK, false, 0},
    Keyword{u"i_open_dwarf_recipebook", TK_I_OPEN_DWARF_RECIPEBOOK, false, 0},
    Keyword{u"i_p_attack", TK_I_P_ATTACK, false, 0},
    Keyword{u"i_p_attack_abnormal", TK_I_P_ATTACK_ABNORMAL, false, 0},
    Keyword{u"i_p_attack_by_dist", TK_I_P_ATTACK_BY_DIST, false, 0},
    Keyword{u"i_p_attack_save_hp", TK_I_P_ATTACK_SAVE_HP, false, 0},
    Keyword{u"i_p_attack_weapon_bonus", TK_I_P_ATTACK_WEAPON_BONUS, false, 0},
    Keyword{u"i_p_percent_attack", TK_I_P_PERCENT_ATTACK, false, 0},
    Keyword{u"i_p_soul_attack", TK_I_P_SOUL_ATTACK, false, 0},
    Keyword{u"i_party_teleport_to_npc", TK_I_PARTY_TELEPORT_TO_NPC, false, 0},
    Keyword{u"i_pcbang_point_up", TK_I_PCBANG_POINT_UP, false, 0},
    Keyword{u"i_physical_attack_hp_link", TK_I_PHYSICAL_ATTACK_HP_LINK, false, 0},
    Keyword{u"i_pk_count", TK_I_PK_COUNT, false, 0},
    Keyword{u"i_pledge_send_system_message", TK_I_PLEDGE_SEND_SYSTEM_MESSAGE, false, 0},
    Keyword{u"i_plunder", TK_I_PLUNDER, false, 0},
    Keyword{u"i_position_change", TK_I_POSITION_CHANGE, false, 0},
    Keyword{u"i_pull", TK_I_PULL, false, 0},
    Keyword{u"i_pvppoint", TK_I_PVPPOINT, false, 0},
    Keyword{u"i_random_call_skill", TK_I_RANDOM_CALL_SKILL, false, 0},
    Keyword{u"i_random_target", TK_I_RANDOM_TARGET, false, 0},
    Keyword{u"i_randomize_hate", TK_I_RANDOMIZE_HATE, false, 0},
    Keyword{u"i_real_damage", TK_I_REAL_DAMAGE, false, 0},
    Keyword{u"i_rebalance_hp", TK_I_REBALANCE_HP, false, 0},
    Keyword{u"i_rebalance_hp_summon", TK_I_REBALANCE_HP_SUMMON, false, 0},
    Keyword{u"i_rebalance_mp", TK_I_REBALANCE_MP, false, 0},
    Keyword{u"i_refuel_airship", TK_I_REFUEL_AIRSHIP, false, 0},
    Keyword{u"i_register_siege_golem", TK_I_REGISTER_SIEGE_GOLEM, false, 0},
    Keyword{u"i_remote_control_trap", TK_I_REMOTE_CONTROL_TRAP, false, 0},
    Keyword{u"i_remove_energy", TK_I_REMOVE_ENERGY, false, 0},
    Keyword{u"i_remove_soul", TK_I_REMOVE_SOUL, false, 0},
    Keyword{u"i_reset_quest", TK_I_RESET_QUEST, false, 0},
    Keyword{u"i_restoration", TK_I_RESTORATION, false, 0},
    Keyword{u"i_restoration_addrandom", TK_I_RESTORATION_ADDRANDOM, false, 0},
    Keyword{u"i_restoration_random", TK_I_RESTORATION_RANDOM, false, 0},
    Keyword{u"i_restoration_random_group", TK_I_RESTORATION_RANDOM_GROUP, false, 0},
    Keyword{u"i_restore_shape_shift_item", TK_I_RESTORE_SHAPE_SHIFT_ITEM, false, 0},
    Keyword{u"i_restore_vital_point", TK_I_RESTORE_VITAL_POINT, false, 0},
    Keyword{u"i_restore_vital_point_noncount", TK_I_RESTORE_VITAL_POINT_NONCOUNT, false, 0},
    Keyword{u"i_resurrection", TK_I_RESURRECTION, false, 0},
    Keyword{u"i_reuse_skill_reduce", TK_I_REUSE_SKILL_REDUCE, false, 0},
    Keyword{u"i_reuse_skill_reset", TK_I_REUSE_SKILL_RESET, false, 0},
    Keyword{u"i_run_away", TK_I_RUN_AWAY, false, 0},
    Keyword{u"i_set_return_pos", TK_I_SET_RETURN_POS, false, 0},
    Keyword{u"i_set_reuse_delay", TK_I_SET_REUSE_DELAY, false, 0},
    Keyword{u"i_set_skill", TK_I_SET_SKILL, false, 0},
    Keyword{u"i_set_visible", TK_I_SET_VISIBLE, false, 0},
    Keyword{u"i_shape_shift_am", TK_I_SHAPE_SHIFT_AM, false, 0},
    Keyword{u"i_shape_shift_am_fixed", TK_I_SHAPE_SHIFT_AM_FIXED, false, 0},
    Keyword{u"i_shape_shift_item_auto_matching", TK_I_SHAPE_SHIFT_ITEM_AUTO_MATCHING, false, 0},
    Keyword{u"i_shape_shift_wp", TK_I_SHAPE_SHIFT_WP, false, 0},
    Keyword{u"i_shape_shift_wp_fixed", TK_I_SHAPE_SHIFT_WP_FIXED, false, 0},
    Keyword{u"i_show_reuse_delay", TK_I_SHOW_REUSE_DELAY, false, 0},
    Keyword{u"i_show_usable_daily_skill_count", TK_I_SHOW_USABLE_DAILY_SKILL_COUNT, false, 0},
    Keyword{u"i_sidestep_to_target", TK_I_SIDESTEP_TO_TARGET, false, 0},
    Keyword{u"i_skill_turning", TK_I_SKILL_TURNING, false, 0},
    Keyword{u"i_soul_blow", TK_I_SOUL_BLOW, false, 0},
    Keyword{u"i_soul_shot", TK_I_SOUL_SHOT, false, 0},
    Keyword{u"i_sowing", TK_I_SOWING, false, 0},
    Keyword{u"i_sp", TK_I_SP, false, 0},
    Keyword{u"i_spirit_shot", TK_I_SPIRIT_SHOT, false, 0},
    Keyword{u"i_spoil", TK_I_SPOIL, false, 0},
    Keyword{u"i_steal_abnormal", TK_I_STEAL_ABNORMAL, false, 0},
    Keyword{u"i_sub_enchant_armor_level_dec", TK_I_SUB_ENCHANT_ARMOR_LEVEL_DEC, false, 0},
    Keyword{u"i_sub_enchant_weapon_level_dec", TK_I_SUB_ENCHANT_WEAPON_LEVEL_DEC, false, 0},
    Keyword{u"i_suicide", TK_I_SUICIDE, false, 0},
    Keyword{u"i_summon", TK_I_SUMMON, false, 0},
    Keyword{u"i_summon_agathion", TK_I_SUMMON_AGATHION, false, 0},
    Keyword{u"i_summon_cubic", TK_I_SUMMON_CUBIC, false, 0},
    Keyword{u"i_summon_hallucination", TK_I_SUMMON_HALLUCINATION, false, 0},
    Keyword{u"i_summon_multi", TK_I_SUMMON_MULTI, false, 0},
    Keyword{u"i_summon_npc", TK_I_SUMMON_NPC, false, 0},
    Keyword{u"i_summon_npc_dying", TK_I_SUMMON_NPC_DYING, false, 0},
    Keyword{u"i_summon_npc_near", TK_I_SUMMON_NPC_NEAR, false, 0},
    Keyword{u"i_summon_npc_position", TK_I_SUMMON_NPC_POSITION, false, 0},
    Keyword{u"i_summon_pet", TK_I_SUMMON_PET, false, 0},
    Keyword{u"i_summon_pledge", TK_I_SUMMON_PLEDGE, false, 0},
    Keyword{u"i_summon_soul_shot", TK_I_SUMMON_SOUL_SHOT, false, 0},
    Keyword{u"i_summon_spirit_shot", TK_I_SUMMON_SPIRIT_SHOT, false, 0},
    Keyword{u"i_summon_target_to_me", TK_I_SUMMON_TARGET_TO_ME, false, 0},
    Keyword{u"i_summon_territory", TK_I_SUMMON_TERRITORY, false, 0},
    Keyword{u"i_summon_trap", TK_I_SUMMON_TRAP, false, 0},
    Keyword{u"i_summon_unique_npc", TK_I_SUMMON_UNIQUE_NPC, false, 0},
    Keyword{u"i_sweeper", TK_I_SWEEPER, false, 0},
    Keyword{u"i_target_cancel", TK_I_TARGET_CANCEL, false, 0},
    Keyword{u"i_target_me", TK_I_TARGET_ME, false, 0},
    Keyword{u"i_teleport", TK_I_TELEPORT, false, 0},
    Keyword{u"i_teleport_to_npc", TK_I_TELEPORT_TO_NPC, false, 0},
    Keyword{u"i_teleport_to_summon", TK_I_TELEPORT_TO_SUMMON, false, 0},
    Keyword{u"i_teleport_to_target", TK_I_TELEPORT_TO_TARGET, false, 0},
    Keyword{u"i_transfer_abnormal", TK_I_TRANSFER_ABNORMAL, false, 0},
    Keyword{u"i_transfer_hate", TK_I_TRANSFER_HATE, false, 0},
    Keyword{u"i_uninstall_advance_base", TK_I_UNINSTALL_ADVANCE_BASE, false, 0},
    Keyword{u"i_unlock", TK_I_UNLOCK, false, 0},
    Keyword{u"i_unlocked_item", TK_I_UNLOCKED_ITEM, false, 0},
    Keyword{u"i_unsummon_agathion", TK_I_UNSUMMON_AGATHION, false, 0},
    Keyword{u"ia_earth", TK_IA_VALUE, true, 3},
    Keyword{u"ia_fire", TK_IA_VALUE, true, 0},
    Keyword{u"ia_holy", TK_IA_VALUE, true, 4},
    Keyword{u"ia_unholy", TK_IA_VALUE, true, 5},
    Keyword{u"ia_water", TK_IA_VALUE, true, 1},
    Keyword{u"ia_wind", TK_IA_VALUE, true, 2},
    Keyword{u"inmaster", TK_INSELF_INMASTER, true, 1},
    Keyword{u"inself", TK_INSELF_INMASTER, true, 0},
    Keyword{u"inventory_normal", TK_INVENTORY_NORMAL_GROUP, true, 0},
    Keyword{u"inventory_waist", TK_INVENTORY_NORMAL_GROUP, true, 7},
    Keyword{u"inverse", TK_NOT_APPLIED_DIRECT_INVERSE, true, 2},
    Keyword{u"invisible", TK_UNDEAD_REAL_ENEMY_GROUP, true, 8},
    Keyword{u"irreplaceable_buff", TK_IRREPLACEABLE_BUFF, false, 0},
    Keyword{u"is_agathion_main_skill", TK_IS_AGATHION_MAIN_SKILL, false, 0},
    Keyword{u"is_double", TK_IS_DOUBLE, false, 0},
    Keyword{u"is_hate", TK_IS_HATE, false, 0},
    Keyword{u"is_magic", TK_IS_MAGIC, false, 0},
    Keyword{u"is_necessarytg", TK_IS_NECESSARYTG, false, 0},
    Keyword{u"item", TK_SELF_GROUP, true, 6},
    Keyword{u"item_consume", TK_ITEM_CONSUME, false, 0},
    Keyword{u"left", TK_FRONT_GROUP, true, 3},
    Keyword{u"level", TK_LEVEL, false, 0},
    Keyword{u"lv_bonus_rate", TK_LV_BONUS_RATE, false, 0},
    Keyword{u"magic_critical_rate", TK_MAGIC_CRITICAL_RATE, false, 0},
    Keyword{u"magic_level", TK_MAGIC_LEVEL, false, 0},
    Keyword{u"mob", TK_PK_GROUP, true, 1},
    Keyword{u"mp_consume1", TK_MP_CONSUME1, false, 0},
    Keyword{u"mp_consume2", TK_MP_CONSUME2, false, 0},
    Keyword{u"mp_consume_tick", TK_MP_CONSUME_TICK, false, 0},
    Keyword{u"multi_class", TK_MULTI_CLASS, false, 0},
    Keyword{u"my_mentor", TK_SELF_GROUP, true, 21},
    Keyword{u"my_party", TK_SELF_GROUP, true, 3},
    Keyword{u"my_pledge", TK_SELF_GROUP, true, 9},
    Keyword{u"neutral_invisible", TK_UNDEAD_REAL_ENEMY_GROUP, true, 12},
    Keyword{u"next_action", TK_NEXT_ACTION, false, 0},
    Keyword{u"no_transfer_abnormal", TK_NO_TRANSFER_ABNORMAL, false, 0},
    Keyword{u"not_applied", TK_NOT_APPLIED_DIRECT_INVERSE, true, 0},
    Keyword{u"not_friend", TK_UNDEAD_REAL_ENEMY_GROUP, true, 2},
    Keyword{u"not_friend_pc", TK_UNDEAD_REAL_ENEMY_GROUP, true, 11},
    Keyword{u"not_in_underwater", TK_NOT_IN_UNDERWATER, false, 0},
    Keyword{u"npc_body", TK_SELF_GROUP, true, 12},
    Keyword{u"npc_notice", TK_NPC_NOTICE, false, 0},
    Keyword{u"object_dead_npc_body", TK_UNDEAD_REAL_ENEMY_GROUP, true, 7},
    Keyword{u"oct_both", TK_OCT_VALUE, true, 3},
    Keyword{u"oct_caster", TK_OCT_VALUE, true, 0},
    Keyword{u"oct_target", TK_OCT_VALUE, true, 1},
    Keyword{u"olympiad_use", TK_OLYMPIAD_USE, false, 0},
    Keyword{u"op_2h_weapon", TK_OP_2H_WEAPON, false, 0},
    Keyword{u"op_agathion_energy", TK_OP_AGATHION_ENERGY, false, 0},
    Keyword{u"op_alignment", TK_OP_ALIGNMENT, false, 0},
    Keyword{u"op_blink", TK_OP_BLINK, false, 0},
    Keyword{u"op_call_pc", TK_OP_CALL_PC, false, 0},
    Keyword{u"op_can_disarm", TK_OP_CAN_DISARM, false, 0},
    Keyword{u"op_can_escape", TK_OP_CAN_ESCAPE, false, 0},
    Keyword{u"op_can_go", TK_OP_CAN_GO, false, 0},
    Keyword{u"op_can_not_use_airship", TK_OP_CAN_NOT_USE_AIRSHIP, false, 0},
    Keyword{u"op_cannot_use_target_with_private_store", TK_OP_CANNOT_USE_TARGET_WITH_PRIVATE_STORE, false, 0},
    Keyword{u"op_change_weapon", TK_OP_CHANGE_WEAPON, false, 0},
    Keyword{u"op_charge_self", TK_OP_CHARGE_SELF, false, 0},
    Keyword{u"op_check_abnormal", TK_OP_CHECK_ABNORMAL, false, 0},
    Keyword{u"op_check_account_type", TK_OP_CHECK_ACCOUNT_TYPE, false, 0},
    Keyword{u"op_check_cast_range", TK_OP_CHECK_CAST_RANGE, false, 0},
    Keyword{u"op_check_class", TK_OP_CHECK_CLASS, false, 0},
    Keyword{u"op_check_class_list", TK_OP_CHECK_CLASS_LIST, false, 0},
    Keyword{u"op_check_crt_effect", TK_OP_CHECK_CRT_EFFECT, false, 0},
    Keyword{u"op_check_equip_slot", TK_OP_CHECK_EQUIP_SLOT, false, 0},
    Keyword{u"op_check_flag", TK_OP_CHECK_FLAG, false, 0},
    Keyword{u"op_check_nobless", TK_OP_CHECK_NOBLESS, false, 0},
    Keyword{u"op_check_not_registered_pledgegame", TK_OP_CHECK_NOT_REGISTERED_PLEDGEGAME, false, 0},
    Keyword{u"op_check_on_going_event_campaign", TK_OP_CHECK_ON_GOING_EVENT_CAMPAIGN, false, 0},
    Keyword{u"op_check_pcbang_point", TK_OP_CHECK_PCBANG_POINT, false, 0},
    Keyword{u"op_check_relative_height", TK_OP_CHECK_RELATIVE_HEIGHT, false, 0},
    Keyword{u"op_check_residence", TK_OP_CHECK_RESIDENCE, false, 0},
    Keyword{u"op_check_skill", TK_OP_CHECK_SKILL, false, 0},
    Keyword{u"op_check_stuck_npc", TK_OP_CHECK_STUCK_NPC, false, 0},
    Keyword{u"op_collecting", TK_OP_COLLECTING, false, 0},
    Keyword{u"op_combat", TK_OP_COMBAT, false, 0},
    Keyword{u"op_companion", TK_OP_COMPANION, false, 0},
    Keyword{u"op_enchant_range", TK_OP_ENCHANT_RANGE, false, 0},
    Keyword{u"op_encumbered", TK_OP_ENCUMBERED, false, 0},
    Keyword{u"op_energy_max", TK_OP_ENERGY_MAX, false, 0},
    Keyword{u"op_equip_agathion_main_slot", TK_OP_EQUIP_AGATHION_MAIN_SLOT, false, 0},
    Keyword{u"op_equip_item", TK_OP_EQUIP_ITEM, false, 0},
    Keyword{u"op_exist_npc", TK_OP_EXIST_NPC, false, 0},
    Keyword{u"op_fishing_cast", TK_OP_FISHING_CAST, false, 0},
    Keyword{u"op_fishing_pumping", TK_OP_FISHING_PUMPING, false, 0},
    Keyword{u"op_fishing_reeling", TK_OP_FISHING_REELING, false, 0},
    Keyword{u"op_harvest", TK_OP_HARVEST, false, 0},
    Keyword{u"op_have_not_summoned_npc", TK_OP_HAVE_NOT_SUMMONED_NPC, false, 0},
    Keyword{u"op_have_summon", TK_OP_HAVE_SUMMON, false, 0},
    Keyword{u"op_have_summoned_npc", TK_OP_HAVE_SUMMONED_NPC, false, 0},
    Keyword{u"op_home", TK_OP_HOME, false, 0},
    Keyword{u"op_in_siege_time", TK_OP_IN_SIEGE_TIME, false, 0},
    Keyword{u"op_instantzone", TK_OP_INSTANTZONE, false, 0},
    Keyword{u"op_mainjob", TK_OP_MAINJOB, false, 0},
    Keyword{u"op_need_agathion", TK_OP_NEED_AGATHION, false, 0},
    Keyword{u"op_need_summon", TK_OP_NEED_SUMMON, false, 0},
    Keyword{u"op_need_summon_or_pet", TK_OP_NEED_SUMMON_OR_PET, false, 0},
    Keyword{u"op_not_cursed", TK_OP_NOT_CURSED, false, 0},
    Keyword{u"op_not_instantzone", TK_OP_NOT_INSTANTZONE, false, 0},
    Keyword{u"op_not_olympiad", TK_OP_NOT_OLYMPIAD, false, 0},
    Keyword{u"op_not_territory", TK_OP_NOT_TERRITORY, false, 0},
    Keyword{u"op_not_territory_list", TK_OP_NOT_TERRITORY_LIST, false, 0},
    Keyword{u"op_peacezone", TK_OP_PEACEZONE, false, 0},
    Keyword{u"op_pkcount", TK_OP_PKCOUNT, false, 0},
    Keyword{u"op_pledge", TK_OP_PLEDGE, false, 0},
    Keyword{u"op_restart_point", TK_OP_RESTART_POINT, false, 0},
    Keyword{u"op_resurrection", TK_OP_RESURRECTION, false, 0},
    Keyword{u"op_return_pos", TK_OP_RETURN_POS, false, 0},
    Keyword{u"op_siege_hammer", TK_OP_SIEGE_HAMMER, false, 0},
    Keyword{u"op_skill", TK_OP_SKILL, false, 0},
    Keyword{u"op_skill_acquire", TK_OP_SKILL_ACQUIRE, false, 0},
    Keyword{u"op_social_class", TK_OP_SOCIAL_CLASS, false, 0},
    Keyword{u"op_soul_max", TK_OP_SOUL_MAX, false, 0},
    Keyword{u"op_subjob", TK_OP_SUBJOB, false, 0},
    Keyword{u"op_sweeper", TK_OP_SWEEPER, false, 0},
    Keyword{u"op_target_all_item_type", TK_OP_TARGET_ALL_ITEM_TYPE, false, 0},
    Keyword{u"op_target_armor_type", TK_OP_TARGET_ARMOR_TYPE, false, 0},
    Keyword{u"op_target_my_pledge_academy", TK_OP_TARGET_MY_PLEDGE_ACADEMY, false, 0},
    Keyword{u"op_target_npc", TK_OP_TARGET_NPC, false, 0},
    Keyword{u"op_target_pc", TK_OP_TARGET_PC, false, 0},
    Keyword{u"op_target_weapon_attack_type", TK_OP_TARGET_WEAPON_ATTACK_TYPE, false, 0},
    Keyword{u"op_territory", TK_OP_TERRITORY, false, 0},
    Keyword{u"op_unlock", TK_OP_UNLOCK, false, 0},
    Keyword{u"op_use_firecracker", TK_OP_USE_FIRECRACKER, false, 0},
    Keyword{u"op_use_praseed", TK_OP_USE_PRASEED, false, 0},
    Keyword{u"op_wyvern", TK_OP_WYVERN, false, 0},
    Keyword{u"operate_cond", TK_OPERATE_COND, false, 0},
    Keyword{u"operate_type", TK_OPERATE_TYPE, false, 0},
    Keyword{u"others", TK_SELF_GROUP, true, 2},
    Keyword{u"p_ability_change", TK_P_ABILITY_CHANGE, false, 0},
    Keyword{u"p_ability_change_itemskill", TK_P_ABILITY_CHANGE_ITEMSKILL, false, 0},
    Keyword{u"p_abnormal_rate_limit", TK_P_ABNORMAL_RATE_LIMIT, false, 0},
    Keyword{u"p_abnormal_remove_by_dmg", TK_P_ABNORMAL_REMOVE_BY_DMG, false, 0},
    Keyword{u"p_abnormal_remove_by_hit", TK_P_ABNORMAL_REMOVE_BY_HIT, false, 0},
    Keyword{u"p_abnormal_shield", TK_P_ABNORMAL_SHIELD, false, 0},
    Keyword{u"p_abnormal_time_change_to_abtype", TK_P_ABNORMAL_TIME_CHANGE_TO_ABTYPE, false, 0},
    Keyword{u"p_abnormal_time_change_to_debuff", TK_P_ABNORMAL_TIME_CHANGE_TO_DEBUFF, false, 0},
    Keyword{u"p_absorb_shield", TK_P_ABSORB_SHIELD, false, 0},
    Keyword{u"p_absorb_shield_status", TK_P_ABSORB_SHIELD_STATUS, false, 0},
    Keyword{u"p_acquire_alchemy_skill", TK_P_ACQUIRE_ALCHEMY_SKILL, false, 0},
    Keyword{u"p_add_ensoul_prob", TK_P_ADD_ENSOUL_PROB, false, 0},
    Keyword{u"p_add_vital_bonus", TK_P_ADD_VITAL_BONUS, false, 0},
    Keyword{u"p_after_mp_cost", TK_P_AFTER_MP_COST, false, 0},
    Keyword{u"p_align_direction", TK_P_ALIGN_DIRECTION, false, 0},
    Keyword{u"p_alter_receive_normal_damage", TK_P_ALTER_RECEIVE_NORMAL_DAMAGE, false, 0},
    Keyword{u"p_alter_skill_by_event", TK_P_ALTER_SKILL_BY_EVENT, false, 0},
    Keyword{u"p_anti_cubic", TK_P_ANTI_CUBIC, false, 0},
    Keyword{u"p_area_damage", TK_P_AREA_DAMAGE, false, 0},
    Keyword{u"p_attack_attribute", TK_P_ATTACK_ATTRIBUTE, false, 0},
    Keyword{u"p_attack_attribute_add", TK_P_ATTACK_ATTRIBUTE_ADD, false, 0},
    Keyword{u"p_attack_position_change", TK_P_ATTACK_POSITION_CHANGE, false, 0},
    Keyword{u"p_attack_range", TK_P_ATTACK_RANGE, false, 0},
    Keyword{u"p_attack_speed", TK_P_ATTACK_SPEED, false, 0},
    Keyword{u"p_attack_speed_by_hp1", TK_P_ATTACK_SPEED_BY_HP1, false, 0},
    Keyword{u"p_attack_speed_by_hp2", TK_P_ATTACK_SPEED_BY_HP2, false, 0},
    Keyword{u"p_attack_speed_by_weapon", TK_P_ATTACK_SPEED_BY_WEAPON, false, 0},
    Keyword{u"p_attack_speed_shield", TK_P_ATTACK_SPEED_SHIELD, false, 0},
    Keyword{u"p_attack_trait", TK_P_ATTACK_TRAIT, false, 0},
    Keyword{u"p_aura", TK_P_AURA, false, 0},
    Keyword{u"p_avoid", TK_P_AVOID, false, 0},
    Keyword{u"p_avoid_agro", TK_P_AVOID_AGRO, false, 0},
    Keyword{u"p_avoid_by_move_mode", TK_P_AVOID_BY_MOVE_MODE, false, 0},
    Keyword{u"p_avoid_counter_skill", TK_P_AVOID_COUNTER_SKILL, false, 0},
    Keyword{u"p_avoid_rate_by_hp1", TK_P_AVOID_RATE_BY_HP1, false, 0},
    Keyword{u"p_avoid_rate_by_hp2", TK_P_AVOID_RATE_BY_HP2, false, 0},
    Keyword{u"p_avoid_skill", TK_P_AVOID_SKILL, false, 0},
    Keyword{u"p_balthus_buff", TK_P_BALTHUS_BUFF, false, 0},
    Keyword{u"p_berserker", TK_P_BERSERKER, false, 0},
    Keyword{u"p_betray", TK_P_BETRAY, false, 0},
    Keyword{u"p_block_act", TK_P_BLOCK_ACT, false, 0},
    Keyword{u"p_block_act_bonus", TK_P_BLOCK_ACT_BONUS, false, 0},
    Keyword{u"p_block_add_slot", TK_P_BLOCK_ADD_SLOT, false, 0},
    Keyword{u"p_block_attack", TK_P_BLOCK_ATTACK, false, 0},
    Keyword{u"p_block_buff", TK_P_BLOCK_BUFF, false, 0},
    Keyword{u"p_block_buff_slot", TK_P_BLOCK_BUFF_SLOT, false, 0},
    Keyword{u"p_block_chat", TK_P_BLOCK_CHAT, false, 0},
    Keyword{u"p_block_controll", TK_P_BLOCK_CONTROLL, false, 0},
    Keyword{u"p_block_debuff", TK_P_BLOCK_DEBUFF, false, 0},
    Keyword{u"p_block_elixer_premium", TK_P_BLOCK_ELIXER_PREMIUM, false, 0},
    Keyword{u"p_block_escape", TK_P_BLOCK_ESCAPE, false, 0},
    Keyword{u"p_block_get_energy", TK_P_BLOCK_GET_ENERGY, false, 0},
    Keyword{u"p_block_getdamage", TK_P_BLOCK_GETDAMAGE, false, 0},
    Keyword{u"p_block_getdamage_distance", TK_P_BLOCK_GETDAMAGE_DISTANCE, false, 0},
    Keyword{u"p_block_getdamage_except", TK_P_BLOCK_GETDAMAGE_EXCEPT, false, 0},
    Keyword{u"p_block_getdamage_except_each_other", TK_P_BLOCK_GETDAMAGE_EXCEPT_EACH_OTHER, false, 0},
    Keyword{u"p_block_getdamage_target_only", TK_P_BLOCK_GETDAMAGE_TARGET_ONLY, false, 0},
    Keyword{u"p_block_move", TK_P_BLOCK_MOVE, false, 0},
    Keyword{u"p_block_party", TK_P_BLOCK_PARTY, false, 0},
    Keyword{u"p_block_resurrection", TK_P_BLOCK_RESURRECTION, false, 0},
    Keyword{u"p_block_skill", TK_P_BLOCK_SKILL, false, 0},
    Keyword{u"p_block_skill_physical", TK_P_BLOCK_SKILL_PHYSICAL, false, 0},
    Keyword{u"p_block_skill_special", TK_P_BLOCK_SKILL_SPECIAL, false, 0},
    Keyword{u"p_block_skill_trigger", TK_P_BLOCK_SKILL_TRIGGER, false, 0},
    Keyword{u"p_block_spell", TK_P_BLOCK_SPELL, false, 0},
    Keyword{u"p_block_target", TK_P_BLOCK_TARGET, false, 0},
    Keyword{u"p_block_use_skill", TK_P_BLOCK_USE_SKILL, false, 0},
    Keyword{u"p_bonustimelimit_up", TK_P_BONUSTIMELIMIT_UP, false, 0},
    Keyword{u"p_breath", TK_P_BREATH, false, 0},
    Keyword{u"p_change_fishing_mastery", TK_P_CHANGE_FISHING_MASTERY, false, 0},
    Keyword{u"p_change_slot", TK_P_CHANGE_SLOT, false, 0},
    Keyword{u"p_changebody", TK_P_CHANGEBODY, false, 0},
    Keyword{u"p_channel_clan", TK_P_CHANNEL_CLAN, false, 0},
    Keyword{u"p_charge_vitality", TK_P_CHARGE_VITALITY, false, 0},
    Keyword{u"p_cheapshot", TK_P_CHEAPSHOT, false, 0},
    Keyword{u"p_condition_block_act_item", TK_P_CONDITION_BLOCK_ACT_ITEM, false, 0},
    Keyword{u"p_condition_block_act_skill", TK_P_CONDITION_BLOCK_ACT_SKILL, false, 0},
    Keyword{u"p_counter_skill", TK_P_COUNTER_SKILL, false, 0},
    Keyword{u"p_cp_regen", TK_P_CP_REGEN, false, 0},
    Keyword{u"p_cp_regen_by_move_mode", TK_P_CP_REGEN_BY_MOVE_MODE, false, 0},
    Keyword{u"p_create_common_item", TK_P_CREATE_COMMON_ITEM, false, 0},
    Keyword{u"p_create_item", TK_P_CREATE_ITEM, false, 0},
    Keyword{u"p_create_item_critical", TK_P_CREATE_ITEM_CRITICAL, false, 0},
    Keyword{u"p_critical_damage", TK_P_CRITICAL_DAMAGE, false, 0},
    Keyword{u"p_critical_damage_position", TK_P_CRITICAL_DAMAGE_POSITION, false, 0},
    Keyword{u"p_critical_damage_shield", TK_P_CRITICAL_DAMAGE_SHIELD, false, 0},
    Keyword{u"p_critical_rate", TK_P_CRITICAL_RATE, false, 0},
    Keyword{u"p_critical_rate_by_hp1", TK_P_CRITICAL_RATE_BY_HP1, false, 0},
    Keyword{u"p_critical_rate_by_hp2", TK_P_CRITICAL_RATE_BY_HP2, false, 0},
    Keyword{u"p_critical_rate_position_bonus", TK_P_CRITICAL_RATE_POSITION_BONUS, false, 0},
    Keyword{u"p_critical_rate_shield", TK_P_CRITICAL_RATE_SHIELD, false, 0},
    Keyword{u"p_crystal_grade_modify", TK_P_CRYSTAL_GRADE_MODIFY, false, 0},
    Keyword{u"p_crystallize", TK_P_CRYSTALLIZE, false, 0},
    Keyword{u"p_cubic_mastery", TK_P_CUBIC_MASTERY, false, 0},
    Keyword{u"p_damage_by_attack", TK_P_DAMAGE_BY_ATTACK, false, 0},
    Keyword{u"p_damage_by_attack_castle_siege", TK_P_DAMAGE_BY_ATTACK_CASTLE_SIEGE, false, 0},
    Keyword{u"p_damage_shield", TK_P_DAMAGE_SHIELD, false, 0},
    Keyword{u"p_damage_shield_resist", TK_P_DAMAGE_SHIELD_RESIST, false, 0},
    Keyword{u"p_damage_shield_resist_castle_siege", TK_P_DAMAGE_SHIELD_RESIST_CASTLE_SIEGE, false, 0},
    Keyword{u"p_death_attack", TK_P_DEATH_ATTACK, false, 0},
    Keyword{u"p_defence_attribute", TK_P_DEFENCE_ATTRIBUTE, false, 0},
    Keyword{u"p_defence_critical_damage", TK_P_DEFENCE_CRITICAL_DAMAGE, false, 0},
    Keyword{u"p_defence_critical_rate", TK_P_DEFENCE_CRITICAL_RATE, false, 0},
    Keyword{u"p_defence_damage", TK_P_DEFENCE_DAMAGE, false, 0},
    Keyword{u"p_defence_fatal_blow_rate", TK_P_DEFENCE_FATAL_BLOW_RATE, false, 0},
    Keyword{u"p_defence_trait", TK_P_DEFENCE_TRAIT, false, 0},
    Keyword{u"p_direction_damage", TK_P_DIRECTION_DAMAGE, false, 0},
    Keyword{u"p_disappear_target", TK_P_DISAPPEAR_TARGET, false, 0},
    Keyword{u"p_disarm", TK_P_DISARM, false, 0},
    Keyword{u"p_dispel_by_realtarget", TK_P_DISPEL_BY_REALTARGET, false, 0},
    Keyword{u"p_dominion_transform", TK_P_DOMINION_TRANSFORM, false, 0},
    Keyword{u"p_double_summon", TK_P_DOUBLE_SUMMON, false, 0},
    Keyword{u"p_dropamount_modify", TK_P_DROPAMOUNT_MODIFY, false, 0},
    Keyword{u"p_droprate_modify", TK_P_DROPRATE_MODIFY, false, 0},
    Keyword{u"p_energy_heal", TK_P_ENERGY_HEAL, false, 0},
    Keyword{u"p_energy_skill_power", TK_P_ENERGY_SKILL_POWER, false, 0},
    Keyword{u"p_enlarge_abnormal_slot", TK_P_ENLARGE_ABNORMAL_SLOT, false, 0},
    Keyword{u"p_enlarge_storage", TK_P_ENLARGE_STORAGE, false, 0},
    Keyword{u"p_exp_add", TK_P_EXP_ADD, false, 0},
    Keyword{u"p_exp_modify", TK_P_EXP_MODIFY, false, 0},
    Keyword{u"p_exp_sp_fishing", TK_P_EXP_SP_FISHING, false, 0},
    Keyword{u"p_expand_agathion_main_slot", TK_P_EXPAND_AGATHION_MAIN_SLOT, false, 0},
    Keyword{u"p_expand_agathion_sub_slot", TK_P_EXPAND_AGATHION_SUB_SLOT, false, 0},
    Keyword{u"p_expand_artifact_slot_group", TK_P_EXPAND_ARTIFACT_SLOT_GROUP, false, 0},
    Keyword{u"p_expand_deco_slot", TK_P_EXPAND_DECO_SLOT, false, 0},
    Keyword{u"p_expand_jewel_slot", TK_P_EXPAND_JEWEL_SLOT, false, 0},
    Keyword{u"p_fatal_blow_rate", TK_P_FATAL_BLOW_RATE, false, 0},
    Keyword{u"p_fear", TK_P_FEAR, false, 0},
    Keyword{u"p_fishing_area_buff", TK_P_FISHING_AREA_BUFF, false, 0},
    Keyword{u"p_fishing_damage", TK_P_FISHING_DAMAGE, false, 0},
    Keyword{u"p_fishing_instantkill_prob", TK_P_FISHING_INSTANTKILL_PROB, false, 0},
    Keyword{u"p_fishing_mastery", TK_P_FISHING_MASTERY, false, 0},
    Keyword{u"p_fishing_monster_prob", TK_P_FISHING_MONSTER_PROB, false, 0},
    Keyword{u"p_fishing_start_time", TK_P_FISHING_START_TIME, false, 0},
    Keyword{u"p_focus_energy", TK_P_FOCUS_ENERGY, false, 0},
    Keyword{u"p_get_damage_limit", TK_P_GET_DAMAGE_LIMIT, false, 0},
    Keyword{u"p_get_damage_limit_status", TK_P_GET_DAMAGE_LIMIT_STATUS, false, 0},
    Keyword{u"p_get_item_by_exp", TK_P_GET_ITEM_BY_EXP, false, 0},
    Keyword{u"p_hate_attack", TK_P_HATE_ATTACK, false, 0},
    Keyword{u"p_heal_effect", TK_P_HEAL_EFFECT, false, 0},
    Keyword{u"p_heal_effect_add", TK_P_HEAL_EFFECT_ADD, false, 0},
    Keyword{u"p_hide", TK_P_HIDE, false, 0},
    Keyword{u"p_hide_no_inherit", TK_P_HIDE_NO_INHERIT, false, 0},
    Keyword{u"p_hit", TK_P_HIT, false, 0},
    Keyword{u"p_hit_at_night", TK_P_HIT_AT_NIGHT, false, 0},
    Keyword{u"p_hit_number", TK_P_HIT_NUMBER, false, 0},
    Keyword{u"p_hit_summon", TK_P_HIT_SUMMON, false, 0},
    Keyword{u"p_hp_regen", TK_P_HP_REGEN, false, 0},
    Keyword{u"p_hp_regen_by_move_mode", TK_P_HP_REGEN_BY_MOVE_MODE, false, 0},
    Keyword{u"p_hp_special_add", TK_P_HP_SPECIAL_ADD, false, 0},
    Keyword{u"p_ignore_death", TK_P_IGNORE_DEATH, false, 0},
    Keyword{u"p_ignore_skill", TK_P_IGNORE_SKILL, false, 0},
    Keyword{u"p_inc_spoil_rate", TK_P_INC_SPOIL_RATE, false, 0},
    Keyword{u"p_instant_kill_resist", TK_P_INSTANT_KILL_RESIST, false, 0},
    Keyword{u"p_intercept_skill", TK_P_INTERCEPT_SKILL, false, 0},
    Keyword{u"p_item_chance", TK_P_ITEM_CHANCE, false, 0},
    Keyword{u"p_kalie_buff", TK_P_KALIE_BUFF, false, 0},
    Keyword{u"p_knockdown", TK_P_KNOCKDOWN, false, 0},
    Keyword{u"p_limit_cp", TK_P_LIMIT_CP, false, 0},
    Keyword{u"p_limit_hp", TK_P_LIMIT_HP, false, 0},
    Keyword{u"p_limit_mp", TK_P_LIMIT_MP, false, 0},
    Keyword{u"p_luck", TK_P_LUCK, false, 0},
    Keyword{u"p_lucky_enchant_rate", TK_P_LUCKY_ENCHANT_RATE, false, 0},
    Keyword{u"p_magic_abnormal_resist", TK_P_MAGIC_ABNORMAL_RESIST, false, 0},
    Keyword{u"p_magic_avoid", TK_P_MAGIC_AVOID, false, 0},
    Keyword{u"p_magic_cancel_resist", TK_P_MAGIC_CANCEL_RESIST, false, 0},
    Keyword{u"p_magic_critical_dmg", TK_P_MAGIC_CRITICAL_DMG, false, 0},
    Keyword{u"p_magic_critical_rate", TK_P_MAGIC_CRITICAL_RATE, false, 0},
    Keyword{u"p_magic_defence_critical_dmg", TK_P_MAGIC_DEFENCE_CRITICAL_DMG, false, 0},
    Keyword{u"p_magic_defence_critical_dmg_add", TK_P_MAGIC_DEFENCE_CRITICAL_DMG_ADD, false, 0},
    Keyword{u"p_magic_defence_critical_rate", TK_P_MAGIC_DEFENCE_CRITICAL_RATE, false, 0},
    Keyword{u"p_magic_defence_critical_rate_add", TK_P_MAGIC_DEFENCE_CRITICAL_RATE_ADD, false, 0},
    Keyword{u"p_magic_hit", TK_P_MAGIC_HIT, false, 0},
    Keyword{u"p_magic_mp_cost", TK_P_MAGIC_MP_COST, false, 0},
    Keyword{u"p_magic_speed", TK_P_MAGIC_SPEED, false, 0},
    Keyword{u"p_magic_speed_by_magic_type", TK_P_MAGIC_SPEED_BY_MAGIC_TYPE, false, 0},
    Keyword{u"p_magic_speed_by_weapon", TK_P_MAGIC_SPEED_BY_WEAPON, false, 0},
    Keyword{u"p_magical_attack", TK_P_MAGICAL_ATTACK, false, 0},
    Keyword{u"p_magical_attack_add", TK_P_MAGICAL_ATTACK_ADD, false, 0},
    Keyword{u"p_magical_attack_by_weapon", TK_P_MAGICAL_ATTACK_BY_WEAPON, false, 0},
    Keyword{u"p_magical_attack_pc_level", TK_P_MAGICAL_ATTACK_PC_LEVEL, false, 0},
    Keyword{u"p_magical_critical_rate_maxup", TK_P_MAGICAL_CRITICAL_RATE_MAXUP, false, 0},
    Keyword{u"p_magical_defence", TK_P_MAGICAL_DEFENCE, false, 0},
    Keyword{u"p_magical_defence_add", TK_P_MAGICAL_DEFENCE_ADD, false, 0},
    Keyword{u"p_magical_penetrate", TK_P_MAGICAL_PENETRATE, false, 0},
    Keyword{u"p_mana_charge", TK_P_MANA_CHARGE, false, 0},
    Keyword{u"p_max_cp", TK_P_MAX_CP, false, 0},
    Keyword{u"p_max_hp", TK_P_MAX_HP, false, 0},
    Keyword{u"p_max_mp", TK_P_MAX_MP, false, 0},
    Keyword{u"p_max_mp_add", TK_P_MAX_MP_ADD, false, 0},
    Keyword{u"p_mp_friendly_payback", TK_P_MP_FRIENDLY_PAYBACK, false, 0},
    Keyword{u"p_mp_regen", TK_P_MP_REGEN, false, 0},
    Keyword{u"p_mp_regen_add", TK_P_MP_REGEN_ADD, false, 0},
    Keyword{u"p_mp_regen_by_move_mode", TK_P_MP_REGEN_BY_MOVE_MODE, false, 0},
    Keyword{u"p_mp_shield", TK_P_MP_SHIELD, false, 0},
    Keyword{u"p_mp_vampiric_attack", TK_P_MP_VAMPIRIC_ATTACK, false, 0},
    Keyword{u"p_nopkcount", TK_P_NOPKCOUNT, false, 0},
    Keyword{u"p_passive", TK_P_PASSIVE, false, 0},
    Keyword{u"p_passive_bonus_skill", TK_P_PASSIVE_BONUS_SKILL, false, 0},
    Keyword{u"p_physical_abnormal_resist", TK_P_PHYSICAL_ABNORMAL_RESIST, false, 0},
    Keyword{u"p_physical_armor_hit", TK_P_PHYSICAL_ARMOR_HIT, false, 0},
    Keyword{u"p_physical_attack", TK_P_PHYSICAL_ATTACK, false, 0},
    Keyword{u"p_physical_attack_by_defence", TK_P_PHYSICAL_ATTACK_BY_DEFENCE, false, 0},
    Keyword{u"p_physical_attack_by_hp1", TK_P_PHYSICAL_ATTACK_BY_HP1, false, 0},
    Keyword{u"p_physical_attack_by_hp2", TK_P_PHYSICAL_ATTACK_BY_HP2, false, 0},
    Keyword{u"p_physical_attack_by_material", TK_P_PHYSICAL_ATTACK_BY_MATERIAL, false, 0},
    Keyword{u"p_physical_attack_pc_level", TK_P_PHYSICAL_ATTACK_PC_LEVEL, false, 0},
    Keyword{u"p_physical_attack_shield", TK_P_PHYSICAL_ATTACK_SHIELD, false, 0},
    Keyword{u"p_physical_critical_rate_maxup", TK_P_PHYSICAL_CRITICAL_RATE_MAXUP, false, 0},
    Keyword{u"p_physical_defence", TK_P_PHYSICAL_DEFENCE, false, 0},
    Keyword{u"p_physical_defence_by_hp1", TK_P_PHYSICAL_DEFENCE_BY_HP1, false, 0},
    Keyword{u"p_physical_defence_by_hp2", TK_P_PHYSICAL_DEFENCE_BY_HP2, false, 0},
    Keyword{u"p_physical_defence_by_material", TK_P_PHYSICAL_DEFENCE_BY_MATERIAL, false, 0},
    Keyword{u"p_physical_defence_pvp", TK_P_PHYSICAL_DEFENCE_PVP, false, 0},
    Keyword{u"p_physical_penetrate", TK_P_PHYSICAL_PENETRATE, false, 0},
    Keyword{u"p_physical_polarm_target_single", TK_P_PHYSICAL_POLARM_TARGET_SINGLE, false, 0},
    Keyword{u"p_physical_shield_defence", TK_P_PHYSICAL_SHIELD_DEFENCE, false, 0},
    Keyword{u"p_physical_shield_defence_angle_all", TK_P_PHYSICAL_SHIELD_DEFENCE_ANGLE_ALL, false, 0},
    Keyword{u"p_physical_skill_critical_damage", TK_P_PHYSICAL_SKILL_CRITICAL_DAMAGE, false, 0},
    Keyword{u"p_physical_skill_critical_rate", TK_P_PHYSICAL_SKILL_CRITICAL_RATE, false, 0},
    Keyword{u"p_pk_protect", TK_P_PK_PROTECT, false, 0},
    Keyword{u"p_play_music", TK_P_PLAY_MUSIC, false, 0},
    Keyword{u"p_position_damage", TK_P_POSITION_DAMAGE, false, 0},
    Keyword{u"p_potion_bonus", TK_P_POTION_BONUS, false, 0},
    Keyword{u"p_preserve_abnormal", TK_P_PRESERVE_ABNORMAL, false, 0},
    Keyword{u"p_prevention_death", TK_P_PREVENTION_DEATH, false, 0},
    Keyword{u"p_protect_death_penalty", TK_P_PROTECT_DEATH_PENALTY, false, 0},
    Keyword{u"p_psychic_power", TK_P_PSYCHIC_POWER, false, 0},
    Keyword{u"p_pve_defence_attribute", TK_P_PVE_DEFENCE_ATTRIBUTE, false, 0},
    Keyword{u"p_pve_magical_defence", TK_P_PVE_MAGICAL_DEFENCE, false, 0},
    Keyword{u"p_pve_magical_skill_dmg_bonus", TK_P_PVE_MAGICAL_SKILL_DMG_BONUS, false, 0},
    Keyword{u"p_pve_physical_attack_dmg_bonus", TK_P_PVE_PHYSICAL_ATTACK_DMG_BONUS, false, 0},
    Keyword{u"p_pve_physical_defence", TK_P_PVE_PHYSICAL_DEFENCE, false, 0},
    Keyword{u"p_pve_physical_skill_dmg_bonus", TK_P_PVE_PHYSICAL_SKILL_DMG_BONUS, false, 0},
    Keyword{u"p_pvp_magical_skill_defence_bonus", TK_P_PVP_MAGICAL_SKILL_DEFENCE_BONUS, false, 0},
    Keyword{u"p_pvp_magical_skill_dmg_bonus", TK_P_PVP_MAGICAL_SKILL_DMG_BONUS, false, 0},
    Keyword{u"p_pvp_physical_attack_defence_bonus", TK_P_PVP_PHYSICAL_ATTACK_DEFENCE_BONUS, false, 0},
    Keyword{u"p_pvp_physical_attack_dmg_bonus", TK_P_PVP_PHYSICAL_ATTACK_DMG_BONUS, false, 0},
    Keyword{u"p_pvp_physical_skill_defence_bonus", TK_P_PVP_PHYSICAL_SKILL_DEFENCE_BONUS, false, 0},
    Keyword{u"p_pvp_physical_skill_dmg_bonus", TK_P_PVP_PHYSICAL_SKILL_DMG_BONUS, false, 0},
    Keyword{u"p_raid_point_modify", TK_P_RAID_POINT_MODIFY, false, 0},
    Keyword{u"p_real_resist", TK_P_REAL_RESIST, false, 0},
    Keyword{u"p_recharge_vital_point", TK_P_RECHARGE_VITAL_POINT, false, 0},
    Keyword{u"p_recharge_vital_point_noncount", TK_P_RECHARGE_VITAL_POINT_NONCOUNT, false, 0},
    Keyword{u"p_reduce_cancel", TK_P_REDUCE_CANCEL, false, 0},
    Keyword{u"p_reduce_drop_penalty", TK_P_REDUCE_DROP_PENALTY, false, 0},
    Keyword{u"p_reduce_vital_consume_point", TK_P_REDUCE_VITAL_CONSUME_POINT, false, 0},
    Keyword{u"p_reflect_dd", TK_P_REFLECT_DD, false, 0},
    Keyword{u"p_reflect_dd_resist", TK_P_REFLECT_DD_RESIST, false, 0},
    Keyword{u"p_reflect_skill", TK_P_REFLECT_SKILL, false, 0},
    Keyword{u"p_remove_equip_penalty", TK_P_REMOVE_EQUIP_PENALTY, false, 0},
    Keyword{u"p_remove_grade_penalty", TK_P_REMOVE_GRADE_PENALTY, false, 0},
    Keyword{u"p_replace_skill", TK_P_REPLACE_SKILL, false, 0},
    Keyword{u"p_resist_abnormal_by_category", TK_P_RESIST_ABNORMAL_BY_CATEGORY, false, 0},
    Keyword{u"p_resist_dd_magic", TK_P_RESIST_DD_MAGIC, false, 0},
    Keyword{u"p_resist_dispel_by_category", TK_P_RESIST_DISPEL_BY_CATEGORY, false, 0},
    Keyword{u"p_resurrection_special", TK_P_RESURRECTION_SPECIAL, false, 0},
    Keyword{u"p_reuse_delay", TK_P_REUSE_DELAY, false, 0},
    Keyword{u"p_safe_fall_height", TK_P_SAFE_FALL_HEIGHT, false, 0},
    Keyword{u"p_set_cloak_slot", TK_P_SET_CLOAK_SLOT, false, 0},
    Keyword{u"p_set_collected", TK_P_SET_COLLECTED, false, 0},
    Keyword{u"p_shield_defence_rate", TK_P_SHIELD_DEFENCE_RATE, false, 0},
    Keyword{u"p_skill_critical", TK_P_SKILL_CRITICAL, false, 0},
    Keyword{u"p_skill_critical_probability", TK_P_SKILL_CRITICAL_PROBABILITY, false, 0},
    Keyword{u"p_skill_critical_rate_fix", TK_P_SKILL_CRITICAL_RATE_FIX, false, 0},
    Keyword{u"p_skill_power", TK_P_SKILL_POWER, false, 0},
    Keyword{u"p_soul_eating", TK_P_SOUL_EATING, false, 0},
    Keyword{u"p_sp_modify", TK_P_SP_MODIFY, false, 0},
    Keyword{u"p_speed", TK_P_SPEED, false, 0},
    Keyword{u"p_speed_out_of_fight", TK_P_SPEED_OUT_OF_FIGHT, false, 0},
    Keyword{u"p_speed_out_of_night", TK_P_SPEED_OUT_OF_NIGHT, false, 0},
    Keyword{u"p_speed_pvp", TK_P_SPEED_PVP, false, 0},
    Keyword{u"p_spell_power", TK_P_SPELL_POWER, false, 0},
    Keyword{u"p_stat_up", TK_P_STAT_UP, false, 0},
    Keyword{u"p_stat_up_at_night", TK_P_STAT_UP_AT_NIGHT, false, 0},
    Keyword{u"p_stat_up_limited", TK_P_STAT_UP_LIMITED, false, 0},
    Keyword{u"p_statbonus_skillcritical", TK_P_STATBONUS_SKILLCRITICAL, false, 0},
    Keyword{u"p_statbonus_speed", TK_P_STATBONUS_SPEED, false, 0},
    Keyword{u"p_stop_consume_vital_point", TK_P_STOP_CONSUME_VITAL_POINT, false, 0},
    Keyword{u"p_stop_consume_vital_point_noncount", TK_P_STOP_CONSUME_VITAL_POINT_NONCOUNT, false, 0},
    Keyword{u"p_stop_vitality_effect", TK_P_STOP_VITALITY_EFFECT, false, 0},
    Keyword{u"p_sub_exp", TK_P_SUB_EXP, false, 0},
    Keyword{u"p_sub_vp", TK_P_SUB_VP, false, 0},
    Keyword{u"p_summon_point", TK_P_SUMMON_POINT, false, 0},
    Keyword{u"p_target_me", TK_P_TARGET_ME, false, 0},
    Keyword{u"p_trade", TK_P_TRADE, false, 0},
    Keyword{u"p_transfer_damage_pc", TK_P_TRANSFER_DAMAGE_PC, false, 0},
    Keyword{u"p_transfer_damage_summon", TK_P_TRANSFER_DAMAGE_SUMMON, false, 0},
    Keyword{u"p_transfer_damage_to", TK_P_TRANSFER_DAMAGE_TO, false, 0},
    Keyword{u"p_transform", TK_P_TRANSFORM, false, 0},
    Keyword{u"p_transform_hangover", TK_P_TRANSFORM_HANGOVER, false, 0},
    Keyword{u"p_transmute_attr_attack", TK_P_TRANSMUTE_ATTR_ATTACK, false, 0},
    Keyword{u"p_transmute_attr_defence", TK_P_TRANSMUTE_ATTR_DEFENCE, false, 0},
    Keyword{u"p_trigger_skill_by_abnormal_slot", TK_P_TRIGGER_SKILL_BY_ABNORMAL_SLOT, false, 0},
    Keyword{u"p_trigger_skill_by_attack", TK_P_TRIGGER_SKILL_BY_ATTACK, false, 0},
    Keyword{u"p_trigger_skill_by_attacked_skill", TK_P_TRIGGER_SKILL_BY_ATTACKED_SKILL, false, 0},
    Keyword{u"p_trigger_skill_by_avoid", TK_P_TRIGGER_SKILL_BY_AVOID, false, 0},
    Keyword{u"p_trigger_skill_by_block", TK_P_TRIGGER_SKILL_BY_BLOCK, false, 0},
    Keyword{u"p_trigger_skill_by_death", TK_P_TRIGGER_SKILL_BY_DEATH, false, 0},
    Keyword{u"p_trigger_skill_by_dmg", TK_P_TRIGGER_SKILL_BY_DMG, false, 0},
    Keyword{u"p_trigger_skill_by_kill", TK_P_TRIGGER_SKILL_BY_KILL, false, 0},
    Keyword{u"p_trigger_skill_by_magic_type", TK_P_TRIGGER_SKILL_BY_MAGIC_TYPE, false, 0},
    Keyword{u"p_trigger_skill_by_skill", TK_P_TRIGGER_SKILL_BY_SKILL, false, 0},
    Keyword{u"p_vampiric_attack", TK_P_VAMPIRIC_ATTACK, false, 0},
    Keyword{u"p_vampiric_defence", TK_P_VAMPIRIC_DEFENCE, false, 0},
    Keyword{u"p_violet_boy", TK_P_VIOLET_BOY, false, 0},
    Keyword{u"p_vital_add", TK_P_VITAL_ADD, false, 0},
    Keyword{u"p_vital_rate", TK_P_VITAL_RATE, false, 0},
    Keyword{u"p_vital_recharge_boost", TK_P_VITAL_RECHARGE_BOOST, false, 0},
    Keyword{u"p_vital_sub", TK_P_VITAL_SUB, false, 0},
    Keyword{u"p_weak_death_penalty", TK_P_WEAK_DEATH_PENALTY, false, 0},
    Keyword{u"p_weight_limit", TK_P_WEIGHT_LIMIT, false, 0},
    Keyword{u"p_weight_penalty", TK_P_WEIGHT_PENALTY, false, 0},
    Keyword{u"p_worldchatpoint_add", TK_P_WORLDCHATPOINT_ADD, false, 0},
    Keyword{u"p_wrong_casting", TK_P_WRONG_CASTING, false, 0},
    Keyword{u"party", TK_SINGLE_GROUP, true, 1},
    Keyword{u"party_pledge", TK_SINGLE_GROUP, true, 16},
    Keyword{u"passive_conditions", TK_PASSIVE_CONDITIONS, false, 0},
    Keyword{u"pc_body", TK_SELF_GROUP, true, 11},
    Keyword{u"per", TK_PER_GROUP, true, 0},
    Keyword{u"pet", TK_SINGLE_GROUP, true, 4},
    Keyword{u"pk", TK_PK_GROUP, true, 0},
    Keyword{u"pledge", TK_SINGLE_GROUP, true, 6},
    Keyword{u"pledge_nv_consume", TK_PLEDGE_NV_CONSUME, false, 0},
    Keyword{u"pledgegame_use", TK_PLEDGEGAME_USE, false, 0},
    Keyword{u"point_blank", TK_SINGLE_GROUP, true, 3},
    Keyword{u"pole", TK_SWORD_GROUP, true, 9},
    Keyword{u"possess_holything", TK_POSSESS_HOLYTHING, false, 0},
    Keyword{u"pve_abnormal_time", TK_PVE_ABNORMAL_TIME, false, 0},
    Keyword{u"pve_activate_rate", TK_PVE_ACTIVATE_RATE, false, 0},
    Keyword{u"pve_effect", TK_PVE_EFFECT, false, 0},
    Keyword{u"pvp_abnormal_time", TK_PVP_ABNORMAL_TIME, false, 0},
    Keyword{u"pvp_activate_rate", TK_PVP_ACTIVATE_RATE, false, 0},
    Keyword{u"pvp_effect", TK_PVP_EFFECT, false, 0},
    Keyword{u"pvp_point_consume", TK_PVP_POINT_CONSUME, false, 0},
    Keyword{u"race_angel", TK_RACE_VALUE, true, 15},
    Keyword{u"race_animal", TK_RACE_VALUE, true, 8},
    Keyword{u"race_construct", TK_RACE_VALUE, true, 16},
    Keyword{u"race_creature", TK_RACE_VALUE, true, 10},
    Keyword{u"race_darkelf", TK_RACE_VALUE, true, 2},
    Keyword{u"race_devil", TK_RACE_VALUE, true, 13},
    Keyword{u"race_divine", TK_RACE_VALUE, true, 19},
    Keyword{u"race_dragon", TK_RACE_VALUE, true, 14},
    Keyword{u"race_dwarf", TK_RACE_VALUE, true, 4},
    Keyword{u"race_elemental", TK_RACE_VALUE, true, 12},
    Keyword{u"race_elf", TK_RACE_VALUE, true, 1},
    Keyword{u"race_ertheia", TK_RACE_VALUE, true, 6},
    Keyword{u"race_human", TK_RACE_VALUE, true, 0},
    Keyword{u"race_humanoid", TK_RACE_VALUE, true, 7},
    Keyword{u"race_kamael", TK_RACE_VALUE, true, 5},
    Keyword{u"race_monster", TK_RACE_VALUE, true, 9},
    Keyword{u"race_none", TK_RACE_VALUE, true, -1},
    Keyword{u"race_orc", TK_RACE_VALUE, true, 3},
    Keyword{u"race_undead", TK_RACE_VALUE, true, 11},
    Keyword{u"range", TK_SINGLE_GROUP, true, 2},
    Keyword{u"range_sort_by_dist", TK_SINGLE_GROUP, true, 24},
    Keyword{u"range_sort_by_hp", TK_SINGLE_GROUP, true, 19},
    Keyword{u"rapier", TK_SWORD_GROUP, true, 16},
    Keyword{u"real_enemy_only", TK_SELF_GROUP, true, 5},
    Keyword{u"recipe_common", TK_INVENTORY_NORMAL_GROUP, true, 6},
    Keyword{u"recipe_dwarven", TK_INVENTORY_NORMAL_GROUP, true, 5},
    Keyword{u"reduceskill", TK_REDUCESKILL, false, 0},
    Keyword{u"remain_cp_per", TK_REMAIN_CP_PER, false, 0},
    Keyword{u"remain_hp_per", TK_REMAIN_HP_PER, false, 0},
    Keyword{u"remain_mp_per", TK_REMAIN_MP_PER, false, 0},
    Keyword{u"reset", TK_PER_GROUP, true, 5},
    Keyword{u"resist_cast", TK_RESIST_CAST, false, 0},
    Keyword{u"reuse_delay", TK_REUSE_DELAY, false, 0},
    Keyword{u"reuse_delay_group", TK_REUSE_DELAY_GROUP, false, 0},
    Keyword{u"reuse_delay_lock", TK_REUSE_DELAY_LOCK, false, 0},
    Keyword{u"reuse_delay_type", TK_REUSE_DELAY_TYPE, false, 0},
    Keyword{u"ride_state", TK_RIDE_STATE, false, 0},
    Keyword{u"right", TK_FRONT_GROUP, true, 4},
    Keyword{u"ring_range", TK_SINGLE_GROUP, true, 15},
    Keyword{u"self", TK_SELF_GROUP, true, 0},
    Keyword{u"self_effect", TK_SELF_EFFECT, false, 0},
    Keyword{u"ship_cannon", TK_SELF_GROUP, true, 17},
    Keyword{u"side", TK_FRONT_GROUP, true, 1},
    Keyword{u"single", TK_SINGLE_GROUP, true, 0},
    Keyword{u"skill_begin", TK_SKILL_BEGIN, false, 0},
    Keyword{u"skill_cool_time", TK_SKILL_COOL_TIME, false, 0},
    Keyword{u"skill_end", TK_SKILL_END, false, 0},
    Keyword{u"skill_hit_cancel_time", TK_SKILL_HIT_CANCEL_TIME, false, 0},
    Keyword{u"skill_hit_time", TK_SKILL_HIT_TIME, false, 0},
    Keyword{u"skill_id", TK_SKILL_ID, false, 0},
    Keyword{u"skill_name", TK_SKILL_NAME, false, 0},
    Keyword{u"slot_agathion_main", TK_SLOT_VALUE, true, 22},
    Keyword{u"slot_agathion_sub1", TK_SLOT_VALUE, true, 23},
    Keyword{u"slot_agathion_sub2", TK_SLOT_VALUE, true, 24},
    Keyword{u"slot_agathion_sub3", TK_SLOT_VALUE, true, 25},
    Keyword{u"slot_agathion_sub4", TK_SLOT_VALUE, true, 26},
    Keyword{u"slot_alldress", TK_SLOT_VALUE, true, 17},
    Keyword{u"slot_artifact_a1", TK_SLOT_VALUE, true, 42},
    Keyword{u"slot_artifact_a10", TK_SLOT_VALUE, true, 51},
    Keyword{u"slot_artifact_a11", TK_SLOT_VALUE, true, 52},
    Keyword{u"slot_artifact_a12", TK_SLOT_VALUE, true, 53},
    Keyword{u"slot_artifact_a2", TK_SLOT_VALUE, true, 43},
    Keyword{u"slot_artifact_a3", TK_SLOT_VALUE, true, 44},
    Keyword{u"slot_artifact_a4", TK_SLOT_VALUE, true, 45},
    Keyword{u"slot_artifact_a5", TK_SLOT_VALUE, true, 46},
    Keyword{u"slot_artifact_a6", TK_SLOT_VALUE, true, 47},
    Keyword{u"slot_artifact_a7", TK_SLOT_VALUE, true, 48},
    Keyword{u"slot_artifact_a8", TK_SLOT_VALUE, true, 49},
    Keyword{u"slot_artifact_a9", TK_SLOT_VALUE, true, 50},
    Keyword{u"slot_artifact_b1", TK_SLOT_VALUE, true, 54},
    Keyword{u"slot_artifact_b2", TK_SLOT_VALUE, true, 55},
    Keyword{u"slot_artifact_b3", TK_SLOT_VALUE, true, 56},
    Keyword{u"slot_artifact_c1", TK_SLOT_VALUE, true, 57},
    Keyword{u"slot_artifact_c2", TK_SLOT_VALUE, true, 58},
    Keyword{u"slot_artifact_c3", TK_SLOT_VALUE, true, 59},
    Keyword{u"slot_artifact_d1", TK_SLOT_VALUE, true, 60},
    Keyword{u"slot_artifact_d2", TK_SLOT_VALUE, true, 61},
    Keyword{u"slot_artifact_d3", TK_SLOT_VALUE, true, 62},
    Keyword{u"slot_artifactbook", TK_SLOT_VALUE, true, 41},
    Keyword{u"slot_back", TK_SLOT_VALUE, true, 13},
    Keyword{u"slot_brooch", TK_SLOT_VALUE, true, 34},
    Keyword{u"slot_chest", TK_SLOT_VALUE, true, 10},
    Keyword{u"slot_deco1", TK_SLOT_VALUE, true, 27},
    Keyword{u"slot_deco2", TK_SLOT_VALUE, true, 28},
    Keyword{u"slot_deco3", TK_SLOT_VALUE, true, 29},
    Keyword{u"slot_deco4", TK_SLOT_VALUE, true, 30},
    Keyword{u"slot_deco5", TK_SLOT_VALUE, true, 31},
    Keyword{u"slot_deco6", TK_SLOT_VALUE, true, 32},
    Keyword{u"slot_feet", TK_SLOT_VALUE, true, 12},
    Keyword{u"slot_gloves", TK_SLOT_VALUE, true, 9},
    Keyword{u"slot_hair", TK_SLOT_VALUE, true, 16},
    Keyword{u"slot_hair2", TK_SLOT_VALUE, true, 18},
    Keyword{u"slot_hairall", TK_SLOT_VALUE, true, 19},
    Keyword{u"slot_head", TK_SLOT_VALUE, true, 6},
    Keyword{u"slot_jewel1", TK_SLOT_VALUE, true, 35},
    Keyword{u"slot_jewel2", TK_SLOT_VALUE, true, 36},
    Keyword{u"slot_jewel3", TK_SLOT_VALUE, true, 37},
    Keyword{u"slot_jewel4", TK_SLOT_VALUE, true, 38},
    Keyword{u"slot_jewel5", TK_SLOT_VALUE, true, 39},
    Keyword{u"slot_jewel6", TK_SLOT_VALUE, true, 40},
    Keyword{u"slot_lbracelet", TK_SLOT_VALUE, true, 21},
    Keyword{u"slot_lear", TK_SLOT_VALUE, true, 2},
    Keyword{u"slot_legs", TK_SLOT_VALUE, true, 11},
    Keyword{u"slot_lfinger", TK_SLOT_VALUE, true, 5},
    Keyword{u"slot_lhand", TK_SLOT_VALUE, true, 8},
    Keyword{u"slot_neck", TK_SLOT_VALUE, true, 3},
    Keyword{u"slot_onepiece", TK_SLOT_VALUE, true, 15},
    Keyword{u"slot_rbracelet", TK_SLOT_VALUE, true, 20},
    Keyword{u"slot_rear", TK_SLOT_VALUE, true, 1},
    Keyword{u"slot_rfinger", TK_SLOT_VALUE, true, 4},
    Keyword{u"slot_rhand", TK_SLOT_VALUE, true, 7},
    Keyword{u"slot_rlhand", TK_SLOT_VALUE, true, 14},
    Keyword{u"slot_underwear", TK_SLOT_VALUE, true, 0},
    Keyword{u"slot_waist", TK_SLOT_VALUE, true, 33},
    Keyword{u"soul_saved", TK_SOUL_SAVED, false, 0},
    Keyword{u"special_level", TK_SPECIAL_LEVEL, false, 0},
    Keyword{u"square", TK_SINGLE_GROUP, true, 7},
    Keyword{u"square_pb", TK_SINGLE_GROUP, true, 9},
    Keyword{u"staff", TK_SWORD_GROUP, true, 6},
    Keyword{u"start_effect", TK_START_EFFECT, false, 0},
    Keyword{u"static_object_scope", TK_SINGLE_GROUP, true, 14},
    Keyword{u"storage_clan", TK_INVENTORY_NORMAL_GROUP, true, 2},
    Keyword{u"storage_private", TK_INVENTORY_NORMAL_GROUP, true, 1},
    Keyword{u"sub_level", TK_SUB_LEVEL, false, 0},
    Keyword{u"subordination_abnormal", TK_SUBORDINATION_ABNORMAL, false, 0},
    Keyword{u"summon", TK_SELF_GROUP, true, 7},
    Keyword{u"summon_and_master", TK_SINGLE_GROUP, true, 20},
    Keyword{u"summon_except_master", TK_SINGLE_GROUP, true, 21},
    Keyword{u"sword", TK_SWORD_GROUP, true, 1},
    Keyword{u"t_cp", TK_T_CP, false, 0},
    Keyword{u"t_energy", TK_T_ENERGY, false, 0},
    Keyword{u"t_get_energy", TK_T_GET_ENERGY, false, 0},
    Keyword{u"t_hp", TK_T_HP, false, 0},
    Keyword{u"t_hp_by_hp", TK_T_HP_BY_HP, false, 0},
    Keyword{u"t_hp_fatal", TK_T_HP_FATAL, false, 0},
    Keyword{u"t_hp_in_invincible", TK_T_HP_IN_INVINCIBLE, false, 0},
    Keyword{u"t_hp_magical_attack", TK_T_HP_MAGICAL_ATTACK, false, 0},
    Keyword{u"t_hp_to_owner", TK_T_HP_TO_OWNER, false, 0},
    Keyword{u"t_mp", TK_T_MP, false, 0},
    Keyword{u"t_pull", TK_T_PULL, false, 0},
    Keyword{u"target", TK_SELF_GROUP, true, 1},
    Keyword{u"target_all", TK_PK_GROUP, true, 0},
    Keyword{u"target_item_crystal_divide_type", TK_TARGET_ITEM_CRYSTAL_DIVIDE_TYPE, false, 0},
    Keyword{u"target_item_crystal_type", TK_TARGET_ITEM_CRYSTAL_TYPE, false, 0},
    Keyword{u"target_my_mentee", TK_TARGET_MY_MENTEE, false, 0},
    Keyword{u"target_my_mentor", TK_TARGET_MY_MENTOR, false, 0},
    Keyword{u"target_my_party", TK_TARGET_MY_PARTY, false, 0},
    Keyword{u"target_my_pledge", TK_TARGET_MY_PLEDGE, false, 0},
    Keyword{u"target_npc", TK_PK_GROUP, true, 2},
    Keyword{u"target_operate_cond", TK_TARGET_OPERATE_COND, false, 0},
    Keyword{u"target_pc", TK_PK_GROUP, true, 1},
    Keyword{u"target_race", TK_TARGET_RACE, false, 0},
    Keyword{u"target_summon", TK_PK_GROUP, true, 3},
    Keyword{u"target_type", TK_TARGET_TYPE, false, 0},
    Keyword{u"tick_effect", TK_TICK_EFFECT, false, 0},
    Keyword{u"tick_interval", TK_TICK_INTERVAL, false, 0},
    Keyword{u"toggle_group_id", TK_TOGGLE_GROUP_ID, false, 0},
    Keyword{u"trade_buy", TK_INVENTORY_NORMAL_GROUP, true, 4},
    Keyword{u"trade_sell", TK_INVENTORY_NORMAL_GROUP, true, 3},
    Keyword{u"trait", TK_TRAIT, false, 0},
    Keyword{u"trait_air_deport", TK_TRAIT_VALUE, true, 60},
    Keyword{u"trait_airbind", TK_TRAIT_VALUE, true, 42},
    Keyword{u"trait_ancientsword", TK_TRAIT_VALUE, true, 34},
    Keyword{u"trait_anesthesia", TK_TRAIT_VALUE, true, 28},
    Keyword{u"trait_animal_weakness", TK_TRAIT_VALUE, true, 16},
    Keyword{u"trait_beast_weakness", TK_TRAIT_VALUE, true, 18},
    Keyword{u"trait_bleed", TK_TRAIT_VALUE, true, 11},
    Keyword{u"trait_bluff", TK_TRAIT_VALUE, true, 58},
    Keyword{u"trait_blunt", TK_TRAIT_VALUE, true, 2},
    Keyword{u"trait_boss", TK_TRAIT_VALUE, true, 23},
    Keyword{u"trait_bow", TK_TRAIT_VALUE, true, 6},
    Keyword{u"trait_bug_weakness", TK_TRAIT_VALUE, true, 15},
    Keyword{u"trait_changebody", TK_TRAIT_VALUE, true, 43},
    Keyword{u"trait_construct_weakness", TK_TRAIT_VALUE, true, 25},
    Keyword{u"trait_critical_poison", TK_TRAIT_VALUE, true, 29},
    Keyword{u"trait_crossbow", TK_TRAIT_VALUE, true, 33},
    Keyword{u"trait_dagger", TK_TRAIT_VALUE, true, 3},
    Keyword{u"trait_death", TK_TRAIT_VALUE, true, 26},
    Keyword{u"trait_demonic_weakness", TK_TRAIT_VALUE, true, 47},
    Keyword{u"trait_deport", TK_TRAIT_VALUE, true, 56},
    Keyword{u"trait_derangement", TK_TRAIT_VALUE, true, 14},
    Keyword{u"trait_disarm", TK_TRAIT_VALUE, true, 57},
    Keyword{u"trait_divine_weakness", TK_TRAIT_VALUE, true, 48},
    Keyword{u"trait_dragon_weakness", TK_TRAIT_VALUE, true, 19},
    Keyword{u"trait_dual", TK_TRAIT_VALUE, true, 21},
    Keyword{u"trait_dualblunt", TK_TRAIT_VALUE, true, 55},
    Keyword{u"trait_dualdagger", TK_TRAIT_VALUE, true, 41},
    Keyword{u"trait_dualfist", TK_TRAIT_VALUE, true, 22},
    Keyword{u"trait_elemental_weakness", TK_TRAIT_VALUE, true, 49},
    Keyword{u"trait_etc", TK_TRAIT_VALUE, true, 7},
    Keyword{u"trait_fairy_weakness", TK_TRAIT_VALUE, true, 50},
    Keyword{u"trait_fist", TK_TRAIT_VALUE, true, 5},
    Keyword{u"trait_giant_weakness", TK_TRAIT_VALUE, true, 24},
    Keyword{u"trait_gravity", TK_TRAIT_VALUE, true, 59},
    Keyword{u"trait_gust", TK_TRAIT_VALUE, true, 36},
    Keyword{u"trait_hate", TK_TRAIT_VALUE, true, 62},
    Keyword{u"trait_hold", TK_TRAIT_VALUE, true, 10},
    Keyword{u"trait_human_weakness", TK_TRAIT_VALUE, true, 51},
    Keyword{u"trait_humanoid_weakness", TK_TRAIT_VALUE, true, 52},
    Keyword{u"trait_knockback", TK_TRAIT_VALUE, true, 44},
    Keyword{u"trait_knockdown", TK_TRAIT_VALUE, true, 45},
    Keyword{u"trait_magical_weakness", TK_TRAIT_VALUE, true, 40},
    Keyword{u"trait_none", TK_TRAIT_VALUE, true, 66},
    Keyword{u"trait_paralyze", TK_TRAIT_VALUE, true, 20},
    Keyword{u"trait_physical_blockade", TK_TRAIT_VALUE, true, 37},
    Keyword{u"trait_physical_weakness", TK_TRAIT_VALUE, true, 39},
    Keyword{u"trait_plant_weakness", TK_TRAIT_VALUE, true, 17},
    Keyword{u"trait_poison", TK_TRAIT_VALUE, true, 9},
    Keyword{u"trait_pole", TK_TRAIT_VALUE, true, 4},
    Keyword{u"trait_psychic", TK_TRAIT_VALUE, true, 61},
    Keyword{u"trait_pull", TK_TRAIT_VALUE, true, 46},
    Keyword{u"trait_rapier", TK_TRAIT_VALUE, true, 32},
    Keyword{u"trait_root_magically", TK_TRAIT_VALUE, true, 31},
    Keyword{u"trait_root_physically", TK_TRAIT_VALUE, true, 30},
    Keyword{u"trait_shock", TK_TRAIT_VALUE, true, 13},
    Keyword{u"trait_silence", TK_TRAIT_VALUE, true, 65},
    Keyword{u"trait_sleep", TK_TRAIT_VALUE, true, 12},
    Keyword{u"trait_sword", TK_TRAIT_VALUE, true, 1},
    Keyword{u"trait_target", TK_TRAIT_VALUE, true, 38},
    Keyword{u"trait_turn_stone", TK_TRAIT_VALUE, true, 35},
    Keyword{u"trait_twohandcrossbow", TK_TRAIT_VALUE, true, 54},
    Keyword{u"trait_undead_weakness", TK_TRAIT_VALUE, true, 53},
    Keyword{u"trait_valakas", TK_TRAIT_VALUE, true, 27},
    Keyword{u"trait_wind_stun", TK_TRAIT_VALUE, true, 63},
    Keyword{u"trait_zone", TK_TRAIT_VALUE, true, 64},
    Keyword{u"transform_type", TK_TRANSFORM_TYPE, false, 0},
    Keyword{u"twohandblunt", TK_SWORD_GROUP, true, 5},
    Keyword{u"twohandcrossbow", TK_SWORD_GROUP, true, 22},
    Keyword{u"twohandstaff", TK_SWORD_GROUP, true, 7},
    Keyword{u"twohandsword", TK_SWORD_GROUP, true, 2},
    Keyword{u"unavailable_cond", TK_UNAVAILABLE_COND, false, 0},
    Keyword{u"undead_real_enemy", TK_UNDEAD_REAL_ENEMY_GROUP, true, 3},
    Keyword{u"undeletable", TK_UNDELETABLE, false, 0},
    Keyword{u"union", TK_SINGLE_GROUP, true, 17},
    Keyword{u"weapon_etc", TK_SWORD_GROUP, true, 12},
    Keyword{u"without_action", TK_WITHOUT_ACTION, false, 0},
    Keyword{u"wyvern_object", TK_UNDEAD_REAL_ENEMY_GROUP, true, 4},
    Keyword{u"wyvern_scope", TK_SINGLE_GROUP, true, 11},
    Keyword{u"wyvern_target", TK_SELF_GROUP, true, 13},
}};

const Keyword* FindKeyword(const std::u16string_view text) {
    const auto found = std::lower_bound(
        kKeywords.begin(), kKeywords.end(), text,
        [](const Keyword& keyword, const std::u16string_view value) { return keyword.text < value; }
    );
    return found != kKeywords.end() && found->text == text ? &*found : nullptr;
}

} // namespace

/*!conditions:re2c*/

namespace {

// Original yybegin value / 2 -> re2c condition.
constexpr YYCONDTYPE kConditionByStart[] = {yycINITIAL, yycCOMMENT, yycMAIN};

} // namespace

int CLexerForSkill::Lex(grammar::SemanticBits* lval) {
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
            <INITIAL,COMMENT,MAIN> "@" [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                std::int64_t value = -1;
                if (!m_bOnlyPrecompiledHeader) {
                    value = g_PrecompiledHeaderDB.Search(yytext + 1);
                    if (value == -1) {
                        // const std::wstring hostText = target_utf16::ToHostCodeUnits(yytext)
                        const std::string hostText = Utf16ToUtf8(yytext);
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"Undefined word[%s] in precompiled header in line[%d]",
                        //     hostText.c_str(),
                        //     line + 1)
                        // L2_LOG_ERROR(Log,
                        //     L"Undefined word[%s] in precompiled header in line[%d]",
                        //     hostText.c_str(),
                        //     line + 1)
                        L2_LOG_ERROR(
                            Log,
                            "skilldata.txt line %lld: PCH reference '%s' resolves to -1; parsing continues with that "
                            "numeric "
                            "value.",
                            static_cast<long long>(line) + 1, hostText.c_str()
                        );
                    }
                }
                *lval = static_cast<grammar::SemanticBits>(value);
                return TK_PREDEFINED;
            }
            <MAIN> "<>" { EndToken(); return TK_LESS_GREATER; }
            <MAIN> ">" { EndToken(); return TK_GREATER; }
            <MAIN> ">=" { EndToken(); return TK_GREATER_EQUAL; }
            <MAIN> "=" { EndToken(); return TK_EQUAL; }
            <MAIN> "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_PREDEFINED;
            }
            <MAIN> "$" ("0" | [1-9][0-9]*) {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext + 1, static_cast<std::size_t>(yyleng - 1)))
                );
                return TK_PREDEFINED;
            }
            <MAIN> "-"{0,2} ("0" | [1-9][0-9]*) "." [0-9]* {
                EndToken();
                *lval = ParseWindowsDoubleBits(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                return TK_FLOAT_NUMBER;
            }
            <MAIN> [&();{}] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <MAIN> "[" [^[\]]* "]" {
                EndToken();
                char16_t* const text = DuplicateInnerText(yytext, yyleng);
                if (text != nullptr) {
                    *lval = grammar::PointerSemantic(text);
                }
                return TK_NAME;
            }
            <MAIN> [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                if (const Keyword* const keyword =
                        FindKeyword(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))) {
                    if (keyword->hasValue) {
                        *lval = static_cast<grammar::SemanticBits>(keyword->value);
                    }
                    return keyword->token;
                }
                char16_t* const text = DuplicateText(yytext, yyleng);
                *lval = grammar::PointerSemantic(text);
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
                // Log.Add(
                //     LOG_ERROR,
                //     L"[LEXER_ERROR] Invalid char[%c] in line[%d]",
                //     static_cast<wchar_t>(*yytext),
                //     line + 1)
                // L2_LOG_ERROR(Log,
                //     L"[LEXER_ERROR] Invalid char[%c] in line[%d]",
                //     static_cast<wchar_t>(*yytext),
                //     line + 1)
                L2_LOG_ERROR(
                    Log, "skilldata.txt line %lld: invalid input '%s' (first UTF-16 code unit 0x%04X); input skipped.",
                    static_cast<long long>(line) + 1, Utf16ToUtf8(yytext).c_str(),
                    static_cast<unsigned>(static_cast<std::uint16_t>(*yytext))
                );
                continue;
            }
            <INITIAL> * { EndToken(); continue; }
            <INITIAL,COMMENT,MAIN> $ { return 0; }
        */
    }
}
