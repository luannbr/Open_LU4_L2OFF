#include "IOSocket.h"

#include "Shared/src/ScopedLockGuards.h"

#include "FileLog.h"
#include "Platform/IOSocketTransport.h"

#include "Shared/src/Runtime/CompletionAssociation.h"
#include "Shared/src/Runtime/CompletionPort.h"
#include "Shared/src/Runtime/DirectLog.h"
#include "Shared/src/Runtime/NativeSocketError.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/SocketTransport.h"
#include "Shared/src/Runtime/TickCount32.h"

#include <cstring>

#if defined(_WIN32)
#  include <WinSock2.h>
#  include <Windows.h>
#endif

namespace {

[[maybe_unused]] constexpr int kIoPending = 997;
constexpr int kNotSocket = 10038;
constexpr int kConnectionAborted = 10053;
constexpr int kConnectionReset = 10054;

bool IsExpectedCloseError(int error) noexcept {
    return error == kNotSocket ||
        error == kConnectionAborted ||
        error == kConnectionReset;
}

int LastSocketError() noexcept {
#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    // AuthGateD.exe calls Kernel32 GetLastError after WSARecv/WSASend.
    return static_cast<int>(::GetLastError());
#else
    return ::portable::GetNativeSocketLastError();
#endif
}

// void ReportSocketError(
//     const char16_t* method,
//     int line,
//     const char16_t* operation,
//     int error) {
//     CFileLog::g_pDefaultFileLog->LogT(
//         LOG_ERROR,
//         u"[%s(%d)] - %s(%d)",
//         method,
//         line,
//         operation,
//         error)
// }
// void ReportSocketError(
//     const char16_t* method,
//     int line,
//     const char16_t* operation,
//     int error,
//     std::uint32_t socket) {
void ReportSocketError(
    const char16_t* method,
    const char16_t* operation,
    int error,
    std::uint32_t socket,
    const char16_t* action) {
    // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
    //     u"component=network event=io_submission_failed socket=%u operation=%s error_domain=windows error_code=%d method=%s target_line=%d",
    //     socket, operation, error, method, line)
    // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
    //     u"[network] Socket %u could not submit %s (Windows error %d, %s at target line %d).",
    //     socket, operation, error, method, line)
    // L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
    //     u"[network] Socket %u could not submit %s (Windows error %d, %s at target line %d).",
    //     socket, operation, error, method, line)
#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    constexpr const char16_t* errorSource = u"GetLastError";
#else
    constexpr const char16_t* errorSource = u"portable socket error";
#endif
    L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
        u"[network] %s: socket %u could not submit %s (%s: %d). %s",
        method, socket, operation, errorSource, error, action);
}

// void ReportDifferentWriteCount(
//     std::uint32_t transferred,
//     std::uint32_t expected) {
//     CFileLog::g_pDefaultFileLog->LogT(
//         LOG_ERROR,
//         u"[%s(%d)] - different write count %d != %d",
//         u"CIOSocket::OnWriteCallback",
//         0xae,
//         static_cast<std::int32_t>(transferred),
//         static_cast<std::int32_t>(expected))
// }
void ReportDifferentWriteCount(
    std::uint32_t transferred,
    std::uint32_t expected,
    std::uint32_t socket) {
    // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
    //     u"component=network event=write_count_mismatch socket=%u transferred_bytes=%u expected_bytes=%u method=CIOSocket::OnWriteCallback action=abort_completion",
    //     socket, transferred, expected)
    // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
    //     u"[network] Socket %u wrote %u bytes. Expected %u. Aborting write-completion processing.",
    //     socket, transferred, expected)
    // Original: L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
    //     u"[network] Socket %u wrote %u bytes. Expected %u. Aborting write-completion processing.",
    //     socket, transferred, expected)
    L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
        u"Write completion on socket %u reports %u bytes; expected %u. Returning without advancing the write queue or submitting the next buffer.",
        socket, transferred, expected);
}

// void ReportInvalidReferenceCount(std::int32_t count) {
//     CFileLog::g_pDefaultFileLog->LogT(
//         LOG_ERROR,
//         u"[%s(%d)] - Invalid Reference count(%d)",
//         u"CIOSocket::OnReadCallback",
//         0x9f,
//         count)
// }
// Original: void ReportInvalidReferenceCount(std::int32_t count, std::uint32_t socket) {
void ReportInvalidReferenceCount(std::int32_t count, std::uint32_t socket,
                                 std::uint32_t transferred) {
    // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
    //     u"component=network event=invalid_reference_count socket=%u references=%d method=CIOSocket::OnReadCallback action=skip_read_dispatch",
    //     socket, count)
    // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
    //     u"[network] Socket %u has invalid reference count %d. Skipping read dispatch.",
    //     socket, count)
    // Original: L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
    //     u"[network] Socket %u has invalid reference count %d. Skipping read dispatch.",
    //     socket, count)
    L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
        u"Read completion on socket %u reports %u bytes with invalid reference count %d. Returning without updating the buffered byte count or dispatching the read.",
        socket, transferred, count);
}

void PostSocketCompletion(
    CIOSocket* socket,
    bool success,
    std::uint32_t transferred,
    _OVERLAPPED* overlapped,
    int error) {
    void* const completionPort =
        runtime::completion_association::Find(socket);
    if (runtime::completion_port::PostResult(
            completionPort,
            success,
            transferred,
            reinterpret_cast<std::uintptr_t>(socket),
            overlapped,
            error)) {
        return;
    }
    if (CFileLog::g_pDefaultFileLog) {
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[network] Could not queue an I/O completion (completion port present: %d). Original result: success=%d, transferred=%u bytes, transport error=%d. Running its callback directly.",
            completionPort != nullptr, success ? 1 : 0, transferred, error);
    } else {
        L2_LOG_DIRECT(stderr, runtime::logging::Level::Error,
            "[network] Could not queue an I/O completion (completion port present: %d). Original result: success=%d, transferred=%u bytes, transport error=%d. Running its callback directly.",
            completionPort != nullptr, success ? 1 : 0, transferred, error);
    }
    socket->OnIOCallback(
        success ? 1 : 0,
        transferred,
        overlapped);
}

void ReleaseBufferReference(CIOBuffer* buffer) {
    if (buffer &&
        buffer->m_ref.fetch_sub(
            1,
            std::memory_order_acq_rel) == 1) {
        buffer->Free();
    }
}

bool SubmitWrite(
    CIOSocket* socket,
    [[maybe_unused]] std::uint32_t nativeSocket,
    CIOBuffer* buffer,
    _OVERLAPPED* overlapped) {
#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    WSABUF nativeBuffer{};
    nativeBuffer.len = buffer->m_size;
    nativeBuffer.buf = buffer->m_buffer;
    DWORD transferred = 0;
    const int result = ::WSASend(
        static_cast<SOCKET>(nativeSocket),
        &nativeBuffer,
        1,
        &transferred,
        0,
        overlapped,
        nullptr);
    return result == 0 || LastSocketError() == kIoPending;
#else
    const ::portable::SocketWriteSubmission submission =
        ::portable::WriteSocketTransport(
            socket,
            reinterpret_cast<const std::uint8_t*>(
                buffer->m_buffer),
            static_cast<int>(buffer->m_size),
            [socket, overlapped](
                bool success,
                std::uint32_t transferred,
                int error) {
                PostSocketCompletion(
                    socket,
                    success,
                    transferred,
                    overlapped,
                    error);
            });
    if (submission.IsFailed()) return false;
    if (submission.IsCompleted()) {
        PostSocketCompletion(
            socket,
            true,
            submission.transferred,
            overlapped,
            0);
    }
    return true;
#endif
}

} // namespace

// AuthGateD.exe 0x0040BD50, IOSocket.cpp.
CIOSocket::CIOSocket()
    : m_lock(u"CIOSocket::m_lock", false) {
    m_hSocket = authgated::platform::kInvalidSocket;
    std::memset(
        &m_overlappedRead,
        0,
        sizeof(m_overlappedRead));
    std::memset(
        &m_overlappedWrite,
        0,
        sizeof(m_overlappedWrite));
    m_pReadBuf = CIOBuffer::Alloc();
    m_nPendingWrite = 0;
    m_pLastBuf = nullptr;
    m_pFirstBuf = nullptr;
    const std::uint32_t now =
        runtime::TickCount32();
    m_iLastIO = now;
    m_iFirstIO = now;
}

// AuthGateD.exe 0x0040BE00, IOSocket.cpp.
CIOSocket::~CIOSocket() {
    ::portable::UnbindSocketTransport(this);
    runtime::completion_association::Forget(this);
    ReleaseBufferReference(m_pReadBuf);
    while (m_pFirstBuf) {
        CIOBuffer* const buffer = m_pFirstBuf;
        m_pFirstBuf = buffer->m_pNext;
        buffer->Free();
    }
}

// AuthGateD.exe 0x0040BEA0, IOSocket.cpp.
void CIOSocket::Reset() {
    {
        L2_GUARD(m_lock);
        ReleaseBufferReference(m_pReadBuf);
        m_pReadBuf = nullptr;
        while (m_pFirstBuf) {
            CIOBuffer* const buffer = m_pFirstBuf;
            m_pFirstBuf = buffer->m_pNext;
            buffer->Free();
        }
        std::memset(
            &m_overlappedRead,
            0,
            sizeof(m_overlappedRead));
        std::memset(
            &m_overlappedWrite,
            0,
            sizeof(m_overlappedWrite));
        m_pReadBuf = CIOBuffer::Alloc();
        m_nPendingWrite = 0;
        m_pLastBuf = nullptr;
        m_pFirstBuf = nullptr;
        const std::uint32_t now =
            runtime::TickCount32();
        m_iLastIO = now;
        m_iFirstIO = now;
    }
}

// AuthGateD.exe 0x0040BF50, IOSocket.cpp.
bool CIOSocket::Connect(in_addr address, int port) {
    std::uint32_t socket =
        authgated::platform::kInvalidSocket;
    if (!authgated::platform::ConnectNativeSocket(
            this,
            address,
            port,
            socket)) {
        m_hSocket = authgated::platform::kInvalidSocket;
        return false;
    }
    m_hSocket = socket;
    m_addr = address;
    m_port = port;
    return true;
}

// AuthGateD.exe 0x0040C000, IOSocket.cpp.
void CIOSocket::CloseSocket() {
    const std::uint32_t socket =
        runtime::raw_atomic::ExchangeU32(
            &m_hSocket,
            authgated::platform::kInvalidSocket);
    if (socket == authgated::platform::kInvalidSocket) {
        return;
    }
    OnClose(socket);
    authgated::platform::AbortiveCloseSocket(
        this,
        socket);
    ReleaseRef();
}

// AuthGateD.exe 0x0040C060, IOSocket.cpp.
bool CIOSocket::Initialize(void* completionPort) {
#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    int bufferSize = 0;
    ::setsockopt(
        static_cast<SOCKET>(m_hSocket),
        SOL_SOCKET,
        SO_RCVBUF,
        reinterpret_cast<const char*>(&bufferSize),
        sizeof(bufferSize));
    bufferSize = 0;
    ::setsockopt(
        static_cast<SOCKET>(m_hSocket),
        SOL_SOCKET,
        SO_SNDBUF,
        reinterpret_cast<const char*>(&bufferSize),
        sizeof(bufferSize));
    if (::CreateIoCompletionPort(
            reinterpret_cast<HANDLE>(
                static_cast<std::uintptr_t>(m_hSocket)),
            completionPort,
            reinterpret_cast<ULONG_PTR>(this),
            0) == nullptr) {
        const DWORD error = ::GetLastError();
        if (CFileLog::g_pDefaultFileLog) {
            L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
                u"[network] Cannot associate socket %u with its completion port (Win32 error %u). Requesting connection closure.",
                m_hSocket, static_cast<unsigned int>(error));
        } else {
            L2_LOG_DIRECT(stderr, runtime::logging::Level::Error,
                "[network] Cannot associate socket %u with its completion port (Win32 error %u). Requesting connection closure.",
                m_hSocket, static_cast<unsigned int>(error));
        }
        CloseSocket();
        return false;
    }
#else
    if (!::portable::InitializeSocketTransport(this)) {
        // With a non-null owner, false means no bound transport was found.
        if (CFileLog::g_pDefaultFileLog) {
            L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
                u"[network] Cannot initialize socket %u: no bound transport was found. Requesting connection closure.",
                m_hSocket);
        } else {
            L2_LOG_DIRECT(stderr, runtime::logging::Level::Error,
                "[network] Cannot initialize socket %u: no bound transport was found. Requesting connection closure.",
                m_hSocket);
        }
        CloseSocket();
        return false;
    }
    runtime::completion_association::Associate(
        this,
        completionPort);
#endif
    OnCreate();
    return true;
}

// AuthGateD.exe 0x0040C100, IOSocket.cpp.
void CIOSocket::OnIOCallback(
    int success,
    std::uint32_t transferred,
    _OVERLAPPED* overlapped) {
    if (success == 0) {
        if (overlapped == &m_overlappedRead ||
            overlapped == &m_overlappedWrite) {
            CloseSocket();
        }
    } else if (overlapped == &m_overlappedRead) {
        OnReadCallback(transferred);
    } else if (overlapped == &m_overlappedWrite) {
        OnWriteCallback(transferred);
    }
    ReleaseRef();
}

// AuthGateD.exe 0x0040F500, IOSocket.cpp (folded no-op body).
void CIOSocket::OnClose(
    [[maybe_unused]] std::uint32_t socket) {}

// AuthGateD.exe 0x0040C0F0, IOSocket.cpp.
void CIOSocket::OnCreate() {
    OnRead();
}

// AuthGateD.exe 0x004179F0, IOSocket.cpp (folded no-op body).
void CIOSocket::OnRead() {}

// AuthGateD.exe 0x0040C170, IOSocket.cpp.
void CIOSocket::OnReadCallback(
    std::uint32_t transferred) {
    if (transferred == 0) {
        CloseSocket();
        return;
    }
    const std::int32_t referenceCount =
        m_refcount.load(std::memory_order_relaxed);
    if (referenceCount < 1) {
        // ReportInvalidReferenceCount(referenceCount)
        // Original: ReportInvalidReferenceCount(referenceCount, m_hSocket)
        ReportInvalidReferenceCount(referenceCount, m_hSocket, transferred);
        return;
    }
    m_pReadBuf->m_size += transferred;
    OnRead();
}

// AuthGateD.exe 0x0040C1C0, IOSocket.cpp.
void CIOSocket::OnWriteCallback(
    std::uint32_t transferred) {
    std::uint32_t expected = 0;
    CIOBuffer* completed = nullptr;
    CIOBuffer* next = nullptr;
    {
        L2_GUARD(m_lock);
        expected = m_pFirstBuf->m_size;
        if (transferred == expected) {
            completed = m_pFirstBuf;
            m_nPendingWrite = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(m_nPendingWrite) -
                completed->m_size);
            m_pFirstBuf = completed->m_pNext;
            if (!m_pFirstBuf) {
                m_pLastBuf = nullptr;
            } else {
                next = m_pFirstBuf;
                AddRef();
            }
        }
    }
    if (transferred != expected) {
        // ReportDifferentWriteCount(
        //     transferred,
        //     expected)
        ReportDifferentWriteCount(transferred, expected, m_hSocket);
        // This extra release is in the compiled function. OnIOCallback then
        // performs its normal completion release as well.
        ReleaseRef();
        return;
    }
    if (next) {
        if (!SubmitWrite(
                this,
                m_hSocket,
                next,
                &m_overlappedWrite)) {
            const int error = LastSocketError();
            if (!IsExpectedCloseError(error)) {
                // ReportSocketError(
                //     u"CIOSocket::OnWriteCallback",
                //     0xc0,
                //     u"WSASend",
                //     error)
                // ReportSocketError(u"CIOSocket::OnWriteCallback", 0xc0, u"WSASend", error, m_hSocket)
                ReportSocketError(u"CIOSocket::OnWriteCallback", u"the next write", error, m_hSocket,
                    u"The next buffer remains queued without resubmission.");
            }
            ReleaseRef();
            completed->Free();
            return;
        }
    }
    completed->Free();
}

// AuthGateD.exe 0x0040C310, IOSocket.cpp.
void CIOSocket::Read(std::uint32_t bytesLeft) {
    m_pReadBuf->m_size -= bytesLeft;
    if (m_pReadBuf->m_ref.load(
            std::memory_order_relaxed) == 1) {
        std::memmove(
            m_pReadBuf->m_buffer,
            m_pReadBuf->m_buffer +
                m_pReadBuf->m_size,
            bytesLeft);
    } else {
        CIOBuffer* const replacement =
            CIOBuffer::Alloc();
        std::memcpy(
            replacement->m_buffer,
            m_pReadBuf->m_buffer +
                m_pReadBuf->m_size,
            bytesLeft);
        ReleaseBufferReference(m_pReadBuf);
        m_pReadBuf = replacement;
    }
    m_pReadBuf->m_size = bytesLeft;
    AddRef();

#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    WSABUF nativeBuffer{};
    nativeBuffer.len = static_cast<ULONG>(
        CIOBuffer::kCapacity - m_pReadBuf->m_size);
    nativeBuffer.buf =
        m_pReadBuf->m_buffer + m_pReadBuf->m_size;
    DWORD transferred = 0;
    DWORD flags = 0;
    const int result = ::WSARecv(
        static_cast<SOCKET>(m_hSocket),
        &nativeBuffer,
        1,
        &transferred,
        &flags,
        &m_overlappedRead,
        nullptr);
    if (result == 0 || LastSocketError() == kIoPending) {
        return;
    }
#else
    if (::portable::ArmSocketReadTransport(
            this,
            CIOBuffer::kCapacity -
                m_pReadBuf->m_size)) {
        return;
    }
#endif

    const int error = LastSocketError();
    if (!IsExpectedCloseError(error)) {
        // ReportSocketError(
        //     u"CIOSocket::Read",
        //     0xe6,
        //     u"WSARecv",
        //     error)
        // ReportSocketError(u"CIOSocket::Read", 0xe6, u"WSARecv", error, m_hSocket)
        ReportSocketError(u"CIOSocket::Read", u"a read", error, m_hSocket,
            u"Closing the connection.");
    }
    CloseSocket();
    ReleaseRef();
}

// AuthGateD.exe 0x0040C460, IOSocket.cpp.
void CIOSocket::Write(CIOBuffer* buffer) {
    if (buffer->m_size == 0) {
        buffer->Free();
        return;
    }

    bool submit = false;
    bool merged = false;
    {
        L2_GUARD(m_lock);
        m_nPendingWrite = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(m_nPendingWrite) +
            buffer->m_size);
        if (!m_pLastBuf) {
            m_pLastBuf = buffer;
            m_pFirstBuf = buffer;
            submit = true;
        } else if (m_pFirstBuf != m_pLastBuf &&
                   m_pLastBuf->m_size + buffer->m_size <=
                       CIOBuffer::kCapacity) {
            std::memcpy(
                m_pLastBuf->m_buffer +
                    m_pLastBuf->m_size,
                buffer->m_buffer,
                buffer->m_size);
            m_pLastBuf->m_size += buffer->m_size;
            merged = true;
        } else {
            m_pLastBuf->m_pNext = buffer;
            m_pLastBuf = buffer;
        }
    }
    if (submit) {
        AddRef();
        if (!SubmitWrite(
                this,
                m_hSocket,
                buffer,
                &m_overlappedWrite)) {
            const int error = LastSocketError();
            if (!IsExpectedCloseError(error)) {
                // ReportSocketError(
                //     u"CIOSocket::Write",
                //     0x104,
                //     u"WSASend",
                //     error)
                // ReportSocketError(u"CIOSocket::Write", 0x104, u"WSASend", error, m_hSocket)
                ReportSocketError(u"CIOSocket::Write", u"a write", error, m_hSocket,
                    u"The buffer remains queued without resubmission.");
            }
            ReleaseRef();
        }
        return;
    }
    if (merged) {
        buffer->Free();
        return;
    }
}

namespace authgated::portable {

struct IOSocketTransportAccess {
    static bool FeedRead(
        CIOSocket* socket,
        const std::uint8_t* data,
        std::size_t length) {
        if (!socket ||
            (!data && length != 0) ||
            runtime::raw_atomic::LoadU32Relaxed(
                &socket->m_hSocket) ==
                authgated::platform::kInvalidSocket) {
            return false;
        }

        CIOBuffer* const buffer =
            socket->m_pReadBuf;
        const std::size_t used = buffer->m_size;
        if (length == 0 ||
            used > CIOBuffer::kCapacity ||
            length > CIOBuffer::kCapacity - used ||
            !::portable::ConsumeSocketReadTransport(socket)) {
            return false;
        }
        std::memcpy(
            buffer->m_buffer + used,
            data,
            length);
        PostSocketCompletion(
            socket,
            true,
            static_cast<std::uint32_t>(length),
            &socket->m_overlappedRead,
            0);
        return true;
    }

    static void FeedTerminal(
        CIOSocket* socket,
        int error) {
        if (!socket ||
            !::portable::ConsumeSocketReadTransport(socket)) {
            return;
        }
        PostSocketCompletion(
            socket,
            error == 0,
            0,
            &socket->m_overlappedRead,
            error);
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

} // namespace authgated::portable
