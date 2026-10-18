#include "RWLock.h"

#include "Shared/src/Runtime/ProcessRuntime.h"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <new>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

constexpr std::uint8_t kWriteLock = 2;
constexpr std::uint8_t kReadLock = 3;
constexpr std::uint32_t kWaitObject0 = 0;
constexpr std::uint32_t kWaitAbandoned = 0x80;
constexpr std::uint32_t kWaitFailed = 0xffffffffU;

struct RWLockDataLock {
    std::mutex mutex;
    std::condition_variable condition;
    bool locked = false;
};

void* CreateDataLock() {
#if defined(_WIN32)
    return reinterpret_cast<void*>(::CreateSemaphoreW(nullptr, 1, 1, nullptr));
#else
    return new (std::nothrow) RWLockDataLock;
#endif
}

void DestroyDataLock(void* dataLock) {
#if defined(_WIN32)
    ::CloseHandle(reinterpret_cast<HANDLE>(dataLock));
#else
    delete static_cast<RWLockDataLock*>(dataLock);
#endif
}

void InitializeMeteredSection(l2portable_detail::CriticalSectionStorage& storage) {
#if defined(_WIN32)
    if (!::InitializeCriticalSectionAndSpinCount(reinterpret_cast<LPCRITICAL_SECTION>(&storage), 0)) {
        throw std::bad_alloc();
    }
#else
    storage.DebugInfo = new std::recursive_mutex;
    storage.LockCount = -1;
    storage.RecursionCount = 0;
    storage.OwningThread = nullptr;
    storage.LockSemaphore = nullptr;
    storage.SpinCount = 0;
#endif
}

void DeleteMeteredSection(l2portable_detail::CriticalSectionStorage& storage) {
#if defined(_WIN32)
    ::DeleteCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(&storage));
#else
    delete static_cast<std::recursive_mutex*>(storage.DebugInfo);
#endif
}

void EnterMeteredSection(l2portable_detail::CriticalSectionStorage& storage) {
#if defined(_WIN32)
    ::EnterCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(&storage));
#else
    static_cast<std::recursive_mutex*>(storage.DebugInfo)->lock();
#endif
}

void LeaveMeteredSection(l2portable_detail::CriticalSectionStorage& storage) {
#if defined(_WIN32)
    ::LeaveCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(&storage));
#else
    static_cast<std::recursive_mutex*>(storage.DebugInfo)->unlock();
#endif
}

std::uint32_t WaitDataLock(void* dataLock) {
#if defined(_WIN32)
    return static_cast<std::uint32_t>(::WaitForSingleObject(reinterpret_cast<HANDLE>(dataLock), INFINITE));
#else
    auto* const state = static_cast<RWLockDataLock*>(dataLock);
    if (!state) {
        return kWaitFailed;
    }
    std::unique_lock<std::mutex> guard(state->mutex);
    state->condition.wait(guard, [state] { return !state->locked; });
    state->locked = true;
    return kWaitObject0;
#endif
}

bool ReleaseDataLock(void* dataLock, std::int32_t* previousCount) {
#if defined(_WIN32)
    return ::ReleaseSemaphore(reinterpret_cast<HANDLE>(dataLock), 1, reinterpret_cast<LPLONG>(previousCount)) != FALSE;
#else
    auto* const state = static_cast<RWLockDataLock*>(dataLock);
    if (!state) {
        return false;
    }
    {
        std::lock_guard<std::mutex> guard(state->mutex);
        if (!state->locked) {
            return false;
        }
        state->locked = false;
        *previousCount = 0;
    }
    state->condition.notify_one();
    return true;
#endif
}

int DataLockLastError() {
#if defined(_WIN32)
    return static_cast<int>(::GetLastError());
#else
    return 6;
#endif
}

std::uint64_t LockAddress(const RWLock* lock) { return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(lock)); }

} // namespace

// Current CacheD RWLock::RWLock: 0x00528d3c. The POSIX branch replaces only
// the Win32 semaphore/critical-section carriers. The target 0x38-byte layout
// and initial reader/semaphore state stay identical.
RWLock::RWLock() {
    L2_GUARD_FUNCTION();
    readerCount = 0;
    dataLock = CreateDataLock();
    if (!dataLock) {
        throw std::bad_alloc();
    }
    try {
        InitializeMeteredSection(mutex);
    } catch (...) {
        DestroyDataLock(dataLock);
        throw;
    }
}

// Current CacheD RWLock::~RWLock: 0x00528dec.
RWLock::~RWLock() {
    L2_GUARD_FUNCTION();
    DestroyDataLock(dataLock);
    DeleteMeteredSection(mutex);
}

// Current CacheD RWLock::ReadLock: 0x00528e88. This order is observable in
// the target lock-status stack: outer wait, outer entered, metered wait,
// reader transition/wait, metered leave, metered entered.
void RWLock::ReadLock(const wchar_t* callerFile, int callerLine) {
    const std::uint64_t address = LockAddress(this);
    runtime::process::RecordLockWaiting(callerFile, callerLine, address, kReadLock);

    EnterMeteredSection(mutex);
    runtime::process::RecordLockEntered(callerFile, callerLine, address, kReadLock);
    runtime::process::RecordLockWaiting(L"RWLock::ReadLock() - MeteredSection", 0x2a, address, kReadLock);

    ++readerCount;
    if (readerCount == 1) {
        const std::uint32_t waitResult = WaitDataLock(dataLock);
        if (waitResult != kWaitObject0) {
            runtime::process::ReportRWLockWaitFailure(waitResult, DataLockLastError(), callerFile, callerLine);
        }
    }
    LeaveMeteredSection(mutex);
    runtime::process::RecordLockEntered(L"RWLock::ReadLock() - MeteredSection", 0x2a, address, kReadLock);
}

// Current CacheD RWLock::ReadUnlock: 0x0052907c.
void RWLock::ReadUnlock() {
    const std::uint64_t address = LockAddress(this);
    runtime::process::RecordLockReleased(L"RWLock::ReadUnlock() - MeteredSection", 0x45, address, kReadLock);
    runtime::process::RecordLockReleased(L"RWLock::ReadUnlock()", 0x46, address, kReadLock);
    runtime::process::RecordLockWaiting(L"RW ReadUnlock Entering Mutex", 0x4a, address, kReadLock);

    EnterMeteredSection(mutex);
    runtime::process::RecordLockEntered(L"RW ReadUnlock Entering Mutex", 0x4a, address, kReadLock);
    --readerCount;
    if (readerCount == 0) {
        std::int32_t previousCount;
        ReleaseDataLock(dataLock, &previousCount);
    }
    LeaveMeteredSection(mutex);
    runtime::process::RecordLockReleased(L"RW ReadUnlock Leaving Mutex", 0x53, address, kReadLock);
}

// Current CacheD RWLock::WriteLock: 0x00529208.
void RWLock::WriteLock(const wchar_t* callerFile, int callerLine) {
    const std::uint64_t address = LockAddress(this);
    runtime::process::RecordLockWaiting(callerFile, callerLine, address, kWriteLock);
    WaitDataLock(dataLock);
    runtime::process::RecordLockEntered(callerFile, callerLine, address, kWriteLock);
}

// Current CacheD RWLock::WriteUnlock: 0x005292d0.
void RWLock::WriteUnlock() {
    const std::uint64_t address = LockAddress(this);
    runtime::process::RecordLockReleased(L"RWLock::WriteUnlock()", 0x78, address, kWriteLock);
    // Current L2Server 0x00a3b28c, L2NPC 0x0050cc54 and Cached
    // 0x005292d0 ignore ReleaseSemaphore's return value. On failure the
    // target therefore observes this uninitialized lane.
    std::int32_t previousCount;
    ReleaseDataLock(dataLock, &previousCount);
    if (previousCount != 0) {
        runtime::process::ReportRWLockBadRelease(previousCount);
    }
}
