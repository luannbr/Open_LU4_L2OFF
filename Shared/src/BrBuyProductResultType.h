#pragma once

// The enum name is present in the current Cached.exe and L2Server.exe guard
// signatures. TotalChecker2015.pdb type 0x15c7e keeps the complete
// enumerator list. The values used by CacheD are visible in the
// current machine code.
enum BrBuyProductResultType : int {
    BR_BUY_SUCCESS = 1,
    BR_BUY_RESERVED = 0,
    BR_BUY_LACK_OF_POINT = -1,
    BR_BUY_INVALID_PRODUCT = -2,
    BR_BUY_INVALID_AMOUNT = -3,
    BR_BUY_INVENTROY_OVERFLOW = -4,
    BR_BUY_CLOSED_PRODUCT = -5,
    BR_BUY_SERVER_ERROR = -6,
    BR_BUY_BEFORE_SALE_DATE = -7,
    BR_BUY_AFTER_SALE_DATE = -8,
    BR_BUY_INVALID_USER = -9,
    BR_BUY_INVALID_ITEM = -10,
    BR_BUY_INVALID_USER_STATE = -11,
    BR_BUY_NOT_DAY_OF_WEEK = -12,
    BR_BUY_NOT_TIME_OF_DAY = -13,
    BR_BUY_SOLD_OUT = -14,
    BR_BUY_SOLD_OUT_CHARACTER = -15,
    BR_BUY_SOLD_OUT_ACCOUNT = -16,
    BR_BUY_INVALID_RECEIVER = -17,
    BR_BUY_ME_TO_ME = -18,
    BR_BUY_SEND_POST_COUNT = -19,
    BR_BUY_RECEIVE_POST_COUNT = -20,
    BR_BUY_BLOCKED = -21,
    BR_BUY_MAX_ITEM_IN_POST = -22,
    BR_BUY_PERIODIC_ITEM = -23,
    BR_BUY_LEVEL_LIMIT = -24,
    BR_BUY_LACK_OF_ADENA = -25,
    BR_BUY_LACK_OF_EVENTCOIN = -26,
    BR_BUY_BIRTHDAY_LIMIT = -27,
    BR_BUY_NUMBER_LIMIT = -28,
    BR_BUY_LACK_OF_MONEY = -29,
    BR_BUY_VIP_GIFT_LEVEL = -30,
};
