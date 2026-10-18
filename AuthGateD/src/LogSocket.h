#pragma once

#include "AuthGateDTypes.h"
#include "IOSocket.h"

#include <cstdint>

// Original owner: LogSocket.h/LogSocket.cpp. AuthGateD.pdb records the
// CIOSocket prefix followed by m_packetLen +0x9c and m_mode +0xa0.
class CLogSocket final : public CIOSocket {
public:
    CLogSocket();
    ~CLogSocket() override;

    void OnRead() override;
    void OnClose(std::uint32_t socket) override;
    void OnCreate() override;

    void SendLog(const char* text);

private:
    friend class CClientPacket;

    bool Send(const char* format, ...);

    static CLogSocket* s_pLogSocket;

    std::int32_t m_packetLen = 0;
    SocketMode m_mode = SM_CLOSE;
};
