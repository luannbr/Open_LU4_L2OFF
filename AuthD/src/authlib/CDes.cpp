#include "include/CDes.h"

#include <cstring>

namespace {

// AuthLib.lib constants compiled into L2Authd.exe at 0x005198C0-0x00519C08.
// Their names, types, and sizes come from CDes.obj's PDB records. Their bytes
// were read from the selected executable instead of copied from another
// AuthD build.
char IP[64] = {
    58, 50, 42, 34, 26, 18, 10, 2,
    60, 52, 44, 36, 28, 20, 12, 4,
    62, 54, 46, 38, 30, 22, 14, 6,
    64, 56, 48, 40, 32, 24, 16, 8,
    57, 49, 41, 33, 25, 17, 9, 1,
    59, 51, 43, 35, 27, 19, 11, 3,
    61, 53, 45, 37, 29, 21, 13, 5,
    63, 55, 47, 39, 31, 23, 15, 7,
};

char FP[64] = {
    40, 8, 48, 16, 56, 24, 64, 32,
    39, 7, 47, 15, 55, 23, 63, 31,
    38, 6, 46, 14, 54, 22, 62, 30,
    37, 5, 45, 13, 53, 21, 61, 29,
    36, 4, 44, 12, 52, 20, 60, 28,
    35, 3, 43, 11, 51, 19, 59, 27,
    34, 2, 42, 10, 50, 18, 58, 26,
    33, 1, 41, 9, 49, 17, 57, 25,
};

char PCT[56] = {
    57, 49, 41, 33, 25, 17, 9,
    1, 58, 50, 42, 34, 26, 18,
    10, 2, 59, 51, 43, 35, 27,
    19, 11, 3, 60, 52, 44, 36,
    63, 55, 47, 39, 31, 23, 15,
    7, 62, 54, 46, 38, 30, 22,
    14, 6, 61, 53, 45, 37, 29,
    21, 13, 5, 28, 20, 12, 4,
};

char TOTROT[16] = {
    1, 2, 4, 6, 8, 10, 12, 14,
    15, 17, 19, 21, 23, 25, 27, 28,
};

char PCK[48] = {
    14, 17, 11, 24, 1, 5,
    3, 28, 15, 6, 21, 10,
    23, 19, 12, 4, 26, 8,
    16, 7, 27, 20, 13, 2,
    41, 52, 31, 37, 47, 55,
    30, 40, 51, 45, 33, 48,
    44, 49, 39, 56, 34, 53,
    46, 42, 50, 36, 29, 32,
};

char SI[8][64] = {
    {
        14, 4, 13, 1, 2, 15, 11, 8, 3, 10, 6, 12, 5, 9, 0, 7,
        0, 15, 7, 4, 14, 2, 13, 1, 10, 6, 12, 11, 9, 5, 3, 8,
        4, 1, 14, 8, 13, 6, 2, 11, 15, 12, 9, 7, 3, 10, 5, 0,
        15, 12, 8, 2, 4, 9, 1, 7, 5, 11, 3, 14, 10, 0, 6, 13,
    },
    {
        15, 1, 8, 14, 6, 11, 3, 4, 9, 7, 2, 13, 12, 0, 5, 10,
        3, 13, 4, 7, 15, 2, 8, 14, 12, 0, 1, 10, 6, 9, 11, 5,
        0, 14, 7, 11, 10, 4, 13, 1, 5, 8, 12, 6, 9, 3, 2, 15,
        13, 8, 10, 1, 3, 15, 4, 2, 11, 6, 7, 12, 0, 5, 14, 9,
    },
    {
        10, 0, 9, 14, 6, 3, 15, 5, 1, 13, 12, 7, 11, 4, 2, 8,
        13, 7, 0, 9, 3, 4, 6, 10, 2, 8, 5, 14, 12, 11, 15, 1,
        13, 6, 4, 9, 8, 15, 3, 0, 11, 1, 2, 12, 5, 10, 14, 7,
        1, 10, 13, 0, 6, 9, 8, 7, 4, 15, 14, 3, 11, 5, 2, 12,
    },
    {
        7, 13, 14, 3, 0, 6, 9, 10, 1, 2, 8, 5, 11, 12, 4, 15,
        13, 8, 11, 5, 6, 15, 0, 3, 4, 7, 2, 12, 1, 10, 14, 9,
        10, 6, 9, 0, 12, 11, 7, 13, 15, 1, 3, 14, 5, 2, 8, 4,
        3, 15, 0, 6, 10, 1, 13, 8, 9, 4, 5, 11, 12, 7, 2, 14,
    },
    {
        2, 12, 4, 1, 7, 10, 11, 6, 8, 5, 3, 15, 13, 0, 14, 9,
        14, 11, 2, 12, 4, 7, 13, 1, 5, 0, 15, 10, 3, 9, 8, 6,
        4, 2, 1, 11, 10, 13, 7, 8, 15, 9, 12, 5, 6, 3, 0, 14,
        11, 8, 12, 7, 1, 14, 2, 13, 6, 15, 0, 9, 10, 4, 5, 3,
    },
    {
        12, 1, 10, 15, 9, 2, 6, 8, 0, 13, 3, 4, 14, 7, 5, 11,
        10, 15, 4, 2, 7, 12, 9, 5, 6, 1, 13, 14, 0, 11, 3, 8,
        9, 14, 15, 5, 2, 8, 12, 3, 7, 0, 4, 10, 1, 13, 11, 6,
        4, 3, 2, 12, 9, 5, 15, 10, 11, 14, 1, 7, 6, 0, 8, 13,
    },
    {
        4, 11, 2, 14, 15, 0, 8, 13, 3, 12, 9, 7, 5, 10, 6, 1,
        13, 0, 11, 7, 4, 9, 1, 10, 14, 3, 5, 12, 2, 15, 8, 6,
        1, 4, 11, 13, 12, 3, 7, 14, 10, 15, 6, 8, 0, 5, 9, 2,
        6, 11, 13, 8, 1, 4, 10, 7, 9, 5, 0, 15, 14, 2, 3, 12,
    },
    {
        13, 2, 8, 4, 6, 15, 11, 1, 10, 9, 3, 14, 5, 0, 12, 7,
        1, 15, 13, 8, 10, 3, 7, 4, 12, 5, 6, 11, 0, 14, 9, 2,
        7, 11, 4, 1, 9, 12, 14, 2, 0, 6, 10, 13, 15, 3, 5, 8,
        2, 1, 14, 7, 4, 10, 8, 13, 15, 12, 9, 0, 3, 5, 6, 11,
    },
};

char P32[32] = {
    16, 7, 20, 21, 29, 12, 28, 17,
    1, 15, 23, 26, 5, 18, 31, 10,
    2, 8, 24, 14, 32, 27, 3, 9,
    19, 13, 30, 6, 22, 11, 4, 25,
};

int BYTEBIT[8] = {128, 64, 32, 16, 8, 4, 2, 1};
int NIBBLEBIT[4] = {8, 4, 2, 1};

unsigned char Byte(char value) noexcept {
    return static_cast<unsigned char>(value);
}

} // namespace

// L2Authd.exe 0x0049A4C0, Src/authlib/cdes.cpp:121-122.
CDes::CDes() = default;

// L2Authd.exe 0x0049A4D0, Src/authlib/cdes.cpp:125-126.
CDes::~CDes() = default;

// L2Authd.exe 0x0049A4E0, Src/authlib/cdes.cpp:435-455.
void CDes::DesKeyInit(char* key) {
    unsigned char foldedKey[8]{};
    for (int index = 0; *key != '\0' && index < 40; ++index, ++key) {
        foldedKey[index & 7] ^= Byte(*key);
    }

    perminit(miperm, IP);
    perminit(mfperm, FP);
    kinit(reinterpret_cast<char*>(foldedKey));
    sinit();
    p32init();
}

// L2Authd.exe 0x0049A580, Src/authlib/cdes.cpp:427-432.
void CDes::DesReadBlock(void* buffer, int length) {
    auto* block = static_cast<char*>(buffer);
    for (int remaining = length & ~7; remaining > 0;
         remaining -= 8, block += 8) {
        dedes(block, block);
    }
}

// L2Authd.exe 0x0049A5B0, Src/authlib/cdes.cpp:418-424.
void CDes::DesWriteBlock(void* buffer, int length) {
    auto* block = static_cast<char*>(buffer);
    for (int remaining = length & ~7; remaining > 0;
         remaining -= 8, block += 8) {
        endes(block, block);
    }
}

// L2Authd.exe 0x0049A5E0, Src/authlib/cdes.cpp:209-226.
void CDes::contract(char* input, char* output) {
    const unsigned char b0 = Byte(input[0]);
    const unsigned char b1 = Byte(input[1]);
    const unsigned char b2 = Byte(input[2]);
    const unsigned char b3 = Byte(input[3]);
    const unsigned char b4 = Byte(input[4]);
    const unsigned char b5 = Byte(input[5]);
    output[0] = ms[0][(b0 << 4U) | (b1 >> 4U)];
    output[1] = ms[1][((b1 & 0x0fU) << 8U) | b2];
    output[2] = ms[2][(b3 << 4U) | (b4 >> 4U)];
    output[3] = ms[3][((b4 & 0x0fU) << 8U) | b5];
}

// L2Authd.exe 0x0049A690, Src/authlib/cdes.cpp:393-415.
void CDes::dedes(char* input, char* output) {
    char rounds[17][8];
    permute(input, miperm, rounds[0]);
    for (int round = 15, outputIndex = 1;
         round >= 0;
         --round, ++outputIndex) {
        iter(round, rounds[outputIndex - 1], rounds[outputIndex]);
    }

    char swapped[8] = {
        rounds[16][4], rounds[16][5], rounds[16][6], rounds[16][7],
        rounds[16][0], rounds[16][1], rounds[16][2], rounds[16][3],
    };
    permute(swapped, mfperm, output);
}

// L2Authd.exe 0x0049A750, Src/authlib/cdes.cpp:367-389.
void CDes::endes(char* input, char* output) {
    char rounds[17][8];
    permute(input, miperm, rounds[0]);
    for (int round = 0; round < 16; ++round) {
        iter(round, rounds[round], rounds[round + 1]);
    }

    char swapped[8] = {
        rounds[16][4], rounds[16][5], rounds[16][6], rounds[16][7],
        rounds[16][0], rounds[16][1], rounds[16][2], rounds[16][3],
    };
    permute(swapped, mfperm, output);
}

// L2Authd.exe 0x0049A810, Src/authlib/cdes.cpp:176-205.
void CDes::expand(char* input, char* output) {
    const unsigned char b0 = Byte(input[0]);
    const unsigned char b1 = Byte(input[1]);
    const unsigned char b2 = Byte(input[2]);
    const unsigned char b3 = Byte(input[3]);
    output[0] = static_cast<char>(
        (b3 << 7U) | ((b0 & 0xf8U) >> 1U) | ((b0 & 0x18U) >> 3U));
    output[1] = static_cast<char>(
        (b0 << 5U) | ((b1 & 0x80U) >> 3U) |
        ((b0 & 1U) << 3U) | ((b1 & 0xe0U) >> 5U));
    output[2] = static_cast<char>(
        ((b1 & 0x18U) << 3U) | ((b1 & 0x1fU) << 1U) |
        ((b2 & 0x80U) >> 7U));
    output[3] = static_cast<char>(
        (b1 << 7U) | ((b2 & 0xf8U) >> 1U) | ((b2 & 0x18U) >> 3U));
    output[4] = static_cast<char>(
        (b2 << 5U) | ((b3 & 0x80U) >> 3U) |
        ((b2 & 1U) << 3U) | ((b3 & 0xe0U) >> 5U));
    output[5] = static_cast<char>(
        ((b3 & 0x18U) << 3U) | ((b3 & 0x1fU) << 1U) |
        ((b0 & 0x80U) >> 7U));
}

// L2Authd.exe 0x0049A8E0, Src/authlib/cdes.cpp:229-247.
void CDes::f(char* right, int round, char* output) {
    char expanded[6];
    expand(right, expanded);
    for (int index = 0; index < 6; ++index) {
        expanded[index] = static_cast<char>(
            Byte(expanded[index]) ^ Byte(mkn[round][index]));
    }

    char contracted[4];
    contract(expanded, contracted);
    perm32(contracted, output);
}

// L2Authd.exe 0x0049A980, Src/authlib/cdes.cpp:271-277.
int CDes::getcomp(int box, int input) {
    const unsigned int value = static_cast<unsigned int>(input);
    const unsigned int index =
        ((value & 1U) << 4U) | (value & 0x20U) |
        ((value >> 1U) & 0x0fU);
    return SI[box][index];
}

// L2Authd.exe 0x0049A9B0, Src/authlib/cdes.cpp:251-268.
void CDes::iter(int round, char* input, char* output) {
    char transformed[4];
    f(input + 4, round, transformed);
    std::memcpy(output, input + 4, 4);
    for (int index = 0; index < 4; ++index) {
        output[index + 4] = static_cast<char>(
            Byte(input[index]) ^ Byte(transformed[index]));
    }
}

// L2Authd.exe 0x0049AA20, Src/authlib/cdes.cpp:293-317.
void CDes::kinit(char* key) {
    for (int index = 0; index < 56; ++index) {
        const int bit = PCT[index] - 1;
        mpc1m[index] = static_cast<char>(
            (BYTEBIT[bit & 7] & Byte(key[bit >> 3])) != 0);
    }

    std::memset(mkn, 0, sizeof(mkn));
    for (int round = 0; round < 16; ++round) {
        for (int index = 0; index < 56; ++index) {
            const int halfEnd = index < 28 ? 28 : 56;
            int source = index + TOTROT[round];
            if (source >= halfEnd) {
                source -= 28;
            }
            mpcr[index] = mpc1m[source];
        }

        for (int bit = 0; bit < 48; ++bit) {
            if (mpcr[PCK[bit] - 1] != 0) {
                mkn[round][bit >> 3] = static_cast<char>(
                    Byte(mkn[round][bit >> 3]) | BYTEBIT[bit & 7]);
            }
        }
    }
}

// L2Authd.exe 0x0049ABC0, Src/authlib/cdes.cpp:320-339.
void CDes::p32init() {
    std::memset(mp32, 0, sizeof(mp32));
    for (int inputByte = 0; inputByte < 4; ++inputByte) {
        for (int input = 0; input < 256; ++input) {
            for (int outputBit = 0; outputBit < 32; ++outputBit) {
                const int sourceBit = P32[outputBit] - 1;
                if ((sourceBit >> 3) == inputByte &&
                    (BYTEBIT[sourceBit & 7] & input) != 0) {
                    mp32[inputByte][input][outputBit >> 3] =
                        static_cast<char>(
                            Byte(mp32[inputByte][input][outputBit >> 3]) |
                            BYTEBIT[outputBit & 7]);
                }
            }
        }
    }
}

// L2Authd.exe 0x0049AC50, Src/authlib/cdes.cpp:151-171.
void CDes::perm32(char* input, char* output) {
    std::memset(output, 0, 4);
    for (int inputByte = 0; inputByte < 4; ++inputByte) {
        const unsigned char value = Byte(input[inputByte]);
        for (int outputByte = 0; outputByte < 4; ++outputByte) {
            output[outputByte] = static_cast<char>(
                Byte(output[outputByte]) |
                Byte(mp32[inputByte][value][outputByte]));
        }
    }
}

// L2Authd.exe 0x0049AD30, Src/authlib/cdes.cpp:344-363.
void CDes::perminit(char (*table)[16][8], char* permutation) {
    std::memset(table, 0, 16U * 16U * 8U);
    for (int inputNibble = 0; inputNibble < 16; ++inputNibble) {
        for (int input = 0; input < 16; ++input) {
            for (int outputBit = 0; outputBit < 64; ++outputBit) {
                const int sourceBit = permutation[outputBit] - 1;
                if ((sourceBit >> 2) == inputNibble &&
                    (NIBBLEBIT[sourceBit & 3] & input) != 0) {
                    table[inputNibble][input][outputBit >> 3] =
                        static_cast<char>(
                            Byte(table[inputNibble][input][outputBit >> 3]) |
                            BYTEBIT[outputBit & 7]);
                }
            }
        }
    }
}

// L2Authd.exe 0x0049ADB0, Src/authlib/cdes.cpp:132-147.
void CDes::permute(
    char* input,
    char (*table)[16][8],
    char* output) {
    std::memset(output, 0, 8);
    for (int inputByte = 0; inputByte < 8; ++inputByte) {
        const unsigned char value = Byte(input[inputByte]);
        const char* high = table[inputByte * 2][value >> 4U];
        const char* low = table[inputByte * 2 + 1][value & 0x0fU];
        for (int outputByte = 0; outputByte < 8; ++outputByte) {
            output[outputByte] = static_cast<char>(
                Byte(output[outputByte]) |
                Byte(high[outputByte]) |
                Byte(low[outputByte]));
        }
    }
}

// L2Authd.exe 0x0049AEC0, Src/authlib/cdes.cpp:280-288.
void CDes::sinit() {
    for (int pair = 0; pair < 4; ++pair) {
        for (int input = 0; input < 4096; ++input) {
            const int low = getcomp(pair * 2 + 1, input & 0x3f);
            const int high = getcomp(pair * 2, input >> 6);
            ms[pair][input] = static_cast<char>((high << 4) | low);
        }
    }
}
