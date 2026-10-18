#include "EventMonitor.h"
#include "Shared/src/ScopedLockGuards.h"

EventMonitor::EventMonitor() = default;

EventMonitor::~EventMonitor() = default;

EventMonitor::MonitorItem* EventMonitor::GetItem(EventType type, std::int64_t param1, std::int64_t param2 [[maybe_unused]]) {

    if (type != ET_KILL_NPC) {
        return nullptr;
    }

    for (MonitorItem& item : m_monitoringList) {
        if (item.m_type == ET_KILL_NPC && item.m_param1 == param1) {
            return &item;
        }
    }
    return nullptr;
}

void EventMonitor::AddMonitorItem(EventType type, std::int64_t param1, std::int64_t param2) {
    {
        L2_WRITE_GUARD(m_lock);
        MonitorItem* item = GetItem(type, param1, param2);
        if (item) {
            ++item->m_refCount;
        } else {
            m_monitoringList.push_back(MonitorItem{1, type, param1, param2});
        }
    }
}

void EventMonitor::RemoveMonitorItem(EventType type, std::int64_t param1, std::int64_t param2) {
    {
        L2_WRITE_GUARD(m_lock);
        MonitorItem* item = GetItem(type, param1, param2);
        if (item) {
            --item->m_refCount;
        }
    }
}

void EventMonitor::OnEvent(EventType type, std::int64_t param1, std::int64_t param2) {
    {
        L2_WRITE_GUARD(m_lock);
        MonitorItem* item = GetItem(type, param1, param2);
        if (item && item->m_type == ET_KILL_NPC) {
            ++item->m_param2;
        }
    }
}
