#pragma once

#include "AuthType.h"
#include "Lock.h"

#include <cstdint>
#include <map>

class CIOSocket;
class CSocketServer;
class TokenSocket;

// Original owner: Src/tokenservermgr.h. L2Authd.pdb type 0x6FC1,
// target size 0xB80.
struct TokenTrans {
    static TokenTrans* Alloc();
    void AddRef();
    void ReleaseRef();

    TokenTrans() = default;

    ::LoginUser LoginUser;
    CSocketServer* pServerSocket;
    std::int32_t opTime;
    std::int32_t mRefCount;
    char uniqueId[16];
};

// Original owner: Src/tokenservermgr.h. L2Authd.pdb type 0x6DAB,
// target size 0x3C.
class TokenServerMgr {
public:
    using TokenTransMap = std::map<int, TokenTrans*>;

    TokenServerMgr();
    virtual ~TokenServerMgr();

    void Initialize();
    void TokenServerTimerRoutine();
    bool AddTokenTrans(int tid, TokenTrans* transaction);
    bool DelTokenTrans(int tid, TokenTrans** transaction);
    bool FindTokenTrans(int tid, TokenTrans** transaction);
    char RequestGetToken(
        CSocketServer* socket,
        LoginUser* user,
        const unsigned char* token);
    char RequestCloseToken(
        CSocketServer* socket,
        LoginUser* user,
        WebSessionId& webSessionId);
    bool RecvGetTokenAck(
        CIOSocket* socket,
        const unsigned char* packet);
    bool RecvCloseTokenAck(
        CIOSocket* socket,
        const unsigned char* packet);

    TokenSocket* mpTokenSocket;
    bool mTokenServerConnected;
    void* mhTokenServerTimer;

    // L2Authd.exe 0x00433C10, Src/tokenservermgr.h:47.
    static TokenServerMgr* Inst() {
        return sInstance;
    }

private:
    TokenTransMap mTokenTransMap;
    CLock mTransMapLock;
    std::int32_t mTransId;

    static TokenServerMgr* sInstance;
};
