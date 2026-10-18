#pragma once

#include "QuestBase.h"

#include "L2Core/src/inc/XStd.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/IOC.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

class CArea;
class CSocket;
class CEventCampaign;
class CEventCampaignStep;
class CEventCampaignStepMultiInfo;
class CParserForEventCampaign;
class User;

namespace EVENTCAMPAIGN {

class ExecutionPeriod {
public:
    ExecutionPeriod() : m_PeriodInfo{} {}

    bool CanExecute(std::int32_t currentTime) {
        return m_PeriodInfo.m_nStartTime <= currentTime && currentTime <= m_PeriodInfo.m_nEndTime;
    }

    ::TimePeriodInfo m_PeriodInfo;
};

static_assert(std::is_trivially_copyable_v<ExecutionPeriod>);
static_assert(std::is_trivially_destructible_v<ExecutionPeriod>);

} // namespace EVENTCAMPAIGN

struct EventCampaignUserScoreInfo {
    unsigned int m_UserDBID{};
    int m_Score{};
    std::u16string m_UserName;
};

struct EventCampaignStartTimeData {
    int m_StartTime{};
    int m_Dice{};
};

struct EventCampaignStartCondition {
    int m_UserMinLevel{};
    int m_UserMaxLevel{};
};

struct EventCampaignGoalScore {
    int m_GoalPoint{};
    int m_CurrentPoint{};
};

struct EventCampaignStartPointVec {
    int x{};
    int y{};
    int z{};
};

class CEventCampaignStepMultiInfo {
public:
    struct RewardItemDataType {
        unsigned int itemClassId{};
        std::int64_t itemCount{};
    };

    using GoalMap = std::map<unsigned int, EventCampaignGoalScore>;
    using RewardList = std::vector<RewardItemDataType>;

    explicit CEventCampaignStepMultiInfo(int id);

    void SetStartPosition(int x, int y, int z);
    void AllDataInit();
    void GiveStartItemToUser(User& user);
    void GiveRewardItemToUser(User& user);
    void GiveEliteRewardItemToUser(User& user);

private:
    friend class CEventCampaign;
    friend class CEventCampaignStep;
    friend class CParserForEventCampaign;

    int m_ID{}; // +0x000
    EventCampaignStartPointVec m_StartPoint{}; // +0x004
    std::u16string m_ContextHTMLName; // +0x010
    std::u16string m_RewardHTMLName; // +0x038
    std::u16string m_EliteRewardHTMLName; // +0x060
    std::u16string m_FailedHTMLName; // +0x088
    std::u16string m_RewardReceivedHTMLName; // +0x0b0
    GoalMap m_GoalMap; // +0x0d8
    RewardList m_RewardList; // +0x0f8
    RewardList m_EliteRewardList; // +0x118
    RewardList m_AcceptList; // +0x138
    int m_GoalCount{}; // +0x158
    int m_SuccessInfo{-1}; // +0x15c
};

class CEventCampaignStep {
public:
    using MakerList = std::vector<std::u16string>;
    using AreaList = std::vector<CArea*>;

    explicit CEventCampaignStep(int id);

    void SetActivateDuration(int minutes);
    void AllDataInit();
    void OnStartWorking(std::int32_t startTime);
    void ActivateNPCMaker();
    void ActivateArea();
    void DeactivateNPCMaker(bool reward);
    void DeactivateArea();
    void OnEndWorking(bool reward);
    void OnStartReward(std::int32_t startTime, int rewardPeriod, int successGroup);
    void OnStartFailed(std::int32_t startTime, int rewardPeriod);
    void OnTickWorking();
    void OnEndReward();
    void OnEndFail();
    int GetRandomGoalGroupId() const;
    int GetId() const { return m_StepID; }

private:
    friend class CEventCampaign;
    friend class CEventCampaignManager;
    friend class CParserForEventCampaign;

    std::u16string m_StartHTMLName; // +0x00
    std::array<CEventCampaignStepMultiInfo*, 5> m_MultiInfo{}; // +0x28
    CEventCampaignStep* m_PrevStep{}; // +0x50
    CEventCampaignStep* m_NextStep{}; // +0x58
    MakerList m_NPCMakerList; // +0x60
    AreaList m_AreaList; // +0x80
    std::array<int, 5> m_AdjustedGoalRate{}; // +0xa0
    int m_StepID{}; // +0xb4
    int m_ActivateDuration{}; // +0xb8
    std::int32_t m_StartTime{}; // +0xbc
    std::int32_t m_EndTime{}; // +0xc0
    std::byte m_AlignmentC4ToC8[4]{};
};

// Current EventCampaign.obj stores this concrete wrapper at
// CEventCampaign+0x1e180. A narrow static trampoline represents the target's
// eight-byte MSVC member-function pointer on portable ABIs.
class EventCampaignSerializer final {
public:
    using Executable = void (*)(CEventCampaign* owner, CExecutionArgumentPad* argument);

    using EmbeddedSerializer = CExecutionSerializer<EventCampaignSerializer, 4194304, CEventCampaign>;

    EventCampaignSerializer();

    virtual void AddExecutable(
        CEventCampaign* owner, Executable executable, CExecutionArgument* argument, const wchar_t* executableName
    );

private:
    EmbeddedSerializer m_Serializer; // +0x08
};


// Exact current physical campaign owner produced by the generated parser and
// consumed by the manager and LevelUp runtime route.
class CEventCampaign : public CIOObject {
public:
    enum State : int {
        PERIOD_SLEEP = 0,
        PERIOD_WORKING = 1,
        PERIOD_REWARD = 2,
        PERIOD_FAILED = 3,
    };

    using JoinUserMap = std::map<unsigned int, EventCampaignUserScoreInfo>;
    using RewardUserMap = std::map<unsigned int, EventCampaignUserScoreInfo>;
    using EliteUserList = std::vector<EventCampaignUserScoreInfo>;
    using StartTimeList = std::vector<EventCampaignStartTimeData>;

    L2_DECLARE_OBJECT_REGISTRATION(CEventCampaign)

    explicit CEventCampaign(int id);
    ~CEventCampaign() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void TimerExpired(int id) override;

    L2_ASYNC_DECLARE_FINAL_EMPTY_2_SLOT(__async_arg_AsyncStartEventCampaign);

    L2_ASYNC_DECLARE_FINAL_EMPTY_2_SLOT(__async_arg_AsyncStopEventCampaign);

    L2_ASYNC_DECLARE_FINAL_EMPTY_2_SLOT(__async_arg_AsyncTimerExpired);

    L2_ASYNC_DECLARE_FINAL_EMPTY_2_SLOT(__async_arg_AsyncStartNewStep);

    L2_ASYNC_DECLARE_FINAL_INLINE_FIELDS_1(__async_arg_AsyncUserEnterWorld, User*);

    L2_ASYNC_DECLARE_FINAL_INLINE_FIELDS_1(__async_arg_AsyncUserTryRequestCampaignHTML, User*);

    L2_ASYNC_DECLARE_FINAL_INLINE_FIELDS_1(__async_arg_AsyncTrySendProgressInfo, User*);

    L2_ASYNC_DECLARE_FINAL_EMPTY_2_SLOT(__async_arg_AsyncInitNPCMaker);

    L2_ASYNC_DECLARE_FINAL_INLINE_FIELDS_6(__async_arg_AsyncTryAddPoint, User*, int, int, int, int, int);

    L2_ASYNC_DECLARE_FINAL_INLINE_FIELDS_2(__async_arg_AsyncTryJoinEventCampaign, User*, int);

    L2_ASYNC_DECLARE_FINAL_INLINE_FIELDS_1(__async_arg_AsyncUserTryReceiveReward, User*);

    L2_ASYNC_DECLARE_FINAL_INLINE_FIELDS_1(__async_arg_AsyncUserTrySendRanking, User*);

    int GetID() const noexcept { return m_ID; }
    int GetState() const noexcept { return m_State; }
    void SetEliteCount(int count);
    void SetRewardPeriod(int minutes);
    void SetStartConditionLevel(int minLevel, int maxLevel);
    void SetAutoExecutionPeriod(EVENTCAMPAIGN::ExecutionPeriod* period);
    bool AddStep(CEventCampaignStep* step);
    bool CanExcute(std::int32_t currentTime);
    bool CanStartEventCampaign(const User& user) const;
    int GetTimeLeft() const;
    void ChangeState(int state) noexcept;
    void TryStart();
    void TryStop();
    void AllDataInit();
    int FindUserGoalGroup(User& user);
    void ShowGoalInfo(User& user);
    void UserEnterWorld(User& user);
    void InitNPCMaker();
    void TryJoinEventCampaign(User& user, int goalGroupId);
    void UserTryRequestCampaignHTML(User& user);
    void TrySendProgressInfo(User& user);
    void UserTryReceiveReward(User& user);
    void UserTrySendRanking(User& user);
    void TryAddPoint(User& user, int campaignId, int stepId, int groupId, int goalId, int point);
    void CollectProgressInfo();
    void BroadcastStartEventCampaignToAllUser();
    void BroadcastStartEventCampaignToUser(User& user);
    void BroadcastFinishEventCampaignToAllUser();
    void SendHTMLToUserOnWorking(User* user);
    void SendHTMLToUserOnReward(User* user);
    void SendHTMLToUserOnFailed(User* user);
    void SendProgressInfoToUserOnWorking(User* user);
    void SendProgressInfoToUserOnReward(User* user);
    void SendProgressInfoToUserOnFailed(User* user);
    void SendRankingInfoToUser(User* user);

    void __async_AsyncStartEventCampaign(CExecutionArgumentPad* argument);
    void __async_AsyncStopEventCampaign(CExecutionArgumentPad* argument);
    void __async_AsyncTimerExpired(CExecutionArgumentPad* argument);
    void __async_AsyncStartNewStep(CExecutionArgumentPad* argument);
    void __async_AsyncUserEnterWorld(CExecutionArgumentPad* argument);
    void __async_AsyncUserTryRequestCampaignHTML(CExecutionArgumentPad* argument);
    void __async_AsyncTrySendProgressInfo(CExecutionArgumentPad* argument);
    void __async_AsyncInitNPCMaker(CExecutionArgumentPad* argument);
    void __async_AsyncTryJoinEventCampaign(CExecutionArgumentPad* argument);
    void __async_AsyncUserTryReceiveReward(CExecutionArgumentPad* argument);
    void __async_AsyncUserTrySendRanking(CExecutionArgumentPad* argument);
    void __async_AsyncTryAddPoint(CExecutionArgumentPad* argument);

private:
    friend class EventCampaignSuggestPacket;
    friend class EventCampaignFinishPacket;
    friend class CEventCampaignManager;
    friend class CParserForEventCampaign;

    static void InvokeAsyncStart(CEventCampaign* owner, CExecutionArgumentPad* argument);
    static void InvokeAsyncStop(CEventCampaign* owner, CExecutionArgumentPad* argument);
    static void InvokeAsyncTimerExpired(CEventCampaign* owner, CExecutionArgumentPad* argument);
    static void InvokeAsyncStartNewStep(CEventCampaign* owner, CExecutionArgumentPad* argument);
    static void InvokeAsyncUserEnterWorld(CEventCampaign* owner, CExecutionArgumentPad* argument);
    static void InvokeAsyncUserTryRequestCampaignHTML(CEventCampaign* owner, CExecutionArgumentPad* argument);
    static void InvokeAsyncTrySendProgressInfo(CEventCampaign* owner, CExecutionArgumentPad* argument);
    static void InvokeAsyncInitNPCMaker(CEventCampaign* owner, CExecutionArgumentPad* argument);
    static void InvokeAsyncTryJoinEventCampaign(CEventCampaign* owner, CExecutionArgumentPad* argument);
    static void InvokeAsyncUserTryReceiveReward(CEventCampaign* owner, CExecutionArgumentPad* argument);
    static void InvokeAsyncUserTrySendRanking(CEventCampaign* owner, CExecutionArgumentPad* argument);
    static void InvokeAsyncTryAddPoint(CEventCampaign* owner, CExecutionArgumentPad* argument);
    bool CanJoinEventCampaign(User& user) const;
    CEventCampaignStep* GetLastStep();
    void OnTick();
    void InitJoinUserInfo();
    void RewardAndUserMapDataInit();
    int IsAllGoalCompleted() const;
    void MakeRankingBoard();
    void MakeEliteRewardUserMap();
    void MakeRewardUserMap(int successGroup);
    void WorkingStepSuccess(int successGroup);

    std::array<JoinUserMap, 5> m_JoinUserMap; // +0x00018
    RewardUserMap m_RewardUserMap; // +0x000b8
    EliteUserList m_EliteRewardUserList; // +0x000d8
    std::array<CPacketHelperEx<20480>, 5> m_ProgressPacket; // +0x000f8
    CPacketHelperEx<20480> m_EliteUserPacket; // +0x19148
    StartTimeList m_StartTimeList; // +0x1e158
    CEventCampaignStep* m_WorkingStep; // +0x1e178
    EventCampaignSerializer* m_Serializer; // +0x1e180
    int m_ID; // +0x1e188
    int m_State; // +0x1e18c
    int m_EliteUserCount; // +0x1e190
    int m_RewardPeriod; // +0x1e194
    EVENTCAMPAIGN::ExecutionPeriod* m_ExecutionPeriod; // +0x1e198
    EventCampaignStartCondition m_StartCondition; // +0x1e1a0
    CEventCampaignStep* m_FirstStep; // +0x1e1a8
    std::int32_t m_EventStartTime; // +0x1e1b0
    std::byte m_Alignment1E1B4To1E1B8[4];
};

class EventCampaignPacket : public QuestPacket {
public:
    explicit EventCampaignPacket(CEventCampaign& campaign) noexcept;
    ~EventCampaignPacket() override;

    void Send(User& user) override;

protected:
    void MakeConstBuffer(const char* format, ...);

    CEventCampaign* m_Campaign; // +0x08
    DefaultIOBuffer* m_Buffer; // +0x10
};

class EventCampaignSuggestPacket final : public EventCampaignPacket {
public:
    explicit EventCampaignSuggestPacket(CEventCampaign& campaign);
    ~EventCampaignSuggestPacket() override = default;
};

class EventCampaignFinishPacket final : public EventCampaignPacket {
public:
    explicit EventCampaignFinishPacket(CEventCampaign& campaign);
    ~EventCampaignFinishPacket() override = default;
};

class EventCampaignProgressInfoPacket final : public EventCampaignPacket {
public:
    EventCampaignProgressInfoPacket(CEventCampaign& campaign, CPacketHelperEx<20480>& packet);
    ~EventCampaignProgressInfoPacket() override = default;

private:
    [[maybe_unused]] CPacketHelperEx<20480>* m_Packet; // +0x18
};

class EventCampaignRewardStartPacket final : public EventCampaignPacket {
public:
    EventCampaignRewardStartPacket(CEventCampaign& campaign, CPacketHelperEx<20480>& packet);
    ~EventCampaignRewardStartPacket() override = default;

private:
    [[maybe_unused]] CPacketHelperEx<20480>* m_Packet; // +0x18
};

class EventCampaignRewardReceivedPacket final : public EventCampaignPacket {
public:
    EventCampaignRewardReceivedPacket(CEventCampaign& campaign, CPacketHelperEx<20480>& packet);
    ~EventCampaignRewardReceivedPacket() override = default;

private:
    [[maybe_unused]] CPacketHelperEx<20480>* m_Packet; // +0x18
};

class EventCampaignFailedPacket final : public EventCampaignPacket {
public:
    EventCampaignFailedPacket(CEventCampaign& campaign, CPacketHelperEx<20480>& packet);
    ~EventCampaignFailedPacket() override = default;

private:
    [[maybe_unused]] CPacketHelperEx<20480>* m_Packet; // +0x18
};

class EventCampaignRankingInfoPacket final : public EventCampaignPacket {
public:
    EventCampaignRankingInfoPacket(CEventCampaign& campaign, CPacketHelperEx<20480>& packet);
    ~EventCampaignRankingInfoPacket() override = default;

private:
    [[maybe_unused]] CPacketHelperEx<20480>* m_Packet; // +0x18
};

class CEventCampaignManager final : public CIOObject {
public:
    using CampaignMap = std::map<int, CEventCampaign*>;
    using SleepingCampaignList = std::vector<CEventCampaign*>;

    CEventCampaignManager();
    ~CEventCampaignManager() override;

    L2_DECLARE_OBJECT_REGISTRATION(CEventCampaignManager)

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void TimerExpired(int id) override;

    static CEventCampaignManager* Inst();
    void Init();
    void Load();
    bool AddEventCampaign(CEventCampaign* campaign);
    static bool UserPacketHandler(CSocket* socket, const unsigned char* packet);
    bool IsWorkingEventCampaignOnlyWorkingState(int campaignId) const;
    bool IsWorkingEventCampaign(int campaignId, int stepId) const;
    bool IsWorkingEventCampaign(int campaignId, int stepId, int goalGroupId) const;
    void InitNPCMaker();
    void OnAddEventCampaignPoint(User& user, int campaignId, int stepId, int goalGroupId, int goalId, int point);
    void OnAcceptEventCampaign(User& user, int campaignId, int stepId, int goalGroupId);
    void OnClientRequestHTML(User& user, int campaignId, int stepId);
    void OnRequestProgressInfo(User& user, int campaignId, int stepId);
    void OnRequestReward(User& user, int campaignId, int stepId, int goalGroupId);
    void OnRequestRanking(User& user, int campaignId, int stepId);
    int OnGoingEventCampaignId();
    int OnGoingEventCampaignStepId();
    int OnJoinEventCampaignGoalGroupId(User& user);
    void OnShowProgressInfo(User& user);
    void OnStopEventCampaign();
    void OnUserEnterWorld(User& user);
    void CheckExecutionEventCampaign(bool force, int campaignId);

private:
    CampaignMap m_EventCamapigns; // +0x18, current spelling
    CEventCampaign* m_OnGoingEventCampaign; // +0x38
    SleepingCampaignList m_OnSleepingEventCampaigns; // +0x40
    int m_LoadFinished; // +0x60
    [[maybe_unused]] std::byte m_Alignment64To68[4]{};
};
