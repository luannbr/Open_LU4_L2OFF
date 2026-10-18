#pragma once

#include "Platform/CriticalSection.h"
#include "Platform/WindowsTypes.h"

#include <cstdint>
#include <map>

// Original owner: Src/ipaccesslimit.h / Src/ipaccesslimit.cpp.
// L2Authd.pdb type 0x78EC, target size 0x204. The target's unsigned long map
// key is 32-bit here so the address contract does not widen on
// LP64 hosts.
class CIPAccessLimit {
public:
    CIPAccessLimit();
    virtual ~CIPAccessLimit();

    bool SetAccessIP(in_addr address);
    bool DelAccessIP(in_addr address);

private:
    int GetIPLockValue(in_addr address);

    authd::platform::CriticalSectionStorage32 lock[16];
    std::map<std::uint32_t, int> accessmap[16];
};

extern CIPAccessLimit IPaccessLimit;
