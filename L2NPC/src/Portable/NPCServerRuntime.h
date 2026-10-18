#pragma once

#include <cstdint>
#include <functional>
#include <memory>

class IOThreadCallback;

namespace l2npc::portable {

using IOThreadCallbackFactory = std::function<std::shared_ptr<IOThreadCallback>(int)>;

// Cross-platform substitutions for the process/thread primitives called by
// NPCServer::Run. Target-owned startup decisions remain in NPCServer.cpp.
struct NPCServerRuntimeServices {
    std::function<bool()> stopRequested;
    // Returns true when shutdown was requested while waiting.
    std::function<bool()> waitForRetryOrStop;
    std::function<void()> initializeIO;
    std::function<void(int, IOThreadCallbackFactory)> startIOThreads;
    std::function<void()> startSocketReceivers;
    std::function<void()> startupReady;
    std::function<void()> startupFailed;
};

class NPCServerRuntimeBinding {
public:
    NPCServerRuntimeBinding() = default;
    explicit NPCServerRuntimeBinding(NPCServerRuntimeServices services);
    ~NPCServerRuntimeBinding();

    NPCServerRuntimeBinding(const NPCServerRuntimeBinding&) = delete;
    NPCServerRuntimeBinding& operator=(const NPCServerRuntimeBinding&) = delete;

    NPCServerRuntimeBinding(NPCServerRuntimeBinding&& other) noexcept;
    NPCServerRuntimeBinding& operator=(NPCServerRuntimeBinding&& other) noexcept;

    void Reset(NPCServerRuntimeServices services = {});

private:
    std::uint64_t generation_ = 0;
};

NPCServerRuntimeServices GetNPCServerRuntimeServices();

// Native CreateEvent/CreateIoCompletionPort transport invoked from the
// recovered InitIOThread owner, at the same point as the current image.
void InitializeIOThreadTransport();

} // namespace l2npc::portable
