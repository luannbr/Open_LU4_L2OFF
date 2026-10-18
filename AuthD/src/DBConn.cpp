#include "Shared/src/Runtime/Utf8.h"
#include "DBConn.h"
#include "Shared/src/ScopedLockGuards.h"

#include "Platform/Odbc.h"
#include "Shared/src/Runtime/BoundedFormat.h"
#include "Shared/src/Runtime/ConfigDirectory.h"
#include "Shared/src/Runtime/DatabaseLog.h"
#include "Shared/src/Runtime/OperatorLog.h"
#include "Shared/src/Runtime/PostgreSQLMigrations.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Config.h"
#include "DES.h"
#include "Log.h"
#include "MailReport.h"
#include "ServiceMgr.h"
#include "Util.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>
#include <optional>
#include <thread>

extern CServiceManager g_serviceManager;

#if defined(_WIN32)
extern HINSTANCE g_instance;
#endif

DBEnv g_linDB;
DBEnv g_secuDB;
DBEnv g_GloActNcsDB;

std::int32_t CDBConn::g_nAvgTimeCost = 0;

namespace {

// Keep the supplied SQL text, including literals.
// Connection credentials stay private.
void LogSqlStatement(LogType type, int databaseId, const char* context, std::string_view statement) {
    runtime::logging::ForEachSqlFragment(statement,
        [=](std::uint64_t sequence, std::size_t part, std::size_t total, std::string_view fragment) {
            if (total == 1) {
                // Log.AddLog(type, "[database %d] %s. SQL: \"%.*s\"",
                //     databaseId, context, static_cast<int>(fragment.size()), fragment.empty() ? "" : fragment.data())
                L2_LOG(Log, type, "[database %d] %s. SQL: \"%.*s\"",
                    databaseId, context, static_cast<int>(fragment.size()), fragment.empty() ? "" : fragment.data());
            } else {
                // Log.AddLog(type, "[database %d] %s. SQL %llu, part %zu/%zu: \"%.*s\"",
                //     databaseId, context, static_cast<unsigned long long>(sequence), part, total,
                //     static_cast<int>(fragment.size()), fragment.empty() ? "" : fragment.data())
                // L2_LOG(Log, type, "[database %d] %s. SQL %llu, part %zu/%zu: \"%.*s\"",
                //     databaseId, context, static_cast<unsigned long long>(sequence), part, total,
                //     static_cast<int>(fragment.size()), fragment.empty() ? "" : fragment.data())
                L2_LOG(Log, type, "[database %d] %s. SQL %llu, part %llu/%llu: \"%.*s\"",
                    databaseId, context, static_cast<unsigned long long>(sequence),
                    static_cast<unsigned long long>(part), static_cast<unsigned long long>(total),
                    static_cast<int>(fragment.size()), fragment.empty() ? "" : fragment.data());
            }
        });
}


constexpr authd::platform::OdbcSmallInt kEnvironmentHandle = 1;
constexpr authd::platform::OdbcSmallInt kConnectionHandle = 2;
constexpr authd::platform::OdbcSmallInt kStatementHandle = 3;
constexpr authd::platform::OdbcReturn kSqlSuccess = 0;
constexpr authd::platform::OdbcReturn kSqlSuccessWithInfo = 1;
constexpr authd::platform::OdbcReturn kSqlError = -1;
constexpr authd::platform::OdbcReturn kSqlInvalidHandle = -2;
constexpr authd::platform::OdbcReturn kSqlNoData = 100;
constexpr authd::platform::OdbcInteger kSqlNts = -3;

bool IsSuccessOrInfo(
    authd::platform::OdbcReturn result) noexcept {
    return result == kSqlSuccess ||
           result == kSqlSuccessWithInfo;
}

// Statement diagnostics may include SQL literals. Retain the text supplied by
// the driver. Connection/environment diagnostics are handled separately because
// they may contain connection strings. Keep the recovered diagnostic buffer size.
template <std::size_t Capacity>
void LogDriverMessage(int databaseId, short handleType, const char (&message)[Capacity],
                      authd::platform::OdbcSmallInt reportedLength,
                      authd::platform::OdbcReturn result) {
    if (handleType != kStatementHandle || !IsSuccessOrInfo(result)) return;
    std::size_t length = 0;
    while (length < Capacity && message[length] != '\0') ++length;
    if (length == 0) return;
    // Log.AddLog(LOG_ERROR,
    //     "[database %d] Driver message (%zu bytes returned. Reported length %d): \"%.*s\"",
    //     databaseId, length, static_cast<int>(reportedLength), static_cast<int>(length), message)
    // L2_LOG_ERROR(Log,
    //     "[database %d] Driver message (%zu bytes returned. Reported length %d): \"%.*s\"",
    //     databaseId, length, static_cast<int>(reportedLength), static_cast<int>(length), message)
    L2_LOG_ERROR(Log,
        "[database %d] Driver message (%llu bytes returned; reported length %d): \"%.*s\"",
        databaseId, static_cast<unsigned long long>(length),
        static_cast<int>(reportedLength), static_cast<int>(length), message);
}

void* IntegerAttributeValue(std::uintptr_t value) noexcept {
    return reinterpret_cast<void*>(value);
}

const char* DatabaseEnvironmentName(int environmentType) noexcept {
    switch (environmentType) {
    case 0: return "game_account";
    case 1: return "secure_service";
    case 2: return "global_activity";
    default: return "unknown";
    }
}

const char* DiagnosticHandleName(short handleType) noexcept {
    switch (handleType) {
    case kEnvironmentHandle: return "environment";
    case kConnectionHandle: return "connection";
    case kStatementHandle: return "statement";
    default: return "unknown";
    }
}

void LogDatabaseDiagnostic(const char* event, int environmentType, short handleType,
                           const char* statement, const char (&sqlState)[6],
                           authd::platform::OdbcInteger nativeError,
                           authd::platform::OdbcReturn diagnosticResult,
                           std::optional<int> operationResult = std::nullopt) {
    const char* description = "Database operation failed";
    if (std::strcmp(event, "fetch_invalid_handle") == 0) description = "Fetch failed with an invalid handle";
    else if (std::strcmp(event, "fetch_failed") == 0) description = "Fetch failed";
    else if (std::strcmp(event, "diagnostic_truncated") == 0) description = "Driver diagnostic was truncated";
    else if (std::strcmp(event, "diagnostic_failed") == 0) description = "Could not retrieve the driver diagnostic";
    else if (std::strcmp(event, "insert_failed") == 0) description = "Insert failed";
    const std::string_view state(sqlState, 5);
    const bool available = IsSuccessOrInfo(diagnosticResult) &&
        runtime::logging::IsSqlState(state);
    char operationCode[16] = "unknown";
    if (operationResult) std::snprintf(operationCode, sizeof(operationCode), "%d", *operationResult);
    // Log.AddLog(LOG_ERROR,
    //     "component=database event=%s environment=%s environment_id=%d handle_type=%d statement_kind=%s operation_rc=%s sqlstate=%.5s native_code=%d diagnostic_rc=%d reason=%s details=redacted",
    //     event, DatabaseEnvironmentName(environmentType), environmentType,
    //     static_cast<int>(handleType), runtime::logging::SqlStatementKind(statement),
    //     operationCode, available ? sqlState : "none", static_cast<int>(nativeError),
    //     static_cast<int>(diagnosticResult),
    //     runtime::logging::SqlStateReason(available ? state : std::string_view{}))
    // Log.AddLog(LOG_ERROR,
    //     "[database] %s for %s (%d): handle %d, %s statement, operation result %s. SQLSTATE %.5s, native error %d, diagnostic result %d (%s).",
    //     description, DatabaseEnvironmentName(environmentType), environmentType,
    //     static_cast<int>(handleType), runtime::logging::SqlStatementKind(statement),
    //     operationCode, available ? sqlState : "none", static_cast<int>(nativeError),
    //     static_cast<int>(diagnosticResult),
    //     runtime::logging::SqlStateReason(available ? state : std::string_view{}))
    // L2_LOG_ERROR(Log,
    //     "[database] %s for %s (%d): handle %d, %s statement, operation result %s. SQLSTATE %.5s, native error %d, diagnostic result %d (%s).",
    //     description, DatabaseEnvironmentName(environmentType), environmentType,
    //     static_cast<int>(handleType), runtime::logging::SqlStatementKind(statement),
    //     operationCode, available ? sqlState : "none", static_cast<int>(nativeError),
    //     static_cast<int>(diagnosticResult),
    //     runtime::logging::SqlStateReason(available ? state : std::string_view{}))
    L2_LOG_ERROR(Log,
        "[database] %s for %s (%d): %s handle (%d), %s statement, operation result %s; SQLSTATE %.5s (%s), native error %d; diagnostic result %d.",
        description, DatabaseEnvironmentName(environmentType), environmentType,
        DiagnosticHandleName(handleType), static_cast<int>(handleType),
        runtime::logging::SqlStatementKind(statement), operationCode,
        available ? sqlState : "none",
        runtime::logging::SqlStateReason(available ? state : std::string_view{}),
        static_cast<int>(nativeError), static_cast<int>(diagnosticResult));
    if (statement != nullptr) LogSqlStatement(LOG_ERROR, environmentType, "Statement associated with the database error", statement);
}

void LogSkippedSqlExecution(const char* operation, int environmentType, int formattedLength) {
    if (formattedLength > 0) return;
    L2_LOG_ERROR(Log,
        "[database] %s did not submit SQL for %s (%d): %s (formatter result %d). Following driver diagnostics may refer to an earlier operation.",
        operation, DatabaseEnvironmentName(environmentType), environmentType,
        formattedLength == 0 ? "formatted statement is empty" : "statement formatting failed or the statement does not fit the buffer",
        formattedLength);
}

void SleepMilliseconds(unsigned int milliseconds) {
    if (milliseconds == 0) {
        std::this_thread::yield();
        return;
    }
    std::this_thread::sleep_for(
        std::chrono::milliseconds(milliseconds));
}

#if defined(AUTHD_USE_POSTGRESQL)
const char* PortableConnectionStringKey(int environmentType) noexcept {
    if (environmentType == 0) return "gameAccountDBconnStr";
    if (environmentType == 1) return "secureServiceDBconnStr";
    // The selected EXE reads this environment from the registry value with
    // exactly this name. The portable configuration uses that proven name
    // instead of adding a synthetic field to Config's PDB-defined layout.
    if (environmentType == 2) return "GloActNcsConn";
    return nullptr;
}

bool LoadPortableConnectionString(
    int environmentType,
    unsigned char (&destination)[256]) {
    const char* const key = PortableConnectionStringKey(environmentType);
    const char* const value = key == nullptr ? nullptr : config.Get(key);
    if (value == nullptr || value[0] == '\0') {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Invalid PostgreSQL connection string for DB environment %d (%s)",
        //     environmentType,
        //     key == nullptr ? "unsupported" : key)
        // Log.AddLog(LOG_ERROR,
        //     "component=database event=config_rejected environment=%s environment_id=%d key=%s reason=%s",
        //     DatabaseEnvironmentName(environmentType), environmentType,
        //     key == nullptr ? "none" : key,
        //     key == nullptr ? "unsupported_environment" : "missing_connection_string")
        // Log.AddLog(LOG_ERROR,
        //     "[database] Invalid configuration for %s (%d), option %s: %s.",
        //     DatabaseEnvironmentName(environmentType), environmentType,
        //     key == nullptr ? "none" : key,
        //     key == nullptr ? "unsupported database environment" : "connection string is missing")
        L2_LOG_ERROR(Log,
            "[database] Invalid configuration for %s (%d), option %s: %s.",
            DatabaseEnvironmentName(environmentType), environmentType,
            key == nullptr ? "none" : key,
            key == nullptr ? "unsupported database environment" : "connection string is missing");
        return false;
    }
    const std::size_t length = std::strlen(value);
    if (length >= sizeof(destination)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "PostgreSQL connection string is too long for DB environment %d",
        //     environmentType)
        // Log.AddLog(LOG_ERROR,
        //     "component=database event=config_rejected environment=%s environment_id=%d key=%s reason=connection_string_too_long bytes=%zu max_bytes=%zu",
        //     DatabaseEnvironmentName(environmentType), environmentType, key,
        //     length, sizeof(destination) - 1)
        // Log.AddLog(LOG_ERROR,
        //     "[database] Connection string for %s (%d), option %s, is too long: %zu bytes. Maximum %zu.",
        //     DatabaseEnvironmentName(environmentType), environmentType, key,
        //     length, sizeof(destination) - 1)
        // L2_LOG_ERROR(Log,
        //     "[database] Connection string for %s (%d), option %s, is too long: %zu bytes. Maximum %zu.",
        //     DatabaseEnvironmentName(environmentType), environmentType, key,
        //     length, sizeof(destination) - 1)
        L2_LOG_ERROR(Log,
            "[database] Connection string for %s (%d), option %s, is too long: %llu bytes; maximum %llu.",
            DatabaseEnvironmentName(environmentType), environmentType, key,
            static_cast<unsigned long long>(length),
            static_cast<unsigned long long>(sizeof(destination) - 1));
        return false;
    }
    std::memcpy(destination, value, length + 1);
    return true;
}
#endif

#if defined(_WIN32)
const char* RegistryValueName(int environmentType) noexcept {
    if (environmentType == 0) {
        if (config.gameId == 8 ||
            config.gameId == 16 ||
            config.gameId == 32) {
            return "L2Conn";
        }
        if (config.gameId == 27) {
            return "Aion";
        }
        if (config.gameId == 4) {
            return "sldb";
        }
        // The selected source leaves this pointer uninitialized for an
        // unsupported game id. That has no reproducible behavior. Passing no
        // registry value deterministically keeps the failed lookup.
        return nullptr;
    }
    if (environmentType == 1) {
        return "SecurityConn";
    }
    if (environmentType == 2) {
        return "GloActNcsConn";
    }
    return nullptr;
}

DBEnv* pEnv = nullptr;
#endif

} // namespace

// L2Authd.exe 0x00435290, Src/dbconn.cpp:29-32.
DBEnv::DBEnv()
    : m_lock(eCustomSpinLock, 0),
      m_henv(nullptr),
      m_connCount(0),
      m_envType(0) {
    // The target global environments keep one permanent owner reference.
    // m_timerStatus is not written by the constructor. Static
    // storage zero-initialization supplies its selected-image initial value.
    refcount = 1;
    std::memset(m_connStr, 0, sizeof(m_connStr));
}

// L2Authd.exe 0x00435340, Src/dbconn.cpp:35-38.
DBEnv::~DBEnv() {
    if (m_henv != nullptr) {
        authd::platform::SQLFreeHandle(
            kEnvironmentHandle,
            m_henv);
    }
}

// L2Authd.exe 0x004353C0, Src/dbconn.cpp:40-113.
bool DBEnv::Init(
    int connectionCount,
    int environmentType) {
    m_envType = environmentType;

    auto result = authd::platform::SQLSetEnvAttr(
        nullptr,
        201,
        IntegerAttributeValue(2),
        -6);
    if (!IsSuccessOrInfo(result)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Could not set connection pooling, retcode:%d",
        //     static_cast<int>(result))
        // Log.AddLog(LOG_ERROR,
        //     "component=database event=initialization_failed environment=%s environment_id=%d operation=set_connection_pooling odbc_rc=%d",
        //     DatabaseEnvironmentName(m_envType), m_envType, static_cast<int>(result))
        // Log.AddLog(LOG_ERROR,
        //     "[database] Cannot initialize %s (%d): enabling ODBC connection pooling returned %d.",
        //     DatabaseEnvironmentName(m_envType), m_envType, static_cast<int>(result))
        L2_LOG_ERROR(Log,
            "[database] Cannot initialize %s (%d): enabling ODBC connection pooling returned %d.",
            DatabaseEnvironmentName(m_envType), m_envType, static_cast<int>(result));
        return false;
    }

    result = authd::platform::SQLAllocHandle(
        kEnvironmentHandle,
        nullptr,
        &m_henv);
    if (!IsSuccessOrInfo(result)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Could not allocate environment handle, retcode:%d",
        //     static_cast<int>(result))
        // Log.AddLog(LOG_ERROR,
        //     "component=database event=initialization_failed environment=%s environment_id=%d operation=allocate_environment odbc_rc=%d",
        //     DatabaseEnvironmentName(m_envType), m_envType, static_cast<int>(result))
        // Log.AddLog(LOG_ERROR,
        //     "[database] Cannot initialize %s (%d): allocating the ODBC environment returned %d.",
        //     DatabaseEnvironmentName(m_envType), m_envType, static_cast<int>(result))
        L2_LOG_ERROR(Log,
            "[database] Cannot initialize %s (%d): allocating the ODBC environment returned %d.",
            DatabaseEnvironmentName(m_envType), m_envType, static_cast<int>(result));
        return false;
    }

    result = authd::platform::SQLSetEnvAttr(
        m_henv,
        200,
        IntegerAttributeValue(3),
        0);
    if (!IsSuccessOrInfo(result)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Could not register ODBC version, retcode:%d",
        //     static_cast<int>(result))
        // Log.AddLog(LOG_ERROR,
        //     "component=database event=initialization_failed environment=%s environment_id=%d operation=set_odbc_version odbc_rc=%d",
        //     DatabaseEnvironmentName(m_envType), m_envType, static_cast<int>(result))
        // Log.AddLog(LOG_ERROR,
        //     "[database] Cannot initialize %s (%d): selecting the ODBC version returned %d.",
        //     DatabaseEnvironmentName(m_envType), m_envType, static_cast<int>(result))
        L2_LOG_ERROR(Log,
            "[database] Cannot initialize %s (%d): selecting the ODBC version returned %d.",
            DatabaseEnvironmentName(m_envType), m_envType, static_cast<int>(result));
        return false;
    }
    if (!LoadConnStr()) {
        return false;
    }
    const char* const migrationsDirectory = config.Get("migrationsDirectory");
    if (migrationsDirectory != nullptr && migrationsDirectory[0] != '\0') {
#if defined(AUTHD_USE_POSTGRESQL)
        const auto directory = runtime::ConfigFile("etc/config.txt", "config.txt").parent_path() /
            std::filesystem::path(runtime::Utf8CodeUnits(migrationsDirectory));
        std::vector<runtime::PostgreSQLMigration> migrations;
        runtime::PostgreSQLMigrationReport report;
        std::string migrationError;
        // Missing or false keeps the database required.
        const bool createDatabase = config.GetBool("createDatabase", false);
        if (!runtime::LoadPostgreSQLMigrations(directory, migrations, migrationError) ||
            (createDatabase &&
                !runtime::CreatePostgreSQLDatabaseIfMissing(
                    reinterpret_cast<const char*>(m_connStr), "authd", migrationError)) ||
            !runtime::ApplyPostgreSQLMigrations(reinterpret_cast<const char*>(m_connStr),
                "authd", "authd", migrations, report, migrationError)) {
            // Log.AddLog(LOG_ERROR, "[migrations] environment %d: %s", m_envType, migrationError.c_str())
            // Log.AddLog(LOG_ERROR,
            //     "component=migrations event=failed environment=%s environment_id=%d %s",
            //     DatabaseEnvironmentName(m_envType), m_envType, migrationError.c_str())
            // Log.AddLog(LOG_ERROR,
            //     "[migrations] Failed for %s (%d): %s",
            //     DatabaseEnvironmentName(m_envType), m_envType, migrationError.c_str())
            L2_LOG_ERROR(Log,
                "[migrations] Failed for %s (%d): %s",
                DatabaseEnvironmentName(m_envType), m_envType, migrationError.c_str());
            return false;
        }
#else
        // Log.AddLog(LOG_ERROR, "migrationsDirectory requires the PostgreSQL backend")
        // Log.AddLog(LOG_ERROR,
        //     "component=database event=config_rejected environment=%s environment_id=%d key=migrationsDirectory reason=postgresql_backend_required",
        //     DatabaseEnvironmentName(m_envType), m_envType)
        // Log.AddLog(LOG_ERROR,
        //     "[database] Invalid configuration for %s (%d): migrationsDirectory requires the PostgreSQL backend.",
        //     DatabaseEnvironmentName(m_envType), m_envType)
        L2_LOG_ERROR(Log,
            "[database] Invalid configuration for %s (%d): migrationsDirectory requires the PostgreSQL backend.",
            DatabaseEnvironmentName(m_envType), m_envType);
        return false;
#endif
    }
    if (connectionCount <= 0) {
        return true;
    }

    CDBConn** const preemptedConns =
        new (std::nothrow) CDBConn*[connectionCount];
    if (preemptedConns == nullptr) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "new CDBConn * [preemptedConns] failed")
        // Log.AddLog(LOG_ERROR,
        //     "component=database event=initialization_failed environment=%s environment_id=%d operation=allocate_pool_index requested_connections=%d reason=allocation_failed",
        //     DatabaseEnvironmentName(m_envType), m_envType, connectionCount)
        // Log.AddLog(LOG_ERROR,
        //     "[database] Cannot initialize %s (%d): allocation failed for the pool index of %d connections.",
        //     DatabaseEnvironmentName(m_envType), m_envType, connectionCount)
        // L2_LOG_ERROR(Log,
        //     "[database] Cannot initialize %s (%d): allocation failed for the pool index of %d connections.",
        //     DatabaseEnvironmentName(m_envType), m_envType, connectionCount)
        char message[256]{};
        std::snprintf(message, sizeof(message),
            "[database] Cannot initialize %s (%d): allocation failed for the pool index of %d connections.",
            DatabaseEnvironmentName(m_envType), m_envType, connectionCount);
        runtime::logging::Write("authd", runtime::logging::Level::Error, message, __FILE__);
        return false;
    }

    int created = 0;
    for (; created < connectionCount; ++created) {
        preemptedConns[created] =
            new (std::nothrow) CDBConn(*this);
        if (preemptedConns[created] == nullptr) {
            // Log.AddLog(
            //     LOG_ERROR,
            //     "new CDBConn(*this) failed")
            // Log.AddLog(LOG_ERROR,
            //     "component=database event=pool_preallocation_incomplete environment=%s environment_id=%d requested_connections=%d allocated_objects=%d reason=allocation_failed",
            //     DatabaseEnvironmentName(m_envType), m_envType, connectionCount, created)
            // Log.AddLog(LOG_ERROR,
            //     "[database] Cannot initialize %s (%d): requested %d connection objects, but allocation stopped after %d.",
            //     DatabaseEnvironmentName(m_envType), m_envType, connectionCount, created)
            // L2_LOG_ERROR(Log,
            //     "[database] Cannot initialize %s (%d): requested %d connection objects, but allocation stopped after %d.",
            //     DatabaseEnvironmentName(m_envType), m_envType, connectionCount, created)
            // L2_LOG_ERROR(Log,
            //     "[database] Connection-object preallocation for %s (%d) stopped after %d of %d objects: allocation failed. Created objects will be released. Initialization returns %s. Object counts do not confirm database connectivity.",
            //     DatabaseEnvironmentName(m_envType), m_envType, created, connectionCount,
            //     created != 0 ? "success" : "failure")
            char message[512]{};
            std::snprintf(message, sizeof(message),
                "[database] Connection-object preallocation for %s (%d) stopped after %d of %d objects: allocation failed. Created objects will be released; initialization returns %s. Object counts do not confirm database connectivity.",
                DatabaseEnvironmentName(m_envType), m_envType, created, connectionCount,
                created != 0 ? "success" : "failure");
            runtime::logging::Write("authd", runtime::logging::Level::Error, message, __FILE__);
            break;
        }
    }
    for (int index = 0; index < created; ++index) {
        delete preemptedConns[index];
    }
    delete[] preemptedConns;
    return created != 0;
}

// L2Authd.exe 0x00435630, Src/dbconn.cpp:115-168.
bool DBEnv::LoadConnStr() {
#if defined(AUTHD_USE_POSTGRESQL)
    // Registry FILEDSNs and the Win32 credentials dialog are ODBC transports.
    // PostgreSQL obtains all three target environments from the ordinary AuthD
    // config map. Config's recovered object layout therefore stays untouched.
    return LoadPortableConnectionString(m_envType, m_connStr);
#else
    if (g_serviceManager.IsServiceMode() == 1) {
        if (m_envType == 0) {
            if (config.gameAccountDBconnStr == nullptr) {
                // Log.AddLog(
                //     LOG_ERROR,
                //     "Invalid gameAccountDB connection string")
                // Log.AddLog(LOG_ERROR,
                //     "component=database event=config_rejected environment=game_account environment_id=0 key=gameAccountDBconnStr reason=missing_connection_string")
                // Log.AddLog(LOG_ERROR,
                //     "[database] Account database connection string is missing. Set gameAccountDBconnStr.")
                L2_LOG_ERROR(Log,
                    "[database] Account database connection string is missing. Set gameAccountDBconnStr.");
                return false;
            }
            std::sprintf(
                reinterpret_cast<char*>(m_connStr),
                "%s",
                config.gameAccountDBconnStr);
        } else if (m_envType == 1) {
            if (config.secureServiceDBconnStr == nullptr) {
                // Log.AddLog(
                //     LOG_ERROR,
                //     "Invalid SecureServiceDB connection string")
                // Log.AddLog(LOG_ERROR,
                //     "component=database event=config_rejected environment=secure_service environment_id=1 key=secureServiceDBconnStr reason=missing_connection_string")
                // Log.AddLog(LOG_ERROR,
                //     "[database] Secure-service database connection string is missing. Set secureServiceDBconnStr.")
                L2_LOG_ERROR(Log,
                    "[database] Secure-service database connection string is missing. Set secureServiceDBconnStr.");
                return false;
            }
            std::sprintf(
                reinterpret_cast<char*>(m_connStr),
                "%s",
                config.secureServiceDBconnStr);
        } else {
            // Log.AddLog(
            //     LOG_ERROR,
            //     "Invalid DB type. m_envType:%d",
            //     m_envType)
            // Log.AddLog(LOG_ERROR,
            //     "component=database event=config_rejected environment=%s environment_id=%d source=service_config reason=unsupported_environment",
            //     DatabaseEnvironmentName(m_envType), m_envType)
            // Log.AddLog(LOG_ERROR,
            //     "[database] Service configuration does not support database environment %s (%d).",
            //     DatabaseEnvironmentName(m_envType), m_envType)
            L2_LOG_ERROR(Log,
                "[database] Service configuration does not support database environment %s (%d).",
                DatabaseEnvironmentName(m_envType), m_envType);
            return false;
        }
        return true;
    }

    if (LoadConnStrFromReg()) {
        return true;
    }
    // Log.AddLog(
    //     LOG_ERROR,
    //     "LoadConnStrFromReg() failed. m_envType:%d",
    //     m_envType)
    // Log.AddLog(LOG_ERROR,
    //     "component=database event=connection_settings_unavailable environment=%s environment_id=%d operation=load_connection_settings",
    //     DatabaseEnvironmentName(m_envType), m_envType)
    // Log.AddLog(LOG_ERROR,
    //     "[database] Could not load connection settings for %s (%d).",
    //     DatabaseEnvironmentName(m_envType), m_envType)
    // L2_LOG_ERROR(Log,
    //     "[database] Could not load connection settings for %s (%d).",
    //     DatabaseEnvironmentName(m_envType), m_envType)

#if defined(_WIN32)
    L2_LOG_WARN(Log,
        "[database] Registry connection settings are unavailable for %s (%d). Opening the credentials dialog.",
        DatabaseEnvironmentName(m_envType), m_envType);
    std::intptr_t result = 0;
    do {
        result = ::DialogBoxParamA(
            g_instance,
            MAKEINTRESOURCEA(101),
            nullptr,
            LoginDlgProc,
            reinterpret_cast<LPARAM>(this));
    } while (result == 4);
    return result != 2;
#else
    // The target's foreground fallback is a Win32 credentials dialog. On a
    // portable host the config-backed lookup below is the matching host
    // boundary. There is no hidden interactive or external credential source.
    L2_LOG_ERROR(Log,
        "[database] Connection settings are unavailable for %s (%d). This ODBC build has no credentials-dialog fallback; loading settings returns failure.",
        DatabaseEnvironmentName(m_envType), m_envType);
    return false;
#endif
#endif
}

// L2Authd.exe 0x00435770, Src/dbconn.cpp:170-212.
bool DBEnv::LoadConnStrFromReg() {
#if defined(_WIN32)
    const char* const valueName = RegistryValueName(m_envType);
    if (valueName == nullptr) {
        return false;
    }

    unsigned char encrypted[256]{};
    HKEY key = nullptr;
    bool loaded = false;
    const LSTATUS opened = ::RegOpenKeyExA(
        HKEY_LOCAL_MACHINE,
        "Software\\NCSoft\\L2AUTHD",
        0,
        KEY_READ,
        &key);
    if (opened == ERROR_SUCCESS) {
        DWORD type = 0;
        DWORD size = sizeof(encrypted);
        const LSTATUS queried = ::RegQueryValueExA(
            key,
            valueName,
            nullptr,
            &type,
            encrypted,
            &size);
        loaded = queried == ERROR_SUCCESS && type == REG_BINARY;
        ::RegCloseKey(key);
        if (queried != ERROR_SUCCESS) {
            L2_LOG_WARN(Log,
                "[database] Cannot read registry connection value %s for %s (%d): Windows error %ld. Stored settings were not loaded.",
                valueName, DatabaseEnvironmentName(m_envType), m_envType,
                static_cast<long>(queried));
        } else if (type != REG_BINARY) {
            L2_LOG_WARN(Log,
                "[database] Registry connection value %s for %s (%d) has type %lu; expected REG_BINARY (%lu). Stored settings were not loaded.",
                valueName, DatabaseEnvironmentName(m_envType), m_envType,
                static_cast<unsigned long>(type), static_cast<unsigned long>(REG_BINARY));
        }
    } else {
        L2_LOG_WARN(Log,
            "[database] Cannot open HKLM\\Software\\NCSoft\\L2AUTHD for reading connection settings for %s (%d): Windows error %ld.",
            DatabaseEnvironmentName(m_envType), m_envType, static_cast<long>(opened));
    }
    if (loaded) {
        DesReadBlock(encrypted, sizeof(encrypted));
        std::strcpy(
            reinterpret_cast<char*>(m_connStr),
            reinterpret_cast<const char*>(encrypted));
    }
    return loaded;
#else
    // Registry storage is the only target-specific part of this method. The
    // same two service connection strings are the portable credential source.
    const char* connectionString = nullptr;
    if (m_envType == 0) {
        connectionString = config.gameAccountDBconnStr;
    } else if (m_envType == 1) {
        connectionString = config.secureServiceDBconnStr;
    } else {
        return false;
    }
    if (connectionString == nullptr) {
        return false;
    }
    // Source bug: std::sprintf(m_connStr, "%s", connectionString) had no bound.
    if (runtime::BoundedFormat(
            reinterpret_cast<char*>(m_connStr),
            sizeof(m_connStr),
            "%s",
            connectionString) < 0) {
        L2_LOG_ERROR(Log,
            "[database] The connection string for %s (%d) is longer than %zu bytes. The database connection is not opened.",
            DatabaseEnvironmentName(m_envType), m_envType, sizeof(m_connStr) - 1);
        return false;
    }
    return true;
#endif
}

// L2Authd.exe 0x00435930, Src/dbconn.cpp:214-257.
void DBEnv::SaveConnStrToReg() {
#if defined(_WIN32)
    const char* const valueName = RegistryValueName(m_envType);
    if (valueName == nullptr) {
        return;
    }

    unsigned char encrypted[256]{};
    std::strcpy(
        reinterpret_cast<char*>(encrypted),
        reinterpret_cast<const char*>(m_connStr));
    DesWriteBlock(encrypted, sizeof(encrypted));

    HKEY key = nullptr;
    DWORD disposition = 0;
    const LSTATUS created = ::RegCreateKeyExA(
        HKEY_LOCAL_MACHINE,
        "Software\\NCSoft\\L2AUTHD",
        0,
        const_cast<char*>(""),
        0,
        KEY_WRITE,
        nullptr,
        &key,
        &disposition);
    if (created == ERROR_SUCCESS) {
        // ::RegSetValueExA(
        //     key,
        //     valueName,
        //     0,
        //     REG_BINARY,
        //     encrypted,
        //     sizeof(encrypted))
        const LSTATUS saved = ::RegSetValueExA(
            key,
            valueName,
            0,
            REG_BINARY,
            encrypted,
            sizeof(encrypted));
        ::RegCloseKey(key);
        if (saved != ERROR_SUCCESS) {
            L2_LOG_ERROR(Log,
                "[database] Cannot save connection settings for %s (%d): writing registry value %s returned Windows error %ld. Settings remain in memory; persistence is not confirmed.",
                DatabaseEnvironmentName(m_envType), m_envType, valueName,
                static_cast<long>(saved));
        }
    } else {
        L2_LOG_ERROR(Log,
            "[database] Cannot save connection settings for %s (%d): opening HKLM\\Software\\NCSoft\\L2AUTHD for writing returned Windows error %ld. Settings remain in memory; nothing was written.",
            DatabaseEnvironmentName(m_envType), m_envType, static_cast<long>(created));
    }
#endif
}

// L2Authd.exe 0x00435AE0, Src/dbconn.cpp:259-262.
bool DBEnv::TestConnect() {
    CDBConn connection(*this);
    return connection.IsConnected();
}

// L2Authd.exe 0x00435020, Src/dbconn.cpp:264-273.
void DBEnv::OnTimerCallback() {
    // Log.AddLog(
    //     LOG_ERROR,
    //     "DBEnv::OnTimerCallback(), m_envType:%d",
    //     m_envType)
    {
        L2_GUARD(m_lock);
        m_timerStatus = false;
    }
    // This timer clears notification suppression. It does not test the DB.
    // Log.AddLog(LOG_NORMAL,
    //     "component=database event=notification_cooldown_finished environment=%s environment_id=%d",
    //     DatabaseEnvironmentName(m_envType), m_envType)
    // Log.AddLog(LOG_NORMAL,
    //     "[database] Connection-error notifications for %s (%d) are enabled again after the cooldown.",
    //     DatabaseEnvironmentName(m_envType), m_envType)
    // L2_LOG_INFO(Log,
    //     "[database] Connection-error notifications for %s (%d) are enabled again after the cooldown.",
    //     DatabaseEnvironmentName(m_envType), m_envType)
    L2_LOG_INFO(Log,
        "[database] Connection-error email cooldown cleared for %s (%d).",
        DatabaseEnvironmentName(m_envType), m_envType);
    ReleaseRef();
}

// L2Authd.exe 0x00435B70, Src/dbconn.cpp:275-277.
void DBEnv::IncrementActiveConn() {
    runtime::raw_atomic::IncrementI32(&m_connCount);
}

// L2Authd.exe 0x00435B90, Src/dbconn.cpp:279-281.
void DBEnv::DecrementActiveConn() {
    runtime::raw_atomic::DecrementI32(&m_connCount);
}

#if defined(_WIN32)
// L2Authd.exe 0x00435080, Src/dbconn.cpp:321-391.
std::intptr_t CALLBACK LoginDlgProc(
    HWND window,
    unsigned int message,
    std::uintptr_t wParam,
    std::intptr_t lParam) {
    if (message == WM_INITDIALOG) {
        pEnv = reinterpret_cast<DBEnv*>(lParam);
        const char* connectionName = "";
        const char* title = "";
        if (pEnv->m_envType == 0) {
            connectionName = "L2Conn";
            title = "L2 ODBC Connection Info";
        } else if (pEnv->m_envType == 1) {
            connectionName = "SecuConn";
            title = "Security ODBC Connection Info";
        } else if (pEnv->m_envType == 2) {
            connectionName = "GloActNcsConn";
            title = "Global ODBC Connection Info";
        }
        ::SendDlgItemMessageA(
            window,
            1000,
            WM_SETTEXT,
            0,
            reinterpret_cast<LPARAM>(connectionName));
        ::SetWindowTextA(window, title);
    } else if (message == WM_COMMAND) {
        if (wParam == 1) {
            char field[64]{};
            char* const connectionString =
                reinterpret_cast<char*>(pEnv->m_connStr);
            ::SendDlgItemMessageA(
                window,
                1000,
                WM_GETTEXT,
                sizeof(field),
                reinterpret_cast<LPARAM>(field));
            std::strcpy(connectionString, "FILEDSN=");
            std::strcat(connectionString, field);
            ::SendDlgItemMessageA(
                window,
                1001,
                WM_GETTEXT,
                sizeof(field),
                reinterpret_cast<LPARAM>(field));
            std::strcat(connectionString, ";UID=");
            std::strcat(connectionString, field);
            ::SendDlgItemMessageA(
                window,
                1002,
                WM_GETTEXT,
                sizeof(field),
                reinterpret_cast<LPARAM>(field));
            std::strcat(connectionString, ";PWD=");
            std::strcat(connectionString, field);
            if (pEnv->TestConnect()) {
                pEnv->SaveConnStrToReg();
                ::EndDialog(window, 1);
            } else {
                ::EndDialog(window, 4);
            }
        } else if (wParam == 2) {
            ::EndDialog(window, 2);
        }
    }
    return 0;
}
#endif

// L2Authd.exe 0x00435BB0, Src/dbconn.cpp:290-300.
CDBConn::CDBConn(DBEnv& environment)
    : m_stmt(nullptr),
      m_colNum(1),
      m_paramNum(1),
      m_hdbc(nullptr),
      m_pEnv(&environment),
      m_isConnected(false) {
    std::memset(m_szStatement, 0, sizeof(m_szStatement));
    const auto result = authd::platform::SQLAllocHandle(
        kConnectionHandle,
        m_pEnv->m_henv,
        &m_hdbc);
    if (IsSuccessOrInfo(result)) {
        m_isConnected = GetConnection();
        if (m_isConnected) {
            m_pEnv->IncrementActiveConn();
        }
    } else {
        Error(kEnvironmentHandle, m_pEnv->m_henv, nullptr);
    }
}

// L2Authd.exe 0x00435C70, Src/dbconn.cpp:303-314.
CDBConn::~CDBConn() {
    if (m_stmt != nullptr) {
        authd::platform::SQLFreeHandle(
            kStatementHandle,
            m_stmt);
    }
    if (m_hdbc != nullptr) {
        authd::platform::SQLDisconnect(m_hdbc);
        authd::platform::SQLFreeHandle(
            kConnectionHandle,
            m_hdbc);
    }
    if (m_isConnected) {
        m_pEnv->DecrementActiveConn();
    }
}

// L2Authd.exe 0x00435CD0, Src/dbconn.cpp:548-565.
bool CDBConn::SetAutoCommit(bool enabled) {
    const auto result = authd::platform::SQLSetConnectAttr(
        m_hdbc,
        102,
        IntegerAttributeValue(enabled ? 1 : 0),
        4);
    if (result == kSqlError || result == kSqlInvalidHandle) {
        Error(kConnectionHandle, m_hdbc, nullptr);
        L2_LOG_ERROR(Log,
            "[database] Cannot %s autocommit for %s (%d): SQLSetConnectAttr returned %d. The requested setting is not confirmed.",
            enabled ? "enable" : "disable",
            DatabaseEnvironmentName(m_pEnv == nullptr ? -1 : m_pEnv->m_envType),
            m_pEnv == nullptr ? -1 : m_pEnv->m_envType, static_cast<int>(result));
        return false;
    }
    return true;
}

// L2Authd.exe 0x00435D50, Src/dbconn.cpp:567-576.
bool CDBConn::EndTran(short completionType) {
    const auto result = authd::platform::SQLEndTran(
        kConnectionHandle,
        m_hdbc,
        completionType);
    if (IsSuccessOrInfo(result)) {
        return true;
    }
    Error(kStatementHandle, m_stmt, nullptr);
    L2_LOG_ERROR(Log,
        "[database] Transaction completion failed for %s (%d): SQLEndTran returned %d for completion type %d. The transaction outcome is not confirmed.",
        DatabaseEnvironmentName(m_pEnv == nullptr ? -1 : m_pEnv->m_envType),
        m_pEnv == nullptr ? -1 : m_pEnv->m_envType, static_cast<int>(result),
        static_cast<int>(completionType));
    return false;
}

// L2Authd.exe 0x00435DB0, Src/dbconn.cpp:393-428.
bool CDBConn::Execute(const char* statement, ...) {
    std::memset(m_szStatement, 0, sizeof(m_szStatement));
    std::va_list arguments;
    va_start(arguments, statement);
    // Source bug: std::vsprintf(m_szStatement, statement, arguments) had no
    // bound. A statement that does not fit is not submitted.
    const int length = runtime::BoundedVFormat(
        m_szStatement,
        sizeof(m_szStatement),
        statement,
        arguments);
    va_end(arguments);

    if (m_pEnv == nullptr) {
        // Sensitive data: formatted statements may contain account values.
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[exec] db session is null so Execute fail(%s)",
        //     m_szStatement)
        // Log.AddLog(LOG_ERROR,
        //     "component=database event=execute_rejected reason=missing_environment statement_kind=%s",
        //     runtime::logging::SqlStatementKind(m_szStatement))
        // Log.AddLog(LOG_ERROR, "[database] Cannot execute the query: database environment is missing.")
        L2_LOG_ERROR(Log, "[database] Cannot execute the query: database environment is missing.");
        LogSqlStatement(LOG_ERROR, -1, "Execution rejected: database environment is missing", m_szStatement);
        return false;
    }

    Stopwatch stopwatch;
    stopwatch.Start();
    authd::platform::OdbcReturn result = kSqlError;
    if (length > 0) {
        result = authd::platform::SQLExecDirect(
            m_stmt,
            m_szStatement,
            length);
    }

    const std::int64_t elapsed = stopwatch.Now();
    if (runtime::raw_atomic::LoadI32Relaxed(
            &g_nAvgTimeCost) == 0) {
        runtime::raw_atomic::ExchangeI32(
            &g_nAvgTimeCost,
            runtime::Int32FromBits(elapsed));
    } else {
        const std::int32_t weightedPrevious =
            runtime::MultiplyInt32Wrapping(
                runtime::raw_atomic::LoadI32Relaxed(
                    &g_nAvgTimeCost),
                100);
        const std::int64_t average =
            (static_cast<std::int64_t>(weightedPrevious) +
             elapsed) /
            101;
        runtime::raw_atomic::ExchangeI32(
            &g_nAvgTimeCost,
            runtime::Int32FromBits(average));
    }

    if (result == kSqlSuccess) {
        return true;
    }
    LogSkippedSqlExecution("Execute", m_pEnv->m_envType, length);
    Error(
        kStatementHandle,
        m_stmt,
        m_szStatement);
    return false;
}

// L2Authd.exe 0x00435F40, Src/dbconn.cpp:433-450.
bool CDBConn::ExecuteIndirect(const char* statement, ...) {
    std::memset(m_szStatement, 0, sizeof(m_szStatement));
    std::va_list arguments;
    va_start(arguments, statement);
    // Source bug: std::vsprintf(m_szStatement, statement, arguments) had no
    // bound. A statement that does not fit is not submitted.
    const int length = runtime::BoundedVFormat(
        m_szStatement,
        sizeof(m_szStatement),
        statement,
        arguments);
    va_end(arguments);

    authd::platform::OdbcReturn result = kSqlError;
    if (length > 0) {
        authd::platform::SQLPrepare(
            m_stmt,
            m_szStatement,
            kSqlNts);
        result = authd::platform::SQLExecute(m_stmt);
    }
    if (IsSuccessOrInfo(result)) {
        return true;
    }
    LogSkippedSqlExecution("ExecuteIndirect", m_pEnv == nullptr ? -1 : m_pEnv->m_envType, length);
    Error(
        kStatementHandle,
        m_stmt,
        m_szStatement);
    return false;
}

// L2Authd.exe 0x00435FF0, Src/dbconn.cpp:454-473.
bool CDBConn::ExecuteInsert(const char* statement, ...) {
    std::memset(m_szStatement, 0, sizeof(m_szStatement));
    std::va_list arguments;
    va_start(arguments, statement);
    // Source bug: std::vsprintf(m_szStatement, statement, arguments) had no
    // bound. A statement that does not fit is not submitted.
    const int length = runtime::BoundedVFormat(
        m_szStatement,
        sizeof(m_szStatement),
        statement,
        arguments);
    va_end(arguments);

    authd::platform::OdbcReturn result = kSqlError;
    if (length > 0) {
        result = authd::platform::SQLExecDirect(
            m_stmt,
            m_szStatement,
            length);
    }
    if (IsSuccessOrInfo(result)) {
        return true;
    }
    LogSkippedSqlExecution("ExecuteInsert", m_pEnv == nullptr ? -1 : m_pEnv->m_envType, length);
    ErrorExceptInsert(
        kStatementHandle,
        m_stmt,
        m_szStatement);
    return false;
}

// L2Authd.exe 0x00436090, Src/dbconn.cpp:476-493.
bool CDBConn::ExecuteInsertIndirect(const char* statement, ...) {
    std::memset(m_szStatement, 0, sizeof(m_szStatement));
    std::va_list arguments;
    va_start(arguments, statement);
    // Source bug: std::vsprintf(m_szStatement, statement, arguments) had no
    // bound. A statement that does not fit is not submitted.
    const int length = runtime::BoundedVFormat(
        m_szStatement,
        sizeof(m_szStatement),
        statement,
        arguments);
    va_end(arguments);

    authd::platform::OdbcReturn result = kSqlError;
    if (length > 0) {
        authd::platform::SQLPrepare(
            m_stmt,
            m_szStatement,
            kSqlNts);
        result = authd::platform::SQLExecute(m_stmt);
    }
    if (IsSuccessOrInfo(result)) {
        return true;
    }
    LogSkippedSqlExecution("ExecuteInsertIndirect", m_pEnv == nullptr ? -1 : m_pEnv->m_envType, length);
    ErrorExceptInsert(
        kStatementHandle,
        m_stmt,
        m_szStatement);
    return false;
}

// L2Authd.exe 0x00436140, Src/dbconn.cpp:497-513.
bool CDBConn::ExecuteDelete(const char* statement, ...) {
    std::memset(m_szStatement, 0, sizeof(m_szStatement));
    std::va_list arguments;
    va_start(arguments, statement);
    // Source bug: std::vsprintf(m_szStatement, statement, arguments) had no
    // bound. A statement that does not fit is not submitted.
    const int length = runtime::BoundedVFormat(
        m_szStatement,
        sizeof(m_szStatement),
        statement,
        arguments);
    va_end(arguments);

    authd::platform::OdbcReturn result = kSqlError;
    if (length > 0) {
        result = authd::platform::SQLExecDirect(
            m_stmt,
            m_szStatement,
            length);
    }
    if (IsSuccessOrInfo(result) || result == kSqlNoData) {
        return true;
    }
    LogSkippedSqlExecution("ExecuteDelete", m_pEnv == nullptr ? -1 : m_pEnv->m_envType, length);
    Error(
        kStatementHandle,
        m_stmt,
        m_szStatement);
    return false;
}

// L2Authd.exe 0x004361F0, Src/dbconn.cpp:519-546.
bool CDBConn::Fetch(bool& noMoreRows) {
    noMoreRows = false;
    const auto result = authd::platform::SQLFetch(m_stmt);
    if (IsSuccessOrInfo(result)) {
        return true;
    }
    if (result == kSqlNoData) {
        noMoreRows = true;
        return false;
    }

    char sqlState[6]{};
    authd::platform::OdbcInteger nativeError = 0;
    char message[513]{};
    authd::platform::OdbcSmallInt messageLength = 0;
    const auto diagnosticResult =
    authd::platform::SQLGetDiagRec(
        kStatementHandle,
        m_stmt,
        1,
        sqlState,
        &nativeError,
        message,
        sizeof(message),
        &messageLength);
    // Sensitive data: driver text and SQL can echo query values.
    // Log.AddLog(
    //     LOG_ERROR,
    //     "[Fetch Error] return: %d, native error: %d, sql state: %s, error message: %s",
    //     static_cast<int>(result),
    //     nativeError,
    //     sqlState,
    //     message)
    // Log.AddLog(
    //     LOG_ERROR,
    //     "[CDBConn::Fetch] Statement: %s",
    //     m_szStatement)
    LogDatabaseDiagnostic(result == kSqlInvalidHandle ? "fetch_invalid_handle" : "fetch_failed",
        m_pEnv == nullptr ? -1 : m_pEnv->m_envType, kStatementHandle,
        m_szStatement, sqlState, nativeError, diagnosticResult, result);
    LogDriverMessage(m_pEnv == nullptr ? -1 : m_pEnv->m_envType, kStatementHandle, message, messageLength, diagnosticResult);
    return false;
}

// L2Authd.exe 0x00436310, Src/dbconn.cpp:578-643.
void CDBConn::Error(
    short handleType,
    void* handle,
    const char* statement) {
    char sqlState[6]{};
    authd::platform::OdbcInteger nativeError = 0;
    char message[513]{};
    authd::platform::OdbcSmallInt messageLength = 0;

    if (m_pEnv == nullptr) {
        // Sensitive data: omit the raw statement from error-reporting failures.
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[err] b session is null so Error fail(%s)",
        //     statement == nullptr ? "" : statement)
        // Log.AddLog(LOG_ERROR,
        //     "component=database event=diagnostic_failed reason=missing_environment handle_type=%d statement_kind=%s",
        //     static_cast<int>(handleType), runtime::logging::SqlStatementKind(statement))
        // Log.AddLog(LOG_ERROR, "[database] Cannot obtain error details: environment is missing (handle type %d).",
        //     static_cast<int>(handleType))
        // L2_LOG_ERROR(Log, "[database] Cannot obtain error details: environment is missing (handle type %d).",
        //     static_cast<int>(handleType))
        L2_LOG_ERROR(Log, "[database] Cannot obtain error details for the %s handle (%d): database environment is missing.",
            DiagnosticHandleName(handleType), static_cast<int>(handleType));
        if (statement != nullptr) LogSqlStatement(LOG_ERROR, -1, "Cannot obtain error details: database environment is missing", statement);
        return;
    }

    const auto result = authd::platform::SQLGetDiagRec(
        handleType,
        handle,
        1,
        sqlState,
        &nativeError,
        message,
        sizeof(message),
        &messageLength);
    if (result != kSqlSuccess) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "sql error in SQLGetDiagRec:(%d) %s",
        //     static_cast<int>(result),
        //     statement == nullptr ? "" : statement)
        LogDatabaseDiagnostic(result == kSqlSuccessWithInfo ? "diagnostic_truncated" : "diagnostic_failed",
            m_pEnv->m_envType, handleType, statement, sqlState, nativeError, result);
        LogDriverMessage(m_pEnv == nullptr ? -1 : m_pEnv->m_envType, handleType, message, messageLength, result);
        return;
    }

    // Sensitive data: keep SQLSTATE/native code without SQL or driver text.
    // if (statement != nullptr) {
    //     Log.AddLog(LOG_ERROR, "sql: %s", statement)
    // }
    // Log.AddLog(LOG_ERROR, "%s:%s", sqlState, message)
    LogDatabaseDiagnostic("odbc_error", m_pEnv->m_envType, handleType,
        statement, sqlState, nativeError, result);
    LogDriverMessage(m_pEnv == nullptr ? -1 : m_pEnv->m_envType, handleType, message, messageLength, result);

    if (std::strcmp(sqlState, "28000") == 0) {
        return;
    }

    const bool connectionError =
        std::strcmp(sqlState, "08S01") == 0 ||
        std::strcmp(sqlState, "01000") == 0 ||
        std::strcmp(sqlState, "08001") == 0;
    if (connectionError) {
        bool timerWasStarted = false;
        {
            L2_GUARD(m_pEnv->m_lock);
            if (!m_pEnv->m_timerStatus) {
                m_pEnv->RegisterTimer(30000, 1);
                m_pEnv->m_timerStatus = true;
                timerWasStarted = true;
            }
        }

        if (config.useReportMail && timerWasStarted) {
            // Log.AddLog(
            //     LOG_WARN,
            //     "[WARN] db session(%d) Email",
            //     m_pEnv->m_envType)
            // Log.AddLog(LOG_WARN,
            //     "component=database event=notification_requested environment=%s environment_id=%d reason=connection_error channel=email cooldown_ms=30000",
            //     DatabaseEnvironmentName(m_pEnv->m_envType), m_pEnv->m_envType)
            // Log.AddLog(LOG_WARN,
            //     "[database] Requested a connection-error email for %s (%d). Further notifications are suppressed for 30 seconds.",
            //     DatabaseEnvironmentName(m_pEnv->m_envType), m_pEnv->m_envType)
            // L2_LOG_WARN(Log,
            //     "[database] Requested a connection-error email for %s (%d). Further notifications are suppressed for 30 seconds.",
            //     DatabaseEnvironmentName(m_pEnv->m_envType), m_pEnv->m_envType)
            L2_LOG_WARN(Log,
                "[database] Requesting a connection-error email for %s (%d). Further connection-error emails are suppressed until the notification timer runs (requested delay: 30 seconds).",
                DatabaseEnvironmentName(m_pEnv->m_envType), m_pEnv->m_envType);
            if (config.gameId == 8) {
                CMailReport::Instance()->SendMail(
                    "[WARN] L2AuthD db session",
                    "Please Check L2Auth DB.");
            } else {
                CMailReport::Instance()->SendMail(
                    "[WARN] AionAuthD DB session",
                    "Please Check AionAuthD DB.");
            }
        }
        return;
    }

    if (config.useReportMail) {
        // Log.AddLog(
        //     LOG_WARN,
        //     "[WARN] Sql query check Email")
        // Log.AddLog(LOG_WARN,
        //     "component=database event=notification_requested environment=%s environment_id=%d reason=query_error channel=email",
        //     DatabaseEnvironmentName(m_pEnv->m_envType), m_pEnv->m_envType)
        // Log.AddLog(LOG_WARN,
        //     "[database] Requested a query-error email for %s (%d).",
        //     DatabaseEnvironmentName(m_pEnv->m_envType), m_pEnv->m_envType)
        // L2_LOG_WARN(Log,
        //     "[database] Requested a query-error email for %s (%d).",
        //     DatabaseEnvironmentName(m_pEnv->m_envType), m_pEnv->m_envType)
        L2_LOG_WARN(Log,
            "[database] Requesting a query-error email for %s (%d).",
            DatabaseEnvironmentName(m_pEnv->m_envType), m_pEnv->m_envType);
        if (config.gameId == 8) {
            CMailReport::Instance()->SendMail(
                "[WARN] L2AuthD Sql query check",
                "Please Check L2Auth DB.");
        } else {
            CMailReport::Instance()->SendMail(
                "[WARN] AionAuthD Sql query check",
                "Please Check AionAuthD DB.");
        }
    }
}

// L2Authd.exe 0x00436610, Src/dbconn.cpp:646-659.
void CDBConn::ErrorExceptInsert(
    short handleType,
    void* handle,
    const char* statement) {
    char sqlState[6]{};
    authd::platform::OdbcInteger nativeError = 0;
    char message[256]{};
    authd::platform::OdbcSmallInt messageLength = 0;
    const auto result = authd::platform::SQLGetDiagRec(
        handleType,
        handle,
        1,
        sqlState,
        &nativeError,
        message,
        sizeof(message),
        &messageLength);
    if (result == kSqlSuccess &&
        std::strcmp(sqlState, "23000") != 0) {
        // Sensitive data: keep SQLSTATE/native code without SQL or driver text.
        // if (statement != nullptr) {
        //     Log.AddLog(LOG_ERROR, "sql: %s", statement)
        // }
        // Log.AddLog(LOG_ERROR, "%s:%s", sqlState, message)
        LogDatabaseDiagnostic("insert_failed", m_pEnv == nullptr ? -1 : m_pEnv->m_envType,
            handleType, statement, sqlState, nativeError, result);
        LogDriverMessage(m_pEnv == nullptr ? -1 : m_pEnv->m_envType, handleType, message, messageLength, result);
    } else if (result != kSqlSuccess) {
        LogDatabaseDiagnostic(result == kSqlSuccessWithInfo ? "diagnostic_truncated" : "diagnostic_failed",
            m_pEnv == nullptr ? -1 : m_pEnv->m_envType,
            handleType, statement, sqlState, nativeError, result);
        LogDriverMessage(m_pEnv == nullptr ? -1 : m_pEnv->m_envType, handleType, message, messageLength, result);
    }
}

// L2Authd.exe 0x004366C0, Src/dbconn.cpp:662-664.
void CDBConn::SetColumnNumber(int column) {
    m_colNum = column;
}

// L2Authd.exe 0x004366E0, Src/dbconn.cpp:689-691.
void CDBConn::Bind(char* value, int capacity) {
    authd::platform::SQLBindCol(
        m_stmt,
        static_cast<authd::platform::OdbcUSmallInt>(m_colNum++),
        1,
        value,
        capacity,
        nullptr);
}

// L2Authd.exe 0x00436730, Src/dbconn.cpp:667-669.
void CDBConn::Bind(char* value) {
    authd::platform::SQLBindCol(
        m_stmt,
        static_cast<authd::platform::OdbcUSmallInt>(m_colNum++),
        -6,
        value,
        1,
        nullptr);
}

// L2Authd.exe 0x00436780, Src/dbconn.cpp:672-674.
void CDBConn::Bind(unsigned char* value) {
    authd::platform::SQLBindCol(
        m_stmt,
        static_cast<authd::platform::OdbcUSmallInt>(m_colNum++),
        -6,
        value,
        1,
        nullptr);
}

// L2Authd.exe 0x004367D0, Src/dbconn.cpp:695-697.
void CDBConn::Bind(unsigned int* value) {
    authd::platform::SQLBindCol(
        m_stmt,
        static_cast<authd::platform::OdbcUSmallInt>(m_colNum++),
        4,
        value,
        4,
        nullptr);
}

// L2Authd.exe 0x00436820, Src/dbconn.cpp:700-702.
void CDBConn::Bind(int* value) {
    authd::platform::SQLBindCol(
        m_stmt,
        static_cast<authd::platform::OdbcUSmallInt>(m_colNum++),
        4,
        value,
        4,
        nullptr);
}

// L2Authd.exe 0x00436870, Src/dbconn.cpp:705-707.
void CDBConn::Bind(short* value) {
    authd::platform::SQLBindCol(
        m_stmt,
        static_cast<authd::platform::OdbcUSmallInt>(m_colNum++),
        5,
        value,
        2,
        nullptr);
}

// L2Authd.exe 0x004368C0, Src/dbconn.cpp:710-712.
void CDBConn::SetParamNumber(int parameter) {
    m_paramNum = parameter;
}

// L2Authd.exe 0x004368E0, Src/dbconn.cpp:727-730.
void CDBConn::BindParam(
    char* value,
    int capacity,
    short inputOutputType) {
    authd::platform::SQLBindParameter(
        m_stmt,
        static_cast<authd::platform::OdbcUSmallInt>(m_paramNum++),
        inputOutputType,
        1,
        12,
        static_cast<authd::platform::OdbcUInteger>(capacity),
        0,
        value,
        static_cast<authd::platform::OdbcLen>(capacity) + 1,
        nullptr);
}

// L2Authd.exe 0x00436940, Src/dbconn.cpp:715-718.
void CDBConn::BindParam(char* value, short inputOutputType) {
    authd::platform::SQLBindParameter(
        m_stmt,
        static_cast<authd::platform::OdbcUSmallInt>(m_paramNum++),
        inputOutputType,
        -6,
        -6,
        0,
        0,
        value,
        0,
        nullptr);
}

// L2Authd.exe 0x00436990, Src/dbconn.cpp:721-724.
void CDBConn::BindParam(
    unsigned char* value,
    short inputOutputType) {
    authd::platform::SQLBindParameter(
        m_stmt,
        static_cast<authd::platform::OdbcUSmallInt>(m_paramNum++),
        inputOutputType,
        -6,
        -6,
        0,
        0,
        value,
        0,
        nullptr);
}

// L2Authd.exe 0x004369E0, Src/dbconn.cpp:733-736.
void CDBConn::BindParam(
    unsigned int* value,
    short inputOutputType) {
    authd::platform::SQLBindParameter(
        m_stmt,
        static_cast<authd::platform::OdbcUSmallInt>(m_paramNum++),
        inputOutputType,
        4,
        4,
        0,
        0,
        value,
        0,
        nullptr);
}

// L2Authd.exe 0x00436A30, Src/dbconn.cpp:739-742.
void CDBConn::BindParam(int* value, short inputOutputType) {
    authd::platform::SQLBindParameter(
        m_stmt,
        static_cast<authd::platform::OdbcUSmallInt>(m_paramNum++),
        inputOutputType,
        4,
        4,
        0,
        0,
        value,
        0,
        nullptr);
}

// L2Authd.exe 0x00436A80, Src/dbconn.cpp:745-748.
void CDBConn::BindParam(short* value, short inputOutputType) {
    authd::platform::SQLBindParameter(
        m_stmt,
        static_cast<authd::platform::OdbcUSmallInt>(m_paramNum++),
        inputOutputType,
        5,
        5,
        0,
        0,
        value,
        0,
        nullptr);
}

// L2Authd.exe 0x00436AD0, Src/dbconn.cpp:750-756.
void CDBConn::ResetHtmt() {
    m_colNum = 1;
    m_paramNum = 1;
    authd::platform::SQLFreeStmt(m_stmt, 0);
    authd::platform::SQLFreeStmt(m_stmt, 2);
    authd::platform::SQLFreeStmt(m_stmt, 3);
}

// L2Authd.exe 0x00436B20, Src/dbconn.cpp:758-806.
bool CDBConn::GetConnection() {
    if (m_hdbc == nullptr) {
        return false;
    }

    authd::platform::SQLSetConnectAttr(
        m_hdbc,
        103,
        IntegerAttributeValue(60),
        0);
    authd::platform::SQLSetConnectAttr(
        m_hdbc,
        113,
        IntegerAttributeValue(60),
        0);

    const auto inputLength =
        static_cast<authd::platform::OdbcSmallInt>(
            std::strlen(
                reinterpret_cast<const char*>(
                    m_pEnv->m_connStr)));
    constexpr unsigned int timeGap[3]{0, 30, 150};
    char output[1024]{};
    authd::platform::OdbcSmallInt outputLength = 0;

    for (unsigned int attempt = 0; attempt < 3; ++attempt) {
        auto result = authd::platform::SQLDriverConnect(
            m_hdbc,
            nullptr,
            reinterpret_cast<const char*>(m_pEnv->m_connStr),
            inputLength,
            output,
            sizeof(output),
            &outputLength,
            0);
        if (IsSuccessOrInfo(result)) {
            result = authd::platform::SQLAllocHandle(
                kStatementHandle,
                m_hdbc,
                &m_stmt);
            if (IsSuccessOrInfo(result)) {
                if (attempt != 0) {
                    L2_LOG_INFO(Log,
                        "[database] Connected to %s (%d) on attempt %u/3; statement handle is ready.",
                        DatabaseEnvironmentName(m_pEnv->m_envType), m_pEnv->m_envType,
                        attempt + 1);
                }
                return true;
            }
            Error(kConnectionHandle, m_hdbc, nullptr);
            L2_LOG_ERROR(Log,
                "[database] Connected to %s (%d), but statement-handle allocation failed: SQLAllocHandle returned %d. Connection setup returns failure without another attempt.",
                DatabaseEnvironmentName(m_pEnv->m_envType), m_pEnv->m_envType,
                static_cast<int>(result));
            return false;
        }

        Error(kConnectionHandle, m_hdbc, nullptr);
        if (attempt + 1 < 3) {
            L2_LOG_WARN(Log,
                "[database] Connection to %s (%d) failed on attempt %u/3: SQLDriverConnect returned %d. Next attempt follows a %u ms wait.",
                DatabaseEnvironmentName(m_pEnv->m_envType), m_pEnv->m_envType,
                attempt + 1, static_cast<int>(result), timeGap[attempt]);
        } else {
            L2_LOG_ERROR(Log,
                "[database] Connection to %s (%d) failed on attempt %u/3: SQLDriverConnect returned %d. No attempts remain.",
                DatabaseEnvironmentName(m_pEnv->m_envType), m_pEnv->m_envType,
                attempt + 1, static_cast<int>(result));
        }
        SleepMilliseconds(timeGap[attempt]);
    }
    return false;
}
