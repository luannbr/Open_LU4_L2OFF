#include "ToDoList.h"
#include "Shared/src/MemorySub.h"

#include "User.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"

#include <algorithm>
#include <array>
#include <ctime>


CToDoListManager g_ToDoListManager;

COneDayRewardData::COneDayRewardData() :
    m_DBID(0),
    m_ConditionDay(0),
    m_ConditionLevel(0),
    m_PeriodDay(0),
    m_Minute(0),
    m_UseEventXml(false),
    m_ConditionCount(0),
    m_DailyMissionType(E_DAILY_MISSION_TYPE_INVALID),
    m_UseRepeat(false),
    m_UseJobType(E_DAILY_MISSION_JOB_TYPE_INVALID) {}

COneDayRewardData::COneDayRewardData(const COneDayRewardData& other) :
    MemoryObject(other),
    m_DBID(other.m_DBID),
    m_ConditionDay(other.m_ConditionDay),
    m_ConditionLevel(other.m_ConditionLevel),
    m_PeriodDay(other.m_PeriodDay),
    m_Minute(other.m_Minute),
    m_UseEventXml(other.m_UseEventXml),
    m_ConditionCount(other.m_ConditionCount),
    m_DailyMissionType(other.m_DailyMissionType),
    m_UseRepeat(other.m_UseRepeat),
    m_UseJobType(other.m_UseJobType),
    m_RewardItemDatas(other.m_RewardItemDatas),
    m_RewardItems(other.m_RewardItems) {}

COneDayRewardData::~COneDayRewardData() = default;

L2_DEFINE_OBJECT_CLASS_ID(COneDayRewardData)

L2_DEFINE_OBJECT_CLASS_NAME(COneDayRewardData, m_pClassName_COneDayRewardData)

L2_DEFINE_OBJECT_ALLOCATOR(COneDayRewardData, m_pClassName_COneDayRewardData)

L2_DEFINE_OBJECT_NEW(COneDayRewardData)

L2_DEFINE_OBJECT_RELEASE(COneDayRewardData)

void COneDayRewardData::AddRewardItem(int level, int typeId, int amount) {
    m_RewardItemDatas[level].push_back(ItemData{typeId, amount});
}

void COneDayRewardData::AddRewardItem(int typeId, int amount) {
    m_RewardItems.push_back(ItemData{typeId, amount});
    AddRewardItem(-1, typeId, amount);
}

bool COneDayRewardData::OnOnSendRewardItelm(User* user, int, int& rewardItemCount) {
    rewardItemCount = 0;
    if (user == nullptr || user->m_data == nullptr) {
        return false;
    }

    std::array<ItemManip, 20> manipulations{};
    auto append = [&](const std::vector<ItemData>* rewards) {
        if (rewards == nullptr) {
            return;
        }
        for (const ItemData& reward : *rewards) {
            if (rewardItemCount >= static_cast<int>(manipulations.size())) {
                break;
            }
            ItemManip& manipulation = manipulations[static_cast<std::size_t>(rewardItemCount++)];
            // The current body writes the reward class ID into the first
            // ItemManip dword and leaves mClassId at its default zero.
            manipulation.mRelatedQuestID = reward.m_TypeId;
            manipulation.mCount = reward.m_Amount;
        }
    };

    const auto common = m_RewardItemDatas.find(-1);
    append(common == m_RewardItemDatas.end() ? nullptr : &common->second);
    const auto level = m_RewardItemDatas.find(user->m_data->m_nLevel);
    append(level == m_RewardItemDatas.end() ? nullptr : &level->second);

    bool result = true;
    if (rewardItemCount > 0) {
        result = user->HandleItemManipulation(
            rewardItemCount, manipulations.data(), nullptr, 0, nullptr, false, ItemGetReason::Etc
        );
    }
    runtime::raw_atomic::ExchangeI32(&user->m_nWaitGetItemOnce, 0);
    return result;
}

CToDoListManager::CToDoListManager() :
    m_Deposits(nullptr),
    m_Loaded(false) {
    m_DailyMissionTypeByDBIDMap.clear();
    m_DailyMissionRepeatTypeByDBIDMap.clear();
    m_DailyMissionJobTypeByDBIDMap.clear();
    m_DailyMissionDBIDList.clear();
}

CToDoListManager::~CToDoListManager() {
    if (m_Deposits != nullptr) {
        delete m_Deposits;
        m_Deposits = nullptr;
        m_DailyMissionTypeByDBIDMap.clear();
        m_DailyMissionRepeatTypeByDBIDMap.clear();
        m_DailyMissionJobTypeByDBIDMap.clear();
        m_DailyMissionDBIDList.clear();
    }
}

L2_DEFINE_OBJECT_CLASS_ID(CToDoListManager)

L2_DEFINE_OBJECT_CLASS_NAME(CToDoListManager, m_pClassName_CToDoListManager)

L2_DEFINE_OBJECT_ALLOCATOR(CToDoListManager, m_pClassName_CToDoListManager)

L2_DEFINE_OBJECT_NEW(CToDoListManager)

L2_DEFINE_OBJECT_RELEASE(CToDoListManager)

std::int32_t CToDoListManager::GetResetTimeForOneDay(const std::int32_t* currentTime) const {
    if (currentTime == nullptr) {
        return -1;
    }
    std::int32_t adjusted = *currentTime;
    std::tm local{};
    if (!runtime::crt::LocalTime32(adjusted, local)) {
        return -1;
    }
    if (local.tm_hour < 6 || (local.tm_hour == 6 && local.tm_min < 30)) {
        adjusted -= 0x6270;
        if (!runtime::crt::LocalTime32(adjusted, local)) {
            return -1;
        }
    }
    local.tm_sec = 0;
    local.tm_hour = 6;
    local.tm_min = 30;
    return runtime::crt::Mktime32(&local);
}

void CToDoListManager::OnDailyMissionCheckAndUpdate(User* user, E_DAILY_MISSION_TYPE missionType) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        user->OnDailyMissionCheckAndUpdate(missionType);
    }
}

bool CToDoListManager::IsDailyMissionQuest(int questId) {
    L2_GUARD_FUNCTION();
    return m_DailyMissionQuestIDSet.empty() || m_DailyMissionQuestIDSet.find(questId) != m_DailyMissionQuestIDSet.end();
}

E_DAILY_MISSION_TYPE CToDoListManager::GetDailyMissionTypeByID(const int databaseId) const {
    L2_GUARD_FUNCTION();
    const auto found = m_DailyMissionTypeByDBIDMap.find(databaseId);
    return found == m_DailyMissionTypeByDBIDMap.end() ? E_DAILY_MISSION_TYPE_INVALID : found->second;
}

bool CToDoListManager::IsRepeatTypeByID(const int databaseId) const {
    L2_GUARD_FUNCTION();
    const auto found = m_DailyMissionRepeatTypeByDBIDMap.find(databaseId);
    return found != m_DailyMissionRepeatTypeByDBIDMap.end() && found->second;
}

E_DAILY_MISSION_JOB_TYPE CToDoListManager::GetJobTypeByID(int databaseId) const {
    const auto found = m_DailyMissionJobTypeByDBIDMap.find(databaseId);
    return found == m_DailyMissionJobTypeByDBIDMap.end() ? E_DAILY_MISSION_JOB_TYPE_INVALID : found->second;
}

bool CToDoListManager::OnSendReward(User* user, int databaseId, int requestId, int& rewardItemCount) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    if (runtime::raw_atomic::ExchangeI32(&user->m_nWaitGetItemOnce, 1) != 1) {
        // The current method still continues after publishing the wait flag.
    }
    if (m_Deposits == nullptr) {
        runtime::raw_atomic::ExchangeI32(&user->m_nWaitGetItemOnce, 0);
        return false;
    }
    CSPointer<COneDayRewardData> reward = m_Deposits->FindObject(databaseId);
    if (!reward) {
        runtime::raw_atomic::ExchangeI32(&user->m_nWaitGetItemOnce, 0);
        return false;
    }
    return reward->OnOnSendRewardItelm(user, requestId, rewardItemCount);
}

bool CToDoListManager::IsOneDayRewardData(int databaseId) {
    L2_GUARD_FUNCTION();
    return m_Deposits != nullptr && static_cast<bool>(m_Deposits->FindObject(databaseId));
}

CSPointer<COneDayRewardData> CToDoListManager::GetOneDayRewardData(int databaseId) {
    L2_GUARD_FUNCTION();
    return m_Deposits == nullptr ? CSPointer<COneDayRewardData>{} : m_Deposits->FindObject(databaseId);
}

CToDoListManager::DailyMissionList CToDoListManager::GetDailyMissionList() const {
    L2_GUARD_FUNCTION();
    return m_DailyMissionDBIDList;
}

bool CToDoListManager::InsertOneDayReward(COneDayRewardData* reward) {
    if (reward == nullptr) {
        return false;
    }
    if (m_Deposits == nullptr) {
        m_Deposits = new OneDayRewardObjectMap;
    }
    return m_Deposits->Insert(reward->GetDBID(), reward);
}

void CToDoListManager::RegisterDailyMission(
    int databaseId, E_DAILY_MISSION_TYPE type, bool repeat, E_DAILY_MISSION_JOB_TYPE jobType
) {
    m_DailyMissionTypeByDBIDMap[databaseId] = type;
    m_DailyMissionRepeatTypeByDBIDMap[databaseId] = repeat;
    m_DailyMissionJobTypeByDBIDMap[databaseId] = jobType;
    m_DailyMissionDBIDList.insert(databaseId);
}

void CToDoListManager::RegisterDailyMissionQuest(int questId) { m_DailyMissionQuestIDSet.insert(questId); }
