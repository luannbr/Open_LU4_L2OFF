#include "Shared/src/Runtime/Utf8.h"
#include "Shared/src/Runtime/ConfigDirectory.h"

#include "Shared/src/Runtime/OperatorLog.h"
#include "AuthGateSvcMgr.h"

#include "AuthGateConfig.h"
#include "ClientSocket.h"
#include "FileLog.h"
#include "GameGuard.h"
#include "IOObject.h"
#include "MailReport.h"
#include "Platform/IOCompletionDispatch.h"
#include "Platform/ServiceThreadRuntime.h"
#include "ServiceMgr.h"
#include "Shared/src/Runtime/CompletionPort.h"
#include "Shared/src/Runtime/LegacyCrtRandom.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/SystemMailTransport.h"
#include "Shared/src/Runtime/WindowsAnsi.h"
#include "StateMonMgr.h"
#include "WindowMgr.h"
#include "GlobalFunc.h"
#include "Lu4Login.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>

CAuthGateSvcMgr* CAuthGateSvcMgr::g_pAuthGateSvcMgr = nullptr;

// AuthGateD.map 0x0043EB00 and AuthGateSvcMgr.obj's $E1/$E2 own this
// process-global singleton. It is not an authgated.cpp definition.
CMailReport g_reportMail;

namespace {

constexpr std::uint32_t kMonitorTimer = 0x65;
constexpr std::uint32_t kAuthReconnectTimer = 0x66;
constexpr std::uint32_t kReportTimer = 0x67;
constexpr std::uint32_t kSocketTimeoutTimer = 0x68;
constexpr std::uint32_t kLogReconnectTimer = 0x69;

std::string TargetAnsi(const char16_t* text) {
    return portable::WindowsAnsiFromUtf16(
        std::u16string(text));
}

void ResumeThreads(void** handles, std::int32_t count) noexcept {
    for (std::int32_t index = 0; index < count; ++index) {
        authgated::platform::ResumeServiceThread(handles[index]);
    }
}

void JoinThreads(void**& handles, std::int32_t count) noexcept {
    if (!handles) return;
    for (std::int32_t index = 0; index < count; ++index) {
        authgated::platform::JoinAndDestroyServiceThread(handles[index]);
    }
    std::free(handles);
    handles = nullptr;
}

} // namespace

// AuthGateD.exe 0x00416470, AuthGateSvcMgr.cpp.
CAuthGateSvcMgr::CAuthGateSvcMgr()
    : m_threadPool(1000) {
    if (!g_pAuthGateSvcMgr) g_pAuthGateSvcMgr = this;
    m_bOn = false;
    m_hThread = nullptr;
    m_hIOThread = nullptr;
}

// AuthGateD.exe 0x00416560, AuthGateSvcMgr.cpp.
CAuthGateSvcMgr::~CAuthGateSvcMgr() {
    g_pAuthGateSvcMgr = nullptr;
}

// AuthGateD.exe 0x00416640, AuthGateSvcMgr.cpp.
bool CAuthGateSvcMgr::IsBlockedIP(in_addr address) {
    return m_blockedIPList.IpExists(address);
}

// AuthGateD.exe 0x00416650, AuthGateSvcMgr.cpp.
bool CAuthGateSvcMgr::IsBlockedTrial(const char* userId) {
    return m_tryList.IsBlocked(userId);
}

// AuthGateD.exe 0x00416660, AuthGateSvcMgr.cpp.
std::uint32_t CAuthGateSvcMgr::AddBadUser(const char* userId) {
    return m_badUsers.Add(userId);
}

// AuthGateD.exe 0x00416670, AuthGateSvcMgr.cpp.
bool CAuthGateSvcMgr::Run() {
    runtime::raw_atomic::StoreBool(&m_bOn, true);
    // CFileLog::g_pDefaultFileLog->LogT(
    //     LOG_SYSTEM,
    //     u"AuthGateD Service Running...")
    // CFileLog::g_pDefaultFileLog->LogT(
    //     LOG_SYSTEM,
    //     u"component=startup event=starting")
    // CFileLog::g_pDefaultFileLog->LogT(
    //     LOG_SYSTEM,
    //     u"[startup] Starting workers and the client listener.")
    L2_LOG(*CFileLog::g_pDefaultFileLog, LOG_SYSTEM,
        u"[startup] Starting workers and the client listener.");
    if (g_serviceManager.IsServiceMode() != 1) {
        CWindowMgr::g_pWindowMgr->SetTimer(kMonitorTimer, 3000);
    }
    CWindowMgr::g_pWindowMgr->SetTimer(kReportTimer, 600000);
    CWindowMgr::g_pWindowMgr->SetTimer(kSocketTimeoutTimer, 60000);
    // m_authSocket.Initialize(m_hIOCP)
    if (!m_authSocket.Initialize(m_hIOCP)) {
        std::array<char16_t, 20> address{};
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[authd] Socket initialization failed for %s:%u during service startup. Connection closure was requested; startup continues.",
            InetToStr(&config.authAddr, address.data(), address.size()),
            static_cast<unsigned int>(static_cast<std::uint16_t>(config.authPort)));
    }

    if (!m_ClientSocketContainer.Run(
            config.serverPort,
            CClientSocket::Allocate,
            config.acceptCallNum,
            config.socketLimit)) {
        // CFileLog::g_pDefaultFileLog->Log(
        //     LOG_ERROR,
        //     u"can't run server")
        // CFileLog::g_pDefaultFileLog->Log(
        //     LOG_ERROR,
        //     u"component=listener event=start_failed port=%d. Check preceding socket error and port availability",
        //     config.serverPort)
        // CFileLog::g_pDefaultFileLog->Log(
        //     LOG_ERROR,
        //     u"[listener] Could not listen on port %d. Check the preceding socket error and whether the port is available.",
        //     config.serverPort)
        // L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
        //     u"[listener] Could not listen on port %d. Check the preceding socket error and whether the port is available.",
        //     config.serverPort)
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[listener] Client listener startup failed on port %d. See the preceding socket or accept error. Service Run returns failure.",
            config.serverPort);
        return false;
    }
    if (config.lu4Port > 0) {
        if (!m_lu4Listener.Run(config.lu4Port, CClientSocket::AllocateLu4, config.acceptCallNum, config.socketLimit)) {
            L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
                u"[listener] LU4 client listener startup failed on port %d. Service Run returns failure.",
                config.lu4Port);
            return false;
        }
        L2_LOG(*CFileLog::g_pDefaultFileLog, LOG_SYSTEM, u"[listener] LU4 clients on port %d.", config.lu4Port);
    }

    ResumeThreads(m_hThread, m_iThreadCount);
    ResumeThreads(m_hIOThread, m_iIOThreadCount);
    // CFileLog::g_pDefaultFileLog->LogT(LOG_SYSTEM,
    //     u"component=listener event=listening port=%d socket_limit=%d",
    //     config.serverPort, config.socketLimit)
    // CFileLog::g_pDefaultFileLog->LogT(LOG_SYSTEM,
    //     u"[listener] Listening on port %d. Socket limit %d.",
    //     config.serverPort, config.socketLimit)
    L2_LOG(*CFileLog::g_pDefaultFileLog, LOG_SYSTEM,
        u"[listener] Listening on port %d; socket limit %d.",
        config.serverPort, config.socketLimit);
    if (config.useReportMail) {
        CMailReport::Instance()->SendMail(
            "AuthGateD Started",
            "AuthGateD Started Successfully.");
    }
    return true;
}

// AuthGateD.exe 0x004167A0, AuthGateSvcMgr.cpp.
void CAuthGateSvcMgr::Stop() {
    runtime::raw_atomic::StoreBool(&m_bOn, false);
    if (g_serviceManager.IsServiceMode() != 1) {
        CWindowMgr::g_pWindowMgr->KillTimer(kMonitorTimer);
    }
    CWindowMgr::g_pWindowMgr->KillTimer(kReportTimer);
    CWindowMgr::g_pWindowMgr->KillTimer(kSocketTimeoutTimer);
    m_ClientSocketContainer.Stop();
    m_lu4Listener.Stop();
    JoinThreads(m_hThread, m_iThreadCount);
    JoinThreads(m_hIOThread, m_iIOThreadCount);
    CleanupGameguardAuth();
    // CFileLog::g_pDefaultFileLog->LogT(
    //     LOG_SYSTEM,
    //     u"AuthGateD Service Stop...")
    // CFileLog::g_pDefaultFileLog->LogT(
    //     LOG_SYSTEM,
    //     u"component=shutdown event=stopped")
    // CFileLog::g_pDefaultFileLog->LogT(
    //     LOG_SYSTEM,
    //     u"[shutdown] Service stopped.")
    // L2_LOG(*CFileLog::g_pDefaultFileLog, LOG_SYSTEM,
    //     u"[shutdown] Service stopped.")
    L2_LOG(*CFileLog::g_pDefaultFileLog, LOG_SYSTEM,
        u"[shutdown] Client listener and service workers stopped; GameGuard cleanup finished.");
    if (config.useReportMail) {
        CMailReport::Instance()->SendMail(
            "AuthGateD Shutdown",
            "AuthGateD shutdown completed.");
    }
}

// AuthGateD.exe 0x00416890, AuthGateSvcMgr.cpp.
void CAuthGateSvcMgr::Reconnect() {
    if (!m_authSocket.Connect(config.authAddr, config.authPort)) {
        // CFileLog::g_pDefaultFileLog->Log(
        //     LOG_ERROR,
        //     u"Can't reconnect to AuthD")
        std::array<char16_t, 20> address{};
        // CFileLog::g_pDefaultFileLog->Log(
        //     LOG_ERROR,
        //     u"component=authd event=reconnect_failed peer=%s port=%d retry_seconds=%d",
        //     InetToStr(&config.authAddr, address.data(), address.size()), config.authPort, config.authReconnectInterval)
        // CFileLog::g_pDefaultFileLog->Log(
        //     LOG_ERROR,
        //     u"[AuthD] Could not reconnect to %s:%d. Retrying in %d seconds.",
        //     InetToStr(&config.authAddr, address.data(), address.size()), config.authPort, config.authReconnectInterval)
        // L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
        //     u"[AuthD] Could not reconnect to %s:%d. Retrying in %d seconds.",
        //     InetToStr(&config.authAddr, address.data(), address.size()), config.authPort, config.authReconnectInterval)
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[authd] Could not reconnect to %s:%d. The reconnect timer remains enabled (configured interval: %d seconds).",
            InetToStr(&config.authAddr, address.data(), address.size()), config.authPort, config.authReconnectInterval);
        return;
    }
    CWindowMgr::g_pWindowMgr->KillTimer(kAuthReconnectTimer);
    m_authSocket.Reset();
    // m_authSocket.Initialize(m_hIOCP)
    const bool initialized = m_authSocket.Initialize(m_hIOCP);
    m_ClientSocketContainer.Restart();
    if (config.lu4Port > 0) {
        m_lu4Listener.Restart();
    }
    std::array<char16_t, 20> address{};
    // L2_LOG(*CFileLog::g_pDefaultFileLog, initialized ? LOG_NORMAL : LOG_ERROR,
    //     u"[authd] Reconnect to %s:%d: socket initialization %s. Client accepts were resumed and replacement accepts requested. Listener readiness is not confirmed by this step.",
    //     InetToStr(&config.authAddr, address.data(), address.size()), config.authPort,
    //     initialized ? u"completed" : u"failed and requested connection closure")
    L2_LOG(*CFileLog::g_pDefaultFileLog, initialized ? LOG_NORMAL : LOG_ERROR,
        u"[authd] Reconnect to %s:%d: socket initialization %s. Resumed client acceptance and requested replacement accepts.",
        InetToStr(&config.authAddr, address.data(), address.size()), config.authPort,
        initialized ? u"completed" : u"failed and requested connection closure");
}

// AuthGateD.exe 0x00416900, AuthGateSvcMgr.cpp.
void CAuthGateSvcMgr::ReconnectLogD() {
    if (!m_logdSocket.Connect(config.logdAddr, config.logdPort)) {
        // CFileLog::g_pDefaultFileLog->Log(
        //     LOG_ERROR,
        //     u"Can't reconnect to LogD")
        std::array<char16_t, 20> address{};
        // CFileLog::g_pDefaultFileLog->Log(
        //     LOG_ERROR,
        //     u"component=logd event=reconnect_failed peer=%s port=%d. Local log fallback remains active",
        //     InetToStr(&config.logdAddr, address.data(), address.size()), config.logdPort)
        // CFileLog::g_pDefaultFileLog->Log(
        //     LOG_ERROR,
        //     u"[LogD] Could not reconnect to %s:%d. Local logging remains active.",
        //     InetToStr(&config.logdAddr, address.data(), address.size()), config.logdPort)
        // CFileLog::g_pDefaultFileLog->Log(
        //     LOG_WARN,
        //     u"[logd] Could not reconnect to %s:%u. Local-file fallback remains available.",
        //     InetToStr(&config.logdAddr, address.data(), address.size()),
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.logdPort)))
        L2_LOG_WARN(*CFileLog::g_pDefaultFileLog,
            u"[logd] Could not reconnect to %s:%u. Local-file fallback remains available.",
            InetToStr(&config.logdAddr, address.data(), address.size()),
            static_cast<unsigned int>(static_cast<std::uint16_t>(config.logdPort)));
        return;
    }
    CWindowMgr::g_pWindowMgr->KillTimer(kLogReconnectTimer);
    m_logdSocket.Reset();
    // m_logdSocket.Initialize(m_hIOCP)
    if (!m_logdSocket.Initialize(m_hIOCP)) {
        std::array<char16_t, 20> address{};
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[logd] Socket initialization failed after connecting to %s:%u. Connection closure was requested.",
            InetToStr(&config.logdAddr, address.data(), address.size()),
            static_cast<unsigned int>(static_cast<std::uint16_t>(config.logdPort)));
    }
}

// AuthGateD.exe 0x00416960, AuthGateSvcMgr.cpp.
bool CAuthGateSvcMgr::QueueThreadPoolWorkItem(
    void (*callback)(void*),
    void* parameter,
    std::uint32_t delayMilliseconds) {
    return m_threadPool.QueueTimerWorkItem(
        callback,
        parameter,
        delayMilliseconds);
}

// AuthGateD.exe 0x00416970, AuthGateSvcMgr.cpp.
std::uint32_t CAuthGateSvcMgr::IOWorkerProc(
    [[maybe_unused]] void* parameter) {
    // runtime::logging::SetIndexedThreadName("io", authgated::platform::CurrentServiceThreadId())
    // const std::uint32_t threadId =
    //     authgated::platform::CurrentServiceThreadId()
    // Diagnostic numbering only: CurrentServiceThreadId is a hash on POSIX.
    static std::atomic<unsigned long long> nextLogIndex{0};
    runtime::logging::SetIndexedThreadName("io", nextLogIndex.fetch_add(1, std::memory_order_relaxed));
    // CFileLog::g_pDefaultFileLog->LogDbgT(
    //     LOG_NORMAL,
    //     u"begin Thread(%d)",
    //     threadId)
    // CFileLog::g_pDefaultFileLog->LogDbgT(
    //     LOG_NORMAL, u"[runtime] I/O worker started (native thread %u).", threadId)
    // Noise: The prefix identifies this worker. A platform-dependent numeric hash adds no useful context.
    CFileLog::g_pDefaultFileLog->LogDbgT(LOG_NORMAL, u"[runtime] I/O worker started.");
    LegacyCrtSrand(
        static_cast<std::uint32_t>(std::time(nullptr)));

    CAuthGateSvcMgr* const manager = g_pAuthGateSvcMgr;
    while (runtime::raw_atomic::LoadBool(&manager->m_bOn)) {
        runtime::completion_port::Packet packet;
        const auto status = runtime::completion_port::Wait(
            manager->m_hIOCP,
            1000,
            packet);
        if (status != runtime::completion_port::WaitStatus::Packet) {
            continue;
        }
        CStateMonMgr::g_pStateMonMgr->ioThreadCount.fetch_add(
            1,
            std::memory_order_relaxed);
        authgated::platform::DispatchIOCompletion(
            reinterpret_cast<CIOObject*>(packet.completionKey),
            packet.success ? 1 : 0,
            packet.transferred,
            static_cast<_OVERLAPPED*>(packet.overlapped));
        CStateMonMgr::g_pStateMonMgr->ioThreadCount.fetch_sub(
            1,
            std::memory_order_relaxed);
    }

    // CFileLog::g_pDefaultFileLog->LogDbgT(
    //     LOG_NORMAL,
    //     u"terminate Thread(%d)",
    //     threadId)
    // CFileLog::g_pDefaultFileLog->LogDbgT(
    //     LOG_NORMAL, u"[runtime] I/O worker stopped (native thread %u).", threadId)
    CFileLog::g_pDefaultFileLog->LogDbgT(LOG_NORMAL, u"[runtime] I/O worker stopped.");
    return 0;
}

// AuthGateD.exe 0x00416A60, AuthGateSvcMgr.cpp.
std::uint32_t CAuthGateSvcMgr::WorkerProc(
    [[maybe_unused]] void* parameter) {
    // runtime::logging::SetIndexedThreadName("work", authgated::platform::CurrentServiceThreadId())
    // const std::uint32_t threadId =
    //     authgated::platform::CurrentServiceThreadId()
    // Diagnostic numbering only. Worker arguments and native IDs are unchanged.
    static std::atomic<unsigned long long> nextLogIndex{0};
    runtime::logging::SetIndexedThreadName("work", nextLogIndex.fetch_add(1, std::memory_order_relaxed));
    // CFileLog::g_pDefaultFileLog->LogDbgT(
    //     LOG_NORMAL,
    //     u"begin Thread(%d)",
    //     threadId)
    // CFileLog::g_pDefaultFileLog->LogDbgT(
    //     LOG_NORMAL, u"[runtime] Task worker started (native thread %u).", threadId)
    // Noise: The prefix identifies this worker. A platform-dependent numeric hash adds no useful context.
    CFileLog::g_pDefaultFileLog->LogDbgT(LOG_NORMAL, u"[runtime] Task worker started.");
    LegacyCrtSrand(
        static_cast<std::uint32_t>(std::time(nullptr)));

    CAuthGateSvcMgr* const manager = g_pAuthGateSvcMgr;
    while (runtime::raw_atomic::LoadBool(&manager->m_bOn)) {
        runtime::completion_port::Packet packet;
        const auto status = runtime::completion_port::Wait(
            manager->m_hWorkIOCP,
            1000,
            packet);
        if (status != runtime::completion_port::WaitStatus::Packet) {
            continue;
        }
        CStateMonMgr::g_pStateMonMgr->threadCount.fetch_add(
            1,
            std::memory_order_relaxed);
        authgated::platform::DispatchWorkCompletion(
            reinterpret_cast<CIOObject*>(packet.completionKey),
            packet.success ? 1 : 0,
            packet.transferred,
            static_cast<_OVERLAPPED*>(packet.overlapped));
        CStateMonMgr::g_pStateMonMgr->threadCount.fetch_sub(
            1,
            std::memory_order_relaxed);
    }

    // CFileLog::g_pDefaultFileLog->LogDbgT(
    //     LOG_NORMAL,
    //     u"terminate Thread(%d)",
    //     threadId)
    // CFileLog::g_pDefaultFileLog->LogDbgT(
    //     LOG_NORMAL, u"[runtime] Task worker stopped (native thread %u).", threadId)
    CFileLog::g_pDefaultFileLog->LogDbgT(LOG_NORMAL, u"[runtime] Task worker stopped.");
    return 0;
}

// AuthGateD.exe 0x00416B40, AuthGateSvcMgr.cpp.
bool CAuthGateSvcMgr::Init() {
    if (!config.Load(runtime::ConfigFile("etc/config.txt", "config.txt").u16string().c_str())) return false;

    // Portability improvement, not original AuthGateD behavior: create
    // the configured log directory before the file loggers and monitor use
    // it. The original expects the directory to exist already. Keep startup
    // nonfatal on failure and report directly to the console, not the file log.
    if (config.logDirectory && config.logDirectory[0] != u'\0') {
        const std::filesystem::path logDirectory(config.logDirectory);
        std::error_code directoryError;
        std::filesystem::create_directories(logDirectory, directoryError);
        if (directoryError) {
            // runtime::logging::Write(
            //     "authgated", runtime::logging::Level::Error,
            //     "[logging] Cannot create log directory \"" + logDirectory.u8string() +
            //         "\": " + directoryError.message() + " (" +
            //         directoryError.category().name() + " " +
            //         std::to_string(directoryError.value()) + ").")
            runtime::logging::Write(
                "authgated", runtime::logging::Level::Error,
                "[logging] Cannot create log directory \"" + runtime::Utf8Bytes(logDirectory.u8string()) +
                    "\": " + directoryError.message() + " (" +
                    directoryError.category().name() + " " +
                    std::to_string(directoryError.value()) + ").", __FILE__);
        }
    }

    CStateMonMgr::g_pStateMonMgr->SetDirectory(config.logDirectory);
    authGateLog.SetDirectory(config.logDirectory);
    logdLog.SetDirectory(config.logDirectory);
    botLog.SetDirectory(config.logDirectory);
    CFileLog::g_pDefaultFileLog = &authGateLog;
    m_badUsers.Init(
        static_cast<std::uint16_t>(config.maxPenaltySeconds));
    m_iThreadCount = config.numThread;
    m_iIOThreadCount = config.numIOThread;
    m_hWorkIOCP = runtime::completion_port::Create();
    m_hIOCP = runtime::completion_port::Create();

    // CFileLog::g_pDefaultFileLog->LogT(
    //     LOG_SYSTEM,
    //     u"AuthGateD Service Initialize...")
    // CFileLog::g_pDefaultFileLog->LogT(
    //     LOG_SYSTEM,
    //     u"component=startup event=initializing")
    // CFileLog::g_pDefaultFileLog->LogT(
    //     LOG_SYSTEM,
    //     u"[startup] Initializing the service.")
    L2_LOG(*CFileLog::g_pDefaultFileLog, LOG_SYSTEM,
        u"[startup] Initializing the service.");
    config.ReportConfig();
    if (config.lu4Port > 0) {
        std::string lu4Error;
        const auto keyFile = runtime::ConfigFile("etc/lu4_auth_key.bin", "lu4_auth_key.bin");
        if (!lu4::LoadKeyTable(keyFile, lu4Error)) {
            L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog, u"[startup] lu4Port is set but %S. Startup aborted.",
                lu4Error.c_str());
            return false;
        }
        L2_LOG(*CFileLog::g_pDefaultFileLog, LOG_SYSTEM,
            u"[startup] LU4 client login enabled: official Init and the key table from %S.",
            keyFile.string().c_str());
    }
    if (!config.ValidateConfig()) {
        // Noise: ValidateConfig already reports the offending option and abort outcome.
        // CFileLog::g_pDefaultFileLog->Log(
        //     LOG_ERROR,
        //     u"Error while validating config")
        return false;
    }
    runtime::ConfigureSystemMailNetwork(
        config.useReportMail);

    static char blowfishKey[] = "_;5.]94-31==-%xT!^[$";
    m_BlowfishWrapper.Init(
        reinterpret_cast<unsigned char*>(blowfishKey),
        0x15);
    m_RSAKeyPool.Init();
    const std::uint32_t gameGuardError = InitGameguardAuth(
        "",
        config.ggNumActive,
        config.ggUseUpdateTimer,
        static_cast<std::uint32_t>(config.ggUseLog));
    if (gameGuardError != 0) {
        // CFileLog::g_pDefaultFileLog->Log(
        //     LOG_ERROR,
        //     u"Can't initialize gameguard. %d",
        //     gameGuardError)
        // CFileLog::g_pDefaultFileLog->Log(
        //     LOG_ERROR,
        //     u"component=gameguard event=initialization_failed code=%u. Startup continues",
        //     gameGuardError)
        // CFileLog::g_pDefaultFileLog->Log(
        //     LOG_ERROR,
        //     u"[GameGuard] Initialization failed with code %u. Startup continues.",
        //     gameGuardError)
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[GameGuard] Initialization failed with code %u. Startup continues.",
            gameGuardError);
    }

    if (!m_authSocket.Connect(config.authAddr, config.authPort)) {
        // CFileLog::g_pDefaultFileLog->Log(
        //     LOG_ERROR,
        //     u"Can't connect to AuthD")
        std::array<char16_t, 20> address{};
        // CFileLog::g_pDefaultFileLog->Log(
        //     LOG_ERROR,
        //     u"component=authd event=connect_failed peer=%s port=%d retry_seconds=%d",
        //     InetToStr(&config.authAddr, address.data(), address.size()), config.authPort, config.authReconnectInterval)
        // CFileLog::g_pDefaultFileLog->Log(
        //     LOG_ERROR,
        //     u"[AuthD] Could not connect to %s:%d. Retrying in %d seconds.",
        //     InetToStr(&config.authAddr, address.data(), address.size()), config.authPort, config.authReconnectInterval)
        // L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
        //     u"[AuthD] Could not connect to %s:%d. Retrying in %d seconds.",
        //     InetToStr(&config.authAddr, address.data(), address.size()), config.authPort, config.authReconnectInterval)
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[authd] Could not connect to %s:%d. Requesting a reconnect timer with a %d-second interval.",
            InetToStr(&config.authAddr, address.data(), address.size()), config.authPort, config.authReconnectInterval);
        CWindowMgr::g_pWindowMgr->SetTimer(
            kAuthReconnectTimer,
            config.authReconnectInterval * 1000);
    }
    if (config.useLogd) {
        if (m_logdSocket.Connect(config.logdAddr, config.logdPort)) {
            // m_logdSocket.Initialize(m_hIOCP)
            if (!m_logdSocket.Initialize(m_hIOCP)) {
                std::array<char16_t, 20> address{};
                L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
                    u"[logd] Socket initialization failed after connecting to %s:%u during startup. Connection closure was requested; startup continues.",
                    InetToStr(&config.logdAddr, address.data(), address.size()),
                    static_cast<unsigned int>(static_cast<std::uint16_t>(config.logdPort)));
            }
        } else {
            // CFileLog::g_pDefaultFileLog->Log(
            //     LOG_ERROR,
            //     u"Can't connect to LogD")
            std::array<char16_t, 20> address{};
            // CFileLog::g_pDefaultFileLog->Log(
            //     LOG_ERROR,
            //     u"component=logd event=connect_failed peer=%s port=%d. Using local log fallback",
            //     InetToStr(&config.logdAddr, address.data(), address.size()), config.logdPort)
            // CFileLog::g_pDefaultFileLog->Log(
            //     LOG_ERROR,
            //     u"[LogD] Could not connect to %s:%d. Using local logging.",
            //     InetToStr(&config.logdAddr, address.data(), address.size()), config.logdPort)
            // CFileLog::g_pDefaultFileLog->Log(
            //     LOG_ERROR,
            //     u"[logd] Could not connect to %s:%u. Local-file fallback remains available. This startup path does not schedule automatic LogD reconnect.",
            //     InetToStr(&config.logdAddr, address.data(), address.size()),
            //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.logdPort)))
            L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
                u"[logd] Could not connect to %s:%u. Local-file fallback remains available. This startup path does not schedule automatic LogD reconnect.",
                InetToStr(&config.logdAddr, address.data(), address.size()),
                static_cast<unsigned int>(static_cast<std::uint16_t>(config.logdPort)));
            // The shipping call site uses 0x66 here as well, not 0x69.
            CWindowMgr::g_pWindowMgr->SetTimer(
                kAuthReconnectTimer,
                config.authReconnectInterval * 1000);
        }
    }
    if (config.useForbiddenIPList) {
        m_blockedIPList.Load(runtime::ConfigFile(u"etc\\BlockIPs.txt", "BlockIPs.txt").u16string().c_str());
    }
    m_ClientSocketContainer.SetIOCP(m_hIOCP);
    m_lu4Listener.SetIOCP(m_hIOCP);

    m_hIOThread = static_cast<void**>(std::malloc(
        static_cast<std::size_t>(m_iIOThreadCount) * sizeof(void*)));
    for (std::int32_t index = 0; index < m_iIOThreadCount; ++index) {
        m_hIOThread[index] =
            authgated::platform::CreateSuspendedServiceThread(
                IOWorkerProc,
                nullptr);
        if (!m_hIOThread[index]) {
            // L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            //     u"[threads] Cannot create I/O worker %d of %d. Startup continues with this worker unavailable.",
            //     index + 1, m_iIOThreadCount)
            std::array<char, 192> message{};
            std::snprintf(message.data(), message.size(),
                "[threads] Cannot create I/O worker %d of %d. Startup continues with this worker unavailable.",
                index + 1, m_iIOThreadCount);
            runtime::logging::Write("authgated", runtime::logging::Level::Error, message.data(), __FILE__);
        }
    }
    m_hThread = static_cast<void**>(std::malloc(
        static_cast<std::size_t>(m_iThreadCount) * sizeof(void*)));
    for (std::int32_t index = 0; index < m_iThreadCount; ++index) {
        m_hThread[index] =
            authgated::platform::CreateSuspendedServiceThread(
                WorkerProc,
                nullptr);
        if (!m_hThread[index]) {
            // L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            //     u"[threads] Cannot create service worker %d of %d. Startup continues with this worker unavailable.",
            //     index + 1, m_iThreadCount)
            std::array<char, 192> message{};
            std::snprintf(message.data(), message.size(),
                "[threads] Cannot create service worker %d of %d. Startup continues with this worker unavailable.",
                index + 1, m_iThreadCount);
            runtime::logging::Write("authgated", runtime::logging::Level::Error, message.data(), __FILE__);
        }
    }

    if (config.useReportMail) {
        const std::string server = TargetAnsi(config.reportMailServer);
        const std::string sender = TargetAnsi(config.reportMailFrom);
        const std::string receiver = TargetAnsi(config.reportMailTo);
        const std::string appVersion = TargetAnsi(u"2015072901");
        if (!CMailReport::Instance()->Initialize(
                server.c_str(),
                static_cast<std::int16_t>(config.reportMailPort),
                sender.c_str(),
                receiver.c_str(),
                "AuthGateD",
                GetProductVersion(),
                appVersion.c_str())) {
            // CFileLog::g_pDefaultFileLog->Log(
            //     LOG_ERROR,
            //     u"Can't Initialize Report Mail")
            // CFileLog::g_pDefaultFileLog->Log(
            //     LOG_ERROR,
            //     u"component=report_mail event=initialization_failed, startup continues without email reports")
            // CFileLog::g_pDefaultFileLog->Log(
            //     LOG_ERROR,
            //     u"[mail] Report initialization failed. Startup continues without email reports.")
            L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
                u"[mail] Report initialization failed. Startup continues without email reports.");
        }
        CMailReport::Instance()->AddAttachment(runtime::ConfigFile("etc/config.txt", "config.txt").string().c_str());
    }
    return true;
}
