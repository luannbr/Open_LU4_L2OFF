#include "ClientSession.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/TickCount32.h"
#include "Shared/src/Runtime/TimerQueue.h"
#include "Config.h"
#include "ConnectedIPList.h"
#include "IOBuffer.h"
#include "IOServerEx.h"
#include "IovationServerMgr.h"
#include "IPAccessLimit.h"
#include "IPSessionDB.h"
#include "Log.h"
#include "Lu4Validator.h"
#include "PAServerMgr.h"
#include "PBServerMgr.h"
#include "Protocol.h"
#include "Reporter.h"
#include "ServerList.h"
#include "SocketServerEx.h"
#include "Util.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

std::int32_t CClientSession::g_nTryLoginCnt = 0;

namespace {

constexpr std::uint32_t kClosedSessionId = UINT32_C(0xffffffff);
constexpr int kClientPayloadCapacity = 0x1ff9;

void LogAssembleFailure(unsigned int sessionId, const char* format) {
    // Log.AddLog(LOG_ERROR, "%d: assemble too large packet. format %s", sessionId, format)
    // L2_LOG_ERROR(Log, "%d: assemble too large packet. format %s", sessionId, format)
    L2_LOG_ERROR(Log,
        "[client] Cannot assemble a packet for session %u within the %d-byte payload capacity "
        "(format \"%s\"). Packet not submitted.",
        sessionId, kClientPayloadCapacity, format ? format : "<null>");
}

void WriteClientPacket(CClientSession* session, CIOBuffer* buffer, int payloadLength) {
    // CSocketServerEx* const socket = serverEx.FindSocket(CIOServerEx::GetGateId(session->GetSessionId()))
    const unsigned int sessionId = session->GetSessionId();
    const unsigned int gateId = CIOServerEx::GetGateId(sessionId);
    CSocketServerEx* const socket = serverEx.FindSocket(gateId);
    if (socket == nullptr) {
        // The selected EXE leaves this allocation owned by nobody when its
        // gate vanishes between lookup and send. Preserve that baseline bug.
        L2_LOG_WARN(Log,
            "[client] Packet for session %u (%d payload bytes) was not submitted: "
            "AuthGate %u is not connected. The legacy path retains the allocated buffer.",
            sessionId, payloadLength, gateId);
        return;
    }

    if ((config.usePacketLog & 2U) != 0U) {
        WritePacketLog(
            "Auth->Gate", socket->m_hSocket, reinterpret_cast<const unsigned char*>(buffer->m_buffer + 7), payloadLength,
            0x400
        );
    }
    socket->Write(buffer);
    socket->ReleaseRef();
    session->SetLastIO(runtime::TickCount32());
}

} // namespace

// L2Authd.exe 0x0041DC50, Src/clientsession.cpp:23-35.
CClientSession::CClientSession(unsigned int sessionId) : m_preLoginUser{} {
    runtime::raw_atomic::IncrementI32(&reporter.m_SocketCount);
    m_refcount = 0;
    um_mode = UM_PRE_LOGIN;
    m_uid = 0;
    m_TimerHandle = nullptr;
    m_sessionId = sessionId;
    m_lastIO = static_cast<std::int32_t>(runtime::TickCount32());
    AddRef();
}

// L2Authd.exe 0x0041DCD0, Src/clientsession.cpp:38-40.
CClientSession::~CClientSession() { runtime::raw_atomic::DecrementI32(&reporter.m_SocketCount); }

// L2Authd.exe 0x004899D0, Src/clientsession.h:9.
CClientSession* CClientSession::Allocate(unsigned int sessionId) { return new (std::nothrow) CClientSession(sessionId); }

// L2Authd.exe 0x0041DCF0, Src/clientsession.cpp:55-67.
void CClientSession::OnCreate() {
    // runtime::timer_queue::CreateTimer(
    //     &m_TimerHandle, g_hSocketTimer, ClientSessionTimerCallback,
    //     reinterpret_cast<void*>(static_cast<std::uintptr_t>(m_sessionId)), static_cast<std::uint32_t>(config.SocketTimeOut),
    //     static_cast<std::uint32_t>(config.SocketTimeOut)
    // )
    const bool timerCreated = runtime::timer_queue::CreateTimer(
        &m_TimerHandle, g_hSocketTimer, ClientSessionTimerCallback,
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(m_sessionId)), static_cast<std::uint32_t>(config.SocketTimeOut),
        static_cast<std::uint32_t>(config.SocketTimeOut)
    );
    if (!timerCreated) {
        L2_LOG_WARN(Log,
            "[client] Could not create the idle timer for session %u (period %d ms). "
            "The session remains active without timer-based cleanup.",
            m_sessionId, config.SocketTimeOut);
    }

    const std::uint32_t high = static_cast<std::uint32_t>(std::rand()) << 16U;
    const std::uint32_t low = static_cast<std::uint32_t>(std::rand());
    m_oneTimeKey = static_cast<std::int32_t>(high | low);
}

// L2Authd.exe 0x0041DD50, Src/clientsession.cpp:85-165.
void CClientSession::OnRead(const unsigned char* packet) {
    m_lastIO = static_cast<std::int32_t>(runtime::TickCount32());

    bool closeSession = false;
    switch (*packet) {
    case 0:
        closeSession = LoginPacket(packet + 1);
        break;
    case 1:
        closeSession = ServerListPacket(packet + 1);
        break;
    case 2:
        closeSession = SelectServerPacket(packet + 1);
        break;
    case 3:
        closeSession = LogoutPacket(packet + 1);
        break;
    case 5:
        closeSession = ServerListExtPacket(packet + 1);
        break;
    case 6:
        closeSession = CheckSecurityCardPacket(packet + 1);
        break;
    case 8:
        closeSession = UpdateSessionPacket(packet + 1);
        break;
    case 9:
        closeSession = WebSessionLoginPacket(packet + 1);
        break;
    case 10:
        closeSession = OTPCheckPacket(packet + 1);
        break;
    case 11:
        closeSession = ExternalTokenLoginPacket(packet + 1);
        break;
    case 12:
        closeSession = AuxAuthenticationAckPacket(packet + 1);
        break;
    case 13:
        closeSession = RecvThridPartyLoginPacket(packet + 1);
        break;
    case 16:
        closeSession = RecvIovationPacket(packet + 1);
        break;
    case 17:
        closeSession = RecvThridPartySteamLoginPacket(packet + 1);
        break;
    case 18:
        closeSession = RecvLoginTokenPacket(packet + 1);
        break;
    default:
        L2_LOG_WARN(Log,
            "[client] Unsupported opcode %u received by session %u. Requesting session closure.",
            static_cast<unsigned int>(*packet), m_sessionId);
        CloseSessionActively();
        return;
    }

    if (closeSession) {
        CloseSessionActively();
    }
}

// L2Authd.exe 0x0041E000, Src/clientsession.cpp:168-189.
void CClientSession::OnClose(unsigned int sessionId) {
    void* const timer = runtime::raw_atomic::ExchangePointer(&m_TimerHandle, static_cast<void*>(nullptr));
    if (timer != nullptr) {
        runtime::timer_queue::DeleteTimer(g_hSocketTimer, timer);
    }
    m_TimerHandle = nullptr;
    serverEx.RemoveSession(sessionId);

    if (um_mode != UM_PLAY_OK && um_mode != UM_CHANGE_SERVER && m_uid != 0 && um_mode != UM_PRE_LOGIN) {
        accountdb.KickAccount(m_uid, 7, false, true);
        accountdb.LogoutProcess(m_uid, 0);
    }
    IPaccessLimit.DelAccessIP(getaddr());
}

// L2Authd.exe 0x0041E0C0, Src/clientsession.cpp:219-225.
void CClientSession::OnTimerCallback() {
    const std::uint32_t elapsed = runtime::TickCount32() - static_cast<std::uint32_t>(m_lastIO);
    if (config.SocketTimeOut - 500 <= static_cast<std::int32_t>(elapsed)) {
        CloseSessionActively();
    }
    ReleaseRef();
}

// L2Authd.exe 0x0041E100, Src/clientsession.cpp:228-230.
void CClientSession::SetAddress(in_addr address) { m_addr = address; }

// L2Authd.exe 0x0041E120, Src/clientsession.cpp:233-262. This is the actual
// variadic overload. Unlike the va_list overload below, it does not change
// UM_PLAY_OK when the first payload byte is seven.
void CClientSession::Send(const char* format, ...) {
    // Original selection-race fix: serialize PlayFail/PlayOk publication with
    // request admission. The original send body below is kept.
    std::lock_guard selectionLock(m_serverSelectionMutex);
    CIOBuffer* const buffer = CIOBuffer::Alloc();
    std::va_list arguments;
    va_start(arguments, format);
    const int payloadLength = Assemble(buffer->m_buffer + 7, kClientPayloadCapacity, format, arguments);
    va_end(arguments);

    if (payloadLength == 0) {
        LogAssembleFailure(m_sessionId, format);
        return;
    }

    Assemble(buffer->m_buffer, 7, "cdh", 2, CIOServerEx::GetSocketId(m_sessionId), payloadLength + 2);
    buffer->m_size = static_cast<std::uint32_t>(payloadLength + 7);
    RecordServerSelectionReply(static_cast<unsigned char>(buffer->m_buffer[7]));
    WriteClientPacket(this, buffer, payloadLength);
}

// L2Authd.exe 0x0041E240, Src/clientsession.cpp:265-295.
void CClientSession::Send(const char* format, std::va_list arguments) {
    // Original selection-race fix: an asynchronous reply must update the
    // reservation under the same lock as SelectServerPacket.
    std::lock_guard selectionLock(m_serverSelectionMutex);
    CIOBuffer* const buffer = CIOBuffer::Alloc();
    const int payloadLength = Assemble(buffer->m_buffer + 7, kClientPayloadCapacity, format, arguments);
    if (payloadLength == 0) {
        LogAssembleFailure(m_sessionId, format);
        return;
    }

    Assemble(buffer->m_buffer, 7, "cdh", 2, CIOServerEx::GetSocketId(m_sessionId), payloadLength + 2);
    if (buffer->m_buffer[7] == 7) {
        SetUserMode(UM_PLAY_OK);
    }
    buffer->m_size = static_cast<std::uint32_t>(payloadLength + 7);
    RecordServerSelectionReply(static_cast<unsigned char>(buffer->m_buffer[7]));
    WriteClientPacket(this, buffer, payloadLength);
}

// L2Authd.exe 0x0041E380, Src/clientsession.cpp:299-334.
void CClientSession::Send(const char* sendMessage, int messageLength) {
    // Original selection-race fix: keep the raw-frame overload consistent
    // with both formatted reply overloads.
    std::lock_guard selectionLock(m_serverSelectionMutex);
    if (messageLength == 0 || messageLength > kClientPayloadCapacity) {
        // Sensitive data: the binary message is not a terminated string and may contain session keys.
        // Log.AddLog(LOG_ERROR, "%d: CClientSession::Send(sendmsg %s, msglen %d)", m_sessionId, sendMessage, messageLength)
        // Log.AddLog(LOG_ERROR,
        //     "component=network event=send_rejected session_id=%u bytes=%d capacity=%d reason=invalid_length",
        //     m_sessionId, messageLength, kClientPayloadCapacity)
        // Log.AddLog(LOG_ERROR,
        //     "[client] Cannot send to session %u: invalid message length %d (capacity %d bytes).",
        //     m_sessionId, messageLength, kClientPayloadCapacity)
        L2_LOG_ERROR(Log,
            "[client] Cannot send to session %u: invalid message length %d (capacity %d bytes).",
            m_sessionId, messageLength, kClientPayloadCapacity);
        return;
    }

    CIOBuffer* const buffer = CIOBuffer::Alloc();
    Assemble(buffer->m_buffer, 8192, "cdb", 2, CIOServerEx::GetSocketId(m_sessionId), messageLength, sendMessage);
    buffer->m_size = static_cast<std::uint32_t>(messageLength + 5);

    CSocketServerEx* const socket = serverEx.FindSocket(CIOServerEx::GetGateId(m_sessionId));
    if (socket == nullptr) {
        // The selected EXE leaves this allocation owned by nobody when its
        // gate vanishes between lookup and send. Preserve that baseline bug.
        L2_LOG_WARN(Log,
            "[client] Raw packet for session %u (%d message bytes) was not submitted: "
            "AuthGate %u is not connected. The legacy path retains the allocated buffer.",
            m_sessionId, messageLength, CIOServerEx::GetGateId(m_sessionId));
        return;
    }
    if ((config.usePacketLog & 2U) != 0U) {
        WritePacketLog(
            "Auth->Gate", socket->m_hSocket, reinterpret_cast<const unsigned char*>(buffer->m_buffer + 7), messageLength - 2,
            0x400
        );
    }
    if (messageLength >= 3) {
        // This overload includes the two-byte client frame length.
        RecordServerSelectionReply(static_cast<unsigned char>(sendMessage[2]));
    }
    socket->Write(buffer);
    socket->ReleaseRef();
    m_lastIO = static_cast<std::int32_t>(runtime::TickCount32());
}

// L2Authd.exe 0x0041E490, Src/clientsession.cpp:338-343.
void CClientSession::SendFail(char command, char result, LoginUser* user) {
    if (user != nullptr) {
        user->ReleaseRef();
    }
    // The client shows a login failure as two system messages. In the European
    // (4game) client of this release, reasons 2 and 3 (wrong account or
    // password; 3 is what this AuthD sends) have the "account restricted for
    // violating the EULA" text as their second message (SystemMsg 450, 452).
    // Together they are longer than the client's message buffer: it overruns
    // its stack and closes. tools/client/patch_systemmsg.py fixes the client;
    // for clients without it, reason 5 shows "Incorrect account information."
    // and the support notice (SystemMsg 453, 454), which fit.
    if (config.unpatchedEuClientLoginFail && command == 1 && (result == 2 || result == 3)) {
        result = 5;
    }
    Send("cc", command, result);
}

// L2Authd.exe 0x0041E4D0, Src/clientsession.cpp:347-349.
const char* CClientSession::IP() { return ::inet_ntoa(m_addr); }

// L2Authd.exe 0x0041E4F0, Src/clientsession.cpp:352-423.
bool CClientSession::LoginPacket(const unsigned char* packet) {
    m_lastIO = static_cast<std::int32_t>(runtime::TickCount32());
    runtime::raw_atomic::IncrementI32(&g_nTryLoginCnt);
    runtime::raw_atomic::IncrementI32(&reporter.m_ClientLoginTry);

    char account[65];
    char password[33];
    std::memset(account, 0, sizeof(account));
    std::memset(password, 0, sizeof(password));
    if (!config.useGCSideExtendAccount) {
        std::memcpy(account, packet, 14);
        account[14] = '\0';
        packet += 14;
        std::memcpy(password, packet, 16);
        password[16] = '\0';
        packet += 16;
    } else {
        std::memcpy(account, packet, 64);
        account[64] = '\0';
        packet += 64;
        std::memcpy(password, packet, 32);
        password[32] = '\0';
        packet += 32;
    }

    const int address = GetIntFromPacket(&packet);
    const int subscription = GetIntFromPacket(&packet);
    const auto cdKind = static_cast<unsigned short>(GetShortFromPacket(&packet));
    const char clientInstances = GetCharFromPacket(&packet);
    if (config.lu4Mode && std::memcmp(account, "\x7fLU4", 4) == 0) {
        // AuthGateD marks LU4 logins (its lu4Port); take the credentials the
        // client hook sent from this address (Lu4Validator.h).
        m_lu4 = true;
        std::string lu4Account;
        std::string lu4Password;
        if (!lu4::TakeCredentials(m_addr, lu4Account, lu4Password)) {
            L2_LOG_WARN(Log,
                "[lu4] Login from %s on session %u has no credentials from the client hook "
                "(lu4authhook.dll not injected, or more than five minutes passed). Login refused.",
                IP(), m_sessionId);
            SendFail(1, 3, nullptr);
            return false;
        }
        std::memset(account, 0, sizeof(account));
        std::memset(password, 0, sizeof(password));
        std::memcpy(account, lu4Account.data(), std::min(lu4Account.size(), sizeof(account) - 1));
        std::memcpy(password, lu4Password.data(), std::min(lu4Password.size(), sizeof(password) - 1));
    }
    return accountdb.LoginAccount(this, account, password, subscription, cdKind, clientInstances, address);
}

// L2Authd.exe 0x0041E7E0, Src/clientsession.cpp:429-693. Every Send call
// below was checked directly at 0x0041E890..0x0041F16A because the decompiler
// dropped the variadic arguments and assigned CClientSession field names to
// the LoginUser pointer.
bool CClientSession::SelectServerPacket(const unsigned char* packet) {
    // Original behavior bug: L2Authd.exe 0x41E7E0 has no pending reservation.
    // AboutToPlay at 0x40EFB0/0x40EFC4 rejects only PlayOk/InGame. Repeated
    // clicks before the reply can therefore request multiple world tickets.
    // This admission guard is new. The original handler below is kept.
    {
        std::lock_guard selectionLock(m_serverSelectionMutex);
        if (m_serverSelectionReserved) {
            // A repeated click neither starts a second asynchronous request
            // nor sends a failure behind the first request's PlayOk.
            return false;
        }
        m_serverSelectionReserved = true;
    }
    const unsigned int uid = static_cast<unsigned int>(GetIntFromPacket(&packet));
    const int md5Key = GetIntFromPacket(&packet);
    const char serverId = GetCharFromPacket(&packet);
    m_lastIO = static_cast<std::int32_t>(runtime::TickCount32());

    LoginUser* user = nullptr;
    if (!accountdb.FindAccount(static_cast<int>(uid), &user)) {
        Send("cc", 6, 10);
        L2_LOG_WARN(Log,
            "[login] Server %u was not selected for session %u, account %u: "
            "the account is absent from AuthD's in-memory registry. Requested failure response 10; the session remains open.",
            static_cast<unsigned int>(static_cast<unsigned char>(serverId)), m_sessionId, uid);
        return false;
    }

    if (user->md5key != md5Key) {
        Send("cc", 6, 10);
        // Log.AddLog(
        //     LOG_ERROR, "md5key does not matching - account(%s) req_uid(%d) sess_uid(%d) acc_ip(%s) sess_ip(%s)",
        //     user->account, uid, m_uid, ::inet_ntoa(user->loginIp), IP()
        // )
        // L2_LOG_ERROR(Log, "md5key does not matching - account(%s) req_uid(%d) sess_uid(%d) acc_ip(%s) sess_ip(%s)",
        //     user->account, uid, m_uid, ::inet_ntoa(user->loginIp), IP()
        // )
        // Snapshot the first address before IP() calls inet_ntoa again.
        const std::string accountAddress(::inet_ntoa(user->loginIp));
        L2_LOG_WARN(Log,
            "[login] Server selection rejected for session %u: authentication key mismatch "
            "(requested account %u, session account %u, login IP %s, session IP %s). "
            "Requested failure response 10 and session closure.",
            m_sessionId, uid, static_cast<unsigned int>(m_uid), accountAddress.c_str(), IP());
        user->ReleaseRef();
        return true;
    }

    if (um_mode == UM_PLAY_OK) {
        Send("cc", 6, 7);
        // Log.AddLog(LOG_WARN, "Servers are selected twice by account(%s)", user->account)
        // L2_LOG_WARN(Log, "Servers are selected twice by account(%s)", user->account)
        L2_LOG_WARN(Log,
            "[login] Repeated server selection rejected for session %u, account %u: "
            "the session is already in play-ready state. Requested failure response 7.",
            m_sessionId, uid);
        user->ReleaseRef();
        return false;
    }

    StdAccount(user->account);
    if (!g_ServerList.ExistId(serverId)) {
        Send("cc", 6, 8);
        L2_LOG_INFO(Log,
            "[login] Server selection rejected for session %u, account %u: server %u is not registered. "
            "Requested failure response 8; the session remains open.",
            m_sessionId, uid, static_cast<unsigned int>(static_cast<unsigned char>(serverId)));
        user->ReleaseRef();
        return false;
    }

    if (config.usePBServer && config.PBType == 2) {
        user->selectedServerid = serverId;
        char chargeServerId = serverId;
        const WorldServer world = g_ServerList.GetAt(serverId);
        if (world.serverKind == 0x101 || world.serverKind == 0x104) {
            chargeServerId = user->originChargedServerId;
        }
        const char result = PBServerMgr::Inst()->RequestGetValueInfo(static_cast<int>(uid), user, this, 2, chargeServerId);
        if (result != 0) {
            Send("cc", 6, result);
            user->ReleaseRef();
        }
        return false;
    }

    if (config.FreeServer == 3) {
        const WorldServer world = GetWorldServer(serverId);
        if (world.serverid < 1) {
            Send("cc", 6, 8);
            user->ReleaseRef();
            return false;
        }
        if ((world.serverKind & 0x40U) == 0U) {
            g_ServerList.MapForbiddenServers(
                user->pbServer.availableServers, user->pbServer.availableServersSize, user->forbidden_servers, 16
            );
        }
        g_ServerList.MapForbiddenServers(user->age, user->forbidden_servers, 16);
    }

    if (!accountdb.IsPermittedServer(serverId, user->forbidden_servers, 16)) {
        if (CIPSessionDB::IsIPCharging(user->stat)) {
            const int sessionId = ipsessionDB.DelSessionID(static_cast<int>(uid));
            if (sessionId != 0) {
                ipsessionDB.ReleaseSessionRequest(sessionId, user->loginIp, user->stat);
            }
        }
        if (user->age < g_ServerList.GetAgeLimit(serverId)) {
            L2_LOG_INFO(Log,
                "[login] Server selection rejected for session %u, account %u on server %u: "
                "the account age is below the server limit. Requested failure response 12.",
                m_sessionId, uid, static_cast<unsigned int>(static_cast<unsigned char>(serverId)));
            Send("cc", 6, 12);
        } else {
            L2_LOG_INFO(Log,
                "[login] Server selection rejected for session %u, account %u on server %u: "
                "the account is not permitted on that server. Requested failure response 33.",
                m_sessionId, uid, static_cast<unsigned int>(static_cast<unsigned char>(serverId)));
            Send("cc", 6, 33);
        }
        user->ReleaseRef();
        return false;
    }

    const bool revivalServer = g_ServerList.IsRevivalServer(serverId);
    if (((user->loginflag & 0x800U) == 0U && revivalServer) || ((user->loginflag & 0x800U) != 0U && !revivalServer)) {
        L2_LOG_INFO(Log,
            "[login] Server selection rejected for session %u, account %u on server %u: "
            "the revival-server flag does not match. Requested failure response 200.",
            m_sessionId, uid, static_cast<unsigned int>(static_cast<unsigned char>(serverId)));
        Send("cc", 6, 200);
        user->ReleaseRef();
        return false;
    }

    if (config.limitIPNumforEachGS > 0 &&
        ConnectedIPList::Inst()->CheckIPOverConnection(serverId, user->loginIp, static_cast<int>(uid))) {
        L2_LOG_INFO(Log,
            "[login] Server selection rejected for session %u, account %u on server %u: "
            "the per-server IP connection limit was reached. Requested failure response 76.",
            m_sessionId, uid, static_cast<unsigned int>(static_cast<unsigned char>(serverId)));
        Send("cc", 6, 76);
        user->ReleaseRef();
        return false;
    }

    if (config.FreeServer == 3) {
        const WorldServer world = GetWorldServer(serverId);
        if (world.serverid < 1) {
            L2_LOG_WARN(Log,
                "[login] Free-server lookup no longer contains server %u for session %u, account %u. "
                "Requested failure response 8; the session remains open.",
                static_cast<unsigned int>(static_cast<unsigned char>(serverId)), m_sessionId, uid);
            Send("cc", 6, 8);
            user->ReleaseRef();
            return false;
        }
        if ((world.serverKind & 0x40U) != 0U) {
            AddRef();
            user->chargeServerId = 0;
            user->stat = 2000;
            accountdb.AboutToPlay(this, static_cast<int>(uid), user, 0, serverId, user->stat, 0);
            ReleaseRef();
            user->ReleaseRef();
            return false;
        }
        if (user->chargeServerId == 0 && user->stat == 2000) {
            L2_LOG_INFO(Log,
                "[login] Server selection rejected for session %u, account %u on server %u: "
                "a subscription is required. Requested failure response 18.",
                m_sessionId, uid, static_cast<unsigned int>(static_cast<unsigned char>(serverId)));
            Send("cc", 6, 18);
            user->ReleaseRef();
            return false;
        }
    }

    const int payStatKind = (user->stat % 1000) / 100;
    if (CIPSessionDB::IsIPCharging(user->stat) && config.UseIPServer) {
        const char result = ipsessionDB.ReadyToIPCharge(uid, getaddr().s_addr, user->stat, serverId);
        if (result == 10) {
            L2_LOG_WARN(Log,
                "[login] IP billing was not started for session %u, account %u on server %u (result 10). "
                "Requested failure response 20.",
                m_sessionId, uid, static_cast<unsigned int>(static_cast<unsigned char>(serverId)));
            Send("cc", 6, 20);
        }
        user->ReleaseRef();
        return false;
    }

    if (!config.usePBServer || user->chargeServerId != 2) {
        int remainingTime = 0;
        char result = 0;
        if (payStatKind != 0) {
            if (payStatKind == 2 || payStatKind == 5) {
                result = accountdb.CheckUserTime(static_cast<int>(uid), &remainingTime);
            } else if (payStatKind == 1 && config.Country == 0) {
                result = accountdb.CheckUserDuration(static_cast<int>(uid), &remainingTime);
            }
        }

        if (result == 0) {
            AddRef();
            accountdb.AboutToPlay(this, static_cast<int>(uid), user, remainingTime, serverId, user->stat, 0);
            ReleaseRef();
        } else {
            L2_LOG_INFO(Log,
                "[login] Local time check rejected server selection for session %u, account %u on server %u "
                "(reason %d). Requested that failure response.",
                m_sessionId, uid, static_cast<unsigned int>(static_cast<unsigned char>(serverId)),
                static_cast<int>(result));
            Send("cc", 6, result);
        }
        user->ReleaseRef();
        return false;
    }

    AddRef();
    accountdb.AboutToPlay(
        this, static_cast<int>(uid), user, user->pbServer.remainTime, serverId, user->stat, user->pbServer.reservations
    );
    user->ReleaseRef();
    ReleaseRef();
    return false;
}

// L2Authd.exe 0x0041F200, Src/clientsession.cpp:696-711.
bool CClientSession::LogoutPacket(const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    const int reason = GetIntFromPacket(&packet);
    return accountdb.logoutAccount(uid, reason);
}

// L2Authd.exe 0x0041F250, Src/clientsession.cpp:714-759.
bool CClientSession::ServerListPacket(const unsigned char* packet) {
    std::array<char, 6146> sendBuffer{};
    const int uid = GetIntFromPacket(&packet);
    GetIntFromPacket(&packet);

    char account[16];
    if (accountdb.FindAccount(uid, account) < 0) {
        L2_LOG_WARN(Log,
            "[server list] Standard-list request for session %u, account %d has no in-memory account. "
            "Requesting session closure without a response.",
            m_sessionId, uid);
        return true;
    }

    m_lastIO = static_cast<std::int32_t>(runtime::TickCount32());
    if (m_lu4) {
        // LU4 list body after the marker opcode 4; AuthGateD frames it
        // (Lu4Login.h there): c 3, c 0x12, c count, then per server c id,
        // 4-byte address, d port, c status (1 up), UTF-16 name, 00 00, c 0.
        std::vector<unsigned char> body{4, 0x03, 0x12, 0};
        for (const auto& [server, status] : g_ServerList.Lu4Servers()) {
            body.push_back(static_cast<unsigned char>(server.serverid));
            const auto* const addressBytes = reinterpret_cast<const unsigned char*>(&server.outer_addr);
            body.insert(body.end(), addressBytes, addressBytes + 4);
            for (int shift = 0; shift < 32; shift += 8) {
                body.push_back(static_cast<unsigned char>(server.port >> shift));
            }
            body.push_back(status == 1 ? 1 : 0);
            for (std::size_t index = 0; index < sizeof(server.name) && server.name[index] != '\0'; ++index) {
                body.push_back(static_cast<unsigned char>(server.name[index]));
                body.push_back(0);
            }
            body.insert(body.end(), {0, 0, 0});
            ++body[3];
        }
        Send("b", static_cast<std::uint32_t>(body.size()), body.data());
        return false;
    }
    if (g_ServerList.packetlen < 10) {
        // Log.AddLog(LOG_ERROR, "Server List Error, Check lin2DB Server Table")
        // L2_LOG_ERROR(Log, "Server List Error, Check lin2DB Server Table")
        L2_LOG_ERROR(Log,
            "[server list] Cannot serve the standard list to session %u, account %d: "
            "cached length %d is below 10 bytes. Requesting failure response 6.",
            m_sessionId, uid, g_ServerList.packetlen);
        Send("cc", 5, 6);
        return false;
    }

    std::memcpy(sendBuffer.data() + 3, g_ServerList.listpacket, static_cast<std::size_t>(g_ServerList.packetlen));
    const int packetSize = config.PacketSizeType + g_ServerList.packetlen;
    sendBuffer[0] = static_cast<char>(packetSize);
    sendBuffer[1] = static_cast<char>(packetSize >> 8);
    sendBuffer[2] = 4;
    Send(sendBuffer.data(), packetSize + 3 - config.PacketSizeType);
    return false;
}

// L2Authd.exe 0x0041F3D0, Src/clientsession.cpp:762-914.
bool CClientSession::ServerListExtPacket(const unsigned char* packet) {
    std::array<char, 8192> sendBuffer{};
    const int uid = GetIntFromPacket(&packet);
    GetIntFromPacket(&packet);
    const char listKind = GetCharFromPacket(&packet);

    if (listKind < 0 || listKind >= 8) {
        L2_LOG_WARN(Log,
            "[server list] Invalid list type %d requested by session %u, account %d. "
            "Requested failure response 6; the session remains open.",
            static_cast<int>(listKind), m_sessionId, uid);
        Send("cc", 5, 6);
        return false;
    }

    LoginUser* user = nullptr;
    if (!accountdb.FindAccount(uid, &user)) {
        L2_LOG_WARN(Log,
            "[server list] Extended-list request for session %u, account %d has no in-memory account. "
            "Requesting session closure without a response.",
            m_sessionId, uid);
        return true;
    }

    m_lastIO = static_cast<std::int32_t>(runtime::TickCount32());
    if (listKind == 6) {
        bool hasMorePages = false;
        int pageNumber = 0;
        do {
            char* packetEnd = sendBuffer.data() + 3;
            int packetLength = 0;
            if (!g_ServerList.m_listPacket6.GetPacketFrame(pageNumber, packetEnd, packetLength, hasMorePages)) {
                // Log.AddLog(LOG_ERROR, "Server List(6) Error")
                // L2_LOG_ERROR(Log, "Server List(6) Error")
                L2_LOG_ERROR(Log,
                    "[server list] Cannot obtain type 6 page %d for session %u, account %d. "
                    "Stopping the list response and requesting failure response 6.",
                    pageNumber, m_sessionId, uid);
                Send("cc", 5, 6);
                user->ReleaseRef();
                return false;
            }

            packetEnd += packetLength;
            if (!hasMorePages) {
                BufCat(&packetEnd, 8192, user->lastworld);
                BufCat(&packetEnd, 8192, user->user_data_len);
                BufCat(&packetEnd, 8192, user->user_data, user->user_data_len);
                packetLength += 3 + user->user_data_len;
            }

            const int packetSize = config.PacketSizeType + packetLength;
            sendBuffer[0] = static_cast<char>(packetSize);
            sendBuffer[1] = static_cast<char>(packetSize >> 8);
            sendBuffer[2] = 0x14;
            Send(sendBuffer.data(), packetSize + 3 - config.PacketSizeType);
            ++pageNumber;
        } while (hasMorePages);
        // SOURCE BUG: the format-6 success path at 0x41F71A leaks the
        // reference from FindAccount. Release it as the error path does.
        user->ReleaseRef();
        return false;
    }

    int packetLength = 0;
    if ((user->loginflag & 0x800U) == 0U) {
        if (g_ServerList.ServerListLen[static_cast<unsigned char>(listKind)] < 10) {
            // Log.AddLog(LOG_ERROR, "Server List Error, Check lin2DB Server Table")
            // L2_LOG_ERROR(Log, "Server List Error, Check lin2DB Server Table")
            L2_LOG_ERROR(Log,
                "[server list] Cannot serve list type %u to session %u, account %d: "
                "cached length %d is below 10 bytes. Requesting failure response 6.",
                static_cast<unsigned int>(static_cast<unsigned char>(listKind)), m_sessionId, uid,
                g_ServerList.ServerListLen[static_cast<unsigned char>(listKind)]);
            Send("cc", 5, 6);
            user->ReleaseRef();
            return false;
        }
        packetLength = g_ServerList.ServerListLen[static_cast<unsigned char>(listKind)];
        std::memcpy(
            sendBuffer.data() + 3, g_ServerList.ServerListPacketFrame[static_cast<unsigned char>(listKind)],
            static_cast<std::size_t>(packetLength)
        );
    } else {
        if (g_ServerList.RevivalServerListLen[static_cast<unsigned char>(listKind)] < 10) {
            // Log.AddLog(LOG_ERROR, "Revival Server List Error, Check lin2DB Server Table")
            // L2_LOG_ERROR(Log, "Revival Server List Error, Check lin2DB Server Table")
            L2_LOG_ERROR(Log,
                "[server list] Cannot serve revival list type %u to session %u, account %d: "
                "cached length %d is below 10 bytes. Requesting failure response 6.",
                static_cast<unsigned int>(static_cast<unsigned char>(listKind)), m_sessionId, uid,
                g_ServerList.RevivalServerListLen[static_cast<unsigned char>(listKind)]);
            Send("cc", 5, 6);
            user->ReleaseRef();
            return false;
        }
        packetLength = g_ServerList.RevivalServerListLen[static_cast<unsigned char>(listKind)];
        std::memcpy(
            sendBuffer.data() + 3, g_ServerList.RevivalServerListPacketFrame[static_cast<unsigned char>(listKind)],
            static_cast<std::size_t>(packetLength)
        );
    }

    sendBuffer[4] = user->lastworld > 0 ? user->lastworld : config.defaultTopServerId;
    if (listKind > 4) {
        if (config.PacketSizeType + packetLength + 2 + user->user_data_len > 8192) {
            L2_LOG_ERROR(Log,
                "[server list] Cannot append %d bytes of account data to list type %u for session %u, account %d: "
                "the resulting frame exceeds 8192 bytes. Requesting session closure without a response.",
                static_cast<int>(user->user_data_len),
                static_cast<unsigned int>(static_cast<unsigned char>(listKind)), m_sessionId, uid);
            user->ReleaseRef();
            return true;
        }
        std::memcpy(sendBuffer.data() + packetLength + config.PacketSizeType, &user->user_data_len, 2);
        packetLength += 2;
        std::memcpy(
            sendBuffer.data() + packetLength + config.PacketSizeType, user->user_data,
            static_cast<std::size_t>(user->user_data_len)
        );
        packetLength += user->user_data_len;
    }

    const int packetSize = config.PacketSizeType + packetLength;
    sendBuffer[0] = static_cast<char>(packetSize);
    sendBuffer[1] = static_cast<char>(packetSize >> 8);
    sendBuffer[2] = 4;
    Send(sendBuffer.data(), packetSize + 3 - config.PacketSizeType);
    user->ReleaseRef();
    return false;
}

// L2Authd.exe 0x0041FA80, Src/clientsession.cpp:918-942.
bool CClientSession::CheckSecurityCardPacket(const unsigned char* packet) {
    const int cardNumber = GetIntFromPacket(&packet);
    const int cardKey = GetIntFromPacket(&packet);
    PreLoginUser user;
    const unsigned char result = accountdb.CheckSecurityCardValue(cardNumber, cardKey, &user);
    const SecAuth::Enum securityCard = SecAuth::SECURITYCARD;
    if (result == 0) {
        user.account.securityUsage.SetPassed(securityCard);
        if (!accountdb.AuthenticateUserSecondary(this, user)) {
            return false;
        }
        return accountdb.AuthorizeUser(this, user);
    }

    user.account.securityUsage.SetFailed(securityCard);
    SendFail(1, static_cast<char>(result), nullptr);
    return false;
}

// L2Authd.exe 0x0041FC60, Src/clientsession.cpp:945-963. The response
// arguments are direct instruction facts from call site 0x0041FD09.
bool CClientSession::UpdateSessionPacket(const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    const int oneTimeKey = GetIntFromPacket(&packet);
    const int reconnectKey = GetIntFromPacket(&packet);
    const char result = accountdb.UpdateSession(uid, oneTimeKey, reconnectKey, this);
    if (result != 0) {
        Send("cddc", 12, uid, 0, result);
    }
    return false;
}

// L2Authd.exe 0x0041FD70, Src/clientsession.cpp:967-1006.
bool CClientSession::WebSessionLoginPacket(const unsigned char* packet) {
    m_lastIO = static_cast<std::int32_t>(runtime::TickCount32());
    runtime::raw_atomic::IncrementI32(&g_nTryLoginCnt);
    runtime::raw_atomic::IncrementI32(&reporter.m_WebLoginTry);

    WebSessionId sessionId{};
    WebToken token{};
    std::memcpy(&sessionId, packet, sizeof(sessionId));
    packet += sizeof(sessionId);
    std::memcpy(&token, packet, sizeof(token));
    packet += sizeof(token);
    const int subscription = GetIntFromPacket(&packet);
    const auto cdKind = static_cast<unsigned short>(GetShortFromPacket(&packet));
    const char clientInstances = GetCharFromPacket(&packet);
    return accountdb.LoginAccountWsid(this, sessionId, token, subscription, cdKind, clientInstances);
}

// L2Authd.exe 0x0041FED0, Src/clientsession.cpp:1010-1037.
bool CClientSession::OTPCheckPacket(const unsigned char* packet) {
    m_lastIO = static_cast<std::int32_t>(runtime::TickCount32());
    const int uid = GetIntFromPacket(&packet);
    const int answer = GetIntFromPacket(&packet);
    if (uid != m_preLoginUser.account.uid || m_preLoginUser.otpAnswer != -1) {
        L2_LOG_WARN(Log,
            "[OTP] Rejecting response for account %d on session %u: expected account %d, already answered %d. "
            "Requesting session closure.",
            uid, m_sessionId, m_preLoginUser.account.uid,
            m_preLoginUser.otpAnswer != -1 ? 1 : 0);
        return true;
    }

    m_preLoginUser.otpAnswer = answer;
    if (!accountdb.AuthenticateUserSecondary(this, m_preLoginUser)) {
        return false;
    }
    return accountdb.AuthorizeUser(this, m_preLoginUser);
}

// L2Authd.exe 0x00420030, Src/clientsession.cpp:1041-1075.
bool CClientSession::ExternalTokenLoginPacket(const unsigned char* packet) {
    std::array<unsigned char, 1024> token{};
    m_lastIO = static_cast<std::int32_t>(runtime::TickCount32());
    const int companyNo = GetIntFromPacket(&packet);
    std::memcpy(token.data(), packet, 64);
    packet += 64;
    const int subscription = GetIntFromPacket(&packet);
    const auto cdKind = static_cast<unsigned short>(GetShortFromPacket(&packet));
    const char clientInstances = GetCharFromPacket(&packet);
    const int gameId = config.gameId == 8 ? 0x20 : 0x1b;
    return accountdb.ExternalTokenLogin(
        this, companyNo, token.data(), config.affiliatedGameId, gameId, subscription, cdKind, clientInstances, m_addr
    );
}

// L2Authd.exe 0x00420240, Src/clientsession.cpp:1080-1094.
bool CClientSession::AuxAuthenticationAckPacket(const unsigned char* packet) {
    m_lastIO = static_cast<std::int32_t>(runtime::TickCount32());
    return PAServerMgr::Inst()->SendAuxAuthenticationAck(packet);
}

// L2Authd.exe 0x00420300, Src/clientsession.cpp:1099-1139.
bool CClientSession::RecvThridPartyLoginPacket(const unsigned char* packet) {
    std::array<unsigned char, 1024> token{};
    m_lastIO = static_cast<std::int32_t>(runtime::TickCount32());
    const int companyNo = GetIntFromPacket(&packet);
    std::memcpy(token.data(), packet, token.size());
    packet += token.size();
    GetIntFromPacket(&packet);
    const int subscription = GetIntFromPacket(&packet);
    const auto cdKind = static_cast<unsigned short>(GetShortFromPacket(&packet));
    const char clientInstances = GetCharFromPacket(&packet);
    const in_addr address = GetAddrFromPacket(&packet);
    const int gameId = config.gameId == 8 ? 0x20 : 0x1b;
    return accountdb.ThirdPartyTokenLogin(
        this, companyNo, token.data(), config.affiliatedGameId, gameId, subscription, cdKind, clientInstances, address
    );
}

// L2Authd.exe 0x00420550, Src/clientsession.cpp:1142-1192.
bool CClientSession::RecvIovationPacket(const unsigned char* packet) {
    const int uid = GetIntFromPacket(&packet);
    const char clientResult = GetCharFromPacket(&packet);
    const auto dataLength = static_cast<unsigned short>(GetShortFromPacket(&packet));

    if (uid != m_preLoginUser.account.uid || m_preLoginUser.iovationChecked) {
        // Log.AddLog(LOG_WARN, "duplicated Iovation result from client uid(%d)", uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=client_verification_rejected peer=iovation account_id=%d expected_account_id=%d already_checked=%d reason=account_mismatch_or_duplicate action=return_close_request",
        //     uid, m_preLoginUser.account.uid, m_preLoginUser.iovationChecked)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[iovation] Rejecting client verification for account %d: expected account %d, already checked %d. Requesting connection closure.",
        //     uid, m_preLoginUser.account.uid, m_preLoginUser.iovationChecked)
        L2_LOG_WARN(Log,
            "[iovation] Rejecting client verification for account %d: expected account %d, already checked %d. Requesting connection closure.",
            uid, m_preLoginUser.account.uid, m_preLoginUser.iovationChecked);
        return true;
    }

    m_preLoginUser.iovationChecked = true;
    if (clientResult != 0) {
        // Log.AddLog(LOG_WARN, "black box result fail from client uid(%d), result(%d)", uid, clientResult)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=client_verification_failed peer=iovation account_id=%d result_code=%u accept_failure=%d action=%s",
        //     uid, static_cast<unsigned int>(static_cast<unsigned char>(clientResult)),
        //     config.acceptClientGetIovationFail,
        //     config.acceptClientGetIovationFail ? "continue_authorization" : "send_failure_reply")
        // Log.AddLog(
        //     LOG_WARN,
        //     "[iovation] Client verification failed for account %d (result %u. AcceptClientGetIovationFail %d). %s",
        //     uid, static_cast<unsigned int>(static_cast<unsigned char>(clientResult)),
        //     config.acceptClientGetIovationFail,
        //     config.acceptClientGetIovationFail ? "Continuing authorization." : "Sending a failure reply.")
        L2_LOG_WARN(Log,
            "[iovation] Client verification failed for account %d (result %u; acceptClientGetIovationFail %d). %s",
            uid, static_cast<unsigned int>(static_cast<unsigned char>(clientResult)),
            config.acceptClientGetIovationFail,
            config.acceptClientGetIovationFail ? "Continuing authorization." : "Sending a failure reply.");
        if (!config.acceptClientGetIovationFail) {
            SendFail(1, 20, nullptr);
        } else {
            accountdb.AuthorizeUser(this, m_preLoginUser);
        }
        return false;
    }

    const char result = IovationServerMgr::Inst()->RequestCheckDetail(m_preLoginUser, this, dataLength, packet);
    if (result == 20) {
        // Log.AddLog(LOG_WARN, "Fail sending IovationProxy to CheckTransactionDetail(%d)", uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=verification_bypassed peer=iovation account_id=%d reason=proxy_request_not_submitted result_code=20 action=continue_authorization",
        //     uid)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[iovation] Could not submit proxy verification for account %d (result 20). Continuing authorization without that check.",
        //     uid)
        L2_LOG_WARN(Log,
            "[iovation] Could not submit proxy verification for account %d (result 20). Continuing authorization without that check.",
            uid);
        accountdb.AuthorizeUser(this, m_preLoginUser);
    } else if (result != 0) {
        SendFail(1, result, nullptr);
    }
    return false;
}

// L2Authd.exe 0x004206C0, Src/clientsession.cpp:192-207.
void CClientSession::CloseSessionActively() {
    const std::uint32_t sessionId = runtime::raw_atomic::ExchangeU32(&m_sessionId, kClosedSessionId);
    if (sessionId == kClosedSessionId) {
        return;
    }

    CSocketServerEx* const socket = serverEx.FindSocket(CIOServerEx::GetGateId(sessionId));
    if (socket != nullptr) {
        socket->SendCloseSession(sessionId);
        socket->ReleaseRef();
        m_lastIO = static_cast<std::int32_t>(runtime::TickCount32());
    }
    OnClose(sessionId);
    ReleaseRef();
}

// L2Authd.exe 0x00420740, Src/clientsession.cpp:210-216.
void CClientSession::CloseSessionPassively() {
    const std::uint32_t sessionId = runtime::raw_atomic::ExchangeU32(&m_sessionId, kClosedSessionId);
    if (sessionId != kClosedSessionId) {
        OnClose(sessionId);
        ReleaseRef();
    }
}

// L2Authd.exe 0x00420780, Src/clientsession.cpp:1197-1228.
bool CClientSession::RecvThridPartySteamLoginPacket(const unsigned char* packet) {
    std::array<unsigned char, 1024> token{};
    m_lastIO = static_cast<std::int32_t>(runtime::TickCount32());
    const int companyNo = GetIntFromPacket(&packet);
    std::memcpy(token.data(), packet, token.size());
    packet += token.size();
    GetIntFromPacket(&packet);
    const int subscription = GetIntFromPacket(&packet);
    const auto cdKind = static_cast<unsigned short>(GetShortFromPacket(&packet));
    const char clientInstances = GetCharFromPacket(&packet);
    const in_addr address = GetAddrFromPacket(&packet);
    const int gameId = config.gameId == 8 ? 0x20 : 0x1b;
    return accountdb.ExternalSteamTokenLogin(
        this, companyNo, token.data(), config.affiliatedGameId, gameId, subscription, cdKind, clientInstances, address
    );
}

// L2Authd.exe 0x004209D0, Src/clientsession.cpp:1232-1275.
bool CClientSession::RecvLoginTokenPacket(const unsigned char* packet) {
    std::array<char, 33> authProviderCode{};
    std::array<char, 2048> token{};
    m_lastIO = static_cast<std::int32_t>(runtime::TickCount32());
    GetIntFromPacket(&packet);
    GetStrFromPacket(&packet, static_cast<int>(authProviderCode.size()), authProviderCode.data());
    GetStrFromPacket(&packet, static_cast<int>(token.size()), token.data());
    const int subscription = GetIntFromPacket(&packet);
    const auto cdKind = static_cast<unsigned short>(GetShortFromPacket(&packet));
    const char clientInstances = GetCharFromPacket(&packet);
    const int gameId = config.gameId == 8 ? 0x20 : 0x1b;
    return accountdb.LoginToken(
        this, authProviderCode.data(), token.data(), config.affiliatedGameId, gameId, subscription, cdKind, clientInstances,
        m_addr
    );
}

// L2Authd.exe 0x00420C10, Src/clientsession.cpp:43-46.
int CClientSession::AddRef() {
    runtime::raw_atomic::IncrementI32(&m_refcount);
    return runtime::raw_atomic::LoadI32Relaxed(&m_refcount);
}

// L2Authd.exe 0x00420C30, Src/clientsession.cpp:49-52.
void CClientSession::ReleaseRef() {
    if (runtime::raw_atomic::DecrementI32(&m_refcount) == 0) {
        delete this;
    }
}

// L2Authd.exe 0x00420C80, Src/clientsession.cpp:70-75.
#if defined(_WIN32)
void __stdcall ClientSessionTimerCallback(
#else
void ClientSessionTimerCallback(
#endif
    void* parameter, [[maybe_unused]] std::uint8_t timerOrWaitFired
) {
    CClientSession* const session =
        serverEx.FindSession(static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(parameter)));
    if (session != nullptr) {
        session->OnTimerCallback();
    }
}
