#include "ConnectThreadManager.h"

#include "AccountClient/AccountServerStub.h"
#include "CConfig.h"
#include "Relay.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Exception.h"
#include "Shared/src/IOC.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/OwnedThread.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"

#include <chrono>
#include <new>
#include <thread>

namespace {

void ConnectThreadCommon() {
    for (;;) {
        // Current 00431cdf keeps this otherwise-unused import call at the
        // beginning of every loop iteration.
        static_cast<void>(runtime::GetTickCount());

        if (g_bConnectRelayServer) {
            static_cast<void>(RelayClient::Inst()->Init());
        }
        if (g_AccountServerSetting.bConnect) {
            static_cast<void>(AccountServerStub::Inst().Connect(2));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    }
}

std::uint32_t ConnectManagerThreadEntry(void*) {
    // Current 00431e58 registers logical thread 29 before publishing the
    // one 100-entry DefaultIOBuffer queue in the per-thread pool.
    CThreadManager::RegisterThisThread(ThreadID::CONNECT);
    const int threadId = CThreadManager::GetThreadID();
    g_IOBufferPools[threadId] = new (std::nothrow) CNewMwsrQueue<DefaultIOBuffer>(100);

    if (ExceptionMailing()) {
        // Log.Add(LOG_NORMAL, L"Relay in mailing mode")
        // L2_LOG_INFO(Log, L"Relay in mailing mode")
        L2_LOG_INFO(Log, "Connection worker enabled exception reporting; starting the relay/account reconnect loop.");
        try {
            ConnectThreadCommon();
        } catch (...) {
            runtime::process::HandleUnhandledSharedThreadException(
                "ConnectThreadManager.cpp", 0x68, "Wed Apr  5 10:17:01 2017"
            );
        }
    } else {
        ConnectThreadCommon();
    }
    return 0;
}

} // namespace

CConnectThreadManager* CConnectThreadManager::m_Inst = nullptr;

CConnectThreadManager::CConnectThreadManager() {}

CConnectThreadManager& CConnectThreadManager::Inst() {
    // Current 00431bf8 uses a non-atomic, process-lifetime
    // pointer and never registers an atexit destructor.
    L2_GUARD_FUNCTION();
    if (m_Inst == nullptr) {
        m_Inst = new CConnectThreadManager;
    }
    return *m_Inst;
}

void CConnectThreadManager::CreateThread() {
    // Current 00431edc starts immediately, stores the handle at +0x08 and
    // publishes it as logical CONNECT thread 29. _beginthreadex writes the
    // native thread id through the object base. OwnedThread exposes the same
    // value after creation on portable hosts.
    L2_GUARD_FUNCTION();
    m_hConnectThread = runtime::CreateOwnedThread(ConnectManagerThreadEntry, nullptr, false);
    m_nConnectThread = runtime::OwnedThreadId(m_hConnectThread);
    CThreadManager::SetThread(ThreadID::CONNECT, m_hConnectThread);
}
