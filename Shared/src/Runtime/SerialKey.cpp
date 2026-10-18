#include "Shared/src/Runtime/SerialKey.h"

#include "Shared/src/Runtime/LittleEndian.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string_view>

#if defined(_WIN32)
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <unistd.h>
#else
#include <ifaddrs.h>
#include <netpacket/packet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace runtime {
namespace {

constexpr std::string_view kSerialAlphabet = "BDEFGIJLMNOPQRSTUVWXYZ0123456789";
constexpr std::size_t kDigestBytes = 16;
constexpr std::size_t kTargetAdapterLimit = 16;

std::uint32_t RotateLeft(std::uint32_t value, unsigned count) { return (value << count) | (value >> (32U - count)); }

// CryptoAPI CALG_MD5 replacement used only for the target's validation
// digest. The input passed below includes its trailing NUL, matching
// CryptHashData(..., strlen(message) + 1, ... ) at 0x00637ce0.
std::array<std::uint8_t, kDigestBytes> Md5(const std::uint8_t* input, std::size_t length) {
    static constexpr std::array<std::uint32_t, 64> shifts = {
        7,
        12,
        17,
        22,
        7,
        12,
        17,
        22,
        7,
        12,
        17,
        22,
        7,
        12,
        17,
        22,
        5,
        9,
        14,
        20,
        5,
        9,
        14,
        20,
        5,
        9,
        14,
        20,
        5,
        9,
        14,
        20,
        4,
        11,
        16,
        23,
        4,
        11,
        16,
        23,
        4,
        11,
        16,
        23,
        4,
        11,
        16,
        23,
        6,
        10,
        15,
        21,
        6,
        10,
        15,
        21,
        6,
        10,
        15,
        21,
        6,
        10,
        15,
        21,
    };
    static constexpr std::array<std::uint32_t, 64> constants = {
        0xd76aa478,
        0xe8c7b756,
        0x242070db,
        0xc1bdceee,
        0xf57c0faf,
        0x4787c62a,
        0xa8304613,
        0xfd469501,
        0x698098d8,
        0x8b44f7af,
        0xffff5bb1,
        0x895cd7be,
        0x6b901122,
        0xfd987193,
        0xa679438e,
        0x49b40821,
        0xf61e2562,
        0xc040b340,
        0x265e5a51,
        0xe9b6c7aa,
        0xd62f105d,
        0x02441453,
        0xd8a1e681,
        0xe7d3fbc8,
        0x21e1cde6,
        0xc33707d6,
        0xf4d50d87,
        0x455a14ed,
        0xa9e3e905,
        0xfcefa3f8,
        0x676f02d9,
        0x8d2a4c8a,
        0xfffa3942,
        0x8771f681,
        0x6d9d6122,
        0xfde5380c,
        0xa4beea44,
        0x4bdecfa9,
        0xf6bb4b60,
        0xbebfbc70,
        0x289b7ec6,
        0xeaa127fa,
        0xd4ef3085,
        0x04881d05,
        0xd9d4d039,
        0xe6db99e5,
        0x1fa27cf8,
        0xc4ac5665,
        0xf4292244,
        0x432aff97,
        0xab9423a7,
        0xfc93a039,
        0x655b59c3,
        0x8f0ccc92,
        0xffeff47d,
        0x85845dd1,
        0x6fa87e4f,
        0xfe2ce6e0,
        0xa3014314,
        0x4e0811a1,
        0xf7537e82,
        0xbd3af235,
        0x2ad7d2bb,
        0xeb86d391,
    };

    const std::uint64_t bitLength = static_cast<std::uint64_t>(length) * 8U;
    std::vector<std::uint8_t> padded(input, input + length);
    padded.push_back(0x80U);
    while ((padded.size() % 64U) != 56U) {
        padded.push_back(0);
    }
    for (unsigned shift = 0; shift < 64; shift += 8) {
        padded.push_back(static_cast<std::uint8_t>(bitLength >> shift));
    }

    std::uint32_t a0 = 0x67452301U;
    std::uint32_t b0 = 0xefcdab89U;
    std::uint32_t c0 = 0x98badcfeU;
    std::uint32_t d0 = 0x10325476U;
    for (std::size_t offset = 0; offset < padded.size(); offset += 64) {
        std::array<std::uint32_t, 16> words{};
        for (std::size_t index = 0; index < words.size(); ++index) {
            words[index] = LoadLittleEndian32(padded.data() + offset + index * 4U);
        }
        std::uint32_t a = a0;
        std::uint32_t b = b0;
        std::uint32_t c = c0;
        std::uint32_t d = d0;
        for (std::uint32_t index = 0; index < 64; ++index) {
            std::uint32_t function = 0;
            std::uint32_t wordIndex = 0;
            if (index < 16) {
                function = (b & c) | ((~b) & d);
                wordIndex = index;
            } else if (index < 32) {
                function = (d & b) | ((~d) & c);
                wordIndex = (5U * index + 1U) % 16U;
            } else if (index < 48) {
                function = b ^ c ^ d;
                wordIndex = (3U * index + 5U) % 16U;
            } else {
                function = c ^ (b | (~d));
                wordIndex = (7U * index) % 16U;
            }
            const std::uint32_t nextD = c;
            c = b;
            b += RotateLeft(a + function + constants[index] + words[wordIndex], shifts[index]);
            a = d;
            d = nextD;
        }
        a0 += a;
        b0 += b;
        c0 += c;
        d0 += d;
    }

    std::array<std::uint8_t, kDigestBytes> digest{};
    StoreLittleEndian32(digest.data(), a0);
    StoreLittleEndian32(digest.data() + 4, b0);
    StoreLittleEndian32(digest.data() + 8, c0);
    StoreLittleEndian32(digest.data() + 12, d0);
    return digest;
}

std::string UppercaseTargetAscii(std::string value) {
    for (char& character : value) {
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 0x20);
        }
    }
    return value;
}

std::string FormatMac(const std::array<std::uint8_t, 6>& address) {
    char result[18]{};
    std::snprintf(
        result, sizeof(result), "%02x-%02x-%02x-%02x-%02x-%02x", address[0], address[1], address[2], address[3], address[4],
        address[5]
    );
    return result;
}

bool DecodeSerial(const std::string& serialKey, std::array<std::uint8_t, kDigestBytes>& output) {
    std::vector<std::uint8_t> values;
    values.reserve(serialKey.size());
    for (const char character : serialKey) {
        const std::size_t index = kSerialAlphabet.find(character);
        if (index != std::string_view::npos) {
            values.push_back(static_cast<std::uint8_t>(index));
        }
    }
    const std::size_t decodedBytes = (values.size() * 5U) / 8U;
    if (decodedBytes == 0 || decodedBytes > output.size()) {
        return false;
    }

    output.fill(0);
    std::size_t bitIndex = 0;
    for (const std::uint8_t value : values) {
        for (int sourceBit = 4; sourceBit >= 0; --sourceBit, ++bitIndex) {
            const std::size_t byteIndex = bitIndex / 8U;
            // 0x00638d28 emits floor(bit_count / 8) complete bytes. A
            // trailing partial group remains zero in the pre-cleared buffer.
            if (byteIndex >= decodedBytes) {
                continue;
            }
            const unsigned destinationBit = 7U - static_cast<unsigned>(bitIndex % 8U);
            if ((value & (1U << sourceBit)) != 0) {
                output[byteIndex] |= static_cast<std::uint8_t>(1U << destinationBit);
            }
        }
    }
    return true;
}

std::string EncodeSerial(const std::array<std::uint8_t, kDigestBytes>& digest) {
    constexpr std::size_t kDigestBits = kDigestBytes * 8U;
    std::string serial;
    serial.reserve((kDigestBits + 4U) / 5U);
    for (std::size_t firstBit = 0; firstBit < kDigestBits; firstBit += 5U) {
        std::uint8_t value = 0;
        for (std::size_t bit = 0; bit < 5U; ++bit) {
            value = static_cast<std::uint8_t>(value << 1U);
            const std::size_t sourceBit = firstBit + bit;
            if (sourceBit >= kDigestBits) {
                continue;
            }
            const std::uint8_t sourceByte = digest[sourceBit / 8U];
            const unsigned shift = 7U - static_cast<unsigned>(sourceBit % 8U);
            value |= static_cast<std::uint8_t>((sourceByte >> shift) & 1U);
        }
        serial.push_back(kSerialAlphabet[value]);
    }
    return serial;
}

std::array<std::uint8_t, kDigestBytes> SerialDigest(
    const std::string& computerName, const std::array<std::uint8_t, 6>& address
) {
    const std::string mac = UppercaseTargetAscii(FormatMac(address));
    std::string message = "[" + mac + "]:[L2live]:[" + computerName + "]";
    message.push_back('\0');
    return Md5(reinterpret_cast<const std::uint8_t*>(message.data()), message.size());
}

void AppendAddress(SerialKeyHostIdentity& identity, const std::uint8_t* bytes) {
    std::array<std::uint8_t, 6> address{};
    std::copy_n(bytes, address.size(), address.begin());
    // GetAdaptersInfo contributes every linked-list entry. Do not deduplicate
    // equal addresses because duplicates consume slots in the target's
    // sixteen-entry bound.
    identity.adapterAddresses.push_back(address);
}

#if defined(_WIN32)
std::string NarrowComputerName(const wchar_t* value) {
    const int required = WideCharToMultiByte(CP_THREAD_ACP, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return {};
    }
    // `required` includes the terminator. Give Win32 the full extent and
    // remove only that terminator after conversion. Passing `required` for a
    // `required - 1` string would let WideCharToMultiByte write one byte past
    // the published object on implementations without spare capacity.
    std::string result(static_cast<std::size_t>(required), '\0');
    const int converted = WideCharToMultiByte(CP_THREAD_ACP, 0, value, -1, result.data(), required, nullptr, nullptr);
    if (converted != required) {
        return {};
    }
    result.pop_back();
    return result;
}
#endif

} // namespace

SerialKeyHostIdentity ReadSerialKeyHostIdentity() {
    SerialKeyHostIdentity identity;
#if defined(_WIN32)
    std::array<wchar_t, 1024> computerName{};
    DWORD computerNameLength = static_cast<DWORD>(computerName.size());
    if (GetComputerNameW(computerName.data(), &computerNameLength) != 0) {
        identity.computerName = NarrowComputerName(computerName.data());
    }

    ULONG adapterBytes = sizeof(IP_ADAPTER_INFO);
    std::vector<std::uint8_t> adapterStorage(adapterBytes);
    auto* adapters = reinterpret_cast<IP_ADAPTER_INFO*>(adapterStorage.data());
    DWORD status = GetAdaptersInfo(adapters, &adapterBytes);
    if (status == ERROR_BUFFER_OVERFLOW) {
        adapterStorage.resize(adapterBytes);
        adapters = reinterpret_cast<IP_ADAPTER_INFO*>(adapterStorage.data());
        status = GetAdaptersInfo(adapters, &adapterBytes);
    }
    if (status == ERROR_SUCCESS) {
        for (IP_ADAPTER_INFO* adapter = adapters;
            adapter != nullptr && identity.adapterAddresses.size() < kTargetAdapterLimit; adapter = adapter->Next) {
            // The target reads six bytes without consulting AddressLength.
            AppendAddress(identity, adapter->Address);
        }
    }
#else
    std::array<char, 1024> computerName{};
    if (gethostname(computerName.data(), computerName.size()) == 0) {
        computerName.back() = '\0';
        identity.computerName = computerName.data();
    }

    ifaddrs* interfaces = nullptr;
    if (getifaddrs(&interfaces) == 0) {
        for (ifaddrs* interface = interfaces; interface != nullptr && identity.adapterAddresses.size() < kTargetAdapterLimit;
            interface = interface->ifa_next) {
            if (interface->ifa_addr == nullptr) {
                continue;
            }
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
            if (interface->ifa_addr->sa_family != AF_LINK) {
                continue;
            }
            const auto* link = reinterpret_cast<const sockaddr_dl*>(interface->ifa_addr);
            if (link->sdl_alen < 6) {
                continue;
            }
            AppendAddress(identity, reinterpret_cast<const std::uint8_t*>(LLADDR(link)));
#else
            if (interface->ifa_addr->sa_family != AF_PACKET) {
                continue;
            }
            const auto* packet = reinterpret_cast<const sockaddr_ll*>(interface->ifa_addr);
            if (packet->sll_halen < 6) {
                continue;
            }
            AppendAddress(identity, packet->sll_addr);
#endif
        }
        freeifaddrs(interfaces);
    }
#endif
    if (identity.adapterAddresses.size() > kTargetAdapterLimit) {
        identity.adapterAddresses.resize(kTargetAdapterLimit);
    }
    return identity;
}

bool ValidateSerialKey(const std::string& serialKey, const SerialKeyHostIdentity& identity) {
    std::array<std::uint8_t, kDigestBytes> decodedSerial{};
    if (!DecodeSerial(serialKey, decodedSerial)) {
        return false;
    }

    const std::string computerName = UppercaseTargetAscii(identity.computerName);
    const std::size_t adapterCount = std::min(identity.adapterAddresses.size(), kTargetAdapterLimit);
    for (std::size_t index = 0; index < adapterCount; ++index) {
        const auto digest = SerialDigest(computerName, identity.adapterAddresses[index]);
        if (digest == decodedSerial) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> GenerateSerialKeys(const SerialKeyHostIdentity& identity) {
    const std::string computerName = UppercaseTargetAscii(identity.computerName);
    const std::size_t adapterCount = std::min(identity.adapterAddresses.size(), kTargetAdapterLimit);
    std::vector<std::string> serialKeys;
    serialKeys.reserve(adapterCount);
    for (std::size_t index = 0; index < adapterCount; ++index) {
        serialKeys.push_back(EncodeSerial(SerialDigest(computerName, identity.adapterAddresses[index])));
    }
    return serialKeys;
}

} // namespace runtime
