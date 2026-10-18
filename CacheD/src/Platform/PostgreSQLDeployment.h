#pragma once

#include "DBConn.h"

#include <cstdint>
#include <string>
#include <vector>

struct CacheDConfiguration;

// Platform carrier for the exact DBList state constructed by the current
// CSqlPoolManager::CSqlPoolManager at 0x004a64ec. It is separate from
// CacheDConfiguration because current InitConfig does not own or read DBList.
struct CacheDDatabasePlan {
    int worldId = 0;
    std::string dbListText;
    std::vector<int> databaseIds;
};

// Reads [SQL]/DBList from the target l2cache.ini and applies its current
// wcstok_s(':')/_wtol validation and WorldId-membership contract.
bool LoadCacheDDatabasePlan(
    const std::string& cacheConfigurationPath,
    int worldId,
    CacheDDatabasePlan& result,
    std::string& error,
    bool emitDiagnostics = true);

// Reads the explicit portable transport mapping. This is not claimed as
// target code: it replaces registry/FILEDSN/ODBC while preserving DB IDs,
// per-ID schemas, pool cardinalities and CSqlPoolManager startup order.
bool BuildPostgreSQLRuntimeConfiguration(
    const std::string& deploymentPath,
    const CacheDConfiguration& cacheConfiguration,
    const CacheDDatabasePlan& databasePlan,
    database::PostgreSQLRuntimeConfig& result,
    std::string& error,
    bool emitDiagnostics = true);

// Opt-in migration gate, called before creating the production SQL pools.
bool ApplyCacheDPostgreSQLMigrations(
    const std::string& deploymentPath,
    const database::PostgreSQLRuntimeConfig& configuration,
    std::string& error);

bool PostgreSQLDeploymentExists(const std::string& path);
std::string DescribeCacheDDatabasePlan(const CacheDDatabasePlan& plan);
