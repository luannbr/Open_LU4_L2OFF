#include "Portable/NPCServerConnection.h"

#include <mutex>
#include <utility>

namespace l2npc::portable {
namespace {

struct NPCServerConnectionRegistry {
    std::mutex mutex;
    NPCServerConnectSink connectSink;
    std::uint64_t generation = 0;
    std::uint64_t nextGeneration = 0;
};

NPCServerConnectionRegistry& Registry() {
    // Winsock is process-global in the target. Keep the portable replacement
    // available while process globals release their target objects.
    static NPCServerConnectionRegistry* registry = new NPCServerConnectionRegistry;
    return *registry;
}

} // namespace

NPCServerConnectionBinding::NPCServerConnectionBinding(NPCServerConnectSink connectSink) { Reset(std::move(connectSink)); }

NPCServerConnectionBinding::~NPCServerConnectionBinding() { Reset(); }

NPCServerConnectionBinding::NPCServerConnectionBinding(NPCServerConnectionBinding&& other) noexcept :
    generation_(other.generation_) {
    other.generation_ = 0;
}

NPCServerConnectionBinding& NPCServerConnectionBinding::operator=(NPCServerConnectionBinding&& other) noexcept {
    if (this != &other) {
        Reset();
        generation_ = other.generation_;
        other.generation_ = 0;
    }
    return *this;
}

void NPCServerConnectionBinding::Reset(NPCServerConnectSink connectSink) {
    NPCServerConnectionRegistry& registry = Registry();
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

NPCServerTransportConnection ConnectNPCServerTransport(const char* address, int port) {
    NPCServerConnectSink connectSink;
    {
        NPCServerConnectionRegistry& registry = Registry();
        std::lock_guard<std::mutex> guard(registry.mutex);
        connectSink = registry.connectSink;
    }
    return connectSink ? connectSink(address, port) : NPCServerTransportConnection{};
}

} // namespace l2npc::portable
