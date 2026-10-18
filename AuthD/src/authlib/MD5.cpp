#include "include/MD5.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr unsigned char PADDING[64] = {0x80};

constexpr std::uint32_t RotateLeft(
    std::uint32_t value,
    unsigned int count) noexcept {
    return (value << count) |
        (value >> (32U - count));
}

constexpr std::uint32_t F(
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t z) noexcept {
    return (x & y) | (~x & z);
}

constexpr std::uint32_t G(
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t z) noexcept {
    return (x & z) | (y & ~z);
}

constexpr std::uint32_t H(
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t z) noexcept {
    return x ^ y ^ z;
}

constexpr std::uint32_t I(
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t z) noexcept {
    return y ^ (x | ~z);
}

void FF(
    std::uint32_t& a,
    std::uint32_t b,
    std::uint32_t c,
    std::uint32_t d,
    std::uint32_t x,
    unsigned int s,
    std::uint32_t ac) noexcept {
    a = RotateLeft(a + F(b, c, d) + x + ac, s) + b;
}

void GG(
    std::uint32_t& a,
    std::uint32_t b,
    std::uint32_t c,
    std::uint32_t d,
    std::uint32_t x,
    unsigned int s,
    std::uint32_t ac) noexcept {
    a = RotateLeft(a + G(b, c, d) + x + ac, s) + b;
}

void HH(
    std::uint32_t& a,
    std::uint32_t b,
    std::uint32_t c,
    std::uint32_t d,
    std::uint32_t x,
    unsigned int s,
    std::uint32_t ac) noexcept {
    a = RotateLeft(a + H(b, c, d) + x + ac, s) + b;
}

void II(
    std::uint32_t& a,
    std::uint32_t b,
    std::uint32_t c,
    std::uint32_t d,
    std::uint32_t x,
    unsigned int s,
    std::uint32_t ac) noexcept {
    a = RotateLeft(a + I(b, c, d) + x + ac, s) + b;
}

// L2Authd.exe 0x0049AF70, Src/authlib/md5.cpp:281-287.
void Decode(
    std::uint32_t* output,
    const unsigned char* input,
    unsigned int length) noexcept {
    for (unsigned int i = 0, j = 0;
         j < length;
         ++i, j += 4) {
        output[i] =
            static_cast<std::uint32_t>(input[j]) |
            (static_cast<std::uint32_t>(input[j + 1]) << 8U) |
            (static_cast<std::uint32_t>(input[j + 2]) << 16U) |
            (static_cast<std::uint32_t>(input[j + 3]) << 24U);
    }
}

// L2Authd.exe 0x0049AFC0, Src/authlib/md5.cpp:266-275.
void Encode(
    unsigned char* output,
    const std::uint32_t* input,
    unsigned int length) noexcept {
    for (unsigned int i = 0, j = 0;
         j < length;
         ++i, j += 4) {
        output[j] = static_cast<unsigned char>(input[i]);
        output[j + 1] = static_cast<unsigned char>(input[i] >> 8U);
        output[j + 2] = static_cast<unsigned char>(input[i] >> 16U);
        output[j + 3] = static_cast<unsigned char>(input[i] >> 24U);
    }
}

// L2Authd.exe 0x0049B0A0, Src/authlib/md5.cpp:175-260.
void MD5Transform(
    std::uint32_t state[4],
    const unsigned char block[64]) noexcept {
    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];
    std::uint32_t x[16];
    Decode(x, block, 64);

    FF(a, b, c, d, x[0], 7, 0xd76aa478U);
    FF(d, a, b, c, x[1], 12, 0xe8c7b756U);
    FF(c, d, a, b, x[2], 17, 0x242070dbU);
    FF(b, c, d, a, x[3], 22, 0xc1bdceeeU);
    FF(a, b, c, d, x[4], 7, 0xf57c0fafU);
    FF(d, a, b, c, x[5], 12, 0x4787c62aU);
    FF(c, d, a, b, x[6], 17, 0xa8304613U);
    FF(b, c, d, a, x[7], 22, 0xfd469501U);
    FF(a, b, c, d, x[8], 7, 0x698098d8U);
    FF(d, a, b, c, x[9], 12, 0x8b44f7afU);
    FF(c, d, a, b, x[10], 17, 0xffff5bb1U);
    FF(b, c, d, a, x[11], 22, 0x895cd7beU);
    FF(a, b, c, d, x[12], 7, 0x6b901122U);
    FF(d, a, b, c, x[13], 12, 0xfd987193U);
    FF(c, d, a, b, x[14], 17, 0xa679438eU);
    FF(b, c, d, a, x[15], 22, 0x49b40821U);

    GG(a, b, c, d, x[1], 5, 0xf61e2562U);
    GG(d, a, b, c, x[6], 9, 0xc040b340U);
    GG(c, d, a, b, x[11], 14, 0x265e5a51U);
    GG(b, c, d, a, x[0], 20, 0xe9b6c7aaU);
    GG(a, b, c, d, x[5], 5, 0xd62f105dU);
    GG(d, a, b, c, x[10], 9, 0x02441453U);
    GG(c, d, a, b, x[15], 14, 0xd8a1e681U);
    GG(b, c, d, a, x[4], 20, 0xe7d3fbc8U);
    GG(a, b, c, d, x[9], 5, 0x21e1cde6U);
    GG(d, a, b, c, x[14], 9, 0xc33707d6U);
    GG(c, d, a, b, x[3], 14, 0xf4d50d87U);
    GG(b, c, d, a, x[8], 20, 0x455a14edU);
    GG(a, b, c, d, x[13], 5, 0xa9e3e905U);
    GG(d, a, b, c, x[2], 9, 0xfcefa3f8U);
    GG(c, d, a, b, x[7], 14, 0x676f02d9U);
    GG(b, c, d, a, x[12], 20, 0x8d2a4c8aU);

    HH(a, b, c, d, x[5], 4, 0xfffa3942U);
    HH(d, a, b, c, x[8], 11, 0x8771f681U);
    HH(c, d, a, b, x[11], 16, 0x6d9d6122U);
    HH(b, c, d, a, x[14], 23, 0xfde5380cU);
    HH(a, b, c, d, x[1], 4, 0xa4beea44U);
    HH(d, a, b, c, x[4], 11, 0x4bdecfa9U);
    HH(c, d, a, b, x[7], 16, 0xf6bb4b60U);
    HH(b, c, d, a, x[10], 23, 0xbebfbc70U);
    HH(a, b, c, d, x[13], 4, 0x289b7ec6U);
    HH(d, a, b, c, x[0], 11, 0xeaa127faU);
    HH(c, d, a, b, x[3], 16, 0xd4ef3085U);
    HH(b, c, d, a, x[6], 23, 0x04881d05U);
    HH(a, b, c, d, x[9], 4, 0xd9d4d039U);
    HH(d, a, b, c, x[12], 11, 0xe6db99e5U);
    HH(c, d, a, b, x[15], 16, 0x1fa27cf8U);
    HH(b, c, d, a, x[2], 23, 0xc4ac5665U);

    II(a, b, c, d, x[0], 6, 0xf4292244U);
    II(d, a, b, c, x[7], 10, 0x432aff97U);
    II(c, d, a, b, x[14], 15, 0xab9423a7U);
    II(b, c, d, a, x[5], 21, 0xfc93a039U);
    II(a, b, c, d, x[12], 6, 0x655b59c3U);
    II(d, a, b, c, x[3], 10, 0x8f0ccc92U);
    II(c, d, a, b, x[10], 15, 0xffeff47dU);
    II(b, c, d, a, x[1], 21, 0x85845dd1U);
    II(a, b, c, d, x[8], 6, 0x6fa87e4fU);
    II(d, a, b, c, x[15], 10, 0xfe2ce6e0U);
    II(c, d, a, b, x[6], 15, 0xa3014314U);
    II(b, c, d, a, x[13], 21, 0x4e0811a1U);
    II(a, b, c, d, x[4], 6, 0xf7537e82U);
    II(d, a, b, c, x[11], 10, 0xbd3af235U);
    II(c, d, a, b, x[2], 15, 0x2ad7d2bbU);
    II(b, c, d, a, x[9], 21, 0xeb86d391U);

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;

    std::memset(x, 0, sizeof(x));
}

} // namespace

// L2Authd.exe 0x0049AF40, Src/authlib/md5.cpp:101-108.
MD5::MD5() {
    count[0] = 0;
    count[1] = 0;
    state[0] = 0x67452301U;
    state[1] = 0xefcdab89U;
    state[2] = 0x98badcfeU;
    state[3] = 0x10325476U;
}

// L2Authd.exe 0x0049B7D0, Src/authlib/md5.cpp:115-142.
void MD5::Update(
    unsigned char* input,
    unsigned int inputLength) {
    unsigned int index = (count[0] >> 3U) & 0x3fU;
    const std::uint32_t inputBits =
        static_cast<std::uint32_t>(inputLength) << 3U;
    count[0] += inputBits;
    if (count[0] < inputBits) {
        ++count[1];
    }
    count[1] += inputLength >> 29U;

    const unsigned int partLength = 64U - index;
    unsigned int consumed = 0;
    if (inputLength >= partLength) {
        std::memcpy(buffer + index, input, partLength);
        MD5Transform(state, buffer);
        for (consumed = partLength;
             consumed + 63U < inputLength;
             consumed += 64U) {
            MD5Transform(state, input + consumed);
        }
        index = 0;
    }
    std::memcpy(
        buffer + index,
        input + consumed,
        inputLength - consumed);
}

// L2Authd.exe 0x0049B010, Src/authlib/md5.cpp:148-170.
void MD5::Final(unsigned char digest[16]) {
    unsigned char bits[8];
    Encode(bits, count, 8);

    const unsigned int index =
        (count[0] >> 3U) & 0x3fU;
    const unsigned int paddingLength =
        index < 56U ? 56U - index : 120U - index;
    Update(
        const_cast<unsigned char*>(PADDING),
        paddingLength);
    Update(bits, 8);
    Encode(digest, state, 16);

    std::memset(this, 0, sizeof(*this));
}
