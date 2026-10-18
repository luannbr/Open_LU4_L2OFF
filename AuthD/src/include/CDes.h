#pragma once

// Src/include/cdes.h. L2Authd.pdb type 0x8653 records both the member order
// and the complete 0x60D0-byte object layout.
class CDes {
public:
    CDes();
    ~CDes();

    void DesKeyInit(char* key);
    void DesWriteBlock(void* buffer, int length);
    void DesReadBlock(void* buffer, int length);

private:
    void permute(char* input, char (*table)[16][8], char* output);
    void perm32(char* input, char* output);
    void expand(char* input, char* output);
    void contract(char* input, char* output);
    void f(char* right, int round, char* output);
    void iter(int round, char* input, char* output);
    int getcomp(int box, int input);
    void sinit();
    void kinit(char* key);
    void p32init();
    void perminit(char (*table)[16][8], char* permutation);
    void endes(char* input, char* output);
    void dedes(char* input, char* output);

    char mpc1m[56];
    char mpcr[56];
    char miperm[16][16][8];
    char mfperm[16][16][8];
    char ms[4][4096];
    char mp32[4][256][4];
    char mkn[16][6];
};
