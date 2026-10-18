#pragma once

#include "Protocol/src/L2Constants.h"
#include "Shared/src/PledgeMasteryID.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"

#include <cstddef>
#include <cstdint>

class User;

namespace PledgeMission {

// The current image fixes this field to one signed DWORD. Its value domain is
// not needed by the closed consumers below and remains opaque.
enum Category : std::int32_t;

} // namespace PledgeMission

class IMissionCondition {
public:
    virtual ~IMissionCondition();
    virtual bool Check(User* user) = 0;
};

class IMissionGoalCondition {
public:
    virtual ~IMissionGoalCondition();
    virtual bool Check(int value) = 0;
};

class UserInPledge_MissionCondition final : public IMissionCondition {
public:
    bool Check(User* user) override;
};

class UserJob_MissionCondition final : public IMissionCondition {
public:
    UserJob_MissionCondition(bool mainJob, bool dualJob, bool subJob);
    bool Check(User* user) override;

private:
    bool bMainJob_; // +0x08
    bool bDualJob_; // +0x09
    bool bSubJob_; // +0x0a
};

class UserMinLevel_MissionCondition final : public IMissionCondition {
public:
    explicit UserMinLevel_MissionCondition(int minimumLevel);
    bool Check(User* user) override;

private:
    std::int32_t nMinLevel_; // +0x08
};

class UserMaxLevel_MissionCondition final : public IMissionCondition {
public:
    explicit UserMaxLevel_MissionCondition(int maximumLevel);
    bool Check(User* user) override;

private:
    std::int32_t nMaxLevel_; // +0x08
};

class PledgeLevel_MissionCondition final : public IMissionCondition {
public:
    explicit PledgeLevel_MissionCondition(int minimumLevel);
    bool Check(User* user) override;

private:
    std::int32_t nMinLevel_; // +0x08
};

class PledgeMastery_MissionCondition final : public IMissionCondition {
public:
    explicit PledgeMastery_MissionCondition(const char16_t* masteryName);
    bool Check(User* user) override;

private:
    PledgeMasteryID::Type nType_; // +0x08
};

class CompleteMission_MissionCondition final : public IMissionCondition {
public:
    explicit CompleteMission_MissionCondition(int missionId);
    bool Check(User* user) override;

private:
    std::int32_t nMissionId_; // +0x08
};

class StartEndDateTime_MissionCondition final : public IMissionCondition {
public:
    StartEndDateTime_MissionCondition(int startDate, int startTime, int endDate, int endTime);
    bool Check(User* user) override;

private:
    std::int32_t tStartDateTime_; // +0x08
    std::int32_t tEndDateTime_; // +0x0c
};

// MSVC's target tm is nine consecutive 32-bit integers. Keeping that
// physical carrier avoids substituting the wider host libc tm on POSIX.
struct PledgeMissionTm32 {
    std::int32_t tm_sec;
    std::int32_t tm_min;
    std::int32_t tm_hour;
    std::int32_t tm_mday;
    std::int32_t tm_mon;
    std::int32_t tm_year;
    std::int32_t tm_wday;
    std::int32_t tm_yday;
    std::int32_t tm_isdst;
};

class StartEndTime_MissionCondition final : public IMissionCondition {
public:
    StartEndTime_MissionCondition(int startTime, int endTime);
    bool Check(User* user) override;

private:
    PledgeMissionTm32 tmStartTime_; // +0x08
    PledgeMissionTm32 tmEndTime_; // +0x2c
};

class AvailableDay_MissionCondition final : public IMissionCondition {
public:
    explicit AvailableDay_MissionCondition(const bool availableDay[7]);
    bool Check(User* user) override;

private:
    bool bAvailableDay_[7]; // +0x08
};

class IsInIntList_MissionCondition final : public IMissionGoalCondition {
public:
    IsInIntList_MissionCondition() = default;
    bool Check(int value) override;

    std::vector<std::int32_t> vIntList_; // +0x08
};

namespace PledgeMissionStuff {

PledgeMission::GoalType ConvertGoalTypeStringToEnum(const char16_t* value);

} // namespace PledgeMissionStuff

struct PledgeMissionInfo {
    PledgeMissionInfo();
    ~PledgeMissionInfo();

    PledgeMissionInfo& operator=(const PledgeMissionInfo& other);

    PledgeMission::Category eCategory_; // +0x00
    bool bRepeat_; // +0x04
    std::vector<IMissionCondition*> vMissionConditionList_; // +0x08
    PledgeMission::GoalType eGoalType_; // +0x28
    std::vector<IMissionGoalCondition*> vMissionGoalConditionList_; // +0x30
    std::int32_t nGoalCount_; // +0x50
    std::int32_t nRewardPledgeNameValue_; // +0x54
    std::int32_t nRewardPVPPoint_; // +0x58
    ::std::map<std::int32_t, std::int32_t> mRewardItemMap_; // +0x60
};

struct PledgeMissionCountOfOneWeekInfo {
    std::int32_t nNovice_;
    std::int32_t nNobless_;
    std::int32_t nHonors_;
};

class PledgeMissionDB : public ::Singleton<PledgeMissionDB> {
public:
    using MissionInfoMap = ::std::map<std::int32_t, PledgeMissionInfo*>;

    PledgeMissionDB();
    ~PledgeMissionDB() override;

    void Load();

    PledgeMissionCountOfOneWeekInfo mPledgeMissionCountOfOneWeekInfo_; // +0x08
    MissionInfoMap mPledgeMissionInfoMap_; // +0x18
};
