// Interactive `.command` shell implementation for the portable L2NPC host.
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/ConsoleHelp.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <iterator>
#include <sstream>
#include <thread>
#include "Portable/Console.h"
#include "Runtime.h" // Shared: portable runtime

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

namespace l2npc::portable {
namespace {

bool IsInteractiveInput() {
#ifdef _WIN32
    return ::_isatty(::_fileno(stdin)) != 0;
#else
    return ::isatty(STDIN_FILENO) != 0;
#endif
}

// 1 = input available, 0 = timeout, -1 = EOF/error.
int WaitForInput(int timeoutMilliseconds) {
#ifdef _WIN32
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    // if (input == nullptr || input == INVALID_HANDLE_VALUE) return -1.
    if (input == nullptr || input == INVALID_HANDLE_VALUE) {
        L2_LOG_WARN(Log, "[console] Standard input has no usable Windows handle; console input is unavailable.");
        return -1;
    }
    const DWORD result = WaitForSingleObject(input, static_cast<DWORD>(timeoutMilliseconds));
    if (result == WAIT_TIMEOUT) {
        return 0;
    }
    // return result == WAIT_OBJECT_0 ? 1 : -1
    if (result == WAIT_OBJECT_0) {
        // A console handle is signaled by any input event (focus, mouse,
        // a key without Enter). std::getline would then block until Enter
        // and the caller could no longer see a shutdown request, so report
        // input only once a whole line (an Enter key press) is queued.
        DWORD mode = 0;
        if (!::GetConsoleMode(input, &mode)) {
            return 1; // pipe or file: data is ready to read
        }
        INPUT_RECORD records[128];
        DWORD count = 0;
        if (!::PeekConsoleInputW(input, records, static_cast<DWORD>(std::size(records)), &count)) {
            return 1;
        }
        bool hasKey = false;
        for (DWORD index = 0; index < count; ++index) {
            if (records[index].EventType != KEY_EVENT) {
                continue;
            }
            hasKey = true;
            const KEY_EVENT_RECORD& key = records[index].Event.KeyEvent;
            if (key.bKeyDown && key.uChar.UnicodeChar == L'\r') {
                return 1;
            }
        }
        if (!hasKey) {
            ::FlushConsoleInputBuffer(input); // only non-key events are queued
        } else {
            ::Sleep(static_cast<DWORD>(timeoutMilliseconds)); // a line is being typed
        }
        return 0;
    }
    if (result == WAIT_FAILED) {
        const DWORD error = ::GetLastError();
        L2_LOG_ERROR(
            Log, "[console] Waiting for standard input failed (Windows error %lu).", static_cast<unsigned long>(error)
        );
        ::SetLastError(error);
    } else {
        L2_LOG_ERROR(
            Log, "[console] Waiting for standard input returned unexpected status 0x%lx.", static_cast<unsigned long>(result)
        );
    }
    return -1;
#else
    pollfd descriptor{};
    descriptor.fd = STDIN_FILENO;
    descriptor.events = POLLIN;
    const int result = ::poll(&descriptor, 1, timeoutMilliseconds);
    if (result == 0) {
        return 0;
    }
    if (result < 0) {
        const int error = errno;
        L2_LOG_ERROR(Log, "[console] Waiting for standard input failed (poll errno %d).", error);
        errno = error;
        return -1;
    }
    // A finite pipe commonly reports POLLIN|POLLHUP together. Consume its
    // final command before treating the later HUP as EOF.
    if ((descriptor.revents & POLLIN) != 0) {
        return 1;
    }
    if ((descriptor.revents & (POLLERR | POLLNVAL)) != 0) {
        L2_LOG_ERROR(
            Log, "[console] Standard input descriptor is unavailable (poll events 0x%x, error=%d, invalid=%d).",
            static_cast<unsigned int>(descriptor.revents), (descriptor.revents & POLLERR) != 0 ? 1 : 0,
            (descriptor.revents & POLLNVAL) != 0 ? 1 : 0
        );
    }
    return (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 ? -1 : 0;
#endif
}

} // namespace

void Console::Register(const std::string& name, const std::string& help, Handler h) {
    cmds_[name] = Cmd{help, std::move(h)};
}

void Console::printHelp() const {
    // std::printf("\nAvailable commands (prefix with '.'):\n")
    // std::printf("  .%-16s %s\n", name.c_str(), c.help.c_str()) for each command
    std::vector<std::string> usages;
    usages.reserve(cmds_.size());
    for (const auto& entry : cmds_) {
        usages.push_back("." + entry.first);
    }
    std::vector<runtime::ConsoleCommandHelp> commands;
    commands.reserve(cmds_.size() + 2);
    std::size_t index = 0;
    for (const auto& entry : cmds_) {
        commands.push_back({usages[index++], entry.second.help});
    }
    commands.push_back({".help, .?", "show this list"});
    commands.push_back({".quit, .exit", "shut down and exit"});
    runtime::WriteConsoleText(runtime::FormatConsoleHelp("L2NPC console commands (start each with '.'):", commands));
}

void Console::Execute(const std::string& line) {
    std::string s = line;
    // trim
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) {
        return;
    }
    s = s.substr(a);
    if (s.empty()) {
        return;
    }
    if (s[0] != '.') {
        // std::printf("commands start with '.', try .help\n")
        // Log.Add(LOG_WARN, "[console] Command rejected: commands must start with a dot. Use .help to list commands.")
        L2_LOG_WARN(Log, "[console] Command rejected: commands must start with a dot. Use .help to list commands.");
        return;
    }
    std::istringstream iss(s.substr(1));
    std::string name;
    iss >> name;
    std::vector<std::string> args;
    for (std::string tok; iss >> tok;) {
        args.push_back(tok);
    }

    if (name == "help" || name == "?") {
        printHelp();
        return;
    }
    if (name == "quit" || name == "exit") {
        RequestQuit();
        return;
    }

    auto it = cmds_.find(name);
    if (it == cmds_.end()) {
        // std::printf("unknown command '.%s'  -  try .help\n", name.c_str())
        // Log.Add(LOG_WARN, "[console] Unknown command \".%s\". Use .help to list commands.", name.c_str())
        L2_LOG_WARN(Log, "[console] Unknown command \".%s\". Use .help to list commands.", name.c_str());
        return;
    }
    try {
        it->second.fn(args);
    } catch (const std::exception& e) {
        // Log.Add(LOG_ERROR, "command .%s failed: %s", name.c_str(), e.what())
        // Log.Add(
        //     LOG_ERROR,
        //     "[console] Command \".%s\" threw an exception: %s. Its changes may be partially applied.",
        //     name.c_str(), e.what())
        L2_LOG_ERROR(
            Log, "[console] Command \".%s\" threw an exception: %s. Its changes may be partially applied.", name.c_str(),
            e.what()
        );
    }
}

int Console::Run(const std::function<bool()>& keepRunning) {
    const bool interactive = IsInteractiveInput();
    if (interactive) {
        printHelp();
    }
    std::string line;
    bool inputOpen = true;
    bool promptVisible = false;
    while (!quit_.load() && (!keepRunning || keepRunning())) {
        if (!inputOpen) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }
        if (interactive && !promptVisible) {
            std::printf("l2npc> ");
            std::fflush(stdout);
            promptVisible = true;
        }
        const int ready = WaitForInput(250);
        if (ready == 0) {
            continue;
        }
        if (ready < 0 || !std::getline(std::cin, line)) {
            // Log.Add(LOG_NORMAL,
            //     "[console] Standard input reached EOF or became unavailable. Console input is disabled while the service
            //     loop continues.")
            // L2_LOG_INFO(Log,
            //     "[console] Standard input reached EOF or became unavailable. Console input is disabled while the service
            //     loop continues.")
            if (ready < 0) {
                L2_LOG_INFO(
                    Log,
                    "[console] Standard input closed or stopped being available; console input is disabled. The server "
                    "continues running."
                );
            } else if (std::cin.bad() || !std::cin.eof()) {
                L2_LOG_ERROR(
                    Log,
                    "[console] Reading a command from standard input failed; console input is disabled. The server "
                    "continues running."
                );
            } else {
                L2_LOG_INFO(
                    Log, "[console] End of standard input; console input is disabled. The server continues running."
                );
            }
            inputOpen = false;
            continue;
        }
        promptVisible = false;
        Execute(line);
    }
    return 0;
}

} // namespace l2npc::portable
