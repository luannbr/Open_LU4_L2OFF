#pragma once

#include "CacheSocket.h"

#include "Shared/src/IOC.h"
#include "Shared/src/IOCs.h"

#include <array>
#include <cstddef>
#include <cstdint>

// Shipping owner: CacheD/Server.{h,cpp}, current 0x005297b0..0x00529c9b.
// This is the common CacheD listener used by the L2Server and NPC-server
// endpoints. It is not Shared/CIOServer itself.
class CServer : public CIOServer {
public:
    using SocketAllocator = CSocket* (*)(std::uint64_t socket);

    CServer();
    ~CServer() override;

    void Run(int port, SocketAllocator allocator, bool restrictAddress, int readerCount, int workerCount);

protected:
    CIOServerSocket* CreateSocket(std::uint64_t socket, ::sockaddr_in* address) override;

    std::uint64_t m_opaque28 = 0; // target +0x28
    bool m_running = false; // target +0x30
    std::array<std::byte, 7> m_padding31{};
    std::uint64_t m_serverSocket = 0; // target +0x38
    std::uint64_t m_event = 0; // target +0x40
    CIOCriticalSection m_socketLock; // target +0x48
    SocketAllocator m_allocator = nullptr; // target +0x70
    bool m_restrictAddress = false; // target +0x78
    std::array<std::byte, 7> m_padding79{};
};


// Current object at VA 0x02a1e450. The usable PDB tail does not keep its
// data identifier. G_gameDbServer is the surviving C1 spelling
// after the current CServer type, address and L2Server endpoint were fixed
// from Cached.exe.
extern CServer g_gameDbServer;
