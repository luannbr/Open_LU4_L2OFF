#include "CLI.h"

#include "AICompiler.h"
#include "CompileInventory.h"
#include "Encoding.h"
#include "HelpGenerator.h"
#include "Platform.h"
#include "Version.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

void PrintHelp() {
    std::cout << "NASC: portable NASC64_2015 AI script compiler\n\n"
              << "Recognized legacy options (inputs remain read-only):\n"
              << "  -qcompile             accept the legacy quick-build request\n"
              << "  -rebuild              request a full deterministic build\n"
              << "  -qrebuild             request a quick full build\n"
              << "  -nousenotepad         never launch the Windows editor\n"
              << "  -skip                 do not run companion checkers\n"
              << "  -q, -m                 legacy external hooks used with -c\n"
              << "  -c PATH                build one AI module safely\n"
              << "  -version PATH          inspect NASC version data\n"
              << "  -genhelp               generate type index/skeleton (terminal)\n\n"
              << "Portable control options:\n"
              << "  --root PATH            script working tree (default: .)\n"
              << "  --output PATH          isolated output tree (default: ./nasc-output)\n"
              << "  --source-dir PATH      preprocessed .ai directory (default: ROOT/ai/src)\n"
              << "  --module NAME          build one root module; may be repeated\n"
              << "  --no-merge             create module .obj/.sym files, but no ai.obj\n"
              << "  --dry-run              print the normalized build plan\n"
              << "  --inventory            inspect ai/*.ai and compile_list.dat\n"
              << "  --help, -h             show this help\n\n"
              << "Place --output before terminal -genhelp; legacy terminal options\n"
              << "stop argument parsing at their original position.\n\n"
              << "Safety: input files are read-only, existing outputs are never\n"
              << "overwritten, and in-place output is disabled.\n";
}

void PrintPlan(const nasc::BuildOptions& options) {
    std::cout << "command=" << nasc::CommandName(options.command) << '\n'
              << "root=" << nasc::PathToUtf8(options.root) << '\n'
              << "output_root=" << nasc::PathToUtf8(options.outputRoot) << '\n'
              << "quick_compile=" << std::boolalpha << options.quickCompile << '\n'
              << "rebuild=" << options.rebuild << '\n'
              << "use_notepad=" << options.useNotepad << '\n'
              << "skip_checks=" << options.skipChecks << '\n'
              << "single_file_quest_merge=" << options.singleFileQuestMerge << '\n'
              << "single_file_macro_merge=" << options.singleFileMacroMerge << '\n'
              << "create_merged_object=" << options.createMergedObject << '\n';
    if (options.sourceDirectory) {
        std::cout << "source_directory=" << nasc::PathToUtf8(*options.sourceDirectory) << '\n';
    }
    for (const std::string& module : options.modules) {
        std::cout << "module=" << module << '\n';
    }
    if (options.singleCompileFile) {
        std::cout << "single_compile_file=" << nasc::PathToUtf8(*options.singleCompileFile) << '\n';
    }
    if (options.versionFile) {
        std::cout << "version_file=" << nasc::PathToUtf8(*options.versionFile) << '\n';
    }
    for (const std::string& ignored : options.ignoredArguments) {
        std::cout << "ignored_argument=" << ignored << '\n';
    }
}

void PrintCompileReport(const nasc::AIBuildReport& report) {
    for (const auto& module : report.modules) {
        std::cout << "module=" << module.module << " accepted=" << std::boolalpha << module.accepted
                  << " event_type=" << module.eventTypeId << " includes=" << module.includes.size()
                  << " actions=" << module.semanticActions << " object_utf16_units=" << module.objectUtf16Units
                  << " symbol_utf16_units=" << module.symbolUtf16Units
                  << " lexer_diagnostics=" << module.lexerDiagnostics.size()
                  << " parser_diagnostics=" << module.parserDiagnostics.size()
                  << " semantic_diagnostics=" << module.semanticDiagnostics.size() << '\n';
        if (module.parserErrorToken) {
            std::cout << "  parser_error line=" << module.parserErrorLine.value_or(0) << " state=" << module.parserErrorState
                      << " token=0x" << std::hex << *module.parserErrorToken << std::dec << '\n';
        }
        for (const auto& diagnostic : module.lexerDiagnostics) {
            std::cout << "  lexer line=" << diagnostic.line << ": " << nasc::Utf16ToUtf8(diagnostic.message) << '\n';
        }
        for (const auto& diagnostic : module.parserDiagnostics) {
            std::cout << "  parser line=" << diagnostic.line << " state=" << diagnostic.state << " token=0x" << std::hex
                      << diagnostic.token << std::dec << ": " << nasc::Utf16ToUtf8(diagnostic.message) << '\n';
        }
        for (std::size_t index = 0; index < module.semanticDiagnostics.size(); ++index) {
            std::cout << "  semantic";
            if (index < module.semanticDiagnosticLines.size()) {
                std::cout << " line=" << module.semanticDiagnosticLines[index];
            }
            std::cout << ": " << nasc::Utf16ToUtf8(module.semanticDiagnostics[index]) << '\n';
        }
    }
    for (const std::string& diagnostic : report.diagnostics) {
        std::cerr << "error: " << diagnostic << '\n';
    }
    if (!report.succeeded) {
        return;
    }

    std::cout << "compile_order=";
    for (std::size_t index = 0; index < report.compileOrder.size(); ++index) {
        if (index != 0) {
            std::cout << ',';
        }
        std::cout << report.compileOrder[index];
    }
    std::cout << '\n';
    if (!report.mergedObjectPath.empty()) {
        std::cout << "merged_object=" << nasc::PathToUtf8(report.mergedObjectPath) << '\n';
    }
}

int Run(std::vector<std::string> arguments) {
    const nasc::ParseResult parsed = nasc::ParseArguments(arguments);
    if (!parsed) {
        for (const std::string& error : parsed.errors) {
            std::cerr << "error: " << error << '\n';
        }
        return 2;
    }
    if (parsed.options.showHelp) {
        PrintHelp();
        return 0;
    }
    if (parsed.options.singleCompileFile) {
        const std::filesystem::path candidate = parsed.options.root / *parsed.options.singleCompileFile;
        if (!nasc::RegularFileExists(candidate)) {
            std::cerr << "error: input file does not exist: " << nasc::PathToUtf8(candidate) << '\n';
            return 2;
        }
    }
    if (parsed.options.dryRun) {
        PrintPlan(parsed.options);
        return 0;
    }
    if (parsed.options.command == nasc::Command::PrintVersion) {
        try {
            std::filesystem::path object = *parsed.options.versionFile;
            if (object.is_relative()) {
                object = parsed.options.root / object;
            }
            const nasc::VersionInfo info = nasc::InspectObjectHeader(object);
            std::cout << nasc::FormatVersionInfo(info) << "Compatible " << (nasc::IsLegacyCompatible(info) ? "yes" : "no")
                      << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return 2;
        }
    }
    if (parsed.options.command == nasc::Command::Inventory) {
        try {
            const std::filesystem::path list = parsed.options.root / "ai" / "data" / "compile_list.dat";
            const nasc::CompileInventory previous = nasc::LoadCompileList(list);
            const nasc::CompileInventory current = nasc::ScanScriptTree(parsed.options.root, previous);
            std::size_t changed = 0;
            std::size_t regular = 0;
            std::size_t development = 0;
            std::size_t test = 0;
            std::size_t broadcast = 0;
            for (const auto& [name, info] : current) {
                changed += nasc::NeedsRecompile(name, info, previous, parsed.options.rebuild);
                switch (info.scriptTree) {
                    case nasc::ScriptTree::Regular:
                        ++regular;
                        break;
                    case nasc::ScriptTree::Development:
                        ++development;
                        break;
                    case nasc::ScriptTree::Test:
                        ++test;
                        break;
                    case nasc::ScriptTree::Broadcast:
                        ++broadcast;
                        break;
                }
            }
            std::cout << "current_files=" << current.size() << '\n'
                      << "previous_files=" << previous.size() << '\n'
                      << "timestamp_changed_or_new=" << changed << '\n'
                      << "regular=" << regular << '\n'
                      << "development=" << development << '\n'
                      << "test=" << test << '\n'
                      << "broadcast=" << broadcast << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return 2;
        }
    }
    if (parsed.options.command == nasc::Command::GenerateHelp) {
        const auto report = nasc::GenerateScriptHelp(parsed.options.outputRoot);
        for (const std::string& diagnostic : report.diagnostics) {
            std::cerr << "error: " << diagnostic << '\n';
        }
        if (!report.succeeded) {
            return 1;
        }
        std::cout << "help_index=" << nasc::PathToUtf8(report.indexPath) << '\n'
                  << "help_skeletons=" << report.skeletonPaths.size() << '\n';
        return 0;
    }

    if (parsed.options.singleCompileFile && (parsed.options.singleFileQuestMerge || parsed.options.singleFileMacroMerge)) {
        std::cerr << "error: legacy -c combined with -q/-m invokes external "
                  << "TotalChecker/MacroDataChecker programs; that pipeline is not "
                  << "enabled by the portable compiler\n";
        return 3;
    }

    nasc::AIBuildRequest request;
    request.scriptRoot = parsed.options.root;
    request.outputRoot = parsed.options.outputRoot;
    request.modules = parsed.options.modules;
    request.createMergedObject = parsed.options.createMergedObject;
    if (parsed.options.sourceDirectory) {
        request.sourceDirectory = *parsed.options.sourceDirectory;
        if (request.sourceDirectory.is_relative()) {
            request.sourceDirectory = parsed.options.root / request.sourceDirectory;
        }
    }
    if (parsed.options.singleCompileFile) {
        std::filesystem::path candidate = *parsed.options.singleCompileFile;
        if (candidate.is_relative()) {
            candidate = parsed.options.root / candidate;
        }
        request.sourceDirectory = candidate.parent_path();
        request.modules = {nasc::PathToUtf8(candidate.filename())};
    }

    const auto report = nasc::CompileAITree(request);
    PrintCompileReport(report);
    return report.succeeded ? 0 : 1;
}

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
    for (int index = 1; index < argc; ++index) {
        std::u16string utf16;
        for (const wchar_t* cursor = argv[index]; *cursor != L'\0'; ++cursor) {
            utf16.push_back(static_cast<char16_t>(*cursor));
        }
        arguments.push_back(nasc::Utf16ToUtf8(utf16));
    }
    return Run(std::move(arguments));
}
#else
int main(int argc, char** argv) {
    std::vector<std::string> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    return Run(std::move(arguments));
}
#endif
