#pragma once

// Original pooled allocator kept for L2_MEMORY_BACKEND=legacy.
// Included only by MemorySub.cpp after its product-specific dependencies.
namespace {

using memory_sub_detail::AllocationHeader;

constexpr std::size_t MemoryPoolBlockSize(std::size_t index) noexcept { return ((index & 3U) + 4U) << ((index >> 2U) + 1U); }

std::size_t PortablePoolIndex(std::size_t transportSize) noexcept {
    for (std::size_t index = 0; index < kMemoryPoolCount; ++index) {
        const std::size_t blockSize = MemoryPoolBlockSize(index);
        if (blockSize >= transportSize && blockSize % alignof(std::max_align_t) == 0) {
            return index;
        }
    }
    return kMemoryPoolCount;
}

} // namespace

class CMemoryPool {
public:
    explicit CMemoryPool(std::size_t blockSize) : m_nBlockSize(blockSize) {
        // Both current images publish these counters before allocating and
        // constructing their process-specific queue, then write the queue
        // pointer last. L2NPC 0x00469228 owns CMwmrGrowingQueue<65536>.
        // CacheD 0x004f65f0 owns CSmartQueue.
        m_nAlloc = 1;
        m_nReuse = 0;
        if (UsesSmartQueue()) {
            m_pFreeQueue = new CSmartQueue(static_cast<std::uint64_t>(blockSize));
        } else {
            m_pFreeQueue = new CMwmrGrowingQueue<65536>(static_cast<int>(blockSize));
        }
    }

    ~CMemoryPool() {
        if (UsesSmartQueue()) {
            delete static_cast<CSmartQueue*>(m_pFreeQueue);
        } else {
            delete static_cast<CMwmrGrowingQueue<65536>*>(m_pFreeQueue);
        }
    }

    std::size_t GetBlockSize() noexcept { return m_nBlockSize; }

    void* Malloc() {
        int allocCount = 0;
        void* const allocation = UsesSmartQueue() ? static_cast<CSmartQueue*>(m_pFreeQueue)->Pop(allocCount)
                                                  : static_cast<CMwmrGrowingQueue<65536>*>(m_pFreeQueue)->Pop(allocCount);
        runtime::raw_atomic::AddI32(&m_nAlloc, allocCount);
        runtime::raw_atomic::AddI64(&m_nReuse, 1);
        return allocation;
    }

    void Free(void* allocation) {
        if (UsesSmartQueue()) {
            static_cast<CSmartQueue*>(m_pFreeQueue)->Push(allocation);
        } else {
            static_cast<CMwmrGrowingQueue<65536>*>(m_pFreeQueue)->Push(allocation);
        }
    }

    int GetAllocCount() const noexcept { return runtime::raw_atomic::LoadI32Relaxed(&m_nAlloc); }

    std::int64_t GetReuseCount() const noexcept { return runtime::raw_atomic::LoadI64Relaxed(&m_nReuse); }

    int GetFreeQueueSize() noexcept {
        return UsesSmartQueue() ? static_cast<CSmartQueue*>(m_pFreeQueue)->GetFreeCount()
                                : static_cast<CMwmrGrowingQueue<65536>*>(m_pFreeQueue)->GetFreeCount();
    }

    std::int64_t GetTotalMallocMemSize() noexcept {
        return UsesSmartQueue() ? static_cast<CSmartQueue*>(m_pFreeQueue)->GetTotalMallocMemSize()
                                : static_cast<CMwmrGrowingQueue<65536>*>(m_pFreeQueue)->GetTotalMallocMemSize();
    }

    void SetLogging(const long logging) noexcept {
        if (UsesSmartQueue()) {
            static_cast<CSmartQueue*>(m_pFreeQueue)->SetLogging(logging);
        }
    }

private:
    static bool UsesSmartQueue() noexcept {
        return runtime::process::GetMemoryPoolQueueConfiguration().model ==
            runtime::process::MemoryPoolQueueModel::Smart;
    }

    const std::size_t m_nBlockSize;
    // The target field is CSmartQueue* in CacheD and
    // CMwmrGrowingQueue<65536>* in L2NPC. A pointer-sized carrier keeps
    // the common 0x20 CMemoryPool layout while the process composition above
    // selects the one class actually linked by that executable.
    void* m_pFreeQueue;
    volatile std::int32_t m_nAlloc;
    volatile std::int64_t m_nReuse;
};


class CMemoryManager {
public:
    static std::int32_t AllocationClassId(const void* allocation) noexcept {
        const auto* const header = static_cast<const AllocationHeader*>(allocation) - 1;
        std::int32_t classId;
        std::memcpy(&classId, header->transportPadding, sizeof(classId));
        return classId;
    }

    static void SetAllocationClassId(void* allocation, std::int32_t classId) noexcept {
        auto* const header = static_cast<AllocationHeader*>(allocation) - 1;
        static_assert(sizeof(header->transportPadding) >= sizeof(classId));
        std::memcpy(header->transportPadding, &classId, sizeof(classId));
    }

    CMemoryManager() {
        // The non-trivial lock/map members have already been constructed at
        // this point. Current L2NPC 0x0046a3d5..0x0046a40a and current CacheD
        // CMemoryManager::CMemoryManager at 0x004f7200 then clear only these
        // six scalar fields before creating the same 49 pools.
        nTotalAlloc = 0;
        m_isLogging = 0;
        nOver32KAllocSize = 0;
        nXSTDAllocSize = 0;
        nSerialFastAllocSize = 0;
        nTotalAllocSize = 0;

        for (std::size_t index = 0; index < kMemoryPoolCount; ++index) {
            const std::size_t blockSize = MemoryPoolBlockSize(index);
            Pool[index] = new CMemoryPool(blockSize);
        }

        for (std::size_t size = 0; size <= kMaximumPooledAllocation; ++size) {
            for (std::size_t poolIndex = 0; poolIndex < kMemoryPoolCount; ++poolIndex) {
                if (Pool[poolIndex]->GetBlockSize() >= size) {
                    MemSizeToInfoIndex[size] = static_cast<int>(poolIndex);
                    break;
                }
            }
        }
    }

    // The four-byte prefix before the user pointer holds the requested size
    // plus four, as in the target allocator.
    static std::size_t RequestedSize(const void* allocation) noexcept {
        if (!allocation) {
            return 0;
        }
        std::uint32_t logicalSize = 0;
        std::memcpy(&logicalSize, static_cast<const std::byte*>(allocation) - sizeof(logicalSize), sizeof(logicalSize));
        return logicalSize >= 4U ? static_cast<std::size_t>(logicalSize - 4U) : 0;
    }

    void* Malloc(std::size_t requestedSize, bool, const wchar_t* memoryTag, int allocationType) noexcept {
        // Current L2NPC 0x00469904 and CacheD CMemoryManager::Malloc at
        // 0x004f69d8 use one wrapping x64 LEA for size + 4. Neither rejects
        // large inputs nor widens the four-byte prefix later consumed by
        // Free.
        const std::uint64_t logicalSize = static_cast<std::uint64_t>(requestedSize) + UINT64_C(4);
        const std::int64_t accountedSize = runtime::Int64FromBits(logicalSize);
        constexpr std::size_t transportBytes = sizeof(AllocationHeader) - sizeof(std::uint32_t);
        const bool transportSizeFits =
            logicalSize <= static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max() - transportBytes);
        const std::size_t transportSize = transportSizeFits ? static_cast<std::size_t>(logicalSize) + transportBytes : 0;
        const std::size_t portablePoolIndex =
            logicalSize <= kMaximumPooledAllocation && transportSizeFits && transportSize <= kMaximumPooledAllocation
            ? PortablePoolIndex(transportSize)
            : kMemoryPoolCount;
        const bool usePortablePool = portablePoolIndex < kMemoryPoolCount;

        // Count the request before allocation and roll it back on failure.
        runtime::raw_atomic::AddI32(&nTotalAlloc, 1);
        runtime::raw_atomic::AddI64(&nTotalAllocSize, accountedSize);
        if (allocationType == 1) {
            runtime::raw_atomic::AddI64(&nXSTDAllocSize, accountedSize);
        } else if (allocationType == 2) {
            runtime::raw_atomic::AddI64(&nSerialFastAllocSize, accountedSize);
        }

        void* raw = nullptr;
        if (usePortablePool) {
            // The target prefix is four bytes and therefore returns an
            // under-aligned x64 pointer. The portable prefix
            // adds only enough leading bytes to keep max alignment. Pool
            // selection must include those transport bytes or a small object
            // would receive less storage than requested.
            raw = Pool[portablePoolIndex]->Malloc();
        } else {
            if (transportSizeFits) {
                raw = std::malloc(transportSize);
            }
            if (!raw) {
                const std::int64_t rollback = runtime::NegateInt64Wrapping(accountedSize);
                runtime::raw_atomic::AddI32(&nTotalAlloc, -1);
                runtime::raw_atomic::AddI64(&nTotalAllocSize, rollback);
                if (allocationType == 1) {
                    runtime::raw_atomic::AddI64(&nXSTDAllocSize, rollback);
                } else if (allocationType == 2) {
                    runtime::raw_atomic::AddI64(&nSerialFastAllocSize, rollback);
                }
                return nullptr;
            }
            // At most `transportBytes` bytes at the top of the target's pool
            // range use host malloc solely for alignment capacity. They are
            // still <=32 KiB allocations in all recovered diagnostics.
            if (logicalSize > kMaximumPooledAllocation) {
                runtime::raw_atomic::AddI64(&nOver32KAllocSize, accountedSize);

                const int threadId = CThreadManager::GetThreadID();
                const int depth = GuardHelper::getCallStackDepth(threadId);
                if (depth < 0) {
                    if (g_bShowMallocLog) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[NO_ERROR] Malloc (>32k), "
                        //     L"ClassName[%s], AllocSize[%d]",
                        //     memoryTag,
                        //     runtime::Int32FromBits(
                        //         static_cast<std::uint32_t>(
                        //             logicalSize)))
                        // Log.Add(
                        //     LOG_NORMAL,
                        //     L"[memory] Large allocation for \"%ls\": %llu bytes "
                        //     L"including the 4-byte size field.",
                        //     memoryTag ? memoryTag : L"unknown class",
                        //     static_cast<unsigned long long>(logicalSize))
                        L2_LOG_INFO(
                            Log,
                            u"[memory] Large allocation for \"%ls\": %llu bytes "
                            u"including the 4-byte size field.",
                            memoryTag ? memoryTag : L"unknown class", static_cast<unsigned long long>(logicalSize)
                        );
                    }
                } else if (g_bShowMallocLog) {
                    const int callerDepth = depth - 1 > 0 ? depth - 1 : 0;
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[NO_ERROR] Malloc (>32k), "
                    //     L"ClassName[%s], AllocSize[%d] "
                    //     L"Call by [%s]",
                    //     memoryTag,
                    //     runtime::Int32FromBits(
                    //         static_cast<std::uint32_t>(
                    //             logicalSize)),
                    //     GuardHelper::getCallStack(
                    //         threadId,
                    //         callerDepth))
                    const wchar_t* const caller = GuardHelper::getCallStack(threadId, callerDepth);
                    // Log.Add(
                    //     LOG_NORMAL,
                    //     L"[memory] Large allocation for \"%ls\": %llu bytes "
                    //     L"including the 4-byte size field. Caller \"%ls\".",
                    //     memoryTag ? memoryTag : L"unknown class",
                    //     static_cast<unsigned long long>(logicalSize),
                    //     caller ? caller : L"unknown frame")
                    L2_LOG_INFO(
                        Log,
                        u"[memory] Large allocation for \"%ls\": %llu bytes "
                        u"including the 4-byte size field; caller \"%ls\".",
                        memoryTag ? memoryTag : L"unknown class", static_cast<unsigned long long>(logicalSize),
                        caller ? caller : L"unknown frame"
                    );
                }
            }
        }

        auto* const header = static_cast<AllocationHeader*>(raw);
        header->logicalSize = static_cast<std::uint32_t>(logicalSize);
        SetAllocationClassId(header + 1, -1);

        return header + 1;
    }

    void SetLogging(const long logging) noexcept {
        if (runtime::raw_atomic::LoadI32Relaxed(&m_isLogging) == logging) {
            return;
        }

        runtime::raw_atomic::ExchangeI32(&m_isLogging, logging);
        for (CMemoryPool* const pool : Pool) {
            pool->SetLogging(logging);
        }
    }

    void Free(void* allocation, int allocationType) noexcept {
        // Current L2NPC 0x0046976c and CacheD CMemoryManager::Free at
        // 0x004f6658 subtract four from the user pointer and sign-extend that
        // dword. Keep the exact externally visible prefix while stepping
        // farther back only to recover the aligned host block.
        const std::uintptr_t userAddress = reinterpret_cast<std::uintptr_t>(allocation);
        std::uint32_t logicalSizeBits = 0;
        std::memcpy(
            &logicalSizeBits, reinterpret_cast<const void*>(userAddress - sizeof(std::uint32_t)), sizeof(logicalSizeBits)
        );
        const std::int32_t logicalSize = runtime::Int32FromBits(logicalSizeBits);
        const std::uint64_t unsignedLogicalSize = static_cast<std::uint64_t>(static_cast<std::int64_t>(logicalSize));
        constexpr std::size_t transportBytes = sizeof(AllocationHeader) - sizeof(std::uint32_t);
        const bool transportSizeFits =
            unsignedLogicalSize <= static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max() - transportBytes);
        const std::size_t transportSize =
            transportSizeFits ? static_cast<std::size_t>(unsignedLogicalSize) + transportBytes : 0;
        const std::size_t portablePoolIndex =
            unsignedLogicalSize <= kMaximumPooledAllocation && transportSizeFits && transportSize <= kMaximumPooledAllocation
            ? PortablePoolIndex(transportSize)
            : kMemoryPoolCount;
        const bool usedPortablePool = portablePoolIndex < kMemoryPoolCount;
        const std::int64_t delta = -static_cast<std::int64_t>(logicalSize);
        auto* const header = reinterpret_cast<AllocationHeader*>(userAddress - sizeof(AllocationHeader));

        runtime::raw_atomic::AddI32(&nTotalAlloc, -1);
        runtime::raw_atomic::AddI64(&nTotalAllocSize, delta);
        if (allocationType == 1) {
            runtime::raw_atomic::AddI64(&nXSTDAllocSize, delta);
        } else if (allocationType == 2) {
            runtime::raw_atomic::AddI64(&nSerialFastAllocSize, delta);
        }

        if (usedPortablePool) {
            Pool[portablePoolIndex]->Free(header);
        } else {
            std::free(header);
            if (unsignedLogicalSize > kMaximumPooledAllocation) {
                runtime::raw_atomic::AddI64(&nOver32KAllocSize, delta);
            }
        }
    }

    ~CMemoryManager() {
        for (CMemoryPool* pool : Pool) {
            delete pool;
        }
    }

    void GetMemoryMon(wchar_t*) noexcept {
        // Present in the PDB surface but empty in the shipping image.
    }

    std::int64_t GetTotalAllocSize() noexcept { return runtime::raw_atomic::LoadI64(&nTotalAllocSize); }

    void Report() {
        std::int64_t reuse = 0;
#if defined(L2_BUILD_CACHED)
        std::int64_t pooledAllocatedSize = 0;
#endif
        for (int index = 0; index < static_cast<int>(kMemoryPoolCount); ++index) {
            reuse = runtime::AddInt64Wrapping(reuse, Pool[static_cast<std::size_t>(index)]->GetReuseCount());
#if defined(L2_BUILD_CACHED)
            // Current Cached.exe 0x004f66e8 performs a second live pool
            // lookup per iteration and accumulates CSmartQueue's qword total
            // at +0. L2NPC.exe 0x004692ac instead emits a literal zero for
            // this report field, so keep the process distinction explicit.
            pooledAllocatedSize = runtime::AddInt64Wrapping(
                pooledAllocatedSize, Pool[static_cast<std::size_t>(index)]->GetTotalMallocMemSize()
            );
#endif
        }

        // Log.Add(
        //     LOG_NORMAL,
        //     L"Memory Total Allocation Count : %d "
        //     L"Reuse : %I64d ",
        //     runtime::raw_atomic::
        //         LoadI32Relaxed(
        //             &nTotalAlloc),
        //     static_cast<long long>(reuse))
        // Log.Add(
        //     LOG_NORMAL,
        //     "[memory] Allocation balance (requests minus frees): %d. "
        //     "pooled allocation requests: %lld.",
        //     runtime::raw_atomic::LoadI32Relaxed(&nTotalAlloc),
        //     static_cast<long long>(reuse))
        L2_LOG_INFO(
            Log,
            "[memory] Allocation balance (requests minus frees): %d; "
            "pooled allocation requests: %lld.",
            runtime::raw_atomic::LoadI32Relaxed(&nTotalAlloc), static_cast<long long>(reuse)
        );

        constexpr std::int64_t bytesPerMiB = INT64_C(1) << 20;
        // Current 0x004692fb..0x00469387 evaluates the six report values
        // from right to left before the variadic call.
        const std::int64_t serialArgsSize = runtime::raw_atomic::LoadI64Relaxed(&nSerialFastAllocSize);
        const std::int64_t xstdSize = runtime::raw_atomic::LoadI64Relaxed(&nXSTDAllocSize);
        const std::int64_t ioBufferSize = runtime::raw_atomic::LoadI64Relaxed(&g_CMwsrQueueAlloc);
        const std::int64_t memoryMapSize = runtime::raw_atomic::LoadI64Relaxed(&g_MemMapAlloc);
        const std::int64_t over32KSize = runtime::raw_atomic::LoadI64Relaxed(&nOver32KAllocSize);
        // Log.Add(
        //     LOG_NORMAL,
        //     L"MallocSize(<=32K) : %I64d MB  "
        //     L"MallocSize(>32K) : %I64d MB "
        //     L"MemoryMapSize : %d MB  "
        //     L"IOBufferSize : %I64d MB, "
        //     L"XSTD Size :  %I64d MB(in MallocSize),  "
        //     L"SerialArgs Size :  %I64d MB(in MallocSize)",
        // #if defined(L2_BUILD_CACHED)
        //     static_cast<long long>(
        //         pooledAllocatedSize /
        //         bytesPerMiB),
        // #else
        //     0LL,
        // #endif
        //     static_cast<long long>(
        //         over32KSize /
        //         bytesPerMiB),
        //     static_cast<int>(
        //         memoryMapSize / bytesPerMiB),
        //     static_cast<long long>(
        //         ioBufferSize / bytesPerMiB),
        //     static_cast<long long>(
        //         xstdSize /
        //         bytesPerMiB),
        //     static_cast<long long>(
        //         serialArgsSize /
        //         bytesPerMiB))
        // Log.Add(
        //     LOG_NORMAL,
        // #if defined(L2_BUILD_CACHED)
        //     "[memory] Pool storage (<=32 KiB): %lld MiB"
        // #else
        //     "[memory] Pool storage (<=32 KiB): not reported"
        // #endif
        //     ". Large allocations: %lld MiB. Mapped memory: %lld MiB. "
        //     "queue storage: %lld MiB. XSTD: %lld MiB and serial arguments: "
        //     "%lld MiB (both included in allocations).",
        // #if defined(L2_BUILD_CACHED)
        //     static_cast<long long>(pooledAllocatedSize / bytesPerMiB),
        // #endif
        //     static_cast<long long>(over32KSize / bytesPerMiB),
        //     static_cast<long long>(memoryMapSize / bytesPerMiB),
        //     static_cast<long long>(ioBufferSize / bytesPerMiB),
        //     static_cast<long long>(xstdSize / bytesPerMiB),
        //     static_cast<long long>(serialArgsSize / bytesPerMiB))
        ::runtime::logging::LogAt(
            Log, LOG_NORMAL, __FILE__,
#if defined(L2_BUILD_CACHED)
            "[memory] Pool storage (<=32 KiB): %lld MiB"
#else
            "[memory] Pool storage (<=32 KiB): not reported"
#endif
            "; large allocations: %lld MiB; mapped memory: %lld MiB; "
            "queue storage: %lld MiB; XSTD: %lld MiB and serial arguments: "
            "%lld MiB (both included in allocations).",
#if defined(L2_BUILD_CACHED)
            static_cast<long long>(pooledAllocatedSize / bytesPerMiB),
#endif
            static_cast<long long>(over32KSize / bytesPerMiB), static_cast<long long>(memoryMapSize / bytesPerMiB),
            static_cast<long long>(ioBufferSize / bytesPerMiB), static_cast<long long>(xstdSize / bytesPerMiB),
            static_cast<long long>(serialArgsSize / bytesPerMiB)
        );
        // Noise: The separator adds no information. Each pool row now carries its own context and labels.
        // Log.Add(
        //     LOG_NORMAL,
        //     L"==============================================="
        //     L" MEMORY POOL "
        //     L"===================================================")
        // Noise: Column meanings are included in every pool row, so a detached table header is unnecessary.
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Index\tBlock\tAlloc\tUse\tReuse\tFreeQueue")

        for (int index = 0; index < static_cast<int>(kMemoryPoolCount); ++index) {
            const std::int32_t allocCount = Pool[static_cast<std::size_t>(index)]->GetAllocCount();
            const std::int64_t reuseCount = Pool[static_cast<std::size_t>(index)]->GetReuseCount();
            CMemoryPool* const freeAndBlockPool = Pool[static_cast<std::size_t>(index)];
            const std::int32_t freeCount = freeAndBlockPool->GetFreeQueueSize();
            const std::int32_t useCount = runtime::SubtractInt32Wrapping(allocCount, freeCount);
            const std::size_t blockSize = freeAndBlockPool->GetBlockSize();
            // Log.Add(
            //     LOG_NORMAL,
            //     L"%d\t%d\t%10d\t%10d\t"
            //     L"%I64d\t%10d",
            //     index,
            //     static_cast<int>(
            //         blockSize),
            //     allocCount,
            //     useCount,
            //     static_cast<long long>(
            //         reuseCount),
            //     freeCount)
            // Log.Add(
            //     LOG_NORMAL,
            //     "[memory] Pool %d: blocks of %zu bytes. Allocated %d, "
            //     "in use %d, requests %lld, free %d.",
            //     index, blockSize, allocCount, useCount,
            //     static_cast<long long>(reuseCount), freeCount)
            L2_LOG_INFO(
                Log,
                "[memory] Pool %d: blocks of %zu bytes; allocated %d, "
                "in use %d, requests %lld, free %d.",
                index, blockSize, allocCount, useCount, static_cast<long long>(reuseCount), freeCount
            );
        }

        // Log.Add(
        //     LOG_NORMAL,
        //     L"IOBufferPoolSize = %d",
        //     GetIOBufferPoolCount())
        // Log.Add(
        //     LOG_NORMAL,
        //     "[memory] Free IO buffers across workers: %d.",
        //     GetIOBufferPoolCount())
        L2_LOG_INFO(Log, "[memory] Free IO buffers across workers: %d.", GetIOBufferPoolCount());
        // Noise: Each worker row already identifies the IO buffer pool and both counts.
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Detail IO Buffer Pool Size for each Thread")
        for (int threadId = 0; threadId < CThreadManager::GetThreadCount(); ++threadId) {
            auto* const totalPool = g_IOBufferPools[threadId];
            const int totalCount = totalPool ? totalPool->GetMaxSize() : 0;
            auto* const freePool = g_IOBufferPools[threadId];
            const int freeCount = freePool ? freePool->InstantSize() : 0;
            // Log.Add(
            //     LOG_NORMAL,
            //     L"(%d) : Free(%d) / Total(%d)",
            //     threadId,
            //     freeCount,
            //     totalCount)
            // Log.Add(
            //     LOG_NORMAL,
            //     "[memory] Worker %d IO buffers: %d free out of %d allocated.",
            //     threadId, freeCount, totalCount)
            L2_LOG_INFO(Log, "[memory] Worker %d IO buffers: %d free out of %d allocated.", threadId, freeCount, totalCount);
        }
    }

private:
    CMemoryPool* Pool[kMemoryPoolCount];
    int MemSizeToInfoIndex[kMaximumPooledAllocation + 1];
    volatile std::int32_t nTotalAlloc;
    volatile std::int32_t m_isLogging;
    volatile std::int64_t nOver32KAllocSize;
    volatile std::int64_t nXSTDAllocSize;
    volatile std::int64_t nSerialFastAllocSize;
    volatile std::int64_t nTotalAllocSize;

public:
    CIOCriticalSection MemoryStatLock;
    std::map<std::CDualKey<const wchar_t*, int>, int> MemoryStatMap;
};
