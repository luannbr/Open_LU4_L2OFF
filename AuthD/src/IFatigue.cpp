#include "IFatigue.h"

#include "AccountDB.h"
#include "Config.h"
#include "Log.h"
#include "ServerList.h"
#include "SocketServerEx.h"
#include "Thread.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if !defined(_WIN32)
#include <dlfcn.h>
#endif

IFatigue fatigueMgr;

namespace {

using FatigueCallbackFunction =
    int (*)(int, int, int, char*, int, int);
using InitializeFunction =
    void (*)(void*, FatigueCallbackFunction);
using FinalizeFunction = void (*)();
using FatigueFunction = void (*)(int, char*, char*);

// Called immediately after a failed native loader operation. No configured
// account, token or connection string is included in this diagnostic.
void ReportFatigueLoaderError(const char* stage, const char* action) {
    const int savedErrno = errno;
#if defined(_WIN32)
    const DWORD error = ::GetLastError();
    // Log.AddLog(LOG_ERROR,
    //     "component=fatigue event=module_init_failed stage=%s path_preview=\"%.512s\" error_domain=win32 code=%lu %s",
    //     stage, config.externalFatigueDllName, static_cast<unsigned long>(error), action)
    // Log.AddLog(LOG_ERROR,
    //     "[fatigue] Module initialization failed during %s (path preview \"%.512s\", Win32 error %lu). %s",
    //     stage, config.externalFatigueDllName, static_cast<unsigned long>(error), action)
    L2_LOG_ERROR(Log,
        "[fatigue] Module initialization failed during %s (path preview \"%.512s\", Win32 error %lu). %s",
        stage, config.externalFatigueDllName, static_cast<unsigned long>(error), action);
    ::SetLastError(error);
#else
    const char* const error = ::dlerror();
    // Log.AddLog(LOG_ERROR,
    //     "component=fatigue event=module_init_failed stage=%s path_preview=\"%.512s\" error_domain=dynamic_loader detail_preview=\"%.512s\" %s",
    //     stage, config.externalFatigueDllName,
    //     error == nullptr ? "unavailable" : error, action)
    // Log.AddLog(LOG_ERROR,
    //     "[fatigue] Module initialization failed during %s (path preview \"%.512s\", loader detail preview \"%.512s\"). %s",
    //     stage, config.externalFatigueDllName,
    //     error == nullptr ? "unavailable" : error, action)
    L2_LOG_ERROR(Log,
        "[fatigue] Module initialization failed during %s (path preview \"%.512s\", loader detail preview \"%.512s\"). %s",
        stage, config.externalFatigueDllName,
        error == nullptr ? "unavailable" : error, action);
#endif
    errno = savedErrno;
}

HINSTANCE__* LoadFatigueModule(const char* path) {
    if (path == nullptr) {
        return nullptr;
    }
#if defined(_WIN32)
    return ::LoadLibraryA(path);
#else
    return reinterpret_cast<HINSTANCE__*>(
        ::dlopen(path, RTLD_NOW | RTLD_LOCAL));
#endif
}

void* FindFatigueSymbol(
    HINSTANCE__* module,
    const char* name) {
    if (module == nullptr) {
        return nullptr;
    }
#if defined(_WIN32)
    const FARPROC symbol = ::GetProcAddress(module, name);
    void* result = nullptr;
    static_assert(sizeof(result) == sizeof(symbol));
    std::memcpy(&result, &symbol, sizeof(result));
    return result;
#else
    return ::dlsym(reinterpret_cast<void*>(module), name);
#endif
}

template <typename Function>
Function GetFatigueFunction(
    HINSTANCE__* module,
    const char* name) {
    void* const symbol = FindFatigueSymbol(module, name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(symbol));
    std::memcpy(&function, &symbol, sizeof(function));
    return function;
}

void FreeFatigueModule(HINSTANCE__* module) {
    if (module == nullptr) {
        return;
    }
#if defined(_WIN32)
    ::FreeLibrary(module);
#else
    ::dlclose(reinterpret_cast<void*>(module));
#endif
}

void FormatLoginAddress(
    char (&text)[16],
    const in_addr address) {
    const auto* const octets =
        reinterpret_cast<const unsigned char*>(&address);
    [[maybe_unused]] const int formatted = std::snprintf(
        text,
        sizeof(text),
        "%d.%d.%d.%d",
        static_cast<int>(octets[0]),
        static_cast<int>(octets[1]),
        static_cast<int>(octets[2]),
        static_cast<int>(octets[3]));
}

} // namespace

// L2Authd.exe 0x00446F70, Src/ifatigue.cpp:17-62.
int IFatigue::InitializeDll() {
    FModule* const pModule = new FModule;
    pModule->mhIOThreadIOCP = &g_hIOCompletionPort;
    pModule->mpPacketLog = &packetLog;
    pModule->mCompanyCode =
        config.externalFatigueCompanyCode;

    const auto itFirstPAConnection =
        config.PAHostInfo.begin();
    if (itFirstPAConnection != config.PAHostInfo.end()) {
        pModule->mPAIP = itFirstPAConnection->host;
        pModule->mPAPort = itFirstPAConnection->port;
    }

    pModule->mPAReconnectInterval =
        config.PAReconnectInterval;
    pModule->mGameId = config.gameId;
    // Source bug: the original passed config.logDirectory as the format. A "%"
    // in the path or a missing logDirectory key was undefined behavior.
    // std::snprintf(pModule->mLogDirectory, sizeof(pModule->mLogDirectory), config.logDirectory)
    [[maybe_unused]] const int formatted = std::snprintf(
        pModule->mLogDirectory,
        sizeof(pModule->mLogDirectory),
        "%s",
        config.logDirectory != nullptr ? config.logDirectory : "");

    mDllHandle = LoadFatigueModule(
        config.externalFatigueDllName);
    if (mDllHandle == nullptr) {
        if (config.externalFatigueDllName == nullptr) {
            // Log.AddLog(LOG_ERROR,
            //     "component=fatigue event=module_init_failed reason=missing_path key=ExternalFatigueDllName result_code=1 action=continue_startup")
            // Log.AddLog(LOG_ERROR,
            //     "[fatigue] Module path ExternalFatigueDllName is missing. Initialization returns 1, startup continues.")
            L2_LOG_ERROR(Log,
                "[fatigue] Module path ExternalFatigueDllName is missing. Initialization returns 1; startup continues.");
        } else {
            // ReportFatigueLoaderError("load_module",
            //     "result_code=1 action=continue_startup")
            ReportFatigueLoaderError("loading the module",
                "Initialization returns 1; startup continues.");
        }
        return 1;
    }

    const InitializeFunction pFunc =
        GetFatigueFunction<InitializeFunction>(
            mDllHandle,
            "Initialize");
    if (pFunc == nullptr) {
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "Loading %s failed.",
        //     config.externalFatigueDllName)
        // ReportFatigueLoaderError("resolve_initialize",
        //     "symbol=Initialize result_code=0 action=skip_module_initializer startup=continue")
        ReportFatigueLoaderError("resolving Initialize",
            "Initialize was not found; skipping the initializer. Returning 0; startup continues.");
    } else {
        // Noise: emit the outcome after the module initializer returns. This
        // earlier message did not establish that initialization completed.
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "%s was successfully loaded",
        //     config.externalFatigueDllName)
        pFunc(pModule, &IFatigue::FatigueCallback);
        // Log.AddLog(LOG_NORMAL,
        //     "component=fatigue event=module_initialized path_preview=\"%.512s\" company_code=%d result=initializer_returned",
        //     config.externalFatigueDllName, config.externalFatigueCompanyCode)
        // Log.AddLog(LOG_NORMAL,
        //     "[fatigue] Module initializer returned (path preview \"%.512s\", company code %d).",
        //     config.externalFatigueDllName, config.externalFatigueCompanyCode)
        L2_LOG_INFO(Log,
            "[fatigue] Module initializer returned (path preview \"%.512s\", company code %d).",
            config.externalFatigueDllName, config.externalFatigueCompanyCode);
    }
    return 0;
}

// L2Authd.exe 0x00447110, Src/ifatigue.cpp:66-74.
int IFatigue::FinalizeDll() {
    const FinalizeFunction pFunc =
        GetFatigueFunction<FinalizeFunction>(
            mDllHandle,
            "Finalize");
    if (pFunc != nullptr) {
        pFunc();
    }
    FreeFatigueModule(mDllHandle);
    return 0;
}

// L2Authd.exe 0x00447150, Src/ifatigue.cpp:78-96.
int IFatigue::FatigueLogin(
    int uid,
    LoginUser* loginUser) {
    const FatigueFunction pFunc =
        GetFatigueFunction<FatigueFunction>(
            mDllHandle,
            "FatigueLogin");
    if (pFunc == nullptr) {
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "GetProcAddress FatigueLogin %s failed.",
        //     config.externalFatigueDllName)
        // Log.AddLog(LOG_ERROR,
        //     "component=fatigue event=entrypoint_unavailable symbol=FatigueLogin uid=%d path_preview=\"%.512s\" module_loaded=%s action=invoke_fallback_then_abort",
        //     uid, config.externalFatigueDllName == nullptr ? "<unset>" : config.externalFatigueDllName,
        //     mDllHandle == nullptr ? "false" : "true")
        // Log.AddLog(LOG_ERROR,
        //     "[fatigue] FatigueLogin is unavailable for account %d (path preview \"%.512s\", module %s). Invoking the fallback, then aborting.",
        //     uid, config.externalFatigueDllName == nullptr ? "<unset>" : config.externalFatigueDllName,
        //     mDllHandle == nullptr ? "not loaded" : "loaded")
        L2_LOG_ERROR(Log,
            "[fatigue] FatigueLogin is unavailable for account %d (path preview \"%.512s\", module %s). Invoking the fallback, then aborting.",
            uid, config.externalFatigueDllName == nullptr ? "<unset>" : config.externalFatigueDllName,
            mDllHandle == nullptr ? "not loaded" : "loaded");
        FatigueCallback(
            0,
            0,
            0,
            loginUser->ptid,
            0,
            0);
        // The selected image falls through and calls the null pFunc here.
        // Make the same fatal boundary deterministic on portable hosts.
        std::abort();
    }

    char strIP[16];
    FormatLoginAddress(strIP, loginUser->loginIp);
    pFunc(uid, loginUser->ptid, strIP);
    return 0;
}

// L2Authd.exe 0x00447210, Src/ifatigue.cpp:100-118.
int IFatigue::FatigueLogOut(
    int uid,
    const LoginUser& user) {
    const FatigueFunction pFunc =
        GetFatigueFunction<FatigueFunction>(
            mDllHandle,
            "FatigueLogOut");
    if (pFunc == nullptr) {
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "GetProcAddress FatigueLogOut %s failed.",
        //     config.externalFatigueDllName)
        // Log.AddLog(LOG_ERROR,
        //     "component=fatigue event=entrypoint_unavailable symbol=FatigueLogOut uid=%d path_preview=\"%.512s\" module_loaded=%s action=invoke_fallback_then_abort",
        //     uid, config.externalFatigueDllName == nullptr ? "<unset>" : config.externalFatigueDllName,
        //     mDllHandle == nullptr ? "false" : "true")
        // Log.AddLog(LOG_ERROR,
        //     "[fatigue] FatigueLogOut is unavailable for account %d (path preview \"%.512s\", module %s). Invoking the fallback, then aborting.",
        //     uid, config.externalFatigueDllName == nullptr ? "<unset>" : config.externalFatigueDllName,
        //     mDllHandle == nullptr ? "not loaded" : "loaded")
        L2_LOG_ERROR(Log,
            "[fatigue] FatigueLogOut is unavailable for account %d (path preview \"%.512s\", module %s). Invoking the fallback, then aborting.",
            uid, config.externalFatigueDllName == nullptr ? "<unset>" : config.externalFatigueDllName,
            mDllHandle == nullptr ? "not loaded" : "loaded");
        FatigueCallback(
            1,
            0,
            0,
            const_cast<char*>(user.ptid),
            0,
            0);
        // As in FatigueLogin, the shipping body next calls the null pFunc.
        std::abort();
    }

    char strIP[16];
    FormatLoginAddress(strIP, user.loginIp);
    pFunc(
        uid,
        const_cast<char*>(user.ptid),
        strIP);
    return 0;
}

// L2Authd.exe 0x004472D0, Src/ifatigue.cpp:122-159.
int IFatigue::FatigueCallback(
    int cmd,
    int uid,
    [[maybe_unused]] int result,
    [[maybe_unused]] char* ptid,
    int onTime,
    int offTime) {
    if (uid <= 0) {
        // Log.AddLog(
        //     LOG_DEBUG,
        //     "FatigueCallBack::UID = %d",
        //     uid)
        // Log.AddLog(LOG_DEBUG,
        //     "component=fatigue event=callback_ignored command=%d uid=%d reason=nonpositive_uid result_code=1",
        //     cmd, uid)
        // Log.AddLog(LOG_DEBUG,
        //     "[fatigue] Ignoring callback command %d for nonpositive account ID %d. Returning 1.",
        //     cmd, uid)
        L2_LOG_DEBUG(Log,
            "[fatigue] Ignoring callback command %d for nonpositive account ID %d. Returning 1.",
            cmd, uid);
        return 1;
    }

    LoginUser* pLoginUser = nullptr;
    if (!accountdb.FindAccount(uid, &pLoginUser)) {
        if (cmd == 0) {
            // Log.AddLog(
            //     LOG_DEBUG,
            //     "FatigueCallBack::FindAccount Failed. uid = %d",
            //     uid)
            // Log.AddLog(LOG_DEBUG,
            //     "component=fatigue event=callback_ignored command=0 uid=%d reason=account_not_registered result_code=1",
            //     uid)
            // Log.AddLog(LOG_DEBUG,
            //     "[fatigue] Ignoring login callback for account %d: the account is not registered. Returning 1.",
            //     uid)
            L2_LOG_DEBUG(Log,
                "[fatigue] Ignoring login callback for account %d: the account is not registered. Returning 1.",
                uid);
        }
        return 1;
    }

    if (cmd == 0) {
        const WorldServer worldServer =
            g_ServerList.GetAt(pLoginUser->serverid);
        [[maybe_unused]] const int sent = SendSocket(
            worldServer.inner_addr,
            "cddd",
            0x12,
            uid,
            offTime,
            onTime);
        if (sent == 0) {
            L2_LOG_WARN(Log,
                "[fatigue] Could not forward login callback for account %d to game server %d: no connection was found for its internal address. Callback still returns 0.",
                uid, static_cast<int>(pLoginUser->serverid));
        }
    }
    pLoginUser->ReleaseRef();
    return 0;
}
