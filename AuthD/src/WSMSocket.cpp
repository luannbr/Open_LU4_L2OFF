#include "WSMSocket.h"

#include "Shared/src/ScopedLockGuards.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/TimerQueue.h"
#include "Config.h"
#include "Exception.h"
#include "IOServer.h"
#include "Log.h"
#include "Thread.h"
#include "Util.h"
#include "WSMServerMgr.h"

#include <cstdarg>
#include <cstdint>
#include <cstdlib>

#if defined(_WIN32)
#include <WinSock2.h>
#else
#include <arpa/inet.h>
#endif

std::int32_t CWSMPacket::s_PendingPacket = 0;
std::int32_t CWSMPacketPool::s_NumAlloc = -1;
std::int32_t CWSMPacketPool::s_NumFree = 0;
CWSMPacketPool::CSlot CWSMPacketPool::s_slot[16];
CWSMPacketPool gWSMSPacketPool;

namespace {

bool UnknownPacket(
    CIOSocket* socket,
    const unsigned char* packet);
bool RecvVerifyWebSessionAck(
    CIOSocket* socket,
    const unsigned char* packet);
bool RecvUpdateTimeoutAck(
    CIOSocket* socket,
    const unsigned char* packet);
bool RecvProtocolVerAck(
    CIOSocket* socket,
    const unsigned char* packet);
bool RecvLoginAck(
    CIOSocket* socket,
    const unsigned char* packet);
bool RecvGetAuthnTokenAck(
    CIOSocket* socket,
    const unsigned char* packet);

// The raw seven entries at L2Authd.exe 0x005198A0 are, in order:
// 4981B0, 498210, 4981D0, 4981F0, 498220, 498240, FFFFFFFF.
// Slot 6 is therefore not replaced by an invented handler.
WSMPacketFunc WSMPacketFuncTable[7] = {
    UnknownPacket,
    RecvProtocolVerAck,
    RecvVerifyWebSessionAck,
    RecvUpdateTimeoutAck,
    RecvLoginAck,
    RecvGetAuthnTokenAck,
    reinterpret_cast<WSMPacketFunc>(
        static_cast<std::uintptr_t>(UINT32_MAX))};

// L2Authd.exe 0x004981B0, Src/wsmsocket.cpp:38-41.
bool UnknownPacket(
    [[maybe_unused]] CIOSocket* socket,
    [[maybe_unused]] const unsigned char* packet) {
    // Log.AddLog(
    //     LOG_WARN,
    //     "Received unrecognizable packet from WSM")
    // Log.AddLog(
    //     LOG_WARN,
    //     "component=network event=unsupported_opcode peer=wsm opcode=0 action=ignore_packet connection=kept")
    // Log.AddLog(
    //     LOG_WARN,
    //     "[WSM] Ignoring unsupported opcode 0. The connection remains open.")
    // L2_LOG_WARN(Log,
    //     "[WSM] Ignoring unsupported opcode 0. The connection remains open.")
    L2_LOG_WARN(Log,
        "[WSM] Ignoring unsupported opcode 0 on socket %u (%s). The connection remains open.",
        socket ? socket->m_hSocket : 0U, socket ? "available" : "unavailable");
    return false;
}

// L2Authd.exe 0x004981D0, Src/wsmsocket.cpp:45-52.
bool RecvVerifyWebSessionAck(
    [[maybe_unused]] CIOSocket* socket,
    const unsigned char* packet) {
    WSMServerMgr::Inst()->RecvVerifyWebSessionAck(
        packet);
    return false;
}

// L2Authd.exe 0x004981F0, Src/wsmsocket.cpp:56-63.
bool RecvUpdateTimeoutAck(
    [[maybe_unused]] CIOSocket* socket,
    const unsigned char* packet) {
    WSMServerMgr::Inst()->RecvUpdateTimeoutAck(
        packet);
    return false;
}

// L2Authd.exe 0x00498210, Src/wsmsocket.cpp:66-68.
bool RecvProtocolVerAck(
    [[maybe_unused]] CIOSocket* socket,
    [[maybe_unused]] const unsigned char* packet) {
    return false;
}

// L2Authd.exe 0x00498220, Src/wsmsocket.cpp:71-78.
bool RecvLoginAck(
    [[maybe_unused]] CIOSocket* socket,
    const unsigned char* packet) {
    WSMServerMgr::Inst()->RecvLoginAck(packet);
    return false;
}

// L2Authd.exe 0x00498240, Src/wsmsocket.cpp:81-86.
bool RecvGetAuthnTokenAck(
    [[maybe_unused]] CIOSocket* socket,
    [[maybe_unused]] const unsigned char* packet) {
    return false;
}

} // namespace

// L2Authd.exe 0x00497930, Src/wsmsocket.cpp:197-202.
CWSMSocket::CWSMSocket(std::uint32_t socket)
    : CIOSocket(socket),
      mode(SM_READ_LEN),
      packetTable(WSMPacketFuncTable) {
    AddRef();
    WSMServerMgr::Inst()->mWSMServerConnected = false;
}

// L2Authd.exe 0x004979C0, Src/wsmsocket.cpp:206-207.
CWSMSocket::~CWSMSocket() = default;

// L2Authd.exe 0x004979E0, Src/wsmsocket.cpp:190-192.
CWSMSocket* CWSMSocket::Allocate(
    std::uint32_t socket) {
    return new CWSMSocket(socket);
}

// L2Authd.exe 0x00497A60, Src/wsmsocket.cpp:236-241.
void CWSMSocket::OnCreate() {
    WSMServerMgr::Inst()->mWSMServerConnected = true;
    const std::uint32_t peerAddress = ntohl(config.WSMIP.s_addr);
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "*new WSM connection from %s, %x(%x)",
    //     ::inet_ntoa(config.WSMIP),
    //     m_hSocket,
    //     runtime::PointerLow32(this))
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "component=network event=connected peer=wsm address=%u.%u.%u.%u port=%u socket=%u",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.WSMPort)), m_hSocket)
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "[WSM] Connected to %u.%u.%u.%u:%u on socket %u.",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.WSMPort)), m_hSocket)
    L2_LOG_INFO(Log,
        "[WSM] Connected to %u.%u.%u.%u:%u on socket %u.",
        (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        (peerAddress >> 8U) & 255U, peerAddress & 255U,
        static_cast<unsigned int>(static_cast<std::uint16_t>(config.WSMPort)), m_hSocket);
    OnRead();
}

// L2Authd.exe 0x00497AB0, Src/wsmsocket.cpp:245-299.
void CWSMSocket::OnRead() {
    int consumed = 0;
    const int available =
        static_cast<int>(m_pReadBuf->m_size);
    auto* const input =
        reinterpret_cast<unsigned char*>(
            m_pReadBuf->m_buffer);

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
                static_cast<int>(input[consumed]) - 2 +
                static_cast<int>(input[consumed + 1]) * 256;
            if (packetLen < 1 || packetLen > 0x2000) {
                // Log.AddLog(
                //     LOG_ERROR,
                //     "%d: bad packet size %d",
                //     m_hSocket,
                //     packetLen)
                // Log.AddLog(
                //     LOG_ERROR,
                //     "component=network event=invalid_packet_length peer=wsm socket=%u payload_bytes=%d min_bytes=1 max_bytes=8192 action=close_connection",
                //     m_hSocket, packetLen)
                // Log.AddLog(
                //     LOG_ERROR,
                //     "[WSM] Invalid packet on socket %u: body length %d is outside 1..8192 bytes. Closing the connection.",
                //     m_hSocket, packetLen)
                // L2_LOG_ERROR(Log,
                //     "[WSM] Invalid packet on socket %u: body length %d is outside 1..8192 bytes. Closing the connection.",
                //     m_hSocket, packetLen)
                L2_LOG_WARN(Log,
                    "[WSM] Invalid packet on socket %u: body length %d is outside 1..8192 bytes. Closing the connection.",
                    m_hSocket, packetLen);
                CloseSocket();
                return;
            }
            consumed += 2;
            mode = SM_READ_BODY;
            continue;
        }

        if (mode != SM_READ_BODY) {
            L2_LOG_ERROR(Log,
                "[WSM] Invalid read state on socket %u: %d. Closing the connection.",
                m_hSocket, static_cast<int>(mode));
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
                "WSM->Auth",
                m_hSocket,
                input + consumed + 2,
                packetLen - 2,
                0x400);
        }

        CWSMPacket* const packet = CWSMPacket::Alloc();
        packet->m_pSocket = this;
        packet->m_pBuf = m_pReadBuf;
        AddRef();
        m_pReadBuf->AddRef();
        runtime::raw_atomic::IncrementI32(
            &CWSMPacket::s_PendingPacket);
        // packet->PostObject(
        //     consumed,
        //     g_hIOCompletionPort)
        const auto posted = packet->PostObject(
            consumed,
            g_hIOCompletionPort);
        if (posted == 0) {
            // Log.AddLog(LOG_ERROR,
            //     "component=network event=packet_dispatch_failed peer=wsm socket=%u action=continue_reading references=retained",
            //     m_hSocket)
            // Log.AddLog(LOG_ERROR,
            //     "[WSM] Could not queue a packet for processing on socket %u. Reading continues. Acquired references are retained.",
            //     m_hSocket)
            L2_LOG_ERROR(Log,
                "[WSM] Could not queue a packet for processing on socket %u. Reading continues; acquired references are retained.",
                m_hSocket);
        }
        consumed += packetLen;
        mode = SM_READ_LEN;
    }

    Read(0);
}

// L2Authd.exe 0x00497CA0, Src/wsmsocket.cpp:211-227.
void CWSMSocket::OnClose() {
    mode = SM_CLOSE;
    WSMServerMgr* const manager = WSMServerMgr::Inst();
    manager->mWSMServerConnected = false;
    const std::uint32_t peerAddress = ntohl(config.WSMIP.s_addr);
    // Log.AddLog(
    //     LOG_ERROR,
    //     "*close connection CWSMSocket from %s, %x(%x)",
    //     ::inet_ntoa(config.WSMIP),
    //     m_hSocket,
    //     runtime::PointerLow32(this))
    // Log.AddLog(
    //     LOG_WARN,
    //     "component=network event=socket_closed peer=wsm address=%u.%u.%u.%u port=%u socket=%u reason=unspecified reconnect=schedule_pending delay_ms=%u",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.WSMPort)), m_hSocket,
    //     static_cast<std::uint32_t>(config.WSMReconnectInterval))
    // Log.AddLog(
    //     LOG_WARN,
    //     "[WSM] Connection to %u.%u.%u.%u:%u closed on socket %u. Cause not provided. Requesting reconnect after %u ms.",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.WSMPort)), m_hSocket,
    //     static_cast<std::uint32_t>(config.WSMReconnectInterval))
    L2_LOG_WARN(Log,
        // Noise: The generic absence-of-cause phrase adds no diagnostic context.
        // "[WSM] Connection to %u.%u.%u.%u:%u closed on socket %u. Cause not provided. Requesting reconnect after %u ms.",
        "[WSM] Connection to %u.%u.%u.%u:%u closed on socket %u. Requesting reconnect after %u ms.",
        (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        (peerAddress >> 8U) & 255U, peerAddress & 255U,
        static_cast<unsigned int>(static_cast<std::uint16_t>(config.WSMPort)), m_hSocket,
        static_cast<std::uint32_t>(config.WSMReconnectInterval));
    AddRef();
    // runtime::timer_queue::CreateTimer(
    //     &manager->mhWSMServerTimer,
    //     nullptr,
    //     WSMSocketTimerRoutine,
    //     this,
    //     static_cast<std::uint32_t>(
    //         config.WSMReconnectInterval),
    //     0)
    const bool created = runtime::timer_queue::CreateTimer(
        &manager->mhWSMServerTimer,
        nullptr,
        WSMSocketTimerRoutine,
        this,
        static_cast<std::uint32_t>(
            config.WSMReconnectInterval),
        0);
    if (!created) {
        // Log.AddLog(LOG_ERROR,
        //     "component=network event=reconnect_schedule_failed peer=wsm socket=%u delay_ms=%u action=return_without_new_retry references=retained",
        //     m_hSocket, static_cast<std::uint32_t>(config.WSMReconnectInterval))
        // Log.AddLog(LOG_ERROR,
        //     "[WSM] Could not schedule reconnect for socket %u after %u ms. No new retry is scheduled. Acquired references are retained.",
        //     m_hSocket, static_cast<std::uint32_t>(config.WSMReconnectInterval))
        // L2_LOG_ERROR(Log,
        //     "[WSM] Could not schedule reconnect for socket %u after %u ms. No new retry is scheduled. Acquired references are retained.",
        //     m_hSocket, static_cast<std::uint32_t>(config.WSMReconnectInterval))
        L2_LOG_ERROR(Log,
            "[WSM] Could not schedule reconnect to %u.%u.%u.%u:%u for socket %u after %u ms. No new retry is scheduled. Acquired references are retained.",
            (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            (peerAddress >> 8U) & 255U, peerAddress & 255U,
            static_cast<unsigned int>(static_cast<std::uint16_t>(config.WSMPort)), m_hSocket, static_cast<std::uint32_t>(config.WSMReconnectInterval));
    }
}

// L2Authd.exe 0x00497D20, Src/wsmsocket.cpp:231-232.
void CWSMSocket::OnTimerCallback() {}

// L2Authd.exe 0x00497D30, Src/wsmsocket.cpp:303-338.
bool CWSMSocket::Send(const char* format, ...) {
    AddRef();
    if (mode == SM_CLOSE ||
        !WSMServerMgr::Inst()->mWSMServerConnected ||
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
        //     "component=network event=packet_assembly_failed peer=wsm socket=%u capacity_bytes=8188 action=discard_empty_buffer legacy_return=success",
        //     m_hSocket)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[WSM] Could not assemble a packet on socket %u within 8188 bytes. Discarding the empty buffer. The send path still returns success.",
        //     m_hSocket)
        L2_LOG_ERROR(Log,
            "[WSM] Could not assemble a packet on socket %u within 8188 bytes. Discarding the empty buffer; the send path still returns success.",
            m_hSocket);
    } else {
        packetSize += 2;
        packet[2] = static_cast<char>(packetSize);
        packet[3] = static_cast<char>(packetSize >> 8);
        packetSize += 2;
        packet[0] = static_cast<char>(packetSize);
        packet[1] = static_cast<char>(packetSize >> 8);
    }
    buffer->m_size = static_cast<std::uint32_t>(
        packetSize);

    if ((config.usePacketLog & 0x80U) != 0U) {
        WritePacketLog(
            "Auth->WSM",
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

CWSMPacket::CWSMPacket() = default;
CWSMPacket::~CWSMPacket() = default;

// L2Authd.exe 0x00497E90, Src/wsmsocket.cpp:110-126.
CWSMPacket* CWSMPacket::Alloc() {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            runtime::raw_atomic::IncrementI32(
                &CWSMPacketPool::s_NumAlloc)) &
        0x0fU;
    CWSMPacketPool::CSlot& slot =
        CWSMPacketPool::s_slot[slotIndex];
    CWSMPacket* packet = nullptr;
    {
        L2_GUARD(slot.m_lock);
        packet = slot.m_pPacket;
        if (packet != nullptr) {
            slot.m_pPacket =
                reinterpret_cast<CWSMPacket*>(
                    packet->m_pSocket);
        }
    }
    if (packet == nullptr) {
        packet = new CWSMPacket;
    }
    return packet;
}

// L2Authd.exe 0x00497F60, Src/wsmsocket.cpp:130-141.
void CWSMPacket::FreeAll() {
    for (CWSMPacketPool::CSlot& slot :
         CWSMPacketPool::s_slot) {
        {
            L2_GUARD(slot.m_lock);
            while (slot.m_pPacket != nullptr) {
                CWSMPacket* const packet =
                    slot.m_pPacket;
                slot.m_pPacket =
                    reinterpret_cast<CWSMPacket*>(
                        packet->m_pSocket);
                delete packet;
            }
        }
    }
}

// L2Authd.exe 0x00497FF0, Src/wsmsocket.cpp:145-152.
void CWSMPacket::Free() {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            runtime::raw_atomic::DecrementI32(
                &CWSMPacketPool::s_NumFree)) &
        0x0fU;
    CWSMPacketPool::CSlot& slot =
        CWSMPacketPool::s_slot[slotIndex];
    {
        L2_GUARD(slot.m_lock);
        m_pSocket = reinterpret_cast<CIOSocket*>(
            slot.m_pPacket);
        slot.m_pPacket = this;
    }
}

// L2Authd.exe 0x00498040, Src/wsmsocket.cpp:156-179.
void CWSMPacket::OnIOCallback(
    [[maybe_unused]] int bSuccess,
    std::uint32_t dwTransferred,
    [[maybe_unused]] _OVERLAPPED* lpOverlapped) {
    try {
        auto* const packet =
            reinterpret_cast<unsigned char*>(
                m_pBuf->m_buffer + dwTransferred + 2);
        const unsigned int opcode = *packet;
        if (opcode > 6U) {
            // The target returns here without dropping the socket/buffer refs,
            // pending counter, or pooled job object.
            // Log.AddLog(
            //     LOG_ERROR,
            //     "unknown protocol %d",
            //     static_cast<int>(opcode))
            // Log.AddLog(
            //     LOG_ERROR,
            //     "component=network event=invalid_opcode peer=wsm opcode=%u max_opcode=6 action=return_without_dispatch references=retained",
            //     opcode)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "[WSM] Cannot dispatch opcode %u: maximum 6. Returning with packet and socket references retained.",
            //     opcode)
            // L2_LOG_ERROR(Log,
            //     "[WSM] Cannot dispatch opcode %u: maximum 6. Returning with packet and socket references retained.",
            //     opcode)
            L2_LOG_ERROR(Log,
                "[WSM] Cannot dispatch opcode %u on socket %u (%s): maximum 6. Returning with packet and socket references retained.",
                opcode, m_pSocket ? m_pSocket->m_hSocket : 0U,
                m_pSocket ? "available" : "unavailable");
            return;
        }

        m_pFunc = WSMPacketFuncTable[opcode];
        if (opcode == 6U) {
            // The selected table contains literal 0xFFFFFFFF in this admitted
            // slot. Its real operation is an invalid indirect call. Abort is
            // the deterministic cross-platform equivalent, not a new handler.
            // Log.AddLog(LOG_ERROR,
            //     "component=network event=invalid_handler peer=wsm opcode=6 reason=legacy_table_sentinel action=abort_process")
            // Log.AddLog(LOG_ERROR,
            //     "[WSM] Opcode 6 selects an invalid handler table sentinel. Aborting the process.")
            // L2_LOG_ERROR(Log,
            //     "[WSM] Opcode 6 selects an invalid handler table sentinel. Aborting the process.")
            L2_LOG_ERROR(Log,
                "[WSM] Opcode 6 on socket %u (%s) selects an invalid handler table sentinel. Aborting the process.",
                m_pSocket ? m_pSocket->m_hSocket : 0U,
                m_pSocket ? "available" : "unavailable");
            std::abort();
        }
        if (m_pFunc(m_pSocket, packet + 1)) {
            m_pSocket->CloseSocket();
        }
        m_pSocket->ReleaseRef();
        m_pBuf->Release();
        runtime::raw_atomic::DecrementI32(
            &s_PendingPacket);
        Free();
    } catch (...) {
        // ELOG(
        //     "*%s:%d(%s) exception\n",
        //     "WSMSocket.cpp",
        //     177,
        //     "Wed Oct 23 17:02:12 2019")
        ELOG(
            "[WSMSocket.cpp] Exception while dispatching or releasing a web-session packet job. Entering exception handling.\n");
        send_exception(true);
    }
}

// L2Authd.exe 0x00498190, Src/wsmsocket.cpp:24-26.
#if defined(_WIN32)
void __stdcall WSMSocketTimerRoutine(
#else
void WSMSocketTimerRoutine(
#endif
    [[maybe_unused]] void* parameter,
    [[maybe_unused]] std::uint8_t timerOrWaitFired) {
    WSMServerMgr::Inst()->WSMServerTimerRoutine();
}

// L2Authd.exe 0x00498250, Src/wsmsocket.h:77.
CWSMPacketPool::CSlot::CSlot()
    : m_pPacket(nullptr),
      m_lock(eCustomSpinLock, 0) {}

// L2Authd.exe 0x004982E0, Src/wsmsocket.h:82.
CWSMPacketPool::~CWSMPacketPool() {
    CWSMPacket::FreeAll();
}
