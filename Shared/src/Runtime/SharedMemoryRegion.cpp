#include "Shared/src/Runtime/SharedMemoryRegion.h"
#include "Shared/src/Runtime/ErrorText.h"
#include "Shared/src/Runtime/PosixSharedMemoryName.h"
#include "TextRuntime.h"

#include <cerrno>
#include <cstring>
#include <limits>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

#ifdef _WIN32

struct NamedMappingLifetime {
    HANDLE handle = nullptr;
    void* view = nullptr;

    ~NamedMappingLifetime() {
        if (view) {
            UnmapViewOfFile(view);
        }
        if (handle) {
            CloseHandle(handle);
        }
    }
};

#else

struct NamedMappingLifetime {
    int descriptor = -1;
    void* view = MAP_FAILED;
    std::size_t bytes = 0;
    std::string name;
    bool unlinkOnDestroy = false;

    ~NamedMappingLifetime() {
        if (view != MAP_FAILED) {
            munmap(view, bytes);
        }
        if (descriptor >= 0) {
            close(descriptor);
        }
        if (unlinkOnDestroy && !name.empty()) {
            shm_unlink(name.c_str());
        }
    }
};

#endif

} // namespace

bool SharedMemoryRegion::Contains(std::size_t offset, std::size_t count) const {
    return data_ && offset <= bytes_ && count <= bytes_ - offset;
}

bool SharedMemoryRegion::AttachView(SharedMemoryViewSpec view, std::size_t minimumBytes, std::string* error) {
    if (!view.data) {
        runtime::SetErrorText(error, "shared-memory view is null");
        return false;
    }
    if (view.bytes < minimumBytes) {
        runtime::SetErrorText(error, "shared-memory view is smaller than " + std::to_string(minimumBytes) + " bytes");
        return false;
    }

    nativeHandle_ = view.nativeHandle;
    data_ = view.data;
    bytes_ = view.bytes;
    lifetime_ = std::move(view.lifetime);
    return true;
}

bool SharedMemoryRegion::AttachNamed(const std::string& name, std::size_t minimumBytes, std::string* error) {
    if (name.empty()) {
        runtime::SetErrorText(error, "shared-memory name is empty");
        return false;
    }
    if (minimumBytes == 0) {
        runtime::SetErrorText(error, "shared-memory minimum size is zero");
        return false;
    }

    auto mapping = std::make_shared<NamedMappingLifetime>();
#ifdef _WIN32
    const std::wstring wideName = ::Utf8ToWideStrict(runtime::ScopedSharedMemoryName(name));
    if (wideName.empty()) {
        runtime::SetErrorText(error, "invalid UTF-8 shared-memory name");
        return false;
    }
    mapping->handle = OpenFileMappingW(FILE_MAP_WRITE, FALSE, wideName.c_str());
    if (!mapping->handle) {
        runtime::SetErrorText(error, "OpenFileMappingW(" + name + ") failed: " + std::to_string(GetLastError()));
        return false;
    }
    mapping->view = MapViewOfFile(mapping->handle, FILE_MAP_WRITE, 0, 0, 0);
    if (!mapping->view) {
        runtime::SetErrorText(error, "MapViewOfFile(" + name + ") failed: " + std::to_string(GetLastError()));
        return false;
    }
    // OpenFileMappingW/MapViewOfFile in the target request a zero mapping
    // extent and never query or validate the producer's object size. The
    // view above therefore already has the target extent. This byte count
    // is only local metadata. Direct SharedType/CSharedVector consumers do
    // not use it to guard their accesses.
    const std::size_t mappedBytes = minimumBytes;
#else
    const std::string posixName = runtime::PosixSharedMemoryName(name);
    mapping->descriptor = shm_open(posixName.c_str(), O_RDWR, 0);
    if (mapping->descriptor < 0) {
        runtime::SetErrorText(error, "shm_open(" + posixName + ") failed: " + std::strerror(errno));
        return false;
    }

    struct stat status{};
    if (fstat(mapping->descriptor, &status) != 0) {
        runtime::SetErrorText(error, "fstat(" + posixName + ") failed: " + std::strerror(errno));
        return false;
    }
    if (status.st_size <= 0 || static_cast<unsigned long long>(status.st_size) > std::numeric_limits<std::size_t>::max()) {
        runtime::SetErrorText(error, "shared mapping " + posixName + " has an invalid extent");
        return false;
    }

    mapping->bytes = static_cast<std::size_t>(status.st_size);
    mapping->view = mmap(nullptr, mapping->bytes, PROT_READ | PROT_WRITE, MAP_SHARED, mapping->descriptor, 0);
    if (mapping->view == MAP_FAILED) {
        runtime::SetErrorText(error, "mmap(" + posixName + ") failed: " + std::strerror(errno));
        return false;
    }
    const std::size_t mappedBytes = mapping->bytes;
#endif

    return AttachView(
        SharedMemoryViewSpec{mapping->view, mappedBytes, mapping,
#ifdef _WIN32
            mapping->handle
#else
            nullptr
#endif
        },
        1, error
    );
}

bool SharedMemoryRegion::CreateNamed(const std::string& name, std::size_t bytes, std::string* error) {
    return CreateNamedImpl(name, nullptr, bytes, SharedMemoryCreateMode::NormalizedFullExtent, error);
}

bool SharedMemoryRegion::CreateNamed(
    const wchar_t* name, std::size_t bytes, SharedMemoryCreateMode mode, std::string* error
) {
    return CreateNamedImpl(::WideToUtf8(name ? name : L""), name, bytes, mode, error);
}

bool SharedMemoryRegion::CreateNamedImpl(
    const std::string& name,
    const wchar_t* suppliedWideName,
    std::size_t bytes,
    SharedMemoryCreateMode mode,
    std::string* error
) {
    const bool factory = mode == SharedMemoryCreateMode::LiteralRequestedExtent;
    if (suppliedWideName ? *suppliedWideName == L'\0' : name.empty()) {
        runtime::SetErrorText(error, factory ? "shared factory mapping name is empty" : "shared-memory name is empty");
        return false;
    }
    if (bytes == 0) {
        runtime::SetErrorText(error, factory ? "shared factory mapping extent is zero" : "shared-memory size is zero");
        return false;
    }

    auto mapping = std::make_shared<NamedMappingLifetime>();
#ifdef _WIN32
    const std::string scopedName = runtime::ScopedSharedMemoryName(name);
    const std::wstring wideName = scopedName != name ? ::Utf8ToWideStrict(scopedName)
        : suppliedWideName ? suppliedWideName : ::Utf8ToWideStrict(name);
    if (wideName.empty()) {
        runtime::SetErrorText(error, "invalid UTF-8 shared-memory name");
        return false;
    }
    const std::uint64_t extent = static_cast<std::uint64_t>(bytes);
    mapping->handle = CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, static_cast<DWORD>(extent >> 32U), static_cast<DWORD>(extent),
        wideName.c_str()
    );
    if (!mapping->handle) {
        runtime::SetErrorText(error, "CreateFileMappingW(" + name + ") failed: " + std::to_string(GetLastError()));
        return false;
    }
    mapping->view = MapViewOfFile(mapping->handle, FILE_MAP_WRITE, 0, 0, 0);
    if (!mapping->view) {
        runtime::SetErrorText(error, "MapViewOfFile(" + name + ") failed: " + std::to_string(GetLastError()));
        return false;
    }
    const std::size_t mappedBytes = bytes;
#else
    if (factory && name.empty()) {
        runtime::SetErrorText(error, "shared factory mapping name is invalid");
        return false;
    }
    const std::string scopedName = runtime::ScopedSharedMemoryName(name);
    const std::string posixName = factory
        ? (scopedName.front() == '/' ? scopedName : "/" + scopedName)
        : runtime::PosixSharedMemoryName(name);
    mapping->descriptor = shm_open(posixName.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    if (mapping->descriptor >= 0) {
        mapping->name = posixName;
        mapping->unlinkOnDestroy = true;
        if ((factory && bytes > static_cast<std::size_t>(std::numeric_limits<off_t>::max())) ||
            ftruncate(mapping->descriptor, static_cast<off_t>(bytes)) != 0) {
            runtime::SetErrorText(error, "ftruncate(" + posixName + ") failed: " + std::strerror(errno));
            return false;
        }
    } else if (errno == EEXIST) {
        mapping->descriptor = shm_open(posixName.c_str(), O_RDWR, 0);
    }
    if (mapping->descriptor < 0) {
        runtime::SetErrorText(error, "shm_open(" + posixName + ") failed: " + std::strerror(errno));
        return false;
    }

    // The factory producer checks only an existing object and maps the
    // requested size. Other region users keep the full-extent policy.
    std::size_t mappedExtent = bytes;
    if (!factory || !mapping->unlinkOnDestroy) {
        struct stat status{};
        if (fstat(mapping->descriptor, &status) != 0) {
            runtime::SetErrorText(error, "fstat(" + posixName + ") failed: " + std::strerror(errno));
            return false;
        }
        if (status.st_size < 0 || static_cast<std::uintmax_t>(status.st_size) < bytes ||
            (!factory && static_cast<unsigned long long>(status.st_size) > std::numeric_limits<std::size_t>::max())) {
            runtime::SetErrorText(
                error,
                factory ? "existing shared factory mapping " + posixName + " is smaller than the current contract"
                        : "shared mapping " + posixName + " has an incompatible extent"
            );
            return false;
        }
        if (!factory) {
            mappedExtent = static_cast<std::size_t>(status.st_size);
        }
    }
    mapping->bytes = mappedExtent;
    mapping->view = mmap(nullptr, mapping->bytes, PROT_READ | PROT_WRITE, MAP_SHARED, mapping->descriptor, 0);
    if (mapping->view == MAP_FAILED) {
        runtime::SetErrorText(error, "mmap(" + posixName + ") failed: " + std::strerror(errno));
        return false;
    }
    const std::size_t mappedBytes = mapping->bytes;
#endif

    return AttachView(
        SharedMemoryViewSpec{mapping->view, mappedBytes, mapping,
#ifdef _WIN32
            mapping->handle
#else
            nullptr
#endif
        },
        bytes, error
    );
}

void SharedMemoryRegion::Reset() {
    lifetime_.reset();
    nativeHandle_ = nullptr;
    data_ = nullptr;
    bytes_ = 0;
}

bool SharedMemoryRegion::ReadBytes(std::size_t offset, void* output, std::size_t count) const {
    if (!output || !Contains(offset, count)) {
        return false;
    }
    std::memcpy(output, static_cast<const std::uint8_t*>(data_) + offset, count);
    return true;
}

bool SharedMemoryRegion::WriteBytes(std::size_t offset, const void* input, std::size_t count) {
    if (!input || !Contains(offset, count)) {
        return false;
    }
    std::memcpy(static_cast<std::uint8_t*>(data_) + offset, input, count);
    return true;
}
