#include "L2NPC/src/Object.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/ThreadManager.h"


CObject::CObject() {
    L2_GUARD_FUNCTION();
    m_nId = 0;
    mDeleteCount = 0;
}

CObject::~CObject() = default;

bool CObject::Delete() {
    L2_GUARD_FUNCTION();
    return runtime::raw_atomic::AddI32(&mDeleteCount, 1) < 2;
}
