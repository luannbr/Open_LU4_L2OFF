#include "Portable/NPCServerThread.h"

#include <mutex>
#include <utility>

namespace l2npc::portable {
namespace {

struct NPCServerThreadRegistry {
    std::mutex mutex;
    NPCServerThreadStartSink startSink;
    std::uint64_t generation = 0;
    std::uint64_t nextGeneration = 0;
};

NPCServerThreadRegistry& Registry() {
    static NPCServerThreadRegistry* registry = new NPCServerThreadRegistry;
    return *registry;
}

} // namespace

NPCServerThreadBinding::NPCServerThreadBinding(NPCServerThreadStartSink startSink) { Reset(std::move(startSink)); }

NPCServerThreadBinding::~NPCServerThreadBinding() { Reset(); }

NPCServerThreadBinding::NPCServerThreadBinding(NPCServerThreadBinding&& other) noexcept : generation_(other.generation_) {
    other.generation_ = 0;
}

NPCServerThreadBinding& NPCServerThreadBinding::operator=(NPCServerThreadBinding&& other) noexcept {
    if (this != &other) {
        Reset();
        generation_ = other.generation_;
        other.generation_ = 0;
    }
    return *this;
}

void NPCServerThreadBinding::Reset(NPCServerThreadStartSink startSink) {
    NPCServerThreadRegistry& registry = Registry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    if (generation_ != 0 && registry.generation == generation_) {
        registry.startSink = {};
        registry.generation = 0;
    }
    generation_ = 0;
    if (startSink) {
        registry.startSink = std::move(startSink);
        generation_ = ++registry.nextGeneration;
        registry.generation = generation_;
    }
}

void* StartNPCServerThread() {
    NPCServerThreadStartSink startSink;
    {
        NPCServerThreadRegistry& registry = Registry();
        std::lock_guard<std::mutex> guard(registry.mutex);
        startSink = registry.startSink;
    }
    return startSink ? startSink() : nullptr;
}

} // namespace l2npc::portable
