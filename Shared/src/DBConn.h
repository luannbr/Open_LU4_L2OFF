#pragma once

// The shipping owner is ..\Shared\DBConn.cpp. DBConn and
// CSqlPoolManager remain in the global namespace because that is
// how both classes appear in the current CacheD guard signatures. Only the
// PostgreSQL configuration carrier below is portable-only API.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace database {

// Portable PostgreSQL carrier for the current DBConn::Error ->
// SystemMail::sendCannotFindSPMail production edge. The target passes an ODBC
// diagnostic record directly. PostgreSQL exposes the same facts as separate
// libpq fields, so the process composition root reconnects them to the
// recovered Shared/SystemMail owner.
struct MissingProcedureReport {
    const wchar_t* query = nullptr;
    std::string sqlState;
    int nativeError = 0;
    std::string errorMessage;
    std::string catalogName;
};

struct PostgreSQLPoolConfig {
    int databaseId = 0;
    std::string connectionString;

    // Portable replacement for the distinct MSSQL database selected by this
    // DBList id. Empty inherits PostgreSQLRuntimeConfig::defaultSchema.
    std::string defaultSchema;

    // Zero keeps CSqlPoolManager::GetDBConnNum from the target. A
    // non-zero value is an explicit portable deployment override.
    std::size_t poolSizeOverride = 0;
};

struct PostgreSQLRuntimeConfig {
    int worldId = 0;
    std::vector<PostgreSQLPoolConfig> databases;

    // Current CacheD values and units. statementTimeoutMilliseconds defaults
    // to zero deliberately: Cached.exe loads WorldDBTimeOut but passes the
    // adjacent zero-initialized global to SQLSetStmtAttrW.
    std::uint32_t sqlExecLimitMilliseconds = 5000;
    std::uint32_t waitTimeoutCheckMilliseconds = 0;
    std::uint32_t statementTimeoutMilliseconds = 0;

    // Every migrated MSSQL procedure is exposed as a PostgreSQL set-returning
    // function. Unqualified and dbo-qualified calls use the checked-out
    // pool's schema (falling back to defaultSchema)
    // three-part database.dbo.proc calls use databaseSchemas first.
    std::string defaultSchema = "public";
    std::map<std::string, std::string> databaseSchemas;

    // Current DBConn::Error terminates on the exact ODBC communication-link
    // state 08S01. The libpq boundary normalizes a confirmed CONNECTION_BAD
    // state to 08S01 before applying this policy. Other class-08 states are
    // ordinary reported query failures, matching the target's full SQLSTATE
    // comparison. Keeping this true is the compatibility default. A
    // controlled embedding can install fatalErrorHandler or opt
    // out.
    bool terminateOnFatalDatabaseError = true;
    std::function<void(const std::string&, const std::string&)> fatalErrorHandler;

    // Current CacheD rate-limits native error 2812 to one CannotFileSP report
    // per ten minutes. CacheD installs the recovered SystemMail owner here.
    // the base PostgreSQL transport remains linkable by isolated consumers.
    std::function<void(const MissingProcedureReport&)> missingProcedureReporter;

    // Current DBConn::ExecutePrepared has two distinct QsCheck call sites:
    // AddSlowQs before SQLExecLimit/last-query handling, and AddValue only
    // after SQL_SUCCESS. Keep those boundaries separate so log ordering and
    // failed-query accounting remain identical.
    std::function<void(const std::string&, std::uint32_t)> slowQueryObserver;
    std::function<void(const std::string&, std::uint32_t)> successfulQueryObserver;
};

} // namespace database

struct _SQLPool_;
class L2Exception;

class CSqlPoolManager {
public:
    CSqlPoolManager();
    ~CSqlPoolManager();

    CSqlPoolManager(const CSqlPoolManager&) = delete;
    CSqlPoolManager& operator=(const CSqlPoolManager&) = delete;

    static CSqlPoolManager& Instance();

    // Portable boundary replacing registry/ODBC DSNs. It performs only
    // validation and stores configuration. Init keeps the target order of
    // Login before AllocateSQLPool. Duplicate database IDs are accepted
    // because the current DBList vector accepts them. The first mapping wins
    // when InitPoolMap constructs its unique ordered map.
    bool ConfigurePostgreSQL(database::PostgreSQLRuntimeConfig config);
    void Shutdown();
    bool IsReady() const;

    // Current public production surface recovered from Cached.exe.
    void Init();
    int GetDBConnNum(int databaseId) const;

    // Slot 66 in the current DX table scans the configured DBList vector
    // directly. This portable boundary keeps that production query
    // without exposing the host configuration carrier to DXPacket.cpp. The
    // original source did not keep a separate method name for the scan.
    bool HasDatabaseId(int databaseId) const;

private:
    friend class DBConn;
    friend class L2Exception;

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    void InitPoolMap();
    bool Login();
    void AllocateSQLPool();
    // The current CacheD crash-summary owner reads the live per-world pool
    // counters and five checkout durations directly. PostgreSQL stores the
    // same state in host containers, so this private transport projection is
    // called only by the main Shared L2Exception owner.
    void WriteUnhandledPoolCrashLog() const;
    _SQLPool_* PopPool(int databaseId);
    void PushPool(int databaseId, _SQLPool_* pool);
};

CSqlPoolManager& GetSqlPoolManager();

class DBConn {
public:
    explicit DBConn(int databaseId);
    virtual ~DBConn();

    DBConn(const DBConn&) = delete;
    DBConn& operator=(const DBConn&) = delete;

    bool ExecuteNoError(const wchar_t* query, ...);
    bool Fetch();

    // Result-column bindings. String/binary lengths keep ODBC's byte units.
    // wchar_t buffers therefore use the target's two-byte UTF-16 width even on
    // hosts whose wchar_t is four bytes.
    void Bind(wchar_t* value, int targetBufferBytes);
    // Direct UTF-16 results. Capacity and full (untruncated) indicators are
    // bytes, including space for a terminator in capacity only. NULL leaves
    // the buffer unchanged and reports -1 through nullable bindings.
    void Bind(char16_t* value, int bufferBytes);
    void Bind(char* value, int bufferBytes);
    void Bind(unsigned int* value);
    void Bind(int* value);
    void Bind(long long* value);
    // std::int64_t is long on LP64 Linux and long long on macOS and Windows.
    // Bind an eight-byte long as the same 64-bit result column.
    template <typename T>
        requires(std::is_same_v<T, long> && sizeof(long) == sizeof(long long))
    void Bind(T* value) {
        BindInt64(value);
    }
    void Bind(short* value);
    void Bind(double* value);
    void BindBinary(char* value, int bufferBytes);
    void BindBinaryNullable(unsigned char* value, int bufferBytes, std::int64_t* lengthOrIndicator);
    void BindNullable(wchar_t* value, int targetBufferBytes, std::int64_t* lengthOrIndicator);
    void BindNullable(char16_t* value, int bufferBytes, std::int64_t* lengthOrIndicator);
    void BindNullable(unsigned int* value, std::int64_t* lengthOrIndicator);
    void BindNullable(int* value, std::int64_t* lengthOrIndicator);
    void BindNullable(long long* value, std::int64_t* lengthOrIndicator);
    template <typename T>
        requires(std::is_same_v<T, long> && sizeof(long) == sizeof(long long))
    void BindNullable(T* value, std::int64_t* lengthOrIndicator) {
        BindNullableInt64(value, lengthOrIndicator);
    }
    void BindNullable(double* value, std::int64_t* lengthOrIndicator);

    // Input bindings are pointer-based because SQLBindParameter reads the
    // pointed value at ExecutePrepared, not when the helper is called. These
    // overloads match to the twelve current helper instantiations.
    void BindParameter(const bool* value);
    void BindParameter(const short* value);
    void BindParameter(const int* value);
    void BindParameter(const unsigned int* value);
    void BindParameter(const long long* value);
    void BindParameter(const unsigned long long* value);
    void BindParameter(const double* value);
    void BindParameter(const char* value);
    void BindParameter(const wchar_t* value);
    // Borrowed UTF-16 text. The buffer must survive until query execution.
    void BindParameter(const char16_t* value);
    void BindParameter(const long* value);
    void BindParameter(const unsigned long* value);

    // Current enum/specialized 32-bit template instances share the DUMMY tag
    // seen in DBConn::ReportParamInfo. Keep this as a real production path,
    // not a test-only side channel.
    template <class Enum, std::enable_if_t<std::is_enum_v<Enum>, int> = 0>
    void BindParameter(const Enum* value) {
        static_assert(sizeof(Enum) == sizeof(int), "shipping DB enum parameters are 32-bit");
        BindParameterEnum32(value);
    }

    // The eight current direct SQLBindParameter sites need a lossless binary
    // carrier when reconstructed away from ODBC.
    void BindParameterBinary(const void* value, std::size_t size, const std::int64_t* lengthOrIndicator = nullptr);
    void BindParameterNull();

    void ResetParam();
    void UnBind();
    void ReportParamInfo();
    bool Prepare(const wchar_t* query);
    bool ExecutePrepared(const wchar_t* query);

    // Portable transport for the handful of current owners that call
    // SQLExecute directly on DBConn's statement handle. This is explicitly
    // not a recovered DBConn source method: it exposes that existing handle
    // boundary without running ExecutePrepared's QsCheck/error policy.
    bool ExecutePreparedDirect(const wchar_t* query, std::uint32_t& durationMilliseconds);

    bool IsValid() const;
    int GetDatabaseId() const;

private:
    void BindInt64(void* value);
    void BindNullableInt64(void* value, std::int64_t* lengthOrIndicator);

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    void BindParameterEnum32(const void* value);
    bool ExecutePreparedImpl(
        const wchar_t* query, bool observeQuery, bool reportFailure, std::uint32_t* durationMilliseconds
    );
    void Error(const wchar_t* query, int statusCode);
};

// Fixed-width documentation mirror of the current MSVC object. The portable
// class above uses a pimpl instead of pretending that host STL
// and wchar_t layouts are ABI-compatible with this target layout.
