#pragma once

#include <cstdint>

struct HWND__;
struct HFONT__;
struct HBRUSH__;

// Fixed-width Win32 value records used by Src/reporter.h. Their target
// sizes are invariant even when opaque GDI handles use the portable host
// pointer width.
struct ReporterRect32 {
    std::int32_t left;
    std::int32_t top;
    std::int32_t right;
    std::int32_t bottom;
};

struct ReporterSize32 {
    std::int32_t cx;
    std::int32_t cy;
};

struct ReporterSystemTime32 {
    std::uint16_t year;
    std::uint16_t month;
    std::uint16_t dayOfWeek;
    std::uint16_t day;
    std::uint16_t hour;
    std::uint16_t minute;
    std::uint16_t second;
    std::uint16_t milliseconds;
};

// Original owner: Src/reporter.h. L2Authd.pdb type 0x7B1E, target size
// 0x50. Access and declaration order follow field list 0x7B1D exactly.
class CReporter {
public:
    CReporter();
    virtual ~CReporter();

    std::int32_t m_InGameUser;
    std::int32_t m_UserCount;
    std::int32_t m_SocketCount;
    std::int32_t m_SocketEXObjectCount;
    std::int32_t m_WebLoginTry;
    std::int32_t m_ClientLoginTry;

    void SetWnd(HWND__* window);
    HWND__* GetWnd();
    void Redraw();
    void Resize(int right, int bottom);
    void Reset();

protected:
    HWND__* m_wnd;
    HFONT__* m_font;
    ReporterRect32 m_clientRect;
    HBRUSH__* m_brush;
    ReporterSize32 m_fontSize;
    ReporterSystemTime32 m_st;
};

extern CReporter reporter;
