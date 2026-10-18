#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace runtime {

// Portable deployment feature, separate from recovered database owners.
struct PostgreSQLMigration {
    std::string name;
    std::string source;
    std::string sql;
};

struct PostgreSQLMigrationReport {
    std::size_t applied = 0;
    std::size_t skipped = 0;
};

// Read top-level N_name.sql files. Optional migrations.order must list all of
// them exactly once. Otherwise sort by numeric prefix, then complete filename.
// UTF-8 SQL may have one outer BEGIN/COMMIT pair. The runner owns transactions.
bool LoadPostgreSQLMigrations(
    const std::filesystem::path& directory, std::vector<PostgreSQLMigration>& migrations, std::string& error
);

// Creates the database named by the connection string when it does not exist.
// It uses the same connection options with the postgres or template1 database.
// The user needs the CREATEDB privilege. It does not throw exceptions.
bool CreatePostgreSQLDatabaseIfMissing(
    const std::string& connectionString, const std::string& application, std::string& error
);

// Uses its own connection, before application pools or request listeners start.
// History is per physical database/application, with a SHA-256 of file bytes.
bool ApplyPostgreSQLMigrations(
    const std::string& connectionString,
    const std::string& application,
    const std::string& defaultSchema,
    const std::vector<PostgreSQLMigration>& migrations,
    PostgreSQLMigrationReport& report,
    std::string& error
);

} // namespace runtime
