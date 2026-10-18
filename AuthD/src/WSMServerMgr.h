#pragma once

#include "AuthType.h"
#include "Lock.h"
#include "Protocol.h"

#include <cstdint>
#include <map>

class CSocketServer;
class CClientSession;
class CIOSocket;
class CWSMSocket;

// Original owner: Src/wsmservermgr.h. L2Authd.pdb type 0x4661 is a
// byte-packed wire record: birthdate begins immediately after account[15].
#pragma pack(push, 1)
struct WSMAccountData {
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
};
#pragma pack(pop)

// Original owner: Src/wsmservermgr.h. L2Authd.pdb type 0x60FE, target
// size 0x2230. The selected image's compiler-generated constructor constructs
// both embedded user records and leaves their ordinary scalar fields alone.
struct WSMUser {
    static WSMUser* Alloc();
    void AddRef();
    void ReleaseRef();

    WSMUser() = default;

    CIOSocket* pIOSocket;
    CClientSession* pSession;
    std::int32_t opTime;
    std::int32_t mRefCount;
    PreLoginUser preUser;
    LoginUser loginUser;
    std::int32_t origin;
    WebSessionId webSessionId;
    WebToken uniqueId;
    char tokenServerId;
};

// Original owner: Src/wsmservermgr.h. L2Authd.pdb type 0x4E13,
// target size 0x3C.
class WSMServerMgr {
public:
    using WSMUserMap = std::map<int, WSMUser*>;

    WSMServerMgr();
    virtual ~WSMServerMgr();

    void Initialize();
    void WSMServerTimerRoutine();
    std::int32_t AddUserWait(WSMUser* user);
    bool DelUserWait(int tid, WSMUser** user);
    char LoginAccountWsid(
        EORIGIN_CODE origin,
        CClientSession* session,
        CIOSocket* socket,
        PreLoginUser* user,
        WebToken& token);
    char LoginAccount(
        EORIGIN_CODE origin,
        CSocketServer* socket,
        LoginUser* user,
        const unsigned char* token,
        int timeout);
    char LogoutAccountWsid(
        EORIGIN_CODE origin,
        CSocketServer* socket,
        LoginUser* user,
        WebSessionId& webSessionId);
    char ExtendLifeTimeWsid(
        EORIGIN_CODE origin,
        CSocketServer* socket,
        LoginUser* user,
        WebSessionId& webSessionId,
        unsigned char* token,
        char tokenServerId);
    bool SendVerifyWebSessionReq(
        std::int32_t tid,
        WebSessionId& webSessionId,
        WebToken& token);
    bool SendUpdateTimeoutReq(
        std::int32_t tid,
        WebSessionId& webSessionId);
    bool SendLoginReq(
        std::int32_t tid,
        LoginUser* user,
        const unsigned char* token,
        int timeout);
    bool RecvVerifyWebSessionAck(
        const unsigned char* packet);
    bool RecvUpdateTimeoutAck(
        const unsigned char* packet);
    bool RecvLoginAck(
        const unsigned char* packet);
    bool ClientLoginCallback(
        WSMAccountData* account,
        WSMUser* user);

    CWSMSocket* mpWSMSocket;
    bool mWSMServerConnected;
    void* mhWSMServerTimer;

    // L2Authd.exe 0x0041A7C0, Src/wsmservermgr.h:82.
    static WSMServerMgr* Inst() {
        return sInstance;
    }

private:
    WSMUserMap mWaitUserMap;
    CLock mUserMapLock;
    std::int32_t mTransCount;

    static WSMServerMgr* sInstance;
};
