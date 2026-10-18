#pragma once

#include "OperatorLog.h"
#include "Shared/src/Runtime/Utf8.h"

#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <string>
#include <utility>

namespace runtime {

// Deployment state published before resource loading and worker startup.
// Paths remain fixed for the process lifetime, including resource reloads.
struct ResourceDirectories {
    std::filesystem::path scripts;
    std::filesystem::path html;
    std::filesystem::path geo;
    bool scriptsFromCommandLine = false;
    bool htmlFromCommandLine = false;
    bool geoFromCommandLine = false;
};

inline ResourceDirectories& ResourceDirectoryStorage() {
    static ResourceDirectories directories;
    return directories;
}

inline bool ResolveDirectoryArgument(
    const std::string& value, const std::filesystem::path& base, const char* label, std::filesystem::path& result
) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(base / std::filesystem::path(Utf8CodeUnits(value)), error);
    if (value.empty() || error || !std::filesystem::is_directory(absolute, error)) {
        // std::fprintf(stderr, "%s is not an accessible directory: %s\n", label, value.c_str())
        const int diagnosticErrno = errno;
        try {
            const std::string reason = value.empty() ? "path is empty"
                : error                              ? error.message()
                                                     : "path does not name an existing directory";
            // logging::Write("", logging::Level::Error,
            //     std::string("[startup] Directory setting ") + label + " rejected: " + reason +
            //     ". Requested \"" + value + "\", candidate path \"" + absolute.u8string() + "\".")
            logging::Write(
                "", logging::Level::Error,
                std::string("[startup] Directory setting ") + label + " rejected: " + reason + ". Requested \"" + value +
                    "\"; candidate path \"" + Utf8Bytes(absolute.u8string()) + "\".",
                __FILE__
            );
        } catch (...) {
            // Reporting a rejected path must not replace the caller's normal false result.
            // logging::Write("", logging::Level::Error,
            //     "[startup] Directory setting rejected, diagnostic details could not be formatted.")
            logging::Write(
                "", logging::Level::Error,
                "[startup] Directory setting rejected; diagnostic details could not be formatted.", __FILE__
            );
        }
        errno = diagnosticErrno;
        return false;
    }
    result = absolute;
    return true;
}

template <class Configuration>
bool ConfigureResourceDirectories(const Configuration& config, const std::filesystem::path& configFile) {
    auto selected = ResourceDirectoryStorage();
    const auto base = configFile.parent_path();
    const std::string scripts = config.Get("Paths", "ScriptsDirectory", "");
    const std::string html = config.Get("Paths", "HtmlDirectory", "");
    const std::string geo = config.Get("Paths", "GeoDirectory", "");
    if (!selected.scriptsFromCommandLine && !scripts.empty() &&
        !ResolveDirectoryArgument(scripts, base, "[Paths]/ScriptsDirectory", selected.scripts)) {
        return false;
    }
    if (!selected.htmlFromCommandLine && !html.empty() &&
        !ResolveDirectoryArgument(html, base, "[Paths]/HtmlDirectory", selected.html)) {
        return false;
    }
    if (!selected.geoFromCommandLine && !geo.empty() &&
        !ResolveDirectoryArgument(geo, base, "[Paths]/GeoDirectory", selected.geo)) {
        return false;
    }
    ResourceDirectoryStorage() = std::move(selected);
    return true;
}

// Only the original ../Script, ../GeoData and ../html[_LANG] roots are redirected.
// Other paths and explicit absolute filenames keep their existing meaning.
// Recognition accepts both target separators and Script/script casing, while
// filename casing and .l2enc suffixes remain intact. An explicit HTML root
// contains the default catalog directly and localized html_LANG subdirectories.
inline std::filesystem::path ResolveResourcePath(const std::filesystem::path& original) {
    const auto& directories = ResourceDirectoryStorage();
    if (directories.scripts.empty() && directories.html.empty() && directories.geo.empty()) {
        return original;
    }
    std::string path = Utf8Bytes(original.u8string());
    std::replace(path.begin(), path.end(), '\\', '/');
    while (path.starts_with("./")) {
        path.erase(0, 2);
    }
    if (!path.starts_with("../")) {
        return original;
    }
    const auto end = path.find('/', 3);
    const std::string folder = path.substr(3, end == std::string::npos ? end : end - 3);
    std::string lower = folder;
    for (char& character : lower) {
        if (character >= 'A' && character <= 'Z') {
            character += 'a' - 'A';
        }
    }
    std::filesystem::path root;
    if (lower == "script" && !directories.scripts.empty()) {
        root = directories.scripts;
    } else if (lower == "geodata" && !directories.geo.empty()) {
        root = directories.geo;
    } else if ((lower == "html" || lower.starts_with("html_")) && !directories.html.empty()) {
        root = directories.html;
        // HtmlCache selects ../html for the default catalog and ../html_LANG
        // for MultiLang. Localized catalogs live INSIDE the selected root.
        if (folder.size() > 4) {
            root /= std::filesystem::path(Utf8CodeUnits(folder));
        }
    } else {
        return original;
    }
    return end == std::string::npos ? root : root / std::filesystem::path(Utf8CodeUnits(path.substr(end + 1)));
}

} // namespace runtime
