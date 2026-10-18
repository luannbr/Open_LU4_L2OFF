#include "StatusBarWnd.h"

// AuthGateD.exe 0x0040A350 and folded methods, StatusBarWnd.cpp.
CStatusBarWnd::CStatusBarWnd() = default;
CStatusBarWnd::~CStatusBarWnd() = default;

bool CStatusBarWnd::Init(
    HINSTANCE__* instance,
    [[maybe_unused]] std::uint16_t classAtom,
    [[maybe_unused]] HWND__* parent) {
    m_hInstance = instance;
    m_hWnd = reinterpret_cast<HWND__*>(this);
    return true;
}

void CStatusBarWnd::Show() {}

void CStatusBarWnd::Move(int x, int y, int width, int height) {
    m_clientRect = {x, y, x + width, y + height};
}

std::int32_t CStatusBarWnd::WinProc(
    [[maybe_unused]] HWND__* window,
    [[maybe_unused]] std::uint32_t message,
    [[maybe_unused]] std::uint32_t wParam,
    [[maybe_unused]] std::int32_t lParam) {
    return 0;
}

void CStatusBarWnd::SetText(const char16_t* text) {
    m_strText = text ? text : u"";
}
