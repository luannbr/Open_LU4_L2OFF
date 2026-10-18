#include "Version.h"

#include "Encoding.h"
#include "Platform.h"
#include "Target.h"

#include <array>
#include <charconv>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace nasc {
namespace {

std::vector<std::string> ReadPrefixLines(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open " + PathToUtf8(path));
    }

    std::array<unsigned char, 2> bytes{};
    if (!input.read(reinterpret_cast<char*>(bytes.data()), 2)) {
        throw std::runtime_error("truncated object header: " + PathToUtf8(path));
    }
    if (bytes[0] != 0xff || bytes[1] != 0xfe) {
        throw std::runtime_error("expected UTF-16LE BOM: " + PathToUtf8(path));
    }

    std::vector<std::string> lines;
    std::u16string line;
    while (lines.size() < 8 && input.read(reinterpret_cast<char*>(bytes.data()), 2)) {
        const auto unit = static_cast<char16_t>(bytes[0] | (static_cast<std::uint16_t>(bytes[1]) << 8));
        if (unit == u'\n') {
            if (!line.empty() && line.back() == u'\r') {
                line.pop_back();
            }
            lines.push_back(Utf16ToUtf8(line));
            line.clear();
            if (lines.size() >= 6 && lines.back().empty()) {
                break;
            }
        } else {
            line.push_back(unit);
        }
    }
    if (!line.empty() && lines.size() < 8) {
        lines.push_back(Utf16ToUtf8(line));
    }
    return lines;
}

std::optional<std::uint32_t> ParseValue(const std::vector<std::string>& lines, std::string_view key) {
    for (const std::string& line : lines) {
        if (!line.starts_with(key) || line.size() <= key.size() || line[key.size()] != ' ') {
            continue;
        }
        const std::string_view value(line.data() + key.size() + 1, line.size() - key.size() - 1);
        std::uint32_t parsed = 0;
        const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
        if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
            throw std::runtime_error("invalid numeric value for " + std::string(key));
        }
        return parsed;
    }
    return std::nullopt;
}

std::uint32_t RequiredValue(const std::vector<std::string>& lines, std::string_view key) {
    const std::optional<std::uint32_t> value = ParseValue(lines, key);
    if (!value) {
        throw std::runtime_error("missing " + std::string(key));
    }
    return *value;
}

} // namespace

VersionInfo InspectObjectHeader(const std::filesystem::path& path) {
    const std::vector<std::string> lines = ReadPrefixLines(path);
    return VersionInfo{
        .pointerSize = RequiredValue(lines, "SizeofPointer"),
        .sharedFactoryVersion = RequiredValue(lines, "SharedFactoryVersion"),
        .npchVersion = RequiredValue(lines, "NPCHVersion"),
        .nascVersion = RequiredValue(lines, "NASCVersion"),
        .npcEventHVersion = RequiredValue(lines, "NPCEventHVersion"),
        .platform = "win64",
    };
}

bool IsLegacyCompatible(const VersionInfo& info) noexcept {
    return info.pointerSize == legacyWin64Target.pointerSize &&
        info.sharedFactoryVersion == legacyWin64Target.sharedFactoryVersion &&
        info.npchVersion == legacyWin64Target.npchVersion && info.nascVersion == legacyWin64Target.nascVersion &&
        info.npcEventHVersion == legacyWin64Target.npcEventHVersion && info.platform == legacyWin64Target.platform;
}

std::string FormatVersionInfo(const VersionInfo& info) {
    std::ostringstream output;
    output << "SizeofPointer " << info.pointerSize << '\n'
           << "SharedFactoryVersion " << info.sharedFactoryVersion << '\n'
           << "NPCHVersion " << info.npchVersion << '\n'
           << "NASCVersion " << info.nascVersion << '\n'
           << "NPCEventHVersion " << info.npcEventHVersion << '\n'
           << "Platform " << info.platform << '\n';
    return output.str();
}

} // namespace nasc
