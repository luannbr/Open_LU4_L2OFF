#pragma once

#include "AuthGateDTypes.h"
#include "IOSocket.h"

#include <cstdint>

// Original owner: AuthSocket.h / AuthSocket.cpp. AuthGateD.pdb records the
// 0x9c-byte CIOSocket prefix followed by these five members through 0xb0.
class CAuthSocket final : public CIOSocket {
public:
    CAuthSocket();
    ~CAuthSocket() override;

    void OnCreate() override;
    void OnRead() override;
    void OnClose(std::uint32_t socket) override;

    void SendCltConnect(std::int32_t sessionId, in_addr address);
    void SendCltDisconnect(std::int32_t sessionId);
    bool SendCltPacket(std::int32_t sessionId, CIOBuffer* buffer);

    static CAuthSocket* g_pAuthSocket;

private:
    friend class CClientSocket;

    char m_opcode = 0;
    std::int32_t m_protocolVer = 0;
    std::int32_t m_sessionId = 0;
    std::int32_t m_packetLen = 0;
    SocketMode m_mode = SM_READ_LEN;
};
