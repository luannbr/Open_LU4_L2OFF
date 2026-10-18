#include "GameEventSystem.h"

#include "CConfig.h"
#include "DB.h"
#include "User.h"
#include "UserBroadcaster.h"
#include "XmlLoader.h"

#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <cstdint>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <string>

namespace {

bool EqualsIgnoreCase(const std::u16string& left, const char16_t* right) {
    const runtime::CStringLess<std::u16string> less;
    return !less(left, right) && !less(right, left);
}

std::int32_t ToInt32(const std::u16string& value) { return WindowsWtol(Utf16ToUtf8(runtime::StringView(value))); }

std::u16string& Attribute(XMLElement* element, const char16_t* name) { return element->m_smapAttr[std::u16string(name)]; }

} // namespace

int CDropInfo::GetPeriod(const std::int32_t time) {
    // Current L2Server 0x004c878c.
    L2_GUARD_FUNCTION();
    int index = 0;
    for (const TimePeriodInfo& period : m_vDropPeriods) {
        if (period.m_nStartTime <= time && time <= period.m_nEndTime) {
            return index;
        }
        ++index;
    }
    return -1;
}

int CDropInfo::GetCurrentPeriod() {
    // Current L2Server 0x004c8858.
    L2_GUARD_FUNCTION();
    return GetPeriod(runtime::crt::Time32Now());
}

CEvent_Buff::BuffEventInfo::BuffEventInfo() : buffType(BR_BUFF_EVENT_NONE), state(false), value(0), broadcast(0) {}

CEvent_Buff::CEvent_Buff() : m_nTick(60), m_bDoing(false), m_EventPeriod{} {}

CEvent_Buff::~CEvent_Buff() = default;

GAME_EVENT::EVENT_TYPE CEvent_Buff::GetEventType() const { return GAME_EVENT::ET_BUFF; }

bool CEvent_Buff::Load(XMLElement* element) {
    // Current L2Server 0x004c9acc.
    L2_GUARD_FUNCTION();
    if (element == nullptr || element->m_vChild.size() != 4) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load PCCafeSetting. invalid child nodes",
        //     L"br_GameEvent.cpp",
        //     0x120)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load PCCafeSetting. invalid child nodes",
        //     L"br_GameEvent.cpp",
        //     0x120)
        L2_LOG_ERROR(
            Log,
            "Cannot load BuffEvent configuration: the XML node is missing or has %llu children; exactly 4 are required. "
            "This event is not loaded.",
            element != nullptr ? static_cast<unsigned long long>(element->m_vChild.size()) : 0ULL
        );
        return false;
    }
    LoadRootElement(element);
    m_nTick = ToInt32(Attribute(element, u"Tick"));
    return true;
}

void CEvent_Buff::ShowSetting(User*) {}

bool CEvent_Buff::InEventTime() {
    const std::int32_t now = runtime::crt::Time32Now();
    return m_EventPeriod.m_nStartTime <= now && now <= m_EventPeriod.m_nEndTime;
}

bool CEvent_Buff::InMyTick(int) { return true; }

void CEvent_Buff::DoTickAction(int) {
    // Current L2Server 0x004c8668.
    L2_GUARD_FUNCTION();
}

bool CEvent_Buff::IsAllDone() { return m_EventPeriod.m_nEndTime < runtime::crt::Time32Now(); }

void CEvent_Buff::SetBuffEventState(bool state) {
    // Current L2Server 0x004c8ad0.
    L2_GUARD_FUNCTION();
    m_BuffEventInfo.state = state;
    if (!state) {
        m_EventPeriod.m_nStartTime = 0;
        m_EventPeriod.m_nEndTime = 0;
    }
    CUserBroadcaster::inst().BroadcastToAllUser(
        "chdddd", 0xfe, 0xdc, m_BuffEventInfo.buffType, m_BuffEventInfo.value, m_EventPeriod.m_nEndTime, state
    );
}

void CEvent_Buff::SetBuffEventDuration(int duration) {
    // Current L2Server 0x004c8bcc.
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    m_EventPeriod.m_nStartTime = now;
    m_EventPeriod.m_nEndTime = now + duration;
    CUserBroadcaster::inst().BroadcastToAllUser(
        "chdddd", 0xfe, 0xdc, m_BuffEventInfo.buffType, m_BuffEventInfo.value, m_EventPeriod.m_nEndTime,
        m_BuffEventInfo.state
    );
}

CEvent_World::CEvent_World() : m_EventStateInfo{}, m_EventPeriod{} {
    L2_GUARD_FUNCTION();
    m_EventStateInfo.rankingCheckTime = -1;
    m_EventStateInfo.state = BR_EVENT_STOP;
}

CEvent_World::~CEvent_World() = default;

GAME_EVENT::EVENT_TYPE CEvent_World::GetEventType() const { return GAME_EVENT::ET_WORLD; }

bool CEvent_World::Load(XMLElement* element) {
    L2_GUARD_FUNCTION();

    std::memset(&m_EventStateInfo, 0, sizeof(m_EventStateInfo));
    LoadRootElement(element);
    m_EventStateInfo.eventId = ToInt32(Attribute(element, u"EventId"));
    m_EventStateInfo.state = BR_EVENT_STOP;
    m_EventStateInfo.rankingCheckTime = -1;

    for (XMLElement* const child : element->m_vChild) {
        if (!child) {
            continue;
        }

        if (EqualsIgnoreCase(child->m_sName, u"EventTimes")) {
            for (XMLElement* const timeElement : child->m_vChild) {
                if (!timeElement) {
                    continue;
                }
                TimePeriodInfo::ParseTime(Attribute(timeElement, u"Start").c_str(), m_EventPeriod.m_nStartTime);
                TimePeriodInfo::ParseTime(Attribute(timeElement, u"End").c_str(), m_EventPeriod.m_nEndTime);
                break;
            }
            continue;
        }

        if (!EqualsIgnoreCase(child->m_sName, u"RankingCheck")) {
            continue;
        }
        for (XMLElement* const rankingElement : child->m_vChild) {
            if (!rankingElement || !EqualsIgnoreCase(rankingElement->m_sName, u"RankingCheckTime")) {
                continue;
            }

            int hour = 0;
            int minute = 0;
            int second = 0;
            const std::u16string& value = Attribute(rankingElement, u"Time");
            const std::wstring hostValue = Utf16ToWide(runtime::StringView(value));
            std::swscanf(hostValue.c_str(), L"%d:%d:%d", &hour, &minute, &second);
            m_EventStateInfo.rankingCheckTime = (hour * 60 + minute) * 60 + second;
        }
    }

    m_EventStateInfo.currentCheckTime = GetDate(m_EventPeriod.m_nStartTime) + m_EventStateInfo.rankingCheckTime;
    return true;
}

void CEvent_World::ShowSetting(User* user) { static_cast<void>(user); }

bool CEvent_World::InEventTime() {
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    return m_EventPeriod.m_nStartTime <= now && now <= m_EventPeriod.m_nEndTime;
}

bool CEvent_World::InMyTick(int tick) {
    static_cast<void>(tick);
    return true;
}

void CEvent_World::DoTickAction(int tick) {
    static_cast<void>(tick);
    L2_GUARD_FUNCTION();
    if (m_EventStateInfo.state != BR_EVENT_DOING) {
        return;
    }

    const std::int32_t periodNow = runtime::crt::Time32Now();
    if (periodNow < m_EventPeriod.m_nStartTime || m_EventPeriod.m_nEndTime < periodNow ||
        m_EventStateInfo.rankingCheckTime < 0) {
        return;
    }

    const std::int32_t checkNow = runtime::crt::Time32Now();
    if (m_EventStateInfo.currentCheckTime >= checkNow) {
        return;
    }

    const std::int32_t roundNow = runtime::crt::Time32Now();
    db.RequestUpdateEventRanking(m_EventStateInfo.eventId, GetRoundNo(roundNow));
    m_EventStateInfo.currentCheckTime += 86400;
}

void CEvent_World::SetEventDay(int day) {
    // Current L2Server 0x004c8580.
    L2_GUARD_FUNCTION();
    SharedEventInfo::Inst()->m_gameEvent.m_FirstDropTime = runtime::crt::Time32Now() - day * 86400 + 86400;
}

int CEvent_World::GetDate(std::int32_t time) {
    L2_GUARD_FUNCTION();
    const int date = GetYYYYMMDDFromTime(time);

    std::tm local;
    local.tm_sec = 0;
    local.tm_min = 0;
    local.tm_hour = 0;
    local.tm_mday = date % 100;
    local.tm_mon = (date % 10000) / 100 - 1;
    local.tm_year = date / 10000 - 1900;
    return runtime::crt::Mktime32(&local);
}

int CEvent_World::GetRoundNo(std::int32_t time) {
    L2_GUARD_FUNCTION();
    return ((time - m_EventStateInfo.rankingCheckTime - GetDate(m_EventPeriod.m_nStartTime)) + 86400) / 86400;
}

void CEvent_World::CheckEventState() {
    L2_GUARD_FUNCTION();

    BrEventWorldState state;
    if (m_EventStateInfo.state == BR_EVENT_STOP) {
        const std::int32_t now = runtime::crt::Time32Now();
        if (now < m_EventPeriod.m_nStartTime || m_EventPeriod.m_nEndTime < now) {
            return;
        }
        state = BR_EVENT_DOING;
    } else if (m_EventStateInfo.state == BR_EVENT_DOING) {
        const std::int32_t now = runtime::crt::Time32Now();
        if (m_EventPeriod.m_nStartTime <= now && now <= m_EventPeriod.m_nEndTime) {
            return;
        }
        state = BR_EVENT_STOP;
    } else {
        return;
    }

    m_EventStateInfo.state = state;
    CUserBroadcaster::inst().BroadcastToAllUser(
        "chdddddddSS", 0xfe, 0xbd, m_EventStateInfo.eventId, state, m_EventStateInfo.param[0], m_EventStateInfo.param[1],
        m_EventStateInfo.param[2], m_EventStateInfo.param[3], m_EventStateInfo.param[4],
        m_EventStateInfo.paramString1.data(), m_EventStateInfo.paramString2.data()
    );
}

void CEvent_Drop::UpdateTick(int playingUserCount) {
    // Current L2Server 0x004c8dd4.
    L2_GUARD_FUNCTION();
    for (const TickSettingVariable& setting : m_DropInfo.m_vTickSettingVariable) {
        if (setting.m_nMinimumUserCount <= playingUserCount && playingUserCount <= setting.m_nMaximumUserCount) {
            m_DropInfo.m_nTick = setting.m_nVariable;
            return;
        }
    }
}

bool CEventManager::WriteEventPost(const char16_t* receiver, const char16_t* subject, const char16_t* body) {
    // Current L2Server 0x004c9190.
    L2_GUARD_FUNCTION();

    std::array<char, 0x400> itemPacket{};
    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, -1));
    User* const rawUser = user.get();
    // The current owner releases the temporary map reference before it uses
    // the kept raw pointer for the post request.
    user.Release();
    if (rawUser != nullptr) {
        db.RequestWritePost(*rawUser, receiver, 0, subject, body, 0, 0, itemPacket.data(), 0, 0, 0);
    }
    return false;
}

void CEventManager::UpdateDropTick(int playingUserCount) {
    // Current L2Server 0x004c9c14.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EventManagerLock);

    std::vector<CEvent*> events;
    Singleton<CEventManager>::inst()._GetEvent(GAME_EVENT::ET_DROP, events);
    for (CEvent* const event : events) {
        if (event != nullptr) {
            static_cast<CEvent_Drop*>(event)->UpdateTick(playingUserCount);
        }
    }
}

void CEventManager::ResetDropItemAmountPerServer() {
    // Current L2Server 0x004c8e94.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EventManagerLock);
    for (auto& entry : m_mapItemAmountPerServer) {
        entry.second = 0;
        db.RequestSaveDropItemAmountPerServer(entry.first, 0);
    }
}

int CEventManager::GetDropItemAmountPerServer(const int itemClassId) {
    // Current L2Server 0x004c9324.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EventManagerLock);
    const auto found = m_mapItemAmountPerServer.find(itemClassId);
    return found == m_mapItemAmountPerServer.end() ? 0 : m_mapItemAmountPerServer[itemClassId];
}

void CEventManager::SetDropItemAmountPerServer(const int itemClassId, const int amount, const std::int32_t timestamp) {
    // Current L2Server 0x004c9d78.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EventManagerLock);

    std::vector<CEvent*> events;
    Singleton<CEventManager>::inst().GetEvent(GAME_EVENT::ET_DROP, events);
    for (CEvent* const baseEvent : events) {
        auto* const event = static_cast<CEvent_Drop*>(baseEvent);
        for (const DropItemInfo& item : event->m_DropInfo.m_vDropItemList) {
            if (item.m_nItemClassID != itemClassId) {
                continue;
            }
            if (event->m_DropInfo.GetPeriod(timestamp) == event->m_DropInfo.GetCurrentPeriod()) {
                m_mapItemAmountPerServer[itemClassId] = amount;
                return;
            }
        }
    }
}

void CEventManager::SetBroadcastPickItemMsg(int itemId, int messageId) {
    // Current L2Server 0x004c86e4.
    L2_GUARD_FUNCTION();
    m_nPickItemId = itemId;
    m_nPickItemMsg = messageId;
}
