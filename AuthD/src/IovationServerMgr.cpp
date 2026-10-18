#include "IovationServerMgr.h"

#include "Shared/src/ScopedLockGuards.h"

#include "Platform/IOSocketTransport.h"
#include "Shared/src/Runtime/NativeSocketError.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/TimerQueue.h"
#include "ClientSession.h"
#include "Config.h"
#include "Exception.h"
#include "IovationSocket.h"
#include "Log.h"
#include "Thread.h"
#include "Util.h"

#include <cstdint>
#include <cstring>
#include <ctime>

IovationServerMgr* IovationServerMgr::sInstance = nullptr;

namespace {

struct IovationConnection {
    std::uint32_t socket = UINT32_C(0xffffffff);
    bool connected = false;
};

IovationConnection ConnectIovationProxy() {
    IovationConnection connection;
    const std::uint32_t peerAddress = ntohl(config.iovationProxyIPAddress.s_addr);
    connection.connected =
        authd::platform::ConnectNativeSocket(
            connection.socket,
            config.iovationProxyIPAddress,
            static_cast<std::uint16_t>(
                config.iovationProxyPort));
    if (!connection.connected) {
        // Log.AddLog(LOG_WARN,
        //     "component=network event=connect_failed peer=iovation address=%u.%u.%u.%u port=%u socket=%u error_domain=winsock_compatible code=%d action=close_socket reconnect=schedule_pending",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.iovationProxyPort)),
        //     connection.socket, ::portable::GetNativeSocketLastError())
        // Log.AddLog(LOG_WARN,
        //     "[Iovation] Could not connect to %u.%u.%u.%u:%u on socket %u (WinSock-compatible error %d). Closing the socket. Requesting another retry.",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.iovationProxyPort)),
        //     connection.socket, ::portable::GetNativeSocketLastError())
        L2_LOG_WARN(Log,
            "[Iovation] Could not connect to %u.%u.%u.%u:%u on socket %u (WinSock-compatible error %d). Closing the socket. Requesting another retry.",
            (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            (peerAddress >> 8U) & 255U, peerAddress & 255U,
            static_cast<unsigned int>(static_cast<std::uint16_t>(config.iovationProxyPort)),
            connection.socket, ::portable::GetNativeSocketLastError());
    }
    return connection;
}

} // namespace

// L2Authd.exe 0x00450490, Src/iovationservermgr.cpp:15-23.
IovationTrans* IovationTrans::Alloc() {
    auto* const transaction = new IovationTrans{};
    transaction->AddRef();
    return transaction;
}

// L2Authd.exe 0x00450550, Src/iovationservermgr.cpp:27-31.
void IovationTrans::AddRef() {
    runtime::raw_atomic::IncrementI32(&mRefCount);
    if (pClientSession != nullptr) {
        pClientSession->AddRef();
    }
}

// L2Authd.exe 0x00450590, Src/iovationservermgr.cpp:35-41.
void IovationTrans::ReleaseRef() {
    if (pClientSession != nullptr) {
        pClientSession->ReleaseRef();
    }
    if (runtime::raw_atomic::DecrementI32(
            &mRefCount) == 0) {
        ::operator delete(this);
    }
}

// L2Authd.exe 0x004505E0, Src/iovationservermgr.cpp:52-60.
IovationServerMgr::IovationServerMgr()
    : mTransMapLock(eCriticalSection, 0) {
    if (sInstance == nullptr) {
        sInstance = this;
        mIovationServerConnected = false;
        mpIovationSocket = nullptr;
        mhIovationServerTimer = nullptr;
        mTransId = 0;
    }
}

// L2Authd.exe 0x00450690, Src/iovationservermgr.cpp:64-66.
IovationServerMgr::~IovationServerMgr() {
    sInstance = nullptr;
}

// L2Authd.exe 0x00420EA0, Src/iovationservermgr.h:44.
IovationServerMgr* IovationServerMgr::Inst() {
    return sInstance;
}

// L2Authd.exe 0x00450700, Src/iovationservermgr.cpp:112-125.
void IovationServerMgr::Initialize() {
    const IovationConnection connection =
        ConnectIovationProxy();
    mpIovationSocket =
        IovationSocket::Allocate(connection.socket);
    if (!connection.connected) {
        mpIovationSocket->CloseSocket();
    } else {
        authd::platform::InitializeNativeSocket(
            mpIovationSocket,
            connection.socket,
            g_hIOCompletionPort);
    }
}

// L2Authd.exe 0x004507A0, Src/iovationservermgr.cpp:70-108.
void IovationServerMgr::IovationServerTimerRoutine() {
    if (mhIovationServerTimer != nullptr) {
        runtime::timer_queue::DeleteTimer(
            nullptr,
            mhIovationServerTimer);
        mhIovationServerTimer = nullptr;
    }
    if (mIovationServerConnected) {
        return;
    }

    const IovationConnection connection =
        ConnectIovationProxy();
    IovationSocket* const replacement =
        IovationSocket::Allocate(connection.socket);
    if (!connection.connected) {
        replacement->CloseSocket();
        replacement->ReleaseRef();
        return;
    }

    IovationSocket* const previous = mpIovationSocket;
    authd::platform::InitializeNativeSocket(
        replacement,
        connection.socket,
        g_hIOCompletionPort);
    runtime::raw_atomic::ExchangePointer(
        &mpIovationSocket,
        replacement);
    previous->ReleaseRef();
    mIovationServerConnected = true;
}

// L2Authd.exe 0x004508A0, Src/iovationservermgr.cpp:159-178.
bool IovationServerMgr::AddIovationTrans(
    int transactionId,
    IovationTrans* transaction) {
    transaction->AddRef();
    transaction->opTime = static_cast<std::int32_t>(
        std::time(nullptr));
    bool inserted = false;
    {
        L2_GUARD(mTransMapLock);
        inserted =
            mIovationTransMap.insert(
                {transactionId, transaction}).second;
    }
    if (!inserted) {
        transaction->ReleaseRef();
    }
    return inserted;
}

// L2Authd.exe 0x00450920, Src/iovationservermgr.cpp:129-155.
bool IovationServerMgr::DelIovationTrans(
    int transactionId,
    IovationTrans** transaction) {
    IovationTrans* removed = nullptr;
    {
        L2_GUARD(mTransMapLock);
        const auto found =
            mIovationTransMap.find(transactionId);
        if (found == mIovationTransMap.end()) {
            return false;
        }

        removed = found->second;
        mIovationTransMap.erase(found);
    }
    if (transaction == nullptr) {
        removed->ReleaseRef();
    } else {
        *transaction = removed;
    }
    return true;
}

// L2Authd.exe 0x004509E0, Src/iovationservermgr.cpp:187-227.
char IovationServerMgr::RequestCheckDetail(
    PreLoginUser& user,
    CClientSession* client,
    unsigned short dataLength,
    const unsigned char* data) {
    try {
        IovationTrans* const transaction =
            IovationTrans::Alloc();
        std::memcpy(
            &transaction->preLoginUser,
            &user,
            0x1694);
        transaction->pClientSession = client;
        client->AddRef();

        const int transactionId =
            runtime::raw_atomic::IncrementI32(
                &mTransId);
        if (!AddIovationTrans(
                transactionId,
                transaction)) {
            // Log.AddLog(LOG_WARN,
            //     "component=authentication event=request_not_submitted peer=iovation operation=check_detail transaction=%d account_id=%d reason=transaction_conflict result_code=7 action=return_failure",
            //     transactionId, user.account.uid)
            // Log.AddLog(LOG_WARN,
            //     "[Iovation] Verification request %d for account %d conflicts with a pending transaction. Returning failure code 7.",
            //     transactionId, user.account.uid)
            L2_LOG_WARN(Log,
                "[Iovation] Verification request %d for account %d conflicts with a pending transaction. Returning failure code 7.",
                transactionId, user.account.uid);
            transaction->ReleaseRef();
            return 7;
        }

        const int proxyCountry =
            config.Country == 8
            ? 32
            : config.Country;
        const in_addr clientAddress = client->getaddr();
        const bool sent = mpIovationSocket->Send(
            "cddddhbc",
            1,
            transactionId,
            proxyCountry,
            user.account.uid,
            clientAddress.s_addr,
            static_cast<int>(dataLength) + 1,
            static_cast<int>(dataLength),
            data,
            0);
        if (!sent) {
            // Log.AddLog(LOG_WARN,
            //     "component=authentication event=request_not_submitted peer=iovation operation=check_detail transaction=%d account_id=%d reason=socket_send_rejected result_code=20 action=remove_pending_request",
            //     transactionId, user.account.uid)
            // Log.AddLog(LOG_WARN,
            //     "[Iovation] Could not send verification request %d for account %d. Removing the pending request and returning code 20.",
            //     transactionId, user.account.uid)
            // L2_LOG_WARN(Log,
            //     "[Iovation] Could not send verification request %d for account %d. Removing the pending request and returning code 20.",
            //     transactionId, user.account.uid)
            L2_LOG_WARN(Log,
                "[Iovation] Verification request %d for account %d was not submitted: proxy connection is unavailable. Removing the pending request and returning code 20.",
                transactionId, user.account.uid);
            DelIovationTrans(transactionId, nullptr);
            transaction->ReleaseRef();
            return 20;
        }
        transaction->ReleaseRef();
        return 0;
    } catch (...) {
        // ELOG(
        //     "*%s:%d(%s) exception\n",
        //     "IovationServerMgr.cpp",
        //     225,
        //     "Wed Oct 23 17:02:11 2019")
        ELOG(
            "[IovationServerMgr.cpp] Exception while requesting the detail check (%u payload bytes). Entering exception handling.\n",
            static_cast<unsigned int>(dataLength));
        send_exception(true);
        return 0;
    }
}

// L2Authd.exe 0x00450BB0, Src/iovationservermgr.cpp:231-260.
bool IovationServerMgr::RecvCheckTransactionDetailAck(
    [[maybe_unused]] CIOSocket* socket,
    const unsigned char* packet) {
    const int transactionId =
        GetIntFromPacket(&packet);
    const int result = GetIntFromPacket(&packet);
    IovationTrans* transaction = nullptr;
    if (!DelIovationTrans(
            transactionId,
            &transaction)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Transaction was deleted while getting "
        //     "IovationProxy(%d)",
        //     transactionId)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=reply_without_pending_request peer=iovation operation=check_detail transaction=%d result_code=%d action=ignore_reply",
        //     transactionId, result)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[Iovation] Ignoring verification reply %d (result %d): no request is pending.",
        //     transactionId, result)
        L2_LOG_WARN(Log,
            "[Iovation] Ignoring verification reply %d (result %d): no request is pending.",
            transactionId, result);
        return false;
    }

    if (result == 1) {
        // Log.AddLog(LOG_NORMAL,
        //     "component=authentication event=login_rejected peer=iovation transaction=%d account_id=%d result_code=1 client_reason=4 action=send_failure_reply",
        //     transactionId, transaction->preLoginUser.account.uid)
        // Log.AddLog(LOG_NORMAL,
        //     "[Iovation] Verification request %d for account %d rejected login (result 1). Sending client failure code 4.",
        //     transactionId, transaction->preLoginUser.account.uid)
        L2_LOG_INFO(Log,
            "[Iovation] Verification request %d for account %d rejected login (result 1). Sending client failure code 4.",
            transactionId, transaction->preLoginUser.account.uid);
        transaction->pClientSession->SendFail(
            1,
            4,
            nullptr);
        transaction->ReleaseRef();
        return false;
    }
    if (result != 0) {
        // Log.AddLog(
        //     LOG_WARN,
        //     "IovationProxy reply CheckTransactionDetails unusual "
        //     "result(%d) on uid(%d)",
        //     result,
        //     transaction->preLoginUser.account.uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=unexpected_verification_result peer=iovation transaction=%d account_id=%d result_code=%d action=continue_authorization",
        //     transactionId, transaction->preLoginUser.account.uid, result)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[Iovation] Verification request %d for account %d returned unexpected result %d. Authorization continues.",
        //     transactionId, transaction->preLoginUser.account.uid, result)
        L2_LOG_WARN(Log,
            "[Iovation] Verification request %d for account %d returned unexpected result %d. Authorization continues.",
            transactionId, transaction->preLoginUser.account.uid, result);
    }
    accountdb.AuthorizeUser(
        transaction->pClientSession,
        transaction->preLoginUser);
    transaction->ReleaseRef();
    return false;
}
