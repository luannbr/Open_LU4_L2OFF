#pragma once

#include "Protocol/VariableBitSet.h"
#include "Shared/src/proto/Stream.h"

#include <cstddef>
#include <cstdint>

enum S_PROTOCOL : std::int32_t {
    S_DIE = 0,
    S_REVIVE = 1,
    S_ATTACK_OUT_OF_RANGE = 2,
    S_ATTACK_IN_COOLTIME = 3,
    S_ATTACK_DEAD_TARGET = 4,
    S_SPAWN_ITEM = 5,
    S_SELL_LIST = 6,
    S_BUY_LIST = 7,
    S_DELETE_OBJECT = 8,
    S_CHARACTER_SELECTION_INFO = 9,
    S_LOGIN_RESULT = 10,
    S_CHARACTER_SELECTED = 11,
    S_NPC_INFO = 12,
    S_NEW_CHARACTER_SUCCESS = 13,
    S_NEW_CHARACTER_FAIL = 14,
    S_CHARACTER_CREATE_SUCCESS = 15,
    S_CHARACTER_CREATE_FAIL = 16,
    S_ITEMLIST = 17,
    S_SUNRISE = 18,
    S_SUNSET = 19,
    S_TRADE_START = 20,
    S_TRADE_START_OK = 21,
    S_DROP_ITEM = 22,
    S_GET_ITEM = 23,
    S_STATUS_UPDATE = 24,
    S_NPC_HTML_MESSAGE = 25,
    S_TRADE_OWN_ADD = 26,
    S_TRADE_OTHER_ADD = 27,
    S_TRADE_DONE = 28,
    S_CHARACTER_DELETE_SUCCESS = 29,
    S_CHARACTER_DELETE_FAIL = 30,
    S_ACTION_FAIL = 31,
    S_SERVER_CLOSE = 32,
    S_INVENTORY_UPDATE = 33,
    S_TELEPORT_TO_LOCATION = 34,
    S_TARGET_SELECTED = 35,
    S_TARGET_UNSELECTED = 36,
    S_COMBAT_MODE_START = 37,
    S_COMBAT_MODE_FINISH = 38,
    S_SOCIAL_ACTION = 39,
    S_CHANGE_MOVE_TYPE = 40,
    S_CHANGE_WAIT_TYPE = 41,
    S_MANAGE_PLEDGE_POWER = 42,
    S_CREATE_PLEDGE = 43,
    S_ASK_JOIN_PLEDGE = 44,
    S_JOIN_PLEDGE = 45,
    S_VERSION_CHECK = 46,
    S_MOVE_TO_LOCATION = 47,
    S_NPC_SAY = 48,
    S_CHAR_INFO = 49,
    S_USER_INFO = 50,
    S_ATTACK = 51,
    S_WITHDRAWAL_PLEDGE = 52,
    S_OUST_PLEDGE_MEMBER = 53,
    S_SET_OUST_PLEDGE_MEMBER = 54,
    S_DISMISS_PLEDGE = 55,
    S_SET_DISMISS_PLEDGE = 56,
    S_ASK_JOIN_PARTY = 57,
    S_JOIN_PARTY = 58,
    S_WITHDRAWAL_PARTY = 59,
    S_OUST_PARTY_MEMBER = 60,
    S_SET_OUST_PARTY_MEMBER = 61,
    S_DISMISS_PARTY = 62,
    S_SET_DISMISS_PARTY = 63,
    S_MAGIC_SKILL_LIST = 64,
    S_WAREHOUSE_DEPOSIT_LIST = 65,
    S_WAREHOUSE_WITHDRAW_LIST = 66,
    S_WAREHOUSE_DONE = 67,
    S_SHORTCUT_REG = 68,
    S_INIT_SHORTCUT = 69,
    S_SHORTCUT_DELETE = 70,
    S_STOP_MOVE = 71,
    S_MAGIC_SKILL_USE = 72,
    S_MAGIC_SKILL_CANCELED = 73,
    S_SAY2 = 74,
    S_NPC_INFO_ABNORMALVISUALEFFECT = 75,
    S_DOOR_INFO = 76,
    S_DOOR_STATUS_UPDATE = 77,
    S_PARTY_SMALL_WINDOW_ALL = 78,
    S_PARTY_SMALL_WINDOW_ADD = 79,
    S_PARTY_SMALL_WINDOW_DELETE_ALL = 80,
    S_PARTY_SMALL_WINDOW_DELETE = 81,
    S_PARTY_SMALL_WINDOW_UPDATE = 82,
    S_TRADE_PRESS_OWN_OK = 83,
    S_MAGIC_SKILL_LAUNCHED = 84,
    S_FRIEND_ADD_REQUEST_RESULT = 85,
    S_FRIEND_ADD = 86,
    S_FRIEND_REMOVE = 87,
    S_FRIEND_LIST = 88,
    S_FRIEND_STATUS = 89,
    S_PLEDGE_SHOW_MEMBER_LIST_ALL = 90,
    S_PLEDGE_SHOW_MEMBER_LIST_UPDATE = 91,
    S_PLEDGE_SHOW_MEMBER_LIST_ADD = 92,
    S_PLEDGE_SHOW_MEMBER_LIST_DELETE = 93,
    S_MAGIC_LIST = 94,
    S_SKILL_LIST = 95,
    S_VEHICLE_INFO = 96,
    S_FINISH_ROTATING = 97,
    S_SYSTEM_MSG = 98,
    S_START_PLEDGE_WAR = 99,
    S_REPLY_START_PLEDGE_WAR = 100,
    S_STOP_PLEDGE_WAR = 101,
    S_REPLY_STOP_PLEDGE_WAR = 102,
    S_SURRENDER_PLEDGE_WAR = 103,
    S_REPLY_SURRENDER_PLEDGE_WAR = 104,
    S_SET_PLEDGE_CREST = 105,
    S_PLEDGE_CREST = 106,
    S_SETUP_GAUGE = 107,
    S_VEHICLE_DEPARTURE = 108,
    S_VEHICLE_CHECK_LOCATION = 109,
    S_GETON_VEHICLE = 110,
    S_GETOFF_VEHICLE = 111,
    S_TRADE_REQUEST = 112,
    S_RESTART_RESPONSE = 113,
    S_MOVE_TO_PAWN = 114,
    S_SSQ_INFO = 115,
    S_GAMEGUARD_QUERY = 116,
    S_L2_FRIEND_LIST = 117,
    S_L2_FRIEND = 118,
    S_L2_FRIEND_STATUS = 119,
    S_L2_FRIEND_SAY = 120,
    S_VALIDATE_LOCATION = 121,
    S_START_ROTATING = 122,
    S_SHOW_BOARD = 123,
    S_CHOOSE_INVENTORY_ITEM = 124,
    S_TARGET_USER_FROM_MENU = 125,
    S_MOVE_TO_LOCATION_IN_VEHICLE = 126,
    S_STOP_MOVE_IN_VEHICLE = 127,
    S_VALIDATE_LOCATION_IN_VEHICLE = 128,
    S_TRADE_UPDATE = 129,
    S_TRADE_PRESS_OTHER_OK = 130,
    S_FRIEND_ADD_REQUEST = 131,
    S_LOGOUT_OK = 132,
    S_ABNORMAL_STATUS_UPDATE = 133,
    S_QUESTLIST = 134,
    S_ENCHANT_RESULT = 135,
    S_PLEDGE_SHOW_MEMBER_LIST_DELETE_ALL = 136,
    S_PLEDGE_INFO = 137,
    S_PLEDGE_EXTENDED_INFO = 138,
    S_SUMMON_INFO = 139,
    S_RIDE = 140,
    S_GIVE_NICKNAME = 141,
    S_PLEDGE_SHOW_INFO_UPDATE = 142,
    S_CLIENT_ACTION = 143,
    S_ACQUIRE_SKILL_LIST = 144,
    S_ACQUIRE_SKILL_INFO = 145,
    S_SERVER_OBJECT_INFO = 146,
    S_GM_HIDE = 147,
    S_ACQUIRE_SKILL_DONE = 148,
    S_GM_VIEW_CHARACTER_INFO = 149,
    S_GM_VIEW_PLEDGE_INFO = 150,
    S_GM_VIEW_SKILL_INFO = 151,
    S_GM_VIEW_MAGIC_INFO = 152,
    S_GM_VIEW_QUEST_INFO = 153,
    S_GM_VIEW_ITEMLIST = 154,
    S_GM_VIEW_WAREHOUSE_WITHDRAW_LIST = 155,
    S_LIST_PARTY_WAITING = 156,
    S_PARTY_ROOM_INFO = 157,
    S_PLAY_SOUND = 158,
    S_STATIC_OBJECT_INFO = 159,
    S_PRIVATE_STORE_MANAGE_LIST = 160,
    S_PRIVATE_STORE_LIST = 161,
    S_PRIVATE_STORE_MSG = 162,
    S_SHOW_MINIMAP = 163,
    S_REVIVE_REQ = 164,
    S_ABNORMAL_VISUAL_EFFECT = 165,
    S_TUTORIAL_SHOW_HTML = 166,
    S_SHOW_TUTORIAL_MARK = 167,
    S_TUTORIAL_ENABLE_CLIENT_EVENT = 168,
    S_TUTORIAL_CLOSE_HTML = 169,
    S_SHOW_RADAR = 170,
    S_WITHDRAW_ALLIANCE = 171,
    S_OUST_ALLIANCE_MEMBER_PLEDGE = 172,
    S_DISMISS_ALLIANCE = 173,
    S_SET_ALLIANCE_CREST = 174,
    S_ALLIANCE_CREST = 175,
    S_SERVER_CLOSE_SOCKET = 176,
    S_PET_STATUS_SHOW = 177,
    S_PET_INFO = 178,
    S_PET_ITEMLIST = 179,
    S_PET_INVENTORY_UPDATE = 180,
    S_ALLIANCE_INFO = 181,
    S_PET_STATUS_UPDATE = 182,
    S_PET_DELETE = 183,
    S_DELETE_RADAR = 184,
    S_MY_TARGET_SELECTED = 185,
    S_PARTY_MEMBER_POSITION = 186,
    S_ASK_JOIN_ALLIANCE = 187,
    S_JOIN_ALLIANCE = 188,
    S_PRIVATE_STORE_BUY_MANAGE_LIST = 189,
    S_PRIVATE_STORE_BUY_LIST = 190,
    S_PRIVATE_STORE_BUY_MSG = 191,
    S_VEHICLE_START_PACKET = 192,
    S_NPC_INFO_STATE = 193,
    S_START_ALLIANCE_WAR = 194,
    S_REPLY_START_ALLIANCE_WAR = 195,
    S_STOP_ALLIANCE_WAR = 196,
    S_REPLY_STOP_ALLIANCE_WAR = 197,
    S_SURRENDER_ALLIANCE_WAR = 198,
    S_SKILL_COOL_TIME = 199,
    S_PACKAGE_TO_LIST = 200,
    S_CASTLE_SIEGE_INFO = 201,
    S_CASTLE_SIEGE_ATTACKER_LIST = 202,
    S_CASTLE_SIEGE_DEFENDER_LIST = 203,
    S_NICKNAME_CHANGED = 204,
    S_PLEDGE_STATUS_CHANGED = 205,
    S_RELATION_CHANGED = 206,
    S_EVENT_TRIGGER = 207,
    S_MULTI_SELL_LIST = 208,
    S_SET_SUMMON_REMAIN_TIME = 209,
    S_PACKAGE_SENDABLE_LIST = 210,
    S_EARTHQUAKE = 211,
    S_FLY_TO_LOCATION = 212,
    S_BLOCK_PACKET_LIST = 213,
    S_SPECIAL_CAMERA = 214,
    S_NORMAL_CAMERA = 215,
    S_SKILL_REMAIN_SEC = 216,
    S_NET_PING = 217,
    S_DICE = 218,
    S_SNOOP = 219,
    S_RECIPE_BOOK_ITEM_LIST = 220,
    S_RECIPE_ITEM_MAKE_INFO = 221,
    S_RECIPE_SHOP_MANAGE_LIST = 222,
    S_RECIPE_SHOP_SELL_LIST = 223,
    S_RECIPE_SHOP_ITEM_INFO = 224,
    S_RECIPE_STORE_MSG = 225,
    S_SHOW_CALC = 226,
    S_MONRACE_INFO = 227,
    S_HENNA_ITEM_INFO = 228,
    S_HENNA_INFO = 229,
    S_HENNA_UNEQUIP_LIST = 230,
    S_HENNA_UNEQUIP_INFO = 231,
    S_MACRO_LIST = 232,
    S_BUY_LIST_SEED = 233,
    S_SHOW_TOWNMAP = 234,
    S_OBSERVER_START = 235,
    S_OBSERVER_END = 236,
    S_CHAIR_SIT = 237,
    S_HENNA_EQUIP_LIST = 238,
    S_SELL_LIST_PROCURE = 239,
    S_GM_HENNA_INFO = 240,
    S_RADAR_CONTROL = 241,
    S_CLIENT_SETTIME = 242,
    S_CONFIRM_DLG = 243,
    S_PARTY_SPELLED_INFO = 244,
    S_BUY_PREVIEW_LIST = 245,
    S_BUY_PREVIEW_INFO = 246,
    S_CAMERA_MODE = 247,
    S_SHOW_XMASSEAL = 248,
    S_ETC_STATUS_UPDATE = 249,
    S_SHORT_BUFF_STATUS_UPDATE = 250,
    S_SSQ_STATUS = 251,
    S_PETITION_VOTE = 252,
    S_AGIT_DECO_INFO = 253,
    S_EX = 254,
    S_MAX = 255,
};


enum S_EX_PROTOCOL : std::int32_t {
    S_EX_DUMMY = 0,
    S_EX_REGEN_MAX = 1,
    S_EX_EVENT_MATCH_USER_INFO = 2,
    S_EX_COLOSSEUM_FENCE_INFO = 3,
    S_EX_EVENT_MATCH_SPELLED_INFO = 4,
    S_EX_EVENT_MATCH_FIRECRACKER = 5,
    S_EX_EVENT_TEAM_UNLOCKED = 6,
    S_EX_EVENT_MATCH_GMTEST = 7,
    S_EX_PARTY_ROOM_MEMBER = 8,
    S_EX_DISMISS_PARTY_ROOM = 9,
    S_EX_MANAGE_PARTY_ROOM_MEMBER = 10,
    S_EX_EVENT_MATCH_LOCK_RESULT = 11,
    S_EX_AUTO_SOULSHOT = 12,
    S_EX_EVENT_MATCH_LIST = 13,
    S_EX_EVENT_MATCH_OBSERVER = 14,
    S_EX_EVENT_MATCH_MESSAGE = 15,
    S_EX_EVENT_MATCH_SCORE = 16,
    S_EX_SERVER_PRIMITIVE = 17,
    S_EX_OPEN_MPCC = 18,
    S_EX_CLOSE_MPCC = 19,
    S_EX_SHOW_CASTLE_INFO = 20,
    S_EX_SHOW_FORTRESS_INFO = 21,
    S_EX_SHOW_AGIT_INFO = 22,
    S_EX_SHOW_FORTRESS_SIEGE_INFO = 23,
    S_EX_PARTY_PET_WINDOW_ADD = 24,
    S_EX_PARTY_PET_WINDOW_UPDATE = 25,
    S_EX_ASK_JOIN_MPCC = 26,
    S_EX_PLEDGE_EMBLEM = 27,
    S_EX_EVENT_MATCH_TEAM_INFO = 28,
    S_EX_EVENT_MATCH_CREATE = 29,
    S_EX_FISHING_START = 30,
    S_EX_FISHING_END = 31,
    S_EX_SHOW_QUEST_INFO = 32,
    S_EX_SHOW_QUEST_MARK = 33,
    S_EX_SEND_MANOR_LIST = 34,
    S_EX_SHOW_SEED_INFO = 35,
    S_EX_SHOW_CROP_INFO = 36,
    S_EX_SHOW_MANOR_DEFAULT_INFO = 37,
    S_EX_SHOW_SEED_SETTING = 38,
    S_EX_FISHING_START_COMBAT = 39,
    S_EX_FISHING_HP_REGEN = 40,
    S_EX_ENCHANT_SKILL_LIST = 41,
    S_EX_ENCHANT_SKILL_INFO = 42,
    S_EX_SHOW_CROP_SETTING = 43,
    S_EX_SHOW_SELL_CROP_LIST = 44,
    S_EX_OLYMPIAD_MATCH_END = 45,
    S_EX_MAIL_ARRIVED = 46,
    S_EX_STORAGE_MAX_COUNT = 47,
    S_EX_EVENT_MATCH_MANAGE = 48,
    S_EX_MULTI_PARTY_COMMAND_CHANNEL_INFO = 49,
    S_EX_PCCAFE_POINT_INFO = 50,
    S_EX_SET_COMPASS_ZONE_CODE = 51,
    S_EX_GET_BOSS_RECORD = 52,
    S_EX_ASK_JOIN_PARTY_ROOM = 53,
    S_EX_LIST_PARTY_MATCHING_WAITING_ROOM = 54,
    S_EX_SET_MPCC_ROUTING = 55,
    S_EX_SHOW_ADVENTURER_GUIDE_BOOK = 56,
    S_EX_SHOW_SCREEN_MESSAGE = 57,
    S_EX_PLEDGE_SKILL_LIST = 58,
    S_EX_PLEDGE_SKILL_ADD = 59,
    S_EX_PLEDGE_SKILL_DELETE = 60,
    S_EX_PLEDGE_POWER_GRADE_LIST = 61,
    S_EX_VIEW_PLEDGE_POWER = 62,
    S_EX_VIEW_PLEDGE_MEMBER_INFO = 63,
    S_EX_VIEW_PLEDGE_WARLIST = 64,
    S_EX_SUBPLEDGE_UPDATED = 65,
    S_EX_REDSKY = 66,
    S_EX_UPDATE_PLEDGE_POWER = 67,
    S_EX_FLY_SELF_DESTINATION = 68,
    S_EX_PCCAFE_COUPON_SHOW_UI = 69,
    S_EX_ORC_MOVE = 70,
    S_EX_CURSED_WEAPON_LIST = 71,
    S_EX_EXISTING_CURSED_WEAPON_LOCATION = 72,
    S_EX_RESTART_CLIENT = 73,
    S_EX_REQUEST_HACK_SHIELD = 74,
    S_EX_USE_SHARED_GROUP_ITEM = 75,
    S_EX_MPCC_SHOW_PARTY_MEMBERS_INFO = 76,
    S_EX_DUEL_ASK_START = 77,
    S_EX_DUEL_READY = 78,
    S_EX_DUEL_START = 79,
    S_EX_DUEL_END = 80,
    S_EX_DUEL_UPDATE_USER_INFO = 81,
    S_EX_SHOW_VARIATION_MAKE_WINDOW = 82,
    S_EX_SHOW_VARIATION_CANCEL_WINDOW = 83,
    S_EX_PUT_ITEM_RESULT_FOR_VARIATION_MAKE = 84,
    S_EX_PUT_INTENSIVE_RESULT_FOR_VARIATION_MAKE = 85,
    S_EX_PUT_COMMISSION_RESULT_FOR_VARIATION_MAKE = 86,
    S_EX_VARIATION_RESULT = 87,
    S_EX_PUT_ITEM_RESULT_FOR_VARIATION_CANCEL = 88,
    S_EX_VARIATION_CANCEL_RESULT = 89,
    S_EX_DUEL_ENEMY_RELATION = 90,
    S_EX_PLAY_ANIMATION = 91,
    S_EX_MPCC_PARTY_INFO_UPDATE = 92,
    S_EX_PLAY_SCENE = 93,
    S_EX_SPAWN_EMITTER = 94,
    S_EX_ENCHANT_SKILL_INFO_DETAIL = 95,
    S_EX_BASIC_ACTION_LIST = 96,
    S_EX_AIRSHIP_INFO = 97,
    S_EX_ATTRIBUTE_ENCHANT_RESULT = 98,
    S_EX_CHOOSE_INVENTORY_ATTRIBUTE_ITEM = 99,
    S_EX_GETON_AIRSHIP = 100,
    S_EX_GETOFF_AIRSHIP = 101,
    S_EX_MOVE_TO_LOCATION_AIRSHIP = 102,
    S_EX_STOP_MOVE_AIRSHIP = 103,
    S_EX_SHOW_TRACE = 104,
    S_EX_ITEM_AUCTION_INFO = 105,
    S_EX_NEED_TO_CHANGE_NAME = 106,
    S_EX_PARTY_PET_WINDOW_DELETE = 107,
    S_EX_TUTORIAL_LIST = 108,
    S_EX_RP_ITEMLINK = 109,
    S_EX_MOVE_TO_LOCATION_IN_AIRSHIP = 110,
    S_EX_STOP_MOVE_IN_AIRSHIP = 111,
    S_EX_VALIDATE_LOCATION_IN_AIRSHIP = 112,
    S_EX_UI_SETTING = 113,
    S_EX_MOVE_TO_TARGET_IN_AIRSHIP = 114,
    S_EX_ATTACK_IN_AIRSHIP = 115,
    S_EX_MAGIC_SKILL_USE_IN_AIRSHIP = 116,
    S_EX_SHOW_BASE_ATTRIBUTE_CANCEL_WINDOW = 117,
    S_EX_BASE_ATTRIBUTE_CANCEL_RESULT = 118,
    S_EX_SUBPLEDGE_SKILL_ADD = 119,
    S_EX_RESPONSE_ANTI_FREE_SERVER = 120,
    S_EX_SHOW_PROCURE_CROP_DETAIL = 121,
    S_EX_HERO_LIST = 122,
    S_EX_OLYMPIAD_USER_INFO = 123,
    S_EX_OLYMPIAD_SPELLED_INFO = 124,
    S_EX_OLYMPIAD_MODE = 125,
    S_EX_SHOW_FORTRESS_MAP_INFO = 126,
    S_EX_PVPMATCH_RECORD = 127,
    S_EX_PVPMATCH_USER_DIE = 128,
    S_EX_PRIVATE_STORE_WHOLE_MSG = 129,
    S_EX_PUT_ENCHANT_TARGET_ITEM_RESULT = 130,
    S_EX_PUT_ENCHANT_SUPPORT_ITEM_RESULT = 131,
    S_EX_CHANGE_NICKNAME_COLOR = 132,
    S_EX_USER_BOOKMARK = 133,
    S_EX_NOTIFY_PREMIUM_ITEM = 134,
    S_EX_PREMIUM_ITEM_LIST = 135,
    S_EX_PERIODIC_ITEM_LIST = 136,
    S_EX_JUMP_TO_LOCATION = 137,
    S_EX_PVPMATCH_CC_RECORD = 138,
    S_EX_PVPMATCH_CC_MY_RECORD = 139,
    S_EX_PVPMATCH_CC_RETIRE = 140,
    S_EX_SHOW_TERRITORY = 141,
    S_EX_NPC_QUEST_HTML_MESSAGE = 142,
    S_EX_SEND_UI_EVENT = 143,
    S_EX_NOTIFY_BIRTHDAY = 144,
    S_EX_SHOW_DOMINION_REGISTRY = 145,
    S_EX_REPLY_REGISTER_DOMINION = 146,
    S_EX_REPLY_DOMINION_INFO = 147,
    S_EX_SHOW_OWNTHING_POS = 148,
    S_EX_CLEFT_LIST = 149,
    S_EX_CLEFT_STATE = 150,
    S_EX_DOMINION_CHANNEL_SET = 151,
    S_EX_BLOCK_UPSET_LIST = 152,
    S_EX_BLOCK_UPSET_STATE = 153,
    S_EX_START_SCENE_PLAYER = 154,
    S_EX_AIRSHIP_TELEPORT_LIST = 155,
    S_EX_MPCC_ROOM_INFO = 156,
    S_EX_LIST_MPCC_WAITING = 157,
    S_EX_DISMISS_MPCC_ROOM = 158,
    S_EX_MANAGE_MPCC_ROOM_MEMBER = 159,
    S_EX_MPCC_ROOM_MEMBER = 160,
    S_EX_VITALITY_POINT_INFO = 161,
    S_EX_SHOW_SEED_MAP_INFO = 162,
    S_EX_MPCC_PARTYMASTER_LIST = 163,
    S_EX_DOMINION_WAR_START = 164,
    S_EX_DOMINION_WAR_END = 165,
    S_EX_SHOW_LINES = 166,
    S_EX_PARTY_MEMBER_RENAMED = 167,
    S_EX_ENCHANT_SKILL_RESULT = 168,
    S_EX_REFUND_LIST = 169,
    S_EX_NOTICE_POST_ARRIVED = 170,
    S_EX_SHOW_RECEIVED_POST_LIST = 171,
    S_EX_REPLY_RECEIVED_POST = 172,
    S_EX_SHOW_SENT_POST_LIST = 173,
    S_EX_REPLY_SENT_POST = 174,
    S_EX_RESPONSE_SHOWSTEP_ONE = 175,
    S_EX_RESPONSE_SHOWSTEP_TWO = 176,
    S_EX_RESPONSE_SHOWCONTENTS = 177,
    S_EX_RESPONSE_SHOWSTEP_THREE = 178,
    S_EX_REPLY_POST_ITEM_LIST = 179,
    S_EX_CHANGE_POST_STATE = 180,
    S_EX_REPLY_WRITE_POST = 181,
    S_EX_INITIALIZE_SEED = 182,
    S_EX_RAID_RESERVE_RESULT = 183,
    S_EX_BUY_SELL_LIST = 184,
    S_EX_CLOSE_RAID_SOCKET = 185,
    S_EX_PRIVATE_MARKET_LIST = 186,
    S_EX_RAID_CHARACTER_SELECTED = 187,
    S_EX_ASK_COUPLE_ACTION = 188,
    S_EX_BR_BROADCAST_EVENT_STATE = 189,
    S_EX_BR_LOAD_EVENT_TOP_RANKERS_ACK = 190,
    S_EX_CHANGE_NPC_STATE = 191,
    S_EX_ASK_MODIFY_PARTY_LOOTING = 192,
    S_EX_SET_PARTY_LOOTING = 193,
    S_EX_ROTATION = 194,
    S_EX_CLIENT_EFFECT_INFO = 195,
    S_EX_ANSWER_MY_MEMBERSHIP = 196,
    S_EX_REPLY_HAND_OVER = 197,
    S_EX_QUEST_NPC_LOG_LIST = 198,
    S_EX_QUEST_ITEMLIST = 199,
    S_EX_GM_VIEW_QUEST_ITEMLIST = 200,
    S_EX_RESTART_RESPONSE = 201,
    S_EX_VOTE_SYSTEM_INFO = 202,
    S_EX_SHUTTLE_INFO = 203,
    S_EX_GETON_SHUTTLE = 204,
    S_EX_GETOFF_SHUTTLE = 205,
    S_EX_SHUTTLE_MOVE = 206,
    S_EX_MOVE_TO_LOCATION_IN_SHUTTLE = 207,
    S_EX_STOP_MOVE_LOCATION_IN_SHUTTLE = 208,
    S_EX_VALIDATE_LOCATION_IN_SHUTTLE = 209,
    S_EX_AGITAUCTION_CMD = 210,
    S_EX_ADD_POST_FRIEND = 211,
    S_EX_SHOW_POST_FRIEND = 212,
    S_EX_GFX_OLYMPIAD = 213,
    S_EX_BR_NOTIFY_GAME_POINT = 214,
    S_EX_BR_PRODUCT_LIST_ACK = 215,
    S_EX_BR_PRODUCT_INFO_ACK = 216,
    S_EX_BR_BUY_PRODUCT_ACK = 217,
    S_EX_BR_NOTIFY_PREMIUM_STATE = 218,
    S_EX_BR_EXTRA_USER_INFO = 219,
    S_EX_BR_BUFF_EVENT_STATE = 220,
    S_EX_BR_RECENT_PRODUCT_ACK = 221,
    S_EX_BR_MINIGAME_LOAD_SCORES_ACK = 222,
    S_EX_BR_AGATHION_ENERGY_INFO = 223,
    S_EX_SHOW_CHANNELING_EFFECT = 224,
    S_EX_RESPONSE_CRYSTALITEM_INFO = 225,
    S_EX_CRYSTALITEM_FAIL = 226,
    S_EX_NAVIT_ADVENT_POINT_INFO = 227,
    S_EX_NAVIT_ADVENT_EFFECT = 228,
    S_EX_NAVIT_ADVENT_TIME_CHANGE = 229,
    S_EX_ABNORMAL_STATUS_UPDATE_FROM_TARGET = 230,
    S_EX_STOP_SCENE_PLAYER = 231,
    S_EX_FLY_MOVE = 232,
    S_EX_DYNAMIC_QUEST = 233,
    S_EX_SUBJOB_INFO = 234,
    S_EX_CHANGE_MP_COST = 235,
    S_EX_FRIEND_DETAIL_INFO = 236,
    S_EX_BLOCK_ADD_RESULT = 237,
    S_EX_BLOCK_REMOVE_RESULT = 238,
    S_EX_BLOCK_DETAIL_INFO = 239,
    S_EX_LOAD_INZONE_PARTY_HISTORY = 240,
    S_EX_FRIEND_NOTIFY_NAME_CHANGED = 241,
    S_EX_SHOW_COMMISSION = 242,
    S_EX_RESPONSE_COMMISSION_ITEM_LIST = 243,
    S_EX_RESPONSE_COMMISSION_INFO = 244,
    S_EX_RESPONSE_COMMISSION_REGISTER = 245,
    S_EX_RESPONSE_COMMISSION_DELETE = 246,
    S_EX_RESPONSE_COMMISSION_LIST = 247,
    S_EX_RESPONSE_COMMISSION_BUY_INFO = 248,
    S_EX_RESPONSE_COMMISSION_BUY_ITEM = 249,
    S_EX_ACQUIRABLE_SKILL_LIST_BY_CLASS = 250,
    S_EX_MAGIC_ATTACK_INFO = 251,
    S_EX_ACQUIRE_SKILL_INFO = 252,
    S_EX_NEW_SKILL_TO_LEARN_BY_LEVEL_UP = 253,
    S_EX_CALL_TO_CHANGE_CLASS = 254,
    S_EX_CHANGE_TO_AWAKENED_CLASS = 255,
    S_EX_TACTICAL_SIGN = 256,
    S_EX_NOT_USED_257 = 257,
    S_EX_NOT_USED_258 = 258,
    S_EX_NOT_USED_259 = 259,
    S_EX_REPLY_WEB_SESSION_ID = 260,
    S_EX_2ND_PASSWORD_CHECK = 261,
    S_EX_2ND_PASSWORD_VERIFY = 262,
    S_EX_2ND_PASSWORD_ACK = 263,
    S_EX_FLY_MOVE_BROADCAST = 264,
    S_EX_SEND_USM_EVENT = 265,
    S_EX_SHOW_STAT_PAGE = 266,
    S_EX_CHECK_CHAR_NAME = 267,
    S_EX_GOODS_INVENTORY_CHANGED_NOTI = 268,
    S_EX_GOODS_INVENTORY_INFO = 269,
    S_EX_GOODS_INVENTORY_RESULT = 270,
    S_EX_ALTER_SKILL_REQUEST = 271,
    S_EX_NOTIFY_FLY_MOVE_START = 272,
    S_EX_SAY2_FAIL = 273,
    S_EX_CLOSE_COMMISSION = 274,
    S_EX_CHANGE_ATTRIBUTE_ITEM_LIST = 275,
    S_EX_CHANGE_ATTRIBUTE_INFO = 276,
    S_EX_CHANGE_ATTRIBUTE_OK = 277,
    S_EX_CHANGE_ATTRIBUTE_FAIL = 278,
    S_EX_LIGHTINGCANDLE = 279,
    S_EX_VITALITY_EFFECT_INFO = 280,
    S_EX_LOGIN_VITALITY_EFFECT_INFO = 281,
    S_EX_BR_BUY_PRODUCT_GIFT_ACK = 282,
    S_EX_MENTOR_LIST = 283,
    S_EX_MENTOR_ADD = 284,
    S_EX_MENTEE_WAITING_LIST = 285,
    S_EX_INZONE_WAITING_INFO = 286,
    S_EX_CURIOUS_HOUSE_STATE = 287,
    S_EX_CURIOUS_HOUSE_ENTER = 288,
    S_EX_CURIOUS_HOUSE_LEAVE = 289,
    S_EX_CURIOUS_HOUSE_MEMBER_LIST = 290,
    S_EX_CURIOUS_HOUSE_MEMBER_UPDATE = 291,
    S_EX_CURIOUS_HOUSE_REMAIN_TIME = 292,
    S_EX_CURIOUS_HOUSE_RESULT = 293,
    S_EX_CURIOUS_HOUSE_OBSERVE_LIST = 294,
    S_EX_CURIOUS_HOUSE_OBSERVE_MODE = 295,
    S_EX_SYS_STRING = 296,
    S_EX_CHOOSE_SHAPE_SHIFTING_ITEM = 297,
    S_EX_PUT_SHAPE_SHIFTING_TARGET_ITEM_RESULT = 298,
    S_EX_PUT_SHAPE_SHIFTING_EXTRACTION_ITEM_RESULT = 299,
    S_EX_SHAPE_SHIFTING_RESULT = 300,
    S_EX_CASTLE_STATE = 301,
    S_EX_NCGUARD = 302,
    S_EX_KALIE_EVENT = 303,
    S_EX_KALIE_JACKPOT_USER = 304,
    S_EX_ABNORMAL_VISUAL_EFFECT_INFO = 305,
    S_EX_NPC_INFO_SPEED = 306,
    S_EX_SET_PLEDGE_EMBLEM_ACK = 307,
    S_EX_SHOW_BEAUTY_MENU = 308,
    S_EX_RESPONSE_BEAUTY_LIST = 309,
    S_EX_RESPONSE_BEAUTY_REGIST_RESET = 310,
    S_EX_RESPONSE_RESET_LIST = 311,
    S_EX_SHUFFLE_SEED_AND_PUBLIC_KEY = 312,
    S_EX_CHECK_SPEEDHACK = 313,
    S_EX_BR_EXIST_NEW_PRODUCT_ACK = 314,
    S_EX_EVENT_CAMPAIGN_INFO = 315,
    S_EX_UNREADMAIL_COUNT = 316,
    S_EX_PLEDGE_COUNT = 317,
    S_EX_ADENA_INVEN_COUNT = 318,
    S_EX_PLEDGE_RECRUIT_INFO = 319,
    S_EX_PLEDGE_RECRUIT_APPLY_INFO = 320,
    S_EX_PLEDGE_RECRUIT_BOARD_SEARCH = 321,
    S_EX_PLEDGE_RECRUIT_BOARD_DETAIL = 322,
    S_EX_PLEDGE_WAITING_LIST_APPLIED = 323,
    S_EX_PLEDGE_WAITING_LIST = 324,
    S_EX_PLEDGE_WAITING_USER = 325,
    S_EX_PLEDGE_DRAFT_LIST_SEARCH = 326,
    S_EX_PLEDGE_WAITING_LIST_ALARM = 327,
    S_EX_VALIDATE_ACTIVE_CHARACTER = 328,
    S_EX_CLOSE_COMMISSION_REGISTER = 329,
    S_EX_TELEPORT_TO_LOCATION_ACTIVATE = 330,
    S_EX_NOTIFY_WEB_PETITION_REPLY_ALARM = 331,
    S_EX_EVENT_SHOW_XMAS_WISHCARD = 332,
    S_EX_INVITATION_EVENT_UI_SETTING = 333,
    S_EX_INVITATION_EVENT_INK_ENERGY = 334,
    S_EX_CHECK_ABUSING = 335,
    S_EX_GM_VIEW_VITALITY_EFFECT_INFO = 336,
    S_EX_PATH_TO_AWAKENING_ALARM = 337,
    S_EX_PUT_ENCHANT_SCROLL_ITEM_RESULT = 338,
    S_EX_REMOVE_ENCHANT_SUPPORT_ITEM_RESULT = 339,
    S_EX_SHOW_CARD_REWARD_LIST = 340,
    S_EX_GM_VIEW_CHARACTER_INFO = 341,
    S_EX_USER_INFO_EQUIPSLOT = 342,
    S_EX_USER_INFO_CUBIC = 343,
    S_EX_USER_INFO_ABNORMALVISUALEFFECT = 344,
    S_EX_USER_INFO_FISHING = 345,
    S_EX_PARTY_SPELLED_INFO_UPDATE = 346,
    S_EX_DIVIDE_ADENA_START = 347,
    S_EX_DIVIDE_ADENA_CANCEL = 348,
    S_EX_DIVIDE_ADENA_DONE = 349,
    S_EX_PET_INFO = 350,
    S_EX_ACQUIRE_AP_SKILL_LIST = 351,
    S_EX_START_LUCKY_GAME = 352,
    S_EX_BETTING_LUCKY_GAME_RESULT = 353,
    S_EX_TRAININGZONE_ADMISSION = 354,
    S_EX_TRAININGZONE_LEAVING = 355,
    S_EX_PERIODIC_HENNA = 356,
    S_EX_SHOW_AP_LIST_WND = 357,
    S_EX_USER_INFO_INVENWEIGHT = 358,
    S_EX_CLOSE_AP_LIST_WND = 359,
    S_EX_ENCHANT_ONE_OK = 360,
    S_EX_ENCHANT_ONE_FAIL = 361,
    S_EX_ENCHANT_ONE_REMOVE_OK = 362,
    S_EX_ENCHANT_ONE_REMOVE_FAIL = 363,
    S_EX_ENCHANT_TWO_OK = 364,
    S_EX_ENCHANT_TWO_FAIL = 365,
    S_EX_ENCHANT_TWO_REMOVE_OK = 366,
    S_EX_ENCHANT_TWO_REMOVE_FAIL = 367,
    S_EX_ENCHANT_SUCCESS = 368,
    S_EX_ENCHANT_FAIL = 369,
    S_EX_ENCHANT_RETRY_TO_PUT_ITEMS_OK = 370,
    S_EX_ENCHANT_RETRY_TO_PUT_ITEMS_FAIL = 371,
    S_EX_ACCOUNT_ATTENDANCE_INFO = 372,
    S_EX_WORLDCHAT_CNT = 373,
    S_EX_ALCHEMY_SKILL_LIST = 374,
    S_EX_TRY_MIX_CUBE = 375,
    S_EX_REQUEST_ALCHEMY_CONVERSION = 376,
    S_EX_BEAUTY_ITEM_LIST = 377,
    S_EX_CLIENT_INI = 378,
    S_EX_AUTOFISH_AVAILABLE = 379,
    S_EX_CC_ENTER_WORLD = 380,
    S_EX_CC_PLEDGE_INFO = 381,
    S_EX_VIP_ATTENDANCE_ITEMLIST = 382,
    S_EX_CONFIRM_VIP_ATTENDANCE_CHECK = 383,
    S_EX_SHOW_ENSOUL_WINDOW = 384,
    S_EX_ENSOUL_RESULT = 385,
    S_EX_MULTISELL_RESULT = 386,
    S_EX_CASTLEWAR_SEASON_RESULT = 387,
    S_EX_CASTLEWAR_SEASON_REWARD = 388,
    S_EX_BR_VIP_PRODUCT_LIST_ACK = 389,
    S_EX_LUCKY_GAME_INFO = 390,
    S_EX_LUCKY_GAME_ITEMLIST = 391,
    S_EX_LUCKY_GAME_BONUS_INFO = 392,
    S_EX_VIP_INFO = 393,
    S_EX_VIP_REMAIN_TIME = 394,
    S_EX_CAPTCHA_IMAGE = 395,
    S_EX_CAPTCHA_ANSWER_RESULT = 396,
    S_EX_PLEDGE_SIGNIN = 397,
    S_EX_REQUEST_MATCH_ARENA = 398,
    S_EX_COMPLETE_MATCH_ARENA = 399,
    S_EX_CONFIRM_MATCH_ARENA = 400,
    S_EX_CANCEL_MATCH_ARENA = 401,
    S_EX_START_CHOOSE_CLASS_ARENA = 402,
    S_EX_CHANGE_CLASS_ARENA = 403,
    S_EX_CONFIRM_CLASS_ARENA = 404,
    S_EX_START_BATTLE_READY_ARENA = 405,
    S_EX_BATTLE_READY_ARENA = 406,
    S_EX_DECO_NPC_INFO = 407,
    S_EX_DECO_NPC_SET = 408,
    S_EX_FACTION_INFO = 409,
    S_EX_BATTLE_RESULT_ARENA = 410,
    S_EX_CLOSING_ARENA = 411,
    S_EX_CLOSED_ARENA = 412,
    S_EX_DIE_IN_ARENA_TMP = 413,
    S_EX_REVIVE_PENALTY_END = 414,
    S_EX_ARENA_DASHBOARD = 415,
    S_EX_ARENA_UPDATE_EQUIP_SLOT = 416,
    S_EX_ARENA_KILL_INFO = 417,
    S_EX_EXIT_ARENA = 418,
    S_EX_BALTHUS_EVENT = 419,
    S_EX_BALTHUS_JACKPOT_USER = 420,
    S_EX_PARTY_MATCHING_ROOM_HISTORY = 421,
    S_EX_CONTENT_UI_EVENT = 422,
    S_EX_ARENA_CUSTOM_NOTIFICATION = 423,
    S_EX_ONE_DAY_REWARD_LIST = 424,
    S_EX_ONE_DAY_REWARD_INFO = 425,
    S_EX_TODOLIST_RECOMMAND = 426,
    S_EX_TODOLIST_INZONE = 427,
    S_EX_TODOLIST_HTML = 428,
    S_EX_QUEUETICKET = 429,
    S_EX_PLEDGE_BONUS_UI_OPEN = 430,
    S_EX_PLEDGE_BONUS_LIST = 431,
    S_EX_PLEDGE_ACTIVITY_MARK_RESET = 432,
    S_EX_PLEDGE_BONUS_UPDATE = 433,
    S_EX_SSO_AUTHNTOKEN = 434,
    S_EX_QUEUETICKET_LOGIN = 435,
    S_EX_SHOW_ENSOUL_EXTRACTION_WINDOW = 436,
    S_EX_ENSOUL_EXTRACTION_RESULT = 437,
    S_EX_FIELD_EVENT_STEP = 438,
    S_EX_FIELD_EVENT_POINT = 439,
    S_EX_FIELD_EVENT_EFFECT = 440,
    S_EX_RAID_BOSS_SPAWN_INFO = 441,
    S_EX_RAID_SERVER_INFO = 442,
    S_EX_SHOW_AGIT_SIEGE_INFO = 443,
    S_EX_ITEM_AUCTION_STATUS = 444,
    S_EX_MONSTER_BOOK = 445,
    S_EX_MONSTER_BOOK_REWARD_ICON = 446,
    S_EX_MONSTER_BOOK_ON_FACTION_UI = 447,
    S_EX_MONSTER_BOOK_OPEN_RESULT = 448,
    S_EX_MONSTER_BOOK_CLOSE_FORCE = 449,
    S_EX_FACTION_LEVEL_UP_NOTIFY = 450,
    S_EX_ITEM_AUCTION_NEXT_INFO = 451,
    S_EX_ITEM_AUCTION_UPDATED_BIDDING_INFO = 452,
    S_EX_PRIVATE_STORE_BUYING_RESULT = 453,
    S_EX_PRIVATE_STORE_SELLING_RESULT = 454,
    S_EX_ENTER_WORLD = 455,
    S_EX_MATCHGROUP = 456,
    S_EX_MATCHGROUP_ASK = 457,
    S_EX_MATCHGROUP_WITHDRAW = 458,
    S_EX_MATCHGROUP_OUST = 459,
    S_EX_ARENA_SHOW_ENEMY_PARTY_LOCATION = 460,
    S_EX_SHOW_UPGRADE_SYSTEM = 461,
    S_EX_UPGRADE_SYSTEM_RESULT = 462,
    S_EX_CARD_UPDOWN_GAME_START = 463,
    S_EX_CARD_UPDOWN_PICK_RESULT = 464,
    S_EX_CARD_UPDOWN_GAME_PREPARE_REWARD = 465,
    S_EX_CARD_UPDOWN_GAME_REWARD_REPLY = 466,
    S_EX_CARD_UPDOWN_GAME_QUIT = 467,
    S_EX_ARENA_RANK_ALL = 468,
    S_EX_ARENA_MYRANK = 469,
    S_EX_PLEDGE_CLASSIC_RAID_INFO = 470,
    S_EX_ARENA_OBSERVE = 471,
    S_EX_HTML_WITH_NPC_VIEWPORT = 472,
    S_EX_PLEDGE_CONTRIBUTION_RANK = 473,
    S_EX_PLEDGE_CONTRIBUTION_INFO = 474,
    S_EX_PLEDGE_CONTRIBUTION_REWARD = 475,
    S_EX_PLEDGE_RAID_INFO = 476,
    S_EX_PLEDGE_RAID_RANK = 477,
    S_EX_PLEDGE_LEVEL_UP = 478,
    S_EX_PLEDGE_SHOW_INFO_UPDATE = 479,
    S_EX_PLEDGE_MISSION_INFO = 480,
    S_EX_PLEDGE_MISSION_REWARD_COUNT = 481,
    S_EX_PLEDGE_MASTERY_INFO = 482,
    S_EX_PLEDGE_MASTERY_SET = 483,
    S_EX_PLEDGE_MASTERY_RESET = 484,
    S_EX_TUTORIAL_SHOW_ID = 485,
    S_EX_PLEDGE_SKILL_INFO = 486,
    S_EX_PLEDGE_SKILL_ACTIVATE = 487,
    S_EX_PLEDGE_ITEM_LIST = 488,
    S_EX_PLEDGE_ITEM_ACTIVATE = 489,
    S_EX_PLEDGE_ANNOUNCE = 490,
    S_EX_PLEDGE_ANNOUNCE_SET = 491,
    S_EX_SET_PLEDGE_EMBLEM = 492,
    S_EX_SHOW_CREATE_PLEDGE = 493,
    S_EX_PLEDGE_ITEM_INFO = 494,
    S_EX_PLEDGE_ITEM_BUY = 495,
    S_EX_ELEMENTAL_SPIRIT_INFO = 496,
    S_EX_ELEMENTAL_SPIRIT_EXTRACT_INFO = 497,
    S_EX_ELEMENTAL_SPIRIT_EVOLUTION_INFO = 498,
    S_EX_ELEMENTAL_SPIRIT_EVOLUTION = 499,
    S_EX_ELEMENTAL_SPIRIT_SET_TALENT = 500,
    S_EX_ELEMENTAL_SPIRIT_ABSORB_INFO = 501,
    S_EX_ELEMENTAL_SPIRIT_ABSORB = 502,
    S_EX_CHOOSE_LOCKED_ITEM = 503,
    S_EX_LOCKED_RESULT = 504,
    S_EX_ELEMENTAL_SPIRIT_EXTRACT = 505,
    S_EX_OLYMPIAD_INFO = 506,
    S_EX_OLYMPIAD_RECORD = 507,
    S_EX_OLYMPIAD_MATCH_INFO = 508,
    S_EX_ELEMENTAL_SPIRIT_GET_EXP = 509,
    S_EX_ITEM_ANNOUNCE = 510,
    S_EX_COMPLETED_DAILY_QUEST_LIST = 511,
    S_EX_COMPLETED_DAILY_QUEST = 512,
    S_EX_TRY_ENCHANT_ARTIFACT_RESULT = 513,
    S_EX_MAX = 514,
};


namespace protocol {


#pragma pack(push, 1)

struct S_NPCInfo_State final : network::IPacket {
    static constexpr std::uint16_t bitCount = 4;

    bool Init();
    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::uint8_t bits[1]; // +0x08
    ::CVariableBitSet bitSet; // +0x09
    char cIsDead; // +0x19
    char cIsCombatMode; // +0x1a
    char cMoveMode; // +0x1b
    char cCreatureMoveType; // +0x1c
};

struct S_NPCInfo_Speed final : network::IPacket {
    static constexpr std::uint16_t bitCount = 4;

    bool Init();
    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::uint8_t bits[1]; // +0x08
    ::CVariableBitSet bitSet; // +0x09
    float fMoveSpeedModifier; // +0x19
    float fAttackSpeedModifier; // +0x1d
    std::int16_t hMCastingSpeed; // +0x21
    std::int16_t hPCastingSpeed; // +0x23
};

struct UserInfo_Basic {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    std::u16string wstrName; // +0x00
    char nBuilder; // +0x28
    char nRace; // +0x29
    char nSex; // +0x2a
    std::int32_t nOriginalClass; // +0x2b
    std::int32_t nNowClass; // +0x2f
    char nLevel; // +0x33
};

struct UserInfo_Stat {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    std::int16_t nStr;
    std::int16_t nDex;
    std::int16_t nCon;
    std::int16_t nInt;
    std::int16_t nWit;
    std::int16_t nMen;
    std::int16_t nLuc;
    std::int16_t nCha;
};

struct UserInfo_BaseAttr {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    std::int32_t nBaseHP;
    std::int32_t nBaseMP;
    std::int32_t nBaseCP;
};

struct UserInfo_Attr {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    std::int32_t nHP;
    std::int32_t nMP;
    std::int32_t nCP;
    std::int64_t nSP;
    std::int64_t nExp;
    double expRatio;
};

struct UserInfo_EquipSlot {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    char cEnchant;
    char hMinSetItemEnchanted;
};

struct UserInfo_HairShop {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    std::int32_t nFace;
    std::int32_t nHairShape;
    std::int32_t nHairColor;
    bool bIsUseHairAccessory;
};

struct UserInfo_RideStoreCrystalizeAp {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    char cYongmaType;
    char cPrivateStore;
    char canCrystallize;
    char cRemainAbilityPoint;
};

struct UserInfo_Parameter {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    std::int16_t hAttackRange;
    std::int32_t nPAttack;
    std::int32_t nPAttackSpeed;
    std::int32_t nPDefence;
    std::int32_t nPAvoid;
    std::int32_t nPHitRate;
    std::int32_t nPCriticalRate;
    std::int32_t nMAttack;
    std::int32_t nMCastingSpeed;
    std::int32_t nPCastingSpeed;
    std::int32_t nMAvoid;
    std::int32_t nMDefend;
    std::int32_t nMHitRate;
    std::int32_t nMCriticalRate;
};

struct UserInfo_Defend {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    std::int16_t hFire;
    std::int16_t hWater;
    std::int16_t hWind;
    std::int16_t hEarth;
    std::int16_t hHoly;
    std::int16_t hUnholy;
};

struct UserInfo_PosVehicle {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    std::int32_t nPosX;
    std::int32_t nPosY;
    std::int32_t nPosZ;
    std::int32_t nVehicleID;
};

struct UserInfo_OrgSpeed {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    std::int16_t nOrgSpeed[8];
};

struct UserInfo_SpeedModifier {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    double dMoveSpeedModifier;
    double dAttackSpeedModifier;
};

struct UserInfo_CollisionInfo {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    double dCollisionRadius;
    double dCollisionHeight;
};

struct UserInfo_AttackInfo {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    char nAttackType;
    std::int16_t nAttackValue;
};

struct UserInfo_Pledge {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    std::u16string wstrNickName; // +0x00
    std::int16_t nPledgetype; // +0x28
    std::int32_t nPledgeSId; // +0x2a
    std::int32_t nPledgeEmblemId; // +0x2e
    std::int32_t nPledgeCrestId; // +0x32
    std::int32_t nPledgePower; // +0x36
    char cIsPledgeMaster; // +0x3a
    std::int32_t nAllianceId; // +0x3b
    std::int32_t nAllianceCrestId; // +0x3f
    char cIsShowPartyWantedMessage; // +0x43
};

struct UserInfo_Social {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    char nGuility;
    std::int32_t nCriminalRate;
    char cIsNobless;
    char cIsHero;
    char nSocialClass;
    std::int32_t nPkCount;
    std::int32_t nDuelCount;
    std::int16_t m_sVoteCount;
    std::int16_t m_sBonusCount;
};

struct UserInfo_Point {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    std::int32_t nVitalPoint;
    char cBR_LectureMark;
    std::int32_t nPVPPoint;
    std::int32_t nRaidPoint;
};

struct UserInfo_Etc1 {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    char nSkillModMaxDecoSlotCount;
    char nSkillModMaxJewelSlotCount;
    char cEventMatchTeamID;
    std::int32_t nFootEffect;
    char nSkillModMaxAgathionMainSlotCount;
    char nSkillModMaxAgathionSubSlotCount;
    char nSkillModMaxArtifactSlotGroupCount;
};

struct UserInfo_Etc2 {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    char cEnvironment;
    char cIsFastMoveMode;
};

struct UserInfo_Etc3 {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    std::int32_t nCharNameColor;
    std::int32_t nNickNameColor;
};

struct UserInfo_Etc4 {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    std::int32_t nYongmaClass;
    std::int16_t hInvenLimit;
    char nCursedWeaponLevel;
};

struct UserInfo_Etc5 {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    char cPVPRestrainStatus;
    std::int32_t nUltimateSkillGauge;
    char cIsSheathingWeapon;
    char cPledgeGameMemberFlag;
};

struct UserInfo_Elemental {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    std::int32_t nElementalAttack;
    std::int32_t nFireDefend;
    std::int32_t nWaterDefend;
    std::int32_t nWindDefend;
    std::int32_t nEarthDefend;
    std::int32_t nCurrentElemental;
};

struct S_UserInfo final : network::IPacket {
    static constexpr std::uint16_t bitCount = 24;

    bool Init();
    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::uint8_t bits[3]; // +0x008
    ::CVariableBitSet bitSet; // +0x00b
    std::int32_t nMyStatus; // +0x01b
    UserInfo_Basic basic; // +0x01f
    UserInfo_Stat stat; // +0x053
    UserInfo_BaseAttr baseAttr; // +0x063
    UserInfo_Attr attribute; // +0x06f
    UserInfo_EquipSlot equipSlot; // +0x093
    UserInfo_HairShop hairShop; // +0x095
    UserInfo_RideStoreCrystalizeAp rideStoreCrystalizeAp; // +0x0a2
    UserInfo_Parameter parameter; // +0x0a6
    UserInfo_Defend defend; // +0x0dc
    UserInfo_PosVehicle posVehicle; // +0x0e8
    UserInfo_OrgSpeed orgSpeed; // +0x0f8
    UserInfo_SpeedModifier speedModifier; // +0x108
    UserInfo_CollisionInfo collisionInfo; // +0x118
    UserInfo_AttackInfo attackInfo; // +0x128
    UserInfo_Pledge pledge; // +0x12b
    UserInfo_Social social; // +0x16f
    UserInfo_Point point; // +0x183
    UserInfo_Etc1 etc1; // +0x190
    UserInfo_Etc2 etc2; // +0x19a
    UserInfo_Etc3 etc3; // +0x19c
    UserInfo_Etc4 etc4; // +0x1a4
    UserInfo_Etc5 etc5; // +0x1ab
    UserInfo_Elemental elemental; // +0x1b2
};

struct S_UserInfo_EquipSlot {
    bool Encode(network::Stream* stream) const;
    bool Decode(network::Stream* stream);

    std::int32_t nSlotItemId; // +0x00
    std::int32_t nSlotItemClassId; // +0x04
    std::int32_t nNormalOptionNo; // +0x08
    std::int32_t nRandomOptionNo; // +0x0c
    std::int32_t nShapeShiftClassId; // +0x10
};

struct S_UserInfo_EquipInfo final : network::IPacket {
    static constexpr std::uint16_t bitCount = 60;

    bool Init();
    bool Encode(network::Stream* stream) const override;
    bool Decode(network::Stream* stream) override;

    std::uint8_t bits[8]; // +0x008
    ::CVariableBitSet bitSet; // +0x010
    S_UserInfo_EquipSlot equipUnderwear; // +0x020
    S_UserInfo_EquipSlot equipRear; // +0x034
    S_UserInfo_EquipSlot equipLear; // +0x048
    S_UserInfo_EquipSlot equipNeck; // +0x05c
    S_UserInfo_EquipSlot equipRFinger; // +0x070
    S_UserInfo_EquipSlot equipLFinger; // +0x084
    S_UserInfo_EquipSlot equipHead; // +0x098
    S_UserInfo_EquipSlot equipRHand; // +0x0ac
    S_UserInfo_EquipSlot equipLHand; // +0x0c0
    S_UserInfo_EquipSlot equipGlobes; // +0x0d4
    S_UserInfo_EquipSlot equipChest; // +0x0e8
    S_UserInfo_EquipSlot equipLegs; // +0x0fc
    S_UserInfo_EquipSlot equipFeet; // +0x110
    S_UserInfo_EquipSlot equipBack; // +0x124
    S_UserInfo_EquipSlot equipRLHand; // +0x138
    S_UserInfo_EquipSlot equipHair; // +0x14c
    S_UserInfo_EquipSlot equipHair2; // +0x160
    S_UserInfo_EquipSlot equipRBracelet; // +0x174
    S_UserInfo_EquipSlot equipLBracelet; // +0x188
    S_UserInfo_EquipSlot equipAgathionMain; // +0x19c
    S_UserInfo_EquipSlot equipAgathionSub1; // +0x1b0
    S_UserInfo_EquipSlot equipAgathionSub2; // +0x1c4
    S_UserInfo_EquipSlot equipAgathionSub3; // +0x1d8
    S_UserInfo_EquipSlot equipAgathionSub4; // +0x1ec
    S_UserInfo_EquipSlot equipDeco1; // +0x200
    S_UserInfo_EquipSlot equipDeco2; // +0x214
    S_UserInfo_EquipSlot equipDeco3; // +0x228
    S_UserInfo_EquipSlot equipDeco4; // +0x23c
    S_UserInfo_EquipSlot equipDeco5; // +0x250
    S_UserInfo_EquipSlot equipDeco6; // +0x264
    S_UserInfo_EquipSlot equipWaist; // +0x278
    S_UserInfo_EquipSlot equipBrooch; // +0x28c
    S_UserInfo_EquipSlot equipJewel1; // +0x2a0
    S_UserInfo_EquipSlot equipJewel2; // +0x2b4
    S_UserInfo_EquipSlot equipJewel3; // +0x2c8
    S_UserInfo_EquipSlot equipJewel4; // +0x2dc
    S_UserInfo_EquipSlot equipJewel5; // +0x2f0
    S_UserInfo_EquipSlot equipJewel6; // +0x304
    S_UserInfo_EquipSlot equipArtifactbook; // +0x318
    S_UserInfo_EquipSlot equipArtifact_a1; // +0x32c
    S_UserInfo_EquipSlot equipArtifact_a2; // +0x340
    S_UserInfo_EquipSlot equipArtifact_a3; // +0x354
    S_UserInfo_EquipSlot equipArtifact_a4; // +0x368
    S_UserInfo_EquipSlot equipArtifact_a5; // +0x37c
    S_UserInfo_EquipSlot equipArtifact_a6; // +0x390
    S_UserInfo_EquipSlot equipArtifact_a7; // +0x3a4
    S_UserInfo_EquipSlot equipArtifact_a8; // +0x3b8
    S_UserInfo_EquipSlot equipArtifact_a9; // +0x3cc
    S_UserInfo_EquipSlot equipArtifact_a10; // +0x3e0
    S_UserInfo_EquipSlot equipArtifact_a11; // +0x3f4
    S_UserInfo_EquipSlot equipArtifact_a12; // +0x408
    S_UserInfo_EquipSlot equipArtifact_b1; // +0x41c
    S_UserInfo_EquipSlot equipArtifact_b2; // +0x430
    S_UserInfo_EquipSlot equipArtifact_b3; // +0x444
    S_UserInfo_EquipSlot equipArtifact_c1; // +0x458
    S_UserInfo_EquipSlot equipArtifact_c2; // +0x46c
    S_UserInfo_EquipSlot equipArtifact_c3; // +0x480
    S_UserInfo_EquipSlot equipArtifact_d1; // +0x494
    S_UserInfo_EquipSlot equipArtifact_d2; // +0x4a8
    S_UserInfo_EquipSlot equipArtifact_d3; // +0x4bc
};

#pragma pack(pop)


} // namespace protocol
