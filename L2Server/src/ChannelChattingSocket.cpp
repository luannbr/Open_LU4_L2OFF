#include "ChannelChattingSocket.h"

#include "ChannelChatting.h"
#include "L2Time.h"
#include "Object.h"
#include "User.h"

#include "Shared/src/IOC.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"

#include <array>
#include <cstdint>
#include <cstring>

bool DummyPacket(CChannelChattingSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     "DummyPacket called in CChannelChatting")
    // L2_LOG_ERROR(Log,
    //     "DummyPacket called in CChannelChatting")
    L2_LOG_WARN(Log, "Channel-chat packet has no registered handler; the fallback handler returns without processing it.");
    return false;
}

bool ChannelChattingServerVersion(CChannelChattingSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    ChannelChattingClient::Inst().SendKeepAlive();
    g_L2Time.AddTimer(30000, 19);
    return false;
}

bool ChannelChattingAuthenticateUser(CChannelChattingSocket* const socket, const unsigned char* const packet) {
    L2_GUARD_FUNCTION();

    std::uint16_t nameLength = 0;
    std::array<char16_t, 25> name;
    name[0] = u'\0';
    std::memset(name.data() + 1, 0, 0x30);
    std::uint64_t gameServerId = 0;
    std::uint64_t userId = 0;
    std::int32_t reason;

    const unsigned char* const cursor = Disassemble(packet, "h", &nameLength);
    const std::uint64_t nameByteLength = static_cast<std::uint64_t>(nameLength) * 2;
    Disassemble(cursor, "bQQd", nameByteLength, name.data(), &gameServerId, &userId, &reason);

    CObject* const object = CObject::GetObjectW(static_cast<std::uint32_t>(userId));
    User* const user = object != nullptr && object->IsPC() ? object->CastUser() : nullptr;
    if (user != nullptr) {
        // The current call order is account ID first, DB ID second. Both
        // remain 32-bit variadic arguments even though the format uses Q.
        // Win64 so leaves each stack slot's high dword untouched.
        const std::uint32_t accountId = user->GetAccountID();
        const std::uint32_t databaseId = user->GetDBID();
        socket->Send(
            "hhbQQQQdddddd", 0x4004, nameLength, nameByteLength, name.data(), gameServerId, userId, databaseId, accountId,
            reason, 0, 1, 0, 0, 0
        );
    } else {
        socket->Send(
            "hhbQQQQdddddd", 0x4004, nameLength, nameByteLength, name.data(), gameServerId, userId, std::uint64_t{0},
            std::uint64_t{0}, reason, 1, 2, 0, 0, 0
        );
    }
    return false;
}

bool ChannelChattingAuthenticatePledge(CChannelChattingSocket* const socket, const unsigned char* const packet) {
    L2_GUARD_FUNCTION();

    std::uint16_t nameLength;
    std::array<char16_t, 25> name;
    name[0] = u'\0';
    std::memset(name.data() + 1, 0, 0x30);
    std::uint64_t gameServerId = 0;
    std::uint64_t userId = 0;
    std::int32_t reason = 0;

    const unsigned char* const cursor = Disassemble(packet, "h", &nameLength);
    const std::uint64_t nameByteLength = static_cast<std::uint64_t>(nameLength) * 2;
    Disassemble(cursor, "bQQd", nameByteLength, name.data(), &gameServerId, &userId, &reason);

    CObject* const object = CObject::GetObjectW(static_cast<std::uint32_t>(userId));
    User* const user = object != nullptr && object->IsPC() ? object->CastUser() : nullptr;
    if (user == nullptr) {
        socket->Send("hhbQQdd", 0x4004, nameLength, nameByteLength, name.data(), gameServerId, userId, reason, 1);
    } else if (user->GetPledge() == nullptr) {
        socket->Send("hhbQQdd", 0x4004, nameLength, nameByteLength, name.data(), gameServerId, userId, reason, 2);
    } else {
        socket->Send("hhbQQdd", 0x4004, nameLength, nameByteLength, name.data(), gameServerId, userId, reason, 0);
    }
    return false;
}

ChannelChattingPacketCallbackMap gPacketTableFromChannelChatting(&DummyPacket, u"A2G_PROTOCOL");

namespace {

L2_BIND_PACKET(IsRegisteredChannelChattingServerVersion, gPacketTableFromChannelChatting, 7, &ChannelChattingServerVersion);
L2_BIND_PACKET(
    IsRegisteredChannelChattingAuthenticateUser, gPacketTableFromChannelChatting, 0x4003, &ChannelChattingAuthenticateUser
);
L2_BIND_PACKET(
    IsRegisteredChannelChattingAuthenticatePledge,
    gPacketTableFromChannelChatting,
    0x4008,
    &ChannelChattingAuthenticatePledge
);

} // namespace

ChannelChattingPacketCallback* ChannelChattingPacketTable = gPacketTableFromChannelChatting.GetData();
