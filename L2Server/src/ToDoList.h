#pragma once

#include "Protocol/src/L2Constants.h"

#include "DailyMission.h"

#include "Shared/src/IOCPacket.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/Obj.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeObjectMap.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

class User;

enum E_TODOLIST_TYPE : std::int32_t {
    E_TODOLIST_TYPE_INVALID = 0,
    E_TODOLIST_TYPE_EVENT = 1,
    E_TODOLIST_TYPE_INSTANTZONE = 2,
    E_TODOLIST_TYPE_OLYMPIAD = 3,
    E_TODOLIST_TYPE_CASTLEWAR = 4,
    E_TODOLIST_TYPE_DOMINIONWAR = 5,
    E_TODOLIST_TYPE_FORTRESSWAR = 6,
    E_TODOLIST_TYPE_AGITWAR = 7,
    E_TODOLIST_TYPE_ETC = 8,
    E_TODOLIST_TYPE_DAILY_REWARD = 9,
    E_TODOLIST_TYPE_DAILY_BOSS_RAID = 10,
    E_TODOLIST_TYPE_MAX = 11,
};


// enum E_TODOLIST_PROGRESS_TYPE : std::int32_t {
//     E_TODOLIST_PROGRESS_TYPE_INVALID = 0,
//     E_TODOLIST_PROGRESS_TYPE_BEFOR = 1,
//     E_TODOLIST_PROGRESS_TYPE_PROGRESS = 2,
//     E_TODOLIST_PROGRESS_TYPE_FINISH = 3,
//     E_TODOLIST_PROGRESS_TYPE_MAX = 4,
// }

// enum E_TODOLIST_ENTER_TYPE : std::int32_t {
//     E_TODOLIST_ENTER_TYPE_INVALID = 0,
//     E_TODOLIST_ENTER_TYPE_POSSIBLE = 1,
//     E_TODOLIST_ENTER_TYPE_IMPOSSIBLE = 2,
//     E_TODOLIST_ENTER_TYPE_ALLTIME = 3,
//     E_TODOLIST_ENTER_TYPE_MAX = 4,
// }


namespace todo_list_detail {

inline void IncrementPacketCount(short& count) noexcept {
    std::uint16_t bits = 0;
    std::memcpy(&bits, &count, sizeof(bits));
    ++bits;
    std::memcpy(&count, &bits, sizeof(count));
}

} // namespace todo_list_detail

// Current L2Server 0x00752244 / 0x007b1090 / 0x007ab6c0. Unlike the
// TotalChecker2015 donor, the current target stores two 0x28-byte
// std::xwstring carriers, so both records are 0x68 bytes and their scalar
// tails begin at +0x58.
struct ToDoListRecommandInfo {
    ToDoListRecommandInfo() :
        m_ToDoListType(E_TODOLIST_TYPE_INVALID),
        m_TimeStart(0),
        m_TimeEnd(0),
        m_LevelMin(0),
        m_LevelMax(0),
        m_ProgressStateType(E_TODOLIST_PROGRESS_TYPE_INVALID),
        m_Priority(0) {}

    void AssembleToDoInfo(char* buffer, int bufferSize, unsigned int& length, short& count) const {
        const int assembled = ::Assemble(
            buffer + length, bufferSize - static_cast<int>(length), "cSShhhhcc", static_cast<int>(m_ToDoListType),
            m_ToDoListID.c_str(), m_Name.c_str(), static_cast<int>(m_TimeStart), static_cast<int>(m_TimeEnd),
            static_cast<int>(m_LevelMin), static_cast<int>(m_LevelMax),
            static_cast<int>(static_cast<std::int8_t>(m_ProgressStateType)),
            static_cast<int>(static_cast<std::int8_t>(m_Priority))
        );
        if (assembled > 0) {
            length += static_cast<unsigned int>(assembled);
            todo_list_detail::IncrementPacketCount(count);
        }
    }

    E_TODOLIST_TYPE m_ToDoListType; // +0x00
    std::byte m_alignment04To08[4];
    std::u16string m_ToDoListID; // +0x08
    std::u16string m_Name; // +0x30
    std::int16_t m_TimeStart; // +0x58
    std::int16_t m_TimeEnd; // +0x5a
    std::int16_t m_LevelMin; // +0x5c
    std::int16_t m_LevelMax; // +0x5e
    E_TODOLIST_PROGRESS_TYPE m_ProgressStateType; // +0x60
    std::int32_t m_Priority; // +0x64
};

struct ToDoListInstantZoneInfo {
    ToDoListInstantZoneInfo() :
        m_ToDoListType(E_TODOLIST_TYPE_INVALID),
        m_LevelMin(0),
        m_LevelMax(0),
        m_UserCountMin(0),
        m_UserCountMax(1),
        m_EnterType(E_TODOLIST_ENTER_TYPE_INVALID),
        m_Priority(0) {}

    void AssembleToDoInfo(char* buffer, int bufferSize, unsigned int& length, short& count) const {
        const int assembled = ::Assemble(
            buffer + length, bufferSize - static_cast<int>(length), "cSShhhhcc", static_cast<int>(m_ToDoListType),
            m_ToDoListID.c_str(), m_Name.c_str(), static_cast<int>(m_LevelMin), static_cast<int>(m_LevelMax),
            static_cast<int>(m_UserCountMin), static_cast<int>(m_UserCountMax),
            static_cast<int>(static_cast<std::int8_t>(m_EnterType)), static_cast<int>(static_cast<std::int8_t>(m_Priority))
        );
        if (assembled > 0) {
            length += static_cast<unsigned int>(assembled);
            todo_list_detail::IncrementPacketCount(count);
        }
    }

    E_TODOLIST_TYPE m_ToDoListType; // +0x00
    std::byte m_alignment04To08[4];
    std::u16string m_ToDoListID; // +0x08
    std::u16string m_Name; // +0x30
    std::int16_t m_LevelMin; // +0x58
    std::int16_t m_LevelMax; // +0x5a
    std::int16_t m_UserCountMin; // +0x5c
    std::int16_t m_UserCountMax; // +0x5e
    E_TODOLIST_ENTER_TYPE m_EnterType; // +0x60
    std::int32_t m_Priority; // +0x64
};

using ToDoListRecommandInfoMap = std::multimap<int, ToDoListRecommandInfo>;
using ToDoListRecommandInfoGroupMap = std::multimap<int, ToDoListRecommandInfoMap>;


class COneDayRewardData : public MemoryObject {
public:
    struct ItemData {
        std::int32_t m_TypeId = 0;
        std::int32_t m_Amount = 0;
    };

    COneDayRewardData();
    COneDayRewardData(const COneDayRewardData& other);
    ~COneDayRewardData() override;

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    bool OnOnSendRewardItelm(User* user, int requestId, int& rewardItemCount);

    int GetDBID() const noexcept { return m_DBID; }
    int GetConditionDay() const noexcept { return m_ConditionDay; }
    int GetConditionLevel() const noexcept { return m_ConditionLevel; }
    int GetPeriodDay() const noexcept { return m_PeriodDay; }
    int GetMinute() const noexcept { return m_Minute; }
    bool IsUseEventXml() const noexcept { return m_UseEventXml; }
    int GetConditionCount() const noexcept { return m_ConditionCount; }
    E_DAILY_MISSION_TYPE GetDailyMissionType() const noexcept { return m_DailyMissionType; }
    bool IsUseRepeat() const noexcept { return m_UseRepeat; }
    E_DAILY_MISSION_JOB_TYPE GetUseJobType() const noexcept { return m_UseJobType; }

    void SetDBID(int value) noexcept { m_DBID = value; }
    void SetConditionDay(int value) noexcept { m_ConditionDay = value; }
    void SetConditionLevel(int value) noexcept { m_ConditionLevel = value; }
    void SetPeriodDay(int value) noexcept { m_PeriodDay = value; }
    void SetMinute(int value) noexcept { m_Minute = value; }
    void SetUseEventXml(bool value) noexcept { m_UseEventXml = value; }
    void SetConditionCount(int value) noexcept { m_ConditionCount = value; }
    void SetDailyMissionType(E_DAILY_MISSION_TYPE value) noexcept { m_DailyMissionType = value; }
    void SetUseRepeat(bool value) noexcept { m_UseRepeat = value; }
    void SetUseJobType(E_DAILY_MISSION_JOB_TYPE value) noexcept { m_UseJobType = value; }
    void AddRewardItem(int level, int typeId, int amount);
    void AddRewardItem(int typeId, int amount);

private:
    L2_DECLARE_OBJECT_REGISTRATION(COneDayRewardData)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(COneDayRewardData)

    RWLock m_Lock; // +0x18
    std::int32_t m_DBID; // +0x50
    std::int32_t m_ConditionDay; // +0x54
    std::int32_t m_ConditionLevel; // +0x58
    std::int32_t m_PeriodDay; // +0x5c
    std::int32_t m_Minute; // +0x60
    bool m_UseEventXml; // +0x64
    [[maybe_unused]] std::byte m_alignment65To68[3]{};
    std::int32_t m_ConditionCount; // +0x68
    E_DAILY_MISSION_TYPE m_DailyMissionType; // +0x6c
    bool m_UseRepeat; // +0x70
    [[maybe_unused]] std::byte m_alignment71To74[3]{};
    E_DAILY_MISSION_JOB_TYPE m_UseJobType; // +0x74
    std::map<int, std::vector<ItemData>> m_RewardItemDatas; // +0x78
    std::vector<ItemData> m_RewardItems; // +0x98
};


class CToDoListManager : public MemoryObject {
public:
    using OneDayRewardObjectMap = CObjectMap<COneDayRewardData, 1, 5, RWLock, int>;
    using DailyMissionList = std::set<int>;

    CToDoListManager();
    ~CToDoListManager() override;

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    std::int32_t GetResetTimeForOneDay(const std::int32_t* currentTime) const;
    void OnDailyMissionCheckAndUpdate(User* user, E_DAILY_MISSION_TYPE missionType);
    bool IsDailyMissionQuest(int questId);
    E_DAILY_MISSION_TYPE GetDailyMissionTypeByID(const int databaseId) const;
    bool IsRepeatTypeByID(const int databaseId) const;
    E_DAILY_MISSION_JOB_TYPE GetJobTypeByID(int databaseId) const;
    bool OnSendReward(User* user, int databaseId, int requestId, int& rewardItemCount);
    bool IsOneDayRewardData(int databaseId);
    CSPointer<COneDayRewardData> GetOneDayRewardData(int databaseId);
    DailyMissionList GetDailyMissionList() const;
    bool IsLoaded() const noexcept { return m_Loaded; }

    bool InsertOneDayReward(COneDayRewardData* reward);
    void RegisterDailyMission(int databaseId, E_DAILY_MISSION_TYPE type, bool repeat, E_DAILY_MISSION_JOB_TYPE jobType);
    void RegisterDailyMissionQuest(int questId);
    void SetLoaded(bool value) noexcept { m_Loaded = value; }

private:
    L2_DECLARE_OBJECT_REGISTRATION(CToDoListManager)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CToDoListManager)

    RWLock m_Lock; // +0x18
    OneDayRewardObjectMap* m_Deposits; // +0x50
    bool m_Loaded; // +0x58
    [[maybe_unused]] std::byte m_alignment59To60[7]{};
    std::map<int, E_DAILY_MISSION_TYPE> m_DailyMissionTypeByDBIDMap; // +0x60
    std::map<int, bool> m_DailyMissionRepeatTypeByDBIDMap; // +0x80
    std::map<int, E_DAILY_MISSION_JOB_TYPE> m_DailyMissionJobTypeByDBIDMap; // +0xa0
    DailyMissionList m_DailyMissionDBIDList; // +0xc0
    DailyMissionList m_DailyMissionQuestIDSet; // +0xe0
};


extern CToDoListManager g_ToDoListManager;
