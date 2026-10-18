#include "L2Packet.h"
#include "NoblessSystem.h"
#include "NpcServer.h"
#include "OlympiadNetwork.h"
#include "OlympiadPacketHandlers.h"
#include "OlympiadServiceProxy.h"
#include "Pledge.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/UserConst.h"
#include "Shared/src/net/Serialize.h"
#include "Shared/src/proto/OlympiadPacket.h"
#include "User.h"
#include "UserBroadcaster.h"
#include "UserSocket.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

bool DummyOSCPacket(WOlympiadSocketHost*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d]DummyOSCPacket called",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x13)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d]DummyOSCPacket called",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x13)
    L2_LOG_ERROR(Log, "Unhandled Olympiad host response reached the peer dispatch table.");
    return false;
}

namespace {

template <class Packet>
bool DecodeOscPacket(Packet& packet, const std::uint8_t* source) {
    return WOlympiadStuff::DecodePacket(packet, source);
}

CSPointer<User> FindOlympiadPeerUser(int userDatabaseId) {
    return WOlympiadStuff::FindUserByKey(std::CDualKey<int, int>(g_nWorldId, userDatabaseId));
}

void SendOCA_RETURN(WOlympiadSocketHost* socket, int userDatabaseId, int userServerId, bool success) {
    if (!socket) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x135,
        //     L"WO_ASSERT_NOT_NULL")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x135,
        //     L"WO_ASSERT_NOT_NULL")
        L2_LOG_WARN(
            Log, "Cannot send Olympiad return response for character DB ID %d, object ID %u: host socket is missing.",
            userDatabaseId, static_cast<unsigned int>(userServerId)
        );
        return;
    }
    olympiad::OCA_RETURN packet{};
    packet.nUserSID = userServerId;
    packet.bSuccess = success;
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OCA_RETURN sent: %d, %d, %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x13b,
    //     userDatabaseId,
    //     userServerId,
    //     success ? 1 : 0)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OCA_RETURN sent: %d, %d, %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x13b,
    //     userDatabaseId,
    //     userServerId,
    //     success ? 1 : 0)
    L2_LOG_INFO(
        Log, "Sending Olympiad return response for character DB ID %d, object ID %u: accepted %s.", userDatabaseId,
        static_cast<unsigned int>(userServerId), success ? "yes" : "no"
    );
    socket->SendPacket(OCA_RETURN, packet);
}

void SendOCA_RESTART(WOlympiadSocketHost* socket, int userDatabaseId, int userServerId, bool success) {
    if (!socket) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x142,
        //     L"WO_ASSERT_NOT_NULL")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x142,
        //     L"WO_ASSERT_NOT_NULL")
        L2_LOG_WARN(
            Log, "Cannot send Olympiad restart response for character DB ID %d, object ID %u: host socket is missing.",
            userDatabaseId, static_cast<unsigned int>(userServerId)
        );
        return;
    }
    olympiad::OCA_RESTART packet{};
    packet.nUserSID = userServerId;
    packet.bSuccess = success;
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OCA_RESTART sent: %d, %d, %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x148,
    //     userDatabaseId,
    //     userServerId,
    //     success ? 1 : 0)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OCA_RESTART sent: %d, %d, %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x148,
    //     userDatabaseId,
    //     userServerId,
    //     success ? 1 : 0)
    L2_LOG_INFO(
        Log, "Sending Olympiad restart response for character DB ID %d, object ID %u: accepted %s.", userDatabaseId,
        static_cast<unsigned int>(userServerId), success ? "yes" : "no"
    );
    socket->SendPacket(OCA_RESTART, packet);
}

} // namespace

bool Handle_OSA_VERSION(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSA_VERSION packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    if (!packet.bSuccess) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]failed to check version",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x27)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]failed to check version",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x27)
        L2_LOG_ERROR(Log, "Olympiad host rejected the version handshake.");
        return true;
    }

    SharedOlympiadInfo::GetInfo()->m_bHostConnected = true;
    WOlympiadServiceProxy::inst().RequestClassRankByServer();
    FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
        WOlympiadServiceProxy::inst().RequestNoblessInfo(CSPointer<User>(user, ObjectRefType::FunctionChain));
    });
    return false;
}

bool Handle_OSA_CONNECT(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSA_CONNECT packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSA_CONNECT recv: %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x4a,
    //     packet.nUserDBID)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSA_CONNECT recv: %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x4a,
    //     packet.nUserDBID)
    L2_LOG_INFO(
        Log, "Received Olympiad connection response for character DB ID %d: accepted %s, player role %s.", packet.nUserDBID,
        packet.bSuccess ? "yes" : "no", packet.bAsPlayer ? "yes" : "no"
    );
    WOlympiadServiceProxy::inst().OnConnectResult(packet.nUserDBID, packet.bSuccess, packet.bAsPlayer);
    return false;
}

bool Handle_OSA_START(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSA_START packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSA_START recv: %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x61,
    //     packet.nUserDBID)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSA_START recv: %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x61,
    //     packet.nUserDBID)
    L2_LOG_INFO(
        Log, "Received Olympiad matchmaking response for character DB ID %d: result %d, message ID %d, entry count %d.",
        packet.nUserDBID, static_cast<int>(packet.cResult), packet.nMessageNo, packet.nEntryCount
    );
    WOlympiadServiceProxy::inst().OnStartResult(packet);
    return false;
}

bool Handle_OSA_STOP(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSA_STOP packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSA_STOP recv: %d, %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x76,
    //     packet.nUserDBID,
    //     static_cast<int>(packet.cResult))
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSA_STOP recv: %d, %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x76,
    //     packet.nUserDBID,
    //     static_cast<int>(packet.cResult))
    L2_LOG_INFO(
        Log, "Received Olympiad matchmaking cancellation response for character DB ID %d: result %d.", packet.nUserDBID,
        static_cast<int>(packet.cResult)
    );
    WOlympiadServiceProxy::inst().OnStopResult(packet);
    return false;
}

bool Handle_OSQ_PREPARE(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSQ_PREPARE packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSQ_PREPARE recv: %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0xe8,
    //     packet.nUserDBID)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSQ_PREPARE recv: %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0xe8,
    //     packet.nUserDBID)
    L2_LOG_INFO(Log, "Received Olympiad player preparation request for character DB ID %d.", packet.nUserDBID);
    WOlympiadServiceProxy::inst().OnPrepareAsPlayer(packet.nUserDBID);
    return false;
}

bool Handle_OSN_CONNECT(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSN_CONNECT packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSN_CONNECT recv: %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0xff,
    //     packet.nUserDBID)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSN_CONNECT recv: %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0xff,
    //     packet.nUserDBID)
    L2_LOG_INFO(Log, "Received Olympiad connection notification for character DB ID %d.", packet.nUserDBID);
    CSPointer<User> user = FindOlympiadPeerUser(packet.nUserDBID);
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x105)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x105)
        L2_LOG_WARN(Log, "Cannot connect character DB ID %d to Olympiad: local user was not found.", packet.nUserDBID);
        return false;
    }
    if (!WOlympiadServiceProxy::inst().ConnectGame(user)) {
        WOlympiadServiceProxy::inst().UnregisterMember(user);
    }
    return false;
}

bool Handle_OSN_DISCONNECTED(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSN_DISCONNECTED packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSN_DISCONNECTED recv: %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x11f,
    //     packet.nUserDBID)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSN_DISCONNECTED recv: %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x11f,
    //     packet.nUserDBID)
    L2_LOG_INFO(Log, "Received Olympiad disconnect notification for character DB ID %d.", packet.nUserDBID);
    CSPointer<User> user = FindOlympiadPeerUser(packet.nUserDBID);
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]No user for disconnected: %d",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x125,
        //     packet.nUserDBID)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]No user for disconnected: %d",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x125,
        //     packet.nUserDBID)
        L2_LOG_WARN(
            Log, "Cannot unregister disconnected Olympiad character DB ID %d: local user was not found.", packet.nUserDBID
        );
        return false;
    }
    WOlympiadServiceProxy::inst().UnregisterMember(user);
    return false;
}

bool Handle_OSA_CLASS_RANK_BY_SERVER(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSA_CLASS_RANK_BY_SERVER packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    const ClassType classType = static_cast<ClassType>(packet.nHeroClass);
    WOlympiadServiceProxyUtil::ResetRankInfo(classType);
    for (const olympiad::PkClassRankByServerInfo& rank : packet.vRanks) {
        const std::wstring name = Utf16ToWide(runtime::StringView(rank.sName));
        WOlympiadServiceProxyUtil::AddRankInfo(classType, rank.nRanking, name.c_str(), rank.nPoint);
    }
    return false;
}

bool Handle_OSA_HERO_MATCH_RESULT(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSA_HERO_MATCH_RESULT packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    WOlympiadServiceProxy::inst().OnHeroMatchResult(packet);
    return false;
}

bool Handle_OSA_OLYMPIAD_RECORD(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSA_OLYMPIAD_RECORD packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    WOlympiadServiceProxy::inst().OnOlympiadRecord(packet);
    return false;
}

bool Handle_OSN_NOBLESS_INFO(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSN_NOBLESS_INFO packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    if (packet.nUserDBID < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x36e)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x36e)
        L2_LOG_WARN(Log, "Ignoring Olympiad nobless information: character DB ID %d must be positive.", packet.nUserDBID);
        return false;
    }
    WOlympiadServiceProxy::inst().OnNotifyNoblessInfo(packet);
    return false;
}

bool Handle_OSN_NOTIFY_SEASON(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSN_NOTIFY_SEASON packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    SharedOlympiadInfo* const info = SharedOlympiadInfo::GetInfo();
    info->m_nSeason = packet.nSeason;
    info->m_nStep = packet.nStep;
    info->m_idLegendary = packet.nLegendaryWorldID;
    WOlympiadServiceProxy::inst().OnSetOlympiadTerm(packet.nStartTime, packet.nNominateSecond);
    return false;
}

bool Handle_OSN_ENTRY_COUNT(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSN_ENTRY_COUNT packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    SharedOlympiadInfo::GetInfo()->m_EntryCount[packet.nGameRule] = packet.nCount;
    return false;
}

bool Handle_OSN_PLAYER_COUNT(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSN_PLAYER_COUNT packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    SharedOlympiadInfo::GetInfo()->m_nSeasonPlayerCount = packet.nCount;
    return false;
}

bool Handle_OSN_OLYMPIAD_MATCHING_INFO(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSN_OLYMPIAD_MATCHING_INFO packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    WOlympiadServiceProxy::inst().OnOlympiadMatchingInfo(packet);
    return false;
}

bool Handle_OSN_OLYMPIAD_POINT(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSN_OLYMPIAD_POINT packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    if (packet.nPoint == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x2e8)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x2e8)
        L2_LOG_WARN(
            Log, "Ignoring zero Olympiad point notification for character DB ID %d (broadcast %s).", packet.nUserDBID,
            packet.bBroadcast ? "yes" : "no"
        );
    } else if (packet.bBroadcast) {
        NoblessSystemUtil::BroadcastAddOlympiadPoint(packet.nPoint);
    } else {
        NoblessSystemUtil::SendAddOlympiadPointToUser(packet.nUserDBID, packet.nPoint);
    }
    return false;
}

bool Handle_OSN_START_SEASON(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSN_START_SEASON packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    CUserBroadcaster::inst().BroadcastToAllUserEx("chccd", 0x62, 0x667, 1, 1, packet.nSeason);
    WOlympiadServiceProxy::inst().RequestClassRankByServer();
    return false;
}

bool Handle_OSN_STOP_SEASON(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSN_STOP_SEASON packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    CUserBroadcaster::inst().BroadcastToAllUserEx("chccd", 0x62, 0x668, 1, 1, packet.nSeason);
    return false;
}

bool Handle_OSA_REGISTER(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSA_REGISTER packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    CSPointer<User> user = FindOlympiadPeerUser(packet.nUserDBID);
    if (user) {
        // Log.Add(
        //     LOG_ERROR,
        //     packet.bSuccess
        //         ? L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]Register user: %d"
        //         : L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     packet.bSuccess ? 0x316 : 0x312,
        //     packet.nUserDBID)
        // L2_LOG_ERROR(Log,
        //     packet.bSuccess
        //         ? L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]Register user: %d"
        //         : L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     packet.bSuccess ? 0x316 : 0x312,
        //     packet.nUserDBID)
        L2_LOG_INFO(
            Log, "Received Olympiad registration response for character DB ID %d: accepted %s.", packet.nUserDBID,
            packet.bSuccess ? "yes" : "no"
        );
    }
    return false;
}

bool Handle_OSA_OBSERVE(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSA_OBSERVE packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSA_OBSERVE recv: %d, %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x47d,
    //     packet.nUserDBID,
    //     static_cast<int>(packet.cResult))
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSA_OBSERVE recv: %d, %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x47d,
    //     packet.nUserDBID,
    //     static_cast<int>(packet.cResult))
    L2_LOG_INFO(
        Log, "Received Olympiad observation response for character DB ID %d: result %d.", packet.nUserDBID,
        static_cast<int>(packet.cResult)
    );
    CSPointer<User> user = FindOlympiadPeerUser(packet.nUserDBID);
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x483)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x483)
        L2_LOG_WARN(
            Log, "Cannot process Olympiad observation response for character DB ID %d: local user was not found.",
            packet.nUserDBID
        );
    } else if (packet.cResult != 0) {
        WOlympiadServiceProxy::inst().UnregisterMember(user);
    }
    return false;
}

bool Handle_OSA_DELETE_TRADE_POINT(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSA_DELETE_TRADE_POINT packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]"
    //     L"Delete olympiad trade point: %d, %d, %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x419,
    //     packet.nUserDBID,
    //     packet.nTradePoint,
    //     packet.bSuccess ? 1 : 0)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]"
    //     L"Delete olympiad trade point: %d, %d, %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x419,
    //     packet.nUserDBID,
    //     packet.nTradePoint,
    //     packet.bSuccess ? 1 : 0)
    L2_LOG_INFO(
        Log, "Received Olympiad trade-point deletion response for character DB ID %d: points %d, accepted %s.",
        packet.nUserDBID, packet.nTradePoint, packet.bSuccess ? "yes" : "no"
    );
    CSPointer<User> user = FindOlympiadPeerUser(packet.nUserDBID);
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]User is logout: %d, %d, %d",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x41f,
        //     packet.nUserDBID,
        //     packet.nTradePoint,
        //     packet.bSuccess ? 1 : 0)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]User is logout: %d, %d, %d",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x41f,
        //     packet.nUserDBID,
        //     packet.nTradePoint,
        //     packet.bSuccess ? 1 : 0)
        L2_LOG_WARN(
            Log,
            "Cannot forward Olympiad trade-point deletion result to NPC: character DB ID %d was not found (points %d, "
            "accepted %s).",
            packet.nUserDBID, packet.nTradePoint, packet.bSuccess ? "yes" : "no"
        );
        return false;
    }
    NpcServerUtil::SendDeleteOlympiadTradePointReturned(
        packet.nNPCSID, static_cast<int>(user->GetServerId()), packet.nTradePoint, packet.bSuccess ? 1 : 0
    );
    return false;
}

bool Handle_OSA_BCMD(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSA_BCMD packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    CSPointer<User> user = FindOlympiadPeerUser(packet.nUserDBID);
    if (!user || !user->GetSocket()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     user ? 0x29a : 0x292)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     user ? 0x29a : 0x292)
        L2_LOG_WARN(
            Log, "Cannot deliver Olympiad builder-command response to character DB ID %d: %s is missing.", packet.nUserDBID,
            user ? "user socket" : "local user"
        );
        return false;
    }
    const std::u16string message = Utf16Text(runtime::StringView(packet.sMessage)).String();
    user->GetSocket()->SendSystemMessage(u"SYS", message.c_str());
    return false;
}

bool Handle_OSA_DEBUG_STRING(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSA_DEBUG_STRING packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    CSPointer<User> user = FindOlympiadPeerUser(packet.nUserDBID);
    if (user && user->GetSocket()) {
        const std::u16string message = Utf16Text(runtime::StringView(packet.sString)).String();
        user->SendSystemMessage(u"SYS", message.c_str());
    }
    return false;
}

bool Handle_OSN_SYSTEM_MESSAGE(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSN_SYSTEM_MESSAGE packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    CSPointer<User> user = FindOlympiadPeerUser(packet.nUserDBID);
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x253)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x253)
        L2_LOG_WARN(
            Log, "Cannot deliver Olympiad system message to character DB ID %d: local user was not found.", packet.nUserDBID
        );
        return false;
    }
    const std::u16string message = Utf16Text(runtime::StringView(packet.sMessage)).String();
    user->SendSystemMessage(u"SYS", message.c_str());
    return false;
}

bool Handle_OSN_SYSTEM_MESSAGE_NO(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSN_SYSTEM_MESSAGE_NO packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    if (packet.bBroadcast) {
        CUserBroadcaster::inst().BroadcastToAllUserEx(
            "chc", 0x62, static_cast<int>(static_cast<std::int16_t>(packet.nMessageNo)), 0
        );
    } else {
        CSPointer<User> user = FindOlympiadPeerUser(packet.nUserDBID);
        if (user) {
            user->SendSystemMessage(packet.nMessageNo);
        }
    }
    return false;
}

bool Handle_OSN_STOP(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSN_STOP packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSN_STOP recv: %d, %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x89,
    //     packet.nUserDBID,
    //     packet.bBroadcast ? 1 : 0)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSN_STOP recv: %d, %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x89,
    //     packet.nUserDBID,
    //     packet.bBroadcast ? 1 : 0)
    L2_LOG_INFO(
        Log, "Received Olympiad stop notification for character DB ID %d (all members %s).", packet.nUserDBID,
        packet.bBroadcast ? "yes" : "no"
    );
    if (packet.bBroadcast) {
        WOlympiadServiceProxy::inst().UnregisterAllMember();
    } else {
        CSPointer<User> user = FindOlympiadPeerUser(packet.nUserDBID);
        if (user) {
            WOlympiadServiceProxy::inst().UnregisterMember(user);
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"OlympiadPacketHandlerInPeer.cpp",
            //     0x95)
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"OlympiadPacketHandlerInPeer.cpp",
            //     0x95)
            L2_LOG_WARN(
                Log,
                "Cannot remove Olympiad membership for character DB ID %d after stop notification: local user was not "
                "found.",
                packet.nUserDBID
            );
        }
    }
    return false;
}

bool Handle_OSN_CANCEL_MATCH(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSN_CANCEL_MATCH packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSN_CANCEL_MATCH recv: %d, %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0xac,
    //     packet.nUserDBID,
    //     packet.nGameValidCondition)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSN_CANCEL_MATCH recv: %d, %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0xac,
    //     packet.nUserDBID,
    //     packet.nGameValidCondition)
    L2_LOG_INFO(
        Log, "Received Olympiad match cancellation for character DB ID %d: condition %d, item ID %d.", packet.nUserDBID,
        packet.nGameValidCondition, packet.nItemID
    );

    CSPointer<User> user = FindOlympiadPeerUser(packet.nUserDBID);
    CUserSocket* const socket = user ? user->GetSocket() : nullptr;
    if (!socket) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0xb2)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0xb2)
        L2_LOG_WARN(
            Log, "Cannot deliver Olympiad cancellation to character DB ID %d: %s is missing.", packet.nUserDBID,
            user ? "user socket" : "local user"
        );
        return false;
    }

    switch (packet.nGameValidCondition) {
        case 1:
        case 2:
        case 5:
        case 6:
        case 7:
        case 8:
            socket->SendSystemMessage(SystemMessageId::MATCH_CANCELLED_OPPONENT_NOT_ELIGIBLE);
            break;
        case 3:
            socket->SendSystemMessage(SystemMessageId::MATCH_CANCELLED_OPPONENT_LOGGED_OUT);
            break;
        case 4:
            socket->Send(
                "chccd", 0x62, SystemMessageId::OLYMPIAD_CANCELLED_OPPONENT_ACQUIRED_NAMED_OBJECT, 1, 3, packet.nItemID
            );
            break;
        default:
            break;
    }
    return false;
}

bool Handle_OSN_CONFIRMED(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSN_CONFIRMED packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSN_CONFIRMED recv: %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x1af,
    //     packet.nUserDBID)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSN_CONFIRMED recv: %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x1af,
    //     packet.nUserDBID)
    L2_LOG_INFO(Log, "Received Olympiad participant confirmation for character DB ID %d.", packet.nUserDBID);
    CSPointer<User> user = FindOlympiadPeerUser(packet.nUserDBID);
    CUserSocket* const socket = user ? user->GetSocket() : nullptr;
    if (!socket) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x1b5)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x1b5)
        L2_LOG_WARN(
            Log,
            "Cannot apply Olympiad participant confirmation for character DB ID %d: %s is missing; user count and raid "
            "status were not changed.",
            packet.nUserDBID, user ? "user socket" : "local user"
        );
        return false;
    }
    socket->DecUserCount(false);
    socket->AddRaidStatus(URS_ON_RAID);
    return false;
}

bool Handle_OSQ_RETURN(WOlympiadSocketHost* socket, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSQ_RETURN packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSQ_RETURN recv: %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x15a,
    //     packet.nUserDBID)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSQ_RETURN recv: %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x15a,
    //     packet.nUserDBID)
    L2_LOG_INFO(Log, "Received Olympiad return request for character DB ID %d.", packet.nUserDBID);
    CSPointer<User> user = FindOlympiadPeerUser(packet.nUserDBID);
    if (!user) {
        SendOCA_RETURN(socket, packet.nUserDBID, packet.nUserSID, false);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Invalid User : %d, %d",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x161,
        //     socket->m_idWorld,
        //     packet.nUserDBID)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Invalid User : %d, %d",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x161,
        //     socket->m_idWorld,
        //     packet.nUserDBID)
        L2_LOG_WARN(
            Log, "Rejected Olympiad return request from world %d for character DB ID %d: local user was not found.",
            socket->m_idWorld, packet.nUserDBID
        );
        return false;
    }
    if (user->m_data->GetWorldId() != g_nWorldId || user->m_RaidInfo.m_nRaidWorldId == 0) {
        SendOCA_RETURN(socket, packet.nUserDBID, packet.nUserSID, false);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]RETURN failed: %d, %d",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x169,
        //     socket->m_idWorld,
        //     packet.nUserDBID)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]RETURN failed: %d, %d",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x169,
        //     socket->m_idWorld,
        //     packet.nUserDBID)
        L2_LOG_WARN(
            Log,
            "Rejected Olympiad return for character DB ID %d: user world %d, local world %d, raid world %d; expected local "
            "ownership and a nonzero raid world.",
            packet.nUserDBID, user->m_data->GetWorldId(), g_nWorldId, user->m_RaidInfo.m_nRaidWorldId
        );
        return false;
    }
    CUserSocket* const userSocket = user->GetSocket();
    if (!userSocket) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x171)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x171)
        L2_LOG_WARN(
            Log,
            "Cannot process Olympiad return for character DB ID %d: user socket is missing; no return response is sent.",
            packet.nUserDBID
        );
        return false;
    }
    userSocket->AddRaidStatus(URS_RETURN_FROM_RAID);
    userSocket->SetRaidCharServerId(packet.nUserSID);
    SendOCA_RETURN(socket, packet.nUserDBID, packet.nUserSID, true);
    return false;
}

bool Handle_OSQ_RESTART(WOlympiadSocketHost* socket, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSQ_RESTART packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSQ_RESTART recv: %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x18a,
    //     packet.nUserDBID)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSQ_RESTART recv: %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x18a,
    //     packet.nUserDBID)
    L2_LOG_INFO(Log, "Received Olympiad restart request for character DB ID %d.", packet.nUserDBID);
    CSPointer<User> user = FindOlympiadPeerUser(packet.nUserDBID);
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]No user: %d",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x190,
        //     packet.nUserDBID)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]No user: %d",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x190,
        //     packet.nUserDBID)
        L2_LOG_WARN(Log, "Rejecting Olympiad restart for character DB ID %d: local user was not found.", packet.nUserDBID);
        SendOCA_RESTART(socket, packet.nUserDBID, packet.nUserSID, false);
        return false;
    }
    if (user->m_data->GetWorldId() != g_nWorldId || user->m_RaidInfo.m_nRaidWorldId == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Invalid user: %d, %d",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x197,
        //     static_cast<int>(user->GetDBID()),
        //     user->m_RaidInfo.m_nRaidWorldId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Invalid user: %d, %d",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x197,
        //     static_cast<int>(user->GetDBID()),
        //     user->m_RaidInfo.m_nRaidWorldId)
        L2_LOG_WARN(
            Log,
            "Rejecting Olympiad restart for character DB ID %d: user world %d, local world %d, raid world %d; expected "
            "local ownership and a nonzero raid world.",
            packet.nUserDBID, user->m_data->GetWorldId(), g_nWorldId, user->m_RaidInfo.m_nRaidWorldId
        );
        SendOCA_RESTART(socket, packet.nUserDBID, packet.nUserSID, false);
        return false;
    }
    CUserSocket* const userSocket = user->GetSocket();
    userSocket->AddRaidStatus(URS_RESTART_ON_RAID);
    SendOCA_RESTART(socket, packet.nUserDBID, packet.nUserSID, true);
    return false;
}

bool Handle_OSA_SET_HERO_TYPE(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSA_SET_HERO_TYPE packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    // Log.Add(
    //     LOG_BOTH,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]User become hero : %d, %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x3ba,
    //     packet.nUserDBID,
    //     packet.nHeroType)
    L2_LOG(
        Log, LOG_BOTH,
        "[Olympiad] Received hero-type response for character %d: requested type %d, result %d; character lookup follows.",
        packet.nUserDBID, packet.nHeroType, static_cast<int>(packet.cResult)
    );
    CSPointer<User> user = FindOlympiadPeerUser(packet.nUserDBID);
    CUserSocket* const socket = user ? user->GetSocket() : nullptr;
    if (socket) {
        if (packet.cResult == 1) {
            socket->SendSystemMessage(SystemMessageId::HERO_SELECTION_IN_PROGRESS);
        } else if (packet.cResult == 2) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"OlympiadPacketHandlerInPeer.cpp",
            //     0x3d3)
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"OlympiadPacketHandlerInPeer.cpp",
            //     0x3d3)
            L2_LOG_WARN(
                Log, "Olympiad hero-type update rejected for character DB ID %d: result %d, requested hero type %d.",
                packet.nUserDBID, static_cast<int>(packet.cResult), packet.nHeroType
            );
        } else {
            user->m_data->SetHeroType(packet.nHeroType);
            if (HeroType_IsHero(packet.nHeroType)) {
                user->SocialAction(0x4e30, 0x800, false);
                if (CPledge* const pledge = user->GetPledge()) {
                    pledge->ApplyPledgeSkillsTo(user.get(), false, false);
                    user->m_nParamChanged |= PCB_USER;
                }
                user->PledgeMissionCheckAndAddCount(PledgeMission::PMGT_OLYMPIAD_FIRST, 0, 1);
                WOlympiadStuff::Write_LOG_SET_HERO_InPeer(user, packet.nWinCount);
            }
            if (packet.nNPCSID != 0) {
                g_npcserver.Send("cddd", 0x52, packet.nNPCSID, user->GetServerId(), packet.nHeroType);
            }
        }
    }
    return false;
}

bool Handle_OSN_LAST_MATCH_RESULT(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSN_LAST_MATCH_RESULT packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    // Log.Add(
    //     LOG_BOTH,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]LastMatchResult: %d, %d, %d",
    //     L"OlympiadPacketHandlerInPeer.cpp",
    //     0x43c,
    //     packet.nUserDBID,
    //     packet.nMatchResult,
    //     packet.nItemCount)
    L2_LOG(
        Log, LOG_BOTH,
        "[Olympiad] Received last-match result for character %d: result %d, reward count %d; validation and reward "
        "processing follow.",
        packet.nUserDBID, packet.nMatchResult, packet.nItemCount
    );
    if (packet.nMatchResult < 0 || packet.nMatchResult > 2) {
        return false;
    }
    CSPointer<User> user = FindOlympiadPeerUser(packet.nUserDBID);
    if (!user) {
        // Log.Add(
        //     LOG_BOTH,
        //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]User is logout: %d, %d, %d",
        //     L"OlympiadPacketHandlerInPeer.cpp",
        //     0x44d,
        //     packet.nUserDBID,
        //     packet.nMatchResult,
        //     packet.nItemCount)
        L2_LOG(
            Log, LOG_BOTH,
            "[Olympiad] Cannot apply last-match result %d or reward count %d: character %d is unavailable; processing "
            "stopped.",
            packet.nMatchResult, packet.nItemCount, packet.nUserDBID
        );
        return false;
    }
    NpcServerUtil::SendOlympiadMatchResult(static_cast<int>(user->GetServerId()), packet.nMatchResult);
    if (packet.nItemCount > 0) {
        InventoryParams parameters;
        parameters.nItemClassID = Singleton<WOlympiadDefaultSetting>::inst().GetRewardItem();
        parameters.nCount = packet.nItemCount;
        if (!user->AddItemToInventory(parameters)) {
            // Log.Add(
            //     LOG_BOTH,
            //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]"
            //     L"Failed to add item: %d, %d, %d",
            //     L"OlympiadPacketHandlerInPeer.cpp",
            //     0x463,
            //     packet.nUserDBID,
            //     packet.nMatchResult,
            //     packet.nItemCount)
            L2_LOG(
                Log, LOG_BOTH,
                "[Olympiad] Could not add reward count %d to character %d's inventory for match result %d; clan mission "
                "processing will continue.",
                packet.nItemCount, packet.nUserDBID, packet.nMatchResult
            );
        }
    }
    user->PledgeMissionCheckAndAddCount(PledgeMission::PMGT_OLYMPIAD_JOIN, 0, 1);
    if (packet.nMatchResult == 1) {
        user->PledgeMissionCheckAndAddCount(PledgeMission::PMGT_OLYMPIAD_WIN, 0, 1);
    } else if (packet.nMatchResult == 2) {
        user->PledgeMissionCheckAndAddCount(PledgeMission::PMGT_OLYMPIAD_LOSE, 0, 1);
    }
    return false;
}

bool Handle_OSA_MATCH_COUNT(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSA_MATCH_COUNT packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    CSPointer<User> user = FindOlympiadPeerUser(packet.nUserDBID);
    CUserSocket* const socket = user ? user->GetSocket() : nullptr;
    if (socket) {
        const int message = packet.nGameRule == 1 ? SystemMessageId::WEEKLY_OLYMPIAD_MATCH_ALLOWANCE
                                                  : SystemMessageId::WEEKLY_CLASS_AND_OPEN_OLYMPIAD_MATCH_COUNTS;
        socket->Send("chcch", 0x62, message, 1, 0x15, static_cast<int>(static_cast<std::int16_t>(packet.nMatchCount)));
    }
    return false;
}

bool Handle_OSA_MATCH_LIST(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSA_MATCH_LIST packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    CSPointer<User> user = FindOlympiadPeerUser(packet.nUserDBID);
    CUserSocket* const socket = user ? user->GetSocket() : nullptr;
    if (!socket) {
        return false;
    }

    std::array<char, 0x5000> buffer{};
    int length = 0;
    int count = 0;
    for (const olympiad::PkMatchInfo& match : packet.vList) {
        const int assembled = ::Assemble(
            buffer.data() + length, static_cast<int>(buffer.size()) - length, "dddSS", match.nFieldID, match.nGameRule,
            match.nStatus, match.sTeam1Name.data(), match.sTeam2Name.data()
        );
        if (assembled <= 0) {
            break;
        }
        length += assembled;
        ++count;
    }
    socket->Send("chdddb", 0xfe, 0xd5, 0, count, packet.bRemain ? 1 : 0, length, buffer.data());
    return false;
}

bool Handle_OSA_HERO_LIST(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSA_HERO_LIST packet{};
    if (!DecodeOscPacket(packet, source)) {
        return true;
    }
    CSPointer<User> user = FindOlympiadPeerUser(packet.nUserDBID);
    CUserSocket* const socket = user ? user->GetSocket() : nullptr;
    if (!socket) {
        return false;
    }

    std::array<char, 0x5000> buffer{};
    int length = 0;
    int count = 0;
    for (const olympiad::PkHeroInfo& hero : packet.vList) {
        const int assembled = ::Assemble(
            buffer.data() + length, static_cast<int>(buffer.size()) - length, "SdSdSdddc", hero.sName.data(),
            hero.nClassType, hero.sPledgeName.data(), hero.nPledgeCrestId, hero.sAllianceName.data(), hero.nAllianceCrestId,
            hero.nWinCount, hero.nWorldID, hero.nHeroType
        );
        if (assembled <= 0) {
            break;
        }
        length += assembled;
        ++count;
    }
    socket->Send("chdb", 0xfe, 0x7a, count, length, buffer.data());
    return false;
}

OSCCallbackMap g_OSCPacketHandlerTable{DummyOSCPacket, u""};

namespace {

L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSA_VERSION, Handle_OSA_VERSION);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSA_REGISTER, Handle_OSA_REGISTER);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSA_START, Handle_OSA_START);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSA_STOP, Handle_OSA_STOP);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSQ_PREPARE, Handle_OSQ_PREPARE);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSN_CONNECT, Handle_OSN_CONNECT);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSA_CONNECT, Handle_OSA_CONNECT);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSN_CONFIRMED, Handle_OSN_CONFIRMED);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSN_DISCONNECTED, Handle_OSN_DISCONNECTED);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSQ_RETURN, Handle_OSQ_RETURN);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSQ_RESTART, Handle_OSQ_RESTART);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSA_OBSERVE, Handle_OSA_OBSERVE);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSN_NOTIFY_SEASON, Handle_OSN_NOTIFY_SEASON);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSN_ENTRY_COUNT, Handle_OSN_ENTRY_COUNT);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSN_PLAYER_COUNT, Handle_OSN_PLAYER_COUNT);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSA_MATCH_LIST, Handle_OSA_MATCH_LIST);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSA_HERO_LIST, Handle_OSA_HERO_LIST);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSA_HERO_MATCH_RESULT, Handle_OSA_HERO_MATCH_RESULT);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSN_SYSTEM_MESSAGE, Handle_OSN_SYSTEM_MESSAGE);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSN_SYSTEM_MESSAGE_NO, Handle_OSN_SYSTEM_MESSAGE_NO);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSN_START_SEASON, Handle_OSN_START_SEASON);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSN_STOP_SEASON, Handle_OSN_STOP_SEASON);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSN_OLYMPIAD_POINT, Handle_OSN_OLYMPIAD_POINT);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSN_NOBLESS_INFO, Handle_OSN_NOBLESS_INFO);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSA_MATCH_COUNT, Handle_OSA_MATCH_COUNT);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSA_DEBUG_STRING, Handle_OSA_DEBUG_STRING);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSA_SET_HERO_TYPE, Handle_OSA_SET_HERO_TYPE);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSA_BCMD, Handle_OSA_BCMD);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSN_STOP, Handle_OSN_STOP);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSN_CANCEL_MATCH, Handle_OSN_CANCEL_MATCH);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSA_DELETE_TRADE_POINT, Handle_OSA_DELETE_TRADE_POINT);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSN_LAST_MATCH_RESULT, Handle_OSN_LAST_MATCH_RESULT);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSA_OLYMPIAD_RECORD, Handle_OSA_OLYMPIAD_RECORD);
L2_BIND_PACKET_TOKEN(
    g_OSCPacketHandlerTable, OSA_PLEDGE_RAID_GET_SEASON, PledgeRaidPacketHandler::Handle_OSA_PLEDGE_RAID_GET_SEASON
);
L2_BIND_PACKET_TOKEN(
    g_OSCPacketHandlerTable, OSA_PLEDGE_RAID_SET_INFO, PledgeRaidPacketHandler::Handle_OSA_PLEDGE_RAID_SET_INFO
);
L2_BIND_PACKET_TOKEN(
    g_OSCPacketHandlerTable, OSA_PLEDGE_RAID_GET_INFO, PledgeRaidPacketHandler::Handle_OSA_PLEDGE_RAID_GET_INFO
);
L2_BIND_PACKET_TOKEN(
    g_OSCPacketHandlerTable,
    OSA_PLEDGE_RAID_GET_INFO_FOR_BUILDER,
    PledgeRaidPacketHandler::Handle_OSA_PLEDGE_RAID_GET_INFO_FOR_BUILDER
);
L2_BIND_PACKET_TOKEN(
    g_OSCPacketHandlerTable, OSA_PLEDGE_RAID_GET_INFO_LIST, PledgeRaidPacketHandler::Handle_OSA_PLEDGE_RAID_GET_INFO_LIST
);
L2_BIND_PACKET_TOKEN(
    g_OSCPacketHandlerTable,
    OSA_PLEDGE_RAID_GET_INFO_LIST_FOR_BUILDER,
    PledgeRaidPacketHandler::Handle_OSA_PLEDGE_RAID_GET_INFO_LIST_FOR_BUILDER
);
L2_BIND_PACKET_TOKEN(
    g_OSCPacketHandlerTable,
    OSA_PLEDGE_RAID_GET_PREV_SEASON_INFO_LIST,
    PledgeRaidPacketHandler::Handle_OSA_PLEDGE_RAID_GET_PREV_SEASON_INFO_LIST
);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSN_OLYMPIAD_MATCHING_INFO, Handle_OSN_OLYMPIAD_MATCHING_INFO);
L2_BIND_PACKET_TOKEN(g_OSCPacketHandlerTable, OSA_CLASS_RANK_BY_SERVER, Handle_OSA_CLASS_RANK_BY_SERVER);

} // namespace
