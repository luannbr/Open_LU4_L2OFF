#include "LogSocket.h"

#include "FileLog.h"
#include "Shared/src/Runtime/TickCount32.h"
#include "Shared/src/Runtime/WindowsAnsi.h"
#include "WindowMgr.h"
#include "GlobalFunc.h"

#include <array>
#include <cstdarg>
#include <cstdint>
#include <string>

CLogSocket* CLogSocket::s_pLogSocket = nullptr;

// AuthGateD.exe 0x00409840, LogSocket.cpp.
CLogSocket::CLogSocket() {
    AddRef();
    if (!s_pLogSocket) s_pLogSocket = this;
    m_mode = SM_CLOSE;
}

// AuthGateD.exe 0x004098B0, LogSocket.cpp.
CLogSocket::~CLogSocket() {
    s_pLogSocket = nullptr;
    m_mode = SM_CLOSE;
}

// AuthGateD.exe 0x004098E0, LogSocket.cpp.
void CLogSocket::OnRead() {
    if (m_mode == SM_CLOSE) {
        CloseSocket();
        return;
    }

    const std::int32_t available =
        static_cast<std::int32_t>(m_pReadBuf->m_size);
    std::int32_t position = 0;
    while (position < available) {
        if (m_mode == SM_READ_LEN) {
            if (available < position + 2) {
                Read(static_cast<std::uint32_t>(available - position));
                return;
            }
            const auto* const packet =
                reinterpret_cast<const std::uint8_t*>(
                    m_pReadBuf->m_buffer + position);
            m_packetLen =
                static_cast<std::int32_t>(packet[0]) - 2 +
                static_cast<std::int32_t>(packet[1]) * 0x100;
            if (m_packetLen < 1 || m_packetLen > 0x2000) {
                // CFileLog::g_pDefaultFileLog->LogT(
                //     LOG_ERROR,
                //     u"%d: bad packet size %d",
                //     static_cast<std::int32_t>(m_hSocket),
                //     m_packetLen)
                // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
                //     u"component=logd event=invalid_packet_length socket=%u body_bytes=%d min_bytes=1 max_bytes=8192 action=close_connection",
                //     m_hSocket, m_packetLen)
                // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
                //     u"[logd] Invalid packet on socket %u: body length %d is outside 1..8192 bytes. Closing the connection.",
                //     m_hSocket, m_packetLen)
                L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
                    u"[logd] Invalid packet on socket %u: body length %d is outside 1..8192 bytes. Closing the connection.",
                    m_hSocket, m_packetLen);
                CloseSocket();
                return;
            }
            position += 2;
            m_mode = SM_READ_BODY;
            continue;
        }

        if (m_mode == SM_READ_BODY) {
            if (available < position + m_packetLen) {
                Read(static_cast<std::uint32_t>(available - position));
                return;
            }
            const auto protocol = static_cast<std::uint8_t>(
                m_pReadBuf->m_buffer[position]);
            // CFileLog::g_pDefaultFileLog->LogT(
            //     LOG_ERROR,
            //     u"unknown protocol %d",
            //     static_cast<int>(protocol))
            // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
            //     u"component=logd event=unexpected_packet socket=%u opcode=%u action=close_connection",
            //     m_hSocket, static_cast<unsigned int>(protocol))
            // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
            //     u"[logd] Unexpected packet on socket %u (opcode %u). Closing the connection.",
            //     m_hSocket, static_cast<unsigned int>(protocol))
            L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
                u"[logd] Unexpected packet on socket %u (opcode %u). Closing the connection.",
                m_hSocket, static_cast<unsigned int>(protocol));
        } else if (m_mode != SM_CLOSE) {
            // Diagnostic addition: the original path closed silently outside the recognized read states.
            L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
                u"[logd] Invalid read state %d on socket %u. Closing the connection.",
                static_cast<int>(m_mode), m_hSocket);
        }
        CloseSocket();
        return;
    }
    Read(0);
}

// AuthGateD.exe 0x004099E0, LogSocket.cpp.
void CLogSocket::OnClose(std::uint32_t socket) {
    AddRef();
    m_mode = SM_CLOSE;
    CWindowMgr::g_pWindowMgr->SetTimer(0x69, 30000);

    std::array<char16_t, 20> address{};
    // CFileLog::g_pDefaultFileLog->LogT(
    //     LOG_NORMAL,
    //     u"*close logd connection from %s, %x(%x)",
    //     InetToStr(&m_addr, address.data(), address.size()),
    //     socket,
    //     static_cast<std::uint32_t>(
    //         reinterpret_cast<std::uintptr_t>(this)))
    // CFileLog::g_pDefaultFileLog->LogT(LOG_WARN,
    //     u"component=logd event=disconnected peer=%s port=%d socket=%u retry_ms=30000 fallback=local_file_attempt",
    //     InetToStr(&m_addr, address.data(), address.size()), m_port, socket)
    // CFileLog::g_pDefaultFileLog->LogT(LOG_WARN,
    //     u"[logd] Disconnected from %s:%d on socket %u. Requested reconnect after 30 seconds. Subsequent records will attempt local-file fallback.",
    //     InetToStr(&m_addr, address.data(), address.size()), m_port, socket)
    L2_LOG_WARN(*CFileLog::g_pDefaultFileLog,
        u"[logd] Disconnected from %s:%d on socket %u. Requested reconnect after 30 seconds; subsequent records will attempt local-file fallback.",
        InetToStr(&m_addr, address.data(), address.size()), m_port, socket);
}

// AuthGateD.exe 0x00409A60, LogSocket.cpp.
bool CLogSocket::Send(const char* format, ...) {
    if (m_mode == SM_CLOSE) return false;

    CIOBuffer* const buffer = CIOBuffer::Alloc();
    va_list arguments;
    va_start(arguments, format);
    const int bodyLength = Assemble(
        buffer->m_buffer + 2,
        static_cast<int>(CIOBuffer::kCapacity - 2U),
        format,
        arguments);
    va_end(arguments);

    if (bodyLength == 0) {
        // const std::u16string legacyFormat =
        //     authgated::portable::LegacyWidePacketFormat(format)
        // CFileLog::g_pDefaultFileLog->LogT(
        //     LOG_ERROR,
        //     u"%d: assemble too large packet. format %s",
        //     static_cast<std::int32_t>(m_hSocket),
        //     legacyFormat.c_str())
        // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
        //     u"component=logd event=packet_assembly_failed socket=%u body_capacity_bytes=%u action=discard_packet",
        //     m_hSocket, static_cast<unsigned int>(CIOBuffer::kCapacity - 2U))
        // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
        //     u"[logd] Could not assemble a packet on socket %u (body capacity %u bytes). Discarding the packet.",
        //     m_hSocket, static_cast<unsigned int>(CIOBuffer::kCapacity - 2U))
        // Original: L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
        //     u"[logd] Could not assemble a packet on socket %u (body capacity %u bytes). Discarding the packet.",
        //     m_hSocket, static_cast<unsigned int>(CIOBuffer::kCapacity - 2U))
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"Cannot assemble a LogD packet on socket %u (format '%S', body capacity %u bytes). Discarding the packet and returning failure.",
            m_hSocket, format, static_cast<unsigned int>(CIOBuffer::kCapacity - 2U));
        // SOURCE BUG: The original error branch leaks this buffer reference.
        // Free it before returning. No packet is sent.
        buffer->Free();
        return false;
    }

    const std::uint32_t packetLength =
        static_cast<std::uint32_t>(bodyLength + 2);
    // Noise: per-packet byte counts add no outcome. The recovered debug sink is a no-op.
    // CFileLog::g_pDefaultFileLog->LogDbgT(
    //     LOG_WARN,
    //     u"Send %d byte To logD",
    //     static_cast<std::int32_t>(packetLength))
    buffer->m_buffer[0] = static_cast<char>(packetLength);
    buffer->m_buffer[1] = static_cast<char>(packetLength >> 8U);
    buffer->m_size = packetLength;
    Write(buffer);
    m_iLastIO = runtime::TickCount32();
    return true;
}

// AuthGateD.exe 0x00409B60, LogSocket.cpp.
void CLogSocket::OnCreate() {
    std::array<char16_t, 20> address{};
    // CFileLog::g_pDefaultFileLog->LogT(
    //     LOG_NORMAL,
    //     u"*new logd connection from %s, %x(%x)",
    //     InetToStr(&m_addr, address.data(), address.size()),
    //     m_hSocket,
    //     static_cast<std::uint32_t>(
    //         reinterpret_cast<std::uintptr_t>(this)))
    // CFileLog::g_pDefaultFileLog->LogT(LOG_NORMAL,
    //     u"component=logd event=connected peer=%s port=%d socket=%u delivery=unverified",
    //     InetToStr(&m_addr, address.data(), address.size()), m_port, m_hSocket)
    // CFileLog::g_pDefaultFileLog->LogT(LOG_NORMAL,
    //     u"[logd] Connected to %s:%d on socket %u. Record delivery is not yet confirmed.",
    //     InetToStr(&m_addr, address.data(), address.size()), m_port, m_hSocket)
    // L2_LOG_INFO(*CFileLog::g_pDefaultFileLog,
    //     u"[logd] Connected to %s:%d on socket %u. Record delivery is not yet confirmed.",
    //     InetToStr(&m_addr, address.data(), address.size()), m_port, m_hSocket)
    // Noise: connection establishment does not need a per-connection delivery disclaimer.
    L2_LOG_INFO(*CFileLog::g_pDefaultFileLog,
        u"[logd] Connected to %s:%d (socket %u).",
        InetToStr(&m_addr, address.data(), address.size()), m_port, m_hSocket);
    m_iLastIO = 0;
    m_mode = SM_READ_LEN;
    Send("cd", 1, std::uint32_t{0});
    Send("cdd", 3, std::uint32_t{0xc9}, std::uint32_t{1});
    OnRead();
}

// AuthGateD.exe 0x00409BF0, LogSocket.cpp.
void CLogSocket::SendLog(const char* text) {
    std::u16string record =
        u"1413,0,,,,,,,,,,7,0,0,0,0,,,,,,,";
    record.append(portable::Utf16FromWindowsAnsi(text));
    record.append(u",,,");
    if (record.size() > 127U) record.resize(127U);

    if (!Send(
            "cddS",
            std::uint32_t{0},
            std::uint32_t{1},
            std::uint32_t{8},
            record.c_str())) {
        logdLog.LogT(LOG_NORMAL, u"%s", record.c_str());
    }
}
