#include "PBServerMgr.h"

#include "Shared/src/ScopedLockGuards.h"

#include "Platform/IOSocketTransport.h"
#include "Shared/src/Runtime/NativeSocketError.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/TickCount32.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/TimerQueue.h"
#include "AccountContainer.h"
#include "ClientSession.h"
#include "Config.h"
#include "ConnectedIPList.h"
#include "Exception.h"
#include "IPSessionDB.h"
#include "L2AuthD.h"
#include "Log.h"
#include "PBSSocket.h"
#include "Protocol.h"
#include "Reporter.h"
#include "ServerList.h"
#include "SocketServerEx.h"
#include "Thread.h"
#include "Util.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>

#if defined(_WIN32)
#include <WinSock2.h>
#else
#include <arpa/inet.h>
#endif

PBServerMgr* PBServerMgr::sInstance = nullptr;

// L2Authd.exe 0x00478BA0, Src/pbservermgr.h:14.
BANUser::BANUser()
    : blockReason(0),
      result(0),
      restrictionNo(0),
      gameAccountNo(0) {
    gameAccount[0] = '\0';
}

// L2Authd.exe 0x00474BA0, Src/pbservermgr.cpp:23-32.
PBSUser* PBSUser::Alloc() {
    auto* const user = new PBSUser;
    user->pClientSession = nullptr;
    user->pLoginUser = nullptr;
    user->mRefCount = 0;
    user->opTime = 0;
    user->mOrigCmd = 0;
    user->AddRef();
    return user;
}

// L2Authd.exe 0x00474C50, Src/pbservermgr.cpp:49-57.
void PBSUser::Free() {
    if (pLoginUser != nullptr) {
        pLoginUser->ReleaseRef();
    }
    if (pClientSession != nullptr) {
        pClientSession->ReleaseRef();
    }
    ::operator delete(this);
}

// L2Authd.exe 0x00474CA0, Src/pbservermgr.cpp:36-38.
void PBSUser::AddRef() {
    runtime::raw_atomic::IncrementI32(
        &mRefCount);
}

// L2Authd.exe 0x00474CC0, Src/pbservermgr.cpp:42-45.
void PBSUser::ReleaseRef() {
    if (runtime::raw_atomic::DecrementI32(
            &mRefCount) == 0) {
        Free();
    }
}

// L2Authd.exe 0x00474CF0, Src/pbservermgr.cpp:70-78.
PBServerMgr::PBServerMgr()
    : mUserMapLock(eCriticalSection, 0) {
    if (sInstance == nullptr) {
        sInstance = this;
        mPBServerConnected = false;
        mPBServerReConnect = false;
        mpPBSSocket = nullptr;
        mhPBServerTimer = nullptr;
    }
}

// L2Authd.exe 0x00474D90, Src/pbservermgr.cpp:82-84.
PBServerMgr::~PBServerMgr() {
    sInstance = nullptr;
}

// L2Authd.exe 0x00474E00, Src/pbservermgr.cpp:136-149.
void PBServerMgr::Initialize() {
    const std::uint32_t peerAddress = ntohl(config.PBSIP.s_addr);
    std::uint32_t nativeSocket = UINT32_C(0xffffffff);
    const bool connected =
        authd::platform::ConnectNativeSocket(
            nativeSocket,
            config.PBSIP,
            static_cast<std::uint16_t>(
                config.PBSPort));
    if (!connected) {
        // Log.AddLog(LOG_WARN,
        //     "component=network event=connect_failed peer=pb address=%u.%u.%u.%u port=%u socket=%u error_domain=winsock_compatible code=%d action=close_socket reconnect=schedule_pending",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.PBSPort)), nativeSocket,
        //     ::portable::GetNativeSocketLastError())
        // Log.AddLog(LOG_WARN,
        //     "[PBS] Could not connect to %u.%u.%u.%u:%u on socket %u (WinSock-compatible error %d). Closing the socket. Requesting another retry.",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.PBSPort)), nativeSocket,
        //     ::portable::GetNativeSocketLastError())
        // L2_LOG_WARN(Log,
        //     "[PBS] Could not connect to %u.%u.%u.%u:%u on socket %u (WinSock-compatible error %d). Closing the socket. Requesting another retry.",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.PBSPort)), nativeSocket,
        //     ::portable::GetNativeSocketLastError())
        L2_LOG_WARN(Log,
            "[PBS] Could not connect to %u.%u.%u.%u:%u on socket %u (WinSock-compatible error %d). %s",
            (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            (peerAddress >> 8U) & 255U, peerAddress & 255U,
            static_cast<unsigned int>(static_cast<std::uint16_t>(config.PBSPort)), nativeSocket,
            ::portable::GetNativeSocketLastError(),
            nativeSocket == UINT32_C(0xffffffff)
                ? "No valid socket was created; the close path will not schedule a retry."
                : "Closing the socket; its close callback will request a retry.");
    }
    mpPBSSocket = CPBSSocket::Allocate(nativeSocket);
    if (!connected) {
        mpPBSSocket->CloseSocket();
    } else {
        authd::platform::InitializeNativeSocket(
            mpPBSSocket,
            nativeSocket,
            g_hIOCompletionPort);
    }
}

// L2Authd.exe 0x00474EA0, Src/pbservermgr.cpp:88-132.
void PBServerMgr::PBServerTimerRoutine() {
    if (mhPBServerTimer != nullptr) {
        runtime::timer_queue::DeleteTimer(
            nullptr,
            mhPBServerTimer);
        mhPBServerTimer = nullptr;
    }
    if (mPBServerConnected) {
        return;
    }

    const std::uint32_t peerAddress = ntohl(config.PBSIP.s_addr);
    std::uint32_t nativeSocket = UINT32_C(0xffffffff);
    const bool connected =
        authd::platform::ConnectNativeSocket(
            nativeSocket,
            config.PBSIP,
            static_cast<std::uint16_t>(
                config.PBSPort));
    if (!connected) {
        // Log.AddLog(LOG_WARN,
        //     "component=network event=reconnect_failed peer=pb address=%u.%u.%u.%u port=%u socket=%u error_domain=winsock_compatible code=%d action=close_socket reconnect=schedule_pending",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.PBSPort)), nativeSocket,
        //     ::portable::GetNativeSocketLastError())
        // Log.AddLog(LOG_WARN,
        //     "[PBS] Could not reconnect to %u.%u.%u.%u:%u on socket %u (WinSock-compatible error %d). Closing the socket and requesting another retry.",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.PBSPort)), nativeSocket,
        //     ::portable::GetNativeSocketLastError())
        // L2_LOG_WARN(Log,
        //     "[PBS] Could not reconnect to %u.%u.%u.%u:%u on socket %u (WinSock-compatible error %d). Closing the socket and requesting another retry.",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.PBSPort)), nativeSocket,
        //     ::portable::GetNativeSocketLastError())
        L2_LOG_WARN(Log,
            "[PBS] Could not reconnect to %u.%u.%u.%u:%u on socket %u (WinSock-compatible error %d). %s",
            (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            (peerAddress >> 8U) & 255U, peerAddress & 255U,
            static_cast<unsigned int>(static_cast<std::uint16_t>(config.PBSPort)), nativeSocket,
            ::portable::GetNativeSocketLastError(),
            nativeSocket == UINT32_C(0xffffffff)
                ? "No valid socket was created; the close path will not schedule a retry."
                : "Closing the socket; its close callback will request a retry.");
    }
    CPBSSocket* const replacement =
        CPBSSocket::Allocate(nativeSocket);
    if (!connected) {
        replacement->CloseSocket();
        replacement->ReleaseRef();
        return;
    }

    CPBSSocket* const oldSocket = mpPBSSocket;
    authd::platform::InitializeNativeSocket(
        replacement,
        nativeSocket,
        g_hIOCompletionPort);
    runtime::raw_atomic::ExchangePointer(
        &mpPBSSocket,
        replacement);
    oldSocket->ReleaseRef();
    mPBServerConnected = true;
    if (config.PBType == 2) {
        mPBServerReConnect = true;
        accountdb.ReConnectPBServer();
    }
}

// L2Authd.exe 0x00474FC0, Src/pbservermgr.cpp:206-213.
bool PBServerMgr::IsPBCharging(int payStat) {
    return payStat >= 1000 &&
           ((payStat % 1000) / 100) >= 1;
}

// L2Authd.exe 0x00475000, Src/pbservermgr.cpp:183-202.
bool PBServerMgr::AddUserWait(
    int uid,
    PBSUser* user) {
    user->AddRef();
    user->opTime = static_cast<std::int32_t>(
        std::time(nullptr));
    bool inserted = false;
    {
        L2_GUARD(mUserMapLock);
        inserted =
            mWaitUserMap.insert({uid, user}).second;
    }
    if (!inserted) {
        user->ReleaseRef();
    }
    return inserted;
}

// L2Authd.exe 0x00475080, Src/pbservermgr.cpp:153-179.
bool PBServerMgr::DelUserWait(
    int uid,
    PBSUser** user) {
    PBSUser* oldUser = nullptr;
    {
        L2_GUARD(mUserMapLock);
        const auto found = mWaitUserMap.find(uid);
        if (found == mWaitUserMap.end()) {
            return false;
        }

        oldUser = found->second;
        mWaitUserMap.erase(found);
    }
    if (user == nullptr) {
        oldUser->ReleaseRef();
    } else {
        *user = oldUser;
    }
    return true;
}

// L2Authd.exe 0x00475140, Src/pbservermgr.cpp:228-289.
char PBServerMgr::RequestGetValueInfo(
    int uid,
    LoginUser* user,
    CClientSession* client,
    char requestType,
    char chargeServerId) {
    try {
        PBSUser* const waitUser = PBSUser::Alloc();
        if (user != nullptr) {
            waitUser->pLoginUser = user;
            user->AddRef();
        }
        waitUser->mOrigCmd = requestType;
        if (client != nullptr) {
            waitUser->pClientSession = client;
            client->AddRef();
        }

        if (!AddUserWait(uid, waitUser)) {
            // Log.AddLog(LOG_WARN,
            //     "component=billing event=request_registration_failed peer=pb operation=get_value_info account_id=%d reason=account_already_pending result_code=7 action=return_failure",
            //     uid)
            // Log.AddLog(LOG_WARN,
            //     "[PBS] Cannot register a billing information request for account %d: another request is pending. Returning failure code 7.",
            //     uid)
            L2_LOG_WARN(Log,
                "[PBS] Cannot register a billing information request for account %d: another request is pending. Returning failure code 7.",
                uid);
            waitUser->ReleaseRef();
            return 7;
        }

        bool sent = false;
        if (config.PBType == 2) {
            if (chargeServerId == 0) {
                unsigned char reply[12]{};
                Assemble(
                    reinterpret_cast<char*>(reply),
                    10,
                    "ddc",
                    uid,
                    0,
                    1);
                RecvGetValueInfo(reply);
                sent = true;
            } else {
                sent = mpPBSSocket->Send(
                    "cDdd",
                    AV_GET_VALUE_INFO,
                    static_cast<std::int64_t>(uid),
                    static_cast<int>(chargeServerId),
                    user->loginIp.s_addr);
            }
        } else {
            sent = mpPBSSocket->Send(
                "cDd",
                AV_GET_VALUE_INFO,
                static_cast<std::int64_t>(uid),
                0);
        }

        if (!sent) {
            // Log.AddLog(LOG_WARN,
            //     "component=billing event=request_not_submitted peer=pb operation=get_value_info account_id=%d reason=socket_send_rejected result_code=20 action=remove_pending_request",
            //     uid)
            // Log.AddLog(LOG_WARN,
            //     "[PBS] Could not request billing information for account %d. Removing the pending request and returning code 20.",
            //     uid)
            // L2_LOG_WARN(Log,
            //     "[PBS] Could not request billing information for account %d. Removing the pending request and returning code 20.",
            //     uid)
            L2_LOG_WARN(Log,
                "[PBS] Billing information request for account %d was not submitted: PBS connection is unavailable. Removing the pending request and returning code 20.",
                uid);
            DelUserWait(uid, nullptr);
            waitUser->ReleaseRef();
            return 0x14;
        }
        waitUser->ReleaseRef();
        return 0;
    } catch (...) {
        // ELOG(
        // "*%s:%d(%s) exception\n",
        // "PBServerMgr.cpp",
        // 287,
        // "Wed Oct 23 17:02:11 2019")
        ELOG(
            "[PBServerMgr.cpp] Exception while requesting billing value information for account %d (request type %u, charge server %u). Entering exception handling.\n",
            uid, static_cast<unsigned int>(static_cast<unsigned char>(requestType)), static_cast<unsigned int>(static_cast<unsigned char>(chargeServerId)));
        send_exception(true);
        return 0;
    }
}

// L2Authd.exe 0x00475330, Src/pbservermgr.cpp:343-364.
char PBServerMgr::RequestStartCharge(
    int uid,
    int serverId,
    in_addr address) {
    try {
        const bool sent = config.PBType == 2
            ? mpPBSSocket->Send(
                  "cDdd",
                  AV_START_CHARGE,
                  static_cast<std::int64_t>(uid),
                  serverId,
                  address.s_addr)
            : mpPBSSocket->Send(
                  "cDd",
                  AV_START_CHARGE,
                  static_cast<std::int64_t>(uid),
                  serverId);
        if (!sent) {
            // Log.AddLog(LOG_WARN,
            //     "component=billing event=request_not_submitted peer=pb operation=start_charge account_id=%d server_id=%d reason=socket_send_rejected result_code=20 action=return_failure",
            //     uid, serverId)
            // Log.AddLog(LOG_WARN,
            //     "[PBS] Could not request charging for account %d on server %d. Returning failure code 20.",
            //     uid, serverId)
            // L2_LOG_WARN(Log,
            //     "[PBS] Could not request charging for account %d on server %d. Returning failure code 20.",
            //     uid, serverId)
            L2_LOG_WARN(Log,
                "[PBS] Charge-start request for account %d on server %d was not submitted: PBS connection is unavailable. Returning failure code 20.",
                uid, serverId);
        }
        return sent ? 0 : 0x14;
    } catch (...) {
        // ELOG(
        // "*%s:%d(%s) exception\n",
        // "PBServerMgr.cpp",
        // 362,
        // "Wed Oct 23 17:02:11 2019")
        ELOG(
            "[PBServerMgr.cpp] Exception while requesting charge start for account %d on server %d. Entering exception handling.\n",
            uid, serverId);
        send_exception(true);
        return 0;
    }
}

// L2Authd.exe 0x00475450, Src/pbservermgr.cpp:293-325.
char PBServerMgr::RequestValueAbandon(
    int uid,
    unsigned char* valueId,
    LoginUser* user) {
    try {
        PBSUser* const waitUser = PBSUser::Alloc();
        if (user != nullptr) {
            waitUser->pLoginUser = user;
            user->AddRef();
        }
        if (!AddUserWait(uid, waitUser)) {
            // Log.AddLog(LOG_WARN,
            //     "component=billing event=request_registration_failed peer=pb operation=abandon_value account_id=%d reason=account_already_pending result_code=7 action=return_failure",
            //     uid)
            // Log.AddLog(LOG_WARN,
            //     "[PBS] Cannot register a billing value release for account %d: another request is pending. Returning failure code 7.",
            //     uid)
            L2_LOG_WARN(Log,
                "[PBS] Cannot register a billing value release for account %d: another request is pending. Returning failure code 7.",
                uid);
            waitUser->ReleaseRef();
            return 7;
        }
        if (!mpPBSSocket->Send(
                "cDdb",
                AV_VALUE_ABANDON,
                static_cast<std::int64_t>(uid),
                0,
                16,
                valueId)) {
            // Log.AddLog(LOG_WARN,
            //     "component=billing event=request_not_submitted peer=pb operation=abandon_value account_id=%d reason=socket_send_rejected result_code=20 action=remove_pending_request",
            //     uid)
            // Log.AddLog(LOG_WARN,
            //     "[PBS] Could not request billing value release for account %d. Removing the pending request and returning code 20.",
            //     uid)
            // L2_LOG_WARN(Log,
            //     "[PBS] Could not request billing value release for account %d. Removing the pending request and returning code 20.",
            //     uid)
            L2_LOG_WARN(Log,
                "[PBS] Billing value release for account %d was not submitted: PBS connection is unavailable. Removing the pending request and returning code 20.",
                uid);
            DelUserWait(uid, nullptr);
            waitUser->ReleaseRef();
            return 0x14;
        }
        waitUser->ReleaseRef();
        return 0;
    } catch (...) {
        // ELOG(
        // "*%s:%d(%s) exception\n",
        // "PBServerMgr.cpp",
        // 323,
        // "Wed Oct 23 17:02:11 2019")
        ELOG(
            "[PBServerMgr.cpp] Exception while requesting billing value release for account %d. Entering exception handling.\n",
            uid);
        send_exception(true);
        return 0;
    }
}

// L2Authd.exe 0x004755A0, Src/pbservermgr.cpp:368-379.
char PBServerMgr::RequestStartChargeList(
    int count,
    int userListSize,
    char* userList) {
    try {
        // return mpPBSSocket->Send(
        //            "cdb",
        //            AV_START_CHARGE_LIST,
        //            count,
        //            userListSize,
        //            userList)
        //     ? 0
        //     : 0x14.
        const bool submitted = mpPBSSocket->Send(
            "cdb", AV_START_CHARGE_LIST, count, userListSize, userList);
        // Noise: the sole production caller, AccountDB::ReConnectPBServer,
        // reports this failure with the same batch size and the retry outcome.
        // if (!submitted) {
        //     L2_LOG_WARN(Log,
        //         "[PBS] Could not submit the charge-start list (%d accounts, %d bytes). Returning failure code 20.",
        //         count, userListSize)
        // }
        return submitted ? 0 : 0x14;
    } catch (...) {
        // ELOG(
        // "*%s:%d(%s) exception\n",
        // "PBServerMgr.cpp",
        // 377,
        // "Wed Oct 23 17:02:11 2019")
        ELOG(
            "[PBServerMgr.cpp] Exception while submitting the charge-start list (%d accounts, %d bytes). Entering exception handling.\n",
            count, userListSize);
        send_exception(true);
        return 0;
    }
}

// L2Authd.exe 0x00475680, Src/pbservermgr.cpp:383-423.
char PBServerMgr::RequestChargeSuspend(
    short reason,
    int result,
    int uid,
    char* account,
    int restrictionNo) {
    try {
        PBSUser* const waitUser = PBSUser::Alloc();
        if (waitUser == nullptr) {
            // The selected branch calls ReleaseRef through a null this.
            // Log.AddLog(LOG_ERROR,
            //     "component=billing event=request_allocation_failed peer=pb operation=suspend_charge account_id=%d action=abort_process",
            //     uid)
            // Log.AddLog(LOG_ERROR,
            //     "[PBS] Could not allocate a charge suspension request for account %d. Aborting the process.",
            //     uid)
            L2_LOG_ERROR(Log,
                "[PBS] Could not allocate a charge suspension request for account %d. Aborting the process.",
                uid);
            std::abort();
        }
        waitUser->mBanUser.blockReason = reason;
        waitUser->mBanUser.result = result;
        waitUser->mBanUser.gameAccountNo = uid;
        StrPrintf(
            waitUser->mBanUser.gameAccount,
            15,
            account);
        waitUser->mBanUser.restrictionNo = restrictionNo;

        if (!AddUserWait(uid, waitUser)) {
            // Log.AddLog(LOG_WARN,
            //     "component=billing event=request_registration_failed peer=pb operation=suspend_charge account_id=%d reason=account_already_pending result_code=7 action=return_failure",
            //     uid)
            // Log.AddLog(LOG_WARN,
            //     "[PBS] Cannot register a charge suspension request for account %d: another request is pending. Returning failure code 7.",
            //     uid)
            L2_LOG_WARN(Log,
                "[PBS] Cannot register a charge suspension request for account %d: another request is pending. Returning failure code 7.",
                uid);
            waitUser->ReleaseRef();
            return 7;
        }
        if (!mpPBSSocket->Send(
                "cDd",
                AV_CHARGE_SUSPEND,
                static_cast<std::int64_t>(uid),
                uid)) {
            // Log.AddLog(LOG_WARN,
            //     "component=billing event=request_not_submitted peer=pb operation=suspend_charge account_id=%d reason=socket_send_rejected result_code=20 action=remove_pending_request",
            //     uid)
            // Log.AddLog(LOG_WARN,
            //     "[PBS] Could not request charge suspension for account %d. Removing the pending request and returning code 20.",
            //     uid)
            // L2_LOG_WARN(Log,
            //     "[PBS] Could not request charge suspension for account %d. Removing the pending request and returning code 20.",
            //     uid)
            L2_LOG_WARN(Log,
                "[PBS] Charge suspension for account %d was not submitted: PBS connection is unavailable. Removing the pending request and returning code 20.",
                uid);
            DelUserWait(uid, nullptr);
            waitUser->ReleaseRef();
            return 0x14;
        }
        waitUser->ReleaseRef();
        return 0;
    } catch (...) {
        // ELOG(
        //     "*%s:%d(%s) exception\n",
        //     "PBServerMgr.cpp",
        //     420,
        //     "Wed Oct 23 17:02:11 2019")
        ELOG(
            "[PBServerMgr.cpp] Exception while requesting charge suspension for account %d (reason %d, restriction %d). Entering exception handling.\n",
            uid, static_cast<int>(reason), restrictionNo);
        send_exception(true);
        return 0;
    }
}

// L2Authd.exe 0x00475810, Src/pbservermgr.cpp:329-339.
char PBServerMgr::NotifyEndCharge(int uid) {
    try {
        // return mpPBSSocket->Send(
        //            "cD",
        //            AV_END_CHARGE,
        //            static_cast<std::int64_t>(uid))
        //     ? 0
        //     : 0x14.
        const bool sent = mpPBSSocket->Send(
            "cD",
            AV_END_CHARGE,
            static_cast<std::int64_t>(uid));
        if (!sent) {
            // Log.AddLog(LOG_WARN,
            //     "component=billing event=request_not_submitted peer=pb operation=end_charge account_id=%d reason=socket_send_rejected result_code=20 action=return_failure",
            //     uid)
            // Log.AddLog(LOG_WARN,
            //     "[PBS] Could not notify the service that charging ended for account %d. Returning failure code 20.",
            //     uid)
            // L2_LOG_WARN(Log,
            //     "[PBS] Could not notify the service that charging ended for account %d. Returning failure code 20.",
            //     uid)
            L2_LOG_WARN(Log,
                "[PBS] Charge-end notification for account %d was not submitted: PBS connection is unavailable. Returning failure code 20.",
                uid);
        }
        return sent ? 0 : 0x14;
    } catch (...) {
        // ELOG(
        //     "*%s:%d(%s) exception\n",
        //     "PBServerMgr.cpp",
        //     337,
        //     "Wed Oct 23 17:02:11 2019")
        ELOG(
            "[PBServerMgr.cpp] Exception while notifying PBS that charging ended for account %d. Entering exception handling.\n",
            uid);
        send_exception(true);
        return 0;
    }
}

// L2Authd.exe 0x004758E0, Src/pbservermgr.cpp:433-575.
void PBServerMgr::RecvGetValueInfo(
    const unsigned char* packet) {
    int remainPlayTime = 0;
    int reservations = 0;
    char valueType = 0;
    int itemId = 0;
    std::uint32_t availableServersSize = 0;
    unsigned char availableServers[64]{};
    unsigned char valueId[16]{};
    char computationBasis = 0;

    const int uid = static_cast<int>(
        GetInt64FromPacket(&packet));
    const char reason = GetCharFromPacket(&packet);
    if (reason == 0 || reason == 3 || reason == 11) {
        [[maybe_unused]] const int gameAccountNo =
            GetIntFromPacket(&packet);
        remainPlayTime = GetIntFromPacket(&packet);
        reservations = GetIntFromPacket(&packet);
        valueType = GetCharFromPacket(&packet);
        itemId = GetIntFromPacket(&packet);
        [[maybe_unused]] const char valueKind =
            GetCharFromPacket(&packet);
        availableServersSize = static_cast<std::uint32_t>(
            GetIntFromPacket(&packet));
        // The target copies the packet-provided count into this fixed 64-byte
        // local without clamping. Keep that shipping parser contract here.
        std::memcpy(
            availableServers,
            packet,
            availableServersSize);
        packet += availableServersSize;
        std::memcpy(valueId, packet, 16);
        packet += 16;
        computationBasis = GetCharFromPacket(&packet);
    }

    PBSUser* waitUser = nullptr;
    if (!DelUserWait(uid, &waitUser)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "User information was deleted while getting payment info from PBS(%d)",
        //     uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=billing event=reply_without_pending_request peer=pb operation=get_value_info account_id=%d action=ignore_reply",
        //     uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[PBS] Ignoring billing information reply for account %d: no request is pending.",
        //     uid)
        L2_LOG_WARN(Log,
            "[PBS] Ignoring billing information reply for account %d: no request is pending.",
            uid);
        return;
    }

    accountdb.SetAvailableServersData(
        uid,
        static_cast<int>(availableServersSize),
        availableServers,
        waitUser->pLoginUser);
    switch (waitUser->mOrigCmd) {
    case 2:
        GetValueInfoAboutToPlay(
            waitUser->pLoginUser,
            waitUser->pClientSession,
            uid,
            reason,
            valueType,
            remainPlayTime,
            reservations,
            computationBasis,
            itemId);
        waitUser->ReleaseRef();
        return;
    case 8:
        GetValueInfoChangeServer(
            waitUser->pLoginUser,
            waitUser->pClientSession,
            uid,
            reason,
            valueType,
            remainPlayTime,
            reservations,
            computationBasis,
            itemId);
        waitUser->ReleaseRef();
        return;
    case 0x19: {
        const WorldServer world = g_ServerList.GetAt(
            waitUser->pLoginUser->serverid);
        int forwarded = 0;
        if (reason == 0 || reason == 3) {
            forwarded = SendSocket(
                world.inner_addr,
                "cdccddcdbc",
                0x10,
                uid,
                0,
                waitUser->pLoginUser->originChargedServerId,
                remainPlayTime,
                reservations,
                valueType,
                itemId,
                16,
                valueId,
                computationBasis);
        } else {
            // Log.AddLog(LOG_NORMAL,
            //     "component=billing event=value_info_rejected peer=pb operation=get_value_info account_id=%d peer_reason=%u action=send_world_failure",
            //     uid, static_cast<unsigned int>(static_cast<unsigned char>(reason)))
            // Log.AddLog(LOG_NORMAL,
            //     "[PBS] Billing information request for account %d was rejected (peer reason %u). Sending failure to the world server.",
            //     uid, static_cast<unsigned int>(static_cast<unsigned char>(reason)))
            L2_LOG_INFO(Log,
                "[PBS] Billing information request for account %d was rejected (peer reason %u). Sending failure to the world server.",
                uid, static_cast<unsigned int>(static_cast<unsigned char>(reason)));
            forwarded = SendSocket(
                world.inner_addr,
                "cdc",
                0x10,
                uid,
                MapVMToAuthReason(reason));
        }
        if (forwarded == 0) {
            L2_LOG_WARN(Log,
                "[PBS] Billing information reply for account %d (peer reason %u) was not forwarded to world server %u: no connection exists for its internal address. Releasing the pending request without a world reply.",
                uid, static_cast<unsigned int>(static_cast<unsigned char>(reason)),
                static_cast<unsigned int>(waitUser->pLoginUser->serverid));
        }
        waitUser->ReleaseRef();
        return;
    }
    case 0x1c:
        GetValueInfoServersideLogin(
            waitUser->pLoginUser,
            uid,
            reason,
            valueType,
            remainPlayTime,
            reservations,
            computationBasis,
            itemId);
        waitUser->ReleaseRef();
        return;
    default:
        break;
    }

    char result = GetValueInfo(
        waitUser,
        uid,
        reason,
        valueType,
        remainPlayTime,
        reservations,
        computationBasis,
        itemId);
    if (config.PBType == 2 && result == 0x12) {
        result = 0;
    }
    if (result != 0) {
        // Log.AddLog(LOG_NORMAL,
        //     "component=billing event=login_rejected peer=pb operation=login account_id=%d reason_code=%d action=send_client_failure",
        //     uid, static_cast<int>(result))
        // Log.AddLog(LOG_NORMAL,
        //     "[PBS] Login rejected for account %d (reason %d). Sending failure to the client.",
        //     uid, static_cast<int>(result))
        L2_LOG_INFO(Log,
            "[PBS] Login rejected for account %d (reason %d). Sending failure to the client.",
            uid, static_cast<int>(result));
        waitUser->pClientSession->SendFail(
            1,
            result,
            waitUser->pLoginUser);
        waitUser->ReleaseRef();
        return;
    }
    if (waitUser->pLoginUser->chargeServerId == 1 ||
        waitUser->pClientSession->GetSessionId() ==
            UINT32_C(0xffffffff)) {
        waitUser->ReleaseRef();
        return;
    }

    result = accountdb.RegAccount(
        uid,
        waitUser->pLoginUser);
    if (result == 0) {
        CClientSession* const client =
            waitUser->pClientSession;
        LoginUser* const user = waitUser->pLoginUser;
        client->SetUserMode(UM_LOGIN);
        client->Send(
            "cddddddddbd",
            3,
            uid,
            client->GetMd5Key(),
            g_updateKey,
            g_updateKey2,
            user->stat,
            remainPlayTime,
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
    } else {
        // Log.AddLog(LOG_NORMAL,
        //     "component=billing event=login_rejected peer=pb operation=login account_id=%d phase=account_registration reason_code=%d action=send_client_failure",
        //     uid, static_cast<int>(result))
        // Log.AddLog(LOG_NORMAL,
        //     "[PBS] Account registration failed during login for account %d (reason %d). Sending failure to the client.",
        //     uid, static_cast<int>(result))
        // L2_LOG_INFO(Log,
        //     "[PBS] Account registration failed during login for account %d (reason %d). Sending failure to the client.",
        //     uid, static_cast<int>(result))
        L2_LOG_INFO(Log,
            "[PBS] Another login is already registered for account %d (reason %d). Rejecting this client login.",
            uid, static_cast<int>(result));
        waitUser->pClientSession->SendFail(
            1,
            result,
            waitUser->pLoginUser);
    }
    waitUser->ReleaseRef();
}

// L2Authd.exe 0x00476050, Src/pbservermgr.cpp:1187-1226.
bool PBServerMgr::RecvStartCharge(
    const unsigned char* packet) {
    try {
        int remainPlayTime = 0;
        int reservations = 0;
        char valueType = 0;
        int itemId = 0;
        unsigned char valueId[16]{};
        char computationBasis = 0;

        const int uid = static_cast<int>(
            GetInt64FromPacket(&packet));
        const char reason = GetCharFromPacket(&packet);
        if (reason == 0) {
            [[maybe_unused]] const int gameAccountNo =
                GetIntFromPacket(&packet);
            remainPlayTime = GetIntFromPacket(&packet);
            reservations = GetIntFromPacket(&packet);
            valueType = GetCharFromPacket(&packet);
            itemId = GetIntFromPacket(&packet);
            std::memcpy(valueId, packet, 16);
            // Shipping code omits packet += 16 here. GetChar therefore reads
            // valueId[0], a parser bug kept in the exact baseline.
            computationBasis = GetCharFromPacket(&packet);
        }

        if (!accountdb.SetPBData(
                uid,
                valueType,
                remainPlayTime,
                reservations,
                valueId,
                computationBasis,
                itemId)) {
            // Log.AddLog(LOG_WARN,
            //     "component=billing event=charge_update_failed peer=pb operation=start_charge account_id=%d reason=account_not_found action=notify_end_charge",
            //     uid)
            // Log.AddLog(LOG_WARN,
            //     "[PBS] Could not apply charge-start data for account %d: account not found. Notifying the service that charging ended.",
            //     uid)
            L2_LOG_WARN(Log,
                "[PBS] Could not apply charge-start data for account %d: account not found. Notifying the service that charging ended.",
                uid);
            NotifyEndCharge(uid);
        }
        if (reason != 0) {
            // Log.AddLog(LOG_NORMAL,
            //     "component=billing event=disconnect_requested peer=pb operation=start_charge account_id=%d peer_reason=%u action=request_disconnect",
            //     uid, static_cast<unsigned int>(static_cast<unsigned char>(reason)))
            // Log.AddLog(LOG_NORMAL,
            //     "[PBS] Charge-start rejected for account %d (peer reason %u). Requesting disconnection.",
            //     uid, static_cast<unsigned int>(static_cast<unsigned char>(reason)))
            L2_LOG_INFO(Log,
                "[PBS] Charge-start rejected for account %d (peer reason %u). Requesting disconnection.",
                uid, static_cast<unsigned int>(static_cast<unsigned char>(reason)));
        }
        if (reason != 0 &&
            !accountdb.KickAccount(
                uid,
                static_cast<char>(
                    MapVMToAuthReason(reason)),
                false,
                true)) {
            NotifyEndCharge(uid);
        }
        return true;
    } catch (...) {
        // ELOG(
        // "*%s:%d(%s) exception\n",
        // "PBServerMgr.cpp",
        // 1224,
        // "Wed Oct 23 17:02:11 2019")
        ELOG(
            "[PBServerMgr.cpp] Exception while processing the PBS charge-start reply. Entering exception handling.\n");
        send_exception(true);
        return true;
    }
}

// L2Authd.exe 0x00476230, Src/pbservermgr.cpp:1230-1238.
bool PBServerMgr::RecvEndChargeReq(
    int uid,
    char reason) {
    try {
        // Log.AddLog(LOG_NORMAL,
        //     "component=billing event=disconnect_requested peer=pb operation=end_charge account_id=%d reason_code=%u action=request_disconnect",
        //     uid, static_cast<unsigned int>(static_cast<unsigned char>(reason)))
        // Log.AddLog(LOG_NORMAL,
        //     "[PBS] Service requested the end of charging for account %d (reason %u). Requesting disconnection.",
        //     uid, static_cast<unsigned int>(static_cast<unsigned char>(reason)))
        L2_LOG_INFO(Log,
            "[PBS] Service requested the end of charging for account %d (reason %u). Requesting disconnection.",
            uid, static_cast<unsigned int>(static_cast<unsigned char>(reason)));
        if (!accountdb.KickAccount(
                uid,
                reason,
                true,
                true)) {
            NotifyEndCharge(uid);
        }
        return true;
    } catch (...) {
        // ELOG(
        // "*%s:%d(%s) exception\n",
        // "PBServerMgr.cpp",
        // 1236,
        // "Wed Oct 23 17:02:11 2019")
        ELOG(
            "[PBServerMgr.cpp] Exception while processing the PBS end-charge request for account %d (reason %u). Entering exception handling.\n",
            uid, static_cast<unsigned int>(static_cast<unsigned char>(reason)));
        send_exception(true);
        return true;
    }
}

// L2Authd.exe 0x004762F0, Src/pbservermgr.cpp:579-649.
char PBServerMgr::GetValueInfo(
    PBSUser* waitUser,
    int uid,
    char reason,
    char valueType,
    int remainPlayTime,
    int reservations,
    char computationBasis,
    int itemId) {
    LoginUser* const user = waitUser->pLoginUser;
    if (reason == 0) {
        user->pbServer.valueType = valueType;
        user->pbServer.remainTime = remainPlayTime;
        user->pbServer.reservations = reservations;
        user->pbServer.computationBasis =
            computationBasis;
        user->pbServer.itemId = itemId;
    } else if (reason == 3) {
        NotifyEndCharge(uid);
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Uid(%d) is in VM, while it isn't in Auth",
        //     uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=billing event=charge_state_mismatch peer=pb operation=login account_id=%d peer_reason=3 client_reason=7 action=return_failure",
        //     uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[PBS] Billing state disagrees during login for account %d (peer reason 3). Returning failure code 7.",
        //     uid)
        L2_LOG_WARN(Log,
            "[PBS] Billing state disagrees during login for account %d (peer reason 3). Returning failure code 7.",
            uid);
        return 7;
    }

    user->chargeServerId =
        accountdb.DetermineChargeServer(user);
    if (user->chargeServerId == 0) {
        if (config.FreeServer == 3) {
            user->stat = 2000;
            return 0;
        }
        if (!config.useDualCharging) {
            return 0x12;
        }
        user->stat = 0;
        return 0;
    }
    if (user->chargeServerId == 1) {
        const char result = ipsessionDB.RequestIPCharge(
            uid,
            user,
            0);
        if (result == 0) {
            return 0;
        }
        return result == 11 ? 0x15 : 0x14;
    }
    if (user->chargeServerId == 2) {
        user->stat = valueType * 100 + 1000;
        if (config.FreeServer != 3) {
            g_ServerList.MapForbiddenServers(
                user->pbServer.availableServers,
                user->pbServer.availableServersSize,
                user->forbidden_servers,
                16);
            g_ServerList.MapForbiddenServers(
                user->age,
                user->forbidden_servers,
                16);
        }
    }
    return 0;
}

// L2Authd.exe 0x004764C0, Src/pbservermgr.cpp:661-742.
bool PBServerMgr::GetValueInfoChangeServer(
    LoginUser* user,
    CClientSession* client,
    int uid,
    char reason,
    char valueType,
    int remainPlayTime,
    int reservations,
    char computationBasis,
    int itemId) {
    if (reason == 3) {
        NotifyEndCharge(uid);
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Uid(%d) is in VM, while it isn't in Auth",
        //     uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=billing event=charge_state_mismatch peer=pb operation=change_server account_id=%d peer_reason=3 client_reason=7 action=reject_and_logout",
        //     uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[PBS] Billing state disagrees during server transfer for account %d (peer reason 3). Rejecting with code 7 and logging out.",
        //     uid)
        L2_LOG_WARN(Log,
            "[PBS] Billing state disagrees during server transfer for account %d (peer reason 3). Rejecting with code 7 and logging out.",
            uid);
        client->Send("cddc", 12, uid, 0, 7);
        accountdb.LogoutProcess(uid, 0);
        return false;
    }
    if (reason != 0) {
        valueType = 0;
        remainPlayTime = 0;
        reservations = 0;
        computationBasis = 0;
        itemId = 0;
    }
    if (!accountdb.UpdateChargeServerData(
            uid,
            2,
            valueType,
            remainPlayTime,
            reservations,
            computationBasis,
            itemId)) {
        // Log.AddLog(LOG_WARN,
        //     "component=billing event=charge_update_failed peer=pb operation=change_server account_id=%d phase=server_charge_data reason=account_not_found client_reason=10 action=reject_and_logout",
        //     uid)
        // Log.AddLog(LOG_WARN,
        //     "[PBS] Could not update server billing data during transfer for account %d: account not found. Rejecting with code 10 and logging out.",
        //     uid)
        // Original: L2_LOG_WARN(Log,
        //     "[PBS] Could not update server billing data during transfer for account %d: account not found. Rejecting with code 10 and logging out.",
        //     uid)
        L2_LOG_WARN(Log,
            "[PBS] Cannot update billing data during transfer for account %d: the account is absent from AuthD's in-memory registry. Attempting failure code 10 and logout.",
            uid);
        client->Send("cddc", 12, uid, 0, 10);
        accountdb.LogoutProcess(uid, 0);
        return false;
    }

    char result = 0x14;
    if (config.UseIPServer &&
        user->ipServer.valueType > 0) {
        result = ipsessionDB.RequestIPCharge(
            uid,
            user,
            8);
    }
    if (result == 0) {
        // The selected path waits for the asynchronous IP response and skips
        // the remaining charge-selection/client-reply block.
        return true;
    }

    const char chargeServerId =
        accountdb.DetermineChargeServer(user);
    if (chargeServerId == 0) {
        if (config.FreeServer == 1) {
            user->stat = 0;
        } else if (config.FreeServer != 2 &&
                   config.FreeServer != 3) {
            user->stat = 0;
        }
    }
    if (config.useNoBill && chargeServerId == 0) {
        user->stat = 2000;
    }

    if (!accountdb.UpdateChargeData(
            uid,
            chargeServerId,
            0,
            UM_LOGIN)) {
        // Log.AddLog(LOG_WARN,
        //     "component=billing event=charge_update_failed peer=pb operation=change_server account_id=%d phase=selected_charge_data reason=account_not_found client_reason=10 action=send_client_failure",
        //     uid)
        // Log.AddLog(LOG_WARN,
        //     "[PBS] Could not update the selected billing source during transfer for account %d: account not found. Sending client failure code 10.",
        //     uid)
        // Original: L2_LOG_WARN(Log,
        //     "[PBS] Could not update the selected billing source during transfer for account %d: account not found. Sending client failure code 10.",
        //     uid)
        L2_LOG_WARN(Log,
            "[PBS] Cannot select billing source %d during transfer for account %d: the account is absent from AuthD's in-memory registry. Attempting client failure code 10; the client will still be set to login mode.",
            static_cast<int>(chargeServerId), uid);
        client->Send("cddc", 12, uid, 0, 10);
    } else if (user->chargeServerId == 0 &&
               user->stat == 0 &&
               !config.useDualCharging) {
        // Log.AddLog(LOG_NORMAL,
        //     "component=billing event=transfer_rejected peer=pb account_id=%d reason=no_charge_source client_reason=18 action=send_client_failure",
        //     uid)
        // Log.AddLog(LOG_NORMAL,
        //     "[PBS] Server transfer rejected for account %d: no billing source. Sending client failure code 18.",
        //     uid)
        L2_LOG_INFO(Log,
            "[PBS] Server transfer rejected for account %d: no billing source. Sending client failure code 18.",
            uid);
        client->Send("cddc", 12, uid, 0, 0x12);
    } else {
        client->Send(
            "cddc",
            12,
            uid,
            client->GetMd5Key(),
            0);
    }
    client->SetUserMode(UM_LOGIN);
    return true;
}

// L2Authd.exe 0x00476710, Src/pbservermgr.cpp:754-924.
bool PBServerMgr::GetValueInfoServersideLogin(
    LoginUser* user,
    int uid,
    char reason,
    char valueType,
    int remainPlayTime,
    int reservations,
    char computationBasis,
    int itemId) {
    if (reason == 0) {
        user->pbServer.valueType = valueType;
        user->pbServer.remainTime = remainPlayTime;
        user->pbServer.reservations = reservations;
        user->pbServer.computationBasis =
            computationBasis;
        user->pbServer.itemId = itemId;
    } else if (reason == 3) {
        NotifyEndCharge(uid);
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Uid(%d) is in VM, while it isn't in Auth",
        //     uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=billing event=charge_state_mismatch peer=pb operation=server_login account_id=%d peer_reason=3 client_reason=7 action=send_world_failure",
        //     uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[PBS] Billing state disagrees during world login for account %d (peer reason 3). Sending failure code 7 to the world server.",
        //     uid)
        L2_LOG_WARN(Log,
            "[PBS] Billing state disagrees during world login for account %d (peer reason 3). Sending failure code 7 to the world server.",
            uid);
        const WorldServer world =
            g_ServerList.GetAt(user->serverid);
        SendSocket(
            world.inner_addr,
            "cdc",
            0x15,
            uid,
            7);
        return false;
    }

    user->chargeServerId =
        accountdb.DetermineChargeServer(user);

    // This raw comparison is present in the selected image: stat is compared
    // with valueType itself, not with valueType*100+1000.
    if (user->stat ==
        static_cast<int>(user->ipServer.valueType)) {
        const char result =
            ipsessionDB.AcquireSessionRequest(
                user,
                uid,
                0x1c);
        if (result == 0) {
            return true;
        }
        // Log.AddLog(LOG_WARN,
        //     "component=billing event=ip_charge_request_failed peer=ip_server operation=server_login account_id=%d reason_code=%d action=send_world_failure_then_continue",
        //     uid, static_cast<int>(result))
        // Log.AddLog(LOG_WARN,
        //     "[PBS] IP billing request failed during world login for account %d (reason %d). Sending failure to the world server. The login path then continues.",
        //     uid, static_cast<int>(result))
        // Original: L2_LOG_WARN(Log,
        //     "[PBS] IP billing request failed during world login for account %d (reason %d). Sending failure to the world server. The login path then continues.",
        //     uid, static_cast<int>(result))
        L2_LOG_WARN(Log,
            "[PBS] IP billing request failed during world login for account %d (reason %d). Attempting to send failure code %d to world server %d; login processing then continues.",
            uid, static_cast<int>(result), result == 11 ? 7 : 0x14,
            static_cast<int>(user->serverid));
        const WorldServer world =
            g_ServerList.GetAt(user->serverid);
        SendSocket(
            world.inner_addr,
            "cdc",
            0x15,
            uid,
            result == 11 ? 7 : 0x14);
        // The shipping function continues after this error response.
    }

    if (user->stat != valueType * 100 + 1000) {
        // Log.AddLog(LOG_NORMAL,
        //     "component=billing event=login_rejected peer=pb operation=server_login account_id=%d reason=charge_status_mismatch client_reason=44 action=send_world_failure",
        //     uid)
        // Log.AddLog(LOG_NORMAL,
        //     "[PBS] World login rejected for account %d: billing status does not match. Sending failure code 44 to the world server.",
        //     uid)
        // Original: L2_LOG_INFO(Log,
        //     "[PBS] World login rejected for account %d: billing status does not match. Sending failure code 44 to the world server.",
        //     uid)
        L2_LOG_INFO(Log,
            "[PBS] World login rejected for account %d: billing status %d does not match expected %d for value type %d. Attempting to send failure code 44 to world server %d.",
            uid, static_cast<int>(user->stat), valueType * 100 + 1000,
            static_cast<int>(valueType), static_cast<int>(user->serverid));
        const WorldServer world =
            g_ServerList.GetAt(user->serverid);
        SendSocket(
            world.inner_addr,
            "cdc",
            0x15,
            uid,
            0x2c);
        return false;
    }
    if (user->chargeServerId == 1) {
        // Log.AddLog(LOG_NORMAL,
        //     "component=billing event=login_rejected peer=pb operation=server_login account_id=%d reason=ip_charge_selected client_reason=44 action=send_world_failure",
        //     uid)
        // Log.AddLog(LOG_NORMAL,
        //     "[PBS] World login rejected for account %d: IP billing was selected. Sending failure code 44 to the world server.",
        //     uid)
        L2_LOG_INFO(Log,
            "[PBS] World login rejected for account %d: IP billing was selected. Sending failure code 44 to the world server.",
            uid);
        const WorldServer world =
            g_ServerList.GetAt(user->serverid);
        SendSocket(
            world.inner_addr,
            "cdc",
            0x15,
            uid,
            0x2c);
        return false;
    }

    user->logintime = runtime::crt::Time32Now();
    const char registrationResult =
        accountdb.RegAccount(uid, user);
    if (registrationResult != 0) {
        // Log.AddLog(LOG_NORMAL,
        //     "component=billing event=login_rejected peer=pb operation=server_login account_id=%d phase=account_registration reason_code=%d action=send_world_failure",
        //     uid, static_cast<int>(registrationResult))
        // Log.AddLog(LOG_NORMAL,
        //     "[PBS] Account registration failed during world login for account %d (reason %d). Sending failure to the world server.",
        //     uid, static_cast<int>(registrationResult))
        // L2_LOG_INFO(Log,
        //     "[PBS] Account registration failed during world login for account %d (reason %d). Sending failure to the world server.",
        //     uid, static_cast<int>(registrationResult))
        L2_LOG_INFO(Log,
            "[PBS] Another login is already registered for account %d (reason %d). Rejecting world login and sending failure to the world server.",
            uid, static_cast<int>(registrationResult));
        const WorldServer world =
            g_ServerList.GetAt(user->serverid);
        SendSocket(
            world.inner_addr,
            "cdc",
            0x15,
            uid,
            registrationResult);
        return false;
    }
    if (!accountdb.recordGamePlayTime(
            uid,
            user->serverid)) {
        // Log.AddLog(LOG_WARN,
        //     "component=billing event=play_time_record_failed peer=pb operation=server_login account_id=%d client_reason=7 action=send_world_failure_and_logout",
        //     uid)
        // Log.AddLog(LOG_WARN,
        //     "[PBS] Could not record play time during world login for account %d. Sending failure code 7 to the world server and logging out.",
        //     uid)
        L2_LOG_WARN(Log,
            "[PBS] Could not record play time during world login for account %d. Sending failure code 7 to the world server and logging out.",
            uid);
        const WorldServer world =
            g_ServerList.GetAt(user->serverid);
        SendSocket(
            world.inner_addr,
            "cdc",
            0x15,
            uid,
            7);
        accountdb.LogoutProcess(uid, 0);
        return false;
    }

    int fatigueRestTime = 0;
    int fatiguePlayTime = 0;
    int fatigueSecSinceLastLogout = 0;
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
            &fatigueSecSinceLastLogout);
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
            remainPlayTime,
            user->loginflag,
            0,
            fatigueRestTime,
            fatiguePlayTime,
            fatigueSecSinceLastLogout,
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
            remainPlayTime,
            user->loginflag,
            0,
            fatigueRestTime,
            fatiguePlayTime,
            fatigueSecSinceLastLogout,
            user->packageVersionInfo,
            zoneFatigueInfoLength,
            zoneFatigueInfoLength,
            zoneFatigueInfo,
            chargedItemId);
    }
    if (forwarded == 0) {
        L2_LOG_WARN(Log,
            "[PBS] World login reply for account %d was not forwarded to server %u: no connection exists for its internal address. Local account registration is retained; this handler still returns success.",
            uid, static_cast<unsigned int>(user->serverid));
    }
    return true;
}

// L2Authd.exe 0x00476E70, Src/pbservermgr.cpp:936-1183.
bool PBServerMgr::GetValueInfoAboutToPlay(
    LoginUser* user,
    CClientSession* client,
    int uid,
    char reason,
    char valueType,
    int remainPlayTime,
    int reservations,
    char computationBasis,
    int itemId) {
    if (reason == 3) {
        NotifyEndCharge(uid);
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Uid(%d) is in VM, while it isn't in Auth",
        //     uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=billing event=charge_state_mismatch peer=pb operation=enter_world account_id=%d peer_reason=3 client_reason=7 action=reject_and_logout",
        //     uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[PBS] Billing state disagrees during world entry for account %d (peer reason 3). Rejecting with code 7 and logging out.",
        //     uid)
        L2_LOG_WARN(Log,
            "[PBS] Billing state disagrees during world entry for account %d (peer reason 3). Rejecting with code 7 and logging out.",
            uid);
        client->Send("cc", 6, 7);
        accountdb.LogoutProcess(uid, 0);
        return false;
    }
    if (reason != 0) {
        valueType = 0;
        remainPlayTime = 0;
        reservations = 0;
        computationBasis = 0;
        itemId = 0;
    }
    user->pbServer.valueType = valueType;
    user->pbServer.remainTime = remainPlayTime;
    user->pbServer.reservations = reservations;
    user->pbServer.computationBasis =
        computationBasis;
    user->pbServer.itemId = itemId;

    if (!accountdb.UpdateChargeServerData(
            uid,
            2,
            valueType,
            remainPlayTime,
            reservations,
            computationBasis,
            itemId)) {
        // Log.AddLog(LOG_WARN,
        //     "component=billing event=charge_update_failed peer=pb operation=enter_world account_id=%d phase=server_charge_data reason=account_not_found client_reason=10 action=reject_and_logout",
        //     uid)
        // Log.AddLog(LOG_WARN,
        //     "[PBS] Could not update server billing data during world entry for account %d: account not found. Rejecting with code 10 and logging out.",
        //     uid)
        // Original: L2_LOG_WARN(Log,
        //     "[PBS] Could not update server billing data during world entry for account %d: account not found. Rejecting with code 10 and logging out.",
        //     uid)
        L2_LOG_WARN(Log,
            "[PBS] Cannot update billing data during world entry for account %d: the account is absent from AuthD's in-memory registry. Attempting failure code 10 and logout.",
            uid);
        client->Send("cc", 6, 10);
        accountdb.LogoutProcess(uid, 0);
        return false;
    }

    user->chargeServerId =
        accountdb.DetermineChargeServer(user);
    if (user->chargeServerId == 0) {
        if (config.FreeServer == 3) {
            user->stat = 2000;
        } else if (!config.useDualCharging) {
            // Log.AddLog(LOG_NORMAL,
            //     "component=billing event=entry_rejected peer=pb account_id=%d reason=no_charge_source client_reason=18 action=reject_and_logout",
            //     uid)
            // Log.AddLog(LOG_NORMAL,
            //     "[PBS] World entry rejected for account %d: no billing source. Sending failure code 18 and logging out.",
            //     uid)
            L2_LOG_INFO(Log,
                "[PBS] World entry rejected for account %d: no billing source. Sending failure code 18 and logging out.",
                uid);
            client->Send("cc", 6, 0x12);
            accountdb.LogoutProcess(uid, 0);
            return false;
        } else {
            user->stat = 0;
        }
    } else if (user->chargeServerId == 1) {
        const char ipResult =
            ipsessionDB.RequestIPCharge(
                uid,
                user,
                2);
        if (ipResult == 11) {
            // Log.AddLog(LOG_NORMAL,
            //     "component=billing event=entry_rejected peer=pb account_id=%d server_id=%d reason=ip_charge_rejected ip_reason=11 client_reason=21 action=reject_and_logout",
            //     uid, static_cast<int>(user->selectedServerid))
            // Log.AddLog(LOG_NORMAL,
            //     "[PBS] World entry rejected for account %d on server %d: IP billing rejected the request with reason 11. Sending failure code 21 and logging out.",
            //     uid, static_cast<int>(user->selectedServerid))
            // L2_LOG_INFO(Log,
            //     "[PBS] World entry rejected for account %d on server %d: IP billing rejected the request with reason 11. Sending failure code 21 and logging out.",
            //     uid, static_cast<int>(user->selectedServerid))
            L2_LOG_INFO(Log,
                "[PBS] World entry rejected for account %d on server %d: local IP billing request returned code 11. Sending client failure code 21 and processing logout.",
                uid, static_cast<int>(user->selectedServerid));
            client->Send("cc", 6, 0x15);
            accountdb.LogoutProcess(uid, 0);
            return false;
        }
    } else if (user->chargeServerId == 2) {
        user->stat = valueType * 100 + 1000;
        if (config.FreeServer != 3) {
            g_ServerList.MapForbiddenServers(
                user->pbServer.availableServers,
                user->pbServer.availableServersSize,
                user->forbidden_servers,
                16);
            g_ServerList.MapForbiddenServers(
                user->age,
                user->forbidden_servers,
                16);
        }
    }

    if (!accountdb.UpdateChargeData(
            uid,
            user->chargeServerId,
            0,
            UM_LOGIN)) {
        // Log.AddLog(LOG_WARN,
        //     "component=billing event=charge_update_failed peer=pb operation=enter_world account_id=%d phase=selected_charge_data reason=account_not_found client_reason=10 action=reject_and_logout",
        //     uid)
        // Log.AddLog(LOG_WARN,
        //     "[PBS] Could not update the selected billing source during world entry for account %d: account not found. Rejecting with code 10 and logging out.",
        //     uid)
        // Original: L2_LOG_WARN(Log,
        //     "[PBS] Could not update the selected billing source during world entry for account %d: account not found. Rejecting with code 10 and logging out.",
        //     uid)
        L2_LOG_WARN(Log,
            "[PBS] Cannot select billing source %d during world entry for account %d: the account is absent from AuthD's in-memory registry. Attempting failure code 10 and logout.",
            static_cast<int>(user->chargeServerId), uid);
        client->Send("cc", 6, 10);
        accountdb.LogoutProcess(uid, 0);
        return false;
    }
    if (!accountdb.IsPermittedServer(
            user->selectedServerid,
            user->forbidden_servers,
            16)) {
        if (CIPSessionDB::IsIPCharging(user->stat)) {
            const int sessionId =
                ipsessionDB.DelSessionID(uid);
            if (sessionId != 0) {
                ipsessionDB.ReleaseSessionRequest(
                    sessionId,
                    user->loginIp,
                    user->stat);
            }
        }
        if (user->age < g_ServerList.GetAgeLimit(
                            user->selectedServerid)) {
            // Log.AddLog(LOG_NORMAL,
            //     "component=billing event=entry_rejected peer=pb account_id=%d server_id=%d reason=server_age_limit client_reason=12 action=reject_and_logout",
            //     uid, static_cast<int>(user->selectedServerid))
            // Log.AddLog(LOG_NORMAL,
            //     "[PBS] World entry rejected for account %d on server %d: server age limit. Sending failure code 12 and logging out.",
            //     uid, static_cast<int>(user->selectedServerid))
            L2_LOG_INFO(Log,
                "[PBS] World entry rejected for account %d on server %d: server age limit. Sending failure code 12 and logging out.",
                uid, static_cast<int>(user->selectedServerid));
            client->Send("cc", 6, 12);
        } else {
            // Log.AddLog(LOG_NORMAL,
            //     "component=billing event=entry_rejected peer=pb account_id=%d server_id=%d reason=server_not_permitted client_reason=33 action=reject_and_logout",
            //     uid, static_cast<int>(user->selectedServerid))
            // Log.AddLog(LOG_NORMAL,
            //     "[PBS] World entry rejected for account %d on server %d: account is not permitted on this server. Sending failure code 33 and logging out.",
            //     uid, static_cast<int>(user->selectedServerid))
            L2_LOG_INFO(Log,
                "[PBS] World entry rejected for account %d on server %d: account is not permitted on this server. Sending failure code 33 and logging out.",
                uid, static_cast<int>(user->selectedServerid));
            client->Send("cc", 6, 33);
        }
        accountdb.LogoutProcess(uid, 0);
        return false;
    }

    const bool revivalServer =
        g_ServerList.IsRevivalServer(
            user->selectedServerid);
    if (((user->loginflag & 0x800U) == 0U &&
         revivalServer) ||
        ((user->loginflag & 0x800U) != 0U &&
         !revivalServer)) {
        // Log.AddLog(LOG_NORMAL,
        //     "component=billing event=entry_rejected peer=pb account_id=%d server_id=%d reason=revival_server_mismatch client_reason=200 action=reject_and_logout",
        //     uid, static_cast<int>(user->selectedServerid))
        // Log.AddLog(LOG_NORMAL,
        //     "[PBS] World entry rejected for account %d on server %d: revival server does not match. Sending failure code 200 and logging out.",
        //     uid, static_cast<int>(user->selectedServerid))
        L2_LOG_INFO(Log,
            "[PBS] World entry rejected for account %d on server %d: revival server does not match. Sending failure code 200 and logging out.",
            uid, static_cast<int>(user->selectedServerid));
        client->Send("cc", 6, 200);
        accountdb.LogoutProcess(uid, 0);
        return false;
    }
    if (config.limitIPNumforEachGS > 0 &&
        ConnectedIPList::Inst()->CheckIPOverConnection(
            user->selectedServerid,
            user->loginIp,
            uid)) {
        // Log.AddLog(LOG_NORMAL,
        //     "component=billing event=entry_rejected peer=pb account_id=%d server_id=%d reason=ip_connection_limit client_reason=76 action=reject_and_logout",
        //     uid, static_cast<int>(user->selectedServerid))
        // Log.AddLog(LOG_NORMAL,
        //     "[PBS] World entry rejected for account %d on server %d: IP connection limit reached. Sending failure code 76 and logging out.",
        //     uid, static_cast<int>(user->selectedServerid))
        L2_LOG_INFO(Log,
            "[PBS] World entry rejected for account %d on server %d: IP connection limit reached. Sending failure code 76 and logging out.",
            uid, static_cast<int>(user->selectedServerid));
        client->Send("cc", 6, 76);
        accountdb.LogoutProcess(uid, 0);
        return false;
    }

    if (config.FreeServer == 3) {
        const WorldServer world =
            GetWorldServer(user->selectedServerid);
        if (world.serverid < 1) {
            // Log.AddLog(LOG_NORMAL,
            //     "component=billing event=entry_rejected peer=pb account_id=%d server_id=%d reason=server_not_found client_reason=8 action=reject_and_logout",
            //     uid, static_cast<int>(user->selectedServerid))
            // Log.AddLog(LOG_NORMAL,
            //     "[PBS] World entry rejected for account %d on server %d: server not found. Sending failure code 8 and logging out.",
            //     uid, static_cast<int>(user->selectedServerid))
            L2_LOG_INFO(Log,
                "[PBS] World entry rejected for account %d on server %d: server not found. Sending failure code 8 and logging out.",
                uid, static_cast<int>(user->selectedServerid));
            client->Send("cc", 6, 8);
            accountdb.LogoutProcess(uid, 0);
            return false;
        }
        if ((world.serverKind & 0x40U) != 0U) {
            client->AddRef();
            user->chargeServerId = 0;
            user->stat = 2000;
            accountdb.AboutToPlay(
                client,
                uid,
                user,
                0,
                user->selectedServerid,
                user->stat,
                0);
            client->ReleaseRef();
            return false;
        }
        if (user->chargeServerId == 0 &&
            user->stat == 2000) {
            // Log.AddLog(LOG_NORMAL,
            //     "component=billing event=entry_rejected peer=pb account_id=%d reason=free_server_requires_subscription client_reason=18 action=reject_and_logout",
            //     uid)
            // Log.AddLog(LOG_NORMAL,
            //     "[PBS] World entry rejected for account %d: a subscription is required for this free server. Sending failure code 18 and logging out.",
            //     uid)
            L2_LOG_INFO(Log,
                "[PBS] World entry rejected for account %d: a subscription is required for this free server. Sending failure code 18 and logging out.",
                uid);
            client->Send("cc", 6, 0x12);
            accountdb.LogoutProcess(uid, 0);
            return false;
        }
    }

    const int payStatKind =
        (user->stat % 1000) / 100;
    if (CIPSessionDB::IsIPCharging(user->stat) &&
        config.UseIPServer) {
        const char result = ipsessionDB.ReadyToIPCharge(
            static_cast<unsigned int>(uid),
            user->loginIp.s_addr,
            user->stat,
            user->selectedServerid);
        if (result == 10) {
            // Log.AddLog(LOG_WARN,
            //     "component=billing event=entry_rejected peer=pb account_id=%d server_id=%d reason=ip_charge_request_failed client_reason=20 action=reject_and_logout",
            //     uid, static_cast<int>(user->selectedServerid))
            // Log.AddLog(LOG_WARN,
            //     "[PBS] Could not request IP billing for account %d on server %d. Rejecting world entry with code 20 and logging out.",
            //     uid, static_cast<int>(user->selectedServerid))
            L2_LOG_WARN(Log,
                "[PBS] Could not request IP billing for account %d on server %d. Rejecting world entry with code 20 and logging out.",
                uid, static_cast<int>(user->selectedServerid));
            client->Send("cc", 6, 0x14);
            accountdb.LogoutProcess(uid, 0);
        }
        return false;
    }

    if (!config.usePBServer ||
        user->chargeServerId != 2) {
        int remainingTime = 0;
        char result = 0;
        if (payStatKind != 0) {
            if (payStatKind == 2 || payStatKind == 5) {
                result = accountdb.CheckUserTime(
                    uid,
                    &remainingTime);
            } else if (payStatKind == 1 &&
                       config.Country == 0) {
                result = accountdb.CheckUserDuration(
                    uid,
                    &remainingTime);
            }
        }
        if (result == 0) {
            client->AddRef();
            accountdb.AboutToPlay(
                client,
                uid,
                user,
                remainingTime,
                user->selectedServerid,
                user->stat,
                0);
            client->ReleaseRef();
            // This extra user release is present before the caller releases
            // its PBSUser carrier. Preserve the selected ownership path.
            user->ReleaseRef();
            return true;
        }
        // Log.AddLog(LOG_NORMAL,
        //     "component=billing event=entry_rejected peer=pb account_id=%d phase=local_time_check reason_code=%d action=reject_and_logout",
        //     uid, static_cast<int>(result))
        // Log.AddLog(LOG_NORMAL,
        //     "[PBS] Local time check rejected world entry for account %d (reason %d). Sending failure and logging out.",
        //     uid, static_cast<int>(result))
        L2_LOG_INFO(Log,
            "[PBS] Local time check rejected world entry for account %d (reason %d). Sending failure and logging out.",
            uid, static_cast<int>(result));
        client->Send("cc", 6, result);
        accountdb.LogoutProcess(uid, 0);
        return false;
    }

    client->AddRef();
    accountdb.AboutToPlay(
        client,
        uid,
        user,
        user->pbServer.remainTime,
        user->selectedServerid,
        user->stat,
        user->pbServer.reservations);
    client->ReleaseRef();
    return false;
}

// L2Authd.exe 0x00477740, Src/pbservermgr.cpp:1320-1339.
bool PBServerMgr::RecvValueAbandon(
    int uid,
    char reason) {
    PBSUser* waitUser = nullptr;
    if (!DelUserWait(uid, &waitUser)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "User infomation was deleted while getting payment info from PBS(%d)",
        //     uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=billing event=reply_without_pending_request peer=pb operation=abandon_value account_id=%d action=ignore_reply",
        //     uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[PBS] Ignoring billing value release reply for account %d: no request is pending.",
        //     uid)
        // Original: L2_LOG_WARN(Log,
        //     "[PBS] Ignoring billing value release reply for account %d: no request is pending.",
        //     uid)
        L2_LOG_WARN(Log,
            "[PBS] Ignoring billing value release reply for account %d (peer reason %u): no request is pending.",
            uid, static_cast<unsigned int>(static_cast<unsigned char>(reason)));
        return false;
    }
    if (reason != 0) {
        // Log.AddLog(LOG_NORMAL,
        //     "component=billing event=value_abandon_rejected peer=pb account_id=%d peer_reason=%u action=send_world_result",
        //     uid, static_cast<unsigned int>(static_cast<unsigned char>(reason)))
        // Log.AddLog(LOG_NORMAL,
        //     "[PBS] Billing value release was rejected for account %d (peer reason %u). Sending the result to the world server.",
        //     uid, static_cast<unsigned int>(static_cast<unsigned char>(reason)))
        L2_LOG_INFO(Log,
            "[PBS] Billing value release was rejected for account %d (peer reason %u). Sending the result to the world server.",
            uid, static_cast<unsigned int>(static_cast<unsigned char>(reason)));
    }
    const WorldServer world = g_ServerList.GetAt(
        waitUser->pLoginUser->serverid);
    const int forwarded = SendSocket(
        world.inner_addr,
        "cdc",
        0x11,
        uid,
        MapVMToAuthReason(reason));
    if (forwarded == 0) {
        L2_LOG_WARN(Log,
            "[PBS] Billing value release reply for account %d (peer reason %u) was not forwarded to world server %u: no connection exists for its internal address. Releasing the pending request without a world reply.",
            uid, static_cast<unsigned int>(static_cast<unsigned char>(reason)),
            static_cast<unsigned int>(waitUser->pLoginUser->serverid));
    }
    waitUser->ReleaseRef();
    return true;
}

// L2Authd.exe 0x00477860, Src/pbservermgr.cpp:1247-1269.
bool PBServerMgr::RecvNotifyRemainTime(
    int uid,
    int remainTime,
    int reservations,
    char computationBasis) {
    LoginUser* user = nullptr;
    if (!accountdb.FindAccount(uid, &user)) {
        return false;
    }
    const char serverId = user->serverid;
    user->ReleaseRef();
    const WorldServer world =
        g_ServerList.GetAt(serverId);
    SendSocket(
        world.inner_addr,
        "cdddc",
        0x0b,
        uid,
        remainTime,
        reservations,
        computationBasis);
    return true;
}

// L2Authd.exe 0x00477970, Src/pbservermgr.cpp:1281-1313.
bool PBServerMgr::RecvNotifyNewItemApplied(
    int uid,
    int remainTime,
    int reservations,
    char valueType,
    unsigned char* valueId,
    char computationBasis,
    int itemId) {
    LoginUser* user = nullptr;
    if (!accountdb.FindAccount(uid, &user)) {
        return false;
    }
    accountdb.WriteBillingData(uid, ACTION_STOP, *user);
    accountdb.SetPBData(
        uid,
        valueType,
        remainTime,
        reservations,
        valueId,
        computationBasis,
        itemId);
    if (std::strcmp(user->loginCafe, "0000000") != 0) {
        // Log.AddLog(LOG_NORMAL,
        //     "component=billing event=disconnect_requested peer=pb operation=apply_new_item account_id=%d reason=billing_item_changed client_reason=34 action=request_disconnect",
        //     uid)
        // Log.AddLog(LOG_NORMAL,
        //     "[PBS] Billing item changed for account %d. Requesting disconnection with client reason 34.",
        //     uid)
        L2_LOG_INFO(Log,
            "[PBS] Billing item changed for account %d. Requesting disconnection with client reason 34.",
            uid);
        accountdb.KickAccount(uid, 0x22, true, true);
        user->ReleaseRef();
        return true;
    }

    const WorldServer world =
        g_ServerList.GetAt(user->serverid);
    SendSocket(
        world.inner_addr,
        "cdddcd",
        0x0c,
        uid,
        remainTime,
        reservations,
        valueType,
        itemId);
    user->ReleaseRef();
    return true;
}

// L2Authd.exe 0x00477AF0, Src/pbservermgr.cpp:1343-1351.
bool PBServerMgr::RecvStartChargeList(
    const unsigned char* packet) {
    try {
        [[maybe_unused]] const int result =
            GetIntFromPacket(&packet);
        // Log.AddLog(LOG_NORMAL,
        //     "component=billing event=resync_reply peer=pb remote_result=%d action=continue_with_next_batch",
        //     result)
        // Log.AddLog(LOG_NORMAL,
        //     "[PBS] Billing resynchronization reply received (result %d). Continuing with the next batch.",
        //     result)
        // L2_LOG_INFO(Log,
        //     "[PBS] Billing resynchronization reply received (result %d). Continuing with the next batch.",
        //     result)
        L2_LOG_INFO(Log,
            "[PBS] Billing resynchronization reply received (result %d). This handler does not evaluate the result; checking for remaining accounts to submit.",
            result);
        accountdb.ReConnectPBServer();
        return true;
    } catch (...) {
        // ELOG(
        // "*%s:%d(%s) exception\n",
        // "PBServerMgr.cpp",
        // 1349,
        // "Wed Oct 23 17:02:11 2019")
        ELOG(
            "[PBServerMgr.cpp] Exception while processing the PBS billing resynchronization reply and advancing to the next batch. Entering exception handling.\n");
        send_exception(true);
        return true;
    }
}

// L2Authd.exe 0x00477BB0, Src/pbservermgr.cpp:1358-1385.
bool PBServerMgr::RecvChargeSuspend(
    int uid,
    char reason) {
    char timeTag[64]{};
    GetTimeTag(timeTag, 64);
    PBSUser* waitUser = nullptr;
    if (!DelUserWait(uid, &waitUser)) {
        // The selected push is 0x004FECCD: the final two characters inside
        // its nearby "0000000" literal, hence the exact value is "00".
        fileLog.AddLog(
            LOG_NORMAL,
            "%s, %14d, %14s, %03d, 1, %d, %d, 1, 1, 1, %d, 0 \r\n",
            timeTag,
            uid,
            "00",
            -1,
            -1,
            -1,
            reason);
        // Log.AddLog(
        //     LOG_ERROR,
        //     "User infomation was deleted while getting charge suspend info from PBS(%d)",
        //     uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=billing event=reply_without_pending_request peer=pb operation=suspend_charge account_id=%d action=record_unmatched_billing_reply",
        //     uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[PBS] Charge suspension reply for account %d has no pending request. Recording the unmatched reply in the billing log.",
        //     uid)
        // Original: L2_LOG_WARN(Log,
        //     "[PBS] Charge suspension reply for account %d has no pending request. Recording the unmatched reply in the billing log.",
        //     uid)
        L2_LOG_WARN(Log,
            "[PBS] Charge suspension reply for account %d has no pending request (peer reason %u). The unmatched reply was submitted to the billing log; processing returns failure.",
            uid, static_cast<unsigned int>(static_cast<unsigned char>(reason)));
        return false;
    }

    fileLog.AddLog(
        LOG_NORMAL,
        "%s, %14d, %14s, %03d, 1, %d, %d, 1, 1, 1, %d, 1 \r\n",
        timeTag,
        uid,
        waitUser->mBanUser.gameAccount,
        waitUser->mBanUser.blockReason,
        waitUser->mBanUser.result,
        waitUser->mBanUser.restrictionNo,
        reason);
    // Log.AddLog(LOG_NORMAL,
    //     "component=billing event=charge_suspend_reply peer=pb account_id=%d peer_reason=%u action=record_billing_reply",
    //     uid, static_cast<unsigned int>(static_cast<unsigned char>(reason)))
    // Log.AddLog(LOG_NORMAL,
    //     "[PBS] Charge suspension reply received for account %d (peer reason %u). Recording the billing result.",
    //     uid, static_cast<unsigned int>(static_cast<unsigned char>(reason)))
    L2_LOG_INFO(Log,
        "[PBS] Charge suspension reply received for account %d (peer reason %u). Recording the billing result.",
        uid, static_cast<unsigned int>(static_cast<unsigned char>(reason)));
    waitUser->ReleaseRef();
    return true;
}
