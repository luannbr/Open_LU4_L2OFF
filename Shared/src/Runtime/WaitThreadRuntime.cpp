#include "Shared/src/Runtime/WaitThreadRuntime.h"

#include <mutex>
#include <utility>

namespace portable {
namespace {

struct WaitThreadRuntimeRegistry {
    std::mutex mutex;
    WaitThreadRuntimeServices services;
    std::uint64_t generation = 0;
    std::uint64_t nextGeneration = 0;
};

WaitThreadRuntimeRegistry& Registry() {
    static WaitThreadRuntimeRegistry* registry = new WaitThreadRuntimeRegistry;
    return *registry;
}

} // namespace

WaitThreadRuntimeBinding::WaitThreadRuntimeBinding(WaitThreadRuntimeServices services) { Reset(std::move(services)); }

WaitThreadRuntimeBinding::~WaitThreadRuntimeBinding() { Reset(); }

WaitThreadRuntimeBinding::WaitThreadRuntimeBinding(WaitThreadRuntimeBinding&& other) noexcept :
    generation_(other.generation_) {
    other.generation_ = 0;
}

WaitThreadRuntimeBinding& WaitThreadRuntimeBinding::operator=(WaitThreadRuntimeBinding&& other) noexcept {
    if (this != &other) {
        Reset();
        generation_ = other.generation_;
        other.generation_ = 0;
    }
    return *this;
}

void WaitThreadRuntimeBinding::Reset(WaitThreadRuntimeServices services) {
    WaitThreadRuntimeRegistry& registry = Registry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    if (generation_ != 0 && registry.generation == generation_) {
        registry.services = {};
        registry.generation = 0;
    }
    generation_ = 0;
    if (services.stopRequested || services.waitForStop || services.waitForObjects) {
        registry.services = std::move(services);
        generation_ = ++registry.nextGeneration;
        registry.generation = generation_;
    }
}

WaitThreadRuntimeServices GetWaitThreadRuntimeServices() {
    WaitThreadRuntimeRegistry& registry = Registry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    return registry.services;
}

} // namespace portable
