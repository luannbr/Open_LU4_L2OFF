#include "DB.h"
#include "L2Packet.h"
#include "PledgeContribution.h"
#include "PledgeItem.h"
#include "PledgeLevel.h"
#include "PledgeMastery.h"
#include "PledgeSkill.h"
#include "PledgeStuff.h"
#include "Protocol/src/SystemMessageIds.h"
#include "User.h"
#include "UserPacketEx.h"
#include "UserSocket.h"

#include "Shared/src/AtomicJob.h"
#include "Shared/src/Log.h"
#include "Shared/src/PledgeAnnounce.h"
#include "Shared/src/PledgeAnnounceRegistry.h"
#include "Shared/src/proto/DBPacket.h"

#include <cstdint>
#include <string>

namespace {

void Send_S_EX_PLEDGE_ANNOUNCE_SET(User* const user, const bool success) {
    // Current L2Server 0x009bfcb8.
    L2_GUARD_FUNCTION();
    protocol::S_EX_PLEDGE_ANNOUNCE_SET packet{};
    packet.bSuccess = success;
    PledgeStuff::SendPacket(user, S_EX_PLEDGE_ANNOUNCE_SET, packet);
}

void Send_S_EX_PLEDGE_ANNOUNCE(User* const user, const std::u16string& content, const bool show) {
    // Current L2Server 0x009bfff8.
    L2_GUARD_FUNCTION();
    protocol::S_EX_PLEDGE_ANNOUNCE packet{};
    packet.content = content;
    packet.bShow = show;
    PledgeStuff::SendPacket(user, S_EX_PLEDGE_ANNOUNCE, packet);
}

} // namespace

template <>
bool UserPacketHandle(CUserSocket* socket, const protocol::C_EX_PLEDGE_CONTRIBUTION_INFO&) {
    PledgeContributionSystem::inst().RetrieveInfo(socket->GetUser());
    return false;
}

template <>
bool UserPacketHandle(CUserSocket* socket, const protocol::C_EX_PLEDGE_ITEM_LIST&) {
    PledgeItemStuff::SendPledgeItemListTo(socket->GetUser());
    return false;
}

template <>
bool UserPacketHandle(CUserSocket* socket, const protocol::C_EX_PLEDGE_ITEM_INFO& packet) {
    PledgeItemStuff::SendPledgeItemInfoTo(socket->GetUser(), packet.nItemClassID);
    return false;
}

template <>
bool UserPacketHandle(CUserSocket* socket, const protocol::C_EX_PLEDGE_ITEM_BUY& packet) {
    PledgeItemStuff::RequestBuyPledgeItem(
        socket->GetUser(), packet.nItemClassID, static_cast<std::int64_t>(packet.nItemAmount)
    );
    return false;
}

template <>
bool UserPacketHandle(CUserSocket* socket, const protocol::C_EX_PLEDGE_ITEM_ACTIVATE& packet) {
    PledgeItemStuff::RequestAddPledgeItem(socket->GetUser(), packet.nItemClassId);
    return false;
}

template <>
bool UserPacketHandle(CUserSocket* socket, const protocol::C_EX_PLEDGE_ANNOUNCE&) {
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return false;
    }
    const unsigned int pledgeId = user->m_data->m_nPledgeID;
    if (pledgeId == 0) {
        return false;
    }

    CPledgeAnnounceSP announcement = CPledgeAnnounceRegistry::inst().Get(pledgeId);
    if (announcement) {
        Send_S_EX_PLEDGE_ANNOUNCE(user, announcement->GetContent(), announcement->DoesShow());
        return false;
    }

    protocol::RQ_EX_PLEDGE_ANNOUNCE request;
    request.nPledgeDbId_ = static_cast<int>(pledgeId);
    const std::uint32_t requesterSid = user->Id();
    const int requesterDatabaseId = static_cast<int>(user->GetDBID());
    const int requesterWorldId = user->m_data->GetWorldId();
    const UserCapture capture{requesterSid, requesterWorldId, requesterDatabaseId};
    AtomicLambdaJobPacket* const job = MakeLambdaJobPacket([capture, pledgeId](AtomicLambdaJobPacket* const callback) {
        // Current L2Server 0x009c0554.
        static constexpr wchar_t functionName[] = L"PledgePacketHandler.cpp(303)";
        L2_GUARD_PROFILE(functionName);
        GuardHelper stackGuard(functionName, CThreadManager::GetThreadID());
        auto* const response = static_cast<protocol::RP_EX_PLEDGE_ANNOUNCE*>(callback->m_pPacket);
        if (response == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]assert falied: %s",
            //     L"PledgePacketHandler.cpp",
            //     0x131,
            //     L"(pPacket != NULL)")
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]assert falied: %s",
            //     L"PledgePacketHandler.cpp",
            //     0x131,
            //     L"(pPacket != NULL)")
            L2_LOG_ERROR(
                Log,
                "Clan %u announcement lookup returned no response for character %u; no announcement is registered or sent "
                "by this callback.",
                static_cast<unsigned>(pledgeId), static_cast<unsigned>(capture.m_dbid)
            );
            return;
        }
        User* const requester = capture.GetUser();
        if (requester == nullptr) {
            return;
        }

        CPledgeAnnounceSP loaded(
            new CPledgeAnnounce(pledgeId, response->bShow, response->content.c_str()), ObjectRefType::FunctionChain
        );
        CPledgeAnnounceRegistry::inst().Register(loaded);
        Send_S_EX_PLEDGE_ANNOUNCE(requester, response->content, response->bShow);
    });
    request.nJobKey = CDB::PushAtomicJobHelper(job);
    db.SendPacket(
        requesterWorldId, static_cast<RQ_PROTOCOL>(0xea), static_cast<RQ_EX_PROTOCOL>(RQ_EX_PLEDGE_ANNOUNCE), request
    );
    return false;
}

template <>
bool UserPacketHandle(CUserSocket* socket, const protocol::C_EX_PLEDGE_ANNOUNCE_SET& packet) {
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return false;
    }
    const unsigned int pledgeId = user->m_data->m_nPledgeID;
    if (pledgeId == 0 || !user->IsPledgeMaster()) {
        user->SendSystemMessage(SystemMessageId::ONLY_CLAN_LEADER_CAN_ACT);
        Send_S_EX_PLEDGE_ANNOUNCE_SET(user, false);
        return false;
    }

    protocol::RQ_EX_PLEDGE_ANNOUNCE_SET request;
    request.nPledgeDBID = static_cast<int>(pledgeId);
    request.content = packet.content;
    request.bShow = packet.bShow;
    const std::uint32_t requesterSid = user->Id();
    const int requesterDatabaseId = static_cast<int>(user->GetDBID());
    const int requesterWorldId = user->m_data->GetWorldId();
    const UserCapture capture{requesterSid, requesterWorldId, requesterDatabaseId};
    AtomicLambdaJobPacket* const job = MakeLambdaJobPacket([capture, pledgeId](AtomicLambdaJobPacket* const callback) {
        // Current L2Server 0x009c06f0.
        static constexpr wchar_t functionName[] = L"PledgePacketHandler.cpp(367)";
        L2_GUARD_PROFILE(functionName);
        GuardHelper stackGuard(functionName, CThreadManager::GetThreadID());
        auto* const response = static_cast<protocol::RP_EX_PLEDGE_ANNOUNCE_SET*>(callback->m_pPacket);
        if (response == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]assert falied: %s",
            //     L"PledgePacketHandler.cpp",
            //     0x171,
            //     L"(pPacket != NULL)")
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]assert falied: %s",
            //     L"PledgePacketHandler.cpp",
            //     0x171,
            //     L"(pPacket != NULL)")
            L2_LOG_ERROR(
                Log,
                "Clan %u announcement update returned no response for character %u; this callback does not update the local "
                "announcement or confirm the change.",
                static_cast<unsigned>(pledgeId), static_cast<unsigned>(capture.m_dbid)
            );
            return;
        }
        User* const requester = capture.GetUser();
        if (requester == nullptr) {
            return;
        }

        CPledgeAnnounceSP updated(
            new CPledgeAnnounce(pledgeId, response->bShow, response->content.c_str()), ObjectRefType::FunctionChain
        );
        CPledgeAnnounceRegistry::inst().Register(updated);
        Send_S_EX_PLEDGE_ANNOUNCE_SET(requester, true);
        Send_S_EX_PLEDGE_ANNOUNCE(requester, response->content, response->bShow);
    });
    request.nJobKey = CDB::PushAtomicJobHelper(job);
    db.SendPacket(
        requesterWorldId, static_cast<RQ_PROTOCOL>(0xea), static_cast<RQ_EX_PROTOCOL>(RQ_EX_PLEDGE_ANNOUNCE_SET), request
    );
    return false;
}

template <>
bool UserPacketHandle(CUserSocket* socket, const protocol::C_EX_PLEDGE_CONTRIBUTION_RANK& packet) {
    PledgeContributionSystem::inst().RetrieveRank(socket->GetUser(), packet.bCurrent);
    return false;
}

template <>
bool UserPacketHandle(CUserSocket* socket, const protocol::C_EX_PLEDGE_CONTRIBUTION_REWARD&) {
    PledgeContributionSystem::inst().TakeTotalReward(socket->GetUser());
    return false;
}

template <>
bool UserPacketHandle(CUserSocket* socket, const protocol::C_EX_PLEDGE_LEVEL_UP&) {
    User* const user = socket->GetUser();
    if (user != nullptr) {
        PledgeLevelStuff::RequestPledgeLevelUp(user);
    }
    return false;
}

template <>
bool UserPacketHandle(CUserSocket* socket, const protocol::C_EX_PLEDGE_MASTERY_INFO&) {
    PledgeMasteryStuff::SendPledgeMasteryInfoTo(socket->GetUser());
    return false;
}

template <>
bool UserPacketHandle(CUserSocket* socket, const protocol::C_EX_PLEDGE_MASTERY_SET& packet) {
    L2_GUARD_FUNCTION();
    User* const user = socket->GetUser();
    if (user != nullptr && PledgeStuff::IsValidMasteryID(static_cast<PledgeMasteryID::Type>(packet.nID))) {
        PledgeMasteryStuff::RequestSetPoint(user, static_cast<PledgeMasteryID::Type>(packet.nID));
    }
    return false;
}

template <>
bool UserPacketHandle(CUserSocket* socket, const protocol::C_EX_PLEDGE_MASTERY_RESET&) {
    PledgeMasteryStuff::RequestResetPoint(socket->GetUser());
    return false;
}

template <>
bool UserPacketHandle(CUserSocket* socket, const protocol::C_EX_PLEDGE_SKILL_INFO& packet) {
    PledgeSkillStuff::SendPledgeSkillInfoTo(socket->GetUser(), packet.nSkillID, packet.nSkillLevel);
    return false;
}

template <>
bool UserPacketHandle(CUserSocket* socket, const protocol::C_EX_PLEDGE_SKILL_ACTIVATE& packet) {
    PledgeSkillStuff::RequestToAddPledgeSkill(socket->GetUser(), packet.nSkillID, packet.nSkillLevel);
    return false;
}
