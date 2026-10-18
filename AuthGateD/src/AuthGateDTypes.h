#pragma once

#include <cstdint>

// Exact enum names and values imported from the matching AuthGateD PDB.
enum LogType : std::int32_t {
    LOG_SYSTEM = 0,
    LOG_NORMAL = 1,
    LOG_DEBUG = 2,
    LOG_WARN = 3,
    LOG_ERROR = 4,
    LOG_DBG_PACKET = 5,
};

enum SocketMode : std::int32_t {
    SM_READ_LEN = 0,
    SM_READ_BODY = 1,
    SM_CLOSE = 2,
};

enum SocketTimeoutOpt : std::int32_t {
    ST_CONNECT = 0,
    ST_LAST_IO = 1,
};
