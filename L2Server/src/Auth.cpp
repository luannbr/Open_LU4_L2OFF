#include "Auth.h"
#include "Shared/src/MemorySub.h"

#include "CConfig.h"
#include "DB.h"
#include "Default.h"
#include "NPSystem.h"
#include "User.h"
#include "UserSocket.h"
#include "VipAttendance.h"

#include "Shared/src/IOCPMessage.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/NativeSocketTransport.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#else
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

void CloseNativeSocket(std::uint64_t socket) noexcept {
#if defined(_WIN32)
    ::closesocket(static_cast<SOCKET>(socket));
#else
    ::close(static_cast<int>(socket));
#endif
}

void CopyConnectedAccountName(char (&destination)[15], const char* source) {
    if (source == nullptr) {
        destination[0] = '\0';
        errno = EINVAL;
        myInvalidParameterHandler(nullptr, nullptr, nullptr, 0, 0);
        return;
    }

    std::size_t length = 0;
    while (length < sizeof(destination) && source[length] != '\0') {
        ++length;
    }
    if (length == sizeof(destination)) {
        destination[0] = '\0';
        errno = ERANGE;
        myInvalidParameterHandler(nullptr, nullptr, nullptr, 0, 0);
        return;
    }
    std::memcpy(destination, source, length + 1U);
}

void CopyPCCafeCode(char (&destination)[9], const char* source) {
    if (source == nullptr) {
        destination[0] = '\0';
        errno = EINVAL;
        myInvalidParameterHandler(nullptr, nullptr, nullptr, 0, 0);
        return;
    }

    std::size_t length = 0;
    while (length < sizeof(destination) && source[length] != '\0') {
        ++length;
    }
    if (length == sizeof(destination)) {
        destination[0] = '\0';
        errno = ERANGE;
        myInvalidParameterHandler(nullptr, nullptr, nullptr, 0, 0);
        return;
    }
    std::memcpy(destination, source, length + 1U);
}

bool TargetWideEqualForCount(const SharedUtilsWideChar* left, const SharedUtilsWideChar* right, std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
        if (left[index] == SharedUtilsWideChar{}) {
            return true;
        }
    }
    return true;
}

} // namespace

CAuthD authd;
std::uint32_t g_nOneTimeKey;
OneTimeKeyMap g_OneTimeKeyMap;
AccountAuthInfoMap g_AccountAuthInfoMap;
RWLock KeyLock;
RWLock AccountAuthInfoLock;
AuthUserDataInfoMap g_AuthUserDataInfoMap;
RWLock AuthUserDataInfoLock;
AccountVIPLevelByDBMap g_AccountVIPLevelByDBMap;
RWLock g_AccountVIPLevelByDBLock;

class CPlaylistSender : public CIOCPMessage {
public:
    CPlaylistSender(CISocket* socket, int count, int size, char* buffer);

    ~CPlaylistSender() override = default;

    void OnReceive(int) override;

    int m_nCount;
    int m_nSize;
    char* m_pBuf;
    CISocket* m_pSocket;
};


CPlaylistSender::CPlaylistSender(CISocket* socket, int count, int size, char* buffer) :
    m_nCount(count),
    m_nSize(size),
    m_pBuf(static_cast<char*>(std::malloc(static_cast<std::size_t>(size)))),
    m_pSocket(socket) {
    std::memcpy(m_pBuf, buffer, static_cast<std::size_t>(size));
    L2_ADD_REF(m_pSocket, ObjectRefType::Anonymous);
}

void CPlaylistSender::OnReceive(int) {
    if (GetCurrentThreadRemainIOBufferPoolCount() < 100) {
        auto* const sender = new CPlaylistSender(m_pSocket, m_nCount, m_nSize, m_pBuf);
        sender->Post(0);
    } else {
        m_pSocket->Send("cdb", 0x0b, m_nCount, m_nSize, m_pBuf);
    }
    L2_RELEASE(m_pSocket, ObjectRefType::Anonymous);
    std::free(m_pBuf);
}

// The current CRT initializer at 0x00d75ca8 assigns the 54th class id.
// Portable registration replaces the shipping number 54.
L2_REGISTER_OBJECT_TYPE(CAuthD)

CAuthD::CAuthD() {
    L2_GUARD_FUNCTION();
    m_bSendAuth = false;
    m_connectedUserList.clear();
    m_sendUserList.clear();
    m_lastSendUserCount = 0;
}

CAuthD::~CAuthD() = default;

L2_DEFINE_OBJECT_RELEASE(CAuthD)

bool CAuthD::ConnectAuthD() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    const SOCKET created = ::socket(AF_INET, SOCK_STREAM, 0);
#else
    const int created = ::socket(AF_INET, SOCK_STREAM, 0);
#endif
    const std::uint64_t nativeSocket = static_cast<std::uint64_t>(created);

    // Diagnostic-only view of the configured IPv4 address. No shared formatter buffer.
    const auto* const peerBytes = reinterpret_cast<const unsigned char*>(&authServerAddr);

    ::sockaddr_in localAddress{};
    localAddress.sin_family = AF_INET;
    if (::bind(created, reinterpret_cast<const ::sockaddr*>(&localAddress), sizeof(localAddress)) == -1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Can't bind socket to auth server")
        // Log.Add(
        //     LOG_ERROR,
        //     "[auth] Cannot bind the local socket for AuthD %u.%u.%u.%u:%u (connection %llu, WinSock-compatible error %d)
        //     closing this connection attempt.", static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]),
        //     static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]),
        //     static_cast<unsigned>(static_cast<std::uint16_t>(authServerPort)),
        //     static_cast<unsigned long long>(nativeSocket), GetSocketLastError())
        L2_LOG_ERROR(
            Log,
            "[auth] Cannot bind the local socket for AuthD %u.%u.%u.%u:%u (connection %llu, WinSock-compatible error %d); "
            "closing this connection attempt.",
            static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]),
            static_cast<unsigned>(peerBytes[3]), static_cast<unsigned>(static_cast<std::uint16_t>(authServerPort)),
            static_cast<unsigned long long>(nativeSocket), GetSocketLastError()
        );
        CloseNativeSocket(nativeSocket);
        return false;
    }

    ::sockaddr_in remoteAddress{};
    remoteAddress.sin_family = AF_INET;
    remoteAddress.sin_addr = authServerAddr;
    remoteAddress.sin_port = htons(static_cast<std::uint16_t>(authServerPort));
    if (::connect(created, reinterpret_cast<const ::sockaddr*>(&remoteAddress), sizeof(remoteAddress)) == -1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Can't connect to auth server")
        // Log.Add(
        //     LOG_ERROR,
        //     "[auth] TCP connection to AuthD %u.%u.%u.%u:%u failed (connection %llu, WinSock-compatible error %d). Closing
        //     this connection attempt.", static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]),
        //     static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]),
        //     static_cast<unsigned>(static_cast<std::uint16_t>(authServerPort)),
        //     static_cast<unsigned long long>(nativeSocket), GetSocketLastError())
        L2_LOG_ERROR(
            Log,
            "[auth] TCP connection to AuthD %u.%u.%u.%u:%u failed (connection %llu, WinSock-compatible error %d); closing "
            "this connection attempt.",
            static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]),
            static_cast<unsigned>(peerBytes[3]), static_cast<unsigned>(static_cast<std::uint16_t>(authServerPort)),
            static_cast<unsigned long long>(nativeSocket), GetSocketLastError()
        );
        CloseNativeSocket(nativeSocket);
        return false;
    }

    // Log.Add(
    //     LOG_NORMAL,
    //     L"Connected to auth server")
    // Log.Add(
    //     LOG_NORMAL,
    //     "[auth] TCP connected to AuthD %u.%u.%u.%u:%u (connection %llu). Preparing authentication transport.",
    //     static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]),
    //     static_cast<unsigned>(peerBytes[2]), static_cast<unsigned>(peerBytes[3]),
    //     static_cast<unsigned>(static_cast<std::uint16_t>(authServerPort)),
    //     static_cast<unsigned long long>(nativeSocket))
    L2_LOG_INFO(
        Log, "[auth] TCP connected to AuthD %u.%u.%u.%u:%u (connection %llu); preparing authentication transport.",
        static_cast<unsigned>(peerBytes[0]), static_cast<unsigned>(peerBytes[1]), static_cast<unsigned>(peerBytes[2]),
        static_cast<unsigned>(peerBytes[3]), static_cast<unsigned>(static_cast<std::uint16_t>(authServerPort)),
        static_cast<unsigned long long>(nativeSocket)
    );

    CAuthSocket* const socket = new CAuthSocket(nativeSocket, authPacketTable);
    if (m_spAuthSocket) {
        m_spAuthSocket.SetObject(socket, ObjectRefType::FunctionChain);
    } else {
        CSPointer<CISocket> replacement(socket, ObjectRefType::FunctionChain);
        m_spAuthSocket = replacement;
    }

    const bool bound = portable::BindNativeSocketTransport(
        socket, nativeSocket,
        [](void* owner, const std::uint8_t* data, std::size_t length) {
            return portable::FeedSocketRead(static_cast<CAuthSocket*>(owner), data, length);
        },
        [](void* owner, int error) { portable::FeedSocketTerminal(static_cast<CAuthSocket*>(owner), error); }
    );
    if (!bound) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[auth] Cannot attach AuthD connection %llu to the IO transport. Closing its native socket. The current
        //     initialization path still proceeds.", static_cast<unsigned long long>(nativeSocket))
        L2_LOG_ERROR(
            Log,
            "[auth] Cannot attach AuthD connection %llu to the IO transport; closing its native socket. The current "
            "initialization path still proceeds.",
            static_cast<unsigned long long>(nativeSocket)
        );
        CloseNativeSocket(nativeSocket);
    }
    m_spAuthSocket->Initialize();
    return true;
}

bool CAuthD::InitAuthD() {
    L2_GUARD_FUNCTION();
    const bool connected = ConnectAuthD();
    if (connected) {
        g_nOneTimeKey = 0;
        g_bAuthConnected = true;
    }
    return connected;
}

bool CAuthD::ReconnAuthD() {
    L2_GUARD_FUNCTION();
    return ConnectAuthD();
}

bool CAuthD::ReconnectAuth() {
    L2_GUARD_FUNCTION();
    const bool connected = ReconnAuthD();
    if (connected) {
        if (g_nUseReconnectAuth == 1) {
            // Log.Add(
            //     LOG_NORMAL,
            //     "[auth] Starting player-list resynchronization with AuthD (reconnect mode 1).")
            L2_LOG_INFO(Log, "[auth] Starting player-list resynchronization with AuthD (reconnect mode 1).");
            MakeOnReconnectedPlayerlist();
        } else if (g_nUseReconnectAuth == 2) {
            // Log.Add(
            //     LOG_NORMAL,
            //     "[auth] Rebuilding the connected-account list for AuthD resynchronization (reconnect mode 2).")
            L2_LOG_INFO(Log, "[auth] Rebuilding the connected-account list for AuthD resynchronization (reconnect mode 2).");
            {
                L2_GUARD(m_Lock);
                for (const auto& entry : m_connectedUserList) {
                    delete entry.second;
                }
                m_connectedUserList.clear();
                for (const auto& entry : m_sendUserList) {
                    delete entry.second;
                }
                m_sendUserList.clear();
            }
            MakeConnectedUserList();
        }
    }
    return connected;
}

void CAuthD::SendAuthConnected() {
    L2_GUARD_FUNCTION();
    CSPointer<CISocket> socket(m_spAuthSocket);
    if (socket) {
        socket->Send("c", 10);
        g_bAuthConnected = true;
        m_bSendAuth = true;
        // Log.Add(
        //     LOG_NORMAL,
        //     "[auth] Local authentication sending enabled after issuing the reconnect completion notification.")
        L2_LOG_INFO(Log, "[auth] Local authentication sending enabled after issuing the reconnect completion notification.");
    }
}

void CAuthD::MakeConnectedUserList() {
    L2_GUARD_FUNCTION();

    {
        L2_GUARD(m_Lock);
        FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserSocketMapPlaying, socket, {
            auto* const userInfo = new ConnectedUserInfo;
            std::memset(userInfo, 0, sizeof(*userInfo));
            userInfo->nAccountId = static_cast<std::int32_t>(socket->GetAccountID());
            userInfo->nPayStat = static_cast<std::int32_t>(socket->m_nUserStat);
            userInfo->ip = socket->GetAddress();
            CopyConnectedAccountName(userInfo->szAccountName, socket->GetAccountNameChar());
            m_connectedUserList.emplace(userInfo->nAccountId, userInfo);
        });
    }
    SendConnectedUserList();
}

void CAuthD::SendConnectedUserList() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);

    char buffer[0x5000] = {};
    int bufferLength = 0;
    std::memset(buffer, 0, sizeof(buffer));
    int userCount = 0;

    if (m_connectedUserList.empty()) {
        SendAuthConnected();
        return;
    }

    auto current = m_connectedUserList.begin();
    while (current != m_connectedUserList.end() && userCount < 100) {
        ConnectedUserInfo* const userInfo = current->second;
        const int assembled = ::Assemble(
            buffer + bufferLength, static_cast<int>(sizeof(buffer)) - bufferLength, "dbdd", userInfo->nAccountId, 0x0e,
            userInfo->szAccountName, userInfo->nPayStat, userInfo->ip.s_addr
        );

        ++userCount;
        // Noise: per-account transfer chatter is replaced by one batch summary below.
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[%d] %S %d %d %d",
        //     userCount,
        //     userInfo->szAccountName,
        //     userInfo->nAccountId,
        //     userInfo->nPayStat,
        //     userInfo->ip.s_addr)

        m_sendUserList.emplace(userInfo->nAccountId, userInfo);
        current = m_connectedUserList.erase(current);
        if (assembled > 0) {
            bufferLength += assembled;
        }
    }

    m_lastSendUserCount = userCount;
    // Log.Add(
    //     LOG_NORMAL,
    //     "[auth] Issuing a resynchronization batch for %d accounts (%d payload bytes). %zu accounts remain to transfer.",
    //     userCount, bufferLength, m_connectedUserList.size())
    L2_LOG_INFO(
        Log, "[auth] Issuing a resynchronization batch for %d accounts (%d payload bytes); %zu accounts remain to transfer.",
        userCount, bufferLength, m_connectedUserList.size()
    );
    m_spAuthSocket->Send("cdb", 0x1c, userCount, bufferLength, buffer);
}

void CAuthD::MakeOnReconnectedPlayerlist() {
    L2_GUARD_FUNCTION();

    m_nReconnectTryUserCount = 0;
    m_nReconnectCompleteUserCount = 0;
    std::memset(m_pBuf, 0, sizeof(m_pBuf));
    std::memset(m_pBufLen, 0, sizeof(m_pBufLen));
    std::memset(m_pBufNum, 0, sizeof(m_pBufNum));
    int bufferIndex = 0;
    m_nMaxReconnBufNum = 1;
    m_nReplyReconnBufNum = 0;
    int totalUserCount = 0;

    FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserSocketMapPlaying, socket, {
        if (bufferIndex >= 100) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] reconn buf overflow",
            //     "auth.cpp",
            //     0x181)
            // Log.Add(
            //     LOG_ERROR,
            //     "[auth] Reconnect player-list capacity reached (100 buffers), omitting this account from the transfer.")
            L2_LOG_ERROR(
                Log, "[auth] Reconnect player-list capacity reached (100 buffers); omitting this account from the transfer."
            );
            continue;
        }
        if (g_nUseReconnectAuth != 1) {
            continue;
        }

        const ::in_addr address = socket->GetAddress();
        const int assembled = ::Assemble(
            m_pBuf[bufferIndex] + m_pBufLen[bufferIndex], 0x5000 - m_pBufLen[bufferIndex], "sddddd",
            socket->GetAccountNameChar(), socket->GetAccountID(), address.s_addr, socket->m_nLoginFlag, socket->m_nWarnFlag
        );
        if (assembled < 1) {
            continue;
        }

        m_pBufLen[bufferIndex] += assembled;
        ++m_pBufNum[bufferIndex];
        ++totalUserCount;
        // Noise: this address lookup served only the disabled per-account diagnostic.
        // const ::in_addr logAddress = socket->GetAddress()
        // Noise: per-account transfer chatter is replaced by one prepared-list summary below.
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[%d][%d][%d]%S %d %d %d %d",
        //     totalUserCount,
        //     bufferIndex,
        //     m_pBufNum[bufferIndex],
        //     socket->GetAccountNameChar(),
        //     socket->GetAccountID(),
        //     logAddress.s_addr,
        //     socket->m_nLoginFlag,
        //     socket->m_nWarnFlag)

        if (m_pBufNum[bufferIndex] >= 100) {
            ++bufferIndex;
            ++m_nMaxReconnBufNum;
        }
    });

    // Log.Add(
    //     LOG_NORMAL,
    //     "[auth] Prepared reconnect player list containing %d accounts (reconnect mode 1).",
    //     totalUserCount)
    L2_LOG_INFO(Log, "[auth] Prepared reconnect player list containing %d accounts (reconnect mode 1).", totalUserCount);
    if (totalUserCount > 0 && m_nMaxReconnBufNum > 0) {
        for (int index = 0; index < m_nMaxReconnBufNum; ++index) {
            if (!m_spAuthSocket) {
                break;
            }
            authd.m_nReconnectTryUserCount += m_pBufNum[index];
            SendOnReconnectedPlayerlist(index);
        }
    }
    SendAuthConnected();
}

void CAuthD::SendOnReconnectedPlayerlist(int bufferIndex) {
    L2_GUARD_FUNCTION();
    if (g_nUseReconnectAuth == 1) {
        auto* const sender =
            new CPlaylistSender(m_spAuthSocket.get(), m_pBufNum[bufferIndex], m_pBufLen[bufferIndex], m_pBuf[bufferIndex]);
        sender->Post(0);
    }
}

void CAuthD::SetAuthClosed() {
    m_spAuthSocket.Release();
    m_bSendAuth = false;
}

void CAuthD::CloseAuthSocket() {
    L2_GUARD_FUNCTION();
    CSPointer<CISocket> socket(m_spAuthSocket);
    if (socket) {
        socket->GracefulClose();
    }
}

void CAuthD::RequestWebSessionID(int accountId, int value) {
    L2_GUARD_FUNCTION();
    char webSessionId[16] = {};
    m_spAuthSocket->Send("cddbd", 0x20, accountId, value, 0x10, webSessionId, 0x15180);
}

void CAuthD::RequestCloseWebSessionID(int accountId, char* webSessionId) {
    L2_GUARD_FUNCTION();
    m_spAuthSocket->Send("cdb", 0x22, accountId, 0x10, webSessionId);
}

bool CAuthD::RequestLogin(
    SharedUtilsWideChar* accountName, int accountId, int loginKey, int oneTimeKey, int gameId, CUserSocket* socket
) {
    L2_GUARD_FUNCTION();

    const int storedLoginKey = [&] {
        L2_WRITE_GUARD(KeyLock);
        return g_OneTimeKeyMap[static_cast<std::uint32_t>(accountId)];
    }();
    if (storedLoginKey != loginKey) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) duplicated login()",
        //     L"auth.cpp",
        //     0x26c,
        //     accountId,
        //     2)
        // Log.Add(
        //     LOG_WARN,
        //     "[auth] Rejecting login for account %d: the login ticket does not match the stored ticket (login error 2).",
        //     accountId)
        L2_LOG_WARN(
            Log, "[auth] Rejecting login for account %d: the login ticket does not match the stored ticket (login error 2).",
            accountId
        );
        CUserSocket::OnLoginFailed(
            socket, static_cast<unsigned int>(accountId),
            // 2)
            NPSystem::LEC_GPFAIL_DUPACCOUNT
        );
        return false;
    }

    if (bWriteConnLog) {
        // Log.Add(
        //     LOG_NORMAL,
        //     "Increase user count. g_nServerUserCount(%d)",
        //     g_nServerUserCount)
        // Log.Add(
        //     LOG_NORMAL,
        //     "[auth] Processing login for account %d. Current online-user count is %d.",
        //     accountId, g_nServerUserCount)
        L2_LOG_INFO(
            Log, "[auth] Processing login for account %d; current online-user count is %d.", accountId, g_nServerUserCount
        );
    }

    int timeLeft = 0;
    int userStat = 0;
    int warnFlag = 0;
    int loginFlag = 0;
    int age = 0;
    char chargeMethod = 0;
    int reservations = 0;
    int restTimeChina = 0;
    int playTimeChina = 0;
    char pcCafeCode[9];
    unsigned short premiumLevelFlag = 0;
    int markingTime = 0;
    int dormantType = 0;
    int maxCharacterId = 0;
    int maxLevel = 0;

    {
        L2_WRITE_GUARD(AccountAuthInfoLock);
        const auto account = g_AccountAuthInfoMap.find(static_cast<std::uint32_t>(accountId));
        if (account == g_AccountAuthInfoMap.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] account not found... account_id(%d)",
            //     "auth.cpp",
            //     0x1d7,
            //     accountId)
            // Log.Add(
            //     LOG_ERROR,
            //     "[auth] Login for account %d has no authentication-cache entry. The current path continues with its local
            //     login fields.", accountId)
            L2_LOG_ERROR(
                Log,
                "[auth] Login for account %d has no authentication-cache entry; the current path continues with its local "
                "login fields.",
                accountId
            );
        } else {
            const stAccountAuthInfo& info = account->second;
            timeLeft = info.nTimeLeft;
            userStat = info.nStat;
            warnFlag = info.nWarnFlag;
            loginFlag = info.nLoginFlag;
            age = info.nAge;
            chargeMethod = info.cChargeMethod;
            reservations = info.nReservations;
            restTimeChina = info.nRestTime_china;
            playTimeChina = info.nPlayTime_china;
            CopyPCCafeCode(pcCafeCode, info.sPCCafeCode);
            premiumLevelFlag = info.nPremiumLevelFlag;
            markingTime = info.nMarkingTime;
            dormantType = info.nDormantType;
            if (g_bDualBillingSystem) {
                maxCharacterId = info.nMaxCharId;
                maxLevel = info.nMaxLevel;
            }
        }
    }

    if ((loginFlag & 0x10) != 0) {
        socket->m_BuilderSetting.m_bNoSeondaryAuth = true;
    }
    if ((loginFlag & 0x30) != 0) {
        socket->m_bForceLogin = true;
        socket->m_iPremiumLevel = 2;
    }
    if ((loginFlag & 0x40) != 0) {
        socket->m_b22to6DisableGameForceLogin = true;
    }
    if ((static_cast<unsigned int>(loginFlag) & 0x100U) != 0) {
        socket->m_bEconomicRestriction = true;
    }
    if ((static_cast<unsigned int>(loginFlag) & 0x200U) != 0) {
        socket->SetApplyChinaFatigueSystem(true);
    }
    if ((premiumLevelFlag & 1U) != 0 || TargetWideEqualForCount(accountName, g_szPremiumAccount, 14U)) {
        socket->m_iPremiumLevel = 1;
    }

    if (g_bDualBillingSystem) {
        if (timeLeft == 0 && userStat == 0) {
            if (g_bChargeFree) {
                socket->m_iPremiumLevel = g_bChargeDistinction ? 0 : 2;
            }
        } else {
            socket->m_iPremiumLevel = 2;
        }
        socket->m_nMaxCharId = maxCharacterId;
        socket->m_nMaxLevel = maxLevel;
    }

    socket->SetAge(age);
    socket->SetSocketUseStart();
    socket->SetLeftTime(timeLeft, userStat, warnFlag, chargeMethod, reservations);
    socket->m_nLoginFlag = static_cast<std::uint32_t>(loginFlag);
    socket->SetRestTime_china(restTimeChina);
    socket->SetPlayTime_china(playTimeChina);
    socket->SetPCCafeCode(pcCafeCode);
    socket->SetDormantMarkingTime(markingTime, dormantType);

    if (!AuthPlayGame(accountId, oneTimeKey, gameId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) fail to AuthPlayGame",
        //     L"auth.cpp",
        //     0x241,
        //     accountId,
        //     1)
        // Log.Add(
        //     LOG_ERROR,
        //     "[auth] Cannot issue the play-game notification to AuthD for account %d. Rejecting login with error 1.",
        //     accountId)
        L2_LOG_ERROR(
            Log, "[auth] Cannot issue the play-game notification to AuthD for account %d; rejecting login with error 1.",
            accountId
        );
        CUserSocket::OnLoginFailed(
            socket, static_cast<unsigned int>(accountId),
            // 1)
            NPSystem::LEC_GPFAIL_AUTHPLAYGAME
        );
        return false;
    }

    if (!g_bUseNPLogin) {
        CUserSocket::OnLoginSuccess(socket, 0, nullptr);
        return true;
    }

    L2_GUARD(accountMapSect);
    if (!NPSystem::CNPD::LoginStep_Init(static_cast<unsigned int>(accountId))) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) fail to "
        //     L"g_NPD.LoginStep_Init",
        //     L"auth.cpp",
        //     0x255,
        //     accountId,
        //     5)
        // Log.Add(
        //     LOG_ERROR,
        //     "[auth] Cannot initialize NP login for account %d. Rejecting login with error 5.",
        //     accountId)
        L2_LOG_ERROR(Log, "[auth] Cannot initialize NP login for account %d; rejecting login with error 5.", accountId);
        CUserSocket::OnLoginFailed(
            socket, static_cast<unsigned int>(accountId),
            // 5)
            NPSystem::LEC_NPFAIL_LOGINSTEPINIT
        );
        return false;
    }

    socket->AddTimer(30000, 4);
    if (!AuthRequestAppToken(static_cast<unsigned int>(accountId))) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) fail to "
        //     L"AuthRequestAppToken()",
        //     L"auth.cpp",
        //     0x261,
        //     accountId,
        //     3)
        // Log.Add(
        //     LOG_ERROR,
        //     "[auth] Cannot request an application token from AuthD for account %d. Rejecting login with error 3.",
        //     accountId)
        L2_LOG_ERROR(
            Log, "[auth] Cannot request an application token from AuthD for account %d; rejecting login with error 3.",
            accountId
        );
        CUserSocket::OnLoginFailed(
            socket, static_cast<unsigned int>(accountId),
            // 3)
            NPSystem::LEC_GPFAIL_APPTOKEN_REQ
        );
        return false;
    }
    return true;
}

bool CAuthD::AuthPing(int value) {
    L2_GUARD_FUNCTION();
    if (g_nUseReconnectAuth != 0 && !m_bSendAuth) {
        // Noise: heartbeat replies are expected to stop while authentication sending is disabled. Connection transitions are
        // already reported. Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] auth socket is broken.",
        //     "auth.cpp",
        //     0x19f)
        return false;
    }

    CSPointer<CISocket> socket(m_spAuthSocket);
    if (!socket) {
        return false;
    }
    socket->Send("cd", 8, value);
    return true;
}

bool CAuthD::AuthRequestAgeKoreanStyle(int accountId) {
    L2_GUARD_FUNCTION();
    if (g_nUseReconnectAuth != 0 && !m_bSendAuth) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] auth socket is broken.",
        //     "auth.cpp",
        //     0x27c)
        // Log.Add(
        //     LOG_WARN,
        //     "[auth] Cannot request account age for account %d: authentication sending is disabled. This operation is
        //     skipped.", accountId)
        L2_LOG_WARN(
            Log,
            "[auth] Cannot request account age for account %d: authentication sending is disabled; this operation is "
            "skipped.",
            accountId
        );
        return false;
    }

    CSPointer<CISocket> socket(m_spAuthSocket);
    if (!socket) {
        return false;
    }
    socket->Send("cd", 0x17, accountId);
    return true;
}

bool CAuthD::AuthPlayOk(int accountId) {
    L2_GUARD_FUNCTION();
    if (g_nUseReconnectAuth != 0 && !m_bSendAuth) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] auth socket is broken.",
        //     "auth.cpp",
        //     0x296)
        // Log.Add(
        //     LOG_WARN,
        //     "[auth] Cannot approve login for account %d: authentication sending is disabled. This operation is skipped.",
        //     accountId)
        L2_LOG_WARN(
            Log,
            "[auth] Cannot approve login for account %d: authentication sending is disabled; this operation is skipped.",
            accountId
        );
        return false;
    }

    const std::uint32_t oneTimeKey = [&] {
        L2_WRITE_GUARD(KeyLock);
        ++g_nOneTimeKey;
        g_OneTimeKeyMap[static_cast<std::uint32_t>(accountId)] = g_nOneTimeKey;
        return g_nOneTimeKey;
    }();

    {
        L2_GUARD(g_ReadyAccountMapSect);
        const auto readyAccount = g_ReadyAccountMap.find(static_cast<std::uint32_t>(accountId));
        if (readyAccount == g_ReadyAccountMap.end()) {
            g_ReadyAccountMap.emplace(static_cast<std::uint32_t>(accountId), 1);
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] DualLogin-PlayOK : account already exists "
            //     L"in ReadyAccountMap [%d]",
            //     "auth.cpp",
            //     0x2a6,
            //     accountId)
            // Log.Add(
            //     LOG_WARN,
            //     "[auth] Account %d already has a pending-login entry. Continuing login approval with the newly assigned
            //     ticket.", accountId)
            L2_LOG_WARN(
                Log,
                "[auth] Account %d already has a pending-login entry; continuing login approval with the newly assigned "
                "ticket.",
                accountId
            );
        }
    }

    CSPointer<CISocket> socket(m_spAuthSocket);
    if (!socket) {
        return false;
    }
    if (g_bDualBillingSystem) {
        socket->Send("ccd", 0x26, static_cast<int>(static_cast<std::uint8_t>(g_nAuthServerID)), accountId);
    }
    socket->Send("cdd", 0, accountId, oneTimeKey);
    return true;
}

bool CAuthD::AuthPlayFail(int accountId, short reason) {
    L2_GUARD_FUNCTION();
    if (g_nUseReconnectAuth != 0 && !m_bSendAuth) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] auth socket is broken.",
        //     "auth.cpp",
        //     0x2c5)
        // Log.Add(
        //     LOG_WARN,
        //     "[auth] Cannot report login rejection for account %d: authentication sending is disabled. This operation is
        //     skipped.", accountId)
        L2_LOG_WARN(
            Log,
            "[auth] Cannot report login rejection for account %d: authentication sending is disabled; this operation is "
            "skipped.",
            accountId
        );
        return false;
    }

    CSPointer<CISocket> socket(m_spAuthSocket);
    if (socket) {
        socket->Send("ccd", 1, static_cast<int>(reason), accountId);
    }
    return false;
}

bool CAuthD::AuthPlayGame(int accountId, int oneTimeKey, int gameId) {
    L2_GUARD_FUNCTION();
    if (g_nUseReconnectAuth != 0 && !m_bSendAuth) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] auth socket is broken.",
        //     "auth.cpp",
        //     0x2dd)
        // Log.Add(
        //     LOG_WARN,
        //     "[auth] Cannot issue the play-game notification for account %d: authentication sending is disabled. This
        //     operation is skipped.", accountId)
        L2_LOG_WARN(
            Log,
            "[auth] Cannot issue the play-game notification for account %d: authentication sending is disabled; this "
            "operation is skipped.",
            accountId
        );
        return false;
    }

    CSPointer<CISocket> socket(m_spAuthSocket);
    if (!socket) {
        return false;
    }
    socket->Send("cddd", 2, accountId, oneTimeKey, gameId);
    return true;
}

bool CAuthD::AuthQuitGame(
    int accountId, short reason, int useTime, int playTime, int maxCharacterId, int maxLevel, int closeReason
) {
    L2_GUARD_FUNCTION();
    if (g_nUseReconnectAuth != 0 && !m_bSendAuth) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] auth socket is broken.",
        //     "auth.cpp",
        //     0x31e)
        // Log.Add(
        //     LOG_WARN,
        //     "[auth] Cannot notify logout for account %d: authentication sending is disabled. This operation is skipped.",
        //     accountId)
        L2_LOG_WARN(
            Log,
            "[auth] Cannot notify logout for account %d: authentication sending is disabled; this operation is skipped.",
            accountId
        );
        return false;
    }

    if (playTime == 0) {
        {
            L2_READ_GUARD(AccountAuthInfoLock);
            const auto account = g_AccountAuthInfoMap.find(static_cast<std::uint32_t>(accountId));
            if (account != g_AccountAuthInfoMap.end()) {
                playTime = account->second.nPlayTime_china;
            }
        }
    }

    {
        L2_WRITE_GUARD(AuthUserDataInfoLock);
        g_AuthUserDataInfoMap.erase(static_cast<std::uint32_t>(accountId));
    }

    bool readyAccount = false;
    {
        L2_GUARD(g_ReadyAccountMapSect);
        const auto account = g_ReadyAccountMap.find(static_cast<std::uint32_t>(accountId));
        if (account == g_ReadyAccountMap.end()) {
            if (closeReason != 9 && closeReason != 7) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] DualLogin-QuitGame : account not exists "
                //     L"in ReadyAccountMap [%d]",
                //     "auth.cpp",
                //     0x33b,
                //     accountId)
                // Log.Add(
                //     LOG_WARN,
                //     "[auth] Logout for account %d has no pending-login entry (close reason %d). The logout notification
                //     will not be issued.", accountId, closeReason)
                L2_LOG_WARN(
                    Log,
                    "[auth] Logout for account %d has no pending-login entry (close reason %d); the logout notification "
                    "will not be issued.",
                    accountId, closeReason
                );
            }
        } else {
            g_ReadyAccountMap.erase(static_cast<std::uint32_t>(accountId));
            readyAccount = true;
        }
    }

    CSPointer<CISocket> socket(m_spAuthSocket);
    if (socket && readyAccount) {
        if (!g_bDualBillingSystem) {
            socket->Send("cdhdd", 3, accountId, static_cast<int>(reason), useTime, playTime);
        } else {
            socket->Send("cdhddd", 3, accountId, static_cast<int>(reason), useTime, maxCharacterId, maxLevel);
        }
    }
    return false;
}

void CAuthD::RequestLogout(int accountId, int useTime, int playTime, int maxCharacterId, int maxLevel, int closeReason) {
    L2_GUARD_FUNCTION();

    AuthQuitGame(accountId, 0, useTime, playTime, maxCharacterId, maxLevel, closeReason);

    {
        L2_WRITE_GUARD(KeyLock);
        g_OneTimeKeyMap.erase(static_cast<std::uint32_t>(accountId));
    }

    VipAttendanceSystem::Inst().ReleseUserAttendanceInfo(static_cast<unsigned int>(accountId));
}

void CAuthD::SaveUserDataAndLogout(
    int accountId, int useTime, int playTime, int maxCharacterId, int maxLevel, int closeReason
) {
    L2_GUARD_FUNCTION();

    {
        L2_WRITE_GUARD(AuthUserDataInfoLock);
        const auto userData = g_AuthUserDataInfoMap.find(static_cast<std::uint32_t>(accountId));
        if (userData != g_AuthUserDataInfoMap.end()) {
            userData->second.nUseTime = useTime;
            userData->second.nPlayTime = playTime;
        }
    }

    if (g_bDualBillingSystem) {
        {
            L2_WRITE_GUARD(AccountAuthInfoLock);
            const auto account = g_AccountAuthInfoMap.find(static_cast<std::uint32_t>(accountId));
            if (account == g_AccountAuthInfoMap.end()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] account not found... account_id(%d)",
                //     "auth.cpp",
                //     0x4be,
                //     accountId)
                // Log.Add(
                //     LOG_WARN,
                //     "[auth] Cannot cache character maxima before logout for account %d: no authentication-cache entry.
                //     continuing logout processing.", accountId)
                L2_LOG_WARN(
                    Log,
                    "[auth] Cannot cache character maxima before logout for account %d: no authentication-cache entry; "
                    "continuing logout processing.",
                    accountId
                );
            } else {
                account->second.nMaxCharId = maxCharacterId;
                account->second.nMaxLevel = maxLevel;
            }
        }
    }

    if ((g_nUseReconnectAuth != 0 && !m_bSendAuth) || !m_spAuthSocket) {
        RequestLogout(accountId, useTime, playTime, maxCharacterId, maxLevel, closeReason);
    } else {
        AuthRequestGetUserData(
            accountId, NEW_AUTH_USER_DATA_FOR_SERVER::TYPE_WRITE, NEW_AUTH_USER_DATA_FOR_SERVER::REASON_FOR_CHARACTER_COUNT
        );
    }
}

bool CAuthD::AuthWaitUserReq(int accountId) {
    L2_GUARD_FUNCTION();
    if (!g_bQueueTicketEnabled) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [SHANGHAI_QUEUETICKET] "
        //     L"g_bQueueTicketEnabled(%s) not used ",
        //     L"auth.cpp",
        //     0x2f6,
        //     L"false")
        // Log.Add(
        //     LOG_WARN,
        //     "[auth] Ignoring the wait-queue request for account %d: the login queue is disabled.",
        //     accountId)
        L2_LOG_WARN(Log, "[auth] Ignoring the wait-queue request for account %d: the login queue is disabled.", accountId);
        return false;
    }

    if (g_nUseReconnectAuth != 0 && !m_bSendAuth) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] auth socket is broken.",
        //     L"auth.cpp",
        //     0x2fc)
        // Log.Add(
        //     LOG_WARN,
        //     "[auth] Cannot request a queue position for account %d: authentication sending is disabled. This operation is
        //     skipped.", accountId)
        L2_LOG_WARN(
            Log,
            "[auth] Cannot request a queue position for account %d: authentication sending is disabled; this operation is "
            "skipped.",
            accountId
        );
        return false;
    }

    CSPointer<CISocket> socket(m_spAuthSocket);
    if (socket) {
        socket->Send("cd", 0x1b, accountId);
    }
    return false;
}

bool CAuthD::AuthUserNum(int userCount, int userLimit) {
    L2_GUARD_FUNCTION();
    static_cast<void>(userLimit);
    CSPointer<CISocket> socket(m_spAuthSocket);
    if (socket) {
        socket->Send(
            "chh", 5, static_cast<int>(static_cast<std::int16_t>(userCount)),
            static_cast<int>(static_cast<std::int16_t>(g_nServerUserCountLimit))
        );
    }
    return false;
}

bool CAuthD::AuthSendServerVersion() {
    L2_GUARD_FUNCTION();
    CSPointer<CISocket> socket(m_spAuthSocket);
    if (socket) {
        socket->Send("cd", 5, GetProtocolVersion());
        AuthUserNum(g_nServerUserCount, g_nServerUserCountLimit);
    }
    return false;
}

void CAuthD::RequestBanAccount(int accountId, Auth::BanUserReason reason) {
    L2_GUARD_FUNCTION();
    CSPointer<CISocket> socket(m_spAuthSocket);
    if (socket) {
        socket->Send("cdh", 6, accountId, static_cast<int>(static_cast<std::int16_t>(reason)));
    }
}

void CAuthD::AuthRequestGetValueInfo(int accountId) {
    L2_GUARD_FUNCTION();
    if (g_nUseReconnectAuth != 0 && !m_bSendAuth) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] auth socket is broken.",
        //     "auth.cpp",
        //     0x710)
        // Log.Add(
        //     LOG_WARN,
        //     "[auth] Cannot request prepaid-value information for account %d: authentication sending is disabled. This
        //     operation is skipped.", accountId)
        L2_LOG_WARN(
            Log,
            "[auth] Cannot request prepaid-value information for account %d: authentication sending is disabled; this "
            "operation is skipped.",
            accountId
        );
        return;
    }

    CSPointer<CISocket> socket(m_spAuthSocket);
    if (socket) {
        socket->Send("cd", 0x19, accountId);
    }
}

bool CAuthD::AuthRequestGetUserData(
    int accountId, NEW_AUTH_USER_DATA_FOR_SERVER::DATA_RW_TYPE type, NEW_AUTH_USER_DATA_FOR_SERVER::DATA_RW_REASON reason
) {
    L2_GUARD_FUNCTION();
    if (g_nUseReconnectAuth != 0 && !m_bSendAuth) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] auth socket is broken.",
        //     "auth.cpp",
        //     0x4e9)
        // Log.Add(
        //     LOG_WARN,
        //     "[auth] Cannot request user data for account %d: authentication sending is disabled. This operation is
        //     skipped.", accountId)
        L2_LOG_WARN(
            Log,
            "[auth] Cannot request user data for account %d: authentication sending is disabled; this operation is skipped.",
            accountId
        );
        return false;
    }

    {
        L2_WRITE_GUARD(AuthUserDataInfoLock);
        const auto account = g_AuthUserDataInfoMap.find(static_cast<std::uint32_t>(accountId));
        if (account == g_AuthUserDataInfoMap.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][ATTEND_ERR] account id [%d] not exist in "
            //     L"AuthUserDataInfoMap!! JobType[%d], Reason[%d]",
            //     L"auth.cpp",
            //     0x50b,
            //     accountId,
            //     static_cast<int>(type),
            //     static_cast<int>(reason))
            // Log.Add(
            //     LOG_ERROR,
            //     "[auth] Cannot request AuthD user data for account %d: no local user-data record (requested type %d,
            //     reason %d).", accountId, static_cast<int>(type), static_cast<int>(reason))
            L2_LOG_ERROR(
                Log,
                "[auth] Cannot request AuthD user data for account %d: no local user-data record (requested type %d, reason "
                "%d).",
                accountId, static_cast<int>(type), static_cast<int>(reason)
            );
            return false;
        }

        AuthUserDataInfo& info = account->second;
        if (runtime::raw_atomic::LoadI32Relaxed(&info.nRWType) != 0 ||
            runtime::raw_atomic::LoadI32Relaxed(&info.nRWReason) != 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][ATTEND_ERR] RW Job In Progress. "
            //     L"AccountId[%d], JobType[%d], Reason[%d]",
            //     L"auth.cpp",
            //     0x501,
            //     accountId,
            //     static_cast<int>(type),
            //     static_cast<int>(reason))
            // Log.Add(
            //     LOG_WARN,
            //     "[auth] User-data request for account %d was not started: another read/write operation is pending
            //     (requested type %d, reason %d).", accountId, static_cast<int>(type), static_cast<int>(reason))
            L2_LOG_WARN(
                Log,
                "[auth] User-data request for account %d was not started: another read/write operation is pending "
                "(requested type %d, reason %d).",
                accountId, static_cast<int>(type), static_cast<int>(reason)
            );
            return false;
        }

        if (type != NEW_AUTH_USER_DATA_FOR_SERVER::TYPE_DEFAULT) {
            runtime::raw_atomic::ExchangeI32(&info.nRWType, static_cast<std::int32_t>(type));
        }
        if (reason != NEW_AUTH_USER_DATA_FOR_SERVER::REASON_DEFAULT) {
            if (reason == NEW_AUTH_USER_DATA_FOR_SERVER::REASON_FOR_ATTENDANCE &&
                type == NEW_AUTH_USER_DATA_FOR_SERVER::TYPE_READ) {
                runtime::raw_atomic::ExchangeI32(&info.nAttendanceInfoLoaded, 0);
            }
            runtime::raw_atomic::ExchangeI32(&info.nRWReason, static_cast<std::int32_t>(reason));
        }
    }

    CSPointer<CISocket> socket(m_spAuthSocket);
    if (socket) {
        socket->Send("cd", 0x18, accountId);
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%S][%d][ATTEND_ERR] auth socket is invalid.",
    //     "auth.cpp",
    //     0x51b)
    // Log.Add(
    //     LOG_ERROR,
    //     "[auth] Cannot request user data for account %d: no AuthD connection after recording the read/write request (type
    //     %d, reason %d), pending state is retained.", accountId, static_cast<int>(type), static_cast<int>(reason))
    L2_LOG_ERROR(
        Log,
        "[auth] Cannot request user data for account %d: no AuthD connection after recording the read/write request (type "
        "%d, reason %d); pending state is retained.",
        accountId, static_cast<int>(type), static_cast<int>(reason)
    );
    return false;
}

void CAuthD::AuthSetUserData(int accountId, unsigned short dataLength, const unsigned char* data) {
    L2_GUARD_FUNCTION();
    if (g_nUseReconnectAuth != 0 && !m_bSendAuth) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d][ATTEND_ERR] auth socket is broken.",
        //     "auth.cpp",
        //     0x54b)
        // Log.Add(
        //     LOG_WARN,
        //     "[auth] Cannot apply the user-data reply for account %d: authentication sending is disabled. This operation is
        //     skipped.", accountId)
        L2_LOG_WARN(
            Log,
            "[auth] Cannot apply the user-data reply for account %d: authentication sending is disabled; this operation is "
            "skipped.",
            accountId
        );
        return;
    }

    NEW_AUTH_USER_DATA::ATTENDANCE_TYPE attendanceType = NEW_AUTH_USER_DATA::AT_FIRST_ATTENDANCE;
    unsigned char attendanceDays = 0;
    std::int32_t lastAttendanceCheckTime = 0;
    int pcCafeAttendanceFlag = 0;
    int vipAttendanceFlag = 0;
    if (dataLength == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][ATTEND_ERR] First Logged In Account, Or "
        //     L"user_etc Data Not Exist Account. AccountId[%d]",
        //     L"auth.cpp",
        //     0x561,
        //     accountId)
        // Log.Add(
        //     LOG_NORMAL,
        //     "[auth] Account %d has no stored AuthD user data. Initializing attendance defaults.",
        //     accountId)
        L2_LOG_INFO(Log, "[auth] Account %d has no stored AuthD user data; initializing attendance defaults.", accountId);
    } else {
        ::Disassemble(
            data + 100, "ccddd", &attendanceType, &attendanceDays, &lastAttendanceCheckTime, &pcCafeAttendanceFlag,
            &vipAttendanceFlag
        );
    }
    AddAuthUserDataAttendanceInfo(
        accountId, attendanceType, attendanceDays, lastAttendanceCheckTime, pcCafeAttendanceFlag, vipAttendanceFlag,
        NEW_AUTH_USER_DATA::AJ_NONE
    );
}

void CAuthD::AddAuthUserDataCharacterInfo(
    int accountId, int totalCharacter, int deleteCharacter, std::int32_t* timeToDelete
) {
    L2_GUARD_FUNCTION();
    AuthUserDataInfo newInfo{};
    newInfo.nTotalCharacter = static_cast<std::uint8_t>(totalCharacter);
    if (totalCharacter > 7) {
        newInfo.nTotalCharacter = 7;
    }
    newInfo.nAuthServerID = static_cast<std::uint8_t>(g_nAuthServerID);
    newInfo.nDeleteCharacter = static_cast<std::uint8_t>(deleteCharacter);
    if (deleteCharacter > 7) {
        newInfo.nDeleteCharacter = 7;
    }
    newInfo.eAttendanceType = NEW_AUTH_USER_DATA::AT_NORMAL;
    if (deleteCharacter > 0) {
        std::memcpy(newInfo.tTimeToDelete, timeToDelete, static_cast<std::size_t>(deleteCharacter) * sizeof(std::int32_t));
    }

    {
        L2_WRITE_GUARD(AuthUserDataInfoLock);
        const auto account = g_AuthUserDataInfoMap.find(static_cast<std::uint32_t>(accountId));
        AuthUserDataInfo& stored = g_AuthUserDataInfoMap[static_cast<std::uint32_t>(accountId)];
        if (account == g_AuthUserDataInfoMap.end()) {
            std::memcpy(&stored, &newInfo, sizeof(stored));
        } else {
            stored.nAuthServerID = newInfo.nAuthServerID;
            stored.nTotalCharacter = newInfo.nTotalCharacter;
            stored.nDeleteCharacter = newInfo.nDeleteCharacter;
            if (stored.nDeleteCharacter != 0) {
                std::memcpy(
                    stored.tTimeToDelete, newInfo.tTimeToDelete,
                    static_cast<std::size_t>(stored.nDeleteCharacter) * sizeof(std::int32_t)
                );
            }
        }
    }
}

void CAuthD::AddAuthUserDataAttendanceInfo(
    int accountId,
    NEW_AUTH_USER_DATA::ATTENDANCE_TYPE attendanceType,
    unsigned char attendanceDays,
    std::int32_t lastAttendanceCheckTime,
    int pcCafeAttendanceFlag,
    int vipAttendanceFlag,
    NEW_AUTH_USER_DATA::ATTENDANCE_JOB job
) {
    L2_GUARD_FUNCTION();
    AuthUserDataInfo newInfo{};
    newInfo.eAttendanceType = attendanceType;
    newInfo.nAttendanceDays = attendanceDays;
    newInfo.tLastAttendanceCheckTime = lastAttendanceCheckTime;
    newInfo.nPCCafeAttendanceFlag = pcCafeAttendanceFlag;
    newInfo.nVIPAttendanceFlag = vipAttendanceFlag;
    newInfo.eJob = job;

    {
        L2_WRITE_GUARD(AuthUserDataInfoLock);
        const auto account = g_AuthUserDataInfoMap.find(static_cast<std::uint32_t>(accountId));
        AuthUserDataInfo& stored = g_AuthUserDataInfoMap[static_cast<std::uint32_t>(accountId)];
        if (account == g_AuthUserDataInfoMap.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][ATTEND_ERR] account id [%d] not exist in "
            //     L"AuthUserDataInfoMap!! AttendanceType=%d / AttendaceDays=%d",
            //     L"auth.cpp",
            //     0x471,
            //     accountId,
            //     static_cast<int>(attendanceType),
            //     static_cast<int>(attendanceDays))
            // Log.Add(
            //     LOG_NORMAL,
            //     "[auth] Creating the missing local attendance record for account %d (attendance type %d, days %u).",
            //     accountId, static_cast<int>(attendanceType), static_cast<unsigned>(attendanceDays))
            L2_LOG_INFO(
                Log, "[auth] Creating the missing local attendance record for account %d (attendance type %d, days %u).",
                accountId, static_cast<int>(attendanceType), static_cast<unsigned>(attendanceDays)
            );
            std::memcpy(&stored, &newInfo, sizeof(stored));
            runtime::raw_atomic::ExchangeI32(&stored.nAttendanceInfoLoaded, 1);
        } else {
            stored.eAttendanceType = newInfo.eAttendanceType;
            stored.nAttendanceDays = newInfo.nAttendanceDays;
            stored.tLastAttendanceCheckTime = newInfo.tLastAttendanceCheckTime;
            stored.nPCCafeAttendanceFlag = newInfo.nPCCafeAttendanceFlag;
            stored.nVIPAttendanceFlag = newInfo.nVIPAttendanceFlag;
            stored.eJob = newInfo.eJob;
            runtime::raw_atomic::ExchangeI32(&stored.nAttendanceInfoLoaded, 1);
            if (runtime::raw_atomic::LoadI32Relaxed(&stored.nRWType) == NEW_AUTH_USER_DATA_FOR_SERVER::TYPE_READ &&
                runtime::raw_atomic::LoadI32Relaxed(&stored.nRWReason) ==
                    NEW_AUTH_USER_DATA_FOR_SERVER::REASON_FOR_ATTENDANCE) {
                runtime::raw_atomic::ExchangeI32(&stored.nRWType, NEW_AUTH_USER_DATA_FOR_SERVER::TYPE_DEFAULT);
                runtime::raw_atomic::ExchangeI32(&stored.nRWReason, NEW_AUTH_USER_DATA_FOR_SERVER::REASON_DEFAULT);
            }
        }
    }
}

bool CAuthD::GetAuthUserDataInfo(int accountId, AuthUserDataInfo& info) {
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(AuthUserDataInfoLock);
        const auto account = g_AuthUserDataInfoMap.find(static_cast<std::uint32_t>(accountId));
        if (account == g_AuthUserDataInfoMap.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][ATTEND_ERR] GetAuthUserDataInfo() Failed. "
            //     L"Reason : Can't Find AuthUserDataInfo.",
            //     L"auth.cpp",
            //     0x493,
            //     accountId)
            // Log.Add(
            //     LOG_WARN,
            //     "[auth] Cannot read attendance information for account %d: no local user-data record.",
            //     accountId)
            L2_LOG_WARN(
                Log, "[auth] Cannot read attendance information for account %d: no local user-data record.", accountId
            );
            return false;
        }
        if (runtime::raw_atomic::LoadI32Relaxed(&account->second.nAttendanceInfoLoaded) == 1) {
            std::memcpy(&info, &account->second, sizeof(info));
            return true;
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][ATTEND_ERR] GetAuthUserDataInfo() Failed. "
        //     L"Reason : AttendanceData Is Not Loaded From AuthDB.",
        //     L"auth.cpp",
        //     0x48b,
        //     accountId)
        // Log.Add(
        //     LOG_WARN,
        //     "[auth] Cannot read attendance information for account %d: AuthD attendance data has not finished loading.",
        //     accountId)
        L2_LOG_WARN(
            Log, "[auth] Cannot read attendance information for account %d: AuthD attendance data has not finished loading.",
            accountId
        );
        return false;
    }
}

void CAuthD::AuthWriteUserData(int accountId, unsigned short dataLength, const unsigned char* data) {
    L2_GUARD_FUNCTION();
    if (g_nUseReconnectAuth != 0 && !m_bSendAuth) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] auth socket is broken.",
        //     "auth.cpp",
        //     0x57f)
        // Log.Add(
        //     LOG_WARN,
        //     "[auth] Cannot write user data for account %d: authentication sending is disabled. This operation is
        //     skipped.", accountId)
        L2_LOG_WARN(
            Log,
            "[auth] Cannot write user data for account %d: authentication sending is disabled; this operation is skipped.",
            accountId
        );
        return;
    }

    std::array<char, 0x800> output{};
    int outputLength = 0;
    char currentAuthServerId = 0;
    int writeReason = NEW_AUTH_USER_DATA_FOR_SERVER::REASON_DEFAULT;
    unsigned int outputEntryCount = 0;

    {
        L2_READ_GUARD(AuthUserDataInfoLock);
        const auto currentAccount = g_AuthUserDataInfoMap.find(static_cast<std::uint32_t>(accountId));
        if (currentAccount != g_AuthUserDataInfoMap.end()) {
            const AuthUserDataInfo& info = currentAccount->second;
            writeReason = runtime::raw_atomic::LoadI32Relaxed(&info.nRWReason);
            outputLength = ::Assemble(
                output.data(), static_cast<int>(output.size()), "cc", static_cast<int>(info.nAuthServerID),
                static_cast<int>(info.nTotalCharacter)
            );
            currentAuthServerId = static_cast<char>(info.nAuthServerID);
            outputEntryCount = 1;
        }
    }

    const unsigned char* const originalData = data;
    const unsigned char* cursor = data;
    for (int index = 0; index < 50; ++index) {
        if (outputLength > 0x7ff || static_cast<std::ptrdiff_t>(dataLength) <= cursor - originalData) {
            break;
        }

        char authServerId = 0;
        unsigned char totalCharacter = 0;
        cursor = ::Disassemble(cursor, "cc", &authServerId, &totalCharacter);
        if (authServerId == 0) {
            break;
        }
        if (totalCharacter > 7) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Invalid Auth UserData : total_char[%d]",
            //     static_cast<int>(totalCharacter))
            // Log.Add(
            //     LOG_ERROR,
            //     "[auth] Account %d user-data entry %d reports %u characters (maximum 7). Stopping the entry scan and
            //     continuing write preparation.", accountId, index, static_cast<unsigned>(totalCharacter))
            L2_LOG_ERROR(
                Log,
                "[auth] Account %d user-data entry %d reports %u characters (maximum 7); stopping the entry scan and "
                "continuing write preparation.",
                accountId, index, static_cast<unsigned>(totalCharacter)
            );
            break;
        }
        if (authServerId != currentAuthServerId) {
            outputLength += ::Assemble(
                output.data() + outputLength, static_cast<int>(output.size()) - outputLength, "cc",
                static_cast<int>(authServerId), static_cast<int>(totalCharacter)
            );
            ++outputEntryCount;
        }
    }

    while (outputEntryCount < 50) {
        outputLength += ::Assemble(output.data() + outputLength, static_cast<int>(output.size()) - outputLength, "cc", 0, 0);
        ++outputEntryCount;
    }

    if (writeReason == NEW_AUTH_USER_DATA_FOR_SERVER::REASON_FOR_CHARACTER_COUNT) {
        std::array<unsigned char, 0x40> emptyUserData{};
        const unsigned char* const userData = dataLength == 0 ? emptyUserData.data() : originalData + 0x64;
        outputLength += ::Assemble(
            output.data() + outputLength, static_cast<int>(output.size()) - outputLength, "b",
            static_cast<int>(emptyUserData.size()), userData
        );
    } else {
        AuthUserDataInfo info{};
        if (!GetAuthUserDataInfo(accountId, info) || info.eJob == NEW_AUTH_USER_DATA::AJ_NONE) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][ATTEND_ERR] AuthUserData Not Exist!! "
            //     L"[account_id : %d] WriteType:[%d]",
            //     L"auth.cpp",
            //     0x63e,
            //     accountId,
            //     writeReason)
            // Log.Add(
            //     LOG_WARN,
            //     "[auth] Attendance write for account %d has unavailable data or no attendance job (write reason %d)
            //     retaining incoming user data, or zeros for an empty payload.", accountId, writeReason)
            L2_LOG_WARN(
                Log,
                "[auth] Attendance write for account %d has unavailable data or no attendance job (write reason %d); "
                "retaining incoming user data, or zeros for an empty payload.",
                accountId, writeReason
            );

            std::array<unsigned char, 0x40> emptyUserData{};
            const unsigned char* const userData = dataLength == 0 ? emptyUserData.data() : originalData + 0x64;
            outputLength += ::Assemble(
                output.data() + outputLength, static_cast<int>(output.size()) - outputLength, "b",
                static_cast<int>(emptyUserData.size()), userData
            );
        } else {
            std::array<unsigned char, 0x12> reserved{};
            outputLength += ::Assemble(
                output.data() + outputLength, static_cast<int>(output.size()) - outputLength, "ccdddb",
                static_cast<int>(static_cast<char>(info.eAttendanceType)), static_cast<int>(info.nAttendanceDays),
                info.tLastAttendanceCheckTime, info.nPCCafeAttendanceFlag, info.nVIPAttendanceFlag,
                static_cast<int>(reserved.size()), reserved.data()
            );
            outputLength += ::Assemble(
                output.data() + outputLength, static_cast<int>(output.size()) - outputLength, "b", 0x20, originalData + 0x84
            );

            CSPointer<User> user = g_UserAccountDBIDMap.FindObject(accountId);
            if (user) {
                CLogFormatter formatter;
                formatter.WriteLog4(
                    0x1fce, user.get(), nullptr, u"AuthWrite", nullptr, nullptr,
                    static_cast<std::int64_t>(info.eAttendanceType), static_cast<std::int64_t>(info.nAttendanceDays),
                    info.nPCCafeAttendanceFlag, info.nVIPAttendanceFlag, info.tLastAttendanceCheckTime,
                    user->GetSocket()->GetDormantType(), g_nWorldId, 0, 0, 0, 0
                );
            }
        }
    }

    CSPointer<CISocket> socket(m_spAuthSocket);
    if (socket) {
        socket->Send("cdhb", 9, accountId, outputLength, outputLength, output.data());
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR] AS_WRITE_USERDATA AccountId[%d] TotalNum[%d]",
        //     accountId,
        //     outputLength)
        // Log.Add(
        //     LOG_NORMAL,
        //     "[auth] AuthD user-data write requested for account %d (%d payload bytes).",
        //     accountId, outputLength)
        L2_LOG_INFO(
            Log, "[auth] AuthD user-data write requested for account %d (%d payload bytes).", accountId, outputLength
        );
    }
}

void CAuthD::AuthFreeToPayUphill(int accountId, int previousPayStat, int currentPayStat) {
    L2_GUARD_FUNCTION();
    if (g_nUseReconnectAuth != 0 && !m_bSendAuth) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] auth socket is broken.",
        //     "auth.cpp",
        //     0x725)
        // Log.Add(
        //     LOG_WARN,
        //     "[auth] Cannot notify the payment-status change for account %d: authentication sending is disabled. This
        //     operation is skipped.", accountId)
        L2_LOG_WARN(
            Log,
            "[auth] Cannot notify the payment-status change for account %d: authentication sending is disabled; this "
            "operation is skipped.",
            accountId
        );
        return;
    }

    CSPointer<CISocket> socket(m_spAuthSocket);
    if (socket) {
        const std::int32_t now = static_cast<std::int32_t>(std::time(nullptr));
        socket->Send(
            "cdcddd", 0x23, accountId, static_cast<int>(static_cast<char>(g_nAuthServerID)), previousPayStat, currentPayStat,
            now
        );
    }
}

void CAuthD::AuthDeleteChar(int accountId, int characterId, int characterLevel, int characterClass, int characterGender) {
    L2_GUARD_FUNCTION();
    if (g_nUseReconnectAuth != 0 && !m_bSendAuth) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] auth socket is broken.",
        //     "auth.cpp",
        //     0x73c)
        // Log.Add(
        //     LOG_WARN,
        //     "[auth] Cannot notify character deletion for account %d: authentication sending is disabled. This operation is
        //     skipped.", accountId)
        L2_LOG_WARN(
            Log,
            "[auth] Cannot notify character deletion for account %d: authentication sending is disabled; this operation is "
            "skipped.",
            accountId
        );
        return;
    }

    CSPointer<CISocket> socket(m_spAuthSocket);
    if (socket) {
        socket->Send(
            "cdcdddd", 0x25, accountId, static_cast<int>(static_cast<char>(g_nAuthServerID)), characterId, characterLevel,
            characterClass, characterGender
        );
    }
}

void CAuthD::AuthCheckCharMaxLevel(int accountId, int characterId, int characterLevel) {
    L2_GUARD_FUNCTION();
    if (g_nUseReconnectAuth != 0 && !m_bSendAuth) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] auth socket is broken.",
        //     "auth.cpp",
        //     0x752)
        // Log.Add(
        //     LOG_WARN,
        //     "[auth] Cannot report the maximum character level for account %d: authentication sending is disabled. This
        //     operation is skipped.", accountId)
        L2_LOG_WARN(
            Log,
            "[auth] Cannot report the maximum character level for account %d: authentication sending is disabled; this "
            "operation is skipped.",
            accountId
        );
        return;
    }

    CSPointer<CISocket> socket(m_spAuthSocket);
    if (socket) {
        socket->Send("cddd", 0x24, accountId, characterId, characterLevel);
    }
}

void CAuthD::AuthChargeStart(int accountId) {
    L2_GUARD_FUNCTION();
    if (g_nUseReconnectAuth != 0 && !m_bSendAuth) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] auth socket is broken.",
        //     "auth.cpp",
        //     0x768)
        // Log.Add(
        //     LOG_WARN,
        //     "[auth] Cannot start charging for account %d: authentication sending is disabled. This operation is skipped.",
        //     accountId)
        L2_LOG_WARN(
            Log,
            "[auth] Cannot start charging for account %d: authentication sending is disabled; this operation is skipped.",
            accountId
        );
        return;
    }

    CSPointer<CISocket> socket(m_spAuthSocket);
    if (socket) {
        socket->Send("cd", 0x27, accountId);
    }
}

void CAuthD::AuthChargeStop(int accountId) {
    L2_GUARD_FUNCTION();
    if (g_nUseReconnectAuth != 0 && !m_bSendAuth) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] auth socket is broken.",
        //     "auth.cpp",
        //     0x77e)
        // Log.Add(
        //     LOG_WARN,
        //     "[auth] Cannot stop charging for account %d: authentication sending is disabled. This operation is skipped.",
        //     accountId)
        L2_LOG_WARN(
            Log,
            "[auth] Cannot stop charging for account %d: authentication sending is disabled; this operation is skipped.",
            accountId
        );
        return;
    }

    CSPointer<CISocket> socket(m_spAuthSocket);
    if (socket) {
        socket->Send("cd", 0x28, accountId);
    }
}

void CAuthD::AuthRequestActivateMark(int accountId, std::int64_t value1, std::int64_t value2, int dormantReason) {
    L2_GUARD_FUNCTION();
    CSPointer<CISocket> socket(m_spAuthSocket);
    if (socket) {
        socket->Send("cddQ", 0x29, accountId, value1, value2);
    }
    db.RequestResetDormantHistory(accountId, dormantReason);
}

void CAuthD::AuthRequest2ndPassword(char requestType, int accountId, int result, char16_t* password, char16_t* newPassword) {
    L2_GUARD_FUNCTION();

    std::array<char, 16> ansiPassword{};
    std::array<char, 16> ansiNewPassword{};
    UnicodeToAnsi(
        password, static_cast<int>(std::char_traits<char16_t>::length(password)), ansiPassword.data(),
        static_cast<int>(ansiPassword.size())
    );
    UnicodeToAnsi(
        newPassword, static_cast<int>(std::char_traits<char16_t>::length(newPassword)), ansiNewPassword.data(),
        static_cast<int>(ansiNewPassword.size())
    );

    if (g_nUseReconnectAuth != 0 && !m_bSendAuth) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] auth socket is broken.",
        //     "auth.cpp",
        //     0x6e8)
        // Log.Add(
        //     LOG_WARN,
        //     "[auth] Cannot request secondary authentication for account %d: authentication sending is disabled. This
        //     operation is skipped.", accountId)
        L2_LOG_WARN(
            Log,
            "[auth] Cannot request secondary authentication for account %d: authentication sending is disabled; this "
            "operation is skipped.",
            accountId
        );
        return;
    }
    if (!g_bCheckSecondaryAuth) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] g_bCheckSecondaryAuth is false.",
        //     "auth.cpp",
        //     0x6ee)
        // Log.Add(
        //     LOG_WARN,
        //     "[auth] Ignoring secondary-authentication request type %d for account %d: secondary authentication is
        //     disabled.", static_cast<int>(requestType), accountId)
        L2_LOG_WARN(
            Log,
            "[auth] Ignoring secondary-authentication request type %d for account %d: secondary authentication is disabled.",
            static_cast<int>(requestType), accountId
        );
        return;
    }

    CSPointer<CISocket> socket(m_spAuthSocket);
    if (!socket) {
        return;
    }

    switch (requestType) {
        case 4:
            socket->Send("cccd", 0x1d, 4, 1, accountId);
            break;
        case 0:
        case 1:
        case 3:
            socket->Send(
                "cccdbd", 0x1d, static_cast<int>(requestType), 1, accountId, static_cast<int>(ansiPassword.size()),
                ansiPassword.data(), result
            );
            break;
        case 2:
            socket->Send(
                "cccdbbd", 0x1d, 2, 1, accountId, static_cast<int>(ansiPassword.size()), ansiPassword.data(),
                static_cast<int>(ansiNewPassword.size()), ansiNewPassword.data(), result
            );
            break;
        default:
            break;
    }
}

bool CAuthD::AuthRequestAppToken(unsigned int accountId) {
    L2_GUARD_FUNCTION();
    if (g_nUseReconnectAuth != 0 && !m_bSendAuth) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] auth socket is broken.",
        //     L"auth.cpp",
        //     0x7a7)
        // Log.Add(
        //     LOG_WARN,
        //     "[auth] Cannot request an application token for account %u: authentication sending is disabled. This operation
        //     is skipped.", static_cast<unsigned>(accountId))
        L2_LOG_WARN(
            Log,
            "[auth] Cannot request an application token for account %u: authentication sending is disabled; this operation "
            "is skipped.",
            static_cast<unsigned>(accountId)
        );
        return false;
    }

    CSPointer<CISocket> socket(m_spAuthSocket);
    if (!socket) {
        return false;
    }
    socket->Send("cd", 0x2a, accountId);
    return true;
}

bool CAuthD::AuthRequestAuthNToken(unsigned int accountId) {
    L2_GUARD_FUNCTION();
    if (g_bUseNPLogin) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][STEAM] Can't Request SSO AUTHNTOKEN "
        //     L"On GP System.",
        //     L"auth.cpp",
        //     0x7c2)
        // Log.Add(
        //     LOG_WARN,
        //     "[auth] Cannot request a Steam SSO token for account %u while NP login is enabled. Request rejected.",
        //     static_cast<unsigned>(accountId))
        L2_LOG_WARN(
            Log, "[auth] Cannot request a Steam SSO token for account %u while NP login is enabled; request rejected.",
            static_cast<unsigned>(accountId)
        );
        return false;
    }
    if (g_nUseReconnectAuth != 0 && !m_bSendAuth) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] auth socket is broken.",
        //     L"auth.cpp",
        //     0x7c9)
        // Log.Add(
        //     LOG_WARN,
        //     "[auth] Cannot request a Steam SSO token for account %u: authentication sending is disabled. This operation is
        //     skipped.", static_cast<unsigned>(accountId))
        L2_LOG_WARN(
            Log,
            "[auth] Cannot request a Steam SSO token for account %u: authentication sending is disabled; this operation is "
            "skipped.",
            static_cast<unsigned>(accountId)
        );
        return false;
    }

    const std::string applicationId = Utf16ToUtf8(runtime::StringView(SSO_FOR_STEAM_INFO::LINEAGE2_APP_ID));
    std::array<char, 40> applicationIdBuffer{};
    std::memcpy(applicationIdBuffer.data(), applicationId.c_str(), applicationId.size() + 1);
    const int applicationIdSize = static_cast<int>(applicationId.size() + 1);

    CSPointer<CISocket> socket(m_spAuthSocket);
    if (!socket) {
        return false;
    }
    socket->Send("cdb", 0x2a, accountId, applicationIdSize, applicationIdBuffer.data());
    return true;
}
