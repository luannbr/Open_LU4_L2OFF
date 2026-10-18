#pragma once

#include "Blowfish.h"

class CBlowfishWrapper {
public:
    CBlowfishWrapper() = default;
    ~CBlowfishWrapper() = default;

    bool Init(unsigned char* key, int keyLength);
    bool EncryptPrimary(unsigned char* buffer, int* length);
    bool EncryptSecondary(unsigned char* buffer, int* length);
    bool DecryptSecondary(unsigned char* buffer, int length);

    Blowfish_matrix m_mtx;
};
