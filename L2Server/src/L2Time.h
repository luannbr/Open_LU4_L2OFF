#pragma once

#include "Shared/src/IOC.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

class CTerritory;

struct RqPledgeInfo {
    std::uint32_t userServerId;
    std::uint32_t emblemDatabaseId;
    std::uint32_t worldId;
    std::uint32_t pledgeServerId;
    std::uint32_t userDatabaseId;
};

struct DynamicTerritoryInfo {
    std::int32_t expirationTime;
    std::int32_t containerId;
    CTerritory* territory;
};


// Current L2Server RTTI fixes CL2Time -> CIOObject at displacement zero.
// The typed data members below are fixed by the current constructor and by
// independent cross-owner reads/writes. The global is declared here but is
// not defined until the complete current Release/TimerExpired lifecycle is
// dependency-closed.
class CL2Time : public CIOObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CL2Time)

    CL2Time();
    ~CL2Time() override;

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    void Init();
    bool IsManorSettingEnableTime();
    void TaxAndIncomeUpdateCheck();
    bool UpdateNextManor();
    void ResetAllUserVoteData();
    void CheckVoteDayChanged();
    void NpcPingStart();
    int GetTime();
    void SetTime(unsigned int time);
    bool IsForceShillienMoon();
    void SetForceShillienMoon(bool force);
    void CheckForServerToUserPacketStat();
    void CheckResetWorldChat();
    void CheckDynamicTerritory();
    void CheckHourForPacketStat();
    void TimerExpired(int id) override;

    std::int32_t timeLastTaxChange; // +0x18
    std::int32_t timeLastIncomeUpdate; // +0x1c
    std::int32_t timeLastManorResetUpdate; // +0x20
    std::uint32_t m_nTime; // +0x24
    std::uint32_t m_nStartTime; // +0x28
    std::uint32_t m_nHour; // +0x2c
    std::uint32_t m_nRealHour; // +0x30
    std::uint32_t m_nInitTime; // +0x34
    std::uint32_t m_nLastCheckedMin; // +0x38
    std::int32_t m_nLastCheckedWeekChanged; // +0x3c
    std::int32_t m_nLastCheckedDayChanged; // +0x40
    std::int32_t m_nLastCheckedManorChanged; // +0x44
    std::int32_t m_nLastCheckedHourForPacketStat; // +0x48
    std::int32_t m_nLastCheckedForServerToUserPacketStat; // +0x4c
    bool m_bIsNight; // +0x50
    bool m_bIsSunRise; // +0x51
    bool m_bForceManorSet; // +0x52
    bool m_bForceShillienTime; // +0x53
};


extern CL2Time g_L2Time;
extern std::queue<RqPledgeInfo> g_RqEmblemQueue;
extern CYieldLock g_RqEmblemQueueLock;
extern std::queue<DynamicTerritoryInfo> g_DynamicTerritoryQueue;
extern CYieldLock g_DynamicTerritoryQueueLock;
extern bool g_bNewVote;
extern std::int32_t g_nNPCCountSlot;
extern volatile std::int32_t g_aNPCCount[15];
extern volatile std::int32_t g_nMaxNPCCount;

// L2Time.obj volatile long at current L2Server.exe 0x16658470. TimerExpired
// raises it whenever the live in-zone NPC count reaches a new maximum.
// Exception.obj reports the kept value in its object report.
extern volatile std::int32_t g_nMaxInZoneNPCCount;
extern volatile std::int32_t g_nInZoneNPCCount;
