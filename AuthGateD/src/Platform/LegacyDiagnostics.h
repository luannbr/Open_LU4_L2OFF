#pragma once

#include <cstddef>
#include <string>

namespace authgated::portable {

inline std::u16string LegacyWidePacketFormat(
    const char* format) {
    // Two shipping assemble-error branches pass a narrow packet format to a
    // wide `%s` conversion. Reproduce the x86 little-endian byte pairing but
    // avoid its potentially out-of-bounds final 16-bit read.
    std::u16string result;
    if (!format) return result;
    const std::size_t length =
        std::char_traits<char>::length(format);
    result.reserve((length + 1U) / 2U);
    for (std::size_t offset = 0; offset < length; offset += 2U) {
        const std::uint16_t low =
            static_cast<unsigned char>(format[offset]);
        const std::uint16_t high = offset + 1U < length
            ? static_cast<unsigned char>(format[offset + 1U])
            : 0U;
        result.push_back(
            static_cast<char16_t>(low | (high << 8U)));
    }
    return result;
}

} // namespace authgated::portable
