#pragma once

#include "Object.h"

#include "Shared/src/BaseStructure.h"
#include "Shared/src/DateTime.h"
#include "Shared/src/IOC.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

class CArea;
class CCreature;
class CSocket;
class CTerritory;
class User;
class FieldCycleActivationMessage;
class FieldCycleInitialActivationMessage;
class IPacketHelper;

namespace FieldCycleReason {

enum Value : std::int32_t {
    ByPointChange = 0,
    ByNPCAI_1 = 1,
    ByNPCAI_2 = 2,
    ByNPCAI_3 = 3,
    ByNPCAI_4 = 4,
    ByNPCAI_5 = 5,
    ByBuilder = 6,
    ByMultiSell = 7,
    ByNPCAI_8 = 8,
    ByTimeInRange = 100,
    ByExpirationTime = 101,
    ByMissionLimitTime = 102,
    ByMissionComplete = 103,
    ByUnlockAccumulate = 104,
    ByLockExpired = 105,
    ByPointTimer = 106,
    ByNpcSetPoint = 107,
    ByUnitTest = 108,
    ByDropTime = 109,
};

} // namespace FieldCycleReason


namespace FieldCycleConst {

extern std::int32_t _CYCLE_HEARTBEAT;

} // namespace FieldCycleConst

struct StepChangeTime {
    std::int32_t day = -1;
    std::int32_t hour = -1;
    std::int32_t min = -1;
    std::int32_t targetPadding = 0;
};


struct RestartInfo {
    FVector normalPoint;
    FVector chaoPoint;
    CTerritory* pTerritory = nullptr;
};


// Current parser_fieldCycle allocates 0xc8 bytes and writes the tail at
// +0x58/+0x60/+0x78/+0x80/+0xa0/+0xc0. TotalChecker's 0xb0 StepInfo is an
// older container layout. The eight bytes at +0x50 are target alignment /
// revision storage which current code never reads or initializes.
struct StepInfo {
    StepInfo();
    ~StepInfo();

    char stepNumber = 0; // +0x00
    std::array<std::byte, 3> padding001{};
    std::int32_t lockTime = 0; // +0x04
    std::int32_t dropTime = 0; // +0x08
    std::int32_t intervalTime = 0; // +0x0c
    std::int32_t intervalPoint = 0; // +0x10
    std::int32_t mapStringNo = 0; // +0x14
    StepChangeTime stepChangeTime{}; // +0x18
    L2WeeklyTimeCalculator stepChangeTimeInRange; // +0x28
    std::array<std::byte, 8> currentPadding050; // +0x50
    std::int32_t expirationTime = 0; // +0x58
    std::array<std::byte, 4> padding05c{};
    FVector mapPoint; // +0x60
    std::int32_t mapHuntingZoneID = 0; // +0x78
    std::array<std::byte, 4> padding07c{};
    std::vector<void*> openDoorList; // +0x80, CDoor*
    std::vector<CArea*> areaOnList; // +0xa0
    RestartInfo* restartInfo = nullptr; // +0xc0
};

class StepInfoGroup {
public:
    StepInfoGroup();
    ~StepInfoGroup();

    void Reset();
    StepInfo* GetStepInfo(int step);
    int GetPoint(int step) const;
    int GetGoalPoint(int step) const;
    bool AddStep(int step, int point, StepInfo* info);

    std::vector<std::int32_t> m_pointList; // +0x00
    std::vector<StepInfo*> m_stepList; // +0x20
    std::int32_t m_maxStep = -1; // +0x40
    std::array<std::byte, 4> padding044{};
};

class FieldCycleUser : public MemoryObject {
public:
    FieldCycleUser();
    ~FieldCycleUser() override;

    L2_DECLARE_MEMORY_ALLOCATION(FieldCycleUser)

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    CSmartID m_idUser; // +0x18
    std::array<std::byte, 4> padding01c{};
};

class FieldCycleMissionUsers {
public:
    FieldCycleMissionUsers() = default;
    ~FieldCycleMissionUsers() = default;

    void Reset();
    void Register(std::uint32_t userId, const CSmartID& idUser);
    bool Unregister(std::uint32_t userId);
    bool IsRegistered(std::uint32_t userId);
    void BroadcastToAll(DefaultIOBuffer* buffer);
    void NotifyFieldEventStepToAll(int step, int goalPoint, int remainSecond);
    void NotifyFieldEventPointToAll(int point);

private:
    CObjectMap<FieldCycleUser, 32, 5, RWLock, int> m_mapUsers;
};

class CFieldCycle : public CIOObject {
public:
    struct MissionMode {
        std::int32_t firstStep = 0;
        std::int32_t limitTime = 0;
        std::int32_t successStep = 0;
        std::int32_t failStep = 0;
    };

    explicit CFieldCycle(int fieldCycleId);
    ~CFieldCycle() override;

    L2_DECLARE_MEMORY_ALLOCATION(CFieldCycle)

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void TimerExpired(int id) override;

    void PostLoad();
    void LoadFromDBContinue(
        int newStep,
        int newPoint,
        std::int32_t stepChangedTime,
        std::int32_t pointChangedTime,
        int accumulatedPoint,
        std::int32_t missionStartedTime
    );
    bool AddStep(int step, int point, StepInfo* info);
    void SetMissionStartStep(int step);
    void SetMissionLimitTime(int seconds);
    void SetMissionSuccessStep(int step);
    void SetMissionFailedStep(int step);
    void SetMissionArea(std::vector<CArea*> areas);
    void SetExpirationTime(std::int32_t seconds);
    bool ChangeStep(int step, FieldCycleReason::Value reason);
    bool ChangeNextStep();
    void TryStepExpire(int step, int day, int hour, int minute);
    void SetMissionResult(bool success, FieldCycleReason::Value reason);
    void SetPoint(int point, FieldCycleReason::Value reason);
    void AddPoint(int point, FieldCycleReason::Value reason, CCreature* creature);
    FVector GetRestartPoint(FVector& position, bool chao);
    SharedFieldCycleInfo& GetMyInfo();
    void BroadcastToAll(DefaultIOBuffer* buffer);
    void AssembleMapInfoPacket(IPacketHelper& packet, int& count);
    void SendDebugInfoTo(CSocket* socket);
    bool ContainPosition(const FVector& position) const;
    bool IsMissionInProgress() const;
    void OnUserEnter(User* user);
    void OnUserCompleteQuest(User* user);
    void OnUserTimerExpired(User* user);
    void OnUserLeave(User* user);

private:
    void _SaveStatus();
    bool _IsMissionInProgress() const;
    void _GiveMissionQuestTo(User* user, int currentQuestId) const;
    void _OnTimer_TIMER_NOTIFY_POINT();
    void _OnStepChanged(int previousStep, std::int32_t changedTime, bool resetPoint, FieldCycleReason::Value reason);
    void _ActivateDoorAndArea(int previousStep);
    void _OnTimerForStepChangeTime(std::int32_t now);
    bool _ChangeStep(int step, FieldCycleReason::Value reason, bool resetPoint);
    bool _ChangeNextStep(FieldCycleReason::Value reason);
    void _ChangePrevStep(FieldCycleReason::Value reason);
    void _OnTimerForMissionLimitTime(std::int32_t now);
    void _AddPoint(int point, FieldCycleReason::Value reason, CCreature* creature);
    void _OnPointChanged(FieldCycleReason::Value reason);
    void _OnLockTimeExpired();
    void _OnTimer_ADD_POINT();
    void _OnTimerForExpireTime(std::int32_t now);
    void _OnTimerForStepChangeTimeInRange(std::int32_t now);
    void _OnTimer_CYCLE_TIMER();

    std::int32_t m_fieldCycleID = 0; // +0x18
    [[maybe_unused]] std::array<std::byte, 4> padding01c{};
    std::unique_ptr<MissionMode> m_pMissionMode; // +0x20
    std::unique_ptr<StepInfoGroup> m_pStepInfos; // +0x28
    std::int32_t m_pointChangedTime = 0; // +0x30
    std::int32_t m_accumulatedPoint = 0; // +0x34
    bool m_lockTimeLeft = false; // +0x38
    bool m_intervalTimeLeft = false; // +0x39
    [[maybe_unused]] std::array<std::byte, 2> padding03a{};
    std::int32_t m_somethingChanged = 0; // +0x3c
    bool m_stepChangeEventFired = false; // +0x40
    [[maybe_unused]] std::array<std::byte, 3> padding041{};
    std::int32_t m_tStepChangeTime = 0; // +0x44
    CYieldLock m_cycleLock; // +0x48
    std::int32_t m_nPrevPointAtNotify = 0; // +0x58
    [[maybe_unused]] std::array<std::byte, 4> padding05c{};
    std::unique_ptr<FieldCycleMissionUsers> m_pMissionUsers; // +0x60
    std::vector<CArea*> m_Areas; // +0x68
};

class CFieldCycleSystem : public MemoryObject {
public:
    CFieldCycleSystem();
    ~CFieldCycleSystem() override;

    L2_DECLARE_MEMORY_ALLOCATION(CFieldCycleSystem)

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    CFieldCycle* GetFieldCycle(int fieldCycleId);
    void SendDebugInfoTo(CSocket* socket);
    void SendDebugInfoTo(int fieldCycleId, CSocket* socket);
    void ChangeStep(int fieldCycleId, int step, FieldCycleReason::Value reason, CCreature* creature, CSocket* socket);
    void TryStepExpire(int fieldCycleId, int step, int day, int hour, int minute);
    void SetMissionResult(int fieldCycleId, bool success, FieldCycleReason::Value reason);
    void SetPoint(int fieldCycleId, int point, FieldCycleReason::Value reason, CSocket* socket);
    void AddPoint(int fieldCycleId, int point, FieldCycleReason::Value reason, CCreature* creature, CSocket* socket);
    void BroadcastTo(int fieldCycleId, DefaultIOBuffer* buffer);
    void Load();
    void RequestFieldCycleInfo();
    void Reset();
    void PostLoad();
    void LoadFromDBContinue(
        int fieldCycleId,
        int newStep,
        int newPoint,
        std::int32_t stepChangedTime,
        std::int32_t pointChangedTime,
        int accumulatedPoint,
        std::int32_t missionStartedTime
    );
    bool AddField(int fieldCycleId, CFieldCycle* fieldCycle);
    CFieldCycle* FindFieldCycleByArea(const FVector& position) const;
    FVector GetRestartPoint(FVector position, bool chao);
    int GetFieldCount() const;
    void SendMapInfoTo(User* user);
    void OnUserEnterWorld(User* user);
    void OnUserLeaveWorld(User* user);
    void OnUserTeleport(User* user);
    void OnUserTimerExpired(User* user);
    void OnUserCompleteQuest(User* user, int questId);
    int GetFieldCycleStep(int fieldCycleId);

private:
    std::vector<CFieldCycle*> m_fieldCycleList; // +0x18
    std::vector<CFieldCycle*> m_vMissionFields; // +0x38
};


extern CFieldCycleSystem g_FieldCycleSystem;
