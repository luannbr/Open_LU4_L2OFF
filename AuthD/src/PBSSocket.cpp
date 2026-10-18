#include "PBSSocket.h"

#include "Shared/src/ScopedLockGuards.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/TimerQueue.h"
#include "Config.h"
#include "Exception.h"
#include "IOServer.h"
#include "Log.h"
#include "PBServerMgr.h"
#include "Protocol.h"
#include "Thread.h"
#include "Util.h"

#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#include <WinSock2.h>
#else
#include <arpa/inet.h>
#endif

std::int32_t CPBSPacket::s_PendingPacket = 0;
std::int32_t CPBSPacketPool::s_NumAlloc = -1;
std::int32_t CPBSPacketPool::s_NumFree = 0;
CPBSPacketPool::CSlot CPBSPacketPool::s_slot[16];
CPBSPacketPool gPBSPacketPool;

namespace {

bool UnknownPacket(
    CPBSSocket* socket,
    const unsigned char* packet);
bool RecvStartCharge(
    CPBSSocket* socket,
    const unsigned char* packet);
bool RecvGetValueInfo(
    CPBSSocket* socket,
    const unsigned char* packet);
bool RecvEndChargeReq(
    CPBSSocket* socket,
    const unsigned char* packet);
bool RecvNotifyRemainTime(
    CPBSSocket* socket,
    const unsigned char* packet);
bool RecvNotifyNewItemApplied(
    CPBSSocket* socket,
    const unsigned char* packet);
bool RecvValueAbandon(
    CPBSSocket* socket,
    const unsigned char* packet);
bool RecvStartChargeList(
    CPBSSocket* socket,
    const unsigned char* packet);
bool RecvChargeSuspend(
    CPBSSocket* socket,
    const unsigned char* packet);

// L2Authd.exe 0x00519868 contains these nine function pointers followed by
// literal 0xFFFFFFFF. OnRead accepts opcode 9, so the linked invalid-call
// slot is kept instead of being replaced with an invented handler.
PBSPacketHandler PBSPacketFuncTable[10] = {
    UnknownPacket,                // 0
    RecvStartCharge,              // 1
    RecvGetValueInfo,             // 2
    RecvEndChargeReq,             // 3
    RecvNotifyRemainTime,         // 4
    RecvNotifyNewItemApplied,     // 5
    RecvValueAbandon,             // 6
    RecvStartChargeList,          // 7
    RecvChargeSuspend,            // 8
    reinterpret_cast<PBSPacketHandler>(
        static_cast<std::uintptr_t>(UINT32_MAX))}; // 9

// L2Authd.exe 0x0047AD90, Src/pbssocket.cpp:113-116.
bool UnknownPacket(
    [[maybe_unused]] CPBSSocket* socket,
    [[maybe_unused]] const unsigned char* packet) {
    // Log.AddLog(
    //     LOG_WARN,
    //     "Received unrecognizable packet from PBServer")
    // Log.AddLog(
    //     LOG_WARN,
    //     "component=network event=unsupported_opcode peer=pb socket=%u opcode=0 action=ignore_packet connection=kept",
    //     socket->m_hSocket)
    // Log.AddLog(
    //     LOG_WARN,
    //     "[PBS] Ignoring unsupported opcode 0 on socket %u. The connection remains open.",
    //     socket->m_hSocket)
    L2_LOG_WARN(Log,
        "[PBS] Ignoring unsupported opcode 0 on socket %u. The connection remains open.",
        socket->m_hSocket);
    return false;
}

// L2Authd.exe 0x0047ADD0, Src/pbssocket.cpp:131-139.
bool RecvStartCharge(
    [[maybe_unused]] CPBSSocket* socket,
    const unsigned char* packet) {
    PBServerMgr::Inst()->RecvStartCharge(packet);
    return false;
}

// L2Authd.exe 0x0047ADB0, Src/pbssocket.cpp:120-127.
bool RecvGetValueInfo(
    [[maybe_unused]] CPBSSocket* socket,
    const unsigned char* packet) {
    PBServerMgr::Inst()->RecvGetValueInfo(packet);
    return false;
}

// L2Authd.exe 0x0047ADF0, Src/pbssocket.cpp:143-152.
bool RecvEndChargeReq(
    [[maybe_unused]] CPBSSocket* socket,
    const unsigned char* packet) {
    const int uid = static_cast<int>(
        GetInt64FromPacket(&packet));
    PBServerMgr::Inst()->RecvEndChargeReq(uid, 0x22);
    return false;
}

// L2Authd.exe 0x0047AE20, Src/pbssocket.cpp:156-173.
bool RecvNotifyRemainTime(
    [[maybe_unused]] CPBSSocket* socket,
    const unsigned char* packet) {
    const int uid = static_cast<int>(
        GetInt64FromPacket(&packet));
    [[maybe_unused]] const int gameAccountNo =
        GetIntFromPacket(&packet);
    const int remainTime = GetIntFromPacket(&packet);
    const int reservations = GetIntFromPacket(&packet);
    [[maybe_unused]] const char valueType =
        GetCharFromPacket(&packet);
    [[maybe_unused]] const int itemId =
        GetIntFromPacket(&packet);
    [[maybe_unused]] const char valueKind =
        GetCharFromPacket(&packet);
    const char computationBasis =
        GetCharFromPacket(&packet);
    PBServerMgr::Inst()->RecvNotifyRemainTime(
        uid,
        remainTime,
        reservations,
        computationBasis);
    return false;
}

// L2Authd.exe 0x0047AED0, Src/pbssocket.cpp:177-197.
bool RecvNotifyNewItemApplied(
    [[maybe_unused]] CPBSSocket* socket,
    const unsigned char* packet) {
    const int uid = static_cast<int>(
        GetInt64FromPacket(&packet));
    const int remainTime = GetIntFromPacket(&packet);
    [[maybe_unused]] const int gameAccountNo =
        GetIntFromPacket(&packet);
    const int reservations = GetIntFromPacket(&packet);
    const char valueType = GetCharFromPacket(&packet);
    const int itemId = GetIntFromPacket(&packet);
    unsigned char valueId[16]{};
    GetBinFromPacket(&packet, 16, valueId);
    const char computationBasis =
        GetCharFromPacket(&packet);
    PBServerMgr::Inst()->RecvNotifyNewItemApplied(
        uid,
        remainTime,
        reservations,
        valueType,
        valueId,
        computationBasis,
        itemId);
    return false;
}

// L2Authd.exe 0x0047AFC0, Src/pbssocket.cpp:201-213.
bool RecvValueAbandon(
    [[maybe_unused]] CPBSSocket* socket,
    const unsigned char* packet) {
    const int uid = static_cast<int>(
        GetInt64FromPacket(&packet));
    [[maybe_unused]] const int gameAccountNo =
        GetIntFromPacket(&packet);
    const char reason = GetCharFromPacket(&packet);
    PBServerMgr::Inst()->RecvValueAbandon(uid, reason);
    return false;
}

// L2Authd.exe 0x0047B010, Src/pbssocket.cpp:217-224.
bool RecvStartChargeList(
    [[maybe_unused]] CPBSSocket* socket,
    const unsigned char* packet) {
    PBServerMgr::Inst()->RecvStartChargeList(packet);
    return false;
}

// L2Authd.exe 0x0047B030, Src/pbssocket.cpp:228-246.
bool RecvChargeSuspend(
    [[maybe_unused]] CPBSSocket* socket,
    const unsigned char* packet) {
    const int uid = static_cast<int>(
        GetInt64FromPacket(&packet));
    [[maybe_unused]] const int gameAccountNo =
        GetIntFromPacket(&packet);
    const char reason = GetCharFromPacket(&packet);
    unsigned char valueId[16]{};
    GetBinFromPacket(&packet, 16, valueId);
    [[maybe_unused]] const char resultKind =
        GetCharFromPacket(&packet);
    [[maybe_unused]] const int restrictionNo =
        GetIntFromPacket(&packet);
    [[maybe_unused]] const int result =
        GetIntFromPacket(&packet);
    PBServerMgr::Inst()->RecvChargeSuspend(uid, reason);
    return false;
}

} // namespace

// L2Authd.exe 0x0047A520, Src/pbssocket.cpp:286-291.
CPBSSocket::CPBSSocket(std::uint32_t socket)
    : CIOSocket(socket),
      mode(SM_READ_LEN),
      packetTable(PBSPacketFuncTable) {
    AddRef();
    PBServerMgr::Inst()->mPBServerConnected = false;
}

// L2Authd.exe 0x0047A5B0, Src/pbssocket.cpp:295-296.
CPBSSocket::~CPBSSocket() = default;

// L2Authd.exe 0x0047A5D0, Src/pbssocket.cpp:279-281.
CPBSSocket* CPBSSocket::Allocate(std::uint32_t socket) {
    return new CPBSSocket(socket);
}

// L2Authd.exe 0x0047A650, Src/pbssocket.cpp:326-334.
void CPBSSocket::OnCreate() {
    PBServerMgr::Inst()->mPBServerConnected = true;
    const std::uint32_t peerAddress = ntohl(config.PBSIP.s_addr);
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "*new PBServer connection from %s, %x(%x)",
    //     ::inet_ntoa(config.PBSIP),
    //     m_hSocket,
    //     runtime::PointerLow32(this))
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "component=network event=connected peer=pb address=%u.%u.%u.%u port=%u socket=%u",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.PBSPort)), m_hSocket)
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "[PBS] Connected to %u.%u.%u.%u:%u on socket %u.",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.PBSPort)), m_hSocket)
    L2_LOG_INFO(Log,
        "[PBS] Connected to %u.%u.%u.%u:%u on socket %u.",
        (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        (peerAddress >> 8U) & 255U, peerAddress & 255U,
        static_cast<unsigned int>(static_cast<std::uint16_t>(config.PBSPort)), m_hSocket);
    // Send("cd", AV_PROTOCOL_VER, config.vmGameId)
    const bool sent = Send("cd", AV_PROTOCOL_VER, config.vmGameId);
    if (!sent) {
        // Log.AddLog(LOG_WARN,
        //     "component=network event=handshake_not_submitted peer=pb socket=%u reason=socket_send_rejected action=continue_reading",
        //     m_hSocket)
        // Log.AddLog(LOG_WARN,
        //     "[PBS] Could not submit the handshake on socket %u: the send was rejected. Reading continues.",
        //     m_hSocket)
        L2_LOG_WARN(Log,
            "[PBS] Could not submit the handshake on socket %u: the send was rejected. Reading continues.",
            m_hSocket);
    }
    OnRead();
}

// L2Authd.exe 0x0047A6C0, Src/pbssocket.cpp:338-399.
void CPBSSocket::OnRead() {
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
                //     "component=network event=invalid_packet_length peer=pb socket=%u payload_bytes=%d min_bytes=1 max_bytes=8192 action=close_connection",
                //     m_hSocket, packetLen)
                // Log.AddLog(
                //     LOG_WARN,
                //     "[PBS] Invalid packet on socket %u: body length %d is outside 1..8192 bytes. Closing the connection.",
                //     m_hSocket, packetLen)
                L2_LOG_WARN(Log,
                    "[PBS] Invalid packet on socket %u: body length %d is outside 1..8192 bytes. Closing the connection.",
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
                "[PBS] Invalid read state on socket %u: %d. Closing the connection.",
                m_hSocket, static_cast<int>(mode));
            CloseSocket();
            return;
        }
        if (available < consumed + packetLen) {
            Read(static_cast<std::uint32_t>(
                available - consumed));
            return;
        }

        const unsigned int opcode = input[consumed];
        if (opcode > 9U) {
            // Log.AddLog(
            //     LOG_ERROR,
            //     "unknown protocol %d",
            //     static_cast<int>(opcode))
            // Log.AddLog(
            //     LOG_WARN,
            //     "component=network event=invalid_opcode peer=pb socket=%u opcode=%u max_opcode=9 action=close_connection",
            //     m_hSocket, opcode)
            // Log.AddLog(
            //     LOG_WARN,
            //     "[PBS] Invalid packet on socket %u: opcode %u exceeds maximum 9. Closing the connection.",
            //     m_hSocket, opcode)
            L2_LOG_WARN(Log,
                "[PBS] Invalid packet on socket %u: opcode %u exceeds maximum 9. Closing the connection.",
                m_hSocket, opcode);
            CloseSocket();
            return;
        }
        if ((config.usePacketLog & 8U) != 0U) {
            WritePacketLog(
                "PBServer->Auth",
                m_hSocket,
                input + consumed,
                packetLen,
                0x400);
        }

        CPBSPacket* const packet = CPBSPacket::Alloc();
        packet->m_pSocket = this;
        packet->m_pBuf = m_pReadBuf;
        packet->m_pFunc = reinterpret_cast<PBSPacketFunc>(
            packetTable[opcode]);
        AddRef();
        m_pReadBuf->AddRef();
        runtime::raw_atomic::IncrementI32(
            &CPBSPacket::s_PendingPacket);
        // packet->PostObject(
        //     consumed,
        //     g_hIOCompletionPort)
        const auto posted = packet->PostObject(
            consumed,
            g_hIOCompletionPort);
        if (!posted) {
            // Log.AddLog(LOG_ERROR,
            //     "component=network event=packet_dispatch_failed peer=pb socket=%u action=continue_reading references=retained",
            //     m_hSocket)
            // Log.AddLog(LOG_ERROR,
            //     "[PBS] Could not queue a packet for processing on socket %u. Reading continues. Acquired references are retained.",
            //     m_hSocket)
            L2_LOG_ERROR(Log,
                "[PBS] Could not queue a packet for processing on socket %u. Reading continues; acquired references are retained.",
                m_hSocket);
        }
        consumed += packetLen;
        mode = SM_READ_LEN;
    }

    Read(0);
}

// L2Authd.exe 0x0047A8F0, Src/pbssocket.cpp:300-317.
void CPBSSocket::OnClose() {
    mode = SM_CLOSE;
    PBServerMgr* const manager = PBServerMgr::Inst();
    manager->mPBServerConnected = false;
    manager->mPBServerReConnect = false;
    const std::uint32_t peerAddress = ntohl(config.PBSIP.s_addr);
    // Log.AddLog(
    //     LOG_ERROR,
    //     "*close connection CPBSSocket from %s, %x(%x)",
    //     ::inet_ntoa(config.PBSIP),
    //     m_hSocket,
    //     runtime::PointerLow32(this))
    // Log.AddLog(
    //     LOG_WARN,
    //     "component=network event=socket_closed peer=pb address=%u.%u.%u.%u port=%u socket=%u reason=unspecified reconnect=schedule_pending delay_ms=%u",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.PBSPort)), m_hSocket,
    //     static_cast<std::uint32_t>(config.PBSReconnectInterval))
    // Log.AddLog(
    //     LOG_WARN,
    //     "[PBS] Connection to %u.%u.%u.%u:%u closed on socket %u. Cause not provided. Requesting reconnect after %u ms.",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.PBSPort)), m_hSocket,
    //     static_cast<std::uint32_t>(config.PBSReconnectInterval))
    L2_LOG_WARN(Log,
        // Noise: The generic absence-of-cause phrase adds no diagnostic context.
        // "[PBS] Connection to %u.%u.%u.%u:%u closed on socket %u. Cause not provided. Requesting reconnect after %u ms.",
        "[PBS] Connection to %u.%u.%u.%u:%u closed on socket %u. Requesting reconnect after %u ms.",
        (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        (peerAddress >> 8U) & 255U, peerAddress & 255U,
        static_cast<unsigned int>(static_cast<std::uint16_t>(config.PBSPort)), m_hSocket,
        static_cast<std::uint32_t>(config.PBSReconnectInterval));
    AddRef();
    // runtime::timer_queue::CreateTimer(
    //     &manager->mhPBServerTimer,
    //     nullptr,
    //     PBSSocketTimerRoutine,
    //     this,
    //     static_cast<std::uint32_t>(
    //         config.PBSReconnectInterval),
    //     0)
    const bool created = runtime::timer_queue::CreateTimer(
        &manager->mhPBServerTimer,
        nullptr,
        PBSSocketTimerRoutine,
        this,
        static_cast<std::uint32_t>(
            config.PBSReconnectInterval),
        0);
    if (!created) {
        // Log.AddLog(LOG_ERROR,
        //     "component=network event=reconnect_schedule_failed peer=pb socket=%u delay_ms=%u action=return_without_new_retry references=retained",
        //     m_hSocket, static_cast<std::uint32_t>(config.PBSReconnectInterval))
        // Log.AddLog(LOG_ERROR,
        //     "[PBS] Could not schedule reconnect for socket %u after %u ms. No new retry is scheduled. Acquired references are retained.",
        //     m_hSocket, static_cast<std::uint32_t>(config.PBSReconnectInterval))
        // L2_LOG_ERROR(Log,
        //     "[PBS] Could not schedule reconnect for socket %u after %u ms. No new retry is scheduled. Acquired references are retained.",
        //     m_hSocket, static_cast<std::uint32_t>(config.PBSReconnectInterval))
        L2_LOG_ERROR(Log,
            "[PBS] Could not schedule reconnect to %u.%u.%u.%u:%u for socket %u after %u ms. No new retry is scheduled. Acquired references are retained.",
            (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            (peerAddress >> 8U) & 255U, peerAddress & 255U,
            static_cast<unsigned int>(static_cast<std::uint16_t>(config.PBSPort)), m_hSocket, static_cast<std::uint32_t>(config.PBSReconnectInterval));
    }
}

// L2Authd.exe 0x0047A980, Src/pbssocket.cpp:321-322.
void CPBSSocket::OnTimerCallback() {}

// L2Authd.exe 0x0047A990, Src/pbssocket.cpp:403-435.
bool CPBSSocket::Send(const char* format, ...) {
    AddRef();
    if (mode == SM_CLOSE ||
        !PBServerMgr::Inst()->mPBServerConnected ||
        m_hSocket == UINT32_C(0xffffffff)) {
        ReleaseRef();
        return false;
    }

    CIOBuffer* const buffer = CIOBuffer::Alloc();
    char* const packet = buffer->m_buffer;
    std::va_list arguments;
    va_start(arguments, format);
    int packetSize = Assemble(
        packet + 2,
        0x1ffe,
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
        //     "component=network event=packet_assembly_failed peer=pb socket=%u capacity_bytes=8190 action=discard_empty_buffer legacy_return=success",
        //     m_hSocket)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[PBS] Could not assemble a packet on socket %u within 8190 bytes. Discarding the empty buffer. The send path still returns success.",
        //     m_hSocket)
        L2_LOG_ERROR(Log,
            "[PBS] Could not assemble a packet on socket %u within 8190 bytes. Discarding the empty buffer; the send path still returns success.",
            m_hSocket);
    } else {
        packetSize += 2;
        packet[0] = static_cast<char>(packetSize);
        packet[1] = static_cast<char>(packetSize >> 8);
    }
    buffer->m_size = static_cast<std::uint32_t>(
        packetSize);

    if ((config.usePacketLog & 8U) != 0U) {
        WritePacketLog(
            "Auth->PBServer",
            m_hSocket,
            reinterpret_cast<const unsigned char*>(
                packet + 2),
            packetSize - 2,
            0x400);
    }
    Write(buffer);
    ReleaseRef();
    return true;
}

// L2Authd.exe 0x0047B100, Src/pbssocket.cpp:106-107.
CPBSPacket::CPBSPacket() = default;

// L2Authd.exe 0x0047B150, Src/pbssocket.cpp:109-110.
CPBSPacket::~CPBSPacket() = default;

// L2Authd.exe 0x0047AAC0, Src/pbssocket.cpp:42-58.
CPBSPacket* CPBSPacket::Alloc() {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            runtime::raw_atomic::IncrementI32(
                &CPBSPacketPool::s_NumAlloc)) &
        0x0fU;
    CPBSPacketPool::CSlot& slot =
        CPBSPacketPool::s_slot[slotIndex];
    CPBSPacket* packet = nullptr;
    {
        L2_GUARD(slot.m_lock);
        packet = slot.m_pPacket;
        if (packet != nullptr) {
            slot.m_pPacket =
                reinterpret_cast<CPBSPacket*>(
                    packet->m_pSocket);
        }
    }
    if (packet == nullptr) {
        packet = new CPBSPacket;
    }
    return packet;
}

// L2Authd.exe 0x0047AB90, Src/pbssocket.cpp:62-73.
void CPBSPacket::FreeAll() {
    for (CPBSPacketPool::CSlot& slot :
         CPBSPacketPool::s_slot) {
        {
            L2_GUARD(slot.m_lock);
            while (slot.m_pPacket != nullptr) {
                CPBSPacket* const packet =
                    slot.m_pPacket;
                slot.m_pPacket =
                    reinterpret_cast<CPBSPacket*>(
                        packet->m_pSocket);
                delete packet;
            }
        }
    }
}

// L2Authd.exe 0x0047AC20, Src/pbssocket.cpp:77-84.
void CPBSPacket::Free() {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            runtime::raw_atomic::DecrementI32(
                &CPBSPacketPool::s_NumFree)) &
        0x0fU;
    CPBSPacketPool::CSlot& slot =
        CPBSPacketPool::s_slot[slotIndex];
    {
        L2_GUARD(slot.m_lock);
        m_pSocket = reinterpret_cast<CIOSocket*>(
            slot.m_pPacket);
        slot.m_pPacket = this;
    }
}

// L2Authd.exe 0x0047AC70, Src/pbssocket.cpp:88-102.
void CPBSPacket::OnIOCallback(
    [[maybe_unused]] int bSuccess,
    std::uint32_t dwTransferred,
    [[maybe_unused]] _OVERLAPPED* lpOverlapped) {
    try {
        const unsigned int opcode =
            static_cast<unsigned char>(
                m_pBuf->m_buffer[dwTransferred]);
        if (opcode == 9U) {
            // Slot 9 is the linked literal 0xFFFFFFFF. Abort is the stable
            // portable counterpart of the target's invalid indirect call.
            // Log.AddLog(LOG_ERROR,
            //     "component=network event=invalid_handler peer=pb opcode=9 reason=legacy_table_sentinel action=abort_process")
            // Log.AddLog(LOG_ERROR,
            //     "[PBS] Opcode 9 selects an invalid handler table sentinel. Aborting the process.")
            L2_LOG_ERROR(Log,
                "[PBS] Opcode 9 selects an invalid handler table sentinel. Aborting the process.");
            std::abort();
        }
        auto* const packet =
            reinterpret_cast<unsigned char*>(
                m_pBuf->m_buffer + dwTransferred + 1);
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
        //     "PBSSocket.cpp",
        //     100,
        //     "Wed Oct 23 17:02:11 2019")
        ELOG(
            "[PBSSocket.cpp] Exception while dispatching or releasing a PBS packet job (buffer offset %u). Entering exception handling.\n",
            dwTransferred);
        send_exception(true);
    }
}

// L2Authd.exe 0x0047AD70, Src/pbssocket.cpp:24-26.
#if defined(_WIN32)
void __stdcall PBSSocketTimerRoutine(
#else
void PBSSocketTimerRoutine(
#endif
    [[maybe_unused]] void* parameter,
    [[maybe_unused]] std::uint8_t timerOrWaitFired) {
    PBServerMgr::Inst()->PBServerTimerRoutine();
}

// L2Authd.exe 0x0047B120, Src/pbssocket.h:78.
CPBSPacketPool::CSlot::CSlot()
    : m_pPacket(nullptr),
      m_lock(eCustomSpinLock, 0) {}

// L2Authd.exe 0x0047B170, Src/pbssocket.h:83.
CPBSPacketPool::~CPBSPacketPool() {
    CPBSPacket::FreeAll();
}
