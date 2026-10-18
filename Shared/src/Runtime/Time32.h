#pragma once

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <limits>

namespace runtime::crt {

inline bool LocalTime(std::time_t value, std::tm& output) noexcept {
#if defined(_WIN32)
    return ::localtime_s(&output, &value) == 0;
#else
    return ::localtime_r(&value, &output) != nullptr;
#endif
}

inline bool LocalTime32(std::int32_t value, std::tm& output) noexcept {
#if defined(_MSC_VER)
    const __time32_t input = static_cast<__time32_t>(value);
    return ::_localtime32_s(&output, &input) == 0;
#else
    // MSVC's 32-bit CRT rejects pre-epoch values. POSIX localtime_r accepts
    // them, so keep the target boundary on portable hosts.
    if (value < 0) {
        errno = EINVAL;
        return false;
    }
    return LocalTime(static_cast<std::time_t>(value), output);
#endif
}

inline std::tm LocalTimeOrZero(std::time_t value) noexcept {
    std::tm output{};
    LocalTime(value, output);
    return output;
}

inline std::tm LocalTime32OrZero(std::int32_t value) noexcept {
    std::tm output{};
    LocalTime32(value, output);
    return output;
}

// Pointer-returning counterpart of MSVC _localtime32. The storage is reused
// by the next pointer-returning conversion on the same thread, matching the
// target CRT lifetime instead of returning an owned snapshot.
inline std::tm* LocalTime32(std::int32_t value) noexcept {
#if defined(_MSC_VER)
    const __time32_t input = static_cast<__time32_t>(value);
    return ::_localtime32(&input);
#else
    thread_local std::tm output;
    return LocalTime32(value, output) ? &output : nullptr;
#endif
}

// Pointer-returning counterpart of VS2010 _localtime64. That CRT accepts
// values only through 3000-12-31 23:59:59.
inline std::tm* LocalTime64(std::int64_t value) noexcept {
#if defined(_MSC_VER)
    const __time64_t input = static_cast<__time64_t>(value);
    return ::_localtime64(&input);
#else
    constexpr std::int64_t kMaximumTime64 = INT64_C(32535244799);
    if (value < 0 || value > kMaximumTime64) {
        errno = EINVAL;
        return nullptr;
    }
    thread_local std::tm output;
    return LocalTime(static_cast<std::time_t>(value), output) ? &output : nullptr;
#endif
}

inline std::int32_t Time32Now() noexcept {
#if defined(_WIN32)
    return static_cast<std::int32_t>(::_time32(nullptr));
#else
    const std::time_t now = std::time(nullptr);
    if (now == static_cast<std::time_t>(-1) ||
        (std::numeric_limits<std::time_t>::is_signed && now < static_cast<std::time_t>(0)) ||
        now > static_cast<std::time_t>(std::numeric_limits<std::int32_t>::max())) {
        errno = EINVAL;
        return -1;
    }
    return static_cast<std::int32_t>(now);
#endif
}

inline std::int32_t Mktime32(std::tm* localTime) noexcept {
    if (localTime == nullptr) {
        errno = EINVAL;
        return -1;
    }
#if defined(_MSC_VER)
    return static_cast<std::int32_t>(::_mktime32(localTime));
#else
    const std::time_t converted = std::mktime(localTime);
    if (converted == static_cast<std::time_t>(-1) ||
        (std::numeric_limits<std::time_t>::is_signed && converted < static_cast<std::time_t>(0)) ||
        converted > static_cast<std::time_t>(std::numeric_limits<std::int32_t>::max())) {
        errno = EINVAL;
        return -1;
    }
    return static_cast<std::int32_t>(converted);
#endif
}

// Common spelling of the target helper that zeroes a CRT tm, copies the six
// calendar fields from a SYSTEMTIME-compatible value, and calls _mktime32.
// Zero-initialization leaves tm_isdst == 0. Do not replace it
// with automatic daylight-saving inference.
template <typename TSystemTime>
inline std::int32_t SystemTimeToTime32(const TSystemTime& value) noexcept {
    std::tm localTime{};
    localTime.tm_year = static_cast<int>(value.wYear) - 1900;
    localTime.tm_mon = static_cast<int>(value.wMonth) - 1;
    localTime.tm_mday = static_cast<int>(value.wDay);
    localTime.tm_hour = static_cast<int>(value.wHour);
    localTime.tm_min = static_cast<int>(value.wMinute);
    localTime.tm_sec = static_cast<int>(value.wSecond);
    return Mktime32(&localTime);
}

template <typename TSystemTime>
inline TSystemTime LocalSystemTimeNow() noexcept {
    const auto now = std::chrono::system_clock::now();
    const std::int64_t sinceEpochMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::int64_t seconds = sinceEpochMilliseconds / 1000;
    std::int64_t milliseconds = sinceEpochMilliseconds % 1000;
    if (milliseconds < 0) {
        --seconds;
        milliseconds += 1000;
    }

    std::tm localTime{};
    if (!LocalTime(static_cast<std::time_t>(seconds), localTime)) {
        return {};
    }
    return TSystemTime{
        static_cast<std::uint16_t>(localTime.tm_year + 1900),
        static_cast<std::uint16_t>(localTime.tm_mon + 1),
        static_cast<std::uint16_t>(localTime.tm_wday),
        static_cast<std::uint16_t>(localTime.tm_mday),
        static_cast<std::uint16_t>(localTime.tm_hour),
        static_cast<std::uint16_t>(localTime.tm_min),
        static_cast<std::uint16_t>(localTime.tm_sec),
        static_cast<std::uint16_t>(milliseconds),
    };
}

} // namespace runtime::crt
