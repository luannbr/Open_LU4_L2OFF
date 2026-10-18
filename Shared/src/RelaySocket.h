#pragma once

#include "Shared/src/Socket.h"

#include <array>
#include <cstddef>
#include <cstdint>


// Server/Main/Shared/RelaySocket. Current L2Server.exe fixes the complete
// CSocket-derived extent and the two product fields at +0x108/+0x10c.
class RelaySocket : public CSocket {
public:
    RelaySocket(std::uint64_t socket, PacketFunc* packetTable, unsigned char maxProtocolNo);
    ~RelaySocket() override = default;

    L2_DECLARE_OBJECT_TYPE(RelaySocket)

    bool CheckPendingWrite() override;
    void OnCreate() override;

protected:
    void OnClose() override;
    void OnRead() override;

    unsigned char m_cMaxProtocolNo; // +0x108
    std::array<std::byte, 3> m_alignment109To10c;
    int m_nWorldId; // +0x10c
};
