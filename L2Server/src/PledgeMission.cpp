#include "PledgeMission.h"
#include "DB.h"
#include "Inventory.h"
#include "Pledge.h"
#include "PledgeStuff.h"
#include "Protocol/src/SystemMessageIds.h"
#include "User.h"
#include "UserPacketEx.h"
#include "UserSocket.h"

#include "Shared/src/DXPacket.h"
#include "Shared/src/DateTime.h"
#include "Shared/src/EtcMani.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/proto/DBPacket.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>

namespace PledgeMissionConst {

const int CHECK_ONE_WEEK_TICK_ = 90;
const int SEND_TO_DB_TICK_ = 1;

} // namespace PledgeMissionConst

namespace {

std::u16string FormatMissionBuilderMessage(const int missionId, const int count, const int goalCount, const int state) {
    std::array<char16_t, 256> message{};
    FormatUtf16To(
        message.data(), message.size(), u"MissionId[%d], Count[%d], GoalCount[%d], State[%d]", missionId, count, goalCount,
        state
    );
    return std::u16string(message.data());
}


void SendPacket(const User* user, S_EX_PROTOCOL extendedProtocol, network::IPacket& packet) {
    L2_GUARD_FUNCTION();
    if (user) {
        user->GetSocket()->Send(static_cast<S_PROTOCOL>(0xfe), extendedProtocol, packet);
    }
}

} // namespace

UserPledgeMissionInfo::UserPledgeMissionInfo(PledgeMissionInfo* pledgeMissionInfo) :
    nCount_(0),
    nDeltaCount_(0),
    nState_(PledgeMission::PMS_INVALID),
    tRewardTime_(0),
    mPledgeMissionInfo_(pledgeMissionInfo) {
    runtime::raw_atomic::ExchangeI32(&nNeedUpdateInfo_[0], 0);
    runtime::raw_atomic::ExchangeI32(&nNeedUpdateInfo_[1], 1);
}

UserPledgeMissionInfo::UserPledgeMissionInfo(const UserPledgeMissionInfo& other) :
    nCount_(runtime::raw_atomic::LoadI32Relaxed(&other.nCount_)),
    nDeltaCount_(runtime::raw_atomic::LoadI32Relaxed(&other.nDeltaCount_)),
    nState_(runtime::raw_atomic::LoadI32Relaxed(&other.nState_)),
    tRewardTime_(other.tRewardTime_),
    mPledgeMissionInfo_(other.mPledgeMissionInfo_) {
    runtime::raw_atomic::ExchangeI32(
        &nNeedUpdateInfo_[0], runtime::raw_atomic::LoadI32Relaxed(&other.nNeedUpdateInfo_[0])
    );
    runtime::raw_atomic::ExchangeI32(
        &nNeedUpdateInfo_[1], runtime::raw_atomic::LoadI32Relaxed(&other.nNeedUpdateInfo_[1])
    );
}

UserPledgeMissionInfo& UserPledgeMissionInfo::operator=(const UserPledgeMissionInfo& other) {
    runtime::raw_atomic::ExchangeI32(&nCount_, runtime::raw_atomic::LoadI32Relaxed(&other.nCount_));
    runtime::raw_atomic::ExchangeI32(&nDeltaCount_, runtime::raw_atomic::LoadI32Relaxed(&other.nDeltaCount_));
    runtime::raw_atomic::ExchangeI32(&nState_, runtime::raw_atomic::LoadI32Relaxed(&other.nState_));
    tRewardTime_ = other.tRewardTime_;
    *mPledgeMissionInfo_ = *other.mPledgeMissionInfo_;
    runtime::raw_atomic::ExchangeI32(
        &nNeedUpdateInfo_[0], runtime::raw_atomic::LoadI32Relaxed(&other.nNeedUpdateInfo_[0])
    );
    runtime::raw_atomic::ExchangeI32(
        &nNeedUpdateInfo_[1], runtime::raw_atomic::LoadI32Relaxed(&other.nNeedUpdateInfo_[1])
    );
    return *this;
}

bool UserPledgeMissionInfo::CheckCompleteState() {
    L2_GUARD_FUNCTION();
    return !mPledgeMissionInfo_->bRepeat_ && tRewardTime_ > 0;
}

void UserPledgeMissionInfo::SetState(PledgeMission::State state) {
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::ExchangeI32(&nState_, static_cast<std::int32_t>(state));
    runtime::raw_atomic::ExchangeI32(&nNeedUpdateInfo_[1], 1);
}

void UserPledgeMissionInfo::AddCount(int count) {
    L2_GUARD_FUNCTION();

    const std::int32_t goal = mPledgeMissionInfo_->nGoalCount_;
    std::int32_t expected = runtime::raw_atomic::LoadI32Relaxed(&nCount_);
    std::int32_t desired = expected;
    for (;;) {
        const std::int32_t sum = runtime::AddInt32Wrapping(expected, count);
        desired = sum < goal ? sum : goal;
        const std::int32_t observed = runtime::raw_atomic::CompareExchangeI32(&nCount_, desired, expected);
        if (observed == expected) {
            break;
        }
        expected = observed;
    }

    runtime::raw_atomic::AddI32(&nDeltaCount_, runtime::SubtractInt32Wrapping(desired, expected));
    if (runtime::raw_atomic::LoadI32Relaxed(&nCount_) == goal) {
        SetState(PledgeMission::PMS_CAN_REWARD);
    }
    runtime::raw_atomic::ExchangeI32(&nNeedUpdateInfo_[0], 1);
    runtime::raw_atomic::ExchangeI32(&nNeedUpdateInfo_[1], 1);
}

void UserPledgeMissionInfo::SetCount(int count) {
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::ExchangeI32(&nCount_, count);
    runtime::raw_atomic::ExchangeI32(&nDeltaCount_, 0);
    if (runtime::raw_atomic::LoadI32Relaxed(&nCount_) == mPledgeMissionInfo_->nGoalCount_) {
        SetState(PledgeMission::PMS_CAN_REWARD);
    }
    runtime::raw_atomic::ExchangeI32(&nNeedUpdateInfo_[1], 1);
}

UserPledgeMission::UserPledgeMission() :
    mUserPledgeMissionInfoMap_(), nRewardCountOfOneWeek_(0), nLoaded_(0) {
    runtime::raw_atomic::ExchangeI32(&nNeedUpdate_[0], 0);
    runtime::raw_atomic::ExchangeI32(&nNeedUpdate_[1], 0);
}

UserPledgeMission::~UserPledgeMission() = default;

void UserPledgeMission::Init(const User* const user) {
    // Current L2Server 0x009bb738.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0xcd,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0xcd,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot initialize clan missions: the user is unavailable.");
        return;
    }
    Load(user);
}

void UserPledgeMission::Load(const User* const user) {
    // Current L2Server 0x009bb448.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0xd8,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0xd8,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot load clan missions: the user is unavailable.");
        return;
    }

    const UserCapture capture{user->Id(), user->m_data->GetWorldId(), static_cast<int>(user->GetDBID())};
    runtime::raw_atomic::ExchangeI32(&nLoaded_, 0);
    runtime::raw_atomic::ExchangeI32(&nNeedUpdate_[0], 0);
    runtime::raw_atomic::ExchangeI32(&nNeedUpdate_[1], 0);

    for (const auto& mission : PledgeMissionDB::inst().mPledgeMissionInfoMap_) {
        UserPledgeMissionInfo missionInfo(mission.second);
        Insert(mission.first, missionInfo);
    }

    AtomicLambdaJobPacket* const job = MakeLambdaJobPacket([capture, this](AtomicLambdaJobPacket* const callback) {
        const auto* const packet = static_cast<protocol::RP_EX_PLEDGE_MISSION_LOAD*>(callback->m_pPacket);
        if (packet == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]assert falied: %s",
            //     L"PledgeMission.cpp",
            //     0xf3,
            //     L"(pPacket != NULL)")
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]assert falied: %s",
            //     L"PledgeMission.cpp",
            //     0xf3,
            //     L"(pPacket != NULL)")
            L2_LOG_ERROR(
                Log,
                "Character %u: the clan-mission load callback received no response packet; mission loading cannot continue.",
                static_cast<unsigned>(capture.m_dbid)
            );
            return;
        }

        User* const capturedUser = capture.GetUser();
        if (capturedUser == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"PledgeMission.cpp",
            //     0xf9)
            // Noise: Mission loading can complete after the captured user has left. This callback returns without publishing
            // user state or sending a reply. L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"PledgeMission.cpp",
            //     0xf9)
            return;
        }

        for (const protocol::PkPledgeMissionLoadData& row : packet->vList_) {
            UserPledgeMissionInfo* const missionInfo = Find(row.nMissionId_);
            if (missionInfo == nullptr) {
                continue;
            }
            missionInfo->SetCount(row.nCount_);
            missionInfo->tRewardTime_ = row.nRewardTime_;
            if (row.nRewardTime_ != 0 && !missionInfo->mPledgeMissionInfo_->bRepeat_) {
                missionInfo->SetState(PledgeMission::PMS_COMPLETE);
            }
        }
        LoadCountOfOneWeek(capturedUser);
    });

    protocol::RQ_EX_PLEDGE_MISSION_LOAD request;
    request.nJobKey = CDB::PushAtomicJobHelper(job);
    request.nUserDbId_ = static_cast<int>(user->GetDBID());
    db.SendPacket(user->m_data->GetWorldId(), RQ_EX, RQ_EX_PLEDGE_MISSION_LOAD, request);
}

void UserPledgeMission::LoadCountOfOneWeek(const User* const user) {
    // Current L2Server 0x009b8c84.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x11d,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x11d,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot load the weekly clan-mission reward count: the user is unavailable.");
        return;
    }

    const std::uint32_t userSid = user->Id();
    const int userDatabaseId = static_cast<int>(user->GetDBID());
    const int capturedWorldId = user->m_data->GetWorldId();
    const UserCapture capture{userSid, capturedWorldId, userDatabaseId};
    CalculateCountOfOneWeek();

    AtomicLambdaJobDx* const job = MakeLambdaJobDx([capture, this](AtomicLambdaJobDx* const callback) {
        User* const capturedUser = capture.GetUser();
        if (capturedUser == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"PledgeMission.cpp",
            //     0x12e)
            // Noise: Weekly reward-count loading can complete after the captured user has left. This callback returns
            // without publishing user state or sending a reply. L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"PledgeMission.cpp",
            //     0x12e)
            return;
        }
        const auto* const packet = static_cast<CDX_PLEDGE_MISSION_COUNT_OF_ONE_WEEK_LOAD*>(callback->m_pDx);
        runtime::raw_atomic::ExchangeI32(&nRewardCountOfOneWeek_, packet->nCount_);
        runtime::raw_atomic::ExchangeI32(&nLoaded_, 1);
        LoadingCompleted(capturedUser);
    });
    CDX_PLEDGE_MISSION_COUNT_OF_ONE_WEEK_LOAD packet(static_cast<int>(user->GetDBID()), tWeekStartTime_, job);
    db.RequestTPacketProcess(user->m_data->GetWorldId(), static_cast<short>(0x71), static_cast<int>(sizeof(packet)), packet);
}

void UserPledgeMission::CalculateCountOfOneWeek() {
    L2_GUARD_FUNCTION();

    const std::int32_t firstNow = runtime::crt::Time32Now();
    const std::int32_t secondNow = runtime::crt::Time32Now();
    const std::int32_t startOfWeek = L2DateTime::FromTime_t(secondNow).GetStartOfWeek();
    const std::int32_t candidate = runtime::AddInt32Wrapping(startOfWeek, 0x44fe8);
    tWeekStartTime_ = candidate;
    if (firstNow < candidate) {
        tNextWeekStartTime_ = candidate;
        tWeekStartTime_ = runtime::AddInt32Wrapping(startOfWeek, -0x4ea98);
    } else {
        tNextWeekStartTime_ = runtime::AddInt32Wrapping(startOfWeek, 0xd8a68);
    }
}

bool UserPledgeMission::CheckGoalCondition(UserPledgeMissionInfo& missionInfo, int firstValue, int secondValue) {
    L2_GUARD_FUNCTION();

    const auto& conditions = missionInfo.mPledgeMissionInfo_->vMissionGoalConditionList_;
    if (conditions.size() == 1) {
        return runtime::CheckedVectorAt(conditions, 0)->Check(firstValue);
    }
    if (conditions.size() == 2) {
        return runtime::CheckedVectorAt(conditions, 0)->Check(firstValue) &&
            runtime::CheckedVectorAt(conditions, 1)->Check(secondValue);
    }
    return true;
}

UserPledgeMissionInfo* UserPledgeMission::Find(int missionId) {
    L2_GUARD_FUNCTION();

    for (MissionInfoMap& missionMap : mUserPledgeMissionInfoMap_) {
        const auto found = missionMap.find(missionId);
        if (found != missionMap.end()) {
            return &found->second;
        }
    }
    return nullptr;
}

bool UserPledgeMission::Insert(int missionId, UserPledgeMissionInfo& missionInfo) {
    L2_GUARD_FUNCTION();

    const std::int32_t goalType = static_cast<std::int32_t>(missionInfo.mPledgeMissionInfo_->eGoalType_);
    if (missionId == 0 || static_cast<std::uint32_t>(goalType) >= static_cast<std::uint32_t>(PledgeMission::PMGT_MAX)) {
        return false;
    }

    MissionInfoMap& missionMap = mUserPledgeMissionInfoMap_[goalType];
    const auto found = missionMap.find(missionId);
    if (found == missionMap.end()) {
        missionMap.emplace(missionId, missionInfo);
    } else {
        found->second = missionInfo;
    }
    return true;
}

int UserPledgeMission::CalculateMaxRwardCountOfOneWeek(int userType) {
    L2_GUARD_FUNCTION();

    const PledgeMissionCountOfOneWeekInfo& countInfo = PledgeMissionDB::inst().mPledgeMissionCountOfOneWeekInfo_;
    if (userType == 2) {
        return countInfo.nHonors_;
    }
    if (userType == 1) {
        return countInfo.nNobless_;
    }
    return countInfo.nNovice_;
}

bool UserPledgeMission::CheckCondition(const User* user, UserPledgeMissionInfo& missionInfo) {
    L2_GUARD_FUNCTION();

    for (IMissionCondition* condition : missionInfo.mPledgeMissionInfo_->vMissionConditionList_) {
        if (!condition->Check(const_cast<User*>(user))) {
            return false;
        }
    }
    return true;
}

bool UserPledgeMission::CheckAndUpdateState(const User* user, int missionId, UserPledgeMissionInfo& missionInfo) {
    L2_GUARD_FUNCTION();
    static_cast<void>(missionId);

    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x207,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x207,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot update clan mission %d state: the user is unavailable.", missionId);
        return false;
    }

    const std::int32_t currentState = runtime::raw_atomic::LoadI32Relaxed(&missionInfo.nState_);
    if (currentState == PledgeMission::PMS_COMPLETE) {
        return false;
    }

    PledgeMission::State nextState = PledgeMission::PMS_COMPLETE;
    if (!missionInfo.CheckCompleteState()) {
        const bool reachedWeeklyLimit = static_cast<std::int32_t>(missionInfo.mPledgeMissionInfo_->eCategory_) == 1 &&
            runtime::raw_atomic::LoadI32Relaxed(&nRewardCountOfOneWeek_) >=
                CalculateMaxRwardCountOfOneWeek(user->m_data->m_nNoblessType);
        if (reachedWeeklyLimit || !CheckCondition(user, missionInfo)) {
            nextState = PledgeMission::PMS_INVALID;
        } else if (runtime::raw_atomic::LoadI32Relaxed(&missionInfo.nCount_) <
            missionInfo.mPledgeMissionInfo_->nGoalCount_) {
            nextState = PledgeMission::PMS_ING;
        } else {
            nextState = PledgeMission::PMS_CAN_REWARD;
        }
    }

    if (currentState == static_cast<std::int32_t>(nextState)) {
        return false;
    }

    missionInfo.SetState(nextState);
    runtime::raw_atomic::ExchangeI32(&nNeedUpdate_[1], 1);
    if (nextState == PledgeMission::PMS_CAN_REWARD) {
        SendToUserIfChanged(user);
    }
    return true;
}

bool UserPledgeMission::CheckConditionAndAddCount(
    const User* user, int missionId, UserPledgeMissionInfo& missionInfo, int firstValue, int secondValue, int count
) {
    L2_GUARD_FUNCTION();

    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x26e,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x26e,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot add %d progress to clan mission %d: the user is unavailable.", count, missionId);
        return false;
    }
    if (runtime::raw_atomic::LoadI32Relaxed(&nLoaded_) == 0) {
        return false;
    }

    CheckAndUpdateState(user, missionId, missionInfo);
    if (runtime::raw_atomic::LoadI32Relaxed(&missionInfo.nState_) != PledgeMission::PMS_ING) {
        return false;
    }
    if (!CheckGoalCondition(missionInfo, firstValue, secondValue)) {
        return false;
    }

    missionInfo.AddCount(count);
    runtime::raw_atomic::ExchangeI32(&nNeedUpdate_[0], 1);
    runtime::raw_atomic::ExchangeI32(&nNeedUpdate_[1], 1);
    if (runtime::raw_atomic::LoadI32Relaxed(&missionInfo.nState_) == PledgeMission::PMS_CAN_REWARD) {
        SendToUserIfChanged(user);
    }
    return true;
}

bool UserPledgeMission::CheckAndAddCount(
    const User* user, PledgeMission::GoalType goalType, int firstValue, int secondValue, int count
) {
    L2_GUARD_FUNCTION();

    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x28f,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x28f,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot add %d clan-mission progress for goal type %d: the user is unavailable.", count,
            static_cast<int>(goalType)
        );
        return false;
    }
    if (runtime::raw_atomic::LoadI32Relaxed(&nLoaded_) == 0) {
        return false;
    }

    MissionInfoMap& missionMap = mUserPledgeMissionInfoMap_[static_cast<std::int32_t>(goalType)];
    for (auto& entry : missionMap) {
        CheckConditionAndAddCount(user, entry.first, entry.second, firstValue, secondValue, count);
    }
    return true;
}

void UserPledgeMission::LoadingCompleted(const User* const user) {
    // Current L2Server 0x009bc4f4.
    L2_GUARD_FUNCTION();

    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x144,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x144,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot complete clan-mission loading: the user is unavailable.");
        return;
    }

    CheckAndUpdateStateAll(user);
    for (int level = user->m_data->m_MainClassLevel; level > 0; --level) {
        CheckAndAddCount(user, PledgeMission::PMGT_PC_MAIN_LEVEL_UP, level, 0, 1);
    }
    for (int level = user->m_data->m_DualClassLevel; level > 0; --level) {
        CheckAndAddCount(user, PledgeMission::PMGT_PC_DUAL_LEVEL_UP, level, 0, 1);
    }
    if (user->m_data->m_nNoblessType == 1 || user->m_data->m_nNoblessType == 2) {
        CheckAndAddCount(user, PledgeMission::PMGT_PC_GET_NOBLE, 0, 0, 1);
    }
    if (user->m_data->m_nNoblessType == 2) {
        CheckAndAddCount(user, PledgeMission::PMGT_PC_GET_HONORS, 0, 0, 1);
    }

    MissionInfoMap& questMissions = mUserPledgeMissionInfoMap_[PledgeMission::PMGT_QUEST];
    for (auto& entry : questMissions) {
        const int missionId = entry.first;
        UserPledgeMissionInfo& missionInfo = entry.second;
        if (runtime::raw_atomic::LoadI32Relaxed(&missionInfo.nState_) != PledgeMission::PMS_ING) {
            continue;
        }

        const std::vector<std::int32_t> questIds = static_cast<IsInIntList_MissionCondition*>(
            runtime::CheckedVectorAt(missionInfo.mPledgeMissionInfo_->vMissionGoalConditionList_, 0)
        )
                                                       ->vIntList_;
        for (const std::int32_t questId : questIds) {
            const bool inQuestRange = (questId >= 0 && questId < 0xff) ||
                (static_cast<std::uint32_t>(questId) - UINT32_C(0x280f) < UINT32_C(0x701));
            if (inQuestRange && const_cast<User*>(user)->IsDoneQuest(questId)) {
                CheckConditionAndAddCount(user, missionId, missionInfo, questId, 0, 1);
            }
        }
    }

    runtime::raw_atomic::ExchangeI32(&nNeedUpdate_[1], 1);
    SendRewardCountOfOneWeekToUser(user);
    SendToUserIfChanged(user);
}

void UserPledgeMission::CheckAndUpdateStateAll(const User* user) {
    L2_GUARD_FUNCTION();

    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x244,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x244,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot update clan-mission states: the user is unavailable.");
        return;
    }
    if (runtime::raw_atomic::LoadI32Relaxed(&nLoaded_) == 0) {
        return;
    }

    for (MissionInfoMap& missionMap : mUserPledgeMissionInfoMap_) {
        for (auto& entry : missionMap) {
            CheckAndUpdateState(user, entry.first, entry.second);
        }
    }
}

void UserPledgeMission::SendToDBIfChanged(const User* user) {
    L2_GUARD_FUNCTION();

    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x38e,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x38e,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot save changed clan-mission progress: the user is unavailable.");
        return;
    }
    if (runtime::raw_atomic::LoadI32Relaxed(&nLoaded_) == 0 ||
        runtime::raw_atomic::ExchangeI32(&nNeedUpdate_[0], 0) == 0) {
        return;
    }

    protocol::RQ_EX_PLEDGE_MISSION_ADD_COUNT packet;
    packet.nUserDbId_ = static_cast<int>(user->GetDBID());
    for (MissionInfoMap& missionMap : mUserPledgeMissionInfoMap_) {
        for (auto& entry : missionMap) {
            UserPledgeMissionInfo& missionInfo = entry.second;
            if (runtime::raw_atomic::ExchangeI32(&missionInfo.nNeedUpdateInfo_[0], 0) != 0) {
                protocol::PkPledgeMissionAddCountData row;
                row.nMissionId_ = entry.first;
                row.nCount_ = runtime::raw_atomic::ExchangeI32(&missionInfo.nDeltaCount_, 0);
                packet.vList_.push_back(row);
            }
        }
    }

    if (!packet.vList_.empty()) {
        user->SendDB(static_cast<RQ_PROTOCOL>(0xea), static_cast<RQ_EX_PROTOCOL>(0x1c1), packet);
    }
}

void UserPledgeMission::SendToUserIfChanged(const User* user) {
    L2_GUARD_FUNCTION();

    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x3d8,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x3d8,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot send changed clan-mission information: the user is unavailable.");
        return;
    }
    if (runtime::raw_atomic::LoadI32Relaxed(&nLoaded_) == 0 ||
        runtime::raw_atomic::ExchangeI32(&nNeedUpdate_[1], 0) == 0) {
        return;
    }

    protocol::S_EX_PLEDGE_MISSION_INFO packet;
    for (MissionInfoMap& missionMap : mUserPledgeMissionInfoMap_) {
        for (auto& entry : missionMap) {
            UserPledgeMissionInfo& missionInfo = entry.second;
            if (runtime::raw_atomic::ExchangeI32(&missionInfo.nNeedUpdateInfo_[1], 0) != 0) {
                protocol::PkPledgeMissionInfo row;
                row.nMissionId_ = entry.first;
                row.nCount_ = runtime::raw_atomic::LoadI32Relaxed(&missionInfo.nCount_);
                row.nState_ = static_cast<char>(runtime::raw_atomic::LoadI32Relaxed(&missionInfo.nState_));
                packet.vList_.push_back(row);
            }
        }
    }

    SendPacket(user, static_cast<S_EX_PROTOCOL>(0x1e0), packet);
}

void UserPledgeMission::SendRewardCountOfOneWeekToUserWithType(const User* user, int userType) {
    L2_GUARD_FUNCTION();

    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x411,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x411,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot send the weekly clan-mission reward count for user type %d: the user is unavailable.", userType
        );
        return;
    }
    if (runtime::raw_atomic::LoadI32Relaxed(&nLoaded_) == 0) {
        return;
    }

    protocol::S_EX_PLEDGE_MISSION_REWARD_COUNT packet;
    packet.nCount_ = runtime::raw_atomic::LoadI32Relaxed(&nRewardCountOfOneWeek_);
    packet.nMaxCount_ = CalculateMaxRwardCountOfOneWeek(userType);
    SendPacket(user, static_cast<S_EX_PROTOCOL>(0x1e1), packet);
}

void UserPledgeMission::SendRewardCountOfOneWeekToUser(const User* user) {
    L2_GUARD_FUNCTION();

    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x404,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x404,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot send the weekly clan-mission reward count: the user is unavailable.");
        return;
    }
    if (runtime::raw_atomic::LoadI32Relaxed(&nLoaded_) != 0) {
        SendRewardCountOfOneWeekToUserWithType(user, user->m_data->m_nNoblessType);
    }
}

void UserPledgeMission::IncrementRewardCountOfOneWeek(const User* const user) {
    // Current L2Server.exe 0x009b86c8.
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::LoadI32Relaxed(&nLoaded_) == 0) {
        return;
    }
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x1a3,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x1a3,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot increment the weekly clan-mission reward count: the user is unavailable.");
        return;
    }

    runtime::raw_atomic::AddI32(&nRewardCountOfOneWeek_, 1);
    CDX_PLEDGE_CONTRIBUTION_MEMBER_ADD packet(static_cast<int>(user->GetDBID()), 1, tWeekStartTime_);
    db.RequestTPacketProcess(user->m_data->GetWorldId(), static_cast<short>(0x72), static_cast<int>(sizeof(packet)), packet);
}

void UserPledgeMission::ResetRewardCountOfOneWeek(const User* const user) {
    // Current L2Server.exe 0x009b8e14.
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::LoadI32Relaxed(&nLoaded_) == 0) {
        return;
    }
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x17c,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x17c,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot reset the weekly clan-mission reward count: the user is unavailable.");
        return;
    }

    User* const mutableUser = const_cast<User*>(user);
    const std::uint32_t requesterSid = mutableUser->Id();
    const int requesterDatabaseId = static_cast<int>(mutableUser->GetDBID());
    const int requesterWorldId = user->m_data->GetWorldId();
    const UserCapture capture{requesterSid, requesterWorldId, requesterDatabaseId};
    runtime::raw_atomic::ExchangeI32(&nLoaded_, 0);
    CalculateCountOfOneWeek();

    const int requestWorldId = user->m_data->GetWorldId();
    AtomicLambdaJobDx* const job = MakeLambdaJobDx([capture, this](AtomicLambdaJobDx*) {
        // Current L2Server 0x009bc7f0.
        static constexpr wchar_t functionName[] = L"PledgeMission.cpp(391)";
        L2_GUARD_PROFILE(functionName);
        GuardHelper stackGuard(functionName, CThreadManager::GetThreadID());
        User* const capturedUser = capture.GetUser();
        if (capturedUser == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"PledgeMission.cpp",
            //     0x18d)
            // Noise: Weekly reward-count reset can complete after the captured user has left. This callback returns without
            // publishing user state or sending a reply. L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"PledgeMission.cpp",
            //     0x18d)
            return;
        }
        runtime::raw_atomic::ExchangeI32(&nRewardCountOfOneWeek_, 0);
        runtime::raw_atomic::ExchangeI32(&nLoaded_, 1);
        SendRewardCountOfOneWeekToUser(capturedUser);
    });
    CDX_PLEDGE_MISSION_COUNT_OF_ONE_WEEK_RESET packet(static_cast<int>(mutableUser->GetDBID()), job);
    db.RequestTPacketProcess(requestWorldId, static_cast<short>(0x73), static_cast<int>(sizeof(packet)), packet);
}

void UserPledgeMission::IntervalCheckOneWeek(const User* const user) {
    // Current L2Server.exe 0x009b9098.
    if (tNextWeekStartTime_ < runtime::crt::Time32Now()) {
        ResetRewardCountOfOneWeek(user);
    }
}

void UserPledgeMission::BuilderCmdShowInfo(const User* const user) {
    // Current L2Server.exe 0x009b917c.
    L2_GUARD_FUNCTION();
    for (const MissionInfoMap& missionMap : mUserPledgeMissionInfoMap_) {
        for (const auto& entry : missionMap) {
            const UserPledgeMissionInfo& mission = entry.second;
            PledgeStuff::SendSystemMessage(
                const_cast<User*>(user),
                FormatMissionBuilderMessage(
                    entry.first, runtime::raw_atomic::LoadI32Relaxed(&mission.nCount_),
                    mission.mPledgeMissionInfo_->nGoalCount_, runtime::raw_atomic::LoadI32Relaxed(&mission.nState_)
                )
            );
        }
    }
}

void UserPledgeMission::BuilderCmdShowInfoWithMissionId(const User* const user, const int missionId) {
    // Current L2Server.exe 0x009b93d0.
    L2_GUARD_FUNCTION();
    UserPledgeMissionInfo* const mission = Find(missionId);
    if (mission == nullptr) {
        return;
    }
    PledgeStuff::SendSystemMessage(
        const_cast<User*>(user),
        FormatMissionBuilderMessage(
            missionId, runtime::raw_atomic::LoadI32Relaxed(&mission->nCount_), mission->mPledgeMissionInfo_->nGoalCount_,
            runtime::raw_atomic::LoadI32Relaxed(&mission->nState_)
        )
    );
}

void UserPledgeMission::BuilderCmdSetCount(const User* const user, const int missionId, const int count) {
    // Current L2Server.exe 0x009b94e4. Callback 0x009bc1c4.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x45c,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x45c,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot set clan mission %d count to %d through the builder command: the user is unavailable.", missionId,
            count
        );
        return;
    }
    if (runtime::raw_atomic::LoadI32Relaxed(&nLoaded_) == 0) {
        return;
    }

    User* const mutableUser = const_cast<User*>(user);
    const std::uint32_t requesterSid = mutableUser->Id();
    const int requesterDatabaseId = static_cast<int>(mutableUser->GetDBID());
    const int requesterWorldId = user->m_data->GetWorldId();
    const UserCapture capture{requesterSid, requesterWorldId, requesterDatabaseId};

    UserPledgeMissionInfo* const mission = Find(missionId);
    if (mission == nullptr || count > mission->mPledgeMissionInfo_->nGoalCount_) {
        return;
    }

    const int requestWorldId = user->m_data->GetWorldId();
    AtomicLambdaJobDx* const job = MakeLambdaJobDx([capture, this](AtomicLambdaJobDx* const callback) {
        // Current L2Server 0x009bc1c4.
        static constexpr wchar_t functionName[] = L"PledgeMission.cpp(1134)";
        L2_GUARD_PROFILE(functionName);
        GuardHelper stackGuard(functionName, CThreadManager::GetThreadID());
        User* const capturedUser = capture.GetUser();
        if (capturedUser == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"PledgeMission.cpp",
            //     0x474)
            // Noise: Builder mission-count update can complete after the captured user has left. This callback returns
            // without publishing user state or sending a reply. L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"PledgeMission.cpp",
            //     0x474)
            return;
        }
        const auto* const response = static_cast<CDX_PLEDGE_MISSION_SET_COUNT*>(callback->m_pDx);
        UserPledgeMissionInfo* const capturedMission = Find(response->nMissionId_);
        if (capturedMission == nullptr) {
            return;
        }

        capturedMission->SetCount(response->nCount_);
        if (response->nCount_ == 0) {
            capturedMission->tRewardTime_ = 0;
        }
        CheckAndUpdateState(capturedUser, response->nMissionId_, *capturedMission);
        runtime::raw_atomic::ExchangeI32(&nNeedUpdate_[1], 1);
        if (runtime::raw_atomic::LoadI32Relaxed(&capturedMission->nState_) == PledgeMission::PMS_CAN_REWARD) {
            SendToUserIfChanged(capturedUser);
        }
    });
    CDX_PLEDGE_MISSION_SET_COUNT packet(static_cast<int>(mutableUser->GetDBID()), missionId, count, job);
    db.RequestTPacketProcess(requestWorldId, static_cast<short>(0x70), static_cast<int>(sizeof(packet)), packet);
}

bool UserPledgeMission::CheckReward(User* const user, const int missionId, UserPledgeMissionInfo* const missionInfo) {
    // Current L2Server.exe 0x009ba9d0.
    L2_GUARD_FUNCTION();
    static_cast<void>(missionId);
    if (runtime::raw_atomic::LoadI32Relaxed(&nLoaded_) == 0) {
        return false;
    }
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x2db,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x2db,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot check rewards for clan mission %d: the user is unavailable; reward eligibility is denied.",
            missionId
        );
        return false;
    }

    PledgeMissionInfo* const definition = missionInfo->mPledgeMissionInfo_;
    if (!definition->mRewardItemMap_.empty()) {
        ItemAmountDataStruct rewards;
        for (const auto& reward : definition->mRewardItemMap_) {
            rewards.Push(reward.first, reward.second);
        }
        if (!user->m_inv.CheckAddableMulti(rewards, false)) {
            user->SendSystemMessage(SystemMessageId::CANNOT_RECEIVE_REWARD_INVENTORY_WEIGHT_OR_QUANTITY_EXCEEDED);
            return false;
        }
    }

    if (definition->nRewardPVPPoint_ == 0) {
        return true;
    }
    if (!user->CanGetPVPPoint()) {
        user->SendSystemMessage(SystemMessageId::FAME_REQUIRES_LEVEL_40_AND_SECOND_CLASS_TRANSFER);
        return false;
    }
    return user->CheckPointAddable(PT_PVP_POINT, definition->nRewardPVPPoint_);
}

void UserPledgeMission::ProcessReward(User* const user, const int missionId, UserPledgeMissionInfo* const missionInfo) {
    // Current L2Server.exe 0x009bae98.
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::LoadI32Relaxed(&nLoaded_) == 0) {
        return;
    }
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x305,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x305,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot process rewards for clan mission %d: the user is unavailable.", missionId);
        return;
    }

    PledgeMissionInfo* const definition = missionInfo->mPledgeMissionInfo_;
    std::array<ItemManip, 100> manipulations{};
    int itemCount = 0;
    for (const auto& reward : definition->mRewardItemMap_) {
        manipulations[static_cast<std::size_t>(itemCount)].mClassId = reward.first;
        manipulations[static_cast<std::size_t>(itemCount)].mCount = reward.second;
        ++itemCount;
    }

    CPacketHelperEx<20480> packet;
    int etcManiCount = 0;
    if (definition->nRewardPVPPoint_ != 0) {
        CETC_MANI_POINT_UPDATE pointUpdate(
            user->Id(), static_cast<int>(user->GetDBID()), PT_PVP_POINT, definition->nRewardPVPPoint_
        );
        static_cast<EtcMani&>(pointUpdate).Assemble(packet);
        ++etcManiCount;
    }

    CPledge* const pledge = user->GetPledge();
    if (definition->nRewardPledgeNameValue_ != 0 && pledge != nullptr) {
        EtcManiUpdatePledgeNameValue nameValueUpdate(
            user->m_data->GetWorldId(), static_cast<int>(user->Id()), pledge->GetDbId(), definition->nRewardPledgeNameValue_
        );
        nameValueUpdate.Assemble(packet);
        ++etcManiCount;
    }

    CETC_MANI_PLEDGE_MISSION_REWARD missionReward(
        static_cast<int>(user->GetDBID()), missionId, runtime::raw_atomic::LoadI32Relaxed(&missionInfo->nCount_),
        missionInfo->tRewardTime_
    );
    static_cast<EtcMani&>(missionReward).Assemble(packet);
    ++etcManiCount;

    User::etcMsg message = user->SaveEtcMsg(false, false, 0, 0, 0, std::u16string(), std::u16string(), 0);
    User::ManipulateQueue queue = user->SaveManipulateQueue(
        ManipulateType::MANI_PLEDGE_MISSION_REWARD, 0, itemCount, manipulations.data(), nullptr, etcManiCount, packet, false,
        ItemGetReason::PledgeMissionReward, message
    );

    const User::ManiItemResultType result = user->_SetManiItemJob(queue);
    if (result == User::MRT_NOJOB) {
        if (!user->_HandleItemManipulation(queue)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d][Mission][%d] "
            //     L"_HandleItemManipulation failed",
            //     L"PledgeMission.cpp",
            //     0x33e,
            //     missionId)
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d][Mission][%d] "
            //     L"_HandleItemManipulation failed",
            //     L"PledgeMission.cpp",
            //     0x33e,
            //     missionId)
            L2_LOG_ERROR(
                Log, "Clan mission %d: the reward item operation failed; the user receives the inventory-limit message.",
                missionId
            );
            user->SendSystemMessage(SystemMessageId::CANNOT_RECEIVE_ITEM_INVENTORY_WEIGHT_OR_QUANTITY_EXCEEDED);
        }
    } else if (result == User::MRT_JOBMAP) {
        user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        PledgeStuff::DestroyJob(queue.pAtomicJob);
    } else if (result != User::MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d][Mission][%d] "
        //     L"ManipulateReturnType::Enum : default",
        //     L"PledgeMission.cpp",
        //     0x353,
        //     missionId)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d][Mission][%d] "
        //     L"ManipulateReturnType::Enum : default",
        //     L"PledgeMission.cpp",
        //     0x353,
        //     missionId)
        L2_LOG_ERROR(Log, "Clan mission %d: unexpected reward item-queue result %d.", missionId, static_cast<int>(result));
    }
}

void UserPledgeMission::Reward(const User* const user, const int missionId) {
    // Current L2Server.exe 0x009bbe10.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x2a4,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x2a4,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot request rewards for clan mission %d: the user is unavailable.", missionId);
        return;
    }
    if (runtime::raw_atomic::LoadI32Relaxed(&nLoaded_) == 0) {
        return;
    }
    User* const mutableUser = const_cast<User*>(user);

    UserPledgeMissionInfo* const missionInfo = Find(missionId);
    if (missionInfo == nullptr) {
        return;
    }
    CheckAndUpdateState(user, missionId, *missionInfo);
    if (runtime::raw_atomic::LoadI32Relaxed(&missionInfo->nState_) != PledgeMission::PMS_CAN_REWARD) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d][Mission][%d] Reward Error "
        //     L"user db Id : %d, State : %d",
        //     L"PledgeMission.cpp",
        //     0x2c8,
        //     missionId,
        //     static_cast<int>(mutableUser->GetDBID()),
        //     runtime::raw_atomic::LoadI32Relaxed(
        //         &missionInfo->nState_))
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d][Mission][%d] Reward Error "
        //     L"user db Id : %d, State : %d",
        //     L"PledgeMission.cpp",
        //     0x2c8,
        //     missionId,
        //     static_cast<int>(mutableUser->GetDBID()),
        //     runtime::raw_atomic::LoadI32Relaxed(
        //         &missionInfo->nState_))
        L2_LOG_WARN(
            Log,
            "Character %u cannot claim clan mission %d rewards: the mission is not ready for reward (observed state %d, "
            "required %d); mission information is refreshed.",
            static_cast<unsigned>(mutableUser->GetDBID()), missionId,
            runtime::raw_atomic::LoadI32Relaxed(&missionInfo->nState_), static_cast<int>(PledgeMission::PMS_CAN_REWARD)
        );
        mutableUser->SendSystemMessage(SystemMessageId::CLAN_DEVELOPMENT_SYSTEM_ERROR);
        runtime::raw_atomic::ExchangeI32(&missionInfo->nNeedUpdateInfo_[1], 1);
        runtime::raw_atomic::ExchangeI32(&nNeedUpdate_[1], 1);
        SendToUserIfChanged(user);
        return;
    }

    if (static_cast<std::int32_t>(missionInfo->mPledgeMissionInfo_->eCategory_) == 1 &&
        runtime::raw_atomic::LoadI32Relaxed(&nRewardCountOfOneWeek_) >=
            CalculateMaxRwardCountOfOneWeek(user->m_data->m_nNoblessType)) {
        return;
    }

    JobTransactionLong jobType = 1;
    if (!rewardLock_.BeginJob(&jobType)) {
        mutableUser->SendSystemMessage(SystemMessageId::CLAN_DEVELOPMENT_SYSTEM_ERROR);
        return;
    }
    if (CheckReward(mutableUser, missionId, missionInfo)) {
        ProcessReward(mutableUser, missionId, missionInfo);
    }
    if (!rewardLock_.EndJob()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Failed to end job at %s(%d)",
        //     L"PledgeMission.cpp",
        //     0x2c4)
        // L2_LOG_ERROR(Log,
        //     L"Failed to end job at %s(%d)",
        //     L"PledgeMission.cpp",
        //     0x2c4)
        L2_LOG_ERROR(
            Log, "Character %u: clan mission %d reward processing reached cleanup with no active reward transaction.",
            static_cast<unsigned>(mutableUser->GetDBID()), missionId
        );
    }
}

void UserPledgeMission::SuccessReward(User* const user, const int missionId, const std::int32_t rewardTime) {
    // Current L2Server.exe 0x009bc8e8.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x360,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeMission.cpp",
        //     0x360,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot publish successful clan mission %d rewards: the user is unavailable.", missionId);
        return;
    }

    UserPledgeMissionInfo* const missionInfo = Find(missionId);
    if (missionInfo == nullptr) {
        return;
    }
    PledgeMissionInfo* const definition = missionInfo->mPledgeMissionInfo_;
    CLogFormatter formatter;
    formatter.WriteLog4(
        0x1fed, user, nullptr, nullptr, nullptr, nullptr, missionId, static_cast<std::int32_t>(definition->eCategory_),
        static_cast<std::int32_t>(definition->eGoalType_), definition->bRepeat_, definition->nGoalCount_,
        runtime::raw_atomic::LoadI32Relaxed(&missionInfo->nCount_), missionInfo->tRewardTime_,
        definition->nRewardPledgeNameValue_, definition->nRewardPVPPoint_, 0, 0
    );
    for (const auto& reward : definition->mRewardItemMap_) {
        formatter.WriteLog4(
            0x1fee, user, nullptr, nullptr, nullptr, nullptr, missionId, static_cast<std::int32_t>(definition->eCategory_),
            static_cast<std::int32_t>(definition->eGoalType_), definition->bRepeat_, definition->nGoalCount_,
            runtime::raw_atomic::LoadI32Relaxed(&missionInfo->nCount_), missionInfo->tRewardTime_, reward.first,
            reward.second, 0, 0
        );
    }

    missionInfo->SetCount(0);
    missionInfo->tRewardTime_ = rewardTime;
    if (definition->bRepeat_ && static_cast<std::int32_t>(definition->eCategory_) == 1) {
        IncrementRewardCountOfOneWeek(user);
        SendRewardCountOfOneWeekToUser(user);
    }
    CheckAndUpdateState(user, missionId, *missionInfo);
    CheckAndAddCount(user, PledgeMission::PMGT_MISSION_SUCCESS, missionId, 0, 1);
    runtime::raw_atomic::ExchangeI32(&nNeedUpdate_[1], 1);
    SendToUserIfChanged(user);
}
