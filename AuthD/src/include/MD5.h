#pragma once

#include <cstdint>

// Original owner: Src/include/md5.h. L2Authd.pdb type 0x8D73 records the
// classic 88-byte RSA-reference MD5 state. `unsigned long` was 32 bits in the
// target ABI, so fixed-width fields keep it on LP64 hosts.
class MD5 {
public:
    MD5();

    void Update(
        unsigned char* input,
        unsigned int inputLength);
    void Final(unsigned char digest[16]);

protected:
    std::uint32_t state[4];
    std::uint32_t count[2];
    unsigned char buffer[64];
};
