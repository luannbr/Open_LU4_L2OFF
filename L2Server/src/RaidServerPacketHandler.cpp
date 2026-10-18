#include <array>
#include <cstdint>
#include <string>
#include "Alliance.h"
#include "DB.h"
#include "Pledge.h"
#include "PledgeMastery.h"
#include "PledgeMemberService.h"
#include "PledgeStuff.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Raid.h"
#include "Shared/src/Log.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/UserConst.h"
#include "Shared/src/Utils.h"
#include "Shared/src/net/Serialize.h"
#include "User.h"
#include "UserSocket.h"

using RaidClientPacketCallback = bool (*)(RaidClientSocket*, const unsigned char*);
using RaidClientPacketCallbackMap = FixedSizeCallbackMap<39, RaidClientPacketCallback>;


bool DummyRaidClientPacket(RaidClientSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     "DummyPacket called in RaidClient")
    // L2_LOG_ERROR(Log,
    //     "DummyPacket called in RaidClient")
    L2_LOG_ERROR(Log, "Unhandled packet reached the raid-client request table.");
    return false;
}

RaidClientPacketCallbackMap g_RaidClientPacketTable(&DummyRaidClientPacket, u"");

bool RaidClientVersionPacket(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a1fc94.
    L2_GUARD_FUNCTION();
    int worldId = 0;
    ::Disassemble(packet, "d", &worldId);
    RaidServerManager::inst().RaidClientVersionPacket(worldId, socket);
    return false;
}

bool RaidReserveUserPacket(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a1fd60.
    L2_GUARD_FUNCTION();
    RaidServerManager::inst().RaidReserveUserPacket(socket, packet);
    return false;
}

bool RaidCancelReservedPacket(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a1fe10.
    L2_GUARD_FUNCTION();
    int characterDatabaseId = 0;
    ::Disassemble(packet, "d", &characterDatabaseId);
    RaidServerManager::inst().DeleteRaidCandidate(socket->GetWorldId(), characterDatabaseId);
    return false;
}

bool RaidRequestRelayPacket(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a1fee0.
    L2_GUARD_FUNCTION();
    int characterDatabaseId = 0;
    int packetLength = 0;
    const unsigned char* const relayPacket = ::Disassemble(packet, "dd", &characterDatabaseId, &packetLength);
    User* const attacker =
        RaidServerManager::inst().GetAttacker(RaidServerManager::RaidKey(socket->GetWorldId(), characterDatabaseId));
    if (attacker != nullptr && packetLength > 0) {
        attacker->GetSocket()->Send("b", packetLength, relayPacket);
    }
    return false;
}

bool RaidDisconnectUser(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a1fff4.
    L2_GUARD_FUNCTION();
    int worldId = 0;
    int characterDatabaseId = 0;
    ::Disassemble(packet, "dd", &worldId, &characterDatabaseId);
    RaidServerManager::inst().DisconnectRaidUser(worldId, characterDatabaseId);
    if (socket != nullptr) {
        socket->OnUserSocketClosed();
    }
    return false;
}

bool RaidConfirmReturnToOrigin(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a21740.
    L2_GUARD_FUNCTION();
    std::uint32_t userServerId = 0;
    int success = 0;
    ::Disassemble(packet, "dd", &userServerId, &success);

    CObject* const object = CObject::GetObjectW(userServerId);
    User* user = nullptr;
    if (object != nullptr && object->IsPC()) {
        user = object->CastUser();
    }
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [RaidConfirmReturnToOrigin] User object does not exist",
        //     L"RaidServerPacketHandler.cpp",
        //     0x2bd)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [RaidConfirmReturnToOrigin] User object does not exist",
        //     L"RaidServerPacketHandler.cpp",
        //     0x2bd)
        L2_LOG_WARN(
            Log, "Raid return confirmation ignored: object ID %u is unavailable or is not a player.",
            static_cast<unsigned int>(userServerId)
        );
        return false;
    }

    CUserSocket* const userSocket = user->GetSocket();
    if (userSocket == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Return to origin failed. success flag [%d]",
        //     L"RaidServerPacketHandler.cpp",
        //     0x2e4,
        //     success)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Return to origin failed. success flag [%d]",
        //     L"RaidServerPacketHandler.cpp",
        //     0x2e4,
        //     success)
        L2_LOG_WARN(
            Log, "Raid return confirmation cannot continue for player object ID %u: socket unavailable (result %d).",
            static_cast<unsigned int>(userServerId), success
        );
        return false;
    }
    if (success == 0) {
        userSocket->SendSystemMessage(SystemMessageId::CANNOT_TELEPORT_DIMENSIONAL_SPACE_CLOSED);
        return false;
    }

    userSocket->m_nCloseReason = SCR_RETURN_TO_ORIGIN;
    {
        L2_GUARD(userSocket->mLock);
        userSocket->UnBindUser();
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][RAID][02] S_EX_CLOSE_RAID_SOCKET protocol is sent. user[%d][%d] SID[%d]",
    //     static_cast<int>(user->GetDBID()),
    //     user->m_data->m_nWorldId,
    //     static_cast<int>(userServerId))
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][RAID][02] S_EX_CLOSE_RAID_SOCKET protocol is sent. user[%d][%d] SID[%d]",
    //     static_cast<int>(user->GetDBID()),
    //     user->m_data->m_nWorldId,
    //     static_cast<int>(userServerId))
    L2_LOG_INFO(
        Log, "Sending raid connection-close instruction for character database ID %u in world %d (object ID %u).",
        static_cast<unsigned int>(user->GetDBID()), user->m_data->m_nWorldId, static_cast<unsigned int>(userServerId)
    );
    userSocket->Send("chd", 0xfe, 0xb9, 0);
    if (socket != nullptr) {
        socket->OnUserSocketClosed();
    }
    return false;
}

bool RaidConfirmRestart(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a21980.
    L2_GUARD_FUNCTION();
    std::uint32_t userServerId = 0;
    int success = 0;
    ::Disassemble(packet, "dd", &userServerId, &success);

    CObject* const object = CObject::GetObjectW(userServerId);
    User* user = nullptr;
    if (object != nullptr && object->IsPC()) {
        user = object->CastUser();
    }
    if (user == nullptr) {
        return false;
    }

    CUserSocket* const userSocket = user->GetSocket();
    if (userSocket == nullptr) {
        return false;
    }
    if (success < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ERROR][RAID] ConfirmRestart failed [%d][%d] Socket Closed",
        //     static_cast<int>(user->GetDBID()),
        //     user->m_data->m_nWorldId)
        // L2_LOG_ERROR(Log,
        //     L"[ERROR][RAID] ConfirmRestart failed [%d][%d] Socket Closed",
        //     static_cast<int>(user->GetDBID()),
        //     user->m_data->m_nWorldId)
        L2_LOG_ERROR(
            Log, "Closing player connection after raid restart refusal: character database ID %u, world %d, result %d.",
            static_cast<unsigned int>(user->GetDBID()), user->m_data->m_nWorldId, success
        );
        userSocket->ForceClose(SCR_ILLEGAL_BEHAVIOR);
        return false;
    }

    userSocket->Send("chd", 0xfe, 0xb9, 1);
    userSocket->m_nCloseReason = SCR_RETURN_TO_ORIGIN;
    {
        L2_GUARD(userSocket->mLock);
        userSocket->UnBindUser();
    }
    if (socket != nullptr) {
        socket->OnUserSocketClosed();
    }
    return false;
}

bool Raid_RCS_WORLD_RAID_START(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a200c8.
    L2_GUARD_FUNCTION();
    int npcServerId = 0;
    ::Disassemble(packet, "d", &npcServerId);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR]WORLDRAID[%s][%d]RCS_WORLD_RAID_START recv: %d",
    //     L"RaidServerPacketHandler.cpp",
    //     0x325,
    //     npcServerId)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR]WORLDRAID[%s][%d]RCS_WORLD_RAID_START recv: %d",
    //     L"RaidServerPacketHandler.cpp",
    //     0x325,
    //     npcServerId)
    L2_LOG_INFO(Log, "World raid start requested by world %d for NPC server ID %d.", socket->GetWorldId(), npcServerId);

    RaidServerManager& manager = RaidServerManager::inst();
    const bool started =
        manager.m_pWorldRaidHost->StartWorldRaid(socket->GetWorldId(), static_cast<std::uint32_t>(npcServerId));
    const int firstStartedWorld = manager.m_pWorldRaidHost->m_idFirstStartedWorld;
    socket->Send("cddd", 0x0c, npcServerId, started, firstStartedWorld);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR]WORLDRAID[%s][%d]RSC_WORLD_RAID_START sent: %d, %d, %d",
    //     L"RaidServerPacketHandler.cpp",
    //     0x32c,
    //     npcServerId,
    //     started,
    //     firstStartedWorld)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR]WORLDRAID[%s][%d]RSC_WORLD_RAID_START sent: %d, %d, %d",
    //     L"RaidServerPacketHandler.cpp",
    //     0x32c,
    //     npcServerId,
    //     started,
    //     firstStartedWorld)
    L2_LOG_INFO(
        Log, "World raid start processed for NPC server ID %d: started %s, first-started world %d; response send attempted.",
        npcServerId, started ? "yes" : "no", firstStartedWorld
    );
    return false;
}

bool Raid_RCS_WORLD_RAID_STOP(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a20248.
    L2_GUARD_FUNCTION();
    int npcServerId = 0;
    ::Disassemble(packet, "d", &npcServerId);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR]WORLDRAID[%s][%d]RCS_WORLD_RAID_STOP recv: %d",
    //     L"RaidServerPacketHandler.cpp",
    //     0x33b,
    //     npcServerId)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR]WORLDRAID[%s][%d]RCS_WORLD_RAID_STOP recv: %d",
    //     L"RaidServerPacketHandler.cpp",
    //     0x33b,
    //     npcServerId)
    L2_LOG_INFO(Log, "World raid stop requested by world %d for NPC server ID %d.", socket->GetWorldId(), npcServerId);
    RaidServerManager::inst().m_pWorldRaidHost->StopWorldRaid(socket->GetWorldId());
    socket->Send("cd", 0x0d, npcServerId);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR]WORLDRAID[%s][%d]RSC_WORLD_RAID_STOP sent: %d",
    //     L"RaidServerPacketHandler.cpp",
    //     0x341,
    //     npcServerId)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR]WORLDRAID[%s][%d]RSC_WORLD_RAID_STOP sent: %d",
    //     L"RaidServerPacketHandler.cpp",
    //     0x341,
    //     npcServerId)
    L2_LOG_INFO(Log, "World raid stop response send attempted for NPC server ID %d.", npcServerId);
    return false;
}

bool Raid_RCS_WORLD_RAID_BROADCAST_EVENT(RaidClientSocket*, const unsigned char* const packet) {
    // Current L2Server 0x00a20398.
    L2_GUARD_FUNCTION();
    int firstValue = 0;
    int secondValue = 0;
    int thirdValue = 0;
    ::Disassemble(packet, "ddd", &firstValue, &secondValue, &thirdValue);
    RaidServerManager::inst().BroadcastWorldRaidEvent(firstValue, secondValue, thirdValue);
    return false;
}

bool RaidRequestSayRaidPacket(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a20604.
    L2_GUARD_FUNCTION();
    int pledgeDatabaseId = 0;
    int characterServerId = 0;
    int characterDatabaseId = 0;
    std::array<char16_t, 24> senderName{};
    std::array<char16_t, 256> message{};
    SayPacketType type = static_cast<SayPacketType>(0);
    ::Disassemble(
        packet, "dddSSd", &pledgeDatabaseId, &characterServerId, &characterDatabaseId, 0x30, senderName.data(), 0x200,
        message.data(), &type
    );

    CSPointer<CPledge> pledge = CPledge::FindPledge(socket->GetWorldId(), pledgeDatabaseId);
    if (!pledge) {
        socket->Send("cd", 9, pledgeDatabaseId);
        return false;
    }

    pledge->SayByMobile(characterServerId, characterDatabaseId, senderName.data(), message.data(), type);
    return false;
}

bool RaidRequestRelayToPledgeMemberPacket(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a23024.
    L2_GUARD_FUNCTION();
    int pledgeDatabaseId = 0;
    int packetLength = 0;
    const unsigned char* const relayPacket = ::Disassemble(packet, "dd", &pledgeDatabaseId, &packetLength);
    CSPointer<CPledge> pledge = CPledge::FindPledge(socket->GetWorldId(), pledgeDatabaseId);
    if (!pledge) {
        socket->Send("cd", 9, pledgeDatabaseId);
        return false;
    }
    if (packetLength <= 0) {
        return false;
    }

    std::set<std::uint32_t> memberIds;
    pledge->GetAllMemberIdSet(memberIds);
    for (const std::uint32_t memberId : memberIds) {
        CObject* const object = CObject::GetObjectW(memberId);
        User* member = nullptr;
        if (object != nullptr && object->IsPC()) {
            member = object->CastUser();
        }
        if (member != nullptr) {
            member->GetSocket()->Send("b", packetLength, relayPacket);
        }
    }
    return false;
}

bool RaidRequestPMSSetEmblem(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a207ec.
    L2_GUARD_FUNCTION();
    int pledgeDatabaseId = 0;
    std::uint32_t emblemId = 0;
    ::Disassemble(packet, "dd", &pledgeDatabaseId, &emblemId);
    CSPointer<CPledge> pledge = CPledge::FindPledge(socket->GetWorldId(), pledgeDatabaseId);
    if (!pledge) {
        socket->Send("cd", 9, pledgeDatabaseId);
        return false;
    }
    pledge->SetNewEmblemId(emblemId);
    SetEmblem setEmblem(emblemId);
    pledge->ServeForAllMember(setEmblem, false);
    SetParamChanged changed(PCB_PLEDGE);
    pledge->ServeForAllMember(changed, false);
    return false;
}

bool RaidRequestPMSSetParamChanged(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a20980.
    L2_GUARD_FUNCTION();
    int pledgeDatabaseId = 0;
    int flag = 0;
    ::Disassemble(packet, "dd", &pledgeDatabaseId, &flag);
    CSPointer<CPledge> pledge = CPledge::FindPledge(socket->GetWorldId(), pledgeDatabaseId);
    if (!pledge) {
        socket->Send("cd", 9, pledgeDatabaseId);
        return false;
    }
    SetParamChanged changed(static_cast<std::int64_t>(flag));
    pledge->ServeForAllMember(changed, false);
    return false;
}

bool RaidRequestPMSOpenAllWindowPacket(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a20ad8.
    L2_GUARD_FUNCTION();
    int pledgeDatabaseId = 0;
    ::Disassemble(packet, "d", &pledgeDatabaseId);
    CSPointer<CPledge> pledge = CPledge::FindPledge(socket->GetWorldId(), pledgeDatabaseId);
    if (!pledge) {
        socket->Send("cd", 9, pledgeDatabaseId);
        return false;
    }
    OpenAllWindowPacket openAllWindows(pledge.get());
    pledge->ServeForAllMember(openAllWindows, false);
    return false;
}

bool RaidRequestPMSDismissPledge(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a21fb0.
    L2_GUARD_FUNCTION();
    int pledgeDatabaseId = 0;
    ::Disassemble(packet, "d", &pledgeDatabaseId);
    CSPointer<CPledge> pledge = CPledge::FindPledge(socket->GetWorldId(), pledgeDatabaseId);
    if (!pledge) {
        socket->Send("cd", 9, pledgeDatabaseId);
        return false;
    }

    pledge->RemovePledgeSkillsFromMembers();
    User* const master = pledge->GetMaster();
    if (master != nullptr) {
        master->DeleteAcquireSkill(0x00f6);
        master->DeleteAcquireSkill(0x4a5a);
        master->DeleteAcquireSkill(0x4a5b);
        master->DeleteAcquireSkill(0x00f7);
        master->DeleteAcquireSkill(0x3bb3);
        master->DeleteAcquireSkill(0x3bb4);
        master->DeleteAcquireSkill(0x3bb5);
        master->ValidateChanged(VB_MAX);
    }

    DismissPledge dismiss(pledge->GetPledgeName());
    pledge->ServeForAllMember(dismiss, false);
    pledge->ResetAllNickName();
    CloseAllWindowPacket closeAllWindows;
    pledge->ServeForAllMember(closeAllWindows, false);

    CSPointer<CPledge> retained(pledge.get(), ObjectRefType::FunctionChain);
    CPledge::UnregisterPledge(retained);
    CPledge::s_pledgeSmartIdManager.DeleteObject(pledge->Id());
    return false;
}

bool RaidRequestPMSUnequipCastleItem(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a20da0.
    L2_GUARD_FUNCTION();
    int pledgeDatabaseId = 0;
    ::Disassemble(packet, "d", &pledgeDatabaseId);
    CSPointer<CPledge> pledge = CPledge::FindPledge(socket->GetWorldId(), pledgeDatabaseId);
    if (!pledge) {
        socket->Send("cd", 9, pledgeDatabaseId);
        return false;
    }
    UnequipCastleItem service;
    pledge->ServeForAllMember(service, false);
    return false;
}

bool RaidRequestPMSSetRelationChanged(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a20eec.
    L2_GUARD_FUNCTION();
    int pledgeDatabaseId = 0;
    ::Disassemble(packet, "d", &pledgeDatabaseId);
    CSPointer<CPledge> pledge = CPledge::FindPledge(socket->GetWorldId(), pledgeDatabaseId);
    if (!pledge) {
        socket->Send("cd", 9, pledgeDatabaseId);
        return false;
    }
    SetRelationChanged service;
    pledge->ServeForAllMember(service, false);
    return false;
}

bool RaidRequestPMSCancelRenamed(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a21038.
    L2_GUARD_FUNCTION();
    int pledgeDatabaseId = 0;
    ::Disassemble(packet, "d", &pledgeDatabaseId);
    CSPointer<CPledge> pledge = CPledge::FindPledge(socket->GetWorldId(), pledgeDatabaseId);
    if (!pledge) {
        socket->Send("cd", 9, pledgeDatabaseId);
        return false;
    }
    CancelRenamed service;
    pledge->ServeForAllMember(service, false);
    return false;
}

bool RaidRequestPMSSetCastleSiege(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a212d0 keeps the uninitialized entries and
    // performs no count-vs-nine validation.
    L2_GUARD_FUNCTION();
    int pledgeDatabaseId = 0;
    int count = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &pledgeDatabaseId, &count);
    std::int32_t castleIds[9];
    std::int32_t castleStatuses[9];
    for (int index = 0; index < count; ++index) {
        cursor = ::Disassemble(cursor, "dd", &castleIds[index], &castleStatuses[index]);
    }
    CSPointer<CPledge> pledge = CPledge::FindPledge(socket->GetWorldId(), pledgeDatabaseId);
    if (!pledge) {
        socket->Send("cd", 9, pledgeDatabaseId);
        return false;
    }
    SetCastleSiege service(castleIds, castleStatuses);
    pledge->ServeForAllMember(service, false);
    return false;
}

bool RaidRequestPMSSetFortressSiege(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a21470.
    L2_GUARD_FUNCTION();
    int pledgeDatabaseId = 0;
    int defenceFortressId = 0;
    int offenceFortressId = 0;
    ::Disassemble(packet, "ddd", &pledgeDatabaseId, &defenceFortressId, &offenceFortressId);
    CSPointer<CPledge> pledge = CPledge::FindPledge(socket->GetWorldId(), pledgeDatabaseId);
    if (!pledge) {
        socket->Send("cd", 9, pledgeDatabaseId);
        return false;
    }
    SetFortressSiege service(defenceFortressId, offenceFortressId);
    pledge->ServeForAllMember(service, false);
    return false;
}

bool RaidRequestPMSSetDominionSiege(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a215d8.
    L2_GUARD_FUNCTION();
    int pledgeDatabaseId = 0;
    int dominionSiegeId = 0;
    int dominionSiegeStatus = 0;
    ::Disassemble(packet, "ddd", &pledgeDatabaseId, &dominionSiegeId, &dominionSiegeStatus);
    CSPointer<CPledge> pledge = CPledge::FindPledge(socket->GetWorldId(), pledgeDatabaseId);
    if (!pledge) {
        socket->Send("cd", 9, pledgeDatabaseId);
        return false;
    }
    SetDominionSiege service(dominionSiegeId, dominionSiegeStatus);
    pledge->ServeForAllMember(service, false);
    return false;
}

bool Raid_RCS_REQUEST_SYNC_PLEDGE_LEVEL(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a21b5c.
    L2_GUARD_FUNCTION();
    int pledgeDatabaseId = 0;
    std::uint32_t pledgeLevel = 0;
    ::Disassemble(packet, "dd", &pledgeDatabaseId, &pledgeLevel);
    CSPointer<CPledge> pledge = CPledge::FindPledge(socket->GetWorldId(), pledgeDatabaseId);
    if (pledge) {
        pledge->SetPledgeLevel(pledgeLevel, false);
        pledge->UpdateInfoWindowPacket();
        PledgeStuff::AddPrivilegeByLevelAndMastery(pledge.get(), true);
    }
    return false;
}

bool Raid_RCS_REQUEST_SYNC_PLEDGE_MASTERY(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a21cbc.
    L2_GUARD_FUNCTION();
    int pledgeDatabaseId = 0;
    std::uint32_t masteryId = 0;
    int point = 0;
    ::Disassemble(packet, "ddd", &pledgeDatabaseId, &masteryId, &point);
    CSPointer<CPledge> pledge = CPledge::FindPledge(socket->GetWorldId(), pledgeDatabaseId);
    if (!pledge) {
        return false;
    }
    if (masteryId == 0) {
        pledge->GetMastery()->OnResetPoint();
        PledgeStuff::RemovePrivilegeByMastery(pledge.get());
    } else {
        pledge->GetMastery()->OnSetPoint(static_cast<PledgeMasteryID::Type>(masteryId), point);
        PledgeStuff::AddPrivilegeByLevelAndMastery(pledge.get(), true);
        pledge->UpdateInfoWindowPacket();
    }
    PledgeMasteryStuff::SendPledgeMasteryInfoToMembers(pledge.get());
    return false;
}

bool Raid_RCS_REQUEST_SYNC_PLEDGE_SKILL(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a21e50.
    L2_GUARD_FUNCTION();
    int pledgeDatabaseId = 0;
    int skillId = 0;
    int skillLevel = 0;
    ::Disassemble(packet, "ddd", &pledgeDatabaseId, &skillId, &skillLevel);
    CSPointer<CPledge> pledge = CPledge::FindPledge(socket->GetWorldId(), pledgeDatabaseId);
    if (!pledge) {
        return false;
    }
    if (skillLevel == 0) {
        static_cast<void>(pledge->RemovePledgeSkill(skillId, false));
    } else if (!pledge->AddPledgeSkillWithSaveAndNotify(skillId, skillLevel, 0, false, false, false)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"RaidServerPacketHandler.cpp",
        //     0x3e0)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"RaidServerPacketHandler.cpp",
        //     0x3e0)
        L2_LOG_ERROR(
            Log, "Raid clan skill update failed: clan database ID %d, skill %d, level %d.", pledgeDatabaseId, skillId,
            skillLevel
        );
    }
    return false;
}

bool RaidRequestPMSSetCrest(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a22204.
    L2_GUARD_FUNCTION();
    int pledgeDatabaseId = 0;
    std::uint32_t crestId = 0;
    ::Disassemble(packet, "dd", &pledgeDatabaseId, &crestId);
    CSPointer<CPledge> pledge = CPledge::FindPledge(socket->GetWorldId(), pledgeDatabaseId);
    if (!pledge) {
        socket->Send("cd", 9, pledgeDatabaseId);
        return false;
    }
    pledge->SetNewCrestId(crestId);
    SetCrest setCrest(crestId);
    pledge->ServeForAllMember(setCrest, false);
    UpdatePledgeInfoWindow update(pledge.get());
    pledge->ServeForAllMember(update, false);
    SetParamChanged changed(PCB_PLEDGE);
    pledge->ServeForAllMember(changed, false);
    return false;
}

bool RaidRequestPMSSetAllianceCrest(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a22400.
    L2_GUARD_FUNCTION();
    int pledgeDatabaseId = 0;
    std::uint32_t allianceCrestId = 0;
    ::Disassemble(packet, "dd", &pledgeDatabaseId, &allianceCrestId);
    CSPointer<CPledge> pledge = CPledge::FindPledge(socket->GetWorldId(), pledgeDatabaseId);
    if (!pledge) {
        socket->Send("cd", 9, pledgeDatabaseId);
        return false;
    }
    SetAllianceCrest setAllianceCrest(allianceCrestId);
    pledge->ServeForAllMember(setAllianceCrest, false);
    UpdatePledgeInfoWindow update(pledge.get());
    pledge->ServeForAllMember(update, false);
    SetParamChanged changed(PCB_PLEDGE);
    pledge->ServeForAllMember(changed, false);
    return false;
}

bool RaidRequestPMSSetAlliance(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a225f0. The third dword is decoded but unused.
    L2_GUARD_FUNCTION();
    int pledgeDatabaseId = 0;
    int allianceDatabaseId = 0;
    int unusedValue = 0;
    ::Disassemble(packet, "ddd", &pledgeDatabaseId, &allianceDatabaseId, &unusedValue);
    static_cast<void>(unusedValue);

    CSPointer<CPledge> pledge = CPledge::FindPledge(socket->GetWorldId(), pledgeDatabaseId);
    if (!pledge) {
        socket->Send("cd", 9, pledgeDatabaseId);
        return false;
    }

    if (allianceDatabaseId == 0) {
        pledge->SetAlliance(nullptr, false);
        SetParamChanged changed(PCB_PLEDGE);
        pledge->ServeForAllMember(changed, false);
        return false;
    }

    CSPointer<CAlliance> alliance =
        g_AllianceDbIdMap.FindObject(std::CDualKey<int, int>(socket->GetWorldId(), allianceDatabaseId));
    if (!alliance) {
        db.RequestLoadAlliance(socket->GetWorldId(), allianceDatabaseId);
        return false;
    }

    pledge->SetAlliance(alliance.get(), false);
    SetParamChanged changed(PCB_PLEDGE);
    pledge->ServeForAllMember(changed, false);
    return false;
}

bool RaidRequestAddPledgeMember(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a22824.
    L2_GUARD_FUNCTION();
    int allianceDatabaseId = 0;
    int pledgeDatabaseId = 0;
    ::Disassemble(packet, "dd", &allianceDatabaseId, &pledgeDatabaseId);

    CSPointer<CPledge> pledge = CPledge::FindPledge(socket->GetWorldId(), pledgeDatabaseId);
    if (!pledge) {
        db.RequestLoadPledge(socket->GetWorldId(), static_cast<std::uint32_t>(pledgeDatabaseId), false);
        return false;
    }

    CSPointer<CAlliance> alliance =
        g_AllianceDbIdMap.FindObject(std::CDualKey<int, int>(socket->GetWorldId(), allianceDatabaseId));
    if (!alliance) {
        db.RequestLoadAlliance(socket->GetWorldId(), allianceDatabaseId);
        return false;
    }

    alliance->AddMemberPledge(pledge.get());
    pledge->SetAlliance(alliance.get(), false);
    return false;
}

bool RaidRequestDeletePledgeMember(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a22a08.
    L2_GUARD_FUNCTION();
    int allianceDatabaseId = 0;
    int pledgeDatabaseId = 0;
    ::Disassemble(packet, "dd", &allianceDatabaseId, &pledgeDatabaseId);

    CSPointer<CAlliance> alliance =
        g_AllianceDbIdMap.FindObject(std::CDualKey<int, int>(socket->GetWorldId(), allianceDatabaseId));
    if (!alliance) {
        db.RequestLoadAlliance(socket->GetWorldId(), allianceDatabaseId);
        return false;
    }

    alliance->DeleteMemberPledge(static_cast<std::uint32_t>(pledgeDatabaseId));
    CSPointer<CPledge> pledge = CPledge::FindPledge(socket->GetWorldId(), pledgeDatabaseId);
    if (pledge) {
        pledge->SetAlliance(nullptr, false);
    }
    return false;
}

bool RaidRequestDismissAlliance(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a23294.
    L2_GUARD_FUNCTION();
    int allianceDatabaseId = 0;
    ::Disassemble(packet, "d", &allianceDatabaseId);
    CSPointer<CAlliance> alliance =
        g_AllianceDbIdMap.FindObject(std::CDualKey<int, int>(socket->GetWorldId(), allianceDatabaseId));
    if (!alliance) {
        return false;
    }

    std::vector<CPledge*> pledges;
    alliance->GetAllMemberPledge(pledges);
    for (CPledge* const pledge : pledges) {
        pledge->SetAlliance(nullptr, false);
    }

    std::array<SharedUtilsWideChar, 32> lowerAllianceName;
    ::ToLowerCase(alliance->m_data->m_sAllianceName, lowerAllianceName.data());
    const std::u16string lowerHostAllianceName = SanitizeUtf16(reinterpret_cast<const char16_t*>(lowerAllianceName.data()));
    g_AllianceNameMap.DeleteObject(
        alliance->m_nWorldId, lowerHostAllianceName, const_cast<char*>("RaidServerPacketHandler.cpp"), 0x295
    );
    g_AllianceDbIdMap.DeleteObject(
        std::CDualKey<int, int>(alliance->m_nWorldId, static_cast<int>(alliance->m_data->m_nDbId)),
        const_cast<char*>("RaidServerPacketHandler.cpp"), 0x296
    );
    return false;
}

bool Raid_RCS_REQUEST_SYNC_PLEDGE_TYPE(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a22b88.
    L2_GUARD_FUNCTION();
    int pledgeDatabaseId = 0;
    std::uint32_t memberDatabaseId = 0;
    int pledgeTypeValue = 0;
    ::Disassemble(packet, "ddd", &pledgeDatabaseId, &memberDatabaseId, &pledgeTypeValue);

    CSPointer<CPledge> pledge = CPledge::FindPledge(socket->GetWorldId(), pledgeDatabaseId);
    if (!pledge) {
        return false;
    }

    const PledgeType pledgeType = static_cast<PledgeType>(pledgeTypeValue);
    pledge->ChangeMemberPledgeType(memberDatabaseId, pledgeType);
    pledge->SetPledgePowerGrade(pledge->GetMemberName(memberDatabaseId), CPledge::GetDefaultPowerGrade(pledgeType));
    pledge->UpdatePledgeMemberListPacket(memberDatabaseId);

    CSPointer<User> member =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(socket->GetWorldId(), static_cast<int>(memberDatabaseId)));
    if (!member) {
        return false;
    }

    member->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(member->m_data->m_nPledgeType, pledgeTypeValue, 0x34);
    member->m_nParamChanged |= std::int64_t{PCB_PLEDGE};
    if (member->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
            member->m_data->m_nGradeId, CPledge::GetDefaultPowerGrade(pledgeType), 0x46
        )) {
        member->ParamChanged(std::int64_t{PCB_USER});
    }
    pledge->RemovePledgeSkillsFrom(member.get());
    pledge->ApplyPledgeSkillsTo(member.get(), true, false);
    return false;
}

bool RaidRequestPMSUpdatePledgePower(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a20c28.
    L2_GUARD_FUNCTION();
    int pledgeDatabaseId = 0;
    int gradeId = 0;
    std::uint32_t pledgePowerBits = 0;
    ::Disassemble(packet, "ddd", &pledgeDatabaseId, &gradeId, &pledgePowerBits);

    CSPointer<CPledge> pledge = CPledge::FindPledge(socket->GetWorldId(), pledgeDatabaseId);
    if (!pledge) {
        socket->Send("cd", 9, pledgeDatabaseId);
        return false;
    }

    pledge->SetPowerGrade(gradeId, static_cast<unsigned long>(pledgePowerBits));
    UpdatePledgePower service(gradeId, pledgePowerBits);
    pledge->ServeForAllMember(service, false);
    return false;
}

bool RaidRequestPMSAffectedByWar(RaidClientSocket* const socket, const unsigned char* const packet) {
    // Current L2Server 0x00a21184.
    L2_GUARD_FUNCTION();
    int pledgeDatabaseId = 0;
    ::Disassemble(packet, "d", &pledgeDatabaseId);

    CSPointer<CPledge> pledge = CPledge::FindPledge(socket->GetWorldId(), pledgeDatabaseId);
    if (!pledge) {
        socket->Send("cd", 9, pledgeDatabaseId);
        return false;
    }

    AffectedByWar service;
    pledge->ServeForAllMember(service, false);
    return false;
}

L2_BIND_PACKET(kBindRaidRequestPMSUpdatePledgePower, g_RaidClientPacketTable, 0x0c, &RaidRequestPMSUpdatePledgePower);

L2_BIND_PACKET(kBindRaidRequestPMSAffectedByWar, g_RaidClientPacketTable, 0x10, &RaidRequestPMSAffectedByWar);

L2_BIND_PACKET(kBindRaidRequestPMSDismissPledge, g_RaidClientPacketTable, 0x0b, &RaidRequestPMSDismissPledge);

L2_BIND_PACKET(kBindRaidClientVersionPacket, g_RaidClientPacketTable, 0x00, &RaidClientVersionPacket);

L2_BIND_PACKET(kBindRaidReserveUserPacket, g_RaidClientPacketTable, 0x01, &RaidReserveUserPacket);

L2_BIND_PACKET(kBindRaidCancelReservedPacket, g_RaidClientPacketTable, 0x02, &RaidCancelReservedPacket);

L2_BIND_PACKET(kBindRaidRequestRelayPacket, g_RaidClientPacketTable, 0x03, &RaidRequestRelayPacket);

L2_BIND_PACKET(kBindRaidRequestSayRaidPacket, g_RaidClientPacketTable, 0x04, &RaidRequestSayRaidPacket);

L2_BIND_PACKET(
    kBindRaidRequestRelayToPledgeMemberPacket, g_RaidClientPacketTable, 0x05, &RaidRequestRelayToPledgeMemberPacket
);

L2_BIND_PACKET(kBindRaidRequestPMSSetCrest, g_RaidClientPacketTable, 0x06, &RaidRequestPMSSetCrest);

L2_BIND_PACKET(kBindRaidRequestPMSSetEmblem, g_RaidClientPacketTable, 0x07, &RaidRequestPMSSetEmblem);

L2_BIND_PACKET(kBindRaidRequestPMSSetAllianceCrest, g_RaidClientPacketTable, 0x08, &RaidRequestPMSSetAllianceCrest);

L2_BIND_PACKET(kBindRaidRequestPMSSetParamChanged, g_RaidClientPacketTable, 0x09, &RaidRequestPMSSetParamChanged);

L2_BIND_PACKET(kBindRaidRequestPMSOpenAllWindowPacket, g_RaidClientPacketTable, 0x0a, &RaidRequestPMSOpenAllWindowPacket);

L2_BIND_PACKET(kBindRaidRequestPMSUnequipCastleItem, g_RaidClientPacketTable, 0x0d, &RaidRequestPMSUnequipCastleItem);

L2_BIND_PACKET(kBindRaidRequestPMSSetRelationChanged, g_RaidClientPacketTable, 0x0e, &RaidRequestPMSSetRelationChanged);

L2_BIND_PACKET(kBindRaidRequestPMSCancelRenamed, g_RaidClientPacketTable, 0x0f, &RaidRequestPMSCancelRenamed);

L2_BIND_PACKET(kBindRaidRequestPMSSetCastleSiege, g_RaidClientPacketTable, 0x11, &RaidRequestPMSSetCastleSiege);

L2_BIND_PACKET(kBindRaidRequestPMSSetFortressSiege, g_RaidClientPacketTable, 0x12, &RaidRequestPMSSetFortressSiege);

L2_BIND_PACKET(kBindRaidRequestPMSSetDominionSiege, g_RaidClientPacketTable, 0x13, &RaidRequestPMSSetDominionSiege);

L2_BIND_PACKET(kBindRaidRequestPMSSetAlliance, g_RaidClientPacketTable, 0x14, &RaidRequestPMSSetAlliance);

L2_BIND_PACKET(kBindRaidRequestAddPledgeMember, g_RaidClientPacketTable, 0x15, &RaidRequestAddPledgeMember);

L2_BIND_PACKET(kBindRaidRequestDeletePledgeMember, g_RaidClientPacketTable, 0x16, &RaidRequestDeletePledgeMember);

L2_BIND_PACKET(kBindRaidRequestDismissAlliance, g_RaidClientPacketTable, 0x17, &RaidRequestDismissAlliance);

L2_BIND_PACKET(kBindRaidDisconnectUser, g_RaidClientPacketTable, 0x1c, &RaidDisconnectUser);

L2_BIND_PACKET(kBindRaidConfirmReturnToOrigin, g_RaidClientPacketTable, 0x1d, &RaidConfirmReturnToOrigin);

L2_BIND_PACKET(kBindRaidConfirmRestart, g_RaidClientPacketTable, 0x1e, &RaidConfirmRestart);

L2_BIND_PACKET(kBindRaidWorldRaidStart, g_RaidClientPacketTable, 0x20, &Raid_RCS_WORLD_RAID_START);

L2_BIND_PACKET(kBindRaidWorldRaidStop, g_RaidClientPacketTable, 0x21, &Raid_RCS_WORLD_RAID_STOP);

L2_BIND_PACKET(kBindRaidWorldRaidBroadcastEvent, g_RaidClientPacketTable, 0x22, &Raid_RCS_WORLD_RAID_BROADCAST_EVENT);

L2_BIND_PACKET(kBindRaidSyncPledgeLevel, g_RaidClientPacketTable, 0x23, &Raid_RCS_REQUEST_SYNC_PLEDGE_LEVEL);

L2_BIND_PACKET(kBindRaidSyncPledgeMastery, g_RaidClientPacketTable, 0x24, &Raid_RCS_REQUEST_SYNC_PLEDGE_MASTERY);

L2_BIND_PACKET(kBindRaidSyncPledgeSkill, g_RaidClientPacketTable, 0x26, &Raid_RCS_REQUEST_SYNC_PLEDGE_SKILL);

L2_BIND_PACKET(kBindRaidSyncPledgeType, g_RaidClientPacketTable, 0x25, &Raid_RCS_REQUEST_SYNC_PLEDGE_TYPE);
