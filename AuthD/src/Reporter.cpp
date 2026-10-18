#include "Reporter.h"

#include "DBConn.h"
#include "IOServerEx.h"
#include "Thread.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

CReporter reporter;

namespace {

void ReadLocalSystemTime(
    ReporterSystemTime32& destination) {
#if defined(_WIN32)
    ::GetLocalTime(
        reinterpret_cast<SYSTEMTIME*>(&destination));
#else
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds =
        std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_r(&seconds, &local);
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) %
        std::chrono::seconds(1);
    destination.year =
        static_cast<std::uint16_t>(local.tm_year + 1900);
    destination.month =
        static_cast<std::uint16_t>(local.tm_mon + 1);
    destination.dayOfWeek =
        static_cast<std::uint16_t>(local.tm_wday);
    destination.day =
        static_cast<std::uint16_t>(local.tm_mday);
    destination.hour =
        static_cast<std::uint16_t>(local.tm_hour);
    destination.minute =
        static_cast<std::uint16_t>(local.tm_min);
    destination.second =
        static_cast<std::uint16_t>(local.tm_sec);
    destination.milliseconds =
        static_cast<std::uint16_t>(milliseconds.count());
#endif
}

void InvalidateReporterWindow(HWND__* window) {
#if defined(_WIN32)
    if (window == nullptr) return;
    ::InvalidateRect(
        reinterpret_cast<HWND>(window),
        nullptr,
        FALSE);
#else
    [[maybe_unused]] HWND__* portableWindow = window;
#endif
}

} // namespace

// L2Authd.exe 0x0047B200, Src/reporter.cpp:16-25.
CReporter::CReporter() {
    m_font = nullptr;
    ReadLocalSystemTime(m_st);
    m_UserCount = 0;
    m_SocketCount = 0;
    m_SocketEXObjectCount = 0;
    m_InGameUser = 0;
    m_WebLoginTry = 0;
    m_ClientLoginTry = 0;
}

// L2Authd.exe 0x0047B270, Src/reporter.cpp:28-29.
CReporter::~CReporter() = default;

// L2Authd.exe 0x0047B290, Src/reporter.cpp:32-34.
void CReporter::SetWnd(HWND__* window) {
    m_wnd = window;
}

// L2Authd.exe 0x0047B2B0, Src/reporter.cpp:37-39.
HWND__* CReporter::GetWnd() {
    return m_wnd;
}

// L2Authd.exe 0x0047B2D0, Src/reporter.cpp:48-68. GUI painting is an explicit
// platform boundary. The status values and exact format remain product code.
void CReporter::Redraw() {
#if defined(_WIN32)
    PAINTSTRUCT painter{};
    const HWND window = reinterpret_cast<HWND>(m_wnd);
    HDC device = ::BeginPaint(window, &painter);
    if (m_font == nullptr) {
        m_font = reinterpret_cast<HFONT__*>(
            ::GetStockObject(SYSTEM_FIXED_FONT));
        ::SelectObject(
            device,
            reinterpret_cast<HFONT>(m_font));

        RECT rect{};
        ::GetClientRect(window, &rect);
        m_clientRect = {
            rect.left,
            rect.top,
            rect.right,
            rect.bottom};
        m_brush = reinterpret_cast<HBRUSH__*>(
            ::GetStockObject(WHITE_BRUSH));

        SIZE size{};
        ::GetTextExtentPoint32A(device, "H", 1, &size);
        m_fontSize = {size.cx, size.cy};
    } else {
        ::SelectObject(
            device,
            reinterpret_cast<HFONT>(m_font));
    }

    RECT rect{
        m_clientRect.left,
        m_clientRect.top,
        m_clientRect.right,
        m_clientRect.bottom};
    ::FillRect(
        device,
        &rect,
        reinterpret_cast<HBRUSH>(m_brush));

    std::array<char, 128> text{};
    std::sprintf(
        text.data(),
        " user %-6d  | socket %-4d | AcceptCall %-3d  | "
        "RunningThread %-3d | ActiveConn %-3d",
        static_cast<int>(m_UserCount),
        static_cast<int>(m_SocketCount),
        static_cast<int>(g_AcceptExThread),
        static_cast<int>(g_nRunningThread),
        static_cast<int>(g_linDB.GetActiveConn()));
    ::SetTextColor(device, 0);
    ::TextOutA(
        device,
        0,
        0,
        text.data(),
        static_cast<int>(std::strlen(text.data())));
    ::EndPaint(window, &painter);
#endif
}

// L2Authd.exe 0x0047B460, Src/reporter.cpp:71-75.
void CReporter::Resize(
    int right,
    int bottom) {
    m_clientRect.right = right;
    m_clientRect.bottom = bottom;
    InvalidateReporterWindow(m_wnd);
}

// L2Authd.exe 0x0047B490, Src/reporter.cpp:42-45.
void CReporter::Reset() {
    ReadLocalSystemTime(m_st);
    InvalidateReporterWindow(m_wnd);
}
