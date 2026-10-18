#pragma once

#include "Shared/src/Runtime/L2ServerClient.h"

#include <cstdint>
#include <functional>

class CSocket;

namespace l2npc::portable {

// NPCServer::Connect owns the target-visible allocator, reference and
// Initialize calls. The portable binding replaces only socket/bind/connect
// and attaches the resulting host transport to that freshly allocated
// CSocket.
struct NPCServerTransportConnection {
    using AttachSink = std::function<bool(CSocket&)>;

    std::uint64_t socket = 0;
    network::SocketConnectResult connectResult;
    AttachSink attachSink;
    std::function<void()> abortSink;

    explicit operator bool() const noexcept { return static_cast<bool>(attachSink); }

    bool Attach(CSocket& targetSocket) const { return attachSink && attachSink(targetSocket); }

    void Abort() const {
        if (abortSink) {
            abortSink();
        }
    }
};

using NPCServerConnectSink = std::function<NPCServerTransportConnection(const char* address, int port)>;

class NPCServerConnectionBinding {
public:
    NPCServerConnectionBinding() = default;
    explicit NPCServerConnectionBinding(NPCServerConnectSink connectSink);
    ~NPCServerConnectionBinding();

    NPCServerConnectionBinding(const NPCServerConnectionBinding&) = delete;
    NPCServerConnectionBinding& operator=(const NPCServerConnectionBinding&) = delete;

    NPCServerConnectionBinding(NPCServerConnectionBinding&& other) noexcept;
    NPCServerConnectionBinding& operator=(NPCServerConnectionBinding&& other) noexcept;

    void Reset(NPCServerConnectSink connectSink = {});

private:
    std::uint64_t generation_ = 0;
};

NPCServerTransportConnection ConnectNPCServerTransport(const char* address, int port);

} // namespace l2npc::portable
