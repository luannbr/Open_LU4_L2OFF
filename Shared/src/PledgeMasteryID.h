#pragma once

#include <cstdint>

// Exact shared enum spelling and values. TotalChecker2015.pdb supplies the
// enumerator names and values. The current Cached.exe range check at
// 0x005202b8 and current L2Server strings keep all 17 slots.
namespace PledgeMasteryID {

enum Type : std::int32_t {
    NONE = 0,
    PLEDGE_LV_1 = 1,
    PLEDGE_LV_2 = 2,
    PLEDGE_LV_3 = 3,
    PLEDGE_LV_4 = 4,
    PLEDGE_LV_5 = 5,
    PLEDGE_LV_6 = 6,
    COMBAT_LV_1 = 7,
    COMBAT_LV_2 = 8,
    COMBAT_LV_3 = 9,
    COMBAT_LV_4 = 10,
    COMBAT_LV_5 = 11,
    WAR_LV_1 = 12,
    WAR_LV_2 = 13,
    WAR_LV_3 = 14,
    WAR_LV_4 = 15,
    WAR_LV_5 = 16,
    MAX_COUNT = 17,
};

} // namespace PledgeMasteryID
