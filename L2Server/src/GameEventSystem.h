#pragma once

#include "CouponKey.h"
#include "Protocol/src/L2Constants.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/IOC.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/YieldLock.h"
#include "Skill.h"
#include "ToDoList.h"

#include <array>
#include <cstddef>
#include <cstdint>

struct XMLElement;
class NpcSocket;
class User;
class CItem;
class CCreature;
class CNPC;
class BalthusEvent;
class KalieEvent;
class VariationDB;

namespace GAME_EVENT {

enum EVENT_TYPE : std::int32_t {
    ET_DROP = 0,
    ET_XMAS_SANTA = 1,
    ET_YEAR_CATCH = 2,
    ET_CORETIME_TELEPORT = 3,
    ET_FISHING_SETTING = 4,
    ET_PCCAFE_COUPON_EVENT = 5,
    ET_PCCAFE_SETTING = 6,
    ET_WORLD = 7,
    ET_BUFF = 8,
    ET_VOTE_SYSTEM = 9,
    ET_COMMONDROP_SYSTEM = 10,
    ET_TEST = 11,
    ET_APOLOGY_NPC = 12,
    ET_AGATHION = 13,
    ET_KALIE = 14,
    ET_VARIATION = 15,
    ET_ONETIME = 16,
    ET_INVITATION = 17,
    ET_RECIPE_SUCCESS_RATE = 18,
    ET_ECONOMY_CONTROL = 19,
    ET_LUCKY_GAME = 20,
    ET_ACCOUNT_EVENT = 21,
    ET_DYNAMIC_PVPZONE = 22,
    ET_BALTHUS = 25,
    ET_TODOLIST_NAME = 26,
    ET_DORMANT_BUFF_EVENT = 27,
    ET_MAX = 28,
};

enum BALTHUS_EVENT_INFO : std::int32_t {
    BALTHUS_NO_CHANGE = -1,
    BALTHUS_OFF = 0,
    BALTHUS_ON = 1,
};

enum DROP_SEQUENCE : std::int32_t {
    DT_RANDOM = 0,
    DT_FIX = 1,
    DT_MAX = 2,
};

} // namespace GAME_EVENT

struct UserDropCond {
    static constexpr std::int32_t INVALID_VALUE = -1;

    std::int32_t m_nMinLev = INVALID_VALUE;
    std::int32_t m_nMaxLev = INVALID_VALUE;
    std::int32_t m_nOwnItemClassId = INVALID_VALUE;
    std::int64_t m_nSkillNameId = INVALID_VALUE;
    std::int64_t m_nOwnItemAmount = INVALID_VALUE;
};

struct DropItemInfo {
    std::int32_t m_nAmountPerServer = 0;
    std::int32_t m_nItemClassID = 0;
    std::int32_t m_nAmount = 0;
    double m_nProb = 0.0;
    std::array<double, 4> m_nConditionProb{};
};

struct DropItemInfoOnTick {
    volatile std::int32_t m_nMaxDropItemAmount = 0;
    volatile std::int32_t m_nCurDropItemAmount = 0;
    volatile double m_dUserRatio = 1.0;
    GAME_EVENT::DROP_SEQUENCE m_eDropSequence = GAME_EVENT::DT_MAX;
    double m_dDropProb = 100.0;
    double m_dPremiumUserDropProb = 0.0;
    double m_dFreeUserDropProb = 0.0;
    UserDropCond m_UserDropCond;
    std::int32_t m_nNPCMinLevel = 0;
    std::int32_t m_nNPCMaxLevel = 0;
    std::vector<DropItemInfo> m_vDropItemList;

    bool IsInNPCLevel(int level);
    bool CheckUserCondition(User* user);
};

struct CommonDropItemInfoOnTick {
    std::int32_t m_nNPCRace = -2;
    std::int32_t m_nPCStatus = 0;
    DropItemInfoOnTick m_baseDropInfoOnTick;

    bool IsEqualNPCRace(RaceType race);
};

class CEvent {
public:
    CEvent();
    virtual ~CEvent();

    virtual bool Load(XMLElement* element) = 0;
    virtual void Init();
    virtual void ShowSetting(User* user) = 0;
    virtual bool InEventTime();
    virtual bool InMyTick(int tick);
    virtual bool InMyDropTick(int tick);
    virtual void DoTickAction(int tick);
    virtual bool IsAllDone();
    virtual int GetMaxDropItemAmount();
    virtual void InitDropItemInfo(CommonDropItemInfoOnTick& info);
    virtual void InitDropItemInfo(DropItemInfoOnTick& info);
    virtual bool ShouldBeRegistered() const;
    virtual double GetUserRatio();
    virtual std::size_t GetEventPeriod(std::vector<TimePeriodInfo>& periods);
    virtual GAME_EVENT::EVENT_TYPE GetEventType() const = 0;

    void StartGetRandomUser();
    void TryToBeWinner(User* user);

    const char16_t* GetName() const noexcept { return m_sEventName.data(); }

    std::int32_t GetToDayPriority() const noexcept { return m_ToDayPriority; }

    void SetToDayPriority(std::int32_t value) noexcept { m_ToDayPriority = value; }

    std::int32_t GetStartTimeHour() const noexcept { return m_StartTimeHour; }

    void SetStartTimeHour(std::int32_t value) noexcept { m_StartTimeHour = value; }

    std::int32_t GetEndTimeHour() const noexcept { return m_EndTimeHour; }

    void SetEndTimeHour(std::int32_t value) noexcept { m_EndTimeHour = value; }

protected:
    friend class CEventManager;
    friend class NpcSocket;
    friend class User;

    void LoadRootElement(XMLElement* element);

    std::int32_t m_EventId;
    bool m_bRegister;
    std::array<char16_t, 256> m_sEventName;
    std::int32_t m_EnterSystemMsg;
    std::int32_t m_ManagerNpcId;
    bool m_EventStart;
    volatile std::int32_t m_RandomUserCount;

public:
    TimePeriodInfo m_EventPeriod;

protected:
    std::int32_t m_nCurrentPeriod;
    bool m_bToDoListCheck;

private:
    std::int32_t m_ToDayPriority;
    std::int32_t m_StartTimeHour;
    std::int32_t m_EndTimeHour;
    std::array<std::byte, 0x04> m_alignment23cTo240;
};

class CEvent_YearCatch final : public CEvent {
public:
    CEvent_YearCatch() : m_nTick(0), m_bIsActive(false), m_nOnStart(0), m_nOnSpawn(0) {}
    ~CEvent_YearCatch() override = default;

    bool Load(XMLElement* element) override;
    void ShowSetting(User*) override {}
    bool InEventTime() override;
    bool InMyTick(const int tick) override { return m_nTick > 0 && m_nTick <= tick && tick % m_nTick == 0; }
    void DoTickAction(int tick) override;
    bool IsAllDone() override { return m_EventPeriod.m_nEndTime < runtime::crt::Time32Now(); }
    GAME_EVENT::EVENT_TYPE GetEventType() const override { return GAME_EVENT::ET_YEAR_CATCH; }

    void SetOn() { runtime::raw_atomic::ExchangeI32(&m_nOnStart, 1); }
    void SetOff() { runtime::raw_atomic::ExchangeI32(&m_nOnStart, 0); }
    void SetSpawnOn() { runtime::raw_atomic::ExchangeI32(&m_nOnSpawn, 1); }
    void SetSpawnOff() { runtime::raw_atomic::ExchangeI32(&m_nOnSpawn, 0); }
    bool SetSpawnFlag() { return runtime::raw_atomic::CompareExchangeI32(&m_nOnSpawn, 0, 1) == 1; }
    bool CheckEventWinCondStart() { return runtime::raw_atomic::LoadI32Relaxed(&m_nOnStart) == 1; }
    bool CheckEventWinCondSpawn() { return runtime::raw_atomic::LoadI32Relaxed(&m_nOnSpawn) == 1; }
    void SetActive(const bool active) { m_bIsActive = active; }
    bool IsActive() { return m_bIsActive; }

private:
    std::int32_t m_nTick; // +0x240
    bool m_bIsActive; // +0x244
    volatile std::int32_t m_nOnStart; // +0x248
    volatile std::int32_t m_nOnSpawn; // +0x24c
};

class CEvent_VoteSystem final : public CEvent {
public:
    // The current 0x0081636c factory initializes only CEvent. These derived
    // scalar lanes keep default-initialization until Load.
    CEvent_VoteSystem() = default;
    ~CEvent_VoteSystem() override = default;

    bool Load(XMLElement* element) override;
    void ShowSetting(User* user) override;
    bool InMyTick(int tick) override { return m_nTick > 0 && m_nTick <= tick && tick % m_nTick == 0; }
    void DoTickAction(int) override {}
    bool IsAllDone() override { return m_EventPeriod.m_nEndTime < runtime::crt::Time32Now(); }
    GAME_EVENT::EVENT_TYPE GetEventType() const override { return GAME_EVENT::ET_VOTE_SYSTEM; }

    int GetVoteCount() const noexcept { return m_nVoteCount; }
    int GetBonusTimeLimit() const noexcept { return m_nBonusTimeLimit; }
    int GetBonusRate() const noexcept { return m_nBonusRate; }
    int GetPlayTime() const noexcept { return m_nPlayTime; }
    int GetPlayTimeBaseValue() const noexcept { return m_nValue; }

private:
    std::int32_t m_nVoteCount; // +0x240
    std::int32_t m_nBonusTimeLimit; // +0x244
    std::int32_t m_nBonusRate; // +0x248
    std::int32_t m_nPlayTime; // +0x24c
    std::int32_t m_nValue; // +0x250
    std::int32_t m_nTick; // +0x254
};

class CEvent_CoretimeTeleport final : public CEvent {
public:
    struct HourPeriodInfo {
        std::int32_t m_nStartHour;
        std::int32_t m_nEndHour;

        static bool ParseTime(const char16_t* time, std::int32_t& value);
    };

    struct CoretimeInfo {
        HourPeriodInfo m_Time;
        std::int32_t m_nPriceRatio;

        bool InCoretime() const;
    };

    struct WeekDayInfo {
        std::int32_t m_nStartWDay;
        std::int32_t m_nEndWDay;

        bool InWeekDay() const;
    };

    CEvent_CoretimeTeleport() = default;
    ~CEvent_CoretimeTeleport() override = default;

    bool Load(XMLElement* element) override;
    void ShowSetting(User* user) override;
    bool InEventTime() override;
    bool InMyTick(int tick) override;
    void DoTickAction(int tick) override;
    bool IsAllDone() override;
    GAME_EVENT::EVENT_TYPE GetEventType() const override;

    std::int32_t ConvertToCoretimePrice(std::int32_t price);
    bool IsInCoretime();

private:
    std::int32_t m_nTick;
    CoretimeInfo m_Coretime;
    WeekDayInfo m_WeekDay;
};

struct SpawnTimes {
    std::u16string m_sMakerEventName;
    std::vector<TimePeriodInfo> m_vSpawnTimes;
};

struct CNPCInfo {
    CNPCInfo();
    ~CNPCInfo();

    // The current constructor does not initialize this field.
    std::int32_t m_nTick;
    TimePeriodInfo m_SpawnPeriod;
    std::vector<SpawnTimes> m_vSpawnEvents;
};

class CEvent_InvitationEvent : public CEvent {
public:
    CEvent_InvitationEvent();
    ~CEvent_InvitationEvent() override;

    bool Load(XMLElement* element) override;
    void ShowSetting(User* user) override;
    bool InMyTick(int tick) override;
    void DoTickAction(int tick) override;
    bool IsAllDone() override;
    GAME_EVENT::EVENT_TYPE GetEventType() const override;

    int GetTickInkEnergy() const noexcept { return m_nTickInkEnergy; }
    int GetMaxInkEnergy() const noexcept { return m_nMaxInkEnergy; }
    int GetTick() const noexcept { return m_nTick; }

private:
    std::int32_t m_nTickInkEnergy;
    std::int32_t m_nMaxInkEnergy;
    std::int32_t m_nTick;
};

class CEvent_Kalie : public CEvent {
public:
    struct KalieInfo {
        std::int32_t m_nState;
        std::int32_t m_nItemClassID;
        float m_fConstA;
        std::int32_t m_nConstB;
        std::int32_t m_nToken;
    };

    using KalieInfoMap = std::map<std::int32_t, KalieInfo>;

    CEvent_Kalie();
    ~CEvent_Kalie() override;

    bool Load(XMLElement* element) override;
    void ShowSetting(User* user) override;
    bool InEventTime() override;
    bool InMyTick(int tick) override;
    void DoTickAction(int tick) override;
    bool IsAllDone() override;
    GAME_EVENT::EVENT_TYPE GetEventType() const override;

    int GetKalieInfoSize();
    float CalculateGoalCnt(std::int32_t state);
    int RewardItem(std::int32_t state);
    int TheNumberOfToken(std::int32_t state);

private:
    friend class KalieEvent;

    KalieInfoMap m_mapKalieInfo; // +0x240
    CNPCInfo m_NPCInfo; // +0x260

    bool LoadNPC(XMLElement* element);

public:
    std::int32_t m_nRewardMinLevel; // +0x290
    std::int32_t m_nRewardMinChangedClass; // +0x294
};

class CEvent_DynamicPvPZone : public CEvent {
public:
    CEvent_DynamicPvPZone();
    ~CEvent_DynamicPvPZone() override;

    bool Load(XMLElement* element) override;
    void ShowSetting(User* user) override;
    bool InEventTime() override;
    bool InMyTick(int tick) override;
    void DoTickAction(int tick) override;
    bool IsAllDone() override;
    bool ShouldBeRegistered() const override;
    GAME_EVENT::EVENT_TYPE GetEventType() const override;

    bool IsInTime();

private:
    static constexpr int DAY_OF_THE_WEEK = 7;

    TimePeriodInfo m_tpEventSeason; // +0x240
    std::array<bool, DAY_OF_THE_WEEK> m_bEventWeeks; // +0x248
    std::int32_t m_nEventStartTime; // +0x250
    std::int32_t m_nEventEndTime; // +0x254
    std::deque<std::u16string> m_mapNPCMaker; // +0x258
    std::deque<std::u16string> m_mapArea; // +0x288
    bool m_isAreaActive; // +0x2b8
    bool m_isNPCServerActive; // +0x2b9
};

class CEvent_ToDoListName : public CEvent {
public:
    CEvent_ToDoListName();
    ~CEvent_ToDoListName() override;

    bool Load(XMLElement* element) override;
    void ShowSetting(User* user) override;
    bool InEventTime() override;
    bool InMyTick(int tick) override;
    void DoTickAction(int tick) override;
    bool IsAllDone() override;
    GAME_EVENT::EVENT_TYPE GetEventType() const override;

private:
    // The current factory does not initialize this lane before Load.
    std::int32_t m_nTick; // +0x240
};

struct BalthusReward {
    std::int32_t m_nItemId = 0;
    std::int32_t m_nTokenCount = 0;
};

struct BalthusSet {
    std::vector<BalthusReward> m_vRewards;
    std::int32_t m_nStartTime = 0;
    std::int32_t m_nEndTime = 0;
};

struct BalthusNpcInfo {
    std::u16string m_sMakerEventName;
    std::vector<TimePeriodInfo> m_vPeriod;
};

class CEvent_Balthus final : public CEvent {
public:
    CEvent_Balthus();
    ~CEvent_Balthus() override;

    bool Load(XMLElement* element) override;
    void Init() override;
    void ShowSetting(User* user) override;
    bool InEventTime() override;
    void DoTickAction(int tick) override;
    bool IsAllDone() override;
    GAME_EVENT::EVENT_TYPE GetEventType() const override;

    int GetCurrentState() const;
    int GetWinningProbability(int progress) const;
    int GetCurrentRewardItem();
    int GetCurrentTokenCount();
    int GetRewardMinLevel() const noexcept { return m_nMinimumLevel; }
    int GetRewardMinChangedClass() const noexcept { return m_nMinimumChangedClass; }
    int BuilderCmd_GetRewardItem(int state);
    int BuilderCmd_GetTokenCount(int state);

private:
    friend class BalthusEvent;

    void Clear();
    bool LoadNPC(XMLElement* element);
    bool ValidateRewards() const;
    void ShuffleRewards();
    void UpdateRewardSetInfo(int state);

    std::int32_t m_nNpcTick;
    std::int32_t m_nNpcStartTime;
    std::int32_t m_nNpcEndTime;
    std::array<std::byte, 0x04> m_alignment24cTo250{};
    std::vector<BalthusNpcInfo> m_vNpcInfo;
    std::vector<BalthusSet> m_vRewardSet;
    std::array<std::int32_t, 5> m_arrWinningProbability;
    std::int32_t m_nStartTime;
    std::int32_t m_nMinimumLevel;
    std::int32_t m_nMinimumChangedClass;
    std::int32_t m_nMinimumUserCount;
    float m_fMinimumUserPenalty;
    std::int32_t m_nCurrentRewardSet;
    std::int32_t m_nCurrentRewardIndex;
};

enum BrEventWorldState : std::int32_t {
    BR_EVENT_STOP = 0,
    BR_EVENT_DOING = 1,
};

// enum BR_BUFF_EVENT_TYPE : std::int32_t {
//     BR_BUFF_EVENT_NONE = 0,
//     BR_BUFF_EVENT_EXP_PER = 1,
//     BR_BUFF_EVENT_GIVE_ITEM = 2,
//     BR_BUFF_EVENT_MAX = 3,
// }

class CEvent_Buff final : public CEvent {
public:
    struct BuffEventInfo {
        BuffEventInfo();

        BR_BUFF_EVENT_TYPE buffType;
        bool state;
        std::array<std::byte, 0x03> alignment05To08{};
        std::int32_t value;
        std::int32_t broadcast;
    };

    CEvent_Buff();
    ~CEvent_Buff() override;

    bool Load(XMLElement* element) override;
    void ShowSetting(User* user) override;
    bool InEventTime() override;
    bool InMyTick(int tick) override;
    void DoTickAction(int tick) override;
    bool IsAllDone() override;
    GAME_EVENT::EVENT_TYPE GetEventType() const override;

    const BuffEventInfo* GetBuffEventInfo() { return &m_BuffEventInfo; }
    void SetBuffEventInfo(BR_BUFF_EVENT_TYPE buffType, int state, int value, int broadcast) {
        m_BuffEventInfo.buffType = buffType;
        m_BuffEventInfo.state = state != 0;
        m_BuffEventInfo.value = value;
        m_BuffEventInfo.broadcast = broadcast;
    }
    TimePeriodInfo GetTimePeriodInfo() { return m_EventPeriod; }
    void SetBuffEventState(bool state);
    void SetBuffEventDuration(int duration);

private:
    std::int32_t m_nTick;
    [[maybe_unused]] bool m_bDoing;
    [[maybe_unused]] std::array<std::byte, 0x03> m_alignment245To248{};
    BuffEventInfo m_BuffEventInfo;
    TimePeriodInfo m_EventPeriod;
};

class CEvent_World final : public CEvent {
public:
    struct EventStateInfo {
        std::int32_t eventId;
        BrEventWorldState state;
        std::array<std::int32_t, 5> param;
        std::array<char16_t, 256> paramString1;
        std::array<char16_t, 256> paramString2;
        std::int32_t rankingCheckTime;
        std::int32_t currentCheckTime;
    };

    CEvent_World();
    ~CEvent_World() override;

    bool Load(XMLElement* element) override;
    void ShowSetting(User* user) override;
    bool InEventTime() override;
    bool InMyTick(int tick) override;
    void DoTickAction(int tick) override;
    GAME_EVENT::EVENT_TYPE GetEventType() const override;

    const EventStateInfo* GetEventStateInfo() { return &m_EventStateInfo; }
    void SetEventDay(int day);
    int GetDate(std::int32_t time);
    int GetRoundNo(std::int32_t time);
    void CheckEventState();
    void SendScheduledTime();

private:
    std::vector<std::int32_t> m_ScheduledTimeList;
    EventStateInfo m_EventStateInfo;
    TimePeriodInfo m_EventPeriod;
};

class CEvent_PCCafe final : public CEvent {
public:
    enum EventType : std::int32_t {
        Nothing = 0,
        GettingPoint = 1,
        UsingPoint = 2,
    };

    enum EventCaseType : std::int32_t {
        ECT_HOMEEVENT = 0,
        ECT_DAILYGIVE = 1,
    };

    enum BonusType : std::int32_t {
        BT_EXP = 1,
        BT_PVP_POINT = 2,
        BT_ITEM = 3,
        BT_POINT = 4,
        BT_BUFFSKILL = 5,
        BT_MAX = 6,
    };

    struct PCCafe {
        std::int32_t m_nPoint = 0;
        std::int32_t m_nRandomPointProb = 0;
        std::vector<TimePeriodInfo> m_vGettingPeriod;
    };

    struct HomePCCafe {
        std::int32_t m_nPoint = 0;
        std::int32_t m_nRandomPointProb = 0;
        bool m_bGettingSet = false;
        bool m_bGettingMaxSet = false;
        std::array<std::byte, 0x02> m_alignment276To278{};
        std::int32_t m_nGettingMaxCount = 0;
        std::vector<TimePeriodInfo> m_vGettingPeriod;
    };

    struct DailyPCCafe {
        std::int32_t m_nPoint = 0;
        std::array<std::byte, 0x04> m_alignment2a4To2a8{};
        std::vector<TimePeriodInfo> m_vGettingPeriod;
    };

    struct PCCafeBonus {
        BonusType m_nBonusType = static_cast<BonusType>(0);
        std::int32_t m_nBonusValue = 0;
        std::int32_t m_nBonusPointTime = 0;
        std::int32_t m_nBonusItemClassID = 0;
        std::int32_t m_nBonusLevel = 0;
        std::array<std::byte, 0x04> m_alignment314To318{};
        std::vector<TimePeriodInfo> m_vGettingPeriod;
    };

    using DailyGiveRecordType = std::pair<std::uint32_t, bool>;
    using DailyGiveRecordListType = std::vector<DailyGiveRecordType>;

    CEvent_PCCafe();
    ~CEvent_PCCafe() override;

    bool Load(XMLElement* element) override;
    void ShowSetting(User* user) override;
    bool InEventTime() override;
    bool InMyTick(int tick) override;
    void DoTickAction(int tick) override;
    bool IsAllDone() override;
    std::size_t GetEventPeriod(std::vector<TimePeriodInfo>& periods) override;
    GAME_EVENT::EVENT_TYPE GetEventType() const override;

    EventType GetPCCafeEventType();
    int GetPCCafeEventRemainHour();
    bool GetHomeUserEventSet();
    bool GetUserEventEnabled(int eventCase);
    int GetHomeDailyMaxCount() const noexcept { return m_HomePCCafe.m_nGettingMaxCount; }
    bool GetHomeDailyMaxSet() const noexcept { return m_HomePCCafe.m_bGettingMaxSet; }
    int GetSettingTime() const noexcept { return m_nSettingSec; }
    int GetPCCafePoint(int randomValue) const noexcept;
    int GetPCCafeRandomPoint(int randomValue) const noexcept;
    int GetHomePCCafePoint() const noexcept { return m_HomePCCafe.m_nPoint; }
    int GetHomePCCafeRandomPoint() const noexcept { return m_HomePCCafe.m_nRandomPointProb; }
    bool CheckDayPassed(int time) const;
    void ResetDailyGiveRecord();
    bool CanGiveDailyPoint(std::uint32_t userDatabaseId);
    int GetDailyGivePoint() const noexcept { return m_DailyPCCafe.m_nPoint; }
    int GetPCCafeExpBonus();
    int GetPCCafePVPPointBonus();
    int GetPCCafeLevelBonus(int bonusType, int level);
    int GetPCCafeItemBonus(int bonusType, int level);
    int GetPCCafeBuffSkillBonus(int bonusType);
    int CheckPCCafeBonusItem(int point, int level);
    void SetDailyResetTime(int time) noexcept { m_nDailyResetTime = time; }
    int GetDailyResetTime() const noexcept { return m_nDailyResetTime; }
    void SetPCCafeBonusTime(int value);

private:
    friend class User;

    std::int32_t m_nTick;
    std::int32_t m_nSettingSec;
    PCCafe m_PCCafe;
    HomePCCafe m_HomePCCafe;
    DailyPCCafe m_DailyPCCafe;
    bool m_bGettingPCCafePoint;
    bool m_bUsingPCCafePoint;
    std::array<std::byte, 0x02> m_alignment2caTo2cc{};
    std::int32_t m_nSavedDate;
    CYieldLock mLock;
    DailyGiveRecordListType m_DailyGiveRecordList;
    std::vector<PCCafeBonus> m_PCCafeBonuses;
    std::int32_t m_nDailyResetTime;
    std::array<std::byte, 0x04> m_alignment324To328{};
    std::array<const wchar_t*, BT_MAX> m_sBonusType;
    std::vector<TimePeriodInfo> m_vUsingPeriod;
};

struct XmasSantaEventInfo {
    bool m_bIsActive = false;
    std::array<std::byte, 0x03> m_alignment01To04{};
    std::int32_t m_nBuffID = 0;
    std::int32_t m_nOnStartSanta = 1;
    std::int32_t m_nOnStartTurky = 1;
};

class CEvent_XmasSanta final : public CEvent {
public:
    CEvent_XmasSanta();
    ~CEvent_XmasSanta() override;

    bool Load(XMLElement* element) override;
    void ShowSetting(User* user) override;
    bool InEventTime() override;
    bool InMyTick(int tick) override;
    void DoTickAction(int tick) override;
    bool IsAllDone() override;
    GAME_EVENT::EVENT_TYPE GetEventType() const override;

    XmasSantaEventInfo* GetEventInfo(int buffId);

private:
    std::int32_t m_nTick;
    [[maybe_unused]] std::array<std::byte, 0x04> m_alignment244To248{};
    std::map<std::int32_t, XmasSantaEventInfo, std::less<std::int32_t>> m_mapBuffs;
};

class CEvent_VariationEvent final : public CEvent {
public:
    CEvent_VariationEvent();
    ~CEvent_VariationEvent() override;

    bool Load(XMLElement* element) override;
    void ShowSetting(User* user) override;
    bool InEventTime() override;
    bool IsAllDone() override;
    GAME_EVENT::EVENT_TYPE GetEventType() const override;

private:
    friend class VariationDB;

    std::int32_t nCancelFeePercentage; // +0x240
    std::int32_t nGemStoneCountPercentage; // +0x244
};

class CEvent_OneTimeEvent final : public CEvent {
public:
    CEvent_OneTimeEvent();
    ~CEvent_OneTimeEvent() override;

    bool Load(XMLElement* element) override;
    void ShowSetting(User* user) override;
    bool InEventTime() override;
    bool InMyTick(int tick) override;
    void DoTickAction(int tick) override;
    bool IsAllDone() override;
    GAME_EVENT::EVENT_TYPE GetEventType() const override;

    bool SetItemGiveToUser(int databaseId);
    XmasWishCardResult::Enum IsUserReceived(int databaseId);
    void GiveItem(User& user);

private:
    std::map<std::int32_t, std::int64_t, std::less<std::int32_t>> m_mapItemInfo;
    [[maybe_unused]] std::array<std::byte, 0x08> m_alignment258To260;
    [[maybe_unused]] std::int32_t m_nEventNumber;
    CYieldLock m_UserInfoLock;
    CYieldLock m_ItemGiveLock;
    [[maybe_unused]] std::array<std::byte, 0x04> m_alignment284To288;
    std::map<std::int32_t, bool, std::less<std::int32_t>> m_mapUserInfo;
};

class CEvent_PCCafeCoupon final : public CEvent {
public:
    enum EventState : std::int32_t {
        NOT_DEFINED = -1,
        NOT_IN_EVENT = 0,
        IN_EVENT = 1,
        IN_EXCHANGE = 2,
    };

    struct CheckData {
        std::int32_t m_nCount = 0;
        std::int32_t m_nBlockedTime = 0;
    };

    struct CouponItem {
        std::int32_t m_nLevel = 0;
        std::int32_t m_nItemClassID = 0;
    };

    CEvent_PCCafeCoupon();
    ~CEvent_PCCafeCoupon() override;

    bool Load(XMLElement* element) override;
    void ShowSetting(User* user) override;
    bool InEventTime() override;
    bool InMyTick(int tick) override;
    void DoTickAction(int tick) override;
    bool IsAllDone() override;
    std::size_t GetEventPeriod(std::vector<TimePeriodInfo>& periods) override;
    GAME_EVENT::EVENT_TYPE GetEventType() const override;

    CCouponKey Reversed(const char16_t* value) const;
    bool Find(const CCouponKey& key) const;
    bool IsValidUser(int databaseId, int& remainingMinutes);
    int FailedInputCoupon(int databaseId);
    int GetCouponItemId(int level) const;
    void CalcEventState();

private:
    friend class User;

    std::int32_t m_nTick;
    std::int32_t m_nOrdinal;
    CCouponKeyMgr m_CouponKeyMgr;
    EventState m_EventState;
    std::map<std::int32_t, CheckData> m_BlockedUsers;
    CYieldLock m_Lock;
    std::array<TimePeriodInfo, 2> m_EventPeriod;
    std::vector<CouponItem> m_vCouponItems;
};

class CEvent_RecipeSuccessRate final : public CEvent {
public:
    CEvent_RecipeSuccessRate();
    ~CEvent_RecipeSuccessRate() override;

    bool Load(XMLElement* element) override;
    void ShowSetting(User* user) override;
    bool InEventTime() override;
    bool InMyTick(int tick) override;
    void DoTickAction(int tick) override;
    bool IsAllDone() override;
    GAME_EVENT::EVENT_TYPE GetEventType() const override;

    int FindRecipeMap(int recipeId);
    int GetRecipeSuccessRate(int recipeId);

private:
    std::int32_t m_EventState;
    std::int32_t m_nDefaultSuccessRate;
    std::map<std::int32_t, std::int32_t, std::less<std::int32_t>> m_mapRecipeSuccessRate;
};

class CEvent_EconomyControlEvent final : public CEvent {
public:
    struct EconomyControl {
        std::int32_t m_nMinLevel;
        std::int32_t m_nMaxLevel;
        double m_dRate;
        std::int32_t m_nRate;
        std::array<std::byte, 0x04> m_alignment14To18;
    };

    CEvent_EconomyControlEvent();
    ~CEvent_EconomyControlEvent() override;

    bool Load(XMLElement* element) override;
    void ShowSetting(User* user) override;
    bool InEventTime() override;
    bool IsAllDone() override;
    GAME_EVENT::EVENT_TYPE GetEventType() const override;

    double GetAdenaDropRate(int level);
    double GetItemDropRate(int level);
    double GetSpoilDropRate(int level);
    double GetExpSpGetRate(int level);

private:
    std::vector<EconomyControl> m_vAdenaDropRateControl;
    std::vector<EconomyControl> m_vItemDropRateControl;
    std::vector<EconomyControl> m_vSpoilDropRateControl;
    std::vector<EconomyControl> m_vExpSpGetRateControl;
    std::array<double, 121> m_arrAdenaDropRatePerLevel;
    std::array<double, 121> m_arrItemDropRatePerLevel;
    std::array<double, 121> m_arrSpoilDropRatPerLevel;
    std::array<double, 121> m_arrExpSpGetRatePerLevel;
};

class CEvent_AccountEvent final : public CEvent {
public:
    struct ItemRewardInfo {
        std::int32_t nItemClassId = 0;
        std::array<std::byte, 0x04> m_alignment04To08{};
        std::int64_t nAmount = 0;
    };

    using ItemRewardInfoMap = std::map<int, ItemRewardInfo>;
    using SendMessageTimeMap = std::map<int, int>;

    CEvent_AccountEvent();
    ~CEvent_AccountEvent() override;

    bool Load(XMLElement* element) override;
    void ShowSetting(User* user) override;
    bool InEventTime() override;
    bool IsAllDone() override;
    GAME_EVENT::EVENT_TYPE GetEventType() const override;

    void LoadStamp(XMLElement* element);
    void LoadAttandanceRewardItem(XMLElement* element);
    bool CanSendMsg(int accountId);
    void SetSendMsg(int accountId, bool sent);

private:
    friend class User;

    std::int32_t m_nEventTick;
    SharedTm32 m_InitTime;

public:
    std::int32_t m_nMaxStamp;
    std::int32_t m_nResetInterval;
    bool m_bPremiumOnly;
    std::array<std::byte, 0x07> m_alignment271To278{};
    ItemRewardInfoMap m_mapAttendReward;
    std::int32_t m_Type;
    std::int32_t m_nAttendMinLevel;
    std::array<CYieldLock, 16> m_lock;
    std::array<SendMessageTimeMap, 16> m_mapTimeTable;
};

class CEvent_DormantBuffEvent final : public CEvent {
public:
    CEvent_DormantBuffEvent() = default;
    ~CEvent_DormantBuffEvent() override = default;

    bool Load(XMLElement* element) override;
    void ShowSetting(User* user) override;
    bool InEventTime() override;
    GAME_EVENT::EVENT_TYPE GetEventType() const override { return GAME_EVENT::ET_DORMANT_BUFF_EVENT; }

    CSkillKey& GetNewbiewBuff() { return m_newbieSkill; }
    CSkillKey& GetDormantBuff() { return m_dormantSkill; }

private:
    CSkillKey m_newbieSkill; // +0x240
    CSkillKey m_dormantSkill; // +0x248
};

struct TimeVariableInfo {
    std::int32_t m_nStartHour = 0;
    std::int32_t m_nEndHour = 0;
    std::int32_t m_nVariable = 0;
};

struct TickVariableInfo {
    std::int32_t m_nMinimumUserCount = 0;
    std::int32_t m_nMaximumUserCount = 0;
    std::int32_t m_nVariable = 0;
};

struct CDropInfo {
    CDropInfo();
    ~CDropInfo();

    void Add(DropItemInfo info);
    void Add(TimeVariableInfo info);
    bool InPeriod();
    int GetRealMaxDropItemAmount(int variable);
    int GetMaxDropItemAmount();
    void SetUserRatio();
    void InitDropItemInfo(DropItemInfoOnTick& info);
    int GetPeriod(std::int32_t time);
    int GetCurrentPeriod();

    std::int32_t m_nTick;
    TimePeriodInfo m_TotalPeriod;
    GAME_EVENT::DROP_SEQUENCE m_eDropSequence;
    double m_dDropProb;
    double m_dPremiumUserDropProb;
    double m_dFreeUserDropProb;
    UserDropCond m_UserDropCond;
    std::int32_t m_nMaxVariables;
    std::int32_t m_nTotalItemAmount;
    std::int32_t m_nNPCMinLevel;
    std::int32_t m_nNPCMaxLevel;
    std::int32_t m_nMinimumPlayingUserCount;
    std::int32_t m_nBasePlayingUserCount;
    double m_dUserRatio;
    std::vector<TimePeriodInfo> m_vDropPeriods;
    std::vector<DropItemInfo> m_vDropItemList;
    std::vector<TimeVariableInfo> m_vTimeVariables;
    std::vector<TickVariableInfo> m_vTickSettingVariable;
};

class CEvent_LuckyGameEvent final : public CEvent {
public:
    CEvent_LuckyGameEvent();
    ~CEvent_LuckyGameEvent() override;

    bool Load(XMLElement* element) override;
    void ShowSetting(User* user) override;
    bool InEventTime() override;
    bool InMyTick(int tick) override;
    void DoTickAction(int tick) override;
    bool IsAllDone() override;
    bool ShouldBeRegistered() const override;
    double GetUserRatio() override;
    GAME_EVENT::EVENT_TYPE GetEventType() const override;

    bool LoadNPC(XMLElement* element);
    void SetCommonIndex(int index);
    void SetPremiumIndex(int index);

private:
    friend class User;

    CNPCInfo m_NPCInfo; // +0x240
    std::int32_t m_nCommonIndex; // +0x270
    std::int32_t m_nPremiumIndex; // +0x274
};

class CEvent_AgathionDrop final : public CEvent {
public:
    CEvent_AgathionDrop();
    ~CEvent_AgathionDrop() override;

    bool Load(XMLElement* element) override;
    void ShowSetting(User* user) override;
    bool InEventTime() override;
    bool InMyTick(int tick) override;
    bool InMyDropTick(int tick) override;
    void DoTickAction(int tick) override;
    bool IsAllDone() override;
    int GetMaxDropItemAmount() override;
    void InitDropItemInfo(DropItemInfoOnTick& info) override;
    double GetUserRatio() override;
    std::size_t GetEventPeriod(std::vector<TimePeriodInfo>& periods) override;
    GAME_EVENT::EVENT_TYPE GetEventType() const override;

    bool LoadDrop(XMLElement* element);
    bool LoadNPC(XMLElement* element);
    void SetEventDropTime(bool enabled);
    void SetDropProb(double probability);
    void SetDropProb(std::u16string probability);
    void SetDropSequence(std::u16string sequence);
    void DiableAllNpcPosEvent();
    double GetProb(User* user, DropItemInfo* info);
    bool IsConquested();
    bool IsForceReachGoal() noexcept;
    bool IsReachGoal() noexcept;
    void SetForceReachGoal(bool force);
    int GetAgathionID() const noexcept;

    CDropInfo& GetDropInfo() noexcept;
    CNPCInfo& GetNPCInfo() noexcept;

private:
    CDropInfo m_DropInfo;
    CNPCInfo m_NPCInfo;
    // Like the target constructor, these two scalar fields do
    // not receive explicit initialization before Load/SetForceReachGoal.
    std::int32_t m_AgathionID;
    bool m_ForceReachGoal;
    bool m_ReachGoal;
    [[maybe_unused]] std::array<std::byte, 0x02> m_alignment35eTo360;
};

struct CCommonDropInfo {
    CCommonDropInfo();
    ~CCommonDropInfo();

    void InitCommonDropItemInfo(CommonDropItemInfoOnTick& info);

    std::int32_t m_nNPCRace;
    std::int32_t m_nPCStatus;
    CDropInfo m_baseDropInfo;
};

class CEvent_CommonDropSystem final : public CEvent {
public:
    CEvent_CommonDropSystem();
    ~CEvent_CommonDropSystem() override;

    bool Load(XMLElement* element) override;
    void ShowSetting(User* user) override;
    bool InEventTime() override;
    bool InMyTick(int tick) override;
    bool InMyDropTick(int tick) override;
    void DoTickAction(int tick) override;
    bool IsAllDone() override;
    int GetMaxDropItemAmount() override;
    double GetUserRatio() override;
    std::size_t GetEventPeriod(std::vector<TimePeriodInfo>& periods) override;
    GAME_EVENT::EVENT_TYPE GetEventType() const override;

    // Current CEvent_CommonDropSystem has one extra vslot after the
    // CEvent surface. It is distinct from CEvent's two
    // InitDropItemInfo overloads.
    virtual void InitCommonDropItemInfo(CommonDropItemInfoOnTick& info);

    bool LoadDrop(XMLElement* element);
    bool LoadPC(XMLElement* element);
    const wchar_t* FindNPCRaceString(int race) const;
    int FindPCStatus(const wchar_t* status) const;
    int FindRaceType(const wchar_t* race) const;
    void SetDropSequence(std::u16string sequence);
    void SetDropProb(std::u16string probability);
    void SetDropProb(double probability);
    void SetEventDropTime(bool enabled);

private:
    CCommonDropInfo m_DropInfo;
};

class CEvent_Drop final : public CEvent {
public:
    using TickSettingVariable = TickVariableInfo;
    using DropInfoPrefix = CDropInfo;

    CEvent_Drop();
    ~CEvent_Drop() override;

    bool Load(XMLElement* element) override;
    void ShowSetting(User* user) override;
    bool InEventTime() override;
    bool InMyTick(int tick) override;
    bool InMyDropTick(int tick) override;
    void DoTickAction(int tick) override;
    bool IsAllDone() override;
    int GetMaxDropItemAmount() override;
    void InitDropItemInfo(DropItemInfoOnTick& info) override;
    double GetUserRatio() override;
    std::size_t GetEventPeriod(std::vector<TimePeriodInfo>& periods) override;
    GAME_EVENT::EVENT_TYPE GetEventType() const override;

    void UpdateTick(int playingUserCount);
    bool LoadDrop(XMLElement* element);
    bool LoadNPC(XMLElement* element);
    void SetEventDropTime(bool enabled);
    void SetDropProb(double probability);
    void SetPremiumUserDropProb(double probability);
    void SetFreeUserDropProb(double probability);
    void SetDropSequence(std::u16string sequence);
    void SetDropProb(std::u16string probability);
    void SetPremiumUserDropProb(std::u16string probability);
    void SetFreeUserDropProb(std::u16string probability);
    void DiableAllNpcPosEvent();

private:
    friend class CEventManager;

    CDropInfo m_DropInfo; // +0x240
    CNPCInfo m_NPCInfo; // +0x328
};

class CEventManager : public CIOObject, public Singleton<CEventManager> {
public:
    CEventManager();
    ~CEventManager() override;

    L2_DECLARE_OBJECT_REGISTRATION(CEventManager)

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    L2_DECLARE_OBJECT_ALLOCATORS()

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void TimerExpired(int id) override;

    void Init(std::u16string filename);
    void InitFromDB();
    void ReplyLoadEventFromDB(int index, int total, std::u16string filename, std::u16string contents);

    CEvent* GetEvent(GAME_EVENT::EVENT_TYPE type);
    CEvent* GetEvent(GAME_EVENT::EVENT_TYPE type, int eventId);
    CEvent* GetEvent(std::u16string name);
    void GetEvent(GAME_EVENT::EVENT_TYPE type, std::vector<CEvent*>& events);
    void GetToDoListInfo(ToDoListRecommandInfoGroupMap& information, bool entering, User* user);
    bool SetEventEnable(std::u16string name, bool enabled);
    bool SetEventEnable(GAME_EVENT::EVENT_TYPE type, bool enabled);
    bool _SetEventEnable(std::u16string name, bool enabled);
    void SendEnterSystemMsg(User* user);
    void UpdateDropTick(int playingUserCount);
    void SetBroadcastPickItemMsg(int itemId, int messageId);
    void OnPickUp(User* user, CItem* item);
    void ResetDropItemAmountPerServer();
    int GetDropItemAmountPerServer(int itemClassId);
    void SetDropItemAmountPerServer(int itemClassId, int amount, std::int32_t timestamp);
    bool CheckAndDropItem(CNPC* npc, CCreature* killer, bool attackedByRange);
    void SendEventInfoToNPC();
    bool WriteEventPost(const char16_t* receiver, const char16_t* subject, const char16_t* body);
    int Reload(GAME_EVENT::EVENT_TYPE type);
    int ReloadAll();
    std::u16string GetRelativeEventData(const std::u16string& filename);

private:
    using EventSMap = std::map<std::u16string, CEvent*, runtime::CStringLess<std::u16string>>;
    using DropItemMap = std::map<std::u16string, DropItemInfoOnTick, runtime::CStringLess<std::u16string>>;
    using CommonDropItemMap = std::map<std::u16string, CommonDropItemInfoOnTick, runtime::CStringLess<std::u16string>>;

    bool _RemoveEvent(std::u16string name);
    void _ResetEventTypeMap();
    void _GetEvent(GAME_EVENT::EVENT_TYPE type, std::vector<CEvent*>& events);
    void _RegisterEvent(CEvent* event);
    bool _Reload(std::u16string name);
    bool _ReloadCommonDropSystem(std::u16string name);
    void CommonDropSystemInit();
    void InitDropItem();
    void InitEvent();
    void SaveEventDataFile(const char16_t* filename, const char16_t* contents);
    void LoadCompleted(std::u16string filename);
    void DumpEventDataToDB();
    void _LoadDropItemAmountPerServer();
    bool CanDropEventItem(int probabilityType, bool summonedKiller);
    bool CheckNpcProperties(CNPC* npc);

public:
    std::u16string m_szFileName;
    std::map<std::int32_t, std::u16string> m_mapContent;

private:
    std::int32_t m_nTickCount;
    CYieldLock m_EventManagerLock;
    EventSMap m_smapRegisteredEvent;
    EventSMap m_smapUnregisteredEvent;
    DropItemMap m_smapDropItemList;
    DropItemMap m_smapAgathionDropItemList;
    CommonDropItemMap m_smapCommonDropItemList;
    std::multimap<GAME_EVENT::EVENT_TYPE, CEvent*> m_mmapEventByType;

public:
    std::map<std::int32_t, std::int32_t> m_mapItemAmountPerServer;
    std::int32_t m_nPickItemId;
    std::int32_t m_nPickItemMsg;

    CYieldLock m_Lock;
};
