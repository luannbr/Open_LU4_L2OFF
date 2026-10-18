#pragma once

#include "Shared/src/IOCs.h"
#include "Shared/src/PledgeAnnounce.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>


// Shipping owner: Shared/PledgeAnnounceRegistry.{h,cpp}. The paired PDB
// supplies the exact member and alias names. The product specializations
// share the xmap at +0x08 but keep their current physical lock: L2Server
// uses a 0x10-byte CYieldLock, while CacheD uses CIOCriticalSection.
class CPledgeAnnounceRegistry : public Singleton<CPledgeAnnounceRegistry> {
public:
    using PledgeAnnounceMap = std::map<unsigned int, CPledgeAnnounceSP>;
    using PledgeAnnounceMapIter = PledgeAnnounceMap::iterator;
#if defined(L2_BUILD_L2SERVER)
    using LockType = CYieldLock;
#else
    using LockType = CIOCriticalSection;
#endif

    CPledgeAnnounceRegistry();
    ~CPledgeAnnounceRegistry() override;

    CPledgeAnnounceSP Get(unsigned int pledgeId);
    bool Register(CPledgeAnnounceSP& pledgeAnnounce);
    bool SwitchFlag(unsigned int pledgeId, bool flag);

protected:
    PledgeAnnounceMap m_pledgeAnnounceMap; // +0x08
    LockType m_lock; // +0x28
};
