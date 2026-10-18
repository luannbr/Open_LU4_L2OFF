#pragma once

#include "Shared/src/Systems.h"

#include <cstdint>

using IpListMonitorCallback = unsigned int (*)(void*);

// Original owner: Src/iplistmonitor.h. L2Authd.pdb type 0x7306,
// field list 0x7305, target size 0x230. Opaque Win32 handles keep their
// native-width spelling so the same class can carry the matching host
// event and worker handles on non-Windows systems.
class CIpListMonitor {
public:
    explicit CIpListMonitor(wchar_t* fileName);
    virtual ~CIpListMonitor();

    unsigned int MonitorLoop();
    void StartMonitor(
        IpListMonitorCallback callback,
        void* parameter);
    void StopMonitor();
    static FileTime GetFileTimestamp(wchar_t* fileName);

private:
    IpListMonitorCallback m_fpCallback;
    void* m_pParam;
    void* m_handles[3];
    std::uint32_t threadId;
    wchar_t m_ipListPath[260];
    wchar_t* m_pBackSlash;
    FileTime m_ftLastWriteTime;
};

#if defined(_WIN32)
unsigned int __stdcall IpListFileMonitorThread(void* parameter);
#else
unsigned int IpListFileMonitorThread(void* parameter);
#endif
