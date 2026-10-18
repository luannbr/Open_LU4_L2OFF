#include "Shared/src/Runtime/LegacyCrtRandom.h"

#include <cstdint>

namespace {

// The selected MSVC CRTs keep this stream per thread and initialize it to one.
// AuthGateD.exe exposes the older form directly at 0x0041B13F/0x0041B14C:
// both functions fetch _ptiddata and use its seed field at +0x14. Keeping the
// state beside the two exported operations also prevents consumers from
// pulling unrelated process-specific Utils.cpp dependencies.
thread_local std::uint32_t g_crtRandomState = 1U;

} // namespace

void LegacyCrtSrand(std::uint32_t seed) { g_crtRandomState = seed; }

int LegacyCrtRand() {
    g_crtRandomState = g_crtRandomState * 214013U + 2531011U;
    return static_cast<int>((g_crtRandomState >> 16) & 0x7fffU);
}
