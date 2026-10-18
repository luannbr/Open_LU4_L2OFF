#include "GradeSocket.h"

#include "Shared/src/ScopedLockGuards.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/TimerQueue.h"
#include "Config.h"
#include "Exception.h"
#include "GradeServerMgr.h"
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

GradePacketPool::CSlot GradePacketPool::s_slot[16];
std::int32_t GradePacketPool::s_NumAlloc = -1;
std::int32_t GradePacketPool::s_NumFree = 0;
std::int32_t GradePacket::s_PendingPacket = 0;
GradePacketPool gGradePacketPool;

namespace {

} // namespace

// L2Authd.exe 0x004465F0, Src/gradesocket.cpp:126-130.
GradeSocket::GradeSocket(std::uint32_t socket)
    : CIOSocket(socket),
      m_mode(SM_READ_LEN) {
    AddRef();
    GradeServerMgr::Inst()->mGradeServerConnected = false;
}

// L2Authd.exe 0x00446670, Src/gradesocket.cpp:134-135.
GradeSocket::~GradeSocket() = default;

// L2Authd.exe 0x00446690, Src/gradesocket.cpp:119-121.
GradeSocket* GradeSocket::Allocate(std::uint32_t socket) {
    return new GradeSocket(socket);
}

// L2Authd.exe 0x00446710, Src/gradesocket.cpp:159-164.
void GradeSocket::OnCreate() {
    GradeServerMgr::Inst()->mGradeServerConnected = true;
    const std::uint32_t peerAddress = ntohl(config.GradeIP.s_addr);
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "*new GradeServer connection from %s, %x(%x)",
    //     ::inet_ntoa(config.GradeIP),
    //     m_hSocket,
    //     runtime::PointerLow32(this))
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "component=network event=connected peer=grade configured_address=%u.%u.%u.%u configured_port=%u socket=%u",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.GradePort)), m_hSocket)
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "[Grade] Connected. Configured endpoint %u.%u.%u.%u:%u, socket %u.",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.GradePort)), m_hSocket)
    L2_LOG_INFO(Log,
        "[Grade] Connected; configured endpoint %u.%u.%u.%u:%u, socket %u.",
        (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        (peerAddress >> 8U) & 255U, peerAddress & 255U,
        static_cast<unsigned int>(static_cast<std::uint16_t>(config.GradePort)), m_hSocket);
    OnRead();
}

// L2Authd.exe 0x00446760, Src/gradesocket.cpp:168-228.
void GradeSocket::OnRead() {
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
                //     "component=network event=invalid_packet_length peer=grade socket=%u payload_bytes=%d min_bytes=1 max_bytes=8192 action=close_connection",
                //     m_hSocket, packetLen)
                // Log.AddLog(
                //     LOG_WARN,
                //     "[Grade] Invalid packet on socket %u: body length %d is outside 1..8192 bytes. Closing the connection.",
                //     m_hSocket, packetLen)
                L2_LOG_WARN(Log,
                    "[Grade] Invalid packet on socket %u: body length %d is outside 1..8192 bytes. Closing the connection.",
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
            //     "component=network event=invalid_read_state peer=grade socket=%u state=%d action=close_connection",
            //     m_hSocket, static_cast<int>(m_mode))
            // Log.AddLog(LOG_ERROR,
            //     "[Grade] Invalid read state on socket %u: %d. Closing the connection.",
            //     m_hSocket, static_cast<int>(m_mode))
            L2_LOG_ERROR(Log,
                "[Grade] Invalid read state on socket %u: %d. Closing the connection.",
                m_hSocket, static_cast<int>(m_mode));
            CloseSocket();
            return;
        }
        if (available < consumed + packetLen) {
            Read(static_cast<std::uint32_t>(
                available - consumed));
            return;
        }
        if (input[consumed + 2] >= 3U) {
            // The selected image checks the inner opcode at +2, but its log
            // prints the low byte of the inner length at +0.
            // Log.AddLog(
            //     LOG_ERROR,
            //     "unknown protocol %d",
            //     static_cast<int>(input[consumed]))
            // Log.AddLog(
            //     LOG_WARN,
            //     "component=network event=invalid_opcode peer=grade socket=%u opcode=%u max_opcode=2 action=close_connection",
            //     m_hSocket, static_cast<unsigned int>(input[consumed + 2]))
            // Log.AddLog(
            //     LOG_WARN,
            //     "[Grade] Invalid packet on socket %u: opcode %u exceeds maximum 2. Closing the connection.",
            //     m_hSocket, static_cast<unsigned int>(input[consumed + 2]))
            L2_LOG_WARN(Log,
                "[Grade] Invalid packet on socket %u: opcode %u exceeds maximum 2. Closing the connection.",
                m_hSocket, static_cast<unsigned int>(input[consumed + 2]));
            CloseSocket();
            return;
        }
        if ((config.usePacketLog & 0x200U) != 0U) {
            WritePacketLog(
                "GradeServer->Auth",
                m_hSocket,
                input + consumed + 2,
                packetLen - 2,
                0x400);
        }

        GradePacket* const packet = GradePacket::Alloc();
        packet->m_pSocket = this;
        packet->m_pBuf = m_pReadBuf;
        AddRef();
        m_pReadBuf->AddRef();
        runtime::raw_atomic::IncrementI32(
            &GradePacket::s_PendingPacket);
        // packet->PostObject(consumed, g_hIOCompletionPort)
        const auto posted = packet->PostObject(consumed, g_hIOCompletionPort);
        if (!posted) {
            // Log.AddLog(LOG_ERROR,
            //     "component=network event=packet_dispatch_failed peer=grade socket=%u action=continue_reading references=retained",
            //     m_hSocket)
            // Log.AddLog(LOG_ERROR,
            //     "[Grade] Could not queue a packet for processing on socket %u. Reading continues. Acquired references are retained.",
            //     m_hSocket)
            L2_LOG_ERROR(Log,
                "[Grade] Could not queue a packet for processing on socket %u. Reading continues; acquired references are retained.",
                m_hSocket);
        }
        consumed += packetLen;
        m_mode = SM_READ_LEN;
    }

    Read(0);
}

// L2Authd.exe 0x00446980, Src/gradesocket.cpp:139-155.
void GradeSocket::OnClose() {
    m_mode = SM_CLOSE;
    GradeServerMgr* const manager = GradeServerMgr::Inst();
    manager->mGradeServerConnected = false;
    const std::uint32_t peerAddress = ntohl(config.GradeIP.s_addr);
    // Log.AddLog(
    //     LOG_ERROR,
    //     "*close connection GradeSocket from %s, %x(%x)",
    //     ::inet_ntoa(config.GradeIP),
    //     m_hSocket,
    //     runtime::PointerLow32(this))
    // Log.AddLog(
    //     LOG_WARN,
    //     "component=network event=socket_closed peer=grade configured_address=%u.%u.%u.%u configured_port=%u socket=%u reason=unspecified reconnect=schedule_pending delay_ms=%u",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.GradePort)), m_hSocket,
    //     static_cast<std::uint32_t>(config.GradeReconnectInterval))
    // Log.AddLog(
    //     LOG_WARN,
    //     "[Grade] Connection closed. Configured endpoint %u.%u.%u.%u:%u, socket %u. Cause not provided. Requesting reconnect after %u ms.",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.GradePort)), m_hSocket,
    //     static_cast<std::uint32_t>(config.GradeReconnectInterval))
    // L2_LOG_WARN(Log,
    //     "[Grade] Connection closed. Configured endpoint %u.%u.%u.%u:%u, socket %u. Cause not provided. Requesting reconnect after %u ms.",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(static_cast<std::uint16_t>(config.GradePort)), m_hSocket,
    //     static_cast<std::uint32_t>(config.GradeReconnectInterval))
    // Noise: this callback has no failure reason. Connect/read diagnostics carry it when available.
    L2_LOG_WARN(Log,
        "[Grade] Connection closed (configured endpoint %u.%u.%u.%u:%u, socket %u). Requesting reconnect after %u ms.",
        (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        (peerAddress >> 8U) & 255U, peerAddress & 255U,
        static_cast<unsigned int>(static_cast<std::uint16_t>(config.GradePort)), m_hSocket,
        static_cast<std::uint32_t>(config.GradeReconnectInterval));
    AddRef();
    // runtime::timer_queue::CreateTimer(
    //     &manager->mhGradeServerTimer,
    //     nullptr,
    //     GradeSocketTimerRoutine,
    //     this,
    //     static_cast<std::uint32_t>(
    //         config.GradeReconnectInterval),
    //     0)
    const bool created = runtime::timer_queue::CreateTimer(
        &manager->mhGradeServerTimer,
        nullptr,
        GradeSocketTimerRoutine,
        this,
        static_cast<std::uint32_t>(
            config.GradeReconnectInterval),
        0);
    if (!created) {
        // Log.AddLog(LOG_ERROR,
        //     "component=network event=reconnect_schedule_failed peer=grade socket=%u delay_ms=%u action=return_without_new_retry references=retained",
        //     m_hSocket, static_cast<std::uint32_t>(config.GradeReconnectInterval))
        // Log.AddLog(LOG_ERROR,
        //     "[Grade] Could not schedule reconnect for socket %u after %u ms. No new retry is scheduled. Acquired references are retained.",
        //     m_hSocket, static_cast<std::uint32_t>(config.GradeReconnectInterval))
        // L2_LOG_ERROR(Log,
        //     "[Grade] Could not schedule reconnect for socket %u after %u ms. No new retry is scheduled. Acquired references are retained.",
        //     m_hSocket, static_cast<std::uint32_t>(config.GradeReconnectInterval))
        L2_LOG_ERROR(Log,
            "[Grade] Could not schedule reconnect to %u.%u.%u.%u:%u for socket %u after %u ms. No new retry is scheduled. Acquired references are retained.",
            (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            (peerAddress >> 8U) & 255U, peerAddress & 255U,
            static_cast<unsigned int>(static_cast<std::uint16_t>(config.GradePort)), m_hSocket, static_cast<std::uint32_t>(config.GradeReconnectInterval));
    }
}

// L2Authd.exe 0x00446A00, Src/gradesocket.cpp:232-267.
bool GradeSocket::Send(const char* format, ...) {
    AddRef();
    if (m_mode == SM_CLOSE ||
        !GradeServerMgr::Inst()->mGradeServerConnected ||
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
        //     "component=network event=packet_assembly_failed peer=grade socket=%u capacity_bytes=8188 action=discard_empty_buffer legacy_return=success",
        //     m_hSocket)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[Grade] Could not assemble a packet on socket %u within 8188 bytes. Discarding the empty buffer. The send path still returns success.",
        //     m_hSocket)
        L2_LOG_ERROR(Log,
            "[Grade] Could not assemble a packet on socket %u within 8188 bytes. Discarding the empty buffer; the send path still returns success.",
            m_hSocket);
    } else {
        packetSize += 2;
        packet[2] = static_cast<char>(packetSize);
        packet[3] = static_cast<char>(packetSize >> 8);
        packetSize += 2;
        packet[0] = static_cast<char>(packetSize);
        packet[1] = static_cast<char>(packetSize >> 8);
    }
    buffer->m_size = static_cast<std::uint32_t>(packetSize);

    if ((config.usePacketLog & 0x200U) != 0U) {
        WritePacketLog(
            "Auth->GradeServer",
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

GradePacket::GradePacket() = default;
GradePacket::~GradePacket() = default;

// L2Authd.exe 0x00446B60, Src/gradesocket.cpp:40-56.
GradePacket* GradePacket::Alloc() {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            runtime::raw_atomic::IncrementI32(
                &GradePacketPool::s_NumAlloc)) &
        0x0fU;
    GradePacketPool::CSlot& slot =
        GradePacketPool::s_slot[slotIndex];
    GradePacket* packet = nullptr;
    {
        L2_GUARD(slot.m_lock);
        packet = slot.m_pPacket;
        if (packet != nullptr) {
            slot.m_pPacket =
                reinterpret_cast<GradePacket*>(
                    packet->m_pSocket);
        }
    }
    if (packet == nullptr) {
        packet = new GradePacket;
    }
    return packet;
}

// L2Authd.exe 0x00446C30, Src/gradesocket.cpp:60-71.
void GradePacket::FreeAll() {
    for (GradePacketPool::CSlot& slot :
         GradePacketPool::s_slot) {
        {
            L2_GUARD(slot.m_lock);
            while (slot.m_pPacket != nullptr) {
                GradePacket* const packet = slot.m_pPacket;
                slot.m_pPacket =
                    reinterpret_cast<GradePacket*>(
                        packet->m_pSocket);
                delete packet;
            }
        }
    }
}

// L2Authd.exe 0x00446CC0, Src/gradesocket.cpp:75-82.
void GradePacket::Free() {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            runtime::raw_atomic::DecrementI32(
                &GradePacketPool::s_NumFree)) &
        0x0fU;
    GradePacketPool::CSlot& slot =
        GradePacketPool::s_slot[slotIndex];
    {
        L2_GUARD(slot.m_lock);
        m_pSocket = reinterpret_cast<CIOSocket*>(
            slot.m_pPacket);
        slot.m_pPacket = this;
    }
}

// L2Authd.exe 0x00446D10, Src/gradesocket.cpp:86-108.
void GradePacket::OnIOCallback(
    [[maybe_unused]] int bSuccess,
    std::uint32_t dwTransferred,
    [[maybe_unused]] _OVERLAPPED* lpOverlapped) {
    try {
        auto* packet =
            reinterpret_cast<unsigned char*>(
                m_pBuf->m_buffer + dwTransferred + 2);
        if (*packet == 2U) {
            if (GradeServerMgr::Inst()->RecvGrade(
                    m_pSocket,
                    packet + 1)) {
                m_pSocket->CloseSocket();
            }
        } else {
            // Log.AddLog(
            //     LOG_WARN,
            //     "Received unrecognizable packet from GradeServer")
            // Log.AddLog(
            //     LOG_WARN,
            //     "component=network event=unsupported_opcode peer=grade socket=%u opcode=%u action=ignore_packet connection=kept",
            //     m_pSocket != nullptr ? m_pSocket->m_hSocket : 0U,
            //     static_cast<unsigned int>(*packet))
            // Log.AddLog(
            //     LOG_WARN,
            //     "[Grade] Ignoring an unsupported packet on socket %u (opcode %u). The connection remains open.",
            //     m_pSocket != nullptr ? m_pSocket->m_hSocket : 0U,
            //     static_cast<unsigned int>(*packet))
            L2_LOG_WARN(Log,
                "[Grade] Ignoring an unsupported packet on socket %u (opcode %u). The connection remains open.",
                m_pSocket != nullptr ? m_pSocket->m_hSocket : 0U,
                static_cast<unsigned int>(*packet));
        }
        m_pSocket->ReleaseRef();
        m_pBuf->Release();
        runtime::raw_atomic::DecrementI32(
            &s_PendingPacket);
        Free();
    } catch (...) {
        // ELOG(
        //     "*%s:%d(%s) exception\n",
        //     "GradeSocket.cpp",
        //     106,
        //     "Wed Oct 23 17:02:11 2019")
        ELOG(
            "[GradeSocket.cpp] Exception while dispatching or releasing a Grade service packet job. Entering exception handling.\n");
        send_exception(true);
    }
}

// L2Authd.exe 0x00446E40, Src/gradesocket.cpp:22-24.
#if defined(_WIN32)
void __stdcall GradeSocketTimerRoutine(
#else
void GradeSocketTimerRoutine(
#endif
    [[maybe_unused]] void* parameter,
    [[maybe_unused]] std::uint8_t timerOrWaitFired) {
    GradeServerMgr::Inst()->GradeServerTimerRoutine();
}

// L2Authd.exe 0x00446E60, Src/gradesocket.h:71.
GradePacketPool::CSlot::CSlot()
    : m_pPacket(nullptr),
      m_lock(eCustomSpinLock, 0) {}

// L2Authd.exe 0x00446EF0, Src/gradesocket.h:76.
GradePacketPool::~GradePacketPool() {
    GradePacket::FreeAll();
}
