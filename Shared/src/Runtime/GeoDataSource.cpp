#include "Shared/src/Runtime/GeoDataSource.h"
#include "Shared/src/Runtime/Utf8.h"

#if defined(L2_BUILD_L2SERVER)
#include "CConfig.h"
#else
#include "L2NPC/src/Config.h"
#endif
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/GeoDataMapping.h"
#include "Shared/src/Runtime/LittleEndian.h"

#include <array>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

namespace l2geo::portable {
namespace {

using detail::MappedFile;
using detail::PortableGeoIndex;
using detail::PortableMappedZone;

constexpr std::uint16_t kHeightMask = 0xfff0u;
constexpr std::uint16_t kOpenMask = 0x000fu;

std::int16_t MakeFlatCell(std::int16_t sourceHeight) {
    const std::uint16_t shifted = static_cast<std::uint16_t>(static_cast<std::uint16_t>(sourceHeight) << 1);
    return static_cast<std::int16_t>((shifted & kHeightMask) | kOpenMask);
}

bool AddChecked(std::size_t& offset, std::size_t amount, std::size_t limit) {
    if (amount > limit || offset > limit - amount) {
        return false;
    }
    offset += amount;
    return true;
}

} // namespace

bool GeoDataSource::LoadDirectory(
    CGeoData& geodata, const std::filesystem::path& directory, std::string* error, GeoDataLoadStats* stats
) {
    const std::filesystem::path indexPath = directory / "geo_index.txt";
    std::ifstream index(indexPath);
    if (!index) {
        const std::string message = "cannot open geodata index " + indexPath.string();
        if (error) {
            *error = message;
        }
        return false;
    }

    int declaredZoneCount = 0;
    if (!(index >> declaredZoneCount) || declaredZoneCount < 0 || declaredZoneCount > 340) {
        const std::string message = "invalid zone count in " + indexPath.string();
        if (error) {
            *error = message;
        }
        return false;
    }

    std::vector<std::filesystem::path> files;
    files.reserve(static_cast<std::size_t>(declaredZoneCount));
    for (int zoneNumber = 0; zoneNumber < declaredZoneCount; ++zoneNumber) {
        std::string fileName;
        if (!(index >> fileName) || fileName.empty()) {
            const std::string message = "missing zone filename #" + std::to_string(zoneNumber) + " in " + indexPath.string();
            if (error) {
                *error = message;
            }
            return false;
        }
        files.emplace_back(fileName);
    }
    return LoadFiles(geodata, directory, files, error, stats);
}

bool GeoDataSource::LoadFiles(
    CGeoData& geodata,
    const std::filesystem::path& directory,
    const std::vector<std::filesystem::path>& files,
    std::string* error,
    GeoDataLoadStats* stats
) {
    const int declaredZoneCount = static_cast<int>(files.size());
    auto loadedIndex = std::make_unique<PortableGeoIndex>();
    loadedIndex->owned = std::make_unique<CGeoDataIndexStruct>();
    loadedIndex->owned->zone_num = static_cast<std::int16_t>(declaredZoneCount);

    std::array<std::unique_ptr<PortableMappedZone>, 20 * 17> loadedZones{};
    GeoDataLoadStats loadedStats{};
    for (int zoneNumber = 0; zoneNumber < declaredZoneCount; ++zoneNumber) {
        auto zone = std::make_unique<PortableMappedZone>();
        zone->path = directory / files[static_cast<std::size_t>(zoneNumber)];
        std::string mapError;
        if (!zone->file.Open(zone->path, mapError)) {
            if (error) {
                *error = mapError;
            }
            return false;
        }
        if (zone->file.Size() < 18 || zone->file.Size() > std::numeric_limits<std::uint32_t>::max()) {
            const std::string message = "unsupported geodata file length for " + zone->path.string();
            if (error) {
                *error = message;
            }
            return false;
        }

        const std::uint8_t* bytes = zone->file.Data();
        zone->x = bytes[0];
        zone->y = bytes[1];
        const std::int16_t sectorSize = runtime::LoadLittleEndianInt16(bytes + 2);
        const std::int16_t cellSize = runtime::LoadLittleEndianInt16(bytes + 4);
        zone->declaredCells = runtime::LoadLittleEndianInt32(bytes + 6);
        zone->declaredNonComplexSectors = runtime::LoadLittleEndianInt32(bytes + 10);
        zone->declaredFlatSectors = runtime::LoadLittleEndianInt32(bytes + 14);

        if (zone->x < CGeoData::kZoneXMinimum || zone->x > CGeoData::kZoneXMaximum || zone->y < CGeoData::kZoneYMinimum ||
            zone->y > CGeoData::kZoneYMaximum || sectorSize != 128 || cellSize != 16 || zone->declaredCells < 0 ||
            zone->declaredNonComplexSectors < 0 || zone->declaredNonComplexSectors > 65536 ||
            zone->declaredFlatSectors < 0 || zone->declaredFlatSectors > zone->declaredNonComplexSectors) {
            const std::string message = "invalid geodata header in " + zone->path.string();
            if (error) {
                *error = message;
            }
            return false;
        }

        const std::size_t slot = static_cast<std::size_t>(zone->x - CGeoData::kZoneXMinimum) * CGeoData::kZoneYCount +
            static_cast<std::size_t>(zone->y - CGeoData::kZoneYMinimum);
        if (loadedZones[slot]) {
            const std::string message = "duplicate geodata zone " + std::to_string(zone->x) + "_" + std::to_string(zone->y);
            if (error) {
                *error = message;
            }
            return false;
        }

        zone->ownedZone = std::make_unique<CSharedGeoZone>();
        zone->ownedZone->zone_x = static_cast<std::int8_t>(zone->x);
        zone->ownedZone->zone_y = static_cast<std::int8_t>(zone->y);
        zone->ownedZone->num_cell = zone->declaredCells;
        zone->ownedCells.resize(static_cast<std::size_t>(zone->declaredCells));
        const std::size_t declaredComplexSectors = static_cast<std::size_t>(65536 - zone->declaredNonComplexSectors);
        zone->ownedCellOffsets.resize(declaredComplexSectors * 64);

        std::size_t cursor = 18;
        std::uint64_t actualCells = 0;
        std::uint64_t actualFlatSectors = 0;
        std::uint64_t actualSimpleSectors = 0;
        std::uint64_t actualComplexSectors = 0;
        std::size_t cellOffsetCursor = 0;

        for (std::size_t sectorIndex = 0; sectorIndex < 65536; ++sectorIndex) {
            if (cursor > zone->file.Size() - 2) {
                const std::string message =
                    "truncated sector table in " + zone->path.string() + " at sector " + std::to_string(sectorIndex);
                if (error) {
                    *error = message;
                }
                return false;
            }
            CGeoSector& sector = zone->ownedZone->sectors[sectorIndex / 256][sectorIndex % 256];
            sector.cell_segment = static_cast<std::int32_t>(actualCells);
            sector.cell_offset_index = static_cast<std::int32_t>(cellOffsetCursor);
            const std::int16_t sectorCellCount = runtime::LoadLittleEndianInt16(bytes + cursor);
            sector.num_cell = sectorCellCount;
            cursor += 2;

            if (sectorCellCount == 0) {
                if (!AddChecked(cursor, 4, zone->file.Size())) {
                    const std::string message = "truncated flat sector in " + zone->path.string();
                    if (error) {
                        *error = message;
                    }
                    return false;
                }
                sector.m_BooleanFlag = 3;
                sector.m_DefaultCell.m_data = MakeFlatCell(runtime::LoadLittleEndianInt16(bytes + cursor - 4));
                ++actualFlatSectors;
                continue;
            }
            if (sectorCellCount == 64) {
                if (!AddChecked(cursor, 128, zone->file.Size())) {
                    const std::string message = "truncated simple sector in " + zone->path.string();
                    if (error) {
                        *error = message;
                    }
                    return false;
                }
                if (actualCells + 64 > zone->ownedCells.size()) {
                    const std::string message = "simple-sector cells exceed header in " + zone->path.string();
                    if (error) {
                        *error = message;
                    }
                    return false;
                }
                sector.m_BooleanFlag = 2;
                for (int cell = 0; cell < 64; ++cell) {
                    zone->ownedCells[static_cast<std::size_t>(actualCells) + static_cast<std::size_t>(cell)].m_data =
                        runtime::LoadLittleEndianInt16(bytes + cursor - 128 + static_cast<std::size_t>(cell) * 2);
                }
                actualCells += 64;
                ++actualSimpleSectors;
                continue;
            }
            if (sectorCellCount < 0) {
                const std::string message = "negative complex-sector cell count in " + zone->path.string();
                if (error) {
                    *error = message;
                }
                return false;
            }

            sector.m_BooleanFlag = 0;
            std::uint64_t sectorCells = 0;
            for (int cell = 0; cell < 64; ++cell) {
                if (cursor > zone->file.Size() - 2) {
                    const std::string message = "truncated layer table in " + zone->path.string();
                    if (error) {
                        *error = message;
                    }
                    return false;
                }
                const std::int16_t layers = runtime::LoadLittleEndianInt16(bytes + cursor);
                cursor += 2;
                const std::size_t layerBytes = static_cast<std::size_t>(layers < 0 ? 0 : layers) * 2;
                if (layers < 0 || layerBytes > zone->file.Size() - cursor ||
                    actualCells + sectorCells + static_cast<std::uint16_t>(layers < 0 ? 0 : layers) >
                        zone->ownedCells.size()) {
                    const std::string message = "invalid layer count in " + zone->path.string();
                    if (error) {
                        *error = message;
                    }
                    return false;
                }
                for (int layer = 0; layer < layers; ++layer) {
                    zone->ownedCells[static_cast<std::size_t>(actualCells + sectorCells) + static_cast<std::size_t>(layer)]
                        .m_data = runtime::LoadLittleEndianInt16(bytes + cursor + static_cast<std::size_t>(layer) * 2);
                }
                cursor += layerBytes;
                sectorCells += static_cast<std::uint16_t>(layers);
                if (cellOffsetCursor >= zone->ownedCellOffsets.size()) {
                    const std::string message = "complex-sector offset total exceeds header in " + zone->path.string();
                    if (error) {
                        *error = message;
                    }
                    return false;
                }
                zone->ownedCellOffsets[cellOffsetCursor++] = static_cast<std::int16_t>(sectorCells);
            }
            if (sectorCells != static_cast<std::uint16_t>(sectorCellCount)) {
                const std::string message = "complex-sector cell total mismatch in " + zone->path.string();
                if (error) {
                    *error = message;
                }
                return false;
            }
            actualCells += sectorCells;
            ++actualComplexSectors;
        }

        const std::uint64_t actualNonComplex = actualFlatSectors + actualSimpleSectors;
        if (cursor != zone->file.Size() || actualCells != static_cast<std::uint64_t>(zone->declaredCells) ||
            actualNonComplex != static_cast<std::uint64_t>(zone->declaredNonComplexSectors) ||
            actualFlatSectors != static_cast<std::uint64_t>(zone->declaredFlatSectors) ||
            cellOffsetCursor != zone->ownedCellOffsets.size()) {
            std::ostringstream message;
            message << "geodata totals or file length mismatch in " << zone->path.string() << " (parsed=" << cursor
                    << ", bytes=" << zone->file.Size() << ", cells=" << actualCells
                    << ", declared_cells=" << zone->declaredCells << ')';
            if (error) {
                *error = message.str();
            }
            return false;
        }

        zone->simpleSectors = actualSimpleSectors;
        zone->complexSectors = actualComplexSectors;
        loadedIndex->owned->num_cell[zoneNumber] = zone->declaredCells;
        loadedIndex->owned->num_complex_sector[zoneNumber] = static_cast<std::int32_t>(actualComplexSectors);
        loadedIndex->owned->idx[zoneNumber] = static_cast<std::int8_t>(zone->x);
        loadedIndex->owned->idy[zoneNumber] = static_cast<std::int8_t>(zone->y);
        loadedStats.zones += 1;
        loadedStats.mappedBytes += zone->file.Size();
        loadedStats.cells += actualCells;
        loadedStats.flatSectors += actualFlatSectors;
        loadedStats.simpleSectors += actualSimpleSectors;
        loadedStats.complexSectors += actualComplexSectors;
        loadedZones[slot] = std::move(zone);
    }

    Reset(geodata);
    geodata.m_zone_index = loadedIndex->Data();
    geodata.m_hfm_index = loadedIndex.release();
    for (std::unique_ptr<PortableMappedZone>& zone : loadedZones) {
        if (!zone) {
            continue;
        }
        const int indexX = zone->x - CGeoData::kZoneXMinimum;
        const int indexY = zone->y - CGeoData::kZoneYMinimum;
        PortableMappedZone* const owner = zone.release();
        geodata.m_hfm_zones[indexX][indexY] = owner;
        geodata.m_SharedZone[indexX][indexY] = owner->SharedZoneData();
        geodata.m_hfm_cells[indexX][indexY] = owner->sharedCells.Data() ? &owner->sharedCells : nullptr;
        geodata.m_cells[indexX][indexY] = owner->CellData();
        geodata.m_hfm_cell_offset[indexX][indexY] = owner->sharedCellOffsets.Data() ? &owner->sharedCellOffsets : nullptr;
        geodata.m_cell_offset[indexX][indexY] = owner->CellOffsetData();
    }
    geodata.LinkWithSharedGeoData();

    std::ifstream badGeoData(directory / "BadGeoData.txt");
    if (badGeoData) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Loading bad geodata list")
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
                // Log.Add(
                //     LOG_ERROR,
                //     L"Invalid bad geo x(%d) y(%d)",
                //     zoneX,
                //     zoneY)
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
            geodata.m_bBadMap[zoneX][zoneY] = 1;
            // Noise: One record per excluded zone adds startup noise. Retain the file path and invalid-entry diagnostic.
            // Log.Add(
            //     LOG_NORMAL,
            //     L"bad geo [%d][%d]",
            //     zoneX,
            //     zoneY)
        }
    }

    if (stats) {
        *stats = loadedStats;
    }
    if (error) {
        error->clear();
    }
    return true;
}

bool GeoDataSource::AttachSharedMemory(
    CGeoData& geodata, const GeoDataSharedMemoryNames& names, std::string* error, GeoDataLoadStats* stats
) {
    if (names.index.empty() || names.zones.empty() || names.cells.empty() || names.cellOffsets.empty()) {
        if (error) {
            *error = "geodata shared-memory base names must not be empty";
        }
        return false;
    }

    MappedFile indexMapping;
    std::string mappingError;
    if (!indexMapping.OpenNamed(names.index, shared_geo_layout::indexBytes, mappingError, g_bUseResourceLoader)) {
        if (error) {
            *error = mappingError;
        }
        return false;
    }
    const auto* const zoneIndex = reinterpret_cast<const CGeoDataIndexStruct*>(indexMapping.Data());
    const int zoneCount = static_cast<int>(zoneIndex->zone_num);
    // Current OpenMMF compares the signed count only as its loop bound. A
    // negative count therefore means zero attached zones. Values above the
    // fixed 340-entry index would make the target walk beyond its own image
    // and cannot be represented by the portable owner array.
    if (zoneCount > 340) {
        if (error) {
            *error = "invalid CGeoDataIndexStruct::zone_num in " + names.index;
        }
        return false;
    }

    std::array<std::unique_ptr<PortableMappedZone>, 20 * 17> attachedZones{};
    GeoDataLoadStats attachedStats{};
    attachedStats.mappedBytes = indexMapping.Size();

    for (int ordinal = 0; ordinal < zoneCount; ++ordinal) {
        const int zoneX = static_cast<int>(zoneIndex->idx[ordinal]);
        const int zoneY = static_cast<int>(zoneIndex->idy[ordinal]);
        const std::int32_t numCells = zoneIndex->num_cell[ordinal];
        const std::int32_t numComplexSectors = zoneIndex->num_complex_sector[ordinal];
        if (zoneX < CGeoData::kZoneXMinimum || zoneX > CGeoData::kZoneXMaximum || zoneY < CGeoData::kZoneYMinimum ||
            zoneY > CGeoData::kZoneYMaximum) {
            if (error) {
                *error = "invalid geodata index entry #" + std::to_string(ordinal);
            }
            return false;
        }

        const std::size_t slot = static_cast<std::size_t>(zoneX - CGeoData::kZoneXMinimum) * CGeoData::kZoneYCount +
            static_cast<std::size_t>(zoneY - CGeoData::kZoneYMinimum);
        auto zone = std::make_unique<PortableMappedZone>();
        zone->x = zoneX;
        zone->y = zoneY;
        zone->declaredCells = numCells;
        zone->declaredNonComplexSectors = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(shared_geo_layout::sectorCount) - static_cast<std::uint32_t>(numComplexSectors)
        );

        const std::string suffix = "_" + std::to_string(ordinal);
        if (!zone->sharedZone.OpenNamed(
                names.zones + suffix, shared_geo_layout::zoneBytes, mappingError, g_bUseResourceLoader
            )) {
            if (error) {
                *error = mappingError;
            }
            return false;
        }
        const auto* const sharedZone = reinterpret_cast<const CSharedGeoZone*>(zone->sharedZone.Data());

        if (numCells > 0) {
            // OpenMMF maps the producer's complete object and does not
            // validate num_cell against its byte length.
            if (!zone->sharedCells.OpenNamed(names.cells + suffix, 1, mappingError, g_bUseResourceLoader)) {
                if (error) {
                    *error = mappingError;
                }
                return false;
            }
            attachedStats.mappedBytes += zone->sharedCells.Size();
        }
        if (numComplexSectors > 0) {
            if (!zone->sharedCellOffsets.OpenNamed(names.cellOffsets + suffix, 1, mappingError, g_bUseResourceLoader)) {
                if (error) {
                    *error = mappingError;
                }
                return false;
            }
            attachedStats.mappedBytes += zone->sharedCellOffsets.Size();
        }

        // Shipping OpenMMF only publishes the mapped views. Sector
        // classification is a portable diagnostic and must not turn the
        // normal constructor path into a full scan of every mapped zone.
        if (stats) {
            std::uint64_t flatSectors = 0;
            std::uint64_t simpleSectors = 0;
            std::uint64_t complexSectors = 0;
            for (std::size_t sectorIndex = 0; sectorIndex < shared_geo_layout::sectorCount; ++sectorIndex) {
                const CGeoSector& sector = sharedZone->sectors[sectorIndex / 256][sectorIndex % 256];
                const std::uint8_t flags = sector.m_BooleanFlag;

                if ((flags & 1u) != 0) {
                    ++flatSectors;
                    continue;
                }
                if ((flags & 2u) != 0) {
                    ++simpleSectors;
                    continue;
                }
                ++complexSectors;
            }

            zone->declaredFlatSectors = static_cast<std::int32_t>(flatSectors);
            zone->simpleSectors = simpleSectors;
            zone->complexSectors = complexSectors;
            attachedStats.zones += 1;
            if (numCells > 0) {
                attachedStats.cells += static_cast<std::uint64_t>(numCells);
            }
            attachedStats.flatSectors += flatSectors;
            attachedStats.simpleSectors += simpleSectors;
            attachedStats.complexSectors += complexSectors;
            attachedStats.mappedBytes += zone->sharedZone.Size();
        }
        // OpenMMF overwrites the slot when the producer repeats a zone. The
        // target leaks the superseded mapping handle. The portable owner can
        // release it because no target field can observe that old view.
        attachedZones[slot] = std::move(zone);
    }

    auto attachedIndex = std::make_unique<PortableGeoIndex>();
    attachedIndex->mapping = std::move(indexMapping);

    Reset(geodata);
    geodata.m_zone_index = attachedIndex->Data();
    geodata.m_hfm_index = attachedIndex.release();
    for (std::unique_ptr<PortableMappedZone>& zone : attachedZones) {
        if (!zone) {
            continue;
        }
        const int indexX = zone->x - CGeoData::kZoneXMinimum;
        const int indexY = zone->y - CGeoData::kZoneYMinimum;
        PortableMappedZone* const owner = zone.release();
        geodata.m_hfm_zones[indexX][indexY] = owner;
        geodata.m_SharedZone[indexX][indexY] = owner->SharedZoneData();
        geodata.m_hfm_cells[indexX][indexY] = owner->sharedCells.Data() ? &owner->sharedCells : nullptr;
        geodata.m_cells[indexX][indexY] = owner->CellData();
        geodata.m_hfm_cell_offset[indexX][indexY] = owner->sharedCellOffsets.Data() ? &owner->sharedCellOffsets : nullptr;
        geodata.m_cell_offset[indexX][indexY] = owner->CellOffsetData();
    }
    geodata.LinkWithSharedGeoData();
    if (stats) {
        *stats = attachedStats;
    }
    if (error) {
        error->clear();
    }
    return true;
}

bool GeoDataSource::HasSharedMemory(const CGeoData& geodata) noexcept {
    return geodata.m_hfm_index != nullptr && geodata.m_zone_index != nullptr;
}

int GeoDataSource::SharedMemoryZoneCount(const CGeoData& geodata) noexcept {
    return HasSharedMemory(geodata) ? static_cast<int>(geodata.m_zone_index->zone_num) : 0;
}

void GeoDataSource::Reset(CGeoData& geodata) noexcept {
    for (int x = 0; x < 20; ++x) {
        for (int y = 0; y < 17; ++y) {
            delete static_cast<PortableMappedZone*>(geodata.m_hfm_zones[x][y]);
            geodata.m_hfm_zones[x][y] = nullptr;
            geodata.m_SharedZone[x][y] = nullptr;
            geodata.m_Zone[x][y].m_data = nullptr;
            geodata.m_hfm_cells[x][y] = nullptr;
            geodata.m_cells[x][y] = nullptr;
            geodata.m_hfm_cell_offset[x][y] = nullptr;
            geodata.m_cell_offset[x][y] = nullptr;
        }
    }
    delete static_cast<PortableGeoIndex*>(geodata.m_hfm_index);
    geodata.m_hfm_index = nullptr;
    geodata.m_zone_index = nullptr;
}

} // namespace l2geo::portable
