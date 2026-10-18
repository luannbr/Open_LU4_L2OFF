#include "AuthSocket.h"

#include "AuthGateConfig.h"
#include "AuthGateSvcMgr.h"
#include "AuthPacket.h"
#include "ClientSocketContainer.h"
#include "FileLog.h"
#include "StateMonMgr.h"
#include "WindowMgr.h"
#include "GlobalFunc.h"

#include "Shared/src/Runtime/LittleEndian.h"
#include "Shared/src/Runtime/TickCount32.h"

#include <atomic>
#include <cstdint>
#include <cstring>

CAuthSocket* CAuthSocket::g_pAuthSocket = nullptr;

// AuthGateD.exe 0x00405C40, AuthSocket.cpp.
CAuthSocket::CAuthSocket() {
    AddRef();
    if (!g_pAuthSocket) {
        g_pAuthSocket = this;
    }
    m_mode = SM_READ_LEN;
}

// AuthGateD.exe 0x00405CB0, AuthSocket.cpp.
CAuthSocket::~CAuthSocket() {
    g_pAuthSocket = nullptr;
    m_iLastIO = 0;
    m_mode = SM_READ_LEN;
}

// AuthGateD.exe 0x00405CD0, AuthSocket.cpp.
void CAuthSocket::OnCreate() {
    char16_t address[20]{};
    // CFileLog::g_pDefaultFileLog->LogT(LOG_NORMAL, u"*new authd connection from %s, %x(%x)", InetToStr(&m_addr, address, 20),
    //                                   m_hSocket, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(this)))
    // CFileLog::g_pDefaultFileLog->LogT(LOG_NORMAL,
    //     u"component=authd event=connected peer=%s port=%d socket=%u",
    //     InetToStr(&m_addr, address, 20), m_port, m_hSocket)
    // CFileLog::g_pDefaultFileLog->LogT(LOG_NORMAL,
    //     u"[authd] Connected to %s:%d on socket %u.",
    //     InetToStr(&m_addr, address, 20), m_port, m_hSocket)
    L2_LOG_INFO(*CFileLog::g_pDefaultFileLog,
        u"[authd] Connected to %s:%d on socket %u.",
        InetToStr(&m_addr, address, 20), m_port, m_hSocket);
    m_iLastIO = 0;
    m_mode = SM_READ_LEN;
    OnRead();
}

// AuthGateD.exe 0x00405D40, AuthSocket.cpp.
void CAuthSocket::OnRead() {
    CIOBuffer* const readBuffer = m_pReadBuf;
    const int available = static_cast<int>(readBuffer->m_size);
    int offset = 0;

    if (m_mode == SM_CLOSE) {
        CloseSocket();
        return;
    }

    while (offset < available) {
        if (m_mode == SM_READ_LEN) {
            const auto opcode = static_cast<unsigned char>(readBuffer->m_buffer[offset]);
            if (opcode == 3) {
                if (offset + 5 > available) {
                    Read(static_cast<std::uint32_t>(available - offset));
                    return;
                }
                m_protocolVer = runtime::LoadLittleEndianInt32(readBuffer->m_buffer + offset + 1);
                offset += 5;
                continue;
            }
            if (opcode == 1) {
                if (offset + 5 > available) {
                    Read(static_cast<std::uint32_t>(available - offset));
                    return;
                }
                m_opcode = 1;
                m_sessionId = runtime::LoadLittleEndianInt32(readBuffer->m_buffer + offset + 1);
                CClientSocketContainer::g_pClientSocketContainer->CloseSocketByAuthD(m_sessionId);
                offset += 5;
                continue;
            }
            if (opcode == 2) {
                if (offset + 7 > available) {
                    Read(static_cast<std::uint32_t>(available - offset));
                    return;
                }
                m_opcode = 2;
                m_sessionId = runtime::LoadLittleEndianInt32(readBuffer->m_buffer + offset + 1);
                m_packetLen = static_cast<std::int32_t>(runtime::LoadLittleEndian16(readBuffer->m_buffer + offset + 5)) - 2;
                if (m_packetLen < 1 || m_packetLen > 0x1ffb) {
                    // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR, u"%d: bad packet size %d", m_hSocket, m_packetLen)
                    // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
                    //     u"component=authd event=invalid_packet_length socket=%u session=%d body_bytes=%d min_bytes=1 max_bytes=8187 action=close_connection",
                    //     m_hSocket, m_sessionId, m_packetLen)
                    // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
                    //     u"[authd] Invalid packet on socket %u, session %d: body length %d is outside 1..8187 bytes. Closing the connection.",
                    //     m_hSocket, m_sessionId, m_packetLen)
                    L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
                        u"[authd] Invalid packet on socket %u, session %d: body length %d is outside 1..8187 bytes. Closing the connection.",
                        m_hSocket, m_sessionId, m_packetLen);
                    CloseSocket();
                    return;
                }
                offset += 7;
                m_mode = SM_READ_BODY;
                continue;
            }

            // The shipping x86 loop leaves offset unchanged for an unknown
            // carrier opcode and spins forever. Closing is the portable
            // liveness correction. No accepted protocol behavior changes.
            // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
            //     u"component=authd event=unknown_carrier_opcode socket=%u opcode=%u action=close_connection",
            //     m_hSocket, static_cast<unsigned int>(opcode))
            // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
            //     u"[authd] Unknown carrier opcode on socket %u: %u. Closing the connection.",
            //     m_hSocket, static_cast<unsigned int>(opcode))
            L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
                u"[authd] Unknown carrier opcode on socket %u: %u. Closing the connection.",
                m_hSocket, static_cast<unsigned int>(opcode));
            CloseSocket();
            return;
        }

        if (m_mode != SM_READ_BODY) {
            CloseSocket();
            return;
        }
        if (offset + m_packetLen > available) {
            Read(static_cast<std::uint32_t>(available - offset));
            return;
        }

        const auto clientOpcode = static_cast<unsigned char>(readBuffer->m_buffer[offset]);
        if (clientOpcode >= 0x15) {
            // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR, u"unknown protocol %d", static_cast<int>(clientOpcode))
            // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
            //     u"component=authd event=unknown_client_opcode socket=%u session=%d opcode=%u action=close_connection",
            //     m_hSocket, m_sessionId, static_cast<unsigned int>(clientOpcode))
            // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
            //     u"[authd] Unknown client opcode on socket %u, session %d: %u. Closing the connection.",
            //     m_hSocket, m_sessionId, static_cast<unsigned int>(clientOpcode))
            L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
                u"[authd] Unknown client opcode on socket %u, session %d: %u. Closing the connection.",
                m_hSocket, m_sessionId, static_cast<unsigned int>(clientOpcode));
            CloseSocket();
            return;
        }

        CAuthPacket* const packet = CAuthPacket::Alloc();
        packet->Init(this, static_cast<std::uint32_t>(m_sessionId), readBuffer, m_packetLen);
        AddRef();
        readBuffer->m_ref.fetch_add(1, std::memory_order_relaxed);
        CStateMonMgr::g_pStateMonMgr->authPacketPendingCount.fetch_add(1, std::memory_order_relaxed);
        packet->PostObject(offset, CAuthGateSvcMgr::g_pAuthGateSvcMgr->m_hWorkIOCP);

        offset += m_packetLen;
        m_mode = SM_READ_LEN;
        m_iLastIO = runtime::TickCount32();
    }

    Read(0);
}

// AuthGateD.exe 0x00405F70, AuthSocket.cpp.
void CAuthSocket::OnClose(std::uint32_t socket) {
    AddRef();
    m_mode = SM_CLOSE;
    CClientSocketContainer::g_pClientSocketContainer->m_bPause = true;
    CWindowMgr::g_pWindowMgr->SetTimer(0x66, config.authReconnectInterval * 1000);

    char16_t address[20]{};
    // CFileLog::g_pDefaultFileLog->LogT(LOG_NORMAL, u"*close authd connection from %s, %x(%x)",
    //                                   InetToStr(&m_addr, address, 20), socket,
    //                                   static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(this)))
    // CFileLog::g_pDefaultFileLog->LogT(LOG_WARN,
    //     u"component=authd event=disconnected peer=%s port=%d socket=%u client_accepts=paused retry_seconds=%d",
    //     InetToStr(&m_addr, address, 20), m_port, socket, config.authReconnectInterval)
    // CFileLog::g_pDefaultFileLog->LogT(LOG_WARN,
    //     u"[authd] Disconnected from %s:%d on socket %u. Client accepts are paused. Requested reconnect after %d seconds.",
    //     InetToStr(&m_addr, address, 20), m_port, socket, config.authReconnectInterval)
    L2_LOG_WARN(*CFileLog::g_pDefaultFileLog,
        u"[authd] Disconnected from %s:%d on socket %u. Client accepts are paused; requested reconnect after %d seconds.",
        InetToStr(&m_addr, address, 20), m_port, socket, config.authReconnectInterval);
}

// AuthGateD.exe 0x00406000, AuthSocket.cpp.
void CAuthSocket::SendCltConnect(std::int32_t sessionId, in_addr address) {
    if (m_mode == SM_CLOSE) {
        return;
    }
    CIOBuffer* const buffer = CIOBuffer::Alloc();
    buffer->m_size = static_cast<std::uint32_t>(
        Assemble(buffer->m_buffer, static_cast<int>(CIOBuffer::kCapacity), "cdd", 0, sessionId, address.s_addr));
    Write(buffer);
}

// AuthGateD.exe 0x00406050, AuthSocket.cpp.
void CAuthSocket::SendCltDisconnect(std::int32_t sessionId) {
    if (m_mode == SM_CLOSE) {
        return;
    }
    CIOBuffer* const buffer = CIOBuffer::Alloc();
    buffer->m_size =
        static_cast<std::uint32_t>(Assemble(buffer->m_buffer, static_cast<int>(CIOBuffer::kCapacity), "cd", 1, sessionId));
    Write(buffer);
}

// AuthGateD.exe 0x004060A0, AuthSocket.cpp.
bool CAuthSocket::SendCltPacket(std::int32_t sessionId, CIOBuffer* buffer) {
    if (m_mode == SM_CLOSE) {
        return false;
    }
    Assemble(buffer->m_buffer, static_cast<int>(CIOBuffer::kCapacity), "cdh", 2, sessionId,
             static_cast<int>(buffer->m_size) - 5);
    Write(buffer);
    return true;
}
