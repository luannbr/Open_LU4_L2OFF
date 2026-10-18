#include "PostgreSQLMigrations.h"
#include "DatabaseLog.h"
#include "OperatorLog.h"
#include "PostgreSQLNoticeLog.h"
#include "Shared/src/Runtime/Utf8.h"

#include <libpq-fe.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <climits>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string_view>

namespace runtime {
namespace {

using Connection = std::unique_ptr<PGconn, decltype(&PQfinish)>;
using Result = std::unique_ptr<PGresult, decltype(&PQclear)>;

#if defined(__MINGW_PRINTF_FORMAT)
__attribute__((format(__MINGW_PRINTF_FORMAT, 3, 4)))
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
void WriteMigrationLog(logging::Level level, const std::string& application, const char* format, ...) noexcept {
    const int savedErrno = errno;
    std::array<char, 8192> text{};
    va_list arguments;
    va_start(arguments, format);
    const int length = std::vsnprintf(text.data(), text.size(), format, arguments);
    va_end(arguments);
    if (length < 0) {
        // logging::Write(application.c_str(), logging::Level::Error,
        //     "component=migrations event=diagnostic_format_failed")
        // logging::Write(application.c_str(), logging::Level::Error,
        //     "[migrations] Could not format a diagnostic message.")
        logging::Write(
            application.c_str(), logging::Level::Error, "[migrations] Could not format a diagnostic message.", __FILE__
        );
        errno = savedErrno;
        return;
    }
    std::size_t messageLength = static_cast<std::size_t>(length);
    if (messageLength >= text.size()) {
        constexpr char marker[] = " [truncated]";
        // std::memcpy(text.data() + text.size() - sizeof(marker), marker, sizeof(marker))
        std::size_t end = text.size() - sizeof(marker);
        while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0U) == 0x80U) {
            --end;
        }
        std::memcpy(text.data() + end, marker, sizeof(marker));
        messageLength = end + sizeof(marker) - 1;
    }
    // logging::Write(application.c_str(), level, text.data())
    // logging::Write(application.c_str(), level, std::string_view(text.data(), messageLength))
    logging::Write(application.c_str(), level, std::string_view(text.data(), messageLength), __FILE__);
    errno = savedErrno;
}

std::string_view DiagnosticField(const PGresult* result, int field) noexcept {
    const char* value = result == nullptr ? nullptr : PQresultErrorField(result, field);
    return value == nullptr ? std::string_view{} : std::string_view(value);
}

// Position fields now use the same lossless field emitter as message and SQL.
// std::uint64_t DiagnosticPosition(const PGresult* result, int field) noexcept {
//     const auto value = DiagnosticField(result, field)
//     if (value.empty()) return 0.
//     std::uint64_t position = 0
//     const auto parsed = std::from_chars(value.data(), value.data() + value.size(), position)
//     return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() ? position : 0
// }

// std::string DatabaseDiagnostic(PGconn* connection, const PGresult* result) {
//     const auto state = DiagnosticField(result, PG_DIAG_SQLSTATE)
//     const bool valid = logging::IsSqlState(state)
//     const char* reason = valid ? logging::SqlStateReason(state) :
//         PQstatus(connection) != CONNECTION_OK ? "connection_unavailable" : "unexpected_query_result".
//     return "sqlstate=" + (valid ? std::string(state) : "none") + " reason=" + reason +
//         " result_status=" + std::to_string(static_cast<int>(PQresultStatus(result))) +
//         " statement_position=" + std::to_string(DiagnosticPosition(result, PG_DIAG_STATEMENT_POSITION)) +
//         " internal_position=" + std::to_string(DiagnosticPosition(result, PG_DIAG_INTERNAL_POSITION)) +
//         " details=redacted".
// }
std::string DatabaseDiagnostic(PGconn* connection, const PGresult* result) {
    const auto state = DiagnosticField(result, PG_DIAG_SQLSTATE);
    const bool valid = logging::IsSqlState(state);
    std::string reason = valid                                           ? logging::SqlStateReason(state)
        : connection == nullptr || PQstatus(connection) != CONNECTION_OK ? "connection unavailable"
                                                                         : "unexpected query result";
    std::replace(reason.begin(), reason.end(), '_', ' ');
    return "PostgreSQL returned " + reason + " (SQLSTATE " + (valid ? std::string(state) : "none") + ", result " +
        std::to_string(static_cast<int>(PQresultStatus(result))) + ").";
}

struct MigrationLogContext {
    const std::string& application;
    const std::string& current;
    const char* phase = "connect";
    std::size_t suppressedNotices = 0;
    std::size_t warnings = 0;
};

const char* MigrationPhase(const char* phase) noexcept {
    struct Phase {
        const char* key;
        const char* description;
    };
    constexpr Phase phases[] = {
        {"connect", "connecting to PostgreSQL"},
        {"create_database", "creating the database"},
        {"acquire_lock", "acquiring the migration lock"},
        {"create_history", "creating migration history"},
        {"load_history", "loading migration history"},
        {"verify_hashes", "verifying migration hashes"},
        {"begin", "starting a transaction"},
        {"configure_transaction", "configuring the transaction"},
        {"execute_sql", "executing SQL"},
        {"record_history", "recording migration history"},
        {"commit", "committing the transaction"},
    };
    for (const auto& entry : phases) {
        if (std::strcmp(phase, entry.key) == 0) {
            return entry.description;
        }
    }
    return phase;
}

void WriteMigrationField(
    logging::Level level,
    const MigrationLogContext& context,
    const char* label,
    std::uint64_t sequence,
    std::size_t part,
    std::size_t total,
    std::string_view fragment
) noexcept {
    const char* name = context.current.empty() ? "startup" : context.current.c_str();
    if (total == 1) {
        WriteMigrationLog(
            level, context.application, "[migrations] %s, %s. %s: \"%.*s\"", name, MigrationPhase(context.phase), label,
            static_cast<int>(fragment.size()), fragment.data()
        );
    } else {
        WriteMigrationLog(
            level, context.application, "[migrations] %s, %s. %s %llu, part %zu/%zu: \"%.*s\"", name,
            MigrationPhase(context.phase), label, static_cast<unsigned long long>(sequence), part, total,
            static_cast<int>(fragment.size()), fragment.data()
        );
    }
}

void WriteMigrationDetails(logging::Level level, const MigrationLogContext& context, const PGresult* result) noexcept {
    logging::ForEachPostgreSQLDiagnosticFragment(
        result,
        [&](const char* label, std::uint64_t sequence, std::size_t part, std::size_t total, std::string_view fragment) {
            WriteMigrationField(level, context, label, sequence, part, total, fragment);
        }
    );
}

void ReceiveMigrationNotice(void* argument, const PGresult* result) noexcept {
    auto& context = *static_cast<MigrationLogContext*>(argument);
    const auto severity = DiagnosticField(result, PG_DIAG_SEVERITY_NONLOCALIZED);
    const auto state = DiagnosticField(result, PG_DIAG_SQLSTATE);
    const auto source = DiagnosticField(result, PG_DIAG_SOURCE_FUNCTION);
    // Noise: IF [NOT] EXISTS emits one NOTICE per already-created/absent object.
    // Previously libpq printed each complete notice to stderr. Count only proven
    // idempotent-DDL cases. Do not suppress WARNING, arbitrary RAISE NOTICE,
    // or every 00000 notice (which also includes cascading DDL).
    const bool expected = severity == "NOTICE" &&
        (state == "42P06" || state == "42P07" || state == "42710" ||
            (state == "00000" && (source == "DropErrorMsgNonExistent" || source == "ATExecDropConstraint")));
    if (expected) {
        ++context.suppressedNotices;
        return;
    }
    const bool informational = severity == "NOTICE" || severity == "INFO" || severity == "DEBUG" || severity == "LOG";
    if (!informational) {
        ++context.warnings;
    }
    // const char* reason = source == "exec_stmt_raise" ?
    //     (informational ? "migration_raised_notice" : "migration_raised_warning") :
    //     source == "reportDependentObjects" ? "dependent_objects_affected" : logging::SqlStateReason(state)
    // // The primary message, detail and context fields can contain SQL values.
    // WriteMigrationLog(informational ? logging::Level::Info : logging::Level::Warning,
    //     context.application,
    //     "component=migrations event=server_notice phase=%s migration=\"%s\" sqlstate=%.5s reason=%s
    //     statement_position=%llu details=redacted", context.phase, context.current.c_str(), logging::IsSqlState(state) ?
    //     state.data() : "none", reason, static_cast<unsigned long long>(DiagnosticPosition(result,
    //     PG_DIAG_STATEMENT_POSITION)))
    const auto record = logging::FormatPostgreSQLNotice(result);
    // const auto level = record.error ? logging::Level::Error :
    //     informational ? logging::Level::Info : logging::Level::Warning.
    const auto level = record.error ? logging::Level::Error
        : record.warning            ? logging::Level::Warning
        : record.debug              ? logging::Level::Debug
                                    : logging::Level::Info;
    WriteMigrationLog(
        level, context.application, "[migrations] %s, %s: %s", context.current.empty() ? "startup" : context.current.c_str(),
        MigrationPhase(context.phase), record.text.data()
    );
    WriteMigrationDetails(level, context, result);
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot read " + path.string());
    }
    std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (input.bad()) {
        throw std::runtime_error("read failed: " + path.string());
    }
    if (text.size() > INT_MAX || text.find('\0') != std::string::npos) {
        throw std::runtime_error("expected UTF-8 text without NUL bytes: " + path.string());
    }
    return text;
}

bool Letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }

bool Digit(char c) { return c >= '0' && c <= '9'; }

struct Statement {
    std::size_t start;
    std::size_t end;
    std::vector<std::string> words;
};

// Inspect only top-level SQL statements. Semicolons and transaction words in
// strings, quoted identifiers, dollar-quoted functions and nested comments are
// not statement boundaries. SQL itself is left to PostgreSQL to validate.
std::vector<Statement> Statements(const std::string& sql) {
    std::vector<Statement> statements;
    Statement current{0, 0, {}};
    bool content = false;
    for (std::size_t i = 0; i < sql.size();) {
        const char c = sql[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f') {
            ++i;
            continue;
        }
        if (sql.compare(i, 2, "--") == 0) {
            const auto end = sql.find('\n', i + 2);
            i = end == std::string::npos ? sql.size() : end + 1;
            continue;
        }
        if (sql.compare(i, 2, "/*") == 0) {
            int depth = 1;
            i += 2;
            while (i < sql.size() && depth) {
                if (sql.compare(i, 2, "/*") == 0) {
                    ++depth;
                    i += 2;
                } else if (sql.compare(i, 2, "*/") == 0) {
                    --depth;
                    i += 2;
                } else {
                    ++i;
                }
            }
            if (depth) {
                throw std::runtime_error("unterminated SQL comment");
            }
            continue;
        }
        if (c == ';') {
            if (content) {
                current.end = i + 1;
                statements.push_back(current);
            }
            current = {i + 1, 0, {}};
            content = false;
            ++i;
            continue;
        }
        content = true;
        if (c == '\'') {
            const bool escapes =
                i > 0 && (sql[i - 1] == 'e' || sql[i - 1] == 'E') && (i < 2 || (!Letter(sql[i - 2]) && !Digit(sql[i - 2])));
            bool closed = false;
            ++i;
            while (i < sql.size()) {
                if (escapes && sql[i] == '\\') {
                    i += std::min<std::size_t>(2, sql.size() - i);
                    continue;
                }
                if (sql[i++] == '\'') {
                    if (i < sql.size() && sql[i] == '\'') {
                        ++i;
                    } else {
                        closed = true;
                        break;
                    }
                }
            }
            if (!closed) {
                throw std::runtime_error("unterminated SQL string");
            }
            current.words.emplace_back("<string>");
            continue;
        }
        if (c == '"') {
            bool closed = false;
            ++i;
            while (i < sql.size()) {
                if (sql[i++] == '"') {
                    if (i < sql.size() && sql[i] == '"') {
                        ++i;
                    } else {
                        closed = true;
                        break;
                    }
                }
            }
            if (!closed) {
                throw std::runtime_error("unterminated SQL identifier");
            }
            current.words.emplace_back("<identifier>");
            continue;
        }
        if (c == '$') {
            std::size_t tagEnd = i + 1;
            if (tagEnd < sql.size() && Letter(sql[tagEnd])) {
                while (tagEnd < sql.size() && (Letter(sql[tagEnd]) || Digit(sql[tagEnd]))) {
                    ++tagEnd;
                }
            }
            if (tagEnd < sql.size() && sql[tagEnd] == '$') {
                const std::string tag = sql.substr(i, tagEnd - i + 1);
                const auto end = sql.find(tag, tagEnd + 1);
                if (end == std::string::npos) {
                    throw std::runtime_error("unterminated dollar-quoted SQL body");
                }
                i = end + tag.size();
                current.words.emplace_back("<body>");
                continue;
            }
        }
        if (Letter(c)) {
            std::string word;
            do {
                const char value = sql[i++];
                word += value >= 'a' && value <= 'z' ? static_cast<char>(value - 'a' + 'A') : value;
            } while (i < sql.size() && (Letter(sql[i]) || Digit(sql[i]) || sql[i] == '$'));
            current.words.push_back(std::move(word));
        } else {
            current.words.emplace_back(1, c);
            ++i;
        }
    }
    if (content) {
        current.end = sql.size();
        statements.push_back(std::move(current));
    }
    return statements;
}

std::string TransactionBody(std::string sql) {
    if (sql.compare(0, 3, "\xef\xbb\xbf") == 0) {
        sql.erase(0, 3);
    }
    const auto statements = Statements(sql);
    if (statements.empty()) {
        throw std::runtime_error("migration has no SQL statements");
    }
    auto simple = [](const Statement& s, std::string_view command) {
        return !s.words.empty() && s.words[0] == command &&
            (s.words.size() == 1 || (s.words.size() == 2 && (s.words[1] == "WORK" || s.words[1] == "TRANSACTION")));
    };
    std::size_t begin = 0, end = statements.size();
    if (simple(statements.front(), "BEGIN") && simple(statements.back(), "COMMIT") && end >= 2) {
        ++begin;
        --end;
    }
    if (begin == end) {
        throw std::runtime_error("migration has no SQL body");
    }
    for (std::size_t i = begin; i < end; ++i) {
        const auto& words = statements[i].words;
        if (words.empty()) {
            continue;
        }
        const auto& first = words[0];
        if (first == "BEGIN" || first == "COMMIT" || first == "END" || first == "ROLLBACK" || first == "ABORT" ||
            first == "SAVEPOINT" || first == "RELEASE" ||
            (words.size() > 1 && words[1] == "TRANSACTION" && (first == "START" || first == "PREPARE" || first == "SET"))) {
            throw std::runtime_error("transaction control is only allowed as one outer BEGIN/COMMIT pair");
        }
    }
    return sql.substr(statements[begin].start, statements[end - 1].end - statements[begin].start);
}

// Result Check(PGconn* connection, PGresult* raw) {
//     Result result(raw, PQclear)
//     if (!result || (PQresultStatus(result.get()) != PGRES_COMMAND_OK &&
//                     PQresultStatus(result.get()) != PGRES_TUPLES_OK)) {
//         // Sensitive data: PostgreSQL message/detail/context can echo SQL literals.
//         // throw std::runtime_error(result ? PQresultErrorMessage(result.get()) : PQerrorMessage(connection))
//         throw std::runtime_error(DatabaseDiagnostic(connection, result.get()))
//     }
//     return result
// }
Result Check(PGconn* connection, PGresult* raw, const MigrationLogContext& context, const char* sql) {
    Result result(raw, PQclear);
    if (!result || (PQresultStatus(result.get()) != PGRES_COMMAND_OK && PQresultStatus(result.get()) != PGRES_TUPLES_OK)) {
        WriteMigrationDetails(logging::Level::Error, context, result.get());
        if (sql != nullptr) {
            logging::ForEachSqlFragment(
                sql, [&](std::uint64_t sequence, std::size_t part, std::size_t total, std::string_view fragment) {
                    WriteMigrationField(logging::Level::Error, context, "SQL", sequence, part, total, fragment);
                }
            );
        }
        throw std::runtime_error(DatabaseDiagnostic(connection, result.get()));
    }
    return result;
}

// Result Execute(PGconn* connection, const char* sql) { return Check(connection, PQexec(connection, sql)). }
Result Execute(PGconn* connection, const char* sql, const MigrationLogContext& context) {
    return Check(connection, PQexec(connection, sql), context, sql);
}

// Result Parameters(PGconn* connection, const char* sql, const std::vector<std::string>& parameters) {
//     std::vector<const char*> values
//     for (const auto& value : parameters) values.push_back(value.c_str())
//     return Check(connection, PQexecParams(connection, sql, static_cast<int>(values.size()),
//         nullptr, values.data(), nullptr, nullptr, 0))
// }
Result Parameters(
    PGconn* connection, const char* sql, const std::vector<std::string>& parameters, const MigrationLogContext& context
) {
    std::vector<const char*> values;
    for (const auto& value : parameters) {
        values.push_back(value.c_str());
    }
    return Check(
        connection,
        PQexecParams(connection, sql, static_cast<int>(values.size()), nullptr, values.data(), nullptr, nullptr, 0), context,
        sql
    );
}

// std::string Sha256(PGconn* connection, const std::string& bytes) {
//     const Oid type = 17, // bytea, binary parameter preserves the original file bytes.
//     const char* value = bytes.data()
//     const int length = static_cast<int>(bytes.size())
//     const int format = 1
//     auto result = Check(connection, PQexecParams(connection,
//         "SELECT encode(sha256($1::bytea), 'hex')", 1, &type, &value, &length, &format, 0))
//     return PQgetvalue(result.get(), 0, 0)
// }
std::string Sha256(PGconn* connection, const std::string& bytes, const MigrationLogContext& context) {
    const Oid type = 17; // bytea. Binary parameter keeps the original file bytes.
    const char* value = bytes.data();
    const int length = static_cast<int>(bytes.size());
    const int format = 1;
    auto result = Check(
        connection,
        PQexecParams(connection, "SELECT encode(sha256($1::bytea), 'hex')", 1, &type, &value, &length, &format, 0), context,
        "SELECT encode(sha256($1::bytea), 'hex')"
    );
    return PQgetvalue(result.get(), 0, 0);
}

} // namespace

bool LoadPostgreSQLMigrations(
    const std::filesystem::path& directory, std::vector<PostgreSQLMigration>& migrations, std::string& error
) {
    migrations.clear();
    error.clear();
    try {
        std::vector<std::pair<std::uint64_t, std::string>> files;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".sql") {
                continue;
            }
            const std::string name = Utf8Bytes(entry.path().filename().u8string());
            const auto underscore = name.find('_');
            std::uint64_t number = 0;
            if (underscore == std::string::npos || underscore == 0 || underscore + 5 >= name.size()) {
                throw std::runtime_error("expected N_name.sql: " + name);
            }
            const auto parsed = std::from_chars(name.data(), name.data() + underscore, number);
            if (parsed.ec != std::errc{} || parsed.ptr != name.data() + underscore) {
                throw std::runtime_error("invalid migration number: " + name);
            }
            files.emplace_back(number, name);
        }
        std::sort(files.begin(), files.end());
        std::vector<std::string> names;
        const auto orderFile = directory / "migrations.order";
        if (std::filesystem::exists(orderFile)) {
            const std::string order = ReadFile(orderFile);
            std::set<std::string> remaining;
            for (const auto& file : files) {
                remaining.insert(file.second);
            }
            std::size_t start = 0;
            std::size_t lineNumber = 0;
            while (start < order.size()) {
                ++lineNumber;
                const auto end = order.find('\n', start);
                std::string line = order.substr(start, end == std::string::npos ? end : end - start);
                const auto first = line.find_first_not_of(" \t\r");
                if (first != std::string::npos && line[first] != '#') {
                    line = line.substr(first, line.find_last_not_of(" \t\r") - first + 1);
                    // Sensitive data: an invalid manifest entry may be pasted credentials or SQL.
                    // if (remaining.erase(line) != 1) throw std::runtime_error("unknown or repeated migrations.order entry:
                    // " + line)
                    if (remaining.erase(line) != 1) {
                        throw std::runtime_error(
                            "unknown or repeated migrations.order entry at line " + std::to_string(lineNumber)
                        );
                    }
                    names.push_back(std::move(line));
                }
                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
            }
            if (!remaining.empty()) {
                throw std::runtime_error("migrations.order omits " + *remaining.begin());
            }
        } else {
            for (const auto& file : files) {
                names.push_back(file.second);
            }
        }
        std::vector<PostgreSQLMigration> loaded;
        for (const auto& name : names) {
            PostgreSQLMigration migration;
            migration.name = name;
            migration.source = ReadFile(directory / std::filesystem::path(Utf8CodeUnits(name)));
            try {
                migration.sql = TransactionBody(migration.source);
            } catch (const std::exception& exception) {
                throw std::runtime_error(name + ": " + exception.what());
            }
            loaded.push_back(std::move(migration));
        }
        migrations = std::move(loaded);
        return true;
    } catch (const std::exception& exception) {
        error = "migrations in " + directory.string() + ": " + exception.what();
        return false;
    }
}

bool CreatePostgreSQLDatabaseIfMissing(
    const std::string& connectionString, const std::string& application, std::string& error
) {
    error.clear();
    const std::string current;
    MigrationLogContext context{application, current};
    Connection target(PQconnectdb(connectionString.c_str()), PQfinish);
    if (!target) {
        error = "Could not allocate a PostgreSQL connection.";
        return false;
    }
    if (PQstatus(target.get()) == CONNECTION_OK) {
        return true;
    }
    // libpq knows the database name after it reads the options, also when
    // the connection fails.
    const char* const targetName = PQdb(target.get());
    if (targetName == nullptr || targetName[0] == '\0') {
        error = "Could not connect to the configured PostgreSQL database.";
        return false;
    }
    const std::string databaseName = targetName;
    target.reset();

    // The first dbname value is the complete connection string. The second
    // dbname value replaces only the database name in it.
    Connection maintenance(nullptr, PQfinish);
    for (const char* const name : {"postgres", "template1"}) {
        if (databaseName == name) {
            continue;
        }
        const char* const keywords[] = {"dbname", "dbname", nullptr};
        const char* const values[] = {connectionString.c_str(), name, nullptr};
        maintenance.reset(PQconnectdbParams(keywords, values, 1));
        if (maintenance && PQstatus(maintenance.get()) == CONNECTION_OK) {
            break;
        }
        maintenance.reset();
    }
    if (!maintenance) {
        // Sensitive data: connection errors can include conninfo and credentials.
        error = "Could not connect to database \"" + databaseName +
            "\" or to the postgres and template1 databases. Check the host, port, user and password.";
        return false;
    }

    context.phase = "create_database";
    const char* const parameters[] = {databaseName.c_str()};
    Result existing(
        PQexecParams(
            maintenance.get(), "SELECT 1 FROM pg_database WHERE datname = $1", 1, nullptr, parameters, nullptr, nullptr, 0
        ),
        PQclear
    );
    if (!existing || PQresultStatus(existing.get()) != PGRES_TUPLES_OK) {
        WriteMigrationDetails(logging::Level::Error, context, existing.get());
        error = "Could not check whether database \"" + databaseName + "\" exists. " +
            DatabaseDiagnostic(maintenance.get(), existing.get());
        return false;
    }
    if (PQntuples(existing.get()) != 0) {
        error = "Could not connect to database \"" + databaseName +
            "\". The database exists, so check the user, password and access rules.";
        return false;
    }
    std::unique_ptr<char, decltype(&PQfreemem)> identifier(
        PQescapeIdentifier(maintenance.get(), databaseName.c_str(), databaseName.size()), PQfreemem
    );
    if (!identifier) {
        error = "Could not quote the database name \"" + databaseName + "\".";
        return false;
    }
    WriteMigrationLog(
        logging::Level::Info, application, "[migrations] Database \"%s\" does not exist. Creating it.",
        databaseName.c_str()
    );
    // Game text is UTF-8. template0 allows an encoding that is different
    // from the template1 encoding.
    const std::string sql = std::string("CREATE DATABASE ") + identifier.get() + " ENCODING 'UTF8' TEMPLATE template0";
    Result created(PQexec(maintenance.get(), sql.c_str()), PQclear);
    if (!created || PQresultStatus(created.get()) != PGRES_COMMAND_OK) {
        // 42P04: another process created the database after the check.
        if (DiagnosticField(created.get(), PG_DIAG_SQLSTATE) == "42P04") {
            return true;
        }
        WriteMigrationDetails(logging::Level::Error, context, created.get());
        error = "Could not create database \"" + databaseName + "\". " +
            DatabaseDiagnostic(maintenance.get(), created.get());
        return false;
    }
    WriteMigrationLog(logging::Level::Info, application, "[migrations] Created database \"%s\".", databaseName.c_str());
    return true;
}

bool ApplyPostgreSQLMigrations(
    const std::string& connectionString,
    const std::string& application,
    const std::string& defaultSchema,
    const std::vector<PostgreSQLMigration>& migrations,
    PostgreSQLMigrationReport& report,
    std::string& error
) {
    report = {};
    error.clear();
    // Original declaration order kept. Notice context must outlive PGconn/results.
    // Connection connection(nullptr, PQfinish)
    // std::string current
    std::string current;
    MigrationLogContext context{application, current};
    Connection connection(nullptr, PQfinish);
    const auto started = std::chrono::steady_clock::now();
    // WriteMigrationLog(logging::Level::Info, application,
    //     "component=migrations event=started schema=\"%s\" discovered=%zu",
    //     defaultSchema.c_str(), migrations.size())
    WriteMigrationLog(
        logging::Level::Info, application, "[migrations] Checking schema \"%s\": %zu migration files.",
        defaultSchema.c_str(), migrations.size()
    );
    try {
        connection.reset(PQconnectdb(connectionString.c_str()));
        if (!connection || PQstatus(connection.get()) != CONNECTION_OK) {
            // Sensitive data: connection errors can include conninfo and credentials.
            // throw std::runtime_error(connection ? PQerrorMessage(connection.get()) : "cannot allocate PostgreSQL
            // connection"), throw std::runtime_error(connection ? "reason=connection_establishment_failed details=redacted"
            // :
            //     "reason=connection_allocation_failed")
            throw std::runtime_error(
                connection ? "Could not connect to the configured PostgreSQL database."
                           : "Could not allocate a PostgreSQL connection."
            );
        }
        PQsetNoticeReceiver(connection.get(), ReceiveMigrationNotice, &context);
        // One database-wide session lock also serializes history-table creation
        // across products. PQfinish releases it on every success/failure path.
        context.phase = "acquire_lock";
        // Execute(connection.get(), "SELECT pg_advisory_lock(742031, 1)")
        Execute(connection.get(), "SELECT pg_advisory_lock(742031, 1)", context);
        context.phase = "create_history";
        // Execute(connection.get(),
        //     "CREATE TABLE IF NOT EXISTS public.server_migrations ("
        //     "application text NOT NULL, name text NOT NULL, hash text NOT NULL, "
        //     "applied_at timestamptz NOT NULL DEFAULT clock_timestamp(), "
        //     "PRIMARY KEY(application, name))")
        Execute(
            connection.get(),
            "CREATE TABLE IF NOT EXISTS public.server_migrations ("
            "application text NOT NULL, name text NOT NULL, hash text NOT NULL, "
            "applied_at timestamptz NOT NULL DEFAULT clock_timestamp(), "
            "PRIMARY KEY(application, name))",
            context
        );
        context.phase = "load_history";
        // auto history = Parameters(connection.get(),
        //     "SELECT name, hash FROM public.server_migrations WHERE application=$1", {application})
        auto history = Parameters(
            connection.get(), "SELECT name, hash FROM public.server_migrations WHERE application=$1", {application}, context
        );
        std::map<std::string, std::string> applied;
        for (int row = 0; row < PQntuples(history.get()); ++row) {
            applied.emplace(PQgetvalue(history.get(), row, 0), PQgetvalue(history.get(), row, 1));
        }
        std::vector<std::string> hashes;
        // Validate all existing entries before executing any pending migration.
        context.phase = "verify_hashes";
        for (const auto& migration : migrations) {
            current = migration.name;
            // hashes.push_back(Sha256(connection.get(), migration.source))
            hashes.push_back(Sha256(connection.get(), migration.source, context));
            const auto found = applied.find(migration.name);
            if (found != applied.end() && found->second != hashes.back()) {
                throw std::runtime_error("applied migration has changed; add a new migration instead");
            }
        }
        for (std::size_t index = 0; index < migrations.size(); ++index) {
            const auto& migration = migrations[index];
            current = migration.name;
            if (applied.count(migration.name)) {
                ++report.skipped;
                continue;
            }
            // std::fprintf(stdout, "[migrations] %s: applying %s\n", application.c_str(), current.c_str())
            // WriteMigrationLog(logging::Level::Info, application,
            //     "component=migrations event=applying migration=\"%s\" index=%zu total=%zu",
            //     current.c_str(), index + 1, migrations.size())
            WriteMigrationLog(
                logging::Level::Info, application, "[migrations] Applying \"%s\" (%zu of %zu).", current.c_str(), index + 1,
                migrations.size()
            );
            context.phase = "begin";
            // Execute(connection.get(), "BEGIN")
            Execute(connection.get(), "BEGIN", context);
            context.phase = "configure_transaction";
            // Parameters(connection.get(),
            //     "SELECT set_config('search_path', quote_ident($1) || ', public', true)", {defaultSchema})
            Parameters(
                connection.get(), "SELECT set_config('search_path', quote_ident($1) || ', public', true)", {defaultSchema},
                context
            );
            // Execute(connection.get(), "SET LOCAL standard_conforming_strings = on")
            Execute(connection.get(), "SET LOCAL standard_conforming_strings = on", context);
            context.phase = "execute_sql";
            // Execute(connection.get(), migration.sql.c_str())
            Execute(connection.get(), migration.sql.c_str(), context);
            if (PQtransactionStatus(connection.get()) != PQTRANS_INTRANS) {
                throw std::runtime_error("migration did not preserve its transaction");
            }
            context.phase = "record_history";
            // Parameters(connection.get(),
            //     "INSERT INTO public.server_migrations(application,name,hash) VALUES($1,$2,$3)",
            //     {application, migration.name, hashes[index]})
            Parameters(
                connection.get(), "INSERT INTO public.server_migrations(application,name,hash) VALUES($1,$2,$3)",
                {application, migration.name, hashes[index]}, context
            );
            context.phase = "commit";
            // Execute(connection.get(), "COMMIT")
            Execute(connection.get(), "COMMIT", context);
            ++report.applied;
        }
        // std::fprintf(stdout, "[migrations] %s: applied=%zu skipped=%zu\n",
        //     application.c_str(), report.applied, report.skipped)
        // WriteMigrationLog(context.warnings ? logging::Level::Warning : logging::Level::Info, application,
        //     "component=migrations event=completed applied=%zu skipped=%zu suppressed_notices=%zu warnings=%zu
        //     elapsed_ms=%lld", report.applied, report.skipped, context.suppressedNotices, context.warnings,
        //     static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
        //         std::chrono::steady_clock::now() - started).count()))
        WriteMigrationLog(
            context.warnings ? logging::Level::Warning : logging::Level::Info, application,
            "[migrations] Finished: %zu applied, %zu already present; %zu expected notices suppressed, %zu warnings; %lld "
            "ms.",
            report.applied, report.skipped, context.suppressedNotices, context.warnings,
            static_cast<long long>(
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count()
            )
        );
        return true;
    } catch (const std::exception& exception) {
        // Closing the dedicated connection rolls back any in-flight migration,
        // including COPY protocol failures, and releases the advisory lock.
        // error = application + " migration " + current + ": " + exception.what()
        // Check/connect errors above contain metadata only. Local validation
        // exceptions contain fixed reasons and migration-file identities.
        // error = "phase=" + std::string(context.phase) + " migration=\"" + current +
        //     "\" applied=" + std::to_string(report.applied) + " skipped=" + std::to_string(report.skipped) +
        //     " suppressed_notices=" + std::to_string(context.suppressedNotices) +
        //     " warnings=" + std::to_string(context.warnings) + " " + exception.what()
        error = "Failed while " + std::string(MigrationPhase(context.phase)) +
            (current.empty() ? std::string() : " for migration \"" + current + "\"") + ". " + exception.what() +
            " Applied: " + std::to_string(report.applied) + "; skipped: " + std::to_string(report.skipped) +
            "; expected notices suppressed: " + std::to_string(context.suppressedNotices) +
            "; warnings: " + std::to_string(context.warnings) + ".";
        if (report.applied != 0) {
            error += " Earlier successfully committed migrations are not rolled back by this failure.";
        }
        if (std::strcmp(context.phase, "commit") == 0) {
            error += " The current migration's commit was not confirmed. Check public.server_migrations before retrying; do "
                     "not assume that it was rolled back.";
        }
        return false;
    }
}

} // namespace runtime
