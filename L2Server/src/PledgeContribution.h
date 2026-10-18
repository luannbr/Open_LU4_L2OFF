#pragma once

#include "Shared/src/DateTime.h"
#include "Shared/src/IOC.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/Utils.h"

#include <cstddef>
#include <cstdint>

class CPledge;
class User;
struct PledgePeriodRewardWeightInfo;
struct PledgeRewardInfo;

namespace PledgeContributionStuff {

void GivePeriodRewardTo(User* user, const PledgeRewardInfo* reward, const PledgePeriodRewardWeightInfo* weightInfo);
void GivePeriodRewardTo(User* user, int rank, int ratio, int activeMember);

} // namespace PledgeContributionStuff


// Current L2Server PledgeContribution.cpp manager. The secondary Singleton
// base occupies +0x18 and the target keeps eight alignment bytes between the
// weekly calculator and the three season dwords, producing the exact 0x70
// physical extent (the older TotalChecker object is only 0x68).
class PledgeContributionSystem : public CIOObject, public Singleton<PledgeContributionSystem> {
public:
    L2_DECLARE_OBJECT_REGISTRATION(PledgeContributionSystem)
    static constexpr std::int64_t kHeartbeatMilliseconds = 30000;

    struct Builder {
        static void AddPoint(PledgeContributionSystem* system, int userDatabaseId, int pledgeDatabaseId, int point);
        static void DecideSeason(PledgeContributionSystem* system, const User* user, std::int32_t delay);
        static void ResetInfo(PledgeContributionSystem* system, User* user);
        static void RetrieveRankInfo(PledgeContributionSystem* system, const User* user, bool current);
        static void ShowInfo(PledgeContributionSystem* system, const User* user);
    };

    PledgeContributionSystem();
    ~PledgeContributionSystem() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void TimerExpired(int id) override;

    bool Init();
    void SetupWeeklyTimerInfo();
    void LoadFromDB();
    void SaveRank();
    void ProcessRankDecision();
    void AddPoint(User* user, CPledge* pledge, int point);
    void RetrieveInfo(User* user);
    void RetrieveRank(User* user, bool current);
    void TakeTotalReward(User* user);
    void TakeTotalReward(User* user, int rewardedStep, int totalPoint);
    void TakePeriodReward(const User* user);

private:
    JobTransactionLock m_DecideRankLock; // +0x20
    [[maybe_unused]] std::byte m_alignment2cTo30[4]{}; // +0x2c
    L2WeeklyTimeCalculator m_WeeklyTimeCalculator; // +0x30
    [[maybe_unused]] std::byte m_alignment58To60[8]{}; // +0x58
    int m_nSeason; // +0x60
    std::int32_t m_tStartTime; // +0x64
    std::int32_t m_tNextSeasonTime; // +0x68
    [[maybe_unused]] std::uint32_t m_alignment6cTo70{}; // +0x6c

    inline static const wchar_t* m_pClassName = L"PledgeContributionSystem";
};
