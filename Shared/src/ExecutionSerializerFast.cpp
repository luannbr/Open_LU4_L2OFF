#include "Shared/src/ThreadSafeLibrary.h"

#include "Shared/src/Runtime/ProcessRuntime.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>

namespace thread_safe_detail {

void ReportExecutionSerializerFastOverflow(int stage, int threadId, const wchar_t* executableName, int queueSize) {
    runtime::process::ReportExecutionSerializerFastOverflow(stage, threadId, executableName, queueSize);
}

} // namespace thread_safe_detail

// These are still the methods owned by Shared/ThreadSafeLibrary.cpp in the
// target. The portable static library gives the target's independent
// template subsystem its own archive member so CacheD does not pull unrelated
// L2NPC smart-id diagnostics merely by constructing a socket serializer.
CExecutionSerializerFast::CExecutionSerializerFast(int maxQueueSize) :
    MaxQueueSize(maxQueueSize),
    Args(new std::atomic<void*>[static_cast<std::size_t>(maxQueueSize)]),
    Funcs(new std::atomic<Executable>[static_cast<std::size_t>(maxQueueSize)]) {
    for (int index = 0; index < MaxQueueSize; ++index) {
        Args[index].store(nullptr, std::memory_order_relaxed);
        Funcs[index].store(nullptr, std::memory_order_relaxed);
    }
}

CExecutionSerializerFast::~CExecutionSerializerFast() {
    delete[] Funcs;
    delete[] Args;
}

int CExecutionSerializerFast::AddExecutable(
    MemoryObject* owner, Executable executable, void* argument, const wchar_t* executableName
) {
    L2_GUARD_FUNCTION();
    const int threadId = CThreadManager::GetThreadID();
    if (RunningThreadID.load(std::memory_order_relaxed) == threadId) {
        executable(owner, argument);
        return 1;
    }

#if defined(L2_BUILD_L2SERVER)
    constexpr int kQueueHeadroom = 16;
#else
    constexpr int kQueueHeadroom = 8;
#endif
    if (Count.load(std::memory_order_relaxed) >= MaxQueueSize - kQueueHeadroom) {
        thread_safe_detail::ReportExecutionSerializerFastOverflow(1, threadId, executableName, MaxQueueSize);
        return -1;
    }

    const std::int64_t reserved = Last.fetch_add(1, std::memory_order_seq_cst) + 1;
    const std::size_t producerSlot =
        static_cast<std::size_t>(static_cast<std::uint64_t>(reserved) % static_cast<std::uint64_t>(MaxQueueSize));
    Args[producerSlot].store(argument, std::memory_order_relaxed);
    // The Windows target stores Args first, stores Funcs second and then
    // publishes Count with an interlocked increment. x86-64 keeps the
    // two stores in that order. The portable ARM path must express the same
    // publication edge so a consumer cannot observe Funcs while
    // still seeing the previous Args value for this slot.
    Funcs[producerSlot].store(executable, std::memory_order_release);

    const std::int32_t queued = Count.fetch_add(1, std::memory_order_seq_cst) + 1;
    if (queued >= MaxQueueSize) {
        thread_safe_detail::ReportExecutionSerializerFastOverflow(2, threadId, executableName, MaxQueueSize);
        return -1;
    }
    if (queued != 1) {
        return 0;
    }

    do {
        RunningThreadID.store(CThreadManager::GetThreadID(), std::memory_order_relaxed);
        const std::int64_t claimed = First.load(std::memory_order_relaxed) + 1;
        First.store(claimed, std::memory_order_relaxed);
        const std::size_t consumerSlot =
            static_cast<std::size_t>(static_cast<std::uint64_t>(claimed) % static_cast<std::uint64_t>(MaxQueueSize));

        Executable queuedExecutable = nullptr;
        while ((queuedExecutable = Funcs[consumerSlot].load(std::memory_order_acquire)) == nullptr) {
            std::this_thread::yield();
        }
        void* const queuedArgument = Args[consumerSlot].load(std::memory_order_relaxed);
        queuedExecutable(owner, queuedArgument);
        Funcs[consumerSlot].store(nullptr, std::memory_order_relaxed);
        RunningThreadID.store(-1, std::memory_order_relaxed);
    } while (Count.fetch_sub(1, std::memory_order_seq_cst) != 1);
    return 0;
}
