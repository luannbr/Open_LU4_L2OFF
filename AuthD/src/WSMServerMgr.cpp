#include "WSMServerMgr.h"

#include "Shared/src/ScopedLockGuards.h"

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
#include "ServerList.h"
#include "SocketServerEx.h"
#include "Thread.h"
#include "Util.h"
#include "WSMSocket.h"

#include <cstdint>
#include <cstring>
#include <ctime>

WSMServerMgr* WSMServerMgr::sInstance = nullptr;

namespace {

struct WSMConnection {
    std::uint32_t socket = UINT32_C(0xffffffff);
    bool connected = false;
};

WSMConnection ConnectWSMServer() {
    WSMConnection connection;
    const std::uint32_t peerAddress = ntohl(config.WSMIP.s_addr);
    connection.connected =
        authd::platform::ConnectNativeSocket(
            connection.socket,
            config.WSMIP,
            static_cast<std::uint16_t>(
                config.WSMPort));
    if (!connection.connected) {
        // Log.AddLog(LOG_WARN,
        //     "component=network event=connect_failed peer=wsm address=%u.%u.%u.%u port=%u socket=%u error_domain=winsock_compatible code=%d action=close_socket",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.WSMPort)),
        //     connection.socket, ::portable::GetNativeSocketLastError())
        // Log.AddLog(LOG_WARN,
        //     "[WSM] Could not connect to %u.%u.%u.%u:%u on socket %u (WinSock-compatible error %d). Closing the socket.",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.WSMPort)),
        //     connection.socket, ::portable::GetNativeSocketLastError())
        // L2_LOG_WARN(Log,
        //     "[WSM] Could not connect to %u.%u.%u.%u:%u on socket %u (WinSock-compatible error %d). Closing the socket.",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.WSMPort)),
        //     connection.socket, ::portable::GetNativeSocketLastError())
        L2_LOG_WARN(Log,
            "[WSM] Could not connect to %u.%u.%u.%u:%u on socket %u (WinSock-compatible error %d). %s",
            (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            (peerAddress >> 8U) & 255U, peerAddress & 255U,
            static_cast<unsigned int>(static_cast<std::uint16_t>(config.WSMPort)),
            connection.socket, ::portable::GetNativeSocketLastError(),
            connection.socket == UINT32_C(0xffffffff)
                ? "No valid socket was created; the close path will not schedule a retry."
                : "Closing the socket; its close callback will request a retry.");
    }
    return connection;
}

} // namespace

// L2Authd.exe 0x00493CC0, Src/wsmservermgr.cpp:25-38.
WSMUser* WSMUser::Alloc() {
    auto* const user = new WSMUser;
    user->pIOSocket = nullptr;
    user->pSession = nullptr;
    user->mRefCount = 0;
    user->opTime = 0;
    user->AddRef();
    user->origin = 0;
    std::memset(
        &user->webSessionId,
        0,
        sizeof(user->webSessionId));
    std::memset(
        &user->uniqueId,
        0,
        sizeof(user->uniqueId));
    user->tokenServerId = 0;
    return user;
}

// L2Authd.exe 0x00493DB0, Src/wsmservermgr.cpp:42-44.
void WSMUser::AddRef() {
    runtime::raw_atomic::IncrementI32(
        &mRefCount);
}

// L2Authd.exe 0x00493DD0, Src/wsmservermgr.cpp:48-51.
void WSMUser::ReleaseRef() {
    if (runtime::raw_atomic::DecrementI32(
            &mRefCount) == 0) {
        ::operator delete(this);
    }
}

// L2Authd.exe 0x00493E00, Src/wsmservermgr.cpp:62-70.
WSMServerMgr::WSMServerMgr()
    : mUserMapLock(eCriticalSection, 0) {
    if (sInstance == nullptr) {
        sInstance = this;
        mWSMServerConnected = false;
        mpWSMSocket = nullptr;
        mhWSMServerTimer = nullptr;
        mTransCount = 0;
    }
}

// L2Authd.exe 0x00493EB0, Src/wsmservermgr.cpp:74-76.
WSMServerMgr::~WSMServerMgr() {
    sInstance = nullptr;
}

// L2Authd.exe 0x00493F20, Src/wsmservermgr.cpp:122-135.
void WSMServerMgr::Initialize() {
    const WSMConnection connection =
        ConnectWSMServer();
    mpWSMSocket = CWSMSocket::Allocate(
        connection.socket);
    if (!connection.connected) {
        mpWSMSocket->CloseSocket();
    } else {
        authd::platform::InitializeNativeSocket(
            mpWSMSocket,
            connection.socket,
            g_hIOCompletionPort);
    }
}

// L2Authd.exe 0x00493FC0, Src/wsmservermgr.cpp:80-118.
void WSMServerMgr::WSMServerTimerRoutine() {
    if (mhWSMServerTimer != nullptr) {
        runtime::timer_queue::DeleteTimer(
            nullptr,
            mhWSMServerTimer);
        mhWSMServerTimer = nullptr;
    }
    if (mWSMServerConnected) {
        return;
    }

    const WSMConnection connection =
        ConnectWSMServer();
    CWSMSocket* const tempSocket =
        CWSMSocket::Allocate(connection.socket);
    if (!connection.connected) {
        tempSocket->CloseSocket();
        tempSocket->ReleaseRef();
        return;
    }

    CWSMSocket* const swapSocket = mpWSMSocket;
    authd::platform::InitializeNativeSocket(
        tempSocket,
        connection.socket,
        g_hIOCompletionPort);
    runtime::raw_atomic::ExchangePointer(
        &mpWSMSocket,
        tempSocket);
    swapSocket->ReleaseRef();
    mWSMServerConnected = true;
}

// L2Authd.exe 0x004940C0, Src/wsmservermgr.cpp:169-193.
std::int32_t WSMServerMgr::AddUserWait(
    WSMUser* user) {
    user->AddRef();
    user->opTime = static_cast<std::int32_t>(
        std::time(nullptr));

    std::int32_t tid = 0;
    bool inserted = false;
    {
        L2_GUARD(mUserMapLock);
        do {
            tid = runtime::raw_atomic::IncrementI32(
                &mTransCount);
            inserted = mWaitUserMap.insert(
                {tid, user}).second;
        } while (!inserted);
    }

    // This check is present after the successful-insertion loop in the
    // selected image even though the loop makes it unreachable.
    if (!inserted) {
        user->ReleaseRef();
    }
    return tid;
}

// L2Authd.exe 0x00494160, Src/wsmservermgr.cpp:139-165.
bool WSMServerMgr::DelUserWait(
    int tid,
    WSMUser** user) {
    WSMUser* oldUser = nullptr;
    {
        L2_GUARD(mUserMapLock);
        const auto it = mWaitUserMap.find(tid);
        if (it == mWaitUserMap.end()) {
            return false;
        }

        oldUser = it->second;
        mWaitUserMap.erase(it);
    }
    if (user == nullptr) {
        oldUser->ReleaseRef();
    } else {
        *user = oldUser;
    }
    return true;
}

// L2Authd.exe 0x00494220, Src/wsmservermgr.cpp:203-230.
char WSMServerMgr::LoginAccountWsid(
    EORIGIN_CODE origin,
    CClientSession* session,
    CIOSocket* socket,
    PreLoginUser* user,
    WebToken& token) {
    WSMUser* const waitUser = WSMUser::Alloc();
    if (session != nullptr) {
        waitUser->pSession = session;
        session->AddRef();
    }
    if (socket != nullptr) {
        waitUser->pIOSocket = socket;
        socket->AddRef();
    }
    std::memcpy(
        &waitUser->preUser,
        user,
        sizeof(PreLoginUser));
    waitUser->origin = origin;

    const std::int32_t tid = AddUserWait(waitUser);
    if (SendVerifyWebSessionReq(
            tid,
            user->webSessionId,
            token)) {
        waitUser->ReleaseRef();
        return 0;
    }

    // Diagnostic addition: this request previously returned code 20 silently.
    L2_LOG_WARN(Log,
        "[WSM] Web-session verification request %d was not submitted: WSM connection is unavailable. Removing the pending request and returning code 20.",
        tid);
    DelUserWait(tid, nullptr);
    if (socket != nullptr) {
        socket->ReleaseRef();
    }
    if (session != nullptr) {
        session->ReleaseRef();
    }
    waitUser->ReleaseRef();
    return 0x14;
}

// L2Authd.exe 0x00494300, Src/wsmservermgr.cpp:234-255.
char WSMServerMgr::LoginAccount(
    EORIGIN_CODE origin,
    CSocketServer* socket,
    LoginUser* user,
    const unsigned char* token,
    int timeout) {
    WSMUser* const waitUser = WSMUser::Alloc();
    if (socket != nullptr) {
        waitUser->pIOSocket = socket;
        socket->AddRef();
    }
    authd::portable::CopyLoginUserImage(
        waitUser->loginUser,
        *user);
    waitUser->origin = origin;

    const std::int32_t tid = AddUserWait(waitUser);
    if (SendLoginReq(tid, user, token, timeout)) {
        waitUser->ReleaseRef();
        return 0;
    }

    // Diagnostic addition: this request previously returned code 20 silently.
    L2_LOG_WARN(Log,
        "[WSM] Login request %d was not submitted: WSM connection is unavailable. Removing the pending request and returning code 20.",
        tid);
    DelUserWait(tid, nullptr);
    // The selected failure path does not release the socket reference taken
    // above. Preserve that ownership behavior in this baseline.
    waitUser->ReleaseRef();
    return 0x14;
}

// L2Authd.exe 0x004943B0, Src/wsmservermgr.cpp:259-284.
char WSMServerMgr::LogoutAccountWsid(
    EORIGIN_CODE origin,
    CSocketServer* socket,
    LoginUser* user,
    WebSessionId& webSessionId) {
    WSMUser* const waitUser = WSMUser::Alloc();
    if (socket != nullptr) {
        waitUser->pIOSocket = socket;
        socket->AddRef();
    }
    if (user != nullptr) {
        authd::portable::CopyLoginUserImage(
            waitUser->loginUser,
            *user);
    }
    waitUser->origin = origin;

    const std::int32_t tid = AddUserWait(waitUser);
    if (SendUpdateTimeoutReq(tid, webSessionId)) {
        waitUser->ReleaseRef();
        return 0;
    }

    DelUserWait(tid, nullptr);
    // Like LoginAccount, the selected failure path keeps pIOSocket's ref.
    waitUser->ReleaseRef();
    return 0x14;
}

// L2Authd.exe 0x00494450, Src/wsmservermgr.cpp:288-319.
char WSMServerMgr::ExtendLifeTimeWsid(
    EORIGIN_CODE origin,
    CSocketServer* socket,
    LoginUser* user,
    WebSessionId& webSessionId,
    unsigned char* token,
    char tokenServerId) {
    WSMUser* const waitUser = WSMUser::Alloc();
    if (socket != nullptr) {
        waitUser->pIOSocket = socket;
        socket->AddRef();
    }
    if (user != nullptr) {
        authd::portable::CopyLoginUserImage(
            waitUser->loginUser,
            *user);
    }
    waitUser->origin = origin;
    waitUser->webSessionId = webSessionId;
    if (token != nullptr) {
        std::memcpy(
            &waitUser->uniqueId,
            token,
            sizeof(waitUser->uniqueId));
    }
    waitUser->tokenServerId = tokenServerId;

    const std::int32_t tid = AddUserWait(waitUser);
    if (SendVerifyWebSessionReq(
            tid,
            webSessionId,
            waitUser->uniqueId)) {
        waitUser->ReleaseRef();
        return 0;
    }

    DelUserWait(tid, nullptr);
    // The shipping failure path also leaves the socket reference kept.
    waitUser->ReleaseRef();
    return 0x14;
}

// L2Authd.exe 0x00494550, Src/wsmservermgr.cpp:329-344.
bool WSMServerMgr::SendVerifyWebSessionReq(
    std::int32_t tid,
    WebSessionId& webSessionId,
    WebToken& token) {
    const int serviceCode = config.gameId == 8
        ? 0x20
        : 0x1b;
    return mpWSMSocket->Send(
        "cddbdb",
        2,
        tid,
        serviceCode,
        16,
        &webSessionId,
        0,
        16,
        &token);
}

// L2Authd.exe 0x004945B0, Src/wsmservermgr.cpp:348-367.
bool WSMServerMgr::SendUpdateTimeoutReq(
    std::int32_t tid,
    WebSessionId& webSessionId) {
    if (webSessionId.s_id0 == 0 &&
        webSessionId.s_id1 == 0 &&
        webSessionId.s_id2 == 0 &&
        webSessionId.s_id3 == 0) {
        // Diagnostic addition: distinguish an empty session from a connection failure.
        L2_LOG_DEBUG(Log,
            "[WSM] Session-timeout request %d was not submitted: the session ID is empty.",
            tid);
        return false;
    }

    const int serviceCode = config.gameId == 8
        ? 0x20
        : 0x1b;
    // return mpWSMSocket->Send(
    //     "cddbd",
    //     3,
    //     tid,
    //     serviceCode,
    //     16,
    //     &webSessionId,
    //     0x4b0)
    const bool submitted = mpWSMSocket->Send(
        "cddbd",
        3,
        tid,
        serviceCode,
        16,
        &webSessionId,
        0x4b0);
    if (!submitted) {
        L2_LOG_WARN(Log,
            "[WSM] Session-timeout request %d was not submitted: WSM connection is unavailable.",
            tid);
    }
    return submitted;
}

// L2Authd.exe 0x00494630, Src/wsmservermgr.cpp:370-424.
bool WSMServerMgr::SendLoginReq(
    std::int32_t tid,
    LoginUser* user,
    const unsigned char* token,
    int timeout) {
    int serviceCode = 0x0d;
    if (user->portalUid == 0) {
        serviceCode = config.gameId == 8
            ? 0x20
            : 0x1b;
    }

    char account[65]{};
    StrPrintf(
        account,
        sizeof(account),
        "%s",
        user->loginAccount);
    account[64] = 0;

    if (config.gameId == 8 &&
        config.convertPortalEmailAccount &&
        config.convertPortalEmailAccountPostfix != nullptr &&
        user->portalUid != 0 &&
        StrChr(user->loginAccount, 65, '@') == nullptr &&
        StrCmp(
            user->loginAccount,
            user->portalAccount,
            false) != 0) {
        StrPrintf(
            account,
            sizeof(account),
            "%s@%s",
            user->loginAccount,
            config.convertPortalEmailAccountPostfix);
        account[64] = 0;
    }

    return mpWSMSocket->Send(
        "cddbbdbd",
        4,
        tid,
        serviceCode,
        65,
        account,
        32,
        user->loginPassword,
        user->nWsmLoginIp.s_addr,
        16,
        token,
        timeout);
}

// L2Authd.exe 0x004947D0, Src/wsmservermgr.cpp:435-476.
bool WSMServerMgr::RecvVerifyWebSessionAck(
    const unsigned char* packet) {
    auto* const account = const_cast<WSMAccountData*>(
        reinterpret_cast<const WSMAccountData*>(
            packet));
    WSMUser* user = nullptr;
    if (!DelUserWait(account->tid, &user)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "User information was deleted while checking webSessionId from WSM(%d)",
        //     account->tid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=reply_without_pending_request peer=wsm operation=verify_web_session transaction=%d action=ignore_reply",
        //     account->tid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[WSM] Ignoring web-session verification reply %d: no request is pending.",
        //     account->tid)
        // L2_LOG_WARN(Log,
        //     "[WSM] Ignoring web-session verification reply %d: no request is pending.",
        //     account->tid)
        L2_LOG_WARN(Log,
            "[WSM] Ignoring web-session verification reply %d (peer reason %d): no request is pending.",
            account->tid, account->reason);
        return false;
    }

    // For origin values other than 0 and 3, the selected image returns an
    // uninitialized stack byte. False is the deterministic portable result
    // for that impossible caller state. The proven 0/3 paths are unchanged.
    bool result = false;
    if (user->origin == ORIGIN_CLIENT_LOGIN) {
        result = ClientLoginCallback(account, user);
    } else if (user->origin ==
               ORIGIN_GAMESERVER_REQUEST) {
        auto* const serverSocket =
            static_cast<CSocketServer*>(
                user->pIOSocket);
        const WorldServer world = g_ServerList.GetAt(
            serverSocket->serverid);
        const int forwarded = SendSocket(
            world.inner_addr,
            "cdcbbc",
            0x1b,
            user->loginUser.uid,
            MapWSMToAuthReason(account->reason),
            16,
            &user->webSessionId,
            16,
            &user->uniqueId,
            user->tokenServerId);
        if (forwarded == 0) {
            L2_LOG_WARN(Log,
                "[WSM] Web-session verification reply %d for account %d (peer reason %d) was not forwarded to world server %d: no connection exists for its internal address. The request will be released; this branch still reports success.",
                account->tid, user->loginUser.uid, account->reason,
                static_cast<int>(serverSocket->serverid));
        }
        result = true;
    } else {
        L2_LOG_WARN(Log,
            "[WSM] Cannot handle web-session verification reply %d (peer reason %d): request origin %d is unsupported. Releasing the request without replying.",
            account->tid, account->reason, static_cast<int>(user->origin));
    }

    if (user->pIOSocket != nullptr) {
        user->pIOSocket->ReleaseRef();
    }
    if (user->pSession != nullptr) {
        user->pSession->ReleaseRef();
    }
    user->ReleaseRef();
    return result;
}

// L2Authd.exe 0x004949D0, Src/wsmservermgr.cpp:480-522.
bool WSMServerMgr::RecvUpdateTimeoutAck(
    const unsigned char* packet) {
    const std::int32_t tid =
        GetIntFromPacket(&packet);
    const std::int32_t reason =
        GetIntFromPacket(&packet);
    WSMUser* user = nullptr;
    if (!DelUserWait(tid, &user)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "User information was deleted while checking webSessionId from WSM(%d)",
        //     tid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=reply_without_pending_request peer=wsm operation=update_timeout transaction=%d action=ignore_reply",
        //     tid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[WSM] Ignoring session-timeout update reply %d: no request is pending.",
        //     tid)
        // L2_LOG_WARN(Log,
        //     "[WSM] Ignoring session-timeout update reply %d: no request is pending.",
        //     tid)
        L2_LOG_WARN(Log,
            "[WSM] Ignoring session-timeout update reply %d (peer reason %d): no request is pending.",
            tid, reason);
        return false;
    }

    bool result = false;
    if (user->origin == ORIGIN_LOGOUT) {
        result = true;
    } else if (user->origin ==
                   ORIGIN_GAMESERVER_REQUEST &&
               user->pIOSocket != nullptr) {
        auto* const serverSocket =
            static_cast<CSocketServer*>(
                user->pIOSocket);
        const WorldServer world = g_ServerList.GetAt(
            serverSocket->serverid);
        const int forwarded = SendSocket(
            world.inner_addr,
            "cdd",
            0x1d,
            user->loginUser.uid,
            MapWSMToAuthReason(reason));
        if (forwarded == 0) {
            L2_LOG_WARN(Log,
                "[WSM] Session-timeout reply %d for account %d (peer reason %d) was not forwarded to world server %d: no connection exists for its internal address. The request will be released; this branch still reports success.",
                tid, user->loginUser.uid, reason,
                static_cast<int>(serverSocket->serverid));
        }
        result = true;
    }

    if (user->pIOSocket != nullptr) {
        user->pIOSocket->ReleaseRef();
    }
    user->ReleaseRef();
    return result;
}

// L2Authd.exe 0x00494BA0, Src/wsmservermgr.cpp:526-604.
bool WSMServerMgr::RecvLoginAck(
    const unsigned char* packet) {
    const std::int32_t tid =
        GetIntFromPacket(&packet);
    const std::int32_t reason =
        GetIntFromPacket(&packet);
    WebSessionId webSessionId{};
    std::memcpy(
        &webSessionId,
        packet,
        sizeof(webSessionId));
    packet += sizeof(webSessionId);
    unsigned char uniqueId[16];
    std::memcpy(uniqueId, packet, sizeof(uniqueId));
    packet += sizeof(uniqueId);
    const char tokenServerId =
        GetCharFromPacket(&packet);

    WSMUser* user = nullptr;
    if (!DelUserWait(tid, &user)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "User information was deleted while login request to WSM(%d)",
        //     tid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=reply_without_pending_request peer=wsm operation=login transaction=%d action=ignore_reply",
        //     tid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[WSM] Ignoring login reply %d: no request is pending.",
        //     tid)
        // L2_LOG_WARN(Log,
        //     "[WSM] Ignoring login reply %d: no request is pending.",
        //     tid)
        L2_LOG_WARN(Log,
            "[WSM] Ignoring login reply %d (peer reason %d): no request is pending.",
            tid, reason);
        return false;
    }

    const auto origin = static_cast<EORIGIN_CODE>(
        user->origin);
    if (config.useExtendWebSessionLifeTime &&
        reason == 0) {
        auto* const serverSocket =
            static_cast<CSocketServer*>(
                user->pIOSocket);
        const char extendResult = ExtendLifeTimeWsid(
            origin,
            serverSocket,
            &user->loginUser,
            webSessionId,
            uniqueId,
            tokenServerId);
        if (extendResult != 0 &&
            origin == ORIGIN_GAMESERVER_REQUEST) {
            // Log.AddLog(
            //     LOG_WARN,
            //     "WSID created succeed, but extend life time failed(reason:%d)",
            //     static_cast<int>(extendResult))
            // Log.AddLog(
            //     LOG_WARN,
            //     "component=authentication event=session_extension_failed peer=wsm transaction=%d reason_code=%d action=report_login_success",
            //     tid, static_cast<int>(extendResult))
            // Log.AddLog(
            //     LOG_WARN,
            //     "[WSM] Could not extend the session for request %d (reason %d). Login is still reported as successful to the world server.",
            //     tid, static_cast<int>(extendResult))
            // L2_LOG_WARN(Log,
            //     "[WSM] Could not extend the session for request %d (reason %d). Login is still reported as successful to the world server.",
            //     tid, static_cast<int>(extendResult))
            // L2_LOG_WARN(Log,
            //     "[WSM] Could not extend the session for login request %d: WSM connection is unavailable (code %d). Login is still reported as successful to the world server.",
            //     tid, static_cast<int>(extendResult))
            L2_LOG_WARN(Log,
                "[WSM] Could not request session extension for login transaction %d: WSM connection is unavailable (code %d). Attempting to send the successful login result to world server %d without extension.",
                tid, static_cast<int>(extendResult), static_cast<int>(serverSocket->serverid));
            const WorldServer world = g_ServerList.GetAt(
                serverSocket->serverid);
            SendSocket(
                world.inner_addr,
                "cdcbbc",
                0x1b,
                user->loginUser.uid,
                MapWSMToAuthReason(0),
                16,
                &webSessionId,
                16,
                uniqueId,
                tokenServerId);
        }
    } else if (origin == ORIGIN_GAMESERVER_REQUEST) {
        auto* const serverSocket =
            static_cast<CSocketServer*>(
                user->pIOSocket);
        const WorldServer world = g_ServerList.GetAt(
            serverSocket->serverid);
        SendSocket(
            world.inner_addr,
            "cdcbbc",
            0x1b,
            user->loginUser.uid,
            MapWSMToAuthReason(reason),
            16,
            &webSessionId,
            16,
            uniqueId,
            tokenServerId);
    } else {
        // Log.AddLog(
        //     LOG_WARN,
        //     "WSID created succeed, but unknown origin request(%d)(uid:%d)",
        //     static_cast<int>(origin),
        //     user->loginUser.uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=unsupported_request_origin peer=wsm operation=login transaction=%d origin=%d account_id=%d reason_code=%d action=release_request_without_reply",
        //     tid, static_cast<int>(origin), user->loginUser.uid, reason)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[WSM] Login request %d has unsupported origin %d for account %d (reason %d). Releasing the request without replying.",
        //     tid, static_cast<int>(origin), user->loginUser.uid, reason)
        L2_LOG_WARN(Log,
            "[WSM] Login request %d has unsupported origin %d for account %d (reason %d). Releasing the request without replying.",
            tid, static_cast<int>(origin), user->loginUser.uid, reason);
    }

    if (user->pIOSocket != nullptr) {
        user->pIOSocket->ReleaseRef();
    }
    user->ReleaseRef();
    return true;
}

// L2Authd.exe 0x00494F30, Src/wsmservermgr.cpp:607-656.
bool WSMServerMgr::ClientLoginCallback(
    WSMAccountData* account,
    WSMUser* user) {
    try {
        CClientSession* const session = user->pSession;
        if (account->reason != 0) {
            L2_LOG_INFO(Log,
                "[WSM] Login rejected for transaction %d (peer reason %d). Sending client failure code %d.",
                account->tid, account->reason, MapWSMToAuthReason(account->reason));
            session->SendFail(
                1,
                static_cast<char>(
                    MapWSMToAuthReason(
                        account->reason)),
                nullptr);
            return false;
        }

        std::strcpy(
            user->preUser.playAccount,
            account->account);
        if (account->portalUid == 0) {
            user->preUser.portalUser.uid = 0;
            if (!accountdb.AuthenticateUser(
                    session,
                    &user->preUser,
                    false)) {
                return false;
            }
            return accountdb.AuthorizeUser(
                session,
                user->preUser);
        }

        account->account[14] = 0;
        std::strcpy(
            user->preUser.portalUser.account,
            account->account);
        user->preUser.portalUser.uid =
            account->portalUid;
        user->preUser.portalUser.birthdate =
            account->birthdate;
        user->preUser.portalUser.sex = account->sex;
        user->preUser.portalUser.blockCode =
            account->blockCode;
        user->preUser.portalUser.noticeCode =
            account->noticeCode;
        user->preUser.portalUser.statusCode =
            account->statusCode;
        user->preUser.portalUser.blockReasonCode =
            account->blockReasonCode;
        if (!accountdb.AuthenticateUserPortal(
                session,
                &user->preUser)) {
            return false;
        }
        return accountdb.AuthorizeUser(
            session,
            user->preUser);
    } catch (...) {
        // ELOG(
        //     "*%s:%d(%s) exception\n",
        //     "WSMServerMgr.cpp",
        //     654,
        //     "Wed Oct 23 17:02:12 2019")
        ELOG(
            "[WSMServerMgr.cpp] Exception while processing the web-session login callback. Entering exception handling.\n");
        send_exception(true);
        return false;
    }
}
