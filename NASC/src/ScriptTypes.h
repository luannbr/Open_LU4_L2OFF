#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace nasc {

struct ScriptTypeDescriptor {
    std::int32_t id;
    std::u16string_view name;
    std::u16string_view nativeType;
    bool isClass;
};

struct ScriptEventTypeDescriptor {
    std::int32_t id;
    std::u16string_view name;
    std::u16string_view nativeType;
};

struct BinaryOperationTypeResult {
    // The result type of a binary operation and whether the original reports
    // an error. The type is set also after an error, because the original
    // continues with it.
    const ScriptTypeDescriptor* type;
    bool compatible;
};

// Script types in the original order. Index 0 is empty, so the valid IDs
// are 1 to 34.
extern const std::array<std::optional<ScriptTypeDescriptor>, 35> scriptTypeTable;
extern const std::array<ScriptEventTypeDescriptor, 2> scriptEventTypeTable;

[[nodiscard]] const ScriptTypeDescriptor* FindScriptType(std::int32_t id) noexcept;
[[nodiscard]] const ScriptTypeDescriptor* FindScriptType(std::u16string_view name) noexcept;
[[nodiscard]] const ScriptEventTypeDescriptor* FindScriptEventType(std::int32_t id) noexcept;

// Type rules of the original compiler. The arguments must be entries of
// the type table, not null.
[[nodiscard]] bool AssignCompatible(const ScriptTypeDescriptor* destination, const ScriptTypeDescriptor* source) noexcept;
[[nodiscard]] bool IsIntegralType(const ScriptTypeDescriptor* type) noexcept;
[[nodiscard]] bool OperatorCompatible(const ScriptTypeDescriptor* left, const ScriptTypeDescriptor* right) noexcept;
[[nodiscard]] BinaryOperationTypeResult BinaryOperationType(
    const ScriptTypeDescriptor* left, const ScriptTypeDescriptor* right
) noexcept;

} // namespace nasc
