#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace nasc {

struct ScriptFunctionDescriptor {
    std::int32_t ownerTypeId;
    std::int32_t index;
    std::u16string_view name;
    std::int32_t returnTypeId;
    std::uint32_t argumentBegin;
    std::uint16_t argumentCount;
};

struct ScriptVariableDescriptor {
    // A ScriptTypeTable type ID for ScriptVariables(), or an event type ID
    // for ScriptEventVariables().
    std::int32_t ownerTypeId;
    std::u16string_view name;
    std::int32_t offset;
    std::int32_t typeId;
};

struct ScriptEventNameDescriptor {
    std::int32_t eventTypeId;
    std::int32_t eventId;
    std::u16string_view name;
};

[[nodiscard]] std::span<const ScriptFunctionDescriptor> ScriptFunctions() noexcept;
[[nodiscard]] std::span<const ScriptVariableDescriptor> ScriptVariables() noexcept;
[[nodiscard]] std::span<const ScriptVariableDescriptor> ScriptEventVariables() noexcept;
[[nodiscard]] std::span<const ScriptEventNameDescriptor> ScriptEventNames() noexcept;

[[nodiscard]] std::span<const std::int32_t> ArgumentTypes(const ScriptFunctionDescriptor& function) noexcept;

[[nodiscard]] const ScriptFunctionDescriptor* FindScriptFunction(
    std::int32_t ownerTypeId, std::u16string_view name
) noexcept;
[[nodiscard]] const ScriptVariableDescriptor* FindScriptVariable(
    std::int32_t ownerTypeId, std::u16string_view name
) noexcept;
[[nodiscard]] const ScriptVariableDescriptor* FindScriptEventVariable(
    std::int32_t eventTypeId, std::u16string_view name
) noexcept;
[[nodiscard]] const ScriptEventNameDescriptor* FindScriptEventName(
    std::int32_t eventTypeId, std::u16string_view name
) noexcept;

} // namespace nasc
