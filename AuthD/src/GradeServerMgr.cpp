#include "GradeServerMgr.h"

#include "Shared/src/ScopedLockGuards.h"

#include "Platform/IOSocketTransport.h"
#include "Shared/src/Runtime/NativeSocketError.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/TimerQueue.h"
#include "AccountDB.h"
#include "ClientSession.h"
#include "Config.h"
#include "GradeSocket.h"
#include "Log.h"
#include "Thread.h"
#include "Util.h"

#include <cstdint>
#include <cstring>
#include <ctime>

GradeServerMgr* GradeServerMgr::sInstance = nullptr;

namespace {

struct GradeConnection {
    std::uint32_t socket = UINT32_C(0xffffffff);
    bool connected = false;
};

GradeConnection ConnectGradeServer() {
    GradeConnection connection;
    const std::uint32_t peerAddress = ntohl(config.GradeIP.s_addr);
    connection.connected =
        authd::platform::ConnectNativeSocket(
            connection.socket,
            config.GradeIP,
            static_cast<std::uint16_t>(
                config.GradePort));
    if (!connection.connected) {
        // Log.AddLog(LOG_WARN,
        //     "component=network event=connect_failed peer=grade address=%u.%u.%u.%u port=%u socket=%u error_domain=winsock_compatible code=%d action=close_socket reconnect=schedule_pending",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.GradePort)),
        //     connection.socket, ::portable::GetNativeSocketLastError())
        // Log.AddLog(LOG_WARN,
        //     "[Grade] Could not connect to %u.%u.%u.%u:%u on socket %u (WinSock-compatible error %d). Closing the socket. Requesting another retry.",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.GradePort)),
        //     connection.socket, ::portable::GetNativeSocketLastError())
        L2_LOG_WARN(Log,
            "[Grade] Could not connect to %u.%u.%u.%u:%u on socket %u (WinSock-compatible error %d). Closing the socket. Requesting another retry.",
            (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            (peerAddress >> 8U) & 255U, peerAddress & 255U,
            static_cast<unsigned int>(static_cast<std::uint16_t>(config.GradePort)),
            connection.socket, ::portable::GetNativeSocketLastError());
    }
    return connection;
}

} // namespace

// L2Authd.exe 0x00443690, Src/gradeservermgr.cpp:17-26.
GradeTrans* GradeTrans::Alloc() {
    auto* const pGradeTrans = new GradeTrans{};
    pGradeTrans->AddRef();
    return pGradeTrans;
}

// L2Authd.exe 0x00443760, Src/gradeservermgr.cpp:30-34.
void GradeTrans::AddRef() {
    runtime::raw_atomic::IncrementI32(&mRefCount);
    if (pClientSession != nullptr) {
        pClientSession->AddRef();
    }
}

// L2Authd.exe 0x00443790, Src/gradeservermgr.cpp:38-44.
void GradeTrans::ReleaseRef() {
    if (pClientSession != nullptr) {
        pClientSession->ReleaseRef();
    }
    if (runtime::raw_atomic::DecrementI32(
            &mRefCount) == 0) {
        ::operator delete(this);
    }
}

// L2Authd.exe 0x004437E0, Src/gradeservermgr.cpp:55-63.
GradeServerMgr::GradeServerMgr()
    : mTransMapLock(eCriticalSection, 0) {
    if (sInstance == nullptr) {
        sInstance = this;
        mGradeServerConnected = false;
        mpGradeSocket = nullptr;
        mhGradeServerTimer = nullptr;
        mTransId = 0;
    }
}

// L2Authd.exe 0x00443890, Src/gradeservermgr.cpp:67-69.
GradeServerMgr::~GradeServerMgr() {
    sInstance = nullptr;
}

// L2Authd.exe 0x00443900, Src/gradeservermgr.cpp:115-128.
void GradeServerMgr::Initialize() {
    const GradeConnection connection =
        ConnectGradeServer();
    mpGradeSocket = GradeSocket::Allocate(
        connection.socket);
    if (!connection.connected) {
        mpGradeSocket->CloseSocket();
    } else {
        authd::platform::InitializeNativeSocket(
            mpGradeSocket,
            connection.socket,
            g_hIOCompletionPort);
    }
}

// L2Authd.exe 0x004439A0, Src/gradeservermgr.cpp:73-111.
void GradeServerMgr::GradeServerTimerRoutine() {
    if (mhGradeServerTimer != nullptr) {
        runtime::timer_queue::DeleteTimer(
            nullptr,
            mhGradeServerTimer);
        mhGradeServerTimer = nullptr;
    }
    if (mGradeServerConnected) {
        return;
    }

    const GradeConnection connection =
        ConnectGradeServer();
    GradeSocket* const tempSocket =
        GradeSocket::Allocate(connection.socket);
    if (!connection.connected) {
        tempSocket->CloseSocket();
        tempSocket->ReleaseRef();
        return;
    }

    GradeSocket* const swapSocket = mpGradeSocket;
    authd::platform::InitializeNativeSocket(
        tempSocket,
        connection.socket,
        g_hIOCompletionPort);
    runtime::raw_atomic::ExchangePointer(
        &mpGradeSocket,
        tempSocket);
    swapSocket->ReleaseRef();
    mGradeServerConnected = true;
}

// L2Authd.exe 0x00443AA0, Src/gradeservermgr.cpp:163-182.
bool GradeServerMgr::AddGradeTrans(
    int tid,
    GradeTrans* pGradeTrans) {
    pGradeTrans->AddRef();
    pGradeTrans->opTime = static_cast<std::int32_t>(
        std::time(nullptr));
    bool result = false;
    {
        L2_GUARD(mTransMapLock);
        result = mGradeTransMap.insert(
            {tid, pGradeTrans}).second;
    }
    if (!result) {
        pGradeTrans->ReleaseRef();
    }
    return result;
}

// L2Authd.exe 0x00443B20, Src/gradeservermgr.cpp:132-159.
bool GradeServerMgr::DelGradeTrans(
    int tid,
    GradeTrans** pGradeTrans) {
    GradeTrans* pOldGradeTrans = nullptr;
    {
        L2_GUARD(mTransMapLock);
        const auto it = mGradeTransMap.find(tid);
        if (it == mGradeTransMap.end()) {
            return false;
        }

        pOldGradeTrans = it->second;
        mGradeTransMap.erase(it);
    }
    if (pGradeTrans == nullptr) {
        pOldGradeTrans->ReleaseRef();
    } else {
        *pGradeTrans = pOldGradeTrans;
    }
    return true;
}

// L2Authd.exe 0x00443BE0, Src/gradeservermgr.cpp:186-228.
char GradeServerMgr::RequestGrade(
    CClientSession* pSession,
    PreLoginUser& prelogin,
    char origCmd) {
    GradeTrans* const pGradeTrans = GradeTrans::Alloc();
    pGradeTrans->pClientSession = pSession;
    std::memcpy(
        &pGradeTrans->preLoginUser,
        &prelogin,
        0x1694);
    pGradeTrans->mOrigCmd = origCmd;
    if (pSession != nullptr) {
        pSession->AddRef();
    }

    const std::int32_t tid =
        runtime::raw_atomic::IncrementI32(
            &mTransId);
    if (!AddGradeTrans(tid, pGradeTrans)) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=grade operation=get_grade transaction=%d account_id=%d reason=transaction_conflict result_code=7 action=return_failure",
        //     tid, prelogin.account.uid)
        // Log.AddLog(LOG_WARN,
        //     "[Grade] Grade request %d for account %d conflicts with a pending transaction. Returning failure code 7.",
        //     tid, prelogin.account.uid)
        L2_LOG_WARN(Log,
            "[Grade] Grade request %d for account %d conflicts with a pending transaction. Returning failure code 7.",
            tid, prelogin.account.uid);
        pGradeTrans->ReleaseRef();
        return 7;
    }

    const short serviceCode = config.gameId == 8
        ? 0x20
        : static_cast<short>(config.gameId);
    if (!mpGradeSocket->Send(
            "cdhD",
            2,
            tid,
            static_cast<int>(serviceCode),
            static_cast<std::int64_t>(
                prelogin.account.uid))) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=grade operation=get_grade transaction=%d account_id=%d reason=socket_send_rejected result_code=75 action=remove_pending_request",
        //     tid, prelogin.account.uid)
        // Log.AddLog(LOG_WARN,
        //     "[Grade] Could not send grade request %d for account %d. Removing the pending request and returning code 75.",
        //     tid, prelogin.account.uid)
        // L2_LOG_WARN(Log,
        //     "[Grade] Could not send grade request %d for account %d. Removing the pending request and returning code 75.",
        //     tid, prelogin.account.uid)
        L2_LOG_WARN(Log,
            "[Grade] Grade request %d for account %d was not submitted: grade-service connection is unavailable. Removing the pending request and returning code 75.",
            tid, prelogin.account.uid);
        DelGradeTrans(tid, nullptr);
        pGradeTrans->ReleaseRef();
        return 'K';
    }

    pGradeTrans->ReleaseRef();
    return 0;
}

// L2Authd.exe 0x00443CE0, Src/gradeservermgr.cpp:233-292.
bool GradeServerMgr::RecvGrade(
    [[maybe_unused]] CIOSocket* pIOSocket,
    const unsigned char* packet) {
    const int tid = GetIntFromPacket(&packet);
    const int result = GetIntFromPacket(&packet);
    [[maybe_unused]] const short serviceCode =
        GetShortFromPacket(&packet);
    [[maybe_unused]] const std::int64_t uid =
        GetInt64FromPacket(&packet);
    const unsigned short gameGradeInfo =
        static_cast<unsigned short>(
            GetShortFromPacket(&packet));

    GradeTrans* pGradeTrans = nullptr;
    if (!DelGradeTrans(tid, &pGradeTrans)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Transaction was deleted while getting Grade(%d)",
        //     tid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=reply_without_pending_request peer=grade operation=get_grade transaction=%d result_code=%d action=ignore_reply",
        //     tid, result)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[Grade] Ignoring grade reply %d (result %d): no request is pending.",
        //     tid, result)
        L2_LOG_WARN(Log,
            "[Grade] Ignoring grade reply %d (result %d): no request is pending.",
            tid, result);
        return false;
    }

    if (pGradeTrans->mOrigCmd != 0) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=unsupported_request_origin peer=grade transaction=%d account_id=%d origin=%u result_code=%d action=release_request_without_reply",
        //     tid, pGradeTrans->preLoginUser.account.uid,
        //     static_cast<unsigned int>(static_cast<unsigned char>(pGradeTrans->mOrigCmd)), result)
        // Log.AddLog(LOG_WARN,
        //     "[Grade] Grade request %d for account %d has unsupported origin %u (result %d). Releasing the request without replying.",
        //     tid, pGradeTrans->preLoginUser.account.uid,
        //     static_cast<unsigned int>(static_cast<unsigned char>(pGradeTrans->mOrigCmd)), result)
        L2_LOG_WARN(Log,
            "[Grade] Grade request %d for account %d has unsupported origin %u (result %d). Releasing the request without replying.",
            tid, pGradeTrans->preLoginUser.account.uid,
            static_cast<unsigned int>(static_cast<unsigned char>(pGradeTrans->mOrigCmd)), result);
    }
    if (result == 0) {
        if (pGradeTrans->mOrigCmd == 0) {
            pGradeTrans->preLoginUser.account.user_level =
                gameGradeInfo;
            accountdb.AuthorizeClientSideLogin(
                pGradeTrans->pClientSession,
                pGradeTrans->preLoginUser);
        }
    } else if (config.useLevelCheck) {
        if (pGradeTrans->mOrigCmd == 0) {
            // Log.AddLog(LOG_WARN,
            //     "component=authentication event=grade_check_failed peer=grade transaction=%d account_id=%d result_code=%d level_check=enabled client_reason=75 action=send_failure_reply",
            //     tid, pGradeTrans->preLoginUser.account.uid, result)
            // Log.AddLog(LOG_WARN,
            //     "[Grade] Grade check failed for request %d, account %d (result %d). Level checking is enabled. Sending client failure code 75.",
            //     tid, pGradeTrans->preLoginUser.account.uid, result)
            L2_LOG_WARN(Log,
                "[Grade] Grade check failed for request %d, account %d (result %d). Level checking is enabled; sending client failure code 75.",
                tid, pGradeTrans->preLoginUser.account.uid, result);
            pGradeTrans->pClientSession->SendFail(
                1,
                'K',
                nullptr);
        }
    } else if (pGradeTrans->mOrigCmd == 0) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=grade_check_bypassed peer=grade transaction=%d account_id=%d result_code=%d level_check=disabled default_grade=0 action=continue_authorization",
        //     tid, pGradeTrans->preLoginUser.account.uid, result)
        // Log.AddLog(LOG_WARN,
        //     "[Grade] Grade check failed for request %d, account %d (result %d). Level checking is disabled. Continuing authorization with grade 0.",
        //     tid, pGradeTrans->preLoginUser.account.uid, result)
        L2_LOG_WARN(Log,
            "[Grade] Grade check failed for request %d, account %d (result %d). Level checking is disabled; continuing authorization with grade 0.",
            tid, pGradeTrans->preLoginUser.account.uid, result);
        pGradeTrans->preLoginUser.account.user_level = 0;
        accountdb.AuthorizeClientSideLogin(
            pGradeTrans->pClientSession,
            pGradeTrans->preLoginUser);
    }
    pGradeTrans->ReleaseRef();
    return false;
}
