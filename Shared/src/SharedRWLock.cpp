#include "Shared/src/CenteredSharedRWLock.h"

#include "Shared/src/Runtime/ErrorText.h"
#include "Shared/src/Runtime/OperatorLog.h"
#include "Shared/src/Runtime/PosixSharedMemoryName.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Exception.h"
#include "Shared/src/LockStatusStack.h"
#include "Shared/src/Log.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#if defined(_WIN32)
#include "TextRuntime.h"

#include <limits>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#include <unistd.h>
#endif

namespace {

std::string NarrowLogicalName(const wchar_t* value) {
    if (!value) {
        return {};
    }
    std::string result;
    while (*value != L'\0') {
        const std::uint32_t code = static_cast<std::uint32_t>(*value++);
        result.push_back(code <= 0x7fU ? static_cast<char>(code) : '?');
    }
    return result;
}

#if defined(_WIN32)

// Exact 0x28 target SHAREDINFO image recovered from the matching NASC PDB
// and revalidated against L2NPC.exe's field accesses.
struct WindowsSharedInfo {
    volatile LONG spinCount;
    volatile LONG lockCount;
    volatile DWORD threadId;
    volatile LONG recurseCount;
    volatile DWORD writingThreadId;
    volatile LONG waitingReaders;
    volatile LONG waitingReadOnly;
    volatile LONG waitingWriters;
    volatile LONG active;
    volatile LONG readOnly;
};

#else

constexpr std::uint64_t kPosixLockMagic = UINT64_C(0x4c324352574c4b31); // "L2CRWLK1"
constexpr std::uint32_t kPosixLockVersion = 2;

struct PosixLockHeader {
    std::uint64_t magic;
    std::uint32_t version;
    std::uint32_t lockCount;
    std::uint32_t spinCount;
    std::uint32_t ready;
};

struct PosixSharedLock {
    pthread_mutex_t stateMutex;
    pthread_cond_t readers;
    pthread_cond_t writers;
    CSharedRWLock::SHAREDINFO info;
    std::uint32_t readerPermits;
    std::uint32_t writerPermits;
};

std::size_t AlignUp(std::size_t value, std::size_t alignment) { return (value + alignment - 1) & ~(alignment - 1); }

std::size_t PosixLocksOffset() { return AlignUp(sizeof(PosixLockHeader), alignof(PosixSharedLock)); }

std::size_t PosixRegionBytes(std::size_t lockCount) { return PosixLocksOffset() + lockCount * sizeof(PosixSharedLock); }

PosixSharedLock* PosixLocks(void* view) {
    return reinterpret_cast<PosixSharedLock*>(static_cast<std::uint8_t*>(view) + PosixLocksOffset());
}

void CheckPthreadResult(int result, const char* operation) {
    if (result == 0) {
        return;
    }
    if (std::getenv("L2_IO_EXCEPTION_TRACE") != nullptr) {
        // std::fprintf(
        //     stderr,
        //     "[SHARED_RWLOCK_TRACE] operation=%s result=%d\n",
        //     operation,
        //     result)
        // Keep this opt-in failure trace independent of CLog: the failure may
        // originate inside synchronization used by the logger itself.
        const int diagnosticErrno = errno;
        char message[256]{};
        const int messageLength = std::snprintf(
            message, sizeof(message), "[locks] %s failed (pthread error %d); throwing an exception. Native stack follows.",
            operation ? operation : "unknown operation", result
        );
        // runtime::logging::Write("shared", runtime::logging::Level::Error,
        //     messageLength >= 0 && static_cast<std::size_t>(messageLength) < sizeof(message)
        //         ? message
        //         : "[locks] POSIX synchronization failed, could not format the operation details. Native stack follows.")
        runtime::logging::Write(
            "shared", runtime::logging::Level::Error,
            messageLength >= 0 && static_cast<std::size_t>(messageLength) < sizeof(message)
                ? message
                : "[locks] POSIX synchronization failed; could not format the operation details. Native stack follows.",
            __FILE__
        );
        std::array<void*, 128> frames{};
        const int frameCount = ::backtrace(frames.data(), static_cast<int>(frames.size()));
        ::backtrace_symbols_fd(
            frames.data(), frameCount,
            // ::fileno(stderr))
            ::fileno(stdout)
        );
        // std::fflush(stderr)
        std::fflush(stdout);
        errno = diagnosticErrno;
    }
    throw std::runtime_error(std::string(operation) + " failed: " + std::strerror(result));
}

class PthreadMutexGuard {
public:
    explicit PthreadMutexGuard(pthread_mutex_t* mutex) : mutex_(mutex) {
        CheckPthreadResult(pthread_mutex_lock(mutex_), "pthread_mutex_lock");
    }

    ~PthreadMutexGuard() {
        if (mutex_) {
            pthread_mutex_unlock(mutex_);
        }
    }

    PthreadMutexGuard(const PthreadMutexGuard&) = delete;
    PthreadMutexGuard& operator=(const PthreadMutexGuard&) = delete;

    pthread_mutex_t* Get() const { return mutex_; }

private:
    pthread_mutex_t* mutex_;
};

#endif

std::uint32_t CurrentNativeThreadId() {
#if defined(_WIN32)
    return static_cast<std::uint32_t>(GetCurrentThreadId());
#elif defined(__APPLE__)
    std::uint64_t threadId = 0;
    pthread_threadid_np(nullptr, &threadId);
    return static_cast<std::uint32_t>(threadId);
#elif defined(__linux__)
    return static_cast<std::uint32_t>(syscall(SYS_gettid));
#else
    const pthread_t self = pthread_self();
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&self);
    std::uint32_t hash = static_cast<std::uint32_t>(getpid());
    for (std::size_t index = 0; index < sizeof(pthread_t); ++index) {
        hash = (hash ^ bytes[index]) * UINT32_C(16777619);
    }
    return hash;
#endif
}

// void ReportCleanUnlock() {
void ReportCleanUnlock(void* state, int waitingReaders, int waitingWriters) {
    // Log.Add(
    //     LOG_ERROR,
    //     "Hey! Your are Unlocking clean Lock!")
    // Log.Add(LOG_ERROR,
    //     "[locks] Unlock requested with no active reader or writer, continuing the release path.")
    // L2_LOG_ERROR(Log,
    //     "[locks] Unlock requested with no active reader or writer, continuing the release path.")
    L2_LOG_ERROR(
        Log,
        "[locks] Unlock requested for state %p with no active reader or writer "
        "(%d readers and %d writers waiting); continuing the release path.",
        state, waitingReaders, waitingWriters
    );
}

struct SharedReadLockStackContext {
    CLockStatusStack* stack;
    const wchar_t* description;
    int line;
    std::uint64_t address;
    std::uint8_t lockType;
};

void MarkSharedReadLockEntered(void* opaque) {
    auto& context = *static_cast<SharedReadLockStackContext*>(opaque);
    context.stack->LockEntered(context.description, context.line, context.address, context.lockType);
}

void RearmSharedReadLock(void* opaque) {
    auto& context = *static_cast<SharedReadLockStackContext*>(opaque);
    context.stack->Unlock(
        0, 0, context.description, static_cast<std::uint32_t>(context.line), context.address, context.lockType
    );
    context.stack->Lock(0, 0, context.description, context.line, context.address, context.lockType);
}

bool ApplyReadLockState(CSharedRWLock::SHAREDINFO& info, bool concession, std::uint32_t threadId) {
    bool wait = concession ? info.m_nWaitingWriters != 0 || info.m_nActive < 0 : info.m_nActive < 0;
    if (wait && info.m_nActive < 0 && info.m_dwWritingThreadID == threadId) {
        --info.m_nActive;
        wait = false;
    } else if (wait) {
        ++info.m_nWaitingReaders;
        ++info.m_nWaitingReadOnly;
    } else {
        ++info.m_nActive;
        ++info.m_nReadOnly;
    }
    return wait;
}

bool ApplyWriteLockState(CSharedRWLock::SHAREDINFO& info, std::uint32_t threadId) {
    const bool wait = info.m_nActive > 0 || (info.m_nActive < 0 && info.m_dwWritingThreadID != threadId);
    if (wait) {
        ++info.m_nWaitingWriters;
    } else {
        --info.m_nActive;
        info.m_dwWritingThreadID = threadId;
    }
    return wait;
}

enum class SharedLockWake {
    None,
    Writer,
    Readers,
};

struct SharedLockWakeRequest {
    SharedLockWake target = SharedLockWake::None;
    std::uint32_t count = 0;
};

SharedLockWakeRequest ApplyDoneState(CSharedRWLock::SHAREDINFO& info) {
    if (info.m_nActive < 1) {
        if (info.m_nActive < 0) {
            ++info.m_nActive;
        } else {
            // ReportCleanUnlock()
            ReportCleanUnlock(&info, static_cast<int>(info.m_nWaitingReaders), static_cast<int>(info.m_nWaitingWriters));
        }
    } else {
        --info.m_nActive;
        --info.m_nReadOnly;
    }

    SharedLockWakeRequest wake;
    if (info.m_nActive == 0) {
        if (info.m_nWaitingWriters > 0) {
            info.m_nActive = -1;
            --info.m_nWaitingWriters;
            wake.target = SharedLockWake::Writer;
            wake.count = 1;
        } else if (info.m_nWaitingReaders > 0) {
            info.m_nActive = info.m_nWaitingReaders;
            info.m_nReadOnly = info.m_nWaitingReadOnly;
            info.m_nWaitingReaders = 0;
            info.m_nWaitingReadOnly = 0;
            wake.target = SharedLockWake::Readers;
            wake.count = static_cast<std::uint32_t>(info.m_nActive);
        }
        info.m_dwWritingThreadID = 0;
    }
    return wake;
}

} // namespace

class CenteredSharedRWLockBackend::Impl {
public:
    Impl(std::string logicalName, std::size_t lockCount, std::uint32_t spinCount) :
        logicalName_(std::move(logicalName)), lockCount_(lockCount), spinCount_(spinCount) {}

    ~Impl() {
#if defined(_WIN32)
        windowsSlots_.clear();
        if (sharedView_) {
            UnmapViewOfFile(sharedView_);
        }
        if (sharedHandle_) {
            CloseHandle(sharedHandle_);
        }
#else
        if (sharedView_ != MAP_FAILED) {
            munmap(sharedView_, sharedByteSize_);
        }
        if (sharedDescriptor_ >= 0) {
            close(sharedDescriptor_);
        }
        if (unlinkSharedMemoryOnDestroy_ && !posixSharedMemoryName_.empty()) {
            shm_unlink(posixSharedMemoryName_.c_str());
        }
        if (ownerDescriptor_ >= 0) {
            flock(ownerDescriptor_, LOCK_UN);
            close(ownerDescriptor_);
        }
#endif
    }

    bool CreateSharedMemory(const std::string& mappingName, std::string* error) {
        if (mappingName.empty()) {
            runtime::SetErrorText(error, "centered shared-lock mapping name is empty");
            return false;
        }

        std::lock_guard<std::mutex> guard(stateMutex_);
        if (HasSharedMemoryUnlocked()) {
            if (mappingName == sharedMappingName_) {
                if (error) {
                    error->clear();
                }
                return true;
            }
            runtime::SetErrorText(
                error,
                "a different centered shared-lock mapping "
                "is already attached"
            );
            return false;
        }
#if defined(_WIN32)
        const std::wstring wideName = ::Utf8ToWideStrict(runtime::ScopedSharedMemoryName(mappingName));
        if (wideName.empty()) {
            runtime::SetErrorText(error, "invalid UTF-8 centered shared-lock name");
            return false;
        }
        const std::uint64_t extent = static_cast<std::uint64_t>(lockCount_ * sizeof(WindowsSharedInfo));
        HANDLE handle = CreateFileMappingW(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, static_cast<DWORD>(extent >> 32U), static_cast<DWORD>(extent),
            wideName.c_str()
        );
        if (!handle) {
            runtime::SetErrorText(
                error, "CreateFileMappingW(" + mappingName + ") failed: " + std::to_string(GetLastError())
            );
            return false;
        }
        void* view = MapViewOfFile(handle, FILE_MAP_WRITE, 0, 0, 0);
        if (!view) {
            const DWORD code = GetLastError();
            CloseHandle(handle);
            runtime::SetErrorText(error, "MapViewOfFile(" + mappingName + ") failed: " + std::to_string(code));
            return false;
        }
        sharedHandle_ = handle;
        sharedView_ = view;
        windowsInfo_ = static_cast<WindowsSharedInfo*>(view);
#else
        const std::string posixName = runtime::PosixSharedMemoryName(mappingName);
        const std::size_t expected = PosixRegionBytes(lockCount_);
        std::string ownerPath = "/tmp/l2_sharedrwlock_";
        ownerPath.append(posixName.begin() + 1, posixName.end());
        ownerPath.append(".lock");
        int ownerDescriptor = open(ownerPath.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (ownerDescriptor < 0) {
            runtime::SetErrorText(error, "open(" + ownerPath + ") failed: " + std::strerror(errno));
            return false;
        }

        bool ownsMappingName = true;
        if (flock(ownerDescriptor, LOCK_EX | LOCK_NB) != 0) {
            const int code = errno;
            close(ownerDescriptor);
            ownerDescriptor = -1;
            if (code == EWOULDBLOCK || code == EAGAIN) {
                ownsMappingName = false;
            } else {
                runtime::SetErrorText(error, "flock(" + ownerPath + ") failed: " + std::strerror(code));
                return false;
            }
        }

        if (ownsMappingName && shm_unlink(posixName.c_str()) != 0 && errno != ENOENT) {
            const int code = errno;
            flock(ownerDescriptor, LOCK_UN);
            close(ownerDescriptor);
            runtime::SetErrorText(error, "shm_unlink(" + posixName + ") failed: " + std::strerror(code));
            return false;
        }

        int descriptor = ownsMappingName ? shm_open(posixName.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600)
                                         : shm_open(posixName.c_str(), O_RDWR, 0);
        const bool created = ownsMappingName && descriptor >= 0;
        if (created) {
            if (ftruncate(descriptor, static_cast<off_t>(expected)) != 0) {
                const int code = errno;
                close(descriptor);
                shm_unlink(posixName.c_str());
                flock(ownerDescriptor, LOCK_UN);
                close(ownerDescriptor);
                runtime::SetErrorText(error, "ftruncate(" + posixName + ") failed: " + std::strerror(code));
                return false;
            }
        }
        if (descriptor < 0) {
            const int code = errno;
            if (ownerDescriptor >= 0) {
                flock(ownerDescriptor, LOCK_UN);
                close(ownerDescriptor);
            }
            runtime::SetErrorText(error, "shm_open(" + posixName + ") failed: " + std::strerror(code));
            return false;
        }

        struct stat status{};
        if (fstat(descriptor, &status) != 0 || status.st_size < static_cast<off_t>(expected)) {
            const int code = errno;
            close(descriptor);
            if (created) {
                shm_unlink(posixName.c_str());
            }
            if (ownerDescriptor >= 0) {
                flock(ownerDescriptor, LOCK_UN);
                close(ownerDescriptor);
            }
            runtime::SetErrorText(
                error, "centered shared-lock mapping " + posixName + " has an incompatible extent: " + std::strerror(code)
            );
            return false;
        }
        void* view = mmap(nullptr, expected, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
        if (view == MAP_FAILED) {
            const int code = errno;
            close(descriptor);
            if (created) {
                shm_unlink(posixName.c_str());
            }
            if (ownerDescriptor >= 0) {
                flock(ownerDescriptor, LOCK_UN);
                close(ownerDescriptor);
            }
            runtime::SetErrorText(error, "mmap(" + posixName + ") failed: " + std::strerror(code));
            return false;
        }

        if (created) {
            std::memset(view, 0, expected);
            auto* const header = static_cast<PosixLockHeader*>(view);
            header->magic = kPosixLockMagic;
            header->version = kPosixLockVersion;
            header->lockCount = static_cast<std::uint32_t>(lockCount_);
            header->spinCount = spinCount_;

            pthread_mutexattr_t mutexAttributes{};
            pthread_condattr_t conditionAttributes{};
            CheckPthreadResult(pthread_mutexattr_init(&mutexAttributes), "pthread_mutexattr_init");
            CheckPthreadResult(
                pthread_mutexattr_setpshared(&mutexAttributes, PTHREAD_PROCESS_SHARED), "pthread_mutexattr_setpshared"
            );
            CheckPthreadResult(pthread_condattr_init(&conditionAttributes), "pthread_condattr_init");
            CheckPthreadResult(
                pthread_condattr_setpshared(&conditionAttributes, PTHREAD_PROCESS_SHARED), "pthread_condattr_setpshared"
            );

            PosixSharedLock* const locks = PosixLocks(view);
            for (std::size_t index = 0; index < lockCount_; ++index) {
                CheckPthreadResult(pthread_mutex_init(&locks[index].stateMutex, &mutexAttributes), "pthread_mutex_init");
                CheckPthreadResult(
                    pthread_cond_init(&locks[index].readers, &conditionAttributes), "pthread_cond_init(readers)"
                );
                CheckPthreadResult(
                    pthread_cond_init(&locks[index].writers, &conditionAttributes), "pthread_cond_init(writers)"
                );
                locks[index].info.m_dwSpinCount = spinCount_;
            }
            pthread_condattr_destroy(&conditionAttributes);
            pthread_mutexattr_destroy(&mutexAttributes);
            __atomic_store_n(&header->ready, UINT32_C(1), __ATOMIC_RELEASE);
        } else {
            const auto* const header = static_cast<const PosixLockHeader*>(view);
            const std::uint32_t ready = __atomic_load_n(&header->ready, __ATOMIC_ACQUIRE);
            if (header->magic != kPosixLockMagic || header->version != kPosixLockVersion ||
                header->lockCount != lockCount_ || header->spinCount != spinCount_ || ready != 1) {
                munmap(view, expected);
                close(descriptor);
                runtime::SetErrorText(error, "centered shared-lock mapping " + posixName + " has an incompatible header");
                return false;
            }
        }

        sharedDescriptor_ = descriptor;
        sharedView_ = view;
        sharedByteSize_ = expected;
        posixLocks_ = PosixLocks(view);
        posixSharedMemoryName_ = posixName;
        unlinkSharedMemoryOnDestroy_ = created;
        ownerDescriptor_ = ownerDescriptor;
#endif

        sharedMappingName_ = mappingName;
        if (error) {
            error->clear();
        }
        return true;
    }

    bool AttachSharedMemory(const std::string& mappingName, std::string* error) {
        if (mappingName.empty()) {
            runtime::SetErrorText(error, "centered shared-lock mapping name is empty");
            return false;
        }

        std::lock_guard<std::mutex> guard(stateMutex_);
        if (HasSharedMemoryUnlocked()) {
            if (mappingName == sharedMappingName_) {
                if (error) {
                    error->clear();
                }
                return true;
            }
            runtime::SetErrorText(
                error,
                "a different centered shared-lock mapping "
                "is already attached"
            );
            return false;
        }
#if defined(_WIN32)
        const std::wstring wideName = ::Utf8ToWideStrict(runtime::ScopedSharedMemoryName(mappingName));
        if (wideName.empty()) {
            runtime::SetErrorText(error, "invalid UTF-8 centered shared-lock name");
            return false;
        }
        HANDLE handle = OpenFileMappingW(FILE_MAP_WRITE, FALSE, wideName.c_str());
        if (!handle) {
            runtime::SetErrorText(
                error, "OpenFileMappingW(" + mappingName + ") failed: " + std::to_string(GetLastError())
            );
            return false;
        }
        void* view = MapViewOfFile(handle, FILE_MAP_WRITE, 0, 0, 0);
        if (!view) {
            const DWORD code = GetLastError();
            CloseHandle(handle);
            runtime::SetErrorText(error, "MapViewOfFile(" + mappingName + ") failed: " + std::to_string(code));
            return false;
        }
        sharedHandle_ = handle;
        sharedView_ = view;
        windowsInfo_ = static_cast<WindowsSharedInfo*>(view);
#else
        const std::string posixName = runtime::PosixSharedMemoryName(mappingName);
        const int descriptor = shm_open(posixName.c_str(), O_RDWR, 0);
        if (descriptor < 0) {
            runtime::SetErrorText(error, "shm_open(" + posixName + ") failed: " + std::strerror(errno));
            return false;
        }
        struct stat status{};
        if (fstat(descriptor, &status) != 0) {
            const int code = errno;
            close(descriptor);
            runtime::SetErrorText(error, "fstat(" + posixName + ") failed: " + std::strerror(code));
            return false;
        }
        const std::size_t expected = PosixRegionBytes(lockCount_);
        if (status.st_size < static_cast<off_t>(expected)) {
            close(descriptor);
            runtime::SetErrorText(
                error,
                "centered shared-lock mapping " + posixName + " is smaller than " + std::to_string(expected) + " bytes"
            );
            return false;
        }
        void* view = mmap(nullptr, expected, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
        if (view == MAP_FAILED) {
            const int code = errno;
            close(descriptor);
            runtime::SetErrorText(error, "mmap(" + posixName + ") failed: " + std::strerror(code));
            return false;
        }
        const auto* header = static_cast<const PosixLockHeader*>(view);
        const std::uint32_t ready = __atomic_load_n(&header->ready, __ATOMIC_ACQUIRE);
        if (header->magic != kPosixLockMagic || header->version != kPosixLockVersion || header->lockCount != lockCount_ ||
            header->spinCount != spinCount_ || ready != 1) {
            munmap(view, expected);
            close(descriptor);
            runtime::SetErrorText(error, "centered shared-lock mapping " + posixName + " has an incompatible header");
            return false;
        }
        sharedDescriptor_ = descriptor;
        sharedView_ = view;
        sharedByteSize_ = expected;
        posixLocks_ = PosixLocks(view);
#endif

        sharedMappingName_ = mappingName;
        if (error) {
            error->clear();
        }
        return true;
    }

    bool HasSharedMemory() const {
        std::lock_guard<std::mutex> guard(stateMutex_);
        return HasSharedMemoryUnlocked();
    }

    void ReadLock(
        std::size_t slot,
        bool concession,
        CenteredSharedRWLockBackend::ReadLockTransition entered,
        CenteredSharedRWLockBackend::ReadLockTransition rearm,
        void* context
    ) {
        ValidateSlot(slot);
#if defined(_WIN32)
        WindowsSharedInfo* info = nullptr;
        {
            std::lock_guard<std::mutex> guard(stateMutex_);
            info = windowsInfo_;
        }
        if (info) {
            WindowsReadLock(slot, info + slot, concession, entered, rearm, context);
            return;
        }
#else
        PosixSharedLock* lock = nullptr;
        {
            std::lock_guard<std::mutex> guard(stateMutex_);
            if (posixLocks_) {
                lock = posixLocks_ + slot;
            }
        }
        if (lock) {
            PosixReadLock(*lock, concession, entered, rearm, context);
            return;
        }
#endif
        throw std::logic_error("centered shared-lock mapping is not attached");
    }

    void WriteLock(std::size_t slot) {
        ValidateSlot(slot);
#if defined(_WIN32)
        WindowsSharedInfo* info = nullptr;
        {
            std::lock_guard<std::mutex> guard(stateMutex_);
            info = windowsInfo_;
        }
        if (info) {
            WindowsWriteLock(slot, info + slot);
            return;
        }
#else
        PosixSharedLock* lock = nullptr;
        {
            std::lock_guard<std::mutex> guard(stateMutex_);
            if (posixLocks_) {
                lock = posixLocks_ + slot;
            }
        }
        if (lock) {
            PosixWriteLock(*lock);
            return;
        }
#endif
        throw std::logic_error("centered shared-lock mapping is not attached");
    }

    void Done(std::size_t slot, bool writeLock) {
        ValidateSlot(slot);
#if defined(_WIN32)
        WindowsSharedInfo* info = nullptr;
        {
            std::lock_guard<std::mutex> guard(stateMutex_);
            info = windowsInfo_;
        }
        if (info) {
            WindowsDone(slot, info + slot);
            return;
        }
#else
        PosixSharedLock* lock = nullptr;
        {
            std::lock_guard<std::mutex> guard(stateMutex_);
            if (posixLocks_) {
                lock = posixLocks_ + slot;
            }
        }
        if (lock) {
            PosixDone(*lock);
            return;
        }
#endif
        throw std::logic_error("centered shared-lock mapping is not attached");
    }

    void* SharedInfo(std::size_t slot) {
        ValidateSlot(slot);
#if defined(_WIN32)
        WindowsSharedInfo* info = nullptr;
        {
            std::lock_guard<std::mutex> guard(stateMutex_);
            if (windowsInfo_) {
                info = windowsInfo_ + slot;
            }
        }
        if (info) {
            return static_cast<void*>(info);
        }
#else
        PosixSharedLock* lock = nullptr;
        {
            std::lock_guard<std::mutex> guard(stateMutex_);
            if (posixLocks_) {
                lock = posixLocks_ + slot;
            }
        }
        if (lock) {
            return static_cast<void*>(&lock->info);
        }
#endif
        throw std::logic_error("centered shared-lock mapping is not attached");
    }

    void PrepareSlot(std::size_t slot) {
        ValidateSlot(slot);
#if defined(_WIN32)
        GetWindowsSlot(slot);
#endif
    }

    void ReleaseSlot(std::size_t slot) {
        ValidateSlot(slot);
#if defined(_WIN32)
        std::lock_guard<std::mutex> guard(windowsSlotsMutex_);
        windowsSlots_[slot].reset();
#endif
    }

private:
    bool HasSharedMemoryUnlocked() const {
#if defined(_WIN32)
        return windowsInfo_ != nullptr;
#else
        return posixLocks_ != nullptr;
#endif
    }

    void ValidateSlot(std::size_t slot) const {
        if (slot >= lockCount_) {
            throw std::out_of_range("centered shared-lock slot is out of range");
        }
    }

#if !defined(_WIN32)
    void PosixReadLock(
        PosixSharedLock& lock,
        bool concession,
        CenteredSharedRWLockBackend::ReadLockTransition entered,
        CenteredSharedRWLockBackend::ReadLockTransition rearm,
        void* context
    ) {
        bool wait = false;
        {
            PthreadMutexGuard guard(&lock.stateMutex);
            if (entered) {
                entered(context);
            }
            wait = ApplyReadLockState(lock.info, concession, CurrentNativeThreadId());
        }
        if (rearm) {
            rearm(context);
        }
        if (!wait) {
            return;
        }

        PthreadMutexGuard guard(&lock.stateMutex);
        while (lock.readerPermits == 0) {
            CheckPthreadResult(pthread_cond_wait(&lock.readers, guard.Get()), "pthread_cond_wait(readers)");
        }
        --lock.readerPermits;
    }

    void PosixWriteLock(PosixSharedLock& lock) {
        const std::uint32_t threadId = CurrentNativeThreadId();
        PthreadMutexGuard guard(&lock.stateMutex);
        const bool wait = ApplyWriteLockState(lock.info, threadId);
        while (wait && lock.writerPermits == 0) {
            CheckPthreadResult(pthread_cond_wait(&lock.writers, guard.Get()), "pthread_cond_wait(writers)");
        }
        if (wait) {
            --lock.writerPermits;
            lock.info.m_dwWritingThreadID = threadId;
        }
    }

    void PosixDone(PosixSharedLock& lock) {
        SharedLockWakeRequest wake;
        {
            PthreadMutexGuard guard(&lock.stateMutex);
            wake = ApplyDoneState(lock.info);
            if (wake.target == SharedLockWake::Writer) {
                ++lock.writerPermits;
            } else if (wake.target == SharedLockWake::Readers) {
                lock.readerPermits += wake.count;
            }
        }
        if (wake.target == SharedLockWake::Writer) {
            CheckPthreadResult(pthread_cond_signal(&lock.writers), "pthread_cond_signal(writers)");
        } else if (wake.target == SharedLockWake::Readers) {
            CheckPthreadResult(pthread_cond_broadcast(&lock.readers), "pthread_cond_broadcast(readers)");
        }
    }
#endif

#if defined(_WIN32)
    struct WindowsSlot {
        HANDLE event = nullptr;
        HANDLE readers = nullptr;
        HANDLE writers = nullptr;

        ~WindowsSlot() {
            if (event) {
                CloseHandle(event);
            }
            if (readers) {
                CloseHandle(readers);
            }
            if (writers) {
                CloseHandle(writers);
            }
        }
    };

    WindowsSlot& GetWindowsSlot(std::size_t slot) {
        std::lock_guard<std::mutex> guard(windowsSlotsMutex_);
        std::unique_ptr<WindowsSlot>& handles = windowsSlots_[slot];
        if (handles) {
            return *handles;
        }

        auto created = std::make_unique<WindowsSlot>();
        const std::string suffix = logicalName_ + "_" + std::to_string(slot);
        const std::wstring eventName = ::Utf8ToWideStrict(
            runtime::ScopedSharedMemoryName("L2SharedLock_Event_" + suffix));
        const std::wstring readerName = ::Utf8ToWideStrict(
            runtime::ScopedSharedMemoryName("L2SharedLock_SemReader_" + suffix));
        const std::wstring writerName = ::Utf8ToWideStrict(
            runtime::ScopedSharedMemoryName("L2SharedLock_SemWriter_" + suffix));
        created->event = CreateEventW(nullptr, FALSE, FALSE, eventName.c_str());
        created->readers =
            CreateSemaphoreW(nullptr, 0, std::numeric_limits<LONG>::max(), readerName.c_str());
        created->writers =
            CreateSemaphoreW(nullptr, 0, std::numeric_limits<LONG>::max(), writerName.c_str());
        // CreateEvent/CreateSemaphore failures are kept
        // as null handles. L2NPC.exe does not check these three calls in
        // CSharedRWLock::CSharedRWLock. A later wait/release operation sees
        // the matching Win32 failure directly.
        handles = std::move(created);
        return *handles;
    }

    bool WindowsTryEnter(WindowsSharedInfo* info) {
        const DWORD threadId = GetCurrentThreadId();
        DWORD spins = static_cast<DWORD>(info->spinCount);
        for (;;) {
            const LONG previous = InterlockedCompareExchange(&info->lockCount, 1, 0);
            if (previous == 0) {
                info->threadId = threadId;
                info->recurseCount = 1;
                return true;
            }
            if (info->threadId == threadId) {
                InterlockedIncrement(&info->lockCount);
                ++info->recurseCount;
                return true;
            }
            if (spins == 0) {
                return false;
            }
            --spins;
            YieldProcessor();
        }
    }

    void WindowsEnter(std::size_t slot, WindowsSharedInfo* info) {
        if (WindowsTryEnter(info)) {
            return;
        }
        const DWORD threadId = GetCurrentThreadId();
        const LONG previous = InterlockedIncrement(&info->lockCount) - 1;
        if (previous == 0) {
            info->threadId = threadId;
            info->recurseCount = 1;
            return;
        }
        if (info->threadId == threadId) {
            ++info->recurseCount;
            return;
        }
        WaitForSingleObject(GetWindowsSlot(slot).event, INFINITE);
        info->threadId = threadId;
        info->recurseCount = 1;
    }

    void WindowsLeave(std::size_t slot, WindowsSharedInfo* info) {
        --info->recurseCount;
        if (info->recurseCount < 1) {
            info->threadId = 0;
            const LONG remaining = InterlockedDecrement(&info->lockCount);
            if (remaining > 0) {
                SetEvent(GetWindowsSlot(slot).event);
            }
        } else {
            InterlockedDecrement(&info->lockCount);
        }
    }

    void WindowsReadLock(
        std::size_t slot,
        WindowsSharedInfo* info,
        bool concession,
        CenteredSharedRWLockBackend::ReadLockTransition entered,
        CenteredSharedRWLockBackend::ReadLockTransition rearm,
        void* context
    ) {
        WindowsEnter(slot, info);
        if (entered) {
            entered(context);
        }
        bool wait = concession ? info->waitingWriters != 0 || info->active < 0 : info->active < 0;
        if (wait && info->active < 0 && info->writingThreadId == GetCurrentThreadId()) {
            --info->active;
            wait = false;
        } else if (wait) {
            ++info->waitingReaders;
            ++info->waitingReadOnly;
        } else {
            ++info->active;
            ++info->readOnly;
        }
        WindowsLeave(slot, info);
        if (rearm) {
            rearm(context);
        }
        if (wait) {
            WaitForSingleObject(GetWindowsSlot(slot).readers, INFINITE);
        }
    }

    void WindowsWriteLock(std::size_t slot, WindowsSharedInfo* info) {
        WindowsEnter(slot, info);
        const DWORD threadId = GetCurrentThreadId();
        const bool wait = info->active > 0 || (info->active < 0 && info->writingThreadId != threadId);
        if (wait) {
            ++info->waitingWriters;
        } else {
            --info->active;
            info->writingThreadId = threadId;
        }
        WindowsLeave(slot, info);
        if (wait) {
            WaitForSingleObject(GetWindowsSlot(slot).writers, INFINITE);
            info->writingThreadId = threadId;
        }
    }

    void WindowsDone(std::size_t slot, WindowsSharedInfo* info) {
        WindowsEnter(slot, info);
        HANDLE wake = nullptr;
        LONG releaseCount = 1;
        if (info->active < 1) {
            if (info->active < 0) {
                ++info->active;
            } else {
                // ReportCleanUnlock()
                ReportCleanUnlock(info, static_cast<int>(info->waitingReaders), static_cast<int>(info->waitingWriters));
            }
        } else {
            --info->active;
            --info->readOnly;
        }
        if (info->active == 0) {
            WindowsSlot& handles = GetWindowsSlot(slot);
            if (info->waitingWriters > 0) {
                info->active = -1;
                --info->waitingWriters;
                wake = handles.writers;
            } else if (info->waitingReaders > 0) {
                info->active = info->waitingReaders;
                info->readOnly = info->waitingReadOnly;
                info->waitingReaders = 0;
                info->waitingReadOnly = 0;
                wake = handles.readers;
                releaseCount = info->active;
            }
            info->writingThreadId = 0;
        }
        WindowsLeave(slot, info);
        if (wake) {
            ReleaseSemaphore(wake, releaseCount, nullptr);
        }
    }
#endif

    std::string logicalName_;
    std::size_t lockCount_;
    std::uint32_t spinCount_;
    mutable std::mutex stateMutex_;
    std::string sharedMappingName_;

#if defined(_WIN32)
    HANDLE sharedHandle_ = nullptr;
    void* sharedView_ = nullptr;
    WindowsSharedInfo* windowsInfo_ = nullptr;
    std::mutex windowsSlotsMutex_;
    std::vector<std::unique_ptr<WindowsSlot>> windowsSlots_{lockCount_};
#else
    int sharedDescriptor_ = -1;
    void* sharedView_ = MAP_FAILED;
    std::size_t sharedByteSize_ = 0;
    PosixSharedLock* posixLocks_ = nullptr;
    std::string posixSharedMemoryName_;
    bool unlinkSharedMemoryOnDestroy_ = false;
    int ownerDescriptor_ = -1;
#endif
};

CenteredSharedRWLockBackend::CenteredSharedRWLockBackend(
    const wchar_t* logicalName, std::size_t lockCount, std::uint32_t spinCount
) :
    impl_(std::make_unique<Impl>(NarrowLogicalName(logicalName), lockCount, spinCount)) {
    const std::string mappingName = "L2CenteredSharedLock_MMF_" + NarrowLogicalName(logicalName);
    std::string error;
#if defined(L2_BUILD_L2SERVER)
    const bool bound = impl_->CreateSharedMemory(mappingName, &error);
#else
    const bool bound = impl_->AttachSharedMemory(mappingName, &error);
#endif
    if (!bound) {
        throw std::runtime_error(error);
    }
}

CenteredSharedRWLockBackend::CenteredSharedRWLockBackend(
    const wchar_t* logicalName,
    std::size_t lockCount,
    std::uint32_t spinCount,
    const std::string& mappingName,
    bool createMapping
) :
    impl_(std::make_unique<Impl>(NarrowLogicalName(logicalName), lockCount, spinCount)) {
    std::string error;
    const bool bound =
        createMapping ? impl_->CreateSharedMemory(mappingName, &error) : impl_->AttachSharedMemory(mappingName, &error);
    if (!bound) {
        throw std::runtime_error(error);
    }
}

CenteredSharedRWLockBackend::~CenteredSharedRWLockBackend() = default;

bool CenteredSharedRWLockBackend::AttachSharedMemory(const std::string& mappingName, std::string* error) {
    return impl_->AttachSharedMemory(mappingName, error);
}

bool CenteredSharedRWLockBackend::HasSharedMemory() const { return impl_->HasSharedMemory(); }

void CenteredSharedRWLockBackend::ReadLock(
    std::size_t slot, bool concession, ReadLockTransition entered, ReadLockTransition rearm, void* context
) {
    impl_->ReadLock(slot, concession, entered, rearm, context);
}

void CenteredSharedRWLockBackend::WriteLock(std::size_t slot) { impl_->WriteLock(slot); }

void CenteredSharedRWLockBackend::Done(std::size_t slot, bool writeLock) { impl_->Done(slot, writeLock); }

void* CenteredSharedRWLockBackend::SharedInfo(std::size_t slot) { return impl_->SharedInfo(slot); }

void CenteredSharedRWLockBackend::PrepareSlot(std::size_t slot) { impl_->PrepareSlot(slot); }

void CenteredSharedRWLockBackend::ReleaseSlot(std::size_t slot) { impl_->ReleaseSlot(slot); }

CenteredSharedRWLockBackend* portable::SharedRWLockTransportAccess::Backend(const CSharedRWLock& lock) {
    return static_cast<CenteredSharedRWLockBackend*>(lock.m_hfm);
}

std::size_t portable::SharedRWLockTransportAccess::Slot(const CSharedRWLock& lock) {
    const std::uintptr_t encoded = reinterpret_cast<std::uintptr_t>(lock.m_hevt);
    return encoded == 0 ? 0 : static_cast<std::size_t>(encoded - 1);
}

CSharedRWLock::CSharedRWLock(const wchar_t*) {
    L2_GUARD_FUNCTION();
    // The exact shared source body (also compiled by NASC64_2015) performs
    // these stores after the guard. They are not member initializers.
    m_hfm = nullptr;
    m_hevt = nullptr;
    m_hsemReaders = nullptr;
    m_hsemWriters = nullptr;
    m_psi = nullptr;
    private_psi = false;
}

CSharedRWLock::CSharedRWLock(const wchar_t* name, std::uint32_t number, PSHAREDINFO reservedInfo, std::uint32_t spinCount) {
    L2_GUARD_FUNCTION();

    // Current L2Server.exe 0x00a54fb8 and L2NPC.exe 0x00533cd0 create the
    // named event/semaphores here, then either create/map the exact
    // L2SharedLock_MMF_<name>_<number> SHAREDINFO or adopt reserved_psi.
    // Portable synchronization occupies the target's opaque handle slots.
    m_hfm = nullptr;
    m_hevt = nullptr;
    m_hsemReaders = nullptr;
    m_hsemWriters = nullptr;
    m_psi = nullptr;
    if (!reservedInfo) {
        auto* const backend = new CenteredSharedRWLockBackend(
            name, 1, spinCount, "L2SharedLock_MMF_" + NarrowLogicalName(name) + "_" + std::to_string(number), true
        );
        portable::SharedRWLockTransportAccess::Bind(*this, backend, 0);
        backend->PrepareSlot(0);
        m_psi = static_cast<PSHAREDINFO>(backend->SharedInfo(0));
        private_psi = true;
        m_psi->m_dwSpinCount = spinCount;
        m_psi->m_nWaitingReaders = 0;
        m_psi->m_nWaitingReadOnly = 0;
        m_psi->m_nWaitingWriters = 0;
        m_psi->m_nActive = 0;
        m_psi->m_nReadOnly = 0;
    } else {
        m_psi = reservedInfo;
        private_psi = false;
    }
}

CSharedRWLock::~CSharedRWLock() {
    L2_GUARD_FUNCTION();
    if (CenteredSharedRWLockBackend* const backend = portable::SharedRWLockTransportAccess::Backend(*this)) {
        backend->ReleaseSlot(portable::SharedRWLockTransportAccess::Slot(*this));
        if (private_psi) {
            delete backend;
        }
    } else if (private_psi) {
        delete m_psi;
    }
    m_psi = nullptr;
    m_hfm = nullptr;
    m_hevt = nullptr;
}

void portable::SharedRWLockTransportAccess::Bind(
    CSharedRWLock& lock, CenteredSharedRWLockBackend* backend, std::size_t slot
) {
    lock.m_hfm = backend;
    lock.m_hevt = reinterpret_cast<void*>(static_cast<std::uintptr_t>(slot + 1));
}

void CSharedRWLock::ReadLock(const wchar_t* callerFile, int callerLine, bool concession) {
    L2_GUARD_FUNCTION();
    CenteredSharedRWLockBackend* const backend = portable::SharedRWLockTransportAccess::Backend(*this);
    const std::size_t slot = portable::SharedRWLockTransportAccess::Slot(*this);
    if (backend) {
        m_psi = static_cast<PSHAREDINFO>(backend->SharedInfo(slot));
    }
    const std::uint64_t address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this));
    constexpr std::uint8_t kReadLock = 3;
    CLockStatusStack& lockStack = g_LockStack[static_cast<std::size_t>(CThreadManager::GetThreadID())];
    lockStack.Lock(0, 0, callerFile, callerLine, address, kReadLock);
    if (!backend) {
        // The target enters its raw SHAREDINFO lock here and therefore faults
        // after publishing the unentered lock-stack node when m_psi is null.
        CrashImpl();
    }
    SharedReadLockStackContext lockContext{&lockStack, callerFile, callerLine, address, kReadLock};
    backend->ReadLock(slot, concession, &MarkSharedReadLockEntered, &RearmSharedReadLock, &lockContext);
    lockStack.LockEntered(callerFile, callerLine, address, kReadLock);
}

void CSharedRWLock::WriteLock(const wchar_t* callerFile, int callerLine) {
    L2_GUARD_FUNCTION();
    CenteredSharedRWLockBackend* const backend = portable::SharedRWLockTransportAccess::Backend(*this);
    const std::size_t slot = portable::SharedRWLockTransportAccess::Slot(*this);
    if (backend) {
        m_psi = static_cast<PSHAREDINFO>(backend->SharedInfo(slot));
    }
    const std::uint64_t address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this));
    constexpr std::uint8_t kWriteLock = 2;
    CLockStatusStack& lockStack = g_LockStack[static_cast<std::size_t>(CThreadManager::GetThreadID())];
    lockStack.Lock(0, 0, callerFile, callerLine, address, kWriteLock);
    if (!backend) {
        CrashImpl();
    }
    backend->WriteLock(slot);
    lockStack.LockEntered(callerFile, callerLine, address, kWriteLock);
}

void CSharedRWLock::Done(const wchar_t* callerFile, int callerLine, bool writeLock) {
    L2_GUARD_FUNCTION();
    CenteredSharedRWLockBackend* const backend = portable::SharedRWLockTransportAccess::Backend(*this);
    const std::size_t slot = portable::SharedRWLockTransportAccess::Slot(*this);
    if (backend) {
        m_psi = static_cast<PSHAREDINFO>(backend->SharedInfo(slot));
    }
    const wchar_t* const description = callerFile ? callerFile : L"CSharedRWLock Done";
    const std::uint64_t address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this));
    g_LockStack[static_cast<std::size_t>(CThreadManager::GetThreadID())].Unlock(
        0, 0, description, static_cast<std::uint32_t>(callerLine), address, writeLock ? 2 : 3
    );
    if (!backend) {
        // Shipping Done has already popped the caller's lock-stack entry
        // before its raw SHAREDINFO access fails.
        CrashImpl();
    }
    backend->Done(slot, writeLock);
}

void CSharedRWLock::WriteUnlock(const wchar_t* callerFile, int callerLine) { Done(callerFile, callerLine, true); }

void CSharedRWLock::ReadUnlock(const wchar_t* callerFile, int callerLine) { Done(callerFile, callerLine, false); }

void CSharedRWLock::SetSpinCount(std::uint32_t spinCount) {
    L2_GUARD_FUNCTION();
    if (CenteredSharedRWLockBackend* const backend = portable::SharedRWLockTransportAccess::Backend(*this)) {
        m_psi = static_cast<PSHAREDINFO>(backend->SharedInfo(portable::SharedRWLockTransportAccess::Slot(*this)));
    }
    if (m_psi) {
        runtime::raw_atomic::StoreU32Release(&m_psi->m_dwSpinCount, spinCount);
    }
}

int CSharedRWLock::TryEnter() {
    if (!m_psi) {
        return 0;
    }
    const std::uint32_t threadId = CurrentNativeThreadId();
    std::uint32_t spins = m_psi->m_dwSpinCount;
    do {
        std::int32_t expected = 0;
        if (runtime::raw_atomic::CompareExchangeI32AcqRel(&m_psi->m_lLockCount, expected, 1)) {
            m_psi->m_dwThreadID = threadId;
            m_psi->m_lRecurseCount = 1;
            return 1;
        }
        if (m_psi->m_dwThreadID == threadId) {
            runtime::raw_atomic::AddI32AcqRel(&m_psi->m_lLockCount, 1);
            ++m_psi->m_lRecurseCount;
            return 1;
        }
        std::this_thread::yield();
    } while (spins-- != 0);
    return 0;
}

void CSharedRWLock::Enter() {
    while (!TryEnter()) {
        std::this_thread::yield();
    }
}

void CSharedRWLock::Leave() {
    if (!m_psi) {
        return;
    }
    --m_psi->m_lRecurseCount;
    if (m_psi->m_lRecurseCount < 1) {
        m_psi->m_dwThreadID = 0;
    }
    runtime::raw_atomic::AddI32AcqRel(&m_psi->m_lLockCount, -1);
}
