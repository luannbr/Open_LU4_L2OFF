#include "IOC.h"

#include "L2Core/src/inc/FileUtils.h"
#include "Shared/src/Runtime/SourceFile.h"
#include "Shared/src/Runtime/AcceptedSocketTransport.h"
#include "Shared/src/Runtime/IOThreadRuntime.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WaitThreadRuntime.h"
#include "Shared/src/Runtime/Win64Integer.h"
#include "ProcessRuntime.h"
#include "Runtime.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"
#include "net/Config.h"

#if defined(L2_BUILD_L2NPC) || defined(L2_BUILD_CACHED)
#include "L2Core/src/inc/Guard.h"
#include "L2Core/src/inc/GuardProfile.h"
#include "Shared/src/Exception.h"
#include "Shared/src/LockStatusStack.h"
#include "Shared/src/Log.h"
#endif

#if defined(L2_BUILD_CACHED)
#include "Shared/src/GIP.h"
#endif

#if defined(L2_BUILD_L2NPC)
#include "L2NPC/src/Config.h"
#include "L2NPC/src/NpcMakerDB.h"
#include "Portable/NPCServerConfiguration.h"
#include "Portable/NPCServerRuntime.h"
#include "Shared/src/TimeAnalysis.h"
#elif !defined(L2_BUILD_CACHED)
#error "Shared/ioc.cpp must be compiled for a concrete server process"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <limits>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <WinSock2.h>
#include <Windows.h>
#else
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#if defined(L2_BUILD_CACHED)
void AddLog(char* format, ...) {
    // Current Cached.exe 0x004e5974. Despite its name this function only
    // formats the message into a 0x200-byte stack buffer and discards it.
    // Routing these calls through CLog changes the shipping diagnostics.
    L2_GUARD_FUNCTION();
    char formatted[0x200]{};
    va_list arguments;
    va_start(arguments, format);
#if defined(_MSC_VER)
    ::vsprintf_s(formatted, std::size(formatted), format ? format : "", arguments);
#else
    std::vsnprintf(formatted, std::size(formatted), format ? format : "", arguments);
#endif
    va_end(arguments);
}
#endif

int GetIOThreadCount() {
#if defined(_WIN32)
    SYSTEM_INFO systemInfo;
    ::GetSystemInfo(&systemInfo);
    const unsigned processors = systemInfo.dwNumberOfProcessors;
#else
    const unsigned processors = std::thread::hardware_concurrency();
#endif
#if defined(L2_BUILD_CACHED) || defined(L2_BUILD_L2SERVER)
    // Current Cached.exe 0x004e4ba0 and current L2Server.exe 0x007cb144:
    // all logical processors, capped at 16.
    return static_cast<int>(std::min(16U, processors));
#else
    // Current L2NPC.exe 0x00454b70: half the logical processors, capped at
    // the compile-time maximum of eight I/O workers.
    return static_cast<int>(std::min(8U, processors / 2U));
#endif
}


std::int32_t g_nTotalOutgoingPacketCount = 0;

#if defined(L2_BUILD_CACHED)
std::atomic<std::int64_t> g_nSocketSendBytes{0};
std::atomic<std::int64_t> g_nSocketReceiveBytes{0};
std::atomic<std::int64_t> g_nSocketSendCount{0};
std::atomic<std::int64_t> g_nSocketReceiveCount{0};
std::atomic<std::int64_t> g_nSocketSendBytesOver7K{0};
std::atomic<std::int64_t> g_nSocketReceiveBytesOver7K{0};
std::atomic<std::int64_t> g_nSocketSendCountOver7K{0};
std::atomic<std::int64_t> g_nSocketReceiveCountOver7K{0};
#endif

#if defined(L2_BUILD_L2NPC)
CIOSyncDeadlockChecker<8> g_IOSyncDeadlockChecker;
#endif

namespace {

#if defined(L2_BUILD_CACHED)
// Portable replacement for Cached.exe's two kernel completion-port handles.
// The second target queue is represented when CacheD worker startup is
// composed. All current socket/timer posts in the image target the first
// queue (DAT_01c6b880).
IOThreadRuntime g_cacheDIOThreadRuntime;
#endif

#if defined(L2_BUILD_L2NPC)
std::int64_t SignedTickDifference(std::uint64_t current, std::uint64_t previous) noexcept {
    const std::uint64_t difference = current - previous;
    std::int64_t result = 0;
    static_assert(sizeof(result) == sizeof(difference));
    std::memcpy(&result, &difference, sizeof(result));
    return result;
}

[[noreturn]] void TargetDebugBreakLoop() noexcept {
    // Current L2NPC.exe 0x00456f10 falls through from IOThread_common into
    // consecutive INT3 padding bytes. There is no guard-stack
    // unwind or ordinary return after that point.
#if defined(_WIN32)
    for (;;) {
        ::DebugBreak();
    }
#else
    for (;;) {
        std::raise(SIGTRAP);
    }
#endif
}
#endif

#if defined(L2_BUILD_L2NPC) || defined(L2_BUILD_CACHED)
void CrashInvalidIOLockStack(const wchar_t* guardName, const std::source_location location) {
    GuardHelper guard(guardName, CThreadManager::GetThreadID());
    ForceCrash(CrashInfo::LOCK_STACK_IS_NOT_EMPTY, location);
}

void CheckIOLockStack(const wchar_t* guardName, const std::source_location location = std::source_location::current()) {
    const int threadId = CThreadManager::GetThreadID();
    if (!g_LockStack[static_cast<std::size_t>(threadId)].IsEmpty()) {
        CrashInvalidIOLockStack(guardName, location);
    }
}
#endif

constexpr std::uint64_t kInvalidNativeSocket = UINT64_C(0xffffffffffffffff);

void CloseNativeSocket(std::uint64_t socket) {
#if defined(_WIN32)
    ::closesocket(static_cast<SOCKET>(socket));
#else
    ::close(static_cast<int>(socket));
#endif
}

#if !defined(_WIN32)
int DescriptorFromWaitHandle(void* handle) {
    const std::uintptr_t encoded = reinterpret_cast<std::uintptr_t>(handle);
    if (encoded == 0 || encoded - 1 > static_cast<std::uintptr_t>(std::numeric_limits<int>::max())) {
        return -1;
    }
    return static_cast<int>(encoded - 1);
}

std::optional<std::size_t> WaitForNativeSocket(
    void* const* handles, std::size_t handleCount, std::chrono::milliseconds timeout
) {
    std::vector<pollfd> descriptors;
    descriptors.reserve(handleCount);
    for (std::size_t index = 0; index < handleCount; ++index) {
        descriptors.push_back({DescriptorFromWaitHandle(handles[index]), static_cast<short>(POLLIN | POLLPRI), 0});
    }
    if (descriptors.empty()) {
        return std::nullopt;
    }

    const auto boundedTimeout =
        std::min<std::int64_t>(std::max<std::int64_t>(timeout.count(), 0), std::numeric_limits<int>::max());
    const int result = ::poll(descriptors.data(), descriptors.size(), static_cast<int>(boundedTimeout));
    if (result <= 0) {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < descriptors.size(); ++index) {
        if ((descriptors[index].revents & (POLLIN | POLLPRI)) != 0) {
            return index;
        }
    }
    return std::nullopt;
}
#endif

struct CIOTimer {
    CIOObject* m_pObject = nullptr;
    std::uint32_t m_dwTime = 0;
    int m_id = 0;
    std::int64_t m_Uid = 0;

    bool operator<(const CIOTimer& other) const noexcept {
        // PDB: CIOTimer::operator<(const CIOTimer&). Reversing only the due
        // tick makes std::less expose the earliest timer at queue.top().
        return m_dwTime > other.m_dwTime;
    }
};


using TimerQueue = runtime::PriorityQueue<CIOTimer, std::vector<CIOTimer>, std::less<CIOTimer>>;


#if defined(L2_BUILD_CACHED)
constexpr std::size_t kTimerQueueCount = 22;
#else
constexpr std::size_t kTimerQueueCount = ThreadID::MAX;
#endif
TimerQueue g_timerQueue[kTimerQueueCount];

constexpr std::size_t kMaximumWaitObjects = 64;
#if defined(L2_BUILD_CACHED)
// Current Cached.exe 0x004e6e6c increments this function-static diagnostic
// counter before clamping sub-80ms timers. Relaxed atomic RMW is the nearest
// defined portable equivalent of the target's unsynchronised aligned dword.
std::atomic<int> g_shortTimerWarningCount{0};
#endif
CIOCriticalSection g_globalLockTimer;
std::vector<void*> g_vHandle;
std::vector<CIOObject*> g_vObject;


TimerQueue* CurrentTimerQueue() {
    const int threadId = CThreadManager::GetThreadID();
    return &g_timerQueue[static_cast<std::size_t>(threadId)];
}

void DispatchCurrentTimerQueueAt(std::uint32_t now) {
    TimerQueue* queue = CurrentTimerQueue();
    if (!queue) {
        return;
    }

    for (;;) {
        if (queue->empty() || now < queue->top().m_dwTime) {
            break;
        }
        const CIOTimer queued = queue->top();
        queue->pop();

        g_ObjectInTimer.fetch_sub(1, std::memory_order_seq_cst);

        // Current L2NPC.exe 0x00456510. LISTEN owns queue 11 and forwards
        // its expiry through virtual OnTimerCallback (+0x40) to IOCP. Every
        // other logical thread enters virtual OnIOCallback (+0x50) directly.
        // Both paths consume the ORT_TIMER reference in OnIOCallback (or in
        // OnTimerCallback's failed-post branch), exactly as the target does.
        const bool listenThread =
#if defined(L2_BUILD_CACHED)
            CThreadManager::GetThreadID() == 21;
#else
            CThreadManager::IsListenThread();
#endif
        if (listenThread) {
            queued.m_pObject->OnTimerCallback(queued.m_id, queued.m_Uid);
        } else {
            PortableOverlapped* overlapped =
                reinterpret_cast<PortableOverlapped*>(static_cast<std::uintptr_t>(queued.m_Uid));
            queued.m_pObject->OnIOCallback(1, static_cast<std::uint32_t>(queued.m_id), overlapped);
        }
    }
}

} // namespace

#if defined(L2_BUILD_CACHED)
void portable::StopCacheDIOThreadRuntime() {
    // The shipping process never returns from WaitThread. The portable host
    // does, so cancel accepted WSARecv-equivalent operations while their
    // completion queue is still installed and can release every IO keep.
    StopAcceptedSocketTransports();
    g_cacheDIOThreadRuntime.Stop();
    g_cacheDIOThreadRuntime.Uninstall();
}
#endif

void InitIOThread() {
    L2_GUARD_FUNCTION();
    g_bTerminating = 0;
    // IOThreadRuntime replaces the target's unnamed event/completion-port
    // kernel transport. Process-specific queue structure remains selected by
    // the same physical Shared source.
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004e5108 creates an event and two completion
    // ports. All recovered socket/timer producers post to its first port.
    g_cacheDIOThreadRuntime.Install();
#else
    // Current L2NPC.exe 0x00455978 creates one completion port.
    l2npc::portable::InitializeIOThreadTransport();
#endif
    InitializeIOBufferPool();
}

void InitializeIOBufferPool() {
    L2_GUARD_FUNCTION();

#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004e4fb8 reads the fixed .\\l2cache.ini profile
    // with a default of 0x10000. Config::Load(false) keeps the profile
    // API's silent missing-file/default behavior.
    runtime::Config cacheConfiguration;
    cacheConfiguration.Load("./l2cache.ini", false);
    const int totalBufferCount = cacheConfiguration.GetInt("Setting", "IOBufferCount", 0x10000);
#else
    // Current L2NPC.exe 0x00455828 reads [Setting]/IOBufferCount from the
    // fixed .\\l2npc.ini profile. The portable composition root binds that
    // same profile view, including an optional deployment-path override.
    const runtime::Config* const configuration = l2npc::portable::GetNPCServerConfiguration();
    const int totalBufferCount =
        configuration && configuration->Loaded() ? configuration->GetInt("Setting", "IOBufferCount", 0x800) : 0x800;
#endif
    const int threadCount = GetIOThreadCount();
    // The shipping body executes IDIV before testing whether the loop has a
    // positive bound. A zero worker count therefore keeps its exception.
    const int buffersPerThread = runtime::Win64SignedDivide(totalBufferCount, threadCount);
    if (threadCount <= 0) {
        return;
    }

    for (int threadId = 0; threadId < threadCount; ++threadId) {
        g_IOBufferPools[threadId] = new (std::nothrow)
#if defined(L2_BUILD_L2SERVER)
            CNewMwsrQueue<DefaultIOBuffer>(buffersPerThread);
#else
            CMwsrQueue<DefaultIOBuffer>(buffersPerThread);
#endif
        g_IOBuffer8kPools[threadId] = new (std::nothrow)
#if defined(L2_BUILD_L2SERVER)
            CNewMwsrQueue<IOBuffer8k>(0x200);
#else
            CMwsrQueue<IOBuffer8k>(0x200);
#endif
        g_IOBuffer32kPools[threadId] = new (std::nothrow)
#if defined(L2_BUILD_L2SERVER)
            CNewMwsrQueue<IOBuffer32k>(0x80);
#else
            CMwsrQueue<IOBuffer32k>(0x80);
#endif
    }
}

#if defined(L2_BUILD_L2NPC)

template <std::size_t ThreadCount>
void CIOSyncDeadlockChecker<ThreadCount>::CheckForDeadlock() {
    const std::uint64_t currentTick = runtime::GetTickCount();
    for (std::size_t threadId = 0; threadId < ThreadCount; ++threadId) {
        const std::uint64_t lastTick = m_lastTickArr[threadId].tick64.fetch_add(0, std::memory_order_seq_cst);
        const std::int64_t difference = SignedTickDifference(currentTick, lastTick);
        if (lastTick == 0 || difference <= 20000) {
            continue;
        }

        m_lastTickArr[threadId].tick64.exchange(0, std::memory_order_seq_cst);
        const auto seconds = static_cast<unsigned>(difference / 1000U);
        // constexpr const wchar_t* format =
        //     L"!!! [IOSyncDeadlock] ThreadID(%d), "
        //     L"deadlock or super-lag detected!!!  "
        //     L"(diff %u sec) ".
        // Log.Add(
        //     LOG_LOCAL,
        //     format,
        //     static_cast<int>(threadId),
        //     seconds)
        // Log.Add(
        //     LOG_LOCAL,
        //     "[watchdog] IO worker %d has not refreshed its synchronization heartbeat for %u seconds (last tick %llu ms,
        //     current tick %llu ms), capturing call and lock stacks.", static_cast<int>(threadId), seconds,
        //     static_cast<unsigned long long>(lastTick), static_cast<unsigned long long>(currentTick))
        // runtime::log::WriteErrorWithLocalCopy(
        //     Log,
        //     "[watchdog] IO worker %d has not refreshed its synchronization heartbeat for %u seconds (last tick %llu ms,
        //     current tick %llu ms), capturing call and lock stacks.", static_cast<int>(threadId), seconds,
        //     static_cast<unsigned long long>(lastTick), static_cast<unsigned long long>(currentTick))
        L2_LOG_ERROR_WITH_LOCAL_COPY(
            Log,
            "[watchdog] IO worker %d has not refreshed its synchronization heartbeat for %u seconds (last tick %llu ms, "
            "current tick %llu ms); capturing call and lock stacks.",
            static_cast<int>(threadId), seconds, static_cast<unsigned long long>(lastTick),
            static_cast<unsigned long long>(currentTick)
        );
        // Log.Add(
        //     LOG_ERROR,
        //     format,
        //     static_cast<int>(threadId),
        //     seconds)
        // Noise: WriteErrorWithLocalCopy above already emits this error once and keeps both destinations.
        // Log.Add(
        //     LOG_ERROR,
        //     "[watchdog] IO worker %d has not refreshed its synchronization heartbeat for %u seconds (last tick %llu ms,
        //     current tick %llu ms), capturing call and lock stacks.", static_cast<int>(threadId), seconds,
        //     static_cast<unsigned long long>(lastTick), static_cast<unsigned long long>(currentTick))
        ReportCallStack(static_cast<int>(threadId), LOG_ERROR, false);
        ReportLockStack(static_cast<int>(threadId), L"Deadlock", LOG_ERROR);
    }
}

template <std::size_t ThreadCount>
void CIOSyncDeadlockChecker<ThreadCount>::UpdateCurrentThread() {
    const int threadId = CThreadManager::GetThreadID();
    if (threadId < 0 || threadId >= static_cast<int>(ThreadCount)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[IOSyncDeadlock] nThreadID over "
        //     L"array index[%d]",
        //     threadId)
        // Log.Add(
        //     LOG_ERROR,
        //     "[watchdog] Cannot update the IO synchronization heartbeat: worker index %d is outside the range 0..%llu. This
        //     update is skipped.", threadId, static_cast<unsigned long long>(ThreadCount - 1))
        L2_LOG_ERROR(
            Log,
            "[watchdog] Cannot update the IO synchronization heartbeat: worker index %d is outside the range 0..%llu; this "
            "update is skipped.",
            threadId, static_cast<unsigned long long>(ThreadCount - 1)
        );
        return;
    }
    m_lastTickArr[static_cast<std::size_t>(threadId)].tick64.exchange(runtime::GetTickCount(), std::memory_order_seq_cst);
}

template class CIOSyncDeadlockChecker<8>;

std::uint32_t WaitThread(void*) {
    // Current L2NPC.exe 0x004566dc snapshots whether g_vHandle was empty
    // before entering the loop. RegisterWait is normally called before this
    // worker starts. Later first-time registrations are not
    // observed, matching that branch invariant.
    Singleton<CIODeadlockChecker>::inst().Init();

    const portable::WaitThreadRuntimeServices runtime = portable::GetWaitThreadRuntimeServices();
    // Current L2NPC.exe 0x004566f7 snapshots only the initial byte distance
    // between g_vHandle's begin/end pointers. Later iterations still read
    // the live xvector pointers and sizes without taking g_globalLockTimer.
    const bool hasRegisteredWaits = !g_vHandle.empty();
    for (;;) {
        GuardProfileHelper::InitializeGuardProfilePerThread();
        const int threadId = CThreadManager::GetThreadID();
        g_dwGuardStartTick[threadId] = runtime::GetTickCount();
        L2_GUARD_FUNCTION();
        const std::uint32_t iterationStart = runtime::GetTickCount();
        g_IOSyncDeadlockChecker.CheckForDeadlock();
        Singleton<CIODeadlockChecker>::inst().TestDeadlock();
        Singleton<CIODeadlockChecker>::inst().Test49Days();
        g_Timer.TurnOn();
        CIOObject::TimerDispatch();
        g_Timer.TurnOff();

        const std::uint32_t afterDispatch = runtime::GetTickCount();
        const std::uint32_t untilTenSeconds = iterationStart + 10000U - afterDispatch;
        const std::uint32_t waitMilliseconds = untilTenSeconds < 100U ? iterationStart + 10000U - runtime::GetTickCount() : 100U;

        const std::chrono::milliseconds timeout(waitMilliseconds);
        bool portableStopSignalled = false;
        if (hasRegisteredWaits) {
            std::optional<std::size_t> signalled;
#if defined(_WIN32)
            const DWORD result = ::WaitForMultipleObjects(
                static_cast<DWORD>(g_vHandle.size()), reinterpret_cast<const HANDLE*>(g_vHandle.data()), FALSE,
                waitMilliseconds
            );
            if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + g_vHandle.size()) {
                signalled = static_cast<std::size_t>(result - WAIT_OBJECT_0);
            }
#else
            if (runtime.waitForObjects) {
                signalled = runtime.waitForObjects(g_vHandle.data(), g_vHandle.size(), timeout);
            } else {
                signalled = WaitForNativeSocket(g_vHandle.data(), g_vHandle.size(), timeout);
            }
#endif
            if (signalled) {
                runtime::CheckedVectorAt(g_vObject, *signalled)->OnWaitCallback();
            }
        } else if (runtime.waitForStop) {
            portableStopSignalled = runtime.waitForStop(timeout);
        } else {
            std::this_thread::sleep_for(timeout);
        }

        // Current L2NPC.exe 0x0045687c. The IO termination flag is not the
        // exception subsystem's recursion flag: the shipping LISTEN thread
        // treats it as an unexpected IO-thread closure and crashes with the
        // dedicated reason after the current wait has completed.
        if (g_bTerminating != 0) {
            ForceCrash(CrashInfo::CLOSE_IO_THREAD);
        }

        // Cooperative process teardown is a host transport boundary, not a
        // shipping WaitThread branch. Observe it only after the complete
        // target iteration (including the CLOSE_IO_THREAD crash gate), so it
        // cannot suppress watchdog, timer, wait-callback or crash behavior.
        if (portableStopSignalled || (runtime.stopRequested && runtime.stopRequested())) {
            return 0;
        }
    }
}

IOThreadCallback::IOThreadCallback(int threadId) noexcept : m_Thread(threadId) {}

void IOThreadCallback::IOThreadNextSyncTime() {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x004568e8.
    g_Timer.TurnOn();
    CIOObject::TimerDispatch();
    IOThreadNextSyncTimeCallback();
    g_Timer.TurnOff();
}

void IOThreadCallback::IOThread_common() {
    // Current L2NPC.exe 0x004569a8. IOThreadRuntime replaces only the
    // blocking GetQueuedCompletionStatus transport. This method keeps the
    // target deadline, profiling, timer, lock-stack, and dispatch logic.
    L2_GUARD_FUNCTION();
    std::uint32_t nextSyncTime = runtime::GetTickCount();
    InitializeRandomNumber();
    if (g_bFastEnterworld) {
        l2npc::g_NpcMakerDB.FastCreateAllNPC(CThreadManager::GetThreadID());
    }

    for (;;) {
        GuardProfileHelper::InitializeGuardProfilePerThread();
        const int threadId = CThreadManager::GetThreadID();
        g_dwGuardStartTick[threadId] = runtime::GetTickCount();
        GuardHelper iteration(L"void IOThread_common 1", threadId);

        const std::uint32_t now = runtime::GetTickCount();
        std::uint32_t waitMilliseconds;
        if (nextSyncTime < now) {
            IOThreadNextSyncTime();
            nextSyncTime = runtime::GetTickCount() + 50U;
            waitMilliseconds = 50U;
        } else {
            waitMilliseconds = nextSyncTime - now;
        }

        IOThreadRuntime::QueueEntry entry;
        ::g_nRunningThread.fetch_add(1, std::memory_order_seq_cst);
        const IOThreadRuntime::BoundWaitResult waitResult = IOThreadRuntime::WaitBoundCompletion(waitMilliseconds, entry);
        ::g_nRunningThread.fetch_sub(1, std::memory_order_seq_cst);

        // The target turns this timer on after every completion-port wait,
        // including a timeout. Only an actual callback turns it off.
        g_Timer.TurnOn();
        if (waitResult == IOThreadRuntime::BoundWaitResult::Stop) {
            // Cooperative teardown is the sole portable return path. Flush
            // the thread pipe once, as the previous backend loop did.
            IOThreadNextSyncTime();
            return;
        }
        if (waitResult == IOThreadRuntime::BoundWaitResult::Timeout) {
            continue;
        }

        CheckIOLockStack(L"Invalid LockStack before io!!!");
        if (entry.object) {
            // GetQueuedCompletionStatus leaves the failed operation's error
            // in the worker's WinSock/last-error slot before the target
            // callback reads it.
            if (!entry.success && entry.socketError != 0) {
                SetSocketLastError(entry.socketError);
            }
            entry.object->OnIOCallback(entry.success, entry.transferred, entry.overlapped);
        }
        g_Timer.TurnOff();
        CheckIOLockStack(L"Invalid LockStack after io!!!");
    }
}

void IOThreadCallback::IOThread_mailing() {
    // Current L2NPC.exe 0x00456e44. The shipping body uses SEH around the
    // process-lifetime common loop. A normal return exists only for portable
    // cooperative shutdown.
    try {
        IOThread_common();
    } catch (...) {
        ELOG(L"*%S:%d(%S) exception\n", L2_SOURCE_FILE, __LINE__, __TIMESTAMP__);
        Log.FlushLogFile();
        send_exception(true);
    }
}

void IOThreadCallback::IOThread_debug() {
    // Current EXE 0x00456e88 enters IOThread_common and reaches INT3 padding
    // only if that shipping infinite loop returns.
    L2_GUARD_FUNCTION();
    IOThread_common();
    TargetDebugBreakLoop();
}

#endif // L2_BUILD_L2NPC

#if defined(L2_BUILD_CACHED)

std::uint32_t WaitThread(void*) {
    // Current Cached.exe 0x004e678c. Unlike the L2NPC creation this
    // loop has no CIOSyncDeadlockChecker<8> pass and does not bracket timer
    // dispatch with g_Timer. It runs only the shared deadlock/49-day checker.
    Singleton<CIODeadlockChecker>::inst().Init();

    const portable::WaitThreadRuntimeServices runtime = portable::GetWaitThreadRuntimeServices();
    // The target snapshots only whether the initial handle-vector byte span
    // is zero. A non-empty snapshot still uses the live count each iteration.
    const bool hasRegisteredWaits = !g_vHandle.empty();
    for (;;) {
        GuardProfileHelper::InitializeGuardProfilePerThread();
        const int threadId = CThreadManager::GetThreadID();
        g_dwGuardStartTick[threadId] = runtime::GetTickCount();
        L2_GUARD_FUNCTION();

        const std::uint32_t iterationStart = runtime::GetTickCount();
        Singleton<CIODeadlockChecker>::inst().TestDeadlock();
        Singleton<CIODeadlockChecker>::inst().Test49Days();
        CIOObject::TimerDispatch();

        const std::uint32_t afterDispatch = runtime::GetTickCount();
        const std::uint32_t untilTenSeconds = iterationStart + 10000U - afterDispatch;
        const std::uint32_t waitMilliseconds = untilTenSeconds < 100U ? iterationStart + 10000U - runtime::GetTickCount() : 100U;

        const std::chrono::milliseconds timeout(waitMilliseconds);
        bool portableStopSignalled = false;
        if (hasRegisteredWaits) {
            std::optional<std::size_t> signalled;
#if defined(_WIN32)
            const DWORD result = ::WaitForMultipleObjects(
                static_cast<DWORD>(g_vHandle.size()), reinterpret_cast<const HANDLE*>(g_vHandle.data()), FALSE,
                waitMilliseconds
            );
            if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + g_vHandle.size()) {
                signalled = static_cast<std::size_t>(result - WAIT_OBJECT_0);
            }
#else
            if (runtime.waitForObjects) {
                signalled = runtime.waitForObjects(g_vHandle.data(), g_vHandle.size(), timeout);
            } else {
                signalled = WaitForNativeSocket(g_vHandle.data(), g_vHandle.size(), timeout);
            }
#endif
            if (signalled) {
                runtime::CheckedVectorAt(g_vObject, *signalled)->OnWaitCallback();
            }
        } else if (runtime.waitForStop) {
            portableStopSignalled = runtime.waitForStop(timeout);
        } else {
            std::this_thread::sleep_for(timeout);
        }

        if (::g_bTerminating != 0) {
            ForceCrash(CrashInfo::CLOSE_IO_THREAD);
        }

        // This is the sole portable process-lifetime escape. It is observed
        // after the complete shipping iteration and therefore cannot bypass
        // timer, wait-callback or crash behavior.
        if (portableStopSignalled || (runtime.stopRequested && runtime.stopRequested())) {
            return 0;
        }
    }
}

#endif // L2_BUILD_CACHED

CIOServer::CIOServer() : m_hSocket(kInvalidNativeSocket), m_hAcceptEvent(nullptr) { L2_GUARD_FUNCTION(); }

CIOServer::~CIOServer() {
    L2_GUARD_FUNCTION();
    Close();
}

int CIOServer::Create(int port) {
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    const SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    m_hSocket = static_cast<std::uint64_t>(listener);
#else
    const int listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    m_hSocket = listener < 0 ? kInvalidNativeSocket : static_cast<std::uint64_t>(listener);
#endif
    if (m_hSocket == kInvalidNativeSocket) {
#if defined(L2_BUILD_CACHED)
        // ::AddLog(
        //     const_cast<char*>("socket error %d"),
        //     GetSocketLastError())
        // Log.Add(
        //     LOG_ERROR,
        //     "[listener] Cannot create a TCP listener socket on 0.0.0.0:%u (socket error %d). Listener startup failed.",
        //     static_cast<unsigned>(static_cast<std::uint16_t>(port)), GetSocketLastError())
        L2_LOG_ERROR(
            Log, "[listener] Cannot create a TCP listener socket on 0.0.0.0:%u (socket error %d); listener startup failed.",
            static_cast<unsigned>(static_cast<std::uint16_t>(port)), GetSocketLastError()
        );
#else
        // LogIOError(
        //     "socket error %d",
        //     GetSocketLastError())
        // Log.Add(
        //     LOG_ERROR,
        //     "[listener] Cannot create a TCP listener socket on 0.0.0.0:%u (socket error %d). Listener startup failed.",
        //     static_cast<unsigned>(static_cast<std::uint16_t>(port)), GetSocketLastError())
        L2_LOG_ERROR(
            Log, "[listener] Cannot create a TCP listener socket on 0.0.0.0:%u (socket error %d); listener startup failed.",
            static_cast<unsigned>(static_cast<std::uint16_t>(port)), GetSocketLastError()
        );
#endif
        return 0;
    }

#if !defined(_WIN32)
    // The shipping WinSock listener binds again immediately after a clean
    // process restart (current Cached.exe CIOServer::Create. The older C1
    // Shared/CIOServer.cpp follows the same socket/bind/listen sequence).
    // POSIX keeps completed connections in TIME_WAIT and requires this
    // transport option to keep that restart behavior. Keep it outside
    // the recovered Windows path: this is not a target-side socket option.
    const int reuseAddress = 1;
    ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuseAddress, sizeof(reuseAddress));
#endif

    ::sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<std::uint16_t>(port));

#if defined(_WIN32)
    const int bindResult =
        ::bind(static_cast<SOCKET>(m_hSocket), reinterpret_cast<const sockaddr*>(&address), sizeof(address));
#else
    const int bindResult = ::bind(static_cast<int>(m_hSocket), reinterpret_cast<const sockaddr*>(&address), sizeof(address));
#endif
    if (bindResult != 0) {
#if defined(L2_BUILD_CACHED)
        // ::AddLog(
        //     const_cast<char*>("bind error %d"),
        //     GetSocketLastError())
        // Log.Add(
        //     LOG_ERROR,
        //     "[listener] Cannot bind the TCP listener on 0.0.0.0:%u (socket error %d). Closing the listener socket.",
        //     static_cast<unsigned>(static_cast<std::uint16_t>(port)), GetSocketLastError())
        L2_LOG_ERROR(
            Log, "[listener] Cannot bind the TCP listener on 0.0.0.0:%u (socket error %d); closing the listener socket.",
            static_cast<unsigned>(static_cast<std::uint16_t>(port)), GetSocketLastError()
        );
#else
        // LogIOError(
        //     "bind error %d",
        //     GetSocketLastError())
        // Log.Add(
        //     LOG_ERROR,
        //     "[listener] Cannot bind the TCP listener on 0.0.0.0:%u (socket error %d). Closing the listener socket.",
        //     static_cast<unsigned>(static_cast<std::uint16_t>(port)), GetSocketLastError())
        L2_LOG_ERROR(
            Log, "[listener] Cannot bind the TCP listener on 0.0.0.0:%u (socket error %d); closing the listener socket.",
            static_cast<unsigned>(static_cast<std::uint16_t>(port)), GetSocketLastError()
        );
#endif
        Close();
        return 0;
    }

#if defined(_WIN32)
    const int listenResult = ::listen(static_cast<SOCKET>(m_hSocket), 5);
#else
    const int listenResult = ::listen(static_cast<int>(m_hSocket), 5);
#endif
    if (listenResult != 0) {
#if defined(L2_BUILD_CACHED)
        // ::AddLog(
        //     const_cast<char*>("listen error %d"),
        //     GetSocketLastError())
        // Log.Add(
        //     LOG_ERROR,
        //     "[listener] Cannot listen on the TCP socket on 0.0.0.0:%u (socket error %d). Closing the listener socket.",
        //     static_cast<unsigned>(static_cast<std::uint16_t>(port)), GetSocketLastError())
        L2_LOG_ERROR(
            Log, "[listener] Cannot listen on the TCP socket on 0.0.0.0:%u (socket error %d); closing the listener socket.",
            static_cast<unsigned>(static_cast<std::uint16_t>(port)), GetSocketLastError()
        );
#else
        // LogIOError(
        //     "listen error %d",
        //     GetSocketLastError())
        // Log.Add(
        //     LOG_ERROR,
        //     "[listener] Cannot listen on the TCP socket on 0.0.0.0:%u (socket error %d). Closing the listener socket.",
        //     static_cast<unsigned>(static_cast<std::uint16_t>(port)), GetSocketLastError())
        L2_LOG_ERROR(
            Log, "[listener] Cannot listen on the TCP socket on 0.0.0.0:%u (socket error %d); closing the listener socket.",
            static_cast<unsigned>(static_cast<std::uint16_t>(port)), GetSocketLastError()
        );
#endif
        Close();
        return 0;
    }

#if defined(_WIN32)
    m_hAcceptEvent = ::WSACreateEvent();
    ::WSAEventSelect(static_cast<SOCKET>(m_hSocket), static_cast<WSAEVENT>(m_hAcceptEvent), FD_ACCEPT);
#else
    const int descriptor = static_cast<int>(m_hSocket);
    const int flags = ::fcntl(descriptor, F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(descriptor, F_SETFL, flags | O_NONBLOCK);
    }
    m_hAcceptEvent = reinterpret_cast<void*>(static_cast<std::uintptr_t>(descriptor) + 1U);
#endif

    if (!RegisterWait(m_hAcceptEvent)) {
#if defined(L2_BUILD_CACHED)
        // ::AddLog(
        //     const_cast<char*>(
        //         "RegisterWait error on port %d"),
        //     port)
        // Log.Add(
        //     LOG_ERROR,
        //     "[listener] Cannot register the accept wait for TCP port %u. Closing the listener socket and stopping
        //     startup.", static_cast<unsigned>(static_cast<std::uint16_t>(port)))
        L2_LOG_ERROR(
            Log,
            "[listener] Cannot register the accept wait for TCP port %u; closing the listener socket and stopping startup.",
            static_cast<unsigned>(static_cast<std::uint16_t>(port))
        );
#else
        // LogIOError(
        //     "RegisterWait error on port %d",
        //     port)
        // Log.Add(
        //     LOG_ERROR,
        //     "[listener] Cannot register the accept wait for TCP port %u. Closing the listener socket and stopping
        //     startup.", static_cast<unsigned>(static_cast<std::uint16_t>(port)))
        L2_LOG_ERROR(
            Log,
            "[listener] Cannot register the accept wait for TCP port %u; closing the listener socket and stopping startup.",
            static_cast<unsigned>(static_cast<std::uint16_t>(port))
        );
#endif
        Close();
        return 0;
    }
    return 1;
}

void CIOServer::Close() {
    L2_GUARD_FUNCTION();
    if (m_hSocket != kInvalidNativeSocket) {
        CloseNativeSocket(m_hSocket);
        m_hSocket = kInvalidNativeSocket;
    }
    if (m_hAcceptEvent) {
#if defined(_WIN32)
        ::WSACloseEvent(static_cast<WSAEVENT>(m_hAcceptEvent));
#endif
        m_hAcceptEvent = nullptr;
    }
}

void CIOServer::Stop() {
    L2_GUARD_FUNCTION();
    const std::uint64_t socket = m_hSocket;
    m_hSocket = kInvalidNativeSocket;
    CloseNativeSocket(socket);
}

void CIOServer::OnWaitCallback() {
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    ::WSAResetEvent(static_cast<WSAEVENT>(m_hAcceptEvent));
#endif
    portable::PostIOCompletion(this, 0, nullptr);
}

void CIOServer::OnIOCallback(int success, std::uint32_t transferred, PortableOverlapped* overlapped) {
    L2_GUARD_FUNCTION();

    ::sockaddr_in clientAddress{};
#if defined(_WIN32)
    int clientAddressLength = sizeof(clientAddress);
    const SOCKET accepted =
        ::accept(static_cast<SOCKET>(m_hSocket), reinterpret_cast<sockaddr*>(&clientAddress), &clientAddressLength);
    const std::uint64_t acceptedSocket = static_cast<std::uint64_t>(accepted);
#else
    socklen_t clientAddressLength = sizeof(clientAddress);
    const int accepted =
        ::accept(static_cast<int>(m_hSocket), reinterpret_cast<sockaddr*>(&clientAddress), &clientAddressLength);
    const std::uint64_t acceptedSocket = accepted < 0 ? kInvalidNativeSocket : static_cast<std::uint64_t>(accepted);
#endif

    if (acceptedSocket == kInvalidNativeSocket) {
        const int error = GetSocketLastError();
        if (error == 10035) {
            return;
        }
        if (m_hSocket != kInvalidNativeSocket) {
#if defined(L2_BUILD_CACHED)
            // ::AddLog(
            //     const_cast<char*>(
            //         "accept error: %d"),
            //     error)
            // Log.Add(
            //     LOG_ERROR,
            //     "[listener] Cannot accept the pending connection on listener socket %llu (socket error %d). This accept
            //     attempt is abandoned.", static_cast<unsigned long long>(m_hSocket), error)
            L2_LOG_ERROR(
                Log,
                "[listener] Cannot accept the pending connection on listener socket %llu (socket error %d); this accept "
                "attempt is abandoned.",
                static_cast<unsigned long long>(m_hSocket), error
            );
#else
            // LogIOError(
            //     "accept error: %d",
            //     error)
            // Log.Add(
            //     LOG_ERROR,
            //     "[listener] Cannot accept the pending connection on listener socket %llu (socket error %d). This accept
            //     attempt is abandoned.", static_cast<unsigned long long>(m_hSocket), error)
            L2_LOG_ERROR(
                Log,
                "[listener] Cannot accept the pending connection on listener socket %llu (socket error %d); this accept "
                "attempt is abandoned.",
                static_cast<unsigned long long>(m_hSocket), error
            );
#endif
        }
        return;
    }

    CIOServerSocket* const socket = CreateSocket(acceptedSocket, &clientAddress);
    if (!socket) {
        CloseNativeSocket(acceptedSocket);
        // LogIOError(
        //     "Socket Create fail")
        // Log.Add(
        //     LOG_WARN,
        //     "[listener] The socket factory declined connection %llu from %u.%u.%u.%u:%u. The accepted socket has been
        //     closed.", static_cast<unsigned long long>(acceptedSocket),
        //     static_cast<unsigned>((ntohl(clientAddress.sin_addr.s_addr) >> 24) & 0xffU),
        //     static_cast<unsigned>((ntohl(clientAddress.sin_addr.s_addr) >> 16) & 0xffU),
        //     static_cast<unsigned>((ntohl(clientAddress.sin_addr.s_addr) >> 8) & 0xffU),
        //     static_cast<unsigned>(ntohl(clientAddress.sin_addr.s_addr) & 0xffU),
        //     static_cast<unsigned>(ntohs(clientAddress.sin_port)))
        L2_LOG_WARN(
            Log,
            "[listener] The socket factory declined connection %llu from %u.%u.%u.%u:%u; the accepted socket has been "
            "closed.",
            static_cast<unsigned long long>(acceptedSocket),
            static_cast<unsigned>((ntohl(clientAddress.sin_addr.s_addr) >> 24) & 0xffU),
            static_cast<unsigned>((ntohl(clientAddress.sin_addr.s_addr) >> 16) & 0xffU),
            static_cast<unsigned>((ntohl(clientAddress.sin_addr.s_addr) >> 8) & 0xffU),
            static_cast<unsigned>(ntohl(clientAddress.sin_addr.s_addr) & 0xffU),
            static_cast<unsigned>(ntohs(clientAddress.sin_port))
        );
        return;
    }

    // CIOServerSocket keeps the exact target layout. The portable owner
    // supplies the native send/close calls and one WSARecv-equivalent read
    // operation whose completion returns through this object's IO callback.
    if (!portable::BindAcceptedSocketTransport(socket, acceptedSocket)) {
        // Log.Add(LOG_ERROR,
        //     "[listener] Cannot attach the IO transport to accepted connection %llu. Closing the connection before
        //     initialization.", static_cast<unsigned long long>(acceptedSocket))
        L2_LOG_ERROR(
            Log,
            "[listener] Cannot attach the IO transport to accepted connection %llu; closing the connection before "
            "initialization.",
            static_cast<unsigned long long>(acceptedSocket)
        );
        socket->Close();
        return;
    }
    socket->Initialize();
}

bool CIOObject::AddTimer(std::uint32_t delayMilliseconds, int id) { return AddTimer2(delayMilliseconds, id, 0); }

bool CIOObject::AddTimer2(std::uint32_t delayMilliseconds, int id, std::int64_t uid) {
    L2_GUARD_FUNCTION();
#if defined(L2_BUILD_CACHED)
    if (delayMilliseconds < 80U) {
        const int previous = g_shortTimerWarningCount.fetch_add(1, std::memory_order_relaxed);
        if (previous < 30) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"dwTime[%d] < 80 !!!!!!!",
            //     delayMilliseconds)
            // Log.Add(
            //     LOG_WARN,
            //     "[timer] Timer %d requested a delay of %u ms (uid %lld). Clamped to 80 ms. Short-delay diagnostics are
            //     limited to the first 30 requests.", id, static_cast<unsigned>(delayMilliseconds), static_cast<long
            //     long>(uid))
            L2_LOG_WARN(
                Log,
                "[timer] Timer %d requested a delay of %u ms (uid %lld); clamped to 80 ms. Short-delay diagnostics are "
                "limited to the first 30 requests.",
                id, static_cast<unsigned>(delayMilliseconds), static_cast<long long>(uid)
            );
            runtime::process::ReportCallStack(LOG_ERROR, false);
        }
        delayMilliseconds = 80U;
    }
#endif
    g_ObjectInTimer.fetch_add(1, std::memory_order_seq_cst);
    L2_ADD_REF(this, ObjectRefType::Timer);

    CIOTimer queued;
    queued.m_pObject = this;
    // GetTickCount is read after AddRef in the target. Do not reuse a loop or
    // scheduler timestamp supplied by the caller.
    queued.m_dwTime = runtime::GetTickCount() + delayMilliseconds;
    queued.m_id = id;
    queued.m_Uid = uid;
    SetInTimer(true);

    // The image rereads the logical TLS thread id only after the virtual
    // AddRef, timestamp construction and m_bInTimer publication.
    TimerQueue* queue = CurrentTimerQueue();
    queue->push(queued);
    return true;
}

void CIOObject::TimerDispatch() {
    // Current Cached.exe 0x004e65c0. The body samples GetTickCount once,
    // drains the priority queue selected by the current TLS thread ID and
    // calls LISTEN thread 21 through OnTimerCallback.
    L2_GUARD_FUNCTION();
    // The target samples GetTickCount once per TimerDispatch call.
    DispatchCurrentTimerQueueAt(runtime::GetTickCount());
}

void CIOObject::TimerDispatchAndClearAll() {
    L2_GUARD_FUNCTION();
    // The matching shared/ioc.cpp implementation linked into
    // NASC64_2015 is empty beyond this guard.
}

void CIOObject::ClearAllTimer() {
    L2_GUARD_FUNCTION();
    // Intentionally empty in the matching shipping shared library.
}

void CIOObject::GetTimerCount(int& timerCount, int& shortTimeCount) {
    L2_GUARD_FUNCTION();
    // The original body does not write either output reference.
}

bool CIOObject::SetTimeStamp(std::int32_t* timestamp) {
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    auto* const rawTimestamp = reinterpret_cast<volatile std::int32_t*>(timestamp);
    const std::int32_t previous = runtime::raw_atomic::LoadI32(rawTimestamp);
    if (previous < now) {
        runtime::raw_atomic::ExchangeI32(rawTimestamp, now);
    }
    return previous < now;
}

bool CIOObject::IsInTime(std::int32_t timestamp, int additionalSeconds) {
    const std::int32_t now = runtime::crt::Time32Now();
    const std::int32_t deadline =
        static_cast<std::int32_t>(static_cast<std::uint32_t>(timestamp) + static_cast<std::uint32_t>(additionalSeconds));
    return timestamp != 0 && now < deadline;
}

bool CIOObject::SetShortTimeStamp(std::uint32_t* timestamp, std::uint32_t additionalMilliseconds) {
    L2_GUARD_FUNCTION();
    const std::uint32_t next = runtime::GetTickCount() + additionalMilliseconds;
    auto* const rawTimestamp = reinterpret_cast<volatile std::int32_t*>(timestamp);
    const std::uint32_t previous = static_cast<std::uint32_t>(runtime::raw_atomic::LoadI32Relaxed(rawTimestamp));
    if (previous < next) {
        runtime::raw_atomic::ExchangeI32(rawTimestamp, static_cast<std::int32_t>(next));
    }
    return previous < next;
}

bool CIOObject::IsInShortTime(std::uint32_t timestamp, std::uint32_t additionalMilliseconds) {
    return timestamp != 0 && runtime::GetTickCount() < timestamp + additionalMilliseconds;
}

int CIOObject::RegisterWait(void* handle) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(g_globalLockTimer);
        if (g_vHandle.size() >= kMaximumWaitObjects) {
            // LogIOError(
            //     L"[%s][%d] CIOObject::RegisterWait failed. "
            //     L"count of handle >= %d",
            //     L"..\\Shared\\ioc.cpp",
            //     0x221,
            //     static_cast<int>(
            //         kMaximumWaitObjects))
            // Log.Add(
            //     LOG_ERROR,
            //     "[IO wait] Cannot register another wait handle: %llu handles are registered and the limit is %llu.
            //     registration is rejected.", static_cast<unsigned long long>(g_vHandle.size()), static_cast<unsigned long
            //     long>(kMaximumWaitObjects))
            L2_LOG_ERROR(
                Log,
                "[IO wait] Cannot register another wait handle: %llu handles are registered and the limit is %llu; "
                "registration is rejected.",
                static_cast<unsigned long long>(g_vHandle.size()), static_cast<unsigned long long>(kMaximumWaitObjects)
            );
            return 0;
        }
        g_vHandle.push_back(handle);
        g_vObject.push_back(this);
    }
    return 1;
}

namespace portable {

std::size_t CancelTimersForOwner(CIOObject* owner, std::size_t* nonzeroUidCount) {
    if (nonzeroUidCount) {
        *nonzeroUidCount = 0;
    }
    if (!owner) {
        return 0;
    }

    std::size_t cancelled = 0;
    std::size_t cancelledWithUid = 0;
    for (TimerQueue& queue : g_timerQueue) {
        TimerQueue retained;
        while (!queue.empty()) {
            CIOTimer queued = queue.top();
            queue.pop();
            if (queued.m_pObject == owner) {
                ++cancelled;
                if (queued.m_Uid != 0) {
                    ++cancelledWithUid;
                }
            } else {
                retained.push(std::move(queued));
            }
        }
        queue = std::move(retained);
    }

    if (nonzeroUidCount) {
        *nonzeroUidCount = cancelledWithUid;
    }
    if (cancelled == 0) {
        return 0;
    }
    g_ObjectInTimer.fetch_sub(static_cast<std::int32_t>(cancelled), std::memory_order_seq_cst);
    owner->SetInTimer(false);
    for (std::size_t index = 0; index < cancelled; ++index) {
        L2_RELEASE(owner, ObjectRefType::Timer);
    }
    return cancelled;
}

} // namespace portable

#if defined(L2_BUILD_L2NPC) || defined(L2_BUILD_CACHED)

CIODeadlockChecker::CIODeadlockChecker() = default;

CIODeadlockChecker::DeadlockCrashInfo::DeadlockCrashInfo() = default;

void CIODeadlockChecker::DeadlockCrashInfo::SetCrashInfo(int threadId, std::uint32_t crashTick, std::uint32_t lastTick) {
    m_bForcedCrash = true;
    m_CrashTick = crashTick;
    m_nLastTick = lastTick;
    m_nThreadID = threadId;
}

bool CIODeadlockChecker::DeadlockCrashInfo::ForcedCrashByDeadlockChecker() { return m_bForcedCrash; }

std::uint32_t CIODeadlockChecker::DeadlockCrashInfo::GetCrashTick() { return m_CrashTick; }

std::uint32_t CIODeadlockChecker::DeadlockCrashInfo::GetLastTick() { return m_nLastTick; }

int CIODeadlockChecker::DeadlockCrashInfo::GetThreadID() { return m_nThreadID; }

void CIODeadlockChecker::DeadlockCrashInfo::PrintLog(void* file) {
    // Current L2NPC.exe 0x00455ec0 and Cached.exe 0x004e55fc.
    hprintf(
        file,
        L"\tDeadlock or Super-Lag!!! "
        L"ThreadID(%d), CurrentTick[%u] - "
        L"DeadlockCheckerTick[%u] = %u\r\n",
        m_nThreadID, m_CrashTick, m_nLastTick, m_CrashTick - m_nLastTick
    );
}

void CIODeadlockChecker::Init() {
    m_nThreadID = CThreadManager::GetThreadID();
    TimerExpired(0);
}

void CIODeadlockChecker::TimerExpired(int) {
    // Current Cached.exe virtual body 0x004cae34 refreshes the short tick,
    // captures the logical TLS thread ID and rearms itself for 5000 ms.
    SetShortTimeStamp(&m_nLastTick, 0);
    m_nThreadID = CThreadManager::GetThreadID();
    AddTimer(5000U, 0);
}

bool CIODeadlockChecker::IsOver49Days(std::uint32_t currentTick, std::uint32_t lastTick) { return currentTick < lastTick; }

bool CIODeadlockChecker::IsTimeOver(std::uint32_t currentTick, std::uint32_t lastTick) {
    return m_bActive && !IsOver49Days(currentTick, lastTick) &&
        static_cast<std::int32_t>(currentTick - lastTick) >
#if defined(L2_BUILD_CACHED)
        240000;
#else
        20000;
#endif
}

void CIODeadlockChecker::TestDeadlock() {
    const std::uint32_t lastTick = m_nLastTick;
    const std::uint32_t currentTick = runtime::GetTickCount();
    if (IsTimeOver(currentTick, lastTick)) {
        // Noise: ProcessDeadlock below reports the same failure with both ticks and the crash action.
        // Log.Add(
        //     LOG_ERROR,
        //     L"[TestDeadlock] last: %u, cur: %u",
        //     lastTick,
        //     currentTick)
        Singleton<CIODeadlockChecker>::inst().ProcessDeadlock(currentTick, lastTick);
    }
}

void CIODeadlockChecker::Test49Days() {
    const std::uint32_t lastTick = m_nLastTick;
    const std::uint32_t currentTick = runtime::GetTickCount();
    if (!Singleton<CIODeadlockChecker>::inst().IsOver49Days(currentTick, lastTick)) {
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[Test49Days] last: %u, cur: %u",
    //     lastTick,
    //     currentTick)
    // Log.Add(
    //     LOG_ERROR,
    //     "[watchdog] The 32-bit tick counter decreased from %u ms to %u ms. The legacy rollover check is forcing a crash
    //     report.", static_cast<unsigned>(lastTick), static_cast<unsigned>(currentTick))
    L2_LOG_ERROR(
        Log,
        "[watchdog] The 32-bit tick counter decreased from %u ms to %u ms; the legacy rollover check is forcing a crash "
        "report.",
        static_cast<unsigned>(lastTick), static_cast<unsigned>(currentTick)
    );
    ForceCrash(CrashInfo::SERVER_BOOT_TIME_IS_OVER_49_DAYS);
}

void CIODeadlockChecker::ProcessDeadlock(std::uint32_t currentTick, std::uint32_t lastTick) {
    L2_GUARD_FUNCTION();
    m_DeadlockCrashInfo.SetCrashInfo(m_nThreadID, currentTick, lastTick);
    const std::uint32_t differenceSeconds = (currentTick - lastTick) / 1000U;
    // Log.Add(
    //     LOG_LOCAL,
    //     L"!!! ThreadID(%d), deadlock or super-lag "
    //     L"detected!!!  (diff %u sec) ",
    //     m_nThreadID,
    //     differenceSeconds)
    // Log.Add(
    //     LOG_LOCAL,
    //     "[watchdog] IO timer heartbeat has not refreshed for %u seconds (last tick %u ms, current tick %u ms. Last
    //     heartbeat on worker %d). Forcing a crash report. A stalled heartbeat alone does not prove a deadlock.",
    //     static_cast<unsigned>(differenceSeconds), static_cast<unsigned>(lastTick),
    //     static_cast<unsigned>(currentTick), m_nThreadID)
    // runtime::log::WriteErrorWithLocalCopy(
    //     Log,
    //     "[watchdog] IO timer heartbeat has not refreshed for %u seconds (last tick %u ms, current tick %u ms. Last
    //     heartbeat on worker %d). Forcing a crash report. A stalled heartbeat alone does not prove a deadlock.",
    //     static_cast<unsigned>(differenceSeconds), static_cast<unsigned>(lastTick),
    //     static_cast<unsigned>(currentTick), m_nThreadID)
    L2_LOG_ERROR_WITH_LOCAL_COPY(
        Log,
        "[watchdog] IO timer heartbeat has not refreshed for %u seconds (last tick %u ms, current tick %u ms; last "
        "heartbeat on worker %d); forcing a crash report. A stalled heartbeat alone does not prove a deadlock.",
        static_cast<unsigned>(differenceSeconds), static_cast<unsigned>(lastTick), static_cast<unsigned>(currentTick),
        m_nThreadID
    );
    // Log.Add(
    //     LOG_ERROR,
    //     L"!!! ThreadID(%d), deadlock or super-lag "
    //     L"detected!!!  (diff %u sec) ",
    //     m_DeadlockCrashInfo.m_nThreadID,
    //     differenceSeconds)
    // Noise: WriteErrorWithLocalCopy above already emits this error once and keeps both destinations.
    // Log.Add(
    //     LOG_ERROR,
    //     "[watchdog] IO timer heartbeat has not refreshed for %u seconds (last tick %u ms, current tick %u ms. Last
    //     heartbeat on worker %d). Forcing a crash report. A stalled heartbeat alone does not prove a deadlock.",
    //     static_cast<unsigned>(differenceSeconds), static_cast<unsigned>(lastTick),
    //     static_cast<unsigned>(currentTick), m_DeadlockCrashInfo.m_nThreadID)
    ForceCrash(CrashInfo::BY_DEADLOCK_CHECKER);
}

CDeadlockCheckerInactivator::CDeadlockCheckerInactivator() { Singleton<CIODeadlockChecker>::inst().m_bActive = false; }

CDeadlockCheckerInactivator::~CDeadlockCheckerInactivator() {
    CIODeadlockChecker& checker = Singleton<CIODeadlockChecker>::inst();
    CIOObject::SetShortTimeStamp(&checker.m_nLastTick, 0);
    checker.m_nThreadID = CThreadManager::GetThreadID();
    checker.m_bActive = true;
}

#endif // L2_BUILD_L2NPC || L2_BUILD_CACHED


#if defined(L2_BUILD_CACHED)

void IOThread_common(void* argument) {
    // Current Cached.exe 0x004e6974. IOThreadRuntime replaces only the two
    // native completion-port waits. Deadline, TLS database routing, timer
    // dispatch, lock-stack checks and callback order remain target behavior.
    const int workerId = static_cast<int>(reinterpret_cast<std::intptr_t>(argument));
    std::uint32_t nextSyncTime = runtime::GetTickCount();
    InitializeRandomNumber();

    for (;;) {
        GuardProfileHelper::InitializeGuardProfilePerThread();
        const int threadId = CThreadManager::GetThreadID();
        g_dwGuardStartTick[threadId] = runtime::GetTickCount();
        // Current CacheD 0x004e69e5 copies DAT_0083d29c into TLS +0x10
        // before TimerDispatch. A queued CServerPacket later replaces it
        // with that packet's captured database selector in OnIOCallback.
        runtime::process::SetCurrentDatabaseId(runtime::process::ConfiguredDatabaseId());
        L2_GUARD_FUNCTION();

        const std::uint32_t now = runtime::GetTickCount();
        std::uint32_t waitMilliseconds = 0;
        if (nextSyncTime < now) {
            CIOObject::TimerDispatch();
            nextSyncTime = runtime::GetTickCount() + 50U;
            waitMilliseconds = 50U;
        } else {
            waitMilliseconds = nextSyncTime - now;
        }

        IOThreadRuntime::QueueEntry entry;
        ::g_nRunningThread.fetch_add(1, std::memory_order_seq_cst);
        const IOThreadRuntime::BoundWaitResult waitResult =
            IOThreadRuntime::WaitBoundCompletion(waitMilliseconds, entry, workerId < 8 ? 0U : 1U);
        ::g_nRunningThread.fetch_sub(1, std::memory_order_seq_cst);

        if (waitResult == IOThreadRuntime::BoundWaitResult::Stop) {
            // Cooperative host teardown is the only return path. The
            // shipping loop is process-lifetime.
            return;
        }
        if (waitResult == IOThreadRuntime::BoundWaitResult::Timeout) {
            continue;
        }

        CheckIOLockStack(L"Invalid LockStack before io!!!");
        if (entry.object) {
            if (!entry.success && entry.socketError != 0) {
                SetSocketLastError(entry.socketError);
            }
            entry.object->OnIOCallback(entry.success, entry.transferred, entry.overlapped);
        }
        CheckIOLockStack(L"Invalid LockStack after io!!!");
    }
}

std::uint32_t IOThread(void* argument) {
    const int workerId = static_cast<int>(reinterpret_cast<std::intptr_t>(argument));
    CThreadManager::RegisterThisThread(workerId);
    if (ExceptionMailing()) {
        try {
            IOThread_common(argument);
        } catch (...) {
            ELOG(L"*%S:%d(%S) exception\n", L2_SOURCE_FILE, __LINE__, __TIMESTAMP__);
            Log.FlushLogFile();
            GIP.FlushGIPFile();
            send_exception(true);
        }
    } else {
        IOThread_common(argument);
        // Current 0x004e6cc0 falls into INT3 if the process-lifetime loop
        // returns. Portable cooperative shutdown must instead let the owning
        // runtime join this worker.
    }
    return 0;
}

int CreateIOThread(int threadCount) {
    // Current Cached.exe 0x004e6d5c.
    L2_GUARD_FUNCTION();
    InitIOThread();
    if (threadCount > 0) {
        g_cacheDIOThreadRuntime.StartRaw(threadCount, [](int threadId) {
            IOThread(reinterpret_cast<void*>(static_cast<std::intptr_t>(threadId)));
        });
    }
    // The target does not check _beginthreadex and returns one even for a
    // non-positive loop bound.
    return 1;
}

#endif // L2_BUILD_CACHED

#if defined(L2_BUILD_L2NPC)

std::uint32_t IOThread2(void* argument) {
    // Current L2NPC.exe 0x00456f1c.
    auto* const callback = static_cast<IOThreadCallback*>(argument);
    if (!callback) {
        return 0;
    }

    CThreadManager::RegisterThisThread(callback->m_Thread);
    if (ExceptionMailing()) {
        callback->IOThread_mailing();
    } else {
        callback->IOThread_debug();
        // The target emits int 3 here because IOThread_debug is noreturn.
        // Cooperative portable shutdown is the only path that can return.
    }
    return 0;
}

bool CreateIOThread2(IOThreadCallback* callback) {
    // Current L2NPC.exe 0x00456f54.
    L2_GUARD_FUNCTION();
    if (callback) {
#if defined(_WIN32)
        SYSTEM_INFO systemInformation{};
        ::GetSystemInfo(&systemInformation);
#else
        std::thread::hardware_concurrency();
#endif
        IOThreadRuntime::StartBoundThread(callback);
    }
    // The target reports only whether the callback pointer was non-null. It
    // does not test _beginthreadex's return value.
    return callback != nullptr;
}

#endif // L2_BUILD_L2NPC
