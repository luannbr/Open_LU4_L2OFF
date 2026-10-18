#include "Shared/src/Runtime/ConfigDirectory.h"
#include "Shared/src/Runtime/OperatorLog.h"
#include "AuthGateSvcMgr.h"
#include "ExceptionReport.h"
#include "FileLog.h"
#include "IOObject.h"
#include "Platform/LegacyWideFormat.h"
#include "Platform/ProcessRuntime.h"
#include "ServiceMgr.h"
#include "StateMonMgr.h"
#include "WindowMgr.h"
#include "GlobalFunc.h"

#include <array>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <string>

#if defined(_WIN32)
#  include <Shellapi.h>
#endif

HINSTANCE__* g_hInstance = nullptr;
void* g_hMutex = nullptr;

// AuthGateD.map assigns every object below to authgated.obj. Their declaration
// order reproduces the module's $E1/$E4/$E6/$E9/$E12/$E15/$E18 initialization
// sequence: monitor manager, IO monitor registration, logs, service carrier,
// then the gateway service owner.
CStateMonMgr g_StateMonMgr;
CIOObjectMonitor g_IOObjectMonitor;
CFileLog authGateLog(u"log", 1, true); // target symbol: `log`
CFileLog logdLog(u"logd", 1, true);
CFileLog botLog(u"bot", 1, true);
CServiceManager g_serviceManager;
CAuthGateSvcMgr g_svcmgr;

namespace {

constexpr char16_t APP_NAME[] = u"AuthGateD";
constexpr char16_t APP_VERSION[] = u"2015072901";

std::u16string TargetFormat(
    const char16_t* format,
    ...) {
    std::u16string output;
    va_list arguments;
    va_start(arguments, format);
    // Original: authgated::portable::FormatTargetWide(output, 0x3ff, format, arguments)
    try {
        authgated::portable::FormatTargetWide(output, 0x3ff, format, arguments);
    } catch (...) {
        va_end(arguments);
        throw;
    }
    va_end(arguments);
    return output;
}

bool CaseInsensitiveEquals(
    const char* left,
    const char* right) noexcept {
    if (!left || !right) return left == right;
    while (*left != '\0' && *right != '\0') {
        const unsigned char leftCharacter =
            static_cast<unsigned char>(*left++);
        const unsigned char rightCharacter =
            static_cast<unsigned char>(*right++);
        if (std::tolower(leftCharacter) !=
            std::tolower(rightCharacter)) {
            return false;
        }
    }
    return *left == *right;
}

bool HasMultipleInstanceArgument(
    [[maybe_unused]] int argumentCount,
    [[maybe_unused]] char** arguments) {
#if defined(_WIN32)
    int wideArgumentCount = 0;
    wchar_t** const wideArguments = ::CommandLineToArgvW(
        ::GetCommandLineW(),
        &wideArgumentCount);
    if (!wideArguments) return false;
    bool found = false;
    for (int index = 0; index < wideArgumentCount; ++index) {
        if (::_wcsicmp(
                wideArguments[index],
                L"/multiple_instance") == 0) {
            found = true;
        }
    }
    ::LocalFree(wideArguments);
    return found;
#else
    for (int index = 0; index < argumentCount; ++index) {
        if (CaseInsensitiveEquals(
                arguments ? arguments[index] : nullptr,
                "/multiple_instance")) {
            return true;
        }
    }
    return false;
#endif
}

} // namespace

#if defined(_WIN32)
#  define AUTHGATED_SERVICE_HANDLER_CALL __stdcall
#else
#  define AUTHGATED_SERVICE_HANDLER_CALL
#endif

// AuthGateD.exe 0x00415F00, authgated.cpp.
std::uint32_t AUTHGATED_SERVICE_HANDLER_CALL service_handler(
    std::uint32_t control,
    [[maybe_unused]] std::uint32_t eventType,
    [[maybe_unused]] void* eventData,
    [[maybe_unused]] void* context) {
    if (control == 1U) {
        g_serviceManager.SetServiceRunStatus(3);
        authgated::platform::PostProcessQuit();
        g_svcmgr.Stop();
        authgated::platform::StopSocketRuntime();
        authgated::platform::CloseInstanceGate(g_hMutex);
        g_serviceManager.SetServiceRunStatus(1);
    }
    return 0;
}

#undef AUTHGATED_SERVICE_HANDLER_CALL

// AuthGateD.exe 0x00415F50, authgated.cpp.
int service_main(int argumentCount, char** arguments) {
    runtime::logging::SetThreadName("service");
    const bool allowMultiple = HasMultipleInstanceArgument(
        argumentCount,
        arguments);
    std::string instanceDiagnostic;
    const authgated::platform::InstanceGateResult instanceResult =
        authgated::platform::AcquireInstanceGate(
            allowMultiple,
            g_hMutex,
            instanceDiagnostic);
    if (instanceResult ==
        authgated::platform::InstanceGateResult::AlreadyRunning) {
        // runtime::logging::Write("authgated", runtime::logging::Level::Error,
        //     "component=startup event=instance_already_running action=exit exit_code=0")
        // runtime::logging::Write("authgated", runtime::logging::Level::Error,
        //     "[startup] Another instance is already running. Exiting without starting another service (exit code 0).")
        runtime::logging::Write("authgated", runtime::logging::Level::Error,
            "[startup] Another instance is already running. Exiting without starting another service (exit code 0).", __FILE__);
        // The selected function calls WSACleanup here even though WSAStartup
        // has not yet run on this path.
        authgated::platform::StopSocketRuntime();
        return 0;
    }
    if (instanceResult == authgated::platform::InstanceGateResult::Error) {
        // runtime::logging::Write("authgated", runtime::logging::Level::Warning,
        //     "component=startup event=instance_guard_failed action=continue_startup, " + instanceDiagnostic)
        // runtime::logging::Write("authgated", runtime::logging::Level::Warning,
        //     "[startup] Could not check the instance guard. Startup continues: " + instanceDiagnostic)
        runtime::logging::Write("authgated", runtime::logging::Level::Warning,
            "[startup] Could not check the instance guard. Startup continues: " + instanceDiagnostic, __FILE__);
    }
    [[maybe_unused]] const bool workingDirectorySet =
        authgated::platform::SetWorkingDirectoryToExecutableDirectory(
            instanceDiagnostic);
    if (!workingDirectorySet) {
        // runtime::logging::Write("authgated", runtime::logging::Level::Warning,
        //     "component=startup event=working_directory_failed action=continue_startup, " + instanceDiagnostic)
        // runtime::logging::Write("authgated", runtime::logging::Level::Warning,
        //     "[startup] Could not set the executable directory as the working directory. Startup continues: " + instanceDiagnostic)
        runtime::logging::Write("authgated", runtime::logging::Level::Warning,
            "[startup] Could not set the executable directory as the working directory. Startup continues: " + instanceDiagnostic, __FILE__);
    }

    if (g_serviceManager.IsServiceMode() == 1) {
        const SERVICE_STATUS_HANDLE handler =
            authgated::platform::RegisterServiceControlHandler(
                CServiceManager::m_InstallParam.lpServiceName,
                service_handler);
        // if (!handler) return -1.
        if (!handler) {
            // runtime::logging::Write("authgated", runtime::logging::Level::Error,
            //     "component=startup event=service_handler_registration_failed action=exit exit_code=-1")
            // runtime::logging::Write("authgated", runtime::logging::Level::Error,
            //     "[service] Could not register the control handler. Startup aborted (exit code -1).")
            runtime::logging::Write("authgated", runtime::logging::Level::Error,
                "[service] Could not register the control handler. Startup aborted (exit code -1).", __FILE__);
            return -1;
        }
        g_serviceManager.SetServiceHandler(handler);
        // if (!g_serviceManager.GetServiceHandler()) return -2.
        if (!g_serviceManager.GetServiceHandler()) {
            // runtime::logging::Write("authgated", runtime::logging::Level::Error,
            //     "component=startup event=service_handler_missing action=exit exit_code=-2")
            // runtime::logging::Write("authgated", runtime::logging::Level::Error,
            //     "[service] The stored control-handler handle is missing. Startup aborted (exit code -2).")
            runtime::logging::Write("authgated", runtime::logging::Level::Error,
                "[service] The stored control-handler handle is missing. Startup aborted (exit code -2).", __FILE__);
            return -2;
        }
        g_serviceManager.SetServiceRunStatus(2);
    }

    const int socketError = authgated::platform::StartSocketRuntime();
    if (socketError != 0) {
        // runtime::logging::Write("authgated", runtime::logging::Level::Error,
        //     "component=startup event=socket_runtime_failed error_domain=winsock native_code=" +
        //     std::to_string(socketError) + " action=exit exit_code=-3")
        // runtime::logging::Write("authgated", runtime::logging::Level::Error,
        //     "[startup] Could not initialize the socket runtime. Winsock error " +
        //     std::to_string(socketError) + ". Startup aborted (exit code -3).")
        runtime::logging::Write("authgated", runtime::logging::Level::Error,
            "[startup] Could not initialize the socket runtime. Winsock error " +
            std::to_string(socketError) + ". Startup aborted (exit code -3).", __FILE__);
        return -3;
    }
    authgated::platform::DisableProcessPriorityBoost();

    CWindowMgr window(g_hInstance, APP_NAME);
    window.Init();
    window.Run();
    std::array<char16_t, 30> begin{};
    GetFullDate(
        begin.data(),
        static_cast<std::uint32_t>(begin.size()));
    const std::u16string status = TargetFormat(
        u"AppVersion [ %s ]   ProductVersion [ %S ]   Begin [ %s ]",
        APP_VERSION,
        GetProductVersion(),
        begin.data());
    window.SetStatusBar(status.c_str());

    if (g_svcmgr.Init()) {
        // authGateLog.Log(
        //     LOG_NORMAL,
        //     u"@AuthGateD : ServiceMode[%d]",
        //     g_serviceManager.IsServiceMode())
        // authGateLog.Log(
        //     LOG_NORMAL,
        //     u"component=startup event=starting service_mode=%d",
        //     g_serviceManager.IsServiceMode())
        // authGateLog.Log(
        //     LOG_NORMAL,
        //     u"[startup] Starting (service mode: %d).",
        //     g_serviceManager.IsServiceMode())
        L2_LOG_INFO(authGateLog,
            u"[startup] Starting (service mode: %d).",
            g_serviceManager.IsServiceMode());
        g_svcmgr.Run();
    }
    if (g_serviceManager.IsServiceMode() == 1) {
        g_serviceManager.SetServiceRunStatus(4);
    }

    authgated::platform::TerminationBridge termination(&window);
    window.MessageLoop();
    g_svcmgr.Stop();
    authgated::platform::StopSocketRuntime();
    authgated::platform::CloseInstanceGate(g_hMutex);
    return 0;
}

namespace {

int AuthGateDMain(
    HINSTANCE__* instance,
    const char* commandLine,
    int argumentCount,
    char** arguments) {
    runtime::logging::SetThreadName("main");
    CExceptionReport::SetProgramName(APP_NAME);
    g_serviceManager.ConfigServiceName("AuthGateD");
    g_serviceManager.ConfigServiceDisp("AuthGateD SERVICE");
    g_serviceManager.ConfigServiceDesc("authentication service");
    if (CaseInsensitiveEquals(commandLine, "/install")) {
        g_serviceManager.Install();
        return 0;
    }
    if (CaseInsensitiveEquals(commandLine, "/uninstall")) {
        g_serviceManager.Uninstall(0, 30);
        return 0;
    }
    g_hInstance = instance;
    if (g_serviceManager.IsServiceMode() == 1) {
        // return authgated::platform::StartServiceDispatcher(
        //            CServiceManager::m_InstallParam.lpServiceName,
        //            service_main)
        //     ? 0
        //     : -1.
        const bool dispatched = authgated::platform::StartServiceDispatcher(
            CServiceManager::m_InstallParam.lpServiceName, service_main);
        if (!dispatched) {
            // runtime::logging::Write("authgated", runtime::logging::Level::Error,
            //     "component=startup event=service_dispatch_failed action=exit exit_code=-1")
            // runtime::logging::Write("authgated", runtime::logging::Level::Error,
            //     "[service] Could not start the service dispatcher. Exiting with code -1.")
            runtime::logging::Write("authgated", runtime::logging::Level::Error,
                "[service] Could not start the service dispatcher. Exiting with code -1.", __FILE__);
        }
        return dispatched ? 0 : -1;
    }
    return service_main(argumentCount, arguments);
}

} // namespace

int main(int argumentCount, char** arguments) {
    if (!runtime::InitializeDirectoryArguments(argumentCount, arguments)) return 2;
    std::string commandLine;
    for (int index = 1; index < argumentCount; ++index) {
        if (!commandLine.empty()) commandLine.push_back(' ');
        commandLine.append(arguments[index]);
    }
    return AuthGateDMain(
#if defined(_WIN32)
        ::GetModuleHandleW(nullptr),
#else
        nullptr,
#endif
        commandLine.c_str(),
        argumentCount,
        arguments);
}
