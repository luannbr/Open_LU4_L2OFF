#include "IOServer.h"

#include "ClientSession.h"
#include "Config.h"
#include "DBConn.h"
#include "IOServerEx.h"
#include "Log.h"
#include "PacketServerEx.h"
#include "Thread.h"
#include "Util.h"

#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <new>

#if defined(_WIN32)
#  include <WinSock2.h>
#else
#  include <arpa/inet.h>
#endif

// L2Authd.exe 0x0042E5D0, Src/csocketmi.cpp:19-24.
CSocketMi::CSocketMi(std::uint32_t socket)
    : CIOSocket(socket) {
    m_socket = socket;
    addr.s_addr = 0;
    host = nullptr;
    mode = MSM_READ_LEN;
}

// L2Authd.exe 0x0042E620, Src/csocketmi.cpp:27-28.
CSocketMi::~CSocketMi() = default;

// L2Authd.exe 0x0042E640, Src/csocketmi.cpp:31-33.
CSocketMi* CSocketMi::Allocate(std::uint32_t socket) {
    void* const storage =
        MemoryObject::operator new(sizeof(CSocketMi));
    return storage == nullptr
        ? nullptr
        : ::new (storage) CSocketMi(socket);
}

// L2Authd.exe 0x0042E6C0, Src/csocketmi.cpp:125-128.
void CSocketMi::OnCreate() {
    AddRef();
    OnRead();
}

// L2Authd.exe 0x0042E6E0, Src/csocketmi.cpp:41-88.
void CSocketMi::OnRead() {
    std::uint32_t consumed = 0;
    const std::uint32_t available = m_pReadBuf->m_size;
    unsigned char* const packet =
        reinterpret_cast<unsigned char*>(m_pReadBuf->m_buffer);

    if (mode == MSM_CLOSE) {
        CloseSocket();
        return;
    }

    while (consumed < available) {
        if (mode == MSM_READ_LEN) {
            if (available < consumed + 5U) {
                Read(available - consumed);
                return;
            }
            packetLen =
                static_cast<std::int32_t>(packet[consumed]) - 4 +
                static_cast<std::int32_t>(packet[consumed + 1]) * 256;
            if (packetLen < 1 || packetLen > 8192) {
                // Log.AddLog(
                //     LOG_ERROR,
                //     "%d: bad packet size %d",
                //     m_socket,
                //     packetLen)
                // Log.AddLog(
                //     LOG_ERROR,
                //     "component=network event=invalid_packet_length peer=monitor socket=%u payload_bytes=%d min_bytes=1 max_bytes=8192 action=close_connection",
                //     m_socket, packetLen)
                // Log.AddLog(
                //     LOG_ERROR,
                //     "[monitor] Closing socket %u: invalid payload length %d bytes. Expected 1..8192.",
                //     m_socket, packetLen)
                L2_LOG_ERROR(Log,
                    "[monitor] Closing socket %u: invalid payload length %d bytes; expected 1..8192.",
                    m_socket, packetLen);
                CloseSocket();
                return;
            }
            consumed += 4;
            mode = MSM_READ_BODY;
        } else {
            if (mode != MSM_READ_BODY) {
                L2_LOG_ERROR(Log,
                    "[monitor] Closing socket %u: invalid read state %d.",
                    m_socket, static_cast<int>(mode));
                CloseSocket();
                return;
            }
            if (available <
                consumed + static_cast<std::uint32_t>(packetLen)) {
                Read(available - consumed);
                return;
            }
            if ((config.usePacketLog & 0x40) != 0) {
                WritePacketLog(
                    "Monitor->Auth",
                    m_hSocket,
                    packet + consumed,
                    packetLen,
                    0x400);
            }
            ProcessMonitorPacket(
                this,
                packet + consumed);
            consumed += static_cast<std::uint32_t>(packetLen);
            mode = MSM_READ_LEN;
        }
    }
    Read(0);
}

// L2Authd.exe 0x0042E880, Src/csocketmi.cpp:35-38.
void CSocketMi::OnClose() {
    mode = MSM_CLOSE;
    const std::uint32_t peerAddress = ntohl(addr.s_addr);
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "*close connection from %s, %x(%x)",
    //     IP(),
    //     m_socket,
    //     runtime::PointerLow32(this))
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "component=network event=socket_closed peer=monitor address=%u.%u.%u.%u socket=%u reason=unspecified",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     m_socket)
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "[monitor] Connection to %u.%u.%u.%u closed (socket %u). Close reason is unavailable.",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     m_socket)
    // Noise: a generic absence-of-cause sentence adds nothing to the close event.
    // L2_LOG_INFO(Log,
    //     "[monitor] Connection to %u.%u.%u.%u closed (socket %u). Close reason is unavailable.",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     m_socket)
    L2_LOG_INFO(Log,
        "[monitor] Connection to %u.%u.%u.%u closed (socket %u).",
        (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        (peerAddress >> 8U) & 255U, peerAddress & 255U,
        m_socket);
}

// L2Authd.exe 0x0042E8C0, Src/csocketmi.cpp:131-133.
void CSocketMi::SetAddress(in_addr address) {
    addr = address;
}

// L2Authd.exe 0x0042E8E0, Src/csocketmi.cpp:136-160.
void CSocketMi::Send(const char* format, ...) {
    if (mode == MSM_CLOSE) {
        return;
    }

    CIOBuffer* const buffer = CIOBuffer::Alloc();
    std::va_list arguments;
    va_start(arguments, format);
    const int payloadSize = Assemble(
        buffer->m_buffer + 4,
        8192 - 4,
        format,
        arguments);
    va_end(arguments);

    if (payloadSize == 0) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "%d: assemble too large packet. format %s",
        //     m_socket,
        //     format)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=network event=packet_assembly_failed peer=monitor socket=%u capacity_bytes=8188 action=submit_four_byte_buffer header=not_initialized",
        //     m_socket)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[monitor] Could not assemble a reply on socket %u within 8188 bytes. The existing path still submits a four-byte buffer with an uninitialized header.",
        //     m_socket)
        L2_LOG_ERROR(Log,
            "[monitor] Could not assemble a reply on socket %u within 8188 bytes. The existing path still submits a four-byte buffer with an uninitialized header.",
            m_socket);
    } else {
        const int outerSize = payloadSize + 4;
        const int innerSize = payloadSize + 2;
        buffer->m_buffer[0] = static_cast<char>(outerSize);
        buffer->m_buffer[1] = static_cast<char>(outerSize >> 8);
        buffer->m_buffer[2] = static_cast<char>(innerSize);
        buffer->m_buffer[3] = static_cast<char>(innerSize >> 8);
        if ((config.usePacketLog & 0x40) != 0) {
            WritePacketLog(
                "Auth->Monitor",
                m_hSocket,
                reinterpret_cast<const unsigned char*>(
                    buffer->m_buffer + 4),
                payloadSize,
                0x400);
        }
    }
    buffer->m_size =
        static_cast<std::uint32_t>(payloadSize + 4);
    Write(buffer);
}

// L2Authd.exe 0x0042EA10, Src/csocketmi.cpp:120-122.
const char* CSocketMi::IP() {
    return ::inet_ntoa(addr);
}

// L2Authd.exe 0x0042EA30, Src/csocketmi.cpp:91-117. The complete 40-argument
// monitor reply is recovered from the push sequence at 0x0042EA8F-0x0042EB32.
// Ghidra's variadic decompilation kept only the first two arguments.
void CSocketMi::ProcessMonitorPacket(
    CSocketMi* socket,
    const unsigned char* packet) {
    const char command = GetCharFromPacket(&packet);
    const std::int32_t request = GetIntFromPacket(&packet);
    if (command != 1) {
        return;
    }

    const bool ioCapacityAvailable =
        (g_nRunningThread * 100 / config.numServerThread) <= 50;
    socket->Send(
        "cdcbbddbbddbbdhhbbddbbddbbdd",
        command,
        request,
        6,
        4,
        "IACP",
        2,
        "LI",
        0,
        ioCapacityAvailable,
        4,
        "IA11",
        2,
        "DI",
        0,
        CClientSession::GetTryLoginCnt(),
        4,
        "IA02",
        2,
        "DI",
        0,
        static_cast<short>(g_nRunningThread),
        static_cast<short>(config.numServerThread),
        4,
        "IA03",
        2,
        "DI",
        0,
        CDBConn::GetAvgTimeCost(),
        4,
        "IA04",
        2,
        "DI",
        0,
        CPacketServer::g_nPendingPacket,
        4,
        "IA14",
        2,
        "DI",
        0,
        CPacketServerEx::g_nPendingPacket);
}
