#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>

namespace runtime::logging {

// These helpers describe an error. Keep the original SQL and driver text
// in the diagnostic as well.
// SQLSTATE meanings: https://www.postgresql.org/docs/18/errcodes-appendix.html
// ODBC-specific codes: https://learn.microsoft.com/en-us/sql/odbc/reference/appendixes/appendix-a-odbc-error-codes
inline bool IsSqlState(std::string_view state) noexcept {
    if (state.size() != 5) {
        return false;
    }
    for (const char c : state) {
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z'))) {
            return false;
        }
    }
    return true;
}

inline const char* SqlStateReason(std::string_view state) noexcept {
    if (!IsSqlState(state)) {
        return "diagnostic_unavailable";
    }
    if (state == "00000") {
        return "successful_completion";
    }
    if (state == "01004") {
        return "string_data_right_truncation";
    }
    if (state == "07006") {
        return "restricted_data_type_attribute_violation";
    }
    if (state == "08001") {
        return "connection_establishment_failed";
    }
    if (state == "08003") {
        return "connection_not_open";
    }
    if (state == "08S01") {
        return "communication_link_failure";
    }
    if (state == "22003") {
        return "numeric_value_out_of_range";
    }
    if (state == "22P02") {
        return "invalid_text_representation";
    }
    if (state == "23502") {
        return "not_null_violation";
    }
    if (state == "23503") {
        return "foreign_key_violation";
    }
    if (state == "23505") {
        return "unique_violation";
    }
    if (state == "23514") {
        return "check_violation";
    }
    if (state == "24000") {
        return "invalid_cursor_state";
    }
    if (state == "25P02") {
        return "transaction_already_aborted";
    }
    if (state == "28P01") {
        return "invalid_password";
    }
    if (state == "3D000") {
        return "invalid_catalog_name";
    }
    if (state == "3F000") {
        return "invalid_schema_name";
    }
    if (state == "40001") {
        return "serialization_failure";
    }
    if (state == "40P01") {
        return "deadlock_detected";
    }
    if (state == "42501") {
        return "insufficient_privilege";
    }
    if (state == "42601") {
        return "syntax_error";
    }
    if (state == "42703") {
        return "undefined_column";
    }
    if (state == "42710") {
        return "duplicate_object";
    }
    if (state == "42883") {
        return "undefined_function";
    }
    if (state == "42P01") {
        return "undefined_table";
    }
    if (state == "42P06") {
        return "duplicate_schema";
    }
    if (state == "42P07") {
        return "duplicate_table";
    }
    if (state == "53100") {
        return "disk_full";
    }
    if (state == "53200" || state == "HY001") {
        return "out_of_memory";
    }
    if (state == "53300") {
        return "too_many_connections";
    }
    if (state == "55P03") {
        return "lock_not_available";
    }
    if (state == "57014") {
        return "query_canceled";
    }
    if (state == "57P01") {
        return "admin_shutdown";
    }
    if (state == "57P03") {
        return "cannot_connect_now";
    }
    if (state == "P0001") {
        return "raise_exception";
    }
    if (state == "HY010") {
        return "function_sequence_error";
    }
    if (state == "HYT00") {
        return "timeout_expired";
    }
    if (state == "HYT01") {
        return "connection_timeout_expired";
    }
    const auto category = state.substr(0, 2);
    if (category == "01") {
        return "warning";
    }
    if (category == "02") {
        return "no_data";
    }
    if (category == "08") {
        return "connection_exception";
    }
    if (category == "22") {
        return "data_exception";
    }
    if (category == "23") {
        return "integrity_constraint_violation";
    }
    if (category == "25") {
        return "invalid_transaction_state";
    }
    if (category == "28") {
        return "invalid_authorization_specification";
    }
    if (category == "40") {
        return "transaction_rollback";
    }
    if (category == "42") {
        return "syntax_or_access_rule_violation";
    }
    if (category == "53") {
        return "insufficient_resources";
    }
    return "database_or_driver_error";
}

// Classify only a bounded prefix and return fixed labels, never identifiers or
// literal values. This is not an SQL parser or a redaction filter.
inline const char* SqlStatementKind(const char* statement) noexcept {
    if (statement == nullptr) {
        return "none";
    }
    std::size_t count = 0;
    while (count < 64 && statement[count] != '\0') {
        ++count;
    }
    auto input = std::string_view(statement, count);
    const auto skipSpace = [&input] {
        while (!input.empty() &&
            (input.front() == ' ' || input.front() == '\t' || input.front() == '\r' || input.front() == '\n')) {
            input.remove_prefix(1);
        }
    };
    skipSpace();
    if (!input.empty() && input.front() == '{') {
        input.remove_prefix(1);
        skipSpace();
        if (!input.empty() && input.front() == '?') {
            input.remove_prefix(1);
            skipSpace();
            if (input.empty() || input.front() != '=') {
                return "other";
            }
            input.remove_prefix(1);
            skipSpace();
        }
    }
    constexpr std::string_view kinds[] = {"call", "exec", "execute", "select", "insert", "update", "delete", "begin",
        "commit", "rollback", "create", "alter", "drop", "with"};
    for (const auto kind : kinds) {
        if (input.size() < kind.size()) {
            continue;
        }
        bool matches = true;
        for (std::size_t i = 0; i < kind.size(); ++i) {
            const char c = input[i] >= 'A' && input[i] <= 'Z' ? static_cast<char>(input[i] + ('a' - 'A')) : input[i];
            if (c != kind[i]) {
                matches = false;
                break;
            }
        }
        if (!matches) {
            continue;
        }
        if (input.size() == kind.size()) {
            return kind.data();
        }
        const char next = input[kind.size()];
        if (next == ' ' || next == '\t' || next == '\r' || next == '\n' || next == '(' || next == ';' || next == '}') {
            return kind.data();
        }
    }
    return "other";
}

// Read only the ASCII prefix needed by the classifier. Do not allocate or
// transcode an entire potentially sensitive host-wide/target-UTF-16 statement.
template <class Character>
inline const char* WideSqlStatementKind(const Character* statement) noexcept {
    if (statement == nullptr) {
        return "none";
    }
    char prefix[65]{};
    for (std::size_t i = 0; i < 64 && statement[i] != 0; ++i) {
        const auto value = static_cast<unsigned long>(statement[i]);
        prefix[i] = value <= 0x7f ? static_cast<char>(value) : '?';
    }
    return SqlStatementKind(prefix);
}

inline std::atomic<std::uint64_t> sqlDiagnosticSequence{0};

// Preserve the complete supplied UTF-8 statement, including whitespace,
// literals and placeholders. Chunking accommodates the smallest legacy sink
// (999 bytes) without dropping a long query or splitting valid UTF-8 characters.
// The emitter must put a closing delimiter after each fragment so a sink's
// trailing-whitespace trimming cannot modify SQL at a fragment boundary.
template <class Emit>
inline void ForEachSqlFragment(std::string_view statement, Emit emit) {
    // constexpr std::size_t limit = 600
    // const auto fragmentSize = [limit](std::string_view remaining) noexcept {
    const auto fragmentSize = [](std::string_view remaining) noexcept {
        constexpr std::size_t limit = 600;
        if (remaining.size() <= limit) {
            return remaining.size();
        }
        std::size_t length = limit;
        while (length > 0 && (static_cast<unsigned char>(remaining[length]) & 0xc0U) == 0x80U) {
            --length;
        }
        return length == 0 ? limit : length;
    };
    std::size_t total = 0;
    for (auto remaining = statement; !remaining.empty();) {
        ++total;
        remaining.remove_prefix(fragmentSize(remaining));
    }
    if (total == 0) {
        total = 1;
    }
    const auto sequence = sqlDiagnosticSequence.fetch_add(1, std::memory_order_relaxed) + 1;
    auto remaining = statement;
    for (std::size_t part = 1; part <= total; ++part) {
        const auto length = fragmentSize(remaining);
        emit(sequence, part, total, remaining.substr(0, length));
        remaining.remove_prefix(length);
    }
}

} // namespace runtime::logging
