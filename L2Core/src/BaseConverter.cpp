#include "L2Core/src/inc/BaseConverter.h"

#include <cstddef>

CBaseConverter::CBaseConverter() {
    static constexpr char16_t alphabet[] = u"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (int index = 0; index < 36; ++index) {
        m_ReverseTable[alphabet[index]] = index;
    }
}

int CBaseConverter::ReversedNumber(char16_t value) const {
    if (u'a' <= value && value <= u'z') {
        value = static_cast<char16_t>(value - u'a' + u'A');
    }
    const auto found = m_ReverseTable.find(value);
    return found == m_ReverseTable.end() ? -1 : found->second;
}

void CBaseConverter::ConvertToInt(const std::uint32_t base, const std::u16string& value, std::uint32_t* const result) const {
    *result = 0;
    std::uint32_t factor = 1;
    for (std::size_t offset = value.size(); offset != 0; --offset) {
        const int digit = ReversedNumber(value[offset - 1]);
        if (digit == -1) {
            return;
        }
        *result += static_cast<std::uint32_t>(digit) * factor;
        factor *= base;
    }
}

namespace Base64 {

bool Encode(char* source, int length, char* destination, int* outputLength) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                       "abcdefghijklmnopqrstuvwxyz"
                                       "0123456789+/";

    int output = 0;
    for (int index = 0; index < length; index += 3) {
        const auto first = static_cast<std::uint8_t>(source[index]);
        const auto second = index + 1 < length ? static_cast<std::uint8_t>(source[index + 1]) : std::uint8_t{};
        const auto third = index + 2 < length ? static_cast<std::uint8_t>(source[index + 2]) : std::uint8_t{};

        destination[output++] = alphabet[first >> 2U];
        destination[output++] = alphabet[((first & 0x03U) << 4U) | (second >> 4U)];
        destination[output++] = index + 1 < length ? alphabet[((second & 0x0fU) << 2U) | (third >> 6U)] : '=';
        destination[output++] = index + 2 < length ? alphabet[third & 0x3fU] : '=';
    }

    destination[output] = '\0';
    *outputLength = output;
    return true;
}

} // namespace Base64
