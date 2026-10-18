#pragma once

#include "Shared/src/Runtime/WindowsTypes.h"

#include <cstddef>
#include <cstdint>

// Shared Win32 service-configuration carrier compiled in both authentication
// products. Product-specific CServiceManager method bodies remain in their
// owning compilation units. Only the byte-for-byte record is shared here.
struct SRVARG {
    char lpServiceName[260]{};
    char lpDisplayName[260]{};
    char lpDescription[260]{};
    char lpBinaryPathName[260]{};
    char lpModulePath[260]{};
    std::uint32_t dwDesiredAccess = 0;
    std::uint32_t dwServiceType = 0;
    std::uint32_t dwStartType = 0;
    std::uint32_t dwErrorControl = 0;
    char lpDependencies[260]{};
    char lpServiceStartName[260]{};
    char lpPassword[260]{};
    SERVICE_FAILURE_ACTIONSA sfa{};
    SC_ACTION sfaAction[3]{};
    char sfaReBootMessage[260]{};
    char sfaRetryCommand[260]{};
    unsigned int sfaFailureCounter = 0;
    SERVICE_STATUS_HANDLE sshHandler = nullptr;
};
