#include "KillPVPPointChecker.h"
#include "DB.h"
#include "Shared/src/UserConst.h"
#include "User.h"

#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"

#include <cstdint>

CKillPVPPointChecker::CKillPVPPointChecker() : m_bStatus(true), m_nGiveCount(0), m_tLastGiveTime(-181) {}

CKillPVPPointChecker::~CKillPVPPointChecker() = default;

bool CKillPVPPointChecker::CheckGivableAndSendChange(User* const user) {
    // Current L2Server 0x007f5070.
    L2_GUARD_FUNCTION();

    const std::int32_t currentTime = runtime::crt::Time32Now();
    if (m_nGiveCount < 10 && runtime::SubtractInt32Wrapping(currentTime, m_tLastGiveTime) > 180) {
        if (!m_bStatus) {
            m_bStatus = true;
            user->ParamChanged(PCB_USER);
        }
    } else if (m_bStatus) {
        m_bStatus = false;
        user->ParamChanged(PCB_USER);
    }
    return m_bStatus;
}

void CKillPVPPointChecker::SaveGiveCount(User* const user) {
    // Current L2Server 0x007f4fbc.
    L2_GUARD_FUNCTION();
    db.RequestSavePVPPointRestrain(user, m_nGiveCount, m_tLastGiveTime);
}

void CKillPVPPointChecker::ResetAll(User* const user) {
    // Current L2Server 0x007f514c.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        m_bStatus = true;
        m_nGiveCount = 0;
        m_tLastGiveTime = 0;
        user->SendMyInfo();
        SaveGiveCount(user);
    }
}

void CKillPVPPointChecker::LoadGiveCount(User* const user) {
    // Current L2Server 0x007f5214.
    L2_GUARD_FUNCTION();
    db.RequestLoadPVPPointRestrain(user);
}

void CKillPVPPointChecker::SetGiveCount(int giveCount, int lastGiveTime, User* user [[maybe_unused]]) {
    L2_GUARD_FUNCTION();

    m_tLastGiveTime = lastGiveTime;
    m_nGiveCount = giveCount;
}
