#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

namespace nasc {

enum class ScriptTree : std::int32_t {
    Regular = 0,
    Development = 1,
    Test = 2,
    Broadcast = 3,
};

struct ScriptFileInfo {
    std::string includeFiles;
    std::int64_t lastModified = 0;
    bool compileCompleted = true;
    ScriptTree scriptTree = ScriptTree::Regular;
};

using CompileInventory = std::map<std::string, ScriptFileInfo, std::less<>>;

// Accepts ASCII files and the UTF-16LE files that MSVC writes in ccs=UNICODE
// mode.
[[nodiscard]] CompileInventory LoadCompileList(const std::filesystem::path& path);

// Lists the .ai files in ai/, ai/dev/, ai/test/ and ai/broadcast/. The
// extension is matched without case, as on Windows.
[[nodiscard]] CompileInventory ScanScriptTree(
    const std::filesystem::path& scriptRoot, const CompileInventory& previous = {}
);

[[nodiscard]] bool NeedsRecompile(
    const std::string& name, const ScriptFileInfo& current, const CompileInventory& previous, bool rebuild
) noexcept;

} // namespace nasc
