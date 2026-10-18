#include "Shared/src/RelaySocket.h"

#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"

#include <atomic>
#include <cstdint>
#include <limits>

RelaySocket::RelaySocket(const std::uint64_t socket, PacketFunc* const packetTable, const unsigned char maxProtocolNo) :
    CSocket(socket, 0x1000), m_cMaxProtocolNo(maxProtocolNo), m_nWorldId(0) {
    m_pPacketTable = packetTable;
}

bool RelaySocket::CheckPendingWrite() {
    // Current L2Server 0x00419978.
    L2_GUARD_FUNCTION();
    if (m_hSocket.load(std::memory_order_acquire) == std::numeric_limits<std::uint64_t>::max()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Write to invalid RelaySocket.",
        //     L"..\\Shared\\RelaySocket.cpp",
        //     0x0e)
        // Log.Add(
        //     LOG_WARN,
        //     "[relay] Rejecting a write to an already closed connection.")
        L2_LOG_WARN(Log, "[relay] Rejecting a write to an already closed connection.");
        return false;
    }
    if (m_nPendingWrite.load(std::memory_order_relaxed) < 0x800001) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"RelaySocket pending write > %d KB, Close!",
    //     0x2000)
    // Log.Add(
    //     LOG_ERROR,
    //     "[relay] Pending writes exceed the 8388608-byte limit on connection %llu. Rejecting the write and requesting
    //     closure.", static_cast<unsigned long long>(
    //         m_hSocket.load(std::memory_order_acquire)))
    L2_LOG_ERROR(
        Log,
        "[relay] Pending writes exceed the 8388608-byte limit on connection %llu; rejecting the write and requesting "
        "closure.",
        static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire))
    );
    ReportCallStack(LOG_ERROR, false);
    return false;
}

void RelaySocket::OnClose() {
    // Current L2Server 0x00419a80.
    L2_GUARD_FUNCTION();
    m_Mode = SM_CLOSE;
}

void RelaySocket::OnCreate() {
    // Current L2Server 0x0041a2c8.
    L2_GUARD_FUNCTION();
    Read(0);
}

void RelaySocket::OnRead() {
    // Current L2Server 0x0041a36c.
    L2_GUARD_FUNCTION();
    if (m_Mode == SM_CLOSE) {
        Close();
        return;
    }

    const int readSize = static_cast<int>(m_pReadBuf->m_dwSize);
    int packetOffset = 0;
    while (packetOffset < readSize) {
        if (m_Mode == SM_READ_LEN) {
            if (readSize < packetOffset + 2) {
                Read(static_cast<std::uint32_t>(readSize - packetOffset));
                return;
            }
            const unsigned char* const header = m_pReadBuf->m_Buffer.data() + packetOffset;
            m_nPacketLen = static_cast<int>(header[1]) * 0x100 - 2 + static_cast<int>(header[0]);
            if (static_cast<std::uint32_t>(m_nPacketLen - 1) > 0x4ffdU) {
                // Log.Add(
                //     LOG_ERROR,
                //     "RelaySocket %x: bad packet size %d",
                //     runtime::LegacySocketImportAddressToken(),
                //     m_nPacketLen)
                // Log.Add(
                //     LOG_ERROR,
                //     "[relay] Invalid packet body length %d bytes on connection %llu (allowed 1..20478). Closing the
                //     connection.", m_nPacketLen, static_cast<unsigned long long>(
                //         m_hSocket.load(std::memory_order_acquire)))
                L2_LOG_ERROR(
                    Log,
                    "[relay] Invalid packet body length %d bytes on connection %llu (allowed 1..20478); closing the "
                    "connection.",
                    m_nPacketLen, static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire))
                );
                m_Mode = SM_CLOSE;
                Close();
                return;
            }
            m_Mode = SM_READ_BODY;
            packetOffset += 2;
        } else if (m_Mode != SM_READ_BODY) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[relay] Invalid receive state %d on connection %llu. Closing the connection.",
            //     static_cast<int>(m_Mode),
            //     static_cast<unsigned long long>(
            //         m_hSocket.load(std::memory_order_acquire)))
            L2_LOG_ERROR(
                Log, "[relay] Invalid receive state %d on connection %llu; closing the connection.",
                static_cast<int>(m_Mode), static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire))
            );
            Close();
            return;
        }

        if (readSize < packetOffset + m_nPacketLen) {
            Read(static_cast<std::uint32_t>(readSize - packetOffset));
            return;
        }

        ++m_nPacketCount;
        const unsigned char opcode = m_pReadBuf->m_Buffer[static_cast<std::size_t>(packetOffset)];
        if (opcode >= m_cMaxProtocolNo) {
            // Log.Add(
            //     LOG_ERROR,
            //     "RelaySocket: unknown protocol %d",
            //     static_cast<int>(opcode))
            // Log.Add(
            //     LOG_ERROR,
            //     "[relay] Unsupported incoming opcode 0x%02x on connection %llu (opcode must be below 0x%02x). Closing the
            //     connection.", static_cast<unsigned int>(opcode), static_cast<unsigned long long>(
            //         m_hSocket.load(std::memory_order_acquire)),
            //     static_cast<unsigned int>(m_cMaxProtocolNo))
            L2_LOG_ERROR(
                Log,
                "[relay] Unsupported incoming opcode 0x%02x on connection %llu (opcode must be below 0x%02x); closing the "
                "connection.",
                static_cast<unsigned int>(opcode),
                static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire)),
                static_cast<unsigned int>(m_cMaxProtocolNo)
            );
            m_Mode = SM_CLOSE;
            Close();
            return;
        }

        auto* const packet = new CPacketEx<DefaultIOBuffer, CSocket, PacketFunc>;
        packet->m_pSocket = this;
        packet->m_pBuf = m_pReadBuf;
        packet->m_pFunc = m_pPacketTable[opcode];
        L2_ADD_REF(this, ObjectRefType::IoRetain);
        m_pReadBuf->AddRef();
        CAbstractPacket::g_nPendingPacket.fetch_add(1, std::memory_order_seq_cst);
        packet->PostObject(packetOffset);

        packetOffset += m_nPacketLen;
        m_Mode = SM_READ_LEN;
    }
    Read(0);
}
