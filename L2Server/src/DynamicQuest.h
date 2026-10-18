#pragma once

#include "QuestBase.h"

#include "L2Core/src/inc/XStd.h"
#include "Protocol/src/L2Constants.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/IOC.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

class CArea;
class CDBSocket;
class CDoor;
class CSocket;
class CTerritory;
class CUserSocket;
class CCampaign;
class CCampaignData;
class CUserZoneQuestHandler;
class CParserForDynamicQuest;
struct DynamicContentSerializer;
class DynamicQuest;
class DynamicQuestInfo;
class DynamicQuestSerializer;
class DynamicQuestStateHandler;
struct QuestCondition_FullInven;
struct QuestFeedback;
class User;
struct SharedDynamicQuestInfo;

namespace BuilderCmd {
bool DynamicQuest(CUserSocket*, User*, char16_t*);
}

namespace DynamicQuestSetting {

struct Value {
    Value() : ELITE_COUNT1(0), ELITE_COUNT2(0), REWARD_COUNT1(0), REWARD_PERIOD(0) {}

    unsigned int ELITE_COUNT1;
    unsigned int ELITE_COUNT2;
    unsigned int REWARD_COUNT1;
    unsigned int REWARD_PERIOD;
};

class ExecutionPeriod {
public:
    ExecutionPeriod() : m_PeriodInfo{} {}

    bool CanExecute(std::int32_t currentTime) {
        return m_PeriodInfo.m_nStartTime <= currentTime && currentTime <= m_PeriodInfo.m_nEndTime;
    }

private:
    friend class ::CParserForDynamicQuest;

    ::TimePeriodInfo m_PeriodInfo;
};

struct CastleTypeCond {
    CastleTypeCond() : m_nCastleId(0), m_nCastleType(0) {}

    int m_nCastleId;
    int m_nCastleType;
};

struct StartTimeData {
    StartTimeData(int hour, int minute, double dice) : startTime(hour * 60 + minute), dice(dice) {}

    int startTime;
    double dice;
};

struct FHTMLParamName {
    FHTMLParamName() : dQuestId(u"dquest_id"), campaignId(u"campaignId") {}

    std::u16string dQuestId;
    std::u16string campaignId;
};

class AutoExecution {
public:
    AutoExecution();
    virtual ~AutoExecution();

    bool CanExecute(DynamicQuestInfo& info, std::int32_t currentTime);
    bool CanExecuteByCastle();

    ExecutionPeriod* m_pPeriod; // +0x08
    CastleTypeCond m_CastleCond; // +0x10

protected:
    virtual bool CanExecuteImpl(DynamicQuestInfo& info, std::int32_t currentTime) = 0;
};

class ManualExecution final : public AutoExecution {
public:
    ManualExecution() = default;
    ~ManualExecution() override = default;

protected:
    bool CanExecuteImpl(DynamicQuestInfo& info, std::int32_t currentTime) override;
};

class AutoExecutionByTimer final : public AutoExecution {
public:
    using StartTimeList = std::vector<StartTimeData>;

    AutoExecutionByTimer();
    ~AutoExecutionByTimer() override;

    int m_ExecuteDayList; // +0x18
    std::byte m_alignment1cTo20[4]{};
    StartTimeList m_StartTimeData; // +0x20

protected:
    bool CanExecuteImpl(DynamicQuestInfo& info, std::int32_t currentTime) override;
};

} // namespace DynamicQuestSetting

class DynamicQuest {
public:
    using ConditionList = std::vector<QuestCondition*>;

    explicit DynamicQuest(int id);
    virtual ~DynamicQuest();

    int GetId() const noexcept { return m_Id; }
    virtual DynamicQuestType::Enum GetType() const = 0;
    void AddCampaignData(CCampaignData* campaignData);
    CCampaign* GetFirstCampaign();
    CCampaign* GetLastCampaign();
    int GetRewardPeriod() const noexcept { return static_cast<int>(m_Setting.REWARD_PERIOD); }
    virtual int GetEliteCount1() const = 0;
    virtual int GetEliteCount2() const = 0;
    virtual int GetRewardCount1() const = 0;
    virtual bool CanExecutedByUser(User& user) = 0;
    virtual bool CanExecutedByCondition(DynamicQuestInfo& info, std::int32_t currentTime) = 0;
    virtual bool IsInRange(User& user) = 0;
    DynamicQuestInfo* MakeInfo();

    DynamicQuestType::QuestType QuestType() const noexcept { return m_QuestType; }
    void QuestType(DynamicQuestType::QuestType type) noexcept { m_QuestType = type; }
    bool IsValid(CQuestBase& quest, const ConditionList& conditions, User& user);
    bool IsCastleQuest() const noexcept { return m_nRelatedCastleID > 0; }
    int GetRelatedCastleID() const noexcept { return m_nRelatedCastleID; }

protected:
    friend class CParserForDynamicQuest;
    friend class DynamicQuestManager;

    virtual DynamicQuestInfo* MakeInfoImpl() = 0;
    virtual void AddRange(CTerritory* territory) = 0;

    int m_Id; // +0x08
    std::byte m_alignment0cTo10[4]{};
    CCampaignData* m_pCampaignData; // +0x10
    DynamicQuestSetting::Value m_Setting; // +0x18
    DynamicQuestSetting::AutoExecution* m_pExecutionSetting; // +0x28
    unsigned int m_TopUserCount; // +0x30
    std::byte m_alignment34To38[4]{};
    ConditionList m_StartConditionList; // +0x38
    DynamicQuestType::QuestType m_QuestType; // +0x58
    int m_nRelatedCastleID; // +0x5c
};

struct QuestOperation_ItemOnAcceptDynamicQuest : QuestOperation_Item {
    ~QuestOperation_ItemOnAcceptDynamicQuest() override = default;

    AtomicJobList* MakeAtomicJobList(User& user, CQuestBase& quest) override;
    void LogOnSuccess(User& user, ItemManip (&items)[100]) override;
};

struct QuestOperation_ItemOnFinishDynamicQuest : QuestOperation_ItemOnAcceptDynamicQuest {
    ~QuestOperation_ItemOnFinishDynamicQuest() override = default;

    void Do(User& user, CQuestBase& quest) override;
    AtomicJobList* MakeAtomicJobList(User& user, CQuestBase& quest) override;
    void LogOnSuccess(User& user, ItemManip (&items)[100]) override;
};


class CCampaignBase : public CQuestBase {
public:
    explicit CCampaignBase(int id) : CQuestBase(id) {}
    ~CCampaignBase() override;
};

class CCampaign : public CCampaignBase {
public:
    using OperationList = CQuestBase::OperationList;
    using GoalMap = std::map<unsigned int, int>;
    using MakerNameList = std::vector<std::u16string>;
    using DoorList = std::vector<CDoor*>;
    using AreaList = std::vector<CArea*>;

    CCampaign(int id, int duration);
    ~CCampaign() override;

    int GetOnetimeQuestId() const override;
    void TryFinish(User& user) override;
    QuestOperation_Item* _GetItemOperation(User& user);
    DynamicQuest* GetDynamicQuest();
    void SetCampaignData(CCampaignData* campaignData) noexcept { m_pCampaignData = campaignData; }
    int GetGoal(unsigned int goalId);
    unsigned int GetGoalCount() const noexcept { return m_GoalCount; }
    int GetDuration() const noexcept { return m_Duration; }
    void ShowGoal(CUserSocket* socket);
    CCampaign* GetNextCampaignOnSuccess();
    CCampaign* GetNextCampaignOnFailed();
    void OnInquiry(User& user);
    void OnRequestReward(User& user);
    void OnRequestFailedHTML(User& user);
    void OnStarted();
    void OnCompleted(bool success);
    int GetAdjustedRewardRate() override;

protected:
    bool CanStart(User& user) override;
    bool CanAccept(User& user) override;
    bool CanFinish(User& user) override;
    void OnAcceptOperation(User& user) override;
    void OnFinishOperation(User& user) override;

private:
    friend class CParserForDynamicQuest;
    friend class DynamicQuestManager;
    friend class CCampaignData;
    friend class CCampaignDataCycle;
    friend class DynamicQuestInfo;
    friend class DynamicQuestState_End;
    friend class DynamicQuestState_Working;

    void ActivateArea();
    void ActivateDoor();
    void ActivateNPCMaker();
    void SendCampaignInfo();
    void DeactivateArea();
    void DeactivateDoor();
    void DeactivateNPCMaker(bool success);

    OperationList m_FailedOperation; // +0x0e8
    OperationList m_InquiryOperation; // +0x108
    OperationList m_SuggestRewardOperation; // +0x128
    OperationList m_SuggestRewardOperation_Elite; // +0x148
    OperationList m_SuggestRewardOperation_Basic; // +0x168
    OperationList m_SuggestRewardOperation_Received; // +0x188
    QuestCondition_FullInven* m_pConditionInven_Elite1; // +0x1a8
    QuestCondition_FullInven* m_pConditionInven_Elite2; // +0x1b0
    QuestCondition_FullInven* m_pConditionInven_Basic; // +0x1b8
    QuestOperation_Item* m_pFinishOperation_Item_Elite1; // +0x1c0
    QuestOperation_Item* m_pFinishOperation_Item_Elite2; // +0x1c8
    QuestOperation_Item* m_pFinishOperation_Item_Basic; // +0x1d0
    CCampaignData* m_pCampaignData; // +0x1d8
    CCampaign* m_pPrevCampaign; // +0x1e0
    CCampaign* m_pNextCampaign; // +0x1e8
    GoalMap m_GoalMap; // +0x1f0
    unsigned int m_GoalCount; // +0x210
    int m_AdjustedGoalRate; // +0x214
    int m_AdjustedRewardRate; // +0x218
    int m_Duration; // +0x21c
    MakerNameList m_MakerList; // +0x220
    DoorList m_OpenDoorList; // +0x240
    AreaList m_AreaOnList; // +0x260
    DynamicQuestInfo* m_pProgressInfo; // +0x280
};

class CampaignPacket : public QuestPacket {
public:
    explicit CampaignPacket(CCampaign& campaign) noexcept;
    ~CampaignPacket() override;

    void Send(User& user) override;

protected:
    void MakeConstBuffer(const char* format, ...);

    CCampaign& campaign; // +0x08
    DefaultIOBuffer* m_pBuffer; // +0x10
};

class CampaignSuggestPacket final : public CampaignPacket {
public:
    explicit CampaignSuggestPacket(CCampaign& campaign);
    ~CampaignSuggestPacket() override = default;
};

class CampaignFinishedPacket final : public CampaignPacket {
public:
    explicit CampaignFinishedPacket(CCampaign& campaign);
    ~CampaignFinishedPacket() override = default;
};

class CampaignHelperPacket : public CampaignPacket {
public:
    CampaignHelperPacket(CCampaign& campaign, CPacketHelperEx<20480>& packet);
    ~CampaignHelperPacket() override = default;

protected:
    CPacketHelperEx<20480>& m_Packet; // +0x18
};

#define L2_DQUEST_CAMPAIGN_HELPER_PACKET(Name)                                                                              \
    class Name final : public CampaignHelperPacket {                                                                        \
    public:                                                                                                                 \
        Name(CCampaign& campaign, CPacketHelperEx<20480>& packet) : CampaignHelperPacket(campaign, packet) {}               \
        ~Name() override = default;                                                                                         \
    }

L2_DQUEST_CAMPAIGN_HELPER_PACKET(CampaignRewardStartedPacket);
L2_DQUEST_CAMPAIGN_HELPER_PACKET(CampaignRewardReceivedPacket);
L2_DQUEST_CAMPAIGN_HELPER_PACKET(CampaignFailedPacket);
L2_DQUEST_CAMPAIGN_HELPER_PACKET(CampaignProgressPacket);
L2_DQUEST_CAMPAIGN_HELPER_PACKET(CampaignScoreBoardPacket);

#undef L2_DQUEST_CAMPAIGN_HELPER_PACKET


class CCampaignData {
public:
    CCampaignData();
    virtual ~CCampaignData();

    bool AddCampaign(CCampaign* campaign);
    DynamicQuest* GetDynamicQuest() const noexcept { return m_pDynamicQuest; }
    void SetDynamicQuest(DynamicQuest* dynamicQuest) noexcept { m_pDynamicQuest = dynamicQuest; }
    CCampaign* GetFirstCampaign() const noexcept { return m_pFirstCampaign; }
    CCampaign* GetLastCampaign();
    virtual void PostLoad() = 0;
    CCampaign* GetNextCampaignOnSuccess(const CCampaign& campaign);
    virtual CCampaign* GetNextCampaignOnFailed(const CCampaign& campaign) = 0;

protected:
    friend class DynamicQuestManager;
    DynamicQuest* m_pDynamicQuest; // +0x08
    CCampaign* m_pFirstCampaign; // +0x10
};

class CCampaignDataCycle final : public CCampaignData {
public:
    CCampaignDataCycle() = default;
    ~CCampaignDataCycle() override;

    void PostLoad() override;
    CCampaign* GetNextCampaignOnFailed(const CCampaign& campaign) override;
};

class CCampaignDataOneTime final : public CCampaignData {
public:
    CCampaignDataOneTime() = default;
    ~CCampaignDataOneTime() override = default;

    void PostLoad() override;
    CCampaign* GetNextCampaignOnFailed(const CCampaign& campaign) override;
};

class DynamicContent final : public DynamicQuest {
public:
    explicit DynamicContent(int id);
    ~DynamicContent() override;

    DynamicQuestType::Enum GetType() const override;
    int GetEliteCount1() const override;
    int GetEliteCount2() const override;
    int GetRewardCount1() const override;
    bool CanExecutedByUser(User& user) override;
    bool CanExecutedByCondition(DynamicQuestInfo& info, std::int32_t currentTime) override;
    bool IsInRange(User& user) override;

protected:
    DynamicQuestInfo* MakeInfoImpl() override;
    void AddRange(CTerritory* territory) override;
};

class ZoneQuest final : public DynamicQuest {
public:
    using TerritoryList = std::vector<CTerritory*>;

    explicit ZoneQuest(int id);
    ~ZoneQuest() override;

    DynamicQuestType::Enum GetType() const override;
    int GetEliteCount1() const override;
    int GetEliteCount2() const override;
    int GetRewardCount1() const override;
    bool CanExecutedByUser(User& user) override;
    bool CanExecutedByCondition(DynamicQuestInfo& info, std::int32_t currentTime) override;
    bool IsInRange(User& user) override;

protected:
    DynamicQuestInfo* MakeInfoImpl() override;
    void AddRange(CTerritory* territory) override;

private:
    friend class ZoneQuestInfo;
    TerritoryList m_TerritoryList; // +0x60
};

// Current DynamicQuest.obj stores a CIOObject-derived serializer interface at
// DynamicQuestInfo+0x18. MSVC's eight-byte MemoryObject member-function
// pointer is represented by a same-width typed static trampoline in the
// portable build, matching the established CExecutionSerializer adaptation.
class DynamicQuestSerializer : public CIOObject {
public:
    using Executable = void (*)(DynamicQuestInfo* owner, CExecutionArgumentPad* argument);

    ~DynamicQuestSerializer() override = default;

    virtual void AddExecutable(
        DynamicQuestInfo* owner, Executable executable, CExecutionArgument* argument, const wchar_t* executableName
    ) = 0;
};

struct DynamicContentSerializer final : DynamicQuestSerializer {
    using EmbeddedSerializer = CExecutionSerializer<DynamicContentSerializer, 16384, DynamicQuestInfo>;

    DynamicContentSerializer();
    ~DynamicContentSerializer() override = default;

    void AddExecutable(
        DynamicQuestInfo* owner, Executable executable, CExecutionArgument* argument, const wchar_t* executableName
    ) override;

    EmbeddedSerializer s; // +0x18
};

struct ZoneQuestSerializer final : DynamicQuestSerializer {
    using EmbeddedSerializer = CExecutionSerializer<ZoneQuestSerializer, 8192, DynamicQuestInfo>;

    ZoneQuestSerializer();
    ~ZoneQuestSerializer() override = default;

    void AddExecutable(
        DynamicQuestInfo* owner, Executable executable, CExecutionArgument* argument, const wchar_t* executableName
    ) override;

    EmbeddedSerializer s; // +0x18
};

class DynamicQuestInfo : public CIOObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(DynamicQuestInfo)
    enum State : std::int32_t {
        PERIOD_SLEEP = 0,
        PERIOD_WORKING = 1,
        PERIOD_REWARD = 2,
        PERIOD_WAIT = 3,
        PERIOD_FAILED = 4,
        PERIOD_MAX = 5,
    };

    struct PointParam {
        PointParam(unsigned int userDbId, unsigned int goalId, int point, DynamicQuestType::PointType pointType) :
            userDBId(userDbId), goalId(goalId), point(point), pointType(pointType) {}

        unsigned int userDBId;
        unsigned int goalId;
        int point;
        DynamicQuestType::PointType pointType;
    };

    struct UserPointData {
        std::array<int, DynamicQuestType::MAX_POINTTYPE> at{};

        int Sum() const noexcept { return at[0] + at[1]; }
    };

    struct UserGoalData {
        UserPointData point;
        std::u16string charName;
    };

    struct ScoreData {
        std::u16string charName;
        UserPointData point;
        std::vector<unsigned int> userDBIdList;
    };

    L2_ASYNC_DECLARE_FINAL_EMPTY_2_SLOT(__async_arg_AsyncStartDynamicQuest);

    L2_ASYNC_DECLARE_FINAL_EMPTY_2_SLOT(__async_arg_AsyncInitNPCMaker);

    L2_ASYNC_DECLARE_FINAL_EMPTY_2_SLOT(__async_arg_AsyncStopDynamicQuest);

    L2_ASYNC_DECLARE_FINAL_INLINE_FIELDS_1(__async_arg_AsyncContinueCampaign, CCampaign*);

    L2_ASYNC_DECLARE_FINAL_INLINE_FIELDS_1(__async_arg_AsyncChangeCampaign, int);

    L2_ASYNC_DECLARE_FINAL_INLINE_FIELDS_1(__async_arg_AsyncUserTryJoinInCampaign, User*);
    L2_ASYNC_DECLARE_FINAL_INLINE_FIELDS_1(__async_arg_AsyncUserTryRequestCampaignHTML, User*);
    L2_ASYNC_DECLARE_FINAL_INLINE_FIELDS_1(__async_arg_AsyncUserTryAcceptCampaign, User*);
    L2_ASYNC_DECLARE_FINAL_INLINE_FIELDS_1(__async_arg_AsyncOnUserAcceptCampaign, User*);
    L2_ASYNC_DECLARE_FINAL_INLINE_FIELDS_1(__async_arg_AsyncUserTryReceiveReward, User*);
    L2_ASYNC_DECLARE_FINAL_INLINE_FIELDS_1(__async_arg_AsyncOnUserReceiveReward, User*);
    L2_ASYNC_DECLARE_FINAL_INLINE_FIELDS_1(__async_arg_AsyncTrySendProgressInfo, User*);
    L2_ASYNC_DECLARE_FINAL_INLINE_FIELDS_1(__async_arg_AsyncTrySendScoreBoard, User*);
    L2_ASYNC_DECLARE_FINAL_INLINE_FIELDS_1(__async_arg_AsyncResetUserGoalMap, User*);

    L2_ASYNC_DECLARE_FINAL_INLINE_FIELDS_4(
        __async_arg_AsyncTryAddPoint, unsigned int, unsigned int, int, DynamicQuestType::PointType
    );

    L2_ASYNC_DECLARE_FINAL_EMPTY_2_SLOT(__async_arg_AsyncTimerExpired);

    using GlobalGoalMap = std::map<unsigned int, int>;
    using UserGoalKey = std::CDualKey<unsigned int, unsigned int>;
    using UserGoalMap = std::map<UserGoalKey, UserGoalData>;
    using ScoreKey = std::pair<int, unsigned int>;
    using ScoreMap = std::map<ScoreKey, ScoreData>;
    using DynamicQuestUserSet = std::set<int>;

    explicit DynamicQuestInfo(DynamicQuest* dynamicQuest);
    ~DynamicQuestInfo() override;

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()
    void TimerExpired(int id) override;

    std::int32_t GetCampaignStartTime() const noexcept { return m_CampaignStartTime; }
    std::int32_t GetCampaignEndTime() const noexcept { return m_CampaignWorkingEndTime; }
    void SetLastExecutedTime(int value) noexcept { m_LastExecutedTime = value; }
    void Init(int lastExecutedTime);
    int GetDQuestId() const;
    int GetCampaignId() const;
    int GetDuration() const;
    int GetRewardPeriod() const;
    DynamicQuestType::QuestType GetQuestType() const;
    bool CanExecutedByCondition(std::int32_t currentTime);
    bool IsAlreadyExecuted(std::int32_t now, int start, int end) const;
    bool IsSleep();
    int GetTimeLeft();
    SharedDynamicQuestInfo& GetSharedInfo();
    void SetCastleCampaignResult(bool success);

    virtual void MakeSerializer() = 0;
    DynamicQuestSerializer* s; // +0x18
    virtual void OnUserEnterWorld(User& user) = 0;
    virtual void OnUserMove(User& user) = 0;

    void TryStart();
    void TryStop();
    void _StartDynamicQuest();
    void InitNPCMaker();
    virtual void _UserTryStartAndJoinInDynamicQuest(User& user) = 0;
    void TryContinue(CCampaign* campaign);
    void TryChangeCampaign(int campaignId);
    void _ContinueCampaign(CCampaign* campaign);
    void _ChangeCampaign(State state, CCampaign* campaign);
    void _OnCampaignStarted();
    void _OnCampaignStoped();
    void _OnCampaignWorkingEnd();
    void _OnCampaignSuccess();
    void _OnCampaignFailed();
    void _OnCampaignFinished();
    void _CampaignFailed();
    void _RewardFinished();
    void _CampaignFailedFinished();
    void UserTryJoinInCampaign(User& user);
    virtual void _TryNoticeCampaignStarted(User& user) = 0;
    void UserTryRequestCampaignHTML(User& user);
    virtual void _UserTryRequestInquiryHTML(User& user) = 0;
    void _UserTryRequestRewardHTML(User& user);
    void _UserTryRequestFailedHTML(User& user);
    virtual void TryAcceptCampaign(User& user) = 0;
    void _TryAcceptCampaign(User& user);
    void _OnAcceptCampaign(User& user);
    void _TryNoticeRewardPeriod(User& user);
    void _NoticeCampaignStarted(User& user);
    void _NoticeRewardPeriod(User& user);
    void _NoticeRewardReceived(User& user);
    void _TryNoticeCampaignFailed(User& user);
    void _NoticeCampaignFailed(User& user);
    void TryReceiveReward(User& user);
    void _TryReceiveReward(User& user);
    void _OnReceiveReward(User& user);
    void TryAddPoint(User& user, unsigned int goalId, int point, DynamicQuestType::PointType pointType);
    void _TryAddPoint(const PointParam& point);
    void TrySendProgressInfo(User& user);
    void _SendProgressInfo(User& user);
    void TrySendScoreBoard(User& user);
    virtual void _TrySendScoreBoardOnWorking(User& user) = 0;
    void _TrySendScoreBoard(User& user);
    void ShowProgressInfo(CUserSocket* socket);
    void OnAcceptCampaign(User& user);
    void OnFinishCampaign(User& user);
    bool _IsParticipant(User& user) const;
    bool _CanReceiveReward(User& user) const;
    bool _IsRewardReceived(User& user) const;
    bool _IsEliteUser1(User& user) const;
    bool _IsEliteUser2(User& user) const;
    bool _IsRewardUser1(User& user) const;
    void _BroadcastCampaignFinished();
    void _ClearResultSummary();
    void _MakeResultSummary();
    bool _IsAllGoalCompleted();
    void _CampaignSuccess();
    virtual void _MakeSnapshot(DynamicQuestType::ProgressInfoType type, bool force) = 0;
    int GetGoal(int goalId) const;
    int GetPoint(int goalId) const;

protected:
    friend bool BuilderCmd::DynamicQuest(CUserSocket*, User*, char16_t*);
    friend class DynamicQuestManager;
    friend class DynamicQuestState_End;
    friend class DynamicQuestState_Failed;
    friend class DynamicQuestState_Reward;
    friend class DynamicQuestState_Sleep;
    friend class DynamicQuestState_Wait;
    friend class DynamicQuestState_Working;
    friend class CUserZoneQuestHandler;

    virtual void _BroadcastCampaignStarted() = 0;
    virtual void _BroadcastRewardPeriod();
    virtual void _BroadcastCampaignFailed();
    virtual void _OnAcceptCampaignImpl(User& user) = 0;
    virtual void OutOfRangeOnSendProgressInfo(User& user) = 0;
    virtual void _MakeScoreMap(ScoreMap& score, bool reward) = 0;
    virtual void _MakeEliteList(const ScoreMap& score) = 0;
    virtual void _MakeScoreBoard(const ScoreMap& score) = 0;
    virtual void _OnSetCamapaign() = 0;

    DynamicQuestStateHandler* GetStateHandler();
    bool _ChangeState(State expected, State next, CCampaign* campaign);
    void _ChangeStateHandler(DynamicQuestStateHandler& previous, DynamicQuestStateHandler& next, CCampaign* campaign);
    void _SetCampaign(CCampaign* campaign);
    void __async_AsyncStartDynamicQuest(CExecutionArgumentPad* argument);
    void __async_AsyncInitNPCMaker(CExecutionArgumentPad* argument);
    void __async_AsyncStopDynamicQuest(CExecutionArgumentPad* argument);
    void __async_AsyncContinueCampaign(CExecutionArgumentPad* argument);
    void __async_AsyncChangeCampaign(CExecutionArgumentPad* argument);
    void __async_AsyncTryAddPoint(CExecutionArgumentPad* argument);
    void __async_AsyncTimerExpired(CExecutionArgumentPad* argument);
    void __async_AsyncUserTryJoinInCampaign(CExecutionArgumentPad* argument);
    void __async_AsyncUserTryRequestCampaignHTML(CExecutionArgumentPad* argument);
    void __async_AsyncUserTryAcceptCampaign(CExecutionArgumentPad* argument);
    void __async_AsyncOnUserAcceptCampaign(CExecutionArgumentPad* argument);
    void __async_AsyncUserTryReceiveReward(CExecutionArgumentPad* argument);
    void __async_AsyncOnUserReceiveReward(CExecutionArgumentPad* argument);
    void __async_AsyncTrySendProgressInfo(CExecutionArgumentPad* argument);
    void __async_AsyncTrySendScoreBoard(CExecutionArgumentPad* argument);
    void __async_AsyncResetUserGoalMap(CExecutionArgumentPad* argument);

    std::int32_t m_CampaignStartTime; // +0x20
    std::int32_t m_CampaignWorkingEndTime; // +0x24
    std::int32_t m_DQuestStartTime; // +0x28
    std::byte m_alignment2cTo30[4]{};
    DynamicQuestStateHandler* volatile m_pStateHandler; // +0x30
    int m_nServerUserCount; // +0x38
    int m_nAdjustedRewardRate; // +0x3c
    CCampaign* m_pCampaign; // +0x40
    DynamicQuest* m_pDynamicQuest; // +0x48
    GlobalGoalMap m_GlobalGoalMap; // +0x50
    UserGoalMap m_UserGoalMap; // +0x70
    int m_LastExecutedTime; // +0x90
    std::byte m_alignment94To98[4]{};
    std::array<CPacketHelperEx<20480>, 4> m_ProgressPacket; // +0x98
    CPacketHelperEx<20480> m_ScoreBoardPacket; // +0x140d8
    DynamicQuestUserSet m_ResultSet; // +0x190e8
    DynamicQuestUserSet m_EliteSet1; // +0x19108
    DynamicQuestUserSet m_EliteSet2; // +0x19128
    DynamicQuestUserSet m_RewardSet1; // +0x19148
};

class DynamicContentInfo final : public DynamicQuestInfo {
public:
    explicit DynamicContentInfo(DynamicQuest* dynamicQuest);
    ~DynamicContentInfo() override = default;

    L2_DECLARE_OBJECT_REGISTRATION(DynamicContentInfo)
    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()
    void MakeSerializer() override;
    void OnUserEnterWorld(User& user) override;
    void OnUserMove(User& user) override;
    void _UserTryStartAndJoinInDynamicQuest(User& user) override;
    void _TryNoticeCampaignStarted(User& user) override;
    void _UserTryRequestInquiryHTML(User& user) override;
    void TryAcceptCampaign(User& user) override;
    void _TrySendScoreBoardOnWorking(User& user) override;

protected:
    void _BroadcastCampaignStarted() override;
    void _BroadcastRewardPeriod() override;
    void _BroadcastCampaignFailed() override;
    void _OnAcceptCampaignImpl(User& user) override;
    void OutOfRangeOnSendProgressInfo(User& user) override;
    void _MakeSnapshot(DynamicQuestType::ProgressInfoType type, bool force) override;
    void _MakeScoreMap(ScoreMap& score, bool reward) override;
    void _MakeEliteList(const ScoreMap& score) override;
    void _MakeScoreBoard(const ScoreMap& score) override;
    void _OnSetCamapaign() override;
};

class ZoneQuestInfo final : public DynamicQuestInfo {
public:
    // TotalChecker CodeView and current 0x0070431c both encode 0xfa.
    enum : int { MAX_SCORE_BOARD_COUNT = 250 };

    struct MapXY {
        int mapX;
        int mapY;
    };

    using MapXYList = std::vector<MapXY>;

    explicit ZoneQuestInfo(DynamicQuest* dynamicQuest);
    ~ZoneQuestInfo() override;

    L2_DECLARE_OBJECT_REGISTRATION(ZoneQuestInfo)
    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()
    void MakeSerializer() override;
    void OnUserEnterWorld(User& user) override;
    void OnUserMove(User& user) override;
    void _UserTryStartAndJoinInDynamicQuest(User& user) override;
    void _TryNoticeCampaignStarted(User& user) override;
    void _UserTryRequestInquiryHTML(User& user) override;
    void TryAcceptCampaign(User& user) override;
    void _TrySendScoreBoardOnWorking(User& user) override;

protected:
    void _BroadcastCampaignStarted() override;
    void _OnAcceptCampaignImpl(User& user) override;
    void OutOfRangeOnSendProgressInfo(User& user) override;
    void _MakeSnapshot(DynamicQuestType::ProgressInfoType type, bool force) override;
    void _MakeScoreMap(ScoreMap& score, bool reward) override;
    void _MakeEliteList(const ScoreMap& score) override;
    void _MakeScoreBoard(const ScoreMap& score) override;
    void _OnSetCamapaign() override;

private:
    friend class CUserZoneQuestHandler;
    int _GetUserCount() const;

    int m_SerialId; // +0x19168
    [[maybe_unused]] std::byte m_alignment1916cTo19170[4]{};
    MapXYList m_MapXYList; // +0x19170
};

class CUserZoneQuestHandler final {
public:
    static void SetCampaign(ZoneQuestInfo& info, User& user);
    static bool IsSetUser(ZoneQuestInfo& info, User& user);
    static void ResetCampaign(ZoneQuestInfo& info, User& user);
};


class DynamicQuestStateHandler {
public:
    virtual ~DynamicQuestStateHandler();
    virtual void _OnStart(DynamicQuestInfo& info) = 0;
    virtual void _OnTick(DynamicQuestInfo& info) = 0;
    virtual void _OnEnd(DynamicQuestInfo& info) = 0;
    virtual bool CanExecutedByCondition() = 0;
    virtual int GetTimeLeft(DynamicQuestInfo& info) = 0;
    virtual void _StartDynamicQuest(DynamicQuestInfo& info) = 0;
    virtual void _StopDynamicQuest(DynamicQuestInfo& info) = 0;
    virtual void _ContinueCampaign(DynamicQuestInfo& info, CCampaign* campaign) = 0;
    virtual void _ChangeCampaign(DynamicQuestInfo& info, CCampaign* campaign) = 0;
    virtual void _UserTryStartCampaign(DynamicQuestInfo& info, User& user) = 0;
    virtual void _UserTryRequestCampaignHTML(DynamicQuestInfo& info, User& user) = 0;
    virtual void _UserTryAcceptCampaign(DynamicQuestInfo& info, User& user) = 0;
    virtual void _OnUserAcceptCampaign(DynamicQuestInfo& info, User& user) = 0;
    virtual void _UserTryReceiveReward(DynamicQuestInfo& info, User& user) = 0;
    virtual void _TryAddPoint(DynamicQuestInfo& info, const DynamicQuestInfo::PointParam& point) = 0;
    virtual void _TrySendProgressInfo(DynamicQuestInfo& info, User& user) = 0;
    virtual void _TrySendScoreBoard(DynamicQuestInfo& info, User& user) = 0;
};

class DynamicQuestState_Sleep : public DynamicQuestStateHandler {
public:
    ~DynamicQuestState_Sleep() override;
    void _OnStart(DynamicQuestInfo& info) override;
    void _OnTick(DynamicQuestInfo& info) override;
    void _OnEnd(DynamicQuestInfo& info) override;
    bool CanExecutedByCondition() override;
    int GetTimeLeft(DynamicQuestInfo& info) override;
    void _StartDynamicQuest(DynamicQuestInfo& info) override;
    void _StopDynamicQuest(DynamicQuestInfo& info) override;
    void _ContinueCampaign(DynamicQuestInfo& info, CCampaign* campaign) override;
    void _ChangeCampaign(DynamicQuestInfo& info, CCampaign* campaign) override;
    void _UserTryStartCampaign(DynamicQuestInfo& info, User& user) override;
    void _UserTryRequestCampaignHTML(DynamicQuestInfo& info, User& user) override;
    void _UserTryAcceptCampaign(DynamicQuestInfo& info, User& user) override;
    void _OnUserAcceptCampaign(DynamicQuestInfo& info, User& user) override;
    void _UserTryReceiveReward(DynamicQuestInfo& info, User& user) override;
    void _TryAddPoint(DynamicQuestInfo& info, const DynamicQuestInfo::PointParam& point) override;
    void _TrySendProgressInfo(DynamicQuestInfo& info, User& user) override;
    void _TrySendScoreBoard(DynamicQuestInfo& info, User& user) override;
};

class DynamicQuestState_Wait final : public DynamicQuestState_Sleep {
public:
    ~DynamicQuestState_Wait() override;
    bool CanExecutedByCondition() override;
    void _StartDynamicQuest(DynamicQuestInfo& info) override;
    void _StopDynamicQuest(DynamicQuestInfo& info) override;
    void _ContinueCampaign(DynamicQuestInfo& info, CCampaign* campaign) override;
    void _ChangeCampaign(DynamicQuestInfo& info, CCampaign* campaign) override;
    void _UserTryStartCampaign(DynamicQuestInfo& info, User& user) override;
};

class DynamicQuestState_Working final : public DynamicQuestStateHandler {
public:
    ~DynamicQuestState_Working() override;
    void _OnStart(DynamicQuestInfo& info) override;
    void _OnTick(DynamicQuestInfo& info) override;
    void _OnEnd(DynamicQuestInfo& info) override;
    bool CanExecutedByCondition() override;
    int GetTimeLeft(DynamicQuestInfo& info) override;
    void _StartDynamicQuest(DynamicQuestInfo& info) override;
    void _StopDynamicQuest(DynamicQuestInfo& info) override;
    void _ContinueCampaign(DynamicQuestInfo& info, CCampaign* campaign) override;
    void _ChangeCampaign(DynamicQuestInfo& info, CCampaign* campaign) override;
    void _UserTryStartCampaign(DynamicQuestInfo& info, User& user) override;
    void _UserTryRequestCampaignHTML(DynamicQuestInfo& info, User& user) override;
    void _UserTryAcceptCampaign(DynamicQuestInfo& info, User& user) override;
    void _OnUserAcceptCampaign(DynamicQuestInfo& info, User& user) override;
    void _UserTryReceiveReward(DynamicQuestInfo& info, User& user) override;
    void _TryAddPoint(DynamicQuestInfo& info, const DynamicQuestInfo::PointParam& point) override;
    void _TrySendProgressInfo(DynamicQuestInfo& info, User& user) override;
    void _TrySendScoreBoard(DynamicQuestInfo& info, User& user) override;
};

class DynamicQuestState_End : public DynamicQuestStateHandler {
public:
    ~DynamicQuestState_End() override;
    int GetTimeLeft(DynamicQuestInfo& info) override;
};

class DynamicQuestState_Reward final : public DynamicQuestState_End {
public:
    ~DynamicQuestState_Reward() override;
    void _OnStart(DynamicQuestInfo& info) override;
    void _OnTick(DynamicQuestInfo& info) override;
    void _OnEnd(DynamicQuestInfo& info) override;
    bool CanExecutedByCondition() override;
    void _StartDynamicQuest(DynamicQuestInfo& info) override;
    void _StopDynamicQuest(DynamicQuestInfo& info) override;
    void _ContinueCampaign(DynamicQuestInfo& info, CCampaign* campaign) override;
    void _ChangeCampaign(DynamicQuestInfo& info, CCampaign* campaign) override;
    void _UserTryStartCampaign(DynamicQuestInfo& info, User& user) override;
    void _UserTryRequestCampaignHTML(DynamicQuestInfo& info, User& user) override;
    void _UserTryAcceptCampaign(DynamicQuestInfo& info, User& user) override;
    void _OnUserAcceptCampaign(DynamicQuestInfo& info, User& user) override;
    void _UserTryReceiveReward(DynamicQuestInfo& info, User& user) override;
    void _TryAddPoint(DynamicQuestInfo& info, const DynamicQuestInfo::PointParam& point) override;
    void _TrySendProgressInfo(DynamicQuestInfo& info, User& user) override;
    void _TrySendScoreBoard(DynamicQuestInfo& info, User& user) override;
};

class DynamicQuestState_Failed final : public DynamicQuestState_End {
public:
    ~DynamicQuestState_Failed() override;
    void _OnStart(DynamicQuestInfo& info) override;
    void _OnTick(DynamicQuestInfo& info) override;
    void _OnEnd(DynamicQuestInfo& info) override;
    bool CanExecutedByCondition() override;
    void _StartDynamicQuest(DynamicQuestInfo& info) override;
    void _StopDynamicQuest(DynamicQuestInfo& info) override;
    void _ContinueCampaign(DynamicQuestInfo& info, CCampaign* campaign) override;
    void _ChangeCampaign(DynamicQuestInfo& info, CCampaign* campaign) override;
    void _UserTryStartCampaign(DynamicQuestInfo& info, User& user) override;
    void _UserTryRequestCampaignHTML(DynamicQuestInfo& info, User& user) override;
    void _UserTryAcceptCampaign(DynamicQuestInfo& info, User& user) override;
    void _OnUserAcceptCampaign(DynamicQuestInfo& info, User& user) override;
    void _UserTryReceiveReward(DynamicQuestInfo& info, User& user) override;
    void _TryAddPoint(DynamicQuestInfo& info, const DynamicQuestInfo::PointParam& point) override;
    void _TrySendProgressInfo(DynamicQuestInfo& info, User& user) override;
    void _TrySendScoreBoard(DynamicQuestInfo& info, User& user) override;
};

namespace DynamicQuestState {
extern DynamicQuestStateHandler* Handler[DynamicQuestInfo::PERIOD_MAX];
void Init();
void Clear();
} // namespace DynamicQuestState

class DynamicQuestManager final : public CIOObject {
public:
    struct OnGoingData {
        OnGoingData(int dynamicQuestId, DynamicQuest* dynamicQuest);

        int m_DQuestId; // +0x00
        std::byte m_alignment04To08[4]{};
        DynamicQuestInfo* m_pProgressInfo; // +0x08
    };

    using DynamicQuestMap = std::map<int, DynamicQuest*>;
    using OnGoingList = std::vector<OnGoingData*>;
    using SleepingList = std::list<OnGoingData*>;

    DynamicQuestManager();
    ~DynamicQuestManager() override;

    L2_DECLARE_OBJECT_REGISTRATION(DynamicQuestManager)

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    static DynamicQuestManager* Inst();
    void TimerExpired(int id) override;
    void Init();
    bool AddDynamicQuest(DynamicQuest* quest, DynamicQuestType::Enum type);
    void Load();
    void OnLoaded(const unsigned char* packet);
    void InitNPCMaker();
    void ShowGoal(int dynamicQuestId, int goalId, CUserSocket* socket);
    static bool UserPacketHandler(CSocket* socket, const unsigned char* packet);
    static bool DBPacketHandler(CDBSocket* socket, const unsigned char* packet);
    void OnContinueCampaign(CCampaign* previous, CCampaign* next);
    int OnGoingQuestID();
    bool CanExecuteDQuest();
    void CheckExecution(bool force, int dynamicQuestId);
    void StopDynamicQuest();
    int MakeSerialId();
    void OnUserEnterWorld(User& user);
    void OnUserMove(User& user);
    void OnAddPoint(User& user, std::pair<int, int>& ids, unsigned int goalId, DynamicQuestType::PointType type, int point);
    void OnChangeCampaign(int dynamicQuestId, int campaignId);
    void OnAcceptDynamicQuest(User& user, std::pair<int, int>& ids);
    void OnRequestReward(User& user, std::pair<int, int>& ids);
    void OnRequestRanking(User& user, std::pair<int, int>& ids);
    void OnClientRequestHTML(User& user, std::pair<int, int>& ids);
    void OnRequestProgressInfo(User& user, std::pair<int, int>& ids);
    DynamicQuestInfo* GetProgressInfo(int dynamicQuestId);
    DynamicQuestInfo* GetProgressInfo(std::pair<int, int>& ids, bool ongoingOnly);
    int GetAdjustedGoalRate();
    int GetAdjustedRewardRate();
    void SetPickInformation(int startTime, double randomNo, double previousProbability);
    int GetGoal(int dynamicQuestId, int goalId);
    int GetPoint(int dynamicQuestId, int goalId);

    DynamicQuestSetting::Value DefaultSetting[2]; // +0x18
    DynamicQuestSetting::FHTMLParamName FHTMLParam; // +0x38
    QuestFeedback* m_pDefaultFeedback[1]; // +0x88
    std::map<std::int32_t, int> m_AdjustedGoalRateMap; // +0x90
    std::map<std::int32_t, int> m_AdjustedRewardRateMap; // +0xb0
    std::set<int> m_DynamicQuestIdExcludedFromAdjusting; // +0xd0

private:
    bool Usable() const noexcept { return m_LoadFinished == 1; }
    void InsertToSleepingDCList(OnGoingData* data, unsigned int index);

    DynamicQuestMap m_DynamicQuestMap; // +0xf0
    DynamicQuestMap m_DynamicContentsMap; // +0x110
    DynamicQuestMap m_ZoneQuestMap; // +0x130
    int m_LoadFinished; // +0x150
    int m_SerialId; // +0x154
    OnGoingList m_OnGoingList; // +0x158
    SleepingList m_SleepingDCList; // +0x178

public:
    int m_nLastPickStartTime; // +0x190
    std::byte m_alignment194To198[4]{};
    double m_nLastPickRandomNo; // +0x198
    double m_nLastPickPrevProb; // +0x1a0

private:
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(DynamicQuestManager)
};
