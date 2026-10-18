#pragma once

#include "Protocol/src/L2Constants.h"
#include "Shared/src/BaseStructure.h"

#include <cstdint>

enum OSC_PROTOCOL : std::int32_t {
    OSA_VERSION = 0,
    OSA_REGISTER = 1,
    OSA_START = 2,
    OSA_STOP = 3,
    OSQ_PREPARE = 4,
    OSN_CONNECT = 5,
    OSA_CONNECT = 6,
    OSN_CONFIRMED = 7,
    OSN_DISCONNECTED = 8,
    OSQ_RETURN = 9,
    OSQ_RESTART = 10,
    OSA_OBSERVE = 11,
    OSN_NOTIFY_SEASON = 12,
    OSN_ENTRY_COUNT = 13,
    OSN_PLAYER_COUNT = 14,
    OSA_MATCH_LIST = 15,
    OSA_HERO_LIST = 16,
    OSA_HERO_MATCH_RESULT = 17,
    OSN_SYSTEM_MESSAGE = 18,
    OSN_SYSTEM_MESSAGE_NO = 19,
    OSN_START_SEASON = 20,
    OSN_STOP_SEASON = 21,
    OSN_OLYMPIAD_POINT = 22,
    OSN_NOBLESS_INFO = 23,
    OSA_MATCH_COUNT = 24,
    OSA_DEBUG_STRING = 25,
    OSA_SET_HERO_TYPE = 26,
    OSA_BCMD = 27,
    OSN_STOP = 28,
    OSN_CANCEL_MATCH = 29,
    OSA_DELETE_TRADE_POINT = 30,
    OSN_LAST_MATCH_RESULT = 31,
    OSA_OLYMPIAD_RECORD = 32,
    OSA_PLEDGE_RAID_GET_SEASON = 33,
    OSA_PLEDGE_RAID_SET_INFO = 34,
    OSA_PLEDGE_RAID_GET_INFO = 35,
    OSA_PLEDGE_RAID_GET_INFO_FOR_BUILDER = 36,
    OSA_PLEDGE_RAID_GET_INFO_LIST = 37,
    OSA_PLEDGE_RAID_GET_INFO_LIST_FOR_BUILDER = 38,
    OSA_PLEDGE_RAID_GET_PREV_SEASON_INFO_LIST = 39,
    OSN_OLYMPIAD_MATCHING_INFO = 40,
    OSA_CLASS_RANK_BY_SERVER = 41,
    OSC_MAX = 42,
};


enum OCS_PROTOCOL : std::int32_t {
    OCQ_VERSION = 0,
    OCQ_REGISTER = 1,
    OCQ_START = 2,
    OCQ_STOP = 3,
    OCQ_CONNECT = 4,
    OCA_RETURN = 5,
    OCA_RESTART = 6,
    OCQ_OBSERVE = 7,
    OCQ_MATCH_LIST = 8,
    OCQ_HERO_LIST = 9,
    OCQ_HERO_MATCH_RESULT = 10,
    OCN_NOBLESS_INFO = 11,
    OCQ_MATCH_COUNT = 12,
    OCQ_DEBUG_STRING = 13,
    OCQ_SET_HERO_TYPE = 14,
    OCQ_BCMD = 15,
    OCA_PREPARE = 16,
    OCQ_DELETE_TRADE_POINT = 17,
    OCN_LAST_MATCH_RESULT = 18,
    OCN_UPDATE_PLEDGE_INFO = 19,
    OCQ_RESET_OLYMPIAD_POINT = 20,
    OCQ_OLYMPIAD_RECORD = 21,
    OCQ_PLEDGE_RAID_GET_SEASON = 22,
    OCN_PLEDGE_RAID_RESET_SEASON = 23,
    OCN_PLEDGE_RAID_DECIDE = 24,
    OCQ_PLEDGE_RAID_SET_INFO = 25,
    OCQ_PLEDGE_RAID_GET_INFO = 26,
    OCQ_PLEDGE_RAID_GET_INFO_FOR_BUILDER = 27,
    OCQ_PLEDGE_RAID_GET_INFO_LIST = 28,
    OCQ_PLEDGE_RAID_GET_INFO_LIST_FOR_BUILDER = 29,
    OCQ_PLEDGE_RAID_GET_PREV_SEASON_INFO_LIST = 30,
    OCQ_CLASS_RANK_BY_SERVER = 31,
    OCS_MAX = 32,
};


// enum GameRuleType : std::int32_t {
//     GRT_TEAM = 0,
//     GRT_CLASSLESS = 1,
//     GRT_CLASS = 2,
//     GRT_MAX = 3,
// }

enum OlympiadFieldStatus : std::int32_t {
    FIELD_NONE = 0,
    FIELD_WAIT = 1,
    FIELD_MATCH = 2,
};

// shared/olympiaddefine.h. The 36-entry HERO_CLASS_TABLE is present in the
// current L2Server image at 0x015c2ee0: the 34 awakened classes followed by
// the two Ertheia hero classes.
class OlympiadDefine {
public:
    static constexpr int HERO_CLASS_COUNT = 36;

    static ClassType GetHeroClassType(int index) {
        static constexpr ClassType HERO_CLASS_TABLE[HERO_CLASS_COUNT] = {
            CT_SIGEL_PHOENIX_KNIGHT,
            CT_SIGEL_HELL_KNIGHT,
            CT_SIGEL_EVAS_TEMPLAR,
            CT_SIGEL_SHILIEN_TEMPLAR,
            CT_TIR_DUELIST,
            CT_TIR_DREADNOUGHT,
            CT_TIR_TITAN,
            CT_TIR_GRAND_KHAVATARI,
            CT_TIR_MAESTRO,
            CT_TIR_DOOMBRINGER,
            CT_OTHEL_ADVENTURER,
            CT_OTHEL_WIND_RIDER,
            CT_OTHEL_GHOST_HUNTER,
            CT_OTHEL_FORTUNE_SEEKER,
            CT_YR_SAGITTARIUS,
            CT_YR_MOONLIGHT_SENTINEL,
            CT_YR_GHOST_SENTINEL,
            CT_YR_TRICKSTER,
            CT_FEOH_ARCHMAGE,
            CT_FEOH_SOULTAKER,
            CT_FEOH_MYSTIC_MUSE,
            CT_FEOH_STORM_SCREAMER,
            CT_FEOH_SOUL_HOUND,
            CT_IS_HIEROPHANT,
            CT_IS_SWORD_MUSE,
            CT_IS_SPECTRAL_DANCER,
            CT_IS_DOMINATOR,
            CT_IS_DOOMCRYER,
            CT_WYNN_ARCANA_LORD,
            CT_WYNN_ELEMENTAL_MASTER,
            CT_WYNN_SPECTRAL_MASTER,
            CT_EOLH_CARDINAL,
            CT_EOLH_EVAS_SAINT,
            CT_EOLH_SHILIEN_SAINT,
            CT_STRATO_LENKER,
            CT_SAYHAS_SEER,
        };
        if (index >= 0 && index < HERO_CLASS_COUNT) {
            return HERO_CLASS_TABLE[index];
        }
        return CT_NONE;
    }

    static int GetHeroClassIndex(ClassType classType) {
        for (int index = 0; index < HERO_CLASS_COUNT; ++index) {
            if (GetHeroClassType(index) == classType) {
                return index;
            }
        }
        return -1;
    }

    static int GetClassIndex(ClassType classType) { return GetHeroClassIndex(classType); }
};
