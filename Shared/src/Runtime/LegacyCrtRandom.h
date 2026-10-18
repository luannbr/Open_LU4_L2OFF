#pragma once

#include <cstdint>

// Cross-platform carrier for the per-thread rand/srand implementation linked
// into the selected MSVC executables. The names remain global because the
// recovered Shared consumers already call them as replacements for CRT entry
// points, not as members of a product-specific runtime.
void LegacyCrtSrand(std::uint32_t seed);
int LegacyCrtRand();
