#include "ClientSocketContainer.h"

#include "Shared/src/ScopedLockGuards.h"

#include "ClientSocket.h"
#include "AuthGateConfig.h"
#include "FileLog.h"
#include "Lu4Login.h"

#include "Shared/src/Runtime/TickCount32.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

CClientSocketContainer*
    CClientSocketContainer::g_pClientSocketContainer = nullptr;

// AuthGateD.exe 0x00409150, ClientSocketContainer.cpp.
CClientSocketContainer::CClientSocketContainer() {
    if (!g_pClientSocketContainer) {
        g_pClientSocketContainer = this;
        m_sessionIdSeed.store(0, std::memory_order_relaxed);
    }
}

// AuthGateD.exe 0x00408A50, ClientSocketContainer.cpp.
CClientSocketContainer::~CClientSocketContainer() {
    g_pClientSocketContainer = nullptr;
}

// AuthGateD.exe 0x00408070, ClientSocketContainer.cpp.
std::int32_t CClientSocketContainer::GetSessionID() {
    const std::uint32_t next = static_cast<std::uint32_t>(
        m_sessionIdSeed.fetch_add(
            1,
            std::memory_order_relaxed) + 1);
    return static_cast<std::int32_t>(next & UINT32_C(0x00ffffff));
}

// AuthGateD.exe 0x00408670, ClientSocketContainer.cpp.
bool CClientSocketContainer::CloseSocketByAuthD(
    std::int32_t sessionId) {
    CClientSocket* socket = nullptr;
    {
        L2_GUARD(m_lock);
        const auto found = m_gateSessionMap.find(sessionId);
        if (found != m_gateSessionMap.end()) {
            socket = found->second;
            socket->AddRef();
        }
    }

    if (socket) {
        socket->CloseSocketByAuthD();
        socket->ReleaseRef();
    }
    return true;
}

// AuthGateD.exe 0x004086E0, ClientSocketContainer.cpp.
void CClientSocketContainer::SendPacket(
    std::int32_t sessionId,
    const unsigned char* packet,
    int packetLength) {
    CClientSocket* socket = nullptr;
    {
        L2_GUARD(m_lock);
        const auto found = m_gateSessionMap.find(sessionId);
        if (found != m_gateSessionMap.end()) {
            socket = found->second;
            socket->AddRef();
        }
    }

    if (socket) {
        std::vector<unsigned char> converted;
        if (socket->m_lu4 && lu4::ConvertReply(packet, packetLength, converted)) {
            socket->Send("b", static_cast<std::uint32_t>(converted.size()), converted.data());
        } else {
            socket->Send(
                "b",
                static_cast<std::uint32_t>(packetLength),
                packet);
        }
        socket->ReleaseRef();
    }
}

// AuthGateD.exe 0x004091F0, ClientSocketContainer.cpp.
void CClientSocketContainer::AddSocket(
    std::uint32_t socket,
    CIOSocket* object) {
    auto* const client = static_cast<CClientSocket*>(object);

    bool socketInserted = false;
    bool sessionInserted = false;
    {
        L2_GUARD(m_lock);
        socketInserted =
            m_sockMap.emplace(socket, object).second;
        sessionInserted =
            m_gateSessionMap.emplace(
                client->m_iSessionId,
                client).second;
    }

    if (!socketInserted) {
        // CFileLog::g_pDefaultFileLog->LogT(
        //     LOG_ERROR,
        //     u"[%s(%d)] - m_sockMap.insert, SOCKET(%d)",
        //     u"CClientSocketContainer::AddSocket",
        //     0x39,
        //     static_cast<std::int32_t>(socket))
        // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
        //     u"component=client event=registration_conflict index=socket socket=%u session=%d action=retain_existing_entry",
        //     socket, client->m_iSessionId)
        // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
        //     u"[client] Socket %u, session %d conflicts with an existing socket registration. Keeping the existing entry.",
        //     socket, client->m_iSessionId)
        // Original: L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
        //     u"[client] Socket %u, session %d conflicts with an existing socket registration. Keeping the existing entry.", socket, client->m_iSessionId)
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[client] Socket %u, session %d conflicts with an existing socket registration. Existing socket entry retained; session registration %s.",
            socket, client->m_iSessionId, sessionInserted ? u"was inserted and is not rolled back" : u"also conflicted");
    }
    if (!sessionInserted) {
        // CFileLog::g_pDefaultFileLog->LogT(
        //     LOG_ERROR,
        //     u"[%s(%d)] - m_gateSessionMap.insert, SOCKET(%d), SESSIONID(%d)",
        //     u"CClientSocketContainer::AddSocket",
        //     0x3c,
        //     static_cast<std::int32_t>(socket),
        //     client->m_iSessionId)
        // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
        //     u"component=client event=registration_conflict index=session socket=%u session=%d action=retain_existing_entry",
        //     socket, client->m_iSessionId)
        // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
        //     u"[client] Socket %u, session %d conflicts with an existing session registration. Keeping the existing entry.",
        //     socket, client->m_iSessionId)
        // Original: L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
        //     u"[client] Socket %u, session %d conflicts with an existing session registration. Keeping the existing entry.", socket, client->m_iSessionId)
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[client] Socket %u, session %d conflicts with an existing session registration. Existing session entry retained; socket registration %s.",
            socket, client->m_iSessionId, socketInserted ? u"was inserted and is not rolled back" : u"also conflicted");
    }
}

// AuthGateD.exe 0x004093F0, ClientSocketContainer.cpp.
bool CClientSocketContainer::RemoveSocket(std::uint32_t socket) {
    {
        L2_GUARD(m_lock);
        const auto found = m_sockMap.find(socket);
        if (found != m_sockMap.end()) {
            auto* const client =
                static_cast<CClientSocket*>(found->second);
            m_gateSessionMap.erase(client->m_iSessionId);
            m_sockMap.erase(found);
        }
    }
    return true;
}

// AuthGateD.exe 0x00409460, ClientSocketContainer.cpp.
void CClientSocketContainer::CheckSocketTimeout(
    int minutes,
    SocketTimeoutOpt option) {
    std::unordered_map<std::uint32_t, CClientSocket*> expired;
    const std::uint32_t threshold =
        static_cast<std::uint32_t>(minutes) * UINT32_C(60000);
    const std::uint32_t now = runtime::TickCount32();

    {
        L2_GUARD(m_lock);
        if (option == ST_LAST_IO || option == ST_CONNECT) {
            for (auto current = m_sockMap.begin();
                 current != m_sockMap.end();) {
                auto* const client =
                    static_cast<CClientSocket*>(current->second);
                const std::uint32_t reference =
                    option == ST_LAST_IO
                    ? client->m_iLastIO
                    : client->m_iFirstIO;
                if (threshold >= now - reference) {
                    ++current;
                    continue;
                }

                const std::uint32_t nativeSocket = current->first;
                const bool inserted =
                    expired.emplace(nativeSocket, client).second;
                if (inserted) {
                    client->AddRef();
                } else {
                    // CFileLog::g_pDefaultFileLog->LogT(
                    //     LOG_ERROR,
                    //     u"[%s(%d)] - tempMap.insert, SOCKET(%d)",
                    //     u"CClientSocketContainer::CheckSocketTimeout",
                    //     option == ST_LAST_IO ? 0x92 : 0x78,
                    //     static_cast<std::int32_t>(nativeSocket))
                    // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
                    //     u"component=client event=timeout_index_conflict socket=%u session=%d timeout_kind=%s action=retain_existing_entry",
                    //     nativeSocket, client->m_iSessionId, option == ST_LAST_IO ? u"idle" : u"connection_age")
                    // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
                    //     u"[client] Socket %u, session %d conflicts with an existing timeout entry (%s). Keeping the existing entry.",
                    //     nativeSocket, client->m_iSessionId, option == ST_LAST_IO ? u"idle" : u"connection age")
                    L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
                        u"[client] Socket %u, session %d conflicts with an existing timeout entry (%s). Keeping the existing entry.",
                        nativeSocket, client->m_iSessionId, option == ST_LAST_IO ? u"idle" : u"connection age");
                }

                current = m_sockMap.erase(current);
                if (m_gateSessionMap.erase(client->m_iSessionId) == 0) {
                    // CFileLog::g_pDefaultFileLog->LogT(
                    //     LOG_ERROR,
                    //     u"[%s(%d)] - sessionMap.erase, SESSIONID(%d)",
                    //     u"CClientSocketContainer::CheckSocketTimeout",
                    //     option == ST_LAST_IO ? 0x97 : 0x7d,
                    //     client->m_iSessionId)
                    // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
                    //     u"component=client event=timeout_session_missing socket=%u session=%d timeout_kind=%s",
                    //     nativeSocket, client->m_iSessionId, option == ST_LAST_IO ? u"idle" : u"connection_age")
                    // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
                    //     u"[client] Socket %u, session %d is missing from the session index during %s timeout processing.",
                    //     nativeSocket, client->m_iSessionId, option == ST_LAST_IO ? u"idle" : u"connection age")
                    // Original: L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
                    //     u"[client] Socket %u, session %d is missing from the session index during %s timeout processing.",
                    //     nativeSocket, client->m_iSessionId, option == ST_LAST_IO ? u"idle" : u"connection age")
                    L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
                        u"[client] Socket %u, session %d is missing from the session index during %s timeout processing. The socket registration has already been removed; timeout cleanup continues.",
                        nativeSocket, client->m_iSessionId, option == ST_LAST_IO ? u"idle" : u"connection age");
                }
            }
        }
    }

    for (const auto& entry : expired) {
        CClientSocket* const client = entry.second;
        // CFileLog::g_pDefaultFileLog->LogDbgT(
        //     LOG_NORMAL,
        //     u"*Timeout session, %ld",
        //     client->m_iSessionId)
        // CFileLog::g_pDefaultFileLog->LogT(LOG_NORMAL,
        //     u"component=client event=timeout socket=%u session=%d timeout_kind=%s threshold_ms=%u action=close_connection",
        //     entry.first, client->m_iSessionId, option == ST_LAST_IO ? u"idle" : u"connection_age", threshold)
        // CFileLog::g_pDefaultFileLog->LogT(LOG_NORMAL,
        //     u"[client] Socket %u, session %d reached its %s timeout (%u ms). Closing the connection.",
        //     entry.first, client->m_iSessionId, option == ST_LAST_IO ? u"idle" : u"connection age", threshold)
        L2_LOG_INFO(*CFileLog::g_pDefaultFileLog,
            u"[client] Socket %u, session %d reached its %s timeout (%u ms). Closing the connection.",
            entry.first, client->m_iSessionId, option == ST_LAST_IO ? u"idle" : u"connection age", threshold);
        client->CloseSocket();
        client->ReleaseRef();
    }
}
