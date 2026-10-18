#include "WindowMgr.h"

#include "AuthGateConfig.h"
#include "AuthGateSvcMgr.h"
#include "ClientSocketContainer.h"
#include "Platform/LegacyWideFormat.h"
#include "Platform/WindowRuntime.h"
#include "StateMonMgr.h"

#include <cstdarg>
#include <cstdint>

CWindowMgr* CWindowMgr::g_pWindowMgr = nullptr;

// AuthGateD.exe 0x0040AB40, WindowMgr.cpp.
CWindowMgr::CWindowMgr(
    HINSTANCE__* instance,
    const char16_t* name)
    : m_hInstance(instance),
      m_strName(name ? name : u"") {
    if (!g_pWindowMgr) g_pWindowMgr = this;
    authgated::platform::RegisterWindowRuntime(this);
}

// AuthGateD.exe 0x0040AAB0, WindowMgr.cpp.
CWindowMgr::~CWindowMgr() {
    authgated::platform::UnregisterWindowRuntime(this);
    g_pWindowMgr = nullptr;
}

// AuthGateD.exe 0x0040A560, WindowMgr.cpp.
bool CWindowMgr::Run() {
    m_MonWnd.Show();
    m_OutputWnd.Show();
    m_StatusBarWnd.Show();
    return true;
}

// AuthGateD.exe 0x0040A5A0, WindowMgr.cpp.
void CWindowMgr::MessageLoop() {
    authgated::platform::RunWindowMessageLoop(this);
}

// AuthGateD.exe 0x0040A600, WindowMgr.cpp.
void CWindowMgr::SetStatusBar(const char16_t* text) {
    m_StatusBarWnd.SetText(text);
}

// AuthGateD.exe 0x0040A610, WindowMgr.cpp.
void CWindowMgr::SetTimer(
    std::uint32_t id,
    int milliseconds) {
    authgated::platform::SetWindowTimer(
        this,
        id,
        static_cast<std::uint32_t>(milliseconds));
}

// AuthGateD.exe 0x0040A630, WindowMgr.cpp.
void CWindowMgr::KillTimer(std::uint32_t id) {
    authgated::platform::KillWindowTimer(this, id);
}

// AuthGateD.exe 0x0040A690, WindowMgr.cpp.
void CWindowMgr::AddLine(
    std::uint32_t color,
    const char16_t* format,
    ...) {
    std::u16string text;
    // Original order: va_list arguments, va_start(arguments, format)
    // std::u16string text
    va_list arguments;
    va_start(arguments, format);
    // Original: authgated::portable::FormatTargetWide(text, 0x3ff, format, arguments)
    try {
        authgated::portable::FormatTargetWide(text, 0x3ff, format, arguments);
    } catch (...) {
        va_end(arguments);
        throw;
    }
    va_end(arguments);
    m_OutputWnd.AddLine(color, text.c_str());
}

// AuthGateD.exe 0x0040A710, WindowMgr.cpp.
std::int32_t CWindowMgr::WinProc(
    HWND__* window,
    std::uint32_t message,
    std::uint32_t wParam,
    std::int32_t lParam) {
    if (window == nullptr || window == m_hWnd) {
        if (message == 2) {
            authgated::platform::RequestWindowQuit(this);
            return 0;
        }
        if (message == 5) {
            const int width = static_cast<int>(
                static_cast<std::uint32_t>(lParam) & 0xffffU);
            const int height = static_cast<int>(
                static_cast<std::uint32_t>(lParam) >> 16U);
            m_MonWnd.Move(0, 0, width, 0x14);
            m_OutputWnd.Move(0, 0x14, width, height - 0x28);
            m_StatusBarWnd.Move(0, height - 0x14, width, 0x14);
            return 0;
        }
        if (message == 0x0113) {
            switch (wParam) {
            case 0x65:
                m_MonWnd.SetText(
                    CStateMonMgr::g_pStateMonMgr->GetStatusSummary());
                break;
            case 0x66:
                CAuthGateSvcMgr::g_pAuthGateSvcMgr->Reconnect();
                break;
            case 0x67:
                CStateMonMgr::g_pStateMonMgr->MakeReport();
                break;
            case 0x68:
                CClientSocketContainer::g_pClientSocketContainer
                    ->CheckSocketTimeout(
                        config.sessionTimeout,
                        ST_CONNECT);
                break;
            case 0x69:
                CAuthGateSvcMgr::g_pAuthGateSvcMgr->ReconnectLogD();
                break;
            default:
                break;
            }
            return 0;
        }
        if (message == 0x10) {
            authgated::platform::RequestWindowQuit(this);
            return 1;
        }
    } else if (window == m_MonWnd.m_hWnd) {
        return m_MonWnd.WinProc(window, message, wParam, lParam);
    } else if (window == m_OutputWnd.m_hWnd) {
        return m_OutputWnd.WinProc(window, message, wParam, lParam);
    } else if (window == m_StatusBarWnd.m_hWnd) {
        return m_StatusBarWnd.WinProc(window, message, wParam, lParam);
    }
    return 0;
}

// AuthGateD.exe 0x0040A9B0, WindowMgr.cpp.
std::int32_t CWindowMgr::WindowProc(
    HWND__* window,
    std::uint32_t message,
    std::uint32_t wParam,
    std::int32_t lParam) {
    return g_pWindowMgr->WinProc(
        window,
        message,
        wParam,
        lParam);
}

// AuthGateD.exe 0x0040A9C0, WindowMgr.cpp.
bool CWindowMgr::Init() {
    m_hWnd = reinterpret_cast<HWND__*>(this);
    constexpr std::uint16_t portableClassAtom = 1;
    m_MonWnd.Init(m_hInstance, portableClassAtom, m_hWnd);
    m_OutputWnd.Init(m_hInstance, portableClassAtom, m_hWnd);
    m_StatusBarWnd.Init(m_hInstance, portableClassAtom, m_hWnd);
    return true;
}
