#pragma once

#include "Shared/src/StandardContainers.h"

#include <cstddef>


// Recovered from StringTokenizer.obj in the donor PDB. Current L2NPC.exe
// keeps the same 0x88-byte object layout and method bodies at
// 0x00539c64-0x0053a22f. Current Cached.exe creates the
// same owner at 0x005993e8-0x0059987c.
class StringTokenizer {
public:
    StringTokenizer(std::u16string str, std::u16string deli);
    virtual ~StringTokenizer();

    int IsPreSame(const char16_t* str, const char16_t* deli);
    std::u16string NextToken();
    int CountTokens();

private:
    std::u16string m_sStr; // +0x08
    std::u16string m_sDeli; // +0x30
    int m_pLoc; // +0x58
    std::u16string m_str; // +0x60
};
