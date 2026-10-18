#pragma once

#include "SharedMemoryName.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace runtime {

// Keep the target's logical Win32 mapping name at every caller boundary.
// POSIX adds the required leading slash. Darwin additionally limits a POSIX
// shared-memory name to 31 bytes, so names that cannot exist literally use a
// deterministic physical alias shared by every producer and consumer.
inline std::string PosixSharedMemoryName(std::string_view logicalName) {
    const std::string scopedName = ScopedSharedMemoryName(logicalName);
    logicalName = scopedName;
    std::string result;
    if (logicalName.empty() || logicalName.front() == '/') {
        result.assign(logicalName);
    } else {
        result.reserve(logicalName.size() + 1);
        result.push_back('/');
        result.append(logicalName);
    }

#if defined(__APPLE__)
    if (result.size() > 31) {
        std::uint64_t hash = UINT64_C(14695981039346656037);
        for (const unsigned char byte : result) {
            hash ^= byte;
            hash *= UINT64_C(1099511628211);
        }

        static constexpr char kHex[] = "0123456789abcdef";
        std::string alias = "/l2_";
        for (int shift = 60; shift >= 0; shift -= 4) {
            alias.push_back(kHex[(hash >> shift) & 0x0fU]);
        }
        return alias;
    }
#endif

    return result;
}

} // namespace runtime
