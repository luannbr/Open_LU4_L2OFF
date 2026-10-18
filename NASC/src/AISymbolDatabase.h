#pragma once

#include "ScriptTypes.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>

namespace nasc {

using AISymbolTypeMap = std::map<std::u16string, const ScriptTypeDescriptor*, std::less<>>;

struct AISymbolLoadResult {
    bool loaded = false;
    bool wellFormed = false;
    std::size_t classes = 0;
    std::size_t parameterLines = 0;
    std::size_t propertyLines = 0;
};

// Reads the UTF-16LE ai/sym/*.sym files. A class is identified together with
// its base event type, but parameters and properties are stored by class
// name only, like in the original.
class AISymbolDatabase {
public:
    [[nodiscard]] AISymbolLoadResult LoadFile(
        const std::filesystem::path& path, std::int32_t eventTypeId, std::u16string logicalName = {}
    );

    void Clear() noexcept;

    [[nodiscard]] bool ContainsClass(std::int32_t eventTypeId, std::u16string_view name) const;
    [[nodiscard]] const AISymbolTypeMap* Parameters(std::u16string_view className) const;
    [[nodiscard]] const AISymbolTypeMap* Properties(std::u16string_view className) const;

    [[nodiscard]] const std::set<std::pair<std::int32_t, std::u16string>>& Classes() const noexcept;
    [[nodiscard]] const std::map<std::u16string, AISymbolTypeMap, std::less<>>& ClassParameters() const noexcept;
    [[nodiscard]] const std::map<std::u16string, AISymbolTypeMap, std::less<>>& ClassProperties() const noexcept;

private:
    std::set<std::pair<std::int32_t, std::u16string>> classes_;
    std::map<std::u16string, AISymbolTypeMap, std::less<>> classParameters_;
    std::map<std::u16string, AISymbolTypeMap, std::less<>> classProperties_;
    std::set<std::u16string, std::less<>> loadedFiles_;
};

} // namespace nasc
