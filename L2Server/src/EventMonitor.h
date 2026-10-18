#pragma once

#include "Shared/src/RWLock.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class EventMonitor {
public:
    enum EventType : std::int32_t {
        ET_INVALID = 0,
        ET_KILL_NPC = 1,
        ET_GET_ITEM = 2,
    };

    struct MonitorItem {
        std::int32_t m_refCount;
        EventType m_type;
        std::int64_t m_param1;
        std::int64_t m_param2;
    };

    EventMonitor();
    ~EventMonitor();

    void AddMonitorItem(EventType type, std::int64_t param1, std::int64_t param2);
    void RemoveMonitorItem(EventType type, std::int64_t param1, std::int64_t param2);
    void OnEvent(EventType type, std::int64_t param1, std::int64_t param2);
    std::int64_t GetMonitorValue(EventType type, std::int64_t param1, std::int64_t param2);

protected:
    MonitorItem* GetItem(EventType type, std::int64_t param1, std::int64_t param2);
    void DeleteItem(EventType type, std::int64_t param1, std::int64_t param2);

    struct SearchWithParam1;

private:
    RWLock m_lock;
    std::vector<MonitorItem> m_monitoringList;
};
