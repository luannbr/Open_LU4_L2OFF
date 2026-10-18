#include "L2Core/src/inc/FileUtils.h"

#include "Shared/src/Runtime/WindowsAnsi.h"
#include "TextRuntime.h"

#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>

#if defined(_WIN32)
#include <limits>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <unistd.h>
#endif

void hprintf(void* file, const wchar_t* format, ...) {
    // PDB owner: l2core/fileutils.cpp. Current target 0x00551ca8 keeps one
    // process-wide 20,000-WCHAR buffer, formats with the MSVCRT wide-printf
    // rules, converts through CP_ACP and writes the resulting byte sequence.
    // Original shared formatter storage:
    // static wchar_t buffer[20000]{}.
    // Logging overlay: concurrent report writers must not overwrite another
    // thread's formatted fragment before conversion and writing. This does not
    // serialize a whole report or make signal-handler reentry safe.
    thread_local wchar_t buffer[20000]{};

    va_list arguments;
    va_start(arguments, format);
#if defined(_MSC_VER)
    // Original unchecked formatting call:
    // ::_vsnwprintf_s(
    //     buffer,
    //     std::size(buffer),
    //     std::size(buffer) - 1,
    //     format,
    //     arguments)
    const int length = ::_vsnwprintf_s(buffer, std::size(buffer), std::size(buffer) - 1, format, arguments);
#else
    const std::wstring hostFormat = WindowsWideFormatToHost(format);
    const int length = std::vswprintf(buffer, std::size(buffer), hostFormat.c_str(), arguments);
    // Original failure handling silently omitted the report fragment:
    // if (length < 0) {
    //     buffer[0] = L'\0'.
    // }
#endif
    va_end(arguments);

    if (length < 0 || static_cast<std::size_t>(length) >= std::size(buffer)) {
        // A negative result can mean invalid formatting/encoding or too little
        // capacity. Do not claim a particular cause or check unspecified
        // buffer contents after vswprintf fails. Successful fragments keep
        // their original bytes and encoding.
        constexpr wchar_t failure[] =
            L"[report fragment unavailable: formatting failed or exceeded the 19999-character limit]\r\n";
        // MSVC rejects std::size on a thread_local array in a constant expression.
        static_assert(std::size(failure) <= std::extent_v<decltype(buffer)>);
        std::wmemcpy(buffer, failure, std::size(failure));
    }

    const std::string converted = portable::WindowsAnsiFromWide(std::wstring_view(buffer));
#if defined(_WIN32)
    bool complete = false;
    if (converted.size() <= std::numeric_limits<DWORD>::max()) {
        DWORD written = 0;
        // Original unchecked write:
        // ::WriteFile(
        //     static_cast<HANDLE>(file),
        //     converted.data(),
        //     static_cast<DWORD>(converted.size()),
        //     &written,
        //     nullptr)
        const BOOL succeeded = ::WriteFile(
            static_cast<HANDLE>(file), converted.data(), static_cast<DWORD>(converted.size()), &written, nullptr
        );
        complete = succeeded && static_cast<std::size_t>(written) == converted.size();
    }
#else
    if (!file)
        return;
    // Original unchecked write/flush:
    // std::fwrite(
    //     converted.data(),
    //     1,
    //     converted.size(),
    //     static_cast<std::FILE*>(file))
    // std::fflush(
    //     static_cast<std::FILE*>(file))
    const std::size_t written = std::fwrite(converted.data(), 1, converted.size(), static_cast<std::FILE*>(file));
    const int flushResult = std::fflush(static_cast<std::FILE*>(file));
    const bool complete = written == converted.size() && flushResult == 0;
#endif

    // Avoid CLog, heap allocation and CRT stream locks in this fallback.
    // This only diagnoses the write. It does not retry or repair lost bytes.
    static std::atomic_flag writeFailureReported = ATOMIC_FLAG_INIT;
    if (complete) {
        if (!converted.empty())
            writeFailureReported.clear(std::memory_order_relaxed);
    } else if (!writeFailureReported.test_and_set(std::memory_order_relaxed)) {
        const int savedErrno = errno;
        constexpr char warning[] = "ERROR [fileutils.cpp] Report write or flush failed, or the write was incomplete. "
                                   "The report may be incomplete. Further warnings are suppressed until a nonempty fragment "
                                   "is written successfully.\n";
#if defined(_WIN32)
        const DWORD savedWindowsError = ::GetLastError();
        DWORD warningWritten = 0;
        ::WriteFile(
            ::GetStdHandle(STD_ERROR_HANDLE), warning, static_cast<DWORD>(sizeof(warning) - 1), &warningWritten, nullptr
        );
        ::SetLastError(savedWindowsError);
#else
        (void)::write(STDERR_FILENO, warning, sizeof(warning) - 1);
#endif
        errno = savedErrno;
    }
}
