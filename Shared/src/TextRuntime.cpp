#include "TextRuntime.h"

#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <limits>
#include <string_view>

#if !defined(_WIN32)
#include <locale.h>
#endif

namespace {

#if !defined(_WIN32)
locale_t WindowsWidePrintfLocale() noexcept {
    static locale_t locale = []() noexcept {
        constexpr const char* candidates[] = {
            "C.UTF-8",
            "C.utf8",
            "UTF-8",
            "en_US.UTF-8",
        };
        for (const char* candidate : candidates) {
            if (locale_t value = newlocale(LC_CTYPE_MASK, candidate, nullptr)) {
                return value;
            }
        }
        return static_cast<locale_t>(nullptr);
    }();
    return locale;
}
#endif

template <class Character>
bool IsDecimalSpace(Character value) noexcept {
    if constexpr (sizeof(Character) == sizeof(char)) {
        return std::isspace(static_cast<unsigned char>(value)) != 0;
    } else {
        return std::iswspace(static_cast<std::wint_t>(value)) != 0;
    }
}

template <class Result, class Character>
Result WindowsDecimalImpl(std::basic_string_view<Character> value) noexcept {
    std::size_t offset = 0;
    while (offset < value.size() && IsDecimalSpace(value[offset])) {
        ++offset;
    }

    bool negative = false;
    if (offset < value.size() &&
        (value[offset] == static_cast<Character>('+') || value[offset] == static_cast<Character>('-'))) {
        negative = value[offset] == static_cast<Character>('-');
        ++offset;
    }

    if (offset >= value.size() || value[offset] < static_cast<Character>('0') ||
        value[offset] > static_cast<Character>('9')) {
        return 0;
    }

    constexpr std::uint64_t positiveLimit = static_cast<std::uint64_t>(std::numeric_limits<Result>::max());
    constexpr std::uint64_t negativeLimit = positiveLimit + 1U;
    const std::uint64_t limit = negative ? negativeLimit : positiveLimit;
    std::uint64_t result = 0;
    while (offset < value.size() && value[offset] >= static_cast<Character>('0') &&
        value[offset] <= static_cast<Character>('9')) {
        const unsigned digit = static_cast<unsigned>(value[offset] - static_cast<Character>('0'));
        if (result > (limit - digit) / 10U) {
            result = limit;
        } else {
            result = result * 10U + digit;
        }
        ++offset;
    }

    if (!negative) {
        return static_cast<Result>(result);
    }
    if (result == negativeLimit) {
        return std::numeric_limits<Result>::min();
    }
    return -static_cast<Result>(result);
}

template <class Character>
std::int32_t WindowsWtolImpl(std::basic_string_view<Character> value) noexcept {
    return WindowsDecimalImpl<std::int32_t>(value);
}

std::u16string Utf8ToUtf16Impl(std::string_view value) {
    std::u16string output;
    output.reserve(value.size());
    std::size_t offset = 0;
    while (offset < value.size()) {
        const auto lead = static_cast<std::uint8_t>(value[offset++]);
        std::uint32_t codepoint = 0xfffd;
        if (lead < 0x80) {
            codepoint = lead;
        } else if ((lead & 0xe0) == 0xc0 && offset < value.size()) {
            const auto second = static_cast<std::uint8_t>(value[offset]);
            if ((second & 0xc0) == 0x80) {
                ++offset;
                codepoint = ((lead & 0x1f) << 6) | (second & 0x3f);
                if (codepoint < 0x80) {
                    codepoint = 0xfffd;
                }
            }
        } else if ((lead & 0xf0) == 0xe0 && offset + 1 < value.size()) {
            const auto second = static_cast<std::uint8_t>(value[offset]);
            const auto third = static_cast<std::uint8_t>(value[offset + 1]);
            if ((second & 0xc0) == 0x80 && (third & 0xc0) == 0x80) {
                offset += 2;
                codepoint = ((lead & 0x0f) << 12) | ((second & 0x3f) << 6) | (third & 0x3f);
                if (codepoint < 0x800 || (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
                    codepoint = 0xfffd;
                }
            }
        } else if ((lead & 0xf8) == 0xf0 && offset + 2 < value.size()) {
            const auto second = static_cast<std::uint8_t>(value[offset]);
            const auto third = static_cast<std::uint8_t>(value[offset + 1]);
            const auto fourth = static_cast<std::uint8_t>(value[offset + 2]);
            if ((second & 0xc0) == 0x80 && (third & 0xc0) == 0x80 && (fourth & 0xc0) == 0x80) {
                offset += 3;
                codepoint = ((lead & 0x07) << 18) | ((second & 0x3f) << 12) | ((third & 0x3f) << 6) | (fourth & 0x3f);
                if (codepoint < 0x10000 || codepoint > 0x10ffff) {
                    codepoint = 0xfffd;
                }
            }
        }

        if (codepoint <= 0xffff) {
            output.push_back(static_cast<char16_t>(codepoint));
        } else {
            codepoint -= 0x10000;
            output.push_back(static_cast<char16_t>(0xd800 + (codepoint >> 10)));
            output.push_back(static_cast<char16_t>(0xdc00 + (codepoint & 0x3ff)));
        }
    }
    return output;
}

void AppendUtf8(std::string& output, std::uint32_t codepoint) {
    if (codepoint <= 0x7f) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
        output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
}

std::string Utf16ToUtf8Impl(std::u16string_view value) {
    std::string output;
    output.reserve(value.size());
    for (std::size_t offset = 0; offset < value.size(); ++offset) {
        std::uint32_t codepoint = static_cast<std::uint16_t>(value[offset]);
        if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
            if (offset + 1 < value.size()) {
                const std::uint32_t low = static_cast<std::uint16_t>(value[offset + 1]);
                if (low >= 0xdc00 && low <= 0xdfff) {
                    ++offset;
                    codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                } else {
                    codepoint = 0xfffd;
                }
            } else {
                codepoint = 0xfffd;
            }
        } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
            // A wchar_t slice may bisect a surrogate pair. UTF-8 cannot
            // represent an isolated surrogate, so use the standard
            // replacement character at this portability boundary.
            codepoint = 0xfffd;
        }
        AppendUtf8(output, codepoint);
    }
    return output;
}

void AppendUtf16CodePoint(std::u16string& output, std::uint32_t codepoint) {
    if (codepoint <= 0xffff) {
        output.push_back(static_cast<char16_t>(codepoint));
        return;
    }

    codepoint -= 0x10000;
    output.push_back(static_cast<char16_t>(0xd800 + (codepoint >> 10)));
    output.push_back(static_cast<char16_t>(0xdc00 + (codepoint & 0x3ff)));
}

void AppendWideCodePoint(std::wstring& output, std::uint32_t codepoint) {
    if constexpr (sizeof(wchar_t) == 2) {
        if (codepoint <= 0xffff) {
            output.push_back(static_cast<wchar_t>(codepoint));
            return;
        }

        codepoint -= 0x10000;
        output.push_back(static_cast<wchar_t>(0xd800 + (codepoint >> 10)));
        output.push_back(static_cast<wchar_t>(0xdc00 + (codepoint & 0x3ff)));
        return;
    }

    output.push_back(static_cast<wchar_t>(codepoint));
}

bool DecodeStrictUtf8CodePoint(std::string_view value, std::size_t& offset, std::uint32_t& codepoint) {
    const auto TakeContinuation = [&value, &offset](std::uint8_t& output) {
        if (offset >= value.size()) {
            return false;
        }
        const auto byte = static_cast<std::uint8_t>(value[offset]);
        if ((byte & 0xc0U) != 0x80U) {
            return false;
        }
        ++offset;
        output = byte;
        return true;
    };

    const auto lead = static_cast<std::uint8_t>(value[offset++]);
    if (lead < 0x80U) {
        codepoint = lead;
        return true;
    }

    std::uint8_t second = 0;
    if (lead >= 0xc2U && lead <= 0xdfU) {
        if (!TakeContinuation(second)) {
            return false;
        }
        codepoint = ((lead & 0x1fU) << 6U) | (second & 0x3fU);
        return true;
    }

    std::uint8_t third = 0;
    if (lead >= 0xe0U && lead <= 0xefU) {
        if (!TakeContinuation(second) || !TakeContinuation(third)) {
            return false;
        }
        if ((lead == 0xe0U && second < 0xa0U) || (lead == 0xedU && second >= 0xa0U)) {
            return false;
        }
        codepoint = ((lead & 0x0fU) << 12U) | ((second & 0x3fU) << 6U) | (third & 0x3fU);
        return true;
    }

    std::uint8_t fourth = 0;
    if (lead >= 0xf0U && lead <= 0xf4U) {
        if (!TakeContinuation(second) || !TakeContinuation(third) || !TakeContinuation(fourth)) {
            return false;
        }
        if ((lead == 0xf0U && second < 0x90U) || (lead == 0xf4U && second >= 0x90U)) {
            return false;
        }
        codepoint = ((lead & 0x07U) << 18U) | ((second & 0x3fU) << 12U) | ((third & 0x3fU) << 6U) | (fourth & 0x3fU);
        return true;
    }

    return false;
}

} // namespace

std::u16string Utf8ToUtf16(std::string_view value) { return Utf8ToUtf16Impl(value); }

std::string Utf16ToUtf8(std::u16string_view value) { return Utf16ToUtf8Impl(value); }

std::string Utf16ToUtf8(const char16_t* value) {
    return value != nullptr ? Utf16ToUtf8Impl(std::u16string_view(value)) : std::string{};
}

std::string Utf16ToUtf8(const std::u16string& value) { return Utf16ToUtf8Impl(value); }
#if defined(_WIN32)
std::string Utf16ToUtf8(const wchar_t* value) {
    return value != nullptr ? WideToUtf8(std::wstring_view(value)) : std::string{};
}
#endif

std::string Utf16ToUtf8(std::u16string_view value, std::size_t maximumCodeUnits) {
    std::size_t count = value.size();
    if (count > maximumCodeUnits) {
        count = maximumCodeUnits;
        if (count != 0 && value[count - 1] >= 0xd800 && value[count - 1] <= 0xdbff && value[count] >= 0xdc00 &&
            value[count] <= 0xdfff) {
            --count;
        }
    }
    return Utf16ToUtf8Impl(value.substr(0, count));
}

std::wstring Utf16ToWide(std::u16string_view value, Utf16WidePolicy policy) {
    const bool preserveInvalid = policy == Utf16WidePolicy::PreserveInvalid ||
        (policy == Utf16WidePolicy::PreserveInvalidOnWindows && sizeof(wchar_t) == 2);
    std::wstring output;
    output.reserve(value.size());
    for (std::size_t offset = 0; offset < value.size(); ++offset) {
        std::uint32_t codepoint = static_cast<std::uint16_t>(value[offset]);
        if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
            if (offset + 1 < value.size()) {
                const std::uint32_t low = static_cast<std::uint16_t>(value[offset + 1]);
                if (low >= 0xdc00 && low <= 0xdfff) {
                    ++offset;
                    codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                } else {
                    if (!preserveInvalid) {
                        codepoint = 0xfffd;
                    }
                }
            } else {
                if (!preserveInvalid) {
                    codepoint = 0xfffd;
                }
            }
        } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
            if (!preserveInvalid) {
                codepoint = 0xfffd;
            }
        }
        AppendWideCodePoint(output, codepoint);
    }
    return output;
}

std::wstring Utf16ToWide(std::u16string_view value) { return Utf16ToWide(value, Utf16WidePolicy::ReplaceInvalid); }

std::wstring Utf16ToWide(const char16_t* value, Utf16WidePolicy policy) {
    return value != nullptr ? Utf16ToWide(std::u16string_view(value), policy) : std::wstring{};
}

#if defined(_WIN32)
std::wstring Utf16ToWide(const wchar_t* value, Utf16WidePolicy policy) {
    if (value == nullptr) {
        return {};
    }
    // Copy units instead of aliasing wchar_t storage as char16_t.
    const std::u16string units(value, value + std::char_traits<wchar_t>::length(value));
    return Utf16ToWide(std::u16string_view(units), policy);
}
#endif

wchar_t* CopyUtf16ToWide(
    const char16_t* value, std::size_t inputCapacity, wchar_t* output, std::size_t outputCapacity
) noexcept {
    for (std::size_t i = 0; i < outputCapacity; ++i) {
        output[i] = L'\0';
    }
    if (value == nullptr || outputCapacity == 0) {
        return output;
    }
    std::size_t destination = 0;
    for (std::size_t i = 0; destination + 1 < outputCapacity && i < inputCapacity && value[i] != u'\0'; ++i) {
        std::uint32_t codepoint = value[i];
        if constexpr (sizeof(wchar_t) > 2) {
            if (codepoint >= 0xd800 && codepoint <= 0xdbff && i + 1 < inputCapacity && value[i + 1] >= 0xdc00 &&
                value[i + 1] <= 0xdfff) {
                codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (value[++i] - 0xdc00);
            }
        }
        output[destination++] = static_cast<wchar_t>(codepoint);
    }
    return output;
}

std::u16string SanitizeUtf16(std::u16string_view value) {
    std::u16string output;
    output.reserve(value.size());
    for (std::size_t i = 0; i < value.size() && value[i] != u'\0'; ++i) {
        const char16_t unit = value[i];
        if (unit >= 0xd800 && unit <= 0xdbff) {
            if (i + 1 < value.size() && value[i + 1] >= 0xdc00 && value[i + 1] <= 0xdfff) {
                output.push_back(unit);
                output.push_back(value[++i]);
            } else {
                output.push_back(u'\ufffd');
            }
        } else {
            output.push_back(unit >= 0xdc00 && unit <= 0xdfff ? u'\ufffd' : unit);
        }
    }
    return output;
}
std::u16string SanitizeUtf16(const char16_t* value) {
    return value != nullptr ? SanitizeUtf16(std::u16string_view(value)) : std::u16string{};
}
std::u16string SanitizeUtf16(const std::u16string& value) { return SanitizeUtf16(std::u16string_view(value)); }
#if defined(_WIN32)
std::u16string SanitizeUtf16(const wchar_t* value) {
    if (value == nullptr) {
        return {};
    }
    const std::u16string units(value, value + std::char_traits<wchar_t>::length(value));
    return SanitizeUtf16(units);
}
#endif

std::wstring Utf16ToWide(const char16_t* value) {
    return value != nullptr ? Utf16ToWide(std::u16string_view(value)) : std::wstring{};
}

std::wstring Utf16ToWide(const std::u16string& value) { return Utf16ToWide(runtime::StringView(value)); }

std::u16string WideToUtf16(std::wstring_view value) {
    std::u16string output;
    output.reserve(value.size());
    if constexpr (sizeof(wchar_t) == 2) {
        for (std::size_t offset = 0; offset < value.size(); ++offset) {
            std::uint32_t codepoint = static_cast<std::uint16_t>(value[offset]);
            if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                if (offset + 1 < value.size()) {
                    const std::uint32_t low = static_cast<std::uint16_t>(value[offset + 1]);
                    if (low >= 0xdc00 && low <= 0xdfff) {
                        ++offset;
                        codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                    } else {
                        codepoint = 0xfffd;
                    }
                } else {
                    codepoint = 0xfffd;
                }
            } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                codepoint = 0xfffd;
            }
            AppendUtf16CodePoint(output, codepoint);
        }
        return output;
    }

    for (const wchar_t unit : value) {
        std::uint32_t codepoint = static_cast<std::uint32_t>(unit);
        if (codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
            codepoint = 0xfffd;
        }
        AppendUtf16CodePoint(output, codepoint);
    }
    return output;
}

std::u16string WideToUtf16(const wchar_t* value) {
    return value != nullptr ? WideToUtf16(std::wstring_view(value)) : std::u16string{};
}

namespace target_utf16 {

std::u16string FromRecoveredWindowsWide(const std::wstring_view value) {
    if constexpr (sizeof(wchar_t) == sizeof(char16_t)) {
        std::u16string result;
        result.reserve(value.size());
        for (const wchar_t unit : value) {
            result.push_back(static_cast<char16_t>(static_cast<std::uint16_t>(unit)));
        }
        return result;
    }
    return ::WideToUtf16(value);
}

std::u16string FromRecoveredWindowsWide(const wchar_t* const value) {
    return value != nullptr ? FromRecoveredWindowsWide(std::wstring_view(value)) : std::u16string{};
}

std::u16string FromHostCodeUnits(const std::wstring_view value) {
    std::u16string result;
    result.reserve(value.size());
    for (const wchar_t unit : value) {
        result.push_back(static_cast<char16_t>(static_cast<std::uint16_t>(unit)));
    }
    return result;
}

std::u16string FromHostCodeUnits(const wchar_t* const value) {
    return value != nullptr ? FromHostCodeUnits(std::wstring_view(value)) : std::u16string{};
}

std::wstring ToHostCodeUnits(const std::u16string_view value) {
    std::wstring result;
    result.reserve(value.size());
    for (const char16_t unit : value) {
        result.push_back(static_cast<wchar_t>(static_cast<std::uint16_t>(unit)));
    }
    return result;
}

std::wstring ToHostCodeUnits(const char16_t* const value) {
    return value != nullptr ? ToHostCodeUnits(std::u16string_view(value)) : std::wstring{};
}

std::wstring ToHostCodeUnitsBounded(const char16_t* const value, const std::size_t capacity) {
    if (value == nullptr) {
        return {};
    }
    return ToHostCodeUnits(std::u16string_view(value, BoundedLength(value, capacity)));
}

std::wstring ToHostCodeUnits(const SharedUtilsWideChar* const value, const std::size_t length) {
    std::wstring result(length, L'\0');
    for (std::size_t index = 0; index < length; ++index) {
        result[index] = static_cast<wchar_t>(static_cast<std::uint16_t>(value[index]));
    }
    return result;
}

std::size_t Length(const char16_t* const value) noexcept {
    std::size_t length = 0;
    while (value[length] != u'\0') {
        ++length;
    }
    return length;
}

int CompareN(const char16_t* left, const char16_t* right, std::size_t count) noexcept {
    for (; count > 0; --count, ++left, ++right) {
        if (*left != *right) {
            return *left < *right ? -1 : 1;
        }
        if (*left == u'\0') {
            return 0;
        }
    }
    return 0;
}

const char16_t* Find(const char16_t* value, const char16_t unit) noexcept {
    for (;; ++value) {
        if (*value == unit) {
            return value;
        }
        if (*value == u'\0') {
            return nullptr;
        }
    }
}

char16_t* Find(char16_t* value, const char16_t unit) noexcept {
    return const_cast<char16_t*>(Find(static_cast<const char16_t*>(value), unit));
}

std::size_t BoundedLength(const char16_t* const value, const std::size_t capacity) noexcept {
    std::size_t length = 0;
    while (length < capacity && value[length] != u'\0') {
        ++length;
    }
    return length;
}

char16_t LoadLittleEndian(const void* const source) noexcept {
    const auto* const bytes = static_cast<const unsigned char*>(source);
    return static_cast<char16_t>(static_cast<std::uint16_t>(bytes[0]) | (static_cast<std::uint16_t>(bytes[1]) << 8U));
}

void StoreLittleEndian(void* const destination, const char16_t value) noexcept {
    auto* const bytes = static_cast<unsigned char*>(destination);
    const std::uint16_t unit = static_cast<std::uint16_t>(value);
    bytes[0] = static_cast<unsigned char>(unit & 0xffU);
    bytes[1] = static_cast<unsigned char>(unit >> 8U);
}

} // namespace target_utf16

std::wstring Utf8ToWide(std::string_view value) {
    const std::u16string utf16 = Utf8ToUtf16Impl(value);
    std::wstring output;
    output.reserve(utf16.size());
    for (std::size_t offset = 0; offset < utf16.size(); ++offset) {
        std::uint32_t codepoint = static_cast<std::uint16_t>(utf16[offset]);
        if constexpr (sizeof(wchar_t) == 2) {
            output.push_back(static_cast<wchar_t>(codepoint));
            continue;
        }
        if (codepoint >= 0xd800 && codepoint <= 0xdbff && offset + 1 < utf16.size()) {
            const std::uint32_t low = static_cast<std::uint16_t>(utf16[offset + 1]);
            if (low >= 0xdc00 && low <= 0xdfff) {
                ++offset;
                codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
            }
        }
        output.push_back(static_cast<wchar_t>(codepoint));
    }
    return output;
}

std::wstring Utf8ToWideStrict(std::string_view value) {
    std::wstring output;
    output.reserve(value.size());
    std::size_t offset = 0;
    while (offset < value.size()) {
        std::uint32_t codepoint = 0;
        if (!DecodeStrictUtf8CodePoint(value, offset, codepoint)) {
            return {};
        }
        AppendWideCodePoint(output, codepoint);
    }
    return output;
}

std::string WideToUtf8(std::wstring_view value) {
    if constexpr (sizeof(wchar_t) == 2) {
        std::u16string utf16;
        utf16.reserve(value.size());
        for (const wchar_t unit : value) {
            utf16.push_back(static_cast<char16_t>(unit));
        }
        return Utf16ToUtf8Impl(utf16);
    }

    std::string output;
    output.reserve(value.size());
    for (const wchar_t unit : value) {
        auto codepoint = static_cast<std::uint32_t>(unit);
        if (codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
            codepoint = 0xfffd;
        }
        AppendUtf8(output, codepoint);
    }
    return output;
}

std::int32_t WindowsWtol(std::string_view value) noexcept { return WindowsWtolImpl(value); }

std::int32_t WindowsWtol(std::wstring_view value) noexcept { return WindowsWtolImpl(value); }

std::int32_t WindowsWtol(std::u16string_view value) noexcept { return WindowsWtolImpl(value); }

std::int64_t WindowsWcstoi64(std::u16string_view value) noexcept { return WindowsDecimalImpl<std::int64_t>(value); }

std::wstring WindowsWideFormatToHost(std::wstring_view format) {
#if defined(_WIN32)
    return std::wstring(format);
#else
    std::wstring result;
    result.reserve(format.size() + 8);

    for (std::size_t offset = 0; offset < format.size();) {
        if (format[offset] != L'%') {
            result.push_back(format[offset++]);
            continue;
        }

        result.push_back(L'%');
        ++offset;
        if (offset >= format.size()) {
            break;
        }
        if (format[offset] == L'%') {
            result.push_back(format[offset++]);
            continue;
        }

        bool hasStringLength = false;
        while (offset < format.size()) {
            const wchar_t current = format[offset];

            if (current == L'I') {
                if (offset + 2 < format.size() && format[offset + 1] == L'6' && format[offset + 2] == L'4') {
                    result.append(L"ll");
                    hasStringLength = true;
                    offset += 3;
                    continue;
                }
                if (offset + 2 < format.size() && format[offset + 1] == L'3' && format[offset + 2] == L'2') {
                    offset += 3;
                    continue;
                }
                // Microsoft's bare I length is pointer-sized.
                result.push_back(L'z');
                hasStringLength = true;
                ++offset;
                continue;
            }

            if (current == L'w') {
                // Microsoft accepts %ws as an explicit wide-string form.
                result.push_back(L'l');
                hasStringLength = true;
                ++offset;
                continue;
            }
            if (current == L'h' || current == L'l' || current == L'j' || current == L'z' || current == L't' ||
                current == L'L') {
                hasStringLength = true;
                result.push_back(current);
                ++offset;
                continue;
            }

            if (std::wcschr(L"diouxXfFeEgGaAcCsSpn", current)) {
                if (current == L's' && !hasStringLength) {
                    result.append(L"ls");
                } else if (current == L'c' && !hasStringLength) {
                    // Windows wide printf consumes a wide character for %c.
                    // POSIX needs %lc or Unicode can fail with EILSEQ.
                    result.append(L"lc");
                } else if (current == L'S') {
                    result.push_back(L's');
                } else if (current == L'C') {
                    result.push_back(L'c');
                } else {
                    result.push_back(current);
                }
                ++offset;
                break;
            }

            // Flags, field widths, precision and '*' are ABI-neutral.
            result.push_back(current);
            ++offset;
        }
    }
    return result;
#endif
}

int WindowsVswprintf(
    wchar_t* const destination, const std::size_t capacity, const wchar_t* const format, std::va_list arguments
) {
    if (destination == nullptr || capacity == 0 || format == nullptr) {
        return -1;
    }

    const std::wstring hostFormat = WindowsWideFormatToHost(format);
    std::va_list copy;
    va_copy(copy, arguments);

#if !defined(_WIN32)
    const locale_t utf8Locale = WindowsWidePrintfLocale();
    const locale_t previousLocale = utf8Locale != nullptr ? uselocale(utf8Locale) : static_cast<locale_t>(nullptr);
#endif

    const int result = std::vswprintf(destination, capacity, hostFormat.c_str(), copy);

#if !defined(_WIN32)
    if (previousLocale != nullptr) {
        static_cast<void>(uselocale(previousLocale));
    }
#endif
    va_end(copy);
    return result;
}

int WindowsSwprintf(wchar_t* const destination, const std::size_t capacity, const wchar_t* const format, ...) {
    std::va_list arguments;
    va_start(arguments, format);
    // const int result = WindowsVswprintf(
    //     destination,
    //     capacity,
    //     format,
    //     arguments)
    int result;
    try {
        result = WindowsVswprintf(destination, capacity, format, arguments);
    } catch (...) {
        // Host-format translation can allocate before formatting starts.
        va_end(arguments);
        throw;
    }
    va_end(arguments);
    return result;
}

std::string Utf16Substring(const char* utf8, int startCodeUnit, int codeUnitCount) {
    if (!utf8 || startCodeUnit < 0 || codeUnitCount <= 0) {
        return {};
    }
    const std::u16string wide = Utf8ToUtf16(utf8);
    const std::uint64_t start = static_cast<std::uint32_t>(startCodeUnit);
    const std::uint64_t count = static_cast<std::uint32_t>(codeUnitCount);
    if (start + count > wide.size()) {
        return {};
    }
    return Utf16ToUtf8(std::u16string_view(wide.data() + static_cast<std::size_t>(start), count));
}
