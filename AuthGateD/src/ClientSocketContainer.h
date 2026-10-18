#pragma once

#include "AuthPacketPool.h"
#include "ClientPacketPool.h"
#include "IOServer.h"

#include <atomic>
#include <cstdint>
#include <unordered_map>

class CClientSocket;

class CClientSocketContainer : public CIOServer {
public:
    CClientSocketContainer();
    ~CClientSocketContainer() override;

    std::int32_t GetSessionID();
    bool CloseSocketByAuthD(std::int32_t sessionId);
    void SendPacket(
        std::int32_t sessionId,
        const unsigned char* packet,
        int packetLength);

    void AddSocket(
        std::uint32_t socket,
        CIOSocket* object) override;
    bool RemoveSocket(std::uint32_t socket) override;
    void CheckSocketTimeout(
        int minutes,
        SocketTimeoutOpt option) override;

    static CClientSocketContainer* g_pClientSocketContainer;

    CAuthPacketPool m_AuthPacketPool;
    CClientPacketPool m_ClientPacketPool;
    std::atomic<std::int32_t> m_sessionIdSeed{0};
    std::unordered_map<std::int32_t, CClientSocket*>
        m_gateSessionMap;
};

// Portable: listener of the LU4 client port (lu4Port). Its sockets are marked
// LU4 (CClientSocket::AllocateLu4) and kept by the main container, which
// routes AuthD replies by session.
class CLu4ClientListener : public CIOServer {
public:
    void AddSocket(std::uint32_t socket, CIOSocket* object) override {
        CClientSocketContainer::g_pClientSocketContainer->AddSocket(socket, object);
    }
    bool RemoveSocket(std::uint32_t socket) override {
        return CClientSocketContainer::g_pClientSocketContainer->RemoveSocket(socket);
    }
};

