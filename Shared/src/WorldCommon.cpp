#include "Shared/src/World.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/GeoData.h"

#include <cstring>
#include <new>

namespace l2geo {
namespace {

int ZoneFromWorld(int value, std::uint32_t bias) {
    const std::uint32_t bits = static_cast<std::uint32_t>(value) + bias;
    std::uint32_t shifted = bits >> 15;
    if ((bits & 0x80000000u) != 0) {
        shifted |= 0xffff0000u;
    }
    std::int32_t signedShifted = 0;
    std::memcpy(&signedShifted, &shifted, sizeof(signedShifted));
    return signedShifted + 10;
}

} // namespace

CWorld::CWorld(bool isTestWorld) : m_IsTestWorld(isTestWorld), m_geo(nullptr) {
    L2_GUARD_FUNCTION();
    // Noise: Constructing the world object does not mean that geodata or
    // the world is ready. The startup coordinator reports loading progress.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"CWorld::Inst() Created")
}

CWorld::~CWorld() {
    if (m_geo && !m_IsTestWorld) {
        delete m_geo;
    }
}

CWorld& CWorld::Inst() {
    static CWorld world(false);
    return world;
}

void CWorld::InitGeo() {
    L2_GUARD_FUNCTION();
    m_geo = new (std::nothrow) CGeoData(false);
}

bool CWorld::IsValidPosition(int x, int y) {
    CGeoData* const geodata = GetGeo();
    if (!geodata) {
        return false;
    }
    return geodata->GetZone(ZoneFromWorld(x, 0x50000u), ZoneFromWorld(y, 0x40000u)) != nullptr;
}

} // namespace l2geo
