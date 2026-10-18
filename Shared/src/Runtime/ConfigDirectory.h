#pragma once

#include "ResourceDirectories.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Opt-in deployment extension, not a recovered shipping command-line option.
// Initialize once at process entry, before startup changes cwd or starts workers.
namespace runtime {

inline std::filesystem::path& ConfigDirectoryStorage() {
    static std::filesystem::path directory;
    return directory;
}

inline bool HasConfigDirectory() { return !ConfigDirectoryStorage().empty(); }

inline std::filesystem::path ConfigFile(
    const std::filesystem::path& defaultPath, const std::filesystem::path& relativeName
) {
    return HasConfigDirectory() ? ConfigDirectoryStorage() / relativeName : defaultPath;
}

inline std::filesystem::path ConfigFile(const std::filesystem::path& defaultPath) {
    return ConfigFile(defaultPath, defaultPath.filename());
}

inline bool HasDirectoryArguments() {
    const auto& resources = ResourceDirectoryStorage();
    return HasConfigDirectory() || resources.scriptsFromCommandLine || resources.htmlFromCommandLine ||
        resources.geoFromCommandLine;
}

// Consume deployment options before forwarding the remaining legacy switches.
// Publish only after every argument is valid. Never change the working directory.
inline bool InitializeDirectoryArguments(int& argc, char** argv) {
    constexpr std::array<std::string_view, 4> options{"--config-dir", "--scripts-dir", "--html-dir", "--geo-dir"};
    std::array<bool, 4> seen{};
    std::vector<bool> consumed(static_cast<std::size_t>(argc), false);
    auto configDirectory = ConfigDirectoryStorage();
    auto resources = ResourceDirectoryStorage();
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        for (std::size_t option = 0; option < options.size(); ++option) {
            const auto name = options[option];
            const bool separate = argument == name;
            if (!separate &&
                !(argument.substr(0, name.size()) == name && argument.size() > name.size() &&
                    argument[name.size()] == '=')) {
                continue;
            }
            if (seen[option]) {
                // std::fprintf(stderr, "%s may only be specified once\n", name.data())
                const int diagnosticErrno = errno;
                char message[160]{};
                // All names come from the four fixed options above and fit in this buffer.
                std::snprintf(
                    message, sizeof(message),
                    "[startup] Option %.*s was specified more than once; directory arguments rejected.",
                    static_cast<int>(name.size()), name.data()
                );
                // logging::Write("", logging::Level::Error, message)
                logging::Write("", logging::Level::Error, message, __FILE__);
                errno = diagnosticErrno;
                return false;
            }
            seen[option] = true;
            consumed[index] = true;
            std::string value;
            if (separate) {
                if (index + 1 >= argc || std::string_view(argv[index + 1]).substr(0, 2) == "--") {
                    // std::fprintf(stderr, "%s requires a directory path\n", name.data())
                    const int diagnosticErrno = errno;
                    char message[160]{};
                    // All names come from the four fixed options above and fit in this buffer.
                    std::snprintf(
                        message, sizeof(message),
                        "[startup] Option %.*s requires a directory path; directory arguments rejected.",
                        static_cast<int>(name.size()), name.data()
                    );
                    // logging::Write("", logging::Level::Error, message)
                    logging::Write("", logging::Level::Error, message, __FILE__);
                    errno = diagnosticErrno;
                    return false;
                }
                value = argv[++index];
                consumed[index] = true;
            } else {
                value = argument.substr(name.size() + 1);
            }
            auto& directory = option == 0 ? configDirectory
                : option == 1             ? resources.scripts
                : option == 2             ? resources.html
                                          : resources.geo;
            if (!ResolveDirectoryArgument(value, {}, name.data(), directory)) {
                return false;
            }
            if (option == 1) {
                resources.scriptsFromCommandLine = true;
            }
            if (option == 2) {
                resources.htmlFromCommandLine = true;
            }
            if (option == 3) {
                resources.geoFromCommandLine = true;
            }
            break;
        }
    }
    ConfigDirectoryStorage() = std::move(configDirectory);
    ResourceDirectoryStorage() = std::move(resources);
    int remaining = 1;
    for (int index = 1; index < argc; ++index) {
        if (!consumed[index]) {
            argv[remaining++] = argv[index];
        }
    }
    argc = remaining;
    argv[argc] = nullptr;
    return true;
}

// The Windows GUI entry points keep their original raw command line when
// no override is present. After consuming the override only legacy switches
// are forwarded to their existing command handlers.
inline std::string RemainingDirectoryArguments(int argc, char** argv) {
    std::string result;
    for (int index = 1; index < argc; ++index) {
        if (index > 1) {
            result.push_back(' ');
        }
        result += argv[index];
    }
    return result;
}

} // namespace runtime
