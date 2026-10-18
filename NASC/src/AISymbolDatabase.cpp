#include "AISymbolDatabase.h"

#include "Encoding.h"
#include "Platform.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace nasc {
namespace {

[[nodiscard]] bool HasUtf16leBom(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    unsigned char first = 0;
    unsigned char second = 0;
    if (!input.read(reinterpret_cast<char*>(&first), 1) || !input.read(reinterpret_cast<char*>(&second), 1)) {
        return false;
    }
    return first == 0xff && second == 0xfe;
}

[[nodiscard]] std::int32_t LegacyWtoi(std::u16string_view text) noexcept {
    char buffer[32]{};
    const std::size_t count = std::min(text.size(), sizeof(buffer) - 1U);
    for (std::size_t index = 0; index < count; ++index) {
        if (text[index] > 0x7f) {
            return 0;
        }
        buffer[index] = static_cast<char>(text[index]);
    }
    std::int32_t value = 0;
    const auto converted = std::from_chars(buffer, buffer + count, value, 10);
    return converted.ec == std::errc{} ? value : 0;
}

[[nodiscard]] std::vector<std::u16string_view> Split(std::u16string_view text, char16_t delimiter) {
    std::vector<std::u16string_view> result;
    std::size_t begin = 0;
    for (;;) {
        const std::size_t end = text.find(delimiter, begin);
        if (end == std::u16string_view::npos) {
            result.push_back(text.substr(begin));
            return result;
        }
        result.push_back(text.substr(begin, end - begin));
        begin = end + 1U;
    }
}

[[nodiscard]] bool ParseTypeMap(std::u16string_view payload, AISymbolTypeMap& output) {
    for (const std::u16string_view entry : Split(payload, u',')) {
        const std::vector<std::u16string_view> pair = Split(entry, u':');
        if (pair.size() < 2U) {
            return false;
        }
        const ScriptTypeDescriptor* const type = FindScriptType(LegacyWtoi(pair[1]));
        output.insert_or_assign(std::u16string(pair[0]), type);
    }
    return true;
}

} // namespace

AISymbolLoadResult AISymbolDatabase::LoadFile(
    const std::filesystem::path& path, std::int32_t eventTypeId, std::u16string logicalName
) {
    if (FindScriptEventType(eventTypeId) == nullptr) {
        throw std::invalid_argument("symbol event type is outside 0..1");
    }
    AISymbolLoadResult result;
    if (!nasc::RegularFileExists(path)) {
        return result;
    }
    result.loaded = true;

    if (logicalName.empty()) {
        logicalName = nasc::Utf8ToUtf16(nasc::PathToUtf8(path));
    }
    if (loadedFiles_.contains(logicalName)) {
        result.wellFormed = true;
        return result;
    }
    loadedFiles_.insert(std::move(logicalName));

    if (!HasUtf16leBom(path)) {
        throw std::runtime_error("AI symbol file has no UTF-16LE BOM: " + nasc::PathToUtf8(path));
    }
    const std::u16string input = nasc::ReadUtf16leFile(path);

    std::u16string currentClass;
    int state = 0;
    for (std::size_t begin = 0; begin < input.size();) {
        const std::size_t newline = input.find(u'\n', begin);
        const std::size_t end = newline == std::u16string::npos ? input.size() : newline;
        const std::u16string_view line(input.data() + begin, end - begin);
        begin = newline == std::u16string::npos ? input.size() : newline + 1U;
        if (line.empty()) {
            continue;
        }
        // The original writes .sym files in Windows text mode, so every line ends
        // with CR. The original reader removes this last character.
        if (line.back() != u'\r') {
            return result;
        }

        if (line.find(u"class : ") != std::u16string_view::npos) {
            if (line.size() < 9U) {
                return result;
            }
            currentClass.assign(line.substr(8U, line.size() - 9U));
            classes_.emplace(eventTypeId, currentClass);
            state = 0;
            ++result.classes;
            continue;
        }
        if (line.find(u"parameter : ") != std::u16string_view::npos) {
            if (state != 0 || currentClass.empty() || line.size() < 13U) {
                return result;
            }
            AISymbolTypeMap values;
            if (!ParseTypeMap(line.substr(12U, line.size() - 13U), values)) {
                return result;
            }
            classParameters_.insert_or_assign(currentClass, std::move(values));
            state = 1;
            ++result.parameterLines;
            continue;
        }
        if (line.find(u"property : ") != std::u16string_view::npos) {
            if ((state != 0 && state != 1) || currentClass.empty() || line.size() < 12U) {
                return result;
            }
            AISymbolTypeMap values;
            if (!ParseTypeMap(line.substr(11U, line.size() - 12U), values)) {
                return result;
            }
            classProperties_.insert_or_assign(currentClass, std::move(values));
            state = 2;
            ++result.propertyLines;
        }
        // Unknown lines are ignored, like in the original.
    }

    result.wellFormed = true;
    return result;
}

void AISymbolDatabase::Clear() noexcept {
    classes_.clear();
    classParameters_.clear();
    classProperties_.clear();
    loadedFiles_.clear();
}

bool AISymbolDatabase::ContainsClass(std::int32_t eventTypeId, std::u16string_view name) const {
    return classes_.contains({eventTypeId, std::u16string(name)});
}

const AISymbolTypeMap* AISymbolDatabase::Parameters(std::u16string_view className) const {
    const auto found = classParameters_.find(className);
    return found == classParameters_.end() ? nullptr : &found->second;
}

const AISymbolTypeMap* AISymbolDatabase::Properties(std::u16string_view className) const {
    const auto found = classProperties_.find(className);
    return found == classProperties_.end() ? nullptr : &found->second;
}

const std::set<std::pair<std::int32_t, std::u16string>>& AISymbolDatabase::Classes() const noexcept { return classes_; }

const std::map<std::u16string, AISymbolTypeMap, std::less<>>& AISymbolDatabase::ClassParameters() const noexcept {
    return classParameters_;
}

const std::map<std::u16string, AISymbolTypeMap, std::less<>>& AISymbolDatabase::ClassProperties() const noexcept {
    return classProperties_;
}

} // namespace nasc
