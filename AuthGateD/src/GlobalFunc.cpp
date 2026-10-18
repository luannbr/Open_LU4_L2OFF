#include "GlobalFunc.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>

#if defined(_WIN32)
#include <ws2tcpip.h>
#endif

namespace {

bool AppendBytes(
    char*& cursor,
    const char* end,
    const void* source,
    std::size_t size) noexcept {
    if (size > static_cast<std::size_t>(end - cursor)) {
        return false;
    }
    if (size != 0) {
        std::memcpy(cursor, source, size);
        cursor += size;
    }
    return true;
}

template <typename Unsigned>
bool AppendLittleEndian(
    char*& cursor,
    const char* end,
    Unsigned value) noexcept {
    std::array<char, sizeof(Unsigned)> encoded{};
    for (std::size_t index = 0; index < encoded.size(); ++index) {
        encoded[index] = static_cast<char>(value & 0xffU);
        value = static_cast<Unsigned>(value >> 8U);
    }
    return AppendBytes(cursor, end, encoded.data(), encoded.size());
}

int AssembleArgs(
    char* destination,
    int destinationCount,
    const char* format,
    va_list arguments) {
    if (!destination || destinationCount < 0 || !format) {
        return 0;
    }

    char* cursor = destination;
    const char* const end = destination + destinationCount;
    for (const char* code = format; *code != '\0'; ++code) {
        switch (*code) {
        case 'c': {
            const auto value = static_cast<std::uint8_t>(
                va_arg(arguments, int));
            if (!AppendLittleEndian(cursor, end, value)) return 0;
            break;
        }
        case 'h': {
            const auto value = static_cast<std::uint16_t>(
                va_arg(arguments, int));
            if (!AppendLittleEndian(cursor, end, value)) return 0;
            break;
        }
        case 'd': {
            const auto value = va_arg(arguments, std::uint32_t);
            if (!AppendLittleEndian(cursor, end, value)) return 0;
            break;
        }
        case 's': {
            const char* value = va_arg(arguments, const char*);
            if (!value) value = "";
            const std::size_t size = std::strlen(value) + 1U;
            if (!AppendBytes(cursor, end, value, size)) return 0;
            break;
        }
        case 'S': {
            const char16_t* value = va_arg(arguments, const char16_t*);
            static constexpr char16_t empty[] = u"";
            if (!value) value = empty;
            std::size_t units = 0;
            while (value[units] != u'\0') ++units;
            ++units;
            if (units > std::numeric_limits<std::size_t>::max() / 2U) {
                return 0;
            }
            for (std::size_t index = 0; index < units; ++index) {
                if (!AppendLittleEndian(
                        cursor,
                        end,
                        static_cast<std::uint16_t>(value[index]))) {
                    return 0;
                }
            }
            break;
        }
        case 'b': {
            const auto size = va_arg(arguments, std::uint32_t);
            const void* value = va_arg(arguments, const void*);
            if ((size != 0 && !value) ||
                !AppendBytes(cursor, end, value, size)) {
                return 0;
            }
            break;
        }
        default:
            // The selected implementation silently ignores unknown format
            // characters instead of placing them in the packet.
            break;
        }
    }
    return static_cast<int>(cursor - destination);
}

} // namespace

void ThrowDeadlockException() {
    // AuthGateD.exe 0x0040e3e0 writes through address zero so
    // ExceptionReport captures a fatal lock timeout. Abort is the equivalent
    // portable terminal action without manufacturing a recoverable exception.
    std::abort();
}

std::string Utf16Ascii(const char16_t* value) {
    if (!value) return {};
    std::string result;
    while (*value != u'\0') {
        const char16_t character = *value++;
        result.push_back(
            character <= 0x7f
                ? static_cast<char>(character)
                : '?');
    }
    return result;
}

in_addr StrToInet(const char16_t* address) noexcept {
    in_addr result{};
    if (!address) return result;
    const std::string narrow = Utf16Ascii(address);
    if (::inet_pton(AF_INET, narrow.c_str(), &result) != 1) {
        result = {};
    }
    return result;
}

std::u16string InetToString(const in_addr& address) {
    std::array<char, INET_ADDRSTRLEN> buffer{};
    if (!::inet_ntop(AF_INET, &address, buffer.data(), buffer.size())) {
        return {};
    }
    std::u16string result;
    for (const char character : std::string(buffer.data())) {
        result.push_back(static_cast<char16_t>(
            static_cast<unsigned char>(character)));
    }
    return result;
}

char16_t* InetToStr(
    const in_addr* address,
    char16_t* destination,
    std::uint32_t destinationCount) noexcept {
    if (!destination || destinationCount == 0) return destination;
    const std::u16string text = address ? InetToString(*address) : std::u16string{};
    const std::size_t count = std::min<std::size_t>(
        text.size(),
        static_cast<std::size_t>(destinationCount - 1U));
    std::copy_n(text.data(), count, destination);
    destination[count] = u'\0';
    return destination;
}

char16_t* GetFullDate(
    char16_t* destination,
    std::uint32_t destinationCount) noexcept {
    if (!destination || destinationCount == 0) return destination;
    std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::array<char, 32> formatted{};
    std::snprintf(
        formatted.data(),
        formatted.size(),
        "%04d-%02d-%02d %02d:%02d:%02d",
        local.tm_year + 1900,
        local.tm_mon + 1,
        local.tm_mday,
        local.tm_hour,
        local.tm_min,
        local.tm_sec);
    std::size_t index = 0;
    while (formatted[index] != '\0' && index + 1U < destinationCount) {
        destination[index] = static_cast<char16_t>(formatted[index]);
        ++index;
    }
    destination[index] = u'\0';
    return destination;
}

int LNStrNCpy(
    char* destination,
    const char* source,
    int destinationCount) noexcept {
    if (!destination || destinationCount <= 0) return 0;
    if (!source || *source == '\0') {
        *destination = '\0';
        return 1;
    }
    int count = 1;
    while (*source != '\0') {
        *destination = *source;
        ++source;
        ++count;
        if (destinationCount < count) break;
        ++destination;
    }
    *destination = '\0';
    return count;
}

int Assemble(
    char* destination,
    int destinationCount,
    const char* format,
    va_list arguments) {
    return AssembleArgs(
        destination,
        destinationCount,
        format,
        arguments);
}

int Assemble(
    char* destination,
    int destinationCount,
    const char* format,
    ...) {
    va_list arguments;
    va_start(arguments, format);
    const int result = AssembleArgs(
        destination,
        destinationCount,
        format,
        arguments);
    va_end(arguments);
    return result;
}

// AuthGateD.exe 0x0040EAB0 reads ProductVersion from its own PE resource and
// caches it. The matching image resource contains this exact value. Portable
// binaries have no Win32 version resource to query.
const char* GetProductVersion() noexcept {
    return "1.1.12-RELEASE";
}
