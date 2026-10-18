#include "BaseCampAgit.h"

CBaseCampAgit::CBaseCampAgit() {
    // Current L2Server 0x0042ae64.
    L2_GUARD_FUNCTION();
}

CBaseCampAgit::~CBaseCampAgit() {
    // Current L2Server 0x0042b05c.
    L2_GUARD_FUNCTION();
}

void CBaseCampAgit::TimerExpired(int) {
    // Current L2Server 0x0042b0f8 is guard-only.
    L2_GUARD_FUNCTION();
}

bool CBaseCampAgit::IsBaseCampAgit() const { return true; }

CBaseCampAgit* CBaseCampAgit::CastBaseCampAgit() const { return const_cast<CBaseCampAgit*>(this); }
