#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace nasc {

// The host may be Windows, Linux or macOS, but generated data must retain the
// legacy win64 target ABI expected by the Lineage II server.
struct TargetProfile {
    std::uint32_t pointerSize;
    std::uint32_t sharedFactoryVersion;
    std::uint32_t npchVersion;
    std::uint32_t nascVersion;
    std::uint32_t npcEventHVersion;
    std::string_view platform;
};

inline constexpr TargetProfile legacyWin64Target{
    .pointerSize = 8,
    .sharedFactoryVersion = 289,
    .npchVersion = 317,
    .nascVersion = 73,
    .npcEventHVersion = 10,
    .platform = "win64",
};

// The ai.obj header text without the UTF-16LE BOM.
[[nodiscard]] std::u16string LegacyObjectHeader();
[[nodiscard]] std::u16string LegacyVersionFile();

} // namespace nasc
