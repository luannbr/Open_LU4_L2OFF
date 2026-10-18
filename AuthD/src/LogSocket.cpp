#include "LogSocket.h"

#include "Shared/src/ScopedLockGuards.h"

#include "Platform/IOSocketTransport.h"
#include "Shared/src/Runtime/NativeSocketError.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/TimerQueue.h"
#include "Shared/src/Runtime/Time32.h"
#include "Config.h"
#include "Exception.h"
#include "IOServer.h"
#include "Log.h"
#include "Thread.h"
#include "Util.h"

#include <cstdarg>
#include <cstdint>
#include <ctime>

#if defined(_WIN32)
#include <WinSock2.h>
#else
#include <arpa/inet.h>
#endif

std::int32_t CLogPacketServer::g_nPendingPacket = 0;
bool LogDReconnect = false;
bool g_opFlag = false;
void* g_hLogDTimer = nullptr;
std::int32_t CLOGPacketServerPool::g_nAlloc = -1;
std::int32_t CLOGPacketServerPool::g_nFree = 0;
CLOGPacketServerPool::CSlot CLOGPacketServerPool::g_slot[16];
CRWLock gLogLock;
CLOGPacketServerPool theLOGPacketPool;

namespace {

bool LOGDummyPacket(
    [[maybe_unused]] CLogSocket* socket,
    [[maybe_unused]] const unsigned char* packet) {
    // Noise: The dummy callback is a no-op and the kept zero-extended opcode dispatch cannot reach it.
    // Log.AddLog(
    //     LOG_WARN,
    //     "Call DummyPacket What What What")
    return false;
}

using LogSocketPacketFunction = bool (*)(
    CLogSocket*,
    const unsigned char*);

// L2Authd.pdb type 0x85D6 is a four-byte array: the selected x86 image has
// exactly one callback pointer at .data:2036.
LogSocketPacketFunction LOGPacketFuncTable[1] = {
    LOGDummyPacket};

} // namespace

// L2Authd.exe 0x00466190, Src/logsocket.cpp:161-170.
CLogSocket::CLogSocket(
    std::uint32_t sock)
    : CIOSocket(sock) {
    addr = config.LogDIP;
    host = nullptr;
    mode = SM_READ_LEN;
    AddRef();
    LogDAddress.sin_family = AF_INET;
    LogDAddress.sin_addr = config.LogDIP;
    LogDAddress.sin_port = htons(
        static_cast<std::uint16_t>(config.LogDPort));
    packetTable = LOGPacketFuncTable;
}

// L2Authd.exe 0x00466250, Src/logsocket.cpp:155-157.
CLogSocket::~CLogSocket() {
    // Noise: Pointer-only destruction chatter duplicates the connection lifecycle without identifying an operation.
    // Log.AddLog(
    //     LOG_DEBUG,
    //     "DELETE LogSocket 0x%x",
    //     runtime::PointerLow32(this))
}

// L2Authd.exe 0x004662C0, Src/logsocket.cpp:173-175.
CLogSocket* CLogSocket::Allocate(
    std::uint32_t s) {
    return new CLogSocket(s);
}

// L2Authd.exe 0x00466340, Src/logsocket.cpp:244-251.
void CLogSocket::OnCreate() {
    try {
        const std::uint32_t peerAddress = ntohl(addr.s_addr);
        // Log.AddLog(LOG_NORMAL,
        //     "component=network event=connected peer=logd address=%u.%u.%u.%u port=%u socket=%u action=begin_handshake",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(ntohs(LogDAddress.sin_port)), m_hSocket)
        // Log.AddLog(LOG_NORMAL,
        //     "[logd] Connected to %u.%u.%u.%u:%u on socket %u. Starting the handshake.",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(ntohs(LogDAddress.sin_port)), m_hSocket)
        L2_LOG_INFO(Log,
            "[logd] Connected to %u.%u.%u.%u:%u on socket %u. Starting the handshake.",
            (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            (peerAddress >> 8U) & 255U, peerAddress & 255U,
            static_cast<unsigned int>(ntohs(LogDAddress.sin_port)), m_hSocket);
        Send2("cd", 1, 0);
        Send2("cdd", 3, 201, 1);
        g_opFlag = true;
        OnRead();
    } catch (...) {
        // ELOG(
        // "*%s:%d(%s) exception\n",
        // "LogSocket.cpp",
        // 250,
        // "Wed Oct 23 17:02:11 2019")
        ELOG(
            "[LogSocket.cpp] Exception while initializing the LogD connection and handshake. Entering exception handling.\n");
        send_exception(true);
    }
}

// L2Authd.exe 0x00466420, Src/logsocket.cpp:177-241.
void CLogSocket::OnRead() {
    int pi = 0;
    const int ri =
        static_cast<int>(m_pReadBuf->m_size);
    auto* const inBuf =
        reinterpret_cast<unsigned char*>(
            m_pReadBuf->m_buffer);
    int errorNum = 0;

    if (mode == SM_CLOSE) {
        CloseSocket();
        return;
    }

    while (pi < ri) {
        if (mode == SM_READ_LEN) {
            // The target waits for three bytes even though this state reads
            // only the two-byte length prefix.
            if (ri < pi + 3) {
                Read(static_cast<std::uint32_t>(
                    ri - pi));
                return;
            }
            packetLen =
                static_cast<int>(inBuf[pi]) + 1 +
                static_cast<int>(inBuf[pi + 1]) * 256 -
                config.PacketSizeType;
            if (packetLen < 1 || packetLen > 0x2000) {
                errorNum = 1;
                // Log.AddLog(
                //     LOG_ERROR,
                //     "%d: bad packet size %d",
                //     m_hSocket,
                //     packetLen)
                // Log.AddLog(
                //     LOG_WARN,
                //     "component=network event=invalid_packet_length peer=logd socket=%u payload_bytes=%d min_bytes=1 max_bytes=8192 action=close_connection",
                //     m_hSocket, packetLen)
                // Log.AddLog(
                //     LOG_WARN,
                //     "[logd] Invalid packet on socket %u: body length %d is outside 1..8192 bytes. Closing the connection.",
                //     m_hSocket, packetLen)
                L2_LOG_WARN(Log,
                    "[logd] Invalid packet on socket %u: body length %d is outside 1..8192 bytes. Closing the connection.",
                    m_hSocket, packetLen);
                break;
            }
            pi += 2;
            mode = SM_READ_BODY;
            continue;
        }

        if (mode != SM_READ_BODY) {
            // Log.AddLog(LOG_WARN,
            //     "component=network event=invalid_read_state peer=logd socket=%u state=%d action=close_connection",
            //     m_hSocket, static_cast<int>(mode))
            // Log.AddLog(LOG_WARN,
            //     "[logd] Socket %u has invalid read state %d. Closing the connection.",
            //     m_hSocket, static_cast<int>(mode))
            // L2_LOG_WARN(Log,
            //     "[logd] Socket %u has invalid read state %d. Closing the connection.",
            //     m_hSocket, static_cast<int>(mode))
            L2_LOG_ERROR(Log,
                "[logd] Socket %u has invalid read state %d. Closing the connection.",
                m_hSocket, static_cast<int>(mode));
            errorNum = 3;
            break;
        }
        if (ri < pi + packetLen) {
            Read(static_cast<std::uint32_t>(
                ri - pi));
            return;
        }

        const unsigned int opcode = inBuf[pi];
        // Selected-image instructions 0x00466560..0x00466565 perform MOVZX,
        // TEST, JL. So this condition can never be true for the
        // zero-extended byte. Keep the physical packet-job block because it
        // is present in the EXE, while preserving its observed reachability.
        if (static_cast<int>(opcode) < 0) {
            CLogPacketServer* const packetJob =
                CLogPacketServer::Alloc();
            packetJob->m_pSocket = this;
            packetJob->m_pBuf = m_pReadBuf;
            packetJob->m_pFunc =
                reinterpret_cast<CLogPacketServer::LOGPacketFunc>(
                    packetTable[opcode]);
            AddRef();
            m_pReadBuf->AddRef();
            runtime::raw_atomic::IncrementI32(
                &CLogPacketServer::g_nPendingPacket);
            packetJob->PostObject(
                pi,
                g_hIOCompletionPort);
            pi += packetLen;
            mode = SM_READ_LEN;
            continue;
        }

        // Log.AddLog(
        //     LOG_ERROR,
        //     "unknown protocol %d",
        //     static_cast<int>(opcode))
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=network event=opcode_rejected peer=logd socket=%u opcode=%u reason=legacy_dispatch_rejects_inbound_packets action=close_connection",
        //     m_hSocket, opcode)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[logd] Socket %u received opcode %u, but inbound packets are not accepted. Closing the connection.",
        //     m_hSocket, opcode)
        L2_LOG_WARN(Log,
            "[logd] Socket %u received opcode %u, but inbound packets are not accepted. Closing the connection.",
            m_hSocket, opcode);
        errorNum = 2;
        break;
    }

    if (pi >= ri) {
        pi = 0;
        Read(static_cast<std::uint32_t>(pi));
        return;
    }

    actionLog.AddLog(
        LOG_NORMAL,
        "logd server connection close. invalid status and protocol "
        "%s, errorNum :%d\r\n",
        IP(),
        errorNum);
    CloseSocket();
}

// L2Authd.exe 0x00466660, Src/logsocket.cpp:380-409.
void CLogSocket::OnClose() {
    LogDReconnect = true;
    config.UseLogD = false;
    mode = SM_CLOSE;

    const std::time_t ActionTime = std::time(nullptr);
    // const std::tm ActionTm = *std::localtime(&ActionTime)
    const std::tm ActionTm = runtime::crt::LocalTimeOrZero(ActionTime);
    const std::uint32_t peerAddress = ntohl(addr.s_addr);
    // Log.AddLog(
    //     LOG_ERROR,
    //     "*close logd connection from %s, %x(%x)",
    //     IP(),
    //     m_hSocket,
    //     runtime::PointerLow32(this))
    // Log.AddLog(
    //     runtime::raw_atomic::LoadBool(&g_bTerminating) ? LOG_NORMAL : LOG_WARN,
    //     "component=network event=socket_closed peer=logd address=%u.%u.%u.%u port=%u reason=unspecified subsequent_records=local_file reconnect=schedule_pending delay_ms=%u",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(ntohs(LogDAddress.sin_port)), static_cast<std::uint32_t>(config.LogDReconnectInterval))
    // Log.AddLog(
    //     runtime::raw_atomic::LoadBool(&g_bTerminating) ? LOG_NORMAL : LOG_WARN,
    //     "[logd] Disconnected from %u.%u.%u.%u:%u. Cause not provided. Subsequent records go to the local file. Requesting a reconnect timer with a %u ms delay.",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(ntohs(LogDAddress.sin_port)), static_cast<std::uint32_t>(config.LogDReconnectInterval))
    // This callback also closes sockets whose connect attempt failed.
    // Log.AddLog(
    //     runtime::raw_atomic::LoadBool(&g_bTerminating) ? LOG_NORMAL : LOG_WARN,
    //     "[logd] Closing the socket for %u.%u.%u.%u:%u. Subsequent records use local-file logging. Requesting a reconnect timer with a %u ms delay.",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
    //     static_cast<unsigned int>(ntohs(LogDAddress.sin_port)), static_cast<std::uint32_t>(config.LogDReconnectInterval))
    L2_LOG(Log, runtime::raw_atomic::LoadBool(&g_bTerminating) ? LOG_NORMAL : LOG_WARN,
        "[logd] Closing the socket for %u.%u.%u.%u:%u. Subsequent records use local-file logging. Requesting a reconnect timer with a %u ms delay.",
        (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        (peerAddress >> 8U) & 255U, peerAddress & 255U,
        static_cast<unsigned int>(ntohs(LogDAddress.sin_port)), static_cast<std::uint32_t>(config.LogDReconnectInterval));
    actionLog.AddLog(
        LOG_NORMAL,
        "%d-%d-%d %d:%d:%d,main server connection close from "
        "%s, 0x%x\r\n",
        ActionTm.tm_year + 1900,
        ActionTm.tm_mon + 1,
        ActionTm.tm_mday,
        ActionTm.tm_hour,
        ActionTm.tm_min,
        ActionTm.tm_sec,
        IP(),
        m_hSocket);

    AddRef();
    // runtime::timer_queue::CreateTimer(
    //     &g_hLogDTimer,
    //     nullptr,
    //     LOGDTimerRoutine,
    //     this,
    //     static_cast<std::uint32_t>(
    //         config.LogDReconnectInterval),
    //     0)
    const bool created = runtime::timer_queue::CreateTimer(
        &g_hLogDTimer,
        nullptr,
        LOGDTimerRoutine,
        this,
        static_cast<std::uint32_t>(
            config.LogDReconnectInterval),
        0);
    if (!created) {
        // Log.AddLog(LOG_ERROR,
        //     "component=network event=reconnect_schedule_failed peer=logd delay_ms=%u action=return_without_new_retry subsequent_records=local_file references=retained",
        //     static_cast<std::uint32_t>(config.LogDReconnectInterval))
        // Log.AddLog(LOG_ERROR,
        //     "[logd] Could not schedule reconnect after %u ms. No new retry is scheduled. Local-file logging continues and the socket reference is retained.",
        //     static_cast<std::uint32_t>(config.LogDReconnectInterval))
        // Log.AddLog(LOG_ERROR,
        //     "[logd] Could not schedule reconnect to %u.%u.%u.%u:%u after %u ms. No new retry is scheduled. Local-file logging continues and the socket reference is retained.",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(ntohs(LogDAddress.sin_port)),
        //     static_cast<std::uint32_t>(config.LogDReconnectInterval))
        L2_LOG_ERROR(Log,
            "[logd] Could not schedule reconnect to %u.%u.%u.%u:%u after %u ms. No new retry is scheduled; local-file logging continues and the socket reference is retained.",
            (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            (peerAddress >> 8U) & 255U, peerAddress & 255U,
            static_cast<unsigned int>(ntohs(LogDAddress.sin_port)),
            static_cast<std::uint32_t>(config.LogDReconnectInterval));
    }
}

// L2Authd.exe 0x00466760, Src/logsocket.cpp:328-377.
void CLogSocket::OnTimerCallback() {}

// L2Authd.exe 0x00466770, Src/logsocket.cpp:254-285.
void CLogSocket::Send(
    const char* format,
    ...) {
    try {
        AddRef();
        if (mode == SM_CLOSE ||
            LogDReconnect ||
            m_hSocket == UINT32_C(0xffffffff) ||
            !g_opFlag) {
            // Log.AddLog(
            //     LOG_DEBUG,
            //     "logdsocket Call invalid")
            // Log.AddLog(
            //     LOG_WARN,
            //     "component=logging event=record_not_submitted peer=logd socket=%u state=%d reconnect_pending=%d logd_ready_flag=%d reason=connection_unavailable action=drop_record local_fallback=not_attempted",
            //     m_hSocket, static_cast<int>(mode), LogDReconnect, g_opFlag)
            // Log.AddLog(
            //     LOG_WARN,
            //     "[logd] Record dropped on socket %u: connection unavailable (state %d, reconnect pending %d, ready flag %d). This send path does not attempt local-file fallback.",
            //     m_hSocket, static_cast<int>(mode), LogDReconnect, g_opFlag)
            L2_LOG_WARN(Log,
                "[logd] Record dropped on socket %u: connection unavailable (state %d, reconnect pending %d, ready flag %d). This send path does not attempt local-file fallback.",
                m_hSocket, static_cast<int>(mode), LogDReconnect, g_opFlag);
            ReleaseRef();
            return;
        }

        CIOBuffer* const pBuffer = CIOBuffer::Alloc();
        char* const buffer = pBuffer->m_buffer + 2;
        std::va_list ap;
        va_start(ap, format);
        int len = Assemble(
            buffer,
            0x1ffe,
            format,
            ap);
        va_end(ap);

        if (len == 0) {
            // Log.AddLog(
            //     LOG_ERROR,
            //     "%d: assemble too large packet. format %s",
            //     m_hSocket,
            //     format)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "component=logging event=record_not_submitted peer=logd socket=%u reason=packet_assembly_failed capacity_bytes=8190 action=discard_empty_buffer local_fallback=not_attempted",
            //     m_hSocket)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "[logd] Record dropped on socket %u: packet assembly failed within the 8190-byte capacity. Discarding the empty buffer. This send path does not attempt local-file fallback.",
            //     m_hSocket)
            // Original: L2_LOG_ERROR(Log,
            //     "[logd] Record dropped on socket %u: packet assembly failed within the 8190-byte capacity. Discarding the empty buffer. This send path does not attempt local-file fallback.",
            //     m_hSocket)
            L2_LOG_ERROR(Log,
                "LogD record dropped on socket %u: packet assembly failed (format '%s', capacity 8190 bytes). Discarding the empty buffer; no local-file fallback is attempted here.",
                m_hSocket, format);
        } else {
            len = len - 1 + 3;
            buffer[-2] = static_cast<char>(len);
            buffer[-1] = static_cast<char>(len >> 8);
        }
        pBuffer->m_size = static_cast<std::uint32_t>(len);
        Write(pBuffer);
        ReleaseRef();
    } catch (...) {
        // ELOG(
        // "*%s:%d(%s) exception\n",
        // "LogSocket.cpp",
        // 284,
        // "Wed Oct 23 17:02:11 2019")
        ELOG(
            "[LogSocket.cpp] Exception while preparing or submitting a LogD record. Entering exception handling.\n");
        send_exception(true);
    }
}

// L2Authd.exe 0x00466920, Src/logsocket.cpp:288-320.
void CLogSocket::Send2(
    const char* format,
    ...) {
    try {
        AddRef();
        if (mode == SM_CLOSE ||
            LogDReconnect ||
            m_hSocket == UINT32_C(0xffffffff)) {
            // Log.AddLog(
            //     LOG_DEBUG,
            //     "logdsocket Call invalid send2")
            // Log.AddLog(
            //     LOG_WARN,
            //     "component=network event=handshake_not_submitted peer=logd socket=%u state=%d reconnect_pending=%d reason=connection_unavailable action=return_without_send",
            //     m_hSocket, static_cast<int>(mode), LogDReconnect)
            // Log.AddLog(
            //     LOG_WARN,
            //     "[logd] Handshake not sent on socket %u: connection unavailable (state %d, reconnect pending %d).",
            //     m_hSocket, static_cast<int>(mode), LogDReconnect)
            L2_LOG_WARN(Log,
                "[logd] Handshake not sent on socket %u: connection unavailable (state %d, reconnect pending %d).",
                m_hSocket, static_cast<int>(mode), LogDReconnect);
            ReleaseRef();
            return;
        }

        CIOBuffer* const pBuffer = CIOBuffer::Alloc();
        char* const buffer = pBuffer->m_buffer + 2;
        std::va_list ap;
        va_start(ap, format);
        int len = Assemble(
            buffer,
            0x1ffe,
            format,
            ap);
        va_end(ap);

        if (len == 0) {
            // Log.AddLog(
            //     LOG_ERROR,
            //     "%d: assemble too large packet. format %s",
            //     m_hSocket,
            //     format)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "component=network event=handshake_not_submitted peer=logd socket=%u reason=packet_assembly_failed capacity_bytes=8190 action=discard_empty_buffer",
            //     m_hSocket)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "[logd] Handshake not sent on socket %u: packet assembly failed within the 8190-byte capacity. Discarding the empty buffer.",
            //     m_hSocket)
            // Original: L2_LOG_ERROR(Log,
            //     "[logd] Handshake not sent on socket %u: packet assembly failed within the 8190-byte capacity. Discarding the empty buffer.",
            //     m_hSocket)
            L2_LOG_ERROR(Log,
                "LogD handshake not sent on socket %u: packet assembly failed (format '%s', capacity 8190 bytes). Discarding the empty buffer.",
                m_hSocket, format);
        } else {
            len = len - 1 + 3;
            buffer[-2] = static_cast<char>(len);
            buffer[-1] = static_cast<char>(len >> 8);
        }
        pBuffer->m_size = static_cast<std::uint32_t>(len);
        Write(pBuffer);
        ReleaseRef();
    } catch (...) {
        // ELOG(
        // "*%s:%d(%s) exception\n",
        // "LogSocket.cpp",
        // 319,
        // "Wed Oct 23 17:02:11 2019")
        ELOG(
            "[LogSocket.cpp] Exception while preparing or submitting a LogD handshake packet. Entering exception handling.\n");
        send_exception(true);
    }
}

// L2Authd.exe 0x00466AC0, Src/logsocket.cpp:323-325.
const char* CLogSocket::IP() {
    return ::inet_ntoa(addr);
}

CLogPacketServer::CLogPacketServer() = default;
CLogPacketServer::~CLogPacketServer() = default;

// L2Authd.exe 0x00466AE0, Src/logsocket.cpp:85-101.
CLogPacketServer* CLogPacketServer::Alloc() {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            runtime::raw_atomic::IncrementI32(
                &CLOGPacketServerPool::g_nAlloc)) &
        0x0fU;
    CLOGPacketServerPool::CSlot* const pSlot =
        &CLOGPacketServerPool::g_slot[slotIndex];
    CLogPacketServer* newPacket = nullptr;
    {
        L2_GUARD(pSlot->m_lock);
        newPacket = pSlot->m_pPacket;
        if (newPacket != nullptr) {
            pSlot->m_pPacket =
                reinterpret_cast<CLogPacketServer*>(
                    newPacket->m_pSocket);
        }
    }
    if (newPacket == nullptr) {
        newPacket = new CLogPacketServer;
    }
    return newPacket;
}

// L2Authd.exe 0x00466BB0, Src/logsocket.cpp:104-115.
void CLogPacketServer::FreeAll() {
    for (int i = 0; i < 16; ++i) {
        CLOGPacketServerPool::CSlot* const pSlot =
            &CLOGPacketServerPool::g_slot[i];
        {
            L2_GUARD(pSlot->m_lock);
            while (pSlot->m_pPacket != nullptr) {
                CLogPacketServer* const pPacket =
                    pSlot->m_pPacket;
                pSlot->m_pPacket =
                    reinterpret_cast<CLogPacketServer*>(
                        pPacket->m_pSocket);
                delete pPacket;
            }
        }
    }
}

// L2Authd.exe 0x00466C40, Src/logsocket.cpp:118-125.
void CLogPacketServer::Free() {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            runtime::raw_atomic::DecrementI32(
                &CLOGPacketServerPool::g_nFree)) &
        0x0fU;
    CLOGPacketServerPool::CSlot* const pSlot =
        &CLOGPacketServerPool::g_slot[slotIndex];
    {
        L2_GUARD(pSlot->m_lock);
        m_pSocket = reinterpret_cast<CIOSocket*>(
            pSlot->m_pPacket);
        pSlot->m_pPacket = this;
    }
}

// L2Authd.exe 0x00466C90, Src/logsocket.cpp:128-142.
void CLogPacketServer::OnIOCallback(
    [[maybe_unused]] int bSuccess,
    std::uint32_t dwTransferred,
    [[maybe_unused]] _OVERLAPPED* lpOverlapped) {
    try {
        const auto* const packet =
            reinterpret_cast<const unsigned char*>(
                m_pBuf->m_buffer + dwTransferred + 1);
        if (m_pFunc(m_pSocket, packet)) {
            m_pSocket->CloseSocket();
        }
        m_pSocket->ReleaseRef();
        m_pBuf->Release();
        runtime::raw_atomic::DecrementI32(
            &g_nPendingPacket);
        Free();
    } catch (...) {
        // ELOG(
        // "*%s:%d(%s) exception\n",
        // "LogSocket.cpp",
        // 140,
        // "Wed Oct 23 17:02:11 2019")
        ELOG(
            "[LogSocket.cpp] Exception while dispatching or releasing a LogD packet job. Entering exception handling.\n");
        send_exception(true);
    }
}

#if defined(_WIN32)
void __stdcall LOGDTimerRoutine(
#else
void LOGDTimerRoutine(
#endif
    [[maybe_unused]] void* parameter,
    [[maybe_unused]] std::uint8_t timerOrWaitFired) {
    if (g_hLogDTimer != nullptr) {
        runtime::timer_queue::DeleteTimer(
            nullptr,
            g_hLogDTimer);
    }
    g_hLogDTimer = nullptr;
    if (!LogDReconnect) {
        return;
    }

    std::uint32_t LOGSock = UINT32_C(0xffffffff);
    sockaddr_in Destination;
    Destination.sin_family = AF_INET;
    Destination.sin_addr = config.LogDIP;
    Destination.sin_port = htons(
        static_cast<std::uint16_t>(config.LogDPort));
    const std::uint32_t peerAddress = ntohl(Destination.sin_addr.s_addr);
    const int ErrorCode =
        authd::platform::ConnectNativeSocket(
            LOGSock,
            Destination.sin_addr,
            ntohs(Destination.sin_port))
        ? 0
        : -1;
    if (ErrorCode == -1) {
        // Log.AddLog(LOG_WARN,
        //     "component=network event=reconnect_failed peer=logd address=%u.%u.%u.%u port=%u error_domain=winsock_compatible code=%d subsequent_records=local_file action=close_socket reconnect=schedule_pending",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(ntohs(Destination.sin_port)),
        //     ::portable::GetNativeSocketLastError())
        // Log.AddLog(LOG_WARN,
        //     "[logd] Could not reconnect to %u.%u.%u.%u:%u (WinSock error %d). Closing the socket and requesting another retry. Local-file logging continues.",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(ntohs(Destination.sin_port)),
        //     ::portable::GetNativeSocketLastError())
        // Log.AddLog(LOGSock == UINT32_MAX ? LOG_ERROR : LOG_WARN,
        //     "[logd] Could not reconnect to %u.%u.%u.%u:%u (Winsock-compatible error %d). %s Local-file logging continues.",
        //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        //     (peerAddress >> 8U) & 255U, peerAddress & 255U,
        //     static_cast<unsigned int>(ntohs(Destination.sin_port)),
        //     ::portable::GetNativeSocketLastError(),
        //     LOGSock == UINT32_MAX
        //         ? "Socket creation failed. This attempt does not schedule another retry."
        //         : "Closing this socket will request another reconnect timer.")
        L2_LOG(Log, LOGSock == UINT32_MAX ? LOG_ERROR : LOG_WARN,
            "[logd] Could not reconnect to %u.%u.%u.%u:%u (Winsock-compatible error %d). %s Local-file logging continues.",
            (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
            (peerAddress >> 8U) & 255U, peerAddress & 255U,
            static_cast<unsigned int>(ntohs(Destination.sin_port)),
            ::portable::GetNativeSocketLastError(),
            LOGSock == UINT32_MAX
                ? "Socket creation failed; this attempt does not schedule another retry."
                : "Closing this socket will request another reconnect timer.");
    }
    CLogSocket* const tempLogSocket =
        CLogSocket::Allocate(LOGSock);
    tempLogSocket->SetAddress(config.LogDIP);
    if (ErrorCode == -1) {
        tempLogSocket->CloseSocket();
        tempLogSocket->ReleaseRef();
        return;
    }

    {
        L2_WRITE_GUARD(gLogLock);
        CLogSocket* const tmpLogSocket = pLogSocket;
        LogDReconnect = false;
        config.UseLogD = true;
        pLogSocket = tempLogSocket;
        authd::platform::InitializeNativeSocket(
            tempLogSocket,
            LOGSock,
            g_hIOCompletionPortInt);
        tmpLogSocket->ReleaseRef();
    }
}

// L2Authd.exe 0x00466EF0, Src/logsocket.cpp:70.
CLOGPacketServerPool::CSlot::CSlot()
    : m_pPacket(nullptr),
      m_lock(eCustomSpinLock, 0) {}

// L2Authd.exe 0x00466F20, Src/logsocket.cpp:75.
CLOGPacketServerPool::~CLOGPacketServerPool() {
    CLogPacketServer::FreeAll();
}
