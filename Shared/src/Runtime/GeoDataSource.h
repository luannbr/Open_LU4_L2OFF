#pragma once

#include "Shared/src/GeoData.h"

#include <vector>

namespace l2geo::portable {

// Cross-platform deployment adapter. The shipping CGeoData API remains in
// Shared/GeoData.h. Configurable filesystem and mapping names belong to the
// portable process owner instead of to the recovered target class surface.
class GeoDataSource {
public:
    static bool LoadDirectory(
        CGeoData& geodata,
        const std::filesystem::path& directory,
        std::string* error = nullptr,
        GeoDataLoadStats* stats = nullptr
    );

    static bool AttachSharedMemory(
        CGeoData& geodata,
        const GeoDataSharedMemoryNames& names,
        std::string* error = nullptr,
        GeoDataLoadStats* stats = nullptr
    );

    static bool HasSharedMemory(const CGeoData& geodata) noexcept;

    static int SharedMemoryZoneCount(const CGeoData& geodata) noexcept;

private:
    friend class ::CGeoData;

    static bool LoadFiles(
        CGeoData& geodata,
        const std::filesystem::path& directory,
        const std::vector<std::filesystem::path>& files,
        std::string* error,
        GeoDataLoadStats* stats
    );
    static void Reset(CGeoData& geodata) noexcept;
};

} // namespace l2geo::portable
