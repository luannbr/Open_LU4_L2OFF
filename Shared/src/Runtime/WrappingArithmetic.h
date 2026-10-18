#pragma once

#include <cstdint>
#include <limits>

namespace runtime {

// The shipping Win64 instructions keep the low 8/16/32/64 result bits. Express
// that contract through unsigned arithmetic so the portable build does not
// depend on signed-overflow undefined behavior or implementation-defined
// unsigned-to-signed conversion.
constexpr std::int8_t Int8FromBits(std::uint8_t bits) noexcept {
    constexpr std::uint8_t signBit = UINT8_C(1) << 7U;
    if (bits < signBit) {
        return static_cast<std::int8_t>(bits);
    }
    return std::numeric_limits<std::int8_t>::min() + static_cast<std::int8_t>(bits - signBit);
}

constexpr std::int16_t Int16FromBits(std::uint16_t bits) noexcept {
    constexpr std::uint16_t signBit = UINT16_C(1) << 15U;
    if (bits < signBit) {
        return static_cast<std::int16_t>(bits);
    }
    return std::numeric_limits<std::int16_t>::min() + static_cast<std::int16_t>(bits - signBit);
}

constexpr std::int32_t Int32FromBits(std::uint32_t bits) noexcept {
    constexpr std::uint32_t signBit = UINT32_C(1) << 31U;
    if (bits < signBit) {
        return static_cast<std::int32_t>(bits);
    }
    return std::numeric_limits<std::int32_t>::min() + static_cast<std::int32_t>(bits - signBit);
}

constexpr std::int64_t Int64FromBits(std::uint64_t bits) noexcept {
    constexpr std::uint64_t signBit = UINT64_C(1) << 63U;
    if (bits < signBit) {
        return static_cast<std::int64_t>(bits);
    }
    return std::numeric_limits<std::int64_t>::min() + static_cast<std::int64_t>(bits - signBit);
}

constexpr std::int16_t AddInt16Wrapping(std::int16_t left, std::int16_t right) noexcept {
    return Int16FromBits(static_cast<std::uint16_t>(static_cast<std::uint16_t>(left) + static_cast<std::uint16_t>(right)));
}

constexpr std::int16_t SubtractInt16Wrapping(std::int16_t left, std::int16_t right) noexcept {
    return Int16FromBits(static_cast<std::uint16_t>(static_cast<std::uint16_t>(left) - static_cast<std::uint16_t>(right)));
}

constexpr std::int32_t AddInt32Wrapping(std::int32_t left, std::int32_t right) noexcept {
    return Int32FromBits(static_cast<std::uint32_t>(left) + static_cast<std::uint32_t>(right));
}

constexpr std::int32_t SubtractInt32Wrapping(std::int32_t left, std::int32_t right) noexcept {
    return Int32FromBits(static_cast<std::uint32_t>(left) - static_cast<std::uint32_t>(right));
}

constexpr std::int32_t MultiplyInt32Wrapping(std::int32_t left, std::int32_t right) noexcept {
    return Int32FromBits(static_cast<std::uint32_t>(left) * static_cast<std::uint32_t>(right));
}

constexpr std::int32_t NegateInt32Wrapping(std::int32_t value) noexcept {
    return Int32FromBits(UINT32_C(0) - static_cast<std::uint32_t>(value));
}

constexpr std::int32_t AbsoluteInt32Wrapping(std::int32_t value) noexcept {
    const auto bits = static_cast<std::uint32_t>(value);
    const std::uint32_t signMask = UINT32_C(0) - (bits >> 31U);
    return Int32FromBits((bits ^ signMask) - signMask);
}

constexpr std::int64_t AddInt64Wrapping(std::int64_t left, std::int64_t right) noexcept {
    return Int64FromBits(static_cast<std::uint64_t>(left) + static_cast<std::uint64_t>(right));
}

constexpr std::int64_t SubtractInt64Wrapping(std::int64_t left, std::int64_t right) noexcept {
    return Int64FromBits(static_cast<std::uint64_t>(left) - static_cast<std::uint64_t>(right));
}

constexpr std::int64_t MultiplyInt64Wrapping(std::int64_t left, std::int64_t right) noexcept {
    return Int64FromBits(static_cast<std::uint64_t>(left) * static_cast<std::uint64_t>(right));
}

constexpr std::int64_t NegateInt64Wrapping(std::int64_t value) noexcept {
    return Int64FromBits(UINT64_C(0) - static_cast<std::uint64_t>(value));
}

constexpr std::int64_t AbsoluteInt64Wrapping(std::int64_t value) noexcept {
    const auto bits = static_cast<std::uint64_t>(value);
    const std::uint64_t signMask = UINT64_C(0) - (bits >> 63U);
    return Int64FromBits((bits ^ signMask) - signMask);
}

static_assert(Int8FromBits(UINT8_MAX) == -1);
static_assert(Int16FromBits(UINT16_MAX) == -1);
static_assert(Int32FromBits(UINT32_MAX) == -1);
static_assert(Int64FromBits(UINT64_MAX) == -1);
static_assert(AddInt16Wrapping(INT16_MAX, 1) == INT16_MIN);
static_assert(SubtractInt16Wrapping(INT16_MIN, 1) == INT16_MAX);
static_assert(AddInt32Wrapping(INT32_MAX, 1) == INT32_MIN);
static_assert(SubtractInt32Wrapping(INT32_MIN, 1) == INT32_MAX);
static_assert(MultiplyInt32Wrapping(INT32_MIN, -1) == INT32_MIN);
static_assert(AddInt64Wrapping(INT64_MAX, 1) == INT64_MIN);
static_assert(SubtractInt64Wrapping(INT64_MIN, 1) == INT64_MAX);
static_assert(MultiplyInt64Wrapping(INT64_MIN, -1) == INT64_MIN);

} // namespace runtime
