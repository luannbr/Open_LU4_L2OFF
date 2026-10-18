#include "Portable/CreatureSharedMemory.h"

#include "Shared/src/Runtime/ErrorText.h"
#include "Shared/src/Runtime/SharedMemoryName.h"
#include "Shared/src/Runtime/SharedFactoryMapping.h"
#include "TextRuntime.h"

#include <cerrno>
#include <cstring>
#include <memory>

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

constexpr std::size_t kCreatureCount = 120000;
constexpr std::size_t kCreatureDataBytes = kCreatureCount * sizeof(CSharedCreatureData);
constexpr std::size_t kCreatureIndexBytes = kCreatureCount * sizeof(std::int32_t);

static_assert(kCreatureDataBytes == 1930560000ULL);
static_assert(kCreatureIndexBytes == 480000ULL);

#ifdef _WIN32

struct MappingLifetime {
    HANDLE dataHandle = nullptr;
    HANDLE indexHandle = nullptr;
    void* dataView = nullptr;
    void* indexView = nullptr;

    ~MappingLifetime() {
        UnmapViewOfFile(indexView);
        CloseHandle(indexHandle);
        UnmapViewOfFile(dataView);
        CloseHandle(dataHandle);
    }
};

#else

std::string PosixName(const std::string& value) {
    const std::string scoped = runtime::ScopedSharedMemoryName(value);
    if (scoped.empty() || scoped.front() == '/') {
        return scoped;
    }
    return "/" + scoped;
}

struct MappingLifetime {
    int dataDescriptor = -1;
    int indexDescriptor = -1;
    void* dataView = MAP_FAILED;
    void* indexView = MAP_FAILED;
    std::size_t dataBytes = 0;
    std::size_t indexBytes = 0;

    ~MappingLifetime() {
        if (indexView != MAP_FAILED) {
            munmap(indexView, indexBytes);
        }
        if (dataView != MAP_FAILED) {
            munmap(dataView, dataBytes);
        }
        if (indexDescriptor >= 0) {
            close(indexDescriptor);
        }
        if (dataDescriptor >= 0) {
            close(dataDescriptor);
        }
    }
};

bool GetCompleteSize(int descriptor, const std::string& name, std::size_t& bytes, std::string* error) {
    struct stat status{};
    if (fstat(descriptor, &status) != 0) {
        runtime::SetErrorText(error, "fstat(" + name + ") failed: " + std::strerror(errno));
        return false;
    }
    if (status.st_size <= 0) {
        runtime::SetErrorText(error, "shared mapping " + name + " has no mappable extent");
        return false;
    }
    bytes = static_cast<std::size_t>(status.st_size);
    return true;
}

#endif

} // namespace

bool AttachCreatureSharedMemory(
    CCreatureFactory& factory, const std::string& dataName, const std::string& indexName, std::string* error
) {
    if (dataName.empty() || indexName.empty()) {
        runtime::SetErrorText(error, "creature data/index shared-memory name is empty");
        return false;
    }

    auto mapping = std::make_shared<MappingLifetime>();
    bool dataOpened = false;
    bool indexOpened = false;

#ifdef _WIN32
    const std::wstring wideData = ::Utf8ToWideStrict(runtime::ScopedSharedMemoryName(dataName));
    const std::wstring wideIndex = ::Utf8ToWideStrict(runtime::ScopedSharedMemoryName(indexName));
    if (wideData.empty() || wideIndex.empty()) {
        runtime::SetErrorText(error, "invalid UTF-8 creature shared-memory name");
        return false;
    }

    const auto openView = [error](const std::string& name, const std::wstring& wideName, HANDLE& handle, void*& view) {
        handle = OpenFileMappingW(FILE_MAP_WRITE, FALSE, wideName.c_str());
        if (!handle) {
            runtime::SetErrorText(error, "OpenFileMappingW(" + name + ") failed: " + std::to_string(GetLastError()));
        }
        view = MapViewOfFile(handle, FILE_MAP_WRITE, 0, 0, 0);
        if (!view) {
            runtime::SetErrorText(error, "MapViewOfFile(" + name + ") failed: " + std::to_string(GetLastError()));
            return false;
        }
        return true;
    };
    dataOpened = openView(dataName, wideData, mapping->dataHandle, mapping->dataView);
    indexOpened = openView(indexName, wideIndex, mapping->indexHandle, mapping->indexView);
#else
    const std::string posixData = PosixName(dataName);
    const std::string posixIndex = PosixName(indexName);
    const auto openView = [error](const std::string& name, int& descriptor, void*& view, std::size_t& bytes) {
        descriptor = shm_open(name.c_str(), O_RDWR, 0);
        if (descriptor < 0) {
            runtime::SetErrorText(error, "shm_open(" + name + ") failed: " + std::strerror(errno));
            return false;
        }
        if (!GetCompleteSize(descriptor, name, bytes, error)) {
            return false;
        }
        view = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
        if (view == MAP_FAILED) {
            runtime::SetErrorText(error, "mmap(" + name + ") failed: " + std::strerror(errno));
            return false;
        }
        return true;
    };
    dataOpened = openView(posixData, mapping->dataDescriptor, mapping->dataView, mapping->dataBytes);
    indexOpened = openView(posixIndex, mapping->indexDescriptor, mapping->indexView, mapping->indexBytes);
#endif

    const bool published = runtime::SharedFactoryMappingAccess::Attach(
        factory, dataOpened ? mapping->dataView : nullptr, dataOpened ? kCreatureDataBytes : 0,
        indexOpened ? mapping->indexView : nullptr, indexOpened ? kCreatureIndexBytes : 0, mapping, error
    );
    return dataOpened && indexOpened && published;
}


namespace runtime {

bool OpenDefaultCreatureSharedFactory(CCreatureFactory& factory) { return AttachCreatureSharedMemory(factory); }

} // namespace runtime
