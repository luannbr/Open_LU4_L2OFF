#pragma once

#include "Shared/src/Runtime/L2ServerClient.h"

#include <cstdint>
#include <functional>
#include <string>

namespace l2npc {
class CDBNPCSocket;
}

namespace l2npc::portable {

// CDBNPC::InitDBNPC owns the target-visible socket publication, Initialize
// call and revision packet. The host first establishes its native TCP
// transport, then CDBNPC constructs the same fresh CDBNPCSocket(socket)
// object as the target and attaches that transport to it.
struct DBNPCTransportConnection {
    using AttachSink = std::function<bool(CDBNPCSocket&)>;

    std::uint64_t socket = 0;
    network::SocketConnectResult connectResult;
    // Diagnostic snapshot of the attempted endpoint. Not part of a recovered
    // object, wire message, or connection decision.
    std::string endpointHost;
    std::uint16_t endpointPort = 0;
    AttachSink attachSink;

    explicit operator bool() const noexcept { return static_cast<bool>(attachSink); }

    bool Attach(CDBNPCSocket& targetSocket) const { return attachSink && attachSink(targetSocket); }
};

using DBNPCConnectSink = std::function<DBNPCTransportConnection()>;

class DBNPCConnectionBinding {
public:
    DBNPCConnectionBinding() = default;
    explicit DBNPCConnectionBinding(DBNPCConnectSink connectSink);
    ~DBNPCConnectionBinding();

    DBNPCConnectionBinding(const DBNPCConnectionBinding&) = delete;
    DBNPCConnectionBinding& operator=(const DBNPCConnectionBinding&) = delete;

    DBNPCConnectionBinding(DBNPCConnectionBinding&& other) noexcept;
    DBNPCConnectionBinding& operator=(DBNPCConnectionBinding&& other) noexcept;

    void Reset(DBNPCConnectSink connectSink = {});

private:
    std::uint64_t generation_ = 0;
};

DBNPCTransportConnection ConnectDBNPCTransport();

} // namespace l2npc::portable
