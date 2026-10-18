#include "RSAKeyPool.h"

#include "FileLog.h"

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

CRSAKeyPool* CRSAKeyPool::m_pInstance = nullptr;

// AuthGateD.exe 0x00417B20, RSAKeyPool.cpp.
CRSAKeyPool::CRSAKeyPool() {
    if (!m_pInstance) {
        m_pInstance = this;
    }
    m_iKeyRotator.store(0, std::memory_order_relaxed);
}

// The target's beecrypt keys were process-lifetime C objects and its C++
// destructor at 0x004179F0 was empty. The portable provider owns EVP objects,
// so their representation is released at process shutdown.
CRSAKeyPool::~CRSAKeyPool() {
    for (evp_pkey_st*& key : m_keyPool) {
        EVP_PKEY_free(key);
        key = nullptr;
    }
    if (m_pInstance == this) {
        m_pInstance = nullptr;
    }
}

// AuthGateD.exe 0x00417B40, RSAKeyPool.cpp.
std::int32_t CRSAKeyPool::GetKeyIndex() {
    const std::int32_t value =
        m_iKeyRotator.fetch_add(1, std::memory_order_relaxed) + 1;
    return value % static_cast<std::int32_t>(kKeyCount);
}

// AuthGateD.exe 0x00417B60, RSAKeyPool.cpp.
bool CRSAKeyPool::Decrypt(
    std::int32_t keyIndex,
    unsigned char* buffer,
    int outputLength) {
    if (keyIndex < 0 ||
        keyIndex >= static_cast<std::int32_t>(kKeyCount) ||
        outputLength < 0 ||
        outputLength > static_cast<int>(kModulusBytes) ||
        !buffer ||
        !m_keyPool[static_cast<std::size_t>(keyIndex)]) {
        return false;
    }

    EVP_PKEY_CTX* context = EVP_PKEY_CTX_new(
        m_keyPool[static_cast<std::size_t>(keyIndex)],
        nullptr);
    if (!context) {
        return false;
    }

    std::array<unsigned char, kModulusBytes> plain{};
    std::size_t plainLength = plain.size();
    const bool decrypted =
        EVP_PKEY_decrypt_init(context) > 0 &&
        EVP_PKEY_CTX_set_rsa_padding(context, RSA_NO_PADDING) > 0 &&
        EVP_PKEY_decrypt(
            context,
            plain.data(),
            &plainLength,
            buffer,
            kModulusBytes) > 0 &&
        plainLength == kModulusBytes;
    EVP_PKEY_CTX_free(context);
    if (!decrypted) {
        return false;
    }

    // beecrypt i2osp first replaced the complete 128-byte input block. The
    // following memmove kept only its least-significant requested bytes.
    std::memcpy(buffer, plain.data(), plain.size());
    std::memmove(
        buffer,
        buffer + kModulusBytes - static_cast<std::size_t>(outputLength),
        static_cast<std::size_t>(outputLength));
    return true;
}

// AuthGateD.exe 0x00417C50, RSAKeyPool.cpp.
void CRSAKeyPool::GetNKey(std::int32_t keyIndex, char* buffer) {
    auto* bytes = reinterpret_cast<unsigned char*>(buffer);
    if (!bytes) {
        return;
    }
    if (keyIndex < 0 ||
        keyIndex >= static_cast<std::int32_t>(kKeyCount) ||
        !m_keyPool[static_cast<std::size_t>(keyIndex)]) {
        std::fill_n(bytes, kModulusBytes, 0);
        return;
    }

    BIGNUM* modulus = nullptr;
    if (EVP_PKEY_get_bn_param(
            m_keyPool[static_cast<std::size_t>(keyIndex)],
            OSSL_PKEY_PARAM_RSA_N,
            &modulus) <= 0 ||
        !modulus ||
        BN_bn2binpad(
            modulus,
            bytes,
            static_cast<int>(kModulusBytes)) !=
            static_cast<int>(kModulusBytes)) {
        BN_free(modulus);
        std::fill_n(bytes, kModulusBytes, 0);
        return;
    }
    BN_free(modulus);

    std::array<unsigned char, 4> firstWord{};
    std::copy_n(bytes, firstWord.size(), firstWord.begin());
    std::copy_n(bytes + 0x4d, firstWord.size(), bytes);
    std::copy(firstWord.begin(), firstWord.end(), bytes + 0x4d);

    for (std::size_t index = 0; index < 0x40; ++index) {
        bytes[index] ^= bytes[index + 0x40];
    }
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[0x0d + index] ^= bytes[0x34 + index];
    }
    for (std::size_t index = 0x40; index < kModulusBytes; ++index) {
        bytes[index] ^= bytes[index - 0x40];
    }
}

// AuthGateD.exe 0x00417D40, RSAKeyPool.cpp.
void CRSAKeyPool::GenKey() {
    for (evp_pkey_st*& key : m_keyPool) {
        EVP_PKEY_CTX* context = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
        EVP_PKEY* generated = nullptr;
        const bool made =
            context &&
            EVP_PKEY_keygen_init(context) > 0 &&
            EVP_PKEY_CTX_set_rsa_keygen_bits(context, 0x400) > 0 &&
            EVP_PKEY_keygen(context, &generated) > 0;
        EVP_PKEY_CTX_free(context);
        if (!made) {
            EVP_PKEY_free(generated);
            // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
            //     u"component=crypto event=rsa_key_generation_failed reason=provider_failure action=abort_initialization")
            // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
            //     u"[crypto] RSA key generation failed in the provider. Aborting initialization.")
            L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
                u"[crypto] RSA key generation failed in the provider. Aborting initialization.");
            throw std::runtime_error("AuthGateD RSA key generation failed");
        }
        EVP_PKEY_free(key);
        key = generated;
    }
}

// AuthGateD.exe 0x00417DA0, RSAKeyPool.cpp.
void CRSAKeyPool::Init() {
    GenKey();
    // CFileLog::g_pDefaultFileLog->LogT(
    //     LOG_SYSTEM,
    //     u"RSA Key Generated...")
    // CFileLog::g_pDefaultFileLog->LogT(LOG_SYSTEM,
    //     u"component=crypto event=rsa_pool_ready keys=%u modulus_bits=%u",
    //     static_cast<unsigned int>(kKeyCount), static_cast<unsigned int>(kModulusBytes * 8U))
    // CFileLog::g_pDefaultFileLog->LogT(LOG_SYSTEM,
    //     u"[crypto] RSA key pool ready: %u keys with %u-bit moduli.",
    //     static_cast<unsigned int>(kKeyCount), static_cast<unsigned int>(kModulusBytes * 8U))
    L2_LOG(*CFileLog::g_pDefaultFileLog, LOG_SYSTEM,
        u"[crypto] RSA key pool ready: %u keys with %u-bit moduli.",
        static_cast<unsigned int>(kKeyCount), static_cast<unsigned int>(kModulusBytes * 8U));
}
