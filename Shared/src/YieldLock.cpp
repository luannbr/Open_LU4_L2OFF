#include "Shared/src/YieldLock.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/ThreadManager.h"

#include <thread>

namespace {

thread_local std::uint32_t g_tlYieldCount = 0;
constexpr std::uint8_t kDefaultLock = 0;

} // namespace

CYieldLock::CYieldLock() = default;

void CYieldLock::Enter(const wchar_t* callerFile, int callerLine) {
    const std::uint64_t address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this));
    runtime::process::RecordLockWaiting(callerFile, callerLine, address, kDefaultLock);

    const std::int32_t threadId = CThreadManager::GetThreadID();
    if (runtime::raw_atomic::LoadI32(&m_nThreadID) != threadId) {
        while (!runtime::raw_atomic::CompareExchangeI32Acquire(&m_nLock, 0, 1)) {
            ++g_tlYieldCount;
            if ((g_tlYieldCount & 0x0fU) != 0) {
                std::this_thread::yield();
            }
        }
        g_tlYieldCount = 0;
        runtime::raw_atomic::StoreI32(&m_nThreadID, threadId);
    }
    runtime::process::RecordLockEntered(callerFile, callerLine, address, kDefaultLock);
    // The shipping body performs an ordinary owner-thread increment after
    // acquiring (or recursively re-entering) the lock. It is not an
    // Interlocked operation.
    ++m_nUseCount;
}

void CYieldLock::Leave(const wchar_t* callerFile, int callerLine) {
    const wchar_t* description = callerFile ? callerFile : L"CYieldLock Leave";
    runtime::process::RecordLockReleased(
        description, static_cast<std::uint32_t>(callerLine),
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this)), kDefaultLock
    );
    // As in current 0x005471e8, recursive ownership is decremented plainly.
    // Only the final lock-word publication needs a host release operation to
    // carry the x64 target's store-ordering contract to weaker platforms.
    --m_nUseCount;
    if (m_nUseCount == 0) {
        runtime::raw_atomic::StoreI32(&m_nThreadID, -1);
        runtime::raw_atomic::StoreI32Release(&m_nLock, 0);
    }
}

bool CYieldLock::IsEntered() const { return m_nUseCount > 0; }
