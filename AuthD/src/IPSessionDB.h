#pragma once

#include "IOBuffer.h"
#include "IOObject.h"
#include "IOSocket.h"
#include "Lock.h"

#include <cstdint>
#include <map>

enum SocketMode : std::int32_t;
struct LoginUser;
class CIPSocket;

// Original owner: Src/ipsessiondb.h. L2Authd.pdb type 0x7D9B, target
// size 0x0C. This is the reference-counted carrier kept while an IP
// billing request is in flight.
struct IPSUser {
    static IPSUser* Alloc();
    void Free();
    void AddRef();
    void ReleaseRef();

    LoginUser* pLoginUser;
    std::int32_t mRefCount;
    char mOrigCmd;
    char mSendingCmd;
};

// Original owner: Src/ipsessiondb.h. L2Authd.pdb type 0x7D8C, target
// size 0x58. Access, maps, locks and method surface follow field list 0x7D8B.
class CIPSessionDB {
private:
    using IPSUserMap = std::map<int, IPSUser*>;

    int AddUserWait(
        int uid,
        IPSUser* user,
        IPSUser** displacedUser);
    bool DelUserWait(
        int uid,
        IPSUser** user);

public:
    CIPSessionDB();
    virtual ~CIPSessionDB();

    static bool IsIPCharging(int payStat);
    char StopIPCharge(
        unsigned int uid,
        unsigned int address,
        int payStat,
        int result,
        std::int32_t loginTime,
        char serverId,
        const char* account);
    char StartIPCharge(
        unsigned int uid,
        unsigned int address,
        int payStat,
        char serverId);
    char ReleaseSessionRequest(
        int sessionId,
        in_addr address,
        int payStat);
    char ReadyToIPCharge(
        unsigned int uid,
        unsigned int address,
        int payStat,
        char serverId);
    char ConfirmIPCharge(
        unsigned int uid,
        unsigned int address,
        int payStat,
        char serverId);
    char RequestIPCharge(
        int uid,
        LoginUser* user,
        char requestType);
    char RequestIPKind(
        LoginUser* user,
        int uid,
        char requestType);
    void IPKindRes(
        int kindResult,
        int gameAccountNo,
        int valueType,
        int valueKind,
        int uid,
        char* cafeId,
        int inCharge,
        int restrictionNo,
        char* chargeState);
    char AcquireSessionRequest(
        LoginUser* user,
        int uid,
        char requestType);
    char AcquireSessionSuccess(
        int uid,
        int sessionId,
        char result,
        int remainingTime,
        int payStat);
    char AcquireSessionFail(
        int uid,
        int sessionId,
        char result);
    void ChangeServerAck(
        int uid,
        int sessionId,
        LoginUser* user,
        int remainingTime,
        int payStat,
        bool acquired);
    char CheckServerSideLogin(
        int uid,
        IPSUser* waitingUser);
    int FindSessionID(int uid);
    int DelSessionID(int uid);
    int AddSessionID(int uid, int sessionId);
    bool DellAllWaitingSessionID();
    bool KickDuplicatedUserWaitAccount(
        IPSUser* waitingUser);

private:
    std::map<int, int> IPSessionMap;
    IPSUserMap mWaitingUser;
    CLock mWaitingUserLock;
    CLock IPSessionLock;
    std::int32_t waitUserCheckTime;
};

using IPPacketHandler = bool (*)(
    CIPSocket*,
    const unsigned char*);
using IPPacketFunc = bool (*)(
    CIOSocket*,
    const unsigned char*);

// Original owner: Src/ipsessiondb.h. L2Authd.pdb type 0x4382, target
// size 0x88. The public charging state precedes the protected framing and
// destination fields exactly as recorded in field list 0x4381.
class CIPSocket : public CIOSocket {
public:
    explicit CIPSocket(std::uint32_t socket);
    ~CIPSocket() override;

    static CIPSocket* Allocate(std::uint32_t socket);
    void OnCreate() override;
    void OnRead() override;
    void OnClose() override;
    void OnTimerCallback() override;
    bool Send(const char* format, ...);
    const char* IP();

    in_addr getaddr() {
        return addr;
    }

    // L2Authd.exe 0x0045D360, Src/ipsessiondb.h:111-113.
    void SetAddress(in_addr address) {
        addr = address;
    }

    // L2Authd.exe 0x0045D380, Src/ipsessiondb.h:114-116.
    void SetConnectSessionKey(
        unsigned int sessionKey) {
        ConnectSessionKey = sessionKey;
    }

    std::int32_t opFlag;
    std::uint32_t ConnectSessionKey;

protected:
    std::int32_t packetLen;
    SocketMode mode;
    IPPacketHandler* packetTable;
    in_addr addr;
    sockaddr_in Destination;
    char* host;
};

// Original owner: Src/ipsessiondb.h. L2Authd.pdb type 0x87CE, target
// size 0x14.
class CIPPacketServer : public CIOObject {
public:
    CIPPacketServer();
    ~CIPPacketServer() override;

    static CIPPacketServer* Alloc();
    static void FreeAll();
    void Free();
    void OnIOCallback(
        int bSuccess,
        std::uint32_t dwTransferred,
        _OVERLAPPED* lpOverlapped) override;

    CIOSocket* m_pSocket;
    CIOBuffer* m_pBuf;
    IPPacketFunc m_pFunc;

    static std::int32_t g_nPendingPacket;
};

// Original owner: Src/ipsessiondb.h. L2Authd.pdb types 0x8773/0x877B.
class CIPPacketServerPool {
public:
    class CSlot {
    public:
        CSlot();
        ~CSlot() = default;

        CIPPacketServer* m_pPacket;
        CLock m_lock;
    };

    ~CIPPacketServerPool();

    static CSlot g_slot[16];
    static std::int32_t g_nAlloc;
    static std::int32_t g_nFree;
};

extern CIPSessionDB ipsessionDB;
extern CIPSocket* pIPSocket;
extern bool IPServerReconnect;
extern void* g_hIPServerTimer;
extern CRWLock gIPLock;
extern CIPPacketServerPool theIPPacketPool;

#if defined(_WIN32)
void __stdcall IPSocketTimerRoutine(
#else
void IPSocketTimerRoutine(
#endif
    void* parameter,
    std::uint8_t timerOrWaitFired);
