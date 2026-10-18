#include "Util.h"

#include "Shared/src/ScopedLockGuards.h"

#include "Shared/src/Runtime/LittleEndian.h"
#include "Shared/src/Runtime/WindowsAnsi.h"
#include "Config.h"
#include "Exception.h"
#include "Log.h"
#include "LogSocket.h"
#include "SendMail.h"

#include "Shared/src/Runtime/Base64.h"

#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

namespace {

constexpr char kHexadecimalUpper[] =
    "0123456789ABCDEF";
constexpr char kHexadecimalLower[] =
    "0123456789abcdef";

constexpr char kLogDFormat[] =
    "%02d/%02d/%04d %02d:%02d:%02d.%03d "
    ",%d,,%d,,,,,,%d.%d.%d.%d,,,%d,%d,%d,%d,%d"
    ",,,,,,,%s,,,\r\n";

constexpr char kLogDShortFormat[] =
    "%02d/%02d/%04d %02d:%02d:%02d.%03d "
    ",%d,,%d,,,,,,%d.%d.%d.%d,,,%d,%d,%d"
    ",,,,,,,,,%s,,,\r\n";

constexpr wchar_t kWideLogDFormat[] =
    L"%02d/%02d/%04d %02d:%02d:%02d.%03d "
    L",%d,,%d,,,,,,%d.%d.%d.%d,,,%d,%d,%d,%d,%d"
    L",,,,,,,%ls,,,\r\n";

constexpr wchar_t kWideLogDShortFormat[] =
    L"%02d/%02d/%04d %02d:%02d:%02d.%03d "
    L",%d,,%d,,,,,,%d.%d.%d.%d,,,%d,%d,%d"
    L",,,,,,,,,%ls,,,\r\n";

struct LocalSystemTime {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int milliseconds;
};

std::tm CopyLocalTime(std::time_t value) {
#if defined(_WIN32)
    std::tm result{};
    ::localtime_s(&result, &value);
    return result;
#else
    std::tm result{};
    ::localtime_r(&value, &result);
    return result;
#endif
}

LocalSystemTime ReadLocalSystemTime() {
#if defined(_WIN32)
    SYSTEMTIME value{};
    ::GetLocalTime(&value);
    return {
        value.wYear,
        value.wMonth,
        value.wDay,
        value.wHour,
        value.wMinute,
        value.wSecond,
        value.wMilliseconds};
#else
    const auto now =
        std::chrono::system_clock::now();
    const std::time_t seconds =
        std::chrono::system_clock::to_time_t(now);
    const std::tm value = CopyLocalTime(seconds);
    const auto milliseconds =
        std::chrono::duration_cast<
            std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
    return {
        value.tm_year + 1900,
        value.tm_mon + 1,
        value.tm_mday,
        value.tm_hour,
        value.tm_min,
        value.tm_sec,
        static_cast<int>(milliseconds.count())};
#endif
}

std::array<int, 4> AddressOctets(in_addr address) {
    const std::uint32_t hostOrder = ntohl(address.s_addr);
    return {
        static_cast<int>((hostOrder >> 24) & 0xffU),
        static_cast<int>((hostOrder >> 16) & 0xffU),
        static_cast<int>((hostOrder >> 8) & 0xffU),
        static_cast<int>(hostOrder & 0xffU)};
}

void FormatLogDMessage(
    char* destination,
    std::size_t destinationSize,
    int logId,
    const LocalSystemTime& time,
    int uid,
    const std::array<int, 4>& ip,
    int status,
    int age,
    int gender,
    int parameter1,
    int zipcode,
    const char* account) {
    if (logId == 0x321 ||
        logId == 0x322 ||
        logId == 0x324) {
        std::snprintf(
            destination,
            destinationSize,
            kLogDFormat,
            time.month,
            time.day,
            time.year,
            time.hour,
            time.minute,
            time.second,
            time.milliseconds,
            logId,
            uid,
            ip[0],
            ip[1],
            ip[2],
            ip[3],
            status,
            age,
            gender,
            parameter1,
            zipcode,
            account);
    } else if (logId == 0x33f) {
        std::snprintf(
            destination,
            destinationSize,
            kLogDShortFormat,
            time.month,
            time.day,
            time.year,
            time.hour,
            time.minute,
            time.second,
            time.milliseconds,
            logId,
            uid,
            ip[0],
            ip[1],
            ip[2],
            ip[3],
            status,
            age,
            gender,
            account);
    }
}

void FormatWideLogDMessage(
    wchar_t* destination,
    int logId,
    const LocalSystemTime& time,
    int uid,
    const std::array<int, 4>& ip,
    int status,
    int age,
    int gender,
    int parameter1,
    int zipcode,
    const wchar_t* account) {
    if (logId == 0x321 ||
        logId == 0x322 ||
        logId == 0x324) {
        std::swprintf(
            destination,
            1024,
            kWideLogDFormat,
            time.month,
            time.day,
            time.year,
            time.hour,
            time.minute,
            time.second,
            time.milliseconds,
            logId,
            uid,
            ip[0],
            ip[1],
            ip[2],
            ip[3],
            status,
            age,
            gender,
            parameter1,
            zipcode,
            account);
    } else if (logId == 0x33f) {
        std::swprintf(
            destination,
            1024,
            kWideLogDShortFormat,
            time.month,
            time.day,
            time.year,
            time.hour,
            time.minute,
            time.second,
            time.milliseconds,
            logId,
            uid,
            ip[0],
            ip[1],
            ip[2],
            ip[3],
            status,
            age,
            gender,
            account);
    }
}

std::size_t Utf16Units(const wchar_t* value) {
    std::size_t count = 0;
    while (*value != L'\0') {
#if WCHAR_MAX > 0xffff
        const std::uint32_t codePoint =
            static_cast<std::uint32_t>(*value);
        count += codePoint > 0xffffU &&
                         codePoint <= 0x10ffffU
            ? 2U
            : 1U;
#else
        ++count;
#endif
        ++value;
    }
    return count;
}

void WriteUtf16CodeUnit(
    char*& destination,
    std::uint16_t value) {
    *destination++ = static_cast<char>(value);
    *destination++ = static_cast<char>(value >> 8);
}

void WriteUtf16String(
    char*& destination,
    const wchar_t* value) {
    while (*value != L'\0') {
        const std::uint32_t codePoint =
            static_cast<std::uint32_t>(*value++);
#if WCHAR_MAX > 0xffff
        if (codePoint > 0xffffU &&
            codePoint <= 0x10ffffU) {
            const std::uint32_t supplementary =
                codePoint - 0x10000U;
            WriteUtf16CodeUnit(
                destination,
                static_cast<std::uint16_t>(
                    0xd800U +
                    (supplementary >> 10)));
            WriteUtf16CodeUnit(
                destination,
                static_cast<std::uint16_t>(
                    0xdc00U +
                    (supplementary & 0x3ffU)));
            continue;
        }
#endif
        WriteUtf16CodeUnit(
            destination,
            static_cast<std::uint16_t>(codePoint));
    }
    WriteUtf16CodeUnit(destination, 0);
}

void EncodePassword(
    char* password,
    const std::array<std::uint32_t, 4>& multipliers,
    const std::array<std::uint32_t, 4>& offsets) {
    std::array<unsigned char, 16> passKey{};
    for (std::size_t index = 0;
         index < multipliers.size();
         ++index) {
        const std::uint32_t source =
            runtime::LoadLittleEndian32(password + index * 4);
        runtime::StoreLittleEndian32(
            passKey.data() + index * 4,
            source * multipliers[index] +
                offsets[index]);
    }

    auto* bytes =
        reinterpret_cast<unsigned char*>(password);
    bytes[0] ^= passKey[0];
    for (unsigned int index = 1;
         index < 16;
         ++index) {
        bytes[index] ^=
            bytes[index - 1] ^
            passKey[index & 0x0fU];
    }
    for (std::size_t index = 0; index < 16; ++index) {
        if (bytes[index] == 0) {
            bytes[index] = static_cast<unsigned char>('f');
        }
    }
}

template <typename Value>
const Value& min6(
    const Value& first,
    const Value& second) {
    return first < second ? first : second;
}

template <typename Character, typename Size>
Size IStrLen(const Character* string) {
    Size length = 0;
    while (*string++ != Character{}) {
        ++length;
    }
    return length;
}

template <typename Character, typename Size>
Character* IStrChr(
    const Character* haystack,
    Size haystackLength,
    Character needle) {
    while (haystackLength != 0 &&
           *haystack != Character{}) {
        if (*haystack == needle) {
            return const_cast<Character*>(haystack);
        }
        ++haystack;
        --haystackLength;
    }
    return nullptr;
}

template <typename Character, typename Size>
Size IStrCopy(
    Character* destination,
    const Character* source,
    Size destinationLength) {
    Size length = 0;
    while (destinationLength > 1 &&
           (*destination = *source) != Character{}) {
        ++destination;
        ++source;
        --destinationLength;
        ++length;
    }
    if (destinationLength != 0) {
        *destination = Character{};
    }
    return length;
}

template <typename Character, typename Size>
Size IStrPrintfValidate(
    Character* destination,
    Size count,
    Size result) {
    if (count == 0) {
        result = 0;
    } else if (result >= count) {
        result = count - 1;
        destination[result] = Character{};
    }
    return result;
}

template <typename Character, typename Size>
Size IStrCat(
    Character* destination,
    const Character* source,
    Size destinationLength) {
    const Size existingLength =
        IStrLen<Character, Size>(destination);
    if (destinationLength - existingLength < 2) {
        return IStrPrintfValidate<Character, Size>(
            destination,
            destinationLength,
            existingLength);
    }
    return existingLength +
           IStrCopy<Character, Size>(
               destination + existingLength,
               source,
               destinationLength - existingLength);
}

template <typename Character, typename Size>
Character* IStrGetTok(
    Character* token,
    Size tokenLength,
    const Character* haystack,
    Size haystackLength,
    const Character* delimiters) {
    const Size length = min6(
        IStrLen<Character, Size>(haystack),
        haystackLength);
    const Size delimiterLength =
        IStrLen<Character, Size>(delimiters);
    if (IStrChr<Character, Size>(
            delimiters,
            delimiterLength,
            *haystack) != nullptr) {
        *token = Character{};
        return token;
    }

    for (Size index = 1;
         index < length;
         ++index) {
        if (IStrChr<Character, Size>(
                delimiters,
                delimiterLength,
                haystack[index]) != nullptr) {
            const Size copyLength =
                min6(tokenLength, index + 1);
            IStrCopy<Character, Size>(
                token,
                haystack,
                copyLength);
            return token;
        }
    }
    IStrCopy<Character, Size>(
        token,
        haystack,
        tokenLength);
    return token;
}

std::int64_t QueryPerformanceFrequencyValue() {
#if defined(_WIN32)
    LARGE_INTEGER frequency{};
    ::QueryPerformanceFrequency(&frequency);
    return frequency.QuadPart;
#else
    return UINT64_C(1000000000);
#endif
}

std::int64_t QueryPerformanceCounterValue() {
#if defined(_WIN32)
    LARGE_INTEGER counter{};
    ::QueryPerformanceCounter(&counter);
    return counter.QuadPart;
#else
    return std::chrono::duration_cast<
               std::chrono::nanoseconds>(
               std::chrono::steady_clock::now()
                   .time_since_epoch())
        .count();
#endif
}

} // namespace

// L2Authd.exe 0x0048FB60, Src/util.cpp:125-137.
int LNStrNCpy(
    char* destination,
    const char* source,
    int length) {
    int copiedLength = 1;
    while (*source != '\0') {
        *destination = *source;
        ++source;
        ++copiedLength;
        if (length < copiedLength) {
            break;
        }
        ++destination;
    }
    *destination = '\0';
    return copiedLength;
}

// L2Authd.exe 0x0048FBC0, Src/util.cpp:12-113.
int Assemble(
    char* buffer,
    int bufferSize,
    const char* format,
    std::va_list arguments) {
    char* const start = buffer;
    char* const end = buffer + bufferSize;
    while (*format != '\0') {
        switch (*format++) {
        case 'D': {
            const std::int64_t value =
                va_arg(arguments, std::int64_t);
            if (end < buffer + 8) {
                return 0;
            }
            const std::uint64_t bits =
                static_cast<std::uint64_t>(value);
            for (unsigned int index = 0;
                 index < 8;
                 ++index) {
                *buffer++ = static_cast<char>(
                    bits >> (index * 8));
            }
            break;
        }
        case 'S': {
            const wchar_t* const value =
                va_arg(arguments, const wchar_t*);
            if (value == nullptr) {
                if (end < buffer + 2) {
                    return 0;
                }
                WriteUtf16CodeUnit(buffer, 0);
                break;
            }
            const std::size_t byteCount =
                Utf16Units(value) * 2 + 2;
            if (byteCount >
                static_cast<std::size_t>(end - buffer)) {
                return 0;
            }
            WriteUtf16String(buffer, value);
            break;
        }
        case 'b': {
            const int length = va_arg(arguments, int);
            const auto* const value =
                va_arg(arguments, const unsigned char*);
            if (length < 0 || end < buffer + length) {
                return 0;
            }
            std::memcpy(buffer, value, length);
            buffer += length;
            break;
        }
        case 'c':
            if (end < buffer + 1) {
                return 0;
            }
            *buffer++ = static_cast<char>(
                va_arg(arguments, int));
            break;
        case 'd': {
            if (end < buffer + 4) {
                return 0;
            }
            const std::uint32_t value =
                static_cast<std::uint32_t>(
                    va_arg(arguments, int));
            *buffer++ = static_cast<char>(value);
            *buffer++ = static_cast<char>(value >> 8);
            *buffer++ = static_cast<char>(value >> 16);
            *buffer++ = static_cast<char>(value >> 24);
            break;
        }
        case 'h': {
            if (end < buffer + 2) {
                return 0;
            }
            const std::uint32_t value =
                static_cast<std::uint32_t>(
                    va_arg(arguments, int));
            *buffer++ = static_cast<char>(value);
            *buffer++ = static_cast<char>(value >> 8);
            break;
        }
        case 's': {
            const char* const value =
                va_arg(arguments, const char*);
            if (value == nullptr) {
                if (end < buffer + 1) {
                    return 0;
                }
                *buffer++ = '\0';
                break;
            }
            const std::size_t byteCount =
                std::strlen(value) + 1;
            if (byteCount >
                static_cast<std::size_t>(end - buffer)) {
                return 0;
            }
            std::strcpy(buffer, value);
            buffer += byteCount;
            break;
        }
        default:
            break;
        }
    }
    return static_cast<int>(buffer - start);
}

// L2Authd.exe 0x0048FF50, Src/util.cpp:116-122.
int Assemble(
    char* buffer,
    int bufferSize,
    const char* format,
    ...) {
    std::va_list arguments;
    va_start(arguments, format);
    const int length = Assemble(
        buffer,
        bufferSize,
        format,
        arguments);
    va_end(arguments);
    return length;
}

// L2Authd.exe 0x0048FF90, Src/util.cpp:163-180.
void EncPwdL2(char* password) {
    EncodePassword(
        password,
        {UINT32_C(0x3407f),
         UINT32_C(0x340ff),
         UINT32_C(0x340d3),
         UINT32_C(0x3433d)},
        {UINT32_C(0x269735),
         UINT32_C(0x269741),
         UINT32_C(0x269935),
         UINT32_C(0x269acd)});
}

// L2Authd.exe 0x004900A0, Src/util.cpp:143-160.
void EncPwdShalo(char* password) {
    EncodePassword(
        password,
        {UINT32_C(0x343fd),
         UINT32_C(0x34127),
         UINT32_C(0x33ffb),
         UINT32_C(0x33b1d)},
        {UINT32_C(0x269735),
         UINT32_C(0x269741),
         UINT32_C(0x269935),
         UINT32_C(0x269acd)});
}

// L2Authd.exe 0x004901B0, Src/util.cpp:183-184.
void EncPwdDev(
    [[maybe_unused]] char* password) {}

// L2Authd.exe 0x004917E0, Src/util.cpp:203-241.
template <typename Character>
unsigned int Encode(
    unsigned int sourceCharacters,
    const unsigned char* sourceData,
    unsigned int destinationCharacters,
    Character* destinationData) {
    return runtime::EncodeBase64Target(
        sourceCharacters,
        sourceData,
        destinationCharacters,
        destinationData);
}

template unsigned int Encode<char>(
    unsigned int,
    const unsigned char*,
    unsigned int,
    char*);

// L2Authd.exe 0x004901C0, Src/util.cpp:249-251.
unsigned int Base64Encode(
    unsigned int sourceCharacters,
    const unsigned char* sourceData,
    unsigned int destinationCharacters,
    char* destinationData) {
    return Encode<char>(
        sourceCharacters,
        sourceData,
        destinationCharacters,
        destinationData);
}

// L2Authd.exe 0x004901E0, Src/util.cpp:349-417.
void WriteLogD(
    int logId,
    char* account,
    in_addr ip,
    int status,
    int age,
    int gender,
    int zipcode,
    int parameter1,
    int uid) {
    try {
        if ((config.gameId & 8U) == 0) {
            return;
        }

        const LocalSystemTime localTime =
            ReadLocalSystemTime();
        if (std::strlen(account) > 14) {
            account[14] = '\0';
        }
        std::array<wchar_t, 15> wideAccount{};
        AnsiToUnicode(
            account,
            15,
            wideAccount.data(),
            15);
        wideAccount[14] = L'\0';
        const std::array<int, 4> octets =
            AddressOctets(ip);

        if (config.UseLogD &&
            !LogDReconnect &&
            g_opFlag) {
            std::array<wchar_t, 1024> message{};
            FormatWideLogDMessage(
                message.data(),
                logId,
                localTime,
                uid,
                octets,
                status,
                age,
                gender,
                parameter1,
                zipcode,
                wideAccount.data());
            {
                L2_READ_GUARD(gLogLock);
                pLogSocket->Send(
                    "cddS",
                    0,
                    1,
                    6,
                    message.data());
            }
        } else {
            std::array<char, 2048> message{};
            FormatLogDMessage(
                message.data(),
                message.size(),
                logId,
                localTime,
                uid,
                octets,
                status,
                age,
                gender,
                parameter1,
                zipcode,
                account);
            message[1023] = '\0';
            logdfileLog.AddLog(
                LOG_NORMAL,
                "%s",
                message.data());
        }
    } catch (...) {
        // Original: ELOG("*%s:%d(%s) exception\n", "util.cpp", 416,
        //     "Wed Oct 23 17:02:12 2019")
        ELOG("[util.cpp] Exception while preparing or submitting a LogD record for account %d. Entering exception handling.\n",
            uid);
        send_exception(true);
    }
}

// L2Authd.exe 0x00490860, Src/util.cpp:255-274.
void StdAccount(char* account) {
    account[14] = '\0';
    const int length = static_cast<int>(
        std::strlen(account));
    if (length <= 0) {
        return;
    }
    account[0] = static_cast<char>(
        std::toupper(
            static_cast<unsigned char>(account[0])));
    for (int index = 1;
         index < length;
         ++index) {
        if (account[index] == ' ') {
            account[index] = '\0';
            return;
        }
        account[index] = static_cast<char>(
            std::tolower(
                static_cast<unsigned char>(account[index])));
    }
}

// L2Authd.exe 0x004909B0, Src/util.cpp:277-293.
void DumpPacket(
    [[maybe_unused]] const unsigned char* packet,
    [[maybe_unused]] int packetLength) {}

// L2Authd.exe 0x004909C0, Src/util.cpp:420-429.
int AnsiToUnicode(
    char* source,
    [[maybe_unused]] int sourceCapacity,
    wchar_t* destination,
    [[maybe_unused]] int destinationCapacity) {
    const int wideLength =
        portable::WindowsAnsiToWideChar(
            source,
            -1,
            static_cast<wchar_t*>(nullptr),
            0);
    if (wideLength <= 0) {
        return wideLength;
    }
    portable::WindowsAnsiToWideChar(
        source,
        -1,
        destination,
        wideLength);
    destination[wideLength - 1] = L'\0';
    return wideLength;
}

// L2Authd.exe 0x00490A10, Src/util.cpp:466-489.
bool CheckAccount(char* account) {
    if (account == nullptr) {
        return false;
    }
    const unsigned char first =
        static_cast<unsigned char>(account[0]);
    if (account[0] != '_' &&
        std::isalpha(first) == 0 &&
        std::isdigit(first) == 0) {
        return false;
    }
    for (int index = 1; index < 15; ++index) {
        if (account[index] == '\0') {
            return true;
        }
        const unsigned char value =
            static_cast<unsigned char>(account[index]);
        if (std::isalpha(value) == 0 &&
            std::isdigit(value) == 0) {
            return false;
        }
    }
    return false;
}

// L2Authd.exe 0x00490BA0, Src/util.cpp:492-504.
bool IsValidNumeric(
    char* value,
    int length) {
    if (value == nullptr || length < 1) {
        return false;
    }
    for (int index = 0; index < length; ++index) {
        if (value[index] < '0' ||
            value[index] > '9') {
            return false;
        }
    }
    return true;
}

// L2Authd.exe 0x00490C00, Src/util.cpp:296-329.
// Original implementation kept. Payload dumping exposed authentication secrets.
// void WritePacketLog(
//     const char* packetPath,
//     unsigned int socket,
//     const unsigned char* packet,
//     int packetLength,
//     int lengthLimit) {
//     const std::time_t logTime = std::time(nullptr)
//     const std::tm logTm = CopyLocalTime(logTime)
//     std::array<char, 0x4401> buffer{}
//     std::sprintf(
//         buffer.data(),
//         "%04d-%02d-%02d %02d:%02d:%02d,%s(%d),%d,",
//         logTm.tm_year + 1900,
//         logTm.tm_mon + 1,
//         logTm.tm_mday,
//         logTm.tm_hour,
//         logTm.tm_min,
//         logTm.tm_sec,
//         packetPath,
//         socket,
//         static_cast<int>(packet[0]))
// 
//     if (packetLength < lengthLimit) {
//         for (int index = 1.
//              index < packetLength && index < 0x4401.
//              ++index) {
//             std::array<char, 4> temporary{}
//             std::sprintf(
//                 temporary.data(),
//                 "%02x",
//                 static_cast<unsigned int>(packet[index]))
//             std::strcat(
//                 buffer.data(),
//                 temporary.data())
//         }
//         std::strcat(buffer.data(), "[END]\r\n")
//     } else {
//         std::array<char, 16> temporary{}
//         std::sprintf(
//             temporary.data(),
//             "[SIZE=%d]\r\n",
//             packetLength)
//         std::strcat(
//             buffer.data(),
//             temporary.data())
//     }
//     packetLog.AddLog(
//         LOG_WARN,
//         "%s",
//         buffer.data())
// }
void WritePacketLog(
    const char* packetPath,
    unsigned int socket,
    const unsigned char* packet,
    int packetLength,
    [[maybe_unused]] int lengthLimit) {
    // Auth/Gate/Token/PA payloads can contain credentials and session keys.
    // Only transport metadata is suitable for this diagnostic file.
    if (packet == nullptr || packetLength <= 0) {
        // packetLog.AddLog(LOG_WARN,
        //     "component=network event=packet_trace_skipped direction=\"%s\" source_id=%u bytes=%d reason=invalid_buffer",
        //     packetPath ? packetPath : "unknown", socket, packetLength)
        // packetLog.AddLog(LOG_WARN,
        //     "[packet trace] Skipped \"%s\" from source %u: invalid buffer or length (%d bytes).",
        //     packetPath ? packetPath : "unknown", socket, packetLength)
        L2_LOG_WARN(packetLog,
            "[packet trace] Skipped \"%s\" from source %u: invalid buffer or length (%d bytes).",
            packetPath ? packetPath : "unknown", socket, packetLength);
        return;
    }
    // packetLog.AddLog(LOG_DEBUG,
    //     "component=network event=packet_trace direction=\"%s\" source_id=%u opcode=0x%02x bytes=%d payload=redacted",
    //     packetPath ? packetPath : "unknown", socket,
    //     static_cast<unsigned int>(packet[0]), packetLength)
    // packetLog.AddLog(LOG_DEBUG,
    //     "[packet trace] \"%s\", source %u: opcode 0x%02x, %d bytes. Payload omitted.",
    //     packetPath ? packetPath : "unknown", socket,
    //     static_cast<unsigned int>(packet[0]), packetLength)
    L2_LOG_DEBUG(packetLog,
        "[packet trace] \"%s\", source %u: opcode 0x%02x, %d bytes. Payload omitted.",
        packetPath ? packetPath : "unknown", socket,
        static_cast<unsigned int>(packet[0]), packetLength);
}

// L2Authd.exe 0x00490DC0, Src/util.cpp:433-443.
long ConvertSQLToTome(
    tagTIMESTAMP_STRUCT& sqlDate,
    std::tm* output) {
    output->tm_sec = sqlDate.second;
    output->tm_min = sqlDate.minute;
    output->tm_hour = sqlDate.hour;
    output->tm_mday = sqlDate.day;
    output->tm_mon = sqlDate.month - 1;
    output->tm_year = sqlDate.year - 1900;
    output->tm_isdst = 0;
    return static_cast<long>(std::mktime(output));
}

// L2Authd.exe 0x00490E30, Src/util.cpp:446-461.
long DBTimeStampToTime(
    const tagTIMESTAMP_STRUCT& timeStamp) {
    if (timeStamp.year < 1) {
        return 0;
    }
    std::tm localTime{};
    localTime.tm_year = timeStamp.year - 1900;
    localTime.tm_mon = timeStamp.month - 1;
    localTime.tm_mday = timeStamp.day;
    localTime.tm_hour = timeStamp.hour;
    localTime.tm_min = timeStamp.minute;
    localTime.tm_sec = timeStamp.second;
    localTime.tm_isdst = 0;
    return static_cast<long>(std::mktime(&localTime));
}

// L2Authd.exe 0x00490EC0, Src/util.cpp:332-345.
HFONT__* GetAFont(
    [[maybe_unused]] HDC__* deviceContext) {
#if defined(_WIN32)
    static LOGFONTA fontDescription{};
    ::GetObjectA(
        ::GetStockObject(SYSTEM_FONT),
        sizeof(fontDescription),
        &fontDescription);
    return reinterpret_cast<HFONT__*>(
        ::CreateFontA(
            fontDescription.lfHeight,
            fontDescription.lfWidth,
            fontDescription.lfEscapement,
            fontDescription.lfOrientation,
            fontDescription.lfWeight,
            fontDescription.lfItalic,
            fontDescription.lfUnderline,
            fontDescription.lfStrikeOut,
            fontDescription.lfCharSet,
            fontDescription.lfOutPrecision,
            fontDescription.lfClipPrecision,
            fontDescription.lfQuality,
            fontDescription.lfPitchAndFamily,
            fontDescription.lfFaceName));
#else
    return nullptr;
#endif
}

// L2Authd.exe 0x00490F50, Src/util.cpp:508-532.
bool CheckDiskSpace(
    const char* directoryPath,
    ULARGE_INTEGER requiredFreeSpace) {
    std::uint64_t currentFreeSpace = 0;
    int errorCode = 0;
#if defined(_WIN32)
    ULARGE_INTEGER available{};
    if (::GetDiskFreeSpaceExA(
            directoryPath,
            nullptr,
            nullptr,
            &available) == FALSE) {
        errorCode = static_cast<int>(::GetLastError());
    } else {
        currentFreeSpace = available.QuadPart;
    }
#else
    std::error_code error;
    const std::filesystem::space_info information =
        std::filesystem::space(
            directoryPath,
            error);
    if (error) {
        errorCode = error.value();
    } else {
        currentFreeSpace = information.available;
    }
#endif

    if (errorCode != 0) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "CheckDiskFreeSpace ErrorCode %d",
        //     errorCode)
        L2_LOG_ERROR(Log,
            "[storage] Cannot query free space for '%s': operating-system error %d. Free-space check failed.",
            directoryPath != nullptr ? directoryPath : "(current directory)", errorCode);
        return false;
    }
    if (requiredFreeSpace.QuadPart <=
        currentFreeSpace) {
        return true;
    }

    // std::array<char, 1024> message{}
    // std::sprintf(
    //     message.data(),
    //     "Insufficient FreeSpace!!!!, %s",
    //     directoryPath)
    // Preserve the mail body without overflowing the former fixed-size buffer.
    // SendMessageA performs its existing length check before using the body.
    std::string message = "Insufficient FreeSpace!!!!, ";
    message += directoryPath != nullptr ? directoryPath : "(current directory)";
    if (config.mailTo != nullptr &&
        config.mailFrom != nullptr) {
        Mailservice.SendMessageA(
            const_cast<char*>(config.mailTo),
            const_cast<char*>(
                "AuthD Insufficient FreeSpace"),
            message.data());
    }
    // Log.AddLog(
    //     LOG_WARN,
    //     "%s",
    //     message.data())
    L2_LOG_WARN(Log,
        "[storage] Insufficient free space for '%s': %llu bytes reported, %llu bytes required. Free-space check failed.",
        directoryPath != nullptr ? directoryPath : "(current directory)",
        static_cast<unsigned long long>(currentFreeSpace), static_cast<unsigned long long>(requiredFreeSpace.QuadPart));
    return false;
}

// L2Authd.exe 0x00491040, Src/util.cpp:538-549.
char MapCharacter(char source) {
    char* position = StrChr(
        kHexadecimalUpper,
        17,
        source);
    if (position != nullptr) {
        return static_cast<char>(
            position - kHexadecimalUpper);
    }
    position = StrChr(
        kHexadecimalLower,
        17,
        source);
    if (position != nullptr) {
        return static_cast<char>(
            position - kHexadecimalLower);
    }
    return static_cast<char>(-1);
}

// L2Authd.exe 0x004910A0, Src/util.cpp:552-580.
unsigned char* StrToBytes(
    unsigned char* destination,
    unsigned int destinationLength,
    const char* source) {
    bool lowNibble = false;
    while (*source != '\0') {
        const char value = MapCharacter(*source);
        if (value < 0 || destinationLength == 0) {
            break;
        }
        if (lowNibble) {
            *destination = static_cast<unsigned char>(
                *destination + value);
            ++destination;
            --destinationLength;
        } else {
            *destination = static_cast<unsigned char>(
                value << 4);
        }
        lowNibble = !lowNibble;
        ++source;
    }
    return destination;
}

// L2Authd.exe 0x00491140, Src/util.cpp:583-608.
char* StrStr(
    const char* haystack,
    int haystackLength,
    const char* needle,
    bool caseSensitivity) {
    while (haystackLength > 0 &&
           *haystack != '\0') {
        const char* currentHaystack = haystack;
        const char* currentNeedle = needle;
        while (*currentHaystack != '\0' &&
               *currentNeedle != '\0') {
            if (*currentHaystack != *currentNeedle) {
                if (caseSensitivity) {
                    break;
                }
                const char foldedHaystack =
                    *currentHaystack < 'Z'
                    ? static_cast<char>(
                          *currentHaystack + 0x20)
                    : *currentHaystack;
                const char foldedNeedle =
                    *currentNeedle < 'Z'
                    ? static_cast<char>(
                          *currentNeedle + 0x20)
                    : *currentNeedle;
                if (foldedHaystack != foldedNeedle) {
                    break;
                }
            }
            ++currentHaystack;
            ++currentNeedle;
        }
        if (*currentNeedle == '\0') {
            return const_cast<char*>(haystack);
        }
        ++haystack;
        --haystackLength;
    }
    return nullptr;
}

// L2Authd.exe 0x00491230, Src/util.cpp:611-636.
int StrCmp(
    const char* first,
    const char* second,
    bool caseSensitivity) {
    while (true) {
        if (*first == '\0' || *second == '\0') {
            if (*first == '\0' && *second == '\0') {
                return 0;
            }
            return *first == '\0' ? -1 : 1;
        }
        if (caseSensitivity) {
            if (*first != *second) {
                return static_cast<int>(*first) -
                       static_cast<int>(*second);
            }
        } else {
            const char foldedFirst = *first < 'Z'
                ? static_cast<char>(*first + ' ')
                : *first;
            const char foldedSecond = *second < 'Z'
                ? static_cast<char>(*second + ' ')
                : *second;
            if (foldedFirst != foldedSecond) {
                return static_cast<int>(foldedFirst) -
                       static_cast<int>(foldedSecond);
            }
        }
        ++first;
        ++second;
    }
}

// L2Authd.exe 0x00491340, Src/util.cpp:769-771.
char* StrGetTok(
    char* token,
    unsigned int tokenLength,
    const char* haystack,
    unsigned int haystackLength,
    const char* delimiters) {
    return IStrGetTok<char, unsigned int>(
        token,
        tokenLength,
        haystack,
        haystackLength,
        delimiters);
}

// L2Authd.exe 0x00491370, Src/util.cpp:775-777.
unsigned int StrLen(const char* string) {
    return IStrLen<char, unsigned int>(string);
}

// L2Authd.exe 0x00491390, Src/util.cpp:781-788.
unsigned int StrPrintf(
    wchar_t* destination,
    unsigned int destinationLength,
    const wchar_t* format,
    ...) {
    std::va_list arguments;
    va_start(arguments, format);
    const int formatted = std::vswprintf(
        destination,
        destinationLength,
        format,
        arguments);
    va_end(arguments);
    return IStrPrintfValidate<wchar_t, unsigned int>(
        destination,
        destinationLength,
        static_cast<unsigned int>(formatted));
}

// L2Authd.exe 0x004913E0, Src/util.cpp:792-795.
unsigned int StrPrintfV(
    wchar_t* destination,
    unsigned int destinationLength,
    const wchar_t* format,
    std::va_list& arguments) {
    std::va_list copy;
    va_copy(copy, arguments);
    const int formatted = std::vswprintf(
        destination,
        destinationLength,
        format,
        copy);
    va_end(copy);
    return IStrPrintfValidate<wchar_t, unsigned int>(
        destination,
        destinationLength,
        static_cast<unsigned int>(formatted));
}

// L2Authd.exe 0x00491420, Src/util.cpp:799-806.
unsigned int StrPrintf(
    char* destination,
    unsigned int destinationLength,
    const char* format,
    ...) {
    std::va_list arguments;
    va_start(arguments, format);
    const int formatted = std::vsnprintf(
        destination,
        destinationLength,
        format,
        arguments);
    va_end(arguments);
    return IStrPrintfValidate<char, unsigned int>(
        destination,
        destinationLength,
        static_cast<unsigned int>(formatted));
}

// L2Authd.exe 0x00491470, Src/util.cpp:810-812.
unsigned int StrCopy(
    char* destination,
    const char* source,
    unsigned int destinationLength) {
    return IStrCopy<char, unsigned int>(
        destination,
        source,
        destinationLength);
}

// L2Authd.exe 0x00491490, Src/util.cpp:816-818.
char* StrChr(
    const char* haystack,
    unsigned int haystackLength,
    char needle) {
    return IStrChr<char, unsigned int>(
        haystack,
        haystackLength,
        needle);
}

// L2Authd.exe 0x004914B0, Src/util.cpp:822-824.
unsigned int StrCat(
    char* destination,
    const char* source,
    unsigned int destinationLength) {
    return IStrCat<char, unsigned int>(
        destination,
        source,
        destinationLength);
}

// L2Authd.exe 0x004914D0, Src/util.cpp:844-893.
char* StrTok_R(
    char* string,
    const char* separators,
    char** context) {
    if (string == nullptr ||
        separators == nullptr ||
        context == nullptr) {
        return nullptr;
    }
    *context = string;
    char* head = *context;
    const unsigned int separatorsLength =
        StrLen(separators);
    while (*head != '\0' &&
           StrChr(
               separators,
               separatorsLength,
               *head) != nullptr) {
        ++head;
    }
    if (*head == '\0') {
        *context = nullptr;
        return nullptr;
    }

    char* tail = head;
    while (*tail != '\0' &&
           StrChr(
               separators,
               separatorsLength,
               *tail) == nullptr) {
        ++tail;
    }
    if (*tail == '\0') {
        *context = nullptr;
    } else {
        *tail = '\0';
        *context = tail + 1;
    }
    return head;
}

// L2Authd.exe 0x004915C0, Src/util.cpp:828-835.
char* GetTimeTag(
    char* destination,
    long inputTime,
    int destinationLength) {
    const std::time_t timeValue =
        static_cast<std::time_t>(inputTime);
    const std::tm localTime =
        CopyLocalTime(timeValue);
    const int maximumLength = 63;
    StrPrintf(
        destination,
        static_cast<unsigned int>(
            min6(maximumLength, destinationLength)),
        "%4d-%02d-%02d %02d:%02d:%02d",
        localTime.tm_year + 1900,
        localTime.tm_mon + 1,
        localTime.tm_mday,
        localTime.tm_hour,
        localTime.tm_min,
        localTime.tm_sec);
    destination[63] = '\0';
    return destination;
}

// L2Authd.exe 0x00491660, Src/util.cpp:838-841.
char* GetTimeTag(
    char* destination,
    int destinationLength) {
    const long logTime = static_cast<long>(
        std::time(nullptr));
    return GetTimeTag(
        destination,
        logTime,
        destinationLength);
}

// L2Authd.exe 0x00491690, Src/util.cpp:901-904.
Stopwatch::Stopwatch()
    : mPerfFreq(QueryPerformanceFrequencyValue()) {
    Start();
}

// L2Authd.exe 0x004916B0, Src/util.cpp:907-908.
Stopwatch::~Stopwatch() = default;

// L2Authd.exe 0x004916C0, Src/util.cpp:911-915.
void Stopwatch::Start() {
    mPerfStart = QueryPerformanceCounterValue();
    mTotalTime = 0;
    mRunning = true;
}

// L2Authd.exe 0x004916F0, Src/util.cpp:918-922.
std::int64_t Stopwatch::Pause() {
    mTotalTime += NowThisLap();
    mRunning = false;
    return mTotalTime;
}

// L2Authd.exe 0x00491730, Src/util.cpp:925-928.
void Stopwatch::Resume() {
    mRunning = true;
    mPerfStart = QueryPerformanceCounterValue();
}

// L2Authd.exe 0x00491750, Src/util.cpp:938-943.
std::int64_t Stopwatch::Now() const {
    return mRunning
        ? mTotalTime + NowThisLap()
        : mTotalTime;
}

// L2Authd.exe 0x00491790, Src/util.cpp:931-935.
std::int64_t Stopwatch::NowThisLap() const {
    const std::int64_t performanceNow =
        QueryPerformanceCounterValue();
    return (performanceNow - mPerfStart) *
           INT64_C(1000000) / mPerfFreq;
}
