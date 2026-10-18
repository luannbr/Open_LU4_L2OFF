#pragma once

#include "Shared/src/Runtime/WrappingArithmetic.h"

#include <bit>
#include <cstdint>

namespace runtime {

inline std::uint16_t LoadLittleEndian16(const void* source) noexcept {
    const auto* const bytes = static_cast<const unsigned char*>(source);
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[0]) | (static_cast<std::uint16_t>(bytes[1]) << 8U));
}

inline std::uint32_t LoadLittleEndian32(const void* source) noexcept {
    const auto* const bytes = static_cast<const unsigned char*>(source);
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) | (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

inline std::int16_t LoadLittleEndianInt16(const void* source) noexcept {
    return std::bit_cast<std::int16_t>(LoadLittleEndian16(source));
}

inline std::int32_t LoadLittleEndianInt32(const void* source) noexcept { return Int32FromBits(LoadLittleEndian32(source)); }

inline std::uint64_t LoadLittleEndian64(const void* source) noexcept {
    const auto* const bytes = static_cast<const unsigned char*>(source);
    std::uint64_t value = 0;
    for (unsigned int index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
    }
    return value;
}

inline void StoreLittleEndian16(void* destination, std::uint16_t value) noexcept {
    auto* const bytes = static_cast<unsigned char*>(destination);
    bytes[0] = static_cast<unsigned char>(value);
    bytes[1] = static_cast<unsigned char>(value >> 8U);
}

inline void StoreLittleEndian32(void* destination, std::uint32_t value) noexcept {
    auto* const bytes = static_cast<unsigned char*>(destination);
    bytes[0] = static_cast<unsigned char>(value);
    bytes[1] = static_cast<unsigned char>(value >> 8U);
    bytes[2] = static_cast<unsigned char>(value >> 16U);
    bytes[3] = static_cast<unsigned char>(value >> 24U);
}

inline void StoreLittleEndian64(void* destination, std::uint64_t value) noexcept {
    auto* const bytes = static_cast<unsigned char*>(destination);
    for (unsigned int index = 0; index < 8; ++index) {
        bytes[index] = static_cast<unsigned char>(value >> (index * 8U));
    }
}

} // namespace runtime
