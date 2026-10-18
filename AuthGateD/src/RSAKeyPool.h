#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

struct evp_pkey_st;

class CRSAKeyPool {
public:
    static constexpr std::size_t kKeyCount = 5;
    static constexpr std::size_t kModulusBytes = 0x80;

    CRSAKeyPool();
    ~CRSAKeyPool();

    std::int32_t GetKeyIndex();
    bool Decrypt(
        std::int32_t keyIndex,
        unsigned char* buffer,
        int outputLength);
    void GetNKey(std::int32_t keyIndex, char* buffer);
    void Init();

    static CRSAKeyPool* m_pInstance;

private:
    void GenKey();

    // The PDB owner contains beecrypt rsakp[5] followed by an interlocked
    // rotator (target size 0x180). OpenSSL replaces only that unavailable
    // platform representation. The five-key policy and wire transforms stay
    // in the original CRSAKeyPool owner.
    std::array<evp_pkey_st*, kKeyCount> m_keyPool{};
    std::atomic<std::int32_t> m_iKeyRotator{0};
};
