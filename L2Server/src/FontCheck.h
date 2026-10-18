#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

struct FFontGlyphInfo {
    std::int32_t X;
    std::int32_t XL;
    std::int32_t Y;
    std::int32_t YL;
};

class NCharSet {
public:
    NCharSet();
    ~NCharSet();

    bool LoadGlyphInfo(int start, int range, std::FILE* fp);
    bool CheckGlyphInfo(int nCode);

    int m_start;
    int m_range;
    FFontGlyphInfo* m_Info;
};

// Current L2Server 0x00737a30..0x00737d6c. The process-global instance is
// zero-initialized before Init(). Its inlined CRT destructor clears only the
// charset pointer and does not release the owned array.
class NFontCheck {
public:
    ~NFontCheck();

    bool Init();
    bool Load(const char16_t* filename);
    bool CheckCode(int code);

    std::int32_t m_nCharSet;
    NCharSet* m_pCharSet;
    std::int32_t m_FontWidth;
    std::int32_t m_FontHeight;
};

extern NFontCheck g_FontCheck;
