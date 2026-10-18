#include "Portable/SharedObjectSharedMemory.h"

#include "Shared/src/Runtime/ErrorText.h"
#include "Shared/src/Runtime/SharedMemoryName.h"
#include "Shared/src/Runtime/SharedFactoryMapping.h"
#include "TextRuntime.h"

#include <cerrno>
#include <cstring>
#include <functional>
#include <memory>
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

using AllianceFactory = CSharedFactory<CAlliance, CSharedAllianceData, CSharedAllianceConstant, &SharedDefine_Alliance>;

constexpr std::size_t kAirShipDataBytes = 1000 * sizeof(CSharedAirShipData);
constexpr std::size_t kAirShipConstantBytes = 100 * sizeof(CSharedAirShipConstant);
constexpr std::size_t kAirShipIndexBytes = 1000 * sizeof(std::int32_t);
constexpr std::size_t kPartyDataBytes = 10003 * sizeof(CSharedPartyData);
constexpr std::size_t kPartyIndexBytes = 10003 * sizeof(std::int32_t);
constexpr std::size_t kPledgeDataBytes = 10002 * sizeof(CSharedPledgeData);
constexpr std::size_t kPledgeIndexBytes = 10002 * sizeof(std::int32_t);
constexpr std::size_t kGeneralObjectDataBytes = 1020000 * sizeof(CSharedGeneralObjectData);
constexpr std::size_t kGeneralObjectIndexBytes = 1020000 * sizeof(std::int32_t);

static_assert(kGeneralObjectDataBytes == 285600000ULL);
static_assert(kGeneralObjectIndexBytes == 4080000ULL);
static_assert(kPledgeDataBytes == 20484096ULL);
static_assert(kPledgeIndexBytes == 40008ULL);

#ifdef _WIN32

struct MappingLifetime {
    HANDLE dataHandle = nullptr;
    HANDLE constantHandle = nullptr;
    HANDLE indexHandle = nullptr;
    void* dataView = nullptr;
    void* constantView = nullptr;
    void* indexView = nullptr;

    ~MappingLifetime() {
        // The folded CSharedFactory destructor at 0x004544a8 does not
        // release the optional constant mapping.
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
    int constantDescriptor = -1;
    int indexDescriptor = -1;
    void* dataView = MAP_FAILED;
    void* constantView = MAP_FAILED;
    void* indexView = MAP_FAILED;
    std::size_t dataBytes = 0;
    std::size_t constantBytes = 0;
    std::size_t indexBytes = 0;

    ~MappingLifetime() {
        // Preserve the target destructor's missing constant cleanup.
        if (indexView != MAP_FAILED) {
            munmap(indexView, indexBytes);
        }
        if (indexDescriptor >= 0) {
            close(indexDescriptor);
        }
        if (dataView != MAP_FAILED) {
            munmap(dataView, dataBytes);
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

using AttachViews = std::function<bool(void*, std::size_t, void*, std::size_t, std::shared_ptr<void>, std::string*)>;

bool AttachPair(
    const std::string& dataName,
    const std::string& indexName,
    std::size_t dataBytes,
    std::size_t indexBytes,
    const AttachViews& attach,
    std::string* error
) {
    if (dataName.empty() || indexName.empty()) {
        runtime::SetErrorText(error, "shared data/index mapping name is empty");
        return false;
    }

    auto mapping = std::make_shared<MappingLifetime>();
    bool dataOpened = false;
    bool indexOpened = false;

#ifdef _WIN32
    const std::wstring wideData = ::Utf8ToWideStrict(runtime::ScopedSharedMemoryName(dataName));
    const std::wstring wideIndex = ::Utf8ToWideStrict(runtime::ScopedSharedMemoryName(indexName));
    if (wideData.empty() || wideIndex.empty()) {
        runtime::SetErrorText(error, "invalid UTF-8 shared-memory name");
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

    const bool published = attach(
        dataOpened ? mapping->dataView : nullptr, dataOpened ? dataBytes : 0, indexOpened ? mapping->indexView : nullptr,
        indexOpened ? indexBytes : 0, mapping, error
    );
    return dataOpened && indexOpened && published;
}

bool AttachAirShipTriple(
    CAirShipFactory& factory,
    const std::string& dataName,
    const std::string& constantName,
    const std::string& indexName,
    std::string* error
) {
    if (dataName.empty() || constantName.empty() || indexName.empty()) {
        runtime::SetErrorText(error, "airship shared mapping name is empty");
        return false;
    }

    auto mapping = std::make_shared<MappingLifetime>();
    bool dataOpened = false;
    bool constantOpened = false;
    bool indexOpened = false;

#ifdef _WIN32
    const std::wstring wideData = ::Utf8ToWideStrict(runtime::ScopedSharedMemoryName(dataName));
    const std::wstring wideConstant = ::Utf8ToWideStrict(runtime::ScopedSharedMemoryName(constantName));
    const std::wstring wideIndex = ::Utf8ToWideStrict(runtime::ScopedSharedMemoryName(indexName));
    if (wideData.empty() || wideConstant.empty() || wideIndex.empty()) {
        runtime::SetErrorText(error, "invalid UTF-8 shared-memory name");
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
    constantOpened = openView(constantName, wideConstant, mapping->constantHandle, mapping->constantView);
    indexOpened = openView(indexName, wideIndex, mapping->indexHandle, mapping->indexView);
#else
    const std::string posixData = PosixName(dataName);
    const std::string posixConstant = PosixName(constantName);
    const std::string posixIndex = PosixName(indexName);

    const auto openView = [error](const std::string& name, int& descriptor, void*& view, std::size_t& mappedBytes) {
        descriptor = shm_open(name.c_str(), O_RDWR, 0);
        if (descriptor < 0) {
            runtime::SetErrorText(error, "shm_open(" + name + ") failed: " + std::strerror(errno));
            return false;
        }
        if (!GetCompleteSize(descriptor, name, mappedBytes, error)) {
            return false;
        }
        view = mmap(nullptr, mappedBytes, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
        if (view == MAP_FAILED) {
            runtime::SetErrorText(error, "mmap(" + name + ") failed: " + std::strerror(errno));
            return false;
        }
        return true;
    };

    dataOpened = openView(posixData, mapping->dataDescriptor, mapping->dataView, mapping->dataBytes);
    constantOpened = openView(posixConstant, mapping->constantDescriptor, mapping->constantView, mapping->constantBytes);
    indexOpened = openView(posixIndex, mapping->indexDescriptor, mapping->indexView, mapping->indexBytes);
#endif

    const bool published = runtime::SharedFactoryMappingAccess::Attach(
        factory, dataOpened ? mapping->dataView : nullptr, dataOpened ? kAirShipDataBytes : 0,
        constantOpened ? mapping->constantView : nullptr, constantOpened ? kAirShipConstantBytes : 0,
        indexOpened ? mapping->indexView : nullptr, indexOpened ? kAirShipIndexBytes : 0, mapping, error
    );
    return dataOpened && constantOpened && indexOpened && published;
}

} // namespace

bool AttachPartySharedMemory(
    CPartyFactory& factory, const std::string& dataName, const std::string& indexName, std::string* error
) {
    return AttachPair(
        dataName, indexName, kPartyDataBytes, kPartyIndexBytes,
        [&factory](
            void* data, std::size_t dataBytes, void* index, std::size_t indexBytes, std::shared_ptr<void> lifetime,
            std::string* attachError
        ) {
            return runtime::SharedFactoryMappingAccess::Attach(
                factory, data, dataBytes, index, indexBytes, std::move(lifetime), attachError
            );
        },
        error
    );
}

bool AttachPledgeSharedMemory(
    CPledgeFactory& factory, const std::string& dataName, const std::string& indexName, std::string* error
) {
    return AttachPair(
        dataName, indexName, kPledgeDataBytes, kPledgeIndexBytes,
        [&factory](
            void* data, std::size_t dataBytes, void* index, std::size_t indexBytes, std::shared_ptr<void> lifetime,
            std::string* attachError
        ) {
            return runtime::SharedFactoryMappingAccess::Attach(
                factory, data, dataBytes, index, indexBytes, std::move(lifetime), attachError
            );
        },
        error
    );
}

bool AttachGeneralObjectSharedMemory(
    CGeneralObjectFactory& factory, const std::string& dataName, const std::string& indexName, std::string* error
) {
    return AttachPair(
        dataName, indexName, kGeneralObjectDataBytes, kGeneralObjectIndexBytes,
        [&factory](
            void* data, std::size_t dataBytes, void* index, std::size_t indexBytes, std::shared_ptr<void> lifetime,
            std::string* attachError
        ) {
            return runtime::SharedFactoryMappingAccess::Attach(
                factory, data, dataBytes, index, indexBytes, std::move(lifetime), attachError
            );
        },
        error
    );
}

bool AttachAllianceSharedMemory(
    AllianceFactory& factory, const std::string& dataName, const std::string& indexName, std::string* error
) {
    const std::size_t dataBytes = static_cast<std::size_t>(SharedDefine_Alliance.max_data) * sizeof(CSharedAllianceData);
    const std::size_t indexBytes = static_cast<std::size_t>(SharedDefine_Alliance.max_data) * sizeof(std::int32_t);
    return AttachPair(
        dataName, indexName, dataBytes, indexBytes,
        [&factory](
            void* data, std::size_t mappedDataBytes, void* index, std::size_t mappedIndexBytes,
            std::shared_ptr<void> lifetime, std::string* attachError
        ) {
            return runtime::SharedFactoryMappingAccess::Attach(
                factory, data, mappedDataBytes, index, mappedIndexBytes, std::move(lifetime), attachError
            );
        },
        error
    );
}

bool AttachAirShipSharedMemory(
    CAirShipFactory& factory,
    const std::string& dataName,
    const std::string& constantName,
    const std::string& indexName,
    std::string* error
) {
    return AttachAirShipTriple(factory, dataName, constantName, indexName, error);
}


namespace runtime {

bool OpenDefaultAllianceSharedFactory(
    CSharedFactory<CAlliance, CSharedAllianceData, CSharedAllianceConstant, &SharedDefine_Alliance>& factory
) {
    return AttachAllianceSharedMemory(factory, kAllianceDataSharedMemoryName, kAllianceIndexSharedMemoryName, nullptr);
}

bool OpenDefaultAirShipSharedFactory(CAirShipFactory& factory) {
    return AttachAirShipSharedMemory(
        factory, kAirShipDataSharedMemoryName, kAirShipConstantSharedMemoryName, kAirShipIndexSharedMemoryName, nullptr
    );
}

bool OpenDefaultPartySharedFactory(CPartyFactory& factory) {
    return AttachPartySharedMemory(factory, kPartyDataSharedMemoryName, kPartyIndexSharedMemoryName, nullptr);
}

bool OpenDefaultPledgeSharedFactory(CPledgeFactory& factory) {
    return AttachPledgeSharedMemory(factory, kPledgeDataSharedMemoryName, kPledgeIndexSharedMemoryName, nullptr);
}

bool OpenDefaultGeneralObjectSharedFactory(CGeneralObjectFactory& factory) {
    return AttachGeneralObjectSharedMemory(
        factory, kGeneralObjectDataSharedMemoryName, kGeneralObjectIndexSharedMemoryName, nullptr
    );
}

} // namespace runtime
