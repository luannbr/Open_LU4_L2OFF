#include "include/Generator.h"

#include "include/MD5.h"

#include "Shared/src/Runtime/LittleEndian.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace {

char generateKeyStorage[] =
    "rarara~!ralla assa~!sizeof(int)invalid access~!!";

} // namespace

// L2Authd.exe 0x00519C48 points at the 48-byte string at 0x005008D4.
char* GENERATE_KEY = generateKeyStorage;

// L2Authd.exe 0x0049B880, Src/authlib/generator.cpp:15-17.
CGenerator::CGenerator(char* generatorKey) {
    m_genDes.DesKeyInit(generatorKey);
}

// L2Authd.exe 0x0049B8E0, Src/authlib/generator.cpp:20-21.
CGenerator::~CGenerator() = default;

// L2Authd.exe 0x0049B8F0, Src/authlib/generator.cpp:42-50.
int CGenerator::GetCardKeyValue(int cardNumber, int key) {
    char value[8]{};
    if (GetCardKeyValue(cardNumber, key, value, 5)) {
        return std::atoi(value);
    }
    return 0;
}

// L2Authd.exe 0x0049B950, Src/authlib/generator.cpp:25-39.
bool CGenerator::GetCardKeyValue(
    int cardNumber,
    int key,
    char* value,
    std::uint32_t valueLength) {
    MD5 md5;
    md5.Update(
        reinterpret_cast<unsigned char*>(GENERATE_KEY),
        static_cast<unsigned int>(std::strlen(GENERATE_KEY)));
    md5.Update(
        reinterpret_cast<unsigned char*>(&cardNumber),
        sizeof(cardNumber));
    md5.Update(
        reinterpret_cast<unsigned char*>(&key),
        sizeof(key));

    unsigned char encryptedBuffer[16];
    md5.Final(encryptedBuffer);
    const unsigned int decimalValue =
        static_cast<unsigned int>(encryptedBuffer[0]) |
        (static_cast<unsigned int>(encryptedBuffer[1]) << 8U);

    if (valueLength != 0) {
        std::snprintf(
            value,
            valueLength,
            "%.04d",
            static_cast<int>(decimalValue));
        value[valueLength - 1] = '\0';
    }
    return true;
}

// L2Authd.exe 0x0049B9F0, Src/authlib/generator.cpp:54-72.
bool CGenerator::GetRegisterNumber(
    char* registerNumber,
    std::uint32_t numberLength) {
    static std::uint32_t offset;
    static std::int64_t lastTime;

    const std::int64_t currentTime =
        static_cast<std::int64_t>(std::time(nullptr));
    if (currentTime != lastTime) {
        offset = 0;
    }

    const std::uint64_t plainValue =
        (static_cast<std::uint64_t>(currentTime) << 16U) |
        (offset & 0xffffU);
    ++offset;

    std::array<unsigned char, 8> encrypted{};
    runtime::StoreLittleEndian64(
        encrypted.data(),
        plainValue);
    m_genDes.DesWriteBlock(encrypted.data(), encrypted.size());
    const std::uint64_t encryptedValue =
        runtime::LoadLittleEndian64(encrypted.data());

    if (numberLength != 0) {
        std::snprintf(
            registerNumber,
            numberLength,
            "%.20llu",
            static_cast<unsigned long long>(encryptedValue));
    }
    lastTime = currentTime;
    return true;
}
