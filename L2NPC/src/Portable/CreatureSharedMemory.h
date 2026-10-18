// Platform adapter for the original creature shared-factory mappings.
#pragma once

#include "Shared/src/SharedFactory.h"

#include <string>


inline constexpr const char* kCreatureDataSharedMemoryName = "L2World_CreatureData";
inline constexpr const char* kCreatureIndexSharedMemoryName = "L2World_CreatureIndex";

bool AttachCreatureSharedMemory(
    CCreatureFactory& factory,
    const std::string& dataName = kCreatureDataSharedMemoryName,
    const std::string& indexName = kCreatureIndexSharedMemoryName,
    std::string* error = nullptr
);
