#include "IOSocket.h"

#include "Shared/src/ScopedLockGuards.h"

#include "Log.h"

#include "Shared/src/Runtime/CompletionPort.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/SocketTransport.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <vector>

#if defined(_WIN32)
#include <WinSock2.h>
#include <Windows.h>
#endif

CPacketServerPool::CSlot CPacketServerPool::g_slot[16];
std::int32_t CPacketServerPool::g_nAlloc = -1;
std::int32_t CPacketServerPool::g_nFree = 0;
std::int32_t CPacketServer::g_nPendingPacket = 0;
CPacketServerPool thePacketPool;

namespace {

constexpr std::uint32_t kInvalidSocket = UINT32_C(0xffffffff);
constexpr int kNotSocket = 10038;
constexpr int kConnectionAborted = 10053;
constexpr int kConnectionReset = 10054;

struct CompletionAssociation {
    CIOSocket* socket = nullptr;
    void* completionPort = nullptr;
};

struct CompletionAssociationRegistry {
    std::mutex mutex;
    std::vector<CompletionAssociation> associations;
};

CompletionAssociationRegistry& CompletionAssociations() {
    // Like CreateIoCompletionPort associations in the target, these entries
    // outlive individual operations and disappear only with their socket.
    static auto* const registry =
        new CompletionAssociationRegistry;
    return *registry;
}

[[maybe_unused]] void AssociateCompletionPort(
    CIOSocket* socket,
    void* completionPort) {
    CompletionAssociationRegistry& registry =
        CompletionAssociations();
    std::lock_guard<std::mutex> guard(
        registry.mutex);
    const auto found = std::find_if(
        registry.associations.begin(),
        registry.associations.end(),
        [socket](const CompletionAssociation& association) {
            return association.socket == socket;
        });
    if (found != registry.associations.end()) {
        found->completionPort = completionPort;
    } else {
        registry.associations.push_back(
            {socket, completionPort});
    }
}

void ForgetCompletionPort(CIOSocket* socket) {
    CompletionAssociationRegistry& registry =
        CompletionAssociations();
    std::lock_guard<std::mutex> guard(
        registry.mutex);
    const auto found = std::find_if(
        registry.associations.begin(),
        registry.associations.end(),
        [socket](const CompletionAssociation& association) {
            return association.socket == socket;
        });
    if (found != registry.associations.end()) {
        registry.associations.erase(found);
    }
}

void* FindCompletionPort(CIOSocket* socket) {
    CompletionAssociationRegistry& registry =
        CompletionAssociations();
    std::lock_guard<std::mutex> guard(
        registry.mutex);
    const auto found = std::find_if(
        registry.associations.begin(),
        registry.associations.end(),
        [socket](const CompletionAssociation& association) {
            return association.socket == socket;
        });
    return found != registry.associations.end()
        ? found->completionPort
        : nullptr;
}

#if !defined(_WIN32)
int WinSockCompatibleError(int error) noexcept {
    switch (error) {
    case 0:
        return 0;
    case EINTR:
        return 10004;
#if EAGAIN != EWOULDBLOCK
    case EAGAIN:
#endif
    case EWOULDBLOCK:
        return 10035;
    case EBADF:
    case ENOTSOCK:
        return kNotSocket;
    case EINVAL:
        return 10022;
    case ECONNABORTED:
        return kConnectionAborted;
    case ECONNRESET:
    case EPIPE:
        return kConnectionReset;
    case ENOBUFS:
    case ENOMEM:
        return 10055;
    case ETIMEDOUT:
        return 10060;
    case ECONNREFUSED:
        return 10061;
    case ENETUNREACH:
        return 10051;
    case EHOSTUNREACH:
        return 10065;
    default:
        return error;
    }
}
#endif

[[maybe_unused]] int LastSocketError() noexcept {
#if defined(_WIN32)
#if INTPTR_MAX == INT32_MAX
    // The selected EXE calls Kernel32 GetLastError after WSARecv/WSASend.
    return static_cast<int>(::GetLastError());
#else
    return ::WSAGetLastError();
#endif
#else
    return WinSockCompatibleError(errno);
#endif
}

bool IsExpectedCloseError(int error) noexcept {
    return error == kNotSocket ||
        error == kConnectionReset ||
        error == kConnectionAborted;
}

void PostSocketCompletion(
    CIOSocket* socket,
    bool success,
    std::uint32_t transferred,
    _OVERLAPPED* overlapped,
    int error,
    const char* operation) {
    if (!success && socket->m_hSocket != kInvalidSocket) {
        // Log.AddLog(IsExpectedCloseError(error) ? LOG_NORMAL : LOG_WARN,
        //     "component=network event=io_completion_failed socket=%u transferred_bytes=%u error_domain=winsock_compatible code=%d action=dispatch_close_callback",
        //     socket->m_hSocket, transferred, error)
        // Log.AddLog(IsExpectedCloseError(error) ? LOG_NORMAL : LOG_WARN,
        //     "[network] Socket %u I/O failed after %u bytes (WinSock error %d). Dispatching the close callback.",
        //     socket->m_hSocket, transferred, error)
        // L2_LOG(Log, IsExpectedCloseError(error) ? LOG_NORMAL : LOG_WARN,
        //     "[network] Socket %u I/O failed after %u bytes (WinSock error %d). Dispatching the close callback.",
        //     socket->m_hSocket, transferred, error)
        // The transport helper cannot check CIOSocket's protected overlapped fields.
        // const char* operation = overlapped == &socket->m_overlappedRead ? "Read"
        //     : overlapped == &socket->m_overlappedWrite ? "Write" : "Unknown I/O operation".
        // L2_LOG(Log, IsExpectedCloseError(error) ? LOG_NORMAL : LOG_WARN,
        //     "[network] %s failed on socket %u (completion reported %u bytes, transport error %d). Dispatching the I/O callback.",
        //     operation, socket->m_hSocket, transferred, error)
        // L2_LOG(Log, IsExpectedCloseError(error) ? LOG_NORMAL : LOG_WARN,
        //     "[network] I/O failed on socket %u (completion reported %u bytes, transport error %d). Dispatching the I/O callback.",
        //     socket->m_hSocket, transferred, error)
        L2_LOG(Log, IsExpectedCloseError(error) ? LOG_NORMAL : LOG_WARN,
            "[network] %s failed on socket %u (completion reported %u bytes, transport error %d). Dispatching the I/O callback.",
            operation, socket->m_hSocket, transferred, error);
    }
    void* const completionPort =
        FindCompletionPort(socket);
    if (runtime::completion_port::PostResult(
            completionPort,
            success,
            transferred,
            reinterpret_cast<std::uintptr_t>(socket),
            overlapped,
            error)) {
        return;
    }
    // Log.AddLog(LOG_ERROR,
    //     "component=network event=completion_dispatch_failed socket=%u completion_port_present=%d action=dispatch_callback_inline",
    //     socket->m_hSocket, completionPort != nullptr)
    // Log.AddLog(LOG_ERROR,
    //     "[network] Could not queue a completion for socket %u (completion port present: %d). Running its callback directly.",
    //     socket->m_hSocket, completionPort != nullptr)
    // L2_LOG_ERROR(Log,
    //     "[network] Could not queue a completion for socket %u (completion port present: %d). Running its callback directly.",
    //     socket->m_hSocket, completionPort != nullptr)
    // L2_LOG_ERROR(Log,
    //     "[network] Could not queue a completion for socket %u (completion port present: %d). "
    //     "Original I/O result: success=%d, transferred=%u bytes, error=%d. Running its callback directly.",
    //     socket->m_hSocket, completionPort != nullptr,
    //     success ? 1 : 0, transferred, error)
    L2_LOG_ERROR(Log,
        "[network] Could not queue a %s completion for socket %u (completion port present: %d). "
        "Original I/O result: success=%d, transferred=%u bytes, transport error=%d. Running its callback directly.",
        operation, socket->m_hSocket, completionPort != nullptr,
        success ? 1 : 0, transferred, error);
    socket->OnIOCallback(
        success ? 1 : 0,
        transferred,
        overlapped);
}

// void LogSocketOperationError(
//     const char* format,
//     const CIOSocket* socket,
//     int error) {
//     Log.AddLog(
//         LOG_ERROR,
//         format,
//         socket->m_hSocket,
//         runtime::PointerLow32(socket),
//         error)
// }
void LogSocketOperationError(
    const char* operation,
    const CIOSocket* socket,
    int error,
    const char* action) {
    // Log.AddLog(LOG_ERROR,
    //     "component=network event=socket_operation_failed operation=%s socket=%u error_domain=winsock_compatible code=%d action=%s",
    //     operation, socket->m_hSocket, error, action)
    // Log.AddLog(LOG_ERROR,
    //     "[network] Could not %s on socket %u (WinSock error %d). %s",
    //     operation, socket->m_hSocket, error, action)
    // L2_LOG_ERROR(Log,
    //     "[network] Could not %s on socket %u (WinSock error %d). %s",
    //     operation, socket->m_hSocket, error, action)
#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    constexpr const char* errorSource = "GetLastError";
#elif defined(_WIN32)
    constexpr const char* errorSource = "WSAGetLastError";
#else
    // WinSockCompatibleError keeps unrecognized errno values unchanged.
    constexpr const char* errorSource = "errno mapped to WinSock where supported";
#endif
    L2_LOG_ERROR(Log,
        "[network] Could not %s on socket %u (%s: %d). %s",
        operation, socket->m_hSocket, errorSource, error, action);
}

} // namespace

// L2Authd.exe 0x0044F830, Src/iosocket.cpp:23-32.
CIOSocket::CIOSocket(std::uint32_t socket)
    : m_pReadBuf(nullptr),
      m_pFirstBuf(nullptr),
      m_pLastBuf(nullptr),
      m_nPendingWrite(0),
      m_hSocket(socket) {
    authd::platform::InitializeCriticalSection(
        m_cs);
    std::fill_n(
        reinterpret_cast<unsigned char*>(
            &m_overlappedRead),
        sizeof(m_overlappedRead),
        static_cast<unsigned char>(0));
    std::fill_n(
        reinterpret_cast<unsigned char*>(
            &m_overlappedWrite),
        sizeof(m_overlappedWrite),
        static_cast<unsigned char>(0));
    m_pReadBuf = CIOBuffer::Alloc();
}

// L2Authd.exe 0x0044F8F0, Src/iosocket.cpp:35-44.
CIOSocket::~CIOSocket() {
    ::portable::UnbindSocketTransport(this);
    ForgetCompletionPort(this);
    authd::platform::DeleteCriticalSection(
        m_cs);
    m_pReadBuf->Release();
    while (m_pFirstBuf != nullptr) {
        CIOBuffer* const buffer = m_pFirstBuf;
        m_pFirstBuf = m_pFirstBuf->m_pNext;
        buffer->Free();
    }
}

// L2Authd.exe 0x0044F990, Src/iosocket.cpp:75-78.
void CIOSocket::OnCreate() {
    OnRead();
}

// L2Authd.exe 0x0044F9B0, Src/iosocket.cpp:81-96.
void CIOSocket::OnIOCallback(
    int bSuccess,
    std::uint32_t dwTransferred,
    _OVERLAPPED* lpOverlapped) {
    if (bSuccess == 0) {
        if (lpOverlapped == &m_overlappedRead ||
            lpOverlapped == &m_overlappedWrite) {
            CloseSocket();
        } else if (m_hSocket != kInvalidSocket) {
            L2_LOG_ERROR(Log,
                "[network] Failed I/O completion for socket %u has an unknown context (reported %u bytes). Leaving the socket open and skipping completion handling.",
                m_hSocket, dwTransferred);
        }
    } else if (lpOverlapped == &m_overlappedRead) {
        OnReadCallback(dwTransferred);
    } else if (lpOverlapped == &m_overlappedWrite) {
        OnWriteCallback(dwTransferred);
    } else if (m_hSocket != kInvalidSocket) {
        L2_LOG_ERROR(Log,
            "[network] Successful I/O completion for socket %u has an unknown context (reported %u bytes). Leaving the socket open and skipping completion handling.",
            m_hSocket, dwTransferred);
    }
    ReleaseRef();
}

// L2Authd.exe 0x0044FA30, Src/iosocket.cpp:99-111.
void CIOSocket::OnReadCallback(
    std::uint32_t dwTransferred) {
    if (dwTransferred == 0) {
        if (m_hSocket != kInvalidSocket) {
            // Log.AddLog(LOG_NORMAL,
            //     "component=network event=peer_closed socket=%u reason=read_eof action=close_connection",
            //     m_hSocket)
            // Log.AddLog(LOG_NORMAL,
            //     "[network] Peer closed socket %u (end of stream). Closing the connection.",
            //     m_hSocket)
            L2_LOG_INFO(Log,
                "[network] Peer closed socket %u (end of stream). Closing the connection.",
                m_hSocket);
        }
        CloseSocket();
    } else if (GetRef() < 1) {
        // Log.AddLog(LOG_ERROR, "Invalid Socket!")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=network event=invalid_socket_reference socket=%u transferred_bytes=%u reason=nonpositive_refcount action=skip_read_dispatch",
        //     m_hSocket, dwTransferred)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[network] Socket %u has a nonpositive reference count after receiving %u bytes. Skipping read dispatch.",
        //     m_hSocket, dwTransferred)
        L2_LOG_ERROR(Log,
            "[network] Socket %u has a nonpositive reference count after receiving %u bytes. Skipping read dispatch.",
            m_hSocket, dwTransferred);
    } else {
        m_pReadBuf->m_size += dwTransferred;
        OnRead();
    }
}

// L2Authd.exe 0x0044FAA0, Src/iosocket.cpp:114-143.
void CIOSocket::OnWriteCallback(
    std::uint32_t dwTransferred) {
    bool mismatch = false;
    CIOBuffer* completed = nullptr;
    CIOBuffer* next = nullptr;
    {
        L2_GUARD(m_cs);
        if (dwTransferred != m_pFirstBuf->m_size) {
            // Log.AddLog(
            //     LOG_ERROR,
            //     "different write count %x(%x) %d != %d",
            //     m_hSocket,
            //     runtime::PointerLow32(this),
            //     dwTransferred,
            //     m_pFirstBuf->m_size)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "component=network event=write_length_mismatch socket=%u transferred_bytes=%u expected_bytes=%u action=retain_buffer_without_resubmission",
            //     m_hSocket, dwTransferred, m_pFirstBuf->m_size)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "[network] Socket %u wrote %u bytes. Expected %u. The buffer remains queued without resubmission.",
            //     m_hSocket, dwTransferred, m_pFirstBuf->m_size)
            L2_LOG_ERROR(Log,
                "[network] Socket %u wrote %u bytes; expected %u. The buffer remains queued without resubmission.",
                m_hSocket, dwTransferred, m_pFirstBuf->m_size);
            mismatch = true;
        } else {
            m_nPendingWrite = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(
                    m_nPendingWrite) -
                m_pFirstBuf->m_size);
            completed = m_pFirstBuf;
            m_pFirstBuf = m_pFirstBuf->m_pNext;
            if (m_pFirstBuf == nullptr) {
                m_pLastBuf = nullptr;
            } else {
                next = m_pFirstBuf;
            }
        }
    }
    if (mismatch) {
        // This additional release is present in the compiled callback. The
        // outer OnIOCallback performs its normal release as well.
        ReleaseRef();
        return;
    }
    if (next != nullptr) {
        AddRef();

#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
        WSABUF nativeBuffer;
        nativeBuffer.len = next->m_size;
        nativeBuffer.buf = next->m_buffer;
        DWORD sent = 0;
        const int result = ::WSASend(
            static_cast<SOCKET>(m_hSocket),
            &nativeBuffer,
            1,
            &sent,
            0,
            &m_overlappedWrite,
            nullptr);
        if (result != 0 &&
            ::GetLastError() != ERROR_IO_PENDING) {
            const int error =
                static_cast<int>(::GetLastError());
            if (!IsExpectedCloseError(error)) {
                // LogSocketOperationError(
                //     "CIOSocket::WriteCallback %x(%x) err=%d",
                //     this,
                //     error)
                // LogSocketOperationError(
                //     "write_next_buffer", this, error, "retain_queue_without_resubmission")
                LogSocketOperationError(
                    "write the next buffer", this, error, "The buffer remains queued without resubmission.");
            }
            ReleaseRef();
        }
#else
        const ::portable::SocketWriteSubmission submission =
            ::portable::WriteSocketTransport(
                this,
                reinterpret_cast<const std::uint8_t*>(
                    next->m_buffer),
                static_cast<int>(next->m_size),
                [this](
                    bool success,
                    std::uint32_t transferred,
                    int error) {
                    PostSocketCompletion(
                        this,
                        success,
                        transferred,
                        &m_overlappedWrite,
                        // error)
                        error, "Write");
                });
        if (submission.IsFailed()) {
            const int error = LastSocketError();
            if (!IsExpectedCloseError(error)) {
                // LogSocketOperationError(
                //     "CIOSocket::WriteCallback %x(%x) err=%d",
                //     this,
                //     error)
                // LogSocketOperationError(
                //     "write_next_buffer", this, error, "retain_queue_without_resubmission")
                LogSocketOperationError(
                    "write the next buffer", this, error, "The buffer remains queued without resubmission.");
            }
            ReleaseRef();
        } else if (submission.IsCompleted()) {
            PostSocketCompletion(
                this,
                true,
                submission.transferred,
                &m_overlappedWrite,
                // 0)
                0, "Write");
        }
#endif
    }
    completed->Free();
}

// L2Authd.exe 0x0044FC30, Src/iosocket.cpp:47-58.
void CIOSocket::CloseSocket() {
    const std::uint32_t socket =
        runtime::raw_atomic::ExchangeU32(
            &m_hSocket,
            kInvalidSocket);
    if (socket == kInvalidSocket) {
        return;
    }

    OnClose();
#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    linger option;
    option.l_onoff = 1;
    option.l_linger = 0;
    // ::setsockopt(
    //     static_cast<SOCKET>(socket),
    //     SOL_SOCKET,
    //     SO_LINGER,
    //     reinterpret_cast<const char*>(
    //         &option),
    //     sizeof(option))
    const int lingerResult = ::setsockopt(
        static_cast<SOCKET>(socket),
        SOL_SOCKET,
        SO_LINGER,
        reinterpret_cast<const char*>(
            &option),
        sizeof(option));
    const int lingerError = lingerResult == SOCKET_ERROR
        ? ::WSAGetLastError()
        : 0;
    // ::closesocket(static_cast<SOCKET>(socket))
    const int closeResult = ::closesocket(
        static_cast<SOCKET>(socket));
    const int closeError = closeResult == SOCKET_ERROR
        ? ::WSAGetLastError()
        : 0;
    if (lingerResult == SOCKET_ERROR) {
        L2_LOG_WARN(Log,
            "[network] Could not enable abortive close for socket %u (WinSock error %d). The close call was still attempted.",
            socket, lingerError);
    }
    if (closeResult == SOCKET_ERROR) {
        L2_LOG_ERROR(Log,
            "[network] Could not close socket %u (WinSock error %d). The socket field remains invalidated.",
            socket, closeError);
    }
#else
    ::portable::CloseSocketTransport(this);
#endif
    ReleaseRef();
}

// L2Authd.exe 0x0044FCA0, Src/iosocket.cpp:61-72.
void CIOSocket::Initialize(void* completionPort) {
#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    int bufferSize = 0;
    // ::setsockopt(
    //     static_cast<SOCKET>(m_hSocket),
    //     SOL_SOCKET,
    //     SO_RCVBUF,
    //     reinterpret_cast<const char*>(
    //         &bufferSize),
    //     sizeof(bufferSize))
    const int receiveBufferResult = ::setsockopt(
        static_cast<SOCKET>(m_hSocket),
        SOL_SOCKET,
        SO_RCVBUF,
        reinterpret_cast<const char*>(
            &bufferSize),
        sizeof(bufferSize));
    bufferSize = 0;
    const int receiveBufferError = receiveBufferResult == SOCKET_ERROR
        ? ::WSAGetLastError()
        : 0;
    // ::setsockopt(
    //     static_cast<SOCKET>(m_hSocket),
    //     SOL_SOCKET,
    //     SO_SNDBUF,
    //     reinterpret_cast<const char*>(
    //         &bufferSize),
    //     sizeof(bufferSize))
    const int sendBufferResult = ::setsockopt(
        static_cast<SOCKET>(m_hSocket),
        SOL_SOCKET,
        SO_SNDBUF,
        reinterpret_cast<const char*>(
            &bufferSize),
        sizeof(bufferSize));
    const int sendBufferError = sendBufferResult == SOCKET_ERROR
        ? ::WSAGetLastError()
        : 0;
    if (receiveBufferResult == SOCKET_ERROR) {
        L2_LOG_WARN(Log,
            "[network] Could not disable the receive buffer for socket %u (WinSock error %d). Initialization will continue with the existing buffer setting.",
            m_hSocket, receiveBufferError);
    }
    if (sendBufferResult == SOCKET_ERROR) {
        L2_LOG_WARN(Log,
            "[network] Could not disable the send buffer for socket %u (WinSock error %d). Initialization will continue with the existing buffer setting.",
            m_hSocket, sendBufferError);
    }
    if (::CreateIoCompletionPort(
            reinterpret_cast<HANDLE>(
                static_cast<std::uintptr_t>(
                    m_hSocket)),
            completionPort,
            reinterpret_cast<ULONG_PTR>(this),
            0) == nullptr) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Initilize CompletionPort Error CloseSocket")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=network event=socket_initialize_failed stage=completion_port_associate socket=%u error_domain=win32 code=%u action=close_connection",
        //     m_hSocket, static_cast<unsigned int>(::GetLastError()))
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[network] Could not associate socket %u with the completion port (Win32 error %u). Closing the connection.",
        //     m_hSocket, static_cast<unsigned int>(::GetLastError()))
        L2_LOG_ERROR(Log,
            "[network] Could not associate socket %u with the completion port (Win32 error %u). Closing the connection.",
            m_hSocket, static_cast<unsigned int>(::GetLastError()));
        CloseSocket();
    } else {
        OnCreate();
    }
#else
    if (!::portable::InitializeSocketTransport(
            this)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Initilize CompletionPort Error CloseSocket")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=network event=socket_initialize_failed stage=transport_initialize socket=%u reason=transport_not_bound action=close_connection",
        //     m_hSocket)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[network] Could not initialize socket %u: no transport is bound. Closing the connection.",
        //     m_hSocket)
        L2_LOG_ERROR(Log,
            "[network] Could not initialize socket %u: no transport is bound. Closing the connection.",
            m_hSocket);
        CloseSocket();
    } else {
        AssociateCompletionPort(
            this,
            completionPort);
        OnCreate();
    }
#endif
}

// L2Authd.exe 0x0044FD40, Src/iosocket.cpp:146-172.
void CIOSocket::Read(std::uint32_t dwLeft) {
    m_pReadBuf->m_size -= dwLeft;
    if (runtime::raw_atomic::LoadI32Relaxed(
            &m_pReadBuf->m_ref) == 1) {
        std::memmove(
            m_pReadBuf->m_buffer,
            m_pReadBuf->m_buffer +
                m_pReadBuf->m_size,
            dwLeft);
    } else {
        CIOBuffer* const replacement =
            CIOBuffer::Alloc();
        std::memcpy(
            replacement->m_buffer,
            m_pReadBuf->m_buffer +
                m_pReadBuf->m_size,
            dwLeft);
        m_pReadBuf->Release();
        m_pReadBuf = replacement;
    }
    m_pReadBuf->m_size = dwLeft;
    AddRef();

#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    WSABUF nativeBuffer;
    nativeBuffer.len =
        static_cast<ULONG>(
            sizeof(m_pReadBuf->m_buffer) -
            m_pReadBuf->m_size);
    nativeBuffer.buf =
        m_pReadBuf->m_buffer +
        m_pReadBuf->m_size;
    DWORD received = 0;
    DWORD flags = 0;
    const int result = ::WSARecv(
        static_cast<SOCKET>(m_hSocket),
        &nativeBuffer,
        1,
        &received,
        &flags,
        &m_overlappedRead,
        nullptr);
    if (result != 0 &&
        ::GetLastError() != ERROR_IO_PENDING) {
        const int error =
            static_cast<int>(::GetLastError());
        if (!IsExpectedCloseError(error)) {
            // LogSocketOperationError(
            //     "CIOSocket::Read %x(%x) err = %d",
            //     this,
            //     error)
            // LogSocketOperationError(
            //     "read", this, error, "close_connection")
            LogSocketOperationError(
                "read", this, error, "Closing the connection.");
        }
        CloseSocket();
        ReleaseRef();
    }
#else
    if (!::portable::ArmSocketReadTransport(
            this,
            sizeof(m_pReadBuf->m_buffer) -
                m_pReadBuf->m_size)) {
        const int error = LastSocketError();
        if (!IsExpectedCloseError(error)) {
            // LogSocketOperationError(
            //     "CIOSocket::Read %x(%x) err = %d",
            //     this,
            //     error)
            // LogSocketOperationError(
            //     "read", this, error, "close_connection")
            LogSocketOperationError(
                "read", this, error, "Closing the connection.");
        }
        CloseSocket();
        ReleaseRef();
    }
#endif
}

// L2Authd.exe 0x0044FED0, Src/iosocket.cpp:175-211.
void CIOSocket::Write(CIOBuffer* buffer) {
    if (buffer->m_size == 0) {
        buffer->Free();
        return;
    }

    bool submit = false;
    bool merged = false;
    {
        L2_GUARD(m_cs);
        m_nPendingWrite = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(
                m_nPendingWrite) +
            buffer->m_size);
        if (m_pLastBuf == nullptr) {
            m_pLastBuf = buffer;
            m_pFirstBuf = buffer;
            submit = true;
        } else if (m_pLastBuf == m_pFirstBuf ||
                   m_pLastBuf->m_size +
                           buffer->m_size >
                       sizeof(m_pLastBuf->m_buffer)) {
            m_pLastBuf->m_pNext = buffer;
            m_pLastBuf = buffer;
        } else {
            std::memcpy(
                m_pLastBuf->m_buffer +
                    m_pLastBuf->m_size,
                buffer->m_buffer,
                buffer->m_size);
            m_pLastBuf->m_size += buffer->m_size;
            merged = true;
        }
    }
    if (submit) {
        AddRef();

#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
        WSABUF nativeBuffer;
        nativeBuffer.len = buffer->m_size;
        nativeBuffer.buf = buffer->m_buffer;
        DWORD sent = 0;
        const int result = ::WSASend(
            static_cast<SOCKET>(m_hSocket),
            &nativeBuffer,
            1,
            &sent,
            0,
            &m_overlappedWrite,
            nullptr);
        if (result != 0 &&
            ::GetLastError() != ERROR_IO_PENDING) {
            const int error =
                static_cast<int>(::GetLastError());
            if (!IsExpectedCloseError(error)) {
                // LogSocketOperationError(
                //     "CIOSocket::Write %x(%x) err=%d",
                //     this,
                //     error)
                // LogSocketOperationError(
                //     "write", this, error, "retain_queue_without_resubmission")
                LogSocketOperationError(
                    "write", this, error, "The buffer remains queued without resubmission.");
            }
            ReleaseRef();
        }
#else
        const ::portable::SocketWriteSubmission submission =
            ::portable::WriteSocketTransport(
                this,
                reinterpret_cast<const std::uint8_t*>(
                    buffer->m_buffer),
                static_cast<int>(buffer->m_size),
                [this](
                    bool success,
                    std::uint32_t transferred,
                    int error) {
                    PostSocketCompletion(
                        this,
                        success,
                        transferred,
                        &m_overlappedWrite,
                        // error)
                        error, "Write");
                });
        if (submission.IsFailed()) {
            const int error = LastSocketError();
            if (!IsExpectedCloseError(error)) {
                // LogSocketOperationError(
                //     "CIOSocket::Write %x(%x) err=%d",
                //     this,
                //     error)
                // LogSocketOperationError(
                //     "write", this, error, "retain_queue_without_resubmission")
                LogSocketOperationError(
                    "write", this, error, "The buffer remains queued without resubmission.");
            }
            ReleaseRef();
        } else if (submission.IsCompleted()) {
            PostSocketCompletion(
                this,
                true,
                submission.transferred,
                &m_overlappedWrite,
                // 0)
                0, "Write");
        }
#endif
    } else if (merged) {
        buffer->Free();
    }
}

// L2Authd.exe 0x004500B0, Src/iosocket.cpp:214-224.
void CIOSocket::Write(
    char* data,
    std::uint32_t size) {
    while (size != 0) {
        CIOBuffer* const buffer =
            CIOBuffer::Alloc();
        const std::uint32_t writeSize =
            size > sizeof(buffer->m_buffer)
            ? static_cast<std::uint32_t>(
                  sizeof(buffer->m_buffer))
            : size;
        std::memcpy(
            buffer->m_buffer,
            data,
            writeSize);
        buffer->m_size = writeSize;
        Write(buffer);
        data += writeSize;
        size -= writeSize;
    }
}

CPacketServer::CPacketServer() = default;
CPacketServer::~CPacketServer() = default;

// L2Authd.exe 0x00450130, Src/iosocket.cpp:251-265.
CPacketServer* CPacketServer::Alloc() {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            runtime::raw_atomic::IncrementI32(
                &CPacketServerPool::g_nAlloc)) &
        0x0fU;
    CPacketServerPool::CSlot& slot =
        CPacketServerPool::g_slot[slotIndex];
    CPacketServer* packet = nullptr;
    {
        L2_GUARD(slot.m_lock);
        packet = slot.m_pPacket;
        if (packet != nullptr) {
            slot.m_pPacket =
                reinterpret_cast<CPacketServer*>(
                    packet->m_pSocket);
        }
    }
    if (packet == nullptr) {
        packet = new CPacketServer;
    }
    return packet;
}

// L2Authd.exe 0x00450200, Src/iosocket.cpp:278-289.
void CPacketServer::FreeAll() {
    for (CPacketServerPool::CSlot& slot :
         CPacketServerPool::g_slot) {
        {
            L2_GUARD(slot.m_lock);
            while (slot.m_pPacket != nullptr) {
                CPacketServer* const packet =
                    slot.m_pPacket;
                slot.m_pPacket =
                    reinterpret_cast<CPacketServer*>(
                        packet->m_pSocket);
                delete packet;
            }
        }
    }
}

// L2Authd.exe 0x00450290, Src/iosocket.cpp:268-275.
void CPacketServer::Free() {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            runtime::raw_atomic::DecrementI32(
                &CPacketServerPool::g_nFree)) &
        0x0fU;
    CPacketServerPool::CSlot& slot =
        CPacketServerPool::g_slot[slotIndex];
    {
        L2_GUARD(slot.m_lock);
        m_pSocket = reinterpret_cast<CIOSocket*>(
            slot.m_pPacket);
        slot.m_pPacket = this;
    }
}

// L2Authd.exe 0x004502E0, Src/iosocket.cpp:292-306.
void CPacketServer::OnIOCallback(
    [[maybe_unused]] int bSuccess,
    std::uint32_t dwTransferred,
    [[maybe_unused]] _OVERLAPPED* lpOverlapped) {
    if (m_pFunc(
            m_pSocket,
            reinterpret_cast<const unsigned char*>(
                m_pBuf->m_buffer +
                dwTransferred + 1))) {
        const std::uint32_t closedSocket = m_pSocket->m_hSocket;
        m_pSocket->CloseSocket();
        // Log.AddLog(
        //     LOG_ERROR,
        //     "ServerClose:PacketServerClose")
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "component=network event=packet_handler_requested_close socket=%u action=close_connection",
        //     closedSocket)
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "[network] The packet handler requested closure of socket %u. Closing the connection.",
        //     closedSocket)
        L2_LOG_INFO(Log,
            "[network] The packet handler requested closure of socket %u. Closing the connection.",
            closedSocket);
    }
    m_pSocket->ReleaseRef();
    m_pBuf->Release();
    runtime::raw_atomic::DecrementI32(
        &g_nPendingPacket);
    Free();
}

// L2Authd.exe 0x00450390, Src/iosocket.cpp:236.
CPacketServerPool::CSlot::CSlot()
    : m_pPacket(nullptr),
      m_lock(eCustomSpinLock, 0) {}

// L2Authd.exe 0x004503E0, Src/iosocket.cpp:241.
CPacketServerPool::~CPacketServerPool() {
    CPacketServer::FreeAll();
}

namespace authd::portable {

struct IOSocketTransportAccess {
    static bool FeedRead(
        CIOSocket* socket,
        const std::uint8_t* data,
        std::size_t length) {
        if (socket == nullptr ||
            (data == nullptr && length != 0) ||
            runtime::raw_atomic::LoadU32Relaxed(
                &socket->m_hSocket) ==
                kInvalidSocket) {
            return false;
        }
        CIOBuffer* const readBuffer =
            socket->m_pReadBuf;
        const std::size_t used =
            readBuffer->m_size;
        if (length == 0 ||
            used > sizeof(readBuffer->m_buffer) ||
            length > sizeof(readBuffer->m_buffer) - used ||
            !::portable::ConsumeSocketReadTransport(
                socket)) {
            return false;
        }
        std::memcpy(
            readBuffer->m_buffer + used,
            data,
            length);
        PostSocketCompletion(
            socket,
            true,
            static_cast<std::uint32_t>(length),
            &socket->m_overlappedRead,
            // 0)
            0, "Read");
        return true;
    }

    static void FeedTerminal(
        CIOSocket* socket,
        int error) {
        if (socket == nullptr ||
            !::portable::ConsumeSocketReadTransport(
                socket)) {
            return;
        }
        PostSocketCompletion(
            socket,
            error == 0,
            0,
            &socket->m_overlappedRead,
            // error)
            error, "Read");
    }
};

bool FeedSocketRead(
    CIOSocket* socket,
    const std::uint8_t* data,
    std::size_t length) {
    return IOSocketTransportAccess::FeedRead(
        socket,
        data,
        length);
}

void FeedSocketTerminal(
    CIOSocket* socket,
    int error) {
    IOSocketTransportAccess::FeedTerminal(
        socket,
        error);
}

} // namespace authd::portable
