#include "BlowfishWrapper.h"

#include "Shared/src/Runtime/LittleEndian.h"

#include <cstdint>

// AuthGateD.exe 0x00417A00, BlowfishWrapper.cpp.
bool CBlowfishWrapper::Init(unsigned char* key, int keyLength) {
    InitializeBlowfishLocal(
        &m_mtx,
        key,
        static_cast<short>(keyLength));
    return true;
}

// AuthGateD.exe 0x00417A20, BlowfishWrapper.cpp.
bool CBlowfishWrapper::EncryptPrimary(
    unsigned char* buffer,
    int* length) {
    const std::uint32_t alignedLength =
        (static_cast<std::uint32_t>(*length) + 7U) & ~7U;
    *length = static_cast<int>(alignedLength);

    std::uint32_t rolling = runtime::LoadLittleEndian32(buffer);
    const int wordCount = static_cast<int>(alignedLength >> 2U);
    int index = 1;
    while (index < wordCount) {
        rolling += runtime::LoadLittleEndian32(buffer + index * 4);
        runtime::StoreLittleEndian32(
            buffer + index * 4,
            runtime::LoadLittleEndian32(buffer + index * 4) ^ rolling);
        ++index;
    }
    runtime::StoreLittleEndian32(buffer + index * 4, rolling);

    *length += 8;
    BlowfishEncryptLocal(&m_mtx, buffer, *length);
    return true;
}

// AuthGateD.exe 0x00417A80, BlowfishWrapper.cpp.
bool CBlowfishWrapper::EncryptSecondary(
    unsigned char* buffer,
    int* length) {
    const std::uint32_t alignedLength =
        (static_cast<std::uint32_t>(*length) + 7U) & ~7U;
    *length = static_cast<int>(alignedLength);

    std::uint32_t checksum = 0;
    const int wordCount = static_cast<int>(alignedLength >> 2U);
    for (int index = 0; index < wordCount; ++index) {
        checksum ^= runtime::LoadLittleEndian32(buffer + index * 4);
    }
    runtime::StoreLittleEndian32(buffer + wordCount * 4, checksum);

    *length += 8;
    BlowfishEncryptLocal(&m_mtx, buffer, *length);
    return true;
}

// AuthGateD.exe 0x00417AD0, BlowfishWrapper.cpp.
bool CBlowfishWrapper::DecryptSecondary(
    unsigned char* buffer,
    int length) {
    if ((static_cast<unsigned int>(length) & 7U) != 0) {
        return false;
    }

    BlowfishDecryptLocal(&m_mtx, buffer, length);
    const int wordCount = (length - 8) >> 2;
    std::uint32_t checksum = 0;
    for (int index = 0; index < wordCount; ++index) {
        checksum ^= runtime::LoadLittleEndian32(buffer + index * 4);
    }
    return runtime::LoadLittleEndian32(
               buffer + wordCount * 4) == checksum;
}
