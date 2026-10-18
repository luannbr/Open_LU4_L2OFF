#pragma once

#include "AccountDB.h"
#include "Lock.h"
#include "Protocol.h"

#include <cstdint>
#include <map>
#include <set>

class CClientSession;
class CIOSocket;
class CSocketServer;
class CGpProxySocket;

// Original owner: Src/gpproxyservermgr.h. L2Authd.pdb type 0x4EFD,
// target size 0x2238. Field order and the uninitialized
// fundamental members in its constructors are kept by the owner body.
struct CGpProxyUser {
    CGpProxyUser();
    explicit CGpProxyUser(int serverId);
    CGpProxyUser(
        EORIGIN_CODE origin,
        CClientSession* session,
        PreLoginUser& user);
    ~CGpProxyUser();

    static CGpProxyUser* Alloc();
    void AddRef();
    void ReleaseRef();

    CIOSocket* m_pIOSocket;
    CClientSession* m_pSession;
    std::int32_t m_opTime;
    std::int32_t m_refCount;
    PreLoginUser m_preLoginUser;
    LoginUser m_loginUser;
    EORIGIN_CODE m_origin;
    std::int32_t m_serverid;
    char m_locationNo;
    char m_command;
    WebSessionId m_webSessionId;
    WebToken m_uniqueId;
    char m_tokenServerId;
};

// The wire reply is packed in the selected PDB: birthdate begins at +0x1F,
// not at the naturally aligned +0x20.
#pragma pack(push, 1)
struct CGpProxyAccountData {
    std::int32_t tid;
    std::int32_t reason;
    std::int32_t gameUid;
    std::int32_t portalUid;
    char account[15];
    std::int32_t birthdate;
    char sex;
    std::int32_t blockCode;
    std::int32_t noticeCode;
    std::int32_t statusCode;
    std::int32_t blockReasonCode;
    std::int32_t userLevel;
    std::int32_t typeCode;
    std::int32_t companyCode;
    _GUID userId;
};
#pragma pack(pop)

// Original owner: Src/gpproxyservermgr.h. L2Authd.pdb type 0x54F6,
// target size 0x5C. The complete recorded data-member graph is kept.
class CGpProxyServerMgr {
public:
    CGpProxyServerMgr();
    virtual ~CGpProxyServerMgr();

    static CGpProxyServerMgr* Inst() {
        return s_instance;
    }

    void Initialize();
    void ReconnectTimerRoutine(CGpProxySocket* socket);
    bool RegisterSocketMap(CGpProxySocket* socket);
    bool UnregisterSocketMap(CGpProxySocket* socket);
    CGpProxySocket* GetValidSocket();

    char LoginAccountWsid(
        EORIGIN_CODE origin,
        CClientSession* session,
        CIOSocket* socket,
        PreLoginUser& user,
        WebToken& token);
    bool RecvVerifyWebSessionAck(const unsigned char* packet);
    bool RecvUpdateWebSessionTimeoutAck(const unsigned char* packet);
    bool RecvCreateWebSessionAck(const unsigned char* packet);
    bool RecvSecAuthListServicesAck(const unsigned char* packet);
    bool RecvSecAuthVerifyOtpAck(const unsigned char* packet);
    bool RecvSecAuthAuthenticatedListAck(const unsigned char* packet);
    bool RecvSecondPasswordAck(const unsigned char* packet);

    char LoginAccountIdPwd(
        EORIGIN_CODE origin,
        CSocketServer* socket,
        LoginUser& user,
        WebToken& token,
        int timeout);
    char LogoutAccountWsid(
        EORIGIN_CODE origin,
        CSocketServer* socket,
        const LoginUser& user,
        WebSessionId& webSessionId);
    char ExtendLifeTimeWsid(
        EORIGIN_CODE origin,
        CSocketServer* socket,
        LoginUser& user,
        WebSessionId& webSessionId,
        WebToken& token,
        char tokenServerId);
    char ListSecAuthServices(
        EORIGIN_CODE origin,
        CClientSession* session,
        PreLoginUser& user);
    char VerifyOtp(
        EORIGIN_CODE origin,
        CClientSession* session,
        PreLoginUser& user);
    char ListSecAuthAuthenticatedList(
        EORIGIN_CODE origin,
        CClientSession* session,
        PreLoginUser& user);
    char SecondPasswordRequest(
        char serverId,
        const unsigned char* packet);

    std::map<int, CGpProxyUser*> m_waitUserMap;
    CLock m_waitUserMapLock;
    std::set<CGpProxySocket*> m_socketMap;
    CLock m_socketMapLock;
    void* m_hReconnectTimer;
    std::int32_t m_transCount;

private:
    bool AddUserWait(
        CGpProxyUser* user,
        int& transactionId);
    bool DelUserWait(
        int transactionId,
        CGpProxyUser*& user);
    bool SendVerifyWebSessionReq(
        std::int32_t transactionId,
        WebSessionId& webSessionId,
        in_addr address);
    bool SendUpdateWebSessionTimeoutReq(
        std::int32_t transactionId,
        WebSessionId& webSessionId,
        in_addr address);
    bool SendCreateWebSessionReq(
        std::int32_t transactionId,
        LoginUser& user,
        int timeout,
        [[maybe_unused]] in_addr address);
    bool SendSecAuthListServicesReq(
        std::int32_t transactionId,
        PreLoginUser& user,
        in_addr address);
    bool SendSecAuthVerifyOtpReq(
        std::int32_t transactionId,
        PreLoginUser& user,
        in_addr address);
    bool SendSecAuthAuthenticatedListReq(
        std::int32_t transactionId,
        PreLoginUser& user,
        in_addr address);
    bool SendSecondPasswordReq(
        int transactionId,
        CGpProxyUser* user,
        const unsigned char* packet);
    bool ClientLoginCallback(
        CGpProxyAccountData* accountData,
        CGpProxyUser* user);

    static CGpProxyServerMgr* s_instance;
};
