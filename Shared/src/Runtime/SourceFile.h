#pragma once

namespace source_location {

// The target's AddRef/Release call sites use __FILE__, and the compiled image
// contains source basenames such as "MakerEventFactory.cpp". Return a pointer
// into the compiler string literal so the portable build keeps that value
// even when its build system expands __FILE__ to an absolute host path.
constexpr const char* FileName(const char* path) noexcept {
    const char* fileName = path;
    for (const char* current = path; *current != '\0'; ++current) {
        if (*current == '/' || *current == '\\') {
            fileName = current + 1;
        }
    }
    return fileName;
}

} // namespace source_location

// Source file name without directories, for caller-location arguments.
#ifndef L2_SOURCE_FILE
#ifdef __FILE_NAME__
#define L2_SOURCE_FILE __FILE_NAME__
#else
#define L2_SOURCE_FILE (::source_location::FileName(__FILE__))
#endif
#endif
