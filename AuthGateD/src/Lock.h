#pragma once

#include "StateMonMgr.h"

#include <atomic>
#include <string>

class CLock {
public:
    CLock();
    explicit CLock(const char16_t* name, bool registerMonitoring = false);
    virtual ~CLock() = default;

    CLock(const CLock&) = delete;
    CLock& operator=(const CLock&) = delete;

    void Lock();
    void Unlock();
    virtual void RegisterMonitoringObjects();

private:
    std::atomic<std::int32_t> m_nBlocked{0};
    std::u16string m_strName;
    MonObj m_monLockCount;
};
