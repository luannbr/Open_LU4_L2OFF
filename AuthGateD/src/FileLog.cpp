#include "FileLog.h"

#include "Shared/src/ScopedLockGuards.h"

#include "Platform/LegacyWideFormat.h"

#include "Shared/src/Runtime/WindowsAnsi.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/OperatorLog.h"
#include "Shared/src/Runtime/LogStorage.h"
#include "TextRuntime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <climits>
#include <cstddef>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

CFileLog* CFileLog::g_pDefaultFileLog = nullptr;

namespace {

constexpr std::size_t kLineCapacity = 1024;

template <std::size_t Capacity>
void CopyTargetString(
    std::array<char16_t, Capacity>& destination,
    const char16_t* source) noexcept {
    destination.fill(u'\0');
    if (!source) return;
    std::size_t index = 0;
    while (index + 1U < Capacity && source[index] != u'\0') {
        destination[index] = source[index];
        ++index;
    }
}

template <std::size_t Capacity>
std::u16string FixedString(
    const std::array<char16_t, Capacity>& source) {
    const auto end = std::find(source.begin(), source.end(), u'\0');
    return std::u16string(source.begin(), end);
}

std::string NativePath(std::u16string path) {
    std::string result = Utf16ToUtf8(path);
#if !defined(_WIN32)
    std::replace(result.begin(), result.end(), '\\', '/');
#endif
    return result;
}

std::string FileName(
    const std::array<char16_t, 260>& directory,
    const std::array<char16_t, 20>& extension,
    int period,
    const std::tm& now) {
    std::array<char, 96> leaf{};
    if (period == 0) {
        std::snprintf(
            leaf.data(),
            leaf.size(),
            "%04d-%02d-%02d.%s",
            now.tm_year + 1900,
            now.tm_mon + 1,
            now.tm_mday,
            portable::WindowsAnsiFromUtf16(
                FixedString(extension)).c_str());
    } else {
        std::snprintf(
            leaf.data(),
            leaf.size(),
            "%04d-%02d-%02d.%02d.%s",
            now.tm_year + 1900,
            now.tm_mon + 1,
            now.tm_mday,
            now.tm_hour,
            portable::WindowsAnsiFromUtf16(
                FixedString(extension)).c_str());
    }

    const std::string root = NativePath(FixedString(directory));
    return root.empty()
        ? std::string(leaf.data())
        : (std::filesystem::path(root) / leaf.data()).string();
}

bool AppendLimited(
    std::u16string& output,
    std::u16string_view value,
    std::size_t capacity) {
    if (value.size() > capacity - std::min(capacity, output.size())) {
        return false;
    }
    output.append(value);
    return true;
}

std::string AsciiFormat(
    std::string_view flags,
    int width,
    int precision,
    std::string_view length,
    char conversion) {
    std::string result("%");
    result.append(flags);
    if (width >= 0) result.append(std::to_string(width));
    if (precision >= 0) {
        result.push_back('.');
        result.append(std::to_string(precision));
    }
    result.append(length);
    result.push_back(conversion);
    return result;
}

template <typename Value>
std::u16string FormatScalar(
    const std::string& format,
    Value value) {
    std::array<char, 256> local{};
    const int count = std::snprintf(
        local.data(),
        local.size(),
        format.c_str(),
        value);
    if (count < 0) return {};
    if (static_cast<std::size_t>(count) < local.size()) {
        std::u16string result;
        result.reserve(static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index) {
            result.push_back(static_cast<char16_t>(
                static_cast<unsigned char>(local[index])));
        }
        return result;
    }

    std::string dynamic(
        static_cast<std::size_t>(count) + 1U,
        '\0');
    std::snprintf(
        dynamic.data(),
        dynamic.size(),
        format.c_str(),
        value);
    std::u16string result;
    result.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        result.push_back(static_cast<char16_t>(
            static_cast<unsigned char>(dynamic[static_cast<std::size_t>(index)])));
    }
    return result;
}

std::u16string ApplyStringField(
    std::u16string value,
    std::string_view flags,
    int width,
    int precision) {
    if (precision >= 0 &&
        value.size() > static_cast<std::size_t>(precision)) {
        value.resize(static_cast<std::size_t>(precision));
    }
    if (width <= 0 ||
        value.size() >= static_cast<std::size_t>(width)) {
        return value;
    }
    const std::size_t padding =
        static_cast<std::size_t>(width) - value.size();
    if (flags.find('-') != std::string_view::npos) {
        value.append(padding, u' ');
    } else {
        value.insert(0, padding, u' ');
    }
    return value;
}

enum class TargetLength {
    None,
    Char,
    Short,
    Long32,
    LongLong,
    Int32,
    Int64,
    Pointer,
    IntMax,
    Size,
    PtrDiff,
    LongDouble,
    Wide,
};

bool VFormatTargetWide(
    std::u16string& output,
    std::size_t capacity,
    const char16_t* format,
    va_list sourceArguments) {
    if (!format) return false;
    va_list arguments;
    va_copy(arguments, sourceArguments);
    // Formatting allocates strings. Release the copied list on exceptions too.
    struct ArgumentsCleanup {
        va_list& value;
        ~ArgumentsCleanup() { va_end(value); }
    } cleanup{arguments};

    bool success = true;
    for (std::size_t offset = 0;
         success && format[offset] != u'\0';) {
        if (format[offset] != u'%') {
            success = AppendLimited(
                output,
                std::u16string_view(format + offset, 1),
                capacity);
            ++offset;
            continue;
        }
        ++offset;
        if (format[offset] == u'%') {
            success = AppendLimited(output, u"%", capacity);
            ++offset;
            continue;
        }

        std::string flags;
        while (format[offset] == u'-' ||
               format[offset] == u'+' ||
               format[offset] == u' ' ||
               format[offset] == u'#' ||
               format[offset] == u'0') {
            flags.push_back(static_cast<char>(format[offset++]));
        }

        int width = -1;
        if (format[offset] == u'*') {
            width = va_arg(arguments, int);
            ++offset;
            if (width < 0) {
                width = width == INT_MIN ? INT_MAX : -width;
                if (flags.find('-') == std::string::npos) {
                    flags.push_back('-');
                }
            }
        } else if (format[offset] >= u'0' && format[offset] <= u'9') {
            width = 0;
            while (format[offset] >= u'0' && format[offset] <= u'9') {
                const int digit = format[offset++] - u'0';
                width = width > (INT_MAX - digit) / 10
                    ? INT_MAX
                    : width * 10 + digit;
            }
        }

        int precision = -1;
        if (format[offset] == u'.') {
            ++offset;
            if (format[offset] == u'*') {
                precision = va_arg(arguments, int);
                ++offset;
                if (precision < 0) precision = -1;
            } else {
                precision = 0;
                while (format[offset] >= u'0' && format[offset] <= u'9') {
                    const int digit = format[offset++] - u'0';
                    precision = precision > (INT_MAX - digit) / 10
                        ? INT_MAX
                        : precision * 10 + digit;
                }
            }
        }

        // Reject pathological widths before scalar/string padding allocates.
        if (width > 0 && static_cast<std::size_t>(width) > capacity) {
            success = false;
            break;
        }
        if (precision > 0 && static_cast<std::size_t>(precision) > capacity) {
            success = false;
            break;
        }

        TargetLength length = TargetLength::None;
        if (format[offset] == u'h') {
            ++offset;
            length = TargetLength::Short;
            if (format[offset] == u'h') {
                ++offset;
                length = TargetLength::Char;
            }
        } else if (format[offset] == u'l') {
            ++offset;
            length = TargetLength::Long32;
            if (format[offset] == u'l') {
                ++offset;
                length = TargetLength::LongLong;
            }
        } else if (format[offset] == u'I') {
            ++offset;
            if (format[offset] == u'6' && format[offset + 1] == u'4') {
                offset += 2;
                length = TargetLength::Int64;
            } else if (format[offset] == u'3' && format[offset + 1] == u'2') {
                offset += 2;
                length = TargetLength::Int32;
            } else {
                length = TargetLength::Pointer;
            }
        } else if (format[offset] == u'j') {
            ++offset;
            length = TargetLength::IntMax;
        } else if (format[offset] == u'z') {
            ++offset;
            length = TargetLength::Size;
        } else if (format[offset] == u't') {
            ++offset;
            length = TargetLength::PtrDiff;
        } else if (format[offset] == u'L') {
            ++offset;
            length = TargetLength::LongDouble;
        } else if (format[offset] == u'w') {
            ++offset;
            length = TargetLength::Wide;
        }

        const char16_t conversionWide = format[offset];
        if (conversionWide == u'\0') {
            success = false;
            break;
        }
        ++offset;
        const char conversion = static_cast<char>(conversionWide);
        std::u16string field;

        if (conversion == 's' || conversion == 'S') {
            const bool narrow =
                (conversion == 'S' &&
                 length != TargetLength::Wide &&
                 length != TargetLength::Long32) ||
                (conversion == 's' &&
                 (length == TargetLength::Short ||
                  length == TargetLength::Char));
            if (narrow) {
                field = portable::Utf16FromWindowsAnsi(
                    va_arg(arguments, const char*),
                    u"(null)");
            } else {
                const char16_t* value =
                    va_arg(arguments, const char16_t*);
                field = value ? std::u16string(value) : u"(null)";
            }
            field = ApplyStringField(
                std::move(field), flags, width, precision);
        } else if (conversion == 'c' || conversion == 'C') {
            const int value = va_arg(arguments, int);
            const bool narrow =
                conversion == 'C' &&
                length != TargetLength::Wide &&
                length != TargetLength::Long32;
            if (narrow) {
                const char byte[2]{static_cast<char>(value), '\0'};
                field = portable::Utf16FromWindowsAnsi(
                    byte,
                    u"(null)");
            } else {
                field.push_back(static_cast<char16_t>(value));
            }
            field = ApplyStringField(
                std::move(field), flags, width, -1);
        } else if (conversion == 'd' || conversion == 'i') {
            const bool wideInteger =
                length == TargetLength::LongLong ||
                length == TargetLength::Int64 ||
                length == TargetLength::IntMax;
            const std::string scalarFormat = AsciiFormat(
                flags,
                width,
                precision,
                wideInteger ? "ll" : "",
                conversion);
            field = wideInteger
                ? FormatScalar(
                      scalarFormat,
                      va_arg(arguments, long long))
                : FormatScalar(
                      scalarFormat,
                      va_arg(arguments, int));
        } else if (conversion == 'u' ||
                   conversion == 'o' ||
                   conversion == 'x' ||
                   conversion == 'X') {
            const bool wideInteger =
                length == TargetLength::LongLong ||
                length == TargetLength::Int64 ||
                length == TargetLength::IntMax;
            const std::string scalarFormat = AsciiFormat(
                flags,
                width,
                precision,
                wideInteger ? "ll" : "",
                conversion);
            field = wideInteger
                ? FormatScalar(
                      scalarFormat,
                      va_arg(arguments, unsigned long long))
                : FormatScalar(
                      scalarFormat,
                      va_arg(arguments, unsigned int));
        } else if (conversion == 'f' || conversion == 'F' ||
                   conversion == 'e' || conversion == 'E' ||
                   conversion == 'g' || conversion == 'G' ||
                   conversion == 'a' || conversion == 'A') {
            const bool longDouble = length == TargetLength::LongDouble;
            const std::string scalarFormat = AsciiFormat(
                flags,
                width,
                precision,
                longDouble ? "L" : "",
                conversion);
            field = longDouble
                ? FormatScalar(
                      scalarFormat,
                      va_arg(arguments, long double))
                : FormatScalar(
                      scalarFormat,
                      va_arg(arguments, double));
        } else if (conversion == 'p') {
            field = FormatScalar(
                AsciiFormat(flags, width, precision, "", conversion),
                va_arg(arguments, void*));
        } else if (conversion == 'n') {
            const std::size_t count = output.size();
            if (length == TargetLength::LongLong ||
                length == TargetLength::Int64) {
                if (auto* value = va_arg(arguments, long long*)) {
                    *value = static_cast<long long>(count);
                }
            } else if (length == TargetLength::Short) {
                if (auto* value = va_arg(arguments, short*)) {
                    *value = static_cast<short>(count);
                }
            } else if (length == TargetLength::Char) {
                if (auto* value = va_arg(arguments, signed char*)) {
                    *value = static_cast<signed char>(count);
                }
            } else {
                if (auto* value = va_arg(arguments, int*)) {
                    *value = static_cast<int>(count);
                }
            }
        } else {
            success = false;
        }

        if (success && conversion != 'n') {
            success = AppendLimited(output, field, capacity);
        }
    }

    // va_end(arguments). // Now owned by ArgumentsCleanup on every exit.
    return success;
}

std::u16string Prefix(LogType type) {
    switch (type) {
    case LOG_DEBUG:
        return u"[DBG] ";
    case LOG_WARN:
        return u"[WARN] ";
    case LOG_ERROR:
        return u"[ERR] ";
    case LOG_DBG_PACKET:
        return u"[PKT] ";
    case LOG_SYSTEM:
    case LOG_NORMAL:
        return {};
    }
    return {};
}


// Noise: One global counter hid first failures of unrelated files. Repeats are now grouped by complete path, operation, sink and errno.
// void ReportStorageFailure(const char* operation, int error, const char* path) noexcept {
//     static std::atomic<unsigned long long> failures{0}
//     const auto count = failures.fetch_add(1, std::memory_order_relaxed) + 1
//     if (count != 0 && (count & (count - 1)) != 0) return.
//     std::array<char, 512> message{}
//     // std::snprintf(message.data(), message.size(),
//     //     "component=logging event=storage_failure operation=%s errno=%d failures_total=%llu path=\"%s\". Check log directory permissions and free disk space",
//     //     operation, error, count, path)
//     std::snprintf(message.data(), message.size(),
//         "[logging] Log operation %s failed (errno %d, %llu storage failures in total, path \"%s\"). Check permissions and free disk space.",
//         operation, error, count, path)
//     runtime::logging::Write("authgated", runtime::logging::Level::Error, message.data())
// }
void ReportStorageFailure(const char* operation, int error, const char* path) noexcept {
    runtime::logging::ReportLogStorageFailure("authgated", operation, "", error, path);
}

runtime::logging::Level OperatorLevel(LogType type) noexcept {
    using runtime::logging::Level;
    return type == LOG_ERROR ? Level::Error : type == LOG_WARN ? Level::Warning :
        type == LOG_DEBUG || type == LOG_DBG_PACKET ? Level::Debug : Level::Info;
}

// void WritePortableConsole(const std::u16string& line) {
//     // The target forwards this line to CWindowMgr::AddLine when it is not
//     // running as a Windows service. A terminal is the non-GUI host transport.
//     // CWindowMgr keeps ownership of Win32 colors and message-loop dispatch.
//     std::clog << Utf16ToUtf8(line) << '\n'
// }
// Noise: This helper only mirrored logd/bot accounting records to the console.
// Their files keep the complete records. Operator messages use WriteRecord.
// void WritePortableConsole(LogType type, std::u16string_view message) {
//     // The target forwards this line to CWindowMgr::AddLine when it is not
//     // running as a Windows service. A terminal is the non-GUI host transport.
//     // CWindowMgr keeps ownership of Win32 colors and message-loop dispatch.
//     // std::clog << Utf16ToUtf8(line) << '\n'
//     using runtime::logging::Level.
//     const Level level = type == LOG_ERROR ? Level::Error :
//         type == LOG_WARN ? Level::Warning :
//         type == LOG_DEBUG || type == LOG_DBG_PACKET ? Level::Debug : Level::Info.
//     runtime::logging::Write("authgated", level, Utf16ToUtf8(message))
// }

} // namespace

bool authgated::portable::FormatTargetWide(
    std::u16string& output,
    std::size_t capacity,
    const char16_t* format,
    va_list arguments) {
    output.clear();
    return VFormatTargetWide(
        output,
        capacity,
        format,
        arguments);
}

// AuthGateD.exe 0x0040DF70, FileLog.cpp.
CFileLog::CFileLog(
    const char16_t* fileExtension,
    int logPeriod,
    bool stdioOutput)
    : m_lock(u"CFileLog::m_lock", true),
      m_logPeriod(logPeriod),
      m_bStdioOutput(stdioOutput) {
    CopyTargetString(m_fileExt, fileExtension);
}

// AuthGateD.exe 0x0040DF50, FileLog.cpp.
// CFileLog::~CFileLog() {
//     if (m_logFile) std::fclose(m_logFile)
// }

// CFileLog::~CFileLog() {
//     if (m_logFile && std::fclose(m_logFile) != 0)
//         ReportStorageFailure("close", errno, "")
// }
CFileLog::~CFileLog() {
    const runtime::logging::StorageErrorState preserveErrors;
    const auto path = runtime::logging::SnapshotLogFilePath(m_logFile);
    // A close failure need not set errno. Do not attribute an earlier error to it.
    errno = 0;
    if (m_logFile && std::fclose(m_logFile) != 0)
        ReportStorageFailure("close", errno, path.data());
    else if (m_logFile)
        runtime::logging::ReportLogStorageSuccess("authgated", "close", "", path.data());
}

// AuthGateD.exe 0x0040DFB0, FileLog.cpp.
// void CFileLog::SetDirectory(const char16_t* directory) {
//     CopyTargetString(m_logDirectory, directory)
// }

void CFileLog::SetDirectory(const char16_t* directory) {
    if (directory) {
        std::size_t length = 0;
        while (length < m_logDirectory.size() && directory[length]) ++length;
        if (length == m_logDirectory.size()) {
            ReportStorageFailure("set_directory", ENAMETOOLONG, "");
            return;
        }
    }
    L2_GUARD(m_lock);
    CopyTargetString(m_logDirectory, directory);
    m_prevDay = m_prevHour = -1;
}

// AuthGateD.exe 0x0040DFD0, FileLog.cpp.
// void CFileLog::WriteLog(
//     LogType type,
//     bool timestamp,
//     const char16_t* format,
//     va_list arguments) {
//     const auto clockNow = std::chrono::system_clock::now()
//     const std::time_t timeNow =
//         std::chrono::system_clock::to_time_t(clockNow)
//     const std::tm now =
//         runtime::crt::LocalTimeOrZero(
//             timeNow)
//     const std::string fileName = FileName(
//         m_logDirectory,
//         m_fileExt,
//         m_logPeriod,
//         now)
// 
//     m_lock.Lock()
//     const bool rotate =
//         !m_logFile ||
//         (m_logPeriod == 0 && now.tm_mday != m_prevDay) ||
//         (m_logPeriod == 1 &&
//          (now.tm_mday != m_prevDay || now.tm_hour != m_prevHour))
//     if (rotate) {
//         std::FILE* const replacement =
//             std::fopen(fileName.c_str(), "ab")
//         std::FILE* const previous = m_logFile
//         m_logFile = replacement
//         if (previous) std::fclose(previous)
//         m_prevDay = now.tm_mday
//         m_prevHour = now.tm_hour
//     }
//     m_lock.Unlock()
// 
//     std::u16string line
//     line.reserve(kLineCapacity)
//     if (timestamp) {
//         const auto milliseconds =
//             std::chrono::duration_cast<std::chrono::milliseconds>(
//                 clockNow.time_since_epoch()) % 1000.
//         std::array<char, 32> value{}
//         std::snprintf(
//             value.data(),
//             value.size(),
//             "%02d:%02d:%02d.%03d ",
//             now.tm_hour,
//             now.tm_min,
//             now.tm_sec,
//             static_cast<int>(milliseconds.count()))
//         for (const char character : std::string(value.data())) {
//             line.push_back(static_cast<char16_t>(character))
//         }
//     }
//     const std::size_t messageOffset = line.size() + Prefix(type).size()
//     if (!AppendLimited(line, Prefix(type), kLineCapacity - 1U) ||
//         !VFormatTargetWide(
//             line,
//             kLineCapacity - 1U,
//             format,
//             arguments)) {
//         return.
//     }
// 
//     // if (m_bStdioOutput) WritePortableConsole(line)
//     if (m_bStdioOutput) WritePortableConsole(type, std::u16string_view(line).substr(messageOffset))
//     if (!AppendLimited(line, u"\r\n", kLineCapacity - 1U)) return.
// 
//     std::string encoded = portable::WindowsAnsiFromUtf16(line)
//     // AuthGateD.exe 0x0040E2B2 computes WriteFile's byte count from the
//     // UTF-16 remaining-capacity value, not from StringCchPrintfA's ACP byte
//     // count. Preserve that observable truncation for multibyte CP_ACP text.
//     encoded.resize(line.size(), '\0')
// 
//     // The Win32 target releases the rotation lock before WriteFile, allowing
//     // another thread to close the exchanged handle underneath this write.
//     // Serializing only the host FILE access is the portable resource-safety
//     // correction. Formatting, rotation policy and written bytes are unchanged.
//     m_lock.Lock()
//     if (m_logFile) {
//         std::fwrite(encoded.data(), 1, encoded.size(), m_logFile)
//         std::fflush(m_logFile)
//     }
//     m_lock.Unlock()
// }

void CFileLog::WriteLog(
    LogType type,
    bool timestamp,
    const char16_t* format,
    va_list arguments) {
    const runtime::logging::StorageErrorState preserveErrors;
    const char* sourceFile = runtime::logging::SourceFileScope::Consume();
    // These two explicit owners store accounting/GameGuard records. Their
    // field layout and encoding remain separate from operator diagnostics.
    const auto extension = FixedString(m_fileExt);
    const bool machineRecord = extension == u"logd" || extension == u"bot";
    if (!machineRecord && (type == LOG_DEBUG || type == LOG_DBG_PACKET) &&
        !runtime::logging::authGateDebugLoggingEnabled.load(std::memory_order_relaxed)) return;
    const std::size_t capacity = machineRecord ? kLineCapacity - 3U : 4095U;
    std::u16string message;
    message.reserve(capacity);
    if (!VFormatTargetWide(message, capacity, format, arguments)) {
        std::array<char, 256> error{};
        // std::snprintf(error.data(), error.size(),
        //     "component=logging event=record_rejected sink=%s reason=invalid_format_or_oversized capacity_utf16=%zu",
        //     Utf16ToUtf8(extension).c_str(), capacity)
        std::snprintf(error.data(), error.size(),
            "[logging] Rejected %s record: invalid format or size exceeds the %zu UTF-16-unit buffer.",
            Utf16ToUtf8(extension).c_str(), capacity);
        // runtime::logging::Write("authgated", runtime::logging::Level::Error, error.data())
        runtime::logging::Write("authgated", runtime::logging::Level::Error, error.data(), sourceFile);
        return;
    }
    // const auto record = runtime::logging::Format("authgated", OperatorLevel(type), Utf16ToUtf8(message))
    const auto record = runtime::logging::Format("authgated", OperatorLevel(type), Utf16ToUtf8(message), machineRecord ? "" : sourceFile);
    if (!machineRecord && record.length == 0) return;

    L2_GUARD(m_lock);
    const auto clockNow = std::chrono::system_clock::now();
    const auto now = runtime::crt::LocalTimeOrZero(std::chrono::system_clock::to_time_t(clockNow));
    const auto fileName = FileName(m_logDirectory, m_fileExt, m_logPeriod, now);
    // const bool rotate = !m_logFile || m_prevDay < 0 ||
    //     (m_logPeriod == 0 && now.tm_mday != m_prevDay) ||
    //     (m_logPeriod == 1 && (now.tm_mday != m_prevDay || now.tm_hour != m_prevHour))
    const auto dateKey = runtime::logging::FileDateKey(now);
    const bool rotate = !m_logFile || m_prevDay < 0 ||
        (m_logPeriod == 0 && dateKey != m_prevDay) ||
        (m_logPeriod == 1 && (dateKey != m_prevDay || now.tm_hour != m_prevHour));
    if (rotate) {
        errno = 0;
        auto* replacement = std::fopen(fileName.c_str(), "ab");
        if (replacement) {
            auto* previous = m_logFile;
            m_logFile = replacement;
            // if (previous && std::fclose(previous) != 0)
            //     ReportStorageFailure("close", errno, fileName.c_str())
            const auto previousPath = runtime::logging::SnapshotLogFilePath(previous);
            errno = 0;
            if (previous && std::fclose(previous) != 0)
                ReportStorageFailure("close", errno, previousPath.data());
            else if (previous)
                runtime::logging::ReportLogStorageSuccess("authgated", "close", "", previousPath.data());
            // m_prevDay = now.tm_mday
            m_prevDay = dateKey;
            m_prevHour = now.tm_hour;
            runtime::logging::ReportLogStorageSuccess("authgated", "open", "", fileName.c_str());
        } else {
            ReportStorageFailure("open", errno, fileName.c_str());
        }
    }

    if (machineRecord) {
        std::u16string line;
        if (timestamp) {
            const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(clockNow.time_since_epoch()) % 1000;
            std::array<char, 32> value{};
            std::snprintf(value.data(), value.size(), "%02d:%02d:%02d.%03d ",
                now.tm_hour, now.tm_min, now.tm_sec, static_cast<int>(millis.count()));
            for (const char c : std::string(value.data())) line.push_back(static_cast<char16_t>(c));
        }
        if (!AppendLimited(line, Prefix(type), kLineCapacity - 3U) ||
            !AppendLimited(line, message, kLineCapacity - 3U)) {
            // runtime::logging::Write("authgated", runtime::logging::Level::Error,
            //     "component=logging event=record_rejected reason=oversized_accounting_record")
            // runtime::logging::Write("authgated", runtime::logging::Level::Error,
            //     "[logging] Rejected accounting record: timestamp, prefix and message exceed the record buffer.")
            std::array<char, 256> error{};
            std::snprintf(error.data(), error.size(),
                "[logging] Rejected %s record: timestamp, prefix and message exceed the %zu UTF-16-unit buffer. The record was not written.",
                extension == u"bot" ? "bot" : "logd",
                kLineCapacity - 3U);
            runtime::logging::Write("authgated", runtime::logging::Level::Error,
                error.data(), sourceFile);
            return;
        }
        line.append(u"\r\n");
        std::string encoded = portable::WindowsAnsiFromUtf16(line);
        encoded.resize(line.size(), '\0');
        // if (m_logFile && (std::fwrite(encoded.data(), 1, encoded.size(), m_logFile) != encoded.size() ||
        //                   std::fflush(m_logFile) != 0))
        //     ReportStorageFailure("write", errno, fileName.c_str())
        errno = 0;
        if (m_logFile && (std::fwrite(encoded.data(), 1, encoded.size(), m_logFile) != encoded.size() ||
                          std::fflush(m_logFile) != 0)) {
            const auto path = runtime::logging::SnapshotLogFilePath(m_logFile);
            ReportStorageFailure("write", errno, path.data());
        } else if (m_logFile) {
            runtime::logging::ReportLogWriteSuccess("authgated", "", m_logFile);
        }
    } else if (m_logFile && !runtime::logging::WriteRecord(m_logFile, record)) {
        // ReportStorageFailure("write", errno, fileName.c_str())
        const auto path = runtime::logging::SnapshotLogFilePath(m_logFile);
        ReportStorageFailure("write", errno, path.data());
    } else if (m_logFile) {
        runtime::logging::ReportLogWriteSuccess("authgated", "", m_logFile);
    }
    // Noise: Dedicated logd/bot records belong in their accounting files.
    // the console should not repeat their CSV fields or GameGuard payloads.
    // if (m_bStdioOutput) {
    //     if (machineRecord) WritePortableConsole(type, message)
    //     else runtime::logging::WriteRecord(stdout, record)
    // }
    if (m_bStdioOutput && !machineRecord) {
        // runtime::logging::WriteRecord(stdout, record)
        runtime::logging::WriteConsoleRecord(record);
    }
}

// AuthGateD.exe 0x0040E350, FileLog.cpp.
void CFileLog::Log(
    LogType type,
    const char16_t* format,
    ...) {
    va_list arguments;
    va_start(arguments, format);
    // WriteLog(type, false, format, arguments)
    try {
        WriteLog(type, false, format, arguments);
    } catch (...) {
        va_end(arguments);
        throw;
    }
    va_end(arguments);
}

// AuthGateD.exe 0x0040E370, FileLog.cpp.
void CFileLog::LogT(
    LogType type,
    const char16_t* format,
    ...) {
    va_list arguments;
    va_start(arguments, format);
    // WriteLog(type, true, format, arguments)
    try {
        WriteLog(type, true, format, arguments);
    } catch (...) {
        va_end(arguments);
        throw;
    }
    va_end(arguments);
}

// AuthGateD.exe 0x004179F0, folded FileLog.cpp entry.
void CFileLog::LogDbg(
    [[maybe_unused]] LogType type,
    [[maybe_unused]] const char16_t* format,
    ...) {}

// AuthGateD.exe 0x004179F0, folded FileLog.cpp entry.
void CFileLog::LogDbgT(
    [[maybe_unused]] LogType type,
    [[maybe_unused]] const char16_t* format,
    ...) {}
