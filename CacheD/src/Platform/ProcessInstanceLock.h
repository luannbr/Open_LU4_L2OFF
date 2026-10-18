#pragma once

// Portable replacement for CreateMutexW(...,
// L"Global\\_L2CACHE_EXE_") at current Cached.exe 0x0045a8a2.
class CacheDProcessInstanceLock {
public:
    CacheDProcessInstanceLock();
    ~CacheDProcessInstanceLock();

    CacheDProcessInstanceLock(const CacheDProcessInstanceLock&) = delete;
    CacheDProcessInstanceLock& operator=(
        const CacheDProcessInstanceLock&) = delete;

    bool TryAcquire();

private:
#if defined(_WIN32)
    void* m_handle = nullptr;
#else
    int m_descriptor = -1;
#endif
};
