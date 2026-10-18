#include "Shared/src/GeoData.h"
#include "Shared/src/Runtime/Utf8.h"

#include "L2Core/src/inc/Guard.h"
#if defined(L2_BUILD_L2SERVER)
#include "CConfig.h"
#include "CWorld.h"
#include "MapBug.h"
#include "RestrictArea.h"
#include "Shared/src/Exception.h"
#else
#include "L2NPC/src/Config.h"
#endif
#include "Shared/src/Runtime/LittleEndian.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/platform/Runtime.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/GeoDataMapping.h"
#include "Shared/src/Runtime/GeoDataSource.h"
#include "Shared/src/Runtime/ResourceDirectories.h"
#include "Shared/src/Territory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/World.h"

#if defined(L2_BUILD_L2SERVER)
#include "CWorldObject.h"
#include "WorldCollision.h"
#endif

#include <array>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <iterator>
#include <new>

int maxHorizonLength = 0;
int maxVerticalLength = 0;
CTerritory* pMaxHorizonTerritory = nullptr;
CTerritory* pMaxVerticalTerritory = nullptr;
int totalloopCount = 0;
std::uint32_t profile1 = 0;
std::uint32_t profile2 = 0;
std::uint32_t profile3 = 0;
std::uint32_t profile4 = 0;
std::uint32_t profile5 = 0;
std::uint32_t profile6 = 0;
std::uint32_t profile7 = 0;
int callCount1 = 0;
int callCount2 = 0;
int callCount3 = 0;
int callCount4 = 0;
int callCount5 = 0;
int callCount6 = 0;
int callCount7 = 0;

using l2geo::portable::detail::MappedFile;
using l2geo::portable::detail::PortableGeoIndex;
using l2geo::portable::detail::PortableMappedZone;

const std::uint16_t GeoFieldMask[GEO_FIELD_MAX] = {
    0x8000u,
    0x4000u,
    0x2000u,
    0x1000u,
    0x0800u,
    0x0400u,
    0x0200u,
    0x0100u,
    0x0080u,
    0x0040u,
    0x0020u,
    0x0010u,
    0x0008u,
    0x0004u,
    0x0002u,
    0x0001u,
};

std::int16_t GeoHeightMask = runtime::Int16FromBits(0xfff0u);

namespace {

#if defined(L2_BUILD_L2SERVER)
using GeoDataWorld = ::CWorld;
#else
using GeoDataWorld = l2geo::CWorld;
#endif

constexpr std::uint16_t kOpenMask = 0x000fu;
constexpr std::array<std::uint16_t, 4> kDirectionMasks{{
    0x0001u,
    0x0002u,
    0x0004u,
    0x0008u,
}};
constexpr std::array<int, 4> kOppositeDirections{{1, 0, 3, 2}};
int ArithmeticShiftRightDword(std::uint32_t bits, unsigned shift) {
    std::uint32_t shifted = bits >> shift;
    if ((bits & 0x80000000u) != 0) {
        shifted |= ~std::uint32_t{0} << (32u - shift);
    }
    return runtime::Int32FromBits(shifted);
}

int ShiftWorldCoordinate(int value, std::uint32_t bias, unsigned shift) {
    return ArithmeticShiftRightDword(static_cast<std::uint32_t>(value) + bias, shift);
}

int GridCenter(int grid, std::uint32_t offset) {
    return runtime::Int32FromBits(static_cast<std::uint32_t>(grid) * 16u - offset);
}

int ZoneXFromWorld(int x) { return ShiftWorldCoordinate(x, 0x50000u, 15) + 10; }

int ZoneYFromWorld(int y) { return ShiftWorldCoordinate(y, 0x40000u, 15) + 10; }

int SectorXFromWorld(int x) { return ShiftWorldCoordinate(x, 0x50000u, 7) & 0xff; }

int SectorYFromWorld(int y) { return ShiftWorldCoordinate(y, 0x40000u, 7) & 0xff; }

int CellXFromWorld(int x) { return ShiftWorldCoordinate(x, 0x50000u, 4) & 7; }

int CellYFromWorld(int y) { return ShiftWorldCoordinate(y, 0x40000u, 4) & 7; }

int GridXFromWorld(int x) { return ShiftWorldCoordinate(x, 0x50000u, 4); }

int GridYFromWorld(int y) { return ShiftWorldCoordinate(y, 0x40000u, 4); }

int GridCenterX(int gridX) { return GridCenter(gridX, 0x4fff8u); }

int GridCenterY(int gridY) { return GridCenter(gridY, 0x3fff8u); }

int CellHeight(std::int16_t data) {
    const std::int16_t packed =
        runtime::Int16FromBits(static_cast<std::uint16_t>(data) & static_cast<std::uint16_t>(GeoHeightMask));
    return static_cast<int>(packed) / 2;
}

double DistanceSquared2D(const FVector& left, const FVector& right) {
    const double dx = left.x - right.x;
    const double dy = left.y - right.y;
    return dx * dx + dy * dy;
}

double DistanceSquared3D(const FVector& left, const FVector& right) {
    const double dx = left.x - right.x;
    const double dy = left.y - right.y;
    const double dz = left.z - right.z;
    return dx * dx + dy * dy + dz * dz;
}

// void LogMoveStraightError(int sourceLine) {
void LogMoveStraightError(const char* mode, const char* reason, int gridX, int gridY, int nextGridX, int nextGridY) {
    // Log.Add(LOG_ERROR, L"error in movestraight3! [%S][%d]", "..\\Shared\\GeoData.cpp", sourceLine)
    L2_LOG_ERROR(
        Log, "[geodata] %s movement check failed: %s (grid %d,%d to %d,%d). Returning through the movement failure path.",
        mode, reason, gridX, gridY, nextGridX, nextGridY
    );
}

} // namespace

CGeoSector::CGeoSector() noexcept :
    m_Field(0),
    m_DefaultCell{},
    m_TerritoryMinZ(0),
    m_TerritoryMaxZ(0),
    m_BooleanFlag(0),
    m_Alignment0{},
    cell_offset_index(0),
    cell_segment(0),
    num_cell(0),
    m_Alignment1{} {}

int CGeoSector::GetFirstCellIndex(int cellX, int cellY, std::int16_t* cellOffsets) {
    if ((m_BooleanFlag & 1u) != 0) {
        return 0;
    }
    if ((m_BooleanFlag & 2u) != 0) {
        return runtime::AddInt32Wrapping(
            runtime::AddInt32Wrapping(cell_segment, runtime::Int32FromBits(static_cast<std::uint32_t>(cellX) * 8u)),
            cellY
        );
    }
    if (cellX == 0 && cellY == 0) {
        return cell_segment;
    }
    return runtime::AddInt32Wrapping(
        cell_segment,
        static_cast<int>(
            cellOffsets[static_cast<std::ptrdiff_t>(cell_offset_index) + static_cast<std::ptrdiff_t>(cellX) * 8 + cellY - 1]
        )
    );
}

int CGeoSector::GetLastCellIndex(int cellX, int cellY, std::int16_t* cellOffsets) {
    if ((m_BooleanFlag & 1u) != 0) {
        return 0;
    }
    if ((m_BooleanFlag & 2u) != 0) {
        return runtime::AddInt32Wrapping(
            runtime::AddInt32Wrapping(
                runtime::AddInt32Wrapping(
                    cell_segment, runtime::Int32FromBits(static_cast<std::uint32_t>(cellX) * 8u)
                ),
                cellY
            ),
            1
        );
    }
    return runtime::AddInt32Wrapping(
        cell_segment,
        static_cast<int>(
            cellOffsets[static_cast<std::ptrdiff_t>(cell_offset_index) + static_cast<std::ptrdiff_t>(cellX) * 8 + cellY]
        )
    );
}

bool HelperFunction::IsInArea(const std::vector<CTerritory*>& areas, const FVector& position) {
    for (CTerritory* territory : areas) {
        if (territory && territory->IsInside(position, true)) {
            return true;
        }
    }
    return false;
}

CGeoSector* CGeoZone::GetSector(int sectorX, int sectorY) { return &m_data->sectors[sectorX][sectorY]; }

bool CGeoZone::Load(const wchar_t* fileName, CGeoCell* cells, std::int16_t* cellOffsets) {
    MappedFile file;
    std::string error;
    if (!file.Open(std::filesystem::path{fileName}, error) || file.Size() < 18) {
        return false;
    }

    const std::uint8_t* const data = file.Data();
    std::size_t cursor = 0;
    m_data->zone_x = static_cast<std::int8_t>(data[cursor++]);
    m_data->zone_y = static_cast<std::int8_t>(data[cursor++]);
    cursor += 4;
    m_data->num_cell = runtime::LoadLittleEndianInt32(data + cursor);
    cursor += 4;
    castleId = 0;
    const int nonComplexSectorCount = runtime::LoadLittleEndianInt32(data + cursor);
    cursor += 4;
    cursor += 4;

    const int complexSectorCount = 0x10000 - nonComplexSectorCount;
    const int cellOffsetCapacity = complexSectorCount * 64;
    int cellSegment = 0;
    int cellOffsetIndex = 0;

    for (int sectorX = 0; sectorX < 256; ++sectorX) {
        for (int sectorY = 0; sectorY < 256; ++sectorY) {
            if (cursor > file.Size() - 2) {
                return false;
            }

            CGeoSector& sector = m_data->sectors[sectorX][sectorY];
            sector.cell_segment = cellSegment;
            sector.cell_offset_index = cellOffsetIndex;
            sector.num_cell = runtime::LoadLittleEndianInt16(data + cursor);
            cursor += 2;

            int sectorCellCount = 0;
            if (sector.num_cell == 0) {
                if (cursor > file.Size() - 4) {
                    return false;
                }
                sector.m_BooleanFlag |= 1u;
                sector.m_BooleanFlag |= 2u;
                const std::int16_t height = runtime::LoadLittleEndianInt16(data + cursor);
                cursor += 4;
                std::uint16_t packed = static_cast<std::uint16_t>(sector.m_DefaultCell.m_data);
                packed |= static_cast<std::uint16_t>(static_cast<std::uint16_t>(height) << 1) &
                    static_cast<std::uint16_t>(GeoHeightMask);
                packed |= kOpenMask;
                sector.m_DefaultCell.m_data = static_cast<std::int16_t>(packed);
            } else if (sector.num_cell == 64) {
                if (cursor > file.Size() - 128 || cellSegment > m_data->num_cell - 64) {
                    return false;
                }
                sector.m_BooleanFlag |= 2u;
                sector.m_BooleanFlag &= static_cast<std::uint8_t>(~1u);
                for (int cellIndex = 0; cellIndex < 64; ++cellIndex) {
                    cells[cellSegment + sectorCellCount].m_data = runtime::LoadLittleEndianInt16(data + cursor);
                    cursor += 2;
                    ++sectorCellCount;
                }
            } else {
                sector.m_BooleanFlag &= static_cast<std::uint8_t>(~2u);
                sector.m_BooleanFlag &= static_cast<std::uint8_t>(~1u);
                for (int cellX = 0; cellX < 8; ++cellX) {
                    for (int cellY = 0; cellY < 8; ++cellY) {
                        if (cursor > file.Size() - 2) {
                            return false;
                        }
                        const int layerCount = runtime::LoadLittleEndianInt16(data + cursor);
                        cursor += 2;
                        if (layerCount < 0 || cursor > file.Size() - static_cast<std::size_t>(layerCount) * 2 ||
                            cellSegment > m_data->num_cell - sectorCellCount - layerCount) {
                            return false;
                        }
                        for (int layer = 0; layer < layerCount; ++layer) {
                            cells[cellSegment + sectorCellCount].m_data = runtime::LoadLittleEndianInt16(data + cursor);
                            cursor += 2;
                            ++sectorCellCount;
                        }
                        if (cellOffsetIndex >= cellOffsetCapacity) {
                            return false;
                        }
                        cellOffsets[cellOffsetIndex++] = static_cast<std::int16_t>(sectorCellCount);
                    }
                }
            }
            cellSegment += sectorCellCount;
        }
    }
    return true;
}

int CGeoZone::ClearData() {
    const int areaCount = static_cast<int>(m_NoFlyAreas.size()) + static_cast<int>(m_NoCallPCAreas.size()) +
        static_cast<int>(m_NoChangePCAreas.size()) + static_cast<int>(m_NoDropItemAreas.size()) +
        static_cast<int>(m_NoSaveBookmarkAreas.size()) + static_cast<int>(m_TransformableAreas.size()) +
        static_cast<int>(m_NoUseBookmarkAreas.size());

    m_NoFlyAreas.clear();
    m_NoCallPCAreas.clear();
    m_NoChangePCAreas.clear();
    m_NoDropItemAreas.clear();
    m_NoSaveBookmarkAreas.clear();
    m_NoUseBookmarkAreas.clear();
    m_TransformableAreas.clear();
    return areaCount;
}

char CGeoZone::GetX() { return m_data->zone_x; }

char CGeoZone::GetY() { return m_data->zone_y; }

CTerritory* CGeoZone::GetNoDropItemTerritory(const FVector& position) {
    L2_GUARD_FUNCTION();

    for (CTerritory* territory : m_NoDropItemAreas) {
        if (territory->IsInside(position, true)) {
            return territory;
        }
    }
    return nullptr;
}

bool CGeoZone::IsNoFlyArea(const FVector& position) {
    L2_GUARD_FUNCTION();
    return HelperFunction::IsInArea(m_NoFlyAreas, position);
}

bool CGeoZone::IsInNoCallPCArea(const FVector& position) {
    L2_GUARD_FUNCTION();
    return HelperFunction::IsInArea(m_NoCallPCAreas, position);
}

bool CGeoZone::IsInNoChangePCArea(const FVector& position) {
    L2_GUARD_FUNCTION();
    return HelperFunction::IsInArea(m_NoChangePCAreas, position);
}

bool CGeoZone::IsInNoDropItemArea(const FVector& position) { return HelperFunction::IsInArea(m_NoDropItemAreas, position); }

bool CGeoZone::IsInNoSaveBookmarkArea(const FVector& position) {
    L2_GUARD_FUNCTION();
    return HelperFunction::IsInArea(m_NoSaveBookmarkAreas, position);
}

bool CGeoZone::IsInNoUseBookmarkArea(const FVector& position) {
    L2_GUARD_FUNCTION();
    return HelperFunction::IsInArea(m_NoUseBookmarkAreas, position);
}

bool CGeoZone::IsInTransformableArea(const FVector& position) {
    L2_GUARD_FUNCTION();
    return HelperFunction::IsInArea(m_TransformableAreas, position);
}

void CGeoZone::AddNoFlyArea(CTerritory* territory) { m_NoFlyAreas.push_back(territory); }

void CGeoZone::AddNoCallPCArea(CTerritory* territory) { m_NoCallPCAreas.push_back(territory); }

void CGeoZone::AddNoChangePCArea(CTerritory* territory) { m_NoChangePCAreas.push_back(territory); }

void CGeoZone::AddNoDropItemArea(CTerritory* territory) { m_NoDropItemAreas.push_back(territory); }

void CGeoZone::AddNoSaveBookmarkArea(CTerritory* territory) { m_NoSaveBookmarkAreas.push_back(territory); }

void CGeoZone::AddNoUseBookmarkArea(CTerritory* territory) { m_NoUseBookmarkAreas.push_back(territory); }

void CGeoZone::AddTransformableArea(CTerritory* territory) { m_TransformableAreas.push_back(territory); }

void CGeoData::AddNoFlyArea(CTerritory* territory) {
    const int lastX = ZoneXFromWorld(territory->m_East);
    const int lastY = ZoneYFromWorld(territory->m_South);
    for (int zoneX = ZoneXFromWorld(territory->m_West); zoneX <= lastX; ++zoneX) {
        for (int zoneY = ZoneYFromWorld(territory->m_North); zoneY <= lastY; ++zoneY) {
            CGeoZone* const zone = GetZone(zoneX, zoneY);
            if (zone) {
                zone->AddNoFlyArea(territory);
            }
        }
    }
}

void CGeoData::AddNoCallPCArea(CTerritory* territory) {
    const int lastX = ZoneXFromWorld(territory->m_East);
    const int lastY = ZoneYFromWorld(territory->m_South);
    for (int zoneX = ZoneXFromWorld(territory->m_West); zoneX <= lastX; ++zoneX) {
        for (int zoneY = ZoneYFromWorld(territory->m_North); zoneY <= lastY; ++zoneY) {
            CGeoZone* const zone = GetZone(zoneX, zoneY);
            if (zone) {
                zone->AddNoCallPCArea(territory);
            }
        }
    }
}

void CGeoData::AddNoChangePCArea(CTerritory* territory) {
    const int lastX = ZoneXFromWorld(territory->m_East);
    const int lastY = ZoneYFromWorld(territory->m_South);
    for (int zoneX = ZoneXFromWorld(territory->m_West); zoneX <= lastX; ++zoneX) {
        for (int zoneY = ZoneYFromWorld(territory->m_North); zoneY <= lastY; ++zoneY) {
            CGeoZone* const zone = GetZone(zoneX, zoneY);
            if (zone) {
                zone->AddNoChangePCArea(territory);
            }
        }
    }
}

void CGeoData::AddNoDropItemArea(CTerritory* territory) {
    const int lastX = ZoneXFromWorld(territory->m_East);
    const int lastY = ZoneYFromWorld(territory->m_South);
    for (int zoneX = ZoneXFromWorld(territory->m_West); zoneX <= lastX; ++zoneX) {
        for (int zoneY = ZoneYFromWorld(territory->m_North); zoneY <= lastY; ++zoneY) {
            CGeoZone* const zone = GetZone(zoneX, zoneY);
            if (zone) {
                zone->AddNoDropItemArea(territory);
            }
        }
    }
}

void CGeoData::AddNoSaveBookmarkArea(CTerritory* territory) {
    const int lastX = ZoneXFromWorld(territory->m_East);
    const int lastY = ZoneYFromWorld(territory->m_South);
    for (int zoneX = ZoneXFromWorld(territory->m_West); zoneX <= lastX; ++zoneX) {
        for (int zoneY = ZoneYFromWorld(territory->m_North); zoneY <= lastY; ++zoneY) {
            CGeoZone* const zone = GetZone(zoneX, zoneY);
            if (zone) {
                zone->AddNoSaveBookmarkArea(territory);
            }
        }
    }
}

void CGeoData::AddNoUseBookmarkArea(CTerritory* territory) {
    const int lastX = ZoneXFromWorld(territory->m_East);
    const int lastY = ZoneYFromWorld(territory->m_South);
    for (int zoneX = ZoneXFromWorld(territory->m_West); zoneX <= lastX; ++zoneX) {
        for (int zoneY = ZoneYFromWorld(territory->m_North); zoneY <= lastY; ++zoneY) {
            CGeoZone* const zone = GetZone(zoneX, zoneY);
            if (zone) {
                zone->AddNoUseBookmarkArea(territory);
            }
        }
    }
}

void CGeoData::AddTransformableArea(CTerritory* territory) {
    const int lastX = ZoneXFromWorld(territory->m_East);
    const int lastY = ZoneYFromWorld(territory->m_South);
    for (int zoneX = ZoneXFromWorld(territory->m_West); zoneX <= lastX; ++zoneX) {
        for (int zoneY = ZoneYFromWorld(territory->m_North); zoneY <= lastY; ++zoneY) {
            CGeoZone* const zone = GetZone(zoneX, zoneY);
            if (zone) {
                zone->AddTransformableArea(territory);
            }
        }
    }
}

CGeoData::CGeoData(bool bUseTdd) {
    // Noise: Constructor chatter does not distinguish mapping attachment from creation.
    // Log.Add(LOG_NORMAL, L"Initializing CGeoData()")
    if (!g_bUseResourceLoader) {
        m_lstWaterPos.reserve(1600000);
    }
#if defined(L2_BUILD_L2NPC)
    // The shipping L2NPC is a consumer of the mappings published by
    // L2Server. In the portable deployment L2Server is also the producer
    // when the separate ResourceLoader is disabled, so the shared
    // g_bUseResourceLoader switch cannot select this process's ownership.
    // Creating the index here would reopen L2Server's named object and reset
    // zone_num to zero before npc-maker territory validation begins. This
    // holds on Windows too: CreateFileMapping opens an existing named object.
    if (!bUseTdd) {
        OpenMMF();
    } else {
        MakeMMF(true);
    }
#else
    if (g_bUseResourceLoader && !bUseTdd) {
        OpenMMF();
    } else {
        MakeMMF(bUseTdd);
    }
#endif
}

CGeoData::~CGeoData() = default;

bool CGeoData::ReadInfo(const wchar_t* fileName, char& zoneX, char& zoneY, int& cellCount, int& nonComplexSectorCount) {
    // Noise: Per-file path chatter is extra. Failures below include the resolved path.
    // Log.Add(LOG_NORMAL, L"path = %s", fileName)

    MappedFile file;
    std::string error;
    if (!file.Open(std::filesystem::path{fileName}, error) || file.Size() < 18) {
        // Log.Add(LOG_ERROR,
        //     "[geodata] Cannot read zone header from \"%s\": %s.",
        //     WideToUtf8(fileName).c_str(),
        //     error.empty() ? "file is shorter than the 18-byte header" : error.c_str())
        L2_LOG_ERROR(
            Log, "[geodata] Cannot read zone header from \"%s\": %s.", WideToUtf8(fileName).c_str(),
            error.empty() ? "file is shorter than the 18-byte header" : error.c_str()
        );
        return false;
    }
    const std::uint8_t* const data = file.Data();
    zoneX = static_cast<char>(data[0]);
    zoneY = static_cast<char>(data[1]);
    cellCount = runtime::LoadLittleEndianInt32(data + 6);
    nonComplexSectorCount = runtime::LoadLittleEndianInt32(data + 10);
    static_cast<void>(runtime::LoadLittleEndianInt16(data + 2));
    static_cast<void>(runtime::LoadLittleEndianInt16(data + 4));
    static_cast<void>(runtime::LoadLittleEndianInt32(data + 14));
    return true;
}

bool CGeoData::Load() {
    std::wifstream stream(runtime::ResolveResourcePath("../GeoData/geo_index.txt"));
    return LoadByStream(stream);
}

bool CGeoData::LoadByStream(std::wistream& stream) {
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server 0x007727cc leaves a failed extraction
    // lane unchanged and does not reject negative or oversized counts here.
    int zoneCount;
    stream >> zoneCount;
    LinkWithSharedGeoData();
    int totalCellCount = 0;
#else
    int zoneCount = 0;
    if (!(stream >> zoneCount) || zoneCount < 0 || zoneCount > 340) {
        return false;
    }
#endif

    const std::filesystem::path directory = runtime::ResolveResourcePath("../GeoData");
    // Log.Add(LOG_NORMAL, "[geodata] Reading zones from \"%s\".", directory.u8string().c_str())
    L2_LOG_INFO(Log, "[geodata] Reading zones from \"%s\".", runtime::Utf8Bytes(directory.u8string()).c_str());
    for (int index = 0; index < zoneCount; ++index) {
        std::wstring fileName;
#if defined(L2_BUILD_L2SERVER)
        stream >> fileName;
#else
        if (!(stream >> fileName) || fileName.empty()) {
            return false;
        }
#endif

        const std::filesystem::path filePath = directory / std::filesystem::path{fileName};
        const std::wstring widePath = filePath.wstring();
        char zoneX = 0;
        char zoneY = 0;
        int cellCount = 0;
        int nonComplexSectorCount = 0;
        if (!ReadInfo(widePath.c_str(), zoneX, zoneY, cellCount, nonComplexSectorCount)) {
#if defined(L2_BUILD_L2SERVER)
            // Log.Add(LOG_LOCAL, L"  Loading %s ...  failed!", fileName.c_str())
            // Log.Add(LOG_ERROR, L"  Loading %s ...  failed!", fileName.c_str())
            // Noise: Keep the required local copy without printing the same failure twice.
            // runtime::log::WriteErrorWithLocalCopy(Log,
            //     "[geodata] Zone header read failed for \"%s\". Invoking the fatal startup handler.",
            //     filePath.u8string().c_str())
            L2_LOG_ERROR_WITH_LOCAL_COPY(
                Log, "[geodata] Zone header read failed for \"%s\"; invoking the fatal startup handler.",
                runtime::Utf8Bytes(filePath.u8string()).c_str()
            );
            ForceCrash(CrashInfo::LOW_MEMORY);
#else
            // Log.Add(LOG_ERROR, L"  Loading %s ...  failed!", fileName.c_str())
            // Log.Add(LOG_ERROR,
            //     "[geodata] Zone header read failed for \"%s\". Stopping zone loading.",
            //     filePath.u8string().c_str())
            L2_LOG_ERROR(
                Log, "[geodata] Zone header read failed for \"%s\"; stopping zone loading.",
                runtime::Utf8Bytes(filePath.u8string()).c_str()
            );
#endif
            return false;
        }

        if (!g_bUseResourceLoader) {
#if defined(L2_BUILD_L2SERVER)
            totalCellCount = runtime::AddInt32Wrapping(totalCellCount, cellCount);
#endif
            if (!AddZone(zoneX, zoneY, cellCount, nonComplexSectorCount)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"  Loading %s ...  failed! "
                //     L"addzone failed",
                //     fileName.c_str()
                // )
                // Log.Add(LOG_ERROR,
                //     "[geodata] Cannot add zone (%d, %d) from \"%s\". Stopping zone loading.",
                //     static_cast<int>(zoneX), static_cast<int>(zoneY), filePath.u8string().c_str())
                L2_LOG_ERROR(
                    Log, "[geodata] Cannot add zone (%d, %d) from \"%s\"; stopping zone loading.", static_cast<int>(zoneX),
                    static_cast<int>(zoneY), runtime::Utf8Bytes(filePath.u8string()).c_str()
                );
                return false;
            }

            CGeoZone* const zone = GetZone(static_cast<int>(zoneX), static_cast<int>(zoneY));
            // zone->Load(
            //     widePath.c_str(), GetCellArray(static_cast<int>(zoneX), static_cast<int>(zoneY)),
            //     GetCellOffsetArray(static_cast<int>(zoneX), static_cast<int>(zoneY))
            // )
            const bool zoneLoaded = zone->Load(
                widePath.c_str(), GetCellArray(static_cast<int>(zoneX), static_cast<int>(zoneY)),
                GetCellOffsetArray(static_cast<int>(zoneX), static_cast<int>(zoneY))
            );
            if (!zoneLoaded) {
                // Log.Add(LOG_ERROR,
                //     "[geodata] Zone (%d, %d) could not be fully loaded from \"%s\". Continuing with potentially incomplete
                //     zone data.", static_cast<int>(zoneX), static_cast<int>(zoneY), filePath.u8string().c_str())
                L2_LOG_ERROR(
                    Log,
                    "[geodata] Zone (%d, %d) could not be fully loaded from \"%s\"; continuing with potentially incomplete "
                    "zone data.",
                    static_cast<int>(zoneX), static_cast<int>(zoneY), runtime::Utf8Bytes(filePath.u8string()).c_str()
                );
            }

            // Noise: Per-zone completion and estimated-size chatter does not confirm successful decoding. Retain aggregate
            // counts below. #if defined(L2_BUILD_L2SERVER)
            //             const std::int32_t sectorBytes =
            //                 runtime::MultiplyInt32Wrapping(
            //                     runtime::SubtractInt32Wrapping(
            //                         0x10000,
            //                         nonComplexSectorCount),
            //                     0x40)
            //             const std::int64_t summedBytes =
            //                 runtime::AddInt64Wrapping(
            //                     static_cast<std::int64_t>(sectorBytes),
            //                     static_cast<std::int64_t>(cellCount))
            //             const std::int64_t doubledBytes =
            //                 runtime::MultiplyInt64Wrapping(summedBytes, 2)
            //             const std::uint64_t mappedBytes =
            //                 static_cast<std::uint64_t>(
            //                     runtime::AddInt64Wrapping(
            //                         doubledBytes,
            //                         0xf8))
            //             const double mappedMegabytes =
            //                 static_cast<double>(mappedBytes) *
            //                 0.0009765625 * 0.0009765625.
            // #else
            //             const std::int64_t mappedBytes =
            //                 static_cast<std::int64_t>(
            //                     0x10000 - nonComplexSectorCount) *
            //                     0x40 * 2 +
            //                 0xc0 +
            //                 static_cast<std::int64_t>(cellCount) * 2
            //             const double mappedMegabytes =
            //                 (static_cast<double>(mappedBytes) / 1024.0) /
            //                 1024.0.
            // #endif
            //             Log.Add(LOG_NORMAL, L"  Loading %s ...  OK! [%5.3f]MB", fileName.c_str(), mappedMegabytes)
        }

#if defined(L2_BUILD_L2SERVER)
        if (g_MapBugDetector.LoadBugCell(static_cast<int>(zoneX), static_cast<int>(zoneY))) {
            // Noise: Per-zone success chatter is extra with the aggregate map-bug cell count.
            // Log.Add(
            //     LOG_NORMAL,
            //     L"   Loading mapbug[%d_%d]... OK!",
            //     static_cast<int>(zoneX),
            //     static_cast<int>(zoneY))
        }

        const std::wstring blockFileName =
            std::to_wstring(static_cast<int>(zoneX)) + L"_" + std::to_wstring(static_cast<int>(zoneY)) + L"_Block.txt";
        const std::filesystem::path blockFilePath = directory / std::filesystem::path{blockFileName};
        if (std::filesystem::exists(blockFilePath)) {
            const std::wstring wideBlockPath = blockFilePath.wstring();
            // static_cast<void>(
            //     g_RestrictArea.LoadScript(
            //         wideBlockPath.c_str(),
            //         false))
            const bool restrictionLoaded = g_RestrictArea.LoadScript(wideBlockPath.c_str(), false);
            // Log.Add(
            //     LOG_NORMAL,
            //     L"  Loading %s ...  OK!",
            //     wideBlockPath.c_str())
            // Log.Add(restrictionLoaded ? LOG_NORMAL : LOG_ERROR,
            //     "[geodata] Restriction file \"%s\": %s.",
            //     blockFilePath.u8string().c_str(),
            //     restrictionLoaded ? "loaded" : "load failed, continuing with any data already registered")
            L2_LOG(
                Log, restrictionLoaded ? LOG_NORMAL : LOG_ERROR, "[geodata] Restriction file \"%s\": %s.",
                runtime::Utf8Bytes(blockFilePath.u8string()).c_str(),
                restrictionLoaded ? "loaded" : "load failed; continuing with any data already registered"
            );
        }
#endif
    }

#if defined(L2_BUILD_L2SERVER)
    if (!g_bUseResourceLoader) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Total Cell Num [%d]",
        //     totalCellCount)
        // Log.Add(LOG_NORMAL,
        //     "[geodata] Zone pass completed. Declared cell total: %d.",
        //     totalCellCount)
        L2_LOG_INFO(Log, "[geodata] Zone pass completed; declared cell total: %d.", totalCellCount);
    }
    // Log.Add(
    //     LOG_NORMAL,
    //     L"Total MapBug Cell Count[%d]",
    //     g_MapBugDetector.GetBugCount())
    // Log.Add(LOG_NORMAL,
    //     "[geodata] Map-bug cells registered: %d.",
    //     g_MapBugDetector.GetBugCount())
    L2_LOG_INFO(Log, "[geodata] Map-bug cells registered: %d.", g_MapBugDetector.GetBugCount());
#endif

    std::ifstream badGeoData(directory / "BadGeoData.txt");
    if (badGeoData) {
        // Log.Add(LOG_NORMAL, L"Loading bad geodata list")
        // Log.Add(LOG_NORMAL,
        //     "[geodata] Reading excluded zones from \"%s\".",
        //     (directory / "BadGeoData.txt").u8string().c_str())
        L2_LOG_INFO(
            Log, "[geodata] Reading excluded zones from \"%s\".",
            runtime::Utf8Bytes((directory / "BadGeoData.txt").u8string()).c_str()
        );
        int zoneX = 0;
        int zoneY = 0;
        while (badGeoData >> zoneX >> zoneY) {
            if (zoneX < 10 || zoneX > 26 || zoneY < 10 || zoneY > 26) {
                // Log.Add(LOG_ERROR, L"Invalid bad geo x(%d) y(%d)", zoneX, zoneY)
                // Log.Add(LOG_ERROR,
                //     "[geodata] Invalid excluded zone (%d, %d) in \"%s\". Both coordinates must be 10..26. Keeping earlier
                //     entries and skipping the rest.", zoneX, zoneY, (directory / "BadGeoData.txt").u8string().c_str())
                L2_LOG_ERROR(
                    Log,
                    "[geodata] Invalid excluded zone (%d, %d) in \"%s\"; both coordinates must be 10..26. Keeping earlier "
                    "entries and skipping the rest.",
                    zoneX, zoneY, runtime::Utf8Bytes((directory / "BadGeoData.txt").u8string()).c_str()
                );
                break;
            }
            m_bBadMap[zoneX][zoneY] = 1;
            // Noise: One record per excluded zone adds startup noise. Retain the file path and invalid-entry diagnostic.
            // Log.Add(LOG_NORMAL, L"bad geo [%d][%d]", zoneX, zoneY)
        }
    }
    return true;
}

void CGeoData::MakeMMF(bool testWorld) {
    auto index = std::make_unique<PortableGeoIndex>();
    const std::string mappingName = testWorld ? "L2World_GeoDataIndex_Test" : "L2World_GeoDataIndex";
    std::string mappingError;
    if (!index->mapping.CreateNamed(mappingName, shared_geo_layout::indexBytes, mappingError)) {
        // Log.Add(LOG_ERROR, "failed to create geodata mapping %s: %s", mappingName.c_str(), mappingError.c_str())
        // Log.Add(LOG_ERROR,
        //     "[geodata] Cannot create shared mapping \"%s\": %s. Attempting process-local index storage. This mapping will
        //     not be available to peers.", mappingName.c_str(), mappingError.c_str())
        L2_LOG_ERROR(
            Log,
            "[geodata] Cannot create shared mapping \"%s\": %s. Attempting process-local index storage; this mapping will "
            "not be available to peers.",
            mappingName.c_str(), mappingError.c_str()
        );
        index->owned = std::make_unique<CGeoDataIndexStruct>();
    }
    m_zone_index = index->Data();
    m_zone_index->zone_num = 0;
    m_hfm_index = index.release();
}

bool CGeoData::AddZone(char zoneX, char zoneY, int cellCount, int nonComplexSectorCount) {
    const int indexX = static_cast<int>(zoneX) - 10;
    const int indexY = static_cast<int>(zoneY) - 10;
    const int complexSectorCount = 0x10000 - nonComplexSectorCount;
    const int ordinal = static_cast<int>(m_zone_index->zone_num);

    m_zone_index->idx[ordinal] = zoneX;
    m_zone_index->idy[ordinal] = zoneY;
    m_zone_index->num_cell[ordinal] = cellCount;
    m_zone_index->num_complex_sector[ordinal] = complexSectorCount;

    auto zone = std::make_unique<PortableMappedZone>();
    zone->x = static_cast<int>(zoneX);
    zone->y = static_cast<int>(zoneY);
    zone->declaredCells = cellCount;
    zone->declaredNonComplexSectors = nonComplexSectorCount;

    const std::string suffix = "_" + std::to_string(ordinal);
    std::string mappingError;
    const std::string zoneMappingName = "L2World_GeoDataZones" + suffix;
    if (zone->sharedZone.CreateNamed(zoneMappingName, shared_geo_layout::zoneBytes, mappingError)) {
        new (zone->sharedZone.MutableData()) CSharedGeoZone;
    } else {
        // Log.Add(LOG_ERROR, "failed to create geodata mapping %s: %s", zoneMappingName.c_str(), mappingError.c_str())
        // Log.Add(LOG_ERROR,
        //     "[geodata] Cannot create shared mapping \"%s\": %s. Attempting process-local zone storage. This mapping will
        //     not be available to peers.", zoneMappingName.c_str(), mappingError.c_str())
        L2_LOG_ERROR(
            Log,
            "[geodata] Cannot create shared mapping \"%s\": %s. Attempting process-local zone storage; this mapping will "
            "not be available to peers.",
            zoneMappingName.c_str(), mappingError.c_str()
        );
        zone->ownedZone = std::make_unique<CSharedGeoZone>();
    }

    if (cellCount > 0) {
        const std::string cellMappingName = "L2World_GeoDataCells" + suffix;
        if (!zone->sharedCells.CreateNamed(
                cellMappingName, static_cast<std::size_t>(cellCount) * sizeof(CGeoCell), mappingError
            )) {
            // Log.Add(LOG_ERROR, "failed to create geodata mapping %s: %s", cellMappingName.c_str(), mappingError.c_str())
            // Log.Add(LOG_ERROR,
            //     "[geodata] Cannot create shared mapping \"%s\": %s. Attempting process-local cells storage. This mapping
            //     will not be available to peers.", cellMappingName.c_str(), mappingError.c_str())
            L2_LOG_ERROR(
                Log,
                "[geodata] Cannot create shared mapping \"%s\": %s. Attempting process-local cells storage; this mapping "
                "will not be available to peers.",
                cellMappingName.c_str(), mappingError.c_str()
            );
            zone->ownedCells.resize(static_cast<std::size_t>(cellCount));
        }
        zone->CellData()->m_data = 0;
    }
    if (complexSectorCount > 0) {
        const std::string offsetMappingName = "L2World_GeoDataCellOffest" + suffix;
        if (!zone->sharedCellOffsets.CreateNamed(
                offsetMappingName, static_cast<std::size_t>(complexSectorCount) * 0x80, mappingError
            )) {
            // Log.Add(LOG_ERROR, "failed to create geodata mapping %s: %s", offsetMappingName.c_str(),
            // mappingError.c_str()), Log.Add(LOG_ERROR,
            //     "[geodata] Cannot create shared mapping \"%s\": %s. Attempting process-local cell offsets storage. This
            //     mapping will not be available to peers.", offsetMappingName.c_str(), mappingError.c_str())
            L2_LOG_ERROR(
                Log,
                "[geodata] Cannot create shared mapping \"%s\": %s. Attempting process-local cell offsets storage; this "
                "mapping will not be available to peers.",
                offsetMappingName.c_str(), mappingError.c_str()
            );
            zone->ownedCellOffsets.resize(static_cast<std::size_t>(complexSectorCount) * 64);
        }
    }

    PortableMappedZone* const owner = zone.release();
    m_hfm_zones[indexX][indexY] = owner;
    m_SharedZone[indexX][indexY] = owner->SharedZoneData();
    m_Zone[indexX][indexY].m_data = owner->SharedZoneData();
    m_cells[indexX][indexY] = owner->CellData();
    m_hfm_cells[indexX][indexY] = owner->sharedCells.Data() ? &owner->sharedCells : nullptr;
    m_cell_offset[indexX][indexY] = owner->CellOffsetData();
    m_hfm_cell_offset[indexX][indexY] = owner->sharedCellOffsets.Data() ? &owner->sharedCellOffsets : nullptr;
    m_zone_index->zone_num = static_cast<std::int16_t>(ordinal + 1);
    return true;
}

void CGeoData::OpenMMF() {
    std::string ignoredError;
    l2geo::portable::GeoDataSource::AttachSharedMemory(*this, GeoDataSharedMemoryNames{}, &ignoredError, nullptr);
}

bool CGeoData::CheckZoneBound(int zoneX, int zoneY) {
    return static_cast<unsigned>(zoneX - kZoneXMinimum) < 20u && static_cast<unsigned>(zoneY - kZoneYMinimum) < 17u;
}

void CGeoData::DecideDirection(
    const FVector& vector, GeoDirEnum* directionX, GeoDirEnum* directionY, GeoDirEnum* directionZ
) {
    if (directionX) {
        *directionX = vector.x <= 0.0 ? GEO_DIR_WEST : GEO_DIR_EAST;
    }
    if (directionY) {
        *directionY = vector.y <= 0.0 ? GEO_DIR_NORTH : GEO_DIR_SOUTH;
    }
    if (directionZ) {
        const int z = runtime::Win64CvttDoubleToInt32(vector.z);
        if (z >= 1) {
            *directionZ = GEO_DIR_ABOVE;
        } else if (z == 0) {
            *directionZ = GEO_DIR_HORIZONTAL;
        } else {
            *directionZ = GEO_DIR_BELOW;
        }
    }
}

CGeoZone* CGeoData::GetZone(int zoneX, int zoneY) {
    if (!CheckZoneBound(zoneX, zoneY)) {
        return nullptr;
    }
    const int indexX = zoneX - kZoneXMinimum;
    const int indexY = zoneY - kZoneYMinimum;
    // Current L2NPC.exe 0x00432938 tests the linked CGeoZone::m_data
    // field, not the source pointer array used during OpenMMF.
    return m_Zone[indexX][indexY].m_data ? &m_Zone[indexX][indexY] : nullptr;
}

CGeoCell* CGeoData::GetCellArray(int zoneX, int zoneY) { return m_cells[zoneX - kZoneXMinimum][zoneY - kZoneYMinimum]; }

std::int16_t* CGeoData::GetCellOffsetArray(int zoneX, int zoneY) {
    return m_cell_offset[zoneX - kZoneXMinimum][zoneY - kZoneYMinimum];
}

bool CGeoData::GetCellIndexes(int x, int y, int& firstIndex, int& lastIndex) {
    const int zoneX = ZoneXFromWorld(x);
    const int zoneY = ZoneYFromWorld(y);
    CGeoZone* const zone = GetZone(zoneX, zoneY);
    if (!zone) {
        return false;
    }
    CGeoSector* const sector = zone->GetSector(SectorXFromWorld(x), SectorYFromWorld(y));
    std::int16_t* const offsets = GetCellOffsetArray(zoneX, zoneY);
    firstIndex = sector->GetFirstCellIndex(CellXFromWorld(x), CellYFromWorld(y), offsets);
    lastIndex = sector->GetLastCellIndex(CellXFromWorld(x), CellYFromWorld(y), offsets);
    return true;
}

CGeoSector* CGeoData::GetSectorByCoord(int x, int y) {
    CGeoZone* const zone = GetZone(ZoneXFromWorld(x), ZoneYFromWorld(y));
    return zone ? zone->GetSector(SectorXFromWorld(x), SectorYFromWorld(y)) : nullptr;
}

CGeoCell* CGeoData::GetBaseCell(int z, CGeoCell* cells, int firstIndex, int lastIndex) {
    CGeoCell* current = nullptr;
    do {
        current = cells + firstIndex;
        ++firstIndex;
        if (lastIndex <= firstIndex) {
            return current;
        }
    } while (z < CellHeight(current->m_data));
    return current;
}

CGeoCell* CGeoData::GetBaseCellFromSector(CGeoSector* sector, int x, int y, int z) {
    if ((sector->m_BooleanFlag & 1u) != 0) {
        return &sector->m_DefaultCell;
    }
    CGeoCell* const cells = GetCellArray(ZoneXFromWorld(x), ZoneYFromWorld(y));
    if (!cells) {
        // Log.Add(
        //     LOG_ERROR,
        //     "failed to get cell_array (%d, %d) "
        //     "at file[%s], line[%d]",
        //     x, y, "..\\Shared\\GeoData.cpp", 0x29e
        // )
        L2_LOG_ERROR(
            Log, "[geodata] Base-cell lookup at (%d,%d,%d) failed: cell array is missing. Returning no cell.", x, y, z
        );
        return nullptr;
    }
    // SHIPPING BUG GEO-SILENT-001: L2NPC.exe 0x0043329c ignores the false
    // result and passes two unwritten stack slots to GetBaseCell. The former
    // portable body initialized both to zero, deterministically selecting
    // cells[0] instead. Keep the shipping call sequence disabled here.
    /*
    int firstIndex.
    int lastIndex.
    GetCellIndexes(
        x,
        y,
        firstIndex,
        lastIndex).
    */

    int firstIndex = 0;
    int lastIndex = 0;
    if (!GetCellIndexes(x, y, firstIndex, lastIndex)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "failed to get cell indexes (%d, %d, %d) "
        //     "at file[%s], line[%d]",
        //     x, y, z, "..\\Shared\\GeoData.cpp", 0x2a8
        // )
        L2_LOG_ERROR(
            Log, "[geodata] Base-cell lookup at (%d,%d,%d) failed: cell-index lookup failed. Returning no cell.", x, y, z
        );
        return nullptr;
    }
    CGeoCell* const result = GetBaseCell(runtime::AddInt32Wrapping(z, 26), cells, firstIndex, lastIndex);
    if (!result) {
        // Log.Add(
        //     LOG_ERROR,
        //     "failed to get basecell (%d, %d, %d) "
        //     "at file[%s], line[%d]",
        //     x, y, z, "..\\Shared\\GeoData.cpp", 0x2a8
        // )
        L2_LOG_ERROR(
            Log, "[geodata] Base-cell selection at (%d,%d,%d) returned no cell (first index %d, last index %d).", x, y, z,
            firstIndex, lastIndex
        );
    }
    return result;
}

CGeoCell* CGeoData::GetBaseCell(int x, int y, int z) {
    CGeoSector* const sector = GetSectorByCoord(x, y);
    return sector ? GetBaseCellFromSector(sector, x, y, z) : nullptr;
}

CGeoCell* CGeoData::GetBaseCell(const FVector& position) {
    return GetBaseCell(
        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        runtime::Win64CvttDoubleToInt32(position.z)
    );
}

CGeoCell* CGeoData::GetClosestUpperCell(const FVector& position) {
    CGeoSector* const sector =
        GetSectorByCoord(runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y));
    return sector ? GetClosestUpperCellFromSector(
                        sector, runtime::Win64CvttDoubleToInt32(position.x),
                        runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z), false
                    )
                  : nullptr;
}

CGeoCell* CGeoData::GetUpperCell(int z, CGeoCell* cells, int firstIndex, int lastIndex) {
    CGeoCell* result = nullptr;
    for (int index = firstIndex; index < lastIndex && !result; ++index) {
        if (z < CellHeight(cells[index].m_data)) {
            result = cells + index;
        }
    }
    return result;
}

CGeoCell* CGeoData::GetClosestUpperCell(int z, CGeoCell* cells, int firstIndex, int lastIndex, bool requireAtOrAbove) {
    CGeoCell* result = cells + firstIndex;
    int nextIndex = firstIndex + 1;
    while (nextIndex < lastIndex) {
        CGeoCell* const next = cells + nextIndex;
        if (CellHeight(result->m_data) <= z || CellHeight(next->m_data) <= z) {
            break;
        }
        result = next;
        ++nextIndex;
    }
    if (requireAtOrAbove && CellHeight(result->m_data) < z) {
        return nullptr;
    }
    return result;
}

CGeoCell* CGeoData::GetClosestUpperCellFromSector(CGeoSector* sector, int x, int y, int z, bool requireAtOrAbove) {
    if ((sector->m_BooleanFlag & 1u) != 0) {
        return &sector->m_DefaultCell;
    }
    CGeoCell* const cells = GetCellArray(ZoneXFromWorld(x), ZoneYFromWorld(y));
    if (!cells) {
        // Log.Add(
        //     LOG_ERROR,
        //     "failed to get cell_array (%d, %d) "
        //     "at file[%s], line[%d]",
        //     x, y, "..\\Shared\\GeoData.cpp", 0x2bd
        // )
        L2_LOG_ERROR(
            Log,
            "[geodata] Closest-upper-cell lookup at (%d,%d,%d) failed: cell array is missing (require at or above %d). "
            "Returning no cell.",
            x, y, z, static_cast<int>(requireAtOrAbove)
        );
        return nullptr;
    }
    int firstIndex = 0;
    int lastIndex = 0;
    GetCellIndexes(x, y, firstIndex, lastIndex);
    CGeoCell* const result = GetClosestUpperCell(z, cells, firstIndex, lastIndex, requireAtOrAbove);
    if (!requireAtOrAbove && !result) {
        // Log.Add(
        //     LOG_ERROR,
        //     "failed to get closestUpperCell (%d, %d, %d) "
        //     "at file[%s], line[%d]",
        //     x, y, z, "..\\Shared\\GeoData.cpp", 0x2ca
        // )
        L2_LOG_ERROR(
            Log,
            "[geodata] Closest-upper-cell selection at (%d,%d,%d) returned no cell with the at-or-above restriction "
            "disabled "
            "(first index %d, last index %d).",
            x, y, z, firstIndex, lastIndex
        );
    }
    return result;
}

bool CGeoData::CollideWithTopOrBottom(GeoDirEnum direction, const FVector& position, double distance) {
    if (distance <= 0.0) {
        return false;
    }

    bool collided = true;
    const int destinationZ = static_cast<int>(position.z) + static_cast<int>(distance);
    if (direction == GEO_DIR_ABOVE || direction == GEO_DIR_BELOW) {
        const int ground = FindGround(static_cast<int>(position.x), static_cast<int>(position.y), destinationZ);
        if (destinationZ < ground) {
            collided = true;
        } else {
            collided = static_cast<int>(position.z) < ground;
        }
    } else if (direction == GEO_DIR_HORIZONTAL) {
        collided = false;
    }
    return collided;
}

bool CGeoData::CollideWithTopOrBottom(
    GeoDirEnum direction, int x, int y, int z, double distance, FVector& adjustedPosition
) {
    if (distance <= 0.0) {
        return false;
    }

    bool collided = true;
    const int destinationZ = z + static_cast<int>(distance);
    if (direction == GEO_DIR_ABOVE || direction == GEO_DIR_BELOW) {
        const int ground = FindGround(x, y, destinationZ);
        if (destinationZ < ground) {
            adjustedPosition.z = static_cast<double>(ground);
            collided = true;
        } else {
            collided = z < ground;
        }
    } else if (direction == GEO_DIR_HORIZONTAL) {
        collided = false;
    }
    return collided;
}

int CGeoData::FindNearGround(int x, int y, int z) {
    static int errorcount = 0;
    CGeoZone* const zone = GetZone(ZoneXFromWorld(x), ZoneYFromWorld(y));
    if (!zone) {
        if (errorcount < 10) {
            // Log.Add(
            //     LOG_ERROR,
            //     "cannot get zone (%d, %d) "
            //     "at file[%s], line[%d]",
            //     x, y, "..\\Shared\\GeoData.cpp", 0xaf9
            // )
            L2_LOG_ERROR(
                Log,
                "[geodata] Cannot find nearby ground at (%d,%d,%d): zone is missing. Returning height 0. "
                "This diagnostic is limited to the first 10 occurrences in this function.",
                x, y, z
            );
            ++errorcount;
        }
        return 0;
    }

    CGeoSector* const sector = zone->GetSector(SectorXFromWorld(x), SectorYFromWorld(y));
    if (!sector) {
        // Log.Add(
        //     LOG_ERROR,
        //     "cannot get sector (%d, %d) "
        //     "at file[%s], line[%d]",
        //     x, y, "..\\Shared\\GeoData.cpp", 0xb05
        // )
        L2_LOG_ERROR(
            Log, "[geodata] Cannot find nearby ground at (%d,%d,%d): sector is missing. Returning height 0.", x, y, z
        );
        return 0;
    }
    if ((sector->m_BooleanFlag & 1u) != 0) {
        return CellHeight(sector->m_DefaultCell.m_data);
    }

    CGeoCell* const cells = GetCellArray(ZoneXFromWorld(x), ZoneYFromWorld(y));
    if (!cells) {
        // Log.Add(
        //     LOG_ERROR,
        //     "failed to get cell_array (%d, %d) "
        //     "at file[%s], line[%d]",
        //     x, y, "..\\Shared\\GeoData.cpp", 0xb11
        // )
        L2_LOG_ERROR(
            Log, "[geodata] Cannot find nearby ground at (%d,%d,%d): cell array is missing. Returning height 0.", x, y, z
        );
        return 0;
    }

    int firstIndex = 0;
    int lastIndex = 0;
    GetCellIndexes(x, y, firstIndex, lastIndex);
    if (lastIndex - firstIndex < 2) {
        return CellHeight(cells[firstIndex].m_data);
    }

    int scanIndex = firstIndex;
    int cellIndex = firstIndex;
    CGeoCell* cell = nullptr;
    for (;;) {
        cellIndex = scanIndex;
        cell = cells + cellIndex;
        const int nextIndex = cellIndex + 1;
        if (lastIndex <= nextIndex) {
            break;
        }
        scanIndex = nextIndex;
        if (z + 26 >= CellHeight(cell->m_data)) {
            break;
        }
    }

    int height = CellHeight(cell->m_data);
    if (512 < z - height && firstIndex <= cellIndex - 1) {
        const int upperHeight = CellHeight(cells[cellIndex - 1].m_data);
        if (upperHeight - z < 256) {
            height = upperHeight;
        }
    }
    return height;
}

int CGeoData::FindNearGround(FVector& position) {
    return FindNearGround(
        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        runtime::Win64CvttDoubleToInt32(position.z)
    );
}

int CGeoData::FindGround(int x, int y, int z) {
    static int errorcount = 0;
    CGeoZone* const zone = GetZone(ZoneXFromWorld(x), ZoneYFromWorld(y));
    if (!zone) {
        if (errorcount < 10) {
            // Log.Add(
            //     LOG_ERROR,
            //     "cannot get zone (%d, %d) "
            //     "at file[%s], line[%d]",
            //     x, y, "..\\Shared\\GeoData.cpp", 0xb4f
            // )
            L2_LOG_ERROR(
                Log,
                "[geodata] Cannot find ground at (%d,%d,%d): zone is missing. Returning height 0. "
                "This diagnostic is limited to the first 10 occurrences in this function.",
                x, y, z
            );
            ++errorcount;
        }
        return 0;
    }
    CGeoSector* const sector = zone->GetSector(SectorXFromWorld(x), SectorYFromWorld(y));
    if ((sector->m_BooleanFlag & 1u) != 0) {
        return CellHeight(sector->m_DefaultCell.m_data);
    }
    CGeoCell* const cell = GetBaseCellFromSector(sector, x, y, z);
    if (!cell) {
        // Log.Add(
        //     LOG_ERROR,
        //     "cannot get cell (%d, %d, %d) "
        //     "at file[%s], line[%d]",
        //     x, y, z, "..\\Shared\\GeoData.cpp", 0xb68
        // )
        L2_LOG_ERROR(Log, "[geodata] Cannot find ground at (%d,%d,%d): cell is missing. Returning height 0.", x, y, z);
        return 0;
    }
    return CellHeight(cell->m_data);
}

int CGeoData::FindGround(FVector& position) {
    return FindGround(
        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        runtime::Win64CvttDoubleToInt32(position.z)
    );
}

int CGeoData::FindGround(const FVector& position) {
    return FindGround(
        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        runtime::Win64CvttDoubleToInt32(position.z)
    );
}

bool CGeoData::IsValidPosition(int x, int y) { return GetZone(ZoneXFromWorld(x), ZoneYFromWorld(y)) != nullptr; }

bool CGeoData::IsOpen(FVector position) {
    const int x = runtime::Win64CvttDoubleToInt32(position.x);
    const int y = runtime::Win64CvttDoubleToInt32(position.y);
    const int z = runtime::Win64CvttDoubleToInt32(position.z);
    CGeoSector* const sector = GetSectorByCoord(x, y);
    if (!sector) {
        return false;
    }
    if ((sector->m_BooleanFlag & 1u) != 0) {
        return true;
    }
    CGeoCell* const cell = GetBaseCellFromSector(sector, x, y, z);
    if (!cell) {
        // Log.Add(
        //     LOG_ERROR,
        //     "cannot get cell from sector (%d, %d, %d) "
        //     "at file[%s], line[%d]",
        //     x, y, z, "..\\Shared\\GeoData.cpp", 0xd0a
        // )
        L2_LOG_ERROR(
            Log, "[geodata] Cannot check whether position (%d,%d,%d) is open: cell is missing. Returning false.", x, y, z
        );
        return false;
    }
    return (static_cast<std::uint16_t>(cell->m_data) & kOpenMask) == kOpenMask;
}

bool CGeoData::IsField(CGeoZone* zone, int x, int y, int z, GeoFieldTypeEnum fieldType) {
    CGeoSector* const sector = zone->GetSector(SectorXFromWorld(x), SectorYFromWorld(y));
    if (!sector) {
        // Log.Add(
        //     LOG_ERROR,
        //     "cannot get sector (%d, %d) "
        //     "at file[%s], line[%d]",
        //     x, y, "..\\Shared\\GeoData.cpp", 0xbc2
        // )
        L2_LOG_ERROR(
            Log, "[geodata] Cannot check field type %d at (%d,%d,%d): sector is missing. Returning false.",
            static_cast<int>(fieldType), x, y, z
        );
        return false;
    }
    const std::uint16_t mask = GeoFieldMask[static_cast<std::size_t>(fieldType)];
    return (static_cast<std::uint16_t>(sector->m_Field) & mask) == mask;
}

bool CGeoData::IsField(int x, int y, int z, GeoFieldTypeEnum fieldType) {
    CGeoZone* const zone = GetZone(ZoneXFromWorld(x), ZoneYFromWorld(y));
    if (!zone) {
        // Log.Add(
        //     LOG_ERROR,
        //     "cannot get zone (%d, %d) "
        //     "at file[%s], line[%d]",
        //     x, y, "..\\Shared\\GeoData.cpp", 0xbce
        // )
        L2_LOG_ERROR(
            Log, "[geodata] Cannot check field type %d at (%d,%d,%d): zone is missing. Returning false.",
            static_cast<int>(fieldType), x, y, z
        );
        return false;
    }
    return IsField(zone, x, y, z, fieldType);
}

bool CGeoData::IsBattleField(FVector position) {
    return IsField(
               static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z),
               GEO_FIELD_BATTLE_RESIDENCE
           ) ||
        IsField(
            static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z), GEO_FIELD_BATTLE_PVP
        );
}

void CGeoData::SetField(CTerritory* territory, GeoFieldTypeEnum fieldType, bool enabled) {
    const std::array<int, 3> sampleOffsets{{
        1,
        64,
        127,
    }};
    for (int x = territory->m_West - 127; x < territory->m_East + 128; x += 128) {
        for (int y = territory->m_North - 127; y < territory->m_South + 128; y += 128) {
            const int baseX = static_cast<int>(static_cast<std::uint32_t>(x) & 0xffffff80u);
            const int baseY = static_cast<int>(static_cast<std::uint32_t>(y) & 0xffffff80u);
            bool inside = false;
            for (int offsetX : sampleOffsets) {
                for (int offsetY : sampleOffsets) {
                    const FVector sample{
                        static_cast<double>(baseX + offsetX),
                        static_cast<double>(baseY + offsetY),
                        0.0,
                    };
                    if (territory->IsInside(sample, false)) {
                        inside = true;
                    }
                }
            }
            if (!inside) {
                continue;
            }

            CGeoZone* const zone = GetZone(ZoneXFromWorld(x), ZoneYFromWorld(y));
            if (!zone) {
                // Log.Add(
                //     LOG_ERROR,
                //     "cannot get zone (%d, %d) "
                //     "at file[%s], line[%d]",
                //     x, y, "..\\Shared\\GeoData.cpp", 0xcb9
                // )
                L2_LOG_ERROR(
                    Log,
                    "[geodata] Cannot update field type %d (enabled %d) at world coordinates (%d,%d): zone is missing. "
                    "Territory update stopped; earlier sector changes are retained.",
                    static_cast<int>(fieldType), static_cast<int>(enabled), x, y
                );
                return;
            }
            CGeoSector* const sector = zone->GetSector(SectorXFromWorld(x), SectorYFromWorld(y));
            if (!sector) {
                // Log.Add(
                //     LOG_ERROR,
                //     "cannot get sector (%d, %d) "
                //     "at file[%s], line[%d]",
                //     x, y, "..\\Shared\\GeoData.cpp", 0xcc2
                // )
                L2_LOG_ERROR(
                    Log,
                    "[geodata] Cannot update field type %d (enabled %d) at world coordinates (%d,%d): sector is missing. "
                    "Territory update stopped; earlier sector changes are retained.",
                    static_cast<int>(fieldType), static_cast<int>(enabled), x, y
                );
                return;
            }

            const std::uint16_t mask = GeoFieldMask[static_cast<std::size_t>(fieldType)];
            if (enabled) {
                sector->m_Field = static_cast<std::int16_t>(static_cast<std::uint16_t>(sector->m_Field) | mask);
                if (fieldType == GEO_FIELD_BATTLE_RESIDENCE || fieldType == GEO_FIELD_BATTLE_PVP ||
                    fieldType == GEO_FIELD_SSQ) {
                    sector->m_TerritoryMinZ = static_cast<std::int16_t>(territory->m_Bottom);
                    sector->m_TerritoryMaxZ = static_cast<std::int16_t>(territory->m_Top);
                }
            } else {
                sector->m_Field = static_cast<std::int16_t>(
                    static_cast<std::uint16_t>(sector->m_Field) & static_cast<std::uint16_t>(~mask)
                );
            }
        }
    }
}

bool CGeoData::SetStaticField(CTerritory* territory, GeoFieldTypeEnum fieldType) {
    L2_GUARD_FUNCTION();
    const int horizonLength = runtime::SubtractInt32Wrapping(territory->m_East, territory->m_West);
    if (maxHorizonLength < horizonLength) {
        maxHorizonLength = horizonLength;
        pMaxHorizonTerritory = territory;
    }
    const int verticalLength = runtime::SubtractInt32Wrapping(territory->m_South, territory->m_North);
    if (maxVerticalLength < verticalLength) {
        maxVerticalLength = verticalLength;
        pMaxVerticalTerritory = territory;
    }

    bool set = false;
    for (int x = territory->m_West - 15; x < territory->m_East + 16; x += 16) {
        for (int y = territory->m_North - 15; y < territory->m_South + 16; y += 16) {
#if defined(L2_STARTUP_DIAGNOSTICS)
            // These counters and millisecond probes exist in the original
            // L2Server.exe. They are built only when L2_STARTUP_DIAGNOSTICS
            // is defined. The field computation below is the same either way
            // and remains the only producer of the geo state.
            totalloopCount = runtime::AddInt32Wrapping(totalloopCount, 1);

            std::uint32_t startTick = runtime::GetTickCount();
            callCount1 = runtime::AddInt32Wrapping(callCount1, 1);
#endif
            CGeoData* const shared = GeoDataWorld::Inst().GetGeo();
            CGeoZone* const zone = shared->GetZone(ZoneXFromWorld(x), ZoneYFromWorld(y));
#if defined(L2_STARTUP_DIAGNOSTICS)
            profile1 += runtime::GetTickCount() - startTick;
#endif
            if (!zone) {
                continue;
            }

#if defined(L2_STARTUP_DIAGNOSTICS)
            startTick = runtime::GetTickCount();
            callCount2 = runtime::AddInt32Wrapping(callCount2, 1);
#endif
            CGeoSector* const sector = zone->GetSector(SectorXFromWorld(x), SectorYFromWorld(y));
            if (!sector) {
                // Log.Add(
                //     LOG_ERROR,
                //     "cannot get sector (%d, %d) "
                //     "at file[%s], line[%d]",
                //     x, y, "..\\Shared\\GeoData.cpp", 0xc4a
                // )
                L2_LOG_ERROR(
                    Log,
                    "[geodata] Cannot set static field type %d at world coordinates (%d,%d): sector is missing. "
                    "Returning failure; earlier sector changes are retained.",
                    static_cast<int>(fieldType), x, y
                );
#if defined(L2_STARTUP_DIAGNOSTICS)
                profile2 += runtime::GetTickCount() - startTick;
#endif
                return false;
            }
#if defined(L2_STARTUP_DIAGNOSTICS)
            profile2 += runtime::GetTickCount() - startTick;
#endif

            const std::uint16_t mask = GeoFieldMask[static_cast<std::size_t>(fieldType)];
#if defined(L2_STARTUP_DIAGNOSTICS)
            startTick = runtime::GetTickCount();
            callCount3 = runtime::AddInt32Wrapping(callCount3, 1);
#endif
            const bool alreadySet = (static_cast<std::uint16_t>(sector->m_Field) & mask) == mask;
#if defined(L2_STARTUP_DIAGNOSTICS)
            profile3 += runtime::GetTickCount() - startTick;
#endif
            if (alreadySet) {
                continue;
            }

#if defined(L2_STARTUP_DIAGNOSTICS)
            startTick = runtime::GetTickCount();
            callCount4 = runtime::AddInt32Wrapping(callCount4, 1);
#endif
            const FVector position{
                static_cast<double>(x),
                static_cast<double>(y),
                0.0,
            };
            const bool inside = territory->IsInside(position, false);
#if defined(L2_STARTUP_DIAGNOSTICS)
            profile4 += runtime::GetTickCount() - startTick;
#endif
            if (!inside) {
                continue;
            }

#if defined(L2_STARTUP_DIAGNOSTICS)
            startTick = runtime::GetTickCount();
            callCount5 = runtime::AddInt32Wrapping(callCount5, 1);
#endif
            CGeoData* const sharedForCell = GeoDataWorld::Inst().GetGeo();
            CGeoCell* const cell = sharedForCell->GetBaseCellFromSector(sector, x, y, territory->m_Top);
#if defined(L2_STARTUP_DIAGNOSTICS)
            profile5 += runtime::GetTickCount() - startTick;
#endif
            if (!cell) {
                continue;
            }

            if (fieldType == GEO_FIELD_WATER) {
#if defined(L2_STARTUP_DIAGNOSTICS)
                startTick = runtime::GetTickCount();
                callCount6 = runtime::AddInt32Wrapping(callCount6, 1);
#endif
                const int height = CellHeight(cell->m_data);
                if (height <= territory->m_Top) {
                    set = true;
                    sector->m_Field = static_cast<std::int16_t>(
                        static_cast<std::uint16_t>(sector->m_Field) | GeoFieldMask[GEO_FIELD_WATER]
                    );
                    if (!g_bUseResourceLoader) {
                        m_lstWaterPos.push_back(
                            FVector{
                                static_cast<double>(x),
                                static_cast<double>(y),
                                static_cast<double>(territory->m_Top),
                            }
                        );
                    }
                }
#if defined(L2_STARTUP_DIAGNOSTICS)
                profile6 += runtime::GetTickCount() - startTick;
#endif
            } else {
#if defined(L2_STARTUP_DIAGNOSTICS)
                startTick = runtime::GetTickCount();
                callCount7 = runtime::AddInt32Wrapping(callCount7, 1);
#endif
                const int height = CellHeight(cell->m_data);
                if (territory->m_Bottom <= height) {
                    set = true;
                    sector->m_Field = static_cast<std::int16_t>(static_cast<std::uint16_t>(sector->m_Field) | mask);
                    if (fieldType == GEO_FIELD_BATTLE_RESIDENCE || fieldType == GEO_FIELD_BATTLE_PVP) {
                        sector->m_TerritoryMinZ = static_cast<std::int16_t>(territory->m_Bottom);
                        sector->m_TerritoryMaxZ = static_cast<std::int16_t>(territory->m_Top);
                    }
                }
#if defined(L2_STARTUP_DIAGNOSTICS)
                profile7 += runtime::GetTickCount() - startTick;
#endif
            }
        }
    }
    return set;
}

void CGeoData::ExpandWaterSector() {
    L2_GUARD_FUNCTION();
    if (g_bUseResourceLoader) {
        return;
    }
    for (const FVector& water : m_lstWaterPos) {
        for (int offsetX = -6; offsetX <= 6; ++offsetX) {
            for (int offsetY = -6; offsetY <= 6; ++offsetY) {
                const int x = static_cast<int>(water.x) + offsetX * 128;
                const int y = static_cast<int>(water.y) + offsetY * 128;
                CGeoData* const shared = GeoDataWorld::Inst().GetGeo();
                CGeoZone* const zone = shared->GetZone(ZoneXFromWorld(x), ZoneYFromWorld(y));
                if (!zone) {
                    continue;
                }
                CGeoSector* const sector = zone->GetSector(SectorXFromWorld(x), SectorYFromWorld(y));
                if (!sector) {
                    continue;
                }
                const std::uint16_t waterMask = GeoFieldMask[GEO_FIELD_WATER];
                if ((static_cast<std::uint16_t>(sector->m_Field) & waterMask) == waterMask) {
                    continue;
                }
                CGeoData* const sharedForCell = GeoDataWorld::Inst().GetGeo();
                CGeoCell* const cell = sharedForCell->GetBaseCellFromSector(sector, x, y, static_cast<int>(water.z));
                if (cell) {
                    sector->m_Field = static_cast<std::int16_t>(static_cast<std::uint16_t>(sector->m_Field) | waterMask);
                }
            }
        }
    }
    m_lstWaterPos.clear();
}

CGeoCell* CGeoData::GetNextCell(
    CGeoCell* current, CGeoSector* sector, GeoDirEnum direction, int x, int y, int z, JumpType jumpType
) {
    const std::uint16_t mask = kDirectionMasks[static_cast<std::size_t>(direction)];
    if ((static_cast<std::uint16_t>(current->m_data) & mask) == mask) {
        return GetBaseCellFromSector(sector, x, y, z);
    }
    if (jumpType != Jumpable) {
        return nullptr;
    }

    CGeoCell* const upper = GetClosestUpperCellFromSector(sector, x, y, z, true);
    if (!upper || runtime::AddInt32Wrapping(z, 32) < CellHeight(upper->m_data)) {
        return nullptr;
    }
    return upper;
}

CGeoCell* CGeoData::GetNextCell(CGeoCell* current, GeoDirEnum direction, int x, int y, int z, JumpType jumpType) {
    CGeoSector* const sector = GetSectorByCoord(x, y);
    return sector ? GetNextCell(current, sector, direction, x, y, z, jumpType) : nullptr;
}

CGeoCell* CGeoData::GetNextCellOnFloating(CGeoCell* current, GeoDirEnum direction, int x, int y, int z, JumpType jumpType) {
    CGeoSector* const sector = GetSectorByCoord(x, y);
    if (!sector) {
        return nullptr;
    }

    const std::uint16_t directionMask = kDirectionMasks[static_cast<std::size_t>(direction)];
    if ((static_cast<std::uint16_t>(current->m_data) & directionMask) == directionMask) {
        CGeoCell* result = GetBaseCellFromSector(sector, x, y, z);
        const GeoDirEnum opposite = AntiGeoDirEnum(direction);
        const std::uint16_t oppositeMask = kDirectionMasks[static_cast<std::size_t>(opposite)];
        if ((static_cast<std::uint16_t>(result->m_data) & oppositeMask) != oppositeMask &&
            CellHeight(current->m_data) < CellHeight(result->m_data)) {
            result = GetClosestUpperCellFromSector(sector, x, y, z, false);
            if (!result || z <= CellHeight(result->m_data)) {
                return nullptr;
            }
        }
        return result;
    }

    if (jumpType != Jumpable) {
        return nullptr;
    }
    CGeoCell* const result = GetClosestUpperCellFromSector(sector, x, y, z, true);
    if (!result || runtime::AddInt32Wrapping(z, 32) < CellHeight(result->m_data)) {
        return nullptr;
    }
    return result;
}

CGeoCell* CGeoData::GetNextCellOnFly(CGeoCell* current, GeoDirEnum direction, int x, int y, int z) {
    const std::uint16_t directionMask = kDirectionMasks[static_cast<std::size_t>(direction)];
    if ((static_cast<std::uint16_t>(current->m_data) & directionMask) == directionMask) {
        CGeoCell* const result = GetBaseCell(x, y, z);
        if (!result) {
            return nullptr;
        }
        const GeoDirEnum opposite = AntiGeoDirEnum(direction);
        const std::uint16_t oppositeMask = kDirectionMasks[static_cast<std::size_t>(opposite)];
        if ((static_cast<std::uint16_t>(result->m_data) & oppositeMask) != oppositeMask && z <= CellHeight(result->m_data)) {
            return nullptr;
        }
        return result;
    }

    CGeoSector* const sector = GetSectorByCoord(x, y);
    if (!sector) {
        return nullptr;
    }
    CGeoCell* upper = GetClosestUpperCellFromSector(sector, x, y, z, false);
    CGeoCell* const base = GetBaseCellFromSector(sector, x, y, z);
    if (!upper || !base) {
        return nullptr;
    }
    if (upper != base && CellHeight(current->m_data) <= CellHeight(base->m_data)) {
        upper = base;
    }
    return z <= CellHeight(upper->m_data) ? nullptr : upper;
}

CGeoCell* CGeoData::GetNextHorizontalCellOnSight(CGeoCell* current, GeoDirEnum direction, int x, int y, int z) {
    CGeoSector* const sector = GetSectorByCoord(x, y);
    if (!sector) {
        return nullptr;
    }

    CGeoCell* result = GetNextCell(current, sector, direction, x, y, z, Jumpable);
    if (result) {
        return result;
    }

    result = GetBaseCellFromSector(sector, x, y, z);
    if (!result || z < CellHeight(result->m_data)) {
        return nullptr;
    }

    int previousX = 0;
    int previousY = 0;
    GeoDirEnum opposite = GEO_DIR_EAST;
    switch (direction) {
        case GEO_DIR_EAST:
            opposite = GEO_DIR_WEST;
            previousX = runtime::AddInt32Wrapping(x, -16);
            previousY = y;
            break;
        case GEO_DIR_WEST:
            opposite = GEO_DIR_EAST;
            previousX = runtime::AddInt32Wrapping(x, 16);
            previousY = y;
            break;
        case GEO_DIR_SOUTH:
            opposite = GEO_DIR_NORTH;
            previousX = x;
            previousY = runtime::AddInt32Wrapping(y, -16);
            break;
        case GEO_DIR_NORTH:
            opposite = GEO_DIR_SOUTH;
            previousX = x;
            previousY = runtime::AddInt32Wrapping(y, 16);
            break;
        default:
            break;
    }

    CGeoCell* const reverse = GetNextCell(result, opposite, previousX, previousY, CellHeight(result->m_data), Jumpable);
    return reverse == current ? result : nullptr;
}

CGeoCell* CGeoData::GetNextHorizontalCellOnSightOld(CGeoCell* current, GeoDirEnum direction, int x, int y, int z) {
    CGeoCell* result = GetNextCell(current, direction, x, y, CellHeight(current->m_data), Jumpable);
    if (result) {
        return result;
    }

    GeoDirEnum opposite = GEO_DIR_EAST;
    int previousX = 0;
    int previousY = 0;
    switch (direction) {
        case GEO_DIR_EAST:
            opposite = GEO_DIR_WEST;
            previousX = runtime::AddInt32Wrapping(x, -16);
            previousY = y;
            break;
        case GEO_DIR_WEST:
            opposite = GEO_DIR_EAST;
            previousX = runtime::AddInt32Wrapping(x, 16);
            previousY = y;
            break;
        case GEO_DIR_SOUTH:
            opposite = GEO_DIR_NORTH;
            previousX = x;
            previousY = runtime::AddInt32Wrapping(y, -16);
            break;
        case GEO_DIR_NORTH:
            opposite = GEO_DIR_SOUTH;
            previousX = x;
            previousY = runtime::AddInt32Wrapping(y, 16);
            break;
        default:
            break;
    }

    CGeoSector* const targetSector = GetSectorByCoord(x, y);
    if (!targetSector) {
        return nullptr;
    }
    result = GetBaseCellFromSector(targetSector, x, y, z);
    if (!result || z < CellHeight(result->m_data)) {
        return nullptr;
    }
    CGeoCell* const reverse = GetNextCell(result, opposite, previousX, previousY, CellHeight(result->m_data), Jumpable);
    return reverse == current ? result : nullptr;
}

GeoDirEnum CGeoData::AntiGeoDirEnum(GeoDirEnum direction) {
    return static_cast<GeoDirEnum>(kOppositeDirections[static_cast<std::size_t>(direction)]);
}

double CGeoData::GetVerticalLimit(const FVector& position) {
    double verticalLimit = position.z + 40.0;
    CGeoSector* const sector = GetSectorByCoord(static_cast<int>(position.x), static_cast<int>(position.y));
    if (sector && (sector->m_BooleanFlag & 1u) != 1u && (sector->m_BooleanFlag & 2u) != 2u) {
        CGeoCell* const upperCell = GetClosestUpperCell(position);
        const int upperHeight = CellHeight(upperCell->m_data);
        if (position.z < static_cast<double>(upperHeight)) {
            if (static_cast<double>(upperHeight - 5) <= position.z + 40.0) {
                verticalLimit = static_cast<double>(upperHeight - 5);
            } else {
                verticalLimit = position.z + 40.0;
            }

            const int ground = GeoDataWorld::Inst().GetGeo()->FindGround(
                static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z)
            );
            if (verticalLimit < static_cast<double>(ground)) {
                verticalLimit = static_cast<double>(GeoDataWorld::Inst().GetGeo()->FindGround(
                    static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z)
                ));
            }
        }
    }
    return verticalLimit;
}

#if defined(L2_BUILD_L2SERVER)
bool CGeoData::CanSee(const FVector& source, const FVector& destination, int inZoneId, bool checkWorldCollision) {
    bool canSee = SingleLineCheckForProjectile(source, destination);
    if (canSee && checkWorldCollision) {
        FVector collisionPosition{};
        if (g_WorldPlaneCollision.CheckCollision(source, destination, 0.0, collisionPosition, inZoneId, false) &&
            g_WorldPlaneCollision.CheckCollision(destination, source, 0.0, collisionPosition, inZoneId, false)) {
            canSee = false;
        }
    }
    return canSee;
}

bool CGeoData::CanSee(CWorldObject* source, CWorldObject* destination, bool checkWorldCollision) {
    if (source == nullptr || destination == nullptr) {
        return false;
    }
    const int inZoneId = static_cast<int>(source->GetInZoneID());
    const FVector destinationPosition = destination->GetPos();
    const FVector sourcePosition = source->GetPos();
    return CanSee(sourcePosition, destinationPosition, inZoneId, checkWorldCollision);
}

bool CGeoData::CanSeeOldStyle(const FVector& source, const FVector& destination, int inZoneId, bool checkWorldCollision) {
    L2_GUARD_FUNCTION();
    bool canSee =
        SingleLineCheckWithFlyOld(source, destination, false) || SingleLineCheckWithFlyOld(destination, source, false);
    if (checkWorldCollision) {
        FVector collisionPosition{};
        if (g_WorldPlaneCollision.CheckCollision(source, destination, 0.0, collisionPosition, inZoneId, false)) {
            canSee = false;
        }
    }
    return canSee;
}

bool CGeoData::CanSeeOldStyle(CWorldObject* source, CWorldObject* destination, bool checkWorldCollision) {
    if (source == nullptr || destination == nullptr) {
        return false;
    }
    const int inZoneId = static_cast<int>(source->GetInZoneID());
    const FVector destinationPosition = destination->GetPos();
    const FVector sourcePosition = source->GetPos();
    return CanSeeOldStyle(sourcePosition, destinationPosition, inZoneId, checkWorldCollision);
}
#endif

bool CGeoData::SingleLineCheckForProjectile(const FVector& source, const FVector& destination) {
    FVector upper{};
    FVector lower{};
    if (destination.z < source.z) {
        upper = source;
        lower = destination;
    } else {
        upper = destination;
        lower = source;
    }

    FVector offset{
        lower.x - upper.x,
        lower.y - upper.y,
        lower.z - upper.z,
    };
    if (offset.DistCube({}) < 0.5) {
        return true;
    }

    offset.z = 0.0;
    double horizontalDistance = 0.0;
    if (64.0 <= offset.Size()) {
        horizontalDistance = 64.0;
    } else {
        horizontalDistance = offset.Size();
    }

    upper.z = GetVerticalLimit(upper);
    lower.z = GetVerticalLimit(lower);
    offset.NormalizeSquare();
    offset.x *= horizontalDistance;
    offset.y *= horizontalDistance;
    offset.z *= horizontalDistance;
    offset.z = 16.0;

    const FVector probe{
        upper.x + offset.x,
        upper.y + offset.y,
        upper.z + offset.z,
    };
    return SingleLineCheckWithFlyTopDown(probe, upper) && SingleLineCheckWithFlyTopDown(upper, probe) &&
        SingleLineCheckWithFlyTopDown(probe, lower) && SingleLineCheckWithFlyTopDown(lower, probe);
}

bool CGeoData::SingleLineCheckWithFlyOld(FVector source, FVector destination, bool checkBothDiagonalPaths) {
    FVector lineStep{
        destination.x - source.x,
        destination.y - source.y,
        destination.z - source.z,
    };
    if (lineStep.x * lineStep.x + lineStep.y * lineStep.y + lineStep.z * lineStep.z < 0.5) {
        return true;
    }

    lineStep.NormalizeSquare();
    lineStep.x *= 16.0;
    lineStep.y *= 16.0;
    lineStep.z *= 16.0;

    int gridX = GridXFromWorld(static_cast<int>(source.x));
    int gridY = GridYFromWorld(static_cast<int>(source.y));
    const int destinationGridX = GridXFromWorld(static_cast<int>(destination.x));
    const int destinationGridY = GridYFromWorld(static_cast<int>(destination.y));

    int xStep = 0;
    int minimumX = 0;
    int maximumX = 0;
    if (gridX < destinationGridX) {
        xStep = 1;
        minimumX = gridX;
        maximumX = destinationGridX;
    } else {
        xStep = -1;
        minimumX = destinationGridX;
        maximumX = gridX;
    }

    int yStep = 0;
    int minimumY = 0;
    int maximumY = 0;
    if (gridY < destinationGridY) {
        yStep = 1;
        minimumY = gridY;
        maximumY = destinationGridY;
    } else {
        yStep = -1;
        minimumY = destinationGridY;
        maximumY = gridY;
    }

    const GeoDirEnum directionX = xStep < 1 ? GEO_DIR_WEST : GEO_DIR_EAST;
    const GeoDirEnum directionY = yStep < 1 ? GEO_DIR_NORTH : GEO_DIR_SOUTH;
    const int deltaX = maximumX - minimumX;
    const int deltaY = maximumY - minimumY;
    const bool xIsDominant = deltaY <= deltaX;
    const int majorDelta = xIsDominant ? deltaX : deltaY;
    const int minorDelta = xIsDominant ? deltaY : deltaX;
    int error = minorDelta * 2 - majorDelta;
    const int straightErrorIncrement = minorDelta * 2;
    const int diagonalErrorIncrement = (minorDelta - majorDelta) * 2;

    CGeoCell* currentCell = GetBaseCell(static_cast<int>(source.x), static_cast<int>(source.y), static_cast<int>(source.z));
    double sightZ = static_cast<double>(CellHeight(currentCell->m_data) + 40);

    for (int index = 0; index < majorDelta; ++index) {
        CGeoCell* nextCell = nullptr;
        if (error < 1) {
            if (xIsDominant) {
                nextCell = GetNextHorizontalCellOnSightOld(
                    currentCell, directionX, GridCenterX(gridX + xStep), GridCenterY(gridY), static_cast<int>(sightZ)
                );
                gridX += xStep;
            } else {
                nextCell = GetNextHorizontalCellOnSightOld(
                    currentCell, directionY, GridCenterX(gridX), GridCenterY(gridY + yStep), static_cast<int>(sightZ)
                );
                gridY += yStep;
            }
            if (!nextCell) {
                return false;
            }
            error += straightErrorIncrement;
        } else {
            CGeoCell* intermediate = GetNextHorizontalCellOnSightOld(
                currentCell, directionX, GridCenterX(gridX + xStep), GridCenterY(gridY), static_cast<int>(sightZ)
            );
            if (intermediate) {
                nextCell = GetNextHorizontalCellOnSightOld(
                    intermediate, directionY, GridCenterX(gridX + xStep), GridCenterY(gridY + yStep),
                    static_cast<int>(sightZ)
                );
            }
            if (checkBothDiagonalPaths && !nextCell) {
                return false;
            }

            if (checkBothDiagonalPaths || !nextCell) {
                intermediate = GetNextHorizontalCellOnSightOld(
                    currentCell, directionY, GridCenterX(gridX), GridCenterY(gridY + yStep), static_cast<int>(sightZ)
                );
                if (!intermediate) {
                    return false;
                }
                nextCell = GetNextHorizontalCellOnSightOld(
                    intermediate, directionX, GridCenterX(gridX + xStep), GridCenterY(gridY + yStep),
                    static_cast<int>(sightZ)
                );
                if (!nextCell) {
                    return false;
                }
            }

            gridX += xStep;
            gridY += yStep;
            error += diagonalErrorIncrement;
        }
        currentCell = nextCell;
        sightZ += lineStep.z;
    }

    const int destinationGround =
        FindGround(static_cast<int>(destination.x), static_cast<int>(destination.y), static_cast<int>(destination.z));
    const int heightDifference = destinationGround - CellHeight(currentCell->m_data);
    return heightDifference * heightDifference < 0x1001;
}

bool CGeoData::SingleLineCheckWithFlyTopDown(const FVector& source, const FVector& destination) {
    const GeoDirEnum verticalDirection = destination.z < source.z ? GEO_DIR_BELOW : GEO_DIR_ABOVE;
    const FVector lineDelta{
        destination.x - source.x,
        destination.y - source.y,
        destination.z - source.z,
    };
    if (lineDelta.x * lineDelta.x + lineDelta.y * lineDelta.y + lineDelta.z * lineDelta.z < 0.5) {
        return true;
    }

    int gridX = GridXFromWorld(static_cast<int>(source.x));
    int gridY = GridYFromWorld(static_cast<int>(source.y));
    const int destinationGridX = GridXFromWorld(static_cast<int>(destination.x));
    const int destinationGridY = GridYFromWorld(static_cast<int>(destination.y));

    int xStep = 0;
    int minimumX = 0;
    int maximumX = 0;
    if (gridX < destinationGridX) {
        xStep = 1;
        minimumX = gridX;
        maximumX = destinationGridX;
    } else {
        xStep = -1;
        minimumX = destinationGridX;
        maximumX = gridX;
    }

    int yStep = 0;
    int minimumY = 0;
    int maximumY = 0;
    if (gridY < destinationGridY) {
        yStep = 1;
        minimumY = gridY;
        maximumY = destinationGridY;
    } else {
        yStep = -1;
        minimumY = destinationGridY;
        maximumY = gridY;
    }

    const GeoDirEnum directionX = xStep < 1 ? GEO_DIR_WEST : GEO_DIR_EAST;
    const GeoDirEnum directionY = yStep < 1 ? GEO_DIR_NORTH : GEO_DIR_SOUTH;
    const int deltaX = maximumX - minimumX;
    const int deltaY = maximumY - minimumY;
    const bool xIsDominant = deltaY <= deltaX;
    const int majorDelta = xIsDominant ? deltaX : deltaY;
    const int minorDelta = xIsDominant ? deltaY : deltaX;
    int error = minorDelta * 2 - majorDelta;
    const int straightErrorIncrement = minorDelta * 2;
    const int diagonalErrorIncrement = (minorDelta - majorDelta) * 2;

    const double verticalStep = majorDelta > 0 ? lineDelta.z / static_cast<double>(majorDelta) : lineDelta.z;
    const double collisionDistance = std::abs(verticalStep);
    CGeoCell* currentCell = GetBaseCell(static_cast<int>(source.x), static_cast<int>(source.y), static_cast<int>(source.z));
    double sightZ = source.z;

    const auto collidesVertically = [&]() {
        const FVector position{
            static_cast<double>(GridCenterX(gridX)),
            static_cast<double>(GridCenterY(gridY)),
            sightZ,
        };
        return CollideWithTopOrBottom(verticalDirection, position, collisionDistance);
    };

    if (majorDelta == 0) {
        return !collidesVertically();
    }

    for (int index = 0; index < majorDelta; ++index) {
        if (collidesVertically()) {
            return false;
        }

        CGeoCell* nextCell = nullptr;
        if (error < 1) {
            if (xIsDominant) {
                nextCell = GetNextHorizontalCellOnSight(
                    currentCell, directionX, GridCenterX(gridX + xStep), GridCenterY(gridY), static_cast<int>(sightZ)
                );
                gridX += xStep;
            } else {
                nextCell = GetNextHorizontalCellOnSight(
                    currentCell, directionY, GridCenterX(gridX), GridCenterY(gridY + yStep), static_cast<int>(sightZ)
                );
                gridY += yStep;
            }
            if (!nextCell) {
                return false;
            }
            error += straightErrorIncrement;
        } else {
            CGeoCell* intermediate = GetNextHorizontalCellOnSight(
                currentCell, directionX, GridCenterX(gridX + xStep), GridCenterY(gridY), static_cast<int>(sightZ)
            );
            if (intermediate) {
                nextCell = GetNextHorizontalCellOnSight(
                    intermediate, directionY, GridCenterX(gridX + xStep), GridCenterY(gridY + yStep),
                    static_cast<int>(sightZ)
                );
            }
            if (!nextCell) {
                intermediate = GetNextHorizontalCellOnSight(
                    currentCell, directionY, GridCenterX(gridX), GridCenterY(gridY + yStep), static_cast<int>(sightZ)
                );
                if (!intermediate) {
                    return false;
                }
                nextCell = GetNextHorizontalCellOnSight(
                    intermediate, directionX, GridCenterX(gridX + xStep), GridCenterY(gridY + yStep),
                    static_cast<int>(sightZ)
                );
                if (!nextCell) {
                    return false;
                }
            }

            gridX += xStep;
            gridY += yStep;
            error += diagonalErrorIncrement;
        }
        currentCell = nextCell;
        sightZ += verticalStep;
    }
    return true;
}

bool CGeoData::MoveStraight(
    const FVector& source,
    const FVector& destination,
    double distance,
    FVector& outputPosition,
    bool& distanceExhausted,
    double& travelledDistance
) {
    outputPosition = source;
    distanceExhausted = false;
    travelledDistance = 0.0;
    FVector step{
        destination.x - source.x,
        destination.y - source.y,
        destination.z - source.z,
    };
    GeoDirEnum directionX{};
    GeoDirEnum directionY{};
    DecideDirection(step, &directionX, &directionY, nullptr);
    step.z = 0.0;
    const double horizontalSquared = step.x * step.x + step.y * step.y;
    if (horizontalSquared < 1e-6) {
        return GetBaseCell(
                   runtime::Win64CvttDoubleToInt32(source.x), runtime::Win64CvttDoubleToInt32(source.y),
                   runtime::Win64CvttDoubleToInt32(source.z)
               ) ==
            GetBaseCell(
                runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y),
                runtime::Win64CvttDoubleToInt32(destination.z)
            );
    }

    step.NormalizeSquare();
    step.x *= 16.0;
    step.y *= 16.0;
    step.z *= 16.0;

    CGeoCell* currentCell = GetBaseCell(
        runtime::Win64CvttDoubleToInt32(source.x), runtime::Win64CvttDoubleToInt32(source.y),
        runtime::Win64CvttDoubleToInt32(source.z)
    );
    if (!currentCell) {
        return false;
    }

    FVector current = source;
    int currentGridX = GridXFromWorld(runtime::Win64CvttDoubleToInt32(source.x));
    int currentGridY = GridYFromWorld(runtime::Win64CvttDoubleToInt32(source.y));
    double stepSquared = step.x * step.x + step.y * step.y;
    bool reachedDestination = false;
    bool reducedStep = false;
    const auto fail = [&]() {
        outputPosition = current;
        return false;
    };

    while (travelledDistance <= distance && distance != travelledDistance && !reachedDestination) {
        if (!reducedStep && distance - travelledDistance < 16.0) {
            const auto originalStep = step;
            step.x *= 0.125;
            step.y *= 0.125;
            step.z *= 0.125;
            stepSquared = step.x * step.x + step.y * step.y;
            if (stepSquared < 0.5) {
                step = originalStep;
                stepSquared = step.x * step.x + step.y * step.y;
            }
            reducedStep = true;
        }

        FVector candidate{};
        if (stepSquared <= DistanceSquared2D(current, destination)) {
            candidate = {
                current.x + step.x,
                current.y + step.y,
                current.z + step.z,
            };
        } else {
            candidate = destination;
            reachedDestination = true;
            step = {
                destination.x - current.x,
                destination.y - current.y,
                destination.z - current.z,
            };
            stepSquared = DistanceSquared2D(current, candidate);
        }

        const int nextGridX = GridXFromWorld(runtime::Win64CvttDoubleToInt32(candidate.x));
        const int nextGridY = GridYFromWorld(runtime::Win64CvttDoubleToInt32(candidate.y));
        const int deltaX = runtime::AbsoluteInt32Wrapping(runtime::SubtractInt32Wrapping(currentGridX, nextGridX));
        const int deltaY = runtime::AbsoluteInt32Wrapping(runtime::SubtractInt32Wrapping(currentGridY, nextGridY));
        CGeoCell* nextCell = nullptr;

        if (deltaX == 0 && deltaY == 0) {
            nextCell = currentCell;
            if (!nextCell) {
                // LogMoveStraightError(0x90d)
                LogMoveStraightError("Ground", "current cell is missing", currentGridX, currentGridY, nextGridX, nextGridY);
                return fail();
            }
        } else if (deltaX == 1 && deltaY == 0) {
            const std::uint16_t mask = kDirectionMasks[static_cast<std::size_t>(directionX)];
            if ((static_cast<std::uint16_t>(currentCell->m_data) & mask) != mask) {
                return fail();
            }
            nextCell = GetNextCell(
                currentCell, directionX, GridCenterX(nextGridX), GridCenterY(currentGridY), CellHeight(currentCell->m_data),
                NoJump
            );
        } else if (deltaX == 0 && deltaY == 1) {
            const std::uint16_t mask = kDirectionMasks[static_cast<std::size_t>(directionY)];
            if ((static_cast<std::uint16_t>(currentCell->m_data) & mask) != mask) {
                return fail();
            }
            nextCell = GetNextCell(
                currentCell, directionY, GridCenterX(currentGridX), GridCenterY(nextGridY), CellHeight(currentCell->m_data),
                NoJump
            );
        } else if (deltaX == 1 && deltaY == 1) {
            const std::uint16_t xMask = kDirectionMasks[static_cast<std::size_t>(directionX)];
            const std::uint16_t yMask = kDirectionMasks[static_cast<std::size_t>(directionY)];
            if ((static_cast<std::uint16_t>(currentCell->m_data) & xMask) != xMask) {
                return fail();
            }
            CGeoCell* intermediate = GetNextCell(
                currentCell, directionX, GridCenterX(nextGridX), GridCenterY(currentGridY), CellHeight(currentCell->m_data),
                NoJump
            );
            if (!intermediate) {
                return fail();
            }
            nextCell = GetNextCell(
                intermediate, directionY, GridCenterX(nextGridX), GridCenterY(nextGridY), CellHeight(intermediate->m_data),
                NoJump
            );
            if (!nextCell || (static_cast<std::uint16_t>(currentCell->m_data) & yMask) != yMask) {
                return fail();
            }
            intermediate = GetNextCell(
                currentCell, directionY, GridCenterX(currentGridX), GridCenterY(nextGridY), CellHeight(currentCell->m_data),
                NoJump
            );
            if (!intermediate) {
                return fail();
            }
            nextCell = GetNextCell(
                intermediate, directionX, GridCenterX(nextGridX), GridCenterY(nextGridY), CellHeight(intermediate->m_data),
                NoJump
            );
            if (!nextCell) {
                return fail();
            }
        } else {
            // LogMoveStraightError(currentGridX == nextGridX || currentGridY == nextGridY ? 0x971 : 0x917)
            LogMoveStraightError("Ground", "unexpected grid step", currentGridX, currentGridY, nextGridX, nextGridY);
            return fail();
        }

        if (!nextCell) {
            return fail();
        }
        travelledDistance += std::sqrt(stepSquared);
        current = candidate;
        current.z = static_cast<double>(CellHeight(nextCell->m_data));
        currentCell = nextCell;
        currentGridX = nextGridX;
        currentGridY = nextGridY;
    }

    if (reachedDestination) {
        const int heightDelta = runtime::AbsoluteInt32Wrapping(
            runtime::SubtractInt32Wrapping(
                CellHeight(currentCell->m_data), runtime::Win64CvttDoubleToInt32(destination.z)
            )
        );
        if (heightDelta > 48) {
            distanceExhausted = true;
            outputPosition = current;
            return false;
        }
    } else {
        distanceExhausted = true;
    }
    outputPosition = current;
    return true;
}

bool CGeoData::MoveStraightFloating(
    const FVector& source,
    const FVector& destination,
    double distance,
    FVector& outputPosition,
    bool& distanceExhausted,
    int verticalLimit
) {
    outputPosition = source;
    distanceExhausted = false;
    FVector step{
        destination.x - source.x,
        destination.y - source.y,
        destination.z - source.z,
    };
    GeoDirEnum directionX{};
    GeoDirEnum directionY{};
    DecideDirection(step, &directionX, &directionY, nullptr);
    const double horizontalSquared = step.x * step.x + step.y * step.y;
    if (horizontalSquared < 1e-6) {
        return GetBaseCell(
                   runtime::Win64CvttDoubleToInt32(source.x), runtime::Win64CvttDoubleToInt32(source.y),
                   runtime::Win64CvttDoubleToInt32(source.z)
               ) ==
            GetBaseCell(
                runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y),
                runtime::Win64CvttDoubleToInt32(destination.z)
            );
    }

    step.NormalizeCube();
    step.x *= 16.0;
    step.y *= 16.0;
    step.z *= 16.0;
    double stepHorizontalSquared = step.x * step.x + step.y * step.y;

    FVector current = source;
    // SHIPPING BUG GEO-NULL-001: MoveStraightFloating at current
    // L2NPC.exe 0x00433fa8 continued from this lookup without testing the
    // result. The first cross-cell sight transition then dereferenced null.
    // Preserve the recovered lookup-without-guard as disabled source.
    /*
    CGeoCell* currentCell =
        GetBaseCell(
            runtime::Win64CvttDoubleToInt32(source.x),
            runtime::Win64CvttDoubleToInt32(source.y),
            runtime::Win64CvttDoubleToInt32(source.z)).
    */

    CGeoCell* currentCell = GetBaseCell(
        runtime::Win64CvttDoubleToInt32(source.x), runtime::Win64CvttDoubleToInt32(source.y),
        runtime::Win64CvttDoubleToInt32(source.z)
    );
    if (!currentCell) {
        return false;
    }
    int currentGridX = GridXFromWorld(runtime::Win64CvttDoubleToInt32(source.x));
    int currentGridY = GridYFromWorld(runtime::Win64CvttDoubleToInt32(source.y));
    double travelled = 0.0;
    bool reachedDestination = false;
    bool reducedStep = false;
    const auto fail = [&]() {
        outputPosition = current;
        return false;
    };

    while (travelled < distance && !reachedDestination) {
        if (!reducedStep && distance - travelled < 16.0) {
            const auto originalStep = step;
            step.x /= 8.0;
            step.y /= 8.0;
            step.z /= 8.0;
            stepHorizontalSquared = step.x * step.x + step.y * step.y;
            if (stepHorizontalSquared < 0.5) {
                step = originalStep;
                stepHorizontalSquared = step.x * step.x + step.y * step.y;
            }
            reducedStep = true;
        }

        FVector candidate{};
        if (stepHorizontalSquared <= DistanceSquared2D(current, destination)) {
            candidate = {
                current.x + step.x,
                current.y + step.y,
                current.z + step.z,
            };
        } else {
            candidate = destination;
            reachedDestination = true;
            step = {
                destination.x - current.x,
                destination.y - current.y,
                destination.z - current.z,
            };
            stepHorizontalSquared = DistanceSquared2D(current, candidate);
        }

        const int nextGridX = GridXFromWorld(runtime::Win64CvttDoubleToInt32(candidate.x));
        const int nextGridY = GridYFromWorld(runtime::Win64CvttDoubleToInt32(candidate.y));
        const int deltaX = runtime::AbsoluteInt32Wrapping(runtime::SubtractInt32Wrapping(currentGridX, nextGridX));
        const int deltaY = runtime::AbsoluteInt32Wrapping(runtime::SubtractInt32Wrapping(currentGridY, nextGridY));
        CGeoCell* nextCell = nullptr;

        if (deltaX == 0 && deltaY == 0) {
            nextCell = currentCell;
            if (!nextCell) {
                // LogMoveStraightError(0x7e7)
                LogMoveStraightError(
                    "Floating", "current cell is missing", currentGridX, currentGridY, nextGridX, nextGridY
                );
                return fail();
            }
        } else if (deltaX == 1 && deltaY == 0) {
            nextCell = GetNextHorizontalCellOnSightOld(
                currentCell, directionX, GridCenterX(nextGridX), GridCenterY(currentGridY),
                runtime::Win64CvttDoubleToInt32(current.z)
            );
        } else if (deltaX == 0 && deltaY == 1) {
            nextCell = GetNextHorizontalCellOnSightOld(
                currentCell, directionY, GridCenterX(currentGridX), GridCenterY(nextGridY),
                runtime::Win64CvttDoubleToInt32(current.z)
            );
        } else if (deltaX == 1 && deltaY == 1) {
            CGeoCell* intermediate = GetNextHorizontalCellOnSightOld(
                currentCell, directionX, GridCenterX(nextGridX), GridCenterY(currentGridY),
                runtime::Win64CvttDoubleToInt32(current.z)
            );
            if (!intermediate) {
                return fail();
            }
            nextCell = GetNextHorizontalCellOnSightOld(
                intermediate, directionY, GridCenterX(nextGridX), GridCenterY(nextGridY),
                runtime::Win64CvttDoubleToInt32(current.z)
            );
            if (!nextCell) {
                return fail();
            }

            intermediate = GetNextHorizontalCellOnSightOld(
                currentCell, directionY, GridCenterX(currentGridX), GridCenterY(nextGridY),
                runtime::Win64CvttDoubleToInt32(current.z)
            );
            if (!intermediate) {
                return fail();
            }
            nextCell = GetNextHorizontalCellOnSightOld(
                intermediate, directionX, GridCenterX(nextGridX), GridCenterY(nextGridY),
                runtime::Win64CvttDoubleToInt32(current.z)
            );
            if (!nextCell) {
                return fail();
            }
        } else {
            // LogMoveStraightError(currentGridX == nextGridX || currentGridY == nextGridY ? 0x825 : 0x7f5)
            LogMoveStraightError("Floating", "unexpected grid step", currentGridX, currentGridY, nextGridX, nextGridY);
            return fail();
        }

        if (!nextCell) {
            return fail();
        }
        const int nextHeight = CellHeight(nextCell->m_data);
        if (runtime::Win64CvttDoubleToInt32(candidate.z) <= nextHeight) {
            candidate.z = static_cast<double>(nextHeight);
        }

        travelled += std::sqrt(step.x * step.x + step.y * step.y + step.z * step.z);
        current = candidate;
        currentCell = nextCell;
        currentGridX = nextGridX;
        currentGridY = nextGridY;

        if (verticalLimit < nextHeight) {
            current.z = static_cast<double>(nextHeight);
        } else if (verticalLimit <= runtime::Win64CvttDoubleToInt32(current.z)) {
            current.z = static_cast<double>(runtime::AddInt32Wrapping(verticalLimit, -1));
        }
    }
    if (!reachedDestination) {
        distanceExhausted = true;
    }
    outputPosition = current;
    return true;
}

bool CGeoData::MoveStraightFly(
    const FVector& source,
    const FVector& destination,
    double distance,
    FVector& outputPosition,
    bool& distanceExhausted,
    double collisionHeight,
    bool legacyCollisionCheck,
    double& travelledDistance
) {
    // This parameter is present in both the PDB signature and all shipping
    // call sites, but the optimized GeoData.cpp body never reads it.
    static_cast<void>(legacyCollisionCheck);

    distanceExhausted = false;
    FVector step{
        destination.x - source.x,
        destination.y - source.y,
        destination.z - source.z,
    };
    GeoDirEnum directionX{};
    GeoDirEnum directionY{};
    GeoDirEnum verticalDirection{};
    DecideDirection(step, &directionX, &directionY, &verticalDirection);

    if (DistanceSquared3D(source, destination) < 1e-6) {
        distanceExhausted = false;
        outputPosition = source;
        return GetBaseCell(static_cast<int>(source.x), static_cast<int>(source.y), static_cast<int>(source.z)) ==
            GetBaseCell(static_cast<int>(destination.x), static_cast<int>(destination.y), static_cast<int>(destination.z));
    }

    step.NormalizeCube();
    step.x *= 16.0;
    step.y *= 16.0;
    step.z *= 16.0;

    CGeoCell* currentCell = GetBaseCell(static_cast<int>(source.x), static_cast<int>(source.y), static_cast<int>(source.z));
    if (!currentCell) {
        // Log.Add(
        //     LOG_ERROR, L"[%s][%d] No cell at (%d,%d,%d)", L"..\\Shared\\GeoData.cpp", 0x9d2, static_cast<int>(source.x),
        //     static_cast<int>(source.y), static_cast<int>(source.z)
        // )
        L2_LOG_ERROR(
            Log,
            "[geodata] Flying movement check has no source cell at (%d,%d,%d). Returning failure with the source as output "
            "position.",
            static_cast<int>(source.x), static_cast<int>(source.y), static_cast<int>(source.z)
        );
        distanceExhausted = false;
        outputPosition = source;
        return false;
    }

    int currentGridX = GridXFromWorld(static_cast<int>(source.x));
    int currentGridY = GridYFromWorld(static_cast<int>(source.y));
    double stepSquared = step.x * step.x + step.y * step.y + step.z * step.z;
    travelledDistance = 0.0;

    bool reachedDestination = false;
    bool reducedStep = false;
    bool firstStep = true;
    bool collided = false;
    FVector current = source;
    const int halfCollisionHeight = static_cast<int>(collisionHeight / 2.0);

    const auto collidesAt = [&](const FVector& position) {
        collided = CollideWithTopOrBottom(
            verticalDirection, static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z),
            collisionHeight, current
        );
        return collided;
    };

    const auto finishFailure = [&]() {
        if (collided) {
            if (verticalDirection == GEO_DIR_ABOVE) {
                const int nextZ = static_cast<int>(current.z) + 16;
                if (!CollideWithTopOrBottom(
                        verticalDirection, static_cast<int>(current.x), static_cast<int>(current.y), nextZ, collisionHeight,
                        current
                    )) {
                    current.z += 16.0;
                }
            } else if (verticalDirection == GEO_DIR_BELOW) {
                const int nextZ = static_cast<int>(current.z) - 16;
                if (!CollideWithTopOrBottom(
                        verticalDirection, static_cast<int>(current.x), static_cast<int>(current.y), nextZ, collisionHeight,
                        current
                    )) {
                    current.z -= 16.0;
                }
            }
        }
        outputPosition = current;
        return false;
    };

    while (travelledDistance <= distance && distance != travelledDistance && !reachedDestination) {
        if (!reducedStep && distance - travelledDistance < 16.0) {
            const FVector originalStep = step;
            step.x *= 0.125;
            step.y *= 0.125;
            step.z *= 0.125;
            stepSquared = step.x * step.x + step.y * step.y + step.z * step.z;
            if (stepSquared < 0.5) {
                step = originalStep;
                stepSquared = step.x * step.x + step.y * step.y + step.z * step.z;
            }
            reducedStep = true;
        }

        FVector candidate{};
        if (stepSquared <= DistanceSquared3D(current, destination)) {
            candidate = {
                current.x + step.x,
                current.y + step.y,
                current.z + step.z,
            };
        } else {
            candidate = destination;
            reachedDestination = true;
            step = {
                destination.x - current.x,
                destination.y - current.y,
                destination.z - current.z,
            };
            stepSquared = DistanceSquared3D(current, candidate);
        }

        const int nextGridX = GridXFromWorld(static_cast<int>(candidate.x));
        const int nextGridY = GridYFromWorld(static_cast<int>(candidate.y));
        const int deltaX = std::abs(currentGridX - nextGridX);
        const int deltaY = std::abs(currentGridY - nextGridY);
        CGeoCell* nextCell = nullptr;

        if (deltaX == 0 && deltaY == 0) {
            if (firstStep && collidesAt(current)) {
                return finishFailure();
            }
            if (collidesAt(candidate)) {
                return finishFailure();
            }
            nextCell = currentCell;
            if (!nextCell) {
                // LogMoveStraightError(0xa33)
                LogMoveStraightError("Flying", "current cell is missing", currentGridX, currentGridY, nextGridX, nextGridY);
                return finishFailure();
            }
        } else if (deltaX == 1 && deltaY == 0) {
            if (firstStep && collidesAt(current)) {
                return finishFailure();
            }
            if (collidesAt(candidate)) {
                return finishFailure();
            }
            nextCell = GetNextCellOnFly(
                currentCell, directionX, GridCenterX(nextGridX), GridCenterY(currentGridY),
                static_cast<int>(current.z) + halfCollisionHeight
            );
            if (!nextCell) {
                return finishFailure();
            }
        } else if (deltaX == 0 && deltaY == 1) {
            if (firstStep && collidesAt(current)) {
                return finishFailure();
            }
            if (collidesAt(candidate)) {
                return finishFailure();
            }
            nextCell = GetNextCellOnFly(
                currentCell, directionY, GridCenterX(currentGridX), GridCenterY(nextGridY),
                static_cast<int>(current.z) + halfCollisionHeight
            );
            if (!nextCell) {
                return finishFailure();
            }
        } else if (deltaX == 1 && deltaY == 1) {
            if (firstStep && collidesAt(current)) {
                return finishFailure();
            }
            if (collidesAt(candidate)) {
                return finishFailure();
            }

            CGeoCell* intermediate = GetNextCellOnFly(
                currentCell, directionX, GridCenterX(nextGridX), GridCenterY(currentGridY),
                static_cast<int>(current.z) + halfCollisionHeight
            );
            if (!intermediate) {
                return finishFailure();
            }
            nextCell = GetNextCellOnFly(
                intermediate, directionY, GridCenterX(nextGridX), GridCenterY(nextGridY),
                static_cast<int>(current.z) + halfCollisionHeight
            );
            if (!nextCell) {
                return finishFailure();
            }

            intermediate = GetNextCellOnFly(
                currentCell, directionY, GridCenterX(currentGridX), GridCenterY(nextGridY),
                static_cast<int>(current.z) + halfCollisionHeight
            );
            if (!intermediate) {
                return finishFailure();
            }
            nextCell = GetNextCellOnFly(
                intermediate, directionX, GridCenterX(nextGridX), GridCenterY(nextGridY),
                static_cast<int>(current.z) + halfCollisionHeight
            );
            if (!nextCell) {
                return finishFailure();
            }
        } else {
            // LogMoveStraightError(currentGridX == nextGridX || currentGridY == nextGridY ? 0xa3d : 0xab1)
            LogMoveStraightError("Flying", "unexpected grid step", currentGridX, currentGridY, nextGridX, nextGridY);
            return finishFailure();
        }

        travelledDistance += std::sqrt(stepSquared);
        currentGridX = nextGridX;
        currentGridY = nextGridY;
        current = candidate;
        currentCell = nextCell;
        const int groundHeight = CellHeight(currentCell->m_data);
        if (static_cast<int>(current.z) < groundHeight) {
            current.z = static_cast<double>(groundHeight);
        }
        firstStep = false;
    }

    if (!reachedDestination) {
        distanceExhausted = true;
    }
    outputPosition = current;
    return true;
}

bool CGeoData::MoveStraightTest(
    const FVector& source, const FVector& destination, double distance, bool floating, int verticalLimit
) {
    FVector outputPosition{};
    double travelledDistance = 0.0;
    bool distanceExhausted = false;
    return MoveStraightTest(
        source, destination, outputPosition, travelledDistance, distanceExhausted, distance, floating, verticalLimit
    );
}

bool CGeoData::MoveStraightTest(
    const FVector& source,
    const FVector& destination,
    FVector& outputPosition,
    double& travelledDistance,
    bool& distanceExhausted,
    double distance,
    bool floating,
    int verticalLimit
) {
    if (floating) {
        return MoveStraightFloating(source, destination, distance, outputPosition, distanceExhausted, verticalLimit);
    }
    return MoveStraight(source, destination, distance, outputPosition, distanceExhausted, travelledDistance);
}

bool CGeoData::MoveStraightTestBoth(
    const FVector& source, const FVector& destination, double distance, bool bothDirections, bool floating, int verticalLimit
) {
    const bool forward = MoveStraightTest(source, destination, distance, floating, verticalLimit);
    if (forward) {
        if (bothDirections) {
            return MoveStraightTest(destination, source, distance, floating, verticalLimit);
        }
        return true;
    }
    if (bothDirections) {
        return false;
    }
    return MoveStraightTest(destination, source, distance, floating, verticalLimit);
}

void CGeoData::LinkWithSharedGeoData() {
    L2_GUARD_FUNCTION();
    // Shipping 0x00432cc0 copies every m_SharedZone entry into the
    // matching embedded CGeoZone::m_data field.
    for (int x = 0; x < 20; ++x) {
        for (int y = 0; y < 17; ++y) {
            m_Zone[x][y].m_data = m_SharedZone[x][y];
        }
    }
}
