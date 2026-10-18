#include "Server.h"

#include "L2Core/src/inc/Guard.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/Log.h"

#include <cstdint>

CServer g_gameDbServer;

CServer::CServer() : CIOServer(), m_socketLock(4000) { L2_GUARD_FUNCTION(); }

CServer::~CServer() {
    L2_GUARD_FUNCTION();
    m_event = 0;
}

void CServer::Run(
    int port,
    SocketAllocator allocator,
    bool restrictAddress,
    int readerCount [[maybe_unused]],
    int workerCount [[maybe_unused]]
) {
    L2_GUARD_FUNCTION();

    m_allocator = allocator;
    m_restrictAddress = restrictAddress;
    if (Create(port) != 0) {
        // Log.Add(LOG_NORMAL, "server ready on port %d", port)
        // Log.Add(LOG_NORMAL,
        //     "[listener] Listening on TCP 0.0.0.0:%u (socket %llu. Allowed addresses: %s).",
        //     static_cast<unsigned>(static_cast<std::uint16_t>(port)), static_cast<unsigned long long>(m_hSocket), restrictAddress ? "10.10.0.0/16" : "any")
        L2_LOG_INFO(Log,
            "[listener] Listening on TCP 0.0.0.0:%u (socket %llu; allowed addresses: %s).",
            static_cast<unsigned>(static_cast<std::uint16_t>(port)),
            static_cast<unsigned long long>(m_hSocket),
            restrictAddress ? "10.10.0.0/16" : "any");
    } else {
        // Log.Add(LOG_ERROR,
        //     "[listener] Cannot start the listener on TCP port %u. See the preceding transport error.",
        //     static_cast<unsigned>(static_cast<std::uint16_t>(port)))
        L2_LOG_ERROR(Log,
            "[listener] Cannot start the listener on TCP port %u; see the preceding transport error.",
            static_cast<unsigned>(static_cast<std::uint16_t>(port)));
    }
    m_serverSocket = m_hSocket;
}

CIOServerSocket* CServer::CreateSocket(std::uint64_t socket, ::sockaddr_in* address) {
    L2_GUARD_FUNCTION();

    const auto* addressBytes = reinterpret_cast<const std::uint8_t*>(&address->sin_addr.s_addr);
    if (m_restrictAddress && !(addressBytes[0] == 10 && addressBytes[1] == 10)) {
        // Log.Add(
        //     LOG_ERROR, "%u.%u.%u.%u: denied", static_cast<unsigned>(addressBytes[0]),
        //     static_cast<unsigned>(addressBytes[1]), static_cast<unsigned>(addressBytes[2]),
        //     static_cast<unsigned>(addressBytes[3])
        // )
        // Log.Add(LOG_WARN,
        //     "[listener] Connection from %u.%u.%u.%u:%u rejected by the 10.10.0.0/16 address restriction (listener %llu).",
        //     static_cast<unsigned>(addressBytes[0]), static_cast<unsigned>(addressBytes[1]), static_cast<unsigned>(addressBytes[2]), static_cast<unsigned>(addressBytes[3]), static_cast<unsigned>(ntohs(address->sin_port)), static_cast<unsigned long long>(m_serverSocket))
        L2_LOG_WARN(Log,
            "[listener] Connection from %u.%u.%u.%u:%u rejected by the 10.10.0.0/16 address restriction (listener %llu).",
            static_cast<unsigned>(addressBytes[0]), static_cast<unsigned>(addressBytes[1]), static_cast<unsigned>(addressBytes[2]), static_cast<unsigned>(addressBytes[3]), static_cast<unsigned>(ntohs(address->sin_port)), static_cast<unsigned long long>(m_serverSocket));
        return nullptr;
    }

    // if (!m_allocator)
        // return nullptr
    if (!m_allocator) {
        // Log.Add(LOG_ERROR,
        //     "[listener] Cannot accept connection from %u.%u.%u.%u:%u: no socket allocator is configured for listener %llu.",
        //     static_cast<unsigned>(addressBytes[0]), static_cast<unsigned>(addressBytes[1]), static_cast<unsigned>(addressBytes[2]), static_cast<unsigned>(addressBytes[3]), static_cast<unsigned>(ntohs(address->sin_port)), static_cast<unsigned long long>(m_serverSocket))
        L2_LOG_ERROR(Log,
            "[listener] Cannot accept connection from %u.%u.%u.%u:%u: no socket allocator is configured for listener %llu.",
            static_cast<unsigned>(addressBytes[0]), static_cast<unsigned>(addressBytes[1]), static_cast<unsigned>(addressBytes[2]), static_cast<unsigned>(addressBytes[3]), static_cast<unsigned>(ntohs(address->sin_port)), static_cast<unsigned long long>(m_serverSocket));
        return nullptr;
    }
    CSocket* created = m_allocator(socket);
    // Current 0x00529c37 immediately calls SetAddress through the
    // returned object's vtable. A null allocator result is therefore fatal,
    // instead of a second graceful refusal path.
    created->SetAddress(address->sin_addr);
    // Log.Add(
    //     LOG_NORMAL, "*new connection from %u.%u.%u.%u", static_cast<unsigned>(addressBytes[0]),
    //     static_cast<unsigned>(addressBytes[1]), static_cast<unsigned>(addressBytes[2]),
    //     static_cast<unsigned>(addressBytes[3])
    // )
    // Log.Add(LOG_NORMAL,
    //     "[listener] Accepted connection %llu from %u.%u.%u.%u:%u on listener %llu.",
    //     static_cast<unsigned long long>(socket), static_cast<unsigned>(addressBytes[0]), static_cast<unsigned>(addressBytes[1]), static_cast<unsigned>(addressBytes[2]), static_cast<unsigned>(addressBytes[3]), static_cast<unsigned>(ntohs(address->sin_port)), static_cast<unsigned long long>(m_serverSocket))
    L2_LOG_INFO(Log,
        "[listener] Accepted connection %llu from %u.%u.%u.%u:%u on listener %llu.",
        static_cast<unsigned long long>(socket), static_cast<unsigned>(addressBytes[0]), static_cast<unsigned>(addressBytes[1]), static_cast<unsigned>(addressBytes[2]), static_cast<unsigned>(addressBytes[3]), static_cast<unsigned>(ntohs(address->sin_port)), static_cast<unsigned long long>(m_serverSocket));
    return created;
}
