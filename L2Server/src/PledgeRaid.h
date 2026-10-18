#pragma once

#include "PledgeRaidNetwork.h"

#include "Shared/src/IOC.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/Utils.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

class CPledge;
class User;
class WOlympiadSocketHost;
class WOlympiadSocketPeer;

// Current PledgeRaid.cpp retry record. The vector stride at +0xd8 is 0x18
// in the current executable (all three fields are copied by the retry and
// acknowledgement paths).
struct PledgeRaidPoint {
    int nPledgeDbId; // +0x00
    std::int32_t targetPadding04; // +0x04
    std::int64_t nPoint; // +0x08
    std::int32_t tSaveTime; // +0x10
    std::int32_t targetPadding14; // +0x14
};


// Exact current PledgeRaidSystem owner. Unlike the older TotalChecker
// donor, current xmap/xvector/RWLock extents produce a physical size of 0xf8.
class PledgeRaidSystem : public CIOObject, public Singleton<PledgeRaidSystem> {
public:
    L2_DECLARE_OBJECT_REGISTRATION(PledgeRaidSystem)
    static constexpr std::int64_t kHeartbeatMilliseconds = 30000;

    PledgeRaidSystem();
    ~PledgeRaidSystem() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void TimerExpired(int id) override;

    bool Init();
    void Clear();
    void LoadFromDB(bool reward);
    void LoadInzoneRestrictFromDB();
    void LoadPrevSeasonPledgeRaid();
    void CheckAndReward();
    void RetrySetPoint();
    void ProcessNextSeason(bool reward);

    int CalculateStep(std::int32_t currentTime, std::int32_t nextSeasonStartTime) const;
    std::int32_t CalculateNextSeasonStartTime(std::int32_t seasonStartTime) const;
    int CalculateTier(int rank, int totalCount, int percentile) const;

    bool CheckPledgeRaidInzoneBinding(User* user);
    void ForceSetPledgeRaidInzoneBinding(User* user, std::int32_t bindingTime);
    void SetPledgeRaidInzoneBinding(User* user, std::int32_t bindingTime);
    void SetPledgeRaidInzoneBindingAllUserInPledge(CPledge* pledge, bool force);
    void ResetInzoneRestrict(const char16_t* pledgeName);

    void SaveToDB(int season, std::int32_t startTime);
    void ResetSeason(std::int32_t resetTime);
    void ResetSeasonBuilderCommand();
    void DecideSeason(const User* user, std::int32_t delay);
    void SetPoint(unsigned int npcSid, int pledgeDbId, int pledgeIndex, std::int64_t point);

    void SendAllToUser(const User* user, bool currentSeason);
    void SendInfoToUser(const User* user, bool currentSeason);
    void SendRankInfoToUser(const User* user, bool currentSeason);
    void ShowInfo(const User* user);
    void ShowInfoList(const User* user, int season);
    void ShowInfoWithPledgeName(const User* user, const char16_t* pledgeName, int season);

    bool Process_OSA_PLEDGE_RAID_GET_SEASON(WOlympiadSocketHost* socket, const unsigned char* packet);
    bool Process_OCN_PLEDGE_RAID_DECIDE(WOlympiadSocketPeer* socket, const unsigned char* packet);
    bool Process_OCN_PLEDGE_RAID_RESET_SEASON(WOlympiadSocketPeer* socket, const unsigned char* packet);
    bool Process_OSA_PLEDGE_RAID_SET_INFO(WOlympiadSocketHost* socket, const unsigned char* packet);
    bool Process_OSA_PLEDGE_RAID_GET_PREV_SEASON_INFO_LIST(WOlympiadSocketHost* socket, const unsigned char* packet);

private:
    int bCanReward_; // +0x20
    int bLoaded_; // +0x24
    JobTransactionLock updateSeasonLock_; // +0x28
    CYieldLock loadLock_; // +0x34
    [[maybe_unused]] std::byte targetPadding44To48_[4]{}; // +0x44
    RWLock lockInzoneRestrictMap_; // +0x48
    std::map<int, std::int32_t> mInzoneRestrictMap_; // +0x80
    RWLock lockPointList_; // +0xa0
    std::vector<PledgeRaidPoint> vPointList_; // +0xd8

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(PledgeRaidSystem)
};
