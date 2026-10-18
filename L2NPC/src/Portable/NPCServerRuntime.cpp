#include "Portable/NPCServerRuntime.h"

#include <mutex>
#include <utility>

namespace l2npc::portable {
namespace {

struct NPCServerRuntimeRegistry {
    std::mutex mutex;
    NPCServerRuntimeServices services;
    std::uint64_t generation = 0;
    std::uint64_t nextGeneration = 0;
};

NPCServerRuntimeRegistry& Registry() {
    static NPCServerRuntimeRegistry* registry = new NPCServerRuntimeRegistry;
    return *registry;
}

} // namespace

NPCServerRuntimeBinding::NPCServerRuntimeBinding(NPCServerRuntimeServices services) { Reset(std::move(services)); }

NPCServerRuntimeBinding::~NPCServerRuntimeBinding() { Reset(); }

NPCServerRuntimeBinding::NPCServerRuntimeBinding(NPCServerRuntimeBinding&& other) noexcept : generation_(other.generation_) {
    other.generation_ = 0;
}

NPCServerRuntimeBinding& NPCServerRuntimeBinding::operator=(NPCServerRuntimeBinding&& other) noexcept {
    if (this != &other) {
        Reset();
        generation_ = other.generation_;
        other.generation_ = 0;
    }
    return *this;
}

void NPCServerRuntimeBinding::Reset(NPCServerRuntimeServices services) {
    NPCServerRuntimeRegistry& registry = Registry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    if (generation_ != 0 && registry.generation == generation_) {
        registry.services = {};
        registry.generation = 0;
    }
    generation_ = 0;
    if (services.stopRequested || services.waitForRetryOrStop || services.initializeIO || services.startIOThreads ||
        services.startSocketReceivers || services.startupReady || services.startupFailed) {
        registry.services = std::move(services);
        generation_ = ++registry.nextGeneration;
        registry.generation = generation_;
    }
}

NPCServerRuntimeServices GetNPCServerRuntimeServices() {
    NPCServerRuntimeRegistry& registry = Registry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    return registry.services;
}

void InitializeIOThreadTransport() {
    std::function<void()> initialize;
    {
        NPCServerRuntimeRegistry& registry = Registry();
        std::lock_guard<std::mutex> guard(registry.mutex);
        initialize = registry.services.initializeIO;
    }
    if (initialize) {
        initialize();
    }
}

} // namespace l2npc::portable
