// config.cpp  -  l2cache.ini parser implementation.
#include "Config.h"
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <vector>
#include "Shared/src/Log.h"

namespace runtime {
namespace {

enum class ConfigEncoding {
    Utf8,
    Utf8Bom,
    Utf16LittleEndian,
    Utf16BigEndian,
};

static std::string lower(std::string s) {
    for (char& c : s) {
        c = (char)std::tolower((unsigned char)c);
    }
    return s;
}
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) {
        return "";
    }
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static ConfigEncoding detectEncoding(const std::vector<unsigned char>& bytes) {
    if (bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe) {
        return ConfigEncoding::Utf16LittleEndian;
    }
    if (bytes.size() >= 2 && bytes[0] == 0xfe && bytes[1] == 0xff) {
        return ConfigEncoding::Utf16BigEndian;
    }
    if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf) {
        return ConfigEncoding::Utf8Bom;
    }
    return ConfigEncoding::Utf8;
}

static void appendUtf8(std::string& out, std::uint32_t codepoint) {
    if (codepoint <= 0x7f) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        out.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
}

static std::string decodeConfig(const std::vector<unsigned char>& bytes) {
    if (bytes.empty()) {
        return {};
    }
    const ConfigEncoding encoding = detectEncoding(bytes);
    if (encoding == ConfigEncoding::Utf16LittleEndian || encoding == ConfigEncoding::Utf16BigEndian) {
        const bool little_endian = encoding == ConfigEncoding::Utf16LittleEndian;
        auto wordAt = [&](std::size_t offset) -> std::uint16_t {
            if (little_endian) {
                return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
            }
            return static_cast<std::uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
        };
        std::string decoded;
        decoded.reserve(bytes.size() / 2);
        for (std::size_t offset = 2; offset + 1 < bytes.size(); offset += 2) {
            std::uint32_t codepoint = wordAt(offset);
            if (codepoint >= 0xd800 && codepoint <= 0xdbff && offset + 3 < bytes.size()) {
                const std::uint16_t low = wordAt(offset + 2);
                if (low >= 0xdc00 && low <= 0xdfff) {
                    codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                    offset += 2;
                }
            }
            appendUtf8(decoded, codepoint);
        }
        return decoded;
    }

    const std::size_t start = encoding == ConfigEncoding::Utf8Bom ? 3 : 0;
    return std::string(reinterpret_cast<const char*>(bytes.data() + start), bytes.size() - start);
}

static std::uint32_t nextUtf8Codepoint(const std::string& text, std::size_t& offset) {
    const std::uint8_t lead = static_cast<std::uint8_t>(text[offset++]);
    if (lead < 0x80) {
        return lead;
    }

    int continuationCount = 0;
    std::uint32_t codepoint = 0;
    std::uint32_t minimum = 0;
    if ((lead & 0xe0) == 0xc0) {
        continuationCount = 1;
        codepoint = lead & 0x1f;
        minimum = 0x80;
    } else if ((lead & 0xf0) == 0xe0) {
        continuationCount = 2;
        codepoint = lead & 0x0f;
        minimum = 0x800;
    } else if ((lead & 0xf8) == 0xf0) {
        continuationCount = 3;
        codepoint = lead & 0x07;
        minimum = 0x10000;
    } else {
        return 0xfffd;
    }

    const std::size_t continuationStart = offset;
    for (int index = 0; index < continuationCount; ++index) {
        if (offset >= text.size()) {
            offset = continuationStart;
            return 0xfffd;
        }
        const std::uint8_t byte = static_cast<std::uint8_t>(text[offset]);
        if ((byte & 0xc0) != 0x80) {
            offset = continuationStart;
            return 0xfffd;
        }
        ++offset;
        codepoint = (codepoint << 6) | (byte & 0x3f);
    }

    if (codepoint < minimum || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
        return 0xfffd;
    }
    return codepoint;
}

static void appendUtf16Word(std::vector<unsigned char>& bytes, std::uint16_t word, bool littleEndian) {
    if (littleEndian) {
        bytes.push_back(static_cast<unsigned char>(word));
        bytes.push_back(static_cast<unsigned char>(word >> 8));
    } else {
        bytes.push_back(static_cast<unsigned char>(word >> 8));
        bytes.push_back(static_cast<unsigned char>(word));
    }
}

static std::vector<unsigned char> encodeConfig(const std::string& text, ConfigEncoding encoding) {
    std::vector<unsigned char> bytes;
    if (encoding == ConfigEncoding::Utf8 || encoding == ConfigEncoding::Utf8Bom) {
        bytes.reserve(text.size() + (encoding == ConfigEncoding::Utf8Bom ? 3 : 0));
        if (encoding == ConfigEncoding::Utf8Bom) {
            bytes.insert(bytes.end(), {0xef, 0xbb, 0xbf});
        }
        bytes.insert(bytes.end(), text.begin(), text.end());
        return bytes;
    }

    const bool littleEndian = encoding == ConfigEncoding::Utf16LittleEndian;
    bytes.reserve(text.size() * 2 + 2);
    if (littleEndian) {
        bytes.insert(bytes.end(), {0xff, 0xfe});
    } else {
        bytes.insert(bytes.end(), {0xfe, 0xff});
    }
    std::size_t offset = 0;
    while (offset < text.size()) {
        std::uint32_t codepoint = nextUtf8Codepoint(text, offset);
        if (codepoint <= 0xffff) {
            appendUtf16Word(bytes, static_cast<std::uint16_t>(codepoint), littleEndian);
            continue;
        }
        codepoint -= 0x10000;
        appendUtf16Word(bytes, static_cast<std::uint16_t>(0xd800 + (codepoint >> 10)), littleEndian);
        appendUtf16Word(bytes, static_cast<std::uint16_t>(0xdc00 + (codepoint & 0x3ff)), littleEndian);
    }
    return bytes;
}

static std::string parseConfigValue(const std::string& rawValue) {
    const std::string rest = trim(rawValue);
    if (rest.size() >= 2 && (rest.front() == '"' || rest.front() == '\'') && rest.back() == rest.front()) {
        return rest.substr(1, rest.size() - 2);
    }
    // GetPrivateProfileString keeps embedded whitespace in an unquoted
    // value. Cutting at the first blank changed valid target values such as
    // map directories, account names and passwords on portable hosts.
    return rest;
}

} // namespace

bool Config::Load(const std::string& path, bool emitDiagnostics) {
    path_ = path;
    kv_.clear();
    loaded_ = false;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (emitDiagnostics) {
            // Log.Add(
            //     LOG_WARN,
            //     "[cfg] cannot open %s (using defaults)",
            //     path.c_str())
            // The caller decides whether missing configuration permits defaults.
            // Log.Add(
            //     LOG_WARN,
            //     "[config] Could not open \"%s\".",
            //     path.c_str())
            L2_LOG_WARN(Log, "[config] Could not open \"%s\".", path.c_str());
        }
        return false;
    }
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    const std::string text = decodeConfig(bytes);
    std::istringstream in(text);
    std::string section, line;
    std::set<std::string> seenSections{""};
    bool useSection = true;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == ';') {
            continue; // blank / comment line
        }
        if (t[0] == '[') { // [Section]
            size_t e = t.find(']');
            if (e != std::string::npos) {
                section = lower(trim(t.substr(1, e - 1)));
                // GetPrivateProfileString searches the first matching section
                // block and the first matching key in that block. A later
                // duplicate section is not merged into the first one.
                useSection = seenSections.insert(section).second;
            }
            continue;
        }
        if (!useSection) {
            continue;
        }
        size_t eq = t.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = lower(trim(t.substr(0, eq)));
        const std::string val = parseConfigValue(t.substr(eq + 1));
        if (!key.empty()) {
            // Win32 profile lookup returns the first duplicate key.
            kv_.emplace(section + "/" + key, val);
        }
    }
    loaded_ = true;
    if (emitDiagnostics) {
        // Log.Add(
        //     LOG_NORMAL,
        //     "[cfg] loaded %s (%zu keys)",
        //     path.c_str(),
        //     kv_.size())
        // Log.Add(
        //     LOG_NORMAL,
        //     "[config] Read \"%s\" (%zu keys).",
        //     path.c_str(),
        //     kv_.size())
        L2_LOG_INFO(Log, "[config] Read \"%s\" (%zu keys).", path.c_str(), kv_.size());
    }
    return true;
}

bool Config::Set(const std::string& section, const std::string& key, const std::string& value) {
    if (path_.empty() || trim(section).empty() || trim(key).empty()) {
        return false;
    }

    std::ifstream input(path_, std::ios::binary);
    if (!input) {
        return false;
    }
    const std::vector<unsigned char> original{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (!input.good() && !input.eof()) {
        return false;
    }

    const ConfigEncoding encoding = detectEncoding(original);
    const std::string text = decodeConfig(original);
    const std::string newline = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const bool hadFinalNewline = !text.empty() && text.back() == '\n';

    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
    }
    if (text.empty()) {
        lines.clear();
    }

    const std::string targetSection = lower(trim(section));
    const std::string targetKey = lower(trim(key));
    bool targetSectionFound = false;
    bool inTargetSection = false;
    bool keyFound = false;
    std::size_t insertAt = lines.size();

    for (std::size_t index = 0; index < lines.size(); ++index) {
        const std::string normalized = trim(lines[index]);
        if (!normalized.empty() && normalized[0] == '[') {
            if (inTargetSection && !keyFound) {
                insertAt = index;
            }
            const std::size_t close = normalized.find(']');
            const std::string currentSection =
                close == std::string::npos ? std::string() : lower(trim(normalized.substr(1, close - 1)));
            inTargetSection = !targetSectionFound && currentSection == targetSection;
            if (inTargetSection) {
                targetSectionFound = true;
                insertAt = index + 1;
            }
            continue;
        }
        if (!inTargetSection) {
            continue;
        }

        insertAt = index + 1;
        const std::size_t equals = normalized.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        if (lower(trim(normalized.substr(0, equals))) != targetKey) {
            continue;
        }

        const std::size_t originalEquals = lines[index].find('=');
        lines[index] = lines[index].substr(0, originalEquals + 1) + value;
        keyFound = true;
        inTargetSection = false;
    }

    if (!keyFound) {
        if (targetSectionFound) {
            lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insertAt), trim(key) + "=" + value);
        } else {
            if (!lines.empty() && !lines.back().empty()) {
                lines.emplace_back();
            }
            lines.push_back("[" + trim(section) + "]");
            lines.push_back(trim(key) + "=" + value);
        }
    }

    std::string updated;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        updated += lines[index];
        if (index + 1 < lines.size() || hadFinalNewline) {
            updated += newline;
        }
    }
    const std::vector<unsigned char> encoded = encodeConfig(updated, encoding);
    std::ofstream output(path_, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    output.flush();
    if (!output) {
        return false;
    }

    kv_[targetSection + "/" + targetKey] = parseConfigValue(value);
    loaded_ = true;
    return true;
}

std::string Config::Get(const std::string& section, const std::string& key, const std::string& def) const {
    auto it = kv_.find(lower(section) + "/" + lower(key));
    return it == kv_.end() ? def : it->second;
}

int Config::GetInt(const std::string& section, const std::string& key, int def) const {
    std::string v = Get(section, key);
    if (v.empty()) {
        return def;
    }
    return std::atoi(v.c_str());
}

} // namespace runtime
