#include "StartupLoaderGate.h"

namespace l2server {

namespace {

constexpr std::uint32_t kErrorFileNotFound = 2;
constexpr std::uint32_t kErrorAccessDenied = 5;
constexpr std::uint32_t kErrorAlreadyExists = 183;

} // namespace

StartupLoaderGateResult EvaluateStartupLoaderGate(
    bool useResourceLoader, const StartupLoaderGateObservations& observations
) {
    StartupLoaderGateResult result;
    result.mode = useResourceLoader ? StartupLoaderMode::ResourceLoader : StartupLoaderMode::Standalone;

    if (useResourceLoader) {
        if (observations.globalLoaderLastError == kErrorFileNotFound) {
            result.failure = StartupLoaderGateFailure::LoaderMustBeStarted;
            return result;
        }

        result.loadingMarkerProbeAttempted = true;
        if (observations.loadingMarkerLastError == kErrorAccessDenied) {
            result.failure = StartupLoaderGateFailure::LoaderStillLoading;
            return result;
        }

        result.modeMarkerCreationAttempted = true;
        if (observations.modeMarkerLastError == kErrorAlreadyExists) {
            result.failure = StartupLoaderGateFailure::DuplicateLoaderServer;
            return result;
        }
    } else {
        if (observations.globalLoaderLastError != kErrorFileNotFound) {
            result.failure = StartupLoaderGateFailure::LoaderMustBeTerminated;
            return result;
        }

        result.modeMarkerCreationAttempted = true;
        if (observations.modeMarkerLastError == kErrorAlreadyExists) {
            result.failure = StartupLoaderGateFailure::DuplicateStandaloneServer;
            return result;
        }
    }

    result.proceedToWorldInitialization = true;
    return result;
}

} // namespace l2server
