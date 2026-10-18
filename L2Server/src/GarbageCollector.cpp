#include "GarbageCollector.h"

#include "Shared/src/TextRuntime.h"

#include "Agent.h"
#include "User.h"

#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/ThreadManager.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>

UserObjGC g_UserGC;

namespace {

std::int32_t ReferenceLane(const User& user, ObjectRefType type) noexcept {
    return user.m_RefHelpers.m_RefData[static_cast<std::size_t>(type)].m_nRefCount.load(std::memory_order_relaxed);
}

} // namespace

bool UserObjGC::CheckSizeCorruption(GarbageData<User>& data) {
    // Current L2Server 0x0044f2c8.
    L2_GUARD_FUNCTION();
    // The target compared the four-byte allocator prefix with the fixed
    // Windows extent 0x8cec (object size plus four). The portable allocators
    // keep the requested size in their own header, so compare that with the
    // real User size on this host.
    const std::size_t expected = sizeof(User);
    const std::size_t actual = Memory::RequestedSize(data.m_pObj);
    if (actual != expected) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Memory corrupted, [%d] != [%d]",
        //     static_cast<int>(kUserAllocationExtent),
        //     static_cast<int>(actual))
        // L2_LOG_ERROR(Log,
        //     L"Memory corrupted, [%d] != [%d]",
        //     static_cast<int>(kUserAllocationExtent),
        //     static_cast<int>(actual))
        L2_LOG_ERROR(
            Log, "Player allocation size is corrupted: expected %u bytes, found %u bytes.",
            static_cast<unsigned>(expected), static_cast<unsigned>(actual)
        );
    }
    return actual != expected;
}

void UserObjGC::Initialize() {
    // Current L2Server 0x0044f398.
    L2_GUARD_FUNCTION();
    // Log.Add(LOG_NORMAL, L"[%s] GC Start", L"User")
    // L2_LOG_INFO(Log, L"[%s] GC Start", L"User")
    L2_LOG_INFO(Log, "Initializing player garbage collection.");
    std::fill_n(g_nUserGarbageRefCount, ThreadID::MAX, 0);
    std::fill_n(g_nUserGarbageUserDBID, ThreadID::MAX, 0);
    std::fill_n(g_nUserGarbageEnterTime, ThreadID::MAX, 0);
}

bool UserObjGC::ReleaseResources(GarbageData<User>& data) {
    // Current L2Server 0x0044f478.
    L2_GUARD_FUNCTION();
    User* const user = data.m_pObj;
    const int threadId = CThreadManager::GetThreadID();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Garbage][Resource][fail] garbage info is null "
        //     L"TID[%d][%s][%d]",
        //     threadId,
        //     kGarbageCollectorSource,
        //     0x85)
        // L2_LOG_ERROR(Log,
        //     L"[Garbage][Resource][fail] garbage info is null "
        //     L"TID[%d][%s][%d]",
        //     threadId,
        //     kGarbageCollectorSource,
        //     0x85)
        L2_LOG_ERROR(Log, "Cannot release player resources on worker %d: garbage entry has no player object.", threadId);
        return false;
    }

    if (user->GetSocket() == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Garbage][Resource][fail] Socket is null TID[%d] "
        //     L"DBID[%d] Name[%s] line[%d]",
        //     threadId,
        //     runtime::Int32FromBits(user->GetDBID()),
        //     user->GetName(),
        //     0x92)
        // L2_LOG_ERROR(Log,
        //     L"[Garbage][Resource][fail] Socket is null TID[%d] "
        //     L"DBID[%d] Name[%s] line[%d]",
        //     threadId,
        //     runtime::Int32FromBits(user->GetDBID()),
        //     user->GetName(),
        //     0x92)
        // L2_LOG_ERROR(Log,
        //     "Player resource cleanup on worker %d: DB ID %u ('%s') has no socket. Other cleanup continues.",
        //     threadId,
        //     static_cast<unsigned>(user->GetDBID()),
        //     WideToUtf8(user->GetName()).c_str())
        L2_LOG_ERROR(
            Log, "Player resource cleanup on worker %d: DB ID %u ('%s') has no socket; other cleanup continues.", threadId,
            static_cast<unsigned>(user->GetDBID()), Utf16ToUtf8(user->GetName()).c_str()
        );
    } else {
        user->ReleaseSocket();
    }

    if (user->m_pAgent == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Garbage][Resource][fail] Agent is null TID[%d] "
        //     L"DBID[%d] Name[%s] line[%d]",
        //     threadId,
        //     runtime::Int32FromBits(user->GetDBID()),
        //     user->GetName(),
        //     0x9d)
        // L2_LOG_ERROR(Log,
        //     L"[Garbage][Resource][fail] Agent is null TID[%d] "
        //     L"DBID[%d] Name[%s] line[%d]",
        //     threadId,
        //     runtime::Int32FromBits(user->GetDBID()),
        //     user->GetName(),
        //     0x9d)
        // L2_LOG_ERROR(Log,
        //     "Player resource cleanup on worker %d: DB ID %u ('%s') has no agent. Other cleanup continues.",
        //     threadId,
        //     static_cast<unsigned>(user->GetDBID()),
        //     WideToUtf8(user->GetName()).c_str())
        L2_LOG_ERROR(
            Log, "Player resource cleanup on worker %d: DB ID %u ('%s') has no agent; other cleanup continues.", threadId,
            static_cast<unsigned>(user->GetDBID()), Utf16ToUtf8(user->GetName()).c_str()
        );
    } else {
        L2_RELEASE(user->m_pAgent, ObjectRefType::CreateDelete);
        user->m_pAgent = nullptr;
    }

    std::int32_t cachedReferences = ReferenceLane(*user, ObjectRefType::Cached);
    if (cachedReferences > 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Garbage][Resource][fail] ORT_CACHED REAMIN TID[%d] "
        //     L"DBID[%d] Name[%s] line[%d] refCnt[%d]",
        //     threadId,
        //     runtime::Int32FromBits(user->GetDBID()),
        //     user->GetName(),
        //     0xa4,
        //     cachedReferences)
        // L2_LOG_ERROR(Log,
        //     L"[Garbage][Resource][fail] ORT_CACHED REAMIN TID[%d] "
        //     L"DBID[%d] Name[%s] line[%d] refCnt[%d]",
        //     threadId,
        //     runtime::Int32FromBits(user->GetDBID()),
        //     user->GetName(),
        //     0xa4,
        //     cachedReferences)
        // L2_LOG_ERROR(Log,
        //     "Player DB ID %u ('%s') still has %d cached references on worker %d. Reporting owners and attempting
        //     release.", static_cast<unsigned>(user->GetDBID()), WideToUtf8(user->GetName()).c_str(), cachedReferences,
        //     threadId)
        L2_LOG_ERROR(
            Log,
            "Player DB ID %u ('%s') still has %d cached references on worker %d; reporting owners and attempting release.",
            static_cast<unsigned>(user->GetDBID()), Utf16ToUtf8(user->GetName()).c_str(), cachedReferences, threadId
        );
        user->ReportOrt(nullptr, 0);
    }

    for (;;) {
        cachedReferences = ReferenceLane(*user, ObjectRefType::Cached);
        if (cachedReferences < 1) {
            break;
        }
        const std::int32_t remaining = L2_RELEASE(user, ObjectRefType::Cached);
        if (remaining <= 20) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Garbage][Resource][warning] ORT_CACHED ORT_CACHED "
            //     L"REAMIN TID[%d] DBID[%d] Name[%s] line[%d] "
            //     L"refCnt[%d]",
            //     threadId,
            //     runtime::Int32FromBits(user->GetDBID()),
            //     user->GetName(),
            //     0xad,
            //     ReferenceLane(*user, ObjectRefType::Cached))
            // L2_LOG_ERROR(Log,
            //     L"[Garbage][Resource][warning] ORT_CACHED ORT_CACHED "
            //     L"REAMIN TID[%d] DBID[%d] Name[%s] line[%d] "
            //     L"refCnt[%d]",
            //     threadId,
            //     runtime::Int32FromBits(user->GetDBID()),
            //     user->GetName(),
            //     0xad,
            //     ReferenceLane(*user, ObjectRefType::Cached))
            L2_LOG_ERROR(
                Log,
                "Stopped releasing cached player references on worker %d: total references after release %d, cached "
                "references before that release %d; resource cleanup returns success.",
                threadId, remaining, cachedReferences
            );
            break;
        }
    }
    return true;
}

void UserObjGC::KeepGarbageInfo(GarbageData<User>& data) {
    // Current L2Server 0x0044f740.
    L2_GUARD_FUNCTION();
    if (data.m_pObj == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Garbage]Garbage info refer NULL")
        // L2_LOG_ERROR(Log,
        //     L"[Garbage]Garbage info refer NULL")
        L2_LOG_ERROR(Log, "Cannot save player garbage-collection context: entry has no player object.");
        return;
    }

    const int threadId = CThreadManager::GetThreadID();
    g_nUserGarbageEnterTime[threadId] = runtime::Int32FromBits(data.m_nTime);
    g_nUserGarbageUserDBID[threadId] = runtime::Int32FromBits(data.m_pObj->GetDBID());
    g_nUserGarbageRefCount[threadId] = data.m_pObj->RefCount();
}

void UserObjGC::ClearGarbageInfo() {
    // Current L2Server 0x0044f840.
    L2_GUARD_FUNCTION();
    const int threadId = CThreadManager::GetThreadID();
    g_nUserGarbageEnterTime[threadId] = 0;
    g_nUserGarbageUserDBID[threadId] = 0;
    g_nUserGarbageRefCount[threadId] = 0;
}

bool UserObjGC::ReleaseUser(GarbageData<User>& data) {
    // Current L2Server 0x0044f8f0.
    L2_GUARD_FUNCTION();
    ClearGarbageInfo();

    User* const user = data.m_pObj;
    const int threadId = CThreadManager::GetThreadID();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Garbage][Destroy][fail] User Release fail, object is "
        //     L"null, line[%d]",
        //     0xc1)
        // L2_LOG_ERROR(Log,
        //     L"[Garbage][Destroy][fail] User Release fail, object is "
        //     L"null, line[%d]",
        //     0xc1)
        L2_LOG_ERROR(Log, "Cannot destroy player on worker %d: garbage entry has no player object.", threadId);
        return false;
    }

    if (CheckSizeCorruption(data)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Garbage][Destroy][fail] User memory size corruption!!, "
        //     L"line[%d]",
        //     0xc7)
        // L2_LOG_ERROR(Log,
        //     L"[Garbage][Destroy][fail] User memory size corruption!!, "
        //     L"line[%d]",
        //     0xc7)
        L2_LOG_ERROR(Log, "Player destruction stopped on worker %d because the allocation size is corrupted.", threadId);
        return false;
    }

    KeepGarbageInfo(data);
    if (user->RefCount() == 20) {
        int released = 0;
        for (;;) {
            const std::int32_t remaining = L2_RELEASE(user, ObjectRefType::VerifierRetain);
            if (remaining == 0 && released != 19) {
                break;
            }
            ++released;
            if (released > 19) {
                return true;
            }
        }

        // Log.Add(
        //     LOG_ERROR,
        //     L"[Garbage][Destroy][fail] TID[%d] DBID[%d] line[%d] "
        //     L"remain[%d]",
        //     threadId,
        //     g_nUserGarbageUserDBID[threadId],
        //     0xe5,
        //     released)
        // L2_LOG_ERROR(Log,
        //     L"[Garbage][Destroy][fail] TID[%d] DBID[%d] line[%d] "
        //     L"remain[%d]",
        //     threadId,
        //     g_nUserGarbageUserDBID[threadId],
        //     0xe5,
        //     released)
        L2_LOG_ERROR(
            Log,
            "Player DB ID %u reached zero references after %d of 20 verifier releases on worker %d; destruction reports "
            "failure.",
            static_cast<unsigned>(g_nUserGarbageUserDBID[threadId]), released + 1, threadId
        );
        return false;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[Garbage][Destroy][fail] RefCount Error TID[%d] DBID[%d] "
    //     L"Name[%s] line[%d] refCnt[%d]",
    //     threadId,
    //     runtime::Int32FromBits(user->GetDBID()),
    //     user->GetName(),
    //     0xd2,
    //     user->RefCount())
    // L2_LOG_ERROR(Log,
    //     L"[Garbage][Destroy][fail] RefCount Error TID[%d] DBID[%d] "
    //     L"Name[%s] line[%d] refCnt[%d]",
    //     threadId,
    //     runtime::Int32FromBits(user->GetDBID()),
    //     user->GetName(),
    //     0xd2,
    //     user->RefCount())
    // L2_LOG_ERROR(Log,
    //     "Cannot destroy player DB ID %u ('%s') on worker %d: total reference count %d, expected 20 verifier references.",
    //     static_cast<unsigned>(user->GetDBID()),
    //     WideToUtf8(user->GetName()).c_str(),
    //     threadId,
    //     user->RefCount())
    L2_LOG_ERROR(
        Log,
        "Cannot destroy player DB ID %u ('%s') on worker %d: total reference count %d, expected 20 verifier references.",
        static_cast<unsigned>(user->GetDBID()), Utf16ToUtf8(user->GetName()).c_str(), threadId, user->RefCount()
    );
    if (user->IsActive() || user->IsAlive()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Garbage][Destroy][fail] Alive TID[%d] DBID[%d] "
        //     L"Name[%s] line[%d]",
        //     threadId,
        //     runtime::Int32FromBits(user->GetDBID()),
        //     user->GetName(),
        //     0xd6)
        // L2_LOG_ERROR(Log,
        //     L"[Garbage][Destroy][fail] Alive TID[%d] DBID[%d] "
        //     L"Name[%s] line[%d]",
        //     threadId,
        //     runtime::Int32FromBits(user->GetDBID()),
        //     user->GetName(),
        //     0xd6)
        // L2_LOG_ERROR(Log,
        //     "Player DB ID %u ('%s') on worker %d is still active or alive while destruction is refused.",
        //     static_cast<unsigned>(user->GetDBID()),
        //     WideToUtf8(user->GetName()).c_str(),
        //     threadId)
        L2_LOG_ERROR(
            Log, "Player DB ID %u ('%s') on worker %d is still active or alive while destruction is refused.",
            static_cast<unsigned>(user->GetDBID()), Utf16ToUtf8(user->GetName()).c_str(), threadId
        );
    }
    user->ReportOrt(nullptr, 0);
    return false;
}

void UserObjGC::Insert(User* user) {
    // Current L2Server 0x0044ff00.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Garbage][insert][fail] NULL object")
        // L2_LOG_ERROR(Log,
        //     L"[Garbage][insert][fail] NULL object")
        L2_LOG_ERROR(Log, "Cannot enqueue player garbage collection: player object is null.");
        return;
    }

    {
        L2_GUARD(*user->m_lock);
        if (ReferenceLane(*user, ObjectRefType::VerifierRetain) >= 2) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Garbage][insert][fail] duplicated, dbid[%d], "
            //     L"name[%s]",
            //     runtime::Int32FromBits(user->GetDBID()),
            //     user->GetName())
            // L2_LOG_ERROR(Log,
            //     L"[Garbage][insert][fail] duplicated, dbid[%d], "
            //     L"name[%s]",
            //     runtime::Int32FromBits(user->GetDBID()),
            //     user->GetName())
            // L2_LOG_ERROR(Log,
            //     "Player DB ID %u ('%s') already has at least two verifier references. Duplicate garbage-collection
            //     insertion is skipped.", static_cast<unsigned>(user->GetDBID()), WideToUtf8(user->GetName()).c_str())
            L2_LOG_ERROR(
                Log,
                "Player DB ID %u ('%s') already has at least two verifier references; duplicate garbage-collection "
                "insertion is skipped.",
                static_cast<unsigned>(user->GetDBID()), Utf16ToUtf8(user->GetName()).c_str()
            );
            return;
        }

        for (int reference = 0; reference < 20; ++reference) {
            L2_ADD_REF(user, ObjectRefType::VerifierRetain);
        }
    }

    GarbageData<User> data;
    data.m_pObj = user;
    data.m_nTime = runtime::GetTickCount();
    m_gcMustQueue[static_cast<std::size_t>(CThreadManager::GetThreadID())].push_back(data);
}

void UserObjGC::Collect() {
    // Current L2Server 0x0045012c.
    L2_GUARD_FUNCTION();
    const std::uint32_t now = runtime::GetTickCount();
    const std::size_t threadId = static_cast<std::size_t>(CThreadManager::GetThreadID());

    auto& mustQueue = m_gcMustQueue[threadId];
    while (!mustQueue.empty()) {
        const GarbageData<User> data = mustQueue.front();
        if (now < data.m_nTime + 20000U) {
            break;
        }
        mustQueue.pop_front();
        GarbageData<User> mutableData = data;
        if (ReleaseResources(mutableData)) {
            m_gcDelayQueue[threadId].push_back(data);
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Garbage][Resource][fail] TID[%d] dbid[%d], "
            //     L"name[%s]",
            //     static_cast<int>(threadId),
            //     runtime::Int32FromBits(data.m_pObj->GetDBID()),
            //     data.m_pObj->GetName())
            // L2_LOG_ERROR(Log,
            //     L"[Garbage][Resource][fail] TID[%d] dbid[%d], "
            //     L"name[%s]",
            //     static_cast<int>(threadId),
            //     runtime::Int32FromBits(data.m_pObj->GetDBID()),
            //     data.m_pObj->GetName())
            L2_LOG_ERROR(
                Log,
                "Player resource cleanup failed on worker %zu; the entry was removed from the pending queue and is not "
                "added to the delayed queue.",
                static_cast<std::size_t>(threadId)
            );
        }
    }

    auto& delayQueue = m_gcDelayQueue[threadId];
    for (int released = 0; released < 10; ++released) {
        if (delayQueue.empty()) {
            break;
        }
        const GarbageData<User> data = delayQueue.front();
        if (now < data.m_nTime + 300000U) {
            break;
        }
        delayQueue.pop_front();
        GarbageData<User> mutableData = data;
        if (!ReleaseUser(mutableData)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Garbage][Destroy][fail] TID[%d] DBID[%d]",
            //     static_cast<int>(threadId),
            //     g_nUserGarbageUserDBID[threadId])
            // L2_LOG_ERROR(Log,
            //     L"[Garbage][Destroy][fail] TID[%d] DBID[%d]",
            //     static_cast<int>(threadId),
            //     g_nUserGarbageUserDBID[threadId])
            L2_LOG_ERROR(
                Log,
                "Player destruction failed on worker %zu (saved DB ID %u); the delayed entry was removed and the "
                "unreleased-player counter is incremented.",
                static_cast<std::size_t>(threadId), static_cast<unsigned>(g_nUserGarbageUserDBID[threadId])
            );
            g_ServerContext.NotReleasedUser.fetch_add(1, std::memory_order_seq_cst);
        }
    }
}
