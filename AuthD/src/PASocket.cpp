#include "PASocket.h"

#include "Shared/src/ScopedLockGuards.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/TargetFormatting.h"
#include "Shared/src/Runtime/TimerQueue.h"
#include "Exception.h"
#include "IOServer.h"
#include "Log.h"
#include "PAServerMgr.h"
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

std::int32_t CPAPacket::s_PendingPacket = 0;
std::int32_t CPAPacketPool::s_NumAlloc = -1;
std::int32_t CPAPacketPool::s_NumFree = 0;
CPAPacketPool::CSlot CPAPacketPool::s_slot[16];
CPAPacketPool gPASPacketPool;

namespace {

bool UnknownPacket(
    CPASocket* socket,
    const unsigned char* packet);
bool RecvVerifyAccountAck(
    CPASocket* socket,
    const unsigned char* packet);
bool RecvGetUserInfoAck(
    CPASocket* socket,
    const unsigned char* packet);
bool RecvExternalTokenLoginAck(
    CPASocket* socket,
    const unsigned char* packet);
bool RecvAuxAuthenticationReq(
    CPASocket* socket,
    const unsigned char* packet);
bool RecvExternalUserInfoAck(
    CPASocket* socket,
    const unsigned char* packet);
bool RecvRequestAuthnTokenAck(
    CPASocket* socket,
    const unsigned char* packet);
bool RecvLoginTokenAck(
    CPASocket* socket,
    const unsigned char* packet);

// L2Authd.pdb type 0x82CE is a 23-pointer array. The linked image places a
// literal 0xFFFFFFFF immediately after it, and OnIOCallback admits opcode 23,
// so that value is kept as the final deterministic-abort slot here.
PAPacketHandler PAPacketFuncTable[24] = {
    UnknownPacket,                 // 0
    RecvVerifyAccountAck,          // 1
    UnknownPacket,                 // 2
    UnknownPacket,                 // 3
    RecvGetUserInfoAck,            // 4
    UnknownPacket,                 // 5
    UnknownPacket,                 // 6
    RecvExternalTokenLoginAck,     // 7
    RecvAuxAuthenticationReq,      // 8
    RecvExternalUserInfoAck,       // 9
    UnknownPacket,                 // 10
    UnknownPacket,                 // 11
    UnknownPacket,                 // 12
    UnknownPacket,                 // 13
    UnknownPacket,                 // 14
    UnknownPacket,                 // 15
    UnknownPacket,                 // 16
    UnknownPacket,                 // 17
    UnknownPacket,                 // 18
    UnknownPacket,                 // 19
    RecvRequestAuthnTokenAck,      // 20
    UnknownPacket,                 // 21
    RecvLoginTokenAck,             // 22
    reinterpret_cast<PAPacketHandler>(
        static_cast<std::uintptr_t>(UINT32_MAX))}; // 23

// L2Authd.exe 0x00474970, Src/pasocket.cpp:39-42.
bool UnknownPacket(
    [[maybe_unused]] CPASocket* socket,
    [[maybe_unused]] const unsigned char* packet) {
    // Log.AddLog(
    //     LOG_WARN,
    //     "Received unrecognizable packet from PAServer")
    // Log.AddLog(
    //     LOG_WARN,
    //     "component=network event=unsupported_opcode peer=pa socket=%u action=ignore_packet connection=kept",
    //     socket->m_hSocket)
    // Log.AddLog(
    //     LOG_WARN,
    //     "[PA] Ignoring an unsupported packet on socket %u. The connection remains open.",
    //     socket->m_hSocket)
    // Noise: the dispatcher reports both opcode and socket before this call.
    // L2_LOG_WARN(Log,
    //     "[PA] Ignoring an unsupported packet on socket %u. The connection remains open.",
    //     socket->m_hSocket)
    return false;
}

// L2Authd.exe 0x00474990, Src/pasocket.cpp:46-53.
bool RecvVerifyAccountAck(
    [[maybe_unused]] CPASocket* socket,
    const unsigned char* packet) {
    PAServerMgr::Inst()->RecvVerifyAccountAck(packet);
    return false;
}

// L2Authd.exe 0x004749B0, Src/pasocket.cpp:57-64.
bool RecvGetUserInfoAck(
    [[maybe_unused]] CPASocket* socket,
    const unsigned char* packet) {
    PAServerMgr::Inst()->RecvGetUserInfoAck(packet);
    return false;
}

// L2Authd.exe 0x004749D0, Src/pasocket.cpp:68-75.
bool RecvExternalTokenLoginAck(
    [[maybe_unused]] CPASocket* socket,
    const unsigned char* packet) {
    PAServerMgr::Inst()->RecvExternalTokenLoginAck(packet);
    return false;
}

// L2Authd.exe 0x004749F0, Src/pasocket.cpp:79-86.
bool RecvAuxAuthenticationReq(
    [[maybe_unused]] CPASocket* socket,
    const unsigned char* packet) {
    PAServerMgr::Inst()->RecvAuxAuthenticationReq(packet);
    return false;
}

// L2Authd.exe 0x00474A10, Src/pasocket.cpp:90-97.
bool RecvExternalUserInfoAck(
    [[maybe_unused]] CPASocket* socket,
    const unsigned char* packet) {
    PAServerMgr::Inst()->RecvExternalUserInfoAck(packet);
    return false;
}

// L2Authd.exe 0x00474A30, Src/pasocket.cpp:101-108.
bool RecvRequestAuthnTokenAck(
    [[maybe_unused]] CPASocket* socket,
    const unsigned char* packet) {
    PAServerMgr::Inst()->RecvRequestAuthnTokenAck(packet);
    return false;
}

// L2Authd.exe 0x00474A50, Src/pasocket.cpp:112-119.
bool RecvLoginTokenAck(
    [[maybe_unused]] CPASocket* socket,
    const unsigned char* packet) {
    PAServerMgr::Inst()->RecvLoginTokenAck(packet);
    return false;
}

} // namespace

// L2Authd.exe 0x004740D0, Src/pasocket.cpp:248-255.
CPASocket::CPASocket(
    std::uint32_t socket,
    ConnectionInformation information)
    : CIOSocket(socket),
      hPAReconnectTimer(nullptr),
      host(information.host),
      port(information.port),
      mode(SM_READ_LEN),
      packetTable(PAPacketFuncTable) {
    AddRef();
}

// L2Authd.exe 0x00474170, Src/pasocket.cpp:259-260.
CPASocket::~CPASocket() = default;

// L2Authd.exe 0x00474190, Src/pasocket.cpp:241-243.
CPASocket* CPASocket::Allocate(
    std::uint32_t socket,
    ConnectionInformation information) {
    return new CPASocket(socket, information);
}

// L2Authd.exe 0x00474210, Src/pasocket.cpp:291-296.
void CPASocket::OnCreate() {
    PAServerMgr::Inst()->RegisterPASocketMap(this);
    const std::uint32_t peerAddress = ntohl(host.s_addr);
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "*new PAServer connection from %s:%d, %x(%x)",
    //     ::inet_ntoa(host),
    //     static_cast<int>(port),
    //     m_hSocket,
    //     runtime::PointerLow32(this))
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "component=network event=connected peer=pa address=%u.%u.%u.%u port=%u socket=%u",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(port), m_hSocket)
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "[PA] Connected to %u.%u.%u.%u:%u on socket %u.",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(port), m_hSocket)
    L2_LOG_INFO(Log,
        "[PA] Connected to %u.%u.%u.%u:%u on socket %u.",
        (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        (peerAddress >> 8U) & 255U, peerAddress & 255U,
        static_cast<unsigned int>(port), m_hSocket);
    OnRead();
}

// L2Authd.exe 0x00474270, Src/pasocket.cpp:300-354.
void CPASocket::OnRead() {
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
                //     LOG_WARN,
                //     "component=network event=invalid_packet_length peer=pa socket=%u payload_bytes=%d min_bytes=1 max_bytes=8192 action=close_connection",
                //     m_hSocket, packetLen)
                // Log.AddLog(
                //     LOG_WARN,
                //     "[PA] Invalid packet on socket %u: body length %d is outside 1..8192 bytes. Closing the connection.",
                //     m_hSocket, packetLen)
                L2_LOG_WARN(Log,
                    "[PA] Invalid packet on socket %u: body length %d is outside 1..8192 bytes. Closing the connection.",
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
                "[PA] Invalid read state on socket %u: %d. Closing the connection.",
                m_hSocket, static_cast<int>(mode));
            CloseSocket();
            return;
        }
        if (available < consumed + packetLen) {
            Read(static_cast<std::uint32_t>(
                available - consumed));
            return;
        }
        if ((config.usePacketLog & 0x20U) != 0U) {
            WritePacketLog(
                "PAServer->Auth",
                m_hSocket,
                input + consumed + 2,
                packetLen - 2,
                0x400);
        }

        CPAPacket* const packet = CPAPacket::Alloc();
        packet->m_pSocket = this;
        packet->m_pBuf = m_pReadBuf;
        AddRef();
        m_pReadBuf->AddRef();
        runtime::raw_atomic::IncrementI32(
            &CPAPacket::s_PendingPacket);
        // packet->PostObject(
        //     consumed,
        //     g_hIOCompletionPort)
        const auto posted = packet->PostObject(
            consumed,
            g_hIOCompletionPort);
        if (!posted) {
            // Log.AddLog(LOG_ERROR,
            //     "component=network event=packet_dispatch_failed peer=pa socket=%u action=continue_reading references=retained",
            //     m_hSocket)
            // Log.AddLog(LOG_ERROR,
            //     "[PA] Could not queue a packet for processing on socket %u. Reading continues. Acquired references are retained.",
            //     m_hSocket)
            L2_LOG_ERROR(Log,
                "[PA] Could not queue a packet for processing on socket %u. Reading continues; acquired references are retained.",
                m_hSocket);
        }
        consumed += packetLen;
        mode = SM_READ_LEN;
    }

    Read(0);
}

// L2Authd.exe 0x00474460, Src/pasocket.cpp:264-282.
void CPASocket::OnClose() {
    mode = SM_CLOSE;
    const std::uint32_t peerAddress = ntohl(host.s_addr);
    // Log.AddLog(
    //     LOG_ERROR,
    //     "*close connection CPASocket from %s:%d, %x(%x)",
    //     ::inet_ntoa(host),
    //     static_cast<int>(port),
    //     m_hSocket,
    //     runtime::PointerLow32(this))
    // Log.AddLog(
    //     LOG_WARN,
    //     "component=network event=socket_closed peer=pa address=%u.%u.%u.%u port=%u socket=%u reason=unspecified reconnect=schedule_pending delay_ms=%u",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(port), m_hSocket,
    //     static_cast<std::uint32_t>(config.PAReconnectInterval))
    // Log.AddLog(
    //     LOG_WARN,
    //     "[PA] Connection to %u.%u.%u.%u:%u closed on socket %u. Cause not provided. Requesting reconnect after %u ms.",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(port), m_hSocket,
    //     static_cast<std::uint32_t>(config.PAReconnectInterval))
    L2_LOG_WARN(Log,
        // Noise: The generic absence-of-cause phrase adds no diagnostic context.
        // "[PA] Connection to %u.%u.%u.%u:%u closed on socket %u. Cause not provided. Requesting reconnect after %u ms.",
        "[PA] Connection to %u.%u.%u.%u:%u closed on socket %u. Requesting reconnect after %u ms.",
        (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        (peerAddress >> 8U) & 255U, peerAddress & 255U,
        static_cast<unsigned int>(port), m_hSocket,
        static_cast<std::uint32_t>(config.PAReconnectInterval));
    PAServerMgr::Inst()->UnregisterPASocketMap(this);
    AddRef();
    // runtime::timer_queue::CreateTimer(
    //     &hPAReconnectTimer,
    //     nullptr,
    //     PASocketTimerRoutine,
    //     this,
    //     static_cast<std::uint32_t>(
    //         config.PAReconnectInterval),
    //     0)
    const bool created = runtime::timer_queue::CreateTimer(
        &hPAReconnectTimer,
        nullptr,
        PASocketTimerRoutine,
        this,
        static_cast<std::uint32_t>(
            config.PAReconnectInterval),
        0);
    if (!created) {
        // Log.AddLog(LOG_ERROR,
        //     "component=network event=reconnect_schedule_failed peer=pa socket=%u delay_ms=%u action=return_without_new_retry references=retained",
        //     m_hSocket, static_cast<std::uint32_t>(config.PAReconnectInterval))
        // Log.AddLog(LOG_ERROR,
        //     "[PA] Could not schedule reconnect for socket %u after %u ms. No new retry is scheduled. Acquired references are retained.",
        //     m_hSocket, static_cast<std::uint32_t>(config.PAReconnectInterval))
        // L2_LOG_ERROR(Log,
        //     "[PA] Could not schedule reconnect for socket %u after %u ms. No new retry is scheduled. Acquired references are retained.",
        //     m_hSocket, static_cast<std::uint32_t>(config.PAReconnectInterval))
        L2_LOG_ERROR(Log,
            "[PA] Could not schedule reconnect to %u.%u.%u.%u:%u for socket %u after %u ms. No new retry is scheduled. Acquired references are retained.",
            (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            (peerAddress >> 8U) & 255U, peerAddress & 255U,
            static_cast<unsigned int>(port), m_hSocket, static_cast<std::uint32_t>(config.PAReconnectInterval));
    }
}

// L2Authd.exe 0x004744F0, Src/pasocket.cpp:286-287.
void CPASocket::OnTimerCallback() {}

// L2Authd.exe 0x00474500, Src/pasocket.cpp:358-392.
bool CPASocket::Send(const char* format, ...) {
    AddRef();
    if (mode == SM_CLOSE ||
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
        //     "component=network event=packet_assembly_failed peer=pa socket=%u capacity_bytes=8188 action=discard_empty_buffer legacy_return=success",
        //     m_hSocket)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[PA] Could not assemble a packet on socket %u within 8188 bytes. Discarding the empty buffer. The send path still returns success.",
        //     m_hSocket)
        L2_LOG_ERROR(Log,
            "[PA] Could not assemble a packet on socket %u within 8188 bytes. Discarding the empty buffer; the send path still returns success.",
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

    if ((config.usePacketLog & 0x20U) != 0U) {
        WritePacketLog(
            "Auth->PAServer",
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

// L2Authd.exe 0x00474A70, Src/pasocket.cpp:124-125.
CPAPacket::CPAPacket() = default;

// L2Authd.exe 0x00474AC0, Src/pasocket.cpp:128-129.
CPAPacket::~CPAPacket() = default;

// L2Authd.exe 0x00474650, Src/pasocket.cpp:161-177.
CPAPacket* CPAPacket::Alloc() {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            runtime::raw_atomic::IncrementI32(
                &CPAPacketPool::s_NumAlloc)) &
        0x0fU;
    CPAPacketPool::CSlot& slot =
        CPAPacketPool::s_slot[slotIndex];
    CPAPacket* packet = nullptr;
    {
        L2_GUARD(slot.m_lock);
        packet = slot.m_pPacket;
        if (packet != nullptr) {
            slot.m_pPacket =
                reinterpret_cast<CPAPacket*>(
                    packet->m_pSocket);
        }
    }
    if (packet == nullptr) {
        packet = new CPAPacket;
    }
    return packet;
}

// L2Authd.exe 0x00474720, Src/pasocket.cpp:181-192.
void CPAPacket::FreeAll() {
    for (CPAPacketPool::CSlot& slot :
         CPAPacketPool::s_slot) {
        {
            L2_GUARD(slot.m_lock);
            while (slot.m_pPacket != nullptr) {
                CPAPacket* const packet =
                    slot.m_pPacket;
                slot.m_pPacket =
                    reinterpret_cast<CPAPacket*>(
                        packet->m_pSocket);
                delete packet;
            }
        }
    }
}

// L2Authd.exe 0x004747B0, Src/pasocket.cpp:196-203.
void CPAPacket::Free() {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            runtime::raw_atomic::DecrementI32(
                &CPAPacketPool::s_NumFree)) &
        0x0fU;
    CPAPacketPool::CSlot& slot =
        CPAPacketPool::s_slot[slotIndex];
    {
        L2_GUARD(slot.m_lock);
        m_pSocket = reinterpret_cast<CIOSocket*>(
            slot.m_pPacket);
        slot.m_pPacket = this;
    }
}

// L2Authd.exe 0x00474800, Src/pasocket.cpp:207-230.
void CPAPacket::OnIOCallback(
    [[maybe_unused]] int bSuccess,
    std::uint32_t dwTransferred,
    [[maybe_unused]] _OVERLAPPED* lpOverlapped) {
    try {
        auto* packet =
            reinterpret_cast<unsigned char*>(
                m_pBuf->m_buffer + dwTransferred + 2);
        const unsigned int opcode = *packet++;
        if (opcode > 23U) {
            // The selected image returns here without releasing the job,
            // socket or buffer references and without decrementing pending.
            // Log.AddLog(
            //     LOG_ERROR,
            //     "unknown protocol %d",
            //     static_cast<int>(opcode))
            // Log.AddLog(
            //     LOG_ERROR,
            //     "component=network event=invalid_opcode peer=pa opcode=%u max_opcode=23 action=return_without_dispatch references=retained",
            //     opcode)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "[PA] Cannot dispatch opcode %u: maximum 23. Returning with packet and socket references retained.",
            //     opcode)
            // L2_LOG_ERROR(Log,
            //     "[PA] Cannot dispatch opcode %u: maximum 23. Returning with packet and socket references retained.",
            //     opcode)
            L2_LOG_ERROR(Log,
                "[PA] Cannot dispatch opcode %u on socket %u (%s): maximum 23. Returning with packet and socket references retained.",
                opcode, m_pSocket ? m_pSocket->m_hSocket : 0U, m_pSocket ? "available" : "unavailable");
            return;
        }

        m_pFunc = reinterpret_cast<PAPacketFunc>(
            PAPacketFuncTable[opcode]);
        if (opcode == 23U) {
            // The admitted linked-data slot is literal 0xFFFFFFFF. An abort
            // is the deterministic portable counterpart of that invalid call.
            // Log.AddLog(LOG_ERROR,
            //     "component=network event=invalid_handler peer=pa opcode=23 reason=legacy_table_sentinel action=abort_process")
            // Log.AddLog(LOG_ERROR,
            //     "[PA] Opcode 23 selects an invalid handler table sentinel. Aborting the process.")
            // L2_LOG_ERROR(Log,
            //     "[PA] Opcode 23 selects an invalid handler table sentinel. Aborting the process.")
            L2_LOG_ERROR(Log,
                "[PA] Opcode 23 on socket %u (%s) selects an invalid handler table sentinel. Aborting the process.",
                m_pSocket ? m_pSocket->m_hSocket : 0U, m_pSocket ? "available" : "unavailable");
            std::abort();
        }
        if (PAPacketFuncTable[opcode] == UnknownPacket) {
            L2_LOG_WARN(Log,
                "[PA] Ignoring unsupported opcode %u on socket %u (%s); connection remains open.",
                opcode, m_pSocket ? m_pSocket->m_hSocket : 0U, m_pSocket ? "available" : "unavailable");
        }
        if (m_pFunc(m_pSocket, packet)) {
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
        //     "PASocket.cpp",
        //     228,
        //     "Wed Oct 23 17:02:11 2019")
        ELOG(
            "[PASocket.cpp] Exception while dispatching or releasing a PA packet job. Entering exception handling.\n");
        send_exception(true);
    }
}

// L2Authd.exe 0x00474950, Src/pasocket.cpp:24-27.
#if defined(_WIN32)
void __stdcall PASocketTimerRoutine(
#else
void PASocketTimerRoutine(
#endif
    void* parameter,
    [[maybe_unused]] std::uint8_t timerOrWaitFired) {
    PAServerMgr::Inst()->PAServerTimerRoutine(
        static_cast<CPASocket*>(parameter));
}

// L2Authd.exe 0x00474A90, Src/pasocket.h:78.
CPAPacketPool::CSlot::CSlot()
    : m_pPacket(nullptr),
      m_lock(eCustomSpinLock, 0) {}

// L2Authd.exe 0x00474AE0, Src/pasocket.h:83.
CPAPacketPool::~CPAPacketPool() {
    CPAPacket::FreeAll();
}
