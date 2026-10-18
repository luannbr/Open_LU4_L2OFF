#pragma once

#include "Shared/src/Runtime/WindowsTypes.h"

#include <cstdint>
#include <memory>
#include <string>

class CWindowMgr;

namespace authgated::platform {

enum class InstanceGateResult {
    Acquired,
    Bypassed,
    AlreadyRunning,
    Error,
};

InstanceGateResult AcquireInstanceGate(
    bool allowMultipleInstances,
    void*& nativeHandle,
    std::string& diagnostic);
void CloseInstanceGate(void*& nativeHandle) noexcept;

bool SetWorkingDirectoryToExecutableDirectory(
    std::string& diagnostic);
int StartSocketRuntime() noexcept;
void StopSocketRuntime() noexcept;
void DisableProcessPriorityBoost() noexcept;
void ShowAlreadyRunning();
void ShowSocketStartupError(int error);

#if defined(_WIN32)
#  define AUTHGATED_SERVICE_CALL __stdcall
#else
#  define AUTHGATED_SERVICE_CALL
#endif

using ServiceControlHandler = std::uint32_t (
    AUTHGATED_SERVICE_CALL *)(
        std::uint32_t,
        std::uint32_t,
        void*,
        void*);
using ServiceMain = int (*)(int, char**);

SERVICE_STATUS_HANDLE RegisterServiceControlHandler(
    const char* serviceName,
    ServiceControlHandler handler) noexcept;
bool StartServiceDispatcher(
    const char* serviceName,
    ServiceMain serviceMain) noexcept;

void PostProcessQuit() noexcept;

// Translate console stop requests into the timer loop's quit request.
class TerminationBridge {
public:
    explicit TerminationBridge(CWindowMgr* window);
    ~TerminationBridge();

    TerminationBridge(const TerminationBridge&) = delete;
    TerminationBridge& operator=(const TerminationBridge&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace authgated::platform

#undef AUTHGATED_SERVICE_CALL
