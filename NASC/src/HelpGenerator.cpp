#include "HelpGenerator.h"

#include "Encoding.h"
#include "Platform.h"
#include "ScriptMetadata.h"
#include "ScriptTypes.h"

#include <algorithm>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string_view>

namespace nasc {
namespace {

void Append(std::u16string& output, std::u16string_view value) { output.append(value); }

void AppendLine(std::u16string& output, std::u16string_view value) {
    output.append(value);
    output.append(u"\r\n");
}

[[nodiscard]] const ScriptTypeDescriptor& RequireType(std::int32_t id) {
    const ScriptTypeDescriptor* const type = FindScriptType(id);
    if (type == nullptr) {
        throw std::runtime_error("help metadata refers to unknown script type " + std::to_string(id));
    }
    return *type;
}

[[nodiscard]] std::vector<const ScriptVariableDescriptor*> VariablesFor(std::int32_t owner) {
    std::vector<const ScriptVariableDescriptor*> result;
    for (const ScriptVariableDescriptor& variable : ScriptVariables()) {
        if (variable.ownerTypeId == owner) {
            result.push_back(&variable);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto* left, const auto* right) { return left->name < right->name; });
    return result;
}

[[nodiscard]] std::vector<const ScriptFunctionDescriptor*> FunctionsFor(std::int32_t owner) {
    std::vector<const ScriptFunctionDescriptor*> result;
    for (const ScriptFunctionDescriptor& function : ScriptFunctions()) {
        if (function.ownerTypeId == owner) {
            result.push_back(&function);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto* left, const auto* right) { return left->name < right->name; });
    return result;
}

void WriteNewUtf8File(const std::filesystem::path& path, std::u16string_view contents) {
    if (std::filesystem::exists(path)) {
        throw std::runtime_error("refusing to overwrite existing help output: " + nasc::PathToUtf8(path));
    }
    std::filesystem::create_directories(path.parent_path());
    std::filesystem::path temporary = path;
    temporary += ".tmp-" + std::to_string(nasc::MonotonicMilliseconds());
    try {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("cannot create help output: " + nasc::PathToUtf8(path));
        }
        const std::string utf8 = nasc::Utf16ToUtf8(contents);
        output.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
        output.close();
        if (!output) {
            throw std::runtime_error("failed to write help output: " + nasc::PathToUtf8(path));
        }
        std::filesystem::rename(temporary, path);
    } catch (...) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        throw;
    }
}

[[nodiscard]] std::u16string MakeIndexForType(const ScriptTypeDescriptor& type) {
    std::u16string output;
    AppendLine(output, type.name);
    for (const ScriptVariableDescriptor* variable : VariablesFor(type.id)) {
        Append(output, type.name);
        Append(output, u"::");
        AppendLine(output, variable->name);
    }
    for (const ScriptFunctionDescriptor* function : FunctionsFor(type.id)) {
        Append(output, type.name);
        Append(output, u"::");
        Append(output, function->name);
        Append(output, u"(");
        const std::span<const std::int32_t> arguments = ArgumentTypes(*function);
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            if (index != 0) {
                Append(output, u",");
            }
            Append(output, RequireType(arguments[index]).name);
        }
        Append(output, u"):");
        Append(output, RequireType(function->returnTypeId).name);
        Append(output, u"\r\n");
    }
    return output;
}

[[nodiscard]] std::u16string MakeSkeleton(const ScriptTypeDescriptor& type) {
    const std::vector<const ScriptVariableDescriptor*> variables = VariablesFor(type.id);
    const std::vector<const ScriptFunctionDescriptor*> functions = FunctionsFor(type.id);

    std::u16string output;
    AppendLine(output, u"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>");
    Append(output, u"<");
    Append(output, type.name);
    AppendLine(output, u">");

    if (!variables.empty()) {
        AppendLine(output, u"\t<Variables>");
        for (const ScriptVariableDescriptor* variable : variables) {
            Append(output, u"\t\t<Var Name=\"");
            Append(output, variable->name);
            Append(output, u"\" Type=\"");
            Append(output, RequireType(variable->typeId).name);
            AppendLine(output, u"\" ></Var>");
        }
        AppendLine(output, u"\t</Variables>");
    }

    if (!functions.empty()) {
        AppendLine(output, u"\t<Functions>");
        for (const ScriptFunctionDescriptor* function : functions) {
            Append(output, u"\t\t<Func Name=\"");
            Append(output, function->name);
            Append(output, u"\" Type=\"");
            Append(output, RequireType(function->returnTypeId).name);
            AppendLine(output, u"\" >");
            for (const std::int32_t argument : ArgumentTypes(*function)) {
                Append(output, u"\t\t\t<Arg Type=\"");
                Append(output, RequireType(argument).name);
                AppendLine(output, u"\" > </Arg>");
            }
            AppendLine(output, u"\t\t</Func>");
        }
        AppendLine(output, u"\t</Functions>");
    }

    Append(output, u"</");
    Append(output, type.name);
    AppendLine(output, u">");
    return output;
}

void ValidateOutputRoot(const std::filesystem::path& outputRoot) {
    if (outputRoot.empty()) {
        throw std::invalid_argument("output_root is required for -genhelp");
    }
    std::error_code error;
    if (!std::filesystem::exists(outputRoot, error)) {
        if (error) {
            throw std::runtime_error("cannot inspect output_root: " + error.message());
        }
        return;
    }
    if (!std::filesystem::is_directory(outputRoot, error) || error) {
        throw std::invalid_argument("help output_root exists but is not a directory: " + nasc::PathToUtf8(outputRoot));
    }
    if (!std::filesystem::is_empty(outputRoot, error) || error) {
        throw std::invalid_argument("help output_root must be new or empty: " + nasc::PathToUtf8(outputRoot));
    }
}

} // namespace

HelpGenerationReport GenerateScriptHelp(const std::filesystem::path& outputRoot) {
    HelpGenerationReport report;
    try {
        ValidateOutputRoot(outputRoot);
        const std::filesystem::path help = outputRoot / "Help";
        std::u16string index;
        for (const auto& maybeType : scriptTypeTable) {
            if (!maybeType || !maybeType->isClass) {
                continue;
            }
            const ScriptTypeDescriptor& type = *maybeType;
            index.append(MakeIndexForType(type));
            std::filesystem::path skeleton = help / nasc::PathFromUtf8(nasc::Utf16ToUtf8(type.name));
            skeleton += ".xml";
            WriteNewUtf8File(skeleton, MakeSkeleton(type));
            report.skeletonPaths.push_back(std::move(skeleton));
        }
        report.indexPath = help / "index.txt";
        WriteNewUtf8File(report.indexPath, index);
        report.succeeded = true;
    } catch (const std::exception& error) {
        report.diagnostics.emplace_back(error.what());
    }
    return report;
}

} // namespace nasc
