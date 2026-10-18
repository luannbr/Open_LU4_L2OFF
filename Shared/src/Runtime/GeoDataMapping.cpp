#include "Shared/src/Runtime/GeoDataMapping.h"
#include "Shared/src/Runtime/PosixSharedMemoryName.h"

#include <cstdlib>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <utility>

#if defined(_WIN32)
#include "TextRuntime.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace l2geo::portable::detail {
namespace {

#if !defined(_WIN32)
class PosixSharedNameRegistry {
public:
    void Retain(const std::string& name) {
        std::lock_guard<std::mutex> guard(lock_);
        ++references_[name];
    }

    void Release(const std::string& name) noexcept {
        bool unlink = false;
        {
            std::lock_guard<std::mutex> guard(lock_);
            const auto found = references_.find(name);
            if (found == references_.end()) {
                return;
            }
            if (--found->second == 0) {
                references_.erase(found);
                unlink = true;
            }
        }
        if (unlink) {
            ::shm_unlink(name.c_str());
        }
    }

    void Cleanup() noexcept {
        std::unordered_map<std::string, std::size_t> names;
        {
            std::lock_guard<std::mutex> guard(lock_);
            names.swap(references_);
        }
        for (const auto& entry : names) {
            ::shm_unlink(entry.first.c_str());
        }
    }

private:
    std::mutex lock_;
    std::unordered_map<std::string, std::size_t> references_;
};

PosixSharedNameRegistry*& PosixSharedNameRegistrySlot() {
    static PosixSharedNameRegistry* registry = nullptr;
    return registry;
}

void CleanupPosixSharedNames() {
    PosixSharedNameRegistry*& registry = PosixSharedNameRegistrySlot();
    if (!registry) {
        return;
    }
    registry->Cleanup();
    delete registry;
    registry = nullptr;
}

void RetainPosixSharedName(const std::string& name) {
    PosixSharedNameRegistry*& registry = PosixSharedNameRegistrySlot();
    if (!registry) {
        registry = new PosixSharedNameRegistry;
        std::atexit(CleanupPosixSharedNames);
    }
    registry->Retain(name);
}

void ReleasePosixSharedName(const std::string& name) noexcept {
    PosixSharedNameRegistry* const registry = PosixSharedNameRegistrySlot();
    if (registry) {
        registry->Release(name);
    }
}
#endif

} // namespace

MappedFile::~MappedFile() { Reset(); }

MappedFile::MappedFile(MappedFile&& other) noexcept { Swap(other); }

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        Reset();
        Swap(other);
    }
    return *this;
}

bool MappedFile::Open(const std::filesystem::path& path, std::string& error) {
    Reset();
#if defined(_WIN32)
    HANDLE file =
        ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "cannot open " + path.string() + " (Win32 error " + std::to_string(::GetLastError()) + ")";
        return false;
    }
    LARGE_INTEGER length{};
    if (!::GetFileSizeEx(file, &length) || length.QuadPart <= 0 ||
        static_cast<unsigned long long>(length.QuadPart) > std::numeric_limits<std::size_t>::max()) {
        error = "invalid mapped-file length for " + path.string();
        ::CloseHandle(file);
        return false;
    }
    HANDLE mapping = ::CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mapping) {
        error = "cannot map " + path.string() + " (Win32 error " + std::to_string(::GetLastError()) + ")";
        ::CloseHandle(file);
        return false;
    }
    void* view = ::MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    ::CloseHandle(mapping);
    ::CloseHandle(file);
    if (!view) {
        error =
            "cannot create a read-only view of " + path.string() + " (Win32 error " + std::to_string(::GetLastError()) + ")";
        return false;
    }
    data_ = static_cast<const std::uint8_t*>(view);
    size_ = static_cast<std::size_t>(length.QuadPart);
#else
    const int descriptor = ::open(path.c_str(), O_RDONLY);
    if (descriptor < 0) {
        error = "cannot open " + path.string() + ": " + std::strerror(errno);
        return false;
    }
    struct stat info{};
    if (::fstat(descriptor, &info) != 0 || info.st_size <= 0 ||
        static_cast<unsigned long long>(info.st_size) > std::numeric_limits<std::size_t>::max()) {
        error = "invalid mapped-file length for " + path.string();
        ::close(descriptor);
        return false;
    }
    void* view = ::mmap(nullptr, static_cast<std::size_t>(info.st_size), PROT_READ, MAP_PRIVATE, descriptor, 0);
    const int mapError = errno;
    ::close(descriptor);
    if (view == MAP_FAILED) {
        error = "cannot map " + path.string() + ": " + std::strerror(mapError);
        return false;
    }
    data_ = static_cast<const std::uint8_t*>(view);
    size_ = static_cast<std::size_t>(info.st_size);
#endif
    return true;
}

bool MappedFile::OpenNamed(const std::string& name, std::size_t bytes, std::string& error, bool requestAllAccess) {
    Reset();
    if (name.empty() || bytes == 0) {
        error = "invalid geodata shared-memory mapping request";
        return false;
    }
#if defined(_WIN32)
    const std::wstring wideName = ::Utf8ToWideStrict(runtime::ScopedSharedMemoryName(name));
    if (wideName.empty()) {
        error = "invalid UTF-8 geodata shared-memory name";
        return false;
    }
    const DWORD access = requestAllAccess ? FILE_MAP_ALL_ACCESS : FILE_MAP_READ;
    HANDLE mapping = ::OpenFileMappingW(access, FALSE, wideName.c_str());
    if (!mapping) {
        error = "OpenFileMappingW(" + name + ") failed: " + std::to_string(::GetLastError());
        return false;
    }
    void* view = ::MapViewOfFile(mapping, access, 0, 0, 0);
    const DWORD mapError = ::GetLastError();
    ::CloseHandle(mapping);
    if (!view) {
        error = "MapViewOfFile(" + name + ") failed: " + std::to_string(mapError);
        return false;
    }
    MEMORY_BASIC_INFORMATION mappingInfo{};
    if (::VirtualQuery(view, &mappingInfo, sizeof(mappingInfo)) == 0 || mappingInfo.RegionSize < bytes) {
        ::UnmapViewOfFile(view);
        error = "shared mapping " + name + " is smaller than " + std::to_string(bytes) + " bytes";
        return false;
    }
    data_ = static_cast<const std::uint8_t*>(view);
    // OpenMMF maps the complete named object (dwNumberOfBytesToMap == 0).
    size_ = mappingInfo.RegionSize;
#else
    const std::string posixName = runtime::PosixSharedMemoryName(name);
    const int descriptor = ::shm_open(posixName.c_str(), requestAllAccess ? O_RDWR : O_RDONLY, 0);
    if (descriptor < 0) {
        error = "shm_open(" + posixName + ") failed: " + std::strerror(errno);
        return false;
    }
    struct stat info{};
    if (::fstat(descriptor, &info) != 0) {
        const int statusError = errno;
        ::close(descriptor);
        error = "fstat(" + posixName + ") failed: " + std::strerror(statusError);
        return false;
    }
    if (info.st_size < static_cast<off_t>(bytes)) {
        ::close(descriptor);
        error = "shared mapping " + posixName + " is smaller than " + std::to_string(bytes) + " bytes";
        return false;
    }
    const std::size_t mappedBytes = static_cast<std::size_t>(info.st_size);
    void* view =
        ::mmap(nullptr, mappedBytes, requestAllAccess ? PROT_READ | PROT_WRITE : PROT_READ, MAP_SHARED, descriptor, 0);
    const int mapError = errno;
    ::close(descriptor);
    if (view == MAP_FAILED) {
        error = "mmap(" + posixName + ") failed: " + std::strerror(mapError);
        return false;
    }
    data_ = static_cast<const std::uint8_t*>(view);
    size_ = mappedBytes;
#endif
    return true;
}

bool MappedFile::CreateNamed(const std::string& name, std::size_t bytes, std::string& error) {
    Reset();
    if (name.empty() || bytes == 0) {
        error = "invalid geodata shared-memory creation request";
        return false;
    }
#if defined(_WIN32)
    const std::wstring wideName = ::Utf8ToWideStrict(runtime::ScopedSharedMemoryName(name));
    if (wideName.empty()) {
        error = "invalid UTF-8 geodata shared-memory name";
        return false;
    }
    HANDLE mapping = ::CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, static_cast<DWORD>(static_cast<std::uint64_t>(bytes) >> 32),
        static_cast<DWORD>(static_cast<std::uint64_t>(bytes) & 0xffffffffu), wideName.c_str()
    );
    if (!mapping) {
        error = "CreateFileMappingW(" + name + ") failed: " + std::to_string(::GetLastError());
        return false;
    }
    void* view = ::MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, bytes);
    const DWORD mapError = ::GetLastError();
    if (!view) {
        ::CloseHandle(mapping);
        error = "MapViewOfFile(" + name + ") failed: " + std::to_string(mapError);
        return false;
    }
    // Keep the handle: closing the last handle removes the name, so a peer
    // process could no longer open this mapping by name.
    ownedMappingHandle_ = mapping;
    data_ = static_cast<const std::uint8_t*>(view);
    size_ = bytes;
#else
    const std::string posixName = runtime::PosixSharedMemoryName(name);
    bool created = false;
    int descriptor = ::shm_open(posixName.c_str(), O_CREAT | O_EXCL | O_RDWR, 0660);
    if (descriptor >= 0) {
        created = true;
    } else if (errno == EEXIST) {
        descriptor = ::shm_open(posixName.c_str(), O_RDWR, 0);
    }
    if (descriptor < 0) {
        error = "shm_open(" + posixName + ") failed: " + std::strerror(errno);
        return false;
    }

    if (created) {
        if (::ftruncate(descriptor, static_cast<off_t>(bytes)) != 0) {
            const int truncateError = errno;
            ::close(descriptor);
            ::shm_unlink(posixName.c_str());
            error = "ftruncate(" + posixName + ") failed: " + std::strerror(truncateError);
            return false;
        }
    } else {
        struct stat info{};
        if (::fstat(descriptor, &info) != 0) {
            const int statusError = errno;
            ::close(descriptor);
            error = "fstat(" + posixName + ") failed: " + std::strerror(statusError);
            return false;
        }
        if (info.st_size < static_cast<off_t>(bytes)) {
            ::close(descriptor);
            error = "existing shared mapping " + posixName + " is smaller than " + std::to_string(bytes) + " bytes";
            return false;
        }
    }

    void* view = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
    const int mapError = errno;
    ::close(descriptor);
    if (view == MAP_FAILED) {
        if (created) {
            ::shm_unlink(posixName.c_str());
        }
        error = "mmap(" + posixName + ") failed: " + std::strerror(mapError);
        return false;
    }
    data_ = static_cast<const std::uint8_t*>(view);
    size_ = bytes;
    // CreateFileMapping does not let a process that merely opened an
    // existing object delete that producer's name when its own view closes.
    if (created) {
        RetainPosixSharedName(posixName);
        ownedSharedName_ = posixName;
    }
#endif
    return true;
}

void MappedFile::Reset() {
    if (data_) {
#if defined(_WIN32)
        ::UnmapViewOfFile(data_);
#else
        ::munmap(const_cast<std::uint8_t*>(data_), size_);
#endif
    }
    data_ = nullptr;
    size_ = 0;
#if defined(_WIN32)
    if (ownedMappingHandle_) {
        ::CloseHandle(static_cast<HANDLE>(ownedMappingHandle_));
        ownedMappingHandle_ = nullptr;
    }
#endif
#if !defined(_WIN32)
    if (!ownedSharedName_.empty()) {
        ReleasePosixSharedName(ownedSharedName_);
    }
#endif
    ownedSharedName_.clear();
}

const std::uint8_t* MappedFile::Data() const { return data_; }

std::uint8_t* MappedFile::MutableData() { return const_cast<std::uint8_t*>(data_); }

std::size_t MappedFile::Size() const { return size_; }

void MappedFile::Swap(MappedFile& other) noexcept {
    std::swap(data_, other.data_);
    std::swap(size_, other.size_);
    std::swap(ownedSharedName_, other.ownedSharedName_);
    std::swap(ownedMappingHandle_, other.ownedMappingHandle_);
}

CSharedGeoZone* PortableMappedZone::SharedZoneData() {
    if (sharedZone.Data()) {
        return reinterpret_cast<CSharedGeoZone*>(sharedZone.MutableData());
    }
    return ownedZone.get();
}

CGeoCell* PortableMappedZone::CellData() {
    if (sharedCells.Data()) {
        return reinterpret_cast<CGeoCell*>(sharedCells.MutableData());
    }
    return ownedCells.empty() ? nullptr : ownedCells.data();
}

std::int16_t* PortableMappedZone::CellOffsetData() {
    if (sharedCellOffsets.Data()) {
        return reinterpret_cast<std::int16_t*>(sharedCellOffsets.MutableData());
    }
    return ownedCellOffsets.empty() ? nullptr : ownedCellOffsets.data();
}

CGeoDataIndexStruct* PortableGeoIndex::Data() {
    if (mapping.Data()) {
        return reinterpret_cast<CGeoDataIndexStruct*>(mapping.MutableData());
    }
    return owned.get();
}

} // namespace l2geo::portable::detail
