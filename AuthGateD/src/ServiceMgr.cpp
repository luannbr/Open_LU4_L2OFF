#include "ServiceMgr.h"

#include <cstring>

#if defined(_WIN32)
#  include <Windows.h>
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>
#else
#  include <unistd.h>
#endif

SRVARG CServiceManager::m_InstallParam{};

namespace {

constexpr std::uint32_t kErrorCallNotImplemented = 120;
#if defined(_WIN32)
constexpr std::uint32_t kErrorSuccess = 0;
constexpr std::uint32_t kErrorInvalidHandle = 6;
constexpr std::uint32_t kErrorTimeout = 258;
constexpr std::uint32_t kErrorServiceDoesNotExist = 1060;
constexpr std::uint32_t kErrorServiceNotActive = 1062;
constexpr std::uint32_t kServiceStopped = 1;
#endif

void CopyTargetString(char* destination, const char* source) {
    if (!source) {
        destination[0] = '\0';
        return;
    }
    std::strcpy(destination, source);
}

void GetExecutablePath(char (&path)[260]) noexcept {
#if defined(_WIN32)
    ::GetModuleFileNameA(nullptr, path, sizeof(path));
#elif defined(__APPLE__)
    std::uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) path[0] = '\0';
#else
    const auto length = ::readlink(
        "/proc/self/exe",
        path,
        sizeof(path) - 1U);
    if (length < 0) {
        path[0] = '\0';
    } else {
        path[length] = '\0';
    }
#endif
}

} // namespace

// AuthGateD.exe 0x00418320, servicemgr.cpp.
CServiceManager::CServiceManager() {
    ConfigInitialize();
}

// AuthGateD.exe 0x00417F10, servicemgr.cpp.
CServiceManager::~CServiceManager() = default;

// AuthGateD.exe 0x00417DE0, servicemgr.cpp.
void CServiceManager::ConfigInitialize() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    // GCC 13 rejects "= {}" for this aggregate. The named temporary gives
    // the same value initialization.
    m_InstallParam = SRVARG{};
    GetExecutablePath(m_InstallParam.lpModulePath);
    CopyTargetString(
        m_InstallParam.lpBinaryPathName,
        m_InstallParam.lpModulePath);

    char* leaf = m_InstallParam.lpModulePath;
    for (char* current = m_InstallParam.lpModulePath;
         *current != '\0';
         ++current) {
        if (*current == '\\' || *current == '/') leaf = current + 1;
    }

    char defaultName[260]{};
    CopyTargetString(defaultName, leaf);
    if (char* extension = std::strrchr(defaultName, '.')) {
        *extension = '\0';
    }
    if (leaf != m_InstallParam.lpModulePath) {
        leaf[-1] = '\0';
    } else {
        m_InstallParam.lpModulePath[0] = '\0';
    }

    CopyTargetString(m_InstallParam.lpServiceName, defaultName);
    CopyTargetString(m_InstallParam.lpDisplayName, defaultName);
    m_InstallParam.dwDesiredAccess = 0x000f01ffU;
    m_InstallParam.dwServiceType = 0x10U;
    m_InstallParam.dwStartType = 2U;
    m_InstallParam.dwErrorControl = 1U;
}

// AuthGateD.exe 0x00417F20, servicemgr.cpp.
void CServiceManager::ConfigServiceName(const char* serviceName) {
    CopyTargetString(m_InstallParam.lpServiceName, serviceName);
}

// AuthGateD.exe 0x00417F40, servicemgr.cpp.
void CServiceManager::ConfigServiceDisp(const char* displayName) {
    CopyTargetString(m_InstallParam.lpDisplayName, displayName);
}

// AuthGateD.exe 0x00417F60, servicemgr.cpp.
void CServiceManager::ConfigServiceDesc(const char* description) {
    CopyTargetString(m_InstallParam.lpDescription, description);
}

// AuthGateD.exe 0x00417F80, servicemgr.cpp.
void CServiceManager::SetServiceHandler(
    SERVICE_STATUS_HANDLE handler) {
    m_InstallParam.sshHandler = handler;
}

// AuthGateD.exe 0x00417F90, servicemgr.cpp.
SERVICE_STATUS_HANDLE CServiceManager::GetServiceHandler() {
    return m_InstallParam.sshHandler;
}

// AuthGateD.exe 0x00417FA0, servicemgr.cpp.
std::uint32_t CServiceManager::Install() {
#if defined(_WIN32)
    SC_HANDLE manager = ::OpenSCManagerA(
        nullptr,
        nullptr,
        SC_MANAGER_ALL_ACCESS);
    if (!manager) return kErrorInvalidHandle;

    SC_HANDLE service = ::CreateServiceA(
        manager,
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
        m_InstallParam.lpServiceStartName[0]
            ? m_InstallParam.lpServiceStartName
            : nullptr,
        m_InstallParam.lpPassword[0]
            ? m_InstallParam.lpPassword
            : nullptr);
    if (!service) {
        const std::uint32_t error = ::GetLastError();
        ::CloseServiceHandle(manager);
        return error;
    }

    SERVICE_DESCRIPTIONA description{
        m_InstallParam.lpDescription};
    ::ChangeServiceConfig2A(
        service,
        SERVICE_CONFIG_DESCRIPTION,
        &description);
    m_InstallParam.sfa.cActions = 3;
    ::ChangeServiceConfig2A(
        service,
        SERVICE_CONFIG_FAILURE_ACTIONS,
        &m_InstallParam.sfa);
    ::CloseServiceHandle(service);
    ::CloseServiceHandle(manager);
    return kErrorSuccess;
#else
    return kErrorCallNotImplemented;
#endif
}

// AuthGateD.exe 0x00418080, servicemgr.cpp.
std::uint32_t CServiceManager::GetServiceRunStatus(
    [[maybe_unused]] SERVICE_STATUS* status) {
#if defined(_WIN32)
    CSrvHandle handles;
    handles.hScm = ::OpenSCManagerA(
        nullptr,
        nullptr,
        SC_MANAGER_ALL_ACCESS);
    if (!handles.hScm) return kErrorInvalidHandle;
    handles.hSrv = ::OpenServiceA(
        static_cast<SC_HANDLE>(handles.hScm),
        m_InstallParam.lpServiceName,
        SERVICE_INTERROGATE);
    if (!handles.hSrv) return ::GetLastError();
    if (::ControlService(
            static_cast<SC_HANDLE>(handles.hSrv),
            SERVICE_CONTROL_INTERROGATE,
            status)) {
        return kErrorSuccess;
    }
    const std::uint32_t error = ::GetLastError();
    if (error == kErrorServiceNotActive) {
        status->dwCurrentState = kServiceStopped;
        return kErrorSuccess;
    }
    return error;
#else
    return kErrorCallNotImplemented;
#endif
}

// AuthGateD.exe 0x00418150, servicemgr.cpp.
std::uint32_t CServiceManager::SetServiceRunStatus(
    [[maybe_unused]] std::uint32_t status) {
#if defined(_WIN32)
    SERVICE_STATUS serviceStatus{};
    serviceStatus.dwServiceType = m_InstallParam.dwServiceType;
    serviceStatus.dwCurrentState = status;
    serviceStatus.dwControlsAccepted = 0xffU;
    if (!::SetServiceStatus(
            m_InstallParam.sshHandler,
            &serviceStatus)) {
        return ::GetLastError();
    }
    return kErrorSuccess;
#else
    return kErrorCallNotImplemented;
#endif
}

// AuthGateD.exe 0x004181B0, servicemgr.cpp.
std::uint32_t CServiceManager::Stop(
    [[maybe_unused]] int force,
    [[maybe_unused]] std::uint32_t waitSeconds) {
#if defined(_WIN32)
    SERVICE_STATUS status{};
    const std::uint32_t query = GetServiceRunStatus(&status);
    if (query != kErrorSuccess) return ::GetLastError();
    if (status.dwCurrentState == kServiceStopped) return kErrorSuccess;

    CSrvHandle handles;
    handles.hScm = ::OpenSCManagerA(
        nullptr,
        nullptr,
        SC_MANAGER_ALL_ACCESS);
    if (!handles.hScm) return kErrorInvalidHandle;
    handles.hSrv = ::OpenServiceA(
        static_cast<SC_HANDLE>(handles.hScm),
        m_InstallParam.lpServiceName,
        SERVICE_ALL_ACCESS);
    if (!handles.hSrv ||
        !::ControlService(
            static_cast<SC_HANDLE>(handles.hSrv),
            SERVICE_CONTROL_STOP,
            &status)) {
        return ::GetLastError();
    }
    while (--waitSeconds != 0) {
        GetServiceRunStatus(&status);
        if (status.dwCurrentState == kServiceStopped) {
            return kErrorSuccess;
        }
        ::Sleep(1000);
    }
    return kErrorTimeout;
#else
    return kErrorCallNotImplemented;
#endif
}

// AuthGateD.exe 0x004182D0, servicemgr.cpp.
int CServiceManager::IsServiceMode() {
#if defined(_WIN32)
    HWINSTA station = ::GetProcessWindowStation();
    if (!station) return 1;
    USEROBJECTFLAGS flags{};
    DWORD required = sizeof(flags);
    if (::GetUserObjectInformationA(
            station,
            UOI_FLAGS,
            &flags,
            sizeof(flags),
            &required)) {
        return flags.dwFlags != WSF_VISIBLE;
    }
#endif
    return 0;
}

// AuthGateD.exe 0x00418360, servicemgr.cpp.
std::uint32_t CServiceManager::Uninstall(
    [[maybe_unused]] int force,
    [[maybe_unused]] std::uint32_t waitSeconds) {
#if defined(_WIN32)
    CSrvHandle handles;
    handles.hScm = ::OpenSCManagerA(
        nullptr,
        nullptr,
        SC_MANAGER_ALL_ACCESS);
    if (!handles.hScm) return kErrorInvalidHandle;
    handles.hSrv = ::OpenServiceA(
        static_cast<SC_HANDLE>(handles.hScm),
        m_InstallParam.lpServiceName,
        SERVICE_ALL_ACCESS);
    if (!handles.hSrv) {
        const std::uint32_t error = ::GetLastError();
        return error == kErrorServiceDoesNotExist
            ? kErrorSuccess
            : error;
    }

    SERVICE_STATUS status{};
    const std::uint32_t query = GetServiceRunStatus(&status);
    if (query == kErrorSuccess) {
        if (status.dwCurrentState != kServiceStopped &&
            Stop(force, waitSeconds) != kErrorSuccess) {
            return ::GetLastError();
        }
    } else if (query != kErrorServiceNotActive) {
        return ::GetLastError();
    }
    if (::DeleteService(static_cast<SC_HANDLE>(handles.hSrv))) {
        return kErrorSuccess;
    }
    return ::GetLastError();
#else
    return kErrorCallNotImplemented;
#endif
}

CServiceManager::CSrvHandle::~CSrvHandle() {
#if defined(_WIN32)
    if (hSrv) ::CloseServiceHandle(static_cast<SC_HANDLE>(hSrv));
    if (hScm) ::CloseServiceHandle(static_cast<SC_HANDLE>(hScm));
#endif
}
