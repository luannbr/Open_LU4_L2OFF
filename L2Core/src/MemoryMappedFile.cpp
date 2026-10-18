#include "L2Core/src/inc/MemoryMappedFile.h"
#include "Shared/src/Runtime/OperatorLog.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

volatile std::int64_t g_MemMapAlloc = 0;

namespace {

constexpr std::size_t kKernelObjectNameCapacity = 1024;

std::wstring ChangeKernelObjectName(
    wchar_t* lpName) {
    // Current L2NPC.exe 0x00551d68, neighboring PDB:
    // `anonymous namespace'::ChangeKernelObjectName.
    std::wstring mutexName{L"MUTEX_OF_"};
    if (lpName) {
        mutexName.append(lpName);
    }
    std::replace(
        mutexName.begin(),
        mutexName.end(),
        L'\\',
        L'_');

    std::wstring prefixed{L"Global\\"};
    prefixed.append(mutexName);

    // Both target swprintf_s calls use wchar_t[1024]. Shipping names are far
    // below this limit. Retaining the fixed boundary also prevents a portable
    // build from accepting a name the target rejects.
    if (mutexName.size() >=
            kKernelObjectNameCapacity ||
        prefixed.size() >=
            kKernelObjectNameCapacity) {
        std::abort();
    }
    return prefixed;
}

[[noreturn]] void ReportDuplicateMapping(
    const std::wstring& mutexName) {
    // This may run while CLog itself is creating its mappings. Use the
    // independent console sink instead of recursively entering CLog.
    try {
        // runtime::logging::Write("", runtime::logging::Level::Error,
        //     "[shared memory] Mapping ownership guard \"" + WideToUtf8(mutexName) +
        //     "\" is already in use, stopping this process.")
        runtime::logging::Write("", runtime::logging::Level::Error,
            "[shared memory] Mapping ownership guard \"" + WideToUtf8(mutexName) +
            "\" is already in use; stopping this process.", __FILE__);
    } catch (...) {
        // runtime::logging::Write("", runtime::logging::Level::Error,
        //     "[shared memory] Mapping ownership guard is already in use, stopping this process. Guard name could not be formatted.")
        runtime::logging::Write("", runtime::logging::Level::Error,
            "[shared memory] Mapping ownership guard is already in use; stopping this process. Guard name could not be formatted.", __FILE__);
    }
#if defined(_WIN32)
    wchar_t message[
        kKernelObjectNameCapacity]{};
    ::swprintf_s(
        message,
        L"MMF[%s] is already used",
        mutexName.c_str());
    ::MessageBoxW(
        nullptr,
        message,
        L"Error",
        0);
#else
    // std::fwprintf(
    //     stderr,
    //     L"MMF[%ls] is already used\n",
    //     mutexName.c_str())
    // std::fflush(stderr)
#endif
    std::exit(-1);
}

void AddMappedBytes(
    unsigned long lowPart) {
#if defined(_WIN32)
    ::InterlockedExchangeAdd64(
        reinterpret_cast<volatile LONG64*>(
            &g_MemMapAlloc),
        static_cast<LONG64>(
            static_cast<std::uint32_t>(
                lowPart)));
#else
    __sync_fetch_and_add(
        &g_MemMapAlloc,
        static_cast<std::int64_t>(
            static_cast<std::uint32_t>(
                lowPart)));
#endif
}

#if !defined(_WIN32)

std::string PosixObjectName(
    const wchar_t* name,
    const char* fallback) {
    std::string result{"/"};
    if (!name || *name == L'\0') {
        result.append(fallback);
        return result;
    }

    for (const wchar_t* cursor = name;
         *cursor != L'\0';
         ++cursor) {
        const wchar_t value = *cursor;
        if ((value >= L'a' &&
             value <= L'z') ||
            (value >= L'A' &&
             value <= L'Z') ||
            (value >= L'0' &&
             value <= L'9') ||
            value == L'_' ||
            value == L'-') {
            result.push_back(
                static_cast<char>(value));
        } else {
            result.push_back('_');
        }
    }
    return result;
}

std::string PosixLockPath(
    const std::wstring& mutexName) {
    std::string path{"/tmp/l2_mmf_"};
    const std::string objectName =
        PosixObjectName(
            mutexName.c_str(),
            "MMF");
    // PosixObjectName starts with the slash required by shm_open. A lock
    // file is a normal filesystem object, so retaining that slash here would
    // accidentally require a nested /tmp directory that the target never
    // creates and leave the mapping without a live-owner marker.
    path.append(objectName.begin() + 1, objectName.end());
    path.append(".lock");
    return path;
}

std::mutex& OwnerHandleMutex() {
    static std::mutex mutex;
    return mutex;
}

std::vector<int>& OwnerHandles() {
    // The Windows function leaves a successful named-mutex
    // handle alive until process teardown. Holding these descriptors has the
    // same lifetime, while flock releases ownership automatically after a
    // crash.
    static std::vector<int> handles;
    return handles;
}

void RetainOwnerHandle(int descriptor) {
    std::lock_guard<std::mutex> guard(
        OwnerHandleMutex());
    OwnerHandles().push_back(descriptor);
}

#endif

}  // namespace

void* L2CreateFileMapping(
    void* hFile,
    _SECURITY_ATTRIBUTES* lpAttributes,
    unsigned long flProtect,
    unsigned long dwMaximumSizeHigh,
    unsigned long dwMaximumSizeLow,
    wchar_t* lpName) {
    // Current L2NPC.exe 0x00551e60.
    const std::wstring mutexName =
        ChangeKernelObjectName(lpName);

#if defined(_WIN32)
    HANDLE mutex = ::CreateMutexW(
        nullptr,
        TRUE,
        mutexName.c_str());
    if (::GetLastError() ==
        ERROR_ALREADY_EXISTS) {
        ReportDuplicateMapping(mutexName);
    }

    HANDLE mapping = ::CreateFileMappingW(
        static_cast<HANDLE>(hFile),
        reinterpret_cast<
            LPSECURITY_ATTRIBUTES>(
            lpAttributes),
        static_cast<DWORD>(flProtect),
        static_cast<DWORD>(
            dwMaximumSizeHigh),
        static_cast<DWORD>(
            dwMaximumSizeLow),
        lpName);
    if (!mapping) {
        // The target calls CloseHandle even when CreateMutexW returned null.
        // Besides matching control flow, that keeps its last-error side
        // effect on this failure path.
        ::CloseHandle(mutex);
    } else {
        AddMappedBytes(
            dwMaximumSizeLow);
    }
    return mapping;
#else

    const std::string mappingName =
        PosixObjectName(
            lpName,
            "L2NPC_MMF");

    int ownerDescriptor = ::open(
        PosixLockPath(mutexName).c_str(),
        O_CREAT | O_RDWR | O_CLOEXEC,
        0600);
    if (ownerDescriptor >= 0 &&
        ::flock(
            ownerDescriptor,
            LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK ||
            errno == EAGAIN) {
            ::close(ownerDescriptor);
            ReportDuplicateMapping(
                mutexName);
        }
        ::close(ownerDescriptor);
        ownerDescriptor = -1;
    }

    if (ownerDescriptor >= 0) {
        // A Win32 named mapping vanishes when its last kernel handle closes.
        // POSIX shm names survive a clean exit or crash until shm_unlink, so
        // an unowned name is stale transport state, not an existing mapping
        // that CreateFileMappingW would have reopened. The live-owner flock
        // above makes this unlink race-free for cooperating server processes.
        if (::shm_unlink(mappingName.c_str()) != 0 &&
            errno != ENOENT) {
            ::close(ownerDescriptor);
            return nullptr;
        }
    }

    const std::uint64_t mappingSize =
        (static_cast<std::uint64_t>(
             static_cast<std::uint32_t>(
                 dwMaximumSizeHigh))
         << 32U) |
        static_cast<std::uint32_t>(
            dwMaximumSizeLow);
    int descriptor = -1;
    if (mappingSize != 0) {
        descriptor = ::shm_open(
            mappingName.c_str(),
            O_CREAT | O_RDWR,
            0600);
    }
    if (descriptor >= 0 &&
        ::ftruncate(
            descriptor,
            static_cast<off_t>(
                mappingSize)) != 0) {
        ::close(descriptor);
        descriptor = -1;
    }

    if (descriptor < 0) {
        if (ownerDescriptor >= 0) {
            ::close(ownerDescriptor);
        }
        return nullptr;
    }

    if (ownerDescriptor >= 0) {
        RetainOwnerHandle(
            ownerDescriptor);
    }
    AddMappedBytes(
        dwMaximumSizeLow);
    return reinterpret_cast<void*>(
        static_cast<std::intptr_t>(
            descriptor + 1));
#endif
}
