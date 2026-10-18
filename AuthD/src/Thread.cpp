#include "Thread.h"
#include "Shared/src/ScopedLockGuards.h"

#include "Shared/src/Runtime/OperatorLog.h"

#include "Config.h"
#include "Exception.h"
#include "IOServer.h"
#include "IOServerEx.h"
#include "IPSessionDB.h"
#include "IOObject.h"
#include "Job.h"
#include "L2AuthD.h"
#include "Log.h"
#include "LogSocket.h"
#include "Memory.h"
#include "SocketServerEx.h"

#include "Shared/src/Runtime/CompletionPort.h"
#include "Shared/src/Runtime/EventHandle.h"
#include "Shared/src/Runtime/NativeThreadHandle.h"
#include "Shared/src/Runtime/RawAtomic.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <thread>
#include <vector>

#if defined(_WIN32) && defined(_MSC_VER)
#include <Windows.h>
#endif

void* g_hIOCompletionPort = nullptr;
void* g_hIOCompletionPortInt = nullptr;
volatile std::int32_t g_nRunningThread = 0;
bool g_bTerminating = false;
void** g_hServerThread = nullptr;
void** g_hServerThreadInt = nullptr;
unsigned int* g_nServerThreadId = nullptr;
unsigned int* g_nServerThreadIntId = nullptr;
CLogSocket* pLogSocket = nullptr;

namespace {

using ThreadBody = int (*)();

struct PortableThreadStorage {
    // _beginthreadex returns process-owned handles that survive the source
    // function. std::thread needs a native owner as well, so this is only the
    // host transport behind the exact published handle/id arrays.
    std::vector<std::thread> threads;
};

PortableThreadStorage& ThreadStorage() {
    // The target does not close these worker handles before process teardown.
    // A process-lifetime sidecar keeps that ownership and avoids adding a
    // portable static-destructor join that the EXE never performs.
    static auto* const storage = new PortableThreadStorage;
    return *storage;
}

void* BeginThread(
    unsigned int (*entry)(void*),
    void* argument,
    unsigned int* threadId) {
    try {
        PortableThreadStorage& storage = ThreadStorage();
        storage.threads.emplace_back(
            [entry, argument] {
                entry(argument);
            });
        std::thread& thread = storage.threads.back();
        if (threadId != nullptr) {
            *threadId = runtime::NativeThreadId(thread);
        }
        return runtime::NativeThreadHandle(thread);
    } catch (...) {
        // _beginthreadex reports creation failure as a null handle. The
        // shipping CreateIOThread does not branch on that result.
        return nullptr;
    }
}

int RunCrashBoundary(
    ThreadBody body,
    [[maybe_unused]] int sourceLine,
    int fallbackResult,
    const char* operation) {
    bool handled = false;
#if defined(_WIN32) && defined(_MSC_VER)
    __try {
        return body();
    } __except (exception_filter(GetExceptionInformation())) {
        handled = true;
    }
#else
    // Win32 SEH is the only unavailable transport here. C++ exceptions still
    // take the target's exact ELOG/send_exception terminal path. Native
    // signals will be connected when Exception.cpp installs its host handler.
    try {
        return body();
    } catch (...) {
        handled = true;
    }
#endif
    if (handled) {
        // Original: ELOG("*%s:%d(%s) exception\n", "Thread.cpp",
        //     sourceLine, "Wed Oct 23 17:02:12 2019")
        ELOG("[Thread.cpp] Exception while %s. Entering exception handling.\n",
            operation);
        send_exception(true);
    }
    return fallbackResult;
}

int IOThreadServerBody() {
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    for (;;) {
        if (runtime::raw_atomic::LoadBool(
                &g_bTerminating)) {
            // Noise: The loop exit below records the same worker stop and termination reason once.
            // Log.AddLog(LOG_DEBUG, "terminate IOThreadServer")
            break;
        }

        runtime::completion_port::Packet packet;
        [[maybe_unused]] const auto waitStatus =
            runtime::completion_port::Wait(
                g_hIOCompletionPort,
                runtime::event_handle::kInfinite,
                packet);
        if (waitStatus != runtime::completion_port::WaitStatus::Packet) {
            // Log.AddLog(LOG_ERROR,
            //     "component=runtime event=completion_wait_failed worker=server_io reason=%s callback_present=%d action=continue_legacy_dispatch",
            //     waitStatus == runtime::completion_port::WaitStatus::Timeout ? "timeout" : "port_closed_or_failed",
            //     packet.completionKey != 0)
            // Log.AddLog(LOG_ERROR,
            //     "[runtime] Server I/O wait failed: %s (callback present: %d). Dispatch continues.",
            //     waitStatus == runtime::completion_port::WaitStatus::Timeout ? "timeout" : "port closed or failed",
            //     packet.completionKey != 0)
            // L2_LOG_ERROR(Log,
            //     "[runtime] Server I/O wait failed: %s (callback present: %d). Dispatch continues.",
            //     waitStatus == runtime::completion_port::WaitStatus::Timeout ? "timeout" : "port closed or failed",
            //     packet.completionKey != 0)
            L2_LOG_ERROR(Log,
                "[runtime] Server I/O wait failed: %s. Callback dispatch is still attempted; target object %s.",
                waitStatus == runtime::completion_port::WaitStatus::Timeout ? "timeout" : "port closed or failed",
                packet.completionKey != 0 ? "present" : "missing");
        }
        auto* const pObject = reinterpret_cast<CIOObject*>(
            packet.completionKey);
        runtime::raw_atomic::IncrementI32(
            &g_nRunningThread);
        pObject->OnIOCallback(
            packet.success ? 1 : 0,
            packet.transferred,
            static_cast<_OVERLAPPED*>(packet.overlapped));
        runtime::raw_atomic::DecrementI32(
            &g_nRunningThread);
    }
    // Log.AddLog(LOG_DEBUG, "IOThread Server Exit")
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "component=runtime event=worker_stopped worker=server_io reason=termination_requested")
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "[runtime] Server I/O worker stopped after a shutdown request.")
    L2_LOG_INFO(Log,
        "[runtime] Server I/O worker stopped after a shutdown request.");
    return 0;
}

int IOThreadIntBody() {
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    for (;;) {
        if (runtime::raw_atomic::LoadBool(
                &g_bTerminating)) {
            // Log.AddLog(LOG_DEBUG, "terminate IOThreadInt")
            // Log.AddLog(
            //     LOG_NORMAL,
            //     "component=runtime event=worker_stopped worker=internal_io reason=termination_requested")
            // Log.AddLog(
            //     LOG_NORMAL,
            //     "[runtime] Internal I/O worker stopped after a shutdown request.")
            L2_LOG_INFO(Log,
                "[runtime] Internal I/O worker stopped after a shutdown request.");
            break;
        }

        runtime::completion_port::Packet packet;
        [[maybe_unused]] const auto waitStatus =
            runtime::completion_port::Wait(
                g_hIOCompletionPortInt,
                runtime::event_handle::kInfinite,
                packet);
        if (waitStatus != runtime::completion_port::WaitStatus::Packet) {
            // Log.AddLog(LOG_ERROR,
            //     "component=runtime event=completion_wait_failed worker=internal_io reason=%s callback_present=%d action=continue_legacy_dispatch",
            //     waitStatus == runtime::completion_port::WaitStatus::Timeout ? "timeout" : "port_closed_or_failed",
            //     packet.completionKey != 0)
            // Log.AddLog(LOG_ERROR,
            //     "[runtime] Internal I/O wait failed: %s (callback present: %d). Dispatch continues.",
            //     waitStatus == runtime::completion_port::WaitStatus::Timeout ? "timeout" : "port closed or failed",
            //     packet.completionKey != 0)
            // L2_LOG_ERROR(Log,
            //     "[runtime] Internal I/O wait failed: %s (callback present: %d). Dispatch continues.",
            //     waitStatus == runtime::completion_port::WaitStatus::Timeout ? "timeout" : "port closed or failed",
            //     packet.completionKey != 0)
            L2_LOG_ERROR(Log,
                "[runtime] Internal I/O wait failed: %s. Callback dispatch is still attempted; target object %s.",
                waitStatus == runtime::completion_port::WaitStatus::Timeout ? "timeout" : "port closed or failed",
                packet.completionKey != 0 ? "present" : "missing");
        }
        auto* const pObject = reinterpret_cast<CIOObject*>(
            packet.completionKey);
        pObject->OnIOCallback(
            packet.success ? 1 : 0,
            packet.transferred,
            static_cast<_OVERLAPPED*>(packet.overlapped));
    }
    return 0;
}

unsigned int TargetArrayBytes(
    int count,
    std::size_t hostElementSize) noexcept {
    // The x86 instruction is `shl count, 2`. Pointer arrays require native
    // pointer width after the ABI boundary. Uint id arrays remain four bytes.
    return static_cast<unsigned int>(
        static_cast<std::uint32_t>(count) *
        static_cast<std::uint32_t>(hostElementSize));
}

int CreateIOThreadBody() {
    unsigned int timerThreadId;
    [[maybe_unused]] void* const timerThread =
        BeginThread(TimerThread, nullptr, &timerThreadId);

    if (timerThread == nullptr) {
        // Log.AddLog(LOG_ERROR,
        //     "component=runtime event=worker_start_failed worker=timer reason=no_thread_handle action=continue_initialization")
        // Log.AddLog(LOG_ERROR,
        //     "[runtime] Could not create the timer worker. Initialization continues.")
        // L2_LOG_ERROR(Log,
        //     "[runtime] Could not create the timer worker. Initialization continues.")
        runtime::logging::Write("authd", runtime::logging::Level::Error,
            "[runtime] Could not create the timer worker. Initialization continues.", __FILE__);
    }
    g_hIOCompletionPort =
        runtime::completion_port::Create();
    g_hIOCompletionPortInt =
        runtime::completion_port::Create();

    if (g_hIOCompletionPort == nullptr || g_hIOCompletionPortInt == nullptr) {
        // Log.AddLog(LOG_ERROR,
        //     "component=runtime event=completion_port_create_failed server_port_present=%d internal_port_present=%d action=continue_initialization",
        //     g_hIOCompletionPort != nullptr, g_hIOCompletionPortInt != nullptr)
        // Log.AddLog(LOG_ERROR,
        //     "[runtime] Could not create both completion ports (server port present: %d. Internal port present: %d). Initialization continues.",
        //     g_hIOCompletionPort != nullptr, g_hIOCompletionPortInt != nullptr)
        // L2_LOG_ERROR(Log,
        //     "[runtime] Could not create both completion ports (server port present: %d. Internal port present: %d). Initialization continues.",
        //     g_hIOCompletionPort != nullptr, g_hIOCompletionPortInt != nullptr)
        char message[256]{};
        std::snprintf(message, sizeof(message),
            "[runtime] Completion port creation failed. Server port: %s; internal port: %s. Initialization continues.",
            g_hIOCompletionPort != nullptr ? "available" : "unavailable",
            g_hIOCompletionPortInt != nullptr ? "available" : "unavailable");
        runtime::logging::Write("authd", runtime::logging::Level::Error,
            message, __FILE__);
    }
    g_hServerThread = static_cast<void**>(Memory::_malloc(
        TargetArrayBytes(
            config.numServerThread,
            sizeof(void*))));
    g_nServerThreadId = static_cast<unsigned int*>(Memory::_malloc(
        TargetArrayBytes(
            config.numServerThread,
            sizeof(unsigned int))));
    g_hServerThreadInt = static_cast<void**>(Memory::_malloc(
        TargetArrayBytes(
            config.numServerIntThread,
            sizeof(void*))));
    g_nServerThreadIntId = static_cast<unsigned int*>(Memory::_malloc(
        TargetArrayBytes(
            config.numServerIntThread,
            sizeof(unsigned int))));

    if (g_hServerThread == nullptr || g_nServerThreadId == nullptr || g_hServerThreadInt == nullptr || g_nServerThreadIntId == nullptr) {
        // Log.AddLog(LOG_ERROR,
        //     "component=runtime event=worker_storage_allocation_failed server_workers=%d internal_workers=%d server_handles_present=%d server_ids_present=%d internal_handles_present=%d internal_ids_present=%d action=continue_legacy_initialization",
        //     config.numServerThread, config.numServerIntThread,
        //     g_hServerThread != nullptr, g_nServerThreadId != nullptr,
        //     g_hServerThreadInt != nullptr, g_nServerThreadIntId != nullptr)
        // Log.AddLog(LOG_ERROR,
        //     "[runtime] Could not allocate storage for %d server and %d internal workers (server handles: %d. Server IDs: %d. Internal handles: %d. Internal IDs: %d). Initialization continues.",
        //     config.numServerThread, config.numServerIntThread,
        //     g_hServerThread != nullptr, g_nServerThreadId != nullptr,
        //     g_hServerThreadInt != nullptr, g_nServerThreadIntId != nullptr)
        // L2_LOG_ERROR(Log,
        //     "[runtime] Could not allocate storage for %d server and %d internal workers (server handles: %d. Server IDs: %d. Internal handles: %d. Internal IDs: %d). Initialization continues.",
        //     config.numServerThread, config.numServerIntThread,
        //     g_hServerThread != nullptr, g_nServerThreadId != nullptr,
        //     g_hServerThreadInt != nullptr, g_nServerThreadIntId != nullptr)
        char message[512]{};
        std::snprintf(message, sizeof(message),
            "[runtime] Cannot allocate worker storage (%d server, %d internal workers). Available arrays: server handles %s, server IDs %s, internal handles %s, internal IDs %s. Initialization continues.",
            config.numServerThread, config.numServerIntThread,
            g_hServerThread != nullptr ? "yes" : "no", g_nServerThreadId != nullptr ? "yes" : "no",
            g_hServerThreadInt != nullptr ? "yes" : "no", g_nServerThreadIntId != nullptr ? "yes" : "no");
        runtime::logging::Write("authd", runtime::logging::Level::Error,
            message, __FILE__);
    }
    for (int i = 0; i < config.numServerThread; ++i) {
        g_hServerThread[i] = BeginThread(
            IOThreadServer,
            reinterpret_cast<void*>(
                static_cast<std::uintptr_t>(i)),
            g_nServerThreadId + i);
        if (g_hServerThread[i] == nullptr) {
            // Log.AddLog(LOG_ERROR,
            //     "component=runtime event=worker_start_failed worker=server_io index=%d reason=no_thread_handle action=continue_initialization",
            //     i)
            // Log.AddLog(LOG_ERROR,
            //     "[runtime] Could not create server I/O worker %d. Initialization continues.",
            //     i)
            // L2_LOG_ERROR(Log,
            //     "[runtime] Could not create server I/O worker %d. Initialization continues.",
            //     i)
            char message[192]{};
            std::snprintf(message, sizeof(message),
                "[runtime] Could not create server I/O worker %d. Initialization continues.", i);
            runtime::logging::Write("authd", runtime::logging::Level::Error,
                message, __FILE__);
        }
    }
    for (int i = 0; i < config.numServerIntThread; ++i) {
        g_hServerThreadInt[i] = BeginThread(
            IOThreadInt,
            reinterpret_cast<void*>(
                static_cast<std::uintptr_t>(i)),
            g_nServerThreadIntId + i);
        if (g_hServerThreadInt[i] == nullptr) {
            // Log.AddLog(LOG_ERROR,
            //     "component=runtime event=worker_start_failed worker=internal_io index=%d reason=no_thread_handle action=continue_initialization",
            //     i)
            // Log.AddLog(LOG_ERROR,
            //     "[runtime] Could not create internal I/O worker %d. Initialization continues.",
            //     i)
            // L2_LOG_ERROR(Log,
            //     "[runtime] Could not create internal I/O worker %d. Initialization continues.",
            //     i)
            char message[192]{};
            std::snprintf(message, sizeof(message),
                "[runtime] Could not create internal I/O worker %d. Initialization continues.", i);
            runtime::logging::Write("authd", runtime::logging::Level::Error,
                message, __FILE__);
        }
    }
    return 1;
}

int ListenThreadBody() {
    server.Run(
        config.serverPort,
        CSocketServer::Allocate,
        false);
    serverEx.Run(
        config.serverExPort,
        CSocketServerEx::Allocate);
    serverInt.Run(
        config.serverIntPort,
        CSocketInt::Allocate);
    serverMi.Run(
        config.serverMiPort,
        CSocketMi::Allocate);

    job.RunEvent();

    server.Close();
    serverEx.Stop();
    serverInt.Close();
    serverMi.Close();

    if (pIPSocket != nullptr) {
        {
            L2_WRITE_GUARD(gIPLock);
            if (!IPServerReconnect) {
                pIPSocket->CloseSocket();
            }
            pIPSocket->ReleaseRef();
        }
    }

    if (pLogSocket != nullptr) {
        {
            L2_WRITE_GUARD(gLogLock);
            if (!LogDReconnect) {
                pLogSocket->CloseSocket();
            }
            pLogSocket->ReleaseRef();
        }
    }

    Memory::_free(g_hServerThread);
    Memory::_free(g_nServerThreadId);
    Memory::_free(g_hServerThreadInt);
    Memory::_free(g_nServerThreadIntId);
    runtime::raw_atomic::StoreBool(
        &globalTeminateEvent,
        true);
    return 0;
}

} // namespace

// L2Authd.exe 0x0048B1E0, Src/Thread.cpp:45-51.
unsigned int TimerThread([[maybe_unused]] void* param) {
    runtime::logging::SetThreadName("timer");
    job.RunTimer();
    // Log.AddLog(LOG_DEBUG, "Timer thread terminated")
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "component=runtime event=worker_stopped worker=timer reason=timer_loop_returned")
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "[runtime] Timer worker stopped, its loop has returned.")
    L2_LOG_INFO(Log,
        "[runtime] Timer worker stopped; its loop has returned.");
    return 0;
}

// L2Authd.exe 0x0048B210, Src/Thread.cpp:55-78. The target SEH report site is
// source line 76.
unsigned int IOThreadServer([[maybe_unused]] void* param) {
    runtime::logging::SetIndexedThreadName("server-io", reinterpret_cast<std::uintptr_t>(param));
    return static_cast<unsigned int>(
        // Original: RunCrashBoundary(IOThreadServerBody, 76, 0))
        RunCrashBoundary(IOThreadServerBody, 76, 0, "running the server I/O worker"));
}

// L2Authd.exe 0x0048B340, Src/Thread.cpp:81-102. The target SEH report site
// is source line 100.
unsigned int IOThreadInt([[maybe_unused]] void* param) {
    runtime::logging::SetIndexedThreadName("internal-io", reinterpret_cast<std::uintptr_t>(param));
    return static_cast<unsigned int>(
        // Original: RunCrashBoundary(IOThreadIntBody, 100, 0))
        RunCrashBoundary(IOThreadIntBody, 100, 0, "running the internal I/O worker"));
}

// L2Authd.exe 0x0048B440, Src/Thread.cpp:116-159. The target SEH report site
// is source line 157.
unsigned int ListenThread([[maybe_unused]] void* param) {
    runtime::logging::SetThreadName("listen");
    return static_cast<unsigned int>(
        // Original: RunCrashBoundary(ListenThreadBody, 157, 0))
        RunCrashBoundary(ListenThreadBody, 157, 0, "running the listener"));
}

// L2Authd.exe 0x0048B620, Src/Thread.cpp:162-187. The target SEH report site
// is source line 185 and its fallback return remains one.
int CreateIOThread() {
    // Original: return RunCrashBoundary(CreateIOThreadBody, 185, 1)
    return RunCrashBoundary(CreateIOThreadBody, 185, 1, "creating I/O workers");
}
