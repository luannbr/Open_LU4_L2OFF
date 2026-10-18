#include "AuthGateConfig.h"

#include "FileLog.h"
#include "Shared/src/Runtime/DirectLog.h"
#include "Shared/src/TextRuntime.h"
#include "GlobalFunc.h"

#include <array>
#include <charconv>
#include <string>
#include <string_view>

CAuthGateConfig config;

namespace {

const char16_t* BoolText(bool value) noexcept {
    return value ? u"true" : u"false";
}

} // namespace

void CAuthGateConfig::InitConfig() {
    // const int debugLogging = GetInt(u"DebugLogging", 1)
    // runtime::logging::authGateDebugLoggingEnabled.store(debugLogging != 0, std::memory_order_relaxed)
    // if (debugLogging != 0 && debugLogging != 1) {
    //     L2_LOG_DIRECT(stderr, runtime::logging::Level::Warning,
    //         "[config] DebugLogging must be 0 or 1. Received %d. Retaining the default enabled debug logging.",
    //         debugLogging)
    // }
    const char16_t* const debugValue = Get(u"DebugLogging");
    const bool debugValid = !debugValue || std::u16string_view(debugValue) == u"0" ||
        std::u16string_view(debugValue) == u"1";
    runtime::logging::authGateDebugLoggingEnabled.store(
        !debugValue || std::u16string_view(debugValue) != u"0", std::memory_order_relaxed);
    if (!debugValid) {
        // InitConfig precedes the service's default-file-logger publication.
        L2_LOG_DIRECT(stderr, runtime::logging::Level::Warning,
            "[config] DebugLogging must be exactly 0 or 1. Invalid value; using the default enabled debug logging.");
    }
    serverPort = GetInt(u"serverPort", 0);
    authAddr = GetInetAddr(u"authAddr");
    authPort = GetInt(u"authPort", 0);
    authReconnectInterval = GetInt(u"authReconnectInterval", 0);
    numThread = GetInt(u"numThread", 0);
    numIOThread = GetInt(u"numIOThread", 8);
    acceptCallNum = GetInt(u"acceptCallNum", 0);
    socketLimit = GetInt(u"socketLimit", 0);
    useForbiddenIPList = GetBool(u"useForbiddenIPList", false);
    logDirectory = Get(u"logDirectory");
    dumpPacket = GetBool(u"dumpPacket", false);
    sessionTimeout = GetInt(u"sessionTimeout", 0);
    tryInterval = GetInt(u"tryInterval", 0);
    tryCount = GetInt(u"tryCount", 0);
    tryBlockInterval = GetInt(u"tryBlockInterval", 0);
    useGameGuard = GetBool(u"useGameGuard", false);
    checkGameGuard = GetBool(u"checkGameGuard", false);
    useNotifyCSResult = GetBool(u"useNotifyCSResult", false);
    ggNumActive = GetInt(u"ggNumActive", 0);
    ggUseUpdateTimer = GetBool(u"ggUseUpdateTimer", false);
    ggUseLog = GetInt(u"ggUseLog", 0);
    ggLogInvalid = GetBool(u"ggLogInvalid", false);
    maxPenaltySeconds = GetInt(u"maxPenaltySeconds", 120);
    useLogd = GetBool(u"useLogd", false);
    logdAddr = GetInetAddr(u"logdAddr");
    logdPort = GetInt(u"logdPort", 0);
    loginType = GetInt(u"loginType", 0);
    tmpCompanyCode = Get(u"companyCode");
    appLaunchBanDelay = GetInt(u"appLaunchBanDelay", 5);
    useGCSideExtendAccount = GetBool(u"useGCSideExtendAccount", false);
    lu4Port = GetInt(u"lu4Port", 0);
    useReportMail = GetBool(u"useReportMail", false);
    reportMailServer = Get(u"reportMailServer");
    reportMailPort = GetInt(u"reportMailPort", 25);
    reportMailFrom = Get(u"reportMailFrom");
    reportMailTo = Get(u"reportMailTo");

    companyCode.fill('\0');
    if (tmpCompanyCode) {
        std::u16string_view codes(tmpCompanyCode);
        std::size_t position = 0;
        std::size_t output = 0;
        while (position <= codes.size() && output < companyCode.size()) {
            const std::size_t comma = codes.find(u',', position);
            const std::size_t end = comma == std::u16string_view::npos
                ? codes.size()
                : comma;
            const std::string token = Utf16ToUtf8(codes.substr(position, end - position));
            int value = 0;
            std::from_chars(token.data(), token.data() + token.size(), value);
            companyCode[output++] = static_cast<char>(value);
            if (comma == std::u16string_view::npos) break;
            position = comma + 1U;
        }
    }
}

bool CAuthGateConfig::ValidateConfig() const {
    if (tryCount >= 63) {
        // CFileLog::g_pDefaultFileLog->Log(
        //     LOG_ERROR,
        //     u"[Error while config validating] tryCount over %d : %d",
        //     63,
        //     tryCount)
        // CFileLog::g_pDefaultFileLog->Log(
        //     LOG_ERROR,
        //     u"component=config event=validation_failed option=tryCount value=%d maximum=62. Startup aborted",
        //     tryCount)
        // CFileLog::g_pDefaultFileLog->Log(
        //     LOG_ERROR,
        //     u"[config] tryCount is %d. The maximum is 62. Startup aborted.",
        //     tryCount)
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[config] tryCount is %d; the maximum is 62. Startup aborted.",
            tryCount);
    }
    return tryCount < 63;
}

// Original configuration report kept. Replacement follows.
// // AuthGateD.exe 0x00415AE0, AuthGateConfig.cpp. The individual calls and
// // original labels are kept. The target does not build one
// // aggregate report string and does not report maxPenaltySeconds here.
// void CAuthGateConfig::ReportConfig() const {
//     CFileLog& logger = *CFileLog::g_pDefaultFileLog.
//     std::array<char16_t, 128> address{}
// 
//     logger.Log(LOG_SYSTEM, u"   serverPort: %d", serverPort)
//     logger.Log(
//         LOG_SYSTEM,
//         u"   authAddr: %s",
//         InetToStr(&authAddr, address.data(), address.size()))
//     logger.Log(LOG_SYSTEM, u"   authPort: %d", authPort)
//     logger.Log(
//         LOG_SYSTEM,
//         u"   authReconnectInterval: %d",
//         authReconnectInterval)
//     logger.Log(LOG_SYSTEM, u"   numThread: %d", numThread)
//     logger.Log(LOG_SYSTEM, u"   numIOThread: %d", numIOThread)
//     logger.Log(LOG_SYSTEM, u"   acceptCallNum: %d", acceptCallNum)
//     logger.Log(LOG_SYSTEM, u"   socketLimit: %d", socketLimit)
//     logger.Log(
//         LOG_SYSTEM,
//         u"   useForbiddenIPList: %s",
//         BoolText(useForbiddenIPList))
//     logger.Log(LOG_SYSTEM, u"   logDirectory: %s", logDirectory)
//     logger.Log(LOG_SYSTEM, u"   dumpPacket: %s", BoolText(dumpPacket))
//     logger.Log(LOG_SYSTEM, u"   sessionTimeout: %d", sessionTimeout)
//     logger.Log(LOG_SYSTEM, u"   tryInterval: %d", tryInterval)
//     logger.Log(LOG_SYSTEM, u"   tryCount: %d", tryCount)
//     logger.Log(
//         LOG_SYSTEM,
//         u"   tryBlockInterval: %d",
//         tryBlockInterval)
//     logger.Log(
//         LOG_SYSTEM,
//         u"   useGameGuard: %s",
//         BoolText(useGameGuard))
//     logger.Log(
//         LOG_SYSTEM,
//         u"   checkGameGuard: %s",
//         BoolText(checkGameGuard))
//     logger.Log(
//         LOG_SYSTEM,
//         u"   useNotifyCSResult: %s",
//         BoolText(useNotifyCSResult))
//     logger.Log(LOG_SYSTEM, u"   ggNumActive: %d", ggNumActive)
//     logger.Log(
//         LOG_SYSTEM,
//         u"   ggUseUpdateTimer: %d",
//         static_cast<int>(ggUseUpdateTimer))
//     logger.Log(LOG_SYSTEM, u"   ggUseLog: %d", ggUseLog)
//     logger.Log(
//         LOG_SYSTEM,
//         u"   ggLogInvalid: %d",
//         static_cast<int>(ggLogInvalid))
//     logger.Log(
//         LOG_SYSTEM,
//         u"   useLogd: %d",
//         static_cast<int>(useLogd))
//     logger.Log(
//         LOG_SYSTEM,
//         u"   logdAddr: %s",
//         InetToStr(&logdAddr, address.data(), address.size()))
//     logger.Log(LOG_SYSTEM, u"   logdPort: %d", logdPort)
//     logger.Log(LOG_SYSTEM, u"   loginType: %d", loginType)
//     logger.Log(
//         LOG_SYSTEM,
//         u"   companyCode: %d, %d, %d",
//         static_cast<int>(static_cast<signed char>(companyCode[0])),
//         static_cast<int>(static_cast<signed char>(companyCode[1])),
//         static_cast<int>(static_cast<signed char>(companyCode[2])))
//     logger.Log(
//         LOG_SYSTEM,
//         u"   appLaunchBanDelay: %d",
//         appLaunchBanDelay)
//     logger.Log(
//         LOG_SYSTEM,
//         u"   useGCSideExtendAccount: %s",
//         BoolText(useGCSideExtendAccount))
//     logger.Log(
//         LOG_SYSTEM,
//         u"   useReportMail: %s",
//         BoolText(useReportMail))
//     logger.Log(
//         LOG_SYSTEM,
//         u"   reportMailServer: %s",
//         reportMailServer ? reportMailServer : u"NULL")
//     logger.Log(LOG_SYSTEM, u"   reportMailPort: %d", reportMailPort)
//     logger.Log(
//         LOG_SYSTEM,
//         u"   reportMailFrom: %s",
//         reportMailFrom ? reportMailFrom : u"NULL")
//     logger.Log(
//         LOG_SYSTEM,
//         u"   reportMailTo: %s",
//         reportMailTo ? reportMailTo : u"NULL")
// }

// Operator report groups related settings. Protocol and validation are unchanged.
void CAuthGateConfig::ReportConfig() const {
    CFileLog& logger = *CFileLog::g_pDefaultFileLog;
    std::array<char16_t, 128> address{};
    // logger.Log(LOG_SYSTEM,
    //     u"component=config section=listener port=%d socket_limit=%d accept_calls=%d workers=%d io_workers=%d forbidden_ip_list=%s",
    //     serverPort, socketLimit, acceptCallNum, numThread, numIOThread, BoolText(useForbiddenIPList))
    // logger.Log(LOG_SYSTEM,
    //     u"[config] Client listener: port %d, limit %d sockets, %d pending accepts.",
    //     serverPort, socketLimit, acceptCallNum)
    L2_LOG(logger, LOG_SYSTEM,
        u"[config] Client listener: port %d, limit %d sockets, %d pending accepts.",
        serverPort, socketLimit, acceptCallNum);
    // logger.Log(LOG_SYSTEM,
    //     u"[config] Workers: %d. I/O workers: %d. Forbidden-IP list enabled: %s.",
    //     numThread, numIOThread, BoolText(useForbiddenIPList))
    L2_LOG(logger, LOG_SYSTEM,
        u"[config] Workers: %d; I/O workers: %d; forbidden-IP list enabled: %s.",
        numThread, numIOThread, BoolText(useForbiddenIPList));
    // logger.Log(LOG_SYSTEM,
    //     u"component=config section=authd address=%s port=%d reconnect_seconds=%d login_type=%d extended_accounts=%s company_codes=%d,%d,%d",
    //     InetToStr(&authAddr, address.data(), address.size()), authPort, authReconnectInterval,
    //     loginType, BoolText(useGCSideExtendAccount),
    //     static_cast<int>(static_cast<signed char>(companyCode[0])),
    //     static_cast<int>(static_cast<signed char>(companyCode[1])),
    //     static_cast<int>(static_cast<signed char>(companyCode[2])))
    // logger.Log(LOG_SYSTEM,
    //     u"[config] AuthD endpoint: %s:%d. Reconnect interval %d seconds.",
    //     InetToStr(&authAddr, address.data(), address.size()), authPort, authReconnectInterval)
    L2_LOG(logger, LOG_SYSTEM,
        u"[config] AuthD endpoint: %s:%d; reconnect interval %d seconds.",
        InetToStr(&authAddr, address.data(), address.size()), authPort, authReconnectInterval);
    // logger.Log(LOG_SYSTEM,
    //     u"[config] Login type: %d. Extended accounts: %s. Company codes: %d, %d, %d.",
    //     loginType, BoolText(useGCSideExtendAccount),
    //     static_cast<int>(static_cast<signed char>(companyCode[0])),
    //     static_cast<int>(static_cast<signed char>(companyCode[1])),
    //     static_cast<int>(static_cast<signed char>(companyCode[2])))
    L2_LOG(logger, LOG_SYSTEM,
        u"[config] Login type: %d; extended accounts: %s; company codes: %d, %d, %d.",
        loginType, BoolText(useGCSideExtendAccount),
        static_cast<int>(static_cast<signed char>(companyCode[0])),
        static_cast<int>(static_cast<signed char>(companyCode[1])),
        static_cast<int>(static_cast<signed char>(companyCode[2])));
    // logger.Log(LOG_SYSTEM,
    //     u"component=config section=sessions timeout_minutes=%d try_interval_seconds=%d try_count=%d block_interval_seconds=%d launch_ban_delay_seconds=%d",
    //     sessionTimeout, tryInterval, tryCount, tryBlockInterval, appLaunchBanDelay)
    // logger.Log(LOG_SYSTEM,
    //     u"[config] Session timeout: %d minutes. Retry interval: %d seconds, attempt limit: %d, blocking interval: %d seconds, launch-ban delay: %d seconds.",
    //     sessionTimeout, tryInterval, tryCount, tryBlockInterval, appLaunchBanDelay)
    L2_LOG(logger, LOG_SYSTEM,
        u"[config] Session timeout: %d minutes. Retry interval: %d seconds, attempt limit: %d, blocking interval: %d seconds, launch-ban delay: %d seconds.",
        sessionTimeout, tryInterval, tryCount, tryBlockInterval, appLaunchBanDelay);
    // logger.Log(LOG_SYSTEM,
    //     u"component=config section=logging directory=\"%s\" packet_dump=%s logd_enabled=%s",
    //     logDirectory ? logDirectory : u".", BoolText(dumpPacket), BoolText(useLogd))
    // logger.Log(LOG_SYSTEM,
    //     u"[config] Log directory: \"%s\". Packet dump enabled: %s. LogD enabled: %s.",
    //     logDirectory ? logDirectory : u".", BoolText(dumpPacket), BoolText(useLogd))
    L2_LOG(logger, LOG_SYSTEM,
        u"[config] Log directory: \"%s\". Packet dump enabled: %s; LogD enabled: %s.",
        logDirectory ? logDirectory : u".", BoolText(dumpPacket), BoolText(useLogd));
    if (useLogd) {
        // logger.Log(LOG_SYSTEM, u"component=config section=logd address=%s port=%d",
        //     InetToStr(&logdAddr, address.data(), address.size()), logdPort)
        // logger.Log(LOG_SYSTEM, u"[config] LogD endpoint: %s:%d.",
        //     InetToStr(&logdAddr, address.data(), address.size()), logdPort)
        L2_LOG(logger, LOG_SYSTEM, u"[config] LogD endpoint: %s:%d.",
            InetToStr(&logdAddr, address.data(), address.size()), logdPort);
    }
    // logger.Log(LOG_SYSTEM,
    //     u"component=config section=gameguard enabled=%s check=%s notify_result=%s active=%d update_timer=%s log_mode=%d log_invalid=%s",
    //     BoolText(useGameGuard), BoolText(checkGameGuard), BoolText(useNotifyCSResult),
    //     ggNumActive, BoolText(ggUseUpdateTimer), ggUseLog, BoolText(ggLogInvalid))
    // logger.Log(LOG_SYSTEM,
    //     u"[config] GameGuard enabled: %s. Checking: %s. Result notifications: %s.",
    //     BoolText(useGameGuard), BoolText(checkGameGuard), BoolText(useNotifyCSResult))
    L2_LOG(logger, LOG_SYSTEM,
        u"[config] GameGuard enabled: %s; checking: %s; result notifications: %s.",
        BoolText(useGameGuard), BoolText(checkGameGuard), BoolText(useNotifyCSResult));
    // logger.Log(LOG_SYSTEM,
    //     u"[config] GameGuard active setting: %d. Update timer: %s. Log mode: %d. Invalid-result logging: %s.",
    //     ggNumActive, BoolText(ggUseUpdateTimer), ggUseLog, BoolText(ggLogInvalid))
    L2_LOG(logger, LOG_SYSTEM,
        u"[config] GameGuard active setting: %d; update timer: %s; log mode: %d; invalid-result logging: %s.",
        ggNumActive, BoolText(ggUseUpdateTimer), ggUseLog, BoolText(ggLogInvalid));
    // logger.Log(LOG_SYSTEM, u"component=config section=report_mail enabled=%s", BoolText(useReportMail))
    // logger.Log(LOG_SYSTEM, u"[config] Report mail enabled: %s.", BoolText(useReportMail))
    L2_LOG(logger, LOG_SYSTEM, u"[config] Report mail enabled: %s.", BoolText(useReportMail));
    if (useReportMail) {
        // logger.Log(LOG_SYSTEM, u"component=config section=report_mail server=%s port=%d",
        //     reportMailServer ? reportMailServer : u"(unset)", reportMailPort)
        // logger.Log(LOG_SYSTEM, u"[config] Report mail server: %s:%d.",
        //     reportMailServer ? reportMailServer : u"(unset)", reportMailPort)
        L2_LOG(logger, LOG_SYSTEM, u"[config] Report mail server: %s:%d.",
            reportMailServer ? reportMailServer : u"(unset)", reportMailPort);
    }
}
