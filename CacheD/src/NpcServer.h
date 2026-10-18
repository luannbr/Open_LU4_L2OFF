#pragma once

#include "Server.h"

#include <cstdarg>
#include <cstdint>

class CNpcDbSocket;

// Shipping owner: CacheD/NpcServer.{h,cpp}, current
// 0x004fe2ac..0x004fe6eb.
class CNpcServer : public CServer {
public:
    CNpcServer();
    ~CNpcServer() override;

    void Send(const char* format, ...);
    std::uint32_t GetReadBufSize();

protected:
    CIOServerSocket* CreateSocket(std::uint64_t socket, ::sockaddr_in* address) override;

private:
    CNpcDbSocket* m_socket = nullptr; // target +0x80
};


extern CNpcServer g_NpcServer;
