#pragma once

#include "Shared/src/Runtime/SourceLog.h"

#include "AuthGateDTypes.h"
#include "Lock.h"

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>

// Original owner: FileLog.h/FileLog.cpp. AuthGateD.pdb type 0x12B7 records
// the 0x288-byte Win32 object. The portable FILE carrier replaces only the
// Win32 HANDLE at +0x44.
class CFileLog {
public:
    CFileLog(
        const char16_t* fileExtension,
        int logPeriod,
        bool stdioOutput);
    ~CFileLog();

    CFileLog(const CFileLog&) = delete;
    CFileLog& operator=(const CFileLog&) = delete;

    void SetDirectory(const char16_t* directory);
    void Log(
        LogType type,
        const char16_t* format,
        ...);
    void LogT(
        LogType type,
        const char16_t* format,
        ...);

    // Both shipping entry points fold to the one-byte RET at 0x004179F0.
    void LogDbg(
        LogType type,
        const char16_t* format,
        ...);
    void LogDbgT(
        LogType type,
        const char16_t* format,
        ...);

    static CFileLog* g_pDefaultFileLog;

protected:
    void WriteLog(
        LogType type,
        bool timestamp,
        const char16_t* format,
        va_list arguments);

private:
    CLock m_lock;
    std::FILE* m_logFile = nullptr;
    std::int32_t m_prevDay = 0;
    std::int32_t m_prevHour = 0;
    std::array<char16_t, 20> m_fileExt{};
    std::array<char16_t, 260> m_logDirectory{};
    std::int32_t m_logPeriod = 0;
    bool m_bStdioOutput = false;
};

// Original AuthGateD globals contributed by authgated.obj. The shipping
// first symbol is named `log`. POSIX libc reserves that identifier for
// log(double), so only its portable C++ spelling changes.
extern CFileLog authGateLog;
extern CFileLog logdLog;
extern CFileLog botLog;

namespace runtime::logging {

// Portable diagnostic policy, outside the recovered CFileLog layout.
// Preserve the previous active-route default. Shipping LogDbg(T) stays a no-op.
inline std::atomic<bool> authGateDebugLoggingEnabled{true};

// template<class... Args>
// inline void LogAt(CFileLog& logger, LogType type, const char* source,
//                   const char16_t* format, Args&&... arguments) {
//     const SourceFileScope location(source)
//     logger.Log(type, format, std::forward<Args>(arguments)...)
// }
template<class... Args>
inline void LogAt(CFileLog& logger, LogType type, const char* source,
                  const char16_t* format, Args... arguments) {
    const SourceFileScope location(source);
    logger.Log(type, format, arguments...);
}

} // namespace runtime::logging
