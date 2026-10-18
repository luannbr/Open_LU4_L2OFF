#pragma once

#include "Lock.h"
#include "WindowTypes.h"

#include <array>
#include <cstdint>

// AuthGateD.pdb type OutputLine, sizeof 0x408.
struct OutputLine {
    std::uint32_t color = 0;
    std::array<char16_t, 512> str{};
    std::uint32_t size = 0;
};


// Original owner: OutputWnd.h/OutputWnd.cpp. PDB sizeof 0x40870.
class COutputWnd {
public:
    COutputWnd();
    ~COutputWnd();

    bool Init(
        HINSTANCE__* instance,
        std::uint16_t classAtom,
        HWND__* parent);
    void Show();
    void Move(int x, int y, int width, int height);
    std::int32_t WinProc(
        HWND__* window,
        std::uint32_t message,
        std::uint32_t wParam,
        std::int32_t lParam);
    void AddLine(
        std::uint32_t color,
        const char16_t* text);

private:
    friend class CWindowMgr;

    HWND__* m_hWnd = nullptr;
    HINSTANCE__* m_hInstance = nullptr;
    WindowRect32 m_clientRect;
    HFONT__* m_hFont = nullptr;
    HBRUSH__* m_hBrush = nullptr;
    WindowSize32 m_fontSize;
    std::array<OutputLine, 256> m_lines{};
    std::int32_t m_iTail = 0;
    CLock m_lock;
};
