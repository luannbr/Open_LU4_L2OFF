// Shared portable full-duplex transport used by the recovered server clients.
#pragma once

#include "Shared/src/Runtime/SocketTransport.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace network {

enum class SocketConnectFailure {
    None,
    Initialize,
    ResolveAddress,
    CreateSocket,
    BindSocket,
    ConnectSocket,
};

struct SocketConnectResult {
    bool connected = false;
    SocketConnectFailure failure = SocketConnectFailure::None;
    int error = 0;

    explicit operator bool() const noexcept { return connected; }
};

class L2ServerClient {
public:
    using ReceiveSink = std::function<void(const std::uint8_t*, int)>;
    using CloseSink = std::function<void()>;

    struct ConnectOptions {
        int timeoutMilliseconds = 400;
        bool startReceiving = true;
        bool blocking = false;
        bool numericIPv4Only = false;
        bool bindEphemeralIPv4 = false;
        // Current CDBNPC::InitDBNPC loses its SOCKET on bind/connect failure.
        // the other recovered clients close theirs. This option keeps
        // that owner-specific legacy lifetime without changing the common
        // transport default.
        bool abandonSocketOnBindOrConnectFailure = false;
    };

    explicit L2ServerClient(std::string endpoint_name = "l2server");
    ~L2ServerClient();

    L2ServerClient(const L2ServerClient&) = delete;
    L2ServerClient& operator=(const L2ServerClient&) = delete;

    // Sinks are snapshotted by ConnectWithResult for that native endpoint.
    // replacing them cannot retarget already-issued receive completions.
    void SetReceiveSink(ReceiveSink receive, CloseSink close = {});

    // NPCServer::Connect performs one TCP connect attempt. The owner controls
    // the original one-second retry policy.
    bool Connect(const std::string& host, int port, int timeout_ms = 400, bool startReceiving = true);
    bool Connect(const std::string& host, int port, const ConnectOptions& options);
    SocketConnectResult ConnectWithResult(const std::string& host, int port, const ConnectOptions& options);
    // InitIOThread installs the target's IOCP before Connect, but the queued
    // socket completions are not consumed until CreateIOThread2. Portable
    // startup mirrors that boundary by leaving bytes in the kernel receive
    // queue until all worker threads have been created.
    void StartReceiving();
    // The target's next WSARecv length is the unused tail of its persistent
    // CIOBufferEx. -1 means that no read is currently armed. Zero represents
    // the target's immediate zero-byte completion.
    void SetNextReadCapacity(int capacity);
    // Native socket-option half of current CIOSocketEx::Initialize.
    void InitializeTargetSocket();
    void Close();
    bool IsConnected() const;
    // Zero identifies an orderly EOF. Nonzero is the WinSock-compatible
    // receive error captured before terminal dispatch.
    int LastReadError() const;

    // Submit one target WSASend buffer. Its IoRetain keeps data alive until
    // completion is delivered back to the recovered IO callback.
    portable::SocketWriteSubmission SubmitRawWrite(
        const std::uint8_t* data, int length, portable::SocketWriteCompletion completion
    );

    // Called by the protocol parser on an invalid size/opcode. This follows
    // the peer-failure path so readerLoop calls OnClose exactly once.
    void RejectPeerProtocol(bool abortive = true);

    const std::string& Host() const { return host_; }
    int Port() const { return port_; }

private:
    static constexpr std::intptr_t kInvalidSocket = -1;
    static constexpr int kMaximumReadSize = 0x5000;

    void readerLoop(ReceiveSink receiveSink, CloseSink closeSink);
    void writerLoop();
    void signalTransportFailure(bool abortive);

    struct WriteJob {
        std::intptr_t socket = kInvalidSocket;
        const std::uint8_t* data = nullptr;
        int length = 0;
        int transferred = 0;
        portable::SocketWriteCompletion completion;
    };

    std::atomic<std::intptr_t> socket_{kInvalidSocket};
    std::atomic<bool> stop_{false};
    std::atomic<bool> receive_enabled_{true};
    std::atomic<int> next_read_capacity_{kMaximumReadSize};
    // CIOSocketEx::Close is abortive. GracefulClose is not. The native
    // reader performs the eventual descriptor close after its recv wakes.
    std::atomic<bool> abortive_close_requested_{true};
    // Set when the recovered CIOSocketEx has crossed its Close or
    // GracefulClose boundary. A peer-terminal reader keeps the native
    // descriptor alive until that target callback has run.
    std::atomic<bool> target_close_requested_{false};
    std::atomic<int> last_read_error_{0};
    // Close requests must be observable while ConnectWithResult holds the
    // lifecycle mutex around one target-style blocking connection attempt.
    std::atomic<std::uint64_t> close_generation_{0};
    std::thread reader_;
    std::thread writer_;
    std::mutex lifecycle_mutex_;
    std::mutex send_mutex_;
    std::mutex write_mutex_;
    std::mutex sink_mutex_;
    std::mutex receive_mutex_;
    std::condition_variable write_wake_;
    std::condition_variable receive_wake_;
    std::deque<WriteJob> write_queue_;
    ReceiveSink receive_sink_;
    CloseSink close_sink_;
    std::string host_;
    int port_ = 0;
};

} // namespace network
