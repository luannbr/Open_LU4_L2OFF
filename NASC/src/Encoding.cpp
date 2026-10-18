#include "Encoding.h"

#include <cstdint>
#include <stdexcept>

namespace nasc {
namespace {

constexpr std::uint32_t replacementLimit = 0x10ffff;

[[noreturn]] void Invalid(std::string_view message) { throw std::invalid_argument(std::string(message)); }

void AppendUtf8(std::string& output, std::uint32_t codePoint) {
    if (codePoint <= 0x7f) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
    } else if (codePoint <= 0xffff) {
        output.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
    } else {
        output.push_back(static_cast<char>(0xf0 | (codePoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
    }
}

} // namespace

std::u16string Utf8ToUtf16(std::string_view input) {
    std::u16string output;
    output.reserve(input.size());

    for (std::size_t index = 0; index < input.size();) {
        const auto lead = static_cast<std::uint8_t>(input[index++]);
        std::uint32_t codePoint = 0;
        std::size_t continuationCount = 0;
        std::uint32_t minimum = 0;

        if (lead <= 0x7f) {
            codePoint = lead;
        } else if ((lead & 0xe0) == 0xc0) {
            codePoint = lead & 0x1f;
            continuationCount = 1;
            minimum = 0x80;
        } else if ((lead & 0xf0) == 0xe0) {
            codePoint = lead & 0x0f;
            continuationCount = 2;
            minimum = 0x800;
        } else if ((lead & 0xf8) == 0xf0) {
            codePoint = lead & 0x07;
            continuationCount = 3;
            minimum = 0x10000;
        } else {
            Invalid("invalid UTF-8 lead byte");
        }

        if (index + continuationCount > input.size()) {
            Invalid("truncated UTF-8 sequence");
        }
        for (std::size_t count = 0; count < continuationCount; ++count) {
            const auto byte = static_cast<std::uint8_t>(input[index++]);
            if ((byte & 0xc0) != 0x80) {
                Invalid("invalid UTF-8 continuation byte");
            }
            codePoint = (codePoint << 6) | (byte & 0x3f);
        }

        if ((continuationCount != 0 && codePoint < minimum) || codePoint > replacementLimit ||
            (codePoint >= 0xd800 && codePoint <= 0xdfff)) {
            Invalid("invalid UTF-8 code point");
        }

        if (codePoint <= 0xffff) {
            output.push_back(static_cast<char16_t>(codePoint));
        } else {
            codePoint -= 0x10000;
            output.push_back(static_cast<char16_t>(0xd800 + (codePoint >> 10)));
            output.push_back(static_cast<char16_t>(0xdc00 + (codePoint & 0x3ff)));
        }
    }

    return output;
}

std::string Utf16ToUtf8(std::u16string_view input) {
    std::string output;
    output.reserve(input.size());

    for (std::size_t index = 0; index < input.size(); ++index) {
        std::uint32_t codePoint = input[index];
        if (codePoint >= 0xd800 && codePoint <= 0xdbff) {
            if (++index >= input.size()) {
                Invalid("unpaired UTF-16 high surrogate");
            }
            const std::uint32_t low = input[index];
            if (low < 0xdc00 || low > 0xdfff) {
                Invalid("unpaired UTF-16 high surrogate");
            }
            codePoint = 0x10000 + ((codePoint - 0xd800) << 10) + (low - 0xdc00);
        } else if (codePoint >= 0xdc00 && codePoint <= 0xdfff) {
            Invalid("unpaired UTF-16 low surrogate");
        }
        AppendUtf8(output, codePoint);
    }

    return output;
}

} // namespace nasc
