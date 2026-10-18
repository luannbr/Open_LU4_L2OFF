#include "PAServerMgr.h"

#include "Shared/src/ScopedLockGuards.h"

#include "Platform/Guid.h"
#include "Platform/IOSocketTransport.h"
#include "Platform/LoginUserImage.h"
#include "Shared/src/Runtime/NativeSocketError.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/TimerQueue.h"
#include "AccountDB.h"
#include "ClientSession.h"
#include "Config.h"
#include "Exception.h"
#include "IOServer.h"
#include "Log.h"
#include "PASocket.h"
#include "Protocol.h"
#include "ServerList.h"
#include "SocketServerEx.h"
#include "Thread.h"
#include "Util.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iterator>

#if defined(_WIN32)
#include <WinSock2.h>
#else
#include <arpa/inet.h>
#endif

void OCHKRequestCallback(
    int result,
    CSocketInt* socket,
    const char* gameAccount);

PAServerMgr* PAServerMgr::sInstance = nullptr;

namespace {

// L2Authd.exe 0x0052B6C4, Src/paservermgr.cpp. The selected image increments
// this raw 32-bit cursor and performs an unsigned division by socket count.
std::uint32_t paIdx = 0;

constexpr char kPAServerFailure = 0x14;

int PAServiceCode() noexcept {
    return config.gameId == 8 ? 0x20 : 0x1b;
}

int PAAffiliatedCode() noexcept {
    int affiliatedCode = config.gameId == 8 ? 0 : 0x0d;
    if (affiliatedCode == 0x0d &&
        config.affiliatedGameId != 0) {
        affiliatedCode = config.affiliatedGameId;
    }
    return affiliatedCode;
}

void CopyPreLoginUser(
    PreLoginUser& destination,
    const PreLoginUser& source) {
    // The selected x86 image copies exactly 0x1694 bytes with REP MOVSD.
    // Both operands have the same native portable representation here.
    std::memcpy(
        &destination,
        &source,
        sizeof(PreLoginUser));
}

} // namespace

// L2Authd.exe 0x0046CE70, Src/paservermgr.cpp:22-36.
PAUser* PAUser::Alloc() {
    auto* const user = new PAUser;
    user->pIOSocket = nullptr;
    user->pSession = nullptr;
    user->mRefCount = 0;
    user->opTime = 0;
    user->AddRef();
    user->origin = 0;
    user->serverid = 0;
    user->stat = 0;
    user->clientIp.s_addr = 0;
    user->locationNo = 0;
    return user;
}

// L2Authd.exe 0x0046CF60, Src/paservermgr.cpp:53-60.
void PAUser::Free() {
    if (pIOSocket != nullptr) {
        pIOSocket->ReleaseRef();
    }
    if (pSession != nullptr) {
        pSession->ReleaseRef();
    }
    // The target calls raw operator delete here instead of the generated
    // PAUser destructor. Preserve that ownership path in the baseline.
    ::operator delete(this);
}

// L2Authd.exe 0x0046CFB0, Src/paservermgr.cpp:40-42.
void PAUser::AddRef() {
    runtime::raw_atomic::IncrementI32(
        &mRefCount);
}

// L2Authd.exe 0x0046CFD0, Src/paservermgr.cpp:46-49.
void PAUser::ReleaseRef() {
    if (runtime::raw_atomic::DecrementI32(
            &mRefCount) == 0) {
        Free();
    }
}

// L2Authd.exe 0x0046D000, Src/paservermgr.cpp:73-78.
PAServerMgr::PAServerMgr()
    : mUserMapLock(eCriticalSection, 0),
      mSocketMapLock(eCriticalSection, 0) {
    if (sInstance == nullptr) {
        sInstance = this;
        mTransCount = 0;
    }
}

// L2Authd.exe 0x0046D0B0, Src/paservermgr.cpp:82-84.
PAServerMgr::~PAServerMgr() {
    sInstance = nullptr;
}

// L2Authd.exe 0x0046D140, Src/paservermgr.cpp:88-98.
bool PAServerMgr::RegisterPASocketMap(
    CPASocket* pPASocket) {
    if (pPASocket != nullptr) {
        pPASocket->AddRef();
        {
            L2_GUARD(mSocketMapLock);
            // The source discards insert's duplicate flag, retaining the added
            // reference even when this pointer is already present in the set.
            mSocketMap.insert(pPASocket);
        }
    }
    return pPASocket != nullptr;
}

// L2Authd.exe 0x0046D190, Src/paservermgr.cpp:101-131.
bool PAServerMgr::UnregisterPASocketMap(
    CPASocket* pPASocket) {
    if (pPASocket == nullptr) {
        return false;
    }

    bool removed = false;
    {
        L2_GUARD(mSocketMapLock);
        for (auto current = mSocketMap.begin();
             current != mSocketMap.end();
             ++current) {
            if (*current == pPASocket) {
                mSocketMap.erase(current);
                pPASocket->ReleaseRef();
                removed = true;
                break;
            }
        }
    }
    return removed;
}

// L2Authd.exe 0x0046D240, Src/paservermgr.cpp:134-158.
CPASocket* PAServerMgr::GetPASocket() {
    CPASocket* socket = nullptr;
    {
        L2_GUARD(mSocketMapLock);
        if (!mSocketMap.empty()) {
            const std::uint32_t selected =
                paIdx % static_cast<std::uint32_t>(
                    mSocketMap.size());
            ++paIdx;
            auto current = mSocketMap.begin();
            std::advance(current, selected);
            socket = *current;
            socket->AddRef();
        }
    }
    return socket;
}

// L2Authd.exe 0x0046D2E0, Src/paservermgr.cpp:223-254.
void PAServerMgr::Initialize() {
    for (ConnectionInformation& information :
         config.PAHostInfo) {
        const std::uint32_t peerAddress = ntohl(information.host.s_addr);
        std::uint32_t nativeSocket = UINT32_C(0xffffffff);
        const bool connected =
            authd::platform::ConnectNativeSocket(
                nativeSocket,
                information.host,
                information.port);
        if (!connected) {
            // Log.AddLog(LOG_WARN,
            //     "component=network event=connect_failed peer=pa address=%u.%u.%u.%u port=%u socket=%u error_domain=winsock_compatible code=%d action=close_socket",
            //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
            //     static_cast<unsigned int>(information.port), nativeSocket,
            //     ::portable::GetNativeSocketLastError())
            // Log.AddLog(LOG_WARN,
            //     "[PA] Could not connect to %u.%u.%u.%u:%u on socket %u (WinSock-compatible error %d). Closing the socket.",
            //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
            //     static_cast<unsigned int>(information.port), nativeSocket,
            //     ::portable::GetNativeSocketLastError())
            L2_LOG_WARN(Log,
                "[PA] Could not connect to %u.%u.%u.%u:%u on socket %u (WinSock-compatible error %d). Closing the socket.",
                (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
                (peerAddress >> 8U) & 255U, peerAddress & 255U,
                static_cast<unsigned int>(information.port), nativeSocket,
                ::portable::GetNativeSocketLastError());
        }
        CPASocket* const socket = CPASocket::Allocate(
            nativeSocket,
            information);
        if (connected) {
            authd::platform::InitializeNativeSocket(
                socket,
                nativeSocket,
                g_hIOCompletionPort);
        } else {
#if !defined(_WIN32) || INTPTR_MAX != INT32_MAX
            // The portable transport is not attached until Initialize, so
            // close the failed native descriptor at this platform boundary.
            authd::platform::AbortiveCloseNativeSocket(
                nativeSocket);
#endif
            socket->CloseSocket();
        }
    }
}

// L2Authd.exe 0x0046D3E0, Src/paservermgr.cpp:161-219.
void PAServerMgr::PAServerTimerRoutine(
    CPASocket* pPASocket) {
    if (reinterpret_cast<std::uintptr_t>(this) == 0U) {
        return;
    }
    if (pPASocket == nullptr) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "PAServer reconnect timer triggered without socket handle")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=network event=reconnect_failed peer=pa reason=null_socket action=abort_process")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[PA] Reconnect callback has no socket. Aborting the process.")
        L2_LOG_ERROR(Log,
            "[PA] Reconnect callback has no socket. Aborting the process.");
        // The selected code continues and immediately dereferences the null
        // argument. Abort is the deterministic portable terminal equivalent.
        std::abort();
    }

    if (pPASocket->hPAReconnectTimer != nullptr) {
        runtime::timer_queue::DeleteTimer(
            nullptr,
            pPASocket->hPAReconnectTimer);
        pPASocket->hPAReconnectTimer = nullptr;
    }

    const in_addr host = pPASocket->host;
    const std::uint16_t port = pPASocket->port;
    const std::uint32_t peerAddress = ntohl(host.s_addr);
    std::uint32_t nativeSocket = UINT32_C(0xffffffff);
    const bool connected =
        authd::platform::ConnectNativeSocket(
            nativeSocket,
            host,
            port);
    if (!connected) {
        // Log.AddLog(LOG_WARN,
        //     "component=network event=reconnect_failed peer=pa address=%u.%u.%u.%u port=%u socket=%u error_domain=winsock_compatible code=%d reconnect=schedule_pending delay_ms=%u",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(port), nativeSocket,
        //     ::portable::GetNativeSocketLastError(),
        //     static_cast<std::uint32_t>(config.PAReconnectInterval))
        // Log.AddLog(LOG_WARN,
        //     "[PA] Could not reconnect to %u.%u.%u.%u:%u on socket %u (WinSock-compatible error %d). Requesting another retry after %u ms.",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(port), nativeSocket,
        //     ::portable::GetNativeSocketLastError(),
        //     static_cast<std::uint32_t>(config.PAReconnectInterval))
        L2_LOG_WARN(Log,
            "[PA] Could not reconnect to %u.%u.%u.%u:%u on socket %u (WinSock-compatible error %d). Requesting another retry after %u ms.",
            (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            (peerAddress >> 8U) & 255U, peerAddress & 255U,
            static_cast<unsigned int>(port), nativeSocket,
            ::portable::GetNativeSocketLastError(),
            static_cast<std::uint32_t>(config.PAReconnectInterval));
#if !defined(_WIN32) || INTPTR_MAX != INT32_MAX
        authd::platform::AbortiveCloseNativeSocket(
            nativeSocket);
#endif
        pPASocket->OnClose();
        pPASocket->ReleaseRef();
        return;
    }

    ConnectionInformation information(host, port);
    CPASocket* const replacement = CPASocket::Allocate(
        nativeSocket,
        information);
    authd::platform::InitializeNativeSocket(
        replacement,
        nativeSocket,
        g_hIOCompletionPort);
    // This explicit registration and the later OnCreate registration are both
    // present in the shipping image, including duplicate-ref behavior.
    RegisterPASocketMap(replacement);
    pPASocket->ReleaseRef();
}

// L2Authd.exe 0x0046D540, Src/paservermgr.cpp:312-336.
std::int32_t PAServerMgr::AddUserWait(
    PAUser* pPAUser) {
    pPAUser->AddRef();
    pPAUser->opTime = static_cast<std::int32_t>(
        std::time(nullptr));

    std::int32_t identity = 0;
    bool inserted = false;
    {
        L2_GUARD(mUserMapLock);
        do {
            identity =
                runtime::raw_atomic::IncrementI32(
                    &mTransCount);
            inserted = mWaitUserMap.insert(
                {identity, pPAUser}).second;
        } while (!inserted);
    }

    // Retained from the original source after its successful-insert loop.
    if (!inserted) {
        pPAUser->ReleaseRef();
    }
    return identity;
}

// L2Authd.exe 0x0046D5E0, Src/paservermgr.cpp:288-307.
PAUser* PAServerMgr::GetUserWait(int identity) {
    PAUser* user = nullptr;
    {
        L2_GUARD(mUserMapLock);
        const auto found = mWaitUserMap.find(identity);
        if (found != mWaitUserMap.end()) {
            user = found->second;
            user->AddRef();
        }
    }
    return user;
}

// L2Authd.exe 0x0046D670, Src/paservermgr.cpp:258-284.
bool PAServerMgr::DelUserWait(
    int identity,
    PAUser** pPAUser) {
    PAUser* user = nullptr;
    {
        L2_GUARD(mUserMapLock);
        const auto found = mWaitUserMap.find(identity);
        if (found == mWaitUserMap.end()) {
            return false;
        }

        user = found->second;
        mWaitUserMap.erase(found);
    }
    if (pPAUser == nullptr) {
        user->ReleaseRef();
    } else {
        *pPAUser = user;
    }
    return true;
}

// L2Authd.exe 0x0046D730, Src/paservermgr.cpp:345-370.
char PAServerMgr::LoginAccount(
    int origin,
    CClientSession* pSession,
    CIOSocket* pSocket,
    PreLoginUser* pPreUser) {
    PAUser* const user = PAUser::Alloc();
    if (pSession != nullptr) {
        user->pSession = pSession;
        pSession->AddRef();
    }
    if (pSocket != nullptr) {
        user->pIOSocket = pSocket;
        pSocket->AddRef();
    }
    CopyPreLoginUser(user->preUser, *pPreUser);
    user->origin = origin;

    const std::int32_t tid = AddUserWait(user);
    if (SendVerifyAccountReq(
            tid,
            pPreUser->playAccount,
            pPreUser->playPassword)) {
        user->ReleaseRef();
        return 0;
    }

    DelUserWait(tid, nullptr);
    user->ReleaseRef();
    return kPAServerFailure;
}

// L2Authd.exe 0x0046D7F0, Src/paservermgr.cpp:378-408.
char PAServerMgr::LoginExternalToken(
    int origin,
    CClientSession* pSession,
    CIOSocket* pSocket,
    PreLoginUser* pPreUser) {
    PAUser* const user = PAUser::Alloc();
    if (pSession != nullptr) {
        user->pSession = pSession;
        pSession->AddRef();
    }
    if (pSocket != nullptr) {
        user->pIOSocket = pSocket;
        pSocket->AddRef();
    }
    CopyPreLoginUser(user->preUser, *pPreUser);
    user->origin = origin;

    const std::int32_t tid = AddUserWait(user);
    if (SendExternalTokenLoginReq(
            tid,
            pPreUser->externalUser.companyNo,
            pPreUser->externalUser.externalToken,
            pPreUser->externalUser.affiliatedGameId,
            pPreUser->externalUser.gameId)) {
        user->ReleaseRef();
        return 0;
    }

    DelUserWait(tid, nullptr);
    user->ReleaseRef();
    return kPAServerFailure;
}

// L2Authd.exe 0x0046D8D0, Src/paservermgr.cpp:412-429.
char PAServerMgr::ExternalUserInfo(
    int uid,
    PreLoginUser preLogin,
    int serverid,
    int stat,
    in_addr clientIp) {
    PAUser* const user = PAUser::Alloc();
    CopyPreLoginUser(user->preUser, preLogin);
    user->serverid = serverid;
    user->stat = stat;
    user->clientIp = clientIp;

    const std::int32_t tid = AddUserWait(user);
    if (SendExternalUserInfoReq(tid, uid)) {
        user->ReleaseRef();
        return 0;
    }

    DelUserWait(tid, nullptr);
    user->ReleaseRef();
    return kPAServerFailure;
}

// L2Authd.exe 0x0046D980, Src/paservermgr.cpp:432-450.
char PAServerMgr::GetUserInfo(
    int origin,
    [[maybe_unused]] int uid,
    PreLoginUser preLogin,
    int serverid,
    int stat,
    in_addr clientIp) {
    PAUser* const user = PAUser::Alloc();
    CopyPreLoginUser(user->preUser, preLogin);
    user->serverid = serverid;
    user->stat = stat;
    user->clientIp = clientIp;
    user->origin = origin;

    const std::int32_t tid = AddUserWait(user);
    if (SendGetUserInfoReq(
            tid,
            preLogin.account.account)) {
        user->ReleaseRef();
        return 0;
    }

    DelUserWait(tid, nullptr);
    user->ReleaseRef();
    return kPAServerFailure;
}

// L2Authd.exe 0x0046DA40, Src/paservermgr.cpp:455-482.
char PAServerMgr::RequestAuthnToken(
    char serverId,
    LoginUser* pLoginUser,
    char* targetAppId) {
    PAUser* const user = PAUser::Alloc();
    user->serverid = static_cast<int>(serverId);
    authd::portable::CopyLoginUserImage(
        user->loginUser,
        *pLoginUser);
    if (pLoginUser->portalUid == 0) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=pa operation=request_authn_token server_id=%d reason=missing_portal_id result_code=2 action=return_failure",
        //     static_cast<int>(serverId))
        // Log.AddLog(LOG_WARN,
        //     "[PA] Cannot request an authentication token for server %d: portal ID is missing. Returning failure code 2.",
        //     static_cast<int>(serverId))
        L2_LOG_WARN(Log,
            "[PA] Cannot request an authentication token for server %d: portal ID is missing. Returning failure code 2.",
            static_cast<int>(serverId));
        user->ReleaseRef();
        return 2;
    }

    const std::int32_t tid = AddUserWait(user);
    if (SendRequestAuthnTokenReq(
            tid,
            pLoginUser->portalUid,
            pLoginUser->loginIp,
            targetAppId,
            config.clientAppId,
            pLoginUser->sessionId)) {
        user->ReleaseRef();
        return 0;
    }

    DelUserWait(tid, nullptr);
    user->ReleaseRef();
    return kPAServerFailure;
}

// L2Authd.exe 0x0046DB40, Src/paservermgr.cpp:491-526.
char PAServerMgr::LoginToken(
    int origin,
    CClientSession* pSession,
    CIOSocket* pSocket,
    PreLoginUser* pPreUser,
    in_addr clientIp) {
    PAUser* const user = PAUser::Alloc();
    if (pSession != nullptr) {
        user->pSession = pSession;
        pSession->AddRef();
    }
    if (pSocket != nullptr) {
        user->pIOSocket = pSocket;
        pSocket->AddRef();
    }
    CopyPreLoginUser(user->preUser, *pPreUser);
    user->origin = origin;
    user->clientIp = clientIp;

    const std::int32_t tid = AddUserWait(user);
    if (SendLoginToken(
            tid,
            user->preUser.tokenInfo.authProviderCode,
            user->preUser.tokenInfo.token,
            clientIp)) {
        user->ReleaseRef();
        return 0;
    }

    DelUserWait(tid, nullptr);
    user->ReleaseRef();
    return kPAServerFailure;
}

// L2Authd.exe 0x0046DC20, Src/paservermgr.cpp:534-589.
bool PAServerMgr::SendVerifyAccountReq(
    std::int32_t tid,
    char* account,
    char* password) {
    CPASocket* const socket = GetPASocket();
    if (socket == nullptr) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=pa operation=verify_account transaction=%d reason=no_connection action=return_failure",
        //     tid)
        // Log.AddLog(LOG_WARN,
        //     "[PA] Cannot submit the account verification request %d: no connection to the service. Returning failure.",
        //     tid)
        L2_LOG_WARN(Log,
            "[PA] Cannot submit the account verification request %d: no connection to the service. Returning failure.",
            tid);
        return false;
    }

    const int accountLength =
        config.usePASideExtendAccount ? 0x41 : 0x0f;
    const int passwordLength =
        config.usePASideExtendAccount ? 0x20 : 0x10;
    const std::uint32_t socketId = socket->m_hSocket;
    const bool sent = socket->Send(
        "cdddbbdb",
        AP_VERIFY_IDPWD_REQ,
        tid,
        PAAffiliatedCode(),
        PAServiceCode(),
        accountLength,
        account,
        passwordLength,
        password,
        0,
        0x10,
        &config.clientAppId);
    if (!sent) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=pa operation=verify_account transaction=%d socket=%u reason=socket_send_rejected action=return_failure",
        //     tid, socketId)
        // Log.AddLog(LOG_WARN,
        //     "[PA] Could not send the account verification request %d on socket %u. Returning failure.",
        //     tid, socketId)
        // L2_LOG_WARN(Log,
        //     "[PA] Could not send the account verification request %d on socket %u. Returning failure.",
        //     tid, socketId)
        L2_LOG_WARN(Log,
            "[PA] The account verification request %d was not submitted on socket %u: the selected connection is closed or invalid. Returning failure.",
            tid, socketId);
    }
    socket->ReleaseRef();
    return sent;
}

// L2Authd.exe 0x0046DD40, Src/paservermgr.cpp:593-620.
bool PAServerMgr::SendExternalTokenLoginReq(
    std::int32_t tid,
    int companyNo,
    unsigned char* externalToken,
    int affiliatedGameId,
    int gameId) {
    CPASocket* const socket = GetPASocket();
    if (socket == nullptr) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=pa operation=login_external_token transaction=%d reason=no_connection action=return_failure",
        //     tid)
        // Log.AddLog(LOG_WARN,
        //     "[PA] Cannot submit the external token login request %d: no connection to the service. Returning failure.",
        //     tid)
        L2_LOG_WARN(Log,
            "[PA] Cannot submit the external token login request %d: no connection to the service. Returning failure.",
            tid);
        return false;
    }
    const std::uint32_t socketId = socket->m_hSocket;
    const bool sent = socket->Send(
        "cddbdd",
        AP_AA_EXTERNAL_TOKEN_REQ,
        tid,
        companyNo,
        0x400,
        externalToken,
        affiliatedGameId,
        gameId);
    if (!sent) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=pa operation=login_external_token transaction=%d socket=%u reason=socket_send_rejected action=return_failure",
        //     tid, socketId)
        // Log.AddLog(LOG_WARN,
        //     "[PA] Could not send the external token login request %d on socket %u. Returning failure.",
        //     tid, socketId)
        // L2_LOG_WARN(Log,
        //     "[PA] Could not send the external token login request %d on socket %u. Returning failure.",
        //     tid, socketId)
        L2_LOG_WARN(Log,
            "[PA] The external token login request %d was not submitted on socket %u: the selected connection is closed or invalid. Returning failure.",
            tid, socketId);
    }
    socket->ReleaseRef();
    return sent;
}

// L2Authd.exe 0x0046DDA0, Src/paservermgr.cpp:624-653.
bool PAServerMgr::SendAuxAuthenticationAck(
    const unsigned char* packet) {
    try {
        const std::int32_t originalTid =
            GetIntFromPacket(&packet);
        const std::int32_t tid =
            GetIntFromPacket(&packet);
        const char tokenType =
            GetCharFromPacket(&packet);
        CPASocket* const socket = GetPASocket();
        if (socket == nullptr) {
            // Log.AddLog(LOG_WARN,
            //     "component=authentication event=request_not_submitted peer=pa operation=aux_authentication_ack transaction=%d reason=no_connection action=return_failure",
            //     tid)
            // Log.AddLog(LOG_WARN,
            //     "[PA] Cannot send the secondary authentication acknowledgement for transaction %d: no connection to the service. Returning failure.",
            //     tid)
            L2_LOG_WARN(Log,
                "[PA] Cannot send the secondary authentication acknowledgement for transaction %d: no connection to the service. Returning failure.",
                tid);
            return false;
        }
        const std::uint32_t socketId = socket->m_hSocket;
        const bool sent = socket->Send(
            "cddcb",
            AP_AA_AUX_AUTHENTICATION_ACK,
            originalTid,
            tid,
            tokenType,
            0x40,
            packet);
        if (!sent) {
            // Log.AddLog(LOG_WARN,
            //     "component=authentication event=request_not_submitted peer=pa operation=aux_authentication_ack transaction=%d socket=%u reason=socket_send_rejected action=return_legacy_true",
            //     tid, socketId)
            // Log.AddLog(LOG_WARN,
            //     "[PA] Could not send the secondary authentication acknowledgement for transaction %d on socket %u. This path returns true when sending fails.",
            //     tid, socketId)
            // L2_LOG_WARN(Log,
            //     "[PA] Could not send the secondary authentication acknowledgement for transaction %d on socket %u. This path returns true when sending fails.",
            //     tid, socketId)
            L2_LOG_WARN(Log,
                "[PA] Secondary authentication acknowledgement for transaction %d was not submitted on socket %u: the selected connection is closed or invalid. This path still returns true.",
                tid, socketId);
        }
        socket->ReleaseRef();
        // The logical inversion is present in the shipping implementation.
        return !sent;
    } catch (...) {
        // ELOG(
        // "*%s:%d(%s) exception\n",
        // "PAServerMgr.cpp",
        // 650,
        // "Wed Oct 23 17:02:11 2019")
        ELOG(
            "[PAServerMgr.cpp] Exception while sending the secondary authentication acknowledgement. Entering exception handling.\n");
        send_exception(true);
        // The x86 source reads an uninitialized result if parsing throws.
        // False is the deterministic portable value for that exceptional path.
        return false;
    }
}

// L2Authd.exe 0x0046DEE0, Src/paservermgr.cpp:657-688.
bool PAServerMgr::SendExternalUserInfoReq(
    std::int32_t tid,
    int uid) {
    CPASocket* const socket = GetPASocket();
    if (socket == nullptr) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=pa operation=get_external_user_info transaction=%d reason=no_connection action=return_failure",
        //     tid)
        // Log.AddLog(LOG_WARN,
        //     "[PA] Cannot submit the external user information request %d: no connection to the service. Returning failure.",
        //     tid)
        L2_LOG_WARN(Log,
            "[PA] Cannot submit the external user information request %d: no connection to the service. Returning failure.",
            tid);
        return false;
    }
    const std::uint32_t socketId = socket->m_hSocket;
    const bool sent = socket->Send(
        "cdddddd",
        AP_GET_EXTERNAL_USER_INFO_REQ,
        tid,
        0,
        config.affiliatedGameId,
        PAServiceCode(),
        config.externalFatigueCompanyCode,
        uid);
    if (!sent) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=pa operation=get_external_user_info transaction=%d socket=%u reason=socket_send_rejected action=return_failure",
        //     tid, socketId)
        // Log.AddLog(LOG_WARN,
        //     "[PA] Could not send the external user information request %d on socket %u. Returning failure.",
        //     tid, socketId)
        // L2_LOG_WARN(Log,
        //     "[PA] Could not send the external user information request %d on socket %u. Returning failure.",
        //     tid, socketId)
        L2_LOG_WARN(Log,
            "[PA] The external user information request %d was not submitted on socket %u: the selected connection is closed or invalid. Returning failure.",
            tid, socketId);
    }
    socket->ReleaseRef();
    return sent;
}

// L2Authd.exe 0x0046E2D0, Src/paservermgr.cpp:691-717.
bool PAServerMgr::SendGetUserInfoReq(
    std::int32_t tid,
    char* account) {
    CPASocket* const socket = GetPASocket();
    if (socket == nullptr) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=pa operation=get_user_info transaction=%d reason=no_connection action=return_failure",
        //     tid)
        // Log.AddLog(LOG_WARN,
        //     "[PA] Cannot submit the user information request %d: no connection to the service. Returning failure.",
        //     tid)
        L2_LOG_WARN(Log,
            "[PA] Cannot submit the user information request %d: no connection to the service. Returning failure.",
            tid);
        return false;
    }
    const std::uint32_t socketId = socket->m_hSocket;
    const bool sent = socket->Send(
        "cddds",
        AP_GET_USER_INFO_REQ,
        tid,
        config.affiliatedGameId,
        PAServiceCode(),
        account);
    if (!sent) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=pa operation=get_user_info transaction=%d socket=%u reason=socket_send_rejected action=return_failure",
        //     tid, socketId)
        // Log.AddLog(LOG_WARN,
        //     "[PA] Could not send the user information request %d on socket %u. Returning failure.",
        //     tid, socketId)
        // L2_LOG_WARN(Log,
        //     "[PA] Could not send the user information request %d on socket %u. Returning failure.",
        //     tid, socketId)
        L2_LOG_WARN(Log,
            "[PA] The user information request %d was not submitted on socket %u: the selected connection is closed or invalid. Returning failure.",
            tid, socketId);
    }
    socket->ReleaseRef();
    return sent;
}

// L2Authd.exe 0x0046E350, Src/paservermgr.cpp:721-758.
bool PAServerMgr::SendRequestAuthnTokenReq(
    int tid,
    int portalUid,
    in_addr loginIp,
    char* targetAppId,
    _GUID fromAppId,
    _GUID sessionId) {
    CPASocket* const socket = GetPASocket();
    if (socket == nullptr) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=pa operation=request_authn_token transaction=%d reason=no_connection action=return_failure",
        //     tid)
        // Log.AddLog(LOG_WARN,
        //     "[PA] Cannot submit the authentication token request %d: no connection to the service. Returning failure.",
        //     tid)
        L2_LOG_WARN(Log,
            "[PA] Cannot submit the authentication token request %d: no connection to the service. Returning failure.",
            tid);
        return false;
    }

    _GUID targetGuid{};
    if (targetAppId == nullptr ||
        targetAppId[36] != '\0' ||
        std::strlen(targetAppId) != 36 ||
        !authd::platform::ParseGuid(
            targetAppId,
            targetGuid)) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=target_app_fallback peer=pa operation=request_authn_token transaction=%d reason=invalid_or_missing_guid action=use_configured_app_id",
        //     tid)
        // Log.AddLog(LOG_WARN,
        //     "[PA] Authentication token request %d has a missing or invalid target GUID. Using the configured application ID.",
        //     tid)
        L2_LOG_WARN(Log,
            "[PA] Authentication token request %d has a missing or invalid target GUID. Using the configured application ID.",
            tid);
        targetGuid = config.appId;
    }

    const std::uint32_t socketId = socket->m_hSocket;
    const bool sent = socket->Send(
        "cddddbbb",
        AP_REQUEST_AUTHN_TOKEN_REQ,
        tid,
        PAServiceCode(),
        portalUid,
        loginIp.s_addr,
        0x10,
        &targetGuid,
        0x10,
        &fromAppId,
        0x10,
        &sessionId);
    if (!sent) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=pa operation=request_authn_token transaction=%d socket=%u reason=socket_send_rejected action=return_failure",
        //     tid, socketId)
        // Log.AddLog(LOG_WARN,
        //     "[PA] Could not send the authentication token request %d on socket %u. Returning failure.",
        //     tid, socketId)
        // L2_LOG_WARN(Log,
        //     "[PA] Could not send the authentication token request %d on socket %u. Returning failure.",
        //     tid, socketId)
        L2_LOG_WARN(Log,
            "[PA] The authentication token request %d was not submitted on socket %u: the selected connection is closed or invalid. Returning failure.",
            tid, socketId);
    }
    socket->ReleaseRef();
    return sent;
}

// L2Authd.exe 0x0046E4E0, Src/paservermgr.cpp:765-790.
bool PAServerMgr::SendLoginToken(
    std::int32_t tid,
    const char* authProviderCode,
    const char* authnToken,
    in_addr loginIp) {
    CPASocket* const socket = GetPASocket();
    if (socket == nullptr) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=pa operation=login_token transaction=%d reason=no_connection action=return_failure",
        //     tid)
        // Log.AddLog(LOG_WARN,
        //     "[PA] Cannot submit the token login request %d: no connection to the service. Returning failure.",
        //     tid)
        L2_LOG_WARN(Log,
            "[PA] Cannot submit the token login request %d: no connection to the service. Returning failure.",
            tid);
        return false;
    }
    const std::uint32_t socketId = socket->m_hSocket;
    const bool sent = socket->Send(
        "cddssdb",
        AP_NP_LOGIN_TOKEN_REQ,
        tid,
        PAServiceCode(),
        authProviderCode,
        authnToken,
        loginIp.s_addr,
        0x10,
        &config.clientAppId);
    if (!sent) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=pa operation=login_token transaction=%d socket=%u reason=socket_send_rejected action=return_failure",
        //     tid, socketId)
        // Log.AddLog(LOG_WARN,
        //     "[PA] Could not send the token login request %d on socket %u. Returning failure.",
        //     tid, socketId)
        // L2_LOG_WARN(Log,
        //     "[PA] Could not send the token login request %d on socket %u. Returning failure.",
        //     tid, socketId)
        L2_LOG_WARN(Log,
            "[PA] The token login request %d was not submitted on socket %u: the selected connection is closed or invalid. Returning failure.",
            tid, socketId);
    }
    socket->ReleaseRef();
    return sent;
}

// L2Authd.exe 0x0046DF70, Src/paservermgr.cpp:967-1017.
bool PAServerMgr::RecvGetUserInfoAck(
    const unsigned char* packet) {
    auto* const data = const_cast<VerifyAccountData*>(
        reinterpret_cast<const VerifyAccountData*>(
            packet));
    PAUser* user = nullptr;
    if (!DelUserWait(data->tid, &user)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "User infomation was deleted while getting user info from PA(tid: %d)",
        //     data->tid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=reply_without_pending_request peer=pa operation=get_user_info transaction=%d action=ignore_reply",
        //     data->tid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[PA] Ignoring user information reply %d: no request is pending.",
        //     data->tid)
        // L2_LOG_WARN(Log,
        //     "[PA] Ignoring user information reply %d: no request is pending.",
        //     data->tid)
        L2_LOG_WARN(Log,
            "[PA] Ignoring user information reply %d (peer reason %d): no request is pending.",
            data->tid, data->reason);
        return false;
    }

    if (user->origin == 0x1c) {
        if (data->reason != 0) {
            // Log.AddLog(LOG_NORMAL,
            //     "component=authentication event=login_rejected peer=pa operation=get_user_info transaction=%d server_id=%d reason_code=%d action=send_world_failure",
            //     data->tid, user->serverid, data->reason)
            // Log.AddLog(LOG_NORMAL,
            //     "[PA] User information request %d for server %d rejected login (reason %d). Sending failure to the world server.",
            //     data->tid, user->serverid, data->reason)
            L2_LOG_INFO(Log,
                "[PA] User information request %d for server %d rejected login (reason %d). Sending failure to the world server.",
                data->tid, user->serverid, data->reason);
            const WorldServer world =
                g_ServerList.GetAt(user->serverid);
            SendSocket(
                world.inner_addr,
                "cdc",
                0x15,
                user->preUser.account.uid,
                MapPAToAuthReason(data->reason));
            user->ReleaseRef();
            return false;
        }

        if (data->portalUid != 0) {
            data->portalAccount[14] = '\0';
            std::strcpy(
                user->preUser.portalUser.account,
                data->portalAccount);
            PortalUser& portal = user->preUser.portalUser;
            portal.uid = data->portalUid;
            portal.birthdate = data->birthdate;
            portal.sex = data->sex;
            portal.blockCode = data->blockCode;
            portal.noticeCode = data->noticeCode;
            portal.statusCode = data->statusCode;
            portal.blockReasonCode =
                data->blockReasonCode;
            std::strcpy(
                user->preUser.playAccount,
                data->gameAccount);
            std::memcpy(
                &portal.userId,
                data->userId,
                sizeof(portal.userId));
        }

        const char result = accountdb.LoginServerSide(
            user->preUser.account.uid,
            nullptr,
            user->preUser,
            user->serverid,
            user->stat,
            user->clientIp);
        if (result != 0) {
            // Log.AddLog(LOG_NORMAL,
            //     "component=authentication event=login_rejected peer=pa operation=get_user_info transaction=%d server_id=%d phase=local_authentication reason_code=%d action=send_world_failure",
            //     data->tid, user->serverid, static_cast<int>(result))
            // Log.AddLog(LOG_NORMAL,
            //     "[PA] Local authentication failed after user information request %d for server %d (reason %d). Sending failure to the world server.",
            //     data->tid, user->serverid, static_cast<int>(result))
            L2_LOG_INFO(Log,
                "[PA] Local authentication failed after user information request %d for server %d (reason %d). Sending failure to the world server.",
                data->tid, user->serverid, static_cast<int>(result));
            const WorldServer world =
                g_ServerList.GetAt(user->serverid);
            SendSocket(
                world.inner_addr,
                "cdc",
                0x15,
                user->preUser.account.uid,
                result);
        }
    }

    if (user->origin != 0x1c) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=unsupported_request_origin peer=pa operation=get_user_info transaction=%d origin=%d action=release_request_without_reply",
        //     data->tid, user->origin)
        // Log.AddLog(LOG_WARN,
        //     "[PA] Cannot handle user information reply %d: request origin %d is unsupported. Releasing the request without replying.",
        //     data->tid, user->origin)
        L2_LOG_WARN(Log,
            "[PA] Cannot handle user information reply %d: request origin %d is unsupported. Releasing the request without replying.",
            data->tid, user->origin);
    }

    user->ReleaseRef();
    return true;
}

// L2Authd.exe 0x0046E570, Src/paservermgr.cpp:799-856.
bool PAServerMgr::RecvVerifyAccountAck(
    const unsigned char* packet) {
    try {
        const auto* const response =
            reinterpret_cast<const VerifyUserResponse*>(
                packet);
        PAUser* user = nullptr;
        if (!DelUserWait(
                response->verifyAccountData.tid,
                &user)) {
            // Log.AddLog(
            //     LOG_ERROR,
            //     "User infomation was deleted while getting payment info from PAS(%d)",
            //     response->verifyAccountData.tid)
            // Log.AddLog(
            //     LOG_WARN,
            //     "component=authentication event=reply_without_pending_request peer=pa operation=verify_account transaction=%d action=ignore_reply",
            //     response->verifyAccountData.tid)
            // Log.AddLog(
            //     LOG_WARN,
            //     "[PA] Ignoring account verification reply %d: no request is pending.",
            //     response->verifyAccountData.tid)
            // L2_LOG_WARN(Log,
            //     "[PA] Ignoring account verification reply %d: no request is pending.",
            //     response->verifyAccountData.tid)
            L2_LOG_WARN(Log,
                "[PA] Ignoring account verification reply %d (peer reason %d): no request is pending.",
                response->verifyAccountData.tid, response->verifyAccountData.reason);
            return false;
        }

        std::memcpy(
            &user->preUser.sessionId,
            response->sessionId,
            sizeof(user->preUser.sessionId));
        if (response->verifyAccountData.reason == 0x13) {
            const unsigned char* cursor = packet;
            GetIntFromPacket(&cursor);
            GetIntFromPacket(&cursor);
            GetIntFromPacket(&cursor);
            const std::int32_t banReasonCode =
                GetIntFromPacket(&cursor);
            // Log.AddLog(LOG_NORMAL,
            //     "component=authentication event=login_rejected peer=pa operation=verify_account transaction=%d reason_code=19 ban_reason=%d action=send_ban_reply references=retained",
            //     response->verifyAccountData.tid, banReasonCode)
            // Log.AddLog(LOG_NORMAL,
            //     "[PA] Account verification request %d rejected login with reason 19 (ban reason %d). Sending the ban reply. Acquired references are retained.",
            //     response->verifyAccountData.tid, banReasonCode)
            L2_LOG_INFO(Log,
                "[PA] Account verification request %d rejected login with reason 19 (ban reason %d). Sending the ban reply; acquired references are retained.",
                response->verifyAccountData.tid, banReasonCode);
            user->pSession->Send(
                "cdd",
                2,
                0,
                banReasonCode);
            // The selected branch neither releases this removed PAUser nor
            // its owned references.
            return false;
        }

        bool result = false;
        if (user->origin == ORIGIN_CLIENT_LOGIN) {
            result = ClientLoginCallback(
                const_cast<VerifyAccountData*>(
                    &response->verifyAccountData),
                user);
        } else if (user->origin ==
                   ORIGIN_PETITION_LOGIN) {
            result = PetitionLoginCallback(
                const_cast<VerifyAccountData*>(
                    &response->verifyAccountData),
                user);
        }
        // Other origins return an uninitialized byte in the target. False is
        // the deterministic portable value. The two product paths are exact.
        if (user->origin != ORIGIN_CLIENT_LOGIN && user->origin != ORIGIN_PETITION_LOGIN) {
            // Log.AddLog(LOG_WARN,
            //     "component=authentication event=unsupported_request_origin peer=pa operation=verify_account transaction=%d origin=%d action=release_request_without_reply",
            //     response->verifyAccountData.tid, user->origin)
            // Log.AddLog(LOG_WARN,
            //     "[PA] Cannot handle account verification reply %d: request origin %d is unsupported. Releasing the request without replying.",
            //     response->verifyAccountData.tid, user->origin)
            L2_LOG_WARN(Log,
                "[PA] Cannot handle account verification reply %d: request origin %d is unsupported. Releasing the request without replying.",
                response->verifyAccountData.tid, user->origin);
        }

        user->ReleaseRef();
        return result;
    } catch (...) {
        // ELOG(
        // "*%s:%d(%s) exception\n",
        // "PAServerMgr.cpp",
        // 852,
        // "Wed Oct 23 17:02:11 2019")
        ELOG(
            "[PAServerMgr.cpp] Exception while processing the account verification reply. Entering exception handling.\n");
        send_exception(true);
        return false;
    }
}

// L2Authd.exe 0x0046E740, Src/paservermgr.cpp:860-889.
bool PAServerMgr::RecvExternalTokenLoginAck(
    const unsigned char* packet) {
    try {
        auto* const data = const_cast<VerifyAccountData*>(
            reinterpret_cast<const VerifyAccountData*>(
                packet));
        PAUser* user = nullptr;
        if (!DelUserWait(data->tid, &user)) {
            // Log.AddLog(
            //     LOG_ERROR,
            //     "User infomation was deleted while getting payment info from PAS(%d)",
            //     data->tid)
            // Log.AddLog(
            //     LOG_WARN,
            //     "component=authentication event=reply_without_pending_request peer=pa operation=login_external_token transaction=%d action=ignore_reply",
            //     data->tid)
            // Log.AddLog(
            //     LOG_WARN,
            //     "[PA] Ignoring external token login reply %d: no request is pending.",
            //     data->tid)
            // L2_LOG_WARN(Log,
            //     "[PA] Ignoring external token login reply %d: no request is pending.",
            //     data->tid)
            L2_LOG_WARN(Log,
                "[PA] Ignoring external token login reply %d (peer reason %d): no request is pending.",
                data->tid, data->reason);
            return false;
        }

        bool result = false;
        if (user->origin == ORIGIN_CLIENT_LOGIN) {
            result = ClientLoginCallback(data, user);
        } else if (user->origin ==
                   ORIGIN_PETITION_LOGIN) {
            result = PetitionLoginCallback(data, user);
        }
        // As above, an invalid origin exposes an uninitialized target byte.
        if (user->origin != ORIGIN_CLIENT_LOGIN && user->origin != ORIGIN_PETITION_LOGIN) {
            // Log.AddLog(LOG_WARN,
            //     "component=authentication event=unsupported_request_origin peer=pa operation=login_external_token transaction=%d origin=%d action=release_request_without_reply",
            //     data->tid, user->origin)
            // Log.AddLog(LOG_WARN,
            //     "[PA] Cannot handle external token login reply %d: request origin %d is unsupported. Releasing the request without replying.",
            //     data->tid, user->origin)
            L2_LOG_WARN(Log,
                "[PA] Cannot handle external token login reply %d: request origin %d is unsupported. Releasing the request without replying.",
                data->tid, user->origin);
        }

        user->ReleaseRef();
        return result;
    } catch (...) {
        // ELOG(
        // "*%s:%d(%s) exception\n",
        // "PAServerMgr.cpp",
        // 887,
        // "Wed Oct 23 17:02:11 2019")
        ELOG(
            "[PAServerMgr.cpp] Exception while processing the external-token login reply. Entering exception handling.\n");
        send_exception(true);
        return false;
    }
}

// L2Authd.exe 0x0046E880, Src/paservermgr.cpp:893-917.
bool PAServerMgr::RecvAuxAuthenticationReq(
    const unsigned char* packet) {
    try {
        const std::int32_t originalTid =
            GetIntFromPacket(&packet);
        const std::int32_t tid =
            GetIntFromPacket(&packet);
        const char tokenType =
            GetCharFromPacket(&packet);
        PAUser* const user = GetUserWait(tid);
        if (user == nullptr) {
            // Log.AddLog(
            //     LOG_ERROR,
            //     "Did not found user infomation while processing AuxAuthentication Req from PAS(%d)",
            //     originalTid)
            // Log.AddLog(
            //     LOG_WARN,
            //     "component=authentication event=reply_without_pending_request peer=pa operation=aux_authentication transaction=%d original_transaction=%d action=ignore_reply",
            //     tid, originalTid)
            // Log.AddLog(
            //     LOG_WARN,
            //     "[PA] Ignoring secondary authentication request %d for original transaction %d: no request is pending.",
            //     tid, originalTid)
            L2_LOG_WARN(Log,
                "[PA] Ignoring secondary authentication request %d for original transaction %d: no request is pending.",
                tid, originalTid);
            return false;
        }

        user->pSession->Send(
            "cdcb",
            0x0e,
            originalTid,
            tokenType,
            0x40,
            packet);
        user->ReleaseRef();
        return true;
    } catch (...) {
        // ELOG(
        // "*%s:%d(%s) exception\n",
        // "PAServerMgr.cpp",
        // 915,
        // "Wed Oct 23 17:02:11 2019")
        ELOG(
            "[PAServerMgr.cpp] Exception while processing the secondary authentication request. Entering exception handling.\n");
        send_exception(true);
        // The target forces AL=1 after its catch block.
        return true;
    }
}

// L2Authd.exe 0x0046E9B0, Src/paservermgr.cpp:921-964.
bool PAServerMgr::RecvExternalUserInfoAck(
    const unsigned char* packet) {
    const std::int32_t tid =
        GetIntFromPacket(&packet);
    const std::int32_t reason =
        GetIntFromPacket(&packet);
    [[maybe_unused]] const std::int32_t extraCommand =
        GetIntFromPacket(&packet);

    unsigned char allianceUserKey[256]{};
    std::memcpy(allianceUserKey, packet, 0xff);
    packet += 0xff;
    [[maybe_unused]] const std::int32_t birthday =
        GetIntFromPacket(&packet);
    [[maybe_unused]] const char sex =
        GetCharFromPacket(&packet);
    [[maybe_unused]] char allianceUserAccount[256]{};
    [[maybe_unused]] char customerName[256]{};
    GetStrFromPacket(
        &packet,
        0xff,
        allianceUserAccount);
    GetStrFromPacket(
        &packet,
        0xff,
        customerName);

    PAUser* user = nullptr;
    if (!DelUserWait(tid, &user)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "User infomation was deleted while getting payment info from PAS(%d)",
        //     tid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=reply_without_pending_request peer=pa operation=get_external_user_info transaction=%d action=ignore_reply",
        //     tid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[PA] Ignoring external user information reply %d: no request is pending.",
        //     tid)
        // L2_LOG_WARN(Log,
        //     "[PA] Ignoring external user information reply %d: no request is pending.",
        //     tid)
        L2_LOG_WARN(Log,
            "[PA] Ignoring external user information reply %d (peer reason %d): no request is pending.",
            tid, reason);
        return false;
    }

    if (reason != 0) {
        // Log.AddLog(LOG_NORMAL,
        //     "component=authentication event=login_rejected peer=pa operation=get_external_user_info transaction=%d server_id=%d reason_code=%d action=send_world_failure",
        //     tid, user->serverid, reason)
        // Log.AddLog(LOG_NORMAL,
        //     "[PA] External user information request %d for server %d rejected login (reason %d). Sending failure to the world server.",
        //     tid, user->serverid, reason)
        L2_LOG_INFO(Log,
            "[PA] External user information request %d for server %d rejected login (reason %d). Sending failure to the world server.",
            tid, user->serverid, reason);
        const WorldServer world =
            g_ServerList.GetAt(user->serverid);
        SendSocket(
            world.inner_addr,
            "cdc",
            0x15,
            user->preUser.account.uid,
            MapPAToAuthReason(reason));
        user->ReleaseRef();
        return false;
    }

    const char result = accountdb.LoginServerSide(
        user->preUser.account.uid,
        reinterpret_cast<char*>(allianceUserKey),
        user->preUser,
        user->serverid,
        user->stat,
        user->clientIp);
    if (result != 0) {
        // Log.AddLog(LOG_NORMAL,
        //     "component=authentication event=login_rejected peer=pa operation=get_external_user_info transaction=%d server_id=%d phase=local_authentication reason_code=%d action=send_world_failure",
        //     tid, user->serverid, static_cast<int>(result))
        // Log.AddLog(LOG_NORMAL,
        //     "[PA] Local authentication failed after external user information request %d for server %d (reason %d). Sending failure to the world server.",
        //     tid, user->serverid, static_cast<int>(result))
        L2_LOG_INFO(Log,
            "[PA] Local authentication failed after external user information request %d for server %d (reason %d). Sending failure to the world server.",
            tid, user->serverid, static_cast<int>(result));
        const WorldServer world =
            g_ServerList.GetAt(user->serverid);
        SendSocket(
            world.inner_addr,
            "cdc",
            0x15,
            user->preUser.account.uid,
            result);
    }
    user->ReleaseRef();
    // The local return byte is initialized to zero and never changed.
    return false;
}

// L2Authd.exe 0x0046ECA0, Src/paservermgr.cpp:1022-1074.
bool PAServerMgr::RecvRequestAuthnTokenAck(
    const unsigned char* packet) {
    const std::int32_t tid =
        GetIntFromPacket(&packet);
    PAUser* user = nullptr;
    if (!DelUserWait(tid, &user)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "(RecvRequestAuthnTokenAck)User infomation was deleted while getting info from PAS(%d)",
        //     tid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=reply_without_pending_request peer=pa operation=request_authn_token transaction=%d action=ignore_reply",
        //     tid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[PA] Ignoring authentication token reply %d: no request is pending.",
        //     tid)
        L2_LOG_WARN(Log,
            "[PA] Ignoring authentication token reply %d: no request is pending.",
            tid);
        return false;
    }

    const WorldServer world =
        g_ServerList.GetAt(user->serverid);
    const std::int32_t result =
        GetIntFromPacket(&packet);
    unsigned char emptyToken[101]{};
    std::int32_t tokenLength = 0;
    const unsigned char* token = nullptr;
    if (result == 0) {
        tokenLength = GetIntFromPacket(&packet);
        token = packet;
    } else {
        tokenLength = 101;
        token = emptyToken;
        // Log.AddLog(
        //     LOG_ERROR,
        //     "(RecvRequestAuthnTokenAck) PA Response(%d) is error(%d)",
        //     user->loginUser.portalUid,
        //     result)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=token_request_failed peer=pa operation=request_authn_token transaction=%d server_id=%d reason_code=%d action=send_world_failure token_payload=zero_filled token_bytes=101",
        //     tid, user->serverid, result)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[PA] Authentication token request %d for server %d failed (reason %d). Sending failure to the world server with a 101-byte zero-filled token.",
        //     tid, user->serverid, result)
        L2_LOG_WARN(Log,
            "[PA] Authentication token request %d for server %d failed (reason %d). Sending failure to the world server with a 101-byte zero-filled token.",
            tid, user->serverid, result);
    }

    const int forwarded = SendSocket(
        world.inner_addr,
        "cddb",
        0x21,
        user->loginUser.portalUid,
        MapPAToAuthReason(result),
        tokenLength,
        token);
    if (forwarded == 0) {
        L2_LOG_WARN(Log,
            "[PA] Authentication token reply %d (peer result %d) was not forwarded to world server %d: no connection exists for its internal address. Releasing the pending request without a world reply.",
            tid, result, user->serverid);
    }
    user->ReleaseRef();
    return true;
}

// L2Authd.exe 0x0046EE50, Src/paservermgr.cpp:1078-1117.
bool PAServerMgr::RecvLoginTokenAck(
    const unsigned char* packet) {
    try {
        const auto* const response =
            reinterpret_cast<const LoginTokenResponse*>(
                packet);
        PAUser* user = nullptr;
        if (!DelUserWait(
                response->verifyAccountData.tid,
                &user)) {
            // Log.AddLog(
            //     LOG_ERROR,
            //     "User infomation was deleted while getting login info from PAS(%d)",
            //     response->verifyAccountData.tid)
            // Log.AddLog(
            //     LOG_WARN,
            //     "component=authentication event=reply_without_pending_request peer=pa operation=login_token transaction=%d action=ignore_reply",
            //     response->verifyAccountData.tid)
            // Log.AddLog(
            //     LOG_WARN,
            //     "[PA] Ignoring token login reply %d: no request is pending.",
            //     response->verifyAccountData.tid)
            // L2_LOG_WARN(Log,
            //     "[PA] Ignoring token login reply %d: no request is pending.",
            //     response->verifyAccountData.tid)
            L2_LOG_WARN(Log,
                "[PA] Ignoring token login reply %d (peer reason %d): no request is pending.",
                response->verifyAccountData.tid, response->verifyAccountData.reason);
            return false;
        }

        std::memcpy(
            &user->preUser.sessionId,
            response->sessionId,
            sizeof(user->preUser.sessionId));
        if (response->verifyAccountData.reason == 0x13) {
            const unsigned char* cursor = packet;
            GetIntFromPacket(&cursor);
            GetIntFromPacket(&cursor);
            GetIntFromPacket(&cursor);
            const std::int32_t banReasonCode =
                GetIntFromPacket(&cursor);
            // Log.AddLog(LOG_NORMAL,
            //     "component=authentication event=login_rejected peer=pa operation=login_token transaction=%d reason_code=19 ban_reason=%d action=send_ban_reply references=retained",
            //     response->verifyAccountData.tid, banReasonCode)
            // Log.AddLog(LOG_NORMAL,
            //     "[PA] Token login request %d rejected login with reason 19 (ban reason %d). Sending the ban reply. Acquired references are retained.",
            //     response->verifyAccountData.tid, banReasonCode)
            L2_LOG_INFO(Log,
                "[PA] Token login request %d rejected login with reason 19 (ban reason %d). Sending the ban reply; acquired references are retained.",
                response->verifyAccountData.tid, banReasonCode);
            user->pSession->Send(
                "cdd",
                2,
                0,
                banReasonCode);
            // This selected branch keeps the removed PAUser.
            return false;
        }

        if (user->origin == ORIGIN_CLIENT_LOGIN) {
            ClientLoginCallback(
                const_cast<VerifyAccountData*>(
                    &response->verifyAccountData),
                user);
        } else if (user->origin ==
                   ORIGIN_PETITION_LOGIN) {
            PetitionLoginCallback(
                const_cast<VerifyAccountData*>(
                    &response->verifyAccountData),
                user);
        }
        if (user->origin != ORIGIN_CLIENT_LOGIN && user->origin != ORIGIN_PETITION_LOGIN) {
            // Log.AddLog(LOG_WARN,
            //     "component=authentication event=unsupported_request_origin peer=pa operation=login_token transaction=%d origin=%d action=release_request_without_reply",
            //     response->verifyAccountData.tid, user->origin)
            // Log.AddLog(LOG_WARN,
            //     "[PA] Cannot handle token login reply %d: request origin %d is unsupported. Releasing the request without replying.",
            //     response->verifyAccountData.tid, user->origin)
            L2_LOG_WARN(Log,
                "[PA] Cannot handle token login reply %d: request origin %d is unsupported. Releasing the request without replying.",
                response->verifyAccountData.tid, user->origin);
        }

        user->ReleaseRef();
        // Unlike RecvVerifyAccountAck, the selected normal path forces true
        // regardless of the callback result.
        return true;
    } catch (...) {
        // ELOG(
        // "*%s:%d(%s) exception\n",
        // "PAServerMgr.cpp",
        // 1115,
        // "Wed Oct 23 17:02:11 2019")
        ELOG(
            "[PAServerMgr.cpp] Exception while processing the token-login reply. Entering exception handling.\n");
        send_exception(true);
        return true;
    }
}

// L2Authd.exe 0x0046F010, Src/paservermgr.cpp:1121-1178.
bool PAServerMgr::ClientLoginCallback(
    VerifyAccountData* pData,
    PAUser* pPAUser) {
    try {
        CClientSession* const session =
            pPAUser->pSession;
        if (pData->reason == 0x0e) {
            pPAUser->preUser.account
                .externalAuthAdditionalInfo = 1;
        } else if (pData->reason != 0) {
            // Log.AddLog(LOG_NORMAL,
            //     "component=authentication event=login_rejected peer=pa transaction=%d reason_code=%d action=send_client_failure",
            //     pData->tid, pData->reason)
            // Log.AddLog(LOG_NORMAL,
            //     "[PA] Login rejected for request %d (reason %d). Sending failure to the client.",
            //     pData->tid, pData->reason)
            L2_LOG_INFO(Log,
                "[PA] Login rejected for request %d (reason %d). Sending failure to the client.",
                pData->tid, pData->reason);
            session->SendFail(
                1,
                static_cast<char>(
                    MapPAToAuthReason(
                        pData->reason)),
                nullptr);
            return false;
        }

        pPAUser->preUser.nUserLevel =
            pData->userLevel;
        if (pData->portalUid == 0) {
            pPAUser->preUser.portalUser.uid = 0;
            if (!accountdb.AuthenticateUser(
                    session,
                    &pPAUser->preUser,
                    false)) {
                return false;
            }
            return accountdb.AuthorizeUser(
                session,
                pPAUser->preUser);
        }

        pData->portalAccount[14] = '\0';
        std::strcpy(
            pPAUser->preUser.portalUser.account,
            pData->portalAccount);
        PortalUser& portal =
            pPAUser->preUser.portalUser;
        portal.uid = pData->portalUid;
        portal.birthdate = pData->birthdate;
        portal.sex = pData->sex;
        portal.blockCode = pData->blockCode;
        portal.noticeCode = pData->noticeCode;
        portal.statusCode = pData->statusCode;
        portal.blockReasonCode =
            pData->blockReasonCode;
        std::strcpy(
            pPAUser->preUser.playAccount,
            pData->gameAccount);
        std::memcpy(
            &portal.userId,
            pData->userId,
            sizeof(portal.userId));
        if (!accountdb.AuthenticateUserPortal(
                session,
                &pPAUser->preUser)) {
            return false;
        }
        return accountdb.AuthorizeUser(
            session,
            pPAUser->preUser);
    } catch (...) {
        // ELOG(
        // "*%s:%d(%s) exception\n",
        // "PAServerMgr.cpp",
        // 1176,
        // "Wed Oct 23 17:02:11 2019")
        ELOG(
            "[PAServerMgr.cpp] Exception while processing the client-login callback. Entering exception handling.\n");
        send_exception(true);
        return false;
    }
}

// L2Authd.exe 0x0046F270, Src/paservermgr.cpp:1182-1185.
bool PAServerMgr::PetitionLoginCallback(
    VerifyAccountData* pData,
    PAUser* pPAUser) {
    OCHKRequestCallback(
        pData->reason,
        static_cast<CSocketInt*>(
            pPAUser->pIOSocket),
        pData->gameAccount);
    return true;
}
