#pragma once

// Shared completion-port transport used by every recovered server process.

#include "Shared/src/Runtime/StackThread.h"
#include "Shared/src/IOC.h"

#include <array>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>


class CIOCPMessage;

namespace portable {

// Embedded-host teardown support. The shipping CIOObject API has no
// cancellation method because its global owners live until process exit.
std::size_t CancelTimersForOwner(CIOObject* owner, std::size_t* nonzeroUidCount = nullptr);

} // namespace portable

// Cross-platform transport for the recovered IOThreadCallback contract.
// Windows used an IO completion port to wake the workers. The portable
// implementation changes only that wait/dispatch primitive. Logical thread
// ids, the 50 ms synchronization boundary and callback execution stay the
// target contract.
class IOThreadRuntime {
public:
    using CallbackFactory = std::function<std::shared_ptr<IOThreadCallback>(int)>;
    using RawWorkerBody = std::function<void(int)>;

    IOThreadRuntime() = default;
    ~IOThreadRuntime();

    IOThreadRuntime(const IOThreadRuntime&) = delete;
    IOThreadRuntime& operator=(const IOThreadRuntime&) = delete;

    // InitIOThread installs the completion port before CreateIOThread2
    // creates workers. Posts made in that interval remain queued.
    void Install();
    void Uninstall();
    void Start(int threadCount, CallbackFactory callbackFactory);
    // CacheD's current Shared/ioc.cpp has no IOThreadCallback owner. This is
    // the portable transport entry used by its exact global
    // CreateIOThread(int)/IOThread_common(void*) path.
    void StartRaw(int threadCount, RawWorkerBody workerBody);
    void Stop();

    void Wake();

    bool Installed() const;
    bool Running() const;
    bool IsCurrentThread() const noexcept;
    int ThreadCount() const;

private:
    friend bool ::CreateIOThread2(IOThreadCallback* callback);
    friend bool portable::PostIOCompletion(CIOObject* object, std::uint32_t transferred, PortableOverlapped* overlapped);
    friend bool portable::PostIOCompletionResult(
        CIOObject* object, int success, std::uint32_t transferred, PortableOverlapped* overlapped, int socketError
    );
    friend class CIOObject;
    friend class CIOCPMessage;
    friend class IOThreadCallback;
#if defined(L2_BUILD_CACHED)
    friend void ::IOThread_common(void* argument);
#endif

    struct QueueEntry {
        CIOObject* object = nullptr;
        int success = 0;
        int socketError = 0;
        std::uint32_t transferred = 0;
        PortableOverlapped* overlapped = nullptr;
    };

    enum class BoundWaitResult {
        Timeout,
        Completion,
        Stop,
    };

    static bool PostCompletion(
        CIOObject* object, std::uint32_t transferred, PortableOverlapped* overlapped, int success = 1, int socketError = 0
    );
    bool EnqueueCompletion(
        CIOObject* object, std::uint32_t transferred, PortableOverlapped* overlapped, int success, int socketError
    );
    static void DrainAcceptedCompletions(std::deque<QueueEntry>& entries);
    static BoundWaitResult WaitBoundCompletion(
        std::uint32_t waitMilliseconds, QueueEntry& entry, std::size_t queueIndex = 0
    );
    static bool StartBoundThread(IOThreadCallback* callback);
    bool StartWorker(IOThreadCallback* callback);
    BoundWaitResult WaitForCompletion(std::uint32_t waitMilliseconds, QueueEntry& entry, std::size_t queueIndex);

    // Current L2Server.exe, L2NPC.exe and Cached.exe all use stackSize=0 in
    // _beginthreadex and inherit the same 0x100000 PE stack reserve.
    static constexpr std::size_t kWorkerStackReserve = 0x100000;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    // Cached.exe owns two IOCP handles. All currently recovered producers
    // post to queue zero. Workers 8..15 still wait and dispatch timers on the
    // distinct second queue.
    std::array<std::deque<QueueEntry>, 2> queues_;
    std::vector<std::shared_ptr<IOThreadCallback>> callbacks_;
    std::vector<runtime::StackThread> threads_;
    bool installed_ = false;
    bool running_ = false;
    bool stopping_ = false;
    int threadCount_ = 0;

    static thread_local IOThreadRuntime* currentRuntime_;
};

namespace io_thread_runtime_detail {

// Process-lifetime equivalent of the target completion-port globals. These
// accessors also make construction safe when process globals post during
// another translation unit's initializer or destructor.
std::mutex& CompletionBackendMutex();
IOThreadRuntime*& CompletionBackend();

} // namespace io_thread_runtime_detail
