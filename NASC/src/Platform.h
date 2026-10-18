#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace nasc {

// Paths are UTF-8 at the application boundary on every supported platform.
[[nodiscard]] std::filesystem::path PathFromUtf8(std::string_view value);
[[nodiscard]] std::string PathToUtf8(const std::filesystem::path& value);

[[nodiscard]] std::uint64_t MonotonicMilliseconds() noexcept;
[[nodiscard]] bool RegularFileExists(const std::filesystem::path& path);

// NASC files are UTF-16 little-endian, regardless of the host wchar_t width
// or byte order.
[[nodiscard]] std::u16string ReadUtf16leFile(const std::filesystem::path& path);
void WriteUtf16leFile(const std::filesystem::path& path, std::u16string_view text, bool includeBom = true);

} // namespace nasc
