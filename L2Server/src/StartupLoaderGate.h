#pragma once

#include <cstdint>

namespace l2server {

enum class StartupLoaderMode : std::uint8_t {
    Standalone,
    ResourceLoader,
};

enum class StartupLoaderGateFailure : std::uint8_t {
    None,
    LoaderMustBeTerminated,
    DuplicateStandaloneServer,
    LoaderMustBeStarted,
    LoaderStillLoading,
    DuplicateLoaderServer,
};

// These are the raw GetLastError observations consumed by current application
// root 0080a32c. The current body does not branch on returned mutex handles.
struct StartupLoaderGateObservations final {
    std::uint32_t globalLoaderLastError = 0;
    std::uint32_t loadingMarkerLastError = 0;
    std::uint32_t modeMarkerLastError = 0;
};

struct StartupLoaderGateResult final {
    StartupLoaderMode mode = StartupLoaderMode::Standalone;
    StartupLoaderGateFailure failure = StartupLoaderGateFailure::None;
    bool globalLoaderProbeAttempted = true;
    bool loadingMarkerProbeAttempted = false;
    bool modeMarkerCreationAttempted = false;
    bool proceedToWorldInitialization = false;
};

// Exact platform-independent decision portion of current 0080a69e..0080a792.
// Mutex probing/creation itself remains a deferred platform boundary.
StartupLoaderGateResult EvaluateStartupLoaderGate(bool useResourceLoader, const StartupLoaderGateObservations& observations);

} // namespace l2server
