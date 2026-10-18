#pragma once

#include "WindowTypes.h"

#include <cstdint>
#include <string>

// Original owner: StatusBarWnd.h/StatusBarWnd.cpp. PDB sizeof 0x3c.
class CStatusBarWnd {
public:
    CStatusBarWnd();
    ~CStatusBarWnd();

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
    void SetText(const char16_t* text);

private:
    friend class CWindowMgr;

    HWND__* m_hWnd = nullptr;
    HINSTANCE__* m_hInstance = nullptr;
    WindowRect32 m_clientRect;
    HFONT__* m_hFont = nullptr;
    HBRUSH__* m_hBrush = nullptr;
    std::u16string m_strText;
};
