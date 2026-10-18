// Archive-granularity companion for the shipping Shared/ioc.cpp owner.
//
// The Windows linker selected process-specific COMDATs from the original
// Shared library. Portable static archives select complete object files, so
// the socket/object prefix used by every server process lives in this small
// translation unit while the NPC-owned worker loop remains in ioc.cpp. The
// public owner, class declarations and target source paths are unchanged.
#include "Shared/src/IOC.h"
#include "Shared/src/MemorySub.h"

#include "Shared/src/Runtime/IOThreadRuntime.h"
#include "Shared/src/Runtime/NativeSocketError.h"
#include "Shared/src/Runtime/NativeSocketTransport.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Log.h"
#include "Shared/src/ThreadManager.h"

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <new>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <WinSock2.h>
#endif

// Process-local Shared/ioc.cpp state used by every server executable. Keep
// it with the archive-granularity socket core so a consumer of CIOSocketEx
// does not pull the recovered L2NPC worker loop.
int g_bTerminating = 0;
std::atomic<std::int32_t> g_nRunningThread{0};


std::atomic<std::int32_t> g_nSocketPendingWrite{0};
std::atomic<std::int32_t> g_ObjectInTimer{0};
std::atomic<std::int32_t> g_ObjectInShortTime{0};

#if defined(L2_BUILD_L2SERVER)
CNewMwsrQueue<DefaultIOBuffer>* g_IOBufferPools[ThreadID::PORTABLE_CAPACITY]{};
CNewMwsrQueue<IOBuffer8k>* g_IOBuffer8kPools[ThreadID::PORTABLE_CAPACITY]{};
CNewMwsrQueue<IOBuffer32k>* g_IOBuffer32kPools[ThreadID::PORTABLE_CAPACITY]{};
#else
CMwsrQueue<DefaultIOBuffer>* g_IOBufferPools[ThreadID::PORTABLE_CAPACITY]{};
CMwsrQueue<IOBuffer8k>* g_IOBuffer8kPools[ThreadID::PORTABLE_CAPACITY]{};
CMwsrQueue<IOBuffer32k>* g_IOBuffer32kPools[ThreadID::PORTABLE_CAPACITY]{};
#endif

std::atomic<std::int32_t> CAbstractPacket::g_nPendingPacket{0};

namespace {

template <typename TQueue>
auto PopBufferFrom(TQueue** pools) -> decltype(pools[0]->Pop()) {
    const int threadId = CThreadManager::GetThreadID();
    return pools[threadId]->Pop();
}

template <typename TQueue, typename TBuffer>
void PushBufferTo(TQueue** pools, TBuffer* buffer) {
    if (!buffer || buffer->m_nOwnThreadID < 0 || buffer->m_nOwnThreadID >= ThreadID::PORTABLE_CAPACITY ||
        !pools[buffer->m_nOwnThreadID]) {
        io_buffer_detail::FailIOBufferAllocation();
    }
    pools[buffer->m_nOwnThreadID]->Push(buffer);
}

template <typename TQueue>
int PoolSizeAt(TQueue** pools, int threadId) {
    if (threadId < 0 || threadId >= ThreadID::PORTABLE_CAPACITY || !pools[threadId]) {
        return 0;
    }
    return pools[threadId]->InstantSize();
}

template <typename TQueue>
int PoolCapacityAt(TQueue** pools, int threadId) {
    if (threadId < 0 || threadId >= ThreadID::PORTABLE_CAPACITY || !pools[threadId]) {
        return 0;
    }
    return pools[threadId]->GetMaxSize();
}

class ScopedIOGeneralTimer {
public:
    ScopedIOGeneralTimer() { runtime::process::BeginIOGeneralTimer(); }

    ~ScopedIOGeneralTimer() { runtime::process::EndIOGeneralTimer(); }

    ScopedIOGeneralTimer(const ScopedIOGeneralTimer&) = delete;
    ScopedIOGeneralTimer& operator=(const ScopedIOGeneralTimer&) = delete;
};

} // namespace

int GetSocketLastError() { return portable::GetNativeSocketLastError(); }

void SetSocketLastError(int error) { portable::SetNativeSocketLastError(error); }

namespace portable {

SocketWriteSubmission SubmitSocketWrite(
    CIOObject* object, const std::uint8_t* data, int length, PortableOverlapped* overlapped
) {
    if (!object) {
        SetSocketLastError(10038);
        return SocketWriteSubmission::Failed();
    }
    return WriteSocketTransport(
        object, data, length, [object, overlapped](bool success, std::uint32_t transferred, int socketError) {
            if (PostIOCompletionResult(object, success ? 1 : 0, transferred, overlapped, socketError)) {
                return;
            }
            if (!success && socketError != 0) {
                SetSocketLastError(socketError);
            }
            object->OnIOCallback(success ? 1 : 0, transferred, overlapped);
        }
    );
}

} // namespace portable

bool IsExpectedSocketCloseError(int error) { return error == 10038 || error == 10054 || error == 10053; }

namespace portable {

void ReportSocketReadFailure(std::uint64_t socket, int error) {
    const bool expectedClose = IsExpectedSocketCloseError(error);
    if (socket == UINT64_MAX) {
        // Noise: An expected terminal read completion after Close adds no
        // new connection state. The socket's close owner already ran.
        if (expectedClose) {
            return;
        }
        // Log.Add(LOG_ERROR,
        //     "[network] Read completion failed after the connection was closed (socket error %d).",
        //     error)
        L2_LOG_ERROR(Log, "[network] Read completion failed after the connection was closed (socket error %d).", error);
        return;
    }
    const char* reason = error == 10054 ? "connection reset"
        : error == 10053                ? "connection aborted"
        : error == 10038                ? "socket no longer valid"
                                        : "read failed";
    // Log.Add(expectedClose ? LOG_NORMAL : LOG_ERROR,
    //     "[network] Read on connection %llu ended: %s (socket error %d). Closing the connection.",
    //     static_cast<unsigned long long>(socket), reason, error)
    L2_LOG(
        Log, expectedClose ? LOG_NORMAL : LOG_ERROR,
        "[network] Read on connection %llu ended: %s (socket error %d); closing the connection.",
        static_cast<unsigned long long>(socket), reason, error
    );
}

void ReportSocketFailureDiagnostics(std::uint64_t socket, int error, std::int32_t pendingBytes) {
    // Noise policy: no periodic output, no extra records for EOF, reset,
    // abort or our own Close. Read/write failures share one snapshot budget.
    NativeSocketDiagnostics state;
    if (!TakeNativeSocketDiagnostics(socket, error, state)) {
        return;
    }
    L2_LOG_ERROR(
        Log,
        "[network] Connection %llu diagnostics: error %d; lifetime %lld ms; last receive %lld ms ago, "
        "last send to OS %lld ms ago; received %llu bytes, sent to OS %llu bytes; application queue %d bytes; "
        "first native error %d (%s), unsent native buffer %d bytes at failure; failure dispatch age %lld ms "
        "(-1 = unavailable).",
        static_cast<unsigned long long>(socket), error, static_cast<long long>(state.lifetimeMs),
        static_cast<long long>(state.lastReadAgeMs), static_cast<long long>(state.lastWriteAgeMs),
        static_cast<unsigned long long>(state.receivedBytes), static_cast<unsigned long long>(state.sentBytes),
        static_cast<int>(pendingBytes), state.firstNativeError,
        state.firstNativeError == 0 ? "unavailable" : (state.writeFailure ? "write" : "read"),
        state.unsentNativeBytesAtFailure, static_cast<long long>(state.failureDispatchAgeMs)
    );
}

void ReportSocketPeerClose(std::uint64_t socket) {
    // Noise: Do not repeat the close record for a late EOF completion.
    if (socket == UINT64_MAX) {
        return;
    }
    // Log.Add(LOG_NORMAL,
    //     "[network] Peer closed connection %llu. Closing the connection.",
    //     static_cast<unsigned long long>(socket))
    L2_LOG_INFO(
        Log, "[network] Peer closed connection %llu; closing the connection.", static_cast<unsigned long long>(socket)
    );
}

void ReportSocketReadArmFailure(std::uint64_t socket, int error, std::uint32_t retainedBytes, std::size_t capacity) {
    // runtime::log::WriteErrorWithLocalCopy(Log,
    //     "[network] Cannot schedule another read on connection %llu (socket error %d. Retained %u bytes, buffer capacity
    //     %llu bytes), closing the connection.", static_cast<unsigned long long>(socket), error,
    //     static_cast<unsigned>(retainedBytes), static_cast<unsigned long long>(capacity))
    L2_LOG_ERROR_WITH_LOCAL_COPY(
        Log,
        "[network] Cannot schedule another read on connection %llu (socket error %d; retained %u bytes, buffer capacity "
        "%llu bytes); closing the connection.",
        static_cast<unsigned long long>(socket), error, static_cast<unsigned>(retainedBytes),
        static_cast<unsigned long long>(capacity)
    );
}

} // namespace portable

void LogIOError(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    // Original: runtime::process::WriteIOErrorV(format, arguments)
    try {
        runtime::process::WriteIOErrorV(format, arguments);
    } catch (...) {
        va_end(arguments);
        throw;
    }
    va_end(arguments);
}

void LogIOError(const wchar_t* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    // Original: runtime::process::WriteIOErrorV(format, arguments)
    try {
        runtime::process::WriteIOErrorV(format, arguments);
    } catch (...) {
        va_end(arguments);
        throw;
    }
    va_end(arguments);
}

void LogIOLocalError(const wchar_t* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    // Original: runtime::process::WriteIOLocalErrorV(format, arguments)
    try {
        runtime::process::WriteIOLocalErrorV(format, arguments);
    } catch (...) {
        va_end(arguments);
        throw;
    }
    va_end(arguments);
}

void ReportIOCloseCallStack() { runtime::process::ReportIOCloseCallStack(); }

int GetCurrentThreadRemainIOBufferPoolCount() { return GetIOBufferPoolThreadCount(CThreadManager::GetThreadID()); }

int GetIOBufferPoolCount() {
    // Current L2Server 0x007cb1bc, Cached.exe 0x004e4f08 and L2NPC.exe
    // 0x00455778 all reload the live thread count after every nullable
    // per-thread pool observation.
    std::uint32_t totalBits = 0;
    for (int threadId = 0; threadId < CThreadManager::GetThreadCount(); ++threadId) {
        totalBits += static_cast<std::uint32_t>(PoolSizeAt(g_IOBufferPools, threadId));
    }
    int total = 0;
    std::memcpy(&total, &totalBits, sizeof(total));
    return total;
}

int GetIOBufferPoolTotalCount() {
    // Current L2Server 0x007cb220, Cached.exe 0x004e4f60 and L2NPC.exe
    // 0x004557d0 use the same traversal over each queue's total-count field
    // instead of its instantaneous free count.
    std::uint32_t totalBits = 0;
    for (int threadId = 0; threadId < CThreadManager::GetThreadCount(); ++threadId) {
        totalBits += static_cast<std::uint32_t>(PoolCapacityAt(g_IOBufferPools, threadId));
    }
    int total = 0;
    std::memcpy(&total, &totalBits, sizeof(total));
    return total;
}

int GetIOBufferPoolThreadCount(int threadId) { return PoolSizeAt(g_IOBufferPools, threadId); }

int GetIOBufferPoolThreadTotalCount(int threadId) { return PoolCapacityAt(g_IOBufferPools, threadId); }

void InitializeCurrentIOBufferPool(int count) {
    const int threadId = CThreadManager::GetThreadID();
    if (threadId < 0 || threadId >= ThreadID::PORTABLE_CAPACITY || g_IOBufferPools[threadId]) {
        return;
    }
    g_IOBufferPools[threadId] = new (std::nothrow)
#if defined(L2_BUILD_L2SERVER)
        CNewMwsrQueue<DefaultIOBuffer>(count);
#else
        CMwsrQueue<DefaultIOBuffer>(count);
#endif
}

DefaultIOBuffer* BufferPoolSelector<DefaultIOBuffer>::PopBuffer() { return PopBufferFrom(g_IOBufferPools); }

void BufferPoolSelector<DefaultIOBuffer>::PushBuffer(DefaultIOBuffer* buffer) { PushBufferTo(g_IOBufferPools, buffer); }

int BufferPoolSelector<DefaultIOBuffer>::InstantSize(int threadId) { return PoolSizeAt(g_IOBufferPools, threadId); }

IOBuffer8k* BufferPoolSelector<IOBuffer8k>::PopBuffer() { return PopBufferFrom(g_IOBuffer8kPools); }

void BufferPoolSelector<IOBuffer8k>::PushBuffer(IOBuffer8k* buffer) { PushBufferTo(g_IOBuffer8kPools, buffer); }

int BufferPoolSelector<IOBuffer8k>::InstantSize(int threadId) { return PoolSizeAt(g_IOBuffer8kPools, threadId); }

IOBuffer32k* BufferPoolSelector<IOBuffer32k>::PopBuffer() { return PopBufferFrom(g_IOBuffer32kPools); }

void BufferPoolSelector<IOBuffer32k>::PushBuffer(IOBuffer32k* buffer) { PushBufferTo(g_IOBuffer32kPools, buffer); }

int BufferPoolSelector<IOBuffer32k>::InstantSize(int threadId) { return PoolSizeAt(g_IOBuffer32kPools, threadId); }

namespace io_buffer_detail {

void FailIOBufferAllocation(const std::source_location location) { runtime::process::FailIOBufferAllocation(location); }

void ReportInvalidIOBufferAllocation(
    int threadId, int remaining, int referenceCount, std::uint32_t dataSize, const void* next
) {
    runtime::process::ReportInvalidIOBufferAllocation(threadId, remaining, referenceCount, dataSize, next);
}

} // namespace io_buffer_detail

CIOObject::CIOObject() = default; // Current CacheD 0x004e51ec.

CIOObject::CIOObject(const CIOObject& other) : MemoryObject(other) {}

CIOObject::~CIOObject() = default;

L2_DEFINE_OBJECT_ADD_REF(CIOObject)

L2_DEFINE_OBJECT_RELEASE(CIOObject)

void CIOObject::TimerExpired(int) {}

void CIOObject::OnTimer(int id) {
    L2_GUARD_FUNCTION();
    SetInTimer(false);
    TimerExpired(id);
}

void CIOObject::TimerExpired2(int, std::int64_t) {}

void CIOObject::OnTimer2(int id, std::int64_t uid) {
    L2_GUARD_FUNCTION();
    SetInTimer(false);
    TimerExpired2(id, uid);
}

void CIOObject::OnTimerCallback(int id, std::int64_t uid) {
    L2_GUARD_FUNCTION();
    PortableOverlapped* overlapped = nullptr;
    if (uid >= 0) {
        overlapped = reinterpret_cast<PortableOverlapped*>(static_cast<std::uintptr_t>(uid));
    }
    if (!portable::PostIOCompletion(this, static_cast<std::uint32_t>(id), overlapped)) {
        runtime::process::ReportIOCompletionPostFailure();
        L2_RELEASE(this, ObjectRefType::Timer);
    }
}

void CIOObject::OnWaitCallback() {}

void CIOObject::OnIOCallback(int, std::uint32_t transferred, PortableOverlapped* overlapped) {
    L2_GUARD_FUNCTION();
    ScopedIOGeneralTimer timer;
    if (!overlapped) {
        OnTimer(static_cast<int>(transferred));
    } else {
        OnTimer2(static_cast<int>(transferred), static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(overlapped)));
    }
    L2_RELEASE(this, ObjectRefType::Timer);
}

int CIOObject::PostObject(int id) {
    // Current Cached.exe virtual body 0x0040e284 posts this object as the
    // completion key, the integer id as transferred bytes and null OVERLAPPED.
    return portable::PostIOCompletion(this, static_cast<std::uint32_t>(id), nullptr) ? 1 : 0;
}
