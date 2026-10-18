#include "Job.h"
#include "Shared/src/ScopedLockGuards.h"

#include "Log.h"

#include "Shared/src/Runtime/EventHandle.h"
#include "Shared/src/Runtime/OperatorLog.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/TickCount32.h"

#include <cstddef>
#include <cstdint>
#include <limits>

CJob job;

// L2Authd.exe 0x0045FD80, Src/job.cpp:190-195.
void CIOTopTimer::OnTimerCallback() {
    if (RegisterTimer(86400000U, 1) != 0) {
        ReleaseRef();
    }
}

// L2Authd.exe 0x0045FDB0, Src/job.cpp:160-174.
CJob::CJob()
    : pIOTopTimer(nullptr),
      m_lock(eCriticalSection, 0),
      m_topTick(0),
      m_timerEvent(nullptr),
      m_terminateEvent(nullptr),
      m_terminating(0) {
    m_timerEvent =
        runtime::event_handle::Create(false, false);
    m_terminateEvent =
        runtime::event_handle::Create(true, false);
    // CJob is global. CLog may not be constructed yet.
    if (m_timerEvent == nullptr) {
        runtime::logging::Write("authd", runtime::logging::Level::Error,
            "[jobs] Cannot create the timer wakeup event. Initialization continues; queued timers cannot wake the timer loop through this event.", __FILE__);
    }
    if (m_terminateEvent == nullptr) {
        runtime::logging::Write("authd", runtime::logging::Level::Error,
            "[jobs] Cannot create the shutdown event. Initialization continues; shutdown cannot wake waiting loops through this event.", __FILE__);
    }

    pIOTopTimer = new CIOTopTimer;
    m_topTick =
        runtime::TickCount32() + 86400000U;
    m_timerQueue.push(
        CIOTimer(pIOTopTimer, m_topTick));
    m_handles.push_back(m_terminateEvent);
    m_objects.push_back(pIOTopTimer);
}

// L2Authd.exe 0x0045FF00, Src/job.cpp:177-180.
CJob::~CJob() {
    runtime::event_handle::Close(m_timerEvent);
    runtime::event_handle::Close(m_terminateEvent);
}

// L2Authd.exe 0x0045FF60, Src/job.cpp:113-136.
int CJob::PushTimer(
    CIOObject* pObject,
    std::uint32_t milisec,
    int setEvent) {
    const std::uint32_t tick =
        runtime::TickCount32() + milisec;

    bool earlierTimer = false;
    {
        L2_GUARD(m_lock);
        if (runtime::raw_atomic::LoadI32Relaxed(
                &m_terminating) != 0) {
            return 0;
        }

        m_timerQueue.push(CIOTimer(pObject, tick));
        if (runtime::SignedDifference32(
                m_topTick,
                tick) > 0) {
            m_topTick = tick;
            earlierTimer = true;
        }
    }
    if (earlierTimer) {
        if (setEvent != 0) {
            // runtime::event_handle::Set(
            //     m_timerEvent)
            const bool signaled = runtime::event_handle::Set(
                m_timerEvent);
            if (!signaled) {
                // Log.AddLog(LOG_ERROR,
                //     "component=runtime event=timer_wakeup_failed reason=event_set_failed action=keep_queued_timer")
                // Log.AddLog(LOG_ERROR,
                //     "[jobs] Could not signal the timer event. The timer remains queued.")
                L2_LOG_ERROR(Log,
                    "[jobs] Could not signal the timer event. The timer remains queued.");
            }
        }
    }
    return 1;
}

// L2Authd.exe 0x00460000, Src/job.cpp:139-157.
int CJob::PushEvent(
    void* handle,
    CIOObject* pObject) {
    {
        L2_GUARD(m_lock);
        if (runtime::raw_atomic::LoadI32Relaxed(
                &m_terminating) != 0) {
            return 0;
        }
        if (m_handles.size() >= 64U) {
            // Log.AddLog(LOG_ERROR,
            //     "component=runtime event=event_registration_rejected reason=handle_limit max_handles=64 action=return_failure")
            // Log.AddLog(LOG_ERROR,
            //     "[jobs] Cannot register another event: the 64-handle limit has been reached.")
            L2_LOG_ERROR(Log,
                "[jobs] Cannot register another event: the 64-handle limit has been reached.");
            return 0;
        }

        m_handles.push_back(handle);
        m_objects.push_back(pObject);
    }
    [[maybe_unused]] const int k =
        static_cast<int>(m_objects.size());
    return 1;
}

// L2Authd.exe 0x00460090, Src/job.cpp:183-187.
void CJob::SetTerminate() {
    runtime::raw_atomic::StoreI32Relaxed(
        &m_terminating,
        1);
    // runtime::event_handle::Set(
    //     m_terminateEvent)
    const bool terminationSignaled = runtime::event_handle::Set(
        m_terminateEvent);
    // runtime::event_handle::Set(
    //     m_timerEvent)
    const bool timerSignaled = runtime::event_handle::Set(
        m_timerEvent);
    if (!terminationSignaled || !timerSignaled) {
        // Log.AddLog(LOG_ERROR,
        //     "component=runtime event=termination_wakeup_failed event_loop_signaled=%d timer_loop_signaled=%d action=retain_termination_request",
        //     terminationSignaled, timerSignaled)
        // Log.AddLog(LOG_ERROR,
        //     "[jobs] Shutdown wakeup was incomplete: event loop %s, timer loop %s. The termination request remains set.",
        //     terminationSignaled ? "signaled" : "not signaled", timerSignaled ? "signaled" : "not signaled")
        L2_LOG_ERROR(Log,
            "[jobs] Shutdown wakeup was incomplete: event loop %s, timer loop %s. The termination request remains set.",
            terminationSignaled ? "signaled" : "not signaled", timerSignaled ? "signaled" : "not signaled");
    }
}

// L2Authd.exe 0x004600C0, Src/job.cpp:27-68.
void CJob::RunTimer() {
    void* events[2] = {
        m_terminateEvent,
        m_timerEvent,
    };

    do {
        const std::uint32_t tick =
            runtime::TickCount32();
        std::int32_t waitTime =
            runtime::SignedDifference32(
                m_topTick,
                tick);
        if (waitTime < 1) {
            {
                L2_GUARD(m_lock);
                CIOTimer top = m_timerQueue.top();
                CIOObject* pObject = top.m_pObject;
                for (;;) {
                    m_timerQueue.pop();
                    pObject->OnTimerCallback();

                    top = m_timerQueue.top();
                    waitTime =
                        runtime::SignedDifference32(
                            top.m_tick,
                            tick);
                    if (waitTime > 0) {
                        break;
                    }
                    pObject = top.m_pObject;
                }
                m_topTick = top.m_tick;
            }
        }

        [[maybe_unused]] const auto waitResult =
            runtime::event_handle::WaitAny(
                events,
                2,
                static_cast<std::uint32_t>(waitTime));
    } while (runtime::raw_atomic::LoadI32Relaxed(
                 &m_terminating) == 0);

    {
        L2_GUARD(m_lock);
        while (!m_timerQueue.empty()) {
            m_timerQueue.top().m_pObject->ReleaseRef();
            m_timerQueue.pop();
        }
    }
}

// L2Authd.exe 0x004602A0, Src/job.cpp:71-110.
void CJob::RunEvent() {
    bool waitFailureReported = false;
    for (;;) {
        const std::optional<std::size_t> waitResult =
            runtime::event_handle::WaitAny(
                m_handles.data(),
                m_handles.size(),
                std::numeric_limits<
                    std::uint32_t>::max());
        if (runtime::raw_atomic::LoadI32Relaxed(
                &m_terminating) != 0) {
            break;
        }
        if (waitResult.has_value() &&
            *waitResult < m_handles.size()) {
            if (waitFailureReported) {
                // Log.AddLog(LOG_NORMAL,
                //     "component=runtime event=event_wait_recovered action=resume_event_dispatch")
                // Log.AddLog(LOG_NORMAL,
                //     "[jobs] Event wait recovered. Resuming callback dispatch.")
                L2_LOG_INFO(Log,
                    "[jobs] Event wait recovered. Resuming callback dispatch.");
                waitFailureReported = false;
            }
            m_objects[*waitResult]->OnEventCallback();
        } else if (!waitFailureReported) {
            // Noise: the retry loop can repeat immediately. Emit one failure
            // until a valid event restores dispatch, then allow a new report.
            // Log.AddLog(LOG_ERROR,
            //     "component=runtime event=event_wait_failed reason=no_valid_event action=retry_wait repeat_policy=once_until_recovery")
            // Log.AddLog(LOG_ERROR,
            //     "[jobs] Event wait returned no valid event. Retrying, repeated failures are suppressed until recovery.")
            L2_LOG_ERROR(Log,
                "[jobs] Event wait returned no valid event. Retrying; repeated failures are suppressed until recovery.");
            waitFailureReported = true;
        }
    }

    for (auto it = m_objects.begin();
         it != m_objects.end();) {
        it = m_objects.erase(it);
    }
}

// L2Authd.exe 0x00460380, Src/job.cpp:16-24. The spelling IOOBject is the
// exact public symbol in L2Authd.pdb.
std::uint32_t IOOBjectTimerCallback(void* param) {
    CIOObject* const pIOObject =
        static_cast<CIOObject*>(param);
    pIOObject->OnTimerCallback();
    return 0;
}
