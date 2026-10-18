#include "Shared/src/MemorySub.h"
#include "Shared/src/MemoryBackend.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/Log.h"

#include <cstdlib>
#include <cstring>

// registrations increment this count in initialization order.
// Previously the completed shipping count was seeded here (295 or 34).
int g_nClassCount = 0;
thread_local int IssuedReportCallStack = 0;
bool g_bShowMallocLog = false;


volatile std::int32_t g_pClassRef[kMemoryClassCapacity]{};
const char* volatile g_pClassRefName[kMemoryClassCapacity]{};
volatile std::int32_t g_ClassSize[kMemoryClassCapacity]{};
std::int32_t g_pClassWorkRefCount[kMemoryClassCapacity][kMemoryWorkTypeCount]{};

std::int32_t runtime::RegisterMemoryClass(const char* name, std::int32_t size) noexcept {
    // No allocation or logging here: registration can precede every product
    // constructor. Serialize publication, including a rare first use at runtime.
    static std::atomic_flag lock = ATOMIC_FLAG_INIT;
    while (lock.test_and_set(std::memory_order_acquire)) {
    }
    const auto id = runtime::raw_atomic::LoadI32Relaxed(&g_nClassCount) + 1;
    // ReportInstance also reads the adjacent slot when printing a pair.
    if (id >= static_cast<std::int32_t>(kMemoryClassCapacity) - 1) {
        std::abort();
    }
    runtime::raw_atomic::StorePointerRelease(&g_pClassRefName[id], name);
    runtime::raw_atomic::StoreI32Relaxed(&g_ClassSize[id], size);
    runtime::raw_atomic::StoreI32Release(&g_nClassCount, id);
    lock.clear(std::memory_order_release);
    return id;
}

Memory g_Memory;
CMemoryManager* g_pMemoryImpl = nullptr;

#if L2_MEMORY_BACKEND_LEGACY
#include "L2Core/src/inc/MemoryMappedFile.h"
#include "L2Core/src/inc/XStd.h"
#include "Shared/src/IOCs.h"
#include "Shared/src/ThreadManager.h"

#include <limits>
#include <map>

#include "Shared/src/LegacyMemoryManager.h"
#else
#include "Shared/src/SystemMemoryManager.h"
#endif

namespace {

std::size_t ClassIndex(std::int32_t classId) noexcept {
    const auto value = classId;
    return value >= 0 ? static_cast<std::size_t>(value) : kMemoryClassCapacity;
}

} // namespace

void* Memory::Malloc(std::size_t size, bool padCheck, const wchar_t* memoryTag, int allocationType) {
#if L2_MEMORY_BACKEND_LEGACY
    // Current L2NPC Memory::Malloc (0x0046a4a4) and CacheD's creation
    // at 0x004f7310 perform this unsynchronised lazy publication
    // through the PDB-named global.
    if (!g_pMemoryImpl) {
        g_pMemoryImpl = new CMemoryManager;
    }
    return g_pMemoryImpl->Malloc(size, padCheck, memoryTag, allocationType);
#else
    return SystemMemoryManager().Malloc(size, padCheck, memoryTag, allocationType);
#endif
}

void Memory::Free(void* allocation, int allocationType) noexcept {
#if L2_MEMORY_BACKEND_LEGACY
    g_pMemoryImpl->Free(allocation, allocationType);
#else
    SystemMemoryManager().Free(allocation, allocationType);
#endif
}

void Memory::Report() {
#if L2_MEMORY_BACKEND_LEGACY
    g_pMemoryImpl->Report();
#else
    SystemMemoryManager().Report();
#endif
}

void Memory::SetLogging(const long logging) {
#if L2_MEMORY_BACKEND_LEGACY
    // Current L2Server 0x0083150c. The wrapper does nothing
    // until the allocator manager has been lazily published.
    if (g_pMemoryImpl != nullptr) {
        g_pMemoryImpl->SetLogging(logging);
    }
#else
    SystemMemoryManager().SetLogging(logging);
#endif
}

void Memory::GetMemoryMon(wchar_t* output) noexcept {
    // Matching 2015 source surface is an empty wrapper, and the current image
    // removes it completely.
}

std::size_t Memory::RequestedSize(const void* allocation) noexcept { return CMemoryManager::RequestedSize(allocation); }

std::int64_t Memory::GetTotalAllocSize() noexcept {
#if L2_MEMORY_BACKEND_LEGACY
    if (!g_pMemoryImpl) {
        return 0;
    }
    return g_pMemoryImpl->GetTotalAllocSize();
#else
    return SystemMemoryManager().GetTotalAllocSize();
#endif
}

void* AllocateMemoryClass(
    std::size_t requestedSize,
    bool objectAllocation,
    std::int32_t classId,
    const wchar_t* allocationName,
    const char* reportName,
    std::int32_t classSize
) {
    void* const allocation = g_Memory.Malloc(requestedSize, objectAllocation, allocationName, 0);
    if (!allocation) {
        throw std::bad_alloc();
    }
    CMemoryManager::SetAllocationClassId(allocation, classId);

    const std::size_t index = ClassIndex(classId);
    runtime::raw_atomic::AddI32(&g_pClassRef[index], 1);
    // diagnostics share these publications with registration.
    // g_pClassRefName[index] = reportName.
    runtime::raw_atomic::StorePointerRelease(&g_pClassRefName[index], reportName);
    // g_ClassSize[index] = classSize.
    runtime::raw_atomic::StoreI32Relaxed(&g_ClassSize[index], classSize);

    std::memset(allocation, 0, requestedSize);
    return allocation;
}

void DecrementMemoryClassReference(std::int32_t classId) noexcept {
    const std::size_t index = ClassIndex(classId);
    if (index < kMemoryClassCapacity) {
        runtime::raw_atomic::AddI32(&g_pClassRef[index], -1);
    }
}

void FreeFailedMemoryClass(void* allocation) noexcept {
    if (allocation) {
        DecrementMemoryClassReference(CMemoryManager::AllocationClassId(allocation));
        g_Memory.Free(allocation, 0);
    }
}

void ReportInstance() {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_NORMAL,
    //     L"<< Instance Reference Report >>")
    // Log.Add(
    //     LOG_NORMAL,
    //     "[memory] Reporting class reference counts and registered sizes.")
    L2_LOG_INFO(Log, "[memory] Reporting class reference counts and registered sizes.");
    std::int64_t totalSize = 0;

    std::int32_t index = 0;
    std::int32_t classCount = runtime::raw_atomic::LoadI32Acquire(&g_nClassCount);
    if (runtime::AddInt32Wrapping(classCount, 1) > 0) {
        do {
            const char* const firstName = runtime::raw_atomic::LoadPointerAcquire(&g_pClassRefName[index]);
            if (firstName || runtime::raw_atomic::LoadPointerAcquire(&g_pClassRefName[index + 1])) {
                // 0x0046a084-0x0046a0c8 reloads every argument except
                // the first name after the presence test. These slots are
                // ordinary volatile publications in the shipping image.
                // atomic individual loads allow concurrent
                // registration/allocation. This is still not a row snapshot.
                const std::int32_t secondCount = runtime::raw_atomic::LoadI32Relaxed(&g_pClassRef[index + 1]);
                const std::int32_t secondSize = runtime::raw_atomic::LoadI32Relaxed(&g_ClassSize[index + 1]);
                const char* const secondName = runtime::raw_atomic::LoadPointerAcquire(&g_pClassRefName[index + 1]);
                const std::int32_t firstCount = runtime::raw_atomic::LoadI32Relaxed(&g_pClassRef[index]);
                const std::int32_t firstSize = runtime::raw_atomic::LoadI32Relaxed(&g_ClassSize[index]);
                // Log.Add(
                //     LOG_NORMAL,
                //     L"%20S [%d] : %d   %20S [%d] : %d",
                //     firstName,
                //     firstSize,
                //     firstCount,
                //     secondName,
                //     secondSize,
                //     secondCount)
                // Noise: An unnamed partner slot with zero size and references adds no information.
                if (firstName || firstCount != 0 || firstSize != 0) {
                    // Log.Add(
                    //     LOG_NORMAL,
                    //     "[memory] Class %d \"%s\": %d references, %d bytes per instance.",
                    //     index, firstName ? firstName : "unnamed", firstCount, firstSize)
                    L2_LOG_INFO(
                        Log, "[memory] Class %d \"%s\": %d references, %d bytes per instance.", index,
                        firstName ? firstName : "unnamed", firstCount, firstSize
                    );
                }
                if (secondName || secondCount != 0 || secondSize != 0) {
                    // Log.Add(
                    //     LOG_NORMAL,
                    //     "[memory] Class %d \"%s\": %d references, %d bytes per instance.",
                    //     index + 1, secondName ? secondName : "unnamed", secondCount, secondSize)
                    L2_LOG_INFO(
                        Log, "[memory] Class %d \"%s\": %d references, %d bytes per instance.", index + 1,
                        secondName ? secondName : "unnamed", secondCount, secondSize
                    );
                }

                // The report call is followed by four fresh loads. Each
                // multiplication and their sum wraps in a 32-bit register.
                // only that signed row total is extended to the qword total.
                const std::int32_t secondProduct = runtime::MultiplyInt32Wrapping(
                    runtime::raw_atomic::LoadI32Relaxed(&g_ClassSize[index + 1]),
                    runtime::raw_atomic::LoadI32Relaxed(&g_pClassRef[index + 1])
                );
                const std::int32_t firstProduct = runtime::MultiplyInt32Wrapping(
                    runtime::raw_atomic::LoadI32Relaxed(&g_ClassSize[index]),
                    runtime::raw_atomic::LoadI32Relaxed(&g_pClassRef[index])
                );
                const std::int32_t rowSize = runtime::AddInt32Wrapping(secondProduct, firstProduct);
                totalSize = runtime::AddInt64Wrapping(totalSize, rowSize);

                // The target refreshes this bound only after a reported
                // pair. An empty pair keeps the preceding observation.
                classCount = runtime::raw_atomic::LoadI32Acquire(&g_nClassCount);
            }
            index = runtime::AddInt32Wrapping(index, 2);
        } while (index < runtime::AddInt32Wrapping(classCount, 1));
    }

    // Log.Add(
    //     LOG_NORMAL,
    //     L"Total InstanceSize : %I64d MB",
    //     static_cast<long long>(
    //         totalSize /
    //         (INT64_C(1) << 20)))
    // Log.Add(
    //     LOG_NORMAL,
    //     "[memory] Instance size estimate: %lld bytes (%lld MiB). "
    //     "class counters are sampled independently.",
    //     static_cast<long long>(totalSize),
    //     static_cast<long long>(totalSize / (INT64_C(1) << 20)))
    L2_LOG_INFO(
        Log,
        "[memory] Instance size estimate: %lld bytes (%lld MiB); "
        "class counters are sampled independently.",
        static_cast<long long>(totalSize), static_cast<long long>(totalSize / (INT64_C(1) << 20))
    );

#if L2_MEMORY_BACKEND_LEGACY
    if (g_pMemoryImpl) {
        L2_GUARD(g_pMemoryImpl->MemoryStatLock);
        for (auto position = g_pMemoryImpl->MemoryStatMap.begin(); position != g_pMemoryImpl->MemoryStatMap.end();
            ++position) {
            // Log.Add(
            //     LOG_NORMAL,
            //     L"[%s, %d] : %ld",
            //     position->first.m_PrimaryKey,
            //     position->first.m_SecondaryKey,
            //     static_cast<long>(
            //         position->second))
            // Log.Add(
            //     LOG_NORMAL,
            //     L"[memory] Registered memory statistic \"%ls\", key %d: %d.",
            //     position->first.m_PrimaryKey ? position->first.m_PrimaryKey : L"unnamed",
            //     position->first.m_SecondaryKey,
            //     position->second)
            L2_LOG_INFO(
                Log, u"[memory] Registered memory statistic \"%ls\", key %d: %d.",
                position->first.m_PrimaryKey ? position->first.m_PrimaryKey : L"unnamed", position->first.m_SecondaryKey,
                position->second
            );
        }
    }

#endif

    // Noise: Every report row is self-describing. The generic end banner adds no state or result.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"<< End of Report >>")
}

void ReportMemory() { g_Memory.Report(); }

MemoryClassInfo GetMemoryClassInfo(std::int32_t classId) noexcept {
    MemoryClassInfo result;
    result.classId = classId;
    if (classId <= 0 || classId > runtime::raw_atomic::LoadI32Acquire(&g_nClassCount)) {
        return result;
    }
    const std::size_t index = static_cast<std::size_t>(classId);
    result.className = runtime::raw_atomic::LoadPointerAcquire(&g_pClassRefName[index]);
    result.classSize = runtime::raw_atomic::LoadI32Relaxed(&g_ClassSize[index]);
    result.liveInstances = runtime::raw_atomic::LoadI32Relaxed(&g_pClassRef[index]);
    // Runtime numbers no longer identify shipping table rows.
    result.exactCurrentIdentity = false;
    return result;
}

std::uint32_t AllocationLogicalSize(const void* allocation) noexcept {
    if (!allocation) {
        return 0;
    }
#if L2_MEMORY_BACKEND_LEGACY
    const auto* const bytes = static_cast<const std::byte*>(allocation);
    std::uint32_t logicalSize = 0;
    std::memcpy(&logicalSize, bytes - sizeof(logicalSize), sizeof(logicalSize));
    return logicalSize;
#else
    // Historical diagnostic value is requested size + 4, modulo 2^32.
    // The system backend has a different header. Never read allocation - 4.
    return static_cast<std::uint32_t>(CMemoryManager::RequestedSize(allocation) + 4);
#endif
}
