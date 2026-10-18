#include "IPAccessLimit.h"
#include "Shared/src/ScopedLockGuards.h"

#include "Config.h"

#include <cstdint>

CIPAccessLimit IPaccessLimit;

// L2Authd.exe 0x00453D80, Src/ipaccesslimit.cpp:22-30.
int CIPAccessLimit::GetIPLockValue(
    in_addr address) {
    const auto* const octets =
        reinterpret_cast<const unsigned char*>(
            &address.s_addr);
    int lockValue =
        (static_cast<int>(octets[0]) +
         static_cast<int>(octets[1]) +
         static_cast<int>(octets[2]) +
         static_cast<int>(octets[3])) &
        0x0f;
    if (lockValue > 0x0f) {
        lockValue = 0x0f;
    }
    return lockValue;
}

// L2Authd.exe 0x00453DD0, Src/ipaccesslimit.cpp:34-57.
bool CIPAccessLimit::SetAccessIP(
    in_addr address) {
    if (config.IPAccessLimit < 1) {
        return true;
    }

    bool accepted = false;
    const int lockValue = GetIPLockValue(address);
    const std::uint32_t key = address.s_addr;
    {
        L2_GUARD(lock[lockValue]);
        auto& shard = accessmap[lockValue];
        const auto found = shard.find(key);
        if (found != shard.end()) {
            if (found->second < config.IPAccessLimit) {
                ++found->second;
                accepted = true;
            }
        } else {
            shard.insert(std::make_pair(key, 1));
            accepted = true;
        }
    }
    return accepted;
}

// L2Authd.exe 0x00453EE0, Src/ipaccesslimit.cpp:60-80.
bool CIPAccessLimit::DelAccessIP(
    in_addr address) {
    if (config.IPAccessLimit < 1) {
        return true;
    }

    bool removed = false;
    const int lockValue = GetIPLockValue(address);
    const std::uint32_t key = address.s_addr;
    {
        L2_GUARD(lock[lockValue]);
        auto& shard = accessmap[lockValue];
        const auto found = shard.find(key);
        if (found != shard.end()) {
            if (found->second <= 1) {
                shard.erase(found);
            } else {
                --found->second;
            }
            removed = true;
        }
    }
    return removed;
}

// L2Authd.exe 0x00453FE0, Src/ipaccesslimit.cpp:10-13.
CIPAccessLimit::CIPAccessLimit() {
    for (auto& criticalSection : lock) {
        authd::platform::InitializeCriticalSection(
            criticalSection);
    }
}

// L2Authd.exe 0x00454050, Src/ipaccesslimit.cpp:16-19.
CIPAccessLimit::~CIPAccessLimit() {
    for (auto& criticalSection : lock) {
        authd::platform::DeleteCriticalSection(
            criticalSection);
    }
}
