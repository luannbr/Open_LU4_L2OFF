#include "Platform.h"

#include "Encoding.h"

#include <chrono>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace nasc {

std::filesystem::path PathFromUtf8(std::string_view value) {
#ifdef _WIN32
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    const std::u16string utf16 = Utf8ToUtf16(value);
    std::wstring native;
    native.reserve(utf16.size());
    for (char16_t character : utf16) {
        native.push_back(static_cast<wchar_t>(character));
    }
    return std::filesystem::path(native);
#else
    return std::filesystem::path(std::string(value));
#endif
}

std::string PathToUtf8(const std::filesystem::path& value) {
#ifdef _WIN32
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    const std::wstring& native = value.native();
    std::u16string utf16;
    utf16.reserve(native.size());
    for (wchar_t character : native) {
        utf16.push_back(static_cast<char16_t>(character));
    }
    return Utf16ToUtf8(utf16);
#else
    return value.native();
#endif
}

std::uint64_t MonotonicMilliseconds() noexcept {
    const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

bool RegularFileExists(const std::filesystem::path& path) {
    std::error_code error;
    const bool result = std::filesystem::is_regular_file(path, error);
    return result && !error;
}

std::u16string ReadUtf16leFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open " + PathToUtf8(path));
    }
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (bytes.size() % 2 != 0) {
        throw std::runtime_error("odd-sized UTF-16 file: " + PathToUtf8(path));
    }

    std::size_t offset = 0;
    if (bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe) {
        offset = 2;
    } else if (bytes.size() >= 2 && bytes[0] == 0xfe && bytes[1] == 0xff) {
        throw std::runtime_error("big-endian UTF-16 is unsupported: " + PathToUtf8(path));
    }

    std::u16string result;
    result.reserve((bytes.size() - offset) / 2);
    for (; offset < bytes.size(); offset += 2) {
        const auto unit = static_cast<std::uint16_t>(bytes[offset] | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8));
        result.push_back(static_cast<char16_t>(unit));
    }
    return result;
}

void WriteUtf16leFile(const std::filesystem::path& path, std::u16string_view text, bool includeBom) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot create " + PathToUtf8(path));
    }
    if (includeBom) {
        output.put(static_cast<char>(0xff));
        output.put(static_cast<char>(0xfe));
    }
    for (char16_t character : text) {
        const auto unit = static_cast<std::uint16_t>(character);
        output.put(static_cast<char>(unit & 0xff));
        output.put(static_cast<char>(unit >> 8));
    }
    if (!output) {
        throw std::runtime_error("failed to write " + PathToUtf8(path));
    }
}

} // namespace nasc
