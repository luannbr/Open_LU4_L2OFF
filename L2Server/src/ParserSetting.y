// setting.txt grammar (original L2Server/parser_setting.y).
// Bison generates ParserSetting.cpp and ParserSettingTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserSetting.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/BlockUpset.h"
#include "L2Server/src/CConfig.h"
#include "L2Server/src/Cleft.h"
#include "L2Server/src/InstantZone.h"
#include "L2Server/src/MentorSetting.h"
#include "L2Server/src/ObjectDB.h"
#include "L2Server/src/OlympiadStuff.h"
#include "L2Server/src/Pledge.h"
#include "L2Server/src/PledgeGameSystem.h"
#include "L2Server/src/PledgeInfo.h"
#include "L2Server/src/PledgeMissionInfo.h"
#include "L2Server/src/PledgeRaidInfo.h"
#include "L2Server/src/Skill.h"
#include "L2Server/src/TrainingZoneSetting.h"
#include "L2Server/src/VipAttendance.h"
#include "L2Server/src/WHeroSystem.h"

#include "Shared/src/Log.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

#include "ParserSettingTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForSetting::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserSettingTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

const char16_t* SemanticText(const grammar::SemanticBits value) {
    return grammar::SemanticString(value);
}

bool EqualsAsciiInsensitive(const char16_t* value, const char16_t* expected) {
    if (value == nullptr || expected == nullptr) {
        return value == expected;
    }
    while (*value != u'\0' && *expected != u'\0') {
        char16_t left = *value++;
        char16_t right = *expected++;
        if (left >= u'A' && left <= u'Z') {
            left = static_cast<char16_t>(left + (u'a' - u'A'));
        }
        if (right >= u'A' && right <= u'Z') {
            right = static_cast<char16_t>(right + (u'a' - u'A'));
        }
        if (left != right) {
            return false;
        }
    }
    return *value == *expected;
}

struct SettingClassEntry {
    const char16_t* name;
    std::int32_t classId;
    std::int64_t classBit;
};

constexpr SettingClassEntry kSettingClasses[] = {
    {u"human_fighter", 0, 0x1},
    {u"human_magician", 10, 0x2},
    {u"elf_fighter", 18, 0x4},
    {u"elf_magician", 25, 0x8},
    {u"darkelf_fighter", 31, 0x10},
    {u"darkelf_magician", 38, 0x20},
    {u"orc_fighter", 44, 0x40},
    {u"orc_shaman", 49, 0x80},
    {u"dwarf_apprentice", 53, 0x100},
    {u"kamael_m_soldier", 123, 0x200},
    {u"kamael_f_soldier", 124, 0x400},
    {u"ertheia_fighter", 182, 0x1000},
    {u"ertheia_wizard", 183, 0x2000},
    {u"duelist", 88, 0},
    {u"dreadnought", 89, 0},
    {u"phoenix_knight", 90, 0},
    {u"hell_knight", 91, 0},
    {u"sagittarius", 92, 0},
    {u"adventurer", 93, 0},
    {u"archmage", 94, 0},
    {u"soultaker", 95, 0},
    {u"arcana_lord", 96, 0},
    {u"cardinal", 97, 0},
    {u"hierophant", 98, 0},
    {u"evas_templar", 99, 0},
    {u"sword_muse", 100, 0},
    {u"wind_rider", 101, 0},
    {u"moonlight_sentinel", 102, 0},
    {u"mystic_muse", 103, 0},
    {u"elemental_master", 104, 0},
    {u"evas_saint", 105, 0},
    {u"shilien_templar", 106, 0},
    {u"spectral_dancer", 107, 0},
    {u"ghost_hunter", 108, 0},
    {u"ghost_sentinel", 109, 0},
    {u"storm_screamer", 110, 0},
    {u"spectral_master", 111, 0},
    {u"shilien_saint", 112, 0},
    {u"titan", 113, 0},
    {u"grand_khavatari", 114, 0},
    {u"dominator", 115, 0},
    {u"doomcryer", 116, 0},
    {u"fortune_seeker", 117, 0},
    {u"maestro", 118, 0},
    {u"doombringer", 131, 0},
    {u"m_soul_hound", 132, 0},
    {u"f_soul_hound", 133, 0},
    {u"trickster", 134, 0},
    {u"dwarf_mage", 147, 0x800},
    {u"sigel_phoenix_knight", 148, 0},
    {u"sigel_hell_knight", 149, 0},
    {u"sigel_evas_templar", 150, 0},
    {u"sigel_shilien_templar", 151, 0},
    {u"tir_duelist", 152, 0},
    {u"tir_dreadnought", 153, 0},
    {u"tir_grand_khavatari", 155, 0},
    {u"tir_maestro", 156, 0},
    {u"tir_doombringer", 157, 0},
    {u"othel_adventurer", 158, 0},
    {u"othel_wind_rider", 159, 0},
    {u"othel_ghost_hunter", 160, 0},
    {u"othel_fortune_seeker", 161, 0},
    {u"yr_sagittarius", 162, 0},
    {u"yr_moonlight_sentinel", 163, 0},
    {u"yr_ghost_sentinel", 164, 0},
    {u"feoh_archmage", 166, 0},
    {u"feoh_soultaker", 167, 0},
    {u"feoh_mystic_muse", 168, 0},
    {u"feoh_storm_screamer", 169, 0},
    {u"feoh_soul_hound", 170, 0},
    {u"is_hierophant", 171, 0},
    {u"is_sword_muse", 172, 0},
    {u"is_spectral_dancer", 173, 0},
    {u"is_dominator", 174, 0},
    {u"is_doomcryer", 175, 0},
    {u"wynn_arcana_lord", 176, 0},
    {u"wynn_elemental_master", 177, 0},
    {u"wynn_spectral_master", 178, 0},
    {u"eolh_cardinal", 179, 0},
    {u"eolh_evas_saint", 180, 0},
    {u"eolh_shilien_saint", 181, 0},
    {u"marauder", 184, 0},
    {u"lenker", 186, 0},
    {u"strato_lenker", 188, 0},
    {u"menace", 185, 0},
    {u"strom_menace", 187, 0},
    {u"sayhas_seer", 189, 0},
    {u"sigel_knight", 139, 0x4000},
    {u"tir_warrior", 140, 0x8000},
    {u"othel_rogue", 141, 0x10000},
    {u"yr_archer", 142, 0x20000},
    {u"feoh_wizard", 143, 0x40000},
    {u"is_enchanter", 144, 0x80000},
    {u"wynn_summoner", 145, 0x100000},
    {u"eolh_healer", 146, 0x200000},
    {u"mass_lenker", 190, 0x400000},
    {u"seer", 191, 0x800000},
    {nullptr, -1, 0},
};

static_assert(std::size(kSettingClasses) == 98);

ClassType FindSettingClass(const char16_t* const name, const int line) {
    for (std::size_t index = 0; index < std::size(kSettingClasses); ++index) {
        const SettingClassEntry& entry = kSettingClasses[index];
        if (entry.name == nullptr) {
            // const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
            const std::wstring hostName = Utf16ToWide(name);
            // Log.Add(
            //     LOG_ERROR,
            //     L"invalid class #1 [%s] line = %d",
            //     hostName.c_str(),
            //     line)
            // L2_LOG_ERROR(Log,
            //     L"invalid class #1 [%s] line = %d",
            //     hostName.c_str(),
            //     line)
            L2_LOG_ERROR(Log, u"Settings line %d: unknown class '%ls'; returning class ID -1.", line, hostName.c_str());
            return static_cast<ClassType>(-1);
        }
        if (EqualsAsciiInsensitive(entry.name, name)) {
            return static_cast<ClassType>(entry.classId);
        }
    }
    // const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
    const std::wstring hostName = Utf16ToWide(name);
    // Log.Add(
    //     LOG_ERROR,
    //     L"invalid class #2 [%s] line = %d",
    //     hostName.c_str(),
    //     line)
    // L2_LOG_ERROR(Log,
    //     L"invalid class #2 [%s] line = %d",
    //     hostName.c_str(),
    //     line)
    L2_LOG_ERROR(Log, u"Settings line %d: unknown class '%ls'; returning class ID -1.", line, hostName.c_str());
    return static_cast<ClassType>(-1);
}

std::int64_t FindSettingClassBit(const char16_t* const name, const int line) {
    for (std::size_t index = 0; index < std::size(kSettingClasses); ++index) {
        const SettingClassEntry& entry = kSettingClasses[index];
        if (entry.name == nullptr) {
            // const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
            const std::wstring hostName = Utf16ToWide(name);
            // Log.Add(
            //     LOG_ERROR,
            //     L"invalid class #3 [%s] line = %d",
            //     hostName.c_str(),
            //     line)
            // L2_LOG_ERROR(Log,
            //     L"invalid class #3 [%s] line = %d",
            //     hostName.c_str(),
            //     line)
            L2_LOG_ERROR(Log, u"Settings line %d: unknown class '%ls'; using an empty class mask.", line, hostName.c_str());
            return 0;
        }
        if (EqualsAsciiInsensitive(entry.name, name)) {
            return entry.classBit;
        }
    }
    // const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
    const std::wstring hostName = Utf16ToWide(name);
    // Log.Add(
    //     LOG_ERROR,
    //     L"invalid class #4 [%s] line = %d",
    //     hostName.c_str(),
    //     line)
    // L2_LOG_ERROR(Log,
    //     L"invalid class #4 [%s] line = %d",
    //     hostName.c_str(),
    //     line)
    L2_LOG_ERROR(Log, u"Settings line %d: unknown class '%ls'; using an empty class mask.", line, hostName.c_str());
    return 0;
}

RaceType FindSettingRace(const char16_t* const name, const bool bannedGroupSpelling) {
    constexpr std::array<const char16_t*, 7> names{u"human", u"elf", u"darkelf", u"orc", u"dwarf", u"kamael", u"ertheia"};
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (index == 6 && bannedGroupSpelling) {
            if (std::u16string_view(name == nullptr ? u"" : name) == u"erteia") {
                return static_cast<RaceType>(index);
            }
        } else if (std::u16string_view(name == nullptr ? u"" : name) == names[index]) {
            return static_cast<RaceType>(index);
        }
    }
    return RT_NONE;
}

std::int64_t PackSkillKey(const CSkillInfo& skill) {
    const CSkillKey key = skill.GetKey();
    std::int64_t packed = 0;
    std::memcpy(&packed, &key, sizeof(packed));
    return packed;
}

template <std::size_t Capacity>
void CopyTargetText(char16_t (&destination)[Capacity], const char16_t* const source) {
    if (source == nullptr) {
        destination[0] = u'\0';
        return;
    }
    const std::size_t length = std::char_traits<char16_t>::length(source);
    if (length >= Capacity) {
        destination[0] = u'\0';
        return;
    }
    std::copy_n(source, length + 1, destination);
}

} // namespace

CParserForSetting::CParserForSetting() :
    m_lexer(),
    stringList(),
    tempPosition(),
    tempPoint(),
    tempPointList(),
    tempChaoPointList(),
    tempItemList(),
    tempInitItemList(),
    tempSkillList(),
    tempPoint1{},
    tempPoint2{},
    tempArenaPoint{},
    m_CleftData(),
    m_BlockUpsetData(),
    m_RewardData(),
    tempRewardList() {
    tempRule = GRT_CLASSLESS;
    m_pointIndex = 0;
    m_pCleftMatch = &Singleton<CCleft>::inst();
    m_pBlockUpsetManager = &Singleton<CBlockUpsetManager>::inst();
    m_pBlockUpset = nullptr;
}

CParserForSetting::~CParserForSetting() = default;

int CParserForSetting::Create(CSetting* const setting, char16_t* const fileBuffer, const int fileSize) {
    m_pSetting = setting;
    return m_lexer.Create(fileBuffer, fileSize);
}
}

%token INITIAL_EQUIPMENT_BEGIN
%token INITIAL_EQUIPMENT_END
%token INITIAL_CUSTOM_EQUIPMENT_BEGIN
%token INITIAL_CUSTOM_EQUIPMENT_END
%token INITIAL_START_POINT_BEGIN
%token INITIAL_START_POINT_END
%token RESTART_POINT_BEGIN
%token RESTART_POINT_END
%token MINIMUM_STAT_BEGIN
%token MINIMUM_STAT_END
%token MAXIMUM_STAT_BEGIN
%token MAXIMUM_STAT_END
%token RECOMMENDED_STAT_BEGIN
%token RECOMMENDED_STAT_END
%token ABNORMAL_RESIST_BEGIN
%token ABNORMAL_RESIST_END
%token POINT_BEGIN
%token POINT_END
%token AREA_BEGIN
%token AREA_END
%token CLASS
%token POINT
%token CHAO_POINT
%token MAP
%token POINT_NAME
%token BANNED_RACE
%token RANGE
%token RACE
%token BBS
%token LOC_NAME
%token NAME
%token IDENTIFIER
%token INTEGER
%token MON_GROUP
%token OLYMPIAD_ARENA_BEGIN
%token OLYMPIAD_ARENA_END
%token ARENA_BEGIN
%token ARENA_END
%token ID
%token POINT1
%token POINT2
%token OLYMPIAD_GENARAL_SETTING_BEGIN
%token OLYMPIAD_GENERAL_SETTING_END
%token FORBIDDEN_WEAPON
%token FORBIDDEN_SKILL
%token FORBIDDEN_ITEM
%token GIVE_SKILL_CLASSLESS
%token GIVE_SKILL_CLASS
%token GIVE_SKILL_TEAM
%token CLASSLESS_BONUS
%token CLASS_BONUS
%token TEAM_BONUS
%token OLYMPIAD_TIME_SETTING
%token OLYMPIAD_ENDTIME_SETTING
%token OLYMPIAD_MATCH_DAYS
%token OLYMPIAD_POINT_DENOMINATOR
%token OLYMPIAD_POINT_WEIGHT
%token OLYMPIAD_DOOR
%token OLYMPIAD_RESET_SKILL
%token OLYMPIAD_RESET_SKILL_EXCLUDED
%token OLYMPIAD_MATCH_LIMIT
%token WINNER_REWARD_ITEM
%token ADDITIONAL_REWARD_ITEM
%token PLEDGE_GAME_SETTING_START
%token PLEDGE_GAME_SETTING_END
%token PLEDGE_GAME_START_TIME
%token PLEDGE_GAME_END_TIME
%token HERO_GENERAL_SETTING_START
%token HERO_GENERAL_SETTING_END
%token HERO_SKILL
%token PVPMATCH_SETTING_START
%token PVPMATCH_SETTING_END
%token GIVE_PVPPOINT_TIME_START
%token GIVE_PVPPOINT_TIME_END
%token GIVE_PVPPOINT_START_TIME
%token GIVE_PVPPOINT_END_TIME
%token GIVE_PVPPOINT_WEEK_DAYS
%token CLEFT_SETTING_BEGIN
%token CLEFT_SETTING_END
%token CLEFT_ZONE_TERRITORY
%token RED_START_POINT_BEGIN
%token RED_START_POINT_END
%token BLUE_START_POINT_BEGIN
%token BLUE_START_POINT_END
%token RED_BANISH_POINT_BEGIN
%token RED_BANISH_POINT_END
%token BLUE_BANISH_POINT_BEGIN
%token BLUE_BANISH_POINT_END
%token CAT_PERIOD
%token DEFAULT_REWARD
%token PCBANG_DEFAULT_REWARD
%token WINNER_DEFAULT_REWARD
%token WINNER_REWARD
%token LOSER_REWARD
%token DRAWER_REWARD
%token WINNER_SP_BONUS
%token LOSER_SP_BONUS
%token KILL_SP_BONUS
%token WAITING_SKILL
%token MINIMUM_POINT
%token DELETE_ITEMS_AFTER_MATCH
%token BLOCKUPSET_SETTING_BEGIN
%token BLOCKUPSET_SETTING_END
%token GAME_PERIOD
%token BLUE_ENTER_SKILL
%token RED_ENTER_SKILL
%token BLOCKUPSET_STAGE_BEGIN
%token BLOCKUPSET_STAGE_END
%token STAGE
%token BLOCKUPSET_ZONE_TERRITORY
%token MENTOR_SETTING_BEGIN
%token MENTOR_SETTING_END
%token MENTOR_SKILL
%token MENTEE_SKILL
%token MENTEE_ITEM_COND
%token WITHOUTMENTOR_SKILL
%token MENTEE_LEV_UP_REWARD
%token MENTEE_LEV_UP_TITLE
%token MENTEE_LEV_UP_CONTENTS
%token MENTEE_LEV_UP_REWARD_ACADEMY
%token MENTEE_GRADUATE_REWARD
%token MENTEE_GRADUATE_TITLE
%token MENTEE_GRADUATE_CONTENTS
%token MENTEE_JOIN_REWARD
%token MENTEE_JOIN_TITLE
%token MENTEE_JOIN_CONTENTS
%token PLEDGEGAME_SETTING_BEGIN
%token PLEDGEGAME_SETTING_END
%token RESERVABLE_PLEDGEGAME_TIME
%token COMMON_LOSE_REWARD
%token COMMON_WIN_REWARD
%token RAND_WIN_MIN_MAX_REWARD
%token TRAINING_ZONE_SETTING_BEGIN
%token TRAINING_ZONE_SETTING_END
%token TRAINING_ZONE_COMPLETE_HTML
%token TRAINING_ZONE_POSITION
%token KAMAROKA_SETTING_START
%token KAMAROKA_SETTING_END
%token KAMAROKA_DAYS
%token ITEM_SKILL_TRANSFER_LIST
%token VIP_ATTENDANCE_SETTING_BEGIN
%token VIP_ATTENDANCE_SETTING_END
%token VIP_ATTENDANCE_RESET_TIME
%token CLANBONUS_SETTING_BEGIN
%token CLANBONUS_SETTING_END
%token ACCESS_GOAL_RATE
%token HUNT_GOAL_RATE
%token ACCESS_REWARD
%token HUNT_REWARD
%token EXP_TO_HUNTPOINT
%token PLEDGE_RAID_BEGIN
%token PLEDGE_RAID_END
%token PLEDGE_RAID_RANK_REWARD_BEGIN
%token PLEDGE_RAID_RANK_REWARD_END
%token PLEDGE_RAID_TIER_REWARD_BEGIN
%token PLEDGE_RAID_TIER_REWARD_END
%token PLEDGE_RAID_MASTERY_REWARD_BEGIN
%token PLEDGE_RAID_MASTERY_REWARD_END
%token RANK_REWARD
%token TIER_REWARD
%token MASTERY_REWARD
%token PLEDGE_RAID_HUNT_NPC_NAME
%token PLEDGE_MISSION_BEGIN
%token PLEDGE_MISSION_END
%token PLEDGE_MISSION_COMMON_MAX_BEGIN
%token PLEDGE_MISSION_COMMON_MAX_END
%token NOVICE
%token NOBLESS
%token HONORS
// The original grammar has 35 shift/reduce and 11 reduce/reduce conflicts. Bison
// resolves them as the original tables did. A shift wins over a reduce,
// and the earlier rule wins over a later rule.
%start n1

%%

n1
    : n1 n2
    | %empty
    ;

n2
    : initial_equipment
    | initial_custom_equipment
    | initial_start_point
    | restart_point
    | minimum_stat
    | maximum_stat
    | n42
    | n44
    | olympiad_arena
    | olympiad_genaral_setting
    | hero_general_setting
    | pvpmatch_setting
    | cleft_setting
    | blockupset_setting
    | mentor_setting
    | pledgegame_setting
    | training_zone_setting
    | kamaroka_setting
    | n132
    | vip_attendance_setting
    | clanbonus_setting
    | pledge_raid
    | pledge_mission
    ;

initial_equipment
    : INITIAL_EQUIPMENT_BEGIN n5 INITIAL_EQUIPMENT_END
    ;

initial_custom_equipment
    : INITIAL_CUSTOM_EQUIPMENT_BEGIN n7 INITIAL_CUSTOM_EQUIPMENT_END
    ;

n5
    : n5 n6
    | n6
    ;

n6
    : identifier_def '=' '{' n11 '}'
      {
          const bool selected = !g_bUseCustomEquipment;
          if (selected && tempClass != static_cast<ClassType>(-1)) {
              auto& equipment = m_pSetting->m_InitialEquipment[static_cast<int>(tempClass)];
              for (const auto& item : tempInitItemList) {
                  equipment.push_back(item);
              }
          }
          tempInitItemList.clear();
          tempClass = static_cast<ClassType>(-1);
      }
    ;

n7
    : n7 n8
    | n8
    ;

n8
    : identifier_def '=' '{' n11 '}'
      {
          const bool selected = g_bUseCustomEquipment;
          if (selected && tempClass != static_cast<ClassType>(-1)) {
              auto& equipment = m_pSetting->m_InitialEquipment[static_cast<int>(tempClass)];
              for (const auto& item : tempInitItemList) {
                  equipment.push_back(item);
              }
          }
          tempInitItemList.clear();
          tempClass = static_cast<ClassType>(-1);
      }
    ;

identifier_def
    : IDENTIFIER
      {
          const char16_t* const name = SemanticText($1);
          tempClass = FindSettingClass(name, m_lexer.line + 1);
          // Noise: FindSettingClass already reports this same class name and line.
          // if (tempClass == static_cast<ClassType>(-1)) {
          //     const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
          //     // Log.Add(
          //     //     LOG_ERROR,
          //     //     L"%s[%d]: invalid class [%s]",
          //     //     L"..\\Script\\setting.txt",
          //     //     m_lexer.line + 1,
          //     //     hostName.c_str())
          //     L2_LOG_ERROR(Log,
          //         L"%s[%d]: invalid class [%s]",
          //         L"..\\Script\\setting.txt",
          //         m_lexer.line + 1,
          //         hostName.c_str())
          // }
      }
    ;

n10
    : n10 ';' name_def
    | name_def
    ;

n11
    : n11 ';' n13
    | n13
    ;

name_def
    : NAME
      {
          const auto reportMissingItem = [this](const char16_t* name) {
              // const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              // L2_LOG_ERROR(Log,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              L2_LOG_ERROR(
                  Log, u"Settings line %lld: no positive item ID was resolved for '%ls'; the item reference is invalid.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
              );
          };
          const auto resolveItem = [&reportMissingItem](const char16_t* name) {
              const int itemId = g_ObjectDB.GetClassIdFromName(name, false);
              if (itemId <= 0) {
                  reportMissingItem(name);
              }
              return itemId;
          };
          const int itemId = resolveItem(SemanticText($1));
          if (itemId > 0) {
              tempItemList.push_back(itemId);
          }
      }
    ;

n13
    : '{' NAME ';' INTEGER '}'
      {
          const auto reportMissingItem = [this](const char16_t* name) {
              // const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              // L2_LOG_ERROR(Log,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              L2_LOG_ERROR(
                  Log, u"Settings line %lld: no positive item ID was resolved for '%ls'; the item reference is invalid.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
              );
          };
          const auto resolveItem = [&reportMissingItem](const char16_t* name) {
              const int itemId = g_ObjectDB.GetClassIdFromName(name, false);
              if (itemId <= 0) {
                  reportMissingItem(name);
              }
              return itemId;
          };
          const int itemId = resolveItem(SemanticText($2));
          if (itemId > 0) {
              tempInitItemList.emplace_back(itemId, grammar::SemanticInt64($4));
          }
      }
    ;

initial_start_point
    : INITIAL_START_POINT_BEGIN point_list INITIAL_START_POINT_END
    ;

point_list
    : point_list point
    | point
    ;

point
    : POINT_BEGIN point_def_list class_def POINT_END
      {
          for (const FVector& point : tempPointList) {
              tempPosition.m_Point = point;
              tempPosition.m_Bit = tempClassBit;
              m_pSetting->m_StartPoint.push_back(tempPosition);
          }
          tempPointList.clear();
      }
    ;

point_def_list
    : point_def_list point_def
    | point_def
    ;

point_def
    : POINT '=' n19
      { tempPointList.push_back(tempPoint); }
    | CHAO_POINT '=' n19
      { tempChaoPointList.push_back(tempPoint); }
    ;

n19
    : '{' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          tempPoint =
              FVector(grammar::SemanticInt32($2), grammar::SemanticInt32($4), grammar::SemanticInt32($6));
      }
    ;

class_def
    : CLASS '=' '{' n21 '}'
    ;

n21
    : n21 ';' IDENTIFIER
      {
          tempClassBit |= FindSettingClassBit(SemanticText($3), m_lexer.line + 1);
      }
    | IDENTIFIER
      {
          tempClassBit = FindSettingClassBit(SemanticText($1), m_lexer.line + 1);
      }
    ;

restart_point
    : RESTART_POINT_BEGIN restart_point_list RESTART_POINT_END
      { m_pSetting->PostProcessRestartPointGroup(); }
    ;

restart_point_list
    : restart_point_list restart_point
    | restart_point
    ;

restart_point
    : point_2 point_name_def point_def_list map_def_list banned_race_def_list bbs_def loc_name_def POINT_END
      {
          for (const FVector& point : tempPointList) {
              pTempRestartPointGroup->m_PointsForNormal.push_back(point);
          }
          for (const FVector& point : tempChaoPointList) {
              pTempRestartPointGroup->m_PointsForChao.push_back(point);
          }
          m_pSetting->AddRestartPointGroup(pTempRestartPointGroup);
      }
    | area range_def race_def_list AREA_END
      { m_pSetting->AddRestartTerritory(pTempRestartTerritory); }
    ;

point_2
    : POINT_BEGIN
      {
          tempPointList.clear();
          tempChaoPointList.clear();
          pTempRestartPointGroup = new CSetting::RestartPointGroup;
          pTempRestartPointGroup->m_nBBS = 0;
          pTempRestartPointGroup->m_nLocName = 0;
          pTempRestartPointGroup->m_bNoviceZone = false;
      }
    ;

area
    : AREA_BEGIN
      {
          pTempRestartTerritory = new CSetting::RestartTerritory;
          m_pTerritory = &pTempRestartTerritory->m_Territory;
      }
    ;

range_def
    : RANGE '=' '{' n27 '}'
    ;

n27
    : n28
    | n27 ';' n28
    ;

n28
    : '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          const int bottom = grammar::SemanticInt32($6);
          const int top = grammar::SemanticInt32($8);
          if (bottom < m_pTerritory->GetBottom()) {
              m_pTerritory->SetBottom(bottom);
          }
          if (top > m_pTerritory->GetTop()) {
              m_pTerritory->SetTop(top);
          }
          FVector* const point = new FVector(grammar::SemanticInt32($2), grammar::SemanticInt32($4), 0);
          m_pTerritory->AddTerritoryPoint(point);
      }
    ;

map_def_list
    : map_def_list map_def
    | map_def
    ;

map_def
    : MAP '=' '{' INTEGER ';' INTEGER '}'
      {
          m_pSetting->MapRestartPointGroup(
              pTempRestartPointGroup, grammar::SemanticInt32($4), grammar::SemanticInt32($6)
          );
      }
    | MAP '=' '{' '}'
    ;

banned_race_def_list
    : banned_race_def_list banned_race_def
    | banned_race_def
    ;

point_name_def
    : POINT_NAME '=' NAME
      {
          pTempRestartPointGroup->m_sName.assign(std::u16string_view(SemanticText($3)));
      }
    ;

bbs_def
    : BBS '=' INTEGER
      { pTempRestartPointGroup->m_nBBS = grammar::SemanticInt32($3); }
    ;

loc_name_def
    : LOC_NAME '=' INTEGER
      { pTempRestartPointGroup->m_nLocName = grammar::SemanticInt32($3); }
    ;

banned_race_def
    : BANNED_RACE '=' '{' IDENTIFIER ';' NAME '}'
      {
          tempRace = FindSettingRace(SemanticText($4), true);
          if (tempRace == RT_NONE) {
              // const std::wstring hostRace = target_utf16::ToHostCodeUnits(SemanticText($4))
              const std::wstring hostRace = Utf16ToWide(SemanticText($4));
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d]: unknown race [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostRace.c_str())
              // L2_LOG_ERROR(Log,
              //     L"%s[%d]: unknown race [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostRace.c_str())
              L2_LOG_ERROR(
                  Log, u"Settings line %lld: unknown race '%ls'; restart-group race restriction was not applied.",
                  static_cast<long long>(m_lexer.line) + 1, hostRace.c_str()
              );
              break;
          }
          const int race = static_cast<int>(tempRace);
          pTempRestartPointGroup->m_bBanned[race] = true;
          pTempRestartPointGroup->m_sBannedAlternativeName[race].assign(std::u16string_view(SemanticText($6)));
      }
    | BANNED_RACE '=' '{' '}'
    ;

race_def_list
    : race_def_list race_def
    | race_def
    ;

race_def
    : RACE '=' '{' IDENTIFIER ';' NAME '}'
      {
          tempRace = FindSettingRace(SemanticText($4), false);
          if (tempRace == RT_NONE) {
              // const std::wstring hostRace = target_utf16::ToHostCodeUnits(SemanticText($4))
              const std::wstring hostRace = Utf16ToWide(SemanticText($4));
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d]: unknown race [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostRace.c_str())
              // L2_LOG_ERROR(Log,
              //     L"%s[%d]: unknown race [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostRace.c_str())
              L2_LOG_ERROR(
                  Log, u"Settings line %lld: unknown race '%ls'; restart-territory race assignment was not applied.",
                  static_cast<long long>(m_lexer.line) + 1, hostRace.c_str()
              );
              break;
          }
          const int race = static_cast<int>(tempRace);
          pTempRestartTerritory->m_bRace[race] = true;
          pTempRestartTerritory->m_sRestartPointGroupName[race].assign(std::u16string_view(SemanticText($6)));
      }
    ;

minimum_stat
    : MINIMUM_STAT_BEGIN
      { tempStatArray = m_pSetting->m_minimum_stat; }
      identifier_def_2_list MINIMUM_STAT_END
    ;

maximum_stat
    : MAXIMUM_STAT_BEGIN
      { tempStatArray = m_pSetting->m_maximum_stat; }
      identifier_def_2_list MAXIMUM_STAT_END
    ;

n42
    : recommended_stat identifier_def_2_list RECOMMENDED_STAT_END
    ;

recommended_stat
    : RECOMMENDED_STAT_BEGIN
      { tempStatArray = m_pSetting->m_recommended_stat; }
    ;

n44
    : abnormal_resist n46 ABNORMAL_RESIST_END
    ;

abnormal_resist
    : ABNORMAL_RESIST_BEGIN
      { tempResistType = m_pSetting->m_ResistType; }
    ;

n46
    : n46 n44
    | %empty
    ;

n44
    : IDENTIFIER '=' '{' INTEGER ';' INTEGER '}'
      {
          tempClass = FindSettingClass(SemanticText($1), m_lexer.line + 1);
          if (tempClass == static_cast<ClassType>(-1)) {
              break;
          }
          CSetting::AbnormalResistType& resist = tempResistType[static_cast<int>(tempClass)];
          resist.m_PAbnormalResist = grammar::SemanticInt32($4);
          resist.m_MAbnormalResist = grammar::SemanticInt32($6);
      }
    ;

identifier_def_2_list
    : identifier_def_2_list identifier_def_2
    | %empty
    ;

identifier_def_2
    : IDENTIFIER '=' '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          tempClass = FindSettingClass(SemanticText($1), m_lexer.line + 1);
          if (tempClass == static_cast<ClassType>(-1)) {
              break;
          }
          CSetting::StatValueType& stats = tempStatArray[static_cast<int>(tempClass)];
          stats.Int = grammar::SemanticInt32($4);
          stats.Str = grammar::SemanticInt32($6);
          stats.Con = grammar::SemanticInt32($8);
          stats.Men = grammar::SemanticInt32($10);
          stats.Dex = grammar::SemanticInt32($12);
          stats.Wit = grammar::SemanticInt32($14);
          stats.Luc = grammar::SemanticInt32($16);
          stats.Cha = grammar::SemanticInt32($18);
      }
    ;

olympiad_arena
    : OLYMPIAD_ARENA_BEGIN n50 OLYMPIAD_ARENA_END
    ;

n50
    : n51 n50
    | %empty
    ;

n51
    : arena n53 ARENA_END
    ;

arena
    : ARENA_BEGIN
    ;

n53
    : n54 n53
    | %empty
    ;

n54
    : id_def
    | n56
    | point1_def
    | point2_def
    | olympiad_door_def
    ;

id_def
    : ID '=' INTEGER
    ;

n56
    : range_def
    ;

point1_def
    : POINT1 '=' '{' n59 '}'
    ;

point2_def
    : POINT2 '=' '{' n59 '}'
    ;

n59
    : n59 ';' n60
    | n60
    ;

n60
    : n19
    ;

olympiad_door_def
    : OLYMPIAD_DOOR '=' '{' NAME ';' NAME '}'
    ;

olympiad_genaral_setting
    : OLYMPIAD_GENARAL_SETTING_BEGIN n63 OLYMPIAD_GENERAL_SETTING_END
    ;

n63
    : n64 n63
    | %empty
    ;

n64
    : forbidden_weapon_def
    | forbidden_skill_def
    | forbidden_item_def
    | n70
    | classless_bonus_def
    | olympiad_time_setting_def
    | olympiad_endtime_setting_def
    | olympiad_match_days_def
    | olympiad_point_denominator_def
    | olympiad_point_weight_def
    | n80
    | olympiad_match_limit_def
    | winner_reward_item_def
    ;

forbidden_weapon_def
    : FORBIDDEN_WEAPON '=' '{' n10 '}'
      {
          WOlympiadDefaultSetting& setting = Singleton<WOlympiadDefaultSetting>::inst();
          for (const int itemId : tempItemList) {
              setting.AddForbiddenWeapon(itemId);
          }
          tempItemList.clear();
      }
    | FORBIDDEN_WEAPON '=' '{' '}'
    ;

forbidden_skill_def
    : FORBIDDEN_SKILL '=' '{' n67 '}'
      {
          for (std::size_t index = 0; index < tempSkillList.size(); ++index) {
              static_cast<void>(Singleton<WOlympiadDefaultSetting>::inst());
          }
          tempSkillList.clear();
      }
    | FORBIDDEN_SKILL '=' '{' '}'
    ;

n67
    : n67 ';' name_def_2
    | name_def_2
    ;

name_def_2
    : NAME
      {
          const auto resolveSkill = [this](const char16_t* name) {
              const std::wstring hostName = target_utf16::ToHostCodeUnits(name);
              const CSkillInfo* const skill = g_SkillDB.GetSkillInfoByName(hostName.c_str());
              if (skill == nullptr) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"%s[%d] : Undefined skill(%s)",
                  //     L"..\\Script\\setting.txt",
                  //     m_lexer.line + 1,
                  //     hostName.c_str())
                  // L2_LOG_ERROR(Log,
                  //     L"%s[%d] : Undefined skill(%s)",
                  //     L"..\\Script\\setting.txt",
                  //     m_lexer.line + 1,
                  //     hostName.c_str())
                  L2_LOG_ERROR(
                      Log, u"Settings line %lld: skill '%ls' was not found; lookup returns null.",
                      static_cast<long long>(m_lexer.line) + 1, name
                  );
              }
              return skill;
          };
          const CSkillInfo* const skill = resolveSkill(SemanticText($1));
          if (skill != nullptr) {
              tempSkillList.push_back(PackSkillKey(*skill));
          }
      }
    ;

forbidden_item_def
    : FORBIDDEN_ITEM '=' '{' n10 '}'
      {
          WOlympiadDefaultSetting& setting = Singleton<WOlympiadDefaultSetting>::inst();
          for (const int itemId : tempItemList) {
              setting.AddForbiddenItem(itemId);
          }
          tempItemList.clear();
      }
    | FORBIDDEN_ITEM '=' '{' '}'
    ;

n70
    : give_skill_classless_def '=' '{' n72 '}'
    ;

give_skill_classless_def
    : GIVE_SKILL_CLASSLESS
      { tempRule = GRT_CLASSLESS; }
    | GIVE_SKILL_CLASS
      { tempRule = GRT_CLASS; }
    | GIVE_SKILL_TEAM
      { tempRule = GRT_TEAM; }
    ;

n72
    : n72 n73
    | n73
    ;

n73
    : identifier_def '=' '{' n67 '}'
      {
          if (tempClass != static_cast<ClassType>(-1)) {
              WOlympiadDefaultSetting& setting = Singleton<WOlympiadDefaultSetting>::inst();
              for (const std::int64_t skillKey : tempSkillList) {
                  setting.AddGiveSkill(tempClass, tempRule, skillKey);
              }
          }
          tempSkillList.clear();
          tempClass = static_cast<ClassType>(-1);
      }
    | identifier_def '=' '{' '}'
      { tempClass = static_cast<ClassType>(-1); }
    ;

classless_bonus_def
    : CLASSLESS_BONUS '=' '{' name_def '=' INTEGER ';' name_def '=' INTEGER '}'
      {
          if (!tempItemList.empty()) {
              const GameRuleType rule = GRT_CLASSLESS;
              Singleton<WOlympiadDefaultSetting>::inst().SetBonusItemData(
                  rule, runtime::CheckedVectorAt(tempItemList, 0), grammar::SemanticInt32($6),
                  tempItemList.size() > 1 ? runtime::CheckedVectorAt(tempItemList, 1) : 0, grammar::SemanticInt32($10)
              );
          }
          tempItemList.clear();
      }
    | CLASS_BONUS '=' '{' name_def '=' INTEGER ';' name_def '=' INTEGER '}'
      {
          if (!tempItemList.empty()) {
              const GameRuleType rule = (GRT_CLASS);
              Singleton<WOlympiadDefaultSetting>::inst().SetBonusItemData(
                  rule, runtime::CheckedVectorAt(tempItemList, 0), grammar::SemanticInt32($6),
                  tempItemList.size() > 1 ? runtime::CheckedVectorAt(tempItemList, 1) : 0, grammar::SemanticInt32($10)
              );
          }
          tempItemList.clear();
      }
    | TEAM_BONUS '=' '{' name_def '=' INTEGER ';' name_def '=' INTEGER '}'
      {
          if (!tempItemList.empty()) {
              const GameRuleType rule = (GRT_TEAM);
              Singleton<WOlympiadDefaultSetting>::inst().SetBonusItemData(
                  rule, runtime::CheckedVectorAt(tempItemList, 0), grammar::SemanticInt32($6),
                  tempItemList.size() > 1 ? runtime::CheckedVectorAt(tempItemList, 1) : 0, grammar::SemanticInt32($10)
              );
          }
          tempItemList.clear();
      }
    ;

olympiad_time_setting_def
    : OLYMPIAD_TIME_SETTING '=' '{' INTEGER ':' INTEGER '}'
      {
          Singleton<WOlympiadDefaultSetting>::inst().SetTimeSetting(
              grammar::SemanticInt32($4), grammar::SemanticInt32($6)
          );
      }
    ;

olympiad_endtime_setting_def
    : OLYMPIAD_ENDTIME_SETTING '=' '{' INTEGER ':' INTEGER '}'
      {
          Singleton<WOlympiadDefaultSetting>::inst().SetEndTime(
              grammar::SemanticInt32($4), grammar::SemanticInt32($6)
          );
      }
    ;

olympiad_match_days_def
    : OLYMPIAD_MATCH_DAYS '=' '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          const std::array<grammar::SemanticBits, 7> days{$4, $6, $8, $10, $12, $14, $16};
          WOlympiadDefaultSetting& setting = Singleton<WOlympiadDefaultSetting>::inst();
          for (std::size_t day = 0; day < days.size(); ++day) {
              if (grammar::SemanticInt32(days[day]) == 1) {
                  setting.SetDaysSetting(static_cast<int>(day));
              }
          }
      }
    ;

olympiad_point_denominator_def
    : OLYMPIAD_POINT_DENOMINATOR '=' '{' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          WOlympiadDefaultSetting& setting = Singleton<WOlympiadDefaultSetting>::inst();
          setting.SetDenominator(GRT_CLASSLESS, grammar::SemanticInt32($4));
          setting.SetDenominator(GRT_CLASS, grammar::SemanticInt32($6));
          setting.SetDenominator(GRT_TEAM, grammar::SemanticInt32($8));
      }
    ;

olympiad_point_weight_def
    : OLYMPIAD_POINT_WEIGHT '=' '{' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          WOlympiadDefaultSetting& setting = Singleton<WOlympiadDefaultSetting>::inst();
          setting.SetWeight(GRT_CLASSLESS, grammar::SemanticInt32($4));
          setting.SetWeight(GRT_CLASS, grammar::SemanticInt32($6));
          setting.SetWeight(GRT_TEAM, grammar::SemanticInt32($8));
      }
    ;

n80
    : olympiad_reset_skill_def
    | olympiad_reset_skill_excluded_def
    ;

olympiad_reset_skill_def
    : OLYMPIAD_RESET_SKILL '=' INTEGER
      {
          Singleton<WOlympiadDefaultSetting>::inst().SetResetSkillLimit(grammar::SemanticInt32($3));
      }
    ;

olympiad_reset_skill_excluded_def
    : OLYMPIAD_RESET_SKILL_EXCLUDED '=' '{' n67 '}'
      {
          Singleton<WOlympiadDefaultSetting>::inst().AddNotResetSkill(&tempSkillList);
          tempSkillList.clear();
      }
    ;

olympiad_match_limit_def
    : OLYMPIAD_MATCH_LIMIT '=' '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          WOlympiadDefaultSetting& setting = Singleton<WOlympiadDefaultSetting>::inst();
          setting.SetMatchLimit(GRT_MAX, grammar::SemanticInt32($4));
          setting.SetMatchLimit(GRT_CLASSLESS, grammar::SemanticInt32($6));
          setting.SetMatchLimit(GRT_CLASS, grammar::SemanticInt32($8));
          setting.SetMatchLimit(GRT_TEAM, grammar::SemanticInt32($10));
      }
    ;

winner_reward_item_def
    : WINNER_REWARD_ITEM '=' '{' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          std::vector<int> rewards;
          rewards.push_back(grammar::SemanticInt32($4));
          rewards.push_back(grammar::SemanticInt32($6));
          rewards.push_back(grammar::SemanticInt32($8));
          Singleton<WOlympiadDefaultSetting>::inst().SetRewardCount(&rewards);
      }
    | ADDITIONAL_REWARD_ITEM '=' '{' INTEGER ';' INTEGER '}'
      {
          Singleton<WOlympiadDefaultSetting>::inst().SetAdditionalRewardCount(
              grammar::SemanticInt32($4), grammar::SemanticInt32($6)
          );
      }
    ;

hero_general_setting
    : HERO_GENERAL_SETTING_START n91 HERO_GENERAL_SETTING_END
    ;

n91
    : hero_skill_def
    ;

hero_skill_def
    : HERO_SKILL '=' '{' n67 '}'
      {
          for (const std::int64_t skillKey : tempSkillList) {
              Singleton<WHeroSystem>::inst().AddGiveSkill(skillKey);
          }
          tempSkillList.clear();
      }
    ;

pvpmatch_setting
    : PVPMATCH_SETTING_START give_pvppoint_time_list PVPMATCH_SETTING_END
    ;

give_pvppoint_time_list
    : give_pvppoint_time_list give_pvppoint_time
    | give_pvppoint_time
    ;

give_pvppoint_time
    : GIVE_PVPPOINT_TIME_START
      { pTimeBlock = new CPVPMatchSystem::CPVPPointTimeBlock; }
      give_pvppoint_start_time_def give_pvppoint_end_time_def give_pvppoint_week_days_def GIVE_PVPPOINT_TIME_END
      {
          if (pTimeBlock->IsValid()) {
              CPVPMatchSystem::inst().AddGivePVPPointTime(pTimeBlock);
          }
      }
    ;

give_pvppoint_start_time_def
    : GIVE_PVPPOINT_START_TIME '=' '{' INTEGER ':' INTEGER '}'
      {
          const int hour = grammar::SemanticInt32($4);
          const int minute = grammar::SemanticInt32($6);
          pTimeBlock->SetStartTime(hour, minute);
          if (hour < 0 || hour >= 24 || minute < 0 || minute >= 60) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d]: invalid time value.",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"%s[%d]: invalid time value.",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"Settings line %lld: PVP point start time has hour %d, minute %d; expected 00:00..23:59. The supplied "
                  u"time is already stored.",
                  static_cast<long long>(m_lexer.line) + 1, hour, minute
              );
          }
      }
    ;

give_pvppoint_end_time_def
    : GIVE_PVPPOINT_END_TIME '=' '{' INTEGER ':' INTEGER '}'
      {
          const int hour = grammar::SemanticInt32($4);
          const int minute = grammar::SemanticInt32($6);
          pTimeBlock->SetEndTime(hour, minute);
          const bool validHour = hour >= 0 && hour < 25 && (hour != 24 || minute < 1);
          if (!validHour || minute < 0 || minute >= 60) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d]: invalid time value.",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"%s[%d]: invalid time value.",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"Settings line %lld: PVP point end time has hour %d, minute %d; expected 00:00..24:00. The supplied "
                  u"time is already stored.",
                  static_cast<long long>(m_lexer.line) + 1, hour, minute
              );
          }
      }
    ;

give_pvppoint_week_days_def
    : GIVE_PVPPOINT_WEEK_DAYS '=' '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          int weekDays[7]{grammar::SemanticInt32($4),  grammar::SemanticInt32($6),  grammar::SemanticInt32($8),
                          grammar::SemanticInt32($10), grammar::SemanticInt32($12), grammar::SemanticInt32($14),
                          grammar::SemanticInt32($16)};
          pTimeBlock->SetWeekDays(weekDays);
          // Noise: report one invalid weekday record instead of repeating the same error for each bad flag.
          // for (const int value : weekDays) {
          //     if (value != 0 && value != 1) {
          //         // Log.Add(
          //         //     LOG_ERROR,
          //         //     L"%s[%d]: weekdays setting is must be 0 or 1.",
          //         //     L"..\\Script\\setting.txt",
          //         //     m_lexer.line + 1)
          //         L2_LOG_ERROR(Log,
          //             L"%s[%d]: weekdays setting is must be 0 or 1.",
          //             L"..\\Script\\setting.txt",
          //             m_lexer.line + 1)
          //     }
          // }
          if (std::any_of(std::begin(weekDays), std::end(weekDays), [](const int value) {
                  return value != 0 && value != 1;
              })) {
              L2_LOG_ERROR(
                  Log,
                  u"Settings line %lld: PVP weekday flags (Sun..Sat) [%d, %d, %d, %d, %d, %d, %d] must be 0 or 1; the "
                  u"supplied flags are already stored.",
                  static_cast<long long>(m_lexer.line) + 1, weekDays[0], weekDays[1], weekDays[2], weekDays[3], weekDays[4],
                  weekDays[5], weekDays[6]
              );
          }
      }
    ;

cleft_setting
    : CLEFT_SETTING_BEGIN
      {
          m_pTerritory = &m_pCleftMatch->m_Territory;
          tempPointList.clear();
      }
      n102 CLEFT_SETTING_END
      {
          m_pCleftMatch->SetTimeProperty(25, 5, m_CleftData.m_CatPeriod);
          m_pCleftMatch->SetSpBonus(m_CleftData.m_WinnerSpBonus, m_CleftData.m_LoserSpBonus, m_CleftData.m_KillSpBonus);
          m_pTerritory = nullptr;
      }
    ;

n102
    : cleft_zone_territory_def n102
    | %empty
    ;

cleft_zone_territory_def
    : CLEFT_ZONE_TERRITORY '=' '{' n27 '}'
    | RED_START_POINT_BEGIN point_def_list RED_START_POINT_END
      {
          m_pCleftMatch->AddStartPos(BlueRedTeam::RED, tempPointList);
          tempPointList.clear();
      }
    | BLUE_START_POINT_BEGIN point_def_list BLUE_START_POINT_END
      {
          m_pCleftMatch->AddStartPos(BlueRedTeam::BLUE, tempPointList);
          tempPointList.clear();
      }
    | RED_BANISH_POINT_BEGIN point_def_list RED_BANISH_POINT_END
      {
          m_pCleftMatch->AddBanishPos(BlueRedTeam::RED, tempPointList);
          tempPointList.clear();
      }
    | BLUE_BANISH_POINT_BEGIN point_def_list BLUE_BANISH_POINT_END
      {
          m_pCleftMatch->AddBanishPos(BlueRedTeam::BLUE, tempPointList);
          tempPointList.clear();
      }
    | CAT_PERIOD '=' INTEGER
      { m_CleftData.m_CatPeriod = grammar::SemanticInt32($3); }
    | WINNER_REWARD '=' '{' NAME ';' INTEGER '}'
      {
          const auto reportMissingItem = [this](const char16_t* name) {
              // const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              // L2_LOG_ERROR(Log,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              L2_LOG_ERROR(
                  Log, u"Settings line %lld: no positive item ID was resolved for '%ls'; the item reference is invalid.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
              );
          };
          const auto resolveItem = [&reportMissingItem](const char16_t* name) {
              const int itemId = g_ObjectDB.GetClassIdFromName(name, false);
              if (itemId <= 0) {
                  reportMissingItem(name);
              }
              return itemId;
          };
          const int itemId = resolveItem(SemanticText($4));
          if (itemId > 0) {
              m_pCleftMatch->SetWinnerReward(itemId, grammar::SemanticInt64($6));
          }
      }
    | LOSER_REWARD '=' '{' NAME ';' INTEGER '}'
      {
          const auto reportMissingItem = [this](const char16_t* name) {
              // const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              // L2_LOG_ERROR(Log,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              L2_LOG_ERROR(
                  Log, u"Settings line %lld: no positive item ID was resolved for '%ls'; the item reference is invalid.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
              );
          };
          const auto resolveItem = [&reportMissingItem](const char16_t* name) {
              const int itemId = g_ObjectDB.GetClassIdFromName(name, false);
              if (itemId <= 0) {
                  reportMissingItem(name);
              }
              return itemId;
          };
          const int itemId = resolveItem(SemanticText($4));
          if (itemId > 0) {
              m_pCleftMatch->SetLoserReward(itemId, grammar::SemanticInt64($6));
          }
      }
    | WINNER_SP_BONUS '=' INTEGER
      { m_CleftData.m_WinnerSpBonus = grammar::SemanticInt32($3); }
    | LOSER_SP_BONUS '=' INTEGER
      { m_CleftData.m_LoserSpBonus = grammar::SemanticInt32($3); }
    | KILL_SP_BONUS '=' INTEGER
      { m_CleftData.m_KillSpBonus = grammar::SemanticInt32($3); }
    | WAITING_SKILL '=' '{' NAME '}'
      {
          const auto resolveSkill = [this](const char16_t* name) {
              const std::wstring hostName = target_utf16::ToHostCodeUnits(name);
              const CSkillInfo* const skill = g_SkillDB.GetSkillInfoByName(hostName.c_str());
              if (skill == nullptr) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"%s[%d] : Undefined skill(%s)",
                  //     L"..\\Script\\setting.txt",
                  //     m_lexer.line + 1,
                  //     hostName.c_str())
                  // L2_LOG_ERROR(Log,
                  //     L"%s[%d] : Undefined skill(%s)",
                  //     L"..\\Script\\setting.txt",
                  //     m_lexer.line + 1,
                  //     hostName.c_str())
                  L2_LOG_ERROR(
                      Log, u"Settings line %lld: skill '%ls' was not found; lookup returns null.",
                      static_cast<long long>(m_lexer.line) + 1, name
                  );
              }
              return skill;
          };
          const CSkillInfo* const skill = resolveSkill(SemanticText($4));
          if (skill == nullptr) {
              break;
          }
          m_pCleftMatch->SetWaitingSkill(const_cast<CSkillInfo*>(skill));
          const std::uint32_t operateType = static_cast<std::uint32_t>(skill->m_OperateType);
          if (operateType >= static_cast<std::uint32_t>(SKILL_MAX) || ((0x27940eu >> operateType) & 1u) == 0u) {
              // const std::wstring hostName = target_utf16::ToHostCodeUnits(SemanticText($4))
              const std::wstring hostName = Utf16ToWide(SemanticText($4));
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d] : Invalid skill operate type(%s)",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              // L2_LOG_ERROR(Log,
              //     L"%s[%d] : Invalid skill operate type(%s)",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              L2_LOG_ERROR(
                  Log,
                  u"Settings line %lld: Cleft waiting skill '%ls' has unsupported operate type %u; the skill is already "
                  u"assigned.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str(), static_cast<unsigned>(operateType)
              );
          }
      }
    ;

blockupset_setting
    : BLOCKUPSET_SETTING_BEGIN { } n106 n108 BLOCKUPSET_SETTING_END
      {
          m_pBlockUpsetManager->SetSkills(
              m_BlockUpsetData.m_pBlueStartSkill, m_BlockUpsetData.m_pRedStartSkill, m_BlockUpsetData.m_pWaitSkill
          );
          m_BlockUpsetData.Init();
      }
    ;

n106
    : game_period_def n106
    | %empty
    ;

game_period_def
    : GAME_PERIOD '=' INTEGER
      { m_pBlockUpsetManager->SetGamePeriod(grammar::SemanticInt32($3) * 60); }
    | DEFAULT_REWARD '=' '{' NAME ';' INTEGER '}'
      {
          const auto reportMissingItem = [this](const char16_t* name) {
              // const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              // L2_LOG_ERROR(Log,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              L2_LOG_ERROR(
                  Log, u"Settings line %lld: no positive item ID was resolved for '%ls'; the item reference is invalid.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
              );
          };
          const auto resolveItem = [&reportMissingItem](const char16_t* name) {
              const int itemId = g_ObjectDB.GetClassIdFromName(name, false);
              if (itemId <= 0) {
                  reportMissingItem(name);
              }
              return itemId;
          };
          const int itemId = resolveItem(SemanticText($4));
          if (itemId <= 0) {
              break;
          }
          BlockUpsetRewardType::Enum type = BlockUpsetRewardType::DEFAULT;
          std::int64_t step = 0;
          m_pBlockUpsetManager->SetReward(type, itemId, grammar::SemanticInt64($6), step);
      }
    | PCBANG_DEFAULT_REWARD '=' '{' NAME ';' INTEGER '}'
      {
          const auto reportMissingItem = [this](const char16_t* name) {
              // const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              // L2_LOG_ERROR(Log,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              L2_LOG_ERROR(
                  Log, u"Settings line %lld: no positive item ID was resolved for '%ls'; the item reference is invalid.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
              );
          };
          const auto resolveItem = [&reportMissingItem](const char16_t* name) {
              const int itemId = g_ObjectDB.GetClassIdFromName(name, false);
              if (itemId <= 0) {
                  reportMissingItem(name);
              }
              return itemId;
          };
          const int itemId = resolveItem(SemanticText($4));
          if (itemId <= 0) {
              break;
          }
          BlockUpsetRewardType::Enum type = BlockUpsetRewardType::DEFAULT;
          std::int64_t step = 0;
          type = BlockUpsetRewardType::PCBANG_DEFAULT;
          m_pBlockUpsetManager->SetReward(type, itemId, grammar::SemanticInt64($6), step);
      }
    | WINNER_DEFAULT_REWARD '=' '{' NAME ';' INTEGER '}'
      {
          const auto reportMissingItem = [this](const char16_t* name) {
              // const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              // L2_LOG_ERROR(Log,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              L2_LOG_ERROR(
                  Log, u"Settings line %lld: no positive item ID was resolved for '%ls'; the item reference is invalid.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
              );
          };
          const auto resolveItem = [&reportMissingItem](const char16_t* name) {
              const int itemId = g_ObjectDB.GetClassIdFromName(name, false);
              if (itemId <= 0) {
                  reportMissingItem(name);
              }
              return itemId;
          };
          const int itemId = resolveItem(SemanticText($4));
          if (itemId <= 0) {
              break;
          }
          BlockUpsetRewardType::Enum type = BlockUpsetRewardType::DEFAULT;
          std::int64_t step = 0;
          type = BlockUpsetRewardType::WIN_DEFAULT;
          step = 5;
          m_pBlockUpsetManager->SetReward(type, itemId, grammar::SemanticInt64($6), step);
      }
    | WINNER_REWARD '=' '{' NAME ';' INTEGER '}'
      {
          const auto reportMissingItem = [this](const char16_t* name) {
              // const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              // L2_LOG_ERROR(Log,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              L2_LOG_ERROR(
                  Log, u"Settings line %lld: no positive item ID was resolved for '%ls'; the item reference is invalid.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
              );
          };
          const auto resolveItem = [&reportMissingItem](const char16_t* name) {
              const int itemId = g_ObjectDB.GetClassIdFromName(name, false);
              if (itemId <= 0) {
                  reportMissingItem(name);
              }
              return itemId;
          };
          const int itemId = resolveItem(SemanticText($4));
          if (itemId <= 0) {
              break;
          }
          BlockUpsetRewardType::Enum type = BlockUpsetRewardType::DEFAULT;
          std::int64_t step = 0;
          type = BlockUpsetRewardType::WIN;
          step = 5;
          m_pBlockUpsetManager->SetReward(type, itemId, grammar::SemanticInt64($6), step);
      }
    | WINNER_REWARD '=' '{' NAME ';' INTEGER ';' INTEGER '}'
      {
          const auto reportMissingItem = [this](const char16_t* name) {
              // const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              // L2_LOG_ERROR(Log,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              L2_LOG_ERROR(
                  Log, u"Settings line %lld: no positive item ID was resolved for '%ls'; the item reference is invalid.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
              );
          };
          const auto resolveItem = [&reportMissingItem](const char16_t* name) {
              const int itemId = g_ObjectDB.GetClassIdFromName(name, false);
              if (itemId <= 0) {
                  reportMissingItem(name);
              }
              return itemId;
          };
          const int itemId = resolveItem(SemanticText($4));
          if (itemId > 0) {
              m_pBlockUpsetManager->SetReward(
                  BlockUpsetRewardType::WIN, itemId, grammar::SemanticInt64($6), grammar::SemanticInt64($8)
              );
          }
      }
    | LOSER_REWARD '=' '{' NAME ';' INTEGER '}'
      {
          const auto reportMissingItem = [this](const char16_t* name) {
              // const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              // L2_LOG_ERROR(Log,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              L2_LOG_ERROR(
                  Log, u"Settings line %lld: no positive item ID was resolved for '%ls'; the item reference is invalid.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
              );
          };
          const auto resolveItem = [&reportMissingItem](const char16_t* name) {
              const int itemId = g_ObjectDB.GetClassIdFromName(name, false);
              if (itemId <= 0) {
                  reportMissingItem(name);
              }
              return itemId;
          };
          const int itemId = resolveItem(SemanticText($4));
          if (itemId <= 0) {
              break;
          }
          BlockUpsetRewardType::Enum type = BlockUpsetRewardType::DEFAULT;
          std::int64_t step = 0;
          type = BlockUpsetRewardType::LOSE;
          m_pBlockUpsetManager->SetReward(type, itemId, grammar::SemanticInt64($6), step);
      }
    | DRAWER_REWARD '=' '{' NAME ';' INTEGER '}'
      {
          const auto reportMissingItem = [this](const char16_t* name) {
              // const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              // L2_LOG_ERROR(Log,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              L2_LOG_ERROR(
                  Log, u"Settings line %lld: no positive item ID was resolved for '%ls'; the item reference is invalid.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
              );
          };
          const auto resolveItem = [&reportMissingItem](const char16_t* name) {
              const int itemId = g_ObjectDB.GetClassIdFromName(name, false);
              if (itemId <= 0) {
                  reportMissingItem(name);
              }
              return itemId;
          };
          const int itemId = resolveItem(SemanticText($4));
          if (itemId <= 0) {
              break;
          }
          BlockUpsetRewardType::Enum type = BlockUpsetRewardType::DEFAULT;
          std::int64_t step = 0;
          type = BlockUpsetRewardType::DRAW;
          m_pBlockUpsetManager->SetReward(type, itemId, grammar::SemanticInt64($6), step);
      }
    | BLUE_ENTER_SKILL '=' '{' NAME '}'
      {
          const auto resolveSkill = [this](const char16_t* name) {
              const std::wstring hostName = target_utf16::ToHostCodeUnits(name);
              const CSkillInfo* const skill = g_SkillDB.GetSkillInfoByName(hostName.c_str());
              if (skill == nullptr) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"%s[%d] : Undefined skill(%s)",
                  //     L"..\\Script\\setting.txt",
                  //     m_lexer.line + 1,
                  //     hostName.c_str())
                  // L2_LOG_ERROR(Log,
                  //     L"%s[%d] : Undefined skill(%s)",
                  //     L"..\\Script\\setting.txt",
                  //     m_lexer.line + 1,
                  //     hostName.c_str())
                  L2_LOG_ERROR(
                      Log, u"Settings line %lld: skill '%ls' was not found; lookup returns null.",
                      static_cast<long long>(m_lexer.line) + 1, name
                  );
              }
              return skill;
          };
          m_BlockUpsetData.m_pBlueStartSkill = const_cast<CSkillInfo*>(resolveSkill(SemanticText($5)));
      }
    | RED_ENTER_SKILL '=' '{' NAME '}'
      {
          const auto resolveSkill = [this](const char16_t* name) {
              const std::wstring hostName = target_utf16::ToHostCodeUnits(name);
              const CSkillInfo* const skill = g_SkillDB.GetSkillInfoByName(hostName.c_str());
              if (skill == nullptr) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"%s[%d] : Undefined skill(%s)",
                  //     L"..\\Script\\setting.txt",
                  //     m_lexer.line + 1,
                  //     hostName.c_str())
                  // L2_LOG_ERROR(Log,
                  //     L"%s[%d] : Undefined skill(%s)",
                  //     L"..\\Script\\setting.txt",
                  //     m_lexer.line + 1,
                  //     hostName.c_str())
                  L2_LOG_ERROR(
                      Log, u"Settings line %lld: skill '%ls' was not found; lookup returns null.",
                      static_cast<long long>(m_lexer.line) + 1, name
                  );
              }
              return skill;
          };
          m_BlockUpsetData.m_pRedStartSkill = const_cast<CSkillInfo*>(resolveSkill(SemanticText($5)));
      }
    | MINIMUM_POINT '=' INTEGER
      { m_pBlockUpsetManager->m_nMinimumPoint = grammar::SemanticInt32($3); }
    | DELETE_ITEMS_AFTER_MATCH '=' '{' n10 '}'
      {
          for (const int itemId : tempItemList) {
              m_pBlockUpsetManager->DeleteItemsAfterMatch(itemId);
          }
          tempItemList.clear();
      }
    | FORBIDDEN_WEAPON '=' '{' '}'
    | WAITING_SKILL '=' '{' NAME '}'
      {
          const auto resolveSkill = [this](const char16_t* name) {
              const std::wstring hostName = target_utf16::ToHostCodeUnits(name);
              const CSkillInfo* const skill = g_SkillDB.GetSkillInfoByName(hostName.c_str());
              if (skill == nullptr) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"%s[%d] : Undefined skill(%s)",
                  //     L"..\\Script\\setting.txt",
                  //     m_lexer.line + 1,
                  //     hostName.c_str())
                  // L2_LOG_ERROR(Log,
                  //     L"%s[%d] : Undefined skill(%s)",
                  //     L"..\\Script\\setting.txt",
                  //     m_lexer.line + 1,
                  //     hostName.c_str())
                  L2_LOG_ERROR(
                      Log, u"Settings line %lld: skill '%ls' was not found; lookup returns null.",
                      static_cast<long long>(m_lexer.line) + 1, name
                  );
              }
              return skill;
          };
          m_BlockUpsetData.m_pWaitSkill = const_cast<CSkillInfo*>(resolveSkill(SemanticText($5)));
      }
    ;

n108
    : blockupset_stage n108
    | %empty
    ;

blockupset_stage
    : BLOCKUPSET_STAGE_BEGIN { } STAGE '=' INTEGER
      {
          m_BlockUpsetData.m_Stage = grammar::SemanticInt32($5);
          m_pBlockUpset = &m_pBlockUpsetManager->GetStage(m_BlockUpsetData.m_Stage);
          m_pTerritory = &m_pBlockUpset->m_Territory;
      }
      n112 BLOCKUPSET_STAGE_END
      { m_pTerritory = nullptr; }
    ;

n112
    : blockupset_zone_territory_def n112
    | %empty
    ;

blockupset_zone_territory_def
    : BLOCKUPSET_ZONE_TERRITORY '=' '{' n27 '}'
    | RED_START_POINT_BEGIN point_def_list RED_START_POINT_END
      {
          m_pBlockUpset->AddStartPos(BlueRedTeam::RED, tempPointList);
          tempPointList.clear();
      }
    | BLUE_START_POINT_BEGIN point_def_list BLUE_START_POINT_END
      {
          m_pBlockUpset->AddStartPos(BlueRedTeam::BLUE, tempPointList);
          tempPointList.clear();
      }
    | RED_BANISH_POINT_BEGIN point_def_list RED_BANISH_POINT_END
      {
          m_pBlockUpset->AddBanishPos(BlueRedTeam::RED, tempPointList);
          tempPointList.clear();
      }
    | BLUE_BANISH_POINT_BEGIN point_def_list BLUE_BANISH_POINT_END
      {
          m_pBlockUpset->AddBanishPos(BlueRedTeam::BLUE, tempPointList);
          tempPointList.clear();
      }
    ;

mentor_setting
    : MENTOR_SETTING_BEGIN n115 MENTOR_SETTING_END
    ;

n115
    : mentor_skill_def n115
    | %empty
    ;

mentor_skill_def
    : MENTOR_SKILL '=' '{' n67 '}'
      {
          for (const std::int64_t skillKey : tempSkillList) {
              Singleton<CMentorSetting>::inst().AddMentorSkill(skillKey);
          }
          tempSkillList.clear();
      }
    | MENTEE_SKILL '=' '{' n67 '}'
      {
          for (const std::int64_t skillKey : tempSkillList) {
              Singleton<CMentorSetting>::inst().AddMenteeSkill(skillKey);
          }
          tempSkillList.clear();
      }
    | WITHOUTMENTOR_SKILL '=' '{' n67 '}'
      {
          for (const std::int64_t skillKey : tempSkillList) {
              Singleton<CMentorSetting>::inst().AddMenteeSkill_withoutMentor(skillKey);
          }
          tempSkillList.clear();
      }
    | MENTEE_ITEM_COND '=' '{' NAME ';' INTEGER '}'
      {
          const auto reportMissingItem = [this](const char16_t* name) {
              // const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              // L2_LOG_ERROR(Log,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              L2_LOG_ERROR(
                  Log, u"Settings line %lld: no positive item ID was resolved for '%ls'; the item reference is invalid.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
              );
          };
          const auto resolveItem = [&reportMissingItem](const char16_t* name) {
              const int itemId = g_ObjectDB.GetClassIdFromName(name, false);
              if (itemId <= 0) {
                  reportMissingItem(name);
              }
              return itemId;
          };
          const int itemId = resolveItem(SemanticText($4));
          if (itemId > 0) {
              CMentorSetting& mentor = Singleton<CMentorSetting>::inst();
              mentor.m_nMenteeCondItemType = itemId;
              mentor.m_nMenteeCondItemCount = grammar::SemanticInt64($6);
          }
      }
    | MENTEE_LEV_UP_REWARD '=' '{' n117 '}'
      {
          CMentorSetting& mentor = Singleton<CMentorSetting>::inst();
          for (const RewardData& reward : tempRewardList) {
              if (reward.nLev < 2 || reward.nItemId < 1 || reward.nItemCount < 1) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"%s[%d]: there is no item [%d]",
                  //     L"..\\Script\\setting.txt",
                  //     m_lexer.line + 1,
                  //     reward.nItemId)
                  // L2_LOG_ERROR(Log,
                  //     L"%s[%d]: there is no item [%d]",
                  //     L"..\\Script\\setting.txt",
                  //     m_lexer.line + 1,
                  //     reward.nItemId)
                  L2_LOG_ERROR(
                      Log,
                      u"Settings line %lld: invalid mentor level-up reward (level %d, item ID %d, count %d); expected "
                      u"level >= 2, positive item ID and count. Reward skipped.",
                      static_cast<long long>(m_lexer.line) + 1, reward.nLev, reward.nItemId, reward.nItemCount
                  );
                  continue;
              }
              mentor.SetLevUpReward(reward.nLev, reward.nItemId, reward.nItemCount);
          }
          tempRewardList.clear();
      }
    | MENTEE_LEV_UP_REWARD_ACADEMY '=' '{' n117 '}'
      {
          CMentorSetting& mentor = Singleton<CMentorSetting>::inst();
          for (const RewardData& reward : tempRewardList) {
              if (reward.nLev < 2 || reward.nItemId < 1 || reward.nItemCount < 1) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"%s[%d]: there is no item [%d]",
                  //     L"..\\Script\\setting.txt",
                  //     m_lexer.line + 1,
                  //     reward.nItemId)
                  // L2_LOG_ERROR(Log,
                  //     L"%s[%d]: there is no item [%d]",
                  //     L"..\\Script\\setting.txt",
                  //     m_lexer.line + 1,
                  //     reward.nItemId)
                  L2_LOG_ERROR(
                      Log,
                      u"Settings line %lld: invalid academy mentor level-up reward (level %d, item ID %d, count %d); "
                      u"expected level >= 2, positive item ID and count. Reward skipped.",
                      static_cast<long long>(m_lexer.line) + 1, reward.nLev, reward.nItemId, reward.nItemCount
                  );
                  continue;
              }
              mentor.SetLevUpRewardForAcademy(reward.nLev, reward.nItemId, reward.nItemCount);
          }
          tempRewardList.clear();
      }
    | MENTEE_LEV_UP_TITLE '=' NAME
      {
          CopyTargetText(Singleton<CMentorSetting>::inst().m_sLevUpTitle, SemanticText($3));
      }
    | MENTEE_LEV_UP_CONTENTS '=' NAME
      {
          CopyTargetText(Singleton<CMentorSetting>::inst().m_sLevUpContents, SemanticText($3));
      }
    | MENTEE_GRADUATE_REWARD '=' '{' NAME ';' INTEGER '}'
      {
          const auto reportMissingItem = [this](const char16_t* name) {
              // const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              // L2_LOG_ERROR(Log,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              L2_LOG_ERROR(
                  Log, u"Settings line %lld: no positive item ID was resolved for '%ls'; the item reference is invalid.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
              );
          };
          const auto resolveItem = [&reportMissingItem](const char16_t* name) {
              const int itemId = g_ObjectDB.GetClassIdFromName(name, false);
              if (itemId <= 0) {
                  reportMissingItem(name);
              }
              return itemId;
          };
          const int itemId = resolveItem(SemanticText($4));
          if (itemId > 0) {
              CMentorSetting& mentor = Singleton<CMentorSetting>::inst();
              mentor.m_nGraduateRewardItem = itemId;
              mentor.m_nGraduateRewardItemCount = grammar::SemanticInt64($6);
          }
      }
    | MENTEE_GRADUATE_TITLE '=' NAME
      {
          CopyTargetText(Singleton<CMentorSetting>::inst().m_sGraduateTitle, SemanticText($3));
      }
    | MENTEE_GRADUATE_CONTENTS '=' NAME
      {
          CopyTargetText(Singleton<CMentorSetting>::inst().m_sGraduateContents, SemanticText($3));
      }
    | MENTEE_JOIN_REWARD '=' '{' NAME ';' INTEGER '}'
      {
          const auto reportMissingItem = [this](const char16_t* name) {
              // const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              // L2_LOG_ERROR(Log,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              L2_LOG_ERROR(
                  Log, u"Settings line %lld: no positive item ID was resolved for '%ls'; the item reference is invalid.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
              );
          };
          const auto resolveItem = [&reportMissingItem](const char16_t* name) {
              const int itemId = g_ObjectDB.GetClassIdFromName(name, false);
              if (itemId <= 0) {
                  reportMissingItem(name);
              }
              return itemId;
          };
          const int itemId = resolveItem(SemanticText($4));
          if (itemId > 0) {
              CMentorSetting& mentor = Singleton<CMentorSetting>::inst();
              mentor.m_nJoinRewardItem = itemId;
              mentor.m_nJoinRewardItemCount = grammar::SemanticInt64($6);
          }
      }
    | MENTEE_JOIN_TITLE '=' NAME
      {
          CopyTargetText(Singleton<CMentorSetting>::inst().m_sJoinTitle, SemanticText($3));
      }
    | MENTEE_JOIN_CONTENTS '=' NAME
      {
          CopyTargetText(Singleton<CMentorSetting>::inst().m_sJoinContents, SemanticText($3));
      }
    ;

n117
    : n117 ';' n118
    | n118
    ;

n118
    : '{' INTEGER ';' NAME ';' INTEGER '}'
      {
          const auto reportMissingItem = [this](const char16_t* name) {
              // const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              // L2_LOG_ERROR(Log,
              //     L"%s[%d]: there is no item [%s]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              L2_LOG_ERROR(
                  Log, u"Settings line %lld: no positive item ID was resolved for '%ls'; the item reference is invalid.",
                  static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
              );
          };
          const auto resolveItem = [&reportMissingItem](const char16_t* name) {
              const int itemId = g_ObjectDB.GetClassIdFromName(name, false);
              if (itemId <= 0) {
                  reportMissingItem(name);
              }
              return itemId;
          };
          m_RewardData.nLev = grammar::SemanticInt32($2);
          m_RewardData.nItemId = resolveItem(SemanticText($4));
          m_RewardData.nItemCount = grammar::SemanticInt32($6);
          if (m_RewardData.nLev >= 2 && m_RewardData.nItemId > 0 && m_RewardData.nItemCount > 0) {
              tempRewardList.push_back(m_RewardData);
          }
          m_RewardData.Init();
      }
    ;

pledgegame_setting
    : PLEDGEGAME_SETTING_BEGIN n120 PLEDGEGAME_SETTING_END
    ;

n120
    : n121 n120
    | n121
    ;

n121
    : reservable_pledgegame_time_def
    | common_lose_reward_def
    | %empty
    ;

common_lose_reward_def
    : COMMON_LOSE_REWARD '=' '{' name_def '=' INTEGER '}'
      {
          if (!tempItemList.empty()) {
              g_PledgeGameSystem.SetLosePrizeItem(tempItemList.front(), grammar::SemanticInt32($6));
          }
          tempItemList.clear();
      }
    | COMMON_WIN_REWARD '=' '{' name_def ';' INTEGER ';' INTEGER '}'
      {
          if (!tempItemList.empty()) {
              g_PledgeGameSystem.SetWinPrizeNormalItem(
                  tempItemList.front(), grammar::SemanticInt32($6), grammar::SemanticInt32($8)
              );
          }
          tempItemList.clear();
      }
    | RAND_WIN_MIN_MAX_REWARD '=' '{' name_def ';' INTEGER ';' INTEGER '}'
      {
          if (!tempItemList.empty()) {
              g_PledgeGameSystem.SetWinPrizeRandItem(
                  tempItemList.front(), grammar::SemanticInt32($6), grammar::SemanticInt32($8)
              );
          }
          tempItemList.clear();
      }
    | %empty
    ;

reservable_pledgegame_time_def
    : RESERVABLE_PLEDGEGAME_TIME '=' '{' n124 '}'
    ;

n124
    : n125 ';' n124
    | n125
    ;

n125
    : '{' MON_GROUP ';' INTEGER '}'
      {
          CPledgeGameSystem::s_lstWday.push_back(grammar::SemanticInt32($2));
          CPledgeGameSystem::s_lstHour.push_back(grammar::SemanticInt32($4));
      }
    ;

training_zone_setting
    : TRAINING_ZONE_SETTING_BEGIN n127 TRAINING_ZONE_SETTING_END
    ;

n127
    : training_zone_complete_html_def n127
    | %empty
    ;

training_zone_complete_html_def
    : TRAINING_ZONE_COMPLETE_HTML '=' NAME
      { CTrainingZoneSetting::Inst().SetCompleteHTML(SemanticText($3)); }
    | TRAINING_ZONE_POSITION '=' INTEGER ';' INTEGER ';' INTEGER
      {
          CTrainingZoneSetting::Inst().SetTrainingZonePos(
              grammar::SemanticInt32($3), grammar::SemanticInt32($5), grammar::SemanticInt32($7)
          );
      }
    ;

kamaroka_setting
    : KAMAROKA_SETTING_START n130 KAMAROKA_SETTING_END
    ;

n130
    : kamaroka_days_def n130
    | %empty
    ;

kamaroka_days_def
    : KAMAROKA_DAYS '=' '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          int cycle[7]{grammar::SemanticInt32($4),  grammar::SemanticInt32($6),  grammar::SemanticInt32($8),
                       grammar::SemanticInt32($10), grammar::SemanticInt32($12), grammar::SemanticInt32($14),
                       grammar::SemanticInt32($16)};
          CPremiumInstantRankingSystem::Inst().SetKamarokaCycle(cycle);
      }
    ;

n132
    : item_skill_transfer_list_def
    ;

item_skill_transfer_list_def
    : ITEM_SKILL_TRANSFER_LIST '=' '{' n67 '}'
      {
          for (const std::int64_t skillKey : tempSkillList) {
              const std::int64_t skillId = static_cast<std::int32_t>(static_cast<std::uint64_t>(skillKey) >> 32);
              g_ItemSkillTransfer.emplace(skillId, true);
          }
          tempSkillList.clear();
      }
    | %empty
    ;

vip_attendance_setting
    : VIP_ATTENDANCE_SETTING_BEGIN n135 VIP_ATTENDANCE_SETTING_END
    ;

n135
    : vip_attendance_reset_time_def n135
    | %empty
    ;

vip_attendance_reset_time_def
    : VIP_ATTENDANCE_RESET_TIME '=' '{' INTEGER ';' INTEGER '}'
      {
          const int hour = grammar::SemanticInt32($4);
          const int minute = grammar::SemanticInt32($6);
          if (static_cast<std::uint32_t>(hour) > 23u) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d]: VipAttendacne Error Reset Hour [%d](0 ~23)",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hour)
              // L2_LOG_ERROR(Log,
              //     L"%s[%d]: VipAttendacne Error Reset Hour [%d](0 ~23)",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     hour)
              L2_LOG_ERROR(
                  Log,
                  u"Settings line %lld: VIP attendance reset hour %d is outside 0..23; the supplied reset time will still "
                  u"be stored.",
                  static_cast<long long>(m_lexer.line) + 1, hour
              );
          }
          if (static_cast<std::uint32_t>(minute) > 59u) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d]: VipAttendacne Error Reset Min [%d](0 ~59)",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     minute)
              // L2_LOG_ERROR(Log,
              //     L"%s[%d]: VipAttendacne Error Reset Min [%d](0 ~59)",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1,
              //     minute)
              L2_LOG_ERROR(
                  Log,
                  u"Settings line %lld: VIP attendance reset minute %d is outside 0..59; the supplied reset time will "
                  u"still be stored.",
                  static_cast<long long>(m_lexer.line) + 1, minute
              );
          }
          g_VipAttendanceDB.SetResetTime(hour, minute);
      }
    ;

clanbonus_setting
    : CLANBONUS_SETTING_BEGIN n138 CLANBONUS_SETTING_END
    ;

n138
    : access_goal_rate_def n138
    | %empty
    ;

access_goal_rate_def
    : ACCESS_GOAL_RATE '=' '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          const int values[6]{grammar::SemanticInt32($4),  grammar::SemanticInt32($6),
                              grammar::SemanticInt32($8),  grammar::SemanticInt32($10),
                              grammar::SemanticInt32($12), grammar::SemanticInt32($14)};
          const bool valid =
              values[0] > 0 && values[1] > 0 && values[2] > 0 && values[3] > 0 && values[4] != 0 && values[5] > 0;
          if (!valid) {
              // Log.Add(
              //     LOG_ERROR,
              //     action == 127
              //         ? L"%s[%d]: there is invalid value [ACCESS_GOAL_RATE]"
              //         : L"%s[%d]: there is invalid value [HUNT_GOAL_RATE]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     action == 127
              //         ? L"%s[%d]: there is invalid value [ACCESS_GOAL_RATE]"
              //         : L"%s[%d]: there is invalid value [HUNT_GOAL_RATE]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"Settings line %lld: %ls values [%d, %d, %d, %d, %d, %d] are invalid; entries 1..4 and 6 must be "
                  u"positive, entry 5 must be nonzero. This update is skipped.",
                  static_cast<long long>(m_lexer.line) + 1, u"ACCESS_GOAL_RATE", values[0], values[1], values[2], values[3],
                  values[4], values[5]
              );
              break;
          }
          CPledgeBonusSetting& setting = CPledgeBonusSetting::Inst();
          for (int index = 0; index < 6; ++index) {
              setting.SetGoalRate(0, index, values[index]);
          }
      }
    | HUNT_GOAL_RATE '=' '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          const int values[6]{grammar::SemanticInt32($4),  grammar::SemanticInt32($6),
                              grammar::SemanticInt32($8),  grammar::SemanticInt32($10),
                              grammar::SemanticInt32($12), grammar::SemanticInt32($14)};
          const bool valid =
              values[0] > 0 && values[1] > 0 && values[2] > 0 && values[3] > 0 && values[4] != 0 && values[5] > 0;
          if (!valid) {
              // Log.Add(
              //     LOG_ERROR,
              //     action == 127
              //         ? L"%s[%d]: there is invalid value [ACCESS_GOAL_RATE]"
              //         : L"%s[%d]: there is invalid value [HUNT_GOAL_RATE]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     action == 127
              //         ? L"%s[%d]: there is invalid value [ACCESS_GOAL_RATE]"
              //         : L"%s[%d]: there is invalid value [HUNT_GOAL_RATE]",
              //     L"..\\Script\\setting.txt",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"Settings line %lld: %ls values [%d, %d, %d, %d, %d, %d] are invalid; entries 1..4 and 6 must be "
                  u"positive, entry 5 must be nonzero. This update is skipped.",
                  static_cast<long long>(m_lexer.line) + 1, u"HUNT_GOAL_RATE", values[0], values[1], values[2], values[3],
                  values[4], values[5]
              );
              break;
          }
          CPledgeBonusSetting& setting = CPledgeBonusSetting::Inst();
          for (int index = 0; index < 6; ++index) {
              setting.SetGoalRate(1, index, values[index]);
          }
      }
    | ACCESS_REWARD '=' '{' n67 '}'
      {
          CPledgeBonusSetting& setting = CPledgeBonusSetting::Inst();
          int index = 0;
          for (const std::int64_t skillKey : tempSkillList) {
              setting.SetAccessRewardSkill(index++, skillKey);
          }
          tempSkillList.clear();
      }
    | HUNT_REWARD '=' '{' n10 '}'
      {
          CPledgeBonusSetting& setting = CPledgeBonusSetting::Inst();
          int index = 0;
          for (const int itemId : tempItemList) {
              setting.SetHuntRewardItem(index++, itemId);
          }
          tempItemList.clear();
      }
    | EXP_TO_HUNTPOINT '=' INTEGER
      {
          if (grammar::SemanticInt32($3) > 0) {
              CPledgeBonusSetting::Inst().SetExpToHuntpoint(grammar::SemanticInt32($3));
          }
          tempItemList.clear();
      }
    ;

pledge_raid
    : PLEDGE_RAID_BEGIN n141 PLEDGE_RAID_END
    ;

n141
    : n141 n142
    | n142
    ;

n142
    : pledge_raid_rank_reward
    | pledge_raid_tier_reward
    | pledge_raid_mastery_reward
    | PLEDGE_RAID_HUNT_NPC_NAME '=' IDENTIFIER
      { PledgeRaidDB::inst().strHuntNpcDbName_.assign(SemanticText($3)); }
    ;

pledge_raid_rank_reward
    : PLEDGE_RAID_RANK_REWARD_BEGIN rank_reward_def_list PLEDGE_RAID_RANK_REWARD_END
    ;

rank_reward_def_list
    : rank_reward_def_list rank_reward_def
    | rank_reward_def
    ;

rank_reward_def
    : RANK_REWARD '=' '{' INTEGER ';' INTEGER ';' INTEGER ';' n149 '}'
      {
          PledgeRaidRankRewardInfo reward;
          reward.nRank_ = grammar::SemanticInt32($4);
          reward.nNameValue_ = grammar::SemanticInt32($6);
          reward.nBonusPercentage_ = grammar::SemanticInt32($8);
          for (const RewardData& item : tempRewardList) {
              reward.mItemMap_.emplace(item.nItemId, item.nItemCount);
          }
          PledgeRaidDB::inst().mRankRewardInfo_[reward.nRank_] = reward;
          tempRewardList.clear();
      }
    ;

pledge_raid_tier_reward
    : PLEDGE_RAID_TIER_REWARD_BEGIN tier_reward_def_list PLEDGE_RAID_TIER_REWARD_END
    ;

tier_reward_def_list
    : tier_reward_def_list tier_reward_def
    | tier_reward_def
    ;

tier_reward_def
    : TIER_REWARD '=' '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' n149 '}'
      {
          PledgeRaidTierRewardInfo reward;
          reward.nTier_ = grammar::SemanticInt32($4);
          reward.nInPercentage_ = grammar::SemanticInt32($6);
          reward.nNameValue_ = grammar::SemanticInt32($8);
          reward.nBonusPercentage_ = grammar::SemanticInt32($10);
          for (const RewardData& item : tempRewardList) {
              reward.mItemMap_.emplace(item.nItemId, item.nItemCount);
          }
          PledgeRaidDB::inst().mTierRewardInfo_[reward.nTier_] = reward;
          tempRewardList.clear();
      }
    ;

n149
    : n150 ';' n149
    | n150
    | '{' '}'
    ;

n150
    : '{' INTEGER ';' INTEGER '}'
      {
          m_RewardData.nLev = 0;
          m_RewardData.nItemId = grammar::SemanticInt32($2);
          m_RewardData.nItemCount = grammar::SemanticInt32($4);
          if (m_RewardData.nItemId > 0 && m_RewardData.nItemCount > 0) {
              tempRewardList.push_back(m_RewardData);
          }
          m_RewardData.Init();
      }
    ;

pledge_raid_mastery_reward
    : PLEDGE_RAID_MASTERY_REWARD_BEGIN mastery_reward_def_list PLEDGE_RAID_MASTERY_REWARD_END
    ;

mastery_reward_def_list
    : mastery_reward_def_list mastery_reward_def
    | mastery_reward_def
    ;

mastery_reward_def
    : MASTERY_REWARD '=' '{' NAME ';' INTEGER '}'
      {
          const std::u16string masteryName(SemanticText($4));
          PledgeRaidMasteryRewardInfo reward{
              static_cast<std::int32_t>(PledgeStuff::ConvertToPledgeMasteryID(masteryName)), grammar::SemanticInt32($6)};
          PledgeRaidDB::inst().vMasteryRewardInfo_.push_back(reward);
      }
    ;

pledge_mission
    : PLEDGE_MISSION_BEGIN n155 PLEDGE_MISSION_END
    ;

n155
    : pledge_mission_common_max
    ;

pledge_mission_common_max
    : PLEDGE_MISSION_COMMON_MAX_BEGIN novice_def PLEDGE_MISSION_COMMON_MAX_END
      {
          PledgeMissionDB& missions = PledgeMissionDB::inst();
          if (missions.mPledgeMissionCountOfOneWeekInfo_.nNovice_ == 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"pledge_mission_common_max novice = 0 line = %d",
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"pledge_mission_common_max novice = 0 line = %d",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log, u"Settings line %lld: pledge_mission_common_max for novice is 0; the zero value is retained.",
                  static_cast<long long>(m_lexer.line) + 1
              );
          }
          if (missions.mPledgeMissionCountOfOneWeekInfo_.nNobless_ == 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"pledge_mission_common_max nobless = 0 line = %d",
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"pledge_mission_common_max nobless = 0 line = %d",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log, u"Settings line %lld: pledge_mission_common_max for nobless is 0; the zero value is retained.",
                  static_cast<long long>(m_lexer.line) + 1
              );
          }
          if (missions.mPledgeMissionCountOfOneWeekInfo_.nHonors_ == 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"pledge_mission_common_max honors = 0 line = %d",
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"pledge_mission_common_max honors = 0 line = %d",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log, u"Settings line %lld: pledge_mission_common_max for honors is 0; the zero value is retained.",
                  static_cast<long long>(m_lexer.line) + 1
              );
          }
      }
    ;

novice_def
    : NOVICE '=' INTEGER NOBLESS '=' INTEGER HONORS '=' INTEGER
      {
          PledgeMissionCountOfOneWeekInfo& counts = PledgeMissionDB::inst().mPledgeMissionCountOfOneWeekInfo_;
          counts.nNovice_ = grammar::SemanticInt32($3);
          counts.nNobless_ = grammar::SemanticInt32($6);
          counts.nHonors_ = grammar::SemanticInt32($9);
      }
    ;

// The original tables have rules 160, 161, 162, 163, 164, 165 and 166 that no
// parser state reduces.
// They are left out.

%%
