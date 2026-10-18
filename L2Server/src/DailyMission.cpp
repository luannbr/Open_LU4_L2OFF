#include "DailyMission.h"
#include "Shared/src/MemorySub.h"

#include "DB.h"
#include "ToDoList.h"
#include "User.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TemplateHelper.h"

#include <ctime>

namespace {


using DailyMissionFactory = GenericFactory<ICDailyMission, E_DAILY_MISSION_TYPE, DailyMissionCreator>;

template <typename T>
ICDailyMission* CreateDailyMission() {
    return new T;
}

#define L2_REGISTER_DAILY_MISSION(Name, Type, ClassName)                                                                    \
    L2_REGISTER_FACTORY_CREATOR(kRegisterDailyMission##Name, DailyMissionFactory, Type, &CreateDailyMission<ClassName>)

L2_REGISTER_DAILY_MISSION(Level, E_DAILY_MISSION_TYPE_LEVEL, CDailyMissionLevel);
L2_REGISTER_DAILY_MISSION(Weekend, E_DAILY_MISSION_TYPE_WEEKEND, CDailyMissionWeekend);
L2_REGISTER_DAILY_MISSION(Month, E_DAILY_MISSION_TYPE_MONTH, CDailyMissionMonth);
L2_REGISTER_DAILY_MISSION(Quest, E_DAILY_MISSION_TYPE_QUESTCOUNT, CDailyMissionQuestCount);
L2_REGISTER_DAILY_MISSION(Olympiad, E_DAILY_MISSION_TYPE_OLYMPIAD, CDailyMissionOlympiad);
L2_REGISTER_DAILY_MISSION(Castle, E_DAILY_MISSION_TYPE_CASTLEWAR, CDailyMissionCastleWar);
L2_REGISTER_DAILY_MISSION(PledgeGame, E_DAILY_MISSION_TYPE_PLEDGEGAME, CDailyMissionPledgeGame);
L2_REGISTER_DAILY_MISSION(Inzone, E_DAILY_MISSION_TYPE_INZONECOUNT, CDailyMissionInzoneCount);
L2_REGISTER_DAILY_MISSION(Fishing, E_DAILY_MISSION_TYPE_FISHINGCOUNT, CDailyMissionFishingCount);
L2_REGISTER_DAILY_MISSION(Pledge, E_DAILY_MISSION_TYPE_PLEDGE, CDailyMissionPledge);
L2_REGISTER_DAILY_MISSION(ItemPk, E_DAILY_MISSION_TYPE_ITEM_PKSHIELD, CDailyMissionItemPkShield);

#undef L2_REGISTER_DAILY_MISSION

} // namespace

ICDailyMission::ICDailyMission() :
    m_DailyMissionType(E_DAILY_MISSION_TYPE_INVALID),
    m_DBID(0),
    m_TimeRewarded(0),
    m_UpdateCheckTime(0),
    m_AccumulateCount(1),
    m_StateType(E_ONE_DAY_REWARD_STATE_TYPE_INVALID),
    m_User(nullptr),
    m_OneDayReward(nullptr) {
    L2_GUARD_FUNCTION();
}

ICDailyMission::~ICDailyMission() = default;

L2_DEFINE_OBJECT_CLASS_ID(ICDailyMission)

L2_DEFINE_OBJECT_CLASS_NAME(ICDailyMission, m_pClassName_ICDailyMission)

L2_DEFINE_OBJECT_ALLOCATOR(ICDailyMission, m_pClassName_ICDailyMission)

L2_DEFINE_OBJECT_NEW(ICDailyMission)

L2_DEFINE_OBJECT_RELEASE(ICDailyMission)

void ICDailyMission::Init(
    User* user,
    const COneDayRewardData* reward,
    int databaseId,
    std::int32_t timeRewarded,
    std::int32_t updateCheckTime,
    int accumulateCount,
    int stateType
) {
    L2_GUARD_FUNCTION();
    m_TimeRewarded = timeRewarded;
    m_User = user;
    m_UpdateCheckTime = updateCheckTime;
    m_AccumulateCount = accumulateCount;
    m_OneDayReward = reward;
    m_DBID = databaseId;
    m_StateType = static_cast<E_ONE_DAY_REWARD_STATE_TYPE>(stateType);
}

void ICDailyMission::IncrementAccumulateCount() {
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::IncrementI32(&m_AccumulateCount);
}

void ICDailyMission::ResetAccumulateCount() {
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::ExchangeI32(&m_AccumulateCount, 0);
}

int ICDailyMission::GetConditionCount() const { return m_OneDayReward ? m_OneDayReward->GetConditionCount() : 0; }

void ICDailyMission::InitCreateData(const SYSTEMTIME&) {}

void ICDailyMission::SetRewardComplete(std::int32_t currentTime) {
    L2_GUARD_FUNCTION();
    m_TimeRewarded = currentTime;
    m_StateType = E_ONE_DAY_REWARD_STATE_TYPE_COMPLETE;
}

void ICDailyMission::CheckTimeAndUpdateAccumulateCount(std::int32_t) {}

bool ICDailyMission::IsShortMakeName() const { return false; }

bool ICDailyMission::_GetOneDayRewardStateByJob(std::int32_t) const {
    L2_GUARD_FUNCTION();
    if (m_User == nullptr || m_OneDayReward == nullptr || m_User->m_data == nullptr) {
        return false;
    }
    switch (m_OneDayReward->GetUseJobType()) {
        case E_DAILY_MISSION_JOB_TYPE_ALLJOB:
            return true;
        case E_DAILY_MISSION_JOB_TYPE_MAINJOB:
            return m_User->m_data->m_nSubJobId == 0;
        case E_DAILY_MISSION_JOB_TYPE_DUALJOB:
            return m_User->m_data->m_IsDualClass;
        default:
            return false;
    }
}

#define DEFINE_DAILY_MISSION_MEMORY(className)                                                                              \
    L2_REGISTER_OBJECT_TYPE(className)                                                                                      \
    int className::__GetClassID() noexcept { return L2_MEMORY_CLASS_ID(className); }                                        \
    const wchar_t* className::__GetClassName() noexcept { return m_pClassName; }                                            \
    void* className::operator new(std::size_t size, bool objectAllocation) {                                                \
        return runtime::process::AllocateMemoryObject(                                                                   \
            size, objectAllocation, L2_MEMORY_CLASS_ID(className), m_pClassName, #className, 0x40                           \
        );                                                                                                                  \
    }                                                                                                                       \
    void* className::operator new(std::size_t size) { return operator new(size, true); }                                    \
    std::int32_t className::Release(const char* callerFile, int callerLine, ObjectRefType, bool) {                          \
        return ReleaseAs(                                                                                                   \
            L2_MEMORY_CLASS_ID(className), #className, L2_SOURCE_FILE, __LINE__, callerFile, callerLine          \
        );                                                                                                                  \
    }

DEFINE_DAILY_MISSION_MEMORY(CDailyMissionLevel)
DEFINE_DAILY_MISSION_MEMORY(CDailyMissionWeekend)
DEFINE_DAILY_MISSION_MEMORY(CDailyMissionMonth)
DEFINE_DAILY_MISSION_MEMORY(CDailyMissionCountPerDay)
DEFINE_DAILY_MISSION_MEMORY(CDailyMissionQuestCount)
DEFINE_DAILY_MISSION_MEMORY(CDailyMissionOlympiad)
DEFINE_DAILY_MISSION_MEMORY(CDailyMissionCastleWar)
DEFINE_DAILY_MISSION_MEMORY(CDailyMissionPledgeGame)
DEFINE_DAILY_MISSION_MEMORY(CDailyMissionInzoneCount)
DEFINE_DAILY_MISSION_MEMORY(CDailyMissionFishingCount)
DEFINE_DAILY_MISSION_MEMORY(CDailyMissionPledge)
DEFINE_DAILY_MISSION_MEMORY(CDailyMissionItemPkShield)

#undef DEFINE_DAILY_MISSION_MEMORY

E_ONE_DAY_REWARD_STATE_TYPE CDailyMissionLevel::GetOneDayRewardState(std::int32_t currentTime) const {
    if (!_GetOneDayRewardStateByJob(currentTime)) {
        return E_ONE_DAY_REWARD_STATE_TYPE_INVALID;
    }

    const std::int32_t resetTime = g_ToDoListManager.GetResetTimeForOneDay(&currentTime);
    int level = m_User->GetLevelForOneDayReward();
    if (m_User->m_data->m_IsDualClass && m_OneDayReward->GetUseJobType() == E_DAILY_MISSION_JOB_TYPE_DUALJOB) {
        level = m_User->Level();
    }

    if (m_TimeRewarded < 1) {
        return level >= m_OneDayReward->GetConditionLevel() ? E_ONE_DAY_REWARD_STATE_TYPE_CAN
                                                            : E_ONE_DAY_REWARD_STATE_TYPE_CAN_NOT;
    }
    if (!m_OneDayReward->IsUseRepeat() && m_TimeRewarded < resetTime) {
        return E_ONE_DAY_REWARD_STATE_TYPE_INVALID;
    }
    return E_ONE_DAY_REWARD_STATE_TYPE_COMPLETE;
}

E_ONE_DAY_REWARD_STATE_TYPE CDailyMissionWeekend::GetOneDayRewardState(std::int32_t currentTime) const {
    L2_GUARD_FUNCTION();
    if (!_GetOneDayRewardStateByJob(currentTime)) {
        return E_ONE_DAY_REWARD_STATE_TYPE_INVALID;
    }
    std::tm local{};
    if (!runtime::crt::LocalTime32(currentTime, local) || (local.tm_wday != 0 && local.tm_wday != 6)) {
        return E_ONE_DAY_REWARD_STATE_TYPE_CAN_NOT;
    }
    const std::int32_t weeklyBind = m_User && m_User->m_data ? m_User->m_data->GetWeeklyBindTime() : 0;
    return m_TimeRewarded > weeklyBind ? E_ONE_DAY_REWARD_STATE_TYPE_COMPLETE : E_ONE_DAY_REWARD_STATE_TYPE_CAN;
}

void CDailyMissionMonth::InitCreateData(const SYSTEMTIME& systemTime) {
    L2_GUARD_FUNCTION();
    if (m_TimeRewarded < 1) {
        m_TimeRewarded = runtime::crt::SystemTimeToTime32(systemTime);
    }
}

E_ONE_DAY_REWARD_STATE_TYPE CDailyMissionMonth::GetOneDayRewardState(std::int32_t currentTime) const {
    L2_GUARD_FUNCTION();
    if (!_GetOneDayRewardStateByJob(currentTime)) {
        return E_ONE_DAY_REWARD_STATE_TYPE_INVALID;
    }
    std::tm previousMonth{};
    if (!runtime::crt::LocalTime32(currentTime, previousMonth)) {
        return E_ONE_DAY_REWARD_STATE_TYPE_INVALID;
    }
    if (--previousMonth.tm_mon < 0) {
        previousMonth.tm_mon = 11;
        --previousMonth.tm_year;
    }
    const std::int32_t previousMonthTime = runtime::crt::Mktime32(&previousMonth);
    if (previousMonthTime < m_TimeRewarded) {
        if (m_AccumulateCount > 0 && currentTime - m_TimeRewarded < 0x15181) {
            return E_ONE_DAY_REWARD_STATE_TYPE_COMPLETE;
        }
        return E_ONE_DAY_REWARD_STATE_TYPE_CAN_NOT;
    }
    return E_ONE_DAY_REWARD_STATE_TYPE_CAN;
}

void CDailyMissionMonth::SetRewardComplete(std::int32_t currentTime) {
    L2_GUARD_FUNCTION();
    ICDailyMission::SetRewardComplete(currentTime);
    IncrementAccumulateCount();
}

E_ONE_DAY_REWARD_STATE_TYPE
CDailyMissionCountPerDay::_GetOneDayRewardState(std::int32_t currentTime) const {
    L2_GUARD_FUNCTION();
    if (!_GetOneDayRewardStateByJob(currentTime)) {
        return E_ONE_DAY_REWARD_STATE_TYPE_INVALID;
    }
    const std::int32_t resetTime = g_ToDoListManager.GetResetTimeForOneDay(&currentTime);
    if (resetTime < m_TimeRewarded) {
        return currentTime - m_TimeRewarded < 0x15181 ? E_ONE_DAY_REWARD_STATE_TYPE_COMPLETE
                                                      : E_ONE_DAY_REWARD_STATE_TYPE_CAN_NOT;
    }
    return m_AccumulateCount >= GetConditionCount() ? E_ONE_DAY_REWARD_STATE_TYPE_CAN : E_ONE_DAY_REWARD_STATE_TYPE_CAN_NOT;
}

void CDailyMissionCountPerDay::_SetRewardComplete(std::int32_t currentTime) {
    L2_GUARD_FUNCTION();
    ICDailyMission::SetRewardComplete(currentTime);
    ResetAccumulateCount();
}

void CDailyMissionCountPerDay::_CheckTimeAndUpdateAccumulateCount(std::int32_t currentTime) {
    L2_GUARD_FUNCTION();
    const std::int32_t resetTime = g_ToDoListManager.GetResetTimeForOneDay(&currentTime);
    if (resetTime < m_TimeRewarded) {
        return;
    }
    if (m_OneDayReward != nullptr && m_AccumulateCount >= GetConditionCount()) {
        return;
    }
    IncrementAccumulateCount();
    if (m_User != nullptr) {
        db.RequestUpdateOneDayRewardInfo(m_User, 2, this);
    }
}

E_ONE_DAY_REWARD_STATE_TYPE
CDailyMissionCountPerDay::GetOneDayRewardState(std::int32_t currentTime) const {
    L2_GUARD_FUNCTION();
    return _GetOneDayRewardState(currentTime);
}

void CDailyMissionCountPerDay::SetRewardComplete(std::int32_t currentTime) {
    L2_GUARD_FUNCTION();
    _SetRewardComplete(currentTime);
}

void CDailyMissionCountPerDay::CheckTimeAndUpdateAccumulateCount(std::int32_t currentTime) {
    L2_GUARD_FUNCTION();
    _CheckTimeAndUpdateAccumulateCount(currentTime);
}

#define DEFINE_COUNT_DAILY_WRAPPERS(className)                                                                              \
    E_ONE_DAY_REWARD_STATE_TYPE className::GetOneDayRewardState(std::int32_t currentTime) const {                           \
        return _GetOneDayRewardState(currentTime);                                                                          \
    }                                                                                                                       \
    void className::SetRewardComplete(std::int32_t currentTime) { _SetRewardComplete(currentTime); }                        \
    void className::CheckTimeAndUpdateAccumulateCount(std::int32_t currentTime) {                                           \
        _CheckTimeAndUpdateAccumulateCount(currentTime);                                                                    \
    }

DEFINE_COUNT_DAILY_WRAPPERS(CDailyMissionQuestCount)
DEFINE_COUNT_DAILY_WRAPPERS(CDailyMissionOlympiad)
DEFINE_COUNT_DAILY_WRAPPERS(CDailyMissionCastleWar)
DEFINE_COUNT_DAILY_WRAPPERS(CDailyMissionPledgeGame)
DEFINE_COUNT_DAILY_WRAPPERS(CDailyMissionInzoneCount)
DEFINE_COUNT_DAILY_WRAPPERS(CDailyMissionFishingCount)

#undef DEFINE_COUNT_DAILY_WRAPPERS

E_ONE_DAY_REWARD_STATE_TYPE CDailyMissionPledge::GetOneDayRewardState(std::int32_t currentTime) const {
    if (!_GetOneDayRewardStateByJob(currentTime)) {
        return E_ONE_DAY_REWARD_STATE_TYPE_INVALID;
    }
    const std::int32_t resetTime = g_ToDoListManager.GetResetTimeForOneDay(&currentTime);
    if (m_TimeRewarded < 1) {
        return m_User->GetPledge() != nullptr ? E_ONE_DAY_REWARD_STATE_TYPE_CAN : E_ONE_DAY_REWARD_STATE_TYPE_CAN_NOT;
    }
    if (!m_OneDayReward->IsUseRepeat() && m_TimeRewarded < resetTime) {
        return E_ONE_DAY_REWARD_STATE_TYPE_INVALID;
    }
    return E_ONE_DAY_REWARD_STATE_TYPE_COMPLETE;
}

E_ONE_DAY_REWARD_STATE_TYPE
CDailyMissionItemPkShield::GetOneDayRewardState(std::int32_t) const {
    L2_GUARD_FUNCTION();
    return E_ONE_DAY_REWARD_STATE_TYPE_INVALID;
}
