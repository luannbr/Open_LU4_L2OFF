#include "NpcServer.h"

#include "NpcDbSocket.h"

#include "L2Core/src/inc/Guard.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/Log.h"

#include <cstdarg>

CNpcServer g_NpcServer;

CNpcServer::CNpcServer() : m_socket(nullptr) { L2_GUARD_FUNCTION(); }

CNpcServer::~CNpcServer() { L2_GUARD_FUNCTION(); }

CIOServerSocket* CNpcServer::CreateSocket(std::uint64_t socket, ::sockaddr_in* address) {
    L2_GUARD_FUNCTION();
    CIOServerSocket* created = CServer::CreateSocket(socket, address);
    m_socket = static_cast<CNpcDbSocket*>(created);
    return created;
}

void CNpcServer::Send(const char* format, ...) {
    L2_GUARD_FUNCTION();
    if (!m_socket) {
        // Log.Add(LOG_ERROR, "[npcserver.cpp][%d] No connection to npcserver.", 0x30)
        // Log.Add(LOG_WARN,
        //     "[L2NPC] Cannot send a reply: no NPC connection is registered. Discarding the reply (format %s).",
        //     format)
        L2_LOG_WARN(Log,
            "[L2NPC] Cannot send a reply: no NPC connection is registered; discarding the reply (format %s).",
            format);
        return;
    }

    va_list arguments;
    va_start(arguments, format);
    m_socket->SendBuffer(format, arguments);
    va_end(arguments);
}

std::uint32_t CNpcServer::GetReadBufSize() {
    // Current Cached.exe 0x004fe6ec. The damaged current PDB does not keep
    // this identifier. The body is the exact null-socket/null-buffer gate
    // around CIOSocketEx::GetReadBufSize.
    return m_socket ? m_socket->GetReadBufSize() : 0;
}
