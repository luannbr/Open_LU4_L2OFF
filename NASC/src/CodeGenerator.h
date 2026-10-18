#pragma once

#include "ScriptTypes.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nasc {

// The original writes a missing string as "(null)". Class 0 (default_npc)
// has no base class and needs this.
using OptionalText = std::optional<std::u16string_view>;

enum class ClassKind {
    Normal,
    Development,
    Test,
    Broadcast,
};

// Writes the assembly text of ai.obj. Text templates, label counters and
// buffering follow NASC64_2015.exe. The output is UTF-16, and Platform.cpp
// writes the BOM and the file bytes.
class CodeGenerator {
public:
    CodeGenerator() = default;
    CodeGenerator(std::int32_t initialCodeLabel, std::int32_t initialStringLabel) noexcept;

    [[nodiscard]] std::u16string_view Output() const noexcept;
    [[nodiscard]] const std::vector<std::u16string>& PendingCode() const noexcept;
    [[nodiscard]] std::int32_t DummyStackPoint() const noexcept;
    [[nodiscard]] std::int32_t NextCodeLabel() const noexcept;
    [[nodiscard]] std::int32_t NextStringLabel() const noexcept;

    void WriteVersions(
        std::int32_t pointerSize,
        std::int32_t sharedFactoryVersion,
        std::int32_t npchVersion,
        std::int32_t nascVersion,
        std::int32_t npcEventHVersion,
        std::int32_t debug
    );

    void NewClass(ClassKind kind, std::int32_t id, std::u16string_view name, OptionalText baseName);
    void ClassEnd();
    void ParameterDefineBegin();
    void ParameterDefineEnd();
    void PropertyDefineBegin();
    void PropertyDefineEnd();
    void IntType(std::u16string_view name, std::int64_t value);
    void FloatType(std::u16string_view name, float value);
    void StringType(std::u16string_view name, std::u16string_view value);
    void WaypointsType(std::u16string_view name);
    void WaypointDelaysType(std::u16string_view name);
    void TelposListBegin(std::u16string_view name);
    void TelposListEnd();
    void Telpos(
        std::u16string_view name, std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t heading, std::int32_t world
    );
    void Telpos2(
        std::int32_t first,
        std::int32_t second,
        std::int32_t third,
        std::int32_t fourth,
        std::int32_t fifth,
        std::int32_t sixth
    );
    void TelposEx(
        std::u16string_view name,
        std::int32_t x,
        std::int32_t y,
        std::int32_t z,
        std::int32_t heading,
        std::span<const std::int32_t> extra
    );
    void TelposEx2(
        std::int32_t first,
        std::int32_t second,
        std::int32_t third,
        std::int32_t fourth,
        std::int32_t fifth,
        std::span<const std::int32_t> extra
    );
    void BuysellListBegin(std::u16string_view name);
    void BuysellListEnd();
    void Buysell(std::int32_t itemId, std::int64_t count, float chance, std::int64_t price);
    void Buy(std::int32_t itemId, std::int32_t count);

    // Handler start, name, local variables and end.
    void InitHandler();
    void SetHandler(std::int32_t id, std::u16string_view name);
    void SetLocalVariables(std::span<const std::u16string> variables);
    void FlushHandler();

    // Instruction writers. The names follow the CODEGEN_* functions of the
    // original.
    void Add();
    void AddString(const ScriptTypeDescriptor* left, const ScriptTypeDescriptor* right);
    void Subtract();
    void Multiply();
    void Divide();
    void Modulo();
    void LogicalNot();
    void Negate();
    void LogicalAnd();
    void LogicalOr();
    void BitwiseAnd();
    void BitwiseOr();
    void BitwiseXor();
    void Greater();
    void GreaterEqual();
    void Less();
    void LessEqual();
    void Equal();
    void NotEqual();
    void Assign();
    void Assign4();
    void Fetch(const ScriptTypeDescriptor* type, OptionalText comment = std::nullopt);
    void FlushFetch(const ScriptTypeDescriptor* type, std::int32_t& fetchCount);
    void PushRegisterSp();
    void PushConst(std::int64_t value, OptionalText comment = std::nullopt);
    void PushConstFloat(float value, OptionalText comment = std::nullopt);
    void PushStringLabel(std::int32_t label);
    void WriteLabel(std::int32_t label);
    void JumpFunction(std::u16string_view name);
    void JumpLabel(std::int32_t label);
    void BranchFalse(std::int32_t label);
    void BranchTrue(std::int32_t label);
    void ShiftSp(std::int32_t amount);
    [[nodiscard]] std::int32_t DeclareString(std::u16string_view value);
    void ExitHandler();
    void CallSuper();
    void PushEvent(std::u16string_view name);
    void FunctionCall(std::uint64_t encodedIndex, std::u16string_view name);
    void PushProperty(std::u16string_view name);
    void PushParameter(std::u16string_view name);

    // Comments are not written, like in the original.
    void Comment(std::u16string_view) noexcept;

    [[nodiscard]] std::int32_t NewCodeLabel() noexcept;
    [[nodiscard]] std::int32_t NewStringLabel() noexcept;
    void PushLabel(std::int32_t label);
    [[nodiscard]] std::int32_t PopLabel();
    void PushIfLabel(std::int32_t label);
    [[nodiscard]] std::int32_t PopIfLabel();
    void PushSelectLabel(std::int32_t label);
    [[nodiscard]] std::int32_t PopSelectLabel();
    void PushAndLabel(std::int32_t label);
    [[nodiscard]] std::int32_t PopAndLabel();
    void PushOrLabel(std::int32_t label);
    [[nodiscard]] std::int32_t PopOrLabel();

private:
    void AppendDirect(std::u16string text);
    void AppendCode(std::u16string text);
    void AppendBinaryInstruction(std::u16string_view instruction);
    void VariableList(std::span<const std::u16string> variables);

    [[nodiscard]] static std::int32_t StringOperandKind(const ScriptTypeDescriptor* type) noexcept;
    [[nodiscard]] static std::u16string IntList(std::span<const std::int32_t> values);
    [[nodiscard]] static std::int32_t PopStack(std::vector<std::int32_t>& stack, std::string_view name);

    std::u16string output_;
    std::int32_t handlerId_ = 0;
    std::u16string handlerName_;
    std::vector<std::u16string> localVariables_;
    std::vector<std::u16string> code_;

    // Start values of the original counters.
    std::int32_t dummyStackPoint_ = -1;
    std::int32_t stringLabel_ = 0;
    std::int32_t codeLabel_ = 0;
    std::vector<std::int32_t> labels_;
    std::vector<std::int32_t> ifLabels_;
    std::vector<std::int32_t> selectLabels_;
    std::vector<std::int32_t> andLabels_;
    std::vector<std::int32_t> orLabels_;
};

} // namespace nasc
