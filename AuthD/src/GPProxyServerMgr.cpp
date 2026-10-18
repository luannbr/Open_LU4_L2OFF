#include "GPProxyServerMgr.h"

#include "Shared/src/ScopedLockGuards.h"

#include "Platform/IOSocketTransport.h"
#include "Platform/LoginUserImage.h"
#include "Shared/src/Runtime/NativeSocketError.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/TimerQueue.h"
#include "ClientSession.h"
#include "Config.h"
#include "GPProxySocket.h"
#include "include/MD5.h"
#include "IOServer.h"
#include "Log.h"
#include "ServerList.h"
#include "SocketServerEx.h"
#include "Thread.h"
#include "Util.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iterator>
#include <string>

#if defined(_WIN32)
#  include <WinSock2.h>
#else
#  include <arpa/inet.h>
#endif

int GetMappedSecondPasswordResultCode(int result);

CGpProxyServerMgr* CGpProxyServerMgr::s_instance = nullptr;

namespace {

// The selected x86 image advances this counter with ADD and divides its raw
// 32-bit value with unsigned DIV. Unsigned storage gives that wraparound exact
// meaning without signed-overflow undefined behavior on portable hosts.
std::uint32_t socketIdx = 0;

constexpr char kGpProxyFailure = 0x14;

in_addr SessionAddress(CClientSession* session) {
    if (session == nullptr) {
        return in_addr{};
    }
    return session->getaddr();
}

} // namespace

// L2Authd.exe 0x0043A830, Src/gpproxyservermgr.cpp:38-42.
CGpProxyUser::CGpProxyUser()
    : m_pIOSocket(nullptr),
      m_pSession(nullptr),
      m_opTime(0),
      m_refCount(0),
      m_preLoginUser(),
      m_loginUser(),
      m_origin(ORIGIN_CLIENT_LOGIN),
      m_locationNo(0),
      m_tokenServerId(0) {
    // m_serverid and m_command are not initialized by the target constructor.
    AddRef();
    std::memset(&m_webSessionId, 0, sizeof(m_webSessionId));
    std::memset(&m_uniqueId, 0, sizeof(m_uniqueId));
}

// L2Authd.exe 0x0043A9B0, Src/gpproxyservermgr.cpp:78-82.
CGpProxyUser::CGpProxyUser(int serverId)
    : m_pIOSocket(nullptr),
      m_pSession(nullptr),
      m_opTime(0),
      m_refCount(0),
      m_preLoginUser(),
      m_loginUser(),
      m_origin(ORIGIN_CLIENT_LOGIN),
      m_serverid(serverId),
      m_locationNo(0),
      m_tokenServerId(0) {
    // m_command is not initialized by the target constructor.
    AddRef();
    std::memset(&m_webSessionId, 0, sizeof(m_webSessionId));
    std::memset(&m_uniqueId, 0, sizeof(m_uniqueId));
}

// L2Authd.exe 0x0043A8E0, Src/gpproxyservermgr.cpp:58-66.
CGpProxyUser::CGpProxyUser(
    EORIGIN_CODE origin,
    CClientSession* session,
    PreLoginUser& user)
    : m_pIOSocket(nullptr),
      m_pSession(session),
      m_opTime(0),
      m_refCount(0),
      m_preLoginUser(user),
      m_loginUser(),
      m_origin(origin),
      m_locationNo(0),
      m_tokenServerId(0) {
    // m_serverid and m_command are not initialized by the target constructor.
    AddRef();
    if (m_pSession != nullptr) {
        m_pSession->AddRef();
    }
    std::memset(&m_webSessionId, 0, sizeof(m_webSessionId));
    std::memset(&m_uniqueId, 0, sizeof(m_uniqueId));
}

// L2Authd.exe 0x0043AA60, Src/gpproxyservermgr.cpp:86-96.
CGpProxyUser::~CGpProxyUser() {
    if (m_pIOSocket != nullptr) {
        m_pIOSocket->ReleaseRef();
        m_pIOSocket = nullptr;
    }
    if (m_pSession != nullptr) {
        m_pSession->ReleaseRef();
        m_pSession = nullptr;
    }
}

// L2Authd.exe 0x0043A740, Src/gpproxyservermgr.cpp:100-103.
CGpProxyUser* CGpProxyUser::Alloc() {
    return new CGpProxyUser;
}

// L2Authd.exe 0x0043A7C0, Src/gpproxyservermgr.cpp:107-110.
void CGpProxyUser::AddRef() {
    runtime::raw_atomic::IncrementI32(&m_refCount);
}

// L2Authd.exe 0x0043A7E0, Src/gpproxyservermgr.cpp:114-120.
void CGpProxyUser::ReleaseRef() {
    if (runtime::raw_atomic::DecrementI32(
            &m_refCount) < 1) {
        delete this;
    }
}

// L2Authd.exe 0x0043AC00, Src/gpproxyservermgr.cpp:133-143.
CGpProxyServerMgr::CGpProxyServerMgr()
    : m_waitUserMapLock(eCriticalSection, 0),
      m_socketMapLock(eCriticalSection, 0) {
    if (s_instance == nullptr) {
        m_waitUserMap.clear();
        m_socketMap.clear();
        m_hReconnectTimer = nullptr;
        m_transCount = 0;
        s_instance = this;
    }
}

// L2Authd.exe 0x0043ACD0, Src/gpproxyservermgr.cpp:147-178.
CGpProxyServerMgr::~CGpProxyServerMgr() {
    {
        L2_GUARD(m_waitUserMapLock);
        for (auto& entry : m_waitUserMap) {
            CGpProxyUser* user = entry.second;
            if (user != nullptr) {
                user->ReleaseRef();
                user = nullptr;
            }
        }
        m_waitUserMap.clear();
    }

    config.gpProxyReconnectInterval = 0;
    while (!m_socketMap.empty()) {
        CGpProxySocket* const socket =
            *m_socketMap.begin();
        if (socket != nullptr) {
            socket->CloseSocket();
        }
    }
    s_instance = nullptr;
}

// L2Authd.exe 0x0043AAB0, Src/gpproxyservermgr.cpp:372-394.
bool CGpProxyServerMgr::AddUserWait(
    CGpProxyUser* user,
    int& transactionId) {
    user->AddRef();
    user->m_opTime = static_cast<std::int32_t>(
        std::time(nullptr));

    bool inserted = false;
    {
        L2_GUARD(m_waitUserMapLock);
        transactionId =
            runtime::raw_atomic::IncrementI32(
                &m_transCount);
        inserted = m_waitUserMap.insert(
            {transactionId, user}).second;
    }
    if (!inserted) {
        user->ReleaseRef();
    }
    return inserted;
}

// L2Authd.exe 0x0043AB50, Src/gpproxyservermgr.cpp:401-422.
bool CGpProxyServerMgr::DelUserWait(
    int transactionId,
    CGpProxyUser*& user) {
    bool removed = false;
    {
        L2_GUARD(m_waitUserMapLock);
        const auto found = m_waitUserMap.find(transactionId);
        if (found != m_waitUserMap.end()) {
            user = found->second;
            m_waitUserMap.erase(found);
            removed = true;
        } else {
            user = nullptr;
        }
    }
    return removed;
}

// L2Authd.exe 0x0043AE50, Src/gpproxyservermgr.cpp:182-203.
void CGpProxyServerMgr::Initialize() {
    for (ConnectionInformation& information :
         config.gpProxyHosts) {
        const std::uint32_t peerAddress = ntohl(information.host.s_addr);
        std::uint32_t nativeSocket = UINT32_C(0xffffffff);
        const bool connected =
            authd::platform::ConnectNativeSocket(
                nativeSocket,
                information.host,
                information.port);
        if (!connected) {
            // Log.AddLog(LOG_WARN,
            //     "component=network event=connect_failed peer=gp_proxy address=%u.%u.%u.%u port=%u socket=%u error_domain=winsock_compatible code=%d action=close_socket",
            //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
            //     static_cast<unsigned int>(information.port), nativeSocket,
            //     ::portable::GetNativeSocketLastError())
            // Log.AddLog(LOG_WARN,
            //     "[GPProxy] Could not connect to %u.%u.%u.%u:%u on socket %u (WinSock-compatible error %d). Closing the socket.",
            //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
            //     static_cast<unsigned int>(information.port), nativeSocket,
            //     ::portable::GetNativeSocketLastError())
            // L2_LOG_WARN(Log,
            //     "[GPProxy] Could not connect to %u.%u.%u.%u:%u on socket %u (WinSock-compatible error %d). Closing the socket.",
            //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
            //     static_cast<unsigned int>(information.port), nativeSocket,
            //     ::portable::GetNativeSocketLastError())
            L2_LOG(Log, nativeSocket == UINT32_C(0xffffffff) ? LOG_ERROR : LOG_WARN,
                "[GPProxy] Could not connect to %u.%u.%u.%u:%u on socket %u (WinSock-compatible error %d). %s",
                (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
                (peerAddress >> 8U) & 255U, peerAddress & 255U,
                static_cast<unsigned int>(information.port), nativeSocket,
                ::portable::GetNativeSocketLastError(),
                nativeSocket == UINT32_C(0xffffffff)
                    ? "No valid socket was created; the close callback and automatic retry are not scheduled."
                    : "Closing the socket; its close callback will request a retry when reconnect is enabled.");
        }
        CGpProxySocket* const socket =
            CGpProxySocket::Allocate(
                nativeSocket,
                information);
        if (connected) {
            authd::platform::InitializeNativeSocket(
                socket,
                nativeSocket,
                g_hIOCompletionPort);
        } else {
#if !defined(_WIN32) || INTPTR_MAX != INT32_MAX
            // Win32 CIOSocket::CloseSocket owns this descriptor directly.
            // The portable wrapper has not been bound to its transport yet.
            authd::platform::AbortiveCloseNativeSocket(
                nativeSocket);
#endif
            socket->CloseSocket();
        }
    }
}

// L2Authd.exe 0x0043AF40, Src/gpproxyservermgr.cpp:207-268.
void CGpProxyServerMgr::ReconnectTimerRoutine(
    CGpProxySocket* socket) {
    // L2Authd.exe contains this test before dereferencing the manager. Calling
    // a non-static C++ member through null is still undefined at the language
    // level, so compare the incoming address as the integer value tested by
    // the target instructions.
    if (reinterpret_cast<std::uintptr_t>(this) == 0U) {
        return;
    }
    if (socket == nullptr) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "CGpProxyServerMgr::ReconnectTimerRoutine called with NULL socket handle.")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=network event=reconnect_failed peer=gp_proxy reason=null_socket action=return_without_retry")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[GPProxy] Reconnect callback has no socket. Returning without requesting a retry.")
        L2_LOG_ERROR(Log,
            "[GPProxy] Reconnect callback has no socket. Returning without requesting a retry.");
        return;
    }

    if (socket->m_hReconnectTimer != nullptr) {
        runtime::timer_queue::DeleteTimer(
            nullptr,
            socket->m_hReconnectTimer);
        socket->m_hReconnectTimer = nullptr;
    }

    const std::uint32_t peerAddress = ntohl(socket->m_host.s_addr);
    std::uint32_t nativeSocket = UINT32_C(0xffffffff);
    const bool connected =
        authd::platform::ConnectNativeSocket(
            nativeSocket,
            socket->m_host,
            socket->m_port);
    if (connected) {
        ConnectionInformation information(
            socket->m_host,
            socket->m_port);
        CGpProxySocket* const replacement =
            CGpProxySocket::Allocate(
                nativeSocket,
                information);
        if (replacement != nullptr) {
            authd::platform::InitializeNativeSocket(
                replacement,
                nativeSocket,
                g_hIOCompletionPort);
            RegisterSocketMap(replacement);
            socket->ReleaseRef();
        }
        return;
    }

    // Log.AddLog(
    //     LOG_ERROR,
    //     "CGpProxyServerMgr::ReconnectTimerRoutine reconnect failed. (%s:%d)",
    //     ::inet_ntoa(socket->m_host),
    //     static_cast<int>(socket->m_port))
    // Log.AddLog(
    //     LOG_WARN,
    //     "component=network event=reconnect_failed peer=gp_proxy address=%u.%u.%u.%u port=%u socket=%u error_domain=winsock_compatible code=%d reconnect=schedule_pending delay_ms=%u",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(socket->m_port), nativeSocket,
    //     ::portable::GetNativeSocketLastError(),
    //     static_cast<std::uint32_t>(config.gpProxyReconnectInterval))
    // Log.AddLog(
    //     LOG_WARN,
    //     "[GPProxy] Could not reconnect to %u.%u.%u.%u:%u on socket %u (WinSock-compatible error %d). Requesting another retry after %u ms.",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(socket->m_port), nativeSocket,
    //     ::portable::GetNativeSocketLastError(),
    //     static_cast<std::uint32_t>(config.gpProxyReconnectInterval))
    L2_LOG_WARN(Log,
        "[GPProxy] Could not reconnect to %u.%u.%u.%u:%u on socket %u (WinSock-compatible error %d). Requesting another retry after %u ms.",
        (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        (peerAddress >> 8U) & 255U, peerAddress & 255U,
        static_cast<unsigned int>(socket->m_port), nativeSocket,
        ::portable::GetNativeSocketLastError(),
        static_cast<std::uint32_t>(config.gpProxyReconnectInterval));
    authd::platform::AbortiveCloseNativeSocket(nativeSocket);

    const bool created =
        runtime::timer_queue::CreateTimer(
            &m_hReconnectTimer,
            nullptr,
            GpProxySocketTimerRoutine,
            socket,
            static_cast<std::uint32_t>(
                config.gpProxyReconnectInterval),
            0);
    if (!created) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "CGpProxyServerMgr::ReconnectTimerRoutine timer reregistering failed. (%s:%d)",
        //     ::inet_ntoa(socket->m_host),
        //     static_cast<int>(socket->m_port))
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=network event=reconnect_schedule_failed peer=gp_proxy address=%u.%u.%u.%u port=%u delay_ms=%u action=return_without_new_retry",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(socket->m_port),
        //     static_cast<std::uint32_t>(config.gpProxyReconnectInterval))
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[GPProxy] Could not schedule reconnect to %u.%u.%u.%u:%u after %u ms. No new retry is scheduled.",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(socket->m_port),
        //     static_cast<std::uint32_t>(config.gpProxyReconnectInterval))
        L2_LOG_ERROR(Log,
            "[GPProxy] Could not schedule reconnect to %u.%u.%u.%u:%u after %u ms. No new retry is scheduled.",
            (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            (peerAddress >> 8U) & 255U, peerAddress & 255U,
            static_cast<unsigned int>(socket->m_port),
            static_cast<std::uint32_t>(config.gpProxyReconnectInterval));
    }
}

// L2Authd.exe 0x0043B110, Src/gpproxyservermgr.cpp:276-296.
bool CGpProxyServerMgr::RegisterSocketMap(
    CGpProxySocket* socket) {
    if (socket == nullptr) {
        return false;
    }

    socket->AddRef();
    bool inserted = false;
    {
        L2_GUARD(m_socketMapLock);
        inserted = m_socketMap.insert(socket).second;
    }
    if (!inserted) {
        socket->ReleaseRef();
    }
    return inserted;
}

// L2Authd.exe 0x0043B180, Src/gpproxyservermgr.cpp:304-328.
bool CGpProxyServerMgr::UnregisterSocketMap(
    CGpProxySocket* socket) {
    if (socket == nullptr) {
        return false;
    }

    bool removed = false;
    {
        L2_GUARD(m_socketMapLock);
        for (auto current = m_socketMap.begin();
             current != m_socketMap.end();
             ++current) {
            if (*current == socket) {
                m_socketMap.erase(current);
                socket->ReleaseRef();
                removed = true;
                break;
            }
        }
    }
    return removed;
}

// L2Authd.exe 0x0043B240, Src/gpproxyservermgr.cpp:336-364.
CGpProxySocket* CGpProxyServerMgr::GetValidSocket() {
    CGpProxySocket* result = nullptr;
    {
        L2_GUARD(m_socketMapLock);
        if (!m_socketMap.empty()) {
            const std::uint32_t selected =
                socketIdx %
                static_cast<std::uint32_t>(m_socketMap.size());
            ++socketIdx;
            auto current = m_socketMap.begin();
            std::advance(current, selected);
            result = *current;
            result->AddRef();
        }
    }
    return result;
}

// L2Authd.exe 0x0043B2F0, Src/gpproxyservermgr.cpp:432-481.
char CGpProxyServerMgr::LoginAccountWsid(
    EORIGIN_CODE origin,
    CClientSession* session,
    CIOSocket* socket,
    PreLoginUser& login,
    WebToken& token) {
    CGpProxyUser* user = CGpProxyUser::Alloc();
    if (user == nullptr) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Failed to alloc CGpProxyUser (LoginAccountWsid)")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=authentication event=request_allocation_failed peer=gp_proxy operation=login_web_session origin=%d result_code=%u action=return_failure",
        //     static_cast<int>(origin), static_cast<unsigned int>(kGpProxyFailure))
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[GPProxy] Could not allocate a request for web session login (origin %d). Returning failure code %u.",
        //     static_cast<int>(origin), static_cast<unsigned int>(kGpProxyFailure))
        L2_LOG_ERROR(Log,
            "[GPProxy] Could not allocate a request for web session login (origin %d). Returning failure code %u.",
            static_cast<int>(origin), static_cast<unsigned int>(kGpProxyFailure));
        return kGpProxyFailure;
    }

    if (session != nullptr) {
        session->AddRef();
        user->m_pSession = session;
    }
    if (socket != nullptr) {
        socket->AddRef();
        user->m_pIOSocket = socket;
    }
    std::memcpy(
        &user->m_preLoginUser,
        &login,
        sizeof(PreLoginUser));
    user->m_origin = origin;
    user->m_uniqueId = token;

    int transactionId = 0;
    if (!AddUserWait(user, transactionId)) {
        user->ReleaseRef();
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Failed to AddUserWait (LoginAccountWsid)")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=authentication event=request_registration_failed peer=gp_proxy operation=login_web_session transaction=%d reason=duplicate_transaction result_code=%u action=return_failure",
        //     transactionId, static_cast<unsigned int>(kGpProxyFailure))
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[GPProxy] Could not register web session login request %d: transaction already exists. Returning failure code %u.",
        //     transactionId, static_cast<unsigned int>(kGpProxyFailure))
        L2_LOG_ERROR(Log,
            "[GPProxy] Could not register web session login request %d: transaction already exists. Returning failure code %u.",
            transactionId, static_cast<unsigned int>(kGpProxyFailure));
        return kGpProxyFailure;
    }

    if (SendVerifyWebSessionReq(
            transactionId,
            user->m_preLoginUser.webSessionId,
            SessionAddress(user->m_pSession))) {
        user->ReleaseRef();
        return 0;
    }

    // The selected image releases the caller reference before erasing the
    // map entry and never releases the transferred map reference here.
    user->ReleaseRef();
    DelUserWait(transactionId, user);
    // Noise: The sender reports every submission failure with its transaction and reason.
    // Log.AddLog(
    //     LOG_ERROR,
    //     "Failed to SendVerifyWebSessionReq (LoginAccountWsid)")
    return kGpProxyFailure;
}

// L2Authd.exe 0x0043B470, Src/gpproxyservermgr.cpp:491-529.
char CGpProxyServerMgr::LoginAccountIdPwd(
    EORIGIN_CODE origin,
    CSocketServer* socket,
    LoginUser& login,
    WebToken& token,
    int timeout) {
    CGpProxyUser* user = CGpProxyUser::Alloc();
    if (user == nullptr) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Failed to alloc CGpProxyUser (LoginAccountIdPwd)")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=authentication event=request_allocation_failed peer=gp_proxy operation=login_password origin=%d result_code=%u action=return_failure",
        //     static_cast<int>(origin), static_cast<unsigned int>(kGpProxyFailure))
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[GPProxy] Could not allocate a request for password login (origin %d). Returning failure code %u.",
        //     static_cast<int>(origin), static_cast<unsigned int>(kGpProxyFailure))
        L2_LOG_ERROR(Log,
            "[GPProxy] Could not allocate a request for password login (origin %d). Returning failure code %u.",
            static_cast<int>(origin), static_cast<unsigned int>(kGpProxyFailure));
        return kGpProxyFailure;
    }

    if (socket != nullptr) {
        socket->AddRef();
        user->m_pIOSocket = socket;
    }
    authd::portable::CopyLoginUserImage(user->m_loginUser, login);
    user->m_origin = origin;
    user->m_uniqueId = token;

    int transactionId = 0;
    if (!AddUserWait(user, transactionId)) {
        user->ReleaseRef();
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Failed to AddUserWait (LoginAccountIdPwd)")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=authentication event=request_registration_failed peer=gp_proxy operation=login_password transaction=%d reason=duplicate_transaction result_code=%u action=return_failure",
        //     transactionId, static_cast<unsigned int>(kGpProxyFailure))
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[GPProxy] Could not register password login request %d: transaction already exists. Returning failure code %u.",
        //     transactionId, static_cast<unsigned int>(kGpProxyFailure))
        L2_LOG_ERROR(Log,
            "[GPProxy] Could not register password login request %d: transaction already exists. Returning failure code %u.",
            transactionId, static_cast<unsigned int>(kGpProxyFailure));
        return kGpProxyFailure;
    }

    if (SendCreateWebSessionReq(
            transactionId,
            user->m_loginUser,
            timeout,
            user->m_loginUser.nWsmLoginIp)) {
        user->ReleaseRef();
        return 0;
    }

    // L2Authd.exe removes the map entry but performs only one of the two
    // outstanding releases on this failure path.
    DelUserWait(transactionId, user);
    user->ReleaseRef();
    // Noise: The sender reports every submission failure with its transaction and reason.
    // Log.AddLog(
    //     LOG_ERROR,
    //     "Failed to SendCreateWebSessionReq (LoginAccountIdPwd)")
    return kGpProxyFailure;
}

// L2Authd.exe 0x0043B5B0, Src/gpproxyservermgr.cpp:538-576.
char CGpProxyServerMgr::LogoutAccountWsid(
    EORIGIN_CODE origin,
    CSocketServer* socket,
    const LoginUser& login,
    WebSessionId& webSessionId) {
    CGpProxyUser* user = CGpProxyUser::Alloc();
    if (user == nullptr) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Failed to alloc CGpProxyUser (LogoutAccountWsid)")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=authentication event=request_allocation_failed peer=gp_proxy operation=logout origin=%d result_code=%u action=return_failure",
        //     static_cast<int>(origin), static_cast<unsigned int>(kGpProxyFailure))
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[GPProxy] Could not allocate a request for logout (origin %d). Returning failure code %u.",
        //     static_cast<int>(origin), static_cast<unsigned int>(kGpProxyFailure))
        L2_LOG_ERROR(Log,
            "[GPProxy] Could not allocate a request for logout (origin %d). Returning failure code %u.",
            static_cast<int>(origin), static_cast<unsigned int>(kGpProxyFailure));
        return kGpProxyFailure;
    }

    if (socket != nullptr) {
        socket->AddRef();
        user->m_pIOSocket = socket;
    }
    authd::portable::CopyLoginUserImage(user->m_loginUser, login);
    user->m_origin = origin;

    int transactionId = 0;
    if (!AddUserWait(user, transactionId)) {
        user->ReleaseRef();
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Failed to AddUserWait (LogoutAccountWsid)")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=authentication event=request_registration_failed peer=gp_proxy operation=logout transaction=%d reason=duplicate_transaction result_code=%u action=return_failure",
        //     transactionId, static_cast<unsigned int>(kGpProxyFailure))
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[GPProxy] Could not register logout request %d: transaction already exists. Returning failure code %u.",
        //     transactionId, static_cast<unsigned int>(kGpProxyFailure))
        L2_LOG_ERROR(Log,
            "[GPProxy] Could not register logout request %d: transaction already exists. Returning failure code %u.",
            transactionId, static_cast<unsigned int>(kGpProxyFailure));
        return kGpProxyFailure;
    }

    if (SendUpdateWebSessionTimeoutReq(
            transactionId,
            webSessionId,
            user->m_loginUser.loginIp)) {
        user->ReleaseRef();
        return 0;
    }

    DelUserWait(transactionId, user);
    user->ReleaseRef();
    // Noise: The sender reports every submission failure with its transaction and reason.
    // Log.AddLog(
    //     LOG_ERROR,
    //     "Failed to SendUpdateWebSessionTimeoutReq (LogoutAccountWsid)")
    return kGpProxyFailure;
}

// L2Authd.exe 0x0043B6C0, Src/gpproxyservermgr.cpp:587-628.
char CGpProxyServerMgr::ExtendLifeTimeWsid(
    EORIGIN_CODE origin,
    CSocketServer* socket,
    LoginUser& login,
    WebSessionId& webSessionId,
    WebToken& token,
    char tokenServerId) {
    CGpProxyUser* user = CGpProxyUser::Alloc();
    if (user == nullptr) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Failed to alloc CGpProxyUser (ExtendLifeTimeWsid)")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=authentication event=request_allocation_failed peer=gp_proxy operation=extend_session origin=%d result_code=%u action=return_failure",
        //     static_cast<int>(origin), static_cast<unsigned int>(kGpProxyFailure))
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[GPProxy] Could not allocate a request for session extension (origin %d). Returning failure code %u.",
        //     static_cast<int>(origin), static_cast<unsigned int>(kGpProxyFailure))
        L2_LOG_ERROR(Log,
            "[GPProxy] Could not allocate a request for session extension (origin %d). Returning failure code %u.",
            static_cast<int>(origin), static_cast<unsigned int>(kGpProxyFailure));
        return kGpProxyFailure;
    }

    if (socket != nullptr) {
        socket->AddRef();
        user->m_pIOSocket = socket;
    }
    authd::portable::CopyLoginUserImage(user->m_loginUser, login);
    user->m_webSessionId = webSessionId;
    user->m_uniqueId = token;
    user->m_tokenServerId = tokenServerId;
    user->m_origin = origin;

    int transactionId = 0;
    if (!AddUserWait(user, transactionId)) {
        user->ReleaseRef();
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Failed to AddUserWait (ExtendLifeTimeWsid)")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=authentication event=request_registration_failed peer=gp_proxy operation=extend_session transaction=%d reason=duplicate_transaction result_code=%u action=return_failure",
        //     transactionId, static_cast<unsigned int>(kGpProxyFailure))
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[GPProxy] Could not register session extension request %d: transaction already exists. Returning failure code %u.",
        //     transactionId, static_cast<unsigned int>(kGpProxyFailure))
        L2_LOG_ERROR(Log,
            "[GPProxy] Could not register session extension request %d: transaction already exists. Returning failure code %u.",
            transactionId, static_cast<unsigned int>(kGpProxyFailure));
        return kGpProxyFailure;
    }

    if (SendVerifyWebSessionReq(
            transactionId,
            webSessionId,
            user->m_loginUser.loginIp)) {
        user->ReleaseRef();
        return 0;
    }

    DelUserWait(transactionId, user);
    user->ReleaseRef();
    // This wording is present in the selected image even though the call
    // immediately above is SendVerifyWebSessionReq.
    // Noise: The sender reports every submission failure with its transaction and reason.
    // Log.AddLog(
    //     LOG_ERROR,
    //     "Failed to SendUpdateWebSessionTimeoutReq (ExtendLifeTimeWsid)")
    return kGpProxyFailure;
}

// L2Authd.exe 0x0043B820, Src/gpproxyservermgr.cpp:631-650.
char CGpProxyServerMgr::ListSecAuthServices(
    EORIGIN_CODE origin,
    CClientSession* session,
    PreLoginUser& login) {
    CGpProxyUser* user =
        new CGpProxyUser(origin, session, login);
    if (user == nullptr) {
        // Log.AddLog(LOG_ERROR,
        //     "component=authentication event=request_allocation_failed peer=gp_proxy operation=list_secondary_services result_code=%u action=return_failure",
        //     static_cast<unsigned int>(kGpProxyFailure))
        // Log.AddLog(LOG_ERROR,
        //     "[GPProxy] Could not allocate a request for secondary authentication service list. Returning failure code %u.",
        //     static_cast<unsigned int>(kGpProxyFailure))
        L2_LOG_ERROR(Log,
            "[GPProxy] Could not allocate a request for secondary authentication service list. Returning failure code %u.",
            static_cast<unsigned int>(kGpProxyFailure));
        return kGpProxyFailure;
    }

    int transactionId = 0;
    if (!AddUserWait(user, transactionId)) {
        // Log.AddLog(LOG_ERROR,
        //     "component=authentication event=request_registration_failed peer=gp_proxy operation=list_secondary_services transaction=%d reason=duplicate_transaction result_code=%u action=return_failure",
        //     transactionId, static_cast<unsigned int>(kGpProxyFailure))
        // Log.AddLog(LOG_ERROR,
        //     "[GPProxy] Could not register secondary authentication service list request %d: transaction already exists. Returning failure code %u.",
        //     transactionId, static_cast<unsigned int>(kGpProxyFailure))
        L2_LOG_ERROR(Log,
            "[GPProxy] Could not register secondary authentication service list request %d: transaction already exists. Returning failure code %u.",
            transactionId, static_cast<unsigned int>(kGpProxyFailure));
        user->ReleaseRef();
        return kGpProxyFailure;
    }
    user->ReleaseRef();

    if (SendSecAuthListServicesReq(
            transactionId,
            login,
            SessionAddress(session))) {
        return 0;
    }

    DelUserWait(transactionId, user);
    user->ReleaseRef();
    return kGpProxyFailure;
}

// L2Authd.exe 0x0043B950, Src/gpproxyservermgr.cpp:675-694.
char CGpProxyServerMgr::VerifyOtp(
    EORIGIN_CODE origin,
    CClientSession* session,
    PreLoginUser& login) {
    CGpProxyUser* user =
        new CGpProxyUser(origin, session, login);
    if (user == nullptr) {
        // Log.AddLog(LOG_ERROR,
        //     "component=authentication event=request_allocation_failed peer=gp_proxy operation=verify_otp result_code=%u action=return_failure",
        //     static_cast<unsigned int>(kGpProxyFailure))
        // Log.AddLog(LOG_ERROR,
        //     "[GPProxy] Could not allocate a request for one-time password verification. Returning failure code %u.",
        //     static_cast<unsigned int>(kGpProxyFailure))
        L2_LOG_ERROR(Log,
            "[GPProxy] Could not allocate a request for one-time password verification. Returning failure code %u.",
            static_cast<unsigned int>(kGpProxyFailure));
        return kGpProxyFailure;
    }

    int transactionId = 0;
    if (!AddUserWait(user, transactionId)) {
        // Log.AddLog(LOG_ERROR,
        //     "component=authentication event=request_registration_failed peer=gp_proxy operation=verify_otp transaction=%d reason=duplicate_transaction result_code=%u action=return_failure",
        //     transactionId, static_cast<unsigned int>(kGpProxyFailure))
        // Log.AddLog(LOG_ERROR,
        //     "[GPProxy] Could not register one-time password verification request %d: transaction already exists. Returning failure code %u.",
        //     transactionId, static_cast<unsigned int>(kGpProxyFailure))
        L2_LOG_ERROR(Log,
            "[GPProxy] Could not register one-time password verification request %d: transaction already exists. Returning failure code %u.",
            transactionId, static_cast<unsigned int>(kGpProxyFailure));
        user->ReleaseRef();
        return kGpProxyFailure;
    }
    user->ReleaseRef();

    if (SendSecAuthVerifyOtpReq(
            transactionId,
            login,
            SessionAddress(session))) {
        return 0;
    }

    DelUserWait(transactionId, user);
    user->ReleaseRef();
    return kGpProxyFailure;
}

// L2Authd.exe 0x0043BA80, Src/gpproxyservermgr.cpp:653-672.
char CGpProxyServerMgr::ListSecAuthAuthenticatedList(
    EORIGIN_CODE origin,
    CClientSession* session,
    PreLoginUser& login) {
    CGpProxyUser* user =
        new CGpProxyUser(origin, session, login);
    if (user == nullptr) {
        // Log.AddLog(LOG_ERROR,
        //     "component=authentication event=request_allocation_failed peer=gp_proxy operation=list_authenticated_services result_code=%u action=return_failure",
        //     static_cast<unsigned int>(kGpProxyFailure))
        // Log.AddLog(LOG_ERROR,
        //     "[GPProxy] Could not allocate a request for authenticated service list. Returning failure code %u.",
        //     static_cast<unsigned int>(kGpProxyFailure))
        L2_LOG_ERROR(Log,
            "[GPProxy] Could not allocate a request for authenticated service list. Returning failure code %u.",
            static_cast<unsigned int>(kGpProxyFailure));
        return kGpProxyFailure;
    }

    int transactionId = 0;
    if (!AddUserWait(user, transactionId)) {
        // Log.AddLog(LOG_ERROR,
        //     "component=authentication event=request_registration_failed peer=gp_proxy operation=list_authenticated_services transaction=%d reason=duplicate_transaction result_code=%u action=return_failure",
        //     transactionId, static_cast<unsigned int>(kGpProxyFailure))
        // Log.AddLog(LOG_ERROR,
        //     "[GPProxy] Could not register authenticated service list request %d: transaction already exists. Returning failure code %u.",
        //     transactionId, static_cast<unsigned int>(kGpProxyFailure))
        L2_LOG_ERROR(Log,
            "[GPProxy] Could not register authenticated service list request %d: transaction already exists. Returning failure code %u.",
            transactionId, static_cast<unsigned int>(kGpProxyFailure));
        user->ReleaseRef();
        return kGpProxyFailure;
    }
    user->ReleaseRef();

    if (SendSecAuthAuthenticatedListReq(
            transactionId,
            login,
            SessionAddress(session))) {
        return 0;
    }

    DelUserWait(transactionId, user);
    user->ReleaseRef();
    return kGpProxyFailure;
}

// L2Authd.exe 0x0043BBB0, Src/gpproxyservermgr.cpp:698-719.
char CGpProxyServerMgr::SecondPasswordRequest(
    char serverId,
    const unsigned char* packet) {
    CGpProxyUser* user = new CGpProxyUser(serverId);
    if (user == nullptr) {
        // Log.AddLog(LOG_ERROR,
        //     "component=authentication event=request_allocation_failed peer=gp_proxy operation=second_password result_code=%u action=return_failure",
        //     static_cast<unsigned int>(kGpProxyFailure))
        // Log.AddLog(LOG_ERROR,
        //     "[GPProxy] Could not allocate a request for second password. Returning failure code %u.",
        //     static_cast<unsigned int>(kGpProxyFailure))
        L2_LOG_ERROR(Log,
            "[GPProxy] Could not allocate a request for second password. Returning failure code %u.",
            static_cast<unsigned int>(kGpProxyFailure));
        return kGpProxyFailure;
    }

    int transactionId = 0;
    if (!AddUserWait(user, transactionId)) {
        // Log.AddLog(LOG_ERROR,
        //     "component=authentication event=request_registration_failed peer=gp_proxy operation=second_password transaction=%d reason=duplicate_transaction result_code=%u action=return_failure",
        //     transactionId, static_cast<unsigned int>(kGpProxyFailure))
        // Log.AddLog(LOG_ERROR,
        //     "[GPProxy] Could not register second password request %d: transaction already exists. Returning failure code %u.",
        //     transactionId, static_cast<unsigned int>(kGpProxyFailure))
        L2_LOG_ERROR(Log,
            "[GPProxy] Could not register second password request %d: transaction already exists. Returning failure code %u.",
            transactionId, static_cast<unsigned int>(kGpProxyFailure));
        user->ReleaseRef();
        return kGpProxyFailure;
    }

    if (SendSecondPasswordReq(
            transactionId,
            user,
            packet)) {
        user->ReleaseRef();
        return 0;
    }

    user->ReleaseRef();
    DelUserWait(transactionId, user);
    user->ReleaseRef();
    return kGpProxyFailure;
}

// L2Authd.exe 0x0043BCB0, Src/gpproxyservermgr.cpp:1020-1068.
bool CGpProxyServerMgr::RecvVerifyWebSessionAck(
    const unsigned char* packet) {
    const auto* const accountData =
        reinterpret_cast<const CGpProxyAccountData*>(
            packet);
    CGpProxyUser* user = nullptr;
    if (!DelUserWait(accountData->tid, user)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Failed to DelUserWait tid:%d (RecvVerifyWebSessionAck)",
        //     accountData->tid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=reply_without_pending_request peer=gp_proxy operation=verify_web_session transaction=%d action=ignore_reply",
        //     accountData->tid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[GPProxy] Ignoring web session verification reply %d: no request is pending.",
        //     accountData->tid)
        L2_LOG_WARN(Log,
            "[GPProxy] Ignoring web session verification reply %d: no request is pending.",
            accountData->tid);
        return false;
    }

    bool result = false;
    if (user->m_origin == ORIGIN_CLIENT_LOGIN) {
        result = ClientLoginCallback(
            const_cast<CGpProxyAccountData*>(accountData),
            user);
    } else if (user->m_origin ==
               ORIGIN_GAMESERVER_REQUEST) {
        auto* const serverSocket =
            static_cast<CSocketServer*>(
                user->m_pIOSocket);
        const WorldServer world =
            g_ServerList.GetAt(serverSocket->serverid);
        SendSocket(
            world.inner_addr,
            "cdcbbc",
            0x1b,
            user->m_loginUser.uid,
            MapGpProxyToAuthReason(accountData->reason),
            0x10,
            &user->m_webSessionId,
            0x10,
            &user->m_uniqueId,
            user->m_tokenServerId);
        result = true;
    } else {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Invalid origin %d (RecvVerifyWebSessionAck)",
        //     static_cast<int>(user->m_origin))
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=unsupported_request_origin peer=gp_proxy operation=verify_web_session transaction=%d origin=%d action=release_request_without_reply",
        //     accountData->tid, static_cast<int>(user->m_origin))
        // Log.AddLog(
        //     LOG_WARN,
        //     "[GPProxy] Cannot handle web session verification reply %d: request origin %d is unsupported. Releasing the request without replying.",
        //     accountData->tid, static_cast<int>(user->m_origin))
        L2_LOG_WARN(Log,
            "[GPProxy] Cannot handle web session verification reply %d: request origin %d is unsupported. Releasing the request without replying.",
            accountData->tid, static_cast<int>(user->m_origin));
    }
    user->ReleaseRef();
    return result;
}

// L2Authd.exe 0x0043BEA0, Src/gpproxyservermgr.cpp:1072-1119.
bool CGpProxyServerMgr::RecvUpdateWebSessionTimeoutAck(
    const unsigned char* packet) {
    const int transactionId = GetIntFromPacket(&packet);
    const int reason = GetIntFromPacket(&packet);
    CGpProxyUser* user = nullptr;
    if (!DelUserWait(transactionId, user)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Failed to DelUserWait tid:%d (RecvUpdateWebSessionTimeoutAck)",
        //     transactionId)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=reply_without_pending_request peer=gp_proxy operation=update_session_timeout transaction=%d action=ignore_reply",
        //     transactionId)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[GPProxy] Ignoring session timeout update reply %d: no request is pending.",
        //     transactionId)
        L2_LOG_WARN(Log,
            "[GPProxy] Ignoring session timeout update reply %d: no request is pending.",
            transactionId);
        return false;
    }

    bool result = false;
    if (user->m_origin == ORIGIN_LOGOUT) {
        result = true;
    } else if (user->m_origin ==
               ORIGIN_GAMESERVER_REQUEST) {
        if (user->m_pIOSocket != nullptr) {
            auto* const serverSocket =
                static_cast<CSocketServer*>(
                    user->m_pIOSocket);
            const WorldServer world =
                g_ServerList.GetAt(serverSocket->serverid);
            SendSocket(
                world.inner_addr,
                "cdd",
                0x1d,
                user->m_loginUser.uid,
                MapGpProxyToAuthReason(reason));
            result = true;
        }
    } else {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Invalid origin %d (RecvUpdateWebSessionTimeoutAck)",
        //     static_cast<int>(user->m_origin))
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=unsupported_request_origin peer=gp_proxy operation=update_session_timeout transaction=%d origin=%d action=release_request_without_reply",
        //     transactionId, static_cast<int>(user->m_origin))
        // Log.AddLog(
        //     LOG_WARN,
        //     "[GPProxy] Cannot handle session timeout update reply %d: request origin %d is unsupported. Releasing the request without replying.",
        //     transactionId, static_cast<int>(user->m_origin))
        L2_LOG_WARN(Log,
            "[GPProxy] Cannot handle session timeout update reply %d: request origin %d is unsupported. Releasing the request without replying.",
            transactionId, static_cast<int>(user->m_origin));
    }
    user->ReleaseRef();
    return result;
}

// L2Authd.exe 0x0043C070, Src/gpproxyservermgr.cpp:1123-1210.
bool CGpProxyServerMgr::RecvCreateWebSessionAck(
    const unsigned char* packet) {
    const int transactionId = GetIntFromPacket(&packet);
    const int reason = GetIntFromPacket(&packet);
    WebSessionId webSessionId{};
    GetBinFromPacket(
        &packet,
        sizeof(webSessionId),
        reinterpret_cast<unsigned char*>(
            &webSessionId));

    CGpProxyUser* user = nullptr;
    if (!DelUserWait(transactionId, user)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Failed to DelUserWait tid:%d (RecvCreateWebSessionAck)",
        //     transactionId)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=reply_without_pending_request peer=gp_proxy operation=create_web_session transaction=%d action=ignore_reply",
        //     transactionId)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[GPProxy] Ignoring web session creation reply %d: no request is pending.",
        //     transactionId)
        L2_LOG_WARN(Log,
            "[GPProxy] Ignoring web session creation reply %d: no request is pending.",
            transactionId);
        return false;
    }

    if (!config.useExtendWebSessionLifeTime ||
        reason != 0) {
        if (user->m_origin ==
            ORIGIN_GAMESERVER_REQUEST) {
            auto* const serverSocket =
                static_cast<CSocketServer*>(
                    user->m_pIOSocket);
            const WorldServer world =
                g_ServerList.GetAt(serverSocket->serverid);
            SendSocket(
                world.inner_addr,
                "cdcbbc",
                0x1b,
                user->m_loginUser.uid,
                MapGpProxyToAuthReason(reason),
                0x10,
                &webSessionId,
                0x10,
                &user->m_uniqueId,
                user->m_tokenServerId);
        } else {
            // Log.AddLog(
            //     LOG_ERROR,
            //     "Invalid origin %d (RecvCreateWebSessionAck)",
            //     static_cast<int>(user->m_origin))
            // Log.AddLog(
            //     LOG_WARN,
            //     "component=authentication event=unsupported_request_origin peer=gp_proxy operation=create_web_session transaction=%d origin=%d reason_code=%d action=release_request_without_reply legacy_return=success",
            //     transactionId, static_cast<int>(user->m_origin), reason)
            // Log.AddLog(
            //     LOG_WARN,
            //     "[GPProxy] Session creation reply %d has unsupported request origin %d (reason %d). Releasing the request without replying. This path still returns success.",
            //     transactionId, static_cast<int>(user->m_origin), reason)
            L2_LOG_WARN(Log,
                "[GPProxy] Session creation reply %d has unsupported request origin %d (reason %d). Releasing the request without replying; this path still returns success.",
                transactionId, static_cast<int>(user->m_origin), reason);
        }
        user->ReleaseRef();
        return true;
    }

    auto* const serverSocket =
        static_cast<CSocketServer*>(
            user->m_pIOSocket);
    const char extendResult = ExtendLifeTimeWsid(
        user->m_origin,
        serverSocket,
        user->m_loginUser,
        webSessionId,
        user->m_uniqueId,
        user->m_tokenServerId);
    bool result = false;
    if (extendResult != 0) {
        if (user->m_origin ==
            ORIGIN_GAMESERVER_REQUEST) {
            // Log.AddLog(
            //     LOG_WARN,
            //     "WsID created, but extend life time failed(reason:%d)",
            //     static_cast<int>(extendResult))
            // Log.AddLog(
            //     LOG_WARN,
            //     "component=authentication event=session_extension_failed peer=gp_proxy transaction=%d reason_code=%d action=report_login_success",
            //     transactionId, static_cast<int>(extendResult))
            // Log.AddLog(
            //     LOG_WARN,
            //     "[GPProxy] Could not extend the session for request %d (reason %d). Login is still reported as successful to the world server.",
            //     transactionId, static_cast<int>(extendResult))
            L2_LOG_WARN(Log,
                "[GPProxy] Could not extend the session for request %d (reason %d). Login is still reported as successful to the world server.",
                transactionId, static_cast<int>(extendResult));
            const WorldServer world =
                g_ServerList.GetAt(serverSocket->serverid);
            SendSocket(
                world.inner_addr,
                "cdcbbc",
                0x1b,
                user->m_loginUser.uid,
                MapGpProxyToAuthReason(0),
                0x10,
                &webSessionId,
                0x10,
                &user->m_uniqueId,
                user->m_tokenServerId);
            result = true;
        } else {
            // Log.AddLog(
            //     LOG_ERROR,
            //     "Invalid origin %d for ExtendLifeTimeWsid (RecvCreateWebsessionAck)",
            //     static_cast<int>(user->m_origin))
            // Log.AddLog(
            //     LOG_WARN,
            //     "component=authentication event=unsupported_request_origin peer=gp_proxy operation=extend_session transaction=%d origin=%d reason_code=%d action=release_request_without_reply",
            //     transactionId, static_cast<int>(user->m_origin), static_cast<int>(extendResult))
            // Log.AddLog(
            //     LOG_WARN,
            //     "[GPProxy] Session extension reply %d has unsupported request origin %d (reason %d). Releasing the request without replying.",
            //     transactionId, static_cast<int>(user->m_origin), static_cast<int>(extendResult))
            L2_LOG_WARN(Log,
                "[GPProxy] Session extension reply %d has unsupported request origin %d (reason %d). Releasing the request without replying.",
                transactionId, static_cast<int>(user->m_origin), static_cast<int>(extendResult));
        }
    }
    user->ReleaseRef();
    return result;
}

// L2Authd.exe 0x0043C3D0, Src/gpproxyservermgr.cpp:1213-1279.
bool CGpProxyServerMgr::RecvSecAuthListServicesAck(
    const unsigned char* packet) {
    CGpProxyUser* user = nullptr;
    const int transactionId = GetIntFromPacket(&packet);
    if (!DelUserWait(transactionId, user)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "CGpProxyServerMgr::RecvSecAuthListServicesAck DelUserWait failed.")
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=reply_without_pending_request peer=gp_proxy operation=list_secondary_services transaction=%d action=ignore_reply",
        //     transactionId)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[GPProxy] Ignoring secondary authentication service list reply %d: no request is pending.",
        //     transactionId)
        L2_LOG_WARN(Log,
            "[GPProxy] Ignoring secondary authentication service list reply %d: no request is pending.",
            transactionId);
        return false;
    }
    if (user->m_pSession == nullptr) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "CGpProxyServerMgr::RecvSecAuthListServicesAck ClientSession is null.")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=authentication event=reply_context_missing peer=gp_proxy operation=list_secondary_services transaction=%d reason=client_session_missing action=release_request_without_reply",
        //     transactionId)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[GPProxy] Cannot deliver secondary authentication service list reply %d: client session is missing. Releasing the request without replying.",
        //     transactionId)
        L2_LOG_ERROR(Log,
            "[GPProxy] Cannot deliver secondary authentication service list reply %d: client session is missing. Releasing the request without replying.",
            transactionId);
        user->ReleaseRef();
        return false;
    }

    const int result = GetIntFromPacket(&packet);
    if (result != 0) {
        // Log.AddLog(
        //     LOG_DEBUG,
        //     "CGpProxyServerMgr::RecvSecAuthListServicesAck Received error %u",
        //     static_cast<unsigned int>(result))
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=service_reply_failed peer=gp_proxy operation=list_secondary_services transaction=%d reason_code=%d action=reject_login client_reason=%u",
        //     transactionId, result, static_cast<unsigned int>(kGpProxyFailure))
        // Log.AddLog(
        //     LOG_WARN,
        //     "[GPProxy] The secondary authentication service list request %d failed (reason %d). Rejecting login with client reason %u.",
        //     transactionId, result, static_cast<unsigned int>(kGpProxyFailure))
        L2_LOG_WARN(Log,
            "[GPProxy] The secondary authentication service list request %d failed (reason %d). Rejecting login with client reason %u.",
            transactionId, result, static_cast<unsigned int>(kGpProxyFailure));
        user->m_pSession->SendFail(
            1,
            kGpProxyFailure,
            nullptr);
        user->ReleaseRef();
        return false;
    }

    const int serviceCount = GetIntFromPacket(&packet);
    bool unsupported = false;
    SecurityServiceFlag& securityFlag =
        user->m_preLoginUser.account.securityFlag;
    for (int index = 0;
         index < serviceCount;
         ++index) {
        GetIntFromPacket(&packet);
        const auto npType = static_cast<NpSecAuth::Enum>(
            GetShortFromPacket(&packet));
        const auto npState =
            static_cast<SecurityServiceFlag::NpEnum>(
                GetShortFromPacket(&packet));
        if (config.useSecurityCard &&
            npType == NpSecAuth::SECURITYCARDNCJ) {
            continue;
        }
        if (!securityFlag.SetFromNp(npType, npState)) {
            // Log.AddLog(
            //     LOG_DEBUG,
            //     "CGpProxyServerMgr::RecvSecAuthListServicesAck not supported 2th auth type %u",
            //     static_cast<unsigned int>(npType))
            // Log.AddLog(
            //     LOG_WARN,
            //     "component=authentication event=unsupported_security_service peer=gp_proxy operation=list_secondary_services transaction=%d service_type=%d action=reject_login client_reason=%u",
            //     transactionId, static_cast<int>(npType), static_cast<unsigned int>(kGpProxyFailure))
            // Log.AddLog(
            //     LOG_WARN,
            //     "[GPProxy] The secondary authentication service list reply %d contains unsupported security service type %d. Rejecting login with client reason %u.",
            //     transactionId, static_cast<int>(npType), static_cast<unsigned int>(kGpProxyFailure))
            L2_LOG_WARN(Log,
                "[GPProxy] The secondary authentication service list reply %d contains unsupported security service type %d. Rejecting login with client reason %u.",
                transactionId, static_cast<int>(npType), static_cast<unsigned int>(kGpProxyFailure));
            unsupported = true;
            break;
        }
    }

    if (unsupported) {
        user->m_pSession->SendFail(
            1,
            kGpProxyFailure,
            nullptr);
    } else if (accountdb.AuthenticateUserSecondary(
                   user->m_pSession,
                   user->m_preLoginUser)) {
        accountdb.AuthorizeUser(
            user->m_pSession,
            user->m_preLoginUser);
    }
    user->ReleaseRef();
    return false;
}

// L2Authd.exe 0x0043C5F0, Src/gpproxyservermgr.cpp:1340-1410.
bool CGpProxyServerMgr::RecvSecAuthVerifyOtpAck(
    const unsigned char* packet) {
    const int transactionId = GetIntFromPacket(&packet);
    CGpProxyUser* user = nullptr;
    if (!DelUserWait(transactionId, user)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "CGpProxyServerMgr::RecvSecAuthVerifyOtpAck DelUserWait failed.")
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=reply_without_pending_request peer=gp_proxy operation=verify_otp transaction=%d action=ignore_reply",
        //     transactionId)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[GPProxy] Ignoring one-time password verification reply %d: no request is pending.",
        //     transactionId)
        L2_LOG_WARN(Log,
            "[GPProxy] Ignoring one-time password verification reply %d: no request is pending.",
            transactionId);
        return false;
    }
    if (user->m_pSession == nullptr) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "CGpProxyServerMgr::RecvSecAuthVerifyOtpAck ClientSession is null.")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=authentication event=reply_context_missing peer=gp_proxy operation=verify_otp transaction=%d reason=client_session_missing action=release_request_without_reply",
        //     transactionId)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[GPProxy] Cannot deliver one-time password verification reply %d: client session is missing. Releasing the request without replying.",
        //     transactionId)
        L2_LOG_ERROR(Log,
            "[GPProxy] Cannot deliver one-time password verification reply %d: client session is missing. Releasing the request without replying.",
            transactionId);
        user->ReleaseRef();
        return false;
    }

    CClientSession* const session = user->m_pSession;
    PreLoginUser& login = user->m_preLoginUser;
    SecurityUsage& securityUsage =
        login.account.securityUsage;
    const SecAuth::Enum authType = SecAuth::GOOGLEOTP;
    const int result = GetIntFromPacket(&packet);
    if (result == 0x13) {
        if (session->UpdateOtpFailCount() <
            config.OTPRetryCount) {
            // Log.AddLog(LOG_NORMAL,
            //     "component=authentication event=otp_rejected peer=gp_proxy transaction=%d reason_code=%d action=request_retry retry_limit=%d",
            //     transactionId, result, config.OTPRetryCount)
            // Log.AddLog(LOG_NORMAL,
            //     "[GPProxy] One-time password rejected for request %d (reason %d). Asking the client to try again. Configured attempt limit %d.",
            //     transactionId, result, config.OTPRetryCount)
            L2_LOG_INFO(Log,
                "[GPProxy] One-time password rejected for request %d (reason %d). Asking the client to try again; configured attempt limit %d.",
                transactionId, result, config.OTPRetryCount);
            session->ResetOtpAnswer();
            session->Send(
                "cdhhc",
                0x0d,
                login.account.uid,
                session->GetOtpFailCount(),
                config.OTPRetryCount,
                static_cast<int>(authType));
        } else {
            // Log.AddLog(LOG_NORMAL,
            //     "component=authentication event=otp_rejected peer=gp_proxy transaction=%d reason_code=%d action=reject_login reason=retry_limit_reached retry_limit=%d client_reason=3",
            //     transactionId, result, config.OTPRetryCount)
            // Log.AddLog(LOG_NORMAL,
            //     "[GPProxy] One-time password rejected for request %d (reason %d). The configured attempt limit of %d was reached. Rejecting login with client reason 3.",
            //     transactionId, result, config.OTPRetryCount)
            L2_LOG_INFO(Log,
                "[GPProxy] One-time password rejected for request %d (reason %d). The configured attempt limit of %d was reached; rejecting login with client reason 3.",
                transactionId, result, config.OTPRetryCount);
            securityUsage.SetFailed(authType);
            session->SendFail(1, 3, nullptr);
        }
    } else if (result == 0) {
        securityUsage.SetPassed(authType);
        if (accountdb.AuthenticateUserSecondary(
                session,
                login)) {
            accountdb.AuthorizeUser(session, login);
        }
    } else {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=service_reply_failed peer=gp_proxy operation=verify_otp transaction=%d reason_code=%d action=reject_login client_reason=%u",
        //     transactionId, result, static_cast<unsigned int>(kGpProxyFailure))
        // Log.AddLog(LOG_WARN,
        //     "[GPProxy] The one-time password verification request %d failed (reason %d). Rejecting login with client reason %u.",
        //     transactionId, result, static_cast<unsigned int>(kGpProxyFailure))
        L2_LOG_WARN(Log,
            "[GPProxy] The one-time password verification request %d failed (reason %d). Rejecting login with client reason %u.",
            transactionId, result, static_cast<unsigned int>(kGpProxyFailure));
        securityUsage.SetFailed(authType);
        session->SendFail(
            1,
            kGpProxyFailure,
            nullptr);
    }
    user->ReleaseRef();
    return false;
}

// L2Authd.exe 0x0043C7A0, Src/gpproxyservermgr.cpp:1282-1337.
bool CGpProxyServerMgr::RecvSecAuthAuthenticatedListAck(
    const unsigned char* packet) {
    CGpProxyUser* user = nullptr;
    const int transactionId = GetIntFromPacket(&packet);
    if (!DelUserWait(transactionId, user)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "CGpProxyServerMgr::RecvSecAuthAuthenticatedListAck DelUserWait failed.")
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=reply_without_pending_request peer=gp_proxy operation=list_authenticated_services transaction=%d action=ignore_reply",
        //     transactionId)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[GPProxy] Ignoring authenticated service list reply %d: no request is pending.",
        //     transactionId)
        L2_LOG_WARN(Log,
            "[GPProxy] Ignoring authenticated service list reply %d: no request is pending.",
            transactionId);
        return false;
    }
    if (user->m_pSession == nullptr) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "CGpProxyServerMgr::RecvSecAuthAuthenticatedListAck ClientSession is null.")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=authentication event=reply_context_missing peer=gp_proxy operation=list_authenticated_services transaction=%d reason=client_session_missing action=release_request_without_reply",
        //     transactionId)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[GPProxy] Cannot deliver authenticated service list reply %d: client session is missing. Releasing the request without replying.",
        //     transactionId)
        L2_LOG_ERROR(Log,
            "[GPProxy] Cannot deliver authenticated service list reply %d: client session is missing. Releasing the request without replying.",
            transactionId);
        user->ReleaseRef();
        return false;
    }

    const int result = GetIntFromPacket(&packet);
    if (result != 0) {
        // Log.AddLog(
        //     LOG_DEBUG,
        //     "CGpProxyServerMgr::RecvSecAuthAuthenticatedListAck Received error %u",
        //     static_cast<unsigned int>(result))
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=service_reply_failed peer=gp_proxy operation=list_authenticated_services transaction=%d reason_code=%d action=reject_login client_reason=%u",
        //     transactionId, result, static_cast<unsigned int>(kGpProxyFailure))
        // Log.AddLog(
        //     LOG_WARN,
        //     "[GPProxy] The authenticated service list request %d failed (reason %d). Rejecting login with client reason %u.",
        //     transactionId, result, static_cast<unsigned int>(kGpProxyFailure))
        L2_LOG_WARN(Log,
            "[GPProxy] The authenticated service list request %d failed (reason %d). Rejecting login with client reason %u.",
            transactionId, result, static_cast<unsigned int>(kGpProxyFailure));
        user->m_pSession->SendFail(
            1,
            kGpProxyFailure,
            nullptr);
        user->ReleaseRef();
        return false;
    }

    const int authenticationCount =
        GetShortFromPacket(&packet);
    for (int index = 0;
         index < authenticationCount;
         ++index) {
        const auto npType = static_cast<NpSecAuth::Enum>(
            GetShortFromPacket(&packet));
        GetInt64FromPacket(&packet);
        if (!user->m_preLoginUser.account.securityUsage
                 .SetPassedFromNpCode(npType)) {
            // Log.AddLog(
            //     LOG_DEBUG,
            //     "CGpProxyServerMgr::RecvSecAuthAuthenticatedListAck not supported auth type %u",
            //     static_cast<unsigned int>(npType))
            // Log.AddLog(
            //     LOG_WARN,
            //     "component=authentication event=unsupported_security_service peer=gp_proxy operation=list_authenticated_services transaction=%d service_type=%d action=reject_login client_reason=%u",
            //     transactionId, static_cast<int>(npType), static_cast<unsigned int>(kGpProxyFailure))
            // Log.AddLog(
            //     LOG_WARN,
            //     "[GPProxy] The authenticated service list reply %d contains unsupported security service type %d. Rejecting login with client reason %u.",
            //     transactionId, static_cast<int>(npType), static_cast<unsigned int>(kGpProxyFailure))
            L2_LOG_WARN(Log,
                "[GPProxy] The authenticated service list reply %d contains unsupported security service type %d. Rejecting login with client reason %u.",
                transactionId, static_cast<int>(npType), static_cast<unsigned int>(kGpProxyFailure));
            user->m_pSession->SendFail(
                1,
                kGpProxyFailure,
                nullptr);
            user->ReleaseRef();
            return false;
        }
    }

    const char listResult = ListSecAuthServices(
        user->m_origin,
        user->m_pSession,
        user->m_preLoginUser);
    if (listResult != 0) {
        user->m_pSession->SendFail(
            1,
            listResult,
            nullptr);
    }
    user->ReleaseRef();
    return false;
}

// L2Authd.exe 0x0043C990, Src/gpproxyservermgr.cpp:1463-1545.
bool CGpProxyServerMgr::RecvSecondPasswordAck(
    const unsigned char* packet) {
    const int transactionId = GetIntFromPacket(&packet);
    CGpProxyUser* user = nullptr;
    if (!DelUserWait(transactionId, user)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "CGpProxyServerMgr::RecvSecondPasswordAck DelUserWait failed.")
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=reply_without_pending_request peer=gp_proxy operation=second_password transaction=%d action=ignore_reply",
        //     transactionId)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[GPProxy] Ignoring second password reply %d: no request is pending.",
        //     transactionId)
        L2_LOG_WARN(Log,
            "[GPProxy] Ignoring second password reply %d: no request is pending.",
            transactionId);
        return false;
    }

    const int uid = user->m_preLoginUser.account.uid;
    LoginUser* login = nullptr;
    if (!accountdb.FindAccount(uid, &login)) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=reply_context_missing peer=gp_proxy operation=second_password transaction=%d account_id=%d reason=account_not_found action=release_request_without_reply",
        //     transactionId, uid)
        // Log.AddLog(LOG_WARN,
        //     "[GPProxy] Cannot deliver the second-password reply for request %d, account %d: account not found. Releasing the request without replying.",
        //     transactionId, uid)
        L2_LOG_WARN(Log,
            "[GPProxy] Cannot deliver the second-password reply for request %d, account %d: account not found. Releasing the request without replying.",
            transactionId, uid);
        user->ReleaseRef();
        return false;
    }

    const int result = GetMappedSecondPasswordResultCode(
        GetIntFromPacket(&packet));
    short firstValue = 0;
    short secondValue = 0;
    int elapsed = 0;
    if (user->m_command != 0) {
        firstValue = GetShortFromPacket(&packet);
        secondValue = GetShortFromPacket(&packet);
        const std::int64_t remoteTime =
            GetInt64FromPacket(&packet);
        const std::time_t currentTime =
            std::time(nullptr);
        elapsed = static_cast<int>(std::difftime(
            static_cast<std::time_t>(remoteTime),
            currentTime));
    }

    const WorldServer world =
        g_ServerList.GetAt(user->m_serverid);
    switch (user->m_command) {
    case 0:
        SendSocket(
            world.inner_addr,
            "cccdd",
            0x17,
            user->m_command,
            user->m_locationNo,
            uid,
            result);
        break;
    case 2:
    case 3:
        SendSocket(
            world.inner_addr,
            "cccddhhd",
            0x17,
            user->m_command,
            user->m_locationNo,
            uid,
            result,
            secondValue,
            firstValue,
            elapsed);
        break;
    case 4: {
        const int bitFlags =
            login->securityUsage.BuildBitFlags();
        const char byteFlags =
            static_cast<char>(bitFlags);
        SendSocket(
            world.inner_addr,
            "cccddhhddc",
            0x17,
            user->m_command,
            user->m_locationNo,
            uid,
            result,
            secondValue,
            firstValue,
            elapsed,
            bitFlags,
            byteFlags);
        break;
    }
    default:
        break;
    }

    login->ReleaseRef();
    user->ReleaseRef();
    return false;
}

// L2Authd.exe 0x0043CCB0, Src/gpproxyservermgr.cpp:730-749.
bool CGpProxyServerMgr::SendVerifyWebSessionReq(
    std::int32_t transactionId,
    WebSessionId& webSessionId,
    in_addr address) {
    CGpProxySocket* const socket = GetValidSocket();
    if (socket == nullptr) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "CGpProxyServerMgr::SendVerifyWebSessionReq failed, there's no connection.")
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=gp_proxy operation=verify_web_session transaction=%d reason=no_connection action=return_failure",
        //     transactionId)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[GPProxy] Cannot submit the web session verification request %d: no connection to the service. Returning failure.",
        //     transactionId)
        L2_LOG_WARN(Log,
            "[GPProxy] Cannot submit the web session verification request %d: no connection to the service. Returning failure.",
            transactionId);
        return false;
    }

    const int country = config.gameId == 8 ? 0x20 : 0x1b;
    const std::uint32_t socketId = socket->m_hSocket;
    const bool sent = socket->Send(
        "cddbdd",
        2,
        transactionId,
        country,
        0x10,
        &webSessionId,
        0,
        address.s_addr);
    if (!sent) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=gp_proxy operation=verify_web_session transaction=%d socket=%u reason=socket_send_rejected action=return_failure",
        //     transactionId, socketId)
        // Log.AddLog(LOG_WARN,
        //     "[GPProxy] Could not send the web session verification request %d on socket %u. Returning failure.",
        //     transactionId, socketId)
        L2_LOG_WARN(Log,
            "[GPProxy] Could not send the web session verification request %d on socket %u. Returning failure.",
            transactionId, socketId);
    }
    socket->ReleaseRef();
    return sent;
}

// L2Authd.exe 0x0043CD40, Src/gpproxyservermgr.cpp:753-775.
bool CGpProxyServerMgr::SendUpdateWebSessionTimeoutReq(
    std::int32_t transactionId,
    WebSessionId& webSessionId,
    in_addr address) {
    if (webSessionId.s_id0 == 0 &&
        webSessionId.s_id1 == 0 &&
        webSessionId.s_id2 == 0 &&
        webSessionId.s_id3 == 0) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=gp_proxy operation=update_session_timeout transaction=%d reason=empty_session_id action=return_failure",
        //     transactionId)
        // Log.AddLog(LOG_WARN,
        //     "[GPProxy] Cannot request a session timeout update for transaction %d: session ID is empty. Returning failure.",
        //     transactionId)
        L2_LOG_WARN(Log,
            "[GPProxy] Cannot request a session timeout update for transaction %d: session ID is empty. Returning failure.",
            transactionId);
        return false;
    }

    CGpProxySocket* const socket = GetValidSocket();
    if (socket == nullptr) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "CGpProxyServerMgr::SendUpdateWebSessionTimeoutReq failed, there's no connection.")
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=gp_proxy operation=update_session_timeout transaction=%d reason=no_connection action=return_failure",
        //     transactionId)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[GPProxy] Cannot submit the session timeout update request %d: no connection to the service. Returning failure.",
        //     transactionId)
        L2_LOG_WARN(Log,
            "[GPProxy] Cannot submit the session timeout update request %d: no connection to the service. Returning failure.",
            transactionId);
        return false;
    }

    const int country = config.gameId == 8 ? 0x20 : 0x1b;
    const std::uint32_t socketId = socket->m_hSocket;
    const bool sent = socket->Send(
        "cddbdd",
        3,
        transactionId,
        country,
        0x10,
        &webSessionId,
        0,
        address.s_addr);
    if (!sent) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=gp_proxy operation=update_session_timeout transaction=%d socket=%u reason=socket_send_rejected action=return_failure",
        //     transactionId, socketId)
        // Log.AddLog(LOG_WARN,
        //     "[GPProxy] Could not send the session timeout update request %d on socket %u. Returning failure.",
        //     transactionId, socketId)
        L2_LOG_WARN(Log,
            "[GPProxy] Could not send the session timeout update request %d on socket %u. Returning failure.",
            transactionId, socketId);
    }
    socket->ReleaseRef();
    return sent;
}

// L2Authd.exe 0x0043CDF0, Src/gpproxyservermgr.cpp:779-826.
bool CGpProxyServerMgr::SendCreateWebSessionReq(
    std::int32_t transactionId,
    LoginUser& login,
    int timeout,
    [[maybe_unused]] in_addr address) {
    char account[65]{};
    StrPrintf(
        account,
        sizeof(account),
        "%s",
        login.loginAccount);

    int country = 0x0d;
    if (login.portalUid == 0) {
        country = config.gameId == 8 ? 0x20 : 0x1b;
    }
    if (config.gameId == 8 &&
        config.convertPortalEmailAccount &&
        config.convertPortalEmailAccountPostfix != nullptr) {
        StrPrintf(
            account,
            sizeof(account),
            "%s@%s",
            login.loginAccount,
            config.convertPortalEmailAccountPostfix);
        account[64] = '\0';
    }

    CGpProxySocket* const socket = GetValidSocket();
    if (socket == nullptr) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "CGpProxyServerMgr::SendCreateWebSessionReq failed, there's no connection.")
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=gp_proxy operation=create_web_session transaction=%d reason=no_connection action=return_failure",
        //     transactionId)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[GPProxy] Cannot submit the web session creation request %d: no connection to the service. Returning failure.",
        //     transactionId)
        L2_LOG_WARN(Log,
            "[GPProxy] Cannot submit the web session creation request %d: no connection to the service. Returning failure.",
            transactionId);
        return false;
    }

    const std::uint32_t socketId = socket->m_hSocket;
    const bool sent = socket->Send(
        "cddbbd",
        1,
        transactionId,
        country,
        65,
        account,
        32,
        login.loginPassword,
        timeout);
    if (!sent) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=gp_proxy operation=create_web_session transaction=%d socket=%u reason=socket_send_rejected action=return_failure",
        //     transactionId, socketId)
        // Log.AddLog(LOG_WARN,
        //     "[GPProxy] Could not send the web session creation request %d on socket %u. Returning failure.",
        //     transactionId, socketId)
        L2_LOG_WARN(Log,
            "[GPProxy] Could not send the web session creation request %d on socket %u. Returning failure.",
            transactionId, socketId);
    }
    socket->ReleaseRef();
    return sent;
}

// L2Authd.exe 0x0043CF60, Src/gpproxyservermgr.cpp:829-851.
bool CGpProxyServerMgr::SendSecAuthListServicesReq(
    std::int32_t transactionId,
    PreLoginUser& login,
    in_addr address) {
    CGpProxySocket* const socket = GetValidSocket();
    if (socket == nullptr) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "CGpProxyServerMgr::SendSecAuthListServicesReq failed. no connected socket.")
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=gp_proxy operation=list_secondary_services transaction=%d reason=no_connection action=return_failure",
        //     transactionId)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[GPProxy] Cannot submit the secondary authentication service list request %d: no connection to the service. Returning failure.",
        //     transactionId)
        L2_LOG_WARN(Log,
            "[GPProxy] Cannot submit the secondary authentication service list request %d: no connection to the service. Returning failure.",
            transactionId);
        return false;
    }

    const std::uint32_t socketId = socket->m_hSocket;
    const bool sent = socket->Send(
        "cdbbdhhd",
        4,
        transactionId,
        0x10,
        &login.portalUser.userId,
        0x10,
        &config.authdAppId,
        0,
        0,
        0,
        address.s_addr);
    if (!sent) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=gp_proxy operation=list_secondary_services transaction=%d socket=%u reason=socket_send_rejected action=return_failure",
        //     transactionId, socketId)
        // Log.AddLog(LOG_WARN,
        //     "[GPProxy] Could not send the secondary authentication service list request %d on socket %u. Returning failure.",
        //     transactionId, socketId)
        L2_LOG_WARN(Log,
            "[GPProxy] Could not send the secondary authentication service list request %d on socket %u. Returning failure.",
            transactionId, socketId);
    }
    socket->ReleaseRef();
    return sent;
}

// L2Authd.exe 0x0043CFE0, Src/gpproxyservermgr.cpp:875-896.
bool CGpProxyServerMgr::SendSecAuthVerifyOtpReq(
    std::int32_t transactionId,
    PreLoginUser& login,
    in_addr address) {
    CGpProxySocket* const socket = GetValidSocket();
    if (socket == nullptr) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "CGpProxyServerMgr::SendSecAuthVerifyOtpReq failed. no connected socket.")
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=gp_proxy operation=verify_otp transaction=%d reason=no_connection action=return_failure",
        //     transactionId)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[GPProxy] Cannot submit the one-time password verification request %d: no connection to the service. Returning failure.",
        //     transactionId)
        L2_LOG_WARN(Log,
            "[GPProxy] Cannot submit the one-time password verification request %d: no connection to the service. Returning failure.",
            transactionId);
        return false;
    }

    const std::uint32_t socketId = socket->m_hSocket;
    const bool sent = socket->Send(
        "cdbbdhdd",
        5,
        transactionId,
        0x10,
        &login.portalUser.userId,
        0x10,
        &config.authdAppId,
        0,
        5,
        login.otpAnswer,
        address.s_addr);
    if (!sent) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=gp_proxy operation=verify_otp transaction=%d socket=%u reason=socket_send_rejected action=return_failure",
        //     transactionId, socketId)
        // Log.AddLog(LOG_WARN,
        //     "[GPProxy] Could not send the one-time password verification request %d on socket %u. Returning failure.",
        //     transactionId, socketId)
        L2_LOG_WARN(Log,
            "[GPProxy] Could not send the one-time password verification request %d on socket %u. Returning failure.",
            transactionId, socketId);
    }
    socket->ReleaseRef();
    return sent;
}

// L2Authd.exe 0x0043D060, Src/gpproxyservermgr.cpp:854-872.
bool CGpProxyServerMgr::SendSecAuthAuthenticatedListReq(
    std::int32_t transactionId,
    PreLoginUser& login,
    in_addr address) {
    CGpProxySocket* const socket = GetValidSocket();
    if (socket == nullptr) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "CGpProxyServerMgr::SendSecAuthAuthenticatedListReq failed. no connected socket.")
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=gp_proxy operation=list_authenticated_services transaction=%d reason=no_connection action=return_failure",
        //     transactionId)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[GPProxy] Cannot submit the authenticated service list request %d: no connection to the service. Returning failure.",
        //     transactionId)
        L2_LOG_WARN(Log,
            "[GPProxy] Cannot submit the authenticated service list request %d: no connection to the service. Returning failure.",
            transactionId);
        return false;
    }

    const std::uint32_t socketId = socket->m_hSocket;
    const bool sent = socket->Send(
        "cdbds",
        6,
        transactionId,
        0x10,
        &login.portalUser.userId,
        address.s_addr,
        login.tokenInfo.token);
    if (!sent) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=gp_proxy operation=list_authenticated_services transaction=%d socket=%u reason=socket_send_rejected action=return_failure",
        //     transactionId, socketId)
        // Log.AddLog(LOG_WARN,
        //     "[GPProxy] Could not send the authenticated service list request %d on socket %u. Returning failure.",
        //     transactionId, socketId)
        L2_LOG_WARN(Log,
            "[GPProxy] Could not send the authenticated service list request %d on socket %u. Returning failure.",
            transactionId, socketId);
    }
    socket->ReleaseRef();
    return sent;
}

// L2Authd.exe 0x0043D0E0, Src/gpproxyservermgr.cpp:900-1010.
bool CGpProxyServerMgr::SendSecondPasswordReq(
    int transactionId,
    CGpProxyUser* user,
    const unsigned char* packet) {
    const char command = GetCharFromPacket(&packet);
    const char locationNo = GetCharFromPacket(&packet);
    const int uid = GetIntFromPacket(&packet);

    LoginUser* login = nullptr;
    if (!accountdb.FindAccount(uid, &login)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "CGpProxyServerMgr::SendSecondPasswordReq failed. can't find account.")
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=gp_proxy operation=second_password transaction=%d account_id=%d reason=account_not_found action=return_failure",
        //     transactionId, uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[GPProxy] Cannot submit a second-password request %d for account %d: account not found. Returning failure.",
        //     transactionId, uid)
        L2_LOG_WARN(Log,
            "[GPProxy] Cannot submit a second-password request %d for account %d: account not found. Returning failure.",
            transactionId, uid);
        return false;
    }

    user->m_preLoginUser.account.uid = uid;
    user->m_locationNo = locationNo;
    user->m_command = command;

    CGpProxySocket* const socket = GetValidSocket();
    if (socket == nullptr) {
        // The selected image returns before releasing the FindAccount ref.
        // Log.AddLog(
        //     LOG_ERROR,
        //     "CGpProxyServerMgr::SendSecondPasswordReq failed. no connected socket.")
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=gp_proxy operation=second_password transaction=%d reason=no_connection action=return_failure",
        //     transactionId)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[GPProxy] Cannot submit the second password request %d: no connection to the service. Returning failure.",
        //     transactionId)
        L2_LOG_WARN(Log,
            "[GPProxy] Cannot submit the second password request %d: no connection to the service. Returning failure.",
            transactionId);
        return false;
    }

    std::array<unsigned char, 16> firstInput{};
    std::array<unsigned char, 16> secondInput{};
    std::array<unsigned char, 16> firstHash{};
    std::array<unsigned char, 16> secondHash{};
    in_addr address{};

    if (command != 4) {
        GetBinFromPacket(
            &packet,
            firstInput.size(),
            firstInput.data());

        const auto makeHash = [](
            int accountUid,
            const std::array<unsigned char, 16>& input,
            std::array<unsigned char, 16>& digest) {
            const std::string key =
                "_" + std::to_string(accountUid) + "_";
            std::array<unsigned char, 30> material{};
            material.fill('$');
            std::memcpy(
                material.data(),
                key.data(),
                key.size());
            std::memcpy(
                material.data() + 14,
                input.data(),
                input.size());

            unsigned char salt[] =
                "ti6&%fjgie115/*50 54_+60y6a^$@hg";
            MD5 md5;
            md5.Update(salt, 0x20);
            md5.Update(material.data(), material.size());
            md5.Final(digest.data());
        };

        makeHash(uid, firstInput, firstHash);
        if (command == 2) {
            GetBinFromPacket(
                &packet,
                secondInput.size(),
                secondInput.data());
            makeHash(uid, secondInput, secondHash);
        }
        address = GetAddrFromPacket(&packet);
    }

    const std::uint32_t socketId = socket->m_hSocket;
    bool sent = false;
    switch (command) {
    case 0:
    case 3:
        sent = socket->Send(
            "cdbbcbd",
            8,
            transactionId,
            0x10,
            &login->userId,
            0x10,
            &config.authdAppId,
            command,
            0x10,
            firstHash.data(),
            address.s_addr);
        break;
    case 1:
        // Noise: The final submission result reports both this and other unsupported commands.
        // Log.AddLog(
        //     LOG_ERROR,
        //     "CGpProxyServerMgr::SendSecondPasswordReq faild. invalid command.")
        break;
    case 2:
        sent = socket->Send(
            "cdbbcbbd",
            8,
            transactionId,
            0x10,
            &login->userId,
            0x10,
            &config.authdAppId,
            command,
            0x10,
            firstHash.data(),
            0x10,
            secondHash.data(),
            address.s_addr);
        break;
    case 4:
        sent = socket->Send(
            "cdbbc",
            8,
            transactionId,
            0x10,
            &login->userId,
            0x10,
            &config.authdAppId,
            command);
        break;
    default:
        break;
    }

    login->ReleaseRef();
    if (!sent) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=request_not_submitted peer=gp_proxy operation=second_password transaction=%d socket=%u command=%u reason=%s action=return_failure",
        //     transactionId, socketId, static_cast<unsigned int>(static_cast<unsigned char>(command)),
        //     command == 0 || command == 2 || command == 3 || command == 4
        //         ? "socket_send_rejected" : "unsupported_command")
        // Log.AddLog(LOG_WARN,
        //     "[GPProxy] Could not submit second-password request %d on socket %u (command %u): %s. Returning failure.",
        //     transactionId, socketId, static_cast<unsigned int>(static_cast<unsigned char>(command)),
        //     command == 0 || command == 2 || command == 3 || command == 4
        //         ? "the socket rejected the send" : "the command is unsupported")
        L2_LOG_WARN(Log,
            "[GPProxy] Could not submit second-password request %d on socket %u (command %u): %s. Returning failure.",
            transactionId, socketId, static_cast<unsigned int>(static_cast<unsigned char>(command)),
            command == 0 || command == 2 || command == 3 || command == 4
                ? "the socket rejected the send" : "the command is unsupported");
    }
    socket->ReleaseRef();
    return sent;
}

// L2Authd.exe 0x0043D380, Src/gpproxyservermgr.cpp:1549-1604.
bool CGpProxyServerMgr::ClientLoginCallback(
    CGpProxyAccountData* accountData,
    CGpProxyUser* user) {
    CClientSession* const session = user->m_pSession;
    if (accountData->reason != 0) {
        // Log.AddLog(LOG_NORMAL,
        //     "component=authentication event=login_rejected peer=gp_proxy transaction=%d reason_code=%d action=send_mapped_failure",
        //     accountData->tid, accountData->reason)
        // Log.AddLog(LOG_NORMAL,
        //     "[GPProxy] Login rejected for request %d (reason %d). Sending the mapped failure reason to the client.",
        //     accountData->tid, accountData->reason)
        L2_LOG_INFO(Log,
            "[GPProxy] Login rejected for request %d (reason %d). Sending the mapped failure reason to the client.",
            accountData->tid, accountData->reason);
        session->SendFail(
            1,
            static_cast<char>(
                MapGpProxyToAuthReason(
                    accountData->reason)),
            nullptr);
        return false;
    }

    std::memset(
        user->m_preLoginUser.playAccount,
        0,
        sizeof(user->m_preLoginUser.playAccount));
    StrCopy(
        user->m_preLoginUser.playAccount,
        accountData->account,
        sizeof(user->m_preLoginUser.playAccount));

    bool authenticated = false;
    if (accountData->portalUid == 0) {
        user->m_preLoginUser.portalUser.uid = 0;
        authenticated = accountdb.AuthenticateUser(
            session,
            &user->m_preLoginUser,
            false);
    } else {
        std::memset(
            user->m_preLoginUser.portalUser.account,
            0,
            sizeof(user->m_preLoginUser.portalUser.account));
        StrCopy(
            user->m_preLoginUser.portalUser.account,
            accountData->account,
            sizeof(user->m_preLoginUser.portalUser.account));
        PortalUser& portal =
            user->m_preLoginUser.portalUser;
        portal.uid = accountData->portalUid;
        portal.birthdate = accountData->birthdate;
        portal.sex = accountData->sex;
        portal.blockCode = accountData->blockCode;
        portal.noticeCode = accountData->noticeCode;
        portal.statusCode = accountData->statusCode;
        portal.blockReasonCode =
            accountData->blockReasonCode;
        std::memcpy(
            &portal.userId,
            &accountData->userId,
            sizeof(portal.userId));
        authenticated = accountdb.AuthenticateUserPortal(
            session,
            &user->m_preLoginUser);
    }

    if (!authenticated) {
        return false;
    }
    return accountdb.AuthorizeUser(
        session,
        user->m_preLoginUser);
}

// L2Authd.exe 0x0043D690, Src/gpproxyservermgr.cpp:1414-1458.
int GetMappedSecondPasswordResultCode(int result) {
    if (result == 0) {
        return 0;
    }

    int mapped = 7;
    switch (result) {
    case 0x15:
        mapped = 6;
        break;
    case 0x16:
        mapped = 2;
        break;
    case 0x17:
    case 0x18:
        mapped = 1;
        break;
    default:
        break;
    }
    return mapped + 0x3c;
}
