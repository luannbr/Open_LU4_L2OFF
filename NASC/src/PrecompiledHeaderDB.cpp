#include "PrecompiledHeaderDB.h"

#include "Platform.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace nasc {
namespace {

constexpr std::array<std::string_view, 11> nameFileOrder{
    "item_pch.txt",
    "npc_pch.txt",
    "skill_pch.txt",
    "quest_pch.txt",
    "manual_pch.txt",
    "category_pch.txt",
    "fishing_pch.txt",
    "option_pch.txt",
    "userquestdata_pch.txt",
    "ensouldata_pch.txt",
    "predefinedlist_pch.txt",
};

constexpr std::array<std::string_view, 2> categoryFileOrder{
    "item_pch2.txt",
    "skill_pch3.txt",
};

constexpr std::int32_t skillDataVersion = -12345;

[[nodiscard]] bool LegacyIsDigit(char16_t character) noexcept {
    return (character >= u'0' && character <= u'9') || character == u'-';
}

[[nodiscard]] bool LegacyIsExtendedDigit(char16_t character) noexcept {
    return LegacyIsDigit(character) || character == u'x' || (character >= u'a' && character <= u'f');
}

[[nodiscard]] std::int64_t ParseLegacyInteger(std::u16string_view text, bool allowHex) {
    std::array<char, 96> ascii{};
    const std::size_t count = std::min(text.size(), ascii.size() - 1U);
    for (std::size_t index = 0; index < count; ++index) {
        if (text[index] > 0x7f) {
            return 0;
        }
        ascii[index] = static_cast<char>(text[index]);
    }

    const char* first = ascii.data();
    const char* last = ascii.data() + count;
    int base = 10;
    if (allowHex && count > 2U && ascii[0] == '0' && (ascii[1] == 'x' || ascii[1] == 'X')) {
        first += 2;
        base = 16;
    }

    std::int64_t value = 0;
    const auto converted = std::from_chars(first, last, value, base);
    if (converted.ec == std::errc::result_out_of_range) {
        return first != last && *first == '-' ? std::numeric_limits<std::int64_t>::min()
                                              : std::numeric_limits<std::int64_t>::max();
    }
    if (converted.ec != std::errc{}) {
        return 0;
    }
    // Like _wtoi64, parsing stops at the first non-digit. from_chars does not
    // need to read the whole string.
    return value;
}

[[nodiscard]] std::int32_t LowI32(std::int64_t value) noexcept {
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(bits));
}

class LegacyPCHScanner {
public:
    explicit LegacyPCHScanner(std::u16string_view input) : input_(input) {}

    [[nodiscard]] std::optional<std::u16string> NextName() {
        while (cursor_ < input_.size() && input_[cursor_] != u'[') {
            ++cursor_;
        }
        if (cursor_ == input_.size()) {
            return std::nullopt;
        }
        ++cursor_;
        std::u16string result;
        while (cursor_ < input_.size() && input_[cursor_] != u']') {
            result.push_back(input_[cursor_++]);
        }
        if (cursor_ == input_.size()) {
            return std::nullopt;
        }
        ++cursor_;
        return result;
    }

    [[nodiscard]] std::optional<std::int64_t> NextInteger(bool allowHex) {
        while (cursor_ < input_.size() && !LegacyIsDigit(input_[cursor_])) {
            ++cursor_;
        }
        if (cursor_ == input_.size()) {
            return std::nullopt;
        }

        std::u16string temporary;
        temporary.push_back(input_[cursor_++]);
        while (cursor_ < input_.size()) {
            const char16_t character = input_[cursor_++];
            const bool accepted = allowHex ? LegacyIsExtendedDigit(character) : LegacyIsDigit(character);
            if (!accepted) {
                break; // The delimiter is consumed, like in the original.
            }
            temporary.push_back(character);
        }
        return ParseLegacyInteger(temporary, allowHex);
    }

private:
    std::u16string_view input_;
    std::size_t cursor_ = 0;
};

[[nodiscard]] bool HasUtf16leBom(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    unsigned char first = 0;
    unsigned char second = 0;
    if (!input.read(reinterpret_cast<char*>(&first), 1) || !input.read(reinterpret_cast<char*>(&second), 1)) {
        return false;
    }
    return first == 0xff && second == 0xfe;
}

[[nodiscard]] std::u16string ReadPCH(const std::filesystem::path& path) {
    if (!HasUtf16leBom(path)) {
        throw std::runtime_error("precompiled header has no UTF-16LE BOM: " + nasc::PathToUtf8(path));
    }
    // CPrecompiledHeaderDB::Load consumes one UTF-16 code unit before parsing.
    // Valid inputs use that unit for the little-endian BOM.
    return nasc::ReadUtf16leFile(path);
}

[[nodiscard]] std::filesystem::path ChildPath(const std::filesystem::path& root, std::string_view name) {
    return root / nasc::PathFromUtf8(name);
}

} // namespace

PrecompiledLoadReport PrecompiledHeaderDB::Load(const std::filesystem::path& scriptRoot) {
    PrecompiledLoadReport report;

    for (const std::string_view filename : nameFileOrder) {
        const std::filesystem::path path = ChildPath(scriptRoot, filename);
        if (!nasc::RegularFileExists(path)) {
            report.missingFiles.push_back(path);
            continue;
        }

        const std::u16string input = ReadPCH(path);
        LegacyPCHScanner scanner(input);
        while (const std::optional<std::u16string> name = scanner.NextName()) {
            // "[]" ends the current file, like in the original.
            if (name->empty()) {
                break;
            }
            const std::optional<std::int64_t> value = scanner.NextInteger(true);
            names_.insert_or_assign(*name, value.value_or(0));
            ++report.nameRecordsRead;
        }
        ++report.nameFilesLoaded;
    }

    const std::filesystem::path skillPath = ChildPath(scriptRoot, "skill_pch2.txt");
    if (!nasc::RegularFileExists(skillPath)) {
        report.missingFiles.push_back(skillPath);
    } else {
        const std::u16string input = ReadPCH(skillPath);
        LegacyPCHScanner scanner(input);
        while (const std::optional<std::int64_t> id = scanner.NextInteger(true)) {
            std::array<std::int32_t, 13> fields{};
            bool complete = true;
            for (std::int32_t& field : fields) {
                const std::optional<std::int64_t> value = scanner.NextInteger(false);
                if (!value) {
                    complete = false;
                    break;
                }
                field = LowI32(*value);
            }
            if (!complete) {
                break;
            }
            if (fields.back() != skillDataVersion) {
                report.skillVersionMismatch = true;
                break;
            }
            if (*id == 0) {
                continue;
            }

            PrecompiledSkillInfo info{
                *id,
                fields[0],
                fields[1],
                fields[2],
                fields[3],
                fields[4],
                fields[5],
                fields[6],
                fields[7],
                fields[8],
                fields[9],
                fields[10],
                fields[11],
            };
            // The first duplicate wins, like in the original.
            skills_.try_emplace(*id, info);
            ++report.skillRecordsRead;
        }
    }

    for (const std::string_view filename : categoryFileOrder) {
        const std::filesystem::path path = ChildPath(scriptRoot, filename);
        if (!nasc::RegularFileExists(path)) {
            report.missingFiles.push_back(path);
            continue;
        }
        const std::u16string input = ReadPCH(path);
        LegacyPCHScanner scanner(input);
        while (const std::optional<std::u16string> name = scanner.NextName()) {
            if (name->empty()) {
                break;
            }
            const std::optional<std::int64_t> category = scanner.NextInteger(false);
            const std::optional<std::int64_t> object = scanner.NextInteger(false);
            if (!category || !object) {
                break;
            }
            categories_[LowI32(*category)].insert(LowI32(*object));
            ++report.categoryRecordsRead;
        }
    }

    return report;
}

void PrecompiledHeaderDB::Reset() noexcept {
    names_.clear();
    skills_.clear();
    categories_.clear();
}

void PrecompiledHeaderDB::ClearNames() noexcept {
    // The original Clear removes only the names.
    names_.clear();
}

void PrecompiledHeaderDB::Set(std::u16string name, std::int64_t value) { names_.insert_or_assign(std::move(name), value); }

std::optional<std::int64_t> PrecompiledHeaderDB::Search(std::u16string_view name) const {
    const auto found = names_.find(name);
    if (found == names_.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::int64_t PrecompiledHeaderDB::SearchOrMinusOne(std::u16string_view name) const { return Search(name).value_or(-1); }

const PrecompiledSkillInfo* PrecompiledHeaderDB::FindSkill(std::int64_t skillNameId) const noexcept {
    const auto found = skills_.find(skillNameId);
    return found == skills_.end() ? nullptr : &found->second;
}

bool PrecompiledHeaderDB::CategoryContains(std::int32_t category, std::int32_t object) const noexcept {
    const auto found = categories_.find(category);
    return found != categories_.end() && found->second.contains(object);
}

const PrecompiledHeaderDB::NameMap& PrecompiledHeaderDB::Names() const noexcept { return names_; }

const PrecompiledHeaderDB::SkillMap& PrecompiledHeaderDB::Skills() const noexcept { return skills_; }

const PrecompiledHeaderDB::CategoryMap& PrecompiledHeaderDB::Categories() const noexcept { return categories_; }

} // namespace nasc
