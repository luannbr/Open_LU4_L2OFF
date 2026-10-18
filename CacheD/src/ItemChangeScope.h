#pragma once

// CacheD item-transaction change markers. The C1 source supplies the
// original names. Current Cached.exe confirms every numeric
// value through the inlined Item.cpp/WareHouse.cpp accesses to CItem +0x54.
enum ItemChangeScope : int {
    ItemChangeScope_Unknown = -1,
    ItemChangeScope_Added = 1,
    ItemChangeScope_Increased = 2,
    ItemChangeScope_Decreased = 3,
    ItemChangeScope_Deleted = 4,
    ItemChangeScope_Fee = 5,
};
