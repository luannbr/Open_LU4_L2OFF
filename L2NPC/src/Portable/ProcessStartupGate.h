#pragma once

#include <memory>
#include <string>

namespace l2npc::portable {

enum class ProcessStartupGateResult {
    Ready,
    AlreadyRunning,
    NpcRemovalInProgress,
};

enum class L2ServerStartupState {
    Ready,
    ResourceLoaderModeMismatch,
    SharedMemoryNotInitialized,
};

// OS transport for the two process-lifetime named mutexes acquired by
// WinMain. The target classes do not know about this adapter.
class ProcessStartupGate {
public:
    ProcessStartupGate();
    ~ProcessStartupGate();

    ProcessStartupGate(const ProcessStartupGate&) = delete;
    ProcessStartupGate& operator=(const ProcessStartupGate&) = delete;

    ProcessStartupGateResult Acquire(std::string* diagnostic = nullptr) const;
    L2ServerStartupState CheckL2ServerState(bool useResourceLoader, std::string* diagnostic = nullptr) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace l2npc::portable
