#include "IOServer.h"
#include "Shared/src/ScopedLockGuards.h"

#include "Platform/IOSocketTransport.h"
#include "AccountDB.h"
#include "Log.h"
#include "Thread.h"

#include "Shared/src/Runtime/CompletionPort.h"
#include "Shared/src/Runtime/EventHandle.h"
#include "Shared/src/Runtime/NativeListenerTransport.h"
#include "Shared/src/Runtime/NativeSocketError.h"
#include "Shared/src/Runtime/RawAtomic.h"

#include <cstdint>

#if defined(_WIN32)
#  include <WinSock2.h>
#else
#  include <arpa/inet.h>
#  include <sys/socket.h>
#endif

CServer server;
CServerInt serverInt;
CServerMi serverMi;
// Public symbol contribution 0003:44992 belongs to IOServer.obj in the
// selected PDB even though IOServerEx owns its only compiled accesses.
volatile std::int32_t g_AcceptExThread = 0;

namespace {

constexpr std::uint32_t kInvalidSocket = UINT32_C(0xffffffff);
constexpr int kWouldBlock = 10035;

std::uint32_t AddressValue(in_addr address) noexcept {
    return static_cast<std::uint32_t>(address.s_addr);
}

const unsigned char* AddressOctets(
    const sockaddr_in* address) noexcept {
    return reinterpret_cast<const unsigned char*>(
        &address->sin_addr.s_addr);
}

// void LogAddress(
//     LogType type,
//     const char* format,
//     const sockaddr_in* address) {
//     const unsigned char* const octets =
//         AddressOctets(address)
//     Log.AddLog(
//         type,
//         format,
//         static_cast<int>(octets[0]),
//         static_cast<int>(octets[1]),
//         static_cast<int>(octets[2]),
//         static_cast<int>(octets[3]))
// }
void LogAddress(
    LogType type,
    const char* format,
    const sockaddr_in* address,
    std::uint32_t socket) {
    const unsigned char* const octets = AddressOctets(address);
    // Log.AddLog(type, format,
    //     static_cast<unsigned int>(octets[0]), static_cast<unsigned int>(octets[1]),
    //     static_cast<unsigned int>(octets[2]), static_cast<unsigned int>(octets[3]),
    //     static_cast<unsigned int>(ntohs(address->sin_port)), socket)
    L2_LOG(Log, type, format,
        static_cast<unsigned int>(octets[0]), static_cast<unsigned int>(octets[1]),
        static_cast<unsigned int>(octets[2]), static_cast<unsigned int>(octets[3]),
        static_cast<unsigned int>(ntohs(address->sin_port)), socket);
}

} // namespace

// L2Authd.exe 0x00447930, Src/ioserver.cpp:25-28.
CIOServer::CIOServer()
    : m_hAcceptEvent(nullptr),
      m_hSocket(kInvalidSocket) {}

// L2Authd.exe 0x00447970, Src/ioserver.cpp:31-34.
CIOServer::~CIOServer() {
    Close();
#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    ::WSACloseEvent(static_cast<WSAEVENT>(m_hAcceptEvent));
#else
    runtime::event_handle::Close(m_hAcceptEvent);
#endif
}

// L2Authd.exe 0x004479E0, Src/ioserver.cpp:54-87.
int CIOServer::Create(int port) {
#if defined(_WIN32)
    const SOCKET nativeSocket =
        ::socket(AF_INET, SOCK_STREAM, 0);
    m_hSocket = nativeSocket == INVALID_SOCKET
        ? kInvalidSocket
        : static_cast<std::uint32_t>(nativeSocket);
#else
    const int nativeSocket =
        ::socket(AF_INET, SOCK_STREAM, 0);
    m_hSocket = nativeSocket < 0
        ? kInvalidSocket
        : static_cast<std::uint32_t>(nativeSocket);
#endif
    if (m_hSocket == kInvalidSocket) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "socket error %d",
        //     ::portable::GetNativeSocketLastError())
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=network event=listener_start_failed stage=socket_create port=%u configured_port=%d socket=%u error_domain=winsock_compatible code=%d action=return_failure",
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket,
        //     ::portable::GetNativeSocketLastError())
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[network] Could not create a listener on port %u (configured %d, socket %u. WinSock error %d). Listener startup failed.",
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket,
        //     ::portable::GetNativeSocketLastError())
        L2_LOG_ERROR(Log,
            "[network] Could not create a listener on port %u (configured %d, socket %u; WinSock error %d). Listener startup failed.",
            static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket,
            ::portable::GetNativeSocketLastError());
        return 0;
    }

#if !defined(_WIN32)
    // Host-only restart transport: WinSock accepts the target's immediate
    // bind sequence, while POSIX keeps prior connections in TIME_WAIT.
    const int reuseAddress = 1;
    const int reuseResult = ::setsockopt(
        nativeSocket,
        SOL_SOCKET,
        SO_REUSEADDR,
        &reuseAddress,
        sizeof(reuseAddress));
    if (reuseResult != 0) {
        L2_LOG_WARN(Log,
            "[network] Could not enable address reuse for listener socket %u on port %u (WinSock-compatible error %d). Continuing with bind; immediate restart may fail.",
            m_hSocket, static_cast<unsigned int>(static_cast<std::uint16_t>(port)),
            ::portable::GetNativeSocketLastError());
    }
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(
        static_cast<std::uint16_t>(port));

    const int bindResult = ::bind(
#if defined(_WIN32)
        static_cast<SOCKET>(m_hSocket),
#else
        static_cast<int>(m_hSocket),
#endif
        reinterpret_cast<const sockaddr*>(&address),
        sizeof(address));
    if (bindResult != 0) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "bind error on port %d (%d)",
        //     port,
        //     ::portable::GetNativeSocketLastError())
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=network event=listener_start_failed stage=bind address=0.0.0.0 port=%u configured_port=%d socket=%u error_domain=winsock_compatible code=%d action=return_failure",
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket,
        //     ::portable::GetNativeSocketLastError())
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[network] Could not bind 0.0.0.0:%u (configured port %d, socket %u. WinSock error %d). Closing the listener.",
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket,
        //     ::portable::GetNativeSocketLastError())
        L2_LOG_ERROR(Log,
            "[network] Could not bind 0.0.0.0:%u (configured port %d, socket %u; WinSock error %d). Closing the listener.",
            static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket,
            ::portable::GetNativeSocketLastError());
        Close();
        return 0;
    }

    const int listenResult = ::listen(
#if defined(_WIN32)
        static_cast<SOCKET>(m_hSocket),
#else
        static_cast<int>(m_hSocket),
#endif
        5);
    if (listenResult != 0) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "listen error on port %d (%d)",
        //     port,
        //     ::portable::GetNativeSocketLastError())
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=network event=listener_start_failed stage=listen address=0.0.0.0 port=%u configured_port=%d socket=%u error_domain=winsock_compatible code=%d action=return_failure",
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket,
        //     ::portable::GetNativeSocketLastError())
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[network] Could not listen on 0.0.0.0:%u (configured port %d, socket %u. WinSock error %d). Closing the listener.",
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket,
        //     ::portable::GetNativeSocketLastError())
        L2_LOG_ERROR(Log,
            "[network] Could not listen on 0.0.0.0:%u (configured port %d, socket %u; WinSock error %d). Closing the listener.",
            static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket,
            ::portable::GetNativeSocketLastError());
        Close();
        return 0;
    }

#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    m_hAcceptEvent = reinterpret_cast<void*>(
        ::WSACreateEvent());
    const int selectResult = ::WSAEventSelect(
        static_cast<SOCKET>(m_hSocket),
        static_cast<WSAEVENT>(m_hAcceptEvent),
        FD_ACCEPT);
    if (selectResult == SOCKET_ERROR) {
        L2_LOG_ERROR(Log,
            "[network] Could not subscribe to accept events for listener socket %u on port %u (WinSock error %d). Startup will continue, but accept notifications are not configured.",
            m_hSocket, static_cast<unsigned int>(static_cast<std::uint16_t>(port)),
            ::WSAGetLastError());
    }
    const bool transportReady = true;
#else
    m_hAcceptEvent =
        runtime::event_handle::Create(false, false);
    const bool transportReady =
        m_hAcceptEvent != nullptr &&
        ::portable::BindNativeListenerTransport(
            this,
            m_hSocket,
            [](void* owner) {
                auto* const server =
                    static_cast<CIOServer*>(owner);
                runtime::event_handle::Set(
                    server->m_hAcceptEvent);
            });
#endif

    if (!transportReady ||
        !RegisterEvent(m_hAcceptEvent)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "RegisterWait error on port %d",
        //     port)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=network event=listener_start_failed stage=%s port=%u configured_port=%d socket=%u error_code=unavailable action=close_socket",
        //     !transportReady ? (m_hAcceptEvent == nullptr ? "event_create" : "transport_bind") : "register_event",
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[network] Listener startup failed while %s on port %u (configured %d, socket %u). No error code was provided. Closing the listener.",
        //     !transportReady ? (m_hAcceptEvent == nullptr ? "creating the accept event" : "binding the accept transport") : "registering the accept event",
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket)
        L2_LOG_ERROR(Log,
            "[network] Listener startup failed while %s on port %u (configured %d, socket %u). No error code was provided; closing the listener.",
            !transportReady ? (m_hAcceptEvent == nullptr ? "creating the accept event" : "binding the accept transport") : "registering the accept event",
            static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket);
        Close();
        return 0;
    }
    return 1;
}

// L2Authd.exe 0x00447B50, Src/ioserver.cpp:37-44.
void CIOServer::Close() {
    if (m_hSocket == kInvalidSocket) {
        return;
    }
#if !defined(_WIN32) || INTPTR_MAX != INT32_MAX
    ::portable::UnbindNativeListenerTransport(this);
#endif
    const std::uint32_t socket =
        runtime::raw_atomic::ExchangeU32(
            &m_hSocket,
            kInvalidSocket);
    authd::platform::CloseNativeSocket(socket);
}

// L2Authd.exe 0x00447B90, Src/ioserver.cpp:47-51.
void CIOServer::Stop() {
    const std::uint32_t socket = m_hSocket;
    m_hSocket = kInvalidSocket;
#if !defined(_WIN32) || INTPTR_MAX != INT32_MAX
    ::portable::UnbindNativeListenerTransport(this);
#endif
    authd::platform::CloseNativeSocket(socket);
}

// L2Authd.exe 0x00447BC0, Src/ioserver.cpp:90-114.
void CIOServer::OnIOCallback(
    [[maybe_unused]] int bSuccess,
    [[maybe_unused]] std::uint32_t dwTransferred,
    [[maybe_unused]] _OVERLAPPED* lpOverlapped) {
    sockaddr_in address{};
    const std::int32_t accepted =
        authd::platform::AcceptNativeSocket(
            m_hSocket,
            address);
    if (accepted == -1) {
        const int error =
            ::portable::GetNativeSocketLastError();
        if (error != kWouldBlock &&
            m_hSocket != kInvalidSocket) {
            // Log.AddLog(LOG_ERROR, "accept error: %d", error)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "component=network event=accept_failed listener=world socket=%u error_domain=winsock_compatible code=%d action=retain_listener",
            //     m_hSocket, error)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "[world] Could not accept a connection on socket %u (WinSock error %d). The listener remains open.",
            //     m_hSocket, error)
            L2_LOG_ERROR(Log,
                "[world] Could not accept a connection on socket %u (WinSock error %d). The listener remains open.",
                m_hSocket, error);
        }
        return;
    }

    CIOSocket* const socket = CreateSocket(
        static_cast<std::uint32_t>(accepted),
        &address);
    if (socket == nullptr) {
        // Noise: CServer::CreateSocket reports the registration rejection before this common close path.
        // Log.AddLog(
        //     LOG_ERROR,
        //     "ServerClose:CreateSocket Fail")
        authd::platform::CloseNativeSocket(
            static_cast<std::uint32_t>(accepted));
        return;
    }
        authd::platform::InitializeNativeSocket(
        socket,
        static_cast<std::uint32_t>(accepted),
        g_hIOCompletionPort);
}

// L2Authd.exe 0x00447D30, Src/ioserver.cpp:117-120.
void CIOServer::OnEventCallback() {
#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    ::WSAResetEvent(static_cast<WSAEVENT>(m_hAcceptEvent));
#else
    runtime::event_handle::Reset(m_hAcceptEvent);
    ::portable::AcknowledgeNativeListenerTransport(this);
#endif
    const bool queued = runtime::completion_port::Post(
        g_hIOCompletionPort,
        0,
        reinterpret_cast<std::uintptr_t>(this),
        nullptr);
    if (!queued) {
        L2_LOG_ERROR(Log,
            "[network] Could not queue the accept event for listener socket %u. This callback will not retry the event.",
            m_hSocket);
    }
}

// L2Authd.exe 0x00447D60, Src/ioserver.cpp:125-128.
CServer::CServer()
    : shutdown(false) {
    authd::platform::InitializeCriticalSectionAndSpinCount(
        sockSect,
        4000);
}

// L2Authd.exe 0x00447DE0, Src/ioserver.cpp:131-134.
CServer::~CServer() {
    authd::platform::DeleteCriticalSection(sockSect);
}

// L2Authd.exe 0x00447E20, Src/ioserver.cpp:185-191.
void CServer::Run(
    int port,
    Allocator socketAllocator,
    bool restrictServer) {
    allocator = socketAllocator;
    restrict = restrictServer;
    if (CIOServer::Create(port) != 0) {
        // Log.AddLog(LOG_NORMAL, "server ready on port %d", port)
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "component=network event=listener_ready listener=world address=0.0.0.0 port=%u configured_port=%d socket=%u",
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket)
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "[world] Listening on 0.0.0.0:%u (configured port %d, socket %u).",
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket)
        L2_LOG_INFO(Log,
            "[world] Listening on 0.0.0.0:%u (configured port %d, socket %u).",
            static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket);
    }
    listener = m_hSocket;
}

// L2Authd.exe 0x00447E80, Src/ioserver.cpp:180-182.
bool CServer::InShutdown() {
    return listener == 0;
}

// L2Authd.exe 0x00447EB0, Src/ioserver.cpp:194-204.
CSocketServer* CServer::FindSocket(in_addr address) {
    CSocketServer* socket = nullptr;
    {
        L2_GUARD(sockSect);
        const auto found = socketmap.find(AddressValue(address));
        if (found != socketmap.end()) {
            socket = found->second;
            socket->AddRef();
        }
    }
    return socket;
}

// L2Authd.exe 0x00447F30, Src/ioserver.cpp:137-177.
CIOSocket* CServer::CreateSocket(
    std::uint32_t socket,
    sockaddr_in* address) {
    // LogAddress(
    //     LOG_NORMAL,
    //     "*new world server connection from %d.%d.%d.%d",
    //     address)
    // LogAddress(
    //     LOG_NORMAL,
    //     "component=network event=connection_received peer=world address=%u.%u.%u.%u port=%u socket=%u",
    //     address, socket)
    LogAddress(
        LOG_NORMAL,
        "[world] Received a connection from %u.%u.%u.%u:%u on socket %u.",
        address, socket);
    const int serverId = SetServerSocket(
        address->sin_addr,
        socket);
    if (serverId == 0) {
        // LogAddress(
        //     LOG_ERROR,
        //     "non-registered world server %d.%d.%d.%d",
        //     address)
        // LogAddress(
        //     LOG_WARN,
        //     "component=network event=connection_rejected peer=world address=%u.%u.%u.%u port=%u socket=%u reason=server_not_registered action=close_socket",
        //     address, socket)
        LogAddress(
            LOG_WARN,
            "[world] Rejecting %u.%u.%u.%u:%u on socket %u: no world server is registered for this address. Closing the connection.",
            address, socket);
        return nullptr;
    }
    if (serverId == -1) {
        // LogAddress(
        //     LOG_ERROR,
        //     "ip: %d.%d.%d.%d world server socket already used",
        //     address)
        // LogAddress(
        //     LOG_WARN,
        //     "component=network event=connection_rejected peer=world address=%u.%u.%u.%u port=%u socket=%u reason=server_already_connected action=close_socket",
        //     address, socket)
        LogAddress(
            LOG_WARN,
            "[world] Rejecting %u.%u.%u.%u:%u on socket %u: this world server is already connected. Closing the new connection.",
            address, socket);
        return nullptr;
    }

    CSocketServer* const created = allocator(socket);
    created->serverid = static_cast<char>(serverId);
    created->SetAddress(address->sin_addr);
    {
        L2_GUARD(sockSect);
        socketmap.insert(
            {AddressValue(address->sin_addr), created});
    }
    return created;
}

// L2Authd.exe 0x00448080, Src/ioserver.cpp:223-234.
void CServer::RemoveSocket(in_addr address) {
    {
        L2_GUARD(sockSect);
        const auto found = socketmap.find(AddressValue(address));
        if (found != socketmap.end()) {
            socketmap.erase(found);
        }
        if (socketmap.empty()) {
            accountdb.RemoveAll();
        }
    }
}

// L2Authd.exe 0x00448110, Src/ioserver.cpp:210-220.
bool CServer::GetServerStatus(in_addr address) {
    bool found;
    {
        L2_GUARD(sockSect);
        found =
            socketmap.find(AddressValue(address)) !=
            socketmap.end();
    }
    return found;
}

// L2Authd.exe 0x00448180, Src/ioserver.cpp:237-240.
CServerInt::CServerInt()
    : shutdown(false) {
    authd::platform::InitializeCriticalSectionAndSpinCount(
        sockSect,
        4000);
}

// L2Authd.exe 0x004481C0, Src/ioserver.cpp:243-245.
CServerInt::~CServerInt() {
    authd::platform::DeleteCriticalSection(sockSect);
}

// L2Authd.exe 0x004481F0, Src/ioserver.cpp:291-296.
void CServerInt::Run(int port, Allocator socketAllocator) {
    allocator = socketAllocator;
    if (CIOServer::Create(port) != 0) {
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "interactive server ready on port %d",
        //     port)
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "component=network event=listener_ready listener=admin address=0.0.0.0 port=%u configured_port=%d socket=%u",
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket)
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "[admin] Listening on 0.0.0.0:%u (configured port %d, socket %u).",
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket)
        L2_LOG_INFO(Log,
            "[admin] Listening on 0.0.0.0:%u (configured port %d, socket %u).",
            static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket);
    }
    listener = m_hSocket;
}

// L2Authd.exe 0x00448240, Src/ioserver.cpp:286-288.
bool CServerInt::InShutdown() {
    return listener == 0;
}

// L2Authd.exe 0x00448270, Src/ioserver.cpp:273-283.
CIOSocket* CServerInt::CreateSocket(
    std::uint32_t socket,
    sockaddr_in* address) {
    CSocketInt* const created = allocator(socket);
    created->SetAddress(address->sin_addr);
    // LogAddress(
    //     LOG_NORMAL,
    //     "*new interactive socket connection from %d.%d.%d.%d",
    //     address)
    // LogAddress(
    //     LOG_NORMAL,
    //     "component=network event=connection_received peer=admin address=%u.%u.%u.%u port=%u socket=%u",
    //     address, socket)
    LogAddress(
        LOG_NORMAL,
        "[admin] Received a connection from %u.%u.%u.%u:%u on socket %u.",
        address, socket);
    return created;
}

// L2Authd.exe 0x004482E0, Src/ioserver.cpp:248-270.
void CServerInt::OnIOCallback(
    [[maybe_unused]] int bSuccess,
    [[maybe_unused]] std::uint32_t dwTransferred,
    [[maybe_unused]] _OVERLAPPED* lpOverlapped) {
    sockaddr_in address{};
    const std::int32_t accepted =
        authd::platform::AcceptNativeSocket(
            m_hSocket,
            address);
    if (accepted == -1) {
        const int error =
            ::portable::GetNativeSocketLastError();
        if (error != kWouldBlock &&
            m_hSocket != kInvalidSocket) {
            // Log.AddLog(LOG_ERROR, "accept error: %d", error)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "component=network event=accept_failed listener=admin socket=%u error_domain=winsock_compatible code=%d action=retain_listener",
            //     m_hSocket, error)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "[admin] Could not accept a connection on socket %u (WinSock error %d). The listener remains open.",
            //     m_hSocket, error)
            L2_LOG_ERROR(Log,
                "[admin] Could not accept a connection on socket %u (WinSock error %d). The listener remains open.",
                m_hSocket, error);
        }
        return;
    }
    CIOSocket* const socket = CreateSocket(
        static_cast<std::uint32_t>(accepted),
        &address);
    if (socket == nullptr) {
        authd::platform::CloseNativeSocket(
            static_cast<std::uint32_t>(accepted));
        return;
    }
    authd::platform::InitializeNativeSocket(
        socket,
        static_cast<std::uint32_t>(accepted),
        g_hIOCompletionPortInt);
}

// L2Authd.exe 0x004483A0, Src/ioserver.cpp:299-302.
CServerMi::CServerMi()
    : shutdown(false) {
    authd::platform::InitializeCriticalSectionAndSpinCount(
        sockSect,
        4000);
}

// L2Authd.exe 0x004483E0, Src/ioserver.cpp:305-307.
CServerMi::~CServerMi() {
    authd::platform::DeleteCriticalSection(sockSect);
}

// L2Authd.exe 0x00448410, Src/ioserver.cpp:353-358.
void CServerMi::Run(int port, Allocator socketAllocator) {
    allocator = socketAllocator;
    if (CIOServer::Create(port) != 0) {
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "monitor server ready on port %d",
        //     port)
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "component=network event=listener_ready listener=monitor address=0.0.0.0 port=%u configured_port=%d socket=%u",
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket)
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "[monitor] Listening on 0.0.0.0:%u (configured port %d, socket %u).",
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket)
        L2_LOG_INFO(Log,
            "[monitor] Listening on 0.0.0.0:%u (configured port %d, socket %u).",
            static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket);
    }
    listener = m_hSocket;
}

// L2Authd.exe 0x00448460, Src/ioserver.cpp:348-350.
bool CServerMi::InShutdown() {
    return listener == 0;
}

// L2Authd.exe 0x00448490, Src/ioserver.cpp:335-345.
CIOSocket* CServerMi::CreateSocket(
    std::uint32_t socket,
    sockaddr_in* address) {
    CSocketMi* const created = allocator(socket);
    created->SetAddress(address->sin_addr);
    // LogAddress(
    //     LOG_NORMAL,
    //     "*new monitor socket connection from %d.%d.%d.%d",
    //     address)
    // LogAddress(
    //     LOG_NORMAL,
    //     "component=network event=connection_received peer=monitor address=%u.%u.%u.%u port=%u socket=%u",
    //     address, socket)
    LogAddress(
        LOG_NORMAL,
        "[monitor] Received a connection from %u.%u.%u.%u:%u on socket %u.",
        address, socket);
    return created;
}

// L2Authd.exe 0x00448500, Src/ioserver.cpp:310-332.
void CServerMi::OnIOCallback(
    [[maybe_unused]] int bSuccess,
    [[maybe_unused]] std::uint32_t dwTransferred,
    [[maybe_unused]] _OVERLAPPED* lpOverlapped) {
    sockaddr_in address{};
    const std::int32_t accepted =
        authd::platform::AcceptNativeSocket(
            m_hSocket,
            address);
    if (accepted == -1) {
        const int error =
            ::portable::GetNativeSocketLastError();
        if (error != kWouldBlock &&
            m_hSocket != kInvalidSocket) {
            // Log.AddLog(LOG_ERROR, "accept error: %d", error)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "component=network event=accept_failed listener=monitor socket=%u error_domain=winsock_compatible code=%d action=retain_listener",
            //     m_hSocket, error)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "[monitor] Could not accept a connection on socket %u (WinSock error %d). The listener remains open.",
            //     m_hSocket, error)
            L2_LOG_ERROR(Log,
                "[monitor] Could not accept a connection on socket %u (WinSock error %d). The listener remains open.",
                m_hSocket, error);
        }
        return;
    }
    CIOSocket* const socket = CreateSocket(
        static_cast<std::uint32_t>(accepted),
        &address);
    if (socket == nullptr) {
        authd::platform::CloseNativeSocket(
            static_cast<std::uint32_t>(accepted));
        return;
    }
    authd::platform::InitializeNativeSocket(
        socket,
        static_cast<std::uint32_t>(accepted),
        g_hIOCompletionPortInt);
}
