#include "Shared/src/Log.h"

#include "Shared/src/GIPSocket.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/GIP.h"
#include "Shared/src/IOCPacket.h"

#include <atomic>
#include <cstdint>

namespace {

using GIPPacket = CPacketEx<IOBuffer8k, CGIPSocket, GIPPacketCallback>;

constexpr int kMaximumGIPBody = 0x1ffe;

} // namespace

bool DummyGIPPacket(CGIPSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     "[ALERT ALERT ALERT] DummyPacket called in CGIPSocket "
    //     "[ALERT ALERT ALERT]")
    // Log.Add(
    //     LOG_WARN,
    //     "[GIP] Received a packet without a registered handler, ignoring it.")
    L2_LOG_WARN(Log, "[GIP] Received a packet without a registered handler; ignoring it.");
    return false;
}

bool GIPResultPacket(CGIPSocket*, const unsigned char* packet) {
    // Current L2Server 0x00776e1c.
    L2_GUARD_FUNCTION();
    if (GIP.m_sendable) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Message from GIP server, but connection is already "
        //     L"established. IGNORE!!!")
        // Log.Add(
        //     LOG_WARN,
        //     "[GIP] Received another handshake reply after sending was enabled, ignoring it.")
        L2_LOG_WARN(Log, "[GIP] Received another handshake reply after sending was enabled; ignoring it.");
        return false;
    }

    std::int16_t result = 0;
    ::Disassemble(packet + 1, "h", &result);
    if (result == 0) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Success to handshaking with GIP server.")
        // Log.Add(
        //     LOG_NORMAL,
        //     "[GIP] Handshake completed, game information can now be sent.")
        L2_LOG_INFO(Log, "[GIP] Handshake completed; game information can now be sent.");
        GIP.SetSendable(true);
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Error Message from GIP server [%d]",
        //     static_cast<int>(result))
        // Log.Add(
        //     LOG_ERROR,
        //     "[GIP] Handshake rejected with result %d. Sending remains disabled.",
        //     static_cast<int>(result))
        L2_LOG_ERROR(Log, "[GIP] Handshake rejected with result %d; sending remains disabled.", static_cast<int>(result));
    }
    return false;
}

// Original Shared/GIPSocket.cpp owners. Current Cached.exe constructs the
// map at 0x004dc690, binds slot two through 0x004da71c, and publishes the
// callback-array address consumed by CGameInfoPipeline::ConnectToDaemon.
GIPCallbackMap g_PacketTableFromGIP{DummyGIPPacket, u""};
L2_BIND_PACKET(IsRegistered_MSG_SET_GAMESERVER_INFO_RESULT, g_PacketTableFromGIP, 2, GIPResultPacket);
GIPPacketCallback* GIPPacketTable = g_PacketTableFromGIP.GetData();

CGIPSocket::CGIPSocket(std::uint64_t socket, GIPPacketCallback* packetTable) :
    CIOSocketEx<IOBuffer8k>(socket, 0x1000),
    m_Socket(socket),
    m_threadCount(0),
    m_targetAlignmentPadding(0),
    m_packetTable(packetTable),
    m_mode(SocketMode::SM_READ_LEN),
    m_packetLength(0) {
    L2_GUARD_FUNCTION();
}

CGIPSocket::~CGIPSocket() {
    // Current Cached.exe scalar deleting destructor 0x004dc4fc clears the
    // callback table at +0xb8 before entering CIOSocketEx<8192> teardown.
    m_packetTable = nullptr;
}

void CGIPSocket::Send(const char* format, ...) {
    L2_GUARD_FUNCTION();
    va_list arguments;
    va_start(arguments, format);
    SendV(format, arguments);
    va_end(arguments);
}

void CGIPSocket::SendV(const char* format, va_list arguments) {
    L2_GUARD_FUNCTION();
    if (m_mode == SocketMode::SM_CLOSE) {
        return;
    }

    IOBuffer8k* const buffer = IOBuffer8k::Alloc();

    const int bodyLength =
        VAssemble(reinterpret_cast<char*>(buffer->m_Buffer.data() + 2), kMaximumGIPBody, format, arguments);
    std::uint16_t frameLength = 0;
    if (bodyLength == 0) {
        // Noise: VAssemble already reports overflow. A zero-length result can also mean an empty format, not an oversized
        // packet. Log.Add(
        //     LOG_ERROR,
        //     L"%d: assemble too large packet. format %s",
        //     runtime::LegacySocketImportAddressToken(),
        //     format)
    } else {
        frameLength = static_cast<std::uint16_t>(bodyLength + 2);
        buffer->m_Buffer[0] = static_cast<std::uint8_t>(frameLength);
        buffer->m_Buffer[1] = static_cast<std::uint8_t>(frameLength >> 8U);
    }
    buffer->m_dwSize = frameLength;

    // Current Cached.exe keeps the synchronous critical-section write.
    // Current L2Server 0x00777754 instead takes the ioc.h:0x29f socket
    // keep and publishes AsyncWrite through CExecutionSerializerFast.
#if defined(L2_BUILD_CACHED)
    L2_ADD_REF(this, ObjectRefType::IoSocketWrite);
    SyncWrite(buffer);
#else
    Write(buffer);
#endif
}

bool CGIPSocket::CheckPendingWrite() {
    L2_GUARD_FUNCTION();
    if (m_hSocket.load(std::memory_order_acquire) == UINT64_C(0xffffffffffffffff)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Write to invalid GIP socket.",
        //     L"..\\Shared\\GIPSocket.cpp",
        //     0x12e)
        // Log.Add(
        //     LOG_WARN,
        //     "[GIP] Rejecting a write to an already closed connection.")
        L2_LOG_WARN(Log, "[GIP] Rejecting a write to an already closed connection.");
        return false;
    }
    if (PendingWrite() < 0x800001) {
        return true;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"GIPSocket pending write > %d KB, Close!",
    //     0x2000)
    // Log.Add(
    //     LOG_ERROR,
    //     "[GIP] Pending writes exceed the 8388608-byte limit on connection %llu. Rejecting the write and requesting
    //     closure.", static_cast<unsigned long long>(
    //         m_hSocket.load(std::memory_order_acquire)))
    L2_LOG_ERROR(
        Log,
        "[GIP] Pending writes exceed the 8388608-byte limit on connection %llu; rejecting the write and requesting closure.",
        static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire))
    );
    runtime::process::ReportCallStack(2, false);
    return false;
}

void CGIPSocket::OnClose() {
    L2_GUARD_FUNCTION();
    GIP.SetSendable(false);
    GIP.m_connected = false;
    // Log.Add(
    //     LOG_WARN,
    //     L"CGIPSocket Close %x(%x)",
    //     static_cast<unsigned long long>(m_Socket),
    //     static_cast<unsigned long long>(
    //         reinterpret_cast<std::uintptr_t>(this)))
    // Log.Add(
    //     LOG_NORMAL,
    //     "[GIP] Connection %llu closed. Sending is disabled.",
    //     static_cast<unsigned long long>(m_Socket))
    L2_LOG_INFO(Log, "[GIP] Connection %llu closed; sending is disabled.", static_cast<unsigned long long>(m_Socket));
    m_mode = SocketMode::SM_CLOSE;
    // Current InterlockedExchange is 32-bit even though the
    // connection token is a qword. Preserve the low-lane-only write.
    runtime::raw_atomic::ExchangeU32(reinterpret_cast<std::uint32_t*>(&m_Socket), 0);
}

void CGIPSocket::OnRead() {
    L2_GUARD_FUNCTION();
    std::uint32_t packetOffset = 0;
    const std::uint32_t available = m_pReadBuf->m_dwSize;

    if (m_mode == SocketMode::SM_CLOSE) {
        Close();
        return;
    }

    while (packetOffset < available) {
        if (m_mode == SocketMode::SM_READ_LEN) {
            if (available < packetOffset + 2U) {
                Read(available - packetOffset);
                return;
            }
            const std::uint16_t frameLength = static_cast<std::uint16_t>(m_pReadBuf->m_Buffer[packetOffset]) |
                static_cast<std::uint16_t>(m_pReadBuf->m_Buffer[packetOffset + 1U] << 8U);
            m_packetLength = static_cast<int>(frameLength) - 2;
            if (m_packetLength < 1 || m_packetLength > kMaximumGIPBody) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"GIPsocket %d: bad packet size %d",
                //     runtime::LegacySocketImportAddressToken(),
                //     m_packetLength)
                // Log.Add(
                //     LOG_ERROR,
                //     "[GIP] Invalid packet body length %d bytes on connection %llu (allowed 1..%d). Closing the
                //     connection.", m_packetLength, static_cast<unsigned long long>(
                //         m_hSocket.load(std::memory_order_acquire)),
                //     kMaximumGIPBody)
                L2_LOG_ERROR(
                    Log,
                    "[GIP] Invalid packet body length %d bytes on connection %llu (allowed 1..%d); closing the connection.",
                    m_packetLength, static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire)),
                    kMaximumGIPBody
                );
                m_mode = SocketMode::SM_CLOSE;
                Close();
                return;
            }
            m_mode = SocketMode::SM_READ_BODY;
            packetOffset += 2U;
        }

        if (m_mode != SocketMode::SM_READ_BODY) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[GIP] Invalid receive state %d on connection %llu. Closing the connection.",
            //     static_cast<int>(m_mode),
            //     static_cast<unsigned long long>(
            //         m_hSocket.load(std::memory_order_acquire)))
            L2_LOG_ERROR(
                Log, "[GIP] Invalid receive state %d on connection %llu; closing the connection.", static_cast<int>(m_mode),
                static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire))
            );
            Close();
            return;
        }
        if (available < packetOffset + static_cast<std::uint32_t>(m_packetLength)) {
            Read(available - packetOffset);
            return;
        }

        const std::uint8_t protocol = m_pReadBuf->m_Buffer[packetOffset];
        if (protocol > 2U) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[GIPsocket]unknown protocol %d",
            //     static_cast<int>(protocol))
            // Log.Add(
            //     LOG_ERROR,
            //     "[GIP] Unsupported incoming opcode 0x%02x on connection %llu (allowed 0x00..0x02). Closing the
            //     connection.", static_cast<unsigned int>(protocol), static_cast<unsigned long long>(
            //         m_hSocket.load(std::memory_order_acquire)))
            L2_LOG_ERROR(
                Log,
                "[GIP] Unsupported incoming opcode 0x%02x on connection %llu (allowed 0x00..0x02); closing the connection.",
                static_cast<unsigned int>(protocol),
                static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire))
            );
            m_mode = SocketMode::SM_CLOSE;
            Close();
            return;
        }

        auto* const packet = new GIPPacket;
        packet->m_pSocket = this;
        packet->m_pBuf = m_pReadBuf;
        packet->m_pFunc = m_packetTable[protocol];
        L2_ADD_REF(this, ObjectRefType::IoRetain);
        m_pReadBuf->AddRef();
        CAbstractPacket::g_nPendingPacket.fetch_add(1, std::memory_order_seq_cst);
        // The shipping code ignores a failed IOCP post.
        packet->PostObject(static_cast<int>(packetOffset));

        packetOffset += static_cast<std::uint32_t>(m_packetLength);
        m_mode = SocketMode::SM_READ_LEN;
    }
    Read(0);
}
