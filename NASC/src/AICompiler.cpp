#include "AICompiler.h"

#include "AIParser.h"
#include "AISymbolDatabase.h"
#include "CodeGenerator.h"
#include "CompileInventory.h"
#include "Encoding.h"
#include "Platform.h"
#include "SemanticActions.h"
#include "Target.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace nasc {
namespace {

enum class VisitState {
    Visiting,
    Complete,
    Failed,
};

[[nodiscard]] bool AsciiAIExtension(const std::filesystem::path& path) {
    std::string extension = nasc::PathToUtf8(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return extension == ".ai";
}

[[nodiscard]] std::string AsciiLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

[[nodiscard]] std::string ModuleKey(std::string_view name) {
    std::filesystem::path path = nasc::PathFromUtf8(name);
    std::string filename = nasc::PathToUtf8(path.filename());
    if (filename.empty()) {
        filename.assign(name);
    }
    if (!AsciiAIExtension(nasc::PathFromUtf8(filename))) {
        filename.append(".ai");
    }
    return AsciiLower(std::move(filename));
}

[[nodiscard]] bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right) {
    std::error_code leftError;
    std::error_code rightError;
    const std::filesystem::path canonicalLeft = std::filesystem::weakly_canonical(left, leftError);
    const std::filesystem::path canonicalRight = std::filesystem::weakly_canonical(right, rightError);
    if (!leftError && !rightError) {
        return canonicalLeft == canonicalRight;
    }
    return left.lexically_normal() == right.lexically_normal();
}

void WriteUtf16Units(std::ostream& output, std::u16string_view text) {
    for (const char16_t character : text) {
        const auto unit = static_cast<std::uint16_t>(character);
        output.put(static_cast<char>(unit & 0xffU));
        output.put(static_cast<char>(unit >> 8U));
    }
}

[[nodiscard]] std::filesystem::path TemporarySibling(const std::filesystem::path& target) {
    std::filesystem::path candidate = target;
    candidate += ".tmp-" + std::to_string(nasc::MonotonicMilliseconds());
    for (std::uint32_t suffix = 0; std::filesystem::exists(candidate); ++suffix) {
        candidate = target;
        candidate += ".tmp-" + std::to_string(nasc::MonotonicMilliseconds()) + "-" + std::to_string(suffix);
    }
    return candidate;
}

template <typename Writer>
void WriteNewFileAtomically(const std::filesystem::path& target, Writer&& writer) {
    if (std::filesystem::exists(target)) {
        throw std::runtime_error("refusing to overwrite existing output: " + nasc::PathToUtf8(target));
    }
    if (!target.parent_path().empty()) {
        std::filesystem::create_directories(target.parent_path());
    }

    const std::filesystem::path temporary = TemporarySibling(target);
    try {
        writer(temporary);
        std::filesystem::rename(temporary, target);
    } catch (...) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        throw;
    }
}

void WriteNewUtf16File(const std::filesystem::path& target, std::u16string_view text) {
    WriteNewFileAtomically(target, [text](const auto& temporary) { nasc::WriteUtf16leFile(temporary, text); });
}

[[nodiscard]] std::string DescribeIncludeCycle(std::span<const std::string> stack, std::string_view repeated) {
    std::string result("circular include dependency: ");
    const auto begin = std::find(stack.begin(), stack.end(), repeated);
    const auto cycleBegin = begin == stack.end() ? stack.begin() : begin;
    bool first = true;
    for (auto cursor = cycleBegin; cursor != stack.end(); ++cursor) {
        if (!first) {
            result.append(" -> ");
        }
        first = false;
        result.append(*cursor);
    }
    if (!first) {
        result.append(" -> ");
    }
    result.append(repeated);
    return result;
}

class CompilerRun {
public:
    explicit CompilerRun(const AIBuildRequest& request) : request_(request) {}

    [[nodiscard]] AIBuildReport Run() {
        ValidatePaths();
        IndexSources();
        report_.precompiledHeaders = precompiledHeaders_.Load(request_.scriptRoot);
        if (!report_.precompiledHeaders.missingFiles.empty()) {
            for (const auto& path : report_.precompiledHeaders.missingFiles) {
                report_.diagnostics.push_back("missing precompiled header: " + nasc::PathToUtf8(path));
            }
            return std::move(report_);
        }
        if (report_.precompiledHeaders.skillVersionMismatch) {
            report_.diagnostics.emplace_back("skill_pch2.txt has a version mismatch");
            return std::move(report_);
        }

        const std::vector<std::string> roots = RequestedModules();
        if (roots.empty()) {
            report_.diagnostics.emplace_back("no AI source modules selected");
            return std::move(report_);
        }

        bool allSucceeded = true;
        for (const std::string& root : roots) {
            allSucceeded = CompileModule(ModuleKey(root)) && allSucceeded;
        }
        if (!allSucceeded) {
            return std::move(report_);
        }

        if (request_.createMergedObject) {
            report_.mergedObjectPath = request_.outputRoot / "ai.obj";
            MergeObjects(report_.mergedObjectPath);
        }
        report_.succeeded = true;
        return std::move(report_);
    }

private:
    void ValidatePaths() {
        if (request_.scriptRoot.empty()) {
            throw std::invalid_argument("script_root is required");
        }
        if (request_.outputRoot.empty()) {
            throw std::invalid_argument("output_root is required");
        }
        if (!request_.allowInPlace && SamePath(request_.scriptRoot, request_.outputRoot)) {
            throw std::invalid_argument("in-place output is disabled; choose a separate output_root");
        }
        std::error_code outputError;
        if (std::filesystem::exists(request_.outputRoot, outputError)) {
            if (outputError || !std::filesystem::is_directory(request_.outputRoot, outputError)) {
                throw std::invalid_argument(
                    "output_root exists but is not a directory: " + nasc::PathToUtf8(request_.outputRoot)
                );
            }
            const bool empty = std::filesystem::is_empty(request_.outputRoot, outputError);
            if (outputError) {
                throw std::runtime_error("cannot inspect output_root: " + outputError.message());
            }
            if (!empty) {
                throw std::invalid_argument(
                    "output_root must be new or empty; refusing to mix or "
                    "overwrite generated files: " +
                    nasc::PathToUtf8(request_.outputRoot)
                );
            }
        } else if (outputError) {
            throw std::runtime_error("cannot inspect output_root: " + outputError.message());
        }
        sourceDirectory_ = request_.sourceDirectory.empty() ? request_.scriptRoot / "ai" / "src" : request_.sourceDirectory;
        if (!std::filesystem::is_directory(sourceDirectory_)) {
            throw std::runtime_error("AI source directory does not exist: " + nasc::PathToUtf8(sourceDirectory_));
        }
    }

    void IndexSources() {
        std::error_code error;
        for (std::filesystem::directory_iterator iterator(sourceDirectory_, error), end; !error && iterator != end;
            iterator.increment(error)) {
            std::error_code statusError;
            if (!iterator->is_regular_file(statusError) || statusError || !AsciiAIExtension(iterator->path())) {
                continue;
            }
            const std::string filename = nasc::PathToUtf8(iterator->path().filename());
            const std::string key = AsciiLower(filename);
            const auto [position, inserted] = sources_.emplace(key, iterator->path());
            if (!inserted && position->second != iterator->path()) {
                throw std::runtime_error("case-insensitive AI filename collision: " + filename);
            }
        }
        if (error) {
            throw std::runtime_error("cannot enumerate AI source directory: " + error.message());
        }
    }

    [[nodiscard]] std::vector<std::string> RequestedModules() const {
        if (!request_.modules.empty()) {
            return request_.modules;
        }

        const CompileInventory inventory = nasc::LoadCompileList(request_.scriptRoot / "ai" / "data" / "compile_list.dat");
        std::vector<std::string> result;
        result.reserve(inventory.empty() ? sources_.size() : inventory.size());
        if (!inventory.empty()) {
            for (const auto& [name, unused] : inventory) {
                (void)unused;
                result.push_back(name);
            }
        } else {
            for (const auto& [unused, path] : sources_) {
                (void)unused;
                result.push_back(nasc::PathToUtf8(path.filename()));
            }
        }
        return result;
    }

    [[nodiscard]] bool CompileModule(const std::string& key) {
        if (const auto found = states_.find(key); found != states_.end()) {
            if (found->second == VisitState::Complete) {
                return true;
            }
            if (found->second == VisitState::Visiting) {
                report_.diagnostics.push_back(DescribeIncludeCycle(includeStack_, key));
            }
            return false;
        }

        const auto source = sources_.find(key);
        if (source == sources_.end()) {
            report_.diagnostics.push_back("cannot open include/source file: " + key);
            states_.insert_or_assign(key, VisitState::Failed);
            return false;
        }

        states_.insert_or_assign(key, VisitState::Visiting);
        includeStack_.push_back(key);
        const std::u16string sourceText = nasc::ReadUtf16leFile(source->second);
        const std::vector<std::u16string> scannedIncludes = ScanAIIncludes(sourceText);
        bool dependenciesSucceeded = true;
        for (const std::u16string& include : scannedIncludes) {
            const std::string includeUtf8 = nasc::Utf16ToUtf8(include);
            dependenciesSucceeded = CompileModule(ModuleKey(includeUtf8)) && dependenciesSucceeded;
        }
        includeStack_.pop_back();
        if (!dependenciesSucceeded) {
            states_.insert_or_assign(key, VisitState::Failed);
            return false;
        }

        AIModuleCompileReport module;
        module.module = nasc::PathToUtf8(source->second.filename());
        module.sourcePath = source->second;
        module.includes = scannedIncludes;

        AILexer lexer(sourceText);
        lexer.ImportPrecompiledHeader(precompiledHeaders_);
        // The original keeps the label counters in global data and never resets
        // them. Labels therefore keep counting across all modules of one run.
        CodeGenerator generator(nextCodeLabel_, nextStringLabel_);
        AISemanticActions semanticActions(generator);
        semanticActions.ImportPrecompiledHeader(precompiledHeaders_);
        semanticActions.ImportSymbolDatabase(symbolDatabase_);
        const AIParseResult parse = AISyntaxParser().Parse(lexer, semanticActions);
        nextCodeLabel_ = generator.NextCodeLabel();
        nextStringLabel_ = generator.NextStringLabel();

        module.accepted = parse.accepted;
        module.shiftedTokens = parse.shiftedTokens;
        module.reductions = parse.reductions;
        module.semanticActions = parse.semanticActions;
        module.eventTypeId = semanticActions.EventTypeId();
        module.objectUtf16Units = generator.Output().size();
        module.symbolUtf16Units = semanticActions.SymbolOutput().size();
        module.lexerDiagnostics.assign(lexer.Diagnostics().begin(), lexer.Diagnostics().end());
        module.parserDiagnostics = parse.diagnostics;
        module.semanticDiagnostics.assign(semanticActions.Diagnostics().begin(), semanticActions.Diagnostics().end());
        module.semanticDiagnosticLines.assign(
            semanticActions.DiagnosticLines().begin(), semanticActions.DiagnosticLines().end()
        );
        if (parse.errorToken) {
            module.parserErrorLine = parse.errorToken->line;
            module.parserErrorToken = parse.errorToken->code;
            module.parserErrorState = parse.errorState;
        }
        const std::span<const std::u16string> parsedIncludes = semanticActions.IncludeFiles();
        if (parsedIncludes.size() != scannedIncludes.size() ||
            !std::equal(parsedIncludes.begin(), parsedIncludes.end(), scannedIncludes.begin(), scannedIncludes.end())) {
            module.semanticDiagnostics.emplace_back(u"internal include pre-scan disagrees with the parser");
        }

        const std::filesystem::path stem = source->second.stem();
        module.objectPath = request_.outputRoot / "ai" / "obj" / stem;
        module.objectPath += ".obj";
        module.symbolPath = request_.outputRoot / "ai" / "sym" / stem;
        module.symbolPath += ".sym";

        if (!module.Succeeded()) {
            report_.modules.push_back(std::move(module));
            states_.insert_or_assign(key, VisitState::Failed);
            return false;
        }

        WriteNewUtf16File(module.objectPath, generator.Output());
        WriteNewUtf16File(module.symbolPath, semanticActions.SymbolOutput());
        const AISymbolLoadResult loaded =
            symbolDatabase_.LoadFile(module.symbolPath, module.eventTypeId, nasc::Utf8ToUtf16(module.module));
        if (!loaded.loaded || !loaded.wellFormed) {
            throw std::runtime_error(
                "generated symbol sidecar failed its own reader: " + nasc::PathToUtf8(module.symbolPath)
            );
        }

        report_.compileOrder.push_back(module.module);
        report_.modules.push_back(std::move(module));
        states_.insert_or_assign(key, VisitState::Complete);
        return true;
    }

    void MergeObjects(const std::filesystem::path& target) const {
        WriteNewFileAtomically(target, [this](const auto& temporary) {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) {
                throw std::runtime_error("cannot create " + nasc::PathToUtf8(temporary));
            }
            output.put(static_cast<char>(0xff));
            output.put(static_cast<char>(0xfe));
            WriteUtf16Units(output, nasc::LegacyObjectHeader());

            std::array<char, 1024 * 1024> buffer{};
            for (const AIModuleCompileReport& module : report_.modules) {
                std::ifstream input(module.objectPath, std::ios::binary);
                if (!input) {
                    throw std::runtime_error("cannot open module object " + nasc::PathToUtf8(module.objectPath));
                }
                unsigned char bom[2]{};
                input.read(reinterpret_cast<char*>(bom), 2);
                if (input.gcount() != 2 || bom[0] != 0xff || bom[1] != 0xfe) {
                    throw std::runtime_error("module object has no UTF-16LE BOM: " + nasc::PathToUtf8(module.objectPath));
                }
                while (input) {
                    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                    output.write(buffer.data(), input.gcount());
                }
                if (!input.eof()) {
                    throw std::runtime_error("failed to read module object: " + nasc::PathToUtf8(module.objectPath));
                }
            }
            if (!output) {
                throw std::runtime_error("failed to write merged object " + nasc::PathToUtf8(temporary));
            }
        });
    }

    const AIBuildRequest& request_;
    AIBuildReport report_;
    std::filesystem::path sourceDirectory_;
    std::map<std::string, std::filesystem::path, std::less<>> sources_;
    std::map<std::string, VisitState, std::less<>> states_;
    std::vector<std::string> includeStack_;
    PrecompiledHeaderDB precompiledHeaders_;
    AISymbolDatabase symbolDatabase_;
    std::int32_t nextCodeLabel_ = 0;
    std::int32_t nextStringLabel_ = 0;
};

} // namespace

std::vector<std::u16string> ScanAIIncludes(std::u16string_view source) {
    std::vector<std::u16string> result;
    std::size_t lineBegin = 0;
    while (lineBegin < source.size()) {
        const std::size_t newline = source.find(u'\n', lineBegin);
        const std::size_t lineEnd = newline == std::u16string_view::npos ? source.size() : newline;
        std::u16string_view line = source.substr(lineBegin, lineEnd - lineBegin);
        lineBegin = newline == std::u16string_view::npos ? source.size() : newline + 1U;

        while (!line.empty() && (line.front() == u' ' || line.front() == u'\t')) {
            line.remove_prefix(1U);
        }
        constexpr std::u16string_view directive = u"#include";
        if (!line.starts_with(directive)) {
            continue;
        }
        line.remove_prefix(directive.size());
        while (!line.empty() && (line.front() == u' ' || line.front() == u'\t')) {
            line.remove_prefix(1U);
        }
        if (line.empty() || line.front() != u'<') {
            continue;
        }
        const std::size_t close = line.find(u'>', 1U);
        if (close == std::u16string_view::npos || close == 1U) {
            continue;
        }
        std::u16string name(line.substr(1U, close - 1U));
        name.append(u".ai");
        result.push_back(std::move(name));
    }
    return result;
}

bool AIModuleCompileReport::Succeeded() const noexcept {
    return accepted && !parserErrorToken.has_value() && lexerDiagnostics.empty() && parserDiagnostics.empty() &&
        semanticDiagnostics.empty();
}

AIBuildReport CompileAITree(const AIBuildRequest& request) {
    try {
        return CompilerRun(request).Run();
    } catch (const std::exception& error) {
        AIBuildReport report;
        report.diagnostics.emplace_back(error.what());
        return report;
    }
}

} // namespace nasc
