#pragma once

#include <atomic>
#include <cstdint>

struct MonObj;

class CIOObject {
public:
    CIOObject();
    virtual ~CIOObject();

    CIOObject(const CIOObject&) = delete;
    CIOObject& operator=(const CIOObject&) = delete;

    std::int32_t AddRef() noexcept;
    void ReleaseRef() noexcept;
    int PostObject(std::int32_t transferred, void* completionPort) noexcept;

protected:
    std::atomic<std::int32_t> m_refcount{0};
    std::int32_t m_objkey = 0;
};

class CIOObjectMonitor {
public:
    // The matching authgated.obj has no separate constructor procedure: this
    // one-line body was inlined into its global initializer ($E4).
    CIOObjectMonitor() { RegisterMonitoringObjects(); }

    void RegisterMonitoringObjects();

    // AuthGateD.exe IOObject.cpp owns this process-wide counter object.
    static MonObj m_monIOObject;
};
