#include "Shared/src/Log.h"
#include "Shared/src/TemplateHelper.h"

#include "PledgeMasteryInCache.h"

#include "Cached.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/DBConn.h"
#include "Portable/ProcessRuntime.h"
#include "Runtime.h"

#include <algorithm>
#include <iterator>

namespace {

// void ReportNotImplemented(int line) {
//     // These odd diagnostics are present in the shipping error
//     // branches and are kept unchanged.
//     Log.Add(LOG_ERROR, "[PLEDGE][PledgeMasteryInCache.cpp][%d]NOT IMPLEMENTED!!!!!", line)
// }

} // namespace

PledgeMasteryInCache::PledgeMasteryInCache(std::int32_t pledgeId) : m_pledgeId(pledgeId), m_points{} {
    // Current constructor 0x00520284 zeroes exactly 0x44 bytes at +0x3c.
}

PledgeMasteryInCache::~PledgeMasteryInCache() = default;

bool PledgeMasteryInCache::IsValidMasteryId(PledgeMasteryID::Type masteryId) noexcept {
    const auto value = static_cast<std::uint32_t>(masteryId);
    return value - 1U < 16U;
}

std::int32_t PledgeMasteryInCache::GetMasteryPoint(PledgeMasteryID::Type masteryId) const {
    // Original inline identifier is unavailable. Current helper 0x005202c4
    // takes the write side of this lock and reads +0x3c + masteryId * 4.
    std::int32_t point;
    {
        L2_WRITE_GUARD(m_lock);
        point = m_points[static_cast<std::int32_t>(masteryId)];
    }
    return point;
}

void PledgeMasteryInCache::GetPoints(std::int32_t (&points)[PledgeMasteryID::MAX_COUNT]) const {
    L2_GUARD_FUNCTION();

    // Current 0x00520300 uses the write side of RWLock even for this const
    // copy. Preserve that synchronization behavior.
    {
        L2_WRITE_GUARD(m_lock);
        std::copy(std::begin(m_points), std::end(m_points), std::begin(points));
    }
}

bool PledgeMasteryInCache::LoadFromDB() {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    std::int32_t unusedPledgeId = 0;
    std::int32_t masteryIdValue = 0;
    std::int32_t point = 0;
    connection.Bind(&unusedPledgeId);
    connection.Bind(&masteryIdValue);
    connection.Bind(&point);

    constexpr const wchar_t* statement = L"{Call lin_PledgeMasteryLoad (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_pledgeId);
    if (!connection.ExecutePrepared(statement)) {
        return false;
    }

    while (connection.Fetch()) {
        const auto masteryId = static_cast<PledgeMasteryID::Type>(masteryIdValue);
        if (!IsValidMasteryId(masteryId)) {
            continue;
        }
        // Current 0x005203cc acquires/releases for each accepted result row.
        {
            L2_WRITE_GUARD(m_lock);
            m_points[masteryIdValue] = point;
        }
    }
    return true;
}

bool PledgeMasteryInCache::SetMasteryPoint(PledgeMasteryID::Type masteryId, std::int32_t point) {
    L2_GUARD_FUNCTION();

    if (!IsValidMasteryId(masteryId)) {
        // ReportNotImplemented(0x39)
        // Log.Add(LOG_WARN,
        //     "[clan mastery] Cannot set mastery points for clan %d: mastery ID %d is outside 1..16. No update submitted.",
        //     m_pledgeId, static_cast<int>(masteryId))
        L2_LOG_WARN(Log,
            "[clan mastery] Cannot set mastery points for clan %d: mastery ID %d is outside 1..16; no update submitted.",
            m_pledgeId, static_cast<int>(masteryId));
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_PledgeMasterySave (?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_pledgeId);
    connection.BindParameter(&masteryId);
    connection.BindParameter(&point);
    if (!connection.ExecutePrepared(statement)) {
        // ReportNotImplemented(0x41)
        // Log.Add(LOG_ERROR,
        //     "[clan mastery] Cannot save %d points for mastery %d of clan %d in database %d. Cached points unchanged.",
        //     point, static_cast<int>(masteryId), m_pledgeId, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[clan mastery] Cannot save %d points for mastery %d of clan %d in database %d; cached points unchanged.",
            point, static_cast<int>(masteryId), m_pledgeId, runtime::process::CurrentDatabaseId());
        return false;
    }

    {
        L2_WRITE_GUARD(m_lock);
        m_points[static_cast<std::int32_t>(masteryId)] = point;
    }
    return true;
}

bool PledgeMasteryInCache::ResetMasteryPoint() {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_PledgeMasteryReset (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_pledgeId);
    if (!connection.ExecutePrepared(statement)) {
        // ReportNotImplemented(0x6d)
        // Log.Add(LOG_ERROR,
        //     "[clan mastery] Cannot reset mastery points for clan %d in database %d. Cached points unchanged.",
        //     m_pledgeId, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[clan mastery] Cannot reset mastery points for clan %d in database %d; cached points unchanged.",
            m_pledgeId, runtime::process::CurrentDatabaseId());
        return false;
    }

    {
        L2_WRITE_GUARD(m_lock);
        std::fill(std::begin(m_points), std::end(m_points), 0);
    }
    return true;
}
