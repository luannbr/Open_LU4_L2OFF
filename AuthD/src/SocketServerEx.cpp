#include "SocketServerEx.h"
#include "Shared/src/ScopedLockGuards.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/TickCount32.h"
#include "AccountDB.h"
#include "ClientSession.h"
#include "Config.h"
#include "IOServer.h"
#include "IOServerEx.h"
#include "IPAccessLimit.h"
#include "Log.h"
#include "PacketServerEx.h"
#include "Thread.h"
#include "Util.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <utility>

#if defined(_WIN32)
#  include <WinSock2.h>
#else
#  include <arpa/inet.h>
#endif

AccountDB accountdb;

namespace {

const unsigned char* AddressOctets(
    const in_addr& address) noexcept {
    return reinterpret_cast<const unsigned char*>(
        &address.s_addr);
}

} // namespace

// L2Authd.exe 0x004872E0, Src/socketserverex.cpp:33-64.
int SendSocket(
    in_addr address,
    const char* format,
    ...) {
    CSocketServer* const socket =
        server.FindSocket(address);
    if (socket == nullptr) {
        return 0;
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

    if (packetSize == 0) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "%d: assemble too large packet. format %s",
        //     socket->m_hSocket,
        //     format)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=network event=packet_assembly_failed peer=world socket=%u capacity_bytes=8190 packet_size_type=%d action=submit_legacy_buffer buffer_size_rule=3_minus_packet_size_type legacy_return=success",
        //     socket->m_hSocket, config.PacketSizeType)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[world] Could not assemble a packet on socket %u within 8190 bytes (PacketSizeType %d). The existing path still submits a buffer of 3 minus PacketSizeType bytes and reports success.",
        //     socket->m_hSocket, config.PacketSizeType)
        L2_LOG_ERROR(Log,
            "[world] Could not assemble a packet on socket %u within 8190 bytes (PacketSizeType %d). The existing path still submits a buffer of 3 minus PacketSizeType bytes and reports success.",
            socket->m_hSocket, config.PacketSizeType);
    } else {
        packetSize =
            packetSize - 1 + config.PacketSizeType;
        buffer->m_buffer[0] =
            static_cast<char>(packetSize);
        buffer->m_buffer[1] =
            static_cast<char>(packetSize >> 8);
        if (config.usePacketLog != 0) {
            char direction[64];
            std::snprintf(
                direction,
                sizeof(direction),
                "%s",
                "Auth->World");
            WritePacketLog(
                direction,
                socket->m_hSocket,
                reinterpret_cast<const unsigned char*>(
                    buffer->m_buffer + 2),
                packetSize - 2,
                0x400);
        }
    }

    buffer->m_size = static_cast<std::uint32_t>(
        packetSize + 3 - config.PacketSizeType);
    socket->Write(buffer);
    socket->ReleaseRef();
    return 1;
}

// L2Authd.exe 0x00487440, Src/socketserverex.cpp:68-80.
CSocketServerEx::CSocketServerEx(
    unsigned int socket,
    unsigned char gateId)
    : CIOSocket(socket),
      m_lock(eCriticalSection, 0) {
    AddRef();
    m_socket = socket;
    m_addr.s_addr = 0;
    m_mode = SM_READ_LEN;
    m_GateId = gateId;
    m_iPendingPacket = 0;
    m_lastIO = static_cast<std::int32_t>(
        runtime::TickCount32());
}

// L2Authd.exe 0x00487520, Src/socketserverex.cpp:83-84.
CSocketServerEx::~CSocketServerEx() = default;

// L2Authd.exe 0x00487590, Src/socketserverex.cpp:87-89.
CSocketServerEx* CSocketServerEx::Allocate(
    unsigned int socket,
    unsigned char gateId) {
    return new CSocketServerEx(socket, gateId);
}

// L2Authd.exe 0x00487610, Src/socketserverex.cpp:107-110.
void CSocketServerEx::OnCreate() {
    SendProcotolVersion();
    OnRead();
}

// L2Authd.exe 0x00487630, Src/socketserverex.cpp:113-234.
void CSocketServerEx::OnRead() {
    int consumed = 0;
    const int available =
        static_cast<int>(m_pReadBuf->m_size);
    auto* const packet =
        reinterpret_cast<unsigned char*>(
            m_pReadBuf->m_buffer);

    if (m_mode == SM_CLOSE) {
        CloseSocket();
        return;
    }

    while (consumed < available) {
        if (m_mode == SM_READ_LEN) {
            const unsigned char gateOpcode =
                packet[consumed];
            if (gateOpcode == 0) {
                if (available < consumed + 9) {
                    Read(static_cast<std::uint32_t>(
                        available - consumed));
                    return;
                }
                m_opcode = static_cast<char>(gateOpcode);
                std::memcpy(
                    &m_sessionSock,
                    packet + consumed + 1,
                    sizeof(m_sessionSock));
                std::memcpy(
                    &m_sessionAddr.s_addr,
                    packet + consumed + 5,
                    sizeof(m_sessionAddr.s_addr));
                CreateSession(
                    m_sessionSock,
                    m_sessionAddr);
                consumed += 9;
                continue;
            }

            if (gateOpcode == 1) {
                if (available < consumed + 5) {
                    Read(static_cast<std::uint32_t>(
                        available - consumed));
                    return;
                }
                m_opcode = static_cast<char>(gateOpcode);
                std::memcpy(
                    &m_sessionSock,
                    packet + consumed + 1,
                    sizeof(m_sessionSock));
                const unsigned int sessionId =
                    CIOServerEx::GetSessionId(
                        static_cast<char>(m_GateId),
                        m_sessionSock);
                CloseSessionPassively(sessionId);
                consumed += 5;
                continue;
            }

            if (gateOpcode == 2) {
                if (available < consumed + 7) {
                    Read(static_cast<std::uint32_t>(
                        available - consumed));
                    return;
                }
                m_opcode = static_cast<char>(gateOpcode);
                std::memcpy(
                    &m_sessionSock,
                    packet + consumed + 1,
                    sizeof(m_sessionSock));
                m_packetLen =
                    static_cast<int>(packet[consumed + 5]) - 2 +
                    static_cast<int>(packet[consumed + 6]) * 256;
                if (m_packetLen < 1 ||
                    m_packetLen > 8192) {
                    // Log.AddLog(
                    //     LOG_ERROR,
                    //     "%d: bad packet size %d",
                    //     m_socket,
                    //     m_packetLen)
                    // Log.AddLog(
                    //     LOG_WARN,
                    //     "component=network event=invalid_packet_length peer=authgated socket=%u gate_id=%u payload_bytes=%d min_bytes=1 max_bytes=8192 action=close_connection",
                    //     m_socket, static_cast<unsigned int>(m_GateId), m_packetLen)
                    // Log.AddLog(
                    //     LOG_WARN,
                    //     "[gateway] Invalid packet on socket %u, gateway %u: body length %d is outside 1..8192 bytes. Closing the connection.",
                    //     m_socket, static_cast<unsigned int>(m_GateId), m_packetLen)
                    L2_LOG_WARN(Log,
                        "[gateway] Invalid packet on socket %u, gateway %u: body length %d is outside 1..8192 bytes. Closing the connection.",
                        m_socket, static_cast<unsigned int>(m_GateId), m_packetLen);
                    CloseSocket();
                    return;
                }
                consumed += 7;
                m_mode = SM_READ_BODY;
                continue;
            }

            // Log.AddLog(
            //     LOG_ERROR,
            //     "%d: bad packet. Client is not authGateD",
            //     m_socket)
            // Log.AddLog(
            //     LOG_WARN,
            //     "component=network event=invalid_opcode peer=authgated layer=gate socket=%u gate_id=%u opcode=%u max_opcode=2 action=close_connection",
            //     m_socket, static_cast<unsigned int>(m_GateId), static_cast<unsigned int>(gateOpcode))
            // Log.AddLog(
            //     LOG_WARN,
            //     "[gateway] Invalid gateway opcode on socket %u, gateway %u: received %u, maximum 2. Closing the connection.",
            //     m_socket, static_cast<unsigned int>(m_GateId), static_cast<unsigned int>(gateOpcode))
            L2_LOG_WARN(Log,
                "[gateway] Invalid gateway opcode on socket %u, gateway %u: received %u, maximum 2. Closing the connection.",
                m_socket, static_cast<unsigned int>(m_GateId), static_cast<unsigned int>(gateOpcode));
            CloseSocket();
            return;
        }

        if (m_mode == SM_READ_BODY) {
            if (available < consumed + m_packetLen) {
                Read(static_cast<std::uint32_t>(
                    available - consumed));
                return;
            }
            if ((config.usePacketLog & 2) != 0) {
                WritePacketLog(
                    "Gate->Auth",
                    m_hSocket,
                    packet + consumed,
                    m_packetLen,
                    0x400);
            }
            if (packet[consumed] >= 0x15) {
                // Log.AddLog(
                //     LOG_ERROR,
                //     "unknown protocol %d",
                //     static_cast<int>(packet[consumed]))
                // Log.AddLog(
                //     LOG_WARN,
                //     "component=network event=invalid_opcode peer=authgated layer=client socket=%u gate_id=%u opcode=%u max_opcode=20 action=close_connection",
                //     m_socket, static_cast<unsigned int>(m_GateId), static_cast<unsigned int>(packet[consumed]))
                // Log.AddLog(
                //     LOG_WARN,
                //     "[gateway] Invalid client opcode on socket %u, gateway %u: received %u, maximum 20. Closing the connection.",
                //     m_socket, static_cast<unsigned int>(m_GateId), static_cast<unsigned int>(packet[consumed]))
                L2_LOG_WARN(Log,
                    "[gateway] Invalid client opcode on socket %u, gateway %u: received %u, maximum 20. Closing the connection.",
                    m_socket, static_cast<unsigned int>(m_GateId), static_cast<unsigned int>(packet[consumed]));
                CloseSocket();
                return;
            }

            CPacketServerEx* const packetJob =
                CPacketServerEx::Alloc();
            runtime::raw_atomic::IncrementI32(
                &m_iPendingPacket);
            packetJob->Init(
                this,
                m_pReadBuf,
                m_sessionSock);
            AddRef();
            m_pReadBuf->AddRef();
            runtime::raw_atomic::IncrementI32(
                &CPacketServerEx::g_nPendingPacket);
            // packetJob->PostObject(
            //     consumed,
            //     g_hIOCompletionPort)
            const auto posted = packetJob->PostObject(
                consumed,
                g_hIOCompletionPort);
            if (!posted) {
                // Log.AddLog(LOG_ERROR,
                //     "component=network event=packet_dispatch_failed peer=authgated socket=%u gate_id=%u action=continue_reading references=retained",
                //     m_socket, static_cast<unsigned int>(m_GateId))
                // Log.AddLog(LOG_ERROR,
                //     "[gateway] Packet dispatch threw on socket %u, gateway %u. Reading continues. Acquired references are retained.",
                //     m_socket, static_cast<unsigned int>(m_GateId))
                // L2_LOG_ERROR(Log,
                //     "[gateway] Packet dispatch threw on socket %u, gateway %u. Reading continues. Acquired references are retained.",
                //     m_socket, static_cast<unsigned int>(m_GateId))
                L2_LOG_ERROR(Log,
                    "[gateway] Could not queue client opcode %u from socket %u, gateway %u for processing. Reading continues; acquired references are retained.",
                    static_cast<unsigned int>(packet[consumed]),
                    m_socket, static_cast<unsigned int>(m_GateId));
            }
            consumed += m_packetLen;
            m_mode = SM_READ_LEN;
            m_lastIO = static_cast<std::int32_t>(
                runtime::TickCount32());
            continue;
        }

        if (m_mode != SM_CLOSE) {
            L2_LOG_ERROR(Log,
                "[gateway] Invalid read state %d on socket %u, gateway %u. Closing the connection.",
                static_cast<int>(m_mode), m_socket, static_cast<unsigned int>(m_GateId));
        }
        CloseSocket();
        return;
    }

    Read(0);
}

// L2Authd.exe 0x004879C0, Src/socketserverex.cpp:92-99.
void CSocketServerEx::OnClose() {
    m_mode = SM_CLOSE;
    serverEx.RemoveSocket(m_GateId);
    RemoveSessionAll();
    const std::uint32_t peerAddress = ntohl(m_addr.s_addr);
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "*close connection authgated from %s, %x(%x)",
    //     IP(),
    //     m_socket,
    //     runtime::PointerLow32(this))
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "component=network event=socket_closed peer=authgated address=%u.%u.%u.%u socket=%u gate_id=%u reason=unspecified action=remove_gate_and_client_sessions",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     m_socket, static_cast<unsigned int>(m_GateId))
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "[gateway] Connection from %u.%u.%u.%u closed on socket %u, gateway %u. Cause not provided. Removing the gateway and its client sessions.",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     m_socket, static_cast<unsigned int>(m_GateId))
    L2_LOG_INFO(Log,
        // Noise: The generic absence-of-cause phrase adds no diagnostic context.
        // "[gateway] Connection from %u.%u.%u.%u closed on socket %u, gateway %u. Cause not provided. Removing the gateway and its client sessions.",
        "[gateway] Connection from %u.%u.%u.%u closed on socket %u, gateway %u. Removing the gateway and its client sessions.",
        (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        (peerAddress >> 8U) & 255U, peerAddress & 255U,
        m_socket, static_cast<unsigned int>(m_GateId));
}

// L2Authd.exe 0x00487A20, Src/socketserverex.cpp:237-239.
void CSocketServerEx::SetAddress(
    in_addr address) {
    m_addr = address;
}

// L2Authd.exe 0x00487A40, Src/socketserverex.cpp:102-104.
char* CSocketServerEx::IP() {
    return ::inet_ntoa(m_addr);
}

// L2Authd.exe 0x00487A60, Src/socketserverex.cpp:343-354.
void CSocketServerEx::SendCloseSession(
    unsigned int sessionId) {
    CIOBuffer* const buffer = CIOBuffer::Alloc();
    buffer->m_size = static_cast<std::uint32_t>(
        Assemble(
            buffer->m_buffer,
            8192,
            "cd",
            1,
            CIOServerEx::GetSocketId(sessionId)));
    Write(buffer);
}

// L2Authd.exe 0x00487AC0, Src/socketserverex.cpp:357-368.
void CSocketServerEx::SendProcotolVersion() {
    CIOBuffer* const buffer = CIOBuffer::Alloc();
    buffer->m_size = static_cast<std::uint32_t>(
        Assemble(
            buffer->m_buffer,
            8192,
            "cd",
            3,
            config.ProtocolVer));
    Write(buffer);
}

// L2Authd.exe 0x00487B20, Src/socketserverex.cpp:242-258.
void CSocketServerEx::CloseSessionPassively(
    unsigned int sessionId) {
    CClientSession* session = nullptr;
    {
        L2_GUARD(m_lock);
        const auto found = m_cltSessionMap.find(sessionId);
        if (found != m_cltSessionMap.end()) {
            session = found->second;
            session->AddRef();
        }
    }

    if (session != nullptr) {
        session->CloseSessionPassively();
        session->ReleaseRef();
    }
}

// L2Authd.exe 0x00487BC0, Src/socketserverex.cpp:261-267.
bool CSocketServerEx::RemoveSession(
    unsigned int sessionId) {
    {
        L2_GUARD(m_lock);
        const auto found = m_cltSessionMap.find(sessionId);
        if (found != m_cltSessionMap.end()) {
            m_cltSessionMap.erase(sessionId);
        }
    }
    return true;
}

// L2Authd.exe 0x00487C40, Src/socketserverex.cpp:270-291.
void CSocketServerEx::RemoveSessionAll() {
    std::map<unsigned int, CClientSession*> sessions;

    {
        L2_GUARD(m_lock);
        sessions = m_cltSessionMap;
        for (const auto& entry : m_cltSessionMap) {
            entry.second->AddRef();
        }
        m_cltSessionMap.clear();
    }

    if (!sessions.empty()) {
        // Log.AddLog(LOG_NORMAL,
        //     "component=authentication event=gate_sessions_closing peer=authgated gate_id=%u clients=%zu action=close_client_sessions",
        //     static_cast<unsigned int>(m_GateId), sessions.size())
        // Log.AddLog(LOG_NORMAL,
        //     "[gateway] Closing client sessions for gateway %u (%zu clients).",
        //     static_cast<unsigned int>(m_GateId), sessions.size())
        // L2_LOG_INFO(Log,
        //     "[gateway] Closing client sessions for gateway %u (%zu clients).",
        //     static_cast<unsigned int>(m_GateId), sessions.size())
        L2_LOG_INFO(Log,
            "[gateway] Closing client sessions for gateway %u (%llu clients).",
            static_cast<unsigned int>(m_GateId),
            static_cast<unsigned long long>(sessions.size()));
    }
    for (const auto& entry : sessions) {
        entry.second->CloseSessionPassively();
        entry.second->ReleaseRef();
    }
}

// L2Authd.exe 0x00487DA0, Src/socketserverex.cpp:294-327.
CClientSession* CSocketServerEx::CreateSession(
    unsigned int sessionSocket,
    in_addr address) {
    const unsigned int sessionId =
        CIOServerEx::GetSessionId(
            static_cast<char>(m_GateId),
            sessionSocket);
    if (!IPaccessLimit.SetAccessIP(address)) {
        SendCloseSession(sessionId);
        const unsigned char* const octets =
            AddressOctets(address);
        // Log.AddLog(
        //     LOG_WARN,
        //     "AccessLimit Expire,%d.%d.%d.%d",
        //     static_cast<int>(octets[0]),
        //     static_cast<int>(octets[1]),
        //     static_cast<int>(octets[2]),
        //     static_cast<int>(octets[3]))
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "component=authentication event=client_connection_rejected peer=authgated gate_id=%u client_socket=%u address=%u.%u.%u.%u reason=ip_connection_limit limit=%d action=request_client_close",
        //     static_cast<unsigned int>(m_GateId), sessionSocket,
        //     static_cast<unsigned int>(octets[0]), static_cast<unsigned int>(octets[1]),
        //     static_cast<unsigned int>(octets[2]), static_cast<unsigned int>(octets[3]),
        //     config.IPAccessLimit)
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "[gateway] Gateway %u: rejecting client socket %u from %u.%u.%u.%u because IP connection limit %d was reached. Requesting client closure.",
        //     static_cast<unsigned int>(m_GateId), sessionSocket,
        //     static_cast<unsigned int>(octets[0]), static_cast<unsigned int>(octets[1]),
        //     static_cast<unsigned int>(octets[2]), static_cast<unsigned int>(octets[3]),
        //     config.IPAccessLimit)
        L2_LOG_INFO(Log,
            "[gateway] Gateway %u: rejecting client socket %u from %u.%u.%u.%u because IP connection limit %d was reached. Requesting client closure.",
            static_cast<unsigned int>(m_GateId), sessionSocket,
            static_cast<unsigned int>(octets[0]), static_cast<unsigned int>(octets[1]),
            static_cast<unsigned int>(octets[2]), static_cast<unsigned int>(octets[3]),
            config.IPAccessLimit);
        return nullptr;
    }

    CClientSession* session =
        CClientSession::Allocate(sessionId);
    if (session == nullptr) {
        return nullptr;
    }

    bool inserted = false;
    {
        L2_GUARD(m_lock);
        inserted =
            m_cltSessionMap.insert(
                std::make_pair(sessionId, session)).second;
    }

    if (!inserted) {
        delete session;
        session = nullptr;
        const unsigned char* const octets =
            AddressOctets(address);
        // Log.AddLog(
        //     LOG_WARN,
        //     "Gate Session duplicated.. %d %d.%d.%d.%d",
        //     sessionId,
        //     static_cast<int>(octets[0]),
        //     static_cast<int>(octets[1]),
        //     static_cast<int>(octets[2]),
        //     static_cast<int>(octets[3]))
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=authentication event=client_connection_rejected peer=authgated gate_id=%u client_socket=%u address=%u.%u.%u.%u reason=duplicate_session action=discard_new_session",
        //     static_cast<unsigned int>(m_GateId), sessionSocket,
        //     static_cast<unsigned int>(octets[0]), static_cast<unsigned int>(octets[1]),
        //     static_cast<unsigned int>(octets[2]), static_cast<unsigned int>(octets[3]))
        // Log.AddLog(
        //     LOG_WARN,
        //     "[gateway] Duplicate session through gateway %u on client socket %u from %u.%u.%u.%u. Discarding the new session.",
        //     static_cast<unsigned int>(m_GateId), sessionSocket,
        //     static_cast<unsigned int>(octets[0]), static_cast<unsigned int>(octets[1]),
        //     static_cast<unsigned int>(octets[2]), static_cast<unsigned int>(octets[3]))
        L2_LOG_WARN(Log,
            "[gateway] Duplicate session through gateway %u on client socket %u from %u.%u.%u.%u. Discarding the new session.",
            static_cast<unsigned int>(m_GateId), sessionSocket,
            static_cast<unsigned int>(octets[0]), static_cast<unsigned int>(octets[1]),
            static_cast<unsigned int>(octets[2]), static_cast<unsigned int>(octets[3]));
    } else {
        session->SetAddress(address);
        session->OnCreate();
    }
    return session;
}

// L2Authd.exe 0x00487F00, Src/socketserverex.cpp:330-340.
CClientSession* CSocketServerEx::FindSession(
    unsigned int sessionId) {
    CClientSession* session = nullptr;
    {
        L2_GUARD(m_lock);
        const auto found = m_cltSessionMap.find(sessionId);
        if (found != m_cltSessionMap.end()) {
            session = found->second;
            session->AddRef();
        }
    }
    return session;
}
