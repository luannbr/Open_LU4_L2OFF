#include "NpcServer.h"
#include "Shared/src/MemorySub.h"

#include "NPC.h"

#include "Shared/src/Exception.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/ShareInfo.h"
#include "Shared/src/Socket.h"
#include "Shared/src/TextRuntime.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <ctime>

namespace {

constexpr std::size_t kConnectionLogCapacity = 20000;
constexpr std::size_t kConnectionLogAppendLimit = 19000;

// Current RTTI fixes NpcSocket as a direct, zero-offset CSocket base. The
// complete NpcSocket surface is recovered separately. This file needs only
// the already-closed base contract used by the current NpcServer code.
CSocket* AsSocket(NpcSocket* socket) noexcept { return reinterpret_cast<CSocket*>(socket); }

NpcSocket* AsNpcSocket(CSocket* socket) noexcept { return reinterpret_cast<NpcSocket*>(socket); }

std::size_t Utf16Length(const char16_t* text) noexcept {
    std::size_t length = 0;
    while (length < kConnectionLogCapacity && text[length] != u'\0') {
        ++length;
    }
    return length;
}

void AppendUtf16(char16_t* destination, const char16_t* source) noexcept {
    std::size_t destinationLength = Utf16Length(destination);
    while (destinationLength + 1 < kConnectionLogCapacity && *source != u'\0') {
        destination[destinationLength++] = *source++;
    }
    destination[destinationLength] = u'\0';
}

void AppendTime32(char16_t* destination, std::int32_t value) noexcept {
#if defined(_MSC_VER)
    const __time32_t targetTime = static_cast<__time32_t>(value);
    const wchar_t* text = ::_wctime32(&targetTime);
    if (!text) {
        return;
    }
    std::size_t destinationLength = Utf16Length(destination);
    while (destinationLength + 1 < kConnectionLogCapacity && *text != L'\0') {
        destination[destinationLength++] = static_cast<char16_t>(*text++);
    }
    destination[destinationLength] = u'\0';
#else
    const std::time_t hostTime = static_cast<std::time_t>(value);
    const char* text = std::ctime(&hostTime);
    if (!text) {
        return;
    }
    std::size_t destinationLength = Utf16Length(destination);
    while (destinationLength + 1 < kConnectionLogCapacity && *text != '\0') {
        destination[destinationLength++] = static_cast<char16_t>(static_cast<unsigned char>(*text++));
    }
    destination[destinationLength] = u'\0';
#endif
}

void AppendConnectionEvent(const char16_t* label, std::int32_t time) noexcept {
    if (Utf16Length(g_sNpcServerConnectionLog) >= kConnectionLogAppendLimit) {
        return;
    }
    AppendUtf16(g_sNpcServerConnectionLog, label);
    AppendTime32(g_sNpcServerConnectionLog, time);
}

void SendRequestNpcSpawnBase(
    const char16_t* npcName,
    FVector position,
    int inZoneId,
    const char16_t* aiName,
    int value5,
    char value6,
    int value7,
    int value8,
    char value9,
    unsigned int value10,
    unsigned int value11,
    char value12,
    int value13,
    int value14,
    int value15
) {
    // Both S operands are UTF-16. Preserve the same packet bytes on every host.
    g_npcserver.Send(
        "cSddddSdcddcddcddd", 0x0b, npcName, static_cast<int>(position.x), static_cast<int>(position.y),
        static_cast<int>(position.z), inZoneId, aiName != nullptr ? aiName : u"", value5, value6, value7, value8, value9,
        value10, value11, value12, value13, value14, value15
    );
}

} // namespace

NpcServer::NpcServer() {
    m_nNpcSocket = 0;
    m_nCurrentSocket = 0;
    m_pNpcSockets[0] = nullptr;
    m_nConnected = 0;
    m_pSocketTable[0] = nullptr;
    m_ConnectTime = 0;
    m_CloseTime = 0;
}

NpcServer::~NpcServer() = default;

L2_DEFINE_OBJECT_CLASS_ID(NpcServer)

L2_DEFINE_OBJECT_CLASS_NAME(NpcServer, m_pClassName_NpcServer)

L2_DEFINE_OBJECT_ALLOCATOR(NpcServer, __GetClassName())

L2_DEFINE_OBJECT_NEW(NpcServer)

L2_DEFINE_OBJECT_ADD_REF(NpcServer)

L2_DEFINE_OBJECT_RELEASE(NpcServer)

NpcServer* NpcServer::Inst() noexcept { return &g_npcserver; }

NpcSocket* NpcServer::GetNpcSocket() {
    L2_GUARD_FUNCTION();
    ::runtime::raw_atomic::IncrementI32(&m_nCurrentSocket);
    return m_pSocketTable[0];
}

int NpcServer::RemoveEx(std::uint64_t socket) {
    L2_GUARD_FUNCTION();

    const int remaining = [&] {
        L2_GUARD(m_npcSocketSect);
        m_CloseTime = runtime::crt::Time32Now();
        AppendConnectionEvent(u"\tClose\t: ", m_CloseTime);

        NpcSocket* removed = nullptr;
        int removedIndex = -1;
        for (int index = 0; index < NPC_SOCKET_MAX; ++index) {
            NpcSocket* const candidate = m_pNpcSockets[static_cast<std::size_t>(index)];
            if (candidate && AsSocket(candidate)->m_Socket == socket) {
                removed = candidate;
                removedIndex = index;
                break;
            }
        }

        if (removedIndex >= 0) {
            const std::size_t index = static_cast<std::size_t>(removedIndex);
            m_pNpcSockets[index] = nullptr;
            for (int next = removedIndex + 1; next < m_nNpcSocket; ++next) {
                m_pNpcSockets[static_cast<std::size_t>(next - 1)] = m_pNpcSockets[static_cast<std::size_t>(next)];
                m_pNpcSockets[static_cast<std::size_t>(next)] = nullptr;
            }
            --m_nNpcSocket;
        }

        {
            L2_GUARD(m_lockTable[0]);
            m_pSocketTable[0] = m_nNpcSocket != 0 ? m_pNpcSockets[0] : nullptr;
        }

        if (removed) {
            L2_RELEASE(AsSocket(removed), ObjectRefType::Anonymous);
        }

        ::runtime::raw_atomic::StoreI32Relaxed(&m_nConnected, m_nNpcSocket > 0 ? 1 : 0);
        const int remaining = m_nNpcSocket;
        return remaining;
    }();
    return remaining;
}

void NpcServer::SetConnected(int connected) {
    L2_GUARD_FUNCTION();
    NpcSpawnInfo::Inst()->Init();
    ::runtime::raw_atomic::ExchangeI32(&m_nConnected, connected);
}

bool NpcServer::IsConnect() {
    L2_GUARD_FUNCTION();
    return GetNpcSocket() != nullptr;
}

bool NpcServer::Connected() const noexcept { return ::runtime::raw_atomic::LoadI32Relaxed(&m_nConnected) > 0; }

void NpcServer::Send(const char* format, ...) {
    L2_GUARD_FUNCTION();

    NpcSocket* const npcSocket = GetNpcSocket();
    if (!npcSocket) {
        return;
    }

    ::runtime::raw_atomic::IncrementI32(&g_nTotalOutgoingNpcPacketCount);
    CSocket* const socket = AsSocket(npcSocket);
    L2_ADD_REF(socket, ObjectRefType::Anonymous);

    va_list arguments;
    va_start(arguments, format);
    socket->SendV(format, arguments);
    va_end(arguments);

    L2_RELEASE(socket, ObjectRefType::Anonymous);
}

void NpcServer::SendNPCCreatePacket(const NPCCreateParam& parameter) {
    L2_GUARD_FUNCTION();

    // The current Windows target passes its two-byte wchar_t string directly
    // to the packet assembler's UTF-16 `S` field. Host wchar_t is four bytes
    // on Unix-like systems, so keep the same wire units explicitly.
    const std::u16string targetAiName = WideToUtf16(parameter.aiName);

    Send(
        "cddpddddddddSdbdd", 1, parameter.pNPC->GetIndex(), static_cast<int>(parameter.pNPC->m_nId), parameter.pDefine,
        parameter.makerId, static_cast<int>(parameter.pos.x), static_cast<int>(parameter.pos.y),
        static_cast<int>(parameter.pos.z), parameter.dir, parameter.isMercenary, parameter.hp, parameter.mp,
        targetAiName.c_str(), parameter.paramCount, parameter.bufLen, parameter.pBuf, parameter.airshipSID,
        parameter.inzoneSID
    );
}

void NpcServerUtil::SendRequestDecoNpcSpawn(
    const char16_t* npcName, FVector position, int inZoneId, int heading, char decoType, int decoId, int decoLevel
) {
    L2_GUARD_FUNCTION();
    SendRequestNpcSpawnBase(npcName, position, inZoneId, u"", 0, 0, 0, heading, 0, 0, 0, decoType, decoId, decoLevel, 0);
}

void NpcServerUtil::SendRequestNpcSpawn(
    const char16_t* npcName, FVector position, int inZoneId, const char16_t* aiName, int heading
) {
    L2_GUARD_FUNCTION();
    SendRequestNpcSpawnBase(npcName, position, inZoneId, aiName, 0, 0, 0, heading, 0, 0, 0, 0, 0, 0, 0);
}

void NpcServerUtil::SendRequestFakeNpcSpawn(
    const char16_t* npcName, FVector position, int inZoneId, int fakeNpcId, int heading
) {
    L2_GUARD_FUNCTION();
    SendRequestNpcSpawnBase(npcName, position, inZoneId, u"", 0, 1, fakeNpcId, heading, 0, 0, 0, 0, 0, 0, 0);
}

void NpcServerUtil::SendRequestDroidNpcSpawn(
    const char16_t* npcName,
    FVector position,
    int inZoneId,
    int heading,
    char droidType,
    unsigned int masterId,
    unsigned int groupId
) {
    L2_GUARD_FUNCTION();
    SendRequestNpcSpawnBase(npcName, position, inZoneId, u"", 0, 0, 0, heading, droidType, masterId, groupId, 0, 0, 0, 0);
}

void NpcServerUtil::SendDeleteOlympiadTradePointReturned(int npcSid, int userSid, int olympiadPoint, int result) {
    L2_GUARD_FUNCTION();
    g_npcserver.Send("cdddd", 0x54, npcSid, userSid, olympiadPoint, result);
}

void NpcServerUtil::SendOlympiadMatchResult(int olympiadGameId, int result) {
    L2_GUARD_FUNCTION();
    g_npcserver.Send("cddd", 0xa0, olympiadGameId, result, 1);
}

void NpcServerUtil::SendRequestReservNpcSpawn(
    const char16_t* npcName, FVector position, int inZoneId, const char16_t* aiName, int heading, int reservationId
) {
    L2_GUARD_FUNCTION();
    SendRequestNpcSpawnBase(npcName, position, inZoneId, aiName, 0, 0, 0, heading, 0, 0, 0, 0, 0, 0, reservationId);
}

bool NpcServer::Add(std::uint64_t socket, CSocket* client) {
    L2_GUARD_FUNCTION();

    bool added = false;
    {
        L2_GUARD(m_npcSocketSect);
        m_ConnectTime = runtime::crt::Time32Now();
        AppendConnectionEvent(u"\tConnect\t: ", m_ConnectTime);

        if (m_nNpcSocket >= 0 && m_nNpcSocket < NPC_SOCKET_MAX) {
            L2_ADD_REF(client, ObjectRefType::Anonymous);
            m_pNpcSockets[static_cast<std::size_t>(m_nNpcSocket)] = AsNpcSocket(client);
            ++m_nNpcSocket;
            added = true;
        }

        {
            L2_GUARD(m_lockTable[0]);
            m_pSocketTable[0] = m_pNpcSockets[0];
        }
    }

    Server::Add(socket, client);
    SetConnected(1);
    return added;
}
