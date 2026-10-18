#include "OlympiadPacketHandlers.h"

#include "CConfig.h"
#include "DB.h"
#include "L2Packet.h"
#include "OlympiadNetwork.h"
#include "Pledge.h"
#include "PledgeRaid.h"
#include "PledgeRaidInfo.h"
#include "PledgeRaidNetwork.h"
#include "PledgeStuff.h"
#include "User.h"
#include "UserPacketEx.h"
#include "UserSocket.h"

#include "Shared/src/AtomicJob.h"
#include "Shared/src/DXPacket.h"
#include "Shared/src/DateTime.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/proto/DBPacket.h"
#include "Shared/src/proto/OlympiadPacket.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwchar>
#include <string>

class PledgeRaidPacketHandlerAccess {
public:
    static int Season(const CDX_PLEDGE_RAID_GET_INFO& packet) { return packet.nSeason_; }
    static int PledgeDatabaseId(const CDX_PLEDGE_RAID_GET_INFO& packet) { return packet.nPledgeDbId_; }
    static const char16_t* PledgeName(const CDX_PLEDGE_RAID_GET_INFO& packet) { return packet.szPledgeName_; }
    static int Rank(const CDX_PLEDGE_RAID_GET_INFO& packet) { return packet.nRank_; }
    static std::int64_t Point(const CDX_PLEDGE_RAID_GET_INFO& packet) { return packet.nPoint_; }
    static int RewardReason(const CDX_PLEDGE_RAID_GET_INFO& packet) { return packet.nRewardReason_; }
    static int TotalCount(const CDX_PLEDGE_RAID_GET_INFO& packet) { return packet.nTotalCount_; }

    static int Year(const L2DateTime& value) { return value.m_nYear; }
    static int Month(const L2DateTime& value) { return value.m_nMonth; }
    static int Day(const L2DateTime& value) { return value.m_nDay; }
};

namespace {

template <std::size_t DestinationCount, std::size_t SourceCount>
void CopyTargetName(char16_t (&destination)[DestinationCount], const char16_t (&source)[SourceCount]) {
    std::fill(std::begin(destination), std::end(destination), u'\0');
    const std::size_t count = std::min(DestinationCount - 1, SourceCount);
    std::copy_n(source, count, destination);
}

template <std::size_t DestinationCount>
void CopyTargetName(char16_t (&destination)[DestinationCount], const char16_t* const source) {
    std::fill(std::begin(destination), std::end(destination), u'\0');
    if (source == nullptr) {
        return;
    }
    std::size_t count = 0;
    while (count + 1 < DestinationCount && source[count] != u'\0') {
        destination[count] = source[count];
        ++count;
    }
}

std::u16string ToTargetString(const char16_t* const value) {
    if (value == nullptr) {
        return std::u16string();
    }
    return std::u16string(value);
}

int MaximumRankReward() {
    int maximum = 1;
    for (const auto& reward : PledgeRaidDB::inst().mRankRewardInfo_) {
        maximum = std::max(maximum, reward.first);
    }
    return maximum;
}

CSPointer<User> FindUser(const int userDatabaseId) {
    return g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, userDatabaseId));
}

void SendPacket(const User* const user, const S_EX_PROTOCOL protocol, network::IPacket& packet) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        user->GetSocket()->Send(S_EX, protocol, packet);
    }
}

void ConvertTo(const protocol::PkPledgeRaidInfoData& source, olympiad::PkOlympiadPledgeRaidInfoData& destination) {
    destination.nWorldId_ = source.nWorldId_;
    destination.nPledgeDbId_ = source.nPledgeDbId_;
    CopyTargetName(destination.szPledgeName_, source.szPledgeName_.data());
    destination.nRank_ = source.nRank_;
    destination.nPoint_ = source.nPoint_;
}

} // namespace

namespace PledgeRaidPacketHandler {

bool Handle_OSA_PLEDGE_RAID_GET_SEASON(WOlympiadSocketHost* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    return PledgeRaidSystem::inst().Process_OSA_PLEDGE_RAID_GET_SEASON(socket, packet);
}

bool Handle_OSA_PLEDGE_RAID_SET_INFO(WOlympiadSocketHost* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    return PledgeRaidSystem::inst().Process_OSA_PLEDGE_RAID_SET_INFO(socket, packet);
}

bool Handle_OSA_PLEDGE_RAID_GET_INFO(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSA_PLEDGE_RAID_GET_INFO packet{};
    if (!PledgeRaidNetworkUtil::DecodePacket(packet, source)) {
        return true;
    }

    CSPointer<User> user = FindUser(packet.nUserDbId_);
    if (!user) {
        return false;
    }

    protocol::S_EX_PLEDGE_RAID_INFO response{};
    response.bIsCurrent_ = packet.bIsCurrent_;
    if (CPledge* const pledge = user->GetPledge()) {
        CopyTargetName(response.szPledgeName_, pledge->GetPledgeName());
    } else {
        CopyTargetName(response.szPledgeName_, packet.szPledgeName_);
    }
    response.nRank_ = packet.nRank_;
    response.nTier_ = PledgeRaidSystem::inst().CalculateTier(packet.nRank_, MaximumRankReward(), packet.nTotalCount_);
    response.nPoint_ = packet.nPoint_;
    response.nRewardReason_ = packet.nRewardReason_;
    response.nSeason_ = packet.nSeason_;

    const L2DateTime start = L2DateTime::FromTime_t(packet.tStartTime_);
    const L2DateTime end = L2DateTime::FromTime_t(packet.tEndTime_);
    response.hStartTimeYear_ = static_cast<std::int16_t>(PledgeRaidPacketHandlerAccess::Year(start));
    response.hStartTimeMonth_ = static_cast<std::int16_t>(PledgeRaidPacketHandlerAccess::Month(start));
    response.hStartTimeDay_ = static_cast<std::int16_t>(PledgeRaidPacketHandlerAccess::Day(start));
    response.hEndTimeYear_ = static_cast<std::int16_t>(PledgeRaidPacketHandlerAccess::Year(end));
    response.hEndTimeMonth_ = static_cast<std::int16_t>(PledgeRaidPacketHandlerAccess::Month(end));
    response.hEndTimeDay_ = static_cast<std::int16_t>(PledgeRaidPacketHandlerAccess::Day(end));
    SendPacket(user.get(), S_EX_PLEDGE_RAID_INFO, response);
    return false;
}

bool Handle_OSA_PLEDGE_RAID_GET_INFO_FOR_BUILDER(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSA_PLEDGE_RAID_GET_INFO packet{};
    if (!PledgeRaidNetworkUtil::DecodePacket(packet, source)) {
        return true;
    }
    CSPointer<User> user = FindUser(packet.nUserDbId_);
    if (!user) {
        return false;
    }

    const int tier = PledgeRaidSystem::inst().CalculateTier(packet.nRank_, MaximumRankReward(), packet.nTotalCount_);
    const std::wstring name = Utf16ToWide(packet.szPledgeName_);
    std::array<char16_t, 512> message{};
    FormatUtf16To(
        message.data(), message.size(), u"[%d][%d][%ls] : Point %lld, RewardResaon %d", packet.nRank_, tier, name.c_str(),
        static_cast<long long>(packet.nPoint_), packet.nRewardReason_
    );
    PledgeStuff::SendSystemMessage(user.get(), ToTargetString(message.data()));
    return false;
}

bool Handle_OSA_PLEDGE_RAID_GET_INFO_LIST(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSA_PLEDGE_RAID_GET_INFO_LIST packet{};
    if (!PledgeRaidNetworkUtil::DecodePacket(packet, source)) {
        return true;
    }
    CSPointer<User> user = FindUser(packet.nUserDbId_);
    if (!user) {
        return false;
    }

    protocol::S_EX_PLEDGE_RAID_RANK response{};
    response.bIsCurrent_ = packet.bIsCurrent_;
    for (const olympiad::PkOlympiadPledgeRaidInfoData& row : packet.vList_) {
        if (row.nRank_ == 0 || row.nPoint_ == 0) {
            continue;
        }
        protocol::PkPledgeRaidRankInfo converted{};
        converted.nWorldId_ = row.nWorldId_;
        CopyTargetName(converted.szPledgeName_, static_cast<const char16_t*>(row.szPledgeName_));
        converted.nRank_ = row.nRank_;
        converted.nPoint_ = row.nPoint_;
        response.vList_.push_back(converted);
    }
    SendPacket(user.get(), S_EX_PLEDGE_RAID_RANK, response);
    return false;
}

bool Handle_OSA_PLEDGE_RAID_GET_INFO_LIST_FOR_BUILDER(WOlympiadSocketHost*, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    olympiad::OSA_PLEDGE_RAID_GET_INFO_LIST packet{};
    if (!PledgeRaidNetworkUtil::DecodePacket(packet, source)) {
        return true;
    }
    CSPointer<User> user = FindUser(packet.nUserDbId_);
    if (!user) {
        return false;
    }

    std::array<char16_t, 512> message{};
    const SharedPledgeRaidInfo* const info = SharedPledgeRaidInfo::GetInfo();
    FormatUtf16To(
        message.data(), message.size(), u"Season[%d] Total[%d]", info != nullptr ? info->nSeason_ : 0, packet.nTotalCount_
    );
    PledgeStuff::SendSystemMessage(user.get(), ToTargetString(message.data()));
    for (const olympiad::PkOlympiadPledgeRaidInfoData& row : packet.vList_) {
        const std::wstring name = Utf16ToWide(row.szPledgeName_);
        FormatUtf16To(
            message.data(), message.size(), u"[%d][%d][%ls][Point : %lld]", row.nRank_, row.nWorldId_, name.c_str(),
            static_cast<long long>(row.nPoint_)
        );
        PledgeStuff::SendSystemMessage(user.get(), ToTargetString(message.data()));
    }
    return false;
}

bool Handle_OSA_PLEDGE_RAID_GET_PREV_SEASON_INFO_LIST(WOlympiadSocketHost* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    return PledgeRaidSystem::inst().Process_OSA_PLEDGE_RAID_GET_PREV_SEASON_INFO_LIST(socket, packet);
}

// Both transports use the same DB handlers. Original handlers obtained worldId
// from socket->m_idWorld. Local dispatch supplies it without inventing a socket.
static bool ProcessGetSeason(const int worldId, const std::uint8_t* source) {
    olympiad::OCQ_PLEDGE_RAID_GET_SEASON request{};
    if (!PledgeRaidNetworkUtil::DecodePacket(request, source)) {
        return true;
    }
    const SharedPledgeRaidInfo* const info = SharedPledgeRaidInfo::GetInfo();
    if (info == nullptr) {
        return false;
    }

    olympiad::OSA_PLEDGE_RAID_GET_SEASON response{};
    response.nSeason_ = info->nSeason_;
    response.tStartTime_ = info->tSeasonStartTime_;
    // Original: PledgeRaidNetworkHost::SendPacket(socket->m_idWorld, OSA_PLEDGE_RAID_GET_SEASON, response)
    PledgeRaidNetworkHost::SendPacket(worldId, OSA_PLEDGE_RAID_GET_SEASON, response);
    return false;
}

bool Handle_OCN_PLEDGE_RAID_RESET_SEASON(WOlympiadSocketPeer* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    return PledgeRaidSystem::inst().Process_OCN_PLEDGE_RAID_RESET_SEASON(socket, packet);
}

bool Handle_OCN_PLEDGE_RAID_DECIDE(WOlympiadSocketPeer* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    return PledgeRaidSystem::inst().Process_OCN_PLEDGE_RAID_DECIDE(socket, packet);
}

static bool ProcessSetInfo(const int worldId, const std::uint8_t* source) {
    olympiad::OCQ_PLEDGE_RAID_SET_INFO request{};
    if (!PledgeRaidNetworkUtil::DecodePacket(request, source)) {
        return true;
    }

    // Original: const int worldId = socket->m_idWorld.
    AtomicLambdaJobDx* const job = MakeLambdaJobDx([worldId, request](AtomicLambdaJobDx*) {
        olympiad::OSA_PLEDGE_RAID_SET_INFO response{};
        const SharedPledgeRaidInfo* const info = SharedPledgeRaidInfo::GetInfo();
        response.nSeason_ = info != nullptr ? info->nSeason_ : 0;
        response.nPledgeDbId_ = request.nPledgeDbId_;
        response.nResult_ = 0;
        PledgeRaidNetworkHost::SendPacket(worldId, OSA_PLEDGE_RAID_SET_INFO, response);
    });
    const std::wstring pledgeName = Utf16ToWide(request.szPledgeName_);
    const SharedPledgeRaidInfo* const info = SharedPledgeRaidInfo::GetInfo();
    CDX_PLEDGE_RAID_SET_INFO packet(
        info != nullptr ? info->nSeason_ : 0, worldId, request.nPledgeDbId_, pledgeName.c_str(), request.nPoint_,
        request.tSaveTime_, job
    );
    db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x6b), static_cast<int>(sizeof(packet)), packet);
    return false;
}

static bool ProcessGetInfo(const int worldId, const std::uint8_t* source) {
    olympiad::OCQ_PLEDGE_RAID_GET_INFO request{};
    if (!PledgeRaidNetworkUtil::DecodePacket(request, source)) {
        return true;
    }

    // Original: const int worldId = socket->m_idWorld.
    AtomicLambdaJobDx* const job = MakeLambdaJobDx([worldId, request](AtomicLambdaJobDx* const callbackJob) {
        auto* const dx = static_cast<CDX_PLEDGE_RAID_GET_INFO*>(callbackJob->m_pDx);
        if (dx == nullptr) {
            return;
        }

        olympiad::OSA_PLEDGE_RAID_GET_INFO response{};
        response.nSeason_ = request.nSeason_;
        response.nPledgeDbId_ = request.nPledgeDbId_;
        CopyTargetName(response.szPledgeName_, PledgeRaidPacketHandlerAccess::PledgeName(*dx));
        response.nRank_ = PledgeRaidPacketHandlerAccess::Rank(*dx);
        response.nPoint_ = PledgeRaidPacketHandlerAccess::Point(*dx);
        response.nRewardReason_ = PledgeRaidPacketHandlerAccess::RewardReason(*dx);
        response.nTotalCount_ = PledgeRaidPacketHandlerAccess::TotalCount(*dx);
        response.bIsCurrent_ = request.bIsCurrent_;
        response.nUserDbId_ = request.nUserDbId_;
        response.tStartTime_ = request.tStartTime_;
        response.tEndTime_ = request.tEndTime_;
        if (response.nRewardReason_ != 0) {
            response.nRank_ = 0;
        }
        PledgeRaidNetworkHost::SendPacket(worldId, OSA_PLEDGE_RAID_GET_INFO, response);
    });

    CDX_PLEDGE_RAID_GET_INFO packet(request.nSeason_, worldId, request.nPledgeDbId_, job);
    db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x69), static_cast<int>(sizeof(packet)), packet);
    return false;
}

static bool ProcessGetInfoForBuilder(const int worldId, const std::uint8_t* source) {
    olympiad::OCQ_PLEDGE_RAID_GET_INFO request{};
    if (!PledgeRaidNetworkUtil::DecodePacket(request, source)) {
        return true;
    }

    // Original: const int worldId = socket->m_idWorld.
    AtomicLambdaJobDx* const job = MakeLambdaJobDx([worldId, request](AtomicLambdaJobDx* const callbackJob) {
        auto* const dx = static_cast<CDX_PLEDGE_RAID_GET_INFO*>(callbackJob->m_pDx);
        if (dx == nullptr) {
            return;
        }

        olympiad::OSA_PLEDGE_RAID_GET_INFO response{};
        response.nSeason_ = request.nSeason_;
        response.nPledgeDbId_ = request.nPledgeDbId_;
        CopyTargetName(response.szPledgeName_, PledgeRaidPacketHandlerAccess::PledgeName(*dx));
        response.nRank_ = PledgeRaidPacketHandlerAccess::Rank(*dx);
        response.nPoint_ = PledgeRaidPacketHandlerAccess::Point(*dx);
        response.nRewardReason_ = PledgeRaidPacketHandlerAccess::RewardReason(*dx);
        response.nTotalCount_ = 0;
        response.bIsCurrent_ = request.bIsCurrent_;
        response.nUserDbId_ = request.nUserDbId_;
        response.tStartTime_ = request.tStartTime_;
        response.tEndTime_ = request.tEndTime_;
        if (response.nRewardReason_ != 0) {
            response.nRank_ = 0;
        }
        PledgeRaidNetworkHost::SendPacket(worldId, OSA_PLEDGE_RAID_GET_INFO_FOR_BUILDER, response);
    });

    CDX_PLEDGE_RAID_GET_INFO packet(request.nSeason_, worldId, request.nPledgeDbId_, job);
    db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x69), static_cast<int>(sizeof(packet)), packet);
    return false;
}

static bool ProcessGetInfoList(const int worldId, const std::uint8_t* source) {
    olympiad::OCQ_PLEDGE_RAID_GET_INFO_LIST request{};
    if (!PledgeRaidNetworkUtil::DecodePacket(request, source)) {
        return true;
    }

    // Original: const int worldId = socket->m_idWorld.
    AtomicLambdaJobPacket* const job = MakeLambdaJobPacket([worldId, request](AtomicLambdaJobPacket* const callbackJob) {
        auto* const dbResponse = static_cast<protocol::RP_EX_PLEDGE_RAID_GET_INFO_LIST*>(callbackJob->m_pPacket);
        if (dbResponse == nullptr) {
            return;
        }

        olympiad::OSA_PLEDGE_RAID_GET_INFO_LIST response{};
        response.nTotalCount_ = dbResponse->nTotalCount_;
        for (const protocol::PkPledgeRaidInfoData& row : dbResponse->vList_) {
            olympiad::PkOlympiadPledgeRaidInfoData converted{};
            ConvertTo(row, converted);
            response.vList_.push_back(converted);
        }
        response.bIsCurrent_ = request.bIsCurrent_;
        response.nUserDbId_ = request.nUserDbId_;
        PledgeRaidNetworkHost::SendPacket(worldId, OSA_PLEDGE_RAID_GET_INFO_LIST, response);
    });

    protocol::RQ_EX_PLEDGE_RAID_GET_INFO_LIST packet{};
    packet.nJobKey = CDB::PushAtomicJobHelper(job);
    packet.nSeason_ = request.nSeason_;
    db.SendPacket(g_nWorldId, RQ_EX, RQ_EX_PLEDGE_RAID_GET_INFO_LIST, packet);
    return false;
}

static bool ProcessGetInfoListForBuilder(const int worldId, const std::uint8_t* source) {
    olympiad::OCQ_PLEDGE_RAID_GET_INFO_LIST request{};
    if (!PledgeRaidNetworkUtil::DecodePacket(request, source)) {
        return true;
    }

    // Original: const int worldId = socket->m_idWorld.
    AtomicLambdaJobPacket* const job = MakeLambdaJobPacket([worldId, request](AtomicLambdaJobPacket* const callbackJob) {
        auto* const dbResponse = static_cast<protocol::RP_EX_PLEDGE_RAID_GET_INFO_LIST*>(callbackJob->m_pPacket);
        if (dbResponse == nullptr) {
            return;
        }

        olympiad::OSA_PLEDGE_RAID_GET_INFO_LIST response{};
        response.nTotalCount_ = dbResponse->nTotalCount_;
        for (const protocol::PkPledgeRaidInfoData& row : dbResponse->vList_) {
            olympiad::PkOlympiadPledgeRaidInfoData converted{};
            ConvertTo(row, converted);
            response.vList_.push_back(converted);
        }
        response.bIsCurrent_ = request.bIsCurrent_;
        response.nUserDbId_ = request.nUserDbId_;
        PledgeRaidNetworkHost::SendPacket(worldId, OSA_PLEDGE_RAID_GET_INFO_LIST_FOR_BUILDER, response);
    });

    protocol::RQ_EX_PLEDGE_RAID_GET_INFO_LIST packet{};
    packet.nJobKey = CDB::PushAtomicJobHelper(job);
    packet.nSeason_ = request.nSeason_;
    db.SendPacket(g_nWorldId, RQ_EX, RQ_EX_PLEDGE_RAID_GET_INFO_LIST, packet);
    return false;
}

static bool ProcessGetPrevSeasonInfoList(const int worldId, const std::uint8_t* source) {
    olympiad::OCQ_PLEDGE_RAID_GET_PREV_SEASON_INFO_LIST request{};
    if (!PledgeRaidNetworkUtil::DecodePacket(request, source)) {
        return true;
    }

    // Original: const int worldId = socket->m_idWorld.
    AtomicLambdaJobPacket* const job = MakeLambdaJobPacket([worldId, request](AtomicLambdaJobPacket* const callbackJob) {
        auto* const dbResponse = static_cast<protocol::RP_EX_PLEDGE_RAID_GET_INFO_LIST*>(callbackJob->m_pPacket);
        if (dbResponse == nullptr) {
            return;
        }

        olympiad::OSA_PLEDGE_RAID_GET_PREV_SEASON_INFO_LIST response{};
        response.nSeason_ = request.nSeason_;
        response.nTotalCount_ = dbResponse->nTotalCount_;
        for (const protocol::PkPledgeRaidInfoData& row : dbResponse->vList_) {
            olympiad::PkOlympiadPledgeRaidInfoData converted{};
            ConvertTo(row, converted);
            response.vList_.push_back(converted);
        }
        PledgeRaidNetworkHost::SendPacket(worldId, OSA_PLEDGE_RAID_GET_PREV_SEASON_INFO_LIST, response);
    });

    protocol::RQ_EX_PLEDGE_RAID_GET_INFO_LIST packet{};
    packet.nJobKey = CDB::PushAtomicJobHelper(job);
    packet.nSeason_ = request.nSeason_;
    db.SendPacket(g_nWorldId, RQ_EX, RQ_EX_PLEDGE_RAID_GET_INFO_LIST, packet);
    return false;
}

bool Handle_OCQ_PLEDGE_RAID_GET_SEASON(WOlympiadSocketPeer* socket, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    return ProcessGetSeason(socket->m_idWorld, source);
}

bool Handle_OCQ_PLEDGE_RAID_SET_INFO(WOlympiadSocketPeer* socket, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    return ProcessSetInfo(socket->m_idWorld, source);
}

bool Handle_OCQ_PLEDGE_RAID_GET_INFO(WOlympiadSocketPeer* socket, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    return ProcessGetInfo(socket->m_idWorld, source);
}

bool Handle_OCQ_PLEDGE_RAID_GET_INFO_FOR_BUILDER(WOlympiadSocketPeer* socket, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    return ProcessGetInfoForBuilder(socket->m_idWorld, source);
}

bool Handle_OCQ_PLEDGE_RAID_GET_INFO_LIST(WOlympiadSocketPeer* socket, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    return ProcessGetInfoList(socket->m_idWorld, source);
}

bool Handle_OCQ_PLEDGE_RAID_GET_INFO_LIST_FOR_BUILDER(WOlympiadSocketPeer* socket, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    return ProcessGetInfoListForBuilder(socket->m_idWorld, source);
}

bool Handle_OCQ_PLEDGE_RAID_GET_PREV_SEASON_INFO_LIST(WOlympiadSocketPeer* socket, const std::uint8_t* source) {
    L2_GUARD_FUNCTION();
    return ProcessGetPrevSeasonInfoList(socket->m_idWorld, source);
}

bool DispatchLocalRequest(OCS_PROTOCOL protocol, int worldId, const std::uint8_t* packet) {
    switch (protocol) {
        case OCQ_PLEDGE_RAID_GET_SEASON: return ProcessGetSeason(worldId, packet);
        case OCQ_PLEDGE_RAID_SET_INFO: return ProcessSetInfo(worldId, packet);
        case OCQ_PLEDGE_RAID_GET_INFO: return ProcessGetInfo(worldId, packet);
        case OCQ_PLEDGE_RAID_GET_INFO_FOR_BUILDER: return ProcessGetInfoForBuilder(worldId, packet);
        case OCQ_PLEDGE_RAID_GET_INFO_LIST: return ProcessGetInfoList(worldId, packet);
        case OCQ_PLEDGE_RAID_GET_INFO_LIST_FOR_BUILDER: return ProcessGetInfoListForBuilder(worldId, packet);
        case OCQ_PLEDGE_RAID_GET_PREV_SEASON_INFO_LIST: return ProcessGetPrevSeasonInfoList(worldId, packet);
        // These two handlers do not use a transport or a source-world identity.
        case OCN_PLEDGE_RAID_RESET_SEASON: return Handle_OCN_PLEDGE_RAID_RESET_SEASON(nullptr, packet);
        case OCN_PLEDGE_RAID_DECIDE: return Handle_OCN_PLEDGE_RAID_DECIDE(nullptr, packet);
        default: return true;
    }
}

bool DispatchLocalResponse(OSC_PROTOCOL protocol, const std::uint8_t* packet) {
    // Reply handlers use packet user/clan IDs. None reads the transport argument.
    switch (protocol) {
        case OSA_PLEDGE_RAID_GET_SEASON: return Handle_OSA_PLEDGE_RAID_GET_SEASON(nullptr, packet);
        case OSA_PLEDGE_RAID_SET_INFO: return Handle_OSA_PLEDGE_RAID_SET_INFO(nullptr, packet);
        case OSA_PLEDGE_RAID_GET_INFO: return Handle_OSA_PLEDGE_RAID_GET_INFO(nullptr, packet);
        case OSA_PLEDGE_RAID_GET_INFO_FOR_BUILDER: return Handle_OSA_PLEDGE_RAID_GET_INFO_FOR_BUILDER(nullptr, packet);
        case OSA_PLEDGE_RAID_GET_INFO_LIST: return Handle_OSA_PLEDGE_RAID_GET_INFO_LIST(nullptr, packet);
        case OSA_PLEDGE_RAID_GET_INFO_LIST_FOR_BUILDER: return Handle_OSA_PLEDGE_RAID_GET_INFO_LIST_FOR_BUILDER(nullptr, packet);
        case OSA_PLEDGE_RAID_GET_PREV_SEASON_INFO_LIST: return Handle_OSA_PLEDGE_RAID_GET_PREV_SEASON_INFO_LIST(nullptr, packet);
        default: return true;
    }
}

} // namespace PledgeRaidPacketHandler
