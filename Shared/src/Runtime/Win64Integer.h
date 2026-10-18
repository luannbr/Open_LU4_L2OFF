#pragma once

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <limits>

#if defined(_WIN32)
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace runtime {


// Win64's signed IDIV raises an integer-divide exception both for a zero
// divisor and for INT_MIN / -1. Express that result explicitly: otherwise
// C++ undefined-behavior assumptions let non-MSVC compilers move later range
// checks ahead of the recovered division and silently remove the fault.
[[noreturn]] inline void RaiseWin64IntegerDivideError() {
#if defined(_WIN32)
    ::RaiseException(EXCEPTION_INT_DIVIDE_BY_ZERO, 0, 0, nullptr);
#else
    std::raise(SIGFPE);
#endif
    std::abort();
}

inline std::int32_t Win64SignedDivide(std::int32_t dividend, std::int32_t divisor) {
    if (divisor == 0 || (dividend == std::numeric_limits<std::int32_t>::min() && divisor == -1)) {
        RaiseWin64IntegerDivideError();
    }
    return dividend / divisor;
}

inline std::int64_t Win64SignedDivide(std::int64_t dividend, std::int64_t divisor) {
    if (divisor == 0 || (dividend == std::numeric_limits<std::int64_t>::min() && divisor == -1)) {
        RaiseWin64IntegerDivideError();
    }
    return dividend / divisor;
}

inline std::int32_t Win64SignedRemainder(std::int32_t dividend, std::int32_t divisor) {
    if (divisor == 0 || (dividend == std::numeric_limits<std::int32_t>::min() && divisor == -1)) {
        RaiseWin64IntegerDivideError();
    }
    return dividend % divisor;
}

inline std::uint64_t Win64UnsignedRemainder(std::uint64_t dividend, std::uint64_t divisor) {
    if (divisor == 0) {
        RaiseWin64IntegerDivideError();
    }
    return dividend % divisor;
}

} // namespace runtime
