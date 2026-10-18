#include "GPProxySocket.h"

#include "Shared/src/ScopedLockGuards.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/TimerQueue.h"
#include "Exception.h"
#include "GPProxyServerMgr.h"
#include "IOServer.h"
#include "Log.h"
#include "Thread.h"
#include "Util.h"

#include <cstdarg>
#include <cstdint>

#if defined(_WIN32)
#include <WinSock2.h>
#else
#include <arpa/inet.h>
#endif

std::int32_t CGpProxyPacket::s_pendingPacket = 0;
std::int32_t CGpProxyPacketPool::s_numAlloc = -1;
std::int32_t CGpProxyPacketPool::s_numFree = 0;
CGpProxyPacketPool::CSlot CGpProxyPacketPool::s_slot[16];
CGpProxyPacketPool g_gpProxyPacketPool;

namespace {

EConnDetermine::Type UnknownPacket(
    CGpProxySocket* socket,
    const unsigned char* packet);
EConnDetermine::Type RecvCreateWebSessionAck(
    CGpProxySocket* socket,
    const unsigned char* packet);
EConnDetermine::Type RecvVerifyWebSessionAck(
    CGpProxySocket* socket,
    const unsigned char* packet);
EConnDetermine::Type RecvUpdateWebSessionTimeoutAck(
    CGpProxySocket* socket,
    const unsigned char* packet);
EConnDetermine::Type RecvSecAuthListServicesAck(
    CGpProxySocket* socket,
    const unsigned char* packet);
EConnDetermine::Type RecvSecAuthAuthenticatedListAck(
    CGpProxySocket* socket,
    const unsigned char* packet);
EConnDetermine::Type RecvSecAuthVerifyOtpAck(
    CGpProxySocket* socket,
    const unsigned char* packet);
EConnDetermine::Type RecvSecondPasswordAck(
    CGpProxySocket* socket,
    const unsigned char* packet);

// L2Authd.pdb type 0x8CFD is exactly nine pointers. The duplicate
// UnknownPacket at opcode 7 and the non-source-order 5/6 entries are the raw
// pointer order at L2Authd.exe 0x00519750.
GpProxyPacketFunction s_gpProxyPacketFuncTable[9] = {
    UnknownPacket,
    RecvCreateWebSessionAck,
    RecvVerifyWebSessionAck,
    RecvUpdateWebSessionTimeoutAck,
    RecvSecAuthListServicesAck,
    RecvSecAuthVerifyOtpAck,
    RecvSecAuthAuthenticatedListAck,
    UnknownPacket,
    RecvSecondPasswordAck};

} // namespace

// L2Authd.exe 0x00442B90, Src/gpproxysocket.cpp:26-29.
#if defined(_WIN32)
void __stdcall GpProxySocketTimerRoutine(
#else
void GpProxySocketTimerRoutine(
#endif
    void* parameter,
    [[maybe_unused]] std::uint8_t timerOrWaitFired) {
    auto* const socket =
        static_cast<CGpProxySocket*>(parameter);
    CGpProxyServerMgr::Inst()->ReconnectTimerRoutine(socket);
}

// L2Authd.exe 0x00442BB0, Src/gpproxysocket.cpp:274-282.
CGpProxySocket::CGpProxySocket(
    std::uint32_t socket,
    ConnectionInformation& connectionInformation)
    : CIOSocket(socket),
      m_hReconnectTimer(nullptr),
      m_host(connectionInformation.host),
      m_port(connectionInformation.port),
      m_mode(SM_READ_LEN),
      m_packetTable(s_gpProxyPacketFuncTable) {
    AddRef();
}

// L2Authd.exe 0x00442C50, Src/gpproxysocket.cpp:286-294.
CGpProxySocket::~CGpProxySocket() = default;

// L2Authd.exe 0x00442C70, Src/gpproxysocket.cpp:268-270.
CGpProxySocket* CGpProxySocket::Allocate(
    std::uint32_t socket,
    ConnectionInformation& connectionInformation) {
    return new CGpProxySocket(
        socket,
        connectionInformation);
}

// L2Authd.exe 0x00442CF0, Src/gpproxysocket.cpp:336-348.
void CGpProxySocket::OnCreate() {
    CGpProxyServerMgr::Inst()->RegisterSocketMap(this);
    const std::uint32_t peerAddress = ntohl(m_host.s_addr);
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "* New CGpProxySocket connection to %s:%d, %x(%x)",
    //     ::inet_ntoa(m_host),
    //     static_cast<int>(m_port),
    //     m_hSocket,
    //     runtime::PointerLow32(this))
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "component=network event=connected peer=gp_proxy address=%u.%u.%u.%u port=%u socket=%u",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(m_port), m_hSocket)
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "[GPProxy] Connected to %u.%u.%u.%u:%u on socket %u.",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(m_port), m_hSocket)
    L2_LOG_INFO(Log,
        "[GPProxy] Connected to %u.%u.%u.%u:%u on socket %u.",
        (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        (peerAddress >> 8U) & 255U, peerAddress & 255U,
        static_cast<unsigned int>(m_port), m_hSocket);
    OnRead();
}

// L2Authd.exe 0x00442D50, Src/gpproxysocket.cpp:352-452.
void CGpProxySocket::OnRead() {
    int consumed = 0;
    const int available =
        static_cast<int>(m_pReadBuf->m_size);
    auto* const input =
        reinterpret_cast<unsigned char*>(
            m_pReadBuf->m_buffer);

    if (m_mode == SM_CLOSE) {
        CloseSocket();
        return;
    }

    while (consumed < available) {
        if (m_mode == SM_READ_LEN) {
            if (available < consumed + 2) {
                Read(static_cast<std::uint32_t>(
                    available - consumed));
                return;
            }

            m_packetLen =
                static_cast<int>(input[consumed]) +
                static_cast<int>(input[consumed + 1]) * 256 -
                2;
            if (m_packetLen < 1 || m_packetLen > 0x2000) {
                // Log.AddLog(
                //     LOG_ERROR,
                //     "Bad size GpProxy packet received. %d (%x)",
                //     m_packetLen,
                //     m_hSocket)
                // Log.AddLog(
                //     LOG_ERROR,
                //     "component=network event=invalid_packet_length peer=gp_proxy socket=%u payload_bytes=%d min_bytes=1 max_bytes=8192 action=close_connection",
                //     m_hSocket, m_packetLen)
                // Log.AddLog(
                //     LOG_ERROR,
                //     "[GPProxy] Invalid packet on socket %u: body length %d is outside 1..8192 bytes. Closing the connection.",
                //     m_hSocket, m_packetLen)
                L2_LOG_ERROR(Log,
                    "[GPProxy] Invalid packet on socket %u: body length %d is outside 1..8192 bytes. Closing the connection.",
                    m_hSocket, m_packetLen);
                CloseSocket();
                return;
            }
            consumed += 2;
            m_mode = SM_READ_BODY;
            continue;
        }

        if (m_mode != SM_READ_BODY) {
            L2_LOG_ERROR(Log,
                "[GPProxy] Invalid read state on socket %u: %d. Closing the connection.",
                m_hSocket, static_cast<int>(m_mode));
            CloseSocket();
            return;
        }
        if (available < consumed + m_packetLen) {
            Read(static_cast<std::uint32_t>(
                available - consumed));
            return;
        }

        if ((config.usePacketLog & 0x800U) != 0U) {
            WritePacketLog(
                "GpProxy->Auth",
                m_hSocket,
                input + consumed + 2,
                m_packetLen - 2,
                0x400);
        }

        CGpProxyPacket* const packet =
            CGpProxyPacket::Alloc();
        packet->m_unionPtr.m_pSocket = this;
        packet->m_pBuf = m_pReadBuf;
        m_pReadBuf->AddRef();
        AddRef();
        runtime::raw_atomic::IncrementI32(
            &CGpProxyPacket::s_pendingPacket);
        if (packet->PostObject(
                consumed,
                g_hIOCompletionPort) == 0) {
            // The shipping path logs but keeps all references
            // after a failed completion-port post.
            // Log.AddLog(
            //     LOG_ERROR,
            //     "Failed to post CGpProxyPacket, sock_handle:%x",
            //     m_hSocket)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "component=network event=packet_dispatch_failed peer=gp_proxy socket=%u action=continue_reading references=retained",
            //     m_hSocket)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "[GPProxy] Could not queue a packet for processing on socket %u. Reading continues. Acquired references are retained.",
            //     m_hSocket)
            L2_LOG_ERROR(Log,
                "[GPProxy] Could not queue a packet for processing on socket %u. Reading continues; acquired references are retained.",
                m_hSocket);
        }

        consumed += m_packetLen;
        m_mode = SM_READ_LEN;
    }

    Read(0);
}

// L2Authd.exe 0x00442F50, Src/gpproxysocket.cpp:298-326.
void CGpProxySocket::OnClose() {
    m_mode = SM_CLOSE;
    const std::uint32_t peerAddress = ntohl(m_host.s_addr);
    // Log.AddLog(
    //     LOG_ERROR,
    //     "* on close CGpProxySocket from %s:%d, %x(%x)",
    //     ::inet_ntoa(m_host),
    //     static_cast<int>(m_port),
    //     m_hSocket,
    //     runtime::PointerLow32(this))
    // Log.AddLog(
    //     LOG_WARN,
    //     "component=network event=socket_closed peer=gp_proxy address=%u.%u.%u.%u port=%u socket=%u reason=unspecified reconnect=%s configured_delay_ms=%d",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(m_port), m_hSocket,
    //     m_hReconnectTimer != nullptr ? "already_requested" :
    //         (config.gpProxyReconnectInterval > 0 ? "schedule_pending" : "disabled"),
    //     config.gpProxyReconnectInterval)
    // Log.AddLog(
    //     LOG_WARN,
    //     "[GPProxy] Connection to %u.%u.%u.%u:%u closed on socket %u. Cause not provided. %s Configured retry delay: %d ms.",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(m_port), m_hSocket,
    //     m_hReconnectTimer != nullptr ? "Reconnect was already requested." :
    //         (config.gpProxyReconnectInterval > 0 ? "Requesting reconnect." : "Reconnect is disabled."),
    //     config.gpProxyReconnectInterval)
    L2_LOG_WARN(Log,
        // Noise: The generic absence-of-cause phrase adds no diagnostic context.
        // "[GPProxy] Connection to %u.%u.%u.%u:%u closed on socket %u. Cause not provided. %s Configured retry delay: %d ms.",
        "[GPProxy] Connection to %u.%u.%u.%u:%u closed on socket %u. %s Configured retry delay: %d ms.",
        (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        (peerAddress >> 8U) & 255U, peerAddress & 255U,
        static_cast<unsigned int>(m_port), m_hSocket,
        m_hReconnectTimer != nullptr ? "Reconnect was already requested." :
            (config.gpProxyReconnectInterval > 0 ? "Requesting reconnect." : "Reconnect is disabled."),
        config.gpProxyReconnectInterval);
    CGpProxyServerMgr::Inst()->UnregisterSocketMap(this);

    if (m_hReconnectTimer == nullptr &&
        config.gpProxyReconnectInterval > 0) {
        const bool created =
            runtime::timer_queue::CreateTimer(
                &m_hReconnectTimer,
                nullptr,
                GpProxySocketTimerRoutine,
                this,
                static_cast<std::uint32_t>(
                    config.gpProxyReconnectInterval),
                0);
        if (created) {
            AddRef();
        } else {
            // Log.AddLog(LOG_ERROR,
            //     "component=network event=reconnect_schedule_failed peer=gp_proxy socket=%u delay_ms=%u action=return_without_new_retry",
            //     m_hSocket, static_cast<std::uint32_t>(config.gpProxyReconnectInterval))
            // Log.AddLog(LOG_ERROR,
            //     "[GPProxy] Could not schedule reconnect for socket %u after %u ms. No new retry is scheduled.",
            //     m_hSocket, static_cast<std::uint32_t>(config.gpProxyReconnectInterval))
            // L2_LOG_ERROR(Log,
            //     "[GPProxy] Could not schedule reconnect for socket %u after %u ms. No new retry is scheduled.",
            //     m_hSocket, static_cast<std::uint32_t>(config.gpProxyReconnectInterval))
            L2_LOG_ERROR(Log,
                "[GPProxy] Could not schedule reconnect to %u.%u.%u.%u:%u for socket %u after %u ms. No new retry is scheduled.",
                (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
                (peerAddress >> 8U) & 255U, peerAddress & 255U,
                static_cast<unsigned int>(m_port), m_hSocket, static_cast<std::uint32_t>(config.gpProxyReconnectInterval));
        }
    }
}

// L2Authd.exe 0x00443000, Src/gpproxysocket.cpp:330-332.
void CGpProxySocket::OnTimerCallback() {}

// L2Authd.exe 0x00443010, Src/gpproxysocket.cpp:456-515.
bool CGpProxySocket::Send(
    const char* format,
    ...) {
    AddRef();
    if (m_mode == SM_CLOSE ||
        m_hSocket == UINT32_C(0xffffffff)) {
        ReleaseRef();
        return false;
    }

    CIOBuffer* const buffer = CIOBuffer::Alloc();
    char* const packet = buffer->m_buffer;
    std::va_list arguments;
    va_start(arguments, format);
    const int payloadLength = Assemble(
        packet + 4,
        0x1ffc,
        format,
        arguments);
    va_end(arguments);

    if (payloadLength == 0) {
        // This is a confirmed target failure path: the selected image returns
        // without releasing either the newly allocated buffer or this socket.
        // Log.AddLog(
        //     LOG_ERROR,
        //     "GpProxyPacket unsent cause of too large size, socket_handle:%x, format: %s",
        //     m_hSocket,
        //     format)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=network event=packet_assembly_failed peer=gp_proxy socket=%u capacity_bytes=8188 action=return_failure references=retained",
        //     m_hSocket)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[GPProxy] Could not assemble a packet on socket %u within 8188 bytes. Returning failure with socket and buffer references retained.",
        //     m_hSocket)
        L2_LOG_ERROR(Log,
            "[GPProxy] Could not assemble a packet on socket %u within 8188 bytes. Returning failure with socket and buffer references retained.",
            m_hSocket);
        return false;
    }

    const int outerLength = payloadLength + 4;
    const int innerLength = payloadLength + 2;
    packet[0] = static_cast<char>(outerLength);
    packet[1] = static_cast<char>(outerLength >> 8);
    packet[2] = static_cast<char>(innerLength);
    packet[3] = static_cast<char>(innerLength >> 8);
    buffer->m_size =
        static_cast<std::uint32_t>(outerLength);

    if ((config.usePacketLog & 0x800U) != 0U) {
        WritePacketLog(
            "Auth->GpProxy",
            m_hSocket,
            reinterpret_cast<const unsigned char*>(
                packet + 4),
            payloadLength,
            0x400);
    }
    Write(buffer);
    ReleaseRef();
    return true;
}

CGpProxyPacket::CGpProxyPacket() = default;
CGpProxyPacket::~CGpProxyPacket() = default;

// L2Authd.exe 0x00443120, Src/gpproxysocket.cpp:154-178.
CGpProxyPacket* CGpProxyPacket::Alloc() {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            runtime::raw_atomic::IncrementI32(
                &CGpProxyPacketPool::s_numAlloc)) &
        0x0fU;
    CGpProxyPacketPool::CSlot& slot =
        CGpProxyPacketPool::s_slot[slotIndex];
    CGpProxyPacket* packet = nullptr;
    {
        L2_GUARD(slot.m_lock);
        packet = slot.m_pPacket;
        if (packet != nullptr) {
            slot.m_pPacket = packet->m_unionPtr.m_pNextPacket;
            packet->m_unionPtr.m_pNextPacket = nullptr;
        }
    }

    if (packet == nullptr) {
        packet = new CGpProxyPacket;
    }
    return packet;
}

// L2Authd.exe 0x00443200, Src/gpproxysocket.cpp:182-199.
void CGpProxyPacket::FreeAll() {
    for (CGpProxyPacketPool::CSlot& slot :
         CGpProxyPacketPool::s_slot) {
        {
            L2_GUARD(slot.m_lock);
            while (slot.m_pPacket != nullptr) {
                CGpProxyPacket* const packet =
                    slot.m_pPacket;
                slot.m_pPacket =
                    packet->m_unionPtr.m_pNextPacket;
                delete packet;
            }
        }
    }
}

// L2Authd.exe 0x004432A0, Src/gpproxysocket.cpp:203-216.
void CGpProxyPacket::Free() {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            runtime::raw_atomic::IncrementI32(
                &CGpProxyPacketPool::s_numFree)) &
        0x0fU;
    CGpProxyPacketPool::CSlot& slot =
        CGpProxyPacketPool::s_slot[slotIndex];
    {
        L2_GUARD(slot.m_lock);
        m_unionPtr.m_pNextPacket = slot.m_pPacket;
        slot.m_pPacket = this;
    }
}

// L2Authd.exe 0x004432F0, Src/gpproxysocket.cpp:224-257.
void CGpProxyPacket::OnIOCallback(
    [[maybe_unused]] int bSuccess,
    std::uint32_t dwTransferred,
    [[maybe_unused]] _OVERLAPPED* lpOverlapped) {
    try {
        const auto* packet =
            reinterpret_cast<const unsigned char*>(
                m_pBuf->m_buffer + dwTransferred + 2);
        const unsigned int command = *packet++;
        if (command >= 9U) {
            // The target returns here without releasing the packet job,
            // socket, or buffer references.
            // Log.AddLog(
            //     LOG_ERROR,
            //     "unkown GpProxy packet, %d",
            //     static_cast<int>(command))
            // Log.AddLog(
            //     LOG_ERROR,
            //     "component=network event=invalid_opcode peer=gp_proxy opcode=%u max_opcode=8 action=return_without_dispatch references=retained",
            //     command)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "[GPProxy] Cannot dispatch opcode %u: maximum 8. Returning with packet and socket references retained.",
            //     command)
            // L2_LOG_ERROR(Log,
            //     "[GPProxy] Cannot dispatch opcode %u: maximum 8. Returning with packet and socket references retained.",
            //     command)
            L2_LOG_ERROR(Log,
                "[GPProxy] Cannot dispatch opcode %u on socket %u (%s): maximum 8. Returning with packet and socket references retained.",
                command, m_unionPtr.m_pSocket ? m_unionPtr.m_pSocket->m_hSocket : 0U,
                m_unionPtr.m_pSocket ? "available" : "unavailable");
            return;
        }

        m_pFunc = s_gpProxyPacketFuncTable[command];
        if (m_pFunc == UnknownPacket) {
            L2_LOG_WARN(Log,
                "[GPProxy] Ignoring unsupported opcode %u on socket %u (%s); connection remains open.",
                command, m_unionPtr.m_pSocket ? m_unionPtr.m_pSocket->m_hSocket : 0U,
                m_unionPtr.m_pSocket ? "available" : "unavailable");
        }
        if (m_pFunc(nullptr, packet) ==
            EConnDetermine::Disconnect) {
            m_unionPtr.m_pSocket->CloseSocket();
        }
        m_unionPtr.m_pSocket->ReleaseRef();
        m_pBuf->Release();
        runtime::raw_atomic::DecrementI32(
            &s_pendingPacket);
        Free();
    } catch (...) {
        // ELOG(
        //     "*%s:%d(%s) exception\n",
        //     "GpProxySocket.cpp",
        //     256,
        //     "Wed Oct 23 17:02:11 2019")
        ELOG(
            "[GpProxySocket.cpp] Exception while dispatching or releasing a GP Proxy packet job. Entering exception handling.\n");
        send_exception(true);
    }
}

// L2Authd.exe 0x00443440, Src/gpproxysocket.cpp:48-51.
namespace {

EConnDetermine::Type UnknownPacket(
    [[maybe_unused]] CGpProxySocket* socket,
    [[maybe_unused]] const unsigned char* packet) {
    // Log.AddLog(
    //     LOG_WARN,
    //     "Received unrecognized packet from GpProxy")
    // Log.AddLog(
    //     LOG_WARN,
    //     "component=network event=unsupported_opcode peer=gp_proxy handler=UnknownPacket action=ignore_packet connection=kept")
    // Log.AddLog(
    //     LOG_WARN,
    //     "[GPProxy] Ignoring an unsupported packet in UnknownPacket. The connection remains open.")
    // Noise: the dispatcher reports the opcode and socket before calling this
    // handler. This handler receives neither the opcode nor a socket pointer.
    // L2_LOG_WARN(Log,
    //     "[GPProxy] Ignoring an unsupported packet in UnknownPacket. The connection remains open.")
    return EConnDetermine::Keep;
}

// L2Authd.exe 0x00443460, Src/gpproxysocket.cpp:58-61.
EConnDetermine::Type RecvCreateWebSessionAck(
    [[maybe_unused]] CGpProxySocket* socket,
    const unsigned char* packet) {
    CGpProxyServerMgr::Inst()->RecvCreateWebSessionAck(packet);
    return EConnDetermine::Keep;
}

// L2Authd.exe 0x00443480, Src/gpproxysocket.cpp:67-70.
EConnDetermine::Type RecvVerifyWebSessionAck(
    [[maybe_unused]] CGpProxySocket* socket,
    const unsigned char* packet) {
    CGpProxyServerMgr::Inst()->RecvVerifyWebSessionAck(packet);
    return EConnDetermine::Keep;
}

// L2Authd.exe 0x004434A0, Src/gpproxysocket.cpp:76-79.
EConnDetermine::Type RecvUpdateWebSessionTimeoutAck(
    [[maybe_unused]] CGpProxySocket* socket,
    const unsigned char* packet) {
    CGpProxyServerMgr::Inst()->RecvUpdateWebSessionTimeoutAck(packet);
    return EConnDetermine::Keep;
}

// L2Authd.exe 0x004434C0, Src/gpproxysocket.cpp:85-90.
EConnDetermine::Type RecvSecAuthListServicesAck(
    [[maybe_unused]] CGpProxySocket* socket,
    const unsigned char* packet) {
    CGpProxyServerMgr::Inst()->RecvSecAuthListServicesAck(packet);
    return EConnDetermine::Keep;
}

// L2Authd.exe 0x004434E0, Src/gpproxysocket.cpp:96-101.
EConnDetermine::Type RecvSecAuthAuthenticatedListAck(
    [[maybe_unused]] CGpProxySocket* socket,
    const unsigned char* packet) {
    CGpProxyServerMgr::Inst()->RecvSecAuthAuthenticatedListAck(packet);
    return EConnDetermine::Keep;
}

// L2Authd.exe 0x00443500, Src/gpproxysocket.cpp:107-112.
EConnDetermine::Type RecvSecAuthVerifyOtpAck(
    [[maybe_unused]] CGpProxySocket* socket,
    const unsigned char* packet) {
    CGpProxyServerMgr::Inst()->RecvSecAuthVerifyOtpAck(packet);
    return EConnDetermine::Keep;
}

// L2Authd.exe 0x00443520, Src/gpproxysocket.cpp:118-123.
EConnDetermine::Type RecvSecondPasswordAck(
    [[maybe_unused]] CGpProxySocket* socket,
    const unsigned char* packet) {
    CGpProxyServerMgr::Inst()->RecvSecondPasswordAck(packet);
    return EConnDetermine::Keep;
}

} // namespace

// L2Authd.exe 0x00443560, Src/gpproxysocket.h:140-143.
CGpProxyPacketPool::CSlot::CSlot()
    : m_pPacket(nullptr),
      m_lock(eCustomSpinLock, 0) {}

// L2Authd.exe 0x004435B0, Src/gpproxysocket.h:148.
CGpProxyPacketPool::~CGpProxyPacketPool() {
    CGpProxyPacket::FreeAll();
}
