#include "QsCheck.h"

#include "Shared/src/Runtime/DatabaseLog.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "ProcessRuntime.h"
#include "Shared/src/L2Time.h"
#include "L2Core/src/inc/Guard.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"
#include "Shared/src/platform/Runtime.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <string>
#include <string_view>

namespace {

// Keep the supplied SQL text, including literals.
void LogSqlStatement(LogType type, int sample, const char* context, std::string_view statement) {
    runtime::logging::ForEachSqlFragment(
        statement, [=](std::uint64_t sequence, std::size_t part, std::size_t total, std::string_view fragment) {
            if (total == 1) {
                // Log.Add(type, "[query %d] %s. SQL: \"%.*s\"",
                //     sample, context, static_cast<int>(fragment.size()), fragment.empty() ? "" : fragment.data())
                L2_LOG(
                    Log, type, "[query %d] %s. SQL: \"%.*s\"", sample, context, static_cast<int>(fragment.size()),
                    fragment.empty() ? "" : fragment.data()
                );
            } else {
                // Log.Add(type, "[query %d] %s. SQL %llu, part %zu/%zu: \"%.*s\"",
                //     sample, context, static_cast<unsigned long long>(sequence), part, total,
                //     static_cast<int>(fragment.size()), fragment.empty() ? "" : fragment.data())
                L2_LOG(
                    Log, type, "[query %d] %s. SQL %llu, part %zu/%zu: \"%.*s\"", sample, context,
                    static_cast<unsigned long long>(sequence), part, total, static_cast<int>(fragment.size()),
                    fragment.empty() ? "" : fragment.data()
                );
            }
        }
    );
}


std::u16string TruncateTargetStatement(const wchar_t* statement, std::size_t maximumUnits) {
    std::u16string result = WideToUtf16(statement);
    if (result.size() > maximumUnits) {
        result.resize(maximumUnits);
    }
    return result;
}

template <std::size_t Capacity>
std::u16string_view FixedStatement(const std::array<char16_t, Capacity>& statement) {
    const auto terminator = std::find(statement.begin(), statement.end(), u'\0');
    return {statement.data(), static_cast<std::size_t>(terminator - statement.begin())};
}

} // namespace

bool CQsCheck::s_packetReport = false;
CQsCheck g_qsCheck;
CQsCheckManager g_qsCheckManager;

CQsCheck::SlowQueryRecord::SlowQueryRecord(std::uint32_t durationMilliseconds, const wchar_t* sourceStatement) :
    creationTime(runtime::crt::Time32Now()),
    totalMilliseconds(static_cast<std::int64_t>(static_cast<std::int32_t>(durationMilliseconds))) {
    const std::u16string targetStatement = TruncateTargetStatement(sourceStatement, statement.size() - 1);
    std::copy(targetStatement.begin(), targetStatement.end(), statement.begin());
}

CQsCheck::CQsCheck() { L2_GUARD_FUNCTION(); }

CQsCheck::~CQsCheck() { L2_GUARD_FUNCTION(); }

void CQsCheck::AddPacketReport(std::uint32_t packetType, std::uint32_t packetCallBackTime, std::uint32_t packetProcessTime) {
    L2_GUARD_FUNCTION();
    if (!s_packetReport) {
        return;
    }

    L2_GUARD(m_packetLock);
    if (packetType < PacketCapacity) {
        ++m_packetCounters[packetType];
        m_packetCallBackTimes[packetType] += packetCallBackTime;
        m_packetProcessTimes[packetType] += packetProcessTime;
    } else {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"packet number is invalise [%d]",
        //     static_cast<int>(packetType))
        // Log.Add(LOG_ERROR, "component=packet_stats event=invalid_packet_id packet_id=%u capacity=%zu",
        //     packetType, static_cast<std::size_t>(PacketCapacity))
        // Log.Add(LOG_ERROR, "[packet stats] Cannot record packet ID %u: outside the %zu-entry counter table.",
        //     packetType, static_cast<std::size_t>(PacketCapacity))
        L2_LOG_ERROR(
            Log, "[packet stats] Cannot record packet ID %u: outside the %zu-entry counter table.", packetType,
            static_cast<std::size_t>(PacketCapacity)
        );
    }
}

void CQsCheck::ShowPacketReport() {
    L2_GUARD_FUNCTION();
    if (!s_packetReport) {
        return;
    }

    // Log.Add(
    //     LOG_NORMAL,
    //     L"=(begin)packet================================================")
    // Log.Add(LOG_NORMAL, "component=packet_stats event=report_started")
    // Log.Add(LOG_NORMAL, "[packet stats] Beginning packet timing report.")
    L2_LOG_INFO(Log, "[packet stats] Beginning packet timing report.");
    {
        L2_GUARD(m_packetLock);
        for (std::size_t index = 0; index < PacketCapacity; ++index) {
            const std::uint32_t count = m_packetCounters[index];
            if (count == 0) {
                continue;
            }
            const std::uint32_t callback = m_packetCallBackTimes[index];
            const std::uint32_t process = m_packetProcessTimes[index];
            // Log.AddT(
            //     LOG_NORMAL,
            //     L"packet[%d],count[%d],callback sum[%d], process sum[%d], "
            //     L"time/callback[%d], time/process[%d]",
            //     static_cast<int>(index),
            //     static_cast<int>(count),
            //     static_cast<int>(callback),
            //     static_cast<int>(process),
            //     static_cast<int>(callback / count),
            //     static_cast<int>(process / count))
            // Log.Add(LOG_NORMAL, "component=packet_stats event=packet_summary packet_id=%zu count=%u callback_total_ms=%u
            // process_total_ms=%u callback_mean_ms=%u process_mean_ms=%u",
            //     index, count, callback, process, callback / count, process / count)
            // Log.Add(LOG_NORMAL, "[packet stats] Packet %zu: %u calls, callbacks %u ms total, processing %u ms total
            // averages %u ms and %u ms respectively.",
            //     index, count, callback, process, callback / count, process / count)
            L2_LOG_INFO(
                Log,
                "[packet stats] Packet %zu: %u calls; callbacks %u ms total, processing %u ms total; averages %u ms and %u "
                "ms respectively.",
                index, count, callback, process, callback / count, process / count
            );
        }
    }
    // Log.Add(
    //     LOG_NORMAL,
    //     L"=(end)packet================================================")
    // Log.Add(LOG_NORMAL, "component=packet_stats event=report_completed")
    // Log.Add(LOG_NORMAL, "[packet stats] End of packet timing report.")
    L2_LOG_INFO(Log, "[packet stats] End of packet timing report.");
}

void CQsCheck::ClearPacketReport() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_packetLock);
        m_packetCounters.fill(0);
        m_packetCallBackTimes.fill(0);
        m_packetProcessTimes.fill(0);
    }
    // Log.Add(
    //     LOG_NORMAL,
    //     L"packet report is cleared")
    // Log.Add(LOG_NORMAL, "component=packet_stats event=counters_cleared")
    // Log.Add(LOG_NORMAL, "[packet stats] Counters cleared.")
    L2_LOG_INFO(Log, "[packet stats] Counters cleared.");
}

void CQsCheck::SlowReport(LogType logType) {
    L2_GUARD_FUNCTION();

    L2_GUARD(m_slowReportLock);
    // Log.Add(
    //     logType,
    //     L"[SlowQs] (begin)")
    // Log.Add(logType, "component=database_stats event=slow_report_started entries=%zu", m_slowQsReport.size())
    // Log.Add(logType, "[queries] Slow-query report: %zu entries.", m_slowQsReport.size())
    L2_LOG(Log, logType, "[queries] Slow-query report: %zu entries.", m_slowQsReport.size());
    int rank = 1;
    for (const SlowQueryRecord* record : m_slowQsReport) {
        const std::u16string_view targetStatement = FixedStatement(record->statement);
        // const std::wstring statement =
        //     Utf16ToWide(targetStatement.substr(
        //         0,
        //         std::min<std::size_t>(
        //             targetStatement.size(),
        //             511)))
        // const std::tm creation =
        //     runtime::crt::LocalTime32OrZero(
        //         record->creationTime)

        // Sensitive data: stored SQL text may contain credentials or character/account values.
        // Log.AddT(
        //     logType,
        //     L"[SlowQs] %d,[%d/%d/%d %d:%d:%d],Total %d(ms),[%s]",
        //     rank,
        //     creation.tm_year + 1900,
        //     creation.tm_mon,
        //     creation.tm_mday,
        //     creation.tm_hour,
        //     creation.tm_min,
        //     creation.tm_sec,
        //     static_cast<std::int32_t>(
        //         record->totalMilliseconds),
        //     statement.c_str())
        // Log.Add(logType, "component=database_stats event=slow_query_entry entry=%d created_unix_seconds=%d elapsed_ms=%lld
        // statement_kind=%s sql=redacted",
        //     rank, record->creationTime, static_cast<long long>(record->totalMilliseconds),
        //     runtime::logging::WideSqlStatementKind(statement.c_str()))
        // Log.Add(logType, "[queries] Slow query %d, recorded at Unix time %d: %lld ms.",
        //     rank, record->creationTime, static_cast<long long>(record->totalMilliseconds))
        L2_LOG(
            Log, logType, "[queries] Slow query %d, recorded at Unix time %d: %lld ms.", rank, record->creationTime,
            static_cast<long long>(record->totalMilliseconds)
        );
        LogSqlStatement(logType, rank, "Stored slow-query snapshot", Utf16ToUtf8(targetStatement));
        ++rank;
    }
    // Log.Add(
    //     logType,
    //     L"[SlowQs] (end)")
    // Log.Add(logType, "component=database_stats event=slow_report_completed entries=%zu", m_slowQsReport.size())
    // Log.Add(logType, "[queries] End of slow-query report (%zu entries).", m_slowQsReport.size())
    L2_LOG(Log, logType, "[queries] End of slow-query report (%zu entries).", m_slowQsReport.size());
}

void CQsCheck::Report(LogType logType) {
    L2_GUARD_FUNCTION();

    // const std::tm firstRecord =
    //     runtime::crt::LocalTime32OrZero(
    //         m_firstRecordTime)

    L2_GUARD(m_reportLock);
    // Log.AddT(
    //     logType,
    //     L"[QsCheck](begin)[%d/%d/%d %d:%d:%d]",
    //     firstRecord.tm_year + 1900,
    //     firstRecord.tm_mon,
    //     firstRecord.tm_mday,
    //     firstRecord.tm_hour,
    //     firstRecord.tm_min,
    //     firstRecord.tm_sec)
    // Log.Add(logType, "component=database_stats event=query_report_started first_record_unix_seconds=%d entries=%zu",
    //     m_firstRecordTime, m_qsReport.size())
    // Log.Add(logType, "[queries] Beginning query report from Unix time %d: %zu entries.",
    //     m_firstRecordTime, m_qsReport.size())
    L2_LOG(
        Log, logType, "[queries] Beginning query report from Unix time %d: %zu entries.", m_firstRecordTime,
        m_qsReport.size()
    );
    // Noise: field names accompany each structured row. A separate CSV heading is extra.
    // Log.Add(
    //     logType,
    //     L"[QsCheck] no,cnt,total_msec,msec_per_cnt,statement")


    int index = 0;
    for (const auto& entry : m_qsReport) {
        const QueryAggregate* aggregate = entry.second;
        const std::u16string_view targetStatement = runtime::StringView(entry.first);
        // const std::wstring statement = Utf16ToWide(
        //     targetStatement.substr(
        //         0,
        //         std::min<std::size_t>(
        //             targetStatement.size(),
        //             99)))
        // Sensitive data: keep query timing/counts without printing SQL literals.
        // Log.AddT(
        //     logType,
        //     L"[QsCheck] %d,%I64d,%I64d,%.1f,\"%s\"",
        //     index,
        //     aggregate->count,
        //     aggregate->totalMilliseconds,
        //     static_cast<double>(
        //         aggregate->totalMilliseconds) /
        //         static_cast<double>(aggregate->count),
        //     statement.c_str())
        // Log.Add(logType, "component=database_stats event=query_summary entry=%d count=%lld total_ms=%lld mean_ms=%.1f
        // statement_kind=%s sql=redacted",
        //     index, static_cast<long long>(aggregate->count), static_cast<long long>(aggregate->totalMilliseconds),
        //     static_cast<double>(aggregate->totalMilliseconds) / static_cast<double>(aggregate->count),
        //     runtime::logging::WideSqlStatementKind(statement.c_str()))
        // Log.Add(logType, "[queries] Entry %d: %lld executions, %lld ms total, %.1f ms on average.",
        //     index, static_cast<long long>(aggregate->count), static_cast<long long>(aggregate->totalMilliseconds),
        //     static_cast<double>(aggregate->totalMilliseconds) / static_cast<double>(aggregate->count))
        L2_LOG(
            Log, logType, "[queries] Entry %d: %lld executions, %lld ms total, %.1f ms on average.", index,
            static_cast<long long>(aggregate->count), static_cast<long long>(aggregate->totalMilliseconds),
            static_cast<double>(aggregate->totalMilliseconds) / static_cast<double>(aggregate->count)
        );
        LogSqlStatement(logType, index, "Stored query snapshot", Utf16ToUtf8(targetStatement));
        ++index;
    }

    const std::int32_t now = runtime::crt::Time32Now();
    // const std::tm reportTime =
    //     runtime::crt::LocalTime32OrZero(now)

    // Log.AddT(
    //     logType,
    //     L"[QsCheck](end)[%d/%d/%d %d:%d:%d][Report Elapsed Time %d]",
    //     reportTime.tm_year + 1900,
    //     reportTime.tm_mon,
    //     reportTime.tm_mday,
    //     reportTime.tm_hour,
    //     reportTime.tm_min,
    //     reportTime.tm_sec,
    //     runtime::SubtractInt32Wrapping(now, m_firstRecordTime))
    // Log.Add(logType, "component=database_stats event=query_report_completed entries=%zu elapsed_seconds=%d",
    //     m_qsReport.size(), runtime::SubtractInt32Wrapping(now, m_firstRecordTime))
    // Log.Add(logType, "[queries] End of query report: %zu entries, %d seconds since the first record.",
    //     m_qsReport.size(), runtime::SubtractInt32Wrapping(now, m_firstRecordTime))
    L2_LOG(
        Log, logType, "[queries] End of query report: %zu entries, %d seconds since the first record.", m_qsReport.size(),
        runtime::SubtractInt32Wrapping(now, m_firstRecordTime)
    );
}

std::uint32_t CQsCheck::AddSlowQs(const wchar_t* sourceStatement, std::uint32_t durationMilliseconds) {
    L2_GUARD_FUNCTION();

    const std::u16string targetStatement = TruncateTargetStatement(sourceStatement, 511);
    // const std::wstring statement =
    //     Utf16ToWide(targetStatement)
    if (!runtime::process::IsQsDataEnabled()) {
        // Sensitive data: SQL values are omitted. Slow execution is a warning.
        // Log.AddT(
        //     LOG_NORMAL,
        //     L"%d,%d,[%s]",
        //     m_slowRecordCounter,
        //     durationMilliseconds,
        //     statement.c_str())
        // Log.Add(LOG_WARN, "component=database_stats event=slow_query sample=%d elapsed_ms=%u statement_kind=%s
        // sql=redacted",
        //     m_slowRecordCounter, durationMilliseconds, runtime::logging::WideSqlStatementKind(statement.c_str()))
        // Log.Add(LOG_WARN, "[queries] Slow query sample %d took %u ms.", m_slowRecordCounter, durationMilliseconds)
        L2_LOG_WARN(Log, "[queries] Slow query sample %d took %u ms.", m_slowRecordCounter, durationMilliseconds);
        LogSqlStatement(LOG_WARN, m_slowRecordCounter, "Slow query", WideToUtf8(sourceStatement));
        ++m_slowRecordCounter;
        return 0;
    }

    bool belowRecordLimit = false;
    {
        L2_GUARD(m_slowReportLock);
        ++m_slowRecordCounter;
        // Sensitive data: SQL values are omitted. Slow execution is a warning.
        // Log.AddT(
        //     LOG_NORMAL,
        //     L"%d,%d,[%s]",
        //     m_slowRecordCounter,
        //     durationMilliseconds,
        //     statement.c_str())
        // Log.Add(LOG_WARN, "component=database_stats event=slow_query sample=%d elapsed_ms=%u statement_kind=%s
        // sql=redacted",
        //     m_slowRecordCounter, durationMilliseconds, runtime::logging::WideSqlStatementKind(statement.c_str()))
        // Log.Add(LOG_WARN, "[queries] Slow query sample %d took %u ms.", m_slowRecordCounter, durationMilliseconds)
        L2_LOG_WARN(Log, "[queries] Slow query sample %d took %u ms.", m_slowRecordCounter, durationMilliseconds);
        LogSqlStatement(LOG_WARN, m_slowRecordCounter, "Slow query", WideToUtf8(sourceStatement));
        belowRecordLimit = m_slowRecordCounter < 1000;
        if (belowRecordLimit) {
            m_slowQsReport.push_back(new SlowQueryRecord(durationMilliseconds, sourceStatement));
        }
    }
    // Noise: replaying all previous slow queries after every tenth sample duplicates up to 999 rows. Scheduled/manual
    // reports remain available. if (belowRecordLimit &&
    //     m_slowRecordCounter % 10 == 0) {
    //     SlowReport(LOG_NORMAL)
    // }

    return 0;
}

void CQsCheck::ResetData() {
    L2_GUARD_FUNCTION();

    const bool saveQsData = runtime::process::ExchangeQsDataEnabled(false);
    {
        L2_GUARD(m_reportLock);
        for (auto& entry : m_qsReport) {
            delete entry.second;
        }
        m_qsReport.clear();
    }

    {
        L2_GUARD(m_slowReportLock);
        for (SlowQueryRecord* record : m_slowQsReport) {
            delete record;
        }
        m_slowQsReport.clear();
    }
    runtime::process::ExchangeQsDataEnabled(saveQsData);
}

std::int64_t CQsCheck::AddValue(const wchar_t* sourceStatement, std::uint32_t durationMilliseconds) {
    L2_GUARD_FUNCTION();

    if (!runtime::process::IsQsDataEnabled()) {
        return 0;
    }
    if (m_recordCounter == 0) {
        m_firstRecordTime = runtime::crt::Time32Now();
    }

    std::int64_t totalMilliseconds = 0;
    {
        L2_GUARD(m_reportLock);
        const std::u16string targetStatement = WideToUtf16(sourceStatement);
        const std::u16string lookup{std::u16string_view(targetStatement)};
        auto found = m_qsReport.find(lookup);
        QueryAggregate* aggregate = nullptr;
        if (found == m_qsReport.end()) {
            aggregate = new QueryAggregate;
            ++aggregate->count;
            aggregate->totalMilliseconds += durationMilliseconds;
            m_qsReport.emplace(std::u16string{std::u16string_view(targetStatement)}, aggregate);
        } else {
            aggregate = found->second;
            ++aggregate->count;
            aggregate->totalMilliseconds += durationMilliseconds;
        }
        totalMilliseconds = aggregate->totalMilliseconds;
    }
    ++m_recordCounter;
    return totalMilliseconds;
}

void CQsCheckManager::SetReportMinute(int reportMinute) {
    m_reportMinute = reportMinute % 60;
    m_nextReportMinute = L2Util::Time::GetElapsedHour() * 60 + m_reportMinute;
}

bool CQsCheckManager::StartTimer() {
    L2_GUARD_FUNCTION();
    AddTimer(30000U, 0);
    return true;
}

bool CQsCheckManager::EnableTimer() {
    L2_GUARD_FUNCTION();
    if (m_enabled) {
        return false;
    }
    m_enabled = true;
    return true;
}

bool CQsCheckManager::DisableTimer() {
    L2_GUARD_FUNCTION();
    if (!m_enabled) {
        return false;
    }
    m_enabled = false;
    return true;
}

bool CQsCheckManager::IsReportMin(int reportMinute, int currentMinute) {
    L2_GUARD_FUNCTION();
    return reportMinute <= currentMinute;
}

void CQsCheckManager::TimerExpired(int id) {
    L2_GUARD_FUNCTION();

    if (m_enabled) {
        SharedUtilsLong now = runtime::crt::Time32Now();
        const int currentMinute = L2Util::Time::GetElapsedMin(&now);
        if (IsReportMin(m_nextReportMinute, currentMinute)) {
            m_nextReportMinute = (L2Util::Time::GetElapsedHour(&now) + 1) * 60 + m_reportMinute;
            // Scheduled query reports are routine statistics. Individual failures keep ERROR.
            // g_qsCheck.Report(LOG_ERROR)
            // g_qsCheck.SlowReport(LOG_ERROR)
            g_qsCheck.Report(LOG_NORMAL);
            g_qsCheck.SlowReport(LOG_NORMAL);
            g_qsCheck.ResetData();
            AddTimer(60000U, 2);
        }
    }

    if (id == 2) {
        if (runtime::process::IsServerToCacheDPacketStatMonitorEnabled()) {
            // CPacketStat<695, 234>::Inst().Report(
            //     LOG_ERROR)
            CPacketStat<695, 234>::Inst().Report(LOG_NORMAL);
            // CPacketStat<695, 234>::Inst().ReportRank(
            //     LOG_ERROR,
            //     CPacketStat<695, 234>::PRED_CNT)
            CPacketStat<695, 234>::Inst().ReportRank(LOG_NORMAL, CPacketStat<695, 234>::PRED_CNT);
            // CPacketStat<695, 234>::Inst().ReportRank(
            //     LOG_ERROR,
            //     CPacketStat<695, 234>::PRED_BYTES)
            CPacketStat<695, 234>::Inst().ReportRank(LOG_NORMAL, CPacketStat<695, 234>::PRED_BYTES);
        }
        if (runtime::process::IsCacheDToServerPacketStatMonitorEnabled()) {
            // CPacketStat<506, 198>::Inst().Report(
            //     LOG_ERROR)
            CPacketStat<506, 198>::Inst().Report(LOG_NORMAL);
            // CPacketStat<506, 198>::Inst().ReportRank(
            //     LOG_ERROR,
            //     CPacketStat<506, 198>::PRED_CNT)
            CPacketStat<506, 198>::Inst().ReportRank(LOG_NORMAL, CPacketStat<506, 198>::PRED_CNT);
            // CPacketStat<506, 198>::Inst().ReportRank(
            //     LOG_ERROR,
            //     CPacketStat<506, 198>::PRED_BYTES)
            CPacketStat<506, 198>::Inst().ReportRank(LOG_NORMAL, CPacketStat<506, 198>::PRED_BYTES);
        }
        CPacketStat<695, 234>::Inst().Reset();
        CPacketStat<506, 198>::Inst().Reset();
    }
    AddTimer(30000U, 0);
}

namespace packet_ex_detail {

CPacketExCacheDInstrumentation::CPacketExCacheDInstrumentation() noexcept :
    m_createTime(runtime::GetTickCount()), m_handleTime(0), m_deleteTime(0), m_packetType(0) {}

CPacketExCacheDInstrumentation::~CPacketExCacheDInstrumentation() {
    m_deleteTime = runtime::GetTickCount();
    g_qsCheck.AddPacketReport(m_packetType, m_handleTime - m_createTime, m_deleteTime - m_handleTime);
}

void CPacketExCacheDInstrumentation::SetPacketType(std::uint32_t packetType) noexcept { m_packetType = packetType; }

void CPacketExCacheDInstrumentation::BeginCallback(std::uint8_t* packet) noexcept {
    m_handleTime = runtime::GetTickCount();
    const std::uint32_t waitingTime = m_handleTime - m_createTime;
    if (m_packetType == 0x6a || m_packetType == 0x6b || m_packetType == 0x6c) {
        std::memcpy(packet, &waitingTime, sizeof(waitingTime));
    }
}

} // namespace packet_ex_detail
