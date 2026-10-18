#pragma once

#include "Shared/src/Runtime/SourceLog.h"

#include "Platform/CriticalSection.h"

#include <cstdint>
#include <cstdio>
#include <atomic>

struct HWND__;
struct HFONT__;
struct HBRUSH__;

// L2Authd.pdb type 0x60F0.
enum LogType : std::int32_t {
    LOG_NORMAL = 0,
    LOG_DEBUG = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3,
};

// L2Authd.pdb type 0x7212, sizeof 0x208.
struct LogLine {
    LogType type;
    char str[512];
    std::int32_t len;
};

struct AuthDRect32 {
    std::int32_t left;
    std::int32_t top;
    std::int32_t right;
    std::int32_t bottom;
};

struct AuthDSize32 {
    std::int32_t cx;
    std::int32_t cy;
};

// Original owner: Src/log.h. The selected 32-bit PDB records a 0x168-byte
// target layout. Native pointers/vptrs use the host width in the
// functional cross-platform class. Field names and order follow PDB 0x7049.
class CLog {
public:
    CLog();
    CLog(int lineCount, const char* extension);
    virtual ~CLog();

    void SetWnd(HWND__* window);
    void Enable(bool enable);
    void AddLog(LogType type, const char* format, ...);
    void Resize(int right, int bottom);
    void Redraw();
    void SetDirectory(const char* directory);

private:
    HWND__* wnd;
    bool enabled;
    std::int32_t nLine;
    std::int32_t tail;
    LogLine* lines;
    authd::platform::CriticalSectionStorage32 criticalSection;
    AuthDRect32 clientRect;
    [[maybe_unused]] AuthDSize32 fontSize;
    HFONT__* font;
    [[maybe_unused]] HBRUSH__* brush;
    std::FILE* logFile;
    std::int32_t prevDay;
    std::int32_t prevHour;
    char fileExt[8];
    char logDirectory[260];
};

// Original owner: Src/log.h. PDB 0x60F7 records sizeof 0x134 on Win32.
class CFileLog {
public:
    explicit CFileLog(const char* extension);
    ~CFileLog();

    void SetDirectory(const char* directory);
    void Enable(bool enable);
    virtual void AddLog(
        LogType type,
        const char* format,
        ...);

protected:
    authd::platform::CriticalSectionStorage32 criticalSection;
    void* logFile;
    std::int32_t prevDay;
    std::int32_t prevHour;
    char fileExt[8];
    char logDirectory[260];
};

// Original owner: Src/log.h. PDB 0x736E records sizeof 0x138 on Win32.
class CLogdFilelog : public CFileLog {
public:
    explicit CLogdFilelog(const char* extension);
    ~CLogdFilelog();

    void SetDirectory(const char* directory);
    void AddLog(
        LogType type,
        const char* format,
        ...) override;

private:
    std::int32_t prevMin;
};

extern CLog Log;
extern CFileLog fileLog;
extern CFileLog actionLog;
extern CFileLog errLog;
extern CFileLog packetLog;
extern CFileLog billingLog;
extern CFileLog blockLog;
extern CFileLog dualLog;
extern CLogdFilelog logdfileLog;
extern CFileLog UserUseLog;


namespace runtime::logging {

// Debug logs start enabled. Keep this setting outside logger objects.
inline std::atomic<bool> authDebugLoggingEnabled{true};

// template<class... Args>
// inline void LogAt(CLog& logger, LogType type, const char* source,
//                   const char* format, Args&&... arguments) {
//     const SourceFileScope location(source)
//     logger.AddLog(type, format, std::forward<Args>(arguments)...)
// }
template<class... Args>
inline void LogAt(CLog& logger, LogType type, const char* source,
                  const char* format, Args... arguments) {
    const SourceFileScope location(source);
    logger.AddLog(type, format, arguments...);
}

// File-only diagnostics use the same source metadata as console diagnostics.
// CFileLog applies it only to diagnostic extensions, never accounting rows.
template<class... Args>
inline void LogAt(CFileLog& logger, LogType type, const char* source,
                  const char* format, Args... arguments) {
    const SourceFileScope location(source);
    logger.AddLog(type, format, arguments...);
}

} // namespace runtime::logging
