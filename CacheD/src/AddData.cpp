#include "Shared/src/Runtime/Utf8.h"
#include "Shared/src/Runtime/ResourceDirectories.h"
#include "AddData.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/DBConn.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/Log.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

constexpr int kTargetWideEof = 0xffff;
// constexpr const wchar_t* kSourceFile = L"AddData.cpp"
// Noise: the source label belonged only to the disabled per-row dumps.

constexpr const wchar_t* kItemDataTargetPath = L"..\\Script\\itemcache.txt";
constexpr const wchar_t* kSkillDataTargetPath = L"..\\Script\\GMskilldata.txt";
constexpr const wchar_t* kQuestDataTargetPath = L"..\\Script\\questcomp.txt";

class TargetWideFile {
public:
    bool Open(const char* fileName) {
#if defined(_WIN32)
        return OpenPath(std::filesystem::path(L"..\\Script") / fileName);
#else
        if (OpenPath(std::filesystem::path("../Script") / fileName)) {
            return true;
        }
        return OpenPath(std::filesystem::path("../script") / fileName);
#endif
    }

    int Get() noexcept {
        if (m_cursor >= m_text.size())
            return kTargetWideEof;
        return static_cast<std::uint16_t>(m_text[m_cursor++]);
    }

private:
    bool OpenPath(const std::filesystem::path& path) {
        std::ifstream input(runtime::ResolveResourcePath(path), std::ios::binary);
        if (!input)
            return false;

        const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if (bytes.size() % 2 != 0) {
            L2_LOG_WARN(Log,
                "[data import] UTF-16 input '%s' has an incomplete final code unit (%zu bytes read). The final byte is ignored; import continues.",
                runtime::ResolveResourcePath(path).string().c_str(), bytes.size());
        }
        m_text.clear();
        m_text.reserve(bytes.size() / 2);
        bool reportedEndMarker = false;
        for (std::size_t index = 0; index + 1 < bytes.size(); index += 2) {
            // All three import callers discard the first code unit as the BOM.
            // Noise: that discarded unit cannot terminate field parsing.
            if (index != 0 && !reportedEndMarker && bytes[index] == 0xff && bytes[index + 1] == 0xff) {
                L2_LOG_WARN(Log,
                    "[data import] UTF-16 input '%s' contains 0xFFFF at byte offset %zu. The reader also uses this value for end of input; import may stop before the remaining data.",
                    runtime::ResolveResourcePath(path).string().c_str(), index);
                reportedEndMarker = true;
            }
            m_text.push_back(
                static_cast<char16_t>(
                    static_cast<std::uint16_t>(bytes[index]) | (static_cast<std::uint16_t>(bytes[index + 1]) << 8U)
                )
            );
        }
        m_cursor = 0;
        return true;
    }

    std::vector<char16_t> m_text;
    std::size_t m_cursor = 0;
};

struct ParsedField {
    std::wstring text;
    int terminator = kTargetWideEof;
};

void AppendFiltered(std::wstring& output, int character, bool quoteApostrophes, bool removeBraces, bool collapseSemicolons) {
    if (character == L'[' || character == L']' || (removeBraces && (character == L'{' || character == L'}'))) {
        return;
    }
    if (collapseSemicolons && character == L';' && !output.empty() && output.back() == L';') {
        return;
    }
    output.push_back(static_cast<wchar_t>(character));
    if (quoteApostrophes && character == L'\'') {
        // This doubling remains in the current prepared-statement path. It
        // is therefore data transformation, not SQL-literal escaping added
        // by the portable PostgreSQL transport.
        output.push_back(L'\'');
    }
}

ParsedField ReadTabField(TargetWideFile& input, int rawLimit, bool quoteApostrophes) {
    ParsedField result;
    for (int count = 0; count < rawLimit; ++count) {
        result.terminator = input.Get();
        if (result.terminator == L'\t' || result.terminator == L'\n' || result.terminator == kTargetWideEof) {
            break;
        }
        AppendFiltered(result.text, result.terminator, quoteApostrophes, false, false);
    }
    return result;
}

ParsedField ReadCarriageReturnField(
    TargetWideFile& input,
    int rawLimit,
    bool stopAtTab,
    bool stopAtLineFeed,
    bool quoteApostrophes,
    bool removeBraces,
    bool collapseSemicolons,
    bool retainConsumedLineFeed
) {
    ParsedField result;
    for (int count = 0; count < rawLimit; ++count) {
        result.terminator = input.Get();
        if (result.terminator == L'\r') {
            const int consumed = input.Get();
            if (retainConsumedLineFeed)
                result.terminator = consumed;
            break;
        }
        if (result.terminator == kTargetWideEof || (stopAtTab && result.terminator == L'\t') ||
            (stopAtLineFeed && result.terminator == L'\n')) {
            break;
        }
        AppendFiltered(result.text, result.terminator, quoteApostrophes, removeBraces, collapseSemicolons);
    }
    return result;
}

std::int32_t ParseTargetLong(const std::wstring& text) noexcept {
    std::size_t cursor = 0;
    while (cursor < text.size() &&
        (text[cursor] == L' ' || text[cursor] == L'\t' || text[cursor] == L'\r' || text[cursor] == L'\n')) {
        ++cursor;
    }

    bool negative = false;
    if (cursor < text.size() && (text[cursor] == L'+' || text[cursor] == L'-')) {
        negative = text[cursor] == L'-';
        ++cursor;
    }

    std::uint32_t magnitude = 0;
    while (cursor < text.size() && text[cursor] >= L'0' && text[cursor] <= L'9') {
        magnitude = magnitude * 10U + static_cast<std::uint32_t>(text[cursor] - L'0');
        ++cursor;
    }
    return static_cast<std::int32_t>(negative ? 0U - magnitude : magnitude);
}

void ShowFileOpenFailure(const wchar_t* title, const std::wstring& message) {
#if defined(_WIN32)
    ::MessageBoxW(nullptr, message.c_str(), title, MB_OK);
#else
    // MessageBoxW is the only non-server side effect in these three target
    // paths. A terminal diagnostic is its headless cross-platform transport.
    // std::fwprintf(stderr, L"%ls: %ls\n", title, message.c_str())
    // Noise: OpenDataFile emits the same failure through the common logger.
    (void)title;
    (void)message;
#endif
}

bool OpenDataFile(
    TargetWideFile& input,
    const char* portableName,
    const wchar_t* targetPath,
    const wchar_t* title,
    const wchar_t* fixedMessage
) {
    if (input.Open(portableName))
        return true;

    // Preserve the caller-visible error before diagnostic allocation/UI work.
    const int diagnosticErrno = errno;
    std::wstring message = fixedMessage;
    if (message.empty()) {
        message = L"Add item data fail. Cannot find file[";
        message += targetPath;
        message += L']';
    }
    ShowFileOpenFailure(title, message);
    // Log.AddT(LOG_ERROR, L"Cannot find [%s]", targetPath)
    // Original (after message construction and UI): const int diagnosticErrno = errno.
    try {
#if defined(_WIN32)
        const auto path = runtime::Utf8Bytes(runtime::ResolveResourcePath(std::filesystem::path(L"..\\Script") / portableName).u8string());
        // Log.Add(LOG_ERROR,
        //     "[data import] Cannot open \"%s\", import skipped before database cleanup.", path.c_str())
        L2_LOG_ERROR(Log,
            "[data import] Cannot open \"%s\"; import skipped before database cleanup.", path.c_str());
#else
        const auto first = runtime::Utf8Bytes(runtime::ResolveResourcePath(std::filesystem::path("../Script") / portableName).u8string());
        const auto second = runtime::Utf8Bytes(runtime::ResolveResourcePath(std::filesystem::path("../script") / portableName).u8string());
        if (first == second) {
            // Log.Add(LOG_ERROR,
            //     "[data import] Cannot open \"%s\", import skipped before database cleanup.", first.c_str())
            L2_LOG_ERROR(Log,
                "[data import] Cannot open \"%s\"; import skipped before database cleanup.", first.c_str());
        } else {
            // Log.Add(LOG_ERROR,
            //     "[data import] Cannot open \"%s\" or \"%s\". Import skipped before database cleanup.",
            //     first.c_str(), second.c_str())
            L2_LOG_ERROR(Log,
                "[data import] Cannot open \"%s\" or \"%s\"; import skipped before database cleanup.",
                first.c_str(), second.c_str());
        }
#endif
    } catch (...) {
        // Log.Add(LOG_ERROR,
        //     "[data import] Cannot open script \"%s\". Resolved path unavailable. Import skipped before database cleanup.",
        //     portableName)
        L2_LOG_ERROR(Log,
            "[data import] Cannot open script \"%s\"; resolved path unavailable. Import skipped before database cleanup.",
            portableName);
    }
    errno = diagnosticErrno;
    return false;
}

// void ExecuteItemInsert(
bool ExecuteItemInsert(
    DBConn& connection,
    std::int32_t id,
    const std::wstring& name,
    const std::wstring& description,
    const std::wstring& consumeType,
    std::int32_t isQuest
) {
    constexpr const wchar_t* statement = L"{Call lin_InsertItemName (?, ?, ?, ?, ?)}";
    // The target binds once to stack slots whose addresses are reused by
    // every helper invocation. Later out-of-range SQLBindParameter calls are
    // ignored and the original ODBC bindings observe the refreshed values.
    // libpq creates an argument vector instead, so clearing that vector
    // is the transport-equivalent way to keep one five-parameter execution
    // per parsed row.
    connection.ResetParam();
    connection.Prepare(statement);
    connection.BindParameter(&id);
    connection.BindParameter(name.c_str());
    connection.BindParameter(description.c_str());
    connection.BindParameter(consumeType.c_str());
    connection.BindParameter(&isQuest);
    // connection.ExecutePrepared(statement)
    return connection.ExecutePrepared(statement);
}

// void ExecuteSkillInsert(
bool ExecuteSkillInsert(
    DBConn& connection,
    std::int32_t id,
    std::int32_t level,
    const std::wstring& name,
    const std::wstring& description,
    std::int32_t isMagic,
    const std::wstring& activateType
) {
    constexpr const wchar_t* statement = L"{Call lin_InsertSkillName (?, ?, ?, ?, ?, ?)}";
    connection.ResetParam();
    connection.Prepare(statement);
    connection.BindParameter(&id);
    connection.BindParameter(&level);
    connection.BindParameter(name.c_str());
    connection.BindParameter(description.c_str());
    connection.BindParameter(&isMagic);
    connection.BindParameter(activateType.c_str());
    // connection.ExecutePrepared(statement)
    return connection.ExecutePrepared(statement);
}

} // namespace

CAddData g_addData;

CAddData::CAddData() = default;
CAddData::~CAddData() = default;

bool CAddData::AddItemData() {
    L2_GUARD_FUNCTION();

    TargetWideFile input;
    if (!OpenDataFile(input, "itemcache.txt", kItemDataTargetPath, L"Add item data fail", L"")) {
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* flush = L"{Call lin_FlushItemName}";
    connection.Prepare(flush);
    // connection.ExecutePrepared(flush)
    const bool cleanupSucceeded = connection.ExecutePrepared(flush);
    if (!cleanupSucceeded) {
        // Log.Add(LOG_ERROR,
        //     "[data import] Cannot clear stored item names before importing itemcache.txt, row inserts will still be attempted.")
        L2_LOG_ERROR(Log,
            "[data import] Cannot clear stored item names before importing itemcache.txt; row inserts will still be attempted.");
    }
    // These counters observe database results only. They do not control import behavior.
    std::size_t inserted = 0;
    std::size_t failed = 0;
    const char* incompleteField = nullptr;
    const auto report = [&](const char* missingField) {
        // Log.Add(!cleanupSucceeded || failed != 0 ? LOG_ERROR : missingField != nullptr ? LOG_WARN : LOG_NORMAL,
        //     "[data import] Item names from itemcache.txt: %zu row inserts succeeded, %zu failed. Cleanup %s. %s%s",
        //     inserted, failed, cleanupSucceeded ? "succeeded" : "failed",
        //     missingField != nullptr ? "Final record skipped. Missing field or terminator: " : "End of file reached.",
        //     missingField != nullptr ? missingField : "")
        L2_LOG(Log, !cleanupSucceeded || failed != 0 ? LOG_ERROR : missingField != nullptr ? LOG_WARN : LOG_NORMAL,
            "[data import] Item names from itemcache.txt: %zu row inserts succeeded, %zu failed; cleanup %s. %s%s",
            inserted, failed, cleanupSucceeded ? "succeeded" : "failed",
            // Original: missingField != nullptr ? "Final record skipped. Missing field or terminator: " : "End of file reached.",
            missingField != nullptr ? "Final record skipped; missing field or terminator: " : "Parser reached the end-of-input marker.",
            missingField != nullptr ? missingField : "");
    };

    // Current 0x0040d214 discards the first UTF-16 code unit (the BOM).
    input.Get();
    for (;;) {
        ParsedField idText = ReadTabField(input, 5, false);
        // if (idText.terminator == kTargetWideEof)
        //     return true
        if (idText.terminator == kTargetWideEof) {
            report(idText.text.empty() ? nullptr : "ID terminator");
            return true;
        }

        ParsedField name = ReadTabField(input, 512, true);
        if (name.text.empty())
            name.text = L"0";
        // if (name.terminator == kTargetWideEof)
        //     break.
        if (name.terminator == kTargetWideEof) {
            incompleteField = "name";
            break;
        }

        ParsedField description = ReadCarriageReturnField(input, 512, true, true, true, false, false, false);
        if (description.text.empty())
            description.text = L" ";

        ParsedField consumeType = ReadCarriageReturnField(input, 512, true, true, true, false, false, true);
        if (consumeType.text.empty())
            consumeType.text = L" ";
        // if (consumeType.terminator == kTargetWideEof)
        //     break.
        if (consumeType.terminator == kTargetWideEof) {
            incompleteField = "consume type";
            break;
        }

        ParsedField isQuestText = ReadCarriageReturnField(input, 256, true, true, false, false, false, true);
        if (isQuestText.text.empty())
            isQuestText.text = L"0";
        // if (isQuestText.terminator == kTargetWideEof)
        //     break.
        if (isQuestText.terminator == kTargetWideEof) {
            incompleteField = "quest flag";
            break;
        }

        const std::int32_t id = ParseTargetLong(idText.text);
        const std::int32_t isQuest = ParseTargetLong(isQuestText.text);
        // ExecuteItemInsert(connection, id, name.text, description.text, consumeType.text, isQuest)
        if (ExecuteItemInsert(connection, id, name.text, description.text, consumeType.text, isQuest)) {
            ++inserted;
        } else {
            if (failed == 0) {
                // Log.Add(LOG_ERROR,
                //     "[data import] First failed item-name insert from itemcache.txt: item ID %d. Import continues. Total failures follow in the summary.",
                //     id)
                L2_LOG_ERROR(Log,
                    "[data import] First failed item-name insert from itemcache.txt: item ID %d. Import continues; total failures follow in the summary.",
                    id);
            }
            ++failed;
        }
        // Log.AddT(
        //     LOG_NORMAL, L"[%s][%d] %d, %s, %s, %s, %d", kSourceFile, 0xbc, id, name.text.c_str(), description.text.c_str(),
        //     consumeType.text.c_str(), isQuest
        // )
        // Noise: per-row content dumps flood the console and do not prove database success. The import summary reports actual execution results.
    }
    report(incompleteField);
    return true;
}

bool CAddData::AddSkillData() {
    L2_GUARD_FUNCTION();

    TargetWideFile input;
    if (!OpenDataFile(
            input, "GMskilldata.txt", kSkillDataTargetPath, L"Add skill data fail", L"Add skill data fail. Cannot find file"
        )) {
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* flush = L"{Call lin_FlushSkillName}";
    connection.Prepare(flush);
    // connection.ExecutePrepared(flush)
    const bool cleanupSucceeded = connection.ExecutePrepared(flush);
    if (!cleanupSucceeded) {
        // Log.Add(LOG_ERROR,
        //     "[data import] Cannot clear stored skill names before importing GMskilldata.txt, row inserts will still be attempted.")
        L2_LOG_ERROR(Log,
            "[data import] Cannot clear stored skill names before importing GMskilldata.txt; row inserts will still be attempted.");
    }
    // These counters observe database results only. They do not control import behavior.
    std::size_t inserted = 0;
    std::size_t failed = 0;
    const char* incompleteField = nullptr;
    const auto report = [&](const char* missingField) {
        // Log.Add(!cleanupSucceeded || failed != 0 ? LOG_ERROR : missingField != nullptr ? LOG_WARN : LOG_NORMAL,
        //     "[data import] Skill names from GMskilldata.txt: %zu row inserts succeeded, %zu failed. Cleanup %s. %s%s",
        //     inserted, failed, cleanupSucceeded ? "succeeded" : "failed",
        //     missingField != nullptr ? "Final record skipped. Missing field or terminator: " : "End of file reached.",
        //     missingField != nullptr ? missingField : "")
        L2_LOG(Log, !cleanupSucceeded || failed != 0 ? LOG_ERROR : missingField != nullptr ? LOG_WARN : LOG_NORMAL,
            "[data import] Skill names from GMskilldata.txt: %zu row inserts succeeded, %zu failed; cleanup %s. %s%s",
            inserted, failed, cleanupSucceeded ? "succeeded" : "failed",
            // Original: missingField != nullptr ? "Final record skipped. Missing field or terminator: " : "End of file reached.",
            missingField != nullptr ? "Final record skipped; missing field or terminator: " : "Parser reached the end-of-input marker.",
            missingField != nullptr ? missingField : "");
    };

    input.Get();
    for (;;) {
        ParsedField idText = ReadTabField(input, 5, false);
        // if (idText.terminator == kTargetWideEof)
        //     return true
        if (idText.terminator == kTargetWideEof) {
            report(idText.text.empty() ? nullptr : "ID terminator");
            return true;
        }

        ParsedField levelText = ReadTabField(input, 5, false);
        if (levelText.text.empty())
            levelText.text = L"0";
        // if (levelText.terminator == kTargetWideEof)
        //     break.
        if (levelText.terminator == kTargetWideEof) {
            incompleteField = "level";
            break;
        }

        ParsedField name = ReadCarriageReturnField(input, 256, true, false, true, false, false, false);
        if (name.text.empty())
            name.text = L" ";

        ParsedField description = ReadCarriageReturnField(input, 256, true, false, true, false, false, false);
        if (description.text.empty())
            description.text = L" ";

        ParsedField isMagicText = ReadCarriageReturnField(input, 3, true, false, true, false, false, false);
        if (isMagicText.text.empty())
            isMagicText.text = L"0";

        ParsedField activateType = ReadCarriageReturnField(input, 5, false, false, true, false, false, true);
        if (activateType.text.empty())
            activateType.text = L"A";
        // if (activateType.terminator == kTargetWideEof)
        //     break.
        if (activateType.terminator == kTargetWideEof) {
            incompleteField = "activation type";
            break;
        }

        const std::int32_t id = ParseTargetLong(idText.text);
        const std::int32_t level = ParseTargetLong(levelText.text);
        const std::int32_t isMagic = ParseTargetLong(isMagicText.text);
        // ExecuteSkillInsert(connection, id, level, name.text, description.text, isMagic, activateType.text)
        if (ExecuteSkillInsert(connection, id, level, name.text, description.text, isMagic, activateType.text)) {
            ++inserted;
        } else {
            if (failed == 0) {
                // Log.Add(LOG_ERROR,
                //     "[data import] First failed skill-name insert from GMskilldata.txt: skill ID %d, level %d. Import continues. Total failures follow in the summary.",
                //     id, level)
                L2_LOG_ERROR(Log,
                    "[data import] First failed skill-name insert from GMskilldata.txt: skill ID %d, level %d. Import continues; total failures follow in the summary.",
                    id, level);
            }
            ++failed;
        }
        // Log.AddT(
        //     LOG_NORMAL, L"[%s][%d] %d, %d, %s, %s, %d, %s", kSourceFile, 0x16b, id, level, name.text.c_str(),
        //     description.text.c_str(), isMagic, activateType.text.c_str()
        // )
        // Noise: per-row content dumps flood the console and do not prove database success. The import summary reports actual execution results.
    }
    report(incompleteField);
    return true;
}

bool CAddData::AddQuestData() {
    L2_GUARD_FUNCTION();

    TargetWideFile input;
    if (!OpenDataFile(
            input, "questcomp.txt", kQuestDataTargetPath, L"Add quest data fail", L"Add quest data fail. Cannot find file"
        )) {
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* flush = L"{Call lin_FlushQuestName}";
    connection.Prepare(flush);
    // connection.ExecutePrepared(flush)
    const bool cleanupSucceeded = connection.ExecutePrepared(flush);
    if (!cleanupSucceeded) {
        // Log.Add(LOG_ERROR,
        //     "[data import] Cannot clear stored quest names before importing questcomp.txt, row inserts will still be attempted.")
        L2_LOG_ERROR(Log,
            "[data import] Cannot clear stored quest names before importing questcomp.txt; row inserts will still be attempted.");
    }
    // These counters observe database results only. They do not control import behavior.
    std::size_t inserted = 0;
    std::size_t failed = 0;
    const char* incompleteField = nullptr;
    const auto report = [&](const char* missingField) {
        // Log.Add(!cleanupSucceeded || failed != 0 ? LOG_ERROR : missingField != nullptr ? LOG_WARN : LOG_NORMAL,
        //     "[data import] Quest names from questcomp.txt: %zu row inserts succeeded, %zu failed. Cleanup %s. %s%s",
        //     inserted, failed, cleanupSucceeded ? "succeeded" : "failed",
        //     missingField != nullptr ? "Final record skipped. Missing field or terminator: " : "End of file reached.",
        //     missingField != nullptr ? missingField : "")
        L2_LOG(Log, !cleanupSucceeded || failed != 0 ? LOG_ERROR : missingField != nullptr ? LOG_WARN : LOG_NORMAL,
            "[data import] Quest names from questcomp.txt: %zu row inserts succeeded, %zu failed; cleanup %s. %s%s",
            inserted, failed, cleanupSucceeded ? "succeeded" : "failed",
            // Original: missingField != nullptr ? "Final record skipped. Missing field or terminator: " : "End of file reached.",
            missingField != nullptr ? "Final record skipped; missing field or terminator: " : "Parser reached the end-of-input marker.",
            missingField != nullptr ? missingField : "");
    };

    input.Get();
    for (;;) {
        ParsedField idText = ReadTabField(input, 7, false);
        // if (idText.terminator == kTargetWideEof)
        //     return true
        if (idText.terminator == kTargetWideEof) {
            report(idText.text.empty() ? nullptr : "ID terminator");
            return true;
        }

        ParsedField name = ReadTabField(input, 256, true);
        if (name.text.empty())
            name.text = L" ";
        // if (name.terminator == kTargetWideEof)
        //     break.
        if (name.terminator == kTargetWideEof) {
            incompleteField = "name";
            break;
        }

        ParsedField data = ReadCarriageReturnField(input, 511, false, false, true, true, true, true);
        if (data.text.empty())
            data.text = L" ";
        // if (data.terminator == kTargetWideEof)
        //     break.
        if (data.terminator == kTargetWideEof) {
            incompleteField = "quest data";
            break;
        }

        const std::int32_t id = ParseTargetLong(idText.text);
        constexpr const wchar_t* statement = L"{Call lin_InsertQuestName (?, ?, ?)}";
        // Current ODBC keeps its first three bindings pointed at the same
        // fixed method-local buffers. Recreate that effect for libpq's
        // per-execution argument carrier.
        connection.ResetParam();
        connection.Prepare(statement);
        connection.BindParameter(&id);
        connection.BindParameter(name.text.c_str());
        connection.BindParameter(data.text.c_str());
        // connection.ExecutePrepared(statement)
        if (connection.ExecutePrepared(statement)) {
            ++inserted;
        } else {
            if (failed == 0) {
                // Log.Add(LOG_ERROR,
                //     "[data import] First failed quest-name insert from questcomp.txt: quest ID %d. Import continues. Total failures follow in the summary.",
                //     id)
                L2_LOG_ERROR(Log,
                    "[data import] First failed quest-name insert from questcomp.txt: quest ID %d. Import continues; total failures follow in the summary.",
                    id);
            }
            ++failed;
        }
        // Log.AddT(LOG_NORMAL, L"[%s][%d] %d, %s, %s", kSourceFile, 0x1d7, id, name.text.c_str(), data.text.c_str())
        // Noise: per-row content dumps flood the console and do not prove database success. The import summary reports actual execution results.
    }
    report(incompleteField);
    return true;
}
