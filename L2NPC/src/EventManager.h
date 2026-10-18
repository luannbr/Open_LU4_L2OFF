#pragma once

#include "Shared/src/BaseStructure.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <cstddef>

namespace l2npc {

struct ScheduledTimeInfo {
    ScheduledTimeInfo() = default;
    explicit ScheduledTimeInfo(int scheduledTime) noexcept : m_ScheduledTime(scheduledTime) {}

    int m_ScheduledTime = 0;
};


class EventData {
public:
    EventData() = default;
    ~EventData() = default;

    bool IsEventPeriod();
    bool IsScheduledTime();
    void SetEventData(const unsigned char* packet);

private:
    // The current image keeps both vectors, but SetEventData at
    // 0x00429f0c only populates the period list.
    std::vector<TimePeriodInfo> m_PeriodList;
    std::vector<ScheduledTimeInfo> m_ScheduledTimeList;
};


class EventManager {
public:
    // Exact nested typedef recorded by the PDB. The adjacent PDB's xmap was
    // 0x38. The current image places m_EventLock at +0x20.
    using EventDataType = std::map<std::u16string, EventData>;

    static EventManager& Inst();

    EventManager() = default;
    ~EventManager() = default;

    void LoadEventData();
    void SetEventData(const char16_t* eventName, const unsigned char* packet);
    bool IsEventPeriod(const char16_t* eventName);
    bool IsScheduledTime(const char16_t* eventName);

private:
    EventDataType m_EventData;
    CIOCriticalSection m_EventLock;
};


} // namespace l2npc
