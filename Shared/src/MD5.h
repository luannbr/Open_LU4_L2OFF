#pragma once

#include <cstdint>

// Current L2Server.exe 0x00ce8734/0x00ce8f68/0x00ce9048.
// The target uses the classic 88-byte RSA-reference MD5 state.
class MD5 {
public:
    MD5();

    void Update(unsigned char* input, unsigned int inputLength);
    void Final(unsigned char digest[16]);

private:
    std::uint32_t m_state[4];
    std::uint32_t m_count[2];
    unsigned char m_buffer[64];
};
