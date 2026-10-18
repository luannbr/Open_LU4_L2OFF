#include "IOObject.h"

#include "Shared/src/Runtime/CompletionPort.h"
#include "StateMonMgr.h"

#include <cstdlib>

namespace {

constexpr std::int32_t kObjectKey =
    static_cast<std::int32_t>(UINT32_C(0x98765432));

} // namespace

MonObj CIOObjectMonitor::m_monIOObject;

CIOObject::CIOObject()
    : m_objkey(kObjectKey) {
    CIOObjectMonitor::m_monIOObject.iValue.fetch_add(
        1,
        std::memory_order_relaxed);
}

CIOObject::~CIOObject() {
    CIOObjectMonitor::m_monIOObject.iValue.fetch_sub(
        1,
        std::memory_order_relaxed);
}

std::int32_t CIOObject::AddRef() noexcept {
    return m_refcount.fetch_add(1, std::memory_order_relaxed) + 1;
}

void CIOObject::ReleaseRef() noexcept {
    if (m_objkey != kObjectKey) std::abort();
    if (m_refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        delete this;
    }
}

int CIOObject::PostObject(
    std::int32_t transferred,
    void* completionPort) noexcept {
    return runtime::completion_port::Post(
               completionPort,
               static_cast<std::uint32_t>(transferred),
               reinterpret_cast<std::uintptr_t>(this),
               nullptr)
        ? 1
        : 0;
}

void CIOObjectMonitor::RegisterMonitoringObjects() {
    m_monIOObject.strName = u"IOObject";
    if (CStateMonMgr::g_pStateMonMgr) {
        CStateMonMgr::g_pStateMonMgr->Register(&m_monIOObject);
    }
}
