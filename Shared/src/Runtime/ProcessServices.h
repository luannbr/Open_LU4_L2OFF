#pragma once

#include "Shared/src/Runtime/ServiceManagerTypes.h"

#include <cstdint>
#include <string>

namespace runtime {

enum class InstanceGateResult {
    Acquired,
    Bypassed,
    AlreadyRunning,
    Error,
};

InstanceGateResult AcquireNamedInstanceGate(
    bool allowMultipleInstances,
    const char16_t* windowsName,
    const char* portableLockName,
    void*& nativeHandle,
    std::string& diagnostic
);
void CloseNamedInstanceGate(void*& nativeHandle) noexcept;

bool SetWorkingDirectoryToExecutableDirectory(std::string& diagnostic);
void DisableCurrentProcessPriorityBoost() noexcept;

#if defined(_WIN32)
#define L2PORTABLE_SERVICE_CALL __stdcall
#else
#define L2PORTABLE_SERVICE_CALL
#endif

using ServiceControlHandler = std::uint32_t(L2PORTABLE_SERVICE_CALL*)(std::uint32_t, std::uint32_t, void*, void*);
using ServiceMain = int (*)(int, char**);

SERVICE_STATUS_HANDLE RegisterServiceControlHandler(const char* serviceName, ServiceControlHandler handler) noexcept;
bool StartServiceDispatcher(const char* serviceName, ServiceMain serviceMain) noexcept;

} // namespace runtime

#undef L2PORTABLE_SERVICE_CALL
