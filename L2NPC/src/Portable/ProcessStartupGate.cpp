#include "Portable/ProcessStartupGate.h"

#include <cerrno>
#include <cstring>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace l2npc::portable {
namespace {

enum class NamedMutexResult {
    Acquired,
    AlreadyExists,
    SystemError,
};

enum class NamedMarkerResult {
    Present,
    Missing,
    SystemError,
};

#if !defined(_WIN32)
std::string NamedObjectPath(const char* name) {
    std::string path{"/tmp/l2npc_process_"};
    for (const char* cursor = name; cursor && *cursor; ++cursor) {
        const unsigned char value = static_cast<unsigned char>(*cursor);
        const bool safe = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') || value == '_' || value == '-';
        path.push_back(safe ? static_cast<char>(value) : '_');
    }
    path.append(".lock");
    return path;
}
#endif

class NamedProcessMutex {
public:
    NamedProcessMutex() = default;

    ~NamedProcessMutex() {
#if defined(_WIN32)
        if (handle_) {
            ::CloseHandle(static_cast<HANDLE>(handle_));
        }
#else
        if (descriptor_ >= 0) {
            ::close(descriptor_);
        }
#endif
    }

    NamedProcessMutex(const NamedProcessMutex&) = delete;
    NamedProcessMutex& operator=(const NamedProcessMutex&) = delete;

    NamedMutexResult Acquire(const char* name, std::string* diagnostic) {
#if defined(_WIN32)
        HANDLE const handle = ::CreateMutexA(nullptr, TRUE, name);
        const DWORD error = ::GetLastError();
        if (error == ERROR_ALREADY_EXISTS) {
            if (handle) {
                ::CloseHandle(handle);
            }
            return NamedMutexResult::AlreadyExists;
        }
        if (!handle) {
            if (diagnostic) {
                *diagnostic = "CreateMutex failed for " + std::string(name) + " (error " + std::to_string(error) + ")";
            }
            return NamedMutexResult::SystemError;
        }
        handle_ = handle;
        return NamedMutexResult::Acquired;
#else
        const std::string path = NamedObjectPath(name);

        const int descriptor = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (descriptor < 0) {
            if (diagnostic) {
                *diagnostic = "open failed for " + path + ": " + std::strerror(errno);
            }
            return NamedMutexResult::SystemError;
        }
        if (::flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
            const int error = errno;
            ::close(descriptor);
            if (error == EWOULDBLOCK || error == EAGAIN) {
                return NamedMutexResult::AlreadyExists;
            }
            if (diagnostic) {
                *diagnostic = "flock failed for " + path + ": " + std::strerror(error);
            }
            return NamedMutexResult::SystemError;
        }
        descriptor_ = descriptor;
        return NamedMutexResult::Acquired;
#endif
    }

    void Release() {
#if defined(_WIN32)
        if (handle_) {
            ::ReleaseMutex(static_cast<HANDLE>(handle_));
            ::CloseHandle(static_cast<HANDLE>(handle_));
            handle_ = nullptr;
        }
#else
        if (descriptor_ >= 0) {
            ::close(descriptor_);
            descriptor_ = -1;
        }
#endif
    }

private:
#if defined(_WIN32)
    void* handle_ = nullptr;
#else
    int descriptor_ = -1;
#endif
};

class NamedProcessMarker {
public:
    NamedProcessMarker() = default;

    ~NamedProcessMarker() {
#if defined(_WIN32)
        if (handle_) {
            ::CloseHandle(static_cast<HANDLE>(handle_));
        }
#else
        if (descriptor_ >= 0) {
            ::close(descriptor_);
        }
#endif
    }

    NamedProcessMarker(const NamedProcessMarker&) = delete;
    NamedProcessMarker& operator=(const NamedProcessMarker&) = delete;

    NamedMarkerResult Open(const char* name, std::string* diagnostic) {
#if defined(_WIN32)
        HANDLE const handle = ::OpenMutexA(0, TRUE, name);
        if (handle) {
            handle_ = handle;
            return NamedMarkerResult::Present;
        }
        const DWORD error = ::GetLastError();
        if (error == ERROR_FILE_NOT_FOUND) {
            return NamedMarkerResult::Missing;
        }
        // Like the original, this asks for access 0. Windows refuses that for
        // an existing mutex with ERROR_ACCESS_DENIED, so the marker exists.
        if (error == ERROR_ACCESS_DENIED) {
            return NamedMarkerResult::Present;
        }
        if (diagnostic) {
            *diagnostic = "OpenMutex failed for " + std::string(name) + " (error " + std::to_string(error) + ")";
        }
        return NamedMarkerResult::SystemError;
#else
        const std::string path = NamedObjectPath(name);
        const int descriptor = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
        if (descriptor < 0) {
            if (errno == ENOENT) {
                return NamedMarkerResult::Missing;
            }
            if (diagnostic) {
                *diagnostic = "open failed for " + path + ": " + std::strerror(errno);
            }
            return NamedMarkerResult::SystemError;
        }

        if (::flock(descriptor, LOCK_EX | LOCK_NB) == 0) {
            // The file survived but no producer owns it. A Win32 named
            // mutex would already have disappeared in this state.
            ::flock(descriptor, LOCK_UN);
            ::close(descriptor);
            return NamedMarkerResult::Missing;
        }
        const int error = errno;
        if (error == EWOULDBLOCK || error == EAGAIN) {
            descriptor_ = descriptor;
            return NamedMarkerResult::Present;
        }
        ::close(descriptor);
        if (diagnostic) {
            *diagnostic = "flock probe failed for " + path + ": " + std::strerror(error);
        }
        return NamedMarkerResult::SystemError;
#endif
    }

private:
#if defined(_WIN32)
    void* handle_ = nullptr;
#else
    int descriptor_ = -1;
#endif
};

} // namespace

struct ProcessStartupGate::Impl {
    NamedProcessMutex process;
    NamedProcessMutex npcRemoval;
    NamedProcessMarker loaderMode;
    NamedProcessMarker sharedMemoryInitialized;
};

ProcessStartupGate::ProcessStartupGate() : impl_(std::make_unique<Impl>()) {}

ProcessStartupGate::~ProcessStartupGate() = default;

ProcessStartupGateResult ProcessStartupGate::Acquire(std::string* diagnostic) const {
    if (diagnostic) {
        diagnostic->clear();
    }

    if (const NamedMutexResult processResult = impl_->process.Acquire("Global\\_L2NPC_EXE_", diagnostic);
        processResult == NamedMutexResult::AlreadyExists) {
        return ProcessStartupGateResult::AlreadyRunning;
    }

    if (const NamedMutexResult removalResult = impl_->npcRemoval.Acquire("Global\\_L2SERVER_NPC_REMOVING_", diagnostic);
        removalResult == NamedMutexResult::AlreadyExists) {
        return ProcessStartupGateResult::NpcRemovalInProgress;
    }
    // This mutex is only a probe. The original L2NPC kept it for its whole
    // life, so when it closed its L2Server connection cleanly, L2Server's
    // NpcSocket::OnClose found the mutex still open, reported
    // "hNPCRemoving HANDLE is Duplicated" in a message box on an IO thread
    // and never cleaned up. (Ending L2NPC by force released it first.)
    impl_->npcRemoval.Release();
    return ProcessStartupGateResult::Ready;
}

L2ServerStartupState ProcessStartupGate::CheckL2ServerState(bool useResourceLoader, std::string* diagnostic) const {
    if (diagnostic) {
        diagnostic->clear();
    }

    const char* const loaderMarker = useResourceLoader ? "Global\\_L2MUTEX_USE_LOADER_" : "_L2MUTEX_NOT_USE_LOADER_";
    const NamedMarkerResult loaderResult = impl_->loaderMode.Open(loaderMarker, diagnostic);
    if (loaderResult == NamedMarkerResult::Missing) {
        return L2ServerStartupState::ResourceLoaderModeMismatch;
    }

    const NamedMarkerResult initializedResult =
        impl_->sharedMemoryInitialized.Open("_L2MUTEX_L2SERVER_IS_INITED_", diagnostic);
    if (initializedResult == NamedMarkerResult::Missing) {
        return L2ServerStartupState::SharedMemoryNotInitialized;
    }
    return L2ServerStartupState::Ready;
}

} // namespace l2npc::portable
