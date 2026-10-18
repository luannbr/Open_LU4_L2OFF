#pragma once

#include "IOObject.h"
#include "Lock.h"

#include "Shared/src/Runtime/TickCount32.h"

#include <cstdint>
#include <queue>
#include <vector>

// Original owner: Src/job.h. L2Authd.pdb type 0x786D, sizeof 0x08.
class CIOTimer {
public:
    CIOTimer(
        CIOObject* pObject,
        std::uint32_t tick)
        : m_tick(tick),
          m_pObject(pObject) {}

    bool operator<(const CIOTimer& right) const noexcept {
        return m_tick != right.m_tick &&
            runtime::SignedDifference32(
                m_tick,
                right.m_tick) > 0;
    }

    std::uint32_t m_tick;
    CIOObject* m_pObject;
};

// Original owner: Src/job.h. L2Authd.pdb type 0x7866, sizeof 0x08.
#if defined(__clang__)
#  pragma clang diagnostic push
// PDB type 0x7866 proves this differently ordered overload adds
// vtable slot +0x18 instead of overriding CIOObject's +0x04 slot.
#  pragma clang diagnostic ignored "-Woverloaded-virtual"
#endif
class CIOTopTimer : public CIOObject {
public:
    CIOTopTimer() {
        refcount = 1;
    }
    ~CIOTopTimer() override = default;

    void OnTimerCallback() override;
    void OnEventCallback() override {}

    // This parameter order is exactly the additional +0x18 vtable slot in
    // the PDB. It is an overload, not CIOObject's +0x04 override.
    virtual void OnIOCallback(
        [[maybe_unused]] int success,
        [[maybe_unused]] _OVERLAPPED* pOverlapped,
        [[maybe_unused]] std::uint32_t transferred) {}
};
#if defined(__clang__)
#  pragma clang diagnostic pop
#endif

// Original owner: Src/job.h. L2Authd.pdb type 0x7487 records a 0x5C-byte
// Win32 object and the exact member order documented below. Standard-library
// containers and pointers take their native host size in the
// portable build.
class CJob {
public:
    CJob();
    ~CJob();

    int PushTimer(
        CIOObject* pObject,
        std::uint32_t milisec,
        int setEvent);
    int PushEvent(
        void* handle,
        CIOObject* pObject);
    void SetTerminate();
    void RunTimer();
    void RunEvent();

private:
    CIOTopTimer* pIOTopTimer;              // target +0x00
    CLock m_lock;                          // target +0x04
    std::uint32_t m_topTick;               // target +0x24
    void* m_timerEvent;                    // target +0x28
    void* m_terminateEvent;                // target +0x2C
    std::vector<void*> m_handles;          // target +0x30
    std::vector<CIOObject*> m_objects;     // target +0x3C
    std::priority_queue<CIOTimer>
        m_timerQueue;                      // target +0x48
    std::int32_t m_terminating;            // target +0x58
};

extern CJob job;

std::uint32_t IOOBjectTimerCallback(void* param);
