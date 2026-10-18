#pragma once

#include "AccountDB.h"
#include "Lock.h"

#include <cstdint>
#include <map>

class CClientSession;
class CPBSSocket;

// Original owner: Src/pbservermgr.h. L2Authd.pdb type 0x6A90, target
// size 0x20. All fields are public in field list 0x6A8F.
struct BANUser {
    BANUser();

    std::int16_t blockReason;
    std::int32_t result;
    std::int32_t restrictionNo;
    std::int32_t gameAccountNo;
    char gameAccount[15];
};

// Original owner: Src/pbservermgr.h. L2Authd.pdb type 0x4313, target
// size 0x34. This is the exact wait-map carrier used by PBServerMgr.
struct PBSUser {
    static PBSUser* Alloc();
    void Free();
    void AddRef();
    void ReleaseRef();

    PBSUser() = default;

    LoginUser* pLoginUser;
    CClientSession* pClientSession;
    std::int32_t opTime;
    std::int32_t mRefCount;
    char mOrigCmd;
    BANUser mBanUser;
};

// Original owner: Src/pbservermgr.h. L2Authd.pdb type 0x692F, target
// size 0x38. Method order, access and member ownership follow field list
// 0x692E. PB is the selected image's legacy VMS charging protocol.
class PBServerMgr {
public:
    using PBSUserMap = std::map<int, PBSUser*>;

    PBServerMgr();
    virtual ~PBServerMgr();

    void Initialize();
    void PBServerTimerRoutine();
    bool IsPBCharging(int payStat);
    bool AddUserWait(int uid, PBSUser* user);
    bool DelUserWait(int uid, PBSUser** user);
    char RequestGetValueInfo(
        int uid,
        LoginUser* user,
        CClientSession* client,
        char requestType,
        char chargeServerId);
    char RequestStartCharge(
        int uid,
        int serverId,
        in_addr address);
    char RequestValueAbandon(
        int uid,
        unsigned char* valueId,
        LoginUser* user);
    char RequestStartChargeList(
        int count,
        int userListSize,
        char* userList);
    char RequestChargeSuspend(
        short reason,
        int result,
        int uid,
        char* account,
        int restrictionNo);
    char NotifyEndCharge(int uid);
    void RecvGetValueInfo(const unsigned char* packet);
    bool RecvStartCharge(const unsigned char* packet);
    bool RecvEndChargeReq(int uid, char reason);
    char GetValueInfo(
        PBSUser* waitUser,
        int uid,
        char reason,
        char valueType,
        int remainPlayTime,
        int reservations,
        char computationBasis,
        int itemId);
    bool GetValueInfoChangeServer(
        LoginUser* user,
        CClientSession* client,
        int uid,
        char reason,
        char valueType,
        int remainPlayTime,
        int reservations,
        char computationBasis,
        int itemId);
    bool GetValueInfoServersideLogin(
        LoginUser* user,
        int uid,
        char reason,
        char valueType,
        int remainPlayTime,
        int reservations,
        char computationBasis,
        int itemId);
    bool GetValueInfoAboutToPlay(
        LoginUser* user,
        CClientSession* client,
        int uid,
        char reason,
        char valueType,
        int remainPlayTime,
        int reservations,
        char computationBasis,
        int itemId);
    bool RecvValueAbandon(int uid, char reason);
    bool RecvNotifyRemainTime(
        int uid,
        int remainTime,
        int reservations,
        char computationBasis);
    bool RecvNotifyNewItemApplied(
        int uid,
        int remainTime,
        int reservations,
        char valueType,
        unsigned char* valueId,
        char computationBasis,
        int itemId);
    bool RecvStartChargeList(const unsigned char* packet);
    bool RecvChargeSuspend(int uid, char reason);

    CPBSSocket* mpPBSSocket;
    bool mPBServerConnected;
    bool mPBServerReConnect;
    void* mhPBServerTimer;

    // L2Authd.exe 0x0041A7B0, Src/pbservermgr.h:77.
    static PBServerMgr* Inst() {
        return sInstance;
    }

private:
    PBSUserMap mWaitUserMap;
    CLock mUserMapLock;

    static PBServerMgr* sInstance;
};
