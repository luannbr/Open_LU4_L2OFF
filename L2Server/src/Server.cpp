#include "Server.h"
#include "Shared/src/MemorySub.h"

#include "CConfig.h"

#include "Shared/src/IOCs.h"
#include "Shared/src/Log.h"
#include "Shared/src/Socket.h"

#include <cstdint>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <arpa/inet.h>
#endif

CIOCriticalSection ipSetLock;
::std::set<std::uint32_t> ipSet;

CDummyServerCallback::CDummyServerCallback(ServerSocketAllocator allocator) noexcept : allocator_(allocator) {}

CSocket* CDummyServerCallback::CreateSocket(std::uint64_t socket) { return allocator_(socket); }

Server::Server() {
    L2_GUARD_FUNCTION();
    ::l2portable_detail::InitializeCriticalSectionAndSpinCount(sockSect, 4000);
}

Server::~Server() {
    L2_GUARD_FUNCTION();
    ::l2portable_detail::DeleteCriticalSection(sockSect);
#if defined(_WIN32)
    ::CloseHandle(static_cast<HANDLE>(completionPort));
#endif
}

L2_DEFINE_OBJECT_CLASS_ID(Server)

L2_DEFINE_OBJECT_CLASS_NAME(Server, m_pClassName_Server)

L2_DEFINE_OBJECT_ALLOCATOR(Server, __GetClassName())

L2_DEFINE_OBJECT_NEW(Server)

L2_DEFINE_OBJECT_ADD_REF(Server)

L2_DEFINE_OBJECT_RELEASE(Server)

void Server::Run(int port, IServerCallback* callback, bool restrictAddress, int parameter4, int parameter5) {
    L2_GUARD_FUNCTION();
    m_pServerCallback = callback;
    restrict = restrictAddress;
    if (CIOServer::Create(port) != 0) {
        // Log.Add(
        //     LOG_NORMAL,
        //     "server ready on port %d",
        //     port)
        // L2_LOG_INFO(Log,
        //     "server ready on port %d",
        //     port)
        L2_LOG_INFO(
            Log,
            "TCP listener created on wildcard IPv4 address (requested port %d, bind port field %u); accept-wait "
            "registration succeeded.",
            port, static_cast<unsigned>(static_cast<std::uint16_t>(port))
        );
    }
    listener = m_hSocket;
    static_cast<void>(parameter4);
    static_cast<void>(parameter5);
}

void Server::Run(int port, ServerSocketAllocator allocator, bool restrictAddress, int parameter4, int parameter5) {
    L2_GUARD_FUNCTION();
    Run(port, new CDummyServerCallback(allocator), restrictAddress, parameter4, parameter5);
}

CIOServerSocket* Server::CreateSocket(std::uint64_t socket, ::sockaddr_in* address) {
    L2_GUARD_FUNCTION();

    const auto* const bytes = reinterpret_cast<const std::uint8_t*>(&address->sin_addr.s_addr);
    if (!restrict) {
        if (bWriteConnLog) {
            // Log.Add(
            //     LOG_NORMAL,
            //     "[%s][%d] %d: %-12s %13s %12s %12s %d.%d.%d.%d",
            //     reinterpret_cast<const char*>(L"server.cpp"),
            //     0x73,
            //     static_cast<int>(socket),
            //     "new conn",
            //     "",
            //     "",
            //     "",
            //     static_cast<int>(bytes[0]),
            //     static_cast<int>(bytes[1]),
            //     static_cast<int>(bytes[2]),
            //     static_cast<int>(bytes[3]))
            // L2_LOG_INFO(Log,
            //     "[%s][%d] %d: %-12s %13s %12s %12s %d.%d.%d.%d",
            //     reinterpret_cast<const char*>(L"server.cpp"),
            //     0x73,
            //     static_cast<int>(socket),
            //     "new conn",
            //     "",
            //     "",
            //     "",
            //     static_cast<int>(bytes[0]),
            //     static_cast<int>(bytes[1]),
            //     static_cast<int>(bytes[2]),
            //     static_cast<int>(bytes[3]))
            L2_LOG_INFO(
                Log, "Accepted TCP socket 0x%llx from %u.%u.%u.%u:%u; client object creation follows.",
                static_cast<unsigned long long>(socket), static_cast<unsigned>(bytes[0]), static_cast<unsigned>(bytes[1]),
                static_cast<unsigned>(bytes[2]), static_cast<unsigned>(bytes[3]),
                static_cast<unsigned>(ntohs(address->sin_port))
            );
        }
    } else if (bytes[0] != 10 || bytes[1] != 10) {
        // Log.Add(
        //     LOG_ERROR,
        //     "%d: deny connection from %d.%d.%d.%d",
        //     static_cast<int>(socket),
        //     static_cast<int>(bytes[0]),
        //     static_cast<int>(bytes[1]),
        //     static_cast<int>(bytes[2]),
        //     static_cast<int>(bytes[3]))
        // L2_LOG_ERROR(Log,
        //     "%d: deny connection from %d.%d.%d.%d",
        //     static_cast<int>(socket),
        //     static_cast<int>(bytes[0]),
        //     static_cast<int>(bytes[1]),
        //     static_cast<int>(bytes[2]),
        //     static_cast<int>(bytes[3]))
        L2_LOG_WARN(
            Log,
            "Rejecting TCP socket 0x%llx from %u.%u.%u.%u:%u: this listener permits only addresses in 10.10.0.0/16; no "
            "client object is created.",
            static_cast<unsigned long long>(socket), static_cast<unsigned>(bytes[0]), static_cast<unsigned>(bytes[1]),
            static_cast<unsigned>(bytes[2]), static_cast<unsigned>(bytes[3]), static_cast<unsigned>(ntohs(address->sin_port))
        );
        return nullptr;
    }

    {
        L2_GUARD(ipSetLock);
        ipSet.insert(address->sin_addr.s_addr);
    }

    CSocket* const client = m_pServerCallback->CreateSocket(socket);
    Add(socket, client);
    client->SetAddress(address->sin_addr);
    return client;
}

void Server::Remove(std::uint64_t socket) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(sockSect);
        const auto found = sockets.find(socket);
        if (found == sockets.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"socket handle[%p] is not in ORT_SOCKET_MAP",
            //     reinterpret_cast<void*>(
            //         static_cast<std::uintptr_t>(socket)))
            // L2_LOG_ERROR(Log,
            //     L"socket handle[%p] is not in ORT_SOCKET_MAP",
            //     reinterpret_cast<void*>(
            //         static_cast<std::uintptr_t>(socket)))
            L2_LOG_ERROR(
                Log, "Cannot remove socket 0x%llx: it is absent from the socket map; no map reference is released.",
                static_cast<unsigned long long>(socket)
            );
        } else {
            L2_RELEASE(found->second, ObjectRefType::SocketMap);
            sockets.erase(found);
        }
    }
}

bool Server::Add(std::uint64_t socket, CSocket* client) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(sockSect);
        L2_ADD_REF(client, ObjectRefType::SocketMap);
        sockets.emplace(socket, client);
    }
    return true;
}

void Server::Shutdown() {
    L2_GUARD_FUNCTION();
    std::vector<CSocket*> clients;
    {
        L2_GUARD(sockSect);
        for (const auto& entry : sockets) {
            clients.push_back(entry.second);
            L2_ADD_REF(entry.second, ObjectRefType::TemporaryUse);
        }
    }

    for (CSocket* client : clients) {
        client->Close();
        L2_RELEASE(client, ObjectRefType::TemporaryUse);
    }
}

void Server::ReportSocket() {
    L2_GUARD_FUNCTION();
    std::vector<CSocket*> clients;
    {
        L2_GUARD(sockSect);
        for (const auto& entry : sockets) {
            clients.push_back(entry.second);
            L2_ADD_REF(entry.second, ObjectRefType::TemporaryUse);
        }
    }

    // Log.Add(
    //     LOG_NORMAL,
    //     "=========    Socket Report   ===========")
    // L2_LOG_INFO(Log,
    //     "=========    Socket Report   ===========")
    L2_LOG_INFO(Log, "Socket report: %zu retained entries in the captured map snapshot.", clients.size());
    for (CSocket* client : clients) {
        // const char* const address =
        //     ::inet_ntoa(client->GetAddress())
        // Keep the report address by value, independent of inet_ntoa storage.
        const std::uint32_t address = ntohl(client->GetAddress().s_addr);
        // Log.Add(
        //     LOG_NORMAL,
        //     "socket [%d] from [%s]",
        //     static_cast<int>(client->m_Socket),
        //     address)
        // L2_LOG_INFO(Log,
        //     "socket [%d] from [%s]",
        //     static_cast<int>(client->m_Socket),
        //     address)
        // L2_LOG_INFO(Log,
        //     "Socket report entry: handle 0x%llx, peer IPv4 %s.",
        //     static_cast<unsigned long long>(client->m_Socket), address != nullptr ? address : "<unknown>")
        L2_LOG_INFO(
            Log, "Socket report entry: handle 0x%llx, peer IPv4 %u.%u.%u.%u.",
            static_cast<unsigned long long>(client->m_Socket), static_cast<unsigned>((address >> 24) & 255U),
            static_cast<unsigned>((address >> 16) & 255U), static_cast<unsigned>((address >> 8) & 255U),
            static_cast<unsigned>(address & 255U)
        );
        L2_RELEASE(client, ObjectRefType::TemporaryUse);
    }
}
