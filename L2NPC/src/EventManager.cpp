#include "EventManager.h"

#include "L2NPC/src/NPCServer.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/IOC.h"
#include "Shared/src/TemplateHelper.h"

namespace l2npc {

bool EventData::IsEventPeriod() {
    // Current L2NPC.exe 0x004296ec.
    L2_GUARD_FUNCTION();
    if (m_PeriodList.empty()) {
        return false;
    }

    const int now = runtime::crt::Time32Now();
    for (TimePeriodInfo& period : m_PeriodList) {
        if (period.IsInTime(now)) {
            return true;
        }
    }
    return false;
}

bool EventData::IsScheduledTime() {
    // Declared by L2NPC.pdb and recovered from matching NASC64_2015.exe
    // 0x004bb990. This L2NPC image leaves the scheduled list empty, but the
    // source-level method remains part of EventData's public surface.
    L2_GUARD_FUNCTION();
    if (m_ScheduledTimeList.empty()) {
        return false;
    }

    const std::int32_t now = runtime::crt::Time32Now();
    for (const ScheduledTimeInfo& scheduled : m_ScheduledTimeList) {
        const std::int32_t end = static_cast<std::int32_t>(static_cast<std::uint32_t>(scheduled.m_ScheduledTime) + 1000U);
        if (scheduled.m_ScheduledTime <= now && now < end) {
            return true;
        }
    }
    return false;
}

void EventData::SetEventData(const unsigned char* packet) {
    // Current L2NPC.exe 0x00429f0c.
    L2_GUARD_FUNCTION();
    int count = 0;
    packet = ::Disassemble(packet, "d", &count);

    int startTime = 0;
    int endTime = 0;
    for (int index = 0; index < count; ++index) {
        // Disassemble leaves an output unchanged when the input is short.
        // The target initializes these two locals once before the loop, so
        // every missing field inherits the preceding iteration's value.
        packet = ::Disassemble(packet, "dd", &startTime, &endTime);
        m_PeriodList.push_back(
            TimePeriodInfo{
                startTime,
                endTime,
            }
        );
    }
}

EventManager& EventManager::Inst() {
    static EventManager manager;
    return manager;
}

void EventManager::LoadEventData() {
    // Current L2NPC.exe 0x00429580.
    L2_GUARD_FUNCTION();
    g_ns.Send("c", 0xe3);
}

void EventManager::SetEventData(const char16_t* eventName, const unsigned char* packet) {
    // Current L2NPC.exe 0x0042b038.
    L2_GUARD_FUNCTION();
    if (!eventName) {
        return;
    }

    EventData eventData;
    eventData.SetEventData(packet);

    L2_GUARD(m_EventLock);
    m_EventData[std::u16string(eventName)] = eventData;
}

bool EventManager::IsEventPeriod(const char16_t* eventName) {
    // Current L2NPC.exe 0x00429a04.
    L2_GUARD_FUNCTION();
    if (!eventName) {
        return false;
    }

    L2_GUARD(m_EventLock);
    const auto found = m_EventData.find(std::u16string(eventName));
    return found != m_EventData.end() && found->second.IsEventPeriod();
}

bool EventManager::IsScheduledTime(const char16_t* eventName) {
    // L2NPC.pdb public surface. Matching NASC64_2015.exe 0x004bbc60 fixes
    // the lock/find/dispatch behavior and source line.
    L2_GUARD_FUNCTION();
    if (!eventName) {
        return false;
    }

    L2_GUARD(m_EventLock);
    const auto found = m_EventData.find(std::u16string(eventName));
    return found != m_EventData.end() && found->second.IsScheduledTime();
}

} // namespace l2npc
