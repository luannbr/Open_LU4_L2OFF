#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdio>

namespace runtime {

// vsnprintf into a fixed buffer. Returns the stored length, or -1 when the
// format fails or the complete text does not fit. The buffer is always
// terminated when size is not zero. Use it where a cut SQL statement or
// protocol line must not be used.
inline int BoundedVFormat(char* buffer, std::size_t size, const char* format, std::va_list arguments) noexcept {
    if (buffer == nullptr || size == 0) {
        return -1;
    }
    const int length = std::vsnprintf(buffer, size, format, arguments);
    if (length < 0 || static_cast<std::size_t>(length) >= size) {
        return -1;
    }
    return length;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
inline int BoundedFormat(char* buffer, std::size_t size, const char* format, ...) noexcept {
    std::va_list arguments;
    va_start(arguments, format);
    const int length = BoundedVFormat(buffer, size, format, arguments);
    va_end(arguments);
    return length;
}

} // namespace runtime
