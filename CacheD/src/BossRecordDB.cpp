#include "BossRecordDB.h"

#include "CacheSocket.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/DBConn.h"
#include "Portable/ProcessRuntime.h"

#include <cstdint>
#include <ctime>

BossRecordDB& GetBossRecordDB() {
    static BossRecordDB database;
    return database;
}

int BossRecordDB::GetRound() {
    // Current Cached.exe 0x004517c8.
    L2_GUARD_FUNCTION();
    return m_round;
}

int BossRecordDB::GetStartTime() {
    // Current Cached.exe 0x00451854.
    L2_GUARD_FUNCTION();
    return m_startTime;
}

int BossRecordDB::GetEndTime() {
    // Current Cached.exe 0x004518e0.
    L2_GUARD_FUNCTION();
    return m_endTime;
}

void BossRecordDB::MoveToNextRound(CSocket* socket) {
    // Current Cached.exe 0x0045196c. The target adds whole 86400-second
    // intervals before converting back to local calendar time, then replaces
    // hour/minute/second with the previous round schedule.
    L2_GUARD_FUNCTION();

    const int round = GetRound();
    const int now = runtime::crt::Time32Now();

    int scheduledWeekday = 3;
    int scheduledHour = 1;
    int scheduledMinute = 0;
    std::tm scheduled{};
    if (GetEndTime() != 0 &&
        runtime::crt::LocalTime32(
            GetEndTime(),
            scheduled)) {
        scheduledWeekday = scheduled.tm_wday;
        scheduledHour = scheduled.tm_hour;
        scheduledMinute = scheduled.tm_min;
    }

    // Shipping code samples _time32 for the persisted start value above,
    // then samples system FILETIME for calendar scheduling.
    // A second Time32Now keeps the observable one-second boundary.
    const int schedulingNow = runtime::crt::Time32Now();
    std::tm current{};
    int nextEndTime = -1;
    if (runtime::crt::LocalTime32(
            schedulingNow,
            current)) {
        int days = (scheduledWeekday - current.tm_wday + 7) % 7;
        if (days == 0)
            days = 7;

        const int shifted = runtime::AddInt32Wrapping(schedulingNow, days * 24 * 60 * 60);
        std::tm next{};
        if (runtime::crt::LocalTime32(
                shifted,
                next)) {
            next.tm_hour = scheduledHour;
            next.tm_min = scheduledMinute;
            next.tm_sec = 0;
            next.tm_isdst = -1;
            // Current CacheD helper 0x0059abe0 performs the same validated
            // DateTime-to-TimeValue/_mktime32 conversion and normalization.
            nextEndTime = runtime::crt::Mktime32(&next);
        }
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_MoveToNextBossRecordRound (?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&round);
    // Current Win64 `long` binding is SQL_DOUBLE even though it reads the
    // four-byte time32 payload. DBConn keeps that target specialization.
    const long databaseStartTime = static_cast<long>(now);
    connection.BindParameter(&databaseStartTime);
    connection.BindParameter(&nextEndTime);
    if (!connection.ExecutePrepared(statement)) {
        socket->Send("chddd", 0xc6, 0x27, 0, 0, 0);
        return;
    }

    m_round = runtime::AddInt32Wrapping(round, 1);
    m_startTime = now;
    m_endTime = nextEndTime;
    socket->Send("chddd", 0xc6, 0x27, m_round, m_startTime, m_endTime);
}
