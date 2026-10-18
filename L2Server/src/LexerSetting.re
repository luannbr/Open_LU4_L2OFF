// setting.txt scanner (original L2Server/lexer_setting.l).
// re2c generates LexerSetting.cpp.

#include "L2Server/src/LexerSetting.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ParserSetting.h"

#include "Shared/src/Log.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>
#include <string_view>

#include "ParserSettingTokens.h"

namespace {

// Exact generated images referenced by the current
// CLexerForSetting::CLexerForSetting@0x00c9871c.

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

char16_t* DuplicateText(const char16_t* text, std::size_t length) {
    auto* const result = static_cast<char16_t*>(std::malloc((length + 1) * sizeof(char16_t)));
    if (result == nullptr) {
        return nullptr;
    }
    std::copy_n(text, length, result);
    result[length] = u'\0';
    return result;
}

} // namespace

CLexerForSetting::CLexerForSetting() : m_FileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), line(0) {
}

CLexerForSetting::~CLexerForSetting() { m_FileBuf = nullptr; }

int CLexerForSetting::Create(char16_t* const fileBuffer, const int fileSize) {
    m_nFilePointer = 0;
    m_FileBuf = fileBuffer;
    m_nFileSize = fileSize;
    return 1;
}

int CLexerForSetting::Lex(grammar::SemanticBits* lval) {
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
            "//" [^\n]* / "\n" { EndToken(); continue; }
            "initial_equipment_begin" { EndToken(); return TK_INITIAL_EQUIPMENT_BEGIN; }
            "initial_equipment_end" { EndToken(); return TK_INITIAL_EQUIPMENT_END; }
            "initial_custom_equipment_begin" { EndToken(); return TK_INITIAL_CUSTOM_EQUIPMENT_BEGIN; }
            "initial_custom_equipment_end" { EndToken(); return TK_INITIAL_CUSTOM_EQUIPMENT_END; }
            "initial_start_point_begin" { EndToken(); return TK_INITIAL_START_POINT_BEGIN; }
            "initial_start_point_end" { EndToken(); return TK_INITIAL_START_POINT_END; }
            "restart_point_begin" { EndToken(); return TK_RESTART_POINT_BEGIN; }
            "restart_point_end" { EndToken(); return TK_RESTART_POINT_END; }
            "point_begin" { EndToken(); return TK_POINT_BEGIN; }
            "point_end" { EndToken(); return TK_POINT_END; }
            "area_begin" { EndToken(); return TK_AREA_BEGIN; }
            "area_end" { EndToken(); return TK_AREA_END; }
            "class" { EndToken(); return TK_CLASS; }
            "point_name" { EndToken(); return TK_POINT_NAME; }
            "point" { EndToken(); return TK_POINT; }
            "chao_point" { EndToken(); return TK_CHAO_POINT; }
            "map" { EndToken(); return TK_MAP; }
            "banned_race" { EndToken(); return TK_BANNED_RACE; }
            "bbs" { EndToken(); return TK_BBS; }
            "loc_name" { EndToken(); return TK_LOC_NAME; }
            "range" { EndToken(); return TK_RANGE; }
            "race" { EndToken(); return TK_RACE; }
            "minimum_stat_begin" { EndToken(); return TK_MINIMUM_STAT_BEGIN; }
            "minimum_stat_end" { EndToken(); return TK_MINIMUM_STAT_END; }
            "maximum_stat_begin" { EndToken(); return TK_MAXIMUM_STAT_BEGIN; }
            "maximum_stat_end" { EndToken(); return TK_MAXIMUM_STAT_END; }
            "recommended_stat_begin" { EndToken(); return TK_RECOMMENDED_STAT_BEGIN; }
            "recommended_stat_end" { EndToken(); return TK_RECOMMENDED_STAT_END; }
            "abnormal_resist_begin" { EndToken(); return TK_ABNORMAL_RESIST_BEGIN; }
            "abnormal_resist_end" { EndToken(); return TK_ABNORMAL_RESIST_END; }
            "olympiad_arena_begin" { EndToken(); return TK_OLYMPIAD_ARENA_BEGIN; }
            "olympiad_arena_end" { EndToken(); return TK_OLYMPIAD_ARENA_END; }
            "arena_begin" { EndToken(); return TK_ARENA_BEGIN; }
            "arena_end" { EndToken(); return TK_ARENA_END; }
            "id" { EndToken(); return TK_ID; }
            "point1" { EndToken(); return TK_POINT1; }
            "point2" { EndToken(); return TK_POINT2; }
            "olympiad_genaral_setting_begin" { EndToken(); return TK_OLYMPIAD_GENARAL_SETTING_BEGIN; }
            "olympiad_general_setting_end" { EndToken(); return TK_OLYMPIAD_GENERAL_SETTING_END; }
            "forbidden_weapon" { EndToken(); return TK_FORBIDDEN_WEAPON; }
            "forbidden_skill" { EndToken(); return TK_FORBIDDEN_SKILL; }
            "forbidden_item" { EndToken(); return TK_FORBIDDEN_ITEM; }
            "give_skill_class" { EndToken(); return TK_GIVE_SKILL_CLASS; }
            "give_skill_classless" { EndToken(); return TK_GIVE_SKILL_CLASSLESS; }
            "give_skill_team" { EndToken(); return TK_GIVE_SKILL_TEAM; }
            "classless_bonus" { EndToken(); return TK_CLASSLESS_BONUS; }
            "class_bonus" { EndToken(); return TK_CLASS_BONUS; }
            "team_bonus" { EndToken(); return TK_TEAM_BONUS; }
            "olympiad_time_setting" { EndToken(); return TK_OLYMPIAD_TIME_SETTING; }
            "olympiad_endtime_setting" { EndToken(); return TK_OLYMPIAD_ENDTIME_SETTING; }
            "olympiad_match_days" { EndToken(); return TK_OLYMPIAD_MATCH_DAYS; }
            "olympiad_point_denominator" { EndToken(); return TK_OLYMPIAD_POINT_DENOMINATOR; }
            "olympiad_point_weight" { EndToken(); return TK_OLYMPIAD_POINT_WEIGHT; }
            "olympiad_reset_skill" { EndToken(); return TK_OLYMPIAD_RESET_SKILL; }
            "olympiad_reset_skill_excluded" { EndToken(); return TK_OLYMPIAD_RESET_SKILL_EXCLUDED; }
            "olympiad_door" { EndToken(); return TK_OLYMPIAD_DOOR; }
            "olympiad_match_limit" { EndToken(); return TK_OLYMPIAD_MATCH_LIMIT; }
            "winner_reward_item" { EndToken(); return TK_WINNER_REWARD_ITEM; }
            "additional_reward_item" { EndToken(); return TK_ADDITIONAL_REWARD_ITEM; }
            "pledge_game_setting_start" { EndToken(); return TK_PLEDGE_GAME_SETTING_START; }
            "pledge_game_setting_end" { EndToken(); return TK_PLEDGE_GAME_SETTING_END; }
            "pledge_game_start_time" { EndToken(); return TK_PLEDGE_GAME_START_TIME; }
            "pledge_game_end_time" { EndToken(); return TK_PLEDGE_GAME_END_TIME; }
            "hero_general_setting_start" { EndToken(); return TK_HERO_GENERAL_SETTING_START; }
            "hero_general_setting_end" { EndToken(); return TK_HERO_GENERAL_SETTING_END; }
            "hero_skill" { EndToken(); return TK_HERO_SKILL; }
            "pvpmatch_setting_start" { EndToken(); return TK_PVPMATCH_SETTING_START; }
            "pvpmatch_setting_end" { EndToken(); return TK_PVPMATCH_SETTING_END; }
            "give_pvppoint_time_start" { EndToken(); return TK_GIVE_PVPPOINT_TIME_START; }
            "give_pvppoint_time_end" { EndToken(); return TK_GIVE_PVPPOINT_TIME_END; }
            "give_pvppoint_start_time" { EndToken(); return TK_GIVE_PVPPOINT_START_TIME; }
            "give_pvppoint_end_time" { EndToken(); return TK_GIVE_PVPPOINT_END_TIME; }
            "give_pvppoint_week_days" { EndToken(); return TK_GIVE_PVPPOINT_WEEK_DAYS; }
            "cleft_setting_begin" { EndToken(); return TK_CLEFT_SETTING_BEGIN; }
            "cleft_setting_end" { EndToken(); return TK_CLEFT_SETTING_END; }
            "cleft_zone_territory" { EndToken(); return TK_CLEFT_ZONE_TERRITORY; }
            "red_start_point_begin" { EndToken(); return TK_RED_START_POINT_BEGIN; }
            "red_start_point_end" { EndToken(); return TK_RED_START_POINT_END; }
            "blue_start_point_begin" { EndToken(); return TK_BLUE_START_POINT_BEGIN; }
            "blue_start_point_end" { EndToken(); return TK_BLUE_START_POINT_END; }
            "red_banish_point_begin" { EndToken(); return TK_RED_BANISH_POINT_BEGIN; }
            "red_banish_point_end" { EndToken(); return TK_RED_BANISH_POINT_END; }
            "blue_banish_point_begin" { EndToken(); return TK_BLUE_BANISH_POINT_BEGIN; }
            "blue_banish_point_end" { EndToken(); return TK_BLUE_BANISH_POINT_END; }
            "CAT_period" { EndToken(); return TK_CAT_PERIOD; }
            "default_reward" { EndToken(); return TK_DEFAULT_REWARD; }
            "pcbang_default_reward" { EndToken(); return TK_PCBANG_DEFAULT_REWARD; }
            "winner_reward" { EndToken(); return TK_WINNER_REWARD; }
            "winner_default_reward" { EndToken(); return TK_WINNER_DEFAULT_REWARD; }
            "loser_reward" { EndToken(); return TK_LOSER_REWARD; }
            "drawer_reward" { EndToken(); return TK_DRAWER_REWARD; }
            "winner_sp_bonus" { EndToken(); return TK_WINNER_SP_BONUS; }
            "loser_sp_bonus" { EndToken(); return TK_LOSER_SP_BONUS; }
            "kill_sp_bonus" { EndToken(); return TK_KILL_SP_BONUS; }
            "waiting_skill" { EndToken(); return TK_WAITING_SKILL; }
            "minimum_point" { EndToken(); return TK_MINIMUM_POINT; }
            "delete_items_after_match" { EndToken(); return TK_DELETE_ITEMS_AFTER_MATCH; }
            "blockupset_setting_begin" { EndToken(); return TK_BLOCKUPSET_SETTING_BEGIN; }
            "blockupset_setting_end" { EndToken(); return TK_BLOCKUPSET_SETTING_END; }
            "game_period" { EndToken(); return TK_GAME_PERIOD; }
            "blue_enter_skill" { EndToken(); return TK_BLUE_ENTER_SKILL; }
            "red_enter_skill" { EndToken(); return TK_RED_ENTER_SKILL; }
            "blockupset_stage_begin" { EndToken(); return TK_BLOCKUPSET_STAGE_BEGIN; }
            "blockupset_stage_end" { EndToken(); return TK_BLOCKUPSET_STAGE_END; }
            "stage" { EndToken(); return TK_STAGE; }
            "blockupset_zone_territory" { EndToken(); return TK_BLOCKUPSET_ZONE_TERRITORY; }
            "mentor_setting_begin" { EndToken(); return TK_MENTOR_SETTING_BEGIN; }
            "mentor_setting_end" { EndToken(); return TK_MENTOR_SETTING_END; }
            "mentor_skill" { EndToken(); return TK_MENTOR_SKILL; }
            "mentee_skill" { EndToken(); return TK_MENTEE_SKILL; }
            "withoutmentor_skill" { EndToken(); return TK_WITHOUTMENTOR_SKILL; }
            "mentee_item_cond" { EndToken(); return TK_MENTEE_ITEM_COND; }
            "mentee_lev_up_reward_academy" { EndToken(); return TK_MENTEE_LEV_UP_REWARD_ACADEMY; }
            "mentee_lev_up_reward" { EndToken(); return TK_MENTEE_LEV_UP_REWARD; }
            "mentee_lev_up_title" { EndToken(); return TK_MENTEE_LEV_UP_TITLE; }
            "mentee_lev_up_contents" { EndToken(); return TK_MENTEE_LEV_UP_CONTENTS; }
            "mentee_graduate_reward" { EndToken(); return TK_MENTEE_GRADUATE_REWARD; }
            "mentee_graduate_title" { EndToken(); return TK_MENTEE_GRADUATE_TITLE; }
            "mentee_graduate_contents" { EndToken(); return TK_MENTEE_GRADUATE_CONTENTS; }
            "mentee_join_reward" { EndToken(); return TK_MENTEE_JOIN_REWARD; }
            "mentee_join_title" { EndToken(); return TK_MENTEE_JOIN_TITLE; }
            "mentee_join_contents" { EndToken(); return TK_MENTEE_JOIN_CONTENTS; }
            "training_zone_setting_begin" { EndToken(); return TK_TRAINING_ZONE_SETTING_BEGIN; }
            "training_zone_setting_end" { EndToken(); return TK_TRAINING_ZONE_SETTING_END; }
            "training_zone_complete_html" { EndToken(); return TK_TRAINING_ZONE_COMPLETE_HTML; }
            "training_zone_position" { EndToken(); return TK_TRAINING_ZONE_POSITION; }
            "pledgegame_setting_begin" { EndToken(); return TK_PLEDGEGAME_SETTING_BEGIN; }
            "reservable_pledgegame_time" { EndToken(); return TK_RESERVABLE_PLEDGEGAME_TIME; }
            "common_lose_reward" { EndToken(); return TK_COMMON_LOSE_REWARD; }
            "common_win_reward" { EndToken(); return TK_COMMON_WIN_REWARD; }
            "rand_win_min_max_reward" { EndToken(); return TK_RAND_WIN_MIN_MAX_REWARD; }
            "pledgegame_setting_end" { EndToken(); return TK_PLEDGEGAME_SETTING_END; }
            "kamaroka_setting_start" { EndToken(); return TK_KAMAROKA_SETTING_START; }
            "kamaroka_setting_end" { EndToken(); return TK_KAMAROKA_SETTING_END; }
            "kamaroka_days" { EndToken(); return TK_KAMAROKA_DAYS; }
            "item_skill_transfer_list" { EndToken(); return TK_ITEM_SKILL_TRANSFER_LIST; }
            "vip_attendance_setting_begin" { EndToken(); return TK_VIP_ATTENDANCE_SETTING_BEGIN; }
            "vip_attendance_reset_time" { EndToken(); return TK_VIP_ATTENDANCE_RESET_TIME; }
            "vip_attendance_setting_end" { EndToken(); return TK_VIP_ATTENDANCE_SETTING_END; }
            "clanbonus_setting_begin" { EndToken(); return TK_CLANBONUS_SETTING_BEGIN; }
            "clanbonus_setting_end" { EndToken(); return TK_CLANBONUS_SETTING_END; }
            "access_goal_rate" { EndToken(); return TK_ACCESS_GOAL_RATE; }
            "hunt_goal_rate" { EndToken(); return TK_HUNT_GOAL_RATE; }
            "access_reward" { EndToken(); return TK_ACCESS_REWARD; }
            "hunt_reward" { EndToken(); return TK_HUNT_REWARD; }
            "exp_to_huntpoint" { EndToken(); return TK_EXP_TO_HUNTPOINT; }
            "pledge_raid_begin" { EndToken(); return TK_PLEDGE_RAID_BEGIN; }
            "pledge_raid_end" { EndToken(); return TK_PLEDGE_RAID_END; }
            "pledge_raid_rank_reward_begin" { EndToken(); return TK_PLEDGE_RAID_RANK_REWARD_BEGIN; }
            "pledge_raid_rank_reward_end" { EndToken(); return TK_PLEDGE_RAID_RANK_REWARD_END; }
            "pledge_raid_tier_reward_begin" { EndToken(); return TK_PLEDGE_RAID_TIER_REWARD_BEGIN; }
            "pledge_raid_tier_reward_end" { EndToken(); return TK_PLEDGE_RAID_TIER_REWARD_END; }
            "pledge_raid_mastery_reward_begin" { EndToken(); return TK_PLEDGE_RAID_MASTERY_REWARD_BEGIN; }
            "pledge_raid_mastery_reward_end" { EndToken(); return TK_PLEDGE_RAID_MASTERY_REWARD_END; }
            "rank_reward" { EndToken(); return TK_RANK_REWARD; }
            "tier_reward" { EndToken(); return TK_TIER_REWARD; }
            "mastery_reward" { EndToken(); return TK_MASTERY_REWARD; }
            "pledge_raid_hunt_npc_name" { EndToken(); return TK_PLEDGE_RAID_HUNT_NPC_NAME; }
            "pledge_mission_begin" { EndToken(); return TK_PLEDGE_MISSION_BEGIN; }
            "pledge_mission_end" { EndToken(); return TK_PLEDGE_MISSION_END; }
            "pledge_mission_common_max_begin" { EndToken(); return TK_PLEDGE_MISSION_COMMON_MAX_BEGIN; }
            "pledge_mission_common_max_end" { EndToken(); return TK_PLEDGE_MISSION_COMMON_MAX_END; }
            "novice" { EndToken(); return TK_NOVICE; }
            "nobless" { EndToken(); return TK_NOBLESS; }
            "honors" { EndToken(); return TK_HONORS; }
            "MON" {
                EndToken();
                *lval = 1;
                return TK_MON_GROUP;
            }
            "TUE" {
                EndToken();
                *lval = 2;
                return TK_MON_GROUP;
            }
            "WED" {
                EndToken();
                *lval = 3;
                return TK_MON_GROUP;
            }
            "THU" {
                EndToken();
                *lval = 4;
                return TK_MON_GROUP;
            }
            "FRI" {
                EndToken();
                *lval = 5;
                return TK_MON_GROUP;
            }
            "SAT" {
                EndToken();
                *lval = 6;
                return TK_MON_GROUP;
            }
            "SUN" {
                EndToken();
                *lval = 0;
                return TK_MON_GROUP;
            }
            "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_INTEGER;
            }
            "$" ("0" | [1-9][0-9]*) {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext + 1, static_cast<std::size_t>(yyleng - 1)))
                );
                return TK_INTEGER;
            }
            "0" [0-9] {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_INTEGER;
            }
            [,:;=[\]{}~] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            "[" [^[\]]* "]" {
                EndToken();
                const std::size_t innerLength = yyleng >= 2 ? static_cast<std::size_t>(yyleng - 2) : 0;
                char16_t* const duplicate = DuplicateText(yytext + 1, innerLength);
                if (duplicate != nullptr) {
                    *lval = grammar::PointerSemantic(duplicate);
                }
                return TK_NAME;
            }
            [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                char16_t* const duplicate = DuplicateText(yytext, std::char_traits<char16_t>::length(yytext));
                if (duplicate != nullptr) {
                    *lval = grammar::PointerSemantic(duplicate);
                }
                return TK_IDENTIFIER;
            }
            [ \t\r] { EndToken(); continue; }
            "\n" {
                EndToken();
                ++line;
                continue;
            }
            . {
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
                    Log, L"Invalid settings token '%ls' at line %lld; the token is skipped.", invalid.c_str(),
                    static_cast<long long>(line) + 1
                );
                continue;
            }
            $ { return 0; }
        */
    }
}
