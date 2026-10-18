#include "ServiceMgr.h"

#include <cstring>

#if defined(_WIN32)
#include <Windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

SRVARG CServiceManager::m_InstallParam{};

namespace {

constexpr std::uint32_t kErrorSuccess = 0;
constexpr std::uint32_t kServiceRunning = 4;

#if defined(_WIN32)
constexpr std::uint32_t kErrorInvalidHandle = 6;
constexpr std::uint32_t kErrorServiceDoesNotExist = 1060;
constexpr std::uint32_t kErrorServiceNotActive = 1062;
constexpr std::uint32_t kErrorTimeout = 258;
constexpr std::uint32_t kServiceStopped = 1;
constexpr std::uint32_t kServicePaused = 7;
#else
constexpr std::uint32_t kErrorCallNotImplemented = 120;
#endif

#if !defined(_WIN32)
void GetExecutablePath(char (&path)[260]) {
#if defined(__APPLE__)
    std::uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) {
        path[0] = '\0';
    }
#else
    const auto length = ::readlink(
        "/proc/self/exe",
        path,
        sizeof(path) - 1);
    if (length < 0) {
        path[0] = '\0';
    } else {
        path[length] = '\0';
    }
#endif
}
#endif

} // namespace

// L2Authd.exe 0x00498350, Src/servicemgr.cpp:18-20.
CServiceManager::CServiceManager() {
    ConfigInitialize();
}

// L2Authd.exe 0x00498370, Src/servicemgr.cpp:62-64.
CServiceManager::~CServiceManager() = default;

// L2Authd.exe 0x00498390, Src/servicemgr.cpp:90-122.
std::uint32_t CServiceManager::Install() {
#if defined(_WIN32)
    CSrvHandle hHandle;
    hHandle.hScm = ::OpenSCManagerA(
        nullptr,
        nullptr,
        SC_MANAGER_ALL_ACCESS);
    if (hHandle.hScm == nullptr) {
        return kErrorInvalidHandle;
    }

    char* const password = m_InstallParam.lpPassword[0] == '\0'
        ? nullptr
        : m_InstallParam.lpPassword;
    char* const serviceStartName =
        m_InstallParam.lpServiceStartName[0] == '\0'
        ? nullptr
        : m_InstallParam.lpServiceStartName;
    hHandle.hSrv = ::CreateServiceA(
        hHandle.hScm,
        m_InstallParam.lpServiceName,
        m_InstallParam.lpDisplayName,
        m_InstallParam.dwDesiredAccess,
        m_InstallParam.dwServiceType,
        m_InstallParam.dwStartType,
        m_InstallParam.dwErrorControl,
        m_InstallParam.lpBinaryPathName,
        nullptr,
        nullptr,
        m_InstallParam.lpDependencies,
        serviceStartName,
        password);
    if (hHandle.hSrv == nullptr) {
        return ::GetLastError();
    }

    SERVICE_DESCRIPTIONA description{
        m_InstallParam.lpDescription};
    ::ChangeServiceConfig2A(
        hHandle.hSrv,
        SERVICE_CONFIG_DESCRIPTION,
        &description);
    m_InstallParam.sfa.cActions = 3;
    ::ChangeServiceConfig2A(
        hHandle.hSrv,
        SERVICE_CONFIG_FAILURE_ACTIONS,
        &m_InstallParam.sfa);
    return kErrorSuccess;
#else
    // Windows SCM is a platform transport, not AuthD product behavior. The
    // portable baseline runs in foreground and does not invent launchd or
    // systemd installation meaning.
    return kErrorCallNotImplemented;
#endif
}

// L2Authd.exe 0x00498510, Src/servicemgr.cpp:125-164.
std::uint32_t CServiceManager::Uninstall(
    [[maybe_unused]] int bForce,
    [[maybe_unused]] std::uint32_t dwWait) {
#if defined(_WIN32)
    CSrvHandle hHandle;
    hHandle.hScm = ::OpenSCManagerA(
        nullptr,
        nullptr,
        SC_MANAGER_ALL_ACCESS);
    if (hHandle.hScm == nullptr) {
        return kErrorInvalidHandle;
    }

    hHandle.hSrv = ::OpenServiceA(
        hHandle.hScm,
        m_InstallParam.lpServiceName,
        SERVICE_ALL_ACCESS);
    if (hHandle.hSrv == nullptr) {
        const std::uint32_t error = ::GetLastError();
        return error == kErrorServiceDoesNotExist
            ? kErrorSuccess
            : error;
    }

    SERVICE_STATUS ss{};
    const std::uint32_t statusError =
        GetServiceRunStatus(ss);
    if (statusError == kErrorSuccess) {
        if (ss.dwCurrentState != kServiceStopped &&
            Stop(bForce, dwWait) != kErrorSuccess) {
            return ::GetLastError();
        }
    } else if (statusError != kErrorServiceNotActive) {
        return ::GetLastError();
    }

    if (::DeleteService(hHandle.hSrv) == 0) {
        return ::GetLastError();
    }
    return kErrorSuccess;
#else
    return kErrorCallNotImplemented;
#endif
}

// L2Authd.exe 0x00498730, Src/servicemgr.cpp:219-248.
std::uint32_t CServiceManager::Start(
    [[maybe_unused]] std::uint32_t dwWait) {
#if defined(_WIN32)
    SERVICE_STATUS ss{};
    if (GetServiceRunStatus(ss) != kErrorSuccess) {
        return ::GetLastError();
    }
    if (ss.dwCurrentState == kServiceRunning) {
        return kErrorSuccess;
    }

    CSrvHandle hHandle;
    hHandle.hScm = ::OpenSCManagerA(
        nullptr,
        nullptr,
        SC_MANAGER_ALL_ACCESS);
    if (hHandle.hScm == nullptr) {
        return kErrorInvalidHandle;
    }
    hHandle.hSrv = ::OpenServiceA(
        hHandle.hScm,
        m_InstallParam.lpServiceName,
        SERVICE_START | SERVICE_QUERY_STATUS);
    if (hHandle.hSrv == nullptr) {
        return ::GetLastError();
    }
    if (::StartServiceA(hHandle.hSrv, 0, nullptr) == 0) {
        return ::GetLastError();
    }

    while (--dwWait != 0) {
        GetServiceRunStatus(ss);
        if (ss.dwCurrentState == kServiceRunning) {
            return kErrorSuccess;
        }
        ::Sleep(1000);
    }
    return kErrorTimeout;
#else
    return kErrorCallNotImplemented;
#endif
}

// L2Authd.exe 0x004988D0, Src/servicemgr.cpp:251-280.
std::uint32_t CServiceManager::Stop(
    [[maybe_unused]] int bForce,
    [[maybe_unused]] std::uint32_t dwWait) {
#if defined(_WIN32)
    SERVICE_STATUS ss{};
    if (GetServiceRunStatus(ss) != kErrorSuccess) {
        return ::GetLastError();
    }
    if (ss.dwCurrentState == kServiceStopped) {
        return kErrorSuccess;
    }

    CSrvHandle hHandle;
    hHandle.hScm = ::OpenSCManagerA(
        nullptr,
        nullptr,
        SC_MANAGER_ALL_ACCESS);
    if (hHandle.hScm == nullptr) {
        return kErrorInvalidHandle;
    }
    hHandle.hSrv = ::OpenServiceA(
        hHandle.hScm,
        m_InstallParam.lpServiceName,
        SERVICE_ALL_ACCESS);
    if (hHandle.hSrv == nullptr) {
        return ::GetLastError();
    }
    if (::ControlService(
            hHandle.hSrv,
            SERVICE_CONTROL_STOP,
            &ss) == 0) {
        return ::GetLastError();
    }

    while (--dwWait != 0) {
        GetServiceRunStatus(ss);
        if (ss.dwCurrentState == kServiceStopped) {
            return kErrorSuccess;
        }
        ::Sleep(1000);
    }
    return kErrorTimeout;
#else
    return kErrorCallNotImplemented;
#endif
}

// L2Authd.exe 0x00498A70, Src/servicemgr.cpp:283-312.
std::uint32_t CServiceManager::Pause(
    [[maybe_unused]] std::uint32_t dwWait) {
#if defined(_WIN32)
    SERVICE_STATUS ss{};
    if (GetServiceRunStatus(ss) != kErrorSuccess) {
        return ::GetLastError();
    }
    if (ss.dwCurrentState == kServicePaused) {
        return kErrorSuccess;
    }

    CSrvHandle hHandle;
    hHandle.hScm = ::OpenSCManagerA(
        nullptr,
        nullptr,
        SC_MANAGER_ALL_ACCESS);
    if (hHandle.hScm == nullptr) {
        return kErrorInvalidHandle;
    }
    hHandle.hSrv = ::OpenServiceA(
        hHandle.hScm,
        m_InstallParam.lpServiceName,
        SERVICE_ALL_ACCESS);
    if (hHandle.hSrv == nullptr) {
        return ::GetLastError();
    }
    if (::ControlService(
            hHandle.hSrv,
            SERVICE_CONTROL_PAUSE,
            &ss) == 0) {
        return ::GetLastError();
    }

    while (--dwWait != 0) {
        GetServiceRunStatus(ss);
        if (ss.dwCurrentState == kServicePaused) {
            return kErrorSuccess;
        }
        ::Sleep(1000);
    }
    return kErrorTimeout;
#else
    return kErrorCallNotImplemented;
#endif
}

// L2Authd.exe 0x00498C10, Src/servicemgr.cpp:315-344.
std::uint32_t CServiceManager::Continue(
    [[maybe_unused]] std::uint32_t dwWait) {
#if defined(_WIN32)
    SERVICE_STATUS ss{};
    if (GetServiceRunStatus(ss) != kErrorSuccess) {
        return ::GetLastError();
    }
    if (ss.dwCurrentState != kServicePaused) {
        return kErrorSuccess;
    }

    CSrvHandle hHandle;
    hHandle.hScm = ::OpenSCManagerA(
        nullptr,
        nullptr,
        SC_MANAGER_ALL_ACCESS);
    if (hHandle.hScm == nullptr) {
        return kErrorInvalidHandle;
    }
    hHandle.hSrv = ::OpenServiceA(
        hHandle.hScm,
        m_InstallParam.lpServiceName,
        SERVICE_ALL_ACCESS);
    if (hHandle.hSrv == nullptr) {
        return ::GetLastError();
    }
    if (::ControlService(
            hHandle.hSrv,
            SERVICE_CONTROL_CONTINUE,
            &ss) == 0) {
        return ::GetLastError();
    }

    while (--dwWait != 0) {
        GetServiceRunStatus(ss);
        if (ss.dwCurrentState == kServiceRunning) {
            return kErrorSuccess;
        }
        ::Sleep(1000);
    }
    return kErrorTimeout;
#else
    return kErrorCallNotImplemented;
#endif
}

// L2Authd.exe 0x00498DB0, Src/servicemgr.cpp:167-190.
std::uint32_t CServiceManager::GetServiceRunStatus(
    [[maybe_unused]] SERVICE_STATUS& ss) {
#if defined(_WIN32)
    CSrvHandle hHandle;
    hHandle.hScm = ::OpenSCManagerA(
        nullptr,
        nullptr,
        SC_MANAGER_ALL_ACCESS);
    if (hHandle.hScm == nullptr) {
        return kErrorInvalidHandle;
    }
    hHandle.hSrv = ::OpenServiceA(
        hHandle.hScm,
        m_InstallParam.lpServiceName,
        SERVICE_INTERROGATE);
    if (hHandle.hSrv == nullptr) {
        return ::GetLastError();
    }
    if (::ControlService(
            hHandle.hSrv,
            SERVICE_CONTROL_INTERROGATE,
            &ss) != 0) {
        return kErrorSuccess;
    }

    const std::uint32_t error = ::GetLastError();
    if (error == kErrorServiceNotActive) {
        ss.dwCurrentState = kServiceStopped;
        return kErrorSuccess;
    }
    return error;
#else
    return kErrorCallNotImplemented;
#endif
}

// L2Authd.exe 0x00498EF0, Src/servicemgr.cpp:193-202.
std::uint32_t CServiceManager::GetServiceRunStatus() {
    SERVICE_STATUS ss{};
    if (IsServiceMode() == 0) {
        ss.dwCurrentState = kServiceRunning;
    } else if (GetServiceRunStatus(ss) != kErrorSuccess) {
        ss.dwCurrentState = 0;
    }
    return ss.dwCurrentState;
}

// L2Authd.exe 0x00498F60, Src/servicemgr.cpp:206-215.
std::uint32_t CServiceManager::SetServiceRunStatus(
    [[maybe_unused]] std::uint32_t dwStatus) {
#if defined(_WIN32)
    SERVICE_STATUS ss{};
    ss.dwServiceType = m_InstallParam.dwServiceType;
    ss.dwCurrentState = dwStatus;
    ss.dwControlsAccepted = 0xFF;
    if (::SetServiceStatus(
            m_InstallParam.sshHandler,
            &ss) == 0) {
        return ::GetLastError();
    }
    return kErrorSuccess;
#else
    return kErrorCallNotImplemented;
#endif
}

// L2Authd.exe 0x00498FE0, Src/servicemgr.cpp:83.
void CServiceManager::SetServiceHandler(
    SERVICE_STATUS_HANDLE sshHandler) {
    m_InstallParam.sshHandler = sshHandler;
}

// L2Authd.exe 0x00499000, Src/servicemgr.cpp:85.
SERVICE_STATUS_HANDLE CServiceManager::GetServiceHandler() {
    return m_InstallParam.sshHandler;
}

// L2Authd.exe 0x00499010, Src/servicemgr.cpp:347-363.
int CServiceManager::IsServiceMode() {
#if defined(_WIN32)
    HWINSTA const windowStation = ::GetProcessWindowStation();
    if (windowStation == nullptr) {
        return 1;
    }

    USEROBJECTFLAGS flags{};
    DWORD length = sizeof(flags);
    if (::GetUserObjectInformationA(
            windowStation,
            UOI_FLAGS,
            &flags,
            sizeof(flags),
            &length) == 0) {
        return 0;
    }
    return flags.dwFlags == WSF_VISIBLE ? 0 : 1;
#else
    return 0;
#endif
}

// L2Authd.exe 0x00499080, Src/servicemgr.cpp:67.
void CServiceManager::ConfigServiceName(char* lpServiceName) {
    std::strcpy(m_InstallParam.lpServiceName, lpServiceName);
}

// L2Authd.exe 0x004990A0, Src/servicemgr.cpp:69.
void CServiceManager::ConfigServiceDisp(char* lpDisplayName) {
    std::strcpy(m_InstallParam.lpDisplayName, lpDisplayName);
}

// L2Authd.exe 0x004990C0, Src/servicemgr.cpp:71.
void CServiceManager::ConfigServiceDesc(char* lpDescription) {
    std::strcpy(m_InstallParam.lpDescription, lpDescription);
}

// L2Authd.exe 0x004990E0, Src/servicemgr.cpp:73.
void CServiceManager::ConfigServiceExec(char* lpBinaryPathName) {
    std::strcpy(m_InstallParam.lpBinaryPathName, lpBinaryPathName);
}

// L2Authd.exe 0x00499100, Src/servicemgr.cpp:75.
void CServiceManager::ConfigDependencies(char* lpDependencies) {
    std::strcpy(m_InstallParam.lpDependencies, lpDependencies);
}

// L2Authd.exe 0x00499120, Src/servicemgr.cpp:77.
void CServiceManager::ConfigSfaRebootMessage(char* lpRebootMessage) {
    std::strcpy(m_InstallParam.sfaReBootMessage, lpRebootMessage);
}

// L2Authd.exe 0x00499140, Src/servicemgr.cpp:79.
void CServiceManager::ConfigSfaRetryCommand(char* lpRetryCommand) {
    std::strcpy(m_InstallParam.sfaRetryCommand, lpRetryCommand);
}

// L2Authd.exe 0x00499160, Src/servicemgr.cpp:81.
void CServiceManager::ConfigSfaCounterReset(
    unsigned int dwFailureCounter) {
    m_InstallParam.sfaFailureCounter = dwFailureCounter;
}

// L2Authd.exe 0x00499180, Src/servicemgr.cpp:23-59.
void CServiceManager::ConfigInitialize() {
    static int bFirstInit = 0;
    if (bFirstInit == 1) {
        return;
    }
    bFirstInit = 1;

    std::memset(&m_InstallParam, 0, sizeof(m_InstallParam));
#if defined(_WIN32)
    ::GetModuleFileNameA(
        nullptr,
        m_InstallParam.lpModulePath,
        sizeof(m_InstallParam.lpModulePath));
    constexpr char pathSeparator = '\\';
#else
    GetExecutablePath(m_InstallParam.lpModulePath);
    constexpr char pathSeparator = '/';
#endif

    char tTemp[260]{};
    const std::size_t pathLength =
        std::strlen(m_InstallParam.lpModulePath);
    const char* const separator = std::strrchr(
        m_InstallParam.lpModulePath,
        pathSeparator);
    const char* const executableName = separator == nullptr
        ? m_InstallParam.lpModulePath
        : separator + 1;
    std::strcpy(tTemp, executableName);
    std::strcpy(
        m_InstallParam.lpBinaryPathName,
        m_InstallParam.lpModulePath);
    if (separator != nullptr) {
        const std::size_t separatorOffset =
            static_cast<std::size_t>(
                separator - m_InstallParam.lpModulePath);
        m_InstallParam.lpModulePath[separatorOffset] = '\0';
    } else if (pathLength == 0) {
        tTemp[0] = '\0';
    }

    if (char* const extension = std::strrchr(tTemp, '.');
        extension != nullptr) {
        *extension = '\0';
    }
    std::strcpy(m_InstallParam.lpServiceName, tTemp);
    std::strcpy(m_InstallParam.lpDisplayName, tTemp);
    m_InstallParam.dwDesiredAccess = 0xF01FF;
    m_InstallParam.dwServiceType = 0x10;
    m_InstallParam.dwStartType = 2;
    m_InstallParam.dwErrorControl = 1;
}

// L2Authd.exe 0x00499350, Src/servicemgr.h:96-99.
CServiceManager::CSrvHandle::CSrvHandle()
    : hSrv(nullptr),
      hScm(nullptr) {}

// L2Authd.exe 0x00499380, Src/servicemgr.h:101-104.
CServiceManager::CSrvHandle::~CSrvHandle() {
#if defined(_WIN32)
    if (hSrv != nullptr) {
        ::CloseServiceHandle(hSrv);
    }
    if (hScm != nullptr) {
        ::CloseServiceHandle(hScm);
    }
#endif
}
