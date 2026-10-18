#include "IPSessionDB.h"

#include "Shared/src/ScopedLockGuards.h"

#include "Platform/IOSocketTransport.h"
#include "Shared/src/Runtime/NativeSocketError.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/TickCount32.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/TimerQueue.h"
#include "AccountContainer.h"
#include "AccountDB.h"
#include "BuildN.h"
#include "ClientSession.h"
#include "Config.h"
#include "Exception.h"
#include "IOServer.h"
#include "IOServerEx.h"
#include "L2AuthD.h"
#include "Log.h"
#include "PBServerMgr.h"
#include "Protocol.h"
#include "Reporter.h"
#include "ServerList.h"
#include "SocketServerEx.h"
#include "Thread.h"
#include "Util.h"

#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <ctime>

#if defined(_WIN32)
#include <WinSock2.h>
#else
#include <arpa/inet.h>
#endif

static bool DummyPacket(CIPSocket* socket, const unsigned char* packet);
static bool StartIPChargeFail(CIPSocket* socket, const unsigned char* packet);
static bool StartIPCharge(CIPSocket* socket, const unsigned char* packet);
static bool GetIPAcquireSuccess(CIPSocket* socket, const unsigned char* packet);
static bool GetIPAcquireFail(CIPSocket* socket, const unsigned char* packet);
static bool GetConnectSessionKey(CIPSocket* socket, const unsigned char* packet);
static bool GetIPKick(CIPSocket* socket, const unsigned char* packet);
static bool ReadyIPOK(CIPSocket* socket, const unsigned char* packet);
static bool ReadyIPFail(CIPSocket* socket, const unsigned char* packet);
static bool SetStartTimeFail(CIPSocket* socket, const unsigned char* packet);
static bool ReconnectOK(CIPSocket* socket, const unsigned char* packet);
static bool ReconnectFail(CIPSocket* socket, const unsigned char* packet);
static bool GetIPKindRes(CIPSocket* socket, const unsigned char* packet);

std::int32_t CIPPacketServer::g_nPendingPacket = 0;
std::int32_t CIPPacketServerPool::g_nAlloc = -1;
std::int32_t CIPPacketServerPool::g_nFree = 0;
CIPPacketServerPool::CSlot CIPPacketServerPool::g_slot[16];

bool IPServerReconnect = false;
CIPSocket* pIPSocket = nullptr;
void* g_hIPServerTimer = nullptr;
CRWLock gIPLock;
CIPSessionDB ipsessionDB;
CIPPacketServerPool theIPPacketPool;

// L2Authd.exe 0x00519798 contains these seventeen admitted handlers followed
// by a literal 0xFFFFFFFF. OnRead rejects opcode 17, so the linked sentinel is
// kept as data but can never be dispatched by the target parser.
static IPPacketHandler IPPacketFuncTable[18] = {
    GetConnectSessionKey, // 0
    DummyPacket,          // 1
    GetIPAcquireSuccess,  // 2
    StartIPCharge,        // 3
    StartIPChargeFail,    // 4
    GetIPAcquireFail,     // 5
    DummyPacket,          // 6
    DummyPacket,          // 7
    DummyPacket,          // 8
    GetIPKick,            // 9
    ReadyIPFail,          // 10
    ReadyIPOK,            // 11
    DummyPacket,          // 12
    SetStartTimeFail,     // 13
    ReconnectFail,        // 14
    ReconnectOK,          // 15
    GetIPKindRes,         // 16
    reinterpret_cast<IPPacketHandler>(
        static_cast<std::uintptr_t>(UINT32_MAX))};

namespace {

} // namespace

// L2Authd.exe 0x00456DD0, Src/ipsessiondb.cpp:36-46.
IPSUser* IPSUser::Alloc() {
    auto* const user = new IPSUser;
    user->pLoginUser = nullptr;
    user->mRefCount = 0;
    user->mOrigCmd = 0;
    user->mSendingCmd = 0;
    user->AddRef();
    return user;
}

// L2Authd.exe 0x00456E20, Src/ipsessiondb.cpp:63-68.
void IPSUser::Free() {
    if (pLoginUser != nullptr) {
        pLoginUser->ReleaseRef();
    }
    ::operator delete(this);
}

// L2Authd.exe 0x00456E60, Src/ipsessiondb.cpp:50-52.
void IPSUser::AddRef() {
    runtime::raw_atomic::IncrementI32(&mRefCount);
}

// L2Authd.exe 0x00456E80, Src/ipsessiondb.cpp:56-59.
void IPSUser::ReleaseRef() {
    if (runtime::raw_atomic::DecrementI32(
            &mRefCount) == 0) {
        Free();
    }
}

// L2Authd.exe 0x00456EB0, Src/ipsessiondb.cpp:238-267.
int CIPSessionDB::AddUserWait(
    int uid,
    IPSUser* user,
    IPSUser** displacedUser) {
    user->AddRef();
    bool inserted = false;
    int waitingCount = 0;
    {
        L2_GUARD(mWaitingUserLock);
        inserted = mWaitingUser.insert({uid, user}).second;
        waitingCount = inserted
            ? static_cast<int>(mWaitingUser.size())
            : 0;
    }

    if (!inserted) {
        IPSUser* previous = nullptr;
        DelUserWait(uid, &previous);
        if (displacedUser != nullptr) {
            *displacedUser = previous;
        }
        // The selected function does not release the failed insertion's map
        // reference, nor `previous` when the caller supplied no output.
    }
    return waitingCount;
}

// L2Authd.exe 0x00456F50, Src/ipsessiondb.cpp:206-235.
bool CIPSessionDB::DelUserWait(
    int uid,
    IPSUser** user) {
    IPSUser* previous = nullptr;
    {
        L2_GUARD(mWaitingUserLock);
        const auto found = mWaitingUser.find(uid);
        if (found == mWaitingUser.end()) {
            return false;
        }

        previous = found->second;
        mWaitingUser.erase(found);
    }
    if (user == nullptr) {
        previous->ReleaseRef();
    } else {
        *user = previous;
    }
    return true;
}

// L2Authd.exe 0x00457010, Src/ipsessiondb.cpp:349-354.
bool CIPSessionDB::IsIPCharging(int payStat) {
    return payStat > 0 && payStat < 1000;
}

// L2Authd.exe 0x00457030, Src/ipsessiondb.cpp:1696-1723.
char CIPSessionDB::StopIPCharge(
    unsigned int uid,
    unsigned int address,
    int payStat,
    int result,
    std::int32_t loginTime,
    char serverId,
    const char* account) {
    char returnCode = 0;
    try {
        const int sessionId = FindSessionID(
            static_cast<int>(uid));
        bool sent = true;
        if (sessionId == 0) {
            // Log.AddLog(LOG_WARN,
            //     "component=billing event=request_not_submitted peer=ip_server operation=stop_charge account_id=%u reason=session_missing result_code=10 action=return_failure",
            //     uid)
            // Log.AddLog(LOG_WARN,
            //     "[ip-billing] Could not stop charging account %u: no IP session exists. Returning failure code 10.",
            //     uid)
            L2_LOG_WARN(Log,
                "[ip-billing] Could not stop charging account %u: no IP session exists. Returning failure code 10.",
                uid);
            returnCode = 10;
        } else {
            if (config.UseIPServer &&
                pIPSocket != nullptr &&
                !IPServerReconnect) {
                {
                    L2_READ_GUARD(gIPLock);
                    sent = pIPSocket->Send(
                        "cddddcddsd",
                        AI_IP_STOP_CHARGE,
                        pIPSocket->ConnectSessionKey,
                        sessionId,
                        address,
                        payStat,
                        serverId,
                        result,
                        loginTime,
                        account,
                        config.gameId);
                }
            } else {
                // Log.AddLog(LOG_WARN,
                //     "component=billing event=request_skipped peer=ip_server operation=stop_charge account_id=%u reason=service_unavailable legacy_return=success",
                //     uid)
                // Log.AddLog(LOG_WARN,
                //     "[ip-billing] Skipping charge-stop for account %u: the IP service is unavailable. The existing path still returns success.",
                //     uid)
                L2_LOG_WARN(Log,
                    "[ip-billing] Skipping charge-stop for account %u: the IP service is unavailable. The existing path still returns success.",
                    uid);
            }
            if (!sent) {
                // Log.AddLog(LOG_WARN,
                //     "component=billing event=request_not_submitted peer=ip_server operation=stop_charge account_id=%u reason=socket_send_rejected result_code=10 action=return_failure",
                //     uid)
                // Log.AddLog(LOG_WARN,
                //     "[ip-billing] Could not send charge-stop for account %u. Returning failure code 10.",
                //     uid)
                // L2_LOG_WARN(Log,
                //     "[ip-billing] Could not send charge-stop for account %u. Returning failure code 10.",
                //     uid)
                L2_LOG_WARN(Log,
                    "[ip-billing] Could not send charge-stop for account %u: the IP connection is closed, reconnecting, or disabled. Returning failure code 10.",
                    uid);
                returnCode = 10;
            }
        }
    } catch (...) {
        // Original: ELOG("*%s:%d(%s) exception\n", "IPSessionDB.cpp", 1720,
        //     "Wed Oct 23 17:02:11 2019")
        ELOG("[IPSessionDB.cpp] Exception while requesting charge-stop for account %u. Entering exception handling.\n",
            uid);
        send_exception(true);
    }
    return returnCode;
}

// L2Authd.exe 0x00457190, Src/ipsessiondb.cpp:1668-1692.
char CIPSessionDB::StartIPCharge(
    unsigned int uid,
    unsigned int address,
    int payStat,
    char serverId) {
    char returnCode = 0;
    try {
        const int sessionId = FindSessionID(
            static_cast<int>(uid));
        if (pIPSocket == nullptr ||
            IPServerReconnect ||
            !config.UseIPServer ||
            sessionId == 0) {
            // Log.AddLog(LOG_WARN,
            //     "component=billing event=request_not_submitted peer=ip_server operation=start_charge account_id=%u reason=prerequisite_unavailable service_enabled=%u reconnecting=%u connection_present=%u session_present=%u result_code=10 action=return_failure",
            //     uid, static_cast<unsigned int>(config.UseIPServer), static_cast<unsigned int>(IPServerReconnect),
            //     static_cast<unsigned int>(pIPSocket != nullptr), static_cast<unsigned int>(sessionId != 0))
            // Log.AddLog(LOG_WARN,
            //     "[ip-billing] Could not start charging account %u: service enabled %u, reconnecting %u, connection present %u, session present %u. Returning failure code 10.",
            //     uid, static_cast<unsigned int>(config.UseIPServer), static_cast<unsigned int>(IPServerReconnect),
            //     static_cast<unsigned int>(pIPSocket != nullptr), static_cast<unsigned int>(sessionId != 0))
            L2_LOG_WARN(Log,
                "[ip-billing] Could not start charging account %u: service enabled %u, reconnecting %u, connection present %u, session present %u. Returning failure code 10.",
                uid, static_cast<unsigned int>(config.UseIPServer), static_cast<unsigned int>(IPServerReconnect),
                static_cast<unsigned int>(pIPSocket != nullptr), static_cast<unsigned int>(sessionId != 0));
            returnCode = 10;
        } else {
            bool sent = false;
            {
                L2_READ_GUARD(gIPLock);
                sent = pIPSocket->Send(
                    "cdddcdd",
                    AI_IP_START_CHARGE,
                    sessionId,
                    pIPSocket->ConnectSessionKey,
                    uid,
                    serverId,
                    address,
                    payStat);
            }
            if (!sent) {
                // Log.AddLog(LOG_WARN,
                //     "component=billing event=request_not_submitted peer=ip_server operation=start_charge account_id=%u reason=socket_send_rejected result_code=10 action=return_failure",
                //     uid)
                // Log.AddLog(LOG_WARN,
                //     "[ip-billing] Could not send charge-start for account %u. Returning failure code 10.",
                //     uid)
                // L2_LOG_WARN(Log,
                //     "[ip-billing] Could not send charge-start for account %u. Returning failure code 10.",
                //     uid)
                L2_LOG_WARN(Log,
                    "[ip-billing] Could not send charge-start for account %u: the IP connection is closed, reconnecting, or disabled. Returning failure code 10.",
                    uid);
                returnCode = 10;
            }
        }
    } catch (...) {
        // Original: ELOG("*%s:%d(%s) exception\n", "IPSessionDB.cpp", 1690,
        //     "Wed Oct 23 17:02:11 2019")
        ELOG("[IPSessionDB.cpp] Exception while requesting charge-start for account %u. Entering exception handling.\n",
            uid);
        send_exception(true);
    }
    return returnCode;
}

// L2Authd.exe 0x004572D0, Src/ipsessiondb.cpp:764-777.
char CIPSessionDB::ReleaseSessionRequest(
    int sessionId,
    in_addr address,
    int payStat) {
    try {
        if (sessionId != 0 &&
            config.UseIPServer &&
            !IPServerReconnect &&
            pIPSocket != nullptr) {
            bool sent = false;
            {
                L2_READ_GUARD(gIPLock);
                // pIPSocket->Send(
                //     "cddddd",
                //     AI_IP_RELEASE,
                //     sessionId,
                //     pIPSocket->ConnectSessionKey,
                //     config.gameId,
                //     address.s_addr,
                //     payStat)
                sent = pIPSocket->Send(
                    "cddddd",
                    AI_IP_RELEASE,
                    sessionId,
                    pIPSocket->ConnectSessionKey,
                    config.gameId,
                    address.s_addr,
                    payStat);
            }
            if (!sent) {
                // Log.AddLog(LOG_WARN,
                //     "component=billing event=request_not_submitted peer=ip_server operation=release_session reason=socket_send_rejected legacy_return=success")
                // Log.AddLog(LOG_WARN,
                //     "[ip-billing] Could not send the session-release request. The existing path still returns success.")
                // L2_LOG_WARN(Log,
                //     "[ip-billing] Could not send the session-release request. The existing path still returns success.")
                L2_LOG_WARN(Log,
                    "[ip-billing] Could not send the session-release request: the IP connection is closed, reconnecting, or disabled. The existing path still returns success.");
            }
        } else if (sessionId != 0) {
            // Log.AddLog(LOG_WARN,
            //     "component=billing event=request_skipped peer=ip_server operation=release_session reason=service_unavailable legacy_return=success")
            // Log.AddLog(LOG_WARN,
            //     "[ip-billing] Skipping session release: the IP service is unavailable. The existing path still returns success.")
            L2_LOG_WARN(Log,
                "[ip-billing] Skipping session release: the IP service is unavailable. The existing path still returns success.");
        }
    } catch (...) {
        // Original: ELOG("*%s:%d(%s) exception\n", "IPSessionDB.cpp", 775,
        //     "Wed Oct 23 17:02:11 2019")
        ELOG("[IPSessionDB.cpp] Exception while requesting IP session release. Entering exception handling.\n");
        send_exception(true);
    }
    return 0;
}

// L2Authd.exe 0x004573F0, Src/ipsessiondb.cpp:1726-1751.
char CIPSessionDB::ReadyToIPCharge(
    unsigned int uid,
    unsigned int address,
    int payStat,
    char serverId) {
    char returnCode = 0;
    try {
        const int sessionId = FindSessionID(
            static_cast<int>(uid));
        if (pIPSocket == nullptr ||
            IPServerReconnect ||
            !config.UseIPServer ||
            sessionId == 0) {
            // Log.AddLog(LOG_WARN,
            //     "component=billing event=request_not_submitted peer=ip_server operation=ready_to_play account_id=%u reason=prerequisite_unavailable service_enabled=%u reconnecting=%u connection_present=%u session_present=%u result_code=10 action=return_failure",
            //     uid, static_cast<unsigned int>(config.UseIPServer), static_cast<unsigned int>(IPServerReconnect),
            //     static_cast<unsigned int>(pIPSocket != nullptr), static_cast<unsigned int>(sessionId != 0))
            // Log.AddLog(LOG_WARN,
            //     "[ip-billing] Could not request ready-to-play for account %u: service enabled %u, reconnecting %u, connection present %u, session present %u. Returning failure code 10.",
            //     uid, static_cast<unsigned int>(config.UseIPServer), static_cast<unsigned int>(IPServerReconnect),
            //     static_cast<unsigned int>(pIPSocket != nullptr), static_cast<unsigned int>(sessionId != 0))
            L2_LOG_WARN(Log,
                "[ip-billing] Could not request ready-to-play for account %u: service enabled %u, reconnecting %u, connection present %u, session present %u. Returning failure code 10.",
                uid, static_cast<unsigned int>(config.UseIPServer), static_cast<unsigned int>(IPServerReconnect),
                static_cast<unsigned int>(pIPSocket != nullptr), static_cast<unsigned int>(sessionId != 0));
            returnCode = 10;
        } else {
            bool sent = false;
            {
                L2_READ_GUARD(gIPLock);
                sent = pIPSocket->Send(
                    "cdddcdd",
                    AI_IP_READY_GAME,
                    sessionId,
                    pIPSocket->ConnectSessionKey,
                    uid,
                    serverId,
                    address,
                    payStat);
            }
            if (!sent) {
                // Log.AddLog(LOG_WARN,
                //     "component=billing event=request_not_submitted peer=ip_server operation=ready_to_play account_id=%u reason=socket_send_rejected result_code=10 action=return_failure",
                //     uid)
                // Log.AddLog(LOG_WARN,
                //     "[ip-billing] Could not send ready-to-play for account %u. Returning failure code 10.",
                //     uid)
                // L2_LOG_WARN(Log,
                //     "[ip-billing] Could not send ready-to-play for account %u. Returning failure code 10.",
                //     uid)
                L2_LOG_WARN(Log,
                    "[ip-billing] Could not send ready-to-play for account %u: the IP connection is closed, reconnecting, or disabled. Returning failure code 10.",
                    uid);
                returnCode = 10;
            }
        }
    } catch (...) {
        // Original: ELOG("*%s:%d(%s) exception\n", "IPSessionDB.cpp", 1749,
        //     "Wed Oct 23 17:02:11 2019")
        ELOG("[IPSessionDB.cpp] Exception while requesting ready-to-play for account %u. Entering exception handling.\n",
            uid);
        send_exception(true);
    }
    return returnCode;
}

// L2Authd.exe 0x00457530, Src/ipsessiondb.cpp:1754-1777.
char CIPSessionDB::ConfirmIPCharge(
    unsigned int uid,
    unsigned int address,
    int payStat,
    char serverId) {
    char returnCode = 0;
    try {
        const int sessionId = FindSessionID(
            static_cast<int>(uid));
        if (pIPSocket == nullptr ||
            IPServerReconnect ||
            !config.UseIPServer ||
            sessionId == 0) {
            // Log.AddLog(LOG_WARN,
            //     "component=billing event=request_not_submitted peer=ip_server operation=confirm_start_time account_id=%u reason=prerequisite_unavailable service_enabled=%u reconnecting=%u connection_present=%u session_present=%u result_code=10 action=return_failure",
            //     uid, static_cast<unsigned int>(config.UseIPServer), static_cast<unsigned int>(IPServerReconnect),
            //     static_cast<unsigned int>(pIPSocket != nullptr), static_cast<unsigned int>(sessionId != 0))
            // Log.AddLog(LOG_WARN,
            //     "[ip-billing] Could not confirm start time for account %u: service enabled %u, reconnecting %u, connection present %u, session present %u. Returning failure code 10.",
            //     uid, static_cast<unsigned int>(config.UseIPServer), static_cast<unsigned int>(IPServerReconnect),
            //     static_cast<unsigned int>(pIPSocket != nullptr), static_cast<unsigned int>(sessionId != 0))
            L2_LOG_WARN(Log,
                "[ip-billing] Could not confirm start time for account %u: service enabled %u, reconnecting %u, connection present %u, session present %u. Returning failure code 10.",
                uid, static_cast<unsigned int>(config.UseIPServer), static_cast<unsigned int>(IPServerReconnect),
                static_cast<unsigned int>(pIPSocket != nullptr), static_cast<unsigned int>(sessionId != 0));
            returnCode = 10;
        } else {
            bool sent = false;
            {
                L2_READ_GUARD(gIPLock);
                sent = pIPSocket->Send(
                    "cdddcdd",
                    AI_IP_SET_START_TIME,
                    sessionId,
                    pIPSocket->ConnectSessionKey,
                    uid,
                    serverId,
                    address,
                    payStat);
            }
            if (!sent) {
                // Log.AddLog(LOG_WARN,
                //     "component=billing event=request_not_submitted peer=ip_server operation=confirm_start_time account_id=%u reason=socket_send_rejected result_code=10 action=return_failure",
                //     uid)
                // Log.AddLog(LOG_WARN,
                //     "[ip-billing] Could not send start-time confirmation for account %u. Returning failure code 10.",
                //     uid)
                // L2_LOG_WARN(Log,
                //     "[ip-billing] Could not send start-time confirmation for account %u. Returning failure code 10.",
                //     uid)
                L2_LOG_WARN(Log,
                    "[ip-billing] Could not send start-time confirmation for account %u: the IP connection is closed, reconnecting, or disabled. Returning failure code 10.",
                    uid);
                returnCode = 10;
            }
        }
    } catch (...) {
        // Original: ELOG("*%s:%d(%s) exception\n", "IPSessionDB.cpp", 1775,
        //     "Wed Oct 23 17:02:11 2019")
        ELOG("[IPSessionDB.cpp] Exception while confirming charge start time for account %u. Entering exception handling.\n",
            uid);
        send_exception(true);
    }
    return returnCode;
}

// L2Authd.exe 0x00457670, Src/ipsessiondb.cpp:415-457.
char CIPSessionDB::RequestIPCharge(
    int uid,
    LoginUser* user,
    char requestType) {
    const int chargeKind = (user->stat % 1000) / 100;
    for (int priorityIndex = 0;
         priorityIndex < 4;
         ++priorityIndex) {
        switch (config.ChargePriority[priorityIndex]) {
        case 0:
            if (!user->ipServer.otherIPCharging) {
                return AcquireSessionRequest(
                    user,
                    uid,
                    requestType);
            }
            if (config.useNoBill &&
                !config.usePBServer) {
                return 11;
            }
            break;
        case 1:
            if (chargeKind == 1) {
                return 10;
            }
            break;
        case 2:
            if (chargeKind == 2) {
                return 10;
            }
            break;
        case 3:
            if (chargeKind == 5) {
                return 10;
            }
            break;
        default:
            break;
        }
    }
    return 10;
}

// L2Authd.exe 0x00457770, Src/ipsessiondb.cpp:357-408.
char CIPSessionDB::RequestIPKind(
    LoginUser* user,
    int uid,
    char requestType) {
    try {
        IPSUser* const waitingUser = IPSUser::Alloc();
        waitingUser->pLoginUser = user;
        user->AddRef();
        waitingUser->mOrigCmd = requestType;
        waitingUser->mSendingCmd = AI_IP_GET_KIND;

        IPSUser* displacedUser = nullptr;
        const int waitingCount = AddUserWait(
            uid,
            waitingUser,
            &displacedUser);
        if (waitingCount < 1) {
            // Log.AddLog(LOG_WARN,
            //     "component=billing event=request_registration_failed peer=ip_server operation=get_charge_kind account_id=%d origin=%u reason=account_already_pending displaced_request=%u result_code=11 action=return_failure",
            //     uid, static_cast<unsigned int>(static_cast<unsigned char>(requestType)), static_cast<unsigned int>(displacedUser != nullptr))
            // Log.AddLog(LOG_WARN,
            //     "[ip-billing] Charge-kind request for account %d (origin %u) conflicts with a pending request. Previous request displaced: %u. Returning failure code 11.",
            //     uid, static_cast<unsigned int>(static_cast<unsigned char>(requestType)), static_cast<unsigned int>(displacedUser != nullptr))
            // L2_LOG_WARN(Log,
            //     "[ip-billing] Charge-kind request for account %d (origin %u) conflicts with a pending request. Previous request displaced: %u. Returning failure code 11.",
            //     uid, static_cast<unsigned int>(static_cast<unsigned char>(requestType)), static_cast<unsigned int>(displacedUser != nullptr))
            L2_LOG_WARN(Log,
                "[ip-billing] Charge-kind request for account %d (origin %u) conflicts with a pending request. %s Returning failure code 11.",
                uid, static_cast<unsigned int>(static_cast<unsigned char>(requestType)),
                displacedUser != nullptr
                    ? "The conflicting request was removed; its account will be disconnected."
                    : "The conflicting request was no longer present when cleanup ran.");
            if (displacedUser != nullptr) {
                KickDuplicatedUserWaitAccount(
                    displacedUser);
            }
            waitingUser->ReleaseRef();
            return 11;
        }
        if (pIPSocket == nullptr ||
            IPServerReconnect ||
            !config.UseIPServer) {
            // Log.AddLog(LOG_WARN,
            //     "component=billing event=request_not_submitted peer=ip_server operation=get_charge_kind account_id=%d reason=service_unavailable service_enabled=%u reconnecting=%u connection_present=%u result_code=10 action=remove_pending_request",
            //     uid, static_cast<unsigned int>(config.UseIPServer), static_cast<unsigned int>(IPServerReconnect), static_cast<unsigned int>(pIPSocket != nullptr))
            // Log.AddLog(LOG_WARN,
            //     "[ip-billing] Charge-kind request for account %d cannot be sent: service enabled %u, reconnecting %u, connection present %u. Removing the pending request and returning code 10.",
            //     uid, static_cast<unsigned int>(config.UseIPServer), static_cast<unsigned int>(IPServerReconnect), static_cast<unsigned int>(pIPSocket != nullptr))
            L2_LOG_WARN(Log,
                "[ip-billing] Charge-kind request for account %d cannot be sent: service enabled %u, reconnecting %u, connection present %u. Removing the pending request and returning code 10.",
                uid, static_cast<unsigned int>(config.UseIPServer), static_cast<unsigned int>(IPServerReconnect), static_cast<unsigned int>(pIPSocket != nullptr));
            DelUserWait(uid, nullptr);
            waitingUser->ReleaseRef();
            return 10;
        }

        const bool sent = pIPSocket->Send(
            "cddd",
            AI_IP_GET_KIND,
            user->loginIp.s_addr,
            config.gameId,
            uid);
        if (!sent) {
            // Log.AddLog(LOG_WARN,
            //     "component=billing event=request_not_submitted peer=ip_server operation=get_charge_kind account_id=%d reason=socket_send_rejected result_code=10 action=remove_pending_request",
            //     uid)
            // Log.AddLog(LOG_WARN,
            //     "[ip-billing] Could not send charge-kind request for account %d. Removing the pending request and returning code 10.",
            //     uid)
            // L2_LOG_WARN(Log,
            //     "[ip-billing] Could not send charge-kind request for account %d. Removing the pending request and returning code 10.",
            //     uid)
            L2_LOG_WARN(Log,
                "[ip-billing] Could not send charge-kind request for account %d: the IP connection is closed, reconnecting, or disabled. Removing the pending request and returning code 10.",
                uid);
            DelUserWait(uid, nullptr);
            waitingUser->ReleaseRef();
            return 10;
        }

        waitingUser->ReleaseRef();
        const std::time_t now = std::time(nullptr);
        if (std::difftime(
                now,
                static_cast<std::time_t>(
                    waitUserCheckTime)) > 60.0) {
            // const std::tm local = *std::localtime(&now)
            const std::tm local = runtime::crt::LocalTimeOrZero(now);
            fileLog.AddLog(
                LOG_NORMAL,
                "%d-%02d-%02d %02d:%02d:%02d, "
                "IPServer waiting user count : %d\r\n",
                local.tm_year + 1900,
                local.tm_mon + 1,
                local.tm_mday,
                local.tm_hour,
                local.tm_min,
                local.tm_sec,
                waitingCount);
            waitUserCheckTime = static_cast<std::int32_t>(now);
        }
        return 0;
    } catch (...) {
        // Original: ELOG("*%s:%d(%s) exception\n", "IPSessionDB.cpp", 406,
        //     "Wed Oct 23 17:02:11 2019")
        ELOG("[IPSessionDB.cpp] Exception while requesting charge-kind information for account %d. Entering exception handling.\n",
            uid);
        send_exception(true);
        return 0;
    }
}

// L2Authd.exe 0x004579E0, Src/ipsessiondb.cpp:607-722.
void CIPSessionDB::IPKindRes(
    int kindResult,
    [[maybe_unused]] int gameAccountNo,
    int valueType,
    [[maybe_unused]] int valueKind,
    int uid,
    char* cafeId,
    int inCharge,
    [[maybe_unused]] int restrictionNo,
    char* chargeState) {
    IPSUser* waitingUser = nullptr;
    if (!DelUserWait(uid, &waitingUser)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "User infomation was deleted while getting "
        //     "payment info from IPS(%d)",
        //     uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=billing event=reply_without_pending_request peer=ip_server operation=get_charge_kind account_id=%d action=ignore_reply",
        //     uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[ip-billing] Ignoring charge-kind reply for account %d: no request is pending.",
        //     uid)
        // L2_LOG_WARN(Log,
        //     "[ip-billing] Ignoring charge-kind reply for account %d: no request is pending.",
        //     uid)
        L2_LOG_WARN(Log,
            "[ip-billing] Ignoring charge-kind reply for account %d (peer result %d, value type %d): no request is pending.",
            uid, kindResult, valueType);
        return;
    }

    LoginUser* const user = waitingUser->pLoginUser;
    user->ipServer.otherIPCharging =
        config.ignoreWhenIPInCharge &&
        (inCharge != 0 || chargeState[0] != '0');

    if (waitingUser->mOrigCmd == 0x1c) {
        std::strncpy(user->loginCafe, cafeId, 9);
        user->ipServer.valueType =
            static_cast<char>(valueType);
        const char loginResult =
            CheckServerSideLogin(uid, waitingUser);
        if (loginResult != 0) {
            // Log.AddLog(LOG_NORMAL,
            //     "component=billing event=login_rejected peer=ip_server operation=server_login account_id=%d reason_code=%d action=send_world_failure",
            //     uid, static_cast<int>(loginResult))
            // Log.AddLog(LOG_NORMAL,
            //     "[ip-billing] World login rejected for account %d (reason %d). Sending failure to the world server.",
            //     uid, static_cast<int>(loginResult))
            // L2_LOG_INFO(Log,
            //     "[ip-billing] World login rejected for account %d (reason %d). Sending failure to the world server.",
            //     uid, static_cast<int>(loginResult))
            L2_LOG_INFO(Log,
                "[ip-billing] World login rejected for account %d (reason %d). Attempting to notify world server %d.",
                uid, static_cast<int>(loginResult), static_cast<int>(user->serverid));
            const WorldServer world =
                g_ServerList.GetAt(user->serverid);
            const int forwarded = SendSocket(
                world.inner_addr,
                "cdc",
                0x15,
                uid,
                loginResult);
            if (forwarded == 0) {
                L2_LOG_WARN(Log,
                    "[ip-billing] Could not forward login rejection for account %d (reason %d) to world server %d: no connection exists. Releasing the pending request.",
                    uid, static_cast<int>(loginResult), static_cast<int>(user->serverid));
            }
        }
        waitingUser->ReleaseRef();
        return;
    }

    CClientSession* const client =
        serverEx.FindSession(user->sid);
    if (client == nullptr) {
        // Log.AddLog(LOG_NORMAL,
        //     "component=billing event=reply_context_missing peer=ip_server operation=get_charge_kind account_id=%d reason=client_session_missing action=release_pending_request",
        //     uid)
        // Log.AddLog(LOG_NORMAL,
        //     "[ip-billing] Charge-kind reply for account %d has no client session. Releasing the pending request.",
        //     uid)
        L2_LOG_INFO(Log,
            "[ip-billing] Charge-kind reply for account %d has no client session. Releasing the pending request.",
            uid);
        waitingUser->ReleaseRef();
        return;
    }

    if (valueType < 1 ||
        kindResult < 1 ||
        user->clientInstances < 1) {
        if (valueType == 99) {
            // Log.AddLog(LOG_NORMAL,
            //     "component=billing event=login_rejected peer=ip_server operation=get_charge_kind account_id=%d reason=restricted_charge_kind client_reason=22 action=send_client_failure",
            //     uid)
            // Log.AddLog(LOG_NORMAL,
            //     "[ip-billing] Login rejected for account %d: charge kind is restricted. Sending client failure code 22.",
            //     uid)
            L2_LOG_INFO(Log,
                "[ip-billing] Login rejected for account %d: charge kind is restricted. Sending client failure code 22.",
                uid);
            client->SendFail(1, 0x16, user);
        } else {
            std::strncpy(user->loginCafe, cafeId, 9);
            if (valueType < 1) {
                accountdb.CheckPersonalPayStat(
                    client,
                    user,
                    uid);
            } else {
                user->ipServer.valueType =
                    static_cast<char>(valueType);
                if (!config.usePBServer) {
                    const char requestResult =
                        RequestIPCharge(uid, user, 0);
                    if (requestResult != 0) {
                        if (requestResult == 11) {
                            if (!config.allowF2PMultiClients) {
                                // Log.AddLog(LOG_NORMAL,
                                //     "component=billing event=login_rejected peer=ip_server operation=get_charge_kind account_id=%d reason=ip_charge_rejected client_reason=21 action=send_client_failure",
                                //     uid)
                                // Log.AddLog(LOG_NORMAL,
                                //     "[ip-billing] Login rejected for account %d: IP charging was rejected. Sending client failure code 21.",
                                //     uid)
                                // L2_LOG_INFO(Log,
                                //     "[ip-billing] Login rejected for account %d: IP charging was rejected. Sending client failure code 21.",
                                //     uid)
                                L2_LOG_INFO(Log,
                                    "[ip-billing] Login rejected for account %d: the IP session request returned code 11. Sending client failure code 21.",
                                    uid);
                                client->SendFail(1, 0x15, user);
                            } else {
                                user->stat = 2000;
                                accountdb.CheckPersonalPayStat(
                                    client,
                                    user,
                                    uid);
                            }
                        } else {
                            accountdb.CheckPersonalPayStat(
                                client,
                                user,
                                uid);
                        }
                    }
                } else {
                    accountdb.CheckPersonalPayStat(
                        client,
                        user,
                        uid);
                }
            }
        }
    } else {
        // Log.AddLog(LOG_NORMAL,
        //     "component=billing event=login_rejected peer=ip_server operation=get_charge_kind account_id=%d reason=multiple_clients_not_allowed client_reason=35 action=send_client_failure",
        //     uid)
        // Log.AddLog(LOG_NORMAL,
        //     "[ip-billing] Login rejected for account %d: multiple clients are not allowed. Sending client failure code 35.",
        //     uid)
        L2_LOG_INFO(Log,
            "[ip-billing] Login rejected for account %d: multiple clients are not allowed. Sending client failure code 35.",
            uid);
        client->SendFail(1, 0x23, user);
    }

    waitingUser->ReleaseRef();
    client->ReleaseRef();
}

// L2Authd.exe 0x00457D90, Src/ipsessiondb.cpp:725-761.
char CIPSessionDB::AcquireSessionRequest(
    LoginUser* user,
    int uid,
    char requestType) {
    try {
        IPSUser* const waitingUser = IPSUser::Alloc();
        waitingUser->pLoginUser = user;
        user->AddRef();
        waitingUser->mOrigCmd = requestType;
        waitingUser->mSendingCmd = AI_IP_ACQUIRE;

        IPSUser* displacedUser = nullptr;
        const int waitingCount = AddUserWait(
            uid,
            waitingUser,
            &displacedUser);
        if (waitingCount < 1) {
            // Log.AddLog(LOG_WARN,
            //     "component=billing event=request_registration_failed peer=ip_server operation=acquire_session account_id=%d origin=%u reason=account_already_pending displaced_request=%u result_code=11 action=return_failure",
            //     uid, static_cast<unsigned int>(static_cast<unsigned char>(requestType)), static_cast<unsigned int>(displacedUser != nullptr))
            // Log.AddLog(LOG_WARN,
            //     "[ip-billing] Session-acquisition request for account %d (origin %u) conflicts with a pending request. Previous request displaced: %u. Returning failure code 11.",
            //     uid, static_cast<unsigned int>(static_cast<unsigned char>(requestType)), static_cast<unsigned int>(displacedUser != nullptr))
            // L2_LOG_WARN(Log,
            //     "[ip-billing] Session-acquisition request for account %d (origin %u) conflicts with a pending request. Previous request displaced: %u. Returning failure code 11.",
            //     uid, static_cast<unsigned int>(static_cast<unsigned char>(requestType)), static_cast<unsigned int>(displacedUser != nullptr))
            L2_LOG_WARN(Log,
                "[ip-billing] Session-acquisition request for account %d (origin %u) conflicts with a pending request. %s Returning failure code 11.",
                uid, static_cast<unsigned int>(static_cast<unsigned char>(requestType)),
                displacedUser != nullptr
                    ? "The conflicting request was removed; its account will be disconnected."
                    : "The conflicting request was no longer present when cleanup ran.");
            if (displacedUser != nullptr) {
                KickDuplicatedUserWaitAccount(
                    displacedUser);
            }
            waitingUser->ReleaseRef();
            return 11;
        }
        if (pIPSocket == nullptr ||
            IPServerReconnect ||
            !config.UseIPServer) {
            // Log.AddLog(LOG_WARN,
            //     "component=billing event=request_not_submitted peer=ip_server operation=acquire_session account_id=%d reason=service_unavailable service_enabled=%u reconnecting=%u connection_present=%u result_code=10 action=remove_pending_request",
            //     uid, static_cast<unsigned int>(config.UseIPServer), static_cast<unsigned int>(IPServerReconnect), static_cast<unsigned int>(pIPSocket != nullptr))
            // Log.AddLog(LOG_WARN,
            //     "[ip-billing] Session acquisition for account %d cannot be sent: service enabled %u, reconnecting %u, connection present %u. Removing the pending request and returning code 10.",
            //     uid, static_cast<unsigned int>(config.UseIPServer), static_cast<unsigned int>(IPServerReconnect), static_cast<unsigned int>(pIPSocket != nullptr))
            L2_LOG_WARN(Log,
                "[ip-billing] Session acquisition for account %d cannot be sent: service enabled %u, reconnecting %u, connection present %u. Removing the pending request and returning code 10.",
                uid, static_cast<unsigned int>(config.UseIPServer), static_cast<unsigned int>(IPServerReconnect), static_cast<unsigned int>(pIPSocket != nullptr));
            DelUserWait(uid, nullptr);
            waitingUser->ReleaseRef();
            return 10;
        }

        const bool sent = pIPSocket->Send(
            "csddd",
            AI_IP_ACQUIRE,
            user->account,
            user->loginIp.s_addr,
            config.gameId,
            uid);
        if (!sent) {
            // Log.AddLog(LOG_WARN,
            //     "component=billing event=request_not_submitted peer=ip_server operation=acquire_session account_id=%d reason=socket_send_rejected result_code=10 action=remove_pending_request",
            //     uid)
            // Log.AddLog(LOG_WARN,
            //     "[ip-billing] Could not send session acquisition for account %d. Removing the pending request and returning code 10.",
            //     uid)
            // L2_LOG_WARN(Log,
            //     "[ip-billing] Could not send session acquisition for account %d. Removing the pending request and returning code 10.",
            //     uid)
            L2_LOG_WARN(Log,
                "[ip-billing] Could not send session acquisition for account %d: the IP connection is closed, reconnecting, or disabled. Removing the pending request and returning code 10.",
                uid);
            DelUserWait(uid, nullptr);
            waitingUser->ReleaseRef();
            return 10;
        }
        waitingUser->ReleaseRef();
        return 0;
    } catch (...) {
        // Original: ELOG("*%s:%d(%s) exception\n", "IPSessionDB.cpp", 759,
        //     "Wed Oct 23 17:02:11 2019")
        ELOG("[IPSessionDB.cpp] Exception while requesting an IP session for account %d. Entering exception handling.\n",
            uid);
        send_exception(true);
        return 0;
    }
}

// L2Authd.exe 0x00457F50, Src/ipsessiondb.cpp:781-993.
char CIPSessionDB::AcquireSessionSuccess(
    int uid,
    int sessionId,
    char result,
    int remainingTime,
    int payStat) {
    IPSUser* waitingUser = nullptr;
    if (!DelUserWait(uid, &waitingUser)) {
        // Log.AddLog(LOG_WARN,
        //     "component=billing event=reply_without_pending_request peer=ip_server operation=acquire_session account_id=%d action=release_remote_session",
        //     uid)
        // Log.AddLog(LOG_WARN,
        //     "[ip-billing] Session-acquisition reply for account %d has no pending request. Releasing the remote session.",
        //     uid)
        // L2_LOG_WARN(Log,
        //     "[ip-billing] Session-acquisition reply for account %d has no pending request. Releasing the remote session.",
        //     uid)
        L2_LOG_WARN(Log,
            "[ip-billing] Session-acquisition reply for account %d has no pending request. Requesting remote session release.",
            uid);
        in_addr emptyAddress{};
        ReleaseSessionRequest(
            sessionId,
            emptyAddress,
            payStat);
        return 1;
    }

    LoginUser* const user = waitingUser->pLoginUser;
    if (waitingUser->mOrigCmd == 8) {
        ChangeServerAck(
            uid,
            sessionId,
            user,
            remainingTime,
            payStat,
            true);
        waitingUser->ReleaseRef();
        return result;
    }

    user->stat = payStat;
    user->ipServer.valueType =
        static_cast<char>(payStat);
    user->ipServer.remainTime = remainingTime;
    user->chargeServerId =
        accountdb.DetermineChargeServer(user);

    if (waitingUser->mOrigCmd == 0x1c) {
        user->logintime = runtime::crt::Time32Now();
        const char registrationResult =
            accountdb.RegAccount(uid, user);
        if (registrationResult != 0) {
            // Log.AddLog(LOG_NORMAL,
            //     "component=billing event=login_rejected peer=ip_server operation=server_login account_id=%d phase=account_registration reason_code=%d action=send_world_failure",
            //     uid, static_cast<int>(registrationResult))
            // Log.AddLog(LOG_NORMAL,
            //     "[ip-billing] World login rejected for account %d during account registration (reason %d). Sending failure to the world server.",
            //     uid, static_cast<int>(registrationResult))
            // L2_LOG_INFO(Log,
            //     "[ip-billing] World login rejected for account %d during account registration (reason %d). Sending failure to the world server.",
            //     uid, static_cast<int>(registrationResult))
            // L2_LOG_INFO(Log,
            //     "[ip-billing] Another login is already registered for account %d (reason %d). Rejecting world login and sending failure to the world server.",
            //     uid, static_cast<int>(registrationResult))
            L2_LOG_INFO(Log,
                "[ip-billing] Another login is already registered for account %d (reason %d). Rejecting world login and attempting to notify world server %d.",
                uid, static_cast<int>(registrationResult), static_cast<int>(user->serverid));
            const WorldServer world =
                g_ServerList.GetAt(user->serverid);
            const int forwarded = SendSocket(
                world.inner_addr,
                "cdc",
                0x15,
                uid,
                registrationResult);
            if (forwarded == 0) {
                L2_LOG_WARN(Log,
                    "[ip-billing] Could not forward login rejection for account %d (reason %d) to world server %d: no connection exists. Releasing the pending request.",
                    uid, static_cast<int>(registrationResult), static_cast<int>(user->serverid));
            }
            user->ReleaseRef();
            waitingUser->ReleaseRef();
            return 0;
        }

        if (AddSessionID(uid, sessionId) == 0) {
            accountdb.LogoutProcess(uid, 0);
            waitingUser->ReleaseRef();
            return 0;
        }
        if (!accountdb.recordGamePlayTime(
                uid,
                user->serverid)) {
            // Log.AddLog(LOG_WARN,
            //     "component=billing event=play_time_record_failed peer=ip_server operation=server_login account_id=%d client_reason=20 action=send_world_failure",
            //     uid)
            // Log.AddLog(LOG_WARN,
            //     "[ip-billing] Could not record play time for account %d during world login. Sending failure code 20 to the world server.",
            //     uid)
            // L2_LOG_WARN(Log,
            //     "[ip-billing] Could not record play time for account %d during world login. Sending failure code 20 to the world server.",
            //     uid)
            L2_LOG_WARN(Log,
                "[ip-billing] Could not record play time for account %d during world login. Attempting to send failure code 20 to world server %d.",
                uid, static_cast<int>(user->serverid));
            const WorldServer world =
                g_ServerList.GetAt(user->serverid);
            const int forwarded = SendSocket(
                world.inner_addr,
                "cdc",
                0x15,
                uid,
                0x14);
            if (forwarded == 0) {
                L2_LOG_WARN(Log,
                    "[ip-billing] Could not forward play-time recording failure for account %d to world server %d: no connection exists. Releasing the pending request.",
                    uid, static_cast<int>(user->serverid));
            }
            waitingUser->ReleaseRef();
            return 0;
        }

        int fatigueRestTime = 0;
        int fatiguePlayTime = 0;
        int fatigueSecondsSinceLastLogout = 0;
        if (config.useFatigueSystem &&
            AccountContainer::Inst()->IsFatigueCustomer(
                user->fcid,
                user->age,
                user->auth_flag)) {
            AccountContainer::Inst()->GetAccountTime(
                user->fcid,
                &fatigueRestTime,
                &fatiguePlayTime);
            AccountContainer::Inst()->GetLastLogoutTime(
                user->fcid,
                &fatigueSecondsSinceLastLogout);
        }

        char zoneFatigueInfo[1556]{};
        const int zoneFatigueInfoLength = 0;
        [[maybe_unused]] const unsigned char
            zoneFatigueAvailableCount = 0;
        const int chargedItemId =
            user->chargeServerId == 2
            ? user->pbServer.itemId
            : 0;
        const WorldServer world =
            g_ServerList.GetAt(user->serverid);
        int forwarded = 0;
        if (config.useExtendPacket) {
            if (config.useGradeServer) {
                user->user_level = 0;
            }
            forwarded = SendSocket(
                world.inner_addr,
                "cdcddddddchbbhd",
                0x15,
                uid,
                0,
                remainingTime,
                user->loginflag,
                0,
                fatigueRestTime,
                fatiguePlayTime,
                fatigueSecondsSinceLastLogout,
                user->packageVersionInfo,
                zoneFatigueInfoLength,
                zoneFatigueInfoLength,
                zoneFatigueInfo,
                24,
                user->byteChildPlayTimeInfo,
                user->user_level,
                chargedItemId);
        } else {
            forwarded = SendSocket(
                world.inner_addr,
                "cdcddddddchbd",
                0x15,
                uid,
                0,
                remainingTime,
                user->loginflag,
                0,
                fatigueRestTime,
                fatiguePlayTime,
                fatigueSecondsSinceLastLogout,
                user->packageVersionInfo,
                zoneFatigueInfoLength,
                zoneFatigueInfoLength,
                zoneFatigueInfo,
                chargedItemId);
        }
        if (forwarded == 0) {
            L2_LOG_WARN(Log,
                "[ip-billing] Could not forward successful login for account %d to world server %d: no connection exists. Local registration is retained; the handler still returns success.",
                uid, static_cast<int>(user->serverid));
        }
        waitingUser->ReleaseRef();
        return 1;
    }

    if (waitingUser->mOrigCmd == 0) {
        CClientSession* const client =
            serverEx.FindSession(user->sid);
        if (client == nullptr) {
            // Log.AddLog(LOG_NORMAL,
            //     "component=billing event=reply_context_missing peer=ip_server operation=acquire_session account_id=%d reason=client_session_missing action=release_remote_session",
            //     uid)
            // Log.AddLog(LOG_NORMAL,
            //     "[ip-billing] Session-acquisition reply for account %d has no client session. Releasing the remote session.",
            //     uid)
            // L2_LOG_INFO(Log,
            //     "[ip-billing] Session-acquisition reply for account %d has no client session. Releasing the remote session.",
            //     uid)
            L2_LOG_INFO(Log,
                "[ip-billing] Session-acquisition reply for account %d has no client session. Requesting remote session release.",
                uid);
            ReleaseSessionRequest(
                sessionId,
                user->loginIp,
                payStat);
            waitingUser->ReleaseRef();
            return 0;
        }
        if (client->GetSessionId() == UINT32_MAX) {
            // Log.AddLog(LOG_NORMAL,
            //     "component=billing event=reply_context_missing peer=ip_server operation=acquire_session account_id=%d reason=client_session_closed action=release_remote_session",
            //     uid)
            // Log.AddLog(LOG_NORMAL,
            //     "[ip-billing] Client session for account %d closed before acquisition completed. Releasing the remote session.",
            //     uid)
            // L2_LOG_INFO(Log,
            //     "[ip-billing] Client session for account %d closed before acquisition completed. Releasing the remote session.",
            //     uid)
            L2_LOG_INFO(Log,
                "[ip-billing] Client session for account %d closed before acquisition completed. Requesting remote session release.",
                uid);
            ReleaseSessionRequest(
                sessionId,
                user->loginIp,
                payStat);
            waitingUser->ReleaseRef();
            return 0;
        }

        const char registrationResult =
            accountdb.RegAccount(uid, user);
        if (registrationResult == 0) {
            if (AddSessionID(uid, sessionId) == 0) {
                accountdb.LogoutProcess(uid, 0);
            } else {
                client->SetUserMode(UM_LOGIN);
                client->Send(
                    "cddddddddbd",
                    3,
                    uid,
                    client->GetMd5Key(),
                    g_updateKey,
                    g_updateKey2,
                    user->stat,
                    remainingTime,
                    0,
                    user->loginflag,
                    16,
                    user->forbidden_servers,
                    user->externalAuthAdditionalInfo);
                client->SetLastIO(runtime::TickCount32());
                WriteLogD(
                    0x321,
                    user->account,
                    user->loginIp,
                    user->stat,
                    user->age,
                    user->gender,
                    0,
                    reporter.m_UserCount,
                    uid);
            }
        } else {
            // Log.AddLog(LOG_NORMAL,
            //     "component=billing event=login_rejected peer=ip_server operation=login account_id=%d phase=account_registration reason_code=%d action=release_remote_session",
            //     uid, static_cast<int>(registrationResult))
            // Log.AddLog(LOG_NORMAL,
            //     "[ip-billing] Login rejected for account %d during account registration (reason %d). Releasing the remote session.",
            //     uid, static_cast<int>(registrationResult))
            // L2_LOG_INFO(Log,
            //     "[ip-billing] Login rejected for account %d during account registration (reason %d). Releasing the remote session.",
            //     uid, static_cast<int>(registrationResult))
            L2_LOG_INFO(Log,
                "[ip-billing] Another login is already registered for account %d (reason %d). Rejecting this login and requesting remote-session release.",
                uid, static_cast<int>(registrationResult));
            ReleaseSessionRequest(
                sessionId,
                user->loginIp,
                payStat);
        }
        client->ReleaseRef();
    }

    if (waitingUser->mOrigCmd != 0) {
        // Log.AddLog(LOG_WARN,
        //     "component=billing event=reply_without_completion_handler peer=ip_server operation=acquire_session account_id=%d origin=%u action=release_request_without_registration",
        //     uid, static_cast<unsigned int>(static_cast<unsigned char>(waitingUser->mOrigCmd)))
        // Log.AddLog(LOG_WARN,
        //     "[ip-billing] Session-acquisition reply for account %d (origin %u) has no completion handler. Releasing the request without registration.",
        //     uid, static_cast<unsigned int>(static_cast<unsigned char>(waitingUser->mOrigCmd)))
        L2_LOG_WARN(Log,
            "[ip-billing] Session-acquisition reply for account %d (origin %u) has no completion handler. Releasing the request without registration.",
            uid, static_cast<unsigned int>(static_cast<unsigned char>(waitingUser->mOrigCmd)));
    }
    waitingUser->ReleaseRef();
    return result;
}

// L2Authd.exe 0x00458770, Src/ipsessiondb.cpp:996-1070.
char CIPSessionDB::AcquireSessionFail(
    int uid,
    int sessionId,
    char result) {
    IPSUser* waitingUser = nullptr;
    if (!DelUserWait(uid, &waitingUser)) {
        // Log.AddLog(LOG_WARN,
        //     "component=billing event=reply_without_pending_request peer=ip_server operation=acquire_session account_id=%d peer_reason=%u action=ignore_reply",
        //     uid, static_cast<unsigned int>(static_cast<unsigned char>(result)))
        // Log.AddLog(LOG_WARN,
        //     "[ip-billing] Ignoring session-acquisition failure for account %d (peer reason %u): no request is pending.",
        //     uid, static_cast<unsigned int>(static_cast<unsigned char>(result)))
        L2_LOG_WARN(Log,
            "[ip-billing] Ignoring session-acquisition failure for account %d (peer reason %u): no request is pending.",
            uid, static_cast<unsigned int>(static_cast<unsigned char>(result)));
        return 1;
    }

    // Log.AddLog(LOG_NORMAL,
    //     "component=billing event=session_acquisition_rejected peer=ip_server account_id=%d origin=%u peer_reason=%u action=dispatch_failure_by_origin",
    //     uid, static_cast<unsigned int>(static_cast<unsigned char>(waitingUser->mOrigCmd)), static_cast<unsigned int>(static_cast<unsigned char>(result)))
    // Log.AddLog(LOG_NORMAL,
    //     "[ip-billing] Session acquisition rejected for account %d (request origin %u, peer reason %u). Dispatching failure to the request origin.",
    //     uid, static_cast<unsigned int>(static_cast<unsigned char>(waitingUser->mOrigCmd)), static_cast<unsigned int>(static_cast<unsigned char>(result)))
    // L2_LOG_INFO(Log,
    //     "[ip-billing] Session acquisition rejected for account %d (request origin %u, peer reason %u). Dispatching failure to the request origin.",
    //     uid, static_cast<unsigned int>(static_cast<unsigned char>(waitingUser->mOrigCmd)), static_cast<unsigned int>(static_cast<unsigned char>(result)))
    L2_LOG_INFO(Log,
        "[ip-billing] Session acquisition rejected for account %d (request origin %u, peer reason %u).",
        uid, static_cast<unsigned int>(static_cast<unsigned char>(waitingUser->mOrigCmd)), static_cast<unsigned int>(static_cast<unsigned char>(result)));
    LoginUser* const user = waitingUser->pLoginUser;
    if (waitingUser->mOrigCmd == 8) {
        ChangeServerAck(
            uid,
            sessionId,
            user,
            0,
            0,
            false);
        waitingUser->ReleaseRef();
    } else if (waitingUser->mOrigCmd == 0x1c) {
        const WorldServer world =
            g_ServerList.GetAt(user->serverid);
        const int forwarded = SendSocket(
            world.inner_addr,
            "cdc",
            0x15,
            uid,
            0x12);
        if (forwarded == 0) {
            L2_LOG_WARN(Log,
                "[ip-billing] Could not forward session-acquisition rejection for account %d to world server %d: no connection exists. Releasing the pending request.",
                uid, static_cast<int>(user->serverid));
        }
        user->ReleaseRef();
        waitingUser->ReleaseRef();
    } else if (waitingUser->mOrigCmd == 2) {
        user->ReleaseRef();
        waitingUser->ReleaseRef();
    } else {
        CClientSession* const client =
            serverEx.FindSession(user->sid);
        if (client == nullptr) {
            waitingUser->ReleaseRef();
        } else if (result == 2) {
            client->SendFail(8, 0x15, user);
            waitingUser->ReleaseRef();
            client->ReleaseRef();
        } else if (!config.ignoreWhenIPInCharge) {
            user->ipServer.valueType = 0;
            accountdb.CheckPersonalPayStat(
                client,
                user,
                uid);
            client->ReleaseRef();
            waitingUser->ReleaseRef();
        } else {
            client->SendFail(8, 0x22, user);
            waitingUser->ReleaseRef();
            client->ReleaseRef();
        }
    }
    return result;
}

// L2Authd.exe 0x00458A00, Src/ipsessiondb.cpp:1073-1163.
void CIPSessionDB::ChangeServerAck(
    int uid,
    int sessionId,
    LoginUser* user,
    int remainingTime,
    int payStat,
    bool acquired) {
    CClientSession* const client =
        serverEx.FindSession(user->sid);
    if (client == nullptr) {
        // Log.AddLog(LOG_NORMAL,
        //     "component=billing event=reply_context_missing peer=ip_server operation=change_server account_id=%d reason=client_session_missing acquired=%u action=%s",
        //     uid, static_cast<unsigned int>(acquired),
        //     config.ignoreWhenIPInCharge && user != nullptr && user->um_mode == UM_CHANGE_SERVER
        //         ? "release_remote_session" : "return_without_session_release")
        // Log.AddLog(LOG_NORMAL,
        //     "[ip-billing] Server-change reply for account %d has no client session (remote session acquired: %u). %s",
        //     uid, static_cast<unsigned int>(acquired),
        //     config.ignoreWhenIPInCharge && user != nullptr && user->um_mode == UM_CHANGE_SERVER
        //         ? "Releasing the remote session." : "Returning without releasing the remote session.")
        // L2_LOG_INFO(Log,
        //     "[ip-billing] Server-change reply for account %d has no client session (remote session acquired: %u). %s",
        //     uid, static_cast<unsigned int>(acquired),
        //     config.ignoreWhenIPInCharge && user != nullptr && user->um_mode == UM_CHANGE_SERVER
        //         ? "Releasing the remote session." : "Returning without releasing the remote session.")
        L2_LOG_INFO(Log,
            "[ip-billing] Server-change reply for account %d has no client session (remote session acquired: %u). %s",
            uid, static_cast<unsigned int>(acquired),
            config.ignoreWhenIPInCharge && user != nullptr && user->um_mode == UM_CHANGE_SERVER
                ? "Requesting remote session release." : "Returning without requesting remote session release.");
        const std::time_t now = std::time(nullptr);
        // const std::tm local = *std::localtime(&now)
        const std::tm local = runtime::crt::LocalTimeOrZero(now);
        if (config.ignoreWhenIPInCharge &&
            user != nullptr) {
            fileLog.AddLog(
                LOG_NORMAL,
                "%d-%02d-%02d %02d:%02d:%02d, "
                "GameClient first down before entering "
                "World's movement.(%d,%d,%d)\r\n",
                local.tm_year + 1900,
                local.tm_mon + 1,
                local.tm_mday,
                local.tm_hour,
                local.tm_min,
                local.tm_sec,
                uid,
                user->loginIp.s_addr,
                user->um_mode);
        } else {
            fileLog.AddLog(
                LOG_NORMAL,
                "%d-%02d-%02d %02d:%02d:%02d, "
                "GameClient first down before entering "
                "World's movement.(%d)\r\n",
                local.tm_year + 1900,
                local.tm_mon + 1,
                local.tm_mday,
                local.tm_hour,
                local.tm_min,
                local.tm_sec,
                uid);
        }
        if (config.ignoreWhenIPInCharge &&
            user != nullptr &&
            user->um_mode == UM_CHANGE_SERVER) {
            ReleaseSessionRequest(
                sessionId,
                user->loginIp,
                payStat);
        }
        return;
    }

    if (!accountdb.UpdateChargeServerData(
            uid,
            1,
            static_cast<char>(payStat),
            remainingTime,
            0,
            0,
            0)) {
        // Log.AddLog(LOG_WARN,
        //     "component=billing event=charge_update_failed peer=ip_server operation=change_server account_id=%d reason=account_not_found client_reason=10 action=reject_and_logout",
        //     uid)
        // Log.AddLog(LOG_WARN,
        //     "[ip-billing] Could not update charging while changing servers for account %d: account not found. Sending failure code 10 and logging out.",
        //     uid)
        L2_LOG_WARN(Log,
            "[ip-billing] Could not update charging while changing servers for account %d: account not found. Sending failure code 10 and logging out.",
            uid);
        client->Send("cddc", 12, uid, 0, 10);
        accountdb.LogoutProcess(uid, 0);
        client->ReleaseRef();
        return;
    }

    if (acquired) {
        if (AddSessionID(uid, sessionId) == 0) {
            client->Send("cddc", 12, uid, 0, 0x14);
            accountdb.LogoutProcess(uid, 0);
            client->ReleaseRef();
            return;
        }
    } else if (config.ignoreWhenIPInCharge) {
        // Log.AddLog(LOG_NORMAL,
        //     "component=billing event=transfer_rejected peer=ip_server account_id=%d reason=session_not_acquired client_reason=18 action=reject_and_logout",
        //     uid)
        // Log.AddLog(LOG_NORMAL,
        //     "[ip-billing] Server transfer rejected for account %d: no IP session was acquired. Sending failure code 18 and logging out.",
        //     uid)
        L2_LOG_INFO(Log,
            "[ip-billing] Server transfer rejected for account %d: no IP session was acquired. Sending failure code 18 and logging out.",
            uid);
        client->Send("cddc", 12, uid, 0, 0x12);
        accountdb.LogoutProcess(uid, 0);
        client->ReleaseRef();
        return;
    }

    // The selected function uses this return value for UpdateChargeData but
    // does not assign it back to LoginUser::chargeServerId.
    const char chargeServerId =
        accountdb.DetermineChargeServer(user);
    if (user->chargeServerId == 0 &&
        user->stat == 0 &&
        !config.useDualCharging) {
        // Log.AddLog(LOG_NORMAL,
        //     "component=billing event=transfer_rejected peer=ip_server account_id=%d reason=no_charge_source client_reason=18 action=send_client_failure",
        //     uid)
        // Log.AddLog(LOG_NORMAL,
        //     "[ip-billing] Server transfer rejected for account %d: no charge source is available. Sending client failure code 18.",
        //     uid)
        L2_LOG_INFO(Log,
            "[ip-billing] Server transfer rejected for account %d: no charge source is available. Sending client failure code 18.",
            uid);
        client->Send("cddc", 12, uid, 0, 0x12);
    } else if (!accountdb.UpdateChargeData(
                   uid,
                   chargeServerId,
                   payStat,
                   UM_LOGIN)) {
        // Log.AddLog(LOG_WARN,
        //     "component=billing event=charge_update_failed peer=ip_server operation=change_server account_id=%d reason=account_not_found client_reason=10 action=send_client_failure",
        //     uid)
        // Log.AddLog(LOG_WARN,
        //     "[ip-billing] Could not update charging while changing servers for account %d: account not found. Sending client failure code 10.",
        //     uid)
        L2_LOG_WARN(Log,
            "[ip-billing] Could not update charging while changing servers for account %d: account not found. Sending client failure code 10.",
            uid);
        client->Send("cddc", 12, uid, 0, 10);
    } else {
        client->Send(
            "cddc",
            12,
            uid,
            client->GetMd5Key(),
            0);
    }

    if (user->chargeServerId != 1 &&
        DelSessionID(uid) != 0) {
        ReleaseSessionRequest(
            sessionId,
            user->loginIp,
            payStat);
    }
    client->SetUserMode(UM_LOGIN);
    client->ReleaseRef();
}

// L2Authd.exe 0x00458D30, Src/ipsessiondb.cpp:460-604.
char CIPSessionDB::CheckServerSideLogin(
    int uid,
    IPSUser* waitingUser) {
    LoginUser* const user = waitingUser->pLoginUser;
    if (config.usePBServer) {
        return PBServerMgr::Inst()->RequestGetValueInfo(
            uid,
            user,
            nullptr,
            0x1c,
            user->originChargedServerId);
    }

    const char chargeServerId =
        accountdb.DetermineChargeServer(user);
    if (chargeServerId == 1) {
        if (user->stat ==
            static_cast<int>(user->ipServer.valueType)) {
            const char requestResult =
                RequestIPCharge(uid, user, 0x1c);
            if (requestResult == 0) {
                return 0;
            }
            return requestResult == 11 ? 0x15 : 0x14;
        }
        return !config.useDualCharging ? 0x12 : 0x2c;
    }

    int remainingTime = 0;
    user->logintime = runtime::crt::Time32Now();
    char result = accountdb.ChargePersonalDBValue(
        uid,
        user,
        nullptr,
        &remainingTime);
    if (result != 0) {
        return result;
    }
    if (!accountdb.recordGamePlayTime(
            uid,
            user->serverid)) {
        return 0x14;
    }

    int fatigueRestTime = 0;
    int fatiguePlayTime = 0;
    int fatigueSecondsSinceLastLogout = 0;
    if (config.useFatigueSystem &&
        AccountContainer::Inst()->IsFatigueCustomer(
            user->fcid,
            user->age,
            user->auth_flag)) {
        AccountContainer::Inst()->GetAccountTime(
            user->fcid,
            &fatigueRestTime,
            &fatiguePlayTime);
        AccountContainer::Inst()->GetLastLogoutTime(
            user->fcid,
            &fatigueSecondsSinceLastLogout);
    }

    char zoneFatigueInfo[1556]{};
    const int zoneFatigueInfoLength = 0;
    [[maybe_unused]] const unsigned char
        zoneFatigueAvailableCount = 0;
    const WorldServer world =
        g_ServerList.GetAt(user->serverid);
    if (config.useExtendPacket) {
        if (config.useGradeServer) {
            user->user_level = 0;
        }
        SendSocket(
            world.inner_addr,
            "cdcddddddchbbhd",
            0x15,
            uid,
            0,
            remainingTime,
            user->loginflag,
            0,
            fatigueRestTime,
            fatiguePlayTime,
            fatigueSecondsSinceLastLogout,
            user->packageVersionInfo,
            zoneFatigueInfoLength,
            zoneFatigueInfoLength,
            zoneFatigueInfo,
            24,
            user->byteChildPlayTimeInfo,
            user->user_level,
            0);
    } else {
        SendSocket(
            world.inner_addr,
            "cdcddddddchbd",
            0x15,
            uid,
            0,
            remainingTime,
            user->loginflag,
            0,
            fatigueRestTime,
            fatiguePlayTime,
            fatigueSecondsSinceLastLogout,
            user->packageVersionInfo,
            zoneFatigueInfoLength,
            zoneFatigueInfoLength,
            zoneFatigueInfo,
            0);
    }
    return 0;
}

// L2Authd.exe 0x00459180, Src/ipsessiondb.cpp:270-284.
int CIPSessionDB::FindSessionID(int uid) {
    int sessionId = 0;
    {
        L2_GUARD(IPSessionLock);
        const auto found = IPSessionMap.find(uid);
        if (found != IPSessionMap.end()) {
            sessionId = found->second;
        }
    }
    return sessionId;
}

// L2Authd.exe 0x004591F0, Src/ipsessiondb.cpp:287-303.
int CIPSessionDB::DelSessionID(int uid) {
    int sessionId = 0;
    {
        L2_GUARD(IPSessionLock);
        const auto found = IPSessionMap.find(uid);
        if (found != IPSessionMap.end()) {
            sessionId = found->second;
            IPSessionMap.erase(found);
        }
    }
    return sessionId;
}

// L2Authd.exe 0x00459280, Src/ipsessiondb.cpp:325-346.
int CIPSessionDB::AddSessionID(
    int uid,
    int sessionId) {
    if (sessionId == 0) {
        // Log.AddLog(LOG_WARN,
        //     "component=billing event=session_registration_failed peer=ip_server account_id=%d reason=empty_session_id action=return_zero",
        //     uid)
        // Log.AddLog(LOG_WARN,
        //     "[ip-billing] Could not register account %d: the IP session ID is empty. Returning zero.",
        //     uid)
        // L2_LOG_WARN(Log,
        //     "[ip-billing] Could not register account %d: the IP session ID is empty. Returning zero.",
        //     uid)
        L2_LOG_WARN(Log,
            "[ip-billing] Could not register an IP session for account %d: the supplied session ID is zero. Session registration is unchanged.",
            uid);
        return 0;
    }
    bool inserted = false;
    {
        L2_GUARD(IPSessionLock);
        inserted =
            IPSessionMap.insert({uid, sessionId}).second;
    }
    if (!inserted) {
        // Log.AddLog(LOG_WARN,
        //     "component=billing event=session_registration_failed peer=ip_server account_id=%d reason=account_already_registered action=return_zero",
        //     uid)
        // Log.AddLog(LOG_WARN,
        //     "[ip-billing] Could not register account %d: it already has an IP session. Returning zero.",
        //     uid)
        // L2_LOG_WARN(Log,
        //     "[ip-billing] Could not register account %d: it already has an IP session. Returning zero.",
        //     uid)
        L2_LOG_WARN(Log,
            "[ip-billing] Could not register a new IP session for account %d: an IP session is already registered. Keeping the existing registration.",
            uid);
    }
    return inserted ? sessionId : 0;
}

// L2Authd.exe 0x004592F0, Src/ipsessiondb.cpp:306-321.
bool CIPSessionDB::DellAllWaitingSessionID() {
    {
        L2_GUARD(mWaitingUserLock);
        for (auto iterator = mWaitingUser.begin();
             iterator != mWaitingUser.end();) {
            IPSUser* const user = iterator->second;
            ::operator delete(user);
            iterator = mWaitingUser.erase(iterator);
        }
    }
    return true;
}

// L2Authd.exe 0x00459390, Src/ipsessiondb.cpp:1780-1834.
bool CIPSessionDB::KickDuplicatedUserWaitAccount(
    IPSUser* waitingUser) {
    LoginUser* const user = waitingUser->pLoginUser;
    if (user == nullptr) {
        waitingUser->ReleaseRef();
        return false;
    }

    if (waitingUser->mSendingCmd == AI_IP_GET_KIND ||
        waitingUser->mSendingCmd == AI_IP_ACQUIRE) {
        if (waitingUser->mOrigCmd == 0) {
            Send2ClientSession(
                user->sid,
                "cc",
                8,
                7);
        } else if (waitingUser->mOrigCmd == 0x1c) {
            const WorldServer world =
                g_ServerList.GetAt(user->serverid);
            SendSocket(
                world.inner_addr,
                "cdc",
                0x15,
                user->uid,
                7);
        }
    }
    waitingUser->ReleaseRef();
    return true;
}

// L2Authd.exe 0x00459590, Src/ipsessiondb.cpp:196-198.
CIPSessionDB::CIPSessionDB()
    : mWaitingUserLock(eCriticalSection, 0),
      IPSessionLock(eCriticalSection, 0),
      waitUserCheckTime(
          runtime::crt::Time32Now()) {}

// L2Authd.exe 0x00459630, Src/ipsessiondb.cpp:201-203.
CIPSessionDB::~CIPSessionDB() = default;

// L2Authd.exe 0x004596B0, Src/ipsessiondb.cpp:1464-1476.
CIPSocket::CIPSocket(std::uint32_t socket)
    : CIOSocket(socket),
      opFlag(0),
      ConnectSessionKey(0),
      mode(SM_READ_LEN),
      packetTable(IPPacketFuncTable),
      addr(config.IPServer),
      host(nullptr) {
    AddRef();
    Destination.sin_family = AF_INET;
    Destination.sin_addr = config.IPServer;
    Destination.sin_port = htons(
        static_cast<std::uint16_t>(config.IPPort));
    IPServerReconnect = false;
}

// L2Authd.exe 0x00459790, Src/ipsessiondb.cpp:1479-1483.
CIPSocket::~CIPSocket() {
    // Noise: Socket destruction adds no outcome beyond the connection-close event.
    // Log.AddLog(LOG_ERROR, "IPSocket Deleted")
}

// L2Authd.exe 0x00459800, Src/ipsessiondb.cpp:1458-1460.
CIPSocket* CIPSocket::Allocate(std::uint32_t socket) {
    return new CIPSocket(socket);
}

// L2Authd.exe 0x00459880, Src/ipsessiondb.cpp:1562-1571.
void CIPSocket::OnCreate() {
    AddRef();
    const std::uint32_t peerAddress = ntohl(addr.s_addr);
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "*new IPServer connection from %s, %x(%x)",
    //     ::inet_ntoa(config.IPServer),
    //     m_hSocket,
    //     runtime::PointerLow32(this))
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "component=network event=connected peer=ip_server address=%u.%u.%u.%u port=%u socket=%u",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(ntohs(Destination.sin_port)), m_hSocket)
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "[ip-service] Connected to %u.%u.%u.%u:%u on socket %u.",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(ntohs(Destination.sin_port)), m_hSocket)
    L2_LOG_INFO(Log,
        "[ip-service] Connected to %u.%u.%u.%u:%u on socket %u.",
        (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        (peerAddress >> 8U) & 255U, peerAddress & 255U,
        static_cast<unsigned int>(ntohs(Destination.sin_port)), m_hSocket);
    // Send(
    //     "cdc",
    //     AI_SERVER_VERSION,
    //     GetBuildNumber(),
    //     config.gameId)
    const bool sent = Send(
        "cdc",
        AI_SERVER_VERSION,
        GetBuildNumber(),
        config.gameId);
    if (!sent) {
        // Log.AddLog(LOG_WARN,
        //     "component=network event=handshake_not_submitted peer=ip_server socket=%u reason=socket_send_rejected action=continue_reading",
        //     m_hSocket)
        // Log.AddLog(LOG_WARN,
        //     "[ip-service] Could not send the handshake on socket %u. Reading continues.",
        //     m_hSocket)
        L2_LOG_WARN(Log,
            "[ip-service] Could not send the handshake on socket %u. Reading continues.",
            m_hSocket);
    }
    OnRead();
}

// L2Authd.exe 0x004598F0, Src/ipsessiondb.cpp:1574-1633.
void CIPSocket::OnRead() {
    int consumed = 0;
    const int available =
        static_cast<int>(m_pReadBuf->m_size);
    auto* const input =
        reinterpret_cast<unsigned char*>(
            m_pReadBuf->m_buffer);

    if (mode == SM_CLOSE) {
        CloseSocket();
        return;
    }

    while (consumed < available) {
        if (mode == SM_READ_LEN) {
            if (available < consumed + 3) {
                Read(static_cast<std::uint32_t>(
                    available - consumed));
                return;
            }
            packetLen =
                static_cast<int>(input[consumed]) + 1 +
                static_cast<int>(input[consumed + 1]) * 256;
            if (packetLen < 1 || packetLen > 0x2000) {
                // Log.AddLog(
                //     LOG_ERROR,
                //     "%d: bad packet size %d",
                //     m_hSocket,
                //     packetLen)
                // Log.AddLog(
                //     LOG_WARN,
                //     "component=network event=invalid_packet_length peer=ip_server socket=%u payload_bytes=%d min_bytes=1 max_bytes=8192 action=close_connection",
                //     m_hSocket, packetLen)
                // Log.AddLog(
                //     LOG_WARN,
                //     "[ip-service] Invalid packet on socket %u: body length %d is outside 1..8192 bytes. Closing the connection.",
                //     m_hSocket, packetLen)
                L2_LOG_WARN(Log,
                    "[ip-service] Invalid packet on socket %u: body length %d is outside 1..8192 bytes. Closing the connection.",
                    m_hSocket, packetLen);
                CloseSocket();
                return;
            }
            consumed += 2;
            mode = SM_READ_BODY;
            continue;
        }

        if (mode != SM_READ_BODY) {
            if (mode != SM_CLOSE) {
                L2_LOG_ERROR(Log,
                    "[ip-service] Invalid read state %d on socket %u. Closing the connection.",
                    static_cast<int>(mode), m_hSocket);
            }
            CloseSocket();
            return;
        }
        if (available < consumed + packetLen) {
            Read(static_cast<std::uint32_t>(
                available - consumed));
            return;
        }

        if ((config.usePacketLog & 1U) != 0U) {
            WritePacketLog(
                "IPServer->Auth",
                m_hSocket,
                input + consumed,
                packetLen,
                0x400);
        }
        const unsigned int opcode = input[consumed];
        if (opcode >= static_cast<unsigned int>(IA_MAX)) {
            // Log.AddLog(
            //     LOG_ERROR,
            //     "unknown protocol %d",
            //     static_cast<int>(opcode))
            // Log.AddLog(
            //     LOG_WARN,
            //     "component=network event=invalid_opcode peer=ip_server socket=%u opcode=%u max_opcode=16 action=close_connection",
            //     m_hSocket, opcode)
            // Log.AddLog(
            //     LOG_WARN,
            //     "[ip-service] Invalid opcode on socket %u: received %u, maximum 16. Closing the connection.",
            //     m_hSocket, opcode)
            L2_LOG_WARN(Log,
                "[ip-service] Invalid opcode on socket %u: received %u, maximum 16. Closing the connection.",
                m_hSocket, opcode);
            CloseSocket();
            return;
        }

        CIPPacketServer* const packet =
            CIPPacketServer::Alloc();
        packet->m_pSocket = this;
        packet->m_pBuf = m_pReadBuf;
        packet->m_pFunc =
            reinterpret_cast<IPPacketFunc>(
                packetTable[opcode]);
        AddRef();
        m_pReadBuf->AddRef();
        runtime::raw_atomic::IncrementI32(
            &CIPPacketServer::g_nPendingPacket);
        // packet->PostObject(
        //     consumed,
        //     g_hIOCompletionPort)
        const auto posted = packet->PostObject(
            consumed,
            g_hIOCompletionPort);
        if (!posted) {
            // Log.AddLog(LOG_ERROR,
            //     "component=network event=packet_dispatch_failed peer=ip_server socket=%u action=continue_reading references=retained",
            //     m_hSocket)
            // Log.AddLog(LOG_ERROR,
            //     "[ip-service] Packet dispatch threw on socket %u. Reading continues. Acquired references are retained.",
            //     m_hSocket)
            // L2_LOG_ERROR(Log,
            //     "[ip-service] Packet dispatch threw on socket %u. Reading continues. Acquired references are retained.",
            //     m_hSocket)
            L2_LOG_ERROR(Log,
                "[ip-service] Could not queue opcode %u from socket %u for processing. Reading continues; acquired references are retained.",
                opcode, m_hSocket);
        }
        consumed += packetLen;
        mode = SM_READ_LEN;
    }
    Read(0);
}

// L2Authd.exe 0x00459B20, Src/ipsessiondb.cpp:1486-1506.
void CIPSocket::OnClose() {
    mode = SM_CLOSE;
    IPServerReconnect = true;
    config.UseIPServer = false;
    const std::uint32_t peerAddress = ntohl(addr.s_addr);
    // Log.AddLog(
    //     LOG_ERROR,
    //     "*close connection IPServer from %s, %x(%x)",
    //     IP(),
    //     m_hSocket,
    //     runtime::PointerLow32(this))
    // Log.AddLog(
    //     LOG_WARN,
    //     "component=network event=socket_closed peer=ip_server address=%u.%u.%u.%u port=%u socket=%u reason=unspecified billing=disabled action=discard_pending_requests reconnect=schedule_pending delay_ms=%u",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(ntohs(Destination.sin_port)), m_hSocket,
    //     static_cast<std::uint32_t>(config.IPConnectInterval))
    // Log.AddLog(
    //     LOG_WARN,
    //     "[ip-service] Disconnected from %u.%u.%u.%u:%u on socket %u. Cause not provided. Disabling IP billing and discarding pending requests. Requesting reconnect after %u ms.",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(ntohs(Destination.sin_port)), m_hSocket,
    //     static_cast<std::uint32_t>(config.IPConnectInterval))
    L2_LOG_WARN(Log,
        // Noise: The generic absence-of-cause phrase adds no diagnostic context.
        // "[ip-service] Disconnected from %u.%u.%u.%u:%u on socket %u. Cause not provided. Disabling IP billing and discarding pending requests. Requesting reconnect after %u ms.",
        "[ip-service] Disconnected from %u.%u.%u.%u:%u on socket %u. Disabling IP billing and discarding pending requests. Requesting reconnect after %u ms.",
        (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        (peerAddress >> 8U) & 255U, peerAddress & 255U,
        static_cast<unsigned int>(ntohs(Destination.sin_port)), m_hSocket,
        static_cast<std::uint32_t>(config.IPConnectInterval));
    ipsessionDB.DellAllWaitingSessionID();
    AddRef();
    // runtime::timer_queue::CreateTimer(
    //     &g_hIPServerTimer,
    //     nullptr,
    //     IPSocketTimerRoutine,
    //     this,
    //     static_cast<std::uint32_t>(
    //         config.IPConnectInterval),
    //     0)
    const bool created = runtime::timer_queue::CreateTimer(
        &g_hIPServerTimer,
        nullptr,
        IPSocketTimerRoutine,
        this,
        static_cast<std::uint32_t>(
            config.IPConnectInterval),
        0);
    if (!created) {
        // Log.AddLog(LOG_ERROR,
        //     "component=network event=reconnect_schedule_failed peer=ip_server socket=%u delay_ms=%u action=return_without_new_retry references=retained",
        //     m_hSocket, static_cast<std::uint32_t>(config.IPConnectInterval))
        // Log.AddLog(LOG_ERROR,
        //     "[ip-service] Could not schedule reconnect for socket %u after %u ms. No new retry is scheduled. The socket reference is retained.",
        //     m_hSocket, static_cast<std::uint32_t>(config.IPConnectInterval))
        // L2_LOG_ERROR(Log,
        //     "[ip-service] Could not schedule reconnect for socket %u after %u ms. No new retry is scheduled. The socket reference is retained.",
        //     m_hSocket, static_cast<std::uint32_t>(config.IPConnectInterval))
        L2_LOG_ERROR(Log,
            "[ip-service] Could not schedule reconnect to %u.%u.%u.%u:%u for socket %u after %u ms. No new retry is scheduled; the socket reference is retained.",
            (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            (peerAddress >> 8U) & 255U, peerAddress & 255U,
            static_cast<unsigned int>(ntohs(Destination.sin_port)), m_hSocket, static_cast<std::uint32_t>(config.IPConnectInterval));
    }
}

// L2Authd.exe 0x00459BA0, Src/ipsessiondb.cpp:1509-1554.
void CIPSocket::OnTimerCallback() {}

// L2Authd.exe 0x00459BB0, Src/ipsessiondb.cpp:1637-1665.
bool CIPSocket::Send(const char* format, ...) {
    AddRef();
    if (mode == SM_CLOSE ||
        IPServerReconnect ||
        !config.UseIPServer) {
        ReleaseRef();
        return false;
    }

    CIOBuffer* const buffer = CIOBuffer::Alloc();
    char* const packet = buffer->m_buffer;
    std::va_list arguments;
    va_start(arguments, format);
    const int packetSize = Assemble(
        packet + 2,
        0x1ffe,
        format,
        arguments);
    va_end(arguments);

    if (packetSize == 0) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "%d: assemble too large packet. format %s",
        //     m_hSocket,
        //     format)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=network event=packet_assembly_failed peer=ip_server socket=%u capacity_bytes=8190 action=submit_legacy_three_byte_buffer header=uninitialized legacy_return=success",
        //     m_hSocket)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[ip-service] Could not assemble a packet on socket %u within 8190 bytes. The existing path still submits a three-byte buffer with an uninitialized header and returns success.",
        //     m_hSocket)
        L2_LOG_ERROR(Log,
            "[ip-service] Could not assemble a packet on socket %u within 8190 bytes. The existing path still submits a three-byte buffer with an uninitialized header and returns success.",
            m_hSocket);
    } else {
        packet[0] = static_cast<char>(packetSize);
        packet[1] = static_cast<char>(packetSize >> 8);
        if ((config.usePacketLog & 1U) != 0U) {
            WritePacketLog(
                "Auth->IPServer",
                m_hSocket,
                reinterpret_cast<const unsigned char*>(
                    packet + 2),
                packetSize,
                0x400);
        }
    }

    // The selected image writes one extra byte beyond its two-byte length
    // and assembled payload. This target framing quirk is intentional.
    buffer->m_size = static_cast<std::uint32_t>(
        packetSize + 3);
    Write(buffer);
    ReleaseRef();
    return true;
}

// L2Authd.exe 0x00459CE0, Src/ipsessiondb.cpp:1557-1559.
const char* CIPSocket::IP() {
    return ::inet_ntoa(addr);
}

CIPPacketServer::CIPPacketServer() = default;
CIPPacketServer::~CIPPacketServer() = default;

// L2Authd.exe 0x00459D00, Src/ipsessiondb.cpp:135-151.
CIPPacketServer* CIPPacketServer::Alloc() {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            runtime::raw_atomic::IncrementI32(
                &CIPPacketServerPool::g_nAlloc)) &
        0x0fU;
    CIPPacketServerPool::CSlot& slot =
        CIPPacketServerPool::g_slot[slotIndex];
    CIPPacketServer* packet = nullptr;
    {
        L2_GUARD(slot.m_lock);
        packet = slot.m_pPacket;
        if (packet != nullptr) {
            slot.m_pPacket =
                reinterpret_cast<CIPPacketServer*>(
                    packet->m_pSocket);
        }
    }
    if (packet == nullptr) {
        packet = new CIPPacketServer;
    }
    return packet;
}

// L2Authd.exe 0x00459DD0, Src/ipsessiondb.cpp:154-165.
void CIPPacketServer::FreeAll() {
    for (CIPPacketServerPool::CSlot& slot :
         CIPPacketServerPool::g_slot) {
        {
            L2_GUARD(slot.m_lock);
            while (slot.m_pPacket != nullptr) {
                CIPPacketServer* const packet =
                    slot.m_pPacket;
                slot.m_pPacket =
                    reinterpret_cast<CIPPacketServer*>(
                        packet->m_pSocket);
                delete packet;
            }
        }
    }
}

// L2Authd.exe 0x00459E60, Src/ipsessiondb.cpp:168-175.
void CIPPacketServer::Free() {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            runtime::raw_atomic::DecrementI32(
                &CIPPacketServerPool::g_nFree)) &
        0x0fU;
    CIPPacketServerPool::CSlot& slot =
        CIPPacketServerPool::g_slot[slotIndex];
    {
        L2_GUARD(slot.m_lock);
        m_pSocket = reinterpret_cast<CIOSocket*>(
            slot.m_pPacket);
        slot.m_pPacket = this;
    }
}

// L2Authd.exe 0x00459EB0, Src/ipsessiondb.cpp:178-192.
void CIPPacketServer::OnIOCallback(
    [[maybe_unused]] int bSuccess,
    std::uint32_t dwTransferred,
    [[maybe_unused]] _OVERLAPPED* lpOverlapped) {
    try {
        const bool closeSocket = m_pFunc(
            m_pSocket,
            reinterpret_cast<const unsigned char*>(
                m_pBuf->m_buffer + dwTransferred + 1));
        if (closeSocket) {
            m_pSocket->CloseSocket();
        }
        m_pSocket->ReleaseRef();
        m_pBuf->Release();
        runtime::raw_atomic::DecrementI32(
            &g_nPendingPacket);
        Free();
    } catch (...) {
        // Original: ELOG("*%s:%d(%s) exception\n", "IPSessionDB.cpp", 190,
        //     "Wed Oct 23 17:02:11 2019")
        ELOG("[IPSessionDB.cpp] Exception while dispatching or releasing an IP-service packet job (buffer offset %u). Entering exception handling.\n",
            dwTransferred);
        send_exception(true);
    }
}

// L2Authd.exe 0x00459FB0, Src/ipsessiondb.cpp:72-110.
#if defined(_WIN32)
void __stdcall IPSocketTimerRoutine(
#else
void IPSocketTimerRoutine(
#endif
    [[maybe_unused]] void* parameter,
    [[maybe_unused]] std::uint8_t timerOrWaitFired) {
    // Noise: A timer invocation is not a reconnect result. Actual failures are reported below.
    // Log.AddLog(LOG_DEBUG, "IPSocketTimerRoutine")
    if (g_hIPServerTimer != nullptr) {
        runtime::timer_queue::DeleteTimer(
            nullptr,
            g_hIPServerTimer);
    }
    g_hIPServerTimer = nullptr;
    if (!IPServerReconnect) {
        return;
    }

    const std::uint32_t peerAddress = ntohl(config.IPServer.s_addr);
    std::uint32_t nativeSocket = UINT32_MAX;
    const bool connected =
        authd::platform::ConnectNativeSocket(
            nativeSocket,
            config.IPServer,
            static_cast<std::uint16_t>(
                config.IPPort));
    if (!connected) {
        // Log.AddLog(LOG_WARN,
        //     "component=network event=reconnect_failed peer=ip_server address=%u.%u.%u.%u port=%u socket=%u error_domain=winsock_compatible code=%d action=close_socket reconnect=schedule_pending",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.IPPort)), nativeSocket,
        //     ::portable::GetNativeSocketLastError())
        // Log.AddLog(LOG_WARN,
        //     "[ip-service] Could not reconnect to %u.%u.%u.%u:%u on socket %u (WinSock error %d). Closing the socket and requesting another retry.",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.IPPort)), nativeSocket,
        //     ::portable::GetNativeSocketLastError())
        // L2_LOG_WARN(Log,
        //     "[ip-service] Could not reconnect to %u.%u.%u.%u:%u on socket %u (WinSock error %d). Closing the socket and requesting another retry.",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.IPPort)), nativeSocket,
        //     ::portable::GetNativeSocketLastError())
        L2_LOG_WARN(Log,
            "[ip-service] Could not reconnect to %u.%u.%u.%u:%u on socket %u (WinSock error %d). %s",
            (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            (peerAddress >> 8U) & 255U, peerAddress & 255U,
            static_cast<unsigned int>(static_cast<std::uint16_t>(config.IPPort)), nativeSocket,
            ::portable::GetNativeSocketLastError(),
            nativeSocket == UINT32_MAX
                ? "No socket was created; this path does not schedule another retry."
                : "Closing the socket; its close callback will request another retry.");
    }
    CIPSocket* const replacement =
        CIPSocket::Allocate(nativeSocket);
    replacement->SetAddress(config.IPServer);
    if (!connected) {
        replacement->CloseSocket();
        replacement->ReleaseRef();
        return;
    }

    CIPSocket* oldSocket = nullptr;
    {
        L2_WRITE_GUARD(gIPLock);
        oldSocket = pIPSocket;
        pIPSocket = replacement;
        IPServerReconnect = false;
        config.UseIPServer = true;
        authd::platform::InitializeNativeSocket(
            replacement,
            nativeSocket,
            g_hIOCompletionPort);
    }
    oldSocket->ReleaseRef();
}

// L2Authd.exe 0x0045A0E0, Src/ipsessiondb.cpp:1166-1169.
static bool DummyPacket(
    [[maybe_unused]] CIPSocket* socket,
    [[maybe_unused]] const unsigned char* packet) {
    // Log.AddLog(
    //     LOG_WARN,
    //     "Call DummyPacket What What What")
    // Log.AddLog(
    //     LOG_WARN,
    //     "component=network event=unsupported_opcode peer=ip_server socket=%u action=ignore_packet connection=kept",
    //     socket->m_hSocket)
    // Log.AddLog(
    //     LOG_WARN,
    //     "[ip-service] Ignoring an unsupported packet on socket %u. The connection remains open.",
    //     socket->m_hSocket)
    L2_LOG_WARN(Log,
        "[ip-service] Ignoring an unsupported packet on socket %u. The connection remains open.",
        socket->m_hSocket);
    return false;
}

// L2Authd.exe 0x0045A100, Src/ipsessiondb.cpp:1171-1196.
static bool StartIPChargeFail(
    [[maybe_unused]] CIPSocket* socket,
    const unsigned char* packet) {
    try {
        const int uid = GetIntFromPacket(&packet);
        [[maybe_unused]] const char serverId =
            GetCharFromPacket(&packet);
        [[maybe_unused]] const int remainingTime =
            GetIntFromPacket(&packet);
        [[maybe_unused]] const int payStat =
            GetIntFromPacket(&packet);
        [[maybe_unused]] const in_addr address =
            GetAddrFromPacket(&packet);
        char account[15]{};
        GetStrFromPacket(&packet, 15, account);
        account[14] = '\0';
        if (uid != 0) {
            // Log.AddLog(LOG_NORMAL,
            //     "component=billing event=charge_start_rejected peer=ip_server account_id=%d action=logout",
            //     uid)
            // Log.AddLog(LOG_NORMAL,
            //     "[ip-billing] Charge-start rejected for account %d. Logging out.",
            //     uid)
            L2_LOG_INFO(Log,
                "[ip-billing] Charge-start rejected for account %d. Logging out.",
                uid);
            accountdb.LogoutProcess(uid, 0);
        }
    } catch (...) {
        // Original: ELOG("*%s:%d(%s) exception\n", "IPSessionDB.cpp", 1194,
        //     "Wed Oct 23 17:02:11 2019")
        ELOG("[IPSessionDB.cpp] Exception while processing an IP charge-start rejection. Entering exception handling.\n");
        send_exception(true);
    }
    return false;
}

// L2Authd.exe 0x0045A270, Src/ipsessiondb.cpp:1201-1234.
static bool StartIPCharge(
    [[maybe_unused]] CIPSocket* socket,
    const unsigned char* packet) {
    try {
        const unsigned int uid =
            static_cast<unsigned int>(
                GetIntFromPacket(&packet));
        const char serverId = GetCharFromPacket(&packet);
        const int remainingTime = GetIntFromPacket(&packet);
        const int payStat = GetIntFromPacket(&packet);
        const unsigned int address =
            static_cast<unsigned int>(
                GetIntFromPacket(&packet));

        LoginUser* user = nullptr;
        if (accountdb.FindAccount(
                static_cast<int>(uid),
                &user)) {
            CClientSession* const client =
                serverEx.FindSession(user->sid);
            if (client != nullptr) {
                const char result = accountdb.AboutToPlay(
                    client,
                    static_cast<int>(uid),
                    user,
                    remainingTime,
                    serverId,
                    payStat,
                    0);
                client->ReleaseRef();
                if (result != 0) {
                    // Log.AddLog(LOG_NORMAL,
                    //     "component=billing event=entry_rejected peer=ip_server account_id=%u server_id=%d reason_code=%d action=stop_charge",
                    //     uid, static_cast<int>(serverId), static_cast<int>(result))
                    // Log.AddLog(LOG_NORMAL,
                    //     "[ip-billing] Account %u was refused entry to server %d (reason %d). Stopping IP charging.",
                    //     uid, static_cast<int>(serverId), static_cast<int>(result))
                    // L2_LOG_INFO(Log,
                    //     "[ip-billing] Account %u was refused entry to server %d (reason %d). Stopping IP charging.",
                    //     uid, static_cast<int>(serverId), static_cast<int>(result))
                    L2_LOG_INFO(Log,
                        "[ip-billing] Account %u was refused entry to server %d (reason %d). Requesting IP charge-stop.",
                        uid, static_cast<int>(serverId), static_cast<int>(result));
                    ipsessionDB.StopIPCharge(
                        uid,
                        address,
                        payStat,
                        0,
                        runtime::crt::Time32Now(),
                        serverId,
                        user->account);
                }
            }
            user->ReleaseRef();
        }
    } catch (...) {
        // Original: ELOG("*%s:%d(%s) exception\n", "IPSessionDB.cpp", 1232,
        //     "Wed Oct 23 17:02:11 2019")
        ELOG("[IPSessionDB.cpp] Exception while processing an IP charge-start reply. Entering exception handling.\n");
        send_exception(true);
    }
    return false;
}

// L2Authd.exe 0x0045A440, Src/ipsessiondb.cpp:1237-1250.
static bool GetIPAcquireSuccess(
    [[maybe_unused]] CIPSocket* socket,
    const unsigned char* packet) {
    try {
        const int uid = GetIntFromPacket(&packet);
        const char payStat = GetCharFromPacket(&packet);
        const int remainingTime = GetIntFromPacket(&packet);
        const int sessionId = GetIntFromPacket(&packet);
        ipsessionDB.AcquireSessionSuccess(
            uid,
            sessionId,
            0,
            remainingTime,
            payStat);
    } catch (...) {
        // Original: ELOG("*%s:%d(%s) exception\n", "IPSessionDB.cpp", 1248,
        //     "Wed Oct 23 17:02:11 2019")
        ELOG("[IPSessionDB.cpp] Exception while processing an IP session acquisition success reply. Entering exception handling.\n");
        send_exception(true);
    }
    return false;
}

// L2Authd.exe 0x0045A530, Src/ipsessiondb.cpp:1253-1269.
static bool GetIPAcquireFail(
    [[maybe_unused]] CIPSocket* socket,
    const unsigned char* packet) {
    try {
        const int uid = GetIntFromPacket(&packet);
        const char result = GetCharFromPacket(&packet);
        if (uid > 0) {
            ipsessionDB.AcquireSessionFail(
                uid,
                0,
                result);
        }
    } catch (...) {
        // Original: ELOG("*%s:%d(%s) exception\n", "IPSessionDB.cpp", 1267,
        //     "Wed Oct 23 17:02:11 2019")
        ELOG("[IPSessionDB.cpp] Exception while processing an IP session acquisition rejection. Entering exception handling.\n");
        send_exception(true);
    }
    return false;
}

// L2Authd.exe 0x0045A600, Src/ipsessiondb.cpp:1272-1281.
static bool GetConnectSessionKey(
    CIPSocket* socket,
    const unsigned char* packet) {
    try {
        socket->SetConnectSessionKey(
            static_cast<unsigned int>(
                GetIntFromPacket(&packet)));
        // Log.AddLog(LOG_NORMAL,
        //     "component=network event=handshake_key_received peer=ip_server socket=%u",
        //     socket->m_hSocket)
        // Log.AddLog(LOG_NORMAL,
        //     "[ip-service] Received the handshake key on socket %u.",
        //     socket->m_hSocket)
        L2_LOG_INFO(Log,
            "[ip-service] Received the handshake key on socket %u.",
            socket->m_hSocket);
    } catch (...) {
        // Original: ELOG("*%s:%d(%s) exception\n", "IPSessionDB.cpp", 1279,
        //     "Wed Oct 23 17:02:11 2019")
        ELOG("[IPSessionDB.cpp] Exception while processing the IP-service handshake key. Entering exception handling.\n");
        send_exception(true);
    }
    return false;
}

// L2Authd.exe 0x0045A6B0, Src/ipsessiondb.cpp:1285-1303.
static bool GetIPKick(
    [[maybe_unused]] CIPSocket* socket,
    const unsigned char* packet) {
    try {
        [[maybe_unused]] const in_addr address =
            GetAddrFromPacket(&packet);
        [[maybe_unused]] const int payStat =
            GetIntFromPacket(&packet);
        [[maybe_unused]] const int gameId =
            GetIntFromPacket(&packet);
        char account[16]{};
        GetStrFromPacket(&packet, 16, account);
        const int uid = GetIntFromPacket(&packet);
        account[14] = '\0';
        // Log.AddLog(LOG_NORMAL,
        //     "component=billing event=disconnect_requested peer=ip_server account_id=%d client_reason=21 action=request_disconnect",
        //     uid)
        // Log.AddLog(LOG_NORMAL,
        //     "[ip-billing] IP service requested disconnection of account %d with client reason 21.",
        //     uid)
        L2_LOG_INFO(Log,
            "[ip-billing] IP service requested disconnection of account %d with client reason 21.",
            uid);
        accountdb.KickAccount(uid, 0x15, true, true);
    } catch (...) {
        // Original: ELOG("*%s:%d(%s) exception\n", "IPSessionDB.cpp", 1300,
        //     "Wed Oct 23 17:02:11 2019")
        ELOG("[IPSessionDB.cpp] Exception while processing an IP-service account disconnection request. Entering exception handling.\n");
        send_exception(true);
    }
    return false;
}

// L2Authd.exe 0x0045A7F0, Src/ipsessiondb.cpp:1306-1338.
static bool ReadyIPOK(
    [[maybe_unused]] CIPSocket* socket,
    const unsigned char* packet) {
    try {
        const unsigned int uid =
            static_cast<unsigned int>(
                GetIntFromPacket(&packet));
        const char serverId = GetCharFromPacket(&packet);
        const int remainingTime = GetIntFromPacket(&packet);
        const int payStat = GetIntFromPacket(&packet);
        const unsigned int address =
            static_cast<unsigned int>(
                GetIntFromPacket(&packet));

        LoginUser* user = nullptr;
        if (accountdb.FindAccount(
                static_cast<int>(uid),
                &user)) {
            CClientSession* const client =
                serverEx.FindSession(user->sid);
            if (client != nullptr) {
                const char result = accountdb.AboutToPlay(
                    client,
                    static_cast<int>(uid),
                    user,
                    remainingTime,
                    serverId,
                    payStat,
                    0);
                client->ReleaseRef();
                if (result != 0) {
                    // Log.AddLog(LOG_NORMAL,
                    //     "component=billing event=entry_rejected peer=ip_server account_id=%u server_id=%d reason_code=%d action=stop_charge",
                    //     uid, static_cast<int>(serverId), static_cast<int>(result))
                    // Log.AddLog(LOG_NORMAL,
                    //     "[ip-billing] Account %u was refused entry to server %d (reason %d). Stopping IP charging.",
                    //     uid, static_cast<int>(serverId), static_cast<int>(result))
                    // L2_LOG_INFO(Log,
                    //     "[ip-billing] Account %u was refused entry to server %d (reason %d). Stopping IP charging.",
                    //     uid, static_cast<int>(serverId), static_cast<int>(result))
                    L2_LOG_INFO(Log,
                        "[ip-billing] Account %u was refused entry to server %d (reason %d). Requesting IP charge-stop.",
                        uid, static_cast<int>(serverId), static_cast<int>(result));
                    ipsessionDB.StopIPCharge(
                        uid,
                        address,
                        payStat,
                        0,
                        runtime::crt::Time32Now(),
                        serverId,
                        user->account);
                }
            }
            user->ReleaseRef();
        }
    } catch (...) {
        // Original: ELOG("*%s:%d(%s) exception\n", "IPSessionDB.cpp", 1336,
        //     "Wed Oct 23 17:02:11 2019")
        ELOG("[IPSessionDB.cpp] Exception while processing an IP ready-to-play acceptance. Entering exception handling.\n");
        send_exception(true);
    }
    return false;
}

// L2Authd.exe 0x0045A9C0, Src/ipsessiondb.cpp:1342-1357.
static bool ReadyIPFail(
    [[maybe_unused]] CIPSocket* socket,
    const unsigned char* packet) {
    try {
        const int uid = GetIntFromPacket(&packet);
        [[maybe_unused]] const char result =
            GetCharFromPacket(&packet);
        if (uid > 0) {
            // Log.AddLog(LOG_NORMAL,
            //     "component=billing event=entry_rejected peer=ip_server account_id=%d peer_reason=%u action=logout",
            //     uid, static_cast<unsigned int>(static_cast<unsigned char>(result)))
            // Log.AddLog(LOG_NORMAL,
            //     "[ip-billing] Entry rejected for account %d (peer reason %u). Logging out.",
            //     uid, static_cast<unsigned int>(static_cast<unsigned char>(result)))
            L2_LOG_INFO(Log,
                "[ip-billing] Entry rejected for account %d (peer reason %u). Logging out.",
                uid, static_cast<unsigned int>(static_cast<unsigned char>(result)));
            accountdb.LogoutProcess(uid, 0);
        }
    } catch (...) {
        // Original: ELOG("*%s:%d(%s) exception\n", "IPSessionDB.cpp", 1355,
        //     "Wed Oct 23 17:02:11 2019")
        ELOG("[IPSessionDB.cpp] Exception while processing an IP ready-to-play rejection. Entering exception handling.\n");
        send_exception(true);
    }
    return false;
}

// L2Authd.exe 0x0045AA90, Src/ipsessiondb.cpp:1360-1372.
static bool SetStartTimeFail(
    [[maybe_unused]] CIPSocket* socket,
    const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    if (uid > 0) {
        // Log.AddLog(LOG_NORMAL,
        //     "component=billing event=disconnect_requested peer=ip_server operation=confirm_start_time account_id=%d client_reason=34 action=request_disconnect",
        //     uid)
        // Log.AddLog(LOG_NORMAL,
        //     "[ip-billing] Start-time confirmation requires disconnection of account %d with client reason 34.",
        //     uid)
        // L2_LOG_INFO(Log,
        //     "[ip-billing] Start-time confirmation requires disconnection of account %d with client reason 34.",
        //     uid)
        L2_LOG_INFO(Log,
            "[ip-billing] Start-time confirmation failed for account %d. Requesting disconnection with client reason 34.",
            uid);
        accountdb.KickAccount(uid, 0x22, true, true);
    }
    return false;
}

// L2Authd.exe 0x0045AAD0, Src/ipsessiondb.cpp:1375-1395.
static bool ReconnectOK(
    [[maybe_unused]] CIPSocket* socket,
    const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    const int sessionId = GetIntFromPacket(&packet);
    const in_addr address = GetAddrFromPacket(&packet);
    const int payStat = GetIntFromPacket(&packet);
    char account[16];
    if (accountdb.FindAccount(uid, account) < 1) {
        // Log.AddLog(LOG_NORMAL,
        //     "component=billing event=session_restore_ignored peer=ip_server account_id=%d reason=account_not_found action=release_remote_session",
        //     uid)
        // Log.AddLog(LOG_NORMAL,
        //     "[ip-billing] Ignoring session restore for account %d: account not found. Releasing the remote session.",
        //     uid)
        // L2_LOG_INFO(Log,
        //     "[ip-billing] Ignoring session restore for account %d: account not found. Releasing the remote session.",
        //     uid)
        L2_LOG_INFO(Log,
            "[ip-billing] Ignoring session restore for account %d: account not found. Attempting to request remote session release.",
            uid);
        ipsessionDB.ReleaseSessionRequest(
            sessionId,
            address,
            payStat);
    } else {
        ipsessionDB.AddSessionID(uid, sessionId);
    }
    return false;
}

// L2Authd.exe 0x0045AB80, Src/ipsessiondb.cpp:1399-1407.
static bool ReconnectFail(
    [[maybe_unused]] CIPSocket* socket,
    const unsigned char* packet) {
    [[maybe_unused]] const int uid =
        GetIntFromPacket(&packet);
    [[maybe_unused]] const int result =
        GetIntFromPacket(&packet);
    // Log.AddLog(LOG_WARN,
    //     "component=billing event=session_restore_failed peer=ip_server account_id=%d peer_result=%d action=ignore_reply",
    //     uid, result)
    // Log.AddLog(LOG_WARN,
    //     "[ip-billing] Session restore failed for account %d (peer result %d). Ignoring the reply.",
    //     uid, result)
    L2_LOG_WARN(Log,
        "[ip-billing] Session restore failed for account %d (peer result %d). Ignoring the reply.",
        uid, result);
    return false;
}

// L2Authd.exe 0x0045ABC0, Src/ipsessiondb.cpp:1410-1435.
static bool GetIPKindRes(
    [[maybe_unused]] CIPSocket* socket,
    const unsigned char* packet) {
    try {
        const char kindResult = GetCharFromPacket(&packet);
        const int gameAccountNo = GetIntFromPacket(&packet);
        const int valueType = GetIntFromPacket(&packet);
        const int valueKind = GetIntFromPacket(&packet);
        const int uid = GetIntFromPacket(&packet);
        char cafeId[9]{};
        GetStrFromPacket(&packet, 8, cafeId);
        cafeId[8] = '\0';
        const int inCharge = GetIntFromPacket(&packet);
        const int restrictionNo = GetIntFromPacket(&packet);
        char chargeState[16]{};
        GetStrFromPacket(&packet, 14, chargeState);
        ipsessionDB.IPKindRes(
            kindResult,
            gameAccountNo,
            valueType,
            valueKind,
            uid,
            cafeId,
            inCharge,
            restrictionNo,
            chargeState);
    } catch (...) {
        // Original: ELOG("*%s:%d(%s) exception\n", "IPSessionDB.cpp", 1433,
        //     "Wed Oct 23 17:02:11 2019")
        ELOG("[IPSessionDB.cpp] Exception while processing an IP charge-kind reply. Entering exception handling.\n");
        send_exception(true);
    }
    return false;
}

// L2Authd.exe 0x0045CAF0, Src/ipsessiondb.cpp:121.
CIPPacketServerPool::CSlot::CSlot()
    : m_pPacket(nullptr),
      m_lock(eCustomSpinLock, 0) {}

// L2Authd.exe 0x0045CC80, Src/ipsessiondb.cpp:126.
CIPPacketServerPool::~CIPPacketServerPool() {
    CIPPacketServer::FreeAll();
}
