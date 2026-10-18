#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace runtime {

// CVTTSD2SI returns Intel's integer-indefinite value for NaN and values that
// do not fit in a signed 32-bit destination. A direct C++ conversion is
// undefined for those inputs, so recovered Win64 call sites use this helper.
inline std::int32_t Win64CvttDoubleToInt32(double value) noexcept {
    constexpr double minimum = std::numeric_limits<std::int32_t>::min();
    constexpr double exclusiveMaximum = static_cast<double>(std::numeric_limits<std::int32_t>::max()) + 1.0;
    if (!std::isfinite(value) || value < minimum || value >= exclusiveMaximum) {
        return std::numeric_limits<std::int32_t>::min();
    }
    return static_cast<std::int32_t>(value);
}

// CVTTSS2SI has the same integer-indefinite result as CVTTSD2SI. Promoting a
// binary32 value to binary64 is exact, so the shared range implementation can
// be reused without changing the target instruction's observable result.
inline std::int32_t Win64CvttFloatToInt32(float value) noexcept {
    return Win64CvttDoubleToInt32(static_cast<double>(value));
}

// Same Intel CVTTSD2SI contract for the 64-bit destination form used by
// current L2Server coordinate/log packet paths.
inline std::int64_t Win64CvttDoubleToInt64(double value) noexcept {
    constexpr auto minimum = static_cast<double>(std::numeric_limits<std::int64_t>::min());
    // 2^63 is exactly representable in binary64. It is the first value that
    // must produce the integer-indefinite result.
    constexpr double exclusiveMaximum = 0x1p63;
    if (!std::isfinite(value) || value < minimum || value >= exclusiveMaximum) {
        return std::numeric_limits<std::int64_t>::min();
    }
    return static_cast<std::int64_t>(value);
}

// CVTTSS2SI with a signed 64-bit destination has the same range and
// integer-indefinite behavior after the exact binary32-to-binary64 widening.
inline std::int64_t Win64CvttFloatToInt64(float value) noexcept {
    return Win64CvttDoubleToInt64(static_cast<double>(value));
}

} // namespace runtime
