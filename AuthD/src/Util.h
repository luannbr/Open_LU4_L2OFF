#pragma once

#include "Platform/WindowsTypes.h"

#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <ctime>

struct HDC__;
struct HFONT__;
struct tagTIMESTAMP_STRUCT;

int LNStrNCpy(
    char* destination,
    const char* source,
    int length);

int Assemble(
    char* buffer,
    int bufferSize,
    const char* format,
    std::va_list arguments);

int Assemble(
    char* buffer,
    int bufferSize,
    const char* format,
    ...);

void EncPwdL2(char* password);
void EncPwdShalo(char* password);
void EncPwdDev(char* password);

template <typename Character>
unsigned int Encode(
    unsigned int sourceCharacters,
    const unsigned char* sourceData,
    unsigned int destinationCharacters,
    Character* destinationData);

extern template unsigned int Encode<char>(
    unsigned int sourceCharacters,
    const unsigned char* sourceData,
    unsigned int destinationCharacters,
    char* destinationData);

unsigned int Base64Encode(
    unsigned int sourceCharacters,
    const unsigned char* sourceData,
    unsigned int destinationCharacters,
    char* destinationData);

void StdAccount(char* account);
void DumpPacket(
    const unsigned char* packet,
    int packetLength);

void WritePacketLog(
    const char* packetPath,
    unsigned int socket,
    const unsigned char* packet,
    int packetLength,
    int lengthLimit);

void WriteLogD(
    int logId,
    char* account,
    in_addr ip,
    int status,
    int age,
    int gender,
    int zipcode,
    int parameter1,
    int uid);

int AnsiToUnicode(
    char* source,
    int sourceCapacity,
    wchar_t* destination,
    int destinationCapacity);

long ConvertSQLToTome(
    tagTIMESTAMP_STRUCT& sqlDate,
    std::tm* output);

long DBTimeStampToTime(
    const tagTIMESTAMP_STRUCT& timeStamp);

HFONT__* GetAFont(HDC__* deviceContext);
bool CheckDiskSpace(
    const char* directoryPath,
    ULARGE_INTEGER requiredFreeSpace);

bool CheckAccount(char* account);
bool IsValidNumeric(
    char* value,
    int length);

char MapCharacter(char source);
unsigned char* StrToBytes(
    unsigned char* destination,
    unsigned int destinationLength,
    const char* source);

char* StrStr(
    const char* haystack,
    int haystackLength,
    const char* needle,
    bool caseSensitivity);

int StrCmp(
    const char* first,
    const char* second,
    bool caseSensitivity);

char* StrGetTok(
    char* token,
    unsigned int tokenLength,
    const char* haystack,
    unsigned int haystackLength,
    const char* delimiters);

unsigned int StrLen(const char* string);

unsigned int StrPrintf(
    wchar_t* destination,
    unsigned int destinationLength,
    const wchar_t* format,
    ...);

unsigned int StrPrintfV(
    wchar_t* destination,
    unsigned int destinationLength,
    const wchar_t* format,
    std::va_list& arguments);

unsigned int StrPrintf(
    char* destination,
    unsigned int destinationLength,
    const char* format,
    ...);

unsigned int StrCopy(
    char* destination,
    const char* source,
    unsigned int destinationLength);

char* StrChr(
    const char* haystack,
    unsigned int haystackLength,
    char needle);

unsigned int StrCat(
    char* destination,
    const char* source,
    unsigned int destinationLength);

char* StrTok_R(
    char* string,
    const char* separators,
    char** context);

char* GetTimeTag(
    char* destination,
    long inputTime,
    int destinationLength);

char* GetTimeTag(
    char* destination,
    int destinationLength);

// Original owner: Src/util.h. L2Authd.pdb type 0x737D, sizeof 0x20.
class Stopwatch {
public:
    Stopwatch();
    ~Stopwatch();

    void Start();
    std::int64_t Pause();
    void Resume();
    std::int64_t Now() const;

private:
    std::int64_t NowThisLap() const;

    std::int64_t mPerfFreq;
    std::int64_t mPerfStart;
    std::int64_t mTotalTime;
    bool mRunning;
};


inline in_addr GetAddrFromPacket(
    const unsigned char** packet) noexcept {
    in_addr result{};
    std::memcpy(&result.s_addr, *packet, 4);
    *packet += 4;
    return result;
}

inline char GetCharFromPacket(
    const unsigned char** packet) noexcept {
    const char result = static_cast<char>(**packet);
    ++*packet;
    return result;
}

inline std::int32_t GetIntFromPacket(
    const unsigned char** packet) noexcept {
    std::int32_t result = 0;
    std::memcpy(&result, *packet, 4);
    *packet += 4;
    return result;
}

inline std::int64_t GetInt64FromPacket(
    const unsigned char** packet) noexcept {
    std::int64_t result = 0;
    std::memcpy(&result, *packet, 8);
    *packet += 8;
    return result;
}

inline std::int16_t GetShortFromPacket(
    const unsigned char** packet) noexcept {
    std::int16_t result = 0;
    std::memcpy(&result, *packet, 2);
    *packet += 2;
    return result;
}

inline void GetStrFromPacket(
    const unsigned char** packet,
    int length,
    char* destination) {
    *packet += LNStrNCpy(
        destination,
        reinterpret_cast<const char*>(*packet),
        length);
}

inline void GetBinFromPacket(
    const unsigned char** packet,
    int length,
    unsigned char* destination) noexcept {
    std::memcpy(destination, *packet, length);
    *packet += length;
}

template <typename Value>
bool BufCat(
    char** buffer,
    int available,
    Value value) noexcept {
    // Src/util.h:98-106 compares the signed count as unsigned against
    // sizeof(T)-1. This keeps the x86 helper's treatment
    // of negative counts.
    const bool fits =
        sizeof(Value) - 1U <
        static_cast<unsigned int>(available);
    if (fits) {
        std::memcpy(*buffer, &value, sizeof(Value));
        *buffer += sizeof(Value);
    }
    return fits;
}

inline bool BufCat(
    char** buffer,
    int available,
    const char* source,
    int length) noexcept {
    const bool fits = length <= available;
    if (fits) {
        std::memcpy(*buffer, source, length);
        *buffer += length;
    }
    return fits;
}
