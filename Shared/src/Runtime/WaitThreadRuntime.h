#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

namespace portable {

// Cross-platform replaces for the Sleep/wake primitive used by
// Shared/ioc.cpp::WaitThread. Timer dispatch and watchdog decisions remain
// in the recovered Shared implementation. This transport is shared by every
// executable that links its process-specific Shared/ioc.cpp creation.
struct WaitThreadRuntimeServices {
    std::function<bool()> stopRequested;
    std::function<bool(std::chrono::milliseconds)> waitForStop;
    // Host transport for the target's opaque WaitForMultipleObjects handles.
    // A returned index selects the matching CIOObject::OnWaitCallback.
    std::function<std::optional<std::size_t>(void* const*, std::size_t, std::chrono::milliseconds)> waitForObjects;
};

class WaitThreadRuntimeBinding {
public:
    WaitThreadRuntimeBinding() = default;
    explicit WaitThreadRuntimeBinding(WaitThreadRuntimeServices services);
    ~WaitThreadRuntimeBinding();

    WaitThreadRuntimeBinding(const WaitThreadRuntimeBinding&) = delete;
    WaitThreadRuntimeBinding& operator=(const WaitThreadRuntimeBinding&) = delete;

    WaitThreadRuntimeBinding(WaitThreadRuntimeBinding&& other) noexcept;
    WaitThreadRuntimeBinding& operator=(WaitThreadRuntimeBinding&& other) noexcept;

    void Reset(WaitThreadRuntimeServices services = {});

private:
    std::uint64_t generation_ = 0;
};

WaitThreadRuntimeServices GetWaitThreadRuntimeServices();

} // namespace portable
