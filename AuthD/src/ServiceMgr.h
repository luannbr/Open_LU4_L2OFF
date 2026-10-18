#pragma once

#include "Shared/src/Runtime/ServiceManagerTypes.h"

#include <cstdint>

// Original owner: Src/servicemgr.h / Src/servicemgr.cpp. The selected x86
// class itself is four bytes and contains only its vptr. All state is held in
// the public static SRVARG above.
class CServiceManager {
public:
    CServiceManager();
    virtual ~CServiceManager();

    std::uint32_t Install();
    std::uint32_t Uninstall(int bForce, std::uint32_t dwWait);
    std::uint32_t Start(std::uint32_t dwWait);
    std::uint32_t Stop(int bForce, std::uint32_t dwWait);
    std::uint32_t Pause(std::uint32_t dwWait);
    std::uint32_t Continue(std::uint32_t dwWait);

    std::uint32_t GetServiceRunStatus(SERVICE_STATUS& ss);
    std::uint32_t GetServiceRunStatus();
    std::uint32_t SetServiceRunStatus(std::uint32_t dwStatus);

    void SetServiceHandler(SERVICE_STATUS_HANDLE sshHandler);
    SERVICE_STATUS_HANDLE GetServiceHandler();
    int IsServiceMode();

    void ConfigServiceName(char* lpServiceName);
    void ConfigServiceDisp(char* lpDisplayName);
    void ConfigServiceDesc(char* lpDescription);
    void ConfigServiceExec(char* lpBinaryPathName);
    void ConfigDependencies(char* lpDependencies);
    void ConfigSfaRebootMessage(char* lpRebootMessage);
    void ConfigSfaRetryCommand(char* lpRetryCommand);
    void ConfigSfaCounterReset(unsigned int dwFailureCounter);

    static SRVARG m_InstallParam;

protected:
    void ConfigInitialize();

private:
    // PDB type 0x6FDB, sizeof eight bytes in the selected x86 build.
    class CSrvHandle {
    public:
        CSrvHandle();
        ~CSrvHandle();

#if defined(_WIN32)
        SC_HANDLE hSrv;
        SC_HANDLE hScm;
#else
        void* hSrv;
        void* hScm;
#endif
    };
};
