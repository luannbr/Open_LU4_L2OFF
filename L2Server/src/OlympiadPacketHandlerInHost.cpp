#include "DB.h"
#include "InstantZone.h"
#include "Object.h"
#include "OlympiadMatch.h"
#include "OlympiadNetwork.h"
#include "OlympiadPacketHandlers.h"
#include "OlympiadService.h"
#include "OlympiadServiceProxy.h"
#include "Pledge.h"
#include "PledgeMemberService.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/AtomicJob.h"
#include "Shared/src/Config.h"
#include "Shared/src/DXPacket.h"
#include "Shared/src/DateTime.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/UserConst.h"
#include "Shared/src/proto/OlympiadPacket.h"
#include "User.h"
#include "UserSocket.h"
#include "WHeroSystem.h"

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <string_view>

namespace {

template <class Packet>
bool DecodeOcsPacket(Packet& packet, const std::uint8_t* source) {
    return WOlympiadStuff::DecodePacket(packet, source);
}

std::CDualKey<int, int> OcsUserKey(const WOlympiadSocketPeer* socket, const int userDatabaseId) {
    return std::CDualKey<int, int>(socket ? socket->m_idWorld : 0, userDatabaseId);
}

CSPointer<User> FindLiveOlympiadUser(const std::uint32_t sid) {
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(sid);
    if (creature == nullptr || !creature->IsPC()) {
        return {};
    }
    User* const user = creature->CastUser();
    if (user == nullptr || user->IsDeleting() || user->m_Deleted != 0) {
        return {};
    }
    return CSPointer<User>(user, ObjectRefType::FunctionChain);
}

bool TokenEquals(const std::u16string& value, const std::u16string_view expected) {
    return runtime::StringView(value) == expected;
}


const std::u16string& GetToken(const std::vector<std::u16string>& tokens, const int index, const std::u16string& fallback) {
    if (index >= 0 && static_cast<std::size_t>(index) < tokens.size()) {
        return runtime::CheckedVectorAt(tokens, static_cast<std::size_t>(index));
    }
    return fallback;
}

int GetTokenAsInt(const std::vector<std::u16string>& tokens, const int index, const int fallback = 0) {
    if (index < 0 || static_cast<std::size_t>(index) >= tokens.size()) {
        return fallback;
    }
    const std::string value = Utf16ToUtf8(runtime::StringView(runtime::CheckedVectorAt(tokens, static_cast<std::size_t>(index))));
    return static_cast<int>(std::strtol(value.c_str(), nullptr, 10));
}

std::int64_t GetTokenAsInt64(const std::vector<std::u16string>& tokens, const int index, const std::int64_t fallback = 0) {
    if (index < 0 || static_cast<std::size_t>(index) >= tokens.size()) {
        return fallback;
    }
    const std::string value = Utf16ToUtf8(runtime::StringView(runtime::CheckedVectorAt(tokens, static_cast<std::size_t>(index))));
    return static_cast<std::int64_t>(std::strtoll(value.c_str(), nullptr, 10));
}

void Send_CLASS_RANK_BY_SERVER_TOP10(const int worldId, const int heroClassType, const int season) {
    // Current L2Server 0x008efc84.
    L2_GUARD_FUNCTION();
    AtomicLambdaJobDx* const job = MakeLambdaJobDx([worldId](AtomicLambdaJobDx* const callbackJob) {
        auto* const rank = static_cast<CDX_WOLYMPIAD_GET_CLASS_RANK_BY_SERVER_TOP10*>(callbackJob->m_pDx);
        WOlympiadService::inst().SendClassRankByServerTop10(worldId, rank);
    });
    CDX_WOLYMPIAD_GET_CLASS_RANK_BY_SERVER_TOP10 request(worldId, heroClassType, season, job);
    db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x5d), static_cast<int>(sizeof(request)), request);
}

void SendBCMDResultText(WOlympiadSocketPeer* const socket, const int userDatabaseId, std::u16string text) {
    // Current L2Server 0x008f1210.
    L2_GUARD_FUNCTION();
    if (socket == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x101,
        //     L"WO_ASSERT_NOT_NULL")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x101,
        //     L"WO_ASSERT_NOT_NULL")
        L2_LOG_WARN(
            Log, "Cannot send Olympiad builder-command result to character DB ID %d: peer socket is missing.", userDatabaseId
        );
        return;
    }

    // The original buffer holds at most 1023 text units.
    if (text.size() > 1023) {
        text.resize(1023);
    }

    olympiad::OSA_BCMD response{};
    response.nUserDBID = userDatabaseId;
    response.sMessage = text;
    socket->SendPacket(OSA_BCMD, response);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]OSA_BCMD is done: %d, %d, %ls",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0x10f,
    //     socket->m_idWorld,
    //     userDatabaseId,
    //     buffer.data())
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]OSA_BCMD is done: %d, %d, %ls",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0x10f,
    //     socket->m_idWorld,
    //     userDatabaseId,
    //     buffer.data())
    L2_LOG_INFO(
        Log, "Olympiad builder-command response send attempted for world %d, character DB ID %d: %s", socket->m_idWorld,
        userDatabaseId, Utf16ToUtf8(text).c_str()
    );
}

template <typename... Args>
void SendBCMDResult(
    WOlympiadSocketPeer* const socket, const int userDatabaseId, const std::u16string_view format, const Args&... arguments
) {
    SendBCMDResultText(socket, userDatabaseId, FormatUtf16(format, arguments...));
}

enum class RecordMatchResultReturn : std::int32_t {
    Success = 0,
    FailedWinnerIsNotOlympiadUser = 1,
    FailedWinnerMatchLimit = 2,
    FailedLoserIsNotOlympiadUser = 3,
    FailedLoserMatchLimit = 4,
};

RecordMatchResultReturn RecordMatchResult(
    const std::CDualKey<int, int> winner,
    const std::CDualKey<int, int> loser,
    const int point,
    const int repeatCount,
    const bool draw
) {
    // Current L2Server 0x008f1b78.
    L2_GUARD_FUNCTION();
    WOlympiadUsers& users = WOlympiadUsers::inst();
    if (!users.IsRegistered(winner)) {
        return RecordMatchResultReturn::FailedWinnerIsNotOlympiadUser;
    }
    if (!users.IsRegistered(loser)) {
        return RecordMatchResultReturn::FailedLoserIsNotOlympiadUser;
    }

    WOlympiadService& service = WOlympiadService::inst();
    const GameRuleType gameRule = service.GetValidRule();
    WOlympiadUsers::MatchLimitType limit = WOlympiadUsers::MLT_MAX;
    if (!users.CanRegistMatch(winner, service.GetSeason(), service.GetStep(), limit, gameRule)) {
        return RecordMatchResultReturn::FailedWinnerMatchLimit;
    }
    if (!users.CanRegistMatch(loser, service.GetSeason(), service.GetStep(), limit, gameRule)) {
        return RecordMatchResultReturn::FailedLoserMatchLimit;
    }

    if (repeatCount != 0) {
        const int loserPoint = -point;
        for (int index = 0; index < repeatCount; ++index) {
            if (draw) {
                service.SaveOlympiadRecord(winner, loser, loserPoint, 0, 0, loserPoint, 0, 0, 1, -1, gameRule);
                service.SaveLastMatchResult(winner, GRE_DRAW, 0);
                service.NotifyLastMatchResult(winner);
                service.SaveLastMatchResult(loser, GRE_DRAW, 0);
            } else {
                service.SaveOlympiadRecord(winner, loser, point, 0, 0, loserPoint, 0, 0, 0, -1, gameRule);
                service.SaveLastMatchResult(winner, GRE_WIN, 0);
                service.NotifyLastMatchResult(winner);
                service.SaveLastMatchResult(loser, GRE_LOSE, 0);
            }
            service.NotifyLastMatchResult(loser);
        }
        users.IncMatchCount(winner, gameRule, repeatCount);
        users.IncMatchCount(loser, gameRule, repeatCount);
    }
    return RecordMatchResultReturn::Success;
}

void SendBCMDResultByRecordMatchResult(
    WOlympiadSocketPeer* const socket,
    const int userDatabaseId,
    const std::u16string& command,
    const RecordMatchResultReturn result
) {
    // Current L2Server 0x008f1340.
    L2_GUARD_FUNCTION();
    if (result == RecordMatchResultReturn::Success) {
        SendBCMDResult(socket, userDatabaseId, u"[olympiad %s] accepted", command);
        return;
    }
    switch (result) {
        case RecordMatchResultReturn::FailedWinnerIsNotOlympiadUser:
        case RecordMatchResultReturn::FailedLoserIsNotOlympiadUser:
            SendBCMDResult(socket, userDatabaseId, u"loser is not an olympiad user");
            break;
        case RecordMatchResultReturn::FailedWinnerMatchLimit:
            SendBCMDResult(socket, userDatabaseId, u"Winner cannot register by match limit");
            break;
        case RecordMatchResultReturn::FailedLoserMatchLimit:
            SendBCMDResult(socket, userDatabaseId, u"Loser cannot register by match limit");
            break;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"OlympiadPacketHandlerInHost.cpp",
            //     0x1b2)
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"OlympiadPacketHandlerInHost.cpp",
            //     0x1b2)
            L2_LOG_ERROR(
                Log, "Cannot report Olympiad match-record command outcome: unsupported result %d (character DB ID %d).",
                static_cast<int>(result), userDatabaseId
            );
            break;
    }
}

void HandleBCMD_result(
    WOlympiadSocketPeer* const socket, const int userDatabaseId, const std::vector<std::u16string>& tokens
) {
    // Current L2Server 0x008f1eac.
    L2_GUARD_FUNCTION();
    if (tokens.size() < 4) {
        SendBCMDResult(socket, userDatabaseId, u"//olympiad result winner loser point [draw]");
        return;
    }
    const std::CDualKey<int, int> winner(socket->m_idWorld, GetTokenAsInt(tokens, 1));
    const std::CDualKey<int, int> loser(socket->m_idWorld, GetTokenAsInt(tokens, 2));
    const std::u16string empty;
    const bool draw = TokenEquals(GetToken(tokens, 4, empty), u"draw");
    const RecordMatchResultReturn result = RecordMatchResult(winner, loser, GetTokenAsInt(tokens, 3), 1, draw);
    SendBCMDResultByRecordMatchResult(socket, userDatabaseId, runtime::CheckedVectorAt(tokens, 0), result);
}

void HandleBCMD_result_repeat(
    WOlympiadSocketPeer* const socket, const int userDatabaseId, const std::vector<std::u16string>& tokens
) {
    // Current L2Server 0x008f209c.
    L2_GUARD_FUNCTION();
    if (tokens.size() < 5) {
        SendBCMDResult(socket, userDatabaseId, u"//olympiad result_repeat winner loser point repeat_count [draw]");
        return;
    }
    const std::CDualKey<int, int> winner(socket->m_idWorld, GetTokenAsInt(tokens, 1));
    const std::CDualKey<int, int> loser(socket->m_idWorld, GetTokenAsInt(tokens, 2));
    const std::u16string empty;
    const bool draw = TokenEquals(GetToken(tokens, 5, empty), u"draw");
    const RecordMatchResultReturn result =
        RecordMatchResult(winner, loser, GetTokenAsInt(tokens, 3), GetTokenAsInt(tokens, 4, 1), draw);
    SendBCMDResultByRecordMatchResult(socket, userDatabaseId, runtime::CheckedVectorAt(tokens, 0), result);
}

} // namespace

bool DummyOCSPacket(WOlympiadSocketPeer*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d]DummyOCSPacket called",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0x18)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d]DummyOCSPacket called",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0x18)
    L2_LOG_ERROR(Log, "Unhandled Olympiad peer request reached the host dispatch table.");
    return false;
}

bool Handle_OCQ_VERSION(WOlympiadSocketPeer* const socket, const std::uint8_t* const source) {
    // Current L2Server 0x008ee704.
    L2_GUARD_FUNCTION();
    olympiad::OCQ_VERSION packet{};
    if (!DecodeOcsPacket(packet, source)) {
        return true;
    }
    if (packet.nWorldId < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d] nWorldId is invalid: %d",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x2c,
        //     packet.nWorldId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d] nWorldId is invalid: %d",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x2c,
        //     packet.nWorldId)
        L2_LOG_WARN(Log, "Rejecting Olympiad peer handshake: world ID %d must be positive.", packet.nWorldId);
        return true;
    }
    if (!WOlympiadServiceConfig::inst().IsAllowedPeer(packet.nWorldId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d] World is not allowed: %d",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x34,
        //     packet.nWorldId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d] World is not allowed: %d",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x34,
        //     packet.nWorldId)
        L2_LOG_WARN(
            Log, "Rejecting Olympiad peer handshake: world %d is not allowed by the service configuration.", packet.nWorldId
        );
        return true;
    }

    socket->m_idWorld = packet.nWorldId;
    if (!WOlympiadNetworkHost::inst().RegisterPeer(CSPointer<WOlympiadSocketPeer>(socket, ObjectRefType::FunctionChain))) {
        socket->m_idWorld = 0;
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d] Registered world is already connected: %d",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x3e,
        //     packet.nWorldId)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d] Registered world is already connected: %d",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x3e,
        //     packet.nWorldId)
        L2_LOG_WARN(Log, "Cannot register Olympiad peer for world %d; socket world ID was reset to zero.", packet.nWorldId);
        return true;
    }

    CDX_WOLYMPIAD_CHECK_PEER request(packet.nWorldId);
    db.RequestTPacketProcess(g_nWorldId, 0x42, static_cast<int>(sizeof(request)), request);
    return false;
}

bool Handle_OCQ_REGISTER(WOlympiadSocketPeer* const socket, const std::uint8_t* const source) {
    // Current L2Server 0x008efe70.
    L2_GUARD_FUNCTION();
    olympiad::OCQ_REGISTER packet{};
    if (!DecodeOcsPacket(packet, source)) {
        return true;
    }
    if (!WOlympiadStuff::IsAllowedClass(static_cast<ClassType>(packet.nClass))) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]ClassType is wrong: %d, %d, %d",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x5c,
        //     socket->m_idWorld,
        //     packet.nUserDBID,
        //     packet.nClass)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]ClassType is wrong: %d, %d, %d",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x5c,
        //     socket->m_idWorld,
        //     packet.nUserDBID,
        //     packet.nClass)
        L2_LOG_WARN(
            Log, "Cannot register Olympiad character DB ID %d from world %d: class %d is not allowed.", packet.nUserDBID,
            socket->m_idWorld, packet.nClass
        );
        return false;
    }

    CDX_WOLYMPIAD_ADD_USER request(
        socket->m_idWorld, packet.nUserDBID, packet.nClass, packet.nClass, packet.sCharName.c_str(), packet.nPledgeDBID,
        packet.sPledgeName.c_str(), packet.nPledgeCrestID, packet.nAllianceDBID, packet.sAllianceName.c_str(),
        packet.nAllianceCrestID, 1000, CDX_WOLYMPIAD_ADD_USER::NA_Register
    );
    db.RequestTPacketProcess(g_nWorldId, 0x46, static_cast<int>(sizeof(request)), request);
    return false;
}

bool Handle_OCQ_START(WOlympiadSocketPeer* const socket, const std::uint8_t* const source) {
    // Current L2Server 0x008f0840.
    L2_GUARD_FUNCTION();
    olympiad::OCQ_START packet{};
    if (!DecodeOcsPacket(packet, source)) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OCQ_START recv: %d, %d",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0xbf,
    //     socket->m_idWorld,
    //     packet.nUserDBID)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OCQ_START recv: %d, %d",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0xbf,
    //     socket->m_idWorld,
    //     packet.nUserDBID)
    L2_LOG_INFO(
        Log, "Received Olympiad matchmaking request from world %d, character DB ID %d.", socket->m_idWorld, packet.nUserDBID
    );

    const std::CDualKey<int, int> key = OcsUserKey(socket, packet.nUserDBID);
    CSPointer<WOlympiadMemberInfo> member(new WOlympiadMemberInfo(key, packet.Application), ObjectRefType::FunctionChain);
    if (WOlympiadService::inst().StartMatchMaking(member) == WOlympiadService::SMMR_Success) {
        WOlympiadStuff::Write_LOG_WOLYMPIAD_ADD_ENTRY_InHost(socket->m_idWorld, packet.nUserDBID, packet.Application);
    }
    return false;
}

bool Handle_OCQ_CONNECT(WOlympiadSocketPeer* const socket, const std::uint8_t* const source) {
    // Current L2Server 0x008eeb90.
    L2_GUARD_FUNCTION();
    olympiad::OCQ_CONNECT packet{};
    if (!DecodeOcsPacket(packet, source)) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OCQ_CONNECT recv: %d, %d",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0x88,
    //     socket->m_idWorld,
    //     packet.nUserDBID)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OCQ_CONNECT recv: %d, %d",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0x88,
    //     socket->m_idWorld,
    //     packet.nUserDBID)
    L2_LOG_INFO(
        Log, "Received Olympiad connection request from world %d, character DB ID %d.", socket->m_idWorld, packet.nUserDBID
    );

    olympiad::OSA_CONNECT response{};
    response.nUserDBID = packet.nUserDBID;
    response.bSuccess = false;
    response.bAsPlayer = false;
    const std::CDualKey<int, int> key = OcsUserKey(socket, packet.nUserDBID);
    if (!WOlympiadService::inst().RegisterAuthInfo(key, packet.authInfo)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
        //     L"[PACKET]OSA_CONNECT sent: %d, %d",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x91,
        //     socket->m_idWorld,
        //     response.nUserDBID)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
        //     L"[PACKET]OSA_CONNECT sent: %d, %d",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x91,
        //     socket->m_idWorld,
        //     response.nUserDBID)
        L2_LOG_WARN(
            Log,
            "Cannot register Olympiad authentication information for world %d, character DB ID %d; sending a rejected "
            "connection response.",
            socket->m_idWorld, response.nUserDBID
        );
        socket->SendPacket(OSA_CONNECT, response);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Failed to register auth: %d, %d",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x94,
        //     key.m_PrimaryKey,
        //     key.m_SecondaryKey)
        // Noise: the preceding rejection message already identifies this authentication registration failure.
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Failed to register auth: %d, %d",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x94,
        //     key.m_PrimaryKey,
        //     key.m_SecondaryKey)
        return false;
    }

    CSPointer<WOlympiadMemberInfo> member = WOlympiadService::inst().FindMember(key);
    if (!member) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x9d)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x9d)
        L2_LOG_WARN(
            Log,
            "Olympiad member from world %d, character DB ID %d disappeared after authentication registration; sending a "
            "rejected connection response.",
            key.m_PrimaryKey, key.m_SecondaryKey
        );
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
        //     L"[PACKET]OSA_CONNECT sent: %d, %d",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x9e,
        //     socket->m_idWorld,
        //     response.nUserDBID)
        // Noise: the preceding missing-member message already describes the rejected response sent next.
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
        //     L"[PACKET]OSA_CONNECT sent: %d, %d",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x9e,
        //     socket->m_idWorld,
        //     response.nUserDBID)
        socket->SendPacket(OSA_CONNECT, response);
        return false;
    }
    response.bAsPlayer = member->IsPlayer();
    response.bSuccess = true;
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSA_CONNECT sent: %d, %d",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0xa9,
    //     socket->m_idWorld,
    //     response.nUserDBID)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OSA_CONNECT sent: %d, %d",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0xa9,
    //     socket->m_idWorld,
    //     response.nUserDBID)
    L2_LOG_INFO(
        Log,
        "Olympiad authentication information registered for world %d, character DB ID %d; sending an accepted connection "
        "response as %s.",
        socket->m_idWorld, response.nUserDBID, response.bAsPlayer ? "player" : "observer"
    );
    socket->SendPacket(OSA_CONNECT, response);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]Succeed to register auth: %d, %d",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0xad,
    //     key.m_PrimaryKey,
    //     key.m_SecondaryKey)
    // Noise: registration success is already reported before the response send. This call adds no delivery evidence.
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]Succeed to register auth: %d, %d",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0xad,
    //     key.m_PrimaryKey,
    //     key.m_SecondaryKey)
    return false;
}

bool Handle_OCA_RETURN(WOlympiadSocketPeer* const peer, const std::uint8_t* const source) {
    // Current L2Server 0x008eef54.
    L2_GUARD_FUNCTION();
    olympiad::OCA_RETURN packet{};
    if (!DecodeOcsPacket(packet, source)) {
        return true;
    }
    CSPointer<User> user = FindLiveOlympiadUser(static_cast<std::uint32_t>(packet.nUserSID));
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]No user: %d, %d",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x3b5,
        //     peer->m_idWorld,
        //     packet.nUserSID)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]No user: %d, %d",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x3b5,
        //     peer->m_idWorld,
        //     packet.nUserSID)
        L2_LOG_WARN(
            Log, "Cannot process Olympiad return response from world %d: live user object ID %u was not found.",
            peer->m_idWorld, static_cast<unsigned int>(packet.nUserSID)
        );
        return false;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]"
    //     L"OCA_RETURN recv: %d, %d",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0x3b9,
    //     user->m_data->m_nWorldId,
    //     static_cast<int>(user->GetDBID()))
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]"
    //     L"OCA_RETURN recv: %d, %d",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0x3b9,
    //     user->m_data->m_nWorldId,
    //     static_cast<int>(user->GetDBID()))
    L2_LOG_INFO(
        Log, "Received Olympiad return response for origin world %d, character DB ID %d: accepted %s.",
        user->m_data->m_nWorldId, static_cast<int>(user->GetDBID()), packet.bSuccess ? "yes" : "no"
    );
    CUserSocket* const socket = user->GetSocket();
    if (socket == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]No user socket",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x3bf)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]No user socket",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x3bf)
        L2_LOG_WARN(
            Log, "Cannot process Olympiad return response for character DB ID %d: user socket is missing.",
            static_cast<int>(user->GetDBID())
        );
        return false;
    }
    if (!packet.bSuccess) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]OCA_RETURN false : %d, %d",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x3c6,
        //     static_cast<int>(user->GetDBID()),
        //     packet.nUserSID)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]OCA_RETURN false : %d, %d",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x3c6,
        //     static_cast<int>(user->GetDBID()),
        //     packet.nUserSID)
        L2_LOG_WARN(
            Log, "Olympiad return was rejected for character DB ID %d, object ID %u; closing the client connection.",
            static_cast<int>(user->GetDBID()), static_cast<unsigned int>(packet.nUserSID)
        );
        socket->SendSystemMessage(SystemMessageId::CANNOT_TELEPORT_DIMENSIONAL_SPACE_CLOSED);
        socket->ForceClose(SCR_RETURN_TO_ORIGIN);
        return false;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]"
    //     L"S_EX_CLOSE_RAID_SOCKET sent: %d, %d, RSCR_RETURN_TO_ORIGIN",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0x3cf,
    //     user->m_data->m_nWorldId,
    //     static_cast<int>(user->GetDBID()))
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]"
    //     L"S_EX_CLOSE_RAID_SOCKET sent: %d, %d, RSCR_RETURN_TO_ORIGIN",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0x3cf,
    //     user->m_data->m_nWorldId,
    //     static_cast<int>(user->GetDBID()))
    L2_LOG_INFO(
        Log, "Olympiad return accepted for origin world %d, character DB ID %d; sending the raid-socket close instruction.",
        user->m_data->m_nWorldId, static_cast<int>(user->GetDBID())
    );
    socket->Send("chd", 0xfe, 0xb9, 0);
    socket->m_nCloseReason = SCR_RETURN_TO_ORIGIN;
    {
        L2_GUARD(socket->mLock);
        socket->UnBindUser();
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]UserSocket::UnBindUser done: %d, %d, %p",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0x3dc,
    //     static_cast<int>(user->GetDBID()),
    //     packet.nUserSID,
    //     socket)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]UserSocket::UnBindUser done: %d, %d, %p",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0x3dc,
    //     static_cast<int>(user->GetDBID()),
    //     packet.nUserSID,
    //     socket)
    L2_LOG_INFO(
        Log, "Olympiad return cleanup: user unbind call completed for character DB ID %d, object ID %u (socket %p).",
        static_cast<int>(user->GetDBID()), static_cast<unsigned int>(packet.nUserSID), static_cast<void*>(socket)
    );
    if (peer != nullptr) {
        peer->OnUserSocketClosed();
    }
    return false;
}

bool Handle_OCA_RESTART(WOlympiadSocketPeer* const peer, const std::uint8_t* const source) {
    // Current L2Server 0x008ef300.
    L2_GUARD_FUNCTION();
    olympiad::OCA_RESTART packet{};
    if (!DecodeOcsPacket(packet, source)) {
        return true;
    }
    CSPointer<User> user = FindLiveOlympiadUser(static_cast<std::uint32_t>(packet.nUserSID));
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]No user: %d, %d",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x3f7,
        //     peer->m_idWorld,
        //     packet.nUserSID)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]No user: %d, %d",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x3f7,
        //     peer->m_idWorld,
        //     packet.nUserSID)
        L2_LOG_WARN(
            Log, "Cannot process Olympiad restart response from world %d: live user object ID %u was not found.",
            peer->m_idWorld, static_cast<unsigned int>(packet.nUserSID)
        );
        return false;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]"
    //     L"OCA_RESTART recv: %d, %d",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0x3fb,
    //     user->m_data->m_nWorldId,
    //     static_cast<int>(user->GetDBID()))
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]"
    //     L"OCA_RESTART recv: %d, %d",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0x3fb,
    //     user->m_data->m_nWorldId,
    //     static_cast<int>(user->GetDBID()))
    L2_LOG_INFO(
        Log, "Received Olympiad restart response for origin world %d, character DB ID %d: accepted %s.",
        user->m_data->m_nWorldId, static_cast<int>(user->GetDBID()), packet.bSuccess ? "yes" : "no"
    );
    CUserSocket* const socket = user->GetSocket();
    if (socket == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x401)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x401)
        L2_LOG_WARN(
            Log, "Cannot process Olympiad restart response for character DB ID %d: user socket is missing.",
            static_cast<int>(user->GetDBID())
        );
        return false;
    }
    if (!packet.bSuccess) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Restart failed: %d, %d",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x407,
        //     static_cast<int>(user->GetDBID()),
        //     packet.nUserSID)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Restart failed: %d, %d",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x407,
        //     static_cast<int>(user->GetDBID()),
        //     packet.nUserSID)
        L2_LOG_WARN(
            Log, "Olympiad restart was rejected for character DB ID %d, object ID %u; closing the client connection.",
            static_cast<int>(user->GetDBID()), static_cast<unsigned int>(packet.nUserSID)
        );
        socket->ForceClose(SCR_ILLEGAL_BEHAVIOR);
        return false;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]"
    //     L"S_EX_CLOSE_RAID_SOCKET sent: %d, %d, "
    //     L"RSCR_RESTART_ON_RAID_SERVER",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0x40f,
    //     user->m_data->m_nWorldId,
    //     static_cast<int>(user->GetDBID()))
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD][PACKET]"
    //     L"S_EX_CLOSE_RAID_SOCKET sent: %d, %d, "
    //     L"RSCR_RESTART_ON_RAID_SERVER",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0x40f,
    //     user->m_data->m_nWorldId,
    //     static_cast<int>(user->GetDBID()))
    L2_LOG_INFO(
        Log, "Olympiad restart accepted for origin world %d, character DB ID %d; sending the raid-socket close instruction.",
        user->m_data->m_nWorldId, static_cast<int>(user->GetDBID())
    );
    socket->Send("chd", 0xfe, 0xb9, 1);
    socket->m_nCloseReason = SCR_RETURN_TO_ORIGIN;
    {
        L2_GUARD(socket->mLock);
        socket->UnBindUser();
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]UserSocket::UnBindUser done: %d, %d, %p",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0x417,
    //     static_cast<int>(user->GetDBID()),
    //     packet.nUserSID,
    //     socket)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]UserSocket::UnBindUser done: %d, %d, %p",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0x417,
    //     static_cast<int>(user->GetDBID()),
    //     packet.nUserSID,
    //     socket)
    L2_LOG_INFO(
        Log, "Olympiad restart cleanup: user unbind call completed for character DB ID %d, object ID %u (socket %p).",
        static_cast<int>(user->GetDBID()), static_cast<unsigned int>(packet.nUserSID), static_cast<void*>(socket)
    );
    if (peer != nullptr) {
        peer->OnUserSocketClosed();
    }
    return false;
}

bool Handle_OCQ_SET_HERO_TYPE(WOlympiadSocketPeer* const socket, const std::uint8_t* const source) {
    // Current L2Server 0x008f0458.
    L2_GUARD_FUNCTION();
    olympiad::OCQ_SET_HERO_TYPE packet{};
    if (!DecodeOcsPacket(packet, source)) {
        return true;
    }
    olympiad::OSA_SET_HERO_TYPE response{};
    response.nUserDBID = packet.nUserDBID;
    response.nHeroType = packet.nHeroType;
    response.nNPCSID = packet.nNPCSID;
    response.nWinCount = 0;
    response.cResult = 0;
    if (!packet.bByBuilder && !WOlympiadService::inst().IsPossibleToSetHero()) {
        response.cResult = 1;
        socket->SendPacket(OSA_SET_HERO_TYPE, response);
        return false;
    }

    CDX_WOLYMPIAD_SAVE_HERO_TYPE request(
        socket->m_idWorld, packet.nUserDBID, packet.nHeroType, packet.sCharName.c_str(), packet.nPledgeDBID,
        packet.sPledgeName.c_str(), packet.nPledgeCrestID, packet.nAllianceDBID, packet.sAllianceName.c_str(),
        packet.nAllianceCrestID, packet.nNPCSID, 0
    );
    db.RequestTPacketProcess(g_nWorldId, 0x54, static_cast<int>(sizeof(request)), request);
    if (packet.bByBuilder) {
        WHeroSystem::inst().RequestReloadHeroRankToCacheD();
    }
    return false;
}

bool Handle_OCQ_BCMD(WOlympiadSocketPeer* const socket, const std::uint8_t* const source) {
    // Current L2Server 0x008f229c.
    L2_GUARD_FUNCTION();
    olympiad::OCQ_BCMD packet{};
    if (!DecodeOcsPacket(packet, source)) {
        return true;
    }

    std::vector<std::u16string> tokens;
    for (const std::u16string& value : packet.vParams) {
        tokens.push_back(value);
    }
    if (tokens.empty()) {
        return false;
    }

    const std::u16string& command = runtime::CheckedVectorAt(tokens, 0);
    const int userDatabaseId = packet.nUserDBID;
    const std::CDualKey<int, int> key(socket->m_idWorld, userDatabaseId);
    static const std::u16string empty;
    static const std::u16string notAvailable(u"NA");

    if (TokenEquals(command, u"reset")) {
        WOlympiadService::inst().ClearAllEntry();
        SendBCMDResult(socket, userDatabaseId, u"[olympiad reset] accepted");
    } else if (TokenEquals(command, u"start")) {
        WOlympiadService& service = WOlympiadService::inst();
        if (service.GetStep() == WOlympiadCondition::NONE) {
            service.SetOlympiadMonthly(0, false, true);
            service.StartOlympiadSeason();
            SendBCMDResult(socket, userDatabaseId, u"[olympiad start] accepted");
        } else {
            SendBCMDResult(socket, userDatabaseId, u"[olympiad start] already started");
        }
    } else if (TokenEquals(command, u"start_time")) {
        std::tm start{};
        start.tm_min = GetTokenAsInt(tokens, 5, 0);
        start.tm_hour = GetTokenAsInt(tokens, 4, 0);
        start.tm_mday = GetTokenAsInt(tokens, 3, 0);
        start.tm_mon = GetTokenAsInt(tokens, 2, 1) - 1;
        start.tm_year = GetTokenAsInt(tokens, 1, 1900) - 1900;
        const std::int32_t seasonStart = static_cast<std::int32_t>(std::mktime(&start));
        WOlympiadService& service = WOlympiadService::inst();
        service.SetSeasonStartOnTheFly(seasonStart);
        DxSaveSeasonStartTime request(service.GetSeason(), seasonStart);
        db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x2d), static_cast<int>(sizeof(request)), request);
        SendBCMDResult(socket, userDatabaseId, u"[olympiad start_time] accepted");
    } else if (TokenEquals(command, u"monthly")) {
        const std::u16string& option = GetToken(tokens, 1, empty);
        WOlympiadService::inst().SetOlympiadMonthly(
            0, !TokenEquals(option, u"same_season"), !TokenEquals(option, u"no_hero")
        );
        SendBCMDResult(socket, userDatabaseId, u"[olympiad monthly] accepted");
    } else if (TokenEquals(command, u"view_match_list")) {
        if (TokenEquals(GetToken(tokens, 1, empty), u"force")) {
            WOlympiadService::inst().SendGameListDump(key);
        } else {
            WOlympiadService::inst().SendGameList(key);
        }
        SendBCMDResult(socket, userDatabaseId, u"[olympiad view_match_list] accepted");
    } else if (TokenEquals(command, u"term")) {
        WOlympiadService& service = WOlympiadService::inst();
        service.SetOlympiadTerm(
            false, true, service.GetSeason(), 0, 0, 0, 0, 0, GetTokenAsInt(tokens, 1, 5),
            TokenEquals(GetToken(tokens, 2, empty), u"same_season"), true
        );
        SendBCMDResult(socket, userDatabaseId, u"[olympiad term] accepted");
    } else if (TokenEquals(command, u"term_ex")) {
        const int bonus1 = GetTokenAsInt(tokens, 1, 0) + GetTokenAsInt(tokens, 2, 0);
        const int bonus2 = bonus1 + GetTokenAsInt(tokens, 3, 0);
        const int bonus3 = bonus2 + GetTokenAsInt(tokens, 4, 0);
        const int bonus4 = bonus3 + GetTokenAsInt(tokens, 5, 0);
        const int nominate = bonus4 + GetTokenAsInt(tokens, 6, 0);
        WOlympiadService& service = WOlympiadService::inst();
        service.SetOlympiadTerm(
            false, true, service.GetSeason(), 0, bonus1, bonus2, bonus3, bonus4, nominate,
            TokenEquals(GetToken(tokens, 7, empty), u"same_season"), true
        );
        SendBCMDResult(socket, userDatabaseId, u"[olympiad term_ex] accepted");
    } else if (TokenEquals(command, u"result") || TokenEquals(command, u"result_classless")) {
        HandleBCMD_result(socket, userDatabaseId, tokens);
    } else if (TokenEquals(command, u"result_class") || TokenEquals(command, u"result_team")) {
        SendBCMDResult(socket, userDatabaseId, u"//olympiad result_class or result_team is deprecated");
    } else if (TokenEquals(command, u"result_repeat")) {
        HandleBCMD_result_repeat(socket, userDatabaseId, tokens);
    } else if (TokenEquals(command, u"heroes")) {
        WHeroSystem::inst().SendHeroList(key);
        SendBCMDResult(socket, userDatabaseId, u"[olympiad heroes] accepted");
    } else if (TokenEquals(command, u"start_match_time")) {
        const unsigned int hour = static_cast<unsigned int>(GetTokenAsInt(tokens, 1, 0));
        const unsigned int minute = static_cast<unsigned int>(GetTokenAsInt(tokens, 2, 0));
        if (hour >= 25 || minute >= 60) {
            SendBCMDResult(socket, userDatabaseId, u"invalid time value");
        } else {
            WOlympiadDefaultSetting::inst().SetTimeSetting(static_cast<int>(hour), static_cast<int>(minute));
            SendBCMDResult(socket, userDatabaseId, u"[olympiad start_match_time] accepted");
        }
    } else if (TokenEquals(command, u"add_point")) {
        const int point = GetTokenAsInt(tokens, 1, 0);
        const int targetUserDatabaseId = GetTokenAsInt(tokens, 2, 0);
        const std::CDualKey<int, int> target(socket->m_idWorld, targetUserDatabaseId);
        if (!WOlympiadUsers::inst().IsRegistered(target)) {
            SendBCMDResult(socket, userDatabaseId, u"target user is not nobless!!");
        } else {
            CDX_WOLYMPIAD_ADD_POINT request(socket->m_idWorld, targetUserDatabaseId, point);
            db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x52), static_cast<int>(sizeof(request)), request);
            SendBCMDResult(socket, userDatabaseId, u"[olympiad add_point] accepted");
        }
    } else if (TokenEquals(command, u"show_inzone_type")) {
        const int displayFieldId = GetTokenAsInt(tokens, 1, 1);
        const int fieldId = displayFieldId - 1;
        WOlympiadMatch* const field = WOlympiadService::inst().GetField(fieldId);
        if (field == nullptr) {
            SendBCMDResult(socket, userDatabaseId, u"invalid field id: %d", fieldId);
        } else if (CInstantZone* const zone = field->GetInZone()) {
            SendBCMDResult(
                socket, userDatabaseId, u"fieldId[%d] inzoneTypeId[%d]", displayFieldId, zone->GetInstantZoneTypeID()
            );
        } else {
            SendBCMDResult(socket, userDatabaseId, u"no inzone: %d", fieldId);
        }
    } else if (TokenEquals(command, u"min_match_count")) {
        WOlympiadDefaultSetting::inst().SetMatchMinCount(GRT_CLASSLESS, GetTokenAsInt(tokens, 1, 1));
        WOlympiadDefaultSetting::inst().SetMatchMinCount(GRT_CLASS, GetTokenAsInt(tokens, 2, 1));
        SendBCMDResult(socket, userDatabaseId, u"[olympiad min_match_count] accepted");
    } else if (TokenEquals(command, u"end_match_time")) {
        const unsigned int hour = static_cast<unsigned int>(GetTokenAsInt(tokens, 1, 0));
        const unsigned int minute = static_cast<unsigned int>(GetTokenAsInt(tokens, 2, 0));
        if (hour > 24 || minute > 59) {
            SendBCMDResult(socket, userDatabaseId, u"invalid time value");
        } else {
            WOlympiadDefaultSetting::inst().SetEndTime(static_cast<int>(hour), static_cast<int>(minute));
            SendBCMDResult(socket, userDatabaseId, u"[olympiad end_match_time] accepted");
        }
    } else if (TokenEquals(command, u"set_match_day")) {
        const int dayCount = GetTokenAsInt(tokens, 1, 0);
        if (dayCount != static_cast<int>(tokens.size()) - 2) {
            SendBCMDResult(
                socket, userDatabaseId,
                u"//olympiad set_match_day day_count day_no1(0 : sunday ~ 6 : saterday) day_no2 ....."
            );
        } else {
            std::vector<L2DateTime::DayOfWeek> days;
            for (std::size_t index = 2; index < tokens.size(); ++index) {
                days.push_back(static_cast<L2DateTime::DayOfWeek>(GetTokenAsInt(tokens, static_cast<int>(index), 0)));
            }
            WOlympiadDefaultSetting::inst().ResetMatchDay(days);
            SendBCMDResult(socket, userDatabaseId, u"[olympiad set_match_day] accepted");
        }
    } else if (TokenEquals(command, u"rank")) {
        const int ranking = GetTokenAsInt(tokens, 1, 0);
        const ClassType classType = static_cast<ClassType>(GetTokenAsInt(tokens, 2, 0));
        const std::wstring name = Utf16ToWide(GetToken(tokens, 3, notAvailable));
        WOlympiadServiceUtil::AddRankInfo(classType, ranking, name.c_str(), 10);
        SendBCMDResult(socket, userDatabaseId, u"[olympiad rank] accepted");
    } else if (TokenEquals(command, u"next_step")) {
        CSPointer<WOlympiadMemberInfo> member = WOlympiadService::inst().FindMember(key);
        if (!member) {
            SendBCMDResult(socket, userDatabaseId, u"[olympiad next_step] is not member: %d", userDatabaseId);
        } else {
            WOlympiadService::inst().DoNextStep(member->GetFieldID(), GetToken(tokens, 1, empty));
            SendBCMDResult(socket, userDatabaseId, u"[olympiad next_step] accepted");
        }
    } else if (TokenEquals(command, u"matchpoint")) {
        WOlympiadService::inst().SetDebugUser(key);
        WOlympiadService::inst().DebugString(u"start olympiad matching point debugging!!");
    } else if (TokenEquals(command, u"inzone_type")) {
        WOlympiadService::inst().SetForceInstantZoneTypeID(GetTokenAsInt(tokens, 1, 0));
        SendBCMDResult(socket, userDatabaseId, u"[olympiad inzone_type] accepted");
    } else if (TokenEquals(command, u"add_tpoint")) {
        CDX_WOLYMPIAD_ADD_TRADE_POINT request(socket->m_idWorld, userDatabaseId, GetTokenAsInt(tokens, 1, 0));
        db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x56), static_cast<int>(sizeof(request)), request);
        SendBCMDResult(socket, userDatabaseId, u"[olympiad add_tpoint] accepted");
    } else if (TokenEquals(command, u"del_tpoint")) {
        CDX_WOLYMPIAD_DELETE_TRADE_POINT request(
            socket->m_idWorld, userDatabaseId, u"", 0, u"", GetTokenAsInt(tokens, 1, 0), 0
        );
        db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x55), static_cast<int>(sizeof(request)), request);
        SendBCMDResult(socket, userDatabaseId, u"[olympiad del_tpoint] accepted");
    } else if (TokenEquals(command, u"matchpoint_set")) {
        const std::int64_t requested = GetTokenAsInt64(tokens, 1, 0);
        olympiad::PkApplication application{};
        char16_t nameBuffer[64]{};
        FormatUtf16To(nameBuffer, std::size(nameBuffer), u"%d_%d", socket->m_idWorld, userDatabaseId);
        application.sCharName = std::u16string(nameBuffer);
        CSPointer<WOlympiadMemberInfo> member(new WOlympiadMemberInfo(key, application), ObjectRefType::FunctionChain);
        member->SetMatchingPoint(requested - 1000);
        std::vector<CSPointer<WOlympiadMemberInfo>> members;
        members.push_back(member);
        WOlympiadServiceUtil::SaveMatchPoints(0, members);
        SendBCMDResult(socket, userDatabaseId, u"[olympiad %s] accepted", command);
    } else if (TokenEquals(command, u"matchpoint_reset_all")) {
        CDX_WOLYMPIAD_MATCHING_POINT_RESET_ALL request{};
        db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x5a), static_cast<int>(sizeof(request)), request);
        SendBCMDResult(socket, userDatabaseId, u"[olympiad matchpoint_reset_all] accepted");
    } else if (TokenEquals(command, u"matchpoint_mode")) {
        const std::u16string option = GetToken(tokens, 1, empty);
        if (option.empty()) {
            SendBCMDResult(socket, userDatabaseId, u"//olympiad matchpoint_mode on|off");
        } else {
            WOlympiadServiceConst::_MATCHING_POINT_MODE = TokenEquals(option, u"on");
            SendBCMDResult(socket, userDatabaseId, u"[olympiad matchpoint_mode %s] accepted", option);
        }
    } else if (TokenEquals(command, u"matchpoint_view")) {
        CDX_WOLYMPIAD_MATCHING_POINT request(socket->m_idWorld, userDatabaseId);
        db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x58), static_cast<int>(sizeof(request)), request);
        SendBCMDResult(socket, userDatabaseId, u"[olympiad matchpoint_view] accepted");
    } else if (TokenEquals(command, u"generate_record")) {
        if (tokens.size() < 4) {
            SendBCMDResult(socket, userDatabaseId, u"//olympiad generate_record point win_count match_count");
        } else {
            CDX_WOLYMPIAD_GENERATE_RECORD_FOR_TEST request(
                GetTokenAsInt(tokens, 1, 2000), GetTokenAsInt(tokens, 2, 100), GetTokenAsInt(tokens, 3, 100)
            );
            db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x5c), static_cast<int>(sizeof(request)), request);
            SendBCMDResult(socket, userDatabaseId, u"[olympiad generate_record] accepted");
        }
    } else if (TokenEquals(command, u"record")) {
        const std::vector<std::u16string> callbackTokens(tokens);
        AtomicLambdaJobDx* const job = MakeLambdaJobDx([key, callbackTokens](AtomicLambdaJobDx* const callbackJob) {
            auto* const record = static_cast<CDX_WOLYMPIAD_RECORD*>(callbackJob->m_pDx);
            if (record == nullptr) {
                return;
            }
            record->nPrevRank = GetTokenAsInt(callbackTokens, 1, record->nPrevRank);
            record->nPrevRankCount = GetTokenAsInt(callbackTokens, 2, record->nPrevRankCount);
            record->nPrevClassRank = GetTokenAsInt(callbackTokens, 3, record->nPrevClassRank);
            record->nPrevClassRankCount = GetTokenAsInt(callbackTokens, 4, record->nPrevClassRankCount);
            record->nPrevClassRankByServer = GetTokenAsInt(callbackTokens, 5, record->nPrevClassRankByServer);
            record->nPrevClassRankByServerCount = GetTokenAsInt(callbackTokens, 6, record->nPrevClassRankByServerCount);
            WOlympiadUsers::inst().SendOlympiadRecordTo(key, record);
        });
        CDX_WOLYMPIAD_RECORD request(socket->m_idWorld, userDatabaseId, WOlympiadService::inst().GetSeason() - 1, job);
        db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x5b), static_cast<int>(sizeof(request)), request);
        SendBCMDResult(socket, userDatabaseId, u"[olympiad record] accepted");
    }
    return false;
}

bool Handle_OCQ_STOP(WOlympiadSocketPeer* const socket, const std::uint8_t* const source) {
    // Current L2Server 0x008ed8e8.
    L2_GUARD_FUNCTION();
    olympiad::OCQ_STOP packet{};
    if (!DecodeOcsPacket(packet, source)) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OCQ_STOP recv: %d, %d, %d",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0xe7,
    //     socket->m_idWorld,
    //     packet.nUserDBID,
    //     packet.bByDisconnect ? 1 : 0)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OCQ_STOP recv: %d, %d, %d",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0xe7,
    //     socket->m_idWorld,
    //     packet.nUserDBID,
    //     packet.bByDisconnect ? 1 : 0)
    L2_LOG_INFO(
        Log, "Received Olympiad stop request from world %d, character DB ID %d: caused by disconnect %s.", socket->m_idWorld,
        packet.nUserDBID, packet.bByDisconnect ? "yes" : "no"
    );
    const std::CDualKey<int, int> key = OcsUserKey(socket, packet.nUserDBID);
    if (packet.bByDisconnect) {
        WOlympiadService::inst().DisconnectMember(key);
    } else {
        WOlympiadService::inst().StopMatchMaking(key);
    }
    return false;
}

bool Handle_OCQ_OBSERVE(WOlympiadSocketPeer* const socket, const std::uint8_t* const source) {
    // Current L2Server 0x008edb18.
    L2_GUARD_FUNCTION();
    olympiad::OCQ_OBSERVE packet{};
    if (!DecodeOcsPacket(packet, source)) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OCQ_OBSERVE recv: %d, %d, %d, %d",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0x433,
    //     socket->m_idWorld,
    //     packet.nUserDBID,
    //     packet.nFieldID,
    //     packet.nAccountID)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OCQ_OBSERVE recv: %d, %d, %d, %d",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0x433,
    //     socket->m_idWorld,
    //     packet.nUserDBID,
    //     packet.nFieldID,
    //     packet.nAccountID)
    L2_LOG_INFO(
        Log, "Received Olympiad observation request from world %d, character DB ID %d: field %d, account %u.",
        socket->m_idWorld, packet.nUserDBID, packet.nFieldID, static_cast<unsigned int>(packet.nAccountID)
    );
    const std::CDualKey<int, int> key = OcsUserKey(socket, packet.nUserDBID);
    if (!WOlympiadService::inst().ObserveMatch(key, packet.nFieldID, static_cast<std::uint32_t>(packet.nAccountID))) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Observing is failed: %d, %d, %d, %d",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x43d,
        //     socket->m_idWorld,
        //     packet.nUserDBID,
        //     packet.nFieldID,
        //     packet.nAccountID)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Observing is failed: %d, %d, %d, %d",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x43d,
        //     socket->m_idWorld,
        //     packet.nUserDBID,
        //     packet.nFieldID,
        //     packet.nAccountID)
        L2_LOG_WARN(
            Log,
            "Olympiad observation request rejected for world %d, character DB ID %d, field %d; sending failure response.",
            socket->m_idWorld, packet.nUserDBID, packet.nFieldID
        );
        WOlympiadServiceUtil::SendOSA_OBSERVE(key, ObserveResult::Failed);
        return false;
    }
    WOlympiadServiceUtil::SendOSA_OBSERVE(key, ObserveResult::Success);
    if (!WOlympiadServiceUtil::SendOSN_CONNECT(key)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x448)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x448)
        L2_LOG_ERROR(
            Log, "Cannot send Olympiad observer connection notification to world %d, character DB ID %d after registration.",
            socket->m_idWorld, packet.nUserDBID
        );
    }
    return false;
}

bool Handle_OCQ_MATCH_LIST(WOlympiadSocketPeer* const socket, const std::uint8_t* const source) {
    // Current L2Server 0x008edce8.
    L2_GUARD_FUNCTION();
    olympiad::OCQ_MATCH_LIST packet{};
    if (!DecodeOcsPacket(packet, source)) {
        return true;
    }
    WOlympiadService::inst().SendGameList(OcsUserKey(socket, packet.nUserDBID));
    return false;
}

bool Handle_OCQ_HERO_LIST(WOlympiadSocketPeer* const socket, const std::uint8_t* const source) {
    // Current L2Server 0x008eddcc.
    L2_GUARD_FUNCTION();
    olympiad::OCQ_HERO_LIST packet{};
    if (!DecodeOcsPacket(packet, source)) {
        return true;
    }
    WHeroSystem::inst().SendHeroList(OcsUserKey(socket, packet.nUserDBID));
    return false;
}

bool Handle_OCQ_HERO_MATCH_RESULT(WOlympiadSocketPeer* const socket, const std::uint8_t* const source) {
    // Current L2Server 0x008edeb0.
    L2_GUARD_FUNCTION();
    olympiad::OCQ_HERO_MATCH_RESULT packet{};
    if (!DecodeOcsPacket(packet, source)) {
        return true;
    }
    WHeroSystem::inst().SendHeroMatchResult(OcsUserKey(socket, packet.nUserDBID), packet.nClassType, packet.nPage);
    return false;
}

bool Handle_OCA_PREPARE(WOlympiadSocketPeer* const socket, const std::uint8_t* const source) {
    // Current L2Server 0x008edfa0.
    L2_GUARD_FUNCTION();
    olympiad::OCA_PREPARE packet{};
    if (!DecodeOcsPacket(packet, source)) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OCA_PREPARE recv: %d, %d, %d",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0x51c,
    //     socket->m_idWorld,
    //     packet.nUserDBID,
    //     packet.nGameValidCondition)
    // L2_LOG_ERROR(Log,
    //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR][WORLD_OLYMPIAD]"
    //     L"[PACKET]OCA_PREPARE recv: %d, %d, %d",
    //     L"OlympiadPacketHandlerInHost.cpp",
    //     0x51c,
    //     socket->m_idWorld,
    //     packet.nUserDBID,
    //     packet.nGameValidCondition)
    L2_LOG_INFO(
        Log, "Received Olympiad preparation response from world %d, character DB ID %d: condition %d.", socket->m_idWorld,
        packet.nUserDBID, packet.nGameValidCondition
    );
    WOlympiadService::inst().SetPrepareValidCondition(
        OcsUserKey(socket, packet.nUserDBID), static_cast<GameValidCondition>(packet.nGameValidCondition)
    );
    return false;
}

bool Handle_OCN_LAST_MATCH_RESULT(WOlympiadSocketPeer* const socket, const std::uint8_t* const source) {
    // Current L2Server 0x008ee0d8.
    L2_GUARD_FUNCTION();
    olympiad::OCN_LAST_MATCH_RESULT packet{};
    if (!DecodeOcsPacket(packet, source)) {
        return true;
    }
    WOlympiadService::inst().NotifyLastMatchResult(OcsUserKey(socket, packet.nUserDBID));
    return false;
}

bool Handle_OCN_NOBLESS_INFO(WOlympiadSocketPeer* const socket, const std::uint8_t* const source) {
    // Current L2Server 0x008f3f30.
    L2_GUARD_FUNCTION();
    olympiad::OCN_NOBLESS_INFO packet{};
    if (!DecodeOcsPacket(packet, source)) {
        return true;
    }
    const std::CDualKey<int, int> key = OcsUserKey(socket, packet.nUserDBID);
    if (!WOlympiadUsers::inst().IsRegistered(key)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Not nobless: %d, %d",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x484,
        //     key.m_PrimaryKey,
        //     key.m_SecondaryKey)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Not nobless: %d, %d",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x484,
        //     key.m_PrimaryKey,
        //     key.m_SecondaryKey)
        L2_LOG_WARN(
            Log, "Cannot send Olympiad nobless information for world %d, character DB ID %d: user is not registered.",
            key.m_PrimaryKey, key.m_SecondaryKey
        );
        return false;
    }
    const SharedNoblessInfo* const info = WOlympiadUsers::inst().GetNoblessInfo(key);
    if (!info) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x48c)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x48c)
        L2_LOG_ERROR(
            Log,
            "Cannot send Olympiad nobless information for world %d, character DB ID %d: registered user data is "
            "unavailable.",
            key.m_PrimaryKey, key.m_SecondaryKey
        );
        return false;
    }
    olympiad::OSN_NOBLESS_INFO response{};
    WOlympiadStuff::ConvertNoblessInfo(info, &response);
    response.bNewSeason = false;
    socket->SendPacket(OSN_NOBLESS_INFO, response);
    return false;
}

bool Handle_OCQ_MATCH_COUNT(WOlympiadSocketPeer* const socket, const std::uint8_t* const source) {
    // Current L2Server 0x008f40d4.
    L2_GUARD_FUNCTION();
    olympiad::OCQ_MATCH_COUNT packet{};
    if (!DecodeOcsPacket(packet, source)) {
        return true;
    }
    WOlympiadUsers::inst().SendMatchCount(
        OcsUserKey(socket, packet.nUserDBID), std::CDualKey<int, int>(packet.nTargetWorldID, packet.nTargetUserDBID)
    );
    return false;
}

bool Handle_OCQ_DEBUG_STRING(WOlympiadSocketPeer* const socket, const std::uint8_t* const source) {
    // Current L2Server 0x008f0344.
    L2_GUARD_FUNCTION();
    olympiad::OCQ_DEBUG_STRING packet{};
    if (!DecodeOcsPacket(packet, source)) {
        return true;
    }
    olympiad::OSA_DEBUG_STRING response{};
    response.nUserDBID = packet.nUserDBID;
    WOlympiadService::inst().GetDebugString(response.sString);
    socket->SendPacket(OSA_DEBUG_STRING, response);
    return false;
}

bool Handle_OCQ_DELETE_TRADE_POINT(WOlympiadSocketPeer* const socket, const std::uint8_t* const source) {
    // Current L2Server 0x008f0694.
    L2_GUARD_FUNCTION();
    olympiad::OCQ_DELETE_TRADE_POINT packet{};
    if (!DecodeOcsPacket(packet, source)) {
        return true;
    }
    CDX_WOLYMPIAD_DELETE_TRADE_POINT request(
        socket->m_idWorld, packet.nUserDBID, packet.sCharName.c_str(), packet.nAccountID, packet.sAccountName.c_str(),
        packet.nTradePoint, packet.nNPCSID
    );
    db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x55), static_cast<int>(sizeof(request)), request);
    return false;
}

bool Handle_OCN_UPDATE_PLEDGE_INFO(WOlympiadSocketPeer* const socket, const std::uint8_t* const source) {
    // Current L2Server 0x008f1470.
    L2_GUARD_FUNCTION();
    olympiad::OCN_UPDATE_PLEDGE_INFO packet{};
    if (!DecodeOcsPacket(packet, source)) {
        return true;
    }

    CSPointer<CPledge> pledge = CPledge::FindPledge(socket->m_idWorld, packet.nPledgeDBID);
    if (!pledge) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x568)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"OlympiadPacketHandlerInHost.cpp",
        //     0x568)
        L2_LOG_WARN(
            Log, "Cannot update Olympiad clan information for world %d, clan DB ID %d: clan was not found.",
            socket->m_idWorld, packet.nPledgeDBID
        );
        return false;
    }

    pledge->SetNewCrestId(static_cast<std::uint32_t>(packet.nCrestDBID));
    SetCrest crest(static_cast<std::uint32_t>(packet.nCrestDBID));
    pledge->ServeForAllMember(crest, false);
    UpdatePledgeInfoWindow update(pledge.get());
    pledge->ServeForAllMember(update, false);
    SetParamChanged changed(PCB_PLEDGE);
    pledge->ServeForAllMember(changed, false);
    return false;
}

bool Handle_OCQ_RESET_OLYMPIAD_POINT(WOlympiadSocketPeer* const socket, const std::uint8_t* const source) {
    // Current L2Server 0x008ee1bc.
    L2_GUARD_FUNCTION();
    olympiad::OCQ_RESET_OLYMPIAD_POINT packet{};
    if (!DecodeOcsPacket(packet, source)) {
        return true;
    }
    CDX_WOLYMPIAD_RESET_POINT request(socket->m_idWorld, packet.nUserDBID, packet.nPoint);
    db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x57), static_cast<int>(sizeof(request)), request);
    return false;
}

bool Handle_OCQ_OLYMPIAD_RECORD(WOlympiadSocketPeer* const socket, const std::uint8_t* const source) {
    // Current L2Server 0x008efb64.
    L2_GUARD_FUNCTION();
    olympiad::OCQ_OLYMPIAD_RECORD packet{};
    if (!DecodeOcsPacket(packet, source)) {
        return true;
    }

    const std::CDualKey<int, int> key(socket->m_idWorld, packet.nUserDBID);
    AtomicLambdaJobDx* const job = MakeLambdaJobDx([key](AtomicLambdaJobDx* const callbackJob) {
        auto* const record = static_cast<CDX_WOLYMPIAD_RECORD*>(callbackJob->m_pDx);
        WOlympiadUsers::inst().SendOlympiadRecordTo(key, record);
    });
    CDX_WOLYMPIAD_RECORD request(socket->m_idWorld, packet.nUserDBID, WOlympiadService::inst().GetSeason() - 1, job);
    db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x5b), static_cast<int>(sizeof(request)), request);
    return false;
}

bool Handle_OCQ_CLASS_RANK_BY_SERVER(WOlympiadSocketPeer* const socket, const std::uint8_t* const source) {
    // Current L2Server 0x008efd5c.
    L2_GUARD_FUNCTION();
    olympiad::OCQ_CLASS_RANK_BY_SERVER packet{};
    if (!DecodeOcsPacket(packet, source)) {
        return true;
    }

    for (int index = 0; index < OlympiadDefine::HERO_CLASS_COUNT; ++index) {
        const int heroClassType = static_cast<int>(OlympiadDefine::GetHeroClassType(index));
        Send_CLASS_RANK_BY_SERVER_TOP10(
            socket->m_idWorld, heroClassType, runtime::SubtractInt32Wrapping(WOlympiadService::inst().GetSeason(), 1)
        );
    }
    return false;
}

OCSCallbackMap g_OCSPacketHandlerTable{DummyOCSPacket, u""};

namespace {

L2_BIND_PACKET_TOKEN(g_OCSPacketHandlerTable, OCQ_VERSION, Handle_OCQ_VERSION);
L2_BIND_PACKET_TOKEN(g_OCSPacketHandlerTable, OCQ_REGISTER, Handle_OCQ_REGISTER);
L2_BIND_PACKET_TOKEN(g_OCSPacketHandlerTable, OCQ_START, Handle_OCQ_START);
L2_BIND_PACKET_TOKEN(g_OCSPacketHandlerTable, OCQ_STOP, Handle_OCQ_STOP);
L2_BIND_PACKET_TOKEN(g_OCSPacketHandlerTable, OCQ_CONNECT, Handle_OCQ_CONNECT);
L2_BIND_PACKET_TOKEN(g_OCSPacketHandlerTable, OCA_RETURN, Handle_OCA_RETURN);
L2_BIND_PACKET_TOKEN(g_OCSPacketHandlerTable, OCA_RESTART, Handle_OCA_RESTART);
L2_BIND_PACKET_TOKEN(g_OCSPacketHandlerTable, OCQ_OBSERVE, Handle_OCQ_OBSERVE);
L2_BIND_PACKET_TOKEN(g_OCSPacketHandlerTable, OCQ_MATCH_LIST, Handle_OCQ_MATCH_LIST);
L2_BIND_PACKET_TOKEN(g_OCSPacketHandlerTable, OCQ_HERO_LIST, Handle_OCQ_HERO_LIST);
L2_BIND_PACKET_TOKEN(g_OCSPacketHandlerTable, OCQ_HERO_MATCH_RESULT, Handle_OCQ_HERO_MATCH_RESULT);
L2_BIND_PACKET_TOKEN(g_OCSPacketHandlerTable, OCN_NOBLESS_INFO, Handle_OCN_NOBLESS_INFO);
L2_BIND_PACKET_TOKEN(g_OCSPacketHandlerTable, OCQ_MATCH_COUNT, Handle_OCQ_MATCH_COUNT);
L2_BIND_PACKET_TOKEN(g_OCSPacketHandlerTable, OCQ_DEBUG_STRING, Handle_OCQ_DEBUG_STRING);
L2_BIND_PACKET_TOKEN(g_OCSPacketHandlerTable, OCQ_SET_HERO_TYPE, Handle_OCQ_SET_HERO_TYPE);
L2_BIND_PACKET_TOKEN(g_OCSPacketHandlerTable, OCQ_BCMD, Handle_OCQ_BCMD);
L2_BIND_PACKET_TOKEN(g_OCSPacketHandlerTable, OCA_PREPARE, Handle_OCA_PREPARE);
L2_BIND_PACKET_TOKEN(g_OCSPacketHandlerTable, OCQ_DELETE_TRADE_POINT, Handle_OCQ_DELETE_TRADE_POINT);
L2_BIND_PACKET_TOKEN(g_OCSPacketHandlerTable, OCN_LAST_MATCH_RESULT, Handle_OCN_LAST_MATCH_RESULT);
L2_BIND_PACKET_TOKEN(g_OCSPacketHandlerTable, OCN_UPDATE_PLEDGE_INFO, Handle_OCN_UPDATE_PLEDGE_INFO);
L2_BIND_PACKET_TOKEN(g_OCSPacketHandlerTable, OCQ_RESET_OLYMPIAD_POINT, Handle_OCQ_RESET_OLYMPIAD_POINT);
L2_BIND_PACKET_TOKEN(g_OCSPacketHandlerTable, OCQ_OLYMPIAD_RECORD, Handle_OCQ_OLYMPIAD_RECORD);
L2_BIND_PACKET_TOKEN(
    g_OCSPacketHandlerTable, OCQ_PLEDGE_RAID_GET_SEASON, PledgeRaidPacketHandler::Handle_OCQ_PLEDGE_RAID_GET_SEASON
);
L2_BIND_PACKET_TOKEN(
    g_OCSPacketHandlerTable, OCN_PLEDGE_RAID_RESET_SEASON, PledgeRaidPacketHandler::Handle_OCN_PLEDGE_RAID_RESET_SEASON
);
L2_BIND_PACKET_TOKEN(
    g_OCSPacketHandlerTable, OCN_PLEDGE_RAID_DECIDE, PledgeRaidPacketHandler::Handle_OCN_PLEDGE_RAID_DECIDE
);
L2_BIND_PACKET_TOKEN(
    g_OCSPacketHandlerTable, OCQ_PLEDGE_RAID_SET_INFO, PledgeRaidPacketHandler::Handle_OCQ_PLEDGE_RAID_SET_INFO
);
L2_BIND_PACKET_TOKEN(
    g_OCSPacketHandlerTable, OCQ_PLEDGE_RAID_GET_INFO, PledgeRaidPacketHandler::Handle_OCQ_PLEDGE_RAID_GET_INFO
);
L2_BIND_PACKET_TOKEN(
    g_OCSPacketHandlerTable,
    OCQ_PLEDGE_RAID_GET_INFO_FOR_BUILDER,
    PledgeRaidPacketHandler::Handle_OCQ_PLEDGE_RAID_GET_INFO_FOR_BUILDER
);
L2_BIND_PACKET_TOKEN(
    g_OCSPacketHandlerTable, OCQ_PLEDGE_RAID_GET_INFO_LIST, PledgeRaidPacketHandler::Handle_OCQ_PLEDGE_RAID_GET_INFO_LIST
);
L2_BIND_PACKET_TOKEN(
    g_OCSPacketHandlerTable,
    OCQ_PLEDGE_RAID_GET_INFO_LIST_FOR_BUILDER,
    PledgeRaidPacketHandler::Handle_OCQ_PLEDGE_RAID_GET_INFO_LIST_FOR_BUILDER
);
L2_BIND_PACKET_TOKEN(
    g_OCSPacketHandlerTable,
    OCQ_PLEDGE_RAID_GET_PREV_SEASON_INFO_LIST,
    PledgeRaidPacketHandler::Handle_OCQ_PLEDGE_RAID_GET_PREV_SEASON_INFO_LIST
);
L2_BIND_PACKET_TOKEN(g_OCSPacketHandlerTable, OCQ_CLASS_RANK_BY_SERVER, Handle_OCQ_CLASS_RANK_BY_SERVER);

} // namespace
