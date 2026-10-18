#include "PledgeContribution.h"
#include "CConfig.h"
#include "DB.h"
#include "Pledge.h"
#include "PledgeContributionInfo.h"
#include "PledgeStuff.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/MemorySub.h"
#include "User.h"
#include "UserPacketEx.h"
#include "UserSocket.h"

#include "Shared/src/AtomicJob.h"
#include "Shared/src/Config.h"
#include "Shared/src/DXPacket.h"
#include "Shared/src/EtcMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/proto/DBPacket.h"

#include <array>
#include <cstring>
#include <cwchar>
#include <string>

namespace {


class HeartbeatScope {
public:
    explicit HeartbeatScope(PledgeContributionSystem& owner) : m_owner(owner) {}
    ~HeartbeatScope() { m_owner.AddTimer(PledgeContributionSystem::kHeartbeatMilliseconds, 0); }

private:
    PledgeContributionSystem& m_owner;
};

void Send_S_EX_PLEDGE_CONTRIBUTION_REWARD(const User* const user, const int result) {
    L2_GUARD_FUNCTION();
    protocol::S_EX_PLEDGE_CONTRIBUTION_REWARD packet{};
    packet.nResult = result;
    PledgeStuff::SendPacket(user, S_EX_PLEDGE_CONTRIBUTION_REWARD, packet);
}

UserCapture CaptureUser(const User& user) {
    User& mutableUser = const_cast<User&>(user);
    return UserCapture{mutableUser.Id(), user.m_data->GetWorldId(), static_cast<int>(mutableUser.GetDBID())};
}

void SendContributionInfo(const UserCapture capture, const protocol::RP_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO& response) {
    User* const user = capture.GetUser();
    if (user == nullptr) {
        return;
    }

    protocol::S_EX_PLEDGE_CONTRIBUTION_INFO packet{};
    packet.nPeriod = response.Info.nPeriod;
    packet.nTotal = response.Info.nTotal;
    const PledgeTotalGradeInfo* const grade = PledgeContributionDB::inst().FindTotalGrade(response.nRewardedStep + 1);
    if (grade != nullptr) {
        packet.nTotalForNextReward = grade->point;
        packet.nItemClassID = grade->Reward.item_class;
        packet.nAmount = static_cast<int>(grade->Reward.item_amount);
        packet.nNameValue = grade->Reward.name_value;
    }
    user->GetSocket()->Send(S_EX, S_EX_PLEDGE_CONTRIBUTION_INFO, packet);
}

void SendContributionRank(
    const UserCapture capture, const bool current, const protocol::RP_EX_PLEDGE_CONTRIBUTION_RANK& response
) {
    User* const user = capture.GetUser();
    if (user == nullptr) {
        return;
    }

    protocol::S_EX_PLEDGE_CONTRIBUTION_RANK packet{};
    packet.bCurrent = current;
    for (const protocol::PkPledgeContributionMemberInfo& source : response.vMembers) {
        protocol::PkPledgeContributionRankInfo target{};
        target.nRank = source.nRank;
        std::memcpy(target.szName, source.szName.data(), sizeof(target.szName));
        target.nPledgeType = source.nPledgeType;
        target.nPeriod = source.nPeriod;
        target.nTotal = source.nTotal;
        packet.vMembers.push_back(target);
    }
    user->GetSocket()->Send(S_EX, S_EX_PLEDGE_CONTRIBUTION_RANK, packet);
}

} // namespace

// Portable registration: the runtime ID is no longer a shipping number.
// static_assert(
//     L2ServerMemory::kPledgeContributionSystemClassId ==
//     PledgeContributionSystem::kMemoryClassId)

namespace PledgeContributionStuff {

void GivePeriodRewardTo(
    User* const user, const PledgeRewardInfo* const reward, const PledgePeriodRewardWeightInfo* const weightInfo
) {
    // Current L2Server 0x00994810.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeContribution.cpp",
        //     0x2f7,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeContribution.cpp",
        //     0x2f7,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot grant a period-contribution reward: the user is unavailable.");
        return;
    }
    if (reward == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeContribution.cpp",
        //     0x2f8,
        //     L"(pReward != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeContribution.cpp",
        //     0x2f8,
        //     L"(pReward != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot grant a period-contribution reward to character %u: the reward definition is unavailable.",
            static_cast<unsigned>(user->GetDBID())
        );
        return;
    }
    if (weightInfo == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeContribution.cpp",
        //     0x2f9,
        //     L"(pWeightInfo != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeContribution.cpp",
        //     0x2f9,
        //     L"(pWeightInfo != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot grant a period-contribution reward to character %u: the reward-weight definition is unavailable.",
            static_cast<unsigned>(user->GetDBID())
        );
        return;
    }

    static_cast<void>(PledgeStuff::GiveItem(
        user, reward->item_class,
        static_cast<std::int64_t>(static_cast<float>(reward->item_amount) * weightInfo->item_factor + 0.5F)
    ));
    static_cast<void>(PledgeStuff::GiveNameValue(
        user, static_cast<int>(static_cast<float>(reward->name_value) * weightInfo->namevalue_factor + 0.5F)
    ));
}

void GivePeriodRewardTo(User* const user, const int rank, const int ratio, const int activeMember) {
    // Current L2Server 0x00994b3c.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeContribution.cpp",
        //     0x2d3,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeContribution.cpp",
        //     0x2d3,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(
            Log,
            "Cannot select period-contribution rewards: the user is unavailable (rank %d, ratio %d, active members %d).",
            rank, ratio, activeMember
        );
        return;
    }

    const PledgeContributionDB& database = PledgeContributionDB::inst();
    const PledgePeriodRewardWeightInfo* const weightInfo = database.FindPeriodWeight(activeMember);
    const PledgePeriodRankInfo* const rankInfo = database.FindPeriodRank(rank);
    if (rankInfo != nullptr && database.min_member_count <= activeMember) {
        GivePeriodRewardTo(user, &rankInfo->Reward, weightInfo);
    }
    const PledgePeriodGradeInfo* const gradeInfo = database.FindPeriodGrade(ratio);
    if (gradeInfo != nullptr) {
        GivePeriodRewardTo(user, &gradeInfo->Reward, weightInfo);
    }
}

} // namespace PledgeContributionStuff

L2_DEFINE_OBJECT_CLASS_ID(PledgeContributionSystem)

L2_DEFINE_OBJECT_CLASS_NAME(PledgeContributionSystem, m_pClassName)

L2_DEFINE_OBJECT_ALLOCATOR(PledgeContributionSystem, __GetClassName())

L2_DEFINE_OBJECT_NEW(PledgeContributionSystem)

PledgeContributionSystem::PledgeContributionSystem() :
    m_alignment2cTo30{},
    m_alignment58To60{},
    m_nSeason(0),
    m_tStartTime(0),
    m_tNextSeasonTime(0),
    m_alignment6cTo70(0) {}

PledgeContributionSystem::~PledgeContributionSystem() = default;

L2_DEFINE_OBJECT_RELEASE(PledgeContributionSystem)

void PledgeContributionSystem::SaveRank() {
    L2_GUARD_FUNCTION();
    CDX_PLEDGE_CONTRIBUTION_RANK_SAVE packet(m_nSeason, m_tStartTime);
    db.RequestTPacketProcess(g_nWorldId, static_cast<short>(99), static_cast<int>(sizeof(packet)), packet);
}

void PledgeContributionSystem::AddPoint(User* const user, CPledge* const pledge, const int point) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeContribution.cpp",
        //     0x10e,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeContribution.cpp",
        //     0x10e,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot add %d clan-contribution points: the user is unavailable.", point);
        return;
    }
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeContribution.cpp",
        //     0x10f,
        //     L"(pPledge != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeContribution.cpp",
        //     0x10f,
        //     L"(pPledge != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot add %d clan-contribution points for character %u: the clan is unavailable.", point,
            static_cast<unsigned>(user->GetDBID())
        );
        return;
    }

    PledgeStuff::Write_LOG_PLEDGE_CONTRIB_ADD(user, pledge, point);
    CDX_PLEDGE_CONTRIBUTION_MEMBER_ADD packet(static_cast<int>(user->GetDBID()), static_cast<int>(pledge->GetDbId()), point);
    db.RequestTPacketProcess(pledge->GetWorldId(), static_cast<short>(95), static_cast<int>(sizeof(packet)), packet);
}

void PledgeContributionSystem::RetrieveInfo(User* const user) {
    // Current L2Server 0x00995e7c.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeContribution.cpp",
        //     0x8e,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeContribution.cpp",
        //     0x8e,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot request clan-contribution information: the user is unavailable.");
        return;
    }
    if (user->m_data->m_nPledgeID == 0) {
        return;
    }

    const UserCapture capture = CaptureUser(*user);
    AtomicLambdaJobPacket* const job = MakeLambdaJobPacket([capture](AtomicLambdaJobPacket* const callback) {
        const auto* const response = static_cast<protocol::RP_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO*>(callback->m_pPacket);
        if (response == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]assert falied: %s",
            //     L"PledgeContribution.cpp",
            //     0xa4,
            //     L"(pPacket != NULL)")
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]assert falied: %s",
            //     L"PledgeContribution.cpp",
            //     0xa4,
            //     L"(pPacket != NULL)")
            L2_LOG_ERROR(
                Log, "Clan-contribution information response is missing for character %u; no information is sent.",
                static_cast<unsigned>(capture.m_dbid)
            );
            return;
        }
        SendContributionInfo(capture, *response);
    });
    protocol::RQ_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO request{};
    request.nJobKey = CDB::PushAtomicJobHelper(job);
    request.nUserDBID = static_cast<int>(user->GetDBID());
    db.SendPacket(user->m_data->GetWorldId(), RQ_EX, RQ_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO, request);
}

void PledgeContributionSystem::RetrieveRank(User* const user, const bool current) {
    // Current L2Server 0x00995fe4.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeContribution.cpp",
        //     0xcf,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeContribution.cpp",
        //     0xcf,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot request %s clan-contribution ranking: the user is unavailable.", current ? "current" : "previous"
        );
        return;
    }
    if (user->m_data->m_nPledgeID == 0) {
        return;
    }

    const UserCapture capture = CaptureUser(*user);
    AtomicLambdaJobPacket* const job = MakeLambdaJobPacket([capture, current](AtomicLambdaJobPacket* const callback) {
        const auto* const response = static_cast<protocol::RP_EX_PLEDGE_CONTRIBUTION_RANK*>(callback->m_pPacket);
        if (response == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]assert falied: %s",
            //     L"PledgeContribution.cpp",
            //     0xe9,
            //     L"(pPacket != NULL)")
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]assert falied: %s",
            //     L"PledgeContribution.cpp",
            //     0xe9,
            //     L"(pPacket != NULL)")
            L2_LOG_ERROR(
                Log, "The %s clan-contribution ranking response is missing for character %u; no ranking is sent.",
                current ? "current" : "previous", static_cast<unsigned>(capture.m_dbid)
            );
            return;
        }
        SendContributionRank(capture, current, *response);
    });
    protocol::RQ_EX_PLEDGE_CONTRIBUTION_RANK request{};
    request.nJobKey = CDB::PushAtomicJobHelper(job);
    request.nPledgeDBID = static_cast<int>(user->m_data->m_nPledgeID);
    request.bCurrent = current;
    db.SendPacket(user->m_data->GetWorldId(), RQ_EX, RQ_EX_PLEDGE_CONTRIBUTION_RANK, request);
}

void PledgeContributionSystem::TakeTotalReward(User* const user) {
    // Current L2Server 0x00996164.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeContribution.cpp",
        //     0x120,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeContribution.cpp",
        //     0x120,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot request a total-contribution reward: the user is unavailable.");
        return;
    }
    if (user->m_data->m_nPledgeID == 0) {
        return;
    }
    if (!user->CanGetPVPPoint()) {
        Send_S_EX_PLEDGE_CONTRIBUTION_REWARD(user, 1);
        user->SendSystemMessage(SystemMessageId::FAME_REQUIRES_LEVEL_40_AND_SECOND_CLASS_TRANSFER);
        return;
    }

    protocol::RQ_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO request{};
    request.nUserDBID = static_cast<int>(user->GetDBID());
    const std::uint32_t requesterSid = user->Id();
    const int requesterDatabaseId = static_cast<int>(user->GetDBID());
    const int requesterWorldId = user->m_data->GetWorldId();
    const UserCapture capture{requesterSid, requesterWorldId, requesterDatabaseId};
    AtomicLambdaJobPacket* const job = MakeLambdaJobPacket([this, capture](AtomicLambdaJobPacket* const callback) {
        // Current L2Server 0x0099783c.
        static constexpr wchar_t functionName[] = L"PledgeContribution.cpp(316)";
        L2_GUARD_PROFILE(functionName);
        GuardHelper stackGuard(functionName, CThreadManager::GetThreadID());
        const auto* const response = static_cast<protocol::RP_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO*>(callback->m_pPacket);
        if (response == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]assert falied: %s",
            //     L"PledgeContribution.cpp",
            //     0x13e,
            //     L"(pPacket != NULL)")
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]assert falied: %s",
            //     L"PledgeContribution.cpp",
            //     0x13e,
            //     L"(pPacket != NULL)")
            L2_LOG_ERROR(
                Log, "Total-contribution reward lookup returned no response for character %u; reward processing stops.",
                static_cast<unsigned>(capture.m_dbid)
            );
            return;
        }
        User* const requester = capture.GetUser();
        if (requester == nullptr) {
            return;
        }
        if (!response->bSuccess) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"PledgeContribution.cpp",
            //     0x14b)
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"PledgeContribution.cpp",
            //     0x14b)
            L2_LOG_ERROR(
                Log, "Total-contribution reward lookup reported failure for character %u; the reward request is rejected.",
                static_cast<unsigned>(capture.m_dbid)
            );
            Send_S_EX_PLEDGE_CONTRIBUTION_REWARD(requester, 1);
            return;
        }
        TakeTotalReward(requester, response->nRewardedStep, response->Info.nTotal);
    });
    request.nJobKey = CDB::PushAtomicJobHelper(job);
    db.SendPacket(requesterWorldId, RQ_EX, RQ_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO, request);
}

void PledgeContributionSystem::TakeTotalReward(User* const user, const int rewardedStep, const int totalPoint) {
    // Current L2Server 0x00996d2c.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeContribution.cpp",
        //     0x15d,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeContribution.cpp",
        //     0x15d,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot process a total-contribution reward: the user is unavailable (rewarded step %d, total points %d).",
            rewardedStep, totalPoint
        );
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        Send_S_EX_PLEDGE_CONTRIBUTION_REWARD(user, 1);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeContribution.cpp",
        //     0x164)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeContribution.cpp",
        //     0x164)
        L2_LOG_WARN(
            Log, "Character %u no longer has an available clan; the total-contribution reward is rejected.",
            static_cast<unsigned>(user->GetDBID())
        );
        return;
    }

    const int nextStep = runtime::AddInt32Wrapping(rewardedStep, 1);
    const PledgeTotalGradeInfo* const info = PledgeContributionDB::inst().FindTotalGrade(nextStep);
    if (info == nullptr) {
        Send_S_EX_PLEDGE_CONTRIBUTION_REWARD(user, 1);
        PledgeStuff::SendSystemMessage(user, L2Util::StringFormat(u"No more reward: %d", nextStep));
        return;
    }
    if (totalPoint < info->point) {
        Send_S_EX_PLEDGE_CONTRIBUTION_REWARD(user, 1);
        PledgeStuff::SendSystemMessage(user, std::u16string(u"Not enough total contribution point"));
        return;
    }

    const std::uint32_t userSid = user->Id();
    const int userDatabaseId = static_cast<int>(user->GetDBID());
    const int userWorldId = user->m_data->GetWorldId();
    const UserCapture userCapture{userSid, userWorldId, userDatabaseId};
    PledgeStuff::ItemManipHelper helper(user, ItemGetReason::PledgeTotalContribution);
    if ((info->Reward.item_class < 1 || info->Reward.item_amount < 1) && info->Reward.name_value < 1) {
        Send_S_EX_PLEDGE_CONTRIBUTION_REWARD(user, 1);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]No reward: %d, %d",
        //     L"PledgeContribution.cpp",
        //     0x185,
        //     static_cast<int>(user->GetDBID()),
        //     nextStep)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]No reward: %d, %d",
        //     L"PledgeContribution.cpp",
        //     0x185,
        //     static_cast<int>(user->GetDBID()),
        //     nextStep)
        L2_LOG_ERROR(
            Log,
            "Total-contribution grade %d has no usable reward for character %u: item class %d, count %lld, fame points %d; "
            "the request is rejected.",
            nextStep, static_cast<unsigned>(user->GetDBID()), info->Reward.item_class,
            static_cast<long long>(info->Reward.item_amount), info->Reward.name_value
        );
        return;
    }
    if (info->Reward.item_class > 0 && info->Reward.item_amount > 0) {
        helper.PushManiplutor(info->Reward.item_class, info->Reward.item_amount);
    }
    if (info->Reward.name_value > 0) {
        helper.PushPointEtcMani(PT_PVP_POINT, info->Reward.name_value);
    }
    const int contributionPledgeId = static_cast<int>(user->m_data->m_nPledgeID);
    const int contributionUserDatabaseId = static_cast<int>(user->GetDBID());
    const std::uint32_t contributionUserSid = user->Id();
    helper.PushEtcMani(new CETC_MANI_PLEDGE_CONTRIBUTION_REWARD(
        contributionUserSid, contributionUserDatabaseId, contributionPledgeId, rewardedStep, info->point
    ));
    PledgeStuff::Write_LOG_PLEDGE_CONTRIB_REWARD(
        user, user->GetPledge(), rewardedStep, totalPoint, info->point, PledgeStuff::ActionStep::Try
    );

    const std::uint32_t pledgeSid = pledge->Id();
    const int pledgeDatabaseId = static_cast<int>(pledge->GetDbId());
    const int pledgeWorldId = pledge->GetWorldId();
    const PledgeCapture pledgeCapture{pledgeSid, pledgeWorldId, pledgeDatabaseId};
    helper.Do(MakeLambdaJob([userCapture, pledgeCapture, rewardedStep, totalPoint, info](const int result) {
        User* const requester = userCapture.GetUser();
        Send_S_EX_PLEDGE_CONTRIBUTION_REWARD(requester, result);
        PledgeStuff::Write_LOG_PLEDGE_CONTRIB_REWARD(
            requester, pledgeCapture.GetPledge(), rewardedStep, totalPoint, info->point, PledgeStuff::ActionStep::Success
        );
    }));
}

void PledgeContributionSystem::TakePeriodReward(const User* const user) {
    // Current L2Server 0x00996328. Callback 0x00997210.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeContribution.cpp",
        //     0x1bc,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeContribution.cpp",
        //     0x1bc,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot request a period-contribution reward: the user is unavailable.");
        return;
    }
    if (user->m_data->m_nPledgeID == 0) {
        return;
    }

    User* const mutableUser = const_cast<User*>(user);
    const std::uint32_t requesterSid = mutableUser->Id();
    const int requesterDatabaseId = static_cast<int>(mutableUser->GetDBID());
    const int requesterWorldId = user->m_data->GetWorldId();
    const UserCapture capture{requesterSid, requesterWorldId, requesterDatabaseId};
    AtomicLambdaJobDx* const job = MakeLambdaJobDx([capture](AtomicLambdaJobDx* const callbackJob) {
        // Current L2Server 0x00997210.
        static constexpr wchar_t functionName[] = L"PledgeContribution.cpp(458)";
        L2_GUARD_PROFILE(functionName);
        GuardHelper stackGuard(functionName, CThreadManager::GetThreadID());
        auto* const packet = static_cast<CDX_PLEDGE_CONTRIBUTION_MEMBER_TAKE_PERIOD_REWARD*>(callbackJob->m_pDx);
        if (packet == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]assert falied: %s",
            //     L"PledgeContribution.cpp",
            //     0x1cc,
            //     L"(pDX != NULL)")
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]assert falied: %s",
            //     L"PledgeContribution.cpp",
            //     0x1cc,
            //     L"(pDX != NULL)")
            L2_LOG_ERROR(
                Log, "Period-contribution reward callback has no response for character %u; no reward is granted.",
                static_cast<unsigned>(capture.m_dbid)
            );
            return;
        }

        User* const requester = capture.GetUser();
        if (requester == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"PledgeContribution.cpp",
            //     0x1d2)
            // Noise: The captured player has left before the asynchronous reward callback. The existing early return
            // remains, without a misleading NOT IMPLEMENTED error. L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"PledgeContribution.cpp",
            //     0x1d2)
            return;
        }
        if (packet->m_nResult == 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]Failed to take period reward: %d, %d",
            //     L"PledgeContribution.cpp",
            //     0x1da,
            //     static_cast<int>(requester->GetDBID()),
            //     0)
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]Failed to take period reward: %d, %d",
            //     L"PledgeContribution.cpp",
            //     0x1da,
            //     static_cast<int>(requester->GetDBID()),
            //     0)
            L2_LOG_ERROR(
                Log,
                "Period-contribution reward response for character %u has result 0; this handler returns without granting "
                "the reward.",
                static_cast<unsigned>(requester->GetDBID())
            );
            return;
        }
        PledgeContributionStuff::GivePeriodRewardTo(requester, packet->m_nRank, packet->m_nRatio, packet->m_nActiveMember);
    });
    CDX_PLEDGE_CONTRIBUTION_MEMBER_TAKE_PERIOD_REWARD packet(static_cast<int>(mutableUser->GetDBID()), job);
    db.RequestTPacketProcess(requesterWorldId, static_cast<short>(97), static_cast<int>(sizeof(packet)), packet);
}

void PledgeContributionSystem::LoadFromDB() {
    L2_GUARD_FUNCTION();
    AtomicLambdaJobDx* const job = MakeLambdaJobDx([this](AtomicLambdaJobDx* const callbackJob) {
        auto* const packet = static_cast<CDX_PLEDGE_CONTRIBUTION_RANK_LOAD*>(callbackJob->m_pDx);
        if (packet == nullptr) {
            return;
        }

        m_nSeason = packet->nSeason;
        if (packet->tStartTime == 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR][PLEDGE][%s][%d]Contribution Rank "
            //     L"started at first time",
            //     L"PledgeContribution.cpp",
            //     0x5a)
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR][PLEDGE][%s][%d]Contribution Rank "
            //     L"started at first time",
            //     L"PledgeContribution.cpp",
            //     0x5a)
            L2_LOG_INFO(
                Log,
                "Clan-contribution season %d has no stored start time; initializing it from the current time and requesting "
                "persistence.",
                packet->nSeason
            );
            m_tStartTime = runtime::crt::Time32Now();
            SaveRank();
        } else {
            m_tStartTime = packet->tStartTime;
        }
        m_tNextSeasonTime = m_WeeklyTimeCalculator.CalculateTime(m_tStartTime, runtime::crt::Time32Now());
        AddTimer(kHeartbeatMilliseconds, 0);
    });

    CDX_PLEDGE_CONTRIBUTION_RANK_LOAD packet(job);
    db.RequestTPacketProcess(g_nWorldId, static_cast<short>(98), static_cast<int>(sizeof(packet)), packet);
}

void PledgeContributionSystem::ProcessRankDecision() {
    L2_GUARD_FUNCTION();
    JobTransactionLong jobType = 1;
    GuardBeginJob guardBegin(m_DecideRankLock, jobType, L"PledgeContribution.cpp", 0x202);
    if (!guardBegin.IsLocked()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]To set job is failed: %d",
        //     L"PledgeContribution.cpp",
        //     0x206,
        //     static_cast<int>(jobType))
        // Noise: The periodic rank check is waiting for an active asynchronous rank-decision transaction. The next heartbeat
        // retries. Transaction timeouts retain their own diagnostic. L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]To set job is failed: %d",
        //     L"PledgeContribution.cpp",
        //     0x206,
        //     static_cast<int>(jobType))
        return;
    }
    guardBegin.WaitForEndJob();

    AtomicLambdaJobDx* const job = MakeLambdaJobDx([this](AtomicLambdaJobDx*) {
        // Current L2Server 0x00995058.
        static constexpr wchar_t functionName[] = L"PledgeContribution.cpp(532)";
        L2_GUARD_PROFILE(functionName);
        GuardHelper stackGuard(functionName, CThreadManager::GetThreadID());
        ++m_nSeason;
        m_tStartTime = m_tNextSeasonTime;
        m_tNextSeasonTime = m_WeeklyTimeCalculator.CalculateTime(m_tStartTime, runtime::crt::Time32Now());
        PledgeStuff::Write_LOG_PLEDGE_CONTRIB_PERIOD_CHANGE(m_nSeason, m_tNextSeasonTime);
        if (!m_DecideRankLock.EndJob()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Failed to end job at %s(%d)",
            //     L"PledgeContribution.cpp",
            //     0x216)
            // L2_LOG_ERROR(Log,
            //     L"Failed to end job at %s(%d)",
            //     L"PledgeContribution.cpp",
            //     0x216)
            L2_LOG_ERROR(
                Log,
                "Clan-contribution rank callback found no active transaction during cleanup; the local season and "
                "next-decision time were already advanced."
            );
        }
    });
    CDX_PLEDGE_CONTRIBUTION_RANK_DECIDE packet(m_nSeason, m_tNextSeasonTime, PledgeContributionDB::inst().min_point, job);
    db.RequestTPacketProcess(g_nWorldId, static_cast<short>(100), static_cast<int>(sizeof(packet)), packet);
}

void PledgeContributionSystem::TimerExpired(int) {
    L2_GUARD_FUNCTION();
    HeartbeatScope heartbeat(*this);
    if (m_tNextSeasonTime != 0 && m_tNextSeasonTime <= runtime::crt::Time32Now()) {
        ProcessRankDecision();
    }
}

void PledgeContributionSystem::SetupWeeklyTimerInfo() {
    L2_GUARD_FUNCTION();
    const auto& period = PledgeContributionDB::inst().setting.period;
    m_WeeklyTimeCalculator.nWeek = period.weeks;
    m_WeeklyTimeCalculator.wdayFilter.clear();
    m_WeeklyTimeCalculator.wdayFilter.push_back(period.weekday);
    m_WeeklyTimeCalculator.hour.first = period.hour;
    m_WeeklyTimeCalculator.hour.second = period.hour;
}

bool PledgeContributionSystem::Init() {
    L2_GUARD_FUNCTION();
    SetupWeeklyTimerInfo();
    LoadFromDB();
    return true;
}

void PledgeContributionSystem::Builder::AddPoint(
    PledgeContributionSystem*, const int userDatabaseId, const int pledgeDatabaseId, const int point
) {
    L2_GUARD_FUNCTION();
    CDX_PLEDGE_CONTRIBUTION_MEMBER_ADD packet(userDatabaseId, pledgeDatabaseId, point);
    db.RequestTPacketProcess(g_nWorldId, static_cast<short>(95), static_cast<int>(sizeof(packet)), packet);
}

void PledgeContributionSystem::Builder::DecideSeason(
    PledgeContributionSystem* const system, const User*, const std::int32_t delay
) {
    L2_GUARD_FUNCTION();
    system->m_tNextSeasonTime = runtime::crt::Time32Now() + delay;
    if (delay == 0) {
        system->ProcessRankDecision();
    }
}

void PledgeContributionSystem::Builder::ResetInfo(PledgeContributionSystem*, User* const user) {
    if (user == nullptr) {
        return;
    }
    CDX_PLEDGE_CONTRIBUTION_MEMBER_RESET packet(
        static_cast<int>(user->GetDBID()), static_cast<int>(user->m_data->m_nPledgeID), nullptr
    );
    db.RequestTPacketProcess(g_nWorldId, static_cast<short>(96), static_cast<int>(sizeof(packet)), packet);
}

void PledgeContributionSystem::Builder::RetrieveRankInfo(
    PledgeContributionSystem*, const User* const user, const bool current
) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeContribution.cpp",
        //     0x25e,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeContribution.cpp",
        //     0x25e,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot request clan-contribution ranking through the builder command: the user is unavailable.");
        return;
    }

    protocol::RQ_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO infoRequest{};
    infoRequest.nUserDBID = static_cast<int>(const_cast<User*>(user)->GetDBID());
    User* const mutableUser = const_cast<User*>(user);
    const std::uint32_t infoRequesterSid = mutableUser->Id();
    const int infoRequesterDatabaseId = static_cast<int>(mutableUser->GetDBID());
    const int infoRequesterWorldId = user->m_data->GetWorldId();
    const UserCapture infoCapture{infoRequesterSid, infoRequesterWorldId, infoRequesterDatabaseId};
    AtomicLambdaJobPacket* const infoJob = MakeLambdaJobPacket([infoCapture](AtomicLambdaJobPacket* const callback) {
        // Current L2Server 0x00997374.
        static constexpr wchar_t functionName[] = L"PledgeContribution.cpp(623)";
        L2_GUARD_PROFILE(functionName);
        GuardHelper stackGuard(functionName, CThreadManager::GetThreadID());
        const auto* const response = static_cast<protocol::RP_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO*>(callback->m_pPacket);
        if (response == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]assert falied: %s",
            //     L"PledgeContribution.cpp",
            //     0x271,
            //     L"(pPacket != NULL)")
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]assert falied: %s",
            //     L"PledgeContribution.cpp",
            //     0x271,
            //     L"(pPacket != NULL)")
            L2_LOG_ERROR(
                Log, "Builder contribution-information request returned no response for character %u.",
                static_cast<unsigned>(infoCapture.m_dbid)
            );
            return;
        }
        if (!response->bSuccess) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"PledgeContribution.cpp",
            //     0x277)
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"PledgeContribution.cpp",
            //     0x277)
            L2_LOG_ERROR(
                Log, "Builder contribution-information lookup reported failure for character %u; no information is sent.",
                static_cast<unsigned>(infoCapture.m_dbid)
            );
            return;
        }

        User* const requester = infoCapture.GetUser();
        if (requester == nullptr) {
            return;
        }
        const PledgeTotalGradeInfo* const grade =
            PledgeContributionDB::inst().FindTotalGrade(runtime::AddInt32Wrapping(response->nRewardedStep, 1));
        const int nextRewardPoint = grade != nullptr ? grade->point : 0;
        PledgeStuff::SendSystemMessage(
            requester->GetSocket(),
            L2Util::StringFormat(
                u"My Contribution Period(%d), Total(%d/%d)", response->Info.nPeriod, response->Info.nTotal, nextRewardPoint
            )
        );
    });
    infoRequest.nJobKey = CDB::PushAtomicJobHelper(infoJob);
    db.SendPacket(infoRequesterWorldId, RQ_EX, RQ_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO, infoRequest);

    protocol::RQ_EX_PLEDGE_CONTRIBUTION_RANK rankRequest{};
    rankRequest.nPledgeDBID = static_cast<int>(user->m_data->m_nPledgeID);
    rankRequest.bCurrent = current;
    const std::uint32_t rankRequesterSid = mutableUser->Id();
    const int rankRequesterDatabaseId = static_cast<int>(mutableUser->GetDBID());
    const int rankRequesterWorldId = user->m_data->GetWorldId();
    const UserCapture rankCapture{rankRequesterSid, rankRequesterWorldId, rankRequesterDatabaseId};
    AtomicLambdaJobPacket* const rankJob =
        MakeLambdaJobPacket([rankCapture, current](AtomicLambdaJobPacket* const callback) {
            // Current L2Server 0x0099750c.
            static constexpr wchar_t functionName[] = L"PledgeContribution.cpp(674)";
            L2_GUARD_PROFILE(functionName);
            GuardHelper stackGuard(functionName, CThreadManager::GetThreadID());
            const auto* const response = static_cast<protocol::RP_EX_PLEDGE_CONTRIBUTION_RANK*>(callback->m_pPacket);
            if (response == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[PLEDGE][%s][%d]assert falied: %s",
                //     L"PledgeContribution.cpp",
                //     0x2a4,
                //     L"(pPacket != NULL)")
                // L2_LOG_ERROR(Log,
                //     L"[PLEDGE][%s][%d]assert falied: %s",
                //     L"PledgeContribution.cpp",
                //     0x2a4,
                //     L"(pPacket != NULL)")
                L2_LOG_ERROR(
                    Log, "Builder %s contribution-ranking request returned no response for character %u.",
                    current ? "current" : "previous", static_cast<unsigned>(rankCapture.m_dbid)
                );
                return;
            }

            User* const requester = rankCapture.GetUser();
            if (requester == nullptr) {
                return;
            }
            CUserSocket* const socket = requester->GetSocket();
            PledgeStuff::SendSystemMessage(socket, std::u16string(u"Contribution Ranker"));
            for (const protocol::PkPledgeContributionMemberInfo& member : response->vMembers) {
                const std::wstring memberName = Utf16ToWide(member.szName.data());
                PledgeStuff::SendSystemMessage(
                    socket,
                    L2Util::StringFormat(
                        u"[%d] %s, %d, %d, %d", member.nRank, memberName.c_str(), member.nPledgeType, member.nPeriod,
                        member.nTotal
                    )
                );
            }
        });
    rankRequest.nJobKey = CDB::PushAtomicJobHelper(rankJob);
    db.SendPacket(rankRequesterWorldId, RQ_EX, RQ_EX_PLEDGE_CONTRIBUTION_RANK, rankRequest);
}

void PledgeContributionSystem::Builder::ShowInfo(PledgeContributionSystem* const system, const User* const user) {
    L2_GUARD_FUNCTION();
    const L2DateTime startTime = L2DateTime::FromTime_t(system->m_tStartTime);
    const L2DateTime nextTime = L2DateTime::FromTime_t(system->m_tNextSeasonTime);
    const std::u16string next = L2DateTimeUtil::ToString(nextTime);
    const std::u16string start = L2DateTimeUtil::ToString(startTime);
    const std::wstring startText = Utf16ToWide(start);
    const std::wstring nextText = Utf16ToWide(next);
    std::array<char16_t, 512> message{};
    FormatUtf16To(
        message.data(), message.size(), u"Season[%d], StartTime[%s], NextTime[%s]", system->m_nSeason, startText.c_str(),
        nextText.c_str()
    );
    PledgeStuff::SendSystemMessage(const_cast<User*>(user), std::u16string(message.data()));
}
