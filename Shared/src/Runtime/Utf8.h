#pragma once

#include <string>
#include <string_view>

namespace runtime {

// C++20 filesystem/u8 literals use char8_t. Logging and database APIs use
// char UTF-8 bytes. Copy the same bytes, without locale conversion or decoding.
inline std::string Utf8Bytes(std::u8string_view value) {
    if (value.empty()) {
        return {};
    }
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

// Tag existing UTF-8 bytes for C++20 APIs (notably filesystem::path).
// The iterator conversion keeps every byte, including embedded NULs.
inline std::u8string Utf8CodeUnits(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    return {value.begin(), value.end()};
}

} // namespace runtime
