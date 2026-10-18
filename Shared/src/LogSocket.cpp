#include "Shared/src/LogSocket.h"

#include "L2Core/src/inc/Guard.h"
#if defined(L2_BUILD_CACHED)
#include "CacheSocket.h"
#include "UserDb.h"
#elif defined(L2_BUILD_L2SERVER)
#include "Default.h"
#elif defined(L2_BUILD_L2NPC)
#include "L2NPC/src/Default.h"
#else
#error "Shared/logsocket.cpp must be compiled for a concrete server process"
#endif
#include "Shared/src/Runtime/LocalLogScope.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/IOC.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogCli.h"
#include "Shared/src/IOCPacket.h"

#include <cstdint>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

LogPacketFunc logdbPacketTable[3]{LogSendMsgPacket, LogCheckStatus, LogTestPacket};

CLogSocket::CLogSocket(std::uint64_t s, LogPacketFunc* aPacketTable) : CLogSocketBase(s, 0x1000) {
    L2_GUARD_FUNCTION();

    threadCount = 0;
    mode = SocketMode::SM_READ_LEN;
    packetLen = 0;
    sock = s;
    packetTable = aPacketTable;
}

CLogSocket::~CLogSocket() {
    // Current L2NPC.exe 0x00461270 and Cached.exe scalar deleting destructor
    // 0x004f0a94 clear the callback table before CIOSocket teardown.
    packetTable = nullptr;
}

void CLogSocket::Send(const char* format, ...) {
    L2_GUARD_FUNCTION();
    const runtime::log::LocalLogScope localDiagnostics;

    if (mode == SocketMode::SM_CLOSE) {
        return;
    }

    auto* const buffer = DefaultIOBuffer::Alloc();
    va_list arguments;
    va_start(arguments, format);
    // const int bodySize =
    //     ::VAssemble(
    //         reinterpret_cast<char*>(
    //             buffer->m_Buffer.data() + 2),
    //         0x4ffe,
    //         format,
    //         arguments)
    int bodySize;
    try {
        bodySize = ::VAssemble(reinterpret_cast<char*>(buffer->m_Buffer.data() + 2), 0x4ffe, format, arguments);
    } catch (...) {
        va_end(arguments);
        throw;
    }
    va_end(arguments);

    if (bodySize == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[LogSocket] %d: assemble too large "
        //     L"packet. format %s",
        //     runtime::LegacySocketImportAddressToken(),
        //     format)
        // Log.Add(LOG_ERROR,
        //     "[LogD] Could not assemble the outgoing record within %d bytes "
        //     "(format '%s'). No record bytes will be sent.",
        //     kMaximumBodySize, format)
        L2_LOG_ERROR(
            Log,
            "[LogD] Could not assemble the outgoing record within %d bytes "
            "(format '%s'); no record bytes will be sent.",
            kMaximumBodySize, format
        );
        buffer->m_dwSize = 0;
    } else {
        const std::uint16_t frameSize = static_cast<std::uint16_t>(bodySize + 2);
        buffer->m_Buffer[0] = static_cast<std::uint8_t>(frameSize);
        buffer->m_Buffer[1] = static_cast<std::uint8_t>(frameSize >> 8U);
        buffer->m_dwSize = frameSize;
    }
    Write(buffer);
}

void CLogSocket::OnRead() {
    L2_GUARD_FUNCTION();
    const runtime::log::LocalLogScope localDiagnostics;

    if (mode == SocketMode::SM_CLOSE) {
        Close();
        return;
    }

    const std::int32_t readSize = static_cast<std::int32_t>(m_pReadBuf->m_dwSize);
    std::int32_t packetOffset = 0;
    while (packetOffset < readSize) {
        if (mode == SocketMode::SM_READ_LEN) {
            if (readSize - packetOffset < 2) {
                Read(readSize - packetOffset);
                return;
            }

            const std::uint8_t* const length = m_pReadBuf->m_Buffer.data() + packetOffset;
            packetLen = static_cast<std::int32_t>(
                            static_cast<std::uint32_t>(length[0]) | (static_cast<std::uint32_t>(length[1]) << 8U)
                        ) -
                2;
            if (packetLen < 1 || packetLen > kMaximumBodySize) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[LogSocket] %d: bad packet size %d",
                //     runtime::LegacySocketImportAddressToken(),
                //     packetLen)
                // Log.Add(LOG_ERROR,
                //     "[LogD] Received a packet body of %d bytes. Expected 1 to %d. "
                //     "Closing the connection.", packetLen, kMaximumBodySize)
                L2_LOG_ERROR(
                    Log,
                    "[LogD] Received a packet body of %d bytes; expected 1 to %d. "
                    "Closing the connection.",
                    packetLen, kMaximumBodySize
                );
                mode = SocketMode::SM_CLOSE;
                Close();
                return;
            }
            mode = SocketMode::SM_READ_BODY;
            packetOffset += 2;
        }

        if (mode != SocketMode::SM_READ_BODY) {
            Close();
            return;
        }
        if (readSize - packetOffset < packetLen) {
            Read(readSize - packetOffset);
            return;
        }

        const std::uint8_t opcode = m_pReadBuf->m_Buffer[packetOffset];
        if (opcode > 2) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[LogSocket] unknown protocol %d",
            //     static_cast<int>(opcode))
            // Log.Add(LOG_ERROR,
            //     "[LogD] Received unsupported opcode %u. Expected 0 to 2. "
            //     "Closing the connection.", static_cast<unsigned int>(opcode))
            L2_LOG_ERROR(
                Log,
                "[LogD] Received unsupported opcode %u; expected 0 to 2. "
                "Closing the connection.",
                static_cast<unsigned int>(opcode)
            );
            mode = SocketMode::SM_CLOSE;
            Close();
            return;
        }

        auto* const packet = new CLogPacket;
        packet->m_pSocket = this;
        packet->m_pBuf = m_pReadBuf;
        packet->m_pFunc = packetTable[opcode];
        L2_ADD_REF(this, ::ObjectRefType::IoRetain);
        m_pReadBuf->AddRef();
        CAbstractPacket::g_nPendingPacket.fetch_add(1, std::memory_order_seq_cst);
        packet->PostObject(static_cast<int>(packetOffset));

        packetOffset += packetLen;
        mode = SocketMode::SM_READ_LEN;
    }
    Read(0);
}

void CLogSocket::OnClose() {
    L2_GUARD_FUNCTION();
    const runtime::log::LocalLogScope localDiagnostics;

    L2_GUARD(logcli);
    Log.Sendable(false);
    // Log.Add(
    //     LOG_WARN,
    //     L"CLogSocket Close %x(%x)",
    //     static_cast<unsigned int>(
    //         sock),
    //     static_cast<unsigned int>(
    //         reinterpret_cast<
    //             std::uintptr_t>(this)))
    // Log.Add(LOG_WARN, "[LogD] Connection closed, switching to local log files.")
    L2_LOG_WARN(Log, "[LogD] Connection closed; switching to local log files.");
    mode = SocketMode::SM_CLOSE;
    runtime::raw_atomic::ExchangeI32(reinterpret_cast<volatile std::int32_t*>(&sock), 0);
}

bool CLogSocket::CheckPendingWrite() {
    // Current L2NPC.exe 0x00460e40 and Cached.exe virtual body 0x004f08e4.
    // The failed admission is then handled by CIOSocketEx::AsyncWrite.
    const runtime::log::LocalLogScope localDiagnostics;
    if (m_hSocket.load(std::memory_order_relaxed) == UINT64_C(0xffffffffffffffff)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Write to invalid log socket.",
        //     L"c:\\work\\lineage2build\\server\\main\\shared\\logSocket.h",
        //     0x2b)
        // Log.Add(LOG_ERROR, "[LogD] Cannot queue a record: the log socket is closed.")
        L2_LOG_ERROR(Log, "[LogD] Cannot queue a record: the log socket is closed.");
        return false;
    }
    if (m_nPendingWrite.load(std::memory_order_relaxed) < 0x4000001) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"LogSocket pending write > %d KB, Close!",
    //     0x10000)
    // Log.Add(LOG_ERROR,
    //     "[LogD] Send backlog exceeds %d KiB. Rejecting the write and closing the connection.",
    //     0x10000)
    L2_LOG_ERROR(Log, "[LogD] Send backlog exceeds %d KiB; rejecting the write and closing the connection.", 0x10000);
    runtime::process::ReportCallStack(LOG_ERROR, false);
    return false;
}

bool LogSendMsgPacket(CLogSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    // Current EXE 0x00460a00.
    return false;
}

bool LogCheckStatus(CLogSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    int nCheckType = 0;
    ::Disassemble(packet, "d", &nCheckType);
    if (nCheckType == 1) {
        int nConfirmConnection = 0;
        ::Disassemble(packet, "d", &nConfirmConnection);
        if (nConfirmConnection != 0) {
            logcli.ConfirmConnection();
        }
    }
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x004f05b5..0x004f066b reads the peak timestamp,
    // maximum/current user counts and active game-server pending-write bytes
    // in this exact status-row order.
    const runtime::SystemTime maximumTime = CUserDb::s_maxOnlineTime;
    const std::int32_t maximumUsers = CUserDb::s_maxUsersOnline.load(std::memory_order_relaxed);
    const std::int32_t currentUsers = CUserDb::s_usersOnline.load(std::memory_order_relaxed);
    CSocket* const serverSocket = g_pActiveServerSocket.load(std::memory_order_acquire);
    const std::int32_t pendingWrite = serverSocket ? serverSocket->PendingWrite() : 0;
    Log.Add(
        LOG_DB,
        u"%d,,,,,,,,%04d/%02d/%02d "
        u"%02d:%02d:%02d.%03d,,,%d,%d,%d"
        u",,,,,,,,,,,,,",
        0x518, maximumTime.wYear, maximumTime.wMonth, maximumTime.wDay, maximumTime.wHour, maximumTime.wMinute,
        maximumTime.wSecond, maximumTime.wMilliseconds, maximumUsers, currentUsers, pendingWrite
    );
#elif defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x00812530 emits the server status row from the
    // default.obj user counters and their peak timestamp.
    const runtime::SystemTime maximumTime = g_tMaxServerUserCount;
    const std::int32_t maximumUsers = runtime::raw_atomic::LoadI32Relaxed(&g_nMaxServerUserCount);
    const std::int32_t currentUsers = runtime::raw_atomic::LoadI32Relaxed(&g_nServerUserCount);
    Log.Add(
        LOG_DB,
        u"%d,,,,,,,,%04d/%02d/%02d "
        u"%02d:%02d:%02d.%03d,,,%d,%d"
        u",,,,,,,,,,,,,",
        0x516, maximumTime.wYear, maximumTime.wMonth, maximumTime.wDay, maximumTime.wHour, maximumTime.wMinute,
        maximumTime.wSecond, maximumTime.wMilliseconds, maximumUsers, currentUsers
    );
#else
    // Current 0x00460b53..0x00460b98 observes the shared record in this
    // exact order before entering the variadic logger. The counters are the
    // original volatile longs, not acquire-load synchronization points.
    const std::uint16_t milliseconds = g_tMaxNPCCount.wMilliseconds;
    const std::uint16_t second = g_tMaxNPCCount.wSecond;
    const std::uint16_t minute = g_tMaxNPCCount.wMinute;
    const std::uint16_t hour = g_tMaxNPCCount.wHour;
    const std::uint16_t day = g_tMaxNPCCount.wDay;
    const std::uint16_t month = g_tMaxNPCCount.wMonth;
    const std::uint16_t year = g_tMaxNPCCount.wYear;
    const std::int32_t currentNpcCount = runtime::raw_atomic::LoadI32Relaxed(&g_nNPCCount);
    const std::int32_t maximumNpcCount = runtime::raw_atomic::LoadI32Relaxed(&g_nMaxNPCCount);
    Log.Add(
        LOG_DB,
        u"%d,,,,,,,,%04d/%02d/%02d "
        u"%02d:%02d:%02d.%03d,,,%d,%d"
        u",,,,,,,,,,,,,",
        0x517, year, month, day, hour, minute, second, milliseconds, maximumNpcCount, currentNpcCount
    );
#endif
    // Current EXE 0x00460a80.
    return false;
}

bool LogTestPacket(CLogSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    // Current EXE 0x00460bf4.
    return false;
}
