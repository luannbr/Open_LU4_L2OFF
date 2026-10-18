#include "Shared/src/ThreadSafeLibrary.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Exception.h"
#include "Shared/src/MemorySub.h"

#include <cstdint>
#include <cwchar>

// The symbol remains the Shared/MemorySub.cpp accounting owner from the
// shipping source. It is isolated only at portable object-file granularity
// so linking a CMwsrQueue does not also import the process-specific memory
// report implementation of another executable.
volatile std::int64_t g_CMwsrQueueAlloc = 0;

namespace thread_safe_detail {

void ReportMwsrQueueLowMemory(const char* allocationName) { runtime::process::ReportMwsrQueueLowMemory(allocationName); }

void ReportNewMwsrQueueNull(const wchar_t* allocationName) { runtime::process::ReportNewMwsrQueueNull(allocationName); }

void RecordMwsrQueueAllocation(std::size_t byteCount) {
    runtime::raw_atomic::AddI64(&g_CMwsrQueueAlloc, static_cast<std::int64_t>(byteCount));
}

void CrashNewMwsrQueueInvariant(const int reason, const std::source_location location) {
    ForceCrash(static_cast<CrashInfo::ReasonType>(reason), location);
}

void CrashMwmrQueueOverflow(const std::source_location location) {
    GuardHelper failureGuard(L"MwmrQueue overflow...", CThreadManager::GetThreadID());
    ForceCrash(CrashInfo::LOW_MEMORY, location);
}

} // namespace thread_safe_detail
