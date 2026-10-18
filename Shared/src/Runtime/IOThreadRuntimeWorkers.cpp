// Worker creation is kept in its own archive unit. Posting completions is a
// common socket primitive. Creating IOThreadCallback workers also selects a
// process-owned callback implementation and must only be linked by a server
// that actually starts those workers.
#include "Shared/src/Runtime/IOThreadRuntime.h"

#include "Shared/src/ThreadManager.h"

#include <stdexcept>

#if defined(L2_BUILD_L2NPC) || defined(L2_BUILD_L2SERVER)

#include <algorithm>
#include <utility>

void IOThreadRuntime::Start(int threadCount, CallbackFactory callbackFactory) {
    constexpr int maximumThreadCount =
#if defined(L2_BUILD_L2SERVER)
        16;
#else
        ThreadID::IO_7 + 1;
#endif
    if (threadCount < 1 || threadCount > maximumThreadCount) {
        throw std::invalid_argument("IOThreadRuntime thread count is outside the product limit");
    }
    if (!callbackFactory) {
        throw std::invalid_argument("IOThreadRuntime requires callbacks");
    }

    std::unique_lock<std::mutex> guard(mutex_);
    if (running_) {
        return;
    }
    if (!installed_) {
        throw std::logic_error("IOThreadRuntime::Start requires Install");
    }

    callbacks_.reserve(static_cast<std::size_t>(threadCount));
    threads_.reserve(static_cast<std::size_t>(threadCount));
    for (int thread = 0; thread < threadCount; ++thread) {
        std::shared_ptr<IOThreadCallback> callback = callbackFactory(thread);
        if (!callback || callback->m_Thread != thread) {
            callbacks_.clear();
            throw std::invalid_argument("IOThreadRuntime callback has wrong thread id");
        }
        callbacks_.push_back(std::move(callback));
    }

    stopping_ = false;
    running_ = true;
    threadCount_ = threadCount;
    CThreadManager::SetThreadCount(threadCount);
    guard.unlock();

    try {
        for (const auto& callback : callbacks_) {
            IOThreadCallback* raw = callback.get();
            if (!::CreateIOThread2(raw)) {
                throw std::logic_error("CreateIOThread2 rejected callback");
            }
        }
    } catch (...) {
        {
            std::lock_guard<std::mutex> failureGuard(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
        for (runtime::StackThread& thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        {
            std::lock_guard<std::mutex> failureGuard(mutex_);
            threads_.clear();
            callbacks_.clear();
            running_ = false;
            stopping_ = false;
            threadCount_ = 0;
        }
        throw;
    }
}

#endif // L2_BUILD_L2NPC || L2_BUILD_L2SERVER

#if defined(L2_BUILD_CACHED)

void IOThreadRuntime::StartRaw(int threadCount, RawWorkerBody workerBody) {
    // Current CacheD caps GetIOThreadCount at 16. The wider ThreadManager
    // capacity also contains its fixed non-IO roles through ID 21.
    if (threadCount < 1 || threadCount > 16) {
        throw std::invalid_argument("IOThreadRuntime CacheD thread count must be in [1, 16]");
    }
    if (!workerBody) {
        throw std::invalid_argument("IOThreadRuntime requires a raw worker body");
    }

    std::unique_lock<std::mutex> guard(mutex_);
    if (running_) {
        return;
    }
    if (!installed_) {
        throw std::logic_error("IOThreadRuntime::StartRaw requires Install");
    }

    threads_.reserve(static_cast<std::size_t>(threadCount));
    stopping_ = false;
    running_ = true;
    threadCount_ = threadCount;
    CThreadManager::SetThreadCount(threadCount);
    guard.unlock();

    try {
        for (int thread = 0; thread < threadCount; ++thread) {
            threads_.emplace_back(kWorkerStackReserve, [this, workerBody, thread] {
                currentRuntime_ = this;
                workerBody(thread);
                CThreadManager::SetThread(thread, nullptr);
                currentRuntime_ = nullptr;
            });
            CThreadManager::SetThread(thread, threads_.back().native_handle());
        }
    } catch (...) {
        {
            std::lock_guard<std::mutex> failureGuard(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
        for (runtime::StackThread& thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        {
            std::lock_guard<std::mutex> failureGuard(mutex_);
            for (int thread = 0; thread < threadCount_; ++thread) {
                CThreadManager::SetThread(thread, nullptr);
            }
            threads_.clear();
            running_ = false;
            stopping_ = false;
            threadCount_ = 0;
        }
        throw;
    }
}

#endif // L2_BUILD_CACHED

#if defined(L2_BUILD_L2NPC) || defined(L2_BUILD_L2SERVER)

bool IOThreadRuntime::StartBoundThread(IOThreadCallback* callback) {
    std::lock_guard<std::mutex> backendGuard(io_thread_runtime_detail::CompletionBackendMutex());
    return io_thread_runtime_detail::CompletionBackend() &&
        io_thread_runtime_detail::CompletionBackend()->StartWorker(callback);
}

bool IOThreadRuntime::StartWorker(IOThreadCallback* callback) {
    if (!callback) {
        return false;
    }
    // L2Server's current ListenThread calls CreateIOThread2 once per raw,
    // process-lifetime callback instead of entering through Start(). Publish
    // the same runtime state on that path so portable teardown can still join
    // the worker threads. L2NPC's Start() path has already set these fields.
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!installed_ || stopping_) {
            return false;
        }
        if (!running_) {
            running_ = true;
            threadCount_ = 0;
        }
        threadCount_ = std::max(threadCount_, callback->m_Thread + 1);
        CThreadManager::SetThreadCount(threadCount_);
    }
    threads_.emplace_back(kWorkerStackReserve, [this, callback] {
        currentRuntime_ = this;
        ::IOThread2(callback);
        CThreadManager::SetThread(callback->m_Thread, nullptr);
        currentRuntime_ = nullptr;
    });
    // Win64 publishes the _beginthreadex handle in this slot. StackThread
    // keeps ownership, but crash/suspend code must still see the native
    // handle instead of the callback object's address.
    CThreadManager::SetThread(callback->m_Thread, threads_.back().native_handle());
    return true;
}

#endif // L2_BUILD_L2NPC || L2_BUILD_L2SERVER
