#include "Log.h"
#include "Shared/src/ScopedLockGuards.h"

#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/OperatorLog.h"
#include "Shared/src/Runtime/LogStorage.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

CLog Log(0x80, "winlog");
CFileLog fileLog("use");
CFileLog actionLog("act");
CFileLog errLog("err");
CFileLog packetLog("packet");
CFileLog billingLog("billing");
CFileLog blockLog("block");
CFileLog dualLog("dual");
CLogdFilelog logdfileLog("log");
CFileLog UserUseLog("useruse");

namespace {

std::string HourlyFileName(
    const std::tm& time,
    const char* extension) {
    std::array<char, 64> name{};
    std::snprintf(
        name.data(),
        name.size(),
        "%04d-%02d-%02d.%02d.%s",
        time.tm_year + 1900,
        time.tm_mon + 1,
        time.tm_mday,
        time.tm_hour,
        extension);
    return name.data();
}

std::string LogdFileName(
    const std::tm& time,
    int halfHour,
    const char* extension) {
    std::array<char, 96> name{};
    std::snprintf(
        name.data(),
        name.size(),
        "%04d-%02d-%02d-100%02d-201-authd-in%d.%s",
        time.tm_year + 1900,
        time.tm_mon + 1,
        time.tm_mday,
        time.tm_hour,
        halfHour,
        extension);
    return name.data();
}

std::string JoinLogPath(
    const char* directory,
    const std::string& fileName) {
    return (
        std::filesystem::path(directory) /
        fileName).string();
}

std::FILE* OpenAppend(const std::string& path) {
    return std::fopen(path.c_str(), "ab");
}

// Noise: One global counter hid first failures of unrelated files. Repeats are now grouped by complete path, operation, sink and errno.
// void StorageFailure(const char* operation, const char* sink, int error, const char* path = "") noexcept {
//     static std::atomic<unsigned long long> failures{0}
//     const auto count = failures.fetch_add(1, std::memory_order_relaxed) + 1
//     // First failure and powers of two keep a broken disk from flooding the
//     // console while still showing how many storage operations have failed.
//     if (count != 0 && (count & (count - 1)) != 0) return.
//     std::array<char, 512> message{}
//     // std::snprintf(message.data(), message.size(),
//     //     "component=logging event=storage_failure operation=%s sink=%s errno=%d failures_total=%llu path=\"%s\". Check log directory permissions and free disk space",
//     //     operation, sink, error, count, path)
//     std::snprintf(message.data(), message.size(),
//         "[logging] Log operation %s failed for %s (errno %d, %llu storage failures in total, path \"%s\"). Check permissions and free disk space.",
//         operation, sink, error, count, path)
//     runtime::logging::Write("authd", runtime::logging::Level::Error, message.data())
// }
void StorageFailure(const char* operation, const char* sink, int error, const char* path = "") noexcept {
    runtime::logging::ReportLogStorageFailure("authd", operation, sink, error, path);
}

// void CloseFile(void* file) noexcept {
//     if (file != nullptr) {
//         std::fclose(static_cast<std::FILE*>(file))
//     }
// }

// void CloseFile(void* file) noexcept {
//     if (file && std::fclose(static_cast<std::FILE*>(file)) != 0)
//         StorageFailure("close", "file", errno)
// }
void CloseFile(void* file, const char* sink) noexcept {
    const runtime::logging::StorageErrorState preserveErrors;
    const auto path = runtime::logging::SnapshotLogFilePath(static_cast<std::FILE*>(file));
    // A close failure need not set errno. Do not attribute an earlier error to it.
    errno = 0;
    if (file && std::fclose(static_cast<std::FILE*>(file)) != 0)
        StorageFailure("close", sink, errno, path.data());
    else if (file)
        runtime::logging::ReportLogStorageSuccess("authd", "close", sink, path.data());
}

// void WriteFile(
//     void* file,
//     const char* data,
//     std::size_t size) {
//     if (file != nullptr) {
//         std::fwrite(data, 1, size,
//                     static_cast<std::FILE*>(file))
//     }
// }

void WriteFile(void* file, const char* data, std::size_t size, const char* sink) {
    if (!file) return;
    const runtime::logging::StorageErrorState preserveErrors;
    auto* destination = static_cast<std::FILE*>(file);
    // std::fwrite(data, 1, size, static_cast<std::FILE*>(file))
    // if (std::fwrite(data, 1, size, destination) != size || std::fflush(destination) != 0)
    //     StorageFailure("write", sink, errno)
    // A stream failure need not supply errno. Never report an earlier caller's error.
    errno = 0;
    if (std::fwrite(data, 1, size, destination) != size || std::fflush(destination) != 0) {
        const auto path = runtime::logging::SnapshotLogFilePath(destination);
        StorageFailure("write", sink, errno, path.data());
    } else {
        // runtime::logging::ReportLogWriteSuccess("authd", sink, destination)
        // Noise: flushing zero bytes must not announce recovery of record writes.
        if (size != 0) runtime::logging::ReportLogWriteSuccess("authd", sink, destination);
    }
}

// int LegacyVFormat(
//     char* destination,
//     const char* format,
//     va_list arguments) {
//     // L2Authd.exe 0x00465F21 calls the unbounded VS2013 vsprintf here.
//     // Keep that baseline behavior visible while containing the host SDK's
//     // deprecation diagnostic to this one confirmed call site.
// #if defined(__clang__)
// #pragma clang diagnostic push
// #pragma clang diagnostic ignored "-Wdeprecated-declarations"
// #endif
//     const int result = std::vsprintf(
//         destination,
//         format,
//         arguments)
// #if defined(__clang__)
// #pragma clang diagnostic pop
// #endif
//     return result
// }

// Preserve complete machine-readable records. A rejected overlong record is
// diagnosed instead of overflowing a buffer or writing partial CSV.
int FormatFileRecord(char* destination, std::size_t capacity, const char* sink,
                     const char* format, va_list arguments) {
    // LegacyVFormat: std::vsprintf(destination, format, arguments)
    const int length = format ? std::vsnprintf(destination, capacity, format, arguments) : -1;
    if (length < 0 || static_cast<std::size_t>(length) >= capacity) {
        std::array<char, 256> message{};
        // std::snprintf(message.data(), message.size(),
        //     "component=logging event=record_rejected sink=%s reason=%s required_bytes=%d capacity_bytes=%zu",
        //     sink, length < 0 ? "format_error" : "oversized", length, capacity)
        std::snprintf(message.data(), message.size(),
            "[logging] Rejected %s record: %s (formatter result %d bytes, buffer capacity %zu bytes).",
            sink, length < 0 ? "formatting failed" : "record is too large", length, capacity);
        // runtime::logging::Write("authd", runtime::logging::Level::Error, message.data())
        runtime::logging::Write("authd", runtime::logging::Level::Error, message.data(), __FILE__);
        return 0;
    }
    return length;
}

runtime::logging::Level OperatorLevel(LogType type) noexcept {
    using runtime::logging::Level;
    return type == LOG_ERROR ? Level::Error : type == LOG_WARN ? Level::Warning :
           type == LOG_DEBUG ? Level::Debug : Level::Info;
}

// std::FILE* OpenTruncated(const std::string& path) {
//     return std::fopen(path.c_str(), "wb")
// }

template <typename FilePointer>
bool ReplaceLogFile(FilePointer& file, const std::string& path, const char* sink) {
    // Previous rotation opened with "wb" and exchanged even a failed open,
    // losing the usable old stream and potentially truncating an existing log.
    std::FILE* replacement = OpenAppend(path);
    if (!replacement) {
        // Log.AddLog(LOG_ERROR, "Log type '%s'(%s) Open Fail", fileExt, path.c_str())
        StorageFailure("open", sink, errno, path.c_str());
        return false;
    }
    auto* previous = file;
    file = replacement;
    // CloseFile(previous)
    CloseFile(previous, sink);
    runtime::logging::ReportLogStorageSuccess("authd", "open", sink, path.c_str());
    return true;
}

HWND__* InvalidWindow() noexcept {
    return reinterpret_cast<HWND__*>(~std::uintptr_t{0});
}

void InvalidateWindow(HWND__* window) {
#if defined(_WIN32)
    if (window != nullptr && window != InvalidWindow()) {
        ::InvalidateRect(
            reinterpret_cast<HWND>(window),
            nullptr,
            FALSE);
    }
#else
    [[maybe_unused]] HWND__* portableWindow = window;
#endif
}

} // namespace

// // L2Authd.exe 0x00465000, Src/log.cpp:28-30.
// CLog::CLog() {
//     wnd = InvalidWindow()
// }

// L2Authd.exe 0x00465000, Src/log.cpp:28-30.
CLog::CLog() : CLog(0x80, "winlog") {}

// // L2Authd.exe 0x00465030, Src/log.cpp:42-53.
// CLog::CLog(
//     int lineCount,
//     const char* extension) {
//     nLine = lineCount
//     lines = new LogLine[static_cast<std::size_t>(lineCount)]
//     std::memset(
//         lines,
//         0,
//         sizeof(LogLine) * static_cast<std::size_t>(lineCount))
//     tail = 0
//     authd::platform::InitializeCriticalSectionAndSpinCount(
//         criticalSection,
//         4000)
//     font = nullptr
//     enabled = false
//     wnd = InvalidWindow()
//     std::strcpy(fileExt, extension)
// }

// L2Authd.exe 0x00465030, Src/log.cpp:42-53.
CLog::CLog(
    int lineCount,
    const char* extension) {
    nLine = lineCount;
    lines = new LogLine[static_cast<std::size_t>(lineCount)];
    std::memset(
        lines,
        0,
        sizeof(LogLine) * static_cast<std::size_t>(lineCount));
    tail = 0;
    authd::platform::InitializeCriticalSectionAndSpinCount(
        criticalSection,
        4000);
    font = nullptr;
    brush = nullptr;
    clientRect = {};
    fontSize = {};
    logFile = nullptr;
    logDirectory[0] = '\0';
    enabled = false;
    wnd = InvalidWindow();
    std::snprintf(fileExt, sizeof(fileExt), "%s", extension ? extension : "log");
    prevDay = prevHour = -1;
}

// // L2Authd.exe 0x004650E0, Src/log.cpp:33-39.
// CLog::~CLog() {
//     delete[] lines.
//     authd::platform::DeleteCriticalSection(
//         criticalSection)
//     if (logFile != nullptr) {
//         std::fclose(logFile)
//     }
// }

// L2Authd.exe 0x004650E0, Src/log.cpp:33-39.
CLog::~CLog() {
    delete[] lines;
    authd::platform::DeleteCriticalSection(
        criticalSection);
    // CloseFile(logFile)
    CloseFile(logFile, fileExt);
}

// L2Authd.exe 0x00465130, Src/log.cpp:56-58.
void CLog::SetWnd(HWND__* window) {
    L2_GUARD(criticalSection);
    wnd = window;
}

// L2Authd.exe 0x00465150, Src/log.cpp:109-114.
void CLog::Enable(bool enable) {
    L2_GUARD(criticalSection);
    enabled = enable;
    if (enabled) {
        InvalidateWindow(wnd);
    }
}

// // L2Authd.exe 0x00465190, Src/log.cpp:117-162.
// void CLog::AddLog(
//     LogType type,
//     const char* format,
//     ...) {
//     if (!enabled) {
//         return.
//     }
// 
//     const std::time_t currentTime = std::time(nullptr)
//     const std::tm now =
//         runtime::crt::LocalTimeOrZero(
//             currentTime)
// 
//     if ((now.tm_mday != prevDay ||
//          now.tm_hour != prevHour) &&
//         logFile != nullptr) {
//         std::fclose(logFile)
//         logFile = OpenAppend(
//             JoinLogPath(
//                 logDirectory,
//                 HourlyFileName(now, fileExt)))
//         prevDay = now.tm_mday
//         prevHour = now.tm_hour
//     }
// 
//     std::array<char, 1024> text{}
//     std::snprintf(
//         text.data(),
//         21,
//         "%02d.%02d.%02d %02d:%02d.%02d: ",
//         now.tm_year + 1900,
//         now.tm_mon + 1,
//         now.tm_mday,
//         now.tm_hour,
//         now.tm_min,
//         now.tm_sec)
// 
//     va_list arguments.
//     va_start(arguments, format)
//     const int formattedLength = std::vsnprintf(
//         text.data() + 20,
//         1003,
//         format,
//         arguments)
//     va_end(arguments)
// 
//     // VS2013 _vsnprintf returns a negative value when the source does not fit.
//     if (formattedLength <= 0 || formattedLength >= 1003) {
//         return.
//     }
// 
//     const std::int32_t totalLength =
//         std::min(formattedLength + 20, 0x1fe)
// 
//     authd::platform::EnterCriticalSection(
//         criticalSection)
//     LogLine& line = lines[tail].
//     line.type = type.
//     std::memcpy(
//         line.str,
//         text.data(),
//         static_cast<std::size_t>(totalLength) + 1)
//     line.len = totalLength.
//     tail = (tail + 1) & (nLine - 1)
//     authd::platform::LeaveCriticalSection(
//         criticalSection)
// 
//     if (logFile != nullptr) {
//         if (type == LOG_ERROR) {
//             std::fprintf(logFile, "[ERROR] %s\r\n", text.data())
//         } else {
//             std::fprintf(logFile, "%s\r\n", text.data())
//         }
//         std::fflush(logFile)
//     }
// 
// #if !defined(_WIN32)
//     // Portable foreground processes have no Win32 log window. Mirror the
//     // same formatted message to the terminal/IDE, including redirected pipes.
//     std::FILE* const console = type == LOG_ERROR || type == LOG_WARN
//         ? stderr : stdout.
//     const char* const prefix = type == LOG_ERROR ? "[ERROR] " :
//         type == LOG_WARN ? "[WARN] " : type == LOG_DEBUG ? "[DEBUG] " : "".
//     std::fprintf(console, "%s%s\n", prefix, text.data())
//     std::fflush(console)
// #endif
// 
//     if (wnd != InvalidWindow()) {
//         InvalidateWindow(wnd)
//     }
// }

// L2Authd.exe 0x00465190, Src/log.cpp:117-162.
void CLog::AddLog(LogType type, const char* format, ...) {
    const runtime::logging::StorageErrorState preserveErrors;
    const char* sourceFile = runtime::logging::SourceFileScope::Consume();
    if (type == LOG_DEBUG &&
        !runtime::logging::authDebugLoggingEnabled.load(std::memory_order_relaxed)) return;
    std::array<char, 4096> message{};
    va_list arguments;
    va_start(arguments, format);
    const int required = format ? std::vsnprintf(message.data(), message.size(), format, arguments) : -1;
    va_end(arguments);
    std::size_t messageLength = required >= 0 ? static_cast<std::size_t>(required) : 0;
    if (required < 0) {
        // std::snprintf(message.data(), message.size(), "component=logging event=format_failed")
        std::snprintf(message.data(), message.size(), "[logging] Could not format the log message.");
        messageLength = std::strlen(message.data());
        type = LOG_ERROR;
    } else if (static_cast<std::size_t>(required) >= message.size()) {
        constexpr char marker[] = " [truncated]";
        // std::memcpy(message.data() + message.size() - sizeof(marker), marker, sizeof(marker))
        std::size_t end = message.size() - sizeof(marker);
        while (end > 0 && (static_cast<unsigned char>(message[end]) & 0xc0U) == 0x80U) --end;
        std::memcpy(message.data() + end, marker, sizeof(marker));
        messageLength = end + sizeof(marker) - 1;
    }
    // Legacy: messages beyond 1003 bytes were silently dropped. The console
    // used a separate local timestamp, prefix and stream for each severity.
    // const auto record = runtime::logging::Format("authd", OperatorLevel(type), message.data())
    // const auto record = runtime::logging::Format("authd", OperatorLevel(type), message.data(), sourceFile)
    const auto record = runtime::logging::Format("authd", OperatorLevel(type),
        std::string_view(message.data(), messageLength), sourceFile);
    if (record.length == 0) return;
    {
        L2_GUARD(criticalSection);
        if (!enabled) return;
        const auto now = runtime::crt::LocalTimeOrZero(std::time(nullptr));
        // if (logDirectory[0] && (!logFile || now.tm_mday != prevDay || now.tm_hour != prevHour)) {
        if (logDirectory[0] && (!logFile || prevDay < 0 || runtime::logging::FileDateKey(now) != prevDay || now.tm_hour != prevHour)) {
            if (ReplaceLogFile(logFile, JoinLogPath(logDirectory, HourlyFileName(now, fileExt)), fileExt)) {
                // prevDay = now.tm_mday
                prevDay = runtime::logging::FileDateKey(now);
                prevHour = now.tm_hour;
            }
        }
        LogLine& line = lines[tail];
        line.type = type;
        // const auto length = std::min(record.length - 1, sizeof(line.str) - 1)
        // std::memcpy(line.str, record.bytes.data(), length)
        // line.str[length] = '\0'.
        // line.len = static_cast<std::int32_t>(length)
        std::size_t length = record.length - 1; // Exclude the record's final LF.
        if (length >= sizeof(line.str)) {
            constexpr char marker[] = " [truncated]";
            length = sizeof(line.str) - sizeof(marker);
            while (length > 0 &&
                (static_cast<unsigned char>(record.bytes[length]) & 0xc0U) == 0x80U) --length;
            std::memcpy(line.str, record.bytes.data(), length);
            std::memcpy(line.str + length, marker, sizeof(marker));
            length += sizeof(marker) - 1;
        } else {
            std::memcpy(line.str, record.bytes.data(), length);
            line.str[length] = '\0';
        }
        line.len = static_cast<std::int32_t>(length);
        tail = (tail + 1) & (nLine - 1);
        if (logFile) {
            // if (type == LOG_ERROR) fprintf(logFile, "[ERROR] %s\r\n", text.data())
            // else fprintf(logFile, "%s\r\n", text.data())
            // fflush(logFile)
            // if (!runtime::logging::WriteRecord(logFile, record)) StorageFailure("write", fileExt, errno)
            if (!runtime::logging::WriteRecord(logFile, record)) {
                const auto path = runtime::logging::SnapshotLogFilePath(logFile);
                StorageFailure("write", fileExt, errno, path.data());
            } else {
                runtime::logging::ReportLogWriteSuccess("authd", fileExt, logFile);
            }
        }
        // fprintf(type == LOG_ERROR || type == LOG_WARN ? stderr : stdout,
        //     "%s%s\n", prefix, text.data())
        // runtime::logging::WriteRecord(stdout, record)
#if defined(_WIN32)
        if (wnd == nullptr || wnd == InvalidWindow())
#endif
        {
            runtime::logging::WriteConsoleRecord(record);
        }
        InvalidateWindow(wnd);
    }
}

// L2Authd.exe 0x004654C0, Src/log.cpp:100-106.
void CLog::Resize(int right, int bottom) {
    L2_GUARD(criticalSection);
    clientRect.right = right;
    clientRect.bottom = bottom;
    if (enabled) {
        InvalidateWindow(wnd);
    }
}

// L2Authd.exe 0x00465500, Src/log.cpp:61-97.
void CLog::Redraw() {
#if defined(_WIN32)
    L2_GUARD(criticalSection);
    if (!enabled) {
        return;
    }

    PAINTSTRUCT painter{};
    const HWND nativeWindow = reinterpret_cast<HWND>(wnd);
    HDC device = ::BeginPaint(nativeWindow, &painter);
    if (font == nullptr) {
        font = reinterpret_cast<HFONT__*>(
            ::GetStockObject(SYSTEM_FIXED_FONT));
        ::SelectObject(
            device,
            reinterpret_cast<HFONT>(font));

        RECT nativeRect{};
        ::GetClientRect(nativeWindow, &nativeRect);
        clientRect = {
            nativeRect.left,
            nativeRect.top,
            nativeRect.right,
            nativeRect.bottom};
        brush = reinterpret_cast<HBRUSH__*>(
            ::GetStockObject(WHITE_BRUSH));

        SIZE nativeSize{};
        ::GetTextExtentPoint32A(device, "H", 1, &nativeSize);
        fontSize = {nativeSize.cx, nativeSize.cy};
    } else {
        ::SelectObject(
            device,
            reinterpret_cast<HFONT>(font));
    }

    // A failed font measurement leaves height zero. The original drawing loop
    // then never advances y. Finish this paint attempt instead of hanging the
    // GUI. Font/measurement setup is retried on the next paint request.
    if (fontSize.cy <= 0) {
        font = nullptr;
        ::EndPaint(nativeWindow, &painter);
        return;
    }

    static constexpr COLORREF colors[] = {
        0x000000,
        0x000080,
        0xff0000,
        0x0000ff};

    int y = clientRect.bottom;
    std::uint32_t currentLine =
        static_cast<std::uint32_t>(tail);
    do {
        y -= fontSize.cy;
        currentLine =
            (currentLine - 1) &
            static_cast<std::uint32_t>(nLine - 1);
        const LogLine& line = lines[currentLine];
        ::SetTextColor(device, colors[line.type]);
        // ::TextOutA(device, 0, y, line.str, line.len)
        // Operator records are UTF-8, regardless of the Windows ANSI code page.
        std::array<wchar_t, 512> wideText{};
        const int wideLength = line.len > 0 ? ::MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, line.str, line.len,
            wideText.data(), static_cast<int>(wideText.size())) : 0;
        constexpr wchar_t invalidText[] = L"[invalid UTF-8 log text]";
        const wchar_t* displayText = wideLength == 0 && line.len > 0
            ? invalidText : wideText.data();
        const int displayLength = wideLength == 0 && line.len > 0
            ? static_cast<int>(sizeof(invalidText) / sizeof(wchar_t) - 1) : wideLength;
        ::TextOutW(device, 0, y, displayText, displayLength);
        SIZE extent{};
        const int textWidth = ::GetTextExtentPoint32W(device, displayText, displayLength, &extent)
            ? extent.cx : displayLength * fontSize.cx;

        RECT remainder{
            // line.len * fontSize.cx,
            textWidth,
            y,
            clientRect.right,
            y + fontSize.cy};
        ::FillRect(
            device,
            &remainder,
            reinterpret_cast<HBRUSH>(brush));
    } while (y >= 0);

    ::EndPaint(nativeWindow, &painter);
#endif
}

// // L2Authd.exe 0x004656D0, Src/log.cpp:165-174.
// void CLog::SetDirectory(const char* directory) {
//     const std::time_t currentTime = std::time(nullptr)
//     const std::tm now =
//         runtime::crt::LocalTimeOrZero(
//             currentTime)
//     std::strcpy(logDirectory, directory)
//     logFile = OpenAppend(
//         JoinLogPath(
//             logDirectory,
//             HourlyFileName(now, fileExt)))
//     prevDay = now.tm_mday
//     prevHour = now.tm_hour
// }

// L2Authd.exe 0x004656D0, Src/log.cpp:165-174.
void CLog::SetDirectory(const char* directory) {
    L2_GUARD(criticalSection);
    if (!directory || std::strlen(directory) >= sizeof(logDirectory)) {
        StorageFailure("set_directory", fileExt, ENAMETOOLONG);
        return;
    }
    std::strcpy(logDirectory, *directory ? directory : ".");
    prevDay = prevHour = -1;
    const auto now = runtime::crt::LocalTimeOrZero(std::time(nullptr));
    if (ReplaceLogFile(logFile, JoinLogPath(logDirectory, HourlyFileName(now, fileExt)), fileExt)) {
        // prevDay = now.tm_mday
        prevDay = runtime::logging::FileDateKey(now);
        prevHour = now.tm_hour;
    }
}

// // L2Authd.exe 0x004657D0, Src/log.cpp:177-182.
// CFileLog::CFileLog(const char* extension) {
//     authd::platform::InitializeCriticalSectionAndSpinCount(
//         criticalSection,
//         4000)
//     logFile = nullptr
//     logDirectory[0] = '\0'.
//     std::strcpy(fileExt, extension)
// }

// L2Authd.exe 0x004657D0, Src/log.cpp:177-182.
CFileLog::CFileLog(const char* extension) {
    authd::platform::InitializeCriticalSectionAndSpinCount(
        criticalSection,
        4000);
    logFile = nullptr;
    logDirectory[0] = '\0';
    std::snprintf(fileExt, sizeof(fileExt), "%s", extension ? extension : "log");
    prevDay = prevHour = -1;
}

// L2Authd.exe 0x00465830, Src/log.cpp:185-190.
CFileLog::~CFileLog() {
    authd::platform::DeleteCriticalSection(
        criticalSection);
    // CloseFile(logFile)
    CloseFile(logFile, fileExt);
}

// // L2Authd.exe 0x00465870, Src/log.cpp:193-209.
// void CFileLog::SetDirectory(const char* directory) {
//     const std::time_t currentTime = std::time(nullptr)
//     const std::tm now =
//         runtime::crt::LocalTimeOrZero(
//             currentTime)
//     std::strcpy(logDirectory, directory)
// 
//     const std::string path = JoinLogPath(
//         logDirectory,
//         HourlyFileName(now, fileExt))
//     logFile = OpenAppend(path)
//     if (logFile == nullptr) {
//         Log.AddLog(
//             LOG_ERROR,
//             "Log type '%s'(%s) Open Fail",
//             fileExt,
//             path.c_str())
//     } else {
//         std::fseek(
//             static_cast<std::FILE*>(logFile),
//             0,
//             SEEK_END)
//     }
// 
//     prevDay = now.tm_mday
//     prevHour = now.tm_hour
// }

// L2Authd.exe 0x00465870, Src/log.cpp:193-209.
void CFileLog::SetDirectory(const char* directory) {
    L2_GUARD(criticalSection);
    if (!directory || std::strlen(directory) >= sizeof(logDirectory)) {
        StorageFailure("set_directory", fileExt, ENAMETOOLONG);
        return;
    }
    // Remember the requested destination even if opening fails, so AddLog
    // retries after the directory/disk recovers. Keep any usable old stream.
    std::strcpy(logDirectory, *directory ? directory : ".");
    prevDay = prevHour = -1;
    const auto now = runtime::crt::LocalTimeOrZero(std::time(nullptr));
    // Log.AddLog(LOG_ERROR, "Log type '%s'(%s) Open Fail", fileExt, path.c_str())
    if (ReplaceLogFile(logFile, JoinLogPath(logDirectory, HourlyFileName(now, fileExt)), fileExt)) {
        // prevDay = now.tm_mday
        prevDay = runtime::logging::FileDateKey(now);
        prevHour = now.tm_hour;
    }
}

// // L2Authd.exe 0x004659D0, Src/log.cpp:212-257.
// void CFileLog::AddLog(
//     [[maybe_unused]] LogType type,
//     const char* format,
//     ...) {
//     const std::time_t currentTime = std::time(nullptr)
//     const std::tm now =
//         runtime::crt::LocalTimeOrZero(
//             currentTime)
// 
//     authd::platform::EnterCriticalSection(
//         criticalSection)
// 
//     if (logFile == nullptr &&
//         std::strlen(logDirectory) != 0) {
//         const std::string path = JoinLogPath(
//             logDirectory,
//             HourlyFileName(now, fileExt))
//         logFile = OpenAppend(path)
//         if (logFile == nullptr) {
//             Log.AddLog(
//                 LOG_ERROR,
//                 "Log type '%s'(%s) Open Fail",
//                 fileExt,
//                 path.c_str())
//         } else {
//             std::fseek(
//                 static_cast<std::FILE*>(logFile),
//                 0,
//                 SEEK_END)
//         }
//     }
// 
//     if ((now.tm_mday != prevDay ||
//          now.tm_hour != prevHour) &&
//         logFile != nullptr) {
//         const std::string path = JoinLogPath(
//             logDirectory,
//             HourlyFileName(now, fileExt))
//         void* const newFile = OpenTruncated(path)
//         void* const oldFile =
//             runtime::raw_atomic::ExchangePointer(
//                 &logFile,
//                 newFile)
//         CloseFile(oldFile)
//         prevDay = now.tm_mday
//         prevHour = now.tm_hour
//     }
// 
//     authd::platform::LeaveCriticalSection(
//         criticalSection)
// 
//     std::array<char, 0x4401> text{}
//     va_list arguments.
//     va_start(arguments, format)
//     const int formattedLength = std::vsnprintf(
//         text.data(),
//         0x4400,
//         format,
//         arguments)
//     va_end(arguments)
//     text[0x4400] = '\0'.
// 
//     if (formattedLength > 0 &&
//         formattedLength < 0x4400) {
//         void* const file =
//             runtime::raw_atomic::CompareExchangePointer(
//                 &logFile,
//                 static_cast<void*>(nullptr),
//                 static_cast<void*>(nullptr))
//         WriteFile(
//             file,
//             text.data(),
//             std::strlen(text.data()))
//     }
// }

// L2Authd.exe 0x004659D0, Src/log.cpp:212-257.
void CFileLog::AddLog([[maybe_unused]] LogType type, const char* format, ...) {
    const runtime::logging::StorageErrorState preserveErrors;
    const char* sourceFile = runtime::logging::SourceFileScope::Consume();
    // Only diagnostic files participate: accounting bytes and LogD stay intact.
    if (type == LOG_DEBUG &&
        (std::strcmp(fileExt, "packet") == 0 || std::strcmp(fileExt, "error") == 0) &&
        !runtime::logging::authDebugLoggingEnabled.load(std::memory_order_relaxed)) return;
    std::array<char, 0x4401> text{};
    va_list arguments;
    va_start(arguments, format);
    const int length = FormatFileRecord(text.data(), text.size(), fileExt, format, arguments);
    va_end(arguments);
    if (length <= 0) return;
    L2_GUARD(criticalSection);
    const auto now = runtime::crt::LocalTimeOrZero(std::time(nullptr));
    // if (logDirectory[0] && (!logFile || now.tm_mday != prevDay || now.tm_hour != prevHour)) {
    if (logDirectory[0] && (!logFile || prevDay < 0 || runtime::logging::FileDateKey(now) != prevDay || now.tm_hour != prevHour)) {
        // Log.AddLog(LOG_ERROR, "Log type '%s'(%s) Open Fail", fileExt, path.c_str())
        if (ReplaceLogFile(logFile, JoinLogPath(logDirectory, HourlyFileName(now, fileExt)), fileExt)) {
            // prevDay = now.tm_mday
            prevDay = runtime::logging::FileDateKey(now);
            prevHour = now.tm_hour;
        }
    }
    // WriteFile(file, text.data(), strlen(text.data())). (after releasing the section)
    // Diagnostic-only extensions. Other CFileLog users keep accounting bytes.
    if (std::strcmp(fileExt, "packet") == 0 || std::strcmp(fileExt, "error") == 0) {
        // const auto record = runtime::logging::Format("authd", OperatorLevel(type),
        //     std::string_view(text.data(), static_cast<std::size_t>(length)))
        const auto record = runtime::logging::Format("authd", OperatorLevel(type),
            std::string_view(text.data(), static_cast<std::size_t>(length)), sourceFile);
        // Noise: Format suppresses blank diagnostics. Do not pass an empty
        // record to WriteFile: a zero-byte write/flush must not report recovery
        // from a prior write failure without writing a diagnostic record.
        if (record.length == 0) return;
        WriteFile(logFile, record.bytes.data(), record.length, fileExt);
    } else {
        WriteFile(logFile, text.data(), static_cast<std::size_t>(length), fileExt);
    }
}

// L2Authd.exe 0x00465CC0, Src/log.cpp:261-269.
CLogdFilelog::CLogdFilelog(const char* extension)
    : CFileLog(extension) {
    const std::time_t currentTime = std::time(nullptr);
    const std::tm now =
        runtime::crt::LocalTimeOrZero(
            currentTime);
    prevMin = now.tm_min >= 30 ? 1 : 0;
}

// L2Authd.exe 0x00465D30, Src/log.cpp:272-276.
CLogdFilelog::~CLogdFilelog() = default;

// // L2Authd.exe 0x00465D50, Src/log.cpp:322-344.
// void CLogdFilelog::SetDirectory(const char* directory) {
//     const std::time_t currentTime = std::time(nullptr)
//     const std::tm now =
//         runtime::crt::LocalTimeOrZero(
//             currentTime)
//     std::strcpy(logDirectory, directory)
//     prevMin = now.tm_min >= 30 ? 1 : 0
// 
//     const std::string path = JoinLogPath(
//         logDirectory,
//         LogdFileName(now, prevMin, fileExt))
//     logFile = OpenAppend(path)
//     if (logFile != nullptr) {
//         std::fseek(
//             static_cast<std::FILE*>(logFile),
//             0,
//             SEEK_END)
//     }
//     prevDay = now.tm_mday
//     prevHour = now.tm_hour
// }

// L2Authd.exe 0x00465D50, Src/log.cpp:322-344.
void CLogdFilelog::SetDirectory(const char* directory) {
    L2_GUARD(criticalSection);
    if (!directory || std::strlen(directory) >= sizeof(logDirectory)) {
        StorageFailure("set_directory", fileExt, ENAMETOOLONG);
        return;
    }
    std::strcpy(logDirectory, *directory ? directory : ".");
    prevDay = prevHour = -1;
    const auto now = runtime::crt::LocalTimeOrZero(std::time(nullptr));
    const int halfHour = now.tm_min >= 30 ? 1 : 0;
    if (ReplaceLogFile(logFile, JoinLogPath(logDirectory, LogdFileName(now, halfHour, fileExt)), fileExt)) {
        // prevDay = now.tm_mday
        prevDay = runtime::logging::FileDateKey(now);
        prevHour = now.tm_hour;
        prevMin = halfHour;
    }
}

// // L2Authd.exe 0x00465F00, Src/log.cpp:279-319.
// void CLogdFilelog::AddLog(
//     [[maybe_unused]] LogType type,
//     const char* format,
//     ...) {
//     const std::time_t currentTime = std::time(nullptr)
//     const std::tm now =
//         runtime::crt::LocalTimeOrZero(
//             currentTime)
//     const int halfHour = now.tm_min >= 30 ? 1 : 0
// 
//     authd::platform::EnterCriticalSection(
//         criticalSection)
//     if (now.tm_mday != prevDay ||
//         now.tm_hour != prevHour ||
//         halfHour != prevMin) {
//         const std::string path = JoinLogPath(
//             logDirectory,
//             LogdFileName(now, halfHour, fileExt))
//         void* const newFile = OpenTruncated(path)
//         void* const oldFile =
//             runtime::raw_atomic::ExchangePointer(
//                 &logFile,
//                 newFile)
//         CloseFile(oldFile)
//         prevDay = now.tm_mday
//         prevHour = now.tm_hour
//         prevMin = halfHour
//     }
//     authd::platform::LeaveCriticalSection(
//         criticalSection)
// 
//     std::array<char, 1024> text{}
//     va_list arguments.
//     va_start(arguments, format)
//     const int formattedLength = LegacyVFormat(
//         text.data(),
//         format,
//         arguments)
//     va_end(arguments)
// 
//     if (formattedLength > 0) {
//         void* const file =
//             runtime::raw_atomic::CompareExchangePointer(
//                 &logFile,
//                 static_cast<void*>(nullptr),
//                 static_cast<void*>(nullptr))
//         WriteFile(
//             file,
//             text.data(),
//             std::strlen(text.data()))
//     }
// }

// L2Authd.exe 0x00465F00, Src/log.cpp:279-319.
void CLogdFilelog::AddLog([[maybe_unused]] LogType type, const char* format, ...) {
    const runtime::logging::StorageErrorState preserveErrors;
    // The CFileLog adapter calls virtually. Consume its pending metadata
    // here too, but never prefix the LogD accounting record with a source name
    // or let nested storage diagnostics inherit the caller's source.
    (void)runtime::logging::SourceFileScope::Consume();
    std::array<char, 0x4401> text{};
    va_list arguments;
    va_start(arguments, format);
    // LegacyVFormat(text.data(), format, arguments). Used unbounded vsprintf.
    const int length = FormatFileRecord(text.data(), text.size(), fileExt, format, arguments);
    va_end(arguments);
    if (length <= 0) return;
    L2_GUARD(criticalSection);
    const auto now = runtime::crt::LocalTimeOrZero(std::time(nullptr));
    const int halfHour = now.tm_min >= 30 ? 1 : 0;
    // if (logDirectory[0] && (!logFile || now.tm_mday != prevDay || now.tm_hour != prevHour || halfHour != prevMin)) {
    if (logDirectory[0] && (!logFile || prevDay < 0 || runtime::logging::FileDateKey(now) != prevDay || now.tm_hour != prevHour || halfHour != prevMin)) {
        if (ReplaceLogFile(logFile, JoinLogPath(logDirectory, LogdFileName(now, halfHour, fileExt)), fileExt)) {
            // prevDay = now.tm_mday
            prevDay = runtime::logging::FileDateKey(now);
            prevHour = now.tm_hour;
            prevMin = halfHour;
        }
    }
    // WriteFile(file, text.data(), strlen(text.data())). (after releasing the section)
    WriteFile(logFile, text.data(), static_cast<std::size_t>(length), fileExt);
}
