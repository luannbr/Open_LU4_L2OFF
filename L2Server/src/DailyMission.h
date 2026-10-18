#pragma once

#include "Shared/src/BaseStructure.h"
#include "Shared/src/Obj.h"

#include <cstddef>
#include <cstdint>

class COneDayRewardData;
class User;

enum E_DAILY_MISSION_TYPE : std::int32_t {
    E_DAILY_MISSION_TYPE_INVALID = 0,
    E_DAILY_MISSION_TYPE_LEVEL = 1,
    E_DAILY_MISSION_TYPE_WEEKEND = 2,
    E_DAILY_MISSION_TYPE_MONTH = 3,
    E_DAILY_MISSION_TYPE_QUESTCOUNT = 4,
    E_DAILY_MISSION_TYPE_OLYMPIAD = 5,
    E_DAILY_MISSION_TYPE_CASTLEWAR = 6,
    E_DAILY_MISSION_TYPE_PLEDGEGAME = 7,
    E_DAILY_MISSION_TYPE_INZONECOUNT = 8,
    E_DAILY_MISSION_TYPE_FISHINGCOUNT = 9,
    E_DAILY_MISSION_TYPE_PLEDGE = 10,
    E_DAILY_MISSION_TYPE_ITEM_PKSHIELD = 11,
    E_DAILY_MISSION_TYPE_CHINA_QQVIP = 12,
    E_DAILY_MISSION_TYPE_CHINA_QQCHATROOM = 13,
    E_DAILY_MISSION_TYPE_MAX = 14,
};

enum E_DAILY_MISSION_JOB_TYPE : std::int32_t {
    E_DAILY_MISSION_JOB_TYPE_INVALID = 0,
    E_DAILY_MISSION_JOB_TYPE_ALLJOB = 1,
    E_DAILY_MISSION_JOB_TYPE_MAINJOB = 2,
    E_DAILY_MISSION_JOB_TYPE_DUALJOB = 3,
    E_DAILY_MISSION_JOB_TYPE_MAX = 4,
};

enum E_ONE_DAY_REWARD_STATE_TYPE : std::int32_t {
    E_ONE_DAY_REWARD_STATE_TYPE_INVALID = 0,
    E_ONE_DAY_REWARD_STATE_TYPE_CAN = 1,
    E_ONE_DAY_REWARD_STATE_TYPE_CAN_NOT = 2,
    E_ONE_DAY_REWARD_STATE_TYPE_COMPLETE = 3,
    E_ONE_DAY_REWARD_STATE_TYPE_MAX = 4,
};


class ICDailyMission : public MemoryObject {
public:
    ICDailyMission();
    ~ICDailyMission() override;

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    void Init(
        User* user,
        const COneDayRewardData* reward,
        int databaseId,
        std::int32_t timeRewarded,
        std::int32_t updateCheckTime,
        int accumulateCount,
        int stateType
    );
    void IncrementAccumulateCount();
    void ResetAccumulateCount();
    int GetConditionCount() const;

    virtual void InitCreateData(const SYSTEMTIME& systemTime);
    virtual E_ONE_DAY_REWARD_STATE_TYPE GetOneDayRewardState(std::int32_t currentTime) const = 0;
    virtual void SetRewardComplete(std::int32_t currentTime);
    virtual void CheckTimeAndUpdateAccumulateCount(std::int32_t currentTime);
    virtual bool IsShortMakeName() const;

    E_DAILY_MISSION_TYPE m_DailyMissionType; // +0x18
    std::int32_t m_DBID; // +0x1c
    std::int32_t m_TimeRewarded; // +0x20
    std::int32_t m_UpdateCheckTime; // +0x24
    volatile std::int32_t m_AccumulateCount; // +0x28
    E_ONE_DAY_REWARD_STATE_TYPE m_StateType; // +0x2c
    User* m_User; // +0x30
    const COneDayRewardData* m_OneDayReward; // +0x38

protected:
    bool _GetOneDayRewardStateByJob(std::int32_t currentTime) const;

private:
    L2_DECLARE_OBJECT_REGISTRATION(ICDailyMission)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(ICDailyMission)
};


#define DAILY_MISSION_WIDEN_IMPL(value) L##value
#define DAILY_MISSION_WIDEN(value) DAILY_MISSION_WIDEN_IMPL(value)

#define DECLARE_DAILY_MISSION_CLASS(className, baseName)                                                                    \
    class className : public baseName {                                                                                     \
    public:                                                                                                                 \
        className() = default;                                                                                              \
        ~className() override = default;                                                                                    \
        static int __GetClassID() noexcept;                                                                                 \
        static const wchar_t* __GetClassName() noexcept;                                                                    \
        static void* operator new(std::size_t, bool);                                                                       \
        static void* operator new(std::size_t);                                                                             \
        std::int32_t Release(const char*, int, ObjectRefType, bool) override;                                               \
        E_ONE_DAY_REWARD_STATE_TYPE GetOneDayRewardState(std::int32_t currentTime) const override;                          \
                                                                                                                            \
    private:                                                                                                                \
        L2_DECLARE_OBJECT_REGISTRATION(className)                                                                           \
        inline static const wchar_t* m_pClassName = DAILY_MISSION_WIDEN(#className);                                        \
    };

DECLARE_DAILY_MISSION_CLASS(CDailyMissionLevel, ICDailyMission);
DECLARE_DAILY_MISSION_CLASS(CDailyMissionWeekend, ICDailyMission);

class CDailyMissionMonth : public ICDailyMission {
public:
    CDailyMissionMonth() = default;
    ~CDailyMissionMonth() override = default;
    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t, bool);
    static void* operator new(std::size_t);
    std::int32_t Release(const char*, int, ObjectRefType, bool) override;
    void InitCreateData(const SYSTEMTIME& systemTime) override;
    E_ONE_DAY_REWARD_STATE_TYPE GetOneDayRewardState(std::int32_t currentTime) const override;
    void SetRewardComplete(std::int32_t currentTime) override;

private:
    L2_DECLARE_OBJECT_REGISTRATION(CDailyMissionMonth)
    inline static const wchar_t* m_pClassName = L"CDailyMissionMonth";
};

class CDailyMissionCountPerDay : public ICDailyMission {
public:
    CDailyMissionCountPerDay() = default;
    ~CDailyMissionCountPerDay() override = default;
    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t, bool);
    static void* operator new(std::size_t);
    std::int32_t Release(const char*, int, ObjectRefType, bool) override;
    E_ONE_DAY_REWARD_STATE_TYPE GetOneDayRewardState(std::int32_t currentTime) const override;
    void SetRewardComplete(std::int32_t currentTime) override;
    void CheckTimeAndUpdateAccumulateCount(std::int32_t currentTime) override;

protected:
    E_ONE_DAY_REWARD_STATE_TYPE _GetOneDayRewardState(std::int32_t currentTime) const;
    void _SetRewardComplete(std::int32_t currentTime);
    void _CheckTimeAndUpdateAccumulateCount(std::int32_t currentTime);

private:
    L2_DECLARE_OBJECT_REGISTRATION(CDailyMissionCountPerDay)
    inline static const wchar_t* m_pClassName = L"CDailyMissionCountPerDay";
};

#define DECLARE_COUNT_DAILY_MISSION_CLASS(className, shortName)                                                             \
    class className : public CDailyMissionCountPerDay {                                                                     \
    public:                                                                                                                 \
        className() = default;                                                                                              \
        ~className() override = default;                                                                                    \
        static int __GetClassID() noexcept;                                                                                 \
        static const wchar_t* __GetClassName() noexcept;                                                                    \
        static void* operator new(std::size_t, bool);                                                                       \
        static void* operator new(std::size_t);                                                                             \
        std::int32_t Release(const char*, int, ObjectRefType, bool) override;                                               \
        E_ONE_DAY_REWARD_STATE_TYPE GetOneDayRewardState(std::int32_t currentTime) const override;                          \
        void SetRewardComplete(std::int32_t currentTime) override;                                                          \
        void CheckTimeAndUpdateAccumulateCount(std::int32_t currentTime) override;                                          \
        bool IsShortMakeName() const override { return shortName; }                                                         \
                                                                                                                            \
    private:                                                                                                                \
        L2_DECLARE_OBJECT_REGISTRATION(className)                                                                           \
        inline static const wchar_t* m_pClassName = DAILY_MISSION_WIDEN(#className);                                        \
    };

DECLARE_COUNT_DAILY_MISSION_CLASS(CDailyMissionQuestCount, true);
DECLARE_COUNT_DAILY_MISSION_CLASS(CDailyMissionOlympiad, true);
DECLARE_COUNT_DAILY_MISSION_CLASS(CDailyMissionCastleWar, false);
DECLARE_COUNT_DAILY_MISSION_CLASS(CDailyMissionPledgeGame, true);
DECLARE_COUNT_DAILY_MISSION_CLASS(CDailyMissionInzoneCount, true);
DECLARE_COUNT_DAILY_MISSION_CLASS(CDailyMissionFishingCount, true);

DECLARE_DAILY_MISSION_CLASS(CDailyMissionPledge, ICDailyMission);
DECLARE_DAILY_MISSION_CLASS(CDailyMissionItemPkShield, ICDailyMission);

#undef DECLARE_COUNT_DAILY_MISSION_CLASS
#undef DECLARE_DAILY_MISSION_CLASS
#undef DAILY_MISSION_WIDEN
#undef DAILY_MISSION_WIDEN_IMPL

using DailyMissionCreator = ICDailyMission* (*)();
