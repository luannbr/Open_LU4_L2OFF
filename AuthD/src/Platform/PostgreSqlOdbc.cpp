// Portable database transport for the recovered ODBC call sites. This is a
// host-boundary replacement, not a source owner recovered from L2Authd.pdb:
// product code keeps its exact ODBC statements, parameter numbers and
// transaction calls while this unit translates them to PostgreSQL/libpq.
#include "Odbc.h"

#include "Shared/src/Runtime/WindowsAnsi.h"
#include "Shared/src/Runtime/PostgreSQLNoticeLog.h"
#include "Shared/src/Runtime/OperatorLog.h"
#include "Shared/src/TextRuntime.h"
#include "Log.h"

#include <libpq-fe.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace authd::platform {
namespace {

void ReceiveDatabaseNotice(void*, const PGresult* result) noexcept {
    const int savedErrno = errno;
    const auto record = runtime::logging::FormatPostgreSQLNotice(result);
    try {
        // Log.AddLog(record.error ? LOG_ERROR : record.warning ? LOG_WARN : LOG_NORMAL,
        //     "component=database event=server_notice adapter=postgresql_odbc %s", record.text.data())
        // Log.AddLog(record.error ? LOG_ERROR : record.warning ? LOG_WARN : LOG_NORMAL,
        //     "[database] %s", record.text.data())
        // L2_LOG(Log, record.error ? LOG_ERROR : record.warning ? LOG_WARN : LOG_NORMAL,
        //     "[database] %s", record.text.data())
        const LogType type = record.error ? LOG_ERROR : record.warning ? LOG_WARN :
            record.debug ? LOG_DEBUG : LOG_NORMAL;
        L2_LOG(Log, type,
            "[database] %s", record.text.data());
        runtime::logging::ForEachPostgreSQLDiagnosticFragment(result,
            [&](const char* label, std::uint64_t sequence, std::size_t part,
                std::size_t total, std::string_view fragment) {
                // const LogType type = record.error ? LOG_ERROR : record.warning ? LOG_WARN : LOG_NORMAL
                // Use the heading's severity for every unchanged payload fragment.
                if (total == 1) {
                    // Log.AddLog(type, "[database] PostgreSQL %s: \"%.*s\"",
                    //     label, static_cast<int>(fragment.size()), fragment.data())
                    L2_LOG(Log, type, "[database] PostgreSQL %s: \"%.*s\"",
                        label, static_cast<int>(fragment.size()), fragment.data());
                } else {
                    // Log.AddLog(type, "[database] PostgreSQL %s %llu, part %zu/%zu: \"%.*s\"",
                    //     label, static_cast<unsigned long long>(sequence), part, total,
                    //     static_cast<int>(fragment.size()), fragment.data())
                    // L2_LOG(Log, type, "[database] PostgreSQL %s %llu, part %zu/%zu: \"%.*s\"",
                    //     label, static_cast<unsigned long long>(sequence), part, total,
                    //     static_cast<int>(fragment.size()), fragment.data())
                    L2_LOG(Log, type, "[database] PostgreSQL %s %llu, part %llu/%llu: \"%.*s\"",
                        label, static_cast<unsigned long long>(sequence),
                        static_cast<unsigned long long>(part),
                        static_cast<unsigned long long>(total),
                        static_cast<int>(fragment.size()), fragment.data());
                }
            });
    } catch (...) {
        // runtime::logging::Write("authd", runtime::logging::Level::Error,
        //     "component=database event=notice_sink_failed details=redacted")
        // runtime::logging::Write("authd", runtime::logging::Level::Error,
        //     "[database] Could not write a server notice: the log sink threw an exception.")
        runtime::logging::Write("authd", runtime::logging::Level::Error,
            "[database] Could not write a server notice: the log sink threw an exception.", __FILE__);
    }
    errno = savedErrno;
}

constexpr OdbcReturn kSqlSuccess = 0;
constexpr OdbcReturn kSqlSuccessWithInfo = 1;
constexpr OdbcReturn kSqlError = -1;
constexpr OdbcReturn kSqlInvalidHandle = -2;
constexpr OdbcReturn kSqlNoData = 100;

constexpr OdbcSmallInt kEnvironmentHandle = 1;
constexpr OdbcSmallInt kConnectionHandle = 2;
constexpr OdbcSmallInt kStatementHandle = 3;

constexpr OdbcSmallInt kParameterInput = 1;
constexpr OdbcSmallInt kParameterInputOutput = 2;
constexpr OdbcSmallInt kParameterOutput = 4;

constexpr OdbcSmallInt kCChar = 1;
constexpr OdbcSmallInt kCBinary = -2;
constexpr OdbcSmallInt kCBit = -7;
constexpr OdbcSmallInt kCTinyInt = -6;
constexpr OdbcSmallInt kCUTinyInt = -28;
constexpr OdbcSmallInt kCShort = 5;
constexpr OdbcSmallInt kCInt = 4;
constexpr OdbcSmallInt kCUInt = -18;
constexpr OdbcSmallInt kCTimestamp = 93;

constexpr OdbcSmallInt kSqlBit = -7;
constexpr OdbcSmallInt kSqlTinyInt = -6;
constexpr OdbcSmallInt kSqlSmallInt = 5;
constexpr OdbcSmallInt kSqlInteger = 4;
constexpr OdbcSmallInt kSqlBinary = -2;
constexpr OdbcSmallInt kSqlVarBinary = -3;
constexpr OdbcSmallInt kSqlTimestamp = 93;
constexpr OdbcSmallInt kSqlTypeTimestamp = 11;

constexpr OdbcLen kSqlNullData = -1;
constexpr OdbcLen kSqlNts = -3;

constexpr OdbcInteger kAutoCommitAttribute = 102;
constexpr OdbcInteger kLoginTimeoutAttribute = 103;
constexpr OdbcInteger kConnectionTimeoutAttribute = 113;
constexpr OdbcUSmallInt kRollback = 1;

constexpr OdbcUSmallInt kCloseStatement = 0;
constexpr OdbcUSmallInt kUnbindColumns = 2;
constexpr OdbcUSmallInt kResetParameters = 3;

constexpr std::uint64_t kHandleMagic = UINT64_C(0x4155544844504748);
constexpr std::string_view kDefaultSchema = "authd";

enum class HandleKind : std::uint8_t {
    Environment,
    Connection,
    Statement,
};

struct Diagnostic {
    std::string state;
    OdbcInteger nativeError = 0;
    std::string message;

    void Clear() {
        state.clear();
        nativeError = 0;
        message.clear();
    }

    void Set(
        std::string newState,
        std::string newMessage,
        OdbcInteger newNativeError = 0) {
        state = std::move(newState);
        nativeError = newNativeError;
        message = std::move(newMessage);
        while (!message.empty() &&
               (message.back() == '\n' || message.back() == '\r')) {
            message.pop_back();
        }
    }
};

struct Handle {
    explicit Handle(HandleKind handleKind)
        : kind(handleKind) {}
    virtual ~Handle() = default;

    std::uint64_t magic = kHandleMagic;
    HandleKind kind;
    Diagnostic diagnostic;
};

struct EnvironmentHandle final : Handle {
    EnvironmentHandle()
        : Handle(HandleKind::Environment) {}
};

struct ConnectionHandle final : Handle {
    explicit ConnectionHandle(EnvironmentHandle* owner)
        : Handle(HandleKind::Connection),
          environment(owner) {}

    ~ConnectionHandle() override {
        if (connection != nullptr) {
            PQfinish(connection);
        }
    }

    EnvironmentHandle* environment = nullptr;
    PGconn* connection = nullptr;
    bool autoCommit = true;
    bool transactionActive = false;
    unsigned int loginTimeoutSeconds = 60;
    unsigned int connectionTimeoutSeconds = 60;
    std::string schema{kDefaultSchema};
};

struct ColumnBinding {
    OdbcUSmallInt ordinal = 0;
    OdbcSmallInt targetType = 0;
    void* value = nullptr;
    OdbcLen capacity = 0;
    OdbcLen* indicator = nullptr;
};

struct ParameterBinding {
    OdbcUSmallInt ordinal = 0;
    OdbcSmallInt direction = 0;
    OdbcSmallInt valueType = 0;
    OdbcSmallInt parameterType = 0;
    OdbcUInteger columnSize = 0;
    OdbcSmallInt decimalDigits = 0;
    void* value = nullptr;
    OdbcLen bufferLength = 0;
    OdbcLen* indicator = nullptr;
};

struct StatementHandle final : Handle {
    explicit StatementHandle(ConnectionHandle* owner)
        : Handle(HandleKind::Statement),
          connection(owner) {}

    ~StatementHandle() override {
        if (result != nullptr) {
            PQclear(result);
        }
    }

    void ClearResult() {
        if (result != nullptr) {
            PQclear(result);
            result = nullptr;
        }
        nextRow = 0;
        outputColumnCount = 0;
        exposesResultSet = false;
        resultColumnByOrdinal.clear();
    }

    ConnectionHandle* connection = nullptr;
    std::string preparedStatement;
    std::vector<ColumnBinding> columns;
    std::vector<ParameterBinding> parameters;
    PGresult* result = nullptr;
    int nextRow = 0;
    int outputColumnCount = 0;
    bool exposesResultSet = false;
    std::vector<int> resultColumnByOrdinal;
};

template <typename Type>
Type* CheckedHandle(void* raw, HandleKind expected) {
    if (raw == nullptr) {
        return nullptr;
    }
    auto* base = static_cast<Handle*>(raw);
    if (base->magic != kHandleMagic || base->kind != expected) {
        return nullptr;
    }
    return static_cast<Type*>(base);
}

std::string_view Trim(std::string_view value) {
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return value;
}

char LowerAscii(char value) {
    return static_cast<char>(
        std::tolower(static_cast<unsigned char>(value)));
}

std::string LowerAscii(std::string_view value) {
    std::string result(value);
    std::transform(
        result.begin(),
        result.end(),
        result.begin(),
        [](char character) { return LowerAscii(character); });
    return result;
}

bool StartsWithInsensitive(
    std::string_view value,
    std::string_view prefix) {
    if (value.size() < prefix.size()) {
        return false;
    }
    for (std::size_t index = 0; index < prefix.size(); ++index) {
        if (LowerAscii(value[index]) != LowerAscii(prefix[index])) {
            return false;
        }
    }
    return true;
}

bool IsIdentifier(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    return std::all_of(
        value.begin(),
        value.end(),
        [](char character) {
            const unsigned char byte =
                static_cast<unsigned char>(character);
            return std::isalnum(byte) != 0 || character == '_';
        });
}

std::string UnquoteIdentifier(std::string_view value) {
    value = Trim(value);
    if (value.size() >= 2 &&
        ((value.front() == '[' && value.back() == ']') ||
         (value.front() == '"' && value.back() == '"'))) {
        value.remove_prefix(1);
        value.remove_suffix(1);
    }
    return LowerAscii(value);
}

std::string QuoteIdentifier(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('"');
    result.append(value);
    result.push_back('"');
    return result;
}

std::vector<std::string> SplitQualifiedIdentifier(
    std::string_view value) {
    std::vector<std::string> result;
    std::size_t begin = 0;
    bool bracketed = false;
    bool quoted = false;
    for (std::size_t index = 0; index <= value.size(); ++index) {
        if (index == value.size() ||
            (value[index] == '.' && !bracketed && !quoted)) {
            result.push_back(
                UnquoteIdentifier(value.substr(begin, index - begin)));
            begin = index + 1;
            continue;
        }
        if (value[index] == '[' && !quoted) bracketed = true;
        if (value[index] == ']' && !quoted) bracketed = false;
        if (value[index] == '"' && !bracketed) quoted = !quoted;
    }
    return result;
}

bool SplitArguments(
    std::string_view value,
    std::vector<std::string_view>& arguments) {
    value = Trim(value);
    if (value.empty()) {
        return true;
    }
    std::size_t begin = 0;
    int nesting = 0;
    bool quoted = false;
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char character = value[index];
        if (character == '\'' &&
            (index + 1 >= value.size() || value[index + 1] != '\'')) {
            quoted = !quoted;
            continue;
        }
        if (quoted && character == '\'' &&
            index + 1 < value.size() && value[index + 1] == '\'') {
            ++index;
            continue;
        }
        if (quoted) continue;
        if (character == '(') ++nesting;
        if (character == ')') --nesting;
        if (nesting < 0) return false;
        if (character == ',' && nesting == 0) {
            arguments.push_back(Trim(value.substr(begin, index - begin)));
            begin = index + 1;
        }
    }
    if (quoted || nesting != 0) {
        return false;
    }
    arguments.push_back(Trim(value.substr(begin)));
    return std::none_of(
        arguments.begin(),
        arguments.end(),
        [](std::string_view argument) { return argument.empty(); });
}

ParameterBinding* FindParameter(
    StatementHandle& statement,
    OdbcUSmallInt ordinal) {
    const auto found = std::find_if(
        statement.parameters.begin(),
        statement.parameters.end(),
        [ordinal](const ParameterBinding& binding) {
            return binding.ordinal == ordinal;
        });
    return found == statement.parameters.end() ? nullptr : &*found;
}

const char* PostgreSqlCast(const ParameterBinding& binding) {
    if (binding.valueType == kCBinary ||
        binding.parameterType == kSqlBinary ||
        binding.parameterType == kSqlVarBinary) {
        return "bytea";
    }
    if (binding.valueType == kCTimestamp ||
        binding.parameterType == kSqlTimestamp ||
        binding.parameterType == kSqlTypeTimestamp) {
        return "timestamp without time zone";
    }
    if (binding.valueType == kCBit || binding.parameterType == kSqlBit) {
        return "boolean";
    }
    if (binding.parameterType == kSqlTinyInt ||
        binding.parameterType == kSqlSmallInt ||
        binding.valueType == kCTinyInt ||
        binding.valueType == kCUTinyInt ||
        binding.valueType == kCShort) {
        return "smallint";
    }
    if (binding.parameterType == kSqlInteger ||
        binding.valueType == kCInt ||
        binding.valueType == kCUInt) {
        return "integer";
    }
    return "text";
}

bool IsInput(const ParameterBinding& binding) {
    return binding.direction == kParameterInput ||
           binding.direction == kParameterInputOutput;
}

bool IsOutput(const ParameterBinding& binding) {
    return binding.direction == kParameterOutput ||
           binding.direction == kParameterInputOutput;
}

struct TranslatedCall {
    std::string sql;
    std::vector<ParameterBinding*> inputs;
    std::vector<ParameterBinding*> outputs;
    std::vector<int> outputColumns;
    std::vector<int> resultColumnByOrdinal;
};

bool NormalizeLiteral(
    std::string_view value,
    std::string& normalized) {
    value = Trim(value);
    if (value.empty()) return false;
    if (StartsWithInsensitive(value, "null") && value.size() == 4) {
        normalized = "NULL";
        return true;
    }
    std::size_t offset = 0;
    if (value.front() == '+' || value.front() == '-') ++offset;
    if (offset < value.size() &&
        std::all_of(
            value.begin() + static_cast<std::ptrdiff_t>(offset),
            value.end(),
            [](char character) {
                return std::isdigit(
                           static_cast<unsigned char>(character)) != 0;
            })) {
        normalized.assign(value);
        normalized += "::integer";
        return true;
    }
    if (value.size() >= 2 &&
        (value.front() == 'N' || value.front() == 'n') &&
        value[1] == '\'') {
        value.remove_prefix(1);
    }
    if (value.size() >= 2 &&
        value.front() == '\'' && value.back() == '\'') {
        normalized.assign(value);
        return true;
    }
    return false;
}

bool TranslateProcedureCall(
    StatementHandle& statement,
    std::string_view original,
    TranslatedCall& translated) {
    std::string_view query = Trim(original);
    if (query.size() < 2 || query.front() != '{' || query.back() != '}') {
        statement.diagnostic.Set(
            "42000",
            "AuthD PostgreSQL transport accepts the shipping ODBC procedure-call syntax only");
        return false;
    }
    query.remove_prefix(1);
    query.remove_suffix(1);
    query = Trim(query);

    bool hasReturnValue = false;
    OdbcUSmallInt argumentOrdinal = 1;
    if (!query.empty() && query.front() == '?') {
        const std::size_t equals = query.find('=');
        if (equals == std::string_view::npos) {
            statement.diagnostic.Set("42000", "Malformed ODBC return-value call");
            return false;
        }
        hasReturnValue = true;
        ParameterBinding* returnBinding = FindParameter(statement, 1);
        if (returnBinding == nullptr || !IsOutput(*returnBinding)) {
            statement.diagnostic.Set("07002", "Missing bound procedure return value");
            return false;
        }
        translated.outputs.push_back(returnBinding);
        argumentOrdinal = 2;
        query = Trim(query.substr(equals + 1));
    }

    if (!StartsWithInsensitive(query, "call")) {
        statement.diagnostic.Set("42000", "Malformed ODBC CALL escape");
        return false;
    }
    query = Trim(query.substr(4));

    const std::size_t open = query.find('(');
    const std::string_view qualified = Trim(
        open == std::string_view::npos ? query : query.substr(0, open));
    const std::vector<std::string> components =
        SplitQualifiedIdentifier(qualified);
    if (components.empty() || !IsIdentifier(components.back())) {
        statement.diagnostic.Set("42000", "Invalid procedure identifier");
        return false;
    }

    std::string_view argumentText;
    if (open != std::string_view::npos) {
        const std::size_t close = query.rfind(')');
        if (close == std::string_view::npos || close < open ||
            !Trim(query.substr(close + 1)).empty()) {
            statement.diagnostic.Set("42000", "Malformed procedure argument list");
            return false;
        }
        argumentText = query.substr(open + 1, close - open - 1);
    }
    std::vector<std::string_view> arguments;
    if (!SplitArguments(argumentText, arguments)) {
        statement.diagnostic.Set("42000", "Malformed procedure argument list");
        return false;
    }

    std::vector<std::string> callArguments;
    for (const std::string_view argument : arguments) {
        if (argument == "?") {
            ParameterBinding* binding =
                FindParameter(statement, argumentOrdinal++);
            if (binding == nullptr) {
                statement.diagnostic.Set("07002", "Missing bound procedure parameter");
                return false;
            }
            if (IsInput(*binding)) {
                translated.inputs.push_back(binding);
                std::string placeholder = "$";
                placeholder += std::to_string(translated.inputs.size());
                placeholder += "::";
                placeholder += PostgreSqlCast(*binding);
                callArguments.push_back(std::move(placeholder));
            }
            if (IsOutput(*binding)) {
                translated.outputs.push_back(binding);
            }
            continue;
        }

        std::string literal;
        if (!NormalizeLiteral(argument, literal)) {
            statement.diagnostic.Set(
                "42000",
                "Unsupported literal in shipping procedure call");
            return false;
        }
        callArguments.push_back(std::move(literal));
    }

    const std::string procedure = components.back();
    const std::string schema = statement.connection->schema;
    if (!IsIdentifier(schema)) {
        statement.diagnostic.Set("3F000", "Invalid AuthD PostgreSQL schema");
        return false;
    }
    translated.sql = "SELECT * FROM ";
    translated.sql += QuoteIdentifier(schema);
    translated.sql.push_back('.');
    translated.sql += QuoteIdentifier(components.back());
    translated.sql.push_back('(');
    for (std::size_t index = 0; index < callArguments.size(); ++index) {
        if (index != 0) translated.sql += ", ";
        translated.sql += callArguments[index];
    }
    translated.sql.push_back(')');

    if (!hasReturnValue) {
        std::sort(
            translated.outputs.begin(),
            translated.outputs.end(),
            [](const ParameterBinding* left, const ParameterBinding* right) {
                return left->ordinal < right->ordinal;
            });
    }

    translated.outputColumns.reserve(translated.outputs.size());
    for (std::size_t index = 0; index < translated.outputs.size(); ++index) {
        int resultColumn = static_cast<int>(index);
        if (procedure == "ap_gstat") {
            const ParameterBinding& output = *translated.outputs[index];
            // ap_GStat has nine invariant outputs followed by two independently
            // configured optional outputs. PostgreSQL exposes one main
            // eleven-column row. Map the active-only deployment shape without
            // changing the recovered CAccount call site.
            if (output.ordinal >= 11) {
                resultColumn = output.valueType == kCBinary ? 9 : 10;
            }
        }
        translated.outputColumns.push_back(resultColumn);
    }

    OdbcUSmallInt highestOrdinal = 0;
    for (const ColumnBinding& binding : statement.columns) {
        highestOrdinal = std::max(highestOrdinal, binding.ordinal);
    }
    translated.resultColumnByOrdinal.assign(
        static_cast<std::size_t>(highestOrdinal) + 1,
        -1);
    for (const ColumnBinding& binding : statement.columns) {
        int resultColumn = static_cast<int>(binding.ordinal) - 1;
        if (procedure == "ap_gstatetc" || procedure == "ap_gstatetct") {
            // The selected EXE binds a deployment-dependent projection:
            // security card only, opaque user data only, or both. The portable
            // function has one stable row shape, so project by the target C
            // type that the shipping code bound for each field.
            if (binding.targetType == kCBinary) {
                resultColumn = 1;
            } else if (binding.targetType == kCShort) {
                resultColumn = 2;
            } else {
                resultColumn = 0;
            }
        }
        translated.resultColumnByOrdinal[binding.ordinal] = resultColumn;
    }
    return true;
}

std::string WindowsAnsiToUtf8(
    const char* value,
    std::size_t length) {
    if (value == nullptr || length == 0) return {};
    const int sourceLength = static_cast<int>(
        std::min<std::size_t>(length, static_cast<std::size_t>(INT_MAX)));
    const int required = portable::WindowsAnsiToWideChar(
        value,
        sourceLength,
        static_cast<char16_t*>(nullptr),
        0);
    if (required <= 0) {
        return std::string(value, static_cast<std::size_t>(sourceLength));
    }
    std::u16string converted(static_cast<std::size_t>(required), u'\0');
    if (portable::WindowsAnsiToWideChar(
            value,
            sourceLength,
            converted.data(),
            required) <= 0) {
        return std::string(value, static_cast<std::size_t>(sourceLength));
    }
    return Utf16ToUtf8(converted);
}

std::string Utf8ToWindowsAnsi(std::string_view value) {
    if (value.empty()) return {};
    const std::u16string converted = Utf8ToUtf16(value);
    const int sourceLength = static_cast<int>(
        std::min<std::size_t>(
            converted.size(),
            static_cast<std::size_t>(INT_MAX)));
    const int required = portable::WideCharToWindowsAnsi(
        converted.data(),
        sourceLength,
        nullptr,
        0);
    if (required <= 0) return std::string(value);
    std::string result(static_cast<std::size_t>(required), '\0');
    if (portable::WideCharToWindowsAnsi(
            converted.data(),
            sourceLength,
            result.data(),
            required) <= 0) {
        return std::string(value);
    }
    return result;
}

template <typename Type>
Type LoadScalar(const void* value) {
    Type result{};
    if (value != nullptr) {
        std::memcpy(&result, value, sizeof(result));
    }
    return result;
}

std::size_t BoundLength(const ParameterBinding& binding) {
    if (binding.indicator != nullptr) {
        if (*binding.indicator >= 0) {
            return static_cast<std::size_t>(*binding.indicator);
        }
        if (*binding.indicator == kSqlNts && binding.value != nullptr) {
            return std::strlen(static_cast<const char*>(binding.value));
        }
    }
    if (binding.valueType == kCChar && binding.value != nullptr) {
        return std::strlen(static_cast<const char*>(binding.value));
    }
    if (binding.bufferLength > 0) {
        return static_cast<std::size_t>(binding.bufferLength);
    }
    return static_cast<std::size_t>(binding.columnSize);
}

struct MaterializedParameter {
    bool isNull = false;
    bool binary = false;
    std::string storage;
};

bool MaterializeParameter(
    const ParameterBinding& binding,
    MaterializedParameter& materialized) {
    if (binding.indicator != nullptr &&
        *binding.indicator == kSqlNullData) {
        materialized.isNull = true;
        return true;
    }
    if (binding.value == nullptr) {
        materialized.isNull = true;
        return true;
    }

    switch (binding.valueType) {
    case kCChar: {
        const std::size_t length = BoundLength(binding);
        materialized.storage = WindowsAnsiToUtf8(
            static_cast<const char*>(binding.value),
            length);
        return true;
    }
    case kCBinary: {
        const std::size_t length = BoundLength(binding);
        materialized.binary = true;
        materialized.storage.assign(
            static_cast<const char*>(binding.value),
            length);
        return true;
    }
    case kCBit:
        materialized.storage =
            LoadScalar<unsigned char>(binding.value) == 0 ? "false" : "true";
        return true;
    case kCTinyInt:
        materialized.storage = std::to_string(
            static_cast<int>(LoadScalar<std::int8_t>(binding.value)));
        return true;
    case kCUTinyInt:
        materialized.storage = std::to_string(
            static_cast<unsigned int>(
                LoadScalar<std::uint8_t>(binding.value)));
        return true;
    case kCShort:
        materialized.storage = std::to_string(
            LoadScalar<std::int16_t>(binding.value));
        return true;
    case kCInt:
        materialized.storage = std::to_string(
            LoadScalar<std::int32_t>(binding.value));
        return true;
    case kCUInt:
        materialized.storage = std::to_string(
            LoadScalar<std::uint32_t>(binding.value));
        return true;
    case kCTimestamp: {
        const auto timestamp =
            LoadScalar<tagTIMESTAMP_STRUCT>(binding.value);
        std::array<char, 64> text{};
        std::snprintf(
            text.data(),
            text.size(),
            "%04d-%02u-%02u %02u:%02u:%02u.%09u",
            static_cast<int>(timestamp.year),
            static_cast<unsigned int>(timestamp.month),
            static_cast<unsigned int>(timestamp.day),
            static_cast<unsigned int>(timestamp.hour),
            static_cast<unsigned int>(timestamp.minute),
            static_cast<unsigned int>(timestamp.second),
            static_cast<unsigned int>(timestamp.fraction));
        materialized.storage = text.data();
        return true;
    }
    default:
        return false;
    }
}

std::string CompatibleSqlState(const char* sqlState) {
    if (sqlState == nullptr || *sqlState == '\0') {
        return "HY000";
    }
    const std::string_view state(sqlState);
    if (state.size() >= 2 && state.substr(0, 2) == "23") {
        return "23000";
    }
    if (state == "28P01") {
        return "28000";
    }
    if (state.size() >= 2 && state.substr(0, 2) == "08") {
        return "08S01";
    }
    return std::string(state.substr(0, std::min<std::size_t>(5, state.size())));
}

void SetResultDiagnostic(
    Handle& handle,
    PGconn* connection,
    PGresult* result) {
    const char* state = result == nullptr
        ? nullptr
        : PQresultErrorField(result, PG_DIAG_SQLSTATE);
    const char* message = result == nullptr
        ? (connection == nullptr ? "PostgreSQL operation failed"
                                 : PQerrorMessage(connection))
        : PQresultErrorMessage(result);
    handle.diagnostic.Set(
        CompatibleSqlState(state),
        message == nullptr ? "PostgreSQL operation failed" : message);
}

bool PostgreSqlSuccess(ExecStatusType status) {
    return status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK;
}

bool ExecuteConnectionCommand(
    ConnectionHandle& connection,
    const char* command) {
    PGresult* result = PQexec(connection.connection, command);
    const bool success = result != nullptr &&
        PostgreSqlSuccess(PQresultStatus(result));
    if (!success) {
        SetResultDiagnostic(connection, connection.connection, result);
    }
    if (result != nullptr) PQclear(result);
    return success;
}

bool EnsureTransaction(ConnectionHandle& connection) {
    if (connection.autoCommit || connection.transactionActive) {
        return true;
    }
    if (!ExecuteConnectionCommand(connection, "BEGIN")) {
        return false;
    }
    connection.transactionActive = true;
    return true;
}

bool DecodeBytea(std::string_view value, std::string& decoded) {
    const std::string encoded(value);
    std::size_t size = 0;
    unsigned char* bytes = PQunescapeBytea(
        reinterpret_cast<const unsigned char*>(encoded.c_str()),
        &size);
    if (bytes == nullptr) return false;
    decoded.assign(reinterpret_cast<const char*>(bytes), size);
    PQfreemem(bytes);
    return true;
}

bool ParseTimestamp(
    std::string_view value,
    tagTIMESTAMP_STRUCT& timestamp) {
    std::string text(value);
    int year = 0;
    unsigned int month = 0;
    unsigned int day = 0;
    unsigned int hour = 0;
    unsigned int minute = 0;
    unsigned int second = 0;
    int consumed = 0;
    if (std::sscanf(
            text.c_str(),
            "%d-%u-%u %u:%u:%u%n",
            &year,
            &month,
            &day,
            &hour,
            &minute,
            &second,
            &consumed) != 6) {
        return false;
    }
    std::uint32_t fraction = 0;
    if (consumed >= 0 &&
        static_cast<std::size_t>(consumed) < text.size() &&
        text[static_cast<std::size_t>(consumed)] == '.') {
        std::size_t position = static_cast<std::size_t>(consumed) + 1;
        unsigned int digits = 0;
        while (position < text.size() &&
               std::isdigit(static_cast<unsigned char>(text[position])) != 0 &&
               digits < 9) {
            fraction = fraction * 10U +
                static_cast<unsigned int>(text[position] - '0');
            ++position;
            ++digits;
        }
        while (digits++ < 9) fraction *= 10U;
    }
    if (year < std::numeric_limits<std::int16_t>::min() ||
        year > std::numeric_limits<std::int16_t>::max() ||
        month > std::numeric_limits<std::uint16_t>::max() ||
        day > std::numeric_limits<std::uint16_t>::max() ||
        hour > std::numeric_limits<std::uint16_t>::max() ||
        minute > std::numeric_limits<std::uint16_t>::max() ||
        second > std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }
    timestamp.year = static_cast<std::int16_t>(year);
    timestamp.month = static_cast<std::uint16_t>(month);
    timestamp.day = static_cast<std::uint16_t>(day);
    timestamp.hour = static_cast<std::uint16_t>(hour);
    timestamp.minute = static_cast<std::uint16_t>(minute);
    timestamp.second = static_cast<std::uint16_t>(second);
    timestamp.fraction = fraction;
    return true;
}

bool ParseSigned(std::string_view value, std::int64_t& result) {
    std::string text(value);
    char* end = nullptr;
    errno = 0;
    const long long parsed = std::strtoll(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0') return false;
    result = static_cast<std::int64_t>(parsed);
    return true;
}

bool ParseUnsigned(std::string_view value, std::uint64_t& result) {
    if (!value.empty() && value.front() == '-') return false;
    std::string text(value);
    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0') return false;
    result = static_cast<std::uint64_t>(parsed);
    return true;
}

void SetIndicator(OdbcLen* indicator, OdbcLen value) {
    if (indicator != nullptr) *indicator = value;
}

bool ApplyValue(
    OdbcSmallInt targetType,
    void* destination,
    OdbcLen capacity,
    OdbcLen* indicator,
    bool isNull,
    std::string_view value) {
    if (isNull) {
        SetIndicator(indicator, kSqlNullData);
        return true;
    }
    if (destination == nullptr) return false;

    switch (targetType) {
    case kCChar: {
        const std::string encoded = Utf8ToWindowsAnsi(value);
        const std::size_t available = capacity <= 0
            ? 0
            : static_cast<std::size_t>(capacity);
        if (available != 0) {
            const std::size_t copied = std::min(
                encoded.size(),
                available - 1);
            std::memcpy(destination, encoded.data(), copied);
            static_cast<char*>(destination)[copied] = '\0';
        }
        SetIndicator(
            indicator,
            static_cast<OdbcLen>(std::min<std::size_t>(
                encoded.size(),
                static_cast<std::size_t>(
                    std::numeric_limits<OdbcLen>::max()))));
        return true;
    }
    case kCBinary: {
        std::string decoded;
        if (!DecodeBytea(value, decoded)) return false;
        const std::size_t available = capacity <= 0
            ? 0
            : static_cast<std::size_t>(capacity);
        const std::size_t copied = std::min(decoded.size(), available);
        if (copied != 0) std::memcpy(destination, decoded.data(), copied);
        SetIndicator(
            indicator,
            static_cast<OdbcLen>(std::min<std::size_t>(
                decoded.size(),
                static_cast<std::size_t>(
                    std::numeric_limits<OdbcLen>::max()))));
        return true;
    }
    case kCBit: {
        const bool parsed = value == "t" || value == "true" || value == "1";
        const unsigned char byte = parsed ? 1U : 0U;
        std::memcpy(destination, &byte, sizeof(byte));
        SetIndicator(indicator, sizeof(byte));
        return true;
    }
    case kCTinyInt: {
        std::int64_t parsed = 0;
        if (!ParseSigned(value, parsed)) return false;
        const std::int8_t result = static_cast<std::int8_t>(parsed);
        std::memcpy(destination, &result, sizeof(result));
        SetIndicator(indicator, sizeof(result));
        return true;
    }
    case kCUTinyInt: {
        std::uint64_t parsed = 0;
        if (!ParseUnsigned(value, parsed)) return false;
        const std::uint8_t result = static_cast<std::uint8_t>(parsed);
        std::memcpy(destination, &result, sizeof(result));
        SetIndicator(indicator, sizeof(result));
        return true;
    }
    case kCShort: {
        std::int64_t parsed = 0;
        if (!ParseSigned(value, parsed)) return false;
        const std::int16_t result = static_cast<std::int16_t>(parsed);
        std::memcpy(destination, &result, sizeof(result));
        SetIndicator(indicator, sizeof(result));
        return true;
    }
    case kCInt: {
        std::int64_t parsed = 0;
        if (!ParseSigned(value, parsed)) return false;
        const std::int32_t result = static_cast<std::int32_t>(parsed);
        std::memcpy(destination, &result, sizeof(result));
        SetIndicator(indicator, sizeof(result));
        return true;
    }
    case kCUInt: {
        std::uint64_t parsed = 0;
        if (!ParseUnsigned(value, parsed)) return false;
        const std::uint32_t result = static_cast<std::uint32_t>(parsed);
        std::memcpy(destination, &result, sizeof(result));
        SetIndicator(indicator, sizeof(result));
        return true;
    }
    case kCTimestamp: {
        tagTIMESTAMP_STRUCT timestamp{};
        if (!ParseTimestamp(value, timestamp)) return false;
        std::memcpy(destination, &timestamp, sizeof(timestamp));
        SetIndicator(indicator, sizeof(timestamp));
        return true;
    }
    default:
        return false;
    }
}

bool ApplyParameterOutput(
    ParameterBinding& binding,
    PGresult* result,
    int column) {
    const bool isNull = PQgetisnull(result, 0, column) != 0;
    const std::string_view value = isNull
        ? std::string_view()
        : std::string_view(
              PQgetvalue(result, 0, column),
              static_cast<std::size_t>(PQgetlength(result, 0, column)));
    OdbcLen capacity = binding.bufferLength;
    if (capacity <= 0 &&
        (binding.valueType == kCChar || binding.valueType == kCBinary)) {
        capacity = static_cast<OdbcLen>(binding.columnSize);
        if (binding.valueType == kCChar && capacity > 0) ++capacity;
    }
    return ApplyValue(
        binding.valueType,
        binding.value,
        capacity,
        binding.indicator,
        isNull,
        value);
}

bool ApplyColumnOutput(
    ColumnBinding& binding,
    PGresult* result,
    int row,
    int column) {
    const bool isNull = PQgetisnull(result, row, column) != 0;
    const std::string_view value = isNull
        ? std::string_view()
        : std::string_view(
              PQgetvalue(result, row, column),
              static_cast<std::size_t>(PQgetlength(result, row, column)));
    return ApplyValue(
        binding.targetType,
        binding.value,
        binding.capacity,
        binding.indicator,
        isNull,
        value);
}

OdbcReturn ExecuteStatement(
    StatementHandle& statement,
    std::string_view query) {
    statement.ClearResult();
    statement.diagnostic.Clear();
    if (statement.connection == nullptr ||
        statement.connection->connection == nullptr ||
        PQstatus(statement.connection->connection) != CONNECTION_OK) {
        statement.diagnostic.Set("08S01", "PostgreSQL connection is not open");
        return kSqlError;
    }
    if (!EnsureTransaction(*statement.connection)) {
        statement.diagnostic = statement.connection->diagnostic;
        return kSqlError;
    }

    TranslatedCall translated;
    if (!TranslateProcedureCall(statement, query, translated)) {
        return kSqlError;
    }

    std::vector<MaterializedParameter> materialized(translated.inputs.size());
    std::vector<const char*> values(translated.inputs.size(), nullptr);
    std::vector<int> lengths(translated.inputs.size(), 0);
    std::vector<int> formats(translated.inputs.size(), 0);
    for (std::size_t index = 0; index < translated.inputs.size(); ++index) {
        if (!MaterializeParameter(
                *translated.inputs[index],
                materialized[index])) {
            statement.diagnostic.Set(
                "HY003",
                "Unsupported AuthD ODBC C parameter type");
            return kSqlError;
        }
        if (materialized[index].isNull) continue;
        values[index] = materialized[index].storage.data();
        lengths[index] = static_cast<int>(std::min<std::size_t>(
            materialized[index].storage.size(),
            static_cast<std::size_t>(INT_MAX)));
        formats[index] = materialized[index].binary ? 1 : 0;
    }

    statement.result = PQexecParams(
        statement.connection->connection,
        translated.sql.c_str(),
        static_cast<int>(translated.inputs.size()),
        nullptr,
        values.empty() ? nullptr : values.data(),
        lengths.empty() ? nullptr : lengths.data(),
        formats.empty() ? nullptr : formats.data(),
        0);
    const ExecStatusType status = statement.result == nullptr
        ? PGRES_FATAL_ERROR
        : PQresultStatus(statement.result);
    if (!PostgreSqlSuccess(status)) {
        SetResultDiagnostic(
            statement,
            statement.connection->connection,
            statement.result);
        statement.connection->diagnostic = statement.diagnostic;
        return kSqlError;
    }

    if (!translated.outputs.empty()) {
        if (status != PGRES_TUPLES_OK ||
            PQntuples(statement.result) < 1 ||
            translated.outputColumns.size() != translated.outputs.size()) {
            statement.diagnostic.Set(
                "07006",
                "PostgreSQL AuthD function did not return its bound output parameters");
            return kSqlError;
        }
        for (std::size_t index = 0; index < translated.outputs.size(); ++index) {
            const int resultColumn = translated.outputColumns[index];
            if (resultColumn < 0 ||
                resultColumn >= PQnfields(statement.result)) {
                statement.diagnostic.Set(
                    "07006",
                    "PostgreSQL AuthD function did not return its bound output parameters");
                return kSqlError;
            }
            if (!ApplyParameterOutput(
                    *translated.outputs[index],
                    statement.result,
                    resultColumn)) {
                statement.diagnostic.Set(
                    "07006",
                    "Cannot convert PostgreSQL AuthD function output parameter");
                return kSqlError;
            }
        }
        statement.outputColumnCount =
            *std::max_element(
                translated.outputColumns.begin(),
                translated.outputColumns.end()) + 1;
    }

    statement.resultColumnByOrdinal =
        std::move(translated.resultColumnByOrdinal);
    statement.exposesResultSet = !statement.columns.empty();
    if (statement.exposesResultSet) {
        if (status != PGRES_TUPLES_OK) {
            statement.diagnostic.Set(
                "24000",
                "AuthD query has bound columns but returned no row set");
            return kSqlError;
        }
        int highestResultColumn = -1;
        for (const ColumnBinding& binding : statement.columns) {
            if (binding.ordinal >= statement.resultColumnByOrdinal.size()) {
                continue;
            }
            highestResultColumn = std::max(
                highestResultColumn,
                statement.resultColumnByOrdinal[binding.ordinal]);
        }
        const int required = statement.outputColumnCount +
            highestResultColumn + 1;
        if (PQnfields(statement.result) < required) {
            statement.diagnostic.Set(
                "07006",
                "PostgreSQL AuthD function returned fewer result columns than the EXE binds");
            return kSqlError;
        }
    }
    return kSqlSuccess;
}

std::uintptr_t AttributeInteger(void* value) {
    return reinterpret_cast<std::uintptr_t>(value);
}

} // namespace

OdbcReturn SQLSetEnvAttr(
    void* environment,
    OdbcInteger,
    void*,
    OdbcInteger) {
    if (environment == nullptr) {
        // SQL_ATTR_CONNECTION_POOLING is process-global and is issued before
        // allocating the target environment. libpq has no equivalent here.
        // CDBConn keeps the shipping connection lifetime instead.
        return kSqlSuccess;
    }
    return CheckedHandle<EnvironmentHandle>(
               environment,
               HandleKind::Environment) == nullptr
        ? kSqlInvalidHandle
        : kSqlSuccess;
}

OdbcReturn SQLAllocHandle(
    OdbcSmallInt handleType,
    void* inputHandle,
    void** outputHandle) {
    if (outputHandle == nullptr) return kSqlError;
    *outputHandle = nullptr;
    if (handleType == kEnvironmentHandle) {
        auto handle = std::make_unique<EnvironmentHandle>();
        *outputHandle = handle.release();
        return kSqlSuccess;
    }
    if (handleType == kConnectionHandle) {
        auto* environment = CheckedHandle<EnvironmentHandle>(
            inputHandle,
            HandleKind::Environment);
        if (environment == nullptr) return kSqlInvalidHandle;
        auto handle = std::make_unique<ConnectionHandle>(environment);
        *outputHandle = handle.release();
        return kSqlSuccess;
    }
    if (handleType == kStatementHandle) {
        auto* connection = CheckedHandle<ConnectionHandle>(
            inputHandle,
            HandleKind::Connection);
        if (connection == nullptr || connection->connection == nullptr) {
            return kSqlInvalidHandle;
        }
        auto handle = std::make_unique<StatementHandle>(connection);
        *outputHandle = handle.release();
        return kSqlSuccess;
    }
    return kSqlError;
}

OdbcReturn SQLFreeHandle(
    OdbcSmallInt handleType,
    void* handle) {
    HandleKind expected = HandleKind::Environment;
    if (handleType == kConnectionHandle) expected = HandleKind::Connection;
    else if (handleType == kStatementHandle) expected = HandleKind::Statement;
    else if (handleType != kEnvironmentHandle) return kSqlError;
    auto* base = CheckedHandle<Handle>(handle, expected);
    if (base == nullptr) return kSqlInvalidHandle;
    base->magic = 0;
    delete base;
    return kSqlSuccess;
}

OdbcReturn SQLSetConnectAttr(
    void* rawConnection,
    OdbcInteger attribute,
    void* value,
    OdbcInteger) {
    auto* connection = CheckedHandle<ConnectionHandle>(
        rawConnection,
        HandleKind::Connection);
    if (connection == nullptr) return kSqlInvalidHandle;
    connection->diagnostic.Clear();

    if (attribute == kLoginTimeoutAttribute) {
        connection->loginTimeoutSeconds = static_cast<unsigned int>(
            std::min<std::uintptr_t>(
                AttributeInteger(value),
                std::numeric_limits<unsigned int>::max()));
        return kSqlSuccess;
    }
    if (attribute == kConnectionTimeoutAttribute) {
        connection->connectionTimeoutSeconds = static_cast<unsigned int>(
            std::min<std::uintptr_t>(
                AttributeInteger(value),
                std::numeric_limits<unsigned int>::max()));
        return kSqlSuccess;
    }
    if (attribute != kAutoCommitAttribute) {
        return kSqlSuccess;
    }

    const bool enabled = AttributeInteger(value) != 0;
    if (enabled == connection->autoCommit) return kSqlSuccess;
    if (enabled && connection->transactionActive) {
        if (!ExecuteConnectionCommand(*connection, "COMMIT")) {
            return kSqlError;
        }
        connection->transactionActive = false;
    }
    connection->autoCommit = enabled;
    return kSqlSuccess;
}

OdbcReturn SQLDisconnect(void* rawConnection) {
    auto* connection = CheckedHandle<ConnectionHandle>(
        rawConnection,
        HandleKind::Connection);
    if (connection == nullptr) return kSqlInvalidHandle;
    if (connection->connection != nullptr) {
        if (connection->transactionActive) {
            PGresult* rolledBack = PQexec(connection->connection, "ROLLBACK");
            if (rolledBack != nullptr) PQclear(rolledBack);
        }
        PQfinish(connection->connection);
        connection->connection = nullptr;
    }
    connection->transactionActive = false;
    return kSqlSuccess;
}

OdbcReturn SQLEndTran(
    OdbcSmallInt handleType,
    void* rawHandle,
    OdbcSmallInt completionType) {
    if (handleType != kConnectionHandle) return kSqlError;
    auto* connection = CheckedHandle<ConnectionHandle>(
        rawHandle,
        HandleKind::Connection);
    if (connection == nullptr) return kSqlInvalidHandle;
    if (!connection->transactionActive) return kSqlSuccess;
    const char* command = completionType == kRollback ? "ROLLBACK" : "COMMIT";
    if (!ExecuteConnectionCommand(*connection, command)) return kSqlError;
    connection->transactionActive = false;
    return kSqlSuccess;
}

OdbcReturn SQLExecDirect(
    void* rawStatement,
    const char* text,
    OdbcInteger textLength) {
    auto* statement = CheckedHandle<StatementHandle>(
        rawStatement,
        HandleKind::Statement);
    if (statement == nullptr) return kSqlInvalidHandle;
    if (text == nullptr) return kSqlError;
    const std::size_t length = textLength < 0
        ? std::strlen(text)
        : static_cast<std::size_t>(textLength);
    return ExecuteStatement(*statement, std::string_view(text, length));
}

OdbcReturn SQLPrepare(
    void* rawStatement,
    const char* text,
    OdbcInteger textLength) {
    auto* statement = CheckedHandle<StatementHandle>(
        rawStatement,
        HandleKind::Statement);
    if (statement == nullptr) return kSqlInvalidHandle;
    if (text == nullptr) return kSqlError;
    const std::size_t length = textLength < 0
        ? std::strlen(text)
        : static_cast<std::size_t>(textLength);
    statement->preparedStatement.assign(text, length);
    statement->diagnostic.Clear();
    return kSqlSuccess;
}

OdbcReturn SQLExecute(void* rawStatement) {
    auto* statement = CheckedHandle<StatementHandle>(
        rawStatement,
        HandleKind::Statement);
    if (statement == nullptr) return kSqlInvalidHandle;
    if (statement->preparedStatement.empty()) {
        statement->diagnostic.Set("HY010", "SQLExecute called before SQLPrepare");
        return kSqlError;
    }
    return ExecuteStatement(*statement, statement->preparedStatement);
}

OdbcReturn SQLFetch(void* rawStatement) {
    auto* statement = CheckedHandle<StatementHandle>(
        rawStatement,
        HandleKind::Statement);
    if (statement == nullptr) return kSqlInvalidHandle;
    if (!statement->exposesResultSet || statement->result == nullptr ||
        PQresultStatus(statement->result) != PGRES_TUPLES_OK) {
        statement->diagnostic.Set("24000", "Statement has no result set");
        return kSqlError;
    }
    if (statement->nextRow >= PQntuples(statement->result)) {
        return kSqlNoData;
    }
    for (ColumnBinding& binding : statement->columns) {
        const int mappedColumn =
            binding.ordinal < statement->resultColumnByOrdinal.size()
            ? statement->resultColumnByOrdinal[binding.ordinal]
            : -1;
        const int column = statement->outputColumnCount + mappedColumn;
        if (column < 0 || column >= PQnfields(statement->result) ||
            !ApplyColumnOutput(
                binding,
                statement->result,
                statement->nextRow,
                column)) {
            statement->diagnostic.Set(
                "07006",
                "Cannot convert PostgreSQL AuthD result column");
            return kSqlError;
        }
    }
    ++statement->nextRow;
    return kSqlSuccess;
}

OdbcReturn SQLGetDiagRec(
    OdbcSmallInt handleType,
    void* rawHandle,
    OdbcSmallInt recordNumber,
    char* sqlState,
    OdbcInteger* nativeError,
    char* message,
    OdbcSmallInt messageCapacity,
    OdbcSmallInt* messageLength) {
    HandleKind expected = HandleKind::Environment;
    if (handleType == kConnectionHandle) expected = HandleKind::Connection;
    else if (handleType == kStatementHandle) expected = HandleKind::Statement;
    else if (handleType != kEnvironmentHandle) return kSqlError;
    auto* handle = CheckedHandle<Handle>(rawHandle, expected);
    if (handle == nullptr) return kSqlInvalidHandle;
    if (recordNumber != 1 || handle->diagnostic.state.empty()) {
        return kSqlNoData;
    }
    if (sqlState != nullptr) {
        std::memset(sqlState, 0, 6);
        std::memcpy(
            sqlState,
            handle->diagnostic.state.data(),
            std::min<std::size_t>(5, handle->diagnostic.state.size()));
    }
    if (nativeError != nullptr) {
        *nativeError = handle->diagnostic.nativeError;
    }
    const std::size_t fullLength = handle->diagnostic.message.size();
    const std::size_t capacity = messageCapacity <= 0
        ? 0
        : static_cast<std::size_t>(messageCapacity);
    if (message != nullptr && capacity != 0) {
        const std::size_t copied = std::min(fullLength, capacity - 1);
        std::memcpy(message, handle->diagnostic.message.data(), copied);
        message[copied] = '\0';
    }
    if (messageLength != nullptr) {
        *messageLength = static_cast<OdbcSmallInt>(
            std::min<std::size_t>(
                fullLength,
                static_cast<std::size_t>(
                    std::numeric_limits<OdbcSmallInt>::max())));
    }
    return fullLength + 1 > capacity && capacity != 0
        ? kSqlSuccessWithInfo
        : kSqlSuccess;
}

OdbcReturn SQLBindCol(
    void* rawStatement,
    OdbcUSmallInt columnNumber,
    OdbcSmallInt targetType,
    void* value,
    OdbcLen valueCapacity,
    OdbcLen* lengthOrIndicator) {
    auto* statement = CheckedHandle<StatementHandle>(
        rawStatement,
        HandleKind::Statement);
    if (statement == nullptr) return kSqlInvalidHandle;
    if (columnNumber == 0) return kSqlError;
    const auto found = std::find_if(
        statement->columns.begin(),
        statement->columns.end(),
        [columnNumber](const ColumnBinding& binding) {
            return binding.ordinal == columnNumber;
        });
    const ColumnBinding binding{
        columnNumber,
        targetType,
        value,
        valueCapacity,
        lengthOrIndicator};
    if (found == statement->columns.end()) {
        statement->columns.push_back(binding);
    } else {
        *found = binding;
    }
    return kSqlSuccess;
}

OdbcReturn SQLBindParameter(
    void* rawStatement,
    OdbcUSmallInt parameterNumber,
    OdbcSmallInt inputOutputType,
    OdbcSmallInt valueType,
    OdbcSmallInt parameterType,
    OdbcUInteger columnSize,
    OdbcSmallInt decimalDigits,
    void* parameterValue,
    OdbcLen bufferLength,
    OdbcLen* lengthOrIndicator) {
    auto* statement = CheckedHandle<StatementHandle>(
        rawStatement,
        HandleKind::Statement);
    if (statement == nullptr) return kSqlInvalidHandle;
    if (parameterNumber == 0) return kSqlError;
    const auto found = std::find_if(
        statement->parameters.begin(),
        statement->parameters.end(),
        [parameterNumber](const ParameterBinding& binding) {
            return binding.ordinal == parameterNumber;
        });
    const ParameterBinding binding{
        parameterNumber,
        inputOutputType,
        valueType,
        parameterType,
        columnSize,
        decimalDigits,
        parameterValue,
        bufferLength,
        lengthOrIndicator};
    if (found == statement->parameters.end()) {
        statement->parameters.push_back(binding);
    } else {
        *found = binding;
    }
    return kSqlSuccess;
}

OdbcReturn SQLFreeStmt(
    void* rawStatement,
    OdbcUSmallInt option) {
    auto* statement = CheckedHandle<StatementHandle>(
        rawStatement,
        HandleKind::Statement);
    if (statement == nullptr) return kSqlInvalidHandle;
    if (option == kCloseStatement) {
        statement->ClearResult();
        return kSqlSuccess;
    }
    if (option == kUnbindColumns) {
        statement->columns.clear();
        return kSqlSuccess;
    }
    if (option == kResetParameters) {
        statement->parameters.clear();
        statement->preparedStatement.clear();
        return kSqlSuccess;
    }
    return kSqlError;
}

OdbcReturn SQLDriverConnect(
    void* rawConnection,
    void*,
    const char* input,
    OdbcSmallInt inputLength,
    char* output,
    OdbcSmallInt outputCapacity,
    OdbcSmallInt* outputLength,
    OdbcUSmallInt) {
    auto* connection = CheckedHandle<ConnectionHandle>(
        rawConnection,
        HandleKind::Connection);
    if (connection == nullptr) return kSqlInvalidHandle;
    connection->diagnostic.Clear();
    if (input == nullptr) {
        connection->diagnostic.Set("08001", "Empty PostgreSQL connection string");
        return kSqlError;
    }
    const std::size_t length = inputLength < 0
        ? std::strlen(input)
        : static_cast<std::size_t>(inputLength);
    const std::string connectionString(input, length);
    if (connection->connection != nullptr) {
        if (connection->transactionActive) {
            PGresult* rolledBack = PQexec(connection->connection, "ROLLBACK");
            if (rolledBack != nullptr) PQclear(rolledBack);
        }
        PQfinish(connection->connection);
        connection->connection = nullptr;
        connection->transactionActive = false;
    }
    const std::string timeout = std::to_string(
        std::max(
            connection->loginTimeoutSeconds,
            connection->connectionTimeoutSeconds));
    const char* keywords[] = {"dbname", "connect_timeout", nullptr};
    const char* values[] = {
        connectionString.c_str(),
        timeout.c_str(),
        nullptr};
    connection->connection = PQconnectdbParams(keywords, values, 1);
    if (connection->connection == nullptr ||
        PQstatus(connection->connection) != CONNECTION_OK) {
        const char* error = connection->connection == nullptr
            ? "Cannot allocate PostgreSQL connection"
            : PQerrorMessage(connection->connection);
        connection->diagnostic.Set(
            "08001",
            error == nullptr ? "Cannot connect to PostgreSQL" : error);
        if (connection->connection != nullptr) {
            PQfinish(connection->connection);
            connection->connection = nullptr;
        }
        return kSqlError;
    }
    // Replace libpq's default raw stderr output. No handle pointer is kept
    // by the callback, including for PGresults that outlive SQLDisconnect.
    PQsetNoticeReceiver(connection->connection, ReceiveDatabaseNotice, nullptr);
    if (PQsetClientEncoding(connection->connection, "UTF8") != 0 ||
        !ExecuteConnectionCommand(
            *connection,
            "SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL READ COMMITTED")) {
        if (connection->diagnostic.state.empty()) {
            connection->diagnostic.Set(
                "HY000",
                PQerrorMessage(connection->connection));
        }
        PQfinish(connection->connection);
        connection->connection = nullptr;
        return kSqlError;
    }

    if (outputLength != nullptr) {
        *outputLength = static_cast<OdbcSmallInt>(
            std::min<std::size_t>(
                connectionString.size(),
                static_cast<std::size_t>(
                    std::numeric_limits<OdbcSmallInt>::max())));
    }
    if (output != nullptr && outputCapacity > 0) {
        const std::size_t capacity = static_cast<std::size_t>(outputCapacity);
        const std::size_t copied = std::min(
            connectionString.size(),
            capacity - 1);
        std::memcpy(output, connectionString.data(), copied);
        output[copied] = '\0';
    }
    return kSqlSuccess;
}

} // namespace authd::platform
