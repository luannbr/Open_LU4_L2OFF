#include "IOServer.h"
#include "Shared/src/ScopedLockGuards.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"

#include "Platform/CurrentL2ServerProfile.h"
#include "Platform/Odbc.h"
#include "BuildN.h"
#include "AccountDB.h"
#include "ClientSession.h"
#include "Config.h"
#include "DBConn.h"
#include "GPProxyServerMgr.h"
#include "IOServerEx.h"
#include "IPSessionDB.h"
#include "Log.h"
#include "PAServerMgr.h"
#include "PBServerMgr.h"
#include "Protocol.h"
#include "SocketServerEx.h"
#include "Thread.h"
#include "TokenServerMgr.h"
#include "Util.h"
#include "WSMServerMgr.h"

#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <exception>
#include <new>

#if defined(_WIN32)
#  include <WinSock2.h>
#else
#  include <arpa/inet.h>
#endif

bool ServerPlayOk(CSocketServer*, const unsigned char*);
bool ServerPlayFail(CSocketServer*, const unsigned char*);
bool ServerPlayGame(CSocketServer*, const unsigned char*);
bool ServerPlayQuit(CSocketServer*, const unsigned char*);
bool ServerKickAccount(CSocketServer*, const unsigned char*);
bool ServerGetUserNum(CSocketServer*, const unsigned char*);
bool ServerBanUser(CSocketServer*, const unsigned char*);
bool ServerVersion(CSocketServer*, const unsigned char*);
bool ServerPing(CSocketServer*, const unsigned char*);
bool ServerWriteUserData(CSocketServer*, const unsigned char*);
bool ServerSetActive(CSocketServer*, const unsigned char*);
bool ServerPlayUserList(CSocketServer*, const unsigned char*);
bool ServerReqCreateChar(CSocketServer*, const unsigned char*);
bool ServerReqDelChar(CSocketServer*, const unsigned char*);
bool ServerCharList(CSocketServer*, const unsigned char*);
bool ServerReqMoveChar(CSocketServer*, const unsigned char*);
bool ServerSelect(CSocketServer*, const unsigned char*);
bool UserPlayServer(CSocketServer*, const unsigned char*);
bool UserLogout(CSocketServer*, const unsigned char*);
bool ServerStatus(CSocketServer*, const unsigned char*);
bool ServerUserNumWithID(CSocketServer*, const unsigned char*);
bool ServerList(CSocketServer*, const unsigned char*);
bool ServerPlayQuitCharge(CSocketServer*, const unsigned char*);
bool ServerGetUserInfo(CSocketServer*, const unsigned char*);
bool ServerGetUserData(CSocketServer*, const unsigned char*);
bool ServerGetValueInfo(CSocketServer*, const unsigned char*);
bool ServerValueAbandon(CSocketServer*, const unsigned char*);
bool ServerWaitUser(CSocketServer*, const unsigned char*);
bool ServerSideLoginUserList(CSocketServer*, const unsigned char*);
bool Server2ndPasswordRequest(CSocketServer*, const unsigned char*);
bool ServerSetRemainTime(CSocketServer*, const unsigned char*);
bool ServerUseTimeExpCapsule(CSocketServer*, const unsigned char*);
bool ServerWsidRequest(CSocketServer*, const unsigned char*);
bool TencentCafeCheck(CSocketServer*, const unsigned char*);
bool ServerWsidClose(CSocketServer*, const unsigned char*);
bool FreeToPayUphill(CSocketServer*, const unsigned char*);
bool CheckMaxLevelAck(CSocketServer*, const unsigned char*);
bool DeleteCharacter(CSocketServer*, const unsigned char*);
bool GetCharacterMaxLevel(CSocketServer*, const unsigned char*);
bool ChargeStart(CSocketServer*, const unsigned char*);
bool ChargeStop(CSocketServer*, const unsigned char*);
bool ServerActivateMark(CSocketServer*, const unsigned char*);
bool ServerAuthnTokenRequest(CSocketServer*, const unsigned char*);
bool ChargeStartEx(CSocketServer*, const unsigned char*);
short MapQuitGameReasonToLogoutReason(short reason, int gameId);

namespace {

// L2Authd.exe .data 0x00519040. All 44 entries and their order are read
// directly from the selected 2019 image.
CSocketServer::PacketFunction ServerPacket[44] = {
    ServerPlayOk,
    ServerPlayFail,
    ServerPlayGame,
    ServerPlayQuit,
    ServerKickAccount,
    ServerGetUserNum,
    ServerBanUser,
    ServerVersion,
    ServerPing,
    ServerWriteUserData,
    ServerSetActive,
    ServerPlayUserList,
    ServerReqCreateChar,
    ServerReqDelChar,
    ServerCharList,
    ServerSelect,
    UserPlayServer,
    UserLogout,
    ServerStatus,
    ServerUserNumWithID,
    ServerList,
    ServerReqMoveChar,
    ServerPlayQuitCharge,
    ServerGetUserInfo,
    ServerGetUserData,
    ServerGetValueInfo,
    ServerValueAbandon,
    ServerWaitUser,
    ServerSideLoginUserList,
    Server2ndPasswordRequest,
    ServerSetRemainTime,
    ServerUseTimeExpCapsule,
    ServerWsidRequest,
    TencentCafeCheck,
    ServerWsidClose,
    FreeToPayUphill,
    CheckMaxLevelAck,
    DeleteCharacter,
    GetCharacterMaxLevel,
    ChargeStart,
    ChargeStop,
    ServerActivateMark,
    ServerAuthnTokenRequest,
    ChargeStartEx,
};

} // namespace

// L2Authd.exe 0x0042EBF0, Src/csocketserver.cpp:1857-1868.
bool CServerKickList::AddKickUser(
    int uid,
    const char* account) {
    L2_GUARD(mLock);
    const bool inserted =
        KickList.insert({uid, account}).second;
    return inserted;
}

// L2Authd.exe 0x0042ECA0, Src/csocketserver.cpp:1873-1903.
bool CServerKickList::PopKickUser(
    CSocketServer* socket) {
    L2_GUARD(mLock);
    for (const auto& [uid, storedAccount] : KickList) {
        char account[15]{};
        std::strncpy(account, storedAccount.c_str(), 14);
        if (uid > 0) {
            socket->Send(
                "cdcs",
                1,
                uid,
                14,
                account);
        }
    }
    KickList.clear();
    return false;
}

// L2Authd.exe 0x00433680, compiled from Src/serverlist.h.
CServerKickList::CServerKickList()
    : mLock(eCriticalSection, 0) {}

// L2Authd.exe 0x004337A0, compiled from Src/serverlist.h.
CServerKickList::~CServerKickList() = default;

// L2Authd.exe 0x0042EDB0, Src/csocketserver.cpp:1651-1658.
CSocketServer::CSocketServer(std::uint32_t socket)
    : CIOSocket(socket) {
    m_socket = socket;
    addr.s_addr = 0;
    host = nullptr;
    mode = SM_READ_LEN;
    packetTable = ServerPacket;
    opFlag = 0;
}

// L2Authd.exe 0x0042EE60, Src/csocketserver.cpp:1662-1663.
CSocketServer::~CSocketServer() = default;

// L2Authd.exe 0x0042EEC0, Src/csocketserver.cpp:1667-1669.
CSocketServer* CSocketServer::Allocate(
    std::uint32_t socket) {
    void* const storage =
        MemoryObject::operator new(sizeof(CSocketServer));
    return storage == nullptr
        ? nullptr
        : ::new (storage) CSocketServer(socket);
}

// L2Authd.exe 0x0042EF40, Src/csocketserver.cpp:1787-1799.
void CSocketServer::OnCreate() {
    AddRef();
    Send(
        "cddc",
        3,
        GetBuildNumber(),
        1,
        config.supportReconnect);
    OnRead();
}

// L2Authd.exe 0x0042EF90, Src/csocketserver.cpp:1705-1777.
void CSocketServer::OnRead() {
    int consumed = 0;
    const int available =
        static_cast<int>(m_pReadBuf->m_size);
    auto* const buffer =
        reinterpret_cast<unsigned char*>(
            m_pReadBuf->m_buffer);
    int errorNumber = 0;

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
                static_cast<int>(buffer[consumed]) + 1 +
                static_cast<int>(buffer[consumed + 1]) * 256 -
                config.PacketSizeType;
            if (packetLen < 1 || packetLen > 8192) {
                errorNumber = 1;
                // Noise: The terminal rejection below records the reason and socket context once before closing.
                // Log.AddLog(
                //     LOG_ERROR,
                //     "%d: bad packet size %d",
                //     m_socket,
                //     packetLen)
                break;
            }

            consumed += 2;
            mode = SM_READ_BODY;
            continue;
        }

        if (mode != SM_READ_BODY) {
            errorNumber = 3;
            break;
        }
        if (available < consumed + packetLen) {
            Read(static_cast<std::uint32_t>(
                available - consumed));
            return;
        }

        if ((config.usePacketLog & 4) != 0) {
            WritePacketLog(
                "World->Auth",
                m_hSocket,
                buffer + consumed,
                packetLen,
                0x400);
        }

        const unsigned int opcode = buffer[consumed];
        if (opcode >= 44) {
            // Noise: The terminal rejection below records the reason and socket context once before closing.
            // Log.AddLog(
            //     LOG_ERROR,
            //     "unknown protocol %d",
            //     opcode)
            errorNumber = 2;
            break;
        }

        CPacketServer* const packet =
            CPacketServer::Alloc();
        packet->m_pSocket = this;
        packet->m_pBuf = m_pReadBuf;
        packet->m_pFunc =
            reinterpret_cast<CPacketServer::ServerPacketFunc>(
                packetTable[opcode]);
        AddRef();
        m_pReadBuf->AddRef();
        runtime::raw_atomic::IncrementI32(
            &CPacketServer::g_nPendingPacket);
        packet->PostObject(
            consumed,
            g_hIOCompletionPort);

        consumed += packetLen;
        mode = SM_READ_LEN;
    }

    if (errorNumber == 0) {
        Read(0);
        return;
    }

    const std::time_t now = std::time(nullptr);
    // [[maybe_unused]] const std::tm current = *std::localtime(&now)
    [[maybe_unused]] const std::tm current = runtime::crt::LocalTimeOrZero(now);
    const std::uint32_t diagnosticPeerAddress = ntohl(addr.s_addr);
    // Log.AddLog(
    //     LOG_WARN,
    //     "component=network event=protocol_rejected peer=world server_id=%d socket=%u address=%u.%u.%u.%u reason=%s error_number=%d read_state=%d body_bytes=%d opcode=%d buffered_bytes=%d consumed_bytes=%d action=close_connection",
    //     static_cast<int>(serverid), m_socket,
    //     (diagnosticPeerAddress >> 24U) & 255U, (diagnosticPeerAddress >> 16U) & 255U,
    //     (diagnosticPeerAddress >> 8U) & 255U, diagnosticPeerAddress & 255U,
    //     errorNumber == 1 ? "invalid_packet_size" : errorNumber == 2 ? "invalid_opcode" : "invalid_read_state",
    //     errorNumber, static_cast<int>(mode), errorNumber == 3 ? 0 : packetLen,
    //     errorNumber == 2 ? static_cast<int>(buffer[consumed]) : -1, available, consumed)
    // Log.AddLog(
    //     LOG_WARN,
    //     "[world] Invalid packet from server %d on socket %u (%u.%u.%u.%u): %s. Error %d, read state %d, body %d bytes, opcode %d, buffered %d bytes, consumed %d. Closing the connection.",
    //     static_cast<int>(serverid), m_socket,
    //     (diagnosticPeerAddress >> 24U) & 255U, (diagnosticPeerAddress >> 16U) & 255U,
    //     (diagnosticPeerAddress >> 8U) & 255U, diagnosticPeerAddress & 255U,
    //     errorNumber == 1 ? "invalid packet size" : errorNumber == 2 ? "invalid opcode" : "invalid read state",
    //     errorNumber, static_cast<int>(mode), errorNumber == 3 ? 0 : packetLen,
    //     errorNumber == 2 ? static_cast<int>(buffer[consumed]) : -1, available, consumed)
    L2_LOG_WARN(Log,
        "[world] Invalid packet from server %d on socket %u (%u.%u.%u.%u): %s. Error %d, read state %d, body %d bytes, opcode %d, buffered %d bytes, consumed %d. Closing the connection.",
        static_cast<int>(serverid), m_socket,
        (diagnosticPeerAddress >> 24U) & 255U, (diagnosticPeerAddress >> 16U) & 255U,
        (diagnosticPeerAddress >> 8U) & 255U, diagnosticPeerAddress & 255U,
        errorNumber == 1 ? "invalid packet size" : errorNumber == 2 ? "invalid opcode" : "invalid read state",
        errorNumber, static_cast<int>(mode), errorNumber == 3 ? 0 : packetLen,
        errorNumber == 2 ? static_cast<int>(buffer[consumed]) : -1, available, consumed);
    errLog.AddLog(
        LOG_NORMAL,
        "main server connection close. invalid status and protocol %s, errorNum :%d\r\n",
        IP(),
        errorNumber);
    CloseSocket();
}

// L2Authd.exe 0x0042F270, Src/csocketserver.cpp:1671-1701.
void CSocketServer::OnClose() {
    mode = SM_CLOSE;

    const std::time_t now = std::time(nullptr);
    // const std::tm current = *std::localtime(&now)
    const std::tm current = runtime::crt::LocalTimeOrZero(now);
    const std::uint32_t diagnosticPeerAddress = ntohl(addr.s_addr);
    const bool diagnosticShuttingDown = runtime::raw_atomic::LoadBool(&g_bTerminating);
    // Log.AddLog(
    //     LOG_ERROR,
    //     "*close connection from %s, %x(%x)",
    //     IP(),
    //     m_socket,
    //     runtime::PointerLow32(this))
    // Log.AddLog(
    //     diagnosticShuttingDown ? LOG_NORMAL : LOG_WARN,
    //     "component=network event=socket_closed peer=world server_id=%d socket=%u address=%u.%u.%u.%u shutdown_requested=%s cause=unspecified session_cleanup=%s world_status=offline_pending",
    //     static_cast<int>(serverid), m_socket,
    //     (diagnosticPeerAddress >> 24U) & 255U, (diagnosticPeerAddress >> 16U) & 255U,
    //     (diagnosticPeerAddress >> 8U) & 255U, diagnosticPeerAddress & 255U,
    //     diagnosticShuttingDown ? "true" : "false", serverid > 0 ? "pending" : "skipped_unregistered_server")
    // Log.AddLog(
    //     diagnosticShuttingDown ? LOG_NORMAL : LOG_WARN,
    //     "[world] Server %d disconnected on socket %u (%u.%u.%u.%u). Shutdown requested: %s. Session cleanup: %s. Cause not provided. Marking the world offline next.",
    //     static_cast<int>(serverid), m_socket,
    //     (diagnosticPeerAddress >> 24U) & 255U, (diagnosticPeerAddress >> 16U) & 255U,
    //     (diagnosticPeerAddress >> 8U) & 255U, diagnosticPeerAddress & 255U,
    //     diagnosticShuttingDown ? "true" : "false", serverid > 0 ? "pending" : "skipped because the server is unregistered")
    L2_LOG(Log, diagnosticShuttingDown ? LOG_NORMAL : LOG_WARN,
        // Noise: The generic absence-of-cause phrase adds no diagnostic context.
        // "[world] Server %d disconnected on socket %u (%u.%u.%u.%u). Shutdown requested: %s. Session cleanup: %s. Cause not provided. Marking the world offline next.",
        "[world] Server %d disconnected on socket %u (%u.%u.%u.%u). Shutdown requested: %s; session cleanup: %s. Marking the world offline next.",
        static_cast<int>(serverid), m_socket,
        (diagnosticPeerAddress >> 24U) & 255U, (diagnosticPeerAddress >> 16U) & 255U,
        (diagnosticPeerAddress >> 8U) & 255U, diagnosticPeerAddress & 255U,
        diagnosticShuttingDown ? "true" : "false", serverid > 0 ? "pending" : "skipped because the server is unregistered");
    errLog.AddLog(
        LOG_NORMAL,
        "%d-%d-%d %d:%d:%d,main server connection close from %s, 0x%x\r\n",
        current.tm_year + 1900,
        current.tm_mon + 1,
        current.tm_mday,
        current.tm_hour,
        current.tm_min,
        current.tm_sec,
        IP(),
        m_socket);

    if (serverid > 0) {
        accountdb.RemoveAll(serverid);
    }
    SetServerSocket(addr, m_socket);
    server.RemoveSocket(addr);

    CDBConn sql(g_linDB);
    short serverId = serverid;
    sql.BindParam(&serverId, 1);
    sql.Execute("{call ap_SetServerStatus (?, 0)}");
    SetServerStatus(serverid, 0);
}

// L2Authd.exe 0x0042F450, Src/csocketserver.cpp:1803-1825.
void CSocketServer::SetAddress(
    in_addr address) {
    addr = address;

    CDBConn sql(g_linDB);
    short serverId = serverid;
    sql.BindParam(&serverId, 1);
    sql.Execute("{call ap_SetServerStatus (?, 1)}");
    SetServerStatus(serverid, 1);
    bSetActiveServer = true;
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "component=world event=connection_registered server_id=%d socket=%u local_status=1 database_status=unconfirmed",
    //     static_cast<int>(serverid), m_socket)
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "[world] Registered server %d on socket %u. Local status is active. The database status update is unconfirmed.",
    //     static_cast<int>(serverid), m_socket)
    L2_LOG_INFO(Log,
        "[world] Registered server %d on socket %u; local status is active. The database status update is unconfirmed.",
        static_cast<int>(serverid), m_socket);
}

// L2Authd.exe 0x0042F540, Src/csocketserver.cpp:1829-1853.
void CSocketServer::Send(
    const char* format,
    ...) {
    if (mode == SM_CLOSE) {
        return;
    }

    CIOBuffer* const buffer = CIOBuffer::Alloc();
    std::va_list arguments;
    va_start(arguments, format);
    int packetSize = Assemble(
        buffer->m_buffer + 2,
        8190,
        format,
        arguments);
    va_end(arguments);

    if (packetSize != 0 &&
        authd::compatibility::SuppressWorldReply(
            static_cast<std::uint8_t>(
                buffer->m_buffer[2]))) {
        buffer->Release();
        return;
    }

    if (packetSize == 0) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "%d: assemble too large packet. format %s",
        //     m_socket,
        //     format)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=network event=packet_assembly_failed peer=world server_id=%d socket=%u capacity_bytes=8190 assembled_bytes=0 packet_size_type=%d action=continue_legacy_write",
        //     static_cast<int>(serverid), m_socket, config.PacketSizeType)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[world] Could not assemble a packet for server %d on socket %u within 8190 bytes (PacketSizeType %d). The existing send path still submits the buffer.",
        //     static_cast<int>(serverid), m_socket, config.PacketSizeType)
        L2_LOG_ERROR(Log,
            "[world] Could not assemble a packet for server %d on socket %u within 8190 bytes (PacketSizeType %d). The existing send path still submits the buffer.",
            static_cast<int>(serverid), m_socket, config.PacketSizeType);
    } else {
        packetSize =
            packetSize - 1 + config.PacketSizeType;
        buffer->m_buffer[0] =
            static_cast<char>(packetSize);
        buffer->m_buffer[1] =
            static_cast<char>(packetSize >> 8);
        if ((config.usePacketLog & 4) != 0) {
            WritePacketLog(
                "Auth->World",
                m_hSocket,
                reinterpret_cast<const unsigned char*>(
                    buffer->m_buffer + 2),
                packetSize - 2,
                0x400);
        }
    }

    buffer->m_size = static_cast<std::uint32_t>(
        packetSize + 3 - config.PacketSizeType);
    Write(buffer);
}

// L2Authd.exe 0x0042F650, Src/csocketserver.cpp:1781-1783.
const char* CSocketServer::IP() {
    return ::inet_ntoa(addr);
}

// L2Authd.exe 0x00434F40, compiled from Src/ioserver.h.
in_addr CSocketServer::getaddr() {
    return addr;
}

// L2Authd.exe 0x0042F670, Src/csocketserver.cpp:1634-1646.
int Send2ClientSession(
    unsigned int sessionId,
    const char* format,
    ...) {
    CClientSession* const session =
        serverEx.FindSession(sessionId);
    if (session == nullptr) {
        return 0;
    }

    std::va_list arguments;
    va_start(arguments, format);
    session->Send(format, arguments);
    va_end(arguments);
    session->ReleaseRef();
    return 1;
}

// L2Authd.exe 0x0042F6C0, Src/csocketserver.cpp:55-93.
bool ServerPlayOk(
    CSocketServer* socket,
    const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    const int oneTimeKey = GetIntFromPacket(&packet);
    char previousServerId = 0;
    char account[15];
    std::memset(account, 0, 14);

    const unsigned int sessionId =
        accountdb.FindSessionId(
            uid,
            socket->serverid,
            true,
            &previousServerId,
            account);
    account[14] = '\0';

    if (sessionId != UINT32_MAX &&
        uid != 0 &&
        previousServerId != 0) {
        accountdb.KickAccount(
            uid,
            14,
            true,
            true);
        if (g_ServerList.ExistId(previousServerId)) {
            const WorldServer world =
                g_ServerList.GetAt(previousServerId);
            SendSocket(
                world.inner_addr,
                "cdcs",
                1,
                uid,
                14,
                account);
        }
        socket->Send(
            "cdcs",
            1,
            uid,
            14,
            account);
        return false;
    }

    if (sessionId != UINT32_MAX && uid != 0) {
        Send2ClientSession(
            sessionId,
            "cddc",
            7,
            oneTimeKey,
            uid,
            socket->serverid);
    }
    return false;
}

// L2Authd.exe 0x0042F8D0, Src/csocketserver.cpp:97-132.
bool ServerPlayFail(
    CSocketServer* socket,
    const unsigned char* packet) {
    const char failCode = GetCharFromPacket(&packet);
    const int uid = GetIntFromPacket(&packet);
    const unsigned int sessionId =
        accountdb.FindSessionId(uid, false);
    int payStat = 0;
    in_addr loginIp{};
    std::int32_t loginTime = 0;
    char account[15];
    const bool found = accountdb.GetAccountInfoForIPStop(
        uid,
        account,
        &payStat,
        &loginIp,
        &loginTime);

    if (found && CIPSessionDB::IsIPCharging(payStat)) {
        ipsessionDB.StopIPCharge(
            static_cast<unsigned int>(uid),
            loginIp.s_addr,
            payStat,
            0,
            loginTime,
            socket->serverid,
            account);
    }
    if (config.AutoKickAccount && failCode == 14) {
        socket->Send(
            "cdcs",
            1,
            uid,
            failCode,
            account);
    }
    if (sessionId != UINT32_MAX && uid != 0) {
        Send2ClientSession(
            sessionId,
            "cc",
            6,
            failCode);
    }
    return false;
}

// L2Authd.exe 0x0042F9F0, Src/csocketserver.cpp:137-153.
bool ServerPlayGame(
    CSocketServer* socket,
    const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    if (!accountdb.recordGamePlayTime(
            uid,
            socket->serverid) &&
        !accountdb.KickAccount(
            uid,
            10,
            true,
            true)) {
        socket->Send(
            "cdcc",
            1,
            uid,
            10,
            0);
    }
    return false;
}

// L2Authd.exe 0x0042FA70, Src/csocketserver.cpp:158-203.
bool ServerPlayQuit(
    CSocketServer* socket,
    const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    const short reason = GetShortFromPacket(&packet);
    [[maybe_unused]] const int useTime =
        GetIntFromPacket(&packet);
    const int characterId = GetIntFromPacket(&packet);
    const int level = GetIntFromPacket(&packet);

    if (config.useDualCharging &&
        characterId != 0 &&
        level != 0 &&
        config.useLobbyCharging) {
        const WorldServer world =
            g_ServerList.GetAt(socket->serverid);
        if (world.serverKind == 0x101 ||
            world.serverKind == 0x104) {
            // Log.AddLog(
            //     LOG_ERROR,
            //     "QuitGame character information with BEGINNER SERVERID(uid:%d, serverid:%d)",
            //     uid,
            //     socket->serverid)
            // Log.AddLog(
            //     LOG_WARN,
            //     "component=world event=character_level_update_skipped operation=play_quit server_id=%d uid=%d character_id=%d level=%d server_kind=%d reason=beginner_world action=continue_logout",
            //     static_cast<int>(socket->serverid), uid, characterId, level, world.serverKind)
            // Log.AddLog(
            //     LOG_WARN,
            //     "[world] Logout from server %d for account %d: skipping character %d level %d because this is a beginner world (kind %d). Logout continues.",
            //     static_cast<int>(socket->serverid), uid, characterId, level, world.serverKind)
            L2_LOG_WARN(Log,
                "[world] Logout from server %d for account %d: skipping character %d level %d because this is a beginner world (kind %d). Logout continues.",
                static_cast<int>(socket->serverid), uid, characterId, level, world.serverKind);
        } else {
            const auto currentTime =
                static_cast<std::int32_t>(
                    std::time(nullptr));
            if (!accountdb.UpdateCharacterLevel(
                    uid,
                    static_cast<unsigned char>(
                        socket->serverid),
                    characterId,
                    level,
                    currentTime)) {
                // Log.AddLog(
                //     LOG_ERROR,
                //     "updating character level in AS_QUIT_GAME(uid:%d)",
                //     uid)
                // Log.AddLog(
                //     LOG_ERROR,
                //     "component=world event=character_level_update_failed operation=play_quit server_id=%d uid=%d character_id=%d level=%d reason=database_or_procedure_rejection action=continue_logout",
                //     static_cast<int>(socket->serverid), uid, characterId, level)
                // Log.AddLog(
                //     LOG_ERROR,
                //     "[world] Logout from server %d for account %d: database update of character %d level %d failed or was rejected. Logout continues.",
                //     static_cast<int>(socket->serverid), uid, characterId, level)
                L2_LOG_ERROR(Log,
                    "[world] Logout from server %d for account %d: database update of character %d level %d failed or was rejected. Logout continues.",
                    static_cast<int>(socket->serverid), uid, characterId, level);
            }
        }
    }

    const short logoutReason =
        MapQuitGameReasonToLogoutReason(
            reason,
            config.gameId);
    accountdb.LogoutProcess(uid, logoutReason);
    return false;
}

// L2Authd.exe 0x0042FC30, Src/csocketserver.cpp:207-219.
bool ServerKickAccount(
    CSocketServer*,
    const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    [[maybe_unused]] const short reason =
        GetShortFromPacket(&packet);
    accountdb.LogoutProcess(uid, 0);
    return false;
}

// L2Authd.exe 0x0042FD00, Src/csocketserver.cpp:223-238.
bool ServerGetUserNum(
    CSocketServer* socket,
    const unsigned char* packet) {
    const int userNumbers = GetIntFromPacket(&packet);
    SetServerUserNum(
        socket->getaddr(),
        userNumbers,
        socket->serverid);
    g_UserCount.UpdateUserNum(
        socket->serverid,
        static_cast<short>(userNumbers),
        static_cast<short>(
            static_cast<unsigned int>(userNumbers) >> 16));
    return false;
}

// L2Authd.exe 0x0042FD70, Src/csocketserver.cpp:242-325.
bool ServerBanUser(
    CSocketServer* socket,
    const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    const short reason = GetShortFromPacket(&packet);
    char timeTag[64]{};
    char gameAccount[15]{};
    int restrictionNumber = 0;
    GetTimeTag(timeTag, 64);

    char result = 0;
    if (!config.useBanAccount) {
        blockLog.AddLog(
            LOG_NORMAL,
            "%s, %14d, %14s, %03d, 0, -, -, -, -, -, -, - \r\n",
            timeTag,
            uid,
            "",
            reason);
        // Log.AddLog(
        //     LOG_WARN,
        //     "useBanAccount is not set, discards AS_BAN_USER, uid:%d, reason:%d",
        //     uid,
        //     reason)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=world event=ban_rejected server_id=%d uid=%d reason_code=%d reason=useBanAccount_disabled response_code=20 action=reply_failure",
        //     static_cast<int>(socket->serverid), uid, static_cast<int>(reason))
        // Log.AddLog(
        //     LOG_WARN,
        //     "[world] Server %d requested a ban for account %d (reason %d), but useBanAccount is disabled. Replying with failure code 20.",
        //     static_cast<int>(socket->serverid), uid, static_cast<int>(reason))
        L2_LOG_WARN(Log,
            "[world] Server %d requested a ban for account %d (reason %d), but useBanAccount is disabled. Replying with failure code 20.",
            static_cast<int>(socket->serverid), uid, static_cast<int>(reason));
        result = 20;
    } else {
        result = static_cast<char>(accountdb.BanAccount(
            uid,
            reason,
            gameAccount,
            15,
            &restrictionNumber));
        if (result != 0) {
            L2_LOG_WARN(Log,
                "[world] Ban request from server %d for account %d (reason %d) returned failure code %d. Logout and charge-suspension steps are skipped; replying with this code.",
                static_cast<int>(socket->serverid), uid, static_cast<int>(reason), static_cast<int>(result));
            blockLog.AddLog(
                LOG_NORMAL,
                "%s, %14d, %14s, %03d, 1, %d, -, -, -, -, -, - \r\n",
                timeTag,
                uid,
                "",
                reason,
                result);
        } else {
            accountdb.LogoutProcess(uid, 0);
            if (config.useAccountBanGameKick) {
                socket->Send(
                    "cdcs",
                    1,
                    uid,
                    reason,
                    gameAccount);
            }

            if (!config.usePBServer) {
                blockLog.AddLog(
                    LOG_NORMAL,
                    "%s, %14d, %14s, %03d, 1, %d, 0, -, -, -, -, - \r\n",
                    timeTag,
                    uid,
                    gameAccount,
                    reason,
                    result);
            } else if (!config.useSuspendCharge) {
                blockLog.AddLog(
                    LOG_NORMAL,
                    "%s, %14d, %14s, %03d, 1, %d, 1, 0, -, -, -, - \r\n",
                    timeTag,
                    uid,
                    gameAccount,
                    reason,
                    result);
                // Log.AddLog(
                //     LOG_WARN,
                //     "useSuspendCharge is not set, discards charge suspending(with VM), uid:%d, reason:%d",
                //     uid,
                //     reason)
                // Log.AddLog(
                //     LOG_WARN,
                //     "component=world event=charge_suspend_skipped operation=ban server_id=%d uid=%d reason_code=%d reason=useSuspendCharge_disabled account_ban=applied response_code=20 action=reply_failure",
                //     static_cast<int>(socket->serverid), uid, static_cast<int>(reason))
                // Log.AddLog(
                //     LOG_WARN,
                //     "[world] Server %d banned account %d (reason %d), but charge suspension was skipped because useSuspendCharge is disabled. Replying with failure code 20.",
                //     static_cast<int>(socket->serverid), uid, static_cast<int>(reason))
                L2_LOG_WARN(Log,
                    "[world] Server %d banned account %d (reason %d), but charge suspension was skipped because useSuspendCharge is disabled. Replying with failure code 20.",
                    static_cast<int>(socket->serverid), uid, static_cast<int>(reason));
                result = 20;
            } else {
                result = PBServerMgr::Inst()->RequestChargeSuspend(
                    reason,
                    result,
                    uid,
                    gameAccount,
                    restrictionNumber);
                if (result != 0) {
                    L2_LOG_WARN(Log,
                        "[world] Charge suspension returned failure code %d after the ban operation reported success for account %d (server %d, reason %d, restriction %d). The ban is not rolled back; replying with the suspension failure code.",
                        static_cast<int>(result), uid, static_cast<int>(socket->serverid),
                        static_cast<int>(reason), restrictionNumber);
                    blockLog.AddLog(
                        LOG_NORMAL,
                        "%s, %14d, %14s, %03d, 1, %d, 1, 1, 0, -, -, - \r\n",
                        timeTag,
                        uid,
                        gameAccount,
                        reason,
                        result);
                }
            }
        }
    }

    socket->Send("cdc", 24, uid, result);
    return false;
}

// L2Authd.exe 0x0042FFF0, Src/csocketserver.cpp:329-337.
bool ServerVersion(
    CSocketServer*,
    const unsigned char* packet) {
    [[maybe_unused]] const int version =
        GetIntFromPacket(&packet);
    return false;
}

// L2Authd.exe 0x00430010, Src/csocketserver.cpp:341-348.
bool ServerPing(
    CSocketServer* socket,
    const unsigned char* packet) {
    const int ping = GetIntFromPacket(&packet);
    socket->Send("cd", 4, ping);
    return false;
}

// L2Authd.exe 0x00430040, Src/csocketserver.cpp:352-386.
bool ServerWriteUserData(
    CSocketServer* socket,
    const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    short length = GetShortFromPacket(&packet);
    LoginUser* user = nullptr;
    char result = 0;

    if (accountdb.FindAccount(uid, &user)) {
        CDBConn connection(g_linDB);
        connection.BindParam(user->account, 14, 1);
        connection.BindParam(&length, 1);

        authd::platform::OdbcLen indicator = length;
        if (length == 0) {
            length = 1;
            indicator = -1;
        }

        authd::platform::SQLBindParameter(
            connection.m_stmt,
            static_cast<authd::platform::OdbcUSmallInt>(
                connection.m_paramNum),
            1,
            -2,
            -3,
            static_cast<authd::platform::OdbcUInteger>(length),
            0,
            const_cast<unsigned char*>(packet),
            length,
            &indicator);
        ++connection.m_paramNum;

        if (connection.Execute(
                "{call dbo.ap_SUserData (?, ?, ?)}") &&
            accountdb.WriteUserData(
                uid,
                length,
                packet)) {
            result = 1;
        }
    }

    socket->Send("cdc", 14, uid, result);
    if (user != nullptr) {
        user->ReleaseRef();
    }
    return false;
}

// L2Authd.exe 0x00430250, Src/csocketserver.cpp:391-409.
bool ServerSetActive(
    CSocketServer* socket,
    const unsigned char*) {
    socket->mKickList.PopKickUser(socket);
    CDBConn connection(g_linDB);
    short serverId = socket->serverid;
    connection.BindParam(&serverId, 1);
    connection.Execute(
        "{call ap_SetServerStatus (?, 1)}");
    SetServerStatus(socket->serverid, 1);
    socket->bSetActiveServer = true;
    // Log.AddLog(
    //     LOG_DEBUG,
    //     "component=world event=active_request_applied server_id=%d socket=%u local_status=1 database_status=unconfirmed",
    //     static_cast<int>(socket->serverid), socket->m_socket)
    // Log.AddLog(
    //     LOG_DEBUG,
    //     "[world] Applied the active-state request for server %d on socket %u. The database status update is unconfirmed.",
    //     static_cast<int>(socket->serverid), socket->m_socket)
    L2_LOG_DEBUG(Log,
        "[world] Applied the active-state request for server %d on socket %u. The database status update is unconfirmed.",
        static_cast<int>(socket->serverid), socket->m_socket);
    return false;
}

// L2Authd.exe 0x00430330, Src/csocketserver.cpp:419-426.
bool ServerPlayUserList(
    CSocketServer*,
    const unsigned char*) {
    return false;
}

// L2Authd.exe 0x00430340, Src/csocketserver.cpp:434-448.
bool ServerReqCreateChar(
    CSocketServer* socket,
    const unsigned char* packet) {
    const int result = GetIntFromPacket(&packet);
    const int uid = GetIntFromPacket(&packet);
    const int characterId = GetIntFromPacket(&packet);
    const short dataLength = GetShortFromPacket(&packet);
    accountdb.CreateChar(
        uid,
        characterId,
        socket->serverid);
    const unsigned int sessionId =
        accountdb.FindSessionId(uid, false);
    Send2ClientSession(
        sessionId,
        "ccddchb",
        1,
        result,
        uid,
        characterId,
        socket->serverid,
        dataLength,
        dataLength,
        packet);
    return false;
}

// L2Authd.exe 0x00430410, Src/csocketserver.cpp:452-465.
bool ServerReqDelChar(
    CSocketServer* socket,
    const unsigned char* packet) {
    const char result = GetCharFromPacket(&packet);
    const int uid = GetIntFromPacket(&packet);
    const int characterId = GetIntFromPacket(&packet);
    accountdb.DelChar(
        uid,
        characterId,
        socket->serverid);
    const unsigned int sessionId =
        accountdb.FindSessionId(uid, false);
    Send2ClientSession(
        sessionId,
        "ccddc",
        2,
        result,
        uid,
        characterId,
        socket->serverid);
    return false;
}

// L2Authd.exe 0x004304B0, Src/csocketserver.cpp:471-488.
bool ServerCharList(
    CSocketServer* socket,
    const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    const char characterCount =
        GetCharFromPacket(&packet);
    const short dataLength =
        GetShortFromPacket(&packet);
    if (characterCount > 0) {
        const unsigned int sessionId =
            accountdb.FindSessionId(uid, false);
        Send2ClientSession(
            sessionId,
            "cdccchb",
            0,
            uid,
            11,
            characterCount,
            socket->serverid,
            dataLength,
            dataLength,
            packet);
    }
    return false;
}

// L2Authd.exe 0x00430560, Src/csocketserver.cpp:492-513.
bool ServerReqMoveChar(
    CSocketServer*,
    const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    const int oldCharacterId =
        GetIntFromPacket(&packet);
    const int oldWorldId = GetIntFromPacket(&packet);
    const int newCharacterId =
        GetIntFromPacket(&packet);
    const int newWorldId = GetIntFromPacket(&packet);
    [[maybe_unused]] const bool result =
        accountdb.UpdateCharLocation(
            uid,
            oldCharacterId,
            static_cast<char>(oldWorldId),
            newCharacterId,
            newWorldId);
    return false;
}

// L2Authd.exe 0x00430600, Src/csocketserver.cpp:517-556.
bool ServerSelect(
    CSocketServer*,
    const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    const int oneTimeKey = GetIntFromPacket(&packet);
    const char serverId = GetCharFromPacket(&packet);
    const unsigned int sessionId =
        accountdb.FindSessionId(uid, true);

    if (!g_ServerList.ExistId(serverId)) {
        Send2ClientSession(
            sessionId,
            "cc",
            6,
            6);
        return false;
    }

    const WorldServer world = GetWorldServer(serverId);
    if (world.serverid < 1 ||
        (static_cast<unsigned int>(world.serverKind) &
         UINT32_C(0x80000000)) != 0) {
        Send2ClientSession(
            sessionId,
            "cc",
            6,
            6);
    } else {
        Send2ClientSession(
            sessionId,
            "cddd",
            3,
            oneTimeKey,
            world.outer_addr.s_addr,
            world.port);
    }
    return false;
}

// L2Authd.exe 0x00430770, Src/csocketserver.cpp:560-564.
bool UserPlayServer(
    CSocketServer*,
    const unsigned char*) {
    return false;
}

// L2Authd.exe 0x00430810, Src/csocketserver.cpp:569-584.
bool UserLogout(
    CSocketServer*,
    const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    [[maybe_unused]] const char reason =
        GetCharFromPacket(&packet);
    accountdb.LogoutProcess(uid, 0);
    return false;
}

// L2Authd.exe 0x004308E0, Src/csocketserver.cpp:587-632.
bool ServerStatus(
    CSocketServer*,
    const unsigned char* packet) {
    const char serverId = GetCharFromPacket(&packet);
    char status = GetCharFromPacket(&packet);
    if (!g_ServerList.ExistId(serverId)) {
        return false;
    }

    const WorldServer world = GetWorldServer(serverId);
    if (world.serverid < 1) {
        return false;
    }
    if ((static_cast<unsigned int>(world.serverKind) &
         UINT32_C(0x80000000)) != 0 &&
        status == 0) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "%d serverid is manage servers (not allow protocol)",
        //     world.serverid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=world event=status_update_rejected server_id=%d requested_status=%d server_kind=%d reason=managed_world action=ignore_request",
        //     static_cast<int>(world.serverid), static_cast<int>(status), world.serverKind)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[world] Server %d requested status %d, but its world kind %d is managed separately. Ignoring the request.",
        //     static_cast<int>(world.serverid), static_cast<int>(status), world.serverKind)
        L2_LOG_WARN(Log,
            "[world] Server %d requested status %d, but its world kind %d is managed separately. Ignoring the request.",
            static_cast<int>(world.serverid), static_cast<int>(status), world.serverKind);
        return false;
    }

    if (status == 0) {
        accountdb.RemoveAll(world.serverid);
    }
    CDBConn connection(g_linDB);
    short boundServerId = world.serverid;
    connection.BindParam(&boundServerId, 1);
    connection.BindParam(&status, 1, 1);
    connection.Execute(
        "{call ap_SetServerStatus (?, ?)}");
    SetServerStatus(world.serverid, status);
    return false;
}

// L2Authd.exe 0x00430A90, Src/csocketserver.cpp:636-667.
bool ServerUserNumWithID(
    CSocketServer*,
    const unsigned char* packet) {
    const int userNumbers = GetIntFromPacket(&packet);
    const char serverId = GetCharFromPacket(&packet);
    if (g_ServerList.ExistId(serverId)) {
        const WorldServer world =
            GetWorldServer(serverId);
        if (world.serverid > 0) {
            SetServerUserNum(
                world.outer_addr,
                userNumbers,
                world.serverid);
        }
    }
    return false;
}

// L2Authd.exe 0x00430B70, Src/csocketserver.cpp:671-693.
bool ServerList(
    CSocketServer* socket,
    const unsigned char*) {
    const int length = g_ServerList.ServerListLen[0];
    char temporaryBuffer[6137];
    std::memcpy(
        temporaryBuffer,
        g_ServerList.ServerListPacketFrame[0],
        static_cast<std::size_t>(length));
    if (static_cast<unsigned int>(length) >=
        sizeof(temporaryBuffer)) {
        std::terminate();
    }
    temporaryBuffer[length] = '\0';
    socket->Send(
        "cb",
        9,
        length,
        temporaryBuffer);
    return false;
}

// L2Authd.exe 0x00430CC0, Src/csocketserver.cpp:698-749.
bool ServerPlayQuitCharge(
    CSocketServer* socket,
    const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    const short reason = GetShortFromPacket(&packet);
    const int useTime = GetIntFromPacket(&packet);
    int reconnectKey = 0;
    LoginUser* user = nullptr;
    LoginUser loginUser;

    if (accountdb.FindAccount(uid, &user)) {
        loginUser = *user;
        user->ReleaseRef();
    } else {
        socket->Send(
            "cdcd",
            15,
            uid,
            20,
            reconnectKey);
        // Log.AddLog(
        //     LOG_ERROR,
        //     "QuitGameCharge error(can't Find Account), uid %d, reason %d, usetime %d\r\n",
        //     uid,
        //     reason,
        //     useTime)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=world event=quit_charge_account_missing server_id=%d uid=%d reason_code=%d reported_use_time=%d response_code=20 action=reply_failure_then_continue_quit_charge",
        //     static_cast<int>(socket->serverid), uid, static_cast<int>(reason), useTime)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[world] Quit-charge request from server %d: account %d is missing (reason %d, reported use time %d). Replying with failure code 20, then continuing quit-charge processing.",
        //     static_cast<int>(socket->serverid), uid, static_cast<int>(reason), useTime)
        L2_LOG_WARN(Log,
            "[world] Quit-charge request from server %d: account %d is missing (reason %d, reported use time %d). Replying with failure code 20, then continuing quit-charge processing.",
            static_cast<int>(socket->serverid), uid, static_cast<int>(reason), useTime);
    }

    if (accountdb.QuitGameCharge(
            uid,
            useTime,
            socket->serverid,
            &reconnectKey)) {
        socket->Send(
            "cdcd",
            15,
            uid,
            0,
            reconnectKey);

        if (config.useDualLog) {
            const std::time_t currentTime =
                std::time(nullptr);
            // const std::tm current = *std::localtime(&currentTime)
            const std::tm current = runtime::crt::LocalTimeOrZero(currentTime);
            const std::time_t loginTime =
                static_cast<std::time_t>(
                    loginUser.logintime);
            // const std::tm login = *std::localtime(&loginTime)
            const std::tm login = runtime::crt::LocalTimeOrZero(loginTime);
            dualLog.AddLog(
                LOG_NORMAL,
                "%d-%02d-%02d %02d:%02d:%02d,%d-%02d-%02d %02d:%02d:%02d,SERVEROUT,%s,,%d,%d,%s,%s,%d,%d,%d,%d\r\n",
                current.tm_year + 1900,
                current.tm_mon + 1,
                current.tm_mday,
                current.tm_hour,
                current.tm_min,
                current.tm_sec,
                login.tm_year + 1900,
                login.tm_mon + 1,
                login.tm_mday,
                login.tm_hour,
                login.tm_min,
                login.tm_sec,
                loginUser.account,
                loginUser.serverid,
                loginUser.stat,
                ::inet_ntoa(loginUser.loginIp),
                loginUser.loginCafe,
                uid,
                loginUser.stat != 0,
                loginUser.gender,
                loginUser.age);
        }
    } else {
        socket->Send(
            "cdcd",
            15,
            uid,
            20,
            reconnectKey);
        // Log.AddLog(
        //     LOG_ERROR,
        //     "QuitGameCharge error, uid %d, reason %d, usetime %d\r\n",
        //     uid,
        //     reason,
        //     useTime)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=world event=quit_charge_failed server_id=%d uid=%d reason_code=%d reported_use_time=%d response_code=20 action=reply_failure",
        //     static_cast<int>(socket->serverid), uid, static_cast<int>(reason), useTime)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[world] Quit-charge failed for server %d, account %d (reason %d, reported use time %d). Replying with failure code 20.",
        //     static_cast<int>(socket->serverid), uid, static_cast<int>(reason), useTime)
        L2_LOG_ERROR(Log,
            "[world] Quit-charge failed for server %d, account %d (reason %d, reported use time %d). Replying with failure code 20.",
            static_cast<int>(socket->serverid), uid, static_cast<int>(reason), useTime);
    }
    return false;
}

// L2Authd.exe 0x00430F80, Src/csocketserver.cpp:754-776.
bool ServerGetUserInfo(
    CSocketServer* socket,
    const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    char age = 0;
    char gender = 0;
    bool result = false;
    if (config.Country == 0) {
        result = accountdb.GetUserInfo(
            uid,
            age,
            gender);
    }
    if (!result) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "AS_GET_USER_INFO error, user not found, uid %d\r\n",
        //     uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=world event=user_info_unavailable server_id=%d uid=%d country=%d reason=%s response_result=0 action=reply_unavailable",
        //     static_cast<int>(socket->serverid), uid, config.Country,
        //     config.Country == 0 ? "lookup_failed" : "lookup_disabled_for_country")
        // Log.AddLog(
        //     LOG_WARN,
        //     "[world] User information is unavailable for server %d, account %d (country %d): %s. Replying with result 0.",
        //     static_cast<int>(socket->serverid), uid, config.Country,
        //     config.Country == 0 ? "lookup failed" : "lookup is disabled for this country")
        L2_LOG_WARN(Log,
            "[world] User information is unavailable for server %d, account %d (country %d): %s. Replying with result 0.",
            static_cast<int>(socket->serverid), uid, config.Country,
            config.Country == 0 ? "lookup failed" : "lookup is disabled for this country");
    }
    socket->Send(
        "cdccc",
        10,
        uid,
        result,
        age,
        gender);
    return false;
}

// L2Authd.exe 0x00431010, Src/csocketserver.cpp:782-795.
bool ServerGetUserData(
    CSocketServer* socket,
    const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    LoginUser* user = nullptr;
    if (accountdb.FindAccount(uid, &user)) {
        socket->Send(
            "cdchb",
            13,
            uid,
            1,
            user->user_data_len,
            user->user_data_len,
            user->user_data);
        user->ReleaseRef();
    } else {
        socket->Send("cdc", 13, uid, 0);
    }
    return false;
}

// L2Authd.exe 0x004310B0, Src/csocketserver.cpp:799-826.
bool ServerGetValueInfo(
    CSocketServer* socket,
    const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    LoginUser* user = nullptr;
    if (accountdb.FindAccount(uid, &user)) {
        if (user->chargeServerId == 2) {
            const char result =
                PBServerMgr::Inst()->RequestGetValueInfo(
                    uid,
                    user,
                    nullptr,
                    25,
                    user->originChargedServerId);
            if (result != 0) {
                socket->Send(
                    "cdc",
                    16,
                    uid,
                    result);
            }
        } else {
            socket->Send(
                "cdcc",
                16,
                uid,
                0,
                user->chargeServerId);
        }
        user->ReleaseRef();
    } else {
        socket->Send("cdc", 16, uid, 4);
    }
    return false;
}

// L2Authd.exe 0x00431190, Src/csocketserver.cpp:830-863.
bool ServerValueAbandon(
    CSocketServer* socket,
    const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    unsigned char valueId[16]{};
    GetBinFromPacket(&packet, 16, valueId);

    if (!config.usePBServer) {
        socket->Send("cdc", 17, uid, 20);
        return false;
    }

    LoginUser* user = nullptr;
    if (accountdb.FindAccount(uid, &user)) {
        const char result =
            PBServerMgr::Inst()->RequestValueAbandon(
                uid,
                valueId,
                user);
        if (result != 0) {
            socket->Send(
                "cdc",
                17,
                uid,
                result);
        }
        user->ReleaseRef();
    } else {
        socket->Send("cdc", 17, uid, 4);
    }
    return false;
}

// L2Authd.exe 0x00431290, Src/csocketserver.cpp:867-880.
bool ServerWaitUser(
    CSocketServer* socket,
    const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    socket->Send(
        "cdc",
        19,
        uid,
        accountdb.SetWaitUser(
            uid,
            socket->serverid)
            ? 0
            : 10);
    return false;
}

// L2Authd.exe 0x00431300, Src/csocketserver.cpp:884-947.
bool ServerSideLoginUserList(
    CSocketServer* socket,
    const unsigned char* packet) {
    const int loginUserCount =
        GetIntFromPacket(&packet);
    int requestedUserCount = 0;
    for (; requestedUserCount < loginUserCount;
         ++requestedUserCount) {
        const int uid = GetIntFromPacket(&packet);
        char account[15]{};
        GetBinFromPacket(
            &packet,
            14,
            reinterpret_cast<unsigned char*>(account));
        const int status = GetIntFromPacket(&packet);
        const in_addr clientIp =
            GetAddrFromPacket(&packet);
        PreLoginUser preLogin{};
        CAccount* const loadedAccount =
            &preLogin.account;

        if (loadedAccount->LoadAccount(account) != 0) {
            socket->Send("cdc", 21, uid, 1);
            continue;
        }

        const bool freeStatus =
            (config.FreeServer == 3 && status == 2000) ||
            (config.FreeServer == 2 && status == 1002);
        if (!config.usePBServer &&
            status > 1000 &&
            loadedAccount->pay_stat != status &&
            !freeStatus) {
            socket->Send("cdc", 21, uid, 44);
            continue;
        }

        if (config.useExternalFatigueSystem &&
            config.externalFatigueCompanyCode == 4) {
            PAServerMgr::Inst()->ExternalUserInfo(
                loadedAccount->uid,
                preLogin,
                socket->serverid,
                status,
                clientIp);
        } else {
            PAServerMgr::Inst()->GetUserInfo(
                28,
                loadedAccount->uid,
                preLogin,
                socket->serverid,
                status,
                clientIp);
        }
    }

    socket->Send("cd", 22, requestedUserCount);
    return false;
}

// L2Authd.exe 0x00431690, Src/csocketserver.cpp:951-955.
bool Server2ndPasswordRequest(
    CSocketServer* socket,
    const unsigned char* packet) {
    CGpProxyServerMgr::Inst()->SecondPasswordRequest(
        socket->serverid,
        packet);
    return false;
}

// L2Authd.exe 0x004316B0, Src/csocketserver.cpp:959-996.
bool ServerSetRemainTime(
    CSocketServer*,
    const unsigned char* packet) {
    [[maybe_unused]] const int uid =
        GetIntFromPacket(&packet);
    [[maybe_unused]] const unsigned char applyZone =
        static_cast<unsigned char>(
            GetCharFromPacket(&packet));
    [[maybe_unused]] const int remainTime =
        GetIntFromPacket(&packet);
    return false;
}

// L2Authd.exe 0x004316F0, Src/csocketserver.cpp:1000-1037.
bool ServerUseTimeExpCapsule(
    CSocketServer*,
    const unsigned char* packet) {
    [[maybe_unused]] const int uid =
        GetIntFromPacket(&packet);
    [[maybe_unused]] const unsigned char applyZone =
        static_cast<unsigned char>(
            GetCharFromPacket(&packet));
    [[maybe_unused]] const unsigned char capsuleId =
        static_cast<unsigned char>(
            GetCharFromPacket(&packet));
    [[maybe_unused]] const int totalRemainTime =
        GetIntFromPacket(&packet);
    return false;
}

// L2Authd.exe 0x00431740, Src/csocketserver.cpp:1041-1136.
bool ServerWsidRequest(
    CSocketServer* socket,
    const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    const in_addr clientIp =
        GetAddrFromPacket(&packet);
    WebToken uniqueId{};
    GetBinFromPacket(
        &packet,
        16,
        reinterpret_cast<unsigned char*>(&uniqueId));
    const int timeout = GetIntFromPacket(&packet);
    WebSessionId webSessionId{};

    const auto sendFailure =
        [&](int error) {
            socket->Send(
                "cdcbbd",
                27,
                uid,
                error,
                16,
                &webSessionId,
                16,
                &uniqueId,
                timeout);
        };

    LoginUser* user = nullptr;
    if (!accountdb.FindAccount(uid, &user)) {
        sendFailure(MapWSMToAuthReason(1));
        return false;
    }

    if (std::memcmp(
            &user->loginIp,
            &clientIp,
            sizeof(clientIp)) != 0) {
        const std::uint32_t diagnosticRequestedAddress = ntohl(clientIp.s_addr);
        const std::uint32_t diagnosticLoginAddress = ntohl(user->loginIp.s_addr);
        // Log.AddLog(
        //     LOG_ERROR,
        //     "ServerWsidRequest IP mismatch, uid(%d), clientIp(%d) clientIp2(%d), timeout(%d)",
        //     uid,
        //     clientIp.s_addr,
        //     user->loginIp.s_addr,
        //     timeout)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=world event=web_session_ip_mismatch server_id=%d uid=%d requested_ip=%u.%u.%u.%u login_ip=%u.%u.%u.%u requested_timeout=%d action=continue_request",
        //     static_cast<int>(socket->serverid), uid,
        //     (diagnosticRequestedAddress >> 24U) & 255U, (diagnosticRequestedAddress >> 16U) & 255U,
        //     (diagnosticRequestedAddress >> 8U) & 255U, diagnosticRequestedAddress & 255U,
        //     (diagnosticLoginAddress >> 24U) & 255U, (diagnosticLoginAddress >> 16U) & 255U,
        //     (diagnosticLoginAddress >> 8U) & 255U, diagnosticLoginAddress & 255U, timeout)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[world] Web-session IP mismatch for server %d, account %d: requested %u.%u.%u.%u, login %u.%u.%u.%u (requested timeout %d). Processing continues.",
        //     static_cast<int>(socket->serverid), uid,
        //     (diagnosticRequestedAddress >> 24U) & 255U, (diagnosticRequestedAddress >> 16U) & 255U,
        //     (diagnosticRequestedAddress >> 8U) & 255U, diagnosticRequestedAddress & 255U,
        //     (diagnosticLoginAddress >> 24U) & 255U, (diagnosticLoginAddress >> 16U) & 255U,
        //     (diagnosticLoginAddress >> 8U) & 255U, diagnosticLoginAddress & 255U, timeout)
        L2_LOG_WARN(Log,
            "[world] Web-session IP mismatch for server %d, account %d: requested %u.%u.%u.%u, login %u.%u.%u.%u (requested timeout %d). Processing continues.",
            static_cast<int>(socket->serverid), uid,
            (diagnosticRequestedAddress >> 24U) & 255U, (diagnosticRequestedAddress >> 16U) & 255U,
            (diagnosticRequestedAddress >> 8U) & 255U, diagnosticRequestedAddress & 255U,
            (diagnosticLoginAddress >> 24U) & 255U, (diagnosticLoginAddress >> 16U) & 255U,
            (diagnosticLoginAddress >> 8U) & 255U, diagnosticLoginAddress & 255U, timeout);
    }
    user->nWsmLoginIp = clientIp;

    char result = 20;
    if (config.useWSM) {
        result = WSMServerMgr::Inst()->LoginAccount(
            ORIGIN_GAMESERVER_REQUEST,
            socket,
            user,
            reinterpret_cast<const unsigned char*>(
                &uniqueId),
            timeout);
        if (result != 0) {
            sendFailure(MapWSMToAuthReason(999));
        }
    } else if (config.useLightTokenServer) {
        result = TokenServerMgr::Inst()->RequestGetToken(
            socket,
            user,
            reinterpret_cast<const unsigned char*>(
                &uniqueId));
        if (result != 0) {
            sendFailure(MapWSMToAuthReason(999));
        }
    } else if (config.useGpProxy) {
        result = CGpProxyServerMgr::Inst()->LoginAccountIdPwd(
            ORIGIN_GAMESERVER_REQUEST,
            socket,
            *user,
            uniqueId,
            timeout);
        if (result != 0) {
            sendFailure(20);
        }
    } else {
        sendFailure(MapWSMToAuthReason(999));
    }

    user->ReleaseRef();
    return false;
}

// L2Authd.exe 0x00431A00, Src/csocketserver.cpp:1140-1160.
bool TencentCafeCheck(
    CSocketServer* socket,
    const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    [[maybe_unused]] const in_addr clientIp =
        GetAddrFromPacket(&packet);
    const char result = 1;
    const int tencentVip = 0;
    const int tencentTeamRank = 0;
    const int tencentCreditRank = 0;
    const int tencentLevel = 0;
    const int tencentCampaign = 0;
    socket->Send(
        "cdcddddd",
        28,
        uid,
        result,
        tencentVip,
        tencentTeamRank,
        tencentCreditRank,
        tencentLevel,
        tencentCampaign);
    return false;
}

// L2Authd.exe 0x00431A90, Src/csocketserver.cpp:1164-1222.
bool ServerWsidClose(
    CSocketServer* socket,
    const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    WebSessionId webSessionId{};
    GetBinFromPacket(
        &packet,
        16,
        reinterpret_cast<unsigned char*>(
            &webSessionId));

    LoginUser* user = nullptr;
    if (!accountdb.FindAccount(uid, &user)) {
        socket->Send(
            "cdd",
            29,
            uid,
            MapWSMToAuthReason(3));
        return false;
    }

    char result = 20;
    if (config.useWSM) {
        result = WSMServerMgr::Inst()->LogoutAccountWsid(
            ORIGIN_GAMESERVER_REQUEST,
            socket,
            user,
            webSessionId);
        if (result != 0) {
            socket->Send(
                "cdd",
                29,
                uid,
                MapWSMToAuthReason(999));
        }
    } else if (config.useLightTokenServer) {
        result = TokenServerMgr::Inst()->RequestCloseToken(
            socket,
            user,
            webSessionId);
        if (result != 0) {
            socket->Send(
                "cdd",
                29,
                uid,
                MapWSMToAuthReason(999));
        }
    } else if (config.useGpProxy) {
        result = CGpProxyServerMgr::Inst()->LogoutAccountWsid(
            ORIGIN_GAMESERVER_REQUEST,
            socket,
            *user,
            webSessionId);
        if (result != 0) {
            socket->Send("cdd", 29, uid, 20);
        }
    } else {
        socket->Send(
            "cdd",
            29,
            uid,
            MapWSMToAuthReason(999));
    }

    user->ReleaseRef();
    return false;
}

// L2Authd.exe 0x00431C60, Src/csocketserver.cpp:1226-1272.
bool FreeToPayUphill(
    CSocketServer*,
    const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    const unsigned char worldNumber =
        static_cast<unsigned char>(
            GetCharFromPacket(&packet));
    const int characterId =
        GetIntFromPacket(&packet);
    const int level = GetIntFromPacket(&packet);
    const std::int32_t updateTime =
        GetIntFromPacket(&packet);

    if (!config.useDualCharging) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "AS_FREE_TO_PAY_UPHILL recved, but option set off",
        //     uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=world event=request_ignored operation=free_to_pay_uphill uid=%d server_id=%u reason=useDualCharging_disabled action=ignore_request",
        //     uid, static_cast<unsigned int>(worldNumber))
        // Log.AddLog(
        //     LOG_WARN,
        //     "[world] Ignoring free-to-pay request for account %d on server %u: useDualCharging is disabled.",
        //     uid, static_cast<unsigned int>(worldNumber))
        L2_LOG_WARN(Log,
            "[world] Ignoring free-to-pay request for account %d on server %u: useDualCharging is disabled.",
            uid, static_cast<unsigned int>(worldNumber));
        return false;
    }
    if (!config.useLobbyCharging) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "AS_CHARGE_STOP recved, but option(lobby) set off")
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=world event=request_ignored operation=free_to_pay_uphill uid=%d server_id=%u reason=useLobbyCharging_disabled action=ignore_request",
        //     uid, static_cast<unsigned int>(worldNumber))
        // Log.AddLog(
        //     LOG_WARN,
        //     "[world] Ignoring free-to-pay request for account %d on server %u: useLobbyCharging is disabled.",
        //     uid, static_cast<unsigned int>(worldNumber))
        L2_LOG_WARN(Log,
            "[world] Ignoring free-to-pay request for account %d on server %u: useLobbyCharging is disabled.",
            uid, static_cast<unsigned int>(worldNumber));
        return false;
    }

    const WorldServer world =
        g_ServerList.GetAt(worldNumber);
    if (world.serverKind == 0x101 ||
        world.serverKind == 0x104) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "UpdateCharacterLevel with BEGINNER SERVERID(uid:%d, serverid:%d)",
        //     uid,
        //     worldNumber)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=world event=request_rejected operation=free_to_pay_uphill uid=%d server_id=%u server_kind=%d reason=beginner_world action=ignore_request",
        //     uid, static_cast<unsigned int>(worldNumber), world.serverKind)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[world] Ignoring free-to-pay request for account %d on server %u: beginner worlds are excluded (kind %d).",
        //     uid, static_cast<unsigned int>(worldNumber), world.serverKind)
        L2_LOG_WARN(Log,
            "[world] Ignoring free-to-pay request for account %d on server %u: beginner worlds are excluded (kind %d).",
            uid, static_cast<unsigned int>(worldNumber), world.serverKind);
        return false;
    }

    if (!accountdb.UpdateCharacterLevel(
            uid,
            worldNumber,
            characterId,
            level,
            updateTime)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "UpdateCharacterLevel fail(uid:%d)",
        //     uid)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=world event=character_level_update_failed operation=free_to_pay_uphill uid=%d server_id=%u character_id=%d level=%d reason=database_or_procedure_rejection action=return_without_reply",
        //     uid, static_cast<unsigned int>(worldNumber), characterId, level)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[world] Free-to-pay request for account %d on server %u: database update of character %d level %d failed or was rejected. Returning without a reply.",
        //     uid, static_cast<unsigned int>(worldNumber), characterId, level)
        L2_LOG_ERROR(Log,
            "[world] Free-to-pay request for account %d on server %u: database update of character %d level %d failed or was rejected. Returning without a reply.",
            uid, static_cast<unsigned int>(worldNumber), characterId, level);
    }
    return false;
}

// L2Authd.exe 0x00431E00, Src/csocketserver.cpp:1276-1291.
bool CheckMaxLevelAck(
    CSocketServer* socket,
    const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    const int characterId =
        GetIntFromPacket(&packet);
    const int level = GetIntFromPacket(&packet);
    const unsigned char worldNumber =
        static_cast<unsigned char>(socket->serverid);
    const auto currentTime =
        static_cast<std::int32_t>(std::time(nullptr));
    accountdb.ForceUpdateCharacterLevel(
        uid,
        worldNumber,
        characterId,
        level,
        currentTime);
    return false;
}

// L2Authd.exe 0x00431E90, Src/csocketserver.cpp:1295-1346.
bool DeleteCharacter(
    CSocketServer*,
    const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    const unsigned char worldNumber =
        static_cast<unsigned char>(
            GetCharFromPacket(&packet));
    const int deleteCharacterId =
        GetIntFromPacket(&packet);
    [[maybe_unused]] const int deleteLevel =
        GetIntFromPacket(&packet);
    const int nextCharacterId =
        GetIntFromPacket(&packet);
    const int nextLevel = GetIntFromPacket(&packet);

    if (!config.useDualCharging) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "AS_DELETE_CHAR recved, but option set off",
        //     uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=world event=request_ignored operation=delete_character uid=%d server_id=%u reason=useDualCharging_disabled action=ignore_request",
        //     uid, static_cast<unsigned int>(worldNumber))
        // Log.AddLog(
        //     LOG_WARN,
        //     "[world] Ignoring character-deletion notification for account %d on server %u: useDualCharging is disabled.",
        //     uid, static_cast<unsigned int>(worldNumber))
        L2_LOG_WARN(Log,
            "[world] Ignoring character-deletion notification for account %d on server %u: useDualCharging is disabled.",
            uid, static_cast<unsigned int>(worldNumber));
        return false;
    }
    if (!config.useLobbyCharging) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "AS_CHARGE_STOP recved, but option(lobby) set off")
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=world event=request_ignored operation=delete_character uid=%d server_id=%u reason=useLobbyCharging_disabled action=ignore_request",
        //     uid, static_cast<unsigned int>(worldNumber))
        // Log.AddLog(
        //     LOG_WARN,
        //     "[world] Ignoring character-deletion notification for account %d on server %u: useLobbyCharging is disabled.",
        //     uid, static_cast<unsigned int>(worldNumber))
        L2_LOG_WARN(Log,
            "[world] Ignoring character-deletion notification for account %d on server %u: useLobbyCharging is disabled.",
            uid, static_cast<unsigned int>(worldNumber));
        return false;
    }

    const WorldServer world =
        g_ServerList.GetAt(worldNumber);
    if (world.serverKind == 0x101 ||
        world.serverKind == 0x104) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "DeleteCharacter with BEGINNER SERVERID(uid:%d, serverid:%d)",
        //     uid,
        //     worldNumber)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=world event=request_rejected operation=delete_character uid=%d server_id=%u server_kind=%d reason=beginner_world action=ignore_request",
        //     uid, static_cast<unsigned int>(worldNumber), world.serverKind)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[world] Ignoring character-deletion notification for account %d on server %u: beginner worlds are excluded (kind %d).",
        //     uid, static_cast<unsigned int>(worldNumber), world.serverKind)
        L2_LOG_WARN(Log,
            "[world] Ignoring character-deletion notification for account %d on server %u: beginner worlds are excluded (kind %d).",
            uid, static_cast<unsigned int>(worldNumber), world.serverKind);
        return false;
    }

    bool result = accountdb.DeleteCharacterLevel(
        uid,
        worldNumber,
        deleteCharacterId);
    if (nextCharacterId != 0 && result) {
        const auto currentTime =
            static_cast<std::int32_t>(std::time(nullptr));
        result = accountdb.ForceUpdateCharacterLevel(
            uid,
            worldNumber,
            nextCharacterId,
            nextLevel,
            currentTime);
    }
    if (!result) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "DeleteCharacterLevel fail(uid:%d)",
        //     uid)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=world event=character_level_update_failed operation=delete_character uid=%d server_id=%u deleted_character_id=%d next_character_id=%d next_level=%d reason=delete_or_next_level_update_failed action=return_without_reply",
        //     uid, static_cast<unsigned int>(worldNumber), deleteCharacterId, nextCharacterId, nextLevel)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[world] Character-deletion notification for account %d on server %u failed: could not delete character %d or update next character %d to level %d. Returning without a reply.",
        //     uid, static_cast<unsigned int>(worldNumber), deleteCharacterId, nextCharacterId, nextLevel)
        L2_LOG_ERROR(Log,
            "[world] Character-deletion notification for account %d on server %u failed: could not delete character %d or update next character %d to level %d. Returning without a reply.",
            uid, static_cast<unsigned int>(worldNumber), deleteCharacterId, nextCharacterId, nextLevel);
    }
    return false;
}

// L2Authd.exe 0x00432090, Src/csocketserver.cpp:1350-1370.
bool GetCharacterMaxLevel(
    CSocketServer* socket,
    const unsigned char* packet) {
    unsigned char worldNumber =
        static_cast<unsigned char>(
            GetCharFromPacket(&packet));
    const int uid = GetIntFromPacket(&packet);
    int characterId = 0;
    int level = 0;
    if (accountdb.GetCharacterMaxLevel(
            uid,
            worldNumber,
            characterId,
            level)) {
        socket->Send(
            "cdcdd",
            31,
            uid,
            worldNumber,
            characterId,
            level);
    } else {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Fail to reply AS_GET_CHAR_MAX_LEVEL. Can't get max level(uid:%d)",
        //     uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=world event=max_level_unavailable server_id=%d result_server_id=%u uid=%d reason=lookup_failed action=return_without_reply",
        //     static_cast<int>(socket->serverid), static_cast<unsigned int>(worldNumber), uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[world] Maximum-level lookup failed for server %d (result server %u), account %d. Returning without a reply.",
        //     static_cast<int>(socket->serverid), static_cast<unsigned int>(worldNumber), uid)
        L2_LOG_WARN(Log,
            "[world] Maximum-level lookup failed for server %d (result server %u), account %d. Returning without a reply.",
            static_cast<int>(socket->serverid), static_cast<unsigned int>(worldNumber), uid);
    }
    return false;
}

// L2Authd.exe 0x00432140, Src/csocketserver.cpp:1374-1401.
void ChargeStartAction(
    CSocketServer* socket,
    int uid,
    ChargeType chargeType) {
    LoginUser* user = nullptr;
    if (!accountdb.FindAccount(uid, &user)) {
        socket->Send("cddd", 44, uid, 2, 0);
        // Log.AddLog(
        //     LOG_ERROR,
        //     "AS_CHARGE_START recved, but no session(uid:%d)",
        //     uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=world event=charge_start_rejected server_id=%d uid=%d charge_type=%d reason=account_session_missing response_code=2 kick_reason=10 action=request_kick",
        //     static_cast<int>(socket->serverid), uid, static_cast<int>(chargeType))
        // Log.AddLog(
        //     LOG_WARN,
        //     "[world] Server %d requested charging for account %d (type %d), but the account session is missing. Replying with code 2 and requesting a kick with reason 10.",
        //     static_cast<int>(socket->serverid), uid, static_cast<int>(chargeType))
        L2_LOG_WARN(Log,
            "[world] Server %d requested charging for account %d (type %d), but the account session is missing. Replying with code 2 and requesting a kick with reason 10.",
            static_cast<int>(socket->serverid), uid, static_cast<int>(chargeType));
        socket->Send("cdcc", 1, uid, 10, 0);
        return;
    }

    if (!accountdb.StartCharge(
            socket,
            uid,
            *user,
            chargeType) &&
        !config.allowF2PMultiClients &&
        !accountdb.KickAccount(
            uid,
            34,
            true,
            true)) {
        socket->Send("cdcc", 1, uid, 34, 0);
    }
    user->ReleaseRef();
}

// L2Authd.exe 0x00432230, Src/csocketserver.cpp:1405-1416.
bool ChargeStart(
    CSocketServer* socket,
    const unsigned char* packet) {
    if (!config.useDualCharging) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "AS_CHARGE_START recved, but option set off")
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=world event=request_ignored operation=charge_start server_id=%d reason=useDualCharging_disabled action=ignore_request",
        //     static_cast<int>(socket->serverid))
        // Log.AddLog(
        //     LOG_WARN,
        //     "[world] Ignoring charge-start request from server %d: useDualCharging is disabled.",
        //     static_cast<int>(socket->serverid))
        L2_LOG_WARN(Log,
            "[world] Ignoring charge-start request from server %d: useDualCharging is disabled.",
            static_cast<int>(socket->serverid));
        return false;
    }
    const int uid = GetIntFromPacket(&packet);
    ChargeStartAction(
        socket,
        uid,
        CHARGE_TYPE_NONE);
    return false;
}

// L2Authd.exe 0x00432280, Src/csocketserver.cpp:1420-1432.
bool ChargeStartEx(
    CSocketServer* socket,
    const unsigned char* packet) {
    if (!config.useDualCharging) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "AS_CHARGE_START recved, but option set off")
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=world event=request_ignored operation=charge_start_ex server_id=%d reason=useDualCharging_disabled action=ignore_request",
        //     static_cast<int>(socket->serverid))
        // Log.AddLog(
        //     LOG_WARN,
        //     "[world] Ignoring extended charge-start request from server %d: useDualCharging is disabled.",
        //     static_cast<int>(socket->serverid))
        L2_LOG_WARN(Log,
            "[world] Ignoring extended charge-start request from server %d: useDualCharging is disabled.",
            static_cast<int>(socket->serverid));
        return false;
    }
    const int uid = GetIntFromPacket(&packet);
    const auto chargeType = static_cast<ChargeType>(
        GetShortFromPacket(&packet));
    ChargeStartAction(socket, uid, chargeType);
    return false;
}

// L2Authd.exe 0x004322F0, Src/csocketserver.cpp:1436-1471.
bool ChargeStop(
    CSocketServer*,
    const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    if (!config.useDualCharging) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "AS_CHARGE_STOP recved, but option set off")
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=world event=request_ignored operation=charge_stop uid=%d reason=useDualCharging_disabled action=ignore_request",
        //     uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[world] Ignoring charge-stop request for account %d: useDualCharging is disabled.",
        //     uid)
        L2_LOG_WARN(Log,
            "[world] Ignoring charge-stop request for account %d: useDualCharging is disabled.",
            uid);
        return false;
    }

    LoginUser* user = nullptr;
    if (!accountdb.FindAccount(uid, &user)) {
        // Log.AddLog(
        //     LOG_DEBUG,
        //     "AS_CHARGE_STOP recved, but no session(uid:%d)",
        //     uid)
        // Log.AddLog(
        //     LOG_DEBUG,
        //     "component=world event=charge_stop_skipped uid=%d reason=account_session_missing action=ignore_request",
        //     uid)
        // Log.AddLog(
        //     LOG_DEBUG,
        //     "[world] Skipping charge-stop request for account %d: no account session exists.",
        //     uid)
        L2_LOG_DEBUG(Log,
            "[world] Skipping charge-stop request for account %d: no account session exists.",
            uid);
        return false;
    }

    if (user->chargeflag) {
        const auto currentTime =
            static_cast<std::int32_t>(std::time(nullptr));
        accountdb.EndCharge(uid, *user, currentTime);
    } else {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "AS_CHARGE_STOP recved, but not in charge(uid:%d)",
        //     uid)
        // Log.AddLog(
        //     LOG_DEBUG,
        //     "component=world event=charge_stop_skipped uid=%d reason=not_charging action=ignore_request",
        //     uid)
        // Log.AddLog(
        //     LOG_DEBUG,
        //     "[world] Skipping charge-stop request for account %d: charging is not active.",
        //     uid)
        L2_LOG_DEBUG(Log,
            "[world] Skipping charge-stop request for account %d: charging is not active.",
            uid);
    }
    user->ReleaseRef();
    return false;
}

// L2Authd.exe 0x004323D0, Src/csocketserver.cpp:1475-1525.
bool ServerActivateMark(
    CSocketServer* socket,
    const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    const std::int32_t activationTime =
        GetIntFromPacket(&packet);
    unsigned char activationInformation[8]{};
    GetBinFromPacket(
        &packet,
        8,
        activationInformation);

    if (!config.supportSleepAccountActivate) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "AS_ACTIVATE_MARK recved, but auth server option set off.")
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=world event=activation_rejected server_id=%d uid=%d reason=supportSleepAccountActivate_disabled response_code=20 action=reply_failure",
        //     static_cast<int>(socket->serverid), uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[world] Rejecting activation for server %d, account %d: supportSleepAccountActivate is disabled. Replying with failure code 20.",
        //     static_cast<int>(socket->serverid), uid)
        L2_LOG_WARN(Log,
            "[world] Rejecting activation for server %d, account %d: supportSleepAccountActivate is disabled. Replying with failure code 20.",
            static_cast<int>(socket->serverid), uid);
        socket->Send("cdd", 32, uid, 20);
        return false;
    }

    LoginUser* user = nullptr;
    if (!accountdb.FindAccount(uid, &user)) {
        L2_LOG_WARN(Log,
            "[world] Rejecting activation from server %d for account %d: the account session is missing. Replying with failure code 2.",
            static_cast<int>(socket->serverid), uid);
        socket->Send("cdd", 32, uid, 2);
        return false;
    }

    const char result = static_cast<char>(
        accountdb.MarkActivateTime(
            uid,
            activationTime,
            activationInformation));
    if (result == 0) {
        user->activateTime = activationTime;
        std::memcpy(
            user->activateInformation,
            activationInformation,
            8);
        socket->Send("cdd", 32, uid, 0);
    } else {
        L2_LOG_WARN(Log,
            "[world] Activation from server %d for account %d failed with result %d. Replying with that result; in-memory activation data remains unchanged.",
            static_cast<int>(socket->serverid), uid, static_cast<int>(result));
        socket->Send("cdd", 32, uid, result);
    }
    user->ReleaseRef();
    return false;
}

// L2Authd.exe 0x00432550, Src/csocketserver.cpp:1529-1582.
bool ServerAuthnTokenRequest(
    CSocketServer* socket,
    const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    char appId[38]{};
    GetStrFromPacket(&packet, 38, appId);
    char authnToken[101]{};

    const auto sendResult =
        [&](int result) {
            socket->Send(
                "cddb",
                33,
                uid,
                result,
                101,
                authnToken);
        };

    if (!config.usePAServer) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "AS_REQUEST_AUTHNTOKEN recvd, but usePAServer option is off.")
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=world event=token_request_rejected server_id=%d uid=%d reason=usePAServer_disabled response_code=20 action=reply_failure",
        //     static_cast<int>(socket->serverid), uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[world] Rejecting token request for server %d, account %d: usePAServer is disabled. Replying with failure code 20.",
        //     static_cast<int>(socket->serverid), uid)
        L2_LOG_WARN(Log,
            "[world] Rejecting token request for server %d, account %d: usePAServer is disabled. Replying with failure code 20.",
            static_cast<int>(socket->serverid), uid);
        sendResult(20);
        return false;
    }

    LoginUser* user = nullptr;
    if (!accountdb.FindAccount(uid, &user)) {
        L2_LOG_WARN(Log,
            "[world] Rejecting token request from server %d for account %d: the account session is missing. Replying with failure code 2.",
            static_cast<int>(socket->serverid), uid);
        sendResult(2);
        return false;
    }

    const char result = static_cast<char>(
        accountdb.RequestAuthnTokenByPAServer(
            socket,
            user,
            appId));
    if (result != 0) {
        sendResult(result);

        // Original diagnostic-only GUID extraction kept. The replacement omits GUID values.
        // // The selected x86 call pushes uid, a 16-byte GUID and portalUid for
        // // a format containing only three %d conversions. The logger consumes
        // // uid and the first two little-endian GUID dwords. Spelling those
        // // consumed values keeps the shipping result without
        // // carrying the original variadic undefined behaviour across ABIs.
        // const std::uint32_t secondGuidDword =
        //     static_cast<std::uint32_t>(user->userId.Data2) |
        //     (static_cast<std::uint32_t>(
        //          user->userId.Data3)
        //      << 16)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "AppToke: %d,%d,%d",
        //     uid,
        //     static_cast<std::int32_t>(
        //         user->userId.Data1),
        //     static_cast<std::int32_t>(
        //         secondGuidDword))
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=world event=token_request_failed server_id=%d uid=%d result=%d kick_reason=2 action=reply_failure_and_request_kick token=redacted guid=redacted",
        //     static_cast<int>(socket->serverid), uid, static_cast<int>(result))
        // Log.AddLog(
        //     LOG_WARN,
        //     "[world] Token request failed for server %d, account %d (result %d). Replying with failure and requesting a kick with reason 2.",
        //     static_cast<int>(socket->serverid), uid, static_cast<int>(result))
        L2_LOG_WARN(Log,
            "[world] Token request failed for server %d, account %d (result %d). Replying with failure and requesting a kick with reason 2.",
            static_cast<int>(socket->serverid), uid, static_cast<int>(result));

        char account[15]{};
        std::strncpy(account, user->account, 14);
        socket->Send("cdcs", 1, uid, 2, account);
    }

    user->ReleaseRef();
    return false;
}

// L2Authd.exe 0x00433C20, Src/csocketserver.cpp:41-51.
short MapQuitGameReasonToLogoutReason(
    short reason,
    int gameId) {
    if (gameId == 27 && reason == 203) {
        return 1;
    }
    return 0;
}

// L2Authd.exe 0x00433C50, compiled from Src/protocol.h:1330-1342.
int MapWSMToAuthReason(int reason) {
    if (reason == 0) {
        return 0;
    }
    if (reason == 3) {
        return 2;
    }
    if (reason == 10) {
        return 37;
    }
    return 20;
}
