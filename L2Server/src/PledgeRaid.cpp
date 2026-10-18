#include "PledgeRaid.h"
#include "CConfig.h"
#include "DB.h"
#include "InstantZone.h"
#include "NpcServer.h"
#include "Pledge.h"
#include "PledgeMastery.h"
#include "PledgeRaidInfo.h"
#include "PledgeStuff.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/MemorySub.h"
#include "User.h"
#include "UserPacketEx.h"
#include "UserSocket.h"

#include "Shared/src/AtomicJob.h"
#include "Shared/src/Config.h"
#include "Shared/src/DXPacket.h"
#include "Shared/src/DateTime.h"
#include "Shared/src/EtcMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/ScopedLockGuards.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/proto/DBPacket.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>
#include <vector>

namespace {


class PledgeRaidTimerScope {
public:
    explicit PledgeRaidTimerScope(PledgeRaidSystem& owner) : m_owner(owner) {}
    ~PledgeRaidTimerScope() { m_owner.AddTimer(PledgeRaidSystem::kHeartbeatMilliseconds, 0); }

private:
    PledgeRaidSystem& m_owner;
};

void CopyPledgeRaidName(char16_t (&destination)[25], const char16_t* const source) {
    std::fill(std::begin(destination), std::end(destination), u'\0');
    if (source == nullptr) {
        return;
    }
    const std::u16string_view target(source);
    const std::size_t copied = std::min<std::size_t>(target.size(), std::size(destination) - 1);
    std::copy_n(target.data(), copied, destination);
}

CSPointer<CPledge> FindPledgeForRaidBuilder(const char16_t* const pledgeName) {
    const std::u16string_view name(pledgeName);
    std::vector<SharedUtilsWideChar> lookupName(name.begin(), name.end());
    lookupName.push_back(0);
    return CPledge::FindPledge(g_nWorldId, lookupName.data());
}


std::int64_t ApplyPledgeRaidPointBonus(const std::int64_t point, const int percentage) noexcept {
    const std::int64_t multiplied = runtime::MultiplyInt64Wrapping(static_cast<std::int64_t>(percentage) + 100, point);
    const std::int64_t divided = multiplied / 100;
    return runtime::Win64CvttDoubleToInt64(static_cast<double>(divided) + 0.5);
}

} // namespace

L2_DEFINE_OBJECT_CLASS_ID(PledgeRaidSystem)

L2_DEFINE_OBJECT_CLASS_NAME(PledgeRaidSystem, m_pClassName_PledgeRaidSystem)

L2_DEFINE_OBJECT_ALLOCATOR(PledgeRaidSystem, __GetClassName())

L2_DEFINE_OBJECT_NEW(PledgeRaidSystem)

PledgeRaidSystem::PledgeRaidSystem() :
    bCanReward_(0),
    bLoaded_(0),
    targetPadding44To48_{} {}

PledgeRaidSystem::~PledgeRaidSystem() = default;

void PledgeRaidSystem::SendInfoToUser(const User* const user, const bool currentSeason) {
    // Current L2Server.exe 0x009c1474.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeRaid.cpp",
        //     0x209,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeRaid.cpp",
        //     0x209,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot send %s clan-raid season information: the user is unavailable.",
            currentSeason ? "current" : "previous"
        );
        return;
    }
    User* const mutableUser = const_cast<User*>(user);
    if (!PledgeRaidNetworkPeer::IsAvailable()) {
        mutableUser->SendSystemMessage(SystemMessageId::RANKING_INFORMATION_NOT_LOADED);
    }

    const SharedPledgeRaidInfo* const info = SharedPledgeRaidInfo::GetInfo();
    const int season = currentSeason ? info->nSeason_ : info->nPrevSeason_;
    const std::int32_t startTime = currentSeason ? info->tSeasonStartTime_ : info->tPrevSeasonStartTime_;
    const std::int32_t endTime = currentSeason ? info->tNextSeasonStartTime_ : info->tSeasonStartTime_;

    if (mutableUser->GetPledge() == nullptr) {
        protocol::S_EX_PLEDGE_RAID_INFO response{};
        response.bIsCurrent_ = currentSeason;
        response.nRank_ = -1;
        response.nTier_ = -1;
        response.nPoint_ = -1;
        response.nRewardReason_ = 0;
        response.nSeason_ = season;

        const L2DateTime start = L2DateTime::FromTime_t(startTime);
        const L2DateTime end = L2DateTime::FromTime_t(endTime);
        response.hStartTimeYear_ = static_cast<std::int16_t>(start.m_nYear);
        response.hStartTimeMonth_ = static_cast<std::int16_t>(start.m_nMonth);
        response.hStartTimeDay_ = static_cast<std::int16_t>(start.m_nDay);
        response.hEndTimeYear_ = static_cast<std::int16_t>(end.m_nYear);
        response.hEndTimeMonth_ = static_cast<std::int16_t>(end.m_nMonth);
        response.hEndTimeDay_ = static_cast<std::int16_t>(end.m_nDay);
        user->GetSocket()->Send(S_EX, S_EX_PLEDGE_RAID_INFO, response);
        return;
    }

    olympiad::OCQ_PLEDGE_RAID_GET_INFO request{};
    request.nSeason_ = season;
    request.nPledgeDbId_ = static_cast<int>(user->m_data->m_nPledgeID);
    request.bIsCurrent_ = currentSeason;
    request.nUserDbId_ = static_cast<int>(user->GetDBID());
    request.tStartTime_ = startTime;
    request.tEndTime_ = endTime;
    PledgeRaidNetworkPeer::SendPacket(OCQ_PLEDGE_RAID_GET_INFO, request);
}

void PledgeRaidSystem::SendRankInfoToUser(const User* const user, const bool currentSeason) {
    // Current L2Server.exe 0x009c16ac.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeRaid.cpp",
        //     0x24f,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeRaid.cpp",
        //     0x24f,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot request %s clan-raid rankings: the user is unavailable.", currentSeason ? "current" : "previous"
        );
        return;
    }

    const SharedPledgeRaidInfo* const info = SharedPledgeRaidInfo::GetInfo();
    olympiad::OCQ_PLEDGE_RAID_GET_INFO_LIST request{};
    request.nSeason_ = currentSeason ? info->nSeason_ : info->nPrevSeason_;
    request.bIsCurrent_ = currentSeason;
    request.nUserDbId_ = static_cast<int>(user->GetDBID());
    PledgeRaidNetworkPeer::SendPacket(OCQ_PLEDGE_RAID_GET_INFO_LIST, request);
}

void PledgeRaidSystem::SendAllToUser(const User* const user, const bool currentSeason) {
    // Current L2Server.exe 0x009c1fcc.
    L2_GUARD_FUNCTION();
    SendInfoToUser(user, currentSeason);
    SendRankInfoToUser(user, currentSeason);
}

void PledgeRaidSystem::ShowInfoList(const User* const user, const int season) {
    // Current L2Server.exe 0x009c0b08.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeRaid.cpp",
        //     0x36f,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeRaid.cpp",
        //     0x36f,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot request builder clan-raid rankings for season %d: the user is unavailable.", season);
        return;
    }

    olympiad::OCQ_PLEDGE_RAID_GET_INFO_LIST request{};
    request.nSeason_ = season;
    request.bIsCurrent_ = false;
    request.nUserDbId_ = static_cast<int>(user->GetDBID());
    PledgeRaidNetworkPeer::SendPacket(OCQ_PLEDGE_RAID_GET_INFO_LIST_FOR_BUILDER, request);
}

void PledgeRaidSystem::DecideSeason(const User*, const std::int32_t delay) {
    // Current L2Server.exe 0x009c1930.
    L2_GUARD_FUNCTION();
    olympiad::OCN_PLEDGE_RAID_DECIDE request{};
    request.tDelay_ = delay;
    PledgeRaidNetworkPeer::SendPacket(OCN_PLEDGE_RAID_DECIDE, request);
}

void PledgeRaidSystem::ResetSeasonBuilderCommand() {
    // Current L2Server.exe 0x009c19e8.
    L2_GUARD_FUNCTION();
    olympiad::OCN_PLEDGE_RAID_RESET_SEASON request{};
    request.tResetTime_ = runtime::crt::Time32Now();
    PledgeRaidNetworkPeer::SendPacket(OCN_PLEDGE_RAID_RESET_SEASON, request);
}

void PledgeRaidSystem::ShowInfo(const User* const user) {
    // Current L2Server.exe 0x009c2094. Keep the target's repeated shared
    // lookup and date-conversion order: start, next, next text, start text,
    // step, season.
    L2_GUARD_FUNCTION();
    const L2DateTime startDate = L2DateTime::FromTime_t(SharedPledgeRaidInfo::GetInfo()->tSeasonStartTime_);
    const L2DateTime nextDate = L2DateTime::FromTime_t(SharedPledgeRaidInfo::GetInfo()->tNextSeasonStartTime_);
    const std::u16string next = L2DateTimeUtil::ToString(nextDate);
    const std::u16string start = L2DateTimeUtil::ToString(startDate);
    const int step = SharedPledgeRaidInfo::GetInfo()->nStep_;
    const int season = SharedPledgeRaidInfo::GetInfo()->nSeason_;

    const std::wstring startText = Utf16ToWide(start);
    const std::wstring nextText = Utf16ToWide(next);
    std::array<char16_t, 512> message{};
    FormatUtf16To(
        message.data(), message.size(), u"Season[%d], Step[%d], StartTime[%s], NextTime[%s]", season, step,
        startText.c_str(), nextText.c_str()
    );
    PledgeStuff::SendSystemMessage(const_cast<User*>(user), std::u16string(message.data()));
}

void PledgeRaidSystem::ShowInfoWithPledgeName(const User* const user, const char16_t* const pledgeName, const int season) {
    // Current L2Server.exe 0x009c2250.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeRaid.cpp",
        //     0x350,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeRaid.cpp",
        //     0x350,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot request builder clan-raid information for season %d: the user is unavailable.", season);
        return;
    }

    CPledge* const pledge = FindPledgeForRaidBuilder(pledgeName).get();
    if (pledge == nullptr) {
        PledgeStuff::SendSystemMessage(const_cast<User*>(user), std::u16string(u"[ShowInfoWithPledgeName] Pledge invalid"));
        return;
    }

    olympiad::OCQ_PLEDGE_RAID_GET_INFO request{};
    request.nSeason_ = season;
    request.nPledgeDbId_ = static_cast<int>(pledge->GetDbId());
    request.bIsCurrent_ = false;
    request.nUserDbId_ = static_cast<int>(user->GetDBID());
    PledgeRaidNetworkPeer::SendPacket(OCQ_PLEDGE_RAID_GET_INFO_FOR_BUILDER, request);
}

void PledgeRaidSystem::SetPoint(const unsigned int npcSid, const int pledgeDbId, const int pledgeIndex, std::int64_t point) {
    // Current L2Server.exe 0x009c43c8.
    L2_GUARD_FUNCTION();
    CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, pledgeDbId);
    if (!pledge) {
        return;
    }

    const int originalPoint = static_cast<int>(point);
    const int actualPledgeDbId = static_cast<int>(pledge->GetDbId());
    SharedPledgeRaidInfo* const shared = SharedPledgeRaidInfo::GetInfo();
    SharedPledgeRaidInfoData* const previous = shared->GetPrevSeasonPledgeRaidInfoWithDbId(actualPledgeDbId);
    if (previous != nullptr) {
        int bonusPercentage = 0;
        if (previous->nTier_ < 1) {
            if (previous->nRank_ > 0) {
                const auto reward = PledgeRaidDB::inst().mRankRewardInfo_.find(previous->nRank_);
                if (reward != PledgeRaidDB::inst().mRankRewardInfo_.end()) {
                    bonusPercentage = reward->second.nBonusPercentage_;
                }
            }
        } else {
            const auto reward = PledgeRaidDB::inst().mTierRewardInfo_.find(previous->nTier_);
            if (reward != PledgeRaidDB::inst().mTierRewardInfo_.end()) {
                bonusPercentage = reward->second.nBonusPercentage_;
            }
        }
        if (bonusPercentage != 0) {
            point = ApplyPledgeRaidPointBonus(point, bonusPercentage);
        }
    }

    for (const PledgeRaidMasteryRewardInfo& reward : PledgeRaidDB::inst().vMasteryRewardInfo_) {
        if (pledge->GetMastery()->GetPoint(static_cast<PledgeMasteryID::Type>(reward.nMasteryId_)) != 0) {
            point = ApplyPledgeRaidPointBonus(point, reward.nPoint_);
        }
    }

    char16_t pledgeName[25]{};
    CopyPledgeRaidName(pledgeName, pledge->GetPledgeName());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x819, 0, 0, 0, 0, 0, 0, 0, pledgeName, nullptr, nullptr, shared->nSeason_, originalPoint, point, 0, 0, 0, 0, 0, 0,
        actualPledgeDbId, nullptr, nullptr, nullptr, nullptr, 0
    );

    olympiad::OCQ_PLEDGE_RAID_SET_INFO request{};
    request.nPledgeDbId_ = actualPledgeDbId;
    CopyPledgeRaidName(request.szPledgeName_, pledge->GetPledgeName());
    request.tSaveTime_ = runtime::crt::Time32Now();
    request.nPoint_ = point;

    {
        L2_WRITE_GUARD(lockPointList_);
        vPointList_.push_back(PledgeRaidPoint{actualPledgeDbId, 0, point, request.tSaveTime_, 0});
    }
    PledgeRaidNetworkPeer::SendPacket(OCQ_PLEDGE_RAID_SET_INFO, request);
    if (npcSid != 0) {
        g_npcserver.Send("cddddQ", 0xc1, npcSid, 1, actualPledgeDbId, pledgeIndex, point);
    }
}

L2_DEFINE_OBJECT_RELEASE(PledgeRaidSystem)

int PledgeRaidSystem::CalculateStep(const std::int32_t currentTime, const std::int32_t nextSeasonStartTime) const {
    L2_GUARD_FUNCTION();
    return (nextSeasonStartTime - currentTime) / 0x93a80 + 1;
}

std::int32_t PledgeRaidSystem::CalculateNextSeasonStartTime(const std::int32_t seasonStartTime) const {
    L2_GUARD_FUNCTION();
    return L2DateTime::FromTime_t(seasonStartTime).GetStartOfWeek() + 0x16c4e8;
}

int PledgeRaidSystem::CalculateTier(const int rank, const int totalCount, const int percentile) const {
    L2_GUARD_FUNCTION();

    if (rank <= 0) {
        return -1;
    }
    if (rank <= totalCount) {
        return 0;
    }
    if (totalCount < percentile && rank <= percentile) {
        const double position =
            static_cast<double>(rank - totalCount) / static_cast<double>(percentile - totalCount) * 100.0;
        for (const auto& reward : PledgeRaidDB::inst().mTierRewardInfo_) {
            if (position <= static_cast<double>(reward.second.nInPercentage_)) {
                return reward.second.nTier_;
            }
        }
    }
    return -1;
}

void PledgeRaidSystem::CheckAndReward() {
    L2_GUARD_FUNCTION();
    if (bCanReward_ == 0) {
        return;
    }

    L2_GUARD(loadLock_);
    SharedPledgeRaidInfo* const shared = SharedPledgeRaidInfo::GetInfo();
    bool noRewardWasStarted = true;

    if (shared != nullptr && shared->nTotalCount_ > 0) {
        for (int index = 0; index < SharedPledgeRaidInfo::GetInfo()->nTotalCount_; ++index) {
            SharedPledgeRaidInfoData* const entry = SharedPledgeRaidInfo::GetInfo()->GetPrevSeasonPledgeRaidInfo(index);
            if (entry == nullptr) {
                continue;
            }

            CSPointer<CPledge> pledgePointer = CPledge::FindPledge(g_nWorldId, entry->nPledgeDbId_);
            CPledge* const pledge = pledgePointer.get();
            if (pledge == nullptr) {
                db.RequestLoadPledge(g_nWorldId, static_cast<unsigned int>(entry->nPledgeDbId_), false);
                continue;
            }
            if (!entry->bCanReward_) {
                continue;
            }

            entry->bCanReward_ = false;
            noRewardWasStarted = false;
            if (entry->nTier_ <= 0 && entry->nRank_ <= 0) {
                continue;
            }

            PledgeStuff::PledgeWarehouseManipHelper helper(nullptr, pledge);
            const PledgeCapture capture{pledge->Id(), pledge->GetWorldId(), static_cast<int>(pledge->GetDbId())};
            const std::int64_t point = entry->nPoint_;
            int rankOrTier = 0;
            int nameValue = 0;
            std::map<int, int> itemMap;

            if (entry->nTier_ > 0) {
                const auto reward = PledgeRaidDB::inst().mTierRewardInfo_.find(entry->nTier_);
                if (reward != PledgeRaidDB::inst().mTierRewardInfo_.end()) {
                    const PledgeRaidTierRewardInfo& info = reward->second;
                    if (info.nNameValue_ != 0) {
                        helper.AppendEtcMani(
                            new EtcManiUpdatePledgeNameValue(pledge->GetWorldId(), 0, pledge->GetDbId(), info.nNameValue_)
                        );
                        nameValue = info.nNameValue_;
                    }
                    for (const auto& item : info.mItemMap_) {
                        helper.AddItem(item.first, item.second);
                    }
                    itemMap = info.mItemMap_;
                    rankOrTier = entry->nTier_ + 100;
                }
            } else {
                const auto reward = PledgeRaidDB::inst().mRankRewardInfo_.find(entry->nRank_);
                if (reward != PledgeRaidDB::inst().mRankRewardInfo_.end()) {
                    const PledgeRaidRankRewardInfo& info = reward->second;
                    if (info.nNameValue_ != 0) {
                        helper.AppendEtcMani(
                            new EtcManiUpdatePledgeNameValue(pledge->GetWorldId(), 0, pledge->GetDbId(), info.nNameValue_)
                        );
                        nameValue = info.nNameValue_;
                    }
                    for (const auto& item : info.mItemMap_) {
                        helper.AddItem(item.first, item.second);
                    }
                    itemMap = info.mItemMap_;
                    rankOrTier = entry->nRank_;
                }
            }

            helper.Do(MakeLambdaJob([capture, point, rankOrTier, nameValue, itemMap](int) {
                // Current L2Server 0x009c506c.
                static constexpr wchar_t functionName[] = L"PledgeRaid.cpp(338)";
                L2_GUARD_PROFILE(functionName);
                GuardHelper stackGuard(functionName, CThreadManager::GetThreadID());
                CPledge* const captured = capture.GetPledge();
                CSPointer<CPledge> retained(captured, ObjectRefType::FunctionChain);
                if (!retained) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[PLEDGE][%s][%d][PledgeRaid] "
                    //     L"No pledge : %d",
                    //     L"PledgeRaid.cpp",
                    //     0x158,
                    //     capture.m_dbid)
                    // L2_LOG_ERROR(Log,
                    //     L"[PLEDGE][%s][%d][PledgeRaid] "
                    //     L"No pledge : %d",
                    //     L"PledgeRaid.cpp",
                    //     0x158,
                    //     capture.m_dbid)
                    L2_LOG_WARN(
                        Log,
                        "Clan %u is unavailable in the raid-reward callback; this callback stops before writing reward "
                        "accounting records.",
                        static_cast<unsigned>(capture.m_dbid)
                    );
                    return;
                }

                const int previousSeason = SharedPledgeRaidInfo::GetInfo()->nPrevSeason_;
                const int pledgeDbId = static_cast<int>(retained->GetDbId());
                const std::u16string pledgeName = std::u16string(retained->GetPledgeName());
                CLogFormatter formatter;
                formatter.WriteLog(
                    0x81b, 0, 0, 0, 0, 0, 0, 0, pledgeName.c_str(), nullptr, nullptr, previousSeason, rankOrTier, point,
                    nameValue, 0, 0, 0, 0, 1, pledgeDbId, nullptr, nullptr, nullptr, nullptr, 0
                );
                for (const auto& item : itemMap) {
                    const int itemPreviousSeason = SharedPledgeRaidInfo::GetInfo()->nPrevSeason_;
                    const int itemPledgeDbId = static_cast<int>(retained->GetDbId());
                    const std::u16string itemPledgeName = std::u16string(retained->GetPledgeName());
                    formatter.WriteLog(
                        0x829, 0, 0, 0, 0, 0, 0, 0, itemPledgeName.c_str(), nullptr, nullptr, itemPreviousSeason, rankOrTier,
                        point, item.first, item.second, 0, 0, 0, 1, itemPledgeDbId, nullptr, nullptr, nullptr, nullptr, 0
                    );
                }
            }));
        }

        if (!noRewardWasStarted) {
            return;
        }
    }

    runtime::raw_atomic::ExchangeI32(reinterpret_cast<volatile std::int32_t*>(&bCanReward_), 0);
}

void PledgeRaidSystem::LoadFromDB(const bool reward) {
    L2_GUARD_FUNCTION();

    AtomicLambdaJobDx* const job = MakeLambdaJobDx([this, reward](AtomicLambdaJobDx* const callbackJob) {
        // Current L2Server 0x009c27e0.
        static constexpr wchar_t functionName[] = L"PledgeRaid.cpp(113)";
        L2_GUARD_PROFILE(functionName);
        GuardHelper stackGuard(functionName, CThreadManager::GetThreadID());
        CDX_PLEDGE_RAID_LOAD* const packet = static_cast<CDX_PLEDGE_RAID_LOAD*>(callbackJob->m_pDx);

        if (packet->tStartTime_ == 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR][PLEDGE][%s][%d]Pledge Raid Rank "
            //     L"started at first time",
            //     L"PledgeRaid.cpp",
            //     0x77)
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR][PLEDGE][%s][%d]Pledge Raid Rank "
            //     L"started at first time",
            //     L"PledgeRaid.cpp",
            //     0x77)
            L2_LOG_INFO(
                Log,
                "Clan-raid season %d has no stored start time; initializing it from the current time and requesting "
                "persistence.",
                packet->nSeason_
            );
            packet->tStartTime_ = runtime::crt::Time32Now();
            SaveToDB(packet->nSeason_, packet->tStartTime_);
        }

        SharedPledgeRaidInfo* const info = SharedPledgeRaidInfo::GetInfo();
        info->nSeason_ = packet->nSeason_;
        info->tSeasonStartTime_ = packet->tStartTime_;
        info->tNextSeasonStartTime_ = CalculateNextSeasonStartTime(packet->tStartTime_);
        info->nPrevSeason_ = packet->nPrevSeason_;
        info->tPrevSeasonStartTime_ = packet->tPrevStartTime_;

        if (reward) {
            const int nextSeasonStart = SharedPledgeRaidInfo::GetInfo()->tNextSeasonStartTime_;
            const int season = SharedPledgeRaidInfo::GetInfo()->nSeason_;
            CLogFormatter formatter;
            formatter.WriteLog(
                0x81a, 0, 0, 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, season, nextSeasonStart, 0, 0, 0, 0, 0, 0, 0, 0,
                nullptr, nullptr, nullptr, nullptr, 0
            );
        }
        LoadInzoneRestrictFromDB();
        runtime::raw_atomic::ExchangeI32(reinterpret_cast<volatile std::int32_t*>(&bCanReward_), reward ? 1 : 0);
        if (!updateSeasonLock_.EndJob()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Failed to end job at %s(%d)",
            //     L"PledgeRaid.cpp",
            //     0x87)
            // L2_LOG_ERROR(Log,
            //     L"Failed to end job at %s(%d)",
            //     L"PledgeRaid.cpp",
            //     0x87)
            L2_LOG_ERROR(
                Log, "Clan-raid season %d load callback reached cleanup with no active season-update transaction.",
                packet->nSeason_
            );
        }
    });

    CDX_PLEDGE_RAID_LOAD packet(job);
    db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x65), static_cast<int>(sizeof(packet)), packet);
}

void PledgeRaidSystem::LoadInzoneRestrictFromDB() {
    L2_GUARD_FUNCTION();

    protocol::RQ_EX_PLEDGE_RAID_INZONE_LOAD packet{};
    AtomicLambdaJobPacket* const job = MakeLambdaJobPacket([this](AtomicLambdaJobPacket* const callbackJob) {
        // Current L2Server 0x009c3fb4.
        static constexpr wchar_t functionName[] = L"PledgeRaid.cpp(82)";
        L2_GUARD_PROFILE(functionName);
        GuardHelper stackGuard(functionName, CThreadManager::GetThreadID());
        network::IPacket* const basePacket = callbackJob->m_pPacket;
        if (basePacket == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]assert falied: %s",
            //     L"PledgeRaid.cpp",
            //     0x54,
            //     L"(pPacket != NULL)")
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]assert falied: %s",
            //     L"PledgeRaid.cpp",
            //     0x54,
            //     L"(pPacket != NULL)")
            L2_LOG_ERROR(
                Log,
                "Cannot load clan-raid instance restrictions: the database callback received no response packet; existing "
                "restrictions are retained."
            );
            return;
        }

        auto* const packet = static_cast<protocol::RP_EX_PLEDGE_RAID_INZONE_LOAD*>(basePacket);
        L2_WRITE_GUARD(lockInzoneRestrictMap_);
        for (const protocol::PkPledgeRaidInzoneData& row : packet->vList_) {
            if (SharedPledgeRaidInfo::GetInfo()->tSeasonStartTime_ <= row.nRestrictTime_ &&
                row.nRestrictTime_ < SharedPledgeRaidInfo::GetInfo()->tNextSeasonStartTime_) {
                mInzoneRestrictMap_[row.nPledgeDbId_] = row.nRestrictTime_;
            }
        }
    });

    const int requestWorldId = g_nWorldId;
    packet.nJobKey = CDB::PushAtomicJobHelper(job);
    db.SendPacket(requestWorldId, RQ_EX, RQ_EX_PLEDGE_RAID_INZONE_LOAD, packet);
}

void PledgeRaidSystem::ProcessNextSeason(const bool reward) {
    L2_GUARD_FUNCTION();
    const std::int32_t nextStart = SharedPledgeRaidInfo::GetInfo()->tNextSeasonStartTime_;
    AtomicLambdaJobDx* const job = MakeLambdaJobDx([this, reward](AtomicLambdaJobDx*) {
        Clear();
        LoadFromDB(reward);
    });
    CDX_PLEDGE_RAID_NEXT_SEASON packet(nextStart, job);
    db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x67), static_cast<int>(sizeof(packet)), packet);
}

void PledgeRaidSystem::ResetSeason(const std::int32_t resetTime) {
    L2_GUARD_FUNCTION();
    AtomicLambdaJobDx* const job = MakeLambdaJobDx([this](AtomicLambdaJobDx*) {
        Clear();
        LoadFromDB(false);
    });
    CDX_PLEDGE_RAID_RESET_SEASON packet(resetTime, job);
    db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x68), static_cast<int>(sizeof(packet)), packet);
}

void PledgeRaidSystem::SaveToDB(const int season, const std::int32_t startTime) {
    L2_GUARD_FUNCTION();
    CDX_PLEDGE_RAID_SAVE packet(season, startTime, nullptr);
    db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x66), static_cast<int>(sizeof(packet)), packet);
}

void PledgeRaidSystem::RetrySetPoint() {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(lockPointList_);
    for (const PledgeRaidPoint& point : vPointList_) {
        CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, point.nPledgeDbId);
        if (!pledge) {
            continue;
        }

        olympiad::OCQ_PLEDGE_RAID_SET_INFO packet{};
        packet.nPledgeDbId_ = point.nPledgeDbId;
        CopyPledgeRaidName(packet.szPledgeName_, pledge->GetPledgeName());
        packet.tSaveTime_ = point.tSaveTime;
        packet.nPoint_ = point.nPoint;
        PledgeRaidNetworkPeer::SendPacket(OCQ_PLEDGE_RAID_SET_INFO, packet);
    }
}

void PledgeRaidSystem::SetPledgeRaidInzoneBindingAllUserInPledge(CPledge* const pledge, const bool force) {
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeRaid.cpp",
        //     0x2d5,
        //     L"(pPledge != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeRaid.cpp",
        //     0x2d5,
        //     L"(pPledge != NULL)")
        L2_LOG_ERROR(Log, "Cannot update clan-raid instance binding for clan members: the clan is unavailable.");
        return;
    }
    pledge->SetPledgeRaidInzoneBinding(force);
}

bool PledgeRaidSystem::CheckPledgeRaidInzoneBinding(User* const user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeRaid.cpp",
        //     0x264,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeRaid.cpp",
        //     0x264,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot check clan-raid instance binding: the user is unavailable; the check fails.");
        return false;
    }

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        return true;
    }

    std::int32_t bindingTime = 0;
    bool found = false;
    {
        L2_READ_GUARD(lockInzoneRestrictMap_);
        const auto position = mInzoneRestrictMap_.find(static_cast<int>(pledge->GetDbId()));
        found = position != mInzoneRestrictMap_.end();
        if (found) {
            bindingTime = position->second;
        }
    }
    if (found) {
        SetPledgeRaidInzoneBinding(user, bindingTime);
    }
    return true;
}

void PledgeRaidSystem::SetPledgeRaidInzoneBinding(User* const user, const std::int32_t bindingTime) {
    // Current L2Server.exe 0x009c17c8 reads CInstantZone+0xd0 directly at
    // 0x009c18d1 and 0x009c1909. The separate virtual
    // CInstantZone::GetInstantZoneTypeID returns -1 and is not
    // dispatched by this owner.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeRaid.cpp",
        //     0x2a2,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeRaid.cpp",
        //     0x2a2,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot set clan-raid instance binding at timestamp %d: the user is unavailable.",
            static_cast<int>(bindingTime)
        );
        return;
    }

    CInstantZone* const zone = g_InstantZoneDB.GetPledgeRaidInstantZone();
    if (zone == nullptr || zone->GetConst() == nullptr || zone->GetConst()->GetZoneType() != IZT_PLEDGE_RAID) {
        return;
    }

    int latestBindingTime = 0;
    int entranceCount = 0;
    int maxEntranceCount = 0;
    if (user->GetInZoneRestriction(zone->m_nTypeId, false, latestBindingTime, entranceCount, maxEntranceCount)) {
        const SharedPledgeRaidInfo* const info = SharedPledgeRaidInfo::GetInfo();
        if (info != nullptr && info->tSeasonStartTime_ <= latestBindingTime) {
            return;
        }
    }
    user->MarkInZoneRestriction(zone->m_nTypeId, bindingTime, false, 1, zone->m_nInzoneDBId);
}

void PledgeRaidSystem::ForceSetPledgeRaidInzoneBinding(User* const user, const std::int32_t bindingTime) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeRaid.cpp",
        //     0x284,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeRaid.cpp",
        //     0x284,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot force clan-raid instance binding at timestamp %d: the user is unavailable.",
            static_cast<int>(bindingTime)
        );
        return;
    }
    CPledge* const pledge = user->GetPledge();
    const SharedPledgeRaidInfo* const info = SharedPledgeRaidInfo::GetInfo();
    if (pledge == nullptr || info == nullptr || bindingTime < info->tSeasonStartTime_ ||
        info->tNextSeasonStartTime_ <= bindingTime) {
        return;
    }

    {
        L2_WRITE_GUARD(lockInzoneRestrictMap_);
        const int pledgeDbId = static_cast<int>(pledge->GetDbId());
        std::int32_t& storedBinding = mInzoneRestrictMap_[pledgeDbId];
        if (storedBinding != bindingTime) {
            storedBinding = bindingTime;
            CDX_PLEDGE_RAID_INZONE_SAVE packet(pledgeDbId, bindingTime, nullptr);
            db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x6d), static_cast<int>(sizeof(packet)), packet);
        }
    }
    SetPledgeRaidInzoneBinding(user, bindingTime);
}

void PledgeRaidSystem::ResetInzoneRestrict(const char16_t* const pledgeName) {
    // Current L2Server.exe 0x009c362c.
    L2_GUARD_FUNCTION();
    CPledge* const pledge = FindPledgeForRaidBuilder(pledgeName).get();
    if (pledge == nullptr) {
        return;
    }

    const int pledgeDbId = static_cast<int>(pledge->GetDbId());
    CDX_PLEDGE_RAID_INZONE_DELETE packet(pledgeDbId, nullptr);
    db.RequestTPacketProcess(g_nWorldId, static_cast<short>(0x6e), static_cast<int>(sizeof(packet)), packet);
    {
        L2_WRITE_GUARD(lockInzoneRestrictMap_);
        mInzoneRestrictMap_.erase(pledgeDbId);
    }
}

void PledgeRaidSystem::Clear() {
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(lockInzoneRestrictMap_);
        mInzoneRestrictMap_.clear();
    }
    {
        L2_WRITE_GUARD(lockPointList_);
        vPointList_.clear();
    }
    runtime::raw_atomic::ExchangeI32(reinterpret_cast<volatile std::int32_t*>(&bLoaded_), 0);
    if (SharedPledgeRaidInfo* const info = SharedPledgeRaidInfo::GetInfo()) {
        info->Clear();
    }
}

bool PledgeRaidSystem::Init() {
    L2_GUARD_FUNCTION();
    Clear();
    JobTransactionLong jobType = 1;
    if (!updateSeasonLock_.BeginJob(&jobType)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeRaid.cpp",
        //     0x37)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeRaid.cpp",
        //     0x37)
        L2_LOG_ERROR(
            Log,
            "Clan-raid initialization stopped after clearing local state: season-update transaction %d is already active; "
            "no database load or heartbeat is started by this call.",
            static_cast<int>(jobType)
        );
        return false;
    }
    LoadFromDB(false);
    AddTimer(kHeartbeatMilliseconds, 0);
    return true;
}

void PledgeRaidSystem::LoadPrevSeasonPledgeRaid() {
    L2_GUARD_FUNCTION();
    const SharedPledgeRaidInfo* const info = SharedPledgeRaidInfo::GetInfo();
    if (info == nullptr) {
        return;
    }
    olympiad::OCQ_PLEDGE_RAID_GET_PREV_SEASON_INFO_LIST packet{};
    packet.nSeason_ = info->nPrevSeason_;
    PledgeRaidNetworkPeer::SendPacket(OCQ_PLEDGE_RAID_GET_PREV_SEASON_INFO_LIST, packet);
}

bool PledgeRaidSystem::Process_OSA_PLEDGE_RAID_GET_SEASON(WOlympiadSocketHost*, const unsigned char* const source) {
    L2_GUARD_FUNCTION();
    olympiad::OSA_PLEDGE_RAID_GET_SEASON packet{};
    if (!PledgeRaidNetworkUtil::DecodePacket(packet, source)) {
        return true;
    }

    JobTransactionLong jobType = 1;
    if (!updateSeasonLock_.BeginJob(&jobType)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]To set job is failed: %d",
        //     L"PledgeRaid.cpp",
        //     0x40e,
        //     static_cast<int>(jobType))
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]To set job is failed: %d",
        //     L"PledgeRaid.cpp",
        //     0x40e,
        //     static_cast<int>(jobType))
        L2_LOG_WARN(
            Log, "Cannot process received clan-raid season %d: season-update transaction %d is already active.",
            packet.nSeason_, static_cast<int>(jobType)
        );
        return false;
    }

    SharedPledgeRaidInfo* const info = SharedPledgeRaidInfo::GetInfo();
    bool asynchronousJobStarted = false;
    if (info != nullptr) {
        if (packet.nSeason_ == info->nSeason_ + 1 && packet.tStartTime_ > info->tSeasonStartTime_ &&
            packet.tStartTime_ <= info->tNextSeasonStartTime_) {
            info->tNextSeasonStartTime_ = packet.tStartTime_;
            ProcessNextSeason(true);
            asynchronousJobStarted = true;
        } else if (info->nSeason_ < packet.nSeason_) {
            info->tNextSeasonStartTime_ = packet.tStartTime_;
            ProcessNextSeason(false);
            asynchronousJobStarted = true;
        } else if (packet.nSeason_ < info->nSeason_) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]Peer Season[%d] Over Host Season[%d]",
            //     L"PledgeRaid.cpp",
            //     0x427,
            //     info->nSeason_,
            //     packet.nSeason_)
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]Peer Season[%d] Over Host Season[%d]",
            //     L"PledgeRaid.cpp",
            //     0x427,
            //     info->nSeason_,
            //     packet.nSeason_)
            L2_LOG_ERROR(
                Log,
                "Local clan-raid season %d is ahead of received season %d; requesting a season reset using the current "
                "time.",
                info->nSeason_, packet.nSeason_
            );
            ResetSeason(runtime::crt::Time32Now());
            asynchronousJobStarted = true;
        }
    }

    if (!asynchronousJobStarted && !updateSeasonLock_.EndJob()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Failed to end job at %s(%d)",
        //     L"PledgeRaid.cpp",
        //     0x40a)
        // L2_LOG_ERROR(Log,
        //     L"Failed to end job at %s(%d)",
        //     L"PledgeRaid.cpp",
        //     0x40a)
        L2_LOG_ERROR(
            Log,
            "Processing received clan-raid season %d reached synchronous cleanup with no active season-update transaction.",
            packet.nSeason_
        );
    }
    return false;
}

bool PledgeRaidSystem::Process_OCN_PLEDGE_RAID_DECIDE(WOlympiadSocketPeer*, const unsigned char* const source) {
    L2_GUARD_FUNCTION();
    olympiad::OCN_PLEDGE_RAID_DECIDE packet{};
    if (!PledgeRaidNetworkUtil::DecodePacket(packet, source)) {
        return true;
    }

    JobTransactionLong jobType = 1;
    if (!updateSeasonLock_.BeginJob(&jobType)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]To set job is failed: %d",
        //     L"PledgeRaid.cpp",
        //     0x4ca,
        //     static_cast<int>(jobType))
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]To set job is failed: %d",
        //     L"PledgeRaid.cpp",
        //     0x4ca,
        //     static_cast<int>(jobType))
        L2_LOG_WARN(
            Log,
            "Cannot schedule the clan-raid season decision with delay %d seconds: season-update transaction %d is already "
            "active.",
            static_cast<int>(packet.tDelay_), static_cast<int>(jobType)
        );
        return false;
    }

    if (SharedPledgeRaidInfo* const info = SharedPledgeRaidInfo::GetInfo()) {
        info->tNextSeasonStartTime_ = runtime::crt::Time32Now() + packet.tDelay_;
    }
    if (packet.tDelay_ == 0) {
        // Combined host also awards its local clans. Original dedicated host: ProcessNextSeason(false)
        ProcessNextSeason(PledgeRaidNetworkUtil::IsLocalWorld());
    } else if (!updateSeasonLock_.EndJob()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Failed to end job at %s(%d)",
        //     L"PledgeRaid.cpp",
        //     0x4c6)
        // L2_LOG_ERROR(Log,
        //     L"Failed to end job at %s(%d)",
        //     L"PledgeRaid.cpp",
        //     0x4c6)
        L2_LOG_ERROR(
            Log,
            "Scheduling the clan-raid season decision with delay %d seconds reached cleanup with no active season-update "
            "transaction.",
            static_cast<int>(packet.tDelay_)
        );
    }
    return false;
}

bool PledgeRaidSystem::Process_OCN_PLEDGE_RAID_RESET_SEASON(WOlympiadSocketPeer*, const unsigned char* const source) {
    L2_GUARD_FUNCTION();
    olympiad::OCN_PLEDGE_RAID_RESET_SEASON packet{};
    if (!PledgeRaidNetworkUtil::DecodePacket(packet, source)) {
        return true;
    }
    if (!PledgeRaidNetworkPeer::IsAvailable()) {
        return false;
    }

    JobTransactionLong jobType = 1;
    if (!updateSeasonLock_.BeginJob(&jobType)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]To set job is failed: %d",
        //     L"PledgeRaid.cpp",
        //     0x4f0,
        //     static_cast<int>(jobType))
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]To set job is failed: %d",
        //     L"PledgeRaid.cpp",
        //     0x4f0,
        //     static_cast<int>(jobType))
        L2_LOG_WARN(
            Log, "Cannot reset the clan-raid season at timestamp %d: season-update transaction %d is already active.",
            static_cast<int>(packet.tResetTime_), static_cast<int>(jobType)
        );
        return false;
    }
    ResetSeason(packet.tResetTime_);
    return false;
}

bool PledgeRaidSystem::Process_OSA_PLEDGE_RAID_SET_INFO(WOlympiadSocketHost*, const unsigned char* const source) {
    L2_GUARD_FUNCTION();
    olympiad::OSA_PLEDGE_RAID_SET_INFO packet{};
    if (!PledgeRaidNetworkUtil::DecodePacket(packet, source)) {
        return true;
    }
    if (packet.nResult_ != 0) {
        return false;
    }

    L2_WRITE_GUARD(lockPointList_);
    for (auto position = vPointList_.begin(); position != vPointList_.end(); ++position) {
        if (position->nPledgeDbId == packet.nPledgeDbId_) {
            vPointList_.erase(position);
            break;
        }
    }
    return false;
}

bool PledgeRaidSystem::Process_OSA_PLEDGE_RAID_GET_PREV_SEASON_INFO_LIST(
    WOlympiadSocketHost*, const unsigned char* const source
) {
    L2_GUARD_FUNCTION();
    olympiad::OSA_PLEDGE_RAID_GET_PREV_SEASON_INFO_LIST packet{};
    if (!PledgeRaidNetworkUtil::DecodePacket(packet, source)) {
        return true;
    }

    L2_GUARD(loadLock_);
    SharedPledgeRaidInfo* const info = SharedPledgeRaidInfo::GetInfo();
    if (bLoaded_ != 0 || packet.nSeason_ != info->nPrevSeason_) {
        return false;
    }

    int maximumRank = 1;
    for (const auto& reward : PledgeRaidDB::inst().mRankRewardInfo_) {
        maximumRank = std::max(maximumRank, reward.first);
    }
    for (const olympiad::PkOlympiadPledgeRaidInfoData& row : packet.vList_) {
        if (SharedPledgeRaidInfo::GetInfo()->nTotalCount_ > 99) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d][PledgeRaid] Over Total Count %d",
            //     L"PledgeRaid.cpp",
            //     0x46d,
            //     SharedPledgeRaidInfo::GetInfo()->nTotalCount_)
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d][PledgeRaid] Over Total Count %d",
            //     L"PledgeRaid.cpp",
            //     0x46d,
            //     SharedPledgeRaidInfo::GetInfo()->nTotalCount_)
            L2_LOG_WARN(
                Log,
                "Previous clan-raid season %d reached its 100-entry limit (currently %d); remaining rows from this response "
                "are omitted and the list is marked loaded.",
                packet.nSeason_, SharedPledgeRaidInfo::GetInfo()->nTotalCount_
            );
            break;
        }

        const int tier = CalculateTier(row.nRank_, maximumRank, packet.nTotalCount_);
        if (tier == -1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d][PledgeRaid] CalculateTier Error "
            //     L"Rank[%d], Total[%d], MaxRankNumber[%d]",
            //     L"PledgeRaid.cpp",
            //     0x476,
            //     row.nRank_,
            //     packet.nTotalCount_,
            //     maximumRank)
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d][PledgeRaid] CalculateTier Error "
            //     L"Rank[%d], Total[%d], MaxRankNumber[%d]",
            //     L"PledgeRaid.cpp",
            //     0x476,
            //     row.nRank_,
            //     packet.nTotalCount_,
            //     maximumRank)
            L2_LOG_ERROR(
                Log,
                "Cannot assign a clan-raid tier to clan %u in season %d: rank %d, reported population %d, highest "
                "configured rank reward %d; this row is skipped.",
                static_cast<unsigned>(row.nPledgeDbId_), packet.nSeason_, row.nRank_, packet.nTotalCount_, maximumRank
            );
            continue;
        }
        if (tier <= 0 && row.nRank_ <= 0) {
            continue;
        }

        SharedPledgeRaidInfo::GetInfo()->AddPrevSeasonPledgeRaid(
            SharedPledgeRaidInfoData(row.nPledgeDbId_, row.szPledgeName_, row.nPoint_, row.nRank_, tier, bCanReward_ != 0)
        );

        if (bCanReward_ != 0) {
            int rankOrTier = 0;
            int nameValue = 0;
            std::map<int, int> itemMap;

            if (tier < 1) {
                const auto reward = PledgeRaidDB::inst().mRankRewardInfo_.find(row.nRank_);
                if (reward != PledgeRaidDB::inst().mRankRewardInfo_.end()) {
                    rankOrTier = row.nRank_;
                    nameValue = reward->second.nNameValue_;
                    itemMap = reward->second.mItemMap_;
                }
            } else {
                const auto reward = PledgeRaidDB::inst().mTierRewardInfo_.find(tier);
                if (reward != PledgeRaidDB::inst().mTierRewardInfo_.end()) {
                    rankOrTier = tier + 100;
                    nameValue = reward->second.nNameValue_;
                    itemMap = reward->second.mItemMap_;
                }
            }

            CLogFormatter formatter;
            formatter.WriteLog(
                0x81b, 0, 0, 0, 0, 0, 0, 0, row.szPledgeName_, nullptr, nullptr,
                SharedPledgeRaidInfo::GetInfo()->nPrevSeason_, rankOrTier, row.nPoint_, nameValue, 0, 0, 0, 0, 0,
                row.nPledgeDbId_, nullptr, nullptr, nullptr, nullptr, 0
            );
            for (const auto& item : itemMap) {
                CLogFormatter itemFormatter;
                itemFormatter.WriteLog(
                    0x829, 0, 0, 0, 0, 0, 0, 0, row.szPledgeName_, nullptr, nullptr,
                    SharedPledgeRaidInfo::GetInfo()->nPrevSeason_, rankOrTier, row.nPoint_, item.first, item.second, 0, 0, 0,
                    0, row.nPledgeDbId_, nullptr, nullptr, nullptr, nullptr, 0
                );
            }
        }
    }
    runtime::raw_atomic::ExchangeI32(reinterpret_cast<volatile std::int32_t*>(&bLoaded_), 1);
    return false;
}

void PledgeRaidSystem::TimerExpired(int) {
    L2_GUARD_FUNCTION();
    PledgeRaidTimerScope reschedule(*this);

    SharedPledgeRaidInfo* const info = SharedPledgeRaidInfo::GetInfo();
    if (info == nullptr || info->tNextSeasonStartTime_ == 0) {
        return;
    }

    if (PledgeRaidNetworkUtil::IsPeer()) {
        olympiad::OCQ_PLEDGE_RAID_GET_SEASON packet{};
        packet.nSeason_ = info->tSeasonStartTime_;
        PledgeRaidNetworkPeer::SendPacket(OCQ_PLEDGE_RAID_GET_SEASON, packet);
    }

    const std::int32_t currentTime = runtime::crt::Time32Now();
    const std::int32_t nextSeasonStart = info->tNextSeasonStartTime_;
    const bool localWorld = PledgeRaidNetworkUtil::IsLocalWorld();
    // Original: if (!PledgeRaidNetworkUtil::IsHost()) {
    // Combined mode must run both the playing-world heartbeat and host rollover.
    if (!PledgeRaidNetworkUtil::IsHost() || localWorld) {
        if (currentTime < nextSeasonStart) {
            if (bLoaded_ == 0) {
                LoadPrevSeasonPledgeRaid();
            } else {
                CheckAndReward();
            }
            RetrySetPoint();
            const int step = CalculateStep(currentTime, nextSeasonStart);
            if (step != info->nStep_) {
                info->nStep_ = step;
            }
        }
        if (!localWorld) {
            return;
        }
    }

    JobTransactionLong jobType = 1;
    if (!updateSeasonLock_.BeginJob(&jobType)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]To set job is failed: %d",
        //     L"PledgeRaid.cpp",
        //     0xc5,
        //     static_cast<int>(jobType))
        // Noise: The heartbeat normally finds a season-update transaction active while its asynchronous database callback is
        // pending. This tick is skipped and the timer is rescheduled. Transaction timeout diagnostics remain in
        // JobTransactionLock. L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]To set job is failed: %d",
        //     L"PledgeRaid.cpp",
        //     0xc5,
        //     static_cast<int>(jobType))
        return;
    }
    if (currentTime < info->tNextSeasonStartTime_) {
        if (!updateSeasonLock_.EndJob()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Failed to end job at %s(%d)",
            //     L"PledgeRaid.cpp",
            //     0xc1)
            // L2_LOG_ERROR(Log,
            //     L"Failed to end job at %s(%d)",
            //     L"PledgeRaid.cpp",
            //     0xc1)
            L2_LOG_ERROR(
                Log, "Clan-raid heartbeat reached cleanup before the next season with no active season-update transaction."
            );
        }
        return;
    }
    // Original dedicated host: ProcessNextSeason(false)
    ProcessNextSeason(localWorld);
}
