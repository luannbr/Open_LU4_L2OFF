#include "Config.h"

#include "Platform/Guid.h"
#include "Shared/src/Runtime/OperatorLog.h"
#include "L2AuthD.h"
#include "Log.h"
#include "Parser.h"
#include "ServiceMgr.h"
#include "Util.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <sstream>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <Windows.h>
#endif

Config config;

namespace {

constexpr int kMillisecondsPerSecond = 1000;
constexpr int kMaximumTimeExpZoneCount = 4;

// Original implementation kept. Fatal configuration errors use the operator format.
// [[noreturn]] void ReportFatalConfigMessage(const char* message) {
//     if (g_serviceManager.IsServiceMode() == 1) {
//         CFileLog defaultErrorLog("error")
//         defaultErrorLog.SetDirectory(".")
//         defaultErrorLog.AddLog(
//             LOG_ERROR,
//             "%s",
//             message)
//         std::exit(1)
//     }
// #if defined(_WIN32)
//     ::MessageBoxA(nullptr, message, "Error", MB_ICONERROR)
// #else
//     // MessageBoxA is the only target-side foreground reporting transport.
//     // stderr is its non-Windows host boundary. The service-mode file path
//     // above remains the exact product branch.
//     std::fprintf(stderr, "%s\n", message)
// #endif
//     std::exit(1)
// }
[[noreturn]] void ReportFatalConfigMessage(const char* message) {
    // runtime::logging::Write("authd", runtime::logging::Level::Error, message)
    runtime::logging::Write("authd", runtime::logging::Level::Error, message, __FILE__);
    if (g_serviceManager.IsServiceMode() == 1) {
        CFileLog defaultErrorLog("error");
        defaultErrorLog.SetDirectory(".");
        // defaultErrorLog.AddLog(LOG_ERROR, "%s", message)
        L2_LOG_ERROR(defaultErrorLog, "%s", message);
        std::exit(1);
    }
#if defined(_WIN32)
    if (config.userInterface == runtime::UserInterface::Gui) {
        ::MessageBoxA(nullptr, message, "Error", MB_ICONERROR);
    }
#endif
    std::exit(1);
}

} // namespace

// L2Authd.exe 0x00424CD0, Src/config.h:38.
ConnectionInformation::ConnectionInformation()
    : host{}, port(0) {}

// L2Authd.exe 0x0043F5B0, Src/config.h:39.
ConnectionInformation::ConnectionInformation(
    in_addr initialHost,
    unsigned short initialPort)
    : host(initialHost), port(initialPort) {}

// L2Authd.exe 0x00420EF0, Src/config.cpp:59-60. Only the two vectors and map
// are constructed. Static zero-initialization supplies the scalar state of
// the global `config` object before this constructor runs.
Config::Config() = default;

// L2Authd.exe 0x00420F70, Src/config.cpp:63-64.
Config::~Config() = default;

// L2Authd.exe 0x00420FB0, Src/config.cpp:67-509.
void Config::Load(const char* filename) {
    int size = 0;
    char* const pBuf = static_cast<char*>(
        LoadBinary(filename, &size, false));
    if (pBuf == nullptr) {
        // Noise: LoadBinary now reports the exact failing stage and existing-values/defaults policy. Omit this generic duplicate.
        // Log.AddLog(LOG_ERROR, "Can't load %s", filename)
    } else {
        std::istringstream inp(
            std::string(pBuf, static_cast<std::size_t>(size)));
        inp >> std::noskipws;

        enum State {
            eWaitKey = 0,
            eWaitValue = 2,
            eInComment = 3,
        };

        State state = eWaitKey;
        int line = 1;
        std::array<char, 257> key{};
        std::array<char, 257> value{};
        Token token = eEOF;
        while ((token = GetToken(inp, value.data(), true)) != eEOF) {
            if (state == eInComment) {
                if (token == eNewLine) {
                    state = eWaitKey;
                    ++line;
                }
                continue;
            }

            switch (token) {
            case eWord:
                if (state == eWaitKey) {
                    std::strcpy(key.data(), value.data());
                } else if (state == eWaitValue) {
                    const auto result = map.insert(
                        std::make_pair(
                            std::string(key.data()),
                            std::string(value.data())));
                    if (!result.second) {
                        // Log.AddLog(
                        //     LOG_ERROR,
                        //     "Insert twice %s at line %d in %s",
                        //     key.data(),
                        //     line,
                        //     filename)
                        // Log.AddLog(
                        //     LOG_WARN,
                        //     "component=config event=duplicate_key path=\"%s\" line=%d key=redacted action=keep_first_value",
                        //     filename, line)
                        // Log.AddLog(
                        //     LOG_WARN,
                        //     "[config] Duplicate key in \"%s\" at line %d. Keeping its first value.",
                        //     filename, line)
                        // Original: L2_LOG_WARN(Log,
                        //     "[config] Duplicate key in \"%s\" at line %d. Keeping its first value.",
                        //     filename, line)
                        // L2_LOG_WARN(Log,
                        //     "Duplicate configuration key \"%s\" in \"%s\" at line %d. Keeping its first value.",
                        //     key.data(), filename, line)
                        L2_LOG_WARN(Log,
                            "[config] Duplicate configuration key \"%s\" in \"%s\" at line %d; keeping its first value.",
                            key.data(), filename, line);
                    }
                } else {
                    // Log.AddLog(
                    //     LOG_ERROR,
                    //     "Invalid state at line %d in %s",
                    //     line,
                    //     filename)
                    // Log.AddLog(
                    //     LOG_ERROR,
                    //     "component=config event=invalid_parser_state path=\"%s\" line=%d state=%d action=ignore_word",
                    //     filename, line, static_cast<int>(state))
                    // Log.AddLog(
                    //     LOG_ERROR,
                    //     "[config] Invalid parser state in \"%s\" at line %d (state %d). Ignoring this word.",
                    //     filename, line, static_cast<int>(state))
                    L2_LOG_ERROR(Log,
                        "[config] Invalid parser state in \"%s\" at line %d (state %d); ignoring this word.",
                        filename, line, static_cast<int>(state));
                }
                break;

            case eSemiColon:
                state = eInComment;
                break;

            case eEqual:
                state = eWaitValue;
                break;

            case eNewLine:
                state = eWaitKey;
                ++line;
                break;

            default: {
                // Noise: ReportFatalConfigMessage already emits this parse failure. Omit the duplicate call.
                // Sensitive data: an invalid config token may be a password. Report only its kind and location.
                // Log.AddLog(
                //     LOG_ERROR,
                //     "Invalid token %s at line %d in %s",
                //     value.data(),
                //     line,
                //     filename)
                // std::array<char, 261> msgString{}
                // std::snprintf(
                //     msgString.data(),
                //     msgString.size(),
                //     "Invalid token '%s' at line %d in '%s'",
                //     value.data(),
                //     line,
                //     filename)
                std::array<char, 8192> msgString{};
                // std::snprintf(msgString.data(), msgString.size(),
                //     "component=config event=invalid_token token_kind=%d line=%d path=\"%s\". Startup aborted",
                //     static_cast<int>(token), line, filename)
                // std::snprintf(msgString.data(), msgString.size(),
                //     "component=config event=invalid_token token_kind=%d line=%d path=\"%s\" action=abort_startup exit_code=1",
                //     static_cast<int>(token), line, filename)
                std::snprintf(msgString.data(), msgString.size(),
                    "[config] Invalid token of kind %d at line %d in \"%s\". Startup aborted (exit code 1).",
                    static_cast<int>(token), line, filename);
                ReportFatalConfigMessage(msgString.data());
            }
            }
        }

        // Source bug: the shipping function allocates through operator new[]
        // but calls scalar operator delete at 0x004213F3. The mismatch is
        // undefined behavior and stops AddressSanitizer builds.
        // ::operator delete(pBuf);
        ::operator delete[](pBuf);
    }

    // const int debugLogging = GetInt("DebugLogging", 1)
    // runtime::logging::authDebugLoggingEnabled.store(debugLogging != 0, std::memory_order_relaxed)
    // if (debugLogging != 0 && debugLogging != 1) {
    //     L2_LOG_WARN(Log,
    //         "[config] DebugLogging must be 0 or 1. Received %d. Retaining the default enabled debug logging.",
    //         debugLogging)
    // }
    const char* const interfaceValue = Get("UserInterface");
    userInterface = runtime::ReadUserInterface(interfaceValue ? interfaceValue : "console");
    const char* const debugValue = Get("DebugLogging");
    const bool debugValid = !debugValue || std::string_view(debugValue) == "0" ||
        std::string_view(debugValue) == "1";
    runtime::logging::authDebugLoggingEnabled.store(
        !debugValue || std::string_view(debugValue) != "0", std::memory_order_relaxed);
    if (!debugValid) {
        L2_LOG_WARN(Log,
            "[config] DebugLogging must be exactly 0 or 1. Invalid value; using the default enabled debug logging.");
    }

    serverPort = GetInt("serverPort", 0);
    serverExPort = GetInt("serverExPort", 0);
    serverIntPort = GetInt("serverIntPort", 0);
    serverMiPort = GetInt("serverMiPort", 0);
    worldPort = GetInt("WorldPort", 0);
    numDBConn = GetInt("DBConnectionNum", 64);
    numServerThread = GetInt("numServerThread", 0);
    numServerIntThread = GetInt("numServerIntThread", 0);
    gameId = GetInt("GameID", 0);
    affiliatedGameId = GetInt("AffiliatedGameID", 0);
    vmGameId = GetInt("vmGameID", gameId);
    SocketTimeOut = GetInt("SocketTimeOut", 0) *
        kMillisecondsPerSecond;
    SocketLimit = GetInt("SocketLimit", 0);
    AcceptCallNum = GetInt("AcceptCallNum", 0);
    PacketSizeType = GetInt("PacketSizeType", 0);
    logDirectory = Get("logDirectory");
    ProtocolVer = GetInt("ProtocolVersion", 0);
    ReadLocalServerList = GetBool("ReadLocalServerList", false);
    OneTimeLogOut = GetBool("OneTimeLogOut", false);
    GMCheckMode = GetBool("GMCheckMode", false);
    Country = GetInt("CountryCode", 0);
    UserData = GetBool("UserData", false);
    DevConnectOuter = GetBool("DevConnectOuter", false);
    DevIP = Get("DevServerIP");
    DumpPacket = GetBool("DumpPacket", false);

    ChargePriority[0] = 1;
    ChargePriority[1] = 0;
    ChargePriority[2] = 2;
    ChargePriority[3] = 3;
    char* chargePriorityStr =
        const_cast<char*>(Get("ChargePriority"));
    if (chargePriorityStr != nullptr) {
        int i = 0;
        char* tokenValue = std::strtok(chargePriorityStr, ",");
        do {
            ChargePriority[i] = static_cast<char>(
                std::atoi(tokenValue));
            ++i;
            tokenValue = std::strtok(nullptr, ",");
        } while (tokenValue != nullptr);
    }

    UseIPServer = GetBool("UseIPServer", false);
    IPServer = GetInetAddr("IPServer");
    IPPort = GetInt("IPPort", 0);
    IPConnectInterval = GetInt("IPInterval", 0) *
        kMillisecondsPerSecond;
    UseLogD = GetBool("uselogd", false);
    LogDIP = GetInetAddr("logdip");
    LogDPort = GetInt("logdport", 0);
    LogDReconnectInterval = GetInt("logdconnectinterval", 60) *
        kMillisecondsPerSecond;
    RestrictGMIP = GetBool("RestrictGMIP", false);
    GMIP = GetInetAddr("GMIP");
    IPAccessLimit = GetInt("IPAccessLimit", 0);
    FreeServer = GetInt("FreeServer", 1);
    supportReconnect = GetBool("supportReconnect", false);
    mailServer = Get("mailServer");
    mailFrom = Get("mailFrom");
    mailTo = Get("mailTo");
    AutoKickAccount = GetBool("AutoKickAccount", false);
    autoCreateAccounts = GetBool("AutoCreateAccounts", true);
    newEncrypt = GetInt("NewEncrypt", 0);
    chargeWhenServerDown = GetBool("chargeWhenServerDown", false);
    usePacketLog = GetInt("UsePacketLog", 0);
    useSecurityCard = GetBool("UseSecurityCard", false);
    securityFailureCount = GetInt("securityFailureCount", 0);
    deprecateSecurityCard = GetBool("deprecateSecurityCard", false);
    useUnderAgeTimeLimit = GetBool("UseUnderAgeTimeLimit", false);
    underAgeTimeLimitFrom = GetInt("underAgeTimeLimitFrom", 0);
    underAgeTimeLimitTo = GetInt("underAgeTimeLimitTo", 0);
    useForbiddenServerSetting =
        GetBool("UseForbiddenServerSetting", false);
    useMultiClientForPCCafeOption =
        GetBool("UseMultiClientForPCCafeOption", false);
    useDormantFlag = GetBool("UseDormantFlag", false);
    defaultTopServerId = static_cast<char>(
        GetInt("DefaultTopServer", 0));
    useNotifyCSResult = GetBool("UseNotifyCSResult", false);
    usePBServer = GetBool("UsePBServer", false);
    PBSIP = GetInetAddr("PBServerIP");
    PBSPort = GetInt("PBServerPort", 3000);
    PBSReconnectInterval = GetInt("PBSReconnectInterval", 60) *
        kMillisecondsPerSecond;
    PBType = GetInt("PBType", 1);
    OTPRetryCount = GetInt("OTPRetryCount", 3);
    OTPAnswerInputedFlag = GetInt("OTPAnswerInputedFlag", 0);
    OTPNotAssignedValue = GetInt("OTPNotAssignedValue", 0);

    usePAServer = GetBool("UsePAServer", false);
    PAConnectionCount = GetInt("PAConnectionCount", 1);
    if (PAConnectionCount == 1) {
        ConnectionInformation onePAConnection;
        onePAConnection.host = GetInetAddr("PAIP");
        onePAConnection.port = static_cast<unsigned short>(
            GetInt("PAPort", 0x2749));
        PAHostInfo.push_back(onePAConnection);
    } else {
        for (int i = 1; i <= PAConnectionCount; ++i) {
            std::array<char, 260> strPAIPConfig{};
            std::array<char, 260> strPAPortConfig{};
            std::snprintf(
                strPAIPConfig.data(),
                strPAIPConfig.size(),
                "PAIP_%d",
                i);
            std::snprintf(
                strPAPortConfig.data(),
                strPAPortConfig.size(),
                "PAPort_%d",
                i);
            ConnectionInformation manyPAConnection;
            manyPAConnection.host =
                GetInetAddr(strPAIPConfig.data());
            manyPAConnection.port = static_cast<unsigned short>(
                GetInt(strPAPortConfig.data(), 0x2749));
            PAHostInfo.push_back(manyPAConnection);
        }
    }
    PAReconnectInterval = GetInt("PAReconnectInterval", 60) *
        kMillisecondsPerSecond;

    useWSM = GetBool("UseWSM", false);
    WSMIP = GetInetAddr("WSMIP");
    WSMPort = GetInt("WSMPort", 0x2743);
    WSMReconnectInterval = GetInt("WSMReconnectInterval", 60) *
        kMillisecondsPerSecond;

    useGpProxy = GetBool("UseGpProxy", false);
    gpProxyConnCount = GetInt("GpProxyConnectionCount", 1);
    for (int i = 1; i <= gpProxyConnCount; ++i) {
        std::array<char, 261> ipCfgKey{};
        std::array<char, 261> portCfgKey{};
        std::strcpy(ipCfgKey.data(), "GPProxyIP");
        std::strcpy(portCfgKey.data(), "GPProxyPort");
        if (gpProxyConnCount > 1) {
            std::snprintf(
                ipCfgKey.data(),
                ipCfgKey.size(),
                "GPProxyIP_%d",
                i);
            std::snprintf(
                portCfgKey.data(),
                portCfgKey.size(),
                "GPProxyPort_%d",
                i);
        }
        ConnectionInformation connInfo;
        connInfo.host = GetInetAddr(ipCfgKey.data());
        connInfo.port = static_cast<unsigned short>(
            GetInt(portCfgKey.data(), 0x4e22));
        gpProxyHosts.push_back(connInfo);
    }
    gpProxyReconnectInterval =
        GetInt("GPProxyReconnectInterval", 10) *
        kMillisecondsPerSecond;
    UseNpSecAuth = GetBool("useNpSecAuth", true);
    checkOtpStatusFromDb = GetBool("checkOtpStatusFromDb", false);
    checkTasStatusFromDb = GetBool("checkTasStatusFromDb", false);
    ForceSkipSecAuthIfOTPPassed =
        GetBool("ForceSkipSecAuthIfOTPPassed", false);

    useFatigueSystem = GetBool("UseFatigueSystem", false);
    useExternalFatigueSystem =
        GetBool("UseExternalFatigueSystem", false);
    fatigueRecoveryTime = GetInt("FatigueRecoveryTime", 18000);
    fatigueAffectedAge = GetInt("FatigueAffectedAge", 0);
    fatigueConcurrentPlayLimit =
        GetInt("FatigueConcurrentPlayLimit", 10);
    externalFatigueCompanyCode =
        GetInt("ExternalFatigueCompanyCode", 1);
    externalFatigueDllName = Get("ExternalFatigueDllName");
    externalFatigueWorkThreadCount =
        GetInt("ExternalFatigueWorkThreadCount", 64);

    useTimeExpZone = GetBool("UseTimeExpZone", false);
    resetTimeExpZoneCapsule =
        GetBool("ResetTimeExpZoneCapsule", true);
    totalTimeExpZoneCount = GetInt("TimeExpZoneCount", 2);
    if (totalTimeExpZoneCount > kMaximumTimeExpZoneCount) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "totalTimeExpZoneCount[%d] is grater than max[%d], apply it with max",
        //     totalTimeExpZoneCount,
        //     kMaximumTimeExpZoneCount)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=config event=value_clamped key=TimeExpZoneCount configured=%d maximum=%d action=use_maximum",
        //     totalTimeExpZoneCount, kMaximumTimeExpZoneCount)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[config] TimeExpZoneCount is %d. Limiting it to the maximum of %d.",
        //     totalTimeExpZoneCount, kMaximumTimeExpZoneCount)
        L2_LOG_WARN(Log,
            "[config] TimeExpZoneCount is %d; limiting it to the maximum of %d.",
            totalTimeExpZoneCount, kMaximumTimeExpZoneCount);
        totalTimeExpZoneCount = kMaximumTimeExpZoneCount;
    } else if (totalTimeExpZoneCount <= 0) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "invalid totalTimeExpZoneCount[%d], zone based fatigue system feature is off",
        //     totalTimeExpZoneCount)
        // Log.AddLog(
        //     useTimeExpZone ? LOG_WARN : LOG_NORMAL,
        //     "component=config event=feature_disabled feature=time_exp_zone key=TimeExpZoneCount configured=%d reason=nonpositive_count action=disable_zone_and_capsule_reset",
        //     totalTimeExpZoneCount)
        // Log.AddLog(
        //     useTimeExpZone ? LOG_WARN : LOG_NORMAL,
        //     "[config] TimeExpZoneCount is %d. Disabling time-exp zones and capsule resets because the count must be positive.",
        //     totalTimeExpZoneCount)
        // L2_LOG(Log, useTimeExpZone ? LOG_WARN : LOG_NORMAL,
        //     "[config] TimeExpZoneCount is %d. Disabling time-exp zones and capsule resets because the count must be positive.",
        //     totalTimeExpZoneCount)
        // Noise: A nonpositive count changes nothing when both dependent features are already disabled.
        if (useTimeExpZone || resetTimeExpZoneCapsule) {
            L2_LOG(Log, useTimeExpZone ? LOG_WARN : LOG_NORMAL,
                "[config] TimeExpZoneCount is %d. Disabling time-exp zones and capsule resets because the count must be positive.",
                totalTimeExpZoneCount);
        }
        useTimeExpZone = false;
        resetTimeExpZoneCapsule = false;
        totalTimeExpZoneCount = 0;
    }
    if (totalTimeExpZoneCount > 0) {
        expCapsuleList = Get("ExpCapsuleList");
        if (expCapsuleList == nullptr) {
            // Noise: Missing capsule data needs no warning when UseTimeExpZone is already disabled.
            // Log.AddLog(
            //     LOG_ERROR,
            //     "there is no ExpCapsuleList, zone based fatigue system feature is off")
            if (useTimeExpZone) {
                // Log.AddLog(
                //     LOG_WARN,
                //     "component=config event=feature_disabled feature=time_exp_zone reason=missing_capsule_list key=ExpCapsuleList action=disable_zone_and_capsule_reset")
                // Log.AddLog(
                //     LOG_WARN,
                //     "[config] ExpCapsuleList is missing. Disabling time-exp zones and capsule resets.")
                L2_LOG_WARN(Log,
                    "[config] ExpCapsuleList is missing. Disabling time-exp zones and capsule resets.");
            }
            useTimeExpZone = false;
            resetTimeExpZoneCapsule = false;
            totalTimeExpZoneCount = 0;
        } else {
            [[maybe_unused]] const std::size_t capsuleListLenth =
                std::strlen(expCapsuleList);
            int zoneIdx = 0;
            std::memset(resetCapsuleInfo, 0, sizeof(resetCapsuleInfo));
            char* pAllZoneCapsuleInfo =
                const_cast<char*>(expCapsuleList);
            char* pOneZoneCapsuleInfo = nullptr;
            while ((pOneZoneCapsuleInfo = StrTok_R(
                        pAllZoneCapsuleInfo,
                        ";",
                        &pAllZoneCapsuleInfo)) != nullptr) {
                StrCopy(
                    resetCapsuleInfo[zoneIdx],
                    pOneZoneCapsuleInfo,
                    sizeof(resetCapsuleInfo[zoneIdx]));
                ++zoneIdx;
            }
            const int timeExpZoneCount = zoneIdx;
            if (timeExpZoneCount > totalTimeExpZoneCount) {
                // Log.AddLog(
                //     LOG_ERROR,
                //     "zoneCount[%d] from capsuleInfo is grater than totalTimeExpZoneCount[%d], appied only totalTimeExpZoneCount",
                //     timeExpZoneCount,
                //     totalTimeExpZoneCount)
                // Log.AddLog(
                //     LOG_WARN,
                //     "component=config event=capsule_count_mismatch key=ExpCapsuleList parsed_zones=%d configured_zones=%d action=use_configured_zone_count",
                //     timeExpZoneCount, totalTimeExpZoneCount)
                // Log.AddLog(
                //     LOG_WARN,
                //     "[config] ExpCapsuleList contains %d zones. Using the configured TimeExpZoneCount of %d.",
                //     timeExpZoneCount, totalTimeExpZoneCount)
                L2_LOG_WARN(Log,
                    "[config] ExpCapsuleList contains %d zones; using the configured TimeExpZoneCount of %d.",
                    timeExpZoneCount, totalTimeExpZoneCount);
            }
        }
    }

    FidoDisabled = GetBool("FidoDisabled", false);
    ForceSkipSecAuthIfFIDOPassed =
        GetBool("ForceSkipSecAuthIfFIDOPassed", false);
    ForceSkipSecAuthIfDevicePassed =
        GetBool("ForceSkipSecAuthIfDevicePassed", false);
    useTestServer = GetBool("useTestServer", false);
    usePackageVersionInfo = GetBool("UsePackageVersionInfo", false);
    useBanAccount = GetBool("UseBanAccount", false);
    useSuspendCharge = GetBool("UseSuspendCharge", true);
    useAccountBanGameKick = GetBool("UseAccountBanGameKick", false);
    useGCSideExtendAccount = GetBool("useGCSideExtendAccount", false);
    usePASideExtendAccount = GetBool("usePASideExtendAccount", false);
    unpatchedEuClientLoginFail = GetBool("UnpatchedEuClientLoginFail", false);
    lu4Mode = GetBool("Lu4Mode", false);
    lu4ValidatorPort = GetInt("Lu4ValidatorPort", 27110);
    useChildUsageCheck = GetBool("useChildUsageCheck", false);
    shutdownAge = GetInt("shutdownAge", 0);
    shutdownBeginHour = GetInt("shutdownBeginHour", 0);
    shutdownEndHour = GetInt("shutdownEndHour", 0);
    useExtendPacket = GetBool("useExtendPacket", false);
    useLevelCheck = GetBool("useLevelCheck", false);
    useGradeServer = GetBool("useGradeServer", false);
    GradeIP = GetInetAddr("GradeServerIP");
    GradePort = GetInt("GradeServerPort", 0x276a);
    GradeReconnectInterval = GetInt("GradeReconnectInterval", 5) *
        kMillisecondsPerSecond;
    useUserPlayTimeLimit = GetBool("useUserPlayTimeLimit", false);
    useNoBill = GetBool("useNoBill", false);
    allowF2PMultiClients = GetBool("allowF2PMultiClients", false);
    useReportMail = GetBool("useReportMail", false);
    reportMailServer = Get("reportMailServer");
    reportMailPort = GetInt("reportMailPort", 25);
    reportMailFrom = Get("reportMailFrom");
    reportMailTo = Get("reportMailTo");
    limitIPNumforEachGS = GetInt("limitIPNumforEachGS", 0);
    whiteListIPFile = Get("whiteListIPFile");
    bServiceMode = GetBool("serviceMode", false);
    serialNumber = Get("serialNumber");
    gameAccountDBconnStr = Get("gameAccountDBconnStr");
    secureServiceDBconnStr = Get("secureServiceDBconnStr");
    useExtendWebSessionLifeTime =
        GetBool("extendWebSessionLifeTime", false);
    useDualCharging = GetBool("useDualCharging", false);
    supportSleepAccountActivate =
        GetBool("supportSleepAccountActivate", false);
    useDualLog = GetBool("useDualLog", false);
    convertPortalEmailAccount =
        GetBool("convertPortalEmailAccount", false);
    convertPortalEmailAccountPostfix =
        Get("convertPortalEmailAccountPostfix");
    useIovationProxy = GetBool("useIovationProxy", false);
    iovationProxyIPAddress = GetInetAddr("iovationProxyIPAddress");
    iovationProxyPort = GetInt("iovationProxyPort", 0);
    iovationProxyConnectInterval =
        GetInt("iovationProxyConnectInterval", 30) *
        kMillisecondsPerSecond;
    acceptClientGetIovationFail =
        GetBool("acceptClientGetIovationFail", false);
    useLightTokenServer = GetBool("useLightTokenServer", false);
    lightTokenServerIPAddress =
        GetInetAddr("lightTokenServerIPAddress");
    lightTokenServerPort = GetInt("lightTokenServerPort", 0);
    lightTokenServerConnectInterval =
        GetInt("lightTokenServerConnectInterval", 30) *
        kMillisecondsPerSecond;
    appId = GetGuid("AppId");
    clientAppId = GetGuid("ClientAppId");
    authdAppId = GetGuid("AuthdAppId");
    if (usePAServer && authd::platform::IsNullGuid(appId)) {
        L2_LOG_WARN(Log,
            "[config] UsePAServer is enabled, but AppId is missing or invalid. PA authentication-token fallback will use a zero GUID.");
    }
    if (usePAServer && authd::platform::IsNullGuid(clientAppId)) {
        L2_LOG_WARN(Log,
            "[config] UsePAServer is enabled, but ClientAppId is missing or invalid. PA requests will use a zero client GUID.");
    }
    if (UseNpSecAuth && useGpProxy &&
        authd::platform::IsNullGuid(authdAppId)) {
        L2_LOG_WARN(Log,
            "[config] Secondary authentication through GPProxy is disabled because AuthdAppId is missing or invalid.");
    }
    ignoreWhenIPInCharge = GetBool("ignoreWhenIPInCharge", false);
    useUserUseLog = GetBool("useUserUseLog", false);
    useLobbyCharging = GetBool("useLobbyCharging", true);
    changeServerListUsersInPercentage =
        GetBool("changeServerListUsersInPercentage", false);
}

// L2Authd.exe 0x00422BD0, Src/config.cpp:512-519.
const char* Config::Get(const char* key) const {
    const auto i = map.find(key);
    return i != map.end() ? i->second.c_str() : nullptr;
}

// L2Authd.exe 0x00422C90, Src/config.cpp:522-531.
bool Config::GetBool(const char* key, bool def) const {
    const char* const pValue = Get(key);
    if (pValue == nullptr) {
        return def;
    }

    std::string val(pValue);
    std::transform(
        val.begin(),
        val.end(),
        val.begin(),
        [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
    return val == "yes" || val == "true" || val == "1";
}

// L2Authd.exe 0x00422DC0, Src/config.cpp:534-541.
int Config::GetInt(const char* key, int def) const {
    const char* const pValue = Get(key);
    return pValue != nullptr ? std::atoi(pValue) : def;
}

// L2Authd.exe 0x00422E00, Src/config.cpp:544-553.
in_addr Config::GetInetAddr(const char* key) const {
    in_addr addr{};
    const char* const pValue = Get(key);
    if (pValue != nullptr) {
        addr.s_addr = ::inet_addr(pValue);
    }
    return addr;
}

// L2Authd.exe 0x00422E50, Src/config.cpp:555-574.
_GUID Config::GetGuid(const char* key) const {
    const char* const id = Get(key);
    if (id == nullptr) {
        // Log.AddLog(LOG_WARN, "no config key : %s", key)
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "component=config event=guid_defaulted key=\"%s\" reason=missing action=use_zero_guid",
        //     key)
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "[config] \"%s\" is missing. Using a zero GUID.",
        //     key)
        // Noise: Missing optional GUIDs are reported by Load only when a dependent integration is enabled.
        // L2_LOG_INFO(Log,
        //     "[config] \"%s\" is missing. Using a zero GUID.",
        //     key)
        return _GUID{};
    }

    _GUID val{};
    if (!authd::platform::ParseGuid(id, val)) {
        // Log.AddLog(LOG_ERROR, "invalid format : %s", key)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=config event=guid_defaulted key=\"%s\" reason=invalid_format input=redacted action=use_zero_guid",
        //     key)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[config] \"%s\" is not a valid GUID. Using a zero GUID.",
        //     key)
        L2_LOG_WARN(Log,
            "[config] \"%s\" is not a valid GUID; using a zero GUID.",
            key);
        return _GUID{};
    }
    return val;
}

// L2Authd.exe 0x00422FA0, Src/config.cpp:577-583.
void Config::ParseCmdLine(const char* cmdLine) {
    config.ParsePort(
        cmdLine, "-serverport=", "serverport", serverPort);
    config.ParsePort(
        cmdLine, "-serverexport=", "serverexport", serverExPort);
    config.ParsePort(
        cmdLine, "-serverintport=", "serverintport", serverIntPort);
    config.ParsePort(
        cmdLine, "-servermiport=", "servermiport", serverMiPort);
    config.ParsePort(
        cmdLine, "-worldport=", "worldport", worldPort);
}

// L2Authd.exe 0x00423050, Src/config.cpp:586-595.
void Config::ParsePort(
    const char* cmdLine,
    const char* param,
    const char* paramName,
    int& port) {
    const char* value = std::strstr(cmdLine, param);
    if (value == nullptr) {
        return;
    }
    value += std::strlen(param);
    if (value[0] >= '0' && value[0] <= '9') {
        port = std::atoi(value);
        // Log.AddLog(
        //     LOG_WARN,
        //     "Editor Mode, %s=%d\n",
        //     paramName,
        //     port)
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "component=config event=port_overridden source=command_line key=%s configured_port=%d",
        //     paramName, port)
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "[config] Command-line override: %s uses port %d.",
        //     paramName, port)
        L2_LOG_INFO(Log,
            "[config] Command-line override: %s uses port %d.",
            paramName, port);
    } else {
        // Log.AddLog(LOG_WARN,
        //     "component=config event=port_override_ignored source=command_line key=%s reason=expected_leading_decimal_digit action=keep_configured_port configured_port=%d",
        //     paramName, port)
        // Log.AddLog(LOG_WARN,
        //     "[config] Ignoring the command-line override for %s: its value must begin with a decimal digit. Keeping port %d.",
        //     paramName, port)
        L2_LOG_WARN(Log,
            "[config] Ignoring the command-line override for %s: its value must begin with a decimal digit. Keeping port %d.",
            paramName, port);
    }
}

// L2Authd.exe 0x004230F0, Src/config.cpp:597-602.
bool Config::NpSecAuth() const {
    return UseNpSecAuth && useGpProxy &&
        !authd::platform::IsNullGuid(authdAppId);
}

// L2Authd.exe 0x00423150, Src/config.cpp:29-56.
void* LoadBinary(
    const char* filename,
    int* size,
    bool zeroTerminate) {
    std::FILE* const file = std::fopen(filename, "rb");
    if (file == nullptr) {
        // Log.AddLog(LOG_ERROR,
        //     "component=config event=load_failed path=\"%s\" stage=open error_domain=errno code=%d action=use_existing_values_and_defaults",
        //     filename, errno)
        // Log.AddLog(LOG_ERROR,
        //     "[config] Could not open \"%s\" (errno %d). Keeping existing values and defaults.",
        //     filename, errno)
        L2_LOG_ERROR(Log,
            "[config] Could not open \"%s\" (errno %d). Keeping existing values and defaults.",
            filename, errno);
        return nullptr;
    }

    if (std::fseek(file, 0, SEEK_END) != 0) {
        // Log.AddLog(LOG_ERROR,
        //     "component=config event=load_failed path=\"%s\" stage=seek_end error_domain=errno code=%d action=use_existing_values_and_defaults",
        //     filename, errno)
        // Log.AddLog(LOG_ERROR,
        //     "[config] Could not seek to the end of \"%s\" (errno %d). Keeping existing values and defaults.",
        //     filename, errno)
        L2_LOG_ERROR(Log,
            "[config] Could not seek to the end of \"%s\" (errno %d). Keeping existing values and defaults.",
            filename, errno);
        std::fclose(file);
        return nullptr;
    }
    const long fileSize = std::ftell(file);
    if (fileSize < 0 || std::fseek(file, 0, SEEK_SET) != 0) {
        // Log.AddLog(LOG_ERROR,
        //     "component=config event=load_failed path=\"%s\" stage=%s error_domain=errno code=%d action=use_existing_values_and_defaults",
        //     filename, fileSize < 0 ? "tell_size" : "seek_start", errno)
        // Log.AddLog(LOG_ERROR,
        //     "[config] Could not load \"%s\" while %s (errno %d). Keeping existing values and defaults.",
        //     filename, fileSize < 0 ? "reading the file size" : "seeking to the beginning", errno)
        L2_LOG_ERROR(Log,
            "[config] Could not load \"%s\" while %s (errno %d). Keeping existing values and defaults.",
            filename, fileSize < 0 ? "reading the file size" : "seeking to the beginning", errno);
        std::fclose(file);
        return nullptr;
    }

    const std::size_t allocationSize =
        static_cast<std::size_t>(fileSize) +
        (zeroTerminate ? 1U : 0U);
    void* const buffer =
        ::operator new[](allocationSize, std::nothrow);
    if (buffer == nullptr) {
        // Log.AddLog(LOG_ERROR,
        //     "component=config event=load_failed path=\"%s\" reason=allocation_failed requested_bytes=%zu action=use_existing_values_and_defaults",
        //     filename, allocationSize)
        // Log.AddLog(LOG_ERROR,
        //     "[config] Could not allocate a buffer for \"%s\" (%zu bytes). Keeping existing values and defaults.",
        //     filename, allocationSize)
        // L2_LOG_ERROR(Log,
        //     "[config] Could not allocate a buffer for \"%s\" (%zu bytes). Keeping existing values and defaults.",
        //     filename, allocationSize)
        // Avoid allocating through the file logger while reporting allocation failure.
        char message[1024]{};
        // std::snprintf(message, sizeof(message),
        //     "[config] Cannot allocate %zu bytes for configuration. Keeping existing values and defaults. File: %.700s",
        //     allocationSize, filename)
        std::size_t pathPrefixLength = 0;
        while (pathPrefixLength < 700 && filename[pathPrefixLength] != '\0') ++pathPrefixLength;
        const bool pathTruncated = filename[pathPrefixLength] != '\0';
        std::snprintf(message, sizeof(message),
            "[config] Cannot allocate %zu bytes for configuration. Keeping existing values and defaults. File: %.700s%s",
            allocationSize, filename, pathTruncated ? " [path truncated]" : "");
        runtime::logging::Write("authd", runtime::logging::Level::Error, message, __FILE__);
        std::fclose(file);
        return nullptr;
    }

    [[maybe_unused]] const std::size_t bytesRead = std::fread(
        buffer,
        1,
        static_cast<std::size_t>(fileSize),
        file);
    if (bytesRead != static_cast<std::size_t>(fileSize)) {
        // Log.AddLog(LOG_ERROR,
        //     "component=config event=file_read_incomplete path=\"%s\" expected_bytes=%ld initialized_bytes=%zu reported_bytes=%d reason=%s action=return_buffer_with_unread_tail",
        //     filename, fileSize, bytesRead, static_cast<int>(fileSize),
        //     std::ferror(file) != 0 ? "read_error" : "unexpected_eof")
        // Log.AddLog(LOG_ERROR,
        //     "[config] Incomplete read of \"%s\": expected %ld bytes, initialized %zu, reported %d (%s). The buffer still contains an unread tail.",
        //     filename, fileSize, bytesRead, static_cast<int>(fileSize),
        //     std::ferror(file) != 0 ? "read error" : "unexpected end of file")
        // L2_LOG_ERROR(Log,
        //     "[config] Incomplete read of \"%s\": expected %ld bytes, initialized %zu, reported %d (%s). The buffer still contains an unread tail.",
        //     filename, fileSize, bytesRead, static_cast<int>(fileSize),
        //     std::ferror(file) != 0 ? "read error" : "unexpected end of file")
        // L2_LOG_ERROR(Log,
        //     "[config] Incomplete read of \"%s\": expected %ld bytes, read %zu, passing %d to the parser (%s). Parsing continues with an uninitialized tail. Configuration values may change.",
        //     filename, fileSize, bytesRead, static_cast<int>(fileSize),
        //     std::ferror(file) != 0 ? "read error" : "unexpected end of file")
        L2_LOG_ERROR(Log,
            "[config] Incomplete read of \"%s\": expected %ld bytes, read %llu, passing %d to the parser (%s). Parsing continues with an uninitialized tail; configuration values may change.",
            filename, fileSize, static_cast<unsigned long long>(bytesRead),
            static_cast<int>(fileSize),
            std::ferror(file) != 0 ? "read error" : "unexpected end of file");
    }
    std::fclose(file);
    if (size != nullptr) {
        *size = static_cast<int>(fileSize);
    }
    if (zeroTerminate) {
        static_cast<char*>(buffer)[fileSize] = '\0';
    }
    return buffer;
}
