#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace nasc {

struct VersionInfo {
    std::uint32_t pointerSize = 0;
    std::uint32_t sharedFactoryVersion = 0;
    std::uint32_t npchVersion = 0;
    std::uint32_t nascVersion = 0;
    std::uint32_t npcEventHVersion = 0;
    std::string platform = "win64";
};

// Reads only the header, so a large ai.obj is not loaded.
[[nodiscard]] VersionInfo InspectObjectHeader(const std::filesystem::path& path);

[[nodiscard]] bool IsLegacyCompatible(const VersionInfo& info) noexcept;
[[nodiscard]] std::string FormatVersionInfo(const VersionInfo& info);

} // namespace nasc
