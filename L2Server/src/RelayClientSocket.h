#pragma once

#include "Shared/src/RelaySocket.h"
#include "Shared/src/TemplateHelper.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

class RelayClientSocket;

using RelayClientPacketFunc = bool (*)(RelayClientSocket*, unsigned char*);

using RelayServerPacketMap = FixedSizeCallbackMap<4, RelayClientPacketFunc>;

extern RelayServerPacketMap g_RelayServerPacketTable;

bool DummyRelayServerPacket(RelayClientSocket* socket, unsigned char* packet);
bool PacketTestFunc(RelayClientSocket* socket, unsigned char* packet);
bool PacketGiveItemFunc(RelayClientSocket* socket, unsigned char* packet);
bool PacketSayPledgeFunc(RelayClientSocket* socket, unsigned char* packet);
bool PacketSayAllianceFunc(RelayClientSocket* socket, unsigned char* packet);

class RelayClientSocket final : public RelaySocket {
public:
    explicit RelayClientSocket(std::uint64_t socket);
    ~RelayClientSocket() override = default;

    L2_DECLARE_OBJECT_TYPE(RelayClientSocket)

protected:
    void OnClose() override;
    void OnRead() override;
};


// RelayClientSocket.h: the current specialization derives directly from
// CAbstractPacket instead of from the generic CPacketEx callback body. Its
// callback receives the opcode address itself. Each relay decoder owns its
// separate +3 mobile-frame offset.
template <typename TBuffer>
class CRelayPacket final : public CAbstractPacket {
public:
    CRelayPacket() = default;
    ~CRelayPacket() override = default;

    L2_DECLARE_OBJECT_TYPE(CRelayPacket)

    void OnIOCallback(int, std::uint32_t transferred, PortableOverlapped*) override {
        const bool closeRequested = m_pFunc(m_pSocket, m_pBuf->m_Buffer.data() + static_cast<std::size_t>(transferred));
        if (closeRequested) {
            m_pSocket->Close();
        }
        L2_RELEASE(m_pSocket, ObjectRefType::IoRetain);
        m_pBuf->Release();
        CAbstractPacket::g_nPendingPacket.fetch_sub(1, std::memory_order_seq_cst);
        L2_RELEASE(this, ObjectRefType::CreateDelete);
    }

    RelayClientSocket* m_pSocket{}; // +0x18
    TBuffer* m_pBuf{}; // +0x20
    RelayClientPacketFunc m_pFunc{}; // +0x28
};

using RelayPacket = CRelayPacket<DefaultIOBuffer>;
