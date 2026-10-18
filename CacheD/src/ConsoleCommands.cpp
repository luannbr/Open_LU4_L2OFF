#include "ConsoleCommands.h"

#include "Portable/ProcessRuntime.h"
#include "Shared/src/DBConn.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/ConsoleHelp.h"
#include "UserDb.h"

#include <array>
#include <charconv>
#include <string>
#include <vector>

namespace cached::console {
namespace {

bool SetAccountBuilder(const std::string& accountName, int builderLevel) {
    const int databaseId = runtime::process::CurrentDatabaseId();
    std::vector<int> characterIds;
    {
        DBConn connection(databaseId);
        int characterId = 0;
        connection.Bind(&characterId);
        // HandleCommand accepts only ASCII letters, digits and underscore.
        // DBConn supports bound parameters only for procedure calls.
        const std::wstring statement =
            L"SELECT char_id FROM world.user_data WHERE account_id > 0 AND "
            L"world.mssql_text_eq(account_name, '" +
            std::wstring(accountName.begin(), accountName.end()) + L"') ORDER BY char_id";
        if (!connection.ExecuteNoError(statement.c_str())) {
            L2_LOG_ERROR(Log, "[console] Cannot read characters for account '%s'. No builder changes were made.",
                accountName.c_str());
            return false;
        }
        while (connection.Fetch()) characterIds.push_back(characterId);
    }

    std::vector<CUserSP> users;
    users.reserve(characterIds.size());
    for (const int characterId : characterIds) {
        CUserSP user = g_userDb.GetUser(characterId, true, false, databaseId);
        if (!user || user->IsLoggedIn()) {
            L2_LOG_WARN(Log,
                "[console] Builder change refused: character %d of account '%s' is online or unavailable. "
                "Fully log out of the account, including character selection, and retry. No builder changes were made.",
                characterId, accountName.c_str());
            return false;
        }
        users.push_back(user);
    }

    {
        DBConn connection(databaseId);
        constexpr const wchar_t* statement = L"{Call lin_SetBuilderAccount (?, ?)}";
        if (!connection.Prepare(statement)) {
            L2_LOG_ERROR(Log, "[console] Cannot prepare the builder default update. No builder changes were made.");
            return false;
        }
        connection.BindParameter(accountName.c_str());
        connection.BindParameter(&builderLevel);
        if (!connection.ExecutePrepared(statement)) {
            L2_LOG_ERROR(Log, "[console] Cannot save builder default for account '%s'. Existing characters were not changed.",
                accountName.c_str());
            return false;
        }
    }

    std::size_t updated = 0;
    for (const CUserSP& user : users) {
        if (!user->IsLoggedIn() && user->SetBuilderLevExtern(user->GetCharName(), builderLevel)) {
            ++updated;
        } else {
            L2_LOG_ERROR(Log, "[console] Cannot set builder for character %u of account '%s': online or update failed.", user->GetId(),
                accountName.c_str());
        }
    }
    if (updated != characterIds.size()) {
        L2_LOG_ERROR(Log,
            "[console] Builder change for account '%s' in world %d is incomplete: level %d, future-character default saved, "
            "%zu of %zu existing characters updated. Fully log out, then retry the command before logging in again.",
            accountName.c_str(), databaseId, builderLevel, updated, characterIds.size());
        return false;
    }
    L2_LOG_INFO(Log,
        "[console] Account '%s' in world %d: builder level %d saved for %zu existing characters and future characters. "
        "The account must stay fully logged out while this command runs, including character selection. Log in after it completes.",
        accountName.c_str(), databaseId, builderLevel, updated);
    if (characterIds.empty()) {
        L2_LOG_INFO(Log,
            "[console] No existing characters match this account name. CacheD saved the future-character default "
            "but cannot verify whether the account exists in AuthD.");
    }
    return true;
}

void PrintHelp() {
    // L2_LOG_INFO(Log, "[console] Commands: help | account builder <account_name> <level> | quit | exit.");
    // L2_LOG_INFO(Log,
    //     "[console] Builder level: 0 removes builder rights, 1..9 grant builder access. Applies to this world's existing "
    //     "and future characters. Fully log out first, including character selection, then run the command before logging in. "
    //     "Create accounts in AuthD.");
    static constexpr runtime::ConsoleCommandHelp commands[]{
        {"account builder <account_name> <level>", "Set the builder level for all characters of an account in this world."},
        {"help", "Show this list."},
        {"quit, exit", "Stop CacheD."},
    };
    static constexpr std::string_view notes[]{
        "Builder level: 0 removes builder rights, 1..9 grant builder access. It applies to existing and future "
        "characters of the account in this world.",
        "Fully log out of the account first, including character selection. Run the command, then log in.",
        "Create accounts in AuthD with: account create <username> <password>.",
    };
    runtime::WriteConsoleText(runtime::FormatConsoleHelp("CacheD console commands:", commands, notes));
}

} // namespace

bool HandleCommand(std::string_view line, const std::function<void()>& requestStop) {
    std::array<std::string_view, 4> tokens{};
    std::size_t tokenCount = 0;
    while (!line.empty()) {
        const auto start = line.find_first_not_of(" \t");
        if (start == std::string_view::npos) break;
        line.remove_prefix(start);
        if (tokenCount == tokens.size()) {
            L2_LOG_WARN(Log, "[console] Too many arguments. Use help for available commands.");
            return false;
        }
        const auto end = line.find_first_of(" \t");
        tokens[tokenCount++] = line.substr(0, end);
        if (end == std::string_view::npos) break;
        line.remove_prefix(end);
    }
    if (tokenCount == 0) return true;
    if (tokenCount == 1 && tokens[0] == "help") {
        PrintHelp();
        return true;
    }
    if (tokenCount == 1 && (tokens[0] == "quit" || tokens[0] == "exit") && requestStop) {
        L2_LOG_INFO(Log, "[console] Shutdown requested.");
        requestStop();
        return true;
    }
    if (tokenCount != 4 || tokens[0] != "account" || tokens[1] != "builder") {
        L2_LOG_WARN(Log, "[console] Unknown command or missing arguments. Use help for available commands.");
        return false;
    }

    const auto name = tokens[2];
    if (name.empty() || name.size() > 14 || name.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string_view::npos) {
        L2_LOG_WARN(Log, "[console] Account names must contain 1..14 ASCII letters, digits or underscores.");
        return false;
    }
    int level = 0;
    const auto levelText = tokens[3];
    const auto result = std::from_chars(levelText.data(), levelText.data() + levelText.size(), level);
    if (result.ec != std::errc{} || result.ptr != levelText.data() + levelText.size() || level < 0 || level > 9) {
        L2_LOG_WARN(Log, "[console] Builder level must be an integer from 0 to 9.");
        return false;
    }
    std::string accountName(name);
    for (char& character : accountName) {
        if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
    }
    return SetAccountBuilder(accountName, level);
}

} // namespace cached::console
