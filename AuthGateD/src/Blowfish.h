#pragma once

#include <cstdint>

// Exact AuthGateD PDB layout: the S boxes precede the P array.
struct Blowfish_matrix {
    std::uint32_t S[4][256]{};
    std::uint32_t P[18]{};
};


void Blowfish_encipher_local(
    Blowfish_matrix* matrix,
    std::uint32_t* left,
    std::uint32_t* right);
void Blowfish_decipher_local(
    Blowfish_matrix* matrix,
    std::uint32_t* left,
    std::uint32_t* right);
void BlowfishDecryptLocal(
    Blowfish_matrix* matrix,
    unsigned char* buffer,
    int length);
void BlowfishEncryptLocal(
    Blowfish_matrix* matrix,
    unsigned char* buffer,
    int length);
short InitializeBlowfishLocal(
    Blowfish_matrix* matrix,
    const unsigned char* key,
    short keyLength);
