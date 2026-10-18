#pragma once

#include "Shared/src/IOC.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeObjectMap.h"
#include "Shared/src/Utils.h"
#include "Shared/src/YieldLock.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#if defined(_WIN32)
#include <WinSock2.h>
#else
#include <netinet/in.h>
#endif

namespace Auth {

enum BanUserReason : std::int32_t {
    ChangeItemDataOnDB = 0,
};

} // namespace Auth

namespace SSO_FOR_STEAM_INFO {

static const std::u16string LINEAGE2_APP_ID(u"2B8B8FCF-FD82-FDDC-0094-39FB650090F0");

} // namespace SSO_FOR_STEAM_INFO

namespace NEW_AUTH_USER_DATA {

enum ATTENDANCE_TYPE : std::int32_t {
    AT_FIRST_ATTENDANCE = 0,
    AT_NORMAL = 1,
    AT_NEWBIE = 2,
    AT_DORMANT = 3,
};

enum ATTENDANCE_JOB : std::int32_t {
    AJ_NONE = 0,
    AJ_RESET = 1,
    AJ_UPDATE = 2,
    AJ_ROLLBACK = 3,
    AJ_SET_BUILDER = 4,
};

} // namespace NEW_AUTH_USER_DATA

namespace NEW_AUTH_USER_DATA_FOR_SERVER {

enum DATA_RW_TYPE : std::int32_t {
    TYPE_DEFAULT = 0,
    TYPE_READ = 1,
    TYPE_WRITE = 2,
};

enum DATA_RW_REASON : std::int32_t {
    REASON_DEFAULT = 0,
    REASON_FOR_CHARACTER_COUNT = 1,
    REASON_FOR_ATTENDANCE = 2,
};

} // namespace NEW_AUTH_USER_DATA_FOR_SERVER

enum WAITER_STATUS : std::int32_t {
    WAITER_NO = 0,
    WAITER_VIPPASS = 1,
    WAITER_QUEUETICKET = 2,
};

struct stAccountAuthInfo {
    stAccountAuthInfo() {
        nWarnFlag = 0;
        nTimeLeft = 0;
        nStat = 0;
        nLoginFlag = 0;
        nAge = 0;
        cChargeMethod = '\0';
        nReservations = 0;
        nRestTime_china = 0;
        nPlayTime_china = 0;
        client_addr.s_addr = 0;
        std::memset(sPCCafeCode, 0, sizeof(sPCCafeCode));
        std::memset(sAccountName, 0, sizeof(sAccountName));
        nMaxCharId = 0;
        nMaxLevel = 0;
        nPremiumLevelFlag = 0;
        nMarkingTime = 0;
        nDormantType = 0;
        WaiterStatus = WAITER_QUEUETICKET;
    }

    int nWarnFlag;
    int nTimeLeft;
    int nStat;
    int nLoginFlag;
    int nAge;
    char cChargeMethod;
    int nReservations;
    int nRestTime_china;
    int nPlayTime_china;
    char sPCCafeCode[9];
    ::in_addr client_addr;
    char sAccountName[15];
    unsigned short nPremiumLevelFlag;
    int nMarkingTime;
    int nDormantType;
    int nMaxCharId;
    int nMaxLevel;
    WAITER_STATUS WaiterStatus;
};


struct stCacheDAccountVIPLevel {
    stCacheDAccountVIPLevel() : m_cVIPLevel(0), m_bIsLoadedByCacheD(false) {}

    std::uint8_t m_cVIPLevel;
    bool m_bIsLoadedByCacheD;
};


using AccountAuthInfoMap = std::map<std::uint32_t, stAccountAuthInfo>;
using AccountVIPLevelByDBMap = std::map<std::uint32_t, stCacheDAccountVIPLevel>;
using OneTimeKeyMap = std::map<std::uint32_t, std::uint32_t>;

extern std::uint32_t g_nOneTimeKey;
extern OneTimeKeyMap g_OneTimeKeyMap;
extern AccountAuthInfoMap g_AccountAuthInfoMap;
extern AccountVIPLevelByDBMap g_AccountVIPLevelByDBMap;
extern RWLock KeyLock;
extern RWLock AccountAuthInfoLock;
extern RWLock g_AccountVIPLevelByDBLock;


struct AuthUserDataInfo {
    AuthUserDataInfo() {
        nAuthServerID = 0;
        nTotalCharacter = 0;
        nDeleteCharacter = 0;
        std::memset(tTimeToDelete, 0, sizeof(tTimeToDelete));
        nUseTime = 0;
        nPlayTime = 0;
        eAttendanceType = NEW_AUTH_USER_DATA::AT_NORMAL;
        nAttendanceDays = 0;
        tLastAttendanceCheckTime = 0;
        nPCCafeAttendanceFlag = 0;
        nVIPAttendanceFlag = 0;
        eJob = NEW_AUTH_USER_DATA::AJ_NONE;
        nAttendanceInfoLoaded = 0;
        nRWType = NEW_AUTH_USER_DATA_FOR_SERVER::TYPE_DEFAULT;
        nRWReason = NEW_AUTH_USER_DATA_FOR_SERVER::REASON_DEFAULT;
    }

    std::uint8_t nAuthServerID;
    std::uint8_t nTotalCharacter;
    std::uint8_t nDeleteCharacter;
    std::int32_t tTimeToDelete[7];
    std::int32_t nUseTime;
    std::int32_t nPlayTime;
    NEW_AUTH_USER_DATA::ATTENDANCE_TYPE eAttendanceType;
    std::uint8_t nAttendanceDays;
    std::int32_t tLastAttendanceCheckTime;
    std::int32_t nPCCafeAttendanceFlag;
    std::int32_t nVIPAttendanceFlag;
    NEW_AUTH_USER_DATA::ATTENDANCE_JOB eJob;
    volatile std::int32_t nAttendanceInfoLoaded;
    volatile std::int32_t nRWType;
    volatile std::int32_t nRWReason;
};

using AuthUserDataInfoMap = std::map<std::uint32_t, AuthUserDataInfo>;

extern AuthUserDataInfoMap g_AuthUserDataInfoMap;
extern RWLock AuthUserDataInfoLock;


// Current .data range 0x015c4180..0x015c4290 is the 35-slot AuthD callback
// table. CAuthSocket::OnRead at 0x004aacac accepts opcodes 0..0x21. The last
// physical slot is the current DummyAuthPacket sentinel at index 0x22.
extern CAuthCallback authPacketTable[35];

class CAuthSocket : public CIOSocketEx<IOBuffer8k> {
public:
    explicit CAuthSocket(std::uint64_t socket, CAuthCallback* packetTable);
    ~CAuthSocket() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    bool CheckPendingWrite() override;
    void Send(const char* format, ...) override;
    void SendV(const char* format, va_list arguments) override;

protected:
    void OnClose() override;
    void OnRead() override;

    std::uint64_t m_socket;
    std::int32_t m_threadCount;
    CAuthCallback* m_packetTable;
    SocketMode m_mode;
    std::int32_t m_packetLength;

private:
    L2_DECLARE_OBJECT_REGISTRATION(CAuthSocket)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CAuthSocket)
};

// Current L2Server RTTI at 0x013d9bd8 records CAuthPacket as a direct
// CPacketEx<CIOBufferEx<8192>> derivative. Its only distinct virtual body
// is the profiled callback at 0x007cd554. The object adds no storage.
class CAuthPacket : public CAuthPacketBase {
public:
    CAuthPacket() = default;
    ~CAuthPacket() override = default;

protected:
    void OnIOCallback(int success, std::uint32_t transferred, PortableOverlapped* overlapped) override;
};

struct ConnectedUserInfo {
    ConnectedUserInfo() { std::memset(szAccountName, 0, sizeof(szAccountName)); }

    std::int32_t nAccountId;
    std::int32_t nPayStat;
    ::in_addr ip;
    char szAccountName[15];
};

static_assert(std::is_trivially_destructible_v<ConnectedUserInfo>);


class CAuthD : public MemoryObject {
public:
    CAuthD();
    ~CAuthD() override;

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    bool ConnectAuthD();
    bool InitAuthD();
    bool ReconnAuthD();
    bool ReconnectAuth();
    void SendAuthConnected();
    void MakeConnectedUserList();
    void SendConnectedUserList();
    void MakeOnReconnectedPlayerlist();
    void SendOnReconnectedPlayerlist(int bufferIndex);
    void SetAuthClosed();
    void CloseAuthSocket();
    void RequestWebSessionID(int accountId, int value);
    void RequestCloseWebSessionID(int accountId, char* webSessionId);
    bool RequestLogin(
        SharedUtilsWideChar* accountName, int accountId, int loginKey, int oneTimeKey, int gameId, CUserSocket* socket
    );
    bool AuthPing(int value);
    bool AuthRequestAgeKoreanStyle(int accountId);
    bool AuthPlayOk(int accountId);
    bool AuthPlayFail(int accountId, short reason);
    bool AuthPlayGame(int accountId, int oneTimeKey, int gameId);
    bool AuthQuitGame(
        int accountId, short reason, int useTime, int playTime, int maxCharacterId, int maxLevel, int closeReason
    );
    void RequestLogout(int accountId, int useTime, int playTime, int maxCharacterId, int maxLevel, int closeReason);
    void SaveUserDataAndLogout(int accountId, int useTime, int playTime, int maxCharacterId, int maxLevel, int closeReason);
    bool AuthWaitUserReq(int accountId);
    bool AuthUserNum(int userCount, int userLimit);
    bool AuthSendServerVersion();
    void RequestBanAccount(int accountId, Auth::BanUserReason reason);
    void AuthRequestGetValueInfo(int accountId);
    bool AuthRequestGetUserData(
        int accountId, NEW_AUTH_USER_DATA_FOR_SERVER::DATA_RW_TYPE type, NEW_AUTH_USER_DATA_FOR_SERVER::DATA_RW_REASON reason
    );
    void AuthSetUserData(int accountId, unsigned short dataLength, const unsigned char* data);
    void AuthWriteUserData(int accountId, unsigned short dataLength, const unsigned char* data);
    void AddAuthUserDataCharacterInfo(int accountId, int totalCharacter, int deleteCharacter, std::int32_t* timeToDelete);
    void AddAuthUserDataAttendanceInfo(
        int accountId,
        NEW_AUTH_USER_DATA::ATTENDANCE_TYPE attendanceType,
        unsigned char attendanceDays,
        std::int32_t lastAttendanceCheckTime,
        int pcCafeAttendanceFlag,
        int vipAttendanceFlag,
        NEW_AUTH_USER_DATA::ATTENDANCE_JOB job
    );
    bool GetAuthUserDataInfo(int accountId, AuthUserDataInfo& info);
    void AuthFreeToPayUphill(int accountId, int previousPayStat, int currentPayStat);
    void AuthDeleteChar(int accountId, int characterId, int characterLevel, int characterClass, int characterGender);
    void AuthCheckCharMaxLevel(int accountId, int characterId, int characterLevel);
    void AuthChargeStart(int accountId);
    void AuthChargeStop(int accountId);
    void AuthRequestActivateMark(int accountId, std::int64_t value1, std::int64_t value2, int dormantReason);
    void AuthRequest2ndPassword(char requestType, int accountId, int result, char16_t* password, char16_t* newPassword);
    bool AuthRequestAppToken(unsigned int accountId);
    bool AuthRequestAuthNToken(unsigned int accountId);

    std::int32_t m_nReplyReconnBufNum;
    std::int32_t m_nMaxReconnBufNum;
    volatile std::int32_t m_nReconnectTryUserCount;
    volatile std::int32_t m_nReconnectCompleteUserCount;

private:
    bool m_bSendAuth;
    char m_pBuf[100][0x5000];
    std::int32_t m_pBufLen[100];
    std::int32_t m_pBufNum[100];
    CSPointer<CISocket> m_spAuthSocket;

public:
    CYieldLock m_Lock;
    std::map<std::int32_t, ConnectedUserInfo*> m_connectedUserList;
    std::map<std::int32_t, ConnectedUserInfo*> m_sendUserList;
    std::int32_t m_lastSendUserCount;
    char m_PlayTimeInfo[24];

private:
};


extern CAuthD authd;

static_assert(std::is_trivially_copyable_v<AuthUserDataInfo>);
static_assert(std::is_trivially_destructible_v<AuthUserDataInfo>);
static_assert(std::is_trivially_copyable_v<stAccountAuthInfo>);
static_assert(std::is_trivially_destructible_v<stAccountAuthInfo>);
