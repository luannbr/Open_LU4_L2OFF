#include "CAnnounce.h"

#include "CacheSocket.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/DBConn.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/TextRuntime.h"

#include <array>
#include <cstring>

CAnnounce g_announce;

CAnnounce::CAnnounce() :
    m_lock{}, m_loaded(false), m_loginAnnoncesPacket{}, m_loginAnnoncesSize(0), m_loginAnnoncesNumber(0) {
    // Current Cached.exe 0x0044f6e4.
    L2_GUARD_FUNCTION();
    l2portable_detail::InitializeCriticalSection(m_lock);
}

CAnnounce::~CAnnounce() {
    // Current Cached.exe 0x0044f798.
    L2_GUARD_FUNCTION();
    l2portable_detail::DeleteCriticalSection(m_lock);
}

void CAnnounce::ReadAnnounce(bool reload) {
    // Current Cached.exe 0x0044f82c.
    L2_GUARD_FUNCTION();
    if (m_loaded && !reload)
        return;

    int announceId = 0;
    std::array<char16_t, 255> announceMessage{};
    std::memset(m_loginAnnoncesPacket, 0, sizeof(m_loginAnnoncesPacket));
    m_loginAnnoncesSize = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&announceId);
    connection.Bind(announceMessage.data(), 0x1fe);

    int announceCount = 0;
    constexpr const wchar_t* statement = L"{Call lin_GetLoginAnnounce}";
    connection.Prepare(statement);
    if (connection.ExecutePrepared(statement)) {
        while (announceCount < 10 && connection.Fetch()) {
            // The current body binds announce_id but encodes the zero-based
            // fetch index, preserving the original login packet contract.
            const int assembledSize = ::Assemble(
                reinterpret_cast<char*>(m_loginAnnoncesPacket) + m_loginAnnoncesSize, 0x1000 - m_loginAnnoncesSize, "dS",
                announceCount, announceMessage.data()
            );
            if (assembledSize > 0) {
                m_loginAnnoncesSize += assembledSize;
            }
            ++announceCount;
        }
    }

    m_loginAnnoncesNumber = announceCount;
    m_loaded = true;
}

bool CAnnounce::ShowIntervalAnnounce(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0044fa08.
    L2_GUARD_FUNCTION();

    int creatureId = 0;
    int repetitive = 0;
    ::Disassemble(packet, "dd", &creatureId, &repetitive);

    int interval = 0;
    int announceId = 0;
    std::array<char16_t, 100> announceMessage{};
    std::array<char, 0x2000> payload{};
    int payloadSize = 0;
    int announceCount = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&interval);
    connection.Bind(&announceId);
    connection.Bind(announceMessage.data(), 200);

    constexpr const wchar_t* statement = L"{Call lin_GetIntervalAnnounce}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement)) {
        // The old source omitted the two zero-length blob varargs. Supplying
        // them avoids host varargs UB while producing the same
        // current zero-length wire payload.
        socket->Send("cdddb", 0x8c, creatureId, repetitive, 0, 0, nullptr);
        return true;
    }

    while (connection.Fetch()) {
        const int assembledSize = ::Assemble(
            payload.data() + payloadSize, static_cast<int>(payload.size()) - payloadSize, "ddS", interval, announceId,
            announceMessage.data()
        );
        if (assembledSize > 0) {
            ++announceCount;
            payloadSize += assembledSize;
        }
        announceMessage.fill(u'\0');
    }

    socket->Send("cdddb", 0x8c, creatureId, repetitive, announceCount, payloadSize, payload.data());
    return true;
}

bool CAnnounce::LoadIntervalAnnounce(CSocket* socket, const std::uint8_t* packet [[maybe_unused]]) {
    // Current Cached.exe 0x0044fcb0.
    L2_GUARD_FUNCTION();

    int interval = 0;
    int announceId = 0;
    std::array<char16_t, 100> announceMessage{};
    std::array<char, 0x2000> payload{};
    int payloadSize = 0;
    int announceCount = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&interval);
    connection.Bind(&announceId);
    connection.Bind(announceMessage.data(), 200);

    constexpr const wchar_t* statement = L"{Call lin_GetIntervalAnnounce}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement)) {
        return true;
    }

    while (connection.Fetch()) {
        const int assembledSize = ::Assemble(
            payload.data() + payloadSize, static_cast<int>(payload.size()) - payloadSize, "ddS", interval, announceId,
            announceMessage.data()
        );
        if (assembledSize > 0) {
            ++announceCount;
            payloadSize += assembledSize;
        }
        announceMessage.fill(u'\0');
    }

    if (announceCount > 0) {
        socket->Send("cdb", 0x91, announceCount, payloadSize, payload.data());
    } else {
        static const char emptyPayload = '\0';
        socket->Send("cdb", 0x91, 0, 0, &emptyPayload);
    }
    return true;
}

bool CAnnounce::SetIntervalAnnounce(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x0044ff38.
    L2_GUARD_FUNCTION();

    int creatureId = 0;
    int interval = 0;
    int announceId = 0;
    std::array<char16_t, 100> announceMessage{};
    ::Disassemble(packet, "dddS", &creatureId, &interval, &announceId, 200, announceMessage.data());

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetIntervalAnnounce (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&interval);
    connection.BindParameter(&announceId);
    connection.BindParameter(announceMessage.data());
    const bool success = connection.ExecutePrepared(statement);

    socket->Send("cdddS", 0x8d, creatureId, interval, success ? announceId : 0, announceMessage.data());
    return true;
}

bool CAnnounce::DelIntervalAnnounce(CSocket* socket, const std::uint8_t* packet) {
    // Current Cached.exe 0x00450138.
    L2_GUARD_FUNCTION();

    int creatureId = 0;
    int interval = 0;
    int announceId = 0;
    ::Disassemble(packet, "ddd", &creatureId, &interval, &announceId);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DelIntervalAnnounce (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&interval);
    connection.BindParameter(&announceId);
    const bool success = connection.ExecutePrepared(statement);

    socket->Send("cddd", 0x8e, creatureId, interval, success ? announceId : 0);
    return true;
}

int CAnnounce::GetLoginAnnoncesNumber() const { return m_loginAnnoncesNumber; }

int CAnnounce::GetPacketSize() const {
    // Current Cached.exe 0x0044f6dc.
    return m_loginAnnoncesSize;
}

const std::uint8_t* CAnnounce::GetLoginAnnoncesPacket() const { return m_loginAnnoncesPacket; }

void CAnnounce::Lock() { l2portable_detail::EnterCriticalSection(m_lock); }

void CAnnounce::Unlock() { l2portable_detail::LeaveCriticalSection(m_lock); }
