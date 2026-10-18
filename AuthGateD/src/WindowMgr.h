#pragma once

#include "MonWnd.h"
#include "OutputWnd.h"
#include "StatusBarWnd.h"

#include <cstdint>
#include <string>

// Original owner: WindowMgr.h/WindowMgr.cpp. AuthGateD.pdb type 0x40920.
// The first five PDB-recorded DWORDs match to the shipping timer IDs
// 0x65..0x69 used by WinProc.
class CWindowMgr {
public:
    CWindowMgr(
        HINSTANCE__* instance,
        const char16_t* name);
    ~CWindowMgr();

    bool Run();
    void MessageLoop();
    void SetStatusBar(const char16_t* text);
    void SetTimer(std::uint32_t id, int milliseconds);
    void KillTimer(std::uint32_t id);
    void AddLine(
        std::uint32_t color,
        const char16_t* format,
        ...);
    std::int32_t WinProc(
        HWND__* window,
        std::uint32_t message,
        std::uint32_t wParam,
        std::int32_t lParam);
    static std::int32_t WindowProc(
        HWND__* window,
        std::uint32_t message,
        std::uint32_t wParam,
        std::int32_t lParam);
    bool Init();

    static CWindowMgr* g_pWindowMgr;

private:
    [[maybe_unused]] std::uint32_t nTIMER_MONITOR_WINDOW_UPDATE = 0x65;
    [[maybe_unused]] std::uint32_t nTIMER_TRY_RECONNECT = 0x66;
    [[maybe_unused]] std::uint32_t nTIMER_GENERATE_REPORT = 0x67;
    [[maybe_unused]] std::uint32_t nTIMER_CHECK_SOCKET_TIMEOUT = 0x68;
    [[maybe_unused]] std::uint32_t nTIMER_TRY_LOGD_RECONNECT = 0x69;
    HINSTANCE__* m_hInstance = nullptr;
    std::u16string m_strName;
    HWND__* m_hWnd = nullptr;
    CMonWnd m_MonWnd;
    COutputWnd m_OutputWnd;
    CStatusBarWnd m_StatusBarWnd;
};
