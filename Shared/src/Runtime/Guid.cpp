#include "Shared/src/Runtime/Guid.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace runtime {
namespace {

int HexValue(char value) noexcept {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

bool ParseHex(const char* text, std::size_t count, std::uint32_t& result) noexcept {
    result = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const int digit = HexValue(text[index]);
        if (digit < 0) {
            return false;
        }
        result = (result << 4U) | static_cast<std::uint32_t>(digit);
    }
    return true;
}

} // namespace

bool ParseGuid(const char* text, _GUID& result) noexcept {
    if (!text || std::strlen(text) != 36 || text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-') {
        return false;
    }

    std::uint32_t data1 = 0;
    std::uint32_t data2 = 0;
    std::uint32_t data3 = 0;
    if (!ParseHex(text, 8, data1) || !ParseHex(text + 9, 4, data2) || !ParseHex(text + 14, 4, data3)) {
        return false;
    }

    std::array<std::uint8_t, 8> data4{};
    constexpr std::array<std::size_t, 8> byteOffsets{19, 21, 24, 26, 28, 30, 32, 34};
    for (std::size_t index = 0; index < data4.size(); ++index) {
        std::uint32_t byte = 0;
        if (!ParseHex(text + byteOffsets[index], 2, byte)) {
            return false;
        }
        data4[index] = static_cast<std::uint8_t>(byte);
    }

    result.Data1 = data1;
    result.Data2 = static_cast<std::uint16_t>(data2);
    result.Data3 = static_cast<std::uint16_t>(data3);
    std::copy(data4.begin(), data4.end(), result.Data4);
    return true;
}

bool IsNullGuid(const _GUID& value) noexcept {
    constexpr _GUID nullGuid{};
    return std::memcmp(&value, &nullGuid, sizeof(value)) == 0;
}

} // namespace runtime
