#include "Shared/src/StatusWnd.h"

#include "L2Core/src/inc/Guard.h"
#include "TextRuntime.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <string_view>

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#endif

namespace {

#if defined(L2_BUILD_CACHED)
constexpr int kStatusWindowHeight = 0xb4;
#else
constexpr int kStatusWindowHeight = 0x10e;
#endif

} // namespace

bool g_bShowStatusWindow = true;

// Current L2Server.exe 0x20d86520 and L2NPC.exe 0x028133e8. The tree head is
// at +0x08 and its element count at +0x10. The complete current MSVC
// std::map object is 0x20 bytes.
CStatusWnd::StatusWindowMap CStatusWnd::s_mapStatusWnd;

const wchar_t* CStatusWnd::STATUS_WND_TITLE = L"status_wnd_title";
const wchar_t* CStatusWnd::STATUS_WND_CLASS = L"status_wnd_class";

CStatusWnd::CStatusWnd() {
    // Current L2NPC.exe 0x005386b8.
    L2_GUARD_FUNCTION();
    m_hWnd = nullptr;
    m_bSetParam = false;
    m_hFont = nullptr;
    m_hBrush = nullptr;
    m_nCharHeight = 0;
    std::memset(m_sMessages, 0, sizeof(m_sMessages));
    m_FontSize = FontSize{0, 0};
}

CStatusWnd::~CStatusWnd() {
    // Current L2NPC.exe 0x00538784 only clears these handles.
    L2_GUARD_FUNCTION();
    m_hWnd = nullptr;
    m_hFont = nullptr;
    m_hBrush = nullptr;
}

void CStatusWnd::SetMessageV(int line, const wchar_t* format, ...) {
    // Current CacheD.exe 0x00597fe8 accepts ten rows. Current L2NPC.exe
    // 0x00538824 and L2Server.exe 0x00adc530 accept sixteen. std::size binds
    // this body to the process-specific physical layout from statuswnd.h.
    L2_GUARD_FUNCTION();
    if (line < 0 || static_cast<std::size_t>(line) >= std::size(m_sMessages)) {
        return;
    }

#if !defined(_WIN32)
    // Translate before va_start so allocation failure cannot bypass va_end.
    const std::wstring translated = WindowsWideFormatToHost(format);
#endif
    va_list arguments;
    va_start(arguments, format);
#if defined(_WIN32)
    ::wvsprintfW(reinterpret_cast<wchar_t*>(m_sMessages[line]), format, arguments);
    va_end(arguments);
#else
    // Original (after va_start): const std::wstring translated =
    //     WindowsWideFormatToHost(format)
    std::array<wchar_t, 257> formatted{};
    std::vswprintf(formatted.data(), formatted.size(), translated.c_str(), arguments);
    // End argument traversal before the allocating UTF-16 conversion.
    va_end(arguments);

    const std::u16string utf16 = WideToUtf16(std::wstring_view(formatted.data()));
    char16_t* const destination = reinterpret_cast<char16_t*>(m_sMessages[line]);
    const std::size_t length = std::min<std::size_t>(utf16.size(), 256);
    std::copy_n(utf16.data(), length, destination);
    destination[length] = u'\0';
#endif
    // Original: va_end(arguments). Now performed in each platform branch.
}

void CStatusWnd::OnMainWndResized(std::uint64_t wParam, std::int64_t lParam) {
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    if (m_hWnd &&
        (static_cast<std::uint32_t>(wParam) == SIZE_MAXIMIZED || static_cast<std::uint32_t>(wParam) == SIZE_RESTORED)) {
        const int width = static_cast<int>(static_cast<std::uint64_t>(lParam) & 0xffffU);
        ::MoveWindow(static_cast<HWND>(m_hWnd), width - 0x168, 0, 0x168, kStatusWindowHeight, TRUE);
    }
#else
#endif
}

void CStatusWnd::OnStatusWndPaint(HDC__* deviceContext) {
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    HDC const dc = static_cast<HDC>(deviceContext);

    if (!m_bSetParam) {
        m_hFont = reinterpret_cast<HFONT__*>(::GetStockObject(SYSTEM_FIXED_FONT));
        ::SelectObject(dc, reinterpret_cast<HGDIOBJ>(m_hFont));
        m_hBrush = reinterpret_cast<HBRUSH__*>(::GetStockObject(WHITE_BRUSH));
        ::GetTextExtentPoint32W(dc, L"H", 1, reinterpret_cast<SIZE*>(&m_FontSize));
        TEXTMETRICW metrics;
        ::GetTextMetricsW(dc, &metrics);
        m_bSetParam = true;
        m_nCharHeight = metrics.tmHeight + metrics.tmExternalLeading;
    } else {
        ::SelectObject(dc, reinterpret_cast<HGDIOBJ>(m_hFont));
    }

    RECT client;
    ::GetClientRect(static_cast<HWND>(m_hWnd), &client);
    ::FillRect(dc, &client, reinterpret_cast<HBRUSH>(m_hBrush));
    for (std::size_t line = 0; line < std::size(m_sMessages); ++line) {
        const wchar_t* const text = reinterpret_cast<const wchar_t*>(m_sMessages[line]);
        ::TextOutW(dc, 10, m_nCharHeight * static_cast<int>(line) + 10, text, static_cast<int>(std::wcslen(text)));
    }
#else
#endif
}

void CStatusWnd::Show() {
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    ::ShowWindow(static_cast<HWND>(m_hWnd), SW_SHOW);
#endif
}

void CStatusWnd::Hide() {
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    ::ShowWindow(static_cast<HWND>(m_hWnd), SW_HIDE);
#endif
}

std::int64_t CStatusWnd::StatusWndProc(HWND__* window, std::uint32_t message, std::uint64_t wParam, std::int64_t lParam) {
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    CStatusWnd* statusWindow = nullptr;
    const auto found = s_mapStatusWnd.find(window);
    if (found != s_mapStatusWnd.end()) {
        statusWindow = found->second;
    }

    if (message == WM_DESTROY) {
        ::PostQuitMessage(0);
        return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        HDC const dc = ::BeginPaint(static_cast<HWND>(window), &paint);
        if (statusWindow) {
            statusWindow->OnStatusWndPaint(static_cast<HDC__*>(dc));
        }
        ::EndPaint(static_cast<HWND>(window), &paint);
        return 0;
    }
    if (message == WM_TIMER) {
        if (statusWindow) {
            statusWindow->OnRefresh();
        }
        ::InvalidateRect(static_cast<HWND>(window), nullptr, TRUE);
        return 0;
    }
    return static_cast<std::int64_t>(
        ::DefWindowProcW(static_cast<HWND>(window), message, static_cast<WPARAM>(wParam), static_cast<LPARAM>(lParam))
    );
#else
    return 0;
#endif
}

void CStatusWnd::RegisterWindowClass(HINSTANCE__* instance) {
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = reinterpret_cast<WNDPROC>(&CStatusWnd::StatusWndProc);
    windowClass.hInstance = static_cast<HINSTANCE>(instance);
    windowClass.lpszClassName = STATUS_WND_CLASS;
    ::RegisterClassExW(&windowClass);
#else
#endif
}

void CStatusWnd::AttachToMainWnd(HINSTANCE__* instance, HWND__* mainWindow, int showCommand) {
    L2_GUARD_FUNCTION();
    RegisterWindowClass(instance);
#if defined(_WIN32)
    RECT client;
    ::GetClientRect(static_cast<HWND>(mainWindow), &client);
    m_hWnd = static_cast<HWND__*>(::CreateWindowExW(
        0, STATUS_WND_CLASS, STATUS_WND_TITLE, WS_CHILD | WS_BORDER, client.right - client.left - 0x168, 0, 0x168,
        kStatusWindowHeight, static_cast<HWND>(mainWindow), nullptr, static_cast<HINSTANCE>(instance), nullptr
    ));
    s_mapStatusWnd[m_hWnd] = this;
    ::ShowWindow(static_cast<HWND>(m_hWnd), showCommand);
    ::UpdateWindow(static_cast<HWND>(m_hWnd));
    ::SetTimer(static_cast<HWND>(m_hWnd), 0, 1000, nullptr);
#else
    m_hWnd = mainWindow;
    s_mapStatusWnd[m_hWnd] = this;
#endif
}
