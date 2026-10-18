#pragma once

#include "CodeGenerator.h"

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nasc {

class PrecompiledHeaderDB;
class AISymbolDatabase;

enum class AISemanticKind : std::uint8_t {
    Empty,
    Integer,
    Floating,
    Text,
    ScriptType,
    TypeList,
    IntegerList,
};

// Value of one parser stack slot. The original used an eight-byte union.
// Explicit alternatives keep ownership clear and do not depend on wchar_t.
struct AISemanticValue {
    AISemanticKind kind = AISemanticKind::Empty;
    std::int64_t integer = 0;
    float floating = 0.0F;
    std::u16string text;
    const ScriptTypeDescriptor* scriptType = nullptr;
    std::vector<const ScriptTypeDescriptor*> typeList;
    std::vector<std::int32_t> integerList;

    [[nodiscard]] static AISemanticValue FromInteger(std::int64_t value);
    [[nodiscard]] static AISemanticValue FromFloat(float value);
    [[nodiscard]] static AISemanticValue FromText(std::u16string_view value);
    [[nodiscard]] static AISemanticValue FromType(const ScriptTypeDescriptor* value);
    [[nodiscard]] static AISemanticValue FromTypeList(std::vector<const ScriptTypeDescriptor*> value);
    [[nodiscard]] static AISemanticValue FromIntegerList(std::vector<std::int32_t> value);
};

struct AIVariableAlias {
    std::u16string object;
    std::u16string variable;
};

// Grammar actions for expressions. The action numbers come from the
// YyReduction table.
class AISemanticActions {
public:
    explicit AISemanticActions(CodeGenerator& codeGenerator, std::int32_t eventTypeId = 0);

    [[nodiscard]] bool Implements(std::int16_t action) const noexcept;
    [[nodiscard]] AISemanticValue Execute(
        std::int16_t action, std::span<const AISemanticValue> semanticStack, std::int16_t reductionLength
    );

    [[nodiscard]] std::int32_t FetchCount() const noexcept;
    void SetFetchCount(std::int32_t count);
    [[nodiscard]] std::int32_t EventTypeId() const noexcept;
    void SetEventTypeId(std::int32_t eventTypeId);
    void SetClassName(std::u16string name);
    void SetClassKind(ClassKind kind) noexcept;
    void SetHandlerWords(std::span<const std::u16string> words);
    void RegisterKnownClass(std::int32_t eventTypeId, std::u16string name);
    void DefinePrecompiledSymbol(std::u16string name, std::int64_t value);
    void ImportPrecompiledHeader(const PrecompiledHeaderDB& database);
    void ImportSymbolDatabase(const AISymbolDatabase& database);
    void SetClassDuplicateCheck(bool enabled) noexcept;
    void SetLocalVariables(std::span<const std::u16string> variables);
    void DefineProperty(std::u16string name, const ScriptTypeDescriptor* type);
    void DefineParameter(std::u16string name, const ScriptTypeDescriptor* type);
    void DefineArgumentAlias(std::u16string alias, std::u16string variable);
    void DefineVariableAlias(std::u16string alias, std::u16string object, std::u16string variable);
    void ForbidAliasVariable(std::u16string name);
    [[nodiscard]] std::span<const std::u16string> Diagnostics() const noexcept;
    // Source line of each diagnostic, at the parser position of its reduction.
    [[nodiscard]] std::span<const std::size_t> DiagnosticLines() const noexcept;
    void SetSourceLine(std::size_t line) noexcept;
    [[nodiscard]] std::span<const std::u16string> IncludeFiles() const noexcept;
    [[nodiscard]] std::u16string_view SymbolOutput() const noexcept;
    [[nodiscard]] std::int32_t LoopCount() const noexcept;

private:
    [[nodiscard]] static const AISemanticValue& FromTop(std::span<const AISemanticValue> stack, std::size_t offset);
    [[nodiscard]] static const ScriptTypeDescriptor* ValueType(const AISemanticValue& value);
    [[nodiscard]] static std::u16string_view ValueText(const AISemanticValue& value);
    [[nodiscard]] static std::int64_t ValueInteger(const AISemanticValue& value);
    [[nodiscard]] static float ValueFloat(const AISemanticValue& value);
    [[nodiscard]] AISemanticValue DefaultValue(std::span<const AISemanticValue> stack, std::int16_t reductionLength) const;
    void Error(std::u16string_view message);
    void Flush(const ScriptTypeDescriptor* type);

    CodeGenerator& codeGenerator_;
    std::int32_t fetchCount_ = 0;
    std::int32_t eventTypeId_ = 0;
    std::int32_t loopCount_ = 0;
    std::int32_t forLabelA_ = 0;
    std::int32_t forLabelB_ = 0;
    std::int32_t forLabelC_ = 0;
    std::int32_t forLabelD_ = 0;
    ClassKind classKind_ = ClassKind::Normal;
    bool classDuplicateCheck_ = true;
    std::u16string className_;
    std::optional<std::u16string> superClassName_;
    std::vector<std::u16string> includeFiles_;
    std::u16string symbolOutput_;
    std::vector<std::u16string> handlerWords_;
    std::vector<std::u16string> localVariables_;
    std::set<std::int32_t> eventHandlers_;
    std::set<std::pair<std::int32_t, std::u16string>> knownClasses_;
    using TypeMap = std::map<std::u16string, const ScriptTypeDescriptor*, std::less<>>;
    std::map<std::u16string, TypeMap, std::less<>> classParameters_;
    std::map<std::u16string, TypeMap, std::less<>> classProperties_;
    TypeMap constants_;
    std::map<std::u16string, std::int64_t, std::less<>> precompiledSymbols_;
    std::map<std::u16string, const ScriptTypeDescriptor*, std::less<>> properties_;
    std::map<std::u16string, const ScriptTypeDescriptor*, std::less<>> parameters_;
    std::map<std::u16string, std::u16string, std::less<>> argumentAliases_;
    std::map<std::u16string, AIVariableAlias, std::less<>> variableAliases_;
    std::set<std::u16string, std::less<>> forbiddenAliasVariables_;
    std::vector<std::u16string> diagnostics_;
    std::vector<std::size_t> diagnosticLines_;
    std::size_t sourceLine_ = 0;
};

} // namespace nasc
