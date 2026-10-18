#include "Shared/src/Utils.h"

#include "Shared/src/platform/Runtime.h"

#include <chrono>
#include <cstdint>
#include <random>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

std::uint64_t PerformanceCounterSeed() {
    // QueryPerformanceCounter supplies the target's raw MT19937-64 seed.
#if defined(_WIN32)
    LARGE_INTEGER counter;
    ::QueryPerformanceCounter(&counter);
    return static_cast<std::uint64_t>(counter.QuadPart);
#else
    // A monotonic high-resolution counter is the POSIX transport peer.
    return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

// Current L2NPC 0x005452f4..0x00545534 and current L2Server
// 0x00c3c080..0x00c3c2c0 emit the same normalized
// MT19937-64 seed/twist/temper/real1/real2 bodies. Their TLS images store
// mti=313 (NN+1), the first call on an unseeded thread therefore
// uses the reference seed 5489. The CRT per-thread rand state starts at one.
// MAIN and every I/O worker overwrite these defaults at the exact startup
// sites recovered below. LISTEN keeps them.
thread_local std::mt19937_64 g_mt19937_64{std::mt19937_64::default_seed};

} // namespace

void InitializeRandomNumber() {
    // Current Cached.exe 0x005c87b8 has the same worker-entry sequence:
    // GetTickCount -> CRT srand, QueryPerformanceCounter -> init_genrand64.
    // Current L2NPC.exe 0x00545564 seeds the CRT stream from GetTickCount,
    // then samples QueryPerformanceCounter once for the MT19937-64 stream.
    // Keep the two clocks and their call order distinct.
    LegacyCrtSrand(runtime::GetTickCount());
    const std::uint64_t performanceCounter = PerformanceCounterSeed();
    init_genrand64(performanceCounter);
}

void init_genrand64(std::uint64_t seed) {
    // Current Cached.exe 0x005c86f8 creates the reference 312-word
    // MT19937-64 seed recurrence in the thread-local state image.
    g_mt19937_64.seed(seed);
}

std::uint64_t genrand64_int64() { return g_mt19937_64(); }

double genrand64_real1() { return static_cast<double>(genrand64_int64() >> 11) * (1.0 / 9007199254740991.0); }

double genrand64_real2() { return static_cast<double>(genrand64_int64() >> 11) * (1.0 / 9007199254740992.0); }

double genrand64_real3() { return (static_cast<double>(genrand64_int64() >> 12) + 0.5) * (1.0 / 4503599627370496.0); }
