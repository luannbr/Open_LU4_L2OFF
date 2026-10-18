#include "IOServer.h"

#include "Shared/src/ScopedLockGuards.h"

#include "FileLog.h"
#include "Platform/IOServerTransport.h"
#include "Platform/IOSocketTransport.h"
#include "StateMonMgr.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/TickCount32.h"

#include <vector>

namespace {

void ReportServerError(
    const char16_t* method,
    [[maybe_unused]] int line,
    const char16_t* operation,
    int error) {
    // CFileLog::g_pDefaultFileLog->LogT(
    //     LOG_ERROR,
    //     u"[%s(%d)] - %s(%d)",
    //     method,
    //     line,
    //     operation,
    //     error)
    // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
    //     u"component=listener event=operation_failed operation=%s error_domain=windows error_code=%d method=%s target_line=%d",
    //     operation, error, method, line)
    // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
    //     u"[listen] %s failed (Windows error %d, %s at target line %d).",
    //     operation, error, method, line)
    // Noise: the shipping source line does not locate the current portable code.
    // L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
    //     u"[listen] %s failed (Windows error %d, %s at target line %d).",
    //     operation, error, method, line)
    L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
        // Portable errors use WinSock-compatible values with unmapped errno fallback.
        // u"[listen] %s failed in %s (Windows error %d).",
        u"[listen] %s failed in %s (socket error %d).",
        operation, method, error);
}

std::int32_t AcceptCallCount() noexcept {
    return CStateMonMgr::g_pStateMonMgr
        ? CStateMonMgr::g_pStateMonMgr->acceptCallCount.load(
              std::memory_order_relaxed)
        : 0;
}

void IncrementAcceptCallCount() noexcept {
    if (CStateMonMgr::g_pStateMonMgr) {
        CStateMonMgr::g_pStateMonMgr->acceptCallCount.fetch_add(
            1,
            std::memory_order_relaxed);
    }
}

void DecrementAcceptCallCount() noexcept {
    if (CStateMonMgr::g_pStateMonMgr) {
        CStateMonMgr::g_pStateMonMgr->acceptCallCount.fetch_sub(
            1,
            std::memory_order_relaxed);
    }
}

std::int32_t SocketCount() noexcept {
    return CStateMonMgr::g_pStateMonMgr
        ? CStateMonMgr::g_pStateMonMgr->socketCount.load(
              std::memory_order_relaxed)
        : 0;
}

} // namespace

// AuthGateD.exe 0x0040B7F0, IOServer.cpp.
CIOServer::CIOServer()
    : m_lock(u"CIOServer::m_lock", true) {
    m_hIOCP = nullptr;
    m_bRun = false;
    m_bPause = false;
    m_hSocket = authgated::platform::kInvalidSocket;
}

// AuthGateD.exe 0x0040B610, IOServer.cpp.
CIOServer::~CIOServer() {
    const std::uint32_t socket =
        runtime::raw_atomic::ExchangeU32(
            &m_hSocket,
            authgated::platform::kInvalidSocket);
    if (socket != authgated::platform::kInvalidSocket) {
        authgated::platform::CloseNativeListener(
            this,
            socket);
    }
}

// AuthGateD.exe 0x0040B0E0, IOServer.cpp.
int CIOServer::SetIOCP(void* completionPort) {
    m_hIOCP = completionPort;
    return 1;
}

// AuthGateD.exe 0x0040B0F0, IOServer.cpp. AcceptEx's pending operation is
// represented by Shared's listener readiness sidecar on portable hosts.
bool CIOServer::WrapAcceptEx() {
    if (m_hSocket == authgated::platform::kInvalidSocket) {
        ReportServerError(
            u"CIOServer::WrapAcceptEx",
            0x8e,
            u"WrapAcceptEx",
            10038);
        return false;
    }
    IncrementAcceptCallCount();
    return true;
}

// AuthGateD.exe 0x0040B200, IOServer.cpp.
bool CIOServer::Run(
    int port,
    SocketAllocator allocator,
    int acceptCallNum,
    int socketLimit) {
    m_iAcceptCallNum = acceptCallNum;
    m_iSocketLimit = socketLimit;
    m_allocator = allocator;

    authgated::platform::ListenerOpenFailure failure;
    if (!authgated::platform::OpenNativeListener(
            this,
            port,
            m_hSocket,
            failure)) {
        switch (failure.stage) {
        case authgated::platform::ListenerOpenStage::Bind:
            ReportServerError(
                u"CIOServer::Run",
                0xb9,
                u"bind",
                failure.error);
            break;
        case authgated::platform::ListenerOpenStage::Listen:
            ReportServerError(
                u"CIOServer::Run",
                0xbe,
                u"listen",
                failure.error);
            break;
        case authgated::platform::ListenerOpenStage::CompletionPort:
            ReportServerError(
                u"CIOServer::Run",
                0xc6,
                u"CreateIoCompletionPort",
                failure.error);
            break;
        case authgated::platform::ListenerOpenStage::None:
        case authgated::platform::ListenerOpenStage::Socket:
            // The shipping socket() failure branch returns false silently.
            // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
            //     u"component=listener event=open_failed port=%d stage=%s error_domain=windows error_code=%d",
            //     port, failure.stage == authgated::platform::ListenerOpenStage::Socket ? u"socket" : u"unavailable",
            //     failure.error)
            // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
            //     u"[listen] Could not open port %d during %s (Windows error %d). Listener startup failed.",
            //     port, failure.stage == authgated::platform::ListenerOpenStage::Socket ? u"socket creation" : u"an unreported stage",
            //     failure.error)
            L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
                // u"[listen] Could not open port %d during %s (Windows error %d). Listener startup failed.",
                u"[listen] Could not open port %d during %s (socket error %d). Listener startup failed.",
                port, failure.stage == authgated::platform::ListenerOpenStage::Socket ? u"socket creation" : u"an unreported stage",
                failure.error);
            break;
        }
        return false;
    }
    for (int index = 0; index < acceptCallNum; ++index) {
        if (!WrapAcceptEx()) return false;
    }
    m_bRun = true;
    return true;
}

// AuthGateD.exe 0x0040B3A0, IOServer.cpp.
CIOSocket* CIOServer::CreateSocket(
    std::uint32_t socket,
    sockaddr_in* address) {
    if (!m_allocator) return nullptr;
    CIOSocket* const object = m_allocator(socket);
    if (!object) return nullptr;
    object->m_addr = address->sin_addr;
    object->m_port = ntohs(address->sin_port);
    AddSocket(socket, object);
    return object;
}

// AuthGateD.exe 0x0040B3F0, IOServer.cpp.
void CIOServer::Restart() {
    m_bPause = false;
    for (std::int32_t pending = AcceptCallCount();
         pending < m_iAcceptCallNum;
         ++pending) {
        if (!WrapAcceptEx()) return;
    }
}

// AuthGateD.exe 0x0040B420, IOServer.cpp.
void CIOServer::OnIOCallback(
    int success,
    [[maybe_unused]] std::uint32_t transferred,
    _OVERLAPPED* overlapped) {
    DecrementAcceptCallCount();
    if (m_bPause || success == 0 || !overlapped) {
        authgated::platform::ReleaseAcceptedSocket(
            overlapped,
            true);
        return;
    }

    if (!WrapAcceptEx()) {
        authgated::platform::ReleaseAcceptedSocket(
            overlapped,
            true);
        return;
    }

    if (m_iSocketLimit <= SocketCount()) {
        // The shipping instructions free the AcceptEx block and then fall
        // through into GetAcceptExSockaddrs, a use-after-free. Rejecting the
        // accepted descriptor is its intended externally visible result.
        authgated::platform::ReleaseAcceptedSocket(
            overlapped,
            true);
        return;
    }

    authgated::platform::AcceptedSocketInfo accepted;
    if (!authgated::platform::GetAcceptedSocketInfo(
            overlapped,
            accepted)) {
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[listen] Accept completed without a valid native socket. Discarding the accepted connection.");
        authgated::platform::ReleaseAcceptedSocket(
            overlapped,
            true);
        return;
    }

    CIOSocket* const socket = CreateSocket(
        accepted.targetSocket,
        &accepted.remoteAddress);
    if (!socket) {
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[listen] Cannot create a connection object for accepted socket %u. Closing the accepted connection.",
            accepted.targetSocket);
        authgated::platform::ReleaseAcceptedSocket(
            overlapped,
            true);
        return;
    }

    std::uint32_t boundSocket =
        authgated::platform::kInvalidSocket;
    if (!authgated::platform::BindNativeSocket(
            socket,
            accepted.nativeSocket,
            boundSocket)) {
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[listen] Cannot bind accepted socket %u to its transport. Removing the connection object.",
            accepted.targetSocket);
        // BindNativeSocket already closed the descriptor. Remove the object
        // created through the exact allocator/AddSocket path before dropping
        // its constructor-held reference.
        authgated::platform::ReleaseAcceptedSocket(
            overlapped,
            false);
        RemoveSocket(accepted.targetSocket);
        socket->m_hSocket =
            authgated::platform::kInvalidSocket;
        socket->ReleaseRef();
        return;
    }
    socket->m_hSocket = boundSocket;
    authgated::platform::ReleaseAcceptedSocket(
        overlapped,
        false);
    socket->Initialize(m_hIOCP);
}

// AuthGateD.exe 0x0040B870, IOServer.cpp.
void CIOServer::AddSocket(
    std::uint32_t socket,
    CIOSocket* object) {
    bool inserted = false;
    {
        L2_GUARD(m_lock);
        inserted =
            m_sockMap.emplace(socket, object).second;
    }
    if (!inserted) {
        // CFileLog::g_pDefaultFileLog->LogT(
        //     LOG_ERROR,
        //     u"[%s(%d)] - m_sockMap.insert, SOCKET(%d)",
        //     u"CIOServer::AddSocket",
        //     0xe6,
        //     static_cast<std::int32_t>(socket))
        // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
        //     u"component=listener event=registration_conflict index=socket socket=%u action=retain_existing_entry",
        //     socket)
        // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
        //     u"[listen] Socket %u conflicts with an existing listener registration. Keeping the existing entry.",
        //     socket)
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[listen] Socket %u conflicts with an existing listener registration. Keeping the existing entry.",
            socket);
    }
}

// AuthGateD.exe 0x0040B8F0, IOServer.cpp.
void CIOServer::CheckSocketTimeout(
    int minutes,
    SocketTimeoutOpt option) {
    const std::uint32_t threshold =
        static_cast<std::uint32_t>(minutes) *
        UINT32_C(60000);
    const std::uint32_t now =
        runtime::TickCount32();
    std::vector<CIOSocket*> expired;

    {
        L2_GUARD(m_lock);
        for (auto iterator = m_sockMap.begin();
             iterator != m_sockMap.end();) {
            CIOSocket* const socket = iterator->second;
            const std::uint32_t reference =
                option == ST_CONNECT
                ? socket->m_iFirstIO
                : socket->m_iLastIO;
            if ((option == ST_CONNECT ||
                 option == ST_LAST_IO) &&
                threshold < now - reference) {
                socket->AddRef();
                expired.push_back(socket);
                iterator = m_sockMap.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }

    if (!expired.empty()) {
        // L2_LOG_DEBUG(*CFileLog::g_pDefaultFileLog,
        //     u"[listen] Closing %zu connections exceeding the %u ms limit measured from %s.",
        //     expired.size(), threshold, option == ST_CONNECT ? u"first I/O" : u"last I/O")
        L2_LOG_DEBUG(*CFileLog::g_pDefaultFileLog,
            u"[listen] Closing %llu connections exceeding the %u ms limit measured from %s.",
            static_cast<unsigned long long>(expired.size()), threshold,
            option == ST_CONNECT ? u"first I/O" : u"last I/O");
    }
    for (CIOSocket* socket : expired) {
        socket->CloseSocket();
        socket->ReleaseRef();
    }
}

// AuthGateD.exe 0x0040BB70, IOServer.cpp.
void CIOServer::Stop() {
    m_bRun = false;
    const std::uint32_t listener =
        runtime::raw_atomic::ExchangeU32(
            &m_hSocket,
            authgated::platform::kInvalidSocket);
    if (listener != authgated::platform::kInvalidSocket) {
        authgated::platform::CloseNativeListener(
            this,
            listener);
    }

    std::vector<CIOSocket*> sockets;
    {
        L2_GUARD(m_lock);
        sockets.reserve(m_sockMap.size());
        for (const auto& entry : m_sockMap) {
            entry.second->AddRef();
            sockets.push_back(entry.second);
        }
        m_sockMap.clear();
    }

    for (CIOSocket* socket : sockets) {
        socket->CloseSocket();
        socket->ReleaseRef();
    }
}

// AuthGateD.exe 0x0040BD20, IOServer.cpp.
bool CIOServer::RemoveSocket(std::uint32_t socket) {
    {
        L2_GUARD(m_lock);
        m_sockMap.erase(socket);
    }
    return true;
}
