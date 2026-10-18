#pragma once

#include "Platform/WindowsTypes.h"
#include "AuthType.h"
#include "Lock.h"

#include <cstdint>
#include <map>
#include <set>

class CClientSession;
class CIOSocket;
class CPASocket;

// Original owner: Src/paservermgr.h. These PA wire records use byte packing.
// their sizes and every offset are recorded by L2Authd.pdb types 0x64AA,
// 0x83D2 and 0x8335.
#pragma pack(push, 1)
struct VerifyAccountData {
    std::int32_t tid;
    std::int32_t reason;
    std::int32_t gameUid;
    char gameAccount[15];
    std::int32_t portalUid;
    char portalAccount[15];
    std::int32_t birthdate;
    char sex;
    std::int32_t blockCode;
    std::int32_t noticeCode;
    std::int32_t statusCode;
    std::int32_t blockReasonCode;
    std::int32_t userLevel;
    std::int32_t userTypeCode;
    std::int32_t allianceCompanyCode;
    char userId[16];
};

struct VerifyUserResponse {
    VerifyAccountData verifyAccountData;
    char sessionId[16];
};

struct LoginTokenResponse {
    VerifyAccountData verifyAccountData;
    char issuerAppId[16];
    char sessionId[16];
};
#pragma pack(pop)


// Original owner: Src/paservermgr.h. L2Authd.pdb type 0x6B03, target size
// 0x221C. The compiler-generated constructor constructs the two embedded
// login records and otherwise leaves scalar state to Alloc, exactly as the
// selected image does.
struct PAUser {
    PAUser() = default;

    static PAUser* Alloc();
    void Free();
    void AddRef();
    void ReleaseRef();

    CIOSocket* pIOSocket;
    CClientSession* pSession;
    std::int32_t opTime;
    std::int32_t mRefCount;
    PreLoginUser preUser;
    std::int32_t origin;
    std::int32_t serverid;
    std::int32_t stat;
    in_addr clientIp;
    char locationNo;
    LoginUser loginUser;
};

// Original owner: Src/paservermgr.h. L2Authd.pdb type 0x4B65, target size
// 0x58. This is the complete recorded public method surface and data graph.
class PAServerMgr {
public:
    using PAUserMap = std::map<int, PAUser*>;

    PAServerMgr();
    virtual ~PAServerMgr();

    bool RegisterPASocketMap(CPASocket* pPASocket);
    bool UnregisterPASocketMap(CPASocket* pPASocket);
    CPASocket* GetPASocket();
    void Initialize();
    void PAServerTimerRoutine(CPASocket* pPASocket);

    std::int32_t AddUserWait(PAUser* pPAUser);
    PAUser* GetUserWait(int identity);
    bool DelUserWait(int identity, PAUser** pPAUser);

    char LoginAccount(
        int origin,
        CClientSession* pSession,
        CIOSocket* pSocket,
        PreLoginUser* pPreUser);
    char LoginExternalToken(
        int origin,
        CClientSession* pSession,
        CIOSocket* pSocket,
        PreLoginUser* pPreUser);
    char ExternalUserInfo(
        int uid,
        PreLoginUser preLogin,
        int serverid,
        int stat,
        in_addr clientIp);
    char GetUserInfo(
        int origin,
        int uid,
        PreLoginUser preLogin,
        int serverid,
        int stat,
        in_addr clientIp);
    char RequestAuthnToken(
        char serverId,
        LoginUser* pLoginUser,
        char* targetAppId);
    char LoginToken(
        int origin,
        CClientSession* pSession,
        CIOSocket* pSocket,
        PreLoginUser* pPreUser,
        in_addr clientIp);

    bool SendVerifyAccountReq(
        std::int32_t tid,
        char* account,
        char* password);
    bool SendExternalTokenLoginReq(
        std::int32_t tid,
        int companyNo,
        unsigned char* externalToken,
        int affiliatedGameId,
        int gameId);
    bool SendAuxAuthenticationAck(
        const unsigned char* packet);
    bool SendExternalUserInfoReq(
        std::int32_t tid,
        int uid);
    bool RecvGetUserInfoAck(
        const unsigned char* packet);
    bool SendGetUserInfoReq(
        std::int32_t tid,
        char* account);
    bool SendRequestAuthnTokenReq(
        int tid,
        int portalUid,
        in_addr loginIp,
        char* targetAppId,
        _GUID fromAppId,
        _GUID sessionId);
    bool SendLoginToken(
        std::int32_t tid,
        const char* authProviderCode,
        const char* authnToken,
        in_addr loginIp);

    bool RecvVerifyAccountAck(
        const unsigned char* packet);
    bool RecvExternalTokenLoginAck(
        const unsigned char* packet);
    bool RecvAuxAuthenticationReq(
        const unsigned char* packet);
    bool RecvExternalUserInfoAck(
        const unsigned char* packet);
    bool RecvRequestAuthnTokenAck(
        const unsigned char* packet);
    bool RecvLoginTokenAck(
        const unsigned char* packet);
    bool ClientLoginCallback(
        VerifyAccountData* pData,
        PAUser* pPAUser);
    bool PetitionLoginCallback(
        VerifyAccountData* pData,
        PAUser* pPAUser);

    // L2Authd.exe 0x0041A7A0, Src/paservermgr.h:118.
    static PAServerMgr* Inst() {
        return sInstance;
    }

private:
    PAUserMap mWaitUserMap;
    CLock mUserMapLock;
    std::set<CPASocket*> mSocketMap;
    CLock mSocketMapLock;
    std::int32_t mTransCount;

    static PAServerMgr* sInstance;
};
