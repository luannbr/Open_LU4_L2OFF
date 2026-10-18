#pragma once

#include "Shared/src/PledgeAnnounce.h"

// Shipping owner: CacheD/PledgeAnnounceDBHandlerImpl.{h,cpp}. Current
// Cached.exe fixes this fieldless polymorphic owner and all three SQL paths.
class CPledgeAnnounceDBHandlerImpl {
public:
    // Current Cached.exe 0x0050a1fc / 0x0050a20c. These defaulted special
    // members are the original fieldless vptr-only construction/destruction,
    // not portable test scaffolding. The deleting destructor is 0x0050a218.
    CPledgeAnnounceDBHandlerImpl() = default;
    virtual ~CPledgeAnnounceDBHandlerImpl() = default;

    CPledgeAnnounceSP Load(unsigned int pledgeDbId);
    bool Set(CPledgeAnnounceSP pledgeAnnounce);
    bool SwitchFlag(unsigned int pledgeDbId, bool flag);
};
