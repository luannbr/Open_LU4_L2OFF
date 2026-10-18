#pragma once

#include "Lock.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>

struct TryRecord {
    std::array<std::int32_t, 63> tryPoint{};
    std::int32_t blockTime = 0;
    std::int32_t lastPos = 0;
};

class TryList {
public:
    TryList();
    ~TryList() = default;

    TryList(const TryList&) = delete;
    TryList& operator=(const TryList&) = delete;

    bool IsBlocked(const char* userId);
    bool IsOverTried(
        [[maybe_unused]] const char* userId,
        TryRecord* record);

private:
    std::map<std::string, TryRecord> m_table;
    CLock m_lock;
};
