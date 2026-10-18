#include "IovationSocket.h"

#include "Shared/src/ScopedLockGuards.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/TimerQueue.h"
#include "Config.h"
#include "IOServer.h"
#include "IovationServerMgr.h"
#include "Log.h"
#include "Thread.h"
#include "Util.h"

#include <cstdarg>
#include <cstdint>
#include <cstdlib>

#if defined(_WIN32)
#include <WinSock2.h>
#else
#include <arpa/inet.h>
#endif

IovationPacketPool::CSlot IovationPacketPool::s_slot[16];
std::int32_t IovationPacketPool::s_NumAlloc = -1;
std::int32_t IovationPacketPool::s_NumFree = 0;
std::int32_t IovationPacket::s_PendingPacket = 0;
IovationPacketPool gIovationPacketPool;

namespace {

IovationSocket::PacketFunction IovationPacketFuncTable[2] = {
    UnknownPacket,
    RecvCheckTransactionDetailAck};

} // namespace

// L2Authd.exe 0x00453470, Src/iovationsocket.cpp:151-156.
IovationSocket::IovationSocket(
    std::uint32_t socket)
    : CIOSocket(socket),
      m_mode(SM_READ_LEN),
      packetTable(IovationPacketFuncTable) {
    AddRef();
    IovationServerMgr::Inst()->mIovationServerConnected = false;
}

// L2Authd.exe 0x00453500, Src/iovationsocket.cpp:160-161.
IovationSocket::~IovationSocket() = default;

// L2Authd.exe 0x00453520, Src/iovationsocket.cpp:144-146.
IovationSocket* IovationSocket::Allocate(
    std::uint32_t socket) {
    return new IovationSocket(socket);
}

// L2Authd.exe 0x004535A0, Src/iovationsocket.cpp:185-190.
void IovationSocket::OnCreate() {
    IovationServerMgr::Inst()->mIovationServerConnected = true;
    const std::uint32_t peerAddress = ntohl(config.iovationProxyIPAddress.s_addr);
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "*new IovationServer connection from %s, %x(%x)",
    //     ::inet_ntoa(config.iovationProxyIPAddress),
    //     m_hSocket,
    //     runtime::PointerLow32(this))
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "component=network event=connected peer=iovation configured_address=%u.%u.%u.%u configured_port=%u socket=%u",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.iovationProxyPort)), m_hSocket)
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "[Iovation] Connected. Configured endpoint %u.%u.%u.%u:%u, socket %u.",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.iovationProxyPort)), m_hSocket)
    L2_LOG_INFO(Log,
        "[Iovation] Connected; configured endpoint %u.%u.%u.%u:%u, socket %u.",
        (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        (peerAddress >> 8U) & 255U, peerAddress & 255U,
        static_cast<unsigned int>(static_cast<std::uint16_t>(config.iovationProxyPort)), m_hSocket);
    OnRead();
}

// L2Authd.exe 0x004535F0, Src/iovationsocket.cpp:194-253.
void IovationSocket::OnRead() {
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
            if (available < consumed + 3) {
                Read(static_cast<std::uint32_t>(
                    available - consumed));
                return;
            }
            packetLen =
                static_cast<int>(packet[consumed]) - 2 +
                static_cast<int>(packet[consumed + 1]) * 256;
            if (packetLen < 1 || packetLen > 0x2000) {
                // Log.AddLog(
                //     LOG_ERROR,
                //     "%d: bad packet size %d",
                //     m_hSocket,
                //     packetLen)
                // Log.AddLog(
                //     LOG_WARN,
                //     "component=network event=invalid_packet_length peer=iovation socket=%u payload_bytes=%d min_bytes=1 max_bytes=8192 action=close_connection",
                //     m_hSocket, packetLen)
                // Log.AddLog(
                //     LOG_WARN,
                //     "[Iovation] Invalid packet on socket %u: body length %d is outside 1..8192 bytes. Closing the connection.",
                //     m_hSocket, packetLen)
                L2_LOG_WARN(Log,
                    "[Iovation] Invalid packet on socket %u: body length %d is outside 1..8192 bytes. Closing the connection.",
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
            //     "component=network event=invalid_read_state peer=iovation socket=%u state=%d action=close_connection",
            //     m_hSocket, static_cast<int>(m_mode))
            // Log.AddLog(LOG_ERROR,
            //     "[Iovation] Invalid read state on socket %u: %d. Closing the connection.",
            //     m_hSocket, static_cast<int>(m_mode))
            L2_LOG_ERROR(Log,
                "[Iovation] Invalid read state on socket %u: %d. Closing the connection.",
                m_hSocket, static_cast<int>(m_mode));
            CloseSocket();
            return;
        }
        if (available < consumed + packetLen) {
            Read(static_cast<std::uint32_t>(
                available - consumed));
            return;
        }
        if (packet[consumed] >= 2) {
            // Log.AddLog(
            //     LOG_ERROR,
            //     "unknown protocol %d",
            //     static_cast<int>(packet[consumed]))
            // Log.AddLog(
            //     LOG_WARN,
            //     "component=network event=invalid_opcode peer=iovation socket=%u opcode=%u max_opcode=1 action=close_connection",
            //     m_hSocket, static_cast<unsigned int>(packet[consumed]))
            // Log.AddLog(
            //     LOG_WARN,
            //     "[Iovation] Invalid packet on socket %u: opcode %u exceeds maximum 1. Closing the connection.",
            //     m_hSocket, static_cast<unsigned int>(packet[consumed]))
            L2_LOG_WARN(Log,
                "[Iovation] Invalid packet on socket %u: opcode %u exceeds maximum 1. Closing the connection.",
                m_hSocket, static_cast<unsigned int>(packet[consumed]));
            CloseSocket();
            return;
        }
        if ((config.usePacketLog & 0x400U) != 0U) {
            WritePacketLog(
                "IovationProxy->Auth",
                m_hSocket,
                packet + consumed,
                packetLen,
                0x400);
        }

        IovationPacket* const packetJob =
            IovationPacket::Alloc();
        packetJob->m_pSocket = this;
        packetJob->m_pBuf = m_pReadBuf;
        AddRef();
        m_pReadBuf->AddRef();
        runtime::raw_atomic::IncrementI32(
            &IovationPacket::s_PendingPacket);
        // packetJob->PostObject(
        //     consumed,
        //     g_hIOCompletionPort)
        const auto posted = packetJob->PostObject(
            consumed,
            g_hIOCompletionPort);
        if (!posted) {
            // Log.AddLog(LOG_ERROR,
            //     "component=network event=packet_dispatch_failed peer=iovation socket=%u action=continue_reading references=retained",
            //     m_hSocket)
            // Log.AddLog(LOG_ERROR,
            //     "[Iovation] Could not queue a packet for processing on socket %u. Reading continues. Acquired references are retained.",
            //     m_hSocket)
            L2_LOG_ERROR(Log,
                "[Iovation] Could not queue a packet for processing on socket %u. Reading continues; acquired references are retained.",
                m_hSocket);
        }
        consumed += packetLen;
        m_mode = SM_READ_LEN;
    }

    Read(0);
}

// L2Authd.exe 0x00453810, Src/iovationsocket.cpp:165-181.
void IovationSocket::OnClose() {
    m_mode = SM_CLOSE;
    IovationServerMgr* const manager =
        IovationServerMgr::Inst();
    manager->mIovationServerConnected = false;
    const std::uint32_t peerAddress = ntohl(config.iovationProxyIPAddress.s_addr);
    // Log.AddLog(
    //     LOG_ERROR,
    //     "*close connection IovationSocket from %s, %x(%x)",
    //     ::inet_ntoa(config.iovationProxyIPAddress),
    //     m_hSocket,
    //     runtime::PointerLow32(this))
    // Log.AddLog(
    //     LOG_WARN,
    //     "component=network event=socket_closed peer=iovation configured_address=%u.%u.%u.%u configured_port=%u socket=%u reason=unspecified reconnect=schedule_pending delay_ms=%u",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.iovationProxyPort)), m_hSocket,
    //     static_cast<std::uint32_t>(config.iovationProxyConnectInterval))
    // Log.AddLog(
    //     LOG_WARN,
    //     "[Iovation] Connection closed. Configured endpoint %u.%u.%u.%u:%u, socket %u. Cause not provided. Requesting reconnect after %u ms.",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.iovationProxyPort)), m_hSocket,
    //     static_cast<std::uint32_t>(config.iovationProxyConnectInterval))
    L2_LOG_WARN(Log,
        // Noise: The generic absence-of-cause phrase adds no diagnostic context.
        // "[Iovation] Connection closed. Configured endpoint %u.%u.%u.%u:%u, socket %u. Cause not provided. Requesting reconnect after %u ms.",
        "[Iovation] Connection closed; configured endpoint %u.%u.%u.%u:%u, socket %u. Requesting reconnect after %u ms.",
        (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        (peerAddress >> 8U) & 255U, peerAddress & 255U,
        static_cast<unsigned int>(static_cast<std::uint16_t>(config.iovationProxyPort)), m_hSocket,
        static_cast<std::uint32_t>(config.iovationProxyConnectInterval));
    AddRef();
    // runtime::timer_queue::CreateTimer(
    //     &manager->mhIovationServerTimer,
    //     nullptr,
    //     IovationSocketTimerRoutine,
    //     this,
    //     static_cast<std::uint32_t>(
    //         config.iovationProxyConnectInterval),
    //     0)
    const bool created = runtime::timer_queue::CreateTimer(
        &manager->mhIovationServerTimer,
        nullptr,
        IovationSocketTimerRoutine,
        this,
        static_cast<std::uint32_t>(
            config.iovationProxyConnectInterval),
        0);
    if (!created) {
        // Log.AddLog(LOG_ERROR,
        //     "component=network event=reconnect_schedule_failed peer=iovation socket=%u delay_ms=%u action=return_without_new_retry references=retained",
        //     m_hSocket, static_cast<std::uint32_t>(config.iovationProxyConnectInterval))
        // Log.AddLog(LOG_ERROR,
        //     "[Iovation] Could not schedule reconnect for socket %u after %u ms. No new retry is scheduled. Acquired references are retained.",
        //     m_hSocket, static_cast<std::uint32_t>(config.iovationProxyConnectInterval))
        // L2_LOG_ERROR(Log,
        //     "[Iovation] Could not schedule reconnect for socket %u after %u ms. No new retry is scheduled. Acquired references are retained.",
        //     m_hSocket, static_cast<std::uint32_t>(config.iovationProxyConnectInterval))
        L2_LOG_ERROR(Log,
            "[Iovation] Could not schedule reconnect to %u.%u.%u.%u:%u for socket %u after %u ms. No new retry is scheduled. Acquired references are retained.",
            (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            (peerAddress >> 8U) & 255U, peerAddress & 255U,
            static_cast<unsigned int>(static_cast<std::uint16_t>(config.iovationProxyPort)), m_hSocket, static_cast<std::uint32_t>(config.iovationProxyConnectInterval));
    }
}

// L2Authd.exe 0x00453890, Src/iovationsocket.cpp:257-287.
bool IovationSocket::Send(
    const char* format,
    ...) {
    AddRef();
    if (m_mode == SM_CLOSE ||
        !IovationServerMgr::Inst()->mIovationServerConnected ||
        m_hSocket == UINT32_C(0xffffffff)) {
        ReleaseRef();
        return false;
    }

    CIOBuffer* const buffer = CIOBuffer::Alloc();
    std::va_list arguments;
    va_start(arguments, format);
    const int payloadLength = Assemble(
        buffer->m_buffer + 2,
        0x1ffe,
        format,
        arguments);
    va_end(arguments);

    std::uint32_t packetSize = 0;
    if (payloadLength == 0) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "%d: assemble too large packet. format %s",
        //     m_hSocket,
        //     format)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=network event=packet_assembly_failed peer=iovation socket=%u capacity_bytes=8190 action=discard_empty_buffer legacy_return=success",
        //     m_hSocket)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[Iovation] Could not assemble a packet on socket %u within 8190 bytes. Discarding the empty buffer. The send path still returns success.",
        //     m_hSocket)
        L2_LOG_ERROR(Log,
            "[Iovation] Could not assemble a packet on socket %u within 8190 bytes. Discarding the empty buffer; the send path still returns success.",
            m_hSocket);
    } else {
        packetSize = static_cast<std::uint32_t>(
            payloadLength + 2);
        buffer->m_buffer[0] =
            static_cast<char>(packetSize);
        buffer->m_buffer[1] =
            static_cast<char>(packetSize >> 8U);
    }
    buffer->m_size = packetSize;

    if ((config.usePacketLog & 0x400U) != 0U) {
        WritePacketLog(
            "Auth->IovationProxy",
            m_hSocket,
            reinterpret_cast<const unsigned char*>(
                buffer->m_buffer + 2),
            static_cast<int>(packetSize) - 2,
            0x400);
    }
    Write(buffer);
    ReleaseRef();
    return true;
}

IovationPacket::IovationPacket() = default;
IovationPacket::~IovationPacket() = default;

// L2Authd.exe 0x004539C0, Src/iovationsocket.cpp:66-82.
IovationPacket* IovationPacket::Alloc() {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            runtime::raw_atomic::IncrementI32(
                &IovationPacketPool::s_NumAlloc)) &
        0x0fU;
    IovationPacketPool::CSlot& slot =
        IovationPacketPool::s_slot[slotIndex];
    IovationPacket* packet = nullptr;
    {
        L2_GUARD(slot.m_lock);
        packet = slot.m_pPacket;
        if (packet != nullptr) {
            slot.m_pPacket =
                reinterpret_cast<IovationPacket*>(
                    packet->m_pSocket);
        }
    }
    if (packet == nullptr) {
        packet = new IovationPacket;
    }
    return packet;
}

// L2Authd.exe 0x00453A90, Src/iovationsocket.cpp:86-97.
void IovationPacket::FreeAll() {
    for (IovationPacketPool::CSlot& slot :
         IovationPacketPool::s_slot) {
        {
            L2_GUARD(slot.m_lock);
            while (slot.m_pPacket != nullptr) {
                IovationPacket* const packet =
                    slot.m_pPacket;
                slot.m_pPacket =
                    reinterpret_cast<IovationPacket*>(
                        packet->m_pSocket);
                delete packet;
            }
        }
    }
}

// L2Authd.exe 0x00453B20, Src/iovationsocket.cpp:101-108.
void IovationPacket::Free() {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            runtime::raw_atomic::DecrementI32(
                &IovationPacketPool::s_NumFree)) &
        0x0fU;
    IovationPacketPool::CSlot& slot =
        IovationPacketPool::s_slot[slotIndex];
    {
        L2_GUARD(slot.m_lock);
        m_pSocket = reinterpret_cast<CIOSocket*>(
            slot.m_pPacket);
        slot.m_pPacket = this;
    }
}

// L2Authd.exe 0x00453B70, Src/iovationsocket.cpp:112-133.
void IovationPacket::OnIOCallback(
    [[maybe_unused]] int bSuccess,
    std::uint32_t dwTransferred,
    [[maybe_unused]] _OVERLAPPED* lpOverlapped) {
    auto* const packet =
        reinterpret_cast<unsigned char*>(
            m_pBuf->m_buffer + dwTransferred);
    const unsigned int opcode = packet[0];
    if (opcode <= 2U) {
        // The PDB records a two-entry table and the selected image stores a
        // null dword immediately after it. OnRead admits only 0/1. Opcode 2
        // would therefore fault in the shipping process.
        if (opcode == 2U) {
            // Log.AddLog(LOG_ERROR,
            //     "component=network event=invalid_handler peer=iovation opcode=2 reason=legacy_null_table_entry action=abort_process")
            // Log.AddLog(LOG_ERROR,
            //     "[Iovation] Opcode 2 selects a null handler table entry. Aborting the process.")
            L2_LOG_ERROR(Log,
                "[Iovation] Opcode 2 selects a null handler table entry. Aborting the process.");
            std::abort();
        }
        m_pFunc = reinterpret_cast<IovationPacketFunc>(
            IovationPacketFuncTable[opcode]);
        if (m_pFunc(m_pSocket, packet + 1)) {
            m_pSocket->CloseSocket();
        }
        m_pSocket->ReleaseRef();
        m_pBuf->Release();
        runtime::raw_atomic::DecrementI32(
            &s_PendingPacket);
        Free();
    } else {
        // The target logs and leaves all three references outstanding on this
        // unreachable corrupt-job path.
        // Log.AddLog(
        //     LOG_ERROR,
        //     "unknown protocol %d",
        //     static_cast<int>(opcode))
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=network event=invalid_opcode peer=iovation socket=%u opcode=%u max_opcode=2 action=return_without_dispatch references=retained",
        //     m_pSocket != nullptr ? m_pSocket->m_hSocket : 0U, opcode)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[Iovation] Cannot dispatch packet on socket %u: opcode %u exceeds maximum 2. Returning with packet and socket references retained.",
        //     m_pSocket != nullptr ? m_pSocket->m_hSocket : 0U, opcode)
        L2_LOG_ERROR(Log,
            "[Iovation] Cannot dispatch packet on socket %u: opcode %u exceeds maximum 2. Returning with packet and socket references retained.",
            m_pSocket != nullptr ? m_pSocket->m_hSocket : 0U, opcode);
    }
}

// L2Authd.exe 0x00453C20, Src/iovationsocket.cpp:22-24.
#if defined(_WIN32)
void __stdcall IovationSocketTimerRoutine(
#else
void IovationSocketTimerRoutine(
#endif
    [[maybe_unused]] void* parameter,
    [[maybe_unused]] std::uint8_t timerOrWaitFired) {
    IovationServerMgr::Inst()->IovationServerTimerRoutine();
}

// L2Authd.exe 0x00453C40, Src/iovationsocket.cpp:34-37.
bool UnknownPacket(
    [[maybe_unused]] IovationSocket* socket,
    [[maybe_unused]] const unsigned char* packet) {
    // Log.AddLog(
    //     LOG_WARN,
    //     "Received unrecognizable packet from IovationProxy")
    // Log.AddLog(
    //     LOG_WARN,
    //     "component=network event=unsupported_opcode peer=iovation socket=%u opcode=0 action=ignore_packet connection=kept",
    //     socket != nullptr ? socket->m_hSocket : 0U)
    // Log.AddLog(
    //     LOG_WARN,
    //     "[Iovation] Ignoring unsupported opcode 0 on socket %u. The connection remains open.",
    //     socket != nullptr ? socket->m_hSocket : 0U)
    L2_LOG_WARN(Log,
        "[Iovation] Ignoring unsupported opcode 0 on socket %u. The connection remains open.",
        socket != nullptr ? socket->m_hSocket : 0U);
    return false;
}

// L2Authd.exe 0x00453C60, Src/iovationsocket.cpp:41-44.
bool RecvCheckTransactionDetailAck(
    IovationSocket* socket,
    const unsigned char* packet) {
    IovationServerMgr::Inst()->RecvCheckTransactionDetailAck(
        socket,
        packet);
    return false;
}

// L2Authd.exe 0x00453C80, Src/iovationsocket.h:59.
IovationPacketPool::CSlot::CSlot()
    : m_pPacket(nullptr),
      m_lock(eCustomSpinLock, 0) {}

// L2Authd.exe 0x00453D10, Src/iovationsocket.h:64.
IovationPacketPool::~IovationPacketPool() {
    IovationPacket::FreeAll();
}
