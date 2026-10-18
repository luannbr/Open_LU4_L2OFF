#include "RelayClientSocket.h"

#include "Relay.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/NativeTcpSocket.h"
#include "Shared/src/Runtime/RawAtomic.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

RelayClientSocket::RelayClientSocket(const std::uint64_t socket) :
    RelaySocket(socket, reinterpret_cast<PacketFunc*>(g_RelayServerPacketTable.GetData()), 4) {
    // Current L2Server 0x00a2bf0c.
}

void RelayClientSocket::OnClose() {
    // Current L2Server 0x00a2c908.
    L2_GUARD_FUNCTION();
    RelayClient* const relayClient = RelayClient::Inst();
    runtime::raw_atomic::StoreI32(&relayClient->sendable, 0);
    RelaySocket::OnClose();
}

void RelayClientSocket::OnRead() {
    // Current L2Server 0x00a2cc20. This owner duplicates the
    // shared framing loop because it creates CRelayPacket, whose callback
    // starts at the opcode instead of at opcode + 1.
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
                //     "RelayClientSocket %x: bad packet size %d",
                //     runtime::LegacySocketImportAddressToken(),
                //     m_nPacketLen)
                // L2_LOG_ERROR(Log,
                //     "RelayClientSocket %x: bad packet size %d",
                //     runtime::LegacySocketImportAddressToken(),
                //     m_nPacketLen)
                L2_LOG_ERROR(
                    Log,
                    "Relay socket 0x%llx received invalid body length %d; expected 1 through 20478 bytes. Closing the "
                    "connection.",
                    static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire)), m_nPacketLen
                );
                m_Mode = SM_CLOSE;
                Close();
                return;
            }
            m_Mode = SM_READ_BODY;
            packetOffset += 2;
        } else if (m_Mode != SM_READ_BODY) {
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
            //     "RelayClientSocket: unknown protocol %d",
            //     static_cast<int>(opcode))
            // L2_LOG_ERROR(Log,
            //     "RelayClientSocket: unknown protocol %d",
            //     static_cast<int>(opcode))
            L2_LOG_ERROR(
                Log,
                "Relay socket 0x%llx received opcode 0x%02x outside its %u-entry protocol table; closing the connection.",
                static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire)), static_cast<unsigned>(opcode),
                static_cast<unsigned>(m_cMaxProtocolNo)
            );
            m_Mode = SM_CLOSE;
            Close();
            return;
        }

        auto* const packet = new RelayPacket;
        packet->m_pSocket = this;
        packet->m_pBuf = m_pReadBuf;
        packet->m_pFunc = reinterpret_cast<RelayClientPacketFunc*>(m_pPacketTable)[opcode];
        L2_ADD_REF(this, ObjectRefType::IoRetain);
        m_pReadBuf->AddRef();
        CAbstractPacket::g_nPendingPacket.fetch_add(1, std::memory_order_seq_cst);
        packet->PostObject(packetOffset);

        packetOffset += m_nPacketLen;
        m_Mode = SM_READ_LEN;
    }
    Read(0);
}
