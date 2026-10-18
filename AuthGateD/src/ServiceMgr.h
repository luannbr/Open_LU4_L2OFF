#pragma once

#include "Shared/src/Runtime/ServiceManagerTypes.h"

#include <cstdint>

// Original owner: servicemgr.h / servicemgr.cpp. AuthGateD.pdb records a
// four-byte class containing only the vptr. Configuration is the static
// 0xa6c-byte SRVARG shared by every instance.
class CServiceManager {
public:
    CServiceManager();
    virtual ~CServiceManager();

    void ConfigServiceName(const char* serviceName);
    void ConfigServiceDisp(const char* displayName);
    void ConfigServiceDesc(const char* description);
    void SetServiceHandler(SERVICE_STATUS_HANDLE handler);
    SERVICE_STATUS_HANDLE GetServiceHandler();
    std::uint32_t Install();
    std::uint32_t GetServiceRunStatus(SERVICE_STATUS* status);
    std::uint32_t SetServiceRunStatus(std::uint32_t status);
    std::uint32_t Stop(int force, std::uint32_t waitSeconds);
    int IsServiceMode();
    std::uint32_t Uninstall(int force, std::uint32_t waitSeconds);

    static SRVARG m_InstallParam;

protected:
    void ConfigInitialize();

private:
    class CSrvHandle {
    public:
        CSrvHandle() = default;
        ~CSrvHandle();

        void* hSrv = nullptr;
        void* hScm = nullptr;
    };
};

extern CServiceManager g_serviceManager;
