#include "L2NPC/src/Desire.h"

#include "L2Core/src/inc/Guard.h"
#include "L2NPC/src/Creature.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Log.h"
#include "Shared/src/SharedFactory.h"
#include "TextRuntime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>


namespace {

constexpr std::size_t kReportCapacity = 0x2800;

template <typename... Arguments>
std::string FormatReportText(const char* format, Arguments... arguments) {
    // UTF-8 can require four bytes for one target UTF-16 code unit, so this
    // staging array keeps the target character capacity across host
    // wchar_t implementations.
    std::array<char, kReportCapacity * 4 + 1> buffer{};
    const int length = std::snprintf(buffer.data(), buffer.size(), format, arguments...);
    if (length < 0) {
        return {};
    }
    return std::string(buffer.data(), std::min(static_cast<std::size_t>(length), buffer.size() - 1));
}

void StoreReportText(std::array<char16_t, kReportCapacity>& buffer, const std::string& text) {
    const std::u16string converted = Utf8ToUtf16(text);
    if (converted.size() >= buffer.size()) {
        // swprintf_s clears its destination when the formatted result does
        // not fit.
        buffer[0] = u'\0';
        return;
    }
    std::copy(converted.begin(), converted.end(), buffer.begin());
    buffer[converted.size()] = u'\0';
}

std::u16string_view ReportBufferView(const std::array<char16_t, kReportCapacity>& buffer) {
    std::size_t length = 0;
    while (length < buffer.size() && buffer[length] != u'\0') {
        ++length;
    }
    return {buffer.data(), length};
}

std::string ResolveCreatureName(int objectId, bool useVirtualName) {
    CCreature* const creature = CCreature::GetObjectW(static_cast<std::uint32_t>(objectId));
    if (!creature) {
        return "NULL";
    }
    if (useVirtualName) {
        return Utf16ToUtf8(std::u16string_view(creature->GetName()));
    }
    return Utf16ToUtf8(std::u16string_view(creature->m_data->Name()));
}

} // namespace

CDesire::CDesire(PhysicalState state) : type(state) {
    L2_GUARD_FUNCTION();

    creatureId = 0;
    staticObject = nullptr;
    targetId = 0;
    item = nullptr;
    contextValue = 0;
    value = 0.0F;
    boosted = false;
    boostCount = 0;
    attackMoveType = AMT_MOVE_TO_TARGET;
    deadlineTick = 0;
    waypoints = nullptr;
    waypointDelays = nullptr;
    waypointIndex = 0;
    boost = 0.0F;
    force = true;
    firedByServer = false;
    position = FVector{};
    name = nullptr;
    id = 0;
    uniqueId = 0;
    formationId = 0;
    formationIndex = 0;
    m_MoveSuperPointContext = {};
    flags = 0;
    range = 0;
    levelDifference = 0;
    absolute = false;
    followType = 0;
    followData1 = 0;
    followData2 = 0;
    targetCreatureId = 0;
}

CDesire::CDesire(const CDesire& other) {
    L2_GUARD_FUNCTION();

    *this = other;
}

CDesire& CDesire::operator=(const CDesire& other) {
    L2_GUARD_FUNCTION();

    if (this == &other) {
        return *this;
    }

    type = other.type;
    creatureId = other.creatureId;
    staticObject = other.staticObject;
    item = other.item;
    targetId = other.targetId;
    levelDifference = other.levelDifference;
    name = other.name;
    m_MoveSuperPointContext = other.m_MoveSuperPointContext;
    id = other.id;
    uniqueId = other.uniqueId;
    formationId = other.formationId;
    formationIndex = other.formationIndex;
    contextValue = other.contextValue;
    deadlineTick = other.deadlineTick;
    attackMoveType = other.attackMoveType;
    waypoints = other.waypoints;
    waypointDelays = other.waypointDelays;
    waypointIndex = other.waypointIndex;
    boost = other.boost;
    boosted = other.boosted;
    // Current L2NPC.exe 0x004225fc resets this field instead of copying it.
    boostCount = 0;
    force = other.force;
    firedByServer = other.firedByServer;
    position = other.position;
    value = other.value;
    flags = other.flags;
    range = other.range;
    absolute = other.absolute;
    followType = other.followType;
    followData1 = other.followData1;
    followData2 = other.followData2;
    targetCreatureId = other.targetCreatureId;
    return *this;
}

bool CDesire::operator==(const CDesire& other) const {
    L2_GUARD_FUNCTION();

    if (type != other.type) {
        return false;
    }

    switch (type) {
        case DESIRE_NONE:
        case DESIRE_MOVE_AROUND:
        case DESIRE_DO_NOTHING:
        case DESIRE_DECAYING:
        case DESIRE_PET_DEFAULT:
            return true;

        case DESIRE_ATTACK:
        case DESIRE_MOVE_TO_TARGET:
            return targetId == other.targetId;

        case DESIRE_CHASE:
        case DESIRE_FLEE:
        case DESIRE_FOLLOW:
            return creatureId == other.creatureId && targetCreatureId == other.targetCreatureId;

        case DESIRE_GET_ITEM:
            return item == other.item;

        case DESIRE_MOVE_WAYPOINT:
            return waypoints == other.waypoints && waypointDelays == other.waypointDelays &&
                waypointIndex == other.waypointIndex;

        case DESIRE_USE_SKILL:
            return targetId == other.targetId && contextValue == other.contextValue;

        case DESIRE_MOVE_TO: {
            const double deltaX = position.x - other.position.x;
            const double deltaY = position.y - other.position.y;
            const double deltaZ = position.z - other.position.z;
            return deltaX * deltaX + deltaY * deltaY < 400.0 && std::fabs(deltaZ) < 30.0;
        }

        case DESIRE_EFFECT_ACTION:
            return creatureId == other.creatureId && contextValue == other.contextValue;

        case DESIRE_EFFECT_ACTION2:
            return creatureId == other.creatureId && contextValue == other.contextValue && range == other.range;

        case DESIRE_MOVE_SUPER_PT:
            return std::u16string_view(name) == std::u16string_view(other.name) && contextValue == other.contextValue;

        case DESIRE_MOVE_FREEWAY:
            return id == other.id;

        case DESIRE_MOVE_FORMATION:
            return uniqueId == other.uniqueId;

        default:
            return false;
    }
}

bool CDesire::operator!=(const CDesire& other) const {
    L2_GUARD_FUNCTION();

    return !(*this == other);
}

bool CDesire::operator<(const CDesire& other) const {
    L2_GUARD_FUNCTION();

    return value < other.value;
}

void CDesire::Report(std::u16string& reports) const {
    L2_GUARD_FUNCTION();

    // The adjacent older PDB describes wchar_t[8192], but the current image
    // passes 0x2800 to swprintf_s and reserves exactly 0x5000 bytes before
    // the next global object. Unknown states reuse the previous
    // contents because the target still logs/appends after its switch.
    static std::array<char16_t, kReportCapacity> buf{};
    static std::mutex reportBufferMutex;

    std::string report;
    bool formatted = true;
    const double priority = static_cast<double>(value);

    switch (type) {
        case DESIRE_NONE:
            report = FormatReportText("[%.2f] PS_IDLE", priority);
            break;

        case DESIRE_MOVE_AROUND:
            report =
                FormatReportText("[%.2f] PS_MOVE_AROUND [%d] [%lld]", priority, range, static_cast<long long>(contextValue));
            break;

        case DESIRE_DO_NOTHING:
            report = FormatReportText("[%.2f] PS_DO_NOTHING", priority);
            break;

        case DESIRE_ATTACK: {
            const std::string targetName = ResolveCreatureName(targetId, true);
            report = FormatReportText("[%.2f] PS_ATTACK (-> %s[%d])", priority, targetName.c_str(), targetId);
            break;
        }

        case DESIRE_CHASE:
            report = FormatReportText("[%.2f] PS_CHASE", priority);
            break;

        case DESIRE_FLEE:
            report = FormatReportText("[%.2f] PS_FLEE", priority);
            break;

        case DESIRE_GET_ITEM:
            report = FormatReportText("[%.2f] PS_GET_ITEM", priority);
            break;

        case DESIRE_FOLLOW: {
            const std::string sourceName = ResolveCreatureName(creatureId, false);
            const std::string targetName = ResolveCreatureName(targetCreatureId, false);
            report = FormatReportText(
                "[%.2f] PS_FOLLOW (%s[%d] -> %s[%d])", priority, sourceName.c_str(), creatureId, targetName.c_str(),
                targetCreatureId
            );
            break;
        }

        case DESIRE_DECAYING:
            report = FormatReportText("[%.2f] PS_DECAYING", priority);
            break;

        case DESIRE_MOVE_WAYPOINT:
            report = FormatReportText("[%.2f] PS_MOVE_TO_WAY_POINT (index[%d])", priority, waypointIndex);
            break;

        case DESIRE_USE_SKILL: {
            const std::string targetName = ResolveCreatureName(targetId, true);
            const auto packed = static_cast<std::uint64_t>(contextValue);
            report = FormatReportText(
                "[%.2f] PS_USE_SKILL -> [%s(%d)], "
                "skill id(%d) level(%d) "
                "Boost(%d, %.2f), nBoost(%d), desire(%d)",
                priority, targetName.c_str(), targetId, static_cast<int>(packed >> 32),
                static_cast<int>((packed >> 16) & 0xffffU), boosted ? 1 : 0, static_cast<double>(boost), boostCount,
                static_cast<int>(flags)
            );
            break;
        }

        case DESIRE_MOVE_TO:
            report = FormatReportText(
                "[%.2f] PS_MOVE_TO (%d, %d, %d)", priority, runtime::Win64CvttDoubleToInt32(position.x),
                runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z)
            );
            break;

        case DESIRE_EFFECT_ACTION:
            report = FormatReportText("[%.2f] PS_EFFECT_ACTION (%lld)", priority, static_cast<long long>(contextValue));
            break;

        case DESIRE_EFFECT_ACTION2:
            report = FormatReportText(
                "[%.2f] PS_EFFECT_ACTION2 (%lld)(%d)", priority, static_cast<long long>(contextValue), range
            );
            break;

        case DESIRE_PET_DEFAULT:
            report = FormatReportText("[%.2f] PS_PET_DEFAULT", priority);
            break;

        case DESIRE_MOVE_TO_TARGET: {
            const std::string targetName = ResolveCreatureName(targetId, false);
            report = FormatReportText(
                "[%.2f] PS_MOVE_TO_TARGET. "
                "(->%s) (%d, %lld, %d)",
                priority, targetName.c_str(), targetId, static_cast<long long>(contextValue), static_cast<int>(flags)
            );
            break;
        }

        case DESIRE_MOVE_SUPER_PT: {
            const std::string pointName = Utf16ToUtf8(std::u16string_view(name));
            const char* const mode = contextValue == 0 ? "Follow" : contextValue == 1 ? "Random" : "invalid";
            report = FormatReportText("[%.2f] PS_MOVE_SUPERPOINT (%s, %s)", priority, pointName.c_str(), mode);
            break;
        }

        case DESIRE_MOVE_FREEWAY:
            report = FormatReportText("[%.2f] PS_MOVE_FREEWAY (%d)", priority, id);
            break;

        case DESIRE_MOVE_FORMATION:
            report = FormatReportText("[%.2f] PS_MOVE_FORMATION (%d)", priority, uniqueId);
            break;

        default:
            formatted = false;
            break;
    }

    // if (formatted) {
    //     StoreReportText(buf, report)
    // }
    // const std::u16string_view visible =
    //     ReportBufferView(buf)
    // Different NPC queues have different locks. Keep the shared previous
    // report for unknown states, but copy it under one buffer lock so the
    // console and builder append use the same stable text. Resolve objects
    // before this lock. Logging and appending to caller output happen after it.
    std::u16string snapshot;
    {
        std::lock_guard<std::mutex> lock(reportBufferMutex);
        if (formatted) {
            StoreReportText(buf, report);
        }
        snapshot.assign(ReportBufferView(buf));
    }
    const std::u16string_view visible(snapshot);
    // Noise: The host-wide copy served only the old console format. The appended builder report remains unchanged.
    // const std::wstring hostReport =
    //     Utf16ToWide(visible)
    // Log.Add(
    //     LOG_NORMAL,
    //     hostReport.c_str())
    // Log.Add(LOG_NORMAL,
    //     "[NPC actions] %s",
    //     Utf16ToUtf8(visible).c_str())
    L2_LOG_INFO(Log, "[NPC actions] %s", Utf16ToUtf8(visible).c_str());
    reports.append(visible.begin(), visible.end());
    reports += u"\r\n";
}
