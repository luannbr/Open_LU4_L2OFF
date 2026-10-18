#pragma once

#include "AuthType.h"
#include "Lock.h"
#include "Shared/src/ScopedLockGuards.h"

#include <cstdint>
#include <map>

class CSocketServer;
class CClientSession;
struct WebToken;

enum EBillActionFlag : std::int32_t {
    ACTION_START = 0,
    ACTION_NOTIFY = 1,
    ACTION_STOP = 2,
};

// Original owner: Src/accountdb.h. L2Authd.pdb type 0x4897, target size
// 0x7C. Native STL nodes use the host ABI in the portable build, while the
// actual three maps and three locks remain the PDB-recorded members.
class AccountDB {
private:
    std::map<int, LoginUser*> usermap;
    std::map<int, PreLoginUser> preUserMap;
    std::map<int, LoginUser*> reconnUserMap;
    CLock m_lock;
    CLock m_preLock;
    CLock m_reconnLock;

    static std::int32_t sBillLogTraceId;
    static std::int32_t sReconnectKey;

    void GetProfileFromPortalUserData(
        CAccount& account,
        const PortalUser& portalUser);
    bool AuthenticateOtp(
        CClientSession* client,
        PreLoginUser& user);
    bool AuthenticateSecurityCard(
        CClientSession* client,
        PreLoginUser& user);
    bool IsTokenLoginUser(const PreLoginUser& user) {
        // L2Authd.exe 0x0041A7D0, Src/accountdb.h:36-38.
        return user.tokenInfo.token[0] != '\0';
    }
    bool IsBase64ExternalToken(
        const int companyNo,
        const int affiliatedGameId);
    bool IsExternalLoginAuthProvider(
        const char* authProviderCode);
    bool IsExternalLoginCompanyNo(const int companyNo);
    int GetCompanyNo(const char* authProviderCode);

public:
    char AboutToPlay(
        CClientSession* pSession,
        int uid,
        LoginUser* pLoginUser,
        int time_left,
        int serverid,
        int stat,
        int reservations);
    char CheckUserTime(int uid, int* remainingTime);
    char CheckUserDuration(int uid, int* remainingTime);
    char CheckPersonalPayStat(
        CClientSession* client,
        LoginUser* user,
        int uid);
    char ChargePersonalDBValue(
        int uid,
        LoginUser* user,
        CClientSession* client,
        int* remainingTime);
    bool LoginAccount(
        CClientSession* pSession,
        char* name,
        char* password,
        int subscription,
        unsigned short cdkind,
        char clientInstances,
        int otpAnswer);
    bool LoginAccountWsid(
        CClientSession* pSession,
        WebSessionId& webSessionId,
        WebToken& webToken,
        int subscription,
        unsigned short cdkind,
        char clientInstances);
    bool ExternalTokenLogin(
        CClientSession* client,
        int companyNo,
        unsigned char* token,
        int affiliatedGameId,
        int gameId,
        int subscription,
        unsigned short cdKind,
        char clientInstances,
        in_addr address);
    bool AuthenticateUser(
        CClientSession* pSession,
        PreLoginUser* pPreLogin,
        bool checkPassword,
        bool autoCreate = false);
    bool AuthenticateUserPortal(
        CClientSession* client,
        PreLoginUser* user);
    bool AuthenticateUserSecondary(
        CClientSession* client,
        PreLoginUser& user);
    bool AuthorizeUser(
        CClientSession* client,
        PreLoginUser& user);
    bool AuthorizeClientSideLogin(
        CClientSession* client,
        PreLoginUser& user);
    char AuthorizeServerSideLogin(
        PreLoginUser& user,
        in_addr address,
        LoginUser* loginUser);
    char LoginServerSide(
        int uid,
        char* allianceUserKey,
        PreLoginUser& preLogin,
        int serverid,
        int stat,
        in_addr clientIp);
    void ReConnectPBServer();
    bool ThirdPartyTokenLogin(
        CClientSession* client,
        int companyNo,
        unsigned char* token,
        int affiliatedGameId,
        int gameId,
        int subscription,
        unsigned short cdKind,
        char clientInstances,
        in_addr address);
    bool LoginToken(
        CClientSession* client,
        char* authProviderCode,
        char* token,
        int affiliatedGameId,
        int gameId,
        int subscription,
        unsigned short cdKind,
        char clientInstances,
        in_addr address);

    AccountDB();
    virtual ~AccountDB();

    bool KickAccount(
        int uid,
        char reason,
        bool sendToServer,
        bool removeAccount);
    bool KickAccountByAccount(
        const char* account,
        char reason,
        bool sendToServer);
    int BanAccount(
        int uid,
        short reason,
        char* account,
        unsigned int accountCapacity,
        int* result);
    char RegAccount(int uid, LoginUser* user);
    int checkInGame(int uid, int serverId);
    bool logoutAccount(int uid, int reason);
    bool recordGamePlayTime(int uid, int serverId);
    bool StartCharge(
        CSocketServer* socket,
        int uid,
        LoginUser& user,
        ChargeType type);
    int UpdateSessionId(
        int uid,
        unsigned int sessionId,
        int oneTimeKey,
        int reconnectKey);
    void TimerCallback(int uid);
    void QuitGameCallback(int uid);
    std::uint32_t FindSessionId(
        int uid,
        int serverId,
        bool checkServer,
        char* otherServer,
        char* account);
    std::uint32_t FindSessionId(int uid, bool checkServer);
    void RemoveAll();
    void RemoveAll(int serverId);
    int GetUserNum() {
        // L2Authd.exe 0x0042E590, Src/accountdb.h:81-88.
        int result = 0;
        {
            L2_GUARD(m_lock);
            result = static_cast<int>(usermap.size());
        }
        return result;
    }
    bool FindAccount(int uid);
    bool FindAccount(int uid, LoginUser** user);
    bool FindAccount(
        int uid,
        char* account,
        int* loginFlag,
        int* warnFlag,
        int* stat,
        int* md5Key,
        in_addr* address);
    char FindAccount(int uid, char* account);
    bool WriteUserData(
        int uid,
        short length,
        const unsigned char* data);
    char UserTimeLogin(
        int uid,
        LoginUser* user,
        int* remainingTime);
    bool GetAccountInfo(
        int uid,
        char* account,
        int* loginFlag,
        int* warnFlag,
        int* md5Key,
        unsigned int* sessionId);
    bool GetAccountInfoForIPStop(
        int uid,
        char* account,
        int* payStat,
        in_addr* address,
        std::int32_t* loginTime);
    bool GetPortalUidByGameUid(int uid, int* portalUid);
    int GetBillTraceId();
    bool SetPBData(
        int uid,
        char valueType,
        int remainTime,
        int reservations,
        unsigned char* valueId,
        char computationBasis,
        int itemId);
    void WriteBillingData(
        int uid,
        EBillActionFlag action,
        const LoginUser& user);
    int GetCharNum(int uid);
    bool CreateChar(int uid, int charId, char serverId);
    bool DelChar(int uid, int charId, char serverId);
    bool UpdateCharLocation(
        int uid,
        int oldCharId,
        char oldServerId,
        int newCharId,
        int newServerId);
    bool GetUserInfo(
        int uid,
        char& age,
        char& gender);
    unsigned char GetCardKeyNSaveState(
        const int uid,
        const int cardNumber,
        PreLoginUser* user);
    bool CheckSecurityPass(const int uid);
    unsigned char CheckSecurityCardValue(
        const int uid,
        const int answer,
        PreLoginUser* user);
    bool IsPermittedServer(
        int serverId,
        char* forbiddenServers,
        unsigned int capacity);
    char DetermineChargeServer(const LoginUser* user);
    int GetReconnectKey();
    bool QuitGameCharge(
        int uid,
        [[maybe_unused]] int useTime,
        int serverId,
        int* result);
    void EndChargePersonalSepcific(int uid, int useTime);
    void EndChargePersonalSepcificDate(
        int uid,
        int useTime,
        int payStat,
        std::int32_t loginTime);
    void WritePlayLog(const LoginUser& user, int uid);
    char UpdateSession(
        int uid,
        int oneTimeKey,
        int reconnectKey,
        CClientSession* client);
    bool UpdateChargeServerData(
        int uid,
        int source,
        char valueType,
        int remainTime,
        int reservations,
        char computationBasis,
        int itemId);
    bool UpdateChargeData(
        int uid,
        int chargeServerId,
        int stat,
        UserMode mode);
    void SetAvailableServersData(
        int uid,
        int size,
        unsigned char* availableServers,
        LoginUser* user);
    bool SetWaitUser(int uid, int serverId);
    bool EndCharge(
        int uid,
        LoginUser& user,
        int endTime);
    void LogoutProcess(int uid, short reason);
    void PostLogoutProcess(
        int uid,
        const LoginUser& user);
    void SendKickMessage(
        int kind,
        int uid,
        int serverId,
        unsigned int sessionId,
        char* account,
        char reason);
    char GetComputationBasis(const LoginUser& user);
    bool UpdateUserLevel(int uid, int userLevel);
    void InitLoginUserData(
        LoginUser* loginUser,
        const PreLoginUser& preLoginUser,
        const CAccount& account,
        in_addr address,
        int md5Key,
        unsigned int sessionId);
    // Present in the PDB class declaration, but the selected executable emits
    // no function body or public symbol for it.
    void InitializeSecurityServiceStatus();
    int MarkActivateTime(
        int uid,
        std::int32_t activateTime,
        unsigned char* information);
    bool UpdateCharacterLevel(
        int uid,
        unsigned char serverId,
        int characterId,
        int level,
        std::int32_t updateTime);
    bool ForceUpdateCharacterLevel(
        int uid,
        unsigned char serverId,
        int characterId,
        int level,
        std::int32_t updateTime);
    bool DeleteCharacterLevel(
        int uid,
        unsigned char serverId,
        int characterId);
    bool GetCharacterMaxLevel(
        int uid,
        unsigned char& serverId,
        int& characterId,
        int& level);
    int RequestAuthnTokenByPAServer(
        CSocketServer* socket,
        LoginUser* user,
        char* appId);
    bool ExternalSteamTokenLogin(
        CClientSession* client,
        int companyNo,
        unsigned char* token,
        int affiliatedGameId,
        int gameId,
        int subscription,
        unsigned short cdKind,
        char clientInstances,
        in_addr address);
};

extern AccountDB accountdb;
