#include <atomic>
#include "Shared/src/Runtime/NativeSocketError.h"
#include "Shared/src/Runtime/NativeSocketTransport.h"
#include "Shared/src/Runtime/SocketTransport.h"
#include "Shared/src/Runtime/OperatorLog.h"

#include <condition_variable>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <WinSock2.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace portable {
namespace {

constexpr std::uint64_t kInvalidNativeSocket = UINT64_C(0xffffffffffffffff);

class AcceptedSocketTransport;

struct AcceptedSocketTransportRegistry {
    std::mutex mutex;
    std::vector<std::weak_ptr<AcceptedSocketTransport>> transports;
};

AcceptedSocketTransportRegistry& TransportRegistry() {
    static auto* const registry = new AcceptedSocketTransportRegistry;
    return *registry;
}

bool SetNativeSocketNonBlocking(std::uint64_t socket) {
#if defined(_WIN32)
    // WSAEventSelect makes the listener nonblocking. Remove any inherited
    // event association, then keep nonblocking mode so SubmitWrite can make
    // the same immediate native submission that target WSASend makes.
    ::WSAEventSelect(static_cast<SOCKET>(socket), nullptr, 0);
    u_long nonblocking = 1;
    return ::ioctlsocket(static_cast<SOCKET>(socket), FIONBIO, &nonblocking) == 0;
#else
    const int descriptor = static_cast<int>(socket);
    const int flags = ::fcntl(descriptor, F_GETFL, 0);
    return flags >= 0 && ::fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

void ShutdownNativeSocket(std::uint64_t socket) {
#if defined(_WIN32)
    ::shutdown(static_cast<SOCKET>(socket), SD_BOTH);
#else
    ::shutdown(static_cast<int>(socket), SHUT_RDWR);
#endif
}

void SetAbortiveLinger(std::uint64_t socket) {
    linger option{1, 0};
#if defined(_WIN32)
    ::setsockopt(static_cast<SOCKET>(socket), SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&option), sizeof(option));
#else
    // WinSock reports a failed peer write through its return value and never
    // delivers SIGPIPE. macOS has no MSG_NOSIGNAL send flag, so suppress the
    // signal on the accepted descriptor to keep that target contract.
#if defined(SO_NOSIGPIPE)
    const int noSigPipe = 1;
    ::setsockopt(static_cast<int>(socket), SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof(noSigPipe));
#endif
    ::setsockopt(static_cast<int>(socket), SOL_SOCKET, SO_LINGER, &option, sizeof(option));
#endif
}

void CloseNativeSocket(std::uint64_t socket) {
#if defined(_WIN32)
    ::closesocket(static_cast<SOCKET>(socket));
#else
    ::close(static_cast<int>(socket));
#endif
}

void InitializeConnectedNativeSocket(std::uint64_t socket) {
#if defined(_WIN32)
    int bufferSize = 0;
    ::setsockopt(
        static_cast<SOCKET>(socket), SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&bufferSize), sizeof(bufferSize)
    );
    bufferSize = 0;
    ::setsockopt(
        static_cast<SOCKET>(socket), SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&bufferSize), sizeof(bufferSize)
    );
#else
    // Keep native buffer defaults on POSIX. A zero size on Linux forces
    // tiny buffers and disables receive autotuning.
    static_cast<void>(socket);
#endif
}

int SendNativeSocket(std::uint64_t socket, const std::uint8_t* data, int length) {
#if defined(_WIN32)
    return ::send(static_cast<SOCKET>(socket), reinterpret_cast<const char*>(data), length, 0);
#else
#if defined(MSG_NOSIGNAL)
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    return static_cast<int>(::send(static_cast<int>(socket), data, static_cast<std::size_t>(length), flags));
#endif
}

int ReceiveNativeSocket(std::uint64_t socket, std::uint8_t* data, int capacity) {
#if defined(_WIN32)
    return ::recv(static_cast<SOCKET>(socket), reinterpret_cast<char*>(data), capacity, 0);
#else
    return static_cast<int>(::recv(static_cast<int>(socket), data, static_cast<std::size_t>(capacity), 0));
#endif
}

bool IsWouldBlockError(int error) { return error == 10035; }

int WaitForNativeSocket(std::uint64_t socket, bool writable) {
#if defined(_WIN32)
    for (;;) {
        fd_set ready;
        fd_set failed;
        FD_ZERO(&ready);
        FD_ZERO(&failed);
        FD_SET(static_cast<SOCKET>(socket), &ready);
        FD_SET(static_cast<SOCKET>(socket), &failed);

        const int result = ::select(0, writable ? nullptr : &ready, writable ? &ready : nullptr, &failed, nullptr);
        if (result != SOCKET_ERROR || GetNativeSocketLastError() != 10004) {
            return result;
        }
    }
#else
    pollfd descriptor{};
    descriptor.fd = static_cast<int>(socket);
    descriptor.events = writable ? POLLOUT : POLLIN;

    int result = 0;
    do {
        result = ::poll(&descriptor, 1, -1);
    } while (result < 0 && errno == EINTR);
    return result;
#endif
}

class AcceptedSocketTransport final : public std::enable_shared_from_this<AcceptedSocketTransport> {
public:
    AcceptedSocketTransport(
        void* target, std::uint64_t nativeSocket, NativeSocketReadSink readSink, NativeSocketTerminalSink terminalSink
    ) noexcept :
        target_(target), nativeSocket_(nativeSocket), readSink_(readSink), terminalSink_(terminalSink) {}

    ~AcceptedSocketTransport() {
        Close(true);
        WaitForWorkerExit();
    }

    bool Start() {
        if (!SetNativeSocketNonBlocking(nativeSocket_)) {
            return false;
        }

        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (started_ || stopping_ || readerStarted_ || writerStarted_) {
                SetNativeSocketLastError(10022);
                return false;
            }
        }

        const std::shared_ptr<AcceptedSocketTransport> self = shared_from_this();
        try {
            std::thread([self] { self->ReaderMain(); }).detach();
            {
                std::lock_guard<std::mutex> guard(mutex_);
                readerStarted_ = true;
            }
        } catch (...) {
            SetNativeSocketLastError(10055);
            return false;
        }

        try {
            std::thread([self] { self->WriterMain(); }).detach();
            {
                std::lock_guard<std::mutex> guard(mutex_);
                writerStarted_ = true;
                started_ = true;
            }
        } catch (...) {
            Abort();
            WaitForWorkerExit();
            SetNativeSocketLastError(10055);
            return false;
        }
        return true;
    }

    bool ArmRead(std::size_t capacity) {
        if (capacity == 0 || capacity > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            SetNativeSocketLastError(10022);
            return false;
        }

        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (!started_ || stopping_ || nativeSocket_ == kInvalidNativeSocket || readArmed_) {
                SetNativeSocketLastError(10038);
                return false;
            }
            readCapacity_ = capacity;
            readArmed_ = true;
        }
        readWake_.notify_one();
        return true;
    }

    SocketWriteSubmission SubmitWrite(const std::uint8_t* data, int length, SocketWriteCompletion completion) {
        if (!data || length <= 0 || !completion) {
            SetNativeSocketLastError(10022);
            return SocketWriteSubmission::Failed();
        }

        std::uint64_t socket = kInvalidNativeSocket;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (!started_ || stopping_ || nativeSocket_ == kInvalidNativeSocket) {
                SetNativeSocketLastError(10038);
                return SocketWriteSubmission::Failed();
            }
            if (writeSubmitting_ || pendingWrite_.completion) {
                SetNativeSocketLastError(10037);
                return SocketWriteSubmission::Failed();
            }
            writeSubmitting_ = true;
            socket = nativeSocket_;
            ++nativeOperations_;
        }

        // Current CIOSocketEx::_Write calls WSASend inline. In particular,
        // CacheVersionPacket can return true immediately after Send and cause
        // CPacketEx to close the socket. Do not defer this first native call
        // to WriterLoop or that close can cancel a write never submitted to
        // the host socket.
        int transferred = 0;
        int error = 0;
        do {
            transferred = SendNativeSocket(socket, data, length);
            error = transferred < 0 ? GetNativeSocketLastError() : 0;
        } while (transferred < 0 && error == 10004);

        SocketWriteSubmission submission = SocketWriteSubmission::Failed();
        bool wakeWriter = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (nativeOperations_ != 0) {
                --nativeOperations_;
            }
            writeSubmitting_ = false;
            RecordResultLocked(true, transferred, error, transferred > 0 ? length - transferred : length);

            if (transferred == length) {
                submission = SocketWriteSubmission::Completed(static_cast<std::uint32_t>(transferred));
            } else if (transferred > 0 && !stopping_) {
                // A POSIX send may write only part of the buffer.
                // The owner expects one completion for the whole CIOBufferEx.
                // Keep the remaining bytes in this operation and finish them on the writer
                // thread. An early partial completion would leave the first buffer stuck.
                pendingWrite_.data = data;
                pendingWrite_.length = length;
                pendingWrite_.transferred = transferred;
                pendingWrite_.completion = std::move(completion);
                submission = SocketWriteSubmission::Pending();
                wakeWriter = true;
            } else if (transferred < 0 && IsWouldBlockError(error) && !stopping_) {
                pendingWrite_.data = data;
                pendingWrite_.length = length;
                pendingWrite_.transferred = 0;
                pendingWrite_.completion = std::move(completion);
                submission = SocketWriteSubmission::Pending();
                wakeWriter = true;
            } else {
                if (stopping_) {
                    error = 10038;
                } else if (error == 0) {
                    error = 10054;
                }
            }
        }
        operationWake_.notify_all();
        if (wakeWriter) {
            writeWake_.notify_one();
        }
        if (submission.IsFailed()) {
            SetNativeSocketLastError(error);
        }
        return submission;
    }

    void Initialize() {
        std::uint64_t socket = kInvalidNativeSocket;
        if (!AcquireNativeOperation(socket)) {
            return;
        }
        InitializeConnectedNativeSocket(socket);
        ReleaseNativeOperation();
    }

    void Abort() { Close(true); }

    bool TakeDiagnostics(std::uint64_t socket, NativeSocketDiagnostics& result) {
        std::lock_guard<std::mutex> guard(mutex_);
        if (nativeSocket_ != socket || diagnosticsReported_) {
            return false;
        }
        diagnosticsReported_ = true;
        const auto now = DiagnosticClock::now();
        const auto age = [now](DiagnosticClock::time_point time) -> std::int64_t {
            return time == DiagnosticClock::time_point{}
                ? -1 : std::chrono::duration_cast<std::chrono::milliseconds>(now - time).count();
        };
        result = {age(createdAt_), age(lastReadAt_), age(lastWriteAt_), age(firstFailureAt_),
            receivedBytes_, sentBytes_, firstNativeError_, writeFailure_, unsentNativeBytesAtFailure_};
        return true;
    }

    void GracefulClose() { Close(false); }

    void WaitForWorkerExit() {
        std::unique_lock<std::mutex> guard(mutex_);
        const std::thread::id current = std::this_thread::get_id();
        if (readerStarted_ && !readerExited_ && readerThreadId_ != current) {
            workerExitWake_.wait(guard, [this] { return readerExited_; });
        }
        if (writerStarted_ && !writerExited_ && writerThreadId_ != current) {
            workerExitWake_.wait(guard, [this] { return writerExited_; });
        }
    }

private:
    using DiagnosticClock = std::chrono::steady_clock;

    // Called under the existing transport lock: no extra registry lookup or
    // logging on successful traffic. Never change native error/close behavior.
    void RecordResultLocked(bool writing, int transferred, int error, int remaining = 0) {
        if (transferred > 0) {
            (writing ? lastWriteAt_ : lastReadAt_) = DiagnosticClock::now();
            (writing ? sentBytes_ : receivedBytes_) += static_cast<std::uint64_t>(transferred);
        } else if (!stopping_ && firstNativeError_ == 0 && error != 0 && error != 10004 && error != 10035 &&
            error != 10038 && error != 10053 && error != 10054) {
            firstFailureAt_ = DiagnosticClock::now();
            firstNativeError_ = error;
            writeFailure_ = writing;
            unsentNativeBytesAtFailure_ = writing ? remaining : pendingWrite_.length - pendingWrite_.transferred;
        }
    }

    bool AcquireNativeOperation(std::uint64_t& socket) {
        std::lock_guard<std::mutex> guard(mutex_);
        if (stopping_ || nativeSocket_ == kInvalidNativeSocket) {
            return false;
        }
        socket = nativeSocket_;
        ++nativeOperations_;
        return true;
    }

    void ReleaseNativeOperation() {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (nativeOperations_ != 0) {
                --nativeOperations_;
            }
        }
        operationWake_.notify_all();
    }

    void Close(bool abortive) {
        std::uint64_t socket = kInvalidNativeSocket;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (stopping_) {
                return;
            }
            stopping_ = true;
            readArmed_ = false;
            socket = nativeSocket_;
        }
        readWake_.notify_all();
        writeWake_.notify_all();

        if (socket == kInvalidNativeSocket) {
            return;
        }
        ShutdownNativeSocket(socket);

        {
            std::unique_lock<std::mutex> guard(mutex_);
            operationWake_.wait(guard, [this] { return nativeOperations_ == 0; });
            if (nativeSocket_ == socket) {
                nativeSocket_ = kInvalidNativeSocket;
            }
        }

        if (abortive) {
            SetAbortiveLinger(socket);
        }
        CloseNativeSocket(socket);
    }

    void ReadLoop() {
        for (;;) {
            std::size_t capacity = 0;
            std::uint64_t socket = kInvalidNativeSocket;
            {
                std::unique_lock<std::mutex> guard(mutex_);
                readWake_.wait(guard, [this] { return stopping_ || readArmed_; });
                if (stopping_) {
                    return;
                }
                capacity = readCapacity_;
                readArmed_ = false;
                socket = nativeSocket_;
                ++nativeOperations_;
            }

            std::vector<std::uint8_t> bytes;
            try {
                bytes.resize(capacity);
            } catch (const std::bad_alloc&) {
                ReleaseNativeOperation();
                terminalSink_(target_, 10055);
                return;
            }

            int transferred = 0;
            int error = 0;
            for (;;) {
                transferred = ReceiveNativeSocket(socket, bytes.data(), static_cast<int>(capacity));
                error = transferred < 0 ? GetNativeSocketLastError() : 0;
                if (transferred < 0 && error == 10004) {
                    continue;
                }
                if (transferred < 0 && IsWouldBlockError(error)) {
                    const int waitResult = WaitForNativeSocket(socket, false);
                    {
                        std::lock_guard<std::mutex> guard(mutex_);
                        if (stopping_) {
                            error = 10038;
                            break;
                        }
                    }
                    if (waitResult > 0) {
                        continue;
                    }
                    error = waitResult < 0 ? GetNativeSocketLastError() : 10054;
                }
                break;
            }

            bool stopping = false;
            {
                std::lock_guard<std::mutex> guard(mutex_);
                stopping = stopping_;
                RecordResultLocked(false, transferred, error);
            }
            ReleaseNativeOperation();

            if (transferred > 0) {
                if (!readSink_(target_, bytes.data(), static_cast<std::size_t>(transferred))) {
                    terminalSink_(target_, 10038);
                    return;
                }
                continue;
            }

            terminalSink_(target_, stopping ? 10038 : error);
            return;
        }
    }

    void ReaderMain() {
        // Diagnostic sequence only. Independent of recovered TLS IDs and socket handles.
        static std::atomic<unsigned long long> nextLogIndex{0};
        runtime::logging::SetIndexedThreadName("socket-read", nextLogIndex.fetch_add(1, std::memory_order_relaxed));
        {
            std::lock_guard<std::mutex> guard(mutex_);
            readerThreadId_ = std::this_thread::get_id();
        }
        try {
            ReadLoop();
        } catch (...) {
            terminalSink_(target_, 10055);
        }
        {
            std::lock_guard<std::mutex> guard(mutex_);
            readerExited_ = true;
        }
        workerExitWake_.notify_all();
    }

    void WriterLoop() {
        for (;;) {
            const std::uint8_t* data = nullptr;
            int remaining = 0;
            std::uint64_t socket = kInvalidNativeSocket;
            SocketWriteCompletion cancelled;
            bool shouldExit = false;
            {
                std::unique_lock<std::mutex> guard(mutex_);
                writeWake_.wait(guard, [this] { return stopping_ || pendingWrite_.completion; });
                if (stopping_) {
                    cancelled = std::move(pendingWrite_.completion);
                    pendingWrite_ = {};
                    shouldExit = true;
                } else {
                    data = pendingWrite_.data + pendingWrite_.transferred;
                    remaining = pendingWrite_.length - pendingWrite_.transferred;
                    socket = nativeSocket_;
                    ++nativeOperations_;
                }
            }

            if (cancelled) {
                cancelled(false, 0, 10038);
            }
            if (shouldExit) {
                break;
            }

            int transferred = 0;
            int error = 0;
            for (;;) {
                transferred = SendNativeSocket(socket, data, remaining);
                error = transferred < 0 ? GetNativeSocketLastError() : 0;
                if (transferred < 0 && error == 10004) {
                    continue;
                }
                if (transferred < 0 && IsWouldBlockError(error)) {
                    const int waitResult = WaitForNativeSocket(socket, true);
                    {
                        std::lock_guard<std::mutex> guard(mutex_);
                        if (stopping_) {
                            error = 10038;
                            break;
                        }
                    }
                    if (waitResult > 0) {
                        continue;
                    }
                    error = waitResult < 0 ? GetNativeSocketLastError() : 10054;
                }
                break;
            }
            ReleaseNativeOperation();

            SocketWriteCompletion completion;
            bool success = false;
            bool stopAfterCompletion = false;
            int completionLength = 0;
            {
                std::lock_guard<std::mutex> guard(mutex_);
                RecordResultLocked(true, transferred, error, remaining);
                if (transferred > 0) {
                    pendingWrite_.transferred += transferred;
                }
                if (pendingWrite_.transferred == pendingWrite_.length) {
                    success = true;
                    completionLength = pendingWrite_.length;
                    completion = std::move(pendingWrite_.completion);
                    pendingWrite_ = {};
                } else if (transferred <= 0 || stopping_) {
                    completion = std::move(pendingWrite_.completion);
                    pendingWrite_ = {};
                    if (error == 0) {
                        error = stopping_ ? 10038 : 10054;
                    }
                }
                stopAfterCompletion = stopping_;
            }

            if (completion) {
                completion(success, success ? static_cast<std::uint32_t>(completionLength) : 0, success ? 0 : error);
            }
            if (stopAfterCompletion) {
                break;
            }
        }
    }

    void WriterMain() {
        // Diagnostic sequence only. Independent of recovered TLS IDs and socket handles.
        static std::atomic<unsigned long long> nextLogIndex{0};
        runtime::logging::SetIndexedThreadName("socket-write", nextLogIndex.fetch_add(1, std::memory_order_relaxed));
        {
            std::lock_guard<std::mutex> guard(mutex_);
            writerThreadId_ = std::this_thread::get_id();
        }
        try {
            WriterLoop();
        } catch (...) {
            SocketWriteCompletion failed;
            {
                std::lock_guard<std::mutex> guard(mutex_);
                failed = std::move(pendingWrite_.completion);
                pendingWrite_ = {};
            }
            if (failed) {
                failed(false, 0, 10055);
            }
        }

        {
            std::lock_guard<std::mutex> guard(mutex_);
            writerExited_ = true;
        }
        workerExitWake_.notify_all();
    }

    struct PendingWrite {
        const std::uint8_t* data = nullptr;
        int length = 0;
        int transferred = 0;
        SocketWriteCompletion completion;
    };

    void* const target_;
    std::mutex mutex_;
    std::condition_variable readWake_;
    std::condition_variable writeWake_;
    std::condition_variable operationWake_;
    std::condition_variable workerExitWake_;
    std::thread::id readerThreadId_;
    std::thread::id writerThreadId_;
    std::uint64_t nativeSocket_ = kInvalidNativeSocket;
    NativeSocketReadSink const readSink_;
    NativeSocketTerminalSink const terminalSink_;
    std::size_t readCapacity_ = 0;
    std::size_t nativeOperations_ = 0;
    PendingWrite pendingWrite_;
    const DiagnosticClock::time_point createdAt_ = DiagnosticClock::now();
    DiagnosticClock::time_point lastReadAt_;
    DiagnosticClock::time_point lastWriteAt_;
    DiagnosticClock::time_point firstFailureAt_;
    std::uint64_t receivedBytes_ = 0;
    std::uint64_t sentBytes_ = 0;
    int firstNativeError_ = 0;
    int unsentNativeBytesAtFailure_ = 0;
    bool writeFailure_ = false;
    bool diagnosticsReported_ = false;
    bool started_ = false;
    bool stopping_ = false;
    bool writeSubmitting_ = false;
    bool readArmed_ = false;
    bool readerStarted_ = false;
    bool writerStarted_ = false;
    bool readerExited_ = false;
    bool writerExited_ = false;
};

void RegisterTransport(const std::shared_ptr<AcceptedSocketTransport>& transport) {
    AcceptedSocketTransportRegistry& registry = TransportRegistry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    auto output = registry.transports.begin();
    for (auto current = registry.transports.begin(); current != registry.transports.end(); ++current) {
        if (!current->expired()) {
            *output++ = *current;
        }
    }
    registry.transports.erase(output, registry.transports.end());
    registry.transports.emplace_back(transport);
}

} // namespace

bool BindNativeSocketTransport(
    void* owner, std::uint64_t nativeSocket, NativeSocketReadSink readSink, NativeSocketTerminalSink terminalSink
) {
    if (!owner || nativeSocket == kInvalidNativeSocket || !readSink || !terminalSink) {
        SetNativeSocketLastError(10038);
        return false;
    }

    const auto transport = std::make_shared<AcceptedSocketTransport>(owner, nativeSocket, readSink, terminalSink);
    if (!BindSocketTransport(
            owner,
            [transport](const std::uint8_t* data, int length, SocketWriteCompletion completion) {
                return transport->SubmitWrite(data, length, std::move(completion));
            },
            [transport] { transport->Abort(); }, [transport] { transport->Initialize(); },
            [transport] { transport->GracefulClose(); },
            [transport](std::size_t capacity) { return transport->ArmRead(capacity); }
        )) {
        transport->Abort();
        return false;
    }

    if (!transport->Start()) {
        return false;
    }
    RegisterTransport(transport);
    return true;
}

void StopNativeSocketTransports() {
    std::vector<std::shared_ptr<AcceptedSocketTransport>> transports;
    {
        AcceptedSocketTransportRegistry& registry = TransportRegistry();
        std::lock_guard<std::mutex> guard(registry.mutex);
        transports.reserve(registry.transports.size());
        for (const auto& weakTransport : registry.transports) {
            if (const auto transport = weakTransport.lock()) {
                transports.push_back(transport);
            }
        }
        registry.transports.clear();
    }

    for (const auto& transport : transports) {
        transport->Abort();
    }
    for (const auto& transport : transports) {
        transport->WaitForWorkerExit();
    }
}

bool TakeNativeSocketDiagnostics(std::uint64_t socket, int error, NativeSocketDiagnostics& result) {
    if (socket == kInvalidNativeSocket || error == 0 || error == 10004 || error == 10035 ||
        error == 10038 || error == 10053 || error == 10054) {
        return false;
    }
    AcceptedSocketTransportRegistry& registry = TransportRegistry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    for (const auto& weak : registry.transports) {
        if (const auto transport = weak.lock(); transport && transport->TakeDiagnostics(socket, result)) {
            return true;
        }
    }
    return false;
}

} // namespace portable
