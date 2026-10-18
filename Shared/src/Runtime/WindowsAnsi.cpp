#include "Shared/src/Runtime/WindowsAnsi.h"

#include "TextRuntime.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <iconv.h>
#endif

namespace portable {

namespace {

#if !defined(_WIN32)

const std::string& WindowsAnsiEncoding() {
    static const std::string encoding = [] {
        const char* const configured = std::getenv("L2NPC_WINDOWS_ACP");
        if (!configured || *configured == '\0') {
            // The recovered configuration's Country=0 is Korea. The Windows
            // image itself asks CP_ACP, so deployments cloned from another
            // Windows host must set this to that host's numeric ACP.
            return std::string("CP949");
        }

        const std::string value(configured);
        const bool numeric = std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return character >= '0' && character <= '9';
        });
        if (!numeric) {
            return value;
        }
        if (value == "65001") {
            return std::string("UTF-8");
        }
        return std::string("CP") + value;
    }();
    return encoding;
}

std::size_t Utf8SequenceSize(unsigned char first, std::size_t remaining) {
    std::size_t size = 1;
    if ((first & 0xe0U) == 0xc0U) {
        size = 2;
    } else if ((first & 0xf0U) == 0xe0U) {
        size = 3;
    } else if ((first & 0xf8U) == 0xf0U) {
        size = 4;
    }
    return std::min(size, remaining);
}

bool EncodeWindowsAnsi(std::string_view utf8, std::string& encoded) {
    iconv_t converter = ::iconv_open(WindowsAnsiEncoding().c_str(), "UTF-8");
    if (converter == reinterpret_cast<iconv_t>(-1)) {
        return false;
    }

    std::vector<char> output(std::max<std::size_t>(32, utf8.size() * 2U + 8U));
    const char* input = utf8.data();
    std::size_t inputLeft = utf8.size();
    std::size_t outputUsed = 0;

    while (inputLeft != 0) {
        if (outputUsed == output.size()) {
            output.resize(output.size() * 2U);
        }
        char* outputCursor = output.data() + outputUsed;
        std::size_t outputLeft = output.size() - outputUsed;

        char* mutableInput = const_cast<char*>(input);
        const std::size_t result = ::iconv(converter, &mutableInput, &inputLeft, &outputCursor, &outputLeft);
        input = mutableInput;
        outputUsed = output.size() - outputLeft;
        if (result != static_cast<std::size_t>(-1)) {
            continue;
        }
        if (errno == E2BIG) {
            output.resize(output.size() * 2U);
            continue;
        }
        if (errno != EILSEQ && errno != EINVAL) {
            ::iconv_close(converter);
            return false;
        }

        // WideCharToMultiByte with flags/default pointers equal to zero uses
        // the code page's default character for an unrepresentable scalar.
        output[outputUsed++] = '?';
        const std::size_t skipped = Utf8SequenceSize(static_cast<unsigned char>(*input), inputLeft);
        input += skipped;
        inputLeft -= skipped;
    }

    ::iconv_close(converter);
    encoded.assign(output.data(), outputUsed);
    return true;
}

bool DecodeWindowsAnsi(std::string_view encoded, std::string& utf8) {
    iconv_t converter = ::iconv_open("UTF-8", WindowsAnsiEncoding().c_str());
    if (converter == reinterpret_cast<iconv_t>(-1)) {
        return false;
    }

    std::vector<char> output(std::max<std::size_t>(32, encoded.size() * 3U + 8U));
    const char* input = encoded.data();
    std::size_t inputLeft = encoded.size();
    std::size_t outputUsed = 0;

    while (inputLeft != 0) {
        if (output.size() - outputUsed < 4U) {
            output.resize(output.size() * 2U);
        }
        char* outputCursor = output.data() + outputUsed;
        std::size_t outputLeft = output.size() - outputUsed;

        char* mutableInput = const_cast<char*>(input);
        const std::size_t result = ::iconv(converter, &mutableInput, &inputLeft, &outputCursor, &outputLeft);
        input = mutableInput;
        outputUsed = output.size() - outputLeft;
        if (result != static_cast<std::size_t>(-1)) {
            continue;
        }
        if (errno == E2BIG) {
            output.resize(output.size() * 2U);
            continue;
        }
        if (errno != EILSEQ && errno != EINVAL) {
            ::iconv_close(converter);
            return false;
        }

        // MultiByteToWideChar with flags equal to zero replaces an invalid
        // input sequence instead of rejecting the complete conversion.
        output[outputUsed++] = static_cast<char>(0xef);
        output[outputUsed++] = static_cast<char>(0xbf);
        output[outputUsed++] = static_cast<char>(0xbd);
        ++input;
        --inputLeft;
    }

    ::iconv_close(converter);
    utf8.assign(output.data(), outputUsed);
    return true;
}

template <typename Character>
int CopyEncoded(
    const Character* source,
    int sourceLength,
    char* destination,
    int destinationCount,
    std::string (*toUtf8)(std::basic_string_view<Character>)
) {
    const bool nullTerminated = sourceLength == -1;
    const std::size_t length =
        nullTerminated ? std::char_traits<Character>::length(source) : static_cast<std::size_t>(sourceLength);
    const std::string utf8 = toUtf8(std::basic_string_view<Character>(source, length));
    std::string encoded;
    if (!EncodeWindowsAnsi(utf8, encoded)) {
        return 0;
    }
    const std::size_t requiredSize = encoded.size() + (nullTerminated ? 1U : 0U);
    if (requiredSize > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return 0;
    }
    const int required = static_cast<int>(requiredSize);
    if (!destination && destinationCount == 0) {
        return required;
    }
    if (!destination || destinationCount < required) {
        return 0;
    }
    if (!encoded.empty()) {
        std::memcpy(destination, encoded.data(), encoded.size());
    }
    if (nullTerminated) {
        destination[encoded.size()] = '\0';
    }
    return required;
}

template <typename Character>
int CopyDecoded(
    const char* source,
    int sourceLength,
    Character* destination,
    int destinationCount,
    std::basic_string<Character> (*fromUtf8)(std::string_view)
) {
    if (!source || sourceLength < -1) {
        return 0;
    }
    const bool nullTerminated = sourceLength == -1;
    const std::size_t length =
        nullTerminated ? std::char_traits<char>::length(source) : static_cast<std::size_t>(sourceLength);
    std::string utf8;
    if (!DecodeWindowsAnsi(std::string_view(source, length), utf8)) {
        return 0;
    }
    const std::basic_string<Character> decoded = fromUtf8(utf8);
    const std::size_t requiredSize = decoded.size() + (nullTerminated ? 1U : 0U);
    if (requiredSize > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return 0;
    }
    const int required = static_cast<int>(requiredSize);
    if (!destination && destinationCount == 0) {
        return required;
    }
    if (!destination || destinationCount < required) {
        return 0;
    }
    std::copy(decoded.begin(), decoded.end(), destination);
    if (nullTerminated) {
        destination[decoded.size()] = static_cast<Character>(0);
    }
    return required;
}

#endif

template <typename Character>
std::string CopyToString(const Character* source, int sourceLength, int (*convert)(const Character*, int, char*, int)) {
    const int required = convert(source, sourceLength, nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    if (convert(source, sourceLength, result.data(), required) == 0) {
        return {};
    }
    return result;
}

} // namespace

int WideCharToWindowsAnsi(const char16_t* source, int sourceLength, char* destination, int destinationCount) {
#if defined(_WIN32)
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    return ::WideCharToMultiByte(
        CP_ACP, 0, reinterpret_cast<const wchar_t*>(source), sourceLength, destination, destinationCount, nullptr, nullptr
    );
#else
    return CopyEncoded(source, sourceLength, destination, destinationCount, &Utf16ToUtf8);
#endif
}

int WideCharToWindowsAnsi(const wchar_t* source, int sourceLength, char* destination, int destinationCount) {
#if defined(_WIN32)
    return ::WideCharToMultiByte(CP_ACP, 0, source, sourceLength, destination, destinationCount, nullptr, nullptr);
#else
    return CopyEncoded(source, sourceLength, destination, destinationCount, &WideToUtf8);
#endif
}

int WindowsAnsiToWideChar(const char* source, int sourceLength, char16_t* destination, int destinationCount) {
#if defined(_WIN32)
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    return ::MultiByteToWideChar(CP_ACP, 0, source, sourceLength, reinterpret_cast<wchar_t*>(destination), destinationCount);
#else
    return CopyDecoded(source, sourceLength, destination, destinationCount, &Utf8ToUtf16);
#endif
}

int WindowsAnsiToWideChar(const char* source, int sourceLength, wchar_t* destination, int destinationCount) {
#if defined(_WIN32)
    return ::MultiByteToWideChar(CP_ACP, 0, source, sourceLength, destination, destinationCount);
#else
    return CopyDecoded(source, sourceLength, destination, destinationCount, &Utf8ToWide);
#endif
}

std::string WindowsAnsiFromUtf16(std::u16string_view source) {
    if (source.size() > static_cast<std::size_t>(INT_MAX)) {
        return {};
    }
    return CopyToString(
        source.data(), static_cast<int>(source.size()),
        static_cast<int (*)(const char16_t*, int, char*, int)>(&WideCharToWindowsAnsi)
    );
}

std::string WindowsAnsiFromWide(std::wstring_view source) {
    if (source.size() > static_cast<std::size_t>(INT_MAX)) {
        return {};
    }
    return CopyToString(
        source.data(), static_cast<int>(source.size()),
        static_cast<int (*)(const wchar_t*, int, char*, int)>(&WideCharToWindowsAnsi)
    );
}

std::u16string Utf16FromWindowsAnsi(const char* source) { return Utf16FromWindowsAnsi(source, {}); }

std::u16string Utf16FromWindowsAnsi(const char* source, std::u16string_view nullValue) {
    if (!source) {
        return std::u16string(nullValue);
    }
    const int required = WindowsAnsiToWideChar(source, -1, static_cast<char16_t*>(nullptr), 0);
    if (required <= 1) {
        return {};
    }
    std::u16string result(static_cast<std::size_t>(required), u'\0');
    WindowsAnsiToWideChar(source, -1, result.data(), required);
    result.resize(static_cast<std::size_t>(required - 1));
    return result;
}

} // namespace portable
