#include "Portable/NPCServerConfiguration.h"

#include <mutex>

namespace l2npc::portable {
namespace {

struct NPCServerConfigurationRegistry {
    std::mutex mutex;
    const runtime::Config* configuration = nullptr;
    std::uint64_t generation = 0;
    std::uint64_t nextGeneration = 0;
};

NPCServerConfigurationRegistry& Registry() {
    static NPCServerConfigurationRegistry* registry = new NPCServerConfigurationRegistry;
    return *registry;
}

} // namespace

NPCServerConfigurationBinding::NPCServerConfigurationBinding(const runtime::Config* configuration) { Reset(configuration); }

NPCServerConfigurationBinding::~NPCServerConfigurationBinding() { Reset(); }

NPCServerConfigurationBinding::NPCServerConfigurationBinding(NPCServerConfigurationBinding&& other) noexcept :
    generation_(other.generation_) {
    other.generation_ = 0;
}

NPCServerConfigurationBinding& NPCServerConfigurationBinding::operator=(NPCServerConfigurationBinding&& other) noexcept {
    if (this != &other) {
        Reset();
        generation_ = other.generation_;
        other.generation_ = 0;
    }
    return *this;
}

void NPCServerConfigurationBinding::Reset(const runtime::Config* configuration) {
    NPCServerConfigurationRegistry& registry = Registry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    if (generation_ != 0 && registry.generation == generation_) {
        registry.configuration = nullptr;
        registry.generation = 0;
    }
    generation_ = 0;
    if (configuration) {
        registry.configuration = configuration;
        generation_ = ++registry.nextGeneration;
        registry.generation = generation_;
    }
}

const runtime::Config* GetNPCServerConfiguration() {
    NPCServerConfigurationRegistry& registry = Registry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    return registry.configuration;
}

} // namespace l2npc::portable
