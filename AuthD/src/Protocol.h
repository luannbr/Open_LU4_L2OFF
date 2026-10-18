#pragma once

#include <cstdint>

// L2Authd.pdb types 0x3F09 and 0x4DF9. The only compiled function from the
// original header is MapWSMToAuthReason (L2Authd.exe 0x00433C50).
struct WebToken {
    std::int32_t s_id0;
    std::int32_t s_id1;
    std::int32_t s_id2;
    std::int32_t s_id3;
};

enum EORIGIN_CODE : std::int32_t {
    ORIGIN_CLIENT_LOGIN = 0,
    ORIGIN_PETITION_LOGIN = 1,
    ORIGIN_LOGOUT = 2,
    ORIGIN_GAMESERVER_REQUEST = 3,
    ORIGIN_AUTHNTOKEN_REQUEST = 4,
};

// Original owner: Src/protocol.h. L2Authd.pdb types 0x8333 and 0x82D0.
enum EPROTO_AUTH_TO_PA : std::int32_t {
    AP_VERIFY_IDPWD_REQ = 1,
    AP_GET_USER_INFO_REQ = 4,
    AP_AA_EXTERNAL_TOKEN_REQ = 7,
    AP_AA_AUX_AUTHENTICATION_ACK = 8,
    AP_GET_EXTERNAL_USER_INFO_REQ = 9,
    AP_TWO_FACTOR_AUX_AUTHN_ACK = 11,
    AP_REQUEST_AUTHN_TOKEN_REQ = 20,
    AP_NP_LOGIN_TOKEN_REQ = 22,
    AP_MAX = 23,
};

enum EPROTO_PA_TO_AUTH : std::int32_t {
    PA_VERIFY_IDPWD_ACK = 1,
    PA_GET_USER_INFO_ACK = 4,
    PA_AA_EXTERNAL_TOKEN_ACK = 7,
    PA_AA_AUX_AUTHENTICATION_REQ = 8,
    PA_GET_EXTERNAL_USER_INFO_ACK = 9,
    PA_TWO_FACTOR_AUX_AUTHN_REQ = 11,
    PA_REQUEST_AUTHN_TOKEN_ACK = 20,
    PA_NP_LOGIN_TOKEN_ACK = 22,
    PA_MAX = 23,
};

// Original owner: Src/protocol.h. L2Authd.pdb types 0x7D2C/0x7D2E.
// The names keep the shipping protocol's VMS terminology even though the
// owning socket and manager use their historical PBS names.
enum EPROTO_AUTH_TO_VMS : std::int32_t {
    AV_PROTOCOL_VER = 0,
    AV_START_CHARGE = 1,
    AV_GET_VALUE_INFO = 2,
    AV_END_CHARGE = 3,
    AV_VALUE_ABANDON = 4,
    AV_START_CHARGE_LIST = 5,
    AV_CHARGE_SUSPEND = 6,
    AV_CHARGE_RESUME = 7,
    AV_MAX = 8,
};

enum EPROTO_VMS_TO_AUTH : std::int32_t {
    VA_START_CHARGE_ACK = 1,
    VA_GET_VALUE_INFO_ACK = 2,
    VA_END_CHARGE_REQ = 3,
    VA_NOTIFY_REMAINTIME = 4,
    VA_NOTIFY_NEW_ITEM_APPLIED = 5,
    VA_VALUE_ABANDON_ACK = 6,
    VA_START_CHARGE_LIST_ACK = 7,
    VA_CHARGE_SUSPEND_ACK = 8,
    VA_CHARGE_RESUME_ACK = 9,
    VA_MAX = 10,
};

// Original owner: Src/protocol.h. L2Authd.pdb records the selected legacy
// IP-billing protocol in these two enum types. The misspelled reconnect name
// is kept unchanged from the target debug information.
enum EPROTO_AUTH_TO_IPSERVER : std::int32_t {
    AI_SERVER_VERSION = 0,
    AI_IP_KIND = 1,
    AI_IP_ACQUIRE = 2,
    AI_IP_RELEASE = 3,
    AI_IP_START_CHARGE = 4,
    AI_IP_STOP_CHARGE = 5,
    AI_IP_INSTANT_START_GAME = 6,
    AI_IP_INSTANT_STOP_GAME = 7,
    AI_IP_KICKED = 8,
    AI_IP_READY_GAME = 9,
    AI_IP_SET_START_TIME = 10,
    AI_IP_USE_FOR_LINEAGE = 11,
    AI_IP_USE_FOR_RECONNECT = 12,
    AI_IP_GET_KIND = 13,
    AI_MAX = 14,
};

enum EPROTO_IPSERVER_TO_AUTH : std::int32_t {
    IA_SERVER_VERSION = 0,
    IA_IP_KIND = 1,
    IA_IP_USE = 2,
    IA_IP_START_OK = 3,
    IA_IP_START_FAIL = 4,
    IA_IP_USE_FAIL = 5,
    IA_IP_SESSIONKEY = 6,
    IA_IP_INSTANTLOGIN_OK = 7,
    IA_IP_INSTANTLOGIN_FAIL = 8,
    IA_IP_KICK = 9,
    IA_IP_READY_FAIL = 10,
    IA_IP_READY_OK = 11,
    IA_IP_SET_STARTTIME_OK = 12,
    IA_IP_SET_STARTTIME_FAIL = 13,
    IA_IP_UES_FOR_RECONNECT_FAIL = 14,
    IA_IP_USE_FOR_RECONNECT_OK = 15,
    IA_IP_GET_KIND_RES = 16,
    IA_MAX = 17,
};

int MapWSMToAuthReason(int reason);

// Original owner: Src/protocol.h:1556-1568. The selected image emits this
// helper in TokenServerMgr.obj at L2Authd.exe 0x0048D680.
inline int MapTokenToAuthReason(int reason) {
    switch (reason) {
    case 0:
        return 0;
    case 1:
    case 2:
    case 4:
        return 2;
    default:
        return 0x14;
    }
}

// Original owner: Src/protocol.h. The selected image emits this inline
// helper in GpProxyServerMgr.obj at L2Authd.exe 0x0043FFB0.
inline int MapGpProxyToAuthReason(int reason) {
    switch (reason) {
    case 0:
        return 0;
    case 3:
        return 2;
    case 10:
        return 0x25;
    default:
        return 0x14;
    }
}

// L2Authd.exe 0x00471780, Src/protocol.h:1210-1244.
inline int MapPAToAuthReason(int reason) {
    switch (reason) {
    case 0:
        return 0;
    case 1:
        return 2;
    case 2:
        return 0x25;
    case 3:
        return 0x28;
    case 4:
        return 0x26;
    case 8:
        return 0x16;
    case 9:
        return 0x2b;
    case 10:
        return 7;
    case 11:
        return 0x47;
    case 13:
        return 0x48;
    case 18:
        return 0x4e;
    default:
        return 0x14;
    }
}

// L2Authd.exe 0x00478FF0, Src/protocol.h:1002-1015.
inline int MapVMToAuthReason(int reason) {
    switch (reason) {
    case 0:
        return 0;
    case 1:
    case 2:
        return 0x12;
    case 3:
        return 7;
    default:
        return 0x14;
    }
}
