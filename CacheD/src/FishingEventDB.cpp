#include "FishingEventDB.h"

#include "L2Core/src/inc/Guard.h"
#include "Portable/ProcessRuntime.h"

#include <array>
#include <cstddef>

// These element sizes are donor-correlated only. They cannot affect current
// production behavior because every current reference audit leaves all three
// vectors empty for the complete process lifetime.
class PrizeItem {
private:
    [[maybe_unused]] std::array<std::byte, 0x0c> targetData_{};
};

struct RankingEntry {
    std::array<std::byte, 0x3c> targetData{};
};


CFishingEventDB g_fishingEventDb;

CFishingEventDB::CFishingEventDB() {
    // Current Cached.exe 0x004d51ac. C++ member initialization already gives
    // the exact target order: three empty xvectors, then four RWLocks.
    L2_GUARD_FUNCTION();
}

CFishingEventDB::~CFishingEventDB() {
    // Current Cached.exe 0x004d5078. The body guard precedes reverse member
    // destruction, matching locks +0x108..+0x60 and vectors +0x40..+0x00.
    L2_GUARD_FUNCTION();
}
