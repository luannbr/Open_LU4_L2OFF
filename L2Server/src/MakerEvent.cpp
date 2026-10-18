#include "Shared/src/Log.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TemplateHelper.h"

#include "MakerEvent.h"

#include "Area.h"
#include "DB.h"
#include "Door.h"
#include "NpcServer.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Exception.h"
#include "Shared/src/TextRuntime.h"

#include <cstring>
#include <string>
#include <string_view>

namespace {

bool TargetUtf16Equals(const std::u16string& left, const char16_t* right) noexcept {
    const char16_t* leftPosition = left.c_str();
    while (*leftPosition == *right) {
        if (*leftPosition == u'\0') {
            return true;
        }
        ++leftPosition;
        ++right;
    }
    return false;
}

bool TargetUtf16Equals(const char16_t* left, const char16_t* right) noexcept {
    while (*left == *right) {
        if (*left == u'\0') {
            return true;
        }
        ++left;
        ++right;
    }
    return false;
}

} // namespace

NpcPosEventManager::NpcPosEventManager() : mTimerId(0), mLastTimerId{-1, -1}, mPadding024(0) {}

void NpcPosEventManager::StartTimer(const int eventIndex, const int days) {
    if (static_cast<unsigned int>(eventIndex) < 2U) {
        mLastTimerId[eventIndex] = mTimerId;
    }

    const std::uint32_t delayBits = static_cast<std::uint32_t>(days) * UINT32_C(86400000);
    const std::int32_t delay = runtime::Int32FromBits(delayBits);
    if (delay > 0) {
        const int timerId = mTimerId;
        mTimerId = runtime::Int32FromBits(static_cast<std::uint32_t>(mTimerId) + 1U);
        AddTimer(delay, timerId);
    }
}

void NpcPosEventManager::TimerExpired(const int id) {
    if (mLastTimerId[0] == id) {
        DisableNpcPosEvent(u"ev_apology");
    } else if (mLastTimerId[1] == id) {
        DisableNpcPosEvent(u"ev_apology_2015");
    }
}

NpcPosEventMap::IEventInfo::IEventInfo(const int enabled) noexcept : mEnabled(enabled), mPadding00c(0) {}

NpcPosEventMap::IEventInfo::~IEventInfo() = default;

NpcPosEventMap::DefaultEventInfo::DefaultEventInfo(const int enabled) noexcept : IEventInfo(enabled) {}

void NpcPosEventMap::DefaultEventInfo::SendToNpcServer(const char16_t* const eventName) {
    L2_GUARD_FUNCTION();
    g_npcserver.Send("cSd", 0x49, eventName, mEnabled);
}

NpcPosEventMap::RewardEventInfo::RewardEventInfo(const int enabled, const NpcPosEventRewardInfo& rewardInfo) noexcept :
    IEventInfo(enabled), mRewardInfo(rewardInfo) {}

void NpcPosEventMap::RewardEventInfo::SendToNpcServer(const char16_t* const eventName) {
    L2_GUARD_FUNCTION();
    g_npcserver.Send(
        "cSddQdddd", 0xc0, eventName, mEnabled, mRewardInfo.nRewardItemEnable, mRewardInfo.nItemCount,
        mRewardInfo.nNpcExistTimeDay, mRewardInfo.nResetInzoneBindingEnabled, mRewardInfo.nResetInzoneBindingStartSec,
        mRewardInfo.nResetInzoneBindingEndSec
    );
}

NpcPosEventMap::NpcPosEventMap() :
    mNpcPosEventManager(new NpcPosEventManager),
    mMakerFinished(false),
    mPadding011{} {}

NpcPosEventMap::~NpcPosEventMap() {
    delete mNpcPosEventManager;
    mNpcPosEventManager = nullptr;
}

void NpcPosEventMap::MakerFinished() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(mLock);
        for (const auto& entry : mPosEvents) {
            const EventInfoPtr& info = entry.second;
            const std::string name = Utf16ToUtf8(entry.first);
            // Log.Add(
            //     LOG_NORMAL,
            //     L"+ NPC POS EVENT : Name[%ls], Enabled[%d]",
            //     name.c_str(),
            //     info->Enabled())
            // L2_LOG_INFO(Log,
            //     L"+ NPC POS EVENT : Name[%ls], Enabled[%d]",
            //     name.c_str(),
            //     info->Enabled())
            L2_LOG_INFO(
                Log, "Synchronizing NPC spawn event '%s' with L2NPC: state %d.", name.c_str(), info->Enabled()
            );
            info->SendToNpcServer(entry.first.c_str());
        }
        mMakerFinished = true;
    }
}

void NpcPosEventMap::OnNpcSocketClosed() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(mLock);
        mMakerFinished = false;
    }
}

NpcPosEventMap::EventInfoPtr NpcPosEventMap::_setPosEvent(const char16_t* const eventName, const int enabled) {
    L2_GUARD_FUNCTION();
    const std::u16string key(eventName);
    auto found = mPosEvents.find(key);
    if (found == mPosEvents.end()) {
        EventInfoPtr created = std::make_shared<DefaultEventInfo>(enabled);
        mPosEvents.insert_or_assign(key, created);
        return mMakerFinished ? created : EventInfoPtr{};
    }

    EventInfoPtr& current = found->second;
    if (current != nullptr && current->Enabled() != enabled) {
        current->Enabled(enabled);
        return mMakerFinished ? current : EventInfoPtr{};
    }
    return {};
}

NpcPosEventMap::EventInfoPtr NpcPosEventMap::setPosEvent(const char16_t* const eventName, const int enabled) {
    L2_GUARD_FUNCTION();
    L2_GUARD(mLock);
    return _setPosEvent(eventName, enabled);
}

NpcPosEventMap::EventInfoPtr NpcPosEventMap::setPosEventWithRewardInfo(
    const char16_t* const eventName, const int enabled, const NpcPosEventRewardInfo& rewardInfo
) {
    L2_GUARD_FUNCTION();
    L2_GUARD(mLock);

    const std::u16string key(eventName);
    const auto found = mPosEvents.find(key);
    if (found != mPosEvents.end() && found->second != nullptr && found->second->Enabled() == enabled) {
        return {};
    }

    EventInfoPtr created = std::make_shared<RewardEventInfo>(enabled, rewardInfo);
    mPosEvents.insert_or_assign(key, created);
    return mMakerFinished ? created : EventInfoPtr{};
}

void NpcPosEventMap::initApologyNpcSeries(const NpcPosEventRewardInfo& rewardInfo, const int eventIndex) {
    L2_GUARD_FUNCTION();
    if (rewardInfo.nRewardItemResetDB == 1) {
        db.RequestClearOneEventTime(0x0c, eventIndex);
    }
    if (rewardInfo.nResetInzoneBindingResetDB == 1) {
        db.RequestClearOneEventTime(0x0c, 2);
    }
    mNpcPosEventManager->StartTimer(eventIndex, rewardInfo.nNpcExistTimeDay);
}

void NpcPosEventMap::TriggerNpcPosEvent(const char16_t* const eventName, const int enabled) {
    L2_GUARD_FUNCTION();
    if (std::char_traits<char16_t>::length(eventName) == 0) {
        return;
    }

    EventInfoPtr info = setPosEvent(eventName, enabled);
    if (info == nullptr) {
        return;
    }
    const std::string name = Utf16ToUtf8(eventName);
    // Log.Add(
    //     LOG_NORMAL,
    //     L"+ NPC POS EVENT : Name[%ls], nEnabled[%d]",
    //     name.c_str(),
    //     enabled)
    // L2_LOG_INFO(Log,
    //     L"+ NPC POS EVENT : Name[%ls], nEnabled[%d]",
    //     name.c_str(),
    //     enabled)
    L2_LOG_INFO(
        Log, "NPC spawn event '%s': sending %s to L2NPC (state %d).", name.c_str(),
        enabled == 1 ? "an enable request" : (enabled == 0 ? "a disable request" : "a state update"), enabled
    );
    info->SendToNpcServer(eventName);
}

void NpcPosEventMap::TriggerRewardNpcPosEvent(
    const char16_t* const eventName, const int enabled, const NpcPosEventRewardInfo& rewardInfo
) {
    L2_GUARD_FUNCTION();
    if (std::char_traits<char16_t>::length(eventName) == 0) {
        return;
    }

    EventInfoPtr info = setPosEventWithRewardInfo(eventName, enabled, rewardInfo);
    if (info == nullptr) {
        return;
    }

    const std::string name = Utf16ToUtf8(eventName);
    // Log.Add(
    //     LOG_NORMAL,
    //     L"+ NPC POS EVENT : Name[%ls], nEnabled[%d]",
    //     name.c_str(),
    //     enabled)
    // L2_LOG_INFO(Log,
    //     L"+ NPC POS EVENT : Name[%ls], nEnabled[%d]",
    //     name.c_str(),
    //     enabled)
    L2_LOG_INFO(
        Log, "NPC reward event '%s': sending %s to L2NPC (state %d).", name.c_str(),
        enabled == 1 ? "an enable request" : (enabled == 0 ? "a disable request" : "a state update"), enabled
    );
    info->SendToNpcServer(eventName);

    if (enabled == 1) {
        if (TargetUtf16Equals(eventName, u"ev_apology")) {
            initApologyNpcSeries(rewardInfo, 0);
        } else if (TargetUtf16Equals(eventName, u"ev_apology_2015")) {
            initApologyNpcSeries(rewardInfo, 1);
        }
    }
}

void EnableNpcPosEvent(const char16_t* const eventName) { NpcPosEventMap::inst().TriggerNpcPosEvent(eventName, 1); }

void DisableNpcPosEvent(const char16_t* const eventName) { NpcPosEventMap::inst().TriggerNpcPosEvent(eventName, 0); }

DoorEventMap::DoorEventMap() :
    mMakerFinished(false), mPadding009{}, mPadding01c{} {}

DoorEventMap::~DoorEventMap() = default;

void DoorEventMap::SetEvent(CDoor* const door, const int eventValue) {
    L2_GUARD_FUNCTION();
    if (door == nullptr) {
        return;
    }

    const char16_t* const doorName = door->GetDoorName();
    const int isOpened = door->IsOpened() ? 1 : 0;
    bool sendEvent = false;
    if (doorName == nullptr || doorName[0] == u'\0') {
        return;
    }

    {
        L2_GUARD(mLock);
        const auto found = mDoorEvents.find(std::u16string(doorName));
        if (found != mDoorEvents.end()) {
            if (isOpened == 0) {
                if (found->second == eventValue) {
                    return;
                }
                found->second = eventValue;
            }
            if (mMakerFinished) {
                sendEvent = true;
            }
        }
    }

    if (sendEvent) {
        g_npcserver.Send("cdSdd", 0x2f, 0, doorName, eventValue, isOpened);
    }
}

void DoorEventMap::OnNpcSocketClosed() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(mLock);
        mMakerFinished = false;
    }
}

void DoorEventMap::RegisterDoorEvent(const char16_t* const doorName) {
    L2_GUARD_FUNCTION();
    const std::string hostDoorName = Utf16ToUtf8(std::u16string_view(doorName));
    CDoor* const door = g_DoorDB.GetDoor(doorName, 0);
    if (door == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] undefined (or unregistered) Door[%s]",
        //     L"MakerEvent.cpp",
        //     0x1a7,
        //     hostDoorName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] undefined (or unregistered) Door[%s]",
        //     L"MakerEvent.cpp",
        //     0x1a7,
        //     hostDoorName.c_str())
        L2_LOG_ERROR(
            Log, "Cannot register NPC events for door '%s' in world 0: the door was not found.",
            hostDoorName.c_str()
        );
        runtime::process::ReportCallStack(2, false);
        return;
    }

    // The current image compares the stored enum cell literally with two.
    if (static_cast<std::int32_t>(door->m_eType) == 2) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Can't regsiter Door[%s]",
        //     L"MakerEvent.cpp",
        //     0x1ae,
        //     hostDoorName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Can't regsiter Door[%s]",
        //     L"MakerEvent.cpp",
        //     0x1ae,
        //     hostDoorName.c_str())
        L2_LOG_WARN(
            Log, "Cannot register NPC events for door '%s': door type %d does not support this registration.",
            hostDoorName.c_str(), static_cast<int>(door->m_eType)
        );
        return;
    }

    L2_GUARD(mLock);
    mDoorEvents[std::u16string(doorName)] = door->IsOpened() ? 1 : 0;
}

void DoorEventMap::MakerFinished() {
    L2_GUARD_FUNCTION();
    L2_GUARD(mLock);
    for (const auto& entry : mDoorEvents) {
        g_npcserver.Send("cdSdd", 0x2f, 0, entry.first.c_str(), entry.second, 0);
    }
    mMakerFinished = true;
}

DoorStatusMap::DoorStatusMap() : mDoorStatusFinished(false), mPadding009{}, mPadding01c{} {}

DoorStatusMap::~DoorStatusMap() = default;

void DoorStatusMap::SetEvent(const char16_t* doorName, const int worldId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(mLock);
    if (std::char_traits<char16_t>::length(doorName) == 0U) {
        return;
    }

    const std::string hostDoorName = Utf16ToUtf8(std::u16string_view(doorName));
    CDoor* const door = g_DoorDB.GetDoor(doorName, static_cast<std::uint32_t>(worldId));
    if (door == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] undefined (or unregistered) Door[%s] - SetEvent",
        //     L"MakerEvent.cpp",
        //     0x24b,
        //     hostDoorName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] undefined (or unregistered) Door[%s] - SetEvent",
        //     L"MakerEvent.cpp",
        //     0x24b,
        //     hostDoorName.c_str())
        L2_LOG_ERROR(
            Log, "Cannot send door '%s' status for world %d to L2NPC: the door was not found.",
            hostDoorName.c_str(), worldId
        );
        runtime::process::ReportCallStack(2, false);
        return;
    }

    for (const auto& entry : mDoorStatus) {
        if (TargetUtf16Equals(entry.first, doorName) && entry.second == worldId) {
            if (mDoorStatusFinished) {
                g_npcserver.Send("cdSdd", 0xa8, 2, doorName, worldId, door->m_data->m_bIsOpen);
            }
            break;
        }
    }
}

void DoorStatusMap::UnRegisterDoorStatus(const char16_t* doorName, const int worldId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(mLock);
    for (auto position = mDoorStatus.begin(); position != mDoorStatus.end(); ++position) {
        if (position->second == worldId && TargetUtf16Equals(position->first, doorName)) {
            mDoorStatus.erase(position);
            return;
        }
    }
}

void DoorStatusMap::RegisterDoorStatus(const char16_t* doorName, const int worldId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(mLock);
    if (!mDoorStatusFinished) {
        mDoorStatusFinished = true;
    }

    const std::string hostDoorName = Utf16ToUtf8(std::u16string_view(doorName));
    CDoor* const door = g_DoorDB.GetDoor(doorName, static_cast<std::uint32_t>(worldId));
    if (door == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] undefined (or unregistered) Door[%s] - Register",
        //     L"MakerEvent.cpp",
        //     0x27f,
        //     hostDoorName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] undefined (or unregistered) Door[%s] - Register",
        //     L"MakerEvent.cpp",
        //     0x27f,
        //     hostDoorName.c_str())
        L2_LOG_ERROR(
            Log,
            "Cannot register door '%s' status for world %d: the door was not found; the status-ready flag is already set.",
            hostDoorName.c_str(), worldId
        );
        runtime::process::ReportCallStack(2, false);
        return;
    }

    mDoorStatus[std::u16string(doorName)] = worldId;
    if (mDoorStatusFinished) {
        g_npcserver.Send("cdSdd", 0xa8, -2, doorName, worldId, door->m_data->m_bIsOpen);
    }
}

AreaStatusMap::AreaStatusMap() : mAreaStatusFinished(false), m_padding009{}, m_padding01c{} {}

AreaStatusMap::~AreaStatusMap() = default;

void AreaStatusMap::SetEvent(const char16_t* areaName, int worldId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(mLock);
    if (std::char_traits<char16_t>::length(areaName) == 0U) {
        return;
    }

    CArea* const area = CAreaDB::inst().FindArea(areaName);
    if (area == nullptr) {
        const std::string hostAreaName = Utf16ToUtf8(areaName);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] undefined (or unregistered) Area[%s] - SetEvent",
        //     L"MakerEvent.cpp",
        //     0x1e4,
        //     hostAreaName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] undefined (or unregistered) Area[%s] - SetEvent",
        //     L"MakerEvent.cpp",
        //     0x1e4,
        //     hostAreaName.c_str())
        L2_LOG_ERROR(
            Log, "Cannot send area '%s' status for world %d to L2NPC: the area was not found.",
            hostAreaName.c_str(), worldId
        );
        runtime::process::ReportCallStack(2, false);
        return;
    }

    for (const auto& entry : mAreaStatus) {
        if (TargetUtf16Equals(entry.first, areaName) && entry.second == worldId) {
            if (mAreaStatusFinished) {
                g_npcserver.Send("cdSdd", 0xa8, 1, areaName, worldId, area->IsOn(worldId));
            }
            break;
        }
    }
}

void AreaStatusMap::UnRegisterAreaStatus(const char16_t* areaName, int worldId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(mLock);
    for (auto position = mAreaStatus.begin(); position != mAreaStatus.end(); ++position) {
        if (position->second == worldId && TargetUtf16Equals(position->first, areaName)) {
            mAreaStatus.erase(position);
            return;
        }
    }
}

void AreaStatusMap::OnNpcSocketClosed() {
    L2_GUARD_FUNCTION();
    L2_GUARD(mLock);
    mAreaStatus.clear();
    mAreaStatusFinished = false;
}

void AreaStatusMap::RegisterAreaStatus(const char16_t* areaName, int worldId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(mLock);
    if (!mAreaStatusFinished) {
        mAreaStatusFinished = true;
    }

    CArea* const area = CAreaDB::inst().FindArea(areaName);
    if (area == nullptr) {
        const std::string hostAreaName = Utf16ToUtf8(areaName);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] undefined (or unregistered) Area[%s] - Register",
        //     L"MakerEvent.cpp",
        //     0x218,
        //     hostAreaName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] undefined (or unregistered) Area[%s] - Register",
        //     L"MakerEvent.cpp",
        //     0x218,
        //     hostAreaName.c_str())
        L2_LOG_ERROR(
            Log,
            "Cannot register area '%s' status for world %d: the area was not found; the status-ready flag is already set.",
            hostAreaName.c_str(), worldId
        );
        runtime::process::ReportCallStack(2, false);
        return;
    }

    mAreaStatus[std::u16string(areaName)] = worldId;
    if (mAreaStatusFinished) {
        g_npcserver.Send("cdSdd", 0xa8, -1, areaName, worldId, area->IsOn(worldId));
    }
}
