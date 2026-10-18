// Platform adapter for the three mappings opened by the target item factory.
#pragma once

#include "Shared/src/CenteredSharedRWLock.h"
#include "Shared/src/SharedFactory.h"

#include <string>


inline constexpr const char* kItemSharedMemoryName = "L2World_ItemData";
inline constexpr const char* kItemConstantSharedMemoryName = "L2World_ItemConstant";
inline constexpr const char* kItemIndexSharedMemoryName = "L2World_ItemIndex";

bool AttachItemSharedMemory(
    CItemFactory& runtime,
    const std::string& dataName = kItemSharedMemoryName,
    const std::string& constantName = kItemConstantSharedMemoryName,
    const std::string& indexName = kItemIndexSharedMemoryName,
    std::string* error = nullptr
);
