#include "Raid.h"

#include "NpcServer.h"
#include "ObjectDB.h"
#include "Pledge.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Config.h"
#include "Shared/src/Log.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/Utils.h"
#include "Shared/src/net/Serialize.h"

#include <array>
#include <cstdint>
#include <string>


bool DummyRaidServerPacket(RaidServerSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     "DummyPacket called in RaidServer")
    // L2_LOG_ERROR(Log,
    //     "DummyPacket called in RaidServer")
    L2_LOG_ERROR(Log, "Unhandled packet reached the raid-server response table.");
    return false;
}

RaidServerPacketCallbackMap g_RaidServerPacketTable(&DummyRaidServerPacket, u"");

bool RaidDelegatePacket(RaidServerSocket*, const unsigned char* const packet) {
    // Current L2Server 0x00a1f02c.
    L2_GUARD_FUNCTION();
    int characterDatabaseId = 0;
    int packetProtocol = 0;
    const unsigned char* const delegatedPacket = ::Disassemble(packet, "dd", &characterDatabaseId, &packetProtocol);
    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, characterDatabaseId));
    if (user) {
        CUserSocket* const userSocket = user->GetSocket();
        userSocket->m_pPacketTable[packetProtocol](userSocket, delegatedPacket);
    }
    return false;
}

bool RaidDoNotRelayForPledgePacket(RaidServerSocket*, const unsigned char* const packet) {
    // Current L2Server 0x00a1ef30.
    L2_GUARD_FUNCTION();
    int pledgeDatabaseId = 0;
    ::Disassemble(packet, "d", &pledgeDatabaseId);
    CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, pledgeDatabaseId);
    if (pledge) {
        pledge->SetUnderRaid(false);
    }
    return false;
}

bool RaidRequestSayOriginPacket(RaidServerSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a1f794.
    L2_GUARD_FUNCTION();
    int pledgeDatabaseId = 0;
    int serverId = 0;
    int characterDatabaseId = 0;
    std::array<char16_t, 24> senderName{};
    std::array<char16_t, 256> message{};
    SayPacketType type = static_cast<SayPacketType>(0);
    ::Disassemble(
        packet, "dddSSd", &pledgeDatabaseId, &serverId, &characterDatabaseId, 0x30, senderName.data(), 0x200, message.data(),
        &type
    );

    CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, pledgeDatabaseId);
    if (pledge) {
        pledge->SayByMobile(serverId, characterDatabaseId, senderName.data(), message.data(), type);
        RaidClientManager::inst().RequestPledgeChattingRelay(
            *pledge.get(), serverId, characterDatabaseId, senderName.data(), message.data(), type, socket->GetWorldId()
        );
    }
    return false;
}

bool RaidReserveResultPacket(RaidServerSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a1f3fc.
    L2_GUARD_FUNCTION();
    int characterDatabaseId = 0;
    int result = 0;
    ::Disassemble(packet, "dd", &characterDatabaseId, &result);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][RAID][06] RSC_RESERVE_RESULT[%d] protocol is received. [%d][%d]",
    //     result,
    //     socket->GetWorldId(),
    //     characterDatabaseId)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][RAID][06] RSC_RESERVE_RESULT[%d] protocol is received. [%d][%d]",
    //     result,
    //     socket->GetWorldId(),
    //     characterDatabaseId)
    L2_LOG_INFO(
        Log, "Raid reservation response received: result %d, world %d, character database ID %d.", result,
        socket->GetWorldId(), characterDatabaseId
    );
    RaidClientManager::inst().RaidReserveRequestPacket(socket->GetWorldId(), characterDatabaseId, result == 1);
    return false;
}

bool RaidRestartOnRaidServerPacket(RaidServerSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a1f698.
    L2_GUARD_FUNCTION();
    int characterDatabaseId = 0;
    int characterServerId = 0;
    ::Disassemble(packet, "dd", &characterDatabaseId, &characterServerId);
    if (!RaidClientManager::inst().RaidRestartOnRaidServerPacket(characterDatabaseId, characterServerId)) {
        socket->Send("cdd", 0x1e, characterServerId, 0);
    }
    return false;
}

bool RaidReturnToOriginPacket(RaidServerSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a1f5c8.
    L2_GUARD_FUNCTION();
    int characterDatabaseId = 0;
    int characterServerId = 0;
    ::Disassemble(packet, "dd", &characterDatabaseId, &characterServerId);
    RaidClientManager::inst().RaidReturnToOriginPacket(characterDatabaseId, characterServerId, socket->GetWorldId());
    return false;
}

bool RaidServerVersionPacket(RaidServerSocket*, const unsigned char* const packet) {
    // Current L2Server 0x00a1f31c.
    L2_GUARD_FUNCTION();
    int worldId = 0;
    int result = 0;
    int quota = 0;
    const unsigned char* const remainder = ::Disassemble(packet, "ddd", &worldId, &result, &quota);
    RaidClientManager::inst().RaidServerVersionPacket(worldId, result, quota, remainder);
    return false;
}

bool RaidUserConfirmedPacket(RaidServerSocket*, const unsigned char* const packet) {
    // Current L2Server 0x00a1f510.
    L2_GUARD_FUNCTION();
    int characterDatabaseId = 0;
    ::Disassemble(packet, "d", &characterDatabaseId);
    RaidClientManager::inst().RaidUserConfirmedPacket(characterDatabaseId);
    return false;
}

bool RaidUserDisconnectedPacket(RaidServerSocket*, const unsigned char* const packet) {
    // Current L2Server 0x00a1f15c.
    L2_GUARD_FUNCTION();
    int characterDatabaseId = 0;
    ::Disassemble(packet, "d", &characterDatabaseId);
    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, characterDatabaseId));
    if (user) {
        user->GetSocket()->ForceClose(SCR_RAID_QUIT);
    }
    return false;
}

bool Raid_RSC_WORLD_RAID_START(RaidServerSocket*, const unsigned char* const packet) {
    // Current L2Server 0x00a1e864.
    L2_GUARD_FUNCTION();
    int first = 0;
    int second = 0;
    int third = 0;
    ::Disassemble(packet, "ddd", &first, &second, &third);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR]WORLDRAID[%s][%d]RSC_WORLD_RAID_START recv: %d, %d, %d",
    //     L"RaidClientPacketHandler.cpp",
    //     0xd1,
    //     first,
    //     second,
    //     third)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR]WORLDRAID[%s][%d]RSC_WORLD_RAID_START recv: %d, %d, %d",
    //     L"RaidClientPacketHandler.cpp",
    //     0xd1,
    //     first,
    //     second,
    //     third)
    L2_LOG_INFO(
        Log, "World raid start response received: NPC server ID %d, result %d, first-started world %d; forwarding to L2NPC.",
        first, second, third
    );
    g_npcserver.Send("cddd", 0xc3, first, second, third);
    return false;
}

bool Raid_RSC_WORLD_RAID_STOP(RaidServerSocket*, const unsigned char*) {
    // Current L2Server 0x00a1e994.
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR]WORLDRAID[%s][%d]RSC_WORLD_RAID_STOP recv",
    //     L"RaidClientPacketHandler.cpp",
    //     0xde)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR]WORLDRAID[%s][%d]RSC_WORLD_RAID_STOP recv",
    //     L"RaidClientPacketHandler.cpp",
    //     0xde)
    L2_LOG_INFO(Log, "World raid stop response received; no further action in this handler.");
    return false;
}

bool Raid_RSC_WORLD_RAID_BROADCAST_EVENT(RaidServerSocket*, const unsigned char* const packet) {
    // Current L2Server 0x00a1ea4c.
    L2_GUARD_FUNCTION();
    int first = 0;
    int second = 0;
    int third = 0;
    int fourth = 0;
    int fifth = 0;
    ::Disassemble(packet, "ddddd", &first, &second, &third, &fourth, &fifth);
    g_npcserver.Send("cddddd", 0xc5, first, second, third, fourth, fifth);
    return false;
}

L2_BIND_PACKET(kBindRaidServerVersionPacket, g_RaidServerPacketTable, 0x00, &RaidServerVersionPacket);
L2_BIND_PACKET(kBindRaidReserveResultPacket, g_RaidServerPacketTable, 0x01, &RaidReserveResultPacket);
L2_BIND_PACKET(kBindRaidUserConfirmedPacket, g_RaidServerPacketTable, 0x02, &RaidUserConfirmedPacket);
L2_BIND_PACKET(kBindRaidReturnToOriginPacket, g_RaidServerPacketTable, 0x03, &RaidReturnToOriginPacket);
L2_BIND_PACKET(kBindRaidDelegatePacket, g_RaidServerPacketTable, 0x05, &RaidDelegatePacket);
L2_BIND_PACKET(kBindRaidUserDisconnectedPacket, g_RaidServerPacketTable, 0x06, &RaidUserDisconnectedPacket);
L2_BIND_PACKET(kBindRaidRestartOnRaidServerPacket, g_RaidServerPacketTable, 0x07, &RaidRestartOnRaidServerPacket);
L2_BIND_PACKET(kBindRaidRequestSayOriginPacket, g_RaidServerPacketTable, 0x08, &RaidRequestSayOriginPacket);
L2_BIND_PACKET(kBindRaidDoNotRelayForPledgePacket, g_RaidServerPacketTable, 0x09, &RaidDoNotRelayForPledgePacket);
L2_BIND_PACKET(kBindRaidRscWorldRaidStart, g_RaidServerPacketTable, 0x0c, &Raid_RSC_WORLD_RAID_START);
L2_BIND_PACKET(kBindRaidRscWorldRaidStop, g_RaidServerPacketTable, 0x0d, &Raid_RSC_WORLD_RAID_STOP);
L2_BIND_PACKET(kBindRaidRscWorldRaidBroadcastEvent, g_RaidServerPacketTable, 0x0e, &Raid_RSC_WORLD_RAID_BROADCAST_EVENT);
