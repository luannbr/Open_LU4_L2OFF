#include "ThreadSafeLibrary.h"

#if defined(L2_BUILD_L2SERVER)
#include "Creature.h"
#include "CreatureController.h"
#include "Fishing.h"
#include "Shared/src/Exception.h"
#include "Shared/src/IOC.h"
#include "Shared/src/Log.h"
#include "StaticObject.h"
#elif defined(L2_BUILD_L2NPC)
#include "Protocol/src/L2Constants.h"
#include "L2NPC/src/Creature.h"
// identify the three process-owned NPCs in diagnostics.
#include "L2NPC/src/AIManager.h"
#include "L2NPC/src/NPC.h"
#include "Shared/src/Exception.h"
#include "Shared/src/MemorySub.h"
#endif

#include <cstdint>
#include <cwchar>
#include <iterator>

#if defined(L2_BUILD_L2SERVER)

namespace thread_safe_detail {

void FailInvalidPipeThread(
    // int,
    int threadId,
    const wchar_t* functionName
) {
    // Noise: duplicate console calls become one ERROR with the required local-file copy.
    // Log.Add(
    //     LOG_LOCAL,
    //     L"WHAT!!!!!")
    // Log.Add(
    //     LOG_ERROR,
    //     L"WHAT!!!!!")
    // runtime::log::WriteErrorWithLocalCopy(Log,
    //     "[thread pipe] Invalid writer thread %d. Terminating the server.",
    //     threadId)
    L2_LOG_ERROR_WITH_LOCAL_COPY(Log, "[thread pipe] Invalid writer thread %d; terminating the server.", threadId);
    ForceCrash(CrashInfo::INVALID_THREAD_NO, functionName, __WFILE__, __LINE__);
}

void FailPipeOverflow(const wchar_t* functionName) {
    GuardHelper failureGuard(L"Thread pipe overflow!!! increase buffer size", CThreadManager::GetThreadID());
    // Log.Add(LOG_ERROR,
    //     "[thread pipe] Write capacity exceeded, terminating the server.")
    L2_LOG_ERROR(Log, "[thread pipe] Write capacity exceeded; terminating the server.");
    ForceCrash(CrashInfo::THREADPIPE_OVERFLOW, functionName, __WFILE__, __LINE__);
}

void FailPipeReadOverflow(const wchar_t* functionName) {
    // Log.Add(LOG_ERROR,
    //     "[thread pipe] Read count exceeded the pipe capacity, terminating the server.")
    L2_LOG_ERROR(Log, "[thread pipe] Read count exceeded the pipe capacity; terminating the server.");
    ForceCrash(CrashInfo::PROCEEDED_OBJECT_IS_OVERFLOW, functionName, __WFILE__, __LINE__);
}

void ReportPipeIndexOrder(std::int32_t firstIndex, std::int32_t lastIndex) {
    // Log.Add(
    //     LOG_ERROR,
    //     L"first index is larger than last index. %d > %d",
    //     firstIndex,
    //     lastIndex)
    // Log.Add(LOG_ERROR,
    //     "[thread pipe] Inconsistent queue indices: first %d exceeds last %d.",
    //     firstIndex, lastIndex)
    L2_LOG_ERROR(Log, "[thread pipe] Inconsistent queue indices: first %d exceeds last %d.", firstIndex, lastIndex);
}

} // namespace thread_safe_detail

namespace {

constexpr int kThreadLocalTimerListenThread = 27;
thread_local int g_threadLocalTimerCallbackHopCount = 0;

std::int64_t ThreadLocalTimerTick() noexcept { return static_cast<std::int64_t>(runtime::GetTickCount()); }

} // namespace

bool CThreadLocalTimer::CSlot::AddTimer(CIOObject* object, const int id, const std::int64_t uid) {
    const int index = nCount;
    if (index > nCountPerSlot - 1) {
        return false;
    }
    nCount = index + 1;
    Object[index] = object;
    Id[index] = id;
    m_Uid[index] = uid;
    return true;
}

void CThreadLocalTimer::CSlot::Dispatch() {
    L2_GUARD_FUNCTION();

    if (CThreadManager::GetThreadID() == kThreadLocalTimerListenThread) {
        for (int index = 0; index < nCount; ++index) {
            Object[index]->OnTimerCallback(Id[index], m_Uid[index]);
        }
        return;
    }

    for (int index = 0; index < nCount; ++index) {
        const int previous = g_threadLocalTimerCallbackHopCount++;
        if (previous == 100) {
            Object[index]->OnTimerCallback(Id[index], m_Uid[index]);
            g_threadLocalTimerCallbackHopCount = 0;
        } else {
            SafeIOCallback(Object[index], Id[index], m_Uid[index]);
        }
    }
}

CThreadLocalTimer::CThreadLocalTimer() {
    for (int thread = 0; thread < nMaxIOThread; ++thread) {
        for (int slotIndex = 0; slotIndex < MAX_SLOT; ++slotIndex) {
            CSlot* const slot = m_SlotQueue.Pop();
            slot->Init();
            m_ReservedSlot[thread][slotIndex] = slot;
        }
        m_CurrentTime[thread] = ThreadLocalTimerTick();
        m_CurrentSlotBase[thread] = 0;
    }
}

void CThreadLocalTimer::Dispatch(CSlot* slot, const std::int64_t slotIndex) {
    L2_GUARD_FUNCTION();

    const int thread = CThreadManager::GetThreadID();
    slot->Dispatch();
    CSlot* next = slot->Next;
    while (next) {
        m_SlotQueue.Push(slot);
        next->Dispatch();
        slot = next;
        next = next->Next;
    }
    slot->Init();
    m_ReservedSlot[thread][slotIndex] = slot;
}

void CThreadLocalTimer::AddTimer(CIOObject* object, const std::int64_t time, const int id, const std::int64_t uid) {
    L2_GUARD_FUNCTION();

    const int thread = CThreadManager::GetThreadID();
    const std::int64_t difference = runtime::SubtractInt64Wrapping(time, m_CurrentTime[thread]);
    const std::int64_t interval = difference / TIME_SLICE;
    if (interval < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid timer time",
        //     L"..\\Shared\\ThreadSafeLibrary.cpp",
        //     0x1bc)
        // Log.Add(LOG_ERROR,
        //     "[timer] Timer %d (uid %lld) rejected on worker %d: negative slot interval %lld (deadline %lld ms, cursor %lld
        //     ms).", id, static_cast<long long>(uid), thread, static_cast<long long>(interval), static_cast<long
        //     long>(time), static_cast<long long>(m_CurrentTime[thread]))
        L2_LOG_ERROR(
            Log,
            "[timer] Timer %d (uid %lld) rejected on worker %d: negative slot interval %lld (deadline %lld ms, cursor %lld "
            "ms).",
            id, static_cast<long long>(uid), thread, static_cast<long long>(interval), static_cast<long long>(time),
            static_cast<long long>(m_CurrentTime[thread])
        );
        runtime::process::ReportCallStack(LOG_ERROR, false);
        return;
    }

    if (interval < MAX_SLOT) {
        const std::int64_t bucketIndex = (m_CurrentSlotBase[thread] + interval) % MAX_SLOT;
        CSlot*& slot = m_ReservedSlot[thread][bucketIndex];
        if (!slot->AddTimer(object, id, uid)) {
            CSlot* const newSlot = m_SlotQueue.Pop();
            newSlot->Init();
            newSlot->Next = slot;
            slot = newSlot;
            if (!newSlot->AddTimer(object, id, uid)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] allocation new slot failed",
                //     L"..\\Shared\\ThreadSafeLibrary.cpp",
                //     0x1d0)
                // Log.Add(LOG_ERROR,
                //     "[timer] Timer %d (uid %lld) could not be queued on worker %d: a new slot rejected it.",
                //     id, static_cast<long long>(uid), thread)
                L2_LOG_ERROR(
                    Log, "[timer] Timer %d (uid %lld) could not be queued on worker %d: a new slot rejected it.", id,
                    static_cast<long long>(uid), thread
                );
            }
        }
        return;
    }

    m_ExtTimer[thread].push(CTimerWrapper(time, object, id, uid));
}

void CThreadLocalTimer::Dispatch() {
    L2_GUARD_FUNCTION();

    const int thread = CThreadManager::GetThreadID();
    const std::int64_t now = ThreadLocalTimerTick();
    while (m_CurrentTime[thread] < now) {
        const std::int64_t slotIndex = m_CurrentSlotBase[thread] % MAX_SLOT;
        Dispatch(m_ReservedSlot[thread][slotIndex], slotIndex);
        m_CurrentTime[thread] = runtime::AddInt64Wrapping(m_CurrentTime[thread], TIME_SLICE);
        m_CurrentSlotBase[thread] = slotIndex < MAX_SLOT - 1 ? slotIndex + 1 : 0;
    }

    TimerQueueType& queue = m_ExtTimer[thread];
    while (!queue.empty()) {
        const CTimerWrapper timer = queue.top();
        std::int64_t interval = 0;
        if (timer.m_dwTime >= m_CurrentTime[thread]) {
            const std::int64_t difference = runtime::SubtractInt64Wrapping(timer.m_dwTime, m_CurrentTime[thread]);
            interval = runtime::Win64CvttDoubleToInt64(static_cast<double>(difference) * 0.02);
            if (interval > MAX_SLOT - 1) {
                break;
            }
        }

        const std::int64_t bucketIndex = (m_CurrentSlotBase[thread] + interval) % MAX_SLOT;
        CSlot*& slot = m_ReservedSlot[thread][bucketIndex];
        if (!slot->AddTimer(timer.m_pObject, timer.m_id, timer.m_Uid)) {
            CSlot* const newSlot = m_SlotQueue.Pop();
            newSlot->Init();
            newSlot->Next = slot;
            slot = newSlot;
            if (!newSlot->AddTimer(timer.m_pObject, timer.m_id, timer.m_Uid)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] allocation new slot failed",
                //     L"..\\Shared\\ThreadSafeLibrary.cpp",
                //     0x215)
                // Log.Add(LOG_ERROR,
                //     "[timer] Deferred timer %d (uid %lld) discarded on worker %d: a new slot rejected it.",
                //     timer.m_id, static_cast<long long>(timer.m_Uid), thread)
                L2_LOG_ERROR(
                    Log, "[timer] Deferred timer %d (uid %lld) discarded on worker %d: a new slot rejected it.", timer.m_id,
                    static_cast<long long>(timer.m_Uid), thread
                );
            }
        }
        queue.pop();
    }
}

std::size_t CThreadLocalTimer::CancelOwnerForPortableShutdown(CIOObject* owner, std::size_t* nonzeroUidCount) {
    std::size_t cancelled = 0;
    std::size_t cancelledWithUid = 0;

    for (int thread = 0; thread < nMaxIOThread; ++thread) {
        for (int slotIndex = 0; slotIndex < MAX_SLOT; ++slotIndex) {
            for (CSlot* slot = m_ReservedSlot[thread][slotIndex]; slot; slot = slot->Next) {
                int retained = 0;
                for (int index = 0; index < slot->nCount; ++index) {
                    if (slot->Object[index] == owner) {
                        ++cancelled;
                        if (slot->m_Uid[index] != 0) {
                            ++cancelledWithUid;
                        }
                        continue;
                    }
                    if (retained != index) {
                        slot->Object[retained] = slot->Object[index];
                        slot->Id[retained] = slot->Id[index];
                        slot->m_Uid[retained] = slot->m_Uid[index];
                    }
                    ++retained;
                }
                slot->nCount = retained;
            }
        }

        TimerQueueType retained;
        TimerQueueType& queue = m_ExtTimer[thread];
        while (!queue.empty()) {
            const CTimerWrapper timer = queue.top();
            queue.pop();
            if (timer.m_pObject == owner) {
                ++cancelled;
                if (timer.m_Uid != 0) {
                    ++cancelledWithUid;
                }
            } else {
                retained.push(timer);
            }
        }
        queue = std::move(retained);
    }

    if (nonzeroUidCount) {
        *nonzeroUidCount = cancelledWithUid;
    }
    return cancelled;
}

#endif

#if defined(L2_BUILD_L2SERVER)
#include "Shared/src/TextRuntime.h"

#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/platform/Runtime.h"

#include <cstdlib>

namespace l2server_smart_id_detail {
[[noreturn]] void FailRegisterInvalidClass(std::uint32_t objectClass, const wchar_t* functionName) {
    GuardHelper failureGuard(L"register_object_crash1", CThreadManager::GetThreadID());
    const std::int32_t signedClass = runtime::Int32FromBits(objectClass);
    // Log.Add(
    //     LOG_LOCAL,
    //     L"[%s][%d] Invalid class[%d]",
    //     kWideSourceFile,
    //     0x6d2,
    //     signedClass)
    // L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"[%s][%d] Invalid class[%d]",
    //     kWideSourceFile,
    //     0x6d2,
    //     signedClass)
    L2_LOG_ERROR_WITH_LOCAL_COPY(
        Log,
        "Smart-ID registration failed in %s: class %d (0x%08x) is outside the supported class range; initiating fatal "
        "INVALID_CLASS report.",
        functionName ? WideToUtf8(functionName).c_str() : "<unknown>", signedClass, static_cast<unsigned>(objectClass)
    );
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Invalid class[%d]",
    //     kWideSourceFile,
    //     0x6d3,
    //     signedClass)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Invalid class[%d]",
    //     kWideSourceFile,
    //     0x6d3,
    //     signedClass)
    // Noise: The preceding ERROR-with-local-copy call keeps both destinations. A second console/GUI error would
    // duplicate the same registration failure.
    ForceCrash(CrashInfo::INVALID_CLASS, functionName, __WFILE__, __LINE__);
    std::abort();
}

[[noreturn]] void FailRegisterInvalidIndex(std::uint32_t index, const wchar_t* functionName) {
    GuardHelper failureGuard(L"register_object_crash2", CThreadManager::GetThreadID());
    const std::int32_t signedIndex = runtime::Int32FromBits(index);
    // Log.Add(
    //     LOG_LOCAL,
    //     L"[%s][%d] Invalid index[%d]",
    //     kWideSourceFile,
    //     0x6db,
    //     signedIndex)
    // L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"[%s][%d] Invalid index[%d]",
    //     kWideSourceFile,
    //     0x6db,
    //     signedIndex)
    L2_LOG_ERROR_WITH_LOCAL_COPY(
        Log,
        "Smart-ID registration failed in %s: index %d (0x%08x) exceeds this manager's capacity; initiating fatal "
        "INVALID_INDEX report.",
        functionName ? WideToUtf8(functionName).c_str() : "<unknown>", signedIndex, static_cast<unsigned>(index)
    );
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Invalid index[%d]",
    //     kWideSourceFile,
    //     0x6dc,
    //     signedIndex)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Invalid index[%d]",
    //     kWideSourceFile,
    //     0x6dc,
    //     signedIndex)
    // Noise: The preceding ERROR-with-local-copy call keeps both destinations. A second console/GUI error would
    // duplicate the same registration failure.
    ForceCrash(CrashInfo::INVALID_INDEX, functionName, __WFILE__, __LINE__);
    std::abort();
}

void ReportNullRegistration() {
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Registering NULL object?",
    //     kWideSourceFile,
    //     0x6e3)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Registering NULL object?",
    //     kWideSourceFile,
    //     0x6e3)
    L2_LOG_ERROR(Log, "Smart-ID registration rejected a null object; returning smart ID 0.");
}

void ReportLookupOutsideIoThread(int threadId) {
    // Log.Add(
    //     LOG_ERROR,
    //     "[%s][0x729] must call GetObject in iothread. thread_id[%d]",
    //     kSourceFile,
    //     threadId)
    // L2_LOG_ERROR(Log,
    //     "[%s][0x729] must call GetObject in iothread. thread_id[%d]",
    //     kSourceFile,
    //     threadId)
    L2_LOG_ERROR(
        Log,
        "Smart-ID lookup rejected worker %d outside the permitted I/O-thread range; reporting the stack and returning no "
        "object.",
        threadId
    );
    runtime::process::ReportCallStack(2, false);
}

void ReportDeleteInvalidIndex(std::uint32_t index, std::uint32_t smartId) {
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Invalid Index[%d], sid[%d]",
    //     kWideSourceFile,
    //     0x756,
    //     runtime::Int32FromBits(index),
    //     runtime::Int32FromBits(smartId))
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Invalid Index[%d], sid[%d]",
    //     kWideSourceFile,
    //     0x756,
    //     runtime::Int32FromBits(index),
    //     runtime::Int32FromBits(smartId))
    L2_LOG_ERROR(
        Log, "Smart-ID removal ignored: index %u for smart ID 0x%08x exceeds this manager's capacity.", index, smartId
    );
}

void ReportReadInvalidIndex(std::uint32_t index) {
    // Log.Add(
    //     LOG_ERROR,
    //     "[%s][0x6a4] invalid index[%u]",
    //     kSourceFile,
    //     index)
    // L2_LOG_ERROR(Log,
    //     "[%s][0x6a4] invalid index[%u]",
    //     kSourceFile,
    //     index)
    L2_LOG_ERROR(
        Log,
        "Smart-ID deferred release ignored: index %u exceeds this manager's capacity; slot and reference left unchanged.",
        index
    );
}

} // namespace l2server_smart_id_detail


#endif // L2_BUILD_L2SERVER

#if defined(L2_BUILD_L2NPC)

namespace thread_safe_detail {

void FailInvalidPipeThread(
    // int) {
    int threadId
) {
    // Current L2NPC.exe 0x0041bd00.
    // Noise: duplicate console calls become one ERROR with the required local-file copy.
    // Log.Add(
    //     LOG_LOCAL,
    //     L"WHAT!!!!!")
    // Log.Add(
    //     LOG_ERROR,
    //     L"WHAT!!!!!")
    // runtime::log::WriteErrorWithLocalCopy(Log,
    //     "[thread pipe] Invalid writer thread %d. Terminating the server.",
    //     threadId)
    L2_LOG_ERROR_WITH_LOCAL_COPY(Log, "[thread pipe] Invalid writer thread %d; terminating the server.", threadId);
    ForceCrash(
        CrashInfo::INVALID_THREAD_NO,
        L"void __cdecl "
        L"CTLPipeBase<unsigned int,120000,8>::"
        L"Write(int,const unsigned int &)",
        __WFILE__, __LINE__
    );
}

void FailPipeOverflow() {
    // Current L2NPC.exe 0x0041ba04.
    // Log.Add(LOG_ERROR,
    //     "[thread pipe] Write capacity exceeded, terminating the server.")
    L2_LOG_ERROR(Log, "[thread pipe] Write capacity exceeded; terminating the server.");
    ForceCrash(
        CrashInfo::THREADPIPE_OVERFLOW,
        L"void __cdecl "
        L"CTLPipeBase<unsigned int,120000,8>::"
        L"CThreadPipe::Write(const unsigned int &)",
        __WFILE__, __LINE__
    );
}

void FailPipeReadOverflow() {
    // Current L2NPC.exe 0x004554bc.
    // Log.Add(LOG_ERROR,
    //     "[thread pipe] Read count exceeded the pipe capacity, terminating the server.")
    L2_LOG_ERROR(Log, "[thread pipe] Read count exceeded the pipe capacity; terminating the server.");
    ForceCrash(
        CrashInfo::PROCEEDED_OBJECT_IS_OVERFLOW,
        L"void __cdecl "
        L"CTLPipeBase<unsigned int,120000,8>::"
        L"CThreadPipe::Read(class "
        L"CTLPipeBase<unsigned int,120000,8>::"
        L"CReadCallback *)",
        __WFILE__, __LINE__
    );
}

void ReportPipeIndexOrder(std::int32_t firstIndex, std::int32_t lastIndex) {
    // Log.Add(
    //     LOG_ERROR,
    //     L"first index is larger than last index. %d > %d",
    //     firstIndex,
    //     lastIndex)
    // Log.Add(LOG_ERROR,
    //     "[thread pipe] Inconsistent queue indices: first %d exceeds last %d.",
    //     firstIndex, lastIndex)
    L2_LOG_ERROR(Log, "[thread pipe] Inconsistent queue indices: first %d exceeds last %d.", firstIndex, lastIndex);
}

void ReportLookupOutsideIoThread(int threadId) {
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] must call GetObject in iothread. "
    //     L"thread_id[%d]",
    //     L"c:\\work\\lineage2build\\server\\main\\shared\\"
    //     L"ThreadSafeLibrary.h",
    //     0x729,
    //     threadId)
    // Log.Add(LOG_ERROR,
    //     "[object registry] Object lookup refused on worker %d: a registered IO worker is required.",
    //     threadId)
    L2_LOG_ERROR(Log, "[object registry] Object lookup refused on worker %d: a registered IO worker is required.", threadId);
    ReportCallStack(LOG_ERROR, false);
}

void ReportSmartIdRegisterInvalidClass(
    std::uint32_t objectClass,
    std::uint32_t smartId,
    // int sourceLine) {
    // int /* sourceLine */) {
    // object identity is diagnostic context only.
    int /* sourceLine */,
    const void* object
) {
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Invalid Class[%d] sid[%x]",
    //     L"c:\\work\\lineage2build\\server\\main\\shared\\"
    //     L"ThreadSafeLibrary.h",
    //     sourceLine,
    //     objectClass,
    //     smartId)
    // Log.Add(LOG_ERROR,
    //     "[object registry] Cannot register object 0x%08x: class %u is unsupported (expected 5 or 9).",
    //     static_cast<unsigned>(smartId), static_cast<unsigned>(objectClass))
    // LoadNPCAI calls Set(0, 0) for these
    // three services and ignores its false registration result. Match the
    // actual global objects, not every caller with an invalid zero ID.
    if (objectClass == 0U && smartId == 0U && object) {
        const char* serviceName = nullptr;
        if (object == static_cast<const CCreature*>(g_TutorialNPC)) {
            serviceName = "tutorial";
        } else if (object == static_cast<const CCreature*>(g_DominionNPC)) {
            serviceName = "dominion";
        } else if (object == static_cast<const CCreature*>(g_FishingNPC)) {
            serviceName = "fishing";
        }
        if (serviceName) {
            L2_LOG_INFO(
                Log,
                "[object registry] Service NPC '%s' uses object ID 0; ordinary creature registration is rejected as "
                "expected. The service is accessed through its global pointer.",
                serviceName
            );
            return;
        }
    }
    L2_LOG_ERROR(
        Log, "[object registry] Cannot register object 0x%08x: class %u is unsupported (expected 5 or 9).",
        static_cast<unsigned>(smartId), static_cast<unsigned>(objectClass)
    );
}

void ReportSmartIdRegisterInvalidIndex(std::uint32_t index, std::uint32_t smartId) {
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Invalid index[%d] sid[%d]",
    //     L"c:\\work\\lineage2build\\server\\main\\shared\\"
    //     L"ThreadSafeLibrary.h",
    //     0x70c,
    //     index,
    //     smartId)
    // Log.Add(LOG_ERROR,
    //     "[object registry] Cannot register object 0x%08x: index %u is out of range.",
    //     static_cast<unsigned>(smartId), static_cast<unsigned>(index))
    L2_LOG_ERROR(
        Log, "[object registry] Cannot register object 0x%08x: index %u is out of range.", static_cast<unsigned>(smartId),
        static_cast<unsigned>(index)
    );
}

void ReportSmartIdDeleteInvalidIndex(std::uint32_t index, std::uint32_t smartId) {
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Invalid Index[%d], sid[%d]",
    //     L"c:\\work\\lineage2build\\server\\main\\shared\\"
    //     L"ThreadSafeLibrary.h",
    //     0x756,
    //     index,
    //     smartId)
    // Log.Add(LOG_ERROR,
    //     "[object registry] Cannot queue deletion of object 0x%08x: index %u is out of range.",
    //     static_cast<unsigned>(smartId), static_cast<unsigned>(index))
    L2_LOG_ERROR(
        Log, "[object registry] Cannot queue deletion of object 0x%08x: index %u is out of range.",
        static_cast<unsigned>(smartId), static_cast<unsigned>(index)
    );
}

void ReportSmartIdReadInvalidIndex(std::uint32_t index) {
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] invalid index[%d]",
    //     L"c:\\work\\lineage2build\\server\\main\\shared\\"
    //     L"ThreadSafeLibrary.h",
    //     0x6a4,
    //     index)
    // Log.Add(LOG_ERROR,
    //     "[object registry] Ignoring garbage-collection entry: object index %u is out of range.",
    //     static_cast<unsigned>(index))
    L2_LOG_ERROR(
        Log, "[object registry] Ignoring garbage-collection entry: object index %u is out of range.",
        static_cast<unsigned>(index)
    );
}

} // namespace thread_safe_detail

namespace {

bool IsCreatureClass(std::uint32_t sid) {
    const std::uint32_t objectClass = sid >> CSmartIdManager<CCreature, 120000, 8>::kClassShift;
    // return objectClass == 5 || objectClass == 9
    return objectClass == SCLS_FAKE_NPC || objectClass == SCLS_CREATURE;
}

} // namespace

CSmartID::CSmartID(CObject* object) : sid_(object ? object->m_nId : 0) {}

CSmartID& CSmartID::operator=(const CSmartID& other) {
    if (this != &other) {
        sid_ = other.sid_;
    }
    return *this;
}

CSmartID& CSmartID::operator=(CObject* object) {
    sid_ = object ? object->m_nId : 0;
    return *this;
}

CCreature* CSmartID::GetCreature() const {
    if (sid_ == 0 || !IsCreatureClass(sid_)) {
        return nullptr;
    }
    return CCreature::GetObjectW(sid_);
}

std::uint32_t CSmartID::GetId() const {
    CObject* object = GetObjectW();
    return object ? object->m_nId : 0;
}

CObject* CSmartID::GetObjectW() const {
    if (sid_ == 0 || !IsCreatureClass(sid_)) {
        return nullptr;
    }
    return CCreature::GetObjectW(sid_);
}

bool CSmartID::IsValid() const { return GetObjectW() != nullptr; }

std::uint32_t CSmartID::Sid() const { return sid_; }

#elif defined(L2_BUILD_L2SERVER)

CDoor* CSmartID::GetDoor() const {
    // Current Shared/ThreadSafeLibrary.cpp member at 0x00aead4c.
    CStaticObject* const object = GetStaticObject();
    if (object == nullptr || !object->IsDoor()) {
        return nullptr;
    }
    return object->CastDoor();
}

template <>
FVector CCreaturePosLogger<CCreature>::LogPosition(MemoryObject* const object) {
    L2_GUARD_FUNCTION();
    if (object == nullptr) {
        return FVector{};
    }
    CCreature* const creature = dynamic_cast<CCreature*>(object);
    return creature != nullptr ? creature->GetPos() : FVector{};
}

template <>
FVector CCreaturePosLogger<CCreatureController>::LogPosition(MemoryObject* const object) {
    L2_GUARD_FUNCTION();
    if (object == nullptr) {
        return FVector{};
    }
    CCreatureController* const controller = dynamic_cast<CCreatureController*>(object);
    if (controller == nullptr) {
        return FVector{};
    }
    CCreature* const actor = controller->GetActor();
    return actor != nullptr ? actor->GetPos() : FVector{};
}

template <>
FVector CCreaturePosLogger<CFishingController>::LogPosition(MemoryObject* const object) {
    L2_GUARD_FUNCTION();
    if (object == nullptr) {
        return FVector{};
    }
    CFishingController* const controller = dynamic_cast<CFishingController*>(object);
    if (controller == nullptr) {
        return FVector{};
    }
    CCreature* const actor = reinterpret_cast<CCreature*>(controller->GetActor());
    return actor != nullptr ? actor->GetPos() : FVector{};
}

#endif
