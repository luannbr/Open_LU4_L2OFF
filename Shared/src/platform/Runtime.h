// runtime.h  -  portable runtime layer for the reconstructed server processes.
// Replaces the Win32/MSVC facilities the original CacheD used (IOCP GUI app) with
// portable std:: equivalents.
#pragma once
#include <cstdint>
#include <mutex>
#include "Shared/src/Runtime/TickCount32.h"
#include "Shared/src/Runtime/Time32.h"

namespace runtime {

// ---- portable SYSTEMTIME (was Win32 _SYSTEMTIME) ----------------------------
struct SystemTime {
    uint16_t wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
};
inline SystemTime GetLocalTimeNow() { return crt::LocalSystemTimeNow<SystemTime>(); }

// ---- monotonic tick (was GetTickCount) --------------------------------------
inline uint32_t GetTickCount() { return TickCount32(); }

// ---- global engine lock: serializes DB / shared game-state (g_UserDb maps,
//      CObjectMaps, DBConn pool use) between network/IO worker threads and the
//      console thread. Recursive so a locked path may re-enter (dispatch within
//      a console command). This is the portable stand-in for the reconstructed
//      per-map RWLocks guarding concurrent access. ---------------------------
std::recursive_mutex& EngineLock();

} // namespace runtime
