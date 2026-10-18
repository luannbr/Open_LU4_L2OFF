#pragma once

// Global wire/result type shared by CacheD and L2Server. The exact spelling,
// signed 32-bit underlying type and all six enumerators are present in the
// matching NASC64_2015 PDB TPI record 0x1a22e. Current Cached.exe and the
// paired L2Server.exe keep BuySellResult in their guard
// signatures and use these same machine values.
enum BuySellResult : int {
    BSR_INVENTROY_OVERFLOW = -4,
    BSR_USER_CANCEL = -3,
    BSR_INVALID_ITEMS = -2,
    BSR_LACK_OF_MONEY = -1,
    BSR_RESERVED = 0,
    BSR_SUCCESS = 1,
};
