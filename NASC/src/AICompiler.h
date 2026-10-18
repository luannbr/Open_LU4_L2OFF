#pragma once

#include "AIParser.h"
#include "PrecompiledHeaderDB.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nasc {

// Returns the modules named by the leading #include <...> lines, with the
// ".ai" suffix. Commented lines are ignored. The compiler builds these
// dependencies before it parses the module.
[[nodiscard]] std::vector<std::u16string> ScanAIIncludes(std::u16string_view source);

struct AIModuleCompileReport {
    std::string module;
    std::filesystem::path sourcePath;
    std::filesystem::path objectPath;
    std::filesystem::path symbolPath;
    std::vector<std::u16string> includes;
    std::vector<AILexerDiagnostic> lexerDiagnostics;
    std::vector<ParserDiagnostic> parserDiagnostics;
    std::vector<std::u16string> semanticDiagnostics;
    std::vector<std::size_t> semanticDiagnosticLines;
    bool accepted = false;
    std::optional<std::size_t> parserErrorLine;
    std::optional<int> parserErrorToken;
    std::int16_t parserErrorState = 0;
    std::int32_t eventTypeId = 0;
    std::size_t shiftedTokens = 0;
    std::size_t reductions = 0;
    std::size_t semanticActions = 0;
    std::size_t objectUtf16Units = 0;
    std::size_t symbolUtf16Units = 0;

    [[nodiscard]] bool Succeeded() const noexcept;
};

struct AIBuildRequest {
    // Script directory with the PCH files and the ai/ tree.
    std::filesystem::path scriptRoot;

    // Empty means scriptRoot/ai/src. Inputs are always read-only.
    std::filesystem::path sourceDirectory;

    // Required. It must differ from scriptRoot unless allowInPlace is set.
    // All generated files stay below this directory.
    std::filesystem::path outputRoot;

    // Empty means the keys of ai/data/compile_list.dat. If that file is
    // absent, every .ai directly in sourceDirectory is selected.
    std::vector<std::string> modules;

    bool createMergedObject = true;
    bool allowInPlace = false;
};

struct AIBuildReport {
    bool succeeded = false;
    PrecompiledLoadReport precompiledHeaders;
    std::vector<AIModuleCompileReport> modules;
    std::vector<std::string> compileOrder;
    std::vector<std::string> diagnostics;
    std::filesystem::path mergedObjectPath;
};

// Compiles the requested modules like NASC64_2015.exe. Dependencies are
// built first, so their .sym files exist when a class needs them. Existing
// output files are never overwritten.
[[nodiscard]] AIBuildReport CompileAITree(const AIBuildRequest& request);

} // namespace nasc
