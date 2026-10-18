#include "PledgeAnnounceRegistry.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/ScopedLockGuards.h"

#include <string>

// Current L2Server 0x0078caf0 and Cached.exe 0x004e384c are both 129 bytes.
// They construct the common xmap at +0x08 and the product lock at +0x28.
CPledgeAnnounceRegistry::CPledgeAnnounceRegistry() = default;

// Destruction is the exact reverse: product lock, xmap, then the Singleton
// base. Current L2Server 0x0098bb04 keeps the 0x38-byte CYieldLock object.
// current Cached.exe 0x0050b474 keeps its 0x50-byte critical-section form.
CPledgeAnnounceRegistry::~CPledgeAnnounceRegistry() = default;

CPledgeAnnounceSP CPledgeAnnounceRegistry::Get(unsigned int pledgeId) {
    // Current L2Server 0x0098b4c0. Current Cached.exe 0x0050a984.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    const auto found = m_pledgeAnnounceMap.find(pledgeId);
    CPledgeAnnounceSP result;
    if (found != m_pledgeAnnounceMap.end()) {
        result = found->second;
    }
    return result;
}

bool CPledgeAnnounceRegistry::SwitchFlag(unsigned int pledgeId, bool flag) {
    // Current Cached.exe 0x0050aaf0.
    L2_GUARD_FUNCTION();
    CPledgeAnnounceSP pledgeAnnounce = Get(pledgeId);
    if (!pledgeAnnounce) {
        return false;
    }
    pledgeAnnounce->SetShowFlag(flag);
    return true;
}

bool CPledgeAnnounceRegistry::Register(CPledgeAnnounceSP& pledgeAnnounce) {
    // Current L2Server 0x0098b854. Current Cached.exe 0x0050b1c4.
    L2_GUARD_FUNCTION();
    if (!pledgeAnnounce) {
        return false;
    }

    CPledgeAnnounceSP registered = Get(static_cast<unsigned int>(pledgeAnnounce->GetId()));
    if (!registered) {
        L2_GUARD(m_lock);
        m_pledgeAnnounceMap[static_cast<unsigned int>(pledgeAnnounce->GetId())] = pledgeAnnounce;
    } else {
        registered->SetContent(pledgeAnnounce->GetContent().c_str());
        registered->SetShowFlag(pledgeAnnounce->DoesShow());
    }
    return true;
}
