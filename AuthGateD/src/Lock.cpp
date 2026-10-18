#include "Lock.h"

#include "GlobalFunc.h"

#include <chrono>
#include <thread>

CLock::CLock()
    : m_strName(u"Default Lock") {}

CLock::CLock(const char16_t* name, bool registerMonitoring)
    : m_strName(name ? name : u"") {
    if (registerMonitoring) RegisterMonitoringObjects();
}

void CLock::Lock() {
    m_monLockCount.iValue.fetch_add(1, std::memory_order_relaxed);

    for (int attempt = 0; attempt < 1000; ++attempt) {
        std::int32_t expected = 0;
        if (m_nBlocked.compare_exchange_strong(
                expected,
                1,
                std::memory_order_acquire,
                std::memory_order_relaxed)) {
            return;
        }
    }
    for (int attempt = 0; attempt < 1000; ++attempt) {
        std::int32_t expected = 0;
        if (m_nBlocked.compare_exchange_strong(
                expected,
                1,
                std::memory_order_acquire,
                std::memory_order_relaxed)) {
            return;
        }
        std::this_thread::yield();
    }
    for (int waited = 0;; waited += 100) {
        std::int32_t expected = 0;
        if (m_nBlocked.compare_exchange_strong(
                expected,
                1,
                std::memory_order_acquire,
                std::memory_order_relaxed)) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (waited >= 59900) ThrowDeadlockException();
    }
}

void CLock::Unlock() {
    m_nBlocked.store(0, std::memory_order_release);
    m_monLockCount.iValue.fetch_sub(1, std::memory_order_relaxed);
}

void CLock::RegisterMonitoringObjects() {
    m_monLockCount.strName = m_strName + u".LockCount";
    if (CStateMonMgr::g_pStateMonMgr) {
        CStateMonMgr::g_pStateMonMgr->Register(&m_monLockCount);
    }
}
