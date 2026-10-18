#pragma once

#include "Protocol/src/L2Constants.h"

#include <cstdint>

// Global enum kept by the matching NASC64_2015 PDB and consumed by
// Shared, CacheD and L2Server packet owners. The current executables confirm
// the values at their item-stackability branches.
// enum ItemConsumeType : std::int32_t {
//     ICT_UNKNOWN = -1,
//     ICT_NORMAL = 0,
//     ICT_CHARGE = 1,
//     ICT_STACKABLE = 2,
//     ICT_ASSET = 3,
// }
