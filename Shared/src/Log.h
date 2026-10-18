#pragma once

#include "Shared/src/Runtime/SourceLog.h"

#include "Runtime.h"
#include "Shared/src/LogCli.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/ThreadManager.h"

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>

struct HDC__;
struct HWND__;

// shared/log.h in the target declares this exact CLog channel enum.
enum LogType : int {
    LOG_NORMAL = 0,
    LOG_WARN = 1,
    LOG_ERROR = 2,
    LOG_DEBUG = 3,
    LOG_INNER = 4,
    LOG_OUTER = 5,
    LOG_BOTH = 6,
    LOG_DB = 7,
    LOG_AUDIT = 8,
    LOG_CHAT = 9,
    LOG_LOCAL = 10,
    LOG_OPT_ERROR = 11,
    LOG_LONG_KEEP = 12,
    LOG_NCGUARD = 13,
    LOG_MAX = 14,
};

// PDB global contributed by NPCServer.obj. It is used only by the L2NPC
// process-specific compilation of Shared/log.cpp.
extern int g_nFileDuration;

class CLog;

// Current CLogLines records are shared with the Windows status window. The
// target WCHAR array is represented as UTF-16 so the mapped
// record remains 0x7d8 bytes on systems where wchar_t is four bytes.
struct LogLine {
    LogType type = LOG_NORMAL;
    std::array<char16_t, 1000> str{};
    int len = 0;
};


class CLogLines {
public:
    virtual ~CLogLines();

    unsigned GetRowCapacity() const;
    int GetIndex(int index) const;
    void TextOutW(HDC__* deviceContext, int y, int index) const;
    void WriteLog(LogType type, wchar_t* text, int length);
    unsigned GetTail() const;

    void WriteLogUtf16(LogType type, const char16_t* text, int length);

protected:
    CLogLines();
    explicit CLogLines(unsigned rowCapacity);

    void SetNextTail();

    LogLine* m_lines;
    unsigned* m_tail;

private:
    unsigned m_rowCapacity;
    // MSVC keeps this tail padding as part of the CLogLines base subobject.
    // making it explicit prevents the Itanium ABI from placing a derived
    // field at +0x1c instead of the PDB-confirmed +0x20.
    std::uint32_t m_rowCapacityPadding;
};


// PDB class used when the console ring does not need inter-process transport.
class CNormalLogLines final : public CLogLines {
public:
    explicit CNormalLogLines(unsigned rowCapacity);
    ~CNormalLogLines() override;

private:
    unsigned m_realTail;
};


// The 2018 L2NPC global CLog is constructed with this class and the mapping
// name Global\CONSOLE_LOG_L2NPC. Windows uses a named file mapping. POSIX
// builds publish the identical LogLine/tail byte layout through shm_open.
class CSharedLogLines final : public CLogLines {
public:
    CSharedLogLines(unsigned rowCapacity, const wchar_t* mappingName);
    ~CSharedLogLines() override;

private:
    void* m_linesHandle;
    void* m_tailHandle;
};


namespace runtime::log {

// A printf format reads a char16_t pointer as a char or wchar_t string. That
// is wrong on every platform where wchar_t is not 2 bytes. UTF-16 text must
// use a u"" format, which formats typed arguments.
template <class Argument>
inline constexpr bool kIsUtf16TextPointer =
    std::is_pointer_v<Argument> && std::is_same_v<std::remove_cv_t<std::remove_pointer_t<Argument>>, char16_t>;

template <class... Arguments>
inline constexpr bool kHasNoUtf16TextPointer = !(kIsUtf16TextPointer<Arguments> || ...);


// Portable diagnostic switch, independent of LogD reconnection/configuration.
// L2NPC continues to use its existing ScriptDebugLog setting.
void SetDebugLoggingEnabled(bool enabled) noexcept;

struct LogOptions {
    std::string logDAddress{"127.0.0.1"};
    int logDPort = 3999;
    int worldId = 0;
    // The target uses the fixed "log" root. Making the root configurable is
    // the portable deployment extension. All category/file names below it
    // keep the target convention.
    std::string localRoot{"log"};
    int fileDurationMinutes = 30;
    // Portable presentation sink. CLog remains the only logger and keeps
    // the target LogD/local-file/shared-buffer routing. This merely mirrors
    // its already formatted output to stdout.
    bool consoleOutput = true;
};

void ConfigureLog(CLog& log, LogOptions options);
// Portable continuation of the current CLogFormatter -> wide CLog::Add path.
// It accepts an already formatted target UTF-16 record so the process adapter
// can keep LOG_BOTH/LOG_AUDIT without adding a method to shipping CLog.
void WriteLogFormatterLine(CLog& log, LogType type, const std::u16string& line);
// ERROR on the console/GUI and normal error route, with an additional copy
// in the existing LOG_LOCAL file category. No second console/GUI record.
void WriteErrorWithLocalCopy(CLog& log, const char* format, ...);
// POSIX replacement for a modal error: one bounded operator-console record,
// available before CLog initialization and independent of its file/LogD sinks.
void WriteConsoleErrorV(const wchar_t* format, std::va_list arguments);
void StartLogReceiving(CLog& log);
void StopLogTransport(CLog& log);

} // namespace runtime::log

// Opaque Win64 layouts kept inside the target CLog prefix. Portable
// synchronization and file objects live behind the implementation boundary.
// these bytes keep every shipping field at its observed 2018 offset.
struct alignas(8) CLogCriticalSectionLayout {
    std::array<std::byte, 40> bytes;
};

struct CLogRectLayout {
    std::int32_t left;
    std::int32_t top;
    std::int32_t right;
    std::int32_t bottom;
};

struct CLogSizeLayout {
    std::int32_t cx;
    std::int32_t cy;
};


// Cross-platform recovery of the current target CLog. Its complete 0x1690
// object layout is kept here. Only Win32 handles, critical sections and GDI
// drawing are replaced behind the implementation boundary.
class CLog {
public:
    explicit CLog(CLogLines* logLines = nullptr);
    ~CLog();

    CLog(const CLog&) = delete;
    CLog& operator=(const CLog&) = delete;

    // Exact current L2NPC.exe public surface.
    void Init(int fileDuration, int worldId);
    void Set(HWND__* window);
    void Enable(bool value);
    void Sendable(bool value);
    void Supress(bool value);
    bool IsSupressed() const;
    bool IsSendable() const;
    void ConnectToLogD();
    void SuspendConnect();
    void ResumeConnect();
    void FlushLogFile();
    void CreateNewLogFile(int year, int month, int day, int hour, int half);
    void Add(LogType type, char* format, ...);
    void Add(LogType type, wchar_t* format, ...);
    void AddV(LogType type, char* format, va_list arguments);
    void AddV(LogType type, wchar_t* format, va_list arguments);
    void Resize(int width, int height);
    void Redraw();
    void CloseLogFile();
    void Enter();
    void Leave();
    bool ValidLogFile(int year, int month, int day, int hour, int half) const;
    void SetLogFileInfo(int world, int type);
    void SetLogFileDate(int year, int month, int day, int hour, int half);
    void LogFileLock(int index);
    void LogFileUnLock(int index);
    int GetConnectTick() const;
    void SetConnectTick();

    // Const-format convenience overloads do not change the target ABI and
    // let recovered literals use the exact Add path without const_cast at
    // each call site.
    void Add(LogType type, const char* format, ...);
    void Add(LogType type, const wchar_t* format, ...);

    // Shared/log.h in the target provides typed forwarding overloads named
    // AddT. The instantiated machine code keeps every argument's real
    // type and then enters the same variadic Add path.
    template <typename... ArgumentTypes>
    void AddT(LogType type, const wchar_t* format, ArgumentTypes... arguments) {
        static_assert(
            runtime::log::kHasNoUtf16TextPointer<ArgumentTypes...>, "Use a u\"\" format for UTF-16 text arguments."
        );
        Add(type, format, arguments...);
    }

    // UTF-16 form of the wide Add. The format follows the original printf
    // rules, see Runtime/Utf16Format.h, and the arguments are typed. The
    // text then takes the same route as a wide record.
    template <typename... ArgumentTypes>
    void Add(LogType type, std::u16string_view format, const ArgumentTypes&... arguments) {
        AddUtf16(type, format, FormatUtf16(format, arguments...));
    }
    template <typename... ArgumentTypes>
    void AddT(LogType type, std::u16string_view format, const ArgumentTypes&... arguments) {
        AddUtf16(type, format, FormatUtf16(format, arguments...));
    }
    void AddUtf16(LogType type, std::u16string_view format, const std::u16string& text);

private:
    friend void runtime::log::ConfigureLog(CLog& log, runtime::log::LogOptions options);
    friend void runtime::log::WriteLogFormatterLine(CLog& log, LogType type, const std::u16string& line);
    friend void runtime::log::WriteErrorWithLocalCopy(CLog& log, const char* format, ...);
    friend void runtime::log::StartLogReceiving(CLog& log);
    friend void runtime::log::StopLogTransport(CLog& log);
    friend bool LogCheckStatus(CLogSocket* socket, const std::uint8_t* packet);

    void AddTypedUtf16(int logType, const std::u16string& line, bool spaceAfterTimestampComma);
    // Portable diagnostic route. The original three-argument entry remains.
    void AddTypedUtf16(int logType, const std::u16string& line, bool spaceAfterTimestampComma, bool copyErrorLocally);
    void AddTypedUtf16(
        int logType, const std::u16string& line, bool spaceAfterTimestampComma, bool copyErrorLocally, const char* sourceFile
    );
    void WriteOverflow(
        LogType type, int formatResult, const std::u16string& buffer, const std::u16string& format, int id, bool wideInput
    );
    void WriteLocal(int logType, const std::string& timestampedLine, const runtime::SystemTime& time);

    // Current L2NPC.exe field prefix. Offsets are established by current
    // machine code. The older PDB stops at nine file channels and therefore
    // is not used for the suffix offsets.
    HWND__* wnd; // +0x0000
    bool enabled; // +0x0008
    bool sendable; // +0x0009
    bool supressed; // +0x000a
    std::array<std::byte, 5> m_flagPadding; // +0x000b
    // The target used std::auto_ptr<CLogLines>. unique_ptr is its modern
    // single-owner replacement and keeps the same 8-byte field layout.
    std::unique_ptr<CLogLines> m_logLines; // +0x0010
    CLogCriticalSectionLayout m_section; // +0x0018
    CLogRectLayout clientRect; // +0x0040
    CLogSizeLayout fontSize; // +0x0050
    void* font; // +0x0058
    void* brush; // +0x0060
    std::uint32_t m_nConnectThread; // +0x0068
    std::uint32_t m_connectThreadPadding; // +0x006c
    void* m_hConnectThread; // +0x0070
    std::int32_t m_nYear; // +0x0078
    std::int32_t m_nMonth; // +0x007c
    std::int32_t m_nDay; // +0x0080
    std::int32_t m_nHour; // +0x0084
    std::int32_t m_nHalf; // +0x0088
    std::int32_t m_nWorld; // +0x008c
    std::int32_t m_nType; // +0x0090
    std::uint32_t m_fileHandlePadding; // +0x0094
    std::array<std::uintptr_t, 10> m_hLogFile; // +0x0098
    std::array<CLogCriticalSectionLayout,
        10> m_FileSection; // +0x00e8
    std::array<std::array<char16_t, 256>,
        10> m_szFileName; // +0x0278
    std::int32_t m_nFileDuration; // +0x1678
    std::int32_t m_nWorldId; // +0x167c

public:
    bool m_bDirty; // +0x1680
    std::array<std::byte, 3> m_dirtyPadding; // +0x1681
    std::uint32_t m_nConnectTick; // +0x1684
    bool m_StopWhenError; // +0x1688
    bool m_InTesting; // +0x1689
    std::array<std::byte, 6> m_tailPadding; // +0x168a
};


// Process-global CLog at current L2NPC.exe DAT_019f45f0.
extern CLog Log;

// Exact global name immediately preceding CLog in both images. L2NPC uses 12
// logical slots and Cached.exe uses 22. The portable process-common header
// reserves the larger current capacity while each image touches only its own
// TLS range.
extern volatile bool g_OptionalErrorLogEnabled;
extern int g_bOnLogging[ThreadID::MAX];

// Target log.h lines 698-727. VS2005 recorded the literal-facing parameter
// as char*. Const keeps the same call ABI while remaining valid modern C++.
inline void LOG(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    // Log.AddV(
    //     LOG_NORMAL,
    //     const_cast<char*>(format),
    //     arguments)
    try {
        Log.AddV(LOG_NORMAL, const_cast<char*>(format), arguments);
    } catch (...) {
        va_end(arguments);
        throw;
    }
    va_end(arguments);
}

namespace runtime::logging {

// template<class Char, class... Args>
// inline void LogAt(CLog& logger, LogType type, const char* source,
//                   const Char* format, Args&&... arguments) {
//     const SourceFileScope location(source)
//     logger.Add(type, format, std::forward<Args>(arguments)...)
// }
template <class Char, class... Args>
inline void LogAt(CLog& logger, LogType type, const char* source, const Char* format, Args... arguments) {
    if constexpr (!std::is_same_v<Char, char16_t>) {
        static_assert(runtime::log::kHasNoUtf16TextPointer<Args...>, "Use a u\"\" format for UTF-16 text arguments.");
    }
    const SourceFileScope location(source);
    logger.Add(type, format, arguments...);
}

// Keep the mandatory local copy and the normal ERROR route in one formatted call.
template <class... Args>
inline void LogErrorWithLocalCopyAt(CLog& logger, const char* source, const char* format, Args... arguments) {
    static_assert(runtime::log::kHasNoUtf16TextPointer<Args...>, "Use a u\"\" format for UTF-16 text arguments.");
    const SourceFileScope location(source);
    ::runtime::log::WriteErrorWithLocalCopy(logger, format, arguments...);
}

} // namespace runtime::logging

#define L2_LOG_ERROR_WITH_LOCAL_COPY(logger, ...)                                                                           \
    ::runtime::logging::LogErrorWithLocalCopyAt((logger), __FILE__, __VA_ARGS__)

inline void LOGE(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    // Log.AddV(
    //     LOG_ERROR,
    //     const_cast<char*>(format),
    //     arguments)
    try {
        Log.AddV(LOG_ERROR, const_cast<char*>(format), arguments);
    } catch (...) {
        va_end(arguments);
        throw;
    }
    va_end(arguments);
}

inline void LOGW(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    // Log.AddV(
    //     LOG_WARN,
    //     const_cast<char*>(format),
    //     arguments)
    try {
        Log.AddV(LOG_WARN, const_cast<char*>(format), arguments);
    } catch (...) {
        va_end(arguments);
        throw;
    }
    va_end(arguments);
}
