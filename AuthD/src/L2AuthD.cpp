#include "Shared/src/Runtime/ConfigDirectory.h"
#include "Shared/src/Runtime/ConsoleSignals.h"
#include "Shared/src/Runtime/ConsoleInput.h"
#include "L2AuthD.h"

#include "Platform/CurrentL2ServerProfile.h"
#include "Platform/IOSocketTransport.h"
#include "Shared/src/Runtime/NativeSocketError.h"
#include "Shared/src/Runtime/OperatorLog.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/SystemMailTransport.h"
#include "AccountContainer.h"
#include "AccountDB.h"
#include "BuildN.h"
#include "ClientSession.h"
#include "Config.h"
#include "ConsoleCommands.h"
#include "ConnectedIPList.h"
#include "DBConn.h"
#include "DES.h"
#include "Exception.h"
#include "GPProxyServerMgr.h"
#include "GradeServerMgr.h"
#include "IFatigue.h"
#include "include/CDes.h"
#include "IovationServerMgr.h"
#include "IPSessionDB.h"
#include "Job.h"
#include "Log.h"
#include "LogSocket.h"
#include "MailReport.h"
#include "PAServerMgr.h"
#include "PBServerMgr.h"
#include "Reporter.h"
#include "SendMail.h"
#include "ServerList.h"
#include "ServiceMgr.h"
#include "SocketServerEx.h"
#include "Thread.h"
#include "TokenServerMgr.h"
#include "Util.h"
#include "Lu4Validator.h"
#include "WhiteListIPMgr.h"
#include "WSMServerMgr.h"
#include "ZoneFatigue.h"

#include <array>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#  include <process.h>
#  include <Windows.h>
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>
#  include <strings.h>
#else
#  include <strings.h>
#  include <unistd.h>
#endif

HWND__* mainWnd = nullptr;
HWND__* logWnd = nullptr;
HWND__* reporterWnd = nullptr;
HWND__* buttonWnd = nullptr;
HWND__* reloadButtonWnd = nullptr;
HINSTANCE__* g_instance = nullptr;
HWND__* ShutdownWnd = nullptr;
bool globalTeminateEvent = false;
bool g_bServiceStop = false;
EncPwdType EncPwd = nullptr;

// The initializer order follows the four selected .CRT entries: DesObject,
// reportMail, g_serviceManager, then the file-local Winsock initializer.
CDes DesObject;
CMailReport reportMail;
CServiceManager g_serviceManager;

int g_updateKey = 0;
int g_updateKey2 = 0;

#if defined(_WIN32)
LRESULT CALLBACK WindowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam);
INT_PTR CALLBACK ShutDownDlgProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam);
DWORD WINAPI service_handler(
    DWORD control,
    DWORD eventType,
    LPVOID eventData,
    LPVOID context);
#endif

namespace {

CWsaInitializer s_wsaInitializer;

using StartupFunction = Result (*)(
    HINSTANCE__*,
    const char*);
using ServiceStartupFunction = Result (*)();

bool legacyDiskTimerEnabled = false;

constexpr std::uint64_t kRequiredLogFreeSpace =
    UINT64_C(400000000);

// Exact CP949 byte strings at L2Authd.exe 0x004FD080/0x004FD108.
// constexpr char kAcceptCallCountWarning[] =
[[maybe_unused]] constexpr char kAcceptCallCountWarning[] =
    "AcceptCallNull \xC0\xCC 0 \xC0\xCC\xB8\xE9 "
    "\xBE\xEE\xB6\xB2 \xC5\xAC\xB6\xF3\xC0\xCC\xBE\xF0\xC6\xAE\xB5\xB5 "
    "\xC1\xA2\xBC\xD3\xC0\xCC \xBE\xC8\xB5\xCB\xB4\xCF\xB4\xD9. 1 "
    "\xB7\xCE \xC0\xDA\xB5\xBF\xBC\xBC\xC6\xC3\xC7\xD5\xB4\xCF\xB4\xD9.";
// constexpr char kSocketTimeoutWarning[] =
[[maybe_unused]] constexpr char kSocketTimeoutWarning[] =
    "SocketTimeOut\xC0\xCC 0\xC0\xCC\xB8\xE9 Connection\xC0\xCC "
    "\xB9\xD9\xB7\xCE \xB2\xF7\xB1\xE2\xB0\xD4 "
    "\xB5\xCB\xB4\xCF\xB4\xD9. 180\xC0\xB8\xB7\xCE "
    "\xC0\xDA\xB5\xBF\xBC\xBC\xC6\xC3\xC7\xD5\xB4\xCF\xB4\xD9. ";

void SleepMilliseconds(unsigned int milliseconds) {
    std::this_thread::sleep_for(
        std::chrono::milliseconds(milliseconds));
}

// void ReportStartupError(const char* message) {
// #if defined(_WIN32)
//     ::MessageBoxA(
//         nullptr,
//         message,
//         "ERROR",
//         MB_ICONERROR)
// #else
//     std::fprintf(stderr, "%s\n", message)
// #endif
// }
void ReportStartupError(
    const char* message,
    const char* errorDomain = "none",
    std::int64_t code = 0,
    [[maybe_unused]] bool showDialog = true) {
    // The diagnostic directory is not usable yet. This console path must work
    // before CLog is enabled and must not depend on its file destination.
    const int savedErrno = errno;
#if defined(_WIN32)
    const DWORD savedLastError = ::GetLastError();
#endif
    std::array<char, 2048> diagnostic{};
    // std::snprintf(diagnostic.data(), diagnostic.size(),
    //     "component=startup event=log_init_failed key=logDirectory path_preview=\"%.512s\" detail_preview=\"%.256s\" error_domain=%s code=%lld action=abort_startup phase=before_log_setup",
    //     config.logDirectory == nullptr ? "<unset>" : config.logDirectory,
    //     message, errorDomain, static_cast<long long>(code))
    std::snprintf(diagnostic.data(), diagnostic.size(),
        "[startup] Cannot initialize logs in \"%.512s\": %.256s (%s error %lld). Startup aborted before log setup.",
        config.logDirectory == nullptr ? "<unset>" : config.logDirectory,
        message, errorDomain, static_cast<long long>(code));
    // runtime::logging::Write("authd", runtime::logging::Level::Error,
    //     diagnostic.data())
    runtime::logging::Write("authd", runtime::logging::Level::Error,
        diagnostic.data(), __FILE__);
    errno = savedErrno;
#if defined(_WIN32)
    ::SetLastError(savedLastError);
    if (showDialog && config.userInterface == runtime::UserInterface::Gui) {
        ::MessageBoxA(
            nullptr,
            message,
            "ERROR",
            MB_ICONERROR);
    }
#endif
}

#if !defined(_WIN32)
std::filesystem::path ExecutablePath() {
#if defined(__APPLE__)
    std::array<char, 4096> path{};
    std::uint32_t pathSize =
        static_cast<std::uint32_t>(path.size());
    if (_NSGetExecutablePath(
            path.data(),
            &pathSize) != 0) {
        return {};
    }
    return std::filesystem::path(path.data());
#else
    std::array<char, 4096> path{};
    const auto length = ::readlink(
        "/proc/self/exe",
        path.data(),
        path.size() - 1);
    if (length < 0) {
        return {};
    }
    path[static_cast<std::size_t>(length)] = '\0';
    return std::filesystem::path(path.data());
#endif
}
#endif

void SendStartedReport() {
    if (!config.useReportMail) {
        return;
    }
    CMailReport::Instance()->AddAttachment(
        runtime::ConfigFile("etc/config.txt", "config.txt").string().c_str());
    if (config.gameId == 8) {
        CMailReport::Instance()->SendMail(
            "L2AuthD Started",
            "L2AuthD Started Successfully.");
    } else {
        CMailReport::Instance()->SendMail(
            "AionAuthD Started",
            "AionAuthD Started Successfully.");
    }
}

void SendShutdownReport(bool addConfigAttachment) {
    if (!config.useReportMail) {
        return;
    }
    if (addConfigAttachment) {
        CMailReport::Instance()->AddAttachment(
            runtime::ConfigFile("etc/config.txt", "config.txt").string().c_str());
    }
    if (config.gameId == 8) {
        CMailReport::Instance()->SendMail(
            "L2AuthD Shutdown",
            "L2AuthD shutdown completed.");
    } else {
        CMailReport::Instance()->SendMail(
            "AionAuthD Shutdown",
            "AionAuthD shutdown completed.");
    }
}

void RequestForegroundShutdown() {
    runtime::raw_atomic::StoreBool(
        &g_bTerminating,
        true);
    job.SetTerminate();
    SendShutdownReport(false);
    SleepMilliseconds(2000);
}

void UpdateReporterUserCount() {
    reporter.m_UserCount = accountdb.GetUserNum();
#if defined(_WIN32)
    if (reporterWnd != nullptr) {
        ::InvalidateRect(reinterpret_cast<HWND>(reporterWnd), nullptr, FALSE);
    }
#endif
}

void SendWorldStatusHeartbeat() {
    for (int serverId = 1; serverId < 128; ++serverId) {
        if (!GetServerStatus(
                static_cast<char>(serverId))) {
            continue;
        }
        const WorldServer world =
            g_ServerList.GetAt(serverId);
        SendSocket(world.inner_addr, "c", 2);
    }
}

void CheckConfiguredLogDiskSpace() {
    ULARGE_INTEGER required{};
    required.QuadPart = kRequiredLogFreeSpace;
    CheckDiskSpace(config.logDirectory, required);
}

// L2Authd.exe 0x00463830, Src/L2AuthD.cpp:77-95.
Result InitializeCurrentDirectory(
    [[maybe_unused]] HINSTANCE__* instance,
    [[maybe_unused]] const char* commandLine) {
#if defined(_WIN32)
    std::array<char, MAX_PATH> modulePath{};
    ::GetModuleFileNameA(
        nullptr,
        modulePath.data(),
        static_cast<DWORD>(modulePath.size()));

    std::array<char, _MAX_DRIVE> drive{};
    std::array<char, _MAX_DIR> directory{};
    std::array<char, _MAX_FNAME> filename{};
    std::array<char, _MAX_EXT> extension{};
    ::_splitpath_s(
        modulePath.data(),
        drive.data(),
        drive.size(),
        directory.data(),
        directory.size(),
        filename.data(),
        filename.size(),
        extension.data(),
        extension.size());
    std::array<char, MAX_PATH> currentDirectory{};
    ::GetCurrentDirectoryA(
        static_cast<DWORD>(currentDirectory.size()),
        currentDirectory.data());
    std::snprintf(
        currentDirectory.data(),
        currentDirectory.size(),
        "%s%s",
        drive.data(),
        directory.data());
    ::SetCurrentDirectoryA(currentDirectory.data());
#else
    std::error_code error;
    std::filesystem::path executable = ExecutablePath();
    if (!executable.empty()) {
        executable = std::filesystem::weakly_canonical(
            executable,
            error);
        if (!error && executable.has_parent_path()) {
            std::filesystem::current_path(
                executable.parent_path(),
                error);
        }
    }
#endif
    return Continue;
}

// L2Authd.exe 0x00463980, Src/L2AuthD.cpp:98-101.
Result InitializeConfig(
    [[maybe_unused]] HINSTANCE__* instance,
    [[maybe_unused]] const char* commandLine) {
    config.Load(runtime::ConfigFile("etc/config.txt", "config.txt").string().c_str());
#if defined(_WIN32)
    if (config.userInterface == runtime::UserInterface::Gui) {
        ::FreeConsole();
    }
#endif
    authd::compatibility::ConfigureCurrentL2ServerProfile(
        config.GetBool("CurrentL2ServerCompatibility", false));
    return Continue;
}

// L2Authd.exe 0x004639A0, Src/L2AuthD.cpp:104-149.
Result InitializeLog(
    [[maybe_unused]] HINSTANCE__* instance,
    [[maybe_unused]] const char* commandLine) {
    if (config.logDirectory == nullptr ||
        config.logDirectory[0] == '\0') {
        // if (g_serviceManager.IsServiceMode() == 0) {
        //     ReportStartupError(
        //         "'logDirectory' is not set in config.txt")
        // }
        ReportStartupError("'logDirectory' is not set in config.txt", "none", 0,
            g_serviceManager.IsServiceMode() == 0);
        return ErrorStop;
    }

#if defined(_WIN32)
    if (::CreateDirectoryA(
            config.logDirectory,
            nullptr) == FALSE &&
        ::GetLastError() != ERROR_ALREADY_EXISTS) {
        // ReportStartupError(
        //     "'logDirectory' is not found.")
        ReportStartupError("Cannot create log directory", "win32", ::GetLastError());
        return ErrorStop;
    }
#else
    std::error_code directoryError;
    std::filesystem::create_directory(
        config.logDirectory,
        directoryError);
    if (directoryError &&
        directoryError !=
            std::make_error_code(
                std::errc::file_exists)) {
        // ReportStartupError(
        //     "'logDirectory' is not found.")
        ReportStartupError("Cannot create log directory",
            directoryError.category().name(), directoryError.value());
        return ErrorStop;
    }
#endif

    Log.SetDirectory(config.logDirectory);
    Log.Enable(true);
    fileLog.SetDirectory(config.logDirectory);
    actionLog.SetDirectory(config.logDirectory);
    errLog.SetDirectory(config.logDirectory);
    logdfileLog.SetDirectory(config.logDirectory);
    billingLog.SetDirectory(config.logDirectory);
    if (config.usePacketLog != 0) {
        packetLog.SetDirectory(config.logDirectory);
    }
    if (config.useDualLog) {
        dualLog.SetDirectory(config.logDirectory);
    }
    blockLog.SetDirectory(config.logDirectory);
    if (config.useUserUseLog) {
        UserUseLog.SetDirectory(config.logDirectory);
    }
    return Continue;
}

#if defined(_WIN32)
void WINAPI ServiceMainThunk(
    DWORD argumentCount,
    LPSTR* arguments) {
    service_main(
        static_cast<int>(argumentCount),
        arguments);
}
#endif

// L2Authd.exe 0x00463B10, Src/L2AuthD.cpp:152-200.
Result InitializeWindowsServiceCtrlManager(
    HINSTANCE__* instance,
    const char* commandLine) {
    if (config.gameId == 8) {
        char serviceName[] = "L2AuthD";
        char displayName[] = "L2AuthD SERVICE";
        char description[] =
            "Lineage2 authentication service";
        g_serviceManager.ConfigServiceName(serviceName);
        g_serviceManager.ConfigServiceDisp(displayName);
        g_serviceManager.ConfigServiceDesc(description);
    } else {
        char serviceName[] = "AionAuthD";
        char displayName[] = "AionAuthD SERVICE";
        char description[] =
            "AION authentication service";
        g_serviceManager.ConfigServiceName(serviceName);
        g_serviceManager.ConfigServiceDisp(displayName);
        g_serviceManager.ConfigServiceDesc(description);
    }

    const char* const arguments =
        commandLine == nullptr ? "" : commandLine;
#if defined(_WIN32)
    const int installComparison =
        ::_stricmp(arguments, "/install");
#else
    const int installComparison =
        ::strcasecmp(arguments, "/install");
#endif
    if (installComparison == 0) {
        g_serviceManager.Install();
        return OkStop;
    }

#if defined(_WIN32)
    const int uninstallComparison =
        ::_stricmp(arguments, "/uninstall");
#else
    const int uninstallComparison =
        ::strcasecmp(arguments, "/uninstall");
#endif
    if (uninstallComparison == 0) {
        g_serviceManager.Uninstall(0, 30);
        return OkStop;
    }

    g_instance = instance;
#if defined(_WIN32)
    if (g_serviceManager.IsServiceMode() == 1) {
        g_bServiceStop = false;
        SERVICE_TABLE_ENTRYA dispatchTable[] = {
            {
                CServiceManager::m_InstallParam.
                    lpServiceName,
                ServiceMainThunk,
            },
            {nullptr, nullptr},
        };
        if (::StartServiceCtrlDispatcherA(
                dispatchTable) == FALSE) {
            L2_LOG_ERROR(Log,
                "[service] Could not connect the process to the Windows service dispatcher (Win32 error %lu). Startup aborted.",
                static_cast<unsigned long>(::GetLastError()));
            return ErrorStop;
        }
        return OkStop;
    }
#endif
    return Continue;
}

// L2Authd.exe 0x00463C30, Src/L2AuthD.cpp:203-257.
Result ValidateConfiguration() {
    const char* const processName =
        config.gameId == 8
            ? "L2AuthD"
            : "AionAuthD";
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "%s - Start : ServiceMode[%d]",
    //     processName,
    //     g_serviceManager.IsServiceMode())
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "component=startup event=process_start product=%s service_mode=%d",
    //     processName, g_serviceManager.IsServiceMode())
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "[startup] Starting %s (service mode: %d).",
    //     processName, g_serviceManager.IsServiceMode())
    L2_LOG_INFO(Log,
        "[startup] Starting %s (service mode: %d).",
        processName,
        g_serviceManager.IsServiceMode());
    // Log.AddLog(LOG_WARN, "LOADED Config")
    // Noise: This point does not prove the config file was read or validation completed. Config::Load reports its failure/default policy.
    // Log.AddLog(LOG_NORMAL, "component=config event=loaded")
    // Log.AddLog(
    //     LOG_WARN,
    //     "BuildNumber : %d",
    //     GetBuildNumber())
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "component=startup build=%d",
    //     GetBuildNumber())
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "component=startup event=build_info build=%d",
    //     GetBuildNumber())
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "[startup] Build number: %d.",
    //     GetBuildNumber())
    L2_LOG_INFO(Log,
        "[startup] Build number: %d.",
        GetBuildNumber());
    // Log.AddLog(
    //     LOG_WARN,
    //     "ProductVersion : %s",
    //     GetProductVersion())
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "component=startup version=%s",
    //     GetProductVersion())
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "component=startup event=version_info version=%s",
    //     GetProductVersion())
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "[startup] Product version: %s.",
    //     GetProductVersion())
    L2_LOG_INFO(Log,
        "[startup] Product version: %s.",
        GetProductVersion());

    if (config.AcceptCallNum == 0) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "%s",
        //     kAcceptCallCountWarning)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=config event=value_defaulted key=AcceptCallNum configured=0 applied=1 reason=accepts_would_be_disabled")
        // Log.AddLog(
        //     LOG_WARN,
        //     "[config] AcceptCallNum is zero, using 1 so connections can be accepted.")
        L2_LOG_WARN(Log,
            "[config] AcceptCallNum is zero; using 1 so connections can be accepted.");
        config.AcceptCallNum = 1;
    }
    if (config.SocketTimeOut == 0) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "%s",
        //     kSocketTimeoutWarning)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=config event=value_defaulted key=SocketTimeOut runtime_ms=0 applied_runtime_ms=180 config_unit=seconds action=use_legacy_fallback")
        // Log.AddLog(
        //     LOG_WARN,
        //     "[config] SocketTimeOut resolved to zero. Using the existing fallback of 180 ms, the configuration option is normally in seconds.")
        L2_LOG_WARN(Log,
            "[config] SocketTimeOut resolved to zero. Using the existing fallback of 180 ms; the configuration option is normally in seconds.");
        config.SocketTimeOut = 180;
    }
    if (config.useWSM &&
        config.useLightTokenServer) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Both useWSM and useLightTokenServer configs set true. Only one can be true!!")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=config event=conflicting_options keys=useWSM,useLightTokenServer configured=true,true action=abort_startup")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[config] useWSM and useLightTokenServer cannot both be enabled. Startup aborted.")
        L2_LOG_ERROR(Log,
            "[config] useWSM and useLightTokenServer cannot both be enabled. Startup aborted.");
        return ErrorStop;
    }
    return Continue;
}

// L2Authd.exe 0x00463D40, Src/L2AuthD.cpp:260-286.
Result InitializeServiceCtrlHandler() {
#if defined(_WIN32)
    if (g_serviceManager.IsServiceMode() == 0) {
        return Continue;
    }
    SERVICE_STATUS_HANDLE const handler =
        ::RegisterServiceCtrlHandlerExA(
            CServiceManager::m_InstallParam.
                lpServiceName,
            service_handler,
            nullptr);
    if (handler == nullptr) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "RegisterServiceCtrlHandlerEx failed")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=service event=handler_registration_failed error_domain=win32 code=%lu action=abort_startup",
        //     static_cast<unsigned long>(::GetLastError()))
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[service] Could not register the control handler (Win32 error %lu). Startup aborted.",
        //     static_cast<unsigned long>(::GetLastError()))
        L2_LOG_ERROR(Log,
            "[service] Could not register the control handler (Win32 error %lu). Startup aborted.",
            static_cast<unsigned long>(::GetLastError()));
        return ErrorStop;
    }
    g_serviceManager.SetServiceHandler(handler);
    if (g_serviceManager.GetServiceHandler() == nullptr) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "SetServiceHandler failed")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=service event=handler_storage_failed reason=stored_handle_is_null action=abort_startup")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[service] The stored control-handler handle is null. Startup aborted.")
        L2_LOG_ERROR(Log,
            "[service] The stored control-handler handle is null. Startup aborted.");
        return ErrorStop;
    }
    g_serviceManager.SetServiceRunStatus(2);
#endif
    return Continue;
}

// L2Authd.exe 0x00463DE0, Src/L2AuthD.cpp:289-300.
Result PreventRedundantRunningProcess() {
#if defined(_WIN32)
    if (g_serviceManager.IsServiceMode() == 0 &&
        ::FindWindowA(nullptr, "AuthServer") != nullptr) {
        L2_LOG_ERROR(Log,
            "[startup] Another AuthServer window already exists. Startup aborted.");
        if (config.userInterface == runtime::UserInterface::Gui) {
            ::MessageBoxA(nullptr, "AuthServer already running", "Warning", MB_ICONERROR);
        }
        return ErrorStop;
    }
#endif
    return Continue;
}

// L2Authd.exe 0x00463E30, Src/L2AuthD.cpp:303-387.
Result InitializeWindow() {
#if defined(_WIN32)
    if (config.userInterface != runtime::UserInterface::Gui ||
        g_serviceManager.IsServiceMode() == 1) {
        return Continue;
    }
    HINSTANCE const module = ::GetModuleHandleA(nullptr);
    WNDCLASSEXA windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = module;
    windowClass.hCursor = ::LoadCursorA(
        nullptr,
        IDC_ARROW);
    windowClass.lpszClassName = "AuthServer";
    const ATOM classAtom =
        ::RegisterClassExA(&windowClass);

    std::string gameName = "Unassigned";
    if (config.gameId == 8 ||
        config.gameId == 32) {
        gameName = "Lineage2";
    } else if (config.gameId == 27) {
        gameName = "Aion";
    }
    std::array<char, 200> title{};
    std::snprintf(
        title.data(),
        title.size(),
        "AuthServer for %s - %d [ProductVersion:%s]",
        gameName.c_str(),
        GetBuildNumber(),
        GetProductVersion());

    mainWnd = reinterpret_cast<HWND__*>(
        ::CreateWindowExA(
            0,
            MAKEINTATOM(classAtom),
            title.data(),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            0,
            600,
            440,
            nullptr,
            nullptr,
            module,
            nullptr));
    logWnd = reinterpret_cast<HWND__*>(
        ::CreateWindowExA(
            WS_EX_CLIENTEDGE,
            MAKEINTATOM(classAtom),
            "",
            WS_CHILD,
            0,
            30,
            640,
            720,
            reinterpret_cast<HWND>(mainWnd),
            nullptr,
            module,
            nullptr));
    reporterWnd = reinterpret_cast<HWND__*>(
        ::CreateWindowExA(
            WS_EX_CLIENTEDGE,
            MAKEINTATOM(classAtom),
            "",
            WS_CHILD,
            0,
            0,
            640,
            30,
            reinterpret_cast<HWND>(mainWnd),
            nullptr,
            module,
            nullptr));
    buttonWnd = reinterpret_cast<HWND__*>(
        ::CreateWindowExA(
            0,
            "BUTTON",
            "Reset",
            WS_CHILD,
            600,
            0,
            40,
            30,
            reinterpret_cast<HWND>(mainWnd),
            reinterpret_cast<HMENU>(
                static_cast<std::uintptr_t>(1)),
            module,
            nullptr));
    reloadButtonWnd = reinterpret_cast<HWND__*>(
        ::CreateWindowExA(
            0,
            "BUTTON",
            "Reload",
            WS_CHILD,
            600,
            0,
            40,
            30,
            reinterpret_cast<HWND>(mainWnd),
            reinterpret_cast<HMENU>(
                static_cast<std::uintptr_t>(2)),
            module,
            nullptr));
#endif

    Log.SetWnd(logWnd);
    reporter.SetWnd(reporterWnd);

#if defined(_WIN32)
    ::ShowWindow(reinterpret_cast<HWND>(mainWnd), 1);
    ::UpdateWindow(reinterpret_cast<HWND>(mainWnd));
    for (HWND__* window : {
             logWnd,
             reporterWnd,
             buttonWnd,
             reloadButtonWnd}) {
        ::ShowWindow(reinterpret_cast<HWND>(window), 5);
        ::UpdateWindow(reinterpret_cast<HWND>(window));
    }
    // ::SetTimer(reinterpret_cast<HWND>(mainWnd), 0x66, 2000, nullptr)
    if (::SetTimer(reinterpret_cast<HWND>(mainWnd), 0x66, 2000, nullptr) == 0) {
        L2_LOG_ERROR(Log,
            "[timer] Could not schedule reporter user-count update (timer 0x66, interval 2000 ms). Startup continues without this newly scheduled timer.");
    }
    // ::SetTimer(reinterpret_cast<HWND>(mainWnd), 0x67, 60000, nullptr)
    if (::SetTimer(reinterpret_cast<HWND>(mainWnd), 0x67, 60000, nullptr) == 0) {
        L2_LOG_ERROR(Log,
            "[timer] Could not schedule world-status heartbeat (timer 0x67, interval 60000 ms). Startup continues without this newly scheduled timer.");
    }
    // ::SetTimer(reinterpret_cast<HWND>(mainWnd), 0x6f, 300000, nullptr)
    if (::SetTimer(reinterpret_cast<HWND>(mainWnd), 0x6f, 300000, nullptr) == 0) {
        L2_LOG_ERROR(Log,
            "[timer] Could not schedule login-attempt counter reset (timer 0x6f, interval 300000 ms). Startup continues without this newly scheduled timer.");
    }
#endif
    return Continue;
}

// L2Authd.exe 0x00464210, Src/L2AuthD.cpp:390-411.
Result InitializeCrypto() {
    char objectKey[] =
        "9=3*2;x]11sa;/4sd.%+$@a*'!2z0~g`{z]?n8(";
    char globalKey[] = "TEST";
    DesObject.DesKeyInit(objectKey);
    DesKeyInit(globalKey);

    switch (config.gameId) {
    case 4:
        EncPwd = EncPwdShalo;
        break;
    case 8:
    case 16:
    case 27:
    case 32:
        EncPwd = EncPwdL2;
        break;
    default:
        EncPwd = EncPwdDev;
        break;
    }
    return Continue;
}

// L2Authd.exe 0x004642B0, Src/L2AuthD.cpp:414-418.
Result CheckCrashDumpDirPermission() {
    exception_init();
    return Continue;
}

// L2Authd.exe 0x004642C0, Src/L2AuthD.cpp:421-439.
Result ConfigureLegacyMailReport() {
    if (config.mailServer != nullptr &&
        std::strlen(config.mailServer) > 0) {
        if (Mailservice.Init() == 0) {
            // Log.AddLog(
            //     LOG_ERROR,
            //     "Sendmail initialize Fail")
            // Log.AddLog(
            //     LOG_ERROR,
            //     "component=mail event=legacy_init_failed action=skip_disk_timer_setup startup=continue")
            // Log.AddLog(
            //     LOG_ERROR,
            //     "[mail] Legacy mail initialization failed. Skipping disk-timer setup, startup continues.")
            L2_LOG_ERROR(Log,
                // "[mail] Legacy mail initialization failed. Skipping disk-timer setup, startup continues.")
                "[mail] Legacy mail initialization failed. Periodic free-space checks will not run; startup continues.");
        } else {
#if defined(_WIN32)
            // ::SetTimer(
            //     reinterpret_cast<HWND>(mainWnd),
            //     0x6e,
            //     7200000,
            //     nullptr)
            if (mainWnd != nullptr) {
                const UINT_PTR diskTimer = ::SetTimer(
                    reinterpret_cast<HWND>(mainWnd),
                    0x6e,
                    7200000,
                    nullptr);
                if (diskTimer == 0) {
                    L2_LOG_ERROR(Log,
                        "[storage] Could not schedule the free-space check (timer 0x6e, interval 2 hours). Startup continues without a newly scheduled disk check.");
                }
            } else
#endif
            {
                legacyDiskTimerEnabled = true;
            }
        }
    }
    return Continue;
}

// L2Authd.exe 0x00464320, Src/L2AuthD.cpp:442-469.
Result ConfigureMailReport() {
    // Portable SMTP requires the same explicit product opt-in as the
    // recovered useReportMail branch. The selected state machine and its
    // product-owned CMailReport remain unchanged above the socket boundary.
    runtime::ConfigureSystemMailNetwork(
        config.useReportMail);
    if (!config.useReportMail) {
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "Report Mail - disabled.")
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "component=mail event=report_disabled reason=configuration")
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "[mail] Email reports are disabled by configuration.")
        L2_LOG_INFO(Log,
            "[mail] Email reports are disabled by configuration.");
        return Continue;
    }

    const std::string buildNumber =
        std::to_string(GetBuildNumber());
    const bool initialized =
        CMailReport::Instance()->Initialize(
            config.reportMailServer,
            static_cast<short>(
                config.reportMailPort),
            config.reportMailFrom,
            config.reportMailTo,
            "L2A1AuthD",
            GetProductVersion(),
            buildNumber.c_str());
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "Report Mail - %s (%s:%d, %s->%s)",
    //     initialized
    //         ? "enabled."
    //         : "failed to initialize.",
    //     config.reportMailServer,
    //     config.reportMailPort,
    //     config.reportMailFrom,
    //     config.reportMailTo)
    // Log.AddLog(
    //     initialized ? LOG_NORMAL : LOG_ERROR,
    //     "component=mail event=report_configuration result=%s host_preview=\"%.255s\" configured_port=%d requested_port=%u action=continue_startup",
    //     initialized ? "success" : "failure",
    //     config.reportMailServer == nullptr ? "<unset>" : config.reportMailServer,
    //     config.reportMailPort, static_cast<unsigned int>(static_cast<std::uint16_t>(config.reportMailPort)))
    // Log.AddLog(
    //     initialized ? LOG_NORMAL : LOG_ERROR,
    //     "[mail] Report initialization: %s. Server \"%.255s\", configured port %d, requested port %u. Startup continues.",
    //     initialized ? "success" : "failure",
    //     config.reportMailServer == nullptr ? "<unset>" : config.reportMailServer,
    //     config.reportMailPort, static_cast<unsigned int>(static_cast<std::uint16_t>(config.reportMailPort)))
    L2_LOG(Log, initialized ? LOG_NORMAL : LOG_ERROR,
        "[mail] Report initialization: %s; server \"%.255s\", configured port %d, requested port %u. Startup continues.",
        initialized ? "success" : "failure",
        config.reportMailServer == nullptr ? "<unset>" : config.reportMailServer,
        config.reportMailPort, static_cast<unsigned int>(static_cast<std::uint16_t>(config.reportMailPort)));
    return Continue;
}

// L2Authd.exe 0x00464450, Src/L2AuthD.cpp:472-500.
Result InitializeDatabase() {
    if (!g_linDB.Init(config.numDBConn, 0)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Failed to connect L2 or AION DB")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=database event=pool_init_failed role=login slot=0 requested_connections=%d action=abort_startup",
        //     config.numDBConn)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[database] Could not initialize the login pool (slot 0, %d requested connections). Startup aborted.",
        //     config.numDBConn)
        L2_LOG_ERROR(Log,
            "[database] Could not initialize the login pool (slot 0, %d requested connections). Startup aborted.",
            config.numDBConn);
        return ErrorStop;
    }
    if (config.useUserPlayTimeLimit &&
        !g_GloActNcsDB.Init(
            config.numDBConn,
            2)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Failed to connect GlobalAccountNCSDB")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=database event=pool_init_failed role=play_time_limit slot=2 requested_connections=%d action=abort_startup",
        //     config.numDBConn)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[database] Could not initialize the play-time-limit pool (slot 2, %d requested connections). Startup aborted.",
        //     config.numDBConn)
        L2_LOG_ERROR(Log,
            "[database] Could not initialize the play-time-limit pool (slot 2, %d requested connections). Startup aborted.",
            config.numDBConn);
        return ErrorStop;
    }

    g_ServerList.ReloadServer();
    CDBConn connection(g_linDB);
    if (config.useTestServer) {
        connection.Execute(
            "{call ap_SetServerStatusT (0, 0)}");
    } else {
        connection.Execute(
            "{call ap_SetServerStatus (0, 0)}");
    }
    return Continue;
}

// L2Authd.exe 0x00464590, Src/L2AuthD.cpp:503-507.
Result InitializeIoThread() {
    CreateIOThread();
    return Continue;
}

// L2Authd.exe 0x004645A0, Src/L2AuthD.cpp:510-575.
Result InitializeClients() {
    if (config.UseLogD) {
        const std::uint32_t peerAddress = ntohl(config.LogDIP.s_addr);
        std::uint32_t nativeSocket = UINT32_MAX;
        const bool connected =
            authd::platform::ConnectNativeSocket(
                nativeSocket,
                config.LogDIP,
                static_cast<std::uint16_t>(
                    config.LogDPort));
        if (!connected) {
            // Log.AddLog(LOG_WARN,
            //     "component=network event=connect_failed peer=logd address=%u.%u.%u.%u port=%u error_domain=winsock_compatible code=%d action=close_socket reconnect=schedule_pending",
            //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
            //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.LogDPort)),
            //     ::portable::GetNativeSocketLastError())
            // Log.AddLog(LOG_WARN,
            //     "[LogD] Could not connect to %u.%u.%u.%u:%u (Winsock-compatible error %d). Closing the socket. Reconnect scheduling is pending.",
            //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
            //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.LogDPort)),
            //     ::portable::GetNativeSocketLastError())
            // Log.AddLog(nativeSocket == UINT32_MAX ? LOG_ERROR : LOG_WARN,
            //     "[logd] Could not connect to %u.%u.%u.%u:%u (Winsock-compatible error %d). %s",
            //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
            //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.LogDPort)),
            //     ::portable::GetNativeSocketLastError(),
            //     nativeSocket == UINT32_MAX
            //         ? "Socket creation failed. This attempt does not schedule automatic reconnect."
            //         : "Closing this socket will request a reconnect timer.")
            L2_LOG(Log, nativeSocket == UINT32_MAX ? LOG_ERROR : LOG_WARN,
                "[logd] Could not connect to %u.%u.%u.%u:%u (Winsock-compatible error %d). %s",
                (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
                (peerAddress >> 8U) & 255U, peerAddress & 255U,
                static_cast<unsigned int>(static_cast<std::uint16_t>(config.LogDPort)),
                ::portable::GetNativeSocketLastError(),
                nativeSocket == UINT32_MAX
                    ? "Socket creation failed; this attempt does not schedule automatic reconnect."
                    : "Closing this socket will request a reconnect timer.");
        }
        pLogSocket =
            CLogSocket::Allocate(nativeSocket);
        pLogSocket->SetAddress(config.LogDIP);
        if (!connected) {
            pLogSocket->CloseSocket();
        } else {
            authd::platform::InitializeNativeSocket(
                pLogSocket,
                nativeSocket,
                g_hIOCompletionPortInt);
        }
    }

    if (config.UseIPServer) {
        const std::uint32_t peerAddress = ntohl(config.IPServer.s_addr);
        std::uint32_t nativeSocket = UINT32_MAX;
        const bool connected =
            authd::platform::ConnectNativeSocket(
                nativeSocket,
                config.IPServer,
                static_cast<std::uint16_t>(
                    config.IPPort));
        if (!connected) {
            // Log.AddLog(LOG_WARN,
            //     "component=network event=connect_failed peer=ip_server address=%u.%u.%u.%u port=%u socket=%u error_domain=winsock_compatible code=%d action=close_socket reconnect=schedule_pending",
            //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
            //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.IPPort)), nativeSocket,
            //     ::portable::GetNativeSocketLastError())
            // Log.AddLog(LOG_WARN,
            //     "[IP server] Could not connect to %u.%u.%u.%u:%u (socket %u. Winsock-compatible error %d). Closing the socket. Reconnect scheduling is pending.",
            //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
            //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.IPPort)), nativeSocket,
            //     ::portable::GetNativeSocketLastError())
            // L2_LOG_WARN(Log,
            //     "[IP server] Could not connect to %u.%u.%u.%u:%u (socket %u. Winsock-compatible error %d). Closing the socket. Reconnect scheduling is pending.",
            //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
            //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.IPPort)), nativeSocket,
            //     ::portable::GetNativeSocketLastError())
            L2_LOG(Log, nativeSocket == UINT32_MAX ? LOG_ERROR : LOG_WARN,
                "[ip-service] Could not connect to %u.%u.%u.%u:%u on socket %u (Winsock-compatible error %d). %s",
                (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
                (peerAddress >> 8U) & 255U, peerAddress & 255U,
                static_cast<unsigned int>(static_cast<std::uint16_t>(config.IPPort)), nativeSocket,
                ::portable::GetNativeSocketLastError(),
                nativeSocket == UINT32_MAX
                    ? "Socket creation failed; this attempt does not schedule automatic reconnect."
                    : "Closing this socket will request a reconnect timer.");
        }
        pIPSocket = new CIPSocket(nativeSocket);
        pIPSocket->SetAddress(config.IPServer);
        if (!connected) {
            pIPSocket->CloseSocket();
        } else {
            authd::platform::InitializeNativeSocket(
                pIPSocket,
                nativeSocket,
                g_hIOCompletionPort);
        }
    }

    if (config.usePBServer) {
        PBServerMgr::Inst()->Initialize();
    }
    if (config.usePAServer) {
        PAServerMgr::Inst()->Initialize();
    }
    if (config.useWSM) {
        WSMServerMgr::Inst()->Initialize();
    }
    if (config.useGpProxy) {
        CGpProxyServerMgr::Inst()->Initialize();
    }
    if (config.useGradeServer) {
        GradeServerMgr::Inst()->Initialize();
    }
    if (config.useLightTokenServer) {
        TokenServerMgr::Inst()->Initialize();
    }
    if (config.useIovationProxy) {
        IovationServerMgr::Inst()->Initialize();
    }
    return Continue;
}

// L2Authd.exe 0x00464800, Src/L2AuthD.cpp:578-602.
Result InitializeExternalFatigueLibrary() {
    if (config.useExternalFatigueSystem &&
        config.externalFatigueCompanyCode != 1) {
        // Noise: IFatigue::InitializeDll now reports the exact load/entrypoint outcome. Omit this duplicate announcement.
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "Load %s",
        //     config.externalFatigueDllName)
        fatigueMgr.InitializeDll();
    }

    if (config.useTimeExpZone) {
        if (!config.resetTimeExpZoneCapsule) {
            // Log.AddLog(
            //     LOG_NORMAL,
            //     "(zone based)Fatigue - resetTimeExpZoneCapsule is disabled")
            // Log.AddLog(
            //     LOG_NORMAL,
            //     "component=fatigue event=capsule_reset_skipped reason=configuration action=retain_stored_capsules")
            // Log.AddLog(
            //     LOG_NORMAL,
            //     "[fatigue] Capsule reset is disabled, stored capsules are retained.")
            L2_LOG_INFO(Log,
                "[fatigue] Capsule reset is disabled; stored capsules are retained.");
        } else {
            // Log.AddLog(
            //     LOG_NORMAL,
            //     "(zone based)Fatigue - resetTimeExpZoneCapsule is enabled, reset capsule info...")
            // Log.AddLog(
            //     LOG_NORMAL,
            //     "component=fatigue event=capsule_reset_started configured_zones=%d",
            //     config.totalTimeExpZoneCount)
            // Log.AddLog(
            //     LOG_NORMAL,
            //     "[fatigue] Resetting capsule information for %d configured zones.",
            //     config.totalTimeExpZoneCount)
            L2_LOG_INFO(Log,
                "[fatigue] Resetting capsule information for %d configured zones.",
                config.totalTimeExpZoneCount);
            [[maybe_unused]] ZoneFatigue zoneFatigue;
            for (int zoneIndex = 0;
                 zoneIndex <
                     config.totalTimeExpZoneCount;
                 ++zoneIndex) {
                const int result =
                    ZoneFatigue::ResetCapsuleInfo(
                        static_cast<unsigned char>(
                            zoneIndex),
                        config.resetCapsuleInfo[
                            zoneIndex]);
                // Log.AddLog(
                //     LOG_NORMAL,
                //     "zoneIdx[%d], resetCapsuleInfo(%s), ret=[%d]",
                //     zoneIndex,
                //     config.resetCapsuleInfo[
                //         zoneIndex],
                //     result)
                // Log.AddLog(
                //     result == 0 ? LOG_NORMAL : LOG_ERROR,
                //     "component=fatigue event=capsule_reset zone=%d result_code=%d outcome=%s action=continue_startup",
                //     zoneIndex, result, result == 0 ? "success" : "failure")
                // Log.AddLog(
                //     result == 0 ? LOG_NORMAL : LOG_ERROR,
                //     "[fatigue] Zone %d capsule reset returned %d (%s). Startup continues.",
                //     zoneIndex, result, result == 0 ? "success" : "failure")
                L2_LOG(Log, result == 0 ? LOG_NORMAL : LOG_ERROR,
                    "[fatigue] Zone %d capsule reset returned %d (%s). Startup continues.",
                    zoneIndex, result, result == 0 ? "success" : "failure");
            }
        }
    }
    return Continue;
}

// L2Authd.exe 0x00464930, Src/L2AuthD.cpp:605-633.
Result InitializeWhiteListIpManager() {
    ConnectedIPList::Inst()->SetConnectionLimit(
        config.limitIPNumforEachGS);
    // Log.AddLog(
    //     LOG_DEBUG,
    //     "config.limitIPNumforEachGS = %d",
    //     config.limitIPNumforEachGS)
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "component=authentication event=ip_connection_limit_configured per_world_limit=%d enforcement=%s",
    //     config.limitIPNumforEachGS, config.limitIPNumforEachGS > 0 ? "enabled" : "disabled")
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "[authentication] Connection limit per IP and world: %d. Enforcement %s.",
    //     config.limitIPNumforEachGS, config.limitIPNumforEachGS > 0 ? "enabled" : "disabled")
    L2_LOG_INFO(Log,
        "[authentication] Connection limit per IP and world: %d; enforcement %s.",
        config.limitIPNumforEachGS, config.limitIPNumforEachGS > 0 ? "enabled" : "disabled");

    if (config.limitIPNumforEachGS >= 0 &&
        config.whiteListIPFile != nullptr) {
        // Noise: The allowlist loader includes the path in its result. This input-only echo duplicates it.
        // Log.AddLog(
        //     LOG_DEBUG,
        //     "config.whiteListIPFile = %s",
        //     config.whiteListIPFile)
        switch (whiteListIPMgr.Load(
            config.whiteListIPFile)) {
        case 1:
            // Noise: The allowlist owner reports its exact load outcome. Omit this generic duplicate.
            // Log.AddLog(
            //     LOG_WARN,
            //     "White List IP File does not exist or wrong path")
            break;
        case 2:
            // Noise: The allowlist owner reports its exact load outcome. Omit this generic duplicate.
            // Log.AddLog(
            //     LOG_ERROR,
            //     "Invalid White List IP Format (Line:%d)",
            //     whiteListIPMgr.GetErrorLine())
            break;
        case 3:
            // Noise: The allowlist owner reports its exact load outcome. Omit this generic duplicate.
            // Log.AddLog(
            //     LOG_WARN,
            //     "No White List IP Loaded")
            break;
        default:
            // Noise: The allowlist owner reports its exact load outcome. Omit this generic duplicate.
            // Log.AddLog(
            //     LOG_NORMAL,
            //     "%d of White List IP Loaded",
            //     whiteListIPMgr.GetIPNum())
            break;
        }
    }
    return Continue;
}

void RunPortableMessageLoop() {
    using Clock = std::chrono::steady_clock;
    auto nextReporter =
        Clock::now() + std::chrono::seconds(2);
    auto nextHeartbeat =
        Clock::now() + std::chrono::minutes(1);
    auto nextLoginReset =
        Clock::now() + std::chrono::minutes(5);
    auto nextDiskCheck =
        Clock::now() + std::chrono::hours(2);
    bool shutdownStarted = false;
    runtime::ConsoleInput consoleInput;
    const bool readConsole = g_serviceManager.IsServiceMode() == 0;

    while (!runtime::raw_atomic::LoadBool(
        &globalTeminateEvent)) {
        if (runtime::ConsoleStopRequested() &&
            !shutdownStarted) {
            shutdownStarted = true;
            RequestForegroundShutdown();
        }
        if (readConsole && !shutdownStarted) {
            consoleInput.Poll([&](std::string_view line) {
                if (!shutdownStarted && authd::ExecuteConsoleCommand(line)) {
                    shutdownStarted = true;
                    RequestForegroundShutdown();
                }
            }, [](std::string_view error) {
                L2_LOG_WARN(Log, "[console] %.*s", static_cast<int>(error.size()), error.data());
            });
        }

        const auto now = Clock::now();
        if (now >= nextReporter) {
            UpdateReporterUserCount();
            nextReporter += std::chrono::seconds(2);
        }
        if (now >= nextHeartbeat) {
            SendWorldStatusHeartbeat();
            nextHeartbeat += std::chrono::minutes(1);
        }
        if (now >= nextLoginReset) {
            CClientSession::ResetTryLoginCnt();
            nextLoginReset += std::chrono::minutes(5);
        }
        if (legacyDiskTimerEnabled &&
            now >= nextDiskCheck) {
            CheckConfiguredLogDiskSpace();
            nextDiskCheck += std::chrono::hours(2);
        }
        SleepMilliseconds(25);
    }
}

// L2Authd.exe 0x00464A30, Src/L2AuthD.cpp:636-678.
Result RunServer() {
    if (mainWnd == nullptr) {
        runtime::InstallConsoleSignalHandlers();
    }
#if defined(_WIN32)
    unsigned int listenerThreadId = 0;
    const std::uintptr_t listenerThread =
        ::_beginthreadex(
            nullptr,
            0,
            ListenThread,
            nullptr,
            0,
            &listenerThreadId);
    if (listenerThread == 0) {
        L2_LOG_ERROR(Log,
            "[network] Could not create the listener thread (errno %d). Startup continues without a listener; the started report is still requested.",
            errno);
    }
#else
    std::thread listenerThread([] {
        ListenThread(nullptr);
    });
#endif

    if (config.lu4Mode) {
        lu4::StartValidator(config.lu4ValidatorPort);
    }
    SendStartedReport();
    if (g_serviceManager.IsServiceMode() == 1) {
        g_serviceManager.SetServiceRunStatus(4);
    }

#if defined(_WIN32)
    if (mainWnd != nullptr) {
        MSG message{};
        while (::GetMessageA(&message, nullptr, 0, 0) > 0) {
            ::TranslateMessage(&message);
            ::DispatchMessageA(&message);
        }
    } else {
        RunPortableMessageLoop();
        if (listenerThread != 0) {
            ::WaitForSingleObject(reinterpret_cast<HANDLE>(listenerThread), INFINITE);
        }
    }
    if (listenerThread != 0) {
        ::CloseHandle(reinterpret_cast<HANDLE>(
            listenerThread));
    }
#else
    RunPortableMessageLoop();
    if (listenerThread.joinable()) {
        listenerThread.join();
    }
#endif
    return Continue;
}

int RunProcessInitializers(
    HINSTANCE__* instance,
    const char* commandLine) {
    const std::array<StartupFunction, 4> initializers{{
        InitializeCurrentDirectory,
        InitializeConfig,
        InitializeLog,
        InitializeWindowsServiceCtrlManager,
    }};
    for (const StartupFunction initializer :
         initializers) {
        const Result result = initializer(
            instance,
            commandLine);
        if (result == OkStop) {
            return 0;
        }
        if (result == ErrorStop) {
            return -1;
        }
    }
    return service_main(0, nullptr);
}

} // namespace

// L2Authd.exe 0x00464B20, Src/L2AuthD.cpp:56-59.
CWsaInitializer::CWsaInitializer() {
#if defined(_WIN32)
    WSADATA data{};
    if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        std::exit(-1);
    }
#endif
}

// L2Authd.exe 0x00464B70, Src/L2AuthD.cpp:61-63.
CWsaInitializer::~CWsaInitializer() {
#if defined(_WIN32)
    ::WSACleanup();
#endif
}

#if defined(_WIN32)
// L2Authd.exe 0x00462F10, Src/L2AuthD.cpp:682-797.
LRESULT CALLBACK WindowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam) {
    if (message == WM_CLOSE &&
        window == reinterpret_cast<HWND>(mainWnd)) {
        ShutdownWnd = reinterpret_cast<HWND__*>(
            ::CreateDialogParamA(
                reinterpret_cast<HINSTANCE>(g_instance),
                MAKEINTRESOURCEA(104),
                window,
                ShutDownDlgProc,
                0));
        ::ShowWindow(
            reinterpret_cast<HWND>(ShutdownWnd),
            5);
        return 0;
    }
    if (message == WM_DESTROY &&
        window == reinterpret_cast<HWND>(mainWnd)) {
        RequestForegroundShutdown();
        ::PostQuitMessage(0);
    } else if (message == WM_SIZE) {
        const int width = LOWORD(lParam);
        const int height = HIWORD(lParam);
        if (window == reinterpret_cast<HWND>(mainWnd)) {
            ::MoveWindow(
                reinterpret_cast<HWND>(reporterWnd),
                160,
                0,
                width - 160,
                20,
                TRUE);
            ::MoveWindow(
                reinterpret_cast<HWND>(logWnd),
                0,
                20,
                width,
                height - 20,
                TRUE);
            ::MoveWindow(
                reinterpret_cast<HWND>(buttonWnd),
                0,
                0,
                80,
                20,
                TRUE);
            ::MoveWindow(
                reinterpret_cast<HWND>(reloadButtonWnd),
                80,
                0,
                80,
                20,
                TRUE);
        } else if (window ==
                   reinterpret_cast<HWND>(logWnd)) {
            Log.Resize(width, height);
        } else if (reporterWnd != nullptr) {
            reporter.Resize(width, height);
        }
    } else if (message == WM_PAINT) {
        if (window == reinterpret_cast<HWND>(logWnd)) {
            Log.Redraw();
        } else if (window ==
                   reinterpret_cast<HWND>(reporterWnd)) {
            reporter.Redraw();
        }
    } else if (message == WM_COMMAND) {
        if (HIWORD(wParam) == 0 &&
            LOWORD(wParam) == 2) {
            g_ServerList.ReloadServer();
        }
    } else if (message == WM_TIMER) {
        switch (wParam) {
        case 0x66:
            UpdateReporterUserCount();
            break;
        case 0x67:
            SendWorldStatusHeartbeat();
            break;
        case 0x6e:
            CheckConfiguredLogDiskSpace();
            break;
        case 0x6f:
            CClientSession::ResetTryLoginCnt();
            break;
        default:
            break;
        }
    } else if (message == 0x500) {
        ::DestroyWindow(
            reinterpret_cast<HWND>(mainWnd));
    }
    return ::DefWindowProcA(
        window,
        message,
        wParam,
        lParam);
}

// L2Authd.exe 0x00463380, Src/L2AuthD.cpp:827-840.
INT_PTR CALLBACK ShutDownDlgProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    [[maybe_unused]] LPARAM lParam) {
    if (message == WM_COMMAND) {
        if (wParam == 1) {
            ::DestroyWindow(
                reinterpret_cast<HWND>(mainWnd));
            ::EndDialog(window, 0);
        } else if (wParam == 2) {
            ::EndDialog(window, 0);
        }
    }
    return 0;
}

// L2Authd.exe 0x004633D0, Src/L2AuthD.cpp:844-880.
DWORD WINAPI service_handler(
    DWORD control,
    [[maybe_unused]] DWORD eventType,
    [[maybe_unused]] LPVOID eventData,
    [[maybe_unused]] LPVOID context) {
    if (control == SERVICE_CONTROL_STOP) {
        g_serviceManager.SetServiceRunStatus(3);
        g_bServiceStop = true;
        runtime::raw_atomic::StoreBool(
            &g_bTerminating,
            true);
        Log.Enable(false);
        job.SetTerminate();
        SendShutdownReport(true);
        SleepMilliseconds(2000);
        g_serviceManager.SetServiceRunStatus(1);
    }
    return 0;
}

#endif

int main(int argumentCount, char** arguments) {
    runtime::logging::SetThreadName("main");
    if (!runtime::InitializeDirectoryArguments(argumentCount, arguments)) return 2;
    std::string commandLine;
    for (int index = 1;
         index < argumentCount;
         ++index) {
        if (!commandLine.empty()) {
            commandLine.push_back(' ');
        }
        commandLine.append(arguments[index]);
    }
    return RunProcessInitializers(
#if defined(_WIN32)
        reinterpret_cast<HINSTANCE__*>(::GetModuleHandleA(nullptr)),
#else
        nullptr,
#endif
        commandLine.c_str());
}

// L2Authd.exe 0x00463490, Src/L2AuthD.cpp:885-933.
int service_main(
    [[maybe_unused]] int argumentCount,
    [[maybe_unused]] char** arguments) {
    runtime::logging::SetThreadName("service");
    PBServerMgr pbServerManager;
    PAServerMgr paServerManager;
    WSMServerMgr wsmServerManager;
    CGpProxyServerMgr gpProxyServerManager;
    GradeServerMgr gradeServerManager;
    TokenServerMgr tokenServerManager;
    IovationServerMgr iovationServerManager;
    AccountContainer accountContainer;
    ConnectedIPList connectedIpList;

    const std::array<ServiceStartupFunction, 14>
        initializers{{
            ValidateConfiguration,
            InitializeServiceCtrlHandler,
            PreventRedundantRunningProcess,
            InitializeWindow,
            InitializeCrypto,
            CheckCrashDumpDirPermission,
            ConfigureLegacyMailReport,
            ConfigureMailReport,
            InitializeDatabase,
            InitializeIoThread,
            InitializeClients,
            InitializeExternalFatigueLibrary,
            InitializeWhiteListIpManager,
            RunServer,
        }};

    for (const ServiceStartupFunction initializer :
         initializers) {
        const Result result = initializer();
        if (result == OkStop) {
            return 0;
        }
        if (result == ErrorStop) {
            return -1;
        }
    }

    while (!runtime::raw_atomic::LoadBool(
        &globalTeminateEvent)) {
        SleepMilliseconds(1000);
    }
    SleepMilliseconds(2000);
    // Log.AddLog(
    //     LOG_WARN,
    //     "Service shutting down.")
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "component=lifecycle event=service_stopping action=disable_logging")
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "[shutdown] Stopping the service and disabling logging.")
    L2_LOG_INFO(Log,
        "[shutdown] Stopping the service and disabling logging.");
    Log.Enable(false);
    return 0;
}
