#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace runtime {

// A shared scope gives related processes their own OS mapping names.
// Set it before starting a process. An empty scope keeps the server names.
inline std::string ScopedSharedMemoryName(std::string_view name) {
    const char* scope = std::getenv("L2_SHARED_MEMORY_NAMESPACE");
    if (!scope || !*scope || name.empty()) {
        return std::string(name);
    }
    if (name.front() == '/') {
        name.remove_prefix(1);
    }
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char byte : std::string_view(scope)) {
        hash = (hash ^ byte) * UINT64_C(1099511628211);
    }
    hash *= UINT64_C(1099511628211);
    for (const unsigned char byte : name) {
        hash = (hash ^ byte) * UINT64_C(1099511628211);
    }
    constexpr char digits[] = "0123456789abcdef";
    std::string result = "L2_";
    for (int shift = 60; shift >= 0; shift -= 4) {
        result.push_back(digits[(hash >> shift) & 15U]);
    }
    return result;
}

} // namespace runtime
