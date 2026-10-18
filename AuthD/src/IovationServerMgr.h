#pragma once

#include "AccountDB.h"
#include "Lock.h"

#include <cstdint>
#include <map>

class CClientSession;
class CIOSocket;
class IovationSocket;

// Original owner: Src/iovationservermgr.h. L2Authd.pdb type 0x5B48,
// target size 0x16A0.
struct IovationTrans {
    static IovationTrans* Alloc();
    void AddRef();
    void ReleaseRef();

    PreLoginUser preLoginUser;
    CClientSession* pClientSession;
    std::int32_t opTime;
    std::int32_t mRefCount;
};

// Original owner: Src/iovationservermgr.h. L2Authd.pdb type 0x5B3D,
// target size 0x3C. The PDB records a vptr but no base class.
class IovationServerMgr {
public:
    IovationServerMgr();
    virtual ~IovationServerMgr();

    void Initialize();
    void IovationServerTimerRoutine();
    bool AddIovationTrans(
        int transactionId,
        IovationTrans* transaction);
    bool DelIovationTrans(
        int transactionId,
        IovationTrans** transaction);
    char RequestCheckDetail(
        PreLoginUser& user,
        CClientSession* client,
        unsigned short dataLength,
        const unsigned char* data);
    bool RecvCheckTransactionDetailAck(
        CIOSocket* socket,
        const unsigned char* packet);

    IovationSocket* mpIovationSocket;
    bool mIovationServerConnected;
    void* mhIovationServerTimer;

    static IovationServerMgr* Inst();

private:
    using IovationTransMap =
        std::map<int, IovationTrans*>;

    IovationTransMap mIovationTransMap;
    CLock mTransMapLock;
    std::int32_t mTransId;

    static IovationServerMgr* sInstance;
};
