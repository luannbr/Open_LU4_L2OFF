#pragma once

#include "Shared/src/Runtime/RawAtomic.h"
#include "Account.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <string>

#if !defined(_WIN32)
#include <strings.h>
#endif

// Original owner: Src/authtype.h. These records, enums and field sequences
// come from the selected PDB. Their inline members are the bodies compiled
// from this header into AccountDB.obj and AccountContainer.obj.
struct PortalUser {
    char account[65];
    std::int32_t uid;
    std::int32_t birthdate;
    char sex;
    std::int32_t blockCode;
    std::int32_t noticeCode;
    std::int32_t statusCode;
    std::int32_t blockReasonCode;
    _GUID userId;
};

struct WebSessionId {
    std::int32_t s_id0;
    std::int32_t s_id1;
    std::int32_t s_id2;
    std::int32_t s_id3;
};

struct ExternalUser {
    std::int32_t companyNo;
    unsigned char externalToken[1024];
    std::int32_t affiliatedGameId;
    std::int32_t gameId;
};

struct TokenInfo {
    TokenInfo() {
        // L2Authd.exe 0x00419D20, Src/authtype.h:376-379.
        std::memset(authProviderCode, 0, 4);
        std::memset(token, 0, 4);
    }

    char authProviderCode[33];
    char token[2048];
};

// PDB type 0x609E, sizeof 0x1694. Its compiler-generated constructor calls
// CAccount and TokenInfo while leaving the other fields uninitialized.
struct PreLoginUser {
    PreLoginUser() = default;

    bool bPassFlag;
    unsigned char cCardKey;
    std::int32_t nCheckCount;
    std::int32_t nCardNumber;
    std::int32_t nUserLevel;
    std::int32_t subscription;
    std::uint16_t cdkind;
    char clientInstances;
    std::int32_t otpAnswer;
    unsigned char otpFailCount;
    char playAccount[65];
    char playPassword[33];
    char loginAccount[65];
    char loginPassword[33];
    _GUID sessionId;
    PortalUser portalUser;
    CAccount account;
    WebSessionId webSessionId;
    ExternalUser externalUser;
    TokenInfo tokenInfo;
    bool iovationChecked;
};

// PDB enum 0x4833 has a 16-bit underlying type.
enum ChargeType : std::int16_t {
    CHARGE_TYPE_NONE = 0,
    CHARGE_TYPE_PCCAFE = 10,
};

enum UserMode : std::int32_t {
    UM_PRE_LOGIN = 0,
    UM_LOGIN = 1,
    UM_CHANGE_SERVER = 2,
    UM_PLAY_REQUEST = 3,
    UM_PLAY_OK = 4,
    UM_IN_GAME = 5,
};

// PDB type 0x60D7, sizeof 0x68.
struct ChargeServer {
    unsigned char valueId[16];
    char valueType;
    std::int32_t remainTime;
    std::int32_t reservations;
    std::int32_t availableServersSize;
    unsigned char availableServers[64];
    char computationBasis;
    bool otherIPCharging;
    std::int32_t itemId;
};

// PDB type 0x6661, sizeof 0xB64.
struct LoginUser {
    LoginUser() = default;

    // L2Authd.exe 0x0041A650, Src/authtype.h:349.
    void AddRef() {
        runtime::raw_atomic::IncrementI32(&mRefCount);
    }

    // L2Authd.exe 0x0040BF60, Src/authtype.h:351-354.
    void ReleaseRef() {
        if (runtime::raw_atomic::DecrementI32(
                &mRefCount) == 0) {
            ::operator delete(this);
        }
    }

    // L2Authd.exe 0x0041A670, Src/authtype.h:356-363.
    static LoginUser* Alloc() {
        auto* const user = new LoginUser{};
        user->AddRef();
        return user;
    }

    std::int32_t logintime;
    std::int32_t billStartTime;
    std::int32_t playStartTime;
    std::int32_t md5key;
    in_addr loginIp;
    in_addr nWsmLoginIp;
    std::int32_t stat;
    char serverid;
    std::uint32_t sid;
    UserMode um_mode;
    std::int32_t uid;
    char account[65];
    char loginAccount[65];
    char loginPassword[33];
    _GUID sessionId;
    void* timerHandle;
    char gender;
    std::int32_t loginflag;
    std::int32_t warnflag;
    char age;
    std::int16_t cdkind;
    char lastworld;
    char selectedServerid;
    std::int32_t subscription;
    std::int32_t serviceflag;
    std::uint32_t externalAuthAdditionalInfo;
    char forbidden_servers[16];
    char loginCafe[9];
    char clientInstances;
    ChargeServer ipServer;
    ChargeServer pbServer;
    char chargeServerId;
    std::int32_t reconnectKey;
    bool chargeflag;
    std::int32_t billlogTraceId;
    std::int32_t portalUid;
    char portalAccount[65];
    _GUID userId;
    char auth_flag;
    std::int32_t fcid;
    std::int32_t fatiguePlayTime;
    std::int32_t fatigueRestTime;
    std::int32_t timeAfterLastLogout;
    char user_data[2048];
    std::int16_t user_data_len;
    char ptid[33];
    WebSessionId webSessionId;
    char packageVersionInfo;
    char playAccount[65];
    unsigned char byteChildPlayTimeInfo[24];
    std::uint16_t user_level;
    std::int32_t lastLogoutTime;
    std::int32_t activateTime;
    char activateInformation[8];
    char originChargedServerId;
    bool gamePlayed;
    SecurityServiceFlag securityFlag;
    SecurityUsage securityUsage;
    std::int32_t mRefCount;
};

// Original owner: Src/authtype.h:428-440. L2Authd.pdb type 0x53B0 is an
// empty, transparent-looking comparator with these three exact overloads.
// Config uses it so duplicate keys and lookups are case-insensitive.
class iless {
public:
    bool operator()(
        const std::string& left,
        const char* right) const {
        return Compare(left.c_str(), right) < 0;
    }

    bool operator()(
        const char* left,
        const std::string& right) const {
        return Compare(left, right.c_str()) < 0;
    }

    bool operator()(
        const std::string& left,
        const std::string& right) const {
        return Compare(left.c_str(), right.c_str()) < 0;
    }

private:
    static int Compare(
        const char* left,
        const char* right) {
#if defined(_WIN32)
        return ::_stricmp(left, right);
#else
        return ::strcasecmp(left, right);
#endif
    }
};
