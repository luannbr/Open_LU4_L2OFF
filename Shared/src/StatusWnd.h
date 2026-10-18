#pragma once

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

struct HDC__;
struct HBRUSH__;
struct HFONT__;
struct HINSTANCE__;
struct HWND__;


// Exact PDB global owned by StatusWnd.obj (section contribution module 31).
// Current L2NPC.exe 0x006ba120 initializes it to TRUE and WndProc toggles it.
extern bool g_bShowStatusWindow;

// Shared/statuswnd.h. The physical header is shared, but the current CacheD
// build instantiates ten rows while current L2NPC/L2Server instantiate
// sixteen. That product macro changes the target object size as well as the
// SetMessageV range check.
class CStatusWnd {
public:
    CStatusWnd();
    virtual ~CStatusWnd();

    void SetMessageV(int line, const wchar_t* format, ...);

    void RegisterWindowClass(HINSTANCE__* instance);
    void AttachToMainWnd(HINSTANCE__* instance, HWND__* mainWindow, int showCommand);
    void OnMainWndResized(std::uint64_t wParam, std::int64_t lParam);
    void OnStatusWndPaint(HDC__* deviceContext);
    virtual void OnRefresh() = 0;
    void Show();
    void Hide();

private:
#if defined(L2_BUILD_CACHED)
    static constexpr std::size_t kMessageLineCount = 10;
#else
    static constexpr std::size_t kMessageLineCount = 16;
#endif

    using MessageChar = std::conditional_t<sizeof(wchar_t) == 2, wchar_t, char16_t>;

    struct FontSize {
        std::int32_t cx;
        std::int32_t cy;
    };

    static std::int64_t StatusWndProc(HWND__* window, std::uint32_t message, std::uint64_t wParam, std::int64_t lParam);


    HWND__* m_hWnd; // +0x0008
    bool m_bSetParam; // +0x0010
    HFONT__* m_hFont; // +0x0018
    HBRUSH__* m_hBrush; // +0x0020
    FontSize m_FontSize; // +0x0028
    std::int32_t m_nCharHeight; // +0x0030
    MessageChar m_sMessages[kMessageLineCount][257]; // +0x0034

    using StatusWindowMap = std::map<HWND__*, CStatusWnd*>;

    static StatusWindowMap s_mapStatusWnd;
    static const wchar_t* STATUS_WND_TITLE;
    static const wchar_t* STATUS_WND_CLASS;
};
