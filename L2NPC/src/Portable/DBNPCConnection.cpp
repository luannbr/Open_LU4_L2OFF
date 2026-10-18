#include "Portable/DBNPCConnection.h"

#include <mutex>
#include <utility>

namespace l2npc::portable {
namespace {

struct DBNPCConnectionRegistry {
    std::mutex mutex;
    DBNPCConnectSink connectSink;
    std::uint64_t generation = 0;
    std::uint64_t nextGeneration = 0;
};

DBNPCConnectionRegistry& Registry() {
    // Native Winsock configuration is process-global as well. Keep this
    // registry alive while late global destructors release target objects.
    static DBNPCConnectionRegistry* registry = new DBNPCConnectionRegistry;
    return *registry;
}

} // namespace

DBNPCConnectionBinding::DBNPCConnectionBinding(DBNPCConnectSink connectSink) { Reset(std::move(connectSink)); }

DBNPCConnectionBinding::~DBNPCConnectionBinding() { Reset(); }

DBNPCConnectionBinding::DBNPCConnectionBinding(DBNPCConnectionBinding&& other) noexcept : generation_(other.generation_) {
    other.generation_ = 0;
}

DBNPCConnectionBinding& DBNPCConnectionBinding::operator=(DBNPCConnectionBinding&& other) noexcept {
    if (this != &other) {
        Reset();
        generation_ = other.generation_;
        other.generation_ = 0;
    }
    return *this;
}

void DBNPCConnectionBinding::Reset(DBNPCConnectSink connectSink) {
    DBNPCConnectionRegistry& registry = Registry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    if (generation_ != 0 && registry.generation == generation_) {
        registry.connectSink = {};
        registry.generation = 0;
    }
    generation_ = 0;
    if (connectSink) {
        registry.connectSink = std::move(connectSink);
        generation_ = ++registry.nextGeneration;
        registry.generation = generation_;
    }
}

DBNPCTransportConnection ConnectDBNPCTransport() {
    DBNPCConnectSink connectSink;
    {
        DBNPCConnectionRegistry& registry = Registry();
        std::lock_guard<std::mutex> guard(registry.mutex);
        connectSink = registry.connectSink;
    }
    return connectSink ? connectSink() : DBNPCTransportConnection{};
}

} // namespace l2npc::portable
