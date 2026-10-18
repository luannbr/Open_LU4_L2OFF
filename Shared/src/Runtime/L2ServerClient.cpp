#include <atomic>
#include "Shared/src/Runtime/OperatorLog.h"
// Shared native TCP transport for the recovered server connection owners.
#include "Shared/src/Runtime/L2ServerClient.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace network {
namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket kNativeInvalid = INVALID_SOCKET;

void closeSocket(NativeSocket socket) {
    if (socket != kNativeInvalid) {
        closesocket(socket);
    }
}

void shutdownSocket(NativeSocket socket) {
    if (socket != kNativeInvalid) {
        shutdown(socket, SD_BOTH);
    }
}

void setAbortiveLinger(NativeSocket socket) {
    if (socket == kNativeInvalid) {
        return;
    }
    linger option{};
    option.l_onoff = 1;
    option.l_linger = 0;
    setsockopt(socket, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&option), sizeof(option));
}

bool setNonBlocking(NativeSocket socket, bool enabled) {
    u_long value = enabled ? 1UL : 0UL;
    return ioctlsocket(socket, FIONBIO, &value) == 0;
}

int lastSocketError() { return WSAGetLastError(); }

void setNotSocketError() { WSASetLastError(WSAENOTSOCK); }

bool connectInProgress(int error) { return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEINVAL; }

#else
using NativeSocket = int;
constexpr NativeSocket kNativeInvalid = -1;

void closeSocket(NativeSocket socket) {
    if (socket != kNativeInvalid) {
        ::close(socket);
    }
}

void shutdownSocket(NativeSocket socket) {
    if (socket != kNativeInvalid) {
        ::shutdown(socket, SHUT_RDWR);
    }
}

void setAbortiveLinger(NativeSocket socket) {
    if (socket == kNativeInvalid) {
        return;
    }
    linger option{};
    option.l_onoff = 1;
    option.l_linger = 0;
    setsockopt(socket, SOL_SOCKET, SO_LINGER, &option, sizeof(option));
}

bool setNonBlocking(NativeSocket socket, bool enabled) {
    const int flags = ::fcntl(socket, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    const int updated = enabled ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return ::fcntl(socket, F_SETFL, updated) == 0;
}

int lastSocketError() { return errno; }

void setNotSocketError() { errno = ENOTSOCK; }

bool connectInProgress(int error) { return error == EINPROGRESS || error == EWOULDBLOCK; }

int winSockCompatibleError(int error) {
    // Keep the numeric diagnostics shown by the Windows target for the
    // connection failures that have direct POSIX equivalents.
    if (error == EACCES) {
        return 10013;
    }
    if (error == EINVAL) {
        return 10022;
    }
    if (error == EMFILE) {
        return 10024;
    }
    if (error == EAGAIN || error == EWOULDBLOCK) {
        return 10035;
    }
    if (error == EINPROGRESS) {
        return 10036;
    }
    if (error == EALREADY) {
        return 10037;
    }
    // POSIX reports an invalid integer descriptor as EBADF, while the same
    // operation on a WinSock SOCKET reports WSAENOTSOCK.
    if (error == EBADF) {
        return 10038;
    }
    if (error == ENOTSOCK) {
        return 10038;
    }
    if (error == EADDRINUSE) {
        return 10048;
    }
    if (error == EADDRNOTAVAIL) {
        return 10049;
    }
    if (error == ENETDOWN) {
        return 10050;
    }
    if (error == ENETUNREACH) {
        return 10051;
    }
    if (error == ENETRESET) {
        return 10052;
    }
    if (error == ECONNABORTED) {
        return 10053;
    }
    if (error == ECONNRESET) {
        return 10054;
    }
    if (error == ENOBUFS) {
        return 10055;
    }
    if (error == EISCONN) {
        return 10056;
    }
    if (error == ENOTCONN) {
        return 10057;
    }
#ifdef ESHUTDOWN
    if (error == ESHUTDOWN) {
        return 10058;
    }
#endif
    if (error == ETIMEDOUT) {
        return 10060;
    }
    if (error == ECONNREFUSED) {
        return 10061;
    }
    if (error == EHOSTUNREACH) {
        return 10065;
    }
    return error;
}
#endif

#ifdef _WIN32
int winSockCompatibleError(int error) { return error; }
#endif

std::intptr_t storeSocket(NativeSocket socket) { return static_cast<std::intptr_t>(socket); }

NativeSocket loadSocket(std::intptr_t socket) { return static_cast<NativeSocket>(socket); }

int waitWritable(NativeSocket socket, int timeout_ms) {
#ifdef _WIN32
    fd_set write_set;
    fd_set exception_set;
    FD_ZERO(&write_set);
    FD_ZERO(&exception_set);
    FD_SET(socket, &write_set);
    FD_SET(socket, &exception_set);
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    const int result = select(0, nullptr, &write_set, &exception_set, &timeout);
    return result > 0 && (FD_ISSET(socket, &write_set) || FD_ISSET(socket, &exception_set)) ? 1 : result;
#else
    pollfd descriptor{};
    descriptor.fd = socket;
    descriptor.events = POLLOUT;
    return ::poll(&descriptor, 1, timeout_ms);
#endif
}

int waitReadable(NativeSocket socket, int timeout_ms) {
#ifdef _WIN32
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(socket, &read_set);
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    const int result = select(0, &read_set, nullptr, nullptr, &timeout);
    return result > 0 && FD_ISSET(socket, &read_set) ? 1 : result;
#else
    pollfd descriptor{};
    descriptor.fd = socket;
    descriptor.events = POLLIN;
    const int result = ::poll(&descriptor, 1, timeout_ms);
    if (result <= 0) {
        return result;
    }
    return (descriptor.revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) != 0 ? 1 : 0;
#endif
}

int socketConnectError(NativeSocket socket) {
    int error = 0;
#ifdef _WIN32
    int length = sizeof(error);
    if (getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) != 0) {
        return lastSocketError();
    }
#else
    socklen_t length = sizeof(error);
    if (getsockopt(socket, SOL_SOCKET, SO_ERROR, &error, &length) != 0) {
        return lastSocketError();
    }
#endif
    return error;
}

bool interruptedSocketCall(int error) {
#ifdef _WIN32
    return error == WSAEINTR;
#else
    return error == EINTR;
#endif
}

} // namespace

L2ServerClient::L2ServerClient(std::string endpoint_name) {
    // Retain source compatibility for portable embedders. Target connection
    // diagnostics are logged by NPCServer, CDBNPC and CLogCli themselves.
}

L2ServerClient::~L2ServerClient() { Close(); }

void L2ServerClient::SetReceiveSink(ReceiveSink receive, CloseSink close) {
    std::lock_guard<std::mutex> guard(sink_mutex_);
    receive_sink_ = std::move(receive);
    close_sink_ = std::move(close);
}

bool L2ServerClient::Connect(const std::string& host, int port, int timeout_ms, bool startReceiving) {
    ConnectOptions options;
    options.timeoutMilliseconds = timeout_ms;
    options.startReceiving = startReceiving;
    return Connect(host, port, options);
}

bool L2ServerClient::Connect(const std::string& host, int port, const ConnectOptions& options) {
    return static_cast<bool>(ConnectWithResult(host, port, options));
}

SocketConnectResult L2ServerClient::ConnectWithResult(const std::string& host, int port, const ConnectOptions& options) {
    const std::uint64_t closeGeneration = close_generation_.load(std::memory_order_acquire) + 1;
    Close();
    std::lock_guard<std::mutex> lifecycle_guard(lifecycle_mutex_);
    if (close_generation_.load(std::memory_order_acquire) != closeGeneration) {
        return {false, SocketConnectFailure::ConnectSocket, 0};
    }
    stop_.store(false, std::memory_order_release);
    host_ = host;
    port_ = port;

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    std::string addressText = host;
    if (options.numericIPv4Only) {
        hints.ai_flags = AI_NUMERICHOST;
        // inet_addr is the API used by all three shipping startup paths. It
        // maps an invalid name to INADDR_NONE instead of resolving DNS.
        const std::uint32_t address = static_cast<std::uint32_t>(::inet_addr(host.c_str()));
        const std::uint32_t hostAddress = ntohl(address);
        std::array<char, 16> numericAddress{};
        std::snprintf(
            numericAddress.data(), numericAddress.size(), "%u.%u.%u.%u", (hostAddress >> 24) & 0xffU,
            (hostAddress >> 16) & 0xffU, (hostAddress >> 8) & 0xffU, hostAddress & 0xffU
        );
        addressText.assign(numericAddress.data());
    }
    addrinfo* addresses = nullptr;
    // Every target caller stores the value with htons(u_short), including
    // negative or oversized configuration values.
    const std::string service = std::to_string(static_cast<std::uint16_t>(port));
    const int addressError = getaddrinfo(addressText.c_str(), service.c_str(), &hints, &addresses);
    if (addressError != 0) {
        return {false, SocketConnectFailure::ResolveAddress, addressError};
    }

    NativeSocket connected = kNativeInvalid;
    SocketConnectFailure failure = SocketConnectFailure::CreateSocket;
    int failureError = 0;
    const auto cancelled = [this, closeGeneration] {
        return close_generation_.load(std::memory_order_acquire) != closeGeneration;
    };
    for (addrinfo* address = addresses; address; address = address->ai_next) {
        if (cancelled()) {
            failure = SocketConnectFailure::ConnectSocket;
            break;
        }
        NativeSocket candidate = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (candidate == kNativeInvalid) {
            failure = SocketConnectFailure::CreateSocket;
            failureError = winSockCompatibleError(lastSocketError());
            continue;
        }
        if (options.bindEphemeralIPv4) {
            sockaddr_in localAddress{};
            localAddress.sin_family = AF_INET;
            localAddress.sin_addr.s_addr = htonl(INADDR_ANY);
            localAddress.sin_port = 0;
            if (bind(candidate, reinterpret_cast<const sockaddr*>(&localAddress), sizeof(localAddress)) != 0) {
                failure = SocketConnectFailure::BindSocket;
                failureError = winSockCompatibleError(lastSocketError());
                if (!options.abandonSocketOnBindOrConnectFailure) {
                    closeSocket(candidate);
                }
                continue;
            }
        }
        // Even the target-style blocking mode uses a nonblocking native
        // descriptor while waiting here. With no owner cancellation it waits
        // indefinitely and is therefore behaviorally blocking. The short
        // polls only let Close() interrupt portable process teardown.
        if (!setNonBlocking(candidate, true)) {
            failure = SocketConnectFailure::ConnectSocket;
            failureError = winSockCompatibleError(lastSocketError());
            closeSocket(candidate);
            continue;
        }
        const int result = connect(candidate, address->ai_addr, static_cast<int>(address->ai_addrlen));
        const int connectError = result == 0 ? 0 : lastSocketError();
        bool candidateConnected = result == 0;
        int candidateError = connectError;
        if (!candidateConnected && connectInProgress(connectError)) {
            const auto deadline = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(options.timeoutMilliseconds > 0 ? options.timeoutMilliseconds : 0);
            bool firstWait = true;
            while (!cancelled()) {
                int waitMilliseconds = 250;
                if (!options.blocking) {
                    const auto remaining =
                        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now())
                            .count();
                    if (remaining <= 0 && !firstWait) {
                        break;
                    }
                    waitMilliseconds = remaining <= 0 ? 0 : static_cast<int>(remaining < 250 ? remaining : 250);
                }
                firstWait = false;
                const int waitResult = waitWritable(candidate, waitMilliseconds);
                if (waitResult > 0) {
                    candidateError = socketConnectError(candidate);
                    candidateConnected = candidateError == 0;
                    break;
                }
                if (waitResult < 0) {
                    candidateError = lastSocketError();
                    if (interruptedSocketCall(candidateError)) {
                        continue;
                    }
                    break;
                }
            }
        }
        if (candidateConnected && !cancelled()) {
            connected = candidate;
            break;
        }
        failure = SocketConnectFailure::ConnectSocket;
        failureError = winSockCompatibleError(candidateError);
        // Current CDBNPC::InitDBNPC at 0x0041ccf0 returns after either bind
        // or connect failure without calling closesocket. Cancellation is a
        // portable process-teardown boundary and still closes immediately.
        if (cancelled() || !options.abandonSocketOnBindOrConnectFailure) {
            closeSocket(candidate);
        }
    }
    freeaddrinfo(addresses);
    if (connected == kNativeInvalid) {
        stop_.store(true, std::memory_order_release);
        return {false, failure, failureError};
    }

    if (cancelled()) {
        closeSocket(connected);
        stop_.store(true, std::memory_order_release);
        return {false, SocketConnectFailure::ConnectSocket, failureError};
    }

    setNonBlocking(connected, false);
    // WinSock has no SIGPIPE. Preserve that platform property on hosts that
    // expose SO_NOSIGPIPE. Linux uses MSG_NOSIGNAL on each native write.
#if !defined(_WIN32) && defined(SO_NOSIGPIPE)
    int one = 1;
    setsockopt(connected, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif

    stop_.store(false);
    receive_enabled_.store(options.startReceiving);
    next_read_capacity_.store(kMaximumReadSize, std::memory_order_release);
    abortive_close_requested_.store(true, std::memory_order_release);
    target_close_requested_.store(false, std::memory_order_release);
    last_read_error_.store(0, std::memory_order_release);
    socket_.store(storeSocket(connected));
    ReceiveSink connectionReceiveSink;
    CloseSink connectionCloseSink;
    {
        // IOCP associates every completion with the socket/OVERLAPPED that
        // submitted it. Snapshot the portable callbacks at the same
        // connection boundary so replacing them for a reconnect cannot
        // redirect an old reader's bytes or EOF to the new target object.
        std::lock_guard<std::mutex> guard(sink_mutex_);
        connectionReceiveSink = receive_sink_;
        connectionCloseSink = close_sink_;
    }
    writer_ = std::thread(&L2ServerClient::writerLoop, this);
    reader_ =
        std::thread(&L2ServerClient::readerLoop, this, std::move(connectionReceiveSink), std::move(connectionCloseSink));
    return {true, SocketConnectFailure::None, 0};
}

void L2ServerClient::StartReceiving() {
    // The native target starts an already-issued overlapped WSARecv and has
    // no condition-variable hand-off. The portable reader waits on
    // receive_wake_, so publish its condition while holding the same mutex.
    // otherwise a notify between condition evaluation and sleep can be lost.
    {
        std::lock_guard<std::mutex> receiveGuard(receive_mutex_);
        receive_enabled_.store(true, std::memory_order_release);
    }
    receive_wake_.notify_all();
}

void L2ServerClient::SetNextReadCapacity(int capacity) {
    if (capacity < -1) {
        capacity = -1;
    } else if (capacity > kMaximumReadSize) {
        capacity = kMaximumReadSize;
    }
    // CIOSocketEx::Read is the target's WSARecv re-arm. Synchronize the
    // portable condition with readerLoop's condition-variable wait so the
    // wake cannot be lost after the reader observes -1 but before it sleeps.
    {
        std::lock_guard<std::mutex> receiveGuard(receive_mutex_);
        next_read_capacity_.store(capacity, std::memory_order_release);
    }
    receive_wake_.notify_all();
}

void L2ServerClient::InitializeTargetSocket() {
    std::lock_guard<std::mutex> send_guard(send_mutex_);
    const std::intptr_t stored = socket_.load(std::memory_order_acquire);
    if (stored == kInvalidSocket) {
        return;
    }

    // Keep native buffer defaults on POSIX. A zero size on Linux forces
    // tiny buffers and disables receive autotuning.
#ifdef _WIN32
    const NativeSocket socket = loadSocket(stored);
    int bufferSize = 0;
    setsockopt(socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&bufferSize), sizeof(bufferSize));
    bufferSize = 0;
    setsockopt(socket, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&bufferSize), sizeof(bufferSize));
#endif
}

void L2ServerClient::Close() {
    close_generation_.fetch_add(1, std::memory_order_acq_rel);
    stop_.store(true, std::memory_order_release);
    receive_wake_.notify_all();
    write_wake_.notify_all();

    std::thread reader;
    std::thread writer;
    NativeSocket socket_to_close = kNativeInvalid;
    {
        std::lock_guard<std::mutex> lifecycle_guard(lifecycle_mutex_);
        // ConnectWithResult may have published a socket after the early
        // cancellation store but before this mutex became available.
        stop_.store(true, std::memory_order_release);
        receive_wake_.notify_all();
        write_wake_.notify_all();
        // Remove publication before taking send_mutex_. A blocking POSIX
        // send can otherwise prevent Close from ever reaching shutdown.
        // shutdown is the operation that wakes that sender. The descriptor
        // itself is not closed until the sender and reader have stopped, so
        // it still cannot be recycled underneath an in-flight native write.
        const std::intptr_t stored = socket_.exchange(kInvalidSocket);
        if (stored != kInvalidSocket) {
            socket_to_close = loadSocket(stored);
            shutdownSocket(socket_to_close);
        }
        if (reader_.joinable() && reader_.get_id() != std::this_thread::get_id()) {
            reader = std::move(reader_);
        }
        if (writer_.joinable() && writer_.get_id() != std::this_thread::get_id()) {
            writer = std::move(writer_);
        }
    }
    if (reader.joinable()) {
        reader.join();
    }
    if (writer.joinable()) {
        writer.join();
    }
    if (socket_to_close != kNativeInvalid) {
        std::lock_guard<std::mutex> send_guard(send_mutex_);
        closeSocket(socket_to_close);
    }
}

bool L2ServerClient::IsConnected() const { return socket_.load() != kInvalidSocket; }

int L2ServerClient::LastReadError() const { return last_read_error_.load(std::memory_order_acquire); }

portable::SocketWriteSubmission L2ServerClient::SubmitRawWrite(
    const std::uint8_t* data, int length, portable::SocketWriteCompletion completion
) {
    if (!data || length <= 0) {
        return portable::SocketWriteSubmission::Failed();
    }
    {
        // Serialize publication with Close(): once this job is accepted, the
        // descriptor remains open until writerLoop has delivered its IOCP-
        // equivalent completion.
        std::lock_guard<std::mutex> lifecycleGuard(lifecycle_mutex_);
        const std::intptr_t stored = socket_.load(std::memory_order_acquire);
        if (stored == kInvalidSocket) {
            setNotSocketError();
            return portable::SocketWriteSubmission::Failed();
        }
#if !defined(_WIN32) && defined(MSG_DONTWAIT)
        // Overlapped WSASend can still reject an operation immediately. A
        // per-call nonblocking attempt keeps that branch without making
        // an IO worker wait for socket capacity. Only would-block becomes a
        // pending operation owned by writerLoop.
        int sendFlags = MSG_DONTWAIT;
#if defined(MSG_NOSIGNAL)
        sendFlags |= MSG_NOSIGNAL;
#endif
        int sent = -1;
        {
            std::lock_guard<std::mutex> sendGuard(send_mutex_);
            do {
                sent = static_cast<int>(send(loadSocket(stored), data, length, sendFlags));
            } while (sent < 0 && errno == EINTR);
        }
        if (sent == length) {
            return portable::SocketWriteSubmission::Completed(static_cast<std::uint32_t>(sent));
        }
        if (sent > 0) {
            // A positive POSIX nonblocking send may consume only a prefix.
            // The recovered owner treats the submitted CIOBufferEx as one
            // operation and accepts its completion only when the reported
            // count equals that whole buffer. Continue the suffix inside the
            // same portable operation. Exposing the prefix as a target
            // completion takes the anomalous count branch and keeps its
            // first queued buffer forever.
            std::lock_guard<std::mutex> writeGuard(write_mutex_);
            write_queue_.push_back({stored, data, length, sent, std::move(completion)});
            write_wake_.notify_one();
            return portable::SocketWriteSubmission::Pending();
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return portable::SocketWriteSubmission::Failed();
        }
#endif
        std::lock_guard<std::mutex> writeGuard(write_mutex_);
        write_queue_.push_back({stored, data, length, 0, std::move(completion)});
    }
    write_wake_.notify_one();
    return portable::SocketWriteSubmission::Pending();
}

void L2ServerClient::RejectPeerProtocol(bool abortive) { signalTransportFailure(abortive); }

void L2ServerClient::signalTransportFailure(bool abortive) {
    abortive_close_requested_.store(abortive, std::memory_order_release);
    // A target-initiated closesocket cancels an outstanding overlapped read.
    // If one exists, its portable completion must observe the matching
    // Windows error domain instead of an orderly EOF.
    last_read_error_.store(995, std::memory_order_release);

    {
        // Keep cooperative owner Close from unpublishing and closing this
        // descriptor after the load but before linger/shutdown. The writer
        // does not take lifecycle_mutex_, so shutdown still wakes a blocked
        // native send.
        std::lock_guard<std::mutex> lifecycle_guard(lifecycle_mutex_);
        const std::intptr_t stored = socket_.load();
        if (stored != kInvalidSocket) {
            const NativeSocket socket = loadSocket(stored);
            if (abortive) {
                setAbortiveLinger(socket);
            }
            shutdownSocket(socket);
        }

        // CIOSocketEx::Close and GracefulClose invalidate and close their
        // target socket before returning. Publish this boundary only after
        // the portable descriptor has crossed the matching shutdown.
        // the reader can then perform its serialized final close.
        target_close_requested_.store(true, std::memory_order_release);
    }
    receive_wake_.notify_all();
    write_wake_.notify_all();
}

void L2ServerClient::writerLoop() {
    // Diagnostic sequence only. Independent of recovered TLS IDs and socket handles.
    static std::atomic<unsigned long long> nextLogIndex{0};
    runtime::logging::SetIndexedThreadName("peer-write", nextLogIndex.fetch_add(1, std::memory_order_relaxed));
    for (;;) {
        WriteJob job;
        {
            std::unique_lock<std::mutex> writeGuard(write_mutex_);
            write_wake_.wait(writeGuard, [this] {
                return !write_queue_.empty() || stop_.load(std::memory_order_acquire) ||
                    target_close_requested_.load(std::memory_order_acquire);
            });
            if (write_queue_.empty()) {
                return;
            }
            job = std::move(write_queue_.front());
            write_queue_.pop_front();
        }

        int sent = -1;
        int socketError = 0;
        const bool cancelledBeforeSend = stop_.load(std::memory_order_acquire) ||
            target_close_requested_.load(std::memory_order_acquire) || socket_.load(std::memory_order_acquire) != job.socket;
        if (cancelledBeforeSend) {
            // closesocket cancels an already submitted overlapped WSASend.
            socketError = 995;
        } else {
            const NativeSocket socket = loadSocket(job.socket);
            std::lock_guard<std::mutex> sendGuard(send_mutex_);
            while (job.transferred < job.length) {
#ifdef _WIN32
                sent =
                    send(socket, reinterpret_cast<const char*>(job.data + job.transferred), job.length - job.transferred, 0);
#else
                int sendFlags = 0;
#if defined(MSG_NOSIGNAL)
                sendFlags = MSG_NOSIGNAL;
#endif
                sent = static_cast<int>(send(socket, job.data + job.transferred, job.length - job.transferred, sendFlags));
                if (sent < 0 && errno == EINTR && !stop_.load(std::memory_order_acquire) &&
                    !target_close_requested_.load(std::memory_order_acquire)) {
                    continue;
                }
#endif
                if (sent > 0) {
                    job.transferred += sent;
                    continue;
                }
                if (sent == 0) {
                    socketError = 10054;
                }
                break;
            }
            if (sent < 0) {
                socketError =
                    stop_.load(std::memory_order_acquire) || target_close_requested_.load(std::memory_order_acquire)
                    ? 995
                    : winSockCompatibleError(lastSocketError());
            }
        }

        if (job.completion) {
            job.completion(
                job.transferred == job.length, job.transferred == job.length ? static_cast<std::uint32_t>(job.length) : 0,
                socketError
            );
        }
    }
}

void L2ServerClient::readerLoop(ReceiveSink receiveSink, CloseSink closeSink) {
    // Diagnostic sequence only. Independent of recovered TLS IDs and socket handles.
    static std::atomic<unsigned long long> nextLogIndex{0};
    runtime::logging::SetIndexedThreadName("peer-read", nextLogIndex.fetch_add(1, std::memory_order_relaxed));
    std::array<std::uint8_t, kMaximumReadSize> buffer{};
    while (!stop_.load() && !target_close_requested_.load(std::memory_order_acquire)) {
        if (!receive_enabled_.load()) {
            std::unique_lock<std::mutex> receiveGuard(receive_mutex_);
            receive_wake_.wait(receiveGuard, [this] {
                return stop_.load() || receive_enabled_.load() || target_close_requested_.load(std::memory_order_acquire);
            });
            if (stop_.load() || target_close_requested_.load(std::memory_order_acquire)) {
                break;
            }
        }

        int readCapacity = next_read_capacity_.load(std::memory_order_acquire);
        if (readCapacity < 0) {
            std::unique_lock<std::mutex> receiveGuard(receive_mutex_);
            receive_wake_.wait(receiveGuard, [this] {
                return stop_.load(std::memory_order_acquire) || target_close_requested_.load(std::memory_order_acquire) ||
                    next_read_capacity_.load(std::memory_order_acquire) >= 0;
            });
            if (stop_.load(std::memory_order_acquire) || target_close_requested_.load(std::memory_order_acquire)) {
                break;
            }
            readCapacity = next_read_capacity_.load(std::memory_order_acquire);
        }
        if (readCapacity == 0) {
            // A zero-length WSARecv completes successfully with zero bytes.
            // the recovered ReadCallback owns the diagnostic and close path.
            last_read_error_.store(0, std::memory_order_release);
            break;
        }

        const std::intptr_t stored = socket_.load();
        if (stored == kInvalidSocket) {
            break;
        }
        NativeSocket socket = loadSocket(stored);
        const int readiness = waitReadable(socket, 250);
        if (readiness < 0) {
            const int readinessError = lastSocketError();
            if (interruptedSocketCall(readinessError)) {
                continue;
            }
            last_read_error_.store(winSockCompatibleError(readinessError), std::memory_order_release);
            break;
        }
        if (readiness == 0) {
            continue;
        }
        // This recv consumes the single WSARecv armed by CIOSocketEx::Read.
        // The next native read remains disabled until that object's IOCP-like
        // completion reaches OnRead and arms another operation.
        next_read_capacity_.store(-1, std::memory_order_release);
#ifdef _WIN32
        const int received = recv(socket, reinterpret_cast<char*>(buffer.data()), readCapacity, 0);
        if (received < 0) {
            last_read_error_.store(winSockCompatibleError(lastSocketError()), std::memory_order_release);
        }
#else
        const int received = static_cast<int>(recv(socket, buffer.data(), static_cast<std::size_t>(readCapacity), 0));
        if (received < 0 && errno == EINTR) {
            next_read_capacity_.store(readCapacity, std::memory_order_release);
            continue;
        }
        if (received < 0) {
            last_read_error_.store(winSockCompatibleError(lastSocketError()), std::memory_order_release);
        }
#endif
        if (received <= 0) {
            break;
        }

        if (receiveSink) {
            receiveSink(buffer.data(), received);
        }
    }

    const bool terminalCompletion = !stop_.load(std::memory_order_acquire);
    if (terminalCompletion) {
        if (closeSink) {
            closeSink();
            // The target leaves the SOCKET open until its IO completion has
            // entered CIOSocketEx::Close. Terminal dispatch may be queued on
            // another IO worker, so wait only for that recovered boundary or
            // for cooperative owner shutdown.
            std::unique_lock<std::mutex> receiveGuard(receive_mutex_);
            receive_wake_.wait(receiveGuard, [this] {
                return stop_.load(std::memory_order_acquire) || target_close_requested_.load(std::memory_order_acquire);
            });
        }
    }

    const std::intptr_t stored = socket_.exchange(kInvalidSocket);
    if (stored != kInvalidSocket) {
        const NativeSocket socket = loadSocket(stored);
        // Wake a concurrent blocking writer before waiting for send_mutex_.
        // closeSocket remains serialized with SendRaw to prevent immediate
        // descriptor/handle reuse.
        if (!stop_.load(std::memory_order_acquire) && abortive_close_requested_.load(std::memory_order_acquire)) {
            setAbortiveLinger(socket);
        }
        shutdownSocket(socket);
        std::lock_guard<std::mutex> send_guard(send_mutex_);
        closeSocket(socket);
    }
}

} // namespace network
