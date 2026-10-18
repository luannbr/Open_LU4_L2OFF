#include "Shared/src/Log.h"

#include "DBConn.h"
#include "Shared/src/Runtime/DatabaseLog.h"
#include "Shared/src/Runtime/OperatorLog.h"
#include "Shared/src/Runtime/PostgreSQLNoticeLog.h"
#include "TextRuntime.h"

#include "Exception.h"
#include "platform/Runtime.h"

#include <libpq-fe.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <list>
#include <locale>
#include <map>
#include <mutex>
#include <sstream>
#include <string_view>
#include <thread>
#include <utility>

namespace {

// Keep the supplied SQL text, including literals.
// Connection credentials stay private.
void LogSqlStatement(LogType type, int databaseId, const char* context, std::string_view statement) {
    runtime::logging::ForEachSqlFragment(
        statement, [=](std::uint64_t sequence, std::size_t part, std::size_t total, std::string_view fragment) {
            if (total == 1) {
                // Log.Add(type, "[database %d] %s. SQL: \"%.*s\"",
                //     databaseId, context, static_cast<int>(fragment.size()), fragment.empty() ? "" : fragment.data())
                L2_LOG(
                    Log, type, "[database %d] %s. SQL: \"%.*s\"", databaseId, context, static_cast<int>(fragment.size()),
                    fragment.empty() ? "" : fragment.data()
                );
            } else {
                // Log.Add(type, "[database %d] %s. SQL %llu, part %zu/%zu: \"%.*s\"",
                //     databaseId, context, static_cast<unsigned long long>(sequence), part, total,
                //     static_cast<int>(fragment.size()), fragment.empty() ? "" : fragment.data())
                L2_LOG(
                    Log, type, "[database %d] %s. SQL %llu, part %zu/%zu: \"%.*s\"", databaseId, context,
                    static_cast<unsigned long long>(sequence), part, total, static_cast<int>(fragment.size()),
                    fragment.empty() ? "" : fragment.data()
                );
            }
        }
    );
}


void LogPostgreSQLDetails(LogType type, int databaseId, const PGresult* result) {
    runtime::logging::ForEachPostgreSQLDiagnosticFragment(
        result,
        [=](const char* label, std::uint64_t sequence, std::size_t part, std::size_t total, std::string_view fragment) {
            if (total == 1) {
                // Log.Add(type, "[database %d] PostgreSQL %s: \"%.*s\"",
                //     databaseId, label, static_cast<int>(fragment.size()), fragment.data())
                L2_LOG(
                    Log, type, "[database %d] PostgreSQL %s: \"%.*s\"", databaseId, label, static_cast<int>(fragment.size()),
                    fragment.data()
                );
            } else {
                // Log.Add(type, "[database %d] PostgreSQL %s %llu, part %zu/%zu: \"%.*s\"",
                //     databaseId, label, static_cast<unsigned long long>(sequence), part, total,
                //     static_cast<int>(fragment.size()), fragment.data())
                L2_LOG(
                    Log, type, "[database %d] PostgreSQL %s %llu, part %zu/%zu: \"%.*s\"", databaseId, label,
                    static_cast<unsigned long long>(sequence), part, total, static_cast<int>(fragment.size()),
                    fragment.data()
                );
            }
        }
    );
}

constexpr std::uint32_t kConnectionTimeoutSeconds = 60;
constexpr std::uint32_t kSlowQueryMilliseconds = 1000;
constexpr std::uint32_t kMissingProcedureIntervalMilliseconds = 600000;
constexpr std::int64_t kNullData = -1;

std::string LowerAscii(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

std::string_view TrimAscii(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return value;
}

bool StartsWithAsciiInsensitive(std::string_view value, std::string_view prefix) {
    if (value.size() < prefix.size()) {
        return false;
    }
    for (std::size_t index = 0; index < prefix.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(value[index])) !=
            std::tolower(static_cast<unsigned char>(prefix[index]))) {
            return false;
        }
    }
    return true;
}

void AppendUtf8(std::string& output, std::uint32_t codePoint) {
    if (codePoint <= 0x7fU) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7ffU) {
        output.push_back(static_cast<char>(0xc0U | (codePoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    } else if (codePoint <= 0xffffU) {
        output.push_back(static_cast<char>(0xe0U | (codePoint >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    } else {
        output.push_back(static_cast<char>(0xf0U | (codePoint >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    }
}

std::uint32_t NextWideCodePoint(const wchar_t* input, std::size_t& index) {
    if constexpr (sizeof(wchar_t) == 2) {
        std::uint32_t codePoint = static_cast<std::uint16_t>(input[index++]);
        if (codePoint >= 0xd800U && codePoint <= 0xdbffU) {
            const std::uint32_t low = static_cast<std::uint16_t>(input[index]);
            if (low >= 0xdc00U && low <= 0xdfffU) {
                ++index;
                return 0x10000U + ((codePoint - 0xd800U) << 10U) + (low - 0xdc00U);
            }
            return 0xfffdU;
        }
        if (codePoint >= 0xdc00U && codePoint <= 0xdfffU) {
            return 0xfffdU;
        }
        return codePoint;
    } else {
        std::uint32_t codePoint = static_cast<std::uint32_t>(input[index++]);
        if (codePoint > 0x10ffffU || (codePoint >= 0xd800U && codePoint <= 0xdfffU)) {
            return 0xfffdU;
        }
        return codePoint;
    }
}

std::size_t TargetUtf16Units(const wchar_t* input) {
    if (input == nullptr) {
        return 0;
    }
    std::size_t units = 0;
    std::size_t index = 0;
    while (input[index] != L'\0') {
        const std::uint32_t codePoint = NextWideCodePoint(input, index);
        units += codePoint > 0xffffU ? 2 : 1;
    }
    return units;
}

std::string WideToUtf8(const wchar_t* input, std::size_t maximumTargetUnits = std::numeric_limits<std::size_t>::max()) {
    if (input == nullptr) {
        return {};
    }
    std::string result;
    std::size_t usedUnits = 0;
    std::size_t index = 0;
    while (input[index] != L'\0') {
        const std::size_t previous = index;
        const std::uint32_t codePoint = NextWideCodePoint(input, index);
        const std::size_t units = codePoint > 0xffffU ? 2 : 1;
        if (usedUnits + units > maximumTargetUnits) {
            index = previous;
            break;
        }
        AppendUtf8(result, codePoint);
        usedUnits += units;
    }
    return result;
}

std::uint32_t NextUtf8CodePoint(std::string_view input, std::size_t& index) {
    const std::uint8_t lead = static_cast<std::uint8_t>(input[index++]);
    if (lead < 0x80U) {
        return lead;
    }

    int continuationCount = 0;
    std::uint32_t codePoint = 0;
    std::uint32_t minimum = 0;
    if ((lead & 0xe0U) == 0xc0U) {
        continuationCount = 1;
        codePoint = lead & 0x1fU;
        minimum = 0x80U;
    } else if ((lead & 0xf0U) == 0xe0U) {
        continuationCount = 2;
        codePoint = lead & 0x0fU;
        minimum = 0x800U;
    } else if ((lead & 0xf8U) == 0xf0U) {
        continuationCount = 3;
        codePoint = lead & 0x07U;
        minimum = 0x10000U;
    } else {
        return 0xfffdU;
    }

    for (int count = 0; count < continuationCount; ++count) {
        if (index >= input.size()) {
            return 0xfffdU;
        }
        const std::uint8_t next = static_cast<std::uint8_t>(input[index]);
        if ((next & 0xc0U) != 0x80U) {
            return 0xfffdU;
        }
        ++index;
        codePoint = (codePoint << 6U) | (next & 0x3fU);
    }
    if (codePoint < minimum || codePoint > 0x10ffffU || (codePoint >= 0xd800U && codePoint <= 0xdfffU)) {
        return 0xfffdU;
    }
    return codePoint;
}

std::size_t Utf8TargetBytes(std::string_view input) {
    std::size_t units = 0;
    for (std::size_t index = 0; index < input.size();) {
        const std::uint32_t codePoint = NextUtf8CodePoint(input, index);
        units += codePoint > 0xffffU ? 2 : 1;
    }
    return units * 2;
}

template <class Character>
void CopyUtf8ToTargetBuffer(std::string_view input, Character* output, int targetBufferBytes) {
    if (output == nullptr || targetBufferBytes < 2) {
        return;
    }
    const std::size_t capacityUnits = static_cast<std::size_t>(targetBufferBytes) / 2;
    if (capacityUnits == 0) {
        return;
    }

    std::size_t usedUnits = 0;
    std::size_t outputIndex = 0;
    for (std::size_t inputIndex = 0; inputIndex < input.size();) {
        const std::uint32_t codePoint = NextUtf8CodePoint(input, inputIndex);
        const std::size_t units = codePoint > 0xffffU ? 2 : 1;
        if (usedUnits + units >= capacityUnits) {
            break;
        }

        if constexpr (sizeof(Character) == 2) {
            if (codePoint <= 0xffffU) {
                output[outputIndex++] = static_cast<Character>(codePoint);
            } else {
                const std::uint32_t adjusted = codePoint - 0x10000U;
                output[outputIndex++] = static_cast<Character>(0xd800U + (adjusted >> 10U));
                output[outputIndex++] = static_cast<Character>(0xdc00U + (adjusted & 0x3ffU));
            }
        } else {
            output[outputIndex++] = static_cast<Character>(codePoint);
        }
        usedUnits += units;
    }
    output[outputIndex] = Character{};
}

std::string TruncateWideForTargetQuery(const wchar_t* query) { return WideToUtf8(query, 511); }

std::string QuoteIdentifier(std::string value) {
    value = LowerAscii(std::move(value));
    std::string result = "\"";
    for (char character : value) {
        if (character == '"') {
            result.push_back('"');
        }
        result.push_back(character);
    }
    result.push_back('"');
    return result;
}

std::string UnquoteIdentifier(std::string_view value) {
    value = TrimAscii(value);
    if (value.size() >= 2 && value.front() == '[' && value.back() == ']') {
        value.remove_prefix(1);
        value.remove_suffix(1);
    }
    std::string result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == ']' && index + 1 < value.size() && value[index + 1] == ']') {
            ++index;
        }
        result.push_back(value[index]);
    }
    return result;
}

std::vector<std::string> SplitQualifiedName(std::string_view value) {
    std::vector<std::string> result;
    std::size_t start = 0;
    bool bracketed = false;
    for (std::size_t index = 0; index <= value.size(); ++index) {
        if (index < value.size()) {
            if (value[index] == '[') {
                bracketed = true;
            }
            if (value[index] == ']') {
                bracketed = false;
            }
        }
        if (index == value.size() || (value[index] == '.' && !bracketed)) {
            result.push_back(UnquoteIdentifier(value.substr(start, index - start)));
            start = index + 1;
        }
    }
    return result;
}

bool SplitCallArguments(std::string_view value, std::vector<std::string_view>& result) {
    if (TrimAscii(value).empty()) {
        return true;
    }
    std::size_t start = 0;
    int parentheses = 0;
    bool bracketed = false;
    bool quoted = false;
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char character = value[index];
        const char following = index + 1 < value.size() ? value[index + 1] : '\0';
        if (quoted) {
            if (character == '\'' && following == '\'') {
                ++index;
            } else if (character == '\'') {
                quoted = false;
            }
            continue;
        }
        if (bracketed) {
            if (character == ']' && following == ']') {
                ++index;
            } else if (character == ']') {
                bracketed = false;
            }
            continue;
        }
        if (character == '\'') {
            quoted = true;
        } else if (character == '[') {
            bracketed = true;
        } else if (character == '(') {
            ++parentheses;
        } else if (character == ')') {
            if (parentheses == 0) {
                return false;
            }
            --parentheses;
        } else if (character == ',' && parentheses == 0) {
            const std::string_view argument = TrimAscii(value.substr(start, index - start));
            if (argument.empty()) {
                return false;
            }
            result.push_back(argument);
            start = index + 1;
        }
    }
    if (quoted || bracketed || parentheses != 0) {
        return false;
    }
    const std::string_view argument = TrimAscii(value.substr(start));
    if (argument.empty()) {
        return false;
    }
    result.push_back(argument);
    return true;
}

bool IsIntegerLiteral(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    if (value.front() == '+' || value.front() == '-') {
        value.remove_prefix(1);
    }
    if (value.empty()) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](char character) {
        return std::isdigit(static_cast<unsigned char>(character)) != 0;
    });
}

bool TranslateCallLiteral(std::string_view value, std::string& translated) {
    value = TrimAscii(value);
    if (StartsWithAsciiInsensitive(value, "null") && value.size() == 4) {
        translated = "NULL";
        return true;
    }
    if (IsIntegerLiteral(value)) {
        translated.assign(value);
        return true;
    }

    // T-SQL and PostgreSQL share doubled-quote escaping for ordinary string
    // literals. The optional N prefix only selects Unicode in MSSQL. The
    // PostgreSQL connection is UTF-8 already.
    if (value.size() >= 2 && (value.front() == 'N' || value.front() == 'n') && value[1] == '\'') {
        value.remove_prefix(1);
    }
    if (value.size() < 2 || value.front() != '\'' || value.back() != '\'') {
        return false;
    }
    for (std::size_t index = 1; index + 1 < value.size(); ++index) {
        if (value[index] != '\'') {
            continue;
        }
        if (index + 1 >= value.size() - 1 || value[index + 1] != '\'') {
            return false;
        }
        ++index;
    }
    translated.assign(value);
    return true;
}

enum class InputKind {
    Null,
    Boolean,
    Int16,
    Int32,
    UInt32,
    Int64,
    UInt64,
    Double,
    Int32AsDouble,
    UInt32AsDouble,
    NarrowText,
    WideText,
    WideLongText,
    Utf16Text,
    Enum32,
    Binary,
};

enum class ParameterSqlType {
    Boolean,
    SmallInt,
    Integer,
    BigInt,
    Double,
    Text,
    Binary,
};

struct InputBinding {
    InputKind kind = InputKind::Null;
    ParameterSqlType sqlType = ParameterSqlType::Text;
    const void* value = nullptr;
    std::size_t boundSize = 0;
    const std::int64_t* lengthOrIndicator = nullptr;
    int diagnosticTag = -1;
};

const char* PostgreSQLCast(ParameterSqlType type) {
    switch (type) {
        case ParameterSqlType::Boolean:
            return "boolean";
        case ParameterSqlType::SmallInt:
            return "smallint";
        case ParameterSqlType::Integer:
            return "integer";
        case ParameterSqlType::BigInt:
            return "bigint";
        case ParameterSqlType::Double:
            return "double precision";
        case ParameterSqlType::Text:
            return "text";
        case ParameterSqlType::Binary:
            return "bytea";
    }
    return "text";
}

struct TranslatedQuery {
    std::string sql;
    bool odbcProcedureCall = false;
    std::size_t parameterCount = 0;
};

TranslatedQuery TranslateQuery(
    const wchar_t* wideQuery,
    const std::vector<InputBinding>& inputs,
    const database::PostgreSQLRuntimeConfig& config,
    const std::string& poolDefaultSchema
) {
    std::string original = WideToUtf8(wideQuery);
    std::string_view query = TrimAscii(original);
    if (query.size() < 4 || query.front() != '{' || query.back() != '}') {
        return {std::string(query), false};
    }

    query.remove_prefix(1);
    query.remove_suffix(1);
    query = TrimAscii(query);
    if (!StartsWithAsciiInsensitive(query, "call")) {
        return {std::string(TrimAscii(original)), false};
    }
    query.remove_prefix(4);
    query = TrimAscii(query);

    const std::size_t open = query.find('(');
    std::string_view qualified = TrimAscii(open == std::string_view::npos ? query : query.substr(0, open));
    std::string_view arguments;
    if (open != std::string_view::npos) {
        const std::size_t close = query.rfind(')');
        if (close == std::string_view::npos || close < open) {
            return {std::string(TrimAscii(original)), false};
        }
        arguments = query.substr(open + 1, close - open - 1);
    }

    const std::vector<std::string> components = SplitQualifiedName(qualified);
    if (components.empty() || components.back().empty()) {
        return {std::string(TrimAscii(original)), false};
    }

    std::string schema = poolDefaultSchema.empty() ? config.defaultSchema : poolDefaultSchema;
    if (components.size() == 2) {
        if (LowerAscii(components[0]) != "dbo") {
            schema = components[0];
        }
    } else if (components.size() >= 3) {
        const std::string database = LowerAscii(components[components.size() - 3]);
        const auto mapped = config.databaseSchemas.find(database);
        schema = mapped == config.databaseSchemas.end() ? database : mapped->second;
    }

    std::vector<std::string_view> callArguments;
    if (!SplitCallArguments(arguments, callArguments)) {
        return {std::string(TrimAscii(original)), false};
    }

    std::string translated = "SELECT * FROM ";
    translated += QuoteIdentifier(schema);
    translated.push_back('.');
    translated += QuoteIdentifier(components.back());
    translated.push_back('(');
    std::size_t placeholderIndex = 0;
    for (std::size_t index = 0; index < callArguments.size(); ++index) {
        if (index != 0) {
            translated += ", ";
        }
        if (callArguments[index] == "?") {
            translated.push_back('$');
            translated += std::to_string(placeholderIndex + 1);
            if (placeholderIndex < inputs.size()) {
                translated += "::";
                translated += PostgreSQLCast(inputs[placeholderIndex].sqlType);
            }
            ++placeholderIndex;
        } else {
            std::string literal;
            if (!TranslateCallLiteral(callArguments[index], literal)) {
                return {std::string(TrimAscii(original)), false};
            }
            translated += literal;
        }
    }
    translated.push_back(')');
    return {std::move(translated), true, placeholderIndex};
}

// Keep the driver's error message, details, hint and SQL context.
void LogDatabaseDiagnostic(
    int databaseId,
    const char* operation,
    PGconn* connection,
    PGresult* result,
    int statusCode,
    const char* statementKind = "none",
    std::string_view policyState = {}
) {
    const char* action = "Database operation";
    if (std::strcmp(operation, "connect") == 0) {
        action = "Connection";
    } else if (std::strcmp(operation, "set_client_encoding") == 0) {
        action = "Setting the client encoding";
    } else if (std::strcmp(operation, "set_isolation") == 0) {
        action = "Setting transaction isolation";
    } else if (std::strcmp(operation, "set_statement_timeout") == 0) {
        action = "Setting the statement timeout";
    } else if (std::strcmp(operation, "fetch") == 0) {
        action = "Fetch";
    } else if (std::strcmp(operation, "query") == 0) {
        action = "Query";
    }
    const int savedErrno = errno;
    const char* rawState = result == nullptr ? nullptr : PQresultErrorField(result, PG_DIAG_SQLSTATE);
    const std::string_view state = rawState == nullptr ? std::string_view{} : rawState;
    const bool validState = runtime::logging::IsSqlState(state);
    const bool validPolicy = runtime::logging::IsSqlState(policyState);
    const char* reason = validState ? runtime::logging::SqlStateReason(state)
        : std::strcmp(operation, "connect") == 0
        ? (connection == nullptr ? "allocation_failed" : "connection_establishment_failed")
        : connection == nullptr                 ? "connection_unavailable"
        : PQstatus(connection) != CONNECTION_OK ? "connection_unavailable"
                                                : "diagnostic_unavailable";
    char resultStatus[16] = "unavailable";
    if (statusCode >= 0) {
        std::snprintf(resultStatus, sizeof(resultStatus), "%d", statusCode);
    }
    unsigned long long position = 0;
    const char* rawPosition = result == nullptr ? nullptr : PQresultErrorField(result, PG_DIAG_STATEMENT_POSITION);
    if (rawPosition != nullptr) {
        const char* end = rawPosition + std::strlen(rawPosition);
        const auto parsed = std::from_chars(rawPosition, end, position);
        if (parsed.ec != std::errc{} || parsed.ptr != end) {
            position = 0;
        }
    }
    // Log.Add(LOG_ERROR,
    //     "component=database event=operation_failed database_id=%d operation=%s statement_kind=%s "
    //     "result_status=%s sqlstate=%.5s policy_sqlstate=%.5s reason=%s statement_position=%llu details=redacted",
    //     databaseId, operation, statementKind, resultStatus,
    //     validState ? state.data() : "none", validPolicy ? policyState.data() : "none",
    //     reason, position)
    // Log.Add(LOG_ERROR,
    //     "[database %d] %s failed for a %s statement. "
    //     "Result %s. SQLSTATE %.5s, policy SQLSTATE %.5s (%s). Statement position %llu.",
    //     databaseId, action, statementKind, resultStatus,
    //     validState ? state.data() : "none", validPolicy ? policyState.data() : "none",
    //     reason, position)
    // L2_LOG_ERROR(Log,
    //     "[database %d] %s failed for a %s statement. "
    //     "Result %s. SQLSTATE %.5s, policy SQLSTATE %.5s (%s). Statement position %llu.",
    //     databaseId, action, statementKind, resultStatus,
    //     validState ? state.data() : "none", validPolicy ? policyState.data() : "none",
    //     reason, position)
    // Only our classification label is humanized. Never transform SQL or
    // PostgreSQL's message/detail/context fields.
    std::string readableReason(reason);
    std::replace(readableReason.begin(), readableReason.end(), '_', ' ');
    L2_LOG_ERROR(
        Log,
        "[database %d] %s failed: %s. "
        "Statement type: %s; result %s; SQLSTATE %.5s; policy SQLSTATE %.5s; statement position %llu.",
        databaseId, action, readableReason.c_str(), statementKind, resultStatus, validState ? state.data() : "none",
        validPolicy ? policyState.data() : "none", position
    );
    LogPostgreSQLDetails(LOG_ERROR, databaseId, result);
    errno = savedErrno;
}

void ReceiveDatabaseNotice(void* argument, const PGresult* result) noexcept {
    const int savedErrno = errno;
    const int databaseId = *static_cast<const int*>(argument);
    const auto record = runtime::logging::FormatPostgreSQLNotice(result);
    try {
        // Log.Add(record.error ? LOG_ERROR : record.warning ? LOG_WARN : LOG_NORMAL,
        //     "component=database event=server_notice database_id=%d %s", databaseId, record.text.data())
        // Log.Add(record.error ? LOG_ERROR : record.warning ? LOG_WARN : LOG_NORMAL,
        //     "[database %d] Server notice: %s", databaseId, record.text.data())
        // L2_LOG(Log, record.error ? LOG_ERROR : record.warning ? LOG_WARN : LOG_NORMAL,
        //     "[database %d] Server notice: %s", databaseId, record.text.data())
        const auto type = record.error ? LOG_ERROR : record.warning ? LOG_WARN : record.debug ? LOG_DEBUG : LOG_NORMAL;
        L2_LOG(Log, type, "[database %d] Server notice: %s", databaseId, record.text.data());
        // LogPostgreSQLDetails(record.error ? LOG_ERROR : record.warning ? LOG_WARN : LOG_NORMAL, databaseId, result)
        LogPostgreSQLDetails(type, databaseId, result);
    } catch (...) {
        // A C++ sink failure must not unwind through libpq's C callback.
#if defined(L2_BUILD_CACHED)
        constexpr const char* application = "cached";
#elif defined(L2_BUILD_L2SERVER)
        constexpr const char* application = "l2server";
#else
        constexpr const char* application = "l2npc";
#endif
        // runtime::logging::Write(application, runtime::logging::Level::Error,
        //     "component=database event=notice_sink_failed details=redacted")
        // runtime::logging::Write(application, runtime::logging::Level::Error,
        //     "[database] Could not write a server notice: the log sink threw an exception.")
        runtime::logging::Write(
            application, runtime::logging::Level::Error,
            "[database] Could not write a server notice: the log sink threw an exception.", __FILE__
        );
    }
    errno = savedErrno;
}

// Current stripped DBConn.cpp connection helper 0x004a69c0 (original symbol
// absent), 424 bytes, SHA-256
// c1d54425c639a73b9e95c041508edca133067c2d485507fb56ce75020f793648.
// It creates the ODBC connection/statement pair. This is the matching
// PostgreSQL transport boundary. The caller-visible success/failure contract
// is kept.
// PGconn* ConnectPostgreSQL(
//     const std::string& connectionString,
//     std::uint32_t statementTimeoutMilliseconds,
//     bool emitDiagnostic) {
PGconn* ConnectPostgreSQL(
    const std::string& connectionString,
    std::uint32_t statementTimeoutMilliseconds,
    // bool emitDiagnostic, int databaseId) {
    bool emitDiagnostic,
    int& databaseId
) {
    if (connectionString.empty()) {
        return nullptr;
    }
    const std::string connectTimeout = std::to_string(kConnectionTimeoutSeconds);
    const char* keywords[] = {"dbname", "connect_timeout", nullptr};
    const char* values[] = {
        connectionString.c_str(),
        connectTimeout.c_str(),
        nullptr,
    };
    PGconn* connection = PQconnectdbParams(keywords, values, 1);
    if (connection == nullptr || PQstatus(connection) != CONNECTION_OK) {
        if (emitDiagnostic) {
            // Sensitive data: driver messages may echo connection options or SQL values.
            // Log.Add(
            //     LOG_ERROR,
            //     "[DBConnection] PostgreSQL connection failed: %s",
            //     connection == nullptr ? "allocation failure" : PQerrorMessage(connection))
            LogDatabaseDiagnostic(databaseId, "connect", connection, nullptr, -1);
        }
        if (connection != nullptr) {
            PQfinish(connection);
        }
        return nullptr;
    }

    // Replace libpq's raw stderr notice output. Every caller passes SqlPool's
    // stable databaseId field, which outlives its connections and their results:
    // DBConn clears results before returning the entry. Shutdown refuses live
    // checkouts and closes connections before destroying the pool map.
    PQsetNoticeReceiver(connection, ReceiveDatabaseNotice, &databaseId);
    if (PQsetClientEncoding(connection, "UTF8") != 0) {
        if (emitDiagnostic) {
            // Sensitive data: driver messages may echo connection options or SQL values.
            // Log.Add(
            //     LOG_ERROR,
            //     "[DBConnection] cannot select UTF8 client encoding: %s",
            //     PQerrorMessage(connection))
            LogDatabaseDiagnostic(databaseId, "set_client_encoding", connection, nullptr, -1);
        }
        PQfinish(connection);
        return nullptr;
    }

    // Cached.exe never changes ODBC transaction isolation, so a new SQL
    // Server session runs at the engine default READ COMMITTED level. Pin the
    // portable session to that same level instead of inheriting a PostgreSQL
    // role/database override. This is transport replacement setup, not a
    // recovered extra query owned by CacheD.
    {
        PGresult* result = PQexec(
            connection,
            "SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL "
            "READ COMMITTED"
        );
        const bool success = result != nullptr && PQresultStatus(result) == PGRES_COMMAND_OK;
        if (!success && emitDiagnostic) {
            // Sensitive data: driver messages may echo connection options or SQL values.
            // Log.Add(
            //     LOG_ERROR,
            //     "[DBConnection] cannot select READ COMMITTED isolation: %s",
            //     result == nullptr ? PQerrorMessage(connection)
            //                       : PQresultErrorMessage(result))
            LogDatabaseDiagnostic(databaseId, "set_isolation", connection, result, static_cast<int>(PQresultStatus(result)));
        }
        if (result != nullptr) {
            PQclear(result);
        }
        if (!success) {
            PQfinish(connection);
            return nullptr;
        }
    }

    if (statementTimeoutMilliseconds != 0) {
        const std::string statement = "SET statement_timeout TO " + std::to_string(statementTimeoutMilliseconds);
        PGresult* result = PQexec(connection, statement.c_str());
        const bool success = result != nullptr && PQresultStatus(result) == PGRES_COMMAND_OK;
        if (!success && emitDiagnostic) {
            // Sensitive data: driver messages may echo connection options or SQL values.
            // Log.Add(
            //     LOG_ERROR,
            //     "[DBConnection] cannot apply portable statement timeout: %s",
            //     result == nullptr ? PQerrorMessage(connection)
            //                       : PQresultErrorMessage(result))
            LogDatabaseDiagnostic(
                databaseId, "set_statement_timeout", connection, result, static_cast<int>(PQresultStatus(result))
            );
        }
        if (result != nullptr) {
            PQclear(result);
        }
        if (!success) {
            PQfinish(connection);
            return nullptr;
        }
    }
    return connection;
}

// Current stripped DBConn.cpp health probe 0x004a3e48 (original symbol
// absent), 249 bytes, SHA-256
// bb9bcdb9107cbdf0eab97153d22269c0606f6e7d4c88a3eb80fcc2ee307e58fc.
// The target executes the same "select 0" statement and closes its cursor.
bool CheckConnection(PGconn* connection) {
    if (connection == nullptr || PQstatus(connection) != CONNECTION_OK) {
        return false;
    }
    PGresult* result = PQexec(connection, "select 0");
    const bool success = result != nullptr && PQresultStatus(result) == PGRES_TUPLES_OK;
    if (result != nullptr) {
        PQclear(result);
    }
    return success;
}

// Current stripped 0x004a4018 constructs the target's 0x260-byte
// per-database pool owner before CSqlPoolManager::InitPoolMap inserts it. Its
// exact SHA-256 is
// 59d9aa95c5ce94309cf5144088168b0b901e11c0c2a29c93bec3415d485b46e8.
// The body zeroes the intrusive resource state, constructs its critical
// section, copies the 256-unit connection string and publishes the requested
// pool count. This host owner keeps those states in typed libpq containers.
// only the ODBC/intrusive transport and physical layout are replaced.
struct SqlPool {
    int databaseId = 0;
    std::string connectionString;
    std::string defaultSchema;
    std::size_t requestedCount = 0;
    std::vector<std::unique_ptr<_SQLPool_>> entries;
    std::list<_SQLPool_*> freeEntries;
    std::mutex mutex;
    std::condition_variable available;
    std::size_t checkedOut = 0;
    std::array<std::uint32_t, 5> checkoutDurations{};
    std::size_t durationCursor = 0;

    // CacheD's fatal reporter reads these fields without taking the pool
    // critical section. Atomic mirrors keep the same lock-free crash path
    // well-defined while the PostgreSQL transport keeps STL containers.
    std::atomic<int> crashUnhandledCount{0};
    std::atomic<int> crashRemainingCount{0};
    std::array<std::atomic<std::uint32_t>, 5> crashCheckoutDurations{};
};

enum class OutputKind {
    WideText,
    Utf16Text,
    NarrowText,
    UInt32,
    Int32,
    Int64,
    Int16,
    Double,
    Binary,
};

struct OutputBinding {
    OutputKind kind = OutputKind::Int32;
    void* value = nullptr;
    int capacity = 0;
    std::int64_t* lengthOrIndicator = nullptr;
};

struct MaterializedParameter {
    bool isNull = false;
    bool binaryFormat = false;
    std::string text;
    std::vector<unsigned char> binary;
};

template <class Value>
std::string IntegerText(Value value) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << value;
    return stream.str();
}

std::string DoubleText(double value) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return stream.str();
}

MaterializedParameter Materialize(const InputBinding& binding) {
    MaterializedParameter result;
    if (binding.kind == InputKind::Null || binding.value == nullptr) {
        result.isNull = true;
        return result;
    }
    switch (binding.kind) {
        case InputKind::Null:
            result.isNull = true;
            break;
        case InputKind::Boolean:
            result.text = *static_cast<const bool*>(binding.value) ? "true" : "false";
            break;
        case InputKind::Int16:
            result.text = IntegerText(*static_cast<const short*>(binding.value));
            break;
        case InputKind::Int32:
            result.text = IntegerText(*static_cast<const int*>(binding.value));
            break;
        case InputKind::UInt32:
            result.text = IntegerText(*static_cast<const unsigned int*>(binding.value));
            break;
        case InputKind::Int64:
            result.text = IntegerText(*static_cast<const long long*>(binding.value));
            break;
        case InputKind::UInt64:
            result.text = IntegerText(*static_cast<const unsigned long long*>(binding.value));
            break;
        case InputKind::Double:
            result.text = DoubleText(*static_cast<const double*>(binding.value));
            break;
        case InputKind::Int32AsDouble: {
            // The target overload takes Win64 long (four bytes). Read the
            // host long as a value before narrowing so LP64 and big-endian
            // hosts do not depend on the address of its low four bytes.
            const std::int32_t value = static_cast<std::int32_t>(*static_cast<const long*>(binding.value));
            result.text = IntegerText(value);
            break;
        }
        case InputKind::UInt32AsDouble: {
            const std::uint32_t value = static_cast<std::uint32_t>(*static_cast<const unsigned long*>(binding.value));
            result.text = IntegerText(value);
            break;
        }
        case InputKind::NarrowText: {
            const char* text = static_cast<const char*>(binding.value);
            result.text.assign(text, std::min(binding.boundSize, std::strlen(text)));
            break;
        }
        case InputKind::Utf16Text:
            result.text = ::Utf16ToUtf8(std::u16string_view(static_cast<const char16_t*>(binding.value)), binding.boundSize);
            break;
        case InputKind::WideText:
        case InputKind::WideLongText:
            result.text = WideToUtf8(static_cast<const wchar_t*>(binding.value), binding.boundSize);
            break;
        case InputKind::Enum32: {
            std::int32_t value = 0;
            std::memcpy(&value, binding.value, sizeof(value));
            result.text = IntegerText(value);
            break;
        }
        case InputKind::Binary: {
            std::size_t size = binding.boundSize;
            if (binding.lengthOrIndicator != nullptr) {
                if (*binding.lengthOrIndicator == kNullData) {
                    result.isNull = true;
                    break;
                }
                if (*binding.lengthOrIndicator >= 0) {
                    size = std::min(size, static_cast<std::size_t>(*binding.lengthOrIndicator));
                }
            }
            const auto* bytes = static_cast<const unsigned char*>(binding.value);
            result.binary.assign(bytes, bytes + size);
            result.binaryFormat = true;
            break;
        }
    }
    return result;
}

void SetIndicator(const OutputBinding& binding, std::int64_t value) {
    if (binding.lengthOrIndicator != nullptr) {
        *binding.lengthOrIndicator = value;
    }
}

void CopyNarrowOutput(const OutputBinding& binding, std::string_view value) {
    SetIndicator(binding, static_cast<std::int64_t>(value.size()));
    if (binding.value == nullptr || binding.capacity <= 0) {
        return;
    }
    const std::size_t count = std::min(value.size(), static_cast<std::size_t>(binding.capacity - 1));
    std::memcpy(binding.value, value.data(), count);
    static_cast<char*>(binding.value)[count] = '\0';
}

void CopyBinaryOutput(const OutputBinding& binding, const char* encoded) {
    std::size_t decodedSize = 0;
    unsigned char* decoded = PQunescapeBytea(reinterpret_cast<const unsigned char*>(encoded), &decodedSize);
    if (decoded == nullptr) {
        SetIndicator(binding, 0);
        return;
    }
    SetIndicator(binding, static_cast<std::int64_t>(decodedSize));
    if (binding.value != nullptr && binding.capacity > 0) {
        const std::size_t count = std::min(decodedSize, static_cast<std::size_t>(binding.capacity));
        std::memcpy(binding.value, decoded, count);
    }
    PQfreemem(decoded);
}

void ApplyOutput(const OutputBinding& binding, bool isNull, const char* value) {
    if (isNull) {
        SetIndicator(binding, kNullData);
        return;
    }
    const std::string_view text(value == nullptr ? "" : value);
    switch (binding.kind) {
        case OutputKind::WideText:
            SetIndicator(binding, static_cast<std::int64_t>(Utf8TargetBytes(text)));
            CopyUtf8ToTargetBuffer(text, static_cast<wchar_t*>(binding.value), binding.capacity);
            break;
        case OutputKind::Utf16Text:
            SetIndicator(binding, static_cast<std::int64_t>(Utf8TargetBytes(text)));
            CopyUtf8ToTargetBuffer(text, static_cast<char16_t*>(binding.value), binding.capacity);
            break;
        case OutputKind::NarrowText:
            CopyNarrowOutput(binding, text);
            break;
        case OutputKind::UInt32:
            if (binding.value != nullptr) {
                *static_cast<unsigned int*>(binding.value) =
                    static_cast<unsigned int>(std::strtoull(text.data(), nullptr, 10));
            }
            SetIndicator(binding, 4);
            break;
        case OutputKind::Int32:
            if (binding.value != nullptr) {
                *static_cast<int*>(binding.value) = static_cast<int>(std::strtoll(text.data(), nullptr, 10));
            }
            SetIndicator(binding, 4);
            break;
        case OutputKind::Int64:
            if (binding.value != nullptr) {
                // The bound object is long long or an eight-byte long.
                const long long parsed = std::strtoll(text.data(), nullptr, 10);
                std::memcpy(binding.value, &parsed, sizeof(parsed));
            }
            SetIndicator(binding, 8);
            break;
        case OutputKind::Int16:
            if (binding.value != nullptr) {
                *static_cast<short*>(binding.value) = static_cast<short>(std::strtol(text.data(), nullptr, 10));
            }
            SetIndicator(binding, 2);
            break;
        case OutputKind::Double:
            if (binding.value != nullptr) {
                *static_cast<double*>(binding.value) = std::strtod(text.data(), nullptr);
            }
            SetIndicator(binding, 8);
            break;
        case OutputKind::Binary:
            CopyBinaryOutput(binding, value == nullptr ? "" : value);
            break;
    }
}

bool PostgreSQLSuccess(ExecStatusType status) { return status == PGRES_TUPLES_OK || status == PGRES_COMMAND_OK; }

std::string ResultErrorMessage(PGconn* connection, PGresult* result) {
    const char* message =
        result == nullptr ? (connection == nullptr ? nullptr : PQerrorMessage(connection)) : PQresultErrorMessage(result);
    return message == nullptr ? std::string() : std::string(message);
}

std::string ResultSqlState(PGresult* result) {
    if (result == nullptr) {
        return {};
    }
    const char* state = PQresultErrorField(result, PG_DIAG_SQLSTATE);
    return state == nullptr ? std::string() : std::string(state);
}

std::mutex g_missingProcedureAlertMutex;
std::uint32_t g_lastMissingProcedureAlertTick = 0;

} // namespace

struct _SQLPool_ {
    PGconn* connection = nullptr;
    std::uint32_t checkoutTick = 0;
    std::string defaultSchema;
};

struct CSqlPoolManager::Impl {
    mutable std::mutex mutex;
    database::PostgreSQLRuntimeConfig config;
    std::map<int, std::unique_ptr<SqlPool>> pools;
    bool configured = false;
    bool initialized = false;
    bool ready = false;
};

CSqlPoolManager::CSqlPoolManager() : m_impl(std::make_unique<Impl>()) {
    L2_GUARD_FUNCTION();
}

CSqlPoolManager::~CSqlPoolManager() {
    // Current Cached.exe 0x004a6340 destroys every per-database pool owner,
    // releases its environment handle and DBList storage, then tears down
    // the ordered pool map. Shutdown is the PostgreSQL transport form of
    // that same ownership boundary.
    L2_GUARD_FUNCTION();
    Shutdown();
}

CSqlPoolManager& CSqlPoolManager::Instance() {
    static CSqlPoolManager manager;
    return manager;
}

CSqlPoolManager& GetSqlPoolManager() { return CSqlPoolManager::Instance(); }

bool CSqlPoolManager::ConfigurePostgreSQL(database::PostgreSQLRuntimeConfig config) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (m_impl->initialized || !m_impl->pools.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[DBConnection] configuration cannot change after Init")
        // Log.Add(LOG_ERROR, "component=database event=config_rejected reason=already_initialized action=restart_required")
        // Log.Add(LOG_ERROR, "[database] Cannot change configuration after initialization. Restart is required.")
        L2_LOG_ERROR(Log, "[database] Cannot change configuration after initialization. Restart is required.");
        return false;
    }
    if (config.worldId == 0 || config.databases.empty() || config.defaultSchema.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[DBConnection] invalid empty DBList/world/schema configuration")
        // Log.Add(LOG_ERROR, "component=database event=config_rejected reason=invalid_world_or_empty_database_list_or_schema
        // world_id=%d database_count=%zu schema_empty=%d",
        //     config.worldId, config.databases.size(), static_cast<int>(config.defaultSchema.empty()))
        // Log.Add(LOG_ERROR, "[database] Invalid configuration: world ID %d, %zu database entries, schema empty: %d. Check
        // WorldId, DBList and the default schema.",
        //     config.worldId, config.databases.size(), static_cast<int>(config.defaultSchema.empty()))
        L2_LOG_ERROR(
            Log,
            "[database] Invalid configuration: world ID %d, %zu database entries, schema empty: %d. Check WorldId, DBList "
            "and the default schema.",
            config.worldId, config.databases.size(), static_cast<int>(config.defaultSchema.empty())
        );
        return false;
    }

    bool containsWorld = false;
    for (const database::PostgreSQLPoolConfig& database : config.databases) {
        if (database.databaseId == 0 || database.connectionString.empty()) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[DBConnection] invalid database id/configuration %d",
            //     database.databaseId)
            // Log.Add(LOG_ERROR, "component=database event=config_rejected reason=invalid_database_id_or_empty_connection
            // database_id=%d connection_empty=%d",
            //     database.databaseId, static_cast<int>(database.connectionString.empty()))
            // Log.Add(LOG_ERROR, "[database %d] Invalid configuration: database ID must be nonzero and the connection string
            // must be present (connection string empty: %d).",
            //     database.databaseId, static_cast<int>(database.connectionString.empty()))
            L2_LOG_ERROR(
                Log,
                "[database %d] Invalid configuration: database ID must be nonzero and the connection string must be present "
                "(connection string empty: %d).",
                database.databaseId, static_cast<int>(database.connectionString.empty())
            );
            return false;
        }
        // Current CSqlPoolManager::CSqlPoolManager (0x004a64ec) keeps
        // duplicate DBList tokens. InitPoolMap (0x004a6bf8) inserts them into
        // a unique ordered map through 0x004d685c, where the first mapped
        // value wins. Keep duplicates here. InitPoolMap::emplace below gives
        // the portable owner the same observable first-wins behavior without
        // reproducing the target's discarded allocation.
        containsWorld = containsWorld || database.databaseId == config.worldId;
    }
    if (!containsWorld) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[DBConnection] DBList does not contain WorldId %d",
        //     config.worldId)
        // Log.Add(LOG_ERROR, "component=database event=config_rejected reason=world_missing_from_database_list world_id=%d",
        //     config.worldId)
        // Log.Add(LOG_ERROR, "[database] DBList does not contain WorldId %d.",
        //     config.worldId)
        L2_LOG_ERROR(Log, "[database] DBList does not contain WorldId %d.", config.worldId);
        return false;
    }

    std::map<std::string, std::string> normalizedSchemas;
    for (auto& [database, schema] : config.databaseSchemas) {
        if (!database.empty() && !schema.empty()) {
            normalizedSchemas[LowerAscii(database)] = std::move(schema);
        }
    }
    config.databaseSchemas = std::move(normalizedSchemas);
    m_impl->config = std::move(config);
    m_impl->configured = true;
    return true;
}

void CSqlPoolManager::InitPoolMap() {
    L2_GUARD_FUNCTION();
    m_impl->pools.clear();
    for (const database::PostgreSQLPoolConfig& database : m_impl->config.databases) {
        auto pool = std::make_unique<SqlPool>();
        pool->databaseId = database.databaseId;
        pool->connectionString = database.connectionString;
        pool->defaultSchema = database.defaultSchema.empty() ? m_impl->config.defaultSchema : database.defaultSchema;
        pool->requestedCount = database.poolSizeOverride == 0 ? static_cast<std::size_t>(GetDBConnNum(database.databaseId))
                                                              : database.poolSizeOverride;
        m_impl->pools.emplace(database.databaseId, std::move(pool));
    }
}

bool CSqlPoolManager::Login() {
    L2_GUARD_FUNCTION();
    for (const auto& [databaseId, pool] : m_impl->pools) {
        // PGconn* probe = ConnectPostgreSQL(
        //     pool->connectionString,
        //     m_impl->config.statementTimeoutMilliseconds,
        //     true)
        PGconn* probe =
            ConnectPostgreSQL(pool->connectionString, m_impl->config.statementTimeoutMilliseconds, true, pool->databaseId);
        if (probe == nullptr) {
            // Noise: the connection helper already reports the failed operation and database ID.
            // Log.Add(
            //     LOG_ERROR,
            //     "[DBConnection] login failed for World[%d]",
            //     databaseId)

            return false;
        }
        PQfinish(probe);
    }
    return true;
}

void CSqlPoolManager::AllocateSQLPool() {
    // Current stripped per-resource allocator 0x004a718c (original symbol
    // absent), 167 bytes, SHA-256
    // 65db0b6aca6bd9eb79fb8e94d378363229c96a1e570770fae684444e64f9627a.
    L2_GUARD_FUNCTION();
    // Its connection helper is 0x004a69c0 above. After allocation, current
    // helper 0x004a3d6c (70 bytes, SHA-256
    // a80a71e78e129060395b6dd5fb2467d90b012feb6cf70cc16433493dea845bbe)
    // links only successfully connected entries into the free list.
    bool allUsable = true;
    for (auto& [databaseId, pool] : m_impl->pools) {
        pool->entries.reserve(pool->requestedCount);
        for (std::size_t index = 0; index < pool->requestedCount; ++index) {
            auto entry = std::make_unique<_SQLPool_>();
            entry->checkoutTick = runtime::GetTickCount();
            entry->defaultSchema = pool->defaultSchema;
            // entry->connection = ConnectPostgreSQL(
            //     pool->connectionString,
            //     m_impl->config.statementTimeoutMilliseconds,
            //     true)
            entry->connection = ConnectPostgreSQL(
                pool->connectionString, m_impl->config.statementTimeoutMilliseconds, true, pool->databaseId
            );
            if (entry->connection != nullptr) {
                pool->freeEntries.push_front(entry.get());
            }
            pool->entries.push_back(std::move(entry));
        }
        const std::size_t connected = pool->freeEntries.size();
        pool->crashRemainingCount.store(static_cast<int>(connected), std::memory_order_relaxed);
        pool->crashUnhandledCount.store(0, std::memory_order_relaxed);
        allUsable = allUsable && connected > 1;
        // Log.Add(
        //     connected > 1 ? LOG_NORMAL : LOG_ERROR,
        //     "[DBConnection] World[%d] connected %zu/%zu, usable %zu",
        //     databaseId,
        //     connected,
        //     pool->requestedCount,
        //     connected == 0 ? 0 : connected - 1)
        // Log.Add(connected <= 1 ? LOG_ERROR : connected < pool->requestedCount ? LOG_WARN : LOG_NORMAL,
        //     "component=database event=pool_initialized database_id=%d connected=%zu requested=%zu usable=%zu ready=%d",
        //     databaseId, connected, pool->requestedCount,
        //     connected == 0 ? 0 : connected - 1, static_cast<int>(connected > 1))
        // Log.Add(connected <= 1 ? LOG_ERROR : connected < pool->requestedCount ? LOG_WARN : LOG_NORMAL,
        //     "[database %d] Pool initialization: %zu of %zu connections established, %zu usable. Ready: %d.",
        //     databaseId, connected, pool->requestedCount,
        //     connected == 0 ? 0 : connected - 1, static_cast<int>(connected > 1))
        // Portability improvement: name the configured schema, retaining the
        // routing ID and the original last-connection reserve policy.
        L2_LOG(
            Log,
            connected <= 1                         ? LOG_ERROR
                : connected < pool->requestedCount ? LOG_WARN
                                                   : LOG_NORMAL,
            "[database %s, pool %d] Pool initialization: %zu of %zu connections established, %zu usable, %zu reserved; "
            "ready: %s.",
            pool->defaultSchema.c_str(), databaseId, connected, pool->requestedCount, connected == 0 ? 0 : connected - 1,
            connected == 0 ? std::size_t{0} : std::size_t{1}, connected > 1 ? "yes" : "no"
        );
    }
    m_impl->ready = allUsable;
}

void CSqlPoolManager::Init() {
    // Current Cached.exe 0x004a77e8, 344 bytes, SHA-256
    // d19072e3efc93abefc287dcce310994ea2c2abd74e56b7f36a7abfbb6656a7b6.
    L2_GUARD_FUNCTION();
    // ODBC environment allocation/version selection is replaced by the
    // configured libpq transport. Login-before-AllocateSQLPool is kept.
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (m_impl->initialized) {
        return;
    }
    m_impl->initialized = true;
    m_impl->ready = false;
    if (!m_impl->configured) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[DBConnection] PostgreSQL runtime is not configured")
        // Log.Add(LOG_ERROR, "component=database event=init_failed reason=runtime_not_configured")
        // Log.Add(LOG_ERROR, "[database] Initialization failed: PostgreSQL runtime has not been configured.")
        L2_LOG_ERROR(Log, "[database] Initialization failed: PostgreSQL runtime has not been configured.");
        return;
    }

    InitPoolMap();
    if (!Login()) {
        // Noise: the connection helper already reports the failed operation and database ID.
        // Log.Add(
        //     LOG_ERROR,
        //     "fail to sign in database")

        return;
    }
    AllocateSQLPool();
}

int CSqlPoolManager::GetDBConnNum(int databaseId) const {
    L2_GUARD_FUNCTION();
    for (const database::PostgreSQLPoolConfig& database : m_impl->config.databases) {
        if (database.databaseId == databaseId && database.poolSizeOverride != 0) {
            return static_cast<int>(std::min<std::size_t>(database.poolSizeOverride, static_cast<std::size_t>(INT_MAX)));
        }
    }
    if (databaseId >= -3 && databaseId <= -1) {
        return 10;
    }
    unsigned int processors = std::thread::hardware_concurrency();
    if (processors == 0) {
        processors = 1;
    }
    processors = std::min(processors, 16U);
    return static_cast<int>(processors * 3U + 2U);
}

bool CSqlPoolManager::HasDatabaseId(int databaseId) const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return std::any_of(
        m_impl->config.databases.begin(), m_impl->config.databases.end(),
        [databaseId](const database::PostgreSQLPoolConfig& database) { return database.databaseId == databaseId; }
    );
}

void CSqlPoolManager::WriteUnhandledPoolCrashLog() const {
    // Current Cached.exe inlines this traversal into
    // L2Exception::PrintCrashSummary at 0x004cba90. It runs only after the
    // deadlock checker has forced a crash and takes no SQL-pool
    // locks. The pool map is immutable between Init and Shutdown. The live
    // counters below are the lock-free PostgreSQL projections of target
    // fields +0x00..+0x18.
    for (const auto& [databaseId, pool] : m_impl->pools) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"UnHandled Sql Pool World[%d] Remain Pool[%d] "
        //     L"UnHandled Pool[%d]",
        //     databaseId,
        //     pool->crashRemainingCount.load(
        //         std::memory_order_relaxed),
        //     pool->crashUnhandledCount.load(
        //         std::memory_order_relaxed))
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Pool Used time [%d],[%d],[%d],[%d],[%d]",
        //     static_cast<int>(
        //         pool->crashCheckoutDurations[0].load(
        //             std::memory_order_relaxed)),
        //     static_cast<int>(
        //         pool->crashCheckoutDurations[1].load(
        //             std::memory_order_relaxed)),
        //     static_cast<int>(
        //         pool->crashCheckoutDurations[2].load(
        //             std::memory_order_relaxed)),
        //     static_cast<int>(
        //         pool->crashCheckoutDurations[3].load(
        //             std::memory_order_relaxed)),
        //     static_cast<int>(
        //         pool->crashCheckoutDurations[4].load(
        //             std::memory_order_relaxed)))
        // Log.Add(
        //     LOG_NORMAL,
        //     L"component=database event=crash_pool_snapshot database_id=%d remaining=%d checked_out=%d",
        //     databaseId,
        //     pool->crashRemainingCount.load(
        //         std::memory_order_relaxed),
        //     pool->crashUnhandledCount.load(
        //         std::memory_order_relaxed))
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[database %d] Pool at crash: %d connections remaining, %d checked out.",
        //     databaseId,
        //     pool->crashRemainingCount.load(
        //         std::memory_order_relaxed),
        //     pool->crashUnhandledCount.load(
        //         std::memory_order_relaxed))
        L2_LOG_INFO(
            Log, u"[database %d] Pool at crash: %d connections remaining, %d checked out.", databaseId,
            pool->crashRemainingCount.load(std::memory_order_relaxed),
            pool->crashUnhandledCount.load(std::memory_order_relaxed)
        );
        // Log.Add(
        //     LOG_NORMAL,
        //     L"component=database event=crash_checkout_history database_id=%d durations_ms=%d,%d,%d,%d,%d",
        //     databaseId,
        //     static_cast<int>(
        //         pool->crashCheckoutDurations[0].load(
        //             std::memory_order_relaxed)),
        //     static_cast<int>(
        //         pool->crashCheckoutDurations[1].load(
        //             std::memory_order_relaxed)),
        //     static_cast<int>(
        //         pool->crashCheckoutDurations[2].load(
        //             std::memory_order_relaxed)),
        //     static_cast<int>(
        //         pool->crashCheckoutDurations[3].load(
        //             std::memory_order_relaxed)),
        //     static_cast<int>(
        //         pool->crashCheckoutDurations[4].load(
        //             std::memory_order_relaxed)))
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[database %d] Recent checkout durations (ms): %d, %d, %d, %d, %d.",
        //     databaseId,
        //     static_cast<int>(
        //         pool->crashCheckoutDurations[0].load(
        //             std::memory_order_relaxed)),
        //     static_cast<int>(
        //         pool->crashCheckoutDurations[1].load(
        //             std::memory_order_relaxed)),
        //     static_cast<int>(
        //         pool->crashCheckoutDurations[2].load(
        //             std::memory_order_relaxed)),
        //     static_cast<int>(
        //         pool->crashCheckoutDurations[3].load(
        //             std::memory_order_relaxed)),
        //     static_cast<int>(
        //         pool->crashCheckoutDurations[4].load(
        //             std::memory_order_relaxed)))
        L2_LOG_INFO(
            Log, u"[database %d] Recent checkout durations (ms): %d, %d, %d, %d, %d.", databaseId,
            static_cast<int>(pool->crashCheckoutDurations[0].load(std::memory_order_relaxed)),
            static_cast<int>(pool->crashCheckoutDurations[1].load(std::memory_order_relaxed)),
            static_cast<int>(pool->crashCheckoutDurations[2].load(std::memory_order_relaxed)),
            static_cast<int>(pool->crashCheckoutDurations[3].load(std::memory_order_relaxed)),
            static_cast<int>(pool->crashCheckoutDurations[4].load(std::memory_order_relaxed))
        );
    }
}

_SQLPool_* CSqlPoolManager::PopPool(int databaseId) {
    L2_GUARD_FUNCTION();
    // The waiter count tracks callers waiting for a connection.
    // The checked-out count rises only after the reserved-slot check passes.
    // Keep the locked pop, checkout timestamp and idle connection check in order.
    SqlPool* pool = nullptr;
    {
        std::lock_guard<std::mutex> managerLock(m_impl->mutex);
        const auto found = m_impl->pools.find(databaseId);
        if (found == m_impl->pools.end()) {
            return nullptr;
        }
        pool = found->second.get();
    }

    std::unique_lock<std::mutex> lock(pool->mutex);
    // int failedAttempts = 0
    std::uint64_t failedAttempts = 0;
    const std::uint32_t waitStarted = runtime::GetTickCount();
    while (pool->freeEntries.size() <= 1) {
        ++failedAttempts;
        // Noise: one ERROR per waiter per second obscures the pool exhaustion episode.
        // Log.Add(
        //     LOG_ERROR,
        //     "DBConn::DBConn no available SqlPool. World[%d]. "
        //     "Sleep 1 second. (%d times)",
        //     databaseId,
        //     failedAttempts)
        // Report the first wait and powers of two. Retain retries and wakeups.
        if ((failedAttempts & (failedAttempts - 1)) == 0) {
            // Log.Add(LOG_WARN, "component=database event=pool_exhausted database_id=%d attempts=%llu checked_out=%zu
            // remaining=%zu retry_ms=1000",
            //     databaseId, static_cast<unsigned long long>(failedAttempts),
            //     pool->checkedOut, pool->freeEntries.size())
            // Log.Add(LOG_WARN, "[database %d] Waiting for a usable connection: %llu unsuccessful attempts, %zu checked out,
            // %zu remaining including the reserved connection. Retry in one second.",
            //     databaseId, static_cast<unsigned long long>(failedAttempts),
            //     pool->checkedOut, pool->freeEntries.size())
            L2_LOG_WARN(
                Log,
                "[database %d] Waiting for a usable connection: %llu unsuccessful attempts, %zu checked out, %zu remaining "
                "including the reserved connection. Retry in one second.",
                databaseId, static_cast<unsigned long long>(failedAttempts), pool->checkedOut, pool->freeEntries.size()
            );
        }
        pool->available.wait_for(lock, std::chrono::milliseconds(1000));
    }

    if (failedAttempts != 0) {
        // Log.Add(LOG_NORMAL, "component=database event=pool_available database_id=%d attempts=%llu wait_ms=%u",
        //     databaseId, static_cast<unsigned long long>(failedAttempts),
        //     static_cast<std::uint32_t>(runtime::GetTickCount() - waitStarted))
        // Log.Add(LOG_NORMAL, "[database %d] A connection is available after %llu unsuccessful attempts and %u ms of
        // waiting.",
        //     databaseId, static_cast<unsigned long long>(failedAttempts),
        //     static_cast<std::uint32_t>(runtime::GetTickCount() - waitStarted))
        L2_LOG_INFO(
            Log, "[database %d] A connection is available after %llu unsuccessful attempts and %u ms of waiting.",
            databaseId, static_cast<unsigned long long>(failedAttempts),
            static_cast<std::uint32_t>(runtime::GetTickCount() - waitStarted)
        );
    }

    _SQLPool_* entry = pool->freeEntries.front();
    pool->freeEntries.pop_front();
    const std::uint32_t previousTick = entry->checkoutTick;
    const std::uint32_t now = runtime::GetTickCount();
    entry->checkoutTick = now;
    ++pool->checkedOut;
    pool->crashRemainingCount.store(static_cast<int>(pool->freeEntries.size()), std::memory_order_relaxed);
    pool->crashUnhandledCount.store(static_cast<int>(pool->checkedOut), std::memory_order_relaxed);
    lock.unlock();

    const std::uint32_t checkInterval = m_impl->config.waitTimeoutCheckMilliseconds;
    if (checkInterval != 0 && static_cast<std::uint32_t>(now - previousTick) > checkInterval) {
        // Noise: checking an idle connection is routine maintenance, not an error.
        // Log.Add(
        //     LOG_ERROR,
        //     "[DBConnection] Check Wait_timeout. Elapsed time since last "
        //     "usage of connection[%u(msec)]",
        //     static_cast<std::uint32_t>(now - previousTick))

        if (!CheckConnection(entry->connection)) {
            // Log.Add(LOG_WARN, "component=database event=health_check_failed database_id=%d idle_ms=%u action=reconnect",
            //     databaseId, static_cast<std::uint32_t>(now - previousTick))
            // Log.Add(LOG_WARN, "[database %d] Connection check failed after %u ms idle. Reconnecting.",
            //     databaseId, static_cast<std::uint32_t>(now - previousTick))
            L2_LOG_WARN(
                Log, "[database %d] Connection check failed after %u ms idle. Reconnecting.", databaseId,
                static_cast<std::uint32_t>(now - previousTick)
            );
            // Current stripped reconnect helper 0x004a6b68 (original symbol
            // absent), 143 bytes, SHA-256
            // 2efaec78fbf189c3861015aa8060d66da8e051ae263234079e067d92a6ba490d.
            // It creates the replacement first and swaps only after success.
            // PGconn* replacement = ConnectPostgreSQL(
            //     pool->connectionString,
            //     m_impl->config.statementTimeoutMilliseconds,
            //     true)
            PGconn* replacement = ConnectPostgreSQL(
                pool->connectionString, m_impl->config.statementTimeoutMilliseconds, true, pool->databaseId
            );
            if (replacement != nullptr) {
                PGconn* previous = entry->connection;
                entry->connection = replacement;
                if (previous != nullptr) {
                    PQfinish(previous);
                }
                // Log.Add(LOG_NORMAL, "component=database event=reconnected database_id=%d", databaseId)
                // Log.Add(LOG_NORMAL, "[database %d] Reconnected.", databaseId)
                L2_LOG_INFO(Log, "[database %d] Reconnected.", databaseId);
            }
        }
    }
    return entry;
}

void CSqlPoolManager::PushPool(int databaseId, _SQLPool_* entry) {
    // Current stripped per-resource return helper 0x004a3db4 (original symbol
    // absent), 147 bytes, SHA-256
    // fe54337767d018d8480755c4d31fdcd1a1f478aeb6190aeb5bbea805a7407538.
    L2_GUARD_FUNCTION();
    // It prepends the entry, records one of five checkout durations and
    // decrements the checked-out count under the pool lock.
    if (entry == nullptr) {
        return;
    }
    SqlPool* pool = nullptr;
    {
        std::lock_guard<std::mutex> managerLock(m_impl->mutex);
        const auto found = m_impl->pools.find(databaseId);
        if (found == m_impl->pools.end()) {
            return;
        }
        pool = found->second.get();
    }

    const std::uint32_t duration = static_cast<std::uint32_t>(runtime::GetTickCount() - entry->checkoutTick);
    {
        std::lock_guard<std::mutex> lock(pool->mutex);
        const std::size_t durationIndex = pool->durationCursor;
        pool->checkoutDurations[durationIndex] = duration;
        pool->crashCheckoutDurations[durationIndex].store(duration, std::memory_order_relaxed);
        pool->durationCursor = (pool->durationCursor + 1) % pool->checkoutDurations.size();
        if (pool->checkedOut != 0) {
            --pool->checkedOut;
        }
        pool->freeEntries.push_front(entry);
        pool->crashRemainingCount.store(static_cast<int>(pool->freeEntries.size()), std::memory_order_relaxed);
        pool->crashUnhandledCount.store(static_cast<int>(pool->checkedOut), std::memory_order_relaxed);
    }
    pool->available.notify_one();
}

bool CSqlPoolManager::IsReady() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->ready;
}

void CSqlPoolManager::Shutdown() {
    // Current stripped array cleanup 0x004a4090 walks the target pool's
    // fixed 0x20-byte ODBC entries, applies 0x004a3f44 to every entry, frees
    // the array and clears its pointer. Here each unique_ptr-owned entry is
    // kept by the portable pool and PQfinish replaces handle cleanup.
    // Current stripped ODBC handle-release helper 0x004a3f44 (original symbol
    // absent), 69 bytes, SHA-256
    // 0e2d5d6d788107c6fc612c4cdf7a7f5e6baed7e866ee1f5053adc383a1e70b4a.
    // PQfinish is the PostgreSQL transport equivalent for each live entry.
    std::lock_guard<std::mutex> managerLock(m_impl->mutex);
    for (const auto& [databaseId, pool] : m_impl->pools) {
        std::lock_guard<std::mutex> poolLock(pool->mutex);
        if (pool->checkedOut != 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[DBConnection] refusing shutdown with %zu checked-out "
            //     "World[%d] sessions",
            //     pool->checkedOut,
            //     databaseId)
            // Log.Add(LOG_ERROR, "component=database event=shutdown_deferred database_id=%d checked_out=%zu
            // reason=sessions_in_use",
            //     databaseId, pool->checkedOut)
            // Log.Add(LOG_ERROR, "[database %d] Shutdown deferred: %zu connections are still checked out.",
            //     databaseId, pool->checkedOut)
            L2_LOG_ERROR(
                Log, "[database %d] Shutdown deferred: %zu connections are still checked out.", databaseId, pool->checkedOut
            );
            return;
        }
    }
    for (auto& [databaseId, pool] : m_impl->pools) {
        for (auto& entry : pool->entries) {
            if (entry->connection != nullptr) {
                PQfinish(entry->connection);
                entry->connection = nullptr;
            }
        }
    }
    m_impl->pools.clear();
    m_impl->ready = false;
    m_impl->initialized = false;
}

struct DBConn::Impl {
    int databaseId = 0;
    _SQLPool_* poolEntry = nullptr;
    PGresult* result = nullptr;
    int row = -1;
    std::vector<InputBinding> inputs;
    std::vector<std::size_t> parameterDiagnostics;
    std::vector<OutputBinding> outputs;
    std::uint16_t resultOrdinal = 1;
    std::uint16_t parameterOrdinal = 1;
    std::size_t preparedParameterCount = 0;
    std::string preparedSql;
    std::string lastQuery;
    // Logging-only originals: keep SQL text separate from translated execution
    // SQL and the recovered, bounded observation buffer above.
    std::string preparedDiagnosticSql;
    std::string resultDiagnosticSql;
    bool prepared = false;
    bool valid = false;

    PGconn* Connection() const { return poolEntry == nullptr ? nullptr : poolEntry->connection; }

    void ClearResult() {
        resultDiagnosticSql.clear();
        if (result != nullptr) {
            PQclear(result);
            result = nullptr;
        }
        row = -1;
    }

    void AddInput(InputBinding binding) {
        inputs.push_back(binding);
        parameterDiagnostics.push_back(inputs.size() - 1);
        ++parameterOrdinal;
    }

    void AddOutput(OutputBinding binding) {
        outputs.push_back(binding);
        ++resultOrdinal;
    }
};

DBConn::DBConn(int databaseId) : m_impl(std::make_unique<Impl>()) {
    L2_GUARD_FUNCTION();
    m_impl->databaseId = databaseId;
    m_impl->poolEntry = GetSqlPoolManager().PopPool(databaseId);
    if (m_impl->poolEntry == nullptr || m_impl->poolEntry->connection == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "invalid dbconnection nDBConn[%d]",
        //     databaseId)
        // Log.Add(LOG_ERROR, "component=database event=checkout_failed database_id=%d reason=session_unavailable",
        //     databaseId)
        // Log.Add(LOG_ERROR, "[database %d] Could not obtain a connection: session is unavailable.",
        //     databaseId)
        L2_LOG_ERROR(Log, "[database %d] Could not obtain a connection: session is unavailable.", databaseId);
        return;
    }
    m_impl->valid = true;
}

DBConn::~DBConn() {
    L2_GUARD_FUNCTION();
    if (!m_impl) {
        return;
    }
    if (m_impl->valid) {
        // SQL_UNBIND, SQL_CLOSE, SQL_RESET_PARAMS in target order.
        m_impl->outputs.clear();
        m_impl->resultOrdinal = 1;
        m_impl->ClearResult();
        m_impl->inputs.clear();
        m_impl->parameterDiagnostics.clear();
        m_impl->parameterOrdinal = 1;
        GetSqlPoolManager().PushPool(m_impl->databaseId, m_impl->poolEntry);
    }
}

bool DBConn::IsValid() const { return m_impl && m_impl->valid; }

int DBConn::GetDatabaseId() const { return m_impl ? m_impl->databaseId : 0; }

void DBConn::Bind(wchar_t* value, int targetBufferBytes) {
    L2_GUARD_FUNCTION();
    m_impl->AddOutput({OutputKind::WideText, value, targetBufferBytes, nullptr});
}

void DBConn::Bind(char16_t* value, int bufferBytes) {
    m_impl->AddOutput({OutputKind::Utf16Text, value, bufferBytes, nullptr});
}

void DBConn::Bind(char* value, int bufferBytes) {
    L2_GUARD_FUNCTION();
    m_impl->AddOutput({OutputKind::NarrowText, value, bufferBytes, nullptr});
}

void DBConn::Bind(unsigned int* value) {
    L2_GUARD_FUNCTION();
    m_impl->AddOutput({OutputKind::UInt32, value, 4, nullptr});
}

void DBConn::Bind(int* value) {
    L2_GUARD_FUNCTION();
    m_impl->AddOutput({OutputKind::Int32, value, 4, nullptr});
}

void DBConn::Bind(long long* value) {
    L2_GUARD_FUNCTION();
    m_impl->AddOutput({OutputKind::Int64, value, 8, nullptr});
}

void DBConn::BindInt64(void* value) { m_impl->AddOutput({OutputKind::Int64, value, 8, nullptr}); }

void DBConn::Bind(short* value) {
    L2_GUARD_FUNCTION();
    m_impl->AddOutput({OutputKind::Int16, value, 2, nullptr});
}

void DBConn::Bind(double* value) {
    L2_GUARD_FUNCTION();
    m_impl->AddOutput({OutputKind::Double, value, 8, nullptr});
}

void DBConn::BindBinary(char* value, int bufferBytes) {
    L2_GUARD_FUNCTION();
    m_impl->AddOutput({OutputKind::Binary, value, bufferBytes, nullptr});
}

void DBConn::BindBinaryNullable(unsigned char* value, int bufferBytes, std::int64_t* lengthOrIndicator) {
    L2_GUARD_FUNCTION();
    m_impl->AddOutput({OutputKind::Binary, value, bufferBytes, lengthOrIndicator});
}

void DBConn::BindNullable(wchar_t* value, int targetBufferBytes, std::int64_t* lengthOrIndicator) {
    L2_GUARD_FUNCTION();
    m_impl->AddOutput({OutputKind::WideText, value, targetBufferBytes, lengthOrIndicator});
}

void DBConn::BindNullable(char16_t* value, int bufferBytes, std::int64_t* lengthOrIndicator) {
    m_impl->AddOutput({OutputKind::Utf16Text, value, bufferBytes, lengthOrIndicator});
}

void DBConn::BindNullable(unsigned int* value, std::int64_t* lengthOrIndicator) {
    L2_GUARD_FUNCTION();
    m_impl->AddOutput({OutputKind::UInt32, value, 4, lengthOrIndicator});
}

void DBConn::BindNullable(int* value, std::int64_t* lengthOrIndicator) {
    L2_GUARD_FUNCTION();
    m_impl->AddOutput({OutputKind::Int32, value, 4, lengthOrIndicator});
}

void DBConn::BindNullable(long long* value, std::int64_t* lengthOrIndicator) {
    L2_GUARD_FUNCTION();
    m_impl->AddOutput({OutputKind::Int64, value, 8, lengthOrIndicator});
}

void DBConn::BindNullableInt64(void* value, std::int64_t* lengthOrIndicator) {
    m_impl->AddOutput({OutputKind::Int64, value, 8, lengthOrIndicator});
}

void DBConn::BindNullable(double* value, std::int64_t* lengthOrIndicator) {
    L2_GUARD_FUNCTION();
    m_impl->AddOutput({OutputKind::Double, value, 8, lengthOrIndicator});
}

// The current CacheD specializations below are separate COMDAT bodies in the
// shipping image. PostgreSQL replaces SQLBindParameter transport, while
// retaining their input kind, SQL type, bind order and diagnostic tag.
// bool: 0x00525624 (SQL_C_BIT / SQL_BIT).
void DBConn::BindParameter(const bool* value) {
    m_impl->AddInput({InputKind::Boolean, ParameterSqlType::Boolean, value, 1, nullptr, 0});
}

// short: 0x00483cf8 (SQL_C_SSHORT / SQL_SMALLINT).
void DBConn::BindParameter(const short* value) {
    m_impl->AddInput({InputKind::Int16, ParameterSqlType::SmallInt, value, 2, nullptr, 3});
}

// int: 0x00402364 (SQL_C_SLONG / SQL_INTEGER).
void DBConn::BindParameter(const int* value) {
    m_impl->AddInput({InputKind::Int32, ParameterSqlType::Integer, value, 4, nullptr, 5});
}

// unsigned int: 0x00409bf8 (SQL_C_ULONG / SQL_INTEGER).
void DBConn::BindParameter(const unsigned int* value) {
    m_impl->AddInput({InputKind::UInt32, ParameterSqlType::Integer, value, 4, nullptr, 6});
}

// __int64: 0x00432294 (SQL_C_SBIGINT / SQL_BIGINT).
void DBConn::BindParameter(const long long* value) {
    m_impl->AddInput({InputKind::Int64, ParameterSqlType::BigInt, value, 8, nullptr, 7});
}

// unsigned __int64: 0x00432310 (SQL_C_UBIGINT / SQL_BIGINT).
void DBConn::BindParameter(const unsigned long long* value) {
    m_impl->AddInput({InputKind::UInt64, ParameterSqlType::BigInt, value, 8, nullptr, 8});
}

// double: 0x00483d74 (SQL_C_DOUBLE / SQL_DOUBLE).
void DBConn::BindParameter(const double* value) {
    m_impl->AddInput({InputKind::Double, ParameterSqlType::Double, value, 8, nullptr, 9});
}

// char string: 0x00432404 (SQL_C_CHAR / SQL_VARCHAR). The target uses a
// minimum column size of one and a zero data length for an empty string.
void DBConn::BindParameter(const char* value) {
    const std::size_t length = value == nullptr ? 0 : std::strlen(value);
    m_impl->AddInput({InputKind::NarrowText, ParameterSqlType::Text, value, length, nullptr, 14});
}

// wchar_t string: 0x00409c70. Windows counts UTF-16 code units, selects
// SQL_WVARCHAR/type tag 15 below 4001 units and SQL_WLONGVARCHAR/tag 16 from
// 4001 onward, and supplies a two-byte minimum column size for an empty value.
void DBConn::BindParameter(const wchar_t* value) {
    const std::size_t units = TargetUtf16Units(value);
    const bool longValue = units >= 4001;
    m_impl->AddInput(
        {longValue ? InputKind::WideLongText : InputKind::WideText, ParameterSqlType::Text, value, units, nullptr,
            longValue ? 16 : 15}
    );
}

// UTF-16 follows the same NULL, length and diagnostic-tag contract as wide
// bindings, without a temporary native-wide string before PostgreSQL UTF-8.
void DBConn::BindParameter(const char16_t* value) {
    const std::size_t units = value ? std::char_traits<char16_t>::length(value) : 0;
    m_impl->AddInput({InputKind::Utf16Text, ParameterSqlType::Text, value, units, nullptr, units >= 4001 ? 16 : 15});
}

// target long: 0x0043238c (SQL_C_SLONG / SQL_DOUBLE).
void DBConn::BindParameter(const long* value) {
    m_impl->AddInput({InputKind::Int32AsDouble, ParameterSqlType::Double, value, 4, nullptr, 11});
}

// target unsigned long: 0x00513158 (SQL_C_ULONG / SQL_DOUBLE).
void DBConn::BindParameter(const unsigned long* value) {
    m_impl->AddInput({InputKind::UInt32AsDouble, ParameterSqlType::Double, value, 4, nullptr, 12});
}

// 32-bit enum specialization: 0x0054af44 (SQL_C_SLONG / SQL_INTEGER).
void DBConn::BindParameterEnum32(const void* value) {
    m_impl->AddInput({InputKind::Enum32, ParameterSqlType::Integer, value, 4, nullptr, 17});
}

void DBConn::BindParameterBinary(const void* value, std::size_t size, const std::int64_t* lengthOrIndicator) {
    m_impl->AddInput({InputKind::Binary, ParameterSqlType::Binary, value, size, lengthOrIndicator, -1});
}

void DBConn::BindParameterNull() { m_impl->AddInput({InputKind::Null, ParameterSqlType::Text, nullptr, 0, nullptr, -1}); }

void DBConn::ResetParam() {
    // Current Cached.exe 0x004a5818.
    L2_GUARD_FUNCTION();
    // SQL_RESET_PARAMS, SQL_CLOSE and diagnostic-queue clear. Result-column
    // bindings and resultOrdinal survive.
    m_impl->inputs.clear();
    m_impl->parameterDiagnostics.clear();
    m_impl->parameterOrdinal = 1;
    m_impl->ClearResult();
}

void DBConn::UnBind() {
    // Current Cached.exe 0x004a58fc.
    L2_GUARD_FUNCTION();
    // Current order: numbers -> close cursor -> unbind columns -> reset params.
    m_impl->resultOrdinal = 1;
    m_impl->parameterOrdinal = 1;
    m_impl->ClearResult();
    m_impl->outputs.clear();
    m_impl->inputs.clear();
    m_impl->parameterDiagnostics.clear();
}

// Sensitive data: parameter dumps expose account data and credentials. Summarize binding metadata without reading values.
// void DBConn::ReportParamInfo() {
//     for (std::size_t bindingIndex : m_impl->parameterDiagnostics) {
//         if (bindingIndex >= m_impl->inputs.size()) continue.
//         const InputBinding& binding = m_impl->inputs[bindingIndex]
//         const MaterializedParameter materialized = Materialize(binding)
//         const std::size_t ordinal = bindingIndex + 1
//         if (created.isNull) {
//             Log.Add(
//                 LOG_ERROR,
//                 "[ErrSQL %03zu][NULL]",
//                 number)
//             continue.
//         }
//         const char* type = "DUMMY"
//         switch (binding.diagnosticTag) {
//             case 0: type = "BOOL". Break.
//             case 1:
//             case 2: type = "SINGLE_CHAR". Break.
//             case 3:
//             case 4: type = "SHORT". Break.
//             case 5:
//             case 6: type = "INT". Break.
//             case 7:
//             case 8: type = "INT64". Break.
//             case 9:
//             case 10: type = "DOUBLE". Break.
//             case 11:
//             case 12: type = "LONG". Break.
//             case 13: type = "SINGLE_WCHAR". Break.
//             case 14: type = "CHAR". Break.
//             case 15: type = "WCHAR". Break.
//             case 16: type = "WLONGVARCHAR". Break.
//             case 17: type = "DUMMY". Break.
//             default: type = "BINARY". Break.
//         }
//         std::string value = materialized.binaryFormat
//             ? "<" + std::to_string(materialized.binary.size()) + " bytes>"
//             : created.text.
//         if (binding.diagnosticTag == 16 && value.size() > 100) {
//             value.resize(100)
//         }
//         Log.Add(
//             LOG_ERROR,
//             "[ErrSQL %03zu][%s][%s]",
//             number,
//             type,
//             value.c_str())
//     }
//     // ReportParamInfo consumes the target's descriptor deque.
//     m_impl->parameterDiagnostics.clear()
// }
void DBConn::ReportParamInfo() {
    L2_GUARD_FUNCTION();
    std::size_t count = 0, textCount = 0, binaryCount = 0;
    for (const std::size_t index : m_impl->parameterDiagnostics) {
        if (index >= m_impl->inputs.size()) {
            continue;
        }
        ++count;
        const auto kind = m_impl->inputs[index].kind;
        if (kind == InputKind::NarrowText || kind == InputKind::WideText || kind == InputKind::WideLongText ||
            kind == InputKind::Utf16Text) {
            ++textCount;
        }
        if (kind == InputKind::Binary) {
            ++binaryCount;
        }
    }
    if (count != 0) {
        // Log.Add(LOG_ERROR, "component=database event=parameter_summary database_id=%d parameters=%zu text_parameters=%zu
        // binary_parameters=%zu values=redacted",
        //     m_impl->databaseId, count, textCount, binaryCount)
        // Log.Add(LOG_ERROR, "[database %d] Bound parameters: %zu total, %zu text, %zu binary. Values are omitted, SQL
        // placeholders are preserved.",
        //     m_impl->databaseId, count, textCount, binaryCount)
        L2_LOG_ERROR(
            Log,
            "[database %d] Bound parameters: %zu total, %zu text, %zu binary. Values are omitted; SQL placeholders are "
            "preserved.",
            m_impl->databaseId, count, textCount, binaryCount
        );
    }
    // Keep the recovered consumption of the diagnostic descriptor deque.
    m_impl->parameterDiagnostics.clear();
}

bool DBConn::Prepare(const wchar_t* query) {
    L2_GUARD_FUNCTION();
    if (!m_impl->valid || m_impl->Connection() == nullptr) {
        return false;
    }
    std::string diagnosticSql = WideToUtf8(query);
    const auto& config = GetSqlPoolManager().m_impl->config;
    const TranslatedQuery translated = TranslateQuery(query, m_impl->inputs, config, m_impl->poolEntry->defaultSchema);
    PGresult* prepared = PQprepare(m_impl->Connection(), "", translated.sql.c_str(), 0, nullptr);
    const bool success = prepared != nullptr && PQresultStatus(prepared) == PGRES_COMMAND_OK;
    if (success) {
        m_impl->preparedDiagnosticSql = std::move(diagnosticSql);
        m_impl->preparedSql = translated.sql;
        m_impl->preparedParameterCount = translated.parameterCount;
        m_impl->prepared = true;
    } else {
        m_impl->prepared = false;
        m_impl->preparedDiagnosticSql.clear();
        m_impl->resultDiagnosticSql.clear();
        m_impl->preparedParameterCount = 0;
        m_impl->preparedSql.clear();
        m_impl->result = prepared;
        Error(query, prepared == nullptr ? static_cast<int>(PGRES_FATAL_ERROR) : static_cast<int>(PQresultStatus(prepared)));
        m_impl->result = nullptr;
    }
    if (prepared != nullptr) {
        PQclear(prepared);
    }
    return success;
}

bool DBConn::ExecutePrepared(const wchar_t* query) {
    L2_GUARD_FUNCTION();
    return ExecutePreparedImpl(query, true, true, nullptr);
}

bool DBConn::ExecutePreparedDirect(const wchar_t* query, std::uint32_t& durationMilliseconds) {
    return ExecutePreparedImpl(query, false, false, &durationMilliseconds);
}

bool DBConn::ExecutePreparedImpl(
    const wchar_t* query, bool observeQuery, bool reportFailure, std::uint32_t* measuredDuration
) {
    if (!m_impl->valid || m_impl->Connection() == nullptr || !m_impl->prepared) {
        if (measuredDuration) {
            *measuredDuration = 0;
        }
        return false;
    }

    // SQLBindParameter calls whose number exceeds the marker count do not
    // become inputs to SQLExecute. Current Cached.exe relies on that ODBC
    // behavior in RequestUpdateUICount: it prepares the same two-marker
    // statement in a loop, keeps incrementing DBConn::parameterOrdinal and
    // ignores the failed excess binds. The first two bindings point at the
    // reused loop locals and therefore carry each iteration's current values.
    const std::size_t effectiveInputCount = std::min(m_impl->inputs.size(), m_impl->preparedParameterCount);
    std::vector<MaterializedParameter> materialized;
    materialized.reserve(effectiveInputCount);
    for (std::size_t index = 0; index < effectiveInputCount; ++index) {
        materialized.push_back(Materialize(m_impl->inputs[index]));
    }
    std::vector<const char*> values(materialized.size());
    std::vector<int> lengths(materialized.size());
    std::vector<int> formats(materialized.size());
    static const char emptyBinary = '\0';
    for (std::size_t index = 0; index < materialized.size(); ++index) {
        MaterializedParameter& parameter = materialized[index];
        if (parameter.isNull) {
            values[index] = nullptr;
            continue;
        }
        if (parameter.binaryFormat) {
            values[index] = parameter.binary.empty() ? &emptyBinary : reinterpret_cast<const char*>(parameter.binary.data());
            lengths[index] =
                static_cast<int>(std::min<std::size_t>(parameter.binary.size(), static_cast<std::size_t>(INT_MAX)));
            formats[index] = 1;
        } else {
            values[index] = parameter.text.c_str();
            lengths[index] =
                static_cast<int>(std::min<std::size_t>(parameter.text.size(), static_cast<std::size_t>(INT_MAX)));
            formats[index] = 0;
        }
    }

    m_impl->ClearResult();
    m_impl->resultDiagnosticSql = m_impl->preparedDiagnosticSql;
    const std::uint32_t begin = runtime::GetTickCount();
    m_impl->result = PQexecPrepared(
        m_impl->Connection(), "", static_cast<int>(values.size()), values.empty() ? nullptr : values.data(),
        lengths.empty() ? nullptr : lengths.data(), formats.empty() ? nullptr : formats.data(), 0
    );
    const std::uint32_t duration = static_cast<std::uint32_t>(runtime::GetTickCount() - begin);
    if (measuredDuration) {
        *measuredDuration = duration;
    }
    const ExecStatusType status = m_impl->result == nullptr ? PGRES_FATAL_ERROR : PQresultStatus(m_impl->result);
    const bool success = PostgreSQLSuccess(status);

    const std::string originalQuery = WideToUtf8(query);
    const auto& config = GetSqlPoolManager().m_impl->config;
    if (observeQuery && duration > kSlowQueryMilliseconds) {
        if (config.slowQueryObserver) {
            config.slowQueryObserver(originalQuery, duration);
        } else {
            // Log.Add(
            //     LOG_WARN,
            //     "[QsCheck] slow sql [%u]ms, sql[%s]",
            //     duration,
            //     originalQuery.c_str())
            // Log.Add(LOG_WARN, "component=database event=slow_query database_id=%d statement_kind=%s elapsed_ms=%u
            // threshold_ms=%u sql=redacted",
            //     m_impl->databaseId, runtime::logging::SqlStatementKind(originalQuery.c_str()), duration,
            //     kSlowQueryMilliseconds)
            // Log.Add(LOG_WARN, "[database %d] Query took %u ms (slow-query threshold %u ms).",
            //     m_impl->databaseId, duration, kSlowQueryMilliseconds)
            L2_LOG_WARN(
                Log, "[database %d] Query took %u ms (slow-query threshold %u ms).", m_impl->databaseId, duration,
                kSlowQueryMilliseconds
            );
            LogSqlStatement(LOG_WARN, m_impl->databaseId, "Slow query", originalQuery);
        }
    }
    if (observeQuery && duration > config.sqlExecLimitMilliseconds) {
        // Historical policy: "SQL literals are omitted" no longer applies.
        // SQL is logged below unchanged. Exceeding this observation threshold does not mean execution failed.
        // Log.Add(
        //     LOG_ERROR,
        //     "sql execution time [%u]ms, sql[%s]",
        //     duration,
        //     originalQuery.c_str())
        // Log.Add(LOG_WARN, "component=database event=query_time_limit_exceeded database_id=%d statement_kind=%s
        // elapsed_ms=%u threshold_ms=%u sql=redacted",
        //     m_impl->databaseId, runtime::logging::WideSqlStatementKind(query), duration,
        //     config.sqlExecLimitMilliseconds)
        // Log.Add(LOG_WARN, "[database %d] Query took %u ms, exceeding the %u ms observation limit.",
        //     m_impl->databaseId, duration, config.sqlExecLimitMilliseconds)
        L2_LOG_WARN(
            Log, "[database %d] Query took %u ms, exceeding the %u ms observation limit.", m_impl->databaseId, duration,
            config.sqlExecLimitMilliseconds
        );
        LogSqlStatement(LOG_WARN, m_impl->databaseId, "Query time limit exceeded", WideToUtf8(query));
    }

    if (observeQuery) {
        m_impl->lastQuery = TruncateWideForTargetQuery(query);
    }
    m_impl->row = -1;
    if (!success) {
        if (reportFailure) {
            Error(query, static_cast<int>(status));
        }
        return false;
    }
    if (observeQuery && config.successfulQueryObserver) {
        config.successfulQueryObserver(originalQuery, duration);
    }
    return true;
}

bool DBConn::ExecuteNoError(const wchar_t* query, ...) {
    L2_GUARD_FUNCTION();
    if (!m_impl->valid || m_impl->Connection() == nullptr) {
        return false;
    }
    const auto& config = GetSqlPoolManager().m_impl->config;
    const TranslatedQuery translated = TranslateQuery(query, m_impl->inputs, config, m_impl->poolEntry->defaultSchema);
    m_impl->ClearResult();
    m_impl->resultDiagnosticSql = WideToUtf8(query);
    const std::uint32_t begin = runtime::GetTickCount();
    m_impl->result = PQexec(m_impl->Connection(), translated.sql.c_str());
    const std::uint32_t duration = static_cast<std::uint32_t>(runtime::GetTickCount() - begin);
    if (duration > config.sqlExecLimitMilliseconds) {
        // Historical policy: "SQL literals are omitted" no longer applies.
        // SQL is logged below unchanged. Exceeding this observation threshold does not mean execution failed.
        // Log.Add(
        //     LOG_ERROR,
        //     "sql execution time [%u]ms, sql[%s]",
        //     duration,
        //     WideToUtf8(query).c_str())
        // Log.Add(LOG_WARN, "component=database event=query_time_limit_exceeded database_id=%d statement_kind=%s
        // elapsed_ms=%u threshold_ms=%u sql=redacted",
        //     m_impl->databaseId, runtime::logging::WideSqlStatementKind(query), duration,
        //     config.sqlExecLimitMilliseconds)
        // Log.Add(LOG_WARN, "[database %d] Query took %u ms, exceeding the %u ms observation limit.",
        //     m_impl->databaseId, duration, config.sqlExecLimitMilliseconds)
        L2_LOG_WARN(
            Log, "[database %d] Query took %u ms, exceeding the %u ms observation limit.", m_impl->databaseId, duration,
            config.sqlExecLimitMilliseconds
        );
        LogSqlStatement(LOG_WARN, m_impl->databaseId, "Query time limit exceeded", WideToUtf8(query));
    }
    m_impl->row = -1;
    return m_impl->result != nullptr && PostgreSQLSuccess(PQresultStatus(m_impl->result));
}

bool DBConn::Fetch() {
    // Current Cached.exe 0x004a4828.
    L2_GUARD_FUNCTION();
    if (!m_impl->valid || m_impl->Connection() == nullptr) {
        // Log.Add(LOG_ERROR, "Fetch error = (STMT)")
        // Log.Add(LOG_ERROR, "component=database event=fetch_failed database_id=%d reason=session_unavailable",
        // m_impl->databaseId), Log.Add(LOG_ERROR, "[database %d] Cannot fetch a row: session is unavailable.",
        // m_impl->databaseId)
        L2_LOG_ERROR(Log, "[database %d] Cannot fetch a row: session is unavailable.", m_impl->databaseId);
        // The target follows the error record with
        // ReportCallStackNew(L"Fetch", LOG_ERROR, false).
        ::ReportCallStackNew(const_cast<wchar_t*>(L"Fetch"), LOG_ERROR, false);
        return false;
    }
    if (m_impl->result == nullptr || PQresultStatus(m_impl->result) != PGRES_TUPLES_OK) {
        if (m_impl->result != nullptr && !PostgreSQLSuccess(PQresultStatus(m_impl->result))) {
            // Historical policy: use result metadata instead of driver text and the last SQL statement.
            // Current policy also emits PostgreSQL details and the saved SQL through the helpers below.
            // Log.Add(
            //     LOG_ERROR,
            //     "[FetchError] sqlstate %s msgText %s",
            //     ResultSqlState(m_impl->result).c_str(),
            //     ResultErrorMessage(
            //         m_impl->Connection(),
            //         m_impl->result).c_str())
            // Log.Add(
            //     LOG_ERROR,
            //     "[FetchError] sql %s",
            //     m_impl->lastQuery.c_str())
            // LogDatabaseDiagnostic(m_impl->databaseId, "fetch", m_impl->Connection(), m_impl->result,
            //     static_cast<int>(PQresultStatus(m_impl->result)),
            //     runtime::logging::SqlStatementKind(m_impl->lastQuery.c_str()))
            // LogSqlStatement(LOG_ERROR, m_impl->databaseId, "Fetch failed, saved statement", m_impl->lastQuery)
            LogDatabaseDiagnostic(
                m_impl->databaseId, "fetch", m_impl->Connection(), m_impl->result,
                static_cast<int>(PQresultStatus(m_impl->result)),
                runtime::logging::SqlStatementKind(m_impl->resultDiagnosticSql.c_str())
            );
            LogSqlStatement(LOG_ERROR, m_impl->databaseId, "Fetch failed; original statement", m_impl->resultDiagnosticSql);
        }
        return false;
    }
    if (m_impl->row + 1 >= PQntuples(m_impl->result)) {
        return false;
    }
    ++m_impl->row;
    const int fields = PQnfields(m_impl->result);
    for (std::size_t index = 0; index < m_impl->outputs.size(); ++index) {
        if (static_cast<int>(index) >= fields) {
            continue;
        }
        ApplyOutput(
            m_impl->outputs[index], PQgetisnull(m_impl->result, m_impl->row, static_cast<int>(index)) != 0,
            PQgetvalue(m_impl->result, m_impl->row, static_cast<int>(index))
        );
    }
    return true;
}

void DBConn::Error(const wchar_t* query, int statusCode) {
    L2_GUARD_FUNCTION();
    // Historical policy: the diagnostic no longer formats a copy of the query.
    // Current policy logs the original query via WideToUtf8 and LogSqlStatement below.
    // const std::string queryText = query == nullptr
    //     ? std::string()
    //     : TruncateWideForTargetQuery(query)

    if (query != nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[ErrSQL] sql: %s (%d)",
        //     queryText.c_str(),
        //     statusCode)
        // Log.Add(LOG_ERROR, "component=database event=query_failed database_id=%d statement_kind=%s result_status=%d
        // sql=redacted",
        //     m_impl->databaseId, runtime::logging::WideSqlStatementKind(query), statusCode)
        // Log.Add(LOG_ERROR, "[database %d] Query failed (result %d).", m_impl->databaseId, statusCode)
        L2_LOG_ERROR(Log, "[database %d] Query failed (result %d).", m_impl->databaseId, statusCode);
        LogSqlStatement(LOG_ERROR, m_impl->databaseId, "Failed query", WideToUtf8(query));
        ReportParamInfo();
    }

    std::string state = ResultSqlState(m_impl->result);
    PGconn* connection = m_impl->Connection();
    const bool brokenConnection = connection == nullptr || PQstatus(connection) != CONNECTION_OK;
    if (brokenConnection && state != "08S01") {
        // Current ODBC reports this transport failure as 08S01. libpq can
        // instead return no SQLSTATE at all (or a server-shutdown state) once
        // the socket is gone, so normalize only the confirmed broken-link
        // condition before applying the target's fatal policy.
        state = "08S01";
    }
    const std::string message = ResultErrorMessage(connection, m_impl->result);
    // Historical policy: keep driver text for the notification contract, but never print it here.
    // Current policy emits PostgreSQL result details through LogDatabaseDiagnostic.
    // separate connection credentials remain excluded from connection-failure diagnostics.
    // Log.Add(
    //     LOG_ERROR,
    //     "[ErrSQL] State:%s Msg:%s",
    //     state.c_str(),
    //     message.c_str())
    LogDatabaseDiagnostic(
        m_impl->databaseId, "query", connection, m_impl->result, statusCode,
        runtime::logging::WideSqlStatementKind(query), state
    );

    const auto& config = GetSqlPoolManager().m_impl->config;
    if (state == "42883") {
        const std::uint32_t now = runtime::GetTickCount();
        std::lock_guard<std::mutex> lock(g_missingProcedureAlertMutex);
        if (static_cast<std::uint32_t>(now - g_lastMissingProcedureAlertTick) > kMissingProcedureIntervalMilliseconds) {
            if (config.missingProcedureReporter) {
                const char* const catalog = connection == nullptr ? nullptr : PQdb(connection);
                config.missingProcedureReporter(
                    database::MissingProcedureReport{query, state,
                        // PostgreSQL has no ODBC native-error integer. 42883
                        // is the direct equivalent of the target's 2812 gate,
                        // so keep that legacy alert field.
                        2812, message, catalog == nullptr ? std::string() : catalog}
                );
            }
            g_lastMissingProcedureAlertTick = now;
        }
    }

    // Only SQL state 08S01 is fatal. Other SQL errors are reported.
    // PostgreSQL uses 08P01 for missing bound values and keeps the connection.
    const bool fatal = state == "08S01";
    if (fatal && config.terminateOnFatalDatabaseError) {
        if (config.fatalErrorHandler) {
            config.fatalErrorHandler(state, message);
        }
        std::abort();
    }
}
