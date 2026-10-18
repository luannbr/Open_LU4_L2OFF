#include "IOServerEx.h"

#include "Shared/src/ScopedLockGuards.h"

#include "Platform/IOSocketTransport.h"
#include "Shared/src/Runtime/CompletionPort.h"
#include "Shared/src/Runtime/EventHandle.h"
#include "Shared/src/Runtime/NativeListenerTransport.h"
#include "Shared/src/Runtime/NativeSocketError.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/TimerQueue.h"
#include "ClientSession.h"
#include "Config.h"
#include "Log.h"
#include "SocketServerEx.h"
#include "Thread.h"

#include <cstdint>
#include <cstring>
#include <utility>

#if defined(_WIN32)
#  include <WinSock2.h>
#  if INTPTR_MAX == INT32_MAX
#    include <MSWSock.h>
#    include <Windows.h>
#  endif
#else
#  include <arpa/inet.h>
#  include <sys/socket.h>
#endif

OverlappedPool::Slot OverlappedPool::m_slot[16];
std::int32_t OverlappedPool::m_alloc = -1;
std::int32_t OverlappedPool::m_free = 0;
OverlappedPool g_overlappedPool;

void* g_hSocketTimer = nullptr;
CIOServerEx serverEx;

namespace {

constexpr std::uint32_t kInvalidSocket = UINT32_C(0xffffffff);
constexpr int kWouldBlock = 10035;

const unsigned char* AddressOctets(
    const sockaddr_in* address) noexcept {
    return reinterpret_cast<const unsigned char*>(
        &address->sin_addr.s_addr);
}

} // namespace

// L2Authd.exe 0x0044AE10, Src/ioserverex.cpp:20-37.
_OverlappedAccept* OverlappedPool::Alloc() {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            runtime::raw_atomic::IncrementI32(
                &m_alloc)) &
        0x0fU;
    Slot& slot = m_slot[slotIndex];
    _OverlappedAccept* overlapped = nullptr;
    {
        L2_GUARD(slot.m_lock);
        overlapped = slot.m_pOverlapped;
        if (overlapped != nullptr) {
            slot.m_pOverlapped = overlapped->next;
        }
    }
    if (overlapped == nullptr) {
        overlapped = new _OverlappedAccept;
    }
    std::memset(
        &overlapped->ol,
        0,
        sizeof(overlapped->ol));
    overlapped->sock = kInvalidSocket;
    overlapped->next = nullptr;
    return overlapped;
}

// L2Authd.exe 0x0044AEC0, Src/ioserverex.cpp:40-47.
void OverlappedPool::Free(
    _OverlappedAccept* overlapped) {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            runtime::raw_atomic::IncrementI32(
                &m_free)) &
        0x0fU;
    Slot& slot = m_slot[slotIndex];
    {
        L2_GUARD(slot.m_lock);
        overlapped->next = slot.m_pOverlapped;
        slot.m_pOverlapped = overlapped;
    }
}

// L2Authd.exe 0x0044AF10, Src/ioserverex.cpp:50-61.
void OverlappedPool::FreeAll() {
    for (Slot& slot : m_slot) {
        {
            L2_GUARD(slot.m_lock);
            while (slot.m_pOverlapped != nullptr) {
                _OverlappedAccept* const overlapped =
                    slot.m_pOverlapped;
                slot.m_pOverlapped = overlapped->next;
                delete overlapped;
            }
        }
    }
}

// L2Authd.exe 0x0044D080, Src/ioserverex.h:29.
OverlappedPool::Slot::Slot()
    : m_pOverlapped(nullptr),
      m_lock(eCustomSpinLock, 0) {}

// L2Authd.exe 0x0044D290, Src/ioserverex.h:38.
OverlappedPool::~OverlappedPool() {
    FreeAll();
}

// L2Authd.exe 0x0044AF90, Src/ioserverex.cpp:64-75.
CIOServerEx::CIOServerEx()
    : m_hSocket(kInvalidSocket),
      m_hAcceptSocket(kInvalidSocket),
      m_acceptEvent(nullptr) {
    g_hSocketTimer = runtime::timer_queue::Create();
    if (g_hSocketTimer == nullptr) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "CIOServerEx Constructor create socket timer fails")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=network event=timer_queue_create_failed listener=authgated action=continue_initialization")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[gateway] Could not create the socket timer queue. Listener initialization will continue.")
        L2_LOG_ERROR(Log,
            "[gateway] Could not create the socket timer queue. Listener initialization will continue.");
    }
    authd::platform::InitializeCriticalSectionAndSpinCount(
        sockSect,
        4000);
}

// L2Authd.exe 0x0044B060, Src/ioserverex.cpp:78-85.
CIOServerEx::~CIOServerEx() {
    Close();
    authd::platform::DeleteCriticalSection(sockSect);
    runtime::timer_queue::Delete(g_hSocketTimer);
    if (m_acceptEvent != nullptr) {
#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
        ::WSACloseEvent(static_cast<WSAEVENT>(m_acceptEvent));
#else
        runtime::event_handle::Close(m_acceptEvent);
#endif
    }
}

// L2Authd.exe 0x0044B110, Src/ioserverex.cpp:206-244.
int CIOServerEx::Create([[maybe_unused]] int port) {
    return 1;
}

// L2Authd.exe 0x0044B130, Src/ioserverex.cpp:251-263.
void CIOServerEx::Close() {
    if (m_hSocket != kInvalidSocket) {
#if !defined(_WIN32) || INTPTR_MAX != INT32_MAX
        ::portable::UnbindNativeListenerTransport(this);
#endif
        const std::uint32_t socket =
            runtime::raw_atomic::ExchangeU32(
                &m_hSocket,
                kInvalidSocket);
        authd::platform::CloseNativeSocket(socket);
    }
    if (m_acceptEvent != nullptr) {
#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
        ::WSACloseEvent(static_cast<WSAEVENT>(m_acceptEvent));
#else
        runtime::event_handle::Close(m_acceptEvent);
#endif
        m_acceptEvent = nullptr;
    }
}

// L2Authd.exe 0x0044B190, Src/ioserverex.cpp:246-248.
void CIOServerEx::Stop() {
    Close();
}

// L2Authd.exe 0x0044B1B0, Src/ioserverex.cpp:334-361.
CSocketServerEx* CIOServerEx::CreateSocket(
    unsigned int socket,
    sockaddr_in* address) {
    const unsigned char gateId = GetFreeGateId();
    if (gateId == 0) {
        // Log.AddLog(LOG_ERROR, "GateId Full")
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=network event=connection_rejected peer=authgated socket=%u reason=gate_id_pool_exhausted configured_limit=%d action=return_without_socket",
        //     socket, config.SocketLimit)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[gateway] Rejecting socket %u: no gateway ID is available (configured socket limit %d). No connection object was created.",
        //     socket, config.SocketLimit)
        // L2_LOG_WARN(Log,
        //     "[gateway] Rejecting socket %u: no gateway ID is available (configured socket limit %d). No connection object was created.",
        //     socket, config.SocketLimit)
        const unsigned char* const octets = AddressOctets(address);
        L2_LOG_WARN(Log,
            "[gateway] Rejecting %u.%u.%u.%u:%u on socket %u: no gateway ID is available (configured socket limit %d). Closing the connection.",
            static_cast<unsigned int>(octets[0]), static_cast<unsigned int>(octets[1]),
            static_cast<unsigned int>(octets[2]), static_cast<unsigned int>(octets[3]),
            static_cast<unsigned int>(ntohs(address->sin_port)), socket, config.SocketLimit);
        return nullptr;
    }

    CSocketServerEx* const created =
        allocator(socket, gateId);
    created->SetAddress(address->sin_addr);
    {
        L2_GUARD(sockSect);
        m_socketmap.insert(
            std::make_pair(gateId, created));
    }

    const unsigned char* const octets =
        AddressOctets(address);
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "*new connection from %d.%d.%d.%d,0x%x",
    //     static_cast<int>(octets[0]),
    //     static_cast<int>(octets[1]),
    //     static_cast<int>(octets[2]),
    //     static_cast<int>(octets[3]),
    //     created->m_hSocket)
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "component=network event=connection_received peer=authgated address=%u.%u.%u.%u port=%u socket=%u gate_id=%u",
    //     static_cast<unsigned int>(octets[0]), static_cast<unsigned int>(octets[1]),
    //     static_cast<unsigned int>(octets[2]), static_cast<unsigned int>(octets[3]),
    //     static_cast<unsigned int>(ntohs(address->sin_port)), created->m_hSocket,
    //     static_cast<unsigned int>(gateId))
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "[gateway] Received a connection from %u.%u.%u.%u:%u on socket %u. Assigned gateway ID %u.",
    //     static_cast<unsigned int>(octets[0]), static_cast<unsigned int>(octets[1]),
    //     static_cast<unsigned int>(octets[2]), static_cast<unsigned int>(octets[3]),
    //     static_cast<unsigned int>(ntohs(address->sin_port)), created->m_hSocket,
    //     static_cast<unsigned int>(gateId))
    L2_LOG_INFO(Log,
        "[gateway] Received a connection from %u.%u.%u.%u:%u on socket %u; assigned gateway ID %u.",
        static_cast<unsigned int>(octets[0]), static_cast<unsigned int>(octets[1]),
        static_cast<unsigned int>(octets[2]), static_cast<unsigned int>(octets[3]),
        static_cast<unsigned int>(ntohs(address->sin_port)), created->m_hSocket,
        static_cast<unsigned int>(gateId));
    return created;
}

// L2Authd.exe 0x0044B290, Src/ioserverex.cpp:88-152.
void CIOServerEx::OnIOCallback(
    int bSuccess,
    [[maybe_unused]] std::uint32_t dwTransferred,
    [[maybe_unused]] _OVERLAPPED* lpOverlapped) {
#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    if (bSuccess == 0) {
        if (lpOverlapped != nullptr) {
            auto* const overlapped =
                reinterpret_cast<_OverlappedAccept*>(
                    lpOverlapped);
            L2_LOG_WARN(Log,
                "[gateway] Accept completion failed on listener socket %u for socket %u; no error code was supplied. Closing the accept socket and queuing a replacement.",
                m_hSocket, overlapped->sock);
            authd::platform::CloseNativeSocket(
                overlapped->sock);
            delete overlapped;
            const BOOL queued = ::QueueUserWorkItem(
                OnAcceptExCallback,
                this,
                0);
            if (queued == FALSE) {
                L2_LOG_ERROR(Log,
                    "[gateway] Could not queue a replacement accept after a failed completion on listener socket %u (Win32 error %u). This callback will not retry submission.",
                    m_hSocket, static_cast<unsigned int>(::GetLastError()));
            }
        } else if (m_hSocket != kInvalidSocket) {
            L2_LOG_ERROR(Log,
                "[gateway] Accept completion failed on listener socket %u without an accept context or error code. No replacement accept can be queued from this callback.",
                m_hSocket);
        }
        return;
    }

    const BOOL queued = ::QueueUserWorkItem(
        OnAcceptExCallback,
        this,
        0);
    if (queued == FALSE) {
        L2_LOG_ERROR(Log,
            "[gateway] Could not queue a replacement accept on listener socket %u (Win32 error %u). Processing the completed connection; this callback will not retry submission.",
            m_hSocket, static_cast<unsigned int>(::GetLastError()));
    }
    auto* const overlapped =
        reinterpret_cast<_OverlappedAccept*>(
            lpOverlapped);
    sockaddr* localAddress = nullptr;
    sockaddr* remoteAddress = nullptr;
    int localAddressLength = 0;
    int remoteAddressLength = sizeof(sockaddr_in);
    ::GetAcceptExSockaddrs(
        overlapped->buffer,
        dwTransferred,
        0x20,
        0x20,
        &localAddress,
        &localAddressLength,
        &remoteAddress,
        &remoteAddressLength);
    CSocketServerEx* const socket = CreateSocket(
        overlapped->sock,
        reinterpret_cast<sockaddr_in*>(remoteAddress));
    if (socket == nullptr) {
        authd::platform::CloseNativeSocket(
            overlapped->sock);
    } else {
        authd::platform::InitializeNativeSocket(
            socket,
            overlapped->sock,
            g_hIOCompletionPort);
    }
    OverlappedPool::Free(overlapped);
#else
    if (bSuccess == 0) {
        return;
    }

    sockaddr_in address{};
    const std::int32_t accepted =
        authd::platform::AcceptNativeSocket(
            m_hSocket,
            address);
    const int acceptError = accepted == -1
        ? ::portable::GetNativeSocketLastError() : 0;
    // AcceptEx posts the replacement operation before it publishes the
    // accepted socket. The readiness backend instead rearms after accept so
    // two IO workers cannot consume the same level-triggered edge.
    WrapAcceptEx();
    if (accepted == -1) {
        // Preserve the accept error across readiness rearming, which may
        // change the thread's last error before this diagnostic is selected.
        // const int error =
        //     ::portable::GetNativeSocketLastError()
        const int error = acceptError;
        if (error != kWouldBlock &&
            m_hSocket != kInvalidSocket) {
            // Log.AddLog(LOG_ERROR, "AcceptEx error %d", error)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "component=network event=accept_failed listener=authgated socket=%u error_domain=winsock_compatible code=%d action=retain_listener",
            //     m_hSocket, acceptError)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "[gateway] Could not accept a connection on socket %u (WinSock error %d). The listener remains open.",
            //     m_hSocket, acceptError)
            L2_LOG_ERROR(Log,
                "[gateway] Could not accept a connection on socket %u (WinSock error %d). The listener remains open.",
                m_hSocket, acceptError);
        }
        return;
    }

    CSocketServerEx* const socket = CreateSocket(
        static_cast<std::uint32_t>(accepted),
        &address);
    if (socket == nullptr) {
        authd::platform::CloseNativeSocket(
            static_cast<std::uint32_t>(accepted));
    } else {
        authd::platform::InitializeNativeSocket(
            socket,
            static_cast<std::uint32_t>(accepted),
            g_hIOCompletionPort);
    }
#endif
}

// L2Authd.exe 0x0044B410, Src/ioserverex.cpp:188-203.
void CIOServerEx::OnEventCallback() {
#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    ::WSAResetEvent(static_cast<WSAEVENT>(m_acceptEvent));
#else
    if (m_acceptEvent != nullptr) {
        runtime::event_handle::Reset(m_acceptEvent);
    }
#endif
    if (WrapAcceptEx()) {
        const std::int32_t count =
            runtime::raw_atomic::IncrementI32(
                &g_AcceptExThread);
        static_cast<void>(count);
        // Noise: Readiness rearming does not add a worker thread. Listener startup and accept failures carry the useful state.
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "AcceptEx Thread Added %d",
        //     count)
    }
}

// L2Authd.exe 0x0044B460, Src/ioserverex.cpp:181-185.
#if defined(_WIN32)
std::uint32_t __stdcall CIOServerEx::OnAcceptExCallback(
    void* parameter) {
#else
std::uint32_t CIOServerEx::OnAcceptExCallback(
    void* parameter) {
#endif
    static_cast<CIOServerEx*>(parameter)->WrapAcceptEx();
    return 0;
}

// L2Authd.exe 0x0044B480, Src/ioserverex.cpp:155-179.
bool CIOServerEx::WrapAcceptEx() {
#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    _OverlappedAccept* const overlapped =
        OverlappedPool::Alloc();
    const SOCKET accepted = ::socket(AF_INET, SOCK_STREAM, 0);
    overlapped->sock = accepted == INVALID_SOCKET
        ? kInvalidSocket
        : static_cast<std::uint32_t>(accepted);
    if (overlapped->sock == kInvalidSocket) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "socket error %d",
        //     ::WSAGetLastError())
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=network event=accept_prepare_failed listener=authgated stage=socket_create socket=%u error_domain=winsock_compatible code=%d action=release_accept_context",
        //     m_hSocket, ::WSAGetLastError())
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[gateway] Could not create an accept socket for listener %u (WinSock error %d). Releasing the accept context.",
        //     m_hSocket, ::WSAGetLastError())
        L2_LOG_ERROR(Log,
            "[gateway] Could not create an accept socket for listener %u (WinSock error %d). Releasing the accept context.",
            m_hSocket, ::WSAGetLastError());
    } else {
        DWORD transferred = 0;
        const BOOL submitted = ::AcceptEx(
            static_cast<SOCKET>(m_hSocket),
            accepted,
            overlapped->buffer,
            0,
            0x20,
            0x20,
            &transferred,
            reinterpret_cast<LPOVERLAPPED>(overlapped));
        if (submitted != FALSE ||
            ::GetLastError() == ERROR_IO_PENDING) {
            return true;
        }
        // Log.AddLog(
        //     LOG_ERROR,
        //     "AcceptEx error %d",
        //     ::WSAGetLastError())
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=network event=accept_prepare_failed listener=authgated stage=accept_submit socket=%u accepted_socket=%u error_domain=winsock_compatible code=%d action=close_accepted_socket",
        //     m_hSocket, overlapped->sock, ::WSAGetLastError())
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[gateway] Could not submit an accept on listener %u for socket %u (WinSock error %d). Closing the accept socket.",
        //     m_hSocket, overlapped->sock, ::WSAGetLastError())
        L2_LOG_ERROR(Log,
            "[gateway] Could not submit an accept on listener %u for socket %u (WinSock error %d). Closing the accept socket.",
            m_hSocket, overlapped->sock, ::WSAGetLastError());
    }

    if (overlapped->sock != kInvalidSocket) {
        authd::platform::CloseNativeSocket(
            overlapped->sock);
    }
    OverlappedPool::Free(overlapped);
    return false;
#else
    if (m_hSocket == kInvalidSocket) {
        return false;
    }
    ::portable::AcknowledgeNativeListenerTransport(this);
    return true;
#endif
}

// L2Authd.exe 0x0044B550, Src/ioserverex.cpp:266-331.
void CIOServerEx::Run(
    int port,
    Allocator socketAllocator) {
    for (unsigned char gateId = 1;
         static_cast<int>(gateId) <= config.SocketLimit &&
             gateId != 0;
         ++gateId) {
        m_GateIdPool.push(gateId);
    }
    allocator = socketAllocator;

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
        //     "acceptex socket error %d",
        //     ::portable::GetNativeSocketLastError())
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=network event=listener_start_failed listener=authgated stage=socket_create address=0.0.0.0 bind_port=%u configured_port=%d socket=%u error_domain=winsock_compatible code=%d action=close_listener",
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket,
        //     ::portable::GetNativeSocketLastError())
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[gateway] Could not create a listener for 0.0.0.0:%u (configured port %d, socket %u. WinSock error %d). Listener startup failed.",
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket,
        //     ::portable::GetNativeSocketLastError())
        L2_LOG_ERROR(Log,
            "[gateway] Could not create a listener for 0.0.0.0:%u (configured port %d, socket %u; WinSock error %d). Listener startup failed.",
            static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket,
            ::portable::GetNativeSocketLastError());
        Close();
        return;
    }

#if !defined(_WIN32)
    // Host-only restart transport. The target WinSock bind sequence needs no
    // option, while POSIX otherwise keeps recently closed connections.
    const int reuseAddress = 1;
    const int reuseResult = ::setsockopt(
        nativeSocket,
        SOL_SOCKET,
        SO_REUSEADDR,
        &reuseAddress,
        sizeof(reuseAddress));
    if (reuseResult != 0) {
        L2_LOG_WARN(Log,
            "[gateway] Could not enable address reuse for listener socket %u on port %u (WinSock-compatible error %d). Continuing with bind; immediate restart may fail.",
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
        //     "bind(port %d) error %d",
        //     port,
        //     ::portable::GetNativeSocketLastError())
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=network event=listener_start_failed listener=authgated stage=bind address=0.0.0.0 bind_port=%u configured_port=%d socket=%u error_domain=winsock_compatible code=%d action=close_listener",
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket,
        //     ::portable::GetNativeSocketLastError())
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[gateway] Could not bind 0.0.0.0:%u (configured port %d, socket %u. WinSock error %d). Closing the listener.",
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket,
        //     ::portable::GetNativeSocketLastError())
        L2_LOG_ERROR(Log,
            "[gateway] Could not bind 0.0.0.0:%u (configured port %d, socket %u; WinSock error %d). Closing the listener.",
            static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket,
            ::portable::GetNativeSocketLastError());
        Close();
        return;
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
        //     "listen error %d",
        //     ::portable::GetNativeSocketLastError())
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=network event=listener_start_failed listener=authgated stage=listen address=0.0.0.0 bind_port=%u configured_port=%d socket=%u error_domain=winsock_compatible code=%d action=close_listener",
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket,
        //     ::portable::GetNativeSocketLastError())
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[gateway] Could not listen on 0.0.0.0:%u (configured port %d, socket %u. WinSock error %d). Closing the listener.",
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket,
        //     ::portable::GetNativeSocketLastError())
        L2_LOG_ERROR(Log,
            "[gateway] Could not listen on 0.0.0.0:%u (configured port %d, socket %u; WinSock error %d). Closing the listener.",
            static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket,
            ::portable::GetNativeSocketLastError());
        Close();
        return;
    }

    bool associated = false;
#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    associated = ::CreateIoCompletionPort(
        reinterpret_cast<HANDLE>(
            static_cast<std::uintptr_t>(m_hSocket)),
        static_cast<HANDLE>(g_hIOCompletionPort),
        reinterpret_cast<ULONG_PTR>(this),
        0) != nullptr;
#else
    if (g_hIOCompletionPort != nullptr) {
        associated = ::portable::BindNativeListenerTransport(
            this,
            m_hSocket,
            [](void* owner) {
                auto* const server =
                    static_cast<CIOServerEx*>(owner);
                if (!runtime::completion_port::Post(
                        g_hIOCompletionPort,
                        0,
                        reinterpret_cast<std::uintptr_t>(server),
                        nullptr)) {
                    // Log.AddLog(LOG_ERROR,
                    //     "component=network event=accept_dispatch_failed listener=authgated socket=%u action=rearm_listener",
                    //     server->m_hSocket)
                    // Log.AddLog(LOG_ERROR,
                    //     "[gateway] Could not dispatch an accept on socket %u. Rearming the listener.",
                    //     server->m_hSocket)
                    L2_LOG_ERROR(Log,
                        "[gateway] Could not dispatch an accept on socket %u. Rearming the listener.",
                        server->m_hSocket);
                    ::portable::AcknowledgeNativeListenerTransport(
                        server);
                }
            });
    } else {
        ::portable::SetNativeSocketLastError(6);
    }
#endif
    if (!associated) {
#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
        const char* associationErrorDomain = "win32";
        const unsigned int associationError = static_cast<unsigned int>(::GetLastError());
#else
        const char* associationErrorDomain = "winsock_compatible";
        const unsigned int associationError = static_cast<unsigned int>(::portable::GetNativeSocketLastError());
#endif
        // Log.AddLog(
        //     LOG_ERROR,
        //     "CreateIoCompletionPort: %d %x %x\n",
        //     ::portable::GetNativeSocketLastError(),
        //     m_hSocket,
        //     runtime::PointerLow32(g_hIOCompletionPort))
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=network event=listener_start_failed listener=authgated stage=completion_transport_associate bind_port=%u configured_port=%d socket=%u completion_port_present=%d error_domain=%s code=%u action=close_listener",
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket,
        //     g_hIOCompletionPort != nullptr, associationErrorDomain, associationError)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[gateway] Could not associate the completion transport on port %u (configured %d, socket %u. Completion port present: %d. %s error %u). Closing the listener.",
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket,
        //     g_hIOCompletionPort != nullptr, associationErrorDomain, associationError)
        L2_LOG_ERROR(Log,
            "[gateway] Could not associate the completion transport on port %u (configured %d, socket %u; completion port present: %d; %s error %u). Closing the listener.",
            static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket,
            g_hIOCompletionPort != nullptr, associationErrorDomain, associationError);
        Close();
        return;
    }

#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    if (config.AcceptCallNum <= 0) {
        L2_LOG_WARN(Log,
            "[gateway] AcceptCallNum is %d for listener socket %u on port %u. No initial AcceptEx operations will be submitted; startup will continue.",
            config.AcceptCallNum, m_hSocket,
            static_cast<unsigned int>(static_cast<std::uint16_t>(port)));
    }
#endif
    for (int call = 0;
         call < config.AcceptCallNum;
         ++call) {
        if (!WrapAcceptEx()) {
            // Log.AddLog(LOG_ERROR,
            //     "component=network event=listener_start_failed listener=authgated stage=initial_accept bind_port=%u configured_port=%d socket=%u accept_index=%d action=close_listener",
            //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket, call)
            // Log.AddLog(LOG_ERROR,
            //     "[gateway] Could not start an initial accept on port %u (configured %d, socket %u. Accept index %d). Closing the listener.",
            //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket, call)
            L2_LOG_ERROR(Log,
                "[gateway] Could not start an initial accept on port %u (configured %d, socket %u; accept index %d). Closing the listener.",
                static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket, call);
            Close();
            return;
        }
    }
    runtime::raw_atomic::StoreI32Relaxed(
        &g_AcceptExThread,
        config.AcceptCallNum);
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "service ready on port %d",
    //     port)
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "component=network event=listener_ready listener=authgated address=0.0.0.0 bind_port=%u configured_port=%d socket=%u configured_accept_operations=%d",
    //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket,
    //     config.AcceptCallNum)
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "[gateway] Listening on 0.0.0.0:%u (configured port %d, socket %u. Configured accept operations: %d).",
    //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket,
    //     config.AcceptCallNum)
    L2_LOG_INFO(Log,
        "[gateway] Listening on 0.0.0.0:%u (configured port %d, socket %u; configured accept operations: %d).",
        static_cast<unsigned int>(static_cast<std::uint16_t>(port)), port, m_hSocket,
        config.AcceptCallNum);
}

// L2Authd.exe 0x0044B740, Src/ioserverex.cpp:377-387.
bool CIOServerEx::RemoveSocket(
    unsigned char gateId) {
    {
        L2_GUARD(sockSect);
        m_socketmap.erase(gateId);
        m_GateIdPool.push(gateId);
    }
    return true;
}

// L2Authd.exe 0x0044B790, Src/ioserverex.cpp:364-374.
CSocketServerEx* CIOServerEx::FindSocket(
    unsigned char gateId) {
    CSocketServerEx* socket = nullptr;
    {
        L2_GUARD(sockSect);
        const auto found = m_socketmap.find(gateId);
        if (found != m_socketmap.end()) {
            socket = found->second;
            socket->AddRef();
        }
    }
    return socket;
}

// L2Authd.exe 0x0044B810, Src/ioserverex.cpp:390-408.
CClientSession* CIOServerEx::FindSession(
    unsigned int sessionId) {
    CSocketServerEx* const socket =
        FindSocket(GetGateId(sessionId));
    if (socket == nullptr) {
        return nullptr;
    }
    CClientSession* const session =
        socket->FindSession(sessionId);
    socket->ReleaseRef();
    return session;
}

// L2Authd.exe 0x0044B8C0, Src/ioserverex.cpp:411-429.
bool CIOServerEx::RemoveSession(
    unsigned int sessionId) {
    CSocketServerEx* const socket =
        FindSocket(GetGateId(sessionId));
    if (socket == nullptr) {
        return false;
    }
    const bool removed = socket->RemoveSession(sessionId);
    socket->ReleaseRef();
    return removed;
}

// L2Authd.exe 0x0044B970, Src/ioserverex.cpp:432-441.
unsigned char CIOServerEx::GetFreeGateId() {
    unsigned char gateId = 0;
    {
        L2_GUARD(sockSect);
        if (!m_GateIdPool.empty()) {
            gateId = m_GateIdPool.front();
            m_GateIdPool.pop();
        }
    }
    return gateId;
}
