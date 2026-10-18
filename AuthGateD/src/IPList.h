#pragma once

#include "Shared/src/Runtime/WindowsTypes.h"

#include <cstdint>
#include <vector>

struct IPRecord {
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
};

class CIPList {
public:
    CIPList();
    ~CIPList() = default;

    bool IpExists(in_addr address);
    bool Load(const char16_t* fileName);

private:
    std::vector<IPRecord> m_table;
};
