#include "GMSetting.h"

#include <cstdint>
#include <cstring>

bool g_bGmNoResOn = false;
int g_GmNoRestartDistance = 0;
FVector g_GmNoRestartPosition{};
FVector g_GmRestartPosition{};

bool IsGmNoRestartPosition(FVector position) {
    if (!g_bGmNoResOn) {
        return false;
    }

    const double x = position.x - g_GmNoRestartPosition.x;
    const double y = position.y - g_GmNoRestartPosition.y;
    const std::uint32_t distanceSquareBits =
        static_cast<std::uint32_t>(g_GmNoRestartDistance) * static_cast<std::uint32_t>(g_GmNoRestartDistance);
    std::int32_t distanceSquare = 0;
    static_assert(sizeof(distanceSquare) == sizeof(distanceSquareBits));
    std::memcpy(&distanceSquare, &distanceSquareBits, sizeof(distanceSquare));
    return x * x + y * y < static_cast<double>(distanceSquare);
}

FVector GetGMRestartPoint() { return g_GmRestartPosition; }
