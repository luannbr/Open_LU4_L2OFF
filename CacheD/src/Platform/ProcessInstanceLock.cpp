#include "ProcessInstanceLock.h"

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

CacheDProcessInstanceLock::CacheDProcessInstanceLock() = default;

CacheDProcessInstanceLock::~CacheDProcessInstanceLock() {
#if defined(_WIN32)
    if (m_handle != nullptr) {
        CloseHandle(static_cast<HANDLE>(m_handle));
        m_handle = nullptr;
    }
#else
    if (m_descriptor >= 0) {
        flock(m_descriptor, LOCK_UN);
        close(m_descriptor);
        m_descriptor = -1;
    }
#endif
}

bool CacheDProcessInstanceLock::TryAcquire() {
#if defined(_WIN32)
    if (m_handle != nullptr) return true;
    const HANDLE handle = CreateMutexW(
        nullptr, TRUE, L"Global\\_L2CACHE_EXE_");
    if (handle == nullptr) return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(handle);
        return false;
    }
    m_handle = handle;
    return true;
#else
    if (m_descriptor >= 0) return true;
    // The lock file remains in the deployment working directory. Keeping it
    // after exit avoids unlink/recreate races. Flock ownership itself is tied
    // to the live descriptor.
    const int descriptor = open(
        ".L2CACHE_EXE.lock", O_RDWR | O_CREAT, 0600);
    if (descriptor < 0) return false;
    if (flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
        close(descriptor);
        return false;
    }
    m_descriptor = descriptor;
    return true;
#endif
}
