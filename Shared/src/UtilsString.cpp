#include "Shared/src/UtilsString.h"

#include <cstddef>
#include <string>

namespace {

template <typename TCodeUnit>
void CopyTargetLowercase(const TCodeUnit* input, TCodeUnit* output) {
    const std::size_t length = std::char_traits<TCodeUnit>::length(input);
    for (std::size_t index = 0; index < length; ++index) {
        TCodeUnit value = input[index];
        if ((value >= static_cast<TCodeUnit>(0x41) && value <= static_cast<TCodeUnit>(0x5a)) ||
            (value >= static_cast<TCodeUnit>(0x410) && value <= static_cast<TCodeUnit>(0x42f))) {
            value = static_cast<TCodeUnit>(value + 0x20);
        }
        output[index] = value;
    }
    output[length] = static_cast<TCodeUnit>(0);
}

} // namespace

void Utils::CopyStrInLowerCase(const char16_t* input, char16_t* output) { CopyTargetLowercase(input, output); }

void Utils::CopyStrInLowerCase(const wchar_t* input, wchar_t* output) { CopyTargetLowercase(input, output); }

std::u16string Utils::UnescapeSqlApostrophes(const char16_t* input) {
    std::u16string result = input ? input : u"";
    std::size_t offset = 0;
    while ((offset = result.find(u"''", offset)) != std::u16string::npos) {
        result.replace(offset, 2, u"'");
        ++offset;
    }
    return result;
}

std::wstring Utils::EscapeSqlApostrophes(const wchar_t* input) {
    std::wstring result = input ? input : L"";
    std::size_t offset = 0;
    while ((offset = result.find(L'\'', offset)) != std::wstring::npos) {
        result.insert(offset + 1, 1, L'\'');
        offset += 2;
    }
    return result;
}
