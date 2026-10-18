#include "Lock.h"

#include "Log.h"

#include "Shared/src/Runtime/RawAtomic.h"

#include <chrono>
#include <thread>

namespace {

bool AcquireDataLock(
    std::uint32_t& dataLock,
    bool tryOnly) {
    std::uint32_t yieldCount = 0;
    do {
        if (runtime::raw_atomic::CompareExchangeU32(
                &dataLock,
                0,
                1) == 1) {
            return true;
        }
        if (tryOnly) {
            return false;
        }

        if (++yieldCount < 4000) {
            std::this_thread::yield();
        } else {
            yieldCount = 0;
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1));
        }
    } while (true);
}

std::uint32_t ReleaseDataLock(
    std::uint32_t& dataLock) noexcept {
    return runtime::raw_atomic::ExchangeU32(
        &dataLock,
        1);
}

} // namespace

// L2Authd.exe 0x00464CC0, Src/lock.cpp:23-33.
CLock::CLock(
    LockType type,
    std::uint32_t spinCount) {
    m_type = type;
    if (type == eCustomSpinLock) {
        m_customLock = 0;
    } else if (type == eSystemSpinLock) {
        authd::platform::InitializeCriticalSectionAndSpinCount(
            m_critical,
            spinCount);
    } else {
        authd::platform::InitializeCriticalSection(
            m_critical);
    }
}

// L2Authd.exe 0x00464D10, Src/lock.cpp:36-40.
CLock::~CLock() {
    if (m_type != eCustomSpinLock) {
        authd::platform::DeleteCriticalSection(
            m_critical);
    }
}

// L2Authd.exe 0x00464D30, Src/lock.cpp:52-60.
void CLock::Enter() {
    if (m_type == eCustomSpinLock) {
        if (runtime::raw_atomic::ExchangeI32(
                &m_customLock,
                1) != 0) {
            Wait();
        }
    } else {
        authd::platform::EnterCriticalSection(
            m_critical);
    }
}

// L2Authd.exe 0x00464D70, Src/lock.cpp:63-69.
void CLock::Leave() {
    if (m_type == eCustomSpinLock) {
        runtime::raw_atomic::ExchangeI32(
            &m_customLock,
            0);
    } else {
        authd::platform::LeaveCriticalSection(
            m_critical);
    }
}

// L2Authd.exe 0x00464DA0, Src/lock.cpp:43-49. The false result for both
// critical-section modes is target behavior, not a missing portable branch.
bool CLock::TryEnter() {
    if (m_type == eCustomSpinLock) {
        return runtime::raw_atomic::ExchangeI32(
                   &m_customLock,
                   1) == 0;
    }
    return false;
}

// L2Authd.exe 0x00464C20, Src/lock.cpp:72-98.
void CLock::Wait() {
    if (m_type != eCustomSpinLock) {
        return;
    }

    for (int attempt = 0; attempt < 4000; ++attempt) {
        if (runtime::raw_atomic::ExchangeI32(
                &m_customLock,
                1) == 0) {
            return;
        }
    }

    for (int attempt = 0; attempt < 4000; ++attempt) {
        std::this_thread::yield();
        if (runtime::raw_atomic::ExchangeI32(
                &m_customLock,
                1) == 0) {
            return;
        }
    }

    do {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(1000));
    } while (runtime::raw_atomic::ExchangeI32(
                 &m_customLock,
                 1) != 0);
}

// L2Authd.exe 0x00464DF0, Src/lock.cpp:102-107.
CRWLock::CRWLock()
    : dataLock(1),
      readerCount(0) {
    authd::platform::InitializeCriticalSection(
        mutex);
}

// L2Authd.exe 0x00464E30, Src/lock.cpp:110-113.
CRWLock::~CRWLock() {
    authd::platform::DeleteCriticalSection(
        mutex);
}

// L2Authd.exe 0x00464E60, Src/lock.cpp:116-124.
void CRWLock::ReadLock() {
    authd::platform::EnterCriticalSection(mutex);
    ++readerCount;
    if (readerCount == 1 &&
        !AcquireDataLock(dataLock, false)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "ReadLock failed on dataLock")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=synchronization event=lock_acquire_failed mode=read lock=%p reader_count=%d action=return_without_data_lock",
        //     static_cast<void*>(this), readerCount)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[locks] Could not acquire the read lock at %p (%d readers). Returning without the data lock.",
        //     static_cast<void*>(this), readerCount)
        L2_LOG_ERROR(Log,
            "[locks] Could not acquire the read lock at %p (%d readers). Returning without the data lock.",
            static_cast<void*>(this), readerCount);
    }
    authd::platform::LeaveCriticalSection(mutex);
}

// L2Authd.exe 0x00464ED0, Src/lock.cpp:127-134.
void CRWLock::ReadUnlock() {
    authd::platform::EnterCriticalSection(mutex);
    --readerCount;
    if (readerCount == 0) {
        ReleaseDataLock(dataLock);
    }
    authd::platform::LeaveCriticalSection(mutex);
}

// L2Authd.exe 0x00464F20, Src/lock.cpp:137-139.
void CRWLock::WriteLock() {
    AcquireDataLock(dataLock, false);
}

// L2Authd.exe 0x00464F40, Src/lock.cpp:142-144.
bool CRWLock::WriteTryLock() {
    return AcquireDataLock(dataLock, true);
}

// L2Authd.exe 0x00464F80, Src/lock.cpp:147-153.
void CRWLock::WriteUnlock() {
    const std::uint32_t previousCount =
        ReleaseDataLock(dataLock);
    if (previousCount != 0) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "WriteUnlock semaphore was not locked")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=synchronization event=unlock_unheld_lock mode=write lock=%p previous_count=%u action=leave_lock_available",
        //     static_cast<void*>(this), previousCount)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[locks] Write unlock at %p found an unheld lock (previous count %u). The lock remains available.",
        //     static_cast<void*>(this), previousCount)
        L2_LOG_ERROR(Log,
            "[locks] Write unlock at %p found an unheld lock (previous count %u). The lock remains available.",
            static_cast<void*>(this), previousCount);
    }
}
