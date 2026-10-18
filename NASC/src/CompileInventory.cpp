#include "CompileInventory.h"

#include "Encoding.h"
#include "Platform.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace nasc {
namespace {

std::string ReadCompileListText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe) {
        if (bytes.size() % 2 != 0) {
            throw std::runtime_error("odd-sized UTF-16 compile list: " + PathToUtf8(path));
        }
        std::u16string text;
        text.reserve((bytes.size() - 2) / 2);
        for (std::size_t offset = 2; offset < bytes.size(); offset += 2) {
            const auto unit =
                static_cast<std::uint16_t>(bytes[offset] | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8));
            text.push_back(static_cast<char16_t>(unit));
        }
        return Utf16ToUtf8(text);
    }
    return std::string(bytes.begin(), bytes.end());
}

bool AIExtension(const std::filesystem::path& path) {
    std::string extension = PathToUtf8(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return extension == ".ai";
}

std::int64_t UnixModifiedTime(const std::filesystem::path& path) {
    const auto fileTime = std::filesystem::last_write_time(path);
    const auto systemTime = [&] {
        using FileClock = decltype(fileTime)::clock;
        if constexpr (requires { FileClock::to_sys(fileTime); }) {
            return FileClock::to_sys(fileTime);
        } else {
            // Fallback for standard libraries without file_clock::to_sys.
            return std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                fileTime - FileClock::now() + std::chrono::system_clock::now()
            );
        }
    }();
    return std::chrono::duration_cast<std::chrono::seconds>(systemTime.time_since_epoch()).count();
}

void ScanOneDirectory(
    CompileInventory& result, const CompileInventory& previous, const std::filesystem::path& directory, ScriptTree tree
) {
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error) || error) {
        return;
    }

    std::vector<std::filesystem::path> files;
    for (std::filesystem::directory_iterator iterator(directory, error), end; !error && iterator != end;
        iterator.increment(error)) {
        std::error_code statusError;
        if (iterator->is_regular_file(statusError) && !statusError && AIExtension(iterator->path())) {
            files.push_back(iterator->path());
        }
    }
    if (error) {
        throw std::runtime_error("cannot enumerate " + PathToUtf8(directory) + ": " + error.message());
    }

    std::sort(files.begin(), files.end(), [](const auto& left, const auto& right) {
        return PathToUtf8(left.filename()) < PathToUtf8(right.filename());
    });
    for (const std::filesystem::path& file : files) {
        const std::string name = PathToUtf8(file.filename());
        ScriptFileInfo info;
        info.lastModified = UnixModifiedTime(file);
        info.compileCompleted = true;
        info.scriptTree = tree;
        if (const auto old = previous.find(name); old != previous.end()) {
            info.includeFiles = old->second.includeFiles;
        }
        // A file in a later directory (dev, test, broadcast) replaces a file with
        // the same name, like in the original.
        result.insert_or_assign(name, std::move(info));
    }
}

} // namespace

CompileInventory LoadCompileList(const std::filesystem::path& path) {
    CompileInventory result;
    std::istringstream input(ReadCompileListText(path));
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }

        std::istringstream fields(line);
        std::string name;
        std::int64_t timestamp = 0;
        std::string includes;
        if (!(fields >> name >> timestamp >> includes)) {
            throw std::runtime_error("invalid compile_list.dat line: " + line);
        }
        ScriptFileInfo info;
        info.lastModified = timestamp;
        if (includes != "0") {
            info.includeFiles = std::move(includes);
        }
        result.insert_or_assign(std::move(name), std::move(info));
    }
    return result;
}

CompileInventory ScanScriptTree(const std::filesystem::path& scriptRoot, const CompileInventory& previous) {
    CompileInventory result;
    const std::filesystem::path ai = scriptRoot / "ai";
    ScanOneDirectory(result, previous, ai, ScriptTree::Regular);
    ScanOneDirectory(result, previous, ai / "dev", ScriptTree::Development);
    ScanOneDirectory(result, previous, ai / "test", ScriptTree::Test);
    ScanOneDirectory(result, previous, ai / "broadcast", ScriptTree::Broadcast);
    return result;
}

bool NeedsRecompile(
    const std::string& name, const ScriptFileInfo& current, const CompileInventory& previous, bool rebuild
) noexcept {
    if (rebuild) {
        return true;
    }
    const auto old = previous.find(name);
    return old == previous.end() || old->second.lastModified != current.lastModified;
}

} // namespace nasc
