#pragma once

#include "PledgeMissionInfo.h"

#include "Shared/src/Utils.h"

#include <cstddef>
#include <cstdint>

class UserPledgeMission;
class NpcSocket;

namespace PledgeMissionConst {

extern const int CHECK_ONE_WEEK_TICK_;
extern const int SEND_TO_DB_TICK_;

} // namespace PledgeMissionConst

namespace NpcSocketEx {

bool Handle_NC_EX_PLEDGE_MISSION_AI_CONTROL(NpcSocket* socket, const unsigned char* packet);

} // namespace NpcSocketEx

class UserPledgeMissionInfo {
public:
    explicit UserPledgeMissionInfo(PledgeMissionInfo* pledgeMissionInfo);
    UserPledgeMissionInfo(const UserPledgeMissionInfo& other);

    UserPledgeMissionInfo& operator=(const UserPledgeMissionInfo& other);

    bool CheckCompleteState();
    void SetState(PledgeMission::State state);
    void AddCount(int count);
    void SetCount(int count);

private:
    friend class UserPledgeMission;
    friend class CompleteMission_MissionCondition;
    friend bool NpcSocketEx::Handle_NC_EX_PLEDGE_MISSION_AI_CONTROL(NpcSocket*, const unsigned char*);

    volatile std::int32_t nCount_; // +0x00
    volatile std::int32_t nDeltaCount_; // +0x04
    volatile std::int32_t nState_; // +0x08
    std::int32_t tRewardTime_; // +0x0c
    PledgeMissionInfo* mPledgeMissionInfo_; // +0x10
    volatile std::int32_t nNeedUpdateInfo_[2]; // +0x18
};


class UserPledgeMission {
public:
    using MissionInfoMap = ::std::map<std::int32_t, UserPledgeMissionInfo>;

    UserPledgeMission();
    ~UserPledgeMission();

    void Init(const User* user);
    void Load(const User* user);
    void LoadCountOfOneWeek(const User* user);
    void CalculateCountOfOneWeek();
    bool CheckGoalCondition(UserPledgeMissionInfo& missionInfo, int firstValue, int secondValue);
    UserPledgeMissionInfo* Find(int missionId);
    bool Insert(int missionId, UserPledgeMissionInfo& missionInfo);
    int CalculateMaxRwardCountOfOneWeek(int userType);
    bool CheckCondition(const User* user, UserPledgeMissionInfo& missionInfo);
    bool CheckAndUpdateState(const User* user, int missionId, UserPledgeMissionInfo& missionInfo);
    bool CheckConditionAndAddCount(
        const User* user, int missionId, UserPledgeMissionInfo& missionInfo, int firstValue, int secondValue, int count
    );
    bool CheckAndAddCount(const User* user, PledgeMission::GoalType goalType, int firstValue, int secondValue, int count);
    void LoadingCompleted(const User* user);
    void CheckAndUpdateStateAll(const User* user);
    void SendToDBIfChanged(const User* user);
    void SendToUserIfChanged(const User* user);
    void SendRewardCountOfOneWeekToUserWithType(const User* user, int userType);
    void SendRewardCountOfOneWeekToUser(const User* user);
    void IncrementRewardCountOfOneWeek(const User* user);
    void IntervalCheckOneWeek(const User* user);
    void BuilderCmdShowInfo(const User* user);
    void BuilderCmdShowInfoWithMissionId(const User* user, int missionId);
    void BuilderCmdSetCount(const User* user, int missionId, int count);
    void ResetRewardCountOfOneWeek(const User* user);
    bool CheckReward(User* user, int missionId, UserPledgeMissionInfo* missionInfo);
    void ProcessReward(User* user, int missionId, UserPledgeMissionInfo* missionInfo);
    void Reward(const User* user, int missionId);
    void SuccessReward(User* user, int missionId, std::int32_t rewardTime);

private:
    MissionInfoMap mUserPledgeMissionInfoMap_[PledgeMission::PMGT_MAX]; // +0x000
    JobTransactionLock rewardLock_; // +0x520
    std::int32_t tWeekStartTime_; // +0x52c
    std::int32_t tNextWeekStartTime_; // +0x530
    volatile std::int32_t nRewardCountOfOneWeek_; // +0x534
    volatile std::int32_t nLoaded_; // +0x538
    volatile std::int32_t nNeedUpdate_[2]; // +0x53c
};
