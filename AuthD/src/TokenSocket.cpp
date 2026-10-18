#include "TokenSocket.h"

#include "Shared/src/ScopedLockGuards.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/TimerQueue.h"
#include "Config.h"
#include "IOServer.h"
#include "Log.h"
#include "Thread.h"
#include "TokenServerMgr.h"
#include "Util.h"

#include <cstdarg>
#include <cstdint>

#if defined(_WIN32)
#include <WinSock2.h>
#else
#include <arpa/inet.h>
#endif

std::int32_t TokenPacket::s_PendingPacket = 0;
std::int32_t TokenPacketPool::s_NumAlloc = -1;
std::int32_t TokenPacketPool::s_NumFree = 0;
TokenPacketPool::CSlot TokenPacketPool::s_slot[16];
TokenPacketPool gTokenPacketPool;

namespace {

bool UnknownProtocol(
    CIOSocket* socket,
    const unsigned char* packet);
bool RecvGetTokenAck(
    CIOSocket* socket,
    const unsigned char* packet);
bool RecvCloseTokenAck(
    CIOSocket* socket,
    const unsigned char* packet);
bool AdjacentWSMUnknownPacket(
    CIOSocket* socket,
    const unsigned char* packet);

// L2Authd.pdb type 0x739E is exactly three pointers, and the raw order at
// L2Authd.exe 0x00519894 is 48F390, 48F3B0, 48F3D0.
TokenPacketFunc TokenPacketFuncTable[3] = {
    UnknownProtocol,
    RecvGetTokenAck,
    RecvCloseTokenAck};

// L2Authd.exe 0x0048F390, Src/tokensocket.cpp:34-38.
bool UnknownProtocol(
    [[maybe_unused]] CIOSocket* socket,
    [[maybe_unused]] const unsigned char* packet) {
    // Log.AddLog(
    //     LOG_ERROR,
    //     "Recv Unknown Protocol from LightTokenServer")
    // Log.AddLog(
    //     LOG_WARN,
    //     "component=network event=unsupported_opcode peer=token socket=%u opcode=0 action=ignore_packet connection=kept",
    //     socket != nullptr ? socket->m_hSocket : 0U)
    // Log.AddLog(
    //     LOG_WARN,
    //     "[Token] Ignoring unsupported opcode 0 on socket %u. The connection remains open.",
    //     socket != nullptr ? socket->m_hSocket : 0U)
    L2_LOG_WARN(Log,
        "[Token] Ignoring unsupported opcode 0 on socket %u. The connection remains open.",
        socket != nullptr ? socket->m_hSocket : 0U);
    return false;
}

// L2Authd.exe 0x0048F3B0, Src/tokensocket.cpp:42-45.
bool RecvGetTokenAck(
    CIOSocket* socket,
    const unsigned char* packet) {
    TokenServerMgr::Inst()->RecvGetTokenAck(
        socket,
        packet);
    return false;
}

// L2Authd.exe 0x0048F3D0, Src/tokensocket.cpp:49-52.
bool RecvCloseTokenAck(
    CIOSocket* socket,
    const unsigned char* packet) {
    TokenServerMgr::Inst()->RecvCloseTokenAck(
        socket,
        packet);
    return false;
}

bool AdjacentWSMUnknownPacket(
    [[maybe_unused]] CIOSocket* socket,
    [[maybe_unused]] const unsigned char* packet) {
    // On the selected link layout opcode 3 reads one pointer past the
    // three-entry Token table, at 0x005198A0: WSM's UnknownPacket. Express
    // that concrete adjacent-table behavior without depending on host linker
    // placement.
    // Log.AddLog(
    //     LOG_WARN,
    //     "Received unrecognizable packet from WSM")
    // Log.AddLog(
    //     LOG_WARN,
    //     "component=network event=unsupported_opcode peer=token socket=%u opcode=3 handler=legacy_adjacent_wsm_entry action=ignore_packet connection=kept",
    //     socket != nullptr ? socket->m_hSocket : 0U)
    // Log.AddLog(
    //     LOG_WARN,
    //     "[Token] Ignoring unsupported opcode 3 on socket %u through the adjacent WSM handler. The connection remains open.",
    //     socket != nullptr ? socket->m_hSocket : 0U)
    L2_LOG_WARN(Log,
        "[Token] Ignoring unsupported opcode 3 on socket %u through the adjacent WSM handler. The connection remains open.",
        socket != nullptr ? socket->m_hSocket : 0U);
    return false;
}

} // namespace

// L2Authd.exe 0x0048EBB0, Src/tokensocket.cpp:160-165.
TokenSocket::TokenSocket(std::uint32_t socket)
    : CIOSocket(socket),
      m_mode(SM_READ_LEN),
      packetTable(TokenPacketFuncTable) {
    AddRef();
    TokenServerMgr::Inst()->mTokenServerConnected = false;
}

// L2Authd.exe 0x0048EC40, Src/tokensocket.cpp:169-170.
TokenSocket::~TokenSocket() = default;

// L2Authd.exe 0x0048EC60, Src/tokensocket.cpp:153-155.
TokenSocket* TokenSocket::Allocate(
    std::uint32_t socket) {
    return new TokenSocket(socket);
}

// L2Authd.exe 0x0048ECE0, Src/tokensocket.cpp:194-199.
void TokenSocket::OnCreate() {
    TokenServerMgr::Inst()->mTokenServerConnected = true;
    const std::uint32_t peerAddress = ntohl(config.lightTokenServerIPAddress.s_addr);
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "*new LightTokenSocket connection from %s, %x(%x)",
    //     ::inet_ntoa(config.lightTokenServerIPAddress),
    //     m_hSocket,
    //     runtime::PointerLow32(this))
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "component=network event=connected peer=token configured_address=%u.%u.%u.%u configured_port=%u socket=%u",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.lightTokenServerPort)), m_hSocket)
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "[Token] Connected. Configured endpoint %u.%u.%u.%u:%u, socket %u.",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.lightTokenServerPort)), m_hSocket)
    L2_LOG_INFO(Log,
        "[Token] Connected; configured endpoint %u.%u.%u.%u:%u, socket %u.",
        (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        (peerAddress >> 8U) & 255U, peerAddress & 255U,
        static_cast<unsigned int>(static_cast<std::uint16_t>(config.lightTokenServerPort)), m_hSocket);
    OnRead();
}

// L2Authd.exe 0x0048ED30, Src/tokensocket.cpp:203-262.
void TokenSocket::OnRead() {
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
            if (available < consumed + 3) {
                Read(static_cast<std::uint32_t>(
                    available - consumed));
                return;
            }

            packetLen =
                static_cast<int>(input[consumed]) - 2 +
                static_cast<int>(input[consumed + 1]) * 256;
            if (packetLen < 1 || packetLen > 0x2000) {
                // Log.AddLog(
                //     LOG_ERROR,
                //     "%d: bad packet size %d",
                //     m_hSocket,
                //     packetLen)
                // Log.AddLog(
                //     LOG_WARN,
                //     "component=network event=invalid_packet_length peer=token socket=%u payload_bytes=%d min_bytes=1 max_bytes=8192 action=close_connection",
                //     m_hSocket, packetLen)
                // Log.AddLog(
                //     LOG_WARN,
                //     "[Token] Invalid packet on socket %u: body length %d is outside 1..8192 bytes. Closing the connection.",
                //     m_hSocket, packetLen)
                L2_LOG_WARN(Log,
                    "[Token] Invalid packet on socket %u: body length %d is outside 1..8192 bytes. Closing the connection.",
                    m_hSocket, packetLen);
                CloseSocket();
                return;
            }
            consumed += 2;
            m_mode = SM_READ_BODY;
            continue;
        }

        if (m_mode != SM_READ_BODY) {
            // Log.AddLog(LOG_ERROR,
            //     "component=network event=invalid_read_state peer=token socket=%u state=%d action=close_connection",
            //     m_hSocket, static_cast<int>(m_mode))
            // Log.AddLog(LOG_ERROR,
            //     "[Token] Invalid read state on socket %u: %d. Closing the connection.",
            //     m_hSocket, static_cast<int>(m_mode))
            L2_LOG_ERROR(Log,
                "[Token] Invalid read state on socket %u: %d. Closing the connection.",
                m_hSocket, static_cast<int>(m_mode));
            CloseSocket();
            return;
        }
        if (available < consumed + packetLen) {
            Read(static_cast<std::uint32_t>(
                available - consumed));
            return;
        }
        if ((config.usePacketLog & 0x80U) != 0U) {
            WritePacketLog(
                "Token->Auth",
                m_hSocket,
                input + consumed + 2,
                packetLen - 2,
                0x400);
        }

        TokenPacket* const packet = TokenPacket::Alloc();
        packet->m_pSocket = this;
        packet->m_pBuf = m_pReadBuf;
        AddRef();
        m_pReadBuf->AddRef();
        runtime::raw_atomic::IncrementI32(
            &TokenPacket::s_PendingPacket);
        // packet->PostObject(
        //     consumed,
        //     g_hIOCompletionPort)
        const auto posted = packet->PostObject(
            consumed,
            g_hIOCompletionPort);
        if (!posted) {
            // Log.AddLog(LOG_ERROR,
            //     "component=network event=packet_dispatch_failed peer=token socket=%u action=continue_reading references=retained",
            //     m_hSocket)
            // Log.AddLog(LOG_ERROR,
            //     "[Token] Could not queue a packet for processing on socket %u. Reading continues. Acquired references are retained.",
            //     m_hSocket)
            L2_LOG_ERROR(Log,
                "[Token] Could not queue a packet for processing on socket %u. Reading continues; acquired references are retained.",
                m_hSocket);
        }
        consumed += packetLen;
        m_mode = SM_READ_LEN;
    }

    Read(0);
}

// L2Authd.exe 0x0048EF20, Src/tokensocket.cpp:174-190.
void TokenSocket::OnClose() {
    m_mode = SM_CLOSE;
    TokenServerMgr* const manager =
        TokenServerMgr::Inst();
    manager->mTokenServerConnected = false;
    const std::uint32_t peerAddress = ntohl(config.lightTokenServerIPAddress.s_addr);
    // Log.AddLog(
    //     LOG_ERROR,
    //     "*close connection LightTokenSocket from %s, %x(%x)",
    //     ::inet_ntoa(config.lightTokenServerIPAddress),
    //     m_hSocket,
    //     runtime::PointerLow32(this))
    // Log.AddLog(
    //     LOG_WARN,
    //     "component=network event=socket_closed peer=token configured_address=%u.%u.%u.%u configured_port=%u socket=%u reason=unspecified reconnect=schedule_pending delay_ms=%u",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.lightTokenServerPort)), m_hSocket,
    //     static_cast<std::uint32_t>(config.lightTokenServerConnectInterval))
    // Log.AddLog(
    //     LOG_WARN,
    //     "[Token] Connection closed. Configured endpoint %u.%u.%u.%u:%u, socket %u. Cause not provided. Requesting reconnect after %u ms.",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.lightTokenServerPort)), m_hSocket,
    //     static_cast<std::uint32_t>(config.lightTokenServerConnectInterval))
    L2_LOG_WARN(Log,
        // Noise: The generic absence-of-cause phrase adds no diagnostic context.
        // "[Token] Connection closed. Configured endpoint %u.%u.%u.%u:%u, socket %u. Cause not provided. Requesting reconnect after %u ms.",
        "[Token] Connection closed; configured endpoint %u.%u.%u.%u:%u, socket %u. Requesting reconnect after %u ms.",
        (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        (peerAddress >> 8U) & 255U, peerAddress & 255U,
        static_cast<unsigned int>(static_cast<std::uint16_t>(config.lightTokenServerPort)), m_hSocket,
        static_cast<std::uint32_t>(config.lightTokenServerConnectInterval));
    AddRef();
    // runtime::timer_queue::CreateTimer(
    //     &manager->mhTokenServerTimer,
    //     nullptr,
    //     TokenSocketTimerRoutine,
    //     this,
    //     static_cast<std::uint32_t>(
    //         config.lightTokenServerConnectInterval),
    //     0)
    const bool created = runtime::timer_queue::CreateTimer(
        &manager->mhTokenServerTimer,
        nullptr,
        TokenSocketTimerRoutine,
        this,
        static_cast<std::uint32_t>(
            config.lightTokenServerConnectInterval),
        0);
    if (!created) {
        // Log.AddLog(LOG_ERROR,
        //     "component=network event=reconnect_schedule_failed peer=token socket=%u delay_ms=%u action=return_without_new_retry references=retained",
        //     m_hSocket, static_cast<std::uint32_t>(config.lightTokenServerConnectInterval))
        // Log.AddLog(LOG_ERROR,
        //     "[Token] Could not schedule reconnect for socket %u after %u ms. No new retry is scheduled. Acquired references are retained.",
        //     m_hSocket, static_cast<std::uint32_t>(config.lightTokenServerConnectInterval))
        // L2_LOG_ERROR(Log,
        //     "[Token] Could not schedule reconnect for socket %u after %u ms. No new retry is scheduled. Acquired references are retained.",
        //     m_hSocket, static_cast<std::uint32_t>(config.lightTokenServerConnectInterval))
        L2_LOG_ERROR(Log,
            "[Token] Could not schedule reconnect to %u.%u.%u.%u:%u for socket %u after %u ms. No new retry is scheduled. Acquired references are retained.",
            (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            (peerAddress >> 8U) & 255U, peerAddress & 255U,
            static_cast<unsigned int>(static_cast<std::uint16_t>(config.lightTokenServerPort)), m_hSocket, static_cast<std::uint32_t>(config.lightTokenServerConnectInterval));
    }
}

// L2Authd.exe 0x0048EFA0, Src/tokensocket.cpp:266-299.
bool TokenSocket::Send(const char* format, ...) {
    AddRef();
    if (m_mode == SM_CLOSE ||
        !TokenServerMgr::Inst()->mTokenServerConnected ||
        m_hSocket == UINT32_C(0xffffffff)) {
        ReleaseRef();
        return false;
    }

    CIOBuffer* const buffer = CIOBuffer::Alloc();
    char* const packet = buffer->m_buffer;
    std::va_list arguments;
    va_start(arguments, format);
    int packetSize = Assemble(
        packet + 4,
        0x1ffc,
        format,
        arguments);
    va_end(arguments);

    if (packetSize == 0) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "%d: assemble too large packet. format %s",
        //     m_hSocket,
        //     format)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=network event=packet_assembly_failed peer=token socket=%u capacity_bytes=8188 action=discard_empty_buffer legacy_return=success",
        //     m_hSocket)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[Token] Could not assemble a packet on socket %u within 8188 bytes. Discarding the empty buffer. The send path still returns success.",
        //     m_hSocket)
        L2_LOG_ERROR(Log,
            "[Token] Could not assemble a packet on socket %u within 8188 bytes. Discarding the empty buffer; the send path still returns success.",
            m_hSocket);
    } else {
        packetSize += 2;
        packet[0] = static_cast<char>(packetSize);
        packet[1] = static_cast<char>(packetSize >> 8);
        packetSize += 2;
        // Unlike WSM/Grade, both stores in the selected Token image target
        // offsets 0 and 1. Offsets 2 and 3 are left untouched.
        packet[0] = static_cast<char>(packetSize);
        packet[1] = static_cast<char>(packetSize >> 8);
    }
    buffer->m_size = static_cast<std::uint32_t>(
        packetSize);

    if ((config.usePacketLog & 0x80U) != 0U) {
        WritePacketLog(
            "Auth->Token",
            m_hSocket,
            reinterpret_cast<const unsigned char*>(
                packet + 4),
            packetSize - 4,
            0x400);
    }
    Write(buffer);
    ReleaseRef();
    return true;
}

TokenPacket::TokenPacket() = default;
TokenPacket::~TokenPacket() = default;

// L2Authd.exe 0x0048F100, Src/tokensocket.cpp:74-90.
TokenPacket* TokenPacket::Alloc() {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            runtime::raw_atomic::IncrementI32(
                &TokenPacketPool::s_NumAlloc)) &
        0x0fU;
    TokenPacketPool::CSlot& slot =
        TokenPacketPool::s_slot[slotIndex];
    TokenPacket* packet = nullptr;
    {
        L2_GUARD(slot.m_lock);
        packet = slot.m_pPacket;
        if (packet != nullptr) {
            slot.m_pPacket =
                reinterpret_cast<TokenPacket*>(
                    packet->m_pSocket);
        }
    }
    if (packet == nullptr) {
        packet = new TokenPacket;
    }
    return packet;
}

// L2Authd.exe 0x0048F1D0, Src/tokensocket.cpp:94-105.
void TokenPacket::FreeAll() {
    for (TokenPacketPool::CSlot& slot :
         TokenPacketPool::s_slot) {
        {
            L2_GUARD(slot.m_lock);
            while (slot.m_pPacket != nullptr) {
                TokenPacket* const packet =
                    slot.m_pPacket;
                slot.m_pPacket =
                    reinterpret_cast<TokenPacket*>(
                        packet->m_pSocket);
                delete packet;
            }
        }
    }
}

// L2Authd.exe 0x0048F260, Src/tokensocket.cpp:109-116.
void TokenPacket::Free() {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            runtime::raw_atomic::DecrementI32(
                &TokenPacketPool::s_NumFree)) &
        0x0fU;
    TokenPacketPool::CSlot& slot =
        TokenPacketPool::s_slot[slotIndex];
    {
        L2_GUARD(slot.m_lock);
        m_pSocket = reinterpret_cast<CIOSocket*>(
            slot.m_pPacket);
        slot.m_pPacket = this;
    }
}

// L2Authd.exe 0x0048F2B0, Src/tokensocket.cpp:120-142.
void TokenPacket::OnIOCallback(
    [[maybe_unused]] int bSuccess,
    std::uint32_t dwTransferred,
    [[maybe_unused]] _OVERLAPPED* lpOverlapped) {
    auto* const packet =
        reinterpret_cast<unsigned char*>(
            m_pBuf->m_buffer + dwTransferred + 2);
    const unsigned int opcode = *packet;
    if (opcode > 3U) {
        // The selected path returns without releasing any queued ownership.
        // Log.AddLog(
        //     LOG_ERROR,
        //     "unknown protocol %d",
        //     static_cast<int>(opcode))
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=network event=invalid_opcode peer=token socket=%u opcode=%u max_opcode=3 action=return_without_dispatch references=retained",
        //     m_pSocket != nullptr ? m_pSocket->m_hSocket : 0U, opcode)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[Token] Cannot dispatch packet on socket %u: opcode %u exceeds maximum 3. Returning with packet and socket references retained.",
        //     m_pSocket != nullptr ? m_pSocket->m_hSocket : 0U, opcode)
        L2_LOG_WARN(Log,
            "[Token] Cannot dispatch packet on socket %u: opcode %u exceeds maximum 3. Returning with packet and socket references retained.",
            m_pSocket != nullptr ? m_pSocket->m_hSocket : 0U, opcode);
        return;
    }

    if (opcode == 3U) {
        m_pFunc = AdjacentWSMUnknownPacket;
    } else {
        m_pFunc = TokenPacketFuncTable[opcode];
    }
    if (m_pFunc(m_pSocket, packet + 1)) {
        m_pSocket->CloseSocket();
    }
    m_pSocket->ReleaseRef();
    m_pBuf->Release();
    runtime::raw_atomic::DecrementI32(
        &s_PendingPacket);
    Free();
}

// L2Authd.exe 0x0048F370, Src/tokensocket.cpp:22-24.
#if defined(_WIN32)
void __stdcall TokenSocketTimerRoutine(
#else
void TokenSocketTimerRoutine(
#endif
    [[maybe_unused]] void* parameter,
    [[maybe_unused]] std::uint8_t timerOrWaitFired) {
    TokenServerMgr::Inst()->TokenServerTimerRoutine();
}

// L2Authd.exe 0x0048F3F0, Src/tokensocket.h:67.
TokenPacketPool::CSlot::CSlot()
    : m_pPacket(nullptr),
      m_lock(eCustomSpinLock, 0) {}

// L2Authd.exe 0x0048F480, Src/tokensocket.h:72.
TokenPacketPool::~TokenPacketPool() {
    TokenPacket::FreeAll();
}
