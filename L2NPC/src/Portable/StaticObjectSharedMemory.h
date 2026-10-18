// Platform adapter for the exact StaticObject data/index factory mappings.
#pragma once

#include "Shared/src/SharedFactory.h"

#include <cstddef>
#include <string>


inline constexpr const char* kStaticObjectSharedMemoryName = "L2World_StaticObjectData";
inline constexpr const char* kStaticObjectIndexSharedMemoryName = "L2World_StaticObjectIndex";
inline constexpr std::size_t kStaticObjectSharedRecordCount = 10001;

// Opens the original named mapping and attaches its live bytes to the runtime.
// Windows uses OpenFileMapping/MapViewOfFile. POSIX uses shm_open/mmap with an
// automatically normalized leading slash.
bool AttachStaticObjectSharedMemory(
    CStaticObjectFactory& runtime,
    const std::string& dataName = kStaticObjectSharedMemoryName,
    const std::string& indexName = kStaticObjectIndexSharedMemoryName,
    std::string* error = nullptr
);
