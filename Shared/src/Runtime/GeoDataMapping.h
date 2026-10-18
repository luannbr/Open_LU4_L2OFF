#pragma once

#include "Shared/src/GeoData.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace l2geo::portable::detail {

class MappedFile {
public:
    MappedFile() = default;
    ~MappedFile();

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;

    bool Open(const std::filesystem::path& path, std::string& error);
    // CGeoData::OpenMMF requests FILE_MAP_READ normally and
    // FILE_MAP_ALL_ACCESS in resource-loader mode.
    bool OpenNamed(const std::string& name, std::size_t bytes, std::string& error, bool requestAllAccess = false);
    bool CreateNamed(const std::string& name, std::size_t bytes, std::string& error);
    void Reset();

    const std::uint8_t* Data() const;
    std::uint8_t* MutableData();
    std::size_t Size() const;

private:
    void Swap(MappedFile& other) noexcept;

    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::string ownedSharedName_;
    // Windows only. A named mapping stays openable by name only while a handle
    // to it exists, so the creator keeps its handle until Reset. Unused on POSIX,
    // where ownedSharedName_ keeps the name.
    void* ownedMappingHandle_ = nullptr;
};

struct PortableMappedZone {
    std::filesystem::path path;
    MappedFile file;
    MappedFile sharedZone;
    MappedFile sharedCells;
    MappedFile sharedCellOffsets;
    std::unique_ptr<CSharedGeoZone> ownedZone;
    std::vector<CGeoCell> ownedCells;
    std::vector<std::int16_t> ownedCellOffsets;
    int x = 0;
    int y = 0;
    std::int32_t declaredCells = 0;
    std::int32_t declaredNonComplexSectors = 0;
    std::int32_t declaredFlatSectors = 0;
    std::uint64_t simpleSectors = 0;
    std::uint64_t complexSectors = 0;

    CSharedGeoZone* SharedZoneData();
    CGeoCell* CellData();
    std::int16_t* CellOffsetData();
};

struct PortableGeoIndex {
    MappedFile mapping;
    std::unique_ptr<CGeoDataIndexStruct> owned;

    CGeoDataIndexStruct* Data();
};

} // namespace l2geo::portable::detail
