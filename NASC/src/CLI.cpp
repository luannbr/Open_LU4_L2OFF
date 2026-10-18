#include "CLI.h"

#include "Platform.h"

#include <string_view>

namespace nasc {
namespace {

bool TakePathArgument(
    std::span<const std::string> arguments,
    std::size_t& index,
    std::optional<std::filesystem::path>& destination,
    std::vector<std::string>& errors,
    std::string_view option
) {
    if (index + 1 >= arguments.size()) {
        errors.emplace_back("missing path after " + std::string(option));
        return false;
    }
    ++index;
    destination = PathFromUtf8(arguments[index]);
    return true;
}

} // namespace

ParseResult ParseArguments(std::span<const std::string> arguments) {
    ParseResult result;

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string& argument = arguments[index];

        if (argument == "-qcompile" || argument == "--quick-compile") {
            result.options.quickCompile = true;
        } else if (argument == "-rebuild" || argument == "--rebuild") {
            result.options.rebuild = true;
        } else if (argument == "-qrebuild" || argument == "--quick-rebuild") {
            result.options.rebuild = true;
            result.options.quickCompile = true;
        } else if (argument == "-nousenotepad" || argument == "--no-editor") {
            result.options.useNotepad = false;
        } else if (argument == "-skip" || argument == "--skip-checks") {
            result.options.skipChecks = true;
        } else if (argument == "-q" || argument == "--quest-merge") {
            result.options.singleFileQuestMerge = true;
        } else if (argument == "-m" || argument == "--macro-merge") {
            result.options.singleFileMacroMerge = true;
        } else if (argument == "-c" || argument == "--compile-file") {
            TakePathArgument(arguments, index, result.options.singleCompileFile, result.errors, argument);
        } else if (argument == "-version") {
            result.options.command = Command::PrintVersion;
            if (TakePathArgument(arguments, index, result.options.versionFile, result.errors, argument)) {
                break; // The original implementation calls exit(0) here.
            }
        } else if (argument == "-genhelp") {
            result.options.command = Command::GenerateHelp;
            break; // The original implementation calls exit(0) here.
        } else if (argument == "--inventory") {
            result.options.command = Command::Inventory;
        } else if (argument == "--root") {
            if (index + 1 >= arguments.size()) {
                result.errors.emplace_back("missing path after --root");
            } else {
                ++index;
                result.options.root = PathFromUtf8(arguments[index]);
            }
        } else if (argument == "--output") {
            if (index + 1 >= arguments.size()) {
                result.errors.emplace_back("missing path after --output");
            } else {
                ++index;
                result.options.outputRoot = PathFromUtf8(arguments[index]);
            }
        } else if (argument == "--source-dir") {
            TakePathArgument(arguments, index, result.options.sourceDirectory, result.errors, argument);
        } else if (argument == "--module") {
            if (index + 1 >= arguments.size()) {
                result.errors.emplace_back("missing name after --module");
            } else {
                ++index;
                result.options.modules.push_back(arguments[index]);
            }
        } else if (argument == "--no-merge") {
            result.options.createMergedObject = false;
        } else if (argument == "--dry-run") {
            result.options.dryRun = true;
        } else if (argument == "--help" || argument == "-h") {
            result.options.showHelp = true;
        } else {
            // Unknown arguments are ignored, like in the original.
            result.options.ignoredArguments.push_back(argument);
        }
    }

    return result;
}

const char* CommandName(Command command) noexcept {
    switch (command) {
        case Command::Compile:
            return "compile";
        case Command::PrintVersion:
            return "print-version";
        case Command::GenerateHelp:
            return "generate-help";
        case Command::Inventory:
            return "inventory";
    }
    return "unknown";
}

} // namespace nasc
