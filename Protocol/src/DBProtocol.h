#pragma once

#include <cstdint>

// Original Server/Main/Protocol/DBProtocol.h: TotalChecker2015
// TPI 0x3E003, IPI source line 180. Exact project manifests keep the owner.
enum JobFinishType : std::int32_t {
    JFT_LOAD_DB_RELATED_COUNTS = 0,
    JFT_LOAD_ALLIANCE = 1,
    JFT_LOAD_CASTLE = 2,
    JFT_LOAD_AGIT = 3,
    JFT_LOAD_FORTRESS = 4,
    JFT_LOAD_DISMISS_RESERVED = 5,
    JFT_LOAD_WAR = 6,
    JFT_LOAD_NOBLESS = 7,
    JFT_LOAD_PLEDGE = 8,
    JFT_LOAD_OLYMPIAD = 9,
    JFT_LOAD_PLEDGEGAME = 10,
    JFT_LOAD_DOMINION = 11,
    JFT_LOAD_NOBLESS_MATCH_COUNT = 12,
    JFT_LOAD_FISHING_CHAMPIONSHIP = 13,
    JFT_WAIT_PLEDGE_RESERVE = 14,
    JFT_LOAD_WORLD_OLYMPIAD_USER = 15,
    JFT_MAX = 16,
};
