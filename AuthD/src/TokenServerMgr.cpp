#include "TokenServerMgr.h"

#include "Shared/src/ScopedLockGuards.h"

#include "Platform/IOSocketTransport.h"
#include "Platform/LoginUserImage.h"
#include "Shared/src/Runtime/NativeSocketError.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/TimerQueue.h"
#include "Config.h"
#include "Exception.h"
#include "IOServer.h"
#include "Log.h"
#include "Protocol.h"
#include "Thread.h"
#include "TokenSocket.h"
#include "Util.h"

#include <cstdint>
#include <cstring>
#include <ctime>

TokenServerMgr* TokenServerMgr::sInstance = nullptr;

namespace {

struct TokenConnection {
    std::uint32_t socket = UINT32_C(0xffffffff);
    bool connected = false;
};

TokenConnection ConnectTokenServer() {
    TokenConnection connection;
    const std::uint32_t peerAddress = ntohl(config.lightTokenServerIPAddress.s_addr);
    connection.connected =
        authd::platform::ConnectNativeSocket(
            connection.socket,
            config.lightTokenServerIPAddress,
            static_cast<std::uint16_t>(
                config.lightTokenServerPort));
    if (!connection.connected) {
        // Log.AddLog(LOG_WARN,
        //     "component=network event=connect_failed peer=token address=%u.%u.%u.%u port=%u socket=%u error_domain=winsock_compatible code=%d action=close_socket reconnect=schedule_pending",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.lightTokenServerPort)),
        //     connection.socket, ::portable::GetNativeSocketLastError())
        // Log.AddLog(LOG_WARN,
        //     "[Token] Could not connect to %u.%u.%u.%u:%u on socket %u (WinSock-compatible error %d). Closing the socket. Requesting another retry.",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.lightTokenServerPort)),
        //     connection.socket, ::portable::GetNativeSocketLastError())
        // L2_LOG_WARN(Log,
        //     "[Token] Could not connect to %u.%u.%u.%u:%u on socket %u (WinSock-compatible error %d). Closing the socket. Requesting another retry.",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.lightTokenServerPort)),
        //     connection.socket, ::portable::GetNativeSocketLastError())
        L2_LOG_WARN(Log,
            "[Token] Could not connect to %u.%u.%u.%u:%u on socket %u (WinSock-compatible error %d). %s",
            (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            (peerAddress >> 8U) & 255U, peerAddress & 255U,
            static_cast<unsigned int>(static_cast<std::uint16_t>(config.lightTokenServerPort)),
            connection.socket, ::portable::GetNativeSocketLastError(),
            connection.socket == UINT32_C(0xffffffff)
                ? "No valid socket was created; the close path will not schedule a retry."
                : "Closing the socket; its close callback will request a retry.");
    }
    return connection;
}

} // namespace

// L2Authd.exe 0x0048B820, Src/tokenservermgr.cpp:13-22.
TokenTrans* TokenTrans::Alloc() {
    auto* const transaction = new TokenTrans{};
    transaction->AddRef();
    return transaction;
}

// L2Authd.exe 0x0048B900, Src/tokenservermgr.cpp:26-31.
void TokenTrans::AddRef() {
    if (pServerSocket != nullptr) {
        pServerSocket->AddRef();
    }
    runtime::raw_atomic::IncrementI32(
        &mRefCount);
}

// L2Authd.exe 0x0048B940, Src/tokenservermgr.cpp:35-41.
void TokenTrans::ReleaseRef() {
    if (pServerSocket != nullptr) {
        pServerSocket->ReleaseRef();
    }
    if (runtime::raw_atomic::DecrementI32(
            &mRefCount) == 0) {
        ::operator delete(this);
    }
}

// L2Authd.exe 0x0048B990, Src/tokenservermgr.cpp:52-60.
TokenServerMgr::TokenServerMgr()
    : mTransMapLock(eCriticalSection, 0) {
    if (sInstance == nullptr) {
        sInstance = this;
        mTokenServerConnected = false;
        mpTokenSocket = nullptr;
        mhTokenServerTimer = nullptr;
        mTransId = 0;
    }
}

// L2Authd.exe 0x0048BA40, Src/tokenservermgr.cpp:64-66.
TokenServerMgr::~TokenServerMgr() {
    sInstance = nullptr;
}

// L2Authd.exe 0x0048BAB0, Src/tokenservermgr.cpp:106-119.
void TokenServerMgr::Initialize() {
    const TokenConnection connection =
        ConnectTokenServer();
    mpTokenSocket = TokenSocket::Allocate(
        connection.socket);
    if (!connection.connected) {
        mpTokenSocket->CloseSocket();
    } else {
        authd::platform::InitializeNativeSocket(
            mpTokenSocket,
            connection.socket,
            g_hIOCompletionPort);
    }
}

// L2Authd.exe 0x0048BB50, Src/tokenservermgr.cpp:70-102.
void TokenServerMgr::TokenServerTimerRoutine() {
    if (mhTokenServerTimer != nullptr) {
        runtime::timer_queue::DeleteTimer(
            nullptr,
            mhTokenServerTimer);
        mhTokenServerTimer = nullptr;
    }
    if (mTokenServerConnected) {
        return;
    }

    const TokenConnection connection =
        ConnectTokenServer();
    TokenSocket* const tempSocket =
        TokenSocket::Allocate(connection.socket);
    if (!connection.connected) {
        tempSocket->CloseSocket();
        tempSocket->ReleaseRef();
        return;
    }

    TokenSocket* const swapSocket = mpTokenSocket;
    authd::platform::InitializeNativeSocket(
        tempSocket,
        connection.socket,
        g_hIOCompletionPort);
    runtime::raw_atomic::ExchangePointer(
        &mpTokenSocket,
        tempSocket);
    swapSocket->ReleaseRef();
    mTokenServerConnected = true;
}

// L2Authd.exe 0x0048BC50, Src/tokenservermgr.cpp:180-199.
bool TokenServerMgr::AddTokenTrans(
    int tid,
    TokenTrans* transaction) {
    transaction->AddRef();
    transaction->opTime = static_cast<std::int32_t>(
        std::time(nullptr));
    bool result = false;
    {
        L2_GUARD(mTransMapLock);
        result = mTokenTransMap.insert(
            {tid, transaction}).second;
    }
    if (!result) {
        transaction->ReleaseRef();
    }
    return result;
}

// L2Authd.exe 0x0048BCD0, Src/tokenservermgr.cpp:150-176.
bool TokenServerMgr::DelTokenTrans(
    int tid,
    TokenTrans** transaction) {
    TokenTrans* oldTransaction = nullptr;
    {
        L2_GUARD(mTransMapLock);
        const auto it = mTokenTransMap.find(tid);
        if (it == mTokenTransMap.end()) {
            return false;
        }

        oldTransaction = it->second;
        mTokenTransMap.erase(it);
    }
    if (transaction == nullptr) {
        oldTransaction->ReleaseRef();
    } else {
        *transaction = oldTransaction;
    }
    return true;
}

// L2Authd.exe 0x0048BD90, Src/tokenservermgr.cpp:123-146.
bool TokenServerMgr::FindTokenTrans(
    int tid,
    TokenTrans** transaction) {
    TokenTrans* found = nullptr;
    {
        L2_GUARD(mTransMapLock);
        const auto it = mTokenTransMap.find(tid);
        if (it == mTokenTransMap.end()) {
            return false;
        }

        found = it->second;
        found->AddRef();
    }
    *transaction = found;
    return true;
}

// L2Authd.exe 0x0048BE30, Src/tokenservermgr.cpp:203-245.
char TokenServerMgr::RequestGetToken(
    CSocketServer* socket,
    LoginUser* user,
    const unsigned char* token) {
    try {
        TokenTrans* const transaction = TokenTrans::Alloc();
        authd::portable::CopyLoginUserImage(
            transaction->LoginUser,
            *user);
        if (socket != nullptr) {
            socket->AddRef();
            transaction->pServerSocket = socket;
        }

        const std::int32_t tid =
            runtime::raw_atomic::IncrementI32(
                &mTransId);
        std::memcpy(
            transaction->uniqueId,
            token,
            sizeof(transaction->uniqueId));
        const int serviceCode = config.gameId == 8
            ? 0x20
            : config.gameId;
        if (!AddTokenTrans(tid, transaction)) {
            // Log.AddLog(
            //     LOG_ERROR,
            //     "Transaction was conflict while adding transaction for LightTokenServer(uid:%d)",
            //     transaction->LoginUser.uid)
            // Log.AddLog(
            //     LOG_WARN,
            //     "component=authentication event=request_not_submitted peer=token operation=get_token transaction=%d account_id=%d reason=transaction_conflict result_code=7 action=return_failure",
            //     tid, transaction->LoginUser.uid)
            // Log.AddLog(
            //     LOG_WARN,
            //     "[Token] Get-token request %d for account %d conflicts with a pending transaction. Returning failure code 7.",
            //     tid, transaction->LoginUser.uid)
            L2_LOG_WARN(Log,
                "[Token] Get-token request %d for account %d conflicts with a pending transaction. Returning failure code 7.",
                tid, transaction->LoginUser.uid);
            transaction->ReleaseRef();
            return 7;
        }

        if (!mpTokenSocket->Send(
                "cddddd",
                1,
                tid,
                serviceCode,
                transaction->LoginUser.uid,
                transaction->LoginUser.portalUid,
                transaction->LoginUser.nWsmLoginIp.s_addr)) {
            // Log.AddLog(
            //     LOG_ERROR,
            //     " send fail to LightTokenServer(uid:%d)",
            //     transaction->LoginUser.uid)
            // Log.AddLog(
            //     LOG_WARN,
            //     "component=authentication event=request_not_submitted peer=token operation=get_token transaction=%d account_id=%d reason=socket_send_rejected result_code=20 action=remove_pending_request",
            //     tid, transaction->LoginUser.uid)
            // Log.AddLog(
            //     LOG_WARN,
            //     "[Token] Could not send get-token request %d for account %d. Removing the pending request and returning code 20.",
            //     tid, transaction->LoginUser.uid)
            // L2_LOG_WARN(Log,
            //     "[Token] Could not send get-token request %d for account %d. Removing the pending request and returning code 20.",
            //     tid, transaction->LoginUser.uid)
            L2_LOG_WARN(Log,
                "[Token] Get-token request %d for account %d was not submitted: token-service connection is unavailable. Removing the pending request and returning code 20.",
                tid, transaction->LoginUser.uid);
            DelTokenTrans(tid, nullptr);
            transaction->ReleaseRef();
            return 0x14;
        }

        transaction->ReleaseRef();
        return 0;
    } catch (...) {
        // ELOG(
        // "*%s:%d(%s) exception\n",
        // "TokenServerMgr.cpp",
        // 243,
        // "Wed Oct 23 17:02:12 2019")
        ELOG(
            "[TokenServerMgr.cpp] Exception while requesting a token from the token service. Entering exception handling.\n");
        send_exception(true);
        return 0;
    }
}

// L2Authd.exe 0x0048C020, Src/tokenservermgr.cpp:248-289.
char TokenServerMgr::RequestCloseToken(
    CSocketServer* socket,
    LoginUser* user,
    WebSessionId& webSessionId) {
    try {
        TokenTrans* const transaction = TokenTrans::Alloc();
        authd::portable::CopyLoginUserImage(
            transaction->LoginUser,
            *user);
        if (socket != nullptr) {
            socket->AddRef();
            transaction->pServerSocket = socket;
        }

        const std::int32_t tid =
            runtime::raw_atomic::IncrementI32(
                &mTransId);
        const int serviceCode = config.gameId == 8
            ? 0x20
            : config.gameId;
        if (!AddTokenTrans(tid, transaction)) {
            // Log.AddLog(
            //     LOG_ERROR,
            //     "Transaction was conflict while adding transaction for LightTokenServer(uid:%d)",
            //     transaction->LoginUser.uid)
            // Log.AddLog(
            //     LOG_WARN,
            //     "component=authentication event=request_not_submitted peer=token operation=close_token transaction=%d account_id=%d reason=transaction_conflict result_code=7 action=return_failure",
            //     tid, transaction->LoginUser.uid)
            // Log.AddLog(
            //     LOG_WARN,
            //     "[Token] Close-token request %d for account %d conflicts with a pending transaction. Returning failure code 7.",
            //     tid, transaction->LoginUser.uid)
            L2_LOG_WARN(Log,
                "[Token] Close-token request %d for account %d conflicts with a pending transaction. Returning failure code 7.",
                tid, transaction->LoginUser.uid);
            transaction->ReleaseRef();
            return 7;
        }

        if (!mpTokenSocket->Send(
                "cddddb",
                2,
                tid,
                serviceCode,
                transaction->LoginUser.uid,
                transaction->LoginUser.portalUid,
                16,
                &webSessionId)) {
            // Log.AddLog(
            //     LOG_ERROR,
            //     " send fail to LightTokenServer(uid:%d)",
            //     transaction->LoginUser.uid)
            // Log.AddLog(
            //     LOG_WARN,
            //     "component=authentication event=request_not_submitted peer=token operation=close_token transaction=%d account_id=%d reason=socket_send_rejected result_code=20 action=remove_pending_request",
            //     tid, transaction->LoginUser.uid)
            // Log.AddLog(
            //     LOG_WARN,
            //     "[Token] Could not send close-token request %d for account %d. Removing the pending request and returning code 20.",
            //     tid, transaction->LoginUser.uid)
            // L2_LOG_WARN(Log,
            //     "[Token] Could not send close-token request %d for account %d. Removing the pending request and returning code 20.",
            //     tid, transaction->LoginUser.uid)
            L2_LOG_WARN(Log,
                "[Token] Close-token request %d for account %d was not submitted: token-service connection is unavailable. Removing the pending request and returning code 20.",
                tid, transaction->LoginUser.uid);
            DelTokenTrans(tid, nullptr);
            transaction->ReleaseRef();
            return 0x14;
        }

        transaction->ReleaseRef();
        return 0;
    } catch (...) {
        // ELOG(
        // "*%s:%d(%s) exception\n",
        // "TokenServerMgr.cpp",
        // 287,
        // "Wed Oct 23 17:02:12 2019")
        ELOG(
            "[TokenServerMgr.cpp] Exception while requesting token closure from the token service. Entering exception handling.\n");
        send_exception(true);
        return 0;
    }
}

// L2Authd.exe 0x0048C1F0, Src/tokenservermgr.cpp:293-321.
bool TokenServerMgr::RecvGetTokenAck(
    [[maybe_unused]] CIOSocket* socket,
    const unsigned char* packet) {
    const std::int32_t tid =
        GetIntFromPacket(&packet);
    const std::int32_t reason =
        GetIntFromPacket(&packet);
    unsigned char token[16];
    GetBinFromPacket(
        &packet,
        sizeof(token),
        token);

    TokenTrans* transaction = nullptr;
    if (!DelTokenTrans(tid, &transaction)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Transaction was deleted while getting LightTokenServer(%d)",
        //     tid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=reply_without_pending_request peer=token operation=get_token transaction=%d result_code=%d action=ignore_reply",
        //     tid, reason)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[Token] Ignoring get-token reply %d (result %d): no request is pending.",
        //     tid, reason)
        L2_LOG_WARN(Log,
            "[Token] Ignoring get-token reply %d (result %d): no request is pending.",
            tid, reason);
        return false;
    }

    if (reason != 0) {
        // Log.AddLog(MapTokenToAuthReason(reason) == 2 ? LOG_NORMAL : LOG_WARN,
        //     "component=authentication event=operation_rejected peer=token operation=get_token transaction=%d account_id=%d result_code=%d auth_reason=%d world_connection_present=%d action=reply_if_world_connection_present",
        //     tid, transaction->LoginUser.uid, reason, MapTokenToAuthReason(reason),
        //     transaction->pServerSocket != nullptr)
        // Log.AddLog(MapTokenToAuthReason(reason) == 2 ? LOG_NORMAL : LOG_WARN,
        //     "[Token] Get-token request %d for account %d was rejected (result %d, authentication reason %d). World connection %s. Replying only if present.",
        //     tid, transaction->LoginUser.uid, reason, MapTokenToAuthReason(reason),
        //     transaction->pServerSocket != nullptr ? "present" : "absent")
        L2_LOG(Log, MapTokenToAuthReason(reason) == 2 ? LOG_NORMAL : LOG_WARN,
            "[Token] Get-token request %d for account %d was rejected (result %d, authentication reason %d). World connection %s; replying only if present.",
            tid, transaction->LoginUser.uid, reason, MapTokenToAuthReason(reason),
            transaction->pServerSocket != nullptr ? "present" : "absent");
    }
    if (transaction->pServerSocket != nullptr) {
        transaction->pServerSocket->Send(
            "cdcbbc",
            0x1b,
            transaction->LoginUser.uid,
            MapTokenToAuthReason(reason),
            16,
            token,
            16,
            transaction->uniqueId,
            0);
    }
    // Noise: a rejected reply already reports the absent world connection above.
    // if (transaction->pServerSocket == nullptr) {
    if (reason == 0 && transaction->pServerSocket == nullptr) {
        // Log.AddLog(LOG_NORMAL,
        //     "component=authentication event=reply_not_forwarded peer=token operation=get_token transaction=%d account_id=%d result_code=%d reason=no_world_connection action=release_request_without_reply",
        //     tid, transaction->LoginUser.uid, reason)
        // Log.AddLog(LOG_NORMAL,
        //     "[Token] Get-token reply %d for account %d (result %d) cannot be forwarded: no world connection. Releasing the request without replying.",
        //     tid, transaction->LoginUser.uid, reason)
        L2_LOG_INFO(Log,
            "[Token] Get-token reply %d for account %d (result %d) cannot be forwarded: no world connection. Releasing the request without replying.",
            tid, transaction->LoginUser.uid, reason);
    }
    transaction->ReleaseRef();
    return false;
}

// L2Authd.exe 0x0048C2E0, Src/tokenservermgr.cpp:326-349.
bool TokenServerMgr::RecvCloseTokenAck(
    [[maybe_unused]] CIOSocket* socket,
    const unsigned char* packet) {
    const std::int32_t tid =
        GetIntFromPacket(&packet);
    const std::int32_t reason =
        GetIntFromPacket(&packet);
    TokenTrans* transaction = nullptr;
    if (!DelTokenTrans(tid, &transaction)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Transaction was deleted while getting LightTokenServer(%d)",
        //     tid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=reply_without_pending_request peer=token operation=close_token transaction=%d result_code=%d action=ignore_reply",
        //     tid, reason)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[Token] Ignoring close-token reply %d (result %d): no request is pending.",
        //     tid, reason)
        L2_LOG_WARN(Log,
            "[Token] Ignoring close-token reply %d (result %d): no request is pending.",
            tid, reason);
        return false;
    }

    if (reason != 0) {
        // Log.AddLog(MapTokenToAuthReason(reason) == 2 ? LOG_NORMAL : LOG_WARN,
        //     "component=authentication event=operation_rejected peer=token operation=close_token transaction=%d account_id=%d result_code=%d auth_reason=%d world_connection_present=%d action=reply_if_world_connection_present",
        //     tid, transaction->LoginUser.uid, reason, MapTokenToAuthReason(reason),
        //     transaction->pServerSocket != nullptr)
        // Log.AddLog(MapTokenToAuthReason(reason) == 2 ? LOG_NORMAL : LOG_WARN,
        //     "[Token] Close-token request %d for account %d was rejected (result %d, authentication reason %d). World connection %s. Replying only if present.",
        //     tid, transaction->LoginUser.uid, reason, MapTokenToAuthReason(reason),
        //     transaction->pServerSocket != nullptr ? "present" : "absent")
        L2_LOG(Log, MapTokenToAuthReason(reason) == 2 ? LOG_NORMAL : LOG_WARN,
            "[Token] Close-token request %d for account %d was rejected (result %d, authentication reason %d). World connection %s; replying only if present.",
            tid, transaction->LoginUser.uid, reason, MapTokenToAuthReason(reason),
            transaction->pServerSocket != nullptr ? "present" : "absent");
    }
    if (transaction->pServerSocket != nullptr) {
        transaction->pServerSocket->Send(
            "cdd",
            0x1d,
            transaction->LoginUser.uid,
            MapTokenToAuthReason(reason));
    }
    // Noise: a rejected reply already reports the absent world connection above.
    // if (transaction->pServerSocket == nullptr) {
    if (reason == 0 && transaction->pServerSocket == nullptr) {
        // Log.AddLog(LOG_NORMAL,
        //     "component=authentication event=reply_not_forwarded peer=token operation=close_token transaction=%d account_id=%d result_code=%d reason=no_world_connection action=release_request_without_reply",
        //     tid, transaction->LoginUser.uid, reason)
        // Log.AddLog(LOG_NORMAL,
        //     "[Token] Close-token reply %d for account %d (result %d) cannot be forwarded: no world connection. Releasing the request without replying.",
        //     tid, transaction->LoginUser.uid, reason)
        L2_LOG_INFO(Log,
            "[Token] Close-token reply %d for account %d (result %d) cannot be forwarded: no world connection. Releasing the request without replying.",
            tid, transaction->LoginUser.uid, reason);
    }
    transaction->ReleaseRef();
    return false;
}
