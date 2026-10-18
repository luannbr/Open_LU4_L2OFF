#pragma once

#include "Platform/WindowsTypes.h"
#include "AuthType.h"
#include "Shared/src/Runtime/UserInterface.h"

#include <map>
#include <string>
#include <vector>

// Original owner: Src/config.h:34-40. L2Authd.pdb type 0x45AA, sizeof 8.
struct ConnectionInformation {
    ConnectionInformation();
    ConnectionInformation(
        in_addr host,
        unsigned short port);

    in_addr host;
    unsigned short port;
};


// Original owner: Src/config.h. Public member names and their order are from
// L2Authd.pdb type 0x7031. The selected VS2013/x86 object is 0xA20 bytes.
// native STL containers and pointers follow the portable host
// ABI, while no product field is renamed, removed, or regrouped.
class Config {
public:
    Config();
    ~Config();

    void Load(const char* filename);
    const char* Get(const char* key) const;
    bool GetBool(const char* key, bool def) const;
    int GetInt(const char* key, int def) const;
    in_addr GetInetAddr(const char* key) const;
    _GUID GetGuid(const char* key) const;
    void ParseCmdLine(const char* cmdLine);
    void ParsePort(
        const char* cmdLine,
        const char* param,
        const char* paramName,
        int& port);

    int serverPort;
    int serverExPort;
    int serverIntPort;
    int serverMiPort;
    int worldPort;
    int numDBConn;
    int numServerThread;
    int numServerIntThread;
    int PacketSizeType;
    int ProtocolVer;
    bool OneTimeLogOut;
    const char* logDirectory;
    int gameId;
    int affiliatedGameId;
    int vmGameId;
    bool ReadLocalServerList;
    bool GMCheckMode;
    bool UserData;
    bool DevConnectOuter;
    bool DumpPacket;
    char ChargePriority[4];
    bool UseIPServer;
    const char* DevIP;
    in_addr IPServer;
    int IPPort;
    int IPConnectInterval;
    int IPAccessLimit;
    int Country;
    int SocketLimit;
    int LimitIOObject;
    int SocketTimeOut;
    int AcceptCallNum;
    bool UseLogD;
    in_addr LogDIP;
    int LogDPort;
    int LogDReconnectInterval;
    bool RestrictGMIP;
    in_addr GMIP;
    int FreeServer;
    bool supportReconnect;
    const char* mailServer;
    const char* mailFrom;
    const char* mailTo;
    bool newServerList;
    bool AutoKickAccount;
    int newEncrypt;
    bool chargeWhenServerDown;
    int usePacketLog;
    bool useSecurityCard;
    int securityFailureCount;
    bool deprecateSecurityCard;
    bool useUnderAgeTimeLimit;
    int underAgeTimeLimitFrom;
    int underAgeTimeLimitTo;
    bool useForbiddenServerSetting;
    bool useMultiClientForPCCafeOption;
    bool useDormantFlag;
    char defaultTopServerId;
    bool useNotifyCSResult;
    bool usePBServer;
    in_addr PBSIP;
    int PBSPort;
    int PBSReconnectInterval;
    int PBType;
    int OTPRetryCount;
    int OTPAnswerInputedFlag;
    int OTPNotAssignedValue;
    bool useFatigueSystem;
    int fatigueRecoveryTime;
    int fatigueAffectedAge;
    int fatigueConcurrentPlayLimit;
    bool useExternalFatigueSystem;
    int externalFatigueCompanyCode;
    const char* externalFatigueDllName;
    int externalFatigueWorkThreadCount;
    bool useTimeExpZone;
    bool resetTimeExpZoneCapsule;
    int totalTimeExpZoneCount;
    const char* expCapsuleList;
    int expCapsuleCount[4];
    char resetCapsuleInfo[4][512];
    bool usePAServer;
    int PAConnectionCount;
    std::vector<ConnectionInformation> PAHostInfo;
    int PAReconnectInterval;
    bool useWSM;
    in_addr WSMIP;
    int WSMPort;
    int WSMReconnectInterval;
    bool useGpProxy;
    int gpProxyConnCount;
    std::vector<ConnectionInformation> gpProxyHosts;
    int gpProxyReconnectInterval;

    bool NpSecAuth() const;

    bool UseNpSecAuth;
    bool checkOtpStatusFromDb;
    bool checkTasStatusFromDb;
    bool ForceSkipSecAuthIfOTPPassed;
    bool FidoDisabled;
    bool ForceSkipSecAuthIfFIDOPassed;
    bool ForceSkipSecAuthIfDevicePassed;
    bool useTestServer;
    bool usePackageVersionInfo;
    bool useBanAccount;
    bool useSuspendCharge;
    bool useAccountBanGameKick;
    bool useGCSideExtendAccount;
    bool usePASideExtendAccount;
    // Portable: see ClientSession.cpp SendFail.
    bool unpatchedEuClientLoginFail;
    // Portable: LU4 client login (Lu4Validator.h).
    bool lu4Mode;
    int lu4ValidatorPort;
    bool useChildUsageCheck;
    int shutdownAge;
    int shutdownBeginHour;
    int shutdownEndHour;
    bool useExtendPacket;
    bool useLevelCheck;
    bool useGradeServer;
    in_addr GradeIP;
    int GradePort;
    int GradeReconnectInterval;
    bool useUserPlayTimeLimit;
    bool useNoBill;
    bool allowF2PMultiClients;
    bool useReportMail;
    const char* reportMailServer;
    int reportMailPort;
    const char* reportMailFrom;
    const char* reportMailTo;
    int limitIPNumforEachGS;
    const char* whiteListIPFile;
    bool bServiceMode;
    const char* serialNumber;
    const char* gameAccountDBconnStr;
    const char* secureServiceDBconnStr;
    bool useExtendWebSessionLifeTime;
    bool useDualCharging;
    bool supportSleepAccountActivate;
    bool useDualLog;
    bool convertPortalEmailAccount;
    const char* convertPortalEmailAccountPostfix;
    bool useIovationProxy;
    in_addr iovationProxyIPAddress;
    int iovationProxyPort;
    int iovationProxyConnectInterval;
    bool acceptClientGetIovationFail;
    bool useLightTokenServer;
    in_addr lightTokenServerIPAddress;
    int lightTokenServerPort;
    int lightTokenServerConnectInterval;
    _GUID appId;
    _GUID clientAppId;
    _GUID authdAppId;
    bool ignoreWhenIPInCharge;
    bool changeServerListUsersInPercentage;
    bool useUserUseLog;
    bool useLobbyCharging;
    runtime::UserInterface userInterface = runtime::UserInterface::Console;
    bool autoCreateAccounts = true;

private:
    std::map<std::string, std::string, iless> map;
};

extern Config config;

void* LoadBinary(
    const char* filename,
    int* size,
    bool zeroTerminate);
