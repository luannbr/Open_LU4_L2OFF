#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace nasc {

// One skill_pch2.txt record. The field order follows the original reader.
struct PrecompiledSkillInfo {
    std::int64_t skillNameId = 0;
    std::int32_t castRange = 0;
    std::int32_t consumeHp = 0;
    std::int32_t consumeMp = 0;
    std::int32_t targetType = 0;
    std::int32_t effectPoint = 0;
    std::int32_t abnormalType = 0;
    std::int32_t abnormalLevel = 0;
    std::int32_t hitCoolTime = 0;
    std::int32_t reuseDelay = 0;
    std::int32_t isMagic = 0;
    std::int32_t isHate = 0;
    std::int32_t attribute = 0;
};

struct PrecompiledLoadReport {
    std::size_t nameFilesLoaded = 0;
    std::size_t nameRecordsRead = 0;
    std::size_t skillRecordsRead = 0;
    std::size_t categoryRecordsRead = 0;
    bool skillVersionMismatch = false;
    std::vector<std::filesystem::path> missingFiles;
};

// Loads PCH files and looks up their names. Loading adds to the existing
// data, like in the original. Use a new object or call Reset() to start
// empty.
class PrecompiledHeaderDB {
public:
    using NameMap = std::map<std::u16string, std::int64_t, std::less<>>;
    using SkillMap = std::map<std::int64_t, PrecompiledSkillInfo>;
    using CategoryMap = std::map<std::int32_t, std::set<std::int32_t>>;

    [[nodiscard]] PrecompiledLoadReport Load(const std::filesystem::path& scriptRoot);

    void Reset() noexcept;
    void ClearNames() noexcept;
    void Set(std::u16string name, std::int64_t value);

    [[nodiscard]] std::optional<std::int64_t> Search(std::u16string_view name) const;
    [[nodiscard]] std::int64_t SearchOrMinusOne(std::u16string_view name) const;
    [[nodiscard]] const PrecompiledSkillInfo* FindSkill(std::int64_t skillNameId) const noexcept;
    [[nodiscard]] bool CategoryContains(std::int32_t category, std::int32_t object) const noexcept;

    [[nodiscard]] const NameMap& Names() const noexcept;
    [[nodiscard]] const SkillMap& Skills() const noexcept;
    [[nodiscard]] const CategoryMap& Categories() const noexcept;

private:
    NameMap names_;
    SkillMap skills_;
    CategoryMap categories_;
};

} // namespace nasc
