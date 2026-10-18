#include "Blowfish.h"

#include "BlowfishTables.h"
#include "Shared/src/Runtime/LittleEndian.h"

#include <algorithm>
#include <iterator>

namespace {

std::uint32_t BlowfishF(
    const Blowfish_matrix& matrix,
    std::uint32_t value) {
    return ((matrix.S[0][value >> 24U] +
             matrix.S[1][(value >> 16U) & 0xffU]) ^
            matrix.S[2][(value >> 8U) & 0xffU]) +
           matrix.S[3][value & 0xffU];
}

} // namespace

// AuthGateD.exe 0x00417070, blowfish.cpp.
void Blowfish_encipher_local(
    Blowfish_matrix* matrix,
    std::uint32_t* left,
    std::uint32_t* right) {
    std::uint32_t leftValue = *left ^ matrix->P[0];
    std::uint32_t rightValue = *right;
    for (int round = 1; round < 17; round += 2) {
        rightValue ^= BlowfishF(*matrix, leftValue) ^ matrix->P[round];
        leftValue ^= BlowfishF(*matrix, rightValue) ^ matrix->P[round + 1];
    }
    *right = leftValue;
    *left = rightValue ^ matrix->P[17];
}

// AuthGateD.exe 0x00417470, blowfish.cpp.
void Blowfish_decipher_local(
    Blowfish_matrix* matrix,
    std::uint32_t* left,
    std::uint32_t* right) {
    std::uint32_t leftValue = *left ^ matrix->P[17];
    std::uint32_t rightValue = *right;
    for (int round = 16; round > 1; round -= 2) {
        rightValue ^= BlowfishF(*matrix, leftValue) ^ matrix->P[round];
        leftValue ^= BlowfishF(*matrix, rightValue) ^ matrix->P[round - 1];
    }
    *left = rightValue ^ matrix->P[0];
    *right = leftValue;
}

// AuthGateD.exe 0x00417870, blowfish.cpp.
void BlowfishDecryptLocal(
    Blowfish_matrix* matrix,
    unsigned char* buffer,
    int length) {
    const int blockCount = length >> 3;
    for (int block = 0; block < blockCount; ++block) {
        std::uint32_t left = runtime::LoadLittleEndian32(buffer);
        std::uint32_t right = runtime::LoadLittleEndian32(buffer + 4);
        Blowfish_decipher_local(matrix, &left, &right);
        runtime::StoreLittleEndian32(buffer, left);
        runtime::StoreLittleEndian32(buffer + 4, right);
        buffer += 8;
    }
}

// AuthGateD.exe 0x004178A0, blowfish.cpp.
void BlowfishEncryptLocal(
    Blowfish_matrix* matrix,
    unsigned char* buffer,
    int length) {
    const int blockCount = length >> 3;
    for (int block = 0; block < blockCount; ++block) {
        std::uint32_t left = runtime::LoadLittleEndian32(buffer);
        std::uint32_t right = runtime::LoadLittleEndian32(buffer + 4);
        Blowfish_encipher_local(matrix, &left, &right);
        runtime::StoreLittleEndian32(buffer, left);
        runtime::StoreLittleEndian32(buffer + 4, right);
        buffer += 8;
    }
}

// AuthGateD.exe 0x004178D0, blowfish.cpp.
short InitializeBlowfishLocal(
    Blowfish_matrix* matrix,
    const unsigned char* key,
    short keyLength) {
    std::copy(std::begin(BF_S_INIT), std::end(BF_S_INIT), &matrix->S[0][0]);
    std::copy(std::begin(BF_P_INIT), std::end(BF_P_INIT), matrix->P);

    const int length = keyLength;
    int keyOffset = 0;
    for (std::uint32_t& entry : matrix->P) {
        const std::uint32_t keyWord =
            (static_cast<std::uint32_t>(key[keyOffset]) << 24U) |
            (static_cast<std::uint32_t>(key[(keyOffset + 1) % length]) << 16U) |
            (static_cast<std::uint32_t>(key[(keyOffset + 2) % length]) << 8U) |
            static_cast<std::uint32_t>(key[(keyOffset + 3) % length]);
        entry ^= keyWord;
        keyOffset = (keyOffset + 4) % length;
    }

    std::uint32_t left = 0;
    std::uint32_t right = 0;
    for (int index = 0; index < 18; index += 2) {
        Blowfish_encipher_local(matrix, &left, &right);
        matrix->P[index] = left;
        matrix->P[index + 1] = right;
    }
    for (auto& sBox : matrix->S) {
        for (int index = 0; index < 256; index += 2) {
            Blowfish_encipher_local(matrix, &left, &right);
            sBox[index] = left;
            sBox[index + 1] = right;
        }
    }
    return 0;
}
