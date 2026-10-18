#include "ConsoleCommands.h"

#include "Account.h"
#include "Log.h"
#include "Shared/src/Runtime/ConsoleHelp.h"

#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace authd {
namespace {

void PrintHelp() {
    // L2_LOG_INFO(Log,
    //     "[console] Commands: account create <username> <password>, help, quit, exit. "
    //     "Use CacheD for account builder <username> <level>.");
    static constexpr runtime::ConsoleCommandHelp commands[]{
        {"account create <username> <password>", "Create a local account."},
        {"help", "Show this list."},
        {"quit, exit", "Stop AuthD."},
    };
    static constexpr std::string_view notes[]{
        "Username: 1 to 14 lowercase letters, digits or underscores. "
        "Password: 1 to 16 printable ASCII characters. Put a password with spaces in double quotes.",
        "Builder rights are set in CacheD with: account builder <username> <level>.",
    };
    runtime::WriteConsoleText(runtime::FormatConsoleHelp("AuthD console commands:", commands, notes));
}

} // namespace

bool ExecuteConsoleCommand(std::string_view line) {
    std::istringstream input{std::string(line)};
    std::vector<std::string> arguments;
    while (input >> std::ws && input.peek() != std::char_traits<char>::eof()) {
        std::string argument;
        if (arguments.size() == 4 || !(input >> std::quoted(argument))) {
            L2_LOG_WARN(Log, "[console] Invalid command arguments. Use help for command syntax.");
            return false;
        }
        arguments.push_back(std::move(argument));
    }
    if (arguments.empty()) {
        return false;
    }
    if (arguments.size() == 1 && arguments[0] == "help") {
        PrintHelp();
        return false;
    }
    if (arguments.size() == 1 && (arguments[0] == "quit" || arguments[0] == "exit")) {
        L2_LOG_INFO(Log, "[console] Shutdown requested.");
        return true;
    }
    if (arguments.size() != 4 || arguments[0] != "account" || arguments[1] != "create") {
        L2_LOG_WARN(Log, "[console] Unknown command or wrong arguments: \"%s\".", arguments[0].c_str());
        PrintHelp();
        return false;
    }

    const auto result = CAccount::CreateLocalAccount(arguments[2], arguments[3]);
    switch (result) {
    case LocalAccountCreateResult::Created:
        L2_LOG_INFO(Log, "[console] Account \"%s\" created.", arguments[2].c_str());
        break;
    case LocalAccountCreateResult::AlreadyExists:
        L2_LOG_WARN(Log, "[console] Account \"%s\" already exists.", arguments[2].c_str());
        break;
    case LocalAccountCreateResult::InvalidName:
        L2_LOG_WARN(Log, "[console] Account name must contain 1 to 14 lowercase letters, digits or underscores.");
        break;
    case LocalAccountCreateResult::InvalidPassword:
        L2_LOG_WARN(Log, "[console] Password must contain 1 to 16 printable ASCII characters.");
        break;
    case LocalAccountCreateResult::DatabaseError:
        L2_LOG_ERROR(Log, "[console] Could not create the account. Check the database log and AuthD migrations.");
        break;
    case LocalAccountCreateResult::Unsupported:
        L2_LOG_WARN(Log, "[console] Account creation requires the PostgreSQL database transport.");
        break;
    }
    return false;
}

} // namespace authd
