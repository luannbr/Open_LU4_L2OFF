// Shared native completion queue for the recovered IOCP contract.
#include "Shared/src/Runtime/IOThreadRuntime.h"

#include "Shared/src/ThreadManager.h"

#include <chrono>
#include <stdexcept>
#include <utility>

namespace io_thread_runtime_detail {

namespace {

struct CompletionBackendRegistry {
    std::mutex mutex;
    IOThreadRuntime* backend = nullptr;
};

CompletionBackendRegistry& Registry() {
    static auto* const registry = new CompletionBackendRegistry;
    return *registry;
}

} // namespace

std::mutex& CompletionBackendMutex() { return Registry().mutex; }

IOThreadRuntime*& CompletionBackend() { return Registry().backend; }

} // namespace io_thread_runtime_detail

thread_local IOThreadRuntime* IOThreadRuntime::currentRuntime_ = nullptr;

bool portable::PostIOCompletion(CIOObject* object, std::uint32_t transferred, PortableOverlapped* overlapped) {
    return IOThreadRuntime::PostCompletion(object, transferred, overlapped);
}

bool portable::PostIOCompletionResult(
    CIOObject* object, int success, std::uint32_t transferred, PortableOverlapped* overlapped, int socketError
) {
    return IOThreadRuntime::PostCompletion(object, transferred, overlapped, success, socketError);
}

IOThreadRuntime::~IOThreadRuntime() { Uninstall(); }

void IOThreadRuntime::Install() {
    std::lock_guard<std::mutex> backendGuard(io_thread_runtime_detail::CompletionBackendMutex());
    std::lock_guard<std::mutex> guard(mutex_);
    if (stopping_) {
        return;
    }
    if (io_thread_runtime_detail::CompletionBackend() && io_thread_runtime_detail::CompletionBackend() != this) {
        throw std::logic_error("another IOThreadRuntime is already installed");
    }
    installed_ = true;
    io_thread_runtime_detail::CompletionBackend() = this;
}

void IOThreadRuntime::Uninstall() {
    {
        std::lock_guard<std::mutex> backendGuard(io_thread_runtime_detail::CompletionBackendMutex());
        std::lock_guard<std::mutex> guard(mutex_);
        if (io_thread_runtime_detail::CompletionBackend() == this) {
            io_thread_runtime_detail::CompletionBackend() = nullptr;
        }
        installed_ = false;
    }
    Stop();
    std::array<std::deque<QueueEntry>, 2> remaining;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        for (std::size_t index = 0; index < queues_.size(); ++index) {
            remaining[index].swap(queues_[index]);
        }
    }
    for (auto& queue : remaining) {
        DrainAcceptedCompletions(queue);
    }
}

void IOThreadRuntime::Stop() {
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!running_) {
            return;
        }
        stopping_ = true;
    }
    wake_.notify_all();

    for (runtime::StackThread& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    std::array<std::deque<QueueEntry>, 2> remaining;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        for (int thread = 0; thread < threadCount_; ++thread) {
            CThreadManager::SetThread(thread, nullptr);
        }
        threads_.clear();
        callbacks_.clear();
        for (std::size_t index = 0; index < queues_.size(); ++index) {
            remaining[index].swap(queues_[index]);
        }
        running_ = false;
        stopping_ = false;
        threadCount_ = 0;
    }
    for (auto& queue : remaining) {
        DrainAcceptedCompletions(queue);
    }
}

bool IOThreadRuntime::PostCompletion(
    CIOObject* object, std::uint32_t transferred, PortableOverlapped* overlapped, int success, int socketError
) {
    std::lock_guard<std::mutex> backendGuard(io_thread_runtime_detail::CompletionBackendMutex());
    return io_thread_runtime_detail::CompletionBackend() &&
        io_thread_runtime_detail::CompletionBackend()->EnqueueCompletion(
            object, transferred, overlapped, success, socketError
        );
}

bool IOThreadRuntime::EnqueueCompletion(
    CIOObject* object, std::uint32_t transferred, PortableOverlapped* overlapped, int success, int socketError
) {
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!installed_ || stopping_) {
            return false;
        }
        QueueEntry entry;
        entry.object = object;
        entry.success = success;
        entry.socketError = socketError;
        entry.transferred = transferred;
        entry.overlapped = overlapped;
        queues_[0].push_back(std::move(entry));
    }
    wake_.notify_one();
    return true;
}

void IOThreadRuntime::DrainAcceptedCompletions(std::deque<QueueEntry>& entries) {
    while (!entries.empty()) {
        QueueEntry entry = std::move(entries.front());
        entries.pop_front();
        if (entry.object) {
            if (!entry.success && entry.socketError != 0) {
                SetSocketLastError(entry.socketError);
            }
            entry.object->OnIOCallback(entry.success, entry.transferred, entry.overlapped);
        }
    }
}

void IOThreadRuntime::Wake() { wake_.notify_all(); }

bool IOThreadRuntime::Running() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return running_ && !stopping_;
}

bool IOThreadRuntime::Installed() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return installed_;
}

bool IOThreadRuntime::IsCurrentThread() const noexcept { return currentRuntime_ == this; }

int IOThreadRuntime::ThreadCount() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return running_ ? threadCount_ : 0;
}

IOThreadRuntime::BoundWaitResult IOThreadRuntime::WaitForCompletion(
    std::uint32_t waitMilliseconds, QueueEntry& entry, std::size_t queueIndex
) {
    std::unique_lock<std::mutex> guard(mutex_);
    if (queueIndex >= queues_.size()) {
        return BoundWaitResult::Stop;
    }
    std::deque<QueueEntry>& queue = queues_[queueIndex];
    if (queue.empty() && !stopping_) {
        wake_.wait_for(guard, std::chrono::milliseconds(waitMilliseconds), [this, queueIndex] {
            return !queues_[queueIndex].empty() || stopping_;
        });
    }
    if (!queue.empty()) {
        entry = std::move(queue.front());
        queue.pop_front();
        return BoundWaitResult::Completion;
    }
    return stopping_ ? BoundWaitResult::Stop : BoundWaitResult::Timeout;
}

IOThreadRuntime::BoundWaitResult IOThreadRuntime::WaitBoundCompletion(
    std::uint32_t waitMilliseconds, QueueEntry& entry, std::size_t queueIndex
) {
    if (!currentRuntime_) {
        return BoundWaitResult::Stop;
    }
    return currentRuntime_->WaitForCompletion(waitMilliseconds, entry, queueIndex);
}
