#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace nasc {

struct HelpGenerationReport {
    bool succeeded = false;
    std::filesystem::path indexPath;
    std::vector<std::filesystem::path> skeletonPaths;
    std::vector<std::string> diagnostics;
};

// Writes Help/index.txt and one XML skeleton per script type, like the
// original -genhelp option. The files go below outputRoot, which must be
// new or empty. Existing files are never overwritten.
[[nodiscard]] HelpGenerationReport GenerateScriptHelp(const std::filesystem::path& outputRoot);

} // namespace nasc
