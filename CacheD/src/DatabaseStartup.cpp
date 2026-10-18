#include "Shared/src/Log.h"
#include "Shared/src/TextRuntime.h"

#include "DatabaseStartup.h"

#include "DBConn.h"
#include "Runtime.h"

#include <string_view>
#include <string>

namespace {

constexpr char16_t kRequiredDatabaseVersion[] = u"L2_WORK_2018_05_15_DAILYBUILD_05_00";

// std::string VersionForLog(const wchar_t* value) {
//     std::string result
//     if (value == nullptr)
//         return result
//     while (*value != L'\0') {
//         const wchar_t character = *value++
//         result.push_back(character >= 0 && character <= 0x7f ? static_cast<char>(character) : '?')
//     }
//     return result
// }
std::string VersionForLog(const char16_t* value) {
    return value ? Utf16ToUtf8(value) : std::string{};
}

} // namespace

bool CheckDatabaseVersion(int databaseId) {
    // The target allocates 104 WCHARs but binds exactly 200 bytes (100 UTF-16
    // units) before executing and fetching lin_GetDBVersion.
    char16_t databaseVersion[104]{};
    DBConn connection(databaseId);
    connection.Bind(databaseVersion, 200);
    connection.Prepare(L"{Call lin_GetDBVersion}");
    if (!connection.ExecutePrepared(L"{Call lin_GetDBVersion}") || !connection.Fetch()) {
        // Log.Add(LOG_ERROR, "DB Version not found, check out db_version_change_history table.")
        // Log.Add(
        //     LOG_ERROR,
        //     "component=database event=version_check_failed database_id=%d operation=lin_GetDBVersion reason=execute_or_fetch_failed check=db_version_change_history action=reject_startup",
        //     databaseId)
        // Log.Add(
        //     LOG_ERROR,
        //     "[database %d] Could not execute or fetch lin_GetDBVersion. Check db_version_change_history. Startup rejected.",
        //     databaseId)
        L2_LOG_ERROR(Log,
            "[database %d] Could not execute or fetch lin_GetDBVersion. Check db_version_change_history; startup rejected.",
            databaseId);
        return false;
    }

    if (std::u16string_view(databaseVersion) == kRequiredDatabaseVersion) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR, "DB Version not matched, %s, %s", VersionForLog(databaseVersion).c_str(),
    //     VersionForLog(kRequiredDatabaseVersion).c_str()
    // )
    // Log.Add(
    //     LOG_ERROR,
    //     "component=database event=version_mismatch database_id=%d received=\"%.400s\" expected=\"%s\" action=reject_startup",
    //     databaseId, VersionForLog(databaseVersion).c_str(), VersionForLog(kRequiredDatabaseVersion).c_str())
    // Log.Add(
    //     LOG_ERROR,
    //     "[database %d] Version mismatch: received \"%.400s\", expected \"%s\". Startup rejected.",
    //     databaseId, VersionForLog(databaseVersion).c_str(), VersionForLog(kRequiredDatabaseVersion).c_str())
    L2_LOG_ERROR(Log,
        "[database %d] Version mismatch: received \"%.400s\", expected \"%s\". Startup rejected.",
        databaseId, VersionForLog(databaseVersion).c_str(), VersionForLog(kRequiredDatabaseVersion).c_str());
    return false;
}
