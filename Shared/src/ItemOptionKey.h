#pragma once

#include <cstdint>

// Main Shared two-integer value. Current CacheD embeds it at CItem +0x68.
// its compiler-generated copy assignment at 0x004eccc0 conditionally copies the
// two adjacent dwords and is reused by 27 production callers. Current L2Server
// copies the same 0x08 value at 0x007f414c and stores it at its
// product CItem +0x90. Member names keep CacheD persistence vocabulary. The
// ABI is shared.
class ItemOptionKey {
public:
    std::int32_t variationOption1 = 0;
    std::int32_t variationOption2 = 0;
};
