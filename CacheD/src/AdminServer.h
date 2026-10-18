#pragma once

#include "Server.h"

#include <cstdint>

// Shipping owner: CacheD/adminServer.{h,cpp}. Current Cached.exe derives this
// listener from CServer without adding storage and replaces only the log text
// and CreateSocket vtable entry.
class CAdminServer : public CServer {
public:
    CAdminServer();
    ~CAdminServer() override;

    void Run(int port, SocketAllocator allocator, bool restrictAddress, int readerCount, int workerCount);

protected:
    CIOServerSocket* CreateSocket(std::uint64_t socket, ::sockaddr_in* address) override;
};


// The usable current symbols do not keep the data identifier. The C1
// source supplies this spelling after the current owner and
// layout have been fixed from Cached.exe.
extern CAdminServer g_adminServer;
