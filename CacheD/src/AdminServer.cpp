#include "AdminServer.h"

#include "L2Core/src/inc/Guard.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/Log.h"

#include <cstdint>

CAdminServer g_adminServer;

CAdminServer::CAdminServer() {
    L2_GUARD_FUNCTION();
    m_running = false;
}

CAdminServer::~CAdminServer() { L2_GUARD_FUNCTION(); }

void CAdminServer::Run(
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
        // Log.Add(LOG_NORMAL, "admin server ready on port %d", port)
        // Log.Add(LOG_NORMAL,
        //     "[admin listener] Listening on TCP 0.0.0.0:%u (socket %llu. Allowed addresses: %s).",
        //     static_cast<unsigned>(static_cast<std::uint16_t>(port)), static_cast<unsigned long long>(m_hSocket), restrictAddress ? "10.10.0.0/16" : "any")
        L2_LOG_INFO(Log,
            "[admin listener] Listening on TCP 0.0.0.0:%u (socket %llu; allowed addresses: %s).",
            static_cast<unsigned>(static_cast<std::uint16_t>(port)), static_cast<unsigned long long>(m_hSocket), restrictAddress ? "10.10.0.0/16" : "any");
    } else {
        // Log.Add(LOG_ERROR,
        //     "[admin listener] Cannot start the listener on TCP port %u. See the preceding transport error.",
        //     static_cast<unsigned>(static_cast<std::uint16_t>(port)))
        L2_LOG_ERROR(Log,
            "[admin listener] Cannot start the listener on TCP port %u; see the preceding transport error.",
            static_cast<unsigned>(static_cast<std::uint16_t>(port)));
    }
    m_serverSocket = m_hSocket;
}

CIOServerSocket* CAdminServer::CreateSocket(std::uint64_t socket, ::sockaddr_in* address) {
    L2_GUARD_FUNCTION();
    CIOServerSocket* const created = CServer::CreateSocket(socket, address);
    // Noise: address-byte preparation only served the duplicate message below.
    // const auto* const bytes = reinterpret_cast<const std::uint8_t*>(&address->sin_addr.s_addr)
    // Noise: CServer::CreateSocket already reports acceptance or refusal. This unconditional duplicate could describe a refused connection as created.
    // Log.Add(
    //     LOG_NORMAL, "admin server create socket[%u.%u.%u.%u]", static_cast<unsigned>(bytes[0]),
    //     static_cast<unsigned>(bytes[1]), static_cast<unsigned>(bytes[2]), static_cast<unsigned>(bytes[3])
    // )
    return created;
}
