#pragma once

#include "Shared/src/CriticalSectionStorage.h"
#include "Shared/src/IOC.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CSocket;

using ServerSocketAllocator = CSocket* (*)(std::uint64_t socket);

struct IServerCallback {
    virtual ~IServerCallback() = default;
    virtual CSocket* CreateSocket(std::uint64_t socket) = 0;
};

struct CDummyServerCallback final : IServerCallback {
    explicit CDummyServerCallback(ServerSocketAllocator allocator) noexcept;

    CSocket* CreateSocket(std::uint64_t socket) override;

private:
    ServerSocketAllocator allocator_;
};

class Server : public CIOServer {
public:
    L2_DECLARE_OBJECT_REGISTRATION(Server)

    Server();
    ~Server() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    L2_DECLARE_OBJECT_ALLOCATORS()

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    void Run(int port, IServerCallback* callback, bool restrictAddress, int parameter4, int parameter5);
    void Run(int port, ServerSocketAllocator allocator, bool restrictAddress, int parameter4, int parameter5);

    virtual void Remove(std::uint64_t socket);
    virtual bool Add(std::uint64_t socket, CSocket* client);
    virtual void Shutdown();

    void ReportSocket();

    void* listenThread;
    bool m_bReceivedShutdownMessage = false;

protected:
    CIOServerSocket* CreateSocket(std::uint64_t socket, ::sockaddr_in* address) override;

    std::uint64_t listener;
    ::std::map<std::uint64_t, CSocket*> sockets;
    void* completionPort;
    ::l2portable_detail::CriticalSectionStorage sockSect;
    bool restrict;
    IServerCallback* m_pServerCallback = nullptr;

private:
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(Server)
};


extern Server g_server;
