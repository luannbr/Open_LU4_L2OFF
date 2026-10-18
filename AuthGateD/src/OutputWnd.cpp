#include "OutputWnd.h"

#include "Shared/src/ScopedLockGuards.h"

#include <algorithm>

// AuthGateD.exe 0x00409F30, OutputWnd.cpp.
COutputWnd::COutputWnd()
    : m_lock(u"COutputWnd::m_lock", true) {}

// AuthGateD.exe 0x00409F80, OutputWnd.cpp.
COutputWnd::~COutputWnd() = default;

// AuthGateD.exe 0x00409C80, OutputWnd.cpp.
bool COutputWnd::Init(
    HINSTANCE__* instance,
    [[maybe_unused]] std::uint16_t classAtom,
    [[maybe_unused]] HWND__* parent) {
    m_hInstance = instance;
    m_hWnd = reinterpret_cast<HWND__*>(this);
    return true;
}

// AuthGateD.exe 0x00409CD0, folded UI method.
void COutputWnd::Show() {}

// AuthGateD.exe 0x00409CF0, folded UI method.
void COutputWnd::Move(int x, int y, int width, int height) {
    m_clientRect = {x, y, x + width, y + height};
}

// AuthGateD.exe 0x00409F90. Painting is the OS transport. The kept ring
// below is the product state consumed by that paint path.
std::int32_t COutputWnd::WinProc(
    [[maybe_unused]] HWND__* window,
    [[maybe_unused]] std::uint32_t message,
    [[maybe_unused]] std::uint32_t wParam,
    [[maybe_unused]] std::int32_t lParam) {
    return 0;
}

// AuthGateD.exe 0x0040A100, OutputWnd.cpp.
void COutputWnd::AddLine(
    std::uint32_t color,
    const char16_t* text) {
    {
        L2_GUARD(m_lock);
        OutputLine& line = m_lines[static_cast<std::size_t>(m_iTail)];
        line.color = color;
        const char16_t* source = text ? text : u"";
        std::size_t count = 0;
        while (count + 1U < line.str.size() && source[count] != u'\0') {
            line.str[count] = source[count];
            ++count;
        }
        line.str[count] = u'\0';
        std::fill(
            line.str.begin() + static_cast<std::ptrdiff_t>(count + 1U),
            line.str.end(),
            u'\0');
        line.size = static_cast<std::uint32_t>(count);
        m_iTail = (m_iTail + 1) & 0xff;
    }
}
