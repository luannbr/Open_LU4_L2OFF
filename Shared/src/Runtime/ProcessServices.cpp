#include "Shared/src/Runtime/ProcessServices.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>

#if defined(_WIN32)
#include <Windows.h>
#elif defined(__APPLE__)
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <sys/file.h>
#include <unistd.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace runtime {
namespace {

#if !defined(_WIN32)
struct PosixInstanceHandle {
    int descriptor = -1;
};

std::filesystem::path ExecutablePath() {
#if defined(__APPLE__)
    std::uint32_t size = 0;
    ::_NSGetExecutablePath(nullptr, &size);
    if (size == 0) {
        return {};
    }
    std::string path(size, '\0');
    if (::_NSGetExecutablePath(path.data(), &size) != 0) {
        return {};
    }
    path.resize(std::strlen(path.c_str()));
    return path;
#else
    std::array<char, 4096> path{};
    const ssize_t size = ::readlink("/proc/self/exe", path.data(), path.size() - 1U);
    if (size <= 0) {
        return {};
    }
    path[static_cast<std::size_t>(size)] = '\0';
    return path.data();
#endif
}
#endif

#if defined(_WIN32)
ServiceMain g_serviceMain = nullptr;

void WINAPI ServiceMainThunk(DWORD argumentCount, LPSTR* arguments) {
    if (g_serviceMain) {
        g_serviceMain(static_cast<int>(argumentCount), arguments);
    }
}
#endif

} // namespace

InstanceGateResult AcquireNamedInstanceGate(
    bool allowMultipleInstances,
    const char16_t* windowsName,
    const char* portableLockName,
    void*& nativeHandle,
    std::string& diagnostic
) {
    nativeHandle = nullptr;
    diagnostic.clear();
    if (allowMultipleInstances) {
        return InstanceGateResult::Bypassed;
    }

#if defined(_WIN32)
    HANDLE const mutex = ::CreateMutexW(nullptr, TRUE, reinterpret_cast<const wchar_t*>(windowsName));
    nativeHandle = mutex;
    if (::GetLastError() == ERROR_ALREADY_EXISTS) {
        return InstanceGateResult::AlreadyRunning;
    }
    if (!mutex) {
        diagnostic = "CreateMutexW failed: " + std::to_string(::GetLastError());
        return InstanceGateResult::Error;
    }
    return InstanceGateResult::Acquired;
#else
    const std::filesystem::path path = std::filesystem::temp_directory_path() / portableLockName;
    const int descriptor = ::open(path.c_str(), O_CREAT | O_RDWR, 0600);
    if (descriptor < 0) {
        diagnostic = "open(" + path.string() + ") failed: " + std::strerror(errno);
        return InstanceGateResult::Error;
    }
    if (::flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
        const int error = errno;
        ::close(descriptor);
        if (error == EWOULDBLOCK || error == EAGAIN) {
            return InstanceGateResult::AlreadyRunning;
        }
        diagnostic = "flock(" + path.string() + ") failed: " + std::strerror(error);
        return InstanceGateResult::Error;
    }
    auto* const handle = new PosixInstanceHandle;
    handle->descriptor = descriptor;
    nativeHandle = handle;
    return InstanceGateResult::Acquired;
#endif
}

void CloseNamedInstanceGate(void*& nativeHandle) noexcept {
    if (!nativeHandle) {
        return;
    }
#if defined(_WIN32)
    ::CloseHandle(static_cast<HANDLE>(nativeHandle));
#else
    auto* const handle = static_cast<PosixInstanceHandle*>(nativeHandle);
    if (handle->descriptor >= 0) {
        ::flock(handle->descriptor, LOCK_UN);
        ::close(handle->descriptor);
    }
    delete handle;
#endif
    nativeHandle = nullptr;
}

bool SetWorkingDirectoryToExecutableDirectory(std::string& diagnostic) {
    diagnostic.clear();
    std::error_code error;
#if defined(_WIN32)
    std::array<wchar_t, 32768> path{};
    const DWORD length = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        diagnostic = "GetModuleFileNameW failed: " + std::to_string(::GetLastError());
        return false;
    }
    const std::filesystem::path directory = std::filesystem::path(path.data()).parent_path();
#else
    const std::filesystem::path directory = ExecutablePath().parent_path();
#endif
    if (directory.empty()) {
        diagnostic = "executable directory is empty";
        return false;
    }
    std::filesystem::current_path(directory, error);
    if (error) {
        diagnostic = error.message();
        return false;
    }
    return true;
}

void DisableCurrentProcessPriorityBoost() noexcept {
#if defined(_WIN32)
    ::SetProcessPriorityBoost(::GetCurrentProcess(), TRUE);
#endif
}

SERVICE_STATUS_HANDLE RegisterServiceControlHandler(const char* serviceName, ServiceControlHandler handler) noexcept {
#if defined(_WIN32)
    return ::RegisterServiceCtrlHandlerExA(serviceName, reinterpret_cast<LPHANDLER_FUNCTION_EX>(handler), nullptr);
#else
    [[maybe_unused]] const char* const unusedName = serviceName;
    [[maybe_unused]] const ServiceControlHandler unusedHandler = handler;
    return nullptr;
#endif
}

bool StartServiceDispatcher(const char* serviceName, ServiceMain serviceMain) noexcept {
#if defined(_WIN32)
    g_serviceMain = serviceMain;
    SERVICE_TABLE_ENTRYA table[] = {
        {const_cast<char*>(serviceName), ServiceMainThunk},
        {nullptr, nullptr},
    };
    return ::StartServiceCtrlDispatcherA(table) != FALSE;
#else
    [[maybe_unused]] const char* const unusedName = serviceName;
    [[maybe_unused]] const ServiceMain unusedMain = serviceMain;
    return false;
#endif
}

} // namespace runtime
