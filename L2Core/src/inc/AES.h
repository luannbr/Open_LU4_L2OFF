#pragma once

#include <cstddef>
#include <cstring>
#include <stdexcept>

class AES {
public:
    enum {
        ECB = 0,
        CBC = 1,
        CFB = 2,
    };

private:
    enum { DEFAULT_BLOCK_SIZE = 16 };
    enum {
        MAX_BLOCK_SIZE = 32,
        MAX_ROUNDS = 14,
        MAX_KC = 8,
        MAX_BC = 8,
    };

    static int Mul(int a, int b) { return (a != 0 && b != 0) ? sm_alog[(sm_log[a & 0xff] + sm_log[b & 0xff]) % 255] : 0; }

    static int Mul4(int a, char b[]) {
        if (a == 0) {
            return 0;
        }

        a = sm_log[a & 0xff];
        const int a0 = b[0] != 0 ? sm_alog[(a + sm_log[b[0] & 0xff]) % 255] & 0xff : 0;
        const int a1 = b[1] != 0 ? sm_alog[(a + sm_log[b[1] & 0xff]) % 255] & 0xff : 0;
        const int a2 = b[2] != 0 ? sm_alog[(a + sm_log[b[2] & 0xff]) % 255] & 0xff : 0;
        const int a3 = b[3] != 0 ? sm_alog[(a + sm_log[b[3] & 0xff]) % 255] & 0xff : 0;
        return a0 << 24 | a1 << 16 | a2 << 8 | a3;
    }

public:
    AES();
    virtual ~AES();

    void MakeKey(const char* key, const char* chain, int keylength = DEFAULT_BLOCK_SIZE, int blockSize = DEFAULT_BLOCK_SIZE);

private:
    void Xor(char* buffer, const char* chain) {
        if (!m_bKeyInit) {
            throw std::runtime_error(sm_szErrorMsg1);
        }
        for (int i = 0; i < m_blockSize; ++i) {
            *buffer++ ^= *chain++;
        }
    }

    void DefEncryptBlock(const char* in, char* result);
    void DefDecryptBlock(const char* in, char* result);

public:
    void EncryptBlock(const char* in, char* result);
    void DecryptBlock(const char* in, char* result);
    void Encrypt(const char* in, char* result, std::size_t size, int mode = ECB);
    void Decrypt(const char* in, char* result, std::size_t size, int mode = ECB);

    int GetKeyLength() {
        if (!m_bKeyInit) {
            throw std::runtime_error(sm_szErrorMsg1);
        }
        return m_keylength;
    }

    int GetBlockSize() {
        if (!m_bKeyInit) {
            throw std::runtime_error(sm_szErrorMsg1);
        }
        return m_blockSize;
    }

    int GetRounds() {
        if (!m_bKeyInit) {
            throw std::runtime_error(sm_szErrorMsg1);
        }
        return m_iROUNDS;
    }

    void ResetChain() { std::memcpy(m_chain, m_chain0, m_blockSize); }

    static const char* sm_chain0;

private:
    static const int sm_alog[256];
    static const int sm_log[256];
    static const char sm_S[256];
    static const char sm_Si[256];
    static const int sm_T1[256];
    static const int sm_T2[256];
    static const int sm_T3[256];
    static const int sm_T4[256];
    static const int sm_T5[256];
    static const int sm_T6[256];
    static const int sm_T7[256];
    static const int sm_T8[256];
    static const int sm_U1[256];
    static const int sm_U2[256];
    static const int sm_U3[256];
    static const int sm_U4[256];
    static const char sm_rcon[30];
    static const int sm_shifts[3][4][2];
    static const char* sm_szErrorMsg1;
    static const char* sm_szErrorMsg2;

    bool m_bKeyInit; // +0x008
    int m_Ke[MAX_ROUNDS + 1][MAX_BC]; // +0x00c
    int m_Kd[MAX_ROUNDS + 1][MAX_BC]; // +0x1ec
    int m_keylength; // +0x3cc
    int m_blockSize; // +0x3d0
    int m_iROUNDS; // +0x3d4
    char m_chain0[MAX_BLOCK_SIZE]; // +0x3d8
    char m_chain[MAX_BLOCK_SIZE]; // +0x3f8
    int tk[MAX_KC]; // +0x418
    int a[MAX_BC]; // +0x438
    int t[MAX_BC]; // +0x458
};
