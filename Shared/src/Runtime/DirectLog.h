#pragma once

#include "LogStorage.h"

#include <cstdarg>

namespace runtime::logging {

// Direct diagnostics keep their caller-selected stream without entering
// CLog or taking an application lock. Use only outside signal handlers: the
// common formatter and CRT stream write are not async-signal-safe.
// One bounded record, one flush. No retry or recursive logging on sink failure.
// Format annotations follow the CRT dialect: MinGW ANSI stdio accepts C99 sizes.
// Original direct formatter kept.
// #if defined(__GNUC__) || defined(__clang__)
// __attribute__((format(printf, 4, 5)))
// #endif
// inline bool WriteDirect(std::FILE* destination, const char* application,
//                         Level level, const char* format, ...) noexcept {
//     const StorageErrorState preserveErrors
//     if (format == nullptr || *format == '\0') return true.
//     char message[4096]{}
//     va_list arguments.
//     va_start(arguments, format)
//     const int length = std::vsnprintf(message, sizeof(message), format, arguments)
//     va_end(arguments)
//     if (length < 0) {
//         return WriteRecord(destination, Format(application, Level::Error,
//             "[logging] Could not format a direct diagnostic message."))
//     }
//     if (static_cast<std::size_t>(length) >= sizeof(message)) {
//         constexpr char marker[] = " [truncated]"
//         std::memcpy(message + sizeof(message) - sizeof(marker), marker, sizeof(marker))
//     }
//     return WriteRecord(destination, Format(application, level, message))
// }
// #if defined(__GNUC__) || defined(__clang__)
// __attribute__((format(printf, 5, 0)))
// #endif
#if defined(__MINGW_PRINTF_FORMAT)
__attribute__((format(__MINGW_PRINTF_FORMAT, 5, 0)))
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 5, 0)))
#endif
inline bool WriteDirectV(
    std::FILE* destination,
    const char* application,
    Level level,
    const char* sourceFile,
    const char* format,
    va_list arguments
) noexcept {
    const StorageErrorState preserveErrors;
    if (format == nullptr || *format == '\0') {
        return true;
    }
    char message[4096]{};
    va_list copy;
    va_copy(copy, arguments);
    const int length = std::vsnprintf(message, sizeof(message), format, copy);
    va_end(copy);
    const char* source = sourceFile ? sourceFile : "";
    if (length < 0) {
        return WriteRecord(
            destination, Format(application, Level::Error, "[logging] Could not format a direct diagnostic message.", source)
        );
    }
    std::size_t messageLength = static_cast<std::size_t>(length);
    if (static_cast<std::size_t>(length) >= sizeof(message)) {
        constexpr char marker[] = " [truncated]";
        std::size_t end = sizeof(message) - sizeof(marker);
        // Do not leave a partial UTF-8 character before the truncation marker.
        while (end > 0 && (static_cast<unsigned char>(message[end]) & 0xc0U) == 0x80U) {
            --end;
        }
        std::memcpy(message + end, marker, sizeof(marker));
        messageLength = end + sizeof(marker) - 1;
    }
    // return WriteRecord(destination, Format(application, level, message, source))
    // Preserve embedded NUL bytes and the text after them. The operator
    // formatter renders NUL as \\x00 instead of silently losing the suffix.
    return WriteRecord(destination, Format(application, level, std::string_view(message, messageLength), source));
}

// Keep the existing direct-output API available without source metadata.
// #if defined(__GNUC__) || defined(__clang__)
// __attribute__((format(printf, 4, 5)))
// #endif
#if defined(__MINGW_PRINTF_FORMAT)
__attribute__((format(__MINGW_PRINTF_FORMAT, 4, 5)))
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 4, 5)))
#endif
inline bool WriteDirect(std::FILE* destination, const char* application, Level level, const char* format, ...) noexcept {
    va_list arguments;
    va_start(arguments, format);
    const bool result = WriteDirectV(destination, application, level, "", format, arguments);
    va_end(arguments);
    return result;
}

// #if defined(__GNUC__) || defined(__clang__)
// __attribute__((format(printf, 4, 5)))
// #endif
#if defined(__MINGW_PRINTF_FORMAT)
__attribute__((format(__MINGW_PRINTF_FORMAT, 4, 5)))
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 4, 5)))
#endif
inline bool WriteDirectAt(std::FILE* destination, const char* sourceFile, Level level, const char* format, ...) noexcept {
    va_list arguments;
    va_start(arguments, format);
    const bool result = WriteDirectV(destination, nullptr, level, sourceFile, format, arguments);
    va_end(arguments);
    return result;
}

} // namespace runtime::logging

// Explicit streams keep pre-init/fatal diagnostics outside the application logger.
#define L2_LOG_DIRECT(destination, level, ...)                                                                              \
    ::runtime::logging::WriteDirectAt((destination), __FILE__, (level), __VA_ARGS__)
