#pragma once

#include "DatabaseLog.h"

#include <libpq-fe.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <initializer_list>
#include <string_view>

namespace runtime::logging {

// The receiver borrows PGresult metadata only while its callback runs.
// PostgreSQL's default receiver prints the full server message to stderr:
// https://www.postgresql.org/docs/18/libpq-notice-processing.html
// The visitor below logs the supplied message, details and SQL context.
struct PostgreSQLNoticeRecord {
    bool warning = true;
    bool error = false;
    bool debug = false;
    std::array<char, 512> text{};
};

// inline PostgreSQLNoticeRecord FormatPostgreSQLNotice(const PGresult* result) noexcept {
//     const int savedErrno = errno
//     const auto field = [result](int code) -> std::string_view {
//         const char* value = result == nullptr ? nullptr : PQresultErrorField(result, code)
//         return value == nullptr ? std::string_view{} : std::string_view(value)
//     }
//     const auto state = field(PG_DIAG_SQLSTATE)
//     const auto severity = field(PG_DIAG_SEVERITY_NONLOCALIZED)
//     const auto source = field(PG_DIAG_SOURCE_FUNCTION)
//     const char* severityName = "unknown"
//     for (const char* known : {"DEBUG", "LOG", "INFO", "NOTICE", "WARNING", "ERROR", "FATAL", "PANIC"}) {
//         if (severity == known) { severityName = known. Break. }
//     }
//     PostgreSQLNoticeRecord record.
//     record.warning = !(severity == "DEBUG" || severity == "LOG" || severity == "INFO" || severity == "NOTICE")
//     record.error = severity == "ERROR" || severity == "FATAL" || severity == "PANIC".
//     const char* reason = source == "exec_stmt_raise" ?
//         (record.error ? "database_raised_error" : record.warning ? "database_raised_warning" : "database_raised_notice") :
//         source == "reportDependentObjects" ? "dependent_objects_affected" : SqlStateReason(state)
//     unsigned long long position = 0
//     const auto rawPosition = field(PG_DIAG_STATEMENT_POSITION)
//     if (!rawPosition.empty()) {
//         const auto parsed = std::from_chars(rawPosition.data(), rawPosition.data() + rawPosition.size(), position)
//         if (parsed.ec != std::errc{} || parsed.ptr != rawPosition.data() + rawPosition.size()) position = 0
//     }
//     std::snprintf(record.text.data(), record.text.size(),
//         "server_severity=%s sqlstate=%.5s reason=%s statement_position=%llu details=redacted",
//         severityName, IsSqlState(state) ? state.data() : "none", reason, position)
//     errno = savedErrno
//     return record
// }
inline PostgreSQLNoticeRecord FormatPostgreSQLNotice(const PGresult* result) noexcept {
    const int savedErrno = errno;
    const auto field = [result](int code) -> std::string_view {
        const char* value = result == nullptr ? nullptr : PQresultErrorField(result, code);
        return value == nullptr ? std::string_view{} : std::string_view(value);
    };
    const auto state = field(PG_DIAG_SQLSTATE);
    const auto severity = field(PG_DIAG_SEVERITY_NONLOCALIZED);
    const char* severityName = "unknown severity";
    for (const char* known : {"DEBUG", "LOG", "INFO", "NOTICE", "WARNING", "ERROR", "FATAL", "PANIC"}) {
        if (severity == known) {
            severityName = known;
            break;
        }
    }
    PostgreSQLNoticeRecord record;
    record.debug = severity == "DEBUG";
    record.warning = !(severity == "DEBUG" || severity == "LOG" || severity == "INFO" || severity == "NOTICE");
    record.error = severity == "ERROR" || severity == "FATAL" || severity == "PANIC";
    std::snprintf(
        record.text.data(), record.text.size(), "PostgreSQL %s (SQLSTATE %.5s).", severityName,
        IsSqlState(state) ? state.data() : "none"
    );
    errno = savedErrno;
    return record;
}

// Read fields only while PGresult is alive. They may contain SQL literals and
// must not be rewritten or redacted. Emitters keep ordinary control escaping.
// The fixed record above is just the heading. Payload fields are written in
// bounded fragments so even a long DETAIL, CONTEXT or internal query survives.
template <class Emit>
inline void ForEachPostgreSQLDiagnosticFragment(const PGresult* result, Emit emit) {
    if (result == nullptr) {
        return;
    }
    struct Field {
        int code;
        const char* label;
    };
    constexpr Field fields[] = {
        {PG_DIAG_MESSAGE_PRIMARY, "Message"},
        {PG_DIAG_MESSAGE_DETAIL, "Detail"},
        {PG_DIAG_MESSAGE_HINT, "Hint"},
        {PG_DIAG_CONTEXT, "Context"},
        {PG_DIAG_STATEMENT_POSITION, "Statement position"},
        {PG_DIAG_INTERNAL_POSITION, "Internal position"},
        {PG_DIAG_INTERNAL_QUERY, "SQL"},
    };
    for (const auto& field : fields) {
        const char* value = PQresultErrorField(result, field.code);
        if (value == nullptr || value[0] == '\0') {
            continue;
        }
        ForEachSqlFragment(
            value, [&](std::uint64_t sequence, std::size_t part, std::size_t total, std::string_view fragment) {
                emit(field.label, sequence, part, total, fragment);
            }
        );
    }
}

} // namespace runtime::logging
