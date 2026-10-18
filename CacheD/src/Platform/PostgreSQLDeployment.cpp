#include "Shared/src/Runtime/Utf8.h"
#include "PostgreSQLDeployment.h"

#include "Cached.h"
#include "Shared/src/Runtime/PostgreSQLMigrations.h"
#include "Shared/src/TextRuntime.h"
#include "Config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>

namespace {

std::string_view TrimAscii(std::string_view value) {
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

// Like the l2cache.ini switches, only "true" in any letter case enables it.
bool IsTrue(std::string_view value) {
    constexpr std::string_view expected = "true";
    value = TrimAscii(value);
    return value.size() == expected.size() &&
        std::equal(value.begin(), value.end(), expected.begin(), [](char left, char right) {
            return std::tolower(static_cast<unsigned char>(left)) == right;
        });
}

std::vector<std::string_view> Split(
    std::string_view value,
    char delimiter,
    bool skipEmpty) {
    std::vector<std::string_view> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t end = value.find(delimiter, start);
        const std::string_view token = value.substr(
            start,
            end == std::string_view::npos
                ? std::string_view::npos
                : end - start);
        if (!skipEmpty || !token.empty()) result.push_back(token);
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return result;
}

bool ParseDatabaseSchemas(
    const std::string& value,
    std::map<std::string, std::string>& result,
    std::string& error) {
    for (std::string_view entry : Split(value, ';', true)) {
        entry = TrimAscii(entry);
        if (entry.empty()) continue;
        const std::size_t equals = entry.find('=');
        if (equals == std::string_view::npos) {
            // error = "[PostgreSQL]/database_schemas entry has no '=': " +
            //     std::string(entry)
            error = "[PostgreSQL]/database_schemas entry has no '='; entry value omitted";
            return false;
        }
        const std::string_view database = TrimAscii(entry.substr(0, equals));
        const std::string_view schema = TrimAscii(entry.substr(equals + 1));
        if (database.empty() || schema.empty()) {
            error = "[PostgreSQL]/database_schemas contains an empty name";
            return false;
        }
        result[std::string(database)] = std::string(schema);
    }
    return true;
}

const char* DatabaseRole(int databaseId, int worldId) {
    switch (databaseId) {
        case -4: return "account";
        case -3: return "shared";
        case -2: return "clan-bbs";
        case -1: return "bbs";
        default: return databaseId == worldId ? "world" : "raid-world";
    }
}

struct MigrationTarget {
    const database::PostgreSQLPoolConfig* database;
    std::vector<runtime::PostgreSQLMigration> migrations;
};

bool ReadMigrationTargets(
    const runtime::Config& deployment,
    const database::PostgreSQLRuntimeConfig& configuration,
    std::vector<MigrationTarget>& targets,
    std::string& error) {
    const std::string common = deployment.Get("PostgreSQL", "MigrationsDirectory", "");
    std::set<std::pair<std::string, std::filesystem::path>> seen;
    for (const auto& database : configuration.databases) {
        const std::string value = deployment.Get("PostgreSQL",
            "MigrationsDirectory(" + std::to_string(database.databaseId) + ")", common);
        if (value.empty()) continue;
        const auto directory = std::filesystem::path(deployment.Path()).parent_path() /
            std::filesystem::path(runtime::Utf8CodeUnits(value));
        if (!seen.emplace(database.connectionString, directory).second) continue;
        MigrationTarget target{&database, {}};
        if (!runtime::LoadPostgreSQLMigrations(directory, target.migrations, error)) return false;
        targets.push_back(std::move(target));
    }
    return true;
}

} // namespace

bool LoadCacheDDatabasePlan(
    const std::string& cacheConfigurationPath,
    int worldId,
    CacheDDatabasePlan& result,
    std::string& error,
    bool emitDiagnostics) {
    result = CacheDDatabasePlan{};
    error.clear();

    runtime::Config source;
    if (!source.Load(cacheConfigurationPath, emitDiagnostics)) {
        error = "cannot read " + cacheConfigurationPath;
        return false;
    }

    std::string dbList = source.Get("SQL", "DBList", "");
    // GetPrivateProfileStringW writes at most 1023 payload wchar_t values into
    // the target's 0x400-element buffer. Every accepted DBList grammar symbol
    // is ASCII, hence the byte and UTF-16-unit limit are identical here.
    if (dbList.size() > 1023) dbList.resize(1023);

    std::vector<int> databaseIds;
    for (const std::string_view token : Split(dbList, ':', true)) {
        const int databaseId = WindowsWtol(token);
        if (databaseId == 0) {
            // error = "There is no Database List!!! Set DBList at [SQL] section."
            error = "Set [SQL] DBList to nonzero database IDs separated by ':'";
            return false;
        }
        databaseIds.push_back(databaseId);
    }
    if (databaseIds.empty()) {
        // error = "There is no Database List!!! Set DBList at [SQL] section."
        error = "Set [SQL] DBList to nonzero database IDs separated by ':'";
        return false;
    }
    if (std::find(databaseIds.begin(), databaseIds.end(), worldId) ==
        databaseIds.end()) {
        // error = "My World is not declared in DBList. DBList must include "
        //     "My World[" + std::to_string(worldId) + "]"
        error = "[SQL] DBList must include WorldId " + std::to_string(worldId);
        return false;
    }

    result.worldId = worldId;
    result.dbListText = std::move(dbList);
    result.databaseIds = std::move(databaseIds);
    return true;
}

bool BuildPostgreSQLRuntimeConfiguration(
    const std::string& deploymentPath,
    const CacheDConfiguration& cacheConfiguration,
    const CacheDDatabasePlan& databasePlan,
    database::PostgreSQLRuntimeConfig& result,
    std::string& error,
    bool emitDiagnostics) {
    result = database::PostgreSQLRuntimeConfig{};
    error.clear();

    runtime::Config deployment;
    if (!deployment.Load(deploymentPath, emitDiagnostics)) {
        error = "cannot read explicit PostgreSQL deployment file " +
            deploymentPath;
        return false;
    }

    result.worldId = databasePlan.worldId;
    result.sqlExecLimitMilliseconds = static_cast<std::uint32_t>(
        std::max(cacheConfiguration.sqlExecLimitMilliseconds, 0));
    result.waitTimeoutCheckMilliseconds =
        cacheConfiguration.waitTimeoutCheckMilliseconds;
    // Exact current behavior: WorldDBTimeOut is loaded, but the adjacent
    // zero-initialized global is passed to SQLSetStmtAttrW at 0x004a6b29.
    result.statementTimeoutMilliseconds = 0;
    result.terminateOnFatalDatabaseError = true;
    bool foundWorldSchema = false;
    for (const int databaseId : databasePlan.databaseIds) {
        const std::string suffix = "(" + std::to_string(databaseId) + ")";
        const std::string connection = deployment.Get(
            "PostgreSQL", "connStr" + suffix, "");
        const std::string schema = deployment.Get(
            "PostgreSQL", "schema" + suffix, "");
        if (connection.empty()) {
            error = "missing [PostgreSQL]/connStr" + suffix +
                " for " + DatabaseRole(databaseId, databasePlan.worldId);
            return false;
        }
        if (schema.empty()) {
            error = "missing [PostgreSQL]/schema" + suffix +
                " for " + DatabaseRole(databaseId, databasePlan.worldId);
            return false;
        }
        database::PostgreSQLPoolConfig pool;
        pool.databaseId = databaseId;
        pool.connectionString = connection;
        pool.defaultSchema = schema;
        // Zero asks CSqlPoolManager::GetDBConnNum to apply the exact target
        // cardinality. No deployment-only pool override is accepted here.
        pool.poolSizeOverride = 0;
        result.databases.push_back(std::move(pool));
        if (databaseId == databasePlan.worldId && !foundWorldSchema) {
            result.defaultSchema = schema;
            foundWorldSchema = true;
        }
    }
    if (!foundWorldSchema) {
        error = "internal DBList error: WorldId schema is unavailable";
        return false;
    }

    if (!ParseDatabaseSchemas(
            deployment.Get("PostgreSQL", "database_schemas", ""),
            result.databaseSchemas,
            error)) {
        return false;
    }
    // --check-config validates files/order without connecting or applying SQL.
    std::vector<MigrationTarget> migrations;
    return ReadMigrationTargets(deployment, result, migrations, error);
}

bool ApplyCacheDPostgreSQLMigrations(
    const std::string& deploymentPath,
    const database::PostgreSQLRuntimeConfig& configuration,
    std::string& error) {
    runtime::Config deployment;
    if (!deployment.Load(deploymentPath, false)) {
        error = "cannot read " + deploymentPath;
        return false;
    }
    std::vector<MigrationTarget> targets;
    if (!ReadMigrationTargets(deployment, configuration, targets, error)) return false;
    // Only databases with migrations are created, because the migrations
    // create their schemas. Missing or false keeps the database required.
    const bool createDatabase = IsTrue(deployment.Get("PostgreSQL", "CreateDatabase", "false"));
    for (const auto& target : targets) {
        if (createDatabase &&
            !runtime::CreatePostgreSQLDatabaseIfMissing(target.database->connectionString, "cached", error)) {
            return false;
        }
        runtime::PostgreSQLMigrationReport report;
        if (!runtime::ApplyPostgreSQLMigrations(target.database->connectionString,
                "cached", target.database->defaultSchema, target.migrations, report, error)) return false;
    }
    return true;
}

bool PostgreSQLDeploymentExists(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    return static_cast<bool>(input);
}

std::string DescribeCacheDDatabasePlan(const CacheDDatabasePlan& plan) {
    std::ostringstream description;
    for (std::size_t index = 0; index < plan.databaseIds.size(); ++index) {
        if (index != 0) description << ':';
        const int databaseId = plan.databaseIds[index];
        description << databaseId << '(' << DatabaseRole(databaseId, plan.worldId)
                    << ')';
    }
    return description.str();
}
