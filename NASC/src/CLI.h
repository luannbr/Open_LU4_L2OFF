#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace nasc {

enum class Command {
    Compile,
    PrintVersion,
    GenerateHelp,
    Inventory,
};

struct BuildOptions {
    bool quickCompile = false;
    bool rebuild = false;
    bool useNotepad = true;
    bool skipChecks = false;
    bool singleFileQuestMerge = false;
    bool singleFileMacroMerge = false;
    std::optional<std::filesystem::path> singleCompileFile;

    Command command = Command::Compile;
    std::optional<std::filesystem::path> versionFile;

    std::filesystem::path root = ".";
    std::filesystem::path outputRoot = "nasc-output";
    std::optional<std::filesystem::path> sourceDirectory;
    std::vector<std::string> modules;
    bool createMergedObject = true;
    bool dryRun = false;
    bool showHelp = false;
    std::vector<std::string> ignoredArguments;
};

struct ParseResult {
    BuildOptions options;
    std::vector<std::string> errors;

    [[nodiscard]] explicit operator bool() const noexcept { return errors.empty(); }
};

// Arguments exclude argv[0]. The legacy switches are case-sensitive, like in
// the original.
[[nodiscard]] ParseResult ParseArguments(std::span<const std::string> arguments);

[[nodiscard]] const char* CommandName(Command command) noexcept;

} // namespace nasc
