#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>

// System allocation with requested-byte diagnostics, without size-class pools.
// The header belongs to the Memory API only. Ordinary STL allocations bypass it.
class CMemoryManager {
    struct alignas(64) Statistics {
        std::atomic<std::int64_t> count{0};
        std::atomic<std::int64_t> bytes{0};
        std::atomic<std::int64_t> xstd{0};
        std::atomic<std::int64_t> serializer{0};
    };
    struct alignas(std::max_align_t) Header {
        std::size_t size;
        Statistics* statistics;
        std::int32_t classId;
    };
    std::array<Statistics, 64> statistics_{};
    std::atomic<unsigned> nextShard_{0};

    Statistics& CurrentStatistics() noexcept {
        // One manager per process, kept through global destruction.
        thread_local const unsigned shard = nextShard_.fetch_add(1, std::memory_order_relaxed) % statistics_.size();
        return statistics_[shard];
    }
    static void Account(Statistics& statistics, std::int64_t size, int type, int count) noexcept {
        statistics.count.fetch_add(count, std::memory_order_relaxed);
        statistics.bytes.fetch_add(size, std::memory_order_relaxed);
        if (type == 1) {
            statistics.xstd.fetch_add(size, std::memory_order_relaxed);
        }
        if (type == 2) {
            statistics.serializer.fetch_add(size, std::memory_order_relaxed);
        }
    }

public:
    static std::int32_t AllocationClassId(const void* allocation) noexcept {
        return (static_cast<const Header*>(allocation) - 1)->classId;
    }
    static void SetAllocationClassId(void* allocation, std::int32_t classId) noexcept {
        (static_cast<Header*>(allocation) - 1)->classId = classId;
    }
    static std::size_t RequestedSize(const void* allocation) noexcept {
        return allocation ? (static_cast<const Header*>(allocation) - 1)->size : 0;
    }
    void* Malloc(std::size_t size, bool, const wchar_t*, int type) {
        if (size > std::numeric_limits<std::size_t>::max() - sizeof(Header) ||
            size > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
            throw std::bad_alloc();
        }
        void* storage = ::operator new(sizeof(Header) + size);
        auto& statistics = CurrentStatistics();
        auto* header = new (storage) Header{size, &statistics, -1};
        Account(statistics, static_cast<std::int64_t>(size), type, 1);
        return header + 1;
    }
    void Free(void* allocation, int type) noexcept {
        if (!allocation) {
            return;
        }
        auto* header = static_cast<Header*>(allocation) - 1;
        Account(*header->statistics, -static_cast<std::int64_t>(header->size), type, -1);
        header->~Header();
        ::operator delete(header);
    }
    std::int64_t GetTotalAllocSize() const noexcept {
        std::int64_t result = 0;
        for (const auto& statistics : statistics_) {
            result += statistics.bytes.load(std::memory_order_relaxed);
        }
        return result;
    }
    void SetLogging(long logging) {
        L2_LOG_INFO(Log, "[memory] System allocator selected; legacy pool tracing is unavailable (requested=%ld).", logging);
    }
    void Report() const {
        std::int64_t count = 0, bytes = 0, xstd = 0, serializer = 0;
        for (const auto& statistics : statistics_) {
            count += statistics.count.load(std::memory_order_relaxed);
            bytes += statistics.bytes.load(std::memory_order_relaxed);
            xstd += statistics.xstd.load(std::memory_order_relaxed);
            serializer += statistics.serializer.load(std::memory_order_relaxed);
        }
        L2_LOG_INFO(
            Log,
            "[memory] System allocator: %lld live allocations, %lld requested bytes "
            "(xstd=%lld, serializer=%lld); counters sampled independently, excludes ordinary STL allocations and object "
            "pools.",
            static_cast<long long>(count), static_cast<long long>(bytes), static_cast<long long>(xstd),
            static_cast<long long>(serializer)
        );
    }
};

inline CMemoryManager& SystemMemoryManager() {
    // Thread-safe first use, including allocations during static initialization.
    // Intentionally kept: global object destructors can still call Memory.
    static CMemoryManager* const manager = new CMemoryManager;
    return *manager;
}
